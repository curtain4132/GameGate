#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <compat/strl.h>
#include <file/file_path.h>
#include <features/features_cpu.h>
#include <retro_miscellaneous.h>
#include <retro_timers.h>
#include <string/stdstring.h>
#include <queues/task_queue.h>

#include "menu_gamegate_sources.h"
#include "menu_driver.h"
#include "menu_input.h"
#include "../defaults.h"
#include "../paths.h"
#include "../gamegate/source_service.h"
#include "../gamegate/source_http.h"
#include "../msg_hash_lbl_str.h"

#define GG_TEXT(id) msg_hash_to_str(MENU_ENUM_LABEL_VALUE_GG_SOURCE_##id)

enum gg_menu_command
{
   GG_MENU_NONE, GG_MENU_ADD, GG_MENU_REFRESH, GG_MENU_ENABLE,
   GG_MENU_REMOVE, GG_MENU_CANCEL, GG_MENU_BROWSE
};

typedef struct gg_menu_error
{
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   enum msg_hash_enums message;
} gg_menu_error_t;

typedef struct gg_menu_sources
{
   gg_source_service_t *service;
   uint64_t revision;
   uint64_t operation_generation;
   uint64_t browse_revision;
   enum gg_menu_command pending;
   enum gg_menu_command active;
   enum msg_hash_enums notice;
   gg_menu_error_t errors[GG_CATALOG_MAX_SOURCES];
   char root[PATH_MAX_LENGTH];
   char input[GG_CATALOG_MAX_URL_BYTES + 1];
   char operation_id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char browse_id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char return_id[MENU_GAMEGATE_SOURCE_ID_SIZE];
   bool dirty;
   bool kiosk;
   bool unavailable;
} gg_menu_sources_t;

static gg_menu_sources_t *gg_menu_sources;

bool menu_gamegate_sources_page(const char *label)
{
   return label && (!strcmp(label, MENU_ENUM_LABEL_GAMEGATE_SOURCES_STR)
         || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_DETAIL_STR)
         || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAMES_STR)
         || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR)
         || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM_STR));
}

static bool gg_menu_kiosk(void)
{
   return config_get_ptr()->bools.kiosk_mode_enable;
}

static const char *gg_menu_language(void)
{
   switch (*msg_hash_get_uint(MSG_HASH_USER_LANGUAGE))
   {
      case RETRO_LANGUAGE_CHINESE_SIMPLIFIED: return "zh-Hans";
      case RETRO_LANGUAGE_CHINESE_TRADITIONAL: return "zh-Hant";
      case RETRO_LANGUAGE_PORTUGUESE_BRAZIL: return "pt-BR";
      case RETRO_LANGUAGE_PORTUGUESE_PORTUGAL: return "pt-PT";
      case RETRO_LANGUAGE_BRITISH_ENGLISH: return "en-GB";
      case RETRO_LANGUAGE_CATALAN_VALENCIA: return "ca-ES-valencia";
      default: return get_user_language_iso639_1(true);
   }
}

static void gg_menu_local_id(char *out, const char *path)
{
   size_t n = path ? strcspn(path, "/") : 0;
   out[0] = '\0';
   if (n == GG_SOURCE_LOCAL_ID_BYTES)
   {
      memcpy(out, path, n);
      out[n] = '\0';
   }
}

static enum msg_hash_enums gg_menu_result(gg_service_result_t result)
{
   switch (result)
   {
      case GG_SERVICE_OK: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_WORKING;
      case GG_SERVICE_BUSY: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_BUSY;
      case GG_SERVICE_INVALID: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_INVALID_URL;
      case GG_SERVICE_UNSUPPORTED: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_UNSUPPORTED;
      case GG_SERVICE_NOT_READY: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_NOT_READY;
      case GG_SERVICE_NOT_FOUND: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_MISSING;
      case GG_SERVICE_DISABLED: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_DISABLED;
      case GG_SERVICE_DUPLICATE: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_DUPLICATE;
      case GG_SERVICE_FULL: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_FULL;
      case GG_SERVICE_TOO_LATE: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_TOO_LATE;
      case GG_SERVICE_CANCELLED: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_CANCELLED;
      default: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_FAILED;
   }
}

