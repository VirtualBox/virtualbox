/* $Id: STAM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * STAM - The Statistics Manager.
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

/** @page pg_stam       STAM - The Statistics Manager
 *
 * The purpose for the statistics manager is to present the rest of the system
 * with a somewhat uniform way of accessing VMM statistics.  STAM sports a
 * couple of different APIs for accessing them: STAMR3EnumU, STAMR3SnapshotU,
 * STAMR3DumpU, STAMR3DumpToReleaseLogU and the debugger.  Main is exposing the
 * XML based one, STAMR3SnapshotU.
 *
 * The rest of the VMM together with the devices and drivers registers their
 * statistics with STAM giving them a name.  The name is hierarchical, the
 * components separated by slashes ('/') and must start with a slash.
 *
 * Each item registered with STAM - also, half incorrectly, called a sample -
 * has a type, unit, visibility, data pointer and description associated with it
 * in addition to the name (described above).  The type tells STAM what kind of
 * structure the pointer is pointing to.  The visibility allows unused
 * statistics from cluttering the output or showing up in the GUI.  All the bits
 * together makes STAM able to present the items in a sensible way to the user.
 * Some types also allows STAM to reset the data, which is very convenient when
 * digging into specific operations and such.
 *
 * PS. The VirtualBox Debugger GUI has a viewer for inspecting the statistics
 * STAM provides.  You will also find statistics in the release and debug logs.
 * And as mentioned in the introduction, the debugger console features a couple
 * of command: .stats and .statsreset.
 *
 * @see grp_stam
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_STAM
#include <VBox/vmm/stam.h>
#include "STAMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>
#include <VBox/dbg.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum name length excluding the terminator. */
#define STAM_MAX_NAME_LEN   239


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Argument structure for stamR3PrintOne().
 */
typedef struct STAMR3PRINTONEARGS
{
    PUVM        pUVM;
    void       *pvArg;
    DECLCALLBACKMEMBER(void, pfnPrintf,(struct STAMR3PRINTONEARGS *pvArg, const char *pszFormat, ...));
} STAMR3PRINTONEARGS, *PSTAMR3PRINTONEARGS;


/**
 * Argument structure to stamR3EnumOne().
 */
typedef struct STAMR3ENUMONEARGS
{
    PVM             pVM;
    PFNSTAMR3ENUM   pfnEnum;
    void           *pvUser;
} STAMR3ENUMONEARGS, *PSTAMR3ENUMONEARGS;


/**
 * The snapshot status structure.
 * Argument package passed to stamR3SnapshotOne, stamR3SnapshotPrintf and stamR3SnapshotOutput.
 */
typedef struct STAMR3SNAPSHOTONE
{
    /** Pointer to the buffer start. */
    char           *pszStart;
    /** Pointer to the buffer end. */
    char           *pszEnd;
    /** Pointer to the current buffer position. */
    char           *psz;
    /** Pointer to the VM. */
    PVM             pVM;
    /** The number of bytes allocated. */
    size_t          cbAllocated;
    /** The status code. */
    int             rc;
    /** Whether to include the description strings. */
    bool            fWithDesc;
} STAMR3SNAPSHOTONE, *PSTAMR3SNAPSHOTONE;


/**
 * Init record for a ring-0 statistic sample.
 */
typedef struct STAMR0SAMPLE
{
    /** The GVMMSTATS structure offset of the variable. */
    unsigned        offVar;
    /** The type. */
    STAMTYPE        enmType;
    /** The unit. */
    STAMUNIT        enmUnit;
    /** The name. */
    const char     *pszName;
    /** The description. */
    const char     *pszDesc;
} STAMR0SAMPLE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void                 stamR3LookupDestroyTree(PSTAMLOOKUP pRoot);
static int                  stamR3RegisterU(PUVM pUVM, void *pvSample, PFNSTAMR3CALLBACKRESET pfnReset,
                                            PFNSTAMR3CALLBACKPRINT pfnPrint, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                            const char *pszName, STAMUNIT enmUnit, const char *pszDesc, uint8_t iRefreshGrp);
static int                  stamR3ResetOne(PSTAMDESC pDesc, void *pvArg);
static DECLCALLBACK(void)   stamR3EnumLogPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static DECLCALLBACK(void)   stamR3EnumRelLogPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static DECLCALLBACK(void)   stamR3EnumPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static int                  stamR3SnapshotOne(PSTAMDESC pDesc, void *pvArg);
static int                  stamR3SnapshotPrintf(PSTAMR3SNAPSHOTONE pThis, const char *pszFormat, ...);
static int                  stamR3PrintOne(PSTAMDESC pDesc, void *pvArg);
static int                  stamR3EnumOne(PSTAMDESC pDesc, void *pvArg);
static bool                 stamR3MultiMatch(const char * const *papszExpressions, unsigned cExpressions, unsigned *piExpression, const char *pszName);
static char **              stamR3SplitPattern(const char *pszPat, unsigned *pcExpressions, char **ppszCopy);
static int                  stamR3EnumU(PUVM pUVM, const char *pszPat, bool fUpdateRing0, int (pfnCallback)(PSTAMDESC pDesc, void *pvArg), void *pvArg);
static void                 stamR3Ring0StatsRegisterU(PUVM pUVM);

#ifdef VBOX_WITH_DEBUGGER
static FNDBGCCMD            stamR3CmdStats;
static DECLCALLBACK(void)   stamR3EnumDbgfPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...);
static FNDBGCCMD            stamR3CmdStatsReset;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_DEBUGGER
/** Pattern argument. */
static const DBGCVARDESC    g_aArgPat[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "pattern",      "Which samples the command shall be applied to. Use '*' as wildcard. Use ';' to separate expression." }
};

/** Command descriptors. */
static const DBGCCMD    g_aCmds[] =
{
    /* pszCmd,      cArgsMin, cArgsMax, paArgDesc,          cArgDescs,                  fFlags,     pfnHandler          pszSyntax,          ....pszDescription */
    { "stats",      0,        1,        &g_aArgPat[0],      RT_ELEMENTS(g_aArgPat),     0,          stamR3CmdStats,     "[pattern]",        "Display statistics." },
    { "statsreset", 0,        1,        &g_aArgPat[0],      RT_ELEMENTS(g_aArgPat),     0,          stamR3CmdStatsReset,"[pattern]",        "Resets statistics." }
};
#endif


/**
 * The GVMM mapping records - sans the host cpus.
 */
static const STAMR0SAMPLE g_aGVMMStats[] =
{
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cHaltCalls),        STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/HaltCalls", "The number of calls to GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cHaltBlocking),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/HaltBlocking", "The number of times we did go to sleep in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cHaltTimeouts),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/HaltTimeouts", "The number of times we timed out in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cHaltNotBlocking),  STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/HaltNotBlocking", "The number of times we didn't go to sleep in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cHaltWakeUps),      STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/HaltWakeUps", "The number of wake ups done during GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cWakeUpCalls),      STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/WakeUpCalls", "The number of calls to GVMMR0WakeUp." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cWakeUpNotHalted),  STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/WakeUpNotHalted", "The number of times the EMT thread wasn't actually halted when GVMMR0WakeUp was called." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cWakeUpWakeUps),    STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/WakeUpWakeUps", "The number of wake ups done during GVMMR0WakeUp (not counting the explicit one)." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cPokeCalls),        STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/PokeCalls", "The number of calls to GVMMR0Poke." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cPokeNotBusy),      STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/PokeNotBusy", "The number of times the EMT thread wasn't actually busy when GVMMR0Poke was called." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cPollCalls),        STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/PollCalls", "The number of calls to GVMMR0SchedPoll." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cPollHalts),        STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/PollHalts", "The number of times the EMT has halted in a GVMMR0SchedPoll call." },
    { RT_UOFFSETOF(GVMMSTATS, SchedVM.cPollWakeUps),      STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/VM/PollWakeUps", "The number of wake ups done during GVMMR0SchedPoll." },

    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cHaltCalls),       STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/HaltCalls", "The number of calls to GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cHaltBlocking),    STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/HaltBlocking", "The number of times we did go to sleep in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cHaltTimeouts),    STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/HaltTimeouts", "The number of times we timed out in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cHaltNotBlocking), STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/HaltNotBlocking", "The number of times we didn't go to sleep in GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cHaltWakeUps),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/HaltWakeUps", "The number of wake ups done during GVMMR0SchedHalt." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cWakeUpCalls),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/WakeUpCalls", "The number of calls to GVMMR0WakeUp." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cWakeUpNotHalted), STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/WakeUpNotHalted", "The number of times the EMT thread wasn't actually halted when GVMMR0WakeUp was called." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cWakeUpWakeUps),   STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/WakeUpWakeUps", "The number of wake ups done during GVMMR0WakeUp (not counting the explicit one)." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cPokeCalls),       STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/PokeCalls", "The number of calls to GVMMR0Poke." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cPokeNotBusy),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/PokeNotBusy", "The number of times the EMT thread wasn't actually busy when GVMMR0Poke was called." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cPollCalls),       STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/PollCalls", "The number of calls to GVMMR0SchedPoll." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cPollHalts),       STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/PollHalts", "The number of times the EMT has halted in a GVMMR0SchedPoll call." },
    { RT_UOFFSETOF(GVMMSTATS, SchedSum.cPollWakeUps),     STAMTYPE_U64_RESET, STAMUNIT_CALLS, "/GVMM/Sum/PollWakeUps", "The number of wake ups done during GVMMR0SchedPoll." },

    { RT_UOFFSETOF(GVMMSTATS, cVMs),                      STAMTYPE_U32,       STAMUNIT_COUNT, "/GVMM/VMs", "The number of VMs accessible to the caller." },
    { RT_UOFFSETOF(GVMMSTATS, cEMTs),                     STAMTYPE_U32,       STAMUNIT_COUNT, "/GVMM/EMTs", "The number of emulation threads." },
    { RT_UOFFSETOF(GVMMSTATS, cHostCpus),                 STAMTYPE_U32,       STAMUNIT_COUNT, "/GVMM/HostCPUs", "The number of host CPUs." },
};


/**
 * The GMM mapping records.
 */
