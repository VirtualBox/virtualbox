/* $Id: GIMKvm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager, KVM implementation.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/gim.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/em.h>
#include "GIMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/disopcode-x86-amd64.h>
#include <VBox/err.h>
#include <VBox/version.h>

#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/**
 * GIM KVM saved-state version.
 */
#define GIM_KVM_SAVED_STATE_VERSION         UINT32_C(1)

/**
 * VBox internal struct. to passback to EMT rendezvous callback while enabling
 * the KVM wall-clock.
 */
typedef struct KVMWALLCLOCKINFO
{
    /** Guest physical address of the wall-clock struct.  */
    RTGCPHYS GCPhysWallClock;
} KVMWALLCLOCKINFO;
/** Pointer to the wall-clock info. struct. */
typedef KVMWALLCLOCKINFO *PKVMWALLCLOCKINFO;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_STATISTICS
# define GIMKVM_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Gim, kCpumMsrWrFn_Gim, 0, 0, 0, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define GIMKVM_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Gim, kCpumMsrWrFn_Gim, 0, 0, 0, 0, 0, a_szName }
#endif

/**
 * Array of MSR ranges supported by KVM.
 */
static CPUMMSRRANGE const g_aMsrRanges_Kvm[] =
{
    GIMKVM_MSRRANGE(MSR_GIM_KVM_RANGE0_FIRST, MSR_GIM_KVM_RANGE0_LAST, "KVM range 0"),
    GIMKVM_MSRRANGE(MSR_GIM_KVM_RANGE1_FIRST, MSR_GIM_KVM_RANGE1_LAST, "KVM range 1")
};
#undef GIMKVM_MSRRANGE


/**
 * Updates the KVM VCPU system-time structure in guest memory.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks This must be called after the system time MSR value has been updated.
 */