static enum msg_hash_enums gg_menu_error(const gg_source_view_t *v)
{
   if (v->error_domain == GG_SERVICE_DOMAIN_SERVICE)
      return gg_menu_result((gg_service_result_t)v->error_code);
   if (v->error_domain == GG_SERVICE_DOMAIN_NETWORK)
   {
      switch (v->error_code)
      {
         case GG_SOURCE_HTTP_ERROR_TLS: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_TLS;
         case GG_SOURCE_HTTP_ERROR_TIMEOUT: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_TIMEOUT;
         case GG_SOURCE_HTTP_ERROR_TOO_LARGE: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_LIMIT;
         case GG_SOURCE_HTTP_ERROR_UNSUPPORTED: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_UNSUPPORTED;
         default: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_NETWORK;
      }
   }
   if (v->error_domain == GG_SERVICE_DOMAIN_STORE)
   {
      switch (v->error_code)
      {
         case GG_STORE_INVALID: case GG_STORE_CORRUPT:
            return MENU_ENUM_LABEL_VALUE_GG_SOURCE_INVALID_CATALOG;
         case GG_STORE_IDENTITY: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_IDENTITY;
         case GG_STORE_LIMIT: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_LIMIT;
         default: return MENU_ENUM_LABEL_VALUE_GG_SOURCE_STORAGE;
      }
   }
   return MSG_UNKNOWN;
}

static void gg_menu_remember_error(gg_menu_sources_t *s,
      const char *id, enum msg_hash_enums message)
{
   size_t i;
   if (!*id)
      return;
   for (i = 0; i < GG_CATALOG_MAX_SOURCES; i++)
      if (!s->errors[i].id[0] || !strcmp(s->errors[i].id, id))
      {
         strlcpy(s->errors[i].id, id, sizeof(s->errors[i].id));
         s->errors[i].message = message;
         return;
      }
}

static void gg_menu_task_cleanup(retro_task_t *task)
{
   gg_menu_sources_t *s = (gg_menu_sources_t*)task->state;
   if (!s)
      return;
   if (gg_menu_sources == s)
      gg_menu_sources = NULL;
   gg_source_service_free(s->service);
   free(s);
   task->state = NULL;
}

