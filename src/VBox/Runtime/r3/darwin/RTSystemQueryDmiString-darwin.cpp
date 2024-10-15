/* $Id: RTSystemQueryDmiString-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryDmiString, darwin ring-3.
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
#include <iprt/system.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <mach/mach_port.h>
#include <IOKit/IOKitLib.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define IOCLASS_PLATFORMEXPERTDEVICE         "IOPlatformExpertDevice"
#define PROP_PRODUCT_NAME                    "product-name"
#define PROP_PRODUCT_VERSION                 "version"
#define PROP_PRODUCT_SERIAL                  "IOPlatformSerialNumber"
#define PROP_PRODUCT_UUID                    "IOPlatformUUID"
#define PROP_MANUFACTURER                    "manufacturer"


RTDECL(int) RTSystemQueryDmiString(RTSYSDMISTR enmString, char *pszBuf, size_t cbBuf)
{
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);
    *pszBuf = '\0';
    AssertReturn(enmString > RTSYSDMISTR_INVALID && enmString < RTSYSDMISTR_END, VERR_INVALID_PARAMETER);

    CFStringRef PropStringRef = NULL;
    switch (enmString)
    {
        case RTSYSDMISTR_PRODUCT_NAME:    PropStringRef = CFSTR(PROP_PRODUCT_NAME);    break;
        case RTSYSDMISTR_PRODUCT_VERSION: PropStringRef = CFSTR(PROP_PRODUCT_VERSION); break;
        case RTSYSDMISTR_PRODUCT_SERIAL:  PropStringRef = CFSTR(PROP_PRODUCT_SERIAL);  break;
        case RTSYSDMISTR_PRODUCT_UUID:    PropStringRef = CFSTR(PROP_PRODUCT_UUID);    break;
        case RTSYSDMISTR_MANUFACTURER:    PropStringRef = CFSTR(PROP_MANUFACTURER);    break;
        default:
            return VERR_NOT_SUPPORTED;
    }

    mach_port_t MasterPort;

    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    kern_return_t kr = IOMasterPort(MACH_PORT_NULL, &MasterPort); /* Deprecated since 12.0. */
    RT_GCC_NO_WARN_DEPRECATED_END
    if (kr != kIOReturnSuccess)
    {
        if (kr == KERN_NO_ACCESS)
            return VERR_ACCESS_DENIED;
        return RTErrConvertFromDarwinIO(kr);
    }

    CFDictionaryRef ClassToMatch = IOServiceMatching(IOCLASS_PLATFORMEXPERTDEVICE);
    if (!ClassToMatch)
        return VERR_NOT_SUPPORTED;

    /* IOServiceGetMatchingServices will always consume ClassToMatch. */
    io_iterator_t Iterator;
    kr = IOServiceGetMatchingServices(MasterPort, ClassToMatch, &Iterator);
    if (kr != kIOReturnSuccess)
        return RTErrConvertFromDarwinIO(kr);

    int rc = VERR_NOT_SUPPORTED;
    io_service_t ServiceObject;
    while ((ServiceObject = IOIteratorNext(Iterator)))
    {
        if (   enmString == RTSYSDMISTR_PRODUCT_NAME
            || enmString == RTSYSDMISTR_PRODUCT_VERSION
            || enmString == RTSYSDMISTR_MANUFACTURER
           )
        {
            CFDataRef DataRef = (CFDataRef)IORegistryEntryCreateCFProperty(ServiceObject, PropStringRef,
                                                                           kCFAllocatorDefault, kNilOptions);
            if (DataRef)
            {
                size_t         cbData  = CFDataGetLength(DataRef);
                const char    *pchData = (const char *)CFDataGetBytePtr(DataRef);
                rc = RTStrCopyEx(pszBuf, cbBuf, pchData, cbData);
                CFRelease(DataRef);
                break;
            }
        }
        else
        {
            CFStringRef StringRef = (CFStringRef)IORegistryEntryCreateCFProperty(ServiceObject, PropStringRef,
                                                                                 kCFAllocatorDefault, kNilOptions);
            if (StringRef)
            {
                Boolean fRc = CFStringGetCString(StringRef, pszBuf, cbBuf, kCFStringEncodingUTF8);
                if (fRc)
                    rc = VINF_SUCCESS;
                else
                {
                    CFIndex cwc    = CFStringGetLength(StringRef);
                    size_t  cbTmp  = cwc + 1;
                    char   *pszTmp = (char *)RTMemTmpAlloc(cbTmp);
                    int     cTries = 1;
                    while (   pszTmp
                           && (fRc = CFStringGetCString(StringRef, pszTmp, cbTmp, kCFStringEncodingUTF8)) == FALSE
                           && cTries++ < 4)
                    {
                        RTMemTmpFree(pszTmp);
                        cbTmp *= 2;
                        pszTmp = (char *)RTMemTmpAlloc(cbTmp);
                    }
                    if (fRc)
                        rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
                    else if (!pszTmp)
                        rc = VERR_NO_TMP_MEMORY;
                    else
                        rc = VERR_ACCESS_DENIED;
                    RTMemFree(pszTmp);
                }
                CFRelease(StringRef);
                break;
            }
        }
    }

    IOObjectRelease(ServiceObject);
    IOObjectRelease(Iterator);
    return rc;
}

