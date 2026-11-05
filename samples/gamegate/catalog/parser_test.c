#include "gamegate/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); \
   } } while (0)

static unsigned checks;
static const char empty_source[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}";
static const char game_prefix[] =
   "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[{";
static const char game_fields[] =
   "\"id\":\"g\",\"platform\":\"nes\",\"original_name\":\"Original\"";

struct buffer
{
   char *data;
   size_t size;
   size_t capacity;
};

static void append_bytes(struct buffer *b, const char *s, size_t n)
{
   if (b->size + n + 1 > b->capacity)
   {
      size_t capacity = (b->size + n + 1) * 2;
      char *data = (char*)realloc(b->data, capacity);
      CHECK(data != NULL);
      b->data = data;
      b->capacity = capacity;
   }
   memcpy(b->data + b->size, s, n);
   b->size += n;
   b->data[b->size] = '\0';
}

static void append(struct buffer *b, const char *s)
{
   append_bytes(b, s, strlen(s));
}

static void append_number(struct buffer *b, size_t value)
{
   char digits[32];
   size_t n = 0;
   do
   {
      digits[n++] = (char)('0' + value % 10);
      value /= 10;
   } while (value);
   while (n)
      append_bytes(b, &digits[--n], 1);
}

static void repeat(struct buffer *b, char c, size_t count)
{
   size_t i;
   for (i = 0; i < count; i++)
      append_bytes(b, &c, 1);
}

static gg_catalog_t *parse_bytes(const char *s, size_t n, bool valid)
{
   gg_catalog_parser_t *p = gg_catalog_parser_create(s, n);
   gg_catalog_parse_status_t status = GG_CATALOG_PARSE_WORKING;
   gg_catalog_t *catalog;
   size_t calls = 0;
   checks++;
   if (!p)
   {
      CHECK(!valid);
      return NULL;
   }
   CHECK(gg_catalog_parser_step(p, 0) == GG_CATALOG_PARSE_WORKING);
   while (status == GG_CATALOG_PARSE_WORKING)
   {
      CHECK(gg_catalog_parser_take_result(p) == NULL);
      status = gg_catalog_parser_step(p, 7);
      CHECK(++calls < 1000000);
   }
   if ((status == GG_CATALOG_PARSE_DONE) != valid)
   {
      fprintf(stderr, "case %u: expected %s, diagnostic: %s\n", checks,
            valid ? "valid" : "invalid", gg_catalog_parser_error(p));
      fprintf(stderr, "test input (%lu bytes): %.180s\n", (unsigned long)n, s);
   }
   CHECK((status == GG_CATALOG_PARSE_DONE) == valid);
   CHECK(gg_catalog_parser_step(p, 1) == status);
   catalog = gg_catalog_parser_take_result(p);
   CHECK((catalog != NULL) == valid);
   CHECK(gg_catalog_parser_take_result(p) == NULL);
   CHECK((*gg_catalog_parser_error(p) == '\0') == valid);
   gg_catalog_parser_destroy(p);
   return catalog;
}

static void expect(const char *s, bool valid)
{
   gg_catalog_free(parse_bytes(s, strlen(s), valid));
}

static gg_catalog_t *game_extra(const char *extra, bool valid)
{
   struct buffer b = {0};
   gg_catalog_t *catalog;
   append(&b, game_prefix);
   append(&b, game_fields);
   append(&b, extra);
   append(&b, "}]}");
   catalog = parse_bytes(b.data, b.size, valid);
   free(b.data);
   return catalog;
}

