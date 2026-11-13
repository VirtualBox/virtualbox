/* $Id: NEMR3Native-linux-armv8.cpp 172407 2026-01-26 11:03:45Z aeichner $ */
/** @file
 * NEM - Native execution manager, native ring-3 Linux backend riscv version.
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
#define LOG_GROUP LOG_GROUP_NEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/trpm.h>
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/riscv.h>

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <linux/kvm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Core register group.
 * Shorthand for: KVM_REG_RISCV | KVM_REG_SIZE_U64  | KVM_REG_RISCV_CORE */
#define KVM_RISCV64_REG_CORE_GROUP  UINT64_C(0x8030000002000000)

#define KVM_RISCV64_REG_CORE_CREATE(a_idReg)   (KVM_RISCV64_REG_CORE_GROUP | ((uint64_t)(a_idReg) & 0xffff))
#define KVM_RISCV64_REG_GPR(a_iGpr)            KVM_RISCV64_REG_CORE_CREATE((a_iGpr))
#define KVM_RISCV64_REG_PC                     KVM_RISCV64_REG_CORE_CREATE(0)
#define KVM_RISCV64_REG_RA                     KVM_RISCV64_REG_GPR(1)
#define KVM_RISCV64_REG_SP                     KVM_RISCV64_REG_GPR(2)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The general registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    fCpumExtrn;
    uint32_t    offCpumCtx;
} s_aCpumRegs[] =
{
#define CPUM_GREG_EMIT_X3_X31(a_Idx) { KVM_RISCV64_REG_GPR(a_Idx), CPUMCTX_EXTRN_X3_X31,     RT_UOFFSETOF(CPUMCTX, aGRegs[a_Idx].x) }
    { KVM_RISCV64_REG_RA,      CPUMCTX_EXTRN_RA,   RT_UOFFSETOF(CPUMCTX, aGRegs[1].x)       },
    { KVM_RISCV64_REG_SP,      CPUMCTX_EXTRN_SP,   RT_UOFFSETOF(CPUMCTX, aGRegs[2].x)       },
    CPUM_GREG_EMIT_X3_X31(2),
    CPUM_GREG_EMIT_X3_X31(3),
    CPUM_GREG_EMIT_X3_X31(4),
    CPUM_GREG_EMIT_X3_X31(5),
    CPUM_GREG_EMIT_X3_X31(6),
    CPUM_GREG_EMIT_X3_X31(7),
    CPUM_GREG_EMIT_X3_X31(8),
    CPUM_GREG_EMIT_X3_X31(9),
    CPUM_GREG_EMIT_X3_X31(10),
    CPUM_GREG_EMIT_X3_X31(11),
    CPUM_GREG_EMIT_X3_X31(12),
    CPUM_GREG_EMIT_X3_X31(13),
    CPUM_GREG_EMIT_X3_X31(14),
    CPUM_GREG_EMIT_X3_X31(15),
    CPUM_GREG_EMIT_X3_X31(16),
    CPUM_GREG_EMIT_X3_X31(17),
    CPUM_GREG_EMIT_X3_X31(18),
    CPUM_GREG_EMIT_X3_X31(19),
    CPUM_GREG_EMIT_X3_X31(20),
    CPUM_GREG_EMIT_X3_X31(21),
    CPUM_GREG_EMIT_X3_X31(22),
    CPUM_GREG_EMIT_X3_X31(23),
    CPUM_GREG_EMIT_X3_X31(24),
    CPUM_GREG_EMIT_X3_X31(25),
    CPUM_GREG_EMIT_X3_X31(26),
    CPUM_GREG_EMIT_X3_X31(27),
    CPUM_GREG_EMIT_X3_X31(28),
    CPUM_GREG_EMIT_X3_X31(29),
    CPUM_GREG_EMIT_X3_X31(30),
    CPUM_GREG_EMIT_X3_X31(31),
    { KVM_RISCV64_REG_PC,      CPUMCTX_EXTRN_PC,   RT_UOFFSETOF(CPUMCTX, Pc.u64)       },
#undef CPUM_GREG_EMIT_X3_X31
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/* Forward declarations of things called by the template. */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo);


/* Instantiate the common bits we share with the x86 KVM backend. */
#include "../NEMR3NativeTemplate-linux.cpp.h"