static const STAMR0SAMPLE g_aGMMStats[] =
{
    { RT_UOFFSETOF(GMMSTATS, cMaxPages),                        STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cMaxPages",                   "The maximum number of pages GMM is allowed to allocate." },
    { RT_UOFFSETOF(GMMSTATS, cReservedPages),                   STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cReservedPages",              "The number of pages that has been reserved." },
    { RT_UOFFSETOF(GMMSTATS, cOverCommittedPages),              STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cOverCommittedPages",         "The number of pages that we have over-committed in reservations." },
    { RT_UOFFSETOF(GMMSTATS, cAllocatedPages),                  STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cAllocatedPages",             "The number of actually allocated (committed if you like) pages." },
    { RT_UOFFSETOF(GMMSTATS, cSharedPages),                     STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cSharedPages",                "The number of pages that are shared. A subset of cAllocatedPages." },
    { RT_UOFFSETOF(GMMSTATS, cDuplicatePages),                  STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cDuplicatePages",             "The number of pages that are actually shared between VMs." },
    { RT_UOFFSETOF(GMMSTATS, cLeftBehindSharedPages),           STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cLeftBehindSharedPages",      "The number of pages that are shared that has been left behind by VMs not doing proper cleanups." },
    { RT_UOFFSETOF(GMMSTATS, cBalloonedPages),                  STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/cBalloonedPages",             "The number of current ballooned pages." },
    { RT_UOFFSETOF(GMMSTATS, cChunks),                          STAMTYPE_U32,   STAMUNIT_COUNT, "/GMM/cChunks",                     "The number of allocation chunks." },
    { RT_UOFFSETOF(GMMSTATS, cFreedChunks),                     STAMTYPE_U32,   STAMUNIT_COUNT, "/GMM/cFreedChunks",                "The number of freed chunks ever." },
    { RT_UOFFSETOF(GMMSTATS, cShareableModules),                STAMTYPE_U32,   STAMUNIT_COUNT, "/GMM/cShareableModules",           "The number of shareable modules." },
    { RT_UOFFSETOF(GMMSTATS, idFreeGeneration),                 STAMTYPE_U64,   STAMUNIT_NONE,  "/GMM/idFreeGeneration",            "The current chunk freeing generation number (for per-VM chunk lookup TLB versioning)." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Reserved.cBasePages),      STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/Reserved/cBasePages",      "The amount of base memory (RAM, ROM, ++) reserved by the VM." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Reserved.cShadowPages),    STAMTYPE_U32,   STAMUNIT_PAGES, "/GMM/VM/Reserved/cShadowPages",    "The amount of memory reserved for shadow/nested page tables." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Reserved.cFixedPages),     STAMTYPE_U32,   STAMUNIT_PAGES, "/GMM/VM/Reserved/cFixedPages",     "The amount of memory reserved for fixed allocations like MMIO2 and the hyper heap." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Allocated.cBasePages),     STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/Allocated/cBasePages",     "The amount of base memory (RAM, ROM, ++) allocated by the VM." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Allocated.cShadowPages),   STAMTYPE_U32,   STAMUNIT_PAGES, "/GMM/VM/Allocated/cShadowPages",   "The amount of memory allocated for shadow/nested page tables." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.Allocated.cFixedPages),    STAMTYPE_U32,   STAMUNIT_PAGES, "/GMM/VM/Allocated/cFixedPages",    "The amount of memory allocated for fixed allocations like MMIO2 and the hyper heap." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cPrivatePages),            STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cPrivatePages",            "The current number of private pages." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cSharedPages),             STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cSharedPages",             "The current number of shared pages." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cBalloonedPages),          STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cBalloonedPages",          "The current number of ballooned pages." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cMaxBalloonedPages),       STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cMaxBalloonedPages",       "The max number of pages that can be ballooned." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cReqBalloonedPages),       STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cReqBalloonedPages",       "The number of pages we've currently requested the guest to give us." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cReqActuallyBalloonedPages),STAMTYPE_U64,  STAMUNIT_PAGES, "/GMM/VM/cReqActuallyBalloonedPages","The number of pages the guest has given us in response to the request." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cReqDeflatePages),         STAMTYPE_U64,   STAMUNIT_PAGES, "/GMM/VM/cReqDeflatePages",         "The number of pages we've currently requested the guest to take back." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.cShareableModules),        STAMTYPE_U32,   STAMUNIT_COUNT, "/GMM/VM/cShareableModules",        "The number of shareable modules traced by the VM." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.enmPolicy),                STAMTYPE_U32,   STAMUNIT_NONE,  "/GMM/VM/enmPolicy",                "The current over-commit policy." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.enmPriority),              STAMTYPE_U32,   STAMUNIT_NONE,  "/GMM/VM/enmPriority",              "The VM priority for arbitrating VMs in low and out of memory situation." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.fBallooningEnabled),       STAMTYPE_BOOL,  STAMUNIT_NONE,  "/GMM/VM/fBallooningEnabled",       "Whether ballooning is enabled or not." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.fSharedPagingEnabled),     STAMTYPE_BOOL,  STAMUNIT_NONE,  "/GMM/VM/fSharedPagingEnabled",     "Whether shared paging is enabled or not." },
    { RT_UOFFSETOF(GMMSTATS, VMStats.fMayAllocate),             STAMTYPE_BOOL,  STAMUNIT_NONE,  "/GMM/VM/fMayAllocate",             "Whether the VM is allowed to allocate memory or not." },
};


/**
 * Initializes the STAM.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM structure.
 */
VMMR3DECL(int) STAMR3InitUVM(PUVM pUVM)
{
    LogFlow(("STAMR3Init\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompile(sizeof(pUVM->stam.s) <= sizeof(pUVM->stam.padding));
    AssertRelease(sizeof(pUVM->stam.s) <= sizeof(pUVM->stam.padding));

    /*
     * Initialize the read/write lock and list.
     */
    int rc = RTSemRWCreate(&pUVM->stam.s.RWSem);
    AssertRCReturn(rc, rc);

    RTListInit(&pUVM->stam.s.List);

    /*
     * Initialize the root node.
     */
    PSTAMLOOKUP pRoot = (PSTAMLOOKUP)RTMemAlloc(sizeof(STAMLOOKUP));
    if (!pRoot)
    {
        RTSemRWDestroy(pUVM->stam.s.RWSem);
        pUVM->stam.s.RWSem = NIL_RTSEMRW;
        return VERR_NO_MEMORY;
    }
    pRoot->pParent      = NULL;
    pRoot->papChildren   = NULL;
    pRoot->pDesc        = NULL;
    pRoot->cDescsInTree = 0;
    pRoot->cChildren    = 0;
    pRoot->iParent      = UINT16_MAX;
    pRoot->off          = 0;
    pRoot->cch          = 0;
    pRoot->szName[0]    = '\0';

    pUVM->stam.s.pRoot = pRoot;

    /*
     * Register the ring-0 statistics (GVMM/GMM).
     */
    if (!SUPR3IsDriverless())
        stamR3Ring0StatsRegisterU(pUVM);

#ifdef VBOX_WITH_DEBUGGER
    /*
     * Register debugger commands.
     */
    static bool fRegisteredCmds = false;
    if (!fRegisteredCmds)
    {
        rc = DBGCRegisterCommands(&g_aCmds[0], RT_ELEMENTS(g_aCmds));
        if (RT_SUCCESS(rc))
            fRegisteredCmds = true;
    }
#endif

    return VINF_SUCCESS;
}


/**
 * Terminates the STAM.
 *
 * @param   pUVM        Pointer to the user mode VM structure.
 */
VMMR3DECL(void) STAMR3TermUVM(PUVM pUVM)
{
    /*
     * Free used memory and the RWLock.
     */
    PSTAMDESC pCur, pNext;
    RTListForEachSafe(&pUVM->stam.s.List, pCur, pNext, STAMDESC, ListEntry)
    {
        pCur->pLookup->pDesc = NULL;
        if (   pCur->enmType != STAMTYPE_INTERNAL_SUM
            && pCur->enmType != STAMTYPE_INTERNAL_PCT_OF_SUM)
        { /* likely*/ }
        else
            RTMemFree(pCur->u.pSum);
        RTMemFree(pCur);
    }

    stamR3LookupDestroyTree(pUVM->stam.s.pRoot);
    pUVM->stam.s.pRoot = NULL;

    Assert(pUVM->stam.s.RWSem != NIL_RTSEMRW);
    RTSemRWDestroy(pUVM->stam.s.RWSem);
    pUVM->stam.s.RWSem = NIL_RTSEMRW;
}


/**
 * Registers a sample with the statistics manager.
 *
 * Statistics are maintained on a per VM basis and is normally registered
 * during the VM init stage, but there is nothing preventing you from
 * register them at runtime.
 *
 * Use STAMR3Deregister() to deregister statistics at runtime, however do
 * not bother calling at termination time.
 *
 * It is not possible to register the same sample twice.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszName     Sample name. The name is on this form "/<component>/<sample>".
 *                      Further nesting is possible.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 */
VMMR3DECL(int)  STAMR3RegisterU(PUVM pUVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, const char *pszName,
                                STAMUNIT enmUnit, const char *pszDesc)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK && enmType < STAMTYPE_FIRST_INTERNAL_TYPE, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    return stamR3RegisterU(pUVM, pvSample, NULL, NULL, enmType, enmVisibility, pszName, enmUnit, pszDesc, STAM_REFRESH_GRP_NONE);
}


/**
 * Registers a sample with the statistics manager.
 *
 * Statistics are maintained on a per VM basis and is normally registered
 * during the VM init stage, but there is nothing preventing you from
 * register them at runtime.
 *
 * Use STAMR3Deregister() to deregister statistics at runtime, however do
 * not bother calling at termination time.
 *
 * It is not possible to register the same sample twice.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszName     Sample name. The name is on this form "/<component>/<sample>".
 *                      Further nesting is possible.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 */
VMMR3DECL(int)  STAMR3Register(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, const char *pszName,
                               STAMUNIT enmUnit, const char *pszDesc)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK && enmType < STAMTYPE_FIRST_INTERNAL_TYPE, VERR_INVALID_PARAMETER);
    return stamR3RegisterU(pVM->pUVM, pvSample, NULL, NULL, enmType, enmVisibility, pszName, enmUnit, pszDesc,
                           STAM_REFRESH_GRP_NONE);
}


/**
 * Same as STAMR3RegisterU except that the name is specified in a
 * RTStrPrintf like fashion.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 */
VMMR3DECL(int)  STAMR3RegisterFU(PUVM pUVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                 const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterVU(pUVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3Register except that the name is specified in a
 * RTStrPrintf like fashion.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 */
VMMR3DECL(int)  STAMR3RegisterF(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterVU(pVM->pUVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3Register except that the name is specified in a
 * RTStrPrintfV like fashion.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 */
VMMR3DECL(int)  STAMR3RegisterVU(PUVM pUVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                 const char *pszDesc, const char *pszName, va_list args)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK, VERR_INVALID_PARAMETER);

    char   szFormattedName[STAM_MAX_NAME_LEN + 8];
    size_t cch = RTStrPrintfV(szFormattedName, sizeof(szFormattedName), pszName, args);
    AssertReturn(cch <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);

    return STAMR3RegisterU(pUVM, pvSample, enmType, enmVisibility, szFormattedName, enmUnit, pszDesc);
}


/**
 * Same as STAMR3Register except that the name is specified in a
 * RTStrPrintfV like fashion.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 */
VMMR3DECL(int)  STAMR3RegisterV(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                const char *pszDesc, const char *pszName, va_list args)
{
    return STAMR3RegisterVU(pVM->pUVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
}


/**
 * Similar to STAMR3Register except for the two callbacks, the implied type (STAMTYPE_CALLBACK),
 * and name given in an RTStrPrintf like fashion.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
VMMR3DECL(int)  STAMR3RegisterCallback(PVM pVM, void *pvSample, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                       PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                                       const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterCallbackV(pVM, pvSample, enmVisibility, enmUnit, pfnReset, pfnPrint, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3RegisterCallback() except for the ellipsis which is a va_list here.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
VMMR3DECL(int)  STAMR3RegisterCallbackV(PVM pVM, void *pvSample, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                        PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                                        const char *pszDesc, const char *pszName, va_list args)
{
    char *pszFormattedName;
    RTStrAPrintfV(&pszFormattedName, pszName, args);
    if (!pszFormattedName)
        return VERR_NO_MEMORY;

    int rc = stamR3RegisterU(pVM->pUVM, pvSample, pfnReset, pfnPrint, STAMTYPE_CALLBACK, enmVisibility, pszFormattedName,
                             enmUnit, pszDesc, STAM_REFRESH_GRP_NONE);
    RTStrFree(pszFormattedName);
    return rc;
}


/**
 * Same as STAMR3RegisterFU, except there is an extra refresh group parameter.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   iRefreshGrp The refresh group, STAM_REFRESH_GRP_XXX.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterRefresh(PUVM pUVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                     uint8_t iRefreshGrp, const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterRefreshV(pUVM, pvSample, enmType, enmVisibility, enmUnit, iRefreshGrp, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3RegisterVU, except there is an extra refresh group parameter.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   iRefreshGrp The refresh group, STAM_REFRESH_GRP_XXX.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   va          Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterRefreshV(PUVM pUVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                      uint8_t iRefreshGrp, const char *pszDesc, const char *pszName, va_list va)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK && enmType < STAMTYPE_FIRST_INTERNAL_TYPE, VERR_INVALID_PARAMETER);

    char   szFormattedName[STAM_MAX_NAME_LEN + 8];
    size_t cch = RTStrPrintfV(szFormattedName, sizeof(szFormattedName), pszName, va);
    AssertReturn(cch <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    return stamR3RegisterU(pUVM, pvSample, NULL, NULL, enmType, enmVisibility, pszName, enmUnit, pszDesc, iRefreshGrp);
}


/**
 * Refreshes the cached sum (STAMSUMSAMPLE::u) of a sum sample.
 */
static void stamR3SumRefresh(PSTAMSUMSAMPLE pSum)
{
    switch (pSum->enmType)
    {
        case STAMTYPE_COUNTER:
        {
            uint64_t  uSum = 0;
            uintptr_t i    = pSum->cSummands;
            while (i-- > 0)
            {
                PSTAMDESC const pDesc = pSum->apSummands[i];
                switch (pDesc->enmType)
                {
                    case STAMTYPE_COUNTER:
                        uSum += pDesc->u.pCounter->c;
                        break;

                    case STAMTYPE_U64:
                    case STAMTYPE_U64_RESET:
                    case STAMTYPE_X64:
                    case STAMTYPE_X64_RESET:
                        uSum += *pDesc->u.pu64;
                        break;

                    case STAMTYPE_U32:
                    case STAMTYPE_U32_RESET:
                    case STAMTYPE_X32:
                    case STAMTYPE_X32_RESET:
                        uSum += *pDesc->u.pu32;
                        break;

                    case STAMTYPE_U16:
                    case STAMTYPE_U16_RESET:
                    case STAMTYPE_X16:
                    case STAMTYPE_X16_RESET:
                        uSum += *pDesc->u.pu16;
                        break;

                    case STAMTYPE_U8:
                    case STAMTYPE_U8_RESET:
                    case STAMTYPE_X8:
                    case STAMTYPE_X8_RESET:
                        uSum += *pDesc->u.pu8;
                        break;

                    default:
                        AssertFailedBreak();
                }
            }
            pSum->u.Counter.c = uSum;
            break;
        }

        case STAMTYPE_PROFILE:
        {
            uint64_t    cPeriods = 0;
            uint64_t    uTotal   = 0;
            uint64_t    uMax     = 0;
            uint64_t    uMin     = UINT64_MAX;
            uintptr_t   i        = pSum->cSummands;
            while (i-- > 0)
            {
                PSTAMDESC const pDesc = pSum->apSummands[i];
                AssertContinue(   pDesc->enmType == STAMTYPE_PROFILE
                               || pDesc->enmType == STAMTYPE_PROFILE_ADV);
                PSTAMPROFILE const pProfile = pDesc->u.pProfile;
                cPeriods += pProfile->cPeriods;
                uTotal   += pProfile->cTicks;
                uint64_t u = pProfile->cTicksMax;
                if (u > uMax)
                    uMax = u;
                u = pProfile->cTicksMin;
                if (u < uMin)
                    uMin = u;
            }

            pSum->u.Profile.cTicks    = uTotal;
            pSum->u.Profile.cPeriods  = cPeriods;
            pSum->u.Profile.cTicksMin = uMin;
            pSum->u.Profile.cTicksMax = uMax;
            break;
        }

        default:
            AssertFailedReturnVoid();
    }
}


/**
 * Used by STAMR3RegisterSumV to locate the samples to sum up.
 */
static int stamR3RegisterSumEnumCallback(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMSUMSAMPLE const pSum = (PSTAMSUMSAMPLE)pvArg;
    if (pSum->cSummands == 0)
    {
        /*
         * The first time around we check that the type is a supported one
         * and just set the unit.
         */
        switch (pDesc->enmType)
        {
            case STAMTYPE_COUNTER:
            case STAMTYPE_U64:
            case STAMTYPE_U64_RESET:
            case STAMTYPE_X64:
            case STAMTYPE_X64_RESET:
            case STAMTYPE_U32:
            case STAMTYPE_U32_RESET:
            case STAMTYPE_X32:
            case STAMTYPE_X32_RESET:
            case STAMTYPE_U16:
            case STAMTYPE_U16_RESET:
            case STAMTYPE_X16:
            case STAMTYPE_X16_RESET:
            case STAMTYPE_U8:
            case STAMTYPE_U8_RESET:
            case STAMTYPE_X8:
            case STAMTYPE_X8_RESET:
                pSum->enmType = STAMTYPE_COUNTER;
                break;

            case STAMTYPE_PROFILE:
            case STAMTYPE_PROFILE_ADV:
                pSum->enmType = STAMTYPE_PROFILE;
                break;

            default:
                AssertMsgFailedReturn(("Summing up enmType=%d types have not been implemented yet! Sorry.\n", pDesc->enmType),
                                      VERR_WRONG_TYPE);
        }
        pSum->enmTypeFirst = pDesc->enmType;
        pSum->enmUnit      = pDesc->enmUnit;
    }
    else
    {
        /*
         * Make sure additional sample compatible with the first,
         * both type and unit.
         */
        if (RT_LIKELY(   pDesc->enmType == pSum->enmType
                      || pDesc->enmType == (STAMTYPE)pSum->enmTypeFirst))
        { /* likely */ }
        else
        {
            switch (pSum->enmType)
            {
                case STAMTYPE_COUNTER:
                    AssertMsgReturn(   pDesc->enmType == STAMTYPE_COUNTER
                                    || pDesc->enmType == STAMTYPE_U64
                                    || pDesc->enmType == STAMTYPE_U64_RESET
                                    || pDesc->enmType == STAMTYPE_X64
                                    || pDesc->enmType == STAMTYPE_X64_RESET
                                    || pDesc->enmType == STAMTYPE_U32
                                    || pDesc->enmType == STAMTYPE_U32_RESET
                                    || pDesc->enmType == STAMTYPE_X32
                                    || pDesc->enmType == STAMTYPE_X32_RESET
                                    || pDesc->enmType == STAMTYPE_U16
                                    || pDesc->enmType == STAMTYPE_U16_RESET
                                    || pDesc->enmType == STAMTYPE_X16
                                    || pDesc->enmType == STAMTYPE_X16_RESET
                                    || pDesc->enmType == STAMTYPE_U8
                                    || pDesc->enmType == STAMTYPE_U8_RESET
                                    || pDesc->enmType == STAMTYPE_X8
                                    || pDesc->enmType == STAMTYPE_X8_RESET,
                                    ("Unsupported type mixup: %d & %d (%s)\n", pSum->enmType, pDesc->enmType, pDesc->pszName),
                                    VERR_MISMATCH);
                    break;

                case STAMTYPE_PROFILE:
                    AssertMsgReturn(   pDesc->enmType == STAMTYPE_PROFILE
                                    || pDesc->enmType == STAMTYPE_PROFILE_ADV,
                                    ("Unsupported type mixup: %d & %d (%s)\n", pSum->enmType, pDesc->enmType, pDesc->pszName),
                                    VERR_MISMATCH);
                    break;

                default:
                    AssertFailedReturn(VERR_MISMATCH);
            }
        }

        if (RT_LIKELY(pDesc->enmUnit == pSum->enmUnit))
        { /* likely */ }
        else if (pDesc->enmUnit != STAMUNIT_NONE)
        {
            AssertReturn(pSum->enmUnit == STAMUNIT_NONE, VERR_MISMATCH);
            pSum->enmUnit = pDesc->enmUnit;
        }

        AssertReturn(pSum->cSummands < pSum->cSummandsAlloc, VERR_TOO_MUCH_DATA);
    }
    pSum->apSummands[pSum->cSummands++] = pDesc;
    return VINF_SUCCESS;
}


/**
 * Registers a sum that is to be calculated from the @a pszSummandPattern hits.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user mode VM structure.
 * @param   enmVisibility       Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszSummandPattern   A simple pattern for the elements that should be
 *                              summed up.  These must have matching types and
 *                              units.
 * @param   pszDesc             Sample description.
 * @param   pszName             The sample name format string.
 * @param   va                  Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterSumV(PUVM pUVM, STAMVISIBILITY enmVisibility, const char *pszSummandPattern,
                                  const char *pszDesc, const char *pszName, va_list va)
{
    char   szFormattedName[STAM_MAX_NAME_LEN + 8];
    size_t cch = RTStrPrintfV(szFormattedName, sizeof(szFormattedName), pszName, va);
    AssertReturn(cch <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * We have to resolve the summands before we continue with the actual registration.
     */
    uint8_t const        cMaxSummands = 32;
    PSTAMSUMSAMPLE const pSum = (PSTAMSUMSAMPLE)RTMemAllocZ(RT_UOFFSETOF_DYN(STAMSUMSAMPLE, apSummands[cMaxSummands]));
    AssertReturn(pSum, VERR_NO_MEMORY);
    pSum->cSummandsAlloc = cMaxSummands;

    STAM_LOCK_WR(pUVM);

    int rc = stamR3EnumU(pUVM, pszSummandPattern, false /*fUpdateRing0*/, stamR3RegisterSumEnumCallback, pSum);
    if (RT_SUCCESS(rc))
    {
        if (pSum->cSummands > 0)
            rc = stamR3RegisterU(pUVM, pSum, NULL, NULL, STAMTYPE_INTERNAL_SUM, enmVisibility, szFormattedName,
                                 (STAMUNIT)pSum->enmUnit, pszDesc, STAM_REFRESH_GRP_NONE);
        else
            AssertFailedStmt(rc = VERR_NO_DATA);
    }

    STAM_UNLOCK_WR(pUVM);

    if (RT_FAILURE(rc))
        RTMemFree(pSum);
    return rc;
}


/**
 * Registers a sum that is to be calculated from the @a pszSummandPattern hits.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user mode VM structure.
 * @param   enmVisibility       Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszSummandPattern   A simple pattern for the elements that should be
 *                              summed up.  These must have matching types and
 *                              units.
 * @param   pszDesc             Sample description.
 * @param   pszName             The sample name format string.
 * @param   ...                 Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterSum(PUVM pUVM, STAMVISIBILITY enmVisibility, const char *pszSummandPattern,
                                 const char *pszDesc, const char *pszName, ...)
{
    va_list va;
    va_start(va, pszName);
    int rc = STAMR3RegisterSumV(pUVM, enmVisibility, pszSummandPattern, pszDesc, pszName, va);
    va_end(va);
    return rc;
}


/**
 * Refreshes the cached value (STAMSUMSAMPLE::u) of a percent-of-sum sample.
 */
static void stamR3PctOfSumRefresh(PSTAMDESC pDesc, PSTAMSUMSAMPLE pSum)
{
    /*
     * First the value so we only read it once.
     */
    PSTAMDESC const pValDesc = pSum->apSummands[0];
    uint64_t        uValue;
    switch (pValDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            uValue = pValDesc->u.pCounter->c;
            break;

        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
            uValue = *pValDesc->u.pu64;
            break;

        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
            uValue = *pValDesc->u.pu32;
            break;

        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
            uValue = *pValDesc->u.pu16;
            break;

        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
            uValue = *pValDesc->u.pu8;
            break;

        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
            uValue = pValDesc->u.pProfile->cTicks;
            break;

        case STAMTYPE_INTERNAL_SUM:
        {
            PSTAMSUMSAMPLE const pSubSum = pValDesc->u.pSum;
            stamR3SumRefresh(pSubSum);
            if (pSubSum->enmType == STAMTYPE_COUNTER)
                uValue = pSubSum->u.Counter.c;
            else
                uValue = pSubSum->u.Profile.cTicks;
            break;
        }

        default:
            AssertFailedReturnVoid();
    }

    /*
     * Sum it up with the rest.
     */
    uint64_t  uSum = pSum->fAddValueToSum ? uValue : 0;
    uintptr_t i    = pSum->cSummands;
    while (i-- > 1)
    {
        PSTAMDESC const pSummandDesc = pSum->apSummands[i];
        switch (pSummandDesc->enmType)
        {
            case STAMTYPE_COUNTER:
                uSum += pSummandDesc->u.pCounter->c;
                break;

            case STAMTYPE_U64:
            case STAMTYPE_U64_RESET:
            case STAMTYPE_X64:
            case STAMTYPE_X64_RESET:
                uSum += *pSummandDesc->u.pu64;
                break;

            case STAMTYPE_U32:
            case STAMTYPE_U32_RESET:
            case STAMTYPE_X32:
            case STAMTYPE_X32_RESET:
                uSum += *pSummandDesc->u.pu32;
                break;

            case STAMTYPE_U16:
            case STAMTYPE_U16_RESET:
            case STAMTYPE_X16:
            case STAMTYPE_X16_RESET:
                uSum += *pSummandDesc->u.pu16;
                break;

            case STAMTYPE_U8:
            case STAMTYPE_U8_RESET:
            case STAMTYPE_X8:
            case STAMTYPE_X8_RESET:
                uSum += *pSummandDesc->u.pu8;
                break;

            case STAMTYPE_PROFILE:
            case STAMTYPE_PROFILE_ADV:
                uSum += pSummandDesc->u.pProfile->cTicks;
                break;

            case STAMTYPE_INTERNAL_SUM:
            {
                PSTAMSUMSAMPLE const pSubSum = pSummandDesc->u.pSum;
                stamR3SumRefresh(pSubSum);
                if (pSubSum->enmType == STAMTYPE_COUNTER)
                    uSum += pSubSum->u.Counter.c;
                else
                    uSum += pSubSum->u.Profile.cTicks;
                break;
            }

            default:
                AssertFailedBreak();
        }
    }

    /*
     * Calculate the percentage.
     */
    if (uSum && uValue)
    {
        switch (pDesc->enmUnit)
        {
            case STAMUNIT_PCT:
                pSum->u.Counter.c = uValue * 100 / uSum;
                break;
            case STAMUNIT_PP1K:
                pSum->u.Counter.c = uValue * 1000 / uSum;
                break;
            case STAMUNIT_PP10K:
                pSum->u.Counter.c = uValue * 10000 / uSum;
                break;
            default:
                AssertFailed();
                RT_FALL_THROUGH();
            case STAMUNIT_PPM:
                pSum->u.Counter.c = uValue * 1000000 / uSum;
                break;
            case STAMUNIT_PPB:
                pSum->u.Counter.c = uValue * 1000000000 / uSum;
                break;
        }
    }
    else
        pSum->u.Counter.c = 0;
}


/**
 * Used by STAMR3RegisterPctOfSumV to locate the value to turn into a
 * percentage.
 */
static int stamR3RegisterPctOfSumEnumCallbackForValue(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMSUMSAMPLE const pSum = (PSTAMSUMSAMPLE)pvArg;
    AssertReturn(pSum->cSummands == 0, VERR_TOO_MUCH_DATA);

    /*
     * Check for compatibility.
     */
    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
        case STAMTYPE_INTERNAL_SUM:
            break;

        default:
            AssertMsgFailedReturn(("Pct-of-sum for enmType=%d types have not been implemented yet! Sorry.\n", pDesc->enmType),
                                  VERR_WRONG_TYPE);
    }
    pSum->enmTypeFirst  = pDesc->enmType;
    pSum->apSummands[0] = pDesc;
    pSum->cSummands     = 1;
    return VINF_SUCCESS;
}


/**
 * Used by STAMR3RegisterPctOfSumV to locate the samples to sum up.
 */
static int stamR3RegisterPctOfSumEnumCallbackForSummands(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMSUMSAMPLE const pSum = (PSTAMSUMSAMPLE)pvArg;

    /*
     * Skip if the same as the value we're calculating the percentage for.
     */
    if (pDesc == pSum->apSummands[0])
        return VINF_SUCCESS;

    /*
     * Make sure additional samples are compatible with the first as far as type.
     */
    if (RT_LIKELY(   pDesc->enmType == pSum->enmType
                  || pDesc->enmType == (STAMTYPE)pSum->enmTypeFirst))
    { /* likely */ }
    else
    {
        switch (pDesc->enmType)
        {
            case STAMTYPE_COUNTER:
            case STAMTYPE_U64:
            case STAMTYPE_U64_RESET:
            case STAMTYPE_X64:
            case STAMTYPE_X64_RESET:
            case STAMTYPE_U32:
            case STAMTYPE_U32_RESET:
            case STAMTYPE_X32:
            case STAMTYPE_X32_RESET:
            case STAMTYPE_U16:
            case STAMTYPE_U16_RESET:
            case STAMTYPE_X16:
            case STAMTYPE_X16_RESET:
            case STAMTYPE_U8:
            case STAMTYPE_U8_RESET:
            case STAMTYPE_X8:
            case STAMTYPE_X8_RESET:
            case STAMTYPE_PROFILE:
            case STAMTYPE_PROFILE_ADV:
            case STAMTYPE_INTERNAL_SUM:
                break;

            default:
                AssertMsgFailedReturn(("Unsupported pct-of-sum type: %d (%s)\n", pDesc->enmType, pDesc->pszName), VERR_MISMATCH);
        }
    }

    AssertReturn(pSum->cSummands < pSum->cSummandsAlloc, VERR_TOO_MUCH_DATA);
    pSum->apSummands[pSum->cSummands++] = pDesc;
    return VINF_SUCCESS;
}


/**
 * Registers a percentage of a sum that is to be calculated from @a pszValue and
 * the @a pszSummandPattern hits.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user mode VM structure.
 * @param   enmVisibility       Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit             The sample unit: STAMUNIT_PCT, STAMUNIT_PP1K,
 *                              STAMUNIT_PP10K, STAMUNIT_PPM or STAMUNIT_PPB.
 * @param   pszValue            Name of the sample which value should be put
 *                              against the sum of all.
 * @param   pszSummandPattern   A simple pattern for the elements that should be
 *                              summed up and used to divide @a pszName by when
 *                              calculating the percentage.  These must have
 *                              compatible types.
 * @param   fAddValueToSum      Whether to add @a pszValue to the values that
 *                              @a pszSummandPattern specifies (@c true) or not
 *                              (@c false).
 * @param   pszDesc             Sample description.
 * @param   pszName             The sample name format string.
 * @param   va                  Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterPctOfSumV(PUVM pUVM, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszValue,
                                       bool fAddValueToSum, const char *pszSummandPattern, const char *pszDesc,
                                       const char *pszName, va_list va)
{
    char   szFormattedName[STAM_MAX_NAME_LEN + 8];
    size_t cch = RTStrPrintfV(szFormattedName, sizeof(szFormattedName), pszName, va);
    AssertReturn(cch <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);
    switch (enmUnit)
    {
            case STAMUNIT_PCT:
            case STAMUNIT_PP1K:
            case STAMUNIT_PP10K:
            case STAMUNIT_PPM:
            case STAMUNIT_PPB:
                break;
            default:
                AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * We have to resolve the value and summands before we continue with the
     * actual registration.  We reuse the STAMSUMSAMPLE structure here.
     */
    uint8_t const        cMaxSummands = 32;
    PSTAMSUMSAMPLE const pSum = (PSTAMSUMSAMPLE)RTMemAllocZ(RT_UOFFSETOF_DYN(STAMSUMSAMPLE, apSummands[cMaxSummands]));
    AssertReturn(pSum, VERR_NO_MEMORY);
    pSum->cSummandsAlloc = cMaxSummands;
    pSum->enmType        = STAMTYPE_COUNTER;
    pSum->enmUnit        = enmUnit;
    pSum->fAddValueToSum = fAddValueToSum;

    STAM_LOCK_WR(pUVM);

    /* The first summand entry is the value. */
    int rc = stamR3EnumU(pUVM, pszValue, false /*fUpdateRing0*/, stamR3RegisterPctOfSumEnumCallbackForValue, pSum);
    if (RT_SUCCESS(rc))
    {
        if (pSum->cSummands == 1)
        {
            /* The additional ones are part of the sum we should divide the value by. */
            rc = stamR3EnumU(pUVM, pszSummandPattern, false /*fUpdateRing0*/, stamR3RegisterPctOfSumEnumCallbackForSummands, pSum);
            if (RT_SUCCESS(rc))
            {
                /* Now, register it. */
                if (pSum->cSummands > 1)
                    rc = stamR3RegisterU(pUVM, pSum, NULL, NULL, STAMTYPE_INTERNAL_PCT_OF_SUM, enmVisibility, szFormattedName,
                                         (STAMUNIT)pSum->enmUnit, pszDesc, STAM_REFRESH_GRP_NONE);
                else
                    AssertFailedStmt(rc = VERR_NO_DATA);
            }
        }
        else
            AssertFailedStmt(rc = VERR_NO_DATA);
    }

    STAM_UNLOCK_WR(pUVM);

    if (RT_FAILURE(rc))
        RTMemFree(pSum);
    return rc;
}


/**
 * Registers a percentage of a sum that is to be calculated from @a pszValue and
 * the @a pszSummandPattern hits.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user mode VM structure.
 * @param   enmVisibility       Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit             The sample unit: STAMUNIT_PCT, STAMUNIT_PP1K,
 *                              STAMUNIT_PP10K, STAMUNIT_PPM or STAMUNIT_PPB.
 * @param   pszValue            Name of the sample which value should be put
 *                              against the sum of all.
 * @param   pszSummandPattern   A simple pattern for the elements that should be
 *                              summed up and used to divide @a pszName by when
 *                              calculating the percentage.  These must have
 *                              compatible types.
 * @param   fAddValueToSum      Whether to add @a pszValue to the values that
 *                              @a pszSummandPattern specifies (@c true) or not
 *                              (@c false).
 * @param   pszDesc             Sample description.
 * @param   pszName             The sample name format string.
 * @param   ...                 Arguments to the format string.
 */
VMMR3DECL(int) STAMR3RegisterPctOfSum(PUVM pUVM, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszValue,
                                      bool fAddValueToSum, const char *pszSummandPattern, const char *pszDesc,
                                      const char *pszName, ...)
{
    va_list va;
    va_start(va, pszName);
    int rc = STAMR3RegisterPctOfSumV(pUVM, enmVisibility, enmUnit, pszValue, fAddValueToSum, pszSummandPattern,
                                     pszDesc, pszName, va);
    va_end(va);
    return rc;
}


#ifdef VBOX_STRICT
/**
 * Divide the strings into sub-strings using '/' as delimiter
 * and then compare them in strcmp fashion.
 *
 * @returns Difference.
 * @retval  0 if equal.
 * @retval  < 0 if psz1 is less than psz2.
 * @retval  > 0 if psz1 greater than psz2.
 *
 * @param   psz1        The first string.
 * @param   psz2        The second string.
 */
static int stamR3SlashCompare(const char *psz1, const char *psz2)
{
    for (;;)
    {
        unsigned int ch1 = *psz1++;
        unsigned int ch2 = *psz2++;
        if (ch1 != ch2)
        {
            /* slash is end-of-sub-string, so it trumps everything but '\0'. */
            if (ch1 == '/')
                return ch2 ? -1 : 1;
            if (ch2 == '/')
                return ch1 ? 1 : -1;
            return ch1 - ch2;
        }

        /* done? */
        if (ch1 == '\0')
            return 0;
    }
}
#endif /* VBOX_STRICT */


/**
 * Compares a lookup node with a name.
 *
 * @returns like strcmp and memcmp.
 * @param   pNode               The lookup node.
 * @param   pchName             The name, not necessarily terminated.
 * @param   cchName             The length of the name.
 */
DECL_FORCE_INLINE(int) stamR3LookupCmp(PSTAMLOOKUP pNode, const char *pchName, uint32_t cchName)
{
    uint32_t cchComp = RT_MIN(pNode->cch, cchName);
    int iDiff = memcmp(pNode->szName, pchName, cchComp);
    if (!iDiff && pNode->cch != cchName)
        iDiff = pNode->cch > cchName ? 2 : -2;
    return iDiff;
}


/**
 * Creates a new lookup child node.
 *
 * @returns Pointer to the newly created lookup node.
 * @param   pParent             The parent node.
 * @param   pchName             The name (not necessarily terminated).
 * @param   cchName             The length of the name.
 * @param   offName             The offset of the node in a path.
 * @param   iChild              Child index of a node that's before the one
 *                              we're inserting (returned by
 *                              stamR3LookupFindChild).
 */
static PSTAMLOOKUP stamR3LookupNewChild(PSTAMLOOKUP pParent, const char *pchName, uint32_t cchName, uint32_t offName,
                                        uint32_t iChild)
{
    Assert(cchName <= UINT8_MAX);
    Assert(offName <= UINT8_MAX);
    Assert(iChild  <  UINT16_MAX);

    /*
     * Allocate a new entry.
     */
    PSTAMLOOKUP pNew = (PSTAMLOOKUP)RTMemAlloc(RT_UOFFSETOF_DYN(STAMLOOKUP, szName[cchName + 1]));
    if (!pNew)
        return NULL;
    pNew->pParent       = pParent;
    pNew->papChildren    = NULL;
    pNew->pDesc         = NULL;
    pNew->cDescsInTree  = 0;
    pNew->cChildren     = 0;
    pNew->cch           = (uint16_t)cchName;
    pNew->off           = (uint16_t)offName;
    memcpy(pNew->szName, pchName, cchName);
    pNew->szName[cchName] = '\0';

    /*
     * Reallocate the array?
     */
    if (RT_IS_POWER_OF_TWO(pParent->cChildren))
    {
        uint32_t cNew = pParent->cChildren ? (uint32_t)pParent->cChildren * 2 : 8;
        AssertReturnStmt(cNew <= 0x8000, RTMemFree(pNew), NULL);
        void *pvNew = RTMemRealloc(pParent->papChildren, cNew * sizeof(pParent->papChildren[0]));
        if (!pvNew)
        {
            RTMemFree(pNew);
            return NULL;
        }
        pParent->papChildren = (PSTAMLOOKUP *)pvNew;
    }

    /*
     * Find the exact insertion point using iChild as a very good clue from
     * the find function.
     */
    if (!pParent->cChildren)
        iChild = 0;
    else
    {
        if (iChild >= pParent->cChildren)
            iChild = pParent->cChildren - 1;
        while (   iChild < pParent->cChildren
               && stamR3LookupCmp(pParent->papChildren[iChild], pchName, cchName) < 0)
            iChild++;
    }

    /*
     * Insert it.
     */
    if (iChild < pParent->cChildren)
    {
        /* Do shift. */
        uint32_t i = pParent->cChildren;
        while (i > iChild)
        {
            PSTAMLOOKUP pNode = pParent->papChildren[i - 1];
            pParent->papChildren[i] = pNode;
            pNode->iParent = i;
            i--;
        }
    }

    pNew->iParent = iChild;
    pParent->papChildren[iChild] = pNew;
    pParent->cChildren++;

    return pNew;
}


/**
 * Looks up a child.
 *
 * @returns Pointer to child node if found, NULL if not.
 * @param   pParent             The parent node.
 * @param   pchName             The name (not necessarily terminated).
 * @param   cchName             The length of the name.
 * @param   piChild             Where to store a child index suitable for
 *                              passing to stamR3LookupNewChild when NULL is
 *                              returned.
 */
static PSTAMLOOKUP stamR3LookupFindChild(PSTAMLOOKUP pParent, const char *pchName, uint32_t cchName, uint32_t *piChild)
{
    uint32_t iChild = pParent->cChildren;
    if (iChild > 4)
    {
        uint32_t iFirst = 0;
        uint32_t iEnd   = iChild;
        iChild /= 2;
        for (;;)
        {
            int iDiff = stamR3LookupCmp(pParent->papChildren[iChild], pchName, cchName);
            if (!iDiff)
            {
                if (piChild)
                    *piChild = iChild;
                return pParent->papChildren[iChild];
            }

            /* Split. */
            if (iDiff < 0)
            {
                iFirst = iChild + 1;
                if (iFirst >= iEnd)
                {
                    if (piChild)
                        *piChild = iChild;
                    break;
                }
            }
            else
            {
                if (iChild == iFirst)
                {
                    if (piChild)
                        *piChild = iChild ? iChild - 1 : 0;
                    break;
                }
                iEnd = iChild;
            }

            /* Calc next child. */
            iChild = (iEnd - iFirst) / 2 + iFirst;
        }
        return NULL;
    }

    /*
     * Linear search.
     */
    while (iChild-- > 0)
    {
        int iDiff = stamR3LookupCmp(pParent->papChildren[iChild], pchName, cchName);
        if (iDiff <= 0)
        {
            if (piChild)
                *piChild = iChild;
            return !iDiff ? pParent->papChildren[iChild] : NULL;
        }
    }
    if (piChild)
        *piChild = 0;
    return NULL;
}


/**
 * Find the next sample descriptor node.
 *
 * This is for use with insertion in the big list and pattern range lookups.
 *
 * @returns Pointer to the next sample descriptor. NULL if not found (i.e.
 *          we're at the end of the list).
 * @param   pLookup             The current node.
 */
static PSTAMDESC stamR3LookupFindNextWithDesc(PSTAMLOOKUP pLookup)
{
    Assert(!pLookup->pDesc);
    PSTAMLOOKUP pCur = pLookup;
    uint32_t    iCur = 0;
    for (;;)
    {
        /*
         * Check all children.
         */
        uint32_t cChildren = pCur->cChildren;
        if (iCur < cChildren)
        {
            PSTAMLOOKUP *papChildren = pCur->papChildren;
            do
            {
                PSTAMLOOKUP pChild = papChildren[iCur];
                if (pChild->pDesc)
                    return pChild->pDesc;

                if (pChild->cChildren > 0)
                {
                    /* One level down. */
                    iCur = 0;
                    pCur = pChild;
                    break;
                }
            } while (++iCur < cChildren);
        }
        else
        {
            /*
             * One level up, resuming after the current.
             */
            iCur = pCur->iParent + 1;
            pCur = pCur->pParent;
            if (!pCur)
                return NULL;
        }
    }
}


/**
 * Look up a sample descriptor by name.
 *
 * @returns Pointer to a sample descriptor.
 * @param   pRoot               The root node.
 * @param   pszName             The name to lookup.
 */
static PSTAMDESC stamR3LookupFindDesc(PSTAMLOOKUP pRoot, const char *pszName)
{
    Assert(!pRoot->pParent);
    while (*pszName++ == '/')
    {
        const char *pszEnd = strchr(pszName, '/');
        uint32_t    cch    = pszEnd ? pszEnd - pszName : (uint32_t)strlen(pszName);
        PSTAMLOOKUP pChild = stamR3LookupFindChild(pRoot, pszName, cch, NULL);
        if (!pChild)
            break;
        if (!pszEnd)
            return pChild->pDesc;
        pszName = pszEnd;
        pRoot   = pChild;
    }

    return NULL;
}


/**
 * Finds the first sample descriptor for a given lookup range.
 *
 * This is for pattern range lookups.
 *
 * @returns Pointer to the first descriptor.
 * @param   pFirst              The first node in the range.
 * @param   pLast               The last node in the range.
 */
static PSTAMDESC stamR3LookupFindFirstDescForRange(PSTAMLOOKUP pFirst, PSTAMLOOKUP pLast)
{
    if (pFirst->pDesc)
        return pFirst->pDesc;

    PSTAMLOOKUP pCur = pFirst;
    uint32_t    iCur = 0;
    for (;;)
    {
        uint32_t cChildren = pCur->cChildren;
        if (iCur < pCur->cChildren)
        {
            /*
             * Check all children.
             */
            PSTAMLOOKUP * const papChildren = pCur->papChildren;
            do
            {
                PSTAMLOOKUP pChild = papChildren[iCur];
                if (pChild->pDesc)
                    return pChild->pDesc;
                if (pChild->cChildren > 0)
                {
                    /* One level down. */
                    iCur = 0;
                    pCur = pChild;
                    break;
                }
                if (pChild == pLast)
                    return NULL;
            } while (++iCur < cChildren);
        }
        else
        {
            /*
             * One level up, checking current and its 'older' sibilings.
             */
            if (pCur == pLast)
                return NULL;
            iCur = pCur->iParent + 1;
            pCur = pCur->pParent;
            if (!pCur)
                break;
        }
    }

    return NULL;
}


/**
 * Finds the last sample descriptor for a given lookup range.
 *
 * This is for pattern range lookups.
 *
 * @returns Pointer to the first descriptor.
 * @param   pFirst              The first node in the range.
 * @param   pLast               The last node in the range.
 */
static PSTAMDESC stamR3LookupFindLastDescForRange(PSTAMLOOKUP pFirst, PSTAMLOOKUP pLast)
{
    PSTAMLOOKUP pCur = pLast;
    uint32_t    iCur = pCur->cChildren - 1;
    for (;;)
    {
        if (iCur < pCur->cChildren)
        {
            /*
             * Check children backwards, depth first.
             */
            PSTAMLOOKUP * const papChildren = pCur->papChildren;
            do
            {
                PSTAMLOOKUP pChild = papChildren[iCur];
                if (pChild->cChildren > 0)
                {
                    /* One level down. */
                    iCur = pChild->cChildren - 1;
                    pCur = pChild;
                    break;
                }

                if (pChild->pDesc)
                    return pChild->pDesc;
                if (pChild == pFirst)
                    return NULL;
            } while (iCur-- > 0); /* (underflow handled above) */
        }
        else
        {
            /*
             * One level up, checking current and its 'older' sibilings.
             */
            if (pCur->pDesc)
                return pCur->pDesc;
            if (pCur == pFirst)
                return NULL;
            iCur = pCur->iParent - 1; /* (underflow handled above) */
            pCur = pCur->pParent;
            if (!pCur)
                break;
        }
    }

    return NULL;
}


/**
 * Look up the first and last descriptors for a (single) pattern expression.
 *
 * This is used to optimize pattern enumerations and doesn't have to return 100%
 * accurate results if that costs too much.
 *
 * @returns Pointer to the first descriptor in the range.
 * @param   pRoot               The root node.
 * @param   pList               The descriptor list anchor.
 * @param   pszPat              The name patter to lookup.
 * @param   ppLastDesc          Where to store the address of the last
 *                              descriptor (approximate).
 */
static PSTAMDESC stamR3LookupFindPatternDescRange(PSTAMLOOKUP pRoot, PRTLISTANCHOR pList, const char *pszPat,
                                                  PSTAMDESC *ppLastDesc)
{
    Assert(!pRoot->pParent);

    /*
     * If there is an early enough wildcard, the whole list needs to be searched.
     */
    if (   pszPat[0] == '*' || pszPat[0] == '?'
        || pszPat[1] == '*' || pszPat[1] == '?')
    {
        *ppLastDesc = RTListGetLast(pList, STAMDESC, ListEntry);
        return RTListGetFirst(pList, STAMDESC, ListEntry);
    }

    /*
     * All statistics starts with a slash.
     */
    while (   *pszPat++ == '/'
           && pRoot->cDescsInTree > 0
           && pRoot->cChildren    > 0)
    {
        const char *pszEnd = strchr(pszPat, '/');
        uint32_t    cch    = pszEnd ? pszEnd - pszPat : (uint32_t)strlen(pszPat);
        if (!cch)
            break;

        const char *pszPat1 = (const char *)memchr(pszPat, '*', cch);
        const char *pszPat2 = (const char *)memchr(pszPat, '?', cch);
        if (pszPat1 || pszPat2)
        {
            /* We've narrowed it down to a sub-tree now. */
            PSTAMLOOKUP pFirst = pRoot->papChildren[0];
            PSTAMLOOKUP pLast  = pRoot->papChildren[pRoot->cChildren - 1];
            /** @todo narrow the range further if both pszPat1/2 != pszPat. */

            *ppLastDesc = stamR3LookupFindLastDescForRange(pFirst, pLast);
            return stamR3LookupFindFirstDescForRange(pFirst, pLast);
        }

        PSTAMLOOKUP pChild = stamR3LookupFindChild(pRoot, pszPat, cch, NULL);
        if (!pChild)
            break;

        /* Advance */
        if (!pszEnd)
            return *ppLastDesc = pChild->pDesc;
        pszPat = pszEnd;
        pRoot  = pChild;
    }

    /* No match. */
    *ppLastDesc = NULL;
    return NULL;
}


/**
 * Look up the first descriptors for starts-with name string.
 *
 * This is used to optimize deletion.
 *
 * @returns Pointer to the first descriptor in the range.
 * @param   pRoot       The root node.
 * @param   pchPrefix   The name prefix.
 * @param   cchPrefix   The name prefix length (can be shorter than the
 *                      actual string).
 * @param   ppLastDesc  Where to store the address of the last descriptor.
 * @sa      stamR3LookupFindPatternDescRange
 */
static PSTAMDESC stamR3LookupFindByPrefixRange(PSTAMLOOKUP pRoot, const char *pchPrefix, uint32_t cchPrefix,
                                               PSTAMDESC *ppLastDesc)

{
    *ppLastDesc = NULL;
    Assert(!pRoot->pParent);
    AssertReturn(cchPrefix > 0, NULL);

    /*
     * We start with a root slash.
     */
    if (!cchPrefix || *pchPrefix != '/')
        return NULL;

    /*
     * Walk thru the prefix component by component, since that's how
     * the lookup tree is organized.
     */
    while (   cchPrefix
           && *pchPrefix == '/'
           && pRoot->cDescsInTree > 0
           && pRoot->cChildren    > 0)
    {
        cchPrefix -= 1;
        pchPrefix += 1;

        const char *pszEnd = (const char *)memchr(pchPrefix, '/', cchPrefix);
        if (!pszEnd)
        {
            /*
             * We've narrowed it down to a sub-tree now.  If we've no more prefix to work
             * with now (e.g. '/Devices/'), the prefix matches all the children.  Otherwise,
             * traverse the children to find the ones matching the prefix.
             */
            if (!cchPrefix)
            {
                *ppLastDesc = stamR3LookupFindLastDescForRange(pRoot->papChildren[0], pRoot->papChildren[pRoot->cChildren - 1]);
                return stamR3LookupFindFirstDescForRange(pRoot->papChildren[0], pRoot->papChildren[pRoot->cChildren - 1]);
            }

            size_t iEnd = pRoot->cChildren;
            if (iEnd < 16)
            {
                /* Linear scan of the children: */
                for (size_t i = 0; i < pRoot->cChildren; i++)
                {
                    PSTAMLOOKUP pCur = pRoot->papChildren[i];
                    if (pCur->cch >= cchPrefix)
                    {
                        int iDiff = memcmp(pCur->szName, pchPrefix, cchPrefix);
                        if (iDiff == 0)
                        {
                            size_t iLast = i;
                            while (++iLast < pRoot->cChildren)
                            {
                                PSTAMLOOKUP pCur2 = pRoot->papChildren[iLast];
                                if (   pCur2->cch < cchPrefix
                                    || memcmp(pCur2->szName, pchPrefix, cchPrefix) != 0)
                                    break;
                            }
                            iLast--;

                            *ppLastDesc = stamR3LookupFindLastDescForRange(pCur, pRoot->papChildren[iLast]);
                            return stamR3LookupFindFirstDescForRange(pCur, pRoot->papChildren[iLast]);
                        }
                        if (iDiff > 0)
                            break;
                    }
                }
            }
            else
            {
                /* Binary search to find something matching the prefix, followed
                   by a reverse scan to locate the first child: */
                size_t iFirst = 0;
                size_t i      = iEnd / 2;
                for (;;)
                {
                    PSTAMLOOKUP pCur = pRoot->papChildren[i];
                    int iDiff;
                    if (pCur->cch >= cchPrefix)
                        iDiff = memcmp(pCur->szName, pchPrefix, cchPrefix);
                    else
                    {
                        iDiff = memcmp(pCur->szName, pchPrefix, pCur->cch);
                        if (!iDiff)
                            iDiff = -1;
                    }
                    if (iDiff > 0)
                    {
                        if (iFirst < i)
                            iEnd = i;
                        else
                            return NULL;
                    }
                    else if (iDiff < 0)
                    {
                        i += 1;
                        if (i < iEnd)
                            iFirst = i;
                        else
                            return NULL;
                    }
                    else
                    {
                        /* Match.  Reverse scan to find the first. */
                        iFirst = i;
                        while (   iFirst > 0
                               && (pCur = pRoot->papChildren[iFirst - 1])->cch >= cchPrefix
                               && memcmp(pCur->szName, pchPrefix, cchPrefix) == 0)
                            iFirst--;

                        /* Forward scan to find the last.*/
                        size_t iLast = i;
                        while (++iLast < pRoot->cChildren)
                        {
                            pCur = pRoot->papChildren[iLast];
                            if (   pCur->cch < cchPrefix
                                || memcmp(pCur->szName, pchPrefix, cchPrefix) != 0)
                                break;
                        }
                        iLast--;

                        *ppLastDesc = stamR3LookupFindLastDescForRange(pRoot->papChildren[iFirst], pRoot->papChildren[iLast]);
                        return stamR3LookupFindFirstDescForRange(pRoot->papChildren[iFirst], pRoot->papChildren[iLast]);
                    }

                    i = iFirst + (iEnd - iFirst) / 2;
                }
            }
            break;
        }

        /* Find child matching the path component:  */
        uint32_t    cchChild = pszEnd - pchPrefix;
        PSTAMLOOKUP pChild   = stamR3LookupFindChild(pRoot, pchPrefix, cchChild, NULL);
        if (!pChild)
            break;

        /* Advance: */
        cchPrefix -= cchChild;
        pchPrefix  = pszEnd;
        pRoot      = pChild;
    }
    return NULL;
}


/**
 * Increments the cDescInTree member of the given node an all ancestors.
 *
 * @param   pLookup             The lookup node.
 */
static void stamR3LookupIncUsage(PSTAMLOOKUP pLookup)
{
    Assert(pLookup->pDesc);

    PSTAMLOOKUP pCur = pLookup;
    while (pCur != NULL)
    {
        pCur->cDescsInTree++;
        pCur = pCur->pParent;
    }
}


/**
 * Descrements the cDescInTree member of the given node an all ancestors.
 *
 * @param   pLookup             The lookup node.
 */
static void stamR3LookupDecUsage(PSTAMLOOKUP pLookup)
{
    Assert(!pLookup->pDesc);

    PSTAMLOOKUP pCur = pLookup;
    while (pCur != NULL)
    {
        Assert(pCur->cDescsInTree > 0);
        pCur->cDescsInTree--;
        pCur = pCur->pParent;
    }
}


/**
 * Frees empty lookup nodes if it's worth it.
 *
 * @param   pLookup             The lookup node.
 */
static void stamR3LookupMaybeFree(PSTAMLOOKUP pLookup)
{
    Assert(!pLookup->pDesc);

    /*
     * Free between two and three levels of nodes.  Freeing too much most
     * likely wasted effort since we're either going to repopluate the tree
     * or quit the whole thing.
     */
    if (pLookup->cDescsInTree > 0)
        return;

    PSTAMLOOKUP pCur = pLookup->pParent;
    if (!pCur)
        return;
    if (pCur->cDescsInTree > 0)
        return;
    PSTAMLOOKUP pParent = pCur->pParent;
    if (!pParent)
        return;

    if (pParent->cDescsInTree == 0 && pParent->pParent)
    {
        pCur = pParent;
        pParent = pCur->pParent;
    }

    /*
     * Remove pCur from pParent.
     */
    PSTAMLOOKUP *papChildren = pParent->papChildren;
    uint32_t     cChildren   = --pParent->cChildren;
    for (uint32_t i = pCur->iParent; i < cChildren; i++)
    {
        PSTAMLOOKUP pChild = papChildren[i + 1];
        pChild->iParent = i;
        papChildren[i] = pChild;
    }
    pCur->pParent = NULL;
    pCur->iParent = UINT16_MAX;

    /*
     * Destroy pCur.
     */
    stamR3LookupDestroyTree(pCur);
}


/**
 * Destroys a lookup tree.
 *
 * This is used by STAMR3Term as well as stamR3LookupMaybeFree.
 *
 * @param   pRoot               The root of the tree (must have no parent).
 */
static void stamR3LookupDestroyTree(PSTAMLOOKUP pRoot)
{
    Assert(pRoot); Assert(!pRoot->pParent);
    PSTAMLOOKUP pCur = pRoot;
    for (;;)
    {
        uint32_t i = pCur->cChildren;
        if (i > 0)
        {
            /*
             * Push child (with leaf optimization).
             */
            PSTAMLOOKUP pChild = pCur->papChildren[--i];
            if (pChild->cChildren != 0)
                pCur = pChild;
            else
            {
                /* free leaves. */
                for (;;)
                {
                    if (pChild->papChildren)
                    {
                        RTMemFree(pChild->papChildren);
                        pChild->papChildren = NULL;
                    }
                    RTMemFree(pChild);
                    pCur->papChildren[i] = NULL;

                    /* next */
                    if (i == 0)
                    {
                        pCur->cChildren = 0;
                        break;
                    }
                    pChild = pCur->papChildren[--i];
                    if (pChild->cChildren != 0)
                    {
                        pCur->cChildren = i + 1;
                        pCur = pChild;
                        break;
                    }
                }
            }
        }
        else
        {
            /*
             * Pop and free current.
             */
            Assert(!pCur->pDesc);

            PSTAMLOOKUP pParent = pCur->pParent;
            Assert(pCur->iParent == (pParent ? pParent->cChildren - 1 : UINT16_MAX));

            RTMemFree(pCur->papChildren);
            pCur->papChildren = NULL;
            RTMemFree(pCur);

            pCur = pParent;
            if (!pCur)
                break;
            pCur->papChildren[--pCur->cChildren] = NULL;
        }
    }
}


/**
 * Internal worker for the different register calls.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pvSample    Pointer to the sample.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszName     The sample name format string.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   iRefreshGrp The refresh group, STAM_REFRESH_GRP_XXX.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
static int stamR3RegisterU(PUVM pUVM, void *pvSample, PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                           STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                           const char *pszName, STAMUNIT enmUnit, const char *pszDesc, uint8_t iRefreshGrp)
{
    AssertReturn(pszName[0] == '/', VERR_INVALID_NAME);
    AssertReturn(pszName[1] != '/' && pszName[1], VERR_INVALID_NAME);
    uint32_t const cchName = (uint32_t)strlen(pszName);
    AssertReturn(cchName <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);
    AssertReturn(pszName[cchName - 1] != '/', VERR_INVALID_NAME);
    AssertReturn(memchr(pszName, '\\', cchName) == NULL, VERR_INVALID_NAME);
    AssertReturn(iRefreshGrp == STAM_REFRESH_GRP_NONE || iRefreshGrp < 64, VERR_INVALID_PARAMETER);

    STAM_LOCK_WR(pUVM);

    /*
     * Look up the tree location, populating the lookup tree as we walk it.
     */
    PSTAMLOOKUP pLookup = pUVM->stam.s.pRoot; Assert(pLookup);
    uint32_t    offName = 1;
    for (;;)
    {
        /* Get the next part of the path. */
        const char *pszStart = &pszName[offName];
        const char *pszEnd   = strchr(pszStart, '/');
        uint32_t    cch      = pszEnd ? (uint32_t)(pszEnd - pszStart) : cchName - offName;
        if (cch == 0)
        {
            STAM_UNLOCK_WR(pUVM);
            AssertMsgFailed(("No double or trailing slashes are allowed: '%s'\n", pszName));
            return VERR_INVALID_NAME;
        }

        /* Do the looking up. */
        uint32_t    iChild = 0;
        PSTAMLOOKUP pChild = stamR3LookupFindChild(pLookup, pszStart, cch, &iChild);
        if (!pChild)
        {
            pChild = stamR3LookupNewChild(pLookup, pszStart, cch, offName, iChild);
            if (!pChild)
            {
                STAM_UNLOCK_WR(pUVM);
                return VERR_NO_MEMORY;
            }
        }

        /* Advance. */
        pLookup = pChild;
        if (!pszEnd)
            break;
        offName += cch + 1;
    }
    if (pLookup->pDesc)
    {
        STAM_UNLOCK_WR(pUVM);
        AssertMsgFailed(("Duplicate sample name: %s\n", pszName));
        return VERR_ALREADY_EXISTS;
    }

    PSTAMDESC pCur = stamR3LookupFindNextWithDesc(pLookup);

    /*
     * Check that the name doesn't screw up sorting order when taking
     * slashes into account. The QT GUI makes some assumptions.
     * Problematic chars are: !"#$%&'()*+,-.
     */
#ifdef VBOX_STRICT
    Assert(pszName[0] == '/');
    PSTAMDESC pPrev = pCur
                    ? RTListGetPrev(&pUVM->stam.s.List, pCur, STAMDESC, ListEntry)
                    : RTListGetLast(&pUVM->stam.s.List, STAMDESC, ListEntry);
    Assert(!pPrev || strcmp(pszName, pPrev->pszName) > 0);
    Assert(!pCur  || strcmp(pszName, pCur->pszName)  < 0);
    Assert(!pPrev || stamR3SlashCompare(pPrev->pszName, pszName) < 0);
    Assert(!pCur  || stamR3SlashCompare(pCur->pszName, pszName) > 0);

    /*
     * Check alignment requirements.
     */
    switch (enmType)
    {
        /* 8 byte / 64-bit */
        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
        case STAMTYPE_COUNTER:
        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
            AssertMsg(!((uintptr_t)pvSample & 7), ("%p - %s\n", pvSample, pszName));
            break;

        /* 4 byte / 32-bit */
        case STAMTYPE_RATIO_U32:
        case STAMTYPE_RATIO_U32_RESET:
        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
            AssertMsg(!((uintptr_t)pvSample & 3), ("%p - %s\n", pvSample, pszName));
            break;

        /* 2 byte / 32-bit */
        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
            AssertMsg(!((uintptr_t)pvSample & 1), ("%p - %s\n", pvSample, pszName));
            break;

        /* 1 byte / 8-bit / unaligned */
        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
        case STAMTYPE_BOOL:
        case STAMTYPE_BOOL_RESET:
        case STAMTYPE_CALLBACK:
        case STAMTYPE_INTERNAL_SUM:
        case STAMTYPE_INTERNAL_PCT_OF_SUM:
            break;

        default:
            AssertMsgFailed(("%d\n", enmType));
            break;
    }
#endif /* VBOX_STRICT */

    /*
     * Create a new node and insert it at the current location.
     */
    int rc;
    size_t cbDesc = pszDesc ? strlen(pszDesc) + 1 : 0;
    PSTAMDESC pNew = (PSTAMDESC)RTMemAlloc(sizeof(*pNew) + cchName + 1 + cbDesc);
    if (pNew)
    {
        pNew->pszName       = (char *)memcpy((char *)(pNew + 1), pszName, cchName + 1);
        pNew->enmType       = enmType;
        pNew->enmVisibility = enmVisibility;
        if (enmType != STAMTYPE_CALLBACK)
            pNew->u.pv      = pvSample;
        else
        {
            pNew->u.Callback.pvSample = pvSample;
            pNew->u.Callback.pfnReset = pfnReset;
            pNew->u.Callback.pfnPrint = pfnPrint;
        }
        pNew->enmUnit       = enmUnit;
        pNew->iRefreshGroup = iRefreshGrp;
        pNew->pszDesc       = NULL;
        if (pszDesc)
            pNew->pszDesc   = (char *)memcpy((char *)(pNew + 1) + cchName + 1, pszDesc, cbDesc);

        if (pCur)
            RTListNodeInsertBefore(&pCur->ListEntry, &pNew->ListEntry);
        else
            RTListAppend(&pUVM->stam.s.List, &pNew->ListEntry);

        pNew->pLookup       = pLookup;
        pLookup->pDesc      = pNew;
        stamR3LookupIncUsage(pLookup);

        stamR3ResetOne(pNew, pUVM->pVM);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    STAM_UNLOCK_WR(pUVM);
    return rc;
}


/**
 * Destroys the statistics descriptor, unlinking it and freeing all resources.
 *
 * @returns VINF_SUCCESS
 * @param   pCur        The descriptor to destroy.
 */
static int stamR3DestroyDesc(PSTAMDESC pCur)
{
    RTListNodeRemove(&pCur->ListEntry);
    pCur->pLookup->pDesc = NULL; /** @todo free lookup nodes once it's working. */
    stamR3LookupDecUsage(pCur->pLookup);
    stamR3LookupMaybeFree(pCur->pLookup);
    if (   pCur->enmType != STAMTYPE_INTERNAL_SUM
        && pCur->enmType != STAMTYPE_INTERNAL_PCT_OF_SUM)
    { /* likely */ }
    else
        RTMemFree(pCur->u.pSum);
    RTMemFree(pCur);

    return VINF_SUCCESS;
}


/**
 * Deregisters a sample previously registered by STAR3Register() given its
 * address.
 *
 * This is intended used for devices which can be unplugged and for
 * temporary samples.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pvSample    Pointer to the sample registered with STAMR3Register().
 */
VMMR3DECL(int)  STAMR3DeregisterByAddr(PUVM pUVM, void *pvSample)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* This is a complete waste of time when shutting down. */
    VMSTATE enmState = VMR3GetStateU(pUVM);
    if (enmState >= VMSTATE_DESTROYING)
        return VINF_SUCCESS;

    STAM_LOCK_WR(pUVM);

    /*
     * Search for it.
     */
    int         rc = VERR_INVALID_HANDLE;
    PSTAMDESC   pCur, pNext;
    RTListForEachSafe(&pUVM->stam.s.List, pCur, pNext, STAMDESC, ListEntry)
    {
        if (pCur->u.pv == pvSample)
            rc = stamR3DestroyDesc(pCur);
    }

    STAM_UNLOCK_WR(pUVM);
    return rc;
}


/**
 * Worker for STAMR3Deregister, STAMR3DeregisterV and STAMR3DeregisterF.
 *
 * @returns VBox status code.
 * @retval  VWRN_NOT_FOUND if no matching names found.
 *
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pszPat      The name pattern.
 */
static int stamR3DeregisterByPattern(PUVM pUVM, const char *pszPat)
{
    Assert(!strchr(pszPat, '|')); /* single pattern! */

    int rc = VWRN_NOT_FOUND;
    STAM_LOCK_WR(pUVM);

    PSTAMDESC pLast;
    PSTAMDESC pCur = stamR3LookupFindPatternDescRange(pUVM->stam.s.pRoot, &pUVM->stam.s.List, pszPat, &pLast);
    if (pCur)
    {
        for (;;)
        {
            PSTAMDESC pNext = RTListNodeGetNext(&pCur->ListEntry, STAMDESC, ListEntry);

            if (RTStrSimplePatternMatch(pszPat, pCur->pszName))
                rc = stamR3DestroyDesc(pCur);

            /* advance. */
            if (pCur == pLast)
                break;
            pCur = pNext;
        }
        Assert(pLast);
    }
    else
        Assert(!pLast);

    STAM_UNLOCK_WR(pUVM);
    return rc;
}


/**
 * Deregister zero or more samples given a (single) pattern matching their
 * names.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pszPat      The name pattern.
 * @sa      STAMR3DeregisterF, STAMR3DeregisterV
 */
VMMR3DECL(int)  STAMR3Deregister(PUVM pUVM, const char *pszPat)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* This is a complete waste of time when shutting down. */
    VMSTATE enmState = VMR3GetStateU(pUVM);
    if (enmState >= VMSTATE_DESTROYING)
        return VINF_SUCCESS;

    return stamR3DeregisterByPattern(pUVM, pszPat);
}


