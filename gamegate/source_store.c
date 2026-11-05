#include <stdlib.h>
#include <string.h>
#include <formats/rjson.h>
#include "source_store_io.h"

#define GG_STORE_U64_MAX (~(uint64_t)0)

typedef struct gg_store_registry
{
   uint64_t generation;
   uint64_t next_id;
   size_t count;
   gg_source_record_t records[GG_CATALOG_MAX_SOURCES];
} gg_store_registry_t;

struct gg_source_store
{
   gg_store_registry_t *registry;
   const gg_store_io_t *io;
   void *io_context;
   char *root;
};

static void gg_store_hex(uint64_t value, char *out)
{
   static const char digits[] = "0123456789abcdef";
   unsigned i;
   out[16] = '\0';
   for (i = 16; i > 0; i--)
   {
      out[i - 1] = digits[value & 15];
      value >>= 4;
   }
}

static bool gg_store_unhex(const char *text, size_t length, uint64_t *out)
{
   uint64_t value = 0;
   size_t i;
   if (length != 16)
      return false;
   for (i = 0; i < length; i++)
   {
      unsigned digit;
      if (text[i] >= '0' && text[i] <= '9')
         digit = (unsigned)(text[i] - '0');
      else if (text[i] >= 'a' && text[i] <= 'f')
         digit = (unsigned)(text[i] - 'a') + 10;
      else
         return false;
      value = (value << 4) | digit;
   }
   *out = value;
   return true;
}

static bool gg_store_remote_id_valid(const char *id)
{
   const unsigned char *p = (const unsigned char*)id;
   if (!*p)
      return false;
   for (; *p; p++)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
               || (*p >= '0' && *p <= '9') || *p == '.'
               || *p == '-' || *p == '_'))
         return false;
   return true;
}

static char *gg_store_path(const gg_source_store_t *store, const char *name)
{
   size_t root_len = strlen(store->root);
   size_t name_len = strlen(name);
   char *path = (char*)malloc(root_len + name_len + 2);
   if (path)
   {
      memcpy(path, store->root, root_len);
      path[root_len] = '/';
      memcpy(path + root_len + 1, name, name_len + 1);
   }
   return path;
}

static char *gg_store_cache_path(const gg_source_store_t *store,
      const gg_source_record_t *record)
{
   char name[39];
   memcpy(name, record->local_id, 16);
   name[16] = '-';
   gg_store_hex(record->cache_revision, name + 17);
   memcpy(name + 33, ".json", 6);
   return gg_store_path(store, name);
}

static size_t gg_store_find(const gg_store_registry_t *registry, const char *id)
{
   size_t i;
   uint64_t value;
   if (!id || !gg_store_unhex(id, strlen(id), &value) || !value)
      return registry->count;
   for (i = 0; i < registry->count; i++)
      if (!strcmp(registry->records[i].local_id, id))
         break;
   return i;
}

static bool gg_store_json_string(rjson_t *json, char *out, size_t capacity)
{
   size_t length;
   const char *text;
   if (rjson_next(json) != RJSON_STRING)
      return false;
   text = rjson_get_string(json, &length);
   if (!length || length >= capacity || memchr(text, '\0', length))
      return false;
   memcpy(out, text, length);
   out[length] = '\0';
   return true;
}

static bool gg_store_json_hex(rjson_t *json, uint64_t *out)
{
   size_t length;
   const char *text;
   if (rjson_next(json) != RJSON_STRING)
      return false;
   text = rjson_get_string(json, &length);
   return gg_store_unhex(text, length, out);
}

static int gg_store_json_key(rjson_t *json, const char *const *keys,
      unsigned count, unsigned *seen)
{
   const char *text;
   size_t length;
   unsigned i;
   text = rjson_get_string(json, &length);
   for (i = 0; i < count; i++)
      if (strlen(keys[i]) == length && !memcmp(keys[i], text, length))
      {
         if (*seen & (1U << i))
            return -1;
         *seen |= 1U << i;
         return (int)i;
      }
   return -1;
}

