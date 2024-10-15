/* $Id: vboximg-mount.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboximg-mount - Disk Image Flattening FUSE Program.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/

#define LOG_GROUP LOG_GROUP_DEFAULT /** @todo log group */

#define RTTIME_INCL_TIMESPEC
#define FUSE_USE_VERSION 27
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
#   define UNIX_DERIVATIVE
#endif
#define MAX_READERS (INT32_MAX / 32)
#ifdef UNIX_DERIVATIVE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <math.h>
#include <cstdarg>
#include <sys/stat.h>
#include <sys/time.h>
#endif
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX)
# include <sys/param.h>
# undef PVM     /* Blasted old BSD mess still hanging around darwin. */
#endif
#ifdef RT_OS_LINUX
# include <linux/fs.h>
# include <linux/hdreg.h>
#endif
#include <VirtualBox_XPCOM.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/vd.h>
#include <VBox/vd-ifs.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/errorprint.h>
#include <VBox/vd-plugin.h>
#include <VBox/version.h> /* VBOX_PUEL_PRODUCT */
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/message.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/types.h>
#include <iprt/path.h>
#include <iprt/utf16.h>
#include <iprt/base64.h>
#include <iprt/vfs.h>
#include <iprt/dvm.h>
#include <iprt/time.h>

#include "fuse.h"
#include "vboximgCrypto.h"
#include "vboximgMedia.h"
#include "SelfSizingTable.h"
#include "vboximgOpts.h"

using namespace com;

enum {
     USAGE_FLAG,
};

#if !defined(S_ISTXT) && defined(S_ISVTX)
# define S_ISTXT (S_ISVTX)
#endif

#define VBOX_EXTPACK                VBOX_PUEL_PRODUCT
#define VERBOSE                     g_vboximgOpts.fVerbose

#define SAFENULL(strPtr)   (strPtr ? strPtr : "")
#define CSTR(arg)     Utf8Str(arg).c_str()          /* Converts XPCOM string type to C string type */

static struct fuse_operations g_vboximgOps;         /** FUSE structure that defines allowed ops for this FS */

/**
 * Volume data.
 */
typedef struct VBOXIMGMOUNTVOL
{
    /** The volume handle. */
    RTDVMVOLUME                 hVol;
    /** The VFS file associated with the volume. */
    RTVFSFILE                   hVfsFileVol;
    /** Handle to the VFS root if supported and specified. */
    RTVFS                       hVfsRoot;
    /** Handle to the root directory. */
    RTVFSDIR                    hVfsDirRoot;
} VBOXIMGMOUNTVOL;
/** Pointer to a volume data structure. */
typedef VBOXIMGMOUNTVOL *PVBOXIMGMOUNTVOL;

/* Global variables */
static RTVFSFILE             g_hVfsFileDisk = NIL_RTVFSFILE;        /** Disk as VFS file handle. */
static uint32_t              g_cbSector;                            /** Disk sector size. */
static RTDVM                 g_hDvmMgr;                             /** Handle to the volume manager. */
static char                 *g_pszDiskUuid;                         /** UUID of image (if known, otherwise NULL) */
static PVDINTERFACE          g_pVdIfs;                              /** @todo Remove when VD I/O becomes threadsafe */
static VDINTERFACETHREADSYNC g_VDIfThreadSync;                      /** @todo Remove when VD I/O becomes threadsafe */
static RTCRITSECT            g_vdioLock;                            /** @todo Remove when VD I/O becomes threadsafe */
static char                 *g_pszImageName = NULL;                 /** Base filename for current VD image */
static char                 *g_pszImagePath;                        /** Full path to current VD image */
static char                 *g_pszBaseImagePath;                    /** Base image known after parsing */
static char                 *g_pszBaseImageName;                    /** Base image known after parsing */
static uint32_t              g_cImages;                             /** Number of images in diff chain */

/** Pointer to the detected volumes. */
static PVBOXIMGMOUNTVOL      g_paVolumes;
/** Number of detected volumes. */
static uint32_t              g_cVolumes;

VBOXIMGOPTS g_vboximgOpts;

#define OPTION(fmt, pos, val) { fmt, offsetof(struct vboximgOpts, pos), val }

static struct fuse_opt vboximgOptDefs[] = {
    OPTION("--image %s",         pszImageUuidOrPath,   0),
    OPTION("-i %s",              pszImageUuidOrPath,   0),
    OPTION("--rw",               fRW,                  1),
    OPTION("--root",             fAllowRoot,           1),
    OPTION("--vm %s",            pszVm,                0),
    OPTION("-l",                 fList,                1),
    OPTION("--list",             fList,                1),
    OPTION("-g",                 fGstFs,               1),
    OPTION("--guest-filesystem", fGstFs,               1),
    OPTION("--verbose",          fVerbose,             1),
    OPTION("-v",                 fVerbose,             1),
    OPTION("--wide",             fWide,                1),
    OPTION("-w",                 fWide,                1),
    OPTION("-lv",                fVerboseList,         1),
    OPTION("-vl",                fVerboseList,         1),
    OPTION("-lw",                fWideList,            1),
    OPTION("-wl",                fWideList,            1),
    OPTION("-h",                 fBriefUsage,          1),
    FUSE_OPT_KEY("--help",       USAGE_FLAG),
    FUSE_OPT_KEY("-vm",          FUSE_OPT_KEY_NONOPT),
    FUSE_OPT_END
};

typedef struct IMAGELIST
{
    struct IMAGELIST *next;
    struct IMAGELIST *prev;
    ComPtr<IToken> pLockToken;
    bool   fWriteable;
    ComPtr<IMedium> pImage;
    Bstr   pImageName;
    Bstr   pImagePath;
} IMAGELIST;

IMAGELIST listHeadLockList;  /* flink & blink intentionally left NULL */



/** @todo Remove when VD I/O becomes threadsafe */
static DECLCALLBACK(int) vboximgThreadStartRead(void *pvUser)
{
    PRTCRITSECT vdioLock = (PRTCRITSECT)pvUser;
    return RTCritSectEnter(vdioLock);
}

static DECLCALLBACK(int) vboximgThreadFinishRead(void *pvUser)
{
    PRTCRITSECT vdioLock = (PRTCRITSECT)pvUser;
    return RTCritSectLeave(vdioLock);
}

static DECLCALLBACK(int) vboximgThreadStartWrite(void *pvUser)
{
    PRTCRITSECT vdioLock = (PRTCRITSECT)pvUser;
    return RTCritSectEnter(vdioLock);
}

static DECLCALLBACK(int) vboximgThreadFinishWrite(void *pvUser)
{
    PRTCRITSECT vdioLock = (PRTCRITSECT)pvUser;
    return RTCritSectLeave(vdioLock);
}
/** @todo (end of to do section) */


