// SPDX-License-Identifier: LGPL-2.1-or-later
/* /sdcard remap. The firmware hard-codes "/sdcard/..."; at link time these calls are wrapped (-Wl,--wrap=...)
 * and a path starting with /sdcard is moved under --root. Shared libraries (system FreeType) are not wrapped,
 * so the font adapter must call sim_path() itself. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sim.h"

static char g_root[PATH_MAX];

void sim_set_root(const char *dir)
{
    snprintf(g_root, sizeof g_root, "%s", dir ? dir : "");
    for (size_t n = strlen(g_root); n > 1 && g_root[n - 1] == '/'; ) g_root[--n] = 0;
}

/* A ".." component would leave the root (and the firmware never writes one): refused with NULL, which every wrapper below turns into ENOENT, the
 * answer for a file that is not there. A path that is not under /sdcard is not ours and is returned as it is. */
const char *sim_path(const char *p, char *buf, size_t n)
{
    if (!p || !g_root[0] || strncmp(p, "/sdcard", 7) || (p[7] && p[7] != '/')) return p;
    for (const char *s = p + 7; *s; ) {
        s += strspn(s, "/");
        size_t len = strcspn(s, "/");
        if (len == 2 && s[0] == '.' && s[1] == '.') return NULL;
        s += len;
    }
    int len = snprintf(buf, n, "%s%s", g_root, p + 7);
    return len < 0 || (size_t)len >= n ? p : buf;   /* too long: leave it, the call then fails with ENOENT */
}

FILE *__real_fopen(const char *, const char *);
int __real_open(const char *, int, ...);
int __real_stat(const char *, struct stat *);
DIR *__real_opendir(const char *);
int __real_remove(const char *);
int __real_rename(const char *, const char *);
int __real_mkdir(const char *, mode_t);
int __real_unlink(const char *);
int __real_rmdir(const char *);
int __real_access(const char *, int);
int __real_truncate(const char *, off_t);

/* P: map the path; REFUSE: a path outside the root fails like a missing file */
#define P(var, p) char var##_b[PATH_MAX]; const char *var = sim_path(p, var##_b, sizeof var##_b)
#define REFUSE(ret, ...) do { const char *c_[] = { __VA_ARGS__ }; for (size_t i_ = 0; i_ < sizeof c_ / sizeof c_[0]; i_++) if (!c_[i_]) { errno = ENOENT; return ret; } } while (0)

FILE *__wrap_fopen(const char *path, const char *mode) { P(p, path); REFUSE(NULL, p); return __real_fopen(p, mode); }

int __wrap_open(const char *path, int flags, ...)
{
    P(p, path);
    REFUSE(-1, p);
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    return __real_open(p, flags, mode);
}

int __wrap_stat(const char *path, struct stat *st) { P(p, path); REFUSE(-1, p); return __real_stat(p, st); }
DIR *__wrap_opendir(const char *path) { P(p, path); REFUSE(NULL, p); return __real_opendir(p); }
int __wrap_remove(const char *path) { P(p, path); REFUSE(-1, p); return __real_remove(p); }
int __wrap_rename(const char *from, const char *to) { P(a, from); P(b, to); REFUSE(-1, a, b); return __real_rename(a, b); }
int __wrap_mkdir(const char *path, mode_t m) { P(p, path); REFUSE(-1, p); return __real_mkdir(p, m); }
int __wrap_unlink(const char *path) { P(p, path); REFUSE(-1, p); return __real_unlink(p); }
int __wrap_rmdir(const char *path) { P(p, path); REFUSE(-1, p); return __real_rmdir(p); }
int __wrap_access(const char *path, int m) { P(p, path); REFUSE(-1, p); return __real_access(p, m); }
int __wrap_truncate(const char *path, off_t n) { P(p, path); REFUSE(-1, p); return __real_truncate(p, n); }