static void test_valid(void)
{
   struct buffer b = {0};
   gg_catalog_t *catalog = parse_bytes(empty_source, strlen(empty_source), true);
   gg_game_t *game;
   gg_game_text_t text;
   CHECK(catalog->game_count == 0);
   CHECK(catalog->schema_version == 1);
   CHECK(strcmp(catalog->source.id, "s") == 0);
   gg_catalog_free(catalog);
   append(&b, ",\"original_language\":\"ja\",\"region\":\"world\",\"version\":\"1.0\",");
   append(&b, "\"localized\":{\"zh-Hans\":{\"name\":\"\\u4e2d\\u6587\",\"description\":\"\"},");
   append(&b, "\"en\":{\"name\":\"English\",\"description\":\"English description\"},");
   append(&b, "\"ja\":{\"name\":\"Japanese\",\"description\":\"Japanese description\"}},");
   append(&b, "\"download\":{\"url\":\"https://example.org/game\",\"size_bytes\":0,");
   append(&b, "\"sha256\":\"0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef\"},");
   append(&b, "\"external_ids\":{\"igdb\":\"12345\",\"other.ns\":\"abc\"},");
   append(&b, "\"cover_url\":\"https://example.org/cover.png\"");
   catalog = game_extra(b.data, true);
   free(b.data);
   CHECK(catalog->game_count == 1);
   game = &catalog->games[0];
   CHECK(strcmp(game->id, "g") == 0);
   CHECK(strcmp(game->region, "world") == 0);
   CHECK(strcmp(game->version, "1.0") == 0);
   CHECK(game->localized_count == 3 && game->external_id_count == 2);
   CHECK(strcmp(game->external_ids[0].provider, "igdb") == 0);
   CHECK(strcmp(game->external_ids[0].value, "12345") == 0);
   CHECK(strcmp(game->cover_url, "https://example.org/cover.png") == 0);
   CHECK(game->download->has_size && game->download->size_bytes == 0);
   CHECK(strlen(game->download->sha256) == 64);
   text = gg_game_select_text(game, "ZH-hANS");
   CHECK(strcmp(text.name, "\344\270\255\346\226\207") == 0);
   CHECK(strcmp(text.name_language, "zh-Hans") == 0);
   CHECK(strcmp(text.description, "English description") == 0);
   CHECK(strcmp(text.description_language, "en") == 0);
   text = gg_game_select_text(game, "zh-Hant");
   CHECK(strcmp(text.name, "English") == 0);
   gg_catalog_free(catalog);
   catalog = game_extra("", true);
   CHECK(!catalog->games[0].download && !catalog->games[0].cover_url);
   text = gg_game_select_text(&catalog->games[0], "fr");
   CHECK(strcmp(text.name, "Original") == 0 && !text.description);
   gg_catalog_free(catalog);
   catalog = game_extra(",\"download\":{\"url\":\"https://a\"}", true);
   CHECK(!catalog->games[0].download->has_size);
   gg_catalog_free(catalog);
   catalog = game_extra(",\"original_language\":\"ja\",\"localized\":{"
         "\"en\":{\"name\":\"\",\"description\":\"\"},"
         "\"ja\":{\"name\":\"Japanese\",\"description\":\"Japanese description\"}}", true);
   CHECK(!catalog->games[0].localized[0].name);
   CHECK(!catalog->games[0].localized[0].description);
   text = gg_game_select_text(&catalog->games[0], "zh-Hans");
   CHECK(strcmp(text.name, "Japanese") == 0);
   CHECK(strcmp(text.description_language, "ja") == 0);
   gg_catalog_free(catalog);
   expect("{\"games\":[{\"original_name\":\"X\",\"platform\":\"unknown-console\",\"id\":\"A._-9\"}],"
         "\"source\":{\"name\":\"Source\",\"id\":\"A._-9\",\"description\":\"D\","
         "\"homepage\":\"https://a\"},\"schema_version\":1}", true);
   expect("{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\",\"description\":\"\"},"
         "\"games\":[],\"extra\":{\"schema_version\":99,\"list\":[null,true,false,123,{},[]]}}", true);
}

