/* $Id: dir-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory manipulation, POSIX.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DIR
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>

#include <iprt/dir.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "internal/dir.h"
#include "internal/fs.h"
#include "internal/path.h"

#if !defined(RT_OS_SOLARIS) && !defined(RT_OS_HAIKU)
# define HAVE_DIRENT_D_TYPE 1
#endif


RTDECL(bool) RTDirExists(const char *pszPath)
{
    bool fRc = false;
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !stat(pszNativePath, &s)
            && S_ISDIR(s.st_mode);

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTDirExists(%p={%s}): returns %RTbool\n", pszPath, pszPath, fRc));
    return fRc;
}


RTDECL(int) RTDirCreate(const char *pszPath, RTFMODE fMode, uint32_t fCreate)
{
    RT_NOREF_PV(fCreate);

    int rc;
    fMode = rtFsModeNormalize(fMode, pszPath, 0, RTFS_TYPE_DIRECTORY);
    if (rtFsModeIsValidPermissions(fMode))
    {
        char const *pszNativePath;
        rc = rtPathToNative(&pszNativePath, pszPath, NULL);
        if (RT_SUCCESS(rc))
        {
            struct stat st;
            if (mkdir(pszNativePath, fMode & RTFS_UNIX_MASK) == 0)
            {
                /* If requested, we try make use the permission bits are set
                   correctly when asked.  For now, we'll just ignore errors here. */
                if (fCreate & RTDIRCREATE_FLAGS_IGNORE_UMASK)
                {
                    if (   stat(pszNativePath, &st)
                        || (st.st_mode & 07777u) != (fMode & 07777u) )
                        chmod(pszNativePath, fMode & RTFS_UNIX_MASK);
                }
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = errno;
                /*
                 * Solaris mkdir returns ENOSYS on autofs directories, and also
                 * did this apparently for NFS mount points in some Nevada
                 * development builds. It also returned EACCES when it should
                 * have returned EEXIST, which actually is within the POSIX
                 * spec (not that I like this interpretation, but it seems
                 * valid). Check ourselves.
                 */
                if (    rc == ENOSYS
                    ||  rc == EACCES)
                {
                    rc = RTErrConvertFromErrno(rc);
                    if (!stat(pszNativePath, &st))
                        rc = VERR_ALREADY_EXISTS;
                }
                else
                    rc = RTErrConvertFromErrno(rc);
            }
        }

        rtPathFreeNative(pszNativePath, pszPath);
    }
    else
    {
        AssertMsgFailed(("Invalid file mode! %RTfmode\n", fMode));
        rc = VERR_INVALID_FMODE;
    }
    LogFlow(("RTDirCreate(%p={%s}, %RTfmode): returns %Rrc\n", pszPath, pszPath, fMode, rc));
    return rc;
}


RTDECL(int) RTDirRemove(const char *pszPath)
{
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        if (rmdir(pszNativePath))
        {
            rc = errno;
            if (rc == EEXIST) /* Solaris returns this, the rest have ENOTEMPTY. */
                rc = VERR_DIR_NOT_EMPTY;
            else if (rc != ENOTDIR)
                rc = RTErrConvertFromErrno(rc);
            else
            {
                /*
                 * This may be a valid path-not-found or it may be a non-directory in
                 * the final component.  FsPerf want us to distinguish between the two,
                 * and trailing slash shouldn't matter because it doesn't on windows...
                 */
                char       *pszFree = NULL;
                const char *pszStat = pszNativePath;
                size_t      cch     = strlen(pszNativePath);
                if (cch > 2 && RTPATH_IS_SLASH(pszNativePath[cch - 1]))
                {
                    pszFree = (char *)RTMemTmpAlloc(cch);
                    if (pszFree)
                    {
                        memcpy(pszFree, pszNativePath, cch);
                        do
                            pszFree[--cch] = '\0';
                        while (cch > 2 && RTPATH_IS_SLASH(pszFree[cch - 1]));
                        pszStat = pszFree;
                    }
                }

                struct stat st;
                if (!stat(pszStat, &st) && !S_ISDIR(st.st_mode))
                    rc = VERR_NOT_A_DIRECTORY;
                else
                    rc = VERR_PATH_NOT_FOUND;

                if (pszFree)
                    RTMemTmpFree(pszFree);
            }
        }

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTDirRemove(%p={%s}): returns %Rrc\n", pszPath, pszPath, rc));
    return rc;
}


