#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gamegate/source_service_internal.h"

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
   fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == GG_SERVICE_OK)

#ifdef GG_SERVICE_TEST_UNSUPPORTED
int main(void)
{
   gg_source_service_t *s = NULL;
   CHECK(gg_source_service_create("unused", &s) == GG_SERVICE_UNSUPPORTED);
   CHECK(!s);
   CHECK(gg_source_service_add(s, "unused", 0, 0) == GG_SERVICE_UNSUPPORTED);
   CHECK(gg_source_service_refresh(s, "unused", 0, 0) == GG_SERVICE_UNSUPPORTED);
   CHECK(gg_source_service_browse(s, "unused") == GG_SERVICE_UNSUPPORTED);
   CHECK(gg_source_service_remove(s, "unused") == GG_SERVICE_UNSUPPORTED);
   CHECK(gg_source_service_set_enabled(s, "unused", false) == GG_SERVICE_UNSUPPORTED);
   CHECK(gg_source_service_cancel(s) == GG_SERVICE_UNSUPPORTED);
   CHECK(!gg_source_service_record(s, "unused"));
   CHECK(!gg_source_service_game(s, "unused", "unused"));
   CHECK(!gg_source_service_view(s));
   gg_source_service_poll(s, 0);
   gg_source_service_free(s);
   printf("PASS: %u threadless assertions, linked without HTTP/store/rthreads\n", checks);
   return 0;
}
#else

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include "gamegate/source_http.h"
#ifndef GG_SERVICE_TEST_NETWORK
#include "mock_http.h"
#endif

static retro_eventcount_t tick;

static int64_t now_ms(void)
{
   return cpu_features_get_time_usec() / 1000;
}

static void step_wait(void)
{
   int key = retro_eventcount_prepare_wait(&tick);
   retro_eventcount_commit_wait_timeout(&tick, key, 1000);
}

static void complete(gg_source_service_t *s)
{
   int64_t deadline = now_ms() + 40000;
   for (;;)
   {
      const gg_source_view_t *v;
      gg_source_service_poll(s, now_ms());
      v = gg_source_service_view(s);
      if (v->state == GG_SERVICE_IDLE || v->state == GG_SERVICE_ERROR)
         return;
      CHECK(now_ms() < deadline);
      step_wait();
   }
}

static gg_source_service_t *reopen(const char *root,
      const gg_store_io_t *io, void *ctx)
{
   gg_source_service_t *s;
   gg_service_result_t result;
   int64_t deadline = now_ms() + 10000;
   do
   {
      result = gg_source_service_create_with_io(root, io, ctx, &s);
      CHECK(result == GG_SERVICE_OK || result == GG_SERVICE_BUSY);
      CHECK(now_ms() < deadline);
      if (result == GG_SERVICE_BUSY)
         step_wait();
   } while (result == GG_SERVICE_BUSY);
   complete(s);
   return s;
}

