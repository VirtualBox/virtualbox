/* $Id: GIMAllHv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager, Microsoft Hyper-V, All Contexts.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/em.h>
#include "GIMHvInternal.h"
#include "GIMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>

#ifdef IN_RING3
# include <iprt/mem.h>
#endif


#ifdef IN_RING3
/**
 * Read and validate slow hypercall parameters.
 *
 * @returns VBox status code.
 * @param   pVM               The cross context VM structure.
 * @param   pCtx              Pointer to the guest-CPU context.
 * @param   fIs64BitMode      Whether the guest is currently in 64-bit mode or not.
 * @param   enmParam          The hypercall parameter type.
 * @param   prcHv             Where to store the Hyper-V status code. Only valid
 *                            to the caller when this function returns
 *                            VINF_SUCCESS.
 */
static int gimHvReadSlowHypercallParam(PVM pVM, PCPUMCTX pCtx, bool fIs64BitMode, GIMHVHYPERCALLPARAM enmParam, int *prcHv)
{
    int       rc = VINF_SUCCESS;
    PGIMHV    pHv = &pVM->gim.s.u.Hv;
    RTGCPHYS  GCPhysParam;
    void     *pvDst;
    if (enmParam == GIMHVHYPERCALLPARAM_IN)
    {
        GCPhysParam = fIs64BitMode ? pCtx->rdx : (pCtx->rbx << 32) | pCtx->ecx;
        pvDst = pHv->pbHypercallIn;
        pHv->GCPhysHypercallIn = GCPhysParam;
    }
    else
    {
        GCPhysParam = fIs64BitMode ? pCtx->r8 : (pCtx->rdi << 32) | pCtx->esi;
        pvDst = pHv->pbHypercallOut;
        pHv->GCPhysHypercallOut = GCPhysParam;
        Assert(enmParam == GIMHVHYPERCALLPARAM_OUT);
    }

    const char *pcszParam = enmParam == GIMHVHYPERCALLPARAM_IN ? "input" : "output";  NOREF(pcszParam);
    if (RT_ALIGN_64(GCPhysParam, 8) == GCPhysParam)
    {
        if (PGMPhysIsGCPhysNormal(pVM, GCPhysParam))
        {
            rc = PGMPhysSimpleReadGCPhys(pVM, pvDst, GCPhysParam, GIM_HV_PAGE_SIZE);
            if (RT_SUCCESS(rc))
            {
                *prcHv = GIM_HV_STATUS_SUCCESS;
                return VINF_SUCCESS;
            }
            LogRel(("GIM: HyperV: Failed reading %s param at %#RGp. rc=%Rrc\n", pcszParam, GCPhysParam, rc));
            rc = VERR_GIM_HYPERCALL_MEMORY_READ_FAILED;
        }
        else
        {
            Log(("GIM: HyperV: Invalid %s param address %#RGp\n", pcszParam, GCPhysParam));
            *prcHv = GIM_HV_STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        Log(("GIM: HyperV: Misaligned %s param address %#RGp\n", pcszParam, GCPhysParam));
        *prcHv = GIM_HV_STATUS_INVALID_ALIGNMENT;
    }
    return rc;
}


/**
 * Helper for reading and validating slow hypercall input and output parameters.
 *
 * @returns VBox status code.
 * @param   pVM               The cross context VM structure.
 * @param   pCtx              Pointer to the guest-CPU context.
 * @param   fIs64BitMode      Whether the guest is currently in 64-bit mode or not.
 * @param   prcHv             Where to store the Hyper-V status code. Only valid
 *                            to the caller when this function returns
 *                            VINF_SUCCESS.
 */
static int gimHvReadSlowHypercallParamsInOut(PVM pVM, PCPUMCTX pCtx, bool fIs64BitMode, int *prcHv)
{
    int rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_IN, prcHv);
    if (   RT_SUCCESS(rc)
        && *prcHv == GIM_HV_STATUS_SUCCESS)
        rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_OUT, prcHv);
    return rc;
}
#endif


