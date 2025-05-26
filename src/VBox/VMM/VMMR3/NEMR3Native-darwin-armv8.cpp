/* $Id: NEMR3Native-darwin-armv8.cpp 168982 2025-05-26 05:42:12Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * NEM - Native execution manager, native ring-3 macOS backend using Hypervisor.framework, ARMv8 variant.
 *
 * Log group 2: Exit logging.
 * Log group 3: Log context on exit.
 * Log group 5: Ring-3 memory management
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/dbgftrace.h>
#include <VBox/vmm/gcm.h>
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/dis.h>
#include <VBox/gic.h>
#include "dtrace/VBoxVMM.h"

#include <iprt/armv8.h>
#include <iprt/asm.h>
#include <iprt/asm-arm.h>
#include <iprt/asm-math.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#include <iprt/formats/arm-psci.h>

#include <mach/mach_time.h>
#include <mach/kern_return.h>

#include <Hypervisor/Hypervisor.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#if MAC_OS_X_VERSION_MIN_REQUIRED < 150000

/* Since 15.0+ */
typedef enum hv_gic_distributor_reg_t : uint16_t
{
    HV_GIC_DISTRIBUTOR_REG_GICD_CTLR,
    HV_GIC_DISTRIBUTOR_REG_GICD_ICACTIVER0
    /** @todo */
} hv_gic_distributor_reg_t;


typedef enum hv_gic_icc_reg_t : uint16_t
{
    HV_GIC_ICC_REG_PMR_EL1,
    HV_GIC_ICC_REG_BPR0_EL1,
    HV_GIC_ICC_REG_AP0R0_EL1,
    HV_GIC_ICC_REG_AP1R0_EL1,
    HV_GIC_ICC_REG_RPR_EL1,
    HV_GIC_ICC_REG_BPR1_EL1,
    HV_GIC_ICC_REG_CTLR_EL1,
    HV_GIC_ICC_REG_SRE_EL1,
    HV_GIC_ICC_REG_IGRPEN0_EL1,
    HV_GIC_ICC_REG_IGRPEN1_EL1,
    HV_GIC_ICC_REG_INVALID,
    /** @todo */
} hv_gic_icc_reg_t;


typedef enum hv_gic_ich_reg_t : uint16_t
{
    HV_GIC_ICH_REG_AP0R0_EL2
    /** @todo */
} hv_gic_ich_reg_t;


typedef enum hv_gic_icv_reg_t : uint16_t
{
    HV_GIC_ICV_REG_AP0R0_EL1
    /** @todo */
} hv_gic_icv_reg_t;


typedef enum hv_gic_msi_reg_t : uint16_t
{
    HV_GIC_REG_GICM_SET_SPI_NSR
    /** @todo */
} hv_gic_msi_reg_t;


typedef enum hv_gic_redistributor_reg_t : uint16_t
{
    HV_GIC_REDISTRIBUTOR_REG_GICR_ICACTIVER0
    /** @todo */
} hv_gic_redistributor_reg_t;


typedef enum hv_gic_intid_t : uint16_t
{
    HV_GIC_INT_EL1_PHYSICAL_TIMER  = 23,
    HV_GIC_INT_EL1_VIRTUAL_TIMER   = 25,
    HV_GIC_INT_EL2_PHYSICAL_TIMER  = 26,
    HV_GIC_INT_MAINTENANCE         = 27,
    HV_GIC_INT_PERFORMANCE_MONITOR = 30
} hv_gic_intid_t;

# define HV_SYS_REG_ACTLR_EL1   (hv_sys_reg_t)0xc081

#else
# define HV_GIC_ICC_REG_INVALID (hv_gic_icc_reg_t)UINT16_MAX
#endif

typedef hv_vm_config_t  FN_HV_VM_CONFIG_CREATE(void);
typedef hv_return_t     FN_HV_VM_CONFIG_GET_EL2_SUPPORTED(bool *el2_supported);
typedef hv_return_t     FN_HV_VM_CONFIG_GET_EL2_ENABLED(hv_vm_config_t config, bool *el2_enabled);
typedef hv_return_t     FN_HV_VM_CONFIG_SET_EL2_ENABLED(hv_vm_config_t config, bool el2_enabled);

typedef struct hv_gic_config_s *hv_gic_config_t;
typedef hv_return_t     FN_HV_GIC_CREATE(hv_gic_config_t gic_config);
typedef hv_return_t     FN_HV_GIC_RESET(void);
typedef hv_gic_config_t FN_HV_GIC_CONFIG_CREATE(void);
typedef hv_return_t     FN_HV_GIC_CONFIG_SET_DISTRIBUTOR_BASE(hv_gic_config_t config, hv_ipa_t distributor_base_address);
typedef hv_return_t     FN_HV_GIC_CONFIG_SET_REDISTRIBUTOR_BASE(hv_gic_config_t config, hv_ipa_t redistributor_base_address);
typedef hv_return_t     FN_HV_GIC_CONFIG_SET_MSI_REGION_BASE(hv_gic_config_t config, hv_ipa_t msi_region_base_address);
typedef hv_return_t     FN_HV_GIC_CONFIG_SET_MSI_INTERRUPT_RANGE(hv_gic_config_t config, uint32_t msi_intid_base, uint32_t msi_intid_count);

typedef hv_return_t     FN_HV_GIC_GET_REDISTRIBUTOR_BASE(hv_vcpu_t vcpu, hv_ipa_t *redistributor_base_address);
typedef hv_return_t     FN_HV_GIC_GET_REDISTRIBUTOR_REGION_SIZE(size_t *redistributor_region_size);
typedef hv_return_t     FN_HV_GIC_GET_REDISTRIBUTOR_SIZE(size_t *redistributor_size);
typedef hv_return_t     FN_HV_GIC_GET_DISTRIBUTOR_SIZE(size_t *distributor_size);
typedef hv_return_t     FN_HV_GIC_GET_DISTRIBUTOR_BASE_ALIGNMENT(size_t *distributor_base_alignment);
typedef hv_return_t     FN_HV_GIC_GET_REDISTRIBUTOR_BASE_ALIGNMENT(size_t *redistributor_base_alignment);
typedef hv_return_t     FN_HV_GIC_GET_MSI_REGION_BASE_ALIGNMENT(size_t *msi_region_base_alignment);
typedef hv_return_t     FN_HV_GIC_GET_MSI_REGION_SIZE(size_t *msi_region_size);
typedef hv_return_t     FN_HV_GIC_GET_SPI_INTERRUPT_RANGE(uint32_t *spi_intid_base, uint32_t *spi_intid_count);

typedef struct hv_gic_state_s *hv_gic_state_t;
typedef hv_gic_state_t  FN_HV_GIC_STATE_CREATE(void);
typedef hv_return_t     FN_HV_GIC_SET_STATE(const void *gic_state_data, size_t gic_state_size);
typedef hv_return_t     FN_HV_GIC_STATE_GET_SIZE(hv_gic_state_t state, size_t *gic_state_size);
typedef hv_return_t     FN_HV_GIC_STATE_GET_DATA(hv_gic_state_t state, void *gic_state_data);

typedef hv_return_t     FN_HV_GIC_SEND_MSI(hv_ipa_t address, uint32_t intid);
typedef hv_return_t     FN_HV_GIC_SET_SPI(uint32_t intid, bool level);

typedef hv_return_t     FN_HV_GIC_GET_DISTRIBUTOR_REG(hv_gic_distributor_reg_t reg, uint64_t *value);
typedef hv_return_t     FN_HV_GIC_GET_MSI_REG(hv_gic_msi_reg_t reg, uint64_t *value);
typedef hv_return_t     FN_HV_GIC_GET_ICC_REG(hv_vcpu_t vcpu, hv_gic_icc_reg_t reg, uint64_t *value);
typedef hv_return_t     FN_HV_GIC_GET_ICH_REG(hv_vcpu_t vcpu, hv_gic_ich_reg_t reg, uint64_t *value);
typedef hv_return_t     FN_HV_GIC_GET_ICV_REG(hv_vcpu_t vcpu, hv_gic_icv_reg_t reg, uint64_t *value);
typedef hv_return_t     FN_HV_GIC_GET_REDISTRIBUTOR_REG(hv_vcpu_t vcpu, hv_gic_redistributor_reg_t reg, uint64_t *value);

typedef hv_return_t     FN_HV_GIC_SET_DISTRIBUTOR_REG(hv_gic_distributor_reg_t reg, uint64_t value);
typedef hv_return_t     FN_HV_GIC_SET_MSI_REG(hv_gic_msi_reg_t reg, uint64_t value);
typedef hv_return_t     FN_HV_GIC_SET_ICC_REG(hv_vcpu_t vcpu, hv_gic_icc_reg_t reg, uint64_t value);
typedef hv_return_t     FN_HV_GIC_SET_ICH_REG(hv_vcpu_t vcpu, hv_gic_ich_reg_t reg, uint64_t value);
typedef hv_return_t     FN_HV_GIC_SET_ICV_REG(hv_vcpu_t vcpu, hv_gic_icv_reg_t reg, uint64_t value);
typedef hv_return_t     FN_HV_GIC_SET_REDISTRIBUTOR_REG(hv_vcpu_t vcpu, hv_gic_redistributor_reg_t reg, uint64_t value);

typedef hv_return_t     FN_HV_GIC_GET_INTID(hv_gic_intid_t interrupt, uint32_t *intid);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Optional APIs imported from Hypervisor.framework.
 * @{ */
static FN_HV_VM_CONFIG_CREATE                       *g_pfnHvVmConfigCreate                      = NULL; /* Since 13.0 */
static FN_HV_VM_CONFIG_GET_EL2_SUPPORTED            *g_pfnHvVmConfigGetEl2Supported             = NULL; /* Since 15.0 */
static FN_HV_VM_CONFIG_GET_EL2_ENABLED              *g_pfnHvVmConfigGetEl2Enabled               = NULL; /* Since 15.0 */
static FN_HV_VM_CONFIG_SET_EL2_ENABLED              *g_pfnHvVmConfigSetEl2Enabled               = NULL; /* Since 15.0 */

static FN_HV_GIC_CREATE                             *g_pfnHvGicCreate                           = NULL; /* Since 15.0 */
static FN_HV_GIC_RESET                              *g_pfnHvGicReset                            = NULL; /* Since 15.0 */
static FN_HV_GIC_CONFIG_CREATE                      *g_pfnHvGicConfigCreate                     = NULL; /* Since 15.0 */
static FN_HV_GIC_CONFIG_SET_DISTRIBUTOR_BASE        *g_pfnHvGicConfigSetDistributorBase         = NULL; /* Since 15.0 */
static FN_HV_GIC_CONFIG_SET_REDISTRIBUTOR_BASE      *g_pfnHvGicConfigSetRedistributorBase       = NULL; /* Since 15.0 */
static FN_HV_GIC_CONFIG_SET_MSI_REGION_BASE         *g_pfnHvGicConfigSetMsiRegionBase           = NULL; /* Since 15.0 */
static FN_HV_GIC_CONFIG_SET_MSI_INTERRUPT_RANGE     *g_pfnHvGicConfigSetMsiInterruptRange       = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_REDISTRIBUTOR_BASE             *g_pfnHvGicGetRedistributorBase             = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_REDISTRIBUTOR_REGION_SIZE      *g_pfnHvGicGetRedistributorRegionSize       = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_REDISTRIBUTOR_SIZE             *g_pfnHvGicGetRedistributorSize             = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_DISTRIBUTOR_SIZE               *g_pfnHvGicGetDistributorSize               = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_DISTRIBUTOR_BASE_ALIGNMENT     *g_pfnHvGicGetDistributorBaseAlignment      = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_REDISTRIBUTOR_BASE_ALIGNMENT   *g_pfnHvGicGetRedistributorBaseAlignment    = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_MSI_REGION_BASE_ALIGNMENT      *g_pfnHvGicGetMsiRegionBaseAlignment        = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_MSI_REGION_SIZE                *g_pfnHvGicGetMsiRegionSize                 = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_SPI_INTERRUPT_RANGE            *g_pfnHvGicGetSpiInterruptRange             = NULL; /* Since 15.0 */
static FN_HV_GIC_STATE_CREATE                       *g_pfnHvGicStateCreate                      = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_STATE                          *g_pfnHvGicSetState                         = NULL; /* Since 15.0 */
static FN_HV_GIC_STATE_GET_SIZE                     *g_pfnHvGicStateGetSize                     = NULL; /* Since 15.0 */
static FN_HV_GIC_STATE_GET_DATA                     *g_pfnHvGicStateGetData                     = NULL; /* Since 15.0 */
static FN_HV_GIC_SEND_MSI                           *g_pfnHvGicSendMsi                          = NULL; /* Since 15.0 */
       FN_HV_GIC_SET_SPI                            *g_pfnHvGicSetSpi                           = NULL; /* Since 15.0, exported for GICR3Nem-darwin.cpp */
static FN_HV_GIC_GET_DISTRIBUTOR_REG                *g_pfnHvGicGetDistributorReg                = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_MSI_REG                        *g_pfnHvGicGetMsiReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_ICC_REG                        *g_pfnHvGicGetIccReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_ICH_REG                        *g_pfnHvGicGetIchReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_ICV_REG                        *g_pfnHvGicGetIcvReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_REDISTRIBUTOR_REG              *g_pfnHvGicGetRedistributorReg              = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_DISTRIBUTOR_REG                *g_pfnHvGicSetDistributorReg                = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_MSI_REG                        *g_pfnHvGicSetMsiReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_ICC_REG                        *g_pfnHvGicSetIccReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_ICH_REG                        *g_pfnHvGicSetIchReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_ICV_REG                        *g_pfnHvGicSetIcvReg                        = NULL; /* Since 15.0 */
static FN_HV_GIC_SET_REDISTRIBUTOR_REG              *g_pfnHvGicSetRedistributorReg              = NULL; /* Since 15.0 */
static FN_HV_GIC_GET_INTID                          *g_pfnHvGicGetIntid                         = NULL; /* Since 15.0 */
/** @} */


/**
 * Import instructions.
 */
static const struct
{
    void        **ppfn;     /**< The function pointer variable. */
    const char  *pszName;   /**< The function name. */
} g_aImports[] =
{
#define NEM_DARWIN_IMPORT(a_Pfn, a_Name) { (void **)&(a_Pfn), #a_Name }
    NEM_DARWIN_IMPORT(g_pfnHvVmConfigCreate,                    hv_vm_config_create),
    NEM_DARWIN_IMPORT(g_pfnHvVmConfigGetEl2Supported,           hv_vm_config_get_el2_supported),
    NEM_DARWIN_IMPORT(g_pfnHvVmConfigGetEl2Enabled,             hv_vm_config_get_el2_enabled),
    NEM_DARWIN_IMPORT(g_pfnHvVmConfigSetEl2Enabled,             hv_vm_config_set_el2_enabled),

    NEM_DARWIN_IMPORT(g_pfnHvGicCreate,                         hv_gic_create),
    NEM_DARWIN_IMPORT(g_pfnHvGicReset,                          hv_gic_reset),
    NEM_DARWIN_IMPORT(g_pfnHvGicConfigCreate,                   hv_gic_config_create),
    NEM_DARWIN_IMPORT(g_pfnHvGicConfigSetDistributorBase,       hv_gic_config_set_distributor_base),
    NEM_DARWIN_IMPORT(g_pfnHvGicConfigSetRedistributorBase,     hv_gic_config_set_redistributor_base),
    NEM_DARWIN_IMPORT(g_pfnHvGicConfigSetMsiRegionBase,         hv_gic_config_set_msi_region_base),
    NEM_DARWIN_IMPORT(g_pfnHvGicConfigSetMsiInterruptRange,     hv_gic_config_set_msi_interrupt_range),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetRedistributorBase,           hv_gic_get_redistributor_base),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetRedistributorRegionSize,     hv_gic_get_redistributor_region_size),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetRedistributorSize,           hv_gic_get_redistributor_size),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetDistributorSize,             hv_gic_get_distributor_size),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetDistributorBaseAlignment,    hv_gic_get_distributor_base_alignment),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetRedistributorBaseAlignment,  hv_gic_get_redistributor_base_alignment),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetMsiRegionBaseAlignment,      hv_gic_get_msi_region_base_alignment),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetMsiRegionSize,               hv_gic_get_msi_region_size),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetSpiInterruptRange,           hv_gic_get_spi_interrupt_range),
    NEM_DARWIN_IMPORT(g_pfnHvGicStateCreate,                    hv_gic_state_create),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetState,                       hv_gic_set_state),
    NEM_DARWIN_IMPORT(g_pfnHvGicStateGetSize,                   hv_gic_state_get_size),
    NEM_DARWIN_IMPORT(g_pfnHvGicStateGetData,                   hv_gic_state_get_data),
    NEM_DARWIN_IMPORT(g_pfnHvGicSendMsi,                        hv_gic_send_msi),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetSpi,                         hv_gic_set_spi),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetDistributorReg,              hv_gic_get_distributor_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetMsiReg,                      hv_gic_get_msi_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetIccReg,                      hv_gic_get_icc_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetIchReg,                      hv_gic_get_ich_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetIcvReg,                      hv_gic_get_icv_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetRedistributorReg,            hv_gic_get_redistributor_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetDistributorReg,              hv_gic_set_distributor_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetMsiReg,                      hv_gic_set_msi_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetIccReg,                      hv_gic_set_icc_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetIchReg,                      hv_gic_set_ich_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetIcvReg,                      hv_gic_set_icv_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicSetRedistributorReg,            hv_gic_set_redistributor_reg),
    NEM_DARWIN_IMPORT(g_pfnHvGicGetIntid,                       hv_gic_get_intid)
#undef NEM_DARWIN_IMPORT
};


/*
 * Let the preprocessor alias the APIs to import variables for better autocompletion.
 */
#ifndef IN_SLICKEDIT
# define hv_vm_config_create                        g_pfnHvVmConfigCreate
# define hv_vm_config_get_el2_supported             g_pfnHvVmConfigGetEl2Supported
# define hv_vm_config_get_el2_enabled               g_pfnHvVmConfigGetEl2Enabled
# define hv_vm_config_set_el2_enabled               g_pfnHvVmConfigSetEl2Enabled

