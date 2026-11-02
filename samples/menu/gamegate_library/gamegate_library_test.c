/* Links the shipping frontend; no menu or message-query stubs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <file/config_file.h>
#include <file/file_path.h>
#include <string/stdstring.h>
#include <time/rtime.h>
#include <queues/task_queue.h>

#include "../../../configuration.h"
#include "../../../command.h"
#include "../../../retroarch.h"
#include "../../../frontend/frontend_driver.h"
#include "../../../menu/menu_driver.h"
#include "../../../menu/menu_entries.h"
#include "../../../menu/menu_displaylist.h"
#include "../../../menu/menu_dirwalk.h"
#include "../../../msg_hash_lbl_str.h"

static unsigned gg_failures;

#define GG_CHECK(condition, message) do { \
   if (!(condition)) { \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, message); \
      gg_failures++; \
   } \
} while (0)

static file_list_t *gg_list(void)
{
   return MENU_LIST_GET_SELECTION(menu_state_get_ptr()->entries.list, 0);
}

static size_t gg_find(enum msg_hash_enums id)
{
   file_list_t *list = gg_list();
   size_t i;
   for (i = 0; i < list->size; i++)
   {
      menu_file_list_cbs_t *cbs =
            (menu_file_list_cbs_t*)list->list[i].actiondata;
      if (cbs && cbs->enum_idx == id)
         return i;
   }
   return list->size;
}

static void gg_build(enum menu_displaylist_ctl_state state, const char *path)
{
   menu_displaylist_info_t info;
   menu_displaylist_info_init(&info);
   info.list = gg_list();
   info.path = strdup(path);
   GG_CHECK(menu_displaylist_ctl(state, &info, config_get_ptr()),
         "displaylist build");
   menu_displaylist_info_free(&info);
}

static void gg_playlists(enum menu_displaylist_ctl_state state, const char *path)
{
   unsigned i;
   /* Drive bounded task frames, rather than sleeping for directory I/O. */
   for (i = 0; i < 1000; i++)
   {
      gg_build(state, path);
      if (gg_find(MSG_LOADING) == gg_list()->size
            && !menu_dirwalk_pending())
         return;
      task_queue_check();
   }
   GG_CHECK(false, "directory walk completed within frame budget");
}

static void gg_messages(void)
{
   static const char chinese[] =
         "\xe6\x88\x91\xe7\x9a\x84\xe6\xb8\xb8\xe6\x88\x8f\xe5\xba\x93";
   static const char chinese_sublabel[] =
         "\xe5\xaf\xbc\xe5\x85\xa5\xe6\x9c\xac\xe5\x9c\xb0\xe6\xb8\xb8\xe6\x88\x8f"
         "\xef\xbc\x8c\xe6\xb5\x8f\xe8\xa7\x88\xe6\x82\xa8\xe7\x9a\x84"
         "\xe6\xb8\xb8\xe6\x88\x8f\xe9\x9b\x86\xe5\x90\x88\xe3\x80\x82";
   static const char english_sublabel[] =
         "Import local games and browse your game collections.";
   unsigned language = *msg_hash_get_uint(MSG_HASH_USER_LANGUAGE);
   msg_hash_set_uint(MSG_HASH_USER_LANGUAGE, RETRO_LANGUAGE_ENGLISH);
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_LABEL_VALUE_GAMEGATE_LIBRARY),
         "My Game Library"), "English title");
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_SUBLABEL_GAMEGATE_LIBRARY),
         english_sublabel), "English sublabel");
   msg_hash_set_uint(MSG_HASH_USER_LANGUAGE, RETRO_LANGUAGE_CHINESE_SIMPLIFIED);
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_LABEL_VALUE_GAMEGATE_LIBRARY),
         chinese), "Simplified Chinese title");
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_SUBLABEL_GAMEGATE_LIBRARY),
         chinese_sublabel),
         "Simplified Chinese sublabel");
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_LABEL_GAMEGATE_LIBRARY),
         MENU_ENUM_LABEL_GAMEGATE_LIBRARY_STR), "language-independent identity");
   msg_hash_set_uint(MSG_HASH_USER_LANGUAGE, RETRO_LANGUAGE_FRENCH);
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_LABEL_VALUE_GAMEGATE_LIBRARY),
         "My Game Library"), "missing translation falls back to English");
   GG_CHECK(!strcmp(msg_hash_to_str(MENU_ENUM_SUBLABEL_GAMEGATE_LIBRARY),
         english_sublabel), "French sublabel falls back to English");
   msg_hash_set_uint(MSG_HASH_USER_LANGUAGE, language);
}

