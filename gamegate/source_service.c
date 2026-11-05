#include <stdlib.h>
#include <string.h>

#include "source_service_internal.h"
#include "source_http.h"

#if defined(HAVE_THREADS) && HAVE_THREADS
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#endif

#if defined(HAVE_THREADS) && HAVE_THREADS && defined(RETRO_ATOMIC_LOCK_FREE)

enum gg_service_mailbox { GG_MAIL_IDLE, GG_MAIL_WORK, GG_MAIL_DONE };
enum gg_service_gate { GG_GATE_PENDING, GG_GATE_COMMITTING, GG_GATE_CANCELLED };
enum gg_service_command { GG_CMD_OPEN, GG_CMD_ADD, GG_CMD_REFRESH,
   GG_CMD_BROWSE, GG_CMD_ENABLE, GG_CMD_REMOVE };

struct gg_source_service
{
   gg_source_view_t view;
   uint64_t expected_revision;
   uint64_t expected_generation;
   uint64_t date;
   gg_source_http_t *http;
   gg_source_store_t *store;
   gg_catalog_t *result_catalog;
   gg_catalog_t *retired_catalog;
   void *body;
   size_t body_size;
   size_t result_count;
   uint64_t result_generation;
   gg_store_io_t io;
   void *io_context;
   gg_source_record_t result_records[GG_CATALOG_MAX_SOURCES];
   retro_eventcount_t wake;
   retro_atomic_int_t refs;
   retro_atomic_int_t stop;
   retro_atomic_int_t mailbox;
   retro_atomic_int_t gate;
   retro_atomic_int_t recycle;
   gg_store_result_t result;
   enum gg_service_command command;
   bool ready;
   bool result_ready;
   bool replace_catalog;
   bool enabled;
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char url[GG_CATALOG_MAX_URL_BYTES + 1];
   char root[4097];
};

static retro_atomic_int_t gg_service_live;

static void gg_service_release(gg_source_service_t *s)
{
   if (retro_atomic_fetch_sub_int(&s->refs, 1) == 1)
   {
      retro_eventcount_free(&s->wake);
      free(s);
      retro_atomic_store_release_int(&gg_service_live, 0);
   }
}

static gg_store_result_t gg_service_read(void *ctx, const char *path,
      size_t limit, void **data, size_t *size)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.read(s->io_context, path, limit, data, size);
}

static void *gg_service_open_write(void *ctx, const char *path)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.open_write(s->io_context, path);
}

static bool gg_service_write(void *ctx, void *file, const void *data, size_t size)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.write(s->io_context, file, data, size);
}

static bool gg_service_flush(void *ctx, void *file)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.flush(s->io_context, file);
}

static bool gg_service_close(void *ctx, void *file)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.close(s->io_context, file);
}

static bool gg_service_replace(void *ctx, const char *src, const char *dst)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   const char *name = strrchr(dst, '/');
   const char *backslash = strrchr(dst, '\\');
   if (backslash && (!name || backslash > name))
      name = backslash;
   name = name ? name + 1 : dst;
   if (!strcmp(name, "registry.json") &&
         !retro_atomic_cas_int(&s->gate, GG_GATE_PENDING, GG_GATE_COMMITTING))
      return false;
   return s->io.replace(s->io_context, src, dst);
}

static bool gg_service_remove_file(void *ctx, const char *path)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return s->io.remove(s->io_context, path);
}

static bool gg_service_mkdir(void *ctx, const char *path)
{
   gg_source_service_t *s = (gg_source_service_t*)ctx;
   return (s->io.mkdir)(s->io_context, path);
}

static const gg_store_io_t gg_service_io = {
   gg_service_read, gg_service_open_write, gg_service_write,
   gg_service_flush, gg_service_close, gg_service_replace,
   gg_service_remove_file, gg_service_mkdir
};