# define hv_gic_create                              g_pfnHvGicCreate
# define hv_gic_reset                               g_pfnHvGicReset
# define hv_gic_config_create                       g_pfnHvGicConfigCreate
# define hv_gic_config_set_distributor_base         g_pfnHvGicConfigSetDistributorBase
# define hv_gic_config_set_redistributor_base       g_pfnHvGicConfigSetRedistributorBase
# define hv_gic_config_set_msi_region_base          g_pfnHvGicConfigSetMsiRegionBase
# define hv_gic_config_set_msi_interrupt_range      g_pfnHvGicConfigSetMsiInterruptRange
# define hv_gic_get_redistributor_base              g_pfnHvGicGetRedistributorBase
# define hv_gic_get_redistributor_region_size       g_pfnHvGicGetRedistributorRegionSize
# define hv_gic_get_redistributor_size              g_pfnHvGicGetRedistributorSize
# define hv_gic_get_distributor_size                g_pfnHvGicGetDistributorSize
# define hv_gic_get_distributor_base_alignment      g_pfnHvGicGetDistributorBaseAlignment
# define hv_gic_get_redistributor_base_alignment    g_pfnHvGicGetRedistributorBaseAlignment
# define hv_gic_get_msi_region_base_alignment       g_pfnHvGicGetMsiRegionBaseAlignment
# define hv_gic_get_msi_region_size                 g_pfnHvGicGetMsiRegionSize
# define hv_gic_get_spi_interrupt_range             g_pfnHvGicGetSpiInterruptRange
# define hv_gic_state_create                        g_pfnHvGicStateCreate
# define hv_gic_set_state                           g_pfnHvGicSetState
# define hv_gic_state_get_size                      g_pfnHvGicStateGetSize
# define hv_gic_state_get_data                      g_pfnHvGicStateGetData
# define hv_gic_send_msi                            g_pfnHvGicSendMsi
# define hv_gic_set_spi                             g_pfnHvGicSetSpi
# define hv_gic_get_distributor_reg                 g_pfnHvGicGetDistributorReg
# define hv_gic_get_msi_reg                         g_pfnHvGicGetMsiReg
# define hv_gic_get_icc_reg                         g_pfnHvGicGetIccReg
# define hv_gic_get_ich_reg                         g_pfnHvGicGetIchReg
# define hv_gic_get_icv_reg                         g_pfnHvGicGetIcvReg
# define hv_gic_get_redistributor_reg               g_pfnHvGicGetRedistributorReg
# define hv_gic_set_distributor_reg                 g_pfnHvGicSetDistributorReg
# define hv_gic_set_msi_reg                         g_pfnHvGicSetMsiReg
# define hv_gic_set_icc_reg                         g_pfnHvGicSetIccReg
# define hv_gic_set_ich_reg                         g_pfnHvGicSetIchReg
# define hv_gic_set_icv_reg                         g_pfnHvGicSetIcvReg
# define hv_gic_set_redistributor_reg               g_pfnHvGicSetRedistributorReg
# define hv_gic_get_intid                           g_pfnHvGicGetIntid
#endif


/** The general registers. */
static const struct
{
    hv_reg_t    enmHvReg;
    uint32_t    fCpumExtrn;
    uint32_t    offCpumCtx;
} s_aCpumRegs[] =
{
#define CPUM_GREG_EMIT_X0_X3(a_Idx)  { HV_REG_X ## a_Idx, CPUMCTX_EXTRN_X ## a_Idx, RT_UOFFSETOF(CPUMCTX, aGRegs[a_Idx].x) }
#define CPUM_GREG_EMIT_X4_X28(a_Idx) { HV_REG_X ## a_Idx, CPUMCTX_EXTRN_X4_X28, RT_UOFFSETOF(CPUMCTX, aGRegs[a_Idx].x) }
    CPUM_GREG_EMIT_X0_X3(0),
    CPUM_GREG_EMIT_X0_X3(1),
    CPUM_GREG_EMIT_X0_X3(2),
    CPUM_GREG_EMIT_X0_X3(3),
    CPUM_GREG_EMIT_X4_X28(4),
    CPUM_GREG_EMIT_X4_X28(5),
    CPUM_GREG_EMIT_X4_X28(6),
    CPUM_GREG_EMIT_X4_X28(7),
    CPUM_GREG_EMIT_X4_X28(8),
    CPUM_GREG_EMIT_X4_X28(9),
    CPUM_GREG_EMIT_X4_X28(10),
    CPUM_GREG_EMIT_X4_X28(11),
    CPUM_GREG_EMIT_X4_X28(12),
    CPUM_GREG_EMIT_X4_X28(13),
    CPUM_GREG_EMIT_X4_X28(14),
    CPUM_GREG_EMIT_X4_X28(15),
    CPUM_GREG_EMIT_X4_X28(16),
    CPUM_GREG_EMIT_X4_X28(17),
    CPUM_GREG_EMIT_X4_X28(18),
    CPUM_GREG_EMIT_X4_X28(19),
    CPUM_GREG_EMIT_X4_X28(20),
    CPUM_GREG_EMIT_X4_X28(21),
    CPUM_GREG_EMIT_X4_X28(22),
    CPUM_GREG_EMIT_X4_X28(23),
    CPUM_GREG_EMIT_X4_X28(24),
    CPUM_GREG_EMIT_X4_X28(25),
    CPUM_GREG_EMIT_X4_X28(26),
    CPUM_GREG_EMIT_X4_X28(27),
    CPUM_GREG_EMIT_X4_X28(28),
    { HV_REG_FP,   CPUMCTX_EXTRN_FP,   RT_UOFFSETOF(CPUMCTX, aGRegs[29].x) },
    { HV_REG_LR,   CPUMCTX_EXTRN_LR,   RT_UOFFSETOF(CPUMCTX, aGRegs[30].x) },
    { HV_REG_PC,   CPUMCTX_EXTRN_PC,   RT_UOFFSETOF(CPUMCTX, Pc.u64)       },
    { HV_REG_FPCR, CPUMCTX_EXTRN_FPCR, RT_UOFFSETOF(CPUMCTX, fpcr)         },
    { HV_REG_FPSR, CPUMCTX_EXTRN_FPSR, RT_UOFFSETOF(CPUMCTX, fpsr)         }
#undef CPUM_GREG_EMIT_X0_X3
#undef CPUM_GREG_EMIT_X4_X28
};
/** SIMD/FP registers. */
static const struct
{
    hv_simd_fp_reg_t    enmHvReg;
    uint32_t            offCpumCtx;
} s_aCpumFpRegs[] =
{
#define CPUM_VREG_EMIT(a_Idx)  { HV_SIMD_FP_REG_Q ## a_Idx, RT_UOFFSETOF(CPUMCTX, aVRegs[a_Idx].v) }
    CPUM_VREG_EMIT(0),
    CPUM_VREG_EMIT(1),
    CPUM_VREG_EMIT(2),
    CPUM_VREG_EMIT(3),
    CPUM_VREG_EMIT(4),
    CPUM_VREG_EMIT(5),
    CPUM_VREG_EMIT(6),
    CPUM_VREG_EMIT(7),
    CPUM_VREG_EMIT(8),
    CPUM_VREG_EMIT(9),
    CPUM_VREG_EMIT(10),
    CPUM_VREG_EMIT(11),
    CPUM_VREG_EMIT(12),
    CPUM_VREG_EMIT(13),
    CPUM_VREG_EMIT(14),
    CPUM_VREG_EMIT(15),
    CPUM_VREG_EMIT(16),
    CPUM_VREG_EMIT(17),
    CPUM_VREG_EMIT(18),
    CPUM_VREG_EMIT(19),
    CPUM_VREG_EMIT(20),
    CPUM_VREG_EMIT(21),
    CPUM_VREG_EMIT(22),
    CPUM_VREG_EMIT(23),
    CPUM_VREG_EMIT(24),
    CPUM_VREG_EMIT(25),
    CPUM_VREG_EMIT(26),
    CPUM_VREG_EMIT(27),
    CPUM_VREG_EMIT(28),
    CPUM_VREG_EMIT(29),
    CPUM_VREG_EMIT(30),
    CPUM_VREG_EMIT(31)
#undef CPUM_VREG_EMIT
};
/** Debug system registers. */
static const struct
{
    hv_sys_reg_t        enmHvReg;
    uint32_t            offCpumCtx;
} s_aCpumDbgRegs[] =
{
#define CPUM_DBGREG_EMIT(a_BorW, a_Idx) \
    { HV_SYS_REG_DBG ## a_BorW ## CR ## a_Idx ## _EL1, RT_UOFFSETOF(CPUMCTX, a ## a_BorW ## p[a_Idx].Ctrl.u64) }, \
    { HV_SYS_REG_DBG ## a_BorW ## VR ## a_Idx ## _EL1, RT_UOFFSETOF(CPUMCTX, a ## a_BorW ## p[a_Idx].Value.u64) }
    /* Breakpoint registers. */
    CPUM_DBGREG_EMIT(B, 0),
    CPUM_DBGREG_EMIT(B, 1),
    CPUM_DBGREG_EMIT(B, 2),
    CPUM_DBGREG_EMIT(B, 3),
    CPUM_DBGREG_EMIT(B, 4),
    CPUM_DBGREG_EMIT(B, 5),
    CPUM_DBGREG_EMIT(B, 6),
    CPUM_DBGREG_EMIT(B, 7),
    CPUM_DBGREG_EMIT(B, 8),
    CPUM_DBGREG_EMIT(B, 9),
    CPUM_DBGREG_EMIT(B, 10),
    CPUM_DBGREG_EMIT(B, 11),
    CPUM_DBGREG_EMIT(B, 12),
    CPUM_DBGREG_EMIT(B, 13),
    CPUM_DBGREG_EMIT(B, 14),
    CPUM_DBGREG_EMIT(B, 15),
    /* Watchpoint registers. */
    CPUM_DBGREG_EMIT(W, 0),
    CPUM_DBGREG_EMIT(W, 1),
    CPUM_DBGREG_EMIT(W, 2),
    CPUM_DBGREG_EMIT(W, 3),
    CPUM_DBGREG_EMIT(W, 4),
    CPUM_DBGREG_EMIT(W, 5),
    CPUM_DBGREG_EMIT(W, 6),
    CPUM_DBGREG_EMIT(W, 7),
    CPUM_DBGREG_EMIT(W, 8),
    CPUM_DBGREG_EMIT(W, 9),
    CPUM_DBGREG_EMIT(W, 10),
    CPUM_DBGREG_EMIT(W, 11),
    CPUM_DBGREG_EMIT(W, 12),
    CPUM_DBGREG_EMIT(W, 13),
    CPUM_DBGREG_EMIT(W, 14),
    CPUM_DBGREG_EMIT(W, 15),
    { HV_SYS_REG_MDSCR_EL1, RT_UOFFSETOF(CPUMCTX, Mdscr.u64) }
#undef CPUM_DBGREG_EMIT
};
/** PAuth key system registers. */
static const struct
{
    hv_sys_reg_t        enmHvReg;
    uint32_t            offCpumCtx;
} s_aCpumPAuthKeyRegs[] =
{
    { HV_SYS_REG_APDAKEYLO_EL1, RT_UOFFSETOF(CPUMCTX, Apda.Low.u64)  },
    { HV_SYS_REG_APDAKEYHI_EL1, RT_UOFFSETOF(CPUMCTX, Apda.High.u64) },
    { HV_SYS_REG_APDBKEYLO_EL1, RT_UOFFSETOF(CPUMCTX, Apdb.Low.u64)  },
    { HV_SYS_REG_APDBKEYHI_EL1, RT_UOFFSETOF(CPUMCTX, Apdb.High.u64) },
    { HV_SYS_REG_APGAKEYLO_EL1, RT_UOFFSETOF(CPUMCTX, Apga.Low.u64)  },
    { HV_SYS_REG_APGAKEYHI_EL1, RT_UOFFSETOF(CPUMCTX, Apga.High.u64) },
    { HV_SYS_REG_APIAKEYLO_EL1, RT_UOFFSETOF(CPUMCTX, Apia.Low.u64)  },
    { HV_SYS_REG_APIAKEYHI_EL1, RT_UOFFSETOF(CPUMCTX, Apia.High.u64) },
    { HV_SYS_REG_APIBKEYLO_EL1, RT_UOFFSETOF(CPUMCTX, Apib.Low.u64)  },
    { HV_SYS_REG_APIBKEYHI_EL1, RT_UOFFSETOF(CPUMCTX, Apib.High.u64) }
};
/** System registers. */
static const struct
{
    hv_sys_reg_t    enmHvReg;
    uint32_t        fCpumExtrn;
    uint32_t        offCpumCtx;
} s_aCpumSysRegs[] =
{
    { HV_SYS_REG_SP_EL0,            CPUMCTX_EXTRN_SP,               RT_UOFFSETOF(CPUMCTX, aSpReg[0].u64)    },
    { HV_SYS_REG_SP_EL1,            CPUMCTX_EXTRN_SP,               RT_UOFFSETOF(CPUMCTX, aSpReg[1].u64)    },
    { HV_SYS_REG_SPSR_EL1,          CPUMCTX_EXTRN_SPSR,             RT_UOFFSETOF(CPUMCTX, Spsr.u64)         },
    { HV_SYS_REG_ELR_EL1,           CPUMCTX_EXTRN_ELR,              RT_UOFFSETOF(CPUMCTX, Elr.u64)          },
    { HV_SYS_REG_VBAR_EL1,          CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, VBar.u64)         },
    { HV_SYS_REG_AFSR0_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Afsr0.u64)        },
    { HV_SYS_REG_AFSR1_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Afsr1.u64)        },
    { HV_SYS_REG_AMAIR_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Amair.u64)        },
    { HV_SYS_REG_CNTKCTL_EL1,       CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, CntKCtl.u64)      },
    { HV_SYS_REG_CONTEXTIDR_EL1,    CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, ContextIdr.u64)   },
    { HV_SYS_REG_CPACR_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Cpacr.u64)        },
    { HV_SYS_REG_CSSELR_EL1,        CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Csselr.u64)       },
    { HV_SYS_REG_ESR_EL1,           CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Esr.u64)          },
    { HV_SYS_REG_FAR_EL1,           CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Far.u64)          },
    { HV_SYS_REG_MAIR_EL1,          CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Mair.u64)         },
    { HV_SYS_REG_PAR_EL1,           CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Par.u64)          },
    { HV_SYS_REG_TPIDRRO_EL0,       CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, TpIdrRoEl0.u64)   },
    { HV_SYS_REG_TPIDR_EL0,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, aTpIdr[0].u64)    },
    { HV_SYS_REG_TPIDR_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, aTpIdr[1].u64)    },
    { HV_SYS_REG_MDCCINT_EL1,       CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, MDccInt.u64)      }

};
/** Paging registers (CPUMCTX_EXTRN_SCTLR_TCR_TTBR). */
static const struct
{
    hv_sys_reg_t    enmHvReg;
    uint32_t        offCpumCtx;
} s_aCpumSysRegsPg[] =
{
    { HV_SYS_REG_SCTLR_EL1,         RT_UOFFSETOF(CPUMCTX, Sctlr.u64)        },
    { HV_SYS_REG_TCR_EL1,           RT_UOFFSETOF(CPUMCTX, Tcr.u64)          },
    { HV_SYS_REG_TTBR0_EL1,         RT_UOFFSETOF(CPUMCTX, Ttbr0.u64)        },
    { HV_SYS_REG_TTBR1_EL1,         RT_UOFFSETOF(CPUMCTX, Ttbr1.u64)        }
};