/**
 * Deregister zero or more samples given a (single) pattern matching their
 * names.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pszPatFmt   The name pattern format string.
 * @param   ...         Format string arguments.
 * @sa      STAMR3Deregister, STAMR3DeregisterV
 */
VMMR3DECL(int)  STAMR3DeregisterF(PUVM pUVM, const char *pszPatFmt, ...)
{
    va_list va;
    va_start(va, pszPatFmt);
    int rc = STAMR3DeregisterV(pUVM, pszPatFmt, va);
    va_end(va);
    return rc;
}


/**
 * Deregister zero or more samples given a (single) pattern matching their
 * names.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pszPatFmt   The name pattern format string.
 * @param   va          Format string arguments.
 * @sa      STAMR3Deregister, STAMR3DeregisterF
 */
VMMR3DECL(int)  STAMR3DeregisterV(PUVM pUVM, const char *pszPatFmt, va_list va)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* This is a complete waste of time when shutting down. */
    VMSTATE enmState = VMR3GetStateU(pUVM);
    if (enmState >= VMSTATE_DESTROYING)
        return VINF_SUCCESS;

    char   szPat[STAM_MAX_NAME_LEN + 8];
    size_t cchPat = RTStrPrintfV(szPat, sizeof(szPat), pszPatFmt, va);
    AssertReturn(cchPat <= STAM_MAX_NAME_LEN, VERR_OUT_OF_RANGE);

    return stamR3DeregisterByPattern(pUVM, szPat);
}


