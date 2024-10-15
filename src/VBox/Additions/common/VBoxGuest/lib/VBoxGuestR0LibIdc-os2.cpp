/* $Id: VBoxGuestR0LibIdc-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestLib - Ring-0 Support Library for VBoxGuest, IDC, OS/2 specific.
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
#include "VBoxGuestR0LibInternal.h"
#include <VBox/err.h>
#include <VBox/log.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN /* for watcom */
/* This is defined in some assembly file.  The AttachDD operation
   is done in the driver init code. */
extern VBGLOS2ATTACHDD g_VBoxGuestIDC;
RT_C_DECLS_END



int VBOXCALL vbglR0IdcNativeOpen(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCCONNECT pReq)
{
    /*
     * Just check whether the connection was made or not.
     */
    if (   g_VBoxGuestIDC.u32Version == VBGL_IOC_VERSION
        && RT_VALID_PTR(g_VBoxGuestIDC.u32Session)
        && RT_VALID_PTR(g_VBoxGuestIDC.pfnServiceEP))
        return g_VBoxGuestIDC.pfnServiceEP(g_VBoxGuestIDC.u32Session, VBGL_IOCTL_IDC_CONNECT, &pReq->Hdr, sizeof(*pReq));
    Log(("vbglDriverOpen: failed\n"));
    RT_NOREF(pHandle);
    return VERR_FILE_NOT_FOUND;
}


int VBOXCALL vbglR0IdcNativeClose(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCDISCONNECT pReq)
{
    return g_VBoxGuestIDC.pfnServiceEP((uintptr_t)pHandle->s.pvSession, VBGL_IOCTL_IDC_DISCONNECT, &pReq->Hdr, sizeof(*pReq));
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
    return g_VBoxGuestIDC.pfnServiceEP((uintptr_t)pHandle->s.pvSession, uReq, pReqHdr, cbReq);
}

