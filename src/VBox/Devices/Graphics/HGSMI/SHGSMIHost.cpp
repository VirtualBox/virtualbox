/* $Id: SHGSMIHost.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Missing description.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#include "SHGSMIHost.h"
#include <VBoxVideo.h>

/*
 * VBOXSHGSMI made on top HGSMI and allows receiving notifications
 * about G->H command completion
 */

static int vboxSHGSMICommandCompleteAsynch (PHGSMIINSTANCE pIns, VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *pHdr)
{
    bool fDoIrq = !!(pHdr->fFlags & VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ)
                || !!(pHdr->fFlags & VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ_FORCE);
    return HGSMICompleteGuestCommand(pIns, pHdr, fDoIrq);
}

void VBoxSHGSMICommandMarkAsynchCompletion(void RT_UNTRUSTED_VOLATILE_GUEST *pvData)
{
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *pHdr = VBoxSHGSMIBufferHeader(pvData);
    Assert(!(pHdr->fFlags & VBOXSHGSMI_FLAG_HG_ASYNCH));
    pHdr->fFlags |= VBOXSHGSMI_FLAG_HG_ASYNCH;
}

int VBoxSHGSMICommandComplete(PHGSMIINSTANCE pIns, void RT_UNTRUSTED_VOLATILE_GUEST *pvData)
{
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *pHdr = VBoxSHGSMIBufferHeader(pvData);
    uint32_t fFlags = pHdr->fFlags;
    ASMCompilerBarrier();
    if (   !(fFlags & VBOXSHGSMI_FLAG_HG_ASYNCH)        /* <- check if synchronous completion */
        && !(fFlags & VBOXSHGSMI_FLAG_GH_ASYNCH_FORCE)) /* <- check if can complete synchronously */
        return VINF_SUCCESS;

    pHdr->fFlags = fFlags | VBOXSHGSMI_FLAG_HG_ASYNCH;
    return vboxSHGSMICommandCompleteAsynch(pIns, pHdr);
}