static void gg_menu_task_handler(retro_task_t *task)
{
   gg_menu_sources_t *s = (gg_menu_sources_t*)task->state;
   const gg_source_view_t *v;
   gg_service_result_t result;
   const char *path = NULL;
   const char *label = NULL;
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   int64_t now = cpu_features_get_time_usec() / 1000;
   bool kiosk = gg_menu_kiosk();

   if (task_get_flags(task) & RETRO_TASK_FLG_CANCELLED)
   {
      /* free also covers INITIALIZING; ordinary cancel does not. */
      gg_menu_task_cleanup(task);
      task_set_flags(task, RETRO_TASK_FLG_FINISHED, true);
      return;
   }
   if (!s->service)
   {
      if (s->unavailable)
         return;
      result = gg_source_service_create(s->root, &s->service);
      if (result != GG_SERVICE_OK)
      {
         s->dirty = s->notice != gg_menu_result(result);
         s->notice = gg_menu_result(result);
         if (result != GG_SERVICE_BUSY)
            s->unavailable = true;
         return;
      }
      s->dirty = true;
      return;
   }
   gg_source_service_poll(s->service, now);
   v = gg_source_service_view(s->service);
   if (s->revision != v->revision)
   {
      s->revision = v->revision;
      s->dirty = true;
      if (s->active != GG_MENU_NONE
            && (v->state == GG_SERVICE_IDLE || v->state == GG_SERVICE_ERROR))
      {
         s->notice = gg_menu_error(v);
         if (s->notice != MSG_UNKNOWN
               && (s->active == GG_MENU_ADD || s->active == GG_MENU_REFRESH)
               && v->generation != s->operation_generation)
            s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_SAVED_LOAD_FAILED;
         if (s->notice == MSG_UNKNOWN)
            s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_COMPLETE;
         if (s->active == GG_MENU_ADD)
            strlcpy(s->operation_id, v->operation_id, sizeof(s->operation_id));
         gg_menu_remember_error(s, s->operation_id,
               gg_menu_error(v) == MSG_UNKNOWN ? MSG_UNKNOWN : s->notice);
         s->active = GG_MENU_NONE;
      }
   }
   if (s->kiosk != kiosk)
   {
      s->kiosk = kiosk;
      s->dirty = true;
   }
   if (s->pending != GG_MENU_NONE)
   {
      enum gg_menu_command command = s->pending;
      const gg_source_record_t *record;
      uint64_t generation = v->generation;
      s->pending = GG_MENU_NONE;
      result = GG_SERVICE_INVALID;
      if (kiosk && command != GG_MENU_BROWSE)
         s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_KIOSK;
      else
      {
         record = gg_source_service_record(s->service, s->input);
         switch (command)
         {
            case GG_MENU_ADD:
               result = gg_source_service_add(s->service, s->input,
                     now, (uint64_t)time(NULL));
               break;
            case GG_MENU_REFRESH:
               result = gg_source_service_refresh(s->service, s->input,
                     now, (uint64_t)time(NULL));
               break;
            case GG_MENU_ENABLE:
               result = record ? gg_source_service_set_enabled(s->service,
                     s->input, !record->enabled) : GG_SERVICE_NOT_FOUND;
               break;
            case GG_MENU_REMOVE:
               result = gg_source_service_remove(s->service, s->input);
               break;
            case GG_MENU_BROWSE:
               result = record && record->enabled
                  ? gg_source_service_browse(s->service, s->input)
                  : GG_SERVICE_DISABLED;
               break;
            case GG_MENU_CANCEL:
               if (v->state == GG_SERVICE_FETCHING
                     || v->state == GG_SERVICE_APPLYING
                     || v->state == GG_SERVICE_LOADING)
                  result = gg_source_service_cancel(s->service);
               else
                  result = GG_SERVICE_NOT_READY;
               break;
            default: break;
         }
         s->notice = gg_menu_result(result);
         if (result == GG_SERVICE_OK)
         {
            s->operation_generation = generation;
            s->active = command;
            strlcpy(s->operation_id, command == GG_MENU_ADD ? "" : s->input,
                  sizeof(s->operation_id));
         }
      }
      s->dirty = true;
   }

   menu_entries_get_last_stack(&path, &label, NULL, NULL, NULL);
   if (!menu_gamegate_sources_page(label) || !path)
      return;
   gg_menu_local_id(id, path);
   if (*id && strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM_STR))
   {
      const gg_source_record_t *record = gg_source_service_record(s->service, id);
      v = gg_source_service_view(s->service);
      if (v->catalog && !strcmp(v->catalog_id, id))
         s->browse_id[0] = '\0';
      else if (record && record->enabled
            && (v->state == GG_SERVICE_IDLE || v->state == GG_SERVICE_ERROR)
            && (strcmp(s->browse_id, id) || s->browse_revision != record->cache_revision))
      {
         strlcpy(s->browse_id, id, sizeof(s->browse_id));
         s->browse_revision = record->cache_revision;
         result = gg_source_service_browse(s->service, id);
         s->notice = gg_menu_result(result);
         if (result == GG_SERVICE_OK)
         {
            s->active = GG_MENU_BROWSE;
            strlcpy(s->operation_id, id, sizeof(s->operation_id));
         }
         s->dirty = true;
      }
   }
}

static bool gg_menu_ensure(void)
{
   gg_menu_sources_t *s;
   retro_task_t *task;
   const char *base;
   settings_t *settings = config_get_ptr();
   if (gg_menu_sources)
      return true;
   s = (gg_menu_sources_t*)calloc(1, sizeof(*s));
   task = task_init();
   if (!s || !task)
   {
      free(s);
      free(task);
      return false;
   }
   base = settings->paths.directory_menu_config;
   if (!string_is_empty(base))
      fill_pathname_expand_special(s->root, base, sizeof(s->root));
   else if (!path_is_empty(RARCH_PATH_CONFIG))
      fill_pathname_basedir(s->root, path_get(RARCH_PATH_CONFIG), sizeof(s->root));
   else
      strlcpy(s->root, g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG], sizeof(s->root));
   if (!*s->root || strlen(s->root) + sizeof("/gamegate/sources") >= sizeof(s->root))
   {
      free(task);
      free(s);
      return false;
   }
   strlcat(s->root, "/gamegate/sources", sizeof(s->root));
   s->notice = MSG_UNKNOWN;
   s->kiosk = gg_menu_kiosk();
   task->state = s;
   task->handler = gg_menu_task_handler;
   task->cleanup = gg_menu_task_cleanup;
   task->flags |= RETRO_TASK_FLG_MAIN_THREAD | RETRO_TASK_FLG_MUTE;
   gg_menu_sources = s;
   if (!task_queue_push(task))
   {
      gg_menu_task_cleanup(task);
      free(task);
      return false;
   }
   return true;
}