/**
 * Queries and logs the supported register list from KVM.
 *
 * @returns VBox status code.
 * @param   fdVCpu              The file descriptor number of vCPU 0.
 */
static int nemR3LnxLogRegList(int fdVCpu)
{
    struct KVM_REG_LIST
    {
        uint64_t cRegs;
        uint64_t aRegs[1024];
    } RegList; RT_ZERO(RegList);

    RegList.cRegs = RT_ELEMENTS(RegList.aRegs);
    int rcLnx = ioctl(fdVCpu, KVM_GET_REG_LIST, &RegList);
    if (rcLnx != 0)
        return RTErrConvertFromErrno(errno);

    LogRel(("NEM: KVM vCPU registers:\n"));

    for (uint64_t i = 0; i < RegList.cRegs; i++)
        LogRel(("NEM:   %36s: %#RX64\n", "Unknown" /** @todo */, RegList.aRegs[i]));

    return VINF_SUCCESS;
}


#if 0
/**
 * Sets the given attribute in KVM to the given value.
 *
 * @returns VBox status code.
 * @param   pVM             The VM instance.
 * @param   u32Grp          The device attribute group being set.
 * @param   u32Attr         The actual attribute inside the group being set.
 * @param   pvAttrVal       Where the attribute value to set.
 * @param   pszAttribute    Attribute description for logging.
 * @param   pErrInfo        Optional error information.
 */
static int nemR3LnxSetAttribute(PVM pVM, uint32_t u32Grp, uint32_t u32Attr, const void *pvAttrVal, const char *pszAttribute,
                                PRTERRINFO pErrInfo)
{
    struct kvm_device_attr DevAttr;

    DevAttr.flags = 0;
    DevAttr.group = u32Grp;
    DevAttr.attr  = u32Attr;
    DevAttr.addr  = (uintptr_t)pvAttrVal;
    int rcLnx = ioctl(pVM->nem.s.fdVm, KVM_HAS_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return RTErrInfoSetF(pErrInfo, RTErrConvertFromErrno(errno),
                             N_("KVM error: KVM doesn't support setting the attribute \"%s\" (%d)"),
                             pszAttribute, errno);

    rcLnx = ioctl(pVM->nem.s.fdVm, KVM_SET_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return RTErrInfoSetF(pErrInfo, RTErrConvertFromErrno(errno),
                             N_("KVM error: Setting the attribute \"%s\" for KVM failed (%d)"),
                             pszAttribute, errno);

    return VINF_SUCCESS;
}
#endif

#ifdef LOG_ENABLED

/** Logging: Returns the KVM VCPU state. */
DECLINLINE(uint32_t) nemR3LnxKvmGetMpState4Log(PVMCPUCC pVCpu)
{
    struct kvm_mp_state MpState = { UINT32_MAX };
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_MP_STATE, &MpState);
    AssertMsgReturn(rcLnx == 0, ("errno=%u rcLnx=%u\n", errno, rcLnx), UINT32_MAX);
    return MpState.mp_state;
}


/** Logging: Returns the value of 64-bit register. */
DECLINLINE(uint64_t) nemR3LnxKvmGetReg4LogU64(PVMCPUCC pVCpu, uint64_t idKvmReg)
{
    uint64_t           uValue = UINT64_MAX;
    struct kvm_one_reg Reg    = { idKvmReg, (uintptr_t)&uValue };
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_ONE_REG, &Reg);
    AssertMsgReturn(rcLnx == 0, ("errno=%u rcLnx=%u idKvmReg=%#RX64\n", errno, rcLnx, idKvmReg), UINT64_MAX);
    return uValue;
}

#endif /* LOG_ENABLED */

DECL_FORCE_INLINE(int) nemR3LnxKvmSetQueryReg(PVMCPUCC pVCpu, bool fQuery, uint64_t idKvmReg, const void *pv)
{
    struct kvm_one_reg Reg;
    Reg.id   = idKvmReg;
    Reg.addr = (uintptr_t)pv;

    /*
     * Who thought that this API was a good idea? Supporting to query/set just one register
     * at a time is horribly inefficient.
     */
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, fQuery ? KVM_GET_ONE_REG : KVM_SET_ONE_REG, &Reg);
    if (RT_LIKELY(!rcLnx))
        return VINF_SUCCESS;

    return RTErrConvertFromErrno(-rcLnx);
}