#ifndef GG_SERVICE_TEST_NETWORK
static const char catalog_a[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"homebrew\","
   "\"name\":\"Alpha\"},\"games\":[{\"id\":\"game\",\"platform\":\"nes\","
   "\"original_name\":\"Test\"}]}";
static const char catalog_b[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"homebrew\",\"name\":\"Beta\"},\"games\":[]}";
static const char changed_id[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"other\",\"name\":\"Other\"},\"games\":[]}";
static const char url_a[] = "https://example.org/a?token=not-logged";

typedef struct test_io
{
   uintptr_t owner;
   void *registry_file;
   retro_eventcount_t event;
   retro_atomic_int_t mode;
   retro_atomic_int_t reached;
   retro_atomic_int_t released;
   retro_atomic_int_t calls;
} test_io_t;

static test_io_t context;
static retro_atomic_int_t catalog_frees;

void gg_test_catalog_free_impl(gg_catalog_t *catalog);

void gg_catalog_free(gg_catalog_t *catalog)
{
   if (catalog)
   {
      if (sthread_get_current_thread_id() == context.owner)
      {
         fputs("FAIL: catalog destruction ran on owner\n", stderr);
         abort();
      }
      retro_atomic_inc_int(&catalog_frees);
   }
   gg_test_catalog_free_impl(catalog);
}

static void on_worker(test_io_t *ctx)
{
   if (sthread_get_current_thread_id() == ctx->owner)
   {
      fputs("FAIL: blocking service I/O ran on owner\n", stderr);
      abort();
   }
   retro_atomic_inc_int(&ctx->calls);
}

static void barrier(test_io_t *ctx, int mode)
{
   int64_t deadline;
   if (retro_atomic_load_acquire_int(&ctx->mode) != mode)
      return;
   deadline = now_ms() + 10000;
   retro_atomic_store_release_int(&ctx->reached, 1);
   retro_eventcount_notify(&ctx->event);
   while (!retro_atomic_load_acquire_int(&ctx->released))
   {
      int key = retro_eventcount_prepare_wait(&ctx->event);
      if (retro_atomic_load_acquire_int(&ctx->released))
         retro_eventcount_cancel_wait(&ctx->event);
      else
         retro_eventcount_commit_wait_timeout(&ctx->event, key, 100000);
      if (now_ms() >= deadline)
      {
         fputs("FAIL: worker barrier timeout\n", stderr);
         abort();
      }
   }
}

static void arm(int mode)
{
   retro_atomic_store_release_int(&context.reached, 0);
   retro_atomic_store_release_int(&context.released, 0);
   retro_atomic_store_release_int(&context.mode, mode);
}

static void reached(void)
{
   int64_t deadline = now_ms() + 10000;
   while (!retro_atomic_load_acquire_int(&context.reached))
   {
      int key = retro_eventcount_prepare_wait(&context.event);
      if (retro_atomic_load_acquire_int(&context.reached))
         retro_eventcount_cancel_wait(&context.event);
      else
         retro_eventcount_commit_wait_timeout(&context.event, key, 100000);
      CHECK(now_ms() < deadline);
   }
}

static void release_barrier(void)
{
   retro_atomic_store_release_int(&context.mode, 0);
   retro_atomic_store_release_int(&context.released, 1);
   retro_eventcount_notify(&context.event);
}

static gg_store_result_t test_read(void *data, const char *path, size_t limit,
      void **out, size_t *size)
{
   test_io_t *ctx = (test_io_t*)data;
   on_worker(ctx);
   if (strstr(path, "registry.json"))
      barrier(ctx, 1);
   else
      barrier(ctx, 4);
   return gg_store_vfs_io.read(NULL, path, limit, out, size);
}

static void *test_open(void *data, const char *path)
{
   test_io_t *ctx = (test_io_t*)data;
   void *file;
   on_worker(ctx);
   file = gg_store_vfs_io.open_write(NULL, path);
   if (strstr(path, "registry.tmp"))
      ctx->registry_file = file;
   return file;
}

static bool test_write(void *data, void *file, const void *bytes, size_t size)
{
   on_worker((test_io_t*)data);
   return gg_store_vfs_io.write(NULL, file, bytes, size);
}

static bool test_flush(void *data, void *file)
{
   on_worker((test_io_t*)data);
   return gg_store_vfs_io.flush(NULL, file);
}

static bool test_close(void *data, void *file)
{
   test_io_t *ctx = (test_io_t*)data;
   bool result;
   on_worker(ctx);
   result = gg_store_vfs_io.close(NULL, file);
   if (file == ctx->registry_file)
   {
      ctx->registry_file = NULL;
      barrier(ctx, 2);
   }
   return result;
}

static bool test_replace(void *data, const char *src, const char *dst)
{
   test_io_t *ctx = (test_io_t*)data;
   on_worker(ctx);
   if (strstr(dst, "registry.json"))
      barrier(ctx, 3);
   return gg_store_vfs_io.replace(NULL, src, dst);
}

static bool test_remove(void *data, const char *path)
{
   on_worker((test_io_t*)data);
   return gg_store_vfs_io.remove(NULL, path);
}

static bool test_mkdir(void *data, const char *path)
{
   on_worker((test_io_t*)data);
   return (gg_store_vfs_io.mkdir)(NULL, path);
}

static const gg_store_io_t test_io = {
   test_read, test_open, test_write, test_flush, test_close,
   test_replace, test_remove, test_mkdir
};

static void success(gg_source_service_t *s)
{
   complete(s);
   CHECK(gg_source_service_view(s)->state == GG_SERVICE_IDLE);
   CHECK(gg_source_service_view(s)->error_domain == GG_SERVICE_DOMAIN_NONE);
}

static void write_bad(const char *root, const char *name)
{
   char *path = (char*)malloc(strlen(root) + strlen(name) + 2);
   void *file;
   CHECK(path);
   sprintf(path, "%s/%s", root, name);
   file = gg_store_vfs_io.open_write(NULL, path);
   CHECK(file);
   CHECK(gg_store_vfs_io.write(NULL, file, "bad", 3));
   CHECK(gg_store_vfs_io.flush(NULL, file));
   CHECK(gg_store_vfs_io.close(NULL, file));
   free(path);
}

static void test_take(void)
{
   gg_source_http_t *h;
   void *body;
   const void *borrow;
   size_t size;
   test_http_reply(catalog_a, 0);
   h = gg_source_http_create(url_a, strlen(url_a));
   CHECK(h);
   CHECK(!gg_source_http_take_data(h, &size) && !size);
   CHECK(gg_source_http_start(h, now_ms()) == GG_SOURCE_HTTP_WORKING);
   CHECK(gg_source_http_poll(h, now_ms()) == GG_SOURCE_HTTP_DONE);
   borrow = gg_source_http_data(h, &size);
   body = gg_source_http_take_data(h, &size);
   CHECK(body == borrow && size == strlen(catalog_a));
   CHECK(!gg_source_http_take_data(h, &size) && !size);
   CHECK(!gg_source_http_data(h, &size) && !size);
   gg_source_http_free(h);
   CHECK(!strcmp((const char*)body, catalog_a));
   free(body);
}

static void test_service(const char *root)
{
   gg_source_service_t *s;
   gg_source_service_t *other;
   const gg_source_view_t *v;
   const gg_catalog_t *old;
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char id2[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char cache_name[96];
   uint64_t revision;
   unsigned i;
   context.owner = sthread_get_current_thread_id();
   retro_atomic_int_init(&context.mode, 0);
   retro_atomic_int_init(&context.reached, 0);
   retro_atomic_int_init(&context.released, 0);
   retro_atomic_int_init(&context.calls, 0);
   retro_atomic_int_init(&catalog_frees, 0);
   CHECK(retro_eventcount_init(&context.event));
   test_take();

   arm(1);
   OK(gg_source_service_create_with_io(root, &test_io, &context, &s));
   CHECK(gg_source_service_view(s)->state == GG_SERVICE_INITIALIZING);
   reached();
   CHECK(gg_source_service_add(s, url_a, now_ms(), 1) == GG_SERVICE_BUSY);
   CHECK(gg_source_service_create(root, &other) == GG_SERVICE_BUSY && !other);
   revision = gg_source_service_view(s)->revision;
   for (i = 0; i < 100; i++)
      gg_source_service_poll(s, now_ms());
   CHECK(gg_source_service_view(s)->revision == revision);
   release_barrier();
   success(s);
   CHECK(gg_source_service_view(s)->count == 0);

   test_http_reply(catalog_a, 0);
   OK(gg_source_service_add(s, url_a, now_ms(), 100));
   CHECK(gg_source_service_view(s)->state == GG_SERVICE_FETCHING);
   CHECK(gg_source_service_view(s)->count == 0);
   success(s);
   v = gg_source_service_view(s);
   CHECK(v->count == 1 && v->catalog && v->catalog->game_count == 1);
   CHECK(v->records[0].last_success == 100);
   strcpy(id, v->records[0].local_id);
   CHECK(!strcmp(v->operation_id, id) && !strcmp(v->catalog_id, id));
   CHECK(gg_source_service_game(s, id, "game") != NULL);
   CHECK(!gg_source_service_game(s, "missing", "game"));
   CHECK(!gg_source_service_game(s, id, "missing"));
   CHECK(gg_source_service_add(s, url_a, now_ms(), 0) == GG_SERVICE_DUPLICATE);
   CHECK(gg_source_service_add(s, "http://example.org/", now_ms(), 0) == GG_SERVICE_INVALID);
   CHECK(gg_source_service_browse(s, "missing") == GG_SERVICE_NOT_FOUND);
   revision = v->revision;
   for (i = 0; i < 100; i++)
      gg_source_service_poll(s, now_ms());
   CHECK(v->revision == revision);

   old = v->catalog;
   arm(4);
   OK(gg_source_service_browse(s, id));
   reached();
   CHECK(v->state == GG_SERVICE_LOADING && v->catalog == old);
   CHECK(!strcmp(old->games[0].id, "game"));
   release_barrier();
   success(s);
   CHECK(v->catalog != old && !strcmp(v->catalog->source.name, "Alpha"));

   test_http_reply("bad", 0);
   old = v->catalog;
   OK(gg_source_service_refresh(s, id, now_ms(), 101));
   CHECK(gg_source_service_refresh(s, id, now_ms(), 101) == GG_SERVICE_BUSY);
   CHECK(v->catalog == old);
   complete(s);
   CHECK(v->state == GG_SERVICE_ERROR && v->error_domain == GG_SERVICE_DOMAIN_STORE);
   CHECK(v->catalog == old && !strcmp(v->records[0].name, "Alpha"));
   test_http_reply(changed_id, 0);
   OK(gg_source_service_refresh(s, id, now_ms(), 102));
   complete(s);
   CHECK(v->error_code == GG_STORE_IDENTITY && v->catalog == old);
   test_http_reply(catalog_b, 2);
   OK(gg_source_service_refresh(s, id, now_ms(), 103));
   complete(s);
   CHECK(v->error_domain == GG_SERVICE_DOMAIN_NETWORK && v->catalog == old);

   test_http_reply(catalog_b, 1);
   OK(gg_source_service_refresh(s, id, now_ms(), 104));
   OK(gg_source_service_set_enabled(s, id, false));
   CHECK(test_http_active() == 0);
   test_http_late();
   success(s);
   CHECK(!gg_source_service_record(s, id)->enabled && v->catalog == old);
   CHECK(gg_source_service_refresh(s, id, now_ms(), 105) == GG_SERVICE_DISABLED);
   OK(gg_source_service_browse(s, id));
   success(s);
   OK(gg_source_service_set_enabled(s, id, true));
   success(s);

   test_http_reply(catalog_b, 0);
   old = v->catalog;
   arm(2);
   OK(gg_source_service_refresh(s, id, now_ms(), 106));
   gg_source_service_poll(s, now_ms());
   reached();
   CHECK(v->state == GG_SERVICE_APPLYING);
   CHECK(gg_source_service_remove(s, id) == GG_SERVICE_BUSY);
   CHECK(gg_source_service_set_enabled(s, id, false) == GG_SERVICE_BUSY);
   OK(gg_source_service_cancel(s));
   CHECK(gg_source_service_remove(s, id) == GG_SERVICE_BUSY);
   release_barrier();
   complete(s);
   CHECK(v->error_domain == GG_SERVICE_DOMAIN_SERVICE && v->error_code == GG_SERVICE_CANCELLED);
   CHECK(v->catalog == old && !strcmp(v->records[0].name, "Alpha"));

   arm(3);
   OK(gg_source_service_refresh(s, id, now_ms(), 107));
   gg_source_service_poll(s, now_ms());
   reached();
   CHECK(gg_source_service_cancel(s) == GG_SERVICE_TOO_LATE);
   CHECK(gg_source_service_remove(s, id) == GG_SERVICE_BUSY);
   release_barrier();
   success(s);
   CHECK(!strcmp(v->catalog->source.name, "Beta") && v->records[0].last_success == 107);
   CHECK(v->catalog != old);
   CHECK(!gg_source_service_game(s, id, "game"));

   test_http_reply(catalog_a, 1);
   OK(gg_source_service_refresh(s, id, now_ms(), 108));
   OK(gg_source_service_cancel(s));
   test_http_late();
   CHECK(v->error_code == GG_SERVICE_CANCELLED);
   CHECK(!strcmp(v->catalog->source.name, "Beta"));
   OK(gg_source_service_refresh(s, id, now_ms(), 109));
   OK(gg_source_service_remove(s, id));
   test_http_late();
   success(s);
   CHECK(!v->count && !v->catalog && !gg_source_service_record(s, id));
   CHECK(gg_source_service_refresh(s, id, now_ms(), 110) == GG_SERVICE_NOT_FOUND);

   test_http_reply(catalog_a, 0);
   arm(2);
   OK(gg_source_service_add(s, url_a, now_ms(), 200));
   gg_source_service_poll(s, now_ms());
   reached();
   OK(gg_source_service_cancel(s));
   release_barrier();
   complete(s);
   CHECK(v->count == 0);
   OK(gg_source_service_add(s, url_a, now_ms(), 201));
   success(s);
   strcpy(id, v->records[0].local_id);
   OK(gg_source_service_add(s, "https://example.org/b", now_ms(), 202));
   success(s);
   strcpy(id2, v->records[1].local_id);
   CHECK(strcmp(id, id2) && v->count == 2);
   gg_source_service_free(s);
   s = reopen(root, &test_io, &context);
   v = gg_source_service_view(s);
   CHECK(v->count == 2 && !v->catalog);
   OK(gg_source_service_browse(s, id));
   success(s);
   CHECK(!strcmp(v->catalog->source.name, "Alpha"));

   old = v->catalog;
   arm(4);
   OK(gg_source_service_browse(s, id2));
   reached();
   OK(gg_source_service_cancel(s));
   release_barrier();
   complete(s);
   CHECK(v->catalog == old && !strcmp(v->catalog_id, id));
   /* Store uses fixed-width hex revision filenames. */
   memcpy(cache_name, id2, 16);
   cache_name[16] = '-';
   revision = gg_source_service_record(s, id2)->cache_revision;
   for (i = 0; i < 16; i++)
      cache_name[32 - i] = "0123456789abcdef"[(unsigned)((revision >> (i * 4)) & 15)];
   memcpy(cache_name + 33, ".json", 6);
   write_bad(root, cache_name);
   OK(gg_source_service_browse(s, id2));
   complete(s);
   CHECK(v->state == GG_SERVICE_ERROR && v->catalog == old && v->count == 2);
   OK(gg_source_service_browse(s, id));
   success(s);

   arm(2);
   OK(gg_source_service_set_enabled(s, id, false));
   reached();
   OK(gg_source_service_cancel(s));
   release_barrier();
   complete(s);
   CHECK(gg_source_service_record(s, id)->enabled);
   arm(3);
   OK(gg_source_service_set_enabled(s, id, false));
   reached();
   CHECK(gg_source_service_cancel(s) == GG_SERVICE_TOO_LATE);
   release_barrier();
   success(s);
   CHECK(!gg_source_service_record(s, id)->enabled);
   OK(gg_source_service_set_enabled(s, id, true));
   success(s);

   arm(2);
   OK(gg_source_service_remove(s, id2));
   reached();
   OK(gg_source_service_cancel(s));
   release_barrier();
   complete(s);
   CHECK(gg_source_service_record(s, id2) != NULL);
   arm(3);
   OK(gg_source_service_remove(s, id2));
   reached();
   CHECK(gg_source_service_cancel(s) == GG_SERVICE_TOO_LATE);
   release_barrier();
   success(s);
   CHECK(!gg_source_service_record(s, id2));

   {
      char *large = (char*)malloc(512 * 1024);
      size_t used;
      int freed;
      int64_t deadline;
      CHECK(large);
      strcpy(large, "{\"schema_version\":1,\"source\":{\"id\":\"large\",\"name\":\"Large\"},\"games\":[");
      used = strlen(large);
      for (i = 0; i < GG_CATALOG_MAX_GAMES; i++)
         used += sprintf(large + used,
               "%s{\"id\":\"g%u\",\"platform\":\"nes\",\"original_name\":\"Test\"}",
               i ? "," : "", i);
      strcpy(large + used, "]}");
      test_http_reply(large, 0);
      OK(gg_source_service_add(s, "https://example.org/large", now_ms(), 203));
      free(large);
      success(s);
      CHECK(v->catalog->game_count == GG_CATALOG_MAX_GAMES);
      strcpy(id2, v->catalog_id);
      CHECK(gg_source_service_game(s, id2, "g4999") != NULL);
      freed = retro_atomic_load_acquire_int(&catalog_frees);
      OK(gg_source_service_browse(s, id));
      success(s);
      deadline = now_ms() + 10000;
      while (retro_atomic_load_acquire_int(&catalog_frees) == freed)
      {
         CHECK(now_ms() < deadline);
         step_wait();
      }
      OK(gg_source_service_remove(s, id2));
      success(s);
   }

   test_http_reply(catalog_b, 1);
   OK(gg_source_service_refresh(s, id, now_ms(), 300));
   gg_source_service_free(s);
   CHECK(test_http_active() == 0);
   test_http_late();
   s = reopen(root, &test_io, &context);
   CHECK(!strcmp(gg_source_service_record(s, id)->name, "Alpha"));

   test_http_reply(catalog_b, 0);
   arm(2);
   OK(gg_source_service_refresh(s, id, now_ms(), 301));
   gg_source_service_poll(s, now_ms());
   reached();
   gg_source_service_free(s);
   CHECK(gg_source_service_create(root, &other) == GG_SERVICE_BUSY && !other);
   release_barrier();
   s = reopen(root, &test_io, &context);
   CHECK(!strcmp(gg_source_service_record(s, id)->name, "Alpha"));

   arm(3);
   OK(gg_source_service_refresh(s, id, now_ms(), 302));
   gg_source_service_poll(s, now_ms());
   reached();
   gg_source_service_free(s);
   CHECK(gg_source_service_create(root, &other) == GG_SERVICE_BUSY && !other);
   release_barrier();
   s = reopen(root, &test_io, &context);
   CHECK(!strcmp(gg_source_service_record(s, id)->name, "Beta"));
   test_http_reply(catalog_a, 0);
   for (i = 1; i < GG_CATALOG_MAX_SOURCES; i++)
   {
      char url[96];
      sprintf(url, "https://example.org/fill-%u", i);
      OK(gg_source_service_add(s, url, now_ms(), 400));
      success(s);
   }
   CHECK(gg_source_service_view(s)->count == GG_CATALOG_MAX_SOURCES);
   CHECK(gg_source_service_add(s, "https://example.org/full", now_ms(), 401) == GG_SERVICE_FULL);
   CHECK(test_http_active() == 0);
   gg_source_service_free(s);

   arm(1);
   s = NULL;
   for (;;)
   {
      gg_service_result_t r = gg_source_service_create_with_io(root, &test_io, &context, &s);
      if (r == GG_SERVICE_OK)
         break;
      CHECK(r == GG_SERVICE_BUSY);
      step_wait();
   }
   reached();
   gg_source_service_free(s);
   CHECK(gg_source_service_create(root, &other) == GG_SERVICE_BUSY);
   release_barrier();
   s = reopen(root, &test_io, &context);
   gg_source_service_free(s);

   /* Quiescent reopen proves the old store is closed before corrupting it. */
   s = reopen(root, &test_io, &context);
   gg_source_service_free(s);
   s = reopen(root, &test_io, &context);
   write_bad(root, "registry.json");
   gg_source_service_free(s);
   s = reopen(root, &test_io, &context);
   CHECK(gg_source_service_view(s)->error_code == GG_STORE_CORRUPT);
   CHECK(gg_source_service_add(s, url_a, now_ms(), 0) == GG_SERVICE_NOT_READY);
   gg_source_service_free(s);
   s = reopen(root, NULL, NULL);
   CHECK(gg_source_service_view(s)->error_code == GG_STORE_CORRUPT);
   gg_source_service_free(s);
   CHECK(retro_atomic_load_acquire_int(&context.calls) > 50);
   CHECK(retro_atomic_load_acquire_int(&catalog_frees) > 50);
   retro_eventcount_free(&context.event);
}
#endif

int main(int argc, char **argv)
{
   CHECK(argc == 2);
   CHECK(retro_eventcount_init(&tick));
#ifdef GG_SERVICE_TEST_NETWORK
   {
      const char *url = getenv("GG_SERVICE_TEST_URL");
      gg_source_service_t *s;
      char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
      if (!url)
         puts("SKIP: real HTTPS synthetic add/store/reopen (use --network)");
      else
      {
         s = reopen(argv[1], NULL, NULL);
         OK(gg_source_service_add(s, url, now_ms(), 123));
         complete(s);
         if (gg_source_service_view(s)->state != GG_SERVICE_IDLE)
            fprintf(stderr, "HTTPS service error domain=%d code=%d\n",
                  (int)gg_source_service_view(s)->error_domain,
                  gg_source_service_view(s)->error_code);
         CHECK(gg_source_service_view(s)->state == GG_SERVICE_IDLE);
         CHECK(gg_source_service_view(s)->count == 1);
         CHECK(!strcmp(gg_source_service_view(s)->catalog->source.id, "synthetic"));
         strcpy(id, gg_source_service_view(s)->records[0].local_id);
         gg_source_service_free(s);
         s = reopen(argv[1], NULL, NULL);
         CHECK(gg_source_service_view(s)->count == 1);
         OK(gg_source_service_browse(s, id));
         complete(s);
         CHECK(gg_source_service_view(s)->catalog != NULL);
         CHECK(!strcmp(gg_source_service_view(s)->catalog->source.id, "synthetic"));
         gg_source_service_free(s);
         puts("PASS: real HTTPS synthetic add/store/reopen/browse");
      }
   }
#else
   test_service(argv[1]);
#endif
   retro_eventcount_free(&tick);
   printf("PASS: %u service assertions\n", checks);
   return 0;
}
#endif
