/* $Id: PDMAll.cpp 110211 2025-07-14 10:40:56Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * PDM Critical Sections
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#ifdef VBOX_VMM_TARGET_ARMV8
# include <VBox/vmm/pdmgic.h>
#else
# include <VBox/vmm/pdmapic.h>
#endif

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>

#include "PDMInline.h"
#include "dtrace/VBoxVMM.h"


#if !defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Gets the pending interrupt.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_APIC_INTR_MASKED_BY_TPR when an APIC interrupt is pending but
 *          can't be delivered due to TPR priority.
 * @retval  VERR_NO_DATA if there is no interrupt to be delivered (either APIC
 *          has been software-disabled since it flagged something was pending,
 *          or other reasons).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pu8Interrupt    Where to store the interrupt.
 */
VMMDECL(int) PDMGetInterrupt(PVMCPUCC pVCpu, uint8_t *pu8Interrupt)
{
    /*
     * The local APIC has a higher priority than the PIC.
     */
    int rc = VERR_NO_DATA;
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC))
    {
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_APIC);

        uint32_t uTagSrc;
        rc = PDMApicGetInterrupt(pVCpu, pu8Interrupt, &uTagSrc);
        if (RT_SUCCESS(rc))
        {
            VBOXVMM_PDM_IRQ_GET(pVCpu, RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc), *pu8Interrupt);
            Log8(("PDMGetInterrupt: irq=%#x tag=%#x (apic)\n", *pu8Interrupt, uTagSrc));
            return VINF_SUCCESS;
        }
        /* else if it's masked by TPR/PPR/whatever, go ahead checking the PIC. Such masked
           interrupts shouldn't prevent ExtINT from being delivered. */
    }

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    pdmLock(pVM);

    /*
     * Check the PIC.
     */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC))
    {
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_PIC);
        Assert(pVM->pdm.s.Pic.CTX_SUFF(pDevIns));
        Assert(pVM->pdm.s.Pic.CTX_SUFF(pfnGetInterrupt));
        uint32_t uTagSrc;
        int i = pVM->pdm.s.Pic.CTX_SUFF(pfnGetInterrupt)(pVM->pdm.s.Pic.CTX_SUFF(pDevIns), &uTagSrc);
        AssertMsg(i <= 255 && i >= 0, ("i=%d\n", i));
        if (i >= 0)
        {
            pdmUnlock(pVM);
            *pu8Interrupt = (uint8_t)i;
            VBOXVMM_PDM_IRQ_GET(pVCpu, RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc), i);
            Log8(("PDMGetInterrupt: irq=%#x tag=%#x (pic)\n", i, uTagSrc));
            return VINF_SUCCESS;
        }
    }

    /*
     * One scenario where we may possibly get here is if the APIC signaled a pending interrupt,
     * got an APIC MMIO/MSR VM-exit which disabled the APIC. We could, in theory, clear the APIC
     * force-flag from all the places which disables the APIC but letting PDMGetInterrupt() fail
     * without returning a valid interrupt still needs to be handled for the TPR masked case,
     * so we shall just handle it here regardless if we choose to update the APIC code in the future.
     */

    pdmUnlock(pVM);
    return rc;
}
#endif


/**
 * Sets the pending interrupt coming from ISA source or HPET.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   u8Irq           The IRQ line.
 * @param   u8Level         The new level.
 * @param   uTagSrc         The IRQ tag and source tracer ID.
 */
