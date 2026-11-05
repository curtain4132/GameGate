#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define GG_SOURCE_HTTP_INTERNAL
#include "gamegate/source_http.h"
#include "mock_http.h"

struct gg_source_http_backend
{
   char *body;
   size_t size;
   int mode;
   gg_source_http_status_t state;
};

static const char *reply;
static int reply_mode;
static gg_source_http_backend_t *late;
static unsigned active;

void test_http_reply(const char *body, int mode)
{
   reply = body;
   reply_mode = mode;
}

void test_http_late(void)
{
   if (late)
   {
      late->state = GG_SOURCE_HTTP_DONE;
      free(late->body);
      free(late);
      late = NULL;
   }
}

unsigned test_http_active(void)
{
   return active;
}

gg_source_http_backend_t *gg_source_http_backend_create(const char *url,
      size_t length, int64_t deadline, gg_source_http_error_t *error)
{
   gg_source_http_backend_t *b = (gg_source_http_backend_t*)calloc(1, sizeof(*b));
   (void)url;
   (void)length;
   (void)deadline;
   assert(b);
   b->size = strlen(reply);
   b->body = (char*)malloc(b->size + 1);
   assert(b->body);
   memcpy(b->body, reply, b->size + 1);
   b->mode = reply_mode;
   b->state = GG_SOURCE_HTTP_WORKING;
   active++;
   *error = GG_SOURCE_HTTP_ERROR_NONE;
   return b;
}

gg_source_http_status_t gg_source_http_backend_poll(gg_source_http_backend_t *b,
      int64_t now)
{
   (void)now;
   if (!b->mode)
      b->state = GG_SOURCE_HTTP_DONE;
   else if (b->mode == 2)
      b->state = GG_SOURCE_HTTP_ERROR;
   return b->state;
}

void gg_source_http_backend_cancel(gg_source_http_backend_t *b)
{
   b->state = GG_SOURCE_HTTP_CANCELLED;
}

void gg_source_http_backend_destroy(gg_source_http_backend_t *b)
{
   assert(active);
   active--;
   if (b->mode == 1)
   {
      assert(!late);
      late = b;
   }
   else
   {
      free(b->body);
      free(b);
   }
}

gg_source_http_status_t gg_source_http_backend_status(const gg_source_http_backend_t *b)
{
   return b->state;
}

gg_source_http_error_t gg_source_http_backend_error(const gg_source_http_backend_t *b)
{
   return b->state == GG_SOURCE_HTTP_ERROR ? GG_SOURCE_HTTP_ERROR_NETWORK :
      GG_SOURCE_HTTP_ERROR_NONE;
}

const void *gg_source_http_backend_data(const gg_source_http_backend_t *b, size_t *size)
{
   *size = b->size;
   return b->body;
}

void *gg_source_http_backend_take_data(gg_source_http_backend_t *b, size_t *size)
{
   void *body = b->body;
   *size = b->size;
   b->body = NULL;
   b->size = 0;
   return body;
}
