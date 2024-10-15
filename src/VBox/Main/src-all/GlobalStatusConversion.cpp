/* $Id: GlobalStatusConversion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM global definitions - status code conversion.
 *
 * NOTE: This file is part of both VBoxC.dll and VBoxSVC.exe.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "Global.h"

#include <iprt/assert.h>
#include <VBox/err.h>


/*static*/ int
Global::vboxStatusCodeFromCOM(HRESULT aComStatus)
{
    switch (aComStatus)
    {
        case S_OK:                              return VINF_SUCCESS;

        /* Standard COM status codes. See also RTErrConvertFromDarwinCOM */
        case E_UNEXPECTED:                      return VERR_COM_UNEXPECTED;
        case E_NOTIMPL:                         return VERR_NOT_IMPLEMENTED;
        case E_OUTOFMEMORY:                     return VERR_NO_MEMORY;
        case E_INVALIDARG:                      return VERR_INVALID_PARAMETER;
        case E_NOINTERFACE:                     return VERR_NOT_SUPPORTED;
        case E_POINTER:                         return VERR_INVALID_POINTER;
#ifdef E_HANDLE
        case E_HANDLE:                          return VERR_INVALID_HANDLE;
#endif
        case E_ABORT:                           return VERR_CANCELLED;
        case E_FAIL:                            return VERR_GENERAL_FAILURE;
        case E_ACCESSDENIED:                    return VERR_ACCESS_DENIED;

        /* VirtualBox status codes */
        case VBOX_E_OBJECT_NOT_FOUND:           return VERR_COM_OBJECT_NOT_FOUND;
        case VBOX_E_INVALID_VM_STATE:           return VERR_COM_INVALID_VM_STATE;
        case VBOX_E_VM_ERROR:                   return VERR_COM_VM_ERROR;
        case VBOX_E_FILE_ERROR:                 return VERR_COM_FILE_ERROR;
        case VBOX_E_IPRT_ERROR:                 return VERR_COM_IPRT_ERROR;
        case VBOX_E_PDM_ERROR:                  return VERR_COM_PDM_ERROR;
        case VBOX_E_INVALID_OBJECT_STATE:       return VERR_COM_INVALID_OBJECT_STATE;
        case VBOX_E_HOST_ERROR:                 return VERR_COM_HOST_ERROR;
        case VBOX_E_NOT_SUPPORTED:              return VERR_COM_NOT_SUPPORTED;
        case VBOX_E_XML_ERROR:                  return VERR_COM_XML_ERROR;
        case VBOX_E_INVALID_SESSION_STATE:      return VERR_COM_INVALID_SESSION_STATE;
        case VBOX_E_OBJECT_IN_USE:              return VERR_COM_OBJECT_IN_USE;

        default:
            if (SUCCEEDED(aComStatus))
                return VINF_SUCCESS;
            /** @todo Check for the win32 facility and use the
             *        RTErrConvertFromWin32 function on windows. */
            return VERR_UNRESOLVED_ERROR;
    }
}


/*static*/ HRESULT
Global::vboxStatusCodeToCOM(int aVBoxStatus)
{
    switch (aVBoxStatus)
    {
        case VINF_SUCCESS:                      return S_OK;

        /* Standard COM status codes. */
        case VERR_COM_UNEXPECTED:               return E_UNEXPECTED;
        case VERR_NOT_IMPLEMENTED:              return E_NOTIMPL;
        case VERR_NO_MEMORY:                    return E_OUTOFMEMORY;
        case VERR_INVALID_PARAMETER:            return E_INVALIDARG;
        case VERR_NOT_SUPPORTED:                return E_NOINTERFACE;
        case VERR_INVALID_POINTER:              return E_POINTER;
#ifdef E_HANDLE
        case VERR_INVALID_HANDLE:               return E_HANDLE;
#endif
        case VERR_CANCELLED:                    return E_ABORT;
        case VERR_GENERAL_FAILURE:              return E_FAIL;
        case VERR_ACCESS_DENIED:                return E_ACCESSDENIED;

        /* VirtualBox COM status codes. */
        case VERR_COM_OBJECT_NOT_FOUND:         return VBOX_E_OBJECT_NOT_FOUND;
        case VERR_COM_INVALID_VM_STATE:         return VBOX_E_INVALID_VM_STATE;
        case VERR_COM_VM_ERROR:                 return VBOX_E_VM_ERROR;
        case VERR_COM_FILE_ERROR:               return VBOX_E_FILE_ERROR;
        case VERR_COM_IPRT_ERROR:               return VBOX_E_IPRT_ERROR;
        case VERR_COM_PDM_ERROR:                return VBOX_E_PDM_ERROR;
        case VERR_COM_INVALID_OBJECT_STATE:     return VBOX_E_INVALID_OBJECT_STATE;
        case VERR_COM_HOST_ERROR:               return VBOX_E_HOST_ERROR;
        case VERR_COM_NOT_SUPPORTED:            return VBOX_E_NOT_SUPPORTED;
        case VERR_COM_XML_ERROR:                return VBOX_E_XML_ERROR;
        case VERR_COM_INVALID_SESSION_STATE:    return VBOX_E_INVALID_SESSION_STATE;
        case VERR_COM_OBJECT_IN_USE:            return VBOX_E_OBJECT_IN_USE;

        /* Other errors. */
        case VERR_UNRESOLVED_ERROR:             return E_FAIL;
        case VERR_NOT_EQUAL:                    return VBOX_E_FILE_ERROR;
        case VERR_FILE_NOT_FOUND:               return VBOX_E_OBJECT_NOT_FOUND;
        case VERR_IO_NOT_READY:                 return VBOX_E_INVALID_OBJECT_STATE;

        /* Guest Control errors. */
        case VERR_GSTCTL_MAX_CID_OBJECTS_REACHED: return VBOX_E_MAXIMUM_REACHED;
        case VERR_GSTCTL_GUEST_ERROR:             return VBOX_E_GSTCTL_GUEST_ERROR;

        default:
            if (RT_SUCCESS(aVBoxStatus))
                return S_OK;

            /* try categorize it */
            if (   aVBoxStatus < 0
                && (   aVBoxStatus > -1000
                    || (aVBoxStatus < -22000 && aVBoxStatus > -32766) )
               )
                return VBOX_E_IPRT_ERROR;
            if (    aVBoxStatus <  VERR_PDM_NO_SUCH_LUN / 100 * 10
                &&  aVBoxStatus >  VERR_PDM_NO_SUCH_LUN / 100 * 10 - 100)
                return VBOX_E_PDM_ERROR;
            if (    aVBoxStatus <= -1000
                &&  aVBoxStatus >  -5000 /* wrong, but so what... */)
                return VBOX_E_VM_ERROR;

            AssertMsgFailed(("%Rrc\n", aVBoxStatus));
            return E_FAIL;
    }
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