static void
briefUsage()
{
    RTPrintf("usage: vboximg-mount [options] <mount point directory path>\n\n"
        "vboximg-mount options:\n\n"
        "  [ { -i | --image } <specifier> ]   VirtualBox disk base image or snapshot,\n"
        "                                     specified by UUID or path\n"
        "\n"
        "  [ { -l | --list } ]                If --image specified, list its partitions,\n"
        "                                     otherwise, list registered VMs and their\n"
        "                                     attached virtual HDD disk media. In verbose\n"
        "                                     mode, VM/media list will be long format,\n"
        "                                     i.e. including snapshot images and paths.\n"
        "\n"
        "  [ { -w | --wide } ]                List media in wide / tabular format\n"
        "                                     (reduces vertical scrolling but requires\n"
        "                                     wider than standard 80 column window)\n"
        "\n"
        "  [ { -g | --guest-filesystem } ]    Exposes supported guest filesystems directly\n"
        "                                     in the mounted directory without the need\n"
        "                                     for a filesystem driver on the host\n"
        "\n"
        "  [ --vm UUID ]                      Restrict media list to specified vm.\n"
        "\n"
        "  [ --rw ]                           Make image writeable (default = readonly)\n"
        "\n"
        "  [ --root ]                         Same as -o allow_root.\n"
        "\n"
        "  [ { -v | --verbose } ]             Log extra information.\n"
        "\n"
        "  [ -o opt[,opt...]]                 FUSE mount options.\n"
        "\n"
        "  [ { --help | -h | -? } ]           Display this usage information.\n"
    );
    RTPrintf("\n"
      "vboximg-mount is a utility to make VirtualBox disk images available to the host\n"
      "operating system for privileged or non-privileged access. Any version of the\n"
      "disk can be mounted from its available history of snapshots.\n"
      "\n"
      "If the user specifies a base image identifier using the --image option, only\n"
      "the base image will be mounted, disregarding any snapshots. Alternatively,\n"
      "if a snapshot is specified, the state of the FUSE-mounted virtual disk\n"
      "is synthesized from the implied chain of snapshots, including the base image.\n"
      "\n"
      "The virtual disk is exposed as a device node within a FUSE-based filesystem\n"
      "that overlays the user-provided mount point. The FUSE filesystem consists of a\n"
      "directory containing a number of files and possibly other directories:\n"
      "    * vhdd:      Provides access to the raw disk image data as a flat image\n"
      "    * vol<id>:   Provides access to individual volumes on the accessed disk image\n"
      "    * fs<id>:    Provides access to a supported filesystem without the need for a\n"
      "                 host filesystem driver\n"
      "\n"
      "The directory will also contain a symbolic link which has the same basename(1)\n"
      "as the virtual disk base image and points to the location of the\n"
      "virtual disk base image.\n"
      "\n"
    );
}

static int
vboximgOptHandler(void *data, const char *arg, int optKey, struct fuse_args *outargs)
{
    RT_NOREF(data);
    RT_NOREF(arg);
    RT_NOREF(optKey);
    RT_NOREF(outargs);

    /*
     * Apparently this handler is only called for arguments FUSE can't parse,
     * and arguments that don't result in variable assignment such as "USAGE"
     * In this impl. that's always deemed a parsing error.
     */
    if (*arg != '-') /* could be user's mount point */
        return 1;

    return -1;
}


/**
 * Queries the VFS object handle from the given path.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if the object denoted by the path couldn't be found.
 * @param   pszPath             The path.
 * @param   phVfsObj            Where to store the handle to the VFS object on success.
 */
