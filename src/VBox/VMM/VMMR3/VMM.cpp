/* $Id: VMM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - The Virtual Machine Monitor Core.
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

//#define NO_SUPCALLR0VMM

/** @page pg_vmm        VMM - The Virtual Machine Monitor
 *
 * The VMM component is two things at the moment, it's a component doing a few
 * management and routing tasks, and it's the whole virtual machine monitor
 * thing.  For hysterical reasons, it is not doing all the management that one
 * would expect, this is instead done by @ref pg_vm.  We'll address this
 * misdesign eventually, maybe.
 *
 * VMM is made up of these components:
 *  - @subpage pg_cfgm
 *  - @subpage pg_cpum
 *  - @subpage pg_dbgf
 *  - @subpage pg_em
 *  - @subpage pg_gim
 *  - @subpage pg_gmm
 *  - @subpage pg_gvmm
 *  - @subpage pg_hm
 *  - @subpage pg_iem
 *  - @subpage pg_iom
 *  - @subpage pg_mm
 *  - @subpage pg_nem
 *  - @subpage pg_pdm
 *  - @subpage pg_pgm
 *  - @subpage pg_selm
 *  - @subpage pg_ssm
 *  - @subpage pg_stam
 *  - @subpage pg_tm
 *  - @subpage pg_trpm
 *  - @subpage pg_vm
 *
 *
 * @see @ref grp_vmm @ref grp_vm @subpage pg_vmm_guideline @subpage pg_raw
 *
 *
 * @section sec_vmmstate        VMM State
 *
 * @image html VM_Statechart_Diagram.gif
 *
 * To be written.
 *
 *
 * @subsection  subsec_vmm_init     VMM Initialization
 *
 * To be written.
 *
 *
 * @subsection  subsec_vmm_term     VMM Termination
 *
 * To be written.
 *
 *
 * @section sec_vmm_limits     VMM Limits
 *
 * There are various resource limits imposed by the VMM and it's
 * sub-components.  We'll list some of them here.
 *
 * On 64-bit hosts:
 *      - Max 8191 VMs.  Imposed by GVMM's handle allocation (GVMM_MAX_HANDLES),
 *        can be increased up to 64K - 1.
 *      - Max 16TB - 64KB of the host memory can be used for backing VM RAM and
 *        ROM pages.  The limit is imposed by the 32-bit page ID used by GMM.
 *      - A VM can be assigned all the memory we can use (16TB), however, the
 *        Main API will restrict this to 2TB (MM_RAM_MAX_IN_MB).
 *      - Max 32 virtual CPUs (VMM_MAX_CPU_COUNT).
 *
 * On 32-bit hosts:
 *      - Max 127 VMs.  Imposed by GMM's per page structure.
 *      - Max 64GB - 64KB of the host memory can be used for backing VM RAM and
 *        ROM pages.  The limit is imposed by the 28-bit page ID used
 *        internally in GMM.  It is also limited by PAE.
 *      - A VM can be assigned all the memory GMM can allocate, however, the
 *        Main API will restrict this to 3584MB (MM_RAM_MAX_IN_MB).
 *      - Max 32 virtual CPUs (VMM_MAX_CPU_COUNT).
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmcritsectrw.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/nem.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/iem.h>
#endif
#include <VBox/vmm/iom.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/em.h>
#include <VBox/sup.h>
#include <VBox/vmm/dbgf.h>
#if defined(VBOX_VMM_TARGET_ARMV8)
# include <VBox/vmm/gic.h>
#else
# include <VBox/vmm/apic.h>
#endif
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/tm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/vmm/hm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#if defined(VBOX_VMM_TARGET_ARMV8)
# include <iprt/armv8.h>
#endif
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/ctype.h>
#include <iprt/x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The saved state version. */
#define VMM_SAVED_STATE_VERSION     4
/** The saved state version used by v3.0 and earlier. (Teleportation) */
#define VMM_SAVED_STATE_VERSION_3_0 3

/** Macro for flushing the ring-0 logging. */
#define VMM_FLUSH_R0_LOG(a_pVM, a_pVCpu, a_pLogger, a_pR3Logger) \
    do { \
        size_t const idxBuf = (a_pLogger)->idxBuf % VMMLOGGER_BUFFER_COUNT; \
        if (   (a_pLogger)->aBufs[idxBuf].AuxDesc.offBuf == 0 \
            || (a_pLogger)->aBufs[idxBuf].AuxDesc.fFlushedIndicator) \
        { /* likely? */ } \
        else \
            vmmR3LogReturnFlush(a_pVM, a_pVCpu, a_pLogger, idxBuf, a_pR3Logger); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void                 vmmR3InitRegisterStats(PVM pVM);
static DECLCALLBACK(int)    vmmR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)    vmmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
#if 0 /* pointless when timers doesn't run on EMT */
static DECLCALLBACK(void)   vmmR3YieldEMT(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser);
#endif
static VBOXSTRICTRC         vmmR3EmtRendezvousCommon(PVM pVM, PVMCPU pVCpu, bool fIsCaller,
                                                     uint32_t fFlags, PFNVMMEMTRENDEZVOUS pfnRendezvous, void *pvUser);
static int                  vmmR3HandleRing0Assert(PVM pVM, PVMCPU pVCpu);
static FNRTTHREAD           vmmR3LogFlusher;
static void                 vmmR3LogReturnFlush(PVM pVM, PVMCPU pVCpu, PVMMR3CPULOGGER pShared, size_t idxBuf,
                                                PRTLOGGER pDstLogger);
static DECLCALLBACK(void)   vmmR3InfoFF(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);



/**
 * Initializes the VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) VMMR3Init(PVM pVM)
{
    LogFlow(("VMMR3Init\n"));

    /*
     * Assert alignment, sizes and order.
     */
    AssertCompile(sizeof(pVM->vmm.s) <= sizeof(pVM->vmm.padding));
    AssertCompile(RT_SIZEOFMEMB(VMCPU, vmm.s) <= RT_SIZEOFMEMB(VMCPU, vmm.padding));

    /*
     * Init basic VM VMM members.
     */
    pVM->vmm.s.pahEvtRendezvousEnterOrdered     = NULL;
    pVM->vmm.s.hEvtRendezvousEnterOneByOne      = NIL_RTSEMEVENT;
    pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce  = NIL_RTSEMEVENTMULTI;
    pVM->vmm.s.hEvtMulRendezvousDone            = NIL_RTSEMEVENTMULTI;
    pVM->vmm.s.hEvtRendezvousDoneCaller         = NIL_RTSEMEVENT;
    pVM->vmm.s.hEvtMulRendezvousRecursionPush   = NIL_RTSEMEVENTMULTI;
    pVM->vmm.s.hEvtMulRendezvousRecursionPop    = NIL_RTSEMEVENTMULTI;
    pVM->vmm.s.hEvtRendezvousRecursionPushCaller = NIL_RTSEMEVENT;
    pVM->vmm.s.hEvtRendezvousRecursionPopCaller = NIL_RTSEMEVENT;
    pVM->vmm.s.nsProgramStart                   = RTTimeProgramStartNanoTS();

#if 0 /* pointless when timers doesn't run on EMT */
    /** @cfgm{/YieldEMTInterval, uint32_t, 1, UINT32_MAX, 23, ms}
     * The EMT yield interval.  The EMT yielding is a hack we employ to play a
     * bit nicer with the rest of the system (like for instance the GUI).
     */
    int rc = CFGMR3QueryU32Def(CFGMR3GetRoot(pVM), "YieldEMTInterval", &pVM->vmm.s.cYieldEveryMillies,
                               23 /* Value arrived at after experimenting with the grub boot prompt. */);
    AssertMsgRCReturn(rc, ("Configuration error. Failed to query \"YieldEMTInterval\", rc=%Rrc\n", rc), rc);
#endif

    /** @cfgm{/VMM/UsePeriodicPreemptionTimers, boolean, true}
     * Controls whether we employ per-cpu preemption timers to limit the time
     * spent executing guest code.  This option is not available on all
     * platforms and we will silently ignore this setting then.  If we are
     * running in VT-x mode, we will use the VMX-preemption timer instead of
     * this one when possible.
     */
    PCFGMNODE pCfgVMM = CFGMR3GetChild(CFGMR3GetRoot(pVM), "VMM");
    int rc = CFGMR3QueryBoolDef(pCfgVMM, "UsePeriodicPreemptionTimers", &pVM->vmm.s.fUsePeriodicPreemptionTimers, true);
    AssertMsgRCReturn(rc, ("Configuration error. Failed to query \"VMM/UsePeriodicPreemptionTimers\", rc=%Rrc\n", rc), rc);

    /*
     * Initialize the VMM rendezvous semaphores.
     */
    pVM->vmm.s.pahEvtRendezvousEnterOrdered = (PRTSEMEVENT)MMR3HeapAlloc(pVM, MM_TAG_VMM, sizeof(RTSEMEVENT) * pVM->cCpus);
    if (!pVM->vmm.s.pahEvtRendezvousEnterOrdered)
        return VERR_NO_MEMORY;
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
        pVM->vmm.s.pahEvtRendezvousEnterOrdered[i] = NIL_RTSEMEVENT;
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        rc = RTSemEventCreate(&pVM->vmm.s.pahEvtRendezvousEnterOrdered[i]);
        AssertRCReturn(rc, rc);
    }
    rc = RTSemEventCreate(&pVM->vmm.s.hEvtRendezvousEnterOneByOne);
    AssertRCReturn(rc, rc);
    rc = RTSemEventMultiCreate(&pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);
    AssertRCReturn(rc, rc);
    rc = RTSemEventMultiCreate(&pVM->vmm.s.hEvtMulRendezvousDone);
    AssertRCReturn(rc, rc);
    rc = RTSemEventCreate(&pVM->vmm.s.hEvtRendezvousDoneCaller);
    AssertRCReturn(rc, rc);
    rc = RTSemEventMultiCreate(&pVM->vmm.s.hEvtMulRendezvousRecursionPush);
    AssertRCReturn(rc, rc);
    rc = RTSemEventMultiCreate(&pVM->vmm.s.hEvtMulRendezvousRecursionPop);
    AssertRCReturn(rc, rc);
    rc = RTSemEventCreate(&pVM->vmm.s.hEvtRendezvousRecursionPushCaller);
    AssertRCReturn(rc, rc);
    rc = RTSemEventCreate(&pVM->vmm.s.hEvtRendezvousRecursionPopCaller);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state data unit.
     */
    rc = SSMR3RegisterInternal(pVM, "vmm", 1, VMM_SAVED_STATE_VERSION, VMM_STACK_SIZE + sizeof(RTGCPTR),
                               NULL, NULL, NULL,
                               NULL, vmmR3Save, NULL,
                               NULL, vmmR3Load, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the Ring-0 VM handle with the session for fast ioctl calls.
     */
    bool const fDriverless = SUPR3IsDriverless();
    if (!fDriverless)
    {
        rc = SUPR3SetVMForFastIOCtl(VMCC_GET_VMR0_FOR_CALL(pVM));
        if (RT_FAILURE(rc))
            return rc;
    }

#ifdef VBOX_WITH_NMI
    /*
     * Allocate mapping for the host APIC.
     */
    rc = MMR3HyperReserve(pVM, HOST_PAGE_SIZE, "Host APIC", &pVM->vmm.s.GCPtrApicBase);
    AssertRC(rc);
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Start the log flusher thread.
         */
        if (!fDriverless)
            rc = RTThreadCreate(&pVM->vmm.s.hLogFlusherThread, vmmR3LogFlusher, pVM, 0 /*cbStack*/,
                                RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "R0LogWrk");
        if (RT_SUCCESS(rc))
        {

            /*
             * Debug info and statistics.
             */
            DBGFR3InfoRegisterInternal(pVM, "fflags", "Displays the current Forced actions Flags.", vmmR3InfoFF);
            vmmR3InitRegisterStats(pVM);
            vmmInitFormatTypes();

            return VINF_SUCCESS;
        }
    }
    /** @todo Need failure cleanup? */

    return rc;
}


/**
 * VMMR3Init worker that register the statistics with STAM.
 *
 * @param   pVM         The cross context VM structure.
 */
