/* $Id: com.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN
#if !defined(VBOX_WITH_XPCOM)

# include <iprt/win/objbase.h>

#else /* !defined (VBOX_WITH_XPCOM) */
# include <stdlib.h>
# include <nsCOMPtr.h>
# include <nsIServiceManagerUtils.h>
# include <nsIComponentManager.h>
# include <ipcIService.h>
# include <ipcCID.h>
# include <ipcIDConnectService.h>
# include <nsIInterfaceInfo.h>
# include <nsIInterfaceInfoManager.h>
# define IPC_DCONNECTSERVICE_CONTRACTID "@mozilla.org/ipc/dconnect-service;1" // official XPCOM headers don't define it yet
#endif /* !defined (VBOX_WITH_XPCOM) */

#include "VBox/com/com.h"
#include "VBox/com/assert.h"

#include "VBox/com/Guid.h"
#include "VBox/com/array.h"

#include <iprt/string.h>

#include <iprt/errcore.h>
#include <VBox/log.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
namespace com
{
/* static */
const Guid Guid::Empty; /* default ctor is OK */

const char Zeroes[16] = {0, };


void GetInterfaceNameByIID(const GUID &aIID, BSTR *aName)
{
    AssertPtrReturnVoid(aName);
    *aName = NULL;

#if !defined(VBOX_WITH_XPCOM)

    LPOLESTR iidStr = NULL;
    if (StringFromIID(aIID, &iidStr) == S_OK)
    {
        HKEY ifaceKey;
        LSTATUS lrc = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Interface", 0, KEY_QUERY_VALUE, &ifaceKey);
        if (lrc == ERROR_SUCCESS)
        {
            HKEY iidKey;
            lrc = RegOpenKeyExW(ifaceKey, iidStr, 0, KEY_QUERY_VALUE, &iidKey);
            if (lrc == ERROR_SUCCESS)
            {
                /* determine the size and type */
                DWORD sz, type;
                lrc = RegQueryValueExW(iidKey, NULL, NULL, &type, NULL, &sz);
                if (lrc == ERROR_SUCCESS && type == REG_SZ)
                {
                    /* query the value to BSTR */
                    *aName = SysAllocStringLen(NULL, (sz + 1) / sizeof(TCHAR) + 1);
                    lrc = RegQueryValueExW(iidKey, NULL, NULL, NULL, (LPBYTE) *aName, &sz);
                    if (lrc != ERROR_SUCCESS)
                    {
                        SysFreeString(*aName);
                        *aName = NULL;
                    }
                }
                RegCloseKey(iidKey);
            }
            RegCloseKey(ifaceKey);
        }
        CoTaskMemFree(iidStr);
    }

#else /* !defined (VBOX_WITH_XPCOM) */

    nsresult rv;
    nsCOMPtr<nsIInterfaceInfoManager> iim =
        do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv))
    {
        nsCOMPtr<nsIInterfaceInfo> iinfo;
        rv = iim->GetInfoForIID(&aIID, getter_AddRefs(iinfo));
        if (NS_SUCCEEDED(rv))
        {
            const char *iname = NULL;
            iinfo->GetNameShared(&iname);
            char *utf8IName = NULL;
            if (RT_SUCCESS(RTStrCurrentCPToUtf8(&utf8IName, iname)))
            {
                PRTUTF16 utf16IName = NULL;
                if (RT_SUCCESS(RTStrToUtf16(utf8IName, &utf16IName)))
                {
                    *aName = SysAllocString((OLECHAR *) utf16IName);
                    RTUtf16Free(utf16IName);
                }
                RTStrFree(utf8IName);
            }
        }
    }

#endif /* !defined (VBOX_WITH_XPCOM) */
}

#ifdef VBOX_WITH_XPCOM

HRESULT GlueCreateObjectOnServer(const CLSID &clsid,
                                 const char *serverName,
                                 const nsIID &id,
                                 void** ppobj)
{
    HRESULT hrc = E_UNEXPECTED;
    nsCOMPtr<ipcIService> ipcServ = do_GetService(IPC_SERVICE_CONTRACTID, &hrc);
    if (SUCCEEDED(hrc))
    {
        PRUint32 serverID = 0;
        hrc = ipcServ->ResolveClientName(serverName, &serverID);
        if (SUCCEEDED (hrc))
        {
            nsCOMPtr<ipcIDConnectService> dconServ = do_GetService(IPC_DCONNECTSERVICE_CONTRACTID, &hrc);
            if (SUCCEEDED(hrc))
                hrc = dconServ->CreateInstance(serverID,
                                               clsid,
                                               id,
                                               ppobj);
        }
    }
    return hrc;
}

HRESULT GlueCreateInstance(const CLSID &clsid,
                           const nsIID &id,
                           void** ppobj)
{
    nsCOMPtr<nsIComponentManager> manager;
    HRESULT hrc = NS_GetComponentManager(getter_AddRefs(manager));
    if (SUCCEEDED(hrc))
        hrc = manager->CreateInstance(clsid,
                                      nsnull,
                                      id,
                                      ppobj);
    return hrc;
}

#endif // VBOX_WITH_XPCOM

} /* namespace com */

