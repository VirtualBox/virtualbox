/* $Id: VMMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - Host Context Ring 0.
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
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pgm.h>
#ifdef VBOX_WITH_NEM_R0
# include <VBox/vmm/nem.h>
#endif
#include <VBox/vmm/em.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/tm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvm.h>
#ifdef VBOX_WITH_PCI_PASSTHROUGH
# include <VBox/vmm/pdmpci.h>
#endif
#include <VBox/vmm/apic.h>

#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/gmm.h>
#include <VBox/vmm/gim.h>
#include <VBox/intnet.h>
#include <VBox/vmm/hm.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/log.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/mp.h>
#include <iprt/once.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/timer.h>
#include <iprt/time.h>

#include "dtrace/VBoxVMM.h"


#if defined(_MSC_VER) && defined(RT_ARCH_AMD64) /** @todo check this with with VC7! */
#  pragma intrinsic(_AddressOfReturnAddress)
#endif

#if defined(RT_OS_DARWIN) && ARCH_BITS == 32
# error "32-bit darwin is no longer supported. Go back to 4.3 or earlier!"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
#if defined(RT_ARCH_X86) && (defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD))
extern uint64_t __udivdi3(uint64_t, uint64_t);
extern uint64_t __umoddi3(uint64_t, uint64_t);
#endif
RT_C_DECLS_END
static int  vmmR0UpdateLoggers(PGVM pGVM, VMCPUID idCpu, PVMMR0UPDATELOGGERSREQ pReq, uint64_t fFlags);
static int  vmmR0LogFlusher(PGVM pGVM);
static int  vmmR0LogWaitFlushed(PGVM pGVM, VMCPUID idCpu, size_t idxLogger);
static int  vmmR0InitLoggers(PGVM pGVM);
static void vmmR0CleanupLoggers(PGVM pGVM);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Drag in necessary library bits.
 * The runtime lives here (in VMMR0.r0) and VBoxDD*R0.r0 links against us. */
struct CLANG11WEIRDNOTHROW { PFNRT pfn; } g_VMMR0Deps[] =
{
    { (PFNRT)RTCrc32 },
    { (PFNRT)RTOnce },
#if defined(RT_ARCH_X86) && (defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD))
    { (PFNRT)__udivdi3 },
    { (PFNRT)__umoddi3 },
#endif
    { NULL }
};

#ifdef RT_OS_SOLARIS
/* Dependency information for the native solaris loader. */
extern "C" { char _depends_on[] = "vboxdrv"; }
#endif


/**
 * Initialize the module.
 * This is called when we're first loaded.
 *
 * @returns 0 on success.
 * @returns VBox status on failure.
 * @param   hMod        Image handle for use in APIs.
 */
DECLEXPORT(int) ModuleInit(void *hMod)
{
#ifdef VBOX_WITH_DTRACE_R0
    /*
     * The first thing to do is register the static tracepoints.
     * (Deregistration is automatic.)
     */
    int rc2 = SUPR0TracerRegisterModule(hMod, &g_VTGObjHeader);
    if (RT_FAILURE(rc2))
        return rc2;
#endif
    LogFlow(("ModuleInit:\n"));

#ifdef VBOX_WITH_64ON32_CMOS_DEBUG
    /*
     * Display the CMOS debug code.
     */
    ASMOutU8(0x72, 0x03);
    uint8_t bDebugCode = ASMInU8(0x73);
    LogRel(("CMOS Debug Code: %#x (%d)\n", bDebugCode, bDebugCode));
    RTLogComPrintf("CMOS Debug Code: %#x (%d)\n", bDebugCode, bDebugCode);
#endif

    /*
     * Initialize the VMM, GVMM, GMM, HM, PGM (Darwin) and INTNET.
     */
    int rc = vmmInitFormatTypes();
    if (RT_SUCCESS(rc))
    {
        rc = GVMMR0Init();
        if (RT_SUCCESS(rc))
        {
            rc = GMMR0Init();
            if (RT_SUCCESS(rc))
            {
                rc = HMR0Init();
                if (RT_SUCCESS(rc))
                {
                    PDMR0Init(hMod);

                    rc = PGMRegisterStringFormatTypes();
                    if (RT_SUCCESS(rc))
                    {
                        rc = IntNetR0Init();
                        if (RT_SUCCESS(rc))
                        {
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                            rc = PciRawR0Init();
#endif
                            if (RT_SUCCESS(rc))
                            {
                                rc = CPUMR0ModuleInit();
                                if (RT_SUCCESS(rc))
                                {
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
                                    rc = vmmR0TripleFaultHackInit();
                                    if (RT_SUCCESS(rc))
#endif
                                    {
#ifdef VBOX_WITH_NEM_R0
                                        rc = NEMR0Init();
                                        if (RT_SUCCESS(rc))
#endif
                                        {
                                            LogFlow(("ModuleInit: returns success\n"));
                                            return VINF_SUCCESS;
                                        }
                                    }

                                    /*
                                     * Bail out.
                                     */
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
                                    vmmR0TripleFaultHackTerm();
#endif
                                }
                                else
                                    LogRel(("ModuleInit: CPUMR0ModuleInit -> %Rrc\n", rc));
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                                PciRawR0Term();
#endif
                            }
                            else
                                LogRel(("ModuleInit: PciRawR0Init -> %Rrc\n", rc));
                            IntNetR0Term();
                        }
                        else
                            LogRel(("ModuleInit: IntNetR0Init -> %Rrc\n", rc));
                        PGMDeregisterStringFormatTypes();
                    }
                    else
                        LogRel(("ModuleInit: PGMRegisterStringFormatTypes -> %Rrc\n", rc));
                    HMR0Term();
                }
                else
                    LogRel(("ModuleInit: HMR0Init -> %Rrc\n", rc));
                GMMR0Term();
            }
            else
                LogRel(("ModuleInit: GMMR0Init -> %Rrc\n", rc));
            GVMMR0Term();
        }
        else
            LogRel(("ModuleInit: GVMMR0Init -> %Rrc\n", rc));
        vmmTermFormatTypes();
    }
    else
        LogRel(("ModuleInit: vmmInitFormatTypes -> %Rrc\n", rc));

    LogFlow(("ModuleInit: failed %Rrc\n", rc));
    return rc;
}


/**
 * Terminate the module.
 * This is called when we're finally unloaded.
 *
 * @param   hMod        Image handle for use in APIs.
 */
DECLEXPORT(void) ModuleTerm(void *hMod)
{
    NOREF(hMod);
    LogFlow(("ModuleTerm:\n"));

    /*
     * Terminate the CPUM module (Local APIC cleanup).
     */
    CPUMR0ModuleTerm();

    /*
     * Terminate the internal network service.
     */
    IntNetR0Term();

    /*
     * PGM (Darwin), HM and PciRaw global cleanup.
     */
#ifdef VBOX_WITH_PCI_PASSTHROUGH
    PciRawR0Term();
#endif
    PGMDeregisterStringFormatTypes();
    HMR0Term();
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
    vmmR0TripleFaultHackTerm();
#endif
#ifdef VBOX_WITH_NEM_R0
    NEMR0Term();
#endif

    /*
     * Destroy the GMM and GVMM instances.
     */
    GMMR0Term();
    GVMMR0Term();

    vmmTermFormatTypes();
    RTTermRunCallbacks(RTTERMREASON_UNLOAD, 0);

    LogFlow(("ModuleTerm: returns\n"));
}


/**
 * Initializes VMM specific members when the GVM structure is created,
 * allocating loggers and stuff.
 *
 * The loggers are allocated here so that we can update their settings before
 * doing VMMR0_DO_VMMR0_INIT and have correct logging at that time.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 */
VMMR0_INT_DECL(int) VMMR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(sizeof(pGVM->vmmr0.s) <= sizeof(pGVM->vmmr0.padding));

    /*
     * Initialize all members first.
     */
    pGVM->vmmr0.s.fCalledInitVm             = false;
    pGVM->vmmr0.s.hMemObjLogger             = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMapObjLogger             = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMemObjReleaseLogger      = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMapObjReleaseLogger      = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.LogFlusher.hSpinlock      = NIL_RTSPINLOCK;
    pGVM->vmmr0.s.LogFlusher.hThread        = NIL_RTNATIVETHREAD;
    pGVM->vmmr0.s.LogFlusher.hEvent         = NIL_RTSEMEVENT;
    pGVM->vmmr0.s.LogFlusher.idxRingHead    = 0;
    pGVM->vmmr0.s.LogFlusher.idxRingTail    = 0;
    pGVM->vmmr0.s.LogFlusher.fThreadWaiting = false;

    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
        Assert(pGVCpu->idHostCpu == NIL_RTCPUID);
        Assert(pGVCpu->iHostCpuSet == UINT32_MAX);
        pGVCpu->vmmr0.s.pPreemptState               = NULL;
        pGVCpu->vmmr0.s.hCtxHook                    = NIL_RTTHREADCTXHOOK;
        pGVCpu->vmmr0.s.AssertJmpBuf.pMirrorBuf     = &pGVCpu->vmm.s.AssertJmpBuf;
        pGVCpu->vmmr0.s.AssertJmpBuf.pvStackBuf     = &pGVCpu->vmm.s.abAssertStack[0];
        pGVCpu->vmmr0.s.AssertJmpBuf.cbStackBuf     = sizeof(pGVCpu->vmm.s.abAssertStack);

        for (size_t iLogger = 0; iLogger < RT_ELEMENTS(pGVCpu->vmmr0.s.u.aLoggers); iLogger++)
            pGVCpu->vmmr0.s.u.aLoggers[iLogger].hEventFlushWait = NIL_RTSEMEVENT;
    }

    /*
     * Create the loggers.
     */
    return vmmR0InitLoggers(pGVM);
}


/**
 * Initiates the R0 driver for a particular VM instance.
 *
 * @returns VBox status code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   uSvnRev     The SVN revision of the ring-3 part.
 * @param   uBuildType  Build type indicator.
 * @thread  EMT(0)
 */
static int vmmR0InitVM(PGVM pGVM, uint32_t uSvnRev, uint32_t uBuildType)
{
    /*
     * Match the SVN revisions and build type.
     */
    if (uSvnRev != VMMGetSvnRev())
    {
        LogRel(("VMMR0InitVM: Revision mismatch, r3=%d r0=%d\n", uSvnRev, VMMGetSvnRev()));
        SUPR0Printf("VMMR0InitVM: Revision mismatch, r3=%d r0=%d\n", uSvnRev, VMMGetSvnRev());
        return VERR_VMM_R0_VERSION_MISMATCH;
    }
    if (uBuildType != vmmGetBuildType())
    {
        LogRel(("VMMR0InitVM: Build type mismatch, r3=%#x r0=%#x\n", uBuildType, vmmGetBuildType()));
        SUPR0Printf("VMMR0InitVM: Build type mismatch, r3=%#x r0=%#x\n", uBuildType, vmmGetBuildType());
        return VERR_VMM_R0_VERSION_MISMATCH;
    }

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0 /*idCpu*/);
    if (RT_FAILURE(rc))
        return rc;

    /* Don't allow this to be called more than once. */
    if (!pGVM->vmmr0.s.fCalledInitVm)
        pGVM->vmmr0.s.fCalledInitVm = true;
    else
        return VERR_ALREADY_INITIALIZED;

#ifdef LOG_ENABLED

    /*
     * Register the EMT R0 logger instance for VCPU 0.
     */
    PVMCPUCC pVCpu = VMCC_GET_CPU_0(pGVM);
    if (pVCpu->vmmr0.s.u.s.Logger.pLogger)
    {
# if 0 /* testing of the logger. */
        LogCom(("vmmR0InitVM: before %p\n", RTLogDefaultInstance()));
        LogCom(("vmmR0InitVM: pfnFlush=%p actual=%p\n", pR0Logger->Logger.pfnFlush, vmmR0LoggerFlush));
        LogCom(("vmmR0InitVM: pfnLogger=%p actual=%p\n", pR0Logger->Logger.pfnLogger, vmmR0LoggerWrapper));
        LogCom(("vmmR0InitVM: offScratch=%d fFlags=%#x fDestFlags=%#x\n", pR0Logger->Logger.offScratch, pR0Logger->Logger.fFlags, pR0Logger->Logger.fDestFlags));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p reg\n", RTLogDefaultInstance()));
        RTLogSetDefaultInstanceThread(NULL, pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p dereg\n", RTLogDefaultInstance()));

        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("vmmR0InitVM: returned successfully from direct logger call.\n"));
        pR0Logger->Logger.pfnFlush(&pR0Logger->Logger);
        LogCom(("vmmR0InitVM: returned successfully from direct flush call.\n"));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p reg2\n", RTLogDefaultInstance()));
        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("vmmR0InitVM: returned successfully from direct logger call (2). offScratch=%d\n", pR0Logger->Logger.offScratch));
        RTLogSetDefaultInstanceThread(NULL, pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p dereg2\n", RTLogDefaultInstance()));

        RTLogLoggerEx(&pR0Logger->Logger, 0, ~0U, "hello ring-0 logger (RTLogLoggerEx)\n");
        LogCom(("vmmR0InitVM: RTLogLoggerEx returned fine offScratch=%d\n", pR0Logger->Logger.offScratch));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        RTLogPrintf("hello ring-0 logger (RTLogPrintf)\n");
        LogCom(("vmmR0InitVM: RTLogPrintf returned fine offScratch=%d\n", pR0Logger->Logger.offScratch));
# endif
# ifdef VBOX_WITH_R0_LOGGING
        Log(("Switching to per-thread logging instance %p (key=%p)\n", pVCpu->vmmr0.s.u.s.Logger.pLogger, pGVM->pSession));
        RTLogSetDefaultInstanceThread(pVCpu->vmmr0.s.u.s.Logger.pLogger, (uintptr_t)pGVM->pSession);
        pVCpu->vmmr0.s.u.s.Logger.fRegistered = true;
# endif
    }
#endif /* LOG_ENABLED */

    /*
     * Check if the host supports high resolution timers or not.
     */
    if (   pGVM->vmm.s.fUsePeriodicPreemptionTimers
        && !RTTimerCanDoHighResolution())
        pGVM->vmm.s.fUsePeriodicPreemptionTimers = false;

    /*
     * Initialize the per VM data for GVMM and GMM.
     */
    rc = GVMMR0InitVM(pGVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Init HM, CPUM and PGM.
         */
        rc = HMR0InitVM(pGVM);
        if (RT_SUCCESS(rc))
        {
            rc = CPUMR0InitVM(pGVM);
            if (RT_SUCCESS(rc))
            {
                rc = PGMR0InitVM(pGVM);
                if (RT_SUCCESS(rc))
                {
                    rc = EMR0InitVM(pGVM);
                    if (RT_SUCCESS(rc))
                    {
                        rc = IEMR0InitVM(pGVM);
                        if (RT_SUCCESS(rc))
                        {
                            rc = IOMR0InitVM(pGVM);
                            if (RT_SUCCESS(rc))
                            {
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                                rc = PciRawR0InitVM(pGVM);
#endif
                                if (RT_SUCCESS(rc))
                                {
                                    rc = GIMR0InitVM(pGVM);
                                    if (RT_SUCCESS(rc))
                                    {
                                        GVMMR0DoneInitVM(pGVM);
                                        PGMR0DoneInitVM(pGVM);

                                        /*
                                         * Collect a bit of info for the VM release log.
                                         */
                                        pGVM->vmm.s.fIsPreemptPendingApiTrusty = RTThreadPreemptIsPendingTrusty();
                                        pGVM->vmm.s.fIsPreemptPossible         = RTThreadPreemptIsPossible();;
                                        return rc;

                                        /* bail out*/
                                        //GIMR0TermVM(pGVM);
                                    }
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                                    PciRawR0TermVM(pGVM);
#endif
                                }
                            }
                        }
                    }
                }
            }
            HMR0TermVM(pGVM);
        }
    }

    RTLogSetDefaultInstanceThread(NULL, (uintptr_t)pGVM->pSession);
    return rc;
}


/**
 * Does EMT specific VM initialization.
 *
 * @returns VBox status code.
 * @param   pGVM        The ring-0 VM structure.
 * @param   idCpu       The EMT that's calling.
 */
static int vmmR0InitVMEmt(PGVM pGVM, VMCPUID idCpu)
{
    /* Paranoia (caller checked these already). */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_INVALID_CPU_ID);