DECLINLINE(int) nemR3LnxKvmQueryRegU64(PVMCPUCC pVCpu, uint64_t idKvmReg, uint64_t *pu64)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pu64);
}


DECLINLINE(int) nemR3LnxKvmQueryRegU32(PVMCPUCC pVCpu, uint64_t idKvmReg, uint32_t *pu32)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pu32);
}


DECLINLINE(int) nemR3LnxKvmQueryRegPV(PVMCPUCC pVCpu, uint64_t idKvmReg, void *pv)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pv);
}


DECLINLINE(int) nemR3LnxKvmSetRegU64(PVMCPUCC pVCpu, uint64_t idKvmReg, const uint64_t *pu64)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pu64);
}


DECLINLINE(int) nemR3LnxKvmSetRegU32(PVMCPUCC pVCpu, uint64_t idKvmReg, const uint32_t *pu32)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pu32);
}


DECLINLINE(int) nemR3LnxKvmSetRegPV(PVMCPUCC pVCpu, uint64_t idKvmReg, const void *pv)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pv);
}


/**
 * Does the early setup of a KVM VM.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo)
{
    AssertReturn(pVM->nem.s.fdVm != -1, RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER, "Wrong initalization order"));

    /*
     * Create the VCpus.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /* Create it. */
        pVCpu->nem.s.fdVCpu = ioctl(pVM->nem.s.fdVm, KVM_CREATE_VCPU, (unsigned long)idCpu);
        if (pVCpu->nem.s.fdVCpu < 0)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "KVM_CREATE_VCPU failed for VCpu #%u: %d", idCpu, errno);

        /* Map the KVM_RUN area. */
        pVCpu->nem.s.pRun = (struct kvm_run *)mmap(NULL, pVM->nem.s.cbVCpuMmap, PROT_READ | PROT_WRITE, MAP_SHARED,
                                                   pVCpu->nem.s.fdVCpu, 0 /*offset*/);
        if ((void *)pVCpu->nem.s.pRun == MAP_FAILED)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "mmap failed for VCpu #%u: %d", idCpu, errno);

        if (idCpu == 0)
        {
            /* Query the supported register list and log it. */
            int rc = nemR3LnxLogRegList(pVCpu->nem.s.fdVCpu);
            if (RT_FAILURE(rc))
                return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "Querying the supported register list failed with %Rrc", rc);
        }
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(int) nemR3NativeInitCompletedRing3(PVM pVM)
{
    /*
     * Make RTThreadPoke work again (disabled for avoiding unnecessary
     * critical section issues in ring-0).
     */
    VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, nemR3LnxFixThreadPoke, NULL);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   CPU State                                                                                                                    *
*********************************************************************************************************************************/

#ifdef LOG_ENABLED
/**
 * Logs the current CPU state.
 */
static void nemR3LnxLogState(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (LogIs3Enabled())
    {
        char szRegs[4096];
        DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                        "              x1=%016VR{x1} x2=%016VR{x2} x3=%016VR{x3}\n"
                        "x4=%016VR{x4} x5=%016VR{x5} x6=%016VR{x6} x7=%016VR{x7}\n"
                        "x8=%016VR{x8} x9=%016VR{x9} x10=%016VR{x10} x11=%016VR{x11}\n"
                        "x12=%016VR{x12} x13=%016VR{x13} x14=%016VR{x14} x15=%016VR{x15}\n"
                        "x16=%016VR{x16} x17=%016VR{x17} x18=%016VR{x18} x19=%016VR{x19}\n"
                        "x20=%016VR{x20} x21=%016VR{x21} x22=%016VR{x22} x23=%016VR{x23}\n"
                        "x24=%016VR{x24} x25=%016VR{x25} x26=%016VR{x26} x27=%016VR{x27}\n"
                        "x28=%016VR{x28} x29=%016VR{x29} x30=%016VR{x30} x31=%016VR{x31}\n"
                        "pc=%016VR{pc}\n"
                        );
        char szInstr[256]; RT_ZERO(szInstr);
        DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, 0, 0,
                           DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                           szInstr, sizeof(szInstr), NULL);
        Log3(("%s%s\n", szRegs, szInstr));
    }
}
#endif /* LOG_ENABLED */


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
DECLINLINE(void) nemR3LnxSetGReg(PVMCPU pVCpu, uint8_t uReg, bool f64BitReg, bool fSignExtend, uint64_t u64Val)
{
    AssertReturnVoid(uReg <= RISCV_REG_X31);

    if (uReg == RISCV_REG_ZERO)
        return;

    if (f64BitReg)
        pVCpu->cpum.GstCtx.aGRegs[uReg].x = fSignExtend ? (int64_t)u64Val : u64Val;
    else
        pVCpu->cpum.GstCtx.aGRegs[uReg].x = (uint64_t)(fSignExtend ? (int32_t)u64Val : (uint32_t)u64Val);

    /* Mark the register as not extern anymore. */
    switch (uReg)
    {
        default:
            AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_GPRS_MASK));
            /** @todo We need to import all missing registers in order to clear this flag (or just set it in KVM from here). */
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
DECLINLINE(uint64_t) nemR3LnxGetGReg(PVMCPU pVCpu, uint8_t uReg)
{
    AssertReturn(uReg <= RISCV_REG_X31, 0);

    if (uReg == RISCV_REG_ZERO)
        return 0;

    /** @todo Import the register if extern. */
    AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_GPRS_MASK));

    return pVCpu->cpum.GstCtx.aGRegs[uReg].x;
}