static void test_invalid(void)
{
   static const char *bad_root[] = {
      "", "null", "[]", "{}", "{", "{\"schema_version\":1}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"}}",
      "{\"schema_version\":1,\"games\":[]}",
      "{\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":2,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":1.0,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":1e0,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":\"1\",\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":true,\"source\":{},\"games\":[]}",
      "{\"schema_version\":1,\"schema_version\":1,\"source\":{},\"games\":[]}",
      "{\"schema_version\":1,\"source\":[],\"games\":[]}",
      "{\"schema_version\":1,\"source\":null,\"games\":[]}",
      "{\"schema_version\":1,\"source\":{},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\"},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"\"},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":false},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\",\"name\":\"S\"},\"games\":[]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":{}}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[1]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[{}]}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[],}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}x",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]}{}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[]/*x*/}",
      "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[],\"x\":\"\\u0000\"}"
   };
   static const char *bad_extra[] = {
      ",\"id\":\"g\"", ",\"\\u0069d\":\"g\"", ",\"platform\":\"nes\"",
      ",\"download\":null", ",\"download\":[]", ",\"download\":{}",
      ",\"download\":{\"url\":123}", ",\"download\":{\"url\":\"http://a\"}",
      ",\"download\":{\"url\":\"https://user:pw@a\"}",
      ",\"download\":{\"url\":\"https://a#x\"}",
      ",\"download\":{\"url\":\"https://a\",\"sha256\":\"bad\"}",
      ",\"download\":{\"url\":\"https://a\",\"sha256\":false}",
      ",\"download\":{\"url\":\"https://a\",\"url\":\"https://b\"}",
      ",\"download\":{\"url\":\"https://a\",\"size_bytes\":0,\"size_bytes\":1}",
      ",\"cover_url\":\"file:///x\"", ",\"cover_url\":null",
      ",\"localized\":[]", ",\"localized\":null", ",\"localized\":{\"en\":\"Name\"}",
      ",\"localized\":{\"en\":{\"name\":1}}", ",\"localized\":{\"en\":{\"description\":null}}",
      ",\"localized\":{\"en\":{},\"EN\":{}}",
      ",\"localized\":{\"zh-Hans\":{},\"ZH-hANS\":{}}",
      ",\"localized\":{\"en\":{\"name\":\"\",\"name\":\"n\"}}",
      ",\"localized\":{\"\":{}}", ",\"localized\":{\"zh_Hans\":{}}",
      ",\"localized\":{\"en-\":{}}", ",\"localized\":{\"en--US\":{}}",
      ",\"localized\":{\"1a\":{}}", ",\"localized\":{\"en-a\":{}}",
      ",\"localized\":{\"en-a-foo-A-bar\":{}}",
      ",\"localized\":{\"en-0-foo-0-bar\":{}}",
      ",\"external_ids\":[]", ",\"external_ids\":null",
      ",\"external_ids\":{\"igdb\":123}", ",\"external_ids\":{\"igdb\":\"\"}",
      ",\"external_ids\":{\"igdb\":\"1\",\"igdb\":\"2\"}",
      ",\"external_ids\":{\"\":\"1\"}", ",\"external_ids\":{\"bad provider\":\"1\"}",
      ",\"original_language\":\"en_US\"", ",\"original_language\":true",
      ",\"region\":[]", ",\"version\":null", ",\"x\":\"\\ud800\"",
      ",\"x\":\"\\udc00\"", ",\"x\":\"\\ud800\\u0041\"",
      ",\"x\":\"\300\200\"", ",\"x\":\"\355\240\200\"",
      ",\"x\":\"\364\220\200\200\"", ",\"x\":\"\377\"",
      ",\"x\":\"\342\202\"", ",\"\\u0000\":1", ",\"x\":\"\\u0000\""
   };
   static const char *missing[] = {
      "\"platform\":\"nes\",\"original_name\":\"N\"",
      "\"id\":\"g\",\"original_name\":\"N\"",
      "\"id\":\"g\",\"platform\":\"nes\"",
      "\"id\":\"g\",\"platform\":\"\",\"original_name\":\"N\"",
      "\"id\":\"g\",\"platform\":\"nes\",\"original_name\":\"\"",
      "\"id\":\"g\",\"platform\":\"nes\",\"original_name\":false",
      "\"id\":\"g g\",\"platform\":\"nes\",\"original_name\":\"N\"",
      "\"id\":\"\\u4e2d\",\"platform\":\"nes\",\"original_name\":\"N\"",
      "\"id\":\"g\",\"platform\":\"\\u4e2d\",\"original_name\":\"N\""
   };
   struct buffer b = {0};
   size_t i;
   for (i = 0; i < sizeof(bad_root) / sizeof(bad_root[0]); i++)
      expect(bad_root[i], false);
   for (i = 0; i < sizeof(bad_extra) / sizeof(bad_extra[0]); i++)
      gg_catalog_free(game_extra(bad_extra[i], false));
   for (i = 0; i < sizeof(missing) / sizeof(missing[0]); i++)
   {
      b.size = 0;
      append(&b, game_prefix);
      append(&b, missing[i]);
      append(&b, "}]}");
      expect(b.data, false);
   }
   free(b.data);
}