static void vmmR3InitRegisterStats(PVM pVM)
{
    RT_NOREF_PV(pVM);

    /* Nothing to do here in driverless mode. */
    if (SUPR3IsDriverless())
        return;

    /*
     * Statistics.
     */
    STAM_REG(pVM, &pVM->vmm.s.StatRunGC,                    STAMTYPE_COUNTER, "/VMM/RunGC",                     STAMUNIT_OCCURENCES, "Number of context switches.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetNormal,              STAMTYPE_COUNTER, "/VMM/RZRet/Normal",              STAMUNIT_OCCURENCES, "Number of VINF_SUCCESS returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterrupt,           STAMTYPE_COUNTER, "/VMM/RZRet/Interrupt",           STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterruptHyper,      STAMTYPE_COUNTER, "/VMM/RZRet/InterruptHyper",      STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT_HYPER returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetGuestTrap,           STAMTYPE_COUNTER, "/VMM/RZRet/GuestTrap",           STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_GUEST_TRAP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRingSwitch,          STAMTYPE_COUNTER, "/VMM/RZRet/RingSwitch",          STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_RING_SWITCH returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRingSwitchInt,       STAMTYPE_COUNTER, "/VMM/RZRet/RingSwitchInt",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_RING_SWITCH_INT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetStaleSelector,       STAMTYPE_COUNTER, "/VMM/RZRet/StaleSelector",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_STALE_SELECTOR returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIRETTrap,            STAMTYPE_COUNTER, "/VMM/RZRet/IRETTrap",            STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_IRET_TRAP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetEmulate,             STAMTYPE_COUNTER, "/VMM/RZRet/Emulate",             STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchEmulate,        STAMTYPE_COUNTER, "/VMM/RZRet/PatchEmulate",        STAMUNIT_OCCURENCES, "Number of VINF_PATCH_EMULATE_INSTR returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIORead,              STAMTYPE_COUNTER, "/VMM/RZRet/IORead",              STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_IOPORT_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIOWrite,             STAMTYPE_COUNTER, "/VMM/RZRet/IOWrite",             STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_IOPORT_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIOCommitWrite,       STAMTYPE_COUNTER, "/VMM/RZRet/IOCommitWrite",       STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_IOPORT_COMMIT_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIORead,            STAMTYPE_COUNTER, "/VMM/RZRet/MMIORead",            STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_MMIO_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOWrite,           STAMTYPE_COUNTER, "/VMM/RZRet/MMIOWrite",           STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_MMIO_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOCommitWrite,     STAMTYPE_COUNTER, "/VMM/RZRet/MMIOCommitWrite",     STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_MMIO_COMMIT_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOReadWrite,       STAMTYPE_COUNTER, "/VMM/RZRet/MMIOReadWrite",       STAMUNIT_OCCURENCES, "Number of VINF_IOM_R3_MMIO_READ_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOPatchRead,       STAMTYPE_COUNTER, "/VMM/RZRet/MMIOPatchRead",       STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_PATCH_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOPatchWrite,      STAMTYPE_COUNTER, "/VMM/RZRet/MMIOPatchWrite",      STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_PATCH_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMSRRead,             STAMTYPE_COUNTER, "/VMM/RZRet/MSRRead",             STAMUNIT_OCCURENCES, "Number of VINF_CPUM_R3_MSR_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMSRWrite,            STAMTYPE_COUNTER, "/VMM/RZRet/MSRWrite",            STAMUNIT_OCCURENCES, "Number of VINF_CPUM_R3_MSR_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetLDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/LDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_GDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetGDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/GDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_LDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/IDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_IDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetTSSFault,            STAMTYPE_COUNTER, "/VMM/RZRet/TSSFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_TSS_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetCSAMTask,            STAMTYPE_COUNTER, "/VMM/RZRet/CSAMTask",            STAMUNIT_OCCURENCES, "Number of VINF_CSAM_PENDING_ACTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetSyncCR3,             STAMTYPE_COUNTER, "/VMM/RZRet/SyncCR",              STAMUNIT_OCCURENCES, "Number of VINF_PGM_SYNC_CR3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMisc,                STAMTYPE_COUNTER, "/VMM/RZRet/Misc",                STAMUNIT_OCCURENCES, "Number of misc returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchInt3,           STAMTYPE_COUNTER, "/VMM/RZRet/PatchInt3",           STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_INT3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchPF,             STAMTYPE_COUNTER, "/VMM/RZRet/PatchPF",             STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_TRAP_PF returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchGP,             STAMTYPE_COUNTER, "/VMM/RZRet/PatchGP",             STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_TRAP_GP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchIretIRQ,        STAMTYPE_COUNTER, "/VMM/RZRet/PatchIret",           STAMUNIT_OCCURENCES, "Number of VINF_PATM_PENDING_IRQ_AFTER_IRET returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRescheduleREM,       STAMTYPE_COUNTER, "/VMM/RZRet/ScheduleREM",         STAMUNIT_OCCURENCES, "Number of VINF_EM_RESCHEDULE_REM returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Total,           STAMTYPE_COUNTER, "/VMM/RZRet/ToR3",                STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Unknown,         STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/Unknown",        STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns without responsible force flag.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3FF,              STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/ToR3",           STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VMCPU_FF_TO_R3.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3TMVirt,          STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/TMVirt",         STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VM_FF_TM_VIRTUAL_SYNC.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3HandyPages,      STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/Handy",          STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VM_FF_PGM_NEED_HANDY_PAGES.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3PDMQueues,       STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/PDMQueue",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VM_FF_PDM_QUEUES.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Rendezvous,      STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/Rendezvous",     STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VM_FF_EMT_RENDEZVOUS.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Timer,           STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/Timer",          STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VMCPU_FF_TIMER.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3DMA,             STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/DMA",            STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VM_FF_PDM_DMA.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3CritSect,        STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/CritSect",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VMCPU_FF_PDM_CRITSECT.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Iem,             STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/IEM",            STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VMCPU_FF_IEM.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3Iom,             STAMTYPE_COUNTER, "/VMM/RZRet/ToR3/IOM",            STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns with VMCPU_FF_IOM.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetTimerPending,        STAMTYPE_COUNTER, "/VMM/RZRet/TimerPending",        STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TIMER_PENDING returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterruptPending,    STAMTYPE_COUNTER, "/VMM/RZRet/InterruptPending",    STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT_PENDING returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPATMDuplicateFn,     STAMTYPE_COUNTER, "/VMM/RZRet/PATMDuplicateFn",     STAMUNIT_OCCURENCES, "Number of VINF_PATM_DUPLICATE_FUNCTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPGMFlushPending,     STAMTYPE_COUNTER, "/VMM/RZRet/PGMFlushPending",     STAMUNIT_OCCURENCES, "Number of VINF_PGM_POOL_FLUSH_PENDING returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPendingRequest,      STAMTYPE_COUNTER, "/VMM/RZRet/PendingRequest",      STAMUNIT_OCCURENCES, "Number of VINF_EM_PENDING_REQUEST returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchTPR,            STAMTYPE_COUNTER, "/VMM/RZRet/PatchTPR",            STAMUNIT_OCCURENCES, "Number of VINF_EM_HM_PATCH_TPR_INSTR returns.");

    STAMR3Register(pVM, &pVM->vmm.s.StatLogFlusherFlushes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, "/VMM/LogFlush/00-Flushes",  STAMUNIT_OCCURENCES, "Total number of buffer flushes");
    STAMR3Register(pVM, &pVM->vmm.s.StatLogFlusherNoWakeUp, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, "/VMM/LogFlush/00-NoWakups", STAMUNIT_OCCURENCES, "Times the flusher thread didn't need waking up.");

    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltBlock,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "", "/PROF/CPU%u/VM/Halt/R0HaltBlock", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltBlockOnTime,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "", "/PROF/CPU%u/VM/Halt/R0HaltBlockOnTime", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltBlockOverslept, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "", "/PROF/CPU%u/VM/Halt/R0HaltBlockOverslept", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltBlockInsomnia,  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "", "/PROF/CPU%u/VM/Halt/R0HaltBlockInsomnia", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltExec,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltExec", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltExecFromSpin,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltExec/FromSpin", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltExecFromBlock,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltExec/FromBlock", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3FromSpin,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/FromSpin", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3Other,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/Other", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3PendingFF,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/PendingFF", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3SmallDelta, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/SmallDelta", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3PostNoInt,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/PostWaitNoInt", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.StatR0HaltToR3PostPendingFF,STAMTYPE_COUNTER,STAMVISIBILITY_ALWAYS,STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltToR3/PostWaitPendingFF", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.cR0Halts,                 STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltHistoryCounter", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.cR0HaltsSucceeded,        STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltHistorySucceeded", i);
        STAMR3RegisterF(pVM, &pVCpu->vmm.s.cR0HaltsToRing3,          STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/PROF/CPU%u/VM/Halt/R0HaltHistoryToRing3", i);

        STAMR3RegisterF(pVM, &pVCpu->cEmtHashCollisions,             STAMTYPE_U8,      STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,  "", "/VMM/EmtHashCollisions/Emt%02u", i);

        PVMMR3CPULOGGER pShared = &pVCpu->vmm.s.u.s.Logger;
        STAMR3RegisterF(pVM, &pShared->StatFlushes,     STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Reg", i);
        STAMR3RegisterF(pVM, &pShared->StatCannotBlock, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Reg/CannotBlock", i);
        STAMR3RegisterF(pVM, &pShared->StatWait,        STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, "", "/VMM/LogFlush/CPU%u/Reg/Wait", i);
        STAMR3RegisterF(pVM, &pShared->StatRaces,       STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, "", "/VMM/LogFlush/CPU%u/Reg/Races", i);
        STAMR3RegisterF(pVM, &pShared->StatRacesToR0,   STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Reg/RacesToR0", i);
        STAMR3RegisterF(pVM, &pShared->cbDropped,       STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Reg/cbDropped", i);
        STAMR3RegisterF(pVM, &pShared->cbBuf,           STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Reg/cbBuf", i);
        STAMR3RegisterF(pVM, &pShared->idxBuf,          STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Reg/idxBuf", i);

        pShared = &pVCpu->vmm.s.u.s.RelLogger;
        STAMR3RegisterF(pVM, &pShared->StatFlushes,     STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Rel", i);
        STAMR3RegisterF(pVM, &pShared->StatCannotBlock, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Rel/CannotBlock", i);
        STAMR3RegisterF(pVM, &pShared->StatWait,        STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, "", "/VMM/LogFlush/CPU%u/Rel/Wait", i);
        STAMR3RegisterF(pVM, &pShared->StatRaces,       STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, "", "/VMM/LogFlush/CPU%u/Rel/Races", i);
        STAMR3RegisterF(pVM, &pShared->StatRacesToR0,   STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,     "", "/VMM/LogFlush/CPU%u/Rel/RacesToR0", i);
        STAMR3RegisterF(pVM, &pShared->cbDropped,       STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Rel/cbDropped", i);
        STAMR3RegisterF(pVM, &pShared->cbBuf,           STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Rel/cbBuf", i);
        STAMR3RegisterF(pVM, &pShared->idxBuf,          STAMTYPE_U32,     STAMVISIBILITY_USED, STAMUNIT_BYTES,          "", "/VMM/LogFlush/CPU%u/Rel/idxBuf", i);
    }
}


/**
 * Worker for VMMR3InitR0 that calls ring-0 to do EMT specific initialization.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per CPU structure.
 * @thread  EMT(pVCpu)
 */
static DECLCALLBACK(int) vmmR3InitR0Emt(PVM pVM, PVMCPU pVCpu)
{
    return VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_VMMR0_INIT_EMT, 0, NULL);
}


/**
 * Initializes the R0 VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) VMMR3InitR0(PVM pVM)
{
    int    rc;
    PVMCPU pVCpu = VMMGetCpu(pVM);
    Assert(pVCpu && pVCpu->idCpu == 0);

    /*
     * Nothing to do here in driverless mode.
     */
    if (SUPR3IsDriverless())
        return VINF_SUCCESS;

    /*
     * Make sure the ring-0 loggers are up to date.
     */
    rc = VMMR3UpdateLoggers(pVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Call Ring-0 entry with init code.
     */
#ifdef NO_SUPCALLR0VMM
    //rc = VERR_GENERAL_FAILURE;
    rc = VINF_SUCCESS;
#else
    rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), 0 /*idCpu*/, VMMR0_DO_VMMR0_INIT, RT_MAKE_U64(VMMGetSvnRev(), vmmGetBuildType()), NULL);
#endif

    /*
     * Flush the logs & deal with assertions.
     */
#ifdef LOG_ENABLED
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.Logger, NULL);
#endif
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.RelLogger, RTLogRelGetDefaultInstance());
    if (rc == VERR_VMM_RING0_ASSERTION)
        rc = vmmR3HandleRing0Assert(pVM, pVCpu);
    if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
    {
        LogRel(("VMM: R0 init failed, rc=%Rra\n", rc));
        if (RT_SUCCESS(rc))
            rc = VERR_IPE_UNEXPECTED_INFO_STATUS;
    }

    /*
     * Log stuff we learned in ring-0.
     */
    /* Log whether thread-context hooks are used (on Linux this can depend on how the kernel is configured). */
    if (pVM->vmm.s.fIsUsingContextHooks)
        LogRel(("VMM: Enabled thread-context hooks\n"));
    else
        LogRel(("VMM: Thread-context hooks unavailable\n"));

    /* Log RTThreadPreemptIsPendingTrusty() and RTThreadPreemptIsPossible() results. */
    if (pVM->vmm.s.fIsPreemptPendingApiTrusty)
        LogRel(("VMM: RTThreadPreemptIsPending() can be trusted\n"));
    else
        LogRel(("VMM: Warning! RTThreadPreemptIsPending() cannot be trusted!  Need to update kernel info?\n"));
    if (pVM->vmm.s.fIsPreemptPossible)
        LogRel(("VMM: Kernel preemption is possible\n"));
    else
        LogRel(("VMM: Kernel preemption is not possible it seems\n"));

    /*
     * Send all EMTs to ring-0 to get their logger initialized.
     */
    for (VMCPUID idCpu = 0; RT_SUCCESS(rc) && idCpu < pVM->cCpus; idCpu++)
        rc = VMR3ReqCallWait(pVM, idCpu, (PFNRT)vmmR3InitR0Emt, 2, pVM, pVM->apCpusR3[idCpu]);

    return rc;
}


/**
 * Called when an init phase completes.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             Which init phase.
 */
VMMR3_INT_DECL(int) VMMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    int rc = VINF_SUCCESS;

    switch (enmWhat)
    {
        case VMINITCOMPLETED_RING3:
        {
#if 0 /* pointless when timers doesn't run on EMT */
            /*
             * Create the EMT yield timer.
             */
            rc = TMR3TimerCreate(pVM, TMCLOCK_REAL, vmmR3YieldEMT, NULL, TMTIMER_FLAGS_NO_RING0,
                                 "EMT Yielder", &pVM->vmm.s.hYieldTimer);
            AssertRCReturn(rc, rc);

            rc = TMTimerSetMillies(pVM, pVM->vmm.s.hYieldTimer, pVM->vmm.s.cYieldEveryMillies);
            AssertRCReturn(rc, rc);
#endif
            break;
        }

        case VMINITCOMPLETED_HM:
        {
#if !defined(VBOX_VMM_TARGET_ARMV8)
            /*
             * Disable the periodic preemption timers if we can use the
             * VMX-preemption timer instead.
             */
            if (   pVM->vmm.s.fUsePeriodicPreemptionTimers
                && HMR3IsVmxPreemptionTimerUsed(pVM))
                pVM->vmm.s.fUsePeriodicPreemptionTimers = false;
            LogRel(("VMM: fUsePeriodicPreemptionTimers=%RTbool\n", pVM->vmm.s.fUsePeriodicPreemptionTimers));
#endif

            /*
             * Last chance for GIM to update its CPUID leaves if it requires
             * knowledge/information from HM initialization.
             */
/** @todo r=bird: This shouldn't be done from here, but rather from VM.cpp. There is no dependency on VMM here. */
            rc = GIMR3InitCompleted(pVM);
            AssertRCReturn(rc, rc);

            /*
             * CPUM's post-initialization (print CPUIDs).
             */
            CPUMR3LogCpuIdAndMsrFeatures(pVM);
            break;
        }

        default: /* shuts up gcc */
            break;
    }

    return rc;
}


/**
 * Terminate the VMM bits.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) VMMR3Term(PVM pVM)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    Assert(pVCpu && pVCpu->idCpu == 0);

    /*
     * Call Ring-0 entry with termination code.
     */
    int rc = VINF_SUCCESS;
    if (!SUPR3IsDriverless())
    {
#ifndef NO_SUPCALLR0VMM
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), 0 /*idCpu*/, VMMR0_DO_VMMR0_TERM, 0, NULL);
#endif
    }

    /*
     * Flush the logs & deal with assertions.
     */
#ifdef LOG_ENABLED
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.Logger, NULL);
#endif
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.RelLogger, RTLogRelGetDefaultInstance());
    if (rc == VERR_VMM_RING0_ASSERTION)
        rc = vmmR3HandleRing0Assert(pVM, pVCpu);
    if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
    {
        LogRel(("VMM: VMMR3Term: R0 term failed, rc=%Rra. (warning)\n", rc));
        if (RT_SUCCESS(rc))
            rc = VERR_IPE_UNEXPECTED_INFO_STATUS;
    }

    /*
     * Do clean ups.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        RTSemEventDestroy(pVM->vmm.s.pahEvtRendezvousEnterOrdered[i]);
        pVM->vmm.s.pahEvtRendezvousEnterOrdered[i] = NIL_RTSEMEVENT;
    }
    RTSemEventDestroy(pVM->vmm.s.hEvtRendezvousEnterOneByOne);
    pVM->vmm.s.hEvtRendezvousEnterOneByOne = NIL_RTSEMEVENT;
    RTSemEventMultiDestroy(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);
    pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce = NIL_RTSEMEVENTMULTI;
    RTSemEventMultiDestroy(pVM->vmm.s.hEvtMulRendezvousDone);
    pVM->vmm.s.hEvtMulRendezvousDone = NIL_RTSEMEVENTMULTI;
    RTSemEventDestroy(pVM->vmm.s.hEvtRendezvousDoneCaller);
    pVM->vmm.s.hEvtRendezvousDoneCaller = NIL_RTSEMEVENT;
    RTSemEventMultiDestroy(pVM->vmm.s.hEvtMulRendezvousRecursionPush);
    pVM->vmm.s.hEvtMulRendezvousRecursionPush = NIL_RTSEMEVENTMULTI;
    RTSemEventMultiDestroy(pVM->vmm.s.hEvtMulRendezvousRecursionPop);
    pVM->vmm.s.hEvtMulRendezvousRecursionPop = NIL_RTSEMEVENTMULTI;
    RTSemEventDestroy(pVM->vmm.s.hEvtRendezvousRecursionPushCaller);
    pVM->vmm.s.hEvtRendezvousRecursionPushCaller = NIL_RTSEMEVENT;
    RTSemEventDestroy(pVM->vmm.s.hEvtRendezvousRecursionPopCaller);
    pVM->vmm.s.hEvtRendezvousRecursionPopCaller = NIL_RTSEMEVENT;

    vmmTermFormatTypes();

    /*
     * Wait for the log flusher thread to complete.
     */
    if (pVM->vmm.s.hLogFlusherThread != NIL_RTTHREAD)
    {
        int rc2 = RTThreadWait(pVM->vmm.s.hLogFlusherThread, RT_MS_30SEC, NULL);
        AssertLogRelRC(rc2);
        if (RT_SUCCESS(rc2))
            pVM->vmm.s.hLogFlusherThread = NIL_RTTHREAD;
    }

    return rc;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The VMM will need to apply relocations to the core code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   offDelta    The relocation delta.
 */
VMMR3_INT_DECL(void) VMMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    LogFlow(("VMMR3Relocate: offDelta=%RGv\n", offDelta));
    RT_NOREF(offDelta);

    /*
     * Update the logger.
     */
    VMMR3UpdateLoggers(pVM);
}


