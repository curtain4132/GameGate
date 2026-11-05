#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gamegate/source_store_io.h"

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
   fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == GG_STORE_OK)

static const char catalog_a[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"homebrew\","
   "\"name\":\"Alpha\"},\"games\":[{\"id\":\"game\",\"platform\":\"nes\","
   "\"original_name\":\"Test\",\"download\":{\"url\":\"https://example.org/game\"}}]}";
static const char catalog_b[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"homebrew\","
   "\"name\":\"Beta \\u6e90 \\\"\\\\\\n#include \\\"secret\\\"\"},\"games\":[]}";
static const char changed_id[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"other\",\"name\":\"Other\"},\"games\":[]}";
static const char wrong_version[] =
   "{\"schema_version\":2,\"source\":{\"id\":\"homebrew\",\"name\":\"Other\"},\"games\":[]}";

typedef struct test_io
{
   const char *root;
   unsigned operation;
   unsigned fail_at;
   unsigned calls;
   bool fail_remove;
} test_io_t;

enum { FAIL_OPEN = 1, FAIL_WRITE, FAIL_FLUSH, FAIL_CLOSE, FAIL_RENAME };

static void check_path(test_io_t *ctx, const char *path)
{
   const char *name;
   size_t n = strlen(ctx->root);
   CHECK(!strncmp(path, ctx->root, n));
   if (!path[n])
      return;
   CHECK(path[n] == '/');
   name = path + n + 1;
   CHECK(!strchr(name, '/') && !strchr(name, '\\') && !strstr(name, ".."));
}

static bool fail(test_io_t *ctx, unsigned operation)
{
   return ctx->operation == operation && ++ctx->calls == ctx->fail_at;
}

static gg_store_result_t test_read(void *data, const char *path, size_t limit,
      void **out, size_t *size)
{
   check_path((test_io_t*)data, path);
   return gg_store_vfs_io.read(NULL, path, limit, out, size);
}

static void *test_open(void *data, const char *path)
{
   test_io_t *ctx = (test_io_t*)data;
   check_path(ctx, path);
   if (fail(ctx, FAIL_OPEN))
      return NULL;
   return gg_store_vfs_io.open_write(NULL, path);
}

static bool test_write(void *data, void *file, const void *buffer, size_t size)
{
   if (fail((test_io_t*)data, FAIL_WRITE))
   {
      CHECK(gg_store_vfs_io.write(NULL, file, buffer, size / 2));
      return false;
   }
   return gg_store_vfs_io.write(NULL, file, buffer, size);
}

static bool test_flush(void *data, void *file)
{
   if (fail((test_io_t*)data, FAIL_FLUSH))
      return false;
   return gg_store_vfs_io.flush(NULL, file);
}

static bool test_close(void *data, void *file)
{
   bool closed = gg_store_vfs_io.close(NULL, file);
   return fail((test_io_t*)data, FAIL_CLOSE) ? false : closed;
}

static bool test_replace(void *data, const char *src, const char *dst)
{
   test_io_t *ctx = (test_io_t*)data;
   check_path(ctx, src);
   check_path(ctx, dst);
   if (fail(ctx, FAIL_RENAME))
      return false;
   return gg_store_vfs_io.replace(NULL, src, dst);
}

static bool test_remove(void *data, const char *path)
{
   test_io_t *ctx = (test_io_t*)data;
   check_path(ctx, path);
   return ctx->fail_remove ? false : gg_store_vfs_io.remove(NULL, path);
}

static bool test_mkdir(void *data, const char *path)
{
   check_path((test_io_t*)data, path);
   return gg_store_vfs_io.mkdir(NULL, path);
}

static const gg_store_io_t adapter = {test_read, test_open, test_write,
   test_flush, test_close, test_replace, test_remove, test_mkdir};

static char *join(const char *root, const char *name)
{
   char *path = (char*)malloc(strlen(root) + strlen(name) + 2);
   CHECK(path != NULL);
   strcpy(path, root);
   strcat(path, "/");
   strcat(path, name);
   return path;
}

static void write_file(const char *path, const void *data, size_t size)
{
   void *file = gg_store_vfs_io.open_write(NULL, path);
   CHECK(file != NULL);
   CHECK(gg_store_vfs_io.write(NULL, file, data, size));
   CHECK(gg_store_vfs_io.flush(NULL, file));
   CHECK(gg_store_vfs_io.close(NULL, file));
}

static void *read_file(const char *path, size_t *size)
{
   void *buffer = NULL;
   OK(gg_store_vfs_io.read(NULL, path, GG_CATALOG_MAX_BYTES, &buffer, size));
   return buffer;
}

static void same_file(const char *path, const void *data, size_t size)
{
   size_t actual_size;
   void *actual = read_file(path, &actual_size);
   CHECK(actual_size == size && !memcmp(data, actual, size));
   free(actual);
}

static uint64_t generation(gg_source_store_t *store)
{
   uint64_t value;
   gg_source_store_records(store, NULL, &value);
   return value;
}

static size_t count(gg_source_store_t *store)
{
   size_t value;
   gg_source_store_records(store, &value, NULL);
   return value;
}

static gg_store_result_t refresh(gg_source_store_t *store, const char *id,
      const char *raw, uint64_t now)
{
   const gg_source_record_t *record = gg_source_store_get(store, id);
   CHECK(record != NULL);
   return gg_source_store_refresh(store, id, record->revision,
         generation(store), raw, strlen(raw), now);
}

static void load_name(gg_source_store_t *store, const char *id, const char *name)
{
   gg_catalog_t *catalog = NULL;
   OK(gg_source_store_load_catalog(store, id, &catalog));
   CHECK(!strcmp(catalog->source.name, name));
   if (catalog->game_count)
      CHECK(!catalog->games[0].download->has_size);
   gg_catalog_free(catalog);
}

static void lifecycle(const char *base)
{
   char *root = join(base, "lifecycle-\xe6\xba\x90");
   char *registry = join(root, "registry.json");
   char *cache;
   char *temp;
   char *large;
   char id[17], other[17], newer[17];
   uint64_t revision, gen, old_cache;
   gg_source_store_t *store = NULL;
   gg_catalog_t *catalog = NULL;
   test_io_t ctx;
   memset(&ctx, 0, sizeof(ctx));
   ctx.root = root;
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   CHECK(count(store) == 0 && generation(store) == 0);
   CHECK(gg_source_store_add(store, "http://example.org/", catalog_a,
            sizeof(catalog_a) - 1, 1, NULL) == GG_STORE_INVALID);
   CHECK(gg_source_store_add(store, "https://example.org/", "{", 1,
            1, NULL) == GG_STORE_INVALID);
   CHECK(count(store) == 0);
   OK(gg_source_store_add(store, "https://example.org/?a=1&b=%41", catalog_a,
            sizeof(catalog_a) - 1, 10, id));
   CHECK(!strcmp(id, "0000000000000001"));
   CHECK(gg_source_store_get(store, id)->enabled);
   CHECK(gg_source_store_add(store, "https://example.org/?a=1&b=%41", catalog_a,
            sizeof(catalog_a) - 1, 11, NULL) == GG_STORE_DUPLICATE);
   OK(gg_source_store_add(store, "https://example.org/?b=%41&a=1", catalog_a,
            sizeof(catalog_a) - 1, 11, other));
   CHECK(strcmp(id, other) && count(store) == 2);
   gg_source_store_close(store);
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   CHECK(count(store) == 2 && gg_source_store_get(store, id)->last_success == 10);
   load_name(store, id, "Alpha");
   CHECK(refresh(store, id, changed_id, 12) == GG_STORE_IDENTITY);
   CHECK(refresh(store, id, "{", 12) == GG_STORE_INVALID);
   CHECK(refresh(store, id, wrong_version, 12) == GG_STORE_INVALID);
   CHECK(gg_source_store_refresh(store, id, gg_source_store_get(store, id)->revision,
            generation(store), "x", GG_CATALOG_MAX_BYTES + 1, 12) == GG_STORE_LIMIT);
   CHECK(gg_source_store_get(store, id)->last_success == 10);
   load_name(store, id, "Alpha");
   OK(refresh(store, id, catalog_b, 20));
   CHECK(gg_source_store_get(store, id)->last_success == 20);
   gg_source_store_close(store);
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   load_name(store, id, "Beta \xe6\xba\x90 \"\\\n#include \"secret\"");
   revision = gg_source_store_get(store, id)->revision;
   gen = generation(store);
   old_cache = gg_source_store_get(store, id)->cache_revision;
   OK(gg_source_store_set_enabled(store, id, false));
   CHECK(gg_source_store_get(store, id)->cache_revision == old_cache);
   CHECK(refresh(store, id, catalog_a, 30) == GG_STORE_DISABLED);
   CHECK(gg_source_store_refresh(store, id, revision, gen, catalog_a,
            sizeof(catalog_a) - 1, 30) == GG_STORE_STALE);
   OK(gg_source_store_set_enabled(store, id, true));
   gg_source_store_close(store);
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   CHECK(gg_source_store_refresh(store, id, revision, gen, catalog_a,
            sizeof(catalog_a) - 1, 30) == GG_STORE_STALE);
   CHECK(gg_source_store_refresh(store, id, gg_source_store_get(store, id)->revision,
            generation(store) + 1, catalog_a, sizeof(catalog_a) - 1, 30) == GG_STORE_STALE);
   CHECK(gg_source_store_get(store, id)->last_success == 20);
   CHECK(gg_source_store_get(store, id)->cache_revision == old_cache);
   CHECK(gg_source_store_get(store, "../registry.json") == NULL);
   CHECK(gg_source_store_remove(store, "../registry.json") == GG_STORE_NOT_FOUND);
   CHECK(gg_source_store_load_catalog(store, "../registry.json", &catalog) == GG_STORE_NOT_FOUND);
   cache = join(root, "0000000000000002-0000000000000002.json");
   write_file(cache, "{", 1);
   CHECK(gg_source_store_load_catalog(store, other, &catalog) == GG_STORE_CORRUPT);
   CHECK(catalog == NULL && count(store) == 2);
   write_file(cache, changed_id, sizeof(changed_id) - 1);
   CHECK(gg_source_store_load_catalog(store, other, &catalog) == GG_STORE_CORRUPT);
   CHECK(catalog == NULL);
   large = (char*)malloc(GG_CATALOG_MAX_BYTES + 1);
   CHECK(large != NULL);
   memset(large, ' ', GG_CATALOG_MAX_BYTES + 1);
   write_file(cache, large, GG_CATALOG_MAX_BYTES + 1);
   free(large);
   CHECK(gg_source_store_load_catalog(store, other, &catalog) == GG_STORE_CORRUPT);
   CHECK(catalog == NULL);
   CHECK(gg_store_vfs_io.remove(NULL, cache));
   CHECK(gg_source_store_load_catalog(store, other, &catalog) == GG_STORE_NOT_FOUND);
   load_name(store, id, "Beta \xe6\xba\x90 \"\\\n#include \"secret\"");
   OK(refresh(store, other, catalog_a, 31));
   ctx.fail_remove = true;
   OK(gg_source_store_remove(store, id));
   CHECK(gg_source_store_get(store, id) == NULL);
   CHECK(gg_source_store_refresh(store, id, revision, gen, catalog_a,
            sizeof(catalog_a) - 1, 32) == GG_STORE_NOT_FOUND);
   ctx.fail_remove = false;
   temp = join(root, "registry.tmp");
   write_file(temp, "{\"fake\":true}", 13);
   gg_source_store_close(store);
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   CHECK(count(store) == 1 && gg_source_store_get(store, id) == NULL);
   load_name(store, other, "Alpha");
   OK(gg_source_store_add(store, "https://example.org/?a=1&b=%41", catalog_a,
            sizeof(catalog_a) - 1, 40, newer));
   CHECK(!strcmp(newer, "0000000000000003"));
   OK(gg_source_store_remove(store, other));
   load_name(store, newer, "Alpha");
   gg_source_store_close(store);
   free(temp);
   free(cache);
   free(registry);
   free(root);
   puts("PASS lifecycle, Unicode root, escaped metadata, stale tokens, isolated caches");
}

static void capacity(const char *base)
{
   char *root = join(base, "capacity");
   char url[100], id[17];
   unsigned i;
   gg_source_store_t *store = NULL;
   OK(gg_source_store_open(root, NULL, NULL, &store));
   for (i = 0; i < 32; i++)
   {
      sprintf(url, "https://example.org/catalog?i=%u", i);
      OK(gg_source_store_add(store, url, catalog_a, sizeof(catalog_a) - 1, i, id));
   }
   CHECK(gg_source_store_add(store, "https://example.org/33", catalog_a,
            sizeof(catalog_a) - 1, 33, NULL) == GG_STORE_FULL);
   CHECK(count(store) == 32);
   gg_source_store_close(store);
   OK(gg_source_store_open(root, NULL, NULL, &store));
   CHECK(count(store) == 32);
   gg_source_store_close(store);
   free(root);
   puts("PASS 32-source bound and reopen");
}

static void rollback(const char *base)
{
   char *root = join(base, "rollback");
   char *registry = join(root, "registry.json");
   char *cache = join(root, "0000000000000001-0000000000000001.json");
   char id[17];
   void *saved;
   size_t saved_size;
   unsigned op, nth, action;
   gg_source_store_t *store = NULL;
   gg_source_record_t *record = (gg_source_record_t*)malloc(sizeof(*record));
   test_io_t ctx;
   CHECK(record != NULL);
   memset(&ctx, 0, sizeof(ctx));
   ctx.root = root;
   OK(gg_source_store_open(root, &adapter, &ctx, &store));
   OK(gg_source_store_add(store, "https://example.org/", catalog_a,
            sizeof(catalog_a) - 1, 1, id));
   saved = read_file(registry, &saved_size);
   memcpy(record, gg_source_store_get(store, id), sizeof(*record));
   for (action = 0; action < 4; action++)
      for (op = FAIL_OPEN; op <= FAIL_RENAME; op++)
         for (nth = 1; nth <= ((action < 2 && op != FAIL_RENAME) ? 2U : 1U); nth++)
         {
            gg_store_result_t result;
            ctx.operation = op;
            ctx.fail_at = nth;
            ctx.calls = 0;
            ctx.fail_remove = true;
            if (action == 0)
               result = refresh(store, id, catalog_b, 100);
            else if (action == 1)
               result = gg_source_store_add(store, "https://example.org/new", catalog_b,
                     sizeof(catalog_b) - 1, 100, NULL);
            else if (action == 2)
               result = gg_source_store_set_enabled(store, id, false);
            else
               result = gg_source_store_remove(store, id);
            CHECK(result == GG_STORE_IO);
            CHECK(ctx.calls == nth);
            ctx.operation = 0;
            ctx.fail_remove = false;
            CHECK(count(store) == 1 && generation(store) == 1);
            CHECK(!memcmp(record, gg_source_store_get(store, id), sizeof(*record)));
            same_file(registry, saved, saved_size);
            same_file(cache, catalog_a, sizeof(catalog_a) - 1);
            load_name(store, id, "Alpha");
            gg_source_store_close(store);
            OK(gg_source_store_open(root, &adapter, &ctx, &store));
            CHECK(count(store) == 1 && generation(store) == 1);
            load_name(store, id, "Alpha");
         }
   OK(refresh(store, id, catalog_b, 200));
   CHECK(gg_source_store_get(store, id)->last_success == 200);
   gg_source_store_close(store);
   free(saved);
   free(record);
   free(cache);
   free(registry);
   free(root);
   puts("PASS write/open/flush/close/rename rollback at cache and registry stages");
}

static char *replace_text(const char *text, const char *from, const char *to)
{
   const char *at = strstr(text, from);
   size_t prefix;
   char *out;
   CHECK(at != NULL);
   prefix = (size_t)(at - text);
   out = (char*)malloc(strlen(text) + strlen(to) + 1);
   CHECK(out != NULL);
   memcpy(out, text, prefix);
   strcpy(out + prefix, to);
   strcpy(out + prefix + strlen(to), at + strlen(from));
   return out;
}

static void reject_registry(const char *root, const char *path, const void *raw, size_t size)
{
   gg_source_store_t *store = NULL;
   write_file(path, raw, size);
   CHECK(gg_source_store_open(root, NULL, NULL, &store) == GG_STORE_CORRUPT);
   CHECK(store == NULL);
   same_file(path, raw, size);
}

static void corrupt_registry(const char *base)
{
   static const char *const from[] = {
      "\"version\":1", "\"generation\":\"0000000000000001\"",
      "\"next_id\":\"0000000000000002\"", "\"next_id\":\"0000000000000002\"",
      "\"local_id\":\"0000000000000001\"", "\"local_id\":\"0000000000000001\"",
      "https://example.org/", "\"enabled\":true", "\"name\":\"Alpha\"",
      "\"revision\":\"0000000000000001\"", "\"cache_revision\":\"0000000000000001\"",
      "\"remote_id\":\"homebrew\"", "\"last_success\":\"0000000000000001\"",
      "\"local_id\":\"0000000000000001\"", "\"version\":1",
      "\"enabled\":true", "\"name\":\"Alpha\"", "\"enabled\":true,",
      "\"local_id\":\"0000000000000001\""
   };
   static const char *const to[] = {
      "\"version\":2", "\"generation\":\"0000000000000000\"",
      "\"next_id\":\"0000000000000001\"", "\"next_id\":\"0000000000000000\"",
      "\"local_id\":\"../outside-file\"", "\"local_id\":\"0000000000000000\"",
      "https://user@example.org/", "\"enabled\":1", "\"name\":\"x\\u0000y\"",
      "\"revision\":\"0000000000000002\"", "\"cache_revision\":\"0000000000000000\"",
      "\"remote_id\":\"../homebrew\"", "\"last_success\":-1",
      "\"local_id\":\"000000000000000A\"", "\"version\":1,\"version\":1",
      "\"enabled\":true,\"enabled\":false", "\"name\":\"\xc0\xaf\"", "",
      "\"local_id\":\"0000000000000001\",\"filename\":\"../outside-file\""
   };
   char *root = join(base, "corrupt");
   char *path = join(root, "registry.json");
   char *outside = join(base, "outside-file");
   char *saved, *bad, *large;
   size_t size, i;
   gg_source_store_t *store = NULL;
   write_file(outside, "untouched", 9);
   OK(gg_source_store_open(root, NULL, NULL, &store));
   OK(gg_source_store_add(store, "https://example.org/", catalog_a,
            sizeof(catalog_a) - 1, 1, NULL));
   gg_source_store_close(store);
   saved = (char*)read_file(path, &size);
   reject_registry(root, path, "{", 1);
   reject_registry(root, path, "", 0);
   reject_registry(root, path, saved, size - 1);
   for (i = 0; i < sizeof(from) / sizeof(from[0]); i++)
   {
      bad = replace_text(saved, from[i], to[i]);
      reject_registry(root, path, bad, strlen(bad));
      free(bad);
   }
   large = (char*)malloc(GG_SOURCE_STORE_MAX_BYTES + 1);
   CHECK(large != NULL);
   memset(large, ' ', GG_SOURCE_STORE_MAX_BYTES + 1);
   reject_registry(root, path, large, GG_SOURCE_STORE_MAX_BYTES + 1);
   free(large);
   write_file(path, saved, size);
   OK(gg_source_store_open(root, NULL, NULL, &store));
   OK(gg_source_store_add(store, "https://example.org/second", catalog_a,
            sizeof(catalog_a) - 1, 2, NULL));
   gg_source_store_close(store);
   free(saved);
   saved = (char*)read_file(path, &size);
   bad = replace_text(saved, "\"local_id\":\"0000000000000002\"",
         "\"local_id\":\"0000000000000001\"");
   reject_registry(root, path, bad, strlen(bad));
   free(bad);
   bad = replace_text(saved, "https://example.org/second", "https://example.org/");
   reject_registry(root, path, bad, strlen(bad));
   free(bad);
   same_file(outside, "untouched", 9);
   free(saved);
   free(outside);
   free(path);
   free(root);
   puts("PASS corrupt/oversize/duplicate/unsafe registry rejection, original bytes preserved");
}

static void first_add_failure(const char *base)
{
   unsigned op, nth;
   for (op = FAIL_OPEN; op <= FAIL_RENAME; op++)
      for (nth = 1; nth <= (op == FAIL_RENAME ? 1U : 2U); nth++)
      {
         char name[64], id[17];
         char *root;
         gg_source_store_t *store = NULL;
         test_io_t ctx;
         sprintf(name, "first-add-%u-%u", op, nth);
         root = join(base, name);
         memset(&ctx, 0, sizeof(ctx));
         ctx.root = root;
         OK(gg_source_store_open(root, &adapter, &ctx, &store));
         ctx.operation = op;
         ctx.fail_at = nth;
         ctx.fail_remove = true;
         CHECK(gg_source_store_add(store, "https://example.org/", catalog_a,
                  sizeof(catalog_a) - 1, 1, NULL) == GG_STORE_IO);
         CHECK(ctx.calls == nth);
         CHECK(count(store) == 0 && generation(store) == 0);
         gg_source_store_close(store);
         ctx.operation = 0;
         ctx.fail_remove = false;
         OK(gg_source_store_open(root, &adapter, &ctx, &store));
         CHECK(count(store) == 0 && generation(store) == 0);
         OK(gg_source_store_add(store, "https://example.org/", catalog_a,
                  sizeof(catalog_a) - 1, 1, id));
         load_name(store, id, "Alpha");
         gg_source_store_close(store);
         free(root);
      }
   puts("PASS failed first add, unpublished temp/orphan files ignored on reopen");
}

static void counters_and_urls(const char *base)
{
   char *root = join(base, "counters");
   char *path = join(root, "registry.json");
   char *saved, *bad;
   char id[17], second[17];
   size_t size;
   gg_source_store_t *store = NULL;
   static const char exhausted_ids[] =
      "{\"version\":1,\"generation\":\"fffffffffffffffe\","
      "\"next_id\":\"ffffffffffffffff\",\"sources\":[]}";
   OK(gg_source_store_open(root, NULL, NULL, &store));
   OK(gg_source_store_add(store, "https://example.org/?x=%41", catalog_a,
            sizeof(catalog_a) - 1, ~(uint64_t)0, id));
   OK(gg_source_store_add(store, "https://example.org/?x=A", catalog_a,
            sizeof(catalog_a) - 1, 0, second));
   CHECK(strcmp(id, second) && count(store) == 2);
   gg_source_store_close(store);
   OK(gg_source_store_open(root, NULL, NULL, &store));
   CHECK(gg_source_store_get(store, id)->last_success == ~(uint64_t)0);
   OK(gg_source_store_remove(store, second));
   gg_source_store_close(store);
   saved = (char*)read_file(path, &size);
   bad = replace_text(saved, "\"generation\":\"0000000000000003\"",
         "\"generation\":\"ffffffffffffffff\"");
   write_file(path, bad, strlen(bad));
   OK(gg_source_store_open(root, NULL, NULL, &store));
   CHECK(refresh(store, id, catalog_b, 10) == GG_STORE_LIMIT);
   CHECK(gg_source_store_set_enabled(store, id, false) == GG_STORE_LIMIT);
   CHECK(gg_source_store_remove(store, id) == GG_STORE_LIMIT);
   CHECK(gg_source_store_add(store, "https://example.org/new", catalog_a,
            sizeof(catalog_a) - 1, 10, NULL) == GG_STORE_LIMIT);
   same_file(path, bad, strlen(bad));
   load_name(store, id, "Alpha");
   gg_source_store_close(store);
   free(bad);
   free(saved);
   write_file(path, exhausted_ids, sizeof(exhausted_ids) - 1);
   OK(gg_source_store_open(root, NULL, NULL, &store));
   CHECK(gg_source_store_add(store, "https://example.org/new", catalog_a,
            sizeof(catalog_a) - 1, 10, NULL) == GG_STORE_LIMIT);
   same_file(path, exhausted_ids, sizeof(exhausted_ids) - 1);
   gg_source_store_close(store);
   free(path);
   free(root);
   puts("PASS uint64 timestamps/counter exhaustion, exact query percent identity");
}

static void oversized_registry_entries(const char *base)
{
   char *root = join(base, "entries33");
   char *path = join(root, "registry.json");
   char *raw = (char*)malloc(20000);
   char *cursor;
   unsigned i;
   CHECK(raw != NULL);
   CHECK(gg_store_vfs_io.mkdir(NULL, root));
   strcpy(raw, "{\"version\":1,\"generation\":\"0000000000000021\","
         "\"next_id\":\"0000000000000022\",\"sources\":[");
   cursor = raw + strlen(raw);
   for (i = 1; i <= 33; i++)
   {
      if (i > 1)
         *cursor++ = ',';
      sprintf(cursor, "{\"local_id\":\"%016x\",\"url\":\"https://example.org/%u\","
            "\"remote_id\":\"homebrew\",\"name\":\"Test\",\"enabled\":true,"
            "\"last_success\":\"0000000000000000\",\"revision\":\"%016x\","
            "\"cache_revision\":\"%016x\"}", i, i, i, i);
      cursor += strlen(cursor);
   }
   strcpy(cursor, "]}");
   reject_registry(root, path, raw, strlen(raw));
   free(raw);
   free(path);
   free(root);
   puts("PASS otherwise-valid 33-entry registry rejected without rewriting");
}

int main(int argc, char **argv)
{
   CHECK(argc == 2);
   lifecycle(argv[1]);
   capacity(argv[1]);
   rollback(argv[1]);
   corrupt_registry(argv[1]);
   first_add_failure(argv[1]);
   counters_and_urls(argv[1]);
   oversized_registry_entries(argv[1]);
   printf("PASS %u checks\n", checks);
   return 0;
}