static void test_numbers(void)
{
   static const char *invalid[] = {
      "-1", "-0", "0.0", "1.5", "1e0", "1E+2", "18446744073709551616",
      "999999999999999999999999999999999999", "\"1\"", "null", "true", "01", "+1"
   };
   struct buffer b = {0};
   gg_catalog_t *catalog;
   size_t i;
   catalog = game_extra(",\"download\":{\"url\":\"https://a\","
         "\"size_bytes\":18446744073709551615}", true);
   CHECK(catalog->games[0].download->has_size);
   CHECK(catalog->games[0].download->size_bytes == (uint64_t)-1);
   gg_catalog_free(catalog);
   catalog = game_extra(",\"download\":{\"url\":\"https://a\","
         "\"size_bytes\":9007199254740993}", true);
   CHECK(catalog->games[0].download->size_bytes == (((uint64_t)1 << 53) + 1));
   gg_catalog_free(catalog);
   for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
   {
      b.size = 0;
      append(&b, ",\"download\":{\"url\":\"https://a\",\"size_bytes\":");
      append(&b, invalid[i]);
      append(&b, "}");
      gg_catalog_free(game_extra(b.data, false));
   }
   free(b.data);
}

static void test_duplicate_fields(void)
{
   static const struct
   {
      const char *prefix;
      const char *field;
      const char *suffix;
   } cases[] = {
      {"", "\"schema_version\":1", ""},
      {"", "\"source\":{\"id\":\"s\",\"name\":\"S\"}", ""},
      {"", "\"games\":[]", ""},
      {"\"source\":{", "\"id\":\"s\"", "}"},
      {"\"source\":{", "\"name\":\"S\"", "}"},
      {"\"source\":{", "\"description\":\"\"", "}"},
      {"\"source\":{", "\"homepage\":\"https://a\"", "}"},
      {"\"games\":[{", "\"original_name\":\"N\"", "}]"},
      {"\"games\":[{", "\"original_language\":\"en\"", "}]"},
      {"\"games\":[{", "\"region\":\"\"", "}]"},
      {"\"games\":[{", "\"version\":\"v\"", "}]"},
      {"\"games\":[{", "\"cover_url\":\"https://a\"", "}]"},
      {"\"games\":[{", "\"localized\":{}", "}]"},
      {"\"games\":[{", "\"external_ids\":{}", "}]"},
      {"\"games\":[{", "\"download\":{\"url\":\"https://a\"}", "}]"},
      {"\"games\":[{\"localized\":{\"en\":{", "\"description\":\"\"", "}}}]"},
      {"\"games\":[{\"download\":{", "\"sha256\":\""
         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"", "}}]"}
   };
   struct buffer b = {0};
   gg_catalog_parser_t *p;
   gg_catalog_parse_status_t status;
   size_t i;
   const char *error;
   for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
   {
      b.size = 0;
      append(&b, "{");
      append(&b, cases[i].prefix);
      append(&b, cases[i].field);
      append(&b, ",");
      append(&b, cases[i].field);
      append(&b, cases[i].suffix);
      append(&b, "}");
      p = gg_catalog_parser_create(b.data, b.size);
      CHECK(p != NULL);
      do
      {
         status = gg_catalog_parser_step(p, 1);
      } while (status == GG_CATALOG_PARSE_WORKING);
      CHECK(status == GG_CATALOG_PARSE_ERROR);
      CHECK(!gg_catalog_parser_take_result(p));
      error = gg_catalog_parser_error(p);
      gg_catalog_parser_destroy(p);
      CHECK(strcmp(error, "Duplicate field") == 0);
      checks++;
   }
   free(b.data);
}

static void string_boundary(const char *prefix, const char *suffix,
      size_t limit, const char *seed)
{
   struct buffer b = {0};
   size_t extra, n = strlen(seed);
   for (extra = 0; extra < 2; extra++)
   {
      b.size = 0;
      append(&b, prefix);
      append(&b, seed);
      repeat(&b, 'a', limit - n + extra);
      append(&b, suffix);
      expect(b.data, extra == 0);
   }
   free(b.data);
}

