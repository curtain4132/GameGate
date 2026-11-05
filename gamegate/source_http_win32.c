#define GG_SOURCE_HTTP_INTERNAL
#include "source_http.h"

#ifdef GG_SOURCE_HTTP_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>
#include <string.h>

typedef HINTERNET (WINAPI *gg_winhttp_open_t)(LPCWSTR, DWORD, LPCWSTR,
      LPCWSTR, DWORD);
typedef HINTERNET (WINAPI *gg_winhttp_connect_t)(HINTERNET, LPCWSTR,
      INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *gg_winhttp_open_request_t)(HINTERNET, LPCWSTR,
      LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL (WINAPI *gg_winhttp_set_timeouts_t)(HINTERNET, int, int, int,
      int);
typedef BOOL (WINAPI *gg_winhttp_set_option_t)(HINTERNET, DWORD, LPVOID,
      DWORD);
typedef WINHTTP_STATUS_CALLBACK (WINAPI *gg_winhttp_set_callback_t)(
      HINTERNET, WINHTTP_STATUS_CALLBACK, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *gg_winhttp_send_request_t)(HINTERNET, LPCWSTR, DWORD,
      LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *gg_winhttp_receive_response_t)(HINTERNET, LPVOID);
typedef BOOL (WINAPI *gg_winhttp_query_headers_t)(HINTERNET, DWORD,
      LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *gg_winhttp_query_data_t)(HINTERNET, LPDWORD);
typedef BOOL (WINAPI *gg_winhttp_read_data_t)(HINTERNET, LPVOID, DWORD,
      LPDWORD);
typedef BOOL (WINAPI *gg_winhttp_close_t)(HINTERNET);

typedef struct gg_winhttp_api
{
   HMODULE module;
   gg_winhttp_open_t open;
   gg_winhttp_connect_t connect;
   gg_winhttp_open_request_t open_request;
   gg_winhttp_set_timeouts_t set_timeouts;
   gg_winhttp_set_option_t set_option;
   gg_winhttp_set_callback_t set_callback;
   gg_winhttp_send_request_t send_request;
   gg_winhttp_receive_response_t receive_response;
   gg_winhttp_query_headers_t query_headers;
   gg_winhttp_query_data_t query_data;
   gg_winhttp_read_data_t read_data;
   gg_winhttp_close_t close;
   bool ready;
} gg_winhttp_api_t;

typedef enum gg_http_phase
{
   GG_HTTP_PHASE_SEND,
   GG_HTTP_PHASE_RESPONSE,
   GG_HTTP_PHASE_DATA,
   GG_HTTP_PHASE_READ
} gg_http_phase_t;

typedef struct gg_http_request_context
{
   gg_source_http_backend_t *backend;
} gg_http_request_context_t;

struct gg_source_http_backend
{
   gg_winhttp_api_t *api;
   HINTERNET session;
   HINTERNET connection;
   HINTERNET request;
   gg_http_request_context_t *request_context;
   DWORD available;
   DWORD read_count;
   DWORD async_error;
   DWORD redirect_count;
   INTERNET_PORT connect_port;
   size_t data_length;
   size_t data_capacity;
   char *data;
   char current_url[GG_CATALOG_MAX_URL_BYTES + 1];
   char next_url[GG_CATALOG_MAX_URL_BYTES + 1];
   WCHAR host[256];
   WCHAR resource[GG_CATALOG_MAX_URL_BYTES + 2];
   unsigned char read_buffer[GG_SOURCE_HTTP_READ_BUFFER_BYTES];
   gg_http_phase_t phase;
   gg_source_http_error_t error;
   retro_atomic_int_t events;
   retro_atomic_int_t references;
   retro_atomic_int_t cancelled;
   retro_atomic_int_t destroying;
   retro_atomic_int_t state;
};

static INIT_ONCE gg_winhttp_once = INIT_ONCE_STATIC_INIT;
static gg_winhttp_api_t gg_winhttp;

static BOOL CALLBACK gg_load_winhttp(PINIT_ONCE once, PVOID parameter,
      PVOID *context)
{
   WCHAR directory[MAX_PATH];
   WCHAR path[MAX_PATH];
   UINT length;
   HMODULE module;
   gg_winhttp_api_t *api = &gg_winhttp;
   (void)once;
   (void)parameter;
   (void)context;
   memset(api, 0, sizeof(*api));
   length = GetSystemDirectoryW(directory, MAX_PATH);
   if (!length || length >= MAX_PATH ||
         length + 13 >= MAX_PATH)
      return FALSE;
   memcpy(path, directory, length * sizeof(WCHAR));
   memcpy(path + length, L"\\winhttp.dll",
         sizeof(L"\\winhttp.dll"));
   module = LoadLibraryW(path);
   if (!module)
      return FALSE;
   api->module = module;
   api->open = (gg_winhttp_open_t)(void *)GetProcAddress(module,
         "WinHttpOpen");
   api->connect = (gg_winhttp_connect_t)(void *)GetProcAddress(module,
         "WinHttpConnect");
   api->open_request = (gg_winhttp_open_request_t)(void *)
      GetProcAddress(module, "WinHttpOpenRequest");
   api->set_timeouts = (gg_winhttp_set_timeouts_t)(void *)
      GetProcAddress(module, "WinHttpSetTimeouts");
   api->set_option = (gg_winhttp_set_option_t)(void *)
      GetProcAddress(module, "WinHttpSetOption");
   api->set_callback = (gg_winhttp_set_callback_t)(void *)
      GetProcAddress(module, "WinHttpSetStatusCallback");
   api->send_request = (gg_winhttp_send_request_t)(void *)
      GetProcAddress(module, "WinHttpSendRequest");
   api->receive_response = (gg_winhttp_receive_response_t)(void *)
      GetProcAddress(module, "WinHttpReceiveResponse");
   api->query_headers = (gg_winhttp_query_headers_t)(void *)
      GetProcAddress(module, "WinHttpQueryHeaders");
   api->query_data = (gg_winhttp_query_data_t)(void *)
      GetProcAddress(module, "WinHttpQueryDataAvailable");
   api->read_data = (gg_winhttp_read_data_t)(void *)
      GetProcAddress(module, "WinHttpReadData");
   api->close = (gg_winhttp_close_t)(void *)
      GetProcAddress(module, "WinHttpCloseHandle");
   if (!api->open || !api->connect || !api->open_request ||
         !api->set_timeouts || !api->set_option || !api->set_callback ||
         !api->send_request || !api->receive_response ||
         !api->query_headers || !api->query_data || !api->read_data ||
         !api->close)
   {
      FreeLibrary(module);
      memset(api, 0, sizeof(*api));
      return FALSE;
   }
   api->ready = true;
   return TRUE;
}

static gg_winhttp_api_t *gg_get_winhttp_api(void)
{
   if (!InitOnceExecuteOnce(&gg_winhttp_once, gg_load_winhttp, NULL, NULL))
      return NULL;
   return gg_winhttp.ready ? &gg_winhttp : NULL;
}

static void gg_backend_release(gg_source_http_backend_t *backend)
{
   if (retro_atomic_fetch_sub_int(&backend->references, 1) == 1)
   {
      free(backend->data);
      free(backend);
   }
}

static void CALLBACK gg_http_callback(HINTERNET handle, DWORD_PTR context,
      DWORD status, LPVOID information, DWORD length)
{
   gg_http_request_context_t *request_context =
      (gg_http_request_context_t *)context;
   gg_source_http_backend_t *backend = request_context
      ? request_context->backend : NULL;
   (void)handle;
   (void)length;
   if (!backend)
      return;
   if (status & WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
   {
      gg_backend_release(backend);
      free(request_context);
      return;
   }
   if (retro_atomic_load_acquire_int(&backend->destroying))
      return;
   if (status & WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
   {
      WINHTTP_ASYNC_RESULT *result = (WINHTTP_ASYNC_RESULT *)information;
      backend->async_error = result ? (DWORD)result->dwError
         : (DWORD)ERROR_GEN_FAILURE;
   }
   else if (status & WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)
   {
      if (information)
         backend->available = *(DWORD *)information;
   }
   else if (status & WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
      backend->read_count = length;
   retro_atomic_fetch_or_int(&backend->events, (int)status);
}

static void gg_close_request(gg_source_http_backend_t *backend)
{
   HINTERNET request = backend->request;
   if (!request)
      return;
   backend->request = NULL;
   backend->request_context = NULL;
   backend->api->close(request);
}

static void gg_close_connection(gg_source_http_backend_t *backend)
{
   HINTERNET connection = backend->connection;
   if (!connection)
      return;
   backend->connection = NULL;
   backend->api->close(connection);
}

static void gg_finish(gg_source_http_backend_t *backend,
      gg_source_http_status_t status, gg_source_http_error_t error)
{
   backend->error = error;
   gg_close_request(backend);
   gg_close_connection(backend);
   retro_atomic_store_release_int(&backend->state, status);
}

static gg_source_http_error_t gg_map_winhttp_error(DWORD error)
{
   if (error == ERROR_WINHTTP_TIMEOUT)
      return GG_SOURCE_HTTP_ERROR_TIMEOUT;
   if (error == (DWORD)E_ABORT)
      return GG_SOURCE_HTTP_ERROR_DECOMPRESSION;
   if (error == ERROR_WINHTTP_SECURE_FAILURE ||
         error == ERROR_WINHTTP_SECURE_CERT_DATE_INVALID ||
         error == ERROR_WINHTTP_SECURE_CERT_CN_INVALID ||
         error == ERROR_WINHTTP_SECURE_INVALID_CA ||
         error == ERROR_WINHTTP_SECURE_CERT_REV_FAILED ||
         error == ERROR_WINHTTP_SECURE_CHANNEL_ERROR ||
         error == ERROR_WINHTTP_SECURE_INVALID_CERT ||
         error == ERROR_WINHTTP_SECURE_CERT_REVOKED ||
         error == ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE ||
         error == ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY ||
         error == ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY ||
         error == ERROR_WINHTTP_SECURE_FAILURE_PROXY)
      return GG_SOURCE_HTTP_ERROR_TLS;
   return GG_SOURCE_HTTP_ERROR_NETWORK;
}

static bool gg_utf8_to_wide(const char *text, size_t length, WCHAR *output,
      size_t output_count)
{
   int converted;
   if (length >= output_count)
      return false;
   converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
         (int)length, output, (int)(output_count - 1));
   if (converted <= 0)
      return false;
   output[converted] = L'\0';
   return true;
}

static bool gg_parse_https_url(gg_source_http_backend_t *backend,
      const char *url, size_t length)
{
   size_t authority_end = 8;
   size_t host_start = 8;
   size_t host_end;
   size_t resource_start;
   unsigned port = 443;
   WCHAR *host = backend->host;
   size_t host_length;
   if (!gg_catalog_https_url_valid(url, length))
      return false;
   while (authority_end < length && url[authority_end] != '/' &&
         url[authority_end] != '?')
      authority_end++;
   host_end = authority_end;
   if (url[8] == '[')
   {
      host_start = 9;
      host_end = host_start;
      while (host_end < authority_end && url[host_end] != ']')
         host_end++;
      if (host_end >= authority_end)
         return false;
      if (host_end + 1 < authority_end)
      {
         size_t i;
         unsigned value = 0;
         if (url[host_end + 1] != ':')
            return false;
         for (i = host_end + 2; i < authority_end; i++)
         {
            value = value * 10 + (unsigned)(url[i] - '0');
            if (value > 65535)
               return false;
         }
         port = value;
      }
   }
   else
   {
      size_t i;
      host_end = 8;
      while (host_end < authority_end && url[host_end] != ':')
         host_end++;
      if (host_end < authority_end)
      {
         unsigned value = 0;
         if (url[host_end] != ':')
            return false;
         for (i = host_end + 1; i < authority_end; i++)
         {
            value = value * 10 + (unsigned)(url[i] - '0');
            if (value > 65535)
               return false;
         }
         port = value;
      }
   }
   host_length = host_end - host_start;
   if (!gg_utf8_to_wide(url + host_start, host_length, host,
         sizeof(backend->host) / sizeof(backend->host[0])))
      return false;
   resource_start = authority_end;
   if (resource_start == length)
   {
      backend->resource[0] = L'/';
      backend->resource[1] = L'\0';
   }
   else if (url[resource_start] == '?')
   {
      backend->resource[0] = L'/';
      if (!gg_utf8_to_wide(url + resource_start, length - resource_start,
            backend->resource + 1,
            sizeof(backend->resource) / sizeof(backend->resource[0]) - 1))
         return false;
   }
   else if (!gg_utf8_to_wide(url + resource_start, length - resource_start,
         backend->resource,
         sizeof(backend->resource) / sizeof(backend->resource[0])))
      return false;
   backend->connect_port = (INTERNET_PORT)port;
   return true;
}

static bool gg_open_current_request(gg_source_http_backend_t *backend)
{
   DWORD disable_features = WINHTTP_DISABLE_COOKIES |
      WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
   DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
   gg_winhttp_api_t *api = backend->api;
   if (!gg_parse_https_url(backend, backend->current_url,
         strlen(backend->current_url)))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      return false;
   }
   backend->connection = api->connect(backend->session, backend->host,
         backend->connect_port, 0);
   if (!backend->connection)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      return false;
   }
   backend->request = api->open_request(backend->connection, L"GET",
         backend->resource, NULL, WINHTTP_NO_REFERER,
         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE |
         WINHTTP_FLAG_REFRESH);
   if (!backend->request)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      return false;
   }
   backend->request_context =
      (gg_http_request_context_t *)calloc(1, sizeof(*backend->request_context));
   if (!backend->request_context)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY;
      return false;
   }
   backend->request_context->backend = backend;
   retro_atomic_inc_int(&backend->references);
   if (!api->set_option(backend->request, WINHTTP_OPTION_CONTEXT_VALUE,
         &backend->request_context, sizeof(backend->request_context)))
   {
      gg_http_request_context_t *request_context =
         backend->request_context;
      backend->request_context = NULL;
      gg_close_request(backend);
      free(request_context);
      gg_backend_release(backend);
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      return false;
   }
   if (!api->set_option(backend->request, WINHTTP_OPTION_DISABLE_FEATURE,
         &disable_features, sizeof(disable_features)))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
      return false;
   }
   if (!api->set_option(backend->request, WINHTTP_OPTION_DECOMPRESSION,
         &decompression, sizeof(decompression)))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
      return false;
   }
   retro_atomic_store_release_int(&backend->events, 0);
   backend->phase = GG_HTTP_PHASE_SEND;
   if (!api->send_request(backend->request, WINHTTP_NO_ADDITIONAL_HEADERS,
         0, NULL, 0, 0, 0))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      return false;
   }
   return true;
}

