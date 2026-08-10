#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/*******************************************************************************
 *                            Structs
 ******************************************************************************/
struct file {
    char *name;
    uid_t uid;
    gid_t gid;
} file = {0};

/*******************************************************************************
 *                            Destructor
 ******************************************************************************/

void destructor(void) __attribute__((destructor));

void
destructor(void)
{
    free(file.name);
}

/*******************************************************************************
 *                              Chown wrapping
 ******************************************************************************/

/** Records the UID and GID and pretend syscall worked */
static int
chown_helper(const char *pathname, uid_t owner, gid_t group)
{
    if (strlen(pathname) > 7 && strncmp(pathname, "/dev/pt", 7) == 0) {
        /*
         * The OpenSSH server modified the PTY which requires root permissions
         * see torture_request_pty_modes
         * */
        return 0;
    }
    if (strlen(pathname) > 4 && strncmp(pathname, "/tmp", 4) == 0) {
        /*
         * faking chown because It requires root permissions to modify the owner
         * under /tmp
         * It's also a helper for torture_sftp_setstat
         * */
        if (file.name != NULL) {
            free((char *)file.name);
        }
        file.name = strdup(pathname);
        file.uid = owner;
        file.gid = group;
        return 0;
    }
    return -1;
}

#define WRAP_CHOWN(syscall_name)                                      \
    typedef int (*__libc_##syscall_name)(const char *pathname,        \
                                         uid_t owner,                 \
                                         gid_t group);                \
    int syscall_name(const char *pathname, uid_t owner, gid_t group); \
    int syscall_name(const char *pathname, uid_t owner, gid_t group)  \
    {                                                                 \
        __libc_##syscall_name original_##syscall_name = NULL;         \
        int rc;                                                       \
                                                                      \
        rc = chown_helper(pathname, owner, group);                    \
        if (rc == 0) {                                                \
            return 0;                                                 \
        }                                                             \
        original_##syscall_name =                                     \
            (__libc_##syscall_name)dlsym(RTLD_NEXT, #syscall_name);   \
        return (*original_##syscall_name)(pathname, owner, group);    \
    }

WRAP_CHOWN(chown)
WRAP_CHOWN(chown32)
WRAP_CHOWN(lchown)

/* fchownat */
typedef int (*__libc_fchownat)(int dirfd,
                               const char *pathname,
                               uid_t owner,
                               gid_t group,
                               int flags);

int
fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);

int
fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags)
{
    __libc_fchownat original_fchownat = NULL;
    int rc;

    rc = chown_helper(pathname, owner, group);
    if (rc == 0) {
        return 0;
    }

    original_fchownat = (__libc_fchownat)dlsym(RTLD_NEXT, "fchownat");
    return (*original_fchownat)(dirfd, pathname, owner, group, flags);
}

/*******************************************************************************
 *                              Stat wrapping
 ******************************************************************************/

/** Returns previously set UID/GID for the filename */
static void
stat_helper(const char *pathname, struct stat *statbuf)
{
    if (file.name != NULL && strcmp(pathname, file.name) == 0) {
        statbuf->st_uid = file.uid;
        statbuf->st_gid = file.gid;
    }
}

static void
stat64_helper(const char *pathname, struct stat64 *statbuf)
{
    if (file.name != NULL && strcmp(pathname, file.name) == 0) {
        statbuf->st_uid = file.uid;
        statbuf->st_gid = file.gid;
    }
}

#define WRAP_STAT(syscall_name, struct_name)                             \
    typedef int (*__libc_##syscall_name)(const char *pathname,           \
                                         struct struct_name *statbuf);   \
    int syscall_name(const char *pathname, struct struct_name *statbuf); \
    int syscall_name(const char *pathname, struct struct_name *statbuf)  \
    {                                                                    \
        int rc;                                                          \
        __libc_##syscall_name original_##syscall_name = NULL;            \
                                                                         \
        original_##syscall_name =                                        \
            (__libc_##syscall_name)dlsym(RTLD_NEXT, #syscall_name);      \
        rc = (*original_##syscall_name)(pathname, statbuf);              \
        struct_name##_helper(pathname, statbuf);                         \
                                                                         \
        return rc;                                                       \
    }

WRAP_STAT(stat, stat)
WRAP_STAT(lstat, stat)
/* i686 arch */
WRAP_STAT(stat64, stat64)
WRAP_STAT(lstat64, stat64)

#define WRAP_XSTAT(syscall_name)                                           \
    typedef int (*__libc_##syscall_name)(int ver,                          \
                                         const char *pathname,             \
                                         struct stat *statbuf);            \
    int syscall_name(int ver, const char *pathname, struct stat *statbuf); \
    int syscall_name(int ver, const char *pathname, struct stat *statbuf)  \
    {                                                                      \
        int rc;                                                            \
        __libc_##syscall_name original_##syscall_name = NULL;              \
                                                                           \
        original_##syscall_name =                                          \
            (__libc_##syscall_name)dlsym(RTLD_NEXT, #syscall_name);        \
        rc = (*original_##syscall_name)(ver, pathname, statbuf);           \
        stat_helper(pathname, statbuf);                                    \
                                                                           \
        return rc;                                                         \
    }

WRAP_XSTAT(__xstat) /* CentOS8 */
WRAP_XSTAT(__lxstat)

/* i686 arch (likely not wrappable) */
static void
statx_helper(const char *pathname, struct statx *statbuf)
{
    if (file.name != NULL && strcmp(pathname, file.name) == 0) {
        statbuf->stx_uid = file.uid;
        statbuf->stx_gid = file.gid;
    }
}

typedef int (*__libc_statx)(int dirfd,
                            const char *pathname,
                            int flags,
                            unsigned int mask,
                            struct statx *statbuf);
int statx(int dirfd,
          const char *pathname,
          int flags,
          unsigned int mask,
          struct statx *statbuf);
int
statx(int dirfd,
      const char *pathname,
      int flags,
      unsigned int mask,
      struct statx *statbuf)
{
    int rc;
    __libc_statx original_statx = NULL;

    original_statx = (__libc_statx)dlsym(RTLD_NEXT, "statx");
    rc = (*original_statx)(dirfd, pathname, flags, mask, statbuf);
    statx_helper(pathname, statbuf);

    return rc;
}

static int is_file_blocked(const char *pathname)
{
    if (pathname == NULL) {
        return 0;
    }

    static const char *blocked_files[] = {
        /* Block for torture_gssapi_server_key_exchange_null */
        "/etc/ssh/ssh_host_ecdsa_key",
        "/etc/ssh/ssh_host_rsa_key",
        "/etc/ssh/ssh_host_ed25519_key",
    };

    for (size_t i = 0; i < sizeof(blocked_files) / sizeof(blocked_files[0]);
         i++) {
        if (strcmp(pathname, blocked_files[i]) == 0) {
            errno = ENOENT; /* No such file or directory */
            return 1;
        }
    }
    return 0;
}

#define WRAP_FOPEN(func_name)                                                 \
    FILE *func_name(const char *pathname, const char *mode)                   \
    {                                                                         \
        typedef FILE *(*orig_func_t)(const char *pathname, const char *mode); \
        static orig_func_t orig_func = NULL;                                  \
        if (orig_func == NULL) {                                              \
            orig_func = (orig_func_t)dlsym(RTLD_NEXT, #func_name);            \
        }                                                                     \
        if (is_file_blocked(pathname)) {                                      \
            return NULL;                                                      \
        }                                                                     \
        return orig_func(pathname, mode);                                     \
    }

WRAP_FOPEN(fopen)
WRAP_FOPEN(fopen64)
