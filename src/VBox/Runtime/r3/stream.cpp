/* $Id: stream.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - I/O Stream.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTSTREAM_STANDALONE
 * Standalone streams w/o depending on stdio.h, using our RTFile API for
 * file/whatever access. */
#if (defined(IPRT_NO_CRT) && defined(RT_OS_WINDOWS)) || defined(DOXYGEN_RUNNING)
# define RTSTREAM_STANDALONE
#endif

#if defined(RT_OS_LINUX) /* PORTME: check for the _unlocked functions in stdio.h */
# ifndef RTSTREAM_STANDALONE
#  define HAVE_FWRITE_UNLOCKED
# endif
#endif

/** @def RTSTREAM_WITH_TEXT_MODE
 * Indicates whether we need to support the 'text' mode files and convert
 * CRLF to LF while reading and writing. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
# define RTSTREAM_WITH_TEXT_MODE
#endif



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/stream.h>
#include "internal/iprt.h"

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#ifndef HAVE_FWRITE_UNLOCKED
# include <iprt/critsect.h>
#endif
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
# include <iprt/file.h>
#ifdef RTSTREAM_STANDALONE
# include <iprt/list.h>
#endif
#include <iprt/mem.h>
#ifdef RTSTREAM_STANDALONE
# include <iprt/once.h>
#endif
#include <iprt/param.h>
#include <iprt/string.h>

#include "internal/alignmentchecks.h"
#include "internal/magics.h"
#if defined(IPRT_NO_CRT) || defined(IN_RT_STATIC)
# include "internal/initterm.h"
#endif

#ifdef RTSTREAM_STANDALONE
# ifdef _MSC_VER
#  define IPRT_COMPILER_VCC_WITH_C_INIT_TERM_SECTIONS
#  include "internal/compiler-vcc.h"
# endif
#else
# include <stdio.h>
# include <errno.h>
# if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
#  include <io.h>
#  include <fcntl.h>
# endif
#endif
#ifdef RT_OS_WINDOWS
# include <iprt/utf16.h>
# include <iprt/win/windows.h>
#elif !defined(RTSTREAM_STANDALONE)
# include <termios.h>
# include <unistd.h>
# include <sys/ioctl.h>
#endif

#if defined(RT_OS_OS2) && !defined(RTSTREAM_STANDALONE)
# define _O_TEXT   O_TEXT
# define _O_BINARY O_BINARY
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef RTSTREAM_STANDALONE
/** The buffer direction. */
typedef enum RTSTREAMBUFDIR
{
    RTSTREAMBUFDIR_NONE = 0,
    RTSTREAMBUFDIR_READ,
    RTSTREAMBUFDIR_WRITE
} RTSTREAMBUFDIR;

/** The buffer style. */
typedef enum RTSTREAMBUFSTYLE
{
    RTSTREAMBUFSTYLE_UNBUFFERED = 0,
    RTSTREAMBUFSTYLE_LINE,
    RTSTREAMBUFSTYLE_FULL
} RTSTREAMBUFSTYLE;

#endif

/**
 * File stream.
 */
typedef struct RTSTREAM
{
    /** Magic value used to validate the stream. (RTSTREAM_MAGIC) */
    uint32_t            u32Magic;
    /** File stream error. */
    int32_t volatile    i32Error;
#ifndef RTSTREAM_STANDALONE
    /** Pointer to the LIBC file stream. */
    FILE               *pFile;
#else
    /** Indicates which standard handle this is supposed to be.
     * Set to RTHANDLESTD_INVALID if not one of the tree standard streams. */
    RTHANDLESTD         enmStdHandle;
    /** The IPRT handle backing this stream.
     * This is initialized lazily using enmStdHandle for the three standard
     * streams. */
    RTFILE              hFile;
    /** Buffer. */
    char               *pchBuf;
    /** Buffer allocation size. */
    size_t              cbBufAlloc;
    /** Offset of the first valid byte in the buffer. */
    size_t              offBufFirst;
    /** Offset of the end of valid bytes in the buffer (exclusive). */
    size_t              offBufEnd;
    /** The stream buffer direction.   */
    RTSTREAMBUFDIR      enmBufDir;
    /** The buffering style (unbuffered, line, full).
     * @todo replace by RTSTRMBUFMODE.  */
    RTSTREAMBUFSTYLE    enmBufStyle;
# ifdef RTSTREAM_WITH_TEXT_MODE
    /** Bitmap running parallel to each char pchBuf, indicating where a '\\r'
     * character have been removed during buffer filling.  This is used to implement
     * RTStrmTell in non-binary mode. */
    uint32_t           *pbmBuf;
    /** Indicates that we've got a CR ('\\r') beyond the end of official buffer
     * and need to check if there is a LF following it.  This member is ignored
     * in binary mode. */
    bool                fPendingCr;
# endif
#endif
    /** Stream is using the current process code set. */
    bool                fCurrentCodeSet;
    /** Whether the stream was opened in binary mode. */
    bool                fBinary;
    /** Whether to recheck the stream mode before writing. */
    bool                fRecheckMode;
#if !defined(HAVE_FWRITE_UNLOCKED) || defined(RTSTREAM_STANDALONE)
    /** Critical section for serializing access to the stream. */
    PRTCRITSECT         pCritSect;
#endif
#ifdef RTSTREAM_STANDALONE
    /** Entry in g_StreamList (for automatic flushing and closing at
     * exit/unload). */
    RTLISTNODE          ListEntry;
#endif
} RTSTREAM;


/**
 * State for wrapped output (RTStrmWrappedPrintf, RTStrmWrappedPrintfV).
 */
typedef struct RTSTRMWRAPPEDSTATE
{
    PRTSTREAM   pStream;            /**< The output stream. */
    uint32_t    cchWidth;           /**< The line width. */
    uint32_t    cchLine;            /**< The current line length (valid chars in szLine). */
    uint32_t    cLines;             /**< Number of lines written. */
    uint32_t    cchIndent;          /**< The indent (determined from the first line). */
    int         rcStatus;           /**< The output status. */
    uint8_t     cchHangingIndent;   /**< Hanging indent (from fFlags). */
    char        szLine[0x1000+1];   /**< We must buffer output so we can do proper word splitting. */
} RTSTRMWRAPPEDSTATE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The standard input stream. */
static RTSTREAM    g_StdIn =
{
    /* .u32Magic = */           RTSTREAM_MAGIC,
    /* .i32Error = */           0,
#ifndef RTSTREAM_STANDALONE
    /* .pFile = */              stdin,
#else
    /* .enmStdHandle = */       RTHANDLESTD_INPUT,
    /* .hFile = */              NIL_RTFILE,
    /* .pchBuf = */             NULL,
    /* .cbBufAlloc = */         0,
    /* .offBufFirst = */        0,
    /* .offBufEnd = */          0,
    /* .enmBufDir = */          RTSTREAMBUFDIR_NONE,
    /* .enmBufStyle = */        RTSTREAMBUFSTYLE_UNBUFFERED,
# ifdef RTSTREAM_WITH_TEXT_MODE
    /* .pbmBuf = */             NULL,
    /* .fPendingCr = */         false,
# endif
#endif
    /* .fCurrentCodeSet = */    true,
    /* .fBinary = */            false,
    /* .fRecheckMode = */       true,
#ifndef HAVE_FWRITE_UNLOCKED
    /* .pCritSect = */          NULL,
#endif
#ifdef RTSTREAM_STANDALONE
    /* .ListEntry = */          { NULL, NULL },
#endif
};

/** The standard error stream. */
static RTSTREAM    g_StdErr =
{
    /* .u32Magic = */           RTSTREAM_MAGIC,
    /* .i32Error = */           0,
#ifndef RTSTREAM_STANDALONE
    /* .pFile = */              stderr,
#else
    /* .enmStdHandle = */       RTHANDLESTD_ERROR,
    /* .hFile = */              NIL_RTFILE,
    /* .pchBuf = */             NULL,
    /* .cbBufAlloc = */         0,
    /* .offBufFirst = */        0,
    /* .offBufEnd = */          0,
    /* .enmBufDir = */          RTSTREAMBUFDIR_NONE,
    /* .enmBufStyle = */        RTSTREAMBUFSTYLE_UNBUFFERED,
# ifdef RTSTREAM_WITH_TEXT_MODE
    /* .pbmBuf = */             NULL,
    /* .fPendingCr = */         false,
# endif
#endif
    /* .fCurrentCodeSet = */    true,
    /* .fBinary = */            false,
    /* .fRecheckMode = */       true,
#ifndef HAVE_FWRITE_UNLOCKED
    /* .pCritSect = */          NULL,
#endif
#ifdef RTSTREAM_STANDALONE
    /* .ListEntry = */          { NULL, NULL },
#endif
};

/** The standard output stream. */
static RTSTREAM    g_StdOut =
{
    /* .u32Magic = */           RTSTREAM_MAGIC,
    /* .i32Error = */           0,
#ifndef RTSTREAM_STANDALONE
    /* .pFile = */              stdout,
#else
    /* .enmStdHandle = */       RTHANDLESTD_OUTPUT,
    /* .hFile = */              NIL_RTFILE,
    /* .pchBuf = */             NULL,
    /* .cbBufAlloc = */         0,
    /* .offBufFirst = */        0,
    /* .offBufEnd = */          0,
    /* .enmBufDir = */          RTSTREAMBUFDIR_NONE,
    /* .enmBufStyle = */        RTSTREAMBUFSTYLE_LINE,
# ifdef RTSTREAM_WITH_TEXT_MODE
    /* .pbmBuf = */             NULL,
    /* .fPendingCr = */         false,
# endif
#endif
    /* .fCurrentCodeSet = */    true,
    /* .fBinary = */            false,
    /* .fRecheckMode = */       true,
#ifndef HAVE_FWRITE_UNLOCKED
    /* .pCritSect = */          NULL,
#endif
#ifdef RTSTREAM_STANDALONE
    /* .ListEntry = */          { NULL, NULL },
#endif
};

/** Pointer to the standard input stream. */
RTDATADECL(PRTSTREAM)   g_pStdIn  = &g_StdIn;

/** Pointer to the standard output stream. */
RTDATADECL(PRTSTREAM)   g_pStdErr = &g_StdErr;

/** Pointer to the standard output stream. */
RTDATADECL(PRTSTREAM)   g_pStdOut = &g_StdOut;

#ifdef RTSTREAM_STANDALONE
/** Run-once initializer for the stream list (g_StreamList + g_StreamListCritSect). */
static RTONCE           g_StreamListOnce = RTONCE_INITIALIZER;
/** List of user created streams (excludes the standard streams). */
static RTLISTANCHOR     g_StreamList;
/** Critical section protecting the stream list. */
static RTCRITSECT       g_StreamListCritSect;


/** @callback_method_impl{FNRTONCE}   */
static DECLCALLBACK(int32_t) rtStrmListInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);
    RTListInit(&g_StreamList);
    return RTCritSectInit(&g_StreamListCritSect);
}

#endif


#ifndef HAVE_FWRITE_UNLOCKED
/**
 * Allocates and acquires the lock for the stream.
 *
 * @returns IPRT status code.
 * @param   pStream     The stream (valid).
 */
