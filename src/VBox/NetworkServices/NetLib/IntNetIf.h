/* $Id: IntNetIf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IntNetIf - Convenience class implementing an IntNet connection.
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

#ifndef VBOX_INCLUDED_SRC_NetLib_IntNetIf_h
#define VBOX_INCLUDED_SRC_NetLib_IntNetIf_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

#include <iprt/initterm.h>
#include <iprt/cpp/ministring.h>

#include <VBox/sup.h>
#include <VBox/vmm/vmm.h>
#include <VBox/intnet.h>



/**
 * Low-level internal network access helpers to hide away the different variants (R0 SUP or R3 XPC on macOS).
 */
/** Internal networking interface context handle. */
typedef struct INTNETIFCTXINT *INTNETIFCTX;
/** Pointer to an internal networking interface context handle. */
typedef INTNETIFCTX *PINTNETIFCTX;

/**
 * User input callback function.
 *
 * @param pvUser    The user specified argument.
 * @param pvFrame   The pointer to the frame data.
 * @param cbFrame   The length of the frame data.
 */
typedef DECLCALLBACKTYPE(void, FNINPUT,(void *pvUser, void *pvFrame, uint32_t cbFrame));

/** Pointer to the user input callback function. */
typedef FNINPUT *PFNINPUT;

/**
 * User GSO input callback function.
 *
 * @param pvUser    The user specified argument.
 * @param pcGso     The pointer to the GSO context.
 * @param cbFrame   The length of the GSO data.
 */
typedef DECLCALLBACKTYPE(void, FNINPUTGSO,(void *pvUser, PCPDMNETWORKGSO pcGso, uint32_t cbFrame));

/** Pointer to the user GSO input callback function. */
typedef FNINPUTGSO *PFNINPUTGSO;


/**
 * An output frame in the send ring buffer.
 *
 * Obtained with IntNetR3IfCtxQueryOutputFrame().  Caller should copy frame
 * contents to pvFrame and pass the frame structure to IntNetR3IfCtxOutputFrameCommit()
 * to be sent to the network.
 */
typedef struct INTNETFRAME
{
    /** The intrnal network frame header. */
    PINTNETHDR pHdr;
    /** The actual frame data. */
    void       *pvFrame;
} INTNETFRAME;
typedef INTNETFRAME *PINTNETFRAME;
typedef const INTNETFRAME *PCINTNETFRAME;


DECLHIDDEN(int) IntNetR3IfCreate(PINTNETIFCTX phIfCtx, const char *pszNetwork);
DECLHIDDEN(int) IntNetR3IfCreateEx(PINTNETIFCTX phIfCtx, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                                   const char *pszTrunk, uint32_t cbSend, uint32_t cbRecv, uint32_t fFlags);
DECLHIDDEN(int) IntNetR3IfDestroy(INTNETIFCTX hIfCtx);
DECLHIDDEN(int) IntNetR3IfQueryBufferPtr(INTNETIFCTX hIfCtx, PINTNETBUF *ppIfBuf);
DECLHIDDEN(int) IntNetR3IfSetActive(INTNETIFCTX hIfCtx, bool fActive);
DECLHIDDEN(int) IntNetR3IfSetPromiscuous(INTNETIFCTX hIfCtx, bool fPromiscuous);
DECLHIDDEN(int) IntNetR3IfSend(INTNETIFCTX hIfCtx);
DECLHIDDEN(int) IntNetR3IfWait(INTNETIFCTX hIfCtx, uint32_t cMillies);
DECLHIDDEN(int) IntNetR3IfWaitAbort(INTNETIFCTX hIfCtx);

DECLHIDDEN(int) IntNetR3IfPumpPkts(INTNETIFCTX hIfCtx, PFNINPUT pfnInput, void *pvUser,
                                   PFNINPUTGSO pfnInputGso, void *pvUserGso);
DECLHIDDEN(int) IntNetR3IfQueryOutputFrame(INTNETIFCTX hIfCtx, uint32_t cbFrame, PINTNETFRAME pFrame);
DECLHIDDEN(int) IntNetR3IfOutputFrameCommit(INTNETIFCTX hIfCtx, PCINTNETFRAME pFrame);

#endif /* !VBOX_INCLUDED_SRC_NetLib_IntNetIf_h */