VMMDECL(int) PDMIsaSetIrq(PVMCC pVM, uint8_t u8Irq, uint8_t u8Level, uint32_t uTagSrc)
{
    pdmLock(pVM);

    /** @todo put the IRQ13 code elsewhere to avoid this unnecessary bloat. */
    if (!uTagSrc && (u8Level & PDM_IRQ_LEVEL_HIGH)) /* FPU IRQ */
    {
        if (u8Level == PDM_IRQ_LEVEL_HIGH)
            VBOXVMM_PDM_IRQ_HIGH(VMMGetCpu(pVM), 0, 0);
        else
            VBOXVMM_PDM_IRQ_HILO(VMMGetCpu(pVM), 0, 0);
    }
    Log9(("PDMIsaSetIrq: irq=%#x lvl=%u tag=%#x\n", u8Irq, u8Level, uTagSrc));

#ifdef VBOX_VMM_TARGET_ARMV8
    int rc = VINF_SUCCESS;
    PDMGicSetSpi(pVM, u8Irq, u8Level == PDM_IRQ_LEVEL_HIGH ? true : false);
#else
    int rc = VERR_PDM_NO_PIC_INSTANCE;
/** @todo r=bird: This code is incorrect, as it ASSUMES the PIC and I/O APIC
 *        are always ring-0 enabled! */
    if (pVM->pdm.s.Pic.CTX_SUFF(pDevIns))
    {
        Assert(pVM->pdm.s.Pic.CTX_SUFF(pfnSetIrq));
        pVM->pdm.s.Pic.CTX_SUFF(pfnSetIrq)(pVM->pdm.s.Pic.CTX_SUFF(pDevIns), u8Irq, u8Level, uTagSrc);
        rc = VINF_SUCCESS;
    }

    if (pVM->pdm.s.IoApic.CTX_SUFF(pDevIns))
    {
        Assert(pVM->pdm.s.IoApic.CTX_SUFF(pfnSetIrq));

        /*
         * Apply Interrupt Source Override rules.
         * See ACPI 4.0 specification 5.2.12.4 and 5.2.12.5 for details on
         * interrupt source override.
         * Shortly, ISA IRQ0 is electically connected to pin 2 on IO-APIC, and some OSes,
         * notably recent OS X rely upon this configuration.
         * If changing, also update override rules in MADT and MPS.
         */
        /* ISA IRQ0 routed to pin 2, all others ISA sources are identity mapped */
        if (u8Irq == 0)
            u8Irq = 2;

        pVM->pdm.s.IoApic.CTX_SUFF(pfnSetIrq)(pVM->pdm.s.IoApic.CTX_SUFF(pDevIns), NIL_PCIBDF, u8Irq, u8Level, uTagSrc);
        rc = VINF_SUCCESS;
    }
#endif

    if (!uTagSrc && u8Level == PDM_IRQ_LEVEL_LOW)
        VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pVM), 0, 0);
    pdmUnlock(pVM);
    return rc;
}


/**
 * Sets the pending I/O APIC interrupt.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   u8Irq       The IRQ line.
 * @param   uBusDevFn   The bus:device:function of the device initiating the IRQ.
 *                      Pass NIL_PCIBDF when it's not a PCI device or interrupt.
 * @param   u8Level     The new level.
 * @param   uTagSrc     The IRQ tag and source tracer ID.
 */
VMM_INT_DECL(int) PDMIoApicSetIrq(PVM pVM, PCIBDF uBusDevFn, uint8_t u8Irq, uint8_t u8Level, uint32_t uTagSrc)
{
    Log9(("PDMIoApicSetIrq: irq=%#x lvl=%u tag=%#x src=%#x\n", u8Irq, u8Level, uTagSrc, uBusDevFn));

#ifdef VBOX_VMM_TARGET_ARMV8
    RT_NOREF(uBusDevFn, uTagSrc);
    PDMGicSetSpi(pVM, u8Irq, u8Level == PDM_IRQ_LEVEL_HIGH ? true : false);
    return VINF_SUCCESS;
#else
    if (pVM->pdm.s.IoApic.CTX_SUFF(pDevIns))
    {
        Assert(pVM->pdm.s.IoApic.CTX_SUFF(pfnSetIrq));
        pVM->pdm.s.IoApic.CTX_SUFF(pfnSetIrq)(pVM->pdm.s.IoApic.CTX_SUFF(pDevIns), uBusDevFn, u8Irq, u8Level, uTagSrc);
        return VINF_SUCCESS;
    }
    return VERR_PDM_NO_PIC_INSTANCE;
#endif
}


/**
 * Broadcasts an EOI to the I/O APIC(s).
 *
 * @param   pVM         The cross context VM structure.
 * @param   uVector     The interrupt vector corresponding to the EOI.
 */