static bool gg_query_location(gg_source_http_backend_t *backend,
      char *location, size_t location_size)
{
   DWORD bytes = 0;
   DWORD index = 0;
   WCHAR *wide = NULL;
   gg_winhttp_api_t *api = backend->api;
   int converted;
   if (api->query_headers(backend->request, WINHTTP_QUERY_LOCATION,
         NULL, NULL, &bytes, &index))
      return false;
   if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !bytes)
      return false;
   wide = (WCHAR *)malloc(bytes);
   if (!wide)
      return false;
   index = 0;
   if (!api->query_headers(backend->request, WINHTTP_QUERY_LOCATION,
         NULL, wide, &bytes, &index))
   {
      free(wide);
      return false;
   }
   converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
         -1, location, (int)location_size, NULL, NULL);
   free(wide);
   return converted > 0;
}

static bool gg_content_encoding_supported(gg_source_http_backend_t *backend)
{
   DWORD bytes = 0;
   DWORD index = 0;
   char encoding[64];
   int converted;
   gg_winhttp_api_t *api = backend->api;
   if (api->query_headers(backend->request, WINHTTP_QUERY_CONTENT_ENCODING,
         NULL, NULL, &bytes, &index))
      return true;
   if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
      return true;
   if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < 2 ||
         bytes > 128)
      return false;
   {
      WCHAR wide[64];
      DWORD wide_bytes = (DWORD)sizeof(wide);
      index = 0;
      if (!api->query_headers(backend->request,
            WINHTTP_QUERY_CONTENT_ENCODING, NULL, wide,
            &wide_bytes, &index))
         return false;
      converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
            -1, encoding, (int)sizeof(encoding), NULL, NULL);
   }
   if (converted <= 0)
      return false;
   return !_stricmp(encoding, "gzip") || !_stricmp(encoding, "deflate") ||
      !_stricmp(encoding, "identity");
}