static void gg_service_execute(gg_source_service_t *s)
{
   const gg_source_record_t *records;
   gg_catalog_free(s->result_catalog);
   s->result_catalog = NULL;
   s->replace_catalog = false;
   s->result = GG_STORE_OK;
   if (retro_atomic_load_acquire_int(&s->gate) != GG_GATE_CANCELLED)
   {
      switch (s->command)
      {
         case GG_CMD_OPEN:
            s->result = gg_source_store_open(s->root, &gg_service_io, s,
                  &s->store);
            break;
         case GG_CMD_ADD:
            s->result = gg_source_store_add(s->store, s->url, s->body,
                  s->body_size, s->date, s->id);
            break;
         case GG_CMD_REFRESH:
            s->result = gg_source_store_refresh(s->store, s->id,
                  s->expected_revision, s->expected_generation,
                  s->body, s->body_size, s->date);
            break;
         case GG_CMD_ENABLE:
            s->result = gg_source_store_set_enabled(s->store, s->id, s->enabled);
            break;
         case GG_CMD_REMOVE:
            s->result = gg_source_store_remove(s->store, s->id);
            break;
         case GG_CMD_BROWSE:
            break;
      }
      if (s->result == GG_STORE_OK &&
            (s->command == GG_CMD_ADD || s->command == GG_CMD_REFRESH ||
             s->command == GG_CMD_BROWSE))
      {
         s->result = gg_source_store_load_catalog(s->store, s->id,
               &s->result_catalog);
         s->replace_catalog = s->result == GG_STORE_OK;
      }
   }
   free(s->body);
   s->body = NULL;
   s->body_size = 0;
   records = gg_source_store_records(s->store, &s->result_count,
         &s->result_generation);
   if (s->result_count)
      memcpy(s->result_records, records,
            s->result_count * sizeof(*records));
   s->result_ready = s->store != NULL;
}

static void gg_service_worker(void *data)
{
   gg_source_service_t *s = (gg_source_service_t*)data;
   for (;;)
   {
      int key;
      if (retro_atomic_load_acquire_int(&s->stop))
         break;
      if (retro_atomic_load_acquire_int(&s->mailbox) == GG_MAIL_WORK)
      {
         /* Consume retirement before publishing another result slot. */
         if (retro_atomic_load_acquire_int(&s->recycle))
         {
            gg_catalog_free(s->retired_catalog);
            s->retired_catalog = NULL;
            retro_atomic_store_release_int(&s->recycle, 0);
         }
         gg_service_execute(s);
         retro_atomic_store_release_int(&s->mailbox, GG_MAIL_DONE);
         continue;
      }
      if (retro_atomic_load_acquire_int(&s->recycle))
      {
         gg_catalog_free(s->retired_catalog);
         s->retired_catalog = NULL;
         retro_atomic_store_release_int(&s->recycle, 0);
         continue;
      }
      key = retro_eventcount_prepare_wait(&s->wake);
      if (retro_atomic_load_acquire_int(&s->stop) ||
            retro_atomic_load_acquire_int(&s->mailbox) == GG_MAIL_WORK ||
            retro_atomic_load_acquire_int(&s->recycle))
         retro_eventcount_cancel_wait(&s->wake);
      else
         retro_eventcount_commit_wait(&s->wake, key);
   }
   gg_catalog_free(s->result_catalog);
   gg_catalog_free(s->retired_catalog);
   gg_catalog_free((gg_catalog_t*)s->view.catalog);
   free(s->body);
   gg_source_store_close(s->store);
   gg_service_release(s);
}

gg_service_result_t gg_source_service_create_with_io(const char *root,
      const gg_store_io_t *io, void *context, gg_source_service_t **out)
{
   gg_source_service_t *s;
   sthread_t *thread;
   size_t length;
   if (out)
      *out = NULL;
   if (!out || !root || !(length = strlen(root)) || length > 4096)
      return GG_SERVICE_INVALID;
   if (io && (!io->read || !io->open_write || !io->write || !io->flush ||
         !io->close || !io->replace || !io->remove || !io->mkdir))
      return GG_SERVICE_INVALID;
   if (!retro_atomic_cas_int(&gg_service_live, 0, 1))
      return GG_SERVICE_BUSY;
   s = (gg_source_service_t*)calloc(1, sizeof(*s));
   if (!s)
   {
      retro_atomic_store_release_int(&gg_service_live, 0);
      return GG_SERVICE_NOMEM;
   }
   memcpy(s->root, root, length + 1);
   s->io = io ? *io : gg_store_vfs_io;
   s->io_context = context;
   s->view.state = GG_SERVICE_INITIALIZING;
   s->view.revision = 1;
   retro_atomic_int_init(&s->refs, 2);
   retro_atomic_int_init(&s->stop, 0);
   retro_atomic_int_init(&s->mailbox, GG_MAIL_WORK);
   retro_atomic_int_init(&s->gate, GG_GATE_PENDING);
   retro_atomic_int_init(&s->recycle, 0);
   if (!retro_eventcount_init(&s->wake))
   {
      retro_eventcount_free(&s->wake);
      free(s);
      retro_atomic_store_release_int(&gg_service_live, 0);
      return GG_SERVICE_NOMEM;
   }
   thread = sthread_create(gg_service_worker, s);
   if (!thread)
   {
      retro_eventcount_free(&s->wake);
      free(s);
      retro_atomic_store_release_int(&gg_service_live, 0);
      return GG_SERVICE_NOMEM;
   }
   sthread_detach(thread);
   *out = s;
   return GG_SERVICE_OK;
}