RTDECL(int) RTDirFlush(const char *pszPath)
{
    /*
     * Linux: The fsync() man page hints at this being required for ensuring
     * consistency between directory and file in case of a crash.
     *
     * Solaris: No mentioned is made of directories on the fsync man page.
     * While rename+fsync will do what we want on ZFS, the code needs more
     * careful studying wrt whether the directory entry of a new file is
     * implicitly synced when the file is synced (it's very likely for ZFS).
     *
     * FreeBSD: The FFS fsync code seems to flush the directory entry as well
     * in some cases.  Don't know exactly what's up with rename, but from the
     * look of things fsync(dir) should work.
     */
    int rc;
#ifdef O_DIRECTORY
    int fd = open(pszPath, O_RDONLY | O_DIRECTORY, 0);
#else
    int fd = open(pszPath, O_RDONLY, 0);
#endif
    if (fd >= 0)
    {
        if (fsync(fd) == 0)
            rc = VINF_SUCCESS;
        else
        {
            /* Linux fsync(2) man page documents both errors as an indication
             * that the file descriptor can't be flushed (seen EINVAL for usual
             * directories on CIFS). BSD (OS X) fsync(2) documents only the
             * latter, and Solaris fsync(3C) pretends there is no problem. */
            if (errno == EROFS || errno == EINVAL)
                rc = VERR_NOT_SUPPORTED;
            else
                rc = RTErrConvertFromErrno(errno);
        }
        close(fd);
    }
    else
        rc = RTErrConvertFromErrno(errno);
    return rc;
}


size_t rtDirNativeGetStructSize(const char *pszPath)
{
    long cbNameMax = pathconf(pszPath, _PC_NAME_MAX);
# ifdef NAME_MAX
    if (cbNameMax < NAME_MAX)           /* This is plain paranoia, but it doesn't hurt. */
        cbNameMax = NAME_MAX;
# endif
# ifdef _XOPEN_NAME_MAX
    if (cbNameMax < _XOPEN_NAME_MAX)    /* Ditto. */
        cbNameMax = _XOPEN_NAME_MAX;
# endif
    size_t cbDir = RT_UOFFSETOF_DYN(RTDIRINTERNAL, Data.d_name[cbNameMax + 1]);
    if (cbDir < sizeof(RTDIRINTERNAL))  /* Ditto. */
        cbDir = sizeof(RTDIRINTERNAL);
    cbDir = RT_ALIGN_Z(cbDir, 8);

    return cbDir;
}