static void gg_menu_queue(enum gg_menu_command command, const char *input)
{
   gg_menu_sources_t *s = gg_menu_sources;
   if (!s)
      return;
   if (s->unavailable)
      s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_UNSUPPORTED;
   else if (gg_menu_kiosk() && command != GG_MENU_BROWSE)
      s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_KIOSK;
   else if (s->pending != GG_MENU_NONE)
      s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_BUSY;
   else if (input && strlen(input) > GG_CATALOG_MAX_URL_BYTES)
      s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_INVALID_URL;
   else
   {
      s->pending = command;
      strlcpy(s->input, input ? input : "", sizeof(s->input));
      s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_WORKING;
   }
   s->dirty = true;
}

static void gg_menu_input_complete(void *userdata, const char *line)
{
   menu_input_dialog_end();
   if (line && *line)
      gg_menu_queue(GG_MENU_ADD, line);
}

static int gg_menu_back(const char *path, const char *label,
      unsigned type, size_t idx)
{
   struct menu_state *menu_st = menu_state_get_ptr();
   const char *current = NULL;
   const char *route = NULL;
   menu_entries_get_last_stack(&current, &route, NULL, NULL, NULL);
   if (gg_menu_sources && current && route
         && !strcmp(route, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR))
      strlcpy(gg_menu_sources->return_id, current,
            sizeof(gg_menu_sources->return_id));
   menu_entries_pop_stack(&menu_st->selection_ptr, 0, true);
   if (gg_menu_sources)
      gg_menu_sources->dirty = true;
   return 0;
}

int menu_gamegate_sources_ok(const char *path, const char *label,
      unsigned type, size_t idx, size_t entry_idx)
{
   menu_displaylist_info_t info;
   struct menu_state *menu_st = menu_state_get_ptr();
   gg_menu_sources_t *s;
   const gg_source_record_t *record;
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   if (!gg_menu_ensure())
      return -1;
   s = gg_menu_sources;
   gg_menu_local_id(id, path);
   record = s->service ? gg_source_service_record(s->service, id) : NULL;
   if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_TEXT_STR))
      return 0;
   if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_BACK_STR))
      return gg_menu_back(path, label, type, idx);
   if (!menu_gamegate_sources_page(label))
   {
      if (gg_menu_kiosk())
      {
         s->notice = MENU_ENUM_LABEL_VALUE_GG_SOURCE_KIOSK;
         s->dirty = true;
         return 0;
      }
      if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_ADD_STR))
      {
         menu_input_ctx_line_t line = {0};
         line.label = GG_TEXT(ADD);
         line.text_type = MENU_INPUT_DIALOG_KB_TYPE_TEXT;
         line.cb = gg_menu_input_complete;
         menu_input_dialog_start(&line);
      }
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_REFRESH_STR))
         gg_menu_queue(GG_MENU_REFRESH, id);
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_ENABLE_STR))
         gg_menu_queue(GG_MENU_ENABLE, id);
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_REMOVE_STR))
         gg_menu_queue(GG_MENU_REMOVE, id);
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_CANCEL_STR))
         gg_menu_queue(GG_MENU_CANCEL, id);
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_RETRY_STR))
         gg_menu_queue(GG_MENU_BROWSE, id);
      return 0;
   }
   if (strcmp(label, MENU_ENUM_LABEL_GAMEGATE_SOURCES_STR))
   {
      if (!record || (gg_menu_kiosk() && (!record->enabled
                  || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM_STR))))
         return 0;
      if (!record->enabled && (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAMES_STR)
               || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR)))
         return 0;
      if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR)
            && (!strchr(path, '/') || !gg_source_service_game(s->service,
                  id, strchr(path, '/') + 1)))
         return 0;
   }
   menu_displaylist_info_init(&info);
   info.list = MENU_LIST_GET(menu_st->entries.list, 0);
   info.type = MENU_SETTING_ACTION;
   info.directory_ptr = idx;
   info.path = strdup(*id ? path : "");
   info.label = strdup(label);
   if (menu_displaylist_ctl(DISPLAYLIST_GENERIC, &info, config_get_ptr()))
      menu_displaylist_process(&info);
   menu_displaylist_info_free(&info);
   s->dirty = true;
   return 0;
}

int menu_gamegate_sources_label(file_list_t *list, unsigned type,
      unsigned i, const char *label, const char *path, char *s, size_t len)
{
   const char *text = i < list->size ? list->list[i].alt : NULL;
   strlcpy(s, text ? text : (path ? path : ""), len);
   return 0;
}

