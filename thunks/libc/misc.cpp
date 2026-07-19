#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <stdarg.h>
#include <link.h>
#include <sys/syscall.h>
#include "platform.h"
#include "so_util.h"
#include "gmloader/gamedata_mmap.h"

#include "bionic_file.h"

// game.droid may be handed to the runner as an mmap rather than a heap buffer;
// intercept its release so the mapping never reaches the host allocator.
extern "C" ABI_ATTR void free_impl(void *ptr)
{
    if (gamedata_mmap_release(ptr))
        return;
    free(ptr);
}

extern "C" ABI_ATTR int dl_iterate_phdr_impl(
                 int (*callback)(struct dl_phdr_info *info,
                                 size_t size, void *data),
                 void *data)
{
    // TODO:: Implement a reasonable version of this.
    fatal_error("-- dl_iterate_phdr was called! --\n");
    return -1;
}

extern "C" ABI_ATTR int login_tty_impl(int fd)
{
    return -1;
}

extern "C" ABI_ATTR long syscall_impl(long number, ...)
{
#ifdef gettid
    if (number == 0xb2)
        return gettid();
#else
    if (number == 0xb2)
        return syscall(SYS_gettid);
#endif
    return 0;
}

extern "C" ABI_ATTR void abort_impl(void)
{
    fatal_error("Guest called abort!\n");
    exit(-1);
}

extern "C" ABI_ATTR void *dlopen_impl(const char *filename, int flags)
{
    if (filename == NULL)
        return NULL;

    char *fn = strdup(filename);
    char *ex = basename(fn);
    int ret = strncmp(ex, "libEGL", 6) == 0 ||
              strncmp(ex, "libGL", 5) == 0 ||
              strncmp(ex, "libOpenSLES", 11) == 0;

    return (ret) ? (void*)0xDEAD : NULL;
}

extern "C" ABI_ATTR char *dlerror_impl(void)
{
    WARN_STUB
    return NULL;
}

extern "C" ABI_ATTR int dlclose_impl(void *handle)
{
    /* ... */
    return 0;
}

extern "C" ABI_ATTR void *dlsym_impl(void *handle, const char *name)
{
    return (void*)so_resolve_link(NULL, name);
}

extern "C" ABI_ATTR const void *
memchr_impl (const void *__s, int __c, size_t __n)
{
  return __builtin_memchr (__s, __c, __n);
}

extern "C" ABI_ATTR int sigsetmask_impl(int mask)
{
    WARN_STUB
    return -1;
}

extern "C" ABI_ATTR char *tempnam_impl(const char *dir, const char *pfx)
{
    WARN_STUB
    return NULL;
}

extern "C" ABI_ATTR char *tmpnam_impl(char *s)
{
    WARN_STUB
    return NULL;
}

extern "C" ABI_ATTR char *mktemp_impl(char *_template)
{
    WARN_STUB
    return NULL;
}

extern "C" ABI_ATTR int* __errno_impl(void)
{
    return __errno_location();
}

extern "C" ABI_ATTR int __android_log_write_impl(int prio, const char *tag, const char *text)
{
    char andlog[2048] = {};
    warning("LOG[%s]: %s\n", tag, text);
    return 1;
}

extern "C" ABI_ATTR int __android_log_print_impl(int prio, const char *tag, const char *fmt, ...)
{
    char andlog[2048] = {};
    va_list va;
    va_start(va, fmt);
    warning("LOG[%s]: ", tag);
    int r = vsnprintf(andlog, 2047, fmt, va);
    warning("%s\n", andlog);
    va_end(va);
    return r;
}

extern "C" ABI_ATTR int __android_log_vprint_impl(int prio, const char *tag, const char *fmt, va_list va)
{
    char andlog[2048] = {};
    warning("LOG[%s]: ", tag);
    int r = vsnprintf(andlog, 2047, fmt, va);
    warning("%s\n", andlog);
    return r;
}

extern "C" ABI_ATTR const char* __strchr_chk(const char* __s, int __ch, size_t __n) { return strchr(__s, __ch); }
extern "C" ABI_ATTR const char* __strrchr_chk(const char* __s, int __ch, size_t __n) { return strrchr(__s, __ch); }
extern "C" ABI_ATTR size_t __strlen_chk(const char* __s, size_t __n) { return strnlen(__s, __n); }

extern "C" ABI_ATTR void android_set_abort_message_impl(const char* msg)
{
    fatal_error("%s", msg);
    abort();
}

extern "C" ABI_ATTR int __system_property_get_impl(const char *name, char *value)
{
    WARN_STUB;
    value[0] = 0;
    return 0;
}