/**
 * Worker for VMMR3UpdateLoggers.
 */
static int vmmR3UpdateLoggersWorker(PVM pVM, PVMCPU pVCpu, PRTLOGGER pSrcLogger, bool fReleaseLogger)
{
    /*
     * Get the group count.
     */
    uint32_t uGroupsCrc32 = 0;
    uint32_t cGroups      = 0;
    uint64_t fFlags       = 0;
    int rc = RTLogQueryBulk(pSrcLogger, &fFlags, &uGroupsCrc32, &cGroups, NULL);
    Assert(rc == VERR_BUFFER_OVERFLOW);

    /*
     * Allocate the request of the right size.
     */
    uint32_t const         cbReq = RT_UOFFSETOF_DYN(VMMR0UPDATELOGGERSREQ, afGroups[cGroups]);
    PVMMR0UPDATELOGGERSREQ pReq  = (PVMMR0UPDATELOGGERSREQ)RTMemAllocZVar(cbReq);
    if (pReq)
    {
        pReq->Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        pReq->Hdr.cbReq    = cbReq;
        pReq->cGroups      = cGroups;
        rc = RTLogQueryBulk(pSrcLogger, &pReq->fFlags, &pReq->uGroupCrc32, &pReq->cGroups, pReq->afGroups);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            /*
             * The 64-bit value argument.
             */
            uint64_t fExtraArg = fReleaseLogger;

            /* Only outputting to the parent VMM's logs? Enable ring-0 to flush directly. */
            uint32_t fDst = RTLogGetDestinations(pSrcLogger);
            fDst &= ~(RTLOGDEST_DUMMY | RTLOGDEST_F_NO_DENY | RTLOGDEST_F_DELAY_FILE | RTLOGDEST_FIXED_FILE | RTLOGDEST_FIXED_DIR);
            if (   (fDst & (RTLOGDEST_VMM | RTLOGDEST_VMM_REL))
                && !(fDst & ~(RTLOGDEST_VMM | RTLOGDEST_VMM_REL)))
                fExtraArg |= (fDst & RTLOGDEST_VMM     ? VMMR0UPDATELOGGER_F_TO_PARENT_VMM_DBG : 0)
                          |  (fDst & RTLOGDEST_VMM_REL ? VMMR0UPDATELOGGER_F_TO_PARENT_VMM_REL : 0);

            rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_VMMR0_UPDATE_LOGGERS, fExtraArg, &pReq->Hdr);
        }

        RTMemFree(pReq);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Updates the settings for the RC and R0 loggers.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @thread  EMT
 */
VMMR3_INT_DECL(int) VMMR3UpdateLoggers(PVM pVM)
{
    /* Nothing to do here if we're in driverless mode: */
    if (SUPR3IsDriverless())
        return VINF_SUCCESS;

    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    /*
     * Each EMT has each own logger instance.
     */
    /* Debug logging.*/
    int rcDebug = VINF_SUCCESS;
#ifdef LOG_ENABLED
    PRTLOGGER const pDefault = RTLogDefaultInstance();
    if (pDefault)
        rcDebug = vmmR3UpdateLoggersWorker(pVM, pVCpu, pDefault, false /*fReleaseLogger*/);
#else
    RT_NOREF(pVM);
#endif

    /* Release logging. */
    int rcRelease = VINF_SUCCESS;
    PRTLOGGER const pRelease = RTLogRelGetDefaultInstance();
    if (pRelease)
        rcRelease = vmmR3UpdateLoggersWorker(pVM, pVCpu, pRelease, true /*fReleaseLogger*/);

    return RT_SUCCESS(rcDebug) ? rcRelease : rcDebug;
}


/**
 * @callback_method_impl{FNRTTHREAD, Ring-0 log flusher thread.}
 */
static DECLCALLBACK(int) vmmR3LogFlusher(RTTHREAD hThreadSelf, void *pvUser)
{
    PVM const pVM = (PVM)pvUser;
    RT_NOREF(hThreadSelf);

    /* Reset the flusher state before we start: */
    pVM->vmm.s.LogFlusherItem.u32 = UINT32_MAX;

    /*
     * The work loop.
     */
    for (;;)
    {
        /*
         * Wait for work.
         */
        int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_VMMR0_LOG_FLUSHER, 0, NULL);
        if (RT_SUCCESS(rc))
        {
            /* Paranoia: Make another copy of the request, to make sure the validated data can't be changed. */
            VMMLOGFLUSHERENTRY Item;
            Item.u32 = pVM->vmm.s.LogFlusherItem.u32;
            if (   Item.s.idCpu     < pVM->cCpus
                && Item.s.idxLogger < VMMLOGGER_IDX_MAX
                && Item.s.idxBuffer < VMMLOGGER_BUFFER_COUNT)
            {
                /*
                 * Verify the request.
                 */
                PVMCPU const          pVCpu     = pVM->apCpusR3[Item.s.idCpu];
                PVMMR3CPULOGGER const pShared   = &pVCpu->vmm.s.u.aLoggers[Item.s.idxLogger];
                uint32_t const        cbToFlush = pShared->aBufs[Item.s.idxBuffer].AuxDesc.offBuf;
                if (cbToFlush > 0)
                {
                    if (cbToFlush <= pShared->cbBuf)
                    {
                        char * const pchBufR3 = pShared->aBufs[Item.s.idxBuffer].pchBufR3;
                        if (pchBufR3)
                        {
                            /*
                             * Do the flushing.
                             */
                            PRTLOGGER const pLogger = Item.s.idxLogger == VMMLOGGER_IDX_REGULAR
                                                    ? RTLogGetDefaultInstance() : RTLogRelGetDefaultInstance();
                            if (pLogger)
                            {
                                char szBefore[128];
                                RTStrPrintf(szBefore, sizeof(szBefore),
                                            "*FLUSH* idCpu=%u idxLogger=%u idxBuffer=%u cbToFlush=%#x fFlushed=%RTbool cbDropped=%#x\n",
                                            Item.s.idCpu, Item.s.idxLogger, Item.s.idxBuffer, cbToFlush,
                                            pShared->aBufs[Item.s.idxBuffer].AuxDesc.fFlushedIndicator, pShared->cbDropped);
                                RTLogBulkWrite(pLogger, szBefore, pchBufR3, cbToFlush, "*FLUSH DONE*\n");
                            }
                        }
                        else
                            Log(("vmmR3LogFlusher: idCpu=%u idxLogger=%u idxBuffer=%u cbToFlush=%#x: Warning! No ring-3 buffer pointer!\n",
                                 Item.s.idCpu, Item.s.idxLogger, Item.s.idxBuffer, cbToFlush));
                    }
                    else
                        Log(("vmmR3LogFlusher: idCpu=%u idxLogger=%u idxBuffer=%u cbToFlush=%#x: Warning! Exceeds %#x bytes buffer size!\n",
                             Item.s.idCpu, Item.s.idxLogger, Item.s.idxBuffer, cbToFlush, pShared->cbBuf));
                }
                else
                    Log(("vmmR3LogFlusher: idCpu=%u idxLogger=%u idxBuffer=%u cbToFlush=%#x: Warning! Zero bytes to flush!\n",
                         Item.s.idCpu, Item.s.idxLogger, Item.s.idxBuffer, cbToFlush));

                /*
                 * Mark the descriptor as flushed and set the request flag for same.
                 */
                pShared->aBufs[Item.s.idxBuffer].AuxDesc.fFlushedIndicator = true;
            }
            else
            {
                Assert(Item.s.idCpu     == UINT16_MAX);
                Assert(Item.s.idxLogger == UINT8_MAX);
                Assert(Item.s.idxBuffer == UINT8_MAX);
            }
        }
        /*
         * Interrupted can happen, just ignore it.
         */
        else if (rc == VERR_INTERRUPTED)
        { /* ignore*/ }
        /*
         * The ring-0 termination code will set the shutdown flag and wake us
         * up, and we should return with object destroyed.  In case there is
         * some kind of race, we might also get sempahore destroyed.
         */
        else if (   rc == VERR_OBJECT_DESTROYED
                 || rc == VERR_SEM_DESTROYED
                 || rc == VERR_INVALID_HANDLE)
        {
            LogRel(("vmmR3LogFlusher: Terminating (%Rrc)\n", rc));
            return VINF_SUCCESS;
        }
        /*
         * There shouldn't be any other errors...
         */
        else
        {
            LogRelMax(64, ("vmmR3LogFlusher: VMMR0_DO_VMMR0_LOG_FLUSHER -> %Rrc\n", rc));
            AssertRC(rc);
            RTThreadSleep(1);
        }
    }
}