/** Additional System registers to sync when on at least macOS Sequioa 15.0. */
static const struct
{
    hv_sys_reg_t    enmHvReg;
    uint32_t        fCpumExtrn;
    uint32_t        offCpumCtx;
} s_aCpumSysRegsSequioa[] =
{
    { HV_SYS_REG_ACTLR_EL1,         CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Actlr.u64)        }
};
/** EL2 support system registers. */
static const struct
{
    uint16_t            idSysReg;
    uint32_t            offCpumCtx;
} s_aCpumEl2SysRegs[] =
{
    { ARMV8_AARCH64_SYSREG_CNTHCTL_EL2,    RT_UOFFSETOF(CPUMCTX, CntHCtlEl2.u64)    },
    { ARMV8_AARCH64_SYSREG_CNTHP_CTL_EL2,  RT_UOFFSETOF(CPUMCTX, CntHpCtlEl2.u64)   },
    { ARMV8_AARCH64_SYSREG_CNTHP_CVAL_EL2, RT_UOFFSETOF(CPUMCTX, CntHpCValEl2.u64)  },
    { ARMV8_AARCH64_SYSREG_CNTHP_TVAL_EL2, RT_UOFFSETOF(CPUMCTX, CntHpTValEl2.u64)  },
    { ARMV8_AARCH64_SYSREG_CNTVOFF_EL2,    RT_UOFFSETOF(CPUMCTX, CntVOffEl2.u64)    },
    { ARMV8_AARCH64_SYSREG_CPTR_EL2,       RT_UOFFSETOF(CPUMCTX, CptrEl2.u64)       },
    { ARMV8_AARCH64_SYSREG_ELR_EL2,        RT_UOFFSETOF(CPUMCTX, ElrEl2.u64)        },
    { ARMV8_AARCH64_SYSREG_ESR_EL2,        RT_UOFFSETOF(CPUMCTX, EsrEl2.u64)        },
    { ARMV8_AARCH64_SYSREG_FAR_EL2,        RT_UOFFSETOF(CPUMCTX, FarEl2.u64)        },
    { ARMV8_AARCH64_SYSREG_HCR_EL2,        RT_UOFFSETOF(CPUMCTX, HcrEl2.u64)        },
    { ARMV8_AARCH64_SYSREG_HPFAR_EL2,      RT_UOFFSETOF(CPUMCTX, HpFarEl2.u64)      },
    { ARMV8_AARCH64_SYSREG_MAIR_EL2,       RT_UOFFSETOF(CPUMCTX, MairEl2.u64)       },
    //{ ARMV8_AARCH64_SYSREG_MDCR_EL2,       RT_UOFFSETOF(CPUMCTX, MdcrEl2.u64)       },
    { ARMV8_AARCH64_SYSREG_SCTLR_EL2,      RT_UOFFSETOF(CPUMCTX, SctlrEl2.u64)      },
    { ARMV8_AARCH64_SYSREG_SPSR_EL2,       RT_UOFFSETOF(CPUMCTX, SpsrEl2.u64)       },
    { ARMV8_AARCH64_SYSREG_SP_EL2,         RT_UOFFSETOF(CPUMCTX, SpEl2.u64)         },
    { ARMV8_AARCH64_SYSREG_TCR_EL2,        RT_UOFFSETOF(CPUMCTX, TcrEl2.u64)        },
    { ARMV8_AARCH64_SYSREG_TPIDR_EL2,      RT_UOFFSETOF(CPUMCTX, TpidrEl2.u64)      },
    { ARMV8_AARCH64_SYSREG_TTBR0_EL2,      RT_UOFFSETOF(CPUMCTX, Ttbr0El2.u64)      },
    { ARMV8_AARCH64_SYSREG_TTBR1_EL2,      RT_UOFFSETOF(CPUMCTX, Ttbr1El2.u64)      },
    { ARMV8_AARCH64_SYSREG_VBAR_EL2,       RT_UOFFSETOF(CPUMCTX, VBarEl2.u64)       },
    { ARMV8_AARCH64_SYSREG_VMPIDR_EL2,     RT_UOFFSETOF(CPUMCTX, VMpidrEl2.u64)     },
    { ARMV8_AARCH64_SYSREG_VPIDR_EL2,      RT_UOFFSETOF(CPUMCTX, VPidrEl2.u64)      },
    { ARMV8_AARCH64_SYSREG_VTCR_EL2,       RT_UOFFSETOF(CPUMCTX, VTcrEl2.u64)       },
    { ARMV8_AARCH64_SYSREG_VTTBR_EL2,      RT_UOFFSETOF(CPUMCTX, VTtbrEl2.u64)      }
};
/** ID registers. */
static const struct
{
    hv_feature_reg_t enmHvReg;
    uint32_t         offIdStruct;
} s_aIdRegs[] =
{
    { HV_FEATURE_REG_ID_AA64DFR0_EL1,       RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Dfr0El1)  },
    { HV_FEATURE_REG_ID_AA64DFR1_EL1,       RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Dfr1El1)  },
    { HV_FEATURE_REG_ID_AA64ISAR0_EL1,      RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Isar0El1) },
    { HV_FEATURE_REG_ID_AA64ISAR1_EL1,      RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Isar1El1) },
    { HV_FEATURE_REG_ID_AA64MMFR0_EL1,      RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Mmfr0El1) },
    { HV_FEATURE_REG_ID_AA64MMFR1_EL1,      RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Mmfr1El1) },
    { HV_FEATURE_REG_ID_AA64MMFR2_EL1,      RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Mmfr2El1) },
    { HV_FEATURE_REG_ID_AA64PFR0_EL1,       RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Pfr0El1)  },
    { HV_FEATURE_REG_ID_AA64PFR1_EL1,       RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegIdAa64Pfr1El1)  },
    { HV_FEATURE_REG_CLIDR_EL1,             RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegClidrEl1)       },
    { HV_FEATURE_REG_CTR_EL0,               RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegCtrEl0)         },
    { HV_FEATURE_REG_DCZID_EL0,             RT_UOFFSETOF(CPUMARMV8IDREGS, u64RegDczidEl0)       }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Converts a HV return code to a VBox status code.
 *
 * @returns VBox status code.
 * @param   hrc                 The HV return code to convert.
 */
DECLINLINE(int) nemR3DarwinHvSts2Rc(hv_return_t hrc)
{
    if (hrc == HV_SUCCESS)
        return VINF_SUCCESS;

    switch (hrc)
    {
        case HV_ERROR:        return VERR_INVALID_STATE;
        case HV_BUSY:         return VERR_RESOURCE_BUSY;
        case HV_BAD_ARGUMENT: return VERR_INVALID_PARAMETER;
        case HV_NO_RESOURCES: return VERR_OUT_OF_RESOURCES;
        case HV_NO_DEVICE:    return VERR_NOT_FOUND;
        case HV_UNSUPPORTED:  return VERR_NOT_SUPPORTED;
    }

    return VERR_IPE_UNEXPECTED_STATUS;
}


/** Puts a name to a hypervisor framework status code. */
static const char *nemR3DarwinHvStatusName(hv_return_t hrc)
{
    switch (hrc)
    {
        RT_CASE_RET_STR(HV_SUCCESS);
        RT_CASE_RET_STR(HV_ERROR);
        RT_CASE_RET_STR(HV_BUSY);
        RT_CASE_RET_STR(HV_BAD_ARGUMENT);
        RT_CASE_RET_STR(HV_ILLEGAL_GUEST_STATE);
        RT_CASE_RET_STR(HV_NO_RESOURCES);
        RT_CASE_RET_STR(HV_NO_DEVICE);
        RT_CASE_RET_STR(HV_DENIED);
        RT_CASE_RET_STR(HV_UNSUPPORTED);
    }
    return "";
}


#if 0 /* unused right now */
/**
 * Converts an ICC system register into Darwin's Hypervisor.Framework equivalent.
 *
 * @returns HvF's ICC system register.
 * @param   u32Reg      The ARMv8 ICC system register.
 */
static hv_gic_icc_reg_t nemR3DarwinIccRegFromSysReg(uint32_t u32Reg)
{
    switch (u32Reg)
    {
        case ARMV8_AARCH64_SYSREG_ICC_PMR_EL1:      return HV_GIC_ICC_REG_PMR_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1:     return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_EOIR0_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR0_EL1:   return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1:     return HV_GIC_ICC_REG_BPR0_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1:    return HV_GIC_ICC_REG_AP0R0_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1:    return HV_GIC_ICC_REG_AP1R0_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1:   return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_DIR_EL1:      return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_RPR_EL1:      return HV_GIC_ICC_REG_RPR_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_SGI1R_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_ASGI1R_EL1:   return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1:     return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_EOIR1_EL1:    return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1:   return HV_GIC_ICC_REG_INVALID;
        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:     return HV_GIC_ICC_REG_BPR1_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:     return HV_GIC_ICC_REG_CTLR_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_SRE_EL1:      return HV_GIC_ICC_REG_SRE_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:  return HV_GIC_ICC_REG_IGRPEN0_EL1;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:  return HV_GIC_ICC_REG_IGRPEN1_EL1;
    }
    AssertReleaseFailed();
    return HV_GIC_ICC_REG_INVALID;
}
#endif


/**
 * Returns a human readable string of the given exception class.
 *
 * @returns Pointer to the string matching the given EC.
 * @param   u32Ec           The exception class to return the string for.
 */
static const char *nemR3DarwinEsrEl2EcStringify(uint32_t u32Ec)
{
    switch (u32Ec)
    {
#define ARMV8_EC_CASE(a_Ec) case a_Ec: return #a_Ec
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_UNKNOWN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_TRAPPED_WFX);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCR_MRC_COPROC_15);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCRR_MRRC_COPROC15);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCR_MRC_COPROC_14);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_LDC_STC);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_SME_SVE_NEON);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_VMRS);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_PA_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_LS64_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MRRC_COPROC14);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_BTI_BRANCH_TARGET_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_ILLEGAL_EXECUTION_STATE);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_SVC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_HVC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_SMC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_SVC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_HVC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_SMC_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_TRAPPED_SYS_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_SVE_TRAPPED);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_PAUTH_NV_TRAPPED_ERET_ERETAA_ERETAB);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_TME_TSTART_INSN_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_FPAC_PA_INSN_FAILURE_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_SME_TRAPPED_SME_ACCESS);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_RME_GRANULE_PROT_CHECK_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_INSN_ABORT_FROM_LOWER_EL);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_INSN_ABORT_FROM_EL2);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_PC_ALIGNMENT_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_DATA_ABORT_FROM_LOWER_EL);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_DATA_ABORT_FROM_EL2);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_SP_ALIGNMENT_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_MOPS_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_FP_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_TRAPPED_FP_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_SERROR_INTERRUPT);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_BKPT_EXCEPTION_FROM_LOWER_EL);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_BKPT_EXCEPTION_FROM_EL2);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_SS_EXCEPTION_FROM_LOWER_EL);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_SS_EXCEPTION_FROM_EL2);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_WATCHPOINT_EXCEPTION_FROM_LOWER_EL);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_WATCHPOINT_EXCEPTION_FROM_EL2);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_BKPT_INSN);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH32_VEC_CATCH_EXCEPTION);
        ARMV8_EC_CASE(ARMV8_ESR_EL2_EC_AARCH64_BRK_INSN);
#undef ARMV8_EC_CASE
        default:
            break;
    }

    return "<INVALID>";
}


/**
 * Resolves a NEM page state from the given protection flags.
 *
 * @returns NEM page state.
 * @param   fPageProt           The page protection flags.
 */
DECLINLINE(uint8_t) nemR3DarwinPageStateFromProt(uint32_t fPageProt)
{
    switch (fPageProt)
    {
        case NEM_PAGE_PROT_NONE:
            return NEM_DARWIN_PAGE_STATE_UNMAPPED;
        case NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE:
            return NEM_DARWIN_PAGE_STATE_RX;
        case NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE:
            return NEM_DARWIN_PAGE_STATE_RW;
        case NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE | NEM_PAGE_PROT_EXECUTE:
            return NEM_DARWIN_PAGE_STATE_RWX;
        default:
            break;
    }

    AssertLogRelMsgFailed(("Invalid combination of page protection flags %#x, can't map to page state!\n", fPageProt));
    return NEM_DARWIN_PAGE_STATE_UNMAPPED;
}


/**
 * Unmaps the given guest physical address range (page aligned).
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The guest physical address to start unmapping at.
 * @param   cb                  The size of the range to unmap in bytes.
 * @param   pu2State            Where to store the new state of the unmappd page, optional.
 */
DECLINLINE(int) nemR3DarwinUnmap(PVM pVM, RTGCPHYS GCPhys, size_t cb, uint8_t *pu2State)
{
    if (*pu2State <= NEM_DARWIN_PAGE_STATE_UNMAPPED)
    {
        Log5(("nemR3DarwinUnmap: %RGp == unmapped\n", GCPhys));
        *pu2State = NEM_DARWIN_PAGE_STATE_UNMAPPED;
        return VINF_SUCCESS;
    }

    LogFlowFunc(("Unmapping %RGp LB %zu\n", GCPhys, cb));
    hv_return_t hrc = hv_vm_unmap(GCPhys, cb);
    if (RT_LIKELY(hrc == HV_SUCCESS))
    {
        STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
        if (pu2State)
            *pu2State = NEM_DARWIN_PAGE_STATE_UNMAPPED;
        Log5(("nemR3DarwinUnmap: %RGp => unmapped\n", GCPhys));
        return VINF_SUCCESS;
    }

    STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
    LogRel(("nemR3DarwinUnmap(%RGp): failed! hrc=%#x\n",
            GCPhys, hrc));
    return VERR_NEM_IPE_6;
}


/**
 * Maps a given guest physical address range backed by the given memory with the given
 * protection flags.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The guest physical address to start mapping.
 * @param   pvRam               The R3 pointer of the memory to back the range with.
 * @param   cb                  The size of the range, page aligned.
 * @param   fPageProt           The page protection flags to use for this range, combination of NEM_PAGE_PROT_XXX
 * @param   pu2State            Where to store the state for the new page, optional.
 */
DECLINLINE(int) nemR3DarwinMap(PVM pVM, RTGCPHYS GCPhys, const void *pvRam, size_t cb, uint32_t fPageProt, uint8_t *pu2State)
{
    LogFlowFunc(("Mapping %RGp LB %zu fProt=%#x\n", GCPhys, cb, fPageProt));

    Assert(fPageProt != NEM_PAGE_PROT_NONE);
    RT_NOREF(pVM);

    hv_memory_flags_t fHvMemProt = 0;
    if (fPageProt & NEM_PAGE_PROT_READ)
        fHvMemProt |= HV_MEMORY_READ;
    if (fPageProt & NEM_PAGE_PROT_WRITE)
        fHvMemProt |= HV_MEMORY_WRITE;
    if (fPageProt & NEM_PAGE_PROT_EXECUTE)
        fHvMemProt |= HV_MEMORY_EXEC;

    hv_return_t hrc = hv_vm_map((void *)pvRam, GCPhys, cb, fHvMemProt);
    if (hrc == HV_SUCCESS)
    {
        if (pu2State)
            *pu2State = nemR3DarwinPageStateFromProt(fPageProt);
        return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Changes the protection flags for the given guest physical address range.
 *
 * @returns VBox status code.
 * @param   GCPhys              The guest physical address to start mapping.
 * @param   cb                  The size of the range, page aligned.
 * @param   fPageProt           The page protection flags to use for this range, combination of NEM_PAGE_PROT_XXX
 * @param   pu2State            Where to store the state for the new page, optional.
 */
DECLINLINE(int) nemR3DarwinProtect(RTGCPHYS GCPhys, size_t cb, uint32_t fPageProt, uint8_t *pu2State)
{
    hv_memory_flags_t fHvMemProt = 0;
    if (fPageProt & NEM_PAGE_PROT_READ)
        fHvMemProt |= HV_MEMORY_READ;
    if (fPageProt & NEM_PAGE_PROT_WRITE)
        fHvMemProt |= HV_MEMORY_WRITE;
    if (fPageProt & NEM_PAGE_PROT_EXECUTE)
        fHvMemProt |= HV_MEMORY_EXEC;

    hv_return_t hrc = hv_vm_protect(GCPhys, cb, fHvMemProt);
    if (hrc == HV_SUCCESS)
    {
        if (pu2State)
            *pu2State = nemR3DarwinPageStateFromProt(fPageProt);
        return VINF_SUCCESS;
    }

    LogRel(("nemR3DarwinProtect(%RGp,%zu,%#x): failed! hrc=%#x\n",
            GCPhys, cb, fPageProt, hrc));
    return nemR3DarwinHvSts2Rc(hrc);
}


#ifdef LOG_ENABLED
/**
 * Logs the current CPU state.
 */
static void nemR3DarwinLogState(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (LogIs3Enabled())
    {
        char szRegs[4096];
        DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                        "x0=%016VR{x0} x1=%016VR{x1} x2=%016VR{x2} x3=%016VR{x3}\n"
                        "x4=%016VR{x4} x5=%016VR{x5} x6=%016VR{x6} x7=%016VR{x7}\n"
                        "x8=%016VR{x8} x9=%016VR{x9} x10=%016VR{x10} x11=%016VR{x11}\n"
                        "x12=%016VR{x12} x13=%016VR{x13} x14=%016VR{x14} x15=%016VR{x15}\n"
                        "x16=%016VR{x16} x17=%016VR{x17} x18=%016VR{x18} x19=%016VR{x19}\n"
                        "x20=%016VR{x20} x21=%016VR{x21} x22=%016VR{x22} x23=%016VR{x23}\n"
                        "x24=%016VR{x24} x25=%016VR{x25} x26=%016VR{x26} x27=%016VR{x27}\n"
                        "x28=%016VR{x28} x29=%016VR{x29} x30=%016VR{x30}\n"
                        "pc=%016VR{pc} pstate=%016VR{pstate}\n"
                        "sp_el0=%016VR{sp_el0} sp_el1=%016VR{sp_el1} elr_el1=%016VR{elr_el1}\n"
                        "sctlr_el1=%016VR{sctlr_el1} tcr_el1=%016VR{tcr_el1}\n"
                        "ttbr0_el1=%016VR{ttbr0_el1} ttbr1_el1=%016VR{ttbr1_el1}\n"
                        "vbar_el1=%016VR{vbar_el1} actlr_el1=%016VR{actlr_el1}\n"
                        );
        if (pVM->nem.s.fEl2Enabled)
        {
            Log3(("%s\n", szRegs));
            DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                            "sp_el2=%016VR{sp_el2} elr_el2=%016VR{elr_el2}\n"
                            "spsr_el2=%016VR{spsr_el2} tpidr_el2=%016VR{tpidr_el2}\n"
                            "sctlr_el2=%016VR{sctlr_el2} tcr_el2=%016VR{tcr_el2}\n"
                            "ttbr0_el2=%016VR{ttbr0_el2} ttbr1_el2=%016VR{ttbr1_el2}\n"
                            "esr_el2=%016VR{esr_el2} far_el2=%016VR{far_el2}\n"
                            "hcr_el2=%016VR{hcr_el2} tcr_el2=%016VR{tcr_el2}\n"
                            "vbar_el2=%016VR{vbar_el2} cptr_el2=%016VR{cptr_el2}\n"
                            );
        }
        char szInstr[256]; RT_ZERO(szInstr);
        DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, 0, 0,
                           DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                           szInstr, sizeof(szInstr), NULL);
        Log3(("%s%s\n", szRegs, szInstr));
    }
}
#endif /* LOG_ENABLED */