/**
 * Worker that imports selected state from KVM.
 */
static int nemHCLnxImportState(PVMCPUCC pVCpu, uint64_t fWhat, PCPUMCTX pCtx)
{
    fWhat &= pVCpu->cpum.GstCtx.fExtrn;
    if (!fWhat)
        return VINF_SUCCESS;

    /** @todo Fix bogus rc handling (overwritten/ignored). Optimize. */
    int rc = VINF_SUCCESS;
    if (fWhat & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (s_aCpumRegs[i].fCpumExtrn & fWhat)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmQueryRegU64(pVCpu, s_aCpumRegs[i].idKvmReg, pu64);
            }
        }
    }

    /*
     * Update the external mask.
     */
    pCtx->fExtrn &= ~fWhat;
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
        pVCpu->cpum.GstCtx.fExtrn = 0;

    return VINF_SUCCESS;
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
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnDemand);
    return nemHCLnxImportState(pVCpu, fWhat, &pVCpu->cpum.GstCtx);
}


/**
 * Exports state to KVM.
 */
static int nemHCLnxExportState(PVM pVM, PVMCPU pVCpu, PCPUMCTX pCtx)
{
    uint64_t const fExtrn = ~pCtx->fExtrn & CPUMCTX_EXTRN_ALL;
    Assert((~fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL); RT_NOREF(fExtrn);
    int rc = VINF_SUCCESS;

    RT_NOREF(pVM);

    /** @todo optimize all of this! */
    if (   (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC))
        !=                              (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_PC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (!(s_aCpumRegs[i].fCpumExtrn & pVCpu->cpum.GstCtx.fExtrn))
            {
                const uint64_t *pu64 = (const uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmSetRegU64(pVCpu, s_aCpumRegs[i].idKvmReg, pu64);
            }
        }
    }

    /*
     * KVM now owns all the state.
     */
    pCtx->fExtrn = CPUMCTX_EXTRN_KEEPER_NEM | CPUMCTX_EXTRN_ALL;
    RT_NOREF(pVCpu, pCtx);
    return rc;
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
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatQueryCpuTick);
    // KVM_GET_CLOCK?
    RT_NOREF(pVCpu, pcTicks, puAux);
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
    // KVM_SET_CLOCK?
    RT_NOREF(pVM, pVCpu, uPausedTscValue);
    return VINF_SUCCESS;
}


VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
    RT_NOREF(pVM);
    return NEM_FEAT_F_NESTED_PAGING
         | NEM_FEAT_F_FULL_GST_EXEC;
}



/*********************************************************************************************************************************
*   Execution                                                                                                                    *
*********************************************************************************************************************************/


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM, pVCpu);
    Assert(VM_IS_NEM_ENABLED(pVM));
    return true;
}


VMMR3_INT_DECL(int) NEMR3Halt(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Force the vCPU to get out of the SIPI state and into the normal runloop
     * as SBI including IPI is handled inside KVM.
     */
    RT_NOREF(pVM);
    EMSetState(pVCpu, EMSTATE_HALTED);
    return VINF_EM_RESCHEDULE;
}


DECLHIDDEN(bool) nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
}


DECLHIDDEN(bool) nemR3NativeNeedSpecialWaitMethod(PVM pVM)
{
    RT_NOREF(pVM);
    /** SBI including IPI is handled inside KVM. */
    return true;
}