static int gimR3KvmUpdateSystemTime(PVM pVM, PVMCPU pVCpu)
{
    PGIMKVM    pKvm    = &pVM->gim.s.u.Kvm;
    PGIMKVMCPU pKvmCpu = &pVCpu->gim.s.u.KvmCpu;

    /*
     * Validate the MSR has the enable bit and the guest's system time struct. address.
     */
    MSR_GIM_KVM_SYSTEM_TIME_IS_ENABLED(pKvmCpu->u64SystemTimeMsr);
    if (!PGMPhysIsGCPhysNormal(pVM, pKvmCpu->GCPhysSystemTime))
    {
        LogRel(("GIM: KVM: VCPU%3d: Invalid physical addr requested for mapping system-time struct. GCPhysSystemTime=%#RGp\n",
               pVCpu->idCpu, pKvmCpu->GCPhysSystemTime));
        return VERR_GIM_OPERATION_FAILED;
    }

    VMSTATE const enmVMState = pVM->enmVMState;
    bool const fRunning = VMSTATE_IS_RUNNING(enmVMState);
    Assert(!(pKvmCpu->u32SystemTimeVersion & UINT32_C(1)));

    /*
     * Construct a system-time struct.
     */
    GIMKVMSYSTEMTIME SystemTime;
    RT_ZERO(SystemTime);
    SystemTime.u32Version  = pKvmCpu->u32SystemTimeVersion + !!fRunning;
    SystemTime.u64NanoTS   = pKvmCpu->uVirtNanoTS;
    SystemTime.u64Tsc      = pKvmCpu->uTsc;
    SystemTime.fFlags      = pKvmCpu->fSystemTimeFlags | GIM_KVM_SYSTEM_TIME_FLAGS_TSC_STABLE;

    /*
     * How the guest calculates the system time (nanoseconds):
     *
     * tsc = rdtsc - SysTime.u64Tsc
     * if (SysTime.i8TscShift >= 0)
     *     tsc <<= i8TscShift;
     * else
     *     tsc >>= -i8TscShift;
     * time = ((tsc * SysTime.u32TscScale) >> 32) + SysTime.u64NanoTS
     */
    uint64_t u64TscFreq   = pKvm->cTscTicksPerSecond;
    SystemTime.i8TscShift = 0;
    while (u64TscFreq > 2 * RT_NS_1SEC_64)
    {
        u64TscFreq >>= 1;
        SystemTime.i8TscShift--;
    }
    uint32_t uTscFreqLo = (uint32_t)u64TscFreq;
    while (uTscFreqLo <= RT_NS_1SEC)
    {
        uTscFreqLo <<= 1;
        SystemTime.i8TscShift++;
    }
    SystemTime.u32TscScale = ASMDivU64ByU32RetU32(RT_NS_1SEC_64 << 32, uTscFreqLo);

    /*
     * For informational purposes, back-calculate the exact TSC frequency the guest will see.
     * Note that the frequency is in kHz, not Hz, since that's what Linux uses.
     */
    uint64_t uTscKHz = (RT_NS_1MS_64 << 32) / SystemTime.u32TscScale;
    if (SystemTime.i8TscShift < 0)
        uTscKHz <<= -SystemTime.i8TscShift;
    else
        uTscKHz >>=  SystemTime.i8TscShift;

    /*
     * Update guest memory with the system-time struct.
     *
     * We update the struct with an incremented, odd version field to indicate to the guest
     * that the memory is being updated concurrently by the host and it should discard any
     * data from this struct when it reads an odd version.
     *
     * When the VM is not running, we don't need to do this two step update for obvious
     * reasons and so we skip it.
     */
    if (fRunning)
        Assert(SystemTime.u32Version & UINT32_C(1));
    else
        Assert(!(SystemTime.u32Version & UINT32_C(1)));

    int rc = PGMPhysSimpleWriteGCPhys(pVM, pKvmCpu->GCPhysSystemTime, &SystemTime, sizeof(GIMKVMSYSTEMTIME));
    if (RT_SUCCESS(rc))
    {
        LogRel(("GIM: KVM: VCPU%3d: Enabled system-time struct. at %#RGp - u32TscScale=%#RX32 i8TscShift=%d uVersion=%#RU32 "
                "fFlags=%#x uTsc=%#RX64 uVirtNanoTS=%#RX64 TscKHz=%RU64\n", pVCpu->idCpu, pKvmCpu->GCPhysSystemTime,
                SystemTime.u32TscScale, SystemTime.i8TscShift, SystemTime.u32Version + !!fRunning, SystemTime.fFlags,
                pKvmCpu->uTsc, pKvmCpu->uVirtNanoTS, uTscKHz));
        TMR3CpuTickParavirtEnable(pVM);
    }
    else
    {
        LogRel(("GIM: KVM: VCPU%3d: Failed to write system-time struct. at %#RGp. rc=%Rrc\n", pVCpu->idCpu,
                pKvmCpu->GCPhysSystemTime, rc));
    }

    if (fRunning)
    {
        ++SystemTime.u32Version;
        Assert(!(SystemTime.u32Version & UINT32_C(1)));
        rc = PGMPhysSimpleWriteGCPhys(pVM, pKvmCpu->GCPhysSystemTime + RT_UOFFSETOF(GIMKVMSYSTEMTIME, u32Version),
                                          &SystemTime.u32Version, sizeof(SystemTime.u32Version));
        if (RT_FAILURE(rc))
        {
            LogRel(("GIM: KVM: VCPU%3d: Failed to write system-time struct. while updating version field at %#RGp. rc=%Rrc\n",
                    pVCpu->idCpu, pKvmCpu->GCPhysSystemTime, rc));
            return rc;
        }

        /* Update the version so our next write will start with an even value. */
        pKvmCpu->u32SystemTimeVersion += 2;
    }

    return rc;
}


