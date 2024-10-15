/* $Id: circbuf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Lock Free Circular Buffer
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/circbuf.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @todo r=bird: this is missing docs and magic. */
typedef struct RTCIRCBUF
{
    /** The current read position in the buffer. */
    size_t          offRead;
    /** Is a read block acquired currently? */
    bool            fReading;
    /** Is a write block acquired currently? */
    bool            fWriting;
    /** The current write position in the buffer. */
    size_t          offWrite;
    /** How much space of the buffer is currently in use. */
    volatile size_t cbUsed;
    /** How big is the buffer. */
    size_t          cbBuf;
    /** The buffer itself. */
    void           *pvBuf;
} RTCIRCBUF, *PRTCIRCBUF;


RTDECL(int) RTCircBufCreate(PRTCIRCBUF *ppBuf, size_t cbSize)
{
    /* Validate input. */
    AssertPtrReturn(ppBuf, VERR_INVALID_POINTER);
    AssertReturn(cbSize > 0, VERR_INVALID_PARAMETER);

    PRTCIRCBUF pTmpBuf;
    pTmpBuf = (PRTCIRCBUF)RTMemAllocZ(sizeof(RTCIRCBUF));
    if (!pTmpBuf)
        return VERR_NO_MEMORY;

    pTmpBuf->pvBuf = RTMemAlloc(cbSize);
    if (pTmpBuf->pvBuf)
    {
        pTmpBuf->cbBuf = cbSize;
        *ppBuf = pTmpBuf;
        return VINF_SUCCESS;
    }

    RTMemFree(pTmpBuf);
    return VERR_NO_MEMORY;
}


RTDECL(void) RTCircBufDestroy(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    if (!pBuf)
        return;
    AssertPtr(pBuf);
    RTMemFree(pBuf->pvBuf);
    RTMemFree(pBuf);
}


RTDECL(void) RTCircBufReset(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtr(pBuf);

    pBuf->offRead  = 0;
    pBuf->offWrite = 0;
    pBuf->cbUsed   = 0;
    pBuf->fReading = false;
    pBuf->fWriting = false;
}


RTDECL(size_t) RTCircBufFree(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return pBuf->cbBuf - ASMAtomicReadZ(&pBuf->cbUsed);
}


RTDECL(size_t) RTCircBufUsed(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return ASMAtomicReadZ(&pBuf->cbUsed);
}

RTDECL(size_t) RTCircBufSize(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return pBuf->cbBuf;
}

RTDECL(bool) RTCircBufIsReading(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return ASMAtomicReadBool(&pBuf->fReading);
}

RTDECL(bool) RTCircBufIsWriting(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return ASMAtomicReadBool(&pBuf->fWriting);
}

RTDECL(size_t) RTCircBufOffsetRead(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return ASMAtomicReadZ(&pBuf->offRead);
}

RTDECL(size_t) RTCircBufOffsetWrite(PRTCIRCBUF pBuf)
{
    /* Validate input. */
    AssertPtrReturn(pBuf, 0);

    return ASMAtomicReadZ(&pBuf->offWrite);
}

RTDECL(void) RTCircBufAcquireReadBlock(PRTCIRCBUF pBuf, size_t cbReqSize, void **ppvStart, size_t *pcbSize)
{
    /* Validate input. */
    AssertPtr(pBuf);
    Assert(cbReqSize > 0);
    AssertPtr(ppvStart);
    AssertPtr(pcbSize);

    *ppvStart = 0;
    *pcbSize = 0;

    /* How much is in use? */
    size_t cbUsed = ASMAtomicReadZ(&pBuf->cbUsed);
    if (cbUsed > 0)
    {
        /* Get the size out of the requested size, the read block till the end
         * of the buffer & the currently used size. */
        size_t cbSize = RT_MIN(cbReqSize, RT_MIN(pBuf->cbBuf - pBuf->offRead, cbUsed));
        if (cbSize > 0)
        {
            /* Return the pointer address which point to the current read
             * position. */
            *ppvStart = (char *)pBuf->pvBuf + pBuf->offRead;
            *pcbSize = cbSize;

            ASMAtomicWriteBool(&pBuf->fReading, true);
        }
    }
}


RTDECL(void) RTCircBufReleaseReadBlock(PRTCIRCBUF pBuf, size_t cbSize)
{
    /* Validate input. */
    AssertPtr(pBuf);

    /* Split at the end of the buffer. */
    pBuf->offRead = (pBuf->offRead + cbSize) % pBuf->cbBuf;

    ASMAtomicSubZ(&pBuf->cbUsed, cbSize);
    ASMAtomicWriteBool(&pBuf->fReading, false);
}


RTDECL(void) RTCircBufAcquireWriteBlock(PRTCIRCBUF pBuf, size_t cbReqSize, void **ppvStart, size_t *pcbSize)
{
    /* Validate input. */
    AssertPtr(pBuf);
    Assert(cbReqSize > 0);
    AssertPtr(ppvStart);
    AssertPtr(pcbSize);

    *ppvStart = 0;
    *pcbSize = 0;

    /* How much is free? */
    size_t cbFree = pBuf->cbBuf - ASMAtomicReadZ(&pBuf->cbUsed);
    if (cbFree > 0)
    {
        /* Get the size out of the requested size, then write block till the end
         * of the buffer & the currently free size. */
        size_t cbSize = RT_MIN(cbReqSize, RT_MIN(pBuf->cbBuf - pBuf->offWrite, cbFree));
        if (cbSize > 0)
        {
            /* Return the pointer address which point to the current write
             * position. */
            *ppvStart = (char*)pBuf->pvBuf + pBuf->offWrite;
            *pcbSize = cbSize;

            ASMAtomicWriteBool(&pBuf->fWriting, true);
        }
    }
}


RTDECL(void) RTCircBufReleaseWriteBlock(PRTCIRCBUF pBuf, size_t cbSize)
{
    /* Validate input. */
    AssertPtr(pBuf);

    /* Split at the end of the buffer. */
    pBuf->offWrite = (pBuf->offWrite + cbSize) % pBuf->cbBuf;

    ASMAtomicAddZ(&pBuf->cbUsed, cbSize);
    ASMAtomicWriteBool(&pBuf->fWriting, false);
}