static bool gg_handle_headers(gg_source_http_backend_t *backend)
{
   DWORD status = 0;
   DWORD bytes = sizeof(status);
   DWORD index = 0;
   if (!backend->api->query_headers(backend->request,
         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
         NULL, &status, &bytes, &index))
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_NETWORK);
      return false;
   }
   if (status == 301 || status == 302 || status == 303 ||
         status == 307 || status == 308)
   {
      char next[GG_CATALOG_MAX_URL_BYTES + 1];
      if (backend->redirect_count >= GG_SOURCE_HTTP_MAX_REDIRECTS ||
            !gg_query_location(backend, next, sizeof(next)) ||
            !gg_source_http_redirect_resolve(backend->current_url,
               strlen(backend->current_url), next, strlen(next),
               backend->next_url, sizeof(backend->next_url)))
      {
         gg_finish(backend, GG_SOURCE_HTTP_ERROR,
               GG_SOURCE_HTTP_ERROR_REDIRECT);
         return false;
      }
      backend->redirect_count++;
      {
         size_t next_length = strlen(backend->next_url);
         memcpy(backend->current_url, backend->next_url, next_length + 1);
      }
      gg_close_request(backend);
      gg_close_connection(backend);
      if (!gg_open_current_request(backend))
      {
         gg_finish(backend, GG_SOURCE_HTTP_ERROR, backend->error);
         return false;
      }
      return true;
   }
   if (status < 200 || status >= 300)
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_HTTP_STATUS);
      return false;
   }
   if (!gg_content_encoding_supported(backend))
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_UNSUPPORTED);
      return false;
   }
   backend->phase = GG_HTTP_PHASE_DATA;
   retro_atomic_store_release_int(&backend->events, 0);
   if (!backend->api->query_data(backend->request, NULL))
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_NETWORK);
      return false;
   }
   return true;
}

