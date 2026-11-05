#ifndef GAMEGATE_SOURCE_SERVICE_INTERNAL_H
#define GAMEGATE_SOURCE_SERVICE_INTERNAL_H

#include "source_service.h"
#include "source_store_io.h"

RETRO_BEGIN_DECLS
/* Test/host injection only. Callbacks run exclusively on the worker; context
 * must outlive asynchronous retirement (a successful next create proves it).
 * The service copies the table and wraps its registry replace cancellation. */
gg_service_result_t gg_source_service_create_with_io(const char *root,
      const gg_store_io_t *io, void *context, gg_source_service_t **out);
RETRO_END_DECLS
#endif