/**
 * Initializes the KVM GIM provider.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3KvmInit(PVM pVM)
{
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pVM->gim.s.enmProviderId == GIMPROVIDERID_KVM, VERR_INTERNAL_ERROR_5);

    int rc;
    PGIMKVM pKvm = &pVM->gim.s.u.Kvm;

    /*
     * Determine interface capabilities based on the version.
     */
    if (!pVM->gim.s.u32Version)
    {
        /* Basic features. */
        pKvm->uBaseFeat = 0
                        | GIM_KVM_BASE_FEAT_CLOCK_OLD
                        //| GIM_KVM_BASE_FEAT_NOP_IO_DELAY
                        //|  GIM_KVM_BASE_FEAT_MMU_OP
                        | GIM_KVM_BASE_FEAT_CLOCK
                        //| GIM_KVM_BASE_FEAT_ASYNC_PF
                        //| GIM_KVM_BASE_FEAT_STEAL_TIME
                        //| GIM_KVM_BASE_FEAT_PV_EOI
                        | GIM_KVM_BASE_FEAT_PV_UNHALT
                        ;
        /* Rest of the features are determined in gimR3KvmInitCompleted(). */
    }

    /*
     * Expose HVP (Hypervisor Present) bit to the guest.
     */
    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_HVP);

    /*
     * Modify the standard hypervisor leaves for KVM.
     */
    CPUMCPUIDLEAF HyperLeaf;
    RT_ZERO(HyperLeaf);
    HyperLeaf.uLeaf        = UINT32_C(0x40000000);
    HyperLeaf.uEax         = UINT32_C(0x40000001); /* Minimum value for KVM is 0x40000001. */
    HyperLeaf.uEbx         = 0x4B4D564B;           /* 'KVMK' */
    HyperLeaf.uEcx         = 0x564B4D56;           /* 'VMKV' */
    HyperLeaf.uEdx         = 0x0000004D;           /* 'M000' */
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Add KVM specific leaves.
     */
    HyperLeaf.uLeaf        = UINT32_C(0x40000001);
    HyperLeaf.uEax         = pKvm->uBaseFeat;
    HyperLeaf.uEbx         = 0;                    /* Reserved */
    HyperLeaf.uEcx         = 0;                    /* Reserved */
    HyperLeaf.uEdx         = 0;                    /* Reserved */
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Insert all MSR ranges of KVM.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aMsrRanges_Kvm); i++)
    {
        rc = CPUMR3MsrRangesInsert(pVM, &g_aMsrRanges_Kvm[i]);
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * Setup hypercall and #UD handling.
     * Note! We always need to trap VMCALL/VMMCALL hypercall using #UDs for raw-mode VMs.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        EMSetHypercallInstructionsEnabled(pVM->apCpusR3[idCpu], true);

    size_t cbHypercall = 0;
    rc = GIMQueryHypercallOpcodeBytes(pVM, pKvm->abOpcodeNative, sizeof(pKvm->abOpcodeNative), &cbHypercall, &pKvm->uOpcodeNative);
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelReturn(cbHypercall == sizeof(pKvm->abOpcodeNative), VERR_GIM_IPE_1);
    pKvm->fTrapXcptUD = pKvm->uOpcodeNative != OP_VMCALL;

    return VINF_SUCCESS;
}


/**
 * Initializes remaining bits of the KVM provider.
 *
 * This is called after initializing HM and almost all other VMM components.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3KvmInitCompleted(PVM pVM)
{
    PGIMKVM pKvm = &pVM->gim.s.u.Kvm;
    pKvm->cTscTicksPerSecond = TMCpuTicksPerSecond(pVM);

    if (TMR3CpuTickIsFixedRateMonotonic(pVM, true /* fWithParavirtEnabled */))
    {
        /** @todo We might want to consider just enabling this bit *always*. As far
         *        as I can see in the Linux guest, the "TSC_STABLE" bit is only
         *        translated as a "monotonic" bit which even in Async systems we
         *        -should- be reporting a strictly monotonic TSC to the guest.  */
        pKvm->uBaseFeat |= GIM_KVM_BASE_FEAT_TSC_STABLE;

        CPUMCPUIDLEAF HyperLeaf;
        RT_ZERO(HyperLeaf);
        HyperLeaf.uLeaf        = UINT32_C(0x40000001);
        HyperLeaf.uEax         = pKvm->uBaseFeat;
        HyperLeaf.uEbx         = 0;
        HyperLeaf.uEcx         = 0;
        HyperLeaf.uEdx         = 0;
        int rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
        AssertLogRelRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}


/**
 * Terminates the KVM GIM provider.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3KvmTerm(PVM pVM)
{
    gimR3KvmReset(pVM);
    return VINF_SUCCESS;
}


/**
 * This resets KVM provider MSRs and unmaps whatever KVM regions that
 * the guest may have mapped.
 *
 * This is called when the VM is being reset.
 *
 * @param   pVM     The cross context VM structure.
 * @thread EMT(0)
 */
VMMR3_INT_DECL(void) gimR3KvmReset(PVM pVM)
{
    VM_ASSERT_EMT0(pVM);
    LogRel(("GIM: KVM: Resetting MSRs\n"));

    /*
     * Reset MSRs.
     */
    PGIMKVM pKvm = &pVM->gim.s.u.Kvm;
    pKvm->u64WallClockMsr = 0;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PGIMKVMCPU pKvmCpu = &pVM->apCpusR3[idCpu]->gim.s.u.KvmCpu;
        pKvmCpu->u64SystemTimeMsr = 0;
        pKvmCpu->u32SystemTimeVersion = 0;
        pKvmCpu->fSystemTimeFlags = 0;
        pKvmCpu->GCPhysSystemTime = 0;
        pKvmCpu->uTsc = 0;
        pKvmCpu->uVirtNanoTS = 0;
    }
}


/**
 * KVM state-save operation.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The saved state handle.
 */