static void gg_service_state(gg_source_service_t *s, gg_service_state_t state,
      gg_service_error_domain_t domain, int code)
{
   s->view.state = state;
   s->view.error_domain = domain;
   s->view.error_code = code;
   s->view.revision++;
}

static void gg_service_send(gg_source_service_t *s, gg_service_state_t state)
{
   memcpy(s->view.operation_id, s->id, sizeof(s->id));
   gg_service_state(s, state, GG_SERVICE_DOMAIN_NONE, 0);
   retro_atomic_store_release_int(&s->gate, GG_GATE_PENDING);
   retro_atomic_store_release_int(&s->mailbox, GG_MAIL_WORK);
   retro_eventcount_notify(&s->wake);
}

static void gg_service_http_free(gg_source_service_t *s)
{
   gg_source_http_cancel(s->http);
   gg_source_http_free(s->http);
   s->http = NULL;
}

void gg_source_service_poll(gg_source_service_t *s, int64_t now_ms)
{
   if (!s)
      return;
   if (s->http)
   {
      gg_source_http_status_t status = gg_source_http_poll(s->http, now_ms);
      if (status == GG_SOURCE_HTTP_WORKING)
         return;
      if (status == GG_SOURCE_HTTP_DONE)
      {
         s->body = gg_source_http_take_data(s->http, &s->body_size);
         gg_service_http_free(s);
         gg_service_send(s, GG_SERVICE_APPLYING);
      }
      else
      {
         int code = (int)gg_source_http_error(s->http);
         gg_service_http_free(s);
         gg_service_state(s, GG_SERVICE_ERROR, GG_SERVICE_DOMAIN_NETWORK, code);
      }
      return;
   }
   if (retro_atomic_load_acquire_int(&s->mailbox) == GG_MAIL_DONE)
   {
      if (retro_atomic_load_acquire_int(&s->gate) == GG_GATE_CANCELLED)
         gg_service_state(s, GG_SERVICE_ERROR, GG_SERVICE_DOMAIN_SERVICE,
               GG_SERVICE_CANCELLED);
      else
      {
         s->ready = s->result_ready;
         s->view.count = s->result_count;
         s->view.generation = s->result_generation;
         memcpy(s->view.records, s->result_records,
               s->result_count * sizeof(s->view.records[0]));
         memcpy(s->view.operation_id, s->id, sizeof(s->id));
         if (s->replace_catalog || (s->command == GG_CMD_REMOVE &&
               s->result == GG_STORE_OK &&
               !strcmp(s->id, s->view.catalog_id)))
         {
            gg_catalog_t *old = (gg_catalog_t*)s->view.catalog;
            s->view.catalog = s->result_catalog;
            s->result_catalog = old;
            if (s->view.catalog)
               memcpy(s->view.catalog_id, s->id, sizeof(s->id));
            else
               s->view.catalog_id[0] = '\0';
         }
         gg_service_state(s, s->result == GG_STORE_OK ? GG_SERVICE_IDLE :
               GG_SERVICE_ERROR, s->result == GG_STORE_OK ?
               GG_SERVICE_DOMAIN_NONE : GG_SERVICE_DOMAIN_STORE, (int)s->result);
      }
      if (s->result_catalog)
      {
         s->retired_catalog = s->result_catalog;
         s->result_catalog = NULL;
         retro_atomic_store_release_int(&s->recycle, 1);
         retro_eventcount_notify(&s->wake);
      }
      retro_atomic_store_release_int(&s->mailbox, GG_MAIL_IDLE);
   }
}

