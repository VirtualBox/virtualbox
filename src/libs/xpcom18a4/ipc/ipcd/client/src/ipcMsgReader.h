/* $Id: ipcMsgReader.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM - IPC message reader helper.
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


#ifndef ipcMsgReader_h__
#define ipcMsgReader_h__

#include <iprt/types.h>
#include <iprt/string.h>

/** Whether to allow unaligned memory access. */
#define IPC_MSG_READER_UNALIGNED_ACCESS

/**
 * The message reader state.
 */
typedef struct IPCMSGREADER
{
    /** The current buffer pointer. */
    const uint8_t       *pbBufCur;
    /** The end of the buffer (exclusive). */
    const uint8_t       *pbBufEnd;
    /** Flag whether an error occurred. */
    bool                fError;
} IPCMSGREADER;
/** Pointer to a IPC message reader. */
typedef IPCMSGREADER *PIPCMSGREADER;
/** Pointer to a const IPC message reader. */
typedef const IPCMSGREADER *PCIPCMSGREADER;


/**
 * Initializes the given message reader state with the given payload.
 *
 * @param   pThis       The messager reader state to initialize.
 * @param   pvMsg       The message payload data.
 * @param   cbMsg       Size of the message payload in bytes.
 */
DECL_FORCE_INLINE(void) IPCMsgReaderInit(PIPCMSGREADER pThis, const void *pvMsg, size_t cbMsg)
{
    pThis->pbBufCur = (const uint8_t *)pvMsg;
    pThis->pbBufEnd = pThis->pbBufCur + cbMsg;
    pThis->fError   = false;
}


/**
 * Returns whether the message reader encountered an error.
 *
 * @returns true if an error (attempt to read more than there was left in the message buffer).
 * @param   pThis       The messager reader instance.
 */
DECL_FORCE_INLINE(bool) IPCMsgReaderHasError(PIPCMSGREADER pThis)
{
    return pThis->fError;
}


/**
 * Returns a byte value from the given message reader.
 *
 * @returns Byte value.
 * @param   pThis       The messager reader instance.
 */
DECL_FORCE_INLINE(uint8_t) IPCMsgReaderGetU8(PIPCMSGREADER pThis)
{
    if (RT_LIKELY(pThis->pbBufCur < pThis->pbBufEnd))
        return *pThis->pbBufCur++;

    pThis->fError = true;
    return 0;
}


/**
 * Returns a 16-bit value from the given message reader.
 *
 * @returns 16-bit value.
 * @param   pThis       The messager reader instance.
 */
DECL_FORCE_INLINE(uint16_t) IPCMsgReaderGetU16(PIPCMSGREADER pThis)
{
    if (RT_LIKELY(pThis->pbBufCur + sizeof(uint16_t) <= pThis->pbBufEnd))
    {
        uint16_t u16;

#ifdef IPC_MSG_READER_UNALIGNED_ACCESS
        u16 = *(const uint16_t *)pThis->pbBufCur;
#else
        const uint8_t abTmp[2] = { pThis->pbBufCur[0], pThis->pbBufCur[1] };
        u16 = *(const uint16_t *)&abTmp[0];
#endif

        pThis->pbBufCur += sizeof(uint16_t);
        return u16;
    }

    pThis->fError = true;
    return 0;
}


/**
 * Returns a 32-bit value from the given message reader.
 *
 * @returns 32-bit value.
 * @param   pThis       The messager reader instance.
 */
DECL_FORCE_INLINE(uint32_t) IPCMsgReaderGetU32(PIPCMSGREADER pThis)
{
    if (RT_LIKELY(pThis->pbBufCur + sizeof(uint32_t) <= pThis->pbBufEnd))
    {
        uint32_t u32;

#ifdef IPC_MSG_READER_UNALIGNED_ACCESS
        u32 = *(const uint32_t *)pThis->pbBufCur;
#else
        const uint8_t abTmp[4] = { pThis->pbBufCur[0], pThis->pbBufCur[1], pThis->pbBufCur[2], pThis->pbBufCur[3] };
        u32 = *(const uint32_t *)&abTmp[0];
#endif

        pThis->pbBufCur += sizeof(uint32_t);
        return u32;
    }

    pThis->fError = true;
    return 0;
}


/**
 * Returns a 64-bit value from the given message reader.
 *
 * @returns 64-bit value.
 * @param   pThis       The messager reader instance.
 */
DECL_FORCE_INLINE(uint64_t) IPCMsgReaderGetU64(PIPCMSGREADER pThis)
{
    if (RT_LIKELY(pThis->pbBufCur + sizeof(uint64_t) <= pThis->pbBufEnd))
    {
        uint64_t u64;

#ifdef IPC_MSG_READER_UNALIGNED_ACCESS
        u64 = *(const uint64_t *)pThis->pbBufCur;
#else
        const uint8_t abTmp[8] = { pThis->pbBufCur[0], pThis->pbBufCur[1], pThis->pbBufCur[2], pThis->pbBufCur[3],
                                   pThis->pbBufCur[4], pThis->pbBufCur[5], pThis->pbBufCur[6], pThis->pbBufCur[7] };
        u64 = *(const uint64_t *)&abTmp[0];
#endif

        pThis->pbBufCur += sizeof(uint64_t);
        return u64;
    }

    pThis->fError = true;
    return 0;
}


/**
 * Tries to read the given amount of bytes into the supplied buffer from he message reader.
 *
 * @param   pThis       The messager reader instance.
 * @param   pvDst       Where to store the data on success.
 * @param   cbRead      Number of bytes to read.
 */
DECL_FORCE_INLINE(void) IPCMsgReaderReadBytes(PIPCMSGREADER pThis, void *pvDst, size_t cbRead)
{
    if (RT_LIKELY(pThis->pbBufCur + cbRead <= pThis->pbBufEnd))
    {
        memcpy(pvDst, pThis->pbBufCur, cbRead);
        pThis->pbBufCur += cbRead;
        return;
    }

    pThis->fError = true;
}


#endif /* ipcMsgReader_h__ */