static int rtStrmAllocLock(PRTSTREAM pStream)
{
    Assert(pStream->pCritSect == NULL);

    PRTCRITSECT pCritSect = (PRTCRITSECT)RTMemAlloc(sizeof(*pCritSect));
    if (!pCritSect)
        return VERR_NO_MEMORY;

    /* The native stream lock are normally not recursive. */
    uint32_t fFlags = RTCRITSECT_FLAGS_NO_NESTING;
# if defined(IPRT_NO_CRT) || defined(IN_RT_STATIC)
    /* IPRT is often used deliberatly without initialization in no-CRT
       binaries (for instance VBoxAddInstallNt3x.exe), so in order to avoid
       asserting in the lock validator we add the bootstrap hack that disable
       lock validation for the section.
       Update: Applying this to all builds involving static linking, as it's
               now going to be used for tests running at compile-time too.  */
    if (!rtInitIsInitialized())
        fFlags |= RTCRITSECT_FLAGS_BOOTSTRAP_HACK;
# endif
    int rc = RTCritSectInitEx(pCritSect, fFlags, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTSemSpinMutex");
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectEnter(pCritSect);
        if (RT_SUCCESS(rc))
        {
            if (RT_LIKELY(ASMAtomicCmpXchgPtr(&pStream->pCritSect, pCritSect, NULL)))
                return VINF_SUCCESS;

            RTCritSectLeave(pCritSect);
        }
        RTCritSectDelete(pCritSect);
    }
    RTMemFree(pCritSect);

    /* Handle the lost race case... */
    pCritSect = ASMAtomicReadPtrT(&pStream->pCritSect, PRTCRITSECT);
    if (pCritSect)
        return RTCritSectEnter(pCritSect);

    return rc;
}
#endif /* !HAVE_FWRITE_UNLOCKED */


/**
 * Locks the stream.  May have to allocate the lock as well.
 *
 * @param   pStream     The stream (valid).
 */
DECLINLINE(void) rtStrmLock(PRTSTREAM pStream)
{
#ifdef HAVE_FWRITE_UNLOCKED
    flockfile(pStream->pFile);
#else
    if (RT_LIKELY(pStream->pCritSect))
        RTCritSectEnter(pStream->pCritSect);
    else
        rtStrmAllocLock(pStream);
#endif
}


/**
 * Unlocks the stream.
 *
 * @param   pStream     The stream (valid).
 */
DECLINLINE(void) rtStrmUnlock(PRTSTREAM pStream)
{
#ifdef HAVE_FWRITE_UNLOCKED
    funlockfile(pStream->pFile);
#else
    if (RT_LIKELY(pStream->pCritSect))
        RTCritSectLeave(pStream->pCritSect);
#endif
}


/**
 * Opens a file stream.
 *
 * @returns iprt status code.
 * @param   pszFilename     Path to the file to open, hFile must be NIL_RTFILE.
 *                          NULL if a hFile is to be used instead.
 * @param   hFile           File handle to use when called from
 *                          RTStrmOpenFileHandle.  pszFilename must be NULL.
 * @param   pszMode         See RTStrmOpen.
 * @param   ppStream        Where to store the opened stream.
 */
static int rtStrmOpenComon(const char *pszFilename, RTFILE hFile, const char *pszMode, PRTSTREAM *ppStream)
{
    /*
     * Validate input and look for things we care for in the pszMode string.
     */
    AssertReturn(pszMode && *pszMode, VERR_INVALID_FLAGS);

    /*
     * Process the mode string.
     */
    char    chMode     = '\0';  /* a|r|w */
    bool    fPlus      = false; /* + */
    bool    fBinary    = false; /* b | !t */
    bool    fExclusive = false; /* x */
    bool    fNoInherit = false;  /* e (linux, freebsd) | N (win) | E (our for reverse) */
    const char *psz = pszMode;
    char        ch;
    while ((ch = *psz++) != '\0')
    {
        switch (ch)
        {
            case 'a':
            case 'r':
            case 'w':
                chMode = ch;
                break;
            case '+':
                fPlus = true;
                break;
            case 'b':
                fBinary = true;
                break;
            case 't':
                fBinary = false;
                break;
            case 'x':
                fExclusive = true;
                break;
            case 'e':
            case 'N':
                fNoInherit = true;
                break;
            case 'E':
                fNoInherit = false;
                break;
            default:
                AssertMsgFailedReturn(("Invalid ch='%c' in pszMode='%s', '<a|r|w>[+][b|t][x][e|N|E]'\n", ch, pszMode),
                                      VERR_INVALID_FLAGS);
        }
    }

    /*
     * Translate into to RTFILE_O_* flags:
     */
    uint64_t fOpen;
    switch (chMode)
    {
        case 'a': fOpen = RTFILE_O_OPEN_CREATE    | RTFILE_O_WRITE | RTFILE_O_APPEND; break;
        case 'w': fOpen = !fExclusive
                        ? RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE
                        : RTFILE_O_CREATE         | RTFILE_O_WRITE; break;
        case 'r': fOpen = RTFILE_O_OPEN           | RTFILE_O_READ; break;
        default:  AssertMsgFailedReturn(("No main mode (a|r|w) specified in '%s'!\n", pszMode), VERR_INVALID_FLAGS);
    }
    AssertMsgReturn(!fExclusive || chMode == 'w', ("the 'x' flag is only allowed with 'w'! (%s)\n", pszMode),
                    VERR_INVALID_FLAGS);
    if (fExclusive)
        fOpen |= RTFILE_O_READ | RTFILE_O_WRITE;
    if (fPlus)
        fOpen |= RTFILE_O_READ | RTFILE_O_WRITE;
    if (!fNoInherit)
        fOpen |= RTFILE_O_INHERIT;
    fOpen |= RTFILE_O_DENY_NONE;
    fOpen |= 0666 << RTFILE_O_CREATE_MODE_SHIFT;

#ifndef RTSTREAM_STANDALONE
    /*
     * Normalize mode for fdopen.
     */
    char szNormalizedMode[8];
    szNormalizedMode[0] = chMode;
    size_t off = 1;
    if (fPlus)
        szNormalizedMode[off++] = '+';
    if (fBinary)
        szNormalizedMode[off++] = 'b';
    szNormalizedMode[off] = '\0';
#endif

#ifdef RTSTREAM_STANDALONE
    /*
     * Make the the stream list is initialized before we allocate anything.
     */
    int rc2 = RTOnce(&g_StreamListOnce, rtStrmListInitOnce, NULL);
    AssertRCReturn(rc2, rc2);
#endif

    /*
     * Allocate the stream handle and try open it.
     */
    int rc = VERR_NO_MEMORY;
    PRTSTREAM pStream = (PRTSTREAM)RTMemAllocZ(sizeof(*pStream));
    if (pStream)
    {
        pStream->u32Magic           = RTSTREAM_MAGIC;
#ifdef RTSTREAM_STANDALONE
        pStream->enmStdHandle       = RTHANDLESTD_INVALID;
        pStream->hFile              = NIL_RTFILE;
        pStream->pchBuf             = NULL;
        pStream->cbBufAlloc         = 0;
        pStream->offBufFirst        = 0;
        pStream->offBufEnd          = 0;
        pStream->enmBufDir          = RTSTREAMBUFDIR_NONE;
        pStream->enmBufStyle        = RTSTREAMBUFSTYLE_FULL;
# ifdef RTSTREAM_WITH_TEXT_MODE
        pStream->pbmBuf             = NULL;
        pStream->fPendingCr         = false,
# endif
#endif
        pStream->i32Error           = VINF_SUCCESS;
        pStream->fCurrentCodeSet    = false;
        pStream->fBinary            = fBinary;
        pStream->fRecheckMode       = false;
#ifndef HAVE_FWRITE_UNLOCKED
        pStream->pCritSect          = NULL;
#endif
        RTFILEACTION enmActionTaken = RTFILEACTION_INVALID;
        if (pszFilename)
            rc = RTFileOpenEx(pszFilename, fOpen, &hFile, &enmActionTaken);
        else
            rc = VINF_SUCCESS;
        if (RT_SUCCESS(rc))
        {
#ifndef RTSTREAM_STANDALONE
# ifndef _MSC_VER
            int fd = (int)RTFileToNative(hFile);
# else
            int fd = _open_osfhandle(RTFileToNative(hFile),
                                       (fPlus ? _O_RDWR : chMode == 'r' ? _O_RDONLY : _O_WRONLY)
                                     | (chMode == 'a' ? _O_APPEND : 0)
                                     | (fBinary ? _O_BINARY : _O_TEXT)
                                     | (fNoInherit ? _O_NOINHERIT : 0));
# endif
            if (fd >= 0)
            {
                pStream->pFile = fdopen(fd, szNormalizedMode);
                if (pStream->pFile)
#endif
                {
#ifdef RTSTREAM_STANDALONE
                    pStream->hFile = hFile;

                    /* We keep a list of these for cleanup purposes. */
                    RTCritSectEnter(&g_StreamListCritSect);
                    RTListAppend(&g_StreamList, &pStream->ListEntry);
                    RTCritSectLeave(&g_StreamListCritSect);
#endif
                    *ppStream = pStream;
                    return VINF_SUCCESS;
                }

                /*
                 * This better not happen too often as in 'w' mode we might've
                 * truncated a file, and in 'w' and 'a' modes there is a chance
                 * that we'll race other access to the file when deleting it.
                 */
#ifndef RTSTREAM_STANDALONE
                rc = RTErrConvertFromErrno(errno);
# ifdef _MSC_VER
                close(fd);
                hFile = NIL_RTFILE;
                /** @todo we're in trouble here when called from RTStrmOpenFileHandle!   */
# endif
            }
            else
            {
# ifdef _MSC_VER
                rc = RTErrConvertFromErrno(errno);
# else
                AssertFailedStmt(rc = VERR_INVALID_HANDLE);
# endif
            }
            if (pszFilename)
            {
                RTFileClose(hFile);
                if (enmActionTaken == RTFILEACTION_CREATED)
                    RTFileDelete(pszFilename);
            }
#endif
        }
        RTMemFree(pStream);
    }
    return rc;
}


RTR3DECL(int) RTStrmOpen(const char *pszFilename, const char *pszMode, PRTSTREAM *ppStream)
{
    *ppStream = NULL;
    AssertReturn(pszFilename, VERR_INVALID_PARAMETER);
    return rtStrmOpenComon(pszFilename, NIL_RTFILE, pszMode, ppStream);
}


RTR3DECL(int) RTStrmOpenFV(const char *pszMode, PRTSTREAM *ppStream, const char *pszFilenameFmt, va_list args)
{
    int     rc;
    char    szFilename[RTPATH_MAX];
    size_t  cch = RTStrPrintfV(szFilename, sizeof(szFilename), pszFilenameFmt, args);
    if (cch < sizeof(szFilename))
        rc = RTStrmOpen(szFilename, pszMode, ppStream);
    else
    {
        AssertMsgFailed(("The filename is too long cch=%d\n", cch));
        rc = VERR_FILENAME_TOO_LONG;
    }
    return rc;
}


RTR3DECL(int) RTStrmOpenF(const char *pszMode, PRTSTREAM *ppStream, const char *pszFilenameFmt, ...)
{
    va_list args;
    va_start(args, pszFilenameFmt);
    int rc = RTStrmOpenFV(pszMode, ppStream, pszFilenameFmt, args);
    va_end(args);
    return rc;
}


