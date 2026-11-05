#include "catalog.h"

#include <formats/rjson.h>
#include <stdlib.h>
#include <string.h>

enum gg_context
{
   GG_ROOT, GG_SOURCE, GG_GAMES, GG_GAME, GG_LOCALIZED, GG_TEXT,
   GG_DOWNLOAD, GG_EXTERNAL, GG_SKIP
};

enum gg_field
{
   GG_UNKNOWN, GG_SCHEMA, GG_SOURCE_FIELD, GG_GAMES_FIELD, GG_ID, GG_NAME,
   GG_DESCRIPTION, GG_HOMEPAGE, GG_PLATFORM, GG_ORIGINAL_LANGUAGE,
   GG_ORIGINAL_NAME, GG_REGION, GG_VERSION, GG_LOCALIZED_FIELD,
   GG_DOWNLOAD_FIELD, GG_EXTERNAL_FIELD, GG_COVER_URL, GG_URL, GG_SIZE,
   GG_SHA256, GG_DYNAMIC
};

struct gg_frame
{
   unsigned long seen;
   enum gg_context context;
   enum gg_field field;
   bool object;
   bool value_pending;
};

struct gg_catalog_parser
{
   rjson_t *json;
   void *input;
   gg_catalog_t *catalog;
   const char *error;
   const char **index;
   const char **scratch;
   size_t capacity;
   size_t indexed;
   size_t width;
   size_t base;
   size_t left;
   size_t right;
   size_t middle;
   size_t end;
   size_t output;
   unsigned depth;
   gg_catalog_parse_status_t status;
   bool started;
   bool sorting;
   struct gg_frame frames[GG_CATALOG_MAX_DEPTH];
};

static void gg_parser_release_input(gg_catalog_parser_t *p)
{
   if (p->json)
      rjson_free(p->json);
   p->json = NULL;
   free(p->input);
   p->input = NULL;
   free(p->index);
   p->index = NULL;
   free(p->scratch);
   p->scratch = NULL;
}

static bool gg_parser_fail(gg_catalog_parser_t *p, const char *error)
{
   p->error = error;
   p->status = GG_CATALOG_PARSE_ERROR;
   gg_parser_release_input(p);
   gg_catalog_free(p->catalog);
   p->catalog = NULL;
   return false;
}

static unsigned char gg_lower(unsigned char c)
{
   return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c;
}

static bool gg_alpha(unsigned char c)
{
   c = gg_lower(c);
   return c >= 'a' && c <= 'z';
}

static bool gg_digit(unsigned char c)
{
   return c >= '0' && c <= '9';
}

static bool gg_equal_language(const char *a, const char *b)
{
   while (*a && *b)
      if (gg_lower((unsigned char)*a++) != gg_lower((unsigned char)*b++))
         return false;
   return *a == *b;
}

static bool gg_identifier(const char *s, size_t n)
{
   size_t i;
   if (!n || n > GG_CATALOG_MAX_ID_BYTES)
      return false;
   for (i = 0; i < n; i++)
      if (!gg_alpha((unsigned char)s[i]) && !gg_digit((unsigned char)s[i])
            && s[i] != '.' && s[i] != '_' && s[i] != '-')
         return false;
   return true;
}

