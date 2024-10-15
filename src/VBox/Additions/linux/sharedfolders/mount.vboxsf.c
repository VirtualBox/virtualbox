/* $Id: mount.vboxsf.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions for Linux - mount(8) helper.
 *
 * Parses options provided by mount (or user directly)
 * Packs them into struct vbsfmount and passes to mount(2)
 * Optionally adds entries to mtab
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

/* #define DEBUG */
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <getopt.h>
#include <mntent.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <mntent.h>
#include <limits.h>
#include <iconv.h>
#include <sys/utsname.h>
#include <linux/version.h>

#include "vbsfmount.h"

#include <iprt/assertcompile.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define PANIC_ATTR __attribute ((noreturn, __format__ (__printf__, 1, 2)))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct vbsf_mount_opts
{
    unsigned long fFlags; /**< MS_XXX */

    /** @name Preformatted option=value or empty if not specified.
     * Helps eliminate duplicate options as well as simplifying concatting.
     * @{ */
    char          szTTL[32];
    char          szMsDirCacheTTL[32];
    char          szMsInodeTTL[32];
    char          szMaxIoPages[32];
    char          szDirBuf[32];
    char          szCacheMode[32];
    char          szUid[32];
    char          szGid[32];
    char          szDMode[32];
    char          szFMode[32];
    char          szDMask[32];
    char          szFMask[32];
    char          szIoCharset[32];
    /** @} */

    bool          fSloppy;
    char         *pszConvertCp;
};


static void PANIC_ATTR
panic(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

static void PANIC_ATTR
panic_err(const char *fmt, ...)
{
    va_list ap;
    int errno_code = errno;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno_code));
    exit(EXIT_FAILURE);
}

static int
safe_atoi(const char *s, size_t size, int base)
{
    char *endptr;
    long long int val = strtoll(s, &endptr, base);

    if (   val < INT_MIN
        || (   val > INT_MAX
            && (base != 8 || val != UINT_MAX) ) /* hack for printf("%o", -1) - 037777777777 */
        || endptr < s + size)
    {
        errno = ERANGE;
        panic_err("could not convert %.*s to integer, result = %lld (%d)",
                  (int)size, s, val, (int)val);
    }
    return (int)val;
}

static unsigned
safe_atoiu(const char *s, size_t size, int base)
{
    char *endptr;
    long long int val = strtoll(s, &endptr, base);

    if (   val < 0
        || val > UINT_MAX
        || endptr < s + size)
    {
        errno = ERANGE;
        panic_err("could not convert %.*s to unsigned integer, result = %lld (%#llx)",
                  (int)size, s, val, val);
    }
    return (unsigned)val;
}

