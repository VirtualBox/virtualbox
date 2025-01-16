/* $Id: NEMR3Native-darwin.cpp 107891 2025-01-16 00:33:11Z knut.osmundsen@oracle.com $ */
/** @file
 * NEM - Native execution manager, native ring-3 macOS backend using Hypervisor.framework.
 *
 * Log group 2: Exit logging.
 * Log group 3: Log context on exit.
 * Log group 5: Ring-3 memory management
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define CPUM_WITH_NONCONST_HOST_FEATURES /* required for initializing parts of the g_CpumHostFeatures structure here. */
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hm_vmx.h>
#include <VBox/vmm/dbgftrace.h>
#include <VBox/vmm/gcm.h>
#include "VMXInternal.h"
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "dtrace/VBoxVMM.h"

#include <iprt/asm.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#include <mach/mach_time.h>
#include <mach/kern_return.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* No nested hwvirt (for now). */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# undef VBOX_WITH_NESTED_HWVIRT_VMX
#endif


/** @name HV return codes.
 * @{ */
/** Operation was successful. */
#define HV_SUCCESS              0
/** An error occurred during operation. */
#define HV_ERROR                0xfae94001
/** The operation could not be completed right now, try again. */
#define HV_BUSY                 0xfae94002
/** One of the parameters passed wis invalid. */
#define HV_BAD_ARGUMENT         0xfae94003
/** Not enough resources left to fulfill the operation. */
#define HV_NO_RESOURCES         0xfae94005
/** The device could not be found. */
#define HV_NO_DEVICE            0xfae94006
/** The operation is not supportd on this platform with this configuration. */
#define HV_UNSUPPORTED          0xfae94007
/** @} */


/** @name HV memory protection flags.
 * @{ */
/** Memory is readable. */
#define HV_MEMORY_READ          RT_BIT_64(0)
/** Memory is writeable. */
#define HV_MEMORY_WRITE         RT_BIT_64(1)
/** Memory is executable. */
#define HV_MEMORY_EXEC          RT_BIT_64(2)
/** @} */


/** @name HV shadow VMCS protection flags.
 * @{ */
/** Shadow VMCS field is not accessible. */
#define HV_SHADOW_VMCS_NONE     0
/** Shadow VMCS fild is readable. */
#define HV_SHADOW_VMCS_READ     RT_BIT_64(0)
/** Shadow VMCS field is writeable. */
#define HV_SHADOW_VMCS_WRITE    RT_BIT_64(1)
/** @} */


/** Default VM creation flags. */
#define HV_VM_DEFAULT           0
/** Default guest address space creation flags. */
#define HV_VM_SPACE_DEFAULT     0
/** Default vCPU creation flags. */
#define HV_VCPU_DEFAULT         0

#define HV_DEADLINE_FOREVER     UINT64_MAX


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** HV return code type. */
typedef uint32_t hv_return_t;
/** HV capability bitmask. */
typedef uint64_t hv_capability_t;
/** Option bitmask type when creating a VM. */
typedef uint64_t hv_vm_options_t;
/** Option bitmask when creating a vCPU. */
typedef uint64_t hv_vcpu_options_t;
/** HV memory protection flags type. */
typedef uint64_t hv_memory_flags_t;
/** Shadow VMCS protection flags. */
typedef uint64_t hv_shadow_flags_t;
/** Guest physical address type. */
typedef uint64_t hv_gpaddr_t;


/**
 * VMX Capability enumeration.
 */
typedef enum
{
    HV_VMX_CAP_PINBASED = 0,
    HV_VMX_CAP_PROCBASED,
    HV_VMX_CAP_PROCBASED2,
    HV_VMX_CAP_ENTRY,
    HV_VMX_CAP_EXIT,
    HV_VMX_CAP_BASIC,                   /* Since 11.0 */
    HV_VMX_CAP_TRUE_PINBASED,           /* Since 11.0 */
    HV_VMX_CAP_TRUE_PROCBASED,          /* Since 11.0 */
    HV_VMX_CAP_TRUE_ENTRY,              /* Since 11.0 */
    HV_VMX_CAP_TRUE_EXIT,               /* Since 11.0 */
    HV_VMX_CAP_MISC,                    /* Since 11.0 */
    HV_VMX_CAP_CR0_FIXED0,              /* Since 11.0 */
    HV_VMX_CAP_CR0_FIXED1,              /* Since 11.0 */
    HV_VMX_CAP_CR4_FIXED0,              /* Since 11.0 */
    HV_VMX_CAP_CR4_FIXED1,              /* Since 11.0 */
    HV_VMX_CAP_VMCS_ENUM,               /* Since 11.0 */
    HV_VMX_CAP_EPT_VPID_CAP,            /* Since 11.0 */
    HV_VMX_CAP_PREEMPTION_TIMER = 32
} hv_vmx_capability_t;


/**
 * MSR information.
 */
typedef enum
{
    HV_VMX_INFO_MSR_IA32_ARCH_CAPABILITIES = 0,
    HV_VMX_INFO_MSR_IA32_PERF_CAPABILITIES,
    HV_VMX_VALID_MSR_IA32_PERFEVNTSEL,
    HV_VMX_VALID_MSR_IA32_FIXED_CTR_CTRL,
    HV_VMX_VALID_MSR_IA32_PERF_GLOBAL_CTRL,
    HV_VMX_VALID_MSR_IA32_PERF_GLOBAL_STATUS,
    HV_VMX_VALID_MSR_IA32_DEBUGCTL,
    HV_VMX_VALID_MSR_IA32_SPEC_CTRL,
    HV_VMX_NEED_MSR_IA32_SPEC_CTRL
} hv_vmx_msr_info_t;


/**
 * HV x86 register enumeration.
 */
typedef enum
{
    HV_X86_RIP = 0,
    HV_X86_RFLAGS,
    HV_X86_RAX,
    HV_X86_RCX,
    HV_X86_RDX,
    HV_X86_RBX,
    HV_X86_RSI,
    HV_X86_RDI,
    HV_X86_RSP,
    HV_X86_RBP,
    HV_X86_R8,
    HV_X86_R9,
    HV_X86_R10,
    HV_X86_R11,
    HV_X86_R12,
    HV_X86_R13,
    HV_X86_R14,
    HV_X86_R15,
    HV_X86_CS,
    HV_X86_SS,
    HV_X86_DS,
    HV_X86_ES,
    HV_X86_FS,
    HV_X86_GS,
    HV_X86_IDT_BASE,
    HV_X86_IDT_LIMIT,
    HV_X86_GDT_BASE,
    HV_X86_GDT_LIMIT,
    HV_X86_LDTR,
    HV_X86_LDT_BASE,
    HV_X86_LDT_LIMIT,
    HV_X86_LDT_AR,
    HV_X86_TR,
    HV_X86_TSS_BASE,
    HV_X86_TSS_LIMIT,
    HV_X86_TSS_AR,
    HV_X86_CR0,
    HV_X86_CR1,
    HV_X86_CR2,
    HV_X86_CR3,
    HV_X86_CR4,
    HV_X86_DR0,
    HV_X86_DR1,
    HV_X86_DR2,
    HV_X86_DR3,
    HV_X86_DR4,
    HV_X86_DR5,
    HV_X86_DR6,
    HV_X86_DR7,
    HV_X86_TPR,
    HV_X86_XCR0,
    HV_X86_REGISTERS_MAX
} hv_x86_reg_t;


/** MSR permission flags type. */
typedef uint32_t hv_msr_flags_t;
/** MSR can't be accessed. */
#define HV_MSR_NONE     0
/** MSR is readable by the guest. */
#define HV_MSR_READ     RT_BIT(0)
/** MSR is writeable by the guest. */
#define HV_MSR_WRITE    RT_BIT(1)


typedef hv_return_t FN_HV_CAPABILITY(hv_capability_t capability, uint64_t *valu);
typedef hv_return_t FN_HV_VM_CREATE(hv_vm_options_t flags);
typedef hv_return_t FN_HV_VM_DESTROY(void);
typedef hv_return_t FN_HV_VM_SPACE_CREATE(hv_vm_space_t *asid);
typedef hv_return_t FN_HV_VM_SPACE_DESTROY(hv_vm_space_t asid);
typedef hv_return_t FN_HV_VM_MAP(const void *uva, hv_gpaddr_t gpa, size_t size, hv_memory_flags_t flags);
typedef hv_return_t FN_HV_VM_UNMAP(hv_gpaddr_t gpa, size_t size);
typedef hv_return_t FN_HV_VM_PROTECT(hv_gpaddr_t gpa, size_t size, hv_memory_flags_t flags);
typedef hv_return_t FN_HV_VM_MAP_SPACE(hv_vm_space_t asid, const void *uva, hv_gpaddr_t gpa, size_t size, hv_memory_flags_t flags);
typedef hv_return_t FN_HV_VM_UNMAP_SPACE(hv_vm_space_t asid, hv_gpaddr_t gpa, size_t size);
typedef hv_return_t FN_HV_VM_PROTECT_SPACE(hv_vm_space_t asid, hv_gpaddr_t gpa, size_t size, hv_memory_flags_t flags);
typedef hv_return_t FN_HV_VM_SYNC_TSC(uint64_t tsc);

typedef hv_return_t FN_HV_VCPU_CREATE(hv_vcpuid_t *vcpu, hv_vcpu_options_t flags);
typedef hv_return_t FN_HV_VCPU_DESTROY(hv_vcpuid_t vcpu);
typedef hv_return_t FN_HV_VCPU_SET_SPACE(hv_vcpuid_t vcpu, hv_vm_space_t asid);
typedef hv_return_t FN_HV_VCPU_READ_REGISTER(hv_vcpuid_t vcpu, hv_x86_reg_t reg, uint64_t *value);
typedef hv_return_t FN_HV_VCPU_WRITE_REGISTER(hv_vcpuid_t vcpu, hv_x86_reg_t reg, uint64_t value);
typedef hv_return_t FN_HV_VCPU_READ_FPSTATE(hv_vcpuid_t vcpu, void *buffer, size_t size);
typedef hv_return_t FN_HV_VCPU_WRITE_FPSTATE(hv_vcpuid_t vcpu, const void *buffer, size_t size);
typedef hv_return_t FN_HV_VCPU_ENABLE_NATIVE_MSR(hv_vcpuid_t vcpu, uint32_t msr, bool enable);
typedef hv_return_t FN_HV_VCPU_READ_MSR(hv_vcpuid_t vcpu, uint32_t msr, uint64_t *value);
typedef hv_return_t FN_HV_VCPU_WRITE_MSR(hv_vcpuid_t vcpu, uint32_t msr, uint64_t value);
typedef hv_return_t FN_HV_VCPU_FLUSH(hv_vcpuid_t vcpu);
typedef hv_return_t FN_HV_VCPU_INVALIDATE_TLB(hv_vcpuid_t vcpu);
typedef hv_return_t FN_HV_VCPU_RUN(hv_vcpuid_t vcpu);
typedef hv_return_t FN_HV_VCPU_RUN_UNTIL(hv_vcpuid_t vcpu, uint64_t deadline);
typedef hv_return_t FN_HV_VCPU_INTERRUPT(hv_vcpuid_t *vcpus, unsigned int vcpu_count);
typedef hv_return_t FN_HV_VCPU_GET_EXEC_TIME(hv_vcpuid_t *vcpus, uint64_t *time);

typedef hv_return_t FN_HV_VMX_VCPU_READ_VMCS(hv_vcpuid_t vcpu, uint32_t field, uint64_t *value);
typedef hv_return_t FN_HV_VMX_VCPU_WRITE_VMCS(hv_vcpuid_t vcpu, uint32_t field, uint64_t value);

typedef hv_return_t FN_HV_VMX_VCPU_READ_SHADOW_VMCS(hv_vcpuid_t vcpu, uint32_t field, uint64_t *value);
typedef hv_return_t FN_HV_VMX_VCPU_WRITE_SHADOW_VMCS(hv_vcpuid_t vcpu, uint32_t field, uint64_t value);
typedef hv_return_t FN_HV_VMX_VCPU_SET_SHADOW_ACCESS(hv_vcpuid_t vcpu, uint32_t field, hv_shadow_flags_t flags);

typedef hv_return_t FN_HV_VMX_READ_CAPABILITY(hv_vmx_capability_t field, uint64_t *value);
typedef hv_return_t FN_HV_VMX_VCPU_SET_APIC_ADDRESS(hv_vcpuid_t vcpu, hv_gpaddr_t gpa);

/* Since 11.0 */
typedef hv_return_t FN_HV_VMX_GET_MSR_INFO(hv_vmx_msr_info_t field, uint64_t *value);
typedef hv_return_t FN_HV_VMX_VCPU_GET_CAP_WRITE_VMCS(hv_vcpuid_t vcpu, uint32_t field, uint64_t *allowed_0, uint64_t *allowed_1);
typedef hv_return_t FN_HV_VCPU_ENABLE_MANAGED_MSR(hv_vcpuid_t vcpu, uint32_t msr, bool enable);
typedef hv_return_t FN_HV_VCPU_SET_MSR_ACCESS(hv_vcpuid_t vcpu, uint32_t msr, hv_msr_flags_t flags);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static void nemR3DarwinVmcsDump(PVMCPU pVCpu);

/** NEM_DARWIN_PAGE_STATE_XXX names. */
NEM_TMPL_STATIC const char * const g_apszPageStates[4] = { "not-set", "unmapped", "readable", "writable" };
/** MSRs. */
static SUPHWVIRTMSRS    g_HmMsrs;
/** VMX: Set if swapping EFER is supported.  */
static bool             g_fHmVmxSupportsVmcsEfer = false;
/** @name APIs imported from Hypervisor.framework.
 * @{ */
static FN_HV_CAPABILITY                 *g_pfnHvCapability              = NULL; /* Since 10.15 */
static FN_HV_VM_CREATE                  *g_pfnHvVmCreate                = NULL; /* Since 10.10 */
static FN_HV_VM_DESTROY                 *g_pfnHvVmDestroy               = NULL; /* Since 10.10 */
static FN_HV_VM_SPACE_CREATE            *g_pfnHvVmSpaceCreate           = NULL; /* Since 10.15 */
static FN_HV_VM_SPACE_DESTROY           *g_pfnHvVmSpaceDestroy          = NULL; /* Since 10.15 */
static FN_HV_VM_MAP                     *g_pfnHvVmMap                   = NULL; /* Since 10.10 */
static FN_HV_VM_UNMAP                   *g_pfnHvVmUnmap                 = NULL; /* Since 10.10 */
static FN_HV_VM_PROTECT                 *g_pfnHvVmProtect               = NULL; /* Since 10.10 */
static FN_HV_VM_MAP_SPACE               *g_pfnHvVmMapSpace              = NULL; /* Since 10.15 */
static FN_HV_VM_UNMAP_SPACE             *g_pfnHvVmUnmapSpace            = NULL; /* Since 10.15 */
static FN_HV_VM_PROTECT_SPACE           *g_pfnHvVmProtectSpace          = NULL; /* Since 10.15 */
static FN_HV_VM_SYNC_TSC                *g_pfnHvVmSyncTsc               = NULL; /* Since 10.10 */

static FN_HV_VCPU_CREATE                *g_pfnHvVCpuCreate              = NULL; /* Since 10.10 */
static FN_HV_VCPU_DESTROY               *g_pfnHvVCpuDestroy             = NULL; /* Since 10.10 */
static FN_HV_VCPU_SET_SPACE             *g_pfnHvVCpuSetSpace            = NULL; /* Since 10.15 */
static FN_HV_VCPU_READ_REGISTER         *g_pfnHvVCpuReadRegister        = NULL; /* Since 10.10 */
static FN_HV_VCPU_WRITE_REGISTER        *g_pfnHvVCpuWriteRegister       = NULL; /* Since 10.10 */
static FN_HV_VCPU_READ_FPSTATE          *g_pfnHvVCpuReadFpState         = NULL; /* Since 10.10 */
static FN_HV_VCPU_WRITE_FPSTATE         *g_pfnHvVCpuWriteFpState        = NULL; /* Since 10.10 */
static FN_HV_VCPU_ENABLE_NATIVE_MSR     *g_pfnHvVCpuEnableNativeMsr     = NULL; /* Since 10.10 */
static FN_HV_VCPU_READ_MSR              *g_pfnHvVCpuReadMsr             = NULL; /* Since 10.10 */
static FN_HV_VCPU_WRITE_MSR             *g_pfnHvVCpuWriteMsr            = NULL; /* Since 10.10 */
static FN_HV_VCPU_FLUSH                 *g_pfnHvVCpuFlush               = NULL; /* Since 10.10 */
static FN_HV_VCPU_INVALIDATE_TLB        *g_pfnHvVCpuInvalidateTlb       = NULL; /* Since 10.10 */
static FN_HV_VCPU_RUN                   *g_pfnHvVCpuRun                 = NULL; /* Since 10.10 */
static FN_HV_VCPU_RUN_UNTIL             *g_pfnHvVCpuRunUntil            = NULL; /* Since 10.15 */
static FN_HV_VCPU_INTERRUPT             *g_pfnHvVCpuInterrupt           = NULL; /* Since 10.10 */
static FN_HV_VCPU_GET_EXEC_TIME         *g_pfnHvVCpuGetExecTime         = NULL; /* Since 10.10 */

static FN_HV_VMX_READ_CAPABILITY        *g_pfnHvVmxReadCapability       = NULL; /* Since 10.10 */
static FN_HV_VMX_VCPU_READ_VMCS         *g_pfnHvVmxVCpuReadVmcs         = NULL; /* Since 10.10 */
static FN_HV_VMX_VCPU_WRITE_VMCS        *g_pfnHvVmxVCpuWriteVmcs        = NULL; /* Since 10.10 */
static FN_HV_VMX_VCPU_READ_SHADOW_VMCS  *g_pfnHvVmxVCpuReadShadowVmcs   = NULL; /* Since 10.15 */
static FN_HV_VMX_VCPU_WRITE_SHADOW_VMCS *g_pfnHvVmxVCpuWriteShadowVmcs  = NULL; /* Since 10.15 */
static FN_HV_VMX_VCPU_SET_SHADOW_ACCESS *g_pfnHvVmxVCpuSetShadowAccess  = NULL; /* Since 10.15 */
static FN_HV_VMX_VCPU_SET_APIC_ADDRESS  *g_pfnHvVmxVCpuSetApicAddress   = NULL; /* Since 10.10 */

static FN_HV_VMX_GET_MSR_INFO            *g_pfnHvVmxGetMsrInfo          = NULL; /* Since 11.0 */
static FN_HV_VMX_VCPU_GET_CAP_WRITE_VMCS *g_pfnHvVmxVCpuGetCapWriteVmcs = NULL; /* Since 11.0 */
static FN_HV_VCPU_ENABLE_MANAGED_MSR     *g_pfnHvVCpuEnableManagedMsr   = NULL; /* Since 11.0 */
static FN_HV_VCPU_SET_MSR_ACCESS         *g_pfnHvVCpuSetMsrAccess       = NULL; /* Since 11.0 */
/** @} */


/**
 * Import instructions.
 */
static const struct
{
    bool        fOptional;  /**< Set if import is optional. */
    void        **ppfn;     /**< The function pointer variable. */
    const char  *pszName;   /**< The function name. */
} g_aImports[] =
{
#define NEM_DARWIN_IMPORT(a_fOptional, a_Pfn, a_Name) { (a_fOptional), (void **)&(a_Pfn), #a_Name }
    NEM_DARWIN_IMPORT(true,  g_pfnHvCapability,             hv_capability),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmCreate,               hv_vm_create),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmDestroy,              hv_vm_destroy),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmSpaceCreate,          hv_vm_space_create),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmSpaceDestroy,         hv_vm_space_destroy),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmMap,                  hv_vm_map),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmUnmap,                hv_vm_unmap),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmProtect,              hv_vm_protect),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmMapSpace,             hv_vm_map_space),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmUnmapSpace,           hv_vm_unmap_space),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmProtectSpace,         hv_vm_protect_space),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmSyncTsc,              hv_vm_sync_tsc),

    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuCreate,             hv_vcpu_create),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuDestroy,            hv_vcpu_destroy),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVCpuSetSpace,           hv_vcpu_set_space),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuReadRegister,       hv_vcpu_read_register),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuWriteRegister,      hv_vcpu_write_register),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuReadFpState,        hv_vcpu_read_fpstate),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuWriteFpState,       hv_vcpu_write_fpstate),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuEnableNativeMsr,    hv_vcpu_enable_native_msr),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuReadMsr,            hv_vcpu_read_msr),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuWriteMsr,           hv_vcpu_write_msr),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuFlush,              hv_vcpu_flush),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuInvalidateTlb,      hv_vcpu_invalidate_tlb),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuRun,                hv_vcpu_run),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVCpuRunUntil,           hv_vcpu_run_until),
    NEM_DARWIN_IMPORT(false, g_pfnHvVCpuInterrupt,          hv_vcpu_interrupt),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVCpuGetExecTime,        hv_vcpu_get_exec_time),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmxReadCapability,      hv_vmx_read_capability),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmxVCpuReadVmcs,        hv_vmx_vcpu_read_vmcs),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmxVCpuWriteVmcs,       hv_vmx_vcpu_write_vmcs),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmxVCpuReadShadowVmcs,  hv_vmx_vcpu_read_shadow_vmcs),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmxVCpuWriteShadowVmcs, hv_vmx_vcpu_write_shadow_vmcs),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmxVCpuSetShadowAccess, hv_vmx_vcpu_set_shadow_access),
    NEM_DARWIN_IMPORT(false, g_pfnHvVmxVCpuSetApicAddress,  hv_vmx_vcpu_set_apic_address),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmxGetMsrInfo,          hv_vmx_get_msr_info),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVmxVCpuGetCapWriteVmcs, hv_vmx_vcpu_get_cap_write_vmcs),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVCpuEnableManagedMsr,   hv_vcpu_enable_managed_msr),
    NEM_DARWIN_IMPORT(true,  g_pfnHvVCpuSetMsrAccess,       hv_vcpu_set_msr_access)
#undef NEM_DARWIN_IMPORT
};


/*
 * Let the preprocessor alias the APIs to import variables for better autocompletion.
 */