/**
 * Handles all Hyper-V hypercalls.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VERR_GIM_HYPERCALLS_NOT_ENABLED hypercalls are disabled by the
 *          guest.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_HYPERCALL_MEMORY_READ_FAILED hypercall failed while reading
 *          memory.
 * @retval  VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED hypercall failed while
 *          writing memory.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pCtx            Pointer to the guest-CPU context.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimHvHypercall(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    VMCPU_ASSERT_EMT(pVCpu);

#ifndef IN_RING3
    RT_NOREF_PV(pVCpu);
    RT_NOREF_PV(pCtx);
    return VINF_GIM_R3_HYPERCALL;
#else
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    STAM_REL_COUNTER_INC(&pVM->gim.s.StatHypercalls);

    /*
     * Verify that hypercalls are enabled by the guest.
     */
    if (!gimHvAreHypercallsEnabled(pVM))
        return VERR_GIM_HYPERCALLS_NOT_ENABLED;

    /*
     * Verify guest is in ring-0 protected mode.
     */
    uint32_t uCpl = CPUMGetGuestCPL(pVCpu);
    if (   uCpl
        || CPUMIsGuestInRealModeEx(pCtx))
    {
        return VERR_GIM_HYPERCALL_ACCESS_DENIED;
    }

    /*
     * Get the hypercall operation code and modes.
     * Fast hypercalls have only two or fewer inputs but no output parameters.
     */
    const bool       fIs64BitMode     = CPUMIsGuestIn64BitCodeEx(pCtx);
    const uint64_t   uHyperIn         = fIs64BitMode ? pCtx->rcx : (pCtx->rdx << 32) | pCtx->eax;
    const uint16_t   uHyperOp         = GIM_HV_HYPERCALL_IN_CALL_CODE(uHyperIn);
    const bool       fHyperFast       = GIM_HV_HYPERCALL_IN_IS_FAST(uHyperIn);
    const uint16_t   cHyperReps       = GIM_HV_HYPERCALL_IN_REP_COUNT(uHyperIn);
    const uint16_t   idxHyperRepStart = GIM_HV_HYPERCALL_IN_REP_START_IDX(uHyperIn);
    uint64_t         cHyperRepsDone   = 0;

    /* Currently no repeating hypercalls are supported. */
    RT_NOREF2(cHyperReps, idxHyperRepStart);

    int rc     = VINF_SUCCESS;
    int rcHv   = GIM_HV_STATUS_OPERATION_DENIED;
    PGIMHV pHv = &pVM->gim.s.u.Hv;

    /*
     * Validate common hypercall input parameters.
     */
    if (   !GIM_HV_HYPERCALL_IN_RSVD_1(uHyperIn)
        && !GIM_HV_HYPERCALL_IN_RSVD_2(uHyperIn)
        && !GIM_HV_HYPERCALL_IN_RSVD_3(uHyperIn))
    {
        /*
         * Perform the hypercall.
         */
        switch (uHyperOp)
        {
            case GIM_HV_HYPERCALL_OP_RETREIVE_DEBUG_DATA:   /* Non-rep, memory IO. */
            {
                if (pHv->uPartFlags & GIM_HV_PART_FLAGS_DEBUGGING)
                {
                    rc  = gimHvReadSlowHypercallParamsInOut(pVM, pCtx, fIs64BitMode, &rcHv);
                    if (   RT_SUCCESS(rc)
                        && rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        LogRelMax(1, ("GIM: HyperV: Initiated debug data reception via hypercall\n"));
                        rc = gimR3HvHypercallRetrieveDebugData(pVM, &rcHv);
                        if (RT_FAILURE(rc))
                            LogRelMax(10, ("GIM: HyperV: gimR3HvHypercallRetrieveDebugData failed. rc=%Rrc\n", rc));
                    }
                }
                else
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                break;
            }

            case GIM_HV_HYPERCALL_OP_POST_DEBUG_DATA:   /* Non-rep, memory IO. */
            {
                if (pHv->uPartFlags & GIM_HV_PART_FLAGS_DEBUGGING)
                {
                    rc = gimHvReadSlowHypercallParamsInOut(pVM, pCtx, fIs64BitMode, &rcHv);
                    if (   RT_SUCCESS(rc)
                        && rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        LogRelMax(1, ("GIM: HyperV: Initiated debug data transmission via hypercall\n"));
                        rc = gimR3HvHypercallPostDebugData(pVM, &rcHv);
                        if (RT_FAILURE(rc))
                            LogRelMax(10, ("GIM: HyperV: gimR3HvHypercallPostDebugData failed. rc=%Rrc\n", rc));
                    }
                }
                else
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                break;
            }

            case GIM_HV_HYPERCALL_OP_RESET_DEBUG_SESSION:   /* Non-rep, fast (register IO). */
            {
                if (pHv->uPartFlags & GIM_HV_PART_FLAGS_DEBUGGING)
                {
                    uint32_t fFlags = 0;
                    if (!fHyperFast)
                    {
                        rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_IN, &rcHv);
                        if (   RT_SUCCESS(rc)
                            && rcHv == GIM_HV_STATUS_SUCCESS)
                        {
                            PGIMHVDEBUGRESETIN pIn = (PGIMHVDEBUGRESETIN)pHv->pbHypercallIn;
                            fFlags = pIn->fFlags;
                        }
                    }
                    else
                    {
                        rcHv = GIM_HV_STATUS_SUCCESS;
                        fFlags = fIs64BitMode ? pCtx->rdx : pCtx->ebx;
                    }

                    /*
                     * Nothing to flush on the sending side as we don't maintain our own buffers.
                     */
                    /** @todo We should probably ask the debug receive thread to flush it's buffer. */
                    if (rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        if (fFlags)
                            LogRel(("GIM: HyperV: Resetting debug session via hypercall\n"));
                        else
                            rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
                    }
                }
                else
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                break;
            }

            case GIM_HV_HYPERCALL_OP_POST_MESSAGE:      /* Non-rep, memory IO. */
            {
                if (pHv->fIsInterfaceVs)
                {
                    rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_IN, &rcHv);
                    if (   RT_SUCCESS(rc)
                        && rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        PGIMHVPOSTMESSAGEIN pMsgIn = (PGIMHVPOSTMESSAGEIN)pHv->pbHypercallIn;
                        PCGIMHVCPU          pHvCpu = &pVCpu->gim.s.u.HvCpu;
                        if (    pMsgIn->uConnectionId  == GIM_HV_VMBUS_MSG_CONNECTION_ID
                            &&  pMsgIn->enmMessageType == GIMHVMSGTYPE_VMBUS
                            && !MSR_GIM_HV_SINT_IS_MASKED(pHvCpu->auSintMsrs[GIM_HV_VMBUS_MSG_SINT])
                            &&  MSR_GIM_HV_SIMP_IS_ENABLED(pHvCpu->uSimpMsr))
                        {
                            RTGCPHYS GCPhysSimp = MSR_GIM_HV_SIMP_GPA(pHvCpu->uSimpMsr);
                            if (PGMPhysIsGCPhysNormal(pVM, GCPhysSimp))
                            {
                                /*
                                 * The VMBus client (guest) expects to see 0xf at offsets 4 and 16 and 1 at offset 0.
                                 */
                                GIMHVMSG HvMsg;
                                RT_ZERO(HvMsg);
                                HvMsg.MsgHdr.enmMessageType = GIMHVMSGTYPE_VMBUS;
                                HvMsg.MsgHdr.cbPayload = 0xf;
                                HvMsg.aPayload[0]      = 0xf;
                                uint16_t const offMsg = GIM_HV_VMBUS_MSG_SINT * sizeof(GIMHVMSG);
                                int rc2 = PGMPhysSimpleWriteGCPhys(pVM, GCPhysSimp + offMsg, &HvMsg, sizeof(HvMsg));
                                if (RT_SUCCESS(rc2))
                                    LogRel(("GIM: HyperV: SIMP hypercall faking message at %#RGp:%u\n", GCPhysSimp, offMsg));
                                else
                                {
                                    LogRel(("GIM: HyperV: Failed to write SIMP message at %#RGp:%u, rc=%Rrc\n", GCPhysSimp,
                                            offMsg, rc));
                                }
                            }
                        }

                        /*
                         * Make the call fail after updating the SIMP, so the guest can go back to using
                         * the Hyper-V debug MSR interface. Any error code below GIM_HV_STATUS_NOT_ACKNOWLEDGED
                         * and the guest tries to proceed with initializing VMBus which is totally unnecessary
                         * for what we're trying to accomplish, i.e. convince guest to use Hyper-V debugging. Also,
                         * we don't implement other VMBus/SynIC functionality so the guest would #GP and die.
                         */
                        rcHv = GIM_HV_STATUS_NOT_ACKNOWLEDGED;
                    }
                    else
                        rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
                }
                else
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                break;
            }

            case GIM_HV_EXT_HYPERCALL_OP_QUERY_CAP:              /* Non-rep, extended hypercall. */
            {
                if (pHv->uPartFlags & GIM_HV_PART_FLAGS_EXTENDED_HYPERCALLS)
                {
                    rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_OUT, &rcHv);
                    if (   RT_SUCCESS(rc)
                        && rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        rc = gimR3HvHypercallExtQueryCap(pVM, &rcHv);
                    }
                }
                else
                {
                    LogRel(("GIM: HyperV: Denied HvExtCallQueryCapabilities when the feature is not exposed\n"));
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                }
                break;
            }

            case GIM_HV_EXT_HYPERCALL_OP_GET_BOOT_ZEROED_MEM:    /* Non-rep, extended hypercall. */
            {
                if (pHv->uPartFlags & GIM_HV_PART_FLAGS_EXTENDED_HYPERCALLS)
                {
                    rc = gimHvReadSlowHypercallParam(pVM, pCtx, fIs64BitMode, GIMHVHYPERCALLPARAM_OUT, &rcHv);
                    if (   RT_SUCCESS(rc)
                        && rcHv == GIM_HV_STATUS_SUCCESS)
                    {
                        rc = gimR3HvHypercallExtGetBootZeroedMem(pVM, &rcHv);
                    }
                }
                else
                {
                    LogRel(("GIM: HyperV: Denied HvExtCallGetBootZeroedMemory when the feature is not exposed\n"));
                    rcHv = GIM_HV_STATUS_ACCESS_DENIED;
                }
                break;
            }

            default:
            {
                LogRel(("GIM: HyperV: Unknown/invalid hypercall opcode %#x (%u)\n", uHyperOp, uHyperOp));
                rcHv = GIM_HV_STATUS_INVALID_HYPERCALL_CODE;
                break;
            }
        }
    }
    else
        rcHv = GIM_HV_STATUS_INVALID_HYPERCALL_INPUT;

    /*
     * Update the guest with results of the hypercall.
     */
    if (RT_SUCCESS(rc))
    {
        if (fIs64BitMode)
            pCtx->rax = (cHyperRepsDone << 32) | rcHv;
        else
        {
            pCtx->edx = cHyperRepsDone;
            pCtx->eax = rcHv;
        }
    }

    return rc;
#endif
}