static void
process_mount_opts(const char *s, struct vbsf_mount_opts *opts)
{
    const char *next = s;
    size_t len;
    typedef enum handler_opt
    {
        HO_RW,
        HO_RO,
        HO_UID,
        HO_GID,
        HO_TTL,
        HO_DENTRY_TTL,
        HO_INODE_TTL,
        HO_MAX_IO_PAGES,
        HO_DIR_BUF,
        HO_CACHE,
        HO_DMODE,
        HO_FMODE,
        HO_UMASK,
        HO_DMASK,
        HO_FMASK,
        HO_IOCHARSET,
        HO_NLS,
        HO_CONVERTCP,
        HO_NOEXEC,
        HO_EXEC,
        HO_NODEV,
        HO_DEV,
        HO_NOSUID,
        HO_SUID,
        HO_REMOUNT,
        HO_NOAUTO,
        HO_NIGNORE
    } handler_opt;
    struct
    {
        const char *name;
        handler_opt opt;
        int has_arg;
        const char *desc;
    } handlers[] =
    {
        {"rw",          HO_RW,              0, "mount read write (default)"},
        {"ro",          HO_RO,              0, "mount read only"},
        {"uid",         HO_UID,             1, "default file owner user id"},
        {"gid",         HO_GID,             1, "default file owner group id"},
        {"ttl",         HO_TTL,             1, "time to live for dentries & inode info"},
        {"dcachettl",   HO_DENTRY_TTL,      1, "time to live for dentries"},
        {"inodettl",    HO_INODE_TTL,       1, "time to live for inode info"},
        {"maxiopages",  HO_MAX_IO_PAGES,    1, "max buffer size for I/O with host"},
        {"dirbuf",      HO_DIR_BUF,         1, "directory buffer size (0 for default)"},
        {"cache",       HO_CACHE,           1, "cache mode: none, strict (default), read, readwrite"},
        {"iocharset",   HO_IOCHARSET,       1, "i/o charset (default utf8)"},
        {"nls",         HO_NLS,             1, "i/o charset (default utf8)"},
        {"convertcp",   HO_CONVERTCP,       1, "convert share name from given charset to utf8"},
        {"dmode",       HO_DMODE,           1, "mode of all directories"},
        {"fmode",       HO_FMODE,           1, "mode of all regular files"},
        {"umask",       HO_UMASK,           1, "umask of directories and regular files"},
        {"dmask",       HO_DMASK,           1, "umask of directories"},
        {"fmask",       HO_FMASK,           1, "umask of regular files"},
        {"noexec",      HO_NOEXEC,          0, NULL}, /* don't document these options directly here */
        {"exec",        HO_EXEC,            0, NULL}, /* as they are well known and described in the */
        {"nodev",       HO_NODEV,           0, NULL}, /* usual manpages */
        {"dev",         HO_DEV,             0, NULL},
        {"nosuid",      HO_NOSUID,          0, NULL},
        {"suid",        HO_SUID,            0, NULL},
        {"remount",     HO_REMOUNT,         0, NULL},
        {"noauto",      HO_NOAUTO,          0, NULL},
        {"_netdev",     HO_NIGNORE,         0, NULL},
        {"relatime",    HO_NIGNORE,         0, NULL},
        {NULL,          0,                  0, NULL}
    }, *handler;

    while (next)
    {
        const char *val;
        size_t key_len, val_len;

        s = next;
        next = strchr(s, ',');
        if (!next)
        {
            len = strlen(s);
        }
        else
        {
            len = next - s;
            next += 1;
            if (!*next)
                next = 0;
        }

        val = NULL;
        val_len = 0;
        for (key_len = 0; key_len < len; ++key_len)
        {
            if (s[key_len] == '=')
            {
                if (key_len + 1 < len)
                {
                    val = s + key_len + 1;
                    val_len = len - key_len - 1;
                }
                break;
            }
        }

        for (handler = handlers; handler->name; ++handler)
        {
            size_t j;
            for (j = 0; j < key_len && handler->name[j] == s[j]; ++j)
                ;

            if (j == key_len && !handler->name[j])
            {
                if (handler->has_arg)
                {
                    if (!(val && *val))
                    {
                        panic("%.*s requires an argument (i.e. %.*s=<arg>)\n",
                              (int)len, s, (int)len, s);
                    }
                }

                switch (handler->opt)
                {
                    case HO_RW:
                        opts->fFlags &= ~MS_RDONLY;
                        break;
                    case HO_RO:
                        opts->fFlags |= MS_RDONLY;
                        break;
                    case HO_NOEXEC:
                        opts->fFlags |= MS_NOEXEC;
                        break;
                    case HO_EXEC:
                        opts->fFlags &= ~MS_NOEXEC;
                        break;
                    case HO_NODEV:
                        opts->fFlags |= MS_NODEV;
                        break;
                    case HO_DEV:
                        opts->fFlags &= ~MS_NODEV;
                        break;
                    case HO_NOSUID:
                        opts->fFlags |= MS_NOSUID;
                        break;
                    case HO_SUID:
                        opts->fFlags &= ~MS_NOSUID;
                        break;
                    case HO_REMOUNT:
                        opts->fFlags |= MS_REMOUNT;
                        break;
                    case HO_TTL:
                        snprintf(opts->szTTL, sizeof(opts->szTTL),
                                 "ttl=%d", safe_atoi(val, val_len, 10));
                        break;
                    case HO_DENTRY_TTL:
                        snprintf(opts->szMsDirCacheTTL, sizeof(opts->szMsDirCacheTTL),
                                 "dcachettl=%d", safe_atoi(val, val_len, 10));
                        break;
                    case HO_INODE_TTL:
                        snprintf(opts->szMsInodeTTL, sizeof(opts->szMsInodeTTL),
                                 "inodettl=%d", safe_atoi(val, val_len, 10));
                        break;
                    case HO_MAX_IO_PAGES:
                        snprintf(opts->szMaxIoPages, sizeof(opts->szMaxIoPages),
                                 "maxiopages=%d", safe_atoiu(val, val_len, 10));
                        break;
                    case HO_DIR_BUF:
                        snprintf(opts->szDirBuf, sizeof(opts->szDirBuf),
                                 "dirbuf=%d", safe_atoiu(val, val_len, 10));
                        break;
                    case HO_CACHE:
#define IS_EQUAL(a_sz) (val_len == sizeof(a_sz) - 1U && strncmp(val, a_sz, sizeof(a_sz) - 1U) == 0)
                        if (IS_EQUAL("default"))
                            strcpy(opts->szCacheMode, "cache=default");
                        else if (IS_EQUAL("none"))
                            strcpy(opts->szCacheMode, "cache=none");
                        else if (IS_EQUAL("strict"))
                            strcpy(opts->szCacheMode, "cache=strict");
                        else if (IS_EQUAL("read"))
                            strcpy(opts->szCacheMode, "cache=read");
                        else if (IS_EQUAL("readwrite"))
                            strcpy(opts->szCacheMode, "cache=readwrite");
                        else
                            panic("invalid cache mode '%.*s'\n"
                                  "Valid cache modes are: default, none, strict, read, readwrite\n",
                                  (int)val_len, val);
                        break;
                    case HO_UID:
                        /** @todo convert string to id. */
                        snprintf(opts->szUid, sizeof(opts->szUid),
                                 "uid=%d", safe_atoi(val, val_len, 10));
                        break;
                    case HO_GID:
                        /** @todo convert string to id. */
                        snprintf(opts->szGid, sizeof(opts->szGid),
                                 "gid=%d", safe_atoi(val, val_len, 10));
                        break;
                    case HO_DMODE:
                        snprintf(opts->szDMode, sizeof(opts->szDMode),
                                 "dmode=0%o", safe_atoi(val, val_len, 8));
                        break;
                    case HO_FMODE:
                        snprintf(opts->szFMode, sizeof(opts->szFMode),
                                 "fmode=0%o", safe_atoi(val, val_len, 8));
                        break;
                    case HO_UMASK:
                    {
                        int fMask = safe_atoi(val, val_len, 8);
                        snprintf(opts->szDMask, sizeof(opts->szDMask), "dmask=0%o", fMask);
                        snprintf(opts->szFMask, sizeof(opts->szFMask), "fmask=0%o", fMask);
                        break;
                    }
                    case HO_DMASK:
                        snprintf(opts->szDMask, sizeof(opts->szDMask),
                                 "dmask=0%o", safe_atoi(val, val_len, 8));
                        break;
                    case HO_FMASK:
                        snprintf(opts->szFMask, sizeof(opts->szFMask),
                                 "fmask=0%o", safe_atoi(val, val_len, 8));
                        break;
                    case HO_IOCHARSET:
                    case HO_NLS:
                        if (val_len >= MAX_NLS_NAME)
                            panic("the character set name for I/O is too long: %*.*s\n", (int)val_len, (int)val_len, val);
                        snprintf(opts->szIoCharset, sizeof(opts->szIoCharset),
                                 "%s=%*.*s", handler->opt == HO_IOCHARSET ? "iocharset" : "nls", (int)val_len, (int)val_len, val);
                        break;
                    case HO_CONVERTCP:
                        opts->pszConvertCp = malloc(val_len + 1);
                        if (!opts->pszConvertCp)
                            panic_err("could not allocate memory");
                        memcpy(opts->pszConvertCp, val, val_len);
                        opts->pszConvertCp[val_len] = '\0';
                        break;
                    case HO_NOAUTO:
                    case HO_NIGNORE:
                        break;
                }
                break;
            }
            continue;
        }

        if (   !handler->name
            && !opts->fSloppy)
        {
            fprintf(stderr, "unknown mount option `%.*s'\n", (int)len, s);
            fprintf(stderr, "valid options:\n");

            for (handler = handlers; handler->name; ++handler)
            {
                if (handler->desc)
                    fprintf(stderr, "  %-10s%s %s\n", handler->name,
                            handler->has_arg ? "=<arg>" : "", handler->desc);
            }
            exit(EXIT_FAILURE);
        }
    }
}