/**
 * Deregister zero or more samples given their name prefix.
 *
 * @returns VBox status code.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   pszPrefix   The name prefix of the samples to remove.
 * @sa      STAMR3Deregister, STAMR3DeregisterF, STAMR3DeregisterV
 */
VMMR3DECL(int)  STAMR3DeregisterByPrefix(PUVM pUVM, const char *pszPrefix)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* This is a complete waste of time when shutting down. */
    VMSTATE enmState = VMR3GetStateU(pUVM);
    if (enmState >= VMSTATE_DESTROYING)
        return VINF_SUCCESS;

    size_t const cchPrefix = strlen(pszPrefix);
    int          rc        = VWRN_NOT_FOUND;
    STAM_LOCK_WR(pUVM);

    PSTAMDESC pLast;
    PSTAMDESC pCur = stamR3LookupFindByPrefixRange(pUVM->stam.s.pRoot, pszPrefix, (uint32_t)cchPrefix, &pLast);
    if (pCur)
        for (;;)
        {
            PSTAMDESC const pNext = RTListNodeGetNext(&pCur->ListEntry, STAMDESC, ListEntry);
            Assert(strncmp(pCur->pszName, pszPrefix, cchPrefix) == 0);

            rc = stamR3DestroyDesc(pCur);

            /* advance. */
            if (pCur == pLast)
                break;
            pCur = pNext;
        }

    STAM_UNLOCK_WR(pUVM);
    return rc;
}