static int nemR3DarwinCopyStateFromHv(PVMCC pVM, PVMCPUCC pVCpu, uint64_t fWhat)
{
    RT_NOREF(pVM);

    hv_return_t hrc = hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CTL_EL0, &pVCpu->cpum.GstCtx.CntvCtlEl0);
    if (hrc == HV_SUCCESS)
        hrc = hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CVAL_EL0, &pVCpu->cpum.GstCtx.CntvCValEl0);

    if (   hrc == HV_SUCCESS
        && (fWhat & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC | CPUMCTX_EXTRN_FPCR | CPUMCTX_EXTRN_FPSR)))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (s_aCpumRegs[i].fCpumExtrn & fWhat)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                hrc |= hv_vcpu_get_reg(pVCpu->nem.s.hVCpu, s_aCpumRegs[i].enmHvReg, pu64);
            }
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_V0_V31))
    {
        /* SIMD/FP registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumFpRegs); i++)
        {
            hv_simd_fp_uchar16_t *pu128 = (hv_simd_fp_uchar16_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumFpRegs[i].offCpumCtx);
            hrc |= hv_vcpu_get_simd_fp_reg(pVCpu->nem.s.hVCpu, s_aCpumFpRegs[i].enmHvReg, pu128);
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SYSREG_DEBUG))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumDbgRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumDbgRegs[i].offCpumCtx);
            hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumDbgRegs[i].enmHvReg, pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SYSREG_PAUTH_KEYS))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumPAuthKeyRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumPAuthKeyRegs[i].offCpumCtx);
            hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumPAuthKeyRegs[i].enmHvReg, pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SYSREG_MISC)))
    {
        /* System registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegs); i++)
        {
            if (s_aCpumSysRegs[i].fCpumExtrn & fWhat)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegs[i].offCpumCtx);
                hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumSysRegs[i].enmHvReg, pu64);
            }
        }
    }

    /* The paging related system registers need to be treated differently as they might invoke a PGM mode change. */
    bool fPgModeChange = false;
    uint64_t u64RegSctlrEl1;
    uint64_t u64RegTcrEl1;
    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SCTLR_TCR_TTBR))
    {
        hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_SCTLR_EL1, &u64RegSctlrEl1);
        hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_TCR_EL1,   &u64RegTcrEl1);
        hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_TTBR0_EL1, &pVCpu->cpum.GstCtx.Ttbr0.u64);
        hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_TTBR1_EL1, &pVCpu->cpum.GstCtx.Ttbr1.u64);
        if (   hrc == HV_SUCCESS
            && (   u64RegSctlrEl1 != pVCpu->cpum.GstCtx.Sctlr.u64
                || u64RegTcrEl1   != pVCpu->cpum.GstCtx.Tcr.u64))
        {
            pVCpu->cpum.GstCtx.Sctlr.u64 = u64RegSctlrEl1;
            pVCpu->cpum.GstCtx.Tcr.u64   = u64RegTcrEl1;
            fPgModeChange = true;
        }
    }

    if (   hrc == HV_SUCCESS
        && pVM->nem.s.fMacOsSequia
        && (fWhat & CPUMCTX_EXTRN_SYSREG_MISC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegsSequioa); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegsSequioa[i].offCpumCtx);
            hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumSysRegsSequioa[i].enmHvReg, pu64);

            /* Make sure only the TOS bit is kept as this seems to return 0x0000000000000c00 which fails during writes. */
            /** @todo r=aeichner Need to find out where the value comes from, some bits were reverse engineered here
             * https://github.com/AsahiLinux/docs/blob/main/docs/hw/cpu/system-registers.md#actlr_el1-arm-standard-not-standard
             * But the ones being set are not documented. Maybe they are always set by the Hypervisor...
             */
            if (s_aCpumSysRegsSequioa[i].enmHvReg == HV_SYS_REG_ACTLR_EL1)
                *pu64 &= RT_BIT_64(1);
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SYSREG_EL2)
        && pVM->nem.s.fEl2Enabled)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumEl2SysRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumEl2SysRegs[i].offCpumCtx);
            hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, (hv_sys_reg_t)s_aCpumEl2SysRegs[i].idSysReg, pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_PSTATE))
    {
        uint64_t u64Tmp;
        hrc |= hv_vcpu_get_reg(pVCpu->nem.s.hVCpu, HV_REG_CPSR, &u64Tmp);
        if (hrc == HV_SUCCESS)
            pVCpu->cpum.GstCtx.fPState = (uint32_t)u64Tmp;
    }

    if (fPgModeChange)
    {
        int rc = PGMChangeMode(pVCpu, 1 /*bEl*/, u64RegSctlrEl1, u64RegTcrEl1);
        AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_1);
    }

    /* Almost done, just update extern flags. */
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
        pVCpu->cpum.GstCtx.fExtrn = 0;

    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Exports the guest state to HV for execution.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 */
static int nemR3DarwinExportGuestState(PVMCC pVM, PVMCPUCC pVCpu)
{
    RT_NOREF(pVM);
    hv_return_t hrc = HV_SUCCESS;

    if (   (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC | CPUMCTX_EXTRN_FPCR | CPUMCTX_EXTRN_FPSR))
        !=                              (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC | CPUMCTX_EXTRN_FPCR | CPUMCTX_EXTRN_FPSR))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (!(s_aCpumRegs[i].fCpumExtrn & pVCpu->cpum.GstCtx.fExtrn))
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                hrc |= hv_vcpu_set_reg(pVCpu->nem.s.hVCpu, s_aCpumRegs[i].enmHvReg, *pu64);
            }
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_V0_V31))
    {
        /* SIMD/FP registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumFpRegs); i++)
        {
            hv_simd_fp_uchar16_t *pu128 = (hv_simd_fp_uchar16_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumFpRegs[i].offCpumCtx);
            hrc |= hv_vcpu_set_simd_fp_reg(pVCpu->nem.s.hVCpu, s_aCpumFpRegs[i].enmHvReg, *pu128);
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_DEBUG))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumDbgRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumDbgRegs[i].offCpumCtx);
            hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumDbgRegs[i].enmHvReg, *pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_PAUTH_KEYS))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumPAuthKeyRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumPAuthKeyRegs[i].offCpumCtx);
            hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumPAuthKeyRegs[i].enmHvReg, *pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        &&     (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SYSREG_MISC))
            !=                              (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SYSREG_MISC))
    {
        /* System registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegs); i++)
        {
            if (!(s_aCpumSysRegs[i].fCpumExtrn & pVCpu->cpum.GstCtx.fExtrn))
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegs[i].offCpumCtx);
                hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumSysRegs[i].enmHvReg, *pu64);
            }
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SCTLR_TCR_TTBR))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegsPg); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegsPg[i].offCpumCtx);
            hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumSysRegsPg[i].enmHvReg, *pu64);
        }
    }

    if (   hrc == HV_SUCCESS
        && pVM->nem.s.fMacOsSequia
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_MISC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegsSequioa); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegsSequioa[i].offCpumCtx);
            hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aCpumSysRegsSequioa[i].enmHvReg, *pu64);
            Assert(hrc == HV_SUCCESS);
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_EL2)
        && pVM->nem.s.fEl2Enabled)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumEl2SysRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumEl2SysRegs[i].offCpumCtx);
            hrc |= hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, (hv_sys_reg_t)s_aCpumEl2SysRegs[i].idSysReg, *pu64);
            Assert(hrc == HV_SUCCESS);
        }
    }

    if (   hrc == HV_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_PSTATE))
        hrc = hv_vcpu_set_reg(pVCpu->nem.s.hVCpu, HV_REG_CPSR, pVCpu->cpum.GstCtx.fPState);

    pVCpu->cpum.GstCtx.fExtrn |= CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_KEEPER_NEM;
    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Worker for nemR3NativeInit that loads the Hypervisor.framework shared library.
 *
 * @returns VBox status code.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3DarwinLoadHv(PRTERRINFO pErrInfo)
{
    RTLDRMOD hMod = NIL_RTLDRMOD;
    static const char *s_pszHvPath = "/System/Library/Frameworks/Hypervisor.framework/Hypervisor";

    int rc = RTLdrLoadEx(s_pszHvPath, &hMod, RTLDRLOAD_FLAGS_NO_UNLOAD | RTLDRLOAD_FLAGS_NO_SUFFIX, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aImports); i++)
        {
            int rc2 = RTLdrGetSymbol(hMod, g_aImports[i].pszName, (void **)g_aImports[i].ppfn);
            if (RT_SUCCESS(rc2))
                LogRel(("NEM:  info: Found optional import Hypervisor!%s.\n", g_aImports[i].pszName));
            else
            {
                *g_aImports[i].ppfn = NULL;
                LogRel(("NEM:  info: Optional import Hypervisor!%s not found: %Rrc\n", g_aImports[i].pszName, rc2));
            }
        }
        Assert(RT_SUCCESS(rc) && !RTErrInfoIsSet(pErrInfo));
        RTLdrClose(hMod);
    }
    else
    {
        RTErrInfoAddF(pErrInfo, rc, "Failed to load Hypervisor.framwork: %s: %Rrc", s_pszHvPath, rc);
        rc = VERR_NEM_INIT_FAILED;
    }

    return rc;
}


/**
 * Dumps some GIC information to the release log.
 */
static void nemR3DarwinDumpGicInfo(void)
{
    size_t val = 0;
    hv_return_t hrc = hv_gic_get_redistributor_size(&val);
    LogRel(("GICNem: hv_gic_get_redistributor_size()                    -> hrc=%#x / size=%zu\n", hrc, val));
    hrc = hv_gic_get_distributor_size(&val);
    LogRel(("GICNem: hv_gic_get_distributor_size()                      -> hrc=%#x / size=%zu\n", hrc, val));
    hrc = hv_gic_get_distributor_base_alignment(&val);
    LogRel(("GICNem: hv_gic_get_distributor_base_alignment()            -> hrc=%#x / size=%zu\n", hrc, val));
    hrc = hv_gic_get_redistributor_base_alignment(&val);
    LogRel(("GICNem: hv_gic_get_redistributor_base_alignment()          -> hrc=%#x / size=%zu\n", hrc, val));
    hrc = hv_gic_get_msi_region_base_alignment(&val);
    LogRel(("GICNem: hv_gic_get_msi_region_base_alignment()             -> hrc=%#x / size=%zu\n", hrc, val));
    hrc = hv_gic_get_msi_region_size(&val);
    LogRel(("GICNem: hv_gic_get_msi_region_size()                       -> hrc=%#x / size=%zu\n", hrc, val));
    uint32_t u32SpiIntIdBase = 0;
    uint32_t cSpiIntIds = 0;
    hrc = hv_gic_get_spi_interrupt_range(&u32SpiIntIdBase, &cSpiIntIds);
    LogRel(("GICNem: hv_gic_get_spi_interrupt_range()                   -> hrc=%#x / SpiIntIdBase=%u, cSpiIntIds=%u\n", hrc, u32SpiIntIdBase, cSpiIntIds));

    uint32_t u32IntId = 0;
    hrc = hv_gic_get_intid(HV_GIC_INT_EL1_PHYSICAL_TIMER, &u32IntId);
    LogRel(("GICNem: hv_gic_get_intid(HV_GIC_INT_EL1_PHYSICAL_TIMER)    -> hrc=%#x / IntId=%u\n", hrc, u32IntId));
    hrc = hv_gic_get_intid(HV_GIC_INT_EL1_VIRTUAL_TIMER, &u32IntId);
    LogRel(("GICNem: hv_gic_get_intid(HV_GIC_INT_EL1_VIRTUAL_TIMER)     -> hrc=%#x / IntId=%u\n", hrc, u32IntId));
    hrc = hv_gic_get_intid(HV_GIC_INT_EL2_PHYSICAL_TIMER, &u32IntId);
    LogRel(("GICNem: hv_gic_get_intid(HV_GIC_INT_EL2_PHYSICAL_TIMER)    -> hrc=%#x / IntId=%u\n", hrc, u32IntId));
    hrc = hv_gic_get_intid(HV_GIC_INT_MAINTENANCE, &u32IntId);
    LogRel(("GICNem: hv_gic_get_intid(HV_GIC_INT_MAINTENANCE)           -> hrc=%#x / IntId=%u\n", hrc, u32IntId));
    hrc = hv_gic_get_intid(HV_GIC_INT_PERFORMANCE_MONITOR, &u32IntId);
    LogRel(("GICNem: hv_gic_get_intid(HV_GIC_INT_PERFORMANCE_MONITOR)   -> hrc=%#x / IntId=%u\n", hrc, u32IntId));
}


static int nemR3DarwinGicCreate(PVM pVM)
{
    nemR3DarwinDumpGicInfo();

    //PCFGMNODE pGicDev = CFGMR3GetChild(CFGMR3GetRoot(pVM), "Devices/gic/0");
    PCFGMNODE pGicCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "Devices/gic-nem/0/Config");
    AssertPtrReturn(pGicCfg, VERR_NEM_IPE_5);

    hv_gic_config_t hGicCfg = hv_gic_config_create();

    /*
     * Query the MMIO ranges.
     */
    RTGCPHYS GCPhysMmioBaseDist = 0;
    int rc = CFGMR3QueryU64(pGicCfg, "DistributorMmioBase", &GCPhysMmioBaseDist);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          "Configuration error: Failed to get the \"DistributorMmioBase\" value\n");

    RTGCPHYS GCPhysMmioBaseReDist = 0;
    rc = CFGMR3QueryU64(pGicCfg, "RedistributorMmioBase", &GCPhysMmioBaseReDist);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          "Configuration error: Failed to get the \"RedistributorMmioBase\" value\n");

    hv_return_t hrc = hv_gic_config_set_distributor_base(hGicCfg, GCPhysMmioBaseDist);
    if (hrc != HV_SUCCESS)
        return nemR3DarwinHvSts2Rc(hrc);

    hrc = hv_gic_config_set_redistributor_base(hGicCfg, GCPhysMmioBaseReDist);
    if (hrc != HV_SUCCESS)
        return nemR3DarwinHvSts2Rc(hrc);

    hrc = hv_gic_create(hGicCfg);
    os_release(hGicCfg);
    if (hrc != HV_SUCCESS)
        return nemR3DarwinHvSts2Rc(hrc);

    return rc;
}


/**
 * Registers statistics for the given vCPU.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           The CPU ID.
 * @param   pNemCpu         The NEM CPU structure.
 */
static int nemR3DarwinStatisticsRegister(PVM pVM, VMCPUID idCpu, PNEMCPU pNemCpu)
{
#define NEM_REG_STAT(a_pVar, a_enmType, s_enmVisibility, a_enmUnit, a_szNmFmt, a_szDesc) do { \
                int rc = STAMR3RegisterF(pVM, a_pVar, a_enmType, s_enmVisibility, a_enmUnit, a_szDesc, a_szNmFmt, idCpu); \
                AssertRC(rc); \
            } while (0)
#define NEM_REG_PROFILE(a_pVar, a_szNmFmt, a_szDesc) \
           NEM_REG_STAT(a_pVar, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, a_szNmFmt, a_szDesc)
#define NEM_REG_COUNTER(a, b, desc) NEM_REG_STAT(a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, b, desc)

    NEM_REG_COUNTER(&pNemCpu->StatExitAll, "/NEM/CPU%u/Exit/All", "Total exits (including nested-guest exits).");

    return VINF_SUCCESS;

#undef NEM_REG_COUNTER
#undef NEM_REG_PROFILE
#undef NEM_REG_STAT
}


/**
 * Try initialize the native API.
 *
 * This may only do part of the job, more can be done in
 * nemR3NativeInitAfterCPUM() and nemR3NativeInitCompleted().
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   fFallback       Whether we're in fallback mode or use-NEM mode. In
 *                          the latter we'll fail if we cannot initialize.
 * @param   fForced         Whether the HMForced flag is set and we should
 *                          fail if we cannot initialize.
 */
int nemR3NativeInit(PVM pVM, bool fFallback, bool fForced)
{
    AssertReturn(!pVM->nem.s.fCreatedVm, VERR_WRONG_ORDER);

    /*
     * Some state init.
     */
    PCFGMNODE pCfgNem = CFGMR3GetChild(CFGMR3GetRoot(pVM), "NEM/");
    RT_NOREF(pCfgNem);

    /*
     * Error state.
     * The error message will be non-empty on failure and 'rc' will be set too.
     */
    RTERRINFOSTATIC ErrInfo;
    PRTERRINFO pErrInfo = RTErrInfoInitStatic(&ErrInfo);

    /* Resolve optional imports */
    int rc = nemR3DarwinLoadHv(pErrInfo);
    if (RT_FAILURE(rc))
    {
        if ((fForced || !fFallback) && RTErrInfoIsSet(pErrInfo))
            return VMSetError(pVM, rc, RT_SRC_POS, "%s", pErrInfo->pszMsg);
        return rc;
    }

    /*
     * Need to enable nested virt here if supported and reset the CFGM value to false
     * if not supported. This ASSUMES that NEM is initialized before CPUM.
     */
    PCFGMNODE pCfgCpum = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM/");

    hv_vm_config_t hVmCfg = NULL;
    if (   hv_vm_config_create
        && hv_vm_config_get_el2_supported)
    {
        pVM->nem.s.fMacOsSequia = true; /* hv_vm_config_get_el2_supported is only available on Sequioa 15.0. */

        hVmCfg = hv_vm_config_create();

        bool fHvEl2Supported = false;
        hv_return_t hrc = hv_vm_config_get_el2_supported(&fHvEl2Supported);
        if (   hrc == HV_SUCCESS
            && fHvEl2Supported)
        {
            /** @cfgm{/CPUM/NestedHWVirt, bool, false}
             * Whether to expose the hardware virtualization (EL2/VHE) feature to the guest.
             * The default is false. Only supported on M3 and later and macOS 15.0+ (Sonoma).
             */
            bool fNestedHWVirt = false;
            rc = CFGMR3QueryBoolDef(pCfgCpum, "NestedHWVirt", &fNestedHWVirt, false);
            AssertLogRelRCReturn(rc, rc);
            if (fNestedHWVirt)
            {
                hrc = hv_vm_config_set_el2_enabled(hVmCfg, fNestedHWVirt);
                if (hrc != HV_SUCCESS)
                    return VMSetError(pVM, VERR_CPUM_INVALID_HWVIRT_CONFIG, RT_SRC_POS,
                                      "Cannot enable nested virtualization: hrc=%#x %s!\n", hrc, nemR3DarwinHvStatusName(hrc));
                pVM->nem.s.fEl2Enabled = true;
                LogRel(("NEM: Enabled nested virtualization (EL2) support\n"));
            }
        }
        else
        {
            /* Ensure nested virt is not set. */
            rc = CFGMR3RemoveValue(pCfgCpum, "NestedHWVirt");
            AssertLogRelRC(rc);

            LogRel(("NEM: The host doesn't supported nested virtualization! (hrc=%#x fHvEl2Supported=%RTbool)\n",
                    hrc, fHvEl2Supported));
        }
    }
    else
    {
        /* Ensure nested virt is not set. */
        rc = CFGMR3RemoveValue(pCfgCpum, "NestedHWVirt");
        AssertLogRelRC(rc);

        LogRel(("NEM: Hypervisor.framework doesn't supported nested virtualization!\n"));
    }

    hv_return_t hrc = hv_vm_create(hVmCfg);
    os_release(hVmCfg);
    if (hrc == HV_SUCCESS)
    {
        pVM->nem.s.fCreatedVm  = true;
        pVM->nem.s.u64CntFrqHz = ASMReadCntFrqEl0();

        /* Will be initialized in NEMHCResumeCpuTickOnAll() before executing guest code. */
        pVM->nem.s.u64VTimerOff = 0;

        VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_NATIVE_API);
        Log(("NEM: Marked active!\n"));
        PGMR3EnableNemMode(pVM);

        /* Register statistics for all VCPUs. */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PNEMCPU pNemCpu = &pVM->apCpusR3[idCpu]->nem.s;
            nemR3DarwinStatisticsRegister(pVM, idCpu, pNemCpu);
        }

        return VINF_SUCCESS;
    }

    rc = RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "hv_vm_create() failed: %#x %s", hrc, nemR3DarwinHvStatusName(hrc));

    /*
     * We only fail if in forced mode, otherwise just log the complaint and return.
     */
    Assert(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API || RTErrInfoIsSet(pErrInfo));
    if (   (fForced || !fFallback)
        && pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NATIVE_API)
        return VMSetError(pVM, RT_SUCCESS_NP(rc) ? VERR_NEM_NOT_AVAILABLE : rc, RT_SRC_POS, "%s", pErrInfo->pszMsg);

    if (RTErrInfoIsSet(pErrInfo))
        LogRel(("NEM: Not available: %s\n", pErrInfo->pszMsg));
    return VINF_SUCCESS;
}


