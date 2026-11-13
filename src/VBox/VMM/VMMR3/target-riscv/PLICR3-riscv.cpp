/* $Id$ */
/** @file
 * PLIC - Platform Level Interrupt Controller (PLIC) - VBox PLIC emulation.
 */

/*
 * Copyright (C) 2026 Alexander Eichner <github@aeichner.de>.
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
#define LOG_GROUP LOG_GROUP_DEV_GIC
#include <VBox/log.h>
#include "PLICInternal.h"
#include <VBox/vmm/pdmaia.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The PLIC MMIO region size in bytes. */
#define PLIC_MMIO_SIZE _64M

/** Base offset of the interrupt source priority table. */
#define PLIC_REG_INTR_PRIO_OFF         0
#define PLIC_REG_INTR_PRIO_SIZE        4
#define PLIC_REG_INTR_PRIO_SHIFT       2
#define PLIC_REG_INTR_PRIO_COUNT    1024

/** Base offset of the interrupt pending table. */
#define PLIC_REG_INTR_PENDING_OFF           0x1000
#define PLIC_REG_INTR_PENDING_SIZE          128
/** Base offset of the enable bits table for the different contexts. */
#define PLIC_REG_INTR_ENABLE_OFF            0x2000
#define PLIC_REG_INTR_ENABLE_SIZE_PER_CTX   0x100
#define PLIC_REG_INTR_ENABLE_BITS_PER_REG   32

/** Base offsets of the per context registers. */
#define PLIC_REG_PER_CTX_OFF            0x200000
#define PLIC_REG_PER_CTX_SIZE           0x1000
#define PLIC_REG_PER_CTX_PRIO_OFF       0
#define PLIC_REG_PER_CTX_CLAIM_OFF      4
#define PLIC_REG_PER_CTX_COMPLETE_OFF   4

/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Sets the interrupt pending force-flag and pokes the EMT if required.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void plicSetInterruptFF(PVMCPUCC pVCpu)
{
#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);

    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (VMMGetCpuId(pVM) != idCpu)
    {
        Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
        VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_POKE);
    }
}


static void plicIntrUpdateCpu(PVMCC pVM, PVMCPUCC pVCpu)
{
    //if (pVCpu->plic.s.u32PriThr > 0)
    {
        for (size_t i = 0; i < RT_ELEMENTS(pVM->plic.s.bmIrqPending); i++)
        {
            uint32_t const uFragment = pVM->plic.s.bmIrqPending[i];
            if (uFragment & pVCpu->plic.s.bmIrqEnabled[i])
                plicSetInterruptFF(pVCpu);
        }
    }
}


static uint32_t plicIntrClaim(PPLIC pPlic, PPLICCPU pPlicCpu)
{
    Log2(("plicIntrClaim:\n"));

    //if (pPlicCpu->u32PriThr > 0)
    {
        for (size_t i = 0; i < RT_ELEMENTS(pPlic->bmIrqPending); i++)
        {
            uint32_t const uFragment = pPlic->bmIrqPending[i];
            if (uFragment & pPlicCpu->bmIrqEnabled[i])
                return (i << 5) | (ASMBitFirstSetU32(pPlicCpu->bmIrqEnabled[i]) - 1);
        }
    }

    return 0;
}