static bool gg_store_parse_record(rjson_t *json, gg_source_record_t *record)
{
   static const char *const keys[] = {"local_id", "url", "remote_id",
      "name", "enabled", "last_success", "revision", "cache_revision"};
   unsigned seen = 0;
   enum rjson_type type;
   while ((type = rjson_next(json)) == RJSON_STRING)
   {
      int key = gg_store_json_key(json, keys, 8, &seen);
      bool ok = false;
      switch (key)
      {
         case 0:
            ok = gg_store_json_string(json, record->local_id,
                  sizeof(record->local_id));
            break;
         case 1:
            ok = gg_store_json_string(json, record->url, sizeof(record->url))
               && gg_catalog_https_url_valid(record->url, strlen(record->url));
            break;
         case 2:
            ok = gg_store_json_string(json, record->remote_id,
                  sizeof(record->remote_id))
               && gg_store_remote_id_valid(record->remote_id);
            break;
         case 3:
            ok = gg_store_json_string(json, record->name, sizeof(record->name));
            break;
         case 4:
            type = rjson_next(json);
            ok = type == RJSON_TRUE || type == RJSON_FALSE;
            record->enabled = type == RJSON_TRUE;
            break;
         case 5:
            ok = gg_store_json_hex(json, &record->last_success);
            break;
         case 6:
            ok = gg_store_json_hex(json, &record->revision);
            break;
         case 7:
            ok = gg_store_json_hex(json, &record->cache_revision);
            break;
      }
      if (!ok)
         return false;
   }
   return type == RJSON_OBJECT_END && seen == 255;
}

static bool gg_store_parse_registry(rjson_t *json, gg_store_registry_t *registry)
{
   static const char *const keys[] = {"version", "generation", "next_id",
      "sources"};
   unsigned seen = 0;
   enum rjson_type type;
   size_t i;
   if (rjson_next(json) != RJSON_OBJECT)
      return false;
   while ((type = rjson_next(json)) == RJSON_STRING)
   {
      int key = gg_store_json_key(json, keys, 4, &seen);
      switch (key)
      {
         case 0:
         {
            size_t length;
            const char *text;
            if (rjson_next(json) != RJSON_NUMBER)
               return false;
            text = rjson_get_string(json, &length);
            if (length != 1 || text[0] != '1')
               return false;
            break;
         }
         case 1:
            if (!gg_store_json_hex(json, &registry->generation))
               return false;
            break;
         case 2:
            if (!gg_store_json_hex(json, &registry->next_id))
               return false;
            break;
         case 3:
            if (rjson_next(json) != RJSON_ARRAY)
               return false;
            while ((type = rjson_next(json)) == RJSON_OBJECT)
            {
               if (registry->count == GG_CATALOG_MAX_SOURCES
                     || !gg_store_parse_record(json,
                        &registry->records[registry->count]))
                  return false;
               registry->count++;
            }
            if (type != RJSON_ARRAY_END)
               return false;
            break;
         default:
            return false;
      }
   }
   if (type != RJSON_OBJECT_END || seen != 15 || rjson_next(json) != RJSON_DONE
         || !registry->next_id || registry->generation < registry->next_id - 1)
      return false;
   for (i = 0; i < registry->count; i++)
   {
      const gg_source_record_t *record = &registry->records[i];
      uint64_t id;
      size_t j;
      if (!gg_store_unhex(record->local_id, strlen(record->local_id), &id)
            || !id || id >= registry->next_id || !record->cache_revision
            || record->cache_revision > record->revision
            || record->revision > registry->generation)
         return false;
      for (j = 0; j < i; j++)
         if (!strcmp(record->local_id, registry->records[j].local_id)
               || !strcmp(record->url, registry->records[j].url))
            return false;
   }
   return true;
}

static void gg_store_write_hex(rjsonwriter_t *writer, uint64_t value)
{
   char text[17];
   gg_store_hex(value, text);
   rjsonwriter_add_string(writer, text);
}

