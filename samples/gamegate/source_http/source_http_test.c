#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#else
#include <time.h>
#endif

#define GG_SOURCE_HTTP_INTERNAL
#include "gamegate/source_http.h"

static int failures;
static int passes;
static int skips;

static void check(int condition, const char *name)
{
   if (!condition)
   {
      fprintf(stderr, "FAIL: %s\n", name);
      failures++;
   }
   else
      passes++;
}

static void skip(const char *name)
{
   fprintf(stderr, "SKIP: %s not set\n", name);
   skips++;
}

static int64_t monotonic_ms(void)
{
#ifdef _WIN32
   return (int64_t)GetTickCount();
#else
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

static void sleep_ms(unsigned milliseconds)
{
#ifdef _WIN32
   Sleep(milliseconds);
#else
   struct timespec delay;
   delay.tv_sec = milliseconds / 1000;
   delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
   nanosleep(&delay, NULL);
#endif
}

static int contains_bytes(const void *data, size_t length,
      const char *needle)
{
   const char *bytes = (const char *)data;
   size_t needle_length = strlen(needle);
   size_t i;
   if (needle_length > length)
      return 0;
   for (i = 0; i <= length - needle_length; i++)
      if (!memcmp(bytes + i, needle, needle_length))
         return 1;
   return 0;
}

static void test_url_validation(void)
{
   static const char userinfo[] = "https://user@example.org/";
   static const char fragment[] = "https://example.org/#fragment";
   static const char space[] = "https://example.org/a b";
   check(!gg_source_http_create(NULL, 0), "NULL URL rejected");
   check(!gg_source_http_create("http://example.org/", 19),
         "HTTP URL rejected");
   check(!gg_source_http_create(userinfo, strlen(userinfo)),
         "userinfo URL rejected");
   check(!gg_source_http_create(fragment, strlen(fragment)),
         "fragment URL rejected");
   check(!gg_source_http_create(space, strlen(space)),
         "space URL rejected");
}

static void test_invalid_lifecycle(void)
{
   gg_source_http_t *request;
   request = gg_source_http_create("https://example.org/catalog.json",
         strlen("https://example.org/catalog.json"));
   check(request != NULL, "lifecycle URL creates request");
   if (!request)
      return;
   check(gg_source_http_poll(request, 0) == GG_SOURCE_HTTP_ERROR,
         "poll before start fails");
   check(gg_source_http_start(request, 0) == GG_SOURCE_HTTP_ERROR,
         "start after failed poll remains failed");
   check(gg_source_http_error(request) == GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT,
         "failed poll reports invalid argument");
   gg_source_http_free(request);
}

static void test_local_deadline(void)
{
   static const char url[] = "https://127.0.0.1:1/";
   gg_source_http_t *request = gg_source_http_create(url, sizeof(url) - 1);
   gg_source_http_status_t status;
   check(request != NULL, "deadline URL creates request");
   if (!request)
      return;
   status = gg_source_http_start(request, 1000);
   if (status == GG_SOURCE_HTTP_WORKING)
      status = gg_source_http_poll(request,
            1000 + GG_SOURCE_HTTP_DEADLINE_MS);
   check(status == GG_SOURCE_HTTP_ERROR,
         "deadline expires without waiting for network");
   check(gg_source_http_error(request) == GG_SOURCE_HTTP_ERROR_TIMEOUT,
         "deadline reports timeout");
   gg_source_http_free(request);
}

static void test_local_cancel(void)
{
   static const char url[] = "https://127.0.0.1:1/";
   gg_source_http_t *request = gg_source_http_create(url, sizeof(url) - 1);
   gg_source_http_status_t status;
   size_t length = 1;
   check(request != NULL, "cancel URL creates request");
   if (!request)
      return;
   status = gg_source_http_start(request, 1000);
   check(status == GG_SOURCE_HTTP_WORKING,
         "cancel URL starts asynchronously");
   gg_source_http_cancel(request);
   status = gg_source_http_poll(request, 1000);
   check(status == GG_SOURCE_HTTP_CANCELLED,
         "cancel returns without waiting for network");
   check(gg_source_http_error(request) == GG_SOURCE_HTTP_ERROR_CANCELLED,
         "cancel reports cancellation");
   check(gg_source_http_data(request, &length) == NULL && length == 0,
         "cancel exposes no data");
   gg_source_http_free(request);
}

static void test_redirect_resolution(void)
{
   static const char base[] =
      "https://example.org/a/catalog.json?token=abc";
   char output[GG_CATALOG_MAX_URL_BYTES + 1];
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "/x/y?z=1", strlen("/x/y?z=1"), output, sizeof(output)) &&
         !strcmp(output, "https://example.org/x/y?z=1"),
         "absolute-path redirect");
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "../next.json?q=%2F", strlen("../next.json?q=%2F"), output,
         sizeof(output)) &&
         !strcmp(output, "https://example.org/next.json?q=%2F"),
         "relative redirect preserves query encoding");
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "?refresh=1", strlen("?refresh=1"), output, sizeof(output)) &&
         !strcmp(output,
               "https://example.org/a/catalog.json?refresh=1"),
         "query-only redirect");
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "//cdn.example.org/path?q=1",
         strlen("//cdn.example.org/path?q=1"), output, sizeof(output)) &&
         !strcmp(output, "https://cdn.example.org/path?q=1"),
         "network-path redirect");
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "/a/../../b", strlen("/a/../../b"), output, sizeof(output)) &&
         !strcmp(output, "https://example.org/b"),
         "dot-segment redirect");
   check(gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "/a/%2e%2e/b", strlen("/a/%2e%2e/b"), output,
         sizeof(output)) &&
         !strcmp(output, "https://example.org/a/%2e%2e/b"),
         "encoded dot is not normalized");
   check(!gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "http://example.org/down", strlen("http://example.org/down"),
         output, sizeof(output)),
         "HTTP downgrade rejected");
   check(!gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "https://user@example.org/",
         strlen("https://user@example.org/"), output, sizeof(output)),
         "redirect userinfo rejected");
   check(!gg_source_http_redirect_resolve(base, sizeof(base) - 1,
         "/next#fragment", strlen("/next#fragment"), output,
         sizeof(output)),
         "redirect fragment rejected");
}

