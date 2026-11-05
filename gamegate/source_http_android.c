#define GG_SOURCE_HTTP_INTERNAL
#include "source_http.h"

#ifdef GG_SOURCE_HTTP_ANDROID

#include "../frontend/drivers/platform_unix.h"

#include <stdlib.h>
#include <string.h>

struct gg_source_http_backend
{
   jobject request;
   jmethodID get_state;
   jmethodID get_error;
   jmethodID get_data;
   jmethodID cancel;
   char *data;
   size_t data_length;
   gg_source_http_error_t error;
   retro_atomic_int_t state;
};

static bool gg_clear_java_exception(JNIEnv *env)
{
   if (!(*env)->ExceptionCheck(env))
      return false;
   (*env)->ExceptionClear(env);
   return true;
}

static gg_source_http_error_t gg_java_error(int error)
{
   if (error < GG_SOURCE_HTTP_ERROR_NONE ||
         error > GG_SOURCE_HTTP_ERROR_INTERNAL)
      return GG_SOURCE_HTTP_ERROR_INTERNAL;
   return (gg_source_http_error_t)error;
}

static gg_source_http_status_t gg_java_state(int state)
{
   if (state < GG_SOURCE_HTTP_WORKING ||
         state > GG_SOURCE_HTTP_UNSUPPORTED)
      return GG_SOURCE_HTTP_ERROR;
   return (gg_source_http_status_t)state;
}

gg_source_http_backend_t *gg_source_http_backend_create(const char *url,
      size_t length, int64_t deadline_ms, gg_source_http_error_t *error)
{
   JNIEnv *env;
   jclass activity_class = NULL;
   jclass request_class = NULL;
   jmethodID factory = NULL;
   jstring java_url = NULL;
   jobject request = NULL;
   jobject global_request = NULL;
   gg_source_http_backend_t *backend = NULL;
   (void)deadline_ms;
   if (!url || !length || length > GG_CATALOG_MAX_URL_BYTES || !error)
   {
      if (error)
         *error = GG_SOURCE_HTTP_ERROR_INVALID_ARGUMENT;
      return NULL;
   }
   env = jni_thread_getenv();
   if (!env || !g_android || !g_android->activity ||
         !g_android->activity->clazz)
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
   retro_atomic_int_init(&backend->state, GG_SOURCE_HTTP_WORKING);
   backend->error = GG_SOURCE_HTTP_ERROR_NONE;
   activity_class = (*env)->GetObjectClass(env,
         g_android->activity->clazz);
   if (gg_clear_java_exception(env) || !activity_class)
      goto fail;
   factory = (*env)->GetStaticMethodID(env, activity_class,
         "createGameGateSourceRequest",
         "(Ljava/lang/String;J)Lcom/retroarch/browser/retroactivity/"
         "GameGateSourceRequest;");
   if (gg_clear_java_exception(env) || !factory)
      goto fail;
   java_url = (*env)->NewStringUTF(env, url);
   if (gg_clear_java_exception(env) || !java_url)
      goto fail;
   request = (*env)->CallStaticObjectMethod(env, activity_class, factory,
         java_url, (jlong)GG_SOURCE_HTTP_DEADLINE_MS);
   if (gg_clear_java_exception(env) || !request)
      goto fail;
   global_request = (*env)->NewGlobalRef(env, request);
   if (gg_clear_java_exception(env) || !global_request)
      goto fail;
   request_class = (*env)->GetObjectClass(env, global_request);
   if (gg_clear_java_exception(env) || !request_class)
      goto fail;
   backend->get_state = (*env)->GetMethodID(env, request_class,
         "getState", "()I");
   if (gg_clear_java_exception(env) || !backend->get_state)
      goto fail;
   backend->get_error = (*env)->GetMethodID(env, request_class,
         "getError", "()I");
   if (gg_clear_java_exception(env) || !backend->get_error)
      goto fail;
   backend->get_data = (*env)->GetMethodID(env, request_class,
         "getData", "()[B");
   if (gg_clear_java_exception(env) || !backend->get_data)
      goto fail;
   backend->cancel = (*env)->GetMethodID(env, request_class,
         "cancel", "()V");
   if (gg_clear_java_exception(env) || !backend->cancel)
      goto fail;
   backend->request = global_request;
   (*env)->DeleteLocalRef(env, request);
   (*env)->DeleteLocalRef(env, java_url);
   (*env)->DeleteLocalRef(env, request_class);
   (*env)->DeleteLocalRef(env, activity_class);
   *error = GG_SOURCE_HTTP_ERROR_NONE;
   return backend;

fail:
   if (global_request)
   {
      jmethodID cancel = NULL;
      gg_clear_java_exception(env);
      if (request_class)
         cancel = (*env)->GetMethodID(env, request_class, "cancel", "()V");
      gg_clear_java_exception(env);
      if (cancel)
      {
         (*env)->CallVoidMethod(env, global_request, cancel);
         gg_clear_java_exception(env);
      }
      (*env)->DeleteGlobalRef(env, global_request);
   }
   else if (request)
   {
      jclass request_class_local;
      gg_clear_java_exception(env);
      request_class_local = (*env)->GetObjectClass(env, request);
      if (gg_clear_java_exception(env))
      {
         if (request_class_local)
            (*env)->DeleteLocalRef(env, request_class_local);
      }
      else if (request_class_local)
      {
         jmethodID cancel = (*env)->GetMethodID(env, request_class_local,
               "cancel", "()V");
         gg_clear_java_exception(env);
         if (cancel)
         {
            (*env)->CallVoidMethod(env, request, cancel);
            gg_clear_java_exception(env);
         }
         (*env)->DeleteLocalRef(env, request_class_local);
      }
   }
   if (request_class)
      (*env)->DeleteLocalRef(env, request_class);
   if (request)
      (*env)->DeleteLocalRef(env, request);
   if (java_url)
      (*env)->DeleteLocalRef(env, java_url);
   if (activity_class)
      (*env)->DeleteLocalRef(env, activity_class);
   free(backend);
   *error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
   return NULL;
}

