/* $Id: GICR3Nem-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GICv3) - KVM in kernel interface.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_APIC
#include <VBox/log.h>
#include "GICInternal.h"
#include "NEMInternal.h" /* Need access to the VM file descriptor. */
#include <VBox/vmm/gic.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>

#include <iprt/armv8.h>

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <linux/kvm.h>


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * GICKvm PDM instance data (per-VM).
 */
typedef struct GICKVMDEV
{
    /** Pointer to the PDM device instance. */
    PPDMDEVINSR3        pDevIns;
    /** The GIC device file descriptor. */
    int                 fdGic;
    /** The VM file descriptor (for KVM_IRQ_LINE). */
    int                 fdKvmVm;
} GICKVMDEV;
/** Pointer to a GIC KVM device. */
typedef GICKVMDEV *PGICKVMDEV;
/** Pointer to a const GIC KVM device. */
typedef GICKVMDEV const *PCGICKVMDEV;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if 0
/**
 * System register ranges for the GICv3.
 */
static CPUMSYSREGRANGE const g_aSysRegRanges_GICv3[] =
{
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_PMR_EL1,   ARMV8_AARCH64_SYSREG_ICC_PMR_EL1,     "ICC_PMR_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1,  ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1,   "ICC_IAR0_EL1 - ICC_AP0R3_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1, ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1,  "ICC_AP1R0_EL1 - ICC_NMIAR1_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_DIR_EL1,   ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1,   "ICC_DIR_EL1 - ICC_SGI0R_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1,  ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1, "ICC_IAR1_EL1 - ICC_IGRPEN1_EL1"),
};
#endif


/**
 * Common worker for GICR3KvmSpiSet() and GICR3KvmPpiSet().
 *
 * @returns VBox status code.
 * @param   pDevIns     The PDM KVM GIC device instance.
 * @param   idCpu       The CPU ID for which the interrupt is updated (only valid for PPIs).
 * @param   u32IrqType  The actual IRQ type (PPI or SPI).
 * @param   uIntId      The interrupt ID to update.
 * @param   fAsserted   Flag whether the interrupt is asserted (true) or not (false).
 */
static int gicR3KvmSetIrq(PPDMDEVINS pDevIns, VMCPUID idCpu, uint32_t u32IrqType, uint32_t uIntId, bool fAsserted)
{
    LogFlowFunc(("pDevIns=%p idCpu=%u u32IrqType=%#x uIntId=%u fAsserted=%RTbool\n",
                 pDevIns, idCpu, u32IrqType, uIntId, fAsserted));

    PGICKVMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PGICKVMDEV);

    struct kvm_irq_level IrqLvl;
    IrqLvl.irq   =   (u32IrqType << KVM_ARM_IRQ_TYPE_SHIFT)
                   | (idCpu & KVM_ARM_IRQ_VCPU_MASK) << KVM_ARM_IRQ_VCPU_SHIFT
                   | ((idCpu >> 8) & KVM_ARM_IRQ_VCPU2_MASK) << KVM_ARM_IRQ_VCPU2_SHIFT
                   | (uIntId & KVM_ARM_IRQ_NUM_MASK) << KVM_ARM_IRQ_NUM_SHIFT;
    IrqLvl.level = fAsserted ? 1 : 0;
    int rcLnx = ioctl(pThis->fdKvmVm, KVM_IRQ_LINE, &IrqLvl);
    AssertReturn(rcLnx == 0, RTErrConvertFromErrno(errno));

    return VINF_SUCCESS;
}


/**
 * Sets the given SPI inside the in-kernel KVM GIC.
 *
 * @returns VBox status code.
 * @param   pVM         The VM instance.
 * @param   uIntId      The SPI ID to update.
 * @param   fAsserted   Flag whether the interrupt is asserted (true) or not (false).
 */
VMMR3_INT_DECL(int) GICR3NemSpiSet(PVMCC pVM, uint32_t uIntId, bool fAsserted)
{
    PGIC pGic = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);

    /* idCpu is ignored for SPI interrupts. */
    return gicR3KvmSetIrq(pDevIns, 0 /*idCpu*/, KVM_ARM_IRQ_TYPE_SPI,
                          uIntId + GIC_INTID_RANGE_SPI_START, fAsserted);
}


/**
 * Sets the given PPI inside the in-kernel KVM GIC.
 *
 * @returns VBox status code.
 * @param   pVCpu       The vCPU for whih the PPI state is updated.
 * @param   uIntId      The PPI ID to update.
 * @param   fAsserted   Flag whether the interrupt is asserted (true) or not (false).
 */
VMMR3_INT_DECL(int) GICR3NemPpiSet(PVMCPUCC pVCpu, uint32_t uIntId, bool fAsserted)
{
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);

    return gicR3KvmSetIrq(pDevIns, pVCpu->idCpu, KVM_ARM_IRQ_TYPE_SPI,
                          uIntId + GIC_INTID_RANGE_PPI_START, fAsserted);
}


/**
 * Sets the given device attribute in KVM to the given value.
 *
 * @returns VBox status code.
 * @param   pThis           The KVM GIC device instance.
 * @param   u32Grp          The device attribute group being set.
 * @param   u32Attr         The actual attribute inside the group being set.
 * @param   pvAttrVal       Where the attribute value to set.
 * @param   pszAttribute    Attribute description for logging.
 */