/* Syntax only: no registry lookup or language alias rewriting. */
static bool gg_language(const char *s, size_t n)
{
   static const char *grandfathered[] = {
      "en-GB-oed", "i-ami", "i-bnn", "i-default", "i-enochian", "i-hak",
      "i-klingon", "i-lux", "i-mingo", "i-navajo", "i-pwn", "i-tao",
      "i-tay", "i-tsu", "sgn-BE-FR", "sgn-BE-NL", "sgn-CH-DE"
   };
   size_t starts[32], lengths[32];
   size_t count = 0, i = 0, j, start, k;
   unsigned long extensions = 0;
   bool private_use;
   if (!n || n > GG_CATALOG_MAX_LANGUAGE_BYTES)
      return false;
   for (j = 0; j < sizeof(grandfathered) / sizeof(grandfathered[0]); j++)
      if (gg_equal_language(s, grandfathered[j]))
         return true;
   while (i < n)
   {
      start = i;
      while (i < n && s[i] != '-')
      {
         if (!gg_alpha((unsigned char)s[i]) && !gg_digit((unsigned char)s[i]))
            return false;
         i++;
      }
      if (i == start || i - start > 8 || count == 32)
         return false;
      starts[count] = start;
      lengths[count++] = i - start;
      if (i < n && ++i == n)
         return false;
   }
   private_use = lengths[0] == 1 && gg_lower((unsigned char)s[0]) == 'x';
   if (private_use)
      return count > 1;
   if (lengths[0] < 2)
      return false;
   for (j = 0; j < lengths[0]; j++)
      if (!gg_alpha((unsigned char)s[j]))
         return false;
   i = 1;
   if (lengths[0] <= 3)
      for (k = 0; k < 3 && i < count && lengths[i] == 3; k++)
      {
         for (j = 0; j < 3 && gg_alpha((unsigned char)s[starts[i] + j]); j++) {}
         if (j != 3)
            break;
         i++;
      }
   if (i < count && lengths[i] == 4)
   {
      for (j = 0; j < 4 && gg_alpha((unsigned char)s[starts[i] + j]); j++) {}
      if (j == 4)
         i++;
   }
   if (i < count && (lengths[i] == 2 || lengths[i] == 3))
   {
      for (j = 0; j < lengths[i]; j++)
         if (lengths[i] == 2 ? !gg_alpha((unsigned char)s[starts[i] + j])
               : !gg_digit((unsigned char)s[starts[i] + j]))
            break;
      if (j == lengths[i])
         i++;
   }
   while (i < count && (lengths[i] >= 5 || (lengths[i] == 4
               && gg_digit((unsigned char)s[starts[i]]))))
      i++;
   while (i < count)
   {
      unsigned char c = gg_lower((unsigned char)s[starts[i]]);
      unsigned bit;
      if (lengths[i] != 1 || ++i == count)
         return false;
      if (c == 'x')
         return true;
      if (gg_digit(c))
      {
         for (k = 1; k + 1 < i; k++)
            if (lengths[k] == 1 && s[starts[k]] == (char)c)
               return false;
      }
      else
      {
         bit = (unsigned)(c - 'a');
         if (extensions & (1UL << bit))
            return false;
         extensions |= 1UL << bit;
      }
      start = i;
      while (i < count && lengths[i] >= 2)
         i++;
      if (i == start)
         return false;
   }
   return true;
}

static bool gg_copy(gg_catalog_parser_t *p, char **target,
      const char *s, size_t n, size_t limit, bool empty_missing)
{
   if (n > limit)
      return gg_parser_fail(p, "Field exceeds byte limit");
   if (!n && empty_missing)
      return true;
   *target = (char*)malloc(n + 1);
   if (!*target)
      return gg_parser_fail(p, "Out of memory");
   memcpy(*target, s, n);
   (*target)[n] = '\0';
   return true;
}

static enum gg_field gg_field_for(enum gg_context context, const char *key)
{
   static const struct
   {
      const char *name;
      enum gg_field field;
      unsigned contexts;
   } fields[] = {
      {"schema_version", GG_SCHEMA, 1U << GG_ROOT},
      {"source", GG_SOURCE_FIELD, 1U << GG_ROOT},
      {"games", GG_GAMES_FIELD, 1U << GG_ROOT},
      {"id", GG_ID, (1U << GG_SOURCE) | (1U << GG_GAME)},
      {"name", GG_NAME, (1U << GG_SOURCE) | (1U << GG_TEXT)},
      {"description", GG_DESCRIPTION, (1U << GG_SOURCE) | (1U << GG_TEXT)},
      {"homepage", GG_HOMEPAGE, 1U << GG_SOURCE},
      {"platform", GG_PLATFORM, 1U << GG_GAME},
      {"original_language", GG_ORIGINAL_LANGUAGE, 1U << GG_GAME},
      {"original_name", GG_ORIGINAL_NAME, 1U << GG_GAME},
      {"region", GG_REGION, 1U << GG_GAME},
      {"version", GG_VERSION, 1U << GG_GAME},
      {"localized", GG_LOCALIZED_FIELD, 1U << GG_GAME},
      {"download", GG_DOWNLOAD_FIELD, 1U << GG_GAME},
      {"external_ids", GG_EXTERNAL_FIELD, 1U << GG_GAME},
      {"cover_url", GG_COVER_URL, 1U << GG_GAME},
      {"url", GG_URL, 1U << GG_DOWNLOAD},
      {"size_bytes", GG_SIZE, 1U << GG_DOWNLOAD},
      {"sha256", GG_SHA256, 1U << GG_DOWNLOAD}
   };
   size_t i;
   for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
      if ((fields[i].contexts & (1U << context))
            && strcmp(key, fields[i].name) == 0)
         return fields[i].field;
   return GG_UNKNOWN;
}

