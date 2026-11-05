#include "gamegate/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); \
   } } while (0)

static char *copy_string(const char *s)
{
   size_t n = strlen(s) + 1;
   char *p = (char*)malloc(n);
   CHECK(p != NULL);
   memcpy(p, s, n);
   return p;
}

static void test_urls(void)
{
   static const char *valid[] = {
      "https://example.org", "HTTPS://Example.org:1/?b=2&a=1&a=3",
      "hTtPs://xn--bcher-kva.example:65535/a%20b?x=%23",
      "https://example.org./", "https://127.0.0.1:443/",
      "https://[::]", "https://[::1]:443/", "https://[2001:db8::1]/",
      "https://[1:2:3:4:5:6:7:8]", "https://[1:2:3:4:5:6:7::]",
      "https://[::ffff:192.0.2.1]/", "https://[1:2:3:4:5:6:192.0.2.1]",
      "https://example.org/a@b?token=x+y&v=~_-.!$'()*+,;=:@/?"
   };
   static const char *invalid[] = {
      "", "https://", "https:///x", "https://?x", "http://example.org",
      "file:///tmp/x", "javascript:alert(1)", "httpsx://example.org",
      "https://user@example.org", "https://user:pw@example.org",
      "https://example.org#x", "https://example.org/#",
      "https://example.org/ x", "https://example.org/\t",
      "https://example.org/\r\n", "https://example.org/\177",
      "https://example.org/\\x", "https://example.org/\303\251",
      "https://example.org:", "https://example.org:0",
      "https://example.org:65536", "https://example.org:999999999999999999",
      "https://example.org:-1", "https://example.org:1x",
      "https://example.org:443:2", "https://-a.org", "https://a-.org",
      "https://a..org", "https://.org", "https://_a.org",
      "https://exa%mple.org", "https://256.1.2.3", "https://127.1",
      "https://01.2.3.4", "https://1.2.3.4.5", "https://1.2.3.4.",
      "https://[]", "https://[::1", "https://::1", "https://[::1]x",
      "https://[:::]", "https://[1::2::3]", "https://[1:2:3]",
      "https://[1:2:3:4:5:6:7:8:9]", "https://[1:2:3:4:5:6:7::8]",
      "https://[12345::]", "https://[::g]", "https://[::1:]",
      "https://[fe80::1%25eth0]", "https://[::ffff:256.1.2.3]",
      "https://[1:2:3:4:5:6::192.0.2.1]", "https://[::192.0.2.1:1]",
      "https://example.org/%", "https://example.org/%2",
      "https://example.org/%gg", "https://example.org/<x>"
   };
   size_t i;
   char *long_url = (char*)malloc(GG_CATALOG_MAX_URL_BYTES + 2);
   char raw[9] = {'h','t','t','p','s',':','/','/','a'};
   char nul[] = "https://example.org/a\0b";
   CHECK(long_url != NULL);
   for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
      CHECK(gg_catalog_https_url_valid(valid[i], strlen(valid[i])));
   for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
      CHECK(!gg_catalog_https_url_valid(invalid[i], strlen(invalid[i])));
   CHECK(!gg_catalog_https_url_valid(NULL, 9));
   CHECK(gg_catalog_https_url_valid(raw, sizeof(raw)));
   CHECK(!gg_catalog_https_url_valid(nul, sizeof(nul) - 1));
   memset(long_url, 'a', GG_CATALOG_MAX_URL_BYTES + 1);
   memcpy(long_url, "https://example.org/", 20);
   CHECK(gg_catalog_https_url_valid(long_url, GG_CATALOG_MAX_URL_BYTES));
   CHECK(!gg_catalog_https_url_valid(long_url, GG_CATALOG_MAX_URL_BYTES + 1));
   memcpy(long_url, "https://", 8);
   memset(long_url + 8, 'a', 64);
   CHECK(!gg_catalog_https_url_valid(long_url, 72));
   CHECK(gg_catalog_https_url_valid(long_url, 71));
   free(long_url);
}

