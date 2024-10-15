/* $Id: VBoxGuestR3LibInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 support library for the guest additions, Internal header.
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

#ifndef GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR3LibInternal_h
#define GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR3LibInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLib.h>

#ifdef VBOX_VBGLR3_XFREE86
/* Rather than try to resolve all the header file conflicts, I will just
   prototype what we need here. */
typedef unsigned long xf86size_t;
extern "C" xf86size_t xf86strlen(const char*);
# undef strlen
# define strlen xf86strlen
#endif /* VBOX_VBGLR3_XFREE86 */

RT_C_DECLS_BEGIN

int     vbglR3DoIOCtl(uintptr_t uFunction, PVBGLREQHDR pReq, size_t cbReq);
int     vbglR3DoIOCtlRaw(uintptr_t uFunction, PVBGLREQHDR pReq, size_t cbReq);
int     vbglR3GRAlloc(VMMDevRequestHeader **ppReq, size_t cb, VMMDevRequestType enmReqType);
int     vbglR3GRPerform(VMMDevRequestHeader *pReq);
void    vbglR3GRFree(VMMDevRequestHeader *pReq);



DECLINLINE(void) VbglHGCMParmUInt32Set(HGCMFunctionParameter *pParm, uint32_t u32)
{
    pParm->type = VMMDevHGCMParmType_32bit;
    pParm->u.value64 = 0; /* init unused bits to 0 */
    pParm->u.value32 = u32;
}


DECLINLINE(int) VbglHGCMParmUInt32Get(HGCMFunctionParameter *pParm, uint32_t *pu32)
{
    if (pParm->type == VMMDevHGCMParmType_32bit)
    {
        *pu32 = pParm->u.value32;
        return VINF_SUCCESS;
    }
    *pu32 = UINT32_MAX; /* shut up gcc */
    return VERR_WRONG_PARAMETER_TYPE;
}


DECLINLINE(void) VbglHGCMParmUInt64Set(HGCMFunctionParameter *pParm, uint64_t u64)
{
    pParm->type      = VMMDevHGCMParmType_64bit;
    pParm->u.value64 = u64;
}


DECLINLINE(int) VbglHGCMParmUInt64Get(HGCMFunctionParameter *pParm, uint64_t *pu64)
{
    if (pParm->type == VMMDevHGCMParmType_64bit)
    {
        *pu64 = pParm->u.value64;
        return VINF_SUCCESS;
    }
    *pu64 = UINT64_MAX; /* shut up gcc */
    return VERR_WRONG_PARAMETER_TYPE;
}


DECLINLINE(void) VbglHGCMParmPtrSet(HGCMFunctionParameter *pParm, void *pv, uint32_t cb)
{
    pParm->type                    = VMMDevHGCMParmType_LinAddr;
    pParm->u.Pointer.size          = cb;
    pParm->u.Pointer.u.linearAddr  = (uintptr_t)pv;
}


#ifdef IPRT_INCLUDED_string_h

DECLINLINE(void) VbglHGCMParmPtrSetString(HGCMFunctionParameter *pParm, const char *psz)
{
    pParm->type                    = VMMDevHGCMParmType_LinAddr_In;
    pParm->u.Pointer.size          = (uint32_t)strlen(psz) + 1;
    pParm->u.Pointer.u.linearAddr  = (uintptr_t)psz;
}

#endif /* IPRT_INCLUDED_string_h */

#ifdef VBOX_VBGLR3_XFREE86
# undef strlen
#endif /* VBOX_VBGLR3_XFREE86 */

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR3LibInternal_h */

