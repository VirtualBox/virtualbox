/* $Id: HGSMIHost.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI), host part.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_Graphics_HGSMI_HGSMIHost_h
#define VBOX_INCLUDED_SRC_Graphics_HGSMI_HGSMIHost_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <HGSMI.h>
#include <HGSMIChSetup.h>

struct HGSMIINSTANCE;
typedef struct HGSMIINSTANCE *PHGSMIINSTANCE;

/** Callback for the guest notification about a new host buffer. */
typedef DECLCALLBACKTYPE(void, FNHGSMINOTIFYGUEST,(void *pvCallback));
typedef FNHGSMINOTIFYGUEST *PFNHGSMINOTIFYGUEST;

/*
 * Public Host API for virtual devices.
 */

DECLHIDDEN(int) HGSMICreate(PHGSMIINSTANCE *ppIns,
                            PPDMDEVINS      pDevIns,
                            const char     *pszName,
                            HGSMIOFFSET     offBase,
                            uint8_t        *pu8MemBase,
                            HGSMISIZE       cbMem,
                            PFNHGSMINOTIFYGUEST pfnNotifyGuest,
                            void           *pvNotifyGuest,
                            size_t         cbContext);
DECLHIDDEN(void)   HGSMIDestroy(PHGSMIINSTANCE pIns);
DECLHIDDEN(void *) HGSMIContext(PHGSMIINSTANCE pIns);

DECLHIDDEN(void RT_UNTRUSTED_VOLATILE_GUEST *) HGSMIOffsetToPointerHost(PHGSMIINSTANCE pIns, HGSMIOFFSET offBuffer);
DECLHIDDEN(HGSMIOFFSET) HGSMIPointerToOffsetHost(PHGSMIINSTANCE pIns, const void RT_UNTRUSTED_VOLATILE_GUEST *pv);
DECLHIDDEN(bool)        HGSMIIsOffsetValid(PHGSMIINSTANCE pIns, HGSMIOFFSET offBuffer);
DECLHIDDEN(HGSMIOFFSET) HGSMIGetAreaOffset(PHGSMIINSTANCE pIns);
DECLHIDDEN(HGSMIOFFSET) HGSMIGetAreaSize(PHGSMIINSTANCE pIns);


DECLHIDDEN(int) HGSMIHostChannelRegister(PHGSMIINSTANCE pIns, uint8_t u8Channel,
                                         PFNHGSMICHANNELHANDLER pfnChannelHandler, void *pvChannelHandler);
#if 0 /* unused */
int HGSMIChannelRegisterName (PHGSMIINSTANCE pIns,
                              const char *pszChannel,
                              PFNHGSMICHANNELHANDLER pfnChannelHandler,
                              void *pvChannelHandler,
                              uint8_t *pu8Channel);
#endif

DECLHIDDEN(int) HGSMIHostHeapSetup(PHGSMIINSTANCE pIns, HGSMIOFFSET RT_UNTRUSTED_GUEST offHeap, HGSMISIZE RT_UNTRUSTED_GUEST cbHeap);

/*
 * Virtual hardware IO handlers.
 */

/* Guests passes a new command buffer to the host. */
DECLHIDDEN(void) HGSMIGuestWrite(PHGSMIINSTANCE pIns, HGSMIOFFSET offBuffer);

/* Guest reads information about guest buffers. */
DECLHIDDEN(HGSMIOFFSET) HGSMIGuestRead(PHGSMIINSTANCE pIns);

/* Guest reads the host FIFO to get a command. */
DECLHIDDEN(HGSMIOFFSET) HGSMIHostRead(PHGSMIINSTANCE pIns);

/* Guest reports that the command at this offset has been processed.  */
DECLHIDDEN(void) HGSMIHostWrite(PHGSMIINSTANCE pIns, HGSMIOFFSET offBuffer);

DECLHIDDEN(void) HGSMISetHostGuestFlags(PHGSMIINSTANCE pIns, uint32_t flags);
DECLHIDDEN(uint32_t) HGSMIGetHostGuestFlags(HGSMIINSTANCE *pIns);

DECLHIDDEN(void) HGSMIClearHostGuestFlags(PHGSMIINSTANCE pIns, uint32_t flags);

/*
 * Low level interface for submitting buffers to the guest.
 *
 * These functions are not directly available for anyone but the
 * virtual hardware device.
 */

/* Allocate a buffer in the host heap. */
DECLHIDDEN(int) HGSMIHostCommandAlloc(PHGSMIINSTANCE pIns, void RT_UNTRUSTED_VOLATILE_GUEST **ppvData, HGSMISIZE cbData,
                                      uint8_t u8Channel, uint16_t u16ChannelInfo);
DECLHIDDEN(int) HGSMIHostCommandSubmitAndFreeAsynch(PHGSMIINSTANCE pIns, void RT_UNTRUSTED_VOLATILE_GUEST *pvData, bool fDoIrq);
DECLHIDDEN(int) HGSMIHostCommandFree(PHGSMIINSTANCE pIns, void RT_UNTRUSTED_VOLATILE_GUEST *pvData);

DECLHIDDEN(int) HGSMIHostLoadStateExec(const struct PDMDEVHLPR3 *pHlp, PHGSMIINSTANCE pIns, PSSMHANDLE pSSM, uint32_t u32Version);
DECLHIDDEN(int) HGSMIHostSaveStateExec(const struct PDMDEVHLPR3 *pHlp, PHGSMIINSTANCE pIns, PSSMHANDLE pSSM);

#ifdef VBOX_WITH_WDDM
DECLHIDDEN(int) HGSMICompleteGuestCommand(PHGSMIINSTANCE pIns, void RT_UNTRUSTED_VOLATILE_GUEST *pvMem, bool fDoIrq);
#endif

/* @return host-guest flags that were set on reset
 * this allows the caller to make further cleaning when needed,
 * e.g. reset the IRQ */
DECLHIDDEN(uint32_t) HGSMIReset(PHGSMIINSTANCE pIns);

#endif /* !VBOX_INCLUDED_SRC_Graphics_HGSMI_HGSMIHost_h */