static gg_service_result_t gg_service_available(const gg_source_service_t *s)
{
   if (!s)
      return GG_SERVICE_INVALID;
   if (s->http || retro_atomic_load_acquire_int(&s->mailbox) != GG_MAIL_IDLE)
      return GG_SERVICE_BUSY;
   return s->ready ? GG_SERVICE_OK : GG_SERVICE_NOT_READY;
}

const gg_source_view_t *gg_source_service_view(const gg_source_service_t *s)
{
   return s ? &s->view : NULL;
}

const gg_source_record_t *gg_source_service_record(const gg_source_service_t *s,
      const char *id)
{
   size_t i;
   if (s && id)
      for (i = 0; i < s->view.count; i++)
         if (!strcmp(s->view.records[i].local_id, id))
            return &s->view.records[i];
   return NULL;
}

static gg_service_result_t gg_service_fetch(gg_source_service_t *s,
      const char *url, const gg_source_record_t *record,
      int64_t now_ms, uint64_t date)
{
   gg_source_http_t *http;
   size_t length = strlen(url);
   if (now_ms < 0 || now_ms > INT64_MAX - GG_SOURCE_HTTP_DEADLINE_MS ||
         !gg_catalog_https_url_valid(url, length))
      return GG_SERVICE_INVALID;
   http = gg_source_http_create(url, length);
   if (!http)
      return GG_SERVICE_NOMEM;
   memcpy(s->url, url, length + 1);
   s->id[0] = '\0';
   if (record)
   {
      memcpy(s->id, record->local_id, sizeof(s->id));
      s->expected_revision = record->revision;
      s->expected_generation = s->view.generation;
   }
   s->command = record ? GG_CMD_REFRESH : GG_CMD_ADD;
   s->date = date;
   s->http = http;
   memcpy(s->view.operation_id, s->id, sizeof(s->id));
   gg_service_state(s, GG_SERVICE_FETCHING, GG_SERVICE_DOMAIN_NONE, 0);
   gg_source_http_start(http, now_ms);
   return GG_SERVICE_OK;
}

gg_service_result_t gg_source_service_add(gg_source_service_t *s,
      const char *url, int64_t now_ms, uint64_t date)
{
   size_t i;
   gg_service_result_t result = gg_service_available(s);
   if (result != GG_SERVICE_OK)
      return result;
   if (!url)
      return GG_SERVICE_INVALID;
   for (i = 0; i < s->view.count; i++)
      if (!strcmp(url, s->view.records[i].url))
         return GG_SERVICE_DUPLICATE;
   if (s->view.count == GG_CATALOG_MAX_SOURCES)
      return GG_SERVICE_FULL;
   return gg_service_fetch(s, url, NULL, now_ms, date);
}

gg_service_result_t gg_source_service_refresh(gg_source_service_t *s,
      const char *id, int64_t now_ms, uint64_t date)
{
   const gg_source_record_t *record;
   gg_service_result_t result = gg_service_available(s);
   if (result != GG_SERVICE_OK)
      return result;
   record = gg_source_service_record(s, id);
   if (!record)
      return GG_SERVICE_NOT_FOUND;
   if (!record->enabled)
      return GG_SERVICE_DISABLED;
   return gg_service_fetch(s, record->url, record, now_ms, date);
}

static gg_service_result_t gg_service_manage(gg_source_service_t *s,
      const char *id, enum gg_service_command command, bool enabled)
{
   const gg_source_record_t *record = gg_source_service_record(s, id);
   gg_service_result_t result;
   if (!s)
      return GG_SERVICE_INVALID;
   if (!record)
      return GG_SERVICE_NOT_FOUND;
   if (s->http && command != GG_CMD_BROWSE && !strcmp(id, s->id))
      gg_service_http_free(s);
   result = gg_service_available(s);
   if (result != GG_SERVICE_OK)
      return result;
   memcpy(s->id, record->local_id, sizeof(s->id));
   s->command = command;
   s->enabled = enabled;
   gg_service_send(s, command == GG_CMD_BROWSE ? GG_SERVICE_LOADING :
         GG_SERVICE_APPLYING);
   return GG_SERVICE_OK;
}

gg_service_result_t gg_source_service_browse(gg_source_service_t *s,
      const char *id)
{
   return gg_service_manage(s, id, GG_CMD_BROWSE, false);
}