static bool gg_append_data(gg_source_http_backend_t *backend)
{
   size_t needed;
   if (!backend->read_count)
   {
      gg_finish(backend, GG_SOURCE_HTTP_DONE, GG_SOURCE_HTTP_ERROR_NONE);
      return false;
   }
   needed = backend->data_length + backend->read_count;
   if (needed < backend->data_length ||
         needed > GG_SOURCE_HTTP_MAX_BYTES)
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_TOO_LARGE);
      return false;
   }
   if (needed > backend->data_capacity)
   {
      size_t capacity = backend->data_capacity
         ? backend->data_capacity * 2 : GG_SOURCE_HTTP_READ_BUFFER_BYTES;
      char *data;
      if (capacity < needed)
         capacity = needed;
      if (capacity > GG_SOURCE_HTTP_MAX_BYTES)
         capacity = GG_SOURCE_HTTP_MAX_BYTES;
      data = (char *)realloc(backend->data, capacity);
      if (!data)
      {
         gg_finish(backend, GG_SOURCE_HTTP_ERROR,
               GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY);
         return false;
      }
      backend->data = data;
      backend->data_capacity = capacity;
   }
   memcpy(backend->data + backend->data_length, backend->read_buffer,
         backend->read_count);
   backend->data_length = needed;
   backend->phase = GG_HTTP_PHASE_DATA;
   retro_atomic_store_release_int(&backend->events, 0);
   if (!backend->api->query_data(backend->request, NULL))
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR,
            GG_SOURCE_HTTP_ERROR_NETWORK);
      return false;
   }
   return true;
}

