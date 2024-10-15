/* $Id: pdmcardreaderinfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * cardreaderinfs - interface between USB Card Reader device and its driver.
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

#ifndef VBOX_INCLUDED_vmm_pdmcardreaderinfs_h
#define VBOX_INCLUDED_vmm_pdmcardreaderinfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>


/** @defgroup grp_pdm_ifs_cardreader    PDM USB Card Reader Interfaces
 * @ingroup grp_pdm_interfaces
 * @{
 */


typedef struct PDMICARDREADER_IO_REQUEST
{
    uint32_t u32Protocol;       /**< Protocol identifier */
    uint32_t cbPciLength;       /**< Protocol Control Information Length */
    /* 'cbPciLength - 8' bytes of control info may follow. */
} PDMICARDREADER_IO_REQUEST;

typedef struct PDMICARDREADER_READERSTATE
{
    char *pszReaderName;
    uint32_t u32CurrentState;   /**< Current state of reader at time of call. */
    uint32_t u32EventState;     /**< State of reader after state change */
    uint32_t cbAtr;             /**< Number of bytes in the returned ATR. */
    uint8_t au8Atr[36];         /**< Atr of inserted card, (extra alignment bytes) */
} PDMICARDREADER_READERSTATE;


#define PDMICARDREADERDOWN_IID  "78d65378-889c-4418-8bc2-7a89a5af2817"
typedef struct PDMICARDREADERDOWN PDMICARDREADERDOWN;
typedef PDMICARDREADERDOWN *PPDMICARDREADERDOWN;
struct PDMICARDREADERDOWN
{
    DECLR3CALLBACKMEMBER(int, pfnEstablishContext,(PPDMICARDREADERDOWN pInterface));
    DECLR3CALLBACKMEMBER(int, pfnConnect,(PPDMICARDREADERDOWN pInterface, void *pvUser, const char *pszCardReaderName,
                                          uint32_t u32ShareMode, uint32_t u32PreferredProtocols));
    DECLR3CALLBACKMEMBER(int, pfnDisconnect,(PPDMICARDREADERDOWN pInterface, void *pvUser, uint32_t u32Disposition));
    DECLR3CALLBACKMEMBER(int, pfnStatus,(PPDMICARDREADERDOWN pInterface, void *pvUser, uint32_t cchReaderName, uint32_t cbAtrLen));
    DECLR3CALLBACKMEMBER(int, pfnReleaseContext,(PPDMICARDREADERDOWN pInterface, void *pvUser));
    DECLR3CALLBACKMEMBER(int, pfnGetStatusChange,(PPDMICARDREADERDOWN pInterface, void *pvUser, uint32_t u32Timeout,
                                                  PDMICARDREADER_READERSTATE *paReaderStats, uint32_t cReaderStats));
    DECLR3CALLBACKMEMBER(int, pfnBeginTransaction,(PPDMICARDREADERDOWN pInterface, void *pvUser));
    DECLR3CALLBACKMEMBER(int, pfnEndTransaction,(PPDMICARDREADERDOWN pInterface, void *pvUser, uint32_t u32Disposition));
    DECLR3CALLBACKMEMBER(int, pfnTransmit,(PPDMICARDREADERDOWN pInterface, void *pvUser,
                                           const PDMICARDREADER_IO_REQUEST *pIoSendRequest,
                                           const uint8_t *pu8SendBuffer, uint32_t cbSendBuffer, uint32_t cbRecvBuffer));
    /**
     * Up level provides pvInBuffer of cbInBuffer bytes to call SCardControl, also it specify bytes it expects to receive
     * @note    Device/driver implementation should copy buffers before execution in
     *          async mode, and both layers shouldn't expect permanent storage for the
     *          buffer.
     */
    DECLR3CALLBACKMEMBER(int, pfnControl,(PPDMICARDREADERDOWN pInterface, void *pvUser,
                                          uint32_t u32ControlCode, const void *pvInBuffer,
                                          uint32_t cbInBuffer, uint32_t cbOutBuffer));
    /**
     * This function ask driver to provide attribute (dwAttribId) and provide limit (cbAttrib) of buffer size for attribute value,
     * Callback UpGetAttrib returns buffer containing the value and altered size of the buffer.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetAttr,(PPDMICARDREADERDOWN pInterface, void *pvUser,
                                          uint32_t u32AttribId, uint32_t cbAttrib));
    DECLR3CALLBACKMEMBER(int, pfnSetAttr,(PPDMICARDREADERDOWN pInterface, void *pvUser,
                                          uint32_t u32AttribId, const void *pvAttrib, uint32_t cbAttrib));
};

#define PDMICARDREADERUP_IID    "c0d7498e-0635-48ca-aab1-b11b6a55cf7d"
typedef struct PDMICARDREADERUP PDMICARDREADERUP;
typedef PDMICARDREADERUP *PPDMICARDREADERUP;
struct PDMICARDREADERUP
{
    DECLR3CALLBACKMEMBER(int, pfnEstablishContext,(PPDMICARDREADERUP pInterface, int32_t lSCardRc));
    DECLR3CALLBACKMEMBER(int, pfnStatus,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                         char *pszReaderName, uint32_t cchReaderName, uint32_t u32CardState,
                                         uint32_t u32Protocol, uint8_t *pu8Atr, uint32_t cbAtr));
    DECLR3CALLBACKMEMBER(int, pfnConnect,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                          uint32_t u32ActiveProtocol));
    DECLR3CALLBACKMEMBER(int, pfnDisconnect,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc));
    DECLR3CALLBACKMEMBER(int, pfnSetStatusChange,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                                  PDMICARDREADER_READERSTATE *paReaderStats, uint32_t cReaderStats));
    DECLR3CALLBACKMEMBER(int, pfnBeginTransaction,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc));
    DECLR3CALLBACKMEMBER(int, pfnEndTransaction,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc));
    /* Note: pioRecvPci stack variable */
    DECLR3CALLBACKMEMBER(int, pfnTransmit,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                           const PDMICARDREADER_IO_REQUEST *pioRecvPci,
                                           uint8_t *pu8RecvBuffer, uint32_t cbRecvBuffer));
    DECLR3CALLBACKMEMBER(int, pfnControl,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                          uint32_t u32ControlCode, void *pvOutBuffer, uint32_t cbOutBuffer));
    DECLR3CALLBACKMEMBER(int, pfnGetAttrib,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc,
                                            uint32_t u32AttribId, void *pvAttrib, uint32_t cbAttrib));
    DECLR3CALLBACKMEMBER(int, pfnSetAttrib,(PPDMICARDREADERUP pInterface, void *pvUser, int32_t lSCardRc, uint32_t u32AttribId));
};

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmcardreaderinfs_h */