int rtDirNativeOpen(PRTDIRINTERNAL pDir, uintptr_t hRelativeDir, void *pvNativeRelative)
{
    NOREF(hRelativeDir);
    NOREF(pvNativeRelative);

    /*
     * Convert to a native path and try opendir.
     */
    char       *pszSlash = NULL;
    char const *pszNativePath;
    int         rc;
    if (   !(pDir->fFlags & RTDIR_F_NO_FOLLOW)
        || pDir->fDirSlash
        || pDir->cchPath <= 1)
        rc = rtPathToNative(&pszNativePath, pDir->pszPath, NULL);
    else
    {
        pszSlash = (char *)&pDir->pszPath[pDir->cchPath - 1];
        *pszSlash = '\0';
        rc = rtPathToNative(&pszNativePath, pDir->pszPath, NULL);
    }
    if (RT_SUCCESS(rc))
    {
        if (   !(pDir->fFlags & RTDIR_F_NO_FOLLOW)
            || pDir->fDirSlash)
            pDir->pDir = opendir(pszNativePath);
        else
        {
            /*
             * If we can get fdopendir() and have both O_NOFOLLOW and O_DIRECTORY,
             * we will use open() to safely open the directory without following
             * symlinks in the final component, and then use fdopendir to get a DIR
             * from the file descriptor.
             *
             * If we cannot get that, we will use lstat() + opendir() as a fallback.
             *
             * We ASSUME that support for the O_NOFOLLOW and O_DIRECTORY flags is
             * older than fdopendir().
             */
#if defined(O_NOFOLLOW) && defined(O_DIRECTORY)
            /* Need to resolve fdopendir dynamically. */
            typedef DIR * (*PFNFDOPENDIR)(int);
            static PFNFDOPENDIR  s_pfnFdOpenDir = NULL;
            static bool volatile s_fInitalized = false;

            PFNFDOPENDIR pfnFdOpenDir = s_pfnFdOpenDir;
            ASMCompilerBarrier();
            if (s_fInitalized)
            { /* likely */ }
            else
            {
                pfnFdOpenDir = (PFNFDOPENDIR)(uintptr_t)dlsym(RTLD_DEFAULT, "fdopendir");
                s_pfnFdOpenDir = pfnFdOpenDir;
                ASMAtomicWriteBool(&s_fInitalized, true);
            }

            if (pfnFdOpenDir)
            {
                int fd = open(pszNativePath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0);
                if (fd >= 0)
                {
                    pDir->pDir = pfnFdOpenDir(fd);
                    if (RT_UNLIKELY(!pDir->pDir))
                    {
                        rc = RTErrConvertFromErrno(errno);
                        close(fd);
                    }
                }
                else
                {
                    /* WSL returns ELOOP here, but we take no chances that O_NOFOLLOW
                       takes precedence over O_DIRECTORY everywhere. */
                    int iErr = errno;
                    if (iErr == ELOOP || iErr == ENOTDIR)
                    {
                        struct stat St;
                        if (   lstat(pszNativePath, &St) == 0
                            && S_ISLNK(St.st_mode))
                            rc = VERR_IS_A_SYMLINK;
                        else
                            rc = RTErrConvertFromErrno(iErr);
                    }
                }
            }
            else
#endif
            {
                /* Fallback.  This contains a race condition. */
                struct stat St;
                if (   lstat(pszNativePath, &St) != 0
                    || !S_ISLNK(St.st_mode))
                    pDir->pDir = opendir(pszNativePath);
                else
                    rc = VERR_IS_A_SYMLINK;
            }
        }
        if (pDir->pDir)
        {
            /*
             * Init data (allocated as all zeros).
             */
            pDir->fDataUnread = false; /* spelling it out */
        }
        else if (RT_SUCCESS_NP(rc))
            rc = RTErrConvertFromErrno(errno);

        rtPathFreeNative(pszNativePath, pDir->pszPath);
    }
    if (pszSlash)
        *pszSlash = RTPATH_SLASH;
    return rc;
}


RTDECL(int) RTDirClose(RTDIR hDir)
{
    PRTDIRINTERNAL pDir = hDir;

    /*
     * Validate input.
     */
    if (!pDir)
        return VERR_INVALID_PARAMETER;
    if (pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the handle.
     */
    int rc = VINF_SUCCESS;
    pDir->u32Magic = RTDIR_MAGIC_DEAD;
    if (closedir(pDir->pDir))
    {
        rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("closedir(%p) -> errno=%d (%Rrc)\n", pDir->pDir, errno, rc));
    }

    RTMemFree(pDir);
    return rc;
}


/**
 * Ensure that there is unread data in the buffer
 * and that there is a converted filename hanging around.
 *
 * @returns IPRT status code.
 * @param   pDir        the open directory. Fully validated.
 */
static int rtDirReadMore(PRTDIRINTERNAL pDir)
{
    /** @todo try avoid the rematching on buffer overflow errors. */
    for (;;)
    {
        /*
         * Fetch data?
         */
        if (!pDir->fDataUnread)
        {
            struct dirent *pResult = NULL;
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            int rc = readdir_r(pDir->pDir, &pDir->Data, &pResult);
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic pop
#endif
            if (rc)
            {
                rc = RTErrConvertFromErrno(rc);
                /** @todo Consider translating ENOENT (The current
                 *        position of the directory stream is invalid)
                 *        differently. */
                AssertMsg(rc == VERR_FILE_NOT_FOUND, ("%Rrc\n", rc));
                return rc;
            }
            if (!pResult)
                return VERR_NO_MORE_FILES;
        }

        /*
         * Convert the filename to UTF-8.
         */
        if (!pDir->pszName)
        {
            int rc = rtPathFromNative(&pDir->pszName, pDir->Data.d_name, pDir->pszPath);
            if (RT_FAILURE(rc))
            {
                pDir->pszName = NULL;
                return rc;
            }
            pDir->cchName = strlen(pDir->pszName);
        }
        if (    !pDir->pfnFilter
            ||  pDir->pfnFilter(pDir, pDir->pszName))
            break;
        rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
        pDir->pszName     = NULL;
        pDir->fDataUnread = false;
    }

    pDir->fDataUnread = true;
    return VINF_SUCCESS;
}


