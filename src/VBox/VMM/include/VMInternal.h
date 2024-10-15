/* $Id: VMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_VMInternal_h
#define VMM_INCLUDED_SRC_include_VMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/vmm/vmapi.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/setjmp-without-sigmask.h>



/** @defgroup grp_vm_int   Internals
 * @ingroup grp_vm
 * @internal
 * @{
 */


/**
 * VM state change callback.
 */
typedef struct VMATSTATE
{
    /** Pointer to the next one. */
    struct VMATSTATE               *pNext;
    /** Pointer to the callback. */
    PFNVMATSTATE                    pfnAtState;
    /** The user argument. */
    void                           *pvUser;
} VMATSTATE;
/** Pointer to a VM state change callback. */
typedef VMATSTATE *PVMATSTATE;


/**
 * VM error callback.
 */
typedef struct VMATERROR
{
    /** Pointer to the next one. */
    struct VMATERROR               *pNext;
    /** Pointer to the callback. */
    PFNVMATERROR                    pfnAtError;
    /** The user argument. */
    void                           *pvUser;
} VMATERROR;
/** Pointer to a VM error callback. */
typedef VMATERROR *PVMATERROR;


/**
 * Chunk of memory allocated off the hypervisor heap in which
 * we copy the error details.
 */
typedef struct VMERROR
{
    /** The size of the chunk. */
    uint32_t                        cbAllocated;
    /** The current offset into the chunk.
     * We start by putting the filename and function immediately
     * after the end of the buffer. */
    uint32_t                        off;
    /** Offset from the start of this structure to the file name. */
    uint32_t                        offFile;
    /** The line number. */
    uint32_t                        iLine;
    /** Offset from the start of this structure to the function name. */
    uint32_t                        offFunction;
    /** Offset from the start of this structure to the formatted message text. */
    uint32_t                        offMessage;
    /** The VBox status code. */
    int32_t                         rc;
} VMERROR, *PVMERROR;


/**
 * VM runtime error callback.
 */
typedef struct VMATRUNTIMEERROR
{
    /** Pointer to the next one. */
    struct VMATRUNTIMEERROR         *pNext;
    /** Pointer to the callback. */
    PFNVMATRUNTIMEERROR              pfnAtRuntimeError;
    /** The user argument. */
    void                            *pvUser;
} VMATRUNTIMEERROR;
/** Pointer to a VM error callback. */
typedef VMATRUNTIMEERROR *PVMATRUNTIMEERROR;


/**
 * Chunk of memory allocated off the hypervisor heap in which
 * we copy the runtime error details.
 */
typedef struct VMRUNTIMEERROR
{
    /** The size of the chunk. */
    uint32_t                        cbAllocated;
    /** The current offset into the chunk.
     * We start by putting the error ID immediately
     * after the end of the buffer. */
    uint32_t                        off;
    /** Offset from the start of this structure to the error ID. */
    uint32_t                        offErrorId;
    /** Offset from the start of this structure to the formatted message text. */
    uint32_t                        offMessage;
    /** Error flags. */
    uint32_t                        fFlags;
} VMRUNTIMEERROR, *PVMRUNTIMEERROR;

/** The halt method. */
typedef enum
{
    /** The usual invalid value. */
    VMHALTMETHOD_INVALID = 0,
    /** Use the method used during bootstrapping. */
    VMHALTMETHOD_BOOTSTRAP,
    /** Use the default method. */
    VMHALTMETHOD_DEFAULT,
    /** The old spin/yield/block method. */
    VMHALTMETHOD_OLD,
    /** The first go at a block/spin method. */
    VMHALTMETHOD_1,
    /** The first go at a more global approach. */
    VMHALTMETHOD_GLOBAL_1,
    /** The end of valid methods. (not inclusive of course) */
    VMHALTMETHOD_END,
    /** The usual 32-bit max value. */
    VMHALTMETHOD_32BIT_HACK = 0x7fffffff
} VMHALTMETHOD;


/**
 * VM Internal Data (part of the VM structure).
 *
 * @todo Move this and all related things to VMM. The VM component was, to some
 *       extent at least, a bad ad hoc design which should all have been put in
 *       VMM. @see pg_vm.
 */