/** Appends @a pszOptVal to pszOpts if not empty. */
static size_t append_option(char *pszOpts, size_t cbOpts, size_t offOpts, const char *pszOptVal)
{
    if (*pszOptVal != '\0')
    {
        size_t cchOptVal = strlen(pszOptVal);
        if (offOpts + (offOpts > 0) + cchOptVal < cbOpts)
        {
            if (offOpts)
                pszOpts[offOpts++] = ',';
            memcpy(&pszOpts[offOpts], pszOptVal, cchOptVal);
            offOpts += cchOptVal;
            pszOpts[offOpts] = '\0';
        }
        else
            panic("Too many options!");
    }
    return offOpts;
}

static void
convertcp(char *in_codeset, char *pszSharedFolder, char *pszDst)
{
    char *i = pszSharedFolder;
    char *o = pszDst;
    size_t ib = strlen(pszSharedFolder);
    size_t ob = MAX_HOST_NAME - 1;
    iconv_t cd;

    cd = iconv_open("UTF-8", in_codeset);
    if (cd == (iconv_t)-1)
    {
        panic_err("could not convert share name, iconv_open `%s' failed",
                   in_codeset);
    }

    while (ib)
    {
        size_t c = iconv(cd, &i, &ib, &o, &ob);
        if (c == (size_t)-1)
        {
            panic_err("could not convert share name(%s) at %d",
                      pszSharedFolder, (int)(strlen(pszSharedFolder) - ib));
        }
    }
    *o = 0;
}