/**
 * Worker to create the vCPU handle on the EMT running it later on (as required by HV).
 *
 * @returns VBox status code
 * @param   pVM                 The VM handle.
 * @param   pVCpu               The vCPU handle.
 * @param   idCpu               ID of the CPU to create.
 */
static DECLCALLBACK(int) nemR3DarwinNativeInitVCpuOnEmt(PVM pVM, PVMCPU pVCpu, VMCPUID idCpu)
{
    if (idCpu == 0)
    {
        Assert(pVM->nem.s.hVCpuCfg == NULL);

        /* Create a new vCPU config and query the ID registers. */
        pVM->nem.s.hVCpuCfg = hv_vcpu_config_create();
        if (!pVM->nem.s.hVCpuCfg)
            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                              "Call to hv_vcpu_config_create failed on vCPU %u", idCpu);

        /* Query ID registers and hand them to CPUM. */
        CPUMARMV8IDREGS IdRegs; RT_ZERO(IdRegs);
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aIdRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&IdRegs + s_aIdRegs[i].offIdStruct);
            hv_return_t hrc = hv_vcpu_config_get_feature_reg(pVM->nem.s.hVCpuCfg, s_aIdRegs[i].enmHvReg, pu64);
            if (hrc != HV_SUCCESS)
                return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                                  "Call to hv_vcpu_get_feature_reg(, %#x, ) failed: %#x (%Rrc)", hrc, nemR3DarwinHvSts2Rc(hrc));
        }

        int rc = CPUMR3PopulateFeaturesByIdRegisters(pVM, &IdRegs);
        if (RT_FAILURE(rc))
            return rc;
    }

    hv_return_t hrc = hv_vcpu_create(&pVCpu->nem.s.hVCpu, &pVCpu->nem.s.pHvExit, pVM->nem.s.hVCpuCfg);
    if (hrc != HV_SUCCESS)
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Call to hv_vcpu_create failed on vCPU %u: %#x (%Rrc)", idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));

    hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_MPIDR_EL1, idCpu);
    if (hrc != HV_SUCCESS)
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Setting MPIDR_EL1 failed on vCPU %u: %#x (%Rrc)", idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));

    return VINF_SUCCESS;
}


/**
 * Worker to destroy the vCPU handle on the EMT running it later on (as required by HV).
 *
 * @returns VBox status code.
 * @param   pVM                 The VM handle.
 * @param   pVCpu               The vCPU handle.
 */
static DECLCALLBACK(int) nemR3DarwinNativeTermVCpuOnEmt(PVM pVM, PVMCPU pVCpu)
{
    hv_return_t hrc = hv_vcpu_destroy(pVCpu->nem.s.hVCpu);
    Assert(hrc == HV_SUCCESS); RT_NOREF(hrc);

    if (pVCpu->idCpu == 0)
    {
        os_release(pVM->nem.s.hVCpuCfg);
        pVM->nem.s.hVCpuCfg = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * This is called after CPUMR3Init is done.
 *
 * @returns VBox status code.
 * @param   pVM                 The VM handle..
 */
int nemR3NativeInitAfterCPUM(PVM pVM)
{
    /*
     * Validate sanity.
     */
    AssertReturn(!pVM->nem.s.fCreatedEmts, VERR_WRONG_ORDER);
    AssertReturn(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API, VERR_WRONG_ORDER);

    /*
     * Need to create the GIC here if the NEM variant is configured
     * before any vCPU is created according to the Apple docs.
     */
    if (   hv_gic_create
        && CFGMR3GetChild(CFGMR3GetRoot(pVM), "Devices/gic-nem/0"))
    {
        int rc = nemR3DarwinGicCreate(pVM);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS, "Creating the GIC failed: %Rrc", rc);
    }

    /*
     * Setup the EMTs.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        int rc = VMR3ReqCallWait(pVM, idCpu, (PFNRT)nemR3DarwinNativeInitVCpuOnEmt, 3, pVM, pVCpu, idCpu);
        if (RT_FAILURE(rc))
        {
            /* Rollback. */
            while (idCpu--)
                VMR3ReqCallWait(pVM, idCpu, (PFNRT)nemR3DarwinNativeTermVCpuOnEmt, 2, pVM, pVCpu);

            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS, "Call to hv_vcpu_create failed: %Rrc", rc);
        }
    }

    pVM->nem.s.fCreatedEmts = true;
    return VINF_SUCCESS;
}


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    RT_NOREF(pVM, enmWhat);
    return VINF_SUCCESS;
}


int nemR3NativeTerm(PVM pVM)
{
    /*
     * Delete the VM.
     */

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu--)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /*
         * Apple's documentation states that the vCPU should be destroyed
         * on the thread running the vCPU but as all the other EMTs are gone
         * at this point, destroying the VM would hang.
         *
         * We seem to be at luck here though as destroying apparently works
         * from EMT(0) as well.
         */
        hv_return_t hrc = hv_vcpu_destroy(pVCpu->nem.s.hVCpu);
        Assert(hrc == HV_SUCCESS); RT_NOREF(hrc);
    }

    pVM->nem.s.fCreatedEmts = false;
    if (pVM->nem.s.fCreatedVm)
    {
        hv_return_t hrc = hv_vm_destroy();
        if (hrc != HV_SUCCESS)
            LogRel(("NEM: hv_vm_destroy() failed with %#x\n", hrc));

        pVM->nem.s.fCreatedVm = false;
    }
    return VINF_SUCCESS;
}


/**
 * VM reset notification.
 *
 * @param   pVM         The cross context VM structure.
 */
void nemR3NativeReset(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Reset CPU due to INIT IPI or hot (un)plugging.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the CPU being
 *                      reset.
 * @param   fInitIpi    Whether this is the INIT IPI or hot (un)plugging case.
 */
void nemR3NativeResetCpu(PVMCPU pVCpu, bool fInitIpi)
{
    RT_NOREF(pVCpu, fInitIpi);
}


/**
 * Returns the byte size from the given access SAS value.
 *
 * @returns Number of bytes to transfer.
 * @param   uSas            The SAS value to convert.
 */
DECLINLINE(size_t) nemR3DarwinGetByteCountFromSas(uint8_t uSas)
{
    switch (uSas)
    {
        case ARMV8_EC_ISS_DATA_ABRT_SAS_BYTE:     return sizeof(uint8_t);
        case ARMV8_EC_ISS_DATA_ABRT_SAS_HALFWORD: return sizeof(uint16_t);
        case ARMV8_EC_ISS_DATA_ABRT_SAS_WORD:     return sizeof(uint32_t);
        case ARMV8_EC_ISS_DATA_ABRT_SAS_DWORD:    return sizeof(uint64_t);
        default:
            AssertReleaseFailed();
    }

    return 0;
}


/**
 * Sets the given general purpose register to the given value.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uReg            The register index.
 * @param   f64BitReg       Flag whether to operate on a 64-bit or 32-bit register.
 * @param   fSignExtend     Flag whether to sign extend the value.
 * @param   u64Val          The value.
 */
DECLINLINE(void) nemR3DarwinSetGReg(PVMCPU pVCpu, uint8_t uReg, bool f64BitReg, bool fSignExtend, uint64_t u64Val)
{
    AssertReturnVoid(uReg < 31);

    if (f64BitReg)
        pVCpu->cpum.GstCtx.aGRegs[uReg].x = fSignExtend ? (int64_t)u64Val : u64Val;
    else
        pVCpu->cpum.GstCtx.aGRegs[uReg].x = (uint64_t)(fSignExtend ? (int32_t)u64Val : (uint32_t)u64Val);

    /* Mark the register as not extern anymore. */
    switch (uReg)
    {
        case 0:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X0;
            break;
        case 1:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X1;
            break;
        case 2:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X2;
            break;
        case 3:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X3;
            break;
        default:
            AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_X4_X28));
            /** @todo We need to import all missing registers in order to clear this flag (or just set it in HV from here). */
    }
}


/**
 * Gets the given general purpose register and returns the value.
 *
 * @returns Value from the given register.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uReg            The register index.
 */
DECLINLINE(uint64_t) nemR3DarwinGetGReg(PVMCPU pVCpu, uint8_t uReg)
{
    AssertReturn(uReg <= ARMV8_A64_REG_XZR, 0);

    if (uReg == ARMV8_A64_REG_XZR)
        return 0;

    /** @todo Import the register if extern. */
    AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_GPRS_MASK));

    return pVCpu->cpum.GstCtx.aGRegs[uReg].x;
}


/**
 * Works on the data abort exception (which will be a MMIO access most of the time).
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uIss            The instruction specific syndrome value.
 * @param   fInsn32Bit      Flag whether the exception was caused by a 32-bit or 16-bit instruction.
 * @param   GCPtrDataAbrt   The virtual GC address causing the data abort.
 * @param   GCPhysDataAbrt  The physical GC address which caused the data abort.
 */
static VBOXSTRICTRC nemR3DarwinHandleExitExceptionDataAbort(PVM pVM, PVMCPU pVCpu, uint32_t uIss, bool fInsn32Bit,
                                                            RTGCPTR GCPtrDataAbrt, RTGCPHYS GCPhysDataAbrt)
{
    bool fIsv        = RT_BOOL(uIss & ARMV8_EC_ISS_DATA_ABRT_ISV);
    bool fL2Fault    = RT_BOOL(uIss & ARMV8_EC_ISS_DATA_ABRT_S1PTW);
    bool fWrite      = RT_BOOL(uIss & ARMV8_EC_ISS_DATA_ABRT_WNR);
    bool f64BitReg   = RT_BOOL(uIss & ARMV8_EC_ISS_DATA_ABRT_SF);
    bool fSignExtend = RT_BOOL(uIss & ARMV8_EC_ISS_DATA_ABRT_SSE);
    uint8_t uReg     = ARMV8_EC_ISS_DATA_ABRT_SRT_GET(uIss);
    uint8_t uAcc     = ARMV8_EC_ISS_DATA_ABRT_SAS_GET(uIss);
    size_t cbAcc     = nemR3DarwinGetByteCountFromSas(uAcc);
    LogFlowFunc(("fIsv=%RTbool fL2Fault=%RTbool fWrite=%RTbool f64BitReg=%RTbool fSignExtend=%RTbool uReg=%u uAcc=%u GCPtrDataAbrt=%RGv GCPhysDataAbrt=%RGp\n",
                 fIsv, fL2Fault, fWrite, f64BitReg, fSignExtend, uReg, uAcc, GCPtrDataAbrt, GCPhysDataAbrt));

    RT_NOREF(fL2Fault, GCPtrDataAbrt);

    if (fWrite)
    {
        /*
         * Check whether this is one of the dirty tracked regions, mark it as dirty
         * and enable write support for this region again.
         *
         * This is required for proper VRAM tracking or the display might not get updated
         * and it is impossible to use the PGM generic facility as it operates on guest page sizes
         * but setting protection flags with Hypervisor.framework works only host page sized regions, so
         * we have to cook our own. Additionally the VRAM region is marked as prefetchable (write-back)
         * which doesn't produce a valid instruction syndrome requiring restarting the instruction after enabling
         * write access again (due to a missing interpreter right now).
         */
        for (uint32_t idSlot = 0; idSlot < RT_ELEMENTS(pVM->nem.s.aMmio2DirtyTracking); idSlot++)
        {
            PNEMHVMMIO2REGION pMmio2Region = &pVM->nem.s.aMmio2DirtyTracking[idSlot];

            if (   GCPhysDataAbrt >= pMmio2Region->GCPhysStart
                && GCPhysDataAbrt <= pMmio2Region->GCPhysLast)
            {
                pMmio2Region->fDirty = true;

                uint8_t u2State;
                int rc = nemR3DarwinProtect(pMmio2Region->GCPhysStart, pMmio2Region->GCPhysLast - pMmio2Region->GCPhysStart + 1,
                                            NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE | NEM_PAGE_PROT_WRITE, &u2State);

                /* Restart the instruction if there is no instruction syndrome available. */
                if (RT_FAILURE(rc) || !fIsv)
                    return rc;
            }
        }
    }

    VBOXSTRICTRC rcStrict;
    if (fIsv)
    {
        EMHistoryAddExit(pVCpu,
                         fWrite
                         ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                         : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                         pVCpu->cpum.GstCtx.Pc.u64, ASMReadTSC());

        uint64_t u64Val = 0;
        if (fWrite)
        {
            u64Val = nemR3DarwinGetGReg(pVCpu, uReg);
            rcStrict = PGMPhysWrite(pVM, GCPhysDataAbrt, &u64Val, cbAcc, PGMACCESSORIGIN_HM);
            Log4(("MmioExit/%u: %08RX64: WRITE %#RGp LB %u, %.*Rhxs -> rcStrict=%Rrc\n",
                  pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, GCPhysDataAbrt, cbAcc, cbAcc,
                  &u64Val, VBOXSTRICTRC_VAL(rcStrict) ));
        }
        else
        {
            rcStrict = PGMPhysRead(pVM, GCPhysDataAbrt, &u64Val, cbAcc, PGMACCESSORIGIN_HM);
            Log4(("MmioExit/%u: %08RX64: READ %#RGp LB %u -> %.*Rhxs rcStrict=%Rrc\n",
                  pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, GCPhysDataAbrt, cbAcc, cbAcc,
                  &u64Val, VBOXSTRICTRC_VAL(rcStrict) ));
            if (rcStrict == VINF_SUCCESS)
                nemR3DarwinSetGReg(pVCpu, uReg, f64BitReg, fSignExtend, u64Val);
        }
    }
    else
    {
        /** @todo Our UEFI firmware accesses the flash region with the following instruction
         *        when the NVRAM actually contains data:
         *             ldrb w9, [x6, #-0x0001]!
         *        This is too complicated for the hardware so the ISV bit is not set. Until there
         *        is a proper IEM implementation we just handle this here for now to avoid annoying
         *        users too much.
         */
        /* The following ASSUMES that the vCPU state is completely synced. */

        /* Read instruction. */
        RTGCPTR GCPtrPage = pVCpu->cpum.GstCtx.Pc.u64 & ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;
        const void *pvPageR3 = NULL;
        PGMPAGEMAPLOCK  PageMapLock;

        rcStrict = PGMPhysGCPtr2CCPtrReadOnly(pVCpu, GCPtrPage, &pvPageR3, &PageMapLock);
        if (rcStrict == VINF_SUCCESS)
        {
            uint32_t u32Instr = *(uint32_t *)((uint8_t *)pvPageR3 + (pVCpu->cpum.GstCtx.Pc.u64 - GCPtrPage));
            PGMPhysReleasePageMappingLock(pVCpu->pVMR3, &PageMapLock);

            DISSTATE Dis;
            rcStrict = DISInstrWithPrefetchedBytes((uintptr_t)pVCpu->cpum.GstCtx.Pc.u64, DISCPUMODE_ARMV8_A64,  0 /*fFilter - none */,
                                                   &u32Instr, sizeof(u32Instr), NULL, NULL, &Dis, NULL);
            if (rcStrict == VINF_SUCCESS)
            {
                if (   Dis.pCurInstr->uOpcode == OP_ARMV8_A64_LDRB
                    && Dis.aParams[0].armv8.enmType == kDisArmv8OpParmReg
                    && Dis.aParams[0].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_32Bit
                    && Dis.aParams[1].armv8.enmType == kDisArmv8OpParmAddrInGpr
                    && Dis.aParams[1].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_64Bit
                    && (Dis.aParams[1].fUse & DISUSE_PRE_INDEXED))
                {
                    /* The fault address is already the final address. */
                    uint8_t bVal = 0;
                    rcStrict = PGMPhysRead(pVM, GCPhysDataAbrt, &bVal, 1, PGMACCESSORIGIN_HM);
                    Log4(("MmioExit/%u: %08RX64: READ %#RGp LB %u -> %.*Rhxs rcStrict=%Rrc\n",
                          pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, GCPhysDataAbrt, sizeof(bVal), sizeof(bVal),
                          &bVal, VBOXSTRICTRC_VAL(rcStrict) ));
                    if (rcStrict == VINF_SUCCESS)
                    {
                        nemR3DarwinSetGReg(pVCpu, Dis.aParams[0].armv8.Op.Reg.idReg, false /*f64BitReg*/, false /*fSignExtend*/, bVal);
                        /* Update the indexed register. */
                        pVCpu->cpum.GstCtx.aGRegs[Dis.aParams[1].armv8.Op.Reg.idReg].x += Dis.aParams[1].armv8.u.offBase;
                    }
                }
                /*
                 * Seeing the following with the Windows 11/ARM TPM driver:
                 *     %fffff800e5342888 48 25 45 29             ldp w8, w9, [x10, #+0x0028]
                 */
                else if (   Dis.pCurInstr->uOpcode == OP_ARMV8_A64_LDP
                         && Dis.aParams[0].armv8.enmType == kDisArmv8OpParmReg
                         && Dis.aParams[0].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_32Bit
                         && Dis.aParams[1].armv8.enmType == kDisArmv8OpParmReg
                         && Dis.aParams[1].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_32Bit
                         && Dis.aParams[2].armv8.enmType == kDisArmv8OpParmAddrInGpr
                         && Dis.aParams[2].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_64Bit)
                {
                    /** @todo This is tricky to handle if the first register read returns something else than VINF_SUCCESS... */
                    /* The fault address is already the final address. */
                    uint32_t u32Val1 = 0;
                    uint32_t u32Val2 = 0;
                    rcStrict = PGMPhysRead(pVM, GCPhysDataAbrt, &u32Val1, sizeof(u32Val1), PGMACCESSORIGIN_HM);
                    if (rcStrict == VINF_SUCCESS)
                        rcStrict = PGMPhysRead(pVM, GCPhysDataAbrt + sizeof(uint32_t), &u32Val2, sizeof(u32Val2), PGMACCESSORIGIN_HM);
                    Log4(("MmioExit/%u: %08RX64: READ %#RGp LB %u -> %.*Rhxs %.*Rhxs rcStrict=%Rrc\n",
                          pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, GCPhysDataAbrt, 2 * sizeof(uint32_t), sizeof(u32Val1),
                          &u32Val1, sizeof(u32Val2), &u32Val2, VBOXSTRICTRC_VAL(rcStrict) ));
                    if (rcStrict == VINF_SUCCESS)
                    {
                        nemR3DarwinSetGReg(pVCpu, Dis.aParams[0].armv8.Op.Reg.idReg, false /*f64BitReg*/, false /*fSignExtend*/, u32Val1);
                        nemR3DarwinSetGReg(pVCpu, Dis.aParams[1].armv8.Op.Reg.idReg, false /*f64BitReg*/, false /*fSignExtend*/, u32Val2);
                    }
                }
                /* T O D O:
                 * Recent W11:
                 * x0=ffffb804ea3217d8 x1=ffffe28437802000 x2=0000000000000424 x3=fffff802e5716030
                 * x4=ffffe28437802424 x5=ffffb804ea321bfc x6=000000000080009c x7=000000000080009c
                 * x8=ffff87849fefc788 x9=ffff87849fefc788 x10=000000000000001c x11=ffffb804ea32909c
                 * x12=000000000000001c x13=000000000000009c x14=ffffb804ea3290a8 x15=ffffd580b2b1f7d8
                 * x16=0000f6999080cdbe x17=0000f6999080cdbe x18=ffffd08158fbf000 x19=ffffb804ea3217d0
                 * x20=0000000000000001 x21=0000000000000004 x22=ffffb804ea321660 x23=000047fb15cdefd8
                 * x24=0000000000000000 x25=ffffb804ea2f1080 x26=0000000000000000 x27=0000000000000380
                 * x28=0000000000000000 x29=ffff87849fefc7e0 x30=fffff802e57120b0
                 * pc=fffff802e5713c20 pstate=00000000a0001344
                 * sp_el0=ffff87849fefc7e0 sp_el1=ffff87849e462400 elr_el1=fffff802e98889c8
                 * pl061gpio!start_seg1_.text+0x2c20:
                 * %fffff802e5713c20 23 00 c0 3d             ldr q3, [x1]
                 * VBoxDbg> format %%(%@x1)
                 * Guest physical address: %%ffddd000
                 * VBoxDbg> info mmio
                 * MMIO registrations: 12 (186 allocated)
                 *  ## Ctx    Size Mapping   PCI    Description
                 *   0 R3     00000000000c0000  0000000004000000-00000000040bffff        Flash Memory
                 * [snip]
                 *  11 R3     0000000000001000  00000000ffddd000-00000000ffdddfff        PL061
                 */
                else
                    AssertLogRelMsgFailedReturn(("pc=%#RX64: %#x opcode=%d\n",
                                                 pVCpu->cpum.GstCtx.Pc.u64, Dis.Instr.au32[0], Dis.pCurInstr->uOpcode),
                                                VERR_NEM_IPE_2);
            }
        }
    }

    if (rcStrict == VINF_SUCCESS)
        pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);

    return rcStrict;
}


