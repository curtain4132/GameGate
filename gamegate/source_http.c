#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define GG_SOURCE_HTTP_INTERNAL
#include "source_http.h"

#if !defined(GG_SOURCE_HTTP_WIN32) && !defined(GG_SOURCE_HTTP_ANDROID) && \
      !defined(GG_SOURCE_HTTP_TEST_BACKEND)
gg_source_http_backend_t *gg_source_http_backend_create(
      const char *url, size_t length, int64_t deadline_ms,
      gg_source_http_error_t *error)
{
   (void)url;
   (void)length;
   (void)deadline_ms;
   *error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
   return NULL;
}

gg_source_http_status_t gg_source_http_backend_poll(
      gg_source_http_backend_t *backend, int64_t now_ms)
{
   (void)backend;
   (void)now_ms;
   return GG_SOURCE_HTTP_UNSUPPORTED;
}

void gg_source_http_backend_cancel(gg_source_http_backend_t *backend)
{
   (void)backend;
}

void gg_source_http_backend_destroy(gg_source_http_backend_t *backend)
{
   (void)backend;
}

gg_source_http_status_t gg_source_http_backend_status(
      const gg_source_http_backend_t *backend)
{
   (void)backend;
   return GG_SOURCE_HTTP_UNSUPPORTED;
}

gg_source_http_error_t gg_source_http_backend_error(
      const gg_source_http_backend_t *backend)
{
   (void)backend;
   return GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
}

const void *gg_source_http_backend_data(
      const gg_source_http_backend_t *backend, size_t *length)
{
   (void)backend;
   *length = 0;
   return NULL;
}

void *gg_source_http_backend_take_data(
      gg_source_http_backend_t *backend, size_t *length)
{
   (void)backend;
   *length = 0;
   return NULL;
}
#endif

