/* $Id: fileio-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, POSIX, Part 1.
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
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
# include <stdio.h>
#else
# include <unistd.h>
# include <sys/time.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/file.h>
#endif
#if defined(RT_OS_OS2) && (!defined(__INNOTEK_LIBC__) || __INNOTEK_LIBC__ < 0x006)
# include <io.h>
#endif
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
# include <sys/disk.h>
#endif
#ifdef RT_OS_SOLARIS
# include <stropts.h>
# include <sys/dkio.h>
# include <sys/vtoc.h>
#endif /* RT_OS_SOLARIS */

#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/thread.h>
#include "internal/file.h"
#include "internal/fs.h"
#include "internal/path.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Default file permissions for newly created files. */
#if defined(S_IRUSR) && defined(S_IWUSR)
# define RT_FILE_PERMISSION  (S_IRUSR | S_IWUSR)
#else
# define RT_FILE_PERMISSION  (00600)
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef O_CLOEXEC
static int volatile g_fHave_O_CLOEXEC = 0; /* {-1,0,1}; since Linux 2.6.23 */
#endif



RTDECL(bool) RTFileExists(const char *pszPath)
{
    bool fRc = false;
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !stat(pszNativePath, &s)
            && S_ISREG(s.st_mode);

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTFileExists(%p={%s}): returns %RTbool\n", pszPath, pszPath, fRc));
    return fRc;
}


#ifdef O_CLOEXEC
/** Worker for RTFileOpenEx that detects whether the kernel supports
 *  O_CLOEXEC or not, setting g_fHave_O_CLOEXEC to 1 or -1 accordingly. */
static int rtFileOpenExDetectCloExecSupport(void)
{
    /*
     * Open /dev/null with O_CLOEXEC and see if FD_CLOEXEC is set or not.
     */
    int fHave_O_CLOEXEC = -1;
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd >= 0)
    {
        int fFlags = fcntl(fd, F_GETFD, 0);
        fHave_O_CLOEXEC = fFlags > 0 && (fFlags & FD_CLOEXEC) ? 1 : -1;
        close(fd);
    }
    else
        AssertMsg(errno == EINVAL, ("%d\n", errno));
    g_fHave_O_CLOEXEC = fHave_O_CLOEXEC;
    return fHave_O_CLOEXEC;
}
#endif


RTR3DECL(int) RTFileOpen(PRTFILE pFile, const char *pszFilename, uint64_t fOpen)
{
    return RTFileOpenEx(pszFilename, fOpen, pFile, NULL);
}


RTDECL(int)  RTFileOpenEx(const char *pszFilename, uint64_t fOpen, PRTFILE phFile, PRTFILEACTION penmActionTaken)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phFile, VERR_INVALID_POINTER);
    *phFile = NIL_RTFILE;
    if (penmActionTaken)
        *penmActionTaken = RTFILEACTION_INVALID;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);

    /*
     * Merge forced open flags and validate them.
     */
    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;
#ifndef O_NONBLOCK
    AssertReturn(!(fOpen & RTFILE_O_NON_BLOCK), VERR_INVALID_FLAGS);
#endif
#if defined(RT_OS_OS2) /* Cannot delete open files on OS/2. */
    AssertReturn(!(fOpen & RTFILE_O_TEMP_AUTO_DELETE), VERR_NOT_SUPPORTED);
#endif

    /*
     * Calculate open mode flags.
     */
    int fOpenMode = 0;
#ifdef O_BINARY
    fOpenMode |= O_BINARY;              /* (pc) */
#endif
#ifdef O_LARGEFILE
    fOpenMode |= O_LARGEFILE;           /* (linux, solaris) */
#endif
#ifdef O_NOINHERIT
    if (!(fOpen & RTFILE_O_INHERIT))
        fOpenMode |= O_NOINHERIT;
