#ifndef GAMEGATE_SOURCE_STORE_H
#define GAMEGATE_SOURCE_STORE_H

#include "catalog.h"

#define GG_SOURCE_STORE_MAX_BYTES (256UL * 1024UL)
#define GG_SOURCE_LOCAL_ID_BYTES 16

typedef enum gg_store_result
{
   GG_STORE_OK,
   GG_STORE_INVALID,
   GG_STORE_NOMEM,
   GG_STORE_IO,
   GG_STORE_CORRUPT,
   GG_STORE_NOT_FOUND,
   GG_STORE_DUPLICATE,
   GG_STORE_FULL,
   GG_STORE_STALE,
   GG_STORE_DISABLED,
   GG_STORE_IDENTITY,
   GG_STORE_LIMIT
} gg_store_result_t;

typedef struct gg_source_record
{
   uint64_t revision;
   uint64_t cache_revision;
   uint64_t last_success;
   char local_id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char url[GG_CATALOG_MAX_URL_BYTES + 1];
   char remote_id[GG_CATALOG_MAX_ID_BYTES + 1];
   char name[GG_CATALOG_MAX_NAME_BYTES + 1];
   bool enabled;
} gg_source_record_t;

typedef struct gg_source_store gg_source_store_t;
struct gg_store_io;

RETRO_BEGIN_DECLS

/* All APIs are synchronous, blocking file I/O / parsing: background worker
 * ONLY, never UI or an optionally-threaded task handler. One serial owner,
 * one live store per root, one process writer. No cross-process transactions.
 * UI must receive independent snapshots, not this mutable store or its views.
 * root is a trusted, dedicated UTF-8 local directory (no hostile symlinks).
 * A VFS must support atomic replace-on-rename; no delete-and-retry fallback.
 * Flush is checked, but power-loss durability / directory fsync is not promised.
 * NULL io selects the real filestream VFS; custom io/context outlive the store.
 * Missing registry opens an empty store; corrupt registry is never replaced.
 * URL identity is exact byte equality, with NO normalization, including query.
 * now is caller-supplied Unix seconds; zero means unspecified.
 * Registry format v1 stores all uint64 values as 16 lowercase hex digits;
 * cache names are derived from local_id/cache_revision, never read from JSON.
 * Orphan cleanup after a crash is not automatic; only published files load. */
gg_store_result_t gg_source_store_open(const char *root,
      const struct gg_store_io *io, void *io_context, gg_source_store_t **out);
void gg_source_store_close(gg_source_store_t *store);

/* Borrowed read-only array, invalidated by successful mutation or close.
 * generation is persisted and changes on every successful mutation. */
const gg_source_record_t *gg_source_store_records(const gg_source_store_t *store,
      size_t *count, uint64_t *generation);
const gg_source_record_t *gg_source_store_get(const gg_source_store_t *store,
      const char *local_id);

/* out_id is optional, written only on success (17 bytes including NUL).
 * Failed operations preserve both the published registry and in-memory state.
 * Unpublished temporary/orphan files are never read as successful data. */
gg_store_result_t gg_source_store_add(gg_source_store_t *store, const char *url,
      const void *raw, size_t size, uint64_t now, char *out_id);
/* Capture BOTH tokens when starting work; reopening preserves the checks.
 * Unrelated mutations also invalidate expected_generation (conservative).
 * Ordering/cancellation belongs to the serial owner, not filesystem checks. */
gg_store_result_t gg_source_store_refresh(gg_source_store_t *store,
      const char *local_id, uint64_t expected_revision,
      uint64_t expected_generation, const void *raw, size_t size, uint64_t now);
/* Even setting the same enabled value invalidates outstanding refreshes. */
gg_store_result_t gg_source_store_set_enabled(gg_source_store_t *store,
      const char *local_id, bool enabled);
/* Commits removal first. Cache cleanup is best effort, never rolls back it. */
gg_store_result_t gg_source_store_remove(gg_source_store_t *store,
      const char *local_id);
/* Owned result; caller frees with gg_catalog_free. Disabled caches may be
 * loaded for management. Missing/corrupt cache affects only this operation. */
gg_store_result_t gg_source_store_load_catalog(gg_source_store_t *store,
      const char *local_id, gg_catalog_t **out);
/* Static diagnostics contain no input data, URL or JSON. */
const char *gg_source_store_error(gg_store_result_t result);

RETRO_END_DECLS
#endif