static bool gg_http_prefix(const char *text, size_t length,
      const char *prefix)
{
   size_t i;
   size_t prefix_length = strlen(prefix);
   if (length < prefix_length)
      return false;
   for (i = 0; i < prefix_length; i++)
   {
      unsigned char a = (unsigned char)text[i];
      unsigned char b = (unsigned char)prefix[i];
      if (a >= 'A' && a <= 'Z')
         a = (unsigned char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z')
         b = (unsigned char)(b - 'A' + 'a');
      if (a != b)
         return false;
   }
   return true;
}

static bool gg_http_has_scheme(const char *text, size_t length)
{
   size_t i;
   unsigned char first;
   if (!length)
      return false;
   first = (unsigned char)text[0];
   if (!((first >= 'A' && first <= 'Z') ||
         (first >= 'a' && first <= 'z')))
      return false;
   for (i = 1; i < length; i++)
   {
      unsigned char c = (unsigned char)text[i];
      if (c == ':')
         return true;
      if (c == '/' || c == '?' || c == '#')
         return false;
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
         return false;
   }
   return false;
}

static void gg_http_pop_segment(char *path, size_t *path_length)
{
   size_t length = *path_length;
   while (length && path[length - 1] != '/')
      length--;
   if (length)
      length--;
   *path_length = length;
}

static bool gg_http_remove_dot_segments(const char *input, size_t input_length,
      char *output, size_t output_size)
{
   size_t in = 0;
   size_t out = 0;
   while (in < input_length)
   {
      size_t start;
      if (input_length - in >= 3 && input[in] == '.' &&
            input[in + 1] == '.' && input[in + 2] == '/')
      {
         in += 3;
      }
      else if (input_length - in >= 2 && input[in] == '.' &&
            input[in + 1] == '/')
      {
         in += 2;
      }
      else if (input_length - in >= 3 && input[in] == '/' &&
            input[in + 1] == '.' && input[in + 2] == '/')
      {
         in += 2;
      }
      else if (input_length - in == 2 && input[in] == '/' &&
            input[in + 1] == '.')
      {
         if (out + 1 >= output_size)
            return false;
         output[out++] = '/';
         in += 2;
      }
      else if (input_length - in >= 4 && input[in] == '/' &&
            input[in + 1] == '.' && input[in + 2] == '.' &&
            input[in + 3] == '/')
      {
         in += 3;
         gg_http_pop_segment(output, &out);
      }
      else if (input_length - in == 3 && input[in] == '/' &&
            input[in + 1] == '.' && input[in + 2] == '.')
      {
         gg_http_pop_segment(output, &out);
         if (out + 1 >= output_size)
            return false;
         output[out++] = '/';
         in += 3;
      }
      else if (input_length - in == 1 && input[in] == '.')
      {
         in++;
      }
      else if (input_length - in == 2 && input[in] == '.' &&
            input[in + 1] == '.')
      {
         in += 2;
      }
      else
      {
         start = in;
         if (input[in] == '/')
            in++;
         while (in < input_length && input[in] != '/')
            in++;
         if (in - start > output_size - out - 1)
            return false;
         memcpy(output + out, input + start, in - start);
         out += in - start;
      }
   }
   output[out] = '\0';
   return true;
}

static bool gg_http_copy(char *output, size_t output_size, size_t *out,
      const char *text, size_t length)
{
   if (length > output_size - *out - 1)
      return false;
   memcpy(output + *out, text, length);
   *out += length;
   output[*out] = '\0';
   return true;
}

bool gg_source_http_redirect_resolve(const char *base, size_t base_length,
      const char *location, size_t location_length, char *output,
      size_t output_size)
{
   char *path;
   char *merged;
   size_t authority_end;
   size_t base_path_end;
   size_t location_path_length;
   size_t merged_length = 0;
   size_t out = 0;
   size_t query_start;
   size_t combined_size = 2 * GG_CATALOG_MAX_URL_BYTES + 2;
   size_t i;
   bool result = false;
   if (!base || !location || !output || output_size < 2 ||
         !location_length ||
         base_length > GG_CATALOG_MAX_URL_BYTES ||
         location_length > GG_CATALOG_MAX_URL_BYTES ||
         !gg_catalog_https_url_valid(base, base_length))
      return false;
   if (output_size > GG_CATALOG_MAX_URL_BYTES + 1)
      output_size = GG_CATALOG_MAX_URL_BYTES + 1;
   for (i = 0; i < location_length; i++)
   {
      unsigned char c = (unsigned char)location[i];
      if (c <= 32 || c >= 127 || c == '\\' || c == '#')
         return false;
   }
   if (gg_http_prefix(location, location_length, "https://"))
      return gg_catalog_https_url_valid(location, location_length) &&
            gg_http_copy(output, output_size, &out, location,
                  location_length);
   path = (char *)malloc(combined_size);
   merged = (char *)malloc(combined_size);
   if (!path || !merged)
      goto done;
   if (location_length >= 2 && location[0] == '/' && location[1] == '/')
   {
      if (!gg_http_copy(output, output_size, &out, "https:", 6) ||
            !gg_http_copy(output, output_size, &out, location,
                  location_length))
         goto done;
      result = gg_catalog_https_url_valid(output, out);
      goto done;
   }
   if (gg_http_has_scheme(location, location_length))
      goto done;
   authority_end = 8;
   while (authority_end < base_length && base[authority_end] != '/' &&
         base[authority_end] != '?')
      authority_end++;
   base_path_end = authority_end;
   while (base_path_end < base_length && base[base_path_end] != '?')
      base_path_end++;
   query_start = location_length;
   for (i = 0; i < location_length; i++)
   {
      if (location[i] == '?')
      {
         query_start = i;
         break;
      }
   }
   location_path_length = query_start;
   if (location[0] == '?')
   {
      if (base_path_end == authority_end)
      {
         merged[0] = '/';
         merged_length = 1;
      }
      else
      {
         merged_length = base_path_end - authority_end;
         memcpy(merged, base + authority_end, merged_length);
      }
   }
   else if (location[0] == '/')
   {
      if (location_path_length > GG_CATALOG_MAX_URL_BYTES)
         goto done;
      memcpy(merged, location, location_path_length);
      merged_length = location_path_length;
   }
   else
   {
      size_t base_path_length = base_path_end - authority_end;
      size_t directory_length = base_path_length;
      while (directory_length && base[authority_end + directory_length - 1]
            != '/')
         directory_length--;
      if (base_path_length && !directory_length)
         directory_length = 1;
      if ((directory_length ? directory_length : 1) +
            location_path_length >= combined_size)
         goto done;
      if (directory_length)
         memcpy(merged, base + authority_end, directory_length);
      else
      {
         merged[0] = '/';
         directory_length = 1;
      }
      memcpy(merged + directory_length, location, location_path_length);
      merged_length = directory_length + location_path_length;
   }
   merged[merged_length] = '\0';
   if (!gg_http_remove_dot_segments(merged, merged_length, path,
            combined_size))
      goto done;
   if (!gg_http_copy(output, output_size, &out, base, authority_end) ||
         !gg_http_copy(output, output_size, &out, path, strlen(path)))
      goto done;
   if (query_start < location_length &&
         !gg_http_copy(output, output_size, &out, location + query_start,
               location_length - query_start))
      goto done;
   result = gg_catalog_https_url_valid(output, out);
done:
   free(merged);
   free(path);
   return result;
}

gg_source_http_t *gg_source_http_create(const char *url, size_t length)
{
   gg_source_http_t *request;
   if (!gg_catalog_https_url_valid(url, length))
      return NULL;
   request = (gg_source_http_t *)calloc(1, sizeof(*request));
   if (!request)
      return NULL;
   request->url = (char *)malloc(length + 1);
   if (!request->url)
   {
      free(request);
      return NULL;
   }
   memcpy(request->url, url, length);
   request->url[length] = '\0';
   request->url_length = length;
   retro_atomic_int_init(&request->started, 0);
   retro_atomic_int_init(&request->cancelled, 0);
   retro_atomic_int_init(&request->state, GG_SOURCE_HTTP_WORKING);
   retro_atomic_int_init(&request->error, GG_SOURCE_HTTP_ERROR_NONE);
   return request;
}

gg_source_http_status_t gg_source_http_start(gg_source_http_t *request,
      int64_t now_ms)
{
   gg_source_http_error_t backend_error = GG_SOURCE_HTTP_ERROR_INTERNAL;
   int expected = 0;
   gg_source_http_status_t status;
   if (!request)
      return GG_SOURCE_HTTP_ERROR;
   if (!retro_atomic_cas_int(&request->started, expected, 1))
      return (gg_source_http_status_t)retro_atomic_load_acquire_int(
            &request->state);
   status = (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &request->state);
   if (status != GG_SOURCE_HTTP_WORKING)
      return status;
   if (retro_atomic_load_acquire_int(&request->cancelled))
   {
      retro_atomic_store_release_int(&request->state,
            GG_SOURCE_HTTP_CANCELLED);
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_CANCELLED);
      return GG_SOURCE_HTTP_CANCELLED;
   }
   if (now_ms < 0 || now_ms > INT64_MAX -
         GG_SOURCE_HTTP_DEADLINE_MS)
   {
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT);
      retro_atomic_store_release_int(&request->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   request->deadline_ms = now_ms + GG_SOURCE_HTTP_DEADLINE_MS;
   request->backend = gg_source_http_backend_create(request->url,
         request->url_length, request->deadline_ms, &backend_error);
   if (!request->backend)
   {
      status = backend_error == GG_SOURCE_HTTP_ERROR_UNSUPPORTED
         ? GG_SOURCE_HTTP_UNSUPPORTED : GG_SOURCE_HTTP_ERROR;
      retro_atomic_store_release_int(&request->error, backend_error);
      retro_atomic_store_release_int(&request->state, status);
      return status;
   }
   status = gg_source_http_backend_status(request->backend);
   if (status != GG_SOURCE_HTTP_WORKING)
      retro_atomic_store_release_int(&request->error,
            gg_source_http_backend_error(request->backend));
   retro_atomic_store_release_int(&request->state, status);
   return status;
}

gg_source_http_status_t gg_source_http_poll(gg_source_http_t *request,
      int64_t now_ms)
{
   gg_source_http_status_t status;
   gg_source_http_error_t error;
   if (!request)
      return GG_SOURCE_HTTP_ERROR;
   status = (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &request->state);
   if (status == GG_SOURCE_HTTP_DONE || status == GG_SOURCE_HTTP_ERROR ||
         status == GG_SOURCE_HTTP_CANCELLED ||
         status == GG_SOURCE_HTTP_UNSUPPORTED)
      return status;
   if (!retro_atomic_load_acquire_int(&request->started))
   {
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT);
      retro_atomic_store_release_int(&request->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   if (retro_atomic_load_acquire_int(&request->cancelled))
   {
      gg_source_http_backend_destroy(request->backend);
      request->backend = NULL;
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_CANCELLED);
      retro_atomic_store_release_int(&request->state,
            GG_SOURCE_HTTP_CANCELLED);
      return GG_SOURCE_HTTP_CANCELLED;
   }
   if (now_ms < 0)
   {
      gg_source_http_backend_destroy(request->backend);
      request->backend = NULL;
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT);
      retro_atomic_store_release_int(&request->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   if (now_ms >= request->deadline_ms)
   {
      gg_source_http_backend_destroy(request->backend);
      request->backend = NULL;
      retro_atomic_store_release_int(&request->error,
            GG_SOURCE_HTTP_ERROR_TIMEOUT);
      retro_atomic_store_release_int(&request->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   status = gg_source_http_backend_poll(request->backend, now_ms);
   if (retro_atomic_load_acquire_int(&request->cancelled))
   {
      gg_source_http_backend_destroy(request->backend);
      request->backend = NULL;
      status = GG_SOURCE_HTTP_CANCELLED;
      error = GG_SOURCE_HTTP_ERROR_CANCELLED;
   }
   else
      error = gg_source_http_backend_error(request->backend);
   if (status != GG_SOURCE_HTTP_WORKING)
      retro_atomic_store_release_int(&request->error, error);
   retro_atomic_store_release_int(&request->state, status);
   return status;
}

gg_source_http_status_t gg_source_http_status(
      const gg_source_http_t *request)
{
   if (!request)
      return GG_SOURCE_HTTP_ERROR;
   return (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &request->state);
}

gg_source_http_error_t gg_source_http_error(const gg_source_http_t *request)
{
   if (!request)
      return GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT;
   return (gg_source_http_error_t)retro_atomic_load_acquire_int(
         &request->error);
}

const void *gg_source_http_data(const gg_source_http_t *request,
      size_t *length)
{
   if (length)
      *length = 0;
   if (!request || !length ||
         gg_source_http_status(request) != GG_SOURCE_HTTP_DONE)
      return NULL;
   return gg_source_http_backend_data(request->backend, length);
}

void *gg_source_http_take_data(gg_source_http_t *request, size_t *length)
{
   if (length)
      *length = 0;
   if (!request || !length ||
         gg_source_http_status(request) != GG_SOURCE_HTTP_DONE)
      return NULL;
   return gg_source_http_backend_take_data(request->backend, length);
}

void gg_source_http_cancel(gg_source_http_t *request)
{
   if (!request)
      return;
   retro_atomic_store_release_int(&request->cancelled, 1);
}

void gg_source_http_free(gg_source_http_t *request)
{
   if (!request)
      return;
   if (request->backend)
      gg_source_http_backend_destroy(request->backend);
   free(request->url);
   free(request);
}
