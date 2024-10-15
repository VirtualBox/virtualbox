/* $Id: NEMR0Native-stubs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NEM - Native execution manager, ring-0 stubs until there is a driverless mode.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_NEM
#include <VBox/vmm/nem.h>
#include "NEMInternal.h"
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvmm.h>

#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Module initialization for NEM.
 */
VMMR0_INT_DECL(int)  NEMR0Init(void)
{
    return VINF_SUCCESS;
}


/**
 * Module termination for NEM.
 */
VMMR0_INT_DECL(void) NEMR0Term(void)
{
}


VMMR0_INT_DECL(int) NEMR0InitVM(PGVM pGVM)
{
    RT_NOREF(pGVM);
    return VINF_SUCCESS;
}


VMMR0_INT_DECL(int) NEMR0InitVMPart2(PGVM pGVM)
{
    RT_NOREF(pGVM);
    return VINF_SUCCESS;
}


VMMR0_INT_DECL(void) NEMR0CleanupVM(PGVM pGVM)
{
    RT_NOREF(pGVM);
}


VMMR0_INT_DECL(int) NEMR0MapPages(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}


VMMR0_INT_DECL(int) NEMR0UnmapPages(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}


VMMR0_INT_DECL(int)  NEMR0ExportState(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR0_INT_DECL(int) NEMR0ImportState(PGVM pGVM, VMCPUID idCpu, uint64_t fWhat)
{
    RT_NOREF(pGVM, idCpu, fWhat);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR0_INT_DECL(int) NEMR0QueryCpuTick(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR0_INT_DECL(int) NEMR0ResumeCpuTickOnAll(PGVM pGVM, VMCPUID idCpu, uint64_t uPausedTscValue)
{
    RT_NOREF(pGVM, idCpu, uPausedTscValue);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR0_INT_DECL(VBOXSTRICTRC) NEMR0RunGuestCode(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR0_INT_DECL(int)  NEMR0UpdateStatistics(PGVM pGVM, VMCPUID idCpu)
{
    RT_NOREF(pGVM, idCpu);
    AssertFailed();
    return VINF_SUCCESS;
}


VMMR0_INT_DECL(int) NEMR0DoExperiment(PGVM pGVM, VMCPUID idCpu, uint64_t u64Arg)
{
    RT_NOREF(pGVM, idCpu, u64Arg);
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}


void nemHCNativeNotifyHandlerPhysicalRegister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalRegister: %RGp LB %RGp enmKind=%d\n", GCPhys, cb, enmKind));
    AssertFailed();
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhys); NOREF(cb);
}


void nemHCNativeNotifyHandlerPhysicalModify(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhysOld,
                                            RTGCPHYS GCPhysNew, RTGCPHYS cb, bool fRestoreAsRAM)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalModify: %RGp LB %RGp -> %RGp enmKind=%d fRestoreAsRAM=%d\n",
          GCPhysOld, cb, GCPhysNew, enmKind, fRestoreAsRAM));
    AssertFailed();
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhysOld); NOREF(GCPhysNew); NOREF(cb); NOREF(fRestoreAsRAM);
}


int nemHCNativeNotifyPhysPageAllocated(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint32_t fPageProt,
                                       PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageAllocated: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, GCPhys, HCPhys, fPageProt, enmType, pu2State);
    return VERR_NOT_SUPPORTED;
}


void nemHCNativeNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint32_t fPageProt,
                                          PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageProtChanged: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, GCPhys, HCPhys, fPageProt, enmType, pu2State);
}


void nemHCNativeNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev, RTHCPHYS HCPhysNew,
                                     uint32_t fPageProt, PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageChanged: %RGp HCPhys=%RHp->%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhysPrev, HCPhysNew, fPageProt, enmType, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, GCPhys, HCPhysPrev, HCPhysNew, fPageProt, enmType, pu2State);
}


VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, uint64_t *pcTicks, uint32_t *puAux)
{
    LogFlowFunc(("pVCpu=%p pcTicks=%RX64 puAux=%RX32\n", pVCpu, pcTicks, puAux));
    AssertFailed();
    RT_NOREF(pVCpu, pcTicks, puAux);
    return VERR_NOT_SUPPORTED;
}


VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uPausedTscValue)
{
    LogFlowFunc(("pVM=%p pVCpu=%p uPausedTscValue=%RX64\n", pVM, pVCpu, uPausedTscValue));
    AssertFailed();
    RT_NOREF(pVM, pVCpu, uPausedTscValue);
    return VERR_NOT_SUPPORTED;
}


VMM_INT_DECL(void) NEMHCNotifyHandlerPhysicalDeregister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb,
                                                        RTR3PTR pvMemR3, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyHandlerPhysicalDeregister: %RGp LB %RGp enmKind=%d pvMemR3=%p pu2State=%p (%d)\n",
          GCPhys, cb, enmKind, pvMemR3, pu2State, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, enmKind, GCPhys, cb, pvMemR3, pu2State);
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev, RTHCPHYS HCPhysNew,
                                              RTR3PTR pvNewR3, uint32_t fPageProt, PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageChanged: %RGp HCPhys=%RHp->%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhysPrev, HCPhysNew, fPageProt, enmType, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, GCPhys, HCPhysPrev, HCPhysNew, pvNewR3, fPageProt, enmType, pu2State);
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, RTR3PTR pvR3, uint32_t fPageProt,
                                                  PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageProtChanged: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    AssertFailed();
    RT_NOREF(pVM, GCPhys, HCPhys, pvR3, fPageProt, enmType, pu2State);
}


VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    AssertFailed();
    RT_NOREF(pVCpu, fWhat);
    return VERR_NOT_IMPLEMENTED;
}
