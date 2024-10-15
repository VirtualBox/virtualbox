/* $Id: VBoxNetAdpInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdp - Network Filter Driver (Host), Internal Header.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetAdp_VBoxNetAdpInternal_h
#define VBOX_INCLUDED_SRC_VBoxNetAdp_VBoxNetAdpInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/sup.h>
#include <VBox/intnet.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>


RT_C_DECLS_BEGIN

/** Pointer to the globals. */
typedef struct VBOXNETADPGLOBALS *PVBOXNETADPGLOBALS;

#define VBOXNETADP_MAX_INSTANCES   128
#define VBOXNETADP_MAX_UNITS       128
#define VBOXNETADP_NAME            "vboxnet"
#define VBOXNETADP_MAX_NAME_LEN    32
#define VBOXNETADP_MTU             1500
#if defined(RT_OS_DARWIN)
# define VBOXNETADP_MAX_FAMILIES   4
# define VBOXNETADP_DETACH_TIMEOUT 500
#endif

#define VBOXNETADP_CTL_DEV_NAME    "vboxnetctl"
#define VBOXNETADP_CTL_ADD   _IOWR('v', 1, VBOXNETADPREQ)
#define VBOXNETADP_CTL_REMOVE _IOW('v', 2, VBOXNETADPREQ)

typedef struct VBoxNetAdpReq
{
    char szName[VBOXNETADP_MAX_NAME_LEN];
} VBOXNETADPREQ;
typedef VBOXNETADPREQ *PVBOXNETADPREQ;

/**
 * Void entries mark vacant slots in adapter array. Valid entries are busy slots.
 * As soon as slot is being modified its state changes to transitional.
 * An entry in transitional state must only be accessed by the thread that
 * put it to this state.
 */
/**
 * To avoid races on adapter fields we stick to the following rules:
 * - rewrite: Int net port calls are serialized
 * - No modifications are allowed on busy adapters (deactivate first)
 *     Refuse to destroy adapter until it gets to available state
 * - No transfers (thus getting busy) on inactive adapters
 * - Init sequence: void->available->connected->active
     1) Create
     2) Connect
     3) Activate
 * - Destruction sequence: active->connected->available->void
     1) Deactivate
     2) Disconnect
     3) Destroy
*/

enum VBoxNetAdpState
{
    kVBoxNetAdpState_Invalid,
    kVBoxNetAdpState_Transitional,
    kVBoxNetAdpState_Active,
    kVBoxNetAdpState_32BitHack = 0x7FFFFFFF
};
typedef enum VBoxNetAdpState VBOXNETADPSTATE;

struct VBoxNetAdapter
{
    /** Denotes availability of this slot in adapter array. */
    VBOXNETADPSTATE   enmState;
    /** Corresponds to the digit at the end of device name. */
    int               iUnit;

    union
    {
#ifdef VBOXNETADP_OS_SPECFIC
        struct
        {
# if defined(RT_OS_DARWIN)
            /** @name Darwin instance data.
             * @{ */
            /** Event to signal detachment of interface. */
            RTSEMEVENT        hEvtDetached;
            /** Pointer to Darwin interface structure. */
            ifnet_t           pIface;
            /** MAC address. */
            RTMAC             Mac;
            /** @} */
# elif defined(RT_OS_LINUX)
            /** @name Darwin instance data.
             * @{ */
            /** Pointer to Linux network device structure. */
            struct net_device *pNetDev;
            /** @} */
# elif defined(RT_OS_FREEBSD)
            /** @name FreeBSD instance data.
             * @{ */
            struct ifnet *ifp;
            /** @} */
# else
# error PORTME
# endif
        } s;
#endif
        /** Union alignment to a pointer. */
        void *pvAlign;
        /** Padding. */
        uint8_t abPadding[64];
    } u;
    /** The interface name. */
    char szName[VBOXNETADP_MAX_NAME_LEN];
};
typedef struct VBoxNetAdapter VBOXNETADP;
typedef VBOXNETADP *PVBOXNETADP;
/* Paranoia checks for alignment and padding. */
AssertCompileMemberAlignment(VBOXNETADP, u, ARCH_BITS/8);
AssertCompileMemberAlignment(VBOXNETADP, szName, ARCH_BITS/8);
AssertCompileMembersSameSize(VBOXNETADP, u, VBOXNETADP, u.abPadding);

DECLHIDDEN(int) vboxNetAdpInit(void);
DECLHIDDEN(void) vboxNetAdpShutdown(void);
DECLHIDDEN(int) vboxNetAdpCreate(PVBOXNETADP *ppNew, const char *pcszName);
DECLHIDDEN(int) vboxNetAdpDestroy(PVBOXNETADP pThis);
DECLHIDDEN(PVBOXNETADP) vboxNetAdpFindByName(const char *pszName);
DECLHIDDEN(void) vboxNetAdpComposeMACAddress(PVBOXNETADP pThis, PRTMAC pMac);


/**
 * This is called to perform OS-specific structure initializations.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) vboxNetAdpOsInit(PVBOXNETADP pThis);

/**
 * Counter part to vboxNetAdpOsCreate().
 *
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(void) vboxNetAdpOsDestroy(PVBOXNETADP pThis);

/**
 * This is called to attach to the actual host interface
 * after linking the instance into the list.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pMac            The MAC address to use for this instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) vboxNetAdpOsCreate(PVBOXNETADP pThis, PCRTMAC pMac);



RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VBoxNetAdp_VBoxNetAdpInternal_h */