/**
 * Works on the trapped MRS, MSR and system instruction exception.
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uIss            The instruction specific syndrome value.
 * @param   fInsn32Bit      Flag whether the exception was caused by a 32-bit or 16-bit instruction.
 */
static VBOXSTRICTRC nemR3DarwinHandleExitExceptionTrappedSysInsn(PVM pVM, PVMCPU pVCpu, uint32_t uIss, bool fInsn32Bit)
{
    bool fRead   = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_DIRECTION_IS_READ(uIss);
    uint8_t uCRm = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRM_GET(uIss);
    uint8_t uReg = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_RT_GET(uIss);
    uint8_t uCRn = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRN_GET(uIss);
    uint8_t uOp1 = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP1_GET(uIss);
    uint8_t uOp2 = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP2_GET(uIss);
    uint8_t uOp0 = ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP0_GET(uIss);
    uint16_t idSysReg = ARMV8_AARCH64_SYSREG_ID_CREATE(uOp0, uOp1, uCRn, uCRm, uOp2);
    LogFlowFunc(("fRead=%RTbool uCRm=%u uReg=%u uCRn=%u uOp1=%u uOp2=%u uOp0=%u idSysReg=%#x\n",
                 fRead, uCRm, uReg, uCRn, uOp1, uOp2, uOp0, idSysReg));

    /** @todo EMEXITTYPE_MSR_READ/EMEXITTYPE_MSR_WRITE are misnomers. */
    EMHistoryAddExit(pVCpu,
                     fRead
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_READ)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_WRITE),
                     pVCpu->cpum.GstCtx.Pc.u64, ASMReadTSC());

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    uint64_t u64Val = 0;
    if (fRead)
    {
        RT_NOREF(pVM);
        rcStrict = CPUMQueryGuestSysReg(pVCpu, idSysReg, &u64Val);
        Log4(("SysInsnExit/%u: %08RX64: READ %u:%u:%u:%u:%u -> %#RX64 rcStrict=%Rrc\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, uOp0, uOp1, uCRn, uCRm, uOp2, u64Val,
              VBOXSTRICTRC_VAL(rcStrict) ));
        if (rcStrict == VINF_SUCCESS)
            nemR3DarwinSetGReg(pVCpu, uReg, true /*f64BitReg*/, false /*fSignExtend*/, u64Val);
    }
    else
    {
        u64Val = nemR3DarwinGetGReg(pVCpu, uReg);
        rcStrict = CPUMSetGuestSysReg(pVCpu, idSysReg, u64Val);
        Log4(("SysInsnExit/%u: %08RX64: WRITE %u:%u:%u:%u:%u %#RX64 -> rcStrict=%Rrc\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc.u64, uOp0, uOp1, uCRn, uCRm, uOp2, u64Val,
              VBOXSTRICTRC_VAL(rcStrict) ));
    }

    if (rcStrict == VINF_SUCCESS)
        pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);

    return rcStrict;
}


/**
 * Works on the trapped HVC instruction exception.
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uIss            The instruction specific syndrome value.
 * @param   fAdvancePc      Flag whether to advance the guest program counter.
 */
static VBOXSTRICTRC nemR3DarwinHandleExitExceptionTrappedHvcInsn(PVM pVM, PVMCPU pVCpu, uint32_t uIss, bool fAdvancePc = false)
{
    uint16_t u16Imm = ARMV8_EC_ISS_AARCH64_TRAPPED_HVC_INSN_IMM_GET(uIss);
    LogFlowFunc(("u16Imm=%#RX16\n", u16Imm));

#if 0 /** @todo For later */
    EMHistoryAddExit(pVCpu,
                     fRead
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_READ)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_WRITE),
                     pVCpu->cpum.GstCtx.Pc.u64, ASMReadTSC());
#endif

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    if (u16Imm == 0)
    {
        /** @todo Raise exception to EL1 if PSCI not configured. */
        /** @todo Need a generic mechanism here to pass this to, GIM maybe?. */
        uint32_t uFunId = pVCpu->cpum.GstCtx.aGRegs[ARMV8_A64_REG_X0].w;
        bool fHvc64 = RT_BOOL(uFunId & ARM_SMCCC_FUNC_ID_64BIT); RT_NOREF(fHvc64);
        uint32_t uEntity = ARM_SMCCC_FUNC_ID_ENTITY_GET(uFunId);
        uint32_t uFunNum = ARM_SMCCC_FUNC_ID_NUM_GET(uFunId);
        if (uEntity == ARM_SMCCC_FUNC_ID_ENTITY_STD_SEC_SERVICE)
        {
            switch (uFunNum)
            {
                case ARM_PSCI_FUNC_ID_PSCI_VERSION:
                    nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, ARM_PSCI_FUNC_ID_PSCI_VERSION_SET(1, 2));
                    break;
                case ARM_PSCI_FUNC_ID_SYSTEM_OFF:
                    rcStrict = VMR3PowerOff(pVM->pUVM);
                    break;
                case ARM_PSCI_FUNC_ID_SYSTEM_RESET:
                case ARM_PSCI_FUNC_ID_SYSTEM_RESET2:
                {
                    bool fHaltOnReset;
                    int rc = CFGMR3QueryBool(CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM"), "HaltOnReset", &fHaltOnReset);
                    if (RT_SUCCESS(rc) && fHaltOnReset)
                    {
                        Log(("nemR3DarwinHandleExitExceptionTrappedHvcInsn: Halt On Reset!\n"));
                        rcStrict = VINF_EM_HALT;
                    }
                    else
                    {
                        /** @todo pVM->pdm.s.fResetFlags = fFlags; */
                        VM_FF_SET(pVM, VM_FF_RESET);
                        rcStrict = VINF_EM_RESET;
                    }
                    break;
                }
                case ARM_PSCI_FUNC_ID_CPU_ON:
                {
                    uint64_t u64TgtCpu      = nemR3DarwinGetGReg(pVCpu, ARMV8_A64_REG_X1);
                    RTGCPHYS GCPhysExecAddr = nemR3DarwinGetGReg(pVCpu, ARMV8_A64_REG_X2);
                    uint64_t u64CtxId       = nemR3DarwinGetGReg(pVCpu, ARMV8_A64_REG_X3);
                    VMMR3CpuOn(pVM, u64TgtCpu & 0xff, GCPhysExecAddr, u64CtxId);
                    nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0, true /*f64BitReg*/, false /*fSignExtend*/, ARM_PSCI_STS_SUCCESS);
                    break;
                }
                case ARM_PSCI_FUNC_ID_PSCI_FEATURES:
                {
                    uint32_t u32FunNum = (uint32_t)nemR3DarwinGetGReg(pVCpu, ARMV8_A64_REG_X1);
                    switch (u32FunNum)
                    {
                        case ARM_PSCI_FUNC_ID_PSCI_VERSION:
                        case ARM_PSCI_FUNC_ID_SYSTEM_OFF:
                        case ARM_PSCI_FUNC_ID_SYSTEM_RESET:
                        case ARM_PSCI_FUNC_ID_SYSTEM_RESET2:
                        case ARM_PSCI_FUNC_ID_CPU_ON:
                        case ARM_PSCI_FUNC_ID_MIGRATE_INFO_TYPE:
                            nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0,
                                               false /*f64BitReg*/, false /*fSignExtend*/,
                                               (uint64_t)ARM_PSCI_STS_SUCCESS);
                            break;
                        default:
                            nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0,
                                               false /*f64BitReg*/, false /*fSignExtend*/,
                                               (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);
                    }
                    break;
                }
                case ARM_PSCI_FUNC_ID_MIGRATE_INFO_TYPE:
                    nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, ARM_PSCI_MIGRATE_INFO_TYPE_TOS_NOT_PRESENT);
                    break;
                default:
                    nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);
            }
        }
        else
            nemR3DarwinSetGReg(pVCpu, ARMV8_A64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);
    }

    /** @todo What to do if immediate is != 0? */

    if (   rcStrict == VINF_SUCCESS
        && fAdvancePc)
        pVCpu->cpum.GstCtx.Pc.u64 += sizeof(uint32_t);

    return rcStrict;
}


/**
 * Handles an exception VM exit.
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pExit           Pointer to the exit information.
 */
static VBOXSTRICTRC nemR3DarwinHandleExitException(PVM pVM, PVMCPU pVCpu, const hv_vcpu_exit_t *pExit)
{
    uint32_t uEc = ARMV8_ESR_EL2_EC_GET(pExit->exception.syndrome);
    uint32_t uIss = ARMV8_ESR_EL2_ISS_GET(pExit->exception.syndrome);
    bool fInsn32Bit = ARMV8_ESR_EL2_IL_IS_32BIT(pExit->exception.syndrome);

    LogFlowFunc(("pVM=%p pVCpu=%p{.idCpu=%u} uEc=%u{%s} uIss=%#RX32 fInsn32Bit=%RTbool\n",
                 pVM, pVCpu, pVCpu->idCpu, uEc, nemR3DarwinEsrEl2EcStringify(uEc), uIss, fInsn32Bit));

    switch (uEc)
    {
        case ARMV8_ESR_EL2_DATA_ABORT_FROM_LOWER_EL:
            return nemR3DarwinHandleExitExceptionDataAbort(pVM, pVCpu, uIss, fInsn32Bit, pExit->exception.virtual_address,
                                                           pExit->exception.physical_address);
        case ARMV8_ESR_EL2_EC_AARCH64_TRAPPED_SYS_INSN:
            return nemR3DarwinHandleExitExceptionTrappedSysInsn(pVM, pVCpu, uIss, fInsn32Bit);
        case ARMV8_ESR_EL2_EC_AARCH64_HVC_INSN:
            return nemR3DarwinHandleExitExceptionTrappedHvcInsn(pVM, pVCpu, uIss);
        case ARMV8_ESR_EL2_EC_AARCH64_SMC_INSN:
            return nemR3DarwinHandleExitExceptionTrappedHvcInsn(pVM, pVCpu, uIss, true);
        case ARMV8_ESR_EL2_EC_TRAPPED_WFX:
        {
            /* No need to halt if there is an interrupt pending already. */
            if (VMCPU_FF_IS_ANY_SET(pVCpu, (VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ)))
            {
                LogFlowFunc(("IRQ | FIQ set => VINF_SUCCESS\n"));
                pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);
                return VINF_SUCCESS;
            }

            /* Set the vTimer expiration in order to get out of the halt at the right point in time. */
            if (   (pVCpu->cpum.GstCtx.CntvCtlEl0 & ARMV8_CNTV_CTL_EL0_AARCH64_ENABLE)
                && !(pVCpu->cpum.GstCtx.CntvCtlEl0 & ARMV8_CNTV_CTL_EL0_AARCH64_IMASK))
            {
                uint64_t cTicksVTimer = mach_absolute_time() - pVM->nem.s.u64VTimerOff;

                /* Check whether it expired and start executing guest code. */
                if (cTicksVTimer >= pVCpu->cpum.GstCtx.CntvCValEl0)
                {
                    LogFlowFunc(("Guest timer expired (cTicksVTimer=%RU64 CntvCValEl0=%RU64) => VINF_SUCCESS\n",
                                 cTicksVTimer, pVCpu->cpum.GstCtx.CntvCValEl0));
                    pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);
                    return VINF_SUCCESS;
                }

                uint64_t cTicksVTimerToExpire = pVCpu->cpum.GstCtx.CntvCValEl0 - cTicksVTimer;
                uint64_t cNanoSecsVTimerToExpire = ASMMultU64ByU32DivByU32(cTicksVTimerToExpire, RT_NS_1SEC, (uint32_t)pVM->nem.s.u64CntFrqHz);

                /*
                 * Our halt method doesn't work with sub millisecond granularity at the moment causing a huge slowdown
                 * + scheduling overhead which would increase the wakeup latency.
                 * So only halt when the threshold is exceeded (needs more experimentation but 5ms turned out to be a good compromise
                 * between CPU load when the guest is idle and performance).
                 */
                if (cNanoSecsVTimerToExpire < 2 * RT_NS_1MS)
                {
                    LogFlowFunc(("Guest timer expiration < 2ms (cNanoSecsVTimerToExpire=%RU64) => VINF_SUCCESS\n",
                                 cNanoSecsVTimerToExpire));
                    pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);
                    return VINF_SUCCESS;
                }

                LogFlowFunc(("Set vTimer activation to cNanoSecsVTimerToExpire=%#RX64 (CntvCValEl0=%#RX64, u64VTimerOff=%#RX64 cTicksVTimer=%#RX64 u64CntFrqHz=%#RX64)\n",
                             cNanoSecsVTimerToExpire, pVCpu->cpum.GstCtx.CntvCValEl0, pVM->nem.s.u64VTimerOff, cTicksVTimer, pVM->nem.s.u64CntFrqHz));
                TMCpuSetVTimerNextActivation(pVCpu, cNanoSecsVTimerToExpire);
            }
            else
                TMCpuSetVTimerNextActivation(pVCpu, UINT64_MAX);

            pVCpu->cpum.GstCtx.Pc.u64 += fInsn32Bit ? sizeof(uint32_t) : sizeof(uint16_t);
            return VINF_EM_HALT;
        }
        case ARMV8_ESR_EL2_EC_AARCH64_BRK_INSN:
        {
            VBOXSTRICTRC rcStrict = DBGFTrap03Handler(pVCpu->CTX_SUFF(pVM), pVCpu, &pVCpu->cpum.GstCtx);
            /** @todo Forward genuine guest traps to the guest by either single stepping instruction with debug exception trapping turned off
             * or create instruction interpreter and inject exception ourselves. */
            Assert(rcStrict == VINF_EM_DBG_BREAKPOINT);
            return rcStrict;
        }
        case ARMV8_ESR_EL2_SS_EXCEPTION_FROM_LOWER_EL:
            return VINF_EM_DBG_STEPPED;
        case ARMV8_ESR_EL2_EC_UNKNOWN:
        default:
            LogRel(("NEM/Darwin: Unknown Exception Class in syndrome: uEc=%u{%s} uIss=%#RX32 fInsn32Bit=%RTbool\n",
                    uEc, nemR3DarwinEsrEl2EcStringify(uEc), uIss, fInsn32Bit));
            AssertReleaseFailed();
            return VERR_NOT_IMPLEMENTED;
    }

    return VINF_SUCCESS;
}