static void test_redirect_vectors(void)
{
   const char *filename = getenv("GG_TEST_REDIRECT_VECTORS");
   FILE *file = filename ? fopen(filename, "r") : NULL;
   char line[1024];
   char output[1024];
   check(file != NULL, "shared redirect vectors available");
   if (!file)
      return;
   while (fgets(line, sizeof(line), file))
   {
      char *location = strchr(line, '\t');
      char *expected = location ? strchr(location + 1, '\t') : NULL;
      check(expected != NULL, "shared vector format");
      if (!expected)
         break;
      *location++ = '\0';
      *expected++ = '\0';
      expected[strcspn(expected, "\r\n")] = '\0';
      check(gg_source_http_redirect_resolve(line, strlen(line), location,
            strlen(location), output, sizeof(output)) &&
            !strcmp(output, expected), "shared redirect vector");
   }
   check(!ferror(file), "shared vectors read");
   fclose(file);
}

static void test_redirect_bounds(void)
{
   static const char base[] = "https://example.org";
   size_t limit = GG_CATALOG_MAX_URL_BYTES;
   size_t prefix = sizeof(base);
   char *location = (char *)malloc(limit + 2);
   char *long_base = (char *)malloc(limit + 2);
   char *output = (char *)malloc(limit + 3);
   check(location && long_base && output, "boundary buffers allocated");
   if (!location || !long_base || !output)
      goto done;
   memset(location, 'a', limit + 1);
   location[limit - prefix] = '\0';
   check(gg_source_http_redirect_resolve(base, strlen(base), location,
         strlen(location), output, limit + 1) && strlen(output) == limit,
         "no-path base resolves exact URL limit");
   output[limit] = 'X';
   check(!gg_source_http_redirect_resolve(base, strlen(base), location,
         strlen(location), output, limit) && output[limit] == 'X',
         "output capacity includes NUL");
   location[limit - prefix] = 'a';
   location[limit - prefix + 1] = '\0';
   check(!gg_source_http_redirect_resolve(base, strlen(base), location,
         strlen(location), output, limit + 3), "final URL over limit");
   memset(location, 'a', limit + 1);
   location[limit] = '\0';
   check(!gg_source_http_redirect_resolve(base, strlen(base), location,
         limit, output, limit + 3), "maximum location with missing slash");
   location[limit] = 'a';
   location[limit + 1] = '\0';
   check(!gg_source_http_redirect_resolve(base, strlen(base), location,
         limit + 1, output, limit + 3), "oversized location rejected");
   memcpy(long_base, base, strlen(base));
   long_base[strlen(base)] = '/';
   memset(long_base + prefix, 'a', limit - prefix);
   long_base[limit - 2] = '/';
   long_base[limit - 1] = 'b';
   long_base[limit] = '\0';
   check(gg_source_http_redirect_resolve(long_base, limit, "../ok", 5,
         output, limit + 1) && !strcmp(output, "https://example.org/ok"),
         "oversized intermediate path normalizes within limit");
   long_base[limit] = 'a';
   long_base[limit + 1] = '\0';
   check(!gg_source_http_redirect_resolve(long_base, limit + 1, ".", 1,
         output, limit + 1), "oversized base rejected");
done:
   free(output);
   free(long_base);
   free(location);
}