gg_service_result_t gg_source_service_set_enabled(gg_source_service_t *s,
      const char *id, bool enabled)
{
   return gg_service_manage(s, id, GG_CMD_ENABLE, enabled);
}

gg_service_result_t gg_source_service_remove(gg_source_service_t *s,
      const char *id)
{
   return gg_service_manage(s, id, GG_CMD_REMOVE, false);
}

gg_service_result_t gg_source_service_cancel(gg_source_service_t *s)
{
   if (!s)
      return GG_SERVICE_INVALID;
   if (s->http)
   {
      gg_service_http_free(s);
      gg_service_state(s, GG_SERVICE_ERROR, GG_SERVICE_DOMAIN_SERVICE,
            GG_SERVICE_CANCELLED);
      return GG_SERVICE_OK;
   }
   if (retro_atomic_load_acquire_int(&s->mailbox) == GG_MAIL_IDLE)
      return GG_SERVICE_NOT_READY;
   if (retro_atomic_cas_int(&s->gate, GG_GATE_PENDING, GG_GATE_CANCELLED) ||
         retro_atomic_load_acquire_int(&s->gate) == GG_GATE_CANCELLED)
      return GG_SERVICE_OK;
   return GG_SERVICE_TOO_LATE;
}

void gg_source_service_free(gg_source_service_t *s)
{
   if (!s)
      return;
   gg_service_http_free(s);
   retro_atomic_cas_int(&s->gate, GG_GATE_PENDING, GG_GATE_CANCELLED);
   retro_atomic_store_release_int(&s->stop, 1);
   /* The owner's reference protects wake through the last notification. */
   retro_eventcount_notify(&s->wake);
   gg_service_release(s);
}

#else

gg_service_result_t gg_source_service_create_with_io(const char *root,
      const gg_store_io_t *io, void *context, gg_source_service_t **out)
{
   (void)root;
   (void)io;
   (void)context;
   if (out)
      *out = NULL;
   return GG_SERVICE_UNSUPPORTED;
}

void gg_source_service_poll(gg_source_service_t *s, int64_t now_ms)
{
   (void)s;
   (void)now_ms;
}

const gg_source_view_t *gg_source_service_view(const gg_source_service_t *s)
{
   (void)s;
   return NULL;
}

const gg_source_record_t *gg_source_service_record(const gg_source_service_t *s,
      const char *id)
{
   (void)s;
   (void)id;
   return NULL;
}

gg_service_result_t gg_source_service_add(gg_source_service_t *s,
      const char *url, int64_t now_ms, uint64_t date)
{
   (void)s;
   (void)url;
   (void)now_ms;
   (void)date;
   return GG_SERVICE_UNSUPPORTED;
}

gg_service_result_t gg_source_service_refresh(gg_source_service_t *s,
      const char *id, int64_t now_ms, uint64_t date)
{
   return gg_source_service_add(s, id, now_ms, date);
}

gg_service_result_t gg_source_service_browse(gg_source_service_t *s,
      const char *id)
{
   (void)s;
   (void)id;
   return GG_SERVICE_UNSUPPORTED;
}

gg_service_result_t gg_source_service_set_enabled(gg_source_service_t *s,
      const char *id, bool enabled)
{
   (void)enabled;
   return gg_source_service_browse(s, id);
}

gg_service_result_t gg_source_service_remove(gg_source_service_t *s,
      const char *id)
{
   return gg_source_service_browse(s, id);
}

gg_service_result_t gg_source_service_cancel(gg_source_service_t *s)
{
   (void)s;
   return GG_SERVICE_UNSUPPORTED;
}

void gg_source_service_free(gg_source_service_t *s)
{
   (void)s;
}
#endif

gg_service_result_t gg_source_service_create(const char *root,
      gg_source_service_t **out)
{
   return gg_source_service_create_with_io(root, NULL, NULL, out);
}

const gg_game_t *gg_source_service_game(const gg_source_service_t *s,
      const char *local_id, const char *game_id)
{
   const gg_source_view_t *v = gg_source_service_view(s);
   size_t i;
   if (!v || !local_id || !game_id || !v->catalog ||
         strcmp(v->catalog_id, local_id))
      return NULL;
   for (i = 0; i < v->catalog->game_count; i++)
      if (!strcmp(v->catalog->games[i].id, game_id))
         return &v->catalog->games[i];
   return NULL;
}