#ifndef IN_SLICKEDIT
# define hv_capability                  g_pfnHvCapability
# define hv_vm_create                   g_pfnHvVmCreate
# define hv_vm_destroy                  g_pfnHvVmDestroy
# define hv_vm_space_create             g_pfnHvVmSpaceCreate
# define hv_vm_space_destroy            g_pfnHvVmSpaceDestroy
# define hv_vm_map                      g_pfnHvVmMap
# define hv_vm_unmap                    g_pfnHvVmUnmap
# define hv_vm_protect                  g_pfnHvVmProtect
# define hv_vm_map_space                g_pfnHvVmMapSpace
# define hv_vm_unmap_space              g_pfnHvVmUnmapSpace
# define hv_vm_protect_space            g_pfnHvVmProtectSpace
# define hv_vm_sync_tsc                 g_pfnHvVmSyncTsc

# define hv_vcpu_create                 g_pfnHvVCpuCreate
# define hv_vcpu_destroy                g_pfnHvVCpuDestroy
# define hv_vcpu_set_space              g_pfnHvVCpuSetSpace
# define hv_vcpu_read_register          g_pfnHvVCpuReadRegister
# define hv_vcpu_write_register         g_pfnHvVCpuWriteRegister
# define hv_vcpu_read_fpstate           g_pfnHvVCpuReadFpState
# define hv_vcpu_write_fpstate          g_pfnHvVCpuWriteFpState
# define hv_vcpu_enable_native_msr      g_pfnHvVCpuEnableNativeMsr
# define hv_vcpu_read_msr               g_pfnHvVCpuReadMsr
# define hv_vcpu_write_msr              g_pfnHvVCpuWriteMsr
# define hv_vcpu_flush                  g_pfnHvVCpuFlush
# define hv_vcpu_invalidate_tlb         g_pfnHvVCpuInvalidateTlb
# define hv_vcpu_run                    g_pfnHvVCpuRun
# define hv_vcpu_run_until              g_pfnHvVCpuRunUntil
# define hv_vcpu_interrupt              g_pfnHvVCpuInterrupt
# define hv_vcpu_get_exec_time          g_pfnHvVCpuGetExecTime

# define hv_vmx_read_capability         g_pfnHvVmxReadCapability
# define hv_vmx_vcpu_read_vmcs          g_pfnHvVmxVCpuReadVmcs
# define hv_vmx_vcpu_write_vmcs         g_pfnHvVmxVCpuWriteVmcs
# define hv_vmx_vcpu_read_shadow_vmcs   g_pfnHvVmxVCpuReadShadowVmcs
# define hv_vmx_vcpu_write_shadow_vmcs  g_pfnHvVmxVCpuWriteShadowVmcs
# define hv_vmx_vcpu_set_shadow_access  g_pfnHvVmxVCpuSetShadowAccess
# define hv_vmx_vcpu_set_apic_address   g_pfnHvVmxVCpuSetApicAddress

# define hv_vmx_get_msr_info            g_pfnHvVmxGetMsrInfo
# define hv_vmx_vcpu_get_cap_write_vmcs g_pfnHvVmxVCpuGetCapWriteVmcs
# define hv_vcpu_enable_managed_msr     g_pfnHvVCpuEnableManagedMsr
# define hv_vcpu_set_msr_access         g_pfnHvVCpuSetMsrAccess
#endif

static const struct
{
    uint32_t    u32VmcsFieldId;  /**< The VMCS field identifier. */
    const char  *pszVmcsField;   /**< The VMCS field name. */
    bool        f64Bit;
} g_aVmcsFieldsCap[] =
{
#define NEM_DARWIN_VMCS64_FIELD_CAP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, true  }
#define NEM_DARWIN_VMCS32_FIELD_CAP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, false }

    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_PIN_EXEC),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_PROC_EXEC),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_EXCEPTION_BITMAP),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_EXIT),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_ENTRY),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_PROC_EXEC2),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_PLE_GAP),
    NEM_DARWIN_VMCS32_FIELD_CAP(VMX_VMCS32_CTRL_PLE_WINDOW),
    NEM_DARWIN_VMCS64_FIELD_CAP(VMX_VMCS64_CTRL_TSC_OFFSET_FULL),
    NEM_DARWIN_VMCS64_FIELD_CAP(VMX_VMCS64_GUEST_DEBUGCTL_FULL)
#undef NEM_DARWIN_VMCS64_FIELD_CAP
#undef NEM_DARWIN_VMCS32_FIELD_CAP
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(void) vmxHCImportGuestIntrState(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo);


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
    if (*pu2State == NEM_DARWIN_PAGE_STATE_UNMAPPED)
    {
        Log5(("nemR3DarwinUnmap: %RGp == unmapped\n", GCPhys));
        *pu2State = NEM_DARWIN_PAGE_STATE_UNMAPPED;
        return VINF_SUCCESS;
    }

    LogFlowFunc(("Unmapping %RGp LB %zu\n", GCPhys, cb));
    hv_return_t hrc;
    if (pVM->nem.s.fCreatedAsid)
        hrc = hv_vm_unmap_space(pVM->nem.s.uVmAsid, GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, cb);
    else
        hrc = hv_vm_unmap(GCPhys, cb);
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

    hv_memory_flags_t fHvMemProt = 0;
    if (fPageProt & NEM_PAGE_PROT_READ)
        fHvMemProt |= HV_MEMORY_READ;
    if (fPageProt & NEM_PAGE_PROT_WRITE)
        fHvMemProt |= HV_MEMORY_WRITE;
    if (fPageProt & NEM_PAGE_PROT_EXECUTE)
        fHvMemProt |= HV_MEMORY_EXEC;

    hv_return_t hrc;
    if (pVM->nem.s.fCreatedAsid)
        hrc = hv_vm_map_space(pVM->nem.s.uVmAsid, pvRam, GCPhys, cb, fHvMemProt);
    else
        hrc = hv_vm_map(pvRam, GCPhys, cb, fHvMemProt);
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
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The guest physical address to start mapping.
 * @param   cb                  The size of the range, page aligned.
 * @param   fPageProt           The page protection flags to use for this range, combination of NEM_PAGE_PROT_XXX
 * @param   pu2State            Where to store the state for the new page, optional.
 */
DECLINLINE(int) nemR3DarwinProtect(PVM pVM, RTGCPHYS GCPhys, size_t cb, uint32_t fPageProt, uint8_t *pu2State)
{
    hv_memory_flags_t fHvMemProt = 0;
    if (fPageProt & NEM_PAGE_PROT_READ)
        fHvMemProt |= HV_MEMORY_READ;
    if (fPageProt & NEM_PAGE_PROT_WRITE)
        fHvMemProt |= HV_MEMORY_WRITE;
    if (fPageProt & NEM_PAGE_PROT_EXECUTE)
        fHvMemProt |= HV_MEMORY_EXEC;

    hv_return_t hrc;
    if (pVM->nem.s.fCreatedAsid)
        hrc = hv_vm_protect_space(pVM->nem.s.uVmAsid, GCPhys, cb, fHvMemProt);
    else
        hrc = hv_vm_protect(GCPhys, cb, fHvMemProt);
    if (hrc == HV_SUCCESS)
    {
        if (pu2State)
            *pu2State = nemR3DarwinPageStateFromProt(fPageProt);
        return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3NativeGCPhys2R3PtrReadOnly(PVM pVM, RTGCPHYS GCPhys, const void **ppv)
{
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhys, ppv, &Lock);
    if (RT_SUCCESS(rc))
        PGMPhysReleasePageMappingLock(pVM, &Lock);
    return rc;
}


DECLINLINE(int) nemR3NativeGCPhys2R3PtrWriteable(PVM pVM, RTGCPHYS GCPhys, void **ppv)
{
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtr(pVM, GCPhys, ppv, &Lock);
    if (RT_SUCCESS(rc))
        PGMPhysReleasePageMappingLock(pVM, &Lock);
    return rc;
}


#ifdef LOG_ENABLED
/**
 * Logs the current CPU state.
 */
static void nemR3DarwinLogState(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (LogIs3Enabled())
    {
#if 0
        char szRegs[4096];
        DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                        "rax=%016VR{rax} rbx=%016VR{rbx} rcx=%016VR{rcx} rdx=%016VR{rdx}\n"
                        "rsi=%016VR{rsi} rdi=%016VR{rdi} r8 =%016VR{r8} r9 =%016VR{r9}\n"
                        "r10=%016VR{r10} r11=%016VR{r11} r12=%016VR{r12} r13=%016VR{r13}\n"
                        "r14=%016VR{r14} r15=%016VR{r15} %VRF{rflags}\n"
                        "rip=%016VR{rip} rsp=%016VR{rsp} rbp=%016VR{rbp}\n"
                        "cs={%04VR{cs} base=%016VR{cs_base} limit=%08VR{cs_lim} flags=%04VR{cs_attr}} cr0=%016VR{cr0}\n"
                        "ds={%04VR{ds} base=%016VR{ds_base} limit=%08VR{ds_lim} flags=%04VR{ds_attr}} cr2=%016VR{cr2}\n"
                        "es={%04VR{es} base=%016VR{es_base} limit=%08VR{es_lim} flags=%04VR{es_attr}} cr3=%016VR{cr3}\n"
                        "fs={%04VR{fs} base=%016VR{fs_base} limit=%08VR{fs_lim} flags=%04VR{fs_attr}} cr4=%016VR{cr4}\n"
                        "gs={%04VR{gs} base=%016VR{gs_base} limit=%08VR{gs_lim} flags=%04VR{gs_attr}} cr8=%016VR{cr8}\n"
                        "ss={%04VR{ss} base=%016VR{ss_base} limit=%08VR{ss_lim} flags=%04VR{ss_attr}}\n"
                        "dr0=%016VR{dr0} dr1=%016VR{dr1} dr2=%016VR{dr2} dr3=%016VR{dr3}\n"
                        "dr6=%016VR{dr6} dr7=%016VR{dr7}\n"
                        "gdtr=%016VR{gdtr_base}:%04VR{gdtr_lim}  idtr=%016VR{idtr_base}:%04VR{idtr_lim}  rflags=%08VR{rflags}\n"
                        "ldtr={%04VR{ldtr} base=%016VR{ldtr_base} limit=%08VR{ldtr_lim} flags=%08VR{ldtr_attr}}\n"
                        "tr  ={%04VR{tr} base=%016VR{tr_base} limit=%08VR{tr_lim} flags=%08VR{tr_attr}}\n"
                        "    sysenter={cs=%04VR{sysenter_cs} eip=%08VR{sysenter_eip} esp=%08VR{sysenter_esp}}\n"
                        "        efer=%016VR{efer}\n"
                        "         pat=%016VR{pat}\n"
                        "     sf_mask=%016VR{sf_mask}\n"
                        "krnl_gs_base=%016VR{krnl_gs_base}\n"
                        "       lstar=%016VR{lstar}\n"
                        "        star=%016VR{star} cstar=%016VR{cstar}\n"
                        "fcw=%04VR{fcw} fsw=%04VR{fsw} ftw=%04VR{ftw} mxcsr=%04VR{mxcsr} mxcsr_mask=%04VR{mxcsr_mask}\n"
                        );

        char szInstr[256];
        DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, 0, 0,
                           DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                           szInstr, sizeof(szInstr), NULL);
        Log3(("%s%s\n", szRegs, szInstr));
#else
        RT_NOREF(pVM, pVCpu);
#endif
    }
}
#endif /* LOG_ENABLED */


