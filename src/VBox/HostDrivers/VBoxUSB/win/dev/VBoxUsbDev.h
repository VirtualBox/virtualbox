/* $Id: VBoxUsbDev.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxUsbDev.h - USB device.
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbDev_h
#define VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbDev_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxUsbCmn.h"
#include <VBox/cdefs.h>
#include <iprt/assert.h>

typedef struct VBOXUSB_GLOBALS
{
    PDRIVER_OBJECT pDrvObj;
    UNICODE_STRING RegPath;
    VBOXUSBRT_IDC RtIdc;
} VBOXUSB_GLOBALS, *PVBOXUSB_GLOBALS;

extern VBOXUSB_GLOBALS g_VBoxUsbGlobals;

/* pnp state decls */
typedef enum
{
    ENMVBOXUSB_PNPSTATE_UNKNOWN = 0,
    ENMVBOXUSB_PNPSTATE_START_PENDING,
    ENMVBOXUSB_PNPSTATE_STARTED,
    ENMVBOXUSB_PNPSTATE_STOP_PENDING,
    ENMVBOXUSB_PNPSTATE_STOPPED,
    ENMVBOXUSB_PNPSTATE_SURPRISE_REMOVED,
    ENMVBOXUSB_PNPSTATE_REMOVE_PENDING,
    ENMVBOXUSB_PNPSTATE_REMOVED,
    ENMVBOXUSB_PNPSTATE_FORSEDWORD = 0x8fffffff
} ENMVBOXUSB_PNPSTATE;
AssertCompile(sizeof (ENMVBOXUSB_PNPSTATE) == sizeof (uint32_t));

#ifdef VBOX_STRICT
DECLHIDDEN(VOID) vboxUsbPnPStateGbgChange(ENMVBOXUSB_PNPSTATE enmOld, ENMVBOXUSB_PNPSTATE enmNew);
# define VBOXUSB_PNP_GBG_STATE_CHANGE(_old, _new) vboxUsbPnPStateGbgChange((_old), (_new))
#else
# define VBOXUSB_PNP_GBG_STATE_CHANGE(_old, _new) do { } while (0)
#endif


typedef struct VBOXUSB_PNPSTATE
{
    /* Current state */
    volatile ENMVBOXUSB_PNPSTATE Curr;
    /* Previous state, used to restore state info on cancell stop device */
    ENMVBOXUSB_PNPSTATE Prev;
} VBOXUSB_PNPSTATE, *PVBOXUSB_PNPSTATE;

typedef struct VBOXUSBDEV_DDISTATE
{
    /* Lock */
    KSPIN_LOCK Lock;
    VBOXDRVTOOL_REF Ref;
    VBOXUSB_PNPSTATE PnPState;
    VBOXUSB_PWRSTATE PwrState;
    /* current dev caps */
    DEVICE_CAPABILITIES DevCaps;
} VBOXUSBDEV_DDISTATE, *PVBOXUSBDEV_DDISTATE;

typedef struct VBOXUSBDEV_EXT
{
    PDEVICE_OBJECT pFDO;
    PDEVICE_OBJECT pPDO;
    PDEVICE_OBJECT pLowerDO;

    VBOXUSBDEV_DDISTATE DdiState;

    uint32_t cHandles;

    VBOXUSB_RT Rt;

} VBOXUSBDEV_EXT, *PVBOXUSBDEV_EXT;

/* pnp state api */
DECLINLINE(ENMVBOXUSB_PNPSTATE) vboxUsbPnPStateGet(PVBOXUSBDEV_EXT pDevExt)
{
    return (ENMVBOXUSB_PNPSTATE)ASMAtomicUoReadU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr);
}

DECLINLINE(ENMVBOXUSB_PNPSTATE) vboxUsbPnPStateSet(PVBOXUSBDEV_EXT pDevExt, ENMVBOXUSB_PNPSTATE enmState)
{
    KIRQL Irql;
    ENMVBOXUSB_PNPSTATE enmOldState;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &Irql);
    pDevExt->DdiState.PnPState.Prev = (ENMVBOXUSB_PNPSTATE)ASMAtomicUoReadU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr);
    ASMAtomicWriteU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr, (uint32_t)enmState);
    pDevExt->DdiState.PnPState.Curr = enmState;
    enmOldState = pDevExt->DdiState.PnPState.Prev;
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, Irql);
    VBOXUSB_PNP_GBG_STATE_CHANGE(enmOldState, enmState);
    return enmState;
}