/**
 * Returns a pointer to the MMIO2 regions supported by Hyper-V.
 *
 * @returns Pointer to an array of MMIO2 regions.
 * @param   pVM         The cross context VM structure.
 * @param   pcRegions   Where to store the number of regions in the array.
 */
VMM_INT_DECL(PGIMMMIO2REGION) gimHvGetMmio2Regions(PVM pVM, uint32_t *pcRegions)
{
    Assert(GIMIsEnabled(pVM));
    PGIMHV pHv = &pVM->gim.s.u.Hv;

    AssertCompile(RT_ELEMENTS(pHv->aMmio2Regions) <= 8);
    *pcRegions = RT_ELEMENTS(pHv->aMmio2Regions);
    return pHv->aMmio2Regions;
}


/**
 * Returns whether the guest has configured and enabled the use of Hyper-V's
 * hypercall interface.
 *
 * @returns true if hypercalls are enabled, false otherwise.
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(bool) gimHvAreHypercallsEnabled(PCVM pVM)
{
    return RT_BOOL(pVM->gim.s.u.Hv.u64GuestOsIdMsr != 0);
}


/**
 * Returns whether the guest has configured and enabled the use of Hyper-V's
 * paravirtualized TSC.
 *
 * @returns true if paravirt. TSC is enabled, false otherwise.
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(bool) gimHvIsParavirtTscEnabled(PVM pVM)
{
    return MSR_GIM_HV_REF_TSC_IS_ENABLED(pVM->gim.s.u.Hv.u64TscPageMsr);
}


#ifdef IN_RING3
/**
 * Gets the descriptive OS ID variant as identified via the
 * MSR_GIM_HV_GUEST_OS_ID MSR.
 *
 * @returns The name.
 * @param   uGuestOsIdMsr     The MSR_GIM_HV_GUEST_OS_ID MSR.
 */
static const char *gimHvGetGuestOsIdVariantName(uint64_t uGuestOsIdMsr)
{
    /* Refer the Hyper-V spec, section 3.6 "Reporting the Guest OS Identity". */
    uint32_t uVendor = MSR_GIM_HV_GUEST_OS_ID_VENDOR(uGuestOsIdMsr);
    if (uVendor == 1 /* Microsoft */)
    {
        uint32_t uOsVariant = MSR_GIM_HV_GUEST_OS_ID_OS_VARIANT(uGuestOsIdMsr);
        switch (uOsVariant)
        {
            case 0:  return "Undefined";
            case 1:  return "MS-DOS";
            case 2:  return "Windows 3.x";
            case 3:  return "Windows 9x";
            case 4:  return "Windows NT or derivative";
            case 5:  return "Windows CE";
            default: return "Unknown";
        }
    }
    return "Unknown";
}
#endif

/**
 * Gets the time reference count for the current VM.
 *
 * @returns The time reference count.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(uint64_t) gimHvGetTimeRefCount(PVMCPUCC pVCpu)
{
    /* Hyper-V reports the time in 100 ns units (10 MHz). */
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCGIMHV pHv = &pVCpu->CTX_SUFF(pVM)->gim.s.u.Hv;
    uint64_t const u64Tsc        = TMCpuTickGet(pVCpu);     /** @todo should we be passing VCPU0 always? */
    uint64_t const u64TscHz      = pHv->cTscTicksPerSecond;
    uint64_t const u64Tsc100NS   = u64TscHz / UINT64_C(10000000); /* 100 ns */
    uint64_t const uTimeRefCount = (u64Tsc / u64Tsc100NS);
    return uTimeRefCount;
}


/**
 * Starts the synthetic timer.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pHvStimer   Pointer to the Hyper-V synthetic timer.
 *
 * @remarks Caller needs to hold the timer critical section.
 * @thread  Any.
 */
VMM_INT_DECL(void) gimHvStartStimer(PVMCPUCC pVCpu, PCGIMHVSTIMER pHvStimer)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    TMTIMERHANDLE hTimer = pHvStimer->hTimer;
    Assert(TMTimerIsLockOwner(pVM, hTimer));

    uint64_t const uTimerCount = pHvStimer->uStimerCountMsr;
    if (uTimerCount)
    {
        uint64_t const uTimerCountNS = uTimerCount * 100;

        /* For periodic timers, 'uTimerCountNS' represents the relative interval. */
        if (MSR_GIM_HV_STIMER_IS_PERIODIC(pHvStimer->uStimerConfigMsr))
        {
            TMTimerSetNano(pVM, hTimer, uTimerCountNS);
            LogFlow(("GIM%u: HyperV: Started relative periodic STIMER%u with uTimerCountNS=%RU64\n", pVCpu->idCpu,
                     pHvStimer->idxStimer, uTimerCountNS));
        }
        else
        {
            /* For one-shot timers, 'uTimerCountNS' represents an absolute expiration wrt to Hyper-V reference time,
               we convert it to a relative time and program the timer. */
            uint64_t const uCurRefTimeNS = gimHvGetTimeRefCount(pVCpu) * 100;
            if (uTimerCountNS > uCurRefTimeNS)
            {
                uint64_t const uRelativeNS = uTimerCountNS - uCurRefTimeNS;
                TMTimerSetNano(pVM, hTimer, uRelativeNS);
                LogFlow(("GIM%u: HyperV: Started one-shot relative STIMER%u with uRelativeNS=%RU64\n", pVCpu->idCpu,
                         pHvStimer->idxStimer, uRelativeNS));
            }
        }
        /** @todo frequency hinting? */
    }
}


/**
 * Stops the synthetic timer for the given VCPU.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pHvStimer   Pointer to the Hyper-V synthetic timer.
 *
 * @remarks Caller needs to the hold the timer critical section.
 * @thread  EMT(pVCpu).
 */
static void gimHvStopStimer(PVMCPUCC pVCpu, PGIMHVSTIMER pHvStimer)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    TMTIMERHANDLE hTimer = pHvStimer->hTimer;
    Assert(TMTimerIsLockOwner(pVM, hTimer));

    if (TMTimerIsActive(pVM, hTimer))
        TMTimerStop(pVM, hTimer);
}


/**
 * MSR read handler for Hyper-V.
 *
 * @returns Strict VBox status code like CPUMQueryGuestMsr().
 * @retval  VINF_CPUM_R3_MSR_READ
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR being read.
 * @param   pRange      The range this MSR belongs to.
 * @param   puValue     Where to store the MSR value read.
 *
 * @thread  EMT.
 */