/**
 * Resets statistics for the specified VM.
 * It's possible to select a subset of the samples.
 *
 * @returns VBox status code. (Basically, it cannot fail.)
 * @param   pUVM        The user mode VM handle.
 * @param   pszPat      The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                      If NULL all samples are reset.
 * @remarks Don't confuse this with the other 'XYZR3Reset' methods, it's not called at VM reset.
 */
VMMR3DECL(int)  STAMR3Reset(PUVM pUVM, const char *pszPat)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    int rc = VINF_SUCCESS;

    /* ring-0 */
    GVMMRESETSTATISTICSSREQ GVMMReq;
    GMMRESETSTATISTICSSREQ  GMMReq;
    bool fGVMMMatched = (!pszPat || !*pszPat) && !SUPR3IsDriverless();
    bool fGMMMatched  = fGVMMMatched;
    if (fGVMMMatched)
    {
        memset(&GVMMReq.Stats, 0xff, sizeof(GVMMReq.Stats));
        memset(&GMMReq.Stats,  0xff, sizeof(GMMReq.Stats));
    }
    else
    {
        char *pszCopy;
        unsigned cExpressions;
        char **papszExpressions = stamR3SplitPattern(pszPat, &cExpressions, &pszCopy);
        if (!papszExpressions)
            return VERR_NO_MEMORY;

        /* GVMM */
        RT_ZERO(GVMMReq.Stats);
        for (unsigned i = 0; i < RT_ELEMENTS(g_aGVMMStats); i++)
            if (stamR3MultiMatch(papszExpressions, cExpressions, NULL, g_aGVMMStats[i].pszName))
            {
                *((uint8_t *)&GVMMReq.Stats + g_aGVMMStats[i].offVar) = 0xff;
                fGVMMMatched = true;
            }
        if (!fGVMMMatched)
        {
            /** @todo match cpu leaves some rainy day. */
        }

        /* GMM */
        RT_ZERO(GMMReq.Stats);
        for (unsigned i = 0; i < RT_ELEMENTS(g_aGMMStats); i++)
            if (stamR3MultiMatch(papszExpressions, cExpressions, NULL, g_aGMMStats[i].pszName))
            {
                 *((uint8_t *)&GMMReq.Stats + g_aGMMStats[i].offVar) = 0xff;
                 fGMMMatched = true;
            }

        RTMemTmpFree(papszExpressions);
        RTStrFree(pszCopy);
    }

    STAM_LOCK_WR(pUVM);

    if (fGVMMMatched)
    {
        PVM pVM = pUVM->pVM;
        GVMMReq.Hdr.cbReq    = sizeof(GVMMReq);
        GVMMReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        GVMMReq.pSession     = pVM->pSession;
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GVMM_RESET_STATISTICS, 0, &GVMMReq.Hdr);
    }

    if (fGMMMatched)
    {
        PVM pVM = pUVM->pVM;
        GMMReq.Hdr.cbReq    = sizeof(GMMReq);
        GMMReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        GMMReq.pSession     = pVM->pSession;
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GMM_RESET_STATISTICS, 0, &GMMReq.Hdr);
    }

    /* and the reset */
    stamR3EnumU(pUVM, pszPat, false /* fUpdateRing0 */, stamR3ResetOne, pUVM->pVM);

    STAM_UNLOCK_WR(pUVM);
    return rc;
}