static void gg_menu_row(file_list_t *list, const char *id,
      enum msg_hash_enums route, const char *text)
{
   menu_file_list_cbs_t *cbs;
   if (!menu_entries_append(list, id ? id : "", msg_hash_to_str(route),
            route, route == MENU_ENUM_LABEL_GG_SOURCE_TEXT
            ? MENU_SETTING_NO_ITEM : MENU_SETTING_ACTION, 0, 0, NULL))
      return;
   file_list_set_alt_at_offset(list, list->size - 1, text);
   cbs = (menu_file_list_cbs_t*)list->list[list->size - 1].actiondata;
   if (cbs)
   {
      cbs->action_ok = menu_gamegate_sources_ok;
      cbs->action_label = menu_gamegate_sources_label;
      cbs->action_cancel = gg_menu_back;
   }
}

static void gg_menu_text(file_list_t *list, const char *title, const char *value)
{
   char text[768];
   const char *p = value;
   size_t n;
   if (!p || !*p)
      p = GG_TEXT(UNKNOWN);
   do
   {
      size_t prefix = snprintf(text, sizeof(text), "%s: ", title);
      n = strlen(p);
      if (n > 512)
      {
         n = 512;
         while (n && (((unsigned char)p[n] & 0xc0) == 0x80))
            n--;
      }
      memcpy(text + prefix, p, n);
      text[prefix + n] = '\0';
      gg_menu_row(list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, text);
      p += n;
   } while (*p);
}

static const char *gg_menu_platform(const gg_game_t *game)
{
   if (!strcmp(game->platform, "nes")) return "NES";
   if (!strcmp(game->platform, "gb")) return "Game Boy";
   if (!strcmp(game->platform, "gbc")) return "Game Boy Color";
   if (!strcmp(game->platform, "gba")) return "Game Boy Advance";
   if (!strcmp(game->platform, "snes")) return "SNES";
   if (!strcmp(game->platform, "megadrive")) return "Mega Drive";
   return GG_TEXT(UNKNOWN_PLATFORM);
}

static const char *gg_menu_state(const gg_source_view_t *v)
{
   switch (v->state)
   {
      case GG_SERVICE_INITIALIZING: return GG_TEXT(INITIALIZING);
      case GG_SERVICE_FETCHING: return GG_TEXT(FETCHING);
      case GG_SERVICE_APPLYING: return GG_TEXT(APPLYING);
      case GG_SERVICE_LOADING: return GG_TEXT(LOADING);
      case GG_SERVICE_ERROR: return GG_TEXT(FAILED);
      default: return GG_TEXT(CACHED);
   }
}