/**
 * Helper for VMM_FLUSH_R0_LOG that does the flushing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      EMT.
 * @param   pShared     The shared logger data.
 * @param   idxBuf      The buffer to flush.
 * @param   pDstLogger  The destination IPRT logger.
 */
static void vmmR3LogReturnFlush(PVM pVM, PVMCPU pVCpu, PVMMR3CPULOGGER pShared, size_t idxBuf, PRTLOGGER pDstLogger)
{
    uint32_t const cbToFlush = pShared->aBufs[idxBuf].AuxDesc.offBuf;
    const char    *pszBefore = cbToFlush < 256 ? NULL : "*FLUSH*\n";
    const char    *pszAfter  = cbToFlush < 256 ? NULL : "*END*\n";

#if VMMLOGGER_BUFFER_COUNT > 1
    /*
     * When we have more than one log buffer, the flusher thread may still be
     * working on the previous buffer when we get here.
     */
    char szBefore[64];
    if (pShared->cFlushing > 0)
    {
        STAM_REL_PROFILE_START(&pShared->StatRaces, a);
        uint64_t const nsStart = RTTimeNanoTS();

        /* A no-op, but it takes the lock and the hope is that we end up waiting
           on the flusher to finish up. */
        RTLogBulkWrite(pDstLogger, NULL, "", 0, NULL);
        if (pShared->cFlushing != 0)
        {
            RTLogBulkWrite(pDstLogger, NULL, "", 0, NULL);

            /* If no luck, go to ring-0 and to proper waiting. */
            if (pShared->cFlushing != 0)
            {
                STAM_REL_COUNTER_INC(&pShared->StatRacesToR0);
                SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), pVCpu->idCpu, VMMR0_DO_VMMR0_LOG_WAIT_FLUSHED, 0, NULL);
            }
        }

        RTStrPrintf(szBefore, sizeof(szBefore), "*%sFLUSH* waited %'RU64 ns\n",
                    pShared->cFlushing == 0 ? "" : " MISORDERED",  RTTimeNanoTS() - nsStart);
        pszBefore = szBefore;
        STAM_REL_PROFILE_STOP(&pShared->StatRaces, a);
    }
#else
    RT_NOREF(pVM, pVCpu);
#endif

    RTLogBulkWrite(pDstLogger, pszBefore, pShared->aBufs[idxBuf].pchBufR3, cbToFlush, pszAfter);
    pShared->aBufs[idxBuf].AuxDesc.fFlushedIndicator = true;
}


/**
 * Gets the pointer to a buffer containing the R0/RC RTAssertMsg1Weak output.
 *
 * @returns Pointer to the buffer.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(const char *) VMMR3GetRZAssertMsg1(PVM pVM)
{
    return pVM->vmm.s.szRing0AssertMsg1;
}


/**
 * Returns the VMCPU of the specified virtual CPU.
 *
 * @returns The VMCPU pointer. NULL if @a idCpu or @a pUVM is invalid.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the virtual CPU.
 */
VMMR3DECL(PVMCPU) VMMR3GetCpuByIdU(PUVM pUVM, RTCPUID idCpu)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    AssertReturn(idCpu < pUVM->cCpus, NULL);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, NULL);
    return pUVM->pVM->apCpusR3[idCpu];
}


/**
 * Gets the pointer to a buffer containing the R0/RC RTAssertMsg2Weak output.
 *
 * @returns Pointer to the buffer.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(const char *) VMMR3GetRZAssertMsg2(PVM pVM)
{
    return pVM->vmm.s.szRing0AssertMsg2;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) vmmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    LogFlow(("vmmR3Save:\n"));

    /*
     * Save the started/stopped state of all CPUs except 0 as it will always
     * be running. This avoids breaking the saved state version. :-)
     */
    for (VMCPUID i = 1; i < pVM->cCpus; i++)
        SSMR3PutBool(pSSM, VMCPUSTATE_IS_STARTED(VMCPU_GET_STATE(pVM->apCpusR3[i])));

    return SSMR3PutU32(pSSM, UINT32_MAX); /* terminator */
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        Data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) vmmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    LogFlow(("vmmR3Load:\n"));
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Validate version.
     */
    if (    uVersion != VMM_SAVED_STATE_VERSION
        &&  uVersion != VMM_SAVED_STATE_VERSION_3_0)
    {
        AssertMsgFailed(("vmmR3Load: Invalid version uVersion=%u!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uVersion <= VMM_SAVED_STATE_VERSION_3_0)
    {
        /* Ignore the stack bottom, stack pointer and stack bits. */
        RTRCPTR RCPtrIgnored;
        SSMR3GetRCPtr(pSSM, &RCPtrIgnored);
        SSMR3GetRCPtr(pSSM, &RCPtrIgnored);
#ifdef RT_OS_DARWIN
        if (   SSMR3HandleVersion(pSSM)  >= VBOX_FULL_VERSION_MAKE(3,0,0)
            && SSMR3HandleVersion(pSSM)  <  VBOX_FULL_VERSION_MAKE(3,1,0)
            && SSMR3HandleRevision(pSSM) >= 48858
            && (   !strcmp(SSMR3HandleHostOSAndArch(pSSM), "darwin.x86")
                || !strcmp(SSMR3HandleHostOSAndArch(pSSM), "") )
           )
            SSMR3Skip(pSSM, 16384);
        else
            SSMR3Skip(pSSM, 8192);
#else
        SSMR3Skip(pSSM, 8192);
#endif
    }

    /*
     * Restore the VMCPU states. VCPU 0 is always started.
     */
    VMCPU_SET_STATE(pVM->apCpusR3[0], VMCPUSTATE_STARTED);
    for (VMCPUID i = 1; i < pVM->cCpus; i++)
    {
        bool fStarted;
        int rc = SSMR3GetBool(pSSM, &fStarted);
        if (RT_FAILURE(rc))
            return rc;
        VMCPU_SET_STATE(pVM->apCpusR3[i], fStarted ? VMCPUSTATE_STARTED : VMCPUSTATE_STOPPED);
    }

    /* terminator */
    uint32_t u32;
    int rc = SSMR3GetU32(pSSM, &u32);
    if (RT_FAILURE(rc))
        return rc;
    if (u32 != UINT32_MAX)
    {
        AssertMsgFailed(("u32=%#x\n", u32));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }
    return VINF_SUCCESS;
}


/**
 * Suspends the CPU yielder.
 *
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(void) VMMR3YieldSuspend(PVM pVM)
{
#if 0 /* pointless when timers doesn't run on EMT */
    VMCPU_ASSERT_EMT(pVM->apCpusR3[0]);
    if (!pVM->vmm.s.cYieldResumeMillies)
    {
        uint64_t u64Now = TMTimerGet(pVM, pVM->vmm.s.hYieldTimer);
        uint64_t u64Expire = TMTimerGetExpire(pVM, pVM->vmm.s.hYieldTimer);
        if (u64Now >= u64Expire || u64Expire == ~(uint64_t)0)
            pVM->vmm.s.cYieldResumeMillies = pVM->vmm.s.cYieldEveryMillies;
        else
            pVM->vmm.s.cYieldResumeMillies = TMTimerToMilli(pVM, pVM->vmm.s.hYieldTimer, u64Expire - u64Now);
        TMTimerStop(pVM, pVM->vmm.s.hYieldTimer);
    }
    pVM->vmm.s.u64LastYield = RTTimeNanoTS();
#else
    RT_NOREF(pVM);
#endif
}


/**
 * Stops the CPU yielder.
 *
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(void) VMMR3YieldStop(PVM pVM)
{
#if 0 /* pointless when timers doesn't run on EMT */
    if (!pVM->vmm.s.cYieldResumeMillies)
        TMTimerStop(pVM, pVM->vmm.s.hYieldTimer);
    pVM->vmm.s.cYieldResumeMillies = pVM->vmm.s.cYieldEveryMillies;
    pVM->vmm.s.u64LastYield = RTTimeNanoTS();
#else
    RT_NOREF(pVM);
#endif
}


/**
 * Resumes the CPU yielder when it has been a suspended or stopped.
 *
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(void) VMMR3YieldResume(PVM pVM)
{
#if 0 /* pointless when timers doesn't run on EMT */
    if (pVM->vmm.s.cYieldResumeMillies)
    {
        TMTimerSetMillies(pVM, pVM->vmm.s.hYieldTimer, pVM->vmm.s.cYieldResumeMillies);
        pVM->vmm.s.cYieldResumeMillies = 0;
    }
#else
    RT_NOREF(pVM);
#endif
}


#if 0 /* pointless when timers doesn't run on EMT */
/**
 * @callback_method_impl{FNTMTIMERINT, EMT yielder}
 *
 * @todo This is a UNI core/thread thing, really...   Should be reconsidered.
 */
static DECLCALLBACK(void) vmmR3YieldEMT(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    NOREF(pvUser);

    /*
     * This really needs some careful tuning. While we shouldn't be too greedy since
     * that'll cause the rest of the system to stop up, we shouldn't be too nice either
     * because that'll cause us to stop up.
     *
     * The current logic is to use the default interval when there is no lag worth
     * mentioning, but when we start accumulating lag we don't bother yielding at all.
     *
     * (This depends on the TMCLOCK_VIRTUAL_SYNC to be scheduled before TMCLOCK_REAL
     * so the lag is up to date.)
     */
    const uint64_t u64Lag = TMVirtualSyncGetLag(pVM);
    if (    u64Lag     <   50000000 /* 50ms */
        ||  (   u64Lag < 1000000000 /*  1s */
             && RTTimeNanoTS() - pVM->vmm.s.u64LastYield < 500000000 /* 500 ms */)
       )
    {
        uint64_t u64Elapsed = RTTimeNanoTS();
        pVM->vmm.s.u64LastYield = u64Elapsed;

        RTThreadYield();

#ifdef LOG_ENABLED
        u64Elapsed = RTTimeNanoTS() - u64Elapsed;
        Log(("vmmR3YieldEMT: %RI64 ns\n", u64Elapsed));
#endif
    }
    TMTimerSetMillies(pVM, hTimer, pVM->vmm.s.cYieldEveryMillies);
}
#endif


/**
 * Executes guest code (Intel VT-x and AMD-V).
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) VMMR3HmRunGC(PVM pVM, PVMCPU pVCpu)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    /* We should actually never get here as the only execution engine is NEM. */
    RT_NOREF(pVM, pVCpu);
    AssertReleaseFailed();
    return VERR_NOT_SUPPORTED;
#else
    Log2(("VMMR3HmRunGC: (cs:rip=%04x:%RX64)\n", CPUMGetGuestCS(pVCpu), CPUMGetGuestRIP(pVCpu)));

    int rc;
    do
    {
# ifdef NO_SUPCALLR0VMM
        rc = VERR_GENERAL_FAILURE;
# else
        rc = SUPR3CallVMMR0Fast(VMCC_GET_VMR0_FOR_CALL(pVM), VMMR0_DO_HM_RUN, pVCpu->idCpu);
        if (RT_LIKELY(rc == VINF_SUCCESS))
            rc = pVCpu->vmm.s.iLastGZRc;
# endif
    } while (rc == VINF_EM_RAW_INTERRUPT_HYPER);

# if 0 /** @todo triggers too often */
    Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TO_R3));
# endif

    /*
     * Flush the logs
     */
# ifdef LOG_ENABLED
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.Logger, NULL);
# endif
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.RelLogger, RTLogRelGetDefaultInstance());
    if (rc != VERR_VMM_RING0_ASSERTION)
    {
        Log2(("VMMR3HmRunGC: returns %Rrc (cs:rip=%04x:%RX64)\n", rc, CPUMGetGuestCS(pVCpu), CPUMGetGuestRIP(pVCpu)));
        return rc;
    }
    return vmmR3HandleRing0Assert(pVM, pVCpu);
#endif
}


/**
 * Perform one of the fast I/O control VMMR0 operation.
 *
 * @returns VBox strict status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   enmOperation    The operation to perform.
 */
VMMR3_INT_DECL(VBOXSTRICTRC) VMMR3CallR0EmtFast(PVM pVM, PVMCPU pVCpu, VMMR0OPERATION enmOperation)
{
    VBOXSTRICTRC rcStrict;
    do
    {
#ifdef NO_SUPCALLR0VMM
        rcStrict = VERR_GENERAL_FAILURE;
#else
        rcStrict = SUPR3CallVMMR0Fast(VMCC_GET_VMR0_FOR_CALL(pVM), enmOperation, pVCpu->idCpu);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            rcStrict = pVCpu->vmm.s.iLastGZRc;
#endif
    } while (rcStrict == VINF_EM_RAW_INTERRUPT_HYPER);

    /*
     * Flush the logs
     */
#ifdef LOG_ENABLED
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.Logger, NULL);
#endif
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.RelLogger, RTLogRelGetDefaultInstance());
    if (rcStrict != VERR_VMM_RING0_ASSERTION)
        return rcStrict;
    return vmmR3HandleRing0Assert(pVM, pVCpu);
}


#if defined(VBOX_VMM_TARGET_ARMV8)

/**
 * VCPU worker for VMMR3CpuOn.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           Virtual CPU to perform SIPI on.
 * @param   GCPhysExecAddr  The guest physical address to start executing at.
 * @param   u64CtxId        The context ID passed in x0/w0.
 */