static void gg_fetch_java_data(gg_source_http_backend_t *backend,
      JNIEnv *env)
{
   jbyteArray array = (jbyteArray)(*env)->CallObjectMethod(env,
         backend->request, backend->get_data);
   jsize length;
   if (gg_clear_java_exception(env) || !array)
   {
      if (array)
         (*env)->DeleteLocalRef(env, array);
      backend->error = GG_SOURCE_HTTP_ERROR_INTERNAL;
      retro_atomic_store_release_int(&backend->state, GG_SOURCE_HTTP_ERROR);
      return;
   }
   length = (*env)->GetArrayLength(env, array);
   if (gg_clear_java_exception(env) || length < 0 ||
         (size_t)length > GG_SOURCE_HTTP_MAX_BYTES)
   {
      (*env)->DeleteLocalRef(env, array);
      backend->error = GG_SOURCE_HTTP_ERROR_TOO_LARGE;
      retro_atomic_store_release_int(&backend->state, GG_SOURCE_HTTP_ERROR);
      return;
   }
   if (length)
   {
      backend->data = (char *)malloc((size_t)length);
      if (!backend->data)
      {
         (*env)->DeleteLocalRef(env, array);
         backend->error = GG_SOURCE_HTTP_ERROR_OUT_OF_MEMORY;
         retro_atomic_store_release_int(&backend->state,
               GG_SOURCE_HTTP_ERROR);
         return;
      }
      (*env)->GetByteArrayRegion(env, array, 0, length,
            (jbyte *)backend->data);
      if (gg_clear_java_exception(env))
      {
         free(backend->data);
         backend->data = NULL;
         (*env)->DeleteLocalRef(env, array);
         backend->error = GG_SOURCE_HTTP_ERROR_INTERNAL;
         retro_atomic_store_release_int(&backend->state,
               GG_SOURCE_HTTP_ERROR);
         return;
      }
   }
   backend->data_length = (size_t)length;
   backend->error = GG_SOURCE_HTTP_ERROR_NONE;
   retro_atomic_store_release_int(&backend->state, GG_SOURCE_HTTP_DONE);
   (*env)->DeleteLocalRef(env, array);
}

gg_source_http_status_t gg_source_http_backend_poll(
      gg_source_http_backend_t *backend, int64_t now_ms)
{
   JNIEnv *env;
   jint state;
   jint error;
   gg_source_http_status_t current;
   (void)now_ms;
   if (!backend)
      return GG_SOURCE_HTTP_ERROR;
   current = (gg_source_http_status_t)retro_atomic_load_acquire_int(
         &backend->state);
   if (current != GG_SOURCE_HTTP_WORKING)
      return current;
   env = jni_thread_getenv();
   if (!env)
   {
      backend->error = GG_SOURCE_HTTP_ERROR_NETWORK;
      retro_atomic_store_release_int(&backend->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   state = (*env)->CallIntMethod(env, backend->request, backend->get_state);
   if (gg_clear_java_exception(env))
   {
      backend->error = GG_SOURCE_HTTP_ERROR_INTERNAL;
      retro_atomic_store_release_int(&backend->state, GG_SOURCE_HTTP_ERROR);
      return GG_SOURCE_HTTP_ERROR;
   }
   current = gg_java_state((int)state);
   if (current == GG_SOURCE_HTTP_WORKING)
      return current;
   if (current == GG_SOURCE_HTTP_DONE)
   {
      gg_fetch_java_data(backend, env);
      return (gg_source_http_status_t)retro_atomic_load_acquire_int(
            &backend->state);
   }
   error = (*env)->CallIntMethod(env, backend->request, backend->get_error);
   if (gg_clear_java_exception(env))
      backend->error = GG_SOURCE_HTTP_ERROR_INTERNAL;
   else
      backend->error = gg_java_error((int)error);
   if (current == GG_SOURCE_HTTP_CANCELLED)
      backend->error = GG_SOURCE_HTTP_ERROR_CANCELLED;
   else if (current == GG_SOURCE_HTTP_UNSUPPORTED)
      backend->error = GG_SOURCE_HTTP_ERROR_UNSUPPORTED;
   retro_atomic_store_release_int(&backend->state, current);
   return current;
}

void gg_source_http_backend_cancel(gg_source_http_backend_t *backend)
{
   JNIEnv *env;
   if (!backend)
      return;
   env = jni_thread_getenv();
   if (!env)
      return;
   (*env)->CallVoidMethod(env, backend->request, backend->cancel);
   gg_clear_java_exception(env);
}

void gg_source_http_backend_destroy(gg_source_http_backend_t *backend)
{
   JNIEnv *env;
   if (!backend)
      return;
   env = jni_thread_getenv();
   if (env)
   {
      (*env)->CallVoidMethod(env, backend->request, backend->cancel);
      gg_clear_java_exception(env);
      (*env)->DeleteGlobalRef(env, backend->request);
   }
   free(backend->data);
   free(backend);
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
typedef int gg_source_http_android_disabled_t;
#endif