static void plicIntrUpdate(PVMCC pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpu = pVM->apCpusR3[idCpu];
        plicIntrUpdateCpu(pVM, pVCpu);
    }
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECLCALLBACK(VBOXSTRICTRC) plicMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu  = PDMDevHlpGetVMCPU(pDevIns);
    PVMCC    pVM    = PDMDevHlpGetVM(pDevIns);
    PPLIC    pPlic  = VM_TO_PLIC(pVM);
    uint32_t uValue = 0;

    if (/*off >= PLIC_REG_INTR_PRIO_OFF &&*/ off < (PLIC_REG_INTR_PRIO_COUNT * PLIC_REG_INTR_PRIO_SIZE))
        uValue = 1; /** @todo Currently hardwired, pPlic->au32IrqPriority[off >> PLIC_REG_INTR_PRIO_SHIFT]; */
    else if (off >= PLIC_REG_INTR_PENDING_OFF && off < (PLIC_REG_INTR_PENDING_OFF + PLIC_REG_INTR_PENDING_SIZE))
        uValue = pPlic->bmIrqPending[(off - PLIC_REG_INTR_PENDING_OFF)];
    else if (off >= PLIC_REG_INTR_ENABLE_OFF && off < (PLIC_REG_INTR_ENABLE_OFF + pVM->cCpus * PLIC_REG_INTR_ENABLE_SIZE_PER_CTX))
    {
        RTGCPHYS const offRegion = off - PLIC_REG_INTR_ENABLE_OFF;
        VMCPUID idCpu = offRegion / PLIC_REG_INTR_ENABLE_SIZE_PER_CTX;

        if (idCpu < pVM->cCpus)
        {
            PPLICCPU pPlicCpu = VMCPU_TO_PLICCPU(pVCpu);
            uint32_t const offReg = (offRegion % PLIC_REG_INTR_ENABLE_SIZE_PER_CTX) >> 2;
            uValue = pPlicCpu->bmIrqEnabled[offReg];
        }
    }
    else if (off >= PLIC_REG_PER_CTX_OFF)
    {
        RTGCPHYS const offRegion = off - PLIC_REG_PER_CTX_OFF;
        VMCPUID idCpu = offRegion / PLIC_REG_PER_CTX_SIZE;

        if (idCpu < pVM->cCpus)
        {
            PPLICCPU pPlicCpu = VMCPU_TO_PLICCPU(pVCpu);
            uint32_t const offReg = offRegion % PLIC_REG_PER_CTX_SIZE;
            if (offReg == PLIC_REG_PER_CTX_PRIO_OFF)
                uValue = pPlicCpu->u32PriThr;
            else if (offReg == PLIC_REG_PER_CTX_CLAIM_OFF)
                uValue = plicIntrClaim(pPlic, pPlicCpu);
        }
    }

    *(uint32_t *)pv = uValue;

    Log2(("PLIC%u: plicReadMmio: offReg=%#RX64 uValue=%#RX32\n", pVCpu->idCpu, off, uValue));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECLCALLBACK(VBOXSTRICTRC) plicMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu  = PDMDevHlpGetVMCPU(pDevIns);
    PVMCC    pVM    = PDMDevHlpGetVM(pDevIns);
    uint32_t uValue = *(uint32_t *)pv;

    Log2(("PLIC%u: plicWriteMmio: offReg=%#RX64 uValue=%#RX32\n", pVCpu->idCpu, off, uValue));

    if (/*off >= PLIC_REG_INTR_PRIO_OFF &&*/ off < (PLIC_REG_INTR_PRIO_COUNT * PLIC_REG_INTR_PRIO_SIZE))
        ; /** @todo Currently hardwired */
    /*else if (off >= PLIC_REG_INTR_PENDING_OFF && off < (PLIC_REG_INTR_PENDING_OFF + PLIC_REG_INTR_PENDING_SIZE)) - readonly */
    else if (off >= PLIC_REG_INTR_ENABLE_OFF && off < (PLIC_REG_INTR_ENABLE_OFF + pVM->cCpus * PLIC_REG_INTR_ENABLE_SIZE_PER_CTX))
    {
        RTGCPHYS const offRegion = off - PLIC_REG_INTR_ENABLE_OFF;
        VMCPUID idCpu = offRegion / PLIC_REG_INTR_ENABLE_SIZE_PER_CTX;

        if (idCpu < pVM->cCpus)
        {
            PPLICCPU pPlicCpu = VMCPU_TO_PLICCPU(pVCpu);
            uint32_t const offReg = (offRegion % PLIC_REG_INTR_ENABLE_SIZE_PER_CTX) >> 2;
            pPlicCpu->bmIrqEnabled[offReg] = uValue;
        }
    }
    else if (off >= PLIC_REG_PER_CTX_OFF)
    {
        RTGCPHYS const offRegion = off - PLIC_REG_PER_CTX_OFF;
        VMCPUID idCpu = offRegion / PLIC_REG_PER_CTX_SIZE;

        if (idCpu < pVM->cCpus)
        {
            PPLICCPU pPlicCpu = VMCPU_TO_PLICCPU(pVCpu);
            uint32_t const offReg = offRegion % PLIC_REG_PER_CTX_SIZE;
            if (offReg == PLIC_REG_PER_CTX_PRIO_OFF)
            {
                pPlicCpu->u32PriThr = uValue;
                //plicIntrUpdateCpu(pVM, pVCpu);
            }
            else if (offReg == PLIC_REG_PER_CTX_COMPLETE_OFF)
            {
                PPLIC pPlic = VM_TO_PLIC(pVM);
                if (uValue < RT_ELEMENTS(pPlic->bmIrqPending) * 32)
                {
                    ASMBitClear(&pPlic->bmIrqPending[0], uValue);
                    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
                    plicIntrUpdateCpu(pVM, pVCpu);
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Sets the given external interrupt inside the in-kernel KVM AIA.
 *
 * @returns VBox status code.
 * @param   pVM         The VM instance.
 * @param   uIntId      The interrupt ID to update.
 * @param   fAsserted   Flag whether the interrupt is asserted (true) or not (false).
 */
static DECLCALLBACK(int) plicR3SetExternal(PVMCC pVM, uint32_t u32Irq, bool fAsserted)
{
    PPLIC pPlic = VM_TO_PLIC(pVM);
    PPDMDEVINS pDevIns = pPlic->CTX_SUFF(pDevIns);

    if (fAsserted)
    {
        int rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->CTX_SUFF(pCritSectRo), VERR_IGNORED);
        AssertRCReturn(rc, rc);

        bool const fPending = ASMBitTestAndSet(&pVM->plic.s.bmIrqPending[0], u32Irq);
        if (!fPending)
            plicIntrUpdate(pVM);

        PDMDevHlpCritSectLeave(pDevIns, pDevIns->CTX_SUFF(pCritSectRo));
    }

    return VINF_SUCCESS;
}


/**
 * Sets the given HART interrupt for the PLIC.
 *
 * @returns VBox status code.
 * @param   pVCpu       The vCPU for whih the PPI state is updated.
 * @param   uIntId      The PPI ID to update.
 * @param   fAsserted   Flag whether the interrupt is asserted (true) or not (false).
 */
static DECLCALLBACK(int) plicR3SetHart(PVMCPUCC pVCpu, uint32_t u32Irq, bool fAsserted)
{
    RT_NOREF(pVCpu, u32Irq, fAsserted);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) plicR3Reset(PPDMDEVINS pDevIns)
{
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    RT_NOREF(pVM);

    LogFlow(("PLIC: plicR3Reset\n"));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) plicR3Destruct(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) plicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPLICDEV        pThis    = PDMDEVINS_2_DATA(pDevIns, PPLICDEV);
    PCPDMDEVHLPR3   pHlp     = pDevIns->pHlpR3;
    PVM             pVM      = PDMDevHlpGetVM(pDevIns);
    PPLIC           pPlic    = VM_TO_PLIC(pVM);
    Assert(iInstance == 0); NOREF(iInstance);

    /*
     * Init the data.
     */
    pPlic->pDevInsR3 = pDevIns;

    /*
     * Validate AIA settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "PlicMmioBase", "");

#if 0 /** @todo Later */
    /*
     * Disable automatic PDM locking for this device.
     */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);
#else
    int rc;
#endif

    /*
     * Register the PLIC with PDM.
     */
    rc = PDMDevHlpIcRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMAiaRegisterBackend(pVM, PDMAIABACKENDTYPE_VBOX_PLIC, &g_AiaPlicBackend);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Query the MMIO ranges.
     */
    RTGCPHYS GCPhysMmioBasePlic = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "PlicMmioBase", &GCPhysMmioBasePlic);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"PlicMmioBase\" value"));

    rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBasePlic, PLIC_MMIO_SIZE, plicMmioWrite, plicMmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "PLIC", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    return VINF_SUCCESS;
}


/**
 * AIA device registration structure.
 */
const PDMDEVREG g_DevicePLIC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "plic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(PLIC),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Platform Level Interrupt Controller (PLIC)",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           plicR3Construct,
    /* .pfnDestruct = */            plicR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               plicR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};


/**
 * The PLIC AIA backend.
 */
const PDMAIABACKEND g_AiaPlicBackend =
{
    /* .pfnReadSysReg = */  NULL,
    /* .pfnWriteSysReg = */ NULL,
    /* .pfnSetExternal = */ plicR3SetExternal,
    /* .pfnSetHart = */     plicR3SetHart,
    /* .pfnSendMsi = */     NULL,
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