static void test_limits(void)
{
   struct buffer prefix = {0}, b = {0};
   size_t i, count;
   static const char *game_strings[] = {"id", "platform", "original_name", "region", "version"};
   static const size_t game_limits[] = {128, 128, 256, 128, 128};
   for (i = 0; i < 5; i++)
   {
      prefix.size = 0;
      append(&prefix, game_prefix);
      if (i != 0) append(&prefix, "\"id\":\"g\",");
      if (i != 1) append(&prefix, "\"platform\":\"nes\",");
      if (i != 2) append(&prefix, "\"original_name\":\"N\",");
      append(&prefix, "\"");
      append(&prefix, game_strings[i]);
      append(&prefix, "\":\"");
      string_boundary(prefix.data, "\"}]}", game_limits[i], "");
   }
   string_boundary("{\"schema_version\":1,\"source\":{\"name\":\"N\",\"id\":\"",
         "\"},\"games\":[]}", 128, "");
   string_boundary("{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"",
         "\"},\"games\":[]}", 256, "");
   string_boundary("{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\",\"description\":\"",
         "\"},\"games\":[]}", 8192, "");
   string_boundary("{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\",\"homepage\":\"",
         "\"},\"games\":[]}", 2048, "https://a/");
   prefix.size = 0;
   append(&prefix, game_prefix);
   append(&prefix, game_fields);
   append(&prefix, ",\"localized\":{\"en\":{\"name\":\"");
   string_boundary(prefix.data, "\"}}}]}", 256, "");
   prefix.size = 0;
   append(&prefix, game_prefix);
   append(&prefix, game_fields);
   append(&prefix, ",\"localized\":{\"en\":{\"description\":\"");
   string_boundary(prefix.data, "\"}}}]}", 8192, "");
   prefix.size = 0;
   append(&prefix, game_prefix);
   append(&prefix, game_fields);
   append(&prefix, ",\"download\":{\"url\":\"");
   string_boundary(prefix.data, "\"}}]}", 2048, "https://a/");
   prefix.size = 0;
   append(&prefix, game_prefix);
   append(&prefix, game_fields);
   append(&prefix, ",\"cover_url\":\"");
   string_boundary(prefix.data, "\"}]}", 2048, "https://a/");
   prefix.size = 0;
   append(&prefix, game_prefix);
   append(&prefix, game_fields);
   append(&prefix, ",\"external_ids\":{\"");
   string_boundary(prefix.data, "\":\"v\"}}]}", 128, "");
   append(&prefix, "p\":\"");
   string_boundary(prefix.data, "\"}}]}", 256, "");
   free(prefix.data);
   for (count = 16; count <= 17; count++)
   {
      b.size = 0;
      append(&b, ",\"localized\":{");
      for (i = 0; i < count; i++)
      {
         append(&b, i ? ",\"en-x-" : "\"en-x-");
         append_number(&b, i);
         append(&b, "\":{}");
      }
      append(&b, "}");
      gg_catalog_free(game_extra(b.data, count == 16));
   }
   for (count = 32; count <= 33; count++)
   {
      b.size = 0;
      append(&b, ",\"external_ids\":{");
      for (i = 0; i < count; i++)
      {
         append(&b, i ? ",\"p" : "\"p");
         append_number(&b, i);
         append(&b, "\":\"v\"");
      }
      append(&b, "}");
      gg_catalog_free(game_extra(b.data, count == 32));
   }
   b.size = 0;
   append(&b, empty_source);
   repeat(&b, ' ', GG_CATALOG_MAX_BYTES - b.size);
   gg_catalog_free(parse_bytes(b.data, b.size, true));
   append(&b, " ");
   CHECK(gg_catalog_parser_create(b.data, b.size) == NULL);
   b.size = 0;
   append_bytes(&b, empty_source, strlen(empty_source) - 1);
   append(&b, ",\"long_unknown\":\"");
   repeat(&b, 'a', GG_CATALOG_MAX_BYTES - b.size - 2);
   append(&b, "\"}");
   CHECK(b.size == GG_CATALOG_MAX_BYTES);
   gg_catalog_free(parse_bytes(b.data, b.size, true));
   free(b.data);
}

