/* $Id: GVMMR3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GVMM - Global VM Manager, ring-3 request wrappers.
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
#define LOG_GROUP LOG_GROUP_GVMM
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/uvm.h>
#include <VBox/sup.h>
#include <VBox/err.h>

#include <iprt/mem.h>


/**
 * Driverless: VMMR0_DO_GVMM_CREATE_VM
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   cCpus       The number of CPUs to create the VM for.
 * @param   pSession    The support driver session handle.
 * @param   ppVM        Where to return the pointer to the VM structure.
 * @param   ppVMR0      Where to return the ring-0 address of the VM structure
 *                      for use in VMMR0 calls.
 */
VMMR3_INT_DECL(int) GVMMR3CreateVM(PUVM pUVM, uint32_t cCpus, PSUPDRVSESSION pSession, PVM *ppVM, PRTR0PTR ppVMR0)
{
    AssertReturn(cCpus >= VMM_MIN_CPU_COUNT && cCpus <= VMM_MAX_CPU_COUNT, VERR_INVALID_PARAMETER);
    AssertCompile((sizeof(VM)    & HOST_PAGE_OFFSET_MASK) == 0);
    AssertCompile((sizeof(VMCPU) & HOST_PAGE_OFFSET_MASK) == 0);

    int rc;
    if (!SUPR3IsDriverless())
    {
        GVMMCREATEVMREQ CreateVMReq;
        CreateVMReq.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
        CreateVMReq.Hdr.cbReq       = sizeof(CreateVMReq);
        CreateVMReq.pSession        = pSession;
        CreateVMReq.pVMR0           = NIL_RTR0PTR;
        CreateVMReq.pVMR3           = NULL;
        CreateVMReq.cCpus           = cCpus;
        rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_GVMM_CREATE_VM, 0, &CreateVMReq.Hdr);
        if (RT_SUCCESS(rc))
        {
            *ppVM   = CreateVMReq.pVMR3;
            *ppVMR0 = CreateVMReq.pVMR0;
        }
    }
    else
    {
        /*
         * Driverless.
         */
        /* Allocate the VM structure: */
        size_t const cbVM = sizeof(VM) + sizeof(VMCPU) * cCpus;
        PVM          pVM  = (PVM)RTMemPageAlloc(cbVM + HOST_PAGE_SIZE * (1 + 2 * cCpus));
        if (!pVM)
            return VERR_NO_PAGE_MEMORY;

        /* Set up guard pages: */
        RTMemProtect(pVM, HOST_PAGE_SIZE, RTMEM_PROT_NONE);
        pVM = (PVM)((uintptr_t)pVM + HOST_PAGE_SIZE);
        RTMemProtect(pVM + 1, HOST_PAGE_SIZE, RTMEM_PROT_NONE);

        /* VM: */
        pVM->enmVMState           = VMSTATE_CREATING;
        pVM->pVMR3                = pVM;
        pVM->hSelf                = _1M;
        pVM->pSession             = pSession;
        pVM->cCpus                = cCpus;
        pVM->uCpuExecutionCap     = 100;
        pVM->cbSelf               = sizeof(VM);
        pVM->cbVCpu               = sizeof(VMCPU);
        pVM->uStructVersion       = 1;

        /* CPUs: */
        PVMCPU pVCpu = (PVMCPU)((uintptr_t)pVM + sizeof(VM) + HOST_PAGE_SIZE);
        for (VMCPUID idxCpu = 0; idxCpu < cCpus; idxCpu++)
        {
            pVM->apCpusR3[idxCpu] = pVCpu;

            pVCpu->enmState        = VMCPUSTATE_STOPPED;
            pVCpu->pVMR3           = pVM;
            pVCpu->hNativeThread   = NIL_RTNATIVETHREAD;
            pVCpu->hNativeThreadR0 = NIL_RTNATIVETHREAD;
            pVCpu->hThread         = NIL_RTTHREAD;
            pVCpu->idCpu           = idxCpu;

            RTMemProtect(pVCpu + 1, HOST_PAGE_SIZE, RTMEM_PROT_NONE);
            pVCpu = (PVMCPU)((uintptr_t)pVCpu + sizeof(VMCPU) + HOST_PAGE_SIZE);
        }

        *ppVM   = pVM;
        *ppVMR0 = NIL_RTR0PTR;
    }
    RT_NOREF(pUVM);
    return VINF_SUCCESS;
}


/**
 * Driverless: VMMR0_DO_GVMM_DESTROY_VM
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) GVMMR3DestroyVM(PUVM pUVM, PVM pVM)
{
    AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    Assert(pUVM->cCpus == pVM->cCpus);
    RT_NOREF(pUVM);

    int rc;
    if (!SUPR3IsDriverless())
        rc = SUPR3CallVMMR0Ex(pVM->pVMR0ForCall, 0 /*idCpu*/, VMMR0_DO_GVMM_DESTROY_VM, 0, NULL);
    else
    {
        RTMemPageFree((uint8_t *)pVM - HOST_PAGE_SIZE,
                      sizeof(VM) + sizeof(VMCPU) * pVM->cCpus + HOST_PAGE_SIZE * (1 + 2 * pVM->cCpus));
        rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * Register the calling EMT with GVM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       The Virtual CPU ID.
 * @thread  EMT(idCpu)
 * @see     GVMMR0RegisterVCpu
 */
VMMR3_INT_DECL(int) GVMMR3RegisterVCpu(PVM pVM, VMCPUID idCpu)
{
    Assert(VMMGetCpuId(pVM) == idCpu);
    int rc;
    if (!SUPR3IsDriverless())
    {
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), idCpu, VMMR0_DO_GVMM_REGISTER_VMCPU, 0, NULL);
        if (RT_FAILURE(rc))
            LogRel(("idCpu=%u rc=%Rrc\n", idCpu, rc));
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Deregister the calling EMT from GVM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       The Virtual CPU ID.
 * @thread  EMT(idCpu)
 * @see     GVMMR0DeregisterVCpu
 */
VMMR3_INT_DECL(int) GVMMR3DeregisterVCpu(PVM pVM, VMCPUID idCpu)
{
    Assert(VMMGetCpuId(pVM) == idCpu);
    int rc;
    if (!SUPR3IsDriverless())
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), idCpu, VMMR0_DO_GVMM_DEREGISTER_VMCPU, 0, NULL);
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * @see GVMMR0RegisterWorkerThread
 */
VMMR3_INT_DECL(int)  GVMMR3RegisterWorkerThread(PVM pVM, GVMMWORKERTHREAD enmWorker)
{
    if (SUPR3IsDriverless())
        return VINF_SUCCESS;
    GVMMREGISTERWORKERTHREADREQ Req;
    Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    Req.hNativeThreadR3 = RTThreadNativeSelf();
    return SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID,
                            VMMR0_DO_GVMM_REGISTER_WORKER_THREAD, (unsigned)enmWorker, &Req.Hdr);
}


/**
 * @see GVMMR0DeregisterWorkerThread
 */
VMMR3_INT_DECL(int)  GVMMR3DeregisterWorkerThread(PVM pVM, GVMMWORKERTHREAD enmWorker)
{
    if (SUPR3IsDriverless())
        return VINF_SUCCESS;
    return SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID,
                            VMMR0_DO_GVMM_DEREGISTER_WORKER_THREAD, (unsigned)enmWorker, NULL);
}