static DECLCALLBACK(int) vmmR3CpuOn(PVM pVM, VMCPUID idCpu, RTGCPHYS GCPhysExecAddr, uint64_t u64CtxId)
{
    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
    VMCPU_ASSERT_EMT(pVCpu);

    if (EMGetState(pVCpu) != EMSTATE_WAIT_SIPI)
        return VINF_SUCCESS;

    PCPUMCTX pCtx = CPUMQueryGuestCtxPtr(pVCpu);

    pCtx->aGRegs[ARMV8_AARCH64_REG_X0].x = u64CtxId;
    pCtx->Pc.u64                         = GCPhysExecAddr;

    Log(("vmmR3CpuOn for VCPU %d with GCPhysExecAddr=%RGp u64CtxId=%#RX64\n", idCpu, GCPhysExecAddr, u64CtxId));

# if 1 /* If we keep the EMSTATE_WAIT_SIPI method, then move this to EM.cpp. */
    EMSetState(pVCpu, EMSTATE_HALTED);
    return VINF_EM_RESCHEDULE;
# else /* And if we go the VMCPU::enmState way it can stay here. */
    VMCPU_ASSERT_STATE(pVCpu, VMCPUSTATE_STOPPED);
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED);
    return VINF_SUCCESS;
# endif
}


/**
 * Sends a Startup IPI to the virtual CPU by setting CS:EIP into
 * vector-dependent state and unhalting processor.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           Virtual CPU to perform SIPI on.
 * @param   GCPhysExecAddr  The guest physical address to start executing at.
 * @param   u64CtxId        The context ID passed in x0/w0.
 */
VMMR3_INT_DECL(void)    VMMR3CpuOn(PVM pVM, VMCPUID idCpu, RTGCPHYS GCPhysExecAddr, uint64_t u64CtxId)
{
    AssertReturnVoid(idCpu < pVM->cCpus);

    int rc = VMR3ReqCallNoWait(pVM, idCpu, (PFNRT)vmmR3CpuOn, 4, pVM, idCpu, GCPhysExecAddr, u64CtxId);
    AssertRC(rc);
}

#else /* !VBOX_VMM_TARGET_ARMV8 */

/**
 * VCPU worker for VMMR3SendStartupIpi.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       Virtual CPU to perform SIPI on.
 * @param   uVector     The SIPI vector.
 */
static DECLCALLBACK(int) vmmR3SendStartupIpi(PVM pVM, VMCPUID idCpu, uint32_t uVector)
{
    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * In the INIT state, the target CPU is only responsive to an SIPI.
     * This is also true for when when the CPU is in VMX non-root mode.
     *
     * See AMD spec. 16.5 "Interprocessor Interrupts (IPI)".
     * See Intel spec. 26.6.2 "Activity State".
     */
    if (EMGetState(pVCpu) != EMSTATE_WAIT_SIPI)
        return VINF_SUCCESS;

    PCPUMCTX pCtx = CPUMQueryGuestCtxPtr(pVCpu);
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (CPUMIsGuestInVmxRootMode(pCtx))
    {
        /* If the CPU is in VMX non-root mode we must cause a VM-exit. */
        if (CPUMIsGuestInVmxNonRootMode(pCtx))
            return VBOXSTRICTRC_TODO(IEMExecVmxVmexitStartupIpi(pVCpu, uVector));

        /* If the CPU is in VMX root mode (and not in VMX non-root mode) SIPIs are blocked. */
        return VINF_SUCCESS;
    }
# endif

    pCtx->cs.Sel        = uVector << 8;
    pCtx->cs.ValidSel   = uVector << 8;
    pCtx->cs.fFlags     = CPUMSELREG_FLAGS_VALID;
    pCtx->cs.u64Base    = uVector << 12;
    pCtx->cs.u32Limit   = UINT32_C(0x0000ffff);
    pCtx->rip           = 0;

    Log(("vmmR3SendSipi for VCPU %d with vector %x\n", idCpu, uVector));

# if 1 /* If we keep the EMSTATE_WAIT_SIPI method, then move this to EM.cpp. */
    EMSetState(pVCpu, EMSTATE_HALTED);
    return VINF_EM_RESCHEDULE;
# else /* And if we go the VMCPU::enmState way it can stay here. */
    VMCPU_ASSERT_STATE(pVCpu, VMCPUSTATE_STOPPED);
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED);
    return VINF_SUCCESS;
# endif
}


/**
 * VCPU worker for VMMR3SendInitIpi.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       Virtual CPU to perform SIPI on.
 */
static DECLCALLBACK(int) vmmR3SendInitIpi(PVM pVM, VMCPUID idCpu)
{
    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
    VMCPU_ASSERT_EMT(pVCpu);

    Log(("vmmR3SendInitIpi for VCPU %d\n", idCpu));

    /** @todo r=ramshankar: We should probably block INIT signal when the CPU is in
     *        wait-for-SIPI state. Verify. */

    /* If the CPU is in VMX non-root mode, INIT signals cause VM-exits. */
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    PCCPUMCTX pCtx = CPUMQueryGuestCtxPtr(pVCpu);
    if (CPUMIsGuestInVmxNonRootMode(pCtx))
        return VBOXSTRICTRC_TODO(IEMExecVmxVmexit(pVCpu, VMX_EXIT_INIT_SIGNAL, 0 /* uExitQual */));
# endif

    /** @todo Figure out how to handle a SVM nested-guest intercepts here for INIT
     *  IPI (e.g. SVM_EXIT_INIT). */

    PGMR3ResetCpu(pVM, pVCpu);
    PDMR3ResetCpu(pVCpu);   /* Only clears pending interrupts force flags */
# if !defined(VBOX_VMM_TARGET_ARMV8)
    APICR3InitIpi(pVCpu);
# endif
    TRPMR3ResetCpu(pVCpu);
    CPUMR3ResetCpu(pVM, pVCpu);
    EMR3ResetCpu(pVCpu);
    HMR3ResetCpu(pVCpu);
    NEMR3ResetCpu(pVCpu, true /*fInitIpi*/);

    /* This will trickle up on the target EMT. */
    return VINF_EM_WAIT_SIPI;
}


/**
 * Sends a Startup IPI to the virtual CPU by setting CS:EIP into
 * vector-dependent state and unhalting processor.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       Virtual CPU to perform SIPI on.
 * @param   uVector     SIPI vector.
 */
VMMR3_INT_DECL(void) VMMR3SendStartupIpi(PVM pVM, VMCPUID idCpu,  uint32_t uVector)
{
    AssertReturnVoid(idCpu < pVM->cCpus);

    int rc = VMR3ReqCallNoWait(pVM, idCpu, (PFNRT)vmmR3SendStartupIpi, 3, pVM, idCpu, uVector);
    AssertRC(rc);
}


/**
 * Sends init IPI to the virtual CPU.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       Virtual CPU to perform int IPI on.
 */
VMMR3_INT_DECL(void) VMMR3SendInitIpi(PVM pVM, VMCPUID idCpu)
{
    AssertReturnVoid(idCpu < pVM->cCpus);

    int rc = VMR3ReqCallNoWait(pVM, idCpu, (PFNRT)vmmR3SendInitIpi, 2, pVM, idCpu);
    AssertRC(rc);
}

#endif /* !VBOX_VMM_TARGET_ARMV8 */

/**
 * Registers the guest memory range that can be used for patching.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3DECL(int) VMMR3RegisterPatchMemory(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    VM_ASSERT_EMT(pVM);
    if (HMIsEnabled(pVM))
        return HMR3EnablePatching(pVM, pPatchMem, cbPatchMem);

    return VERR_NOT_SUPPORTED;
}


/**
 * Deregisters the guest memory range that can be used for patching.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3DECL(int) VMMR3DeregisterPatchMemory(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    if (HMIsEnabled(pVM))
        return HMR3DisablePatching(pVM, pPatchMem, cbPatchMem);

    return VINF_SUCCESS;
}


/**
 * Common recursion handler for the other EMTs.
 *
 * @returns Strict VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   rcStrict            Current status code to be combined with the one
 *                              from this recursion and returned.
 */
static VBOXSTRICTRC vmmR3EmtRendezvousCommonRecursion(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
    int rc2;

    /*
     * We wait here while the initiator of this recursion reconfigures
     * everything.  The last EMT to get in signals the initiator.
     */
    if (ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsRecursingPush) == pVM->cCpus)
    {
        rc2 = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousRecursionPushCaller);
        AssertLogRelRC(rc2);
    }

    rc2 = RTSemEventMultiWait(pVM->vmm.s.hEvtMulRendezvousRecursionPush, RT_INDEFINITE_WAIT);
    AssertLogRelRC(rc2);

    /*
     * Do the normal rendezvous processing.
     */
    VBOXSTRICTRC rcStrict2 = vmmR3EmtRendezvousCommon(pVM, pVCpu, false /* fIsCaller */, pVM->vmm.s.fRendezvousFlags,
                                                      pVM->vmm.s.pfnRendezvous, pVM->vmm.s.pvRendezvousUser);

    /*
     * Wait for the initiator to restore everything.
     */
    rc2 = RTSemEventMultiWait(pVM->vmm.s.hEvtMulRendezvousRecursionPop, RT_INDEFINITE_WAIT);
    AssertLogRelRC(rc2);

    /*
     * Last thread out of here signals the initiator.
     */
    if (ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsRecursingPop) == pVM->cCpus)
    {
        rc2 = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousRecursionPopCaller);
        AssertLogRelRC(rc2);
    }

    /*
     * Merge status codes and return.
     */
    AssertRC(VBOXSTRICTRC_VAL(rcStrict2));
    if (    rcStrict2 != VINF_SUCCESS
        &&  (   rcStrict == VINF_SUCCESS
             || rcStrict > rcStrict2))
        rcStrict = rcStrict2;
    return rcStrict;
}


/**
 * Count returns and have the last non-caller EMT wake up the caller.
 *
 * @returns VBox strict informational status code for EM scheduling. No failures
 *          will be returned here, those are for the caller only.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   rcStrict            The current accumulated recursive status code,
 *                              to be merged with i32RendezvousStatus and
 *                              returned.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) vmmR3EmtRendezvousNonCallerReturn(PVM pVM, VBOXSTRICTRC rcStrict)
{
    VBOXSTRICTRC rcStrict2 = ASMAtomicReadS32(&pVM->vmm.s.i32RendezvousStatus);

    uint32_t cReturned = ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsReturned);
    if (cReturned == pVM->cCpus - 1U)
    {
        int rc = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousDoneCaller);
        AssertLogRelRC(rc);
    }

    /*
     * Merge the status codes, ignoring error statuses in this code path.
     */
    AssertLogRelMsgReturn(   rcStrict2 <= VINF_SUCCESS
                          || (rcStrict2 >= VINF_EM_FIRST && rcStrict2 <= VINF_EM_LAST),
                          ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict2)),
                          VERR_IPE_UNEXPECTED_INFO_STATUS);

    if (RT_SUCCESS(rcStrict2))
    {
        if (    rcStrict2 != VINF_SUCCESS
            &&  (   rcStrict == VINF_SUCCESS
                 || rcStrict > rcStrict2))
            rcStrict = rcStrict2;
    }
    return rcStrict;
}


/**
 * Common worker for VMMR3EmtRendezvous and VMMR3EmtRendezvousFF.
 *
 * @returns VBox strict informational status code for EM scheduling. No failures
 *          will be returned here, those are for the caller only.  When
 *          fIsCaller is set, VINF_SUCCESS is always returned.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   fIsCaller           Whether we're the VMMR3EmtRendezvous caller or
 *                              not.
 * @param   fFlags              The flags.
 * @param   pfnRendezvous       The callback.
 * @param   pvUser              The user argument for the callback.
 */