#if defined(LOG_ENABLED) && defined(VBOX_WITH_R0_LOGGING)
    /*
     * Registration of ring 0 loggers.
     */
    PVMCPUCC pVCpu = &pGVM->aCpus[idCpu];
    if (   pVCpu->vmmr0.s.u.s.Logger.pLogger
        && !pVCpu->vmmr0.s.u.s.Logger.fRegistered)
    {
        RTLogSetDefaultInstanceThread(pVCpu->vmmr0.s.u.s.Logger.pLogger, (uintptr_t)pGVM->pSession);
        pVCpu->vmmr0.s.u.s.Logger.fRegistered = true;
    }
#endif

    return VINF_SUCCESS;
}



/**
 * Terminates the R0 bits for a particular VM instance.
 *
 * This is normally called by ring-3 as part of the VM termination process, but
 * may alternatively be called during the support driver session cleanup when
 * the VM object is destroyed (see GVMM).
 *
 * @returns VBox status code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       Set to 0 if EMT(0) or NIL_VMCPUID if session cleanup
 *                      thread.
 * @thread  EMT(0) or session clean up thread.
 */
VMMR0_INT_DECL(int) VMMR0TermVM(PGVM pGVM, VMCPUID idCpu)
{
    /*
     * Check EMT(0) claim if we're called from userland.
     */
    if (idCpu != NIL_VMCPUID)
    {
        AssertReturn(idCpu == 0, VERR_INVALID_CPU_ID);
        int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
        if (RT_FAILURE(rc))
            return rc;
    }

#ifdef VBOX_WITH_PCI_PASSTHROUGH
    PciRawR0TermVM(pGVM);
#endif

    /*
     * Tell GVMM what we're up to and check that we only do this once.
     */
    if (GVMMR0DoingTermVM(pGVM))
    {
        GIMR0TermVM(pGVM);

        /** @todo I wish to call PGMR0PhysFlushHandyPages(pGVM, &pGVM->aCpus[idCpu])
         *        here to make sure we don't leak any shared pages if we crash... */
        HMR0TermVM(pGVM);
    }

    /*
     * Deregister the logger for this EMT.
     */
    RTLogSetDefaultInstanceThread(NULL, (uintptr_t)pGVM->pSession);

    /*
     * Start log flusher thread termination.
     */
    ASMAtomicWriteBool(&pGVM->vmmr0.s.LogFlusher.fThreadShutdown, true);
    if (pGVM->vmmr0.s.LogFlusher.hEvent != NIL_RTSEMEVENT)
        RTSemEventSignal(pGVM->vmmr0.s.LogFlusher.hEvent);

    return VINF_SUCCESS;
}


/**
 * This is called at the end of gvmmR0CleanupVM().
 *
 * @param   pGVM        The global (ring-0) VM structure.
 */
VMMR0_INT_DECL(void) VMMR0CleanupVM(PGVM pGVM)
{
    AssertCompile(NIL_RTTHREADCTXHOOK == (RTTHREADCTXHOOK)0); /* Depends on zero initialized memory working for NIL at the moment. */
    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];

        /** @todo Can we busy wait here for all thread-context hooks to be
         *        deregistered before releasing (destroying) it? Only until we find a
         *        solution for not deregistering hooks everytime we're leaving HMR0
         *        context. */
        VMMR0ThreadCtxHookDestroyForEmt(pGVCpu);
    }

    vmmR0CleanupLoggers(pGVM);
}


/**
 * An interrupt or unhalt force flag is set, deal with it.
 *
 * @returns VINF_SUCCESS (or VINF_EM_HALT).
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   uMWait                  Result from EMMonitorWaitIsActive().
 * @param   enmInterruptibility     Guest CPU interruptbility level.
 */
static int vmmR0DoHaltInterrupt(PVMCPUCC pVCpu, unsigned uMWait, CPUMINTERRUPTIBILITY enmInterruptibility)
{
    Assert(!TRPMHasTrap(pVCpu));
    Assert(   enmInterruptibility > CPUMINTERRUPTIBILITY_INVALID
           && enmInterruptibility < CPUMINTERRUPTIBILITY_END);

    /*
     * Pending interrupts w/o any SMIs or NMIs?  That the usual case.
     */
    if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
        && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI  | VMCPU_FF_INTERRUPT_NMI))
    {
        if (enmInterruptibility <= CPUMINTERRUPTIBILITY_UNRESTRAINED)
        {
            uint8_t u8Interrupt = 0;
            int rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
            Log(("vmmR0DoHaltInterrupt: CPU%d u8Interrupt=%d (%#x) rc=%Rrc\n", pVCpu->idCpu, u8Interrupt, u8Interrupt, rc));
            if (RT_SUCCESS(rc))
            {
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_UNHALT);

                rc = TRPMAssertTrap(pVCpu, u8Interrupt, TRPM_HARDWARE_INT);
                AssertRCSuccess(rc);
                STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
                return rc;
            }
        }
    }
    /*
     * SMI is not implemented yet, at least not here.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI))
    {
        Log12(("vmmR0DoHaltInterrupt: CPU%d failed #3\n", pVCpu->idCpu));
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
        return VINF_EM_HALT;
    }
    /*
     * NMI.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI))
    {
        if (enmInterruptibility < CPUMINTERRUPTIBILITY_NMI_INHIBIT)
        {
            /** @todo later. */
            Log12(("vmmR0DoHaltInterrupt: CPU%d failed #2 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
            STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
            return VINF_EM_HALT;
        }
    }
    /*
     * Nested-guest virtual interrupt.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST))
    {
        if (enmInterruptibility < CPUMINTERRUPTIBILITY_VIRT_INT_DISABLED)
        {
            /** @todo NSTVMX: NSTSVM: Remember, we might have to check and perform VM-exits
             *        here before injecting the virtual interrupt. See emR3ForcedActions
             *        for details. */
            Log12(("vmmR0DoHaltInterrupt: CPU%d failed #1 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
            STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
            return VINF_EM_HALT;
        }
    }

    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UNHALT))
    {
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
        Log11(("vmmR0DoHaltInterrupt: CPU%d success VINF_SUCCESS (UNHALT)\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }
    if (uMWait > 1)
    {
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
        Log11(("vmmR0DoHaltInterrupt: CPU%d success VINF_SUCCESS (uMWait=%u > 1)\n", pVCpu->idCpu, uMWait));
        return VINF_SUCCESS;
    }

    Log12(("vmmR0DoHaltInterrupt: CPU%d failed #0 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
    STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
    return VINF_EM_HALT;
}


/**
 * This does one round of vmR3HaltGlobal1Halt().
 *
 * The rational here is that we'll reduce latency in interrupt situations if we
 * don't go to ring-3 immediately on a VINF_EM_HALT (guest executed HLT or
 * MWAIT), but do one round of blocking here instead and hope the interrupt is
 * raised in the meanwhile.
 *
 * If we go to ring-3 we'll quit the inner HM/NEM loop in EM and end up in the
 * outer loop, which will then call VMR3WaitHalted() and that in turn will do a
 * ring-0 call (unless we're too close to a timer event).  When the interrupt
 * wakes us up, we'll return from ring-0 and EM will by instinct do a
 * rescheduling (because of raw-mode) before it resumes the HM/NEM loop and gets
 * back to VMMR0EntryFast().
 *
 * @returns VINF_SUCCESS or VINF_EM_HALT.
 * @param   pGVM        The ring-0 VM structure.
 * @param   pGVCpu      The ring-0 virtual CPU structure.
 *
 * @todo r=bird: All the blocking/waiting and EMT managment should move out of
 *       the VM module, probably to VMM.  Then this would be more weird wrt
 *       parameters and statistics.
 */
static int vmmR0DoHalt(PGVM pGVM, PGVMCPU pGVCpu)
{
    /*
     * Do spin stat historization.
     */
    if (++pGVCpu->vmm.s.cR0Halts & 0xff)
    { /* likely */ }
    else if (pGVCpu->vmm.s.cR0HaltsSucceeded > pGVCpu->vmm.s.cR0HaltsToRing3)
    {
        pGVCpu->vmm.s.cR0HaltsSucceeded = 2;
        pGVCpu->vmm.s.cR0HaltsToRing3   = 0;
    }
    else
    {
        pGVCpu->vmm.s.cR0HaltsSucceeded = 0;
        pGVCpu->vmm.s.cR0HaltsToRing3   = 2;
    }

    /*
     * Flags that makes us go to ring-3.
     */
    uint32_t const fVmFFs  = VM_FF_TM_VIRTUAL_SYNC            | VM_FF_PDM_QUEUES              | VM_FF_PDM_DMA
                           | VM_FF_DBGF                       | VM_FF_REQUEST                 | VM_FF_CHECK_VM_STATE
                           | VM_FF_RESET                      | VM_FF_EMT_RENDEZVOUS          | VM_FF_PGM_NEED_HANDY_PAGES
                           | VM_FF_PGM_NO_MEMORY              | VM_FF_DEBUG_SUSPEND;
    uint64_t const fCpuFFs = VMCPU_FF_TIMER                   | VMCPU_FF_PDM_CRITSECT         | VMCPU_FF_IEM
                           | VMCPU_FF_REQUEST                 | VMCPU_FF_DBGF                 | VMCPU_FF_HM_UPDATE_CR3
                           | VMCPU_FF_PGM_SYNC_CR3            | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL
                           | VMCPU_FF_TO_R3                   | VMCPU_FF_IOM;

    /*
     * Check preconditions.
     */
    unsigned const             uMWait              = EMMonitorWaitIsActive(pGVCpu);
    CPUMINTERRUPTIBILITY const enmInterruptibility = CPUMGetGuestInterruptibility(pGVCpu);
    if (   pGVCpu->vmm.s.fMayHaltInRing0
        && !TRPMHasTrap(pGVCpu)
        && (   enmInterruptibility == CPUMINTERRUPTIBILITY_UNRESTRAINED
            || uMWait > 1))
    {
        if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
            && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
        {
            /*
             * Interrupts pending already?
             */
            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                APICUpdatePendingInterrupts(pGVCpu);

            /*
             * Flags that wake up from the halted state.
             */
            uint64_t const fIntMask = VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_INTERRUPT_NESTED_GUEST
                                    | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_UNHALT;

            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
            ASMNopPause();

            /*
             * Check out how long till the next timer event.
             */
            uint64_t u64Delta;
            uint64_t u64GipTime = TMTimerPollGIP(pGVM, pGVCpu, &u64Delta);

            if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
            {
                if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                    APICUpdatePendingInterrupts(pGVCpu);

                if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                    return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);

                /*
                 * Wait if there is enough time to the next timer event.
                 */
                if (u64Delta >= pGVCpu->vmm.s.cNsSpinBlockThreshold)
                {
                    /* If there are few other CPU cores around, we will procrastinate a
                       little before going to sleep, hoping for some device raising an
                       interrupt or similar.   Though, the best thing here would be to
                       dynamically adjust the spin count according to its usfulness or
                       something... */
                    if (   pGVCpu->vmm.s.cR0HaltsSucceeded > pGVCpu->vmm.s.cR0HaltsToRing3
                        && RTMpGetOnlineCount() >= 4)
                    {
                        /** @todo Figure out how we can skip this if it hasn't help recently...
                         *        @bugref{9172#c12} */
                        uint32_t cSpinLoops = 42;
                        while (cSpinLoops-- > 0)
                        {
                            ASMNopPause();
                            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                APICUpdatePendingInterrupts(pGVCpu);
                            ASMNopPause();
                            if (VM_FF_IS_ANY_SET(pGVM, fVmFFs))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3FromSpin);
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
                                return VINF_EM_HALT;
                            }
                            ASMNopPause();
                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3FromSpin);
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
                                return VINF_EM_HALT;
                            }
                            ASMNopPause();
                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltExecFromSpin);
                                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                            }
                            ASMNopPause();
                        }
                    }

                    /*
                     * We have to set the state to VMCPUSTATE_STARTED_HALTED here so ring-3
                     * knows when to notify us (cannot access VMINTUSERPERVMCPU::fWait from here).
                     * After changing the state we must recheck the force flags of course.
                     */
                    if (VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED_HALTED, VMCPUSTATE_STARTED))
                    {
                        if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                            && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                        {
                            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                APICUpdatePendingInterrupts(pGVCpu);

                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                            {
                                VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                            }

                            /* Okay, block! */
                            uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
                            int rc = GVMMR0SchedHalt(pGVM, pGVCpu, u64GipTime);
                            uint64_t const u64EndSchedHalt     = RTTimeNanoTS();
                            uint64_t const cNsElapsedSchedHalt = u64EndSchedHalt - u64StartSchedHalt;
                            Log10(("vmmR0DoHalt: CPU%d: halted %llu ns\n", pGVCpu->idCpu, cNsElapsedSchedHalt));

                            VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                            STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlock, cNsElapsedSchedHalt);
                            if (   rc == VINF_SUCCESS
                                || rc == VERR_INTERRUPTED)
                            {
                                /* Keep some stats like ring-3 does. */
                                int64_t const cNsOverslept = u64EndSchedHalt - u64GipTime;
                                if (cNsOverslept > 50000)
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockOverslept, cNsOverslept);
                                else if (cNsOverslept < -50000)
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockInsomnia,  cNsElapsedSchedHalt);
                                else
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockOnTime,    cNsElapsedSchedHalt);

                                /*
                                 * Recheck whether we can resume execution or have to go to ring-3.
                                 */
                                if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                                    && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                                {
                                    if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                        APICUpdatePendingInterrupts(pGVCpu);
                                    if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                                    {
                                        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltExecFromBlock);
                                        return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                                    }
                                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PostNoInt);
                                    Log12(("vmmR0DoHalt: CPU%d post #2 - No pending interrupt\n", pGVCpu->idCpu));
                                }
                                else
                                {
                                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PostPendingFF);
                                    Log12(("vmmR0DoHalt: CPU%d post #1 - Pending FF\n", pGVCpu->idCpu));
                                }
                            }
                            else
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
                                Log12(("vmmR0DoHalt: CPU%d GVMMR0SchedHalt failed: %Rrc\n", pGVCpu->idCpu, rc));
                            }
                        }
                        else
                        {
                            VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                            STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
                            Log12(("vmmR0DoHalt: CPU%d failed #5 - Pending FF\n", pGVCpu->idCpu));
                        }
                    }
                    else
                    {
                        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
                        Log12(("vmmR0DoHalt: CPU%d failed #4 - enmState=%d\n", pGVCpu->idCpu, VMCPU_GET_STATE(pGVCpu)));
                    }
                }
                else
                {
                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3SmallDelta);
                    Log12(("vmmR0DoHalt: CPU%d failed #3 - delta too small: %RU64\n", pGVCpu->idCpu, u64Delta));
                }
            }
            else
            {
                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
                Log12(("vmmR0DoHalt: CPU%d failed #2 - Pending FF\n", pGVCpu->idCpu));
            }
        }
        else
        {
            STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
            Log12(("vmmR0DoHalt: CPU%d failed #1 - Pending FF\n", pGVCpu->idCpu));
        }
    }
    else
    {
        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
        Log12(("vmmR0DoHalt: CPU%d failed #0 - fMayHaltInRing0=%d TRPMHasTrap=%d enmInt=%d uMWait=%u\n",
               pGVCpu->idCpu, pGVCpu->vmm.s.fMayHaltInRing0, TRPMHasTrap(pGVCpu), enmInterruptibility, uMWait));
    }

    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
    return VINF_EM_HALT;
}


/**
 * VMM ring-0 thread-context callback.
 *
 * This does common HM state updating and calls the HM-specific thread-context
 * callback.
 *
 * This is used together with RTThreadCtxHookCreate() on platforms which
 * supports it, and directly from VMMR0EmtPrepareForBlocking() and
 * VMMR0EmtResumeAfterBlocking() on platforms which don't.
 *
 * @param   enmEvent    The thread-context event.
 * @param   pvUser      Opaque pointer to the VMCPU.
 *
 * @thread  EMT(pvUser)
 */