static void test_model(void)
{
   gg_catalog_t *catalog;
   gg_game_t *game;
   gg_game_text_t text;
   unsigned stage;
   gg_catalog_free(NULL);
   for (stage = 0; stage < 5; stage++)
   {
      catalog = (gg_catalog_t*)calloc(1, sizeof(*catalog));
      CHECK(catalog != NULL);
      if (stage == 0)
      {
         gg_catalog_free(catalog);
         continue;
      }
      catalog->source.id = copy_string("source-stable");
      catalog->source.name = copy_string("Source");
      catalog->source.description = copy_string("Description");
      catalog->source.homepage = copy_string("https://example.org");
      catalog->schema_version = GG_CATALOG_SCHEMA_VERSION;
      catalog->games = (gg_game_t*)calloc(2, sizeof(*catalog->games));
      CHECK(catalog->games != NULL);
      catalog->game_count = 2;
      game = catalog->games;
      game->id = copy_string("stable-id");
      game->platform = copy_string("future-console");
      game->original_language = copy_string("ja");
      game->original_name = copy_string("Original");
      game->region = copy_string("world");
      game->version = copy_string("1.0");
      if (stage == 1)
      {
         gg_catalog_free(catalog);
         continue;
      }
      game->localized = (gg_localized_t*)calloc(3, sizeof(*game->localized));
      CHECK(game->localized != NULL);
      game->localized_count = 3;
      game->localized[0].language = copy_string("zh-Hans");
      game->localized[0].name = copy_string("Chinese name");
      game->localized[0].description = copy_string("");
      game->localized[1].language = copy_string("EN");
      game->localized[1].name = copy_string("English name");
      game->localized[1].description = copy_string("English description");
      game->localized[2].language = copy_string("ja");
      game->localized[2].name = copy_string("Japanese name");
      game->localized[2].description = copy_string("Japanese description");
      text = gg_game_select_text(game, "ZH-hANS");
      CHECK(text.name == game->localized[0].name);
      CHECK(text.name_language == game->localized[0].language);
      CHECK(text.description == game->localized[1].description);
      CHECK(text.description_language == game->localized[1].language);
      text = gg_game_select_text(game, "en");
      CHECK(text.name == game->localized[1].name);
      text = gg_game_select_text(game, "JA");
      CHECK(text.name == game->localized[2].name);
      CHECK(text.description_language == game->localized[2].language);
      game->localized[1].description[0] = '\0';
      text = gg_game_select_text(game, "zh-Hans");
      CHECK(text.name_language == game->localized[0].language);
      CHECK(text.description == game->localized[2].description);
      CHECK(text.description_language == game->localized[2].language);
      text = gg_game_select_text(game, "zh-Hant");
      CHECK(text.name == game->localized[1].name);
      game->localized[1].name[0] = '\0';
      text = gg_game_select_text(game, NULL);
      CHECK(text.name == game->localized[2].name);
      CHECK(text.name_language == game->localized[2].language);
      game->localized[2].name[0] = '\0';
      game->localized[2].description[0] = '\0';
      text = gg_game_select_text(game, "");
      CHECK(text.name == game->original_name);
      CHECK(text.name_language == game->original_language);
      CHECK(!text.description && !text.description_language);
      game->original_language[0] = '\0';
      text = gg_game_select_text(game, NULL);
      CHECK(text.name == game->original_name && !text.name_language);
      CHECK(strcmp(game->id, "stable-id") == 0);
      CHECK(strcmp(game->platform, "future-console") == 0);
      if (stage == 2)
      {
         gg_catalog_free(catalog);
         continue;
      }
      game->download = (gg_download_t*)calloc(1, sizeof(*game->download));
      CHECK(game->download != NULL);
      CHECK(!game->download->has_size && game->download->size_bytes == 0);
      game->download->has_size = true;
      CHECK(game->download->has_size && game->download->size_bytes == 0);
      game->download->size_bytes = (uint64_t)-1;
      CHECK(game->download->size_bytes > (uint64_t)0xffffffffUL);
      game->external_ids = (gg_external_id_t*)calloc(2,
            sizeof(*game->external_ids));
      CHECK(game->external_ids != NULL);
      game->external_id_count = 2;
      game->external_ids[0].provider = copy_string("igdb");
      if (stage == 4)
      {
         game->external_ids[0].value = copy_string("12345");
         game->download->url = copy_string("https://example.org/game");
         game->download->sha256 = copy_string(
               "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
      }
      gg_catalog_free(catalog);
   }
   text = gg_game_select_text(NULL, "en");
   CHECK(!text.name && !text.name_language);
   CHECK(!text.description && !text.description_language);
}

int main(void)
{
   test_urls();
   test_model();
   puts("catalog model tests passed");
   return 0;
}
