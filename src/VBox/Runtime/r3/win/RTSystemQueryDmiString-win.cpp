/* $Id: RTSystemQueryDmiString-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryDmiString, windows ring-3.
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
#define _WIN32_DCOM
#include <iprt/win/windows.h>
#include <WbemCli.h>

#include <iprt/system.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/utf16.h>


/**
 * Initialize COM.
 *
 * @returns COM status code.
 */
static HRESULT rtSystemDmiWinInitialize(void)
{
    HRESULT hrc = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrc))
    {
        hrc = CoInitializeSecurity(NULL,
                                   -1,                          /* COM authentication. */
                                   NULL,                        /* Which authentication services. */
                                   NULL,                        /* Reserved. */
                                   RPC_C_AUTHN_LEVEL_DEFAULT,   /* Default authentication. */
                                   RPC_C_IMP_LEVEL_IMPERSONATE, /* Default impersonation. */
                                   NULL,                        /* Authentication info. */
                                   EOAC_NONE,                   /* Additional capabilities. */
                                   NULL);                       /* Reserved. */
        if (hrc == RPC_E_TOO_LATE)
            hrc = S_OK;
        else if (FAILED(hrc))
            CoUninitialize();
    }
    return hrc;
}


/**
 * Undo what rtSystemDmiWinInitialize did.
 */
static void rtSystemDmiWinTerminate(void)
{
    CoUninitialize();
}


/**
 * Convert a UTF-8 string to a BSTR.
 *
 * @returns BSTR pointer.
 * @param   psz                 The UTF-8 string.
 */
static BSTR rtSystemWinBstrFromUtf8(const char *psz)
{
    PRTUTF16 pwsz = NULL;
    int rc = RTStrToUtf16(psz, &pwsz);
    if (RT_FAILURE(rc))
        return NULL;
    BSTR pBStr = SysAllocString((const OLECHAR *)pwsz);
    RTUtf16Free(pwsz);
    return pBStr;
}


/**
 * Connect to the DMI server.
 *
 * @returns COM status code.
 * @param   pLocator            The locator.
 * @param   pszServer           The server name.
 * @param   ppServices          Where to return the services interface.
 */
static HRESULT rtSystemDmiWinConnectToServer(IWbemLocator *pLocator, const char *pszServer, IWbemServices **ppServices)
{
    AssertPtr(pLocator);
    AssertPtrNull(pszServer);
    AssertPtr(ppServices);

    BSTR pBStrServer = rtSystemWinBstrFromUtf8(pszServer);
    if (!pBStrServer)
        return E_OUTOFMEMORY;

    HRESULT hrc = pLocator->ConnectServer(pBStrServer,
                                          NULL,
                                          NULL,
                                          0,
                                          NULL,
                                          0,
                                          0,
                                          ppServices);
    if (SUCCEEDED(hrc))
    {
        hrc = CoSetProxyBlanket(*ppServices,
                                RPC_C_AUTHN_WINNT,
                                RPC_C_AUTHZ_NONE,
                                NULL,
                                RPC_C_AUTHN_LEVEL_CALL,
                                RPC_C_IMP_LEVEL_IMPERSONATE,
                                NULL,
                                EOAC_NONE);
        if (FAILED(hrc))
            (*ppServices)->Release();
    }
    SysFreeString(pBStrServer);
    return hrc;
}


RTDECL(int) RTSystemQueryDmiString(RTSYSDMISTR enmString, char *pszBuf, size_t cbBuf)
{
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);
    *pszBuf = '\0';
    AssertReturn(enmString > RTSYSDMISTR_INVALID && enmString < RTSYSDMISTR_END, VERR_INVALID_PARAMETER);

    /*
     * Figure the property name before we start.
     */
    const char *pszPropName;
    switch (enmString)
    {
        case RTSYSDMISTR_PRODUCT_NAME:      pszPropName = "Name"; break;
        case RTSYSDMISTR_PRODUCT_VERSION:   pszPropName = "Version"; break;
        case RTSYSDMISTR_PRODUCT_UUID:      pszPropName = "UUID"; break;
        case RTSYSDMISTR_PRODUCT_SERIAL:    pszPropName = "IdentifyingNumber"; break;
        case RTSYSDMISTR_MANUFACTURER:      pszPropName = "Vendor"; break;

        default:
            return VERR_NOT_SUPPORTED;
    }

    /*
     * Before we do anything with COM, we have to initialize it.
     */
    bool fUninit = true;
    HRESULT hrc = rtSystemDmiWinInitialize();
    if (hrc == RPC_E_CHANGED_MODE)
        fUninit = false;  /* don't fail if already initialized */
    else if (FAILED(hrc))
        return VERR_NOT_SUPPORTED;

    int rc = VERR_NOT_SUPPORTED;
    BSTR pBstrPropName = rtSystemWinBstrFromUtf8(pszPropName);
    if (pBstrPropName)
    {
        /*
         * Instantiate the IWbemLocator, whatever that is and connect to the
         * DMI serve.
         */
        IWbemLocator *pLoc;
        hrc = CoCreateInstance(CLSID_WbemLocator,
                               0,
                               CLSCTX_INPROC_SERVER,
                               IID_IWbemLocator,
                               (LPVOID *)&pLoc);
        if (SUCCEEDED(hrc))
        {
            IWbemServices *pServices;
            hrc = rtSystemDmiWinConnectToServer(pLoc, "ROOT\\CIMV2", &pServices);
            if (SUCCEEDED(hrc))
            {
                /*
                 * Enumerate whatever it is we're looking at and try get
                 * the desired property.
                 */
                BSTR pBstrFilter = rtSystemWinBstrFromUtf8("Win32_ComputerSystemProduct");
                if (pBstrFilter)
                {
                    IEnumWbemClassObject *pEnum;
                    hrc = pServices->CreateInstanceEnum(pBstrFilter, 0, NULL, &pEnum);
                    if (SUCCEEDED(hrc))
                    {
                        do
                        {
                            IWbemClassObject *pObj;
                            ULONG cObjRet;
                            hrc = pEnum->Next(WBEM_INFINITE, 1, &pObj, &cObjRet);
                            if (   SUCCEEDED(hrc)
                                && cObjRet >= 1)
                            {
                                VARIANT Var;
                                VariantInit(&Var);
                                hrc = pObj->Get(pBstrPropName, 0, &Var, 0, 0);
                                if (   SUCCEEDED(hrc)
                                    && V_VT(&Var) == VT_BSTR)
                                {
                                    /*
                                     * Convert the BSTR to UTF-8 and copy it
                                     * into the return buffer.
                                     */
                                    char *pszValue;
                                    rc = RTUtf16ToUtf8(Var.bstrVal, &pszValue);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = RTStrCopy(pszBuf, cbBuf, pszValue);
                                        RTStrFree(pszValue);
                                        hrc = WBEM_S_FALSE;
                                    }
                                }
                                VariantClear(&Var);
                                pObj->Release();
                            }
                        } while (hrc != WBEM_S_FALSE);

                        pEnum->Release();
                    }
                    SysFreeString(pBstrFilter);
                }
                else
                    hrc = E_OUTOFMEMORY;
                pServices->Release();
            }
            pLoc->Release();
        }
        SysFreeString(pBstrPropName);
    }
    else
        hrc = E_OUTOFMEMORY;
    if (fUninit)
        rtSystemDmiWinTerminate();
    if (FAILED(hrc) && rc == VERR_NOT_SUPPORTED)
        rc = VERR_NOT_SUPPORTED;
    return rc;
}