DECLHIDDEN(void) nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    int rc = RTThreadPoke(pVCpu->hThread);
    LogFlow(("nemR3NativeNotifyFF: #%u -> %Rrc\n", pVCpu->idCpu, rc));
    AssertRC(rc);
    RT_NOREF(pVM, fFlags);
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    RT_NOREF(pVM, fUseDebugLoop);
    return false;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu, fUseDebugLoop);
    return false;
}


DECL_FORCE_INLINE(int) nemR3LnxKvmUpdateIntrState(PVMCPU pVCpu, bool fAsserted)
{
    LogFlowFunc(("pVCpu=%p fAsserted=%RTbool\n", pVCpu, fAsserted));

    struct kvm_interrupt Irq;
    Irq.irq = fAsserted ? KVM_INTERRUPT_SET : KVM_INTERRUPT_UNSET;
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_INTERRUPT, &Irq);
    AssertReturn(rcLnx == 0, VERR_NEM_IPE_9);

    return VINF_SUCCESS;
}


/**
 * Deals with pending interrupt FFs prior to executing guest code.
 */
static VBOXSTRICTRC nemHCLnxHandleInterruptFF(PVMCPU pVCpu)
{
    bool const fIrq = VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);

    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool\n", pVCpu, pVCpu->idCpu, fIrq));

    /* Update the pending interrupt state. */
    if (fIrq != pVCpu->nem.s.fIrqLastSeen)
    {
        int rc = nemR3LnxKvmUpdateIntrState(pVCpu, fIrq);
        AssertRCReturn(rc, VERR_NEM_IPE_9);
        pVCpu->nem.s.fIrqLastSeen = fIrq;
    }

    return VINF_SUCCESS;
}


#if 0
/**
 * Handles KVM_EXIT_INTERNAL_ERROR.
 */
static VBOXSTRICTRC nemR3LnxHandleInternalError(PVMCPU pVCpu, struct kvm_run *pRun)
{
    Log(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%d) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror,
         pRun->internal.suberror, pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));

    /*
     * Deal with each suberror, returning if we don't want IEM to handle it.
     */
    switch (pRun->internal.suberror)
    {
        case KVM_INTERNAL_ERROR_EMULATION:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_EMULATION),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorEmulation);
            break;
        }

        case KVM_INTERNAL_ERROR_SIMUL_EX:
        case KVM_INTERNAL_ERROR_DELIVERY_EV:
        case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON:
        default:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_FATAL),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorFatal);
            const char *pszName;
            switch (pRun->internal.suberror)
            {
                case KVM_INTERNAL_ERROR_EMULATION:              pszName = "KVM_INTERNAL_ERROR_EMULATION"; break;
                case KVM_INTERNAL_ERROR_SIMUL_EX:               pszName = "KVM_INTERNAL_ERROR_SIMUL_EX"; break;
                case KVM_INTERNAL_ERROR_DELIVERY_EV:            pszName = "KVM_INTERNAL_ERROR_DELIVERY_EV"; break;
                case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON: pszName = "KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON"; break;
                default:                                        pszName = "unknown"; break;
            }
            LogRel(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%s) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror, pszName,
                    pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));
            return VERR_NEM_IPE_0;
        }
    }

    /*
     * Execute instruction in IEM and try get on with it.
     */
    Log2(("nemR3LnxHandleInternalError: Executing instruction at %04x:%08RX64 in IEM\n",
          pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip));
    VBOXSTRICTRC rcStrict = nemHCLnxImportState(pVCpu,
                                                IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT
                                                 | CPUMCTX_EXTRN_INHIBIT_NMI,
                                                &pVCpu->cpum.GstCtx, pRun);
    if (RT_SUCCESS(rcStrict))
        rcStrict = IEMExecOne(pVCpu);
    return rcStrict;
}
#endif


/**
 * Handles KVM_EXIT_MMIO.
 */