/**
 * Handles an exit from hv_vcpu_run().
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 */
static VBOXSTRICTRC nemR3DarwinHandleExit(PVM pVM, PVMCPU pVCpu)
{
    int rc = nemR3DarwinCopyStateFromHv(pVM, pVCpu, CPUMCTX_EXTRN_ALL);
    if (RT_FAILURE(rc))
        return rc;

#ifdef LOG_ENABLED
    if (LogIs3Enabled())
        nemR3DarwinLogState(pVM, pVCpu);
#endif

    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitAll);

    hv_vcpu_exit_t *pExit = pVCpu->nem.s.pHvExit;
    switch (pExit->reason)
    {
        case HV_EXIT_REASON_CANCELED:
            return VINF_EM_RAW_INTERRUPT;
        case HV_EXIT_REASON_EXCEPTION:
            return nemR3DarwinHandleExitException(pVM, pVCpu, pExit);
        case HV_EXIT_REASON_VTIMER_ACTIVATED:
        {
            LogFlowFunc(("vTimer got activated\n"));
            TMCpuSetVTimerNextActivation(pVCpu, UINT64_MAX);
            pVCpu->nem.s.fVTimerActivated = true;
            return PDMGicSetPpi(pVCpu, pVM->nem.s.u32GicPpiVTimer, true /*fAsserted*/);
        }
        default:
            AssertReleaseFailed();
            break;
    }

    return VERR_INVALID_STATE;
}


/**
 * Runs the guest once until an exit occurs.
 *
 * @returns HV status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static hv_return_t nemR3DarwinRunGuest(PVM pVM, PVMCPU pVCpu)
{
    TMNotifyStartOfExecution(pVM, pVCpu);

    hv_return_t hrc = hv_vcpu_run(pVCpu->nem.s.hVCpu);

    TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());

    return hrc;
}


/**
 * Prepares the VM to run the guest.
 *
 * @returns Strict VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fSingleStepping     Flag whether we run in single stepping mode.
 */
static VBOXSTRICTRC nemR3DarwinPreRunGuest(PVM pVM, PVMCPU pVCpu, bool fSingleStepping)
{
#ifdef LOG_ENABLED
    bool fIrq = false;
    bool fFiq = false;

    if (LogIs3Enabled())
        nemR3DarwinLogState(pVM, pVCpu);
#endif

    int rc = nemR3DarwinExportGuestState(pVM, pVCpu);
    AssertRCReturn(rc, rc);

    /* In single stepping mode we will re-read SPSR and MDSCR and enable the software step bits. */
    if (fSingleStepping)
    {
        uint64_t u64Tmp;
        hv_return_t hrc = hv_vcpu_get_reg(pVCpu->nem.s.hVCpu, HV_REG_CPSR, &u64Tmp);
        if (hrc == HV_SUCCESS)
        {
            u64Tmp |= ARMV8_SPSR_EL2_AARCH64_SS;
            hrc = hv_vcpu_set_reg(pVCpu->nem.s.hVCpu, HV_REG_CPSR, u64Tmp);
        }

        hrc |= hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_MDSCR_EL1, &u64Tmp);
        if (hrc == HV_SUCCESS)
        {
            u64Tmp |= ARMV8_MDSCR_EL1_AARCH64_SS;
            hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_MDSCR_EL1, u64Tmp);
        }

        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
    }

    /* Check whether the vTimer interrupt was handled by the guest and we can unmask the vTimer. */
    if (pVCpu->nem.s.fVTimerActivated)
    {
        /* Read the CNTV_CTL_EL0 register. */
        uint64_t u64CntvCtl = 0;

        hv_return_t hrc = hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CTL_EL0, &u64CntvCtl);
        AssertRCReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);

        if (   (u64CntvCtl & (ARMV8_CNTV_CTL_EL0_AARCH64_ENABLE | ARMV8_CNTV_CTL_EL0_AARCH64_IMASK | ARMV8_CNTV_CTL_EL0_AARCH64_ISTATUS))
            != (ARMV8_CNTV_CTL_EL0_AARCH64_ENABLE | ARMV8_CNTV_CTL_EL0_AARCH64_ISTATUS))
        {
            /* Clear the interrupt. */
            PDMGicSetPpi(pVCpu, pVM->nem.s.u32GicPpiVTimer, false /*fAsserted*/);

            pVCpu->nem.s.fVTimerActivated = false;
            hrc = hv_vcpu_set_vtimer_mask(pVCpu->nem.s.hVCpu, false /*vtimer_is_masked*/);
            AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
        }
    }

    /* Set the pending interrupt state. */
    hv_return_t hrc = HV_SUCCESS;
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ))
    {
        hrc = hv_vcpu_set_pending_interrupt(pVCpu->nem.s.hVCpu, HV_INTERRUPT_TYPE_IRQ, true);
        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
#ifdef LOG_ENABLED
        fIrq = true;
#endif
    }
    else
    {
        hrc = hv_vcpu_set_pending_interrupt(pVCpu->nem.s.hVCpu, HV_INTERRUPT_TYPE_IRQ, false);
        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
    }

    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ))
    {
        hrc = hv_vcpu_set_pending_interrupt(pVCpu->nem.s.hVCpu, HV_INTERRUPT_TYPE_FIQ, true);
        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
#ifdef LOG_ENABLED
        fFiq = true;
#endif
    }
    else
    {
        hrc = hv_vcpu_set_pending_interrupt(pVCpu->nem.s.hVCpu, HV_INTERRUPT_TYPE_FIQ, false);
        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_IPE_9);
    }

    LogFlowFunc(("Running vCPU [%s,%s]\n", fIrq ? "I" : "nI", fFiq ? "F" : "nF"));
    pVCpu->nem.s.fEventPending = false;
    return VINF_SUCCESS;
}


/**
 * The normal runloop (no debugging features enabled).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC nemR3DarwinRunGuestNormal(PVM pVM, PVMCPU pVCpu)
{
    /*
     * The run loop.
     *
     * Current approach to state updating to use the sledgehammer and sync
     * everything every time.  This will be optimized later.
     */

    /* Update the vTimer offset after resuming if instructed. */
    if (pVCpu->nem.s.fVTimerOffUpdate)
    {
        hv_return_t hrc = hv_vcpu_set_vtimer_offset(pVCpu->nem.s.hVCpu, pVM->nem.s.u64VTimerOff);
        if (hrc != HV_SUCCESS)
            return nemR3DarwinHvSts2Rc(hrc);

        pVCpu->nem.s.fVTimerOffUpdate = false;

        hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CTL_EL0, pVCpu->cpum.GstCtx.CntvCtlEl0);
        if (hrc == HV_SUCCESS)
            hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CVAL_EL0, pVCpu->cpum.GstCtx.CntvCValEl0);
        if (hrc != HV_SUCCESS)
            return nemR3DarwinHvSts2Rc(hrc);
    }

    /*
     * Poll timers and run for a bit.
     */
    /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
     *        the whole polling job when timers have changed... */
    uint64_t       offDeltaIgnored;
    uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
    VBOXSTRICTRC   rcStrict       = VINF_SUCCESS;
    for (unsigned iLoop = 0;; iLoop++)
    {
        rcStrict = nemR3DarwinPreRunGuest(pVM, pVCpu, false /* fSingleStepping */);
        if (rcStrict != VINF_SUCCESS)
            break;

        hv_return_t hrc = nemR3DarwinRunGuest(pVM, pVCpu);
        if (hrc == HV_SUCCESS)
        {
            /*
             * Deal with the message.
             */
            rcStrict = nemR3DarwinHandleExit(pVM, pVCpu);
            if (rcStrict == VINF_SUCCESS)
            { /* hopefully likely */ }
            else
            {
                LogFlow(("NEM/%u: breaking: nemR3DarwinHandleExit -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                break;
            }
        }
        else
        {
            AssertLogRelMsgFailedReturn(("hv_vcpu_run()) failed for CPU #%u: %#x \n",
                                        pVCpu->idCpu, hrc), VERR_NEM_IPE_0);
        }
    } /* the run loop */

    return rcStrict;
}


/**
 * The debug runloop.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC nemR3DarwinRunGuestDebug(PVM pVM, PVMCPU pVCpu)
{
    /*
     * The run loop.
     *
     * Current approach to state updating to use the sledgehammer and sync
     * everything every time.  This will be optimized later.
     */

    bool const fSavedSingleInstruction = pVCpu->nem.s.fSingleInstruction;
    pVCpu->nem.s.fSingleInstruction    = pVCpu->nem.s.fSingleInstruction || DBGFIsStepping(pVCpu);
    pVCpu->nem.s.fUsingDebugLoop       = true;

    /* Trap any debug exceptions. */
    hv_return_t hrc = hv_vcpu_set_trap_debug_exceptions(pVCpu->nem.s.hVCpu, true);
    if (hrc != HV_SUCCESS)
        return VMSetError(pVM, VERR_NEM_SET_REGISTERS_FAILED, RT_SRC_POS,
                          "Trapping debug exceptions on vCPU %u failed: %#x (%Rrc)", pVCpu->idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));

    /* Update the vTimer offset after resuming if instructed. */
    if (pVCpu->nem.s.fVTimerOffUpdate)
    {
        hrc = hv_vcpu_set_vtimer_offset(pVCpu->nem.s.hVCpu, pVM->nem.s.u64VTimerOff);
        if (hrc != HV_SUCCESS)
            return nemR3DarwinHvSts2Rc(hrc);

        pVCpu->nem.s.fVTimerOffUpdate = false;

        hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CTL_EL0, pVCpu->cpum.GstCtx.CntvCtlEl0);
        if (hrc == HV_SUCCESS)
            hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CVAL_EL0, pVCpu->cpum.GstCtx.CntvCValEl0);
        if (hrc != HV_SUCCESS)
            return nemR3DarwinHvSts2Rc(hrc);
    }

    /* Save the guest MDSCR_EL1 */
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_SYSREG_DEBUG | CPUMCTX_EXTRN_PSTATE);
    uint64_t u64RegMdscrEl1 = pVCpu->cpum.GstCtx.Mdscr.u64;

    /*
     * Poll timers and run for a bit.
     */
    /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
     *        the whole polling job when timers have changed... */
    uint64_t       offDeltaIgnored;
    uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
    VBOXSTRICTRC    rcStrict        = VINF_SUCCESS;
    for (unsigned iLoop = 0;; iLoop++)
    {
        bool const fStepping = pVCpu->nem.s.fSingleInstruction;

        rcStrict = nemR3DarwinPreRunGuest(pVM, pVCpu, fStepping);
        if (rcStrict != VINF_SUCCESS)
            break;

        hrc = nemR3DarwinRunGuest(pVM, pVCpu);
        if (hrc == HV_SUCCESS)
        {
            /*
             * Deal with the message.
             */
            rcStrict = nemR3DarwinHandleExit(pVM, pVCpu);
            if (rcStrict == VINF_SUCCESS)
            { /* hopefully likely */ }
            else
            {
                LogFlow(("NEM/%u: breaking: nemR3DarwinHandleExit -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                break;
            }
        }
        else
        {
            AssertLogRelMsgFailedReturn(("hv_vcpu_run()) failed for CPU #%u: %#x \n",
                                        pVCpu->idCpu, hrc), VERR_NEM_IPE_0);
        }
    } /* the run loop */

    /* Restore single stepping state. */
    if (pVCpu->nem.s.fSingleInstruction)
    {
        /** @todo This ASSUMES that guest code being single stepped is not modifying the MDSCR_EL1 register. */
        CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_SYSREG_DEBUG | CPUMCTX_EXTRN_PSTATE);
        Assert(pVCpu->cpum.GstCtx.Mdscr.u64 & ARMV8_MDSCR_EL1_AARCH64_SS);

        pVCpu->cpum.GstCtx.Mdscr.u64 = u64RegMdscrEl1;
    }

    /* Restore debug exceptions trapping. */
    hrc |= hv_vcpu_set_trap_debug_exceptions(pVCpu->nem.s.hVCpu, false);
    if (hrc != HV_SUCCESS)
        return VMSetError(pVM, VERR_NEM_SET_REGISTERS_FAILED, RT_SRC_POS,
                          "Clearing trapping of debug exceptions on vCPU %u failed: %#x (%Rrc)", pVCpu->idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));

    pVCpu->nem.s.fUsingDebugLoop     = false;
    pVCpu->nem.s.fSingleInstruction  = fSavedSingleInstruction;

    return rcStrict;

}


VBOXSTRICTRC nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu)
{
#ifdef LOG_ENABLED
    if (LogIs3Enabled())
        nemR3DarwinLogState(pVM, pVCpu);
#endif

    AssertReturn(NEMR3CanExecuteGuest(pVM, pVCpu), VERR_NEM_IPE_9);

    if (RT_UNLIKELY(!pVCpu->nem.s.fIdRegsSynced))
    {
        /*
         * Sync the guest ID registers which are per VM once (they are readonly and stay constant during VM lifetime).
         * Need to do it here and not during the init because loading a saved state might change the ID registers from what
         * done in the call to CPUMR3PopulateFeaturesByIdRegisters().
         */
        static const struct
        {
            const char       *pszIdReg;
            hv_sys_reg_t     enmHvReg;
            uint32_t         offIdStruct;
        } s_aSysIdRegs[] =
        {
#define ID_SYS_REG_CREATE(a_IdReg, a_CpumIdReg) { #a_IdReg, HV_SYS_REG_##a_IdReg, RT_UOFFSETOF(CPUMARMV8IDREGS, a_CpumIdReg) }
            ID_SYS_REG_CREATE(ID_AA64DFR0_EL1,  u64RegIdAa64Dfr0El1),
            ID_SYS_REG_CREATE(ID_AA64DFR1_EL1,  u64RegIdAa64Dfr1El1),
            ID_SYS_REG_CREATE(ID_AA64ISAR0_EL1, u64RegIdAa64Isar0El1),
            ID_SYS_REG_CREATE(ID_AA64ISAR1_EL1, u64RegIdAa64Isar1El1),
            ID_SYS_REG_CREATE(ID_AA64MMFR0_EL1, u64RegIdAa64Mmfr0El1),
            ID_SYS_REG_CREATE(ID_AA64MMFR1_EL1, u64RegIdAa64Mmfr1El1),
            ID_SYS_REG_CREATE(ID_AA64MMFR2_EL1, u64RegIdAa64Mmfr2El1),
            ID_SYS_REG_CREATE(ID_AA64PFR0_EL1,  u64RegIdAa64Pfr0El1),
            ID_SYS_REG_CREATE(ID_AA64PFR1_EL1,  u64RegIdAa64Pfr1El1),
#undef ID_SYS_REG_CREATE
        };

        PCCPUMARMV8IDREGS pIdRegsGst = NULL;
        int rc = CPUMR3QueryGuestIdRegs(pVM, &pIdRegsGst);
        AssertRCReturn(rc, rc);

        for (uint32_t i = 0; i < RT_ELEMENTS(s_aSysIdRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)pIdRegsGst + s_aSysIdRegs[i].offIdStruct);
            hv_return_t hrc = hv_vcpu_set_sys_reg(pVCpu->nem.s.hVCpu, s_aSysIdRegs[i].enmHvReg, *pu64);
            if (hrc != HV_SUCCESS)
                return VMSetError(pVM, VERR_NEM_SET_REGISTERS_FAILED, RT_SRC_POS,
                                  "Setting %s failed on vCPU %u: %#x (%Rrc)", s_aSysIdRegs[i].pszIdReg, pVCpu->idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));
        }

        pVCpu->nem.s.fIdRegsSynced = true;
    }

    /*
     * Try switch to NEM runloop state.
     */
    if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED))
    { /* likely */ }
    else
    {
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);
        LogFlow(("NEM/%u: returning immediately because canceled\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rcStrict;
    if (   !pVCpu->nem.s.fUseDebugLoop
        /*&& !nemR3DarwinAnyExpensiveProbesEnabled()*/
        && !DBGFIsStepping(pVCpu)
        && !pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledSwBreakpoints)
        rcStrict = nemR3DarwinRunGuestNormal(pVM, pVCpu);
    else
        rcStrict = nemR3DarwinRunGuestDebug(pVM, pVCpu);

    if (rcStrict == VINF_EM_RAW_TO_R3)
        rcStrict = VINF_SUCCESS;

    /*
     * Convert any pending HM events back to TRPM due to premature exits.
     *
     * This is because execution may continue from IEM and we would need to inject
     * the event from there (hence place it back in TRPM).
     */
    if (pVCpu->nem.s.fEventPending)
    {
        /** @todo */
    }


    if (!VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM))
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);

    if (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL))
    {
        /* Try anticipate what we might need. */
        uint64_t fImport = NEM_DARWIN_CPUMCTX_EXTRN_MASK_FOR_IEM;
        if (   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
            || RT_FAILURE(rcStrict))
            fImport = CPUMCTX_EXTRN_ALL;
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ
                                          | VMCPU_FF_INTERRUPT_NMI | VMCPU_FF_INTERRUPT_SMI))
            fImport |= IEM_CPUMCTX_EXTRN_XCPT_MASK;

        if (pVCpu->cpum.GstCtx.fExtrn & fImport)
        {
            /* Only import what is external currently. */
            int rc2 = nemR3DarwinCopyStateFromHv(pVM, pVCpu, fImport);
            if (RT_SUCCESS(rc2))
                pVCpu->cpum.GstCtx.fExtrn &= ~fImport;
            else if (RT_SUCCESS(rcStrict))
                rcStrict = rc2;
            if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
                pVCpu->cpum.GstCtx.fExtrn = 0;
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturn);
        }
        else
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }
    else
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
        pVCpu->cpum.GstCtx.fExtrn = 0;
    }

    return rcStrict;
}


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM, pVCpu);
    return true; /** @todo Are there any cases where we have to emulate? */
}


