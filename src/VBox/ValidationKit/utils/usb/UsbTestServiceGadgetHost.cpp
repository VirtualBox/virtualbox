/* $Id: UsbTestServiceGadgetHost.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, USB gadget host API.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "UsbTestServiceGadget.h"
#include "UsbTestServiceGadgetHostInternal.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * Internal UTS gadget host instance data.
 */
typedef struct UTSGADGETHOSTINT
{
    /** Reference counter. */
    volatile uint32_t         cRefs;
    /** Pointer to the gadget host callback table. */
    PCUTSGADGETHOSTIF         pHstIf;
    /** Interface specific instance data - variable in size. */
    uint8_t                   abIfInst[1];
} UTSGADGETHOSTINT;
/** Pointer to the internal gadget host instance data. */
typedef UTSGADGETHOSTINT *PUTSGADGETHOSTINT;


/*********************************************************************************************************************************
*   Global variables                                                                                                             *
*********************************************************************************************************************************/

/** Known gadget host interfaces. */
static const PCUTSGADGETHOSTIF g_apUtsGadgetHostIf[] =
{
    &g_UtsGadgetHostIfUsbIp,
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Destroys a gadget host instance.
 *
 * @param   pThis    The gadget host instance.
 */
static void utsGadgetHostDestroy(PUTSGADGETHOSTINT pThis)
{
    /** @todo Remove all gadgets. */
    pThis->pHstIf->pfnTerm((PUTSGADGETHOSTTYPEINT)&pThis->abIfInst[0]);
    RTMemFree(pThis);
}


DECLHIDDEN(int) utsGadgetHostCreate(UTSGADGETHOSTTYPE enmType, PCUTSGADGETCFGITEM paCfg,
                                    PUTSGADGETHOST phGadgetHost)
{
    int rc = VINF_SUCCESS;
    PCUTSGADGETHOSTIF pIf = NULL;

    /* Get the interface. */
    for (unsigned i = 0; i < RT_ELEMENTS(g_apUtsGadgetHostIf); i++)
    {
        if (g_apUtsGadgetHostIf[i]->enmType == enmType)
        {
            pIf = g_apUtsGadgetHostIf[i];
            break;
        }
    }

    if (RT_LIKELY(pIf))
    {
        PUTSGADGETHOSTINT pThis = (PUTSGADGETHOSTINT)RTMemAllocZ(RT_UOFFSETOF_DYN(UTSGADGETHOSTINT, abIfInst[pIf->cbIf]));
        if (RT_LIKELY(pThis))
        {
            pThis->cRefs = 1;
            pThis->pHstIf = pIf;
            rc = pIf->pfnInit((PUTSGADGETHOSTTYPEINT)&pThis->abIfInst[0], paCfg);
            if (RT_SUCCESS(rc))
                *phGadgetHost = pThis;
            else
                RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}


DECLHIDDEN(uint32_t) utsGadgetHostRetain(UTSGADGETHOST hGadgetHost)
{
    PUTSGADGETHOSTINT pThis = hGadgetHost;

    AssertPtrReturn(pThis, 0);

    return ASMAtomicIncU32(&pThis->cRefs);
}


DECLHIDDEN(uint32_t) utsGadgetHostRelease(UTSGADGETHOST hGadgetHost)
{
    PUTSGADGETHOSTINT pThis = hGadgetHost;

    AssertPtrReturn(pThis, 0);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (!cRefs)
        utsGadgetHostDestroy(pThis);

    return cRefs;
}


DECLHIDDEN(int) utsGadgetHostGadgetConnect(UTSGADGETHOST hGadgetHost, UTSGADGET hGadget)
{
    PUTSGADGETHOSTINT pThis = hGadgetHost;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    return pThis->pHstIf->pfnGadgetConnect((PUTSGADGETHOSTTYPEINT)&pThis->abIfInst[0], hGadget);
}


DECLHIDDEN(int) utsGadgetHostGadgetDisconnect(UTSGADGETHOST hGadgetHost, UTSGADGET hGadget)
{
    PUTSGADGETHOSTINT pThis = hGadgetHost;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    return pThis->pHstIf->pfnGadgetDisconnect((PUTSGADGETHOSTTYPEINT)&pThis->abIfInst[0], hGadget);
}