typedef struct VMINT
{
    /** VM Error Message. */
    R3PTRTYPE(PVMERROR)             pErrorR3;
    /** VM Runtime Error Message. */
    R3PTRTYPE(PVMRUNTIMEERROR)      pRuntimeErrorR3;
    /** The VM was/is-being teleported and has not yet been fully resumed. */
    bool                            fTeleportedAndNotFullyResumedYet;
    /** The VM should power off instead of reset. */
    bool                            fPowerOffInsteadOfReset;
    /** Reset counter (soft + hard). */
    uint32_t                        cResets;
    /** Hard reset counter. */
    uint32_t                        cHardResets;
    /** Soft reset counter. */
    uint32_t                        cSoftResets;
} VMINT;
/** Pointer to the VM Internal Data (part of the VM structure). */
typedef VMINT *PVMINT;


#ifdef IN_RING3

/**
 * VM internal data kept in the UVM.
 */
typedef struct VMINTUSERPERVM
{
    /** Head of the standard request queue. Atomic. */
    volatile PVMREQ                 pNormalReqs;
    /** Head of the priority request queue. Atomic. */
    volatile PVMREQ                 pPriorityReqs;
    /** The last index used during alloc/free. */
    volatile uint32_t               iReqFree;
    /** Number of free request packets. */
    volatile uint32_t               cReqFree;
    /** Array of pointers to lists of free request packets. Atomic. */
    volatile PVMREQ                 apReqFree[16 - (HC_ARCH_BITS == 32 ? 5 : 4)];

    /** The reference count of the UVM handle. */
    volatile uint32_t               cUvmRefs;

    /** Number of active EMTs. */
    volatile uint32_t               cActiveEmts;

# ifdef VBOX_WITH_STATISTICS
#  if HC_ARCH_BITS == 32
    uint32_t                        uPadding;
#  endif
    /** Number of VMR3ReqAlloc returning a new packet. */
    STAMCOUNTER                     StatReqAllocNew;
    /** Number of VMR3ReqAlloc causing races. */
    STAMCOUNTER                     StatReqAllocRaces;
    /** Number of VMR3ReqAlloc returning a recycled packet. */
    STAMCOUNTER                     StatReqAllocRecycled;
    /** Number of VMR3ReqFree calls. */
    STAMCOUNTER                     StatReqFree;
    /** Number of times the request was actually freed. */
    STAMCOUNTER                     StatReqFreeOverflow;
    /** Number of requests served. */
    STAMCOUNTER                     StatReqProcessed;
    /** Number of times there are more than one request and the others needed to be
     * pushed back onto the list. */
    STAMCOUNTER                     StatReqMoreThan1;
    /** Number of times we've raced someone when pushing the other requests back
     * onto the list. */
    STAMCOUNTER                     StatReqPushBackRaces;
# endif

    /** Pointer to the support library session.
     * Mainly for creation and destruction. */
    PSUPDRVSESSION                  pSession;

    /** Force EMT to terminate. */
    bool volatile                   fTerminateEMT;

    /** Critical section for pAtState and enmPrevVMState. */
    RTCRITSECT                      AtStateCritSect;
    /** List of registered state change callbacks. */
    PVMATSTATE                      pAtState;
    /** List of registered state change callbacks. */
    PVMATSTATE                     *ppAtStateNext;
    /** The previous VM state.
     * This is mainly used for the 'Resetting' state, but may come in handy later
     * and when debugging. */
    VMSTATE                         enmPrevVMState;

    /** Reason for the most recent suspend operation. */
    VMSUSPENDREASON                 enmSuspendReason;
    /** Reason for the most recent operation. */
    VMRESUMEREASON                  enmResumeReason;

    /** Critical section for pAtError and pAtRuntimeError. */
    RTCRITSECT                      AtErrorCritSect;

    /** List of registered error callbacks. */
    PVMATERROR                      pAtError;
    /** List of registered error callbacks. */
    PVMATERROR                     *ppAtErrorNext;
    /** The error message count.
     * This is incremented every time an error is raised.  */
    uint32_t volatile               cErrors;

    /** The runtime error message count.
     * This is incremented every time a runtime error is raised.  */
    uint32_t volatile               cRuntimeErrors;
    /** List of registered error callbacks. */
    PVMATRUNTIMEERROR               pAtRuntimeError;
    /** List of registered error callbacks. */
    PVMATRUNTIMEERROR              *ppAtRuntimeErrorNext;

    /** @name Generic Halt data
     * @{
     */
    /** The current halt method.
     * Can be selected by CFGM option 'VM/HaltMethod'. */
    VMHALTMETHOD                    enmHaltMethod;
    /** The index into g_aHaltMethods of the current halt method. */
    uint32_t volatile               iHaltMethod;
    /** @} */

    /** @todo Do NOT add new members here or reuse the current, we need to store the config for
     *  each halt method separately because we're racing on SMP guest rigs. */
    union
    {
       /**
        * Method 1 & 2 - Block whenever possible, and when lagging behind
        * switch to spinning with regular blocking every 5-200ms (defaults)
        * depending on the accumulated lag. The blocking interval is adjusted
        * with the average oversleeping of the last 64 times.
        *
        * The difference between 1 and 2 is that we use native absolute
        * time APIs for the blocking instead of the millisecond based IPRT
        * interface.
        */
        struct
        {
            /** The max interval without blocking (when spinning). */
            uint32_t                u32MinBlockIntervalCfg;
            /** The minimum interval between blocking (when spinning). */
            uint32_t                u32MaxBlockIntervalCfg;
            /** The value to divide the current lag by to get the raw blocking interval (when spinning). */
            uint32_t                u32LagBlockIntervalDivisorCfg;
            /** When to start spinning (lag / nano secs). */
            uint32_t                u32StartSpinningCfg;
            /** When to stop spinning (lag / nano secs). */
            uint32_t                u32StopSpinningCfg;
        }                           Method12;

       /**
        * The GVMM manages halted and waiting EMTs.
        */
        struct
        {
            /** The threshold between spinning and blocking. */
            uint32_t                cNsSpinBlockThresholdCfg;
        }                           Global1;
    }                               Halt;

    /** Pointer to the DBGC instance data. */
    void                           *pvDBGC;

    /** TLS index for the VMINTUSERPERVMCPU pointer. */
    RTTLS                           idxTLS;

    /** The VM name. (Set after the config constructure has been called.) */
    char                           *pszName;
    /** The VM UUID. (Set after the config constructure has been called.) */
    RTUUID                          Uuid;
} VMINTUSERPERVM;
# ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(VMINTUSERPERVM, StatReqAllocNew, 8);
# endif