static rjsonwriter_t *gg_store_serialize(const gg_store_registry_t *registry)
{
   size_t i;
   rjsonwriter_t *writer = rjsonwriter_open_memory();
   if (!writer)
      return NULL;
   rjsonwriter_raw(writer, "{\"version\":1,\"generation\":", 26);
   gg_store_write_hex(writer, registry->generation);
   rjsonwriter_raw(writer, ",\"next_id\":", 11);
   gg_store_write_hex(writer, registry->next_id);
   rjsonwriter_raw(writer, ",\"sources\":[", 12);
   for (i = 0; i < registry->count; i++)
   {
      const gg_source_record_t *record = &registry->records[i];
      if (i)
         rjsonwriter_raw(writer, ",", 1);
      rjsonwriter_raw(writer, "{\"local_id\":", 12);
      rjsonwriter_add_string(writer, record->local_id);
      rjsonwriter_raw(writer, ",\"url\":", 7);
      rjsonwriter_add_string(writer, record->url);
      rjsonwriter_raw(writer, ",\"remote_id\":", 13);
      rjsonwriter_add_string(writer, record->remote_id);
      rjsonwriter_raw(writer, ",\"name\":", 8);
      rjsonwriter_add_string(writer, record->name);
      rjsonwriter_raw(writer, ",\"enabled\":", 11);
      rjsonwriter_raw(writer, record->enabled ? "true" : "false",
            record->enabled ? 4 : 5);
      rjsonwriter_raw(writer, ",\"last_success\":", 16);
      gg_store_write_hex(writer, record->last_success);
      rjsonwriter_raw(writer, ",\"revision\":", 12);
      gg_store_write_hex(writer, record->revision);
      rjsonwriter_raw(writer, ",\"cache_revision\":", 18);
      gg_store_write_hex(writer, record->cache_revision);
      rjsonwriter_raw(writer, "}", 1);
   }
   rjsonwriter_raw(writer, "]}", 2);
   return writer;
}

static bool gg_store_write_file(gg_source_store_t *store, const char *path,
      const void *data, size_t size)
{
   void *file = store->io->open_write(store->io_context, path);
   bool ok;
   if (!file)
      return false;
   ok = store->io->write(store->io_context, file, data, size);
   if (ok)
      ok = store->io->flush(store->io_context, file);
   if (!store->io->close(store->io_context, file))
      ok = false;
   return ok;
}

/* Registry rename is the only publication point. */
static gg_store_result_t gg_store_commit(gg_source_store_t *store,
      gg_store_registry_t *candidate)
{
   rjsonwriter_t *writer = gg_store_serialize(candidate);
   char *temp = NULL;
   char *dest = NULL;
   const char *data;
   int size = 0;
   gg_store_result_t result = GG_STORE_NOMEM;
   if (!writer)
      return result;
   data = rjsonwriter_get_memory_buffer(writer, &size);
   if (!data)
      goto end;
   if (size < 0 || (size_t)size > GG_SOURCE_STORE_MAX_BYTES)
   {
      result = GG_STORE_LIMIT;
      goto end;
   }
   temp = gg_store_path(store, "registry.tmp");
   dest = gg_store_path(store, "registry.json");
   if (!temp || !dest)
      goto end;
   result = GG_STORE_IO;
   if (!gg_store_write_file(store, temp, data, (size_t)size)
         || !store->io->replace(store->io_context, temp, dest))
   {
      store->io->remove(store->io_context, temp);
      goto end;
   }
   free(store->registry);
   store->registry = candidate;
   result = GG_STORE_OK;
end:
   free(temp);
   free(dest);
   rjsonwriter_free(writer);
   return result;
}

static gg_store_registry_t *gg_store_candidate(const gg_source_store_t *store)
{
   gg_store_registry_t *candidate = (gg_store_registry_t*)malloc(sizeof(*candidate));
   if (candidate)
   {
      memcpy(candidate, store->registry, sizeof(*candidate));
      candidate->generation++;
   }
   return candidate;
}