static void gg_navigation(void)
{
   menu_displaylist_info_t info;
   file_list_t *list;
   file_list_t *stack;
   menu_file_list_cbs_t *cbs;
   size_t index;
   char text[256];

   gg_build(DISPLAYLIST_MAIN_MENU, "");
   list = gg_list();
   index = gg_find(MENU_ENUM_LABEL_GAMEGATE_LIBRARY);
   GG_CHECK(index < list->size, "independent main menu entry");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_PLAYLISTS_TAB) < list->size,
         "original Playlists entry retained");
   if (index >= list->size)
      return;
   cbs = (menu_file_list_cbs_t*)list->list[index].actiondata;
   GG_CHECK(cbs->action_sublabel != NULL, "sublabel binding");
   if (cbs->action_sublabel)
   {
      cbs->action_sublabel(list, list->list[index].type, (unsigned)index,
            list->list[index].label, list->list[index].path, text, sizeof(text));
      GG_CHECK(!strcmp(text, msg_hash_to_str(MENU_ENUM_SUBLABEL_GAMEGATE_LIBRARY)),
            "sublabel lookup through real sorted map");
   }
   GG_CHECK(cbs->action_ok != NULL, "OK binding");
   if (!cbs->action_ok)
      return;
   cbs->action_ok(list->list[index].path, list->list[index].label,
         list->list[index].type, index, list->list[index].entry_idx);
   stack = MENU_LIST_GET(menu_state_get_ptr()->entries.list, 0);
   cbs = (menu_file_list_cbs_t*)stack->list[stack->size - 1].actiondata;
   GG_CHECK(cbs && cbs->action_deferred_push, "library deferred binding");
   if (!cbs || !cbs->action_deferred_push)
      return;
   menu_displaylist_info_init(&info);
   info.list = list;
   info.path = strdup(stack->list[stack->size - 1].path);
   info.label = strdup(stack->list[stack->size - 1].label);
   GG_CHECK(!strcmp(info.label, MENU_ENUM_LABEL_GAMEGATE_LIBRARY_STR),
         "library stack identity");
   GG_CHECK(!strcmp(info.path, config_get_ptr()->paths.directory_playlist),
         "existing playlist directory passed through");
   if (cbs->action_get_title)
   {
      cbs->action_get_title(info.path, info.label, MENU_SETTING_ACTION,
            text, sizeof(text));
      GG_CHECK(!strcmp(text, "My Game Library"), "page title binding");
   }
   else
      GG_CHECK(false, "page title callback");
   GG_CHECK(cbs->action_deferred_push(&info) == 0, "open library page");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) < list->size,
         "import reachable through real navigation");
   menu_displaylist_info_free(&info);
}

static void gg_check_collections(void)
{
   static const char *names[] = {"Alpha.lpl", "Beta.lpl"};
   file_list_t *list = gg_list();
   size_t i, count = 0;
   for (i = 0; i < list->size; i++)
      if (list->list[i].type == FILE_TYPE_PLAYLIST_COLLECTION)
      {
         menu_file_list_cbs_t *cbs =
               (menu_file_list_cbs_t*)list->list[i].actiondata;
         GG_CHECK(cbs && cbs->enum_idx == MENU_ENUM_LABEL_PLAYLIST_COLLECTION_ENTRY,
               "native collection identity");
         GG_CHECK(cbs && cbs->action_ok != NULL, "native collection action");
         GG_CHECK(list->list[i].entry_idx == count, "collection index excludes shortcuts");
         if (count < 2)
            GG_CHECK(!strcmp(path_basename(list->list[i].path), names[count]),
                  "collection filenames and ordering retained");
         count++;
      }
   GG_CHECK(count == 2, "two collections; histories/favorites/non-playlists excluded");
}

