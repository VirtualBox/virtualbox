/* $Id: nocrt-streams-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - minimal stream implementation
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
#include "internal/iprt.h"
#include <iprt/stream.h>

#include <iprt/nt/nt-and-windows.h>

#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/string.h>



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct PRINTFBUF
{
    HANDLE  hNative;
    size_t  offBuf;
    char    szBuf[128];
} PRINTFBUF;

struct RTSTREAM
{
    int     iStream;
    HANDLE  hNative;
    RTFILE  hFile;
};


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MAKE_SURE_WE_HAVE_HFILE_RETURN(a_pStream) do { \
        if ((a_pStream)->hFile != NIL_RTFILE) \
            break; \
        int rc = RTFileFromNative(&(a_pStream)->hFile, (uintptr_t)(a_pStream)->hNative); \
        AssertRCReturn(rc, rc); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RTSTREAM g_aStdStreams[3] =
{
    { 0, NULL, NIL_RTFILE },
    { 1, NULL, NIL_RTFILE },
    { 2, NULL, NIL_RTFILE },
};

RTSTREAM *g_pStdIn  = &g_aStdStreams[0];
RTSTREAM *g_pStdOut = &g_aStdStreams[1];
RTSTREAM *g_pStdErr = &g_aStdStreams[2];



DECLHIDDEN(void) InitStdHandles(PRTL_USER_PROCESS_PARAMETERS pParams)
{
    if (pParams)
    {
        g_pStdIn->hNative  = pParams->StandardInput;
        g_pStdOut->hNative = pParams->StandardOutput;
        g_pStdErr->hNative = pParams->StandardError;
    }
}


static void FlushPrintfBuffer(PRINTFBUF *pBuf)
{
    if (pBuf->offBuf)
    {
        DWORD cbWritten = 0;
        WriteFile(pBuf->hNative, pBuf->szBuf, (DWORD)pBuf->offBuf, &cbWritten, NULL);
        pBuf->offBuf   = 0;
        pBuf->szBuf[0] = '\0';
    }
}


/** @callback_method_impl{FNRTSTROUTPUT} */
static DECLCALLBACK(size_t) MyPrintfOutputter(void *pvArg, const char *pachChars, size_t cbChars)
{
    PRINTFBUF *pBuf = (PRINTFBUF *)pvArg;
    if (cbChars != 0)
    {
        size_t offSrc = 0;
        while  (offSrc < cbChars)
        {
            size_t cbLeft = sizeof(pBuf->szBuf) - pBuf->offBuf - 1;
            if (cbLeft > 0)
            {
                size_t cbToCopy = RT_MIN(cbChars - offSrc, cbLeft);
                memcpy(&pBuf->szBuf[pBuf->offBuf], &pachChars[offSrc], cbToCopy);
                pBuf->offBuf += cbToCopy;
                pBuf->szBuf[pBuf->offBuf] = '\0';
                if (cbLeft > cbToCopy)
                    break;
                offSrc += cbToCopy;
            }
            FlushPrintfBuffer(pBuf);
        }
    }
    else /* Special zero byte write at the end of the formatting. */
        FlushPrintfBuffer(pBuf);
    return cbChars;
}


RTR3DECL(int) RTStrmPrintfV(PRTSTREAM pStream, const char *pszFormat, va_list args)
{
    PRINTFBUF Buf;
    Buf.hNative  = pStream->hNative;
    Buf.offBuf   = 0;
    Buf.szBuf[0] = '\0';

    return (int)RTStrFormatV(MyPrintfOutputter, &Buf, NULL, NULL, pszFormat, args);
}


RTR3DECL(int) RTStrmPrintf(PRTSTREAM pStream, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc = RTStrmPrintfV(pStream, pszFormat, args);
    va_end(args);
    return rc;
}


RTR3DECL(int) RTPrintfV(const char *pszFormat, va_list va)
{
    PRINTFBUF Buf;
    Buf.hNative  = g_pStdOut->hNative;
    Buf.offBuf   = 0;
    Buf.szBuf[0] = '\0';

    return (int)RTStrFormatV(MyPrintfOutputter, &Buf, NULL, NULL, pszFormat, va);
}


RTR3DECL(int) RTPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = RTPrintfV(pszFormat, va);
    va_end(va);
    return rc;
}

#ifndef IPRT_MINIMAL_STREAM

# if 0
RTR3DECL(int) RTStrmReadEx(PRTSTREAM pStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    MAKE_SURE_WE_HAVE_HFILE_RETURN(pStream);
    return RTFileRead(pStream->hFile, pvBuf, cbToRead, pcbRead);
}
# endif


RTR3DECL(int) RTStrmWriteEx(PRTSTREAM pStream, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    MAKE_SURE_WE_HAVE_HFILE_RETURN(pStream);
    return RTFileWrite(pStream->hFile, pvBuf, cbToWrite, pcbWritten);
}


RTR3DECL(int) RTStrmFlush(PRTSTREAM pStream)
{
    MAKE_SURE_WE_HAVE_HFILE_RETURN(pStream);
    return RTFileFlush(pStream->hFile);
}


RTR3DECL(int) RTStrmSetMode(PRTSTREAM pStream, int fBinary, int fCurrentCodeSet)
{
    AssertReturn(fBinary != (int)false, VERR_NOT_IMPLEMENTED);
    AssertReturn(fCurrentCodeSet <= (int)false, VERR_NOT_IMPLEMENTED);
    RT_NOREF(pStream);
    return VINF_SUCCESS;
}

#endif