static int vboxImgMntVfsObjQueryFromPath(const char *pszPath, PRTVFSOBJ phVfsObj)
{
    PRTPATHSPLIT pPathSplit = NULL;
    int rc = RTPathSplitA(pszPath, &pPathSplit, RTPATH_STR_F_STYLE_HOST);
    if (RT_SUCCESS(rc))
    {
        if (   RTPATH_PROP_HAS_ROOT_SPEC(pPathSplit->fProps)
            && pPathSplit->cComps >= 2)
        {
            /* Skip the root specifier and start with the component coming afterwards. */
            if (   !RTStrCmp(pPathSplit->apszComps[1], "vhdd")
                && g_hVfsFileDisk != NIL_RTVFSFILE)
                *phVfsObj = RTVfsObjFromFile(g_hVfsFileDisk);
            else if (!RTStrNCmp(pPathSplit->apszComps[1], "vol", sizeof("vol") - 1))
            {
                /* Retrieve the accessed volume and return the stat data. */
                uint32_t idxVol;
                int vrc = RTStrToUInt32Full(&pPathSplit->apszComps[1][3], 10, &idxVol);
                if (   vrc == VINF_SUCCESS
                    && idxVol < g_cVolumes
                    && g_paVolumes[idxVol].hVfsFileVol != NIL_RTVFSFILE)
                    *phVfsObj = RTVfsObjFromFile(g_paVolumes[idxVol].hVfsFileVol);
                else
                    rc = VERR_NOT_FOUND;
            }
            else if (!RTStrNCmp(pPathSplit->apszComps[1], "fs", sizeof("fs") - 1))
            {
                /* Retrieve the accessed volume and return the stat data. */
                uint32_t idxVol;
                int vrc = RTStrToUInt32Full(&pPathSplit->apszComps[1][2], 10, &idxVol);
                if (   vrc == VINF_SUCCESS
                    && idxVol < g_cVolumes
                    && g_paVolumes[idxVol].hVfsDirRoot != NIL_RTVFSDIR)
                    *phVfsObj = RTVfsObjFromDir(g_paVolumes[idxVol].hVfsDirRoot);
                else
                    rc = VERR_NOT_FOUND;

                /* Is an object inside the guest filesystem requested? */
                if (pPathSplit->cComps > 2)
                {
                    PRTPATHSPLIT pPathSplitVfs = (PRTPATHSPLIT)RTMemTmpAllocZ(RT_UOFFSETOF_DYN(RTPATHSPLIT, apszComps[pPathSplit->cComps - 1]));
                    if (RT_LIKELY(pPathSplitVfs))
                    {
                        pPathSplitVfs->cComps       = pPathSplit->cComps - 1;
                        pPathSplitVfs->fProps       = pPathSplit->fProps;
                        pPathSplitVfs->cchPath      = pPathSplit->cchPath - strlen(pPathSplit->apszComps[1]) - 1;
                        pPathSplitVfs->cbNeeded     = pPathSplit->cbNeeded;
                        pPathSplitVfs->pszSuffix    = pPathSplit->pszSuffix;
                        pPathSplitVfs->apszComps[0] = pPathSplit->apszComps[0];
                        for (uint32_t i = 1; i < pPathSplitVfs->cComps; i++)
                            pPathSplitVfs->apszComps[i] = pPathSplit->apszComps[i + 1];

                        /* Reassemble the path. */
                        char *pszPathVfs = (char *)RTMemTmpAllocZ(pPathSplitVfs->cbNeeded);
                        if (RT_LIKELY(pszPathVfs))
                        {
                            rc = RTPathSplitReassemble(pPathSplitVfs, RTPATH_STR_F_STYLE_HOST, pszPathVfs, pPathSplitVfs->cbNeeded);
                            if (RT_SUCCESS(rc))
                            {
                                rc = RTVfsObjOpen(g_paVolumes[idxVol].hVfsRoot, pszPathVfs,
                                                  RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                                  RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING | RTPATH_F_ON_LINK,
                                                  phVfsObj);
                            }
                            RTMemTmpFree(pszPathVfs);
                        }

                        RTMemTmpFree(pPathSplitVfs);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VERR_NOT_FOUND;

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NOT_FOUND;
        RTPathSplitFree(pPathSplit);
    }

    return rc;
}


/** @copydoc fuse_operations::open */
static int vboximgOp_open(const char *pszPath, struct fuse_file_info *pInfo)
{
    LogFlowFunc(("pszPath=%s\n", pszPath));
    int rc = 0;

    RTVFSOBJ hVfsObj;
    int vrc = vboxImgMntVfsObjQueryFromPath(pszPath, &hVfsObj);
    if (RT_SUCCESS(vrc))
    {
        uint32_t fNotSup = 0;

#ifdef UNIX_DERIVATIVE
# ifdef RT_OS_DARWIN
        fNotSup = O_APPEND | O_NONBLOCK | O_SYMLINK | O_NOCTTY | O_SHLOCK | O_EXLOCK |
                  O_ASYNC  | O_CREAT    | O_TRUNC   | O_EXCL | O_EVTONLY;
# elif defined(RT_OS_LINUX)
        fNotSup = O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK;
                  /* | O_LARGEFILE | O_SYNC | ? */
# elif defined(RT_OS_FREEBSD)
        fNotSup = O_APPEND | O_ASYNC | O_DIRECT | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK;
                  /* | O_LARGEFILE | O_SYNC | ? */
# endif
#else
#   error "Port me"
#endif

        if (!(pInfo->flags & fNotSup))
        {
#ifdef UNIX_DERIVATIVE
            if ((pInfo->flags & O_ACCMODE) == O_ACCMODE)
                rc = -EINVAL;
# ifdef O_DIRECTORY
            if (pInfo->flags & O_DIRECTORY)
                rc = -ENOTDIR;
# endif
#endif
            if (!rc)
            {
                pInfo->fh = (uintptr_t)hVfsObj;
                return 0;
            }
        }
        else
            rc = -EINVAL;

        RTVfsObjRelease(hVfsObj);
    }
    else
        rc = -RTErrConvertToErrno(vrc);

    LogFlowFunc(("rc=%d \"%s\"\n", rc, pszPath));
    return rc;

}

/** @copydoc fuse_operations::release */
static int vboximgOp_release(const char *pszPath, struct fuse_file_info *pInfo)
{
    RT_NOREF(pszPath);

    LogFlowFunc(("pszPath=%s\n", pszPath));

    RTVFSOBJ hVfsObj = (RTVFSOBJ)(uintptr_t)pInfo->fh;
    RTVfsObjRelease(hVfsObj);

    LogFlowFunc(("\"%s\"\n", pszPath));
    return 0;
}


/** @copydoc fuse_operations::read */
static int vboximgOp_read(const char *pszPath, char *pbBuf, size_t cbBuf,
                          off_t offset, struct fuse_file_info *pInfo)
{
    RT_NOREF(pszPath);

    LogFlowFunc(("offset=%#llx size=%#zx path=\"%s\"\n", (uint64_t)offset, cbBuf, pszPath));

    AssertReturn(offset >= 0, -EINVAL);
    AssertReturn((int)cbBuf >= 0, -EINVAL);
    AssertReturn((unsigned)cbBuf == cbBuf, -EINVAL);

    int rc = 0;
    RTVFSOBJ hVfsObj = (RTVFSOBJ)(uintptr_t)pInfo->fh;
    switch (RTVfsObjGetType(hVfsObj))
    {
        case RTVFSOBJTYPE_FILE:
        {
            size_t cbRead = 0;
            RTVFSFILE hVfsFile = RTVfsObjToFile(hVfsObj);
            int vrc = RTVfsFileReadAt(hVfsFile, offset, pbBuf, cbBuf, &cbRead);
            if (cbRead)
                rc = cbRead;
            else if (vrc == VINF_EOF)
                rc = -RTErrConvertToErrno(VERR_EOF);
            RTVfsFileRelease(hVfsFile);
            break;
        }
        default:
            rc = -EINVAL;
    }

    if (rc < 0)
        LogFlowFunc(("%s\n", strerror(rc)));
    return rc;
}

/** @copydoc fuse_operations::write */
static int vboximgOp_write(const char *pszPath, const char *pbBuf, size_t cbBuf,
                           off_t offset, struct fuse_file_info *pInfo)
{
    RT_NOREF(pszPath);
    RT_NOREF(pInfo);

    LogFlowFunc(("offset=%#llx size=%#zx path=\"%s\"\n", (uint64_t)offset, cbBuf, pszPath));

    AssertReturn(offset >= 0, -EINVAL);
    AssertReturn((int)cbBuf >= 0, -EINVAL);
    AssertReturn((unsigned)cbBuf == cbBuf, -EINVAL);

    if (!g_vboximgOpts.fRW)
    {
        LogFlowFunc(("WARNING: vboximg-mount (FUSE FS) --rw option not specified\n"
                     "              (write operation ignored w/o error!)\n"));
        return cbBuf;
    }

    int rc = 0;
    RTVFSOBJ hVfsObj = (RTVFSOBJ)(uintptr_t)pInfo->fh;
    switch (RTVfsObjGetType(hVfsObj))
    {
        case RTVFSOBJTYPE_FILE:
        {
            size_t cbWritten = 0;
            RTVFSFILE hVfsFile = RTVfsObjToFile(hVfsObj);
            int vrc = RTVfsFileWriteAt(hVfsFile, offset, pbBuf, cbBuf, &cbWritten);
            if (cbWritten)
                rc = cbWritten;
            else if (vrc == VINF_EOF)
                rc = -RTErrConvertToErrno(VERR_EOF);
            RTVfsFileRelease(hVfsFile);
            break;
        }
        default:
            rc = -EINVAL;
    }

    if (rc < 0)
        LogFlowFunc(("%s\n", strerror(rc)));

    return rc;
}

/** @copydoc fuse_operations::getattr */
static int vboximgOp_getattr(const char *pszPath, struct stat *stbuf)
{
    int rc = 0;

    LogFlowFunc(("pszPath=%s, stat(\"%s\")\n", pszPath, g_pszImagePath));

    memset(stbuf, 0, sizeof(struct stat));

    if (RTStrCmp(pszPath, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else if (   g_pszImageName
             && RTStrNCmp(pszPath + 1, g_pszImageName, strlen(g_pszImageName)) == 0)
    {
        /* When the disk is partitioned, the symbolic link named from `basename` of
         * resolved path to VBox disk image, has appended to it formatted text
         * representing the offset range of the partition.
         *
         *  $ vboximg-mount -i /stroll/along/the/path/simple_fixed_disk.vdi -p 1 /mnt/tmpdir
         *  $ ls /mnt/tmpdir
         *  simple_fixed_disk.vdi[20480:2013244928]    vhdd
         */
        rc = stat(g_pszImagePath, stbuf);
        if (rc < 0)
            return rc;
        stbuf->st_size = 0;
        stbuf->st_mode = S_IFLNK | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_uid = 0;
        stbuf->st_gid = 0;
    }
    else
    {
        /* Query the VFS object and fill in the data. */
        RTVFSOBJ hVfsObj = NIL_RTVFSOBJ;
        int vrc = vboxImgMntVfsObjQueryFromPath(pszPath, &hVfsObj);
        if (RT_SUCCESS(vrc))
        {
            RTFSOBJINFO ObjInfo;

            vrc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_UNIX);
            if (RT_SUCCESS(vrc))
            {
                stbuf->st_size  = ObjInfo.cbObject;
                stbuf->st_nlink = 1;
                stbuf->st_uid   = 0;
                stbuf->st_gid   = 0;

#ifdef RT_OS_DARWIN
                RTTimeSpecGetTimespec(&ObjInfo.AccessTime, &stbuf->st_atimespec);
                RTTimeSpecGetTimespec(&ObjInfo.ModificationTime, &stbuf->st_mtimespec);
                RTTimeSpecGetTimespec(&ObjInfo.ChangeTime, &stbuf->st_ctimespec);
                RTTimeSpecGetTimespec(&ObjInfo.BirthTime, &stbuf->st_birthtimespec);
#else
                RTTimeSpecGetTimespec(&ObjInfo.AccessTime, &stbuf->st_atim);
                RTTimeSpecGetTimespec(&ObjInfo.ModificationTime, &stbuf->st_mtim);
                RTTimeSpecGetTimespec(&ObjInfo.ChangeTime, &stbuf->st_ctim);
#endif

                switch (ObjInfo.Attr.fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_FIFO:
                    {
                        stbuf->st_mode = S_IFIFO;
                        break;
                    }
                    case RTFS_TYPE_DEV_CHAR:
                    {
                        stbuf->st_mode = S_IFCHR;
                        break;
                    }
                    case RTFS_TYPE_DIRECTORY:
                    {
                        stbuf->st_mode = S_IFDIR;
                        stbuf->st_nlink = 2;
                        break;
                    }
                    case RTFS_TYPE_DEV_BLOCK:
                    {
                        stbuf->st_mode = S_IFBLK;
                        break;
                    }
                    case RTFS_TYPE_FILE:
                    {
                        stbuf->st_mode = S_IFREG;
                        break;
                    }
                    case RTFS_TYPE_SYMLINK:
                    {
                        stbuf->st_mode = S_IFLNK;
                        break;
                    }
                    case RTFS_TYPE_SOCKET:
                    {
                        stbuf->st_mode = S_IFSOCK;
                        break;
                    }
#if 0 /* Not existing on Linux. */
                    case RTFS_TYPE_WHITEOUT:
                    {
                        stbuf->st_mode = S_IFWHT;
                        break;
                    }
#endif
                    default:
                        stbuf->st_mode = 0;
                }

                if (ObjInfo.Attr.fMode & RTFS_UNIX_ISUID)
                    stbuf->st_mode |= S_ISUID;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_ISGID)
                    stbuf->st_mode |= S_ISGID;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_ISTXT)
                    stbuf->st_mode |= S_ISTXT;

                /* Owner permissions. */
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IRUSR)
                    stbuf->st_mode |= S_IRUSR;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IWUSR)
                    stbuf->st_mode |= S_IWUSR;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IXUSR)
                    stbuf->st_mode |= S_IXUSR;

                /* Group permissions. */
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IRGRP)
                    stbuf->st_mode |= S_IRGRP;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IWGRP)
                    stbuf->st_mode |= S_IWGRP;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IXGRP)
                    stbuf->st_mode |= S_IXGRP;

                /* Other permissions. */
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IROTH)
                    stbuf->st_mode |= S_IROTH;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IWOTH)
                    stbuf->st_mode |= S_IWOTH;
                if (ObjInfo.Attr.fMode & RTFS_UNIX_IXOTH)
                    stbuf->st_mode |= S_IXOTH;

                if (ObjInfo.Attr.enmAdditional == RTFSOBJATTRADD_UNIX)
                {
                    stbuf->st_uid   = ObjInfo.Attr.u.Unix.uid;
                    stbuf->st_gid   = ObjInfo.Attr.u.Unix.gid;
                    stbuf->st_nlink = ObjInfo.Attr.u.Unix.cHardlinks;
                    stbuf->st_ino   = ObjInfo.Attr.u.Unix.INodeId;
                    stbuf->st_dev   = ObjInfo.Attr.u.Unix.INodeIdDevice;
                    /*stbuf->st_flags = ObjInfo.Attr.u.Unix.fFlags;*/       /* Not existing on Linux. */
                    /*stbuf->st_gen   = ObjInfo.Attr.u.Unix.GenerationId;*/ /* Not existing on Linux. */
                    stbuf->st_rdev  = ObjInfo.Attr.u.Unix.Device;
                }
            }

            RTVfsObjRelease(hVfsObj);
        }
        else if (vrc == VERR_NOT_FOUND)
            rc = -ENOENT;
        else
            rc = -RTErrConvertToErrno(vrc);
    }

    return rc;
}