static VBOXSTRICTRC vmmR3EmtRendezvousCommon(PVM pVM, PVMCPU pVCpu, bool fIsCaller,
                                             uint32_t fFlags, PFNVMMEMTRENDEZVOUS pfnRendezvous, void *pvUser)
{
    int rc;
    VBOXSTRICTRC rcStrictRecursion = VINF_SUCCESS;

    /*
     * Enter, the last EMT triggers the next callback phase.
     */
    uint32_t cEntered = ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsEntered);
    if (cEntered != pVM->cCpus)
    {
        if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE)
        {
            /* Wait for our turn. */
            for (;;)
            {
                rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousEnterOneByOne, RT_INDEFINITE_WAIT);
                AssertLogRelRC(rc);
                if (!pVM->vmm.s.fRendezvousRecursion)
                    break;
                rcStrictRecursion = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrictRecursion);
            }
        }
        else if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE)
        {
            /* Wait for the last EMT to arrive and wake everyone up. */
            rc = RTSemEventMultiWait(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce, RT_INDEFINITE_WAIT);
            AssertLogRelRC(rc);
            Assert(!pVM->vmm.s.fRendezvousRecursion);
        }
        else if (   (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                 || (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING)
        {
            /* Wait for our turn. */
            for (;;)
            {
                rc = RTSemEventWait(pVM->vmm.s.pahEvtRendezvousEnterOrdered[pVCpu->idCpu], RT_INDEFINITE_WAIT);
                AssertLogRelRC(rc);
                if (!pVM->vmm.s.fRendezvousRecursion)
                    break;
                rcStrictRecursion = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrictRecursion);
            }
        }
        else
        {
            Assert((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE);

            /*
             * The execute once is handled specially to optimize the code flow.
             *
             * The last EMT to arrive will perform the callback and the other
             * EMTs will wait on the Done/DoneCaller semaphores (instead of
             * the EnterOneByOne/AllAtOnce) in the meanwhile. When the callback
             * returns, that EMT will initiate the normal return sequence.
             */
            if (!fIsCaller)
            {
                for (;;)
                {
                    rc = RTSemEventMultiWait(pVM->vmm.s.hEvtMulRendezvousDone, RT_INDEFINITE_WAIT);
                    AssertLogRelRC(rc);
                    if (!pVM->vmm.s.fRendezvousRecursion)
                        break;
                    rcStrictRecursion = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrictRecursion);
                }

                return vmmR3EmtRendezvousNonCallerReturn(pVM, rcStrictRecursion);
            }
            return VINF_SUCCESS;
        }
    }
    else
    {
        /*
         * All EMTs are waiting, clear the FF and take action according to the
         * execution method.
         */
        VM_FF_CLEAR(pVM, VM_FF_EMT_RENDEZVOUS);

        if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE)
        {
            /* Wake up everyone. */
            rc = RTSemEventMultiSignal(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);
            AssertLogRelRC(rc);
        }
        else if (   (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                 || (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING)
        {
            /* Figure out who to wake up and wake it up. If it's ourself, then
               it's easy otherwise wait for our turn. */
            VMCPUID iFirst = (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                           ? 0
                           : pVM->cCpus - 1U;
            if (pVCpu->idCpu != iFirst)
            {
                rc = RTSemEventSignal(pVM->vmm.s.pahEvtRendezvousEnterOrdered[iFirst]);
                AssertLogRelRC(rc);
                for (;;)
                {
                    rc = RTSemEventWait(pVM->vmm.s.pahEvtRendezvousEnterOrdered[pVCpu->idCpu], RT_INDEFINITE_WAIT);
                    AssertLogRelRC(rc);
                    if (!pVM->vmm.s.fRendezvousRecursion)
                        break;
                    rcStrictRecursion = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrictRecursion);
                }
            }
        }
        /* else: execute the handler on the current EMT and wake up one or more threads afterwards. */
    }


    /*
     * Do the callback and update the status if necessary.
     */
    if (    !(fFlags & VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR)
        ||  RT_SUCCESS(ASMAtomicUoReadS32(&pVM->vmm.s.i32RendezvousStatus)) )
    {
        VBOXSTRICTRC rcStrict2 = pfnRendezvous(pVM, pVCpu, pvUser);
        if (rcStrict2 != VINF_SUCCESS)
        {
            AssertLogRelMsg(   rcStrict2 <= VINF_SUCCESS
                            || (rcStrict2 >= VINF_EM_FIRST && rcStrict2 <= VINF_EM_LAST),
                            ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict2)));
            int32_t i32RendezvousStatus;
            do
            {
                i32RendezvousStatus = ASMAtomicUoReadS32(&pVM->vmm.s.i32RendezvousStatus);
                if (    rcStrict2 == i32RendezvousStatus
                    ||  RT_FAILURE(i32RendezvousStatus)
                    ||  (   i32RendezvousStatus != VINF_SUCCESS
                         && rcStrict2 > i32RendezvousStatus))
                    break;
            } while (!ASMAtomicCmpXchgS32(&pVM->vmm.s.i32RendezvousStatus, VBOXSTRICTRC_VAL(rcStrict2), i32RendezvousStatus));
        }
    }

    /*
     * Increment the done counter and take action depending on whether we're
     * the last to finish callback execution.
     */
    uint32_t cDone = ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsDone);
    if (    cDone != pVM->cCpus
        &&  (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) != VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE)
    {
        /* Signal the next EMT? */
        if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE)
        {
            rc = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousEnterOneByOne);
            AssertLogRelRC(rc);
        }
        else if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING)
        {
            Assert(cDone == pVCpu->idCpu + 1U);
            rc = RTSemEventSignal(pVM->vmm.s.pahEvtRendezvousEnterOrdered[pVCpu->idCpu + 1U]);
            AssertLogRelRC(rc);
        }
        else if ((fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING)
        {
            Assert(pVM->cCpus - cDone == pVCpu->idCpu);
            rc = RTSemEventSignal(pVM->vmm.s.pahEvtRendezvousEnterOrdered[pVM->cCpus - cDone - 1U]);
            AssertLogRelRC(rc);
        }

        /* Wait for the rest to finish (the caller waits on hEvtRendezvousDoneCaller). */
        if (!fIsCaller)
        {
            for (;;)
            {
                rc = RTSemEventMultiWait(pVM->vmm.s.hEvtMulRendezvousDone, RT_INDEFINITE_WAIT);
                AssertLogRelRC(rc);
                if (!pVM->vmm.s.fRendezvousRecursion)
                    break;
                rcStrictRecursion = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrictRecursion);
            }
        }
    }
    else
    {
        /* Callback execution is all done, tell the rest to return. */
        rc = RTSemEventMultiSignal(pVM->vmm.s.hEvtMulRendezvousDone);
        AssertLogRelRC(rc);
    }

    if (!fIsCaller)
        return vmmR3EmtRendezvousNonCallerReturn(pVM, rcStrictRecursion);
    return rcStrictRecursion;
}


/**
 * Called in response to VM_FF_EMT_RENDEZVOUS.
 *
 * @returns VBox strict status code - EM scheduling.  No errors will be returned
 *          here, nor will any non-EM scheduling status codes be returned.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 *
 * @thread  EMT
 */