static DECLCALLBACK(void) vmmR0ThreadCtxCallback(RTTHREADCTXEVENT enmEvent, void *pvUser)
{
    PVMCPUCC pVCpu = (PVMCPUCC)pvUser;

    switch (enmEvent)
    {
        case RTTHREADCTXEVENT_IN:
        {
            /*
             * Linux may call us with preemption enabled (really!) but technically we
             * cannot get preempted here, otherwise we end up in an infinite recursion
             * scenario (i.e. preempted in resume hook -> preempt hook -> resume hook...
             * ad infinitum). Let's just disable preemption for now...
             */
            /** @todo r=bird: I don't believe the above. The linux code is clearly enabling
             *        preemption after doing the callout (one or two functions up the
             *        call chain). */
            /** @todo r=ramshankar: See @bugref{5313#c30}. */
            RTTHREADPREEMPTSTATE ParanoidPreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&ParanoidPreemptState);

            /* We need to update the VCPU <-> host CPU mapping. */
            RTCPUID idHostCpu;
            uint32_t iHostCpuSet = RTMpCurSetIndexAndId(&idHostCpu);
            pVCpu->iHostCpuSet = iHostCpuSet;
            ASMAtomicWriteU32(&pVCpu->idHostCpu, idHostCpu);

            /* In the very unlikely event that the GIP delta for the CPU we're
               rescheduled needs calculating, try force a return to ring-3.
               We unfortunately cannot do the measurements right here. */
            if (RT_LIKELY(!SUPIsTscDeltaAvailableForCpuSetIndex(iHostCpuSet)))
            { /* likely */ }
            else
                VMCPU_FF_SET(pVCpu, VMCPU_FF_TO_R3);

            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);

            /* Restore preemption. */
            RTThreadPreemptRestore(&ParanoidPreemptState);
            break;
        }

        case RTTHREADCTXEVENT_OUT:
        {
            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);

            /*
             * Sigh. See VMMGetCpu() used by VMCPU_ASSERT_EMT(). We cannot let several VCPUs
             * have the same host CPU associated with it.
             */
            pVCpu->iHostCpuSet = UINT32_MAX;
            ASMAtomicWriteU32(&pVCpu->idHostCpu, NIL_RTCPUID);
            break;
        }

        default:
            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);
            break;
    }
}


/**
 * Creates thread switching hook for the current EMT thread.
 *
 * This is called by GVMMR0CreateVM and GVMMR0RegisterVCpu.  If the host
 * platform does not implement switcher hooks, no hooks will be create and the
 * member set to NIL_RTTHREADCTXHOOK.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(int) VMMR0ThreadCtxHookCreateForEmt(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pVCpu->vmmr0.s.hCtxHook == NIL_RTTHREADCTXHOOK);

#if 1 /* To disable this stuff change to zero. */
    int rc = RTThreadCtxHookCreate(&pVCpu->vmmr0.s.hCtxHook, 0, vmmR0ThreadCtxCallback, pVCpu);
    if (RT_SUCCESS(rc))
    {
        pVCpu->pGVM->vmm.s.fIsUsingContextHooks = true;
        return rc;
    }
#else
    RT_NOREF(vmmR0ThreadCtxCallback);
    int rc = VERR_NOT_SUPPORTED;
#endif

    pVCpu->vmmr0.s.hCtxHook = NIL_RTTHREADCTXHOOK;
    pVCpu->pGVM->vmm.s.fIsUsingContextHooks = false;
    if (rc == VERR_NOT_SUPPORTED)
        return VINF_SUCCESS;

    LogRelMax(32, ("RTThreadCtxHookCreate failed! rc=%Rrc pVCpu=%p idCpu=%RU32\n", rc, pVCpu, pVCpu->idCpu));
    return VINF_SUCCESS; /* Just ignore it, we can live without context hooks. */
}


/**
 * Destroys the thread switching hook for the specified VCPU.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @remarks Can be called from any thread.
 */
VMMR0_INT_DECL(void) VMMR0ThreadCtxHookDestroyForEmt(PVMCPUCC pVCpu)
{
    int rc = RTThreadCtxHookDestroy(pVCpu->vmmr0.s.hCtxHook);
    AssertRC(rc);
    pVCpu->vmmr0.s.hCtxHook = NIL_RTTHREADCTXHOOK;
}


/**
 * Disables the thread switching hook for this VCPU (if we got one).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT(pVCpu)
 *
 * @remarks This also clears GVMCPU::idHostCpu, so the mapping is invalid after
 *          this call.  This means you have to be careful with what you do!
 */
VMMR0_INT_DECL(void) VMMR0ThreadCtxHookDisable(PVMCPUCC pVCpu)
{
    /*
     * Clear the VCPU <-> host CPU mapping as we've left HM context.
     * @bugref{7726#c19} explains the need for this trick:
     *
     *      VMXR0CallRing3Callback/SVMR0CallRing3Callback &
     *      hmR0VmxLeaveSession/hmR0SvmLeaveSession disables context hooks during
     *      longjmp & normal return to ring-3, which opens a window where we may be
     *      rescheduled without changing GVMCPUID::idHostCpu and cause confusion if
     *      the CPU starts executing a different EMT.  Both functions first disables
     *      preemption and then calls HMR0LeaveCpu which invalids idHostCpu, leaving
     *      an opening for getting preempted.
     */
    /** @todo Make HM not need this API!  Then we could leave the hooks enabled
     *        all the time. */

    /*
     * Disable the context hook, if we got one.
     */
    if (pVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
    {
        Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
        ASMAtomicWriteU32(&pVCpu->idHostCpu, NIL_RTCPUID);
        int rc = RTThreadCtxHookDisable(pVCpu->vmmr0.s.hCtxHook);
        AssertRC(rc);
    }
}


/**
 * Internal version of VMMR0ThreadCtxHooksAreRegistered.
 *
 * @returns true if registered, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(bool) vmmR0ThreadCtxHookIsEnabled(PVMCPUCC pVCpu)
{
    return RTThreadCtxHookIsEnabled(pVCpu->vmmr0.s.hCtxHook);
}


/**
 * Whether thread-context hooks are registered for this VCPU.
 *
 * @returns true if registered, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(bool) VMMR0ThreadCtxHookIsEnabled(PVMCPUCC pVCpu)
{
    return vmmR0ThreadCtxHookIsEnabled(pVCpu);
}


/**
 * Returns the ring-0 release logger instance.
 *
 * @returns Pointer to release logger, NULL if not configured.
 * @param   pVCpu       The cross context virtual CPU structure of the caller.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(PRTLOGGER) VMMR0GetReleaseLogger(PVMCPUCC pVCpu)
{
    return pVCpu->vmmr0.s.u.s.RelLogger.pLogger;
}


#ifdef VBOX_WITH_STATISTICS
/**
 * Record return code statistics
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rc          The status code.
 */
static void vmmR0RecordRC(PVMCC pVM, PVMCPUCC pVCpu, int rc)
{
    /*
     * Collect statistics.
     */
    switch (rc)
    {
        case VINF_SUCCESS:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetNormal);
            break;
        case VINF_EM_RAW_INTERRUPT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterrupt);
            break;
        case VINF_EM_RAW_INTERRUPT_HYPER:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterruptHyper);
            break;
        case VINF_EM_RAW_GUEST_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetGuestTrap);
            break;
        case VINF_EM_RAW_RING_SWITCH:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRingSwitch);
            break;
        case VINF_EM_RAW_RING_SWITCH_INT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRingSwitchInt);
            break;
        case VINF_EM_RAW_STALE_SELECTOR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetStaleSelector);
            break;
        case VINF_EM_RAW_IRET_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIRETTrap);
            break;
        case VINF_IOM_R3_IOPORT_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIORead);
            break;
        case VINF_IOM_R3_IOPORT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIOWrite);
            break;
        case VINF_IOM_R3_IOPORT_COMMIT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIOCommitWrite);
            break;
        case VINF_IOM_R3_MMIO_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIORead);
            break;
        case VINF_IOM_R3_MMIO_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOWrite);
            break;
        case VINF_IOM_R3_MMIO_COMMIT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOCommitWrite);
            break;
        case VINF_IOM_R3_MMIO_READ_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOReadWrite);
            break;
        case VINF_PATM_HC_MMIO_PATCH_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOPatchRead);
            break;
        case VINF_PATM_HC_MMIO_PATCH_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOPatchWrite);
            break;
        case VINF_CPUM_R3_MSR_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMSRRead);
            break;
        case VINF_CPUM_R3_MSR_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMSRWrite);
            break;
        case VINF_EM_RAW_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetEmulate);
            break;
        case VINF_PATCH_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchEmulate);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetLDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetGDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetTSSFault);
            break;
        case VINF_CSAM_PENDING_ACTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetCSAMTask);
            break;
        case VINF_PGM_SYNC_CR3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetSyncCR3);
            break;
        case VINF_PATM_PATCH_INT3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchInt3);
            break;
        case VINF_PATM_PATCH_TRAP_PF:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchPF);
            break;
        case VINF_PATM_PATCH_TRAP_GP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchGP);
            break;
        case VINF_PATM_PENDING_IRQ_AFTER_IRET:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchIretIRQ);
            break;
        case VINF_EM_RESCHEDULE_REM:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRescheduleREM);
            break;
        case VINF_EM_RAW_TO_R3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Total);
            if (VM_FF_IS_SET(pVM, VM_FF_TM_VIRTUAL_SYNC))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3TMVirt);
            else if (VM_FF_IS_SET(pVM, VM_FF_PGM_NEED_HANDY_PAGES))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3HandyPages);
            else if (VM_FF_IS_SET(pVM, VM_FF_PDM_QUEUES))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3PDMQueues);
            else if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Rendezvous);
            else if (VM_FF_IS_SET(pVM, VM_FF_PDM_DMA))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3DMA);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TIMER))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Timer);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PDM_CRITSECT))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3CritSect);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TO_R3))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3FF);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Iem);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IOM))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Iom);
            else
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Unknown);
            break;

        case VINF_EM_RAW_TIMER_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetTimerPending);
            break;
        case VINF_EM_RAW_INTERRUPT_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterruptPending);
            break;
        case VINF_PATM_DUPLICATE_FUNCTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPATMDuplicateFn);
            break;
        case VINF_PGM_POOL_FLUSH_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPGMFlushPending);
            break;
        case VINF_EM_PENDING_REQUEST:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPendingRequest);
            break;
        case VINF_EM_HM_PATCH_TPR_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchTPR);
            break;
        default:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMisc);
            break;
    }
}
#endif /* VBOX_WITH_STATISTICS */


/**
 * The Ring 0 entry point, called by the fast-ioctl path.
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pVMIgnored      The cross context VM structure. The return code is
 *                          stored in pVM->vmm.s.iLastGZRc.
 * @param   idCpu           The Virtual CPU ID of the calling EMT.
 * @param   enmOperation    Which operation to execute.
 * @remarks Assume called with interrupts _enabled_.
 */