DECLINLINE(int) nemR3DarwinReadVmcs16(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint16_t *pData)
{
    uint64_t u64Data;
    hv_return_t hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, &u64Data);
    if (RT_LIKELY(hrc == HV_SUCCESS))
    {
        *pData = (uint16_t)u64Data;
        return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3DarwinReadVmcs32(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint32_t *pData)
{
    uint64_t u64Data;
    hv_return_t hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, &u64Data);
    if (RT_LIKELY(hrc == HV_SUCCESS))
    {
        *pData = (uint32_t)u64Data;
        return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3DarwinReadVmcs64(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint64_t *pData)
{
    hv_return_t hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, pData);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3DarwinWriteVmcs16(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint16_t u16Val)
{
    hv_return_t hrc = hv_vmx_vcpu_write_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, u16Val);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3DarwinWriteVmcs32(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint32_t u32Val)
{
    hv_return_t hrc = hv_vmx_vcpu_write_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, u32Val);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}


DECLINLINE(int) nemR3DarwinWriteVmcs64(PVMCPUCC pVCpu, uint32_t uFieldEnc, uint64_t u64Val)
{
    hv_return_t hrc = hv_vmx_vcpu_write_vmcs(pVCpu->nem.s.hVCpuId, uFieldEnc, u64Val);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}

DECLINLINE(int) nemR3DarwinMsrRead(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t *pu64Val)
{
    hv_return_t hrc = hv_vcpu_read_msr(pVCpu->nem.s.hVCpuId, idMsr, pu64Val);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}

#if 0 /*unused*/
DECLINLINE(int) nemR3DarwinMsrWrite(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t u64Val)
{
    hv_return_t hrc = hv_vcpu_write_msr(pVCpu->nem.s.hVCpuId, idMsr, u64Val);
    if (RT_LIKELY(hrc == HV_SUCCESS))
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}
#endif

static int nemR3DarwinCopyStateFromHv(PVMCC pVM, PVMCPUCC pVCpu, uint64_t fWhat)
{
#define READ_GREG(a_GReg, a_Value) \
    do \
    { \
        hrc = hv_vcpu_read_register(pVCpu->nem.s.hVCpuId, (a_GReg), &(a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define READ_VMCS_FIELD(a_Field, a_Value) \
    do \
    { \
        hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, (a_Field), &(a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define READ_VMCS16_FIELD(a_Field, a_Value) \
    do \
    { \
        uint64_t u64Data; \
        hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, (a_Field), &u64Data); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { (a_Value) = (uint16_t)u64Data; } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define READ_VMCS32_FIELD(a_Field, a_Value) \
    do \
    { \
        uint64_t u64Data; \
        hrc = hv_vmx_vcpu_read_vmcs(pVCpu->nem.s.hVCpuId, (a_Field), &u64Data); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { (a_Value) = (uint32_t)u64Data; } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define READ_MSR(a_Msr, a_Value) \
    do \
    { \
        hrc = hv_vcpu_read_msr(pVCpu->nem.s.hVCpuId, (a_Msr), &(a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            AssertFailedReturn(VERR_INTERNAL_ERROR); \
    } while(0)

    STAM_PROFILE_ADV_START(&pVCpu->nem.s.StatProfGstStateImport, x);

    RT_NOREF(pVM);
    fWhat &= pVCpu->cpum.GstCtx.fExtrn;

    if (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
        vmxHCImportGuestIntrState(pVCpu, &pVCpu->nem.s.VmcsInfo);

    /* GPRs */
    hv_return_t hrc;
    if (fWhat & CPUMCTX_EXTRN_GPRS_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_RAX)
            READ_GREG(HV_X86_RAX, pVCpu->cpum.GstCtx.rax);
        if (fWhat & CPUMCTX_EXTRN_RCX)
            READ_GREG(HV_X86_RCX, pVCpu->cpum.GstCtx.rcx);
        if (fWhat & CPUMCTX_EXTRN_RDX)
            READ_GREG(HV_X86_RDX, pVCpu->cpum.GstCtx.rdx);
        if (fWhat & CPUMCTX_EXTRN_RBX)
            READ_GREG(HV_X86_RBX, pVCpu->cpum.GstCtx.rbx);
        if (fWhat & CPUMCTX_EXTRN_RSP)
            READ_GREG(HV_X86_RSP, pVCpu->cpum.GstCtx.rsp);
        if (fWhat & CPUMCTX_EXTRN_RBP)
            READ_GREG(HV_X86_RBP, pVCpu->cpum.GstCtx.rbp);
        if (fWhat & CPUMCTX_EXTRN_RSI)
            READ_GREG(HV_X86_RSI, pVCpu->cpum.GstCtx.rsi);
        if (fWhat & CPUMCTX_EXTRN_RDI)
            READ_GREG(HV_X86_RDI, pVCpu->cpum.GstCtx.rdi);
        if (fWhat & CPUMCTX_EXTRN_R8_R15)
        {
            READ_GREG(HV_X86_R8, pVCpu->cpum.GstCtx.r8);
            READ_GREG(HV_X86_R9, pVCpu->cpum.GstCtx.r9);
            READ_GREG(HV_X86_R10, pVCpu->cpum.GstCtx.r10);
            READ_GREG(HV_X86_R11, pVCpu->cpum.GstCtx.r11);
            READ_GREG(HV_X86_R12, pVCpu->cpum.GstCtx.r12);
            READ_GREG(HV_X86_R13, pVCpu->cpum.GstCtx.r13);
            READ_GREG(HV_X86_R14, pVCpu->cpum.GstCtx.r14);
            READ_GREG(HV_X86_R15, pVCpu->cpum.GstCtx.r15);
        }
    }

    /* RIP & Flags */
    if (fWhat & CPUMCTX_EXTRN_RIP)
        READ_GREG(HV_X86_RIP, pVCpu->cpum.GstCtx.rip);
    if (fWhat & CPUMCTX_EXTRN_RFLAGS)
    {
        uint64_t fRFlagsTmp = 0;
        READ_GREG(HV_X86_RFLAGS, fRFlagsTmp);
        pVCpu->cpum.GstCtx.rflags.u = fRFlagsTmp;
    }

    /* Segments */
#define READ_SEG(a_SReg, a_enmName) \
        do { \
            READ_VMCS16_FIELD(VMX_VMCS16_GUEST_ ## a_enmName ## _SEL,           (a_SReg).Sel); \
            READ_VMCS32_FIELD(VMX_VMCS32_GUEST_ ## a_enmName ## _LIMIT,         (a_SReg).u32Limit); \
            READ_VMCS32_FIELD(VMX_VMCS32_GUEST_ ## a_enmName ## _ACCESS_RIGHTS, (a_SReg).Attr.u); \
            READ_VMCS_FIELD(VMX_VMCS_GUEST_ ## a_enmName ## _BASE,            (a_SReg).u64Base); \
            (a_SReg).ValidSel = (a_SReg).Sel; \
        } while (0)
    if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_ES)
            READ_SEG(pVCpu->cpum.GstCtx.es, ES);
        if (fWhat & CPUMCTX_EXTRN_CS)
            READ_SEG(pVCpu->cpum.GstCtx.cs, CS);
        if (fWhat & CPUMCTX_EXTRN_SS)
            READ_SEG(pVCpu->cpum.GstCtx.ss, SS);
        if (fWhat & CPUMCTX_EXTRN_DS)
            READ_SEG(pVCpu->cpum.GstCtx.ds, DS);
        if (fWhat & CPUMCTX_EXTRN_FS)
            READ_SEG(pVCpu->cpum.GstCtx.fs, FS);
        if (fWhat & CPUMCTX_EXTRN_GS)
            READ_SEG(pVCpu->cpum.GstCtx.gs, GS);
    }

    /* Descriptor tables and the task segment. */
    if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_LDTR)
            READ_SEG(pVCpu->cpum.GstCtx.ldtr, LDTR);

        if (fWhat & CPUMCTX_EXTRN_TR)
        {
            /* AMD-V likes loading TR with in AVAIL state, whereas intel insists on BUSY.  So,
               avoid to trigger sanity assertions around the code, always fix this. */
            READ_SEG(pVCpu->cpum.GstCtx.tr, TR);
            switch (pVCpu->cpum.GstCtx.tr.Attr.n.u4Type)
            {
                case X86_SEL_TYPE_SYS_386_TSS_BUSY:
                case X86_SEL_TYPE_SYS_286_TSS_BUSY:
                    break;
                case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
                    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_386_TSS_BUSY;
                    break;
                case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
                    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_286_TSS_BUSY;
                    break;
            }
        }
        if (fWhat & CPUMCTX_EXTRN_IDTR)
        {
            READ_VMCS32_FIELD(VMX_VMCS32_GUEST_IDTR_LIMIT, pVCpu->cpum.GstCtx.idtr.cbIdt);
            READ_VMCS_FIELD(VMX_VMCS_GUEST_IDTR_BASE,  pVCpu->cpum.GstCtx.idtr.pIdt);
        }
        if (fWhat & CPUMCTX_EXTRN_GDTR)
        {
            READ_VMCS32_FIELD(VMX_VMCS32_GUEST_GDTR_LIMIT, pVCpu->cpum.GstCtx.gdtr.cbGdt);
            READ_VMCS_FIELD(VMX_VMCS_GUEST_GDTR_BASE,  pVCpu->cpum.GstCtx.gdtr.pGdt);
        }
    }

    /* Control registers. */
    bool fMaybeChangedMode = false;
    bool fUpdateCr3        = false;
    if (fWhat & CPUMCTX_EXTRN_CR_MASK)
    {
        uint64_t u64CrTmp = 0;

        if (fWhat & CPUMCTX_EXTRN_CR0)
        {
            READ_GREG(HV_X86_CR0, u64CrTmp);
            if (pVCpu->cpum.GstCtx.cr0 != u64CrTmp)
            {
                CPUMSetGuestCR0(pVCpu, u64CrTmp);
                fMaybeChangedMode = true;
            }
        }
        if (fWhat & CPUMCTX_EXTRN_CR2)
            READ_GREG(HV_X86_CR2, pVCpu->cpum.GstCtx.cr2);
        if (fWhat & CPUMCTX_EXTRN_CR3)
        {
            READ_GREG(HV_X86_CR3, u64CrTmp);
            if (pVCpu->cpum.GstCtx.cr3 != u64CrTmp)
            {
                CPUMSetGuestCR3(pVCpu, u64CrTmp);
                fUpdateCr3 = true;
            }

            /*
             * If the guest is in PAE mode, sync back the PDPE's into the guest state.
             * CR4.PAE, CR0.PG, EFER MSR changes are always intercepted, so they're up to date.
             */
            if (CPUMIsGuestInPAEModeEx(&pVCpu->cpum.GstCtx))
            {
                X86PDPE aPaePdpes[4];
                READ_VMCS_FIELD(VMX_VMCS64_GUEST_PDPTE0_FULL, aPaePdpes[0].u);
                READ_VMCS_FIELD(VMX_VMCS64_GUEST_PDPTE1_FULL, aPaePdpes[1].u);
                READ_VMCS_FIELD(VMX_VMCS64_GUEST_PDPTE2_FULL, aPaePdpes[2].u);
                READ_VMCS_FIELD(VMX_VMCS64_GUEST_PDPTE3_FULL, aPaePdpes[3].u);
                if (memcmp(&aPaePdpes[0], &pVCpu->cpum.GstCtx.aPaePdpes[0], sizeof(aPaePdpes)))
                {
                    memcpy(&pVCpu->cpum.GstCtx.aPaePdpes[0], &aPaePdpes[0], sizeof(aPaePdpes));
                    fUpdateCr3 = true;
                }
            }
        }
        if (fWhat & CPUMCTX_EXTRN_CR4)
        {
            READ_GREG(HV_X86_CR4, u64CrTmp);
            u64CrTmp &= ~VMX_V_CR4_FIXED0;

            if (pVCpu->cpum.GstCtx.cr4 != u64CrTmp)
            {
                CPUMSetGuestCR4(pVCpu, u64CrTmp);
                fMaybeChangedMode = true;
            }
        }
    }

#if 0 /* Always done. */
    if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
    {
        uint64_t u64Cr8 = 0;

        READ_GREG(HV_X86_TPR, u64Cr8);
        APICSetTpr(pVCpu, u64Cr8 << 4);
    }
#endif

    if (fWhat & CPUMCTX_EXTRN_XCRx)
        READ_GREG(HV_X86_XCR0, pVCpu->cpum.GstCtx.aXcr[0]);

    /* Debug registers. */
    if (fWhat & CPUMCTX_EXTRN_DR7)
    {
        uint64_t u64Dr7;
        READ_GREG(HV_X86_DR7, u64Dr7);
        if (pVCpu->cpum.GstCtx.dr[7] != u64Dr7)
            CPUMSetGuestDR7(pVCpu, u64Dr7);
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_DR7; /* Hack alert! Avoids asserting when processing CPUMCTX_EXTRN_DR0_DR3. */
    }
    if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
    {
        uint64_t u64DrTmp;

        READ_GREG(HV_X86_DR0, u64DrTmp);
        if (pVCpu->cpum.GstCtx.dr[0] != u64DrTmp)
            CPUMSetGuestDR0(pVCpu, u64DrTmp);
        READ_GREG(HV_X86_DR1, u64DrTmp);
        if (pVCpu->cpum.GstCtx.dr[1] != u64DrTmp)
            CPUMSetGuestDR1(pVCpu, u64DrTmp);
        READ_GREG(HV_X86_DR2, u64DrTmp);
        if (pVCpu->cpum.GstCtx.dr[2] != u64DrTmp)
            CPUMSetGuestDR2(pVCpu, u64DrTmp);
        READ_GREG(HV_X86_DR3, u64DrTmp);
        if (pVCpu->cpum.GstCtx.dr[3] != u64DrTmp)
            CPUMSetGuestDR3(pVCpu, u64DrTmp);
    }
    if (fWhat & CPUMCTX_EXTRN_DR6)
    {
        uint64_t u64Dr6;
        READ_GREG(HV_X86_DR6, u64Dr6);
        if (pVCpu->cpum.GstCtx.dr[6] != u64Dr6)
            CPUMSetGuestDR6(pVCpu, u64Dr6);
    }

    if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX))
    {
        hrc = hv_vcpu_read_fpstate(pVCpu->nem.s.hVCpuId, &pVCpu->cpum.GstCtx.XState, sizeof(pVCpu->cpum.GstCtx.XState));
        if (hrc == HV_SUCCESS)
        { /* likely */ }
        else
        {
            STAM_PROFILE_ADV_STOP(&pVCpu->nem.s.StatProfGstStateImport, x);
            return nemR3DarwinHvSts2Rc(hrc);
        }
    }

    /* MSRs */
    if (fWhat & CPUMCTX_EXTRN_EFER)
    {
        uint64_t u64Efer;

        READ_VMCS_FIELD(VMX_VMCS64_GUEST_EFER_FULL, u64Efer);
        if (u64Efer != pVCpu->cpum.GstCtx.msrEFER)
        {
            Log7(("NEM/%u: MSR EFER changed %RX64 -> %RX64\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.msrEFER, u64Efer));
            if ((u64Efer ^ pVCpu->cpum.GstCtx.msrEFER) & MSR_K6_EFER_NXE)
                PGMNotifyNxeChanged(pVCpu, RT_BOOL(u64Efer & MSR_K6_EFER_NXE));
            pVCpu->cpum.GstCtx.msrEFER = u64Efer;
            fMaybeChangedMode = true;
        }
    }

    if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
        READ_MSR(MSR_K8_KERNEL_GS_BASE, pVCpu->cpum.GstCtx.msrKERNELGSBASE);
    if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        uint64_t u64Tmp;
        READ_MSR(MSR_IA32_SYSENTER_EIP, u64Tmp);
        pVCpu->cpum.GstCtx.SysEnter.eip = u64Tmp;
        READ_MSR(MSR_IA32_SYSENTER_ESP, u64Tmp);
        pVCpu->cpum.GstCtx.SysEnter.esp = u64Tmp;
        READ_MSR(MSR_IA32_SYSENTER_CS, u64Tmp);
        pVCpu->cpum.GstCtx.SysEnter.cs = u64Tmp;
    }
    if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        READ_MSR(MSR_K6_STAR, pVCpu->cpum.GstCtx.msrSTAR);
        READ_MSR(MSR_K8_LSTAR, pVCpu->cpum.GstCtx.msrLSTAR);
        READ_MSR(MSR_K8_CSTAR, pVCpu->cpum.GstCtx.msrCSTAR);
        READ_MSR(MSR_K8_SF_MASK, pVCpu->cpum.GstCtx.msrSFMASK);
    }
    if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
    {
        PCPUMCTXMSRS pCtxMsrs = CPUMQueryGuestCtxMsrsPtr(pVCpu);
        READ_MSR(MSR_K8_TSC_AUX, pCtxMsrs->msr.TscAux);
    }
    if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
    {
        /* Last Branch Record. */
        if (pVM->nem.s.fLbr)
        {
            PVMXVMCSINFOSHARED const pVmcsInfoShared = &pVCpu->nem.s.vmx.VmcsInfo;
            uint32_t const idFromIpMsrStart = pVM->nem.s.idLbrFromIpMsrFirst;
            uint32_t const idToIpMsrStart   = pVM->nem.s.idLbrToIpMsrFirst;
            uint32_t const idInfoMsrStart   = pVM->nem.s.idLbrInfoMsrFirst;
            uint32_t const cLbrStack        = pVM->nem.s.idLbrFromIpMsrLast - pVM->nem.s.idLbrFromIpMsrFirst + 1;
            Assert(cLbrStack <= 32);
            for (uint32_t i = 0; i < cLbrStack; i++)
            {
                READ_MSR(idFromIpMsrStart + i, pVmcsInfoShared->au64LbrFromIpMsr[i]);

                /* Some CPUs don't have a Branch-To-IP MSR (P4 and related Xeons). */
                if (idToIpMsrStart != 0)
                    READ_MSR(idToIpMsrStart + i, pVmcsInfoShared->au64LbrToIpMsr[i]);
                if (idInfoMsrStart != 0)
                    READ_MSR(idInfoMsrStart + i, pVmcsInfoShared->au64LbrInfoMsr[i]);
            }

            READ_MSR(pVM->nem.s.idLbrTosMsr, pVmcsInfoShared->u64LbrTosMsr);

            if (pVM->nem.s.idLerFromIpMsr)
                READ_MSR(pVM->nem.s.idLerFromIpMsr, pVmcsInfoShared->u64LerFromIpMsr);
            if (pVM->nem.s.idLerToIpMsr)
                READ_MSR(pVM->nem.s.idLerToIpMsr, pVmcsInfoShared->u64LerToIpMsr);
        }
    }

    /* Almost done, just update extrn flags and maybe change PGM mode. */
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
        pVCpu->cpum.GstCtx.fExtrn = 0;

#ifdef LOG_ENABLED
    nemR3DarwinLogState(pVM, pVCpu);
#endif

    /* Typical. */
    if (!fMaybeChangedMode && !fUpdateCr3)
    {
        STAM_PROFILE_ADV_STOP(&pVCpu->nem.s.StatProfGstStateImport, x);
        return VINF_SUCCESS;
    }

    /*
     * Slow.
     */
    if (fMaybeChangedMode)
    {
        int rc = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                               false /* fForce */);
        AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_1);
    }

    if (fUpdateCr3)
    {
        int rc = PGMUpdateCR3(pVCpu, pVCpu->cpum.GstCtx.cr3);
        if (rc == VINF_SUCCESS)
        { /* likely */ }
        else
            AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_2);
    }

    STAM_PROFILE_ADV_STOP(&pVCpu->nem.s.StatProfGstStateImport, x);

    return VINF_SUCCESS;
#undef READ_GREG
#undef READ_VMCS_FIELD
#undef READ_VMCS32_FIELD
#undef READ_SEG
#undef READ_MSR
}


/**
 * State to pass between vmxHCExitEptViolation
 * and nemR3DarwinHandleMemoryAccessPageCheckerCallback.
 */
typedef struct NEMHCDARWINHMACPCCSTATE
{
    /** Input: Write access. */
    bool    fWriteAccess;
    /** Output: Set if we did something. */
    bool    fDidSomething;
    /** Output: Set it we should resume. */
    bool    fCanResume;
} NEMHCDARWINHMACPCCSTATE;

/**
 * @callback_method_impl{FNPGMPHYSNEMCHECKPAGE,
 *      Worker for vmxHCExitEptViolation; pvUser points to a
 *      NEMHCDARWINHMACPCCSTATE structure. }
 */
static DECLCALLBACK(int)
nemR3DarwinHandleMemoryAccessPageCheckerCallback(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, PPGMPHYSNEMPAGEINFO pInfo, void *pvUser)
{
    RT_NOREF(pVCpu);

    NEMHCDARWINHMACPCCSTATE *pState = (NEMHCDARWINHMACPCCSTATE *)pvUser;
    pState->fDidSomething = false;
    pState->fCanResume    = false;

    uint8_t  u2State = pInfo->u2NemState;

    /*
     * Consolidate current page state with actual page protection and access type.
     * We don't really consider downgrades here, as they shouldn't happen.
     */
    switch (u2State)
    {
        case NEM_DARWIN_PAGE_STATE_UNMAPPED:
        {
            if (pInfo->fNemProt == NEM_PAGE_PROT_NONE)
            {
                Log4(("nemR3DarwinHandleMemoryAccessPageCheckerCallback: %RGp - #1\n", GCPhys));
                return VINF_SUCCESS;
            }

            /* Don't bother remapping it if it's a write request to a non-writable page. */
            if (   pState->fWriteAccess
                && !(pInfo->fNemProt & NEM_PAGE_PROT_WRITE))
            {
                Log4(("nemR3DarwinHandleMemoryAccessPageCheckerCallback: %RGp - #1w\n", GCPhys));
                return VINF_SUCCESS;
            }

            int rc = VINF_SUCCESS;
            if (pInfo->fNemProt & NEM_PAGE_PROT_WRITE)
            {
                void *pvPage;
                rc = nemR3NativeGCPhys2R3PtrWriteable(pVM, GCPhys, &pvPage);
                if (RT_SUCCESS(rc))
                    rc = nemR3DarwinMap(pVM, GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, pvPage, X86_PAGE_SIZE, pInfo->fNemProt, &u2State);
            }
            else if (pInfo->fNemProt & NEM_PAGE_PROT_READ)
            {
                const void *pvPage;
                rc = nemR3NativeGCPhys2R3PtrReadOnly(pVM, GCPhys, &pvPage);
                if (RT_SUCCESS(rc))
                    rc = nemR3DarwinMap(pVM, GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, pvPage, X86_PAGE_SIZE, pInfo->fNemProt, &u2State);
            }
            else /* Only EXECUTE doesn't work. */
                AssertReleaseFailed();

            pInfo->u2NemState = u2State;
            Log4(("nemR3DarwinHandleMemoryAccessPageCheckerCallback: %RGp - synced => %s + %Rrc\n",
                  GCPhys, g_apszPageStates[u2State], rc));
            pState->fDidSomething = true;
            pState->fCanResume    = true;
            return rc;
        }
        case NEM_DARWIN_PAGE_STATE_RX:
            if (   !(pInfo->fNemProt & NEM_PAGE_PROT_WRITE)
                && (pInfo->fNemProt & (NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE)))
            {
                pState->fCanResume = true;
                Log4(("nemR3DarwinHandleMemoryAccessPageCheckerCallback: %RGp - #2\n", GCPhys));
                return VINF_SUCCESS;
            }
            break;

        case NEM_DARWIN_PAGE_STATE_RW:
        case NEM_DARWIN_PAGE_STATE_RWX:
            if (pInfo->fNemProt & NEM_PAGE_PROT_WRITE)
            {
                pState->fCanResume = true;
                if (   pInfo->u2OldNemState == NEM_DARWIN_PAGE_STATE_RW
                    || pInfo->u2OldNemState == NEM_DARWIN_PAGE_STATE_RWX)
                    Log4(("nemR3DarwinHandleMemoryAccessPageCheckerCallback: Spurious EPT fault\n", GCPhys));
                return VINF_SUCCESS;
            }
            break;

        default:
            AssertLogRelMsgFailedReturn(("u2State=%#x\n", u2State), VERR_NEM_IPE_4);
    }

    /* Unmap and restart the instruction. */
    int rc = nemR3DarwinUnmap(pVM, GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, X86_PAGE_SIZE, &u2State);
    if (RT_SUCCESS(rc))
    {
        pInfo->u2NemState     = u2State;
        pState->fDidSomething = true;
        pState->fCanResume    = true;
        Log5(("NEM GPA unmapped/exit: %RGp (was %s)\n", GCPhys, g_apszPageStates[u2State]));
        return VINF_SUCCESS;
    }

    LogRel(("nemR3DarwinHandleMemoryAccessPageCheckerCallback/unmap: GCPhys=%RGp %s rc=%Rrc\n",
            GCPhys, g_apszPageStates[u2State], rc));
    return VERR_NEM_UNMAP_PAGES_FAILED;
}


DECL_FORCE_INLINE(bool) nemR3DarwinIsUnrestrictedGuest(PCVMCC pVM)
{
    RT_NOREF(pVM);
    return true;
}


DECL_FORCE_INLINE(bool) nemR3DarwinIsNestedPaging(PCVMCC pVM)
{
    RT_NOREF(pVM);
    return true;
}


DECL_FORCE_INLINE(bool) nemR3DarwinIsPreemptTimerUsed(PCVMCC pVM)
{
    RT_NOREF(pVM);
    return false;
}


#if 0 /* unused */
DECL_FORCE_INLINE(bool) nemR3DarwinIsVmxLbr(PCVMCC pVM)
{
    RT_NOREF(pVM);
    return false;
}
#endif


/*
 * Instantiate the code we share with ring-0.
 */
#define IN_NEM_DARWIN
//#define HMVMX_ALWAYS_TRAP_ALL_XCPTS
//#define HMVMX_ALWAYS_SYNC_FULL_GUEST_STATE
//#define HMVMX_ALWAYS_INTERCEPT_CR3_ACCESS
#define VCPU_2_VMXSTATE(a_pVCpu)            (a_pVCpu)->nem.s
#define VCPU_2_VMXSTATS(a_pVCpu)            (*(a_pVCpu)->nem.s.pVmxStats)

#define VM_IS_VMX_UNRESTRICTED_GUEST(a_pVM) nemR3DarwinIsUnrestrictedGuest((a_pVM))
#define VM_IS_VMX_NESTED_PAGING(a_pVM)      nemR3DarwinIsNestedPaging((a_pVM))
#define VM_IS_VMX_PREEMPT_TIMER_USED(a_pVM) nemR3DarwinIsPreemptTimerUsed((a_pVM))
#define VM_IS_VMX_LBR(a_pVM)                nemR3DarwinIsVmxLbr((a_pVM))

#define VMX_VMCS_WRITE_16(a_pVCpu, a_FieldEnc, a_Val) nemR3DarwinWriteVmcs16((a_pVCpu), (a_FieldEnc), (a_Val))
#define VMX_VMCS_WRITE_32(a_pVCpu, a_FieldEnc, a_Val) nemR3DarwinWriteVmcs32((a_pVCpu), (a_FieldEnc), (a_Val))
#define VMX_VMCS_WRITE_64(a_pVCpu, a_FieldEnc, a_Val) nemR3DarwinWriteVmcs64((a_pVCpu), (a_FieldEnc), (a_Val))
#define VMX_VMCS_WRITE_NW(a_pVCpu, a_FieldEnc, a_Val) nemR3DarwinWriteVmcs64((a_pVCpu), (a_FieldEnc), (a_Val))

#define VMX_VMCS_READ_16(a_pVCpu, a_FieldEnc, a_pVal) nemR3DarwinReadVmcs16((a_pVCpu), (a_FieldEnc), (a_pVal))
#define VMX_VMCS_READ_32(a_pVCpu, a_FieldEnc, a_pVal) nemR3DarwinReadVmcs32((a_pVCpu), (a_FieldEnc), (a_pVal))
#define VMX_VMCS_READ_64(a_pVCpu, a_FieldEnc, a_pVal) nemR3DarwinReadVmcs64((a_pVCpu), (a_FieldEnc), (a_pVal))
#define VMX_VMCS_READ_NW(a_pVCpu, a_FieldEnc, a_pVal) nemR3DarwinReadVmcs64((a_pVCpu), (a_FieldEnc), (a_pVal))

#include "../VMMAll/VMXAllTemplate.cpp.h"

#undef VMX_VMCS_WRITE_16
#undef VMX_VMCS_WRITE_32
#undef VMX_VMCS_WRITE_64
#undef VMX_VMCS_WRITE_NW

#undef VMX_VMCS_READ_16
#undef VMX_VMCS_READ_32
#undef VMX_VMCS_READ_64
#undef VMX_VMCS_READ_NW

#undef VM_IS_VMX_PREEMPT_TIMER_USED
#undef VM_IS_VMX_NESTED_PAGING
#undef VM_IS_VMX_UNRESTRICTED_GUEST
#undef VCPU_2_VMXSTATS
#undef VCPU_2_VMXSTATE


/**
 * Exports the guest GP registers to HV for execution.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 */
static int nemR3DarwinExportGuestGprs(PVMCPUCC pVCpu)
{
#define WRITE_GREG(a_GReg, a_Value) \
    do \
    { \
        hv_return_t hrc = hv_vcpu_write_register(pVCpu->nem.s.hVCpuId, (a_GReg), (a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)

    uint64_t fCtxChanged = ASMAtomicUoReadU64(&pVCpu->nem.s.fCtxChanged);
    if (fCtxChanged & HM_CHANGED_GUEST_GPRS_MASK)
    {
        if (fCtxChanged & HM_CHANGED_GUEST_RAX)
            WRITE_GREG(HV_X86_RAX, pVCpu->cpum.GstCtx.rax);
        if (fCtxChanged & HM_CHANGED_GUEST_RCX)
            WRITE_GREG(HV_X86_RCX, pVCpu->cpum.GstCtx.rcx);
        if (fCtxChanged & HM_CHANGED_GUEST_RDX)
            WRITE_GREG(HV_X86_RDX, pVCpu->cpum.GstCtx.rdx);
        if (fCtxChanged & HM_CHANGED_GUEST_RBX)
            WRITE_GREG(HV_X86_RBX, pVCpu->cpum.GstCtx.rbx);
        if (fCtxChanged & HM_CHANGED_GUEST_RSP)
            WRITE_GREG(HV_X86_RSP, pVCpu->cpum.GstCtx.rsp);
        if (fCtxChanged & HM_CHANGED_GUEST_RBP)
            WRITE_GREG(HV_X86_RBP, pVCpu->cpum.GstCtx.rbp);
        if (fCtxChanged & HM_CHANGED_GUEST_RSI)
            WRITE_GREG(HV_X86_RSI, pVCpu->cpum.GstCtx.rsi);
        if (fCtxChanged & HM_CHANGED_GUEST_RDI)
            WRITE_GREG(HV_X86_RDI, pVCpu->cpum.GstCtx.rdi);
        if (fCtxChanged & HM_CHANGED_GUEST_R8_R15)
        {
            WRITE_GREG(HV_X86_R8, pVCpu->cpum.GstCtx.r8);
            WRITE_GREG(HV_X86_R9, pVCpu->cpum.GstCtx.r9);
            WRITE_GREG(HV_X86_R10, pVCpu->cpum.GstCtx.r10);
            WRITE_GREG(HV_X86_R11, pVCpu->cpum.GstCtx.r11);
            WRITE_GREG(HV_X86_R12, pVCpu->cpum.GstCtx.r12);
            WRITE_GREG(HV_X86_R13, pVCpu->cpum.GstCtx.r13);
            WRITE_GREG(HV_X86_R14, pVCpu->cpum.GstCtx.r14);
            WRITE_GREG(HV_X86_R15, pVCpu->cpum.GstCtx.r15);
        }

        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_GPRS_MASK);
    }

    if (fCtxChanged & HM_CHANGED_GUEST_CR2)
    {
        WRITE_GREG(HV_X86_CR2, pVCpu->cpum.GstCtx.cr2);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_CR2);
    }

    return VINF_SUCCESS;
#undef WRITE_GREG
}


/**
 * Exports the guest debug registers into the guest-state applying any hypervisor
 * debug related states (hardware breakpoints from the debugger, etc.).
 *
 * This also sets up whether \#DB and MOV DRx accesses cause VM-exits.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
static int nemR3DarwinExportDebugState(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;

#ifdef VBOX_STRICT
    /* Validate. Intel spec. 26.3.1.1 "Checks on Guest Controls Registers, Debug Registers, MSRs" */
    if (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
    {
        /* Validate. Intel spec. 17.2 "Debug Registers", recompiler paranoia checks. */
        Assert((pVCpu->cpum.GstCtx.dr[7] & (X86_DR7_MBZ_MASK | X86_DR7_RAZ_MASK)) == 0);
        Assert((pVCpu->cpum.GstCtx.dr[7] & X86_DR7_RA1_MASK) == X86_DR7_RA1_MASK);
    }
#endif

    bool     fSteppingDB      = false;
    bool     fInterceptMovDRx = false;
    uint32_t uProcCtls        = pVmcsInfo->u32ProcCtls;
    if (pVCpu->nem.s.fSingleInstruction)
    {
        /* If the CPU supports the monitor trap flag, use it for single stepping in DBGF and avoid intercepting #DB. */
        if (g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_MONITOR_TRAP_FLAG)
        {
            uProcCtls |= VMX_PROC_CTLS_MONITOR_TRAP_FLAG;
            Assert(fSteppingDB == false);
        }
        else
        {
            pVCpu->cpum.GstCtx.eflags.u |= X86_EFL_TF;
            pVCpu->nem.s.fCtxChanged |= HM_CHANGED_GUEST_RFLAGS;
            pVCpu->nem.s.fClearTrapFlag = true;
            fSteppingDB = true;
        }
    }

    uint64_t u64GuestDr7;
    if (   fSteppingDB
        || (CPUMGetHyperDR7(pVCpu) & X86_DR7_ENABLED_MASK))
    {
        /*
         * Use the combined guest and host DRx values found in the hypervisor register set
         * because the hypervisor debugger has breakpoints active or someone is single stepping
         * on the host side without a monitor trap flag.
         *
         * Note! DBGF expects a clean DR6 state before executing guest code.
         */
        if (!CPUMIsHyperDebugStateActive(pVCpu))
        {
            /*
             * Make sure the hypervisor values are up to date.
             */
            CPUMRecalcHyperDRx(pVCpu, UINT8_MAX /* no loading, please */);

            CPUMR3NemActivateHyperDebugState(pVCpu);

            Assert(CPUMIsHyperDebugStateActive(pVCpu));
            Assert(!CPUMIsGuestDebugStateActive(pVCpu));
        }

        /* Update DR7 with the hypervisor value (other DRx registers are handled by CPUM one way or another). */
        u64GuestDr7 = CPUMGetHyperDR7(pVCpu);
        pVCpu->nem.s.fUsingHyperDR7 = true;
        fInterceptMovDRx = true;
    }
    else
    {
        /*
         * If the guest has enabled debug registers, we need to load them prior to
         * executing guest code so they'll trigger at the right time.
         */
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);
        if (pVCpu->cpum.GstCtx.dr[7] & (X86_DR7_ENABLED_MASK | X86_DR7_GD))
        {
            if (!CPUMIsGuestDebugStateActive(pVCpu))
            {
                CPUMR3NemActivateGuestDebugState(pVCpu);

                Assert(CPUMIsGuestDebugStateActive(pVCpu));
                Assert(!CPUMIsHyperDebugStateActive(pVCpu));
            }
            Assert(!fInterceptMovDRx);
        }
        else if (!CPUMIsGuestDebugStateActive(pVCpu))
        {
            /*
             * If no debugging enabled, we'll lazy load DR0-3.  Unlike on AMD-V, we
             * must intercept #DB in order to maintain a correct DR6 guest value, and
             * because we need to intercept it to prevent nested #DBs from hanging the
             * CPU, we end up always having to intercept it. See hmR0VmxSetupVmcsXcptBitmap().
             */
            fInterceptMovDRx = true;
        }

        /* Update DR7 with the actual guest value. */
        u64GuestDr7 = pVCpu->cpum.GstCtx.dr[7];
        pVCpu->nem.s.fUsingHyperDR7 = false;
    }

    /** @todo The DRx handling is not quite correct breaking debugging inside the guest with gdb,
     * see @ticketref{21413} and @ticketref{21546}, so this is disabled for now. See @bugref{10504}
     * as well.
     */
#if 0
    if (fInterceptMovDRx)
        uProcCtls |= VMX_PROC_CTLS_MOV_DR_EXIT;
    else
        uProcCtls &= ~VMX_PROC_CTLS_MOV_DR_EXIT;
#endif

    /*
     * Update the processor-based VM-execution controls with the MOV-DRx intercepts and the
     * monitor-trap flag and update our cache.
     */
    if (uProcCtls != pVmcsInfo->u32ProcCtls)
    {
        int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, uProcCtls);
        AssertRC(rc);
        pVmcsInfo->u32ProcCtls = uProcCtls;
    }

    /*
     * Update guest DR7.
     */
    int rc = nemR3DarwinWriteVmcs64(pVCpu, VMX_VMCS_GUEST_DR7, u64GuestDr7);
    AssertRC(rc);

    /*
     * If we have forced EFLAGS.TF to be set because we're single-stepping in the hypervisor debugger,
     * we need to clear interrupt inhibition if any as otherwise it causes a VM-entry failure.
     *
     * See Intel spec. 26.3.1.5 "Checks on Guest Non-Register State".
     */
    if (fSteppingDB)
    {
        Assert(pVCpu->nem.s.fSingleInstruction);
        Assert(pVCpu->cpum.GstCtx.eflags.Bits.u1TF);

        uint32_t fIntrState = 0;
        rc = nemR3DarwinReadVmcs32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, &fIntrState);
        AssertRC(rc);

        if (fIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
        {
            fIntrState &= ~(VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS);
            rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, fIntrState);
            AssertRC(rc);
        }
    }

    /*
     * Store status of the shared guest/host debug state at the time of VM-entry.
     */
    pVmxTransient->fWasGuestDebugStateActive = CPUMIsGuestDebugStateActive(pVCpu);
    pVmxTransient->fWasHyperDebugStateActive = CPUMIsHyperDebugStateActive(pVCpu);

    return VINF_SUCCESS;
}


/**
 * Converts the given CPUM externalized bitmask to the appropriate HM changed bitmask.
 *
 * @returns Bitmask of HM changed flags.
 * @param   fCpumExtrn      The CPUM extern bitmask.
 */
static uint64_t nemR3DarwinCpumExtrnToHmChanged(uint64_t fCpumExtrn)
{
    uint64_t fHmChanged = 0;

    /* Invert to gt a mask of things which are kept in CPUM. */
    uint64_t fCpumIntern = ~fCpumExtrn;

    if (fCpumIntern & CPUMCTX_EXTRN_GPRS_MASK)
    {
        if (fCpumIntern & CPUMCTX_EXTRN_RAX)
            fHmChanged |= HM_CHANGED_GUEST_RAX;
        if (fCpumIntern & CPUMCTX_EXTRN_RCX)
            fHmChanged |= HM_CHANGED_GUEST_RCX;
        if (fCpumIntern & CPUMCTX_EXTRN_RDX)
            fHmChanged |= HM_CHANGED_GUEST_RDX;
        if (fCpumIntern & CPUMCTX_EXTRN_RBX)
            fHmChanged |= HM_CHANGED_GUEST_RBX;
        if (fCpumIntern & CPUMCTX_EXTRN_RSP)
            fHmChanged |= HM_CHANGED_GUEST_RSP;
        if (fCpumIntern & CPUMCTX_EXTRN_RBP)
            fHmChanged |= HM_CHANGED_GUEST_RBP;
        if (fCpumIntern & CPUMCTX_EXTRN_RSI)
            fHmChanged |= HM_CHANGED_GUEST_RSI;
        if (fCpumIntern & CPUMCTX_EXTRN_RDI)
            fHmChanged |= HM_CHANGED_GUEST_RDI;
        if (fCpumIntern & CPUMCTX_EXTRN_R8_R15)
            fHmChanged |= HM_CHANGED_GUEST_R8_R15;
    }

    /* RIP & Flags */
    if (fCpumIntern & CPUMCTX_EXTRN_RIP)
        fHmChanged |= HM_CHANGED_GUEST_RIP;
    if (fCpumIntern & CPUMCTX_EXTRN_RFLAGS)
        fHmChanged |= HM_CHANGED_GUEST_RFLAGS;

    /* Segments */
    if (fCpumIntern & CPUMCTX_EXTRN_SREG_MASK)
    {
        if (fCpumIntern & CPUMCTX_EXTRN_ES)
            fHmChanged |= HM_CHANGED_GUEST_ES;
        if (fCpumIntern & CPUMCTX_EXTRN_CS)
            fHmChanged |= HM_CHANGED_GUEST_CS;
        if (fCpumIntern & CPUMCTX_EXTRN_SS)
            fHmChanged |= HM_CHANGED_GUEST_SS;
        if (fCpumIntern & CPUMCTX_EXTRN_DS)
            fHmChanged |= HM_CHANGED_GUEST_DS;
        if (fCpumIntern & CPUMCTX_EXTRN_FS)
            fHmChanged |= HM_CHANGED_GUEST_FS;
        if (fCpumIntern & CPUMCTX_EXTRN_GS)
            fHmChanged |= HM_CHANGED_GUEST_GS;
    }

    /* Descriptor tables & task segment. */
    if (fCpumIntern & CPUMCTX_EXTRN_TABLE_MASK)
    {
        if (fCpumIntern & CPUMCTX_EXTRN_LDTR)
            fHmChanged |= HM_CHANGED_GUEST_LDTR;
        if (fCpumIntern & CPUMCTX_EXTRN_TR)
            fHmChanged |= HM_CHANGED_GUEST_TR;
        if (fCpumIntern & CPUMCTX_EXTRN_IDTR)
            fHmChanged |= HM_CHANGED_GUEST_IDTR;
        if (fCpumIntern & CPUMCTX_EXTRN_GDTR)
            fHmChanged |= HM_CHANGED_GUEST_GDTR;
    }

    /* Control registers. */
    if (fCpumIntern & CPUMCTX_EXTRN_CR_MASK)
    {
        if (fCpumIntern & CPUMCTX_EXTRN_CR0)
            fHmChanged |= HM_CHANGED_GUEST_CR0;
        if (fCpumIntern & CPUMCTX_EXTRN_CR2)
            fHmChanged |= HM_CHANGED_GUEST_CR2;
        if (fCpumIntern & CPUMCTX_EXTRN_CR3)
            fHmChanged |= HM_CHANGED_GUEST_CR3;
        if (fCpumIntern & CPUMCTX_EXTRN_CR4)
            fHmChanged |= HM_CHANGED_GUEST_CR4;
    }
    if (fCpumIntern & CPUMCTX_EXTRN_APIC_TPR)
        fHmChanged |= HM_CHANGED_GUEST_APIC_TPR;

    /* Debug registers. */
    if (fCpumIntern & CPUMCTX_EXTRN_DR0_DR3)
        fHmChanged |= HM_CHANGED_GUEST_DR0_DR3;
    if (fCpumIntern & CPUMCTX_EXTRN_DR6)
        fHmChanged |= HM_CHANGED_GUEST_DR6;
    if (fCpumIntern & CPUMCTX_EXTRN_DR7)
        fHmChanged |= HM_CHANGED_GUEST_DR7;

    /* Floating point state. */
    if (fCpumIntern & CPUMCTX_EXTRN_X87)
        fHmChanged |= HM_CHANGED_GUEST_X87;
    if (fCpumIntern & CPUMCTX_EXTRN_SSE_AVX)
        fHmChanged |= HM_CHANGED_GUEST_SSE_AVX;
    if (fCpumIntern & CPUMCTX_EXTRN_OTHER_XSAVE)
        fHmChanged |= HM_CHANGED_GUEST_OTHER_XSAVE;
    if (fCpumIntern & CPUMCTX_EXTRN_XCRx)
        fHmChanged |= HM_CHANGED_GUEST_XCRx;

    /* MSRs */
    if (fCpumIntern & CPUMCTX_EXTRN_EFER)
        fHmChanged |= HM_CHANGED_GUEST_EFER_MSR;
    if (fCpumIntern & CPUMCTX_EXTRN_KERNEL_GS_BASE)
        fHmChanged |= HM_CHANGED_GUEST_KERNEL_GS_BASE;
    if (fCpumIntern & CPUMCTX_EXTRN_SYSENTER_MSRS)
        fHmChanged |= HM_CHANGED_GUEST_SYSENTER_MSR_MASK;
    if (fCpumIntern & CPUMCTX_EXTRN_SYSCALL_MSRS)
        fHmChanged |= HM_CHANGED_GUEST_SYSCALL_MSRS;
    if (fCpumIntern & CPUMCTX_EXTRN_TSC_AUX)
        fHmChanged |= HM_CHANGED_GUEST_TSC_AUX;
    if (fCpumIntern & CPUMCTX_EXTRN_OTHER_MSRS)
        fHmChanged |= HM_CHANGED_GUEST_OTHER_MSRS;

    return fHmChanged;
}


/**
 * Exports the guest state to HV for execution.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pVmxTransient   The transient VMX structure.
 */
static int nemR3DarwinExportGuestState(PVMCC pVM, PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
#define WRITE_GREG(a_GReg, a_Value) \
    do \
    { \
        hv_return_t hrc = hv_vcpu_write_register(pVCpu->nem.s.hVCpuId, (a_GReg), (a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define WRITE_VMCS_FIELD(a_Field, a_Value) \
    do \
    { \
        hv_return_t hrc = hv_vmx_vcpu_write_vmcs(pVCpu->nem.s.hVCpuId, (a_Field), (a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            return VERR_INTERNAL_ERROR; \
    } while(0)
#define WRITE_MSR(a_Msr, a_Value) \
    do \
    { \
        hv_return_t hrc = hv_vcpu_write_msr(pVCpu->nem.s.hVCpuId, (a_Msr), (a_Value)); \
        if (RT_LIKELY(hrc == HV_SUCCESS)) \
        { /* likely */ } \
        else \
            AssertFailedReturn(VERR_INTERNAL_ERROR); \
    } while(0)

    RT_NOREF(pVM);

#ifdef LOG_ENABLED
    nemR3DarwinLogState(pVM, pVCpu);
#endif

    STAM_PROFILE_ADV_START(&pVCpu->nem.s.StatProfGstStateExport, x);

    uint64_t const fWhat = ~pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL;
    if (!fWhat)
        return VINF_SUCCESS;

    pVCpu->nem.s.fCtxChanged |= nemR3DarwinCpumExtrnToHmChanged(pVCpu->cpum.GstCtx.fExtrn);

    int rc = vmxHCExportGuestEntryExitCtls(pVCpu, pVmxTransient);
    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc\n", rc), rc);

    rc = nemR3DarwinExportGuestGprs(pVCpu);
    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc\n", rc), rc);

    rc = vmxHCExportGuestCR0(pVCpu, pVmxTransient);
    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc\n", rc), rc);

    VBOXSTRICTRC rcStrict = vmxHCExportGuestCR3AndCR4(pVCpu, pVmxTransient);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
    {
        Assert(rcStrict == VINF_EM_RESCHEDULE_REM || RT_FAILURE_NP(rcStrict));
        return VBOXSTRICTRC_VAL(rcStrict);
    }

    rc = nemR3DarwinExportDebugState(pVCpu, pVmxTransient);
    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc\n", rc), rc);

    vmxHCExportGuestXcptIntercepts(pVCpu, pVmxTransient);
    vmxHCExportGuestRip(pVCpu);
    //vmxHCExportGuestRsp(pVCpu);
    vmxHCExportGuestRflags(pVCpu, pVmxTransient);

    rc = vmxHCExportGuestSegRegsXdtr(pVCpu, pVmxTransient);
    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc\n", rc), rc);

    if (fWhat & CPUMCTX_EXTRN_XCRx)
    {
        WRITE_GREG(HV_X86_XCR0, pVCpu->cpum.GstCtx.aXcr[0]);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_XCRx);
    }

    if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
    {
        Assert(pVCpu->nem.s.fCtxChanged & HM_CHANGED_GUEST_APIC_TPR);
        vmxHCExportGuestApicTpr(pVCpu, pVmxTransient);

        rc = APICGetTpr(pVCpu, &pVmxTransient->u8GuestTpr, NULL /*pfPending*/, NULL /*pu8PendingIntr*/);
        AssertRC(rc);

        WRITE_GREG(HV_X86_TPR, pVmxTransient->u8GuestTpr);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_APIC_TPR);
    }

    /* Debug registers. */
    if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
    {
        WRITE_GREG(HV_X86_DR0, CPUMGetHyperDR0(pVCpu));
        WRITE_GREG(HV_X86_DR1, CPUMGetHyperDR1(pVCpu));
        WRITE_GREG(HV_X86_DR2, CPUMGetHyperDR2(pVCpu));
        WRITE_GREG(HV_X86_DR3, CPUMGetHyperDR3(pVCpu));
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_DR0_DR3);
    }
    if (fWhat & CPUMCTX_EXTRN_DR6)
    {
        WRITE_GREG(HV_X86_DR6, CPUMGetHyperDR6(pVCpu));
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_DR6);
    }
    if (fWhat & CPUMCTX_EXTRN_DR7)
    {
        WRITE_GREG(HV_X86_DR7, CPUMGetHyperDR7(pVCpu));
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_DR7);
    }

    if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
    {
        hv_return_t hrc = hv_vcpu_write_fpstate(pVCpu->nem.s.hVCpuId, &pVCpu->cpum.GstCtx.XState, sizeof(pVCpu->cpum.GstCtx.XState));
        if (hrc == HV_SUCCESS)
        { /* likely */ }
        else
            return nemR3DarwinHvSts2Rc(hrc);

        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~(HM_CHANGED_GUEST_X87 | HM_CHANGED_GUEST_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE));
    }

    /* MSRs */
    if (fWhat & CPUMCTX_EXTRN_EFER)
    {
        WRITE_VMCS_FIELD(VMX_VMCS64_GUEST_EFER_FULL, pVCpu->cpum.GstCtx.msrEFER);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_EFER_MSR);
    }
    if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
    {
        WRITE_MSR(MSR_K8_KERNEL_GS_BASE, pVCpu->cpum.GstCtx.msrKERNELGSBASE);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_KERNEL_GS_BASE);
    }
    if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        WRITE_MSR(MSR_IA32_SYSENTER_CS, pVCpu->cpum.GstCtx.SysEnter.cs);
        WRITE_MSR(MSR_IA32_SYSENTER_EIP, pVCpu->cpum.GstCtx.SysEnter.eip);
        WRITE_MSR(MSR_IA32_SYSENTER_ESP, pVCpu->cpum.GstCtx.SysEnter.esp);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_SYSENTER_MSR_MASK);
    }
    if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        WRITE_MSR(MSR_K6_STAR, pVCpu->cpum.GstCtx.msrSTAR);
        WRITE_MSR(MSR_K8_LSTAR, pVCpu->cpum.GstCtx.msrLSTAR);
        WRITE_MSR(MSR_K8_CSTAR, pVCpu->cpum.GstCtx.msrCSTAR);
        WRITE_MSR(MSR_K8_SF_MASK, pVCpu->cpum.GstCtx.msrSFMASK);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_SYSCALL_MSRS);
    }
    if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
    {
        PCPUMCTXMSRS pCtxMsrs = CPUMQueryGuestCtxMsrsPtr(pVCpu);

        WRITE_MSR(MSR_K8_TSC_AUX, pCtxMsrs->msr.TscAux);
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_TSC_AUX);
    }
    if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
    {
        /* Last Branch Record. */
        if (pVM->nem.s.fLbr)
        {
            PVMXVMCSINFOSHARED const pVmcsInfoShared = &pVCpu->nem.s.vmx.VmcsInfo;
            uint32_t const idFromIpMsrStart = pVM->nem.s.idLbrFromIpMsrFirst;
            uint32_t const idToIpMsrStart   = pVM->nem.s.idLbrToIpMsrFirst;
            uint32_t const idInfoMsrStart   = pVM->nem.s.idLbrInfoMsrFirst;
            uint32_t const cLbrStack        = pVM->nem.s.idLbrFromIpMsrLast - pVM->nem.s.idLbrFromIpMsrFirst + 1;
            Assert(cLbrStack <= 32);
            for (uint32_t i = 0; i < cLbrStack; i++)
            {
                WRITE_MSR(idFromIpMsrStart + i, pVmcsInfoShared->au64LbrFromIpMsr[i]);

                /* Some CPUs don't have a Branch-To-IP MSR (P4 and related Xeons). */
                if (idToIpMsrStart != 0)
                    WRITE_MSR(idToIpMsrStart + i, pVmcsInfoShared->au64LbrToIpMsr[i]);
                if (idInfoMsrStart != 0)
                    WRITE_MSR(idInfoMsrStart + i, pVmcsInfoShared->au64LbrInfoMsr[i]);
            }

            WRITE_MSR(pVM->nem.s.idLbrTosMsr, pVmcsInfoShared->u64LbrTosMsr);
            if (pVM->nem.s.idLerFromIpMsr)
                WRITE_MSR(pVM->nem.s.idLerFromIpMsr, pVmcsInfoShared->u64LerFromIpMsr);
            if (pVM->nem.s.idLerToIpMsr)
                WRITE_MSR(pVM->nem.s.idLerToIpMsr, pVmcsInfoShared->u64LerToIpMsr);
        }

        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_OTHER_MSRS);
    }

    hv_vcpu_invalidate_tlb(pVCpu->nem.s.hVCpuId);
    hv_vcpu_flush(pVCpu->nem.s.hVCpuId);

    pVCpu->cpum.GstCtx.fExtrn |= CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_KEEPER_NEM;

    /* Clear any bits that may be set but exported unconditionally or unused/reserved bits. */
    ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~(  HM_CHANGED_GUEST_HWVIRT
                                                   | HM_CHANGED_VMX_GUEST_AUTO_MSRS
                                                   | HM_CHANGED_VMX_GUEST_LAZY_MSRS
                                                   | (HM_CHANGED_KEEPER_STATE_MASK & ~HM_CHANGED_VMX_MASK)));

    STAM_PROFILE_ADV_STOP(&pVCpu->nem.s.StatProfGstStateExport, x);
    return VINF_SUCCESS;