VMMR3_INT_DECL(int) VMMR3EmtRendezvousFF(PVM pVM, PVMCPU pVCpu)
{
    Assert(!pVCpu->vmm.s.fInRendezvous);
    Log(("VMMR3EmtRendezvousFF: EMT%#u\n", pVCpu->idCpu));
    pVCpu->vmm.s.fInRendezvous = true;
    VBOXSTRICTRC rcStrict = vmmR3EmtRendezvousCommon(pVM, pVCpu, false /* fIsCaller */, pVM->vmm.s.fRendezvousFlags,
                                                     pVM->vmm.s.pfnRendezvous, pVM->vmm.s.pvRendezvousUser);
    pVCpu->vmm.s.fInRendezvous = false;
    Log(("VMMR3EmtRendezvousFF: EMT%#u returns %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict)));
    return VBOXSTRICTRC_TODO(rcStrict);
}


/**
 * Helper for resetting an single wakeup event sempahore.
 *
 * @returns VERR_TIMEOUT on success, RTSemEventWait status otherwise.
 * @param   hEvt        The event semaphore to reset.
 */
static int vmmR3HlpResetEvent(RTSEMEVENT hEvt)
{
    for (uint32_t cLoops = 0; ; cLoops++)
    {
        int rc = RTSemEventWait(hEvt, 0 /*cMsTimeout*/);
        if (rc != VINF_SUCCESS || cLoops > _4K)
            return rc;
    }
}


/**
 * Worker for VMMR3EmtRendezvous that handles recursion.
 *
 * @returns VBox strict status code.  This will be the first error,
 *          VINF_SUCCESS, or an EM scheduling status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   fFlags          Flags indicating execution methods. See
 *                          grp_VMMR3EmtRendezvous_fFlags.
 * @param   pfnRendezvous   The callback.
 * @param   pvUser          User argument for the callback.
 *
 * @thread  EMT(pVCpu)
 */
static VBOXSTRICTRC vmmR3EmtRendezvousRecursive(PVM pVM, PVMCPU pVCpu, uint32_t fFlags,
                                                PFNVMMEMTRENDEZVOUS pfnRendezvous, void *pvUser)
{
    Log(("vmmR3EmtRendezvousRecursive: %#x EMT#%u depth=%d\n", fFlags, pVCpu->idCpu, pVM->vmm.s.cRendezvousRecursions));
    AssertLogRelReturn(pVM->vmm.s.cRendezvousRecursions < 3, VERR_DEADLOCK);
    Assert(pVCpu->vmm.s.fInRendezvous);

    /*
     * Save the current state.
     */
    uint32_t const              fParentFlags    = pVM->vmm.s.fRendezvousFlags;
    uint32_t const              cParentDone     = pVM->vmm.s.cRendezvousEmtsDone;
    int32_t const               iParentStatus   = pVM->vmm.s.i32RendezvousStatus;
    PFNVMMEMTRENDEZVOUS const   pfnParent       = pVM->vmm.s.pfnRendezvous;
    void * const                pvParentUser    = pVM->vmm.s.pvRendezvousUser;

    /*
     * Check preconditions and save the current state.
     */
    AssertReturn(   (fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                 || (fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING
                 || (fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE
                 || (fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE,
                 VERR_INTERNAL_ERROR);
    AssertReturn(pVM->vmm.s.cRendezvousEmtsEntered == pVM->cCpus, VERR_INTERNAL_ERROR_2);
    AssertReturn(pVM->vmm.s.cRendezvousEmtsReturned == 0, VERR_INTERNAL_ERROR_3);

    /*
     * Reset the recursion prep and pop semaphores.
     */
    int rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousRecursionPush);
    AssertLogRelRCReturn(rc, rc);
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousRecursionPop);
    AssertLogRelRCReturn(rc, rc);
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousRecursionPushCaller);
    AssertLogRelMsgReturn(rc == VERR_TIMEOUT, ("%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousRecursionPopCaller);
    AssertLogRelMsgReturn(rc == VERR_TIMEOUT, ("%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);

    /*
     * Usher the other thread into the recursion routine.
     */
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsRecursingPush, 0);
    ASMAtomicWriteBool(&pVM->vmm.s.fRendezvousRecursion, true);

    uint32_t cLeft = pVM->cCpus - (cParentDone + 1U);
    if ((fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE)
        while (cLeft-- > 0)
        {
            rc = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousEnterOneByOne);
            AssertLogRelRC(rc);
        }
    else if ((fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING)
    {
        Assert(cLeft == pVM->cCpus - (pVCpu->idCpu + 1U));
        for (VMCPUID iCpu = pVCpu->idCpu + 1U; iCpu < pVM->cCpus; iCpu++)
        {
            rc = RTSemEventSignal(pVM->vmm.s.pahEvtRendezvousEnterOrdered[iCpu]);
            AssertLogRelRC(rc);
        }
    }
    else if ((fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING)
    {
        Assert(cLeft == pVCpu->idCpu);
        for (VMCPUID iCpu = pVCpu->idCpu; iCpu > 0; iCpu--)
        {
            rc = RTSemEventSignal(pVM->vmm.s.pahEvtRendezvousEnterOrdered[iCpu - 1U]);
            AssertLogRelRC(rc);
        }
    }
    else
        AssertLogRelReturn((fParentFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE,
                           VERR_INTERNAL_ERROR_4);

    rc = RTSemEventMultiSignal(pVM->vmm.s.hEvtMulRendezvousDone);
    AssertLogRelRC(rc);
    rc = RTSemEventSignal(pVM->vmm.s.hEvtRendezvousDoneCaller);
    AssertLogRelRC(rc);


    /*
     * Wait for the EMTs to wake up and get out of the parent rendezvous code.
     */
    if (ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsRecursingPush) != pVM->cCpus)
    {
        rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousRecursionPushCaller, RT_INDEFINITE_WAIT);
        AssertLogRelRC(rc);
    }

    ASMAtomicWriteBool(&pVM->vmm.s.fRendezvousRecursion, false);

    /*
     * Clear the slate and setup the new rendezvous.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        rc = vmmR3HlpResetEvent(pVM->vmm.s.pahEvtRendezvousEnterOrdered[i]);
        AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));
    }
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousEnterOneByOne);        AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);  AssertLogRelRC(rc);
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousDone);            AssertLogRelRC(rc);
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousDoneCaller);           AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));

    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsEntered, 0);
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsDone, 0);
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsReturned, 0);
    ASMAtomicWriteS32(&pVM->vmm.s.i32RendezvousStatus, VINF_SUCCESS);
    ASMAtomicWritePtr((void * volatile *)&pVM->vmm.s.pfnRendezvous, (void *)(uintptr_t)pfnRendezvous);
    ASMAtomicWritePtr(&pVM->vmm.s.pvRendezvousUser, pvUser);
    ASMAtomicWriteU32(&pVM->vmm.s.fRendezvousFlags, fFlags);
    ASMAtomicIncU32(&pVM->vmm.s.cRendezvousRecursions);

    /*
     * We're ready to go now, do normal rendezvous processing.
     */
    rc = RTSemEventMultiSignal(pVM->vmm.s.hEvtMulRendezvousRecursionPush);
    AssertLogRelRC(rc);

    VBOXSTRICTRC rcStrict = vmmR3EmtRendezvousCommon(pVM, pVCpu, true /*fIsCaller*/, fFlags, pfnRendezvous, pvUser);

    /*
     * The caller waits for the other EMTs to be done, return and waiting on the
     * pop semaphore.
     */
    for (;;)
    {
        rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousDoneCaller, RT_INDEFINITE_WAIT);
        AssertLogRelRC(rc);
        if (!pVM->vmm.s.fRendezvousRecursion)
            break;
        rcStrict = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrict);
    }

    /*
     * Get the return code and merge it with the above recursion status.
     */
    VBOXSTRICTRC rcStrict2 = pVM->vmm.s.i32RendezvousStatus;
    if (    rcStrict2 != VINF_SUCCESS
        &&  (   rcStrict == VINF_SUCCESS
             || rcStrict > rcStrict2))
        rcStrict = rcStrict2;

    /*
     * Restore the parent rendezvous state.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        rc = vmmR3HlpResetEvent(pVM->vmm.s.pahEvtRendezvousEnterOrdered[i]);
        AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));
    }
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousEnterOneByOne);        AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);  AssertLogRelRC(rc);
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousDone);            AssertLogRelRC(rc);
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousDoneCaller);           AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));

    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsEntered,   pVM->cCpus);
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsReturned,  0);
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsDone,      cParentDone);
    ASMAtomicWriteS32(&pVM->vmm.s.i32RendezvousStatus,      iParentStatus);
    ASMAtomicWriteU32(&pVM->vmm.s.fRendezvousFlags,         fParentFlags);
    ASMAtomicWritePtr(&pVM->vmm.s.pvRendezvousUser,         pvParentUser);
    ASMAtomicWritePtr((void * volatile *)&pVM->vmm.s.pfnRendezvous, (void *)(uintptr_t)pfnParent);

    /*
     * Usher the other EMTs back to their parent recursion routine, waiting
     * for them to all get there before we return (makes sure they've been
     * scheduled and are past the pop event sem, see below).
     */
    ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsRecursingPop, 0);
    rc = RTSemEventMultiSignal(pVM->vmm.s.hEvtMulRendezvousRecursionPop);
    AssertLogRelRC(rc);

    if (ASMAtomicIncU32(&pVM->vmm.s.cRendezvousEmtsRecursingPop) != pVM->cCpus)
    {
        rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousRecursionPopCaller, RT_INDEFINITE_WAIT);
        AssertLogRelRC(rc);
    }

    /*
     * We must reset the pop semaphore on the way out (doing the pop caller too,
     * just in case).  The parent may be another recursion.
     */
    rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousRecursionPop);    AssertLogRelRC(rc);
    rc = vmmR3HlpResetEvent(pVM->vmm.s.hEvtRendezvousRecursionPopCaller);   AssertLogRelMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));

    ASMAtomicDecU32(&pVM->vmm.s.cRendezvousRecursions);

    Log(("vmmR3EmtRendezvousRecursive: %#x EMT#%u depth=%d returns %Rrc\n",
         fFlags, pVCpu->idCpu, pVM->vmm.s.cRendezvousRecursions, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * EMT rendezvous.
 *
 * Gathers all the EMTs and execute some code on each of them, either in a one
 * by one fashion or all at once.
 *
 * @returns VBox strict status code.  This will be the first error,
 *          VINF_SUCCESS, or an EM scheduling status code.
 *
 * @retval  VERR_DEADLOCK if recursion is attempted using a rendezvous type that
 *          doesn't support it or if the recursion is too deep.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          Flags indicating execution methods. See
 *                          grp_VMMR3EmtRendezvous_fFlags.  The one-by-one,
 *                          descending and ascending rendezvous types support
 *                          recursion from inside @a pfnRendezvous.
 * @param   pfnRendezvous   The callback.
 * @param   pvUser          User argument for the callback.
 *
 * @thread  Any.
 */
VMMR3DECL(int) VMMR3EmtRendezvous(PVM pVM, uint32_t fFlags, PFNVMMEMTRENDEZVOUS pfnRendezvous, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);
    AssertMsg(   (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) != VMMEMTRENDEZVOUS_FLAGS_TYPE_INVALID
              && (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) <= VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING
              && !(fFlags & ~VMMEMTRENDEZVOUS_FLAGS_VALID_MASK), ("%#x\n", fFlags));
    AssertMsg(   !(fFlags & VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR)
              || (   (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) != VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE
                  && (fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) != VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE),
              ("type %u\n", fFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK));

    VBOXSTRICTRC rcStrict;
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
    {
        /*
         * Forward the request to an EMT thread.
         */
        Log(("VMMR3EmtRendezvous: %#x non-EMT\n", fFlags));
        if (!(fFlags & VMMEMTRENDEZVOUS_FLAGS_PRIORITY))
            rcStrict = VMR3ReqCallWait(pVM, VMCPUID_ANY, (PFNRT)VMMR3EmtRendezvous, 4, pVM, fFlags, pfnRendezvous, pvUser);
        else
            rcStrict = VMR3ReqPriorityCallWait(pVM, VMCPUID_ANY, (PFNRT)VMMR3EmtRendezvous, 4, pVM, fFlags, pfnRendezvous, pvUser);
        Log(("VMMR3EmtRendezvous: %#x non-EMT returns %Rrc\n", fFlags, VBOXSTRICTRC_VAL(rcStrict)));
    }
    else if (   pVM->cCpus == 1
             || (   pVM->enmVMState == VMSTATE_DESTROYING
                 && VMR3GetActiveEmts(pVM->pUVM) < pVM->cCpus ) )
    {
        /*
         * Shortcut for the single EMT case.
         *
         * We also ends up here if EMT(0) (or others) tries to issue a rendezvous
         * during vmR3Destroy after other emulation threads have started terminating.
         */
        if (!pVCpu->vmm.s.fInRendezvous)
        {
            Log(("VMMR3EmtRendezvous: %#x EMT (uni)\n", fFlags));
            pVCpu->vmm.s.fInRendezvous  = true;
            pVM->vmm.s.fRendezvousFlags = fFlags;
            rcStrict = pfnRendezvous(pVM, pVCpu, pvUser);
            pVCpu->vmm.s.fInRendezvous  = false;
        }
        else
        {
            /* Recursion. Do the same checks as in the SMP case. */
            Log(("VMMR3EmtRendezvous: %#x EMT (uni), recursion depth=%d\n", fFlags, pVM->vmm.s.cRendezvousRecursions));
            uint32_t fType = pVM->vmm.s.fRendezvousFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK;
            AssertLogRelReturn(   !pVCpu->vmm.s.fInRendezvous
                               || fType == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                               || fType == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING
                               || fType == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE
                               || fType == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE
                               , VERR_DEADLOCK);

            AssertLogRelReturn(pVM->vmm.s.cRendezvousRecursions < 3, VERR_DEADLOCK);
            pVM->vmm.s.cRendezvousRecursions++;
            uint32_t const fParentFlags = pVM->vmm.s.fRendezvousFlags;
            pVM->vmm.s.fRendezvousFlags = fFlags;

            rcStrict = pfnRendezvous(pVM, pVCpu, pvUser);

            pVM->vmm.s.fRendezvousFlags = fParentFlags;
            pVM->vmm.s.cRendezvousRecursions--;
        }
        Log(("VMMR3EmtRendezvous: %#x EMT (uni) returns %Rrc\n", fFlags, VBOXSTRICTRC_VAL(rcStrict)));
    }
    else
    {
        /*
         * Spin lock. If busy, check for recursion, if not recursing wait for
         * the other EMT to finish while keeping a lookout for the RENDEZVOUS FF.
         */
        int rc;
        rcStrict = VINF_SUCCESS;
        if (RT_UNLIKELY(!ASMAtomicCmpXchgU32(&pVM->vmm.s.u32RendezvousLock, 0x77778888, 0)))
        {
            /* Allow recursion in some cases. */
            if (   pVCpu->vmm.s.fInRendezvous
                && (   (pVM->vmm.s.fRendezvousFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING
                    || (pVM->vmm.s.fRendezvousFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING
                    || (pVM->vmm.s.fRendezvousFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE
                    || (pVM->vmm.s.fRendezvousFlags & VMMEMTRENDEZVOUS_FLAGS_TYPE_MASK) == VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE
                       ))
                return VBOXSTRICTRC_TODO(vmmR3EmtRendezvousRecursive(pVM, pVCpu, fFlags, pfnRendezvous, pvUser));

            AssertLogRelMsgReturn(!pVCpu->vmm.s.fInRendezvous, ("fRendezvousFlags=%#x\n", pVM->vmm.s.fRendezvousFlags),
                                  VERR_DEADLOCK);

            Log(("VMMR3EmtRendezvous: %#x EMT#%u, waiting for lock...\n", fFlags, pVCpu->idCpu));
            while (!ASMAtomicCmpXchgU32(&pVM->vmm.s.u32RendezvousLock, 0x77778888, 0))
            {
                if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
                {
                    rc = VMMR3EmtRendezvousFF(pVM, pVCpu);
                    if (    rc != VINF_SUCCESS
                        &&  (   rcStrict == VINF_SUCCESS
                             || rcStrict > rc))
                        rcStrict = rc;
                    /** @todo Perhaps deal with termination here?  */
                }
                ASMNopPause();
            }
        }

        Log(("VMMR3EmtRendezvous: %#x EMT#%u\n", fFlags, pVCpu->idCpu));
        Assert(!VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS));
        Assert(!pVCpu->vmm.s.fInRendezvous);
        pVCpu->vmm.s.fInRendezvous = true;

        /*
         * Clear the slate and setup the rendezvous. This is a semaphore ping-pong orgy. :-)
         */
        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            rc = RTSemEventWait(pVM->vmm.s.pahEvtRendezvousEnterOrdered[i], 0);
            AssertLogRelMsg(rc == VERR_TIMEOUT || rc == VINF_SUCCESS, ("%Rrc\n", rc));
        }
        rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousEnterOneByOne, 0);         AssertLogRelMsg(rc == VERR_TIMEOUT || rc == VINF_SUCCESS, ("%Rrc\n", rc));
        rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousEnterAllAtOnce);  AssertLogRelRC(rc);
        rc = RTSemEventMultiReset(pVM->vmm.s.hEvtMulRendezvousDone);            AssertLogRelRC(rc);
        rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousDoneCaller, 0);            AssertLogRelMsg(rc == VERR_TIMEOUT || rc == VINF_SUCCESS, ("%Rrc\n", rc));
        ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsEntered, 0);
        ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsDone, 0);
        ASMAtomicWriteU32(&pVM->vmm.s.cRendezvousEmtsReturned, 0);
        ASMAtomicWriteS32(&pVM->vmm.s.i32RendezvousStatus, VINF_SUCCESS);
        ASMAtomicWritePtr((void * volatile *)&pVM->vmm.s.pfnRendezvous, (void *)(uintptr_t)pfnRendezvous);
        ASMAtomicWritePtr(&pVM->vmm.s.pvRendezvousUser, pvUser);
        ASMAtomicWriteU32(&pVM->vmm.s.fRendezvousFlags, fFlags);

        /*
         * Set the FF and poke the other EMTs.
         */
        VM_FF_SET(pVM, VM_FF_EMT_RENDEZVOUS);
        VMR3NotifyGlobalFFU(pVM->pUVM, VMNOTIFYFF_FLAGS_POKE);

        /*
         * Do the same ourselves.
         */
        VBOXSTRICTRC rcStrict2 = vmmR3EmtRendezvousCommon(pVM, pVCpu, true /* fIsCaller */, fFlags, pfnRendezvous, pvUser);

        /*
         * The caller waits for the other EMTs to be done and return before doing
         * the cleanup. This makes away with wakeup / reset races we would otherwise
         * risk in the multiple release event semaphore code (hEvtRendezvousDoneCaller).
         */
        for (;;)
        {
            rc = RTSemEventWait(pVM->vmm.s.hEvtRendezvousDoneCaller, RT_INDEFINITE_WAIT);
            AssertLogRelRC(rc);
            if (!pVM->vmm.s.fRendezvousRecursion)
                break;
            rcStrict2 = vmmR3EmtRendezvousCommonRecursion(pVM, pVCpu, rcStrict2);
        }

        /*
         * Get the return code and clean up a little bit.
         */
        VBOXSTRICTRC rcStrict3 = pVM->vmm.s.i32RendezvousStatus;
        ASMAtomicWriteNullPtr((void * volatile *)&pVM->vmm.s.pfnRendezvous);

        ASMAtomicWriteU32(&pVM->vmm.s.u32RendezvousLock, 0);
        pVCpu->vmm.s.fInRendezvous = false;

        /*
         * Merge rcStrict, rcStrict2 and rcStrict3.
         */
        AssertRC(VBOXSTRICTRC_VAL(rcStrict));
        AssertRC(VBOXSTRICTRC_VAL(rcStrict2));
        if (    rcStrict2 != VINF_SUCCESS
            &&  (   rcStrict == VINF_SUCCESS
                 || rcStrict > rcStrict2))
            rcStrict = rcStrict2;
        if (    rcStrict3 != VINF_SUCCESS
            &&  (   rcStrict == VINF_SUCCESS
                 || rcStrict > rcStrict3))
            rcStrict = rcStrict3;
        Log(("VMMR3EmtRendezvous: %#x EMT#%u returns %Rrc\n", fFlags, pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict)));
    }

    AssertLogRelMsgReturn(   rcStrict <= VINF_SUCCESS
                          || (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST),
                          ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)),
                          VERR_IPE_UNEXPECTED_INFO_STATUS);
    return VBOXSTRICTRC_VAL(rcStrict);
}


/**
 * Interface for vmR3SetHaltMethodU.
 *
 * @param   pVCpu                   The cross context virtual CPU structure of the
 *                                  calling EMT.
 * @param   fMayHaltInRing0         The new state.
 * @param   cNsSpinBlockThreshold   The spin-vs-blocking threashold.
 * @thread  EMT(pVCpu)
 *
 * @todo    Move the EMT handling to VMM (or EM).  I soooooo regret that VM
 *          component.
 */
VMMR3_INT_DECL(void) VMMR3SetMayHaltInRing0(PVMCPU pVCpu, bool fMayHaltInRing0, uint32_t cNsSpinBlockThreshold)
{
    LogFlow(("VMMR3SetMayHaltInRing0(#%u, %d, %u)\n", pVCpu->idCpu, fMayHaltInRing0, cNsSpinBlockThreshold));
    pVCpu->vmm.s.fMayHaltInRing0       = fMayHaltInRing0;
    pVCpu->vmm.s.cNsSpinBlockThreshold = cNsSpinBlockThreshold;
}


/**
 * Read from the ring 0 jump buffer stack.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           The ID of the source CPU context (for the address).
 * @param   R0Addr          Where to start reading.
 * @param   pvBuf           Where to store the data we've read.
 * @param   cbRead          The number of bytes to read.
 */