static bool gg_key(gg_catalog_parser_t *p, struct gg_frame *f,
      const char *s, size_t n)
{
   gg_game_t *game;
   size_t i;
   f->value_pending = true;
   if (f->context == GG_LOCALIZED || f->context == GG_EXTERNAL)
   {
      game = &p->catalog->games[p->catalog->game_count - 1];
      f->field = GG_DYNAMIC;
      if (f->context == GG_LOCALIZED)
      {
         gg_localized_t *entry;
         if (!gg_language(s, n) || game->localized_count == GG_CATALOG_MAX_LANGUAGES)
            return gg_parser_fail(p, "Invalid language tag or language count");
         for (i = 0; i < game->localized_count; i++)
            if (gg_equal_language(game->localized[i].language, s))
               return gg_parser_fail(p, "Duplicate language");
         entry = &game->localized[game->localized_count++];
         return gg_copy(p, &entry->language, s, n, GG_CATALOG_MAX_LANGUAGE_BYTES, false);
      }
      if (!gg_identifier(s, n) || game->external_id_count == GG_CATALOG_MAX_EXTERNAL_IDS)
         return gg_parser_fail(p, "Invalid provider or external ID count");
      for (i = 0; i < game->external_id_count; i++)
         if (strcmp(game->external_ids[i].provider, s) == 0)
            return gg_parser_fail(p, "Duplicate external ID provider");
      i = game->external_id_count++;
      return gg_copy(p, &game->external_ids[i].provider, s, n,
            GG_CATALOG_MAX_ID_BYTES, false);
   }
   f->field = gg_field_for(f->context, s);
   if (f->field != GG_UNKNOWN)
   {
      unsigned long bit = 1UL << f->field;
      if (f->seen & bit)
         return gg_parser_fail(p, "Duplicate field");
      f->seen |= bit;
   }
   return true;
}

static bool gg_push(gg_catalog_parser_t *p, enum gg_context context,
      enum rjson_type type)
{
   struct gg_frame *f;
   if (p->depth == GG_CATALOG_MAX_DEPTH)
      return gg_parser_fail(p, "Nesting limit exceeded");
   f = &p->frames[p->depth++];
   memset(f, 0, sizeof(*f));
   f->context = context;
   f->object = type == RJSON_OBJECT;
   return true;
}

static bool gg_start_game(gg_catalog_parser_t *p)
{
   gg_catalog_t *catalog = p->catalog;
   if (catalog->game_count == GG_CATALOG_MAX_GAMES)
      return gg_parser_fail(p, "Game count limit exceeded");
   if (catalog->game_count == p->capacity)
   {
      size_t capacity = p->capacity ? p->capacity * 2 : 16;
      gg_game_t *games;
      if (capacity > GG_CATALOG_MAX_GAMES)
         capacity = GG_CATALOG_MAX_GAMES;
      games = (gg_game_t*)realloc(catalog->games, capacity * sizeof(*games));
      if (!games)
         return gg_parser_fail(p, "Out of memory");
      catalog->games = games;
      p->capacity = capacity;
   }
   memset(&catalog->games[catalog->game_count++], 0, sizeof(*catalog->games));
   return gg_push(p, GG_GAME, RJSON_OBJECT);
}