RTR3DECL(int) RTStrmOpenFileHandle(RTFILE hFile, const char *pszMode, uint32_t fFlags, PRTSTREAM *ppStream)
{
    *ppStream = NULL;
    AssertReturn(RTFileIsValid(hFile), VERR_INVALID_HANDLE);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);
    return rtStrmOpenComon(NULL, hFile, pszMode, ppStream);
}


RTR3DECL(int) RTStrmClose(PRTSTREAM pStream)
{
    /*
     * Validate input.
     */
    if (!pStream)
        return VINF_SUCCESS;
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_MAGIC);

    /* We don't implement closing any of the standard handles at present. */
    AssertReturn(pStream != &g_StdIn, VERR_NOT_SUPPORTED);
    AssertReturn(pStream != &g_StdOut, VERR_NOT_SUPPORTED);
    AssertReturn(pStream != &g_StdErr, VERR_NOT_SUPPORTED);

    /*
     * Invalidate the stream and destroy the critical section first.
     */
#ifdef RTSTREAM_STANDALONE
    RTCritSectEnter(&g_StreamListCritSect);
    RTListNodeRemove(&pStream->ListEntry);
    RTCritSectLeave(&g_StreamListCritSect);
#endif
    pStream->u32Magic = 0xdeaddead;
#ifndef HAVE_FWRITE_UNLOCKED
    if (pStream->pCritSect)
    {
        RTCritSectEnter(pStream->pCritSect);
        RTCritSectLeave(pStream->pCritSect);
        RTCritSectDelete(pStream->pCritSect);
        RTMemFree(pStream->pCritSect);
        pStream->pCritSect = NULL;
    }
#endif

    /*
     * Flush and close the underlying file.
     */
#ifdef RTSTREAM_STANDALONE
    int const rc1 = RTStrmFlush(pStream);
    AssertRC(rc1);
    int const rc2 = RTFileClose(pStream->hFile);
    AssertRC(rc2);
    int const rc = RT_SUCCESS(rc1) ? rc2 : rc1;
#else
    int const rc = !fclose(pStream->pFile) ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
#endif

    /*
     * Destroy the stream.
     */
#ifdef RTSTREAM_STANDALONE
    pStream->hFile          = NIL_RTFILE;
    RTMemFree(pStream->pchBuf);
    pStream->pchBuf         = NULL;
    pStream->cbBufAlloc     = 0;
    pStream->offBufFirst    = 0;
    pStream->offBufEnd      = 0;
# ifdef RTSTREAM_WITH_TEXT_MODE
    RTMemFree(pStream->pbmBuf);
    pStream->pbmBuf         = NULL;
# endif
#else
    pStream->pFile          = NULL;
#endif
    RTMemFree(pStream);
    return rc;
}


RTR3DECL(int) RTStrmError(PRTSTREAM pStream)
{
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_MAGIC);
    return pStream->i32Error;
}


RTR3DECL(int) RTStrmClearError(PRTSTREAM pStream)
{
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_MAGIC);

#ifndef RTSTREAM_STANDALONE
    clearerr(pStream->pFile);
#endif
    ASMAtomicWriteS32(&pStream->i32Error, VINF_SUCCESS);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTStrmSetMode(PRTSTREAM pStream, int fBinary, int fCurrentCodeSet)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn((unsigned)(fBinary + 1) <= 2, VERR_INVALID_PARAMETER);
    AssertReturn((unsigned)(fCurrentCodeSet + 1) <= 2, VERR_INVALID_PARAMETER);

    rtStrmLock(pStream);

    if (fBinary != -1)
    {
        pStream->fBinary      = RT_BOOL(fBinary);
        pStream->fRecheckMode = true;
    }

    if (fCurrentCodeSet != -1)
        pStream->fCurrentCodeSet = RT_BOOL(fCurrentCodeSet);

    rtStrmUnlock(pStream);

    return VINF_SUCCESS;
}


RTR3DECL(int) RTStrmSetBufferingMode(PRTSTREAM pStream, RTSTRMBUFMODE enmMode)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(enmMode > RTSTRMBUFMODE_INVALID && enmMode < RTSTRMBUFMODE_END, VERR_INVALID_PARAMETER);

#ifndef RTSTREAM_STANDALONE
    int iCrtMode = enmMode == RTSTRMBUFMODE_FULL ? _IOFBF : enmMode == RTSTRMBUFMODE_LINE ? _IOLBF : _IONBF;
    int rc = setvbuf(pStream->pFile, NULL, iCrtMode, 0);
    if (rc >= 0)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);

#else
    rtStrmLock(pStream);
    pStream->enmBufStyle = enmMode == RTSTRMBUFMODE_FULL ? RTSTREAMBUFSTYLE_FULL
                         : enmMode == RTSTRMBUFMODE_LINE ? RTSTREAMBUFSTYLE_LINE : RTSTREAMBUFSTYLE_UNBUFFERED;
    rtStrmUnlock(pStream);
    return VINF_SUCCESS;
#endif
}


#ifdef RTSTREAM_STANDALONE

/**
 * Deals with NIL_RTFILE in rtStrmGetFile.
 */
DECL_NO_INLINE(static, RTFILE) rtStrmGetFileNil(PRTSTREAM pStream)
{
# ifdef RT_OS_WINDOWS
    DWORD dwStdHandle;
    switch (pStream->enmStdHandle)
    {
        case RTHANDLESTD_INPUT:     dwStdHandle = STD_INPUT_HANDLE; break;
        case RTHANDLESTD_OUTPUT:    dwStdHandle = STD_OUTPUT_HANDLE; break;
        case RTHANDLESTD_ERROR:     dwStdHandle = STD_ERROR_HANDLE; break;
        default:                    return NIL_RTFILE;
    }
    HANDLE hHandle = GetStdHandle(dwStdHandle);
    if (hHandle != INVALID_HANDLE_VALUE && hHandle != NULL)
    {
        int rc = RTFileFromNative(&pStream->hFile, (uintptr_t)hHandle);
        if (RT_SUCCESS(rc))
        {
            /* Switch to full buffering if not a console handle. */
            DWORD dwMode;
            if (!GetConsoleMode(hHandle, &dwMode))
                pStream->enmBufStyle = RTSTREAMBUFSTYLE_FULL;

            return pStream->hFile;
        }
    }

# else
    uintptr_t uNative;
    switch (pStream->enmStdHandle)
    {
        case RTHANDLESTD_INPUT:     uNative = RTFILE_NATIVE_STDIN; break;
        case RTHANDLESTD_OUTPUT:    uNative = RTFILE_NATIVE_STDOUT; break;
        case RTHANDLESTD_ERROR:     uNative = RTFILE_NATIVE_STDERR; break;
        default:                    return NIL_RTFILE;
    }
    int rc = RTFileFromNative(&pStream->hFile, uNative);
    if (RT_SUCCESS(rc))
    {
        /* Switch to full buffering if not a console handle. */
        if (!isatty((int)uNative))
            pStream->enmBufStyle = RTSTREAMBUFDIR_FULL;

        return pStream->hFile;
    }

# endif
    return NIL_RTFILE;
}


/**
 * For lazily resolving handles for the standard streams.
 */
DECLINLINE(RTFILE) rtStrmGetFile(PRTSTREAM pStream)
{
    RTFILE hFile = pStream->hFile;
    if (hFile != NIL_RTFILE)
        return hFile;
    return rtStrmGetFileNil(pStream);
}


RTR3DECL(int) RTStrmQueryFileHandle(PRTSTREAM pStream, PRTFILE phFile)
{
    AssertPtrReturn(phFile, VERR_INVALID_POINTER);
    *phFile = NIL_RTFILE;
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_MAGIC);

    rtStrmLock(pStream);
    RTFILE hFile = rtStrmGetFile(pStream);
    rtStrmUnlock(pStream);
    if (hFile != NIL_RTFILE)
    {
        *phFile = hFile;
        return VINF_SUCCESS;
    }
    return VERR_NOT_AVAILABLE;
}

#endif /* RTSTREAM_STANDALONE */


/**
 * Wrapper around isatty, assumes caller takes care of stream locking/whatever
 * is needed.
 */
DECLINLINE(bool) rtStrmIsTerminal(PRTSTREAM pStream)
{
#ifdef RTSTREAM_STANDALONE
    RTFILE hFile = rtStrmGetFile(pStream);
    if (hFile != NIL_RTFILE)
    {
        HANDLE hNative = (HANDLE)RTFileToNative(hFile);
        DWORD dwType = GetFileType(hNative);
        if (dwType == FILE_TYPE_CHAR)
        {
            DWORD dwMode;
            if (GetConsoleMode(hNative, &dwMode))
                return true;
        }
    }
    return false;

#else
    if (pStream->pFile)
    {
        int fh = fileno(pStream->pFile);
        if (isatty(fh) != 0)
        {
# ifdef RT_OS_WINDOWS
            DWORD  dwMode;
            HANDLE hCon = (HANDLE)_get_osfhandle(fh);
            if (GetConsoleMode(hCon, &dwMode))
                return true;
# else
            return true;
# endif
        }
    }
    return false;
#endif
}


static int rtStrmInputGetEchoCharsNative(uintptr_t hNative, bool *pfEchoChars)
{
#ifdef RT_OS_WINDOWS
    DWORD dwMode;
    if (GetConsoleMode((HANDLE)hNative, &dwMode))
        *pfEchoChars = RT_BOOL(dwMode & ENABLE_ECHO_INPUT);
    else
    {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_INVALID_HANDLE)
            return GetFileType((HANDLE)hNative) != FILE_TYPE_UNKNOWN ? VERR_INVALID_FUNCTION : VERR_INVALID_HANDLE;
        return RTErrConvertFromWin32(dwErr);
    }
#else
    struct termios Termios;
    int rcPosix = tcgetattr((int)hNative, &Termios);
    if (!rcPosix)
        *pfEchoChars = RT_BOOL(Termios.c_lflag & ECHO);
    else
        return errno == ENOTTY ? VERR_INVALID_FUNCTION :  RTErrConvertFromErrno(errno);
#endif
    return VINF_SUCCESS;
}



RTR3DECL(int) RTStrmInputGetEchoChars(PRTSTREAM pStream, bool *pfEchoChars)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfEchoChars, VERR_INVALID_POINTER);

#ifdef RTSTREAM_STANDALONE
    return rtStrmInputGetEchoCharsNative(RTFileToNative(pStream->hFile), pfEchoChars);
#else
    int rc;
    int fh = fileno(pStream->pFile);
    if (isatty(fh))
    {
# ifdef RT_OS_WINDOWS
        rc = rtStrmInputGetEchoCharsNative(_get_osfhandle(fh), pfEchoChars);
# else
        rc = rtStrmInputGetEchoCharsNative(fh, pfEchoChars);
# endif
    }
    else
        rc = VERR_INVALID_FUNCTION;
    return rc;
#endif
}