#ifdef HAVE_DIRENT_D_TYPE
/**
 * Converts the d_type field to IPRT directory entry type.
 *
 * @returns IPRT directory entry type.
 * @param    Unix
 */
static RTDIRENTRYTYPE rtDirType(int iType)
{
    switch (iType)
    {
        case DT_UNKNOWN:    return RTDIRENTRYTYPE_UNKNOWN;
        case DT_FIFO:       return RTDIRENTRYTYPE_FIFO;
        case DT_CHR:        return RTDIRENTRYTYPE_DEV_CHAR;
        case DT_DIR:        return RTDIRENTRYTYPE_DIRECTORY;
        case DT_BLK:        return RTDIRENTRYTYPE_DEV_BLOCK;
        case DT_REG:        return RTDIRENTRYTYPE_FILE;
        case DT_LNK:        return RTDIRENTRYTYPE_SYMLINK;
        case DT_SOCK:       return RTDIRENTRYTYPE_SOCKET;
        case DT_WHT:        return RTDIRENTRYTYPE_WHITEOUT;
        default:
            AssertMsgFailed(("iType=%d\n", iType));
            return RTDIRENTRYTYPE_UNKNOWN;
    }
}
#endif /*HAVE_DIRENT_D_TYPE */


RTDECL(int) RTDirRead(RTDIR hDir, PRTDIRENTRY pDirEntry, size_t *pcbDirEntry)
{
    PRTDIRINTERNAL pDir = hDir;

    /*
     * Validate and digest input.
     */
    if (!rtDirValidHandle(pDir))
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pDirEntry, VERR_INVALID_POINTER);

    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        AssertPtrReturn(pcbDirEntry, VERR_INVALID_POINTER);
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRY, szName[2]),
                        ("Invalid *pcbDirEntry=%d (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch more data if necessary and/or convert the name.
     */
    int rc = rtDirReadMore(pDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if we've got enough space to return the data.
         */
        const char  *pszName    = pDir->pszName;
        const size_t cchName    = pDir->cchName;
        const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRY, szName[1]) + cchName;
        if (pcbDirEntry)
            *pcbDirEntry = cbRequired;
        if (cbRequired <= cbDirEntry)
        {
            /*
             * Setup the returned data.
             */
            pDirEntry->INodeId = pDir->Data.d_ino; /* may need #ifdefing later */
#ifdef HAVE_DIRENT_D_TYPE
            pDirEntry->enmType = rtDirType(pDir->Data.d_type);
#else
            pDirEntry->enmType = RTDIRENTRYTYPE_UNKNOWN;
#endif
            pDirEntry->cbName  = (uint16_t)cchName;
            Assert(pDirEntry->cbName == cchName);
            memcpy(pDirEntry->szName, pszName, cchName + 1);

            /* free cached data */
            pDir->fDataUnread  = false;
            rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
            pDir->pszName = NULL;
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }

    LogFlow(("RTDirRead(%p:{%s}, %p:{%s}, %p:{%u}): returns %Rrc\n",
             pDir, pDir->pszPath, pDirEntry, RT_SUCCESS(rc) ? pDirEntry->szName : "<failed>",
             pcbDirEntry, pcbDirEntry ? *pcbDirEntry : 0, rc));
    return rc;
}


/**
 * Fills dummy info into the info structure.
 * This function is called if we cannot stat the file.
 *
 * @param   pInfo   The struct in question.
 * @param
 */
