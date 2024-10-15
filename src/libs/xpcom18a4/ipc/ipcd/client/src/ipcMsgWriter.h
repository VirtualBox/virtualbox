/* $Id: ipcMsgWriter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM - IPC message writer helper.
 */

/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla IPC.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Conrad Carlen <ccarlen@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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


#ifndef ipcMsgWriter_h__
#define ipcMsgWriter_h__

#include <iprt/types.h>
#include <iprt/string.h>
#include <iprt/mem.h>

/** Whether to allow unaligned memory access for the message writer. */
#define IPC_MSG_WRITER_UNALIGNED_ACCESS

/**
 * The message writer state.
 */
typedef struct IPCMSGWRITER
{
    /** The buffer base (for freeing). */
    uint8_t             *pbBuf;
    /** The current buffer pointer. */
    uint8_t             *pbBufCur;
    /** The end of the buffer (exclusive). */
    uint8_t             *pbBufEnd;
    /** Flag whether an error occurred. */
    bool                fError;
} IPCMSGWRITER;
/** Pointer to a IPC message reader. */
typedef IPCMSGWRITER *PIPCMSGWRITER;
/** Pointer to a const IPC message reader. */
typedef const IPCMSGWRITER *PCIPCMSGWRITER;


/**
 * Initialises the given IPC message writer state and allocates the given amount
 * initial buffer space.
 *
 * @param   pThis           The IPC message writer to initialize.
 * @param   cbInitial       How much space to reserve in the buffer in bytes initially.
 */
DECLINLINE(void) IPCMsgWriterInit(PIPCMSGWRITER pThis, size_t cbInitial)
{
    pThis->pbBuf = (uint8_t *)RTMemAlloc(cbInitial);
    if (RT_UNLIKELY(!pThis->pbBuf))
    {
        pThis->pbBufCur = NULL;
        pThis->pbBufEnd = NULL;
        pThis->pbBufCur = NULL;
        pThis->fError   = true;
        return;
    }

    pThis->fError   = false;
    pThis->pbBufCur = pThis->pbBuf;
    pThis->pbBufEnd = pThis->pbBuf + cbInitial;
}


/**
 * Cleans up the given message writer instance, freeing all allocated resources.
 *
 * @param   pThis           The IPC message writer instance.
 */
DECLINLINE(void) IPCMsgWriterCleanup(PIPCMSGWRITER pThis)
{
    if (pThis->pbBuf)
        RTMemFree(pThis->pbBuf);
}


/**
 * Returns whether the given IPC message writer encountered an error (out of memory)
 * during its current lifetime.
 *
 * @param   pThis           The IPC message writer instance.
 */
DECL_FORCE_INLINE(bool) IPCMsgWriterHasError(PCIPCMSGWRITER pThis)
{
    return pThis->fError;
}


/**
 * Returns a pointer to the message buffer of the given IPC message writer instance.
 *
 * @returns Pointer to the start of the message buffer.
 * @param   pThis           The IPC message writer instance.
 */
DECL_FORCE_INLINE(const uint8_t *) IPCMsgWriterGetBuf(PCIPCMSGWRITER pThis)
{
    return pThis->pbBuf;
}


/**
 * Returns the current size of the message in bytes of the given IPC message writer instance.
 *
 * @returns Size of the message in bytes.
 * @param   pThis           The IPC message writer instance.
 */
DECL_FORCE_INLINE(size_t) IPCMsgWriterGetSize(PCIPCMSGWRITER pThis)
{
    return pThis->pbBufCur - pThis->pbBuf;
}


/**
 * Grows the message buffer of the given IPC message writer instance by at least the
 * given amount of bytes.
 *
 * @returns true if success, false on out of memory error (error flag is set).
 * @param   pThis           The IPC message writer instance.
 * @param   cbAdditional    How much to grow the buffer in bytes.
 */
DECLINLINE(bool) IPCMsgWriterEnsureCapacitySlow(PIPCMSGWRITER pThis, size_t cbAdditional)
{
    if (RT_UNLIKELY(pThis->fError))
        return false;

    const size_t cbOld = pThis->pbBufEnd - pThis->pbBuf;
    const size_t cbNew = RT_ALIGN_Z(cbOld + cbAdditional, 128);
    uint8_t *pbBufNew = (uint8_t *)RTMemRealloc(pThis->pbBuf, cbNew);
    if (RT_UNLIKELY(!pbBufNew))
    {
        pThis->fError = true;
        return false;
    }

    const size_t offBufCur = pThis->pbBufCur - pThis->pbBuf;
    pThis->pbBuf    = pbBufNew;
    pThis->pbBufCur = pThis->pbBuf + offBufCur;
    pThis->pbBufEnd = pThis->pbBuf + cbNew;
    return true;
}