static bool gg_uint64(const char *s, size_t n, uint64_t *result)
{
   uint64_t value = 0, maximum = (uint64_t)-1;
   size_t i;
   if (!n || (n > 1 && *s == '0'))
      return false;
   for (i = 0; i < n; i++)
   {
      unsigned digit;
      if (!gg_digit((unsigned char)s[i]))
         return false;
      digit = (unsigned)(s[i] - '0');
      if (value > (maximum - digit) / 10)
         return false;
      value = value * 10 + digit;
   }
   *result = value;
   return true;
}

static bool gg_value(gg_catalog_parser_t *p, struct gg_frame *f,
      enum rjson_type type, const char *s, size_t n)
{
   gg_source_t *source = &p->catalog->source;
   gg_game_t *game = p->catalog->game_count
      ? &p->catalog->games[p->catalog->game_count - 1] : NULL;
   char **target = NULL;
   size_t limit = GG_CATALOG_MAX_ID_BYTES, i;
   bool empty_missing = false;
   enum gg_context child = GG_SKIP;
   f->value_pending = false;
   if (f->context == GG_GAMES)
      return type == RJSON_OBJECT ? gg_start_game(p)
         : gg_parser_fail(p, "Game must be an object");
   if (f->field == GG_UNKNOWN || f->context == GG_SKIP)
   {
      if (type == RJSON_OBJECT || type == RJSON_ARRAY)
         return gg_push(p, GG_SKIP, type);
      return true;
   }
   switch (f->field)
   {
      case GG_SOURCE_FIELD: child = GG_SOURCE; break;
      case GG_GAMES_FIELD: child = GG_GAMES; break;
      case GG_LOCALIZED_FIELD: child = GG_LOCALIZED; break;
      case GG_DOWNLOAD_FIELD: child = GG_DOWNLOAD; break;
      case GG_EXTERNAL_FIELD: child = GG_EXTERNAL; break;
      case GG_DYNAMIC:
         if (f->context == GG_LOCALIZED)
            child = GG_TEXT;
         break;
      default: break;
   }
   if (child != GG_SKIP)
   {
      if (type != (child == GG_GAMES ? RJSON_ARRAY : RJSON_OBJECT))
         return gg_parser_fail(p, "Wrong container type");
      if (child == GG_LOCALIZED)
      {
         game->localized = (gg_localized_t*)calloc(GG_CATALOG_MAX_LANGUAGES,
               sizeof(*game->localized));
         if (!game->localized)
            return gg_parser_fail(p, "Out of memory");
      }
      else if (child == GG_EXTERNAL)
      {
         game->external_ids = (gg_external_id_t*)calloc(GG_CATALOG_MAX_EXTERNAL_IDS,
               sizeof(*game->external_ids));
         if (!game->external_ids)
            return gg_parser_fail(p, "Out of memory");
      }
      else if (child == GG_DOWNLOAD)
      {
         game->download = (gg_download_t*)calloc(1, sizeof(*game->download));
         if (!game->download)
            return gg_parser_fail(p, "Out of memory");
      }
      return gg_push(p, child, type);
   }
   if (f->field == GG_SCHEMA)
   {
      if (type != RJSON_NUMBER || n != 1 || *s != '1')
         return gg_parser_fail(p, "Unsupported schema version");
      p->catalog->schema_version = GG_CATALOG_SCHEMA_VERSION;
      return true;
   }
   if (f->field == GG_SIZE)
   {
      if (type != RJSON_NUMBER || !gg_uint64(s, n, &game->download->size_bytes))
         return gg_parser_fail(p, "Invalid unsigned size");
      game->download->has_size = true;
      return true;
   }
   if (type != RJSON_STRING)
      return gg_parser_fail(p, "Expected string");
   switch (f->field)
   {
      case GG_ID:
      case GG_PLATFORM:
         if (!gg_identifier(s, n))
            return gg_parser_fail(p, "Invalid identifier");
         target = f->field == GG_PLATFORM ? &game->platform
            : (f->context == GG_SOURCE ? &source->id : &game->id);
         break;
      case GG_NAME:
      case GG_DESCRIPTION:
         empty_missing = true;
         limit = f->field == GG_NAME ? GG_CATALOG_MAX_NAME_BYTES
            : GG_CATALOG_MAX_DESCRIPTION_BYTES;
         if (f->context == GG_SOURCE)
            target = f->field == GG_NAME ? &source->name : &source->description;
         else
         {
            gg_localized_t *text = &game->localized[game->localized_count - 1];
            target = f->field == GG_NAME ? &text->name : &text->description;
         }
         break;
      case GG_ORIGINAL_NAME:
         if (!n)
            return gg_parser_fail(p, "Empty original name");
         target = &game->original_name;
         limit = GG_CATALOG_MAX_NAME_BYTES;
         break;
      case GG_ORIGINAL_LANGUAGE:
         if (!gg_language(s, n))
            return gg_parser_fail(p, "Invalid original language");
         target = &game->original_language;
         limit = GG_CATALOG_MAX_LANGUAGE_BYTES;
         break;
      case GG_REGION: target = &game->region; break;
      case GG_VERSION: target = &game->version; break;
      case GG_HOMEPAGE:
      case GG_COVER_URL:
      case GG_URL:
         if (!gg_catalog_https_url_valid(s, n))
            return gg_parser_fail(p, "Invalid HTTPS URL");
         target = f->field == GG_HOMEPAGE ? &source->homepage
            : (f->field == GG_COVER_URL ? &game->cover_url : &game->download->url);
         limit = GG_CATALOG_MAX_URL_BYTES;
         break;
      case GG_SHA256:
         if (n != 64)
            return gg_parser_fail(p, "Invalid SHA256");
         for (i = 0; i < n; i++)
         {
            unsigned char c = gg_lower((unsigned char)s[i]);
            if (!gg_digit(c) && (c < 'a' || c > 'f'))
               return gg_parser_fail(p, "Invalid SHA256");
         }
         target = &game->download->sha256;
         limit = 64;
         break;
      case GG_DYNAMIC:
         if (!n)
            return gg_parser_fail(p, "Empty external ID");
         target = &game->external_ids[game->external_id_count - 1].value;
         limit = GG_CATALOG_MAX_NAME_BYTES;
         break;
      default:
         return gg_parser_fail(p, "Invalid field state");
   }
   return gg_copy(p, target, s, n, limit, empty_missing);
}

