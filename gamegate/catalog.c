#include "catalog.h"

#include <stdlib.h>
#include <string.h>

void gg_catalog_free(gg_catalog_t *catalog)
{
   size_t i, j;
   if (!catalog)
      return;
   free(catalog->source.id);
   free(catalog->source.name);
   free(catalog->source.description);
   free(catalog->source.homepage);
   for (i = 0; catalog->games && i < catalog->game_count; i++)
   {
      gg_game_t *game = &catalog->games[i];
      free(game->id);
      free(game->platform);
      free(game->original_language);
      free(game->original_name);
      free(game->region);
      free(game->version);
      free(game->cover_url);
      for (j = 0; game->localized && j < game->localized_count; j++)
      {
         free(game->localized[j].language);
         free(game->localized[j].name);
         free(game->localized[j].description);
      }
      free(game->localized);
      for (j = 0; game->external_ids && j < game->external_id_count; j++)
      {
         free(game->external_ids[j].provider);
         free(game->external_ids[j].value);
      }
      free(game->external_ids);
      if (game->download)
      {
         free(game->download->url);
         free(game->download->sha256);
         free(game->download);
      }
   }
   free(catalog->games);
   free(catalog);
}

static unsigned char gg_ascii_lower(unsigned char c)
{
   return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

static bool gg_hex(unsigned char c)
{
   c = gg_ascii_lower(c);
   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static bool gg_alnum(unsigned char c)
{
   c = gg_ascii_lower(c);
   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static bool gg_ipv4(const char *s, size_t n)
{
   size_t i = 0, start;
   unsigned parts = 0, value;
   while (i < n)
   {
      start = i;
      value = 0;
      while (i < n && s[i] >= '0' && s[i] <= '9')
      {
         value = value * 10 + (unsigned)(s[i++] - '0');
         if (i - start > 3 || value > 255)
            return false;
      }
      if (i == start || (i - start > 1 && s[start] == '0'))
         return false;
      parts++;
      if (i == n)
         return parts == 4;
      if (s[i++] != '.' || i == n)
         return false;
   }
   return false;
}

static bool gg_ipv6(const char *s, size_t n)
{
   size_t i = 0, start, j;
   unsigned groups = 0;
   bool compressed = false;
   if (!n)
      return false;
   if (s[0] == ':')
   {
      if (n < 2 || s[1] != ':')
         return false;
      compressed = true;
      i = 2;
   }
   while (i < n)
   {
      start = i;
      while (i < n && s[i] != ':')
         i++;
      if (memchr(s + start, '.', i - start))
         return i == n && gg_ipv4(s + start, i - start)
            && (compressed ? groups + 2 < 8 : groups + 2 == 8);
      if (i == start || i - start > 4)
         return false;
      for (j = start; j < i; j++)
         if (!gg_hex((unsigned char)s[j]))
            return false;
      if (++groups > 8)
         return false;
      if (i < n)
      {
         i++;
         if (i < n && s[i] == ':')
         {
            if (compressed)
               return false;
            compressed = true;
            i++;
         }
         else if (i == n)
            return false;
      }
   }
   return compressed ? groups < 8 : groups == 8;
}

static bool gg_host(const char *s, size_t n)
{
   size_t i, start = 0;
   bool numeric = true;
   if (!n || n > 254)
      return false;
   for (i = 0; i < n; i++)
      if (s[i] != '.' && (s[i] < '0' || s[i] > '9'))
         numeric = false;
   if (numeric)
      return gg_ipv4(s, n);
   if (s[n - 1] == '.')
      n--;
   if (!n || n > 253)
      return false;
   for (i = 0; i <= n; i++)
   {
      if (i == n || s[i] == '.')
      {
         if (i == start || i - start > 63 || s[start] == '-'
               || s[i - 1] == '-')
            return false;
         start = i + 1;
      }
      else if (!gg_alnum((unsigned char)s[i]) && s[i] != '-')
         return false;
   }
   return true;
}

bool gg_catalog_https_url_valid(const char *url, size_t length)
{
   static const char scheme[] = "https://";
   size_t i, end, host_end, port_start;
   unsigned port = 0;
   if (!url || length < 9 || length > GG_CATALOG_MAX_URL_BYTES)
      return false;
   for (i = 0; i < 8; i++)
      if (gg_ascii_lower((unsigned char)url[i]) != scheme[i])
         return false;
   for (i = 8; i < length; i++)
   {
      unsigned char c = (unsigned char)url[i];
      if (c <= 32 || c >= 127 || c == '\\' || c == '#')
         return false;
   }
   end = 8;
   while (end < length && url[end] != '/' && url[end] != '?')
      end++;
   port_start = end;
   if (url[8] == '[')
   {
      host_end = 9;
      while (host_end < end && url[host_end] != ']')
         host_end++;
      if (host_end == end || !gg_ipv6(url + 9, host_end - 9))
         return false;
      port_start = host_end + 1;
   }
   else
   {
      host_end = 8;
      while (host_end < end && url[host_end] != ':')
         host_end++;
      if (!gg_host(url + 8, host_end - 8))
         return false;
      port_start = host_end;
   }
   if (port_start < end)
   {
      if (url[port_start++] != ':' || port_start == end)
         return false;
      for (i = port_start; i < end; i++)
      {
         if (url[i] < '0' || url[i] > '9')
            return false;
         port = port * 10 + (unsigned)(url[i] - '0');
         if (port > 65535)
            return false;
      }
      if (!port)
         return false;
   }
   for (i = end; i < length; i++)
   {
      unsigned char c = (unsigned char)url[i];
      if (c == '%')
      {
         if (length - i < 3 || !gg_hex((unsigned char)url[i + 1])
               || !gg_hex((unsigned char)url[i + 2]))
            return false;
         i += 2;
      }
      else if (!gg_alnum(c) && !strchr("-._~!$&'()*+,;=:@/?", c))
         return false;
   }
   return true;
}

static bool gg_language_equal(const char *a, const char *b)
{
   if (!a || !b || !*a || !*b)
      return false;
   while (*a && *b)
      if (gg_ascii_lower((unsigned char)*a++) !=
            gg_ascii_lower((unsigned char)*b++))
         return false;
   return *a == *b;
}

gg_game_text_t gg_game_select_text(const gg_game_t *game,
      const char *requested_language)
{
   gg_game_text_t text;
   const char *languages[3];
   size_t i, j;
   memset(&text, 0, sizeof(text));
   if (!game)
      return text;
   languages[0] = requested_language;
   languages[1] = "en";
   languages[2] = game->original_language;
   for (i = 0; i < 3; i++)
      for (j = 0; game->localized && j < game->localized_count; j++)
      {
         const gg_localized_t *entry = &game->localized[j];
         if (!gg_language_equal(languages[i], entry->language))
            continue;
         if (!text.name && entry->name && *entry->name)
         {
            text.name = entry->name;
            text.name_language = entry->language;
         }
         if (!text.description && entry->description && *entry->description)
         {
            text.description = entry->description;
            text.description_language = entry->language;
         }
      }
   if (!text.name && game->original_name && *game->original_name)
   {
      text.name = game->original_name;
      if (game->original_language && *game->original_language)
         text.name_language = game->original_language;
   }
   return text;
}