#endif
#ifdef O_CLOEXEC
    int fHave_O_CLOEXEC = g_fHave_O_CLOEXEC;
    if (   !(fOpen & RTFILE_O_INHERIT)
        && (   fHave_O_CLOEXEC > 0
            || (   fHave_O_CLOEXEC == 0
                && (fHave_O_CLOEXEC = rtFileOpenExDetectCloExecSupport()) > 0)))
        fOpenMode |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
    if (fOpen & RTFILE_O_NON_BLOCK)
        fOpenMode |= O_NONBLOCK;
#endif
#ifdef O_SYNC
    if (fOpen & RTFILE_O_WRITE_THROUGH)
        fOpenMode |= O_SYNC;
#endif
#if defined(O_DIRECT) && defined(RT_OS_LINUX)
    /* O_DIRECT is mandatory to get async I/O working on Linux. */
    if (fOpen & RTFILE_O_ASYNC_IO)
        fOpenMode |= O_DIRECT;
#endif
#if defined(O_DIRECT) && (defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD))
    /* Disable the kernel cache. */
    if (fOpen & RTFILE_O_NO_CACHE)
        fOpenMode |= O_DIRECT;
#endif

    /* create/truncate file */
    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case RTFILE_O_OPEN:             break;
        case RTFILE_O_OPEN_CREATE:      fOpenMode |= O_CREAT; break;
        case RTFILE_O_CREATE:           fOpenMode |= O_CREAT | O_EXCL; break;
        case RTFILE_O_CREATE_REPLACE:   fOpenMode |= O_CREAT | O_TRUNC; break; /** @todo replacing needs fixing, this is *not* a 1:1 mapping! */
        default:
            AssertMsgFailed(("fOpen=%#llx\n", fOpen));
            fOpen = (fOpen & ~RTFILE_O_ACTION_MASK) | RTFILE_O_OPEN;
            break;

    }
    if (   (fOpen & RTFILE_O_TRUNCATE)
        && (fOpen & RTFILE_O_ACTION_MASK) != RTFILE_O_CREATE)
        fOpenMode |= O_TRUNC;

    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            fOpenMode |= O_RDONLY; /* RTFILE_O_APPEND is ignored. */
            break;
        case RTFILE_O_WRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_WRONLY : O_WRONLY;
            break;
        case RTFILE_O_READWRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_RDWR   : O_RDWR;
            break;
        default:
            AssertMsgFailedReturn(("RTFileOpen received an invalid RW value, fOpen=%#llx\n", fOpen), VERR_INVALID_FLAGS);
    }

    /* File mode. */
    int fMode = (fOpen & RTFILE_O_CREATE_MODE_MASK)
              ? (fOpen & RTFILE_O_CREATE_MODE_MASK) >> RTFILE_O_CREATE_MODE_SHIFT
              : RT_FILE_PERMISSION;

    /** @todo sharing? */

    /*
     * Open/create the file.
     */
    char const *pszNativeFilename;
    rc = rtPathToNative(&pszNativeFilename, pszFilename, NULL);
    if (RT_FAILURE(rc))
        return (rc);

    int fh;
    int iErr;
    if (!penmActionTaken)
    {
        fh   = open(pszNativeFilename, fOpenMode, fMode);
        iErr = errno;
    }
    else
    {
        /* We need to know exactly which action was taken by open, Windows &
           OS/2 style.  Can be tedious and subject to races:  */
        switch (fOpen & RTFILE_O_ACTION_MASK)
        {
            case RTFILE_O_OPEN:
                Assert(!(fOpenMode & O_CREAT));
                Assert(!(fOpenMode & O_EXCL));
                fh   = open(pszNativeFilename, fOpenMode, fMode);
                iErr = errno;
                if (fh >= 0)
                    *penmActionTaken = fOpenMode & O_TRUNC ? RTFILEACTION_TRUNCATED : RTFILEACTION_OPENED;
                break;

            case RTFILE_O_CREATE:
                Assert(fOpenMode & O_CREAT);
                Assert(fOpenMode & O_EXCL);
                fh   = open(pszNativeFilename, fOpenMode, fMode);
                iErr = errno;
                if (fh >= 0)
                    *penmActionTaken = RTFILEACTION_CREATED;
                else if (iErr == EEXIST)
                    *penmActionTaken = RTFILEACTION_ALREADY_EXISTS;
                break;

            case RTFILE_O_OPEN_CREATE:
            case RTFILE_O_CREATE_REPLACE:
            {
                Assert(fOpenMode & O_CREAT);
                Assert(!(fOpenMode & O_EXCL));
                int iTries = 64;
                while (iTries-- > 0)
                {
                    /* Yield the CPU if we've raced too long. */
                    if (iTries < 4)
                        RTThreadSleep(2 - (iTries & 1));

                    /* Try exclusive creation first: */
                    fh   = open(pszNativeFilename, fOpenMode | O_EXCL, fMode);
                    iErr = errno;
                    if (fh >= 0)
                    {
                        *penmActionTaken = RTFILEACTION_CREATED;
                        break;
                    }
                    if (iErr != EEXIST)
                        break;

                    /* If the file exists, try open it: */
                    fh   = open(pszNativeFilename, fOpenMode & ~O_CREAT, fMode);
                    iErr = errno;
                    if (fh >= 0)
                    {
                        if ((fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
                            *penmActionTaken = fOpenMode & O_TRUNC ? RTFILEACTION_TRUNCATED : RTFILEACTION_OPENED;
                        else
                            *penmActionTaken = RTFILEACTION_REPLACED;
                        break;
                    }
                    if (iErr != ENOENT)
                        break;
                }
                Assert(iTries >= 0);
                if (iTries < 0)
                {
                    /* Thanks for the race, but we need to get on with things.  */
                    fh   = open(pszNativeFilename, fOpenMode, fMode);
                    iErr = errno;
                    if (fh >= 0)
                        *penmActionTaken = RTFILEACTION_OPENED;
                }
                break;
            }

            default:
                AssertMsgFailed(("fOpen=%#llx fOpenMode=%#x\n", fOpen, fOpenMode));
                iErr = EINVAL;
                fh = -1;
                break;
        }
    }

    /*
     * If temporary file, delete it.
     */
    if (   fh >= 0
        && (fOpen & RTFILE_O_TEMP_AUTO_DELETE))
    {
        /** @todo Use funlinkat/funlink or similar here when available!  Or better,
         *        use O_TMPFILE, only that may require fallback as not supported by
         *        all file system on linux. */
        iErr = unlink(pszNativeFilename);
        Assert(iErr == 0);
    }

    rtPathFreeNative(pszNativeFilename, pszFilename);
    if (fh >= 0)
    {
        iErr = 0;

        /*
         * Mark the file handle close on exec, unless inherit is specified.
         */
        if (    !(fOpen & RTFILE_O_INHERIT)
#ifdef O_NOINHERIT
            &&  !(fOpenMode & O_NOINHERIT)  /* Take care since it might be a zero value dummy. */
#endif
#ifdef O_CLOEXEC
            &&  fHave_O_CLOEXEC <= 0
#endif
            )
            iErr = fcntl(fh, F_SETFD, FD_CLOEXEC) >= 0 ? 0 : errno;

        /*
         * Switch direct I/O on now if requested and required.
         */
#if defined(RT_OS_DARWIN) \
 || (defined(RT_OS_SOLARIS) && !defined(IN_GUEST))
        if (iErr == 0 && (fOpen & RTFILE_O_NO_CACHE))
        {
# if defined(RT_OS_DARWIN)
            iErr = fcntl(fh, F_NOCACHE, 1)        >= 0 ? 0 : errno;
# else
            iErr = directio(fh, DIRECTIO_ON)      >= 0 ? 0 : errno;
# endif
        }
#endif

        /*
         * Implement / emulate file sharing.
         *
         * We need another mode which allows skipping this stuff completely
         * and do things the UNIX way. So for the present this is just a debug
         * aid that can be enabled by developers too lazy to test on Windows.
         */
#if 0 && defined(RT_OS_LINUX)
        if (iErr == 0)
        {
            /* This approach doesn't work because only knfsd checks for these
               buggers. :-( */
            int iLockOp;
            switch (fOpen & RTFILE_O_DENY_MASK)
            {
                default:
                AssertFailed();
                case RTFILE_O_DENY_NONE:
                case RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_READ | LOCK_WRITE;
                    break;
                case RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_WRITE;
                    break;
                case RTFILE_O_DENY_WRITE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_READ;
                    break;
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND;
                    break;
            }
            iErr = flock(fh, iLockOp | LOCK_NB);
            if (iErr != 0)
                iErr = errno == EAGAIN ? ETXTBSY : 0;
        }
#endif /* 0 && RT_OS_LINUX */
#if defined(DEBUG_bird) && !defined(RT_OS_SOLARIS)
        if (iErr == 0)
        {
            /* This emulation is incomplete but useful. */
            switch (fOpen & RTFILE_O_DENY_MASK)
            {
                default:
                AssertFailed();
                case RTFILE_O_DENY_NONE:
                case RTFILE_O_DENY_NOT_DELETE:
                case RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    break;
                case RTFILE_O_DENY_WRITE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_NOT_DELETE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    if (fOpen & RTFILE_O_WRITE)
                    {
                        iErr = flock(fh, LOCK_EX | LOCK_NB);
                        if (iErr != 0)
                            iErr = errno == EAGAIN ? ETXTBSY : 0;
                    }
                    break;
            }
        }
#endif
#ifdef RT_OS_SOLARIS
        /** @todo Use fshare_t and associates, it's a perfect match. see sys/fcntl.h */
#endif

        /*
         * We're done.
         */
        if (iErr == 0)
        {
            *phFile = (RTFILE)(uintptr_t)fh;
            Assert((intptr_t)*phFile == fh);
            LogFlow(("RTFileOpen(%p:{%RTfile}, %p:{%s}, %#llx): returns %Rrc\n",
                     phFile, *phFile, pszFilename, pszFilename, fOpen, rc));
            return VINF_SUCCESS;
        }

        close(fh);
    }
    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int)  RTFileOpenBitBucket(PRTFILE phFile, uint64_t fAccess)
{
    AssertReturn(   fAccess == RTFILE_O_READ
                 || fAccess == RTFILE_O_WRITE
                 || fAccess == RTFILE_O_READWRITE,
                 VERR_INVALID_PARAMETER);
    return RTFileOpen(phFile, "/dev/null", fAccess | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
}


RTR3DECL(int)  RTFileClose(RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return VINF_SUCCESS;
    if (close(RTFileToNative(hFile)) == 0)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileFromNative(PRTFILE pFile, RTHCINTPTR uNative)
{
    AssertCompile(sizeof(uNative) == sizeof(*pFile));
    if (uNative < 0)
    {
        AssertMsgFailed(("%p\n", uNative));
        *pFile = NIL_RTFILE;
        return VERR_INVALID_HANDLE;
    }
    *pFile = (RTFILE)uNative;
    return VINF_SUCCESS;
}


RTR3DECL(RTHCINTPTR) RTFileToNative(RTFILE hFile)
{
    AssertReturn(hFile != NIL_RTFILE, -1);
    return (intptr_t)hFile;
}


RTFILE rtFileGetStandard(RTHANDLESTD enmStdHandle)
{
    int fd;
    switch (enmStdHandle)
    {
        case RTHANDLESTD_INPUT:  fd = 0; break;
        case RTHANDLESTD_OUTPUT: fd = 1; break;
        case RTHANDLESTD_ERROR:  fd = 2; break;
        default:
            AssertFailedReturn(NIL_RTFILE);
    }

    struct stat st;
    int rc = fstat(fd, &st);
    if (rc == -1)
        return NIL_RTFILE;
    return (RTFILE)(intptr_t)fd;
}


RTR3DECL(int)  RTFileDelete(const char *pszFilename)
{
    char const *pszNativeFilename;
    int rc = rtPathToNative(&pszNativeFilename, pszFilename, NULL);
    if (RT_SUCCESS(rc))
    {
        if (unlink(pszNativeFilename) != 0)
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFilename, pszFilename);
    }
    return rc;
}


RTR3DECL(int)  RTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    static const unsigned aSeekRecode[] =
    {
        SEEK_SET,
        SEEK_CUR,
        SEEK_END,
    };

    /*
     * Validate input.
     */
    if (uMethod > RTFILE_SEEK_END)
    {
        AssertMsgFailed(("Invalid uMethod=%d\n", uMethod));
        return VERR_INVALID_PARAMETER;
    }

    /* check that within off_t range. */
    if (    sizeof(off_t) < sizeof(offSeek)
        && (    (offSeek > 0 && (unsigned)(offSeek >> 32) != 0)
            ||  (offSeek < 0 && (unsigned)(-offSeek >> 32) != 0)))
    {
        AssertMsgFailed(("64-bit search not supported\n"));
        return VERR_NOT_SUPPORTED;
    }

    off_t offCurrent = lseek(RTFileToNative(hFile), (off_t)offSeek, aSeekRecode[uMethod]);
    if (offCurrent != ~0)
    {
        if (poffActual)
            *poffActual = (uint64_t)offCurrent;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int)  RTFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    if (cbToRead <= 0)
    {
        if (pcbRead)
            *pcbRead = 0;
        return VINF_SUCCESS;
    }

    /*
     * Attempt read.
     */
    ssize_t cbRead = read(RTFileToNative(hFile), pvBuf, cbToRead);
    if (cbRead >= 0)
    {
        if (pcbRead)
            /* caller can handle partial read. */
            *pcbRead = cbRead;
        else
        {
            /* Caller expects all to be read. */
            while ((ssize_t)cbToRead > cbRead)
            {
                ssize_t cbReadPart = read(RTFileToNative(hFile), (char*)pvBuf + cbRead, cbToRead - cbRead);
                if (cbReadPart <= 0)
                {
                    if (cbReadPart == 0)
                        return VERR_EOF;
                    return RTErrConvertFromErrno(errno);
                }
                cbRead += cbReadPart;
            }
        }
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int)  RTFileWrite(RTFILE hFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    if (cbToWrite <= 0)
        return VINF_SUCCESS;

    /*
     * Attempt write.
     */
    ssize_t cbWritten = write(RTFileToNative(hFile), pvBuf, cbToWrite);
    if (cbWritten >= 0)
    {
        if (pcbWritten)
            /* caller can handle partial write. */
            *pcbWritten = cbWritten;
        else
        {
            /* Caller expects all to be write. */
            while ((ssize_t)cbToWrite > cbWritten)
            {
                ssize_t cbWrittenPart = write(RTFileToNative(hFile), (const char *)pvBuf + cbWritten, cbToWrite - cbWritten);
                if (cbWrittenPart <= 0)
                    return cbWrittenPart < 0 ? RTErrConvertFromErrno(errno) : VERR_TRY_AGAIN;
                cbWritten += cbWrittenPart;
            }
        }
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int)  RTFileSetSize(RTFILE hFile, uint64_t cbSize)
{
    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbSize)
        &&  (cbSize >> 32) != 0)
    {
        AssertMsgFailed(("64-bit filesize not supported! cbSize=%lld\n", cbSize));
        return VERR_NOT_SUPPORTED;
    }

#if defined(_MSC_VER) || (defined(RT_OS_OS2) && (!defined(__INNOTEK_LIBC__) || __INNOTEK_LIBC__ < 0x006))
    if (chsize(RTFileToNative(hFile), (off_t)cbSize) == 0)
#else
    /* This relies on a non-standard feature of FreeBSD, Linux, and OS/2
     * LIBC v0.6 and higher. (SuS doesn't define ftruncate() and size bigger
     * than the file.)
     */
    if (ftruncate(RTFileToNative(hFile), (off_t)cbSize) == 0)
#endif
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileQuerySize(RTFILE hFile, uint64_t *pcbSize)
{
    /*
     * Ask fstat() first.
     */
    struct stat st;
    if (!fstat(RTFileToNative(hFile), &st))
    {
        *pcbSize = st.st_size;
        if (   st.st_size != 0
#if defined(RT_OS_SOLARIS) || defined(RT_OS_DARWIN)
            || (!S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
#elif defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_DARWIN)
            || !S_ISCHR(st.st_mode)
#else
            || !S_ISBLK(st.st_mode)
#endif
            )
            return VINF_SUCCESS;

        /*
         * It could be a block device.  Try determin the size by I/O control
         * query or seek.
         */
#ifdef RT_OS_DARWIN
        uint64_t cBlocks;
        if (!ioctl(RTFileToNative(hFile), DKIOCGETBLOCKCOUNT, &cBlocks))
        {
            uint32_t cbBlock;
            if (!ioctl(RTFileToNative(hFile), DKIOCGETBLOCKSIZE, &cbBlock))
            {
                *pcbSize = cBlocks * cbBlock;
                return VINF_SUCCESS;
            }
        }

        /* Always fail block devices.  Character devices doesn't all need to be
           /dev/rdisk* nodes, they should return ENOTTY but /dev/null returns ENODEV
           and we include EINVAL just in case. */
        if (!S_ISBLK(st.st_mode) && (errno == ENOTTY || errno == ENODEV || errno == EINVAL))
            return VINF_SUCCESS;

#elif defined(RT_OS_SOLARIS)
        struct dk_minfo MediaInfo;
        if (!ioctl(RTFileToNative(hFile), DKIOCGMEDIAINFO, &MediaInfo))
        {
            *pcbSize = MediaInfo.dki_capacity * MediaInfo.dki_lbsize;
            return VINF_SUCCESS;
        }
        /* might not be a block device. */
        if (errno == EINVAL || errno == ENOTTY)
            return VINF_SUCCESS;

#elif defined(RT_OS_FREEBSD)
        off_t cbMedia = 0;
        if (!ioctl(RTFileToNative(hFile), DIOCGMEDIASIZE, &cbMedia))
        {
            *pcbSize = cbMedia;
            return VINF_SUCCESS;
        }
        /* might not be a block device. */
        if (errno == EINVAL || errno == ENOTTY)
            return VINF_SUCCESS;

#else
        /* PORTME! Avoid this path when possible. */
        uint64_t offSaved = UINT64_MAX;
        int rc = RTFileSeek(hFile, 0, RTFILE_SEEK_CURRENT, &offSaved);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileSeek(hFile, 0, RTFILE_SEEK_END, pcbSize);
            int rc2 = RTFileSeek(hFile, offSaved, RTFILE_SEEK_BEGIN, NULL);
            if (RT_SUCCESS(rc))
                return rc2;
        }
#endif
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileQueryMaxSizeEx(RTFILE hFile, PRTFOFF pcbMax)
{
    /*
     * Save the current location
     */
    uint64_t offOld = UINT64_MAX;
    int rc = RTFileSeek(hFile, 0, RTFILE_SEEK_CURRENT, &offOld);
    if (RT_FAILURE(rc))
        return rc;

    uint64_t offLow  =       0;
    uint64_t offHigh = INT64_MAX; /* we don't need bigger files */
    /** @todo Unfortunately this does not work for certain file system types,
     * for instance cifs mounts. Even worse, statvfs.f_fsid returns 0 for such
     * file systems. */

    /*
     * Quickly guess the order of magnitude for offHigh and offLow.
     */
    {
        uint64_t offHighPrev = offHigh;
        while (offHigh >= INT32_MAX)
        {
            rc = RTFileSeek(hFile, offHigh, RTFILE_SEEK_BEGIN, NULL);
            if (RT_SUCCESS(rc))
            {
                offLow = offHigh;
                offHigh = offHighPrev;
                break;
            }
            else
            {
                offHighPrev = offHigh;
                offHigh >>= 8;
            }
        }
    }

    /*
     * Sanity: if the seek to the initial offHigh (INT64_MAX) works, then
     * this algorithm cannot possibly work. Declare defeat.
     */
    if (offLow == offHigh)
    {
        rc = RTFileSeek(hFile, offOld, RTFILE_SEEK_BEGIN, NULL);
        if (RT_SUCCESS(rc))
            rc = VERR_NOT_IMPLEMENTED;

        return rc;
    }

    /*
     * Perform a binary search for the max file size.
     */
    while (offLow <= offHigh)
    {
        uint64_t offMid = offLow + (offHigh - offLow) / 2;
        rc = RTFileSeek(hFile, offMid, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
            offHigh = offMid - 1;
        else
            offLow  = offMid + 1;
    }

    if (pcbMax)
        *pcbMax = RT_MIN(offLow, offHigh);
    return RTFileSeek(hFile, offOld, RTFILE_SEEK_BEGIN, NULL);
}


RTR3DECL(bool) RTFileIsValid(RTFILE hFile)
{
    if (hFile != NIL_RTFILE)
    {
        int fFlags = fcntl(RTFileToNative(hFile), F_GETFD);
        if (fFlags >= 0)
            return true;
    }
    return false;
}


RTR3DECL(int) RTFileFlush(RTFILE hFile)
{
    if (!fsync(RTFileToNative(hFile)))
        return VINF_SUCCESS;
    /* Ignore EINVAL here as that's what returned for pseudo ttys
       and other odd handles. */
    if (errno == EINVAL)
        return VINF_NOT_SUPPORTED;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileIoCtl(RTFILE hFile, unsigned long ulRequest, void *pvData, unsigned cbData, int *piRet)
{
    NOREF(cbData);
    int rc = ioctl(RTFileToNative(hFile), ulRequest, pvData);
    if (piRet)
        *piRet = rc;
    return rc >= 0 ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileSetMode(RTFILE hFile, RTFMODE fMode)
{
    /*
     * Normalize the mode and call the API.
     */
    fMode = rtFsModeNormalize(fMode, NULL, 0, RTFS_TYPE_FILE);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    if (fchmod(RTFileToNative(hFile), fMode & RTFS_UNIX_MASK))
    {
        int rc = RTErrConvertFromErrno(errno);
        Log(("RTFileSetMode(%RTfile,%RTfmode): returns %Rrc\n", hFile, fMode, rc));
        return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTFileSetOwner(RTFILE hFile, uint32_t uid, uint32_t gid)
{
    uid_t uidNative = uid != NIL_RTUID ? (uid_t)uid : (uid_t)-1;
    AssertReturn(uid == uidNative, VERR_INVALID_PARAMETER);
    gid_t gidNative = gid != NIL_RTGID ? (gid_t)gid : (gid_t)-1;
    AssertReturn(gid == gidNative, VERR_INVALID_PARAMETER);

    if (fchown(RTFileToNative(hFile), uidNative, gidNative))
        return RTErrConvertFromErrno(errno);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFileRename(const char *pszSrc, const char *pszDst, unsigned fRename)
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
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, RTFS_TYPE_FILE);

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;
}