#undef WRITE_GREG
#undef WRITE_VMCS_FIELD
}


/**
 * Common worker for both nemR3DarwinHandleExit() and nemR3DarwinHandleExitDebug().
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pVmxTransient   The transient VMX structure.
 */
DECLINLINE(int) nemR3DarwinHandleExitCommon(PVM pVM, PVMCPU pVCpu, PVMXTRANSIENT pVmxTransient)
{
    uint32_t uExitReason;
    int rc = nemR3DarwinReadVmcs32(pVCpu, VMX_VMCS32_RO_EXIT_REASON, &uExitReason);
    AssertRC(rc);
    pVmxTransient->fVmcsFieldsRead = 0;
    pVmxTransient->fIsNestedGuest  = false;
    pVmxTransient->uExitReason     = VMX_EXIT_REASON_BASIC(uExitReason);
    pVmxTransient->fVMEntryFailed  = VMX_EXIT_REASON_HAS_ENTRY_FAILED(uExitReason);

    if (RT_UNLIKELY(pVmxTransient->fVMEntryFailed))
        AssertLogRelMsgFailedReturn(("Running guest failed for CPU #%u: %#x %u\n",
                                    pVCpu->idCpu, pVmxTransient->uExitReason, vmxHCCheckGuestState(pVCpu, &pVCpu->nem.s.VmcsInfo)),
                                    VERR_NEM_IPE_0);

    /** @todo Only copy the state on demand (the R0 VT-x code saves some stuff unconditionally and the VMX template assumes that
     * when handling exits). */
    /*
     * Note! What is being fetched here must match the default value for the
     *       a_fDonePostExit parameter of vmxHCImportGuestState exactly!
     */
    rc = nemR3DarwinCopyStateFromHv(pVM, pVCpu, CPUMCTX_EXTRN_ALL);
    AssertRCReturn(rc, rc);

    STAM_COUNTER_INC(&pVCpu->nem.s.pVmxStats->aStatExitReason[pVmxTransient->uExitReason & MASK_EXITREASON_STAT]);
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.pVmxStats->StatExitAll);
    return VINF_SUCCESS;
}