DECLINLINE(ENMVBOXUSB_PNPSTATE) vboxUsbPnPStateRestore(PVBOXUSBDEV_EXT pDevExt)
{
    ENMVBOXUSB_PNPSTATE enmNewState, enmOldState;
    KIRQL Irql;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &Irql);
    enmOldState = pDevExt->DdiState.PnPState.Curr;
    enmNewState = pDevExt->DdiState.PnPState.Prev;
    ASMAtomicWriteU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr, (uint32_t)pDevExt->DdiState.PnPState.Prev);
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, Irql);
    VBOXUSB_PNP_GBG_STATE_CHANGE(enmOldState, enmNewState);
    Assert(enmNewState == ENMVBOXUSB_PNPSTATE_STARTED);
    Assert(enmOldState == ENMVBOXUSB_PNPSTATE_STOP_PENDING
            || enmOldState == ENMVBOXUSB_PNPSTATE_REMOVE_PENDING);
    return enmNewState;
}

DECLINLINE(VOID) vboxUsbPnPStateInit(PVBOXUSBDEV_EXT pDevExt)
{
    pDevExt->DdiState.PnPState.Curr = pDevExt->DdiState.PnPState.Prev = ENMVBOXUSB_PNPSTATE_START_PENDING;
}

DECLINLINE(VOID) vboxUsbDdiStateInit(PVBOXUSBDEV_EXT pDevExt)
{
    KeInitializeSpinLock(&pDevExt->DdiState.Lock);
    VBoxDrvToolRefInit(&pDevExt->DdiState.Ref);
    vboxUsbPwrStateInit(pDevExt);
    vboxUsbPnPStateInit(pDevExt);
}

DECLINLINE(bool) vboxUsbDdiStateRetainIfStarted(PVBOXUSBDEV_EXT pDevExt)
{
    KIRQL oldIrql;
    bool bRetained = true;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &oldIrql);
    if (vboxUsbPnPStateGet(pDevExt) == ENMVBOXUSB_PNPSTATE_STARTED)
    {
        VBoxDrvToolRefRetain(&pDevExt->DdiState.Ref);
    }
    else
    {
        bRetained = false;
    }
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, oldIrql);
    return bRetained;
}

/* if device is removed - does nothing and returns zero,
 * otherwise increments a ref counter and returns the current pnp state
 * NOTE: never returns ENMVBOXUSB_PNPSTATE_REMOVED
 * */
DECLINLINE(ENMVBOXUSB_PNPSTATE) vboxUsbDdiStateRetainIfNotRemoved(PVBOXUSBDEV_EXT pDevExt)
{
    KIRQL oldIrql;
    ENMVBOXUSB_PNPSTATE enmState;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &oldIrql);
    enmState = vboxUsbPnPStateGet(pDevExt);
    if (enmState != ENMVBOXUSB_PNPSTATE_REMOVED)
    {
        VBoxDrvToolRefRetain(&pDevExt->DdiState.Ref);
    }
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, oldIrql);
    return enmState != ENMVBOXUSB_PNPSTATE_REMOVED ? enmState : (ENMVBOXUSB_PNPSTATE)0;
}

DECLINLINE(uint32_t) vboxUsbDdiStateRetain(PVBOXUSBDEV_EXT pDevExt)
{
    return VBoxDrvToolRefRetain(&pDevExt->DdiState.Ref);
}

DECLINLINE(uint32_t) vboxUsbDdiStateRelease(PVBOXUSBDEV_EXT pDevExt)
{
    return VBoxDrvToolRefRelease(&pDevExt->DdiState.Ref);
}

DECLINLINE(VOID) vboxUsbDdiStateReleaseAndWaitCompleted(PVBOXUSBDEV_EXT pDevExt)
{
    VBoxDrvToolRefRelease(&pDevExt->DdiState.Ref);
    VBoxDrvToolRefWaitEqual(&pDevExt->DdiState.Ref, 1);
}

DECLINLINE(VOID) vboxUsbDdiStateReleaseAndWaitRemoved(PVBOXUSBDEV_EXT pDevExt)
{
    VBoxDrvToolRefRelease(&pDevExt->DdiState.Ref);
    VBoxDrvToolRefWaitEqual(&pDevExt->DdiState.Ref, 0);
}

#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbDev_h */