static gg_source_http_status_t poll_until_done(gg_source_http_t *request,
      int64_t deadline_ms)
{
   gg_source_http_status_t status;
   for (;;)
   {
      int64_t now = monotonic_ms();
      if (now >= deadline_ms)
         return GG_SOURCE_HTTP_ERROR;
      status = gg_source_http_poll(request, now);
      if (status != GG_SOURCE_HTTP_WORKING)
         return status;
      sleep_ms(10);
   }
}

static void expect_network_result(const char *variable,
      gg_source_http_status_t expected_status,
      gg_source_http_error_t expected_error, size_t expected_length,
      const char *expected_text)
{
   const char *url = getenv(variable);
   gg_source_http_t *request;
   gg_source_http_status_t status;
   gg_source_http_error_t error;
   const void *data;
   void *owned;
   size_t length = 0;
   char label[96];
   if (!url)
   {
      skip(variable);
      return;
   }
   request = gg_source_http_create(url, strlen(url));
   check(request != NULL, "network URL creates request");
   if (!request)
      return;
   status = gg_source_http_start(request, monotonic_ms());
   if (status == GG_SOURCE_HTTP_WORKING)
      status = poll_until_done(request, monotonic_ms() + 35000);
   error = gg_source_http_error(request);
   data = gg_source_http_data(request, &length);
   snprintf(label, sizeof(label), "%s status=%d error=%d length=%lu",
         variable, (int)status, (int)error, (unsigned long)length);
   check(status == expected_status, label);
   check(error == expected_error, label);
   if (status == GG_SOURCE_HTTP_DONE)
   {
      check(data != NULL || length == 0, "DONE data contract");
      if (expected_length != (size_t)-1)
         check(length == expected_length, "DONE response length");
      if (expected_text)
         check(data && contains_bytes(data, length, expected_text),
               "DONE response content");
      owned = gg_source_http_take_data(request, &length);
      check(owned == data, "take transfers the borrowed body without copying");
      check(gg_source_http_take_data(request, &length) == NULL && length == 0,
            "second take is empty");
      check(gg_source_http_data(request, &length) == NULL && length == 0,
            "borrow is empty after take");
      gg_source_http_free(request);
      free(owned);
      return;
   }
   else
   {
      check(data == NULL && length == 0, "error exposes no data");
   }
   gg_source_http_free(request);
}

