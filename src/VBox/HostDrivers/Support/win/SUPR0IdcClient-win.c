/* $Id: SUPR0IdcClient-win.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - IDC Client Lib, Windows Specific Code.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "../SUPR0IdcClientInternal.h"
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** NT Device name. */
#define DEVICE_NAME_NT   L"\\Device\\VBoxDrv"


/**
 * Internal I/O Control call worker.
 *
 * @returns VBox status code.
 * @param   pDeviceObject   The device object to call.
 * @param   pFileObject     The file object for the connection.
 * @param   uReq            The request.
 * @param   pReq            The request packet.
 */
static int supR0IdcNtCallInternal(PDEVICE_OBJECT pDeviceObject, PFILE_OBJECT pFileObject, uint32_t uReq, PSUPDRVIDCREQHDR pReq)
{
    int                 rc;
    IO_STATUS_BLOCK     IoStatusBlock;
    KEVENT              Event;
    PIRP                pIrp;
    NTSTATUS            rcNt;

    /*
     * Build the request.
     */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    pIrp = IoBuildDeviceIoControlRequest(uReq,              /* IoControlCode */
                                         pDeviceObject,
                                         pReq,              /* InputBuffer */
                                         pReq->cb,          /* InputBufferLength */
                                         pReq,              /* OutputBuffer */
                                         pReq->cb,          /* OutputBufferLength */
                                         TRUE,              /* InternalDeviceIoControl (=> IRP_MJ_INTERNAL_DEVICE_CONTROL) */
                                         &Event,            /* Event */
                                         &IoStatusBlock);   /* IoStatusBlock */
    if (pIrp)
    {
        IoGetNextIrpStackLocation(pIrp)->FileObject = pFileObject;

        /*
         * Call the driver, wait for an async request to complete (should never happen).
         */
        rcNt = IoCallDriver(pDeviceObject, pIrp);
        if (rcNt == STATUS_PENDING)
        {
            rcNt = KeWaitForSingleObject(&Event,            /* Object */
                                         Executive,         /* WaitReason */
                                         KernelMode,        /* WaitMode */
                                         FALSE,             /* Alertable */
                                         NULL);             /* TimeOut */
            rcNt = IoStatusBlock.Status;
        }
        if (NT_SUCCESS(rcNt))
            rc = pReq->rc;
        else
            rc = RTErrConvertFromNtStatus(rcNt);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


int VBOXCALL supR0IdcNativeOpen(PSUPDRVIDCHANDLE pHandle, PSUPDRVIDCREQCONNECT pReq)
{
    PDEVICE_OBJECT  pDeviceObject = NULL;
    PFILE_OBJECT    pFileObject = NULL;
    UNICODE_STRING  wszDeviceName;
    NTSTATUS        rcNt;
    int             rc;

    /*
     * Get the device object pointer.
     */
    RtlInitUnicodeString(&wszDeviceName, DEVICE_NAME_NT);
    rcNt = IoGetDeviceObjectPointer(&wszDeviceName, FILE_ALL_ACCESS, &pFileObject, &pDeviceObject);
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Make the connection call.
         */
        rc = supR0IdcNtCallInternal(pDeviceObject, pFileObject, SUPDRV_IDC_REQ_CONNECT, &pReq->Hdr);
        if (RT_SUCCESS(rc))
        {
            pHandle->s.pDeviceObject = pDeviceObject;
            pHandle->s.pFileObject   = pFileObject;
            return rc;
        }

        /* only the file object. */
        ObDereferenceObject(pFileObject);
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    pHandle->s.pDeviceObject = NULL;
    pHandle->s.pFileObject = NULL;
    return rc;
}


int VBOXCALL supR0IdcNativeClose(PSUPDRVIDCHANDLE pHandle, PSUPDRVIDCREQHDR pReq)
{
    PFILE_OBJECT pFileObject = pHandle->s.pFileObject;
    int rc = supR0IdcNtCallInternal(pHandle->s.pDeviceObject, pFileObject, SUPDRV_IDC_REQ_DISCONNECT, pReq);
    if (RT_SUCCESS(rc))
    {
        pHandle->s.pDeviceObject = NULL;
        pHandle->s.pFileObject  = NULL;
        ObDereferenceObject(pFileObject);
    }

    return rc;
}


int VBOXCALL supR0IdcNativeCall(PSUPDRVIDCHANDLE pHandle, uint32_t uReq, PSUPDRVIDCREQHDR pReq)
{
    return supR0IdcNtCallInternal(pHandle->s.pDeviceObject, pHandle->s.pFileObject, uReq, pReq);
}

