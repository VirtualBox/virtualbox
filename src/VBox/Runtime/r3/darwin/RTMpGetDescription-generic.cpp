/* $Id: RTMpGetDescription-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, RTMpGetDescription for darwin/arm.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/mp.h>
#include "internal/iprt.h"
#include <iprt/err.h>
#include <iprt/string.h>

#include <sys/sysctl.h>
#if defined(RT_ARCH_ARM64)
# include <IOKit/IOKitLib.h>
#endif


RTDECL(int) RTMpGetDescription(RTCPUID idCpu, char *pszBuf, size_t cbBuf)
{
    /*
     * Check that the specified cpu is valid & online.
     */
    if (idCpu != NIL_RTCPUID && !RTMpIsCpuOnline(idCpu))
        return RTMpIsCpuPossible(idCpu)
             ? VERR_CPU_OFFLINE
             : VERR_CPU_NOT_FOUND;

    /*
     * For ARM there are typically two different types of cores, so look up the
     * processor in the IODeviceTree and get the core name and type from there
     * if we can.
     */
    char   szExtra[256];
    size_t cchExtra = 0;

#if defined(RT_ARCH_ARM64)
    char szArmCpuPath[64];
    RTStrPrintf(szArmCpuPath, sizeof(szArmCpuPath), "IODeviceTree:/cpus/cpu%x", idCpu); /** @todo Hex? M1 Max only has 10 cores... */

    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    io_registry_entry_t hIoRegEntry = IORegistryEntryFromPath(kIOMasterPortDefault, szArmCpuPath); /* kIOMasterPortDefault: Deprecated since 12.0. */
    RT_GCC_NO_WARN_DEPRECATED_END
    if (hIoRegEntry != MACH_PORT_NULL)
    {
        /* This property is typically "E" or "P".  Don't know why it's mapped
           to a CFDataRef rather than a CFStringRef... */
        CFTypeRef hValRef = IORegistryEntryCreateCFProperty(hIoRegEntry, CFSTR("cluster-type"), kCFAllocatorDefault, kNilOptions);
        if (hValRef)
        {
            if (CFGetTypeID(hValRef) == CFDataGetTypeID())
            {
                size_t const          cbData = CFDataGetLength((CFDataRef)hValRef);
                uint8_t const * const pbData = (uint8_t const *)CFDataGetBytePtr((CFDataRef)hValRef);
                if (cbData > 0 && pbData != NULL)
                {
                    int rc = RTStrValidateEncodingEx((const char *)pbData, cbData, RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                    AssertMsgRC(rc, ("%p LB %#zx: %.*Rhxs\n", pbData, cbData, cbData, pbData));
                    if (RT_SUCCESS(rc))
                    {
                        RTStrCopy(&szExtra[1], sizeof(szExtra) - 1, (const char *)pbData);
                        szExtra[0] = ' ';
                        cchExtra = strlen(szExtra);
                    }
                }
            }
            else
                AssertMsgFailed(("%p=%#lx\n", hValRef, CFGetTypeID(hValRef)));

            CFRelease(hValRef);
        }

        /* The compatible property is an "array" of zero terminated strings.
           For the M1 mini the first entry is either "apple,firestorm" (P cores)
           or "apple,icestorm" (E cores).  We extract the bits after the comma
           and append it to the extra string. (Again, dunno why it's a CFDataRef.) */
        hValRef = IORegistryEntryCreateCFProperty(hIoRegEntry, CFSTR("compatible"), kCFAllocatorDefault, 0);
        if (hValRef)
        {
            if (CFGetTypeID(hValRef) == CFDataGetTypeID())
            {
                size_t const          cbData = CFDataGetLength((CFDataRef)hValRef);
                uint8_t const * const pbData = (uint8_t const *)CFDataGetBytePtr((CFDataRef)hValRef);
                if (cbData > 0 && pbData != NULL)
                {
                    Assert(pbData[cbData - 1] == '\0');
                    if (pbData[cbData - 1] == '\0')
                    {
                        size_t offData = 0;
                        while (offData < cbData)
                        {
                            const char  *psz = (const char *)&pbData[offData];
                            size_t const cch = strlen(psz);

                            if (RTStrStartsWith(psz, "apple,"))
                            {
                                psz += sizeof("apple,") - 1;
                                psz = RTStrStripL(psz);
                                if (*psz)
                                {
                                    if (RTStrIsValidEncoding(psz))
                                        cchExtra += RTStrPrintf(&szExtra[cchExtra], sizeof(szExtra) - cchExtra, " (%s)", psz);
                                    else
                                        AssertFailed();
                                }
                            }

                            /* advance */
                            offData += cch + 1;
                        }
                    }
                }
            }
            else
                AssertMsgFailed(("%p=%#lx\n", hValRef, CFGetTypeID(hValRef)));
            CFRelease(hValRef);
        }

        IOObjectRelease(hIoRegEntry);
    }
#endif
    szExtra[cchExtra] = '\0';

    /*
     * Just use the sysctl machdep.cpu.brand_string value for now.
     */
    char szBrand[128] = {0};
    size_t cb = sizeof(szBrand);
    int rc = sysctlbyname("machdep.cpu.brand_string", &szBrand, &cb, NULL, 0);
    if (rc == -1)
        szBrand[0] = '\0';

    char *pszStripped = RTStrStrip(szBrand);
    if (*pszStripped == '\0')
        pszStripped = strcpy(szBrand, "Unknown");

    rc = RTStrCopy(pszBuf, cbBuf, pszStripped);
    if (cchExtra > 0 && RT_SUCCESS(rc))
        rc = RTStrCat(pszBuf, cbBuf, szExtra);
    return rc;
}
RT_EXPORT_SYMBOL(RTMpGetDescription);