/**
 * Forced inline helper ensuring that message buffer of the given IPC message writer instance
 * has at least the given amount of free bytes left, trying to grow the buffer.
 *
 * @returns true if success, false on out of memory error (error flag is set).
 * @param   pThis           The IPC message writer instance.
 * @param   cbAdditional    How much to grow the buffer in bytes.
 */
DECL_FORCE_INLINE(bool) IPCMsgWriterEnsureCapacity(PIPCMSGWRITER pThis, size_t cbAdditional)
{
    if (RT_LIKELY(pThis->pbBufCur + cbAdditional <= pThis->pbBufEnd))
        return true;

    return IPCMsgWriterEnsureCapacitySlow(pThis, cbAdditional);
}


/**
 * Appends the given byte to the message for the given message writer.
 *
 * @param   pThis           The IPC message writer instance.
 * @param   u8              The byte value to append.
 */
DECL_FORCE_INLINE(void) IPCMsgWriterPutU8(PIPCMSGWRITER pThis, uint8_t u8)
{
    if (IPCMsgWriterEnsureCapacity(pThis, sizeof(uint8_t)))
    { /* likely */ }
    else return;

    *pThis->pbBufCur++ = u8;
}


/**
 * Appends the given 16-bit value to the message for the given message writer.
 *
 * @param   pThis           The IPC message writer instance.
 * @param   u16             The 16-bit value to append.
 */
DECL_FORCE_INLINE(void) IPCMsgWriterPutU16(PIPCMSGWRITER pThis, uint16_t u16)
{
    if (IPCMsgWriterEnsureCapacity(pThis, sizeof(uint16_t)))
    { /* likely */ }
    else return;

#ifdef IPC_MSG_WRITER_UNALIGNED_ACCESS
    *(uint16_t *)pThis->pbBufCur = u16;
#else
    uint8_t abTmp[2];
    *(uint16_t *)&abTmp[0] = u16;
    *pThis->pbBufCur++ = abTmp[0];
    *pThis->pbBufCur++ = abTmp[1];
#endif
    pThis->pbBufCur += sizeof(uint16_t);
}


/**
 * Appends the given 32-bit value to the message for the given message writer.
 *
 * @param   pThis           The IPC message writer instance.
 * @param   u32             The 32-bit value to append.
 */
DECL_FORCE_INLINE(void) IPCMsgWriterPutU32(PIPCMSGWRITER pThis, uint32_t u32)
{
    if (IPCMsgWriterEnsureCapacity(pThis, sizeof(uint32_t)))
    { /* likely */ }
    else return;

#ifdef IPC_MSG_WRITER_UNALIGNED_ACCESS
    *(uint32_t *)pThis->pbBufCur = u32;
#else
    uint8_t abTmp[4];
    *(uint32_t *)&abTmp[0] = u32;
    *pThis->pbBufCur++ = abTmp[0];
    *pThis->pbBufCur++ = abTmp[1];
    *pThis->pbBufCur++ = abTmp[2];
    *pThis->pbBufCur++ = abTmp[3];
#endif
    pThis->pbBufCur += sizeof(uint32_t);
}


/**
 * Appends the given 64-bit value to the message for the given message writer.
 *
 * @param   pThis           The IPC message writer instance.
 * @param   u64             The 64-bit value to append.
 */
DECL_FORCE_INLINE(void) IPCMsgWriterPutU64(PIPCMSGWRITER pThis, uint64_t u64)
{
    if (IPCMsgWriterEnsureCapacity(pThis, sizeof(uint64_t)))
    { /* likely */ }
    else return;

#ifdef IPC_MSG_WRITER_UNALIGNED_ACCESS
    *(uint64_t *)pThis->pbBufCur = u64;
#else
    uint8_t abTmp[8];
    *(uint64_t *)&abTmp[0] = u64;
    *pThis->pbBufCur++ = abTmp[0];
    *pThis->pbBufCur++ = abTmp[1];
    *pThis->pbBufCur++ = abTmp[2];
    *pThis->pbBufCur++ = abTmp[3];
    *pThis->pbBufCur++ = abTmp[4];
    *pThis->pbBufCur++ = abTmp[5];
    *pThis->pbBufCur++ = abTmp[6];
    *pThis->pbBufCur++ = abTmp[7];
#endif
    pThis->pbBufCur += sizeof(uint64_t);
}


/**
 * Appends the given buffer to the message for the given message writer.
 *
 * @param   pThis           The IPC message writer instance.
 * @param   pvBuf           The buffer to append.
 * @param   cbBuf           How many bytes to append.
 */
DECL_FORCE_INLINE(void) IPCMsgWriterPutBytes(PIPCMSGWRITER pThis, const void *pvBuf, size_t cbBuf)
{
    if (IPCMsgWriterEnsureCapacity(pThis, cbBuf))
    { /* likely */ }
    else return;

    memcpy(pThis->pbBufCur, pvBuf, cbBuf);
    pThis->pbBufCur += cbBuf;
}

#endif /* ipcMsgWriter_h__ */
