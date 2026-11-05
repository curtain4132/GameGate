#ifndef MENU_GAMEGATE_SOURCES_H
#define MENU_GAMEGATE_SOURCES_H

#include "menu_displaylist.h"
#include "../gamegate/source_store.h"

#define MENU_GAMEGATE_SOURCE_ID_SIZE (GG_SOURCE_LOCAL_ID_BYTES + GG_CATALOG_MAX_ID_BYTES + 2)

bool menu_gamegate_sources_page(const char *label);
bool menu_gamegate_sources_refresh(void);
void menu_gamegate_sources_build(menu_displaylist_info_t *info);
int menu_gamegate_sources_ok(const char *path, const char *label,
      unsigned type, size_t idx, size_t entry_idx);
int menu_gamegate_sources_label(file_list_t *list, unsigned type,
      unsigned i, const char *label, const char *path, char *s, size_t len);

#endif