static void test_depth_and_encoding(void)
{
   struct buffer b = {0};
   size_t depth, i;
   gg_catalog_t *catalog;
   for (depth = 16; depth <= 17; depth++)
   {
      b.size = 0;
      append_bytes(&b, empty_source, strlen(empty_source) - 1);
      append(&b, ",\"unknown\":");
      for (i = 1; i < depth; i++)
         append(&b, i % 2 ? "[" : "{\"x\":");
      append(&b, "null");
      for (i = depth - 1; i > 0; i--)
         append(&b, i % 2 ? "]" : "}");
      append(&b, "}");
      expect(b.data, depth == 16);
   }
   b.size = 0;
   append(&b, empty_source);
   append_bytes(&b, "\0", 1);
   gg_catalog_free(parse_bytes(b.data, b.size, false));
   b.size = 0;
   append(&b, game_prefix);
   append(&b, "\"id\":\"g\",\"platform\":\"future\",\"original_name\":\"\\ud83d\\ude00\"}]}");
   catalog = parse_bytes(b.data, b.size, true);
   CHECK(strcmp(catalog->games[0].original_name, "\360\237\230\200") == 0);
   CHECK(strcmp(catalog->games[0].platform, "future") == 0);
   gg_catalog_free(catalog);
   b.size = 0;
   append(&b, game_prefix);
   append(&b, "\"id\":\"g\",\"platform\":\"nes\",\"original_name\":\"");
   for (i = 0; i < 128; i++) append(&b, "\303\251");
   append(&b, "\"}]}");
   expect(b.data, true);
   b.size -= 4;
   append(&b, "a\"}]}");
   expect(b.data, false);
   free(b.data);
}

static void test_languages(void)
{
   static const char *valid[] = {
      "en", "ja", "zh-Hans", "zh-Hant", "en-US", "es-419", "zh-cmn-Hans-CN",
      "de-CH-1901", "sl-rozaj-biske", "en-US-u-ca-gregory", "x-private", "i-klingon"
   };
   struct buffer b = {0};
   size_t i, length;
   for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
   {
      b.size = 0;
      append(&b, ",\"localized\":{\"");
      append(&b, valid[i]);
      append(&b, "\":{}}");
      gg_catalog_free(game_extra(b.data, true));
   }
   for (length = 63; length <= 64; length++)
   {
      b.size = 0;
      append(&b, ",\"localized\":{\"x");
      for (i = 0; i < 6; i++) append(&b, "-abcdefgh");
      append(&b, "-");
      repeat(&b, 'a', length - 56);
      append(&b, "\":{}}");
      gg_catalog_free(game_extra(b.data, length == 63));
      b.size = 0;
      append(&b, ",\"original_language\":\"x");
      for (i = 0; i < 6; i++) append(&b, "-abcdefgh");
      append(&b, "-");
      repeat(&b, 'a', length - 56);
      append(&b, "\"");
      gg_catalog_free(game_extra(b.data, length == 63));
   }
   free(b.data);
}

static void make_games(struct buffer *b, size_t count, bool duplicate)
{
   size_t i;
   b->size = 0;
   append(b, "{\"schema_version\":1,\"source\":{\"id\":\"s\",\"name\":\"S\"},\"games\":[");
   for (i = 0; i < count; i++)
   {
      if (i) append(b, ",");
      append(b, "{\"id\":\"g");
      append_number(b, duplicate && i + 1 == count ? count : count - i);
      append(b, "\",");
      append(b, "\"platform\":\"nes\",\"original_name\":\"Same name\"}");
   }
   append(b, "]}");
}

static void test_games_and_budget(void)
{
   struct buffer b = {0};
   gg_catalog_parser_t *p;
   gg_catalog_t *catalog;
   gg_catalog_parse_status_t status;
   size_t count, calls = 0;
   for (count = 2; count < 20; count++)
   {
      make_games(&b, count, false);
      catalog = parse_bytes(b.data, b.size, true);
      CHECK(catalog->game_count == count);
      gg_catalog_free(catalog);
      make_games(&b, count, true);
      expect(b.data, false);
   }
   make_games(&b, 5000, false);
   p = gg_catalog_parser_create(b.data, b.size);
   CHECK(p != NULL);
   memset(b.data, '!', b.size);
   status = GG_CATALOG_PARSE_WORKING;
   CHECK(gg_catalog_parser_step(p, 0) == status);
   while (status == GG_CATALOG_PARSE_WORKING)
   {
      CHECK(gg_catalog_parser_take_result(p) == NULL);
      status = gg_catalog_parser_step(p, 1);
      calls++;
      CHECK(calls < 200000);
   }
   CHECK(status == GG_CATALOG_PARSE_DONE);
   CHECK(calls > 5000 * 8);
   catalog = gg_catalog_parser_take_result(p);
   CHECK(catalog != NULL && catalog->game_count == 5000);
   CHECK(strcmp(catalog->games[0].id, "g5000") == 0);
   CHECK(strcmp(catalog->games[4999].id, "g1") == 0);
   gg_catalog_parser_destroy(p);
   gg_catalog_free(catalog);
   make_games(&b, 5000, true);
   expect(b.data, false);
   make_games(&b, 5001, false);
   expect(b.data, false);
   free(b.data);
   printf("5000 games: %lu budget-1 steps\n", (unsigned long)calls);
}