VMM_INT_DECL(void) PDMIoApicBroadcastEoi(PVMCC pVM, uint8_t uVector)
{
    /*
     * At present, we support only a maximum of one I/O APIC per-VM. If we ever implement having
     * multiple I/O APICs per-VM, we'll have to broadcast this EOI to all of the I/O APICs.
     */
    PCPDMIOAPIC pIoApic = &pVM->pdm.s.IoApic;
#ifdef IN_RING0
    if (pIoApic->pDevInsR0)
    {
        Assert(pIoApic->pfnSetEoiR0);
        pIoApic->pfnSetEoiR0(pIoApic->pDevInsR0, uVector);
    }
    else if (pIoApic->pDevInsR3)
    {
        /* Queue for ring-3 execution. */
        PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)PDMQueueAlloc(pVM, pVM->pdm.s.hDevHlpQueue, pVM);
        if (pTask)
        {
            pTask->enmOp = PDMDEVHLPTASKOP_IOAPIC_SET_EOI;
            pTask->pDevInsR3 = NIL_RTR3PTR; /* not required */
            pTask->u.IoApicSetEoi.uVector = uVector;
            PDMQueueInsert(pVM, pVM->pdm.s.hDevHlpQueue, pVM, &pTask->Core);
        }
        else
            AssertMsgFailed(("We're out of devhlp queue items!!!\n"));
    }
#else
    if (pIoApic->pDevInsR3)
    {
        Assert(pIoApic->pfnSetEoiR3);
        pIoApic->pfnSetEoiR3(pIoApic->pDevInsR3, uVector);
    }
#endif
}


/**
 * Send a MSI to an I/O APIC.
 *
 * @param   pVM         The cross context VM structure.
 * @param   uBusDevFn   The bus:device:function of the device initiating the MSI.
 * @param   pMsi        The MSI to send.
 * @param   uTagSrc     The IRQ tag and source tracer ID.
 */
VMM_INT_DECL(void) PDMIoApicSendMsi(PVMCC pVM, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    Log9(("PDMIoApicSendMsi: addr=%#RX64 data=%#RX32 tag=%#x src=%#x\n", pMsi->Addr.u64, pMsi->Data.u32, uTagSrc, uBusDevFn));
#ifdef VBOX_VMM_TARGET_ARMV8
    NOREF(uBusDevFn);
    PCPDMGICBACKEND pGic = &pVM->pdm.s.Ic.u.armv8.GicBackend;
    if (pGic->pfnSendMsi)
        pGic->pfnSendMsi(pVM, uBusDevFn, pMsi, uTagSrc);
#else
    PCPDMIOAPIC pIoApic = &pVM->pdm.s.IoApic;
# ifdef IN_RING0
    if (pIoApic->pDevInsR0)
        pIoApic->pfnSendMsiR0(pIoApic->pDevInsR0, uBusDevFn, pMsi, uTagSrc);
    else if (pIoApic->pDevInsR3)
    {
        /* Queue for ring-3 execution. */
        PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)PDMQueueAlloc(pVM, pVM->pdm.s.hDevHlpQueue, pVM);
        if (pTask)
        {
            pTask->enmOp = PDMDEVHLPTASKOP_IOAPIC_SEND_MSI;
            pTask->pDevInsR3 = NIL_RTR3PTR; /* not required */
            pTask->u.IoApicSendMsi.uBusDevFn = uBusDevFn;
            pTask->u.IoApicSendMsi.Msi       = *pMsi;
            pTask->u.IoApicSendMsi.uTagSrc   = uTagSrc;
            PDMQueueInsert(pVM, pVM->pdm.s.hDevHlpQueue, pVM, &pTask->Core);
        }
        else
            AssertMsgFailed(("We're out of devhlp queue items!!!\n"));
    }
# else
    if (pIoApic->pDevInsR3)
    {
        Assert(pIoApic->pfnSendMsiR3);
        pIoApic->pfnSendMsiR3(pIoApic->pDevInsR3, uBusDevFn, pMsi, uTagSrc);
    }
# endif
#endif
}



/**
 * Returns the presence of an IO-APIC.
 *
 * @returns true if an IO-APIC is present.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(bool) PDMHasIoApic(PVM pVM)
{
    return pVM->pdm.s.IoApic.pDevInsR3 != NULL;
}


/**
 * Returns the presence of an APIC.
 *
 * @returns true if an APIC is present.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(bool) PDMHasApic(PVM pVM)
{
    return pVM->pdm.s.Ic.pDevInsR3 != NIL_RTR3PTR;
}


/**
 * Translates a ring-0 device instance index to a pointer.
 *
 * This is used by PGM for device access handlers.
 *
 * @returns Device instance pointer if valid index, otherwise NULL (asserted).
 * @param   pVM         The cross context VM structure.
 * @param   idxR0Device The ring-0 device instance index.
 */