/**
 * Resets one statistics sample.
 * Callback for stamR3EnumU().
 *
 * @returns VINF_SUCCESS
 * @param   pDesc   Pointer to the current descriptor.
 * @param   pvArg   User argument - Pointer to the VM.
 */
static int stamR3ResetOne(PSTAMDESC pDesc, void *pvArg)
{
    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            ASMAtomicXchgU64(&pDesc->u.pCounter->c, 0);
            break;

        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
            ASMAtomicXchgU64(&pDesc->u.pProfile->cPeriods, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicks, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicksMax, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicksMin, UINT64_MAX);
            break;

        case STAMTYPE_RATIO_U32_RESET:
            ASMAtomicXchgU32(&pDesc->u.pRatioU32->u32A, 0);
            ASMAtomicXchgU32(&pDesc->u.pRatioU32->u32B, 0);
            break;

        case STAMTYPE_CALLBACK:
            if (pDesc->u.Callback.pfnReset)
                pDesc->u.Callback.pfnReset((PVM)pvArg, pDesc->u.Callback.pvSample);
            break;

        case STAMTYPE_U8_RESET:
        case STAMTYPE_X8_RESET:
            ASMAtomicXchgU8(pDesc->u.pu8, 0);
            break;

        case STAMTYPE_U16_RESET:
        case STAMTYPE_X16_RESET:
            ASMAtomicXchgU16(pDesc->u.pu16, 0);
            break;

        case STAMTYPE_U32_RESET:
        case STAMTYPE_X32_RESET:
            ASMAtomicXchgU32(pDesc->u.pu32, 0);
            break;

        case STAMTYPE_U64_RESET:
        case STAMTYPE_X64_RESET:
            ASMAtomicXchgU64(pDesc->u.pu64, 0);
            break;

        case STAMTYPE_BOOL_RESET:
            ASMAtomicXchgBool(pDesc->u.pf, false);
            break;

        /* These are custom and will not be touched. */
        case STAMTYPE_U8:
        case STAMTYPE_X8:
        case STAMTYPE_U16:
        case STAMTYPE_X16:
        case STAMTYPE_U32:
        case STAMTYPE_X32:
        case STAMTYPE_U64:
        case STAMTYPE_X64:
        case STAMTYPE_RATIO_U32:
        case STAMTYPE_BOOL:
        case STAMTYPE_INTERNAL_SUM:
        case STAMTYPE_INTERNAL_PCT_OF_SUM:
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", pDesc->enmType));
            break;
    }
    NOREF(pvArg);
    return VINF_SUCCESS;
}


/**
 * Get a snapshot of the statistics.
 * It's possible to select a subset of the samples.
 *
 * @returns VBox status code. (Basically, it cannot fail.)
 * @param   pUVM            The user mode VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are reset.
 * @param   fWithDesc       Whether to include the descriptions.
 * @param   ppszSnapshot    Where to store the pointer to the snapshot data.
 *                          The format of the snapshot should be XML, but that will have to be discussed
 *                          when this function is implemented.
 *                          The returned pointer must be freed by calling STAMR3SnapshotFree().
 * @param   pcchSnapshot    Where to store the size of the snapshot data. (Excluding the trailing '\0')
 */
VMMR3DECL(int) STAMR3Snapshot(PUVM pUVM, const char *pszPat, char **ppszSnapshot, size_t *pcchSnapshot, bool fWithDesc)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    STAMR3SNAPSHOTONE State = { NULL, NULL, NULL, pUVM->pVM, 0, VINF_SUCCESS, fWithDesc };

    /*
     * Write the XML header.
     */
    /** @todo Make this proper & valid XML. */
    stamR3SnapshotPrintf(&State, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");

    /*
     * Write the content.
     */
    stamR3SnapshotPrintf(&State, "<Statistics>\n");
    int rc = stamR3EnumU(pUVM, pszPat, true /* fUpdateRing0 */, stamR3SnapshotOne, &State);
    stamR3SnapshotPrintf(&State, "</Statistics>\n");

    if (RT_SUCCESS(rc))
        rc = State.rc;
    else
    {
        RTMemFree(State.pszStart);
        State.pszStart = State.pszEnd = State.psz = NULL;
        State.cbAllocated = 0;
    }

    /*
     * Done.
     */
    *ppszSnapshot = State.pszStart;
    if (pcchSnapshot)
        *pcchSnapshot = State.psz - State.pszStart;
    return rc;
}


/**
 * stamR3EnumU callback employed by STAMR3Snapshot.
 *
 * @returns VBox status code, but it's interpreted as 0 == success / !0 == failure by enmR3Enum.
 * @param   pDesc       The sample.
 * @param   pvArg       The snapshot status structure.
 */
static int stamR3SnapshotOne(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMR3SNAPSHOTONE pThis = (PSTAMR3SNAPSHOTONE)pvArg;

    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pCounter->c == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<Counter c=\"%lld\"", pDesc->u.pCounter->c);
            break;

        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pProfile->cPeriods == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<Profile cPeriods=\"%lld\" cTicks=\"%lld\" cTicksMin=\"%lld\" cTicksMax=\"%lld\"",
                                 pDesc->u.pProfile->cPeriods, pDesc->u.pProfile->cTicks, pDesc->u.pProfile->cTicksMin,
                                 pDesc->u.pProfile->cTicksMax);
            break;

        case STAMTYPE_RATIO_U32:
        case STAMTYPE_RATIO_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && !pDesc->u.pRatioU32->u32A && !pDesc->u.pRatioU32->u32B)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<Ratio32 u32A=\"%lld\" u32B=\"%lld\"",
                                 pDesc->u.pRatioU32->u32A, pDesc->u.pRatioU32->u32B);
            break;

        case STAMTYPE_CALLBACK:
        {
            char szBuf[512];
            pDesc->u.Callback.pfnPrint(pThis->pVM, pDesc->u.Callback.pvSample, szBuf, sizeof(szBuf));
            stamR3SnapshotPrintf(pThis, "<Callback val=\"%s\"", szBuf);
            break;
        }

        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<U8 val=\"%u\"", *pDesc->u.pu8);
            break;

        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<X8 val=\"%#x\"", *pDesc->u.pu8);
            break;

        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<U16 val=\"%u\"", *pDesc->u.pu16);
            break;

        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<X16 val=\"%#x\"", *pDesc->u.pu16);
            break;

        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<U32 val=\"%u\"", *pDesc->u.pu32);
            break;

        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<X32 val=\"%#x\"", *pDesc->u.pu32);
            break;

        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<U64 val=\"%llu\"", *pDesc->u.pu64);
            break;

        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<X64 val=\"%#llx\"", *pDesc->u.pu64);
            break;

        case STAMTYPE_BOOL:
        case STAMTYPE_BOOL_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pf == false)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<BOOL val=\"%RTbool\"", *pDesc->u.pf);
            break;

        case STAMTYPE_INTERNAL_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3SumRefresh(pSum);
            switch (pSum->enmType)
            {
                case STAMTYPE_COUNTER:
                    if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Counter.c == 0)
                        return VINF_SUCCESS;
                    stamR3SnapshotPrintf(pThis, "<Counter c=\"%lld\"", pSum->u.Counter.c);
                    break;

                case STAMTYPE_PROFILE:
                    if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Profile.cPeriods == 0)
                        return VINF_SUCCESS;
                    stamR3SnapshotPrintf(pThis, "<Profile cPeriods=\"%lld\" cTicks=\"%lld\" cTicksMin=\"%lld\" cTicksMax=\"%lld\"",
                                         pSum->u.Profile.cPeriods, pSum->u.Profile.cTicks, pSum->u.Profile.cTicksMin,
                                         pSum->u.Profile.cTicksMax);
                    break;

                default:
                    AssertMsgFailedReturn(("%d\n", pSum->enmType), VINF_SUCCESS);
            }
            break;
        }

        case STAMTYPE_INTERNAL_PCT_OF_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3PctOfSumRefresh(pDesc, pSum);
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Counter.c == 0)
                return VINF_SUCCESS;
            stamR3SnapshotPrintf(pThis, "<Counter c=\"%lld\"", pSum->u.Counter.c);
            break;
        }

        default:
            AssertMsgFailedReturn(("%d\n", pDesc->enmType), VINF_SUCCESS);
    }

    stamR3SnapshotPrintf(pThis, " unit=\"%s\"", STAMR3GetUnit(pDesc->enmUnit));

    switch (pDesc->enmVisibility)
    {
        default:
        case STAMVISIBILITY_ALWAYS:
            break;
        case STAMVISIBILITY_USED:
            stamR3SnapshotPrintf(pThis, " vis=\"used\"");
            break;
        case STAMVISIBILITY_NOT_GUI:
            stamR3SnapshotPrintf(pThis, " vis=\"not-gui\"");
            break;
    }

    stamR3SnapshotPrintf(pThis, " name=\"%s\"", pDesc->pszName);

    if (pThis->fWithDesc && pDesc->pszDesc)
    {
        /*
         * The description is a bit tricky as it may include chars that
         * xml requires to be escaped.
         */
        const char *pszBadChar = strpbrk(pDesc->pszDesc, "&<>\"'");
        if (!pszBadChar)
            return stamR3SnapshotPrintf(pThis, " desc=\"%s\"/>\n", pDesc->pszDesc);

        stamR3SnapshotPrintf(pThis, " desc=\"");
        const char *pszCur = pDesc->pszDesc;
        do
        {
            stamR3SnapshotPrintf(pThis, "%.*s", pszBadChar - pszCur, pszCur);
            switch (*pszBadChar)
            {
                case '&':   stamR3SnapshotPrintf(pThis, "&amp;");   break;
                case '<':   stamR3SnapshotPrintf(pThis, "&lt;");    break;
                case '>':   stamR3SnapshotPrintf(pThis, "&gt;");    break;
                case '"':   stamR3SnapshotPrintf(pThis, "&quot;");  break;
                case '\'':  stamR3SnapshotPrintf(pThis, "&apos;");  break;
                default:    AssertMsgFailed(("%c", *pszBadChar));    break;
            }
            pszCur = pszBadChar + 1;
            pszBadChar = strpbrk(pszCur, "&<>\"'");
        } while (pszBadChar);
        return stamR3SnapshotPrintf(pThis, "%s\"/>\n", pszCur);
    }
    return stamR3SnapshotPrintf(pThis, "/>\n");
}


/**
 * Output callback for stamR3SnapshotPrintf.
 *
 * @returns number of bytes written.
 * @param   pvArg       The snapshot status structure.
 * @param   pach        Pointer to an array of characters (bytes).
 * @param   cch         The number or chars (bytes) to write from the array.
 */
