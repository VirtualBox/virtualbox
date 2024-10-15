/* $Id: VBoxNetAdp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdp - Virtual Network Adapter Driver (Host), Common Code.
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

/** @page pg_netadp     VBoxNetAdp - Network Adapter
 *
 * This is a kernel module that creates a virtual interface that can be attached
 * to an internal network.
 *
 * In the big picture we're one of the three trunk interface on the internal
 * network, the one named "TAP Interface": @image html Networking_Overview.gif
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include "VBoxNetAdpInternal.h"

#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/string.h>


VBOXNETADP g_aAdapters[VBOXNETADP_MAX_INSTANCES];
static uint8_t g_aUnits[VBOXNETADP_MAX_UNITS/8];


DECLINLINE(int) vboxNetAdpGetUnitByName(const char *pcszName)
{
    uint32_t iUnit = RTStrToUInt32(pcszName + sizeof(VBOXNETADP_NAME) - 1);
    bool fOld;

    if (iUnit >= VBOXNETADP_MAX_UNITS)
        return -1;

    fOld = ASMAtomicBitTestAndSet(g_aUnits, iUnit);
    return fOld ? -1 : (int)iUnit;
}

DECLINLINE(int) vboxNetAdpGetNextAvailableUnit(void)
{
    bool fOld;
    int iUnit;
    /* There is absolutely no chance that all units are taken */
    do {
        iUnit = ASMBitFirstClear(g_aUnits, VBOXNETADP_MAX_UNITS);
        if (iUnit < 0)
            break;
        fOld = ASMAtomicBitTestAndSet(g_aUnits, iUnit);
    } while (fOld);

    return iUnit;
}

DECLINLINE(void) vboxNetAdpReleaseUnit(int iUnit)
{
    bool fSet = ASMAtomicBitTestAndClear(g_aUnits, iUnit);
    NOREF(fSet);
    Assert(fSet);
}

/**
 * Generate a suitable MAC address.
 *
 * @param   pThis       The instance.
 * @param   pMac        Where to return the MAC address.
 */
DECLHIDDEN(void) vboxNetAdpComposeMACAddress(PVBOXNETADP pThis, PRTMAC pMac)
{
    /* Use a locally administered version of the OUI we use for the guest NICs. */
    pMac->au8[0] = 0x08 | 2;
    pMac->au8[1] = 0x00;
    pMac->au8[2] = 0x27;

    pMac->au8[3] = 0; /* pThis->iUnit >> 16; */
    pMac->au8[4] = 0; /* pThis->iUnit >> 8; */
    pMac->au8[5] = pThis->iUnit;
}

int vboxNetAdpCreate(PVBOXNETADP *ppNew, const char *pcszName)
{
    int rc;
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        PVBOXNETADP pThis = &g_aAdapters[i];

        if (ASMAtomicCmpXchgU32((uint32_t volatile *)&pThis->enmState, kVBoxNetAdpState_Transitional, kVBoxNetAdpState_Invalid))
        {
            RTMAC Mac;
            /* Found an empty slot -- use it. */
            Log(("vboxNetAdpCreate: found empty slot: %d\n", i));
            if (pcszName)
            {
                Log(("vboxNetAdpCreate: using name: %s\n", pcszName));
                pThis->iUnit = vboxNetAdpGetUnitByName(pcszName);
                strncpy(pThis->szName, pcszName, sizeof(pThis->szName) - 1);
                pThis->szName[sizeof(pThis->szName) - 1] = '\0';
            }
            else
            {
                pThis->iUnit = vboxNetAdpGetNextAvailableUnit();
                pThis->szName[0] = '\0';
            }
            if (pThis->iUnit < 0)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                vboxNetAdpComposeMACAddress(pThis, &Mac);
                rc = vboxNetAdpOsCreate(pThis, &Mac);
                Log(("vboxNetAdpCreate: pThis=%p pThis->iUnit=%d, pThis->szName=%s\n",
                     pThis, pThis->iUnit, pThis->szName));
            }
            if (RT_SUCCESS(rc))
            {
                *ppNew = pThis;
                ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kVBoxNetAdpState_Active);
                Log2(("VBoxNetAdpCreate: Created %s\n", g_aAdapters[i].szName));
            }
            else
            {
                ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kVBoxNetAdpState_Invalid);
                Log(("vboxNetAdpCreate: vboxNetAdpOsCreate failed with '%Rrc'.\n", rc));
            }
            for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
                Log2(("VBoxNetAdpCreate: Scanning entry: state=%d unit=%d name=%s\n",
                      g_aAdapters[i].enmState, g_aAdapters[i].iUnit, g_aAdapters[i].szName));
            return rc;
        }
    }
    Log(("vboxNetAdpCreate: no empty slots!\n"));

    /* All slots in adapter array are busy. */
    return VERR_OUT_OF_RESOURCES;
}

int vboxNetAdpDestroy(PVBOXNETADP pThis)
{
    int rc = VINF_SUCCESS;

    if (!ASMAtomicCmpXchgU32((uint32_t volatile *)&pThis->enmState, kVBoxNetAdpState_Transitional, kVBoxNetAdpState_Active))
        return VERR_INTNET_FLT_IF_BUSY;

    Assert(pThis->iUnit >= 0 && pThis->iUnit < VBOXNETADP_MAX_UNITS);
    vboxNetAdpOsDestroy(pThis);
    vboxNetAdpReleaseUnit(pThis->iUnit);
    pThis->iUnit = -1;
    pThis->szName[0] = '\0';

    ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kVBoxNetAdpState_Invalid);

    return rc;
}

int  vboxNetAdpInit(void)
{
    unsigned i;
    /*
     * Init common members and call OS-specific init.
     */
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aAdapters, 0, sizeof(g_aAdapters));
    LogFlow(("vboxnetadp: max host-only interfaces supported: %d (%d bytes)\n",
             VBOXNETADP_MAX_INSTANCES, sizeof(g_aAdapters)));
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        g_aAdapters[i].enmState = kVBoxNetAdpState_Invalid;
        g_aAdapters[i].iUnit    = -1;
        vboxNetAdpOsInit(&g_aAdapters[i]);
    }

    return VINF_SUCCESS;
}

/**
 * Finds an adapter by its name.
 *
 * @returns Pointer to the instance by the given name. NULL if not found.
 * @param   pszName         The name of the instance.
 */
PVBOXNETADP vboxNetAdpFindByName(const char *pszName)
{
    unsigned i;

    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        PVBOXNETADP pThis = &g_aAdapters[i];
        Log2(("VBoxNetAdp: Scanning entry: state=%d name=%s\n", pThis->enmState, pThis->szName));
        if (   strcmp(pThis->szName, pszName) == 0
            && ASMAtomicReadU32((uint32_t volatile *)&pThis->enmState) == kVBoxNetAdpState_Active)
            return pThis;
    }
    return NULL;
}

void vboxNetAdpShutdown(void)
{
    unsigned i;

    /* Remove virtual adapters */
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
        vboxNetAdpDestroy(&g_aAdapters[i]);
}