static int rtStrmInputSetEchoCharsNative(uintptr_t hNative, bool fEchoChars)
{
    int rc;
#ifdef RT_OS_WINDOWS
    DWORD dwMode;
    if (GetConsoleMode((HANDLE)hNative, &dwMode))
    {
        if (fEchoChars)
            dwMode |= ENABLE_ECHO_INPUT;
        else
            dwMode &= ~ENABLE_ECHO_INPUT;
        if (SetConsoleMode((HANDLE)hNative, dwMode))
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
    {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_INVALID_HANDLE)
            return GetFileType((HANDLE)hNative) != FILE_TYPE_UNKNOWN ? VERR_INVALID_FUNCTION : VERR_INVALID_HANDLE;
        return RTErrConvertFromWin32(dwErr);
    }
#else
    struct termios Termios;
    int rcPosix = tcgetattr((int)hNative, &Termios);
    if (!rcPosix)
    {
        if (fEchoChars)
            Termios.c_lflag |= ECHO;
        else
            Termios.c_lflag &= ~ECHO;

        rcPosix = tcsetattr((int)hNative, TCSAFLUSH, &Termios);
        if (rcPosix == 0)
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno(errno);
    }
    else
        rc = errno == ENOTTY ? VERR_INVALID_FUNCTION : RTErrConvertFromErrno(errno);
#endif
    return rc;
}


RTR3DECL(int) RTStrmInputSetEchoChars(PRTSTREAM pStream, bool fEchoChars)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSTREAM_STANDALONE
    return rtStrmInputSetEchoCharsNative(RTFileToNative(pStream->hFile), fEchoChars);
#else
    int rc;
    int fh = fileno(pStream->pFile);
    if (isatty(fh))
    {
# ifdef RT_OS_WINDOWS
        rc = rtStrmInputSetEchoCharsNative(_get_osfhandle(fh), fEchoChars);
# else
        rc = rtStrmInputSetEchoCharsNative(fh, fEchoChars);
# endif
    }
    else
        rc = VERR_INVALID_FUNCTION;
    return rc;
#endif
}


RTR3DECL(bool) RTStrmIsTerminal(PRTSTREAM pStream)
{
    AssertPtrReturn(pStream, false);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, false);

    return rtStrmIsTerminal(pStream);
}


RTR3DECL(int) RTStrmQueryTerminalWidth(PRTSTREAM pStream, uint32_t *pcchWidth)
{
    AssertPtrReturn(pcchWidth, VERR_INVALID_HANDLE);
    *pcchWidth = 80;

    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);

    if (rtStrmIsTerminal(pStream))
    {
#ifdef RT_OS_WINDOWS
# ifdef RTSTREAM_STANDALONE
        HANDLE hCon = (HANDLE)RTFileToNative(pStream->hFile);
# else
        HANDLE hCon = (HANDLE)_get_osfhandle(fileno(pStream->pFile));
# endif
        CONSOLE_SCREEN_BUFFER_INFO Info;
        RT_ZERO(Info);
        if (GetConsoleScreenBufferInfo(hCon, &Info))
        {
            *pcchWidth = Info.dwSize.X ? Info.dwSize.X : 80;
            return VINF_SUCCESS;
        }
        return RTErrConvertFromWin32(GetLastError());

#elif defined(RT_OS_OS2) && !defined(TIOCGWINSZ) /* only OS/2 should currently miss this */
        return VINF_SUCCESS; /* just pretend for now. */

#else
        struct winsize Info;
        RT_ZERO(Info);
        int rc = ioctl(fileno(pStream->pFile), TIOCGWINSZ, &Info);
        if (rc >= 0)
        {
            *pcchWidth = Info.ws_col ? Info.ws_col : 80;
            return VINF_SUCCESS;
        }
        return RTErrConvertFromErrno(errno);
#endif
    }
    return VERR_INVALID_FUNCTION;
}


#ifdef RTSTREAM_STANDALONE

DECLINLINE(void) rtStrmBufInvalidate(PRTSTREAM pStream)
{
    pStream->enmBufDir   = RTSTREAMBUFDIR_NONE;
    pStream->offBufEnd   = 0;
    pStream->offBufFirst = 0;
}


static int rtStrmBufFlushWrite(PRTSTREAM pStream, size_t cbToFlush)
{
    Assert(cbToFlush <= pStream->offBufEnd - pStream->offBufFirst);

    RTFILE const hFile = rtStrmGetFile(pStream);
    if (hFile != NIL_RTFILE)
    {
        /** @todo do nonblocking & incomplete writes?   */
        size_t offBufFirst = pStream->offBufFirst;
        int rc = RTFileWrite(hFile, &pStream->pchBuf[offBufFirst], cbToFlush, NULL);
        if (RT_SUCCESS(rc))
        {
            offBufFirst += cbToFlush;
            if (offBufFirst >= pStream->offBufEnd)
                pStream->offBufEnd = 0;
            else
            {
                /* Shift up the remaining content so the next write can take full
                   advantage of the buffer size. */
                size_t cbLeft = pStream->offBufEnd - offBufFirst;
                memmove(pStream->pchBuf, &pStream->pchBuf[offBufFirst], cbLeft);
                pStream->offBufEnd = cbLeft;
            }
            pStream->offBufFirst = 0;
            return VINF_SUCCESS;
        }
        return rc;
    }
    return VERR_INVALID_HANDLE;
}


static int rtStrmBufFlushWriteMaybe(PRTSTREAM pStream, bool fInvalidate)
{
    if (pStream->enmBufDir == RTSTREAMBUFDIR_WRITE)
    {
        size_t cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
        if (cbInBuffer > 0)
        {
            int rc = rtStrmBufFlushWrite(pStream, cbInBuffer);
            if (fInvalidate)
                pStream->enmBufDir = RTSTREAMBUFDIR_NONE;
            return rc;
        }
    }
    if (fInvalidate)
        rtStrmBufInvalidate(pStream);
    return VINF_SUCCESS;
}


/**
 * Worker for rtStrmBufCheckErrorAndSwitchToReadMode and
 * rtStrmBufCheckErrorAndSwitchToWriteMode that allocates a buffer.
 *
 * Only updates cbBufAlloc and pchBuf, callers deals with error fallout.
 */
static int rtStrmBufAlloc(PRTSTREAM pStream)
{
    size_t cbBuf = pStream->enmBufStyle == RTSTREAMBUFSTYLE_FULL ? _64K : _16K;
    do
    {
        pStream->pchBuf = (char *)RTMemAllocZ(cbBuf);
        if (RT_LIKELY(pStream->pchBuf))
        {
# ifdef RTSTREAM_WITH_TEXT_MODE
            Assert(RT_ALIGN_Z(cbBuf, 64 / 8) == cbBuf);
            pStream->pbmBuf = (uint32_t *)RTMemAllocZ(cbBuf / 8);
            if (RT_LIKELY(pStream->pbmBuf))
# endif
            {
                pStream->cbBufAlloc = cbBuf;
                return VINF_SUCCESS;
            }
# ifdef RTSTREAM_WITH_TEXT_MODE
            RTMemFree(pStream->pchBuf);
            pStream->pchBuf = NULL;
# endif
        }
        cbBuf /= 2;
    } while (cbBuf >= 256);
    return VERR_NO_MEMORY;
}


/**
 * Checks the stream error status, flushed any pending writes, ensures there is
 * a buffer allocated and switches the stream to the read direction.
 *
 * @returns IPRT status code (same as i32Error).
 * @param   pStream             The stream.
 */
static int rtStrmBufCheckErrorAndSwitchToReadMode(PRTSTREAM pStream)
{
    int rc = pStream->i32Error;
    if (RT_SUCCESS(rc))
    {
        /*
         * We're very likely already in read mode and can return without doing
         * anything here.
         */
        if (pStream->enmBufDir == RTSTREAMBUFDIR_READ)
            return VINF_SUCCESS;

        /*
         * Flush any pending writes before switching the buffer to read:
         */
        rc = rtStrmBufFlushWriteMaybe(pStream, false /*fInvalidate*/);
        if (RT_SUCCESS(rc))
        {
            pStream->enmBufDir   = RTSTREAMBUFDIR_READ;
            pStream->offBufEnd   = 0;
            pStream->offBufFirst = 0;
            pStream->fPendingCr  = false;

            /*
             * Read direction implies a buffer, so make sure we've got one and
             * change to NONE direction if allocating one fails.
             */
            if (pStream->pchBuf)
            {
                Assert(pStream->cbBufAlloc >= 256);
                return VINF_SUCCESS;
            }

            rc = rtStrmBufAlloc(pStream);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            pStream->enmBufDir = RTSTREAMBUFDIR_NONE;
        }
        ASMAtomicWriteS32(&pStream->i32Error, rc);
    }
    return rc;
}


/**
 * Checks the stream error status, ensures there is a buffer allocated and
 * switches the stream to the write direction.
 *
 * @returns IPRT status code (same as i32Error).
 * @param   pStream             The stream.
 */
static int rtStrmBufCheckErrorAndSwitchToWriteMode(PRTSTREAM pStream)
{
    int rc = pStream->i32Error;
    if (RT_SUCCESS(rc))
    {
        /*
         * We're very likely already in write mode and can return without doing
         * anything here.
         */
        if (pStream->enmBufDir == RTSTREAMBUFDIR_WRITE)
            return VINF_SUCCESS;

        /*
         * A read buffer does not need any flushing, so we just have to make
         * sure there is a buffer present before switching to the write direction.
         */
        pStream->enmBufDir   = RTSTREAMBUFDIR_WRITE;
        pStream->offBufEnd   = 0;
        pStream->offBufFirst = 0;
        if (pStream->pchBuf)
        {
            Assert(pStream->cbBufAlloc >= 256);
            return VINF_SUCCESS;
        }

        rc = rtStrmBufAlloc(pStream);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        pStream->enmBufDir = RTSTREAMBUFDIR_NONE;
        ASMAtomicWriteS32(&pStream->i32Error, rc);
    }
    return rc;
}


/**
 * Reads more bytes into the buffer.
 *
 * @returns IPRT status code (same as i32Error).
 * @param   pStream             The stream.
 */
