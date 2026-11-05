#ifndef GAMEGATE_CATALOG_H
#define GAMEGATE_CATALOG_H

#include <stddef.h>
#include <boolean.h>
#if defined(_MSC_VER) && _MSC_VER < 1600
#include <compat/msvc/stdint.h>
#else
#include <stdint.h>
#endif
#include <retro_common_api.h>

#define GG_CATALOG_SCHEMA_VERSION 1
#define GG_CATALOG_MAX_SOURCES 32
#define GG_CATALOG_MAX_BYTES (4UL * 1024UL * 1024UL)
#define GG_CATALOG_MAX_GAMES 5000
#define GG_CATALOG_MAX_DEPTH 16
#define GG_CATALOG_MAX_LANGUAGES 16
#define GG_CATALOG_MAX_ID_BYTES 128
#define GG_CATALOG_MAX_NAME_BYTES 256
#define GG_CATALOG_MAX_DESCRIPTION_BYTES 8192
#define GG_CATALOG_MAX_URL_BYTES 2048
#define GG_CATALOG_MAX_LANGUAGE_BYTES 63
/* Defensive bound for provider namespaces per game. */
#define GG_CATALOG_MAX_EXTERNAL_IDS 32

/* All model pointers own separate malloc-compatible allocations. Initialize
 * objects/array slots to zero; counts include every initialized partial slot
 * and never exceed allocated capacity. Strings are NUL-terminated UTF-8. */
typedef struct gg_localized
{
   char *language;
   char *name;
   char *description;
} gg_localized_t;

typedef struct gg_download
{
   uint64_t size_bytes;
   char *url;
   char *sha256;
   bool has_size;
} gg_download_t;

typedef struct gg_external_id
{
   char *provider;
   char *value;
} gg_external_id_t;

typedef struct gg_game
{
   char *id;
   char *platform; /* Preserve unknown platform codes. */
   char *original_language;
   char *original_name;
   char *region;
   char *version;
   char *cover_url; /* Metadata only; never fetched by the parser. */
   gg_localized_t *localized;
   size_t localized_count;
   gg_external_id_t *external_ids;
   size_t external_id_count;
   gg_download_t *download; /* NULL means no download object. */
} gg_game_t;

typedef struct gg_source
{
   char *id; /* Remote identity, not a local instance ID or path. */
   char *name;
   char *description;
   char *homepage;
} gg_source_t;

typedef struct gg_catalog
{
   gg_source_t source;
   gg_game_t *games;
   size_t game_count;
   unsigned schema_version;
} gg_catalog_t;

/* Borrowed views, valid only for the lifetime of the unchanged game. */
typedef struct gg_game_text
{
   const char *name;
   const char *name_language;
   const char *description;
   const char *description_language;
} gg_game_text_t;

typedef struct gg_catalog_parser gg_catalog_parser_t;

typedef enum gg_catalog_parse_status
{
   GG_CATALOG_PARSE_WORKING,
   GG_CATALOG_PARSE_DONE,
   GG_CATALOG_PARSE_ERROR
} gg_catalog_parse_status_t;

RETRO_BEGIN_DECLS

/* Accepts NULL or a heap catalog, including partially initialized objects. */
void gg_catalog_free(gg_catalog_t *catalog);

/* Copies length bytes (no terminator needed); caller retains input ownership.
 * Returns NULL for invalid input size/pointer or allocation failure.
 * Creation copies at most 4 MiB; no JSON is parsed until step(). */
gg_catalog_parser_t *gg_catalog_parser_create(const void *input, size_t length);

/* At most budget rjson events or ID-index merge operations; zero does no work.
 * rjson_next cannot be interrupted within a token (RJSON_LONG_TOKEN), nor
 * within whitespace: one event can scan up to the entire 4 MiB input cap.
 * ID validation is incrementally merge-sorted, not a quadratic string scan.
 * DONE requires RJSON_DONE and all validation; ERROR frees the partial model. */
gg_catalog_parse_status_t gg_catalog_parser_step(gg_catalog_parser_t *parser,
      size_t budget);

/* Transfers the complete model once, only after DONE; otherwise returns NULL.
 * Caller frees it with gg_catalog_free(). Never exposes a partial catalog. */
gg_catalog_t *gg_catalog_parser_take_result(gg_catalog_parser_t *parser);

/* Static diagnostic, without input data/URLs; valid after parser destruction. */
const char *gg_catalog_parser_error(const gg_catalog_parser_t *parser);

/* Also cancels WORKING parsers and frees every partial allocation. */
void gg_catalog_parser_destroy(gg_catalog_parser_t *parser);

/* Length excludes any terminator; no terminator is required or inspected.
 * ASCII URLs only; Unicode hosts must use punycode. No rewriting or DNS. */
bool gg_catalog_https_url_valid(const char *url, size_t length);

/* Each field: requested -> en -> original_language. Name then uses
 * original_name (language may be NULL); absent fields/languages are NULL. */
gg_game_text_t gg_game_select_text(const gg_game_t *game,
      const char *requested_language);

RETRO_END_DECLS
#endif