gg_source_http_backend_t *gg_source_http_backend_create(const char *url,
      size_t length, int64_t deadline_ms, gg_source_http_error_t *error)
{
   gg_source_http_backend_t *backend;
   gg_winhttp_api_t *api;
   WINHTTP_STATUS_CALLBACK previous_callback = NULL;
   (void)deadline_ms;
   if (!url || !length || length > GG_CATALOG_MAX_URL_BYTES || !error)
   {
      if (error)
         *error = GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT;
      return NULL;
   }
   api = gg_get_winhttp_api();
   if (!api)
   {
      *error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
      return NULL;
   }
   backend = (gg_source_http_backend_t *)calloc(1, sizeof(*backend));
   if (!backend)
   {
      *error = GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY;
      return NULL;
   }
   backend->api = api;
   memcpy(backend->current_url, url, length);
   backend->current_url[length] = '\0';
   retro_atomic_int_init(&backend->events, 0);
   retro_atomic_int_init(&backend->references, 1);
   retro_atomic_int_init(&backend->cancelled, 0);
   retro_atomic_int_init(&backend->destroying, 0);
   retro_atomic_int_init(&backend->state, GG_SOURCE_HTTP_WORKING);
   backend->session = api->open(L"GameGate/1.0",
         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
         WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
   if (!backend->session)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
   }
   else if (!api->set_timeouts(backend->session, 15000, 15000, 15000,
         15000))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
   }
   else if ((previous_callback = api->set_callback(backend->session,
         gg_http_callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
         WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING |
         WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0)) ==
         WINHTTP_INVALID_STATUS_CALLBACK)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
   }
   if (!backend->session ||
         backend->error != GG_SOURCE_HTTP_ERROR_NONE ||
         !gg_open_current_request(backend))
   {
      if (backend->error == GG_SOURCE_HTTP_ERROR_NONE)
         backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      gg_close_request(backend);
      gg_close_connection(backend);
      if (backend->session)
      {
         api->close(backend->session);
         backend->session = NULL;
      }
      retro_atomic_store_release_int(&backend->state,
            GG_SOURCE_HTTP_ERROR);
      *error = backend->error;
      return backend;
   }
   *error = GG_SOURCE_HTTP_ERROR_NONE;
   return backend;
}