static int rtStrmBufFill(PRTSTREAM pStream)
{
    /*
     * Check preconditions
     */
    Assert(pStream->i32Error    == VINF_SUCCESS);
    Assert(pStream->enmBufDir   == RTSTREAMBUFDIR_READ);
    AssertPtr(pStream->pchBuf);
    Assert(pStream->cbBufAlloc  >= 256);
    Assert(RT_ALIGN_Z(pStream->cbBufAlloc, 64) == pStream->cbBufAlloc);
    Assert(pStream->offBufFirst <= pStream->cbBufAlloc);
    Assert(pStream->offBufEnd   <= pStream->cbBufAlloc);
    Assert(pStream->offBufFirst <= pStream->offBufEnd);
# ifdef RTSTREAM_WITH_TEXT_MODE
    AssertPtr(pStream->pbmBuf);
# endif
    /*
     * If there is data in the buffer, move it up to the start.
     */
    size_t cbInBuffer;
    if (!pStream->offBufFirst)
        cbInBuffer = pStream->offBufEnd;
    else
    {
        cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
        if (cbInBuffer)
        {
            memmove(pStream->pchBuf, &pStream->pchBuf[pStream->offBufFirst], cbInBuffer);
# ifdef RTSTREAM_WITH_TEXT_MODE
            if (!pStream->fBinary) /** @todo this isn't very efficient, must be a better way of shifting a bitmap. */
                for (size_t off = 0; off < pStream->offBufFirst; off++)
                    if (ASMBitTest(pStream->pbmBuf, (int32_t)off))
                        ASMBitSet(pStream->pbmBuf, (int32_t)off);
                    else
                        ASMBitClear(pStream->pbmBuf, (int32_t)off);
# endif
        }
        pStream->offBufFirst = 0;
        pStream->offBufEnd   = cbInBuffer;
    }

    /*
     * Add pending CR to the buffer.
     */
    size_t const offCrLfConvStart = cbInBuffer;
    Assert(cbInBuffer + 2 <= pStream->cbBufAlloc);
    if (!pStream->fPendingCr || pStream->fBinary)
    { /* likely */ }
    else
    {
        pStream->pchBuf[cbInBuffer] = '\r';
        pStream->fPendingCr         = false;
        pStream->offBufEnd          = ++cbInBuffer;
    }

    /*
     * Read data till the buffer is full.
     */
    int          rc     = VERR_INVALID_HANDLE;
    RTFILE const hFile  = rtStrmGetFile(pStream);
    if (hFile != NIL_RTFILE)
    {
        size_t   cbRead = 0;
        rc = RTFileRead(hFile, &pStream->pchBuf[cbInBuffer], pStream->cbBufAlloc - cbInBuffer, &cbRead);
        if (RT_SUCCESS(rc))
        {
            cbInBuffer        += cbRead;
            pStream->offBufEnd = cbInBuffer;

            if (cbInBuffer != 0)
            {
# ifdef RTSTREAM_WITH_TEXT_MODE
            if (pStream->fBinary)
# endif
                return VINF_SUCCESS;
            }
            else
            {
                /** @todo this shouldn't be sticky, should it? */
                ASMAtomicWriteS32(&pStream->i32Error, VERR_EOF);
                return VERR_EOF;
            }

# ifdef RTSTREAM_WITH_TEXT_MODE
            /*
             * Do CRLF -> LF conversion in the buffer.
             */
            ASMBitClearRange(pStream->pbmBuf, offCrLfConvStart, RT_ALIGN_Z(cbInBuffer, 64));
            char  *pchCur = &pStream->pchBuf[offCrLfConvStart];
            size_t cbLeft = cbInBuffer - offCrLfConvStart;
            while (cbLeft > 0)
            {
                Assert(&pchCur[cbLeft] == &pStream->pchBuf[pStream->offBufEnd]);
                char *pchCr = (char *)memchr(pchCur, '\r', cbLeft);
                if (pchCr)
                {
                    size_t offCur = (size_t)(pchCr - pchCur);
                    if (offCur + 1 < cbLeft)
                    {
                        if (pchCr[1] == '\n')
                        {
                            /* Found one '\r\n' sequence. Look for more before shifting the buffer content. */
                            cbLeft -= offCur;
                            pchCur  = pchCr;

                            do
                            {
                                ASMBitSet(pStream->pbmBuf, (int32_t)(pchCur - pStream->pchBuf));
                                *pchCur++  = '\n'; /* dst */
                                cbLeft    -= 2;
                                pchCr     += 2;    /* src */
                            } while  (cbLeft >= 2 && pchCr[0] == '\r' && pchCr[1] == '\n');

                            memmove(&pchCur, pchCr, cbLeft);
                        }
                        else
                        {
                            cbLeft -= offCur + 1;
                            pchCur  = pchCr  + 1;
                        }
                    }
                    else
                    {
                        Assert(pchCr == &pStream->pchBuf[pStream->offBufEnd - 1]);
                        pStream->fPendingCr = true;
                        pStream->offBufEnd  = --cbInBuffer;
                        break;
                    }
                }
                else
                    break;
            }

            return VINF_SUCCESS;
# endif
        }
    }

    /*
     * If there is data in the buffer, don't raise the error till it has all
     * been consumed, ASSUMING that another fill call will follow and that the
     * error condition will reoccur then.
     *
     * Note! We may currently end up not converting a CRLF pair, if it's
     *       split over a temporary EOF condition, since we forces the caller
     *       to read the CR before requesting more data.  However, it's not a
     *       very likely scenario, so we'll just leave it like that for now.
     */
    if (cbInBuffer)
        return VINF_SUCCESS;
    ASMAtomicWriteS32(&pStream->i32Error, rc);
    return rc;
}


/**
 * Copies @a cbSrc bytes from @a pvSrc and into the buffer, flushing as needed
 * to make space available.
 *
 *
 * @returns IPRT status code (errors not assigned to i32Error).
 * @param   pStream             The stream.
 * @param   pvSrc               The source buffer.
 * @param   cbSrc               Number of bytes to copy from @a pvSrc.
 * @param   pcbTotal            A total counter to update with what was copied.
 */