/**
  * Print out a usage message and exit.
  *
  * @returns 1
  * @param   argv0      The name of the application
  */
static int usage(char *argv0)
{
    printf("Usage: %s [OPTIONS] NAME MOUNTPOINT\n"
           "Mount the VirtualBox shared folder NAME from the host system to MOUNTPOINT.\n"
           "\n"
           "  -w                    mount the shared folder writable (the default)\n"
           "  -r                    mount the shared folder read-only\n"
           "  -n                    do not create an mtab entry\n"
           "  -s                    sloppy parsing, ignore unrecognized mount options\n"
           "  -o OPTION[,OPTION...] use the mount options specified\n"
           "\n", argv0);
    printf("Available mount options are:\n"
           "     rw                 mount writable (the default)\n"
           "     ro                 mount read only\n"
           "     uid=UID            set the default file owner user id to UID\n"
           "     gid=GID            set the default file owner group id to GID\n");
    printf("     ttl=MILLIESECSONDS set the \"time to live\" for both the directory cache\n"
           "                        and inode info.  -1 for kernel default, 0 disables it.\n"
           "     dcachettl=MILLIES  set the \"time to live\" for the directory cache,\n"
           "                        overriding the 'ttl' option.  Ignored if negative.\n"
           "     inodettl=MILLIES   set the \"time to live\" for the inode information,\n"
           "                        overriding the 'ttl' option.  Ignored if negative.\n");
    printf("     maxiopages=PAGES   set the max host I/O buffers size in pages. Uses\n"
           "                        default if zero.\n"
           "     dirbuf=BYTES       set the directory enumeration buffer size in bytes.\n"
           "                        Uses default size if zero.\n");
    printf("     cache=MODE         set the caching mode for the mount.  Allowed values:\n"
           "                          default: use the kernel default (strict)\n"
           "                             none: no caching; may experience guest side\n"
           "                                   coherence issues between mmap and read.\n");
    printf("                           strict: no caching, except for writably mapped\n"
           "                                   files (for guest side coherence)\n"
           "                             read: read via the page cache; host changes\n"
           "                                   may be completely ignored\n");
    printf("                        readwrite: read and write via the page cache; host\n"
           "                                   changes may be completely ignored and\n"
           "                                   guest changes takes a while to reach the host\n");
    printf("     dmode=MODE         override the mode of all directories to (octal) MODE\n"
           "     fmode=MODE         override the mode of all regular files to (octal) MODE\n"
           "     umask=UMASK        set the umask to (octal) UMASK\n");
    printf("     dmask=UMASK        set the umask applied to directories only\n"
           "     fmask=UMASK        set the umask applied to regular files only\n"
           "     iocharset CHARSET  use the character set CHARSET for I/O operations\n"
           "                        (default set is utf8)\n"
           "     convertcp CHARSET  convert the folder name from CHARSET to utf8\n"
           "\n");
    printf("Less common used options:\n"
           "     noexec,exec,nodev,dev,nosuid,suid\n");
    return EXIT_FAILURE;
}