static DECLCALLBACK(size_t) stamR3SnapshotOutput(void *pvArg, const char *pach, size_t cch)
{
    PSTAMR3SNAPSHOTONE pThis = (PSTAMR3SNAPSHOTONE)pvArg;

    /*
     * Make sure we've got space for it.
     */
    if (RT_UNLIKELY((uintptr_t)pThis->pszEnd - (uintptr_t)pThis->psz < cch + 1))
    {
        if (RT_FAILURE(pThis->rc))
            return 0;

        size_t cbNewSize = pThis->cbAllocated;
        if (cbNewSize > cch)
            cbNewSize *= 2;
        else
            cbNewSize += RT_ALIGN(cch + 1, 0x1000);
        char *pszNew = (char *)RTMemRealloc(pThis->pszStart, cbNewSize);
        if (!pszNew)
        {
            /*
             * Free up immediately, out-of-memory is bad news and this
             * isn't an important allocations / API.
             */
            pThis->rc = VERR_NO_MEMORY;
            RTMemFree(pThis->pszStart);
            pThis->pszStart = pThis->pszEnd = pThis->psz = NULL;
            pThis->cbAllocated = 0;
            return 0;
        }

        pThis->psz = pszNew + (pThis->psz - pThis->pszStart);
        pThis->pszStart = pszNew;
        pThis->pszEnd = pszNew + cbNewSize;
        pThis->cbAllocated = cbNewSize;
    }

    /*
     * Copy the chars to the buffer and terminate it.
     */
    if (cch)
    {
        memcpy(pThis->psz, pach, cch);
        pThis->psz += cch;
    }
    *pThis->psz = '\0';
    return cch;
}


/**
 * Wrapper around RTStrFormatV for use by the snapshot API.
 *
 * @returns VBox status code.
 * @param   pThis       The snapshot status structure.
 * @param   pszFormat   The format string.
 * @param   ...         Optional arguments.
 */
static int stamR3SnapshotPrintf(PSTAMR3SNAPSHOTONE pThis, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTStrFormatV(stamR3SnapshotOutput, pThis, NULL, NULL, pszFormat, va);
    va_end(va);
    return pThis->rc;
}


/**
 * Releases a statistics snapshot returned by STAMR3Snapshot().
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszSnapshot     The snapshot data pointer returned by STAMR3Snapshot().
 *                          NULL is allowed.
 */
VMMR3DECL(int)  STAMR3SnapshotFree(PUVM pUVM, char *pszSnapshot)
{
    if (pszSnapshot)
        RTMemFree(pszSnapshot);
    NOREF(pUVM);
    return VINF_SUCCESS;
}


/**
 * Dumps the selected statistics to the log.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are written to the log.
 */
VMMR3DECL(int)  STAMR3Dump(PUVM pUVM, const char *pszPat)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    STAMR3PRINTONEARGS Args;
    Args.pUVM = pUVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumLogPrintf;

    stamR3EnumU(pUVM, pszPat, true /* fUpdateRing0 */, stamR3PrintOne, &Args);
    return VINF_SUCCESS;
}


/**
 * Prints to the log.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumLogPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTLogPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Dumps the selected statistics to the release log.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are written to the log.
 */
VMMR3DECL(int)  STAMR3DumpToReleaseLog(PUVM pUVM, const char *pszPat)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    STAMR3PRINTONEARGS Args;
    Args.pUVM = pUVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumRelLogPrintf;

    stamR3EnumU(pUVM, pszPat, true /* fUpdateRing0 */, stamR3PrintOne, &Args);
    return VINF_SUCCESS;
}

/**
 * Prints to the release log.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumRelLogPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTLogRelPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Prints the selected statistics to standard out.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are reset.
 */
VMMR3DECL(int)  STAMR3Print(PUVM pUVM, const char *pszPat)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    STAMR3PRINTONEARGS Args;
    Args.pUVM = pUVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumPrintf;

    stamR3EnumU(pUVM, pszPat, true /* fUpdateRing0 */, stamR3PrintOne, &Args);
    return VINF_SUCCESS;
}


/**
 * Prints to stdout.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Prints one sample.
 * Callback for stamR3EnumU().
 *
 * @returns VINF_SUCCESS
 * @param   pDesc   Pointer to the current descriptor.
 * @param   pvArg   User argument - STAMR3PRINTONEARGS.
 */
static int stamR3PrintOne(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMR3PRINTONEARGS pArgs = (PSTAMR3PRINTONEARGS)pvArg;

    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pCounter->c == 0)
                return VINF_SUCCESS;

            pArgs->pfnPrintf(pArgs, "%-42s %9llu %s\n", pDesc->pszName, pDesc->u.pCounter->c, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
        {
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pProfile->cPeriods == 0)
                return VINF_SUCCESS;

            uint64_t const u64 = pDesc->u.pProfile->cPeriods ? pDesc->u.pProfile->cPeriods : 1;
            pArgs->pfnPrintf(pArgs, "%-42s %9llu %s (%12llu %s, %7llu %s, max %9llu, min %7lld)\n", pDesc->pszName,
                             pDesc->u.pProfile->cTicks / u64, STAMR3GetUnit(pDesc->enmUnit),
                             pDesc->u.pProfile->cTicks, STAMR3GetUnit1(pDesc->enmUnit),
                             pDesc->u.pProfile->cPeriods, STAMR3GetUnit2(pDesc->enmUnit),
                             pDesc->u.pProfile->cTicksMax, pDesc->u.pProfile->cTicksMin);
            break;
        }

        case STAMTYPE_RATIO_U32:
        case STAMTYPE_RATIO_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && !pDesc->u.pRatioU32->u32A && !pDesc->u.pRatioU32->u32B)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9u:%-8u %s\n", pDesc->pszName,
                             pDesc->u.pRatioU32->u32A, pDesc->u.pRatioU32->u32B, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_CALLBACK:
        {
            char szBuf[512];
            pDesc->u.Callback.pfnPrint(pArgs->pUVM->pVM, pDesc->u.Callback.pvSample, szBuf, sizeof(szBuf));
            pArgs->pfnPrintf(pArgs, "%-42s %s %s\n", pDesc->pszName, szBuf, STAMR3GetUnit(pDesc->enmUnit));
            break;
        }

        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9u %s\n", pDesc->pszName, *pDesc->u.pu8, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9x %s\n", pDesc->pszName, *pDesc->u.pu8, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9u %s\n", pDesc->pszName, *pDesc->u.pu16, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9x %s\n", pDesc->pszName, *pDesc->u.pu16, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9u %s\n", pDesc->pszName, *pDesc->u.pu32, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9x %s\n", pDesc->pszName, *pDesc->u.pu32, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9llu %s\n", pDesc->pszName, *pDesc->u.pu64, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9llx %s\n", pDesc->pszName, *pDesc->u.pu64, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_BOOL:
        case STAMTYPE_BOOL_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pf == false)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %s %s\n", pDesc->pszName, *pDesc->u.pf ? "true    " : "false   ", STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_INTERNAL_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3SumRefresh(pSum);
            switch (pSum->enmType)
            {
                case STAMTYPE_COUNTER:
                    if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Counter.c == 0)
                        return VINF_SUCCESS;
                    pArgs->pfnPrintf(pArgs, "%-42s %9llu %s\n", pDesc->pszName, pSum->u.Counter.c, STAMR3GetUnit(pDesc->enmUnit));
                    break;

                case STAMTYPE_PROFILE:
                {
                    if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Profile.cPeriods == 0)
                        return VINF_SUCCESS;

                    uint64_t const u64 = pSum->u.Profile.cPeriods ? pSum->u.Profile.cPeriods : 1;
                    pArgs->pfnPrintf(pArgs, "%-42s %9llu %s (%12llu %s, %7llu %s, max %9llu, min %7lld)\n", pDesc->pszName,
                                     pSum->u.Profile.cTicks / u64, STAMR3GetUnit(pDesc->enmUnit),
                                     pSum->u.Profile.cTicks, STAMR3GetUnit1(pDesc->enmUnit),
                                     pSum->u.Profile.cPeriods, STAMR3GetUnit2(pDesc->enmUnit),
                                     pSum->u.Profile.cTicksMax, pSum->u.Profile.cTicksMin);
                    break;
                }

                default:
                    AssertMsgFailed(("%d\n", pSum->enmType));
                    break;
            }
            break;
        }

        case STAMTYPE_INTERNAL_PCT_OF_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3PctOfSumRefresh(pDesc, pSum);
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pSum->u.Counter.c == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-42s %9llu %s\n", pDesc->pszName, pSum->u.Counter.c, STAMR3GetUnit(pDesc->enmUnit));
            break;
        }

        default:
            AssertMsgFailed(("enmType=%d\n", pDesc->enmType));
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Enumerate the statistics by the means of a callback function.
 *
 * @returns Whatever the callback returns.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   pszPat      The pattern to match samples.
 * @param   pfnEnum     The callback function.
 * @param   pvUser      The pvUser argument of the callback function.
 */
VMMR3DECL(int) STAMR3Enum(PUVM pUVM, const char *pszPat, PFNSTAMR3ENUM pfnEnum, void *pvUser)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);

    STAMR3ENUMONEARGS Args;
    Args.pVM     = pUVM->pVM;
    Args.pfnEnum = pfnEnum;
    Args.pvUser  = pvUser;

    return stamR3EnumU(pUVM, pszPat, true /* fUpdateRing0 */, stamR3EnumOne, &Args);
}


/**
 * Callback function for STARTR3Enum().
 *
 * @returns whatever the callback returns.
 * @param   pDesc       Pointer to the current descriptor.
 * @param   pvArg       Points to a STAMR3ENUMONEARGS structure.
 */
static int stamR3EnumOne(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMR3ENUMONEARGS const pArgs   = (PSTAMR3ENUMONEARGS)pvArg;
    const char * const       pszUnit = STAMR3GetUnit(pDesc->enmUnit);
    switch (pDesc->enmType)
    {
        default:
            return pArgs->pfnEnum(pDesc->pszName, pDesc->enmType, pDesc->u.pv, pDesc->enmUnit, pszUnit,
                                  pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);

        case STAMTYPE_CALLBACK:
        {
            /* Give the enumerator something useful. */
            char szBuf[512];
            pDesc->u.Callback.pfnPrint(pArgs->pVM, pDesc->u.Callback.pvSample, szBuf, sizeof(szBuf));
            return pArgs->pfnEnum(pDesc->pszName, pDesc->enmType, szBuf, pDesc->enmUnit, pszUnit,
                                  pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);
        }

        case STAMTYPE_INTERNAL_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3SumRefresh(pSum);
            return pArgs->pfnEnum(pDesc->pszName, pSum->enmType, &pSum->u, pDesc->enmUnit, pszUnit,
                                  pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);
        }

        case STAMTYPE_INTERNAL_PCT_OF_SUM:
        {
            PSTAMSUMSAMPLE const pSum = pDesc->u.pSum;
            stamR3PctOfSumRefresh(pDesc, pSum);
            return pArgs->pfnEnum(pDesc->pszName, pSum->enmType, &pSum->u, pDesc->enmUnit, pszUnit,
                                  pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);
        }
    }
}

static void stamR3RefreshGroup(PUVM pUVM, uint8_t iRefreshGroup, uint64_t *pbmRefreshedGroups)
{
    *pbmRefreshedGroups |= RT_BIT_64(iRefreshGroup);

    PVM pVM = pUVM->pVM;
    if (pVM && pVM->pSession)
    {
        switch (iRefreshGroup)
        {
            /*
             * GVMM
             */
            case STAM_REFRESH_GRP_GVMM:
            {
                GVMMQUERYSTATISTICSSREQ Req;
                Req.Hdr.cbReq    = sizeof(Req);
                Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                Req.pSession     = pVM->pSession;
                int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GVMM_QUERY_STATISTICS, 0, &Req.Hdr);
                if (RT_SUCCESS(rc))
                {
                    pUVM->stam.s.GVMMStats = Req.Stats;

                    /*
                     * Check if the number of host CPUs has changed (it will the first
                     * time around and normally never again).
                     */
                    if (RT_UNLIKELY(pUVM->stam.s.GVMMStats.cHostCpus > pUVM->stam.s.cRegisteredHostCpus))
                    {
                        if (RT_UNLIKELY(pUVM->stam.s.GVMMStats.cHostCpus > pUVM->stam.s.cRegisteredHostCpus))
                        {
                            STAM_UNLOCK_RD(pUVM);
                            STAM_LOCK_WR(pUVM);
                            uint32_t cCpus = pUVM->stam.s.GVMMStats.cHostCpus;
                            for (uint32_t iCpu = pUVM->stam.s.cRegisteredHostCpus; iCpu < cCpus; iCpu++)
                            {
                                char   szName[120];
                                size_t cchBase = RTStrPrintf(szName, sizeof(szName), "/GVMM/HostCpus/%u", iCpu);
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].idCpu, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_NONE,
                                                "Host CPU ID", STAM_REFRESH_GRP_GVMM);
                                strcpy(&szName[cchBase], "/idxCpuSet");
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].idxCpuSet, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_NONE,
                                                "CPU Set index", STAM_REFRESH_GRP_GVMM);
                                strcpy(&szName[cchBase], "/DesiredHz");
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].uDesiredHz, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_HZ,
                                                "The desired frequency", STAM_REFRESH_GRP_GVMM);
                                strcpy(&szName[cchBase], "/CurTimerHz");
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].uTimerHz, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_HZ,
                                                "The current timer frequency", STAM_REFRESH_GRP_GVMM);
                                strcpy(&szName[cchBase], "/PPTChanges");
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].cChanges, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES,
                                                "RTTimerChangeInterval calls", STAM_REFRESH_GRP_GVMM);
                                strcpy(&szName[cchBase], "/PPTStarts");
                                stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aHostCpus[iCpu].cStarts, NULL, NULL,
                                                STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES,
                                                "RTTimerStart calls", STAM_REFRESH_GRP_GVMM);
                            }
                            pUVM->stam.s.cRegisteredHostCpus = cCpus;
                            STAM_UNLOCK_WR(pUVM);
                            STAM_LOCK_RD(pUVM);
                        }
                    }
                }
                break;
            }

            /*
             * GMM
             */
            case STAM_REFRESH_GRP_GMM:
            {
                GMMQUERYSTATISTICSSREQ Req;
                Req.Hdr.cbReq    = sizeof(Req);
                Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                Req.pSession     = pVM->pSession;
                int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GMM_QUERY_STATISTICS, 0, &Req.Hdr);
                if (RT_SUCCESS(rc))
                    pUVM->stam.s.GMMStats = Req.Stats;
                break;
            }

            /*
             * NEM.
             */
            case STAM_REFRESH_GRP_NEM:
                SUPR3CallVMMR0(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_NEM_UPDATE_STATISTICS, NULL);
                break;

            default:
                AssertMsgFailed(("iRefreshGroup=%d\n", iRefreshGroup));
        }
    }
}


/**
 * Refreshes the statistics behind the given entry, if necessary.
 *
 * This helps implement fetching global ring-0 stats into ring-3 accessible
 * storage.  GVMM, GMM and NEM makes use of this.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   pCur                The statistics descriptor which group to check
 *                              and maybe update.
 * @param   pbmRefreshedGroups  Bitmap tracking what has already been updated.
 */
DECLINLINE(void) stamR3Refresh(PUVM pUVM, PSTAMDESC pCur, uint64_t *pbmRefreshedGroups)
{
    uint8_t const iRefreshGroup = pCur->iRefreshGroup;
    if (RT_LIKELY(iRefreshGroup == STAM_REFRESH_GRP_NONE))
    { /* likely */ }
    else if (!(*pbmRefreshedGroups & RT_BIT_64(iRefreshGroup)))
        stamR3RefreshGroup(pUVM, iRefreshGroup, pbmRefreshedGroups);
}


/**
 * Match a name against an array of patterns.
 *
 * @returns true if it matches, false if it doesn't match.
 * @param   papszExpressions    The array of pattern expressions.
 * @param   cExpressions        The number of array entries.
 * @param   piExpression        Where to read/store the current skip index. Optional.
 * @param   pszName             The name to match.
 */
static bool stamR3MultiMatch(const char * const *papszExpressions, unsigned cExpressions,
                             unsigned *piExpression, const char *pszName)
{
    for (unsigned i = piExpression ? *piExpression : 0; i < cExpressions; i++)
    {
        const char *pszPat = papszExpressions[i];
        if (RTStrSimplePatternMatch(pszPat, pszName))
        {
            /* later:
            if (piExpression && i > *piExpression)
            {
                Check if we can skip some expressions.
                Requires the expressions to be sorted.
            }*/
            return true;
        }
    }
    return false;
}