static int rtStrmBufCopyTo(PRTSTREAM pStream, const void *pvSrc, size_t cbSrc, size_t *pcbTotal)
{
    Assert(cbSrc > 0);
    for (;;)
    {
        size_t cbToCopy = RT_MIN(pStream->cbBufAlloc - pStream->offBufEnd, cbSrc);
        if (cbToCopy)
        {
            memcpy(&pStream->pchBuf[pStream->offBufEnd], pvSrc, cbToCopy);
            pStream->offBufEnd += cbToCopy;
            pvSrc               = (const char *)pvSrc + cbToCopy;
            *pcbTotal          += cbToCopy;
            cbSrc              -= cbToCopy;
            if (!cbSrc)
                break;
        }

        int rc = rtStrmBufFlushWrite(pStream, pStream->offBufEnd - pStream->offBufFirst);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Worker for rtStrmFlushAndCloseAll and rtStrmFlushAndClose.
 */
static RTFILE rtStrmFlushAndCleanup(PRTSTREAM pStream)
{
    if (pStream->pchBuf)
    {
        if (   pStream->enmBufDir == RTSTREAMBUFDIR_WRITE
            && pStream->offBufFirst < pStream->offBufEnd
            && RT_SUCCESS(pStream->i32Error) )
            rtStrmBufFlushWrite(pStream, pStream->offBufEnd - pStream->offBufFirst);
        RTMemFree(pStream->pchBuf);
        pStream->pchBuf      = NULL;
        pStream->offBufFirst = 0;
        pStream->offBufEnd   = 0;
# ifdef RTSTREAM_WITH_TEXT_MODE
        RTMemFree(pStream->pbmBuf);
        pStream->pbmBuf = NULL;
# endif
    }

    PRTCRITSECT pCritSect = pStream->pCritSect;
    if (pCritSect)
    {
        pStream->pCritSect = NULL;
        RTCritSectDelete(pCritSect);
        RTMemFree(pCritSect);
    }

    RTFILE hFile = pStream->hFile;
    pStream->hFile = NIL_RTFILE;
    return hFile;
}


/**
 * Worker for rtStrmFlushAndCloseAll.
 */
static void rtStrmFlushAndClose(PRTSTREAM pStream)
{
    pStream->u32Magic = ~RTSTREAM_MAGIC;
    RTFILE hFile = rtStrmFlushAndCleanup(pStream);
    if (hFile != NIL_RTFILE)
        RTFileClose(hFile);
    RTMemFree(pStream);
}


/**
 * Flushes and cleans up the standard streams, should flush and close all others
 * too but doesn't yet...
 */
DECLCALLBACK(void) rtStrmFlushAndCloseAll(void)
{
    /*
     * Flush the standard handles.
     */
    rtStrmFlushAndCleanup(&g_StdOut);
    rtStrmFlushAndCleanup(&g_StdErr);
    rtStrmFlushAndCleanup(&g_StdIn);

    /*
     * Make a list of the rest and flush+close those too.
     */
    if (RTOnceWasInitialized(&g_StreamListOnce))
    {
        RTCritSectDelete(&g_StreamListCritSect);

        PRTSTREAM pStream;
        while ((pStream = RTListRemoveFirst(&g_StreamList, RTSTREAM, ListEntry)) != NULL)
            rtStrmFlushAndClose(pStream);

        RTOnceReset(&g_StreamListOnce);
    }
}

# ifdef IPRT_COMPILER_TERM_CALLBACK
IPRT_COMPILER_TERM_CALLBACK(rtStrmFlushAndCloseAll);
# endif

#endif /* RTSTREAM_STANDALONE */


RTR3DECL(int) RTStrmRewind(PRTSTREAM pStream)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSTREAM_STANDALONE
    rtStrmLock(pStream);
    int const rc1 = rtStrmBufFlushWriteMaybe(pStream, true /*fInvalidate*/);
    int const rc2 = RTFileSeek(rtStrmGetFile(pStream), 0, RTFILE_SEEK_BEGIN, NULL);
    int rc = RT_SUCCESS(rc1) ? rc2 : rc1;
    ASMAtomicWriteS32(&pStream->i32Error, rc);
    rtStrmUnlock(pStream);
#else
    clearerr(pStream->pFile);
    errno = 0;
    int rc;
    if (!fseek(pStream->pFile, 0, SEEK_SET))
        rc = VINF_SUCCESS;
    else
        rc = RTErrConvertFromErrno(errno);
    ASMAtomicWriteS32(&pStream->i32Error, rc);
#endif
    return rc;
}


RTR3DECL(int) RTStrmSeek(PRTSTREAM pStream, RTFOFF off, uint32_t uMethod)
{
    AssertReturn(uMethod <= RTFILE_SEEK_END, VERR_INVALID_PARAMETER);
#ifdef RTSTREAM_STANDALONE
    rtStrmLock(pStream);
    int rc = rtStrmBufFlushWriteMaybe(pStream, true /*fInvalidate*/);
    if (RT_SUCCESS(rc))
        rc = RTFileSeek(rtStrmGetFile(pStream), off, uMethod, NULL);
    if (RT_FAILURE(rc))
        ASMAtomicWriteS32(&pStream->i32Error, rc);
    rtStrmUnlock(pStream);
#else
    int const iCrtMethod = uMethod == RTFILE_SEEK_BEGIN ? SEEK_SET : uMethod == RTFILE_SEEK_CURRENT ? SEEK_CUR : SEEK_END;
    errno = 0;
    int rc;
# ifdef _MSC_VER
    if (!_fseeki64(pStream->pFile, off, iCrtMethod))
# else
    if (!fseeko(pStream->pFile, off, iCrtMethod))
# endif
        rc = VINF_SUCCESS;
    else
        rc = RTErrConvertFromErrno(errno);
    ASMAtomicWriteS32(&pStream->i32Error, rc);
#endif
    return rc;
}


RTR3DECL(RTFOFF) RTStrmTell(PRTSTREAM pStream)
{
#ifdef RTSTREAM_STANDALONE
    uint64_t off = 0;
    rtStrmLock(pStream);
    int rc = pStream->i32Error;
    if (RT_SUCCESS(rc))
    {
        RTFILE const hFile = rtStrmGetFile(pStream);
        if (hFile != NIL_RTFILE)
        {
            rc = RTFileSeek(hFile, 0, RTFILE_SEEK_CURRENT, &off);
            if (RT_SUCCESS(rc))
            {
                switch (pStream->enmBufDir)
                {
                    case RTSTREAMBUFDIR_READ:
                        /* Subtract unconsumed chars and removed '\r' characters. */
                        off -= pStream->offBufEnd - pStream->offBufFirst;
                        if (!pStream->fBinary)
                            for (size_t offBuf = pStream->offBufFirst; offBuf < pStream->offBufEnd; offBuf++)
                                off -= ASMBitTest(pStream->pbmBuf, (int32_t)offBuf);
                        break;
                    case RTSTREAMBUFDIR_WRITE:
                        /* Add unwrittend chars in the buffer. */
                        off += pStream->offBufEnd - pStream->offBufFirst;
                        break;
                    default:
                        AssertFailed();
                    case RTSTREAMBUFDIR_NONE:
                        break;
                }
            }
        }
        else
            rc = VERR_INVALID_HANDLE;
    }
    if (RT_FAILURE(rc))
    {
        ASMAtomicWriteS32(&pStream->i32Error, rc);
        off = rc;
    }
    rtStrmUnlock(pStream);
#else
# ifdef _MSC_VER
    RTFOFF off = _ftelli64(pStream->pFile);
# else
    RTFOFF off = ftello(pStream->pFile);
# endif
    if (off < 0)
    {
        int rc = RTErrConvertFromErrno(errno);
        ASMAtomicWriteS32(&pStream->i32Error, rc);
        off = rc;
    }
#endif
    return off;
}


/**
 * Recheck the stream mode.
 *
 * @param   pStream             The stream (locked).
 */
static void rtStreamRecheckMode(PRTSTREAM pStream)
{
#if (defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)) && !defined(RTSTREAM_STANDALONE)
    int fh = fileno(pStream->pFile);
    if (fh >= 0)
    {
        int fExpected = pStream->fBinary ? _O_BINARY : _O_TEXT;
        int fActual   = _setmode(fh, fExpected);
        if (fActual != -1 && fExpected != (fActual & (_O_BINARY | _O_TEXT)))
        {
            fActual = _setmode(fh, fActual & (_O_BINARY | _O_TEXT));
            pStream->fBinary = !(fActual & _O_TEXT);
        }
    }
#else
    NOREF(pStream);
#endif
    pStream->fRecheckMode = false;
}


RTR3DECL(int) RTStrmReadEx(PRTSTREAM pStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSTREAM_STANDALONE
    rtStrmLock(pStream);
    int rc = rtStrmBufCheckErrorAndSwitchToReadMode(pStream);
#else
    int rc = pStream->i32Error;
#endif
    if (RT_SUCCESS(rc))
    {
        if (pStream->fRecheckMode)
            rtStreamRecheckMode(pStream);

#ifdef RTSTREAM_STANDALONE

        /*
         * Copy data thru the read buffer for now as that'll handle both binary
         * and text modes seamlessly.  We could optimize larger reads here when
         * in binary mode, that can wait till the basics work, I think.
         */
        size_t cbTotal = 0;
        if (cbToRead > 0)
            for (;;)
            {
                size_t cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
                if (cbInBuffer > 0)
                {
                    size_t cbToCopy = RT_MIN(cbInBuffer, cbToRead);
                    memcpy(pvBuf, &pStream->pchBuf[pStream->offBufFirst], cbToCopy);
                    cbTotal  += cbToRead;
                    cbToRead -= cbToCopy;
                    pvBuf     = (char *)pvBuf + cbToCopy;
                    if (!cbToRead)
                        break;
                }
                rc = rtStrmBufFill(pStream);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else
                {
                    if (rc == VERR_EOF && pcbRead && cbTotal > 0)
                        rc = VINF_EOF;
                    break;
                }
            }
        if (pcbRead)
            *pcbRead = cbTotal;

#else  /* !RTSTREAM_STANDALONE */
        if (pcbRead)
        {
            /*
             * Can do with a partial read.
             */
            *pcbRead = fread(pvBuf, 1, cbToRead, pStream->pFile);
            if (    *pcbRead == cbToRead
                || !ferror(pStream->pFile))
                rc = VINF_SUCCESS;
            else if (feof(pStream->pFile))
                rc = *pcbRead ? VINF_EOF : VERR_EOF;
            else if (ferror(pStream->pFile))
                rc = VERR_READ_ERROR;
            else
            {
                AssertMsgFailed(("This shouldn't happen\n"));
                rc = VERR_INTERNAL_ERROR;
            }
        }
        else
        {
            /*
             * Must read it all!
             */
            if (fread(pvBuf, cbToRead, 1, pStream->pFile) == 1)
                rc = VINF_SUCCESS;
            /* possible error/eof. */
            else if (feof(pStream->pFile))
                rc = VERR_EOF;
            else if (ferror(pStream->pFile))
                rc = VERR_READ_ERROR;
            else
            {
                AssertMsgFailed(("This shouldn't happen\n"));
                rc = VERR_INTERNAL_ERROR;
            }
        }
#endif /* !RTSTREAM_STANDALONE */
        if (RT_FAILURE(rc))
            ASMAtomicWriteS32(&pStream->i32Error, rc);
    }
#ifdef RTSTREAM_STANDALONE
    rtStrmUnlock(pStream);
#endif
    return rc;
}


/**
 * Check if the input text is valid UTF-8.
 *
 * @returns true/false.
 * @param   pvBuf               Pointer to the buffer.
 * @param   cbBuf               Size of the buffer.
 */
static bool rtStrmIsUtf8Text(const void *pvBuf, size_t cbBuf)
{
    NOREF(pvBuf);
    NOREF(cbBuf);
    /** @todo not sure this is a good idea... Better redefine RTStrmWrite. */
    return false;
}


#if defined(RT_OS_WINDOWS) && !defined(RTSTREAM_STANDALONE)

/**
 * Check if the stream is for a Window console.
 *
 * @returns true / false.
 * @param   pStream             The stream.
 * @param   phCon               Where to return the console handle.
 */
static bool rtStrmIsConsoleUnlocked(PRTSTREAM pStream, HANDLE *phCon)
{
    int fh = fileno(pStream->pFile);
    if (isatty(fh))
    {
        DWORD dwMode;
        HANDLE hCon = (HANDLE)_get_osfhandle(fh);
        if (GetConsoleMode(hCon, &dwMode))
        {
            *phCon = hCon;
            return true;
        }
    }
    return false;
}


static int rtStrmWriteWinConsoleLocked(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten, HANDLE hCon)
{
    int rc;
# ifdef HAVE_FWRITE_UNLOCKED
    if (!fflush_unlocked(pStream->pFile))
# else
    if (!fflush(pStream->pFile))
# endif
    {
        /** @todo Consider buffering later. For now, we'd rather correct output than
         *        fast output. */
        DWORD    cwcWritten = 0;
        PRTUTF16 pwszSrc = NULL;
        size_t   cwcSrc = 0;
        rc = RTStrToUtf16Ex((const char *)pvBuf, cbToWrite, &pwszSrc, 0, &cwcSrc);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            if (!WriteConsoleW(hCon, pwszSrc, (DWORD)cwcSrc, &cwcWritten, NULL))
            {
                /* try write char-by-char to avoid heap problem. */
                cwcWritten = 0;
                while (cwcWritten != cwcSrc)
                {
                    DWORD cwcThis;
                    if (!WriteConsoleW(hCon, &pwszSrc[cwcWritten], 1, &cwcThis, NULL))
                    {
                        if (!pcbWritten || cwcWritten == 0)
                            rc = RTErrConvertFromErrno(GetLastError());
                        break;
                    }
                    if (cwcThis != 1) /* Unable to write current char (amount)? */
                        break;
                    cwcWritten++;
                }
            }
            if (RT_SUCCESS(rc))
            {
                if (cwcWritten == cwcSrc)
                {
                    if (pcbWritten)
                        *pcbWritten = cbToWrite;
                }
                else if (pcbWritten)
                {
                    PCRTUTF16   pwszCur = pwszSrc;
                    const char *pszCur  = (const char *)pvBuf;
                    while ((uintptr_t)(pwszCur - pwszSrc) < cwcWritten)
                    {
                        RTUNICP CpIgnored;
                        RTUtf16GetCpEx(&pwszCur, &CpIgnored);
                        RTStrGetCpEx(&pszCur, &CpIgnored);
                    }
                    *pcbWritten = pszCur - (const char *)pvBuf;
                }
                else
                    rc = VERR_WRITE_ERROR;
            }
            RTUtf16Free(pwszSrc);
        }
    }
    else
        rc = RTErrConvertFromErrno(errno);
    return rc;
}

#endif /* RT_OS_WINDOWS */

static int rtStrmWriteWorkerLocked(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten, bool fMustWriteAll)
{
#ifdef RTSTREAM_STANDALONE
    /*
     * Check preconditions.
     */
    Assert(pStream->enmBufDir    == RTSTREAMBUFDIR_WRITE);
    Assert(pStream->cbBufAlloc   >= 256);
    Assert(pStream->offBufFirst  <= pStream->cbBufAlloc);
    Assert(pStream->offBufEnd    <= pStream->cbBufAlloc);
    Assert(pStream->offBufFirst  <= pStream->offBufEnd);

    /*
     * We write everything via the buffer, letting the buffer flushing take
     * care of console output hacks and similar.
     */
    RT_NOREF(fMustWriteAll);
    int    rc      = VINF_SUCCESS;
    size_t cbTotal = 0;
    if (cbToWrite > 0)
    {
# ifdef RTSTREAM_WITH_TEXT_MODE
        const char *pchLf;
        if (   !pStream->fBinary
            && (pchLf = (const char *)memchr(pvBuf, '\n', cbToWrite)) != NULL)
            for (;;)
            {
                /* Deal with everything up to the newline. */
                size_t const cbToLf = (size_t)(pchLf - (const char *)pvBuf);
                if (cbToLf > 0)
                {
                    rc = rtStrmBufCopyTo(pStream, pvBuf, cbToLf, &cbTotal);
                    if (RT_FAILURE(rc))
                        break;
                }

                /* Copy the CRLF sequence into the buffer in one go to avoid complications. */
                if (pStream->cbBufAlloc - pStream->offBufEnd < 2)
                {
                    rc = rtStrmBufFlushWrite(pStream, pStream->offBufEnd - pStream->offBufFirst);
                    if (RT_FAILURE(rc))
                        break;
                    Assert(pStream->cbBufAlloc - pStream->offBufEnd >= 2);
                }
                pStream->pchBuf[pStream->offBufEnd++] = '\r';
                pStream->pchBuf[pStream->offBufEnd++] = '\n';

                /* Advance past the newline. */
                pvBuf               = (const char *)pvBuf + 1 + cbToLf;
                cbTotal            += 1 + cbToLf;
                cbToWrite          -= 1 + cbToLf;
                if (!cbToWrite)
                    break;

                /* More newlines? */
                pchLf = (const char *)memchr(pvBuf, '\n', cbToWrite);
                if (!pchLf)
                {
                    rc = rtStrmBufCopyTo(pStream, pvBuf, cbToWrite, &cbTotal);
                    break;
                }
            }
        else
# endif
            rc = rtStrmBufCopyTo(pStream, pvBuf, cbToWrite, &cbTotal);

        /*
         * If line buffered or unbuffered, we probably have to do some flushing now.
         */
        if (RT_SUCCESS(rc) && pStream->enmBufStyle != RTSTREAMBUFSTYLE_FULL)
        {
            Assert(pStream->enmBufStyle == RTSTREAMBUFSTYLE_LINE || pStream->enmBufStyle == RTSTREAMBUFSTYLE_UNBUFFERED);
            size_t cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
            if (cbInBuffer > 0)
            {
                if (   pStream->enmBufStyle != RTSTREAMBUFSTYLE_LINE
                    || pStream->pchBuf[pStream->offBufEnd - 1] == '\n')
                    rc = rtStrmBufFlushWrite(pStream, cbInBuffer);
                else
                {
                    const char *pchToFlush = &pStream->pchBuf[pStream->offBufFirst];
                    const char *pchLastLf  = (const char *)memrchr(pchToFlush, '\n', cbInBuffer);
                    if (pchLastLf)
                        rc = rtStrmBufFlushWrite(pStream, (size_t)(&pchLastLf[1] - pchToFlush));
                }
            }
        }
    }
    if (pcbWritten)
        *pcbWritten = cbTotal;
    return rc;


#else
    if (!fMustWriteAll)
    {
        IPRT_ALIGNMENT_CHECKS_DISABLE(); /* glibc / mempcpy again */
# ifdef HAVE_FWRITE_UNLOCKED
        *pcbWritten = fwrite_unlocked(pvBuf, 1, cbToWrite, pStream->pFile);
# else
        *pcbWritten = fwrite(pvBuf, 1, cbToWrite, pStream->pFile);
# endif
        IPRT_ALIGNMENT_CHECKS_ENABLE();
        if (    *pcbWritten == cbToWrite
# ifdef HAVE_FWRITE_UNLOCKED
            ||  !ferror_unlocked(pStream->pFile))
# else
            ||  !ferror(pStream->pFile))
# endif
            return VINF_SUCCESS;
    }
    else
    {
        /* Must write it all! */
        IPRT_ALIGNMENT_CHECKS_DISABLE(); /* glibc / mempcpy again */
# ifdef HAVE_FWRITE_UNLOCKED
        size_t cbWritten = fwrite_unlocked(pvBuf, cbToWrite, 1, pStream->pFile);
# else
        size_t cbWritten = fwrite(pvBuf, cbToWrite, 1, pStream->pFile);
# endif
        if (pcbWritten)
            *pcbWritten = cbWritten;
        IPRT_ALIGNMENT_CHECKS_ENABLE();
        if (cbWritten == 1)
            return VINF_SUCCESS;
# ifdef HAVE_FWRITE_UNLOCKED
        if (!ferror_unlocked(pStream->pFile))
# else
        if (!ferror(pStream->pFile))
# endif
            return VINF_SUCCESS; /* WEIRD! But anyway... */
    }
    return VERR_WRITE_ERROR;