void menu_gamegate_sources_build(menu_displaylist_info_t *info)
{
   gg_menu_sources_t *s;
   const gg_source_view_t *v;
   const gg_source_record_t *record;
   const gg_catalog_t *catalog;
   const char *route = info->label ? info->label : "";
   char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
   char text[768];
   size_t i;
   bool kiosk = gg_menu_kiosk();
   menu_entries_clear(info->list);
   if (!gg_menu_ensure())
   {
      gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(STORAGE));
      return;
   }
   s = gg_menu_sources;
   v = s->service ? gg_source_service_view(s->service) : NULL;
   gg_menu_local_id(id, info->path);
   record = s->service ? gg_source_service_record(s->service, id) : NULL;
   catalog = v && v->catalog && !strcmp(v->catalog_id, id) ? v->catalog : NULL;

   if (!strcmp(route, MENU_ENUM_LABEL_GG_SOURCE_GAMES_STR) && record && record->enabled && catalog)
   {
      for (i = 0; i < catalog->game_count; i++)
      {
         const gg_game_t *game = &catalog->games[i];
         gg_game_text_t chosen = gg_game_select_text(game, gg_menu_language());
         char identity[MENU_GAMEGATE_SOURCE_ID_SIZE];
         snprintf(identity, sizeof(identity), "%s/%s", id, game->id);
         snprintf(text, sizeof(text), "%s [%s]", chosen.name, gg_menu_platform(game));
         gg_menu_row(info->list, identity, MENU_ENUM_LABEL_GG_SOURCE_GAME, text);
      }
      file_list_sort_on_alt(info->list);
      if (!catalog->game_count)
         gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(EMPTY_GAMES));
      if (*s->return_id)
      {
         for (i = 0; i < info->list->size; i++)
            if (!strcmp(info->list->list[i].path, s->return_id))
               menu_state_get_ptr()->selection_ptr = i;
         s->return_id[0] = '\0';
      }
   }
   else if (!strcmp(route, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR) && record && record->enabled && catalog)
   {
      const char *game_id = info->path ? strchr(info->path, '/') : NULL;
      const gg_game_t *game = game_id ? gg_source_service_game(s->service, id, game_id + 1) : NULL;
      if (game)
      {
         gg_game_text_t chosen = gg_game_select_text(game, gg_menu_language());
         gg_menu_text(info->list, GG_TEXT(NAME), chosen.name);
         gg_menu_text(info->list, GG_TEXT(ORIGINAL_NAME), game->original_name);
         gg_menu_text(info->list, GG_TEXT(DESCRIPTION), chosen.description ? chosen.description : GG_TEXT(NO_DESCRIPTION));
         gg_menu_text(info->list, GG_TEXT(NAME_LANGUAGE), chosen.name_language);
         gg_menu_text(info->list, GG_TEXT(DESCRIPTION_LANGUAGE), chosen.description_language);
         gg_menu_text(info->list, GG_TEXT(ORIGINAL_LANGUAGE), game->original_language);
         gg_menu_text(info->list, GG_TEXT(PROVIDED_BY), record->name);
         gg_menu_text(info->list, GG_TEXT(PLATFORM), gg_menu_platform(game));
         gg_menu_text(info->list, GG_TEXT(PLATFORM_CODE), game->platform);
         gg_menu_text(info->list, GG_TEXT(REGION), game->region);
         gg_menu_text(info->list, GG_TEXT(VERSION), game->version);
         gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(BROWSE_ONLY));
         if (!game->download)
         {
            gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(NO_DOWNLOAD));
            gg_menu_text(info->list, GG_TEXT(SIZE), GG_TEXT(UNKNOWN));
         }
         else
         {
            gg_menu_text(info->list, GG_TEXT(DOWNLOAD_URL), game->download->url);
            if (game->download->has_size)
               snprintf(text, sizeof(text), "%" PRIu64, game->download->size_bytes);
            else
               strlcpy(text, GG_TEXT(UNKNOWN), sizeof(text));
            gg_menu_text(info->list, GG_TEXT(SIZE), text);
            if (game->download->sha256)
               gg_menu_text(info->list, "SHA-256", game->download->sha256);
         }
         for (i = 0; i < game->external_id_count; i++)
         {
            snprintf(text, sizeof(text), "%s / %s", GG_TEXT(EXTERNAL_ID), game->external_ids[i].provider);
            gg_menu_text(info->list, text, game->external_ids[i].value);
         }
         if (game->cover_url)
            gg_menu_text(info->list, GG_TEXT(COVER_URL), game->cover_url);
      }
   }
   else if (!strcmp(route, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM_STR) && record && !kiosk)
   {
      gg_menu_text(info->list, GG_TEXT(REMOVE_QUESTION), record->name);
      gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_BACK, GG_TEXT(BACK));
      gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_REMOVE, GG_TEXT(REMOVE));
   }
   else if (!strcmp(route, MENU_ENUM_LABEL_GG_SOURCE_DETAIL_STR) && record && (!kiosk || record->enabled))
   {
      gg_menu_text(info->list, GG_TEXT(NAME), record->name);
      gg_menu_text(info->list, GG_TEXT(STATE), record->enabled ? GG_TEXT(ENABLED) : GG_TEXT(DISABLED));
      if (record->enabled)
         gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_GAMES, GG_TEXT(GAMES));
      if (!kiosk)
      {
         if (record->enabled)
         {
            gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_REFRESH, GG_TEXT(REFRESH));
            gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_RETRY, GG_TEXT(RETRY));
         }
         gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_ENABLE,
               record->enabled ? GG_TEXT(DISABLE) : GG_TEXT(ENABLE));
         gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM, GG_TEXT(CONFIRM));
      }
      if (record->last_success)
      {
         time_t date = (time_t)record->last_success;
         struct tm *utc = gmtime(&date);
         if (utc)
            strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", utc);
         else
            strlcpy(text, GG_TEXT(UNKNOWN), sizeof(text));
      }
      else
         strlcpy(text, GG_TEXT(UNKNOWN), sizeof(text));
      gg_menu_text(info->list, GG_TEXT(LAST_SUCCESS), text);
      for (i = 0; i < GG_CATALOG_MAX_SOURCES; i++)
         if (!strcmp(s->errors[i].id, id) && s->errors[i].message != MSG_UNKNOWN)
            gg_menu_text(info->list, GG_TEXT(LAST_ERROR), msg_hash_to_str(s->errors[i].message));
      if (catalog)
      {
         if (catalog->source.description)
            gg_menu_text(info->list, GG_TEXT(DESCRIPTION), catalog->source.description);
         if (catalog->source.homepage)
            gg_menu_text(info->list, GG_TEXT(HOMEPAGE), catalog->source.homepage);
      }
   }
   else if (!strcmp(route, MENU_ENUM_LABEL_GAMEGATE_SOURCES_STR))
   {
      size_t visible = 0;
      if (!kiosk)
         gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_ADD, GG_TEXT(ADD));
      if (v)
         for (i = 0; i < v->count; i++)
         {
            const gg_source_record_t *r = &v->records[i];
            if (kiosk && !r->enabled)
               continue;
            snprintf(text, sizeof(text), "%s [%s]", r->name,
                  r->enabled ? GG_TEXT(ENABLED) : GG_TEXT(DISABLED));
            gg_menu_row(info->list, r->local_id, MENU_ENUM_LABEL_GG_SOURCE_DETAIL, text);
            visible++;
         }
      if (!visible)
         gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT,
               kiosk ? GG_TEXT(EMPTY_KIOSK) : GG_TEXT(EMPTY));
   }
   if (s->notice != MSG_UNKNOWN)
      gg_menu_text(info->list, GG_TEXT(STATUS), msg_hash_to_str(s->notice));
   if (v)
   {
      gg_menu_text(info->list, GG_TEXT(STATUS), gg_menu_state(v));
      if (v->error_domain != GG_SERVICE_DOMAIN_NONE)
         gg_menu_text(info->list, GG_TEXT(LAST_ERROR), msg_hash_to_str(gg_menu_error(v)));
      if (!kiosk && (v->state == GG_SERVICE_FETCHING
               || v->state == GG_SERVICE_APPLYING || v->state == GG_SERVICE_LOADING))
         gg_menu_row(info->list, id, MENU_ENUM_LABEL_GG_SOURCE_CANCEL, GG_TEXT(CANCEL));
   }
   else if (!s->unavailable)
      gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(INITIALIZING));