/** @copydoc fuse_operations::readdir */
static int vboximgOp_readdir(const char *pszPath, void *pvBuf, fuse_fill_dir_t pfnFiller,
                             off_t offset, struct fuse_file_info *pInfo)
{
    RT_NOREF(offset);
    RT_NOREF(pInfo);

    int rc = 0;

    /* Special root directory handling?. */
    if (!RTStrCmp(pszPath, "/"))
    {
        /*
         *  mandatory '.', '..', ...
         */
        pfnFiller(pvBuf, ".", NULL, 0);
        pfnFiller(pvBuf, "..", NULL, 0);

        if (g_pszImageName)
        {
            /*
             * Create FUSE FS dir entry that is depicted here (and exposed via stat()) as
             * a symbolic link back to the resolved path to the VBox virtual disk image,
             * whose symlink name is basename that path. This is a convenience so anyone
             * listing the dir can figure out easily what the vhdd FUSE node entry
             * represents.
             */
            pfnFiller(pvBuf, g_pszImageName, NULL, 0);
        }

        if (g_hVfsFileDisk != NIL_RTVFSFILE)
        {
            /*
             * Create entry named "vhdd" denoting the whole disk, which getattr() will describe as a
             * regular file, and thus will go through the open/release/read/write vectors
             * to access the VirtualBox image as processed by the IRPT VD API.
             */
            pfnFiller(pvBuf, "vhdd", NULL, 0);
        }

        /* Create entries for the individual volumes. */
        for (uint32_t i = 0; i < g_cVolumes; i++)
        {
            char tmp[64];
            if (g_paVolumes[i].hVfsFileVol != NIL_RTVFSFILE)
            {
                RTStrPrintf(tmp, sizeof (tmp), "vol%u", i);
                pfnFiller(pvBuf, tmp, NULL, 0);
            }

            if (g_paVolumes[i].hVfsRoot != NIL_RTVFS)
            {
                RTStrPrintf(tmp, sizeof (tmp), "fs%u", i);
                pfnFiller(pvBuf, tmp, NULL, 0);
            }
        }
    }
    else
    {
        /* Query the VFS object and fill in the data. */
        RTVFSOBJ hVfsObj = NIL_RTVFSOBJ;
        int vrc = vboxImgMntVfsObjQueryFromPath(pszPath, &hVfsObj);
        if (RT_SUCCESS(vrc))
        {
            switch (RTVfsObjGetType(hVfsObj))
            {
                case RTVFSOBJTYPE_DIR:
                {
                    RTVFSDIR hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTDIRENTRYEX DirEntry;

                    vrc = RTVfsDirRewind(hVfsDir); AssertRC(vrc);
                    vrc = RTVfsDirReadEx(hVfsDir, &DirEntry, NULL, RTFSOBJATTRADD_NOTHING);
                    while (RT_SUCCESS(vrc))
                    {
                        pfnFiller(pvBuf, DirEntry.szName, NULL, 0);
                        vrc = RTVfsDirReadEx(hVfsDir, &DirEntry, NULL, RTFSOBJATTRADD_NOTHING);
                    }

                    RTVfsDirRelease(hVfsDir);
                    break;
                }
                default:
                    rc = -EINVAL;
            }

            RTVfsObjRelease(hVfsObj);
        }
        else
            rc = -RTErrConvertToErrno(vrc);
    }

    return rc;
}

