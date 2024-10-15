/* $Id: VBoxSCSI.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices - Simple SCSI interface for BIOS access.
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

/** @page pg_drv_scsi   Simple SCSI interface for BIOS access.
 *
 * This is a simple interface to access SCSI devices from the BIOS which is
 * shared between the BusLogic and the LsiLogic SCSI host adapters to simplify
 * the BIOS part.
 *
 * The first interface (if available) will be starting at port 0x430 and
 * each will occupy 4 ports. The ports are used as described below:
 *
 * +--------+--------+----------+
 * | Offset | Access | Purpose  |
 * +--------+--------+----------+
 * |   0    |  Write | Command  |
 * +--------+--------+----------+
 * |   0    |  Read  | Status   |
 * +--------+--------+----------+
 * |   1    |  Write | Data in  |
 * +--------+--------+----------+
 * |   1    |  Read  | Data out |
 * +--------+--------+----------+
 * |   2    |  R/W   | Detect   |
 * +--------+--------+----------+
 * |   3    |  Read  | SCSI rc  |
 * +--------+--------+----------+
 * |   3    |  Write | Reset    |
 * +--------+--------+----------+
 *
 * The register at port 0 receives the SCSI CDB issued from the driver when
 * writing to it but before writing the actual CDB the first write gives the
 * size of the CDB in bytes.
 *
 * Reading the port at offset 0 gives status information about the adapter. If
 * the busy bit is set the adapter is processing a previous issued request if it is
 * cleared the command finished and the adapter can process another request.
 * The driver has to poll this bit because the adapter will not assert an IRQ
 * for simplicity reasons.
 *
 * The register at offset 2 is to detect if a host adapter is available. If the
 * driver writes a value to this port and gets the same value after reading it
 * again the adapter is available.
 *
 * Any write to the register at offset 3 causes the interface to be reset. A
 * read returns the SCSI status code of the last operation.
 *
 * This part has no R0 or RC components.
 */

#ifndef VBOX_INCLUDED_SRC_Storage_VBoxSCSI_h
#define VBOX_INCLUDED_SRC_Storage_VBoxSCSI_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/vmm/pdmdev.h>
#include <VBox/version.h>

#ifdef IN_RING3
RT_C_DECLS_BEGIN

/**
 * Helper shared by the LsiLogic and BusLogic device emulations to load legacy saved states
 * before the removal of the VBoxSCSI interface.
 *
 * @returns VBox status code.
 * @param   pHlp                Pointer to the Ring-3 device helper table.
 * @param   pSSM                The SSM handle to operate on.
 */
DECLINLINE(int) vboxscsiR3LoadExecLegacy(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMSkip(pSSM, 4);

    /*
     * The CDB buffer was increased with r104155 in trunk (backported to 5.0
     * in r104311) without bumping the SSM state versions which leaves us
     * with broken saved state restoring for older VirtualBox releases
     * (up to 5.0.10).
     */
    if (   (   pHlp->pfnSSMHandleRevision(pSSM) < 104311
            && pHlp->pfnSSMHandleVersion(pSSM)  < VBOX_FULL_VERSION_MAKE(5, 0, 12))
        || (   pHlp->pfnSSMHandleRevision(pSSM) < 104155
            && pHlp->pfnSSMHandleVersion(pSSM)  >= VBOX_FULL_VERSION_MAKE(5, 0, 51)))
        pHlp->pfnSSMSkip(pSSM, 12);
    else
        pHlp->pfnSSMSkip(pSSM, 20);

    pHlp->pfnSSMSkip(pSSM, 1); /*iCDB*/
    uint32_t cbBufLeft, iBuf;
    pHlp->pfnSSMGetU32(pSSM, &cbBufLeft);
    pHlp->pfnSSMGetU32(pSSM, &iBuf);
    pHlp->pfnSSMSkip(pSSM, 2); /*fBusy, enmState*/

    if (cbBufLeft + iBuf)
        pHlp->pfnSSMSkip(pSSM, cbBufLeft + iBuf);

    return VINF_SUCCESS;
}


RT_C_DECLS_END
#endif /* IN_RING3 */

#endif /* !VBOX_INCLUDED_SRC_Storage_VBoxSCSI_h */