int
main(int argc, char **argv)
{
    int c;
    int err;
    int saved_errno;
    int nomtab = 0;
    char *pszSharedFolder;
    char *pszMountPoint;
    struct utsname uts;
    int major, minor, patch;
    size_t offOpts;
    static const char s_szSfNameOpt[] = "sf_name=";
    char szSharedFolderIconved[sizeof(s_szSfNameOpt) - 1 + MAX_HOST_NAME];
    char *pszOpts = NULL;
    struct vbsf_mount_opts opts =
    {
        MS_NODEV,
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        false, /*fSloppy*/
        NULL,
    };

    AssertCompile(sizeof(uid_t) == sizeof(int));
    AssertCompile(sizeof(gid_t) == sizeof(int));

    if (getuid())
        panic("Only root can mount shared folders from the host.\n");

    if (!argv[0])
        argv[0] = "mount.vboxsf";

    long cbOpts = sysconf(_SC_PAGESIZE);
    if (cbOpts == -1)
        panic("Couldn't query system page size errno=%d.\n", errno);

    pszOpts = alloca(cbOpts);
    if (!pszOpts)
        panic("Couldn't allocate memory for mount options buffers\n");

    /*
     * Parse options.
     */
    while ((c = getopt(argc, argv, "rwsno:h")) != -1)
    {
        switch (c)
        {
            default:
                fprintf(stderr, "unknown option `%c:%#x'\n", c, c);
                RT_FALL_THRU();
            case '?':
            case 'h':
                return usage(argv[0]);

            case 'r':
                opts.fFlags |= MS_RDONLY;
                break;

            case 'w':
                opts.fFlags &= ~MS_RDONLY;
                break;

            case 's':
                opts.fSloppy = true;
                break;

            case 'o':
                process_mount_opts(optarg, &opts);
                break;

            case 'n':
                nomtab = 1;
                break;
        }
    }

    if (argc - optind < 2)
        return usage(argv[0]);

    pszSharedFolder = argv[optind];
    pszMountPoint   = argv[optind + 1];
    if (opts.pszConvertCp)
    {
        convertcp(opts.pszConvertCp, pszSharedFolder, &szSharedFolderIconved[sizeof(s_szSfNameOpt) - 1]);
        pszSharedFolder = &szSharedFolderIconved[sizeof(s_szSfNameOpt) - 1];
    }

    /*
     * Concat option strings.
     */
    offOpts   = 0;
    pszOpts[0] = '\0';
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szTTL);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szMsDirCacheTTL);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szMsInodeTTL);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szMaxIoPages);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szDirBuf);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szCacheMode);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szUid);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szGid);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szDMode);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szFMode);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szDMask);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szFMask);
    offOpts = append_option(pszOpts, cbOpts, offOpts, opts.szIoCharset);

    /* For pre-2.6 kernels we have to supply the shared folder name as a
       string option because the kernel hides the device name from us. */
    RT_ZERO(uts);
    if (   uname(&uts) == -1
        || sscanf(uts.release, "%d.%d.%d", &major, &minor, &patch) != 3)
        major = minor = patch = 5;

    if (KERNEL_VERSION(major, minor, patch) < KERNEL_VERSION(2,6,0))
    {
        memcpy(szSharedFolderIconved, s_szSfNameOpt, sizeof(s_szSfNameOpt) - 1);
        if (!opts.pszConvertCp)
        {
            if (strlen(pszSharedFolder) >= MAX_HOST_NAME)
                panic("%s: shared folder name is too long (max %d)", argv[0], (int)MAX_HOST_NAME - 1);
            strcpy(&szSharedFolderIconved[sizeof(s_szSfNameOpt) - 1], pszSharedFolder);
        }
        offOpts = append_option(pszOpts, cbOpts, offOpts, szSharedFolderIconved);
    }

    /*
     * Do the actual mounting.
     */
    err = mount(pszSharedFolder, pszMountPoint, "vboxsf", opts.fFlags, pszOpts);
    saved_errno = errno;

    if (err)
    {
        if (saved_errno == ENXIO)
            panic("%s: shared folder '%s' was not found (check VM settings / spelling)\n", argv[0], pszSharedFolder);
        else
            panic_err("%s: mounting failed with the error", argv[0]);
    }

    if (!nomtab)
    {
        err = vbsfmount_complete(pszSharedFolder, pszMountPoint, opts.fFlags, pszOpts);
        switch (err)
        {
            case 0: /* Success. */
                break;

            case 1:
                panic_err("%s: Could not update mount table (out of memory).", argv[0]);
                break;

            case 2:
                panic_err("%s: Could not open mount table for update.", argv[0]);
                break;

            case 3:
                /* panic_err("%s: Could not add an entry to the mount table.", argv[0]); */
                break;

            default:
                panic_err("%s: Unknown error while completing mount operation: %d", argv[0], err);
                break;
        }
    }

    exit(EXIT_SUCCESS);
}

