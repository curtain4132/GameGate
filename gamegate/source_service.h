#ifndef GAMEGATE_SOURCE_SERVICE_H
#define GAMEGATE_SOURCE_SERVICE_H

#include "source_store.h"

typedef struct gg_source_service gg_source_service_t;

typedef enum gg_service_result
{
   GG_SERVICE_OK,
   GG_SERVICE_BUSY,
   GG_SERVICE_INVALID,
   GG_SERVICE_UNSUPPORTED,
   GG_SERVICE_NOMEM,
   GG_SERVICE_NOT_READY,
   GG_SERVICE_NOT_FOUND,
   GG_SERVICE_DISABLED,
   GG_SERVICE_DUPLICATE,
   GG_SERVICE_FULL,
   GG_SERVICE_TOO_LATE,
   GG_SERVICE_CANCELLED
} gg_service_result_t;

typedef enum gg_service_state
{
   GG_SERVICE_INITIALIZING,
   GG_SERVICE_IDLE,
   GG_SERVICE_FETCHING,
   GG_SERVICE_APPLYING,
   GG_SERVICE_LOADING,
   GG_SERVICE_ERROR
} gg_service_state_t;

typedef enum gg_service_error_domain
{
   GG_SERVICE_DOMAIN_NONE,
   GG_SERVICE_DOMAIN_SERVICE,
   GG_SERVICE_DOMAIN_NETWORK,
   GG_SERVICE_DOMAIN_STORE
} gg_service_error_domain_t;

typedef struct gg_source_view
{
   uint64_t revision;
   uint64_t generation;
   const gg_catalog_t *catalog;
   size_t count;
   gg_source_record_t records[GG_CATALOG_MAX_SOURCES];
   gg_service_state_t state;
   gg_service_error_domain_t error_domain;
   int error_code;
   char catalog_id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char operation_id[GG_SOURCE_LOCAL_ID_BYTES + 1];
} gg_source_view_t;

RETRO_BEGIN_DECLS

/* One owner thread for ALL calls. No task-runner dependency. root is a trusted
 * local directory, 1..4096 bytes. Only one live service (including retirement)
 * per process; create returns BUSY until the previous worker has closed.
 * OK starts asynchronous initialization, not successful registry loading.
 * Threadless builds return UNSUPPORTED without opening files. */
gg_service_result_t gg_source_service_create(const char *root,
      gg_source_service_t **out);
void gg_source_service_poll(gg_source_service_t *service, int64_t now_ms);

/* All returned data is borrowed and read-only, valid until the next poll or
 * free. Copy display text immediately; retain local_id/game id, not pointers.
 * This is an independent snapshot, never a view into the worker's store.
 * revision changes only for visible transitions/data, not idle polls.
 * Error codes belong to their domain (HTTP/store/service enums), no URL text. */
const gg_source_view_t *gg_source_service_view(const gg_source_service_t *service);
const gg_source_record_t *gg_source_service_record(
      const gg_source_service_t *service, const char *local_id);
const gg_game_t *gg_source_service_game(const gg_source_service_t *service,
      const char *local_id, const char *game_id);

/* Inputs are copied. now_ms is monotonic milliseconds, date is Unix seconds.
 * OK means accepted, inspect view after poll for completion. Rejections leave
 * the current operation/view intact. Only one network/IO operation at a time.
 * Add/refresh load the resulting catalog; old catalog survives failures.
 * Browse permits disabled caches. A loaded catalog remains readable in FETCH.
 * Management of the FETCH source cancels that transfer first; APPLY returns
 * BUSY (retry after completion), never silently queues or drops an action. */
gg_service_result_t gg_source_service_add(gg_source_service_t *service,
      const char *url, int64_t now_ms, uint64_t date);
gg_service_result_t gg_source_service_refresh(gg_source_service_t *service,
      const char *local_id, int64_t now_ms, uint64_t date);
gg_service_result_t gg_source_service_browse(gg_source_service_t *service,
      const char *local_id);
gg_service_result_t gg_source_service_set_enabled(gg_source_service_t *service,
      const char *local_id, bool enabled);
gg_service_result_t gg_source_service_remove(gg_source_service_t *service,
      const char *local_id);

/* FETCH cancels immediately. IO cancellation is acknowledged by a later poll;
 * the registry replace CAS is the boundary: TOO_LATE means commit has begun.
 * No new command until the cancelled worker result has been drained. */
gg_service_result_t gg_source_service_cancel(gg_source_service_t *service);

/* Consumes the handle, cancels/releases HTTP on owner, never joins/waits.
 * Worker frees catalogs and store. An already committing write may finish;
 * nothing is published to the dead owner. NULL is safe. */
void gg_source_service_free(gg_source_service_t *service);

RETRO_END_DECLS
#endif