#endif
}


/**
 * Internal write API, stream lock already held.
 *
 * @returns IPRT status code.
 * @param   pStream             The stream.
 * @param   pvBuf               What to write.
 * @param   cbToWrite           How much to write.
 * @param   pcbWritten          Where to optionally return the number of bytes
 *                              written.
 * @param   fSureIsText         Set if we're sure this is UTF-8 text already.
 */
static int rtStrmWriteLocked(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten, bool fSureIsText)
{
#ifdef RTSTREAM_STANDALONE
    int rc = rtStrmBufCheckErrorAndSwitchToWriteMode(pStream);
#else
    int rc = pStream->i32Error;
#endif
    if (RT_FAILURE(rc))
        return rc;
    if (pStream->fRecheckMode)
        rtStreamRecheckMode(pStream);

#if defined(RT_OS_WINDOWS) && !defined(RTSTREAM_STANDALONE)
    /*
     * Use the unicode console API when possible in order to avoid stuff
     * getting lost in unnecessary code page translations.
     */
    HANDLE hCon;
    if (rtStrmIsConsoleUnlocked(pStream, &hCon))
        rc = rtStrmWriteWinConsoleLocked(pStream, pvBuf, cbToWrite, pcbWritten, hCon);
#else
    if (0) { }
#endif /* RT_OS_WINDOWS && !RTSTREAM_STANDALONE */

    /*
     * If we're sure it's text output, convert it from UTF-8 to the current
     * code page before printing it.
     *
     * Note! Partial writes are not supported in this scenario because we
     *       cannot easily report back a written length matching the input.
     */
    /** @todo Skip this if the current code set is UTF-8. */
    else if (   pStream->fCurrentCodeSet
             && !pStream->fBinary
             && (   fSureIsText
                 || rtStrmIsUtf8Text(pvBuf, cbToWrite))
            )
    {
        char       *pszSrcFree = NULL;
        const char *pszSrc     = (const char *)pvBuf;
        if (pszSrc[cbToWrite - 1])
        {
            pszSrc = pszSrcFree = RTStrDupN(pszSrc, cbToWrite);
            if (pszSrc == NULL)
                rc = VERR_NO_STR_MEMORY;
        }
        if (RT_SUCCESS(rc))
        {
            char *pszSrcCurCP;
            rc = RTStrUtf8ToCurrentCP(&pszSrcCurCP, pszSrc);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                size_t  cchSrcCurCP = strlen(pszSrcCurCP);
                size_t  cbWritten = 0;
                rc = rtStrmWriteWorkerLocked(pStream, pszSrcCurCP, cchSrcCurCP, &cbWritten, true /*fMustWriteAll*/);
                if (pcbWritten)
                    *pcbWritten = cbWritten == cchSrcCurCP ? cbToWrite : 0;
                RTStrFree(pszSrcCurCP);
            }
            RTStrFree(pszSrcFree);
        }
    }
    /*
     * Otherwise, just write it as-is.
     */
    else
        rc = rtStrmWriteWorkerLocked(pStream, pvBuf, cbToWrite, pcbWritten, pcbWritten == NULL);

    /*
     * Update error status on failure and return.
     *
     * We ignore failures from RTStrUtf8ToCurrentCP and RTStrToUtf16Ex regarding
     * invalid UTF-8 encoding, as that's an input issue and shouldn't affect the
     * stream state.
     */
    if (RT_FAILURE(rc) && rc != VERR_INVALID_UTF8_ENCODING)
        ASMAtomicWriteS32(&pStream->i32Error, rc);
    return rc;
}


/**
 * Internal write API.
 *
 * @returns IPRT status code.
 * @param   pStream             The stream.
 * @param   pvBuf               What to write.
 * @param   cbToWrite           How much to write.
 * @param   pcbWritten          Where to optionally return the number of bytes
 *                              written.
 * @param   fSureIsText         Set if we're sure this is UTF-8 text already.
 */
DECLINLINE(int) rtStrmWrite(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten, bool fSureIsText)
{
    rtStrmLock(pStream);
    int rc = rtStrmWriteLocked(pStream, pvBuf, cbToWrite, pcbWritten, fSureIsText);
    rtStrmUnlock(pStream);
    return rc;
}


RTR3DECL(int) RTStrmWriteEx(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    AssertReturn(RT_VALID_PTR(pStream) && pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_PARAMETER);
    return rtStrmWrite(pStream, pvBuf, cbToWrite, pcbWritten, false);
}


RTR3DECL(int) RTStrmGetCh(PRTSTREAM pStream)
{
    unsigned char ch;
    int rc = RTStrmReadEx(pStream, &ch, 1, NULL);
    if (RT_SUCCESS(rc))
        return ch;
    return -1;
}


RTR3DECL(int) RTStrmPutCh(PRTSTREAM pStream, int ch)
{
    return rtStrmWrite(pStream, &ch, 1, NULL, true /*fSureIsText*/);
}


RTR3DECL(int) RTStrmPutStr(PRTSTREAM pStream, const char *pszString)
{
    size_t cch = strlen(pszString);
    return rtStrmWrite(pStream, pszString, cch, NULL, true /*fSureIsText*/);
}


RTR3DECL(int) RTStrmGetLine(PRTSTREAM pStream, char *pszString, size_t cbString)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pszString, VERR_INVALID_POINTER);
    AssertReturn(cbString >= 2, VERR_INVALID_PARAMETER);

    rtStrmLock(pStream);

#ifdef RTSTREAM_STANDALONE
    int rc = rtStrmBufCheckErrorAndSwitchToReadMode(pStream);
#else
    int rc = pStream->i32Error;
#endif
    if (RT_SUCCESS(rc))
    {
        cbString--;            /* Reserve space for the terminator. */

#ifdef RTSTREAM_STANDALONE
        char * const pszStringStart = pszString;
#endif
        for (;;)
        {
#ifdef RTSTREAM_STANDALONE
            /* Make sure there is at least one character in the buffer: */
            size_t cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
            if (cbInBuffer == 0)
            {
                rc = rtStrmBufFill(pStream);
                if (RT_SUCCESS(rc))
                    cbInBuffer = pStream->offBufEnd - pStream->offBufFirst;
                else
                    break;
            }

            /* Scan the buffer content terminating on a '\n', '\r\n' and '\0' sequence. */
            const char *pchSrc     = &pStream->pchBuf[pStream->offBufFirst];
            const char *pchNewline = (const char *)memchr(pchSrc, '\n', cbInBuffer);
            const char *pchTerm    = (const char *)memchr(pchSrc, '\0', cbInBuffer);
            size_t      cbCopy;
            size_t      cbAdvance;
            bool        fStop      = pchNewline || pchTerm;
            if (!fStop)
                cbAdvance = cbCopy = cbInBuffer;
            else if (!pchTerm || (pchNewline && pchTerm && (uintptr_t)pchNewline < (uintptr_t)pchTerm))
            {
                cbCopy    = (size_t)(pchNewline - pchSrc);
                cbAdvance = cbCopy + 1;
                if (cbCopy && pchNewline[-1] == '\r')
                    cbCopy--;
                else if (cbCopy == 0 && (uintptr_t)pszString > (uintptr_t)pszStringStart && pszString[-1] == '\r')
                    pszString--, cbString++; /* drop trailing '\r' that it turns out was followed by '\n' */
            }
            else
            {
                cbCopy    = (size_t)(pchTerm - pchSrc);
                cbAdvance = cbCopy + 1;
            }

            /* Adjust for available space in the destination buffer, copy over the string
               characters and advance the buffer position (even on overflow). */
            if (cbCopy <= cbString)
                pStream->offBufFirst += cbAdvance;
            else
            {
                rc        = VERR_BUFFER_OVERFLOW;
                fStop     = true;
                cbCopy    = cbString;
                pStream->offBufFirst += cbString;
            }

            memcpy(pszString, pchSrc, cbCopy);
            pszString += cbCopy;
            cbString  -= cbCopy;

            if (fStop)
                break;

#else  /* !RTSTREAM_STANDALONE */
# ifdef HAVE_FWRITE_UNLOCKED /** @todo darwin + freebsd(?) has fgetc_unlocked but not fwrite_unlocked, optimize... */
            int ch = fgetc_unlocked(pStream->pFile);
# else
            int ch = fgetc(pStream->pFile);
# endif

            /* Deal with \r\n sequences here. We'll return lone CR, but
               treat CRLF as LF. */
            if (ch == '\r')
            {
# ifdef HAVE_FWRITE_UNLOCKED /** @todo darwin + freebsd(?) has fgetc_unlocked but not fwrite_unlocked, optimize... */
                ch = fgetc_unlocked(pStream->pFile);
# else
                ch = fgetc(pStream->pFile);
# endif
                if (ch == '\n')
                    break;

                *pszString++ = '\r';
                if (--cbString <= 0)
                {
                    /* yeah, this is an error, we dropped a character. */
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
            }

            /* Deal with end of file. */
            if (ch == EOF)
            {
# ifdef HAVE_FWRITE_UNLOCKED
                if (feof_unlocked(pStream->pFile))
# else
                if (feof(pStream->pFile))
# endif
                {
                    rc = VERR_EOF;
                    break;
                }
# ifdef HAVE_FWRITE_UNLOCKED
                if (ferror_unlocked(pStream->pFile))
# else
                if (ferror(pStream->pFile))
# endif
                    rc = VERR_READ_ERROR;
                else
                {
                    AssertMsgFailed(("This shouldn't happen\n"));
                    rc = VERR_INTERNAL_ERROR;
                }
                break;
            }

            /* Deal with null terminator and (lone) new line. */
            if (ch == '\0' || ch == '\n')
                break;

            /* No special character, append it to the return string. */
            *pszString++ = ch;
            if (--cbString <= 0)
            {
                rc = VINF_BUFFER_OVERFLOW;
                break;
            }
#endif /* !RTSTREAM_STANDALONE */
        }

        *pszString = '\0';
        if (RT_FAILURE(rc))
            ASMAtomicWriteS32(&pStream->i32Error, rc);
    }

    rtStrmUnlock(pStream);
    return rc;
}