static gg_store_result_t gg_store_parse_catalog(const void *raw, size_t size,
      gg_catalog_t **out)
{
   gg_catalog_parser_t *parser;
   gg_catalog_parse_status_t status;
   *out = NULL;
   if (!raw || !size)
      return GG_STORE_INVALID;
   if (size > GG_CATALOG_MAX_BYTES)
      return GG_STORE_LIMIT;
   parser = gg_catalog_parser_create(raw, size);
   if (!parser)
      return GG_STORE_NOMEM;
   do
   {
      status = gg_catalog_parser_step(parser, 4096);
   } while (status == GG_CATALOG_PARSE_WORKING);
   if (status == GG_CATALOG_PARSE_DONE)
      *out = gg_catalog_parser_take_result(parser);
   gg_catalog_parser_destroy(parser);
   return *out ? GG_STORE_OK : GG_STORE_INVALID;
}

gg_store_result_t gg_source_store_open(const char *root,
      const gg_store_io_t *io, void *io_context, gg_source_store_t **out)
{
   gg_source_store_t *store;
   gg_store_result_t result;
   char *path;
   void *data = NULL;
   size_t size = 0;
   rjson_t *json;
   if (!out)
      return GG_STORE_INVALID;
   *out = NULL;
   if (!root || !*root || strlen(root) > 4096)
      return GG_STORE_INVALID;
   if (!io)
      io = &gg_store_vfs_io;
   if (!io->read || !io->open_write || !io->write || !io->flush || !io->close
         || !io->replace || !io->remove || !io->mkdir)
      return GG_STORE_INVALID;
   store = (gg_source_store_t*)calloc(1, sizeof(*store));
   if (!store)
      return GG_STORE_NOMEM;
   store->root = (char*)malloc(strlen(root) + 1);
   store->registry = (gg_store_registry_t*)calloc(1, sizeof(*store->registry));
   store->io = io;
   store->io_context = io_context;
   if (!store->root || !store->registry)
   {
      gg_source_store_close(store);
      return GG_STORE_NOMEM;
   }
   strcpy(store->root, root);
   if (!(io->mkdir)(io_context, root))
   {
      gg_source_store_close(store);
      return GG_STORE_IO;
   }
   path = gg_store_path(store, "registry.json");
   if (!path)
   {
      gg_source_store_close(store);
      return GG_STORE_NOMEM;
   }
   result = io->read(io_context, path, GG_SOURCE_STORE_MAX_BYTES, &data, &size);
   free(path);
   if (result == GG_STORE_NOT_FOUND)
   {
      store->registry->next_id = 1;
      result = GG_STORE_OK;
   }
   else if (result == GG_STORE_OK)
   {
      json = rjson_open_buffer(data, size);
      if (!json)
         result = GG_STORE_NOMEM;
      else
      {
         rjson_set_max_depth(json, 4);
         if (!gg_store_parse_registry(json, store->registry))
            result = GG_STORE_CORRUPT;
         rjson_free(json);
      }
   }
   else if (result == GG_STORE_LIMIT)
      result = GG_STORE_CORRUPT;
   free(data);
   if (result != GG_STORE_OK)
      gg_source_store_close(store);
   else
      *out = store;
   return result;
}

void gg_source_store_close(gg_source_store_t *store)
{
   if (!store)
      return;
   free(store->registry);
   free(store->root);
   free(store);
}

const gg_source_record_t *gg_source_store_records(const gg_source_store_t *store,
      size_t *count, uint64_t *generation)
{
   if (count)
      *count = store ? store->registry->count : 0;
   if (generation)
      *generation = store ? store->registry->generation : 0;
   return store ? store->registry->records : NULL;
}

const gg_source_record_t *gg_source_store_get(const gg_source_store_t *store,
      const char *local_id)
{
   size_t index;
   if (!store)
      return NULL;
   index = gg_store_find(store->registry, local_id);
   return index < store->registry->count ? &store->registry->records[index] : NULL;
}

static gg_store_result_t gg_store_publish_catalog(gg_source_store_t *store,
      gg_store_registry_t *candidate, size_t index, const void *raw, size_t size)
{
   char *path = gg_store_cache_path(store, &candidate->records[index]);
   gg_store_result_t result;
   if (!path)
      return GG_STORE_NOMEM;
   result = gg_store_write_file(store, path, raw, size)
      ? gg_store_commit(store, candidate) : GG_STORE_IO;
   if (result != GG_STORE_OK)
      store->io->remove(store->io_context, path);
   free(path);
   return result;
}