VMM_INT_DECL(PPDMDEVINS) PDMDeviceRing0IdxToInstance(PVMCC pVM, uint64_t idxR0Device)
{
#ifdef IN_RING0
    AssertMsgReturn(idxR0Device < RT_ELEMENTS(pVM->pdmr0.s.apDevInstances), ("%#RX64\n", idxR0Device), NULL);
    PPDMDEVINS pDevIns = pVM->pdmr0.s.apDevInstances[idxR0Device];
#elif defined(IN_RING3)
    AssertMsgReturn(idxR0Device < RT_ELEMENTS(pVM->pdm.s.apDevRing0Instances), ("%#RX64\n", idxR0Device), NULL);
    PPDMDEVINS pDevIns = pVM->pdm.s.apDevRing0Instances[idxR0Device];
#else
# error "Unsupported context"
#endif
    AssertMsg(pDevIns, ("%#RX64\n", idxR0Device));
    return pDevIns;
}


/**
 * Locks PDM.
 *
 * This might block.
 *
 * @param   pVM     The cross context VM structure.
 */
void pdmLock(PVMCC pVM)
{
    int rc = PDMCritSectEnter(pVM, &pVM->pdm.s.CritSect, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC(pVM, &pVM->pdm.s.CritSect, rc);
}


/**
 * Locks PDM but don't go to ring-3 if it's owned by someone.
 *
 * @returns VINF_SUCCESS on success.
 * @returns rc if we're in GC or R0 and can't get the lock.
 * @param   pVM     The cross context VM structure.
 * @param   rcBusy  The RC to return in GC or R0 when we can't get the lock.
 */
int pdmLockEx(PVMCC pVM, int rcBusy)
{
    return PDMCritSectEnter(pVM, &pVM->pdm.s.CritSect, rcBusy);
}


/**
 * Unlocks PDM.
 *
 * @param   pVM     The cross context VM structure.
 */
void pdmUnlock(PVMCC pVM)
{
    PDMCritSectLeave(pVM, &pVM->pdm.s.CritSect);
}


/**
 * Checks if this thread is owning the PDM lock.
 *
 * @returns @c true if the lock is taken, @c false otherwise.
 * @param   pVM     The cross context VM structure.
 */
bool pdmLockIsOwner(PVMCC pVM)
{
    return PDMCritSectIsOwner(pVM, &pVM->pdm.s.CritSect);
}


/**
 * Converts ring 3 VMM heap pointer to a guest physical address
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pv              Ring-3 pointer.
 * @param   pGCPhys         GC phys address (out).
 */
VMM_INT_DECL(int) PDMVmmDevHeapR3ToGCPhys(PVM pVM, RTR3PTR pv, RTGCPHYS *pGCPhys)
{
    if (RT_LIKELY(pVM->pdm.s.GCPhysVMMDevHeap != NIL_RTGCPHYS))
    {
        RTR3UINTPTR const offHeap = (RTR3UINTPTR)pv - (RTR3UINTPTR)pVM->pdm.s.pvVMMDevHeap;
        if (RT_LIKELY(offHeap < pVM->pdm.s.cbVMMDevHeap))
        {
            *pGCPhys = pVM->pdm.s.GCPhysVMMDevHeap + offHeap;
            return VINF_SUCCESS;
        }

        /* Don't assert here as this is called before we can catch ring-0 assertions. */
        Log(("PDMVmmDevHeapR3ToGCPhys: pv=%p pvVMMDevHeap=%p cbVMMDevHeap=%#x\n",
             pv, pVM->pdm.s.pvVMMDevHeap, pVM->pdm.s.cbVMMDevHeap));
    }
    else
        Log(("PDMVmmDevHeapR3ToGCPhys: GCPhysVMMDevHeap=%RGp (pv=%p)\n", pVM->pdm.s.GCPhysVMMDevHeap, pv));
    return VERR_PDM_DEV_HEAP_R3_TO_GCPHYS;
}


/**
 * Checks if the vmm device heap is enabled (== vmm device's pci region mapped)
 *
 * @returns dev heap enabled status (true/false)
 * @param   pVM             The cross context VM structure.
 */
VMM_INT_DECL(bool) PDMVmmDevHeapIsEnabled(PVM pVM)
{
    return pVM->pdm.s.GCPhysVMMDevHeap != NIL_RTGCPHYS;
}