static VBOXSTRICTRC nemHCLnxHandleExitMmio(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(pRun->mmio.len <= sizeof(pRun->mmio.data));
    Assert(pRun->mmio.is_write <= 1);

#if 0
    /*
     * We cannot easily act on the exit history here, because the MMIO port
     * exit is stateful and the instruction will be completed in the next
     * KVM_RUN call.  There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu,
                     pRun->mmio.is_write
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                     pRun->s.regs.regs.pc, ASMReadTSC());
#else
    RT_NOREF(pVCpu);
#endif

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC rcStrict;
    if (pRun->mmio.is_write)
    {
        rcStrict = PGMPhysWrite(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u:WRITE %#x LB %u, %.*Rhxs -> rcStrict=%Rrc\n",
              pVCpu->idCpu,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    else
    {
        rcStrict = PGMPhysRead(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u: READ %#x LB %u -> %.*Rhxs rcStrict=%Rrc\n",
              pVCpu->idCpu,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    return rcStrict;
}


static VBOXSTRICTRC nemHCLnxHandleExit(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun, bool *pfStatefulExit)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitTotal);

    //nemHCLnxImportState(pVCpu, UINT64_MAX, &pVCpu->cpum.GstCtx);

#ifdef LOG_ENABLED
    if (LogIs3Enabled())
        nemR3LnxLogState(pVM, pVCpu);
#endif

    switch (pRun->exit_reason)
    {
        case KVM_EXIT_EXCEPTION:
            AssertFailed();
            break;

        case KVM_EXIT_MMIO:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitMmio);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitMmio(pVM, pVCpu, pRun);

        case KVM_EXIT_INTR: /* EINTR */
            //EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERRUPTED),
            //                 pRun->s.regs.regs.pc, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitIntr);
            Log5(("Intr/%u\n", pVCpu->idCpu));
            return VINF_SUCCESS;

#if 0
        case KVM_EXIT_HYPERCALL:
        case KVM_EXIT_DEBUG:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitDebug);
            AssertFailed();
            break;

        case KVM_EXIT_SYSTEM_EVENT:
            AssertFailed();
            break;

        case KVM_EXIT_DIRTY_RING_FULL:
            AssertFailed();
            break;
        case KVM_EXIT_AP_RESET_HOLD:
            AssertFailed();
            break;


        case KVM_EXIT_SHUTDOWN:
            AssertFailed();
            break;

        case KVM_EXIT_FAIL_ENTRY:
            LogRel(("NEM: KVM_EXIT_FAIL_ENTRY! hardware_entry_failure_reason=%#x cpu=%#x\n",
                    pRun->fail_entry.hardware_entry_failure_reason, pRun->fail_entry.cpu));
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_FAILED_ENTRY),
                             pRun->s.regs.regs.pc, ASMReadTSC());
            return VERR_NEM_IPE_1;

        case KVM_EXIT_INTERNAL_ERROR:
            /* we're counting sub-reasons inside the function. */
            return nemR3LnxHandleInternalError(pVCpu, pRun);
#endif

        /*
         * Foreign and unknowns.
         */
#if 0
        case KVM_EXIT_IO:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_IO on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_NMI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_NMI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_EPR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_EPR on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_WATCHDOG:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_WATCHDOG on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_ARM_NISV:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_ARM_NISV on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_STSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_STSI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_TSCH:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_TSCH on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_OSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_OSI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_PAPR_HCALL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_PAPR_HCALL on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_UCONTROL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_UCONTROL on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_DCR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_DCR on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_SIEIC:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_SIEIC on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_RESET:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_RESET on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_UNKNOWN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_UNKNOWN on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_XEN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_XEN on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
#endif
        default:
            AssertLogRelMsgFailedReturn(("Unknown exit reason %u on VCpu #%u!\n", pRun->exit_reason, pVCpu->idCpu), VERR_NEM_IPE_1);
    }
    RT_NOREF(pVM, pVCpu);
    return VERR_NOT_IMPLEMENTED;
}