VMMR0DECL(void) VMMR0EntryFast(PGVM pGVM, PVMCC pVMIgnored, VMCPUID idCpu, VMMR0OPERATION enmOperation)
{
    RT_NOREF(pVMIgnored);

    /*
     * Validation.
     */
    if (   idCpu < pGVM->cCpus
        && pGVM->cCpus == pGVM->cCpusUnsafe)
    { /*likely*/ }
    else
    {
        SUPR0Printf("VMMR0EntryFast: Bad idCpu=%#x cCpus=%#x cCpusUnsafe=%#x\n", idCpu, pGVM->cCpus, pGVM->cCpusUnsafe);
        return;
    }

    PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
    RTNATIVETHREAD const hNativeThread = RTThreadNativeSelf();
    if (RT_LIKELY(   pGVCpu->hEMT            == hNativeThread
                  && pGVCpu->hNativeThreadR0 == hNativeThread))
    { /* likely */ }
    else
    {
        SUPR0Printf("VMMR0EntryFast: Bad thread idCpu=%#x hNativeSelf=%p pGVCpu->hEmt=%p pGVCpu->hNativeThreadR0=%p\n",
                    idCpu, hNativeThread, pGVCpu->hEMT, pGVCpu->hNativeThreadR0);
        return;
    }

    /*
     * Perform requested operation.
     */
    switch (enmOperation)
    {
        /*
         * Run guest code using the available hardware acceleration technology.
         */
        case VMMR0_DO_HM_RUN:
        {
            for (;;) /* hlt loop */
            {
                /*
                 * Disable ring-3 calls & blocking till we've successfully entered HM.
                 * Otherwise we sometimes end up blocking at the finall Log4 statement
                 * in VMXR0Enter, while still in a somewhat inbetween state.
                 */
                VMMRZCallRing3Disable(pGVCpu);

                /*
                 * Disable preemption.
                 */
                Assert(!vmmR0ThreadCtxHookIsEnabled(pGVCpu));
                RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
                RTThreadPreemptDisable(&PreemptState);
                pGVCpu->vmmr0.s.pPreemptState = &PreemptState;

                /*
                 * Get the host CPU identifiers, make sure they are valid and that
                 * we've got a TSC delta for the CPU.
                 */
                RTCPUID  idHostCpu;
                uint32_t iHostCpuSet = RTMpCurSetIndexAndId(&idHostCpu);
                if (RT_LIKELY(   iHostCpuSet < RTCPUSET_MAX_CPUS
                              && SUPIsTscDeltaAvailableForCpuSetIndex(iHostCpuSet)))
                {
                    pGVCpu->iHostCpuSet = iHostCpuSet;
                    ASMAtomicWriteU32(&pGVCpu->idHostCpu, idHostCpu);

                    /*
                     * Update the periodic preemption timer if it's active.
                     */
                    if (pGVM->vmm.s.fUsePeriodicPreemptionTimers)
                        GVMMR0SchedUpdatePeriodicPreemptionTimer(pGVM, pGVCpu->idHostCpu, TMCalcHostTimerFrequency(pGVM, pGVCpu));

#ifdef VMM_R0_TOUCH_FPU
                    /*
                     * Make sure we've got the FPU state loaded so and we don't need to clear
                     * CR0.TS and get out of sync with the host kernel when loading the guest
                     * FPU state.  @ref sec_cpum_fpu (CPUM.cpp) and @bugref{4053}.
                     */
                    CPUMR0TouchHostFpu();
#endif
                    int  rc;
                    bool fPreemptRestored = false;
                    if (!HMR0SuspendPending())
                    {
                        /*
                         * Enable the context switching hook.
                         */
                        if (pGVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
                        {
                            Assert(!RTThreadCtxHookIsEnabled(pGVCpu->vmmr0.s.hCtxHook));
                            int rc2 = RTThreadCtxHookEnable(pGVCpu->vmmr0.s.hCtxHook); AssertRC(rc2);
                        }

                        /*
                         * Enter HM context.
                         */
                        rc = HMR0Enter(pGVCpu);
                        if (RT_SUCCESS(rc))
                        {
                            VMCPU_SET_STATE(pGVCpu, VMCPUSTATE_STARTED_HM);

                            /*
                             * When preemption hooks are in place, enable preemption now that
                             * we're in HM context.
                             */
                            if (vmmR0ThreadCtxHookIsEnabled(pGVCpu))
                            {
                                fPreemptRestored = true;
                                pGVCpu->vmmr0.s.pPreemptState = NULL;
                                RTThreadPreemptRestore(&PreemptState);
                            }
                            VMMRZCallRing3Enable(pGVCpu);

                            /*
                             * Setup the longjmp machinery and execute guest code (calls HMR0RunGuestCode).
                             */
                            rc = vmmR0CallRing3SetJmp(&pGVCpu->vmmr0.s.AssertJmpBuf, HMR0RunGuestCode, pGVM, pGVCpu);

                            /*
                             * Assert sanity on the way out.  Using manual assertions code here as normal
                             * assertions are going to panic the host since we're outside the setjmp/longjmp zone.
                             */
                            if (RT_UNLIKELY(   VMCPU_GET_STATE(pGVCpu) != VMCPUSTATE_STARTED_HM
                                            && RT_SUCCESS_NP(rc)
                                            && rc != VERR_VMM_RING0_ASSERTION ))
                            {
                                pGVM->vmm.s.szRing0AssertMsg1[0] = '\0';
                                RTStrPrintf(pGVM->vmm.s.szRing0AssertMsg2, sizeof(pGVM->vmm.s.szRing0AssertMsg2),
                                            "Got VMCPU state %d expected %d.\n", VMCPU_GET_STATE(pGVCpu), VMCPUSTATE_STARTED_HM);
                                rc = VERR_VMM_WRONG_HM_VMCPU_STATE;
                            }
#if 0
                            /** @todo Get rid of this. HM shouldn't disable the context hook. */
                            else if (RT_UNLIKELY(vmmR0ThreadCtxHookIsEnabled(pGVCpu)))
                            {
                                pGVM->vmm.s.szRing0AssertMsg1[0] = '\0';
                                RTStrPrintf(pGVM->vmm.s.szRing0AssertMsg2, sizeof(pGVM->vmm.s.szRing0AssertMsg2),
                                            "Thread-context hooks still enabled! VCPU=%p Id=%u rc=%d.\n", pGVCpu, pGVCpu->idCpu, rc);
                                rc = VERR_VMM_CONTEXT_HOOK_STILL_ENABLED;
                            }
#endif

                            VMMRZCallRing3Disable(pGVCpu); /* Lazy bird: Simpler just disabling it again... */
                            VMCPU_SET_STATE(pGVCpu, VMCPUSTATE_STARTED);
                        }
                        STAM_COUNTER_INC(&pGVM->vmm.s.StatRunGC);

                        /*
                         * Invalidate the host CPU identifiers before we disable the context
                         * hook / restore preemption.
                         */
                        pGVCpu->iHostCpuSet = UINT32_MAX;
                        ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);

                        /*
                         * Disable context hooks.  Due to unresolved cleanup issues, we
                         * cannot leave the hooks enabled when we return to ring-3.
                         *
                         * Note! At the moment HM may also have disabled the hook
                         *       when we get here, but the IPRT API handles that.
                         */
                        if (pGVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
                            RTThreadCtxHookDisable(pGVCpu->vmmr0.s.hCtxHook);
                    }
                    /*
                     * The system is about to go into suspend mode; go back to ring 3.
                     */
                    else
                    {
                        pGVCpu->iHostCpuSet = UINT32_MAX;
                        ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);
                        rc = VINF_EM_RAW_INTERRUPT;
                    }

                    /** @todo When HM stops messing with the context hook state, we'll disable
                     *        preemption again before the RTThreadCtxHookDisable call. */
                    if (!fPreemptRestored)
                    {
                        pGVCpu->vmmr0.s.pPreemptState = NULL;
                        RTThreadPreemptRestore(&PreemptState);
                    }

                    pGVCpu->vmm.s.iLastGZRc = rc;

                    /* Fire dtrace probe and collect statistics. */
                    VBOXVMM_R0_VMM_RETURN_TO_RING3_HM(pGVCpu, CPUMQueryGuestCtxPtr(pGVCpu), rc);
#ifdef VBOX_WITH_STATISTICS
                    vmmR0RecordRC(pGVM, pGVCpu, rc);
#endif
                    VMMRZCallRing3Enable(pGVCpu);

                    /*
                     * If this is a halt.
                     */
                    if (rc != VINF_EM_HALT)
                    { /* we're not in a hurry for a HLT, so prefer this path */ }
                    else
                    {
                        pGVCpu->vmm.s.iLastGZRc = rc = vmmR0DoHalt(pGVM, pGVCpu);
                        if (rc == VINF_SUCCESS)
                        {
                            pGVCpu->vmm.s.cR0HaltsSucceeded++;
                            continue;
                        }
                        pGVCpu->vmm.s.cR0HaltsToRing3++;
                    }
                }
                /*
                 * Invalid CPU set index or TSC delta in need of measuring.
                 */
                else
                {
                    pGVCpu->vmmr0.s.pPreemptState = NULL;
                    pGVCpu->iHostCpuSet = UINT32_MAX;
                    ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);
                    RTThreadPreemptRestore(&PreemptState);

                    VMMRZCallRing3Enable(pGVCpu);

                    if (iHostCpuSet < RTCPUSET_MAX_CPUS)
                    {
                        int rc = SUPR0TscDeltaMeasureBySetIndex(pGVM->pSession, iHostCpuSet, 0 /*fFlags*/,
                                                                2 /*cMsWaitRetry*/, 5*RT_MS_1SEC /*cMsWaitThread*/,
                                                                0 /*default cTries*/);
                        if (RT_SUCCESS(rc) || rc == VERR_CPU_OFFLINE)
                            pGVCpu->vmm.s.iLastGZRc = VINF_EM_RAW_TO_R3;
                        else
                            pGVCpu->vmm.s.iLastGZRc = rc;
                    }
                    else
                        pGVCpu->vmm.s.iLastGZRc = VERR_INVALID_CPU_INDEX;
                }
                break;
            } /* halt loop. */
            break;
        }

#ifdef VBOX_WITH_NEM_R0
# if defined(RT_ARCH_AMD64) && defined(RT_OS_WINDOWS)
        case VMMR0_DO_NEM_RUN:
        {
            /*
             * Setup the longjmp machinery and execute guest code (calls NEMR0RunGuestCode).
             */
#  ifdef VBOXSTRICTRC_STRICT_ENABLED
            int rc = vmmR0CallRing3SetJmp2(&pGVCpu->vmmr0.s.AssertJmpBuf, (PFNVMMR0SETJMP2)NEMR0RunGuestCode, pGVM, idCpu);
#  else
            int rc = vmmR0CallRing3SetJmp2(&pGVCpu->vmmr0.s.AssertJmpBuf, NEMR0RunGuestCode, pGVM, idCpu);
#  endif
            STAM_COUNTER_INC(&pGVM->vmm.s.StatRunGC);

            pGVCpu->vmm.s.iLastGZRc = rc;

            /*
             * Fire dtrace probe and collect statistics.
             */
            VBOXVMM_R0_VMM_RETURN_TO_RING3_NEM(pGVCpu, CPUMQueryGuestCtxPtr(pGVCpu), rc);
#  ifdef VBOX_WITH_STATISTICS
            vmmR0RecordRC(pGVM, pGVCpu, rc);
#  endif
            break;
        }
# endif
#endif

        /*
         * For profiling.
         */
        case VMMR0_DO_NOP:
            pGVCpu->vmm.s.iLastGZRc = VINF_SUCCESS;
            break;

        /*
         * Shouldn't happen.
         */
        default:
            AssertMsgFailed(("%#x\n", enmOperation));
            pGVCpu->vmm.s.iLastGZRc = VERR_NOT_SUPPORTED;
            break;
    }
}


/**
 * Validates a session or VM session argument.
 *
 * @returns true / false accordingly.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pClaimedSession The session claim to validate.
 * @param   pSession        The session argument.
 */
DECLINLINE(bool) vmmR0IsValidSession(PGVM pGVM, PSUPDRVSESSION pClaimedSession, PSUPDRVSESSION pSession)
{
    /* This must be set! */
    if (!pSession)
        return false;

    /* Only one out of the two. */
    if (pGVM && pClaimedSession)
        return false;
    if (pGVM)
        pClaimedSession = pGVM->pSession;
    return pClaimedSession == pSession;
}


/**
 * VMMR0EntryEx worker function, either called directly or when ever possible
 * called thru a longjmp so we can exit safely on failure.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           Virtual CPU ID argument. Must be NIL_VMCPUID if pVM
 *                          is NIL_RTR0PTR, and may be NIL_VMCPUID if it isn't
 * @param   enmOperation    Which operation to execute.
 * @param   pReqHdr         This points to a SUPVMMR0REQHDR packet. Optional.
 *                          The support driver validates this if it's present.
 * @param   u64Arg          Some simple constant argument.
 * @param   pSession        The session of the caller.
 *
 * @remarks Assume called with interrupts _enabled_.
 */
