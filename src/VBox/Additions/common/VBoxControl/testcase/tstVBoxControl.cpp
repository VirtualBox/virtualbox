/* $Id: tstVBoxControl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxControl - Guest Additions Command Line Management Interface, test case
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <iprt/cpp/autores.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include <VBox/VBoxGuestLib.h>
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>
#endif

VBGLR3DECL(int)     VbglR3Init(void)
{
    RTPrintf("Initialising guest library...\n");
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropConnect(HGCMCLIENTID *pidClient)
{
    AssertPtrReturn(pidClient, VERR_INVALID_POINTER);
    RTPrintf("Connect to guest property service...\n");
    *pidClient = 1;
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropDisconnect(HGCMCLIENTID idClient)
{
    RTPrintf("Disconnect client %d from guest property service...\n", idClient);
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropWrite(HGCMCLIENTID idClient,
                                         const char *pszName,
                                         const char *pszValue,
                                         const char *pszFlags)
{
    RTPrintf("Called SET_PROP, client %d, name %s, value %s, flags %s...\n",
             idClient, pszName, pszValue, pszFlags);
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropWriteValue(HGCMCLIENTID idClient,
                                              const char *pszName,
                                              const char *pszValue)
{
    RTPrintf("Called SET_PROP_VALUE, client %d, name %s, value %s...\n",
             idClient, pszName, pszValue);
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_GUEST_PROPS
VBGLR3DECL(int)     VbglR3GuestPropRead(HGCMCLIENTID idClient,
                                        const char *pszName,
                                        void *pvBuf,
                                        uint32_t cbBuf,
                                        char **ppszValue,
                                        uint64_t *pu64Timestamp,
                                        char **ppszFlags,
                                        uint32_t *pcbBufActual)
{
    RT_NOREF2(pvBuf, cbBuf);
    RTPrintf("Called GET_PROP, client %d, name %s...\n",
             idClient, pszName);
    static char szValue[] = "Value";
    static char szFlags[] = "TRANSIENT";
    if (ppszValue)
        *ppszValue = szValue;
    if (pu64Timestamp)
        *pu64Timestamp = 12345;
    if (ppszFlags)
        *ppszFlags = szFlags;
    if (pcbBufActual)
        *pcbBufActual = 256;
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropDelete(HGCMCLIENTID idClient,
                                          const char *pszName)
{
    RTPrintf("Called DEL_PROP, client %d, name %s...\n",
             idClient, pszName);
    return VINF_SUCCESS;
}

struct VBGLR3GUESTPROPENUM
{
    uint32_t u32;
};

VBGLR3DECL(int)     VbglR3GuestPropEnum(HGCMCLIENTID idClient,
                                        char const * const *ppaszPatterns,
                                        uint32_t cPatterns,
                                        PVBGLR3GUESTPROPENUM *ppHandle,
                                        char const **ppszName,
                                        char const **ppszValue,
                                        uint64_t *pu64Timestamp,
                                        char const **ppszFlags)
{
    RT_NOREF2(ppaszPatterns, cPatterns);
    RTPrintf("Called ENUM_PROPS, client %d...\n", idClient);
    AssertPtrReturn(ppHandle, VERR_INVALID_POINTER);
    static VBGLR3GUESTPROPENUM Handle = { 0 };
    static char szName[] = "Name";
    static char szValue[] = "Value";
    static char szFlags[] = "TRANSIENT";
    *ppHandle = &Handle;
    if (ppszName)
        *ppszName = szName;
    if (ppszValue)
        *ppszValue = szValue;
    if (pu64Timestamp)
        *pu64Timestamp = 12345;
    if (ppszFlags)
        *ppszFlags = szFlags;
    return VINF_SUCCESS;
}

VBGLR3DECL(int)     VbglR3GuestPropEnumNext(PVBGLR3GUESTPROPENUM pHandle,
                                            char const **ppszName,
                                            char const **ppszValue,
                                            uint64_t *pu64Timestamp,
                                            char const **ppszFlags)
{
    RT_NOREF1(pHandle);
    RTPrintf("Called enumerate next...\n");
    AssertReturn(RT_VALID_PTR(ppszName) || RT_VALID_PTR(ppszValue) || RT_VALID_PTR(ppszFlags),
                 VERR_INVALID_POINTER);
    if (ppszName)
        *ppszName = NULL;
    if (ppszValue)
        *ppszValue = NULL;
    if (pu64Timestamp)
        *pu64Timestamp = 0;
    if (ppszFlags)
        *ppszFlags = NULL;
    return VINF_SUCCESS;
}

VBGLR3DECL(void)    VbglR3GuestPropEnumFree(PVBGLR3GUESTPROPENUM pHandle)
{
    RT_NOREF1(pHandle);
    RTPrintf("Called enumerate free...\n");
}

VBGLR3DECL(int)     VbglR3GuestPropWait(HGCMCLIENTID idClient,
                                        const char *pszPatterns,
                                        void *pvBuf,
                                        uint32_t cbBuf,
                                        uint64_t u64Timestamp,
                                        uint32_t u32Timeout,
                                        char ** ppszName,
                                        char **ppszValue,
                                        uint64_t *pu64Timestamp,
                                        char **ppszFlags,
                                        uint32_t *pcbBufActual,
                                        bool *pfWasDeleted)
{
    RT_NOREF2(pvBuf, cbBuf);
    if (u32Timeout == RT_INDEFINITE_WAIT)
        RTPrintf("Called GET_NOTIFICATION, client %d, patterns %s, timestamp %llu,\n"
                 "    timeout RT_INDEFINITE_WAIT...\n",
                 idClient, pszPatterns, u64Timestamp);
    else
        RTPrintf("Called GET_NOTIFICATION, client %d, patterns %s, timestamp %llu,\n"
                 "    timeout %u...\n",
                 idClient, pszPatterns, u64Timestamp, u32Timeout);
    static char szName[] = "Name";
    static char szValue[] = "Value";
    static char szFlags[] = "TRANSIENT";
    if (ppszName)
        *ppszName = szName;
    if (ppszValue)
        *ppszValue = szValue;
    if (pu64Timestamp)
        *pu64Timestamp = 12345;
    if (ppszFlags)
        *ppszFlags = szFlags;
    if (pcbBufActual)
        *pcbBufActual = 256;
    if (pfWasDeleted)
        *pfWasDeleted = false;
    return VINF_SUCCESS;
}

#endif

VBGLR3DECL(int) VbglR3WriteLog(const char *pch, size_t cch)
{
    NOREF(pch); NOREF(cch);
    return VINF_SUCCESS;
}

