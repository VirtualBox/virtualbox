/* $Id: HMVMXR0.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HM VMX (VT-x) - Internal header file.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VMM_INCLUDED_SRC_VMMR0_HMVMXR0_h
#define VMM_INCLUDED_SRC_VMMR0_HMVMXR0_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

RT_C_DECLS_BEGIN

/** @defgroup grp_vmx_int   Internal
 * @ingroup grp_vmx
 * @internal
 * @{
 */

#ifdef IN_RING0
VMMR0DECL(int)          VMXR0Enter(PVMCPUCC pVCpu);
VMMR0DECL(void)         VMXR0ThreadCtxCallback(RTTHREADCTXEVENT enmEvent, PVMCPUCC pVCpu, bool fGlobalInit);
VMMR0DECL(int)          VMXR0AssertionCallback(PVMCPUCC pVCpu);
VMMR0DECL(int)          VMXR0EnableCpu(PHMPHYSCPU pHostCpu, PVMCC pVM, void *pvPageCpu, RTHCPHYS pPageCpuPhys,
                                       bool fEnabledBySystem, PCSUPHWVIRTMSRS pHwvirtMsrs);
VMMR0DECL(int)          VMXR0DisableCpu(PHMPHYSCPU pHostCpu, void *pvPageCpu, RTHCPHYS pPageCpuPhys);
VMMR0DECL(int)          VMXR0GlobalInit(void);
VMMR0DECL(void)         VMXR0GlobalTerm(void);
VMMR0DECL(int)          VMXR0InitVM(PVMCC pVM);
VMMR0DECL(int)          VMXR0TermVM(PVMCC pVM);
VMMR0DECL(int)          VMXR0SetupVM(PVMCC pVM);
VMMR0DECL(int)          VMXR0ExportHostState(PVMCPUCC pVCpu);
VMMR0DECL(int)          VMXR0InvalidatePage(PVMCPUCC pVCpu, RTGCPTR GCVirt);
VMMR0DECL(int)          VMXR0ImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat);
VMMR0DECL(int)          VMXR0GetExitAuxInfo(PVMCPUCC pVCpu, PVMXEXITAUX pVmxExitAux, uint32_t fWhat);
VMMR0DECL(VBOXSTRICTRC) VMXR0RunGuestCode(PVMCPUCC pVCpu);
#endif /* IN_RING0 */

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_VMMR0_HMVMXR0_h */