extern "C" ABI_ATTR int __open_2_impl(const char* pathname, int flags) {
  return open(pathname, flags);
}

// Bionic declares open() variadic; mode is only read when flags ask for it,
// so a fixed three-argument callee is ABI-compatible on both AAPCS targets.
extern "C" ABI_ATTR int open_impl(const char* pathname, int flags, mode_t mode) {
  return open(pathname, flags, mode);
}

extern "C" ABI_ATTR int open64_impl(const char* pathname, int flags, mode_t mode) {
#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif
  return open(pathname, flags, mode);
}

// Bionic's struct statvfs matches glibc's on LP64, but on ILP32 glibc inserts
// an extra __f_unused word after f_fsid, so translate field-by-field.
struct bionic_statvfs {
  unsigned long f_bsize;
  unsigned long f_frsize;
  fsblkcnt_t f_blocks;
  fsblkcnt_t f_bfree;
  fsblkcnt_t f_bavail;
  fsfilcnt_t f_files;
  fsfilcnt_t f_ffree;
  fsfilcnt_t f_favail;
  unsigned long f_fsid;
  unsigned long f_flag;
  unsigned long f_namemax;
#if defined(__LP64__)
  uint32_t __f_reserved[6];
#endif
};

static void statvfs_to_bionic(const struct statvfs *in, struct bionic_statvfs *out) {
  out->f_bsize = in->f_bsize;
  out->f_frsize = in->f_frsize;
  out->f_blocks = in->f_blocks;
  out->f_bfree = in->f_bfree;
  out->f_bavail = in->f_bavail;
  out->f_files = in->f_files;
  out->f_ffree = in->f_ffree;
  out->f_favail = in->f_favail;
  out->f_fsid = in->f_fsid;
  out->f_flag = in->f_flag;
  out->f_namemax = in->f_namemax;
}

extern "C" ABI_ATTR int statvfs_impl(const char* path, struct bionic_statvfs* buf) {
  struct statvfs st;
  int ret = statvfs(path, &st);
  if (ret == 0 && buf)
    statvfs_to_bionic(&st, buf);
  return ret;
}

extern "C" ABI_ATTR int fstatvfs_impl(int fd, struct bionic_statvfs* buf) {
  struct statvfs st;
  int ret = fstatvfs(fd, &st);
  if (ret == 0 && buf)
    statvfs_to_bionic(&st, buf);
  return ret;
}

// Taken from https://github.com/libhybris/libhybris/blob/master/hybris/common/hooks.c
ABI_ATTR int scandirat_impl(int fd, const char *dir,
                      struct bionic_dirent ***namelist,
                      int (*filter) (const struct bionic_dirent *),
                      int (*compar) (const struct bionic_dirent **,
                                     const struct bionic_dirent **))
{
    struct dirent **namelist_r;
    struct bionic_dirent **result;
    struct bionic_dirent *filter_r;

    int i = 0;
    size_t nItems = 0;

    int res = scandirat(fd, dir, &namelist_r, NULL, NULL);

    if (res > 0 && namelist_r != NULL) {
        result = (bionic_dirent**)malloc(res * sizeof(struct bionic_dirent));
        if (!result)
            return -1;

        for (i = 0; i < res; i++) {
            filter_r = (bionic_dirent*)malloc(sizeof(struct bionic_dirent));
            if (!filter_r) {
                while (i-- > 0)
                    free(result[i]);
                free(result);
                return -1;
            }

            filter_r->d_ino = namelist_r[i]->d_ino;
            filter_r->d_off = namelist_r[i]->d_off;
            filter_r->d_reclen = namelist_r[i]->d_reclen;
            filter_r->d_type = namelist_r[i]->d_type;

            strcpy(filter_r->d_name, namelist_r[i]->d_name);
            filter_r->d_name[sizeof(namelist_r[i]->d_name) - 1] = '\0';

            if (filter != NULL && !(*filter)(filter_r)) {//apply filter
                free(filter_r);
                continue;
            }

            result[nItems++] = filter_r;
        }
        
        if (nItems && compar != NULL) // sort
            qsort(result, nItems, sizeof(struct bionic_dirent *), (__compar_fn_t)compar);

        *namelist = result;
    } else {
        return res;
    }

    return nItems;
}

ABI_ATTR int scandir_impl(const char *dir,
                      struct bionic_dirent ***namelist,
                      int (*filter) (const struct bionic_dirent *),
                      int (*compar) (const struct bionic_dirent **,
                                     const struct bionic_dirent **))
{
    return scandirat_impl(AT_FDCWD, dir, namelist, filter, compar);
}