VMMR3_INT_DECL(VBOXSTRICTRC) NEMR3RunGC(PVM pVM, PVMCPU pVCpu)
{
    Assert(VM_IS_NEM_ENABLED(pVM));

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

    /*
     * Enable getting breakpoint exception exits as soon as our debugger has one enabled.
     */
    if (pVM->nem.s.fSetGstDbg)
    {
        if (   pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledSwBreakpoints
            && !pVM->nem.s.fGstDbg)
        {
            struct kvm_guest_debug GstDbg;
            GstDbg.control = KVM_GUESTDBG_ENABLE;
            GstDbg.pad     = 0;

            int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_GUEST_DEBUG, &GstDbg);
            if (rcLnx)
            {
                int rc = RTErrConvertFromErrno(errno);
                AssertLogRelMsgFailedReturn(("KVM_SET_GUEST_DEBUG failed: KVM_SET_GUEST_DEBUG=%#x rcLnx=%d errno=%u rc=%Rrc\n", KVM_SET_GUEST_DEBUG, rcLnx, errno), rc);
            }
        }
        else if (   !pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledSwBreakpoints
                 && pVM->nem.s.fGstDbg)
        {
            struct kvm_guest_debug GstDbg;
            GstDbg.control = 0;
            GstDbg.pad     = 0;

            int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_GUEST_DEBUG, &GstDbg);
            if (rcLnx)
            {
                int rc = RTErrConvertFromErrno(errno);
                AssertLogRelMsgFailedReturn(("KVM_SET_GUEST_DEBUG failed: rcLnx=%d errno=%u rc=%Rrc\n", rcLnx, errno), rc);
            }
        }
    }

    /*
     * The run loop.
     */
    struct kvm_run * const  pRun                = pVCpu->nem.s.pRun;
    const bool              fSingleStepping     = DBGFIsStepping(pVCpu);
    VBOXSTRICTRC            rcStrict            = VINF_SUCCESS;
    bool                    fStatefulExit       = false;  /* For MMIO and IO exits. */
    for (unsigned iLoop = 0;; iLoop++)
    {
        /*
         * Sync the interrupt state.
         */
        rcStrict = nemHCLnxHandleInterruptFF(pVCpu);
        if (rcStrict == VINF_SUCCESS)
        { /* likely */ }
        else
        {
            LogFlow(("NEM/%u: breaking: nemHCLnxHandleInterruptFF -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
            break;
        }

        /*
         * Ensure KVM has the whole state.
         */
        if ((pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL)
        {
            int rc2 = nemHCLnxExportState(pVM, pVCpu, &pVCpu->cpum.GstCtx);
            AssertRCReturn(rc2, rc2);
        }

        /*
         * Poll timers and run for a bit.
         *
         * With the VID approach (ring-0 or ring-3) we can specify a timeout here,
         * so we take the time of the next timer event and uses that as a deadline.
         * The rounding heuristics are "tuned" so that rhel5 (1K timer) will boot fine.
         */
        /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
         *        the whole polling job when timers have changed... */
        uint64_t       offDeltaIgnored;
        uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
        if (   !VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VM_FF_TM_VIRTUAL_SYNC)
            && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
        {
            if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM_WAIT, VMCPUSTATE_STARTED_EXEC_NEM))
            {
                LogFlow(("NEM/%u: Entry @ %012RX64 ra=%012RX64 sp=%012RX64 mp=%RX32\n",
                         pVCpu->idCpu,
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_PC),
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_RA),
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_SP),
                         nemR3LnxKvmGetMpState4Log(pVCpu) ));

                TMNotifyStartOfExecution(pVM, pVCpu);

                int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);

                VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_WAIT);
                TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());

                LogFlow(("NEM/%u: Exit  @ %012RX64 ra=%012RX64 sp=%012RX64 mp=%RX32 exit=%u (%d/%d) ff=%RX64/%RX32\n",
                         pVCpu->idCpu,
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_PC),
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_RA),
                         nemR3LnxKvmGetReg4LogU64(pVCpu, KVM_RISCV64_REG_SP),
                         nemR3LnxKvmGetMpState4Log(pVCpu),
                         pRun->exit_reason, rcLnx, errno,
                         pVCpu->fLocalForcedActions, pVM->fGlobalForcedActions));

                fStatefulExit = false;
                if (RT_LIKELY(rcLnx == 0 || errno == EINTR))
                {
                    /*
                     * Deal with the exit.
                     */
                    rcStrict = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
                    if (rcStrict == VINF_SUCCESS)
                    { /* hopefully likely */ }
                    else
                    {
                        LogFlow(("NEM/%u: breaking: nemHCLnxHandleExit -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                        break;
                    }
                }
                else
                {
                    int rc2 = RTErrConvertFromErrno(errno);
                    AssertLogRelMsgFailedReturn(("KVM_RUN failed: rcLnx=%d errno=%u rc=%Rrc\n", rcLnx, errno, rc2), rc2);
                }

                /*
                 * If no relevant FFs are pending, loop.
                 */
                if (   !VM_FF_IS_ANY_SET(   pVM,   !fSingleStepping ? VM_FF_HP_R0_PRE_HM_MASK    : VM_FF_HP_R0_PRE_HM_STEP_MASK)
                    && !VMCPU_FF_IS_ANY_SET(pVCpu, !fSingleStepping ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
                { /* likely */ }
                else
                {

                    /** @todo Try handle pending flags, not just return to EM loops.  Take care
                     *        not to set important RCs here unless we've handled an exit. */
                    LogFlow(("NEM/%u: breaking: pending FF (%#x / %#RX64)\n",
                             pVCpu->idCpu, pVM->fGlobalForcedActions, (uint64_t)pVCpu->fLocalForcedActions));
                    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPost);
                    break;
                }
            }
            else
            {
                LogFlow(("NEM/%u: breaking: canceled %d (pre exec)\n", pVCpu->idCpu, VMCPU_GET_STATE(pVCpu) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnCancel);
                break;
            }
        }
        else
        {
            LogFlow(("NEM/%u: breaking: pending FF (pre exec)\n", pVCpu->idCpu));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPre);
            break;
        }
    } /* the run loop */


    /*
     * If the last exit was stateful, commit the state we provided before
     * returning to the EM loop so we have a consistent state and can safely
     * be rescheduled and whatnot.  This may require us to make multiple runs
     * for larger MMIO and I/O operations. Sigh^3.
     *
     * Note! There is no 'ing way to reset the kernel side completion callback
     *       for these stateful i/o exits.  Very annoying interface.
     */
    /** @todo check how this works with string I/O and string MMIO. */
    if (fStatefulExit && RT_SUCCESS(rcStrict))
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn);
        uint32_t const uOrgExit = pRun->exit_reason;
        for (uint32_t i = 0; ; i++)
        {
            pRun->immediate_exit = 1;
            int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);
            Log(("NEM/%u: Flushed stateful exit -> %d/%d exit_reason=%d\n", pVCpu->idCpu, rcLnx, errno, pRun->exit_reason));
            if (rcLnx == -1 && errno == EINTR)
            {
                switch (i)
                {
                    case 0: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn1Loop); break;
                    case 1: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn2Loops); break;
                    case 2: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn3Loops); break;
                    default: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn4PlusLoops); break;
                }
                break;
            }
            AssertLogRelMsgBreakStmt(rcLnx == 0 && pRun->exit_reason == uOrgExit,
                                     ("rcLnx=%d errno=%d exit_reason=%d uOrgExit=%d\n", rcLnx, errno, pRun->exit_reason, uOrgExit),
                                     rcStrict = VERR_NEM_IPE_6);
            VBOXSTRICTRC rcStrict2 = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
            if (rcStrict2 == VINF_SUCCESS || rcStrict2 == rcStrict)
            { /* likely */ }
            else if (RT_FAILURE(rcStrict2))
            {
                rcStrict = rcStrict2;
                break;
            }
            else
            {
                AssertLogRelMsgBreakStmt(rcStrict == VINF_SUCCESS,
                                         ("rcStrict=%Rrc rcStrict2=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict), VBOXSTRICTRC_VAL(rcStrict2)),
                                         rcStrict = VERR_NEM_IPE_7);
                rcStrict = rcStrict2;
            }
        }
        pRun->immediate_exit = 0;
    }

    /*
     * If the CPU is running, make sure to stop it before we try sync back the
     * state and return to EM.  We don't sync back the whole state if we can help it.
     */
    if (!VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM))
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);

    if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL)
    {
        /* Try anticipate what we might need. */
        uint64_t fImport = IEM_CPUMCTX_EXTRN_MUST_MASK /*?*/;
        if (   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
            || RT_FAILURE(rcStrict))
            fImport = CPUMCTX_EXTRN_ALL;
#if 0
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ))
            fImport |= IEM_CPUMCTX_EXTRN_XCPT_MASK;
#endif

        if (pVCpu->cpum.GstCtx.fExtrn & fImport)
        {
            int rc2 = nemHCLnxImportState(pVCpu, fImport, &pVCpu->cpum.GstCtx);
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
        pVCpu->cpum.GstCtx.fExtrn = 0;
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }

    LogFlow(("NEM/%u: %08RX64 => %Rrc\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}


/** @page pg_nem_linux_armv8 NEM/linux - Native Execution Manager, Linux.
 *
 * This is using KVM.
 *
 */