/**
 * Handles an exit from hv_vcpu_run().
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pVmxTransient   The transient VMX structure.
 */
static VBOXSTRICTRC nemR3DarwinHandleExit(PVM pVM, PVMCPU pVCpu, PVMXTRANSIENT pVmxTransient)
{
    int rc = nemR3DarwinHandleExitCommon(pVM, pVCpu, pVmxTransient);
    AssertRCReturn(rc, rc);

#ifndef HMVMX_USE_FUNCTION_TABLE
    return vmxHCHandleExit(pVCpu, pVmxTransient);
#else
    return g_aVMExitHandlers[pVmxTransient->uExitReason].pfn(pVCpu, pVmxTransient);
#endif
}


/**
 * Handles an exit from hv_vcpu_run() - debug runloop variant.
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pVmxTransient   The transient VMX structure.
 * @param   pDbgState       The debug state structure.
 */
static VBOXSTRICTRC nemR3DarwinHandleExitDebug(PVM pVM, PVMCPU pVCpu, PVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState)
{
    int rc = nemR3DarwinHandleExitCommon(pVM, pVCpu, pVmxTransient);
    AssertRCReturn(rc, rc);

    return vmxHCRunDebugHandleExit(pVCpu, pVmxTransient, pDbgState);
}


/**
 * Worker for nemR3NativeInit that loads the Hypervisor.framework shared library.
 *
 * @returns VBox status code.
 * @param   fForced             Whether the HMForced flag is set and we should
 *                              fail if we cannot initialize.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3DarwinLoadHv(bool fForced, PRTERRINFO pErrInfo)
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
            {
                if (g_aImports[i].fOptional)
                    LogRel(("NEM:  info: Found optional import Hypervisor!%s.\n",
                            g_aImports[i].pszName));
            }
            else
            {
                *g_aImports[i].ppfn = NULL;

                LogRel(("NEM:  %s: Failed to import Hypervisor!%s: %Rrc\n",
                        g_aImports[i].fOptional ? "info" : fForced ? "fatal" : "error",
                        g_aImports[i].pszName, rc2));
                if (!g_aImports[i].fOptional)
                {
                    if (RTErrInfoIsSet(pErrInfo))
                        RTErrInfoAddF(pErrInfo, rc2, ", Hypervisor!%s", g_aImports[i].pszName);
                    else
                        rc = RTErrInfoSetF(pErrInfo, rc2, "Failed to import: Hypervisor!%s", g_aImports[i].pszName);
                    Assert(RT_FAILURE(rc));
                }
            }
        }
        if (RT_SUCCESS(rc))
        {
            Assert(!RTErrInfoIsSet(pErrInfo));
        }

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
 * Read and initialize the global capabilities supported by this CPU.
 *
 * @returns VBox status code.
 */
static int nemR3DarwinCapsInit(PVM pVM)
{
    RT_ZERO(g_HmMsrs);

    hv_return_t hrc = hv_vmx_read_capability(HV_VMX_CAP_PINBASED, &g_HmMsrs.u.vmx.PinCtls.u);
    if (hrc == HV_SUCCESS)
        hrc = hv_vmx_read_capability(HV_VMX_CAP_PROCBASED, &g_HmMsrs.u.vmx.ProcCtls.u);
    if (hrc == HV_SUCCESS)
        hrc = hv_vmx_read_capability(HV_VMX_CAP_ENTRY, &g_HmMsrs.u.vmx.EntryCtls.u);
    if (hrc == HV_SUCCESS)
        hrc = hv_vmx_read_capability(HV_VMX_CAP_EXIT, &g_HmMsrs.u.vmx.ExitCtls.u);
    if (hrc == HV_SUCCESS)
    {
        hrc = hv_vmx_read_capability(HV_VMX_CAP_BASIC, &g_HmMsrs.u.vmx.u64Basic);
        if (hrc == HV_SUCCESS)
        {
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_MISC, &g_HmMsrs.u.vmx.u64Misc);
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_CR0_FIXED0, &g_HmMsrs.u.vmx.u64Cr0Fixed0);
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_CR0_FIXED1, &g_HmMsrs.u.vmx.u64Cr0Fixed1);
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_CR4_FIXED0, &g_HmMsrs.u.vmx.u64Cr4Fixed0);
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_CR4_FIXED1, &g_HmMsrs.u.vmx.u64Cr4Fixed1);
            if (hrc == HV_SUCCESS)
                hrc = hv_vmx_read_capability(HV_VMX_CAP_VMCS_ENUM, &g_HmMsrs.u.vmx.u64VmcsEnum);
            if (   hrc == HV_SUCCESS
                && RT_BF_GET(g_HmMsrs.u.vmx.u64Basic, VMX_BF_BASIC_TRUE_CTLS))
            {
                hrc = hv_vmx_read_capability(HV_VMX_CAP_TRUE_PINBASED, &g_HmMsrs.u.vmx.TruePinCtls.u);
                if (hrc == HV_SUCCESS)
                    hrc = hv_vmx_read_capability(HV_VMX_CAP_TRUE_PROCBASED, &g_HmMsrs.u.vmx.TrueProcCtls.u);
                if (hrc == HV_SUCCESS)
                    hrc = hv_vmx_read_capability(HV_VMX_CAP_TRUE_ENTRY, &g_HmMsrs.u.vmx.TrueEntryCtls.u);
                if (hrc == HV_SUCCESS)
                    hrc = hv_vmx_read_capability(HV_VMX_CAP_TRUE_EXIT, &g_HmMsrs.u.vmx.TrueExitCtls.u);
            }
        }
        else
        {
            /* Likely running on anything < 11.0 (BigSur) so provide some sensible defaults. */
            g_HmMsrs.u.vmx.u64Cr0Fixed0 = 0x80000021;
            g_HmMsrs.u.vmx.u64Cr0Fixed1 = 0xffffffff;
            g_HmMsrs.u.vmx.u64Cr4Fixed0 = 0x2000;
            g_HmMsrs.u.vmx.u64Cr4Fixed1 = 0x1767ff;
            hrc = HV_SUCCESS;
        }
    }

    if (   hrc == HV_SUCCESS
        && g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
    {
        hrc = hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2, &g_HmMsrs.u.vmx.ProcCtls2.u);

        if (   hrc == HV_SUCCESS
            && g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & (VMX_PROC_CTLS2_EPT | VMX_PROC_CTLS2_VPID))
        {
            hrc = hv_vmx_read_capability(HV_VMX_CAP_EPT_VPID_CAP, &g_HmMsrs.u.vmx.u64EptVpidCaps);
            if (hrc != HV_SUCCESS)
                hrc = HV_SUCCESS; /* Probably just outdated OS. */
        }

        g_HmMsrs.u.vmx.u64VmFunc = 0; /* No way to read that on macOS. */
    }

    if (hrc == HV_SUCCESS)
    {
        /*
         * Check for EFER swapping support.
         */
        g_fHmVmxSupportsVmcsEfer = true; //(g_HmMsrs.u.vmx.EntryCtls.n.allowed1 & VMX_ENTRY_CTLS_LOAD_EFER_MSR)
                                //&& (g_HmMsrs.u.vmx.ExitCtls.n.allowed1  & VMX_EXIT_CTLS_LOAD_EFER_MSR)
                                //&& (g_HmMsrs.u.vmx.ExitCtls.n.allowed1  & VMX_EXIT_CTLS_SAVE_EFER_MSR);
    }

    /*
     * Get MSR_IA32_ARCH_CAPABILITIES and expand it into the host feature structure.
     *
     * This is only available with 11.0+ (BigSur) as the required API is only available there,
     * we could in theory initialize this when creating the EMTs using hv_vcpu_read_msr() but
     * the required vCPU handle is created after CPUM was initialized which is too late.
     * Given that the majority of users is on 11.0 and later we don't care for now.
     *
     * (Yes, this is done after CPUM init.)
     */
    uint64_t fHostArchVal = 0;
    bool     fHasArchCap  = false;
    if (   hrc == HV_SUCCESS
        && hv_vmx_get_msr_info)
    {
        uint32_t const cStdRange = ASMCpuId_EAX(0);
        if (   RTX86IsValidStdRange(cStdRange)
            && cStdRange >= 7)
        {
            uint32_t const fStdFeaturesEdx = ASMCpuId_EDX(1);
            uint32_t fStdExtFeaturesEdx;
            ASMCpuIdExSlow(7, 0, 0, 0, NULL, NULL, NULL, &fStdExtFeaturesEdx);
            if (   (fStdExtFeaturesEdx & X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP)
                && (fStdFeaturesEdx    & X86_CPUID_FEATURE_EDX_MSR))
            {
                fHasArchCap = true;
                hrc = hv_vmx_get_msr_info(HV_VMX_INFO_MSR_IA32_ARCH_CAPABILITIES, &fHostArchVal);
                if (hrc != HV_SUCCESS)
                    fHostArchVal = 0;
            }
        }
    }
    CPUMCpuIdApplyX86HostArchCapabilities(pVM, fHasArchCap, fHostArchVal);

    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Sets up the LBR MSR ranges based on the host CPU.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 *
 * @sa hmR0VmxSetupLbrMsrRange
 */
static int nemR3DarwinSetupLbrMsrRange(PVMCC pVM)
{
    Assert(pVM->nem.s.fLbr);
    uint32_t idLbrFromIpMsrFirst;
    uint32_t idLbrFromIpMsrLast;
    uint32_t idLbrToIpMsrFirst;
    uint32_t idLbrToIpMsrLast;
    uint32_t idLbrInfoMsrFirst;
    uint32_t idLbrInfoMsrLast;
    uint32_t idLbrTosMsr;
    uint32_t idLbrSelectMsr;
    uint32_t idLerFromIpMsr;
    uint32_t idLerToIpMsr;

    /*
     * Determine the LBR MSRs supported for this host CPU family and model.
     *
     * See Intel spec. 17.4.8 "LBR Stack".
     * See Intel "Model-Specific Registers" spec.
     */
    uint32_t const uFamilyModel = (g_CpumHostFeatures.s.uFamily << 8)
                                | g_CpumHostFeatures.s.uModel;
    switch (uFamilyModel)
    {
        case 0x0f01: case 0x0f02:
            idLbrFromIpMsrFirst = MSR_P4_LASTBRANCH_0;
            idLbrFromIpMsrLast  = MSR_P4_LASTBRANCH_3;
            idLbrToIpMsrFirst   = 0x0;
            idLbrToIpMsrLast    = 0x0;
            idLbrInfoMsrFirst   = 0x0;
            idLbrInfoMsrLast    = 0x0;
            idLbrTosMsr         = MSR_P4_LASTBRANCH_TOS;
            idLbrSelectMsr      = 0x0;
            idLerFromIpMsr      = 0x0;
            idLerToIpMsr        = 0x0;
            break;

        case 0x065c: case 0x065f: case 0x064e: case 0x065e: case 0x068e:
        case 0x069e: case 0x0655: case 0x0666: case 0x067a: case 0x0667:
        case 0x066a: case 0x066c: case 0x067d: case 0x067e:
            idLbrFromIpMsrFirst = MSR_LASTBRANCH_0_FROM_IP;
            idLbrFromIpMsrLast  = MSR_LASTBRANCH_31_FROM_IP;
            idLbrToIpMsrFirst   = MSR_LASTBRANCH_0_TO_IP;
            idLbrToIpMsrLast    = MSR_LASTBRANCH_31_TO_IP;
            idLbrInfoMsrFirst   = MSR_LASTBRANCH_0_INFO;
            idLbrInfoMsrLast    = MSR_LASTBRANCH_31_INFO;
            idLbrTosMsr         = MSR_LASTBRANCH_TOS;
            idLbrSelectMsr      = MSR_LASTBRANCH_SELECT;
            idLerFromIpMsr      = MSR_LER_FROM_IP;
            idLerToIpMsr        = MSR_LER_TO_IP;
            break;

        case 0x063d: case 0x0647: case 0x064f: case 0x0656: case 0x063c:
        case 0x0645: case 0x0646: case 0x063f: case 0x062a: case 0x062d:
        case 0x063a: case 0x063e: case 0x061a: case 0x061e: case 0x061f:
        case 0x062e: case 0x0625: case 0x062c: case 0x062f:
            idLbrFromIpMsrFirst = MSR_LASTBRANCH_0_FROM_IP;
            idLbrFromIpMsrLast  = MSR_LASTBRANCH_15_FROM_IP;
            idLbrToIpMsrFirst   = MSR_LASTBRANCH_0_TO_IP;
            idLbrToIpMsrLast    = MSR_LASTBRANCH_15_TO_IP;
            idLbrInfoMsrFirst   = MSR_LASTBRANCH_0_INFO;
            idLbrInfoMsrLast    = MSR_LASTBRANCH_15_INFO;
            idLbrTosMsr         = MSR_LASTBRANCH_TOS;
            idLbrSelectMsr      = MSR_LASTBRANCH_SELECT;
            idLerFromIpMsr      = MSR_LER_FROM_IP;
            idLerToIpMsr        = MSR_LER_TO_IP;
            break;

        case 0x0617: case 0x061d: case 0x060f:
            idLbrFromIpMsrFirst = MSR_CORE2_LASTBRANCH_0_FROM_IP;
            idLbrFromIpMsrLast  = MSR_CORE2_LASTBRANCH_3_FROM_IP;
            idLbrToIpMsrFirst   = MSR_CORE2_LASTBRANCH_0_TO_IP;
            idLbrToIpMsrLast    = MSR_CORE2_LASTBRANCH_3_TO_IP;
            idLbrInfoMsrFirst   = 0x0;
            idLbrInfoMsrLast    = 0x0;
            idLbrTosMsr         = MSR_CORE2_LASTBRANCH_TOS;
            idLbrSelectMsr      = 0x0;
            idLerFromIpMsr      = 0x0;
            idLerToIpMsr        = 0x0;
            break;

        /* Atom and related microarchitectures we don't care about:
        case 0x0637: case 0x064a: case 0x064c: case 0x064d: case 0x065a:
        case 0x065d: case 0x061c: case 0x0626: case 0x0627: case 0x0635:
        case 0x0636: */
        /* All other CPUs: */
        default:
        {
            LogRelFunc(("Could not determine LBR stack size for the CPU model %#x\n", uFamilyModel));
            VMCC_GET_CPU_0(pVM)->nem.s.u32HMError = VMX_UFC_LBR_STACK_SIZE_UNKNOWN;
            return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
        }
    }

    /*
     * Validate.
     */
    uint32_t const cLbrStack = idLbrFromIpMsrLast - idLbrFromIpMsrFirst + 1;
    PCVMCPU pVCpu0 = VMCC_GET_CPU_0(pVM);
    AssertCompile(   RT_ELEMENTS(pVCpu0->nem.s.vmx.VmcsInfo.au64LbrFromIpMsr)
                  == RT_ELEMENTS(pVCpu0->nem.s.vmx.VmcsInfo.au64LbrToIpMsr));
    AssertCompile(   RT_ELEMENTS(pVCpu0->nem.s.vmx.VmcsInfo.au64LbrFromIpMsr)
                  == RT_ELEMENTS(pVCpu0->nem.s.vmx.VmcsInfo.au64LbrInfoMsr));
    if (cLbrStack > RT_ELEMENTS(pVCpu0->nem.s.vmx.VmcsInfo.au64LbrFromIpMsr))
    {
        LogRelFunc(("LBR stack size of the CPU (%u) exceeds our buffer size\n", cLbrStack));
        VMCC_GET_CPU_0(pVM)->nem.s.u32HMError = VMX_UFC_LBR_STACK_SIZE_OVERFLOW;
        return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
    }
    NOREF(pVCpu0);

    /*
     * Update the LBR info. to the VM struct. for use later.
     */
    pVM->nem.s.idLbrTosMsr         = idLbrTosMsr;
    pVM->nem.s.idLbrSelectMsr      = idLbrSelectMsr;

    pVM->nem.s.idLbrFromIpMsrFirst = idLbrFromIpMsrFirst;
    pVM->nem.s.idLbrFromIpMsrLast  = idLbrFromIpMsrLast;

    pVM->nem.s.idLbrToIpMsrFirst   = idLbrToIpMsrFirst;
    pVM->nem.s.idLbrToIpMsrLast    = idLbrToIpMsrLast;

    pVM->nem.s.idLbrInfoMsrFirst   = idLbrInfoMsrFirst;
    pVM->nem.s.idLbrInfoMsrLast    = idLbrInfoMsrLast;

    pVM->nem.s.idLerFromIpMsr      = idLerFromIpMsr;
    pVM->nem.s.idLerToIpMsr        = idLerToIpMsr;
    return VINF_SUCCESS;
}


/**
 * Sets up pin-based VM-execution controls in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static int nemR3DarwinVmxSetupVmcsPinCtls(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    //PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    uint32_t       fVal = g_HmMsrs.u.vmx.PinCtls.n.allowed0;      /* Bits set here must always be set. */
    uint32_t const fZap = g_HmMsrs.u.vmx.PinCtls.n.allowed1;      /* Bits cleared here must always be cleared. */

    if (g_HmMsrs.u.vmx.PinCtls.n.allowed1 & VMX_PIN_CTLS_VIRT_NMI)
        fVal |= VMX_PIN_CTLS_VIRT_NMI;                       /* Use virtual NMIs and virtual-NMI blocking features. */

#if 0 /** @todo Use preemption timer */
    /* Enable the VMX-preemption timer. */
    if (pVM->hmr0.s.vmx.fUsePreemptTimer)
    {
        Assert(g_HmMsrs.u.vmx.PinCtls.n.allowed1 & VMX_PIN_CTLS_PREEMPT_TIMER);
        fVal |= VMX_PIN_CTLS_PREEMPT_TIMER;
    }

    /* Enable posted-interrupt processing. */
    if (pVM->hm.s.fPostedIntrs)
    {
        Assert(g_HmMsrs.u.vmx.PinCtls.n.allowed1  & VMX_PIN_CTLS_POSTED_INT);
        Assert(g_HmMsrs.u.vmx.ExitCtls.n.allowed1 & VMX_EXIT_CTLS_ACK_EXT_INT);
        fVal |= VMX_PIN_CTLS_POSTED_INT;
    }
#endif

    if ((fVal & fZap) != fVal)
    {
        LogRelFunc(("Invalid pin-based VM-execution controls combo! Cpu=%#RX32 fVal=%#RX32 fZap=%#RX32\n",
                    g_HmMsrs.u.vmx.PinCtls.n.allowed0, fVal, fZap));
        pVCpu->nem.s.u32HMError = VMX_UFC_CTRL_PIN_EXEC;
        return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
    }

    /* Commit it to the VMCS and update our cache. */
    int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PIN_EXEC, fVal);
    AssertRC(rc);
    pVmcsInfo->u32PinCtls = fVal;

    return VINF_SUCCESS;
}


/**
 * Sets up secondary processor-based VM-execution controls in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static int nemR3DarwinVmxSetupVmcsProcCtls2(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    uint32_t       fVal = g_HmMsrs.u.vmx.ProcCtls2.n.allowed0;    /* Bits set here must be set in the VMCS. */
    uint32_t const fZap = g_HmMsrs.u.vmx.ProcCtls2.n.allowed1;    /* Bits cleared here must be cleared in the VMCS. */

    /* WBINVD causes a VM-exit. */
    if (g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_WBINVD_EXIT)
        fVal |= VMX_PROC_CTLS2_WBINVD_EXIT;

    /* Enable the INVPCID instruction if we expose it to the guest and is supported
       by the hardware. Without this, guest executing INVPCID would cause a #UD. */
    if (   pVM->cpum.ro.GuestFeatures.fInvpcid
        && (g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_INVPCID))
        fVal |= VMX_PROC_CTLS2_INVPCID;

#if 0 /** @todo */
    /* Enable VPID. */
    if (pVM->hmr0.s.vmx.fVpid)
        fVal |= VMX_PROC_CTLS2_VPID;

    if (pVM->hm.s.fVirtApicRegs)
    {
        /* Enable APIC-register virtualization. */
        Assert(g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_APIC_REG_VIRT);
        fVal |= VMX_PROC_CTLS2_APIC_REG_VIRT;

        /* Enable virtual-interrupt delivery. */
        Assert(g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_VIRT_INTR_DELIVERY);
        fVal |= VMX_PROC_CTLS2_VIRT_INTR_DELIVERY;
    }

    /* Virtualize-APIC accesses if supported by the CPU. The virtual-APIC page is
       where the TPR shadow resides. */
    /** @todo VIRT_X2APIC support, it's mutually exclusive with this. So must be
     *        done dynamically. */
    if (g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS)
    {
        fVal |= VMX_PROC_CTLS2_VIRT_APIC_ACCESS;
        hmR0VmxSetupVmcsApicAccessAddr(pVCpu);
    }