static bool gg_close(gg_catalog_parser_t *p, struct gg_frame *f)
{
   gg_game_t *game = p->catalog->game_count
      ? &p->catalog->games[p->catalog->game_count - 1] : NULL;
   unsigned long required = (1UL << GG_SCHEMA) | (1UL << GG_SOURCE_FIELD)
      | (1UL << GG_GAMES_FIELD);
   if ((f->context == GG_ROOT && (f->seen & required) != required)
         || (f->context == GG_SOURCE && (!p->catalog->source.id || !p->catalog->source.name))
         || (f->context == GG_GAME && (!game->id || !game->platform || !game->original_name))
         || (f->context == GG_DOWNLOAD && !game->download->url))
      return gg_parser_fail(p, "Missing required field");
   p->depth--;
   return true;
}

static bool gg_event(gg_catalog_parser_t *p, enum rjson_type type)
{
   const char *s = NULL;
   size_t n = 0;
   struct gg_frame *f;
   if (type == RJSON_ERROR)
      return gg_parser_fail(p, "Invalid JSON or encoding");
   if (type == RJSON_DONE)
   {
      size_t count = p->catalog->game_count;
      if (!p->started || p->depth)
         return gg_parser_fail(p, "Incomplete catalog");
      if (count < 2)
      {
         p->status = GG_CATALOG_PARSE_DONE;
         gg_parser_release_input(p);
         return true;
      }
      p->index = (const char**)malloc(count * sizeof(*p->index));
      p->scratch = (const char**)malloc(count * sizeof(*p->scratch));
      if (!p->index || !p->scratch)
         return gg_parser_fail(p, "Out of memory");
      p->sorting = true;
      return true;
   }
   if (!p->started)
   {
      p->started = true;
      return type == RJSON_OBJECT ? gg_push(p, GG_ROOT, type)
         : gg_parser_fail(p, "Catalog must be an object");
   }
   if (!p->depth)
      return gg_parser_fail(p, "Trailing data");
   f = &p->frames[p->depth - 1];
   if (type == RJSON_OBJECT_END || type == RJSON_ARRAY_END)
      return gg_close(p, f);
   if (type == RJSON_STRING || type == RJSON_NUMBER)
   {
      s = rjson_get_string(p->json, &n);
      if (memchr(s, '\0', n))
         return gg_parser_fail(p, "Embedded NUL");
   }
   if (f->object && !f->value_pending)
      return type == RJSON_STRING ? gg_key(p, f, s, n)
         : gg_parser_fail(p, "Expected member name");
   return gg_value(p, f, type, s, n);
}