static void test_lifetime(void)
{
   struct buffer b = {0};
   size_t stop, i, events;
   gg_catalog_parser_t *p;
   gg_catalog_parse_status_t status;
   CHECK(gg_catalog_parser_create(NULL, 1) == NULL);
   CHECK(gg_catalog_parser_create("", 0) == NULL);
   CHECK(gg_catalog_parser_create("x", GG_CATALOG_MAX_BYTES + 1) == NULL);
   CHECK(gg_catalog_parser_step(NULL, 1) == GG_CATALOG_PARSE_ERROR);
   CHECK(gg_catalog_parser_take_result(NULL) == NULL);
   gg_catalog_parser_destroy(NULL);
   append(&b, game_prefix);
   append(&b, game_fields);
   append(&b, ",\"localized\":{\"en\":{\"name\":\"E\",\"description\":\"D\"}},");
   append(&b, "\"download\":{\"url\":\"https://a\",\"size_bytes\":123},");
   append(&b, "\"external_ids\":{\"p\":\"v\"},\"cover_url\":\"https://a\"}]}");
   p = gg_catalog_parser_create(b.data, b.size);
   CHECK(p != NULL);
   events = 0;
   do
   {
      status = gg_catalog_parser_step(p, 1);
      events++;
      CHECK(events < 200);
   } while (status == GG_CATALOG_PARSE_WORKING);
   CHECK(status == GG_CATALOG_PARSE_DONE);
   gg_catalog_parser_destroy(p);
   for (stop = 0; stop <= events; stop++)
   {
      p = gg_catalog_parser_create(b.data, b.size);
      CHECK(p != NULL);
      for (i = 0; i < stop; i++) gg_catalog_parser_step(p, 1);
      gg_catalog_parser_destroy(p);
   }
   for (i = 1; i < b.size; i++)
      gg_catalog_free(parse_bytes(b.data, i, false));
   /* Cancellation during index construction and merge passes. */
   make_games(&b, 10, false);
   p = gg_catalog_parser_create(b.data, b.size);
   CHECK(p != NULL);
   events = 0;
   while (gg_catalog_parser_step(p, 1) == GG_CATALOG_PARSE_WORKING) events++;
   gg_catalog_parser_destroy(p);
   for (stop = 0; stop <= events; stop++)
   {
      p = gg_catalog_parser_create(b.data, b.size);
      CHECK(p != NULL);
      gg_catalog_parser_step(p, stop);
      gg_catalog_parser_destroy(p);
   }
   free(b.data);
   /* Root close is not publication: RJSON_DONE still consumes one event. */
   p = gg_catalog_parser_create(empty_source, strlen(empty_source));
   CHECK(p != NULL);
   CHECK(gg_catalog_parser_step(p, 14) == GG_CATALOG_PARSE_WORKING);
   CHECK(!gg_catalog_parser_take_result(p));
   CHECK(gg_catalog_parser_step(p, 1) == GG_CATALOG_PARSE_DONE);
   gg_catalog_parser_destroy(p);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
   _CrtMemState before, after, difference;
   _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
   _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
   _CrtMemCheckpoint(&before);
#endif
   test_valid();
   test_invalid();
   test_numbers();
   test_duplicate_fields();
   test_limits();
   test_depth_and_encoding();
   test_languages();
   test_games_and_budget();
   test_lifetime();
#if defined(_MSC_VER) && defined(_DEBUG)
   _CrtMemCheckpoint(&after);
   if (_CrtMemDifference(&difference, &before, &after))
   {
      _CrtMemDumpStatistics(&difference);
      CHECK(difference.lCounts[_NORMAL_BLOCK] == 0);
      CHECK(difference.lSizes[_NORMAL_BLOCK] == 0);
   }
#endif
   printf("catalog parser tests passed (%u cases)\n", checks);
   return 0;
}