DECL_NO_INLINE(static, int) vmmR0EntryExWorker(PGVM pGVM, VMCPUID idCpu, VMMR0OPERATION enmOperation,
                                               PSUPVMMR0REQHDR pReqHdr, uint64_t u64Arg, PSUPDRVSESSION pSession)
{
    /*
     * Validate pGVM and idCpu for consistency and validity.
     */
    if (pGVM != NULL)
    {
        if (RT_LIKELY(((uintptr_t)pGVM & HOST_PAGE_OFFSET_MASK) == 0))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid pGVM=%p! (op=%d)\n", pGVM, enmOperation);
            return VERR_INVALID_POINTER;
        }

        if (RT_LIKELY(idCpu == NIL_VMCPUID || idCpu < pGVM->cCpus))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid idCpu %#x (cCpus=%#x)\n", idCpu, pGVM->cCpus);
            return VERR_INVALID_PARAMETER;
        }

        if (RT_LIKELY(   pGVM->enmVMState >= VMSTATE_CREATING
                      && pGVM->enmVMState <= VMSTATE_TERMINATED
                      && pGVM->pSession   == pSession
                      && pGVM->pSelf      == pGVM))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid pGVM=%p:{.enmVMState=%d, .cCpus=%#x, .pSession=%p(==%p), .pSelf=%p(==%p)}! (op=%d)\n",
                        pGVM, pGVM->enmVMState, pGVM->cCpus, pGVM->pSession, pSession, pGVM->pSelf, pGVM, enmOperation);
            return VERR_INVALID_POINTER;
        }
    }
    else if (RT_LIKELY(idCpu == NIL_VMCPUID))
    { /* likely */ }
    else
    {
        SUPR0Printf("vmmR0EntryExWorker: Invalid idCpu=%u\n", idCpu);
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Process the request.
     */
    int rc;
    switch (enmOperation)
    {
        /*
         * GVM requests
         */
        case VMMR0_DO_GVMM_CREATE_VM:
            if (pGVM == NULL && u64Arg == 0 && idCpu == NIL_VMCPUID)
                rc = GVMMR0CreateVMReq((PGVMMCREATEVMREQ)pReqHdr, pSession);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_DESTROY_VM:
            if (pReqHdr == NULL && u64Arg == 0)
                rc = GVMMR0DestroyVM(pGVM);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_REGISTER_VMCPU:
            if (pGVM != NULL)
                rc = GVMMR0RegisterVCpu(pGVM, idCpu);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_DEREGISTER_VMCPU:
            if (pGVM != NULL)
                rc = GVMMR0DeregisterVCpu(pGVM, idCpu);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_REGISTER_WORKER_THREAD:
            if (pGVM != NULL && pReqHdr && pReqHdr->cbReq == sizeof(GVMMREGISTERWORKERTHREADREQ))
                rc = GVMMR0RegisterWorkerThread(pGVM, (GVMMWORKERTHREAD)(unsigned)u64Arg,
                                                ((PGVMMREGISTERWORKERTHREADREQ)(pReqHdr))->hNativeThreadR3);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_DEREGISTER_WORKER_THREAD:
            if (pGVM != NULL)
                rc = GVMMR0DeregisterWorkerThread(pGVM, (GVMMWORKERTHREAD)(unsigned)u64Arg);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VMMR0_DO_GVMM_SCHED_HALT:
            if (pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedHaltReq(pGVM, idCpu, u64Arg);
            break;

        case VMMR0_DO_GVMM_SCHED_WAKE_UP:
            if (pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedWakeUp(pGVM, idCpu);
            break;

        case VMMR0_DO_GVMM_SCHED_POKE:
            if (pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedPoke(pGVM, idCpu);
            break;

        case VMMR0_DO_GVMM_SCHED_WAKE_UP_AND_POKE_CPUS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedWakeUpAndPokeCpusReq(pGVM, (PGVMMSCHEDWAKEUPANDPOKECPUSREQ)pReqHdr);
            break;

        case VMMR0_DO_GVMM_SCHED_POLL:
            if (pReqHdr || u64Arg > 1)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedPoll(pGVM, idCpu, !!u64Arg);
            break;

        case VMMR0_DO_GVMM_QUERY_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0QueryStatisticsReq(pGVM, (PGVMMQUERYSTATISTICSSREQ)pReqHdr, pSession);
            break;

        case VMMR0_DO_GVMM_RESET_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0ResetStatisticsReq(pGVM, (PGVMMRESETSTATISTICSSREQ)pReqHdr, pSession);
            break;

        /*
         * Initialize the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_INIT:
            rc = vmmR0InitVM(pGVM, RT_LODWORD(u64Arg), RT_HIDWORD(u64Arg));
            break;

        /*
         * Does EMT specific ring-0 init.
         */
        case VMMR0_DO_VMMR0_INIT_EMT:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = vmmR0InitVMEmt(pGVM, idCpu);
            break;

        /*
         * Terminate the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_TERM:
            rc = VMMR0TermVM(pGVM, 0 /*idCpu*/);
            break;

        /*
         * Update release or debug logger instances.
         */
        case VMMR0_DO_VMMR0_UPDATE_LOGGERS:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (!(u64Arg & ~VMMR0UPDATELOGGER_F_VALID_MASK) && pReqHdr != NULL)
                rc = vmmR0UpdateLoggers(pGVM, idCpu /*idCpu*/, (PVMMR0UPDATELOGGERSREQ)pReqHdr, u64Arg);
            else
                return VERR_INVALID_PARAMETER;
            break;

        /*
         * Log flusher thread.
         */
        case VMMR0_DO_VMMR0_LOG_FLUSHER:
            if (idCpu != NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (pReqHdr == NULL && pGVM != NULL)
                rc = vmmR0LogFlusher(pGVM);
            else
                return VERR_INVALID_PARAMETER;
            break;

        /*
         * Wait for the flush to finish with all the buffers for the given logger.
         */
        case VMMR0_DO_VMMR0_LOG_WAIT_FLUSHED:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg < VMMLOGGER_IDX_MAX  && pReqHdr == NULL)
                rc = vmmR0LogWaitFlushed(pGVM, idCpu /*idCpu*/, (size_t)u64Arg);
            else
                return VERR_INVALID_PARAMETER;
            break;

        /*
         * Attempt to enable hm mode and check the current setting.
         */
        case VMMR0_DO_HM_ENABLE:
            rc = HMR0EnableAllCpus(pGVM);
            break;

        /*
         * Setup the hardware accelerated session.
         */
        case VMMR0_DO_HM_SETUP_VM:
            rc = HMR0SetupVM(pGVM);
            break;

        /*
         * PGM wrappers.
         */
        case VMMR0_DO_PGM_ALLOCATE_HANDY_PAGES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysAllocateHandyPages(pGVM, idCpu);
            break;

        case VMMR0_DO_PGM_FLUSH_HANDY_PAGES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysFlushHandyPages(pGVM, idCpu);
            break;

        case VMMR0_DO_PGM_ALLOCATE_LARGE_PAGE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysAllocateLargePage(pGVM, idCpu, u64Arg);
            break;

        case VMMR0_DO_PGM_PHYS_SETUP_IOMMU:
            if (idCpu != 0)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysSetupIoMmu(pGVM);
            break;

        case VMMR0_DO_PGM_POOL_GROW:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PoolGrow(pGVM, idCpu);
            break;

        case VMMR0_DO_PGM_PHYS_HANDLER_INIT:
            if (idCpu != 0 || pReqHdr != NULL || u64Arg > UINT32_MAX)
                return VERR_INVALID_PARAMETER;
            rc = PGMR0PhysHandlerInitReqHandler(pGVM, (uint32_t)u64Arg);
            break;

        case VMMR0_DO_PGM_PHYS_ALLOCATE_RAM_RANGE:
            if (idCpu != 0 || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PGMR0PhysAllocateRamRangeReq(pGVM, (PPGMPHYSALLOCATERAMRANGEREQ)pReqHdr);
            break;

        case VMMR0_DO_PGM_PHYS_MMIO2_REGISTER:
            if (idCpu != 0 || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PGMR0PhysMmio2RegisterReq(pGVM, (PPGMPHYSMMIO2REGISTERREQ)pReqHdr);
            break;

        case VMMR0_DO_PGM_PHYS_MMIO2_DEREGISTER:
            if (idCpu != 0 || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PGMR0PhysMmio2DeregisterReq(pGVM, (PPGMPHYSMMIO2DEREGISTERREQ)pReqHdr);
            break;

        case VMMR0_DO_PGM_PHYS_ROM_ALLOCATE_RANGE:
            if (idCpu != 0 || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PGMR0PhysRomAllocateRangeReq(pGVM, (PPGMPHYSROMALLOCATERANGEREQ)pReqHdr);
            break;

        /*
         * GMM wrappers.
         */
        case VMMR0_DO_GMM_INITIAL_RESERVATION:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0InitialReservationReq(pGVM, idCpu, (PGMMINITIALRESERVATIONREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_UPDATE_RESERVATION:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0UpdateReservationReq(pGVM, idCpu, (PGMMUPDATERESERVATIONREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_ALLOCATE_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0AllocatePagesReq(pGVM, idCpu, (PGMMALLOCATEPAGESREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_FREE_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FreePagesReq(pGVM, idCpu, (PGMMFREEPAGESREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_FREE_LARGE_PAGE:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FreeLargePageReq(pGVM, idCpu, (PGMMFREELARGEPAGEREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_QUERY_HYPERVISOR_MEM_STATS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryHypervisorMemoryStatsReq((PGMMMEMSTATSREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_QUERY_MEM_STATS:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryMemoryStatsReq(pGVM, idCpu, (PGMMMEMSTATSREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_BALLOONED_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0BalloonedPagesReq(pGVM, idCpu, (PGMMBALLOONEDPAGESREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_MAP_UNMAP_CHUNK:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0MapUnmapChunkReq(pGVM, (PGMMMAPUNMAPCHUNKREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_REGISTER_SHARED_MODULE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0RegisterSharedModuleReq(pGVM, idCpu, (PGMMREGISTERSHAREDMODULEREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_UNREGISTER_SHARED_MODULE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0UnregisterSharedModuleReq(pGVM, idCpu, (PGMMUNREGISTERSHAREDMODULEREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_RESET_SHARED_MODULES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (    u64Arg
                ||  pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0ResetSharedModules(pGVM, idCpu);
            break;

#ifdef VBOX_WITH_PAGE_SHARING
        case VMMR0_DO_GMM_CHECK_SHARED_MODULES:
        {
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (    u64Arg
                ||  pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0CheckSharedModules(pGVM, idCpu);
            break;
        }
#endif

#if defined(VBOX_STRICT) && HC_ARCH_BITS == 64
        case VMMR0_DO_GMM_FIND_DUPLICATE_PAGE:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FindDuplicatePageReq(pGVM, (PGMMFINDDUPLICATEPAGEREQ)pReqHdr);
            break;
#endif

        case VMMR0_DO_GMM_QUERY_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryStatisticsReq(pGVM, (PGMMQUERYSTATISTICSSREQ)pReqHdr);
            break;

        case VMMR0_DO_GMM_RESET_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0ResetStatisticsReq(pGVM, (PGMMRESETSTATISTICSSREQ)pReqHdr);
            break;

        /*
         * A quick GCFGM mock-up.
         */
        /** @todo GCFGM with proper access control, ring-3 management interface and all that. */
        case VMMR0_DO_GCFGM_SET_VALUE:
        case VMMR0_DO_GCFGM_QUERY_VALUE:
        {
            if (pGVM || !pReqHdr || u64Arg || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            PGCFGMVALUEREQ pReq = (PGCFGMVALUEREQ)pReqHdr;
            if (pReq->Hdr.cbReq != sizeof(*pReq))
                return VERR_INVALID_PARAMETER;
            if (enmOperation == VMMR0_DO_GCFGM_SET_VALUE)
            {
                rc = GVMMR0SetConfig(pReq->pSession, &pReq->szName[0], pReq->u64Value);
                //if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                //    rc = GMMR0SetConfig(pReq->pSession, &pReq->szName[0], pReq->u64Value);
            }
            else
            {
                rc = GVMMR0QueryConfig(pReq->pSession, &pReq->szName[0], &pReq->u64Value);
                //if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                //    rc = GMMR0QueryConfig(pReq->pSession, &pReq->szName[0], &pReq->u64Value);
            }
            break;
        }

        /*
         * PDM Wrappers.
         */
        case VMMR0_DO_PDM_DRIVER_CALL_REQ_HANDLER:
        {
            if (!pReqHdr || u64Arg || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DriverCallReqHandler(pGVM, (PPDMDRIVERCALLREQHANDLERREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_PDM_DEVICE_CREATE:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceCreateReqHandler(pGVM, (PPDMDEVICECREATEREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_PDM_DEVICE_GEN_CALL:
        {
            if (!pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceGenCallReqHandler(pGVM, (PPDMDEVICEGENCALLREQ)pReqHdr, idCpu);
            break;
        }

        /** @todo Remove the once all devices has been converted to new style! @bugref{9218} */
        case VMMR0_DO_PDM_DEVICE_COMPAT_SET_CRITSECT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceCompatSetCritSectReqHandler(pGVM, (PPDMDEVICECOMPATSETCRITSECTREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_PDM_QUEUE_CREATE:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0QueueCreateReqHandler(pGVM, (PPDMQUEUECREATEREQ)pReqHdr);
            break;
        }

        /*
         * Requests to the internal networking service.
         */
        case VMMR0_DO_INTNET_OPEN:
        {
            PINTNETOPENREQ pReq = (PINTNETOPENREQ)pReqHdr;
            if (u64Arg || !pReq || !vmmR0IsValidSession(pGVM, pReq->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0OpenReq(pSession, pReq);
            break;
        }

        case VMMR0_DO_INTNET_IF_CLOSE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFCLOSEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfCloseReq(pSession, (PINTNETIFCLOSEREQ)pReqHdr);
            break;


        case VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFGETBUFFERPTRSREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfGetBufferPtrsReq(pSession, (PINTNETIFGETBUFFERPTRSREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETPROMISCUOUSMODEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetPromiscuousModeReq(pSession, (PINTNETIFSETPROMISCUOUSMODEREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETMACADDRESSREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetMacAddressReq(pSession, (PINTNETIFSETMACADDRESSREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_SET_ACTIVE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETACTIVEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetActiveReq(pSession, (PINTNETIFSETACTIVEREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_SEND:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSENDREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSendReq(pSession, (PINTNETIFSENDREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_WAIT:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFWAITREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfWaitReq(pSession, (PINTNETIFWAITREQ)pReqHdr);
            break;

        case VMMR0_DO_INTNET_IF_ABORT_WAIT:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFWAITREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfAbortWaitReq(pSession, (PINTNETIFABORTWAITREQ)pReqHdr);
            break;

#if 0 //def VBOX_WITH_PCI_PASSTHROUGH
        /*
         * Requests to host PCI driver service.
         */
        case VMMR0_DO_PCIRAW_REQ:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PPCIRAWSENDREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = PciRawR0ProcessReq(pGVM, pSession, (PPCIRAWSENDREQ)pReqHdr);
            break;
#endif

        /*
         * NEM requests.
         */
#ifdef VBOX_WITH_NEM_R0
# if defined(RT_ARCH_AMD64) && defined(RT_OS_WINDOWS)
        case VMMR0_DO_NEM_INIT_VM:
            if (u64Arg || pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0InitVM(pGVM);
            break;

        case VMMR0_DO_NEM_INIT_VM_PART_2:
            if (u64Arg || pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0InitVMPart2(pGVM);
            break;

        case VMMR0_DO_NEM_MAP_PAGES:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0MapPages(pGVM, idCpu);
            break;

        case VMMR0_DO_NEM_UNMAP_PAGES:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0UnmapPages(pGVM, idCpu);
            break;

        case VMMR0_DO_NEM_EXPORT_STATE:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ExportState(pGVM, idCpu);
            break;

        case VMMR0_DO_NEM_IMPORT_STATE:
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ImportState(pGVM, idCpu, u64Arg);
            break;

        case VMMR0_DO_NEM_QUERY_CPU_TICK:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0QueryCpuTick(pGVM, idCpu);
            break;

        case VMMR0_DO_NEM_RESUME_CPU_TICK_ON_ALL:
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ResumeCpuTickOnAll(pGVM, idCpu, u64Arg);
            break;

        case VMMR0_DO_NEM_UPDATE_STATISTICS:
            if (u64Arg || pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0UpdateStatistics(pGVM, idCpu);
            break;

#   if 1 && defined(DEBUG_bird)
        case VMMR0_DO_NEM_EXPERIMENT:
            if (pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0DoExperiment(pGVM, idCpu, u64Arg);
            break;
#   endif
# endif
#endif

        /*
         * IOM requests.
         */
        case VMMR0_DO_IOM_GROW_IO_PORTS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortGrowRegistrationTables(pGVM, u64Arg);
            break;
        }

        case VMMR0_DO_IOM_GROW_IO_PORT_STATS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortGrowStatisticsTable(pGVM, u64Arg);
            break;
        }

        case VMMR0_DO_IOM_GROW_MMIO_REGS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0MmioGrowRegistrationTables(pGVM, u64Arg);
            break;
        }

        case VMMR0_DO_IOM_GROW_MMIO_STATS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0MmioGrowStatisticsTable(pGVM, u64Arg);
            break;
        }

        case VMMR0_DO_IOM_SYNC_STATS_INDICES:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortSyncStatisticsIndices(pGVM);
            if (RT_SUCCESS(rc))
                rc = IOMR0MmioSyncStatisticsIndices(pGVM);
            break;
        }

        /*
         * DBGF requests.
         */
#ifdef VBOX_WITH_DBGF_TRACING
        case VMMR0_DO_DBGF_TRACER_CREATE:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0TracerCreateReqHandler(pGVM, (PDBGFTRACERCREATEREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_DBGF_TRACER_CALL_REQ_HANDLER:
        {
            if (!pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
# if 0 /** @todo */
            rc = DBGFR0TracerGenCallReqHandler(pGVM, (PDBGFTRACERGENCALLREQ)pReqHdr, idCpu);
# else
            rc = VERR_NOT_IMPLEMENTED;
# endif
            break;
        }
#endif

        case VMMR0_DO_DBGF_BP_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpInitReqHandler(pGVM, (PDBGFBPINITREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_DBGF_BP_CHUNK_ALLOC:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpChunkAllocReqHandler(pGVM, (PDBGFBPCHUNKALLOCREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_DBGF_BP_L2_TBL_CHUNK_ALLOC:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpL2TblChunkAllocReqHandler(pGVM, (PDBGFBPL2TBLCHUNKALLOCREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_DBGF_BP_OWNER_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpOwnerInitReqHandler(pGVM, (PDBGFBPOWNERINITREQ)pReqHdr);
            break;
        }

        case VMMR0_DO_DBGF_BP_PORTIO_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpPortIoInitReqHandler(pGVM, (PDBGFBPINITREQ)pReqHdr);
            break;
        }


        /*
         * TM requests.
         */
        case VMMR0_DO_TM_GROW_TIMER_QUEUE:
        {
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = TMR0TimerQueueGrow(pGVM, RT_HI_U32(u64Arg), RT_LO_U32(u64Arg));
            break;
        }

        /*
         * For profiling.
         */
        case VMMR0_DO_NOP:
        case VMMR0_DO_SLOW_NOP:
            return VINF_SUCCESS;

        /*
         * For testing Ring-0 APIs invoked in this environment.
         */
        case VMMR0_DO_TESTS:
            /** @todo make new test */
            return VINF_SUCCESS;

        default:
            /*
             * We're returning VERR_NOT_SUPPORT here so we've got something else
             * than -1 which the interrupt gate glue code might return.
             */
            Log(("operation %#x is not supported\n", enmOperation));
            return VERR_NOT_SUPPORTED;
    }
    return rc;
}


/**
 * This is just a longjmp wrapper function for VMMR0EntryEx calls.
 *
 * @returns VBox status code.
 * @param   pvArgs      The argument package
 */
static DECLCALLBACK(int) vmmR0EntryExWrapper(void *pvArgs)
{
    PGVMCPU pGVCpu = (PGVMCPU)pvArgs;
    return vmmR0EntryExWorker(pGVCpu->vmmr0.s.pGVM,
                              pGVCpu->vmmr0.s.idCpu,
                              pGVCpu->vmmr0.s.enmOperation,
                              pGVCpu->vmmr0.s.pReq,
                              pGVCpu->vmmr0.s.u64Arg,
                              pGVCpu->vmmr0.s.pSession);
}


/**
 * The Ring 0 entry point, called by the support library (SUP).
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           Virtual CPU ID argument. Must be NIL_VMCPUID if pVM
 *                          is NIL_RTR0PTR, and may be NIL_VMCPUID if it isn't
 * @param   enmOperation    Which operation to execute.
 * @param   pReq            Pointer to the SUPVMMR0REQHDR packet. Optional.
 * @param   u64Arg          Some simple constant argument.
 * @param   pSession        The session of the caller.
 * @remarks Assume called with interrupts _enabled_.
 */
VMMR0DECL(int) VMMR0EntryEx(PGVM pGVM, PVMCC pVM, VMCPUID idCpu, VMMR0OPERATION enmOperation,
                            PSUPVMMR0REQHDR pReq, uint64_t u64Arg, PSUPDRVSESSION pSession)
{
    /*
     * Requests that should only happen on the EMT thread will be
     * wrapped in a setjmp so we can assert without causing too much trouble.
     */
    if (   pVM  != NULL
        && pGVM != NULL
        && pVM  == pGVM /** @todo drop pVM or pGVM */
        && idCpu < pGVM->cCpus
        && pGVM->pSession == pSession
        && pGVM->pSelf    == pGVM
        && enmOperation != VMMR0_DO_GVMM_DESTROY_VM
        && enmOperation != VMMR0_DO_GVMM_REGISTER_VMCPU
        && enmOperation != VMMR0_DO_GVMM_SCHED_WAKE_UP  /* idCpu is not caller but target. Sigh. */ /** @todo fix*/
        && enmOperation != VMMR0_DO_GVMM_SCHED_POKE     /* idCpu is not caller but target. Sigh. */ /** @todo fix*/
       )
    {
        PGVMCPU        pGVCpu        = &pGVM->aCpus[idCpu];
        RTNATIVETHREAD hNativeThread = RTThreadNativeSelf();
        if (RT_LIKELY(   pGVCpu->hEMT            == hNativeThread
                      && pGVCpu->hNativeThreadR0 == hNativeThread))
        {
            pGVCpu->vmmr0.s.pGVM         = pGVM;
            pGVCpu->vmmr0.s.idCpu        = idCpu;
            pGVCpu->vmmr0.s.enmOperation = enmOperation;
            pGVCpu->vmmr0.s.pReq         = pReq;
            pGVCpu->vmmr0.s.u64Arg       = u64Arg;
            pGVCpu->vmmr0.s.pSession     = pSession;
            return vmmR0CallRing3SetJmpEx(&pGVCpu->vmmr0.s.AssertJmpBuf, vmmR0EntryExWrapper, pGVCpu,
                                          ((uintptr_t)u64Arg << 16) | (uintptr_t)enmOperation);
        }
        return VERR_VM_THREAD_NOT_EMT;
    }
    return vmmR0EntryExWorker(pGVM, idCpu, enmOperation, pReq, u64Arg, pSession);
}


/*********************************************************************************************************************************
*   EMT Blocking                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Checks whether we've armed the ring-0 long jump machinery.
 *
 * @returns @c true / @c false
 * @param   pVCpu           The cross context virtual CPU structure.
 * @thread  EMT
 * @sa      VMMIsLongJumpArmed
 */
VMMR0_INT_DECL(bool) VMMR0IsLongJumpArmed(PVMCPUCC pVCpu)
{
#ifdef RT_ARCH_X86
    return pVCpu->vmmr0.s.AssertJmpBuf.eip != 0;
#else
    return pVCpu->vmmr0.s.AssertJmpBuf.rip != 0;
#endif
}


/**
 * Locking helper that deals with HM context and checks if the thread can block.
 *
 * @returns VINF_SUCCESS if we can block.  Returns @a rcBusy or
 *          VERR_VMM_CANNOT_BLOCK if not able to block.
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   rcBusy      What to return in case of a blocking problem.  Will IPE
 *                      if VINF_SUCCESS and we cannot block.
 * @param   pszCaller   The caller (for logging problems).
 * @param   pvLock      The lock address (for logging problems).
 * @param   pCtx        Where to return context info for the resume call.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(int) VMMR0EmtPrepareToBlock(PVMCPUCC pVCpu, int rcBusy, const char *pszCaller, void *pvLock,
                                           PVMMR0EMTBLOCKCTX pCtx)
{
    const char *pszMsg;

    /*
     * Check that we are allowed to block.
     */
    if (RT_LIKELY(VMMRZCallRing3IsEnabled(pVCpu)))
    {
        /*
         * Are we in HM context and w/o a context hook?  If so work the context hook.
         */
        if (pVCpu->idHostCpu != NIL_RTCPUID)
        {
            Assert(pVCpu->iHostCpuSet != UINT32_MAX);

            if (pVCpu->vmmr0.s.hCtxHook == NIL_RTTHREADCTXHOOK)
            {
                vmmR0ThreadCtxCallback(RTTHREADCTXEVENT_OUT, pVCpu);
                if (pVCpu->vmmr0.s.pPreemptState)
                    RTThreadPreemptRestore(pVCpu->vmmr0.s.pPreemptState);

                pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC;
                pCtx->fWasInHmContext = true;
                return VINF_SUCCESS;
            }
        }

        if (RT_LIKELY(!pVCpu->vmmr0.s.pPreemptState))
        {
            /*
             * Not in HM context or we've got hooks, so just check that preemption
             * is enabled.
             */
            if (RT_LIKELY(RTThreadPreemptIsEnabled(NIL_RTTHREAD)))
            {
                pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC;
                pCtx->fWasInHmContext = false;
                return VINF_SUCCESS;
            }
            pszMsg = "Preemption is disabled!";
        }
        else
            pszMsg = "Preemption state w/o HM state!";
    }
    else
        pszMsg = "Ring-3 calls are disabled!";

    static uint32_t volatile s_cWarnings = 0;
    if (++s_cWarnings < 50)
        SUPR0Printf("VMMR0EmtPrepareToBlock: %s pvLock=%p pszCaller=%s rcBusy=%p\n", pszMsg, pvLock, pszCaller, rcBusy);
    pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC_DEAD;
    pCtx->fWasInHmContext = false;
    return rcBusy != VINF_SUCCESS ? rcBusy : VERR_VMM_CANNOT_BLOCK;
}


/**
 * Counterpart to VMMR0EmtPrepareToBlock.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   pCtx        The context structure used with VMMR0EmtPrepareToBlock.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(void) VMMR0EmtResumeAfterBlocking(PVMCPUCC pVCpu, PVMMR0EMTBLOCKCTX pCtx)
{
    AssertReturnVoid(pCtx->uMagic == VMMR0EMTBLOCKCTX_MAGIC);
    if (pCtx->fWasInHmContext)
    {
        if (pVCpu->vmmr0.s.pPreemptState)
            RTThreadPreemptDisable(pVCpu->vmmr0.s.pPreemptState);

        pCtx->fWasInHmContext = false;
        vmmR0ThreadCtxCallback(RTTHREADCTXEVENT_IN, pVCpu);
    }
    pCtx->uMagic = VMMR0EMTBLOCKCTX_MAGIC_DEAD;
}


/**
 * Helper for waiting on an RTSEMEVENT, caller did VMMR0EmtPrepareToBlock.
 *
 * @returns
 * @retval  VERR_THREAD_IS_TERMINATING
 * @retval  VERR_TIMEOUT if we ended up waiting too long, either according to
 *          @a cMsTimeout or to maximum wait values.
 *
 * @param   pGVCpu      The ring-0 virtual CPU structure.
 * @param   fFlags      VMMR0EMTWAIT_F_XXX.
 * @param   hEvent      The event to wait on.
 * @param   cMsTimeout  The timeout or RT_INDEFINITE_WAIT.
 */
VMMR0_INT_DECL(int) VMMR0EmtWaitEventInner(PGVMCPU pGVCpu, uint32_t fFlags, RTSEMEVENT hEvent, RTMSINTERVAL cMsTimeout)
{
    AssertReturn(pGVCpu->hEMT == RTThreadNativeSelf(), VERR_VM_THREAD_NOT_EMT);

    /*
     * Note! Similar code is found in the PDM critical sections too.
     */
    uint64_t const nsStart           = RTTimeNanoTS();
    uint64_t       cNsMaxTotal       = cMsTimeout == RT_INDEFINITE_WAIT
                                     ? RT_NS_5MIN : RT_MIN(RT_NS_5MIN, RT_NS_1MS_64 * cMsTimeout);
    uint32_t       cMsMaxOne         = RT_MS_5SEC;
    bool           fNonInterruptible = false;
    for (;;)
    {
        /* Wait. */
        int rcWait = !fNonInterruptible
                   ? RTSemEventWaitNoResume(hEvent, cMsMaxOne)
                   : RTSemEventWait(hEvent, cMsMaxOne);
        if (RT_SUCCESS(rcWait))
            return rcWait;

        if (rcWait == VERR_TIMEOUT || rcWait == VERR_INTERRUPTED)
        {
            uint64_t const cNsElapsed = RTTimeNanoTS() - nsStart;

            /*
             * Check the thread termination status.
             */
            int const rcTerm = RTThreadQueryTerminationStatus(NIL_RTTHREAD);
            AssertMsg(rcTerm == VINF_SUCCESS || rcTerm == VERR_NOT_SUPPORTED || rcTerm == VINF_THREAD_IS_TERMINATING,
                      ("rcTerm=%Rrc\n", rcTerm));
            if (   rcTerm == VERR_NOT_SUPPORTED
                && !fNonInterruptible
                && cNsMaxTotal > RT_NS_1MIN)
                cNsMaxTotal = RT_NS_1MIN;

            /* We return immediately if it looks like the thread is terminating. */
            if (rcTerm == VINF_THREAD_IS_TERMINATING)
                return VERR_THREAD_IS_TERMINATING;

            /* We may suppress VERR_INTERRUPTED if VMMR0EMTWAIT_F_TRY_SUPPRESS_INTERRUPTED was
               specified, otherwise we'll just return it. */
            if (rcWait == VERR_INTERRUPTED)
            {
                if (!(fFlags & VMMR0EMTWAIT_F_TRY_SUPPRESS_INTERRUPTED))
                    return VERR_INTERRUPTED;
                if (!fNonInterruptible)
                {
                    /* First time: Adjust down the wait parameters and make sure we get at least
                                   one non-interruptible wait before timing out. */
                    fNonInterruptible   = true;
                    cMsMaxOne           = 32;
                    uint64_t const cNsLeft = cNsMaxTotal - cNsElapsed;
                    if (cNsLeft > RT_NS_10SEC)
                        cNsMaxTotal = cNsElapsed + RT_NS_10SEC;
                    continue;
                }
            }

            /* Check for timeout. */
            if (cNsElapsed > cNsMaxTotal)
                return VERR_TIMEOUT;
        }
        else
            return rcWait;
    }
    /* not reached */
}


/**
 * Helper for signalling an SUPSEMEVENT.
 *
 * This may temporarily leave the HM context if the host requires that for
 * signalling SUPSEMEVENT objects.
 *
 * @returns VBox status code (see VMMR0EmtPrepareToBlock)
 * @param   pGVM        The ring-0 VM structure.
 * @param   pGVCpu      The ring-0 virtual CPU structure.
 * @param   hEvent      The event to signal.
 */
VMMR0_INT_DECL(int) VMMR0EmtSignalSupEvent(PGVM pGVM, PGVMCPU pGVCpu, SUPSEMEVENT hEvent)
{
    AssertReturn(pGVCpu->hEMT == RTThreadNativeSelf(), VERR_VM_THREAD_NOT_EMT);
    if (RTSemEventIsSignalSafe())
        return SUPSemEventSignal(pGVM->pSession, hEvent);

    VMMR0EMTBLOCKCTX Ctx;
    int rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, __FUNCTION__, (void *)(uintptr_t)hEvent, &Ctx);
    if (RT_SUCCESS(rc))
    {
        rc = SUPSemEventSignal(pGVM->pSession, hEvent);
        VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);
    }
    return rc;
}


/**
 * Helper for signalling an SUPSEMEVENT, variant supporting non-EMTs.
 *
 * This may temporarily leave the HM context if the host requires that for
 * signalling SUPSEMEVENT objects.
 *
 * @returns VBox status code (see VMMR0EmtPrepareToBlock)
 * @param   pGVM        The ring-0 VM structure.
 * @param   hEvent      The event to signal.
 */
VMMR0_INT_DECL(int) VMMR0EmtSignalSupEventByGVM(PGVM pGVM, SUPSEMEVENT hEvent)
{
    if (!RTSemEventIsSignalSafe())
    {
        PGVMCPU pGVCpu = GVMMR0GetGVCpuByGVMandEMT(pGVM, NIL_RTNATIVETHREAD);
        if (pGVCpu)
        {
            VMMR0EMTBLOCKCTX Ctx;
            int rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, __FUNCTION__, (void *)(uintptr_t)hEvent, &Ctx);
            if (RT_SUCCESS(rc))
            {
                rc = SUPSemEventSignal(pGVM->pSession, hEvent);
                VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);
            }
            return rc;
        }
    }
    return SUPSemEventSignal(pGVM->pSession, hEvent);
}


/*********************************************************************************************************************************
*   Logging.                                                                                                                     *
*********************************************************************************************************************************/

/**
 * VMMR0_DO_VMMR0_UPDATE_LOGGERS: Updates the EMT loggers for the VM.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The ID of the calling EMT.
 * @param   pReq            The request data.
 * @param   fFlags          Flags, see VMMR0UPDATELOGGER_F_XXX.
 * @thread  EMT(idCpu)
 */
static int vmmR0UpdateLoggers(PGVM pGVM, VMCPUID idCpu, PVMMR0UPDATELOGGERSREQ pReq, uint64_t fFlags)
{
    /*
     * Check sanity.  First we require EMT to be calling us.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_INVALID_CPU_ID);

    AssertReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF_DYN(VMMR0UPDATELOGGERSREQ, afGroups[0]), VERR_INVALID_PARAMETER);
    AssertReturn(pReq->cGroups < _8K, VERR_INVALID_PARAMETER);
    AssertReturn(pReq->Hdr.cbReq == RT_UOFFSETOF_DYN(VMMR0UPDATELOGGERSREQ, afGroups[pReq->cGroups]), VERR_INVALID_PARAMETER);

    size_t const idxLogger = (size_t)(fFlags & VMMR0UPDATELOGGER_F_LOGGER_MASK);
    AssertReturn(idxLogger < VMMLOGGER_IDX_MAX, VERR_OUT_OF_RANGE);

    /*
     * Adjust flags.
     */
    /* Always buffered, unless logging directly to parent VMM: */
    if (!(fFlags & (VMMR0UPDATELOGGER_F_TO_PARENT_VMM_DBG | VMMR0UPDATELOGGER_F_TO_PARENT_VMM_REL)))
        pReq->fFlags |= RTLOGFLAGS_BUFFERED;
    /* These doesn't make sense at present: */
    pReq->fFlags &= ~(RTLOGFLAGS_FLUSH | RTLOGFLAGS_WRITE_THROUGH);
    /* We've traditionally skipped the group restrictions. */
    pReq->fFlags &= ~RTLOGFLAGS_RESTRICT_GROUPS;

    /*
     * Do the updating.
     */
    int rc = VINF_SUCCESS;
    for (idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU   pGVCpu  = &pGVM->aCpus[idCpu];
        PRTLOGGER pLogger = pGVCpu->vmmr0.s.u.aLoggers[idxLogger].pLogger;
        if (pLogger)
        {
            pGVCpu->vmmr0.s.u.aLoggers[idxLogger].fFlushToParentVmmDbg = RT_BOOL(fFlags & VMMR0UPDATELOGGER_F_TO_PARENT_VMM_DBG);
            pGVCpu->vmmr0.s.u.aLoggers[idxLogger].fFlushToParentVmmRel = RT_BOOL(fFlags & VMMR0UPDATELOGGER_F_TO_PARENT_VMM_REL);

            RTLogSetR0ProgramStart(pLogger, pGVM->vmm.s.nsProgramStart);
            rc = RTLogBulkUpdate(pLogger, pReq->fFlags, pReq->uGroupCrc32, pReq->cGroups, pReq->afGroups);
        }
    }

    return rc;
}


/**
 * VMMR0_DO_VMMR0_LOG_FLUSHER: Get the next log flushing job.
 *
 * The job info is copied into VMM::LogFlusherItem.
 *
 * @returns VBox status code.
 * @retval  VERR_OBJECT_DESTROYED if we're shutting down.
 * @retval  VERR_NOT_OWNER if the calling thread is not the flusher thread.
 * @param   pGVM            The global (ring-0) VM structure.
 * @thread  The log flusher thread (first caller automatically becomes the log
 *          flusher).
 */
static int vmmR0LogFlusher(PGVM pGVM)
{
    /*
     * Check that this really is the flusher thread.
     */
    RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
    AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, VERR_INTERNAL_ERROR_3);
    if (RT_LIKELY(pGVM->vmmr0.s.LogFlusher.hThread == hNativeSelf))
    { /* likely */ }
    else
    {
        /* The first caller becomes the flusher thread. */
        bool fOk;
        ASMAtomicCmpXchgHandle(&pGVM->vmmr0.s.LogFlusher.hThread, hNativeSelf, NIL_RTNATIVETHREAD, fOk);
        if (!fOk)
            return VERR_NOT_OWNER;
        pGVM->vmmr0.s.LogFlusher.fThreadRunning = true;
    }

    /*
     * Acknowledge flush, waking up waiting EMT.
     */
    RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);

    uint32_t idxTail = pGVM->vmmr0.s.LogFlusher.idxRingTail % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
    uint32_t idxHead = pGVM->vmmr0.s.LogFlusher.idxRingHead % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
    if (   idxTail != idxHead
        && pGVM->vmmr0.s.LogFlusher.aRing[idxHead].s.fProcessing)
    {
        /* Pop the head off the ring buffer. */
        uint32_t const idCpu     = pGVM->vmmr0.s.LogFlusher.aRing[idxHead].s.idCpu;
        uint32_t const idxLogger = pGVM->vmmr0.s.LogFlusher.aRing[idxHead].s.idxLogger;
        uint32_t const idxBuffer = pGVM->vmmr0.s.LogFlusher.aRing[idxHead].s.idxBuffer;

        pGVM->vmmr0.s.LogFlusher.aRing[idxHead].u32 = UINT32_MAX >> 1; /* invalidate the entry */
        pGVM->vmmr0.s.LogFlusher.idxRingHead = (idxHead + 1) % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);

        /* Validate content. */
        if (   idCpu     < pGVM->cCpus
            && idxLogger < VMMLOGGER_IDX_MAX
            && idxBuffer < VMMLOGGER_BUFFER_COUNT)
        {
            PGVMCPU             pGVCpu  = &pGVM->aCpus[idCpu];
            PVMMR0PERVCPULOGGER pR0Log  = &pGVCpu->vmmr0.s.u.aLoggers[idxLogger];
            PVMMR3CPULOGGER     pShared = &pGVCpu->vmm.s.u.aLoggers[idxLogger];

            /*
             * Accounting.
             */
            uint32_t cFlushing = pR0Log->cFlushing - 1;
            if (RT_LIKELY(cFlushing < VMMLOGGER_BUFFER_COUNT))
            { /*likely*/ }
            else
                cFlushing = 0;
            pR0Log->cFlushing = cFlushing;
            ASMAtomicWriteU32(&pShared->cFlushing, cFlushing);

            /*
             * Wake up the EMT if it's waiting.
             */
            if (!pR0Log->fEmtWaiting)
                RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            else
            {
                pR0Log->fEmtWaiting = false;
                RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);

                int rc = RTSemEventSignal(pR0Log->hEventFlushWait);
                if (RT_FAILURE(rc))
                    LogRelMax(64, ("vmmR0LogFlusher: RTSemEventSignal failed ACKing entry #%u (%u/%u/%u): %Rrc!\n",
                                   idxHead, idCpu, idxLogger, idxBuffer, rc));
            }
        }
        else
        {
            RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            LogRelMax(64, ("vmmR0LogFlusher: Bad ACK entry #%u: %u/%u/%u!\n", idxHead, idCpu, idxLogger, idxBuffer));
        }

        RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    }

    /*
     * The wait loop.
     */
    int rc;
    for (;;)
    {
        /*
         * Work pending?
         */
        idxTail = pGVM->vmmr0.s.LogFlusher.idxRingTail % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
        idxHead = pGVM->vmmr0.s.LogFlusher.idxRingHead % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
        if (idxTail != idxHead)
        {
            pGVM->vmmr0.s.LogFlusher.aRing[idxHead].s.fProcessing = true;
            pGVM->vmm.s.LogFlusherItem.u32 = pGVM->vmmr0.s.LogFlusher.aRing[idxHead].u32;

            RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            return VINF_SUCCESS;
        }

        /*
         * Nothing to do, so, check for termination and go to sleep.
         */
        if (!pGVM->vmmr0.s.LogFlusher.fThreadShutdown)
        { /* likely */ }
        else
        {
            rc = VERR_OBJECT_DESTROYED;
            break;
        }

        pGVM->vmmr0.s.LogFlusher.fThreadWaiting = true;
        RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);

        rc = RTSemEventWaitNoResume(pGVM->vmmr0.s.LogFlusher.hEvent, RT_MS_5MIN);

        RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);
        pGVM->vmmr0.s.LogFlusher.fThreadWaiting = false;

        if (RT_SUCCESS(rc) || rc == VERR_TIMEOUT)
        { /* likely */ }
        else if (rc == VERR_INTERRUPTED)
        {
            RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            return rc;
        }
        else if (rc == VERR_SEM_DESTROYED || rc == VERR_INVALID_HANDLE)
            break;
        else
        {
            LogRel(("vmmR0LogFlusher: RTSemEventWaitNoResume returned unexpected status %Rrc\n", rc));
            break;
        }
    }

    /*
     * Terminating - prevent further calls and indicate to the EMTs that we're no longer around.
     */
    pGVM->vmmr0.s.LogFlusher.hThread        = ~pGVM->vmmr0.s.LogFlusher.hThread;  /* (should be reasonably safe) */
    pGVM->vmmr0.s.LogFlusher.fThreadRunning = false;

    RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    return rc;
}


/**
 * VMMR0_DO_VMMR0_LOG_WAIT_FLUSHED: Waits for the flusher thread to finish all
 * buffers for logger @a idxLogger.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The ID of the calling EMT.
 * @param   idxLogger       Which logger to wait on.
 * @thread  EMT(idCpu)
 */
static int vmmR0LogWaitFlushed(PGVM pGVM, VMCPUID idCpu, size_t idxLogger)
{
    /*
     * Check sanity.  First we require EMT to be calling us.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
    AssertReturn(pGVCpu->hEMT == RTThreadNativeSelf(), VERR_INVALID_CPU_ID);
    AssertReturn(idxLogger < VMMLOGGER_IDX_MAX, VERR_OUT_OF_RANGE);
    PVMMR0PERVCPULOGGER const pR0Log = &pGVCpu->vmmr0.s.u.aLoggers[idxLogger];

    /*
     * Do the waiting.
     */
    int rc = VINF_SUCCESS;
    RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    uint32_t cFlushing = pR0Log->cFlushing;
    while (cFlushing > 0)
    {
        pR0Log->fEmtWaiting = true;
        RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);

        rc = RTSemEventWaitNoResume(pR0Log->hEventFlushWait, RT_MS_5MIN);

        RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);
        pR0Log->fEmtWaiting = false;
        if (RT_SUCCESS(rc))
        {
            /* Read the new count, make sure it decreased before looping.  That
               way we can guarentee that we will only wait more than 5 min * buffers. */
            uint32_t const cPrevFlushing = cFlushing;
            cFlushing = pR0Log->cFlushing;
            if (cFlushing < cPrevFlushing)
                continue;
            rc = VERR_INTERNAL_ERROR_3;
        }
        break;
    }
    RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    return rc;
}


/**
 * Inner worker for vmmR0LoggerFlushCommon for flushing to ring-3.
 */
static bool   vmmR0LoggerFlushInnerToRing3(PGVM pGVM, PGVMCPU pGVCpu, uint32_t idxLogger, size_t idxBuffer, uint32_t cbToFlush)
{
    PVMMR0PERVCPULOGGER const pR0Log    = &pGVCpu->vmmr0.s.u.aLoggers[idxLogger];
    PVMMR3CPULOGGER const     pShared   = &pGVCpu->vmm.s.u.aLoggers[idxLogger];

    /*
     * Figure out what we need to do and whether we can.
     */
    enum { kJustSignal, kPrepAndSignal, kPrepSignalAndWait } enmAction;
#if VMMLOGGER_BUFFER_COUNT >= 2
    if (pR0Log->cFlushing < VMMLOGGER_BUFFER_COUNT - 1)
    {
        if (RTSemEventIsSignalSafe())
            enmAction = kJustSignal;
        else if (VMMRZCallRing3IsEnabled(pGVCpu))
            enmAction = kPrepAndSignal;
        else
        {
            /** @todo This is a bit simplistic.  We could introduce a FF to signal the
             *        thread or similar. */
            STAM_REL_COUNTER_INC(&pShared->StatCannotBlock);
# if defined(RT_OS_LINUX)
            SUP_DPRINTF(("vmmR0LoggerFlush: Signalling not safe and EMT blocking disabled! (%u bytes)\n", cbToFlush));
# endif
            pShared->cbDropped += cbToFlush;
            return true;
        }
    }
    else
#endif
    if (VMMRZCallRing3IsEnabled(pGVCpu))
        enmAction = kPrepSignalAndWait;
    else
    {
        STAM_REL_COUNTER_INC(&pShared->StatCannotBlock);
# if defined(RT_OS_LINUX)
        SUP_DPRINTF(("vmmR0LoggerFlush: EMT blocking disabled! (%u bytes)\n", cbToFlush));
# endif
        pShared->cbDropped += cbToFlush;
        return true;
    }

    /*
     * Prepare for blocking if necessary.
     */
    VMMR0EMTBLOCKCTX Ctx;
    if (enmAction != kJustSignal)
    {
        int rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, "vmmR0LoggerFlushInnerToRing3", pR0Log->hEventFlushWait, &Ctx);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            STAM_REL_COUNTER_INC(&pShared->StatCannotBlock);
            SUP_DPRINTF(("vmmR0LoggerFlush: VMMR0EmtPrepareToBlock failed! rc=%d\n", rc));
            return false;
        }
    }

    /*
     * Queue the flush job.
     */
    bool fFlushedBuffer;
    RTSpinlockAcquire(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    if (pGVM->vmmr0.s.LogFlusher.fThreadRunning)
    {
        uint32_t const idxHead    = pGVM->vmmr0.s.LogFlusher.idxRingHead % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
        uint32_t const idxTail    = pGVM->vmmr0.s.LogFlusher.idxRingTail % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
        uint32_t const idxNewTail = (idxTail + 1)                        % RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing);
        if (idxNewTail != idxHead)
        {
            /* Queue it. */
            pGVM->vmmr0.s.LogFlusher.aRing[idxTail].s.idCpu       = pGVCpu->idCpu;
            pGVM->vmmr0.s.LogFlusher.aRing[idxTail].s.idxLogger   = idxLogger;
            pGVM->vmmr0.s.LogFlusher.aRing[idxTail].s.idxBuffer   = (uint32_t)idxBuffer;
            pGVM->vmmr0.s.LogFlusher.aRing[idxTail].s.fProcessing = 0;
            pGVM->vmmr0.s.LogFlusher.idxRingTail = idxNewTail;

            /* Update the number of buffers currently being flushed. */
            uint32_t cFlushing = pR0Log->cFlushing;
            cFlushing = RT_MIN(cFlushing + 1, VMMLOGGER_BUFFER_COUNT);
            pShared->cFlushing = pR0Log->cFlushing = cFlushing;

            /* We must wait if all buffers are currently being flushed. */
            bool const fEmtWaiting = cFlushing >= VMMLOGGER_BUFFER_COUNT && enmAction != kJustSignal /* paranoia */;
            pR0Log->fEmtWaiting = fEmtWaiting;

            /* Stats. */
            STAM_REL_COUNTER_INC(&pShared->StatFlushes);
            STAM_REL_COUNTER_INC(&pGVM->vmm.s.StatLogFlusherFlushes);

            /* Signal the worker thread. */
            if (pGVM->vmmr0.s.LogFlusher.fThreadWaiting)
            {
                RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
                RTSemEventSignal(pGVM->vmmr0.s.LogFlusher.hEvent);
            }
            else
            {
                STAM_REL_COUNTER_INC(&pGVM->vmm.s.StatLogFlusherNoWakeUp);
                RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            }

            /*
             * Wait for a buffer to finish flushing.
             *
             * Note! Lazy bird is ignoring the status code here.  The result is
             *       that we might end up with an extra even signalling and the
             *       next time we need to wait we won't and end up with some log
             *       corruption.  However, it's too much hazzle right now for
             *       a scenario which would most likely end the process rather
             *       than causing log corruption.
             */
            if (fEmtWaiting)
            {
                STAM_REL_PROFILE_START(&pShared->StatWait, a);
                VMMR0EmtWaitEventInner(pGVCpu, VMMR0EMTWAIT_F_TRY_SUPPRESS_INTERRUPTED,
                                       pR0Log->hEventFlushWait, RT_INDEFINITE_WAIT);
                STAM_REL_PROFILE_STOP(&pShared->StatWait, a);
            }

            /*
             * We always switch buffer if we have more than one.
             */
#if VMMLOGGER_BUFFER_COUNT == 1
            fFlushedBuffer = true;
#else
            AssertCompile(VMMLOGGER_BUFFER_COUNT >= 1);
            pShared->idxBuf = (idxBuffer + 1) % VMMLOGGER_BUFFER_COUNT;
            fFlushedBuffer = false;
#endif
        }
        else
        {
            RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
            SUP_DPRINTF(("vmmR0LoggerFlush: ring buffer is full!\n"));
            fFlushedBuffer = true;
        }
    }
    else
    {
        RTSpinlockRelease(pGVM->vmmr0.s.LogFlusher.hSpinlock);
        SUP_DPRINTF(("vmmR0LoggerFlush: flusher not active - dropping %u bytes\n", cbToFlush));
        fFlushedBuffer = true;
    }

    /*
     * Restore the HM context.
     */
    if (enmAction != kJustSignal)
        VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);

    return fFlushedBuffer;
}


/**
 * Inner worker for vmmR0LoggerFlushCommon when only flushing to the parent
 * VMM's logs.
 */
static bool vmmR0LoggerFlushInnerToParent(PVMMR0PERVCPULOGGER pR0Log, PRTLOGBUFFERDESC pBufDesc)
{
    uint32_t const cbToFlush = pBufDesc->offBuf;
    if (pR0Log->fFlushToParentVmmDbg)
        RTLogWriteVmm(pBufDesc->pchBuf, cbToFlush, false /*fRelease*/);
    if (pR0Log->fFlushToParentVmmRel)
        RTLogWriteVmm(pBufDesc->pchBuf, cbToFlush, true /*fRelease*/);
    return true;
}



/**
 * Common worker for vmmR0LogFlush and vmmR0LogRelFlush.
 */
static bool vmmR0LoggerFlushCommon(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc, uint32_t idxLogger)
{
    /*
     * Convert the pLogger into a GVMCPU handle and 'call' back to Ring-3.
     * (This is a bit paranoid code.)
     */
    if (RT_VALID_PTR(pLogger))
    {
        if (   pLogger->u32Magic == RTLOGGER_MAGIC
            && (pLogger->u32UserValue1 & VMMR0_LOGGER_FLAGS_MAGIC_MASK) == VMMR0_LOGGER_FLAGS_MAGIC_VALUE
            && pLogger->u64UserValue2 == pLogger->u64UserValue3)
        {
            PGVMCPU const pGVCpu = (PGVMCPU)(uintptr_t)pLogger->u64UserValue2;
            if (   RT_VALID_PTR(pGVCpu)
                && ((uintptr_t)pGVCpu & HOST_PAGE_OFFSET_MASK) == 0)
            {
                RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
                PGVM const           pGVM        = pGVCpu->pGVM;
                if (   hNativeSelf == pGVCpu->hEMT
                    && RT_VALID_PTR(pGVM))
                {
                    PVMMR0PERVCPULOGGER const pR0Log    = &pGVCpu->vmmr0.s.u.aLoggers[idxLogger];
                    size_t const              idxBuffer = pBufDesc - &pR0Log->aBufDescs[0];
                    if (idxBuffer < VMMLOGGER_BUFFER_COUNT)
                    {
                        /*
                         * Make sure we don't recurse forever here should something in the
                         * following code trigger logging or an assertion.  Do the rest in
                         * an inner work to avoid hitting the right margin too hard.
                         */
                        if (!pR0Log->fFlushing)
                        {
                            pR0Log->fFlushing = true;
                            bool fFlushed;
                            if (   !pR0Log->fFlushToParentVmmDbg
                                && !pR0Log->fFlushToParentVmmRel)
                                fFlushed = vmmR0LoggerFlushInnerToRing3(pGVM, pGVCpu, idxLogger, idxBuffer, pBufDesc->offBuf);
                            else
                                fFlushed = vmmR0LoggerFlushInnerToParent(pR0Log, pBufDesc);
                            pR0Log->fFlushing = false;
                            return fFlushed;
                        }

                        SUP_DPRINTF(("vmmR0LoggerFlush: Recursive flushing!\n"));
                    }
                    else
                        SUP_DPRINTF(("vmmR0LoggerFlush: pLogger=%p pGVCpu=%p: idxBuffer=%#zx\n", pLogger, pGVCpu, idxBuffer));
                }
                else
                    SUP_DPRINTF(("vmmR0LoggerFlush: pLogger=%p pGVCpu=%p hEMT=%p hNativeSelf=%p!\n",
                                 pLogger, pGVCpu, pGVCpu->hEMT, hNativeSelf));
            }
            else
                SUP_DPRINTF(("vmmR0LoggerFlush: pLogger=%p pGVCpu=%p!\n", pLogger, pGVCpu));
        }
        else
            SUP_DPRINTF(("vmmR0LoggerFlush: pLogger=%p u32Magic=%#x u32UserValue1=%#x u64UserValue2=%#RX64 u64UserValue3=%#RX64!\n",
                         pLogger, pLogger->u32Magic, pLogger->u32UserValue1, pLogger->u64UserValue2, pLogger->u64UserValue3));
    }
    else
        SUP_DPRINTF(("vmmR0LoggerFlush: pLogger=%p!\n", pLogger));
    return true;
}


/**
 * @callback_method_impl{FNRTLOGFLUSH, Release logger buffer flush callback.}
 */
static DECLCALLBACK(bool) vmmR0LogRelFlush(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc)
{
    return vmmR0LoggerFlushCommon(pLogger, pBufDesc, VMMLOGGER_IDX_RELEASE);
}


/**
 * @callback_method_impl{FNRTLOGFLUSH, Logger (debug) buffer flush callback.}
 */
static DECLCALLBACK(bool) vmmR0LogFlush(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc)
{
#ifdef LOG_ENABLED
    return vmmR0LoggerFlushCommon(pLogger, pBufDesc, VMMLOGGER_IDX_REGULAR);
#else
    RT_NOREF(pLogger, pBufDesc);
    return true;
#endif
}


/*
 * Override RTLogDefaultInstanceEx so we can do logging from EMTs in ring-0.
 */
DECLEXPORT(PRTLOGGER) RTLogDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
#ifdef LOG_ENABLED
    PGVMCPU pGVCpu = GVMMR0GetGVCpuByEMT(NIL_RTNATIVETHREAD);
    if (pGVCpu)
    {
        PRTLOGGER pLogger = pGVCpu->vmmr0.s.u.s.Logger.pLogger;
        if (RT_VALID_PTR(pLogger))
        {
            if (   pLogger->u64UserValue2 == (uintptr_t)pGVCpu
                && pLogger->u64UserValue3 == (uintptr_t)pGVCpu)
            {
                if (!pGVCpu->vmmr0.s.u.s.Logger.fFlushing)
                    return RTLogCheckGroupFlags(pLogger, fFlagsAndGroup);

                /*
                 * When we're flushing we _must_ return NULL here to suppress any
                 * attempts at using the logger while in vmmR0LoggerFlushCommon.
                 * The VMMR0EmtPrepareToBlock code may trigger logging in HM,
                 * which will reset the buffer content before we even get to queue
                 * the flush request.  (Only an issue when VBOX_WITH_R0_LOGGING
                 * is enabled.)
                 */
                return NULL;
            }
        }
    }
#endif
    return SUPR0DefaultLogInstanceEx(fFlagsAndGroup);
}


/*
 * Override RTLogRelGetDefaultInstanceEx so we can do LogRel to VBox.log from EMTs in ring-0.
 */
DECLEXPORT(PRTLOGGER) RTLogRelGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    PGVMCPU pGVCpu = GVMMR0GetGVCpuByEMT(NIL_RTNATIVETHREAD);
    if (pGVCpu)
    {
        PRTLOGGER pLogger = pGVCpu->vmmr0.s.u.s.RelLogger.pLogger;
        if (RT_VALID_PTR(pLogger))
        {
            if (   pLogger->u64UserValue2 == (uintptr_t)pGVCpu
                && pLogger->u64UserValue3 == (uintptr_t)pGVCpu)
            {
                if (!pGVCpu->vmmr0.s.u.s.RelLogger.fFlushing)
                    return RTLogCheckGroupFlags(pLogger, fFlagsAndGroup);

                /* ASSUMES no LogRels hidden within the VMMR0EmtPrepareToBlock code
                   path, so we don't return NULL here like for the debug logger... */
            }
        }
    }
    return SUPR0GetDefaultLogRelInstanceEx(fFlagsAndGroup);
}


/**
 * Helper for vmmR0InitLoggerSet
 */
static int vmmR0InitLoggerOne(PGVMCPU pGVCpu, bool fRelease, PVMMR0PERVCPULOGGER pR0Log, PVMMR3CPULOGGER pShared,
                              uint32_t cbBuf, char *pchBuf, RTR3PTR pchBufR3)
{
    /*
     * Create and configure the logger.
     */
    for (size_t i = 0; i < VMMLOGGER_BUFFER_COUNT; i++)
    {
        pR0Log->aBufDescs[i].u32Magic    = RTLOGBUFFERDESC_MAGIC;
        pR0Log->aBufDescs[i].uReserved   = 0;
        pR0Log->aBufDescs[i].cbBuf       = cbBuf;
        pR0Log->aBufDescs[i].offBuf      = 0;
        pR0Log->aBufDescs[i].pchBuf      = pchBuf + i * cbBuf;
        pR0Log->aBufDescs[i].pAux        = &pShared->aBufs[i].AuxDesc;

        pShared->aBufs[i].AuxDesc.fFlushedIndicator   = false;
        pShared->aBufs[i].AuxDesc.afPadding[0]        = 0;
        pShared->aBufs[i].AuxDesc.afPadding[1]        = 0;
        pShared->aBufs[i].AuxDesc.afPadding[2]        = 0;
        pShared->aBufs[i].AuxDesc.offBuf              = 0;
        pShared->aBufs[i].pchBufR3                    = pchBufR3 + i * cbBuf;
    }
    pShared->cbBuf = cbBuf;

    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    int rc = RTLogCreateEx(&pR0Log->pLogger, fRelease ? "VBOX_RELEASE_LOG" : "VBOX_LOG", RTLOG_F_NO_LOCKING | RTLOGFLAGS_BUFFERED,
                           "all", RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           VMMLOGGER_BUFFER_COUNT, pR0Log->aBufDescs, RTLOGDEST_DUMMY,
                           NULL /*pfnPhase*/, 0 /*cHistory*/, 0 /*cbHistoryFileMax*/, 0 /*cSecsHistoryTimeSlot*/,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           NULL /*pErrInfo*/, NULL /*pszFilenameFmt*/);
    if (RT_SUCCESS(rc))
    {
        PRTLOGGER pLogger = pR0Log->pLogger;
        pLogger->u32UserValue1 = VMMR0_LOGGER_FLAGS_MAGIC_VALUE;
        pLogger->u64UserValue2 = (uintptr_t)pGVCpu;
        pLogger->u64UserValue3 = (uintptr_t)pGVCpu;

        rc = RTLogSetFlushCallback(pLogger, fRelease ? vmmR0LogRelFlush : vmmR0LogFlush);
        if (RT_SUCCESS(rc))
        {
            RTLogSetR0ThreadNameF(pLogger, "EMT-%u-R0", pGVCpu->idCpu);

            /*
             * Create the event sem the EMT waits on while flushing is happening.
             */
            rc = RTSemEventCreate(&pR0Log->hEventFlushWait);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            pR0Log->hEventFlushWait = NIL_RTSEMEVENT;
        }
        RTLogDestroy(pLogger);
    }
    pR0Log->pLogger = NULL;
    return rc;
}


/**
 * Worker for VMMR0CleanupVM and vmmR0InitLoggerSet that destroys one logger.
 */
static void vmmR0TermLoggerOne(PVMMR0PERVCPULOGGER pR0Log, PVMMR3CPULOGGER pShared)
{
    RTLogDestroy(pR0Log->pLogger);
    pR0Log->pLogger = NULL;

    for (size_t i = 0; i < VMMLOGGER_BUFFER_COUNT; i++)
        pShared->aBufs[i].pchBufR3 = NIL_RTR3PTR;

    RTSemEventDestroy(pR0Log->hEventFlushWait);
    pR0Log->hEventFlushWait = NIL_RTSEMEVENT;
}


/**
 * Initializes one type of loggers for each EMT.
 */
static int vmmR0InitLoggerSet(PGVM pGVM, uint8_t idxLogger, uint32_t cbBuf, PRTR0MEMOBJ phMemObj, PRTR0MEMOBJ phMapObj)
{
    /* Allocate buffers first. */
    int rc = RTR0MemObjAllocPage(phMemObj, cbBuf * pGVM->cCpus * VMMLOGGER_BUFFER_COUNT, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        rc = RTR0MemObjMapUser(phMapObj, *phMemObj, (RTR3PTR)-1, 0 /*uAlignment*/, RTMEM_PROT_READ, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
        {
            char  * const pchBuf   = (char *)RTR0MemObjAddress(*phMemObj);
            AssertPtrReturn(pchBuf, VERR_INTERNAL_ERROR_2);

            RTR3PTR const pchBufR3 = RTR0MemObjAddressR3(*phMapObj);
            AssertReturn(pchBufR3 != NIL_RTR3PTR, VERR_INTERNAL_ERROR_3);

            /* Initialize the per-CPU loggers. */
            for (uint32_t i = 0; i < pGVM->cCpus; i++)
            {
                PGVMCPU             pGVCpu  = &pGVM->aCpus[i];
                PVMMR0PERVCPULOGGER pR0Log  = &pGVCpu->vmmr0.s.u.aLoggers[idxLogger];
                PVMMR3CPULOGGER     pShared = &pGVCpu->vmm.s.u.aLoggers[idxLogger];
                rc = vmmR0InitLoggerOne(pGVCpu, idxLogger == VMMLOGGER_IDX_RELEASE, pR0Log, pShared, cbBuf,
                                        pchBuf   + i * cbBuf * VMMLOGGER_BUFFER_COUNT,
                                        pchBufR3 + i * cbBuf * VMMLOGGER_BUFFER_COUNT);
                if (RT_FAILURE(rc))
                {
                    vmmR0TermLoggerOne(pR0Log, pShared);
                    while (i-- > 0)
                    {
                        pGVCpu = &pGVM->aCpus[i];
                        vmmR0TermLoggerOne(&pGVCpu->vmmr0.s.u.aLoggers[idxLogger], &pGVCpu->vmm.s.u.aLoggers[idxLogger]);
                    }
                    break;
                }
            }
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            /* Bail out. */
            RTR0MemObjFree(*phMapObj, false /*fFreeMappings*/);
            *phMapObj = NIL_RTR0MEMOBJ;
        }
        RTR0MemObjFree(*phMemObj, true /*fFreeMappings*/);
        *phMemObj = NIL_RTR0MEMOBJ;
    }
    return rc;
}


/**
 * Worker for VMMR0InitPerVMData that initializes all the logging related stuff.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 */
static int vmmR0InitLoggers(PGVM pGVM)
{
    /*
     * Invalidate the ring buffer (not really necessary).
     */
    for (size_t idx = 0; idx < RT_ELEMENTS(pGVM->vmmr0.s.LogFlusher.aRing); idx++)
        pGVM->vmmr0.s.LogFlusher.aRing[idx].u32 = UINT32_MAX >> 1; /* (all bits except fProcessing set) */

    /*
     * Create the spinlock and flusher event semaphore.
     */
    int rc = RTSpinlockCreate(&pGVM->vmmr0.s.LogFlusher.hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VM-Log-Flusher");
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventCreate(&pGVM->vmmr0.s.LogFlusher.hEvent);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create the ring-0 release loggers.
             */
            rc = vmmR0InitLoggerSet(pGVM, VMMLOGGER_IDX_RELEASE, _4K,
                                    &pGVM->vmmr0.s.hMemObjReleaseLogger, &pGVM->vmmr0.s.hMapObjReleaseLogger);
#ifdef LOG_ENABLED
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create debug loggers.
                 */
                rc = vmmR0InitLoggerSet(pGVM, VMMLOGGER_IDX_REGULAR, _64K,
                                        &pGVM->vmmr0.s.hMemObjLogger, &pGVM->vmmr0.s.hMapObjLogger);
            }
#endif
        }
    }
    return rc;
}


/**
 * Worker for VMMR0InitPerVMData that initializes all the logging related stuff.
 *
 * @param   pGVM            The global (ring-0) VM structure.
 */
static void vmmR0CleanupLoggers(PGVM pGVM)
{
    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
        for (size_t iLogger = 0; iLogger < RT_ELEMENTS(pGVCpu->vmmr0.s.u.aLoggers); iLogger++)
            vmmR0TermLoggerOne(&pGVCpu->vmmr0.s.u.aLoggers[iLogger], &pGVCpu->vmm.s.u.aLoggers[iLogger]);
    }

    /*
     * Free logger buffer memory.
     */
    RTR0MemObjFree(pGVM->vmmr0.s.hMapObjReleaseLogger, false /*fFreeMappings*/);
    pGVM->vmmr0.s.hMapObjReleaseLogger = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->vmmr0.s.hMemObjReleaseLogger, true /*fFreeMappings*/);
    pGVM->vmmr0.s.hMemObjReleaseLogger = NIL_RTR0MEMOBJ;

    RTR0MemObjFree(pGVM->vmmr0.s.hMapObjLogger, false /*fFreeMappings*/);
    pGVM->vmmr0.s.hMapObjLogger = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->vmmr0.s.hMemObjLogger, true /*fFreeMappings*/);
    pGVM->vmmr0.s.hMemObjLogger = NIL_RTR0MEMOBJ;

    /*
     * Free log flusher related stuff.
     */
    RTSpinlockDestroy(pGVM->vmmr0.s.LogFlusher.hSpinlock);
    pGVM->vmmr0.s.LogFlusher.hSpinlock = NIL_RTSPINLOCK;
    RTSemEventDestroy(pGVM->vmmr0.s.LogFlusher.hEvent);
    pGVM->vmmr0.s.LogFlusher.hEvent = NIL_RTSEMEVENT;
}


/*********************************************************************************************************************************
*   Assertions                                                                                                                   *
*********************************************************************************************************************************/

/**
 * Installs a notification callback for ring-0 assertions.
 *
 * @param   pVCpu         The cross context virtual CPU structure.
 * @param   pfnCallback   Pointer to the callback.
 * @param   pvUser        The user argument.
 *
 * @return VBox status code.
 */
VMMR0_INT_DECL(int) VMMR0AssertionSetNotification(PVMCPUCC pVCpu, PFNVMMR0ASSERTIONNOTIFICATION pfnCallback, RTR0PTR pvUser)
{
    AssertPtrReturn(pVCpu, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    if (!pVCpu->vmmr0.s.pfnAssertCallback)
    {
        pVCpu->vmmr0.s.pfnAssertCallback    = pfnCallback;
        pVCpu->vmmr0.s.pvAssertCallbackUser = pvUser;
        return VINF_SUCCESS;
    }
    return VERR_ALREADY_EXISTS;
}


/**
 * Removes the ring-0 callback.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(void) VMMR0AssertionRemoveNotification(PVMCPUCC pVCpu)
{
    pVCpu->vmmr0.s.pfnAssertCallback    = NULL;
    pVCpu->vmmr0.s.pvAssertCallbackUser = NULL;
}


/**
 * Checks whether there is a ring-0 callback notification active.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @returns true if there the notification is active, false otherwise.
 */
VMMR0_INT_DECL(bool) VMMR0AssertionIsNotificationSet(PVMCPUCC pVCpu)
{
    return pVCpu->vmmr0.s.pfnAssertCallback != NULL;
}


/*
 * Jump back to ring-3 if we're the EMT and the longjmp is armed.
 *
 * @returns true if the breakpoint should be hit, false if it should be ignored.
 */
DECLEXPORT(bool) RTCALL RTAssertShouldPanic(void)
{
#if 0
    return true;
#else
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
    {
        PVMCPUCC pVCpu = VMMGetCpu(pVM);

        if (pVCpu)
        {
# ifdef RT_ARCH_X86
            if (pVCpu->vmmr0.s.AssertJmpBuf.eip)
# else
            if (pVCpu->vmmr0.s.AssertJmpBuf.rip)
# endif
            {
                if (pVCpu->vmmr0.s.pfnAssertCallback)
                    pVCpu->vmmr0.s.pfnAssertCallback(pVCpu, pVCpu->vmmr0.s.pvAssertCallbackUser);
                int rc = vmmR0CallRing3LongJmp(&pVCpu->vmmr0.s.AssertJmpBuf, VERR_VMM_RING0_ASSERTION);
                return RT_FAILURE_NP(rc);
            }
        }
    }
# ifdef RT_OS_LINUX
    return true;
# else
    return false;
# endif
#endif
}


/*
 * Override this so we can push it up to ring-3.
 */
DECLEXPORT(void) RTCALL RTAssertMsg1Weak(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * To host kernel log/whatever.
     */
    SUPR0Printf("!!R0-Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                pszExpr, pszFile, uLine, pszFunction);

    /*
     * To the log.
     */
    LogAlways(("\n!!R0-Assertion Failed!!\n"
               "Expression: %s\n"
               "Location  : %s(%d) %s\n",
               pszExpr, pszFile, uLine, pszFunction));

    /*
     * To the global VMM buffer.
     */
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
        RTStrPrintf(pVM->vmm.s.szRing0AssertMsg1, sizeof(pVM->vmm.s.szRing0AssertMsg1),
                    "\n!!R0-Assertion Failed!!\n"
                    "Expression: %.*s\n"
                    "Location  : %s(%d) %s\n",
                    sizeof(pVM->vmm.s.szRing0AssertMsg1) / 4 * 3, pszExpr,
                    pszFile, uLine, pszFunction);

    /*
     * Continue the normal way.
     */
    RTAssertMsg1(pszExpr, uLine, pszFile, pszFunction);
}


/**
 * Callback for RTLogFormatV which writes to the ring-3 log port.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogOutput(void *pv, const char *pachChars, size_t cbChars)
{
    for (size_t i = 0; i < cbChars; i++)
    {
        LogAlways(("%c", pachChars[i])); NOREF(pachChars);
    }

    NOREF(pv);
    return cbChars;
}


/*
 * Override this so we can push it up to ring-3.
 */
DECLEXPORT(void) RTCALL RTAssertMsg2WeakV(const char *pszFormat, va_list va)
{
    va_list vaCopy;

    /*
     * Push the message to the loggers.
     */
    PRTLOGGER pLog = RTLogRelGetDefaultInstance();
    if (pLog)
    {
        va_copy(vaCopy, va);
        RTLogFormatV(rtLogOutput, pLog, pszFormat, vaCopy);
        va_end(vaCopy);
    }
    pLog = RTLogGetDefaultInstance(); /* Don't initialize it here... */
    if (pLog)
    {
        va_copy(vaCopy, va);
        RTLogFormatV(rtLogOutput, pLog, pszFormat, vaCopy);
        va_end(vaCopy);
    }

    /*
     * Push it to the global VMM buffer.
     */
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
    {
        va_copy(vaCopy, va);
        RTStrPrintfV(pVM->vmm.s.szRing0AssertMsg2, sizeof(pVM->vmm.s.szRing0AssertMsg2), pszFormat, vaCopy);
        va_end(vaCopy);
    }

    /*
     * Continue the normal way.
     */
    RTAssertMsg2V(pszFormat, va);
}