VMMR3_INT_DECL(int) VMMR3ReadR0Stack(PVM pVM, VMCPUID idCpu, RTHCUINTPTR R0Addr, void *pvBuf, size_t cbRead)
{
    PVMCPU pVCpu = VMMGetCpuById(pVM, idCpu);
    AssertReturn(pVCpu, VERR_INVALID_PARAMETER);
    AssertReturn(cbRead < ~(size_t)0 / 2, VERR_INVALID_PARAMETER);

    /*
     * Hopefully we've got all the requested bits.  If not supply what we
     * can and zero the remaining stuff.
     */
    RTHCUINTPTR off = R0Addr - pVCpu->vmm.s.AssertJmpBuf.UnwindSp;
    if (off < pVCpu->vmm.s.AssertJmpBuf.cbStackValid)
    {
        size_t const cbValid = pVCpu->vmm.s.AssertJmpBuf.cbStackValid - off;
        if (cbRead <= cbValid)
        {
            memcpy(pvBuf, &pVCpu->vmm.s.abAssertStack[off], cbRead);
            return VINF_SUCCESS;
        }

        memcpy(pvBuf, &pVCpu->vmm.s.abAssertStack[off], cbValid);
        RT_BZERO((uint8_t *)pvBuf + cbValid, cbRead - cbValid);
    }
    else
        RT_BZERO(pvBuf, cbRead);

    /*
     * Supply the setjmp return RIP/EIP if requested.
     */
    if (   pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation + sizeof(RTR0UINTPTR) > R0Addr
        && pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation < R0Addr + cbRead)
    {
        uint8_t const  *pbSrc  = (uint8_t const *)&pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcValue;
        size_t          cbSrc  = sizeof(pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcValue);
        size_t          offDst = 0;
        if (R0Addr < pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation)
            offDst = pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation - R0Addr;
        else if (R0Addr > pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation)
        {
            size_t offSrc = R0Addr - pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation;
            Assert(offSrc < cbSrc);
            pbSrc -= offSrc;
            cbSrc -= offSrc;
        }
        if (cbSrc > cbRead - offDst)
            cbSrc = cbRead - offDst;
        memcpy((uint8_t *)pvBuf + offDst, pbSrc, cbSrc);

        //if (cbSrc == cbRead)
        //    rc = VINF_SUCCESS;
    }

    return VINF_SUCCESS;
}


/**
 * Used by the DBGF stack unwinder to initialize the register state.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of the CPU being unwound.
 * @param   pState          The unwind state to initialize.
 */
VMMR3_INT_DECL(void) VMMR3InitR0StackUnwindState(PUVM pUVM, VMCPUID idCpu, struct RTDBGUNWINDSTATE *pState)
{
    PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, idCpu);
    AssertReturnVoid(pVCpu);

    /*
     * This is all we really need here if we had proper unwind info (win64 only)...
     */
    pState->u.x86.auRegs[X86_GREG_xBP] = pVCpu->vmm.s.AssertJmpBuf.UnwindBp;
    pState->u.x86.auRegs[X86_GREG_xSP] = pVCpu->vmm.s.AssertJmpBuf.UnwindSp;
    pState->uPc                        = pVCpu->vmm.s.AssertJmpBuf.UnwindPc;

    /*
     * Locate the resume point on the stack.
     */
#ifdef RT_ARCH_AMD64
    /* This code must match the vmmR0CallRing3LongJmp stack frame setup in VMMR0JmpA-amd64.asm exactly. */
    uintptr_t off = 0;
# ifdef RT_OS_WINDOWS
    off += 0xa0; /* XMM6 thru XMM15 */
# endif
    pState->u.x86.uRFlags              = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_xBX] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
# ifdef RT_OS_WINDOWS
    pState->u.x86.auRegs[X86_GREG_xSI] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_xDI] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
# endif
    pState->u.x86.auRegs[X86_GREG_x12] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_x13] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_x14] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_x15] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->u.x86.auRegs[X86_GREG_xBP] = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 8;
    pState->uPc                        = *(uint64_t const *)&pVCpu->vmm.s.abAssertStack[off];
    pState->u.x86.auRegs[X86_GREG_xSP] = pVCpu->vmm.s.AssertJmpBuf.UnwindRetSp;

#elif defined(RT_ARCH_X86)
    /* This code must match the vmmR0CallRing3LongJmp stack frame setup in VMMR0JmpA-x86.asm exactly. */
    uintptr_t off = 0;
    pState->u.x86.uRFlags              = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 4;
    pState->u.x86.auRegs[X86_GREG_xBX] = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 4;
    pState->u.x86.auRegs[X86_GREG_xSI] = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 4;
    pState->u.x86.auRegs[X86_GREG_xDI] = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 4;
    pState->u.x86.auRegs[X86_GREG_xBP] = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    off += 4;
    pState->uPc                        = *(uint32_t const *)&pVCpu->vmm.s.abAssertStack[off];
    pState->u.x86.auRegs[X86_GREG_xSP] = pVCpu->vmm.s.AssertJmpBuf.UnwindRetSp;

#elif defined(RT_ARCH_ARM64)
    /** @todo PORTME: arm ring-0  */

#else
# error "Port me"
#endif
}


/**
 * Wrapper for SUPR3CallVMMR0Ex which will deal with VINF_VMM_CALL_HOST returns.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   uOperation  Operation to execute.
 * @param   u64Arg      Constant argument.
 * @param   pReqHdr     Pointer to a request header. See SUPR3CallVMMR0Ex for
 *                      details.
 */
VMMR3DECL(int) VMMR3CallR0(PVM pVM, uint32_t uOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);
    return VMMR3CallR0Emt(pVM, pVCpu, (VMMR0OPERATION)uOperation, u64Arg, pReqHdr);
}


/**
 * Wrapper for SUPR3CallVMMR0Ex which will deal with VINF_VMM_CALL_HOST returns.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context VM structure.
 * @param   enmOperation    Operation to execute.
 * @param   u64Arg          Constant argument.
 * @param   pReqHdr         Pointer to a request header. See SUPR3CallVMMR0Ex for
 *                          details.
 */
VMMR3_INT_DECL(int) VMMR3CallR0Emt(PVM pVM, PVMCPU pVCpu, VMMR0OPERATION enmOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr)
{
    /*
     * Call ring-0.
     */
#ifdef NO_SUPCALLR0VMM
    int rc = VERR_GENERAL_FAILURE;
#else
    int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), pVCpu->idCpu, enmOperation, u64Arg, pReqHdr);
#endif

    /*
     * Flush the logs and deal with ring-0 assertions.
     */
#ifdef LOG_ENABLED
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.Logger, NULL);
#endif
    VMM_FLUSH_R0_LOG(pVM, pVCpu, &pVCpu->vmm.s.u.s.RelLogger, RTLogRelGetDefaultInstance());
    if (rc != VERR_VMM_RING0_ASSERTION)
    {
        AssertLogRelMsgReturn(rc == VINF_SUCCESS || RT_FAILURE(rc),
                              ("enmOperation=%u rc=%Rrc\n", enmOperation, rc),
                              VERR_IPE_UNEXPECTED_INFO_STATUS);
        return rc;
    }
    return vmmR3HandleRing0Assert(pVM, pVCpu);
}


/**
 * Logs a ring-0 assertion ASAP after returning to ring-3.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int vmmR3HandleRing0Assert(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVCpu);
    LogRel(("%s", pVM->vmm.s.szRing0AssertMsg1));
    LogRel(("%s", pVM->vmm.s.szRing0AssertMsg2));
    return VERR_VMM_RING0_ASSERTION;
}


/**
 * Displays the Force action Flags.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The additional arguments (ignored).
 */
static DECLCALLBACK(void) vmmR3InfoFF(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    int         c;
    uint32_t    f;
    NOREF(pszArgs);

#define PRINT_FLAG(prf,flag) do { \
        if (f & (prf##flag)) \
        { \
            static const char *s_psz = #flag; \
            if (!(c % 6)) \
                pHlp->pfnPrintf(pHlp, "%s\n    %s", c ? "," : "", s_psz); \
            else \
                pHlp->pfnPrintf(pHlp, ", %s", s_psz); \
            c++; \
            f &= ~(prf##flag); \
        } \
    } while (0)

#define PRINT_GROUP(prf,grp,sfx) do { \
        if (f & (prf##grp##sfx)) \
        { \
            static const char *s_psz = #grp; \
            if (!(c % 5)) \
                pHlp->pfnPrintf(pHlp, "%s    %s", c ? ",\n" : "  Groups:\n", s_psz); \
            else \
                pHlp->pfnPrintf(pHlp, ", %s", s_psz); \
            c++; \
        } \
    } while (0)

    /*
     * The global flags.
     */
    const uint32_t fGlobalForcedActions = pVM->fGlobalForcedActions;
    pHlp->pfnPrintf(pHlp, "Global FFs: %#RX32", fGlobalForcedActions);

    /* show the flag mnemonics  */
    c = 0;
    f = fGlobalForcedActions;
    PRINT_FLAG(VM_FF_,TM_VIRTUAL_SYNC);
    PRINT_FLAG(VM_FF_,PDM_QUEUES);
    PRINT_FLAG(VM_FF_,PDM_DMA);
    PRINT_FLAG(VM_FF_,DBGF);
    PRINT_FLAG(VM_FF_,REQUEST);
    PRINT_FLAG(VM_FF_,CHECK_VM_STATE);
    PRINT_FLAG(VM_FF_,RESET);
    PRINT_FLAG(VM_FF_,EMT_RENDEZVOUS);
    PRINT_FLAG(VM_FF_,PGM_NEED_HANDY_PAGES);
    PRINT_FLAG(VM_FF_,PGM_NO_MEMORY);
    PRINT_FLAG(VM_FF_,PGM_POOL_FLUSH_PENDING);
    PRINT_FLAG(VM_FF_,DEBUG_SUSPEND);
    if (f)
        pHlp->pfnPrintf(pHlp, "%s\n    Unknown bits: %#RX32\n", c ? "," : "", f);
    else
        pHlp->pfnPrintf(pHlp, "\n");

    /* the groups */
    c = 0;
    f = fGlobalForcedActions;
    PRINT_GROUP(VM_FF_,EXTERNAL_SUSPENDED,_MASK);
    PRINT_GROUP(VM_FF_,EXTERNAL_HALTED,_MASK);
    PRINT_GROUP(VM_FF_,HIGH_PRIORITY_PRE,_MASK);
    PRINT_GROUP(VM_FF_,HIGH_PRIORITY_PRE_RAW,_MASK);
    PRINT_GROUP(VM_FF_,HIGH_PRIORITY_POST,_MASK);
    PRINT_GROUP(VM_FF_,NORMAL_PRIORITY_POST,_MASK);
    PRINT_GROUP(VM_FF_,NORMAL_PRIORITY,_MASK);
    PRINT_GROUP(VM_FF_,ALL_REM,_MASK);
    if (c)
        pHlp->pfnPrintf(pHlp, "\n");

    /*
     * Per CPU flags.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU         pVCpu               = pVM->apCpusR3[i];
        const uint64_t fLocalForcedActions = pVCpu->fLocalForcedActions;
        pHlp->pfnPrintf(pHlp, "CPU %u FFs: %#RX64", i, fLocalForcedActions);

        /* show the flag mnemonics */
        c = 0;
        f = fLocalForcedActions;
#if defined(VBOX_VMM_TARGET_ARMV8)
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_IRQ);
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_FIQ);
#else
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_APIC);
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_PIC);
#endif
        PRINT_FLAG(VMCPU_FF_,TIMER);
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_NMI);
        PRINT_FLAG(VMCPU_FF_,INTERRUPT_SMI);
        PRINT_FLAG(VMCPU_FF_,PDM_CRITSECT);
        PRINT_FLAG(VMCPU_FF_,UNHALT);
        PRINT_FLAG(VMCPU_FF_,IEM);
        PRINT_FLAG(VMCPU_FF_,UPDATE_APIC);
        PRINT_FLAG(VMCPU_FF_,DBGF);
        PRINT_FLAG(VMCPU_FF_,REQUEST);
        PRINT_FLAG(VMCPU_FF_,HM_UPDATE_CR3);
        PRINT_FLAG(VMCPU_FF_,PGM_SYNC_CR3);
        PRINT_FLAG(VMCPU_FF_,PGM_SYNC_CR3_NON_GLOBAL);
        PRINT_FLAG(VMCPU_FF_,TLB_FLUSH);
        PRINT_FLAG(VMCPU_FF_,TO_R3);
        PRINT_FLAG(VMCPU_FF_,IOM);
        if (f)
            pHlp->pfnPrintf(pHlp, "%s\n    Unknown bits: %#RX64\n", c ? "," : "", f);
        else
            pHlp->pfnPrintf(pHlp, "\n");

        /* the groups */
        c = 0;
        f = fLocalForcedActions;
        PRINT_GROUP(VMCPU_FF_,EXTERNAL_SUSPENDED,_MASK);
        PRINT_GROUP(VMCPU_FF_,EXTERNAL_HALTED,_MASK);
        PRINT_GROUP(VMCPU_FF_,HIGH_PRIORITY_PRE,_MASK);
        PRINT_GROUP(VMCPU_FF_,HIGH_PRIORITY_PRE_RAW,_MASK);
        PRINT_GROUP(VMCPU_FF_,HIGH_PRIORITY_POST,_MASK);
        PRINT_GROUP(VMCPU_FF_,NORMAL_PRIORITY_POST,_MASK);
        PRINT_GROUP(VMCPU_FF_,NORMAL_PRIORITY,_MASK);
        PRINT_GROUP(VMCPU_FF_,RESUME_GUEST,_MASK);
        PRINT_GROUP(VMCPU_FF_,HM_TO_R3,_MASK);
        PRINT_GROUP(VMCPU_FF_,ALL_REM,_MASK);
        if (c)
            pHlp->pfnPrintf(pHlp, "\n");
    }

#undef PRINT_FLAG
#undef PRINT_GROUP
}