VMMR3_INT_DECL(int) gimR3KvmSave(PVM pVM, PSSMHANDLE pSSM)
{
    PCGIMKVM pKvm = &pVM->gim.s.u.Kvm;

    /*
     * Save the KVM SSM version.
     */
    SSMR3PutU32(pSSM, GIM_KVM_SAVED_STATE_VERSION);

    /*
     * Save per-VCPU data.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PCGIMKVMCPU pKvmCpu = &pVM->apCpusR3[idCpu]->gim.s.u.KvmCpu;
        SSMR3PutU64(pSSM, pKvmCpu->u64SystemTimeMsr);
        SSMR3PutU64(pSSM, pKvmCpu->uTsc);
        SSMR3PutU64(pSSM, pKvmCpu->uVirtNanoTS);
        SSMR3PutGCPhys(pSSM, pKvmCpu->GCPhysSystemTime);
        SSMR3PutU32(pSSM, pKvmCpu->u32SystemTimeVersion);
        SSMR3PutU8(pSSM, pKvmCpu->fSystemTimeFlags);
    }

    /*
     * Save per-VM data.
     */
    SSMR3PutU64(pSSM, pKvm->u64WallClockMsr);
    return SSMR3PutU32(pSSM, pKvm->uBaseFeat);
}


/**
 * KVM state-load operation, final pass.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The saved state handle.
 */
VMMR3_INT_DECL(int) gimR3KvmLoad(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Load the KVM SSM version first.
     */
    uint32_t uKvmSavedStatVersion;
    int rc = SSMR3GetU32(pSSM, &uKvmSavedStatVersion);
    AssertRCReturn(rc, rc);
    if (uKvmSavedStatVersion != GIM_KVM_SAVED_STATE_VERSION)
        return SSMR3SetLoadError(pSSM, VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION, RT_SRC_POS,
                                 N_("Unsupported KVM saved-state version %u (expected %u)."),
                                 uKvmSavedStatVersion, GIM_KVM_SAVED_STATE_VERSION);

    /*
     * Update the TSC frequency from TM.
     */
    PGIMKVM pKvm = &pVM->gim.s.u.Kvm;
    pKvm->cTscTicksPerSecond = TMCpuTicksPerSecond(pVM);

    /*
     * Load per-VCPU data.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU     pVCpu   = pVM->apCpusR3[idCpu];
        PGIMKVMCPU pKvmCpu = &pVCpu->gim.s.u.KvmCpu;

        SSMR3GetU64(pSSM, &pKvmCpu->u64SystemTimeMsr);
        SSMR3GetU64(pSSM, &pKvmCpu->uTsc);
        SSMR3GetU64(pSSM, &pKvmCpu->uVirtNanoTS);
        SSMR3GetGCPhys(pSSM, &pKvmCpu->GCPhysSystemTime);
        SSMR3GetU32(pSSM, &pKvmCpu->u32SystemTimeVersion);
        rc = SSMR3GetU8(pSSM, &pKvmCpu->fSystemTimeFlags);
        AssertRCReturn(rc, rc);

        /* Enable the system-time struct. if necessary. */
        /** @todo update guest struct only if cTscTicksPerSecond doesn't match host
         *        anymore. */
        if (MSR_GIM_KVM_SYSTEM_TIME_IS_ENABLED(pKvmCpu->u64SystemTimeMsr))
        {
            Assert(!TMVirtualIsTicking(pVM));       /* paranoia. */
            Assert(!TMCpuTickIsTicking(pVCpu));
            gimR3KvmUpdateSystemTime(pVM, pVCpu);
        }
    }

    /*
     * Load per-VM data.
     */
    SSMR3GetU64(pSSM, &pKvm->u64WallClockMsr);
    rc = SSMR3GetU32(pSSM, &pKvm->uBaseFeat);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Disables the KVM system-time struct.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3KvmDisableSystemTime(PVM pVM)
{
    TMR3CpuTickParavirtDisable(pVM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PFNVMMEMTRENDEZVOUS,
 *      Worker for gimR3KvmEnableWallClock}
 */
static DECLCALLBACK(VBOXSTRICTRC) gimR3KvmEnableWallClockCallback(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    PKVMWALLCLOCKINFO pWallClockInfo  = (PKVMWALLCLOCKINFO)pvUser; AssertPtr(pWallClockInfo);
    RTGCPHYS          GCPhysWallClock = pWallClockInfo->GCPhysWallClock;
    RT_NOREF1(pVCpu);

    /*
     * Read the wall-clock version (sequence) from the guest.
     */
    uint32_t uVersion;
    Assert(PGMPhysIsGCPhysNormal(pVM, GCPhysWallClock));
    int rc = PGMPhysSimpleReadGCPhys(pVM, &uVersion, GCPhysWallClock, sizeof(uVersion));
    if (RT_FAILURE(rc))
    {
        LogRel(("GIM: KVM: Failed to read wall-clock struct. version at %#RGp. rc=%Rrc\n", GCPhysWallClock, rc));
        return rc;
    }

    /*
     * Ensure the version is incrementally even.
     */
    /* faster: uVersion = (uVersion | 1) + 1; */
    if (!(uVersion & 1))
        ++uVersion;
    ++uVersion;

    /*
     * Update wall-clock guest struct. with UTC information.
     */
    RTTIMESPEC TimeSpec;
    int32_t    iSec;
    int32_t    iNano;
    TMR3UtcNow(pVM, &TimeSpec);
    RTTimeSpecGetSecondsAndNano(&TimeSpec, &iSec, &iNano);

    GIMKVMWALLCLOCK WallClock;
    RT_ZERO(WallClock);
    AssertCompile(sizeof(uVersion) == sizeof(WallClock.u32Version));
    WallClock.u32Version = uVersion;
    WallClock.u32Sec     = iSec;
    WallClock.u32Nano    = iNano;

    /*
     * Write out the wall-clock struct. to guest memory.
     */
    Assert(!(WallClock.u32Version & 1));
    rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysWallClock, &WallClock, sizeof(GIMKVMWALLCLOCK));
    if (RT_SUCCESS(rc))
        LogRel(("GIM: KVM: Enabled wall-clock struct. at %#RGp - u32Sec=%u u32Nano=%u uVersion=%#RU32\n", GCPhysWallClock,
                WallClock.u32Sec, WallClock.u32Nano, WallClock.u32Version));
    else
        LogRel(("GIM: KVM: Failed to write wall-clock struct. at %#RGp. rc=%Rrc\n", GCPhysWallClock, rc));
    return rc;
}