/* One copied index entry per work unit, including initial index creation. */
static bool gg_index_step(gg_catalog_parser_t *p)
{
   size_t count = p->catalog->game_count;
   if (p->indexed < count)
   {
      p->index[p->indexed] = p->catalog->games[p->indexed].id;
      p->indexed++;
      return true;
   }
   if (!p->width)
      p->width = 1;
   if (p->output == p->end)
   {
      p->base = p->end;
      if (p->base == count)
      {
         const char **swap = p->index;
         p->index = p->scratch;
         p->scratch = swap;
         p->width *= 2;
         p->base = 0;
         if (p->width >= count)
         {
            p->status = GG_CATALOG_PARSE_DONE;
            gg_parser_release_input(p);
            return true;
         }
      }
      p->middle = p->base + p->width;
      if (p->middle > count)
         p->middle = count;
      p->end = p->middle + p->width;
      if (p->end > count)
         p->end = count;
      p->left = p->base;
      p->right = p->middle;
      p->output = p->base;
   }
   if (p->left < p->middle && p->right < p->end)
   {
      int comparison = strcmp(p->index[p->left], p->index[p->right]);
      if (!comparison)
         return gg_parser_fail(p, "Duplicate game ID");
      p->scratch[p->output++] = p->index[comparison < 0 ? p->left++ : p->right++];
   }
   else
      p->scratch[p->output++] = p->index[p->left < p->middle ? p->left++ : p->right++];
   return true;
}

gg_catalog_parser_t *gg_catalog_parser_create(const void *input, size_t length)
{
   gg_catalog_parser_t *p;
   if (!input || !length || length > GG_CATALOG_MAX_BYTES)
      return NULL;
   p = (gg_catalog_parser_t*)calloc(1, sizeof(*p));
   if (!p)
      return NULL;
   p->input = malloc(length);
   p->catalog = (gg_catalog_t*)calloc(1, sizeof(*p->catalog));
   if (!p->input || !p->catalog)
   {
      gg_catalog_parser_destroy(p);
      return NULL;
   }
   memcpy(p->input, input, length);
   p->json = rjson_open_buffer(p->input, length);
   if (!p->json)
   {
      gg_catalog_parser_destroy(p);
      return NULL;
   }
   /* rjson includes its top-level sentinel in the stack capacity. */
   rjson_set_max_depth(p->json, GG_CATALOG_MAX_DEPTH + 1);
   return p;
}

gg_catalog_parse_status_t gg_catalog_parser_step(gg_catalog_parser_t *p,
      size_t budget)
{
   if (!p)
      return GG_CATALOG_PARSE_ERROR;
   while (budget && p->status == GG_CATALOG_PARSE_WORKING)
   {
      budget--;
      if (p->sorting)
         gg_index_step(p);
      else
         gg_event(p, rjson_next(p->json));
   }
   return p->status;
}

gg_catalog_t *gg_catalog_parser_take_result(gg_catalog_parser_t *p)
{
   gg_catalog_t *catalog;
   if (!p || p->status != GG_CATALOG_PARSE_DONE)
      return NULL;
   catalog = p->catalog;
   p->catalog = NULL;
   return catalog;
}

const char *gg_catalog_parser_error(const gg_catalog_parser_t *p)
{
   return p ? (p->error ? p->error : "") : "Invalid parser";
}

void gg_catalog_parser_destroy(gg_catalog_parser_t *p)
{
   if (!p)
      return;
   gg_parser_release_input(p);
   gg_catalog_free(p->catalog);
   free(p);
}
