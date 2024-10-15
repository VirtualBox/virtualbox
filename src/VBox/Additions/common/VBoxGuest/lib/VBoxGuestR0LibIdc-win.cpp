/* $Id: VBoxGuestR0LibIdc-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestLib - Ring-0 Support Library for VBoxGuest, IDC, Windows specific.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/nt/nt.h>
#include "VBoxGuestR0LibInternal.h"
#include <VBox/VBoxGuest.h>
#include <iprt/errcore.h>
#include <VBox/log.h>


/**
 * Internal I/O Control call worker.
 *
 * @returns VBox status code.
 * @param   pDeviceObject   The device object to call.
 * @param   pFileObject     The file object for the connection.
 * @param   uReq            The request.
 * @param   pReq            The request packet.
 */
static int vbglR0IdcNtCallInternal(PDEVICE_OBJECT pDeviceObject, PFILE_OBJECT pFileObject, uint32_t uReq, PVBGLREQHDR pReq)
{
    int      rc;
    NTSTATUS rcNt;

    /*
     * Build the request.
     *
     * We want to avoid double buffering of the request, therefore we don't
     * specify any request pointers or sizes when asking the kernel to build
     * the IRP for us, but instead do that part our selves.
     *
     * See https://www.osr.com/blog/2018/02/14/beware-iobuilddeviceiocontrolrequest/
     * for how fun this is when we're not at IRQL PASSIVE (HACK ALERT futher down).
     * Ran into this little issue when LoadLibraryEx on a .NET DLL using the
     * LOAD_LIBRARY_AS_DATAFILE and LOAD_LIBRARY_AS_IMAGE_RESOURCE flags.
     */
    KEVENT Event;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IO_STATUS_BLOCK IoStatusBlock = RTNT_IO_STATUS_BLOCK_INITIALIZER;
#if 0
    PIRP pIrp = IoBuildDeviceIoControlRequest(uReq,              /* IoControlCode */
                                              pDeviceObject,
                                              pReq,              /* InputBuffer */
                                              pReq->cbIn,        /* InputBufferLength */
                                              pReq,              /* OutputBuffer */
                                              pReq->cbOut,       /* OutputBufferLength */
                                              TRUE,              /* InternalDeviceIoControl (=> IRP_MJ_INTERNAL_DEVICE_CONTROL) */
                                              &Event,            /* Event */
                                              &IoStatusBlock);   /* IoStatusBlock */
#else
    PIRP pIrp = IoBuildDeviceIoControlRequest(uReq,              /* IoControlCode */
                                              pDeviceObject,
                                              NULL,              /* InputBuffer */
                                              0,                 /* InputBufferLength */
                                              NULL,              /* OutputBuffer */
                                              0,                 /* OutputBufferLength */
                                              TRUE,              /* InternalDeviceIoControl (=> IRP_MJ_INTERNAL_DEVICE_CONTROL) */
                                              &Event,            /* Event */
                                              &IoStatusBlock);   /* IoStatusBlock */
#endif
    if (pIrp)
    {
#if 0
        IoGetNextIrpStackLocation(pIrp)->FileObject = pFileObject;
#else
        //w2k3sp1+: if (!KeAreAllApcsDisabled())
        //w2k3sp1+:     pIrp->Flags                                      |= IRP_SYNCHRONOUS_API;
        //w2k3sp1+: else
        {
            /* HACK ALERT! Causes IoCompleteRequest to update UserIosb and free the IRP w/o any APC happening. */
            pIrp->Flags                                      |= IRP_SYNCHRONOUS_API | IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO;
            KIRQL bIrqlSaved;
            KeRaiseIrql(APC_LEVEL, &bIrqlSaved);
            RemoveEntryList(&pIrp->ThreadListEntry);
            InitializeListHead(&pIrp->ThreadListEntry);
            KeLowerIrql(bIrqlSaved);
        }
        pIrp->UserBuffer                                      = pReq;
        pIrp->AssociatedIrp.SystemBuffer                      = pReq;
        PIO_STACK_LOCATION pStack = IoGetNextIrpStackLocation(pIrp);
        pStack->FileObject                                    = pFileObject;
        pStack->Parameters.DeviceIoControl.OutputBufferLength = pReq->cbOut;
        pStack->Parameters.DeviceIoControl.InputBufferLength  = pReq->cbIn;
#endif

        /*
         * Call the driver, wait for an async request to complete (should never happen).
         */
        rcNt = IoCallDriver(pDeviceObject, pIrp);
        if (rcNt == STATUS_PENDING)
            rcNt = KeWaitForSingleObject(&Event,            /* Object */
                                         Executive,         /* WaitReason */
                                         KernelMode,        /* WaitMode */
                                         FALSE,             /* Alertable */
                                         NULL);             /* TimeOut */
        if (NT_SUCCESS(rcNt))
            rcNt = IoStatusBlock.Status;
        if (NT_SUCCESS(rcNt))
            rc = pReq->rc;
        else
            rc = RTErrConvertFromNtStatus(rcNt);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


int VBOXCALL vbglR0IdcNativeOpen(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCCONNECT pReq)
{
    PDEVICE_OBJECT  pDeviceObject = NULL;
    PFILE_OBJECT    pFileObject = NULL;
    UNICODE_STRING  wszDeviceName;
    NTSTATUS        rcNt;
    int             rc;

    /*
     * Get the device object pointer.
     */
    RtlInitUnicodeString(&wszDeviceName, VBOXGUEST_DEVICE_NAME_NT);
    rcNt = IoGetDeviceObjectPointer(&wszDeviceName, FILE_ALL_ACCESS, &pFileObject, &pDeviceObject);
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Make the connection call.
         */
        rc = vbglR0IdcNtCallInternal(pDeviceObject, pFileObject, VBGL_IOCTL_IDC_CONNECT, &pReq->Hdr);
        if (RT_SUCCESS(rc) && RT_SUCCESS(pReq->Hdr.rc))
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


int VBOXCALL vbglR0IdcNativeClose(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCDISCONNECT pReq)
{
    PFILE_OBJECT pFileObject = pHandle->s.pFileObject;
    int rc = vbglR0IdcNtCallInternal(pHandle->s.pDeviceObject, pFileObject, VBGL_IOCTL_IDC_DISCONNECT, &pReq->Hdr);
    if (RT_SUCCESS(rc) && RT_SUCCESS(pReq->Hdr.rc))
    {
        pHandle->s.pDeviceObject = NULL;
        pHandle->s.pFileObject   = NULL;
        ObDereferenceObject(pFileObject);
    }

    return rc;
}


/**
 * Makes an IDC call, returning only the I/O control status code.
 *
 * @returns VBox status code (the I/O control failure status).
 * @param   pHandle             The IDC handle.
 * @param   uReq                The request number.
 * @param   pReqHdr             The request header.
 * @param   cbReq               The request size.
 */
DECLR0VBGL(int) VbglR0IdcCallRaw(PVBGLIDCHANDLE pHandle, uintptr_t uReq, PVBGLREQHDR pReqHdr, uint32_t cbReq)
{
    NOREF(cbReq);
    return vbglR0IdcNtCallInternal(pHandle->s.pDeviceObject, pHandle->s.pFileObject, (uint32_t)uReq, pReqHdr);
}