/**
 * Enables the KVM wall-clock structure.
 *
 * Since the wall-clock can be read by any VCPU but it is a global struct. in
 * guest-memory, we do an EMT rendezvous here to be on the safe side. The
 * alternative is to use an MMIO2 region and use the WallClock.u32Version field
 * for transactional update. However, this MSR is rarely written to (typically
 * once during bootup) it's currently not a performance issue especially since
 * we're already in ring-3. If we really wanted better performance in this code
 * path, we should be doing it in ring-0 with transactional update while make
 * sure there is only 1 writer as well.
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 * @param   GCPhysWallClock    Where the guest wall-clock structure is located.
 *
 * @remarks Don't do any release assertions here, these can be triggered by
 *          guest R0 code.
 */
VMMR3_INT_DECL(int) gimR3KvmEnableWallClock(PVM pVM, RTGCPHYS GCPhysWallClock)
{
    KVMWALLCLOCKINFO WallClockInfo;
    WallClockInfo.GCPhysWallClock = GCPhysWallClock;
    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE, gimR3KvmEnableWallClockCallback, &WallClockInfo);
}


/**
 * Enables the KVM system time structure.
 *
 * This can be done concurrently because the guest memory being updated is per-VCPU
 * and the struct even has a "version" field which needs to be incremented
 * before/after altering guest memory to allow concurrent updates from the host.
 * Hence this is not being done in an EMT rendezvous. It -is- done in ring-3 since
 * we call into ring-3 only TM code in the end.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uMsrSystemTime  The system time MSR value being written.
 */
VMMR3_INT_DECL(int) gimR3KvmEnableSystemTime(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uMsrSystemTime)
{
    Assert(uMsrSystemTime & MSR_GIM_KVM_SYSTEM_TIME_ENABLE_BIT);
    PGIMKVM    pKvm    = &pVM->gim.s.u.Kvm;
    PGIMKVMCPU pKvmCpu = &pVCpu->gim.s.u.KvmCpu;

    /*
     * Update the system-time struct.
     * The system-time structs are usually placed at a different guest address for each VCPU.
     */
    pKvmCpu->uTsc                  = TMCpuTickGetNoCheck(pVCpu);
    pKvmCpu->uVirtNanoTS           = ASMMultU64ByU32DivByU32(pKvmCpu->uTsc, RT_NS_1SEC, pKvm->cTscTicksPerSecond);
    pKvmCpu->u64SystemTimeMsr      = uMsrSystemTime;
    pKvmCpu->GCPhysSystemTime      = MSR_GIM_KVM_SYSTEM_TIME_GUEST_GPA(uMsrSystemTime);

    int rc = gimR3KvmUpdateSystemTime(pVM, pVCpu);
    if (RT_FAILURE(rc))
    {
        pKvmCpu->u64SystemTimeMsr = 0;
        /* We shouldn't throw a #GP(0) here for buggy guests (neither does KVM apparently), see @bugref{8627}. */
    }

    return rc;
}