gg_store_result_t gg_source_store_add(gg_source_store_t *store, const char *url,
      const void *raw, size_t size, uint64_t now, char *out_id)
{
   gg_catalog_t *catalog;
   gg_store_registry_t *candidate;
   gg_source_record_t *record;
   gg_store_result_t result;
   size_t i;
   if (!store || !url || !gg_catalog_https_url_valid(url, strlen(url)))
      return GG_STORE_INVALID;
   for (i = 0; i < store->registry->count; i++)
      if (!strcmp(url, store->registry->records[i].url))
         return GG_STORE_DUPLICATE;
   if (store->registry->count == GG_CATALOG_MAX_SOURCES)
      return GG_STORE_FULL;
   if (store->registry->generation == GG_STORE_U64_MAX
         || store->registry->next_id == GG_STORE_U64_MAX)
      return GG_STORE_LIMIT;
   result = gg_store_parse_catalog(raw, size, &catalog);
   if (result != GG_STORE_OK)
      return result;
   candidate = gg_store_candidate(store);
   if (!candidate)
   {
      gg_catalog_free(catalog);
      return GG_STORE_NOMEM;
   }
   record = &candidate->records[candidate->count++];
   memset(record, 0, sizeof(*record));
   gg_store_hex(candidate->next_id++, record->local_id);
   strcpy(record->url, url);
   strcpy(record->remote_id, catalog->source.id);
   strcpy(record->name, catalog->source.name);
   record->enabled = true;
   record->revision = candidate->generation;
   record->cache_revision = record->revision;
   record->last_success = now;
   gg_catalog_free(catalog);
   result = gg_store_publish_catalog(store, candidate, candidate->count - 1, raw, size);
   if (result != GG_STORE_OK)
      free(candidate);
   else if (out_id)
      memcpy(out_id, record->local_id, sizeof(record->local_id));
   return result;
}

gg_store_result_t gg_source_store_refresh(gg_source_store_t *store,
      const char *local_id, uint64_t expected_revision,
      uint64_t expected_generation, const void *raw, size_t size, uint64_t now)
{
   const gg_source_record_t *old;
   gg_store_registry_t *candidate;
   gg_source_record_t *record;
   gg_catalog_t *catalog;
   gg_store_result_t result;
   size_t index;
   char *old_path;
   if (!store)
      return GG_STORE_INVALID;
   index = gg_store_find(store->registry, local_id);
   if (index == store->registry->count)
      return GG_STORE_NOT_FOUND;
   old = &store->registry->records[index];
   if (old->revision != expected_revision
         || store->registry->generation != expected_generation)
      return GG_STORE_STALE;
   if (!old->enabled)
      return GG_STORE_DISABLED;
   if (store->registry->generation == GG_STORE_U64_MAX)
      return GG_STORE_LIMIT;
   result = gg_store_parse_catalog(raw, size, &catalog);
   if (result != GG_STORE_OK)
      return result;
   if (strcmp(catalog->source.id, old->remote_id))
   {
      gg_catalog_free(catalog);
      return GG_STORE_IDENTITY;
   }
   candidate = gg_store_candidate(store);
   old_path = gg_store_cache_path(store, old);
   if (!candidate || !old_path)
   {
      free(candidate);
      free(old_path);
      gg_catalog_free(catalog);
      return GG_STORE_NOMEM;
   }
   record = &candidate->records[index];
   strcpy(record->name, catalog->source.name);
   record->revision = candidate->generation;
   record->cache_revision = record->revision;
   record->last_success = now;
   gg_catalog_free(catalog);
   result = gg_store_publish_catalog(store, candidate, index, raw, size);
   if (result != GG_STORE_OK)
      free(candidate);
   else
      store->io->remove(store->io_context, old_path);
   free(old_path);
   return result;
}