static void gg_collections(const char *dir)
{
   settings_t *settings = config_get_ptr();
   bool show_playlists = settings->bools.menu_content_show_playlists;
   bool show_favorites = settings->bools.menu_content_show_favorites;
   bool show_history   = settings->bools.menu_content_show_history;
   bool kiosk          = settings->bools.kiosk_mode_enable;
   unsigned show_add   = settings->uints.menu_content_show_add_entry;

   settings->bools.menu_content_show_playlists = true;
   settings->bools.menu_content_show_favorites = true;
   settings->bools.menu_content_show_history = true;
   settings->bools.kiosk_mode_enable = false;
   settings->uints.menu_content_show_add_entry = MENU_ADD_CONTENT_ENTRY_DISPLAY_HIDDEN;
   gg_playlists(DISPLAYLIST_GAMEGATE_LIBRARY, dir);
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) == 0,
         "import first even when original import entry hidden");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_GOTO_FAVORITES) < gg_list()->size,
         "visible favorites reused");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_LOAD_CONTENT_HISTORY) < gg_list()->size,
         "visible history reused");
   gg_check_collections();
   GG_CHECK(settings->uints.menu_content_show_add_entry ==
         MENU_ADD_CONTENT_ENTRY_DISPLAY_HIDDEN, "global import setting unchanged");
   settings->bools.menu_content_show_playlists = false;
   gg_playlists(DISPLAYLIST_GAMEGATE_LIBRARY, dir);
   GG_CHECK(gg_find(MENU_ENUM_LABEL_PLAYLIST_COLLECTION_ENTRY) == gg_list()->size,
         "collection visibility respected");
   settings->bools.menu_content_show_favorites = false;
   settings->bools.menu_content_show_history = false;
   gg_playlists(DISPLAYLIST_GAMEGATE_LIBRARY, "");
   GG_CHECK(gg_list()->size == 1
         && gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) == 0,
         "empty/unset library still imports, hidden shortcuts stay hidden");
   settings->bools.kiosk_mode_enable = true;
   gg_playlists(DISPLAYLIST_GAMEGATE_LIBRARY, "");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) == gg_list()->size,
         "kiosk hides import");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_NO_PLAYLISTS) < gg_list()->size,
         "empty kiosk library has placeholder");
   settings->bools.menu_content_show_playlists = true;
   gg_playlists(DISPLAYLIST_GAMEGATE_LIBRARY, dir);
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) == gg_list()->size,
         "populated kiosk library hides import");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_NO_PLAYLISTS) == gg_list()->size,
         "populated kiosk library has no empty placeholder");
   gg_check_collections();
   settings->bools.kiosk_mode_enable = false;
   gg_build(DISPLAYLIST_ADD_CONTENT_LIST, "");
   GG_CHECK(gg_find(MENU_ENUM_LABEL_MANUAL_CONTENT_SCAN_LIST) < gg_list()->size,
         "existing local manual import retained");
   settings->bools.menu_content_show_playlists = show_playlists;
   settings->bools.menu_content_show_favorites = show_favorites;
   settings->bools.menu_content_show_history = show_history;
   settings->bools.kiosk_mode_enable = kiosk;
   settings->uints.menu_content_show_add_entry = show_add;
}