#endif

    /* Enable the RDTSCP instruction if we expose it to the guest and is supported
       by the hardware. Without this, guest executing RDTSCP would cause a #UD. */
    if (   pVM->cpum.ro.GuestFeatures.fRdTscP
        && (g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_RDTSCP))
        fVal |= VMX_PROC_CTLS2_RDTSCP;

    /* Enable Pause-Loop exiting. */
    if (   (g_HmMsrs.u.vmx.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_PAUSE_LOOP_EXIT)
        && pVM->nem.s.cPleGapTicks
        && pVM->nem.s.cPleWindowTicks)
    {
        fVal |= VMX_PROC_CTLS2_PAUSE_LOOP_EXIT;

        int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PLE_GAP, pVM->nem.s.cPleGapTicks);          AssertRC(rc);
        rc     = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PLE_WINDOW, pVM->nem.s.cPleWindowTicks);    AssertRC(rc);
    }

    if ((fVal & fZap) != fVal)
    {
        LogRelFunc(("Invalid secondary processor-based VM-execution controls combo! cpu=%#RX32 fVal=%#RX32 fZap=%#RX32\n",
                    g_HmMsrs.u.vmx.ProcCtls2.n.allowed0, fVal, fZap));
        pVCpu->nem.s.u32HMError = VMX_UFC_CTRL_PROC_EXEC2;
        return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
    }

    /* Commit it to the VMCS and update our cache. */
    int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, fVal);
    AssertRC(rc);
    pVmcsInfo->u32ProcCtls2 = fVal;

    return VINF_SUCCESS;
}


/**
 * Enables native access for the given MSR.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR to enable native access for.
 */
static int nemR3DarwinMsrSetNative(PVMCPUCC pVCpu, uint32_t idMsr)
{
    hv_return_t hrc = hv_vcpu_enable_native_msr(pVCpu->nem.s.hVCpuId, idMsr, true /*enable*/);
    if (hrc == HV_SUCCESS)
        return VINF_SUCCESS;

    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Sets the MSR to managed for the given vCPU allowing the guest to access it.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR to enable managed access for.
 * @param   fMsrPerm    The MSR permissions flags.
 */
static int nemR3DarwinMsrSetManaged(PVMCPUCC pVCpu, uint32_t idMsr, hv_msr_flags_t fMsrPerm)
{
    Assert(hv_vcpu_enable_managed_msr);

    hv_return_t hrc = hv_vcpu_enable_managed_msr(pVCpu->nem.s.hVCpuId, idMsr, true /*enable*/);
    if (hrc == HV_SUCCESS)
    {
        hrc = hv_vcpu_set_msr_access(pVCpu->nem.s.hVCpuId, idMsr, fMsrPerm);
        if (hrc == HV_SUCCESS)
            return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
}


/**
 * Sets up the MSR permissions which don't change through the lifetime of the VM.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static int nemR3DarwinSetupVmcsMsrPermissions(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    RT_NOREF(pVmcsInfo);

    /*
     * The guest can access the following MSRs (read, write) without causing
     * VM-exits; they are loaded/stored automatically using fields in the VMCS.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    int rc;
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_SYSENTER_CS);  AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_SYSENTER_ESP); AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_SYSENTER_EIP); AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_GS_BASE);        AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_FS_BASE);        AssertRCReturn(rc, rc);

    /*
     * The IA32_PRED_CMD and IA32_FLUSH_CMD MSRs are write-only and has no state
     * associated with then. We never need to intercept access (writes need to be
     * executed without causing a VM-exit, reads will #GP fault anyway).
     *
     * The IA32_SPEC_CTRL MSR is read/write and has state. We allow the guest to
     * read/write them. We swap the guest/host MSR value using the
     * auto-load/store MSR area.
     */
    if (pVM->cpum.ro.GuestFeatures.fIbpb)
    {
        rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_PRED_CMD);
        AssertRCReturn(rc, rc);
    }
#if 0 /* Doesn't work. */
    if (pVM->cpum.ro.GuestFeatures.fFlushCmd)
    {
        rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_FLUSH_CMD);
        AssertRCReturn(rc, rc);
    }
#endif
    if (pVM->cpum.ro.GuestFeatures.fIbrs)
    {
        rc = nemR3DarwinMsrSetNative(pVCpu, MSR_IA32_SPEC_CTRL);
        AssertRCReturn(rc, rc);
    }

    /*
     * Allow full read/write access for the following MSRs (mandatory for VT-x)
     * required for 64-bit guests.
     */
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_LSTAR);          AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K6_STAR);           AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_SF_MASK);        AssertRCReturn(rc, rc);
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_KERNEL_GS_BASE); AssertRCReturn(rc, rc);

    /* Required for enabling the RDTSCP instruction. */
    rc = nemR3DarwinMsrSetNative(pVCpu, MSR_K8_TSC_AUX);        AssertRCReturn(rc, rc);

    /* Last Branch Record. */
    if (pVM->nem.s.fLbr)
    {
        uint32_t const idFromIpMsrStart = pVM->nem.s.idLbrFromIpMsrFirst;
        uint32_t const idToIpMsrStart   = pVM->nem.s.idLbrToIpMsrFirst;
        uint32_t const idInfoMsrStart   = pVM->nem.s.idLbrInfoMsrFirst;
        uint32_t const cLbrStack        = pVM->nem.s.idLbrFromIpMsrLast - pVM->nem.s.idLbrFromIpMsrFirst + 1;
        Assert(cLbrStack <= 32);
        for (uint32_t i = 0; i < cLbrStack; i++)
        {
            rc = nemR3DarwinMsrSetManaged(pVCpu, idFromIpMsrStart + i, HV_MSR_READ | HV_MSR_WRITE);
            AssertRCReturn(rc, rc);

            /* Some CPUs don't have a Branch-To-IP MSR (P4 and related Xeons). */
            if (idToIpMsrStart != 0)
            {
                rc = nemR3DarwinMsrSetManaged(pVCpu, idToIpMsrStart + i, HV_MSR_READ | HV_MSR_WRITE);
                AssertRCReturn(rc, rc);
            }

            if (idInfoMsrStart != 0)
            {
                rc = nemR3DarwinMsrSetManaged(pVCpu, idInfoMsrStart + i, HV_MSR_READ | HV_MSR_WRITE);
                AssertRCReturn(rc, rc);
            }
        }

        rc = nemR3DarwinMsrSetManaged(pVCpu, pVM->nem.s.idLbrTosMsr, HV_MSR_READ | HV_MSR_WRITE);
        AssertRCReturn(rc, rc);

        if (pVM->nem.s.idLerFromIpMsr)
        {
            rc = nemR3DarwinMsrSetManaged(pVCpu, pVM->nem.s.idLerFromIpMsr, HV_MSR_READ | HV_MSR_WRITE);
            AssertRCReturn(rc, rc);
        }

        if (pVM->nem.s.idLerToIpMsr)
        {
            rc = nemR3DarwinMsrSetManaged(pVCpu, pVM->nem.s.idLerToIpMsr, HV_MSR_READ | HV_MSR_WRITE);
            AssertRCReturn(rc, rc);
        }

        if (pVM->nem.s.idLbrSelectMsr)
        {
            rc = nemR3DarwinMsrSetManaged(pVCpu, pVM->nem.s.idLbrSelectMsr, HV_MSR_READ | HV_MSR_WRITE);
            AssertRCReturn(rc, rc);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Sets up processor-based VM-execution controls in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static int nemR3DarwinVmxSetupVmcsProcCtls(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    uint32_t       fVal = g_HmMsrs.u.vmx.ProcCtls.n.allowed0;     /* Bits set here must be set in the VMCS. */
    uint32_t const fZap = g_HmMsrs.u.vmx.ProcCtls.n.allowed1;     /* Bits cleared here must be cleared in the VMCS. */

    /** @todo The DRx handling is not quite correct breaking debugging inside the guest with gdb,
     * see @ticketref{21413} and @ticketref{21546}, so intercepting mov drX is disabled for now. See @bugref{10504}
     * as well. This will break the hypervisor debugger but only very few people use it and even less on macOS
     * using the NEM backend.
     */
    fVal |= VMX_PROC_CTLS_HLT_EXIT                                    /* HLT causes a VM-exit. */
//         |  VMX_PROC_CTLS_USE_TSC_OFFSETTING                          /* Use TSC-offsetting. */
//         |  VMX_PROC_CTLS_MOV_DR_EXIT                                 /* MOV DRx causes a VM-exit. */
         |  VMX_PROC_CTLS_UNCOND_IO_EXIT                              /* All IO instructions cause a VM-exit. */
         |  VMX_PROC_CTLS_RDPMC_EXIT                                  /* RDPMC causes a VM-exit. */
         |  VMX_PROC_CTLS_MONITOR_EXIT                                /* MONITOR causes a VM-exit. */
         |  VMX_PROC_CTLS_MWAIT_EXIT;                                 /* MWAIT causes a VM-exit. */

#ifdef HMVMX_ALWAYS_INTERCEPT_CR3_ACCESS
    fVal |= VMX_PROC_CTLS_CR3_LOAD_EXIT
         |  VMX_PROC_CTLS_CR3_STORE_EXIT;
#endif

    /* We toggle VMX_PROC_CTLS_MOV_DR_EXIT later, check if it's not -always- needed to be set or clear. */
    if (   !(g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_MOV_DR_EXIT)
        ||  (g_HmMsrs.u.vmx.ProcCtls.n.allowed0 & VMX_PROC_CTLS_MOV_DR_EXIT))
    {
        pVCpu->nem.s.u32HMError = VMX_UFC_CTRL_PROC_MOV_DRX_EXIT;
        return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
    }

    /* Use the secondary processor-based VM-execution controls if supported by the CPU. */
    if (g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
        fVal |= VMX_PROC_CTLS_USE_SECONDARY_CTLS;

    if ((fVal & fZap) != fVal)
    {
        LogRelFunc(("Invalid processor-based VM-execution controls combo! cpu=%#RX32 fVal=%#RX32 fZap=%#RX32\n",
                    g_HmMsrs.u.vmx.ProcCtls.n.allowed0, fVal, fZap));
        pVCpu->nem.s.u32HMError = VMX_UFC_CTRL_PROC_EXEC;
        return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
    }

    /* Commit it to the VMCS and update our cache. */
    int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, fVal);
    AssertRC(rc);
    pVmcsInfo->u32ProcCtls = fVal;

    /* Set up MSR permissions that don't change through the lifetime of the VM. */
    rc = nemR3DarwinSetupVmcsMsrPermissions(pVCpu, pVmcsInfo);
    AssertRCReturn(rc, rc);

    /*
     * Set up secondary processor-based VM-execution controls
     * (we assume the CPU to always support it as we rely on unrestricted guest execution support).
     */
    Assert(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_SECONDARY_CTLS);
    return nemR3DarwinVmxSetupVmcsProcCtls2(pVCpu, pVmcsInfo);
}


/**
 * Sets up miscellaneous (everything other than Pin, Processor and secondary
 * Processor-based VM-execution) control fields in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static int nemR3DarwinVmxSetupVmcsMiscCtls(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    int rc = VINF_SUCCESS;
    //rc = hmR0VmxSetupVmcsAutoLoadStoreMsrAddrs(pVmcsInfo); TODO
    if (RT_SUCCESS(rc))
    {
        uint64_t const u64Cr0Mask = vmxHCGetFixedCr0Mask(pVCpu);
        uint64_t const u64Cr4Mask = vmxHCGetFixedCr4Mask(pVCpu);

        rc = nemR3DarwinWriteVmcs64(pVCpu, VMX_VMCS_CTRL_CR0_MASK, u64Cr0Mask);    AssertRC(rc);
        rc = nemR3DarwinWriteVmcs64(pVCpu, VMX_VMCS_CTRL_CR4_MASK, u64Cr4Mask);    AssertRC(rc);

        pVmcsInfo->u64Cr0Mask = u64Cr0Mask;
        pVmcsInfo->u64Cr4Mask = u64Cr4Mask;

        if (pVCpu->CTX_SUFF(pVM)->nem.s.fLbr)
        {
            rc = nemR3DarwinWriteVmcs64(pVCpu, VMX_VMCS64_GUEST_DEBUGCTL_FULL, MSR_IA32_DEBUGCTL_LBR);
            AssertRC(rc);
        }
        return VINF_SUCCESS;
    }
    else
        LogRelFunc(("Failed to initialize VMCS auto-load/store MSR addresses. rc=%Rrc\n", rc));
    return rc;
}


/**
 * Sets up the initial exception bitmap in the VMCS based on static conditions.
 *
 * We shall setup those exception intercepts that don't change during the
 * lifetime of the VM here. The rest are done dynamically while loading the
 * guest state.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static void nemR3DarwinVmxSetupVmcsXcptBitmap(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    /*
     * The following exceptions are always intercepted:
     *
     * #AC - To prevent the guest from hanging the CPU and for dealing with
     *       split-lock detecting host configs.
     * #DB - To maintain the DR6 state even when intercepting DRx reads/writes and
     *       recursive #DBs can cause a CPU hang.
     */
    /** @todo The DRx handling is not quite correct breaking debugging inside the guest with gdb,
     * see @ticketref{21413} and @ticketref{21546}, so intercepting \#DB is disabled for now. See @bugref{10504}
     * as well. This will break the hypervisor debugger but only very few people use it and even less on macOS
     * using the NEM backend.
     */
    uint32_t const uXcptBitmap = RT_BIT(X86_XCPT_AC)
                               /*| RT_BIT(X86_XCPT_DB)*/;

    /* Commit it to the VMCS. */
    int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, uXcptBitmap);
    AssertRC(rc);

    /* Update our cache of the exception bitmap. */
    pVmcsInfo->u32XcptBitmap = uXcptBitmap;
}


/**
 * Initialize the VMCS information field for the given vCPU.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 */
static int nemR3DarwinInitVmcs(PVMCPU pVCpu)
{
    int rc = nemR3DarwinVmxSetupVmcsPinCtls(pVCpu, &pVCpu->nem.s.VmcsInfo);
    if (RT_SUCCESS(rc))
    {
        rc = nemR3DarwinVmxSetupVmcsProcCtls(pVCpu, &pVCpu->nem.s.VmcsInfo);
        if (RT_SUCCESS(rc))
        {
            rc = nemR3DarwinVmxSetupVmcsMiscCtls(pVCpu, &pVCpu->nem.s.VmcsInfo);
            if (RT_SUCCESS(rc))
            {
                rc = nemR3DarwinReadVmcs32(pVCpu, VMX_VMCS32_CTRL_ENTRY, &pVCpu->nem.s.VmcsInfo.u32EntryCtls);
                if (RT_SUCCESS(rc))
                {
                    rc = nemR3DarwinReadVmcs32(pVCpu, VMX_VMCS32_CTRL_EXIT, &pVCpu->nem.s.VmcsInfo.u32ExitCtls);
                    if (RT_SUCCESS(rc))
                    {
                        nemR3DarwinVmxSetupVmcsXcptBitmap(pVCpu, &pVCpu->nem.s.VmcsInfo);
                        return VINF_SUCCESS;
                    }
                    LogRelFunc(("Failed to read the exit controls. rc=%Rrc\n", rc));
                }
                else
                    LogRelFunc(("Failed to read the entry controls. rc=%Rrc\n", rc));
            }
            else
                LogRelFunc(("Failed to setup miscellaneous controls. rc=%Rrc\n", rc));
        }
        else
            LogRelFunc(("Failed to setup processor-based VM-execution controls. rc=%Rrc\n", rc));
    }
    else
        LogRelFunc(("Failed to setup pin-based controls. rc=%Rrc\n", rc));

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

    PVMXSTATISTICS const pVmxStats = pNemCpu->pVmxStats;

    NEM_REG_COUNTER(&pVmxStats->StatExitCR0Read,  "/NEM/CPU%u/Exit/Instr/CR-Read/CR0", "CR0 read.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR2Read,  "/NEM/CPU%u/Exit/Instr/CR-Read/CR2", "CR2 read.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR3Read,  "/NEM/CPU%u/Exit/Instr/CR-Read/CR3", "CR3 read.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR4Read,  "/NEM/CPU%u/Exit/Instr/CR-Read/CR4", "CR4 read.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR8Read,  "/NEM/CPU%u/Exit/Instr/CR-Read/CR8", "CR8 read.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR0Write, "/NEM/CPU%u/Exit/Instr/CR-Write/CR0", "CR0 write.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR2Write, "/NEM/CPU%u/Exit/Instr/CR-Write/CR2", "CR2 write.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR3Write, "/NEM/CPU%u/Exit/Instr/CR-Write/CR3", "CR3 write.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR4Write, "/NEM/CPU%u/Exit/Instr/CR-Write/CR4", "CR4 write.");
    NEM_REG_COUNTER(&pVmxStats->StatExitCR8Write, "/NEM/CPU%u/Exit/Instr/CR-Write/CR8", "CR8 write.");

    NEM_REG_COUNTER(&pVmxStats->StatExitAll,      "/NEM/CPU%u/Exit/All", "Total exits (including nested-guest exits).");

    NEM_REG_COUNTER(&pVmxStats->StatImportGuestStateFallback, "/NEM/CPU%u/ImportGuestStateFallback", "Times vmxHCImportGuestState took the fallback code path.");
    NEM_REG_COUNTER(&pVmxStats->StatReadToTransientFallback,  "/NEM/CPU%u/ReadToTransientFallback",  "Times vmxHCReadToTransient took the fallback code path.");

#ifdef VBOX_WITH_STATISTICS
    NEM_REG_PROFILE(&pNemCpu->StatProfGstStateImport, "/NEM/CPU%u/ImportGuestState", "Profiling of importing guest state from hardware after VM-exit.");
    NEM_REG_PROFILE(&pNemCpu->StatProfGstStateExport, "/NEM/CPU%u/ExportGuestState", "Profiling of exporting guest state from hardware after VM-exit.");

    for (int j = 0; j < MAX_EXITREASON_STAT; j++)
    {
        const char *pszExitName = HMGetVmxExitName(j);
        if (pszExitName)
        {
            int rc = STAMR3RegisterF(pVM, &pVmxStats->aStatExitReason[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                     STAMUNIT_OCCURENCES, pszExitName, "/NEM/CPU%u/Exit/Reason/%02x", idCpu, j);
            AssertRCReturn(rc, rc);
        }
    }
#endif

    return VINF_SUCCESS;

#undef NEM_REG_COUNTER
#undef NEM_REG_PROFILE
#undef NEM_REG_STAT
}


/**
 * Displays the HM Last-Branch-Record info. for the guest.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) nemR3DarwinInfoLbr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    Assert(pVM->nem.s.fLbr);

    PCVMXVMCSINFOSHARED pVmcsInfoShared = &pVCpu->nem.s.vmx.VmcsInfo;
    uint32_t const      cLbrStack       = pVM->nem.s.idLbrFromIpMsrLast - pVM->nem.s.idLbrFromIpMsrFirst + 1;

    /** @todo r=ramshankar: The index technically varies depending on the CPU, but
     *        0xf should cover everything we support thus far. Fix if necessary
     *        later. */
    uint32_t const idxTopOfStack = pVmcsInfoShared->u64LbrTosMsr & 0xf;
    if (idxTopOfStack > cLbrStack)
    {
        pHlp->pfnPrintf(pHlp, "Top-of-stack LBR MSR seems corrupt (index=%u, msr=%#RX64) expected index < %u\n",
                        idxTopOfStack, pVmcsInfoShared->u64LbrTosMsr, cLbrStack);
        return;
    }

    /*
     * Dump the circular buffer of LBR records starting from the most recent record (contained in idxTopOfStack).
     */
    pHlp->pfnPrintf(pHlp, "CPU[%u]: LBRs (most-recent first)\n", pVCpu->idCpu);
    if (pVM->nem.s.idLerFromIpMsr)
        pHlp->pfnPrintf(pHlp, "LER: From IP=%#016RX64 - To IP=%#016RX64\n",
                        pVmcsInfoShared->u64LerFromIpMsr, pVmcsInfoShared->u64LerToIpMsr);
    uint32_t idxCurrent = idxTopOfStack;
    Assert(idxTopOfStack < cLbrStack);
    Assert(RT_ELEMENTS(pVmcsInfoShared->au64LbrFromIpMsr) <= cLbrStack);
    Assert(RT_ELEMENTS(pVmcsInfoShared->au64LbrToIpMsr) <= cLbrStack);
    for (;;)
    {
        if (pVM->nem.s.idLbrToIpMsrFirst)
            pHlp->pfnPrintf(pHlp, "  Branch (%2u): From IP=%#016RX64 - To IP=%#016RX64 (Info: %#016RX64)\n", idxCurrent,
                            pVmcsInfoShared->au64LbrFromIpMsr[idxCurrent],
                            pVmcsInfoShared->au64LbrToIpMsr[idxCurrent],
                            pVmcsInfoShared->au64LbrInfoMsr[idxCurrent]);
        else
            pHlp->pfnPrintf(pHlp, "  Branch (%2u): LBR=%#RX64\n", idxCurrent, pVmcsInfoShared->au64LbrFromIpMsr[idxCurrent]);

        idxCurrent = (idxCurrent - 1) % cLbrStack;
        if (idxCurrent == idxTopOfStack)
            break;
    }
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

    /** @cfgm{/NEM/VmxPleGap, uint32_t, 0}
     * The pause-filter exiting gap in TSC ticks. When the number of ticks between
     * two successive PAUSE instructions exceeds VmxPleGap, the CPU considers the
     * latest PAUSE instruction to be start of a new PAUSE loop.
     */
    int rc = CFGMR3QueryU32Def(pCfgNem, "VmxPleGap", &pVM->nem.s.cPleGapTicks, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/NEM/VmxPleWindow, uint32_t, 0}
     * The pause-filter exiting window in TSC ticks. When the number of ticks
     * between the current PAUSE instruction and first PAUSE of a loop exceeds
     * VmxPleWindow, a VM-exit is triggered.
     *
     * Setting VmxPleGap and VmxPleGap to 0 disables pause-filter exiting.
     */
    rc = CFGMR3QueryU32Def(pCfgNem, "VmxPleWindow", &pVM->nem.s.cPleWindowTicks, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/NEM/VmxLbr, bool, false}
     * Whether to enable LBR for the guest. This is disabled by default as it's only
     * useful while debugging and enabling it causes a noticeable performance hit. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "VmxLbr", &pVM->nem.s.fLbr, false);
    AssertRCReturn(rc, rc);

    /*
     * Error state.
     * The error message will be non-empty on failure and 'rc' will be set too.
     */
    RTERRINFOSTATIC ErrInfo;
    PRTERRINFO pErrInfo = RTErrInfoInitStatic(&ErrInfo);
    rc = nemR3DarwinLoadHv(fForced, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (   !hv_vcpu_enable_managed_msr
            && pVM->nem.s.fLbr)
        {
            LogRel(("NEM: LBR recording is disabled because the Hypervisor API misses hv_vcpu_enable_managed_msr/hv_vcpu_set_msr_access functionality\n"));
            pVM->nem.s.fLbr = false;
        }

        /*
         * While hv_vcpu_run_until() is available starting with Catalina (10.15) it sometimes returns
         * an error there for no obvious reasons and there is no indication as to why this happens
         * and Apple doesn't document anything. Starting with BigSur (11.0) it appears to work correctly
         * so pretend that hv_vcpu_run_until() doesn't exist on Catalina which can be determined by checking
         * whether another method is available which was introduced with BigSur.
         */
        if (!hv_vmx_get_msr_info) /* Not available means this runs on < 11.0 */
            hv_vcpu_run_until = NULL;

        if (hv_vcpu_run_until)
        {
            struct mach_timebase_info TimeInfo;

            if (mach_timebase_info(&TimeInfo) == KERN_SUCCESS)
            {
                pVM->nem.s.cMachTimePerNs = RT_MIN(1, (double)TimeInfo.denom / (double)TimeInfo.numer);
                LogRel(("NEM: cMachTimePerNs=%llu (TimeInfo.numer=%u TimeInfo.denom=%u)\n",
                        pVM->nem.s.cMachTimePerNs, TimeInfo.numer, TimeInfo.denom));
            }
            else
                hv_vcpu_run_until = NULL; /* To avoid running forever (TM asserts when the guest runs for longer than 4 seconds). */
        }

        hv_return_t hrc = hv_vm_create(HV_VM_DEFAULT);
        if (hrc == HV_SUCCESS)
        {
            if (hv_vm_space_create)
            {
                hrc = hv_vm_space_create(&pVM->nem.s.uVmAsid);
                if (hrc == HV_SUCCESS)
                {
                    LogRel(("NEM: Successfully created ASID: %u\n", pVM->nem.s.uVmAsid));
                    pVM->nem.s.fCreatedAsid = true;
                }
                else
                    LogRel(("NEM: Failed to create ASID for VM (hrc=%#x), continuing...\n", pVM->nem.s.uVmAsid));
            }
            pVM->nem.s.fCreatedVm = true;

            /* Register release statistics */
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
            {
                PNEMCPU pNemCpu = &pVM->apCpusR3[idCpu]->nem.s;
                PVMXSTATISTICS pVmxStats = (PVMXSTATISTICS)RTMemAllocZ(sizeof(*pVmxStats));
                if (RT_LIKELY(pVmxStats))
                {
                        pNemCpu->pVmxStats = pVmxStats;
                        rc = nemR3DarwinStatisticsRegister(pVM, idCpu, pNemCpu);
                        AssertRC(rc);
                }
                else
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }
            }

            if (RT_SUCCESS(rc))
            {
                VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_NATIVE_API);
                Log(("NEM: Marked active!\n"));
                PGMR3EnableNemMode(pVM);
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                               "hv_vm_create() failed: %#x", hrc);
    }

    /*
     * We only fail if in forced mode, otherwise just log the complaint and return.
     */
    Assert(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API || RTErrInfoIsSet(pErrInfo));
    if (   (fForced || !fFallback)
        && pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NATIVE_API)
        return VMSetError(pVM, RT_SUCCESS_NP(rc) ? VERR_NEM_NOT_AVAILABLE : rc, RT_SRC_POS, "%s", pErrInfo->pszMsg);

    if (pVM->nem.s.fLbr)
    {
        rc = DBGFR3InfoRegisterInternalEx(pVM, "lbr", "Dumps the NEM LBR info.", nemR3DarwinInfoLbr, DBGFINFO_FLAGS_ALL_EMTS);
        AssertRCReturn(rc, rc);
    }

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
    hv_return_t hrc = hv_vcpu_create(&pVCpu->nem.s.hVCpuId, HV_VCPU_DEFAULT);
    if (hrc != HV_SUCCESS)
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Call to hv_vcpu_create failed on vCPU %u: %#x (%Rrc)", idCpu, hrc, nemR3DarwinHvSts2Rc(hrc));

    if (idCpu == 0)
    {
        /* First call initializs the MSR structure holding the capabilities of the host CPU. */
        int rc = nemR3DarwinCapsInit(pVM);
        AssertRCReturn(rc, rc);

        if (hv_vmx_vcpu_get_cap_write_vmcs)
        {
            /* Log the VMCS field write capabilities. */
            for (uint32_t i = 0; i < RT_ELEMENTS(g_aVmcsFieldsCap); i++)
            {
                uint64_t u64Allowed0 = 0;
                uint64_t u64Allowed1 = 0;

                hrc = hv_vmx_vcpu_get_cap_write_vmcs(pVCpu->nem.s.hVCpuId, g_aVmcsFieldsCap[i].u32VmcsFieldId,
                                                     &u64Allowed0, &u64Allowed1);
                if (hrc == HV_SUCCESS)
                {
                    if (g_aVmcsFieldsCap[i].f64Bit)
                        LogRel(("NEM:    %s = (allowed_0=%#016RX64 allowed_1=%#016RX64)\n",
                                g_aVmcsFieldsCap[i].pszVmcsField, u64Allowed0, u64Allowed1));
                    else
                        LogRel(("NEM:    %s = (allowed_0=%#08RX32 allowed_1=%#08RX32)\n",
                                g_aVmcsFieldsCap[i].pszVmcsField, (uint32_t)u64Allowed0, (uint32_t)u64Allowed1));

                    uint32_t cBits = g_aVmcsFieldsCap[i].f64Bit ? 64 : 32;
                    for (uint32_t iBit = 0; iBit < cBits; iBit++)
                    {
                        bool fAllowed0 = RT_BOOL(u64Allowed0 & RT_BIT_64(iBit));
                        bool fAllowed1 = RT_BOOL(u64Allowed1 & RT_BIT_64(iBit));

                        if (!fAllowed0 && !fAllowed1)
                            LogRel(("NEM:        Bit %02u = Must NOT be set\n", iBit));
                        else if (!fAllowed0 && fAllowed1)
                            LogRel(("NEM:        Bit %02u = Can be set or not be set\n", iBit));
                        else if (fAllowed0 && !fAllowed1)
                            LogRel(("NEM:        Bit %02u = UNDEFINED (AppleHV error)!\n", iBit));
                        else if (fAllowed0 && fAllowed1)
                            LogRel(("NEM:        Bit %02u = MUST be set\n", iBit));
                        else
                            AssertFailed();
                    }
                }
                else
                    LogRel(("NEM:    %s = failed to query (hrc=%d)\n", g_aVmcsFieldsCap[i].pszVmcsField, hrc));
            }
        }
    }

    int rc = nemR3DarwinInitVmcs(pVCpu);
    AssertRCReturn(rc, rc);

    if (pVM->nem.s.fCreatedAsid)
    {
        hrc = hv_vcpu_set_space(pVCpu->nem.s.hVCpuId, pVM->nem.s.uVmAsid);
        AssertReturn(hrc == HV_SUCCESS, VERR_NEM_VM_CREATE_FAILED);
    }

    ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_ALL_GUEST);

    return VINF_SUCCESS;
}