RTR3DECL(int) RTStrmFlush(PRTSTREAM pStream)
{
    AssertPtrReturn(pStream, VERR_INVALID_HANDLE);
    AssertReturn(pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSTREAM_STANDALONE
    rtStrmLock(pStream);
    int rc = rtStrmBufFlushWriteMaybe(pStream, true /*fInvalidate*/);
    rtStrmUnlock(pStream);
    return rc;

#else
    if (!fflush(pStream->pFile))
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
#endif
}


/**
 * Output callback.
 *
 * @returns number of bytes written.
 * @param   pvArg       User argument.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cchChars    Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) rtstrmOutput(void *pvArg, const char *pachChars, size_t cchChars)
{
    if (cchChars)
        rtStrmWriteLocked((PRTSTREAM)pvArg, pachChars, cchChars, NULL, true /*fSureIsText*/);
    /* else: ignore termination call. */
    return cchChars;
}


RTR3DECL(int) RTStrmPrintfV(PRTSTREAM pStream, const char *pszFormat, va_list args)
{
    AssertReturn(RT_VALID_PTR(pStream) && pStream->u32Magic == RTSTREAM_MAGIC, VERR_INVALID_PARAMETER);
    int rc = pStream->i32Error;
    if (RT_SUCCESS(rc))
    {
        rtStrmLock(pStream);
//        pStream->fShouldFlush = true;
        rc = (int)RTStrFormatV(rtstrmOutput, pStream, NULL, NULL, pszFormat, args);
        rtStrmUnlock(pStream);
        Assert(rc >= 0);
    }
    else
        rc = -1;
    return rc;
}


RTR3DECL(int) RTStrmPrintf(PRTSTREAM pStream, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc = RTStrmPrintfV(pStream, pszFormat, args);
    va_end(args);
    return rc;
}


RTDECL(void) RTStrmDumpPrintfV(void *pvUser, const char *pszFormat, va_list va)
{
    RTStrmPrintfV(pvUser ? (PRTSTREAM)pvUser : g_pStdOut, pszFormat, va);
}


RTR3DECL(int) RTPrintfV(const char *pszFormat, va_list args)
{
    return RTStrmPrintfV(g_pStdOut, pszFormat, args);
}


RTR3DECL(int) RTPrintf(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc = RTStrmPrintfV(g_pStdOut, pszFormat, args);
    va_end(args);
    return rc;
}


/**
 * Outputs @a cchIndent spaces.
 */
static void rtStrmWrapppedIndent(RTSTRMWRAPPEDSTATE *pState, uint32_t cchIndent)
{
    static const char s_szSpaces[] = "                                                ";
    while (cchIndent)
    {
        uint32_t cchToWrite = RT_MIN(cchIndent, sizeof(s_szSpaces) - 1);
        int rc = RTStrmWrite(pState->pStream, s_szSpaces, cchToWrite);
        if (RT_SUCCESS(rc))
            cchIndent -= cchToWrite;
        else
        {
            pState->rcStatus = rc;
            break;
        }
    }
}


/**
 * Flushes the current line.
 *
 * @param   pState      The wrapped output state.
 * @param   fPartial    Set if partial flush due to buffer overflow, clear when
 *                      flushing due to '\n'.
 */
static void rtStrmWrappedFlushLine(RTSTRMWRAPPEDSTATE *pState, bool fPartial)
{
    /*
     * Check indentation in case we need to split the line later.
     */
    uint32_t cchIndent = pState->cchIndent;
    if (cchIndent == UINT32_MAX)
    {
        pState->cchIndent = 0;
        cchIndent = pState->cchHangingIndent;
        while (RT_C_IS_BLANK(pState->szLine[cchIndent]))
            cchIndent++;
    }

    /*
     * Do the flushing.
     */
    uint32_t cchLine = pState->cchLine;
    Assert(cchLine < sizeof(pState->szLine));
    while (cchLine >= pState->cchWidth || !fPartial)
    {
        /*
         * Hopefully we don't need to do any wrapping ...
         */
        uint32_t offSplit;
        if (pState->cchIndent + cchLine <= pState->cchWidth)
        {
            if (!fPartial)
            {
                rtStrmWrapppedIndent(pState, pState->cchIndent);
                pState->szLine[cchLine] = '\n';
                int rc = RTStrmWrite(pState->pStream, pState->szLine, cchLine + 1);
                if (RT_FAILURE(rc))
                    pState->rcStatus = rc;
                pState->cLines   += 1;
                pState->cchLine   = 0;
                pState->cchIndent = UINT32_MAX;
                return;
            }

            /*
             * ... no such luck.
             */
            offSplit = cchLine;
        }
        else
            offSplit = pState->cchWidth - pState->cchIndent;

        /* Find the start of the current word: */
        while (offSplit > 0 && !RT_C_IS_BLANK(pState->szLine[offSplit - 1]))
            offSplit--;

        /* Skip spaces. */
        while (offSplit > 0 && RT_C_IS_BLANK(pState->szLine[offSplit - 1]))
            offSplit--;
        uint32_t offNextLine = offSplit;

        /* If the first word + indent is wider than the screen width, so just output it in full. */
        if (offSplit == 0) /** @todo Split words, look for hyphen...  This code is currently a bit crude. */
        {
            while (offSplit < cchLine && !RT_C_IS_BLANK(pState->szLine[offSplit]))
                offSplit++;
            offNextLine = offSplit;
        }

        while (offNextLine < cchLine && RT_C_IS_BLANK(pState->szLine[offNextLine]))
            offNextLine++;

        /*
         * Output and advance.
         */
        rtStrmWrapppedIndent(pState, pState->cchIndent);
        int rc = RTStrmWrite(pState->pStream, pState->szLine, offSplit);
        if (RT_SUCCESS(rc))
            rc = RTStrmPutCh(pState->pStream, '\n');
        if (RT_FAILURE(rc))
            pState->rcStatus = rc;

        cchLine -= offNextLine;
        pState->cchLine   = cchLine;
        pState->cLines   += 1;
        pState->cchIndent = cchIndent;
        memmove(&pState->szLine[0], &pState->szLine[offNextLine], cchLine);
    }

    /* The indentation level is reset for each '\n' we process, so only save cchIndent if partial. */
    pState->cchIndent = fPartial ? cchIndent : UINT32_MAX;
}


/**
 * @callback_method_impl{FNRTSTROUTPUT}
 */
static DECLCALLBACK(size_t) rtStrmWrappedOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    RTSTRMWRAPPEDSTATE *pState = (RTSTRMWRAPPEDSTATE *)pvArg;
    size_t const cchRet = cbChars;
    while (cbChars > 0)
    {
        if (*pachChars == '\n')
        {
            rtStrmWrappedFlushLine(pState, false /*fPartial*/);
            pachChars++;
            cbChars--;
        }
        else
        {
            const char *pszEol      = (const char *)memchr(pachChars, '\n', cbChars);
            size_t      cchToCopy   = pszEol ? (size_t)(pszEol - pachChars) : cbChars;
            uint32_t    cchLine     = pState->cchLine;
            Assert(cchLine < sizeof(pState->szLine));
            bool const  fFlush      = cchLine + cchToCopy >= sizeof(pState->szLine);
            if (fFlush)
                cchToCopy = cchToCopy - sizeof(pState->szLine) - 1;

            pState->cchLine = cchLine + (uint32_t)cchToCopy;
            memcpy(&pState->szLine[cchLine], pachChars, cchToCopy);

            pachChars += cchToCopy;
            cbChars   -= cchToCopy;

            if (fFlush)
                rtStrmWrappedFlushLine(pState, true /*fPartial*/);
        }
    }
    return cchRet;
}


RTDECL(int32_t) RTStrmWrappedPrintfV(PRTSTREAM pStream, uint32_t fFlags, const char *pszFormat, va_list va)
{
    /*
     * Figure the output width and set up the rest of the output state.
     */
    RTSTRMWRAPPEDSTATE State;
    State.pStream           = pStream;
    State.cchLine           = fFlags & RTSTRMWRAPPED_F_LINE_OFFSET_MASK;
    State.cLines            = 0;
    State.rcStatus          = VINF_SUCCESS;
    State.cchIndent         = UINT32_MAX;
    State.cchHangingIndent  = 0;
    if (fFlags & RTSTRMWRAPPED_F_HANGING_INDENT)
    {
        State.cchHangingIndent = (fFlags & RTSTRMWRAPPED_F_HANGING_INDENT_MASK) >> RTSTRMWRAPPED_F_HANGING_INDENT_SHIFT;
        if (!State.cchHangingIndent)
            State.cchHangingIndent = 4;
    }

    int rc = RTStrmQueryTerminalWidth(pStream, &State.cchWidth);
    if (RT_SUCCESS(rc))
        State.cchWidth = RT_MIN(State.cchWidth, RTSTRMWRAPPED_F_LINE_OFFSET_MASK + 1);
    else
    {
        State.cchWidth = (uint32_t)fFlags & RTSTRMWRAPPED_F_NON_TERMINAL_WIDTH_MASK;
        if (!State.cchWidth)
            State.cchWidth = 80;
    }
    if (State.cchWidth < 32)
        State.cchWidth = 32;
    //State.cchWidth         -= 1; /* necessary here? */

    /*
     * Do the formatting.
     */
    RTStrFormatV(rtStrmWrappedOutput, &State, NULL, NULL, pszFormat, va);

    /*
     * Returning is simple if the buffer is empty.  Otherwise we'll have to
     * perform a partial flush and write out whatever is left ourselves.
     */
    if (RT_SUCCESS(State.rcStatus))
    {
        if (State.cchLine == 0)
            return State.cLines << 16;

        rtStrmWrappedFlushLine(&State, true /*fPartial*/);
        if (RT_SUCCESS(State.rcStatus) && State.cchLine > 0)
        {
            rtStrmWrapppedIndent(&State, State.cchIndent);
            State.rcStatus = RTStrmWrite(State.pStream, State.szLine, State.cchLine);
        }
        if (RT_SUCCESS(State.rcStatus))
            return RT_MIN(State.cchIndent + State.cchLine, RTSTRMWRAPPED_F_LINE_OFFSET_MASK) | (State.cLines << 16);
    }
    return State.rcStatus;
}


RTDECL(int32_t) RTStrmWrappedPrintf(PRTSTREAM pStream, uint32_t fFlags, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int32_t rcRet = RTStrmWrappedPrintfV(pStream, fFlags, pszFormat, va);
    va_end(va);
    return rcRet;
}