/** @copydoc fuse_operations::readlink */
static int vboximgOp_readlink(const char *pszPath, char *buf, size_t size)
{
    RT_NOREF(pszPath);
    RTStrCopy(buf, size, g_pszImagePath);
    return 0;
}


/**
 * Displays the list of volumes on the opened image.
 */
static void vboxImgMntVolumesDisplay(void)
{
    /*
     * Partition table is most readable and concise when headers and columns
     * are adapted to the actual data, to avoid insufficient or excessive whitespace.
     */

    RTPrintf( "Virtual disk image:\n\n");
    RTPrintf("   Base: %s\n", g_pszBaseImagePath);
    if (g_cImages > 1)
        RTPrintf("   Diff: %s\n", g_pszImagePath);
    if (g_pszDiskUuid)
            RTPrintf("   UUID: %s\n\n", g_pszDiskUuid);

    SELFSIZINGTABLE tbl(2);

    void *colPartition = tbl.addCol("Partition",    "%s(%d)",     -1);
    void *colBoot      = tbl.addCol("Boot",         "%c   ",     1);
    void *colStart     = tbl.addCol("Start",        "%lld",      1);
    void *colSectors   = tbl.addCol("Sectors",      "%lld",     -1, 2);
    void *colSize      = tbl.addCol("Size",         "%s",        1);
    void *colOffset    = tbl.addCol("Offset",       "%lld",      1);
    void *colType      = tbl.addCol("Type",         "%s",       -1, 2);

    for (uint32_t i = 0; i < g_cVolumes; i++)
    {
        PVBOXIMGMOUNTVOL pVol = &g_paVolumes[i];
        uint64_t fVolFlags = RTDvmVolumeGetFlags(pVol->hVol);
        uint64_t cbVol = RTDvmVolumeGetSize(pVol->hVol);
        RTDVMVOLTYPE enmType = RTDvmVolumeGetType(pVol->hVol);
        uint64_t offStart = 0;
        uint64_t offEnd = 0;

        if (fVolFlags & DVMVOLUME_F_CONTIGUOUS)
        {
            int rc = RTDvmVolumeQueryRange(pVol->hVol, &offStart, &offEnd);
            AssertRC(rc);
        }

        void *row = tbl.addRow();
        tbl.setCell(row, colPartition,  g_pszBaseImageName, i);
        tbl.setCell(row, colBoot,       (fVolFlags & DVMVOLUME_FLAGS_BOOTABLE) ? '*' : ' ');
        tbl.setCell(row, colStart,      offStart / g_cbSector);
        tbl.setCell(row, colSectors,    cbVol / g_cbSector);
        tbl.setCell(row, colSize,       vboximgScaledSize(cbVol));
        tbl.setCell(row, colOffset,     offStart);
        tbl.setCell(row, colType,       RTDvmVolumeTypeGetDescr(enmType));
    }
    tbl.displayTable();
    RTPrintf ("\n");
}


/**
 * Sets up the volumes for the disk.
 *
 * @returns IPRT status code.
 */