static void gg_original_playlists(const char *dir)
{
   settings_t *settings = config_get_ptr();
   unsigned show_add = settings->uints.menu_content_show_add_entry;
   bool kiosk = settings->bools.kiosk_mode_enable;
   menu_displaylist_info_t info;
   file_list_t *list;
   file_list_t *stack;
   menu_file_list_cbs_t *cbs;
   size_t index;

   settings->bools.kiosk_mode_enable = false;
   settings->uints.menu_content_show_add_entry = MENU_ADD_CONTENT_ENTRY_DISPLAY_HIDDEN;
   gg_playlists(DISPLAYLIST_DATABASE_PLAYLISTS, dir);
   gg_check_collections();
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) == gg_list()->size,
         "original Playlists respects hidden import setting");
   settings->uints.menu_content_show_add_entry = MENU_ADD_CONTENT_ENTRY_DISPLAY_PLAYLISTS_TAB;
   gg_playlists(DISPLAYLIST_DATABASE_PLAYLISTS, dir);
   gg_check_collections();
   GG_CHECK(gg_find(MENU_ENUM_LABEL_ADD_CONTENT_LIST) < gg_list()->size,
         "original Playlists retains configured import entry");
   settings->uints.menu_content_show_add_entry = show_add;
   settings->bools.kiosk_mode_enable = kiosk;

   list = gg_list();
   index = gg_find(MENU_ENUM_LABEL_PLAYLIST_COLLECTION_ENTRY);
   if (index >= list->size)
      return;
   cbs = (menu_file_list_cbs_t*)list->list[index].actiondata;
   if (!cbs || !cbs->action_ok)
      return;
   GG_CHECK(cbs->action_ok(list->list[index].path, list->list[index].label,
         list->list[index].type, index, list->list[index].entry_idx) == 0,
         "original collection OK callback");
   stack = MENU_LIST_GET(menu_state_get_ptr()->entries.list, 0);
   cbs = (menu_file_list_cbs_t*)stack->list[stack->size - 1].actiondata;
   GG_CHECK(cbs && cbs->action_deferred_push, "original collection deferred binding");
   if (!cbs || !cbs->action_deferred_push)
      return;
   menu_displaylist_info_init(&info);
   info.list = list;
   info.path = strdup(stack->list[stack->size - 1].path);
   info.label = strdup(stack->list[stack->size - 1].label);
   GG_CHECK(!strcmp(info.label, MENU_ENUM_LABEL_DEFERRED_PLAYLIST_LIST_STR),
         "original collection stack identity");
   GG_CHECK(!strcmp(path_basename(info.path), "Alpha.lpl"),
         "original collection path passed through");
   GG_CHECK(cbs->action_deferred_push(&info) == 0, "open original collection");
   GG_CHECK(list->size == 1, "original collection retains its game entry");
   if (list->size == 1)
   {
      GG_CHECK(list->list[0].type == FILE_TYPE_RPL_ENTRY,
            "original game entry type");
      GG_CHECK(!strcmp(list->list[0].path, "Alpha.lpl"),
            "original game entry label");
      GG_CHECK(list->list[0].entry_idx == 0, "original game entry index");
   }
   menu_displaylist_info_free(&info);
}

int main(int argc, char **argv)
{
   char *frontend_argv[4];
   if (argc != 3)
   {
      fprintf(stderr, "usage: gamegate_library_test config playlist-directory\n");
      return 2;
   }
   frontend_argv[0] = argv[0];
   frontend_argv[1] = (char*)"--menu";
   frontend_argv[2] = (char*)"--config";
   frontend_argv[3] = argv[1];
   config_file_set_io_default(config_file_io_filestream());
   rtime_init();
   retroarch_config_init();
   retroarch_ctl(RARCH_CTL_STATE_FREE, NULL);
   frontend_driver_init_first(NULL);
   if (!retroarch_main_init(4, frontend_argv))
      return 2;
   GG_CHECK(command_event(CMD_EVENT_HISTORY_INIT, NULL), "history fixture initialized");
   gg_messages();
   gg_navigation();
   gg_collections(argv[2]);
   gg_original_playlists(argv[2]);
   fprintf(stderr, "%s gamegate_library_test (%u failures)\n",
         gg_failures ? "FAIL" : "PASS", gg_failures);
   return gg_failures ? 1 : 0;
}
