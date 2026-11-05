#ifndef GAMEGATE_SOURCE_HTTP_H
#define GAMEGATE_SOURCE_HTTP_H

#include <stddef.h>
#if defined(_MSC_VER) && _MSC_VER < 1600
#include <compat/msvc/stdint.h>
#else
#include <stdint.h>
#endif
#include <boolean.h>
#include "catalog.h"

#define GG_SOURCE_HTTP_DEADLINE_MS 30000
#define GG_SOURCE_HTTP_MAX_REDIRECTS 5
#define GG_SOURCE_HTTP_MAX_BYTES GG_CATALOG_MAX_BYTES
#define GG_SOURCE_HTTP_READ_BUFFER_BYTES 16384

typedef struct gg_source_http gg_source_http_t;

typedef enum gg_source_http_status
{
   GG_SOURCE_HTTP_WORKING = 0,
   GG_SOURCE_HTTP_DONE,
   GG_SOURCE_HTTP_ERROR,
   GG_SOURCE_HTTP_CANCELLED,
   GG_SOURCE_HTTP_UNSUPPORTED
} gg_source_http_status_t;

typedef enum gg_source_http_error
{
   GG_SOURCE_HTTP_ERROR_NONE = 0,
   GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT,
   GG_SOURCE_HTTP_ERROR_INVALID_URL,
   GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY,
   GG_SOURCE_HTTP_ERROR_NETWORK,
   GG_SOURCE_HTTP_ERROR_TLS,
   GG_SOURCE_HTTP_ERROR_HTTP_STATUS,
   GG_SOURCE_HTTP_ERROR_REDIRECT,
   GG_SOURCE_HTTP_ERROR_TOO_LARGE,
   GG_SOURCE_HTTP_ERROR_DECOMPRESSION,
   GG_SOURCE_HTTP_ERROR_CANCELLED,
   GG_SOURCE_HTTP_ERROR_TIMEOUT,
   GG_SOURCE_HTTP_ERROR_UNSUPPORTED,
   GG_SOURCE_HTTP_ERROR_INTERNAL
} gg_source_http_error_t;

#ifdef GG_SOURCE_HTTP_INTERNAL
#include <retro_atomic.h>

#if defined(_WIN32) && defined(HAVE_THREADS) && HAVE_THREADS && \
      defined(HAVE_NETWORKING)
#define GG_SOURCE_HTTP_WIN32 1
#elif defined(ANDROID) && defined(HAVE_THREADS) && HAVE_THREADS && \
      defined(HAVE_NETWORKING)
#define GG_SOURCE_HTTP_ANDROID 1
#endif

typedef struct gg_source_http_backend gg_source_http_backend_t;

struct gg_source_http
{
   char *url;
   size_t url_length;
   int64_t deadline_ms;
   gg_source_http_backend_t *backend;
   retro_atomic_int_t started;
   retro_atomic_int_t cancelled;
   retro_atomic_int_t state;
   retro_atomic_int_t error;
};

gg_source_http_backend_t *gg_source_http_backend_create(const char *url,
      size_t length, int64_t deadline_ms, gg_source_http_error_t *error);
gg_source_http_status_t gg_source_http_backend_poll(
      gg_source_http_backend_t *backend, int64_t now_ms);
void gg_source_http_backend_cancel(gg_source_http_backend_t *backend);
void gg_source_http_backend_destroy(gg_source_http_backend_t *backend);
gg_source_http_status_t gg_source_http_backend_status(
      const gg_source_http_backend_t *backend);
gg_source_http_error_t gg_source_http_backend_error(
      const gg_source_http_backend_t *backend);
const void *gg_source_http_backend_data(
      const gg_source_http_backend_t *backend, size_t *length);
void *gg_source_http_backend_take_data(
      gg_source_http_backend_t *backend, size_t *length);

bool gg_source_http_redirect_resolve(const char *base, size_t base_length,
      const char *location, size_t location_length, char *output,
      size_t output_size);
#endif

RETRO_BEGIN_DECLS

/* Copies and validates the URL. NULL is returned for invalid input. */
gg_source_http_t *gg_source_http_create(const char *url, size_t length);

/* Start, poll and free run serially on the same owner thread. Start may be
 * called once. now_ms is a monotonic clock; the deadline includes redirects. */
gg_source_http_status_t gg_source_http_start(gg_source_http_t *request,
      int64_t now_ms);

/* Drives a platform transfer without waiting for network I/O. Poll from one
 * owner thread; completion is not delivered from a backend callback. */
gg_source_http_status_t gg_source_http_poll(gg_source_http_t *request,
      int64_t now_ms);

gg_source_http_status_t gg_source_http_status(const gg_source_http_t *request);
gg_source_http_error_t gg_source_http_error(const gg_source_http_t *request);

/* Borrowed response bytes, valid until take_data/free; only after DONE. */
const void *gg_source_http_data(const gg_source_http_t *request,
      size_t *length);

/* Owner only, after DONE. Transfers the malloc-compatible body without a
 * copy; caller frees it. Repeated take returns NULL and length zero. */
void *gg_source_http_take_data(gg_source_http_t *request, size_t *length);

/* After start returns, may race with poll, but not free. Only marks cancellation;
 * the owner performs teardown on poll/free without waiting for network I/O. */
void gg_source_http_cancel(gg_source_http_t *request);

/* Immediately abandons the request; no other call may race with free. A
 * backend retains its own references until callbacks or worker execution can
 * safely finish. */
void gg_source_http_free(gg_source_http_t *request);

RETRO_END_DECLS
#endif