/** Pointer to the VM internal data kept in the UVM. */
typedef VMINTUSERPERVM *PVMINTUSERPERVM;


/**
 * VMCPU internal data kept in the UVM.
 *
 * Almost a copy of VMINTUSERPERVM. Separate data properly later on.
 */
typedef struct VMINTUSERPERVMCPU
{
    /** Head of the normal request queue. Atomic. */
    volatile PVMREQ                 pNormalReqs;
    /** Head of the priority request queue. Atomic. */
    volatile PVMREQ                 pPriorityReqs;

    /** The handle to the EMT thread. */
    RTTHREAD                        ThreadEMT;
    /** The native of the EMT thread. */
    RTNATIVETHREAD                  NativeThreadEMT;
    /** Wait event semaphore. */
    RTSEMEVENT                      EventSemWait;
    /** Wait/Idle indicator. */
    bool volatile                   fWait;
    /** Set if we've been thru vmR3Destroy and decremented the active EMT count
     *  already. */
    bool volatile                   fBeenThruVmDestroy;
    /** Align the next bit. */
    bool                            afAlignment[HC_ARCH_BITS == 32 ? 2 : 6];

    /** @name Generic Halt data
     * @{
     */
    /** The average time (ns) between two halts in the last second. (updated once per second) */
    uint32_t                        HaltInterval;
    /** The average halt frequency for the last second. (updated once per second) */
    uint32_t                        HaltFrequency;
    /** The number of halts in the current period. */
    uint32_t                        cHalts;
    uint32_t                        padding; /**< alignment padding. */
    /** When we started counting halts in cHalts (RTTimeNanoTS). */
    uint64_t                        u64HaltsStartTS;
    /** @} */

    /** Union containing data and config for the different halt algorithms. */
    union
    {
       /**
        * Method 1 & 2 - Block whenever possible, and when lagging behind
        * switch to spinning with regular blocking every 5-200ms (defaults)
        * depending on the accumulated lag. The blocking interval is adjusted
        * with the average oversleeping of the last 64 times.
        *
        * The difference between 1 and 2 is that we use native absolute
        * time APIs for the blocking instead of the millisecond based IPRT
        * interface.
        */
        struct
        {
            /** How many times we've blocked while cBlockedNS and cBlockedTooLongNS has been accumulating. */
            uint32_t                cBlocks;
            /** Align the next member. */
            uint32_t                u32Alignment;
            /** Avg. time spend oversleeping when blocking. (Re-calculated every so often.) */
            uint64_t                cNSBlockedTooLongAvg;
            /** Total time spend oversleeping when blocking. */
            uint64_t                cNSBlockedTooLong;
            /** Total time spent blocking. */
            uint64_t                cNSBlocked;
            /** The timestamp (RTTimeNanoTS) of the last block. */
            uint64_t                u64LastBlockTS;

            /** When we started spinning relentlessly in order to catch up some of the oversleeping.
             * This is 0 when we're not spinning. */
            uint64_t                u64StartSpinTS;
        }                           Method12;

# if 0
       /**
        * Method 3 & 4 - Same as method 1 & 2 respectivly, except that we
        * sprinkle it with yields.
        */
       struct
       {
           /** How many times we've blocked while cBlockedNS and cBlockedTooLongNS has been accumulating. */
           uint32_t                 cBlocks;
           /** Avg. time spend oversleeping when blocking. (Re-calculated every so often.) */
           uint64_t                 cBlockedTooLongNSAvg;
           /** Total time spend oversleeping when blocking. */
           uint64_t                 cBlockedTooLongNS;
           /** Total time spent blocking. */
           uint64_t                 cBlockedNS;
           /** The timestamp (RTTimeNanoTS) of the last block. */
           uint64_t                 u64LastBlockTS;

           /** How many times we've yielded while cBlockedNS and cBlockedTooLongNS has been accumulating. */
           uint32_t                 cYields;
           /** Avg. time spend oversleeping when yielding. */
           uint32_t                 cYieldTooLongNSAvg;
           /** Total time spend oversleeping when yielding. */
           uint64_t                 cYieldTooLongNS;
           /** Total time spent yielding. */
           uint64_t                 cYieldedNS;
           /** The timestamp (RTTimeNanoTS) of the last block. */
           uint64_t                 u64LastYieldTS;

           /** When we started spinning relentlessly in order to catch up some of the oversleeping. */
           uint64_t                 u64StartSpinTS;
       }                            Method34;
# endif
    }                               Halt;

    /** Profiling the halted state; yielding vs blocking.
     * @{ */
    STAMPROFILE                     StatHaltYield;
    STAMPROFILE                     StatHaltBlock;
    STAMPROFILE                     StatHaltBlockOverslept;
    STAMPROFILE                     StatHaltBlockInsomnia;
    STAMPROFILE                     StatHaltBlockOnTime;
    STAMPROFILE                     StatHaltTimers;
    STAMPROFILE                     StatHaltPoll;
    /** @} */
} VMINTUSERPERVMCPU;
AssertCompileMemberAlignment(VMINTUSERPERVMCPU, u64HaltsStartTS, 8);
AssertCompileMemberAlignment(VMINTUSERPERVMCPU, Halt.Method12.cNSBlockedTooLongAvg, 8);
AssertCompileMemberAlignment(VMINTUSERPERVMCPU, StatHaltYield, 8);

/** Pointer to the VM internal data kept in the UVM. */
typedef VMINTUSERPERVMCPU *PVMINTUSERPERVMCPU;

#endif /* IN_RING3 */

RT_C_DECLS_BEGIN

DECLCALLBACK(int)   vmR3EmulationThread(RTTHREAD ThreadSelf, void *pvArg);
int                 vmR3SetHaltMethodU(PUVM pUVM, VMHALTMETHOD enmHaltMethod);
DECLCALLBACK(int)   vmR3Destroy(PVM pVM);
DECLCALLBACK(void)  vmR3SetErrorUV(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list *args);
void                vmSetErrorCopy(PVM pVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list args);
DECLCALLBACK(int)   vmR3SetRuntimeError(PVM pVM, uint32_t fFlags, const char *pszErrorId, char *pszMessage);
DECLCALLBACK(int)   vmR3SetRuntimeErrorV(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list *pVa);
void                vmSetRuntimeErrorCopy(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va);
void                vmR3SetTerminated(PVM pVM);

RT_C_DECLS_END


/** @} */

#endif /* !VMM_INCLUDED_SRC_include_VMInternal_h */