static int vboxImgMntVolumesSetup(void)
{
    g_cVolumes = 0;
    g_paVolumes = NULL;

    int rc = RTDvmCreate(&g_hDvmMgr, g_hVfsFileDisk, g_cbSector, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        rc = RTDvmMapOpen(g_hDvmMgr);
        if (RT_SUCCESS(rc))
        {
            g_cVolumes = RTDvmMapGetValidVolumes(g_hDvmMgr);
            if (   g_cVolumes != UINT32_MAX
                && g_cVolumes > 0)
            {
                g_paVolumes = (PVBOXIMGMOUNTVOL)RTMemAllocZ(g_cVolumes * sizeof(VBOXIMGMOUNTVOL));
                if (RT_LIKELY(g_paVolumes))
                {
                    g_paVolumes[0].hVfsRoot = NIL_RTVFS;

                    rc = RTDvmMapQueryFirstVolume(g_hDvmMgr, &g_paVolumes[0].hVol);
                    if (RT_SUCCESS(rc))
                        rc = RTDvmVolumeCreateVfsFile(g_paVolumes[0].hVol,
                                                      RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_READWRITE,
                                                      &g_paVolumes[0].hVfsFileVol);

                    for (uint32_t i = 1; i < g_cVolumes && RT_SUCCESS(rc); i++)
                    {
                        g_paVolumes[i].hVfsRoot = NIL_RTVFS;
                        rc = RTDvmMapQueryNextVolume(g_hDvmMgr, g_paVolumes[i-1].hVol, &g_paVolumes[i].hVol);
                        if (RT_SUCCESS(rc))
                            rc = RTDvmVolumeCreateVfsFile(g_paVolumes[i].hVol,
                                                          RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_READWRITE,
                                                          &g_paVolumes[i].hVfsFileVol);
                    }

                    if (RT_SUCCESS(rc))
                        return VINF_SUCCESS;

                    RTMemFree(g_paVolumes);
                    g_paVolumes = NULL;
                    g_cVolumes  = 0;
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else if (g_cVolumes == UINT32_MAX)
            {
                g_cVolumes = 0;
                rc = VERR_INTERNAL_ERROR;
            }

            RTDvmRelease(g_hDvmMgr);
        }
        else if (rc == VERR_NOT_FOUND)
            rc = VINF_SUCCESS;
    }

    return rc;
}


static int vboxImgMntImageSetup(struct fuse_args *args)
{
    /*
     * Initialize COM.
     */
    using namespace com;
    HRESULT hrc = com::Initialize();
    if (FAILED(hrc))
    {
# ifdef VBOX_WITH_XPCOM
        if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
        {
            char szHome[RTPATH_MAX] = "";
            com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                   "Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
        }
# endif
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to initialize COM! (hrc=%Rhrc)", hrc);
    }

    /*
     * Get the remote VirtualBox object and create a local session object.
     */
    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> pVirtualBox;

    hrc = pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (SUCCEEDED(hrc))
        hrc = pVirtualBoxClient->COMGETTER(VirtualBox)(pVirtualBox.asOutParam());

    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to get IVirtualBox object! (hrc=%Rhrc)", hrc);

    if (g_vboximgOpts.fList && g_vboximgOpts.pszImageUuidOrPath == NULL)
    {
        vboximgListVMs(pVirtualBox);
        return VINF_SUCCESS;
    }

    if (!g_vboximgOpts.pszImageUuidOrPath)
        return RTMsgErrorExitFailure("A image UUID or path needs to be provided using the --image/-i option\n");

    Bstr    pMediumUuid;
    ComPtr<IMedium> pVDiskMedium = NULL;
    char   *pszFormat;
    VDTYPE  enmType;

    /*
     * Open chain of images from what is provided on command line, to base image
     */
    if (g_vboximgOpts.pszImageUuidOrPath)
    {
        /* compiler was too fussy about access mode's data type in conditional expr, so... */
        if (g_vboximgOpts.fRW)
            CHECK_ERROR(pVirtualBox, OpenMedium(Bstr(g_vboximgOpts.pszImageUuidOrPath).raw(), DeviceType_HardDisk,
                AccessMode_ReadWrite, false /* forceNewUuid */, pVDiskMedium.asOutParam()));

        else
            CHECK_ERROR(pVirtualBox, OpenMedium(Bstr(g_vboximgOpts.pszImageUuidOrPath).raw(), DeviceType_HardDisk,
                AccessMode_ReadOnly, false /* forceNewUuid */, pVDiskMedium.asOutParam()));

        if (FAILED(hrc))
            return RTMsgErrorExitFailure("\nCouldn't find specified VirtualBox base or snapshot disk image:\n%s",
                 g_vboximgOpts.pszImageUuidOrPath);


        CHECK_ERROR(pVDiskMedium, COMGETTER(Id)(pMediumUuid.asOutParam()));
        g_pszDiskUuid = RTStrDup((char *)CSTR(pMediumUuid));

        /*
         * Lock & cache the disk image media chain (from leaf to base).
         * Only leaf can be rw (and only if media is being mounted in non-default writable (rw) mode)
         *
         * Note: Failure to acquire lock is intentionally fatal (e.g. program termination)
         */

        if (VERBOSE)
            RTPrintf("\nAttempting to lock medium chain from leaf image to base image\n");

        bool fLeaf = true;
        g_cImages = 0;

        do
        {
            ++g_cImages;
            IMAGELIST *pNewEntry= new IMAGELIST();
            pNewEntry->pImage = pVDiskMedium;
            CHECK_ERROR(pVDiskMedium, COMGETTER(Name)((pNewEntry->pImageName).asOutParam()));
            CHECK_ERROR(pVDiskMedium, COMGETTER(Location)((pNewEntry->pImagePath).asOutParam()));

            if (VERBOSE)
                RTPrintf("  %s", CSTR(pNewEntry->pImageName));

            if (fLeaf && g_vboximgOpts.fRW)
            {
                if (VERBOSE)
                    RTPrintf(" ... Locking for write\n");
                CHECK_ERROR_RET(pVDiskMedium, LockWrite((pNewEntry->pLockToken).asOutParam()), hrc);
                pNewEntry->fWriteable = true;
            }
            else
            {
                if (VERBOSE)
                    RTPrintf(" ... Locking for read\n");
                CHECK_ERROR_RET(pVDiskMedium, LockRead((pNewEntry->pLockToken).asOutParam()), hrc);
            }

            IMAGELIST *pCurImageEntry = &listHeadLockList;
            while (pCurImageEntry->next)
                pCurImageEntry = pCurImageEntry->next;
            pCurImageEntry->next = pNewEntry;
            pNewEntry->prev = pCurImageEntry;
            listHeadLockList.prev = pNewEntry;

            CHECK_ERROR(pVDiskMedium, COMGETTER(Parent)(pVDiskMedium.asOutParam()));
            fLeaf = false;
        }
        while(pVDiskMedium);
    }

    ComPtr<IMedium> pVDiskBaseMedium = listHeadLockList.prev->pImage;
    Bstr pVDiskBaseImagePath = listHeadLockList.prev->pImagePath;
    Bstr pVDiskBaseImageName = listHeadLockList.prev->pImageName;

    g_pszBaseImagePath = RTStrDup((char *)CSTR(pVDiskBaseImagePath));
    g_pszBaseImageName = RTStrDup((char *)CSTR(pVDiskBaseImageName));

    g_pszImagePath = RTStrDup((char *)CSTR(listHeadLockList.next->pImagePath));
    g_pszImageName = RTStrDup((char *)CSTR(listHeadLockList.next->pImageName));

    /*
     * Attempt to VDOpen media (base and any snapshots), handling encryption,
     * if that property is set for base media
     */
    Bstr pBase64EncodedKeyStore;

    hrc = pVDiskBaseMedium->GetProperty(Bstr("CRYPT/KeyStore").raw(), pBase64EncodedKeyStore.asOutParam());
    if (SUCCEEDED(hrc) && strlen(CSTR(pBase64EncodedKeyStore)) != 0)
    {
        RTPrintf("\nvboximgMount: Encrypted disks not supported in this version\n\n");
        return -1;
    }


/* ***************** BEGIN IFDEF'D (STUBBED-OUT) CODE ************** */
/* vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv */

#if 0 /* The following encrypted disk related code is stubbed out until it can be finished.
       * What is here is an attempt to port the VBoxSVC specific code in i_openForIO to
       * a client's proximity. It is supplemented by code in vboximgCrypto.cpp and
       * vboximageCrypt.h that was lifed from SecretKeyStore.cpp along with the setup
       * task function.
       *
       * The ultimate solution may be to use a simpler but less efficient COM interface,
       * or to use VD encryption interfaces and key containers entirely. The keystore
       * handling/filter approach that is here may be a bumbling hybrid approach
       * that is broken (trying to bridge incompatible disk encryption mechanisms) or otherwise
       * doesn't make sense. */

    Bstr pKeyId;
    ComPtr<IExtPackManager> pExtPackManager;
    ComPtr<IExtPack> pExtPack;
    com::SafeIfaceArray<IExtPackPlugIn> pExtPackPlugIns;

    if (SUCCEEDED(rc))
    {
        RTPrintf("Got GetProperty(\"CRYPT/KeyStore\") = %s\n", CSTR(pBase64EncodedKeyStore));
        if (strlen(CSTR(pBase64EncodedKeyStore)) == 0)
            return RTMsgErrorExitFailure("Image '%s' is configured for encryption but "
                    "there is no key store to retrieve the password from", CSTR(pVDiskBaseImageName));

        SecretKeyStore keyStore(false);
        RTBase64Decode(CSTR(pBase64EncodedKeyStore), &keyStore, sizeof (SecretKeyStore), NULL, NULL);

        rc = pVDiskBaseMedium->GetProperty(Bstr("CRYPT/KeyId").raw(), pKeyId.asOutParam());
        if (SUCCEEDED(rc) && strlen(CSTR(pKeyId)) == 0)
            return RTMsgErrorExitFailure("Image '%s' is configured for encryption but "
                "doesn't have a key identifier set", CSTR(pVDiskBaseImageName));

        RTPrintf("        key id: %s\n", CSTR(pKeyId));

#ifndef VBOX_WITH_EXTPACK
        return RTMsgErrorExitFailure(
            "Encryption is not supported because extension pack support is not built in");
#endif

        CHECK_ERROR(pVirtualBox, COMGETTER(ExtensionPackManager)(pExtPackManager.asOutParam()));
        BOOL fExtPackUsable;
        CHECK_ERROR(pExtPackManager, IsExtPackUsable((PRUnichar *)VBOX_EXTPACK, &fExtPackUsable));
        if (fExtPackUsable)
        {
            /* Load the PlugIn */

            CHECK_ERROR(pExtPackManager, Find((PRUnichar *)VBOX_EXTPACK, pExtPack.asOutParam()));
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure(
                    "Encryption is not supported because the extension pack '%s' is missing",
                    VBOX_EXTPACK);

            CHECK_ERROR(pExtPack, COMGETTER(PlugIns)(ComSafeArrayAsOutParam(pExtPackPlugIns)));

            Bstr pPlugInPath;
            size_t iPlugIn;
            for (iPlugIn = 0; iPlugIn < pExtPackPlugIns.size(); iPlugIn++)
            {
                Bstr pPlugInName;
                CHECK_ERROR(pExtPackPlugIns[iPlugIn], COMGETTER(Name)(pPlugInName.asOutParam()));
                if (RTStrCmp(CSTR(pPlugInName), "VDPlugInCrypt") == 0)
                {
                    CHECK_ERROR(pExtPackPlugIns[iPlugIn], COMGETTER(ModulePath)(pPlugInPath.asOutParam()));
                    break;
                }
            }
            if (iPlugIn == pExtPackPlugIns.size())
                return RTMsgErrorExitFailure("Encryption is not supported because the extension pack '%s' "
                    "is missing the encryption PlugIn (old extension pack installed?)", VBOX_EXTPACK);

            rc = VDPluginLoadFromFilename(CSTR(pPlugInPath));
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure("Retrieving encryption settings of the image failed "
                    "because the encryption PlugIn could not be loaded\n");
        }

        SecretKey *pKey = NULL;
        rc = keyStore.retainSecretKey(Utf8Str(pKeyId), &pKey);
        if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure(
                    "Failed to retrieve the secret key with ID \"%s\" from the store (%Rrc)",
                    CSTR(pKeyId), rc);

        VDISKCRYPTOSETTINGS vdiskCryptoSettings, *pVDiskCryptoSettings = &vdiskCryptoSettings;

        vboxImageCryptoSetup(pVDiskCryptoSettings, NULL,
            (const char *)CSTR(pBase64EncodedKeyStore), (const char *)pKey->getKeyBuffer(), false);

        rc = VDFilterAdd(g_pVDisk, "CRYPT", VD_FILTER_FLAGS_DEFAULT, pVDiskCryptoSettings->vdFilterIfaces);
        keyStore.releaseSecretKey(Utf8Str(pKeyId));

        if (rc == VERR_VD_PASSWORD_INCORRECT)
            return RTMsgErrorExitFailure("The password to decrypt the image is incorrect");

        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("Failed to load the decryption filter: %Rrc", rc);
    }
#endif

/* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
/* **************** END IFDEF'D (STUBBED-OUT) CODE ***************** */

    int vrc = RTCritSectInit(&g_vdioLock);
    if (RT_SUCCESS(vrc))
    {
        g_VDIfThreadSync.pfnStartRead   = vboximgThreadStartRead;
        g_VDIfThreadSync.pfnFinishRead  = vboximgThreadFinishRead;
        g_VDIfThreadSync.pfnStartWrite  = vboximgThreadStartWrite;
        g_VDIfThreadSync.pfnFinishWrite = vboximgThreadFinishWrite;
        vrc = VDInterfaceAdd(&g_VDIfThreadSync.Core, "vboximg_ThreadSync", VDINTERFACETYPE_THREADSYNC,
                             &g_vdioLock, sizeof(VDINTERFACETHREADSYNC), &g_pVdIfs);
    }
    else
        return RTMsgErrorExitFailure("ERROR: Failed to create critsects "
                                     "for virtual disk I/O, rc=%Rrc\n", vrc);

   /*
     * Create HDD container to open base image and differencing images into
     */
    vrc = VDGetFormat(NULL /* pVDIIfsDisk */, NULL /* pVDIIfsImage*/,
                      CSTR(pVDiskBaseImagePath), VDTYPE_INVALID, &pszFormat, &enmType);

    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure("VDGetFormat(,,%s,,) "
            "failed (during HDD container creation), rc=%Rrc\n", g_pszImagePath, vrc);

    if (VERBOSE)
        RTPrintf("\nCreating container for base image of format %s\n", pszFormat);

    PVDISK pVDisk = NULL;
    vrc = VDCreate(g_pVdIfs, enmType, &pVDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure("ERROR: Couldn't create virtual disk container\n");

    /* Open all virtual disk media from leaf snapshot (if any) to base image*/

    if (VERBOSE)
        RTPrintf("\nOpening medium chain\n");

    IMAGELIST *pCurMedium = listHeadLockList.prev;  /* point to base image */
    while (pCurMedium != &listHeadLockList)
    {
        if (VERBOSE)
            RTPrintf("  Open: %s\n", CSTR(pCurMedium->pImagePath));

        vrc = VDOpen(pVDisk,
                     pszFormat,
                     CSTR(pCurMedium->pImagePath),
                     pCurMedium->fWriteable ? 0 : VD_OPEN_FLAGS_READONLY,
                     g_pVdIfs);

        if (RT_FAILURE(vrc))
            return RTMsgErrorExitFailure("Could not open the medium storage unit '%s' %Rrc",
                                         CSTR(pCurMedium->pImagePath), vrc);

        pCurMedium = pCurMedium->prev;
    }

    RTStrFree(pszFormat);

    /* Create the VFS file to use for the disk image access. */
    vrc = VDCreateVfsFileFromDisk(pVDisk, VD_VFSFILE_DESTROY_ON_RELEASE, &g_hVfsFileDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure("Error creating VFS file wrapper for disk image\n");

    g_cbSector = VDGetSectorSize(pVDisk, VD_LAST_IMAGE);

    vrc = vboxImgMntVolumesSetup();
    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure("Error parsing volumes on disk\n");

    if (g_vboximgOpts.fList)
    {
        if (g_hVfsFileDisk == NIL_RTVFSFILE)
            return RTMsgErrorExitFailure("No valid --image to list partitions from\n");

        RTPrintf("\n");
        vboxImgMntVolumesDisplay();
        return VINF_SUCCESS; /** @todo r=andy Re-visit this. */
    }

    /* Try to "mount" supported filesystems inside the disk image if specified. */
    if (g_vboximgOpts.fGstFs)
    {
        for (uint32_t i = 0; i < g_cVolumes; i++)
        {
            vrc = RTVfsMountVol(g_paVolumes[i].hVfsFileVol,
                               g_vboximgOpts.fRW ? 0 : RTVFSMNT_F_READ_ONLY,
                               &g_paVolumes[i].hVfsRoot,
                               NULL);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTVfsOpenRoot(g_paVolumes[i].hVfsRoot, &g_paVolumes[i].hVfsDirRoot);
                if (RT_FAILURE(vrc))
                {
                    RTPrintf("\nvboximg-mount: Failed to access filesystem on volume %u, ignoring\n", i);
                    RTVfsRelease(g_paVolumes[i].hVfsRoot);
                    g_paVolumes[i].hVfsRoot = NIL_RTVFS;
                }
            }
            else
                RTPrintf("\nvboximg-mount: Failed to access filesystem on volume %u, ignoring\n", i);
        }
    }

    /*
     * Hand control over to libfuse.
     */
    if (VERBOSE)
        RTPrintf("\nvboximg-mount: Going into background...\n");

    int rc = fuse_main_real(args->argc, args->argv, &g_vboximgOps, sizeof(g_vboximgOps), NULL);
    RTPrintf("vboximg-mount: fuse_main -> %d\n", rc);

    int rc2 = RTVfsFileRelease(g_hVfsFileDisk);
    AssertRC(rc2);

    return vrc;
}


int main(int argc, char **argv)
{

    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTR3InitExe failed, rc=%Rrc\n", rc);

    rc = VDInit();
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("VDInit failed, rc=%Rrc\n", rc);

    rc = RTFuseLoadLib();
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to load the fuse library, rc=%Rrc\n", rc);

    memset(&g_vboximgOps, 0, sizeof(g_vboximgOps));
    g_vboximgOps.open        = vboximgOp_open;
    g_vboximgOps.read        = vboximgOp_read;
    g_vboximgOps.write       = vboximgOp_write;
    g_vboximgOps.getattr     = vboximgOp_getattr;
    g_vboximgOps.release     = vboximgOp_release;
    g_vboximgOps.readdir     = vboximgOp_readdir;
    g_vboximgOps.readlink    = vboximgOp_readlink;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    memset(&g_vboximgOpts, 0, sizeof(g_vboximgOpts));

    rc = fuse_opt_parse(&args, &g_vboximgOpts, vboximgOptDefs, vboximgOptHandler);
    if (rc < 0 || argc < 2 || RTStrCmp(argv[1], "-?" ) == 0 || g_vboximgOpts.fBriefUsage)
    {
        briefUsage();
        return 0;
    }

    if (g_vboximgOpts.fAllowRoot)
        fuse_opt_add_arg(&args, "-oallow_root");

    /*
     * FUSE doesn't seem to like combining options with one hyphen, as traditional UNIX
     * command line utilities allow. The following flags, fWideList and fVerboseList,
     * and their respective option definitions give the appearance of combined opts,
     * so that -vl, -lv, -wl, -lw options are allowed, since those in particular would
     * tend to conveniently facilitate some of the most common use cases.
     */
    if (g_vboximgOpts.fWideList)
    {
        g_vboximgOpts.fWide = true;
        g_vboximgOpts.fList = true;
    }
    if (g_vboximgOpts.fVerboseList)
    {
        g_vboximgOpts.fVerbose = true;
        g_vboximgOpts.fList    = true;
    }
    if (g_vboximgOpts.fAllowRoot)
        fuse_opt_add_arg(&args, "-oallow_root");

    if (   !g_vboximgOpts.pszImageUuidOrPath
        || !RTVfsChainIsSpec(g_vboximgOpts.pszImageUuidOrPath))
        return vboxImgMntImageSetup(&args);

    /* Mount the VFS chain. */
    RTVFSOBJ hVfsObj;
    rc = RTVfsChainOpenObj(g_vboximgOpts.pszImageUuidOrPath, RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING | RTPATH_F_ON_LINK,
                           &hVfsObj, NULL, NULL);
    if (   RT_SUCCESS(rc)
        && RTVfsObjGetType(hVfsObj) == RTVFSOBJTYPE_VFS)
    {
        g_paVolumes = (PVBOXIMGMOUNTVOL)RTMemAllocZ(sizeof(*g_paVolumes));
        if (RT_LIKELY(g_paVolumes))
        {
            g_cVolumes = 1;
            g_paVolumes[0].hVfsRoot = RTVfsObjToVfs(hVfsObj);
            g_paVolumes[0].hVfsFileVol = NIL_RTVFSFILE;
            RTVfsObjRelease(hVfsObj);

            rc = RTVfsOpenRoot(g_paVolumes[0].hVfsRoot, &g_paVolumes[0].hVfsDirRoot);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Hand control over to libfuse.
                 */
                if (VERBOSE)
                    RTPrintf("\nvboximg-mount: Going into background...\n");

                rc = fuse_main_real(args.argc, args.argv, &g_vboximgOps, sizeof(g_vboximgOps), NULL);
                RTVfsDirRelease(g_paVolumes[0].hVfsDirRoot);
                RTVfsRelease(g_paVolumes[0].hVfsRoot);
            }

            RTMemFree(g_paVolumes);
            g_paVolumes = NULL;
            g_cVolumes  = 0;
        }
        else
            rc = VERR_NO_MEMORY;

        RTVfsObjRelease(hVfsObj);
    }

    return rc;
}