bool nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    VMCPU_ASSERT_EMT(pVCpu);
    bool fOld = pVCpu->nem.s.fSingleInstruction;
    pVCpu->nem.s.fSingleInstruction = fEnable;
    pVCpu->nem.s.fUseDebugLoop = fEnable || pVM->nem.s.fUseDebugLoop;
    return fOld;
}


void nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    LogFlowFunc(("pVM=%p pVCpu=%p fFlags=%#x\n", pVM, pVCpu, fFlags));

    RT_NOREF(pVM, fFlags);

    hv_return_t hrc = hv_vcpus_exit(&pVCpu->nem.s.hVCpu, 1);
    if (hrc != HV_SUCCESS)
        LogRel(("NEM: hv_vcpus_exit(%u, 1) failed with %#x\n", pVCpu->nem.s.hVCpu, hrc));
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    RT_NOREF(pVM, fUseDebugLoop);
    //AssertReleaseFailed();
    return false;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu, fUseDebugLoop);
    return fUseDebugLoop;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysRamRegister(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvR3,
                                               uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pVM, puNemRange);

    Log5(("NEMR3NotifyPhysRamRegister: %RGp LB %RGp, pvR3=%p\n", GCPhys, cb, pvR3));
#if defined(VBOX_WITH_PGM_NEM_MODE)
    if (pvR3)
    {
        int rc = nemR3DarwinMap(pVM, GCPhys, pvR3, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE | NEM_PAGE_PROT_EXECUTE, pu2State);
        if (RT_FAILURE(rc))
        {
            LogRel(("NEMR3NotifyPhysRamRegister: GCPhys=%RGp LB %RGp pvR3=%p rc=%Rrc\n", GCPhys, cb, pvR3, rc));
            return VERR_NEM_MAP_PAGES_FAILED;
        }
    }
    return VINF_SUCCESS;
#else
    RT_NOREF(pVM, GCPhys, cb, pvR3);
    return VERR_NEM_MAP_PAGES_FAILED;
#endif
}


VMMR3_INT_DECL(bool) NEMR3IsMmio2DirtyPageTrackingSupported(PVM pVM)
{
    RT_NOREF(pVM);
    return true;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExMapEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                  void *pvRam, void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pvRam);

    Log5(("NEMR3NotifyPhysMmioExMapEarly: %RGp LB %RGp fFlags=%#x pvRam=%p pvMmio2=%p pu2State=%p (%d)\n",
          GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State, *pu2State));

#if defined(VBOX_WITH_PGM_NEM_MODE)
    /*
     * Unmap the RAM we're replacing.
     */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE)
    {
        int rc = nemR3DarwinUnmap(pVM, GCPhys, cb, pu2State);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else if (pvMmio2)
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> rc=%Rc(ignored)\n",
                    GCPhys, cb, fFlags, rc));
        else
        {
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> rc=%Rrc\n",
                    GCPhys, cb, fFlags, rc));
            return VERR_NEM_UNMAP_PAGES_FAILED;
        }
    }

    /*
     * Map MMIO2 if any.
     */
    if (pvMmio2)
    {
        Assert(fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2);

        /* We need to set up our own dirty tracking due to Hypervisor.framework only working on host page sized aligned regions. */
        uint32_t fProt = NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE;
        if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_TRACK_DIRTY_PAGES)
        {
            /* Find a slot for dirty tracking. */
            PNEMHVMMIO2REGION pMmio2Region = NULL;
            uint32_t idSlot;
            for (idSlot = 0; idSlot < RT_ELEMENTS(pVM->nem.s.aMmio2DirtyTracking); idSlot++)
            {
                if (   pVM->nem.s.aMmio2DirtyTracking[idSlot].GCPhysStart == 0
                    && pVM->nem.s.aMmio2DirtyTracking[idSlot].GCPhysLast == 0)
                {
                    pMmio2Region = &pVM->nem.s.aMmio2DirtyTracking[idSlot];
                    break;
                }
            }

            if (!pMmio2Region)
            {
                LogRel(("NEMR3NotifyPhysMmioExMapEarly: Out of dirty tracking structures -> VERR_NEM_MAP_PAGES_FAILED\n"));
                return VERR_NEM_MAP_PAGES_FAILED;
            }

            pMmio2Region->GCPhysStart = GCPhys;
            pMmio2Region->GCPhysLast  = GCPhys + cb - 1;
            pMmio2Region->fDirty      = false;
            *puNemRange = idSlot;
        }
        else
            fProt |= NEM_PAGE_PROT_WRITE;

        int rc = nemR3DarwinMap(pVM, GCPhys, pvMmio2, cb, fProt, pu2State);
        if (RT_FAILURE(rc))
        {
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x pvMmio2=%p: Map -> rc=%Rrc\n",
                    GCPhys, cb, fFlags, pvMmio2, rc));
            return VERR_NEM_MAP_PAGES_FAILED;
        }
    }
    else
        Assert(!(fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2));

#else
    RT_NOREF(pVM, GCPhys, cb, pvRam, pvMmio2);
    *pu2State = (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE) ? UINT8_MAX : NEM_DARWIN_PAGE_STATE_UNMAPPED;
#endif
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExMapLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                 void *pvRam, void *pvMmio2, uint32_t *puNemRange)
{
    RT_NOREF(pVM, GCPhys, cb, fFlags, pvRam, pvMmio2, puNemRange);
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExUnmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags, void *pvRam,
                                               void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pVM, puNemRange);

    Log5(("NEMR3NotifyPhysMmioExUnmap: %RGp LB %RGp fFlags=%#x pvRam=%p pvMmio2=%p pu2State=%p uNemRange=%#x (%#x)\n",
          GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State, puNemRange, *puNemRange));

    int rc = VINF_SUCCESS;
#if defined(VBOX_WITH_PGM_NEM_MODE)
    /*
     * Unmap the MMIO2 pages.
     */
    /** @todo If we implement aliasing (MMIO2 page aliased into MMIO range),
     *        we may have more stuff to unmap even in case of pure MMIO... */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2)
    {
        rc = nemR3DarwinUnmap(pVM, GCPhys, cb, pu2State);
        if (RT_FAILURE(rc))
        {
            LogRel2(("NEMR3NotifyPhysMmioExUnmap: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> rc=%Rrc\n",
                     GCPhys, cb, fFlags, rc));
            rc = VERR_NEM_UNMAP_PAGES_FAILED;
        }

        if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_TRACK_DIRTY_PAGES)
        {
            /* Reset tracking structure. */
            uint32_t idSlot = *puNemRange;
            *puNemRange = UINT32_MAX;

            Assert(idSlot < RT_ELEMENTS(pVM->nem.s.aMmio2DirtyTracking));
            pVM->nem.s.aMmio2DirtyTracking[idSlot].GCPhysStart = 0;
            pVM->nem.s.aMmio2DirtyTracking[idSlot].GCPhysLast  = 0;
            pVM->nem.s.aMmio2DirtyTracking[idSlot].fDirty      = false;
        }
    }

    /* Ensure the page is masked as unmapped if relevant. */
    Assert(!pu2State || *pu2State == NEM_DARWIN_PAGE_STATE_UNMAPPED);

    /*
     * Restore the RAM we replaced.
     */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE)
    {
        AssertPtr(pvRam);
        rc = nemR3DarwinMap(pVM, GCPhys, pvRam, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE | NEM_PAGE_PROT_EXECUTE, pu2State);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            LogRel(("NEMR3NotifyPhysMmioExUnmap: GCPhys=%RGp LB %RGp pvMmio2=%p rc=%Rrc\n", GCPhys, cb, pvMmio2, rc));
            rc = VERR_NEM_MAP_PAGES_FAILED;
        }
    }

    RT_NOREF(pvMmio2);
#else
    RT_NOREF(pVM, GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State);
    if (pu2State)
        *pu2State = UINT8_MAX;
    rc = VERR_NEM_UNMAP_PAGES_FAILED;
#endif
    return rc;
}


VMMR3_INT_DECL(int) NEMR3PhysMmio2QueryAndResetDirtyBitmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t uNemRange,
                                                           void *pvBitmap, size_t cbBitmap)
{
    LogFlowFunc(("NEMR3PhysMmio2QueryAndResetDirtyBitmap: %RGp LB %RGp UnemRange=%u\n", GCPhys, cb, uNemRange));
    Assert(uNemRange < RT_ELEMENTS(pVM->nem.s.aMmio2DirtyTracking));

    /* Keep it simple for now and mark everything as dirty if it is. */
    int rc = VINF_SUCCESS;
    if (pVM->nem.s.aMmio2DirtyTracking[uNemRange].fDirty)
    {
        ASMBitSetRange(pvBitmap, 0, cbBitmap * 8);

        pVM->nem.s.aMmio2DirtyTracking[uNemRange].fDirty = false;
        /* Restore as RX only. */
        uint8_t u2State;
        rc = nemR3DarwinProtect(GCPhys, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE, &u2State);
    }
    else
        ASMBitClearRange(pvBitmap, 0, cbBitmap * 8);

    return rc;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages, uint32_t fFlags,
                                                     uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pVM, GCPhys, cb, pvPages, fFlags, puNemRange);

    Log5(("NEMR3NotifyPhysRomRegisterEarly: %RGp LB %RGp pvPages=%p fFlags=%#x\n", GCPhys, cb, pvPages, fFlags));
    *pu2State   = UINT8_MAX;
    *puNemRange = 0;
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages,
                                                    uint32_t fFlags, uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("NEMR3NotifyPhysRomRegisterLate: %RGp LB %RGp pvPages=%p fFlags=%#x pu2State=%p (%d) puNemRange=%p (%#x)\n",
          GCPhys, cb, pvPages, fFlags, pu2State, *pu2State, puNemRange, *puNemRange));
    *pu2State = UINT8_MAX;

#if defined(VBOX_WITH_PGM_NEM_MODE)
    /*
     * (Re-)map readonly.
     */
    AssertPtrReturn(pvPages, VERR_INVALID_POINTER);

    int rc = nemR3DarwinUnmap(pVM, GCPhys, cb, pu2State);
    AssertRC(rc);

    rc = nemR3DarwinMap(pVM, GCPhys, pvPages, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE, pu2State);
    if (RT_FAILURE(rc))
    {
        LogRel(("nemR3NativeNotifyPhysRomRegisterLate: GCPhys=%RGp LB %RGp pvPages=%p fFlags=%#x rc=%Rrc\n",
                GCPhys, cb, pvPages, fFlags, rc));
        return VERR_NEM_MAP_PAGES_FAILED;
    }
    RT_NOREF(fFlags, puNemRange);
    return VINF_SUCCESS;
#else
    RT_NOREF(pVM, GCPhys, cb, pvPages, fFlags, puNemRange);
    return VERR_NEM_MAP_PAGES_FAILED;
#endif
}


VMM_INT_DECL(void) NEMHCNotifyHandlerPhysicalDeregister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb,
                                                        RTR3PTR pvMemR3, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyHandlerPhysicalDeregister: %RGp LB %RGp enmKind=%d pvMemR3=%p pu2State=%p (%d)\n",
          GCPhys, cb, enmKind, pvMemR3, pu2State, *pu2State));

    *pu2State = UINT8_MAX;
#if defined(VBOX_WITH_PGM_NEM_MODE)
    if (pvMemR3)
    {
        /* Unregister what was there before. */
        int rc = nemR3DarwinUnmap(pVM, GCPhys, cb, pu2State);
        AssertRC(rc);

        rc = nemR3DarwinMap(pVM, GCPhys, pvMemR3, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE | NEM_PAGE_PROT_EXECUTE, pu2State);
        AssertLogRelMsgRC(rc, ("NEMHCNotifyHandlerPhysicalDeregister: nemR3DarwinMap(,%p,%RGp,%RGp,) -> %Rrc\n",
                          pvMemR3, GCPhys, cb, rc));
    }
    RT_NOREF(enmKind);
#else
    RT_NOREF(pVM, enmKind, GCPhys, cb, pvMemR3);
    AssertFailed();
#endif
}


VMMR3_INT_DECL(void) NEMR3NotifySetA20(PVMCPU pVCpu, bool fEnabled)
{
    Log(("NEMR3NotifySetA20: fEnabled=%RTbool\n", fEnabled));
    RT_NOREF(pVCpu, fEnabled);
}


void nemHCNativeNotifyHandlerPhysicalRegister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalRegister: %RGp LB %RGp enmKind=%d\n", GCPhys, cb, enmKind));
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhys); NOREF(cb);
}


void nemHCNativeNotifyHandlerPhysicalModify(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhysOld,
                                            RTGCPHYS GCPhysNew, RTGCPHYS cb, bool fRestoreAsRAM)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalModify: %RGp LB %RGp -> %RGp enmKind=%d fRestoreAsRAM=%d\n",
          GCPhysOld, cb, GCPhysNew, enmKind, fRestoreAsRAM));
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhysOld); NOREF(GCPhysNew); NOREF(cb); NOREF(fRestoreAsRAM);
}


int nemHCNativeNotifyPhysPageAllocated(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint32_t fPageProt,
                                       PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageAllocated: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    RT_NOREF(pVM, GCPhys, HCPhys, fPageProt, enmType, pu2State);

    AssertFailed();
    return VINF_SUCCESS;
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, RTR3PTR pvR3, uint32_t fPageProt,
                                                  PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageProtChanged: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    RT_NOREF(pVM, GCPhys, HCPhys, pvR3, fPageProt, enmType, pu2State);
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev, RTHCPHYS HCPhysNew,
                                              RTR3PTR pvNewR3, uint32_t fPageProt, PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageChanged: %RGp HCPhys=%RHp->%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhysPrev, HCPhysNew, fPageProt, enmType, *pu2State));
    RT_NOREF(pVM, GCPhys, HCPhysPrev, HCPhysNew, pvNewR3, fPageProt, enmType, pu2State);

    AssertFailed();
}


/**
 * Interface for importing state on demand (used by IEM).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   fWhat       What to import, CPUMCTX_EXTRN_XXX.
 */
VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    LogFlowFunc(("pVCpu=%p fWhat=%RX64\n", pVCpu, fWhat));
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnDemand);

    return nemR3DarwinCopyStateFromHv(pVCpu->pVMR3, pVCpu, fWhat);
}


/**
 * Query the CPU tick counter and optionally the TSC_AUX MSR value.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   pcTicks     Where to return the CPU tick count.
 * @param   puAux       Where to return the TSC_AUX register value.
 */
VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, uint64_t *pcTicks, uint32_t *puAux)
{
    LogFlowFunc(("pVCpu=%p pcTicks=%RX64 puAux=%RX32\n", pVCpu, pcTicks, puAux));
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatQueryCpuTick);

    if (puAux)
        *puAux = 0;
    *pcTicks = mach_absolute_time() - pVCpu->pVMR3->nem.s.u64VTimerOff; /* This is the host timer minus the offset. */
    return VINF_SUCCESS;
}


/**
 * Resumes CPU clock (TSC) on all virtual CPUs.
 *
 * This is called by TM when the VM is started, restored, resumed or similar.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context CPU structure of the calling EMT.
 * @param   uPausedTscValue The TSC value at the time of pausing.
 */
VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uPausedTscValue)
{
    LogFlowFunc(("pVM=%p pVCpu=%p uPausedTscValue=%RX64\n", pVM, pVCpu, uPausedTscValue));
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(VM_IS_NEM_ENABLED(pVM), VERR_NEM_IPE_9);

    /*
     * Calculate the new offset, first get the new TSC value with the old vTimer offset and then adjust the
     * the new offset to let the guest not notice the pause.
     */
    uint64_t u64TscNew = mach_absolute_time() - pVCpu->pVMR3->nem.s.u64VTimerOff;
    Assert(u64TscNew >= uPausedTscValue);
    LogFlowFunc(("u64VTimerOffOld=%#RX64 u64TscNew=%#RX64 u64VTimerValuePaused=%#RX64 -> u64VTimerOff=%#RX64\n",
                 pVM->nem.s.u64VTimerOff, u64TscNew, uPausedTscValue,
                 pVM->nem.s.u64VTimerOff + (u64TscNew - uPausedTscValue)));

    pVM->nem.s.u64VTimerOff += u64TscNew - uPausedTscValue;

    /*
     * Set the flag to update the vTimer offset when the vCPU resumes for the first time
     * (needs to be done on the actual EMT).
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpuDst = pVM->apCpusR3[idCpu];
        pVCpuDst->nem.s.fVTimerOffUpdate = true;
    }

    return VINF_SUCCESS;
}


/**
 * Returns features supported by the NEM backend.
 *
 * @returns Flags of features supported by the native NEM backend.
 * @param   pVM             The cross context VM structure.
 */
VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
    RT_NOREF(pVM);
    /*
     * Apple's Hypervisor.framework is not supported if the CPU doesn't support nested paging
     * and unrestricted guest execution support so we can safely return these flags here always.
     */
    return NEM_FEAT_F_NESTED_PAGING | NEM_FEAT_F_FULL_GST_EXEC | NEM_FEAT_F_XSAVE_XRSTOR;
}


/** @page pg_nem_darwin NEM/darwin - Native Execution Manager, macOS.
 *
 * @todo Add notes as the implementation progresses...
 */

