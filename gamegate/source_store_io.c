#include <errno.h>
#include <stdlib.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include "source_store_io.h"

static gg_store_result_t gg_vfs_read(void *ctx, const char *path,
      size_t limit, void **data, size_t *size)
{
   RFILE *file;
   int64_t length;
   void *buffer;
   char extra;
   bool ok;
   (void)ctx;
   *data = NULL;
   *size = 0;
   errno = 0;
   file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 0);
   if (!file)
      return errno == ENOENT ? GG_STORE_NOT_FOUND : GG_STORE_IO;
   length = filestream_get_size(file);
   if (length < 0 || (uint64_t)length > limit)
   {
      filestream_close(file);
      return length < 0 ? GG_STORE_IO : GG_STORE_LIMIT;
   }
   buffer = malloc((size_t)length + 1);
   if (!buffer)
   {
      filestream_close(file);
      return GG_STORE_NOMEM;
   }
   ok = filestream_read(file, buffer, length) == length
      && filestream_read(file, &extra, 1) == 0;
   if (filestream_close(file) != 0)
      ok = false;
   if (!ok)
   {
      free(buffer);
      return GG_STORE_IO;
   }
   ((char*)buffer)[(size_t)length] = '\0';
   *data = buffer;
   *size = (size_t)length;
   return GG_STORE_OK;
}

static void *gg_vfs_open_write(void *ctx, const char *path)
{
   (void)ctx;
   return filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, 0);
}

static bool gg_vfs_write(void *ctx, void *file, const void *data, size_t size)
{
   (void)ctx;
   return filestream_write((RFILE*)file, data, (int64_t)size) == (int64_t)size;
}

static bool gg_vfs_flush(void *ctx, void *file)
{
   (void)ctx;
   return filestream_flush((RFILE*)file) == 0;
}

static bool gg_vfs_close(void *ctx, void *file)
{
   (void)ctx;
   return filestream_close((RFILE*)file) == 0;
}

static bool gg_vfs_replace(void *ctx, const char *src, const char *dst)
{
   (void)ctx;
   return filestream_rename(src, dst) == 0;
}

static bool gg_vfs_remove(void *ctx, const char *path)
{
   (void)ctx;
   return filestream_delete(path) == 0;
}

static bool gg_vfs_mkdir(void *ctx, const char *path)
{
   (void)ctx;
   return path_is_directory(path) || path_mkdir(path);
}

const gg_store_io_t gg_store_vfs_io = {
   gg_vfs_read, gg_vfs_open_write, gg_vfs_write, gg_vfs_flush,
   gg_vfs_close, gg_vfs_replace, gg_vfs_remove, gg_vfs_mkdir
};