static void test_network(void)
{
   const char *delay_url = getenv("GG_TEST_DELAY_URL");
   const char *url = getenv("GG_TEST_URL");
   gg_source_http_t *request;
   gg_source_http_status_t status;
   const void *data;
   size_t length = 0;
   expect_network_result("GG_TEST_REFUSED_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_NETWORK, 0, NULL);
   if (url)
   {
      request = gg_source_http_create(url, strlen(url));
      check(request != NULL, "fixture URL creates request");
      if (request)
      {
         status = gg_source_http_start(request, monotonic_ms());
         if (status == GG_SOURCE_HTTP_WORKING)
            status = poll_until_done(request, monotonic_ms() + 35000);
         check(status == GG_SOURCE_HTTP_DONE, "fixture HTTPS succeeds");
         data = gg_source_http_data(request, &length);
         check(data != NULL && length > 0, "fixture has body");
         check(data && contains_bytes(data, length,
               "\"schema_version\":1"),
               "fixture body is catalog JSON");
         check(gg_source_http_error(request) == GG_SOURCE_HTTP_ERROR_NONE,
               "successful request has no error");
         gg_source_http_free(request);
      }
   }
   else
      skip("GG_TEST_URL");
   if (delay_url)
   {
      request = gg_source_http_create(delay_url, strlen(delay_url));
      check(request != NULL, "delay URL creates request");
      if (request)
      {
         status = gg_source_http_start(request, monotonic_ms());
         gg_source_http_cancel(request);
         status = gg_source_http_poll(request, monotonic_ms());
         check(status == GG_SOURCE_HTTP_CANCELLED,
               "cancel does not wait for network");
         check(gg_source_http_data(request, &length) == NULL && length == 0,
               "cancel exposes no data");
         gg_source_http_free(request);
      }
   }
   else
      skip("GG_TEST_DELAY_URL");
   expect_network_result("GG_TEST_MAX_URL", GG_SOURCE_HTTP_DONE,
         GG_SOURCE_HTTP_ERROR_NONE, GG_SOURCE_HTTP_MAX_BYTES, NULL);
   expect_network_result("GG_TEST_LARGE_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_TOO_LARGE, 0, NULL);
   expect_network_result("GG_TEST_STREAM_LARGE_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_TOO_LARGE, 0, NULL);
   expect_network_result("GG_TEST_GZIP_URL", GG_SOURCE_HTTP_DONE,
         GG_SOURCE_HTTP_ERROR_NONE, (size_t)-1, "\"gzipped\": true");
   expect_network_result("GG_TEST_DEFLATE_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_DECOMPRESSION, 0, NULL);
   expect_network_result("GG_TEST_REDIRECT_OK_URL", GG_SOURCE_HTTP_DONE,
         GG_SOURCE_HTTP_ERROR_NONE, (size_t)-1, NULL);
   expect_network_result("GG_TEST_REDIRECT_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_REDIRECT, 0, NULL);
   expect_network_result("GG_TEST_TLS_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_TLS, 0, NULL);
   expect_network_result("GG_TEST_TIMEOUT_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_TIMEOUT, 0, NULL);
   expect_network_result("GG_TEST_304_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_HTTP_STATUS, 0, NULL);
   expect_network_result("GG_TEST_305_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_HTTP_STATUS, 0, NULL);
   expect_network_result("GG_TEST_306_URL", GG_SOURCE_HTTP_ERROR,
         GG_SOURCE_HTTP_ERROR_HTTP_STATUS, 0, NULL);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
   _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) |
         _CRTDBG_LEAK_CHECK_DF);
#endif
#ifdef GG_TEST_UNSUPPORTED
   gg_source_http_t *request;
   gg_source_http_status_t status;
   request = gg_source_http_create("https://example.org/catalog.json",
         strlen("https://example.org/catalog.json"));
   check(request != NULL, "unsupported build still creates request");
   status = gg_source_http_start(request, monotonic_ms());
   check(status == GG_SOURCE_HTTP_UNSUPPORTED,
         "threadless build reports unsupported");
   check(gg_source_http_error(request) == GG_SOURCE_HTTP_ERROR_UNSUPPORTED,
         "threadless build error is unsupported");
   gg_source_http_free(request);
#else
   test_url_validation();
   test_invalid_lifecycle();
   test_local_deadline();
   test_local_cancel();
   test_redirect_resolution();
   test_redirect_vectors();
   test_redirect_bounds();
   test_network();
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
   if (_CrtDumpMemoryLeaks())
      failures++;
#endif
   printf("source HTTP assertions: %d passed, %d failed; %d network cases skipped\n",
         passes, failures, skips);
   if (failures)
   {
      fprintf(stderr, "%d source HTTP test failure(s)\n", failures);
      return 1;
   }
   puts("source HTTP tests passed");
   return 0;
}