gg_source_http_status_t gg_source_http_backend_poll(
      gg_source_http_backend_t *backend, int64_t now_ms)
{
   gg_source_http_status_t state;
   int events;
   (void)now_ms;
   if (!backend)
      return GG_SOURCE_HTTP_ERROR;
   state = (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &backend->state);
   if (state != GG_SOURCE_HTTP_WORKING)
      return state;
   if (retro_atomic_load_acquire_int(&backend->cancelled))
   {
      gg_finish(backend, GG_SOURCE_HTTP_CANCELLED,
            GG_SOURCE_HTTP_ERROR_CANCELLED);
      return GG_SOURCE_HTTP_CANCELLED;
   }
   events = retro_atomic_exchange_int(&backend->events, 0);
   if (!events)
      return GG_SOURCE_HTTP_WORKING;
   if (events & WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
   {
      gg_source_http_error_t error = gg_map_winhttp_error(
            backend->async_error);
      gg_finish(backend, GG_SOURCE_HTTP_ERROR, error);
      return GG_SOURCE_HTTP_ERROR;
   }
   if (events & WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
   {
      gg_finish(backend, GG_SOURCE_HTTP_ERROR, GG_SOURCE_HTTP_ERROR_TLS);
      return GG_SOURCE_HTTP_ERROR;
   }
   if (backend->phase == GG_HTTP_PHASE_SEND)
   {
      if (!(events & WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE))
         return GG_SOURCE_HTTP_WORKING;
      backend->phase = GG_HTTP_PHASE_RESPONSE;
      retro_atomic_store_release_int(&backend->events, 0);
      if (!backend->api->receive_response(backend->request, NULL))
      {
         gg_finish(backend, GG_SOURCE_HTTP_ERROR,
               GG_SOURCE_HTTP_ERROR_NETWORK);
         return GG_SOURCE_HTTP_ERROR;
      }
      return GG_SOURCE_HTTP_WORKING;
   }
   if (backend->phase == GG_HTTP_PHASE_RESPONSE)
   {
      if (!(events & WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE))
         return GG_SOURCE_HTTP_WORKING;
      gg_handle_headers(backend);
      return (gg_source_http_status_t)retro_atomic_load_acquire_int(
            &backend->state);
   }
   if (backend->phase == GG_HTTP_PHASE_DATA)
   {
      if (!(events & WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE))
         return GG_SOURCE_HTTP_WORKING;
      if (!backend->available)
      {
         gg_finish(backend, GG_SOURCE_HTTP_DONE,
               GG_SOURCE_HTTP_ERROR_NONE);
         return GG_SOURCE_HTTP_DONE;
      }
      backend->phase = GG_HTTP_PHASE_READ;
      retro_atomic_store_release_int(&backend->events, 0);
      if (!backend->api->read_data(backend->request, backend->read_buffer,
            (DWORD)sizeof(backend->read_buffer), NULL))
      {
         gg_finish(backend, GG_SOURCE_HTTP_ERROR,
               GG_SOURCE_HTTP_ERROR_NETWORK);
         return GG_SOURCE_HTTP_ERROR;
      }
      return GG_SOURCE_HTTP_WORKING;
   }
   if (events & WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
   {
      gg_append_data(backend);
      return (gg_source_http_status_t)retro_atomic_load_acquire_int(
            &backend->state);
   }
   return GG_SOURCE_HTTP_WORKING;
}

void gg_source_http_backend_cancel(gg_source_http_backend_t *backend)
{
   if (backend)
      retro_atomic_store_release_int(&backend->cancelled, 1);
}

void gg_source_http_backend_destroy(gg_source_http_backend_t *backend)
{
   if (!backend)
      return;
   retro_atomic_store_release_int(&backend->cancelled, 1);
   retro_atomic_store_release_int(&backend->destroying, 1);
   gg_close_request(backend);
   gg_close_connection(backend);
   if (backend->session)
   {
      backend->api->close(backend->session);
      backend->session = NULL;
   }
   gg_backend_release(backend);
}

gg_source_http_status_t gg_source_http_backend_status(
      const gg_source_http_backend_t *backend)
{
   return backend ? (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &backend->state) : GG_SOURCE_HTTP_ERROR;
}

gg_source_http_error_t gg_source_http_backend_error(
      const gg_source_http_backend_t *backend)
{
   return backend ? backend->error : GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT;
}

const void *gg_source_http_backend_data(
      const gg_source_http_backend_t *backend, size_t *length)
{
   if (!backend || !length)
      return NULL;
   *length = backend->data_length;
   return backend->data;
}

void *gg_source_http_backend_take_data(
      gg_source_http_backend_t *backend, size_t *length)
{
   void *data = backend->data;
   *length = backend->data_length;
   backend->data = NULL;
   backend->data_length = 0;
   return data;
}

#else
typedef int gg_source_http_win32_disabled_t;
#endif