/**
 * Worker to destroy the vCPU handle on the EMT running it later on (as required by HV).
 *
 * @returns VBox status code
 * @param   pVCpu               The vCPU handle.
 */
static DECLCALLBACK(int) nemR3DarwinNativeTermVCpuOnEmt(PVMCPU pVCpu)
{
    hv_return_t hrc = hv_vcpu_set_space(pVCpu->nem.s.hVCpuId, 0 /*asid*/);
    Assert(hrc == HV_SUCCESS);

    hrc = hv_vcpu_destroy(pVCpu->nem.s.hVCpuId);
    Assert(hrc == HV_SUCCESS); RT_NOREF(hrc);
    return VINF_SUCCESS;
}


/**
 * Worker to setup the TPR shadowing feature if available on the CPU and the VM has an APIC enabled.
 *
 * @returns VBox status code
 * @param   pVM                 The VM handle.
 * @param   pVCpu               The vCPU handle.
 */
static DECLCALLBACK(int) nemR3DarwinNativeInitTprShadowing(PVM pVM, PVMCPU pVCpu)
{
    PVMXVMCSINFO pVmcsInfo = &pVCpu->nem.s.VmcsInfo;
    uint32_t fVal = pVmcsInfo->u32ProcCtls;

    /* Use TPR shadowing if supported by the CPU. */
    if (   PDMHasApic(pVM)
        && (g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_TPR_SHADOW))
    {
        fVal |= VMX_PROC_CTLS_USE_TPR_SHADOW;                /* CR8 reads from the Virtual-APIC page. */
                                                             /* CR8 writes cause a VM-exit based on TPR threshold. */
        Assert(!(fVal & VMX_PROC_CTLS_CR8_STORE_EXIT));
        Assert(!(fVal & VMX_PROC_CTLS_CR8_LOAD_EXIT));
    }
    else
    {
        fVal |= VMX_PROC_CTLS_CR8_STORE_EXIT             /* CR8 reads cause a VM-exit. */
             |  VMX_PROC_CTLS_CR8_LOAD_EXIT;             /* CR8 writes cause a VM-exit. */
    }

    /* Commit it to the VMCS and update our cache. */
    int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, fVal);
    AssertRC(rc);
    pVmcsInfo->u32ProcCtls = fVal;

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

    if (pVM->nem.s.fLbr)
    {
        int rc = nemR3DarwinSetupLbrMsrRange(pVM);
        AssertRCReturn(rc, rc);
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
                VMR3ReqCallWait(pVM, idCpu, (PFNRT)nemR3DarwinNativeTermVCpuOnEmt, 1, pVCpu);

            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS, "Call to hv_vcpu_create failed: %Rrc", rc);
        }
    }

    pVM->nem.s.fCreatedEmts = true;
    return VINF_SUCCESS;
}


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    if (enmWhat == VMINITCOMPLETED_RING3)
    {
        /* Now that PDM is initialized the APIC state is known in order to enable the TPR shadowing feature on all EMTs. */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];

            int rc = VMR3ReqCallWait(pVM, idCpu, (PFNRT)nemR3DarwinNativeInitTprShadowing, 2, pVM, pVCpu);
            if (RT_FAILURE(rc))
                return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS, "Setting up TPR shadowing failed: %Rrc", rc);
        }
    }
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
         * Need to do this or hv_vm_space_destroy() fails later on (on 10.15 at least). Could've been documented in
         * API reference so I wouldn't have to decompile the kext to find this out but we are talking
         * about Apple here unfortunately, API documentation is not their strong suit...
         * Would have been of course even better to just automatically drop the address space reference when the vCPU
         * gets destroyed.
         */
        hv_return_t hrc = hv_vcpu_set_space(pVCpu->nem.s.hVCpuId, 0 /*asid*/);
        Assert(hrc == HV_SUCCESS);

        /*
         * Apple's documentation states that the vCPU should be destroyed
         * on the thread running the vCPU but as all the other EMTs are gone
         * at this point, destroying the VM would hang.
         *
         * We seem to be at luck here though as destroying apparently works
         * from EMT(0) as well.
         */
        hrc = hv_vcpu_destroy(pVCpu->nem.s.hVCpuId);
        Assert(hrc == HV_SUCCESS); RT_NOREF(hrc);

        if (pVCpu->nem.s.pVmxStats)
        {
            RTMemFree(pVCpu->nem.s.pVmxStats);
            pVCpu->nem.s.pVmxStats = NULL;
        }
    }

    pVM->nem.s.fCreatedEmts = false;

    if (pVM->nem.s.fCreatedAsid)
    {
        hv_return_t hrc = hv_vm_space_destroy(pVM->nem.s.uVmAsid);
        Assert(hrc == HV_SUCCESS); RT_NOREF(hrc);
        pVM->nem.s.fCreatedAsid = false;
    }

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
    RT_NOREF(fInitIpi);
    ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
}


/**
 * Dumps the VMCS in response to a faild hv_vcpu_run{_until}() call.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void nemR3DarwinVmcsDump(PVMCPU pVCpu)
{
    static const struct
    {
        uint32_t    u32VmcsFieldId;  /**< The VMCS field identifier. */
        const char  *pszVmcsField;   /**< The VMCS field name. */
        bool        f64Bit;
    } s_aVmcsFieldsDump[] =
    {
    #define NEM_DARWIN_VMCSNW_FIELD_DUMP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, true  }
    #define NEM_DARWIN_VMCS64_FIELD_DUMP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, true  }
    #define NEM_DARWIN_VMCS32_FIELD_DUMP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, false }
    #define NEM_DARWIN_VMCS16_FIELD_DUMP(a_u32VmcsFieldId) { (a_u32VmcsFieldId), #a_u32VmcsFieldId, false }
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_VPID),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_POSTED_INT_NOTIFY_VECTOR),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_EPTP_INDEX),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_ES_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_CS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_SS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_DS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_FS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_GS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_LDTR_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_TR_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_INTR_STATUS),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_GUEST_PML_INDEX),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_ES_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_CS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_SS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_DS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_FS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_GS_SEL),
        NEM_DARWIN_VMCS16_FIELD_DUMP(VMX_VMCS16_HOST_TR_SEL),

        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_IO_BITMAP_A_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_IO_BITMAP_A_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_IO_BITMAP_B_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_IO_BITMAP_B_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_MSR_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_MSR_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXIT_MSR_STORE_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXIT_MSR_STORE_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXIT_MSR_LOAD_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXIT_MSR_LOAD_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXEC_VMCS_PTR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXEC_VMCS_PTR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXEC_PML_ADDR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EXEC_PML_ADDR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_TSC_OFFSET_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_TSC_OFFSET_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_APIC_ACCESSADDR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_APIC_ACCESSADDR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_POSTED_INTR_DESC_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_POSTED_INTR_DESC_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMFUNC_CTRLS_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMFUNC_CTRLS_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EPTP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EPTP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_0_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_0_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_1_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_1_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_2_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_2_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_3_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EOI_BITMAP_3_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EPTP_LIST_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_EPTP_LIST_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMREAD_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMREAD_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMWRITE_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VMWRITE_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_SPPTP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_SPPTP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_TSC_MULTIPLIER_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_TSC_MULTIPLIER_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_PROC_EXEC3_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_PROC_EXEC3_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_RO_GUEST_PHYS_ADDR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_VMCS_LINK_PTR_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_DEBUGCTL_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_DEBUGCTL_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PAT_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PAT_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_EFER_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_EFER_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE0_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE0_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE1_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE1_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE2_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE2_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE3_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PDPTE3_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_BNDCFGS_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_BNDCFGS_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_RTIT_CTL_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_RTIT_CTL_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PKRS_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_GUEST_PKRS_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PAT_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PAT_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_EFER_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_EFER_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_HIGH),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PKRS_FULL),
        NEM_DARWIN_VMCS64_FIELD_DUMP(VMX_VMCS64_HOST_PKRS_HIGH),

        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PIN_EXEC),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PROC_EXEC),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_EXCEPTION_BITMAP),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MASK),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MATCH),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_CR3_TARGET_COUNT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_EXIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_EXIT_MSR_STORE_COUNT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_EXIT_MSR_LOAD_COUNT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_ENTRY),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_ENTRY_MSR_LOAD_COUNT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_TPR_THRESHOLD),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PROC_EXEC2),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PLE_GAP),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_CTRL_PLE_WINDOW),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_VM_INSTR_ERROR),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_EXIT_REASON),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_IDT_VECTORING_INFO),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_EXIT_INSTR_LENGTH),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_RO_EXIT_INSTR_INFO),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_ES_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_CS_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_SS_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_DS_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_FS_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_GS_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_LDTR_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_TR_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_GDTR_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_IDTR_LIMIT),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_CS_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_SS_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_DS_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_FS_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_GS_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_INT_STATE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_ACTIVITY_STATE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_SMBASE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_GUEST_SYSENTER_CS),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_PREEMPT_TIMER_VALUE),
        NEM_DARWIN_VMCS32_FIELD_DUMP(VMX_VMCS32_HOST_SYSENTER_CS),

        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR0_MASK),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR4_MASK),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR0_READ_SHADOW),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR4_READ_SHADOW),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR3_TARGET_VAL0),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR3_TARGET_VAL1),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR3_TARGET_VAL2),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_CTRL_CR3_TARGET_VAL3),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_EXIT_QUALIFICATION),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_IO_RCX),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_IO_RSI),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_IO_RDI),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_IO_RIP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_RO_GUEST_LINEAR_ADDR),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_CR0),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_CR3),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_CR4),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_ES_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_CS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_SS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_DS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_FS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_GS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_LDTR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_TR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_GDTR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_IDTR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_DR7),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_RSP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_RIP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_RFLAGS),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_SYSENTER_ESP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_SYSENTER_EIP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_S_CET),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_SSP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_GUEST_INTR_SSP_TABLE_ADDR),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_CR0),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_CR3),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_CR4),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_FS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_GS_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_TR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_GDTR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_IDTR_BASE),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_SYSENTER_ESP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_SYSENTER_EIP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_RSP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_RIP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_S_CET),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_SSP),
        NEM_DARWIN_VMCSNW_FIELD_DUMP(VMX_VMCS_HOST_INTR_SSP_TABLE_ADDR)
    #undef NEM_DARWIN_VMCSNW_FIELD_DUMP
    #undef NEM_DARWIN_VMCS64_FIELD_DUMP
    #undef NEM_DARWIN_VMCS32_FIELD_DUMP
    #undef NEM_DARWIN_VMCS16_FIELD_DUMP
    };

    for (uint32_t i = 0; i < RT_ELEMENTS(s_aVmcsFieldsDump); i++)
    {
        if (s_aVmcsFieldsDump[i].f64Bit)
        {
            uint64_t u64Val;
            int rc = nemR3DarwinReadVmcs64(pVCpu, s_aVmcsFieldsDump[i].u32VmcsFieldId, &u64Val);
            if (RT_SUCCESS(rc))
                LogRel(("NEM/VMCS: %040s: 0x%016RX64\n", s_aVmcsFieldsDump[i].pszVmcsField, u64Val));
            else
                LogRel(("NEM/VMCS: %040s: rc=%Rrc\n", s_aVmcsFieldsDump[i].pszVmcsField, rc));
        }
        else
        {
            uint32_t u32Val;
            int rc = nemR3DarwinReadVmcs32(pVCpu, s_aVmcsFieldsDump[i].u32VmcsFieldId, &u32Val);
            if (RT_SUCCESS(rc))
                LogRel(("NEM/VMCS: %040s: 0x%08RX32\n", s_aVmcsFieldsDump[i].pszVmcsField, u32Val));
            else
                LogRel(("NEM/VMCS: %040s: rc=%Rrc\n", s_aVmcsFieldsDump[i].pszVmcsField, rc));
        }
    }
}


/**
 * Runs the guest once until an exit occurs.
 *
 * @returns HV status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The transient VMX execution structure.
 */
static hv_return_t nemR3DarwinRunGuest(PVM pVM, PVMCPU pVCpu, PVMXTRANSIENT pVmxTransient)
{
    TMNotifyStartOfExecution(pVM, pVCpu);
    Assert(!pVCpu->nem.s.fCtxChanged);

    if (!(pVCpu->nem.s.fMdsClearOnVmEntry | pVCpu->nem.s.fMdsClearOnVmEntry))
    { /* likely*/ }
    else
    {
        uint16_t u16 = ASMGetDS();
        __asm__ __volatile__("verw %0" : "=m" (u16) : "0" (u16));
    }

    hv_return_t hrc;
    if (hv_vcpu_run_until) /** @todo Configur the deadline dynamically based on when the next timer triggers. */
        hrc = hv_vcpu_run_until(pVCpu->nem.s.hVCpuId, mach_absolute_time() + 2 * RT_NS_1SEC_64 * pVM->nem.s.cMachTimePerNs);
    else
        hrc = hv_vcpu_run(pVCpu->nem.s.hVCpuId);

    TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());

    if (hrc != HV_SUCCESS)
        nemR3DarwinVmcsDump(pVCpu);

    /*
     * Sync the TPR shadow with our APIC state.
     */
    if (   !pVmxTransient->fIsNestedGuest
        && (pVCpu->nem.s.VmcsInfo.u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW))
    {
        uint64_t u64Tpr;
        hv_return_t hrc2 = hv_vcpu_read_register(pVCpu->nem.s.hVCpuId, HV_X86_TPR, &u64Tpr);
        Assert(hrc2 == HV_SUCCESS); RT_NOREF(hrc2);

        if (pVmxTransient->u8GuestTpr != (uint8_t)u64Tpr)
        {
            int rc = APICSetTpr(pVCpu, (uint8_t)u64Tpr);
            AssertRC(rc);
            ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }
    }

    return hrc;
}


/**
 * Prepares the VM to run the guest.
 *
 * @returns Strict VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pVmxTransient       The VMX transient state.
 * @param   fSingleStepping     Flag whether we run in single stepping mode.
 */
