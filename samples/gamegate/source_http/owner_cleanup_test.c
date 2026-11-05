#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GG_SOURCE_HTTP_INTERNAL
#include "gamegate/source_http.h"

struct gg_source_http_backend
{
   int active;
};

static gg_source_http_t *cancel_during_poll;
static int destroys;
static int cancels;
static int failures;
static int passes;

static void check(int condition, const char *name)
{
   if (condition)
      passes++;
   else
   {
      fprintf(stderr, "FAIL: %s\n", name);
      failures++;
   }
}

gg_source_http_backend_t *gg_source_http_backend_create(const char *url,
      size_t length, int64_t deadline_ms, gg_source_http_error_t *error)
{
   gg_source_http_backend_t *backend =
      (gg_source_http_backend_t *)calloc(1, sizeof(*backend));
   (void)url;
   (void)length;
   (void)deadline_ms;
   *error = backend ? GG_SOURCE_HTTP_ERROR_NONE
      : GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY;
   return backend;
}

gg_source_http_status_t gg_source_http_backend_poll(
      gg_source_http_backend_t *backend, int64_t now_ms)
{
   (void)backend;
   (void)now_ms;
   if (cancel_during_poll)
      gg_source_http_cancel(cancel_during_poll);
   return GG_SOURCE_HTTP_WORKING;
}

void gg_source_http_backend_cancel(gg_source_http_backend_t *backend)
{
   (void)backend;
   cancels++;
}

void gg_source_http_backend_destroy(gg_source_http_backend_t *backend)
{
   check(backend != NULL, "owner destroys a live backend");
   destroys++;
   free(backend);
}

gg_source_http_status_t gg_source_http_backend_status(
      const gg_source_http_backend_t *backend)
{
   (void)backend;
   return GG_SOURCE_HTTP_WORKING;
}

gg_source_http_error_t gg_source_http_backend_error(
      const gg_source_http_backend_t *backend)
{
   (void)backend;
   return GG_SOURCE_HTTP_ERROR_NONE;
}

const void *gg_source_http_backend_data(
      const gg_source_http_backend_t *backend, size_t *length)
{
   (void)backend;
   *length = 0;
   return NULL;
}

void *gg_source_http_backend_take_data(
      gg_source_http_backend_t *backend, size_t *length)
{
   (void)backend;
   *length = 0;
   return NULL;
}

static void test_terminal_cleanup(int mode)
{
   static const char url[] = "https://example.org/";
   gg_source_http_t *request = gg_source_http_create(url, sizeof(url) - 1);
   gg_source_http_status_t status;
   size_t length = 1;
   int64_t now = 1000;
   int before = destroys;
   check(request != NULL, "cleanup request created");
   if (!request)
      return;
   check(gg_source_http_start(request, now) == GG_SOURCE_HTTP_WORKING,
         "cleanup request started");
   if (mode == 0)
   {
      gg_source_http_cancel(request);
      check(destroys == before && cancels == 0,
            "public cancel only marks, no backend calls");
   }
   else if (mode == 1)
      now += GG_SOURCE_HTTP_DEADLINE_MS;
   else if (mode == 2)
      now = -1;
   else
      cancel_during_poll = request;
   status = gg_source_http_poll(request, now);
   cancel_during_poll = NULL;
   check(status == (mode == 0 || mode == 3 ? GG_SOURCE_HTTP_CANCELLED
            : GG_SOURCE_HTTP_ERROR), "wrapper terminal state");
   check(gg_source_http_error(request) == (mode == 1
            ? GG_SOURCE_HTTP_ERROR_TIMEOUT : mode == 2
            ? GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT
            : GG_SOURCE_HTTP_ERROR_CANCELLED), "wrapper terminal error");
   check(destroys == before + 1 && request->backend == NULL,
         "owner releases backend before terminal return");
   check(gg_source_http_data(request, &length) == NULL && length == 0,
         "terminal cancellation/error exposes no data");
   check(gg_source_http_poll(request, now) == status,
         "terminal state remains stable");
   gg_source_http_free(request);
   check(destroys == before + 1, "free does not destroy backend twice");
}

int main(void)
{
   int mode;
   for (mode = 0; mode < 4; mode++)
      test_terminal_cleanup(mode);
   printf("owner cleanup assertions: %d passed, %d failed\n", passes, failures);
   return failures ? 1 : 0;
}