/**
 * Splits a multi pattern into single ones.
 *
 * @returns Pointer to an array of single patterns. Free it with RTMemTmpFree.
 * @param   pszPat          The pattern to split.
 * @param   pcExpressions   The number of array elements.
 * @param   ppszCopy        The pattern copy to free using RTStrFree.
 */
static char **stamR3SplitPattern(const char *pszPat, unsigned *pcExpressions, char **ppszCopy)
{
    Assert(pszPat && *pszPat);

    char *pszCopy = RTStrDup(pszPat);
    if (!pszCopy)
        return NULL;

    /* count them & allocate array. */
    char *psz = pszCopy;
    unsigned cExpressions = 1;
    while ((psz = strchr(psz, '|')) != NULL)
        cExpressions++, psz++;

    char **papszExpressions = (char **)RTMemTmpAllocZ((cExpressions + 1) * sizeof(char *));
    if (!papszExpressions)
    {
        RTStrFree(pszCopy);
        return NULL;
    }

    /* split */
    psz = pszCopy;
    for (unsigned i = 0;;)
    {
        papszExpressions[i] = psz;
        if (++i >= cExpressions)
            break;
        psz = strchr(psz, '|');
        *psz++ = '\0';
    }

    /* sort the array, putting '*' last. */
    /** @todo sort it... */

    *pcExpressions = cExpressions;
    *ppszCopy = pszCopy;
    return papszExpressions;
}


/**
 * Enumerates the nodes selected by a pattern or all nodes if no pattern
 * is specified.
 *
 * The call may lock STAM for writing before calling this function, however do
 * not lock it for reading as this function may need to write lock STAM.
 *
 * @returns The rc from the callback.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszPat          Pattern.
 * @param   fUpdateRing0    Update the stats residing in ring-0.
 * @param   pfnCallback     Callback function which shall be called for matching nodes.
 *                          If it returns anything but VINF_SUCCESS the enumeration is
 *                          terminated and the status code returned to the caller.
 * @param   pvArg           User parameter for the callback.
 */
static int stamR3EnumU(PUVM pUVM, const char *pszPat, bool fUpdateRing0,
                       int (*pfnCallback)(PSTAMDESC pDesc, void *pvArg), void *pvArg)
{
    size_t const cchPat            = pszPat ? strlen(pszPat) : 0;
    int          rc                = VINF_SUCCESS;
    uint64_t     bmRefreshedGroups = 0;
    PSTAMDESC    pCur;

    /*
     * All.
     */
    if (   cchPat < 1
        || (   cchPat == 1
            && *pszPat == '*'))
    {
        STAM_LOCK_RD(pUVM);
        RTListForEach(&pUVM->stam.s.List, pCur, STAMDESC, ListEntry)
        {
            if (fUpdateRing0)
                stamR3Refresh(pUVM, pCur, &bmRefreshedGroups);
            rc = pfnCallback(pCur, pvArg);
            if (rc)
                break;
        }
        STAM_UNLOCK_RD(pUVM);
    }

    /*
     * Single expression pattern.
     */
    else if (memchr(pszPat, '|', cchPat) == NULL)
    {
        const char  *pszAsterisk = (const char *)memchr(pszPat, '*',  cchPat);
        const char  *pszQuestion = (const char *)memchr(pszPat, '?',  cchPat);

        STAM_LOCK_RD(pUVM);
        if (!pszAsterisk && !pszQuestion)
        {
            pCur = stamR3LookupFindDesc(pUVM->stam.s.pRoot, pszPat);
            if (pCur)
            {
                if (fUpdateRing0)
                    stamR3Refresh(pUVM, pCur, &bmRefreshedGroups);
                rc = pfnCallback(pCur, pvArg);
            }
        }
        /* Is this a prefix expression where we can use the lookup tree to
           efficiently figure out the exact range? */
        else if (   pszAsterisk == &pszPat[cchPat - 1]
                 && pszPat[0] == '/'
                 && !pszQuestion)
        {
            PSTAMDESC pLast;
            pCur = stamR3LookupFindByPrefixRange(pUVM->stam.s.pRoot, pszPat, (uint32_t)(cchPat - 1), &pLast);
            if (pCur)
            {
                for (;;)
                {
                    Assert(strncmp(pCur->pszName, pszPat, cchPat - 1) == 0);
                    if (fUpdateRing0)
                        stamR3Refresh(pUVM, pCur, &bmRefreshedGroups);
                    rc = pfnCallback(pCur, pvArg);
                    if (rc)
                        break;
                    if (pCur == pLast)
                        break;
                    pCur = RTListNodeGetNext(&pCur->ListEntry, STAMDESC, ListEntry);
                }
                Assert(pLast);
            }
            else
                Assert(!pLast);
        }
        else
        {
            /* It's a more complicated pattern.  Find the approximate range
               and scan it for matches. */
            PSTAMDESC pLast;
            pCur = stamR3LookupFindPatternDescRange(pUVM->stam.s.pRoot, &pUVM->stam.s.List, pszPat, &pLast);
            if (pCur)
            {
                for (;;)
                {
                    if (RTStrSimplePatternMatch(pszPat, pCur->pszName))
                    {
                        if (fUpdateRing0)
                            stamR3Refresh(pUVM, pCur, &bmRefreshedGroups);
                        rc = pfnCallback(pCur, pvArg);
                        if (rc)
                            break;
                    }
                    if (pCur == pLast)
                        break;
                    pCur = RTListNodeGetNext(&pCur->ListEntry, STAMDESC, ListEntry);
                }
                Assert(pLast);
            }
            else
                Assert(!pLast);
        }
        STAM_UNLOCK_RD(pUVM);
    }

    /*
     * Multi expression pattern.
     */
    else
    {
        /*
         * Split up the pattern first.
         */
        char *pszCopy;
        unsigned cExpressions;
        char **papszExpressions = stamR3SplitPattern(pszPat, &cExpressions, &pszCopy);
        if (!papszExpressions)
            return VERR_NO_MEMORY;

        /*
         * Perform the enumeration.
         */
        STAM_LOCK_RD(pUVM);
        unsigned iExpression = 0;
        RTListForEach(&pUVM->stam.s.List, pCur, STAMDESC, ListEntry)
        {
            if (stamR3MultiMatch(papszExpressions, cExpressions, &iExpression, pCur->pszName))
            {
                if (fUpdateRing0)
                    stamR3Refresh(pUVM, pCur, &bmRefreshedGroups);
                rc = pfnCallback(pCur, pvArg);
                if (rc)
                    break;
            }
        }
        STAM_UNLOCK_RD(pUVM);

        RTMemTmpFree(papszExpressions);
        RTStrFree(pszCopy);
    }

    return rc;
}


/**
 * Registers the ring-0 statistics.
 *
 * @param   pUVM        Pointer to the user mode VM structure.
 */
static void stamR3Ring0StatsRegisterU(PUVM pUVM)
{
    /* GVMM */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aGVMMStats); i++)
        stamR3RegisterU(pUVM, (uint8_t *)&pUVM->stam.s.GVMMStats + g_aGVMMStats[i].offVar, NULL, NULL,
                        g_aGVMMStats[i].enmType, STAMVISIBILITY_ALWAYS, g_aGVMMStats[i].pszName,
                        g_aGVMMStats[i].enmUnit, g_aGVMMStats[i].pszDesc, STAM_REFRESH_GRP_GVMM);

    for (unsigned i = 0; i < pUVM->cCpus; i++)
    {
        char   szName[120];
        size_t cchBase = RTStrPrintf(szName, sizeof(szName), pUVM->cCpus < 10 ? "/GVMM/VCpus/%u/" : "/GVMM/VCpus/%02u/", i);

        strcpy(&szName[cchBase], "cWakeUpTimerHits");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].cWakeUpTimerHits, NULL, NULL,
                        STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES, "", STAM_REFRESH_GRP_GVMM);

        strcpy(&szName[cchBase], "cWakeUpTimerMisses");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].cWakeUpTimerMisses, NULL, NULL,
                        STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES, "", STAM_REFRESH_GRP_GVMM);

        strcpy(&szName[cchBase], "cWakeUpTimerCanceled");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].cWakeUpTimerCanceled, NULL, NULL,
                        STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES, "", STAM_REFRESH_GRP_GVMM);

        strcpy(&szName[cchBase], "cWakeUpTimerSameCpu");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].cWakeUpTimerSameCpu, NULL, NULL,
                        STAMTYPE_U32, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES, "", STAM_REFRESH_GRP_GVMM);

        strcpy(&szName[cchBase], "Start");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].Start, NULL, NULL,
                        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_TICKS_PER_CALL, "", STAM_REFRESH_GRP_GVMM);

        strcpy(&szName[cchBase], "Stop");
        stamR3RegisterU(pUVM, &pUVM->stam.s.GVMMStats.aVCpus[i].Stop, NULL, NULL,
                        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_TICKS_PER_CALL, "", STAM_REFRESH_GRP_GVMM);
    }
    pUVM->stam.s.cRegisteredHostCpus = 0;

    /* GMM */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aGMMStats); i++)
        stamR3RegisterU(pUVM, (uint8_t *)&pUVM->stam.s.GMMStats + g_aGMMStats[i].offVar, NULL, NULL,
                        g_aGMMStats[i].enmType, STAMVISIBILITY_ALWAYS, g_aGMMStats[i].pszName,
                        g_aGMMStats[i].enmUnit, g_aGMMStats[i].pszDesc, STAM_REFRESH_GRP_GMM);
}


/**
 * Get the unit string.
 *
 * @returns Pointer to read only unit string.
 * @param   enmUnit     The unit.
 */
VMMR3DECL(const char *) STAMR3GetUnit(STAMUNIT enmUnit)
{
    switch (enmUnit)
    {
        case STAMUNIT_NONE:                 return "";
        case STAMUNIT_CALLS:                return "calls";
        case STAMUNIT_CALLS_PER_TB:         return "calls/tb";
        case STAMUNIT_COUNT:                return "count";
        case STAMUNIT_BYTES:                return "bytes";
        case STAMUNIT_BYTES_PER_CALL:       return "bytes/call";
        case STAMUNIT_PAGES:                return "pages";
        case STAMUNIT_ERRORS:               return "errors";
        case STAMUNIT_OCCURENCES:           return "times";
        case STAMUNIT_TICKS:                return "ticks";
        case STAMUNIT_TICKS_PER_CALL:       return "ticks/call";
        case STAMUNIT_TICKS_PER_OCCURENCE:  return "ticks/time";
        case STAMUNIT_GOOD_BAD:             return "good:bad";
        case STAMUNIT_MEGABYTES:            return "megabytes";
        case STAMUNIT_KILOBYTES:            return "kilobytes";
        case STAMUNIT_NS:                   return "ns";
        case STAMUNIT_NS_PER_CALL:          return "ns/call";
        case STAMUNIT_NS_PER_OCCURENCE:     return "ns/time";
        case STAMUNIT_PCT:                  return "%";
        case STAMUNIT_PP1K:                 return "pp1k";
        case STAMUNIT_PP10K:                return "pp10k";
        case STAMUNIT_PPM:                  return "ppm";
        case STAMUNIT_PPB:                  return "ppb";
        case STAMUNIT_HZ:                   return "Hz";
        case STAMUNIT_INSTR:                return "instr";
        case STAMUNIT_INSTR_PER_TB:         return "instr/tb";
        case STAMUNIT_BYTES_PER_TB:         return "bytes/tb";

        default:
            AssertMsgFailed(("Unknown unit %d\n", enmUnit));
            return "(?unit?)";
    }
}


/**
 * For something per something-else unit, get the first something.
 *
 * @returns Pointer to read only unit string.
 * @param   enmUnit     The unit.
 */
VMMR3DECL(const char *) STAMR3GetUnit1(STAMUNIT enmUnit)
{
    switch (enmUnit)
    {
        case STAMUNIT_NONE:                 return "";
        case STAMUNIT_CALLS:                return "calls";
        case STAMUNIT_CALLS_PER_TB:         return "calls";
        case STAMUNIT_COUNT:                return "count";
        case STAMUNIT_BYTES:                return "bytes";
        case STAMUNIT_BYTES_PER_CALL:       return "bytes";
        case STAMUNIT_PAGES:                return "pages";
        case STAMUNIT_ERRORS:               return "errors";
        case STAMUNIT_OCCURENCES:           return "times";
        case STAMUNIT_TICKS:                return "ticks";
        case STAMUNIT_TICKS_PER_CALL:       return "ticks";
        case STAMUNIT_TICKS_PER_OCCURENCE:  return "ticks";
        case STAMUNIT_GOOD_BAD:             return "good";
        case STAMUNIT_MEGABYTES:            return "megabytes";
        case STAMUNIT_KILOBYTES:            return "kilobytes";
        case STAMUNIT_NS:                   return "ns";
        case STAMUNIT_NS_PER_CALL:          return "ns";
        case STAMUNIT_NS_PER_OCCURENCE:     return "ns";
        case STAMUNIT_PCT:                  return "%";
        case STAMUNIT_PP1K:                 return "pp1k";
        case STAMUNIT_PP10K:                return "pp10k";
        case STAMUNIT_PPM:                  return "ppm";
        case STAMUNIT_PPB:                  return "ppb";
        case STAMUNIT_HZ:                   return "Hz";
        case STAMUNIT_INSTR:                return "instr";
        case STAMUNIT_INSTR_PER_TB:         return "instr";
        case STAMUNIT_BYTES_PER_TB:         return "bytes";

        default:
            AssertMsgFailed(("Unknown unit %d\n", enmUnit));
            return "(?unit?)";
    }
}


/**
 * For something per something-else unit, get the something-else.
 *
 * @returns Pointer to read only unit string.
 * @param   enmUnit     The unit.
 */
VMMR3DECL(const char *) STAMR3GetUnit2(STAMUNIT enmUnit)
{
    switch (enmUnit)
    {
        case STAMUNIT_TICKS_PER_CALL:       return "calls";
        case STAMUNIT_NS_PER_CALL:          return "calls";
        case STAMUNIT_BYTES_PER_CALL:       return "calls";
        case STAMUNIT_TICKS_PER_OCCURENCE:  return "times";
        case STAMUNIT_NS_PER_OCCURENCE:     return "times";
        case STAMUNIT_NONE:                 return "times";
        case STAMUNIT_GOOD_BAD:             return "bad";
        case STAMUNIT_CALLS_PER_TB:         return "tbs";
        case STAMUNIT_INSTR_PER_TB:         return "tbs";
        case STAMUNIT_BYTES_PER_TB:         return "tbs";
        default:
            AssertMsgFailed(("Wrong unit %d\n", enmUnit));
            return "times";
    }
}

#ifdef VBOX_WITH_DEBUGGER

/**
 * @callback_method_impl{FNDBGCCMD, The '.stats' command.}
 */
static DECLCALLBACK(int) stamR3CmdStats(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    if (RTListIsEmpty(&pUVM->stam.s.List))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "No statistics present");

    /*
     * Do the printing.
     */
    STAMR3PRINTONEARGS Args;
    Args.pUVM       = pUVM;
    Args.pvArg      = pCmdHlp;
    Args.pfnPrintf  = stamR3EnumDbgfPrintf;

    return stamR3EnumU(pUVM, cArgs ? paArgs[0].u.pszString : NULL, true /* fUpdateRing0 */, stamR3PrintOne, &Args);
}


/**
 * Display one sample in the debugger.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumDbgfPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    PDBGCCMDHLP pCmdHlp = (PDBGCCMDHLP)pArgs->pvArg;

    va_list va;
    va_start(va, pszFormat);
    pCmdHlp->pfnPrintfV(pCmdHlp, NULL, pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * @callback_method_impl{FNDBGCCMD, The '.statsreset' command.}
 */
static DECLCALLBACK(int) stamR3CmdStatsReset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    if (RTListIsEmpty(&pUVM->stam.s.List))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "No statistics present");

    /*
     * Execute reset.
     */
    int rc = STAMR3Reset(pUVM, cArgs ? paArgs[0].u.pszString : NULL);
    if (RT_SUCCESS(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "STAMR3ResetU");
    return DBGCCmdHlpPrintf(pCmdHlp, "Statistics have been reset.\n");
}

#endif /* VBOX_WITH_DEBUGGER */

