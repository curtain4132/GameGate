#ifndef GAMEGATE_SOURCE_STORE_IO_H
#define GAMEGATE_SOURCE_STORE_IO_H

#include "source_store.h"

/* Narrow synchronous adapter. read allocates a malloc-compatible buffer,
 * bounds allocation/read by limit, and distinguishes absence from I/O error.
 * replace must atomically replace dst, leaving dst untouched on failure.
 * close always releases the handle, including when returning false.
 * Only paths below the trusted caller root are passed to these callbacks. */
typedef struct gg_store_io
{
   gg_store_result_t (*read)(void *ctx, const char *path, size_t limit,
         void **data, size_t *size);
   void *(*open_write)(void *ctx, const char *path);
   bool (*write)(void *ctx, void *file, const void *data, size_t size);
   bool (*flush)(void *ctx, void *file);
   bool (*close)(void *ctx, void *file);
   bool (*replace)(void *ctx, const char *src, const char *dst);
   bool (*remove)(void *ctx, const char *path);
   bool (*mkdir)(void *ctx, const char *path);
} gg_store_io_t;

RETRO_BEGIN_DECLS
extern const gg_store_io_t gg_store_vfs_io;
RETRO_END_DECLS
#endif