static int gicR3KvmSetDevAttribute(PGICKVMDEV pThis, uint32_t u32Grp, uint32_t u32Attr, const void *pvAttrVal, const char *pszAttribute)
{
    struct kvm_device_attr DevAttr;

    DevAttr.flags = 0;
    DevAttr.group = u32Grp;
    DevAttr.attr  = u32Attr;
    DevAttr.addr  = (uintptr_t)pvAttrVal;
    int rcLnx = ioctl(pThis->fdGic, KVM_HAS_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("KVM error: The in-kernel VGICv3 device doesn't support setting the attribute \"%s\" (%d)"),
                                   pszAttribute, errno);

    rcLnx = ioctl(pThis->fdGic, KVM_SET_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("KVM error: Setting the attribute \"%s\" for the in-kernel GICv3 failed (%d)"),
                                   pszAttribute, errno);

    return VINF_SUCCESS;
}


/**
 * Queries the value of the given device attribute from KVM.
 *
 * @returns VBox status code.
 * @param   pThis           The KVM GIC device instance.
 * @param   u32Grp          The device attribute group being queried.
 * @param   u32Attr         The actual attribute inside the group being queried.
 * @param   pvAttrVal       Where the attribute value should be stored upon success.
 * @param   pszAttribute    Attribute description for logging.
 */
static int gicR3KvmQueryDevAttribute(PGICKVMDEV pThis, uint32_t u32Grp, uint32_t u32Attr, void *pvAttrVal, const char *pszAttribute)
{
    struct kvm_device_attr DevAttr;

    DevAttr.flags = 0;
    DevAttr.group = u32Grp;
    DevAttr.attr  = u32Attr;
    DevAttr.addr  = (uintptr_t)pvAttrVal;
    int rcLnx = ioctl(pThis->fdGic, KVM_GET_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("KVM error: Failed to query attribute \"%s\" from the in-kernel VGICv3 (%d)"),
                                   pszAttribute, errno);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) gicR3KvmReset(PPDMDEVINS pDevIns)
{
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    RT_NOREF(pVM);

    LogFlow(("GIC: gicR3KvmReset\n"));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) gicR3KvmDestruct(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    PGICKVMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PGICKVMDEV);

    close(pThis->fdGic);
    pThis->fdGic = 0;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) gicR3KvmConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PGICKVMDEV      pThis    = PDMDEVINS_2_DATA(pDevIns, PGICKVMDEV);
    PCPDMDEVHLPR3   pHlp     = pDevIns->pHlpR3;
    PVM             pVM      = PDMDevHlpGetVM(pDevIns);
    PGIC            pGic     = VM_TO_GIC(pVM);
    Assert(iInstance == 0); NOREF(iInstance);

    /*
     * Init the data.
     */
    pGic->pDevInsR3 = pDevIns;
    pGic->fNemGic   = true;
    pThis->pDevIns  = pDevIns;
    pThis->fdKvmVm  = pVM->nem.s.fdVm;

    /*
     * Validate GIC settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "DistributorMmioBase|RedistributorMmioBase", "");

    /*
     * Disable automatic PDM locking for this device.
     */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the GIC with PDM.
     */
    rc = PDMDevHlpApicRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Query the MMIO ranges.
     */
    RTGCPHYS GCPhysMmioBaseDist = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "DistributorMmioBase", &GCPhysMmioBaseDist);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"DistributorMmioBase\" value"));

    RTGCPHYS GCPhysMmioBaseReDist = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "RedistributorMmioBase", &GCPhysMmioBaseReDist);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"RedistributorMmioBase\" value"));

    /*
     * Create the device.
     */
    struct kvm_create_device DevCreate;
    DevCreate.type  = KVM_DEV_TYPE_ARM_VGIC_V3;
    DevCreate.fd    = 0;
    DevCreate.flags = 0;
    int rcLnx = ioctl(pThis->fdKvmVm, KVM_CREATE_DEVICE, &DevCreate);
    if (rcLnx < 0)
        return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("KVM error: Creating the in-kernel VGICv3 device failed (%d)"), errno);

    pThis->fdGic = DevCreate.fd;

    /*
     * Set the distributor and re-distributor base.
     */
    rc = gicR3KvmSetDevAttribute(pThis, KVM_DEV_ARM_VGIC_GRP_ADDR, KVM_VGIC_V3_ADDR_TYPE_DIST, &GCPhysMmioBaseDist,
                                 "Distributor MMIO base");
    AssertRCReturn(rc, rc);

    rc = gicR3KvmSetDevAttribute(pThis, KVM_DEV_ARM_VGIC_GRP_ADDR, KVM_VGIC_V3_ADDR_TYPE_REDIST, &GCPhysMmioBaseReDist,
                                 "Re-Distributor MMIO base");
    AssertRCReturn(rc, rc);

    /* Query and log the number of IRQ lines this GIC supports. */
    uint32_t cIrqs = 0;
    rc = gicR3KvmQueryDevAttribute(pThis, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0, &cIrqs,
                                   "IRQ line count");
    AssertRCReturn(rc, rc);
    LogRel(("GICR3Kvm: Supports %u IRQs\n", cIrqs));

    /*
     * Init the controller.
     */
    rc = gicR3KvmSetDevAttribute(pThis, KVM_DEV_ARM_VGIC_GRP_CTRL, KVM_DEV_ARM_VGIC_CTRL_INIT, NULL,
                                 "VGIC init");
    AssertRCReturn(rc, rc);

    gicR3Reset(pDevIns);
    return VINF_SUCCESS;
}


/**
 * GIC device registration structure.
 */
const PDMDEVREG g_DeviceGICNem =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "gic-nem",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(GICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Generic Interrupt Controller",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           gicR3KvmConstruct,
    /* .pfnDestruct = */            gicR3KvmDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               gicR3KvmReset,
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

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