VMM_INT_DECL(VBOXSTRICTRC) gimHvReadMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    NOREF(pRange);
    PVMCC   pVM = pVCpu->CTX_SUFF(pVM);
    PCGIMHV pHv = &pVM->gim.s.u.Hv;

    switch (idMsr)
    {
        case MSR_GIM_HV_TIME_REF_COUNT:
            *puValue = gimHvGetTimeRefCount(pVCpu);
            return VINF_SUCCESS;

        case MSR_GIM_HV_VP_INDEX:
            *puValue = pVCpu->idCpu;
            return VINF_SUCCESS;

        case MSR_GIM_HV_TPR:
            *puValue = APICHvGetTpr(pVCpu);
            return VINF_SUCCESS;

        case MSR_GIM_HV_ICR:
            *puValue = APICHvGetIcr(pVCpu);
            return VINF_SUCCESS;

        case MSR_GIM_HV_GUEST_OS_ID:
            *puValue = pHv->u64GuestOsIdMsr;
            return VINF_SUCCESS;

        case MSR_GIM_HV_HYPERCALL:
            *puValue = pHv->u64HypercallMsr;
            return VINF_SUCCESS;

        case MSR_GIM_HV_REF_TSC:
            *puValue = pHv->u64TscPageMsr;
            return VINF_SUCCESS;

        case MSR_GIM_HV_TSC_FREQ:
            *puValue = TMCpuTicksPerSecond(pVM);
            return VINF_SUCCESS;

        case MSR_GIM_HV_APIC_FREQ:
        {
            int rc = APICGetTimerFreq(pVM, puValue);
            if (RT_FAILURE(rc))
                return VERR_CPUM_RAISE_GP_0;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SYNTH_DEBUG_STATUS:
            *puValue = pHv->uDbgStatusMsr;
            return VINF_SUCCESS;

        case MSR_GIM_HV_SINT0:   case MSR_GIM_HV_SINT1:   case MSR_GIM_HV_SINT2:   case MSR_GIM_HV_SINT3:
        case MSR_GIM_HV_SINT4:   case MSR_GIM_HV_SINT5:   case MSR_GIM_HV_SINT6:   case MSR_GIM_HV_SINT7:
        case MSR_GIM_HV_SINT8:   case MSR_GIM_HV_SINT9:   case MSR_GIM_HV_SINT10:  case MSR_GIM_HV_SINT11:
        case MSR_GIM_HV_SINT12:  case MSR_GIM_HV_SINT13:  case MSR_GIM_HV_SINT14:  case MSR_GIM_HV_SINT15:
        {
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            *puValue = pHvCpu->auSintMsrs[idMsr - MSR_GIM_HV_SINT0];
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_STIMER0_CONFIG:
        case MSR_GIM_HV_STIMER1_CONFIG:
        case MSR_GIM_HV_STIMER2_CONFIG:
        case MSR_GIM_HV_STIMER3_CONFIG:
        {
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            uint8_t const idxStimer  = (idMsr - MSR_GIM_HV_STIMER0_CONFIG) >> 1;
            PCGIMHVSTIMER pcHvStimer = &pHvCpu->aStimers[idxStimer];
            *puValue = pcHvStimer->uStimerConfigMsr;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_STIMER0_COUNT:
        case MSR_GIM_HV_STIMER1_COUNT:
        case MSR_GIM_HV_STIMER2_COUNT:
        case MSR_GIM_HV_STIMER3_COUNT:
        {
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            uint8_t const idxStimer  = (idMsr - MSR_GIM_HV_STIMER0_COUNT) >> 1;
            PCGIMHVSTIMER pcHvStimer = &pHvCpu->aStimers[idxStimer];
            *puValue = pcHvStimer->uStimerCountMsr;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_EOM:
        {
            *puValue = 0;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SCONTROL:
        {
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            *puValue = pHvCpu->uSControlMsr;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SIMP:
        {
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            *puValue = pHvCpu->uSimpMsr;
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SVERSION:
            *puValue = GIM_HV_SVERSION;
            return VINF_SUCCESS;

        case MSR_GIM_HV_RESET:
            *puValue = 0;
            return VINF_SUCCESS;

        case MSR_GIM_HV_CRASH_CTL:
            *puValue = pHv->uCrashCtlMsr;
            return VINF_SUCCESS;

        case MSR_GIM_HV_CRASH_P0: *puValue = pHv->uCrashP0Msr;   return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P1: *puValue = pHv->uCrashP1Msr;   return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P2: *puValue = pHv->uCrashP2Msr;   return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P3: *puValue = pHv->uCrashP3Msr;   return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P4: *puValue = pHv->uCrashP4Msr;   return VINF_SUCCESS;

        case MSR_GIM_HV_DEBUG_OPTIONS_MSR:
        {
            if (pHv->fIsVendorMsHv)
            {
#ifndef IN_RING3
                return VINF_CPUM_R3_MSR_READ;
#else
                LogRelMax(1, ("GIM: HyperV: Guest querying debug options, suggesting %s interface\n",
                              pHv->fDbgHypercallInterface ? "hypercall" : "MSR"));
                *puValue = pHv->fDbgHypercallInterface ? GIM_HV_DEBUG_OPTIONS_USE_HYPERCALLS : 0;
                return VINF_SUCCESS;
#endif
            }
            break;
        }

        /* Write-only MSRs: */
        case MSR_GIM_HV_EOI:
        /* Reserved/unknown MSRs: */
        default:
        {
#ifdef IN_RING3
            static uint32_t s_cTimes = 0;
            if (s_cTimes++ < 20)
                LogRel(("GIM: HyperV: Unknown/invalid RdMsr (%#x) -> #GP(0)\n", idMsr));
            LogFunc(("Unknown/invalid RdMsr (%#RX32) -> #GP(0)\n", idMsr));
            break;
#else
            return VINF_CPUM_R3_MSR_READ;
#endif
        }
    }

    return VERR_CPUM_RAISE_GP_0;
}


/**
 * MSR write handler for Hyper-V.
 *
 * @returns Strict VBox status code like CPUMSetGuestMsr().
 * @retval  VINF_CPUM_R3_MSR_WRITE
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR being written.
 * @param   pRange      The range this MSR belongs to.
 * @param   uRawValue   The raw value with the ignored bits not masked.
 *
 * @thread  EMT.
 */
VMM_INT_DECL(VBOXSTRICTRC) gimHvWriteMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uRawValue)
{
    NOREF(pRange);
    PVMCC  pVM = pVCpu->CTX_SUFF(pVM);
    PGIMHV pHv = &pVM->gim.s.u.Hv;

    switch (idMsr)
    {
        case MSR_GIM_HV_TPR:
            return APICHvSetTpr(pVCpu, uRawValue);

        case MSR_GIM_HV_EOI:
            return APICHvSetEoi(pVCpu, uRawValue);

        case MSR_GIM_HV_ICR:
            return APICHvSetIcr(pVCpu, uRawValue);

        case MSR_GIM_HV_GUEST_OS_ID:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            /* Disable the hypercall-page and hypercalls if 0 is written to this MSR. */
            if (!uRawValue)
            {
                if (MSR_GIM_HV_HYPERCALL_PAGE_IS_ENABLED(pHv->u64HypercallMsr))
                {
                    gimR3HvDisableHypercallPage(pVM);
                    pHv->u64HypercallMsr &= ~MSR_GIM_HV_HYPERCALL_PAGE_ENABLE;
                    LogRel(("GIM: HyperV: Hypercall page disabled via Guest OS ID MSR\n"));
                }
            }
            else
            {
                LogRel(("GIM: HyperV: Guest OS reported ID %#RX64\n", uRawValue));
                LogRel(("GIM: HyperV: Open-source=%RTbool Vendor=%#x OS=%#x (%s) Major=%u Minor=%u ServicePack=%u Build=%u\n",
                        MSR_GIM_HV_GUEST_OS_ID_IS_OPENSOURCE(uRawValue),   MSR_GIM_HV_GUEST_OS_ID_VENDOR(uRawValue),
                        MSR_GIM_HV_GUEST_OS_ID_OS_VARIANT(uRawValue),      gimHvGetGuestOsIdVariantName(uRawValue),
                        MSR_GIM_HV_GUEST_OS_ID_MAJOR_VERSION(uRawValue),   MSR_GIM_HV_GUEST_OS_ID_MINOR_VERSION(uRawValue),
                        MSR_GIM_HV_GUEST_OS_ID_SERVICE_VERSION(uRawValue), MSR_GIM_HV_GUEST_OS_ID_BUILD(uRawValue)));

                /* Update the CPUID leaf, see Hyper-V spec. "Microsoft Hypervisor CPUID Leaves". */
                CPUMCPUIDLEAF HyperLeaf;
                RT_ZERO(HyperLeaf);
                HyperLeaf.uLeaf = UINT32_C(0x40000002);
                HyperLeaf.uEax  = MSR_GIM_HV_GUEST_OS_ID_BUILD(uRawValue);
                HyperLeaf.uEbx  =  MSR_GIM_HV_GUEST_OS_ID_MINOR_VERSION(uRawValue)
                                | (MSR_GIM_HV_GUEST_OS_ID_MAJOR_VERSION(uRawValue) << 16);
                HyperLeaf.uEcx  = MSR_GIM_HV_GUEST_OS_ID_SERVICE_VERSION(uRawValue);
                HyperLeaf.uEdx  =  MSR_GIM_HV_GUEST_OS_ID_SERVICE_VERSION(uRawValue)
                                | (MSR_GIM_HV_GUEST_OS_ID_BUILD(uRawValue) << 24);
                int rc2 = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
                AssertRC(rc2);
            }

            pHv->u64GuestOsIdMsr = uRawValue;

            /*
             * Update EM on hypercall instruction enabled state.
             */
            if (uRawValue)
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                    EMSetHypercallInstructionsEnabled(pVM->CTX_SUFF(apCpus)[idCpu], true);
            else
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                    EMSetHypercallInstructionsEnabled(pVM->CTX_SUFF(apCpus)[idCpu], false);

            return VINF_SUCCESS;
#endif /* IN_RING3 */
        }

        case MSR_GIM_HV_HYPERCALL:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            /** @todo There is/was a problem with hypercalls for FreeBSD 10.1 guests,
             *  see @bugref{7270#c116}. */
            /* First, update all but the hypercall page enable bit. */
            pHv->u64HypercallMsr = (uRawValue & ~MSR_GIM_HV_HYPERCALL_PAGE_ENABLE);

            /* Hypercall page can only be enabled when the guest has enabled hypercalls. */
            bool fEnable = MSR_GIM_HV_HYPERCALL_PAGE_IS_ENABLED(uRawValue);
            if (   fEnable
                && !gimHvAreHypercallsEnabled(pVM))
            {
                return VINF_SUCCESS;
            }

            /* Is the guest disabling the hypercall-page? Allow it regardless of the Guest-OS Id Msr. */
            if (!fEnable)
            {
                gimR3HvDisableHypercallPage(pVM);
                pHv->u64HypercallMsr = uRawValue;
                return VINF_SUCCESS;
            }

            /* Enable the hypercall-page. */
            RTGCPHYS GCPhysHypercallPage = MSR_GIM_HV_HYPERCALL_GUEST_PFN(uRawValue) << GUEST_PAGE_SHIFT;
            int rc = gimR3HvEnableHypercallPage(pVM, GCPhysHypercallPage);
            if (RT_SUCCESS(rc))
            {
                pHv->u64HypercallMsr = uRawValue;
                return VINF_SUCCESS;
            }

            return VERR_CPUM_RAISE_GP_0;
#endif
        }

        case MSR_GIM_HV_REF_TSC:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else  /* IN_RING3 */
            /* First, update all but the TSC page enable bit. */
            pHv->u64TscPageMsr = (uRawValue & ~MSR_GIM_HV_REF_TSC_ENABLE);

            /* Is the guest disabling the TSC page? */
            bool fEnable = MSR_GIM_HV_REF_TSC_IS_ENABLED(uRawValue);
            if (!fEnable)
            {
                gimR3HvDisableTscPage(pVM);
                pHv->u64TscPageMsr = uRawValue;
                return VINF_SUCCESS;
            }

            /* Enable the TSC page. */
            RTGCPHYS GCPhysTscPage = MSR_GIM_HV_REF_TSC_GUEST_PFN(uRawValue) << GUEST_PAGE_SHIFT;
            int rc = gimR3HvEnableTscPage(pVM, GCPhysTscPage, false /* fUseThisTscSequence */, 0 /* uTscSequence */);
            if (RT_SUCCESS(rc))
            {
                pHv->u64TscPageMsr = uRawValue;
                return VINF_SUCCESS;
            }

            return VERR_CPUM_RAISE_GP_0;
#endif /* IN_RING3 */
        }

        case MSR_GIM_HV_APIC_ASSIST_PAGE:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else  /* IN_RING3 */
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            pHvCpu->uApicAssistPageMsr = uRawValue;

            if (MSR_GIM_HV_APICASSIST_PAGE_IS_ENABLED(uRawValue))
            {
                RTGCPHYS GCPhysApicAssistPage = MSR_GIM_HV_APICASSIST_GUEST_PFN(uRawValue) << GUEST_PAGE_SHIFT;
                if (PGMPhysIsGCPhysNormal(pVM, GCPhysApicAssistPage))
                {
                    int rc = gimR3HvEnableApicAssistPage(pVCpu, GCPhysApicAssistPage);
                    if (RT_SUCCESS(rc))
                    {
                        pHvCpu->uApicAssistPageMsr = uRawValue;
                        return VINF_SUCCESS;
                    }
                }
                else
                {
                    LogRelMax(5, ("GIM%u: HyperV: APIC-assist page address %#RGp invalid!\n", pVCpu->idCpu,
                                  GCPhysApicAssistPage));
                }
            }
            else
                gimR3HvDisableApicAssistPage(pVCpu);

            return VERR_CPUM_RAISE_GP_0;
#endif /* IN_RING3 */
        }

        case MSR_GIM_HV_RESET:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            if (MSR_GIM_HV_RESET_IS_ENABLED(uRawValue))
            {
                LogRel(("GIM: HyperV: Reset initiated through MSR\n"));
                int rc = PDMDevHlpVMReset(pVM->gim.s.pDevInsR3, PDMVMRESET_F_GIM);
                AssertRC(rc); /* Note! Not allowed to return VINF_EM_RESET / VINF_EM_HALT here, so ignore them. */
            }
            /* else: Ignore writes to other bits. */
            return VINF_SUCCESS;
#endif /* IN_RING3 */
        }

        case MSR_GIM_HV_CRASH_CTL:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            if (uRawValue & MSR_GIM_HV_CRASH_CTL_NOTIFY)
            {
                LogRel(("GIM: HyperV: Guest indicates a fatal condition! P0=%#RX64 P1=%#RX64 P2=%#RX64 P3=%#RX64 P4=%#RX64\n",
                        pHv->uCrashP0Msr, pHv->uCrashP1Msr, pHv->uCrashP2Msr, pHv->uCrashP3Msr, pHv->uCrashP4Msr));
                DBGFR3ReportBugCheck(pVM, pVCpu, DBGFEVENT_BSOD_MSR, pHv->uCrashP0Msr, pHv->uCrashP1Msr,
                                     pHv->uCrashP2Msr, pHv->uCrashP3Msr, pHv->uCrashP4Msr);
                /* (Do not try pass VINF_EM_DBG_EVENT, doesn't work from here!) */
            }
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_SYNTH_DEBUG_SEND_BUFFER:
        {
            if (!pHv->fDbgEnabled)
                return VERR_CPUM_RAISE_GP_0;
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            RTGCPHYS GCPhysBuffer    = (RTGCPHYS)uRawValue;
            pHv->uDbgSendBufferMsr = GCPhysBuffer;
            if (PGMPhysIsGCPhysNormal(pVM, GCPhysBuffer))
                LogRel(("GIM: HyperV: Set up debug send buffer at %#RGp\n", GCPhysBuffer));
            else
                LogRel(("GIM: HyperV: Destroyed debug send buffer\n"));
            pHv->uDbgSendBufferMsr = uRawValue;
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_SYNTH_DEBUG_RECEIVE_BUFFER:
        {
            if (!pHv->fDbgEnabled)
                return VERR_CPUM_RAISE_GP_0;
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            RTGCPHYS GCPhysBuffer  = (RTGCPHYS)uRawValue;
            pHv->uDbgRecvBufferMsr = GCPhysBuffer;
            if (PGMPhysIsGCPhysNormal(pVM, GCPhysBuffer))
                LogRel(("GIM: HyperV: Set up debug receive buffer at %#RGp\n", GCPhysBuffer));
            else
                LogRel(("GIM: HyperV: Destroyed debug receive buffer\n"));
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_SYNTH_DEBUG_PENDING_BUFFER:
        {
            if (!pHv->fDbgEnabled)
                return VERR_CPUM_RAISE_GP_0;
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            RTGCPHYS GCPhysBuffer      = (RTGCPHYS)uRawValue;
            pHv->uDbgPendingBufferMsr  = GCPhysBuffer;
            if (PGMPhysIsGCPhysNormal(pVM, GCPhysBuffer))
                LogRel(("GIM: HyperV: Set up debug pending buffer at %#RGp\n", uRawValue));
            else
                LogRel(("GIM: HyperV: Destroyed debug pending buffer\n"));
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_SYNTH_DEBUG_CONTROL:
        {
            if (!pHv->fDbgEnabled)
                return VERR_CPUM_RAISE_GP_0;
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            if (   MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_WRITE(uRawValue)
                && MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_READ(uRawValue))
            {
                LogRel(("GIM: HyperV: Requesting both read and write through debug control MSR -> #GP(0)\n"));
                return VERR_CPUM_RAISE_GP_0;
            }

            if (MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_WRITE(uRawValue))
            {
                uint32_t cbWrite = MSR_GIM_HV_SYNTH_DEBUG_CONTROL_W_LEN(uRawValue);
                if (   cbWrite > 0
                    && cbWrite < GIM_HV_PAGE_SIZE)
                {
                    if (PGMPhysIsGCPhysNormal(pVM, (RTGCPHYS)pHv->uDbgSendBufferMsr))
                    {
                        Assert(pHv->pvDbgBuffer);
                        int rc = PGMPhysSimpleReadGCPhys(pVM, pHv->pvDbgBuffer, (RTGCPHYS)pHv->uDbgSendBufferMsr, cbWrite);
                        if (RT_SUCCESS(rc))
                        {
                            LogRelMax(1, ("GIM: HyperV: Initiated debug data transmission via MSR\n"));
                            uint32_t cbWritten = 0;
                            rc = gimR3HvDebugWrite(pVM, pHv->pvDbgBuffer, cbWrite, &cbWritten, false /*fUdpPkt*/);
                            if (   RT_SUCCESS(rc)
                                && cbWrite == cbWritten)
                                pHv->uDbgStatusMsr = MSR_GIM_HV_SYNTH_DEBUG_STATUS_W_SUCCESS;
                            else
                                pHv->uDbgStatusMsr = 0;
                        }
                        else
                            LogRelMax(5, ("GIM: HyperV: Failed to read debug send buffer at %#RGp, rc=%Rrc\n",
                                          (RTGCPHYS)pHv->uDbgSendBufferMsr, rc));
                    }
                    else
                        LogRelMax(5, ("GIM: HyperV: Debug send buffer address %#RGp invalid! Ignoring debug write!\n",
                                      (RTGCPHYS)pHv->uDbgSendBufferMsr));
                }
                else
                    LogRelMax(5, ("GIM: HyperV: Invalid write size %u specified in MSR, ignoring debug write!\n",
                                  MSR_GIM_HV_SYNTH_DEBUG_CONTROL_W_LEN(uRawValue)));
            }
            else if (MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_READ(uRawValue))
            {
                if (PGMPhysIsGCPhysNormal(pVM, (RTGCPHYS)pHv->uDbgRecvBufferMsr))
                {
                    LogRelMax(1, ("GIM: HyperV: Initiated debug data reception via MSR\n"));
                    uint32_t cbReallyRead;
                    Assert(pHv->pvDbgBuffer);
                    int rc = gimR3HvDebugRead(pVM, pHv->pvDbgBuffer, GIM_HV_PAGE_SIZE, GIM_HV_PAGE_SIZE,
                                              &cbReallyRead, 0, false /*fUdpPkt*/);
                    if (   RT_SUCCESS(rc)
                        && cbReallyRead > 0)
                    {
                        rc = PGMPhysSimpleWriteGCPhys(pVM, (RTGCPHYS)pHv->uDbgRecvBufferMsr, pHv->pvDbgBuffer, cbReallyRead);
                        if (RT_SUCCESS(rc))
                        {
                            pHv->uDbgStatusMsr  = ((uint16_t)cbReallyRead) << 16;
                            pHv->uDbgStatusMsr |= MSR_GIM_HV_SYNTH_DEBUG_STATUS_R_SUCCESS;
                        }
                        else
                        {
                            pHv->uDbgStatusMsr = 0;
                            LogRelMax(5, ("GIM: HyperV: PGMPhysSimpleWriteGCPhys failed. rc=%Rrc\n", rc));
                        }
                    }
                    else
                        pHv->uDbgStatusMsr = 0;
                }
                else
                {
                    LogRelMax(5, ("GIM: HyperV: Debug receive buffer address %#RGp invalid! Ignoring debug read!\n",
                                  (RTGCPHYS)pHv->uDbgRecvBufferMsr));
                }
            }
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_SINT0:    case MSR_GIM_HV_SINT1:    case MSR_GIM_HV_SINT2:    case MSR_GIM_HV_SINT3:
        case MSR_GIM_HV_SINT4:    case MSR_GIM_HV_SINT5:    case MSR_GIM_HV_SINT6:    case MSR_GIM_HV_SINT7:
        case MSR_GIM_HV_SINT8:    case MSR_GIM_HV_SINT9:    case MSR_GIM_HV_SINT10:   case MSR_GIM_HV_SINT11:
        case MSR_GIM_HV_SINT12:   case MSR_GIM_HV_SINT13:   case MSR_GIM_HV_SINT14:   case MSR_GIM_HV_SINT15:
        {
            uint8_t      uVector    = MSR_GIM_HV_SINT_GET_VECTOR(uRawValue);
            bool const   fVMBusMsg  = RT_BOOL(idMsr == GIM_HV_VMBUS_MSG_SINT);
            size_t const idxSintMsr = idMsr - MSR_GIM_HV_SINT0;
            const char  *pszDesc    = fVMBusMsg ? "VMBus Message" : "Generic";
            if (uVector < GIM_HV_SINT_VECTOR_VALID_MIN)
            {
                LogRel(("GIM%u: HyperV: Programmed an invalid vector in SINT%u (%s), uVector=%u -> #GP(0)\n", pVCpu->idCpu,
                        idxSintMsr, pszDesc, uVector));
                return VERR_CPUM_RAISE_GP_0;
            }

            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            pHvCpu->auSintMsrs[idxSintMsr] = uRawValue;
            if (fVMBusMsg)
            {
                if (MSR_GIM_HV_SINT_IS_MASKED(uRawValue))
                    Log(("GIM%u: HyperV: Masked SINT%u (%s)\n", pVCpu->idCpu, idxSintMsr, pszDesc));
                else
                    Log(("GIM%u: HyperV: Unmasked SINT%u (%s), uVector=%u\n", pVCpu->idCpu, idxSintMsr, pszDesc, uVector));
            }
            Log(("GIM%u: HyperV: Written SINT%u=%#RX64\n", pVCpu->idCpu, idxSintMsr, uRawValue));
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SCONTROL:
        {
#ifndef IN_RING3
            /** @todo make this RZ later? */
            return VINF_CPUM_R3_MSR_WRITE;
#else
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            pHvCpu->uSControlMsr = uRawValue;
            if (MSR_GIM_HV_SCONTROL_IS_ENABLED(uRawValue))
                LogRel(("GIM%u: HyperV: Synthetic interrupt control enabled\n", pVCpu->idCpu));
            else
                LogRel(("GIM%u: HyperV: Synthetic interrupt control disabled\n", pVCpu->idCpu));
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_STIMER0_CONFIG:
        case MSR_GIM_HV_STIMER1_CONFIG:
        case MSR_GIM_HV_STIMER2_CONFIG:
        case MSR_GIM_HV_STIMER3_CONFIG:
        {
            PGIMHVCPU     pHvCpu    = &pVCpu->gim.s.u.HvCpu;
            uint8_t const idxStimer = (idMsr - MSR_GIM_HV_STIMER0_CONFIG) >> 1;

            /* Validate the writable bits. */
            if (RT_LIKELY(!(uRawValue & ~MSR_GIM_HV_STIMER_RW_VALID)))
            {
                Assert(idxStimer < RT_ELEMENTS(pHvCpu->aStimers));
                PGIMHVSTIMER pHvStimer = &pHvCpu->aStimers[idxStimer];

                /* Lock to prevent concurrent access from the timer callback. */
                int rc = TMTimerLock(pVM, pHvStimer->hTimer, VERR_IGNORED);
                if (rc == VINF_SUCCESS)
                {
                    /* Update the MSR value. */
                    pHvStimer->uStimerConfigMsr = uRawValue;
                    Log(("GIM%u: HyperV: Set STIMER_CONFIG%u=%#RX64\n", pVCpu->idCpu, idxStimer, uRawValue));

                    /* Process the MSR bits. */
                    if (   !MSR_GIM_HV_STIMER_GET_SINTX(uRawValue)   /* Writing SINTx as 0 causes the timer to be disabled. */
                        || !MSR_GIM_HV_STIMER_IS_ENABLED(uRawValue))
                    {
                        pHvStimer->uStimerConfigMsr &= ~MSR_GIM_HV_STIMER_ENABLE;
                        gimHvStopStimer(pVCpu, pHvStimer);
                        Log(("GIM%u: HyperV: Disabled STIMER_CONFIG%u\n", pVCpu->idCpu, idxStimer));
                    }
                    else if (MSR_GIM_HV_STIMER_IS_ENABLED(uRawValue))
                    {
                        /* Auto-enable implies writing to the STIMERx_COUNT MSR is what starts the timer. */
                        if (!MSR_GIM_HV_STIMER_IS_AUTO_ENABLED(uRawValue))
                        {
                            if (!TMTimerIsActive(pVM, pHvStimer->hTimer))
                            {
                                gimHvStartStimer(pVCpu, pHvStimer);
                                Log(("GIM%u: HyperV: Started STIMER%u\n", pVCpu->idCpu, idxStimer));
                            }
                            else
                            {
                                /*
                                 * Enabling a timer that's already enabled is undefined behaviour,
                                 * see Hyper-V spec. 15.3.1 "Synthetic Timer Configuration Register".
                                 *
                                 * Our implementation just re-starts the timer. Guests that comform to
                                 * the Hyper-V specs. should not be doing this anyway.
                                 */
                                AssertFailed();
                                gimHvStopStimer(pVCpu, pHvStimer);
                                gimHvStartStimer(pVCpu, pHvStimer);
                            }
                        }
                    }

                    TMTimerUnlock(pVM, pHvStimer->hTimer);
                }
                return rc;
            }
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            LogRel(("GIM%u: HyperV: Setting reserved bits of STIMER%u MSR (uRawValue=%#RX64) -> #GP(0)\n", pVCpu->idCpu,
                    idxStimer, uRawValue));
            return VERR_CPUM_RAISE_GP_0;
#endif
        }

        case MSR_GIM_HV_STIMER0_COUNT:
        case MSR_GIM_HV_STIMER1_COUNT:
        case MSR_GIM_HV_STIMER2_COUNT:
        case MSR_GIM_HV_STIMER3_COUNT:
        {
            PGIMHVCPU     pHvCpu    = &pVCpu->gim.s.u.HvCpu;
            uint8_t const idxStimer = (idMsr - MSR_GIM_HV_STIMER0_CONFIG) >> 1;
            Assert(idxStimer < RT_ELEMENTS(pHvCpu->aStimers));
            PGIMHVSTIMER  pHvStimer = &pHvCpu->aStimers[idxStimer];
            int const     rcBusy    = VINF_CPUM_R3_MSR_WRITE;

            /*
             * Writing zero to this MSR disables the timer regardless of whether the auto-enable
             * flag is set in the config MSR corresponding to the timer.
             */
            if (!uRawValue)
            {
                gimHvStopStimer(pVCpu, pHvStimer);
                pHvStimer->uStimerCountMsr = 0;
                Log(("GIM%u: HyperV: Set STIMER_COUNT%u=%RU64, stopped timer\n", pVCpu->idCpu, idxStimer, uRawValue));
                return VINF_SUCCESS;
            }

            /*
             * Concurrent writes to the config. MSR can't happen as it's serialized by way
             * of being done on the same EMT as this.
             */
            if (MSR_GIM_HV_STIMER_IS_AUTO_ENABLED(pHvStimer->uStimerConfigMsr))
            {
                int rc = TMTimerLock(pVM, pHvStimer->hTimer, rcBusy);
                if (rc == VINF_SUCCESS)
                {
                    pHvStimer->uStimerCountMsr = uRawValue;
                    gimHvStartStimer(pVCpu, pHvStimer);
                    TMTimerUnlock(pVM, pHvStimer->hTimer);
                    Log(("GIM%u: HyperV: Set STIMER_COUNT%u=%RU64 %RU64 msec, auto-started timer\n", pVCpu->idCpu, idxStimer,
                         uRawValue, (uRawValue * 100) / RT_NS_1MS_64));
                }
                return rc;
            }

            /* Simple update of the counter without any timer start/stop side-effects. */
            pHvStimer->uStimerCountMsr = uRawValue;
            Log(("GIM%u: HyperV: Set STIMER_COUNT%u=%RU64\n", pVCpu->idCpu, idxStimer, uRawValue));
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_EOM:
        {
            /** @todo implement EOM. */
            Log(("GIM%u: HyperV: EOM\n", pVCpu->idCpu));
            return VINF_SUCCESS;
        }

        case MSR_GIM_HV_SIEFP:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            pHvCpu->uSiefpMsr = uRawValue;
            if (MSR_GIM_HV_SIEF_PAGE_IS_ENABLED(uRawValue))
            {
                RTGCPHYS GCPhysSiefPage = MSR_GIM_HV_SIEF_GUEST_PFN(uRawValue) << GUEST_PAGE_SHIFT;
                if (PGMPhysIsGCPhysNormal(pVM, GCPhysSiefPage))
                {
                    int rc = gimR3HvEnableSiefPage(pVCpu, GCPhysSiefPage);
                    if (RT_SUCCESS(rc))
                    {
                        LogRel(("GIM%u: HyperV: Enabled synthetic interrupt event flags page at %#RGp\n", pVCpu->idCpu,
                                GCPhysSiefPage));
                        /** @todo SIEF setup. */
                        return VINF_SUCCESS;
                    }
                }
                else
                    LogRelMax(5, ("GIM%u: HyperV: SIEF page address %#RGp invalid!\n", pVCpu->idCpu, GCPhysSiefPage));
            }
            else
                gimR3HvDisableSiefPage(pVCpu);

            return VERR_CPUM_RAISE_GP_0;
#endif
            break;
        }

        case MSR_GIM_HV_SIMP:
        {
#ifndef IN_RING3
            return VINF_CPUM_R3_MSR_WRITE;
#else
            PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
            pHvCpu->uSimpMsr = uRawValue;
            if (MSR_GIM_HV_SIMP_IS_ENABLED(uRawValue))
            {
                RTGCPHYS GCPhysSimp = MSR_GIM_HV_SIMP_GPA(uRawValue);
                if (PGMPhysIsGCPhysNormal(pVM, GCPhysSimp))
                {
                    uint8_t abSimp[GIM_HV_PAGE_SIZE];
                    RT_ZERO(abSimp);
                    int rc2 = PGMPhysSimpleWriteGCPhys(pVM, GCPhysSimp, &abSimp[0], sizeof(abSimp));
                    if (RT_SUCCESS(rc2))
                        LogRel(("GIM%u: HyperV: Enabled synthetic interrupt message page at %#RGp\n", pVCpu->idCpu, GCPhysSimp));
                    else
                    {
                        LogRel(("GIM%u: HyperV: Failed to update synthetic interrupt message page at %#RGp. uSimpMsr=%#RX64 rc=%Rrc\n",
                                pVCpu->idCpu, pHvCpu->uSimpMsr, GCPhysSimp, rc2));
                        return VERR_CPUM_RAISE_GP_0;
                    }
                }
                else
                {
                    LogRel(("GIM%u: HyperV: Enabled synthetic interrupt message page at invalid address %#RGp\n", pVCpu->idCpu,
                            GCPhysSimp));
                }
            }
            else
                LogRel(("GIM%u: HyperV: Disabled synthetic interrupt message page\n", pVCpu->idCpu));
            return VINF_SUCCESS;
#endif
        }

        case MSR_GIM_HV_CRASH_P0:  pHv->uCrashP0Msr = uRawValue;  return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P1:  pHv->uCrashP1Msr = uRawValue;  return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P2:  pHv->uCrashP2Msr = uRawValue;  return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P3:  pHv->uCrashP3Msr = uRawValue;  return VINF_SUCCESS;
        case MSR_GIM_HV_CRASH_P4:  pHv->uCrashP4Msr = uRawValue;  return VINF_SUCCESS;

        case MSR_GIM_HV_TIME_REF_COUNT:     /* Read-only MSRs. */
        case MSR_GIM_HV_VP_INDEX:
        case MSR_GIM_HV_TSC_FREQ:
        case MSR_GIM_HV_APIC_FREQ:
            LogFunc(("WrMsr on read-only MSR %#RX32 -> #GP(0)\n", idMsr));
            break;

        case MSR_GIM_HV_DEBUG_OPTIONS_MSR:
        {
            if (pHv->fIsVendorMsHv)
            {
#ifndef IN_RING3
                return VINF_CPUM_R3_MSR_WRITE;
#else
                LogRelMax(5, ("GIM: HyperV: Write debug options MSR with %#RX64 ignored\n", uRawValue));
                return VINF_SUCCESS;
#endif
            }
            return VERR_CPUM_RAISE_GP_0;
        }

        default:
        {
#ifdef IN_RING3
            static uint32_t s_cTimes = 0;
            if (s_cTimes++ < 20)
                LogRel(("GIM: HyperV: Unknown/invalid WrMsr (%#x,%#x`%08x) -> #GP(0)\n", idMsr,
                        uRawValue & UINT64_C(0xffffffff00000000), uRawValue & UINT64_C(0xffffffff)));
            LogFunc(("Unknown/invalid WrMsr (%#RX32,%#RX64) -> #GP(0)\n", idMsr, uRawValue));
            break;
#else
            return VINF_CPUM_R3_MSR_WRITE;
#endif
        }
    }

    return VERR_CPUM_RAISE_GP_0;
}


/**
 * Whether we need to trap \#UD exceptions in the guest.
 *
 * We only needed to trap \#UD exceptions for the old raw-mode guests when
 * hypercalls are enabled. For HM VMs, the hypercall would be handled via the
 * VMCALL/VMMCALL VM-exit.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) gimHvShouldTrapXcptUD(PVMCPU pVCpu)
{
    RT_NOREF(pVCpu);
    return false;
}


/**
 * Checks the instruction and executes the hypercall if it's a valid hypercall
 * instruction.
 *
 * This interface is used by \#UD handlers and IEM.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   uDisOpcode  The disassembler opcode.
 * @param   cbInstr     The instruction length.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimHvHypercallEx(PVMCPUCC pVCpu, PCPUMCTX pCtx, unsigned uDisOpcode, uint8_t cbInstr)
{
    Assert(pVCpu);
    Assert(pCtx);
    VMCPU_ASSERT_EMT(pVCpu);

    PVM pVM = pVCpu->CTX_SUFF(pVM);
    CPUMCPUVENDOR const enmGuestCpuVendor = (CPUMCPUVENDOR)pVM->cpum.ro.GuestFeatures.enmCpuVendor;
    if (   (   uDisOpcode == OP_VMCALL
            && (   enmGuestCpuVendor == CPUMCPUVENDOR_INTEL
                || enmGuestCpuVendor == CPUMCPUVENDOR_VIA
                || enmGuestCpuVendor == CPUMCPUVENDOR_SHANGHAI))
        || (   uDisOpcode == OP_VMMCALL
            && (   enmGuestCpuVendor == CPUMCPUVENDOR_AMD
                || enmGuestCpuVendor == CPUMCPUVENDOR_HYGON)) )
        return gimHvHypercall(pVCpu, pCtx);

    RT_NOREF_PV(cbInstr);
    return VERR_GIM_INVALID_HYPERCALL_INSTR;
}


/**
 * Exception handler for \#UD.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VINF_GIM_HYPERCALL_CONTINUING continue hypercall without updating
 *          RIP.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_INVALID_HYPERCALL_INSTR instruction at RIP is not a valid
 *          hypercall instruction.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   pDis        Pointer to the disassembled instruction state at RIP.
 *                      Optional, can be NULL.
 * @param   pcbInstr    Where to store the instruction length of the hypercall
 *                      instruction. Optional, can be NULL.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimHvXcptUD(PVMCPUCC pVCpu, PCPUMCTX pCtx, PDISSTATE pDis, uint8_t *pcbInstr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * If we didn't ask for #UD to be trapped, bail.
     */
    if (!gimHvShouldTrapXcptUD(pVCpu))
        return VERR_GIM_IPE_1;

    if (!pDis)
    {
        /*
         * Disassemble the instruction at RIP to figure out if it's the Intel VMCALL instruction
         * or the AMD VMMCALL instruction and if so, handle it as a hypercall.
         */
        unsigned cbInstr;
        DISSTATE Dis;
        int rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbInstr);
        if (RT_SUCCESS(rc))
        {
            if (pcbInstr)
                *pcbInstr = (uint8_t)cbInstr;
            return gimHvHypercallEx(pVCpu, pCtx, Dis.pCurInstr->uOpcode, Dis.cbInstr);
        }

        Log(("GIM: HyperV: Failed to disassemble instruction at CS:RIP=%04x:%08RX64. rc=%Rrc\n", pCtx->cs.Sel, pCtx->rip, rc));
        return rc;
    }

    return gimHvHypercallEx(pVCpu, pCtx, pDis->pCurInstr->uOpcode, pDis->cbInstr);
}