#if !defined(_WIN32) && !defined(ANDROID)
   gg_menu_row(info->list, "", MENU_ENUM_LABEL_GG_SOURCE_TEXT, GG_TEXT(UNSUPPORTED));
#endif
}

bool menu_gamegate_sources_refresh(void)
{
   const char *path = NULL;
   const char *label = NULL;
   struct menu_state *menu_st = menu_state_get_ptr();
   gg_menu_sources_t *s = gg_menu_sources;
   if (!s || !s->dirty)
      return false;
   menu_entries_get_last_stack(&path, &label, NULL, NULL, NULL);
   if (!menu_gamegate_sources_page(label))
      return false;
   s->dirty = false;
   while (path && label && strcmp(label, MENU_ENUM_LABEL_GAMEGATE_SOURCES_STR))
   {
      char id[GG_SOURCE_LOCAL_ID_BYTES + 1];
      const gg_source_record_t *record;
      const gg_source_view_t *v = s->service ? gg_source_service_view(s->service) : NULL;
      bool pop = false;
      gg_menu_local_id(id, path);
      record = s->service ? gg_source_service_record(s->service, id) : NULL;
      if (!record || (gg_menu_kiosk() && (!record->enabled
                  || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_CONFIRM_STR))))
         pop = true;
      else if ((!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAMES_STR)
               || !strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR)) && !record->enabled)
         pop = true;
      else if (!strcmp(label, MENU_ENUM_LABEL_GG_SOURCE_GAME_STR)
            && v && v->catalog && !strcmp(v->catalog_id, id)
            && strchr(path, '/') && !gg_source_service_game(s->service, id, strchr(path, '/') + 1))
         pop = true;
      if (!pop)
         break;
      menu_entries_pop_stack(&menu_st->selection_ptr, 0, false);
      menu_entries_get_last_stack(&path, &label, NULL, NULL, NULL);
      if (!menu_gamegate_sources_page(label))
         return false;
   }
   return true;
}