gg_store_result_t gg_source_store_set_enabled(gg_source_store_t *store,
      const char *local_id, bool enabled)
{
   gg_store_registry_t *candidate;
   gg_store_result_t result;
   size_t index;
   if (!store)
      return GG_STORE_INVALID;
   index = gg_store_find(store->registry, local_id);
   if (index == store->registry->count)
      return GG_STORE_NOT_FOUND;
   if (store->registry->generation == GG_STORE_U64_MAX)
      return GG_STORE_LIMIT;
   candidate = gg_store_candidate(store);
   if (!candidate)
      return GG_STORE_NOMEM;
   candidate->records[index].enabled = enabled;
   candidate->records[index].revision = candidate->generation;
   result = gg_store_commit(store, candidate);
   if (result != GG_STORE_OK)
      free(candidate);
   return result;
}

gg_store_result_t gg_source_store_remove(gg_source_store_t *store,
      const char *local_id)
{
   gg_store_registry_t *candidate;
   gg_store_result_t result;
   size_t index;
   char *path;
   if (!store)
      return GG_STORE_INVALID;
   index = gg_store_find(store->registry, local_id);
   if (index == store->registry->count)
      return GG_STORE_NOT_FOUND;
   if (store->registry->generation == GG_STORE_U64_MAX)
      return GG_STORE_LIMIT;
   candidate = gg_store_candidate(store);
   path = gg_store_cache_path(store, &store->registry->records[index]);
   if (!candidate || !path)
   {
      free(candidate);
      free(path);
      return GG_STORE_NOMEM;
   }
   candidate->count--;
   memmove(&candidate->records[index], &candidate->records[index + 1],
         (candidate->count - index) * sizeof(candidate->records[0]));
   memset(&candidate->records[candidate->count], 0, sizeof(candidate->records[0]));
   result = gg_store_commit(store, candidate);
   if (result != GG_STORE_OK)
      free(candidate);
   else
      store->io->remove(store->io_context, path);
   free(path);
   return result;
}

gg_store_result_t gg_source_store_load_catalog(gg_source_store_t *store,
      const char *local_id, gg_catalog_t **out)
{
   const gg_source_record_t *record;
   gg_store_result_t result;
   void *raw = NULL;
   size_t size = 0;
   char *path;
   if (!out)
      return GG_STORE_INVALID;
   *out = NULL;
   record = gg_source_store_get(store, local_id);
   if (!record)
      return GG_STORE_NOT_FOUND;
   path = gg_store_cache_path(store, record);
   if (!path)
      return GG_STORE_NOMEM;
   result = store->io->read(store->io_context, path, GG_CATALOG_MAX_BYTES, &raw, &size);
   free(path);
   if (result == GG_STORE_OK)
      result = gg_store_parse_catalog(raw, size, out);
   free(raw);
   if (result == GG_STORE_INVALID || result == GG_STORE_LIMIT)
      result = GG_STORE_CORRUPT;
   if (result == GG_STORE_OK && (strcmp((*out)->source.id, record->remote_id)
            || strcmp((*out)->source.name, record->name)))
   {
      gg_catalog_free(*out);
      *out = NULL;
      result = GG_STORE_CORRUPT;
   }
   return result;
}

const char *gg_source_store_error(gg_store_result_t result)
{
   switch (result)
   {
      case GG_STORE_OK:        return "Success";
      case GG_STORE_INVALID:   return "Invalid source data or argument";
      case GG_STORE_NOMEM:     return "Out of memory";
      case GG_STORE_IO:        return "Source storage I/O failed";
      case GG_STORE_CORRUPT:   return "Source storage is corrupt";
      case GG_STORE_NOT_FOUND: return "Source or cache not found";
      case GG_STORE_DUPLICATE: return "Source address already registered";
      case GG_STORE_FULL:      return "Source limit reached";
      case GG_STORE_STALE:     return "Source operation is stale";
      case GG_STORE_DISABLED:  return "Source is disabled";
      case GG_STORE_IDENTITY:  return "Source identity changed";
      case GG_STORE_LIMIT:     return "Source storage limit reached";
   }
   return "Unknown source storage error";
}