static VBOXSTRICTRC nemR3DarwinPreRunGuest(PVM pVM, PVMCPU pVCpu, PVMXTRANSIENT pVmxTransient, bool fSingleStepping)
{
    /*
     * Check and process force flag actions, some of which might require us to go back to ring-3.
     */
    VBOXSTRICTRC rcStrict = vmxHCCheckForceFlags(pVCpu, false /*fIsNestedGuest*/, fSingleStepping);
    if (rcStrict == VINF_SUCCESS)
    { /*likely */ }
    else
        return rcStrict;

    /*
     * Do not execute in HV if the A20 isn't enabled.
     */
    if (PGMPhysIsA20Enabled(pVCpu))
    { /* likely */ }
    else
    {
        LogFlow(("NEM/%u: breaking: A20 disabled\n", pVCpu->idCpu));
        return VINF_EM_RESCHEDULE_REM;
    }

    /*
     * Evaluate events to be injected into the guest.
     *
     * Events in TRPM can be injected without inspecting the guest state.
     * If any new events (interrupts/NMI) are pending currently, we try to set up the
     * guest to cause a VM-exit the next time they are ready to receive the event.
     */
    if (TRPMHasTrap(pVCpu))
        vmxHCTrpmTrapToPendingEvent(pVCpu);

    uint32_t fIntrState;
    rcStrict = vmxHCEvaluatePendingEvent(pVCpu, &pVCpu->nem.s.VmcsInfo, &fIntrState);

    /*
     * Event injection may take locks (currently the PGM lock for real-on-v86 case) and thus
     * needs to be done with longjmps or interrupts + preemption enabled. Event injection might
     * also result in triple-faulting the VM.
     *
     * With nested-guests, the above does not apply since unrestricted guest execution is a
     * requirement. Regardless, we do this here to avoid duplicating code elsewhere.
     */
    rcStrict = vmxHCInjectPendingEvent(pVCpu, &pVCpu->nem.s.VmcsInfo, false /*fIsNestedGuest*/, fIntrState, fSingleStepping);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* likely */ }
    else
        return rcStrict;

    int rc = nemR3DarwinExportGuestState(pVM, pVCpu, pVmxTransient);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("Running vCPU\n"));
    pVCpu->nem.s.Event.fPending = false;
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
    VMXTRANSIENT VmxTransient;
    RT_ZERO(VmxTransient);
    VmxTransient.pVmcsInfo = &pVCpu->nem.s.VmcsInfo;

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
        rcStrict = nemR3DarwinPreRunGuest(pVM, pVCpu, &VmxTransient, false /* fSingleStepping */);
        if (rcStrict != VINF_SUCCESS)
            break;

        hv_return_t hrc = nemR3DarwinRunGuest(pVM, pVCpu, &VmxTransient);
        if (hrc == HV_SUCCESS)
        {
            /*
             * Deal with the message.
             */
            rcStrict = nemR3DarwinHandleExit(pVM, pVCpu, &VmxTransient);
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
            AssertLogRelMsgFailedReturn(("hv_vcpu_run()) failed for CPU #%u: %#x %u\n",
                                        pVCpu->idCpu, hrc, vmxHCCheckGuestState(pVCpu, &pVCpu->nem.s.VmcsInfo)),
                                        VERR_NEM_IPE_0);
        }
    } /* the run loop */

    return rcStrict;
}


/**
 * Checks if any expensive dtrace probes are enabled and we should go to the
 * debug loop.
 *
 * @returns true if we should use debug loop, false if not.
 */
static bool nemR3DarwinAnyExpensiveProbesEnabled(void)
{
    /** @todo Check performance penalty when checking these over and over */
    return (  VBOXVMM_R0_HMVMX_VMEXIT_ENABLED() /* expensive too due to context */
            | VBOXVMM_XCPT_DE_ENABLED()
            | VBOXVMM_XCPT_DB_ENABLED()
            | VBOXVMM_XCPT_BP_ENABLED()
            | VBOXVMM_XCPT_OF_ENABLED()
            | VBOXVMM_XCPT_BR_ENABLED()
            | VBOXVMM_XCPT_UD_ENABLED()
            | VBOXVMM_XCPT_NM_ENABLED()
            | VBOXVMM_XCPT_DF_ENABLED()
            | VBOXVMM_XCPT_TS_ENABLED()
            | VBOXVMM_XCPT_NP_ENABLED()
            | VBOXVMM_XCPT_SS_ENABLED()
            | VBOXVMM_XCPT_GP_ENABLED()
            | VBOXVMM_XCPT_PF_ENABLED()
            | VBOXVMM_XCPT_MF_ENABLED()
            | VBOXVMM_XCPT_AC_ENABLED()
            | VBOXVMM_XCPT_XF_ENABLED()
            | VBOXVMM_XCPT_VE_ENABLED()
            | VBOXVMM_XCPT_SX_ENABLED()
            | VBOXVMM_INT_SOFTWARE_ENABLED()
            /* not available in R3 | VBOXVMM_INT_HARDWARE_ENABLED()*/
           ) != 0
        || (  VBOXVMM_INSTR_HALT_ENABLED()
            | VBOXVMM_INSTR_MWAIT_ENABLED()
            | VBOXVMM_INSTR_MONITOR_ENABLED()
            | VBOXVMM_INSTR_CPUID_ENABLED()
            | VBOXVMM_INSTR_INVD_ENABLED()
            | VBOXVMM_INSTR_WBINVD_ENABLED()
            | VBOXVMM_INSTR_INVLPG_ENABLED()
            | VBOXVMM_INSTR_RDTSC_ENABLED()
            | VBOXVMM_INSTR_RDTSCP_ENABLED()
            | VBOXVMM_INSTR_RDPMC_ENABLED()
            | VBOXVMM_INSTR_RDMSR_ENABLED()
            | VBOXVMM_INSTR_WRMSR_ENABLED()
            | VBOXVMM_INSTR_CRX_READ_ENABLED()
            | VBOXVMM_INSTR_CRX_WRITE_ENABLED()
            | VBOXVMM_INSTR_DRX_READ_ENABLED()
            | VBOXVMM_INSTR_DRX_WRITE_ENABLED()
            | VBOXVMM_INSTR_PAUSE_ENABLED()
            | VBOXVMM_INSTR_XSETBV_ENABLED()
            | VBOXVMM_INSTR_SIDT_ENABLED()
            | VBOXVMM_INSTR_LIDT_ENABLED()
            | VBOXVMM_INSTR_SGDT_ENABLED()
            | VBOXVMM_INSTR_LGDT_ENABLED()
            | VBOXVMM_INSTR_SLDT_ENABLED()
            | VBOXVMM_INSTR_LLDT_ENABLED()
            | VBOXVMM_INSTR_STR_ENABLED()
            | VBOXVMM_INSTR_LTR_ENABLED()
            | VBOXVMM_INSTR_GETSEC_ENABLED()
            | VBOXVMM_INSTR_RSM_ENABLED()
            | VBOXVMM_INSTR_RDRAND_ENABLED()
            | VBOXVMM_INSTR_RDSEED_ENABLED()
            | VBOXVMM_INSTR_XSAVES_ENABLED()
            | VBOXVMM_INSTR_XRSTORS_ENABLED()
            | VBOXVMM_INSTR_VMM_CALL_ENABLED()
            | VBOXVMM_INSTR_VMX_VMCLEAR_ENABLED()
            | VBOXVMM_INSTR_VMX_VMLAUNCH_ENABLED()
            | VBOXVMM_INSTR_VMX_VMPTRLD_ENABLED()
            | VBOXVMM_INSTR_VMX_VMPTRST_ENABLED()
            | VBOXVMM_INSTR_VMX_VMREAD_ENABLED()
            | VBOXVMM_INSTR_VMX_VMRESUME_ENABLED()
            | VBOXVMM_INSTR_VMX_VMWRITE_ENABLED()
            | VBOXVMM_INSTR_VMX_VMXOFF_ENABLED()
            | VBOXVMM_INSTR_VMX_VMXON_ENABLED()
            | VBOXVMM_INSTR_VMX_VMFUNC_ENABLED()
            | VBOXVMM_INSTR_VMX_INVEPT_ENABLED()
            | VBOXVMM_INSTR_VMX_INVVPID_ENABLED()
            | VBOXVMM_INSTR_VMX_INVPCID_ENABLED()
           ) != 0
        || (  VBOXVMM_EXIT_TASK_SWITCH_ENABLED()
            | VBOXVMM_EXIT_HALT_ENABLED()
            | VBOXVMM_EXIT_MWAIT_ENABLED()
            | VBOXVMM_EXIT_MONITOR_ENABLED()
            | VBOXVMM_EXIT_CPUID_ENABLED()
            | VBOXVMM_EXIT_INVD_ENABLED()
            | VBOXVMM_EXIT_WBINVD_ENABLED()
            | VBOXVMM_EXIT_INVLPG_ENABLED()
            | VBOXVMM_EXIT_RDTSC_ENABLED()
            | VBOXVMM_EXIT_RDTSCP_ENABLED()
            | VBOXVMM_EXIT_RDPMC_ENABLED()
            | VBOXVMM_EXIT_RDMSR_ENABLED()
            | VBOXVMM_EXIT_WRMSR_ENABLED()
            | VBOXVMM_EXIT_CRX_READ_ENABLED()
            | VBOXVMM_EXIT_CRX_WRITE_ENABLED()
            | VBOXVMM_EXIT_DRX_READ_ENABLED()
            | VBOXVMM_EXIT_DRX_WRITE_ENABLED()
            | VBOXVMM_EXIT_PAUSE_ENABLED()
            | VBOXVMM_EXIT_XSETBV_ENABLED()
            | VBOXVMM_EXIT_SIDT_ENABLED()
            | VBOXVMM_EXIT_LIDT_ENABLED()
            | VBOXVMM_EXIT_SGDT_ENABLED()
            | VBOXVMM_EXIT_LGDT_ENABLED()
            | VBOXVMM_EXIT_SLDT_ENABLED()
            | VBOXVMM_EXIT_LLDT_ENABLED()
            | VBOXVMM_EXIT_STR_ENABLED()
            | VBOXVMM_EXIT_LTR_ENABLED()
            | VBOXVMM_EXIT_GETSEC_ENABLED()
            | VBOXVMM_EXIT_RSM_ENABLED()
            | VBOXVMM_EXIT_RDRAND_ENABLED()
            | VBOXVMM_EXIT_RDSEED_ENABLED()
            | VBOXVMM_EXIT_XSAVES_ENABLED()
            | VBOXVMM_EXIT_XRSTORS_ENABLED()
            | VBOXVMM_EXIT_VMM_CALL_ENABLED()
            | VBOXVMM_EXIT_VMX_VMCLEAR_ENABLED()
            | VBOXVMM_EXIT_VMX_VMLAUNCH_ENABLED()
            | VBOXVMM_EXIT_VMX_VMPTRLD_ENABLED()
            | VBOXVMM_EXIT_VMX_VMPTRST_ENABLED()
            | VBOXVMM_EXIT_VMX_VMREAD_ENABLED()
            | VBOXVMM_EXIT_VMX_VMRESUME_ENABLED()
            | VBOXVMM_EXIT_VMX_VMWRITE_ENABLED()
            | VBOXVMM_EXIT_VMX_VMXOFF_ENABLED()
            | VBOXVMM_EXIT_VMX_VMXON_ENABLED()
            | VBOXVMM_EXIT_VMX_VMFUNC_ENABLED()
            | VBOXVMM_EXIT_VMX_INVEPT_ENABLED()
            | VBOXVMM_EXIT_VMX_INVVPID_ENABLED()
            | VBOXVMM_EXIT_VMX_INVPCID_ENABLED()
            | VBOXVMM_EXIT_VMX_EPT_VIOLATION_ENABLED()
            | VBOXVMM_EXIT_VMX_EPT_MISCONFIG_ENABLED()
            | VBOXVMM_EXIT_VMX_VAPIC_ACCESS_ENABLED()
            | VBOXVMM_EXIT_VMX_VAPIC_WRITE_ENABLED()
           ) != 0;
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
    VMXTRANSIENT VmxTransient;
    RT_ZERO(VmxTransient);
    VmxTransient.pVmcsInfo = &pVCpu->nem.s.VmcsInfo;

    bool const fSavedSingleInstruction = pVCpu->nem.s.fSingleInstruction;
    pVCpu->nem.s.fSingleInstruction    = pVCpu->nem.s.fSingleInstruction || DBGFIsStepping(pVCpu);
    pVCpu->nem.s.fDebugWantRdTscExit   = false;
    pVCpu->nem.s.fUsingDebugLoop       = true;

    /* State we keep to help modify and later restore the VMCS fields we alter, and for detecting steps.  */
    VMXRUNDBGSTATE DbgState;
    vmxHCRunDebugStateInit(pVCpu, &VmxTransient, &DbgState);
    vmxHCPreRunGuestDebugStateUpdate(pVCpu, &VmxTransient, &DbgState);

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
        bool fStepping = pVCpu->nem.s.fSingleInstruction;

        /* Set up VM-execution controls the next two can respond to. */
        vmxHCPreRunGuestDebugStateApply(pVCpu, &VmxTransient, &DbgState);

        rcStrict = nemR3DarwinPreRunGuest(pVM, pVCpu, &VmxTransient, fStepping);
        if (rcStrict != VINF_SUCCESS)
            break;

        /* Override any obnoxious code in the above call. */
        vmxHCPreRunGuestDebugStateApply(pVCpu, &VmxTransient, &DbgState);

        hv_return_t hrc = nemR3DarwinRunGuest(pVM, pVCpu, &VmxTransient);
        if (hrc == HV_SUCCESS)
        {
            /*
             * Deal with the message.
             */
            rcStrict = nemR3DarwinHandleExitDebug(pVM, pVCpu, &VmxTransient, &DbgState);
            if (rcStrict == VINF_SUCCESS)
            { /* hopefully likely */ }
            else
            {
                LogFlow(("NEM/%u: breaking: nemR3DarwinHandleExitDebug -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                break;
            }

            /*
             * Stepping: Did the RIP change, if so, consider it a single step.
             * Otherwise, make sure one of the TFs gets set.
             */
            if (fStepping)
            {
                int rc = vmxHCImportGuestStateEx(pVCpu, VmxTransient.pVmcsInfo, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
                AssertRC(rc);
                if (   pVCpu->cpum.GstCtx.rip    != DbgState.uRipStart
                    || pVCpu->cpum.GstCtx.cs.Sel != DbgState.uCsStart)
                {
                    rcStrict = VINF_EM_DBG_STEPPED;
                    break;
                }
                ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_GUEST_DR7);
            }
        }
        else
        {
            AssertLogRelMsgFailedReturn(("hv_vcpu_run()) failed for CPU #%u: %#x %u\n",
                                        pVCpu->idCpu, hrc, vmxHCCheckGuestState(pVCpu, &pVCpu->nem.s.VmcsInfo)),
                                        VERR_NEM_IPE_0);
        }
    } /* the run loop */

    /*
     * Clear the X86_EFL_TF if necessary.
     */
    if (pVCpu->nem.s.fClearTrapFlag)
    {
        int rc = vmxHCImportGuestStateEx(pVCpu, VmxTransient.pVmcsInfo, CPUMCTX_EXTRN_RFLAGS);
        AssertRC(rc);
        pVCpu->nem.s.fClearTrapFlag = false;
        pVCpu->cpum.GstCtx.eflags.Bits.u1TF = 0;
    }

    pVCpu->nem.s.fUsingDebugLoop     = false;
    pVCpu->nem.s.fDebugWantRdTscExit = false;
    pVCpu->nem.s.fSingleInstruction  = fSavedSingleInstruction;

    /* Restore all controls applied by vmxHCPreRunGuestDebugStateApply above. */
    return vmxHCRunDebugStateRevert(pVCpu, &VmxTransient, &DbgState, rcStrict);
}


VBOXSTRICTRC nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu)
{
    LogFlow(("NEM/%u: %04x:%08RX64 efl=%#08RX64 <=\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u));
#ifdef LOG_ENABLED
    if (LogIs3Enabled())
        nemR3DarwinLogState(pVM, pVCpu);
#endif

    AssertReturn(NEMR3CanExecuteGuest(pVM, pVCpu), VERR_NEM_IPE_9);

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
        && !nemR3DarwinAnyExpensiveProbesEnabled()
        && !DBGFIsStepping(pVCpu)
        && !pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledInt3Breakpoints)
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
    if (pVCpu->nem.s.Event.fPending)
    {
        vmxHCPendingEventToTrpmTrap(pVCpu);
        Assert(!pVCpu->nem.s.Event.fPending);

        /* Clear the events from the VMCS. */
        int rc = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO, 0);    AssertRC(rc);
        rc     = nemR3DarwinWriteVmcs32(pVCpu, VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS, 0);         AssertRC(rc);
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
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_INTERRUPT_APIC
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
            {
                pVCpu->cpum.GstCtx.fExtrn = 0;
                ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
            }
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturn);
        }
        else
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }
    else
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
        pVCpu->cpum.GstCtx.fExtrn = 0;
        ASMAtomicUoOrU64(&pVCpu->nem.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
    }

    LogFlow(("NEM/%u: %04x:%08RX64 efl=%#08RX64 => %Rrc\n",
             pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    NOREF(pVM);
    return PGMPhysIsA20Enabled(pVCpu);
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

    hv_return_t hrc = hv_vcpu_interrupt(&pVCpu->nem.s.hVCpuId, 1);
    if (hrc != HV_SUCCESS)
        LogRel(("NEM: hv_vcpu_interrupt(%u, 1) failed with %#x\n", pVCpu->nem.s.hVCpuId, hrc));
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_VMX_FIRST;
         !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_VMX_LAST;
         enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
        fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    return fUseDebugLoop;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu);
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
    return false;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExMapEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                  void *pvRam, void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pVM, puNemRange, pvRam, fFlags);

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
        int rc = nemR3DarwinMap(pVM, GCPhys, pvMmio2, cb, NEM_PAGE_PROT_READ | NEM_PAGE_PROT_WRITE, pu2State);
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
            LogRel(("NEMR3NotifyPhysMmioExUnmap: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> rc=%Rrc\n",
                    GCPhys, cb, fFlags, rc));
            return VERR_NEM_UNMAP_PAGES_FAILED;
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
    RT_NOREF(pVM, GCPhys, cb, uNemRange, pvBitmap, cbBitmap);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages, uint32_t fFlags,
                                                     uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pvPages);

    Log5(("nemR3NativeNotifyPhysRomRegisterEarly: %RGp LB %RGp pvPages=%p fFlags=%#x pu2State=%p (%d) puNemRange=%p (%#x)\n",
          GCPhys, cb, pvPages, fFlags, pu2State, *pu2State, puNemRange, *puNemRange));
    if (fFlags & NEM_NOTIFY_PHYS_ROM_F_REPLACE)
    {
        int rc = nemR3DarwinUnmap(pVM, GCPhys, cb, pu2State);
        if (RT_FAILURE(rc))
        {
            LogRel(("NEMR3NotifyPhysRomRegisterLate: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> rc=%Rrc\n",
                    GCPhys, cb, fFlags, rc));
            return VERR_NEM_UNMAP_PAGES_FAILED;
        }
    }

    *puNemRange = 0;
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages,
                                                    uint32_t fFlags, uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("nemR3NativeNotifyPhysRomRegisterLate: %RGp LB %RGp pvPages=%p fFlags=%#x pu2State=%p (%d) puNemRange=%p (%#x)\n",
          GCPhys, cb, pvPages, fFlags, pu2State, *pu2State, puNemRange, *puNemRange));
    *pu2State = UINT8_MAX;
    RT_NOREF(pVM, GCPhys, cb, pvPages, fFlags, puNemRange);
    return VINF_SUCCESS;
}


VMM_INT_DECL(void) NEMHCNotifyHandlerPhysicalDeregister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb,
                                                        RTR3PTR pvMemR3, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyHandlerPhysicalDeregister: %RGp LB %RGp enmKind=%d pvMemR3=%p pu2State=%p (%d)\n",
          GCPhys, cb, enmKind, pvMemR3, pu2State, *pu2State));
    *pu2State = UINT8_MAX;
    RT_NOREF(pVM, enmKind, GCPhys, cb, pvMemR3);
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
    RT_NOREF(HCPhys, fPageProt, enmType);

    return nemR3DarwinUnmap(pVM, GCPhys, X86_PAGE_SIZE, pu2State);
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, RTR3PTR pvR3, uint32_t fPageProt,
                                                  PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageProtChanged: %RGp HCPhys=%RHp pvR3=%p fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, pvR3, fPageProt, enmType, *pu2State));
    RT_NOREF(HCPhys, pvR3, fPageProt, enmType)

    uint8_t u2StateOld = *pu2State;
    /* Can return early if this is an unmap request and the page is not mapped. */
    if (   fPageProt == NEM_PAGE_PROT_NONE
        && u2StateOld == NEM_DARWIN_PAGE_STATE_UNMAPPED)
    {
        Assert(!pvR3);
        return;
    }

    int rc;
    if (u2StateOld == NEM_DARWIN_PAGE_STATE_UNMAPPED)
    {
        AssertPtr(pvR3);
        rc = nemR3DarwinMap(pVM, GCPhys, pvR3, X86_PAGE_SIZE, fPageProt, pu2State);
    }
    else
        rc = nemR3DarwinProtect(pVM, GCPhys, X86_PAGE_SIZE, fPageProt, pu2State);
    AssertLogRelMsgRC(rc, ("NEMHCNotifyPhysPageProtChanged: nemR3DarwinMap/nemR3DarwinProtect(,%p,%RGp,%RGp,) u2StateOld=%u -> %Rrc\n",
                      pvR3, GCPhys, X86_PAGE_SIZE, u2StateOld, rc));
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev, RTHCPHYS HCPhysNew,
                                              RTR3PTR pvNewR3, uint32_t fPageProt, PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageChanged: %RGp HCPhys=%RHp->%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhysPrev, HCPhysNew, fPageProt, enmType, *pu2State));
    RT_NOREF(HCPhysPrev, HCPhysNew, pvNewR3, fPageProt, enmType);

    int rc = nemR3DarwinUnmap(pVM, GCPhys, X86_PAGE_SIZE, pu2State);
    if (RT_SUCCESS(rc))
    {
        rc = nemR3DarwinMap(pVM, GCPhys, pvNewR3, X86_PAGE_SIZE, fPageProt, pu2State);
        AssertLogRelMsgRC(rc, ("NEMHCNotifyPhysPageChanged: nemR3DarwinMap(,%p,%RGp,%RGp,) -> %Rrc\n",
                          pvNewR3, GCPhys, X86_PAGE_SIZE, rc));
    }
    else
        AssertReleaseFailed();
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

    int rc = nemR3DarwinMsrRead(pVCpu, MSR_IA32_TSC, pcTicks);
    if (   RT_SUCCESS(rc)
        && puAux)
    {
        if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_TSC_AUX)
        {
            uint64_t u64Aux;
            rc = nemR3DarwinMsrRead(pVCpu, MSR_K8_TSC_AUX, &u64Aux);
            if (RT_SUCCESS(rc))
                *puAux = (uint32_t)u64Aux;
        }
        else
            *puAux = CPUMGetGuestTscAux(pVCpu);
    }

    return rc;
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
    LogFlowFunc(("pVM=%p pVCpu=%p uPausedTscValue=%RX64\n", pVCpu, uPausedTscValue));
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(VM_IS_NEM_ENABLED(pVM), VERR_NEM_IPE_9);

    hv_return_t hrc = hv_vm_sync_tsc(uPausedTscValue);
    if (RT_LIKELY(hrc == HV_SUCCESS))
    {
        ASMAtomicUoAndU64(&pVCpu->nem.s.fCtxChanged, ~HM_CHANGED_GUEST_TSC_AUX);
        return VINF_SUCCESS;
    }

    return nemR3DarwinHvSts2Rc(hrc);
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