static void rtDirSetDummyInfo(PRTFSOBJINFO pInfo, RTDIRENTRYTYPE enmType)
{
    pInfo->cbObject = 0;
    pInfo->cbAllocated = 0;
    RTTimeSpecSetNano(&pInfo->AccessTime, 0);
    RTTimeSpecSetNano(&pInfo->ModificationTime, 0);
    RTTimeSpecSetNano(&pInfo->ChangeTime, 0);
    RTTimeSpecSetNano(&pInfo->BirthTime, 0);
    memset(&pInfo->Attr, 0, sizeof(pInfo->Attr));
    pInfo->Attr.enmAdditional = RTFSOBJATTRADD_NOTHING;
    switch (enmType)
    {
        default:
        case RTDIRENTRYTYPE_UNKNOWN:    pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL;                       break;
        case RTDIRENTRYTYPE_FIFO:       pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FIFO;      break;
        case RTDIRENTRYTYPE_DEV_CHAR:   pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_DEV_CHAR;  break;
        case RTDIRENTRYTYPE_DIRECTORY:  pInfo->Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY; break;
        case RTDIRENTRYTYPE_DEV_BLOCK:  pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_DEV_BLOCK; break;
        case RTDIRENTRYTYPE_FILE:       pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE;      break;
        case RTDIRENTRYTYPE_SYMLINK:    pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_SYMLINK;   break;
        case RTDIRENTRYTYPE_SOCKET:     pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_SOCKET;    break;
        case RTDIRENTRYTYPE_WHITEOUT:   pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_WHITEOUT;  break;
    }
}


RTDECL(int) RTDirReadEx(RTDIR hDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                        RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    PRTDIRINTERNAL pDir = hDir;

    /*
     * Validate and digest input.
     */
    if (!rtDirValidHandle(pDir))
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pDirEntry, VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        AssertPtrReturn(pcbDirEntry, VERR_INVALID_POINTER);
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRYEX, szName[2]),
                        ("Invalid *pcbDirEntry=%zu (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch more data if necessary and/or convert the name.
     */
    int rc = rtDirReadMore(pDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if we've got enough space to return the data.
         */
        const char  *pszName    = pDir->pszName;
        const size_t cchName    = pDir->cchName;
        const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRYEX, szName[1]) + cchName;
        if (pcbDirEntry)
            *pcbDirEntry = cbRequired;
        if (cbRequired <= cbDirEntry)
        {
            /*
             * Setup the returned data.
             */
            pDirEntry->cwcShortName = 0;
            pDirEntry->wszShortName[0] = 0;
            pDirEntry->cbName  = (uint16_t)cchName;
            Assert(pDirEntry->cbName == cchName);
            memcpy(pDirEntry->szName, pszName, cchName + 1);

            /* get the info data */
            size_t cch = cchName + pDir->cchPath + 1;
            char *pszNamePath = (char *)alloca(cch);
            if (pszNamePath)
            {
                memcpy(pszNamePath, pDir->pszPath, pDir->cchPath);
                memcpy(pszNamePath + pDir->cchPath, pszName, cchName + 1);
                rc = RTPathQueryInfoEx(pszNamePath, &pDirEntry->Info, enmAdditionalAttribs, fFlags);
            }
            else
                rc = VERR_NO_MEMORY;
            if (RT_FAILURE(rc))
            {
#ifdef HAVE_DIRENT_D_TYPE
                rtDirSetDummyInfo(&pDirEntry->Info, rtDirType(pDir->Data.d_type));
#else
                rtDirSetDummyInfo(&pDirEntry->Info, RTDIRENTRYTYPE_UNKNOWN);
#endif
                rc = VWRN_NO_DIRENT_INFO;
            }

            /* free cached data */
            pDir->fDataUnread  = false;
            rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
            pDir->pszName = NULL;
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}


RTDECL(int) RTDirRewind(RTDIR hDir)
{
    PRTDIRINTERNAL pDir = hDir;

    /*
     * Validate and digest input.
     */
    if (!rtDirValidHandle(pDir))
        return VERR_INVALID_PARAMETER;

    /*
     * Do the rewinding.
     */
    /** @todo OS/2 does not rescan the directory as it should. */
    rewinddir(pDir->pDir);
    pDir->fDataUnread = false;

    return VINF_SUCCESS;
}


RTDECL(int) RTDirRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Take common cause with RTPathRename.
     */
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, RTFS_TYPE_DIRECTORY);

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, rc));
    return rc;
}

