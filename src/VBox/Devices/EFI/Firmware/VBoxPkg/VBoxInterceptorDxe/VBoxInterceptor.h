/* $Id: VBoxInterceptor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxInterceptor.h - Helpful macrodefinitions used in the interceptor.
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

#ifndef __VBOXINTERCEPTOR_H__
#define __VBOXINTERCEPTOR_H__
#include <Uefi.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include "print_types.h"

#define VBOXINTERCEPTOR VBoxInterceptor
#define XCONCAT(a,b) a##b
#define CONCAT(a,b) XCONCAT(a,b)
#define CONCAT3(a,b,c) a ## b ## c

#define SCL(type) PRNT_##type, type
#define PTR(type) PRNT_P_##type, type *
#define PTR2(type) PRNT_PP_##type, type **
#define PTR3(type) PRNT_PP_##type, type ***
#define PTRC(type) PRNT_P_##type, CONST type *

#define PARAMETER1(ign0, type0) type0 a0
#define PARAMETER2(ign0, type0, ign1, type1) type0 a0, type1 a1
#define PARAMETER3(ign0, type0, ign1, type1, ign2, type2) type0 a0, type1 a1, type2 a2
#define PARAMETER4(ign0, type0, ign1, type1, ign2, type2, ign3, type3) type0 a0, type1 a1, type2 a2, type3 a3
#define PARAMETER5(ign0, type0, ign1, type1, ign2, type2, ign3, type3, ign4, type4) type0 a0, type1 a1, type2 a2, type3 a3, type4 a4
#define PARAMETER6(ign0, type0, ign1, type1, ign2, type2, ign3, type3, ign4, type4, ign5, type5) type0 a0, type1 a1, type2 a2, type3 a3, type4 a4, type5 a5

#define PARAMETER(x) PARAMETER##x

#define PRNT_PARAMETER1(f0, ign0) "(" f0 ")"
#define PRNT_PARAMETER2(f0, ign0, f1, ign1) "(" f0 "," f1 ")"
#define PRNT_PARAMETER3(f0, ign0, f1, ign1, f2, ign2) "(" f0 "," f1 "," f2 ")"
#define PRNT_PARAMETER4(f0, ign0, f1, ign1, f2, ign2, f3, ign3) "(" f0 "," f1 "," f2 "," f3 ")"
#define PRNT_PARAMETER5(f0, ign0, f1, ign1, f2, ign2, f3, ign3, f4, ign4) "(" f0 "," f1 "," f2 "," f3 "," f4 ")"
#define PRNT_PARAMETER6(f0, ign0, f1, ign1, f2, ign2, f3, ign3, f4, ign4, f5, ign5) "(" f0 "," f1 "," f2 "," f3 "," f4 "," f5 ")"

#define PRNT_PARAMETERS(x) PRNT_PARAMETER##x

#define ARGS1 a0
#define ARGS2 a0, a1
#define ARGS3 a0, a1, a2
#define ARGS4 a0, a1, a2, a3
#define ARGS5 a0, a1, a2, a3, a4
#define ARGS6 a0, a1, a2, a3, a4, a5

#define ARGS(x) ARGS##x

char* indentRight();
char* indentLeft();

#if ARCH_BITS == 64
# define ARCH_FRAME_POINTER "rbp"
#elif ARCH_BITS == 32
# define ARCH_FRAME_POINTER "ebp"
#else
# error "port me"
#endif

#define DUMP_STACK(depth)                                                       \
do {                                                                            \
    int i;                                                                      \
    UINTN *bp = (UINTN *)frame_pointer;                                         \
    for (i = 0; i < depth; ++i)                                                 \
    {                                                                           \
        DEBUG((DEBUG_INFO, "[%d frame pbp:%x ip: %x]\n", i, bp[0], bp[1]));     \
        if (bp == NULL || bp < (UINTN *)0x1000)                                 \
            break;                                                              \
        bp = *(UINTN **)bp;                                                     \
    }                                                                           \
} while(0)


register volatile UINTN *frame_pointer asm(ARCH_FRAME_POINTER);

#define FUNCTION(RET_TYPE) RET_TYPE ## _FUNCTION
#define RVOID_FUNCTION(return_type, func_name, nparams, params)                 \
static void EFIAPI CONCAT(VBOXINTERCEPTOR,func_name)(PARAMETER(nparams)params)  \
{                                                                               \
    UINT32 off = (UINT32)(uintptr_t)&(((typeof(gThis))0)->CONCAT(SERVICE,Orig).func_name); \
    DEBUG((DEBUG_INFO, "%a%a[%x] enter " PRNT_PARAMETERS(nparams)params "\n",   \
           indentRight(), #func_name, off, ARGS(nparams)));                     \
    DUMP_STACK(2);                                                              \
    gThis->CONCAT(SERVICE,Orig).func_name(ARGS(nparams));                       \
    DEBUG((DEBUG_INFO, "%a%a exit \n", indentLeft(), #func_name));              \
}

/*XXX: Assume atm that Bs and Rt if func returns smth, this smt is EFI_STATUS */
#define NVOID_FUNCTION(return_type, func_name, nparams, params)                  \
static return_type EFIAPI CONCAT(VBOXINTERCEPTOR,func_name)(PARAMETER(nparams)params)  \
{                                                                               \
    return_type r;                                                              \
    UINT32 off = (UINT32)(uintptr_t)&(((typeof(gThis))0)->CONCAT(SERVICE,Orig).func_name); \
    DEBUG((DEBUG_INFO, "%a%a[%x] enter " PRNT_PARAMETERS(nparams)params "\n",   \
           indentRight(), #func_name, off, ARGS(nparams)));                     \
    DUMP_STACK(2);                                                              \
    r =gThis->CONCAT(SERVICE,Orig).func_name(ARGS(nparams));                    \
    DEBUG((DEBUG_INFO, "%a%a exit:(%r) \n",                                     \
           indentLeft(), #func_name, r));                                       \
    return r;                                                                   \
}

#define INSTALLER(x) CONCAT3(install_ ,x, _interceptors)
#define UNINSTALLER(x) CONCAT3(uninstall_,x,_interceptors)

typedef struct {
    EFI_BOOT_SERVICES bsOrig;
    EFI_RUNTIME_SERVICES rtOrig;
} VBOXINTERCEPTOR, *PVBOXINTERCEPTOR;

PVBOXINTERCEPTOR gThis;

EFI_STATUS install_bs_interceptors();
EFI_STATUS uninstall_bs_interceptors();
EFI_STATUS install_rt_interceptors();
EFI_STATUS uninstall_rt_interceptors();
#endif
