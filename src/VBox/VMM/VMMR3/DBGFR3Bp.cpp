/* $Id: DBGFR3Bp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Breakpoint Management.
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


/** @page pg_dbgf_bp            DBGF - The Debugger Facility, Breakpoint Management
 *
 * The debugger facilities breakpoint managers purpose is to efficiently manage
 * large amounts of breakpoints for various use cases like dtrace like operations
 * or execution flow tracing for instance. Especially execution flow tracing can
 * require thousands of breakpoints which need to be managed efficiently to not slow
 * down guest operation too much. Before the rewrite starting end of 2020, DBGF could
 * only handle 32 breakpoints (+ 4 hardware assisted breakpoints). The new
 * manager is supposed to be able to handle up to one million breakpoints.
 *
 * @see grp_dbgf
 *
 *
 * @section sec_dbgf_bp_owner   Breakpoint owners
 *
 * A single breakpoint owner has a mandatory ring-3 callback and an optional ring-0
 * callback assigned which is called whenever a breakpoint with the owner assigned is hit.
 * The common part of the owner is managed by a single table mapped into both ring-0
 * and ring-3 and the handle being the index into the table. This allows resolving
 * the handle to the internal structure efficiently. Searching for a free entry is
 * done using a bitmap indicating free and occupied entries. For the optional
 * ring-0 owner part there is a separate ring-0 only table for security reasons.
 *
 * The callback of the owner can be used to gather and log guest state information
 * and decide whether to continue guest execution or stop and drop into the debugger.
 * Breakpoints which don't have an owner assigned will always drop the VM right into
 * the debugger.
 *
 *
 * @section sec_dbgf_bp_bps     Breakpoints
 *
 * Breakpoints are referenced by an opaque handle which acts as an index into a global table
 * mapped into ring-3 and ring-0. Each entry contains the necessary state to manage the breakpoint
 * like trigger conditions, type, owner, etc. If an owner is given an optional opaque user argument
 * can be supplied which is passed in the respective owner callback. For owners with ring-0 callbacks
 * a dedicated ring-0 table is held saving possible ring-0 user arguments.
 *
 * To keep memory consumption under control and still support large amounts of
 * breakpoints the table is split into fixed sized chunks and the chunk index and index
 * into the chunk can be derived from the handle with only a few logical operations.
 *
 *
 * @section sec_dbgf_bp_resolv  Resolving breakpoint addresses
 *
 * Whenever a \#BP(0) event is triggered DBGF needs to decide whether the event originated
 * from within the guest or whether a DBGF breakpoint caused it. This has to happen as fast
 * as possible. The following scheme is employed to achieve this:
 *
 * @verbatim
 *                       7   6   5   4   3   2   1   0
 *                     +---+---+---+---+---+---+---+---+
 *                     |   |   |   |   |   |   |   |   | BP address
 *                     +---+---+---+---+---+---+---+---+
 *                      \_____________________/ \_____/
 *                                 |               |
 *                                 |               +---------------+
 *                                 |                               |
 *    BP table                     |                               v
 * +------------+                  |                         +-----------+
 * |   hBp 0    |                  |                    X <- | 0 | xxxxx |
 * |   hBp 1    | <----------------+------------------------ | 1 | hBp 1 |
 * |            |                  |                    +--- | 2 | idxL2 |
 * |   hBp <m>  | <---+            v                    |    |...|  ...  |
 * |            |     |      +-----------+              |    |...|  ...  |
 * |            |     |      |           |              |    |...|  ...  |
 * |   hBp <n>  | <-+ +----- | +> leaf   |              |    |     .     |
 * |            |   |        | |         |              |    |     .     |
 * |            |   |        | + root +  | <------------+    |     .     |
 * |            |   |        |        |  |                   +-----------+
 * |            |   +------- |   leaf<+  |                     L1: 65536
 * |     .      |            |     .     |
 * |     .      |            |     .     |
 * |     .      |            |     .     |
 * +------------+            +-----------+
 *                            L2 idx BST
 * @endverbatim
 *
 *     -# Take the lowest 16 bits of the breakpoint address and use it as an direct index
 *        into the L1 table. The L1 table is contiguous and consists of 4 byte entries
 *        resulting in 256KiB of memory used. The topmost 4 bits indicate how to proceed
 *        and the meaning of the remaining 28bits depends on the topmost 4 bits:
 *            - A 0 type entry means no breakpoint is registered with the matching lowest 16bits,
 *              so forward the event to the guest.
 *            - A 1 in the topmost 4 bits means that the remaining 28bits directly denote a breakpoint
 *              handle which can be resolved by extracting the chunk index and index into the chunk
 *              of the global breakpoint table. If the address matches the breakpoint is processed
 *              according to the configuration. Otherwise the breakpoint is again forwarded to the guest.
 *            - A 2 in the topmost 4 bits means that there are multiple breakpoints registered
 *              matching the lowest 16bits and the search must continue in the L2 table with the
 *              remaining 28bits acting as an index into the L2 table indicating the search root.
 *     -# The L2 table consists of multiple index based binary search trees, there is one for each reference
 *        from the L1 table. The key for the table are the upper 6 bytes of the breakpoint address
 *        used for searching. This tree is traversed until either a matching address is found and
 *        the breakpoint is being processed or again forwarded to the guest if it isn't successful.
 *        Each entry in the L2 table is 16 bytes big and densly packed to avoid excessive memory usage.
 *
 * @section sec_dbgf_bp_ioport  Handling I/O port breakpoints
 *
 * Because of the limited amount of I/O ports being available (65536) a single table with 65536 entries,
 * each 4 byte big will be allocated. This amounts to 256KiB of memory being used additionally as soon as
 * an I/O breakpoint is enabled. The entries contain the breakpoint handle directly allowing only one breakpoint
 * per port right now, which is something we accept as a limitation right now to keep things relatively simple.
 * When there is at least one I/O breakpoint active IOM will be notified and it will afterwards call the DBGF API
 * whenever the guest does an I/O port access to decide whether a breakpoint was hit. This keeps the overhead small
 * when there is no I/O port breakpoint enabled.
 *
 * @section sec_dbgf_bp_note    Random thoughts and notes for the implementation
 *
 * - The assumption for this approach is that the lowest 16bits of the breakpoint address are
 *   hopefully the ones being the most varying ones across breakpoints so the traversal
 *   can skip the L2 table in most of the cases. Even if the L2 table must be taken the
 *   individual trees should be quite shallow resulting in low overhead when walking it
 *   (though only real world testing can assert this assumption).
 * - Index based tables and trees are used instead of pointers because the tables
 *   are always mapped into ring-0 and ring-3 with different base addresses.
 * - Efficent breakpoint allocation is done by having a global bitmap indicating free
 *   and occupied breakpoint entries. Same applies for the L2 BST table.
 * - Special care must be taken when modifying the L1 and L2 tables as other EMTs
 *   might still access it (want to try a lockless approach first using
 *   atomic updates, have to resort to locking if that turns out to be too difficult).
 * - Each BP entry is supposed to be 64 byte big and each chunk should contain 65536
 *   breakpoints which results in 4MiB for each chunk plus the allocation bitmap.
 * - ring-0 has to take special care when traversing the L2 BST to not run into cycles
 *   and do strict bounds checking before accessing anything. The L1 and L2 table
 *   are written to from ring-3 only. Same goes for the breakpoint table with the
 *   exception being the opaque user argument for ring-0 which is stored in ring-0 only
 *   memory.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/hm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>

#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/mem.h>

#include "DBGFInline.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
RT_C_DECLS_END


/**
 * Initialize the breakpoint mangement.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
DECLHIDDEN(int) dbgfR3BpInit(PUVM pUVM)
{
    PVM pVM = pUVM->pVM;

    //pUVM->dbgf.s.paBpOwnersR3       = NULL;
    //pUVM->dbgf.s.pbmBpOwnersAllocR3 = NULL;

    /* Init hardware breakpoint states. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); i++)
    {
        PDBGFBPHW pHwBp = &pVM->dbgf.s.aHwBreakpoints[i];

        AssertCompileSize(DBGFBP, sizeof(uint32_t));
        pHwBp->hBp      = NIL_DBGFBP;
        //pHwBp->fEnabled = false;
    }

    /* Now the global breakpoint table chunks. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpChunks); i++)
    {
        PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[i];

        //pBpChunk->pBpBaseR3 = NULL;
        //pBpChunk->pbmAlloc  = NULL;
        //pBpChunk->cBpsFree  = 0;
        pBpChunk->idChunk = DBGF_BP_CHUNK_ID_INVALID; /* Not allocated. */
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpL2TblChunks); i++)
    {
        PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[i];

        //pL2Chunk->pL2BaseR3 = NULL;
        //pL2Chunk->pbmAlloc  = NULL;
        //pL2Chunk->cFree     = 0;
        pL2Chunk->idChunk = DBGF_BP_CHUNK_ID_INVALID; /* Not allocated. */
    }

    //pUVM->dbgf.s.paBpLocL1R3     = NULL;
    //pUVM->dbgf.s.paBpLocPortIoR3 = NULL;
    pUVM->dbgf.s.hMtxBpL2Wr = NIL_RTSEMFASTMUTEX;
    return RTSemFastMutexCreate(&pUVM->dbgf.s.hMtxBpL2Wr);
}


/**
 * Terminates the breakpoint mangement.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
DECLHIDDEN(int) dbgfR3BpTerm(PUVM pUVM)
{
    if (pUVM->dbgf.s.pbmBpOwnersAllocR3)
    {
        RTMemFree((void *)pUVM->dbgf.s.pbmBpOwnersAllocR3);
        pUVM->dbgf.s.pbmBpOwnersAllocR3 = NULL;
    }

    /* Free all allocated chunk bitmaps (the chunks itself are destroyed during ring-0 VM destruction). */
    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpChunks); i++)
    {
        PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[i];

        if (pBpChunk->idChunk != DBGF_BP_CHUNK_ID_INVALID)
        {
            AssertPtr(pBpChunk->pbmAlloc);
            RTMemFree((void *)pBpChunk->pbmAlloc);
            pBpChunk->pbmAlloc = NULL;
            pBpChunk->idChunk = DBGF_BP_CHUNK_ID_INVALID;
        }
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpL2TblChunks); i++)
    {
        PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[i];

        if (pL2Chunk->idChunk != DBGF_BP_CHUNK_ID_INVALID)
        {
            AssertPtr(pL2Chunk->pbmAlloc);
            RTMemFree((void *)pL2Chunk->pbmAlloc);
            pL2Chunk->pbmAlloc = NULL;
            pL2Chunk->idChunk = DBGF_BP_CHUNK_ID_INVALID;
        }
    }

    if (pUVM->dbgf.s.hMtxBpL2Wr != NIL_RTSEMFASTMUTEX)
    {
        RTSemFastMutexDestroy(pUVM->dbgf.s.hMtxBpL2Wr);
        pUVM->dbgf.s.hMtxBpL2Wr = NIL_RTSEMFASTMUTEX;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpInitEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF(pvUser);

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * The initialization will be done on EMT(0). It is possible that multiple
     * initialization attempts are done because dbgfR3BpEnsureInit() can be called
     * from racing non EMT threads when trying to set a breakpoint for the first time.
     * Just fake success if the L1 is already present which means that a previous rendezvous
     * successfully initialized the breakpoint manager.
     */
    PUVM pUVM = pVM->pUVM;
    if (   pVCpu->idCpu == 0
        && !pUVM->dbgf.s.paBpLocL1R3)
    {
        if (!SUPR3IsDriverless())
        {
            DBGFBPINITREQ Req;
            Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
            Req.Hdr.cbReq    = sizeof(Req);
            Req.paBpLocL1R3  = NULL;
            int rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_DBGF_BP_INIT, 0 /*u64Arg*/, &Req.Hdr);
            AssertLogRelMsgRCReturn(rc, ("VMMR0_DO_DBGF_BP_INIT failed: %Rrc\n", rc), rc);
            pUVM->dbgf.s.paBpLocL1R3 = Req.paBpLocL1R3;
        }
        else
        {
            /* Driverless: Do dbgfR0BpInitWorker here, ring-3 style. */
            uint32_t const cbL1Loc = RT_ALIGN_32(UINT16_MAX * sizeof(uint32_t), HOST_PAGE_SIZE);
            pUVM->dbgf.s.paBpLocL1R3 = (uint32_t *)RTMemPageAllocZ(cbL1Loc);
            AssertLogRelMsgReturn(pUVM->dbgf.s.paBpLocL1R3, ("cbL1Loc=%#x\n", cbL1Loc), VERR_NO_PAGE_MEMORY);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Ensures that the breakpoint manager is fully initialized.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 *
 * @thread Any thread.
 */
static int dbgfR3BpEnsureInit(PUVM pUVM)
{
    /* If the L1 lookup table is allocated initialization succeeded before. */
    if (RT_LIKELY(pUVM->dbgf.s.paBpLocL1R3))
        return VINF_SUCCESS;

    /* Gather all EMTs and call into ring-0 to initialize the breakpoint manager. */
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpInitEmtWorker, NULL /*pvUser*/);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpPortIoInitEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF(pvUser);

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * The initialization will be done on EMT(0). It is possible that multiple
     * initialization attempts are done because dbgfR3BpPortIoEnsureInit() can be called
     * from racing non EMT threads when trying to set a breakpoint for the first time.
     * Just fake success if the L1 is already present which means that a previous rendezvous
     * successfully initialized the breakpoint manager.
     */
    PUVM pUVM = pVM->pUVM;
    if (   pVCpu->idCpu == 0
        && !pUVM->dbgf.s.paBpLocPortIoR3)
    {
        if (!SUPR3IsDriverless())
        {
            DBGFBPINITREQ Req;
            Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
            Req.Hdr.cbReq    = sizeof(Req);
            Req.paBpLocL1R3  = NULL;
            int rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_DBGF_BP_PORTIO_INIT, 0 /*u64Arg*/, &Req.Hdr);
            AssertLogRelMsgRCReturn(rc, ("VMMR0_DO_DBGF_BP_PORTIO_INIT failed: %Rrc\n", rc), rc);
            pUVM->dbgf.s.paBpLocPortIoR3 = Req.paBpLocL1R3;
        }
        else
        {
            /* Driverless: Do dbgfR0BpPortIoInitWorker here, ring-3 style. */
            uint32_t const cbPortIoLoc = RT_ALIGN_32(UINT16_MAX * sizeof(uint32_t), HOST_PAGE_SIZE);
            pUVM->dbgf.s.paBpLocPortIoR3 = (uint32_t *)RTMemPageAllocZ(cbPortIoLoc);
            AssertLogRelMsgReturn(pUVM->dbgf.s.paBpLocPortIoR3, ("cbPortIoLoc=%#x\n", cbPortIoLoc), VERR_NO_PAGE_MEMORY);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Ensures that the breakpoint manager is initialized to handle I/O port breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 *
 * @thread Any thread.
 */
static int dbgfR3BpPortIoEnsureInit(PUVM pUVM)
{
    /* If the L1 lookup table is allocated initialization succeeded before. */
    if (RT_LIKELY(pUVM->dbgf.s.paBpLocPortIoR3))
        return VINF_SUCCESS;

    /* Ensure that the breakpoint manager is initialized. */
    int rc = dbgfR3BpEnsureInit(pUVM);
    if (RT_FAILURE(rc))
        return rc;

    /* Gather all EMTs and call into ring-0 to initialize the breakpoint manager. */
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpPortIoInitEmtWorker, NULL /*pvUser*/);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpOwnerInitEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF(pvUser);

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * The initialization will be done on EMT(0). It is possible that multiple
     * initialization attempts are done because dbgfR3BpOwnerEnsureInit() can be called
     * from racing non EMT threads when trying to create a breakpoint owner for the first time.
     * Just fake success if the pointers are initialized already, meaning that a previous rendezvous
     * successfully initialized the breakpoint owner table.
     */
    int rc = VINF_SUCCESS;
    PUVM pUVM = pVM->pUVM;
    if (   pVCpu->idCpu == 0
        && !pUVM->dbgf.s.pbmBpOwnersAllocR3)
    {
        AssertCompile(!(DBGF_BP_OWNER_COUNT_MAX % 64));
        pUVM->dbgf.s.pbmBpOwnersAllocR3 = RTMemAllocZ(DBGF_BP_OWNER_COUNT_MAX / 8);
        if (pUVM->dbgf.s.pbmBpOwnersAllocR3)
        {
            if (!SUPR3IsDriverless())
            {
                DBGFBPOWNERINITREQ Req;
                Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                Req.Hdr.cbReq    = sizeof(Req);
                Req.paBpOwnerR3  = NULL;
                rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_DBGF_BP_OWNER_INIT, 0 /*u64Arg*/, &Req.Hdr);
                if (RT_SUCCESS(rc))
                {
                    pUVM->dbgf.s.paBpOwnersR3 = (PDBGFBPOWNERINT)Req.paBpOwnerR3;
                    return VINF_SUCCESS;
                }
                AssertLogRelMsgRC(rc, ("VMMR0_DO_DBGF_BP_OWNER_INIT failed: %Rrc\n", rc));
            }
            else
            {
                /* Driverless: Do dbgfR0BpOwnerInitWorker here, ring-3 style. */
                uint32_t const cbBpOwnerR3 = RT_ALIGN_32(DBGF_BP_OWNER_COUNT_MAX * sizeof(DBGFBPOWNERINT), HOST_PAGE_SIZE);
                pUVM->dbgf.s.paBpLocPortIoR3 = (uint32_t *)RTMemPageAllocZ(cbBpOwnerR3);
                if (pUVM->dbgf.s.paBpLocPortIoR3)
                    return VINF_SUCCESS;
                AssertLogRelMsgFailed(("cbBpOwnerR3=%#x\n", cbBpOwnerR3));
                rc = VERR_NO_PAGE_MEMORY;
            }

            RTMemFree((void *)pUVM->dbgf.s.pbmBpOwnersAllocR3);
            pUVM->dbgf.s.pbmBpOwnersAllocR3 = NULL;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Ensures that the breakpoint manager is fully initialized.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 *
 * @thread Any thread.
 */
static int dbgfR3BpOwnerEnsureInit(PUVM pUVM)
{
    /* If the allocation bitmap is allocated initialization succeeded before. */
    if (RT_LIKELY(pUVM->dbgf.s.pbmBpOwnersAllocR3))
        return VINF_SUCCESS;

    /* Gather all EMTs and call into ring-0 to initialize the breakpoint manager. */
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpOwnerInitEmtWorker, NULL /*pvUser*/);
}


/**
 * Retains the given breakpoint owner handle for use.
 *
 * @returns VBox status code.
 * @retval VERR_INVALID_HANDLE if the given breakpoint owner handle is invalid.
 * @param   pUVM                The user mode VM handle.
 * @param   hBpOwner            The breakpoint owner handle to retain, NIL_DBGFOWNER is accepted without doing anything.
 * @param   fIo                 Flag whether the owner must have the I/O handler set because it used by an I/O breakpoint.
 */
DECLINLINE(int) dbgfR3BpOwnerRetain(PUVM pUVM, DBGFBPOWNER hBpOwner, bool fIo)
{
    if (hBpOwner == NIL_DBGFBPOWNER)
        return VINF_SUCCESS;

    PDBGFBPOWNERINT pBpOwner = dbgfR3BpOwnerGetByHnd(pUVM, hBpOwner);
    if (pBpOwner)
    {
        AssertReturn (   (   fIo
                          && pBpOwner->pfnBpIoHitR3)
                      ||  (   !fIo
                           && pBpOwner->pfnBpHitR3),
                      VERR_INVALID_HANDLE);
        ASMAtomicIncU32(&pBpOwner->cRefs);
        return VINF_SUCCESS;
    }

    return VERR_INVALID_HANDLE;
}


/**
 * Releases the given breakpoint owner handle.
 *
 * @returns VBox status code.
 * @retval VERR_INVALID_HANDLE if the given breakpoint owner handle is invalid.
 * @param   pUVM                The user mode VM handle.
 * @param   hBpOwner            The breakpoint owner handle to retain, NIL_DBGFOWNER is accepted without doing anything.
 */
DECLINLINE(int) dbgfR3BpOwnerRelease(PUVM pUVM, DBGFBPOWNER hBpOwner)
{
    if (hBpOwner == NIL_DBGFBPOWNER)
        return VINF_SUCCESS;

    PDBGFBPOWNERINT pBpOwner = dbgfR3BpOwnerGetByHnd(pUVM, hBpOwner);
    if (pBpOwner)
    {
        Assert(pBpOwner->cRefs > 1);
        ASMAtomicDecU32(&pBpOwner->cRefs);
        return VINF_SUCCESS;
    }

    return VERR_INVALID_HANDLE;
}


/**
 * Returns the internal breakpoint state for the given handle.
 *
 * @returns Pointer to the internal breakpoint state or NULL if the handle is invalid.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to resolve.
 */
DECLINLINE(PDBGFBPINT) dbgfR3BpGetByHnd(PUVM pUVM, DBGFBP hBp)
{
    uint32_t idChunk  = DBGF_BP_HND_GET_CHUNK_ID(hBp);
    uint32_t idxEntry = DBGF_BP_HND_GET_ENTRY(hBp);

    AssertReturn(idChunk < DBGF_BP_CHUNK_COUNT, NULL);
    AssertReturn(idxEntry < DBGF_BP_COUNT_PER_CHUNK, NULL);

    PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[idChunk];
    AssertReturn(pBpChunk->idChunk == idChunk, NULL);
    AssertPtrReturn(pBpChunk->pbmAlloc, NULL);
    AssertReturn(ASMBitTest(pBpChunk->pbmAlloc, idxEntry), NULL);

    return &pBpChunk->pBpBaseR3[idxEntry];
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpChunkAllocEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    uint32_t idChunk = (uint32_t)(uintptr_t)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    AssertReturn(idChunk < DBGF_BP_CHUNK_COUNT, VERR_DBGF_BP_IPE_1);

    PUVM pUVM = pVM->pUVM;
    PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[idChunk];

    AssertReturn(   pBpChunk->idChunk == DBGF_BP_CHUNK_ID_INVALID
                 || pBpChunk->idChunk == idChunk,
                 VERR_DBGF_BP_IPE_2);

    /*
     * The initialization will be done on EMT(0). It is possible that multiple
     * allocation attempts are done when multiple racing non EMT threads try to
     * allocate a breakpoint and a new chunk needs to be allocated.
     * Ignore the request and succeed if the chunk is allocated meaning that a
     * previous rendezvous successfully allocated the chunk.
     */
    int rc = VINF_SUCCESS;
    if (   pVCpu->idCpu == 0
        && pBpChunk->idChunk == DBGF_BP_CHUNK_ID_INVALID)
    {
        /* Allocate the bitmap first so we can skip calling into VMMR0 if it fails. */
        AssertCompile(!(DBGF_BP_COUNT_PER_CHUNK % 64));
        void *pbmAlloc = RTMemAllocZ(DBGF_BP_COUNT_PER_CHUNK / 8);
        if (RT_LIKELY(pbmAlloc))
        {
            if (!SUPR3IsDriverless())
            {
                DBGFBPCHUNKALLOCREQ Req;
                Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                Req.Hdr.cbReq    = sizeof(Req);
                Req.idChunk      = idChunk;
                Req.pChunkBaseR3 = NULL;
                rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_DBGF_BP_CHUNK_ALLOC, 0 /*u64Arg*/, &Req.Hdr);
                if (RT_SUCCESS(rc))
                    pBpChunk->pBpBaseR3 = (PDBGFBPINT)Req.pChunkBaseR3;
                else
                    AssertLogRelMsgRC(rc, ("VMMR0_DO_DBGF_BP_CHUNK_ALLOC failed: %Rrc\n", rc));
            }
            else
            {
                /* Driverless: Do dbgfR0BpChunkAllocWorker here, ring-3 style. */
                uint32_t const cbShared = RT_ALIGN_32(DBGF_BP_COUNT_PER_CHUNK * sizeof(DBGFBPINT), HOST_PAGE_SIZE);
                pBpChunk->pBpBaseR3 = (PDBGFBPINT)RTMemPageAllocZ(cbShared);
                AssertLogRelMsgStmt(pBpChunk->pBpBaseR3, ("cbShared=%#x\n", cbShared), rc = VERR_NO_PAGE_MEMORY);
            }
            if (RT_SUCCESS(rc))
            {
                pBpChunk->pbmAlloc  = (void volatile *)pbmAlloc;
                pBpChunk->cBpsFree  = DBGF_BP_COUNT_PER_CHUNK;
                pBpChunk->idChunk   = idChunk;
                return VINF_SUCCESS;
            }

            RTMemFree(pbmAlloc);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Tries to allocate the given chunk which requires an EMT rendezvous.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idChunk             The chunk to allocate.
 *
 * @thread Any thread.
 */
DECLINLINE(int) dbgfR3BpChunkAlloc(PUVM pUVM, uint32_t idChunk)
{
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpChunkAllocEmtWorker, (void *)(uintptr_t)idChunk);
}


/**
 * Tries to allocate a new breakpoint of the given type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hOwner              The owner handle, NIL_DBGFBPOWNER if none assigned.
 * @param   pvUser              Opaque user data passed in the owner callback.
 * @param   enmType             Breakpoint type to allocate.
 * @param   fFlags              Flags assoicated with the allocated breakpoint.
 * @param   iHitTrigger         The hit count at which the breakpoint start triggering.
 *                              Use 0 (or 1) if it's gonna trigger at once.
 * @param   iHitDisable         The hit count which disables the breakpoint.
 *                              Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp                Where to return the opaque breakpoint handle on success.
 * @param   ppBp                Where to return the pointer to the internal breakpoint state on success.
 *
 * @thread Any thread.
 */
static int dbgfR3BpAlloc(PUVM pUVM, DBGFBPOWNER hOwner, void *pvUser, DBGFBPTYPE enmType,
                         uint16_t fFlags, uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp,
                         PDBGFBPINT *ppBp)
{
    bool fIo =    enmType == DBGFBPTYPE_PORT_IO
               || enmType == DBGFBPTYPE_MMIO;
    int rc = dbgfR3BpOwnerRetain(pUVM, hOwner, fIo);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Search for a chunk having a free entry, allocating new chunks
     * if the encountered ones are full.
     *
     * This can be called from multiple threads at the same time so special care
     * has to be taken to not require any locking here.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpChunks); i++)
    {
        PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[i];

        uint32_t idChunk  = ASMAtomicReadU32(&pBpChunk->idChunk);
        if (idChunk == DBGF_BP_CHUNK_ID_INVALID)
        {
            rc = dbgfR3BpChunkAlloc(pUVM, i);
            if (RT_FAILURE(rc))
            {
                LogRel(("DBGF/Bp: Allocating new breakpoint table chunk failed with %Rrc\n", rc));
                break;
            }

            idChunk = ASMAtomicReadU32(&pBpChunk->idChunk);
            Assert(idChunk == i);
        }

        /** @todo Optimize with some hinting if this turns out to be too slow. */
        for (;;)
        {
            uint32_t cBpsFree = ASMAtomicReadU32(&pBpChunk->cBpsFree);
            if (cBpsFree)
            {
                /*
                 * Scan the associated bitmap for a free entry, if none can be found another thread
                 * raced us and we go to the next chunk.
                 */
                int32_t iClr = ASMBitFirstClear(pBpChunk->pbmAlloc, DBGF_BP_COUNT_PER_CHUNK);
                if (iClr != -1)
                {
                    /*
                     * Try to allocate, we could get raced here as well. In that case
                     * we try again.
                     */
                    if (!ASMAtomicBitTestAndSet(pBpChunk->pbmAlloc, iClr))
                    {
                        /* Success, immediately mark as allocated, initialize the breakpoint state and return. */
                        ASMAtomicDecU32(&pBpChunk->cBpsFree);

                        PDBGFBPINT pBp = &pBpChunk->pBpBaseR3[iClr];
                        pBp->Pub.cHits = 0;
                        pBp->Pub.iHitTrigger   = iHitTrigger;
                        pBp->Pub.iHitDisable   = iHitDisable;
                        pBp->Pub.hOwner        = hOwner;
                        pBp->Pub.u16Type       = DBGF_BP_PUB_MAKE_TYPE(enmType);
                        pBp->Pub.fFlags        = fFlags & ~DBGF_BP_F_ENABLED; /* The enabled flag is handled in the respective APIs. */
                        pBp->pvUserR3          = pvUser;

                        /** @todo Owner handling (reference and call ring-0 if it has an ring-0 callback). */

                        *phBp = DBGF_BP_HND_CREATE(idChunk, iClr);
                        *ppBp = pBp;
                        return VINF_SUCCESS;
                    }
                    /* else Retry with another spot. */
                }
                else /* no free entry in bitmap, go to the next chunk */
                    break;
            }
            else /* !cBpsFree, go to the next chunk */
                break;
        }
    }

    rc = dbgfR3BpOwnerRelease(pUVM, hOwner); AssertRC(rc);
    return VERR_DBGF_NO_MORE_BP_SLOTS;
}


/**
 * Frees the given breakpoint handle.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to free.
 * @param   pBp                 The internal breakpoint state pointer.
 */
static void dbgfR3BpFree(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    uint32_t idChunk  = DBGF_BP_HND_GET_CHUNK_ID(hBp);
    uint32_t idxEntry = DBGF_BP_HND_GET_ENTRY(hBp);

    AssertReturnVoid(idChunk < DBGF_BP_CHUNK_COUNT);
    AssertReturnVoid(idxEntry < DBGF_BP_COUNT_PER_CHUNK);

    PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[idChunk];
    AssertPtrReturnVoid(pBpChunk->pbmAlloc);
    AssertReturnVoid(ASMBitTest(pBpChunk->pbmAlloc, idxEntry));

    /** @todo Need a trip to Ring-0 if an owner is assigned with a Ring-0 part to clear the breakpoint. */
    int rc = dbgfR3BpOwnerRelease(pUVM, pBp->Pub.hOwner); AssertRC(rc); RT_NOREF(rc);
    memset(pBp, 0, sizeof(*pBp));

    ASMAtomicBitClear(pBpChunk->pbmAlloc, idxEntry);
    ASMAtomicIncU32(&pBpChunk->cBpsFree);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpL2TblChunkAllocEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    uint32_t idChunk = (uint32_t)(uintptr_t)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    AssertReturn(idChunk < DBGF_BP_L2_TBL_CHUNK_COUNT, VERR_DBGF_BP_IPE_1);

    PUVM pUVM = pVM->pUVM;
    PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[idChunk];

    AssertReturn(   pL2Chunk->idChunk == DBGF_BP_L2_IDX_CHUNK_ID_INVALID
                 || pL2Chunk->idChunk == idChunk,
                 VERR_DBGF_BP_IPE_2);

    /*
     * The initialization will be done on EMT(0). It is possible that multiple
     * allocation attempts are done when multiple racing non EMT threads try to
     * allocate a breakpoint and a new chunk needs to be allocated.
     * Ignore the request and succeed if the chunk is allocated meaning that a
     * previous rendezvous successfully allocated the chunk.
     */
    int rc = VINF_SUCCESS;
    if (   pVCpu->idCpu == 0
        && pL2Chunk->idChunk == DBGF_BP_L2_IDX_CHUNK_ID_INVALID)
    {
        /* Allocate the bitmap first so we can skip calling into VMMR0 if it fails. */
        AssertCompile(!(DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK % 64));
        void *pbmAlloc = RTMemAllocZ(DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK / 8);
        if (RT_LIKELY(pbmAlloc))
        {
            if (!SUPR3IsDriverless())
            {
                DBGFBPL2TBLCHUNKALLOCREQ Req;
                Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                Req.Hdr.cbReq    = sizeof(Req);
                Req.idChunk      = idChunk;
                Req.pChunkBaseR3 = NULL;
                rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_DBGF_BP_L2_TBL_CHUNK_ALLOC, 0 /*u64Arg*/, &Req.Hdr);
                if (RT_SUCCESS(rc))
                    pL2Chunk->pL2BaseR3 = (PDBGFBPL2ENTRY)Req.pChunkBaseR3;
                else
                    AssertLogRelMsgRC(rc, ("VMMR0_DO_DBGF_BP_L2_TBL_CHUNK_ALLOC failed: %Rrc\n", rc));
            }
            else
            {
                /* Driverless: Do dbgfR0BpL2TblChunkAllocWorker here, ring-3 style. */
                uint32_t const cbTotal = RT_ALIGN_32(DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK * sizeof(DBGFBPL2ENTRY), HOST_PAGE_SIZE);
                pL2Chunk->pL2BaseR3 = (PDBGFBPL2ENTRY)RTMemPageAllocZ(cbTotal);
                AssertLogRelMsgStmt(pL2Chunk->pL2BaseR3, ("cbTotal=%#x\n", cbTotal), rc = VERR_NO_PAGE_MEMORY);
            }
            if (RT_SUCCESS(rc))
            {
                pL2Chunk->pbmAlloc  = (void volatile *)pbmAlloc;
                pL2Chunk->cFree     = DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK;
                pL2Chunk->idChunk   = idChunk;
                return VINF_SUCCESS;
            }

            RTMemFree(pbmAlloc);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Tries to allocate the given L2 table chunk which requires an EMT rendezvous.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idChunk             The chunk to allocate.
 *
 * @thread Any thread.
 */
DECLINLINE(int) dbgfR3BpL2TblChunkAlloc(PUVM pUVM, uint32_t idChunk)
{
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpL2TblChunkAllocEmtWorker, (void *)(uintptr_t)idChunk);
}


/**
 * Tries to allocate a new breakpoint of the given type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pidxL2Tbl           Where to return the L2 table entry index on success.
 * @param   ppL2TblEntry        Where to return the pointer to the L2 table entry on success.
 *
 * @thread Any thread.
 */
static int dbgfR3BpL2TblEntryAlloc(PUVM pUVM, uint32_t *pidxL2Tbl, PDBGFBPL2ENTRY *ppL2TblEntry)
{
    /*
     * Search for a chunk having a free entry, allocating new chunks
     * if the encountered ones are full.
     *
     * This can be called from multiple threads at the same time so special care
     * has to be taken to not require any locking here.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.aBpL2TblChunks); i++)
    {
        PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[i];

        uint32_t idChunk  = ASMAtomicReadU32(&pL2Chunk->idChunk);
        if (idChunk == DBGF_BP_L2_IDX_CHUNK_ID_INVALID)
        {
            int rc = dbgfR3BpL2TblChunkAlloc(pUVM, i);
            if (RT_FAILURE(rc))
            {
                LogRel(("DBGF/Bp: Allocating new breakpoint L2 lookup table chunk failed with %Rrc\n", rc));
                break;
            }

            idChunk = ASMAtomicReadU32(&pL2Chunk->idChunk);
            Assert(idChunk == i);
        }

        /** @todo Optimize with some hinting if this turns out to be too slow. */
        for (;;)
        {
            uint32_t cFree = ASMAtomicReadU32(&pL2Chunk->cFree);
            if (cFree)
            {
                /*
                 * Scan the associated bitmap for a free entry, if none can be found another thread
                 * raced us and we go to the next chunk.
                 */
                int32_t iClr = ASMBitFirstClear(pL2Chunk->pbmAlloc, DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK);
                if (iClr != -1)
                {
                    /*
                     * Try to allocate, we could get raced here as well. In that case
                     * we try again.
                     */
                    if (!ASMAtomicBitTestAndSet(pL2Chunk->pbmAlloc, iClr))
                    {
                        /* Success, immediately mark as allocated, initialize the breakpoint state and return. */
                        ASMAtomicDecU32(&pL2Chunk->cFree);

                        PDBGFBPL2ENTRY pL2Entry = &pL2Chunk->pL2BaseR3[iClr];

                        *pidxL2Tbl    = DBGF_BP_L2_IDX_CREATE(idChunk, iClr);
                        *ppL2TblEntry = pL2Entry;
                        return VINF_SUCCESS;
                    }
                    /* else Retry with another spot. */
                }
                else /* no free entry in bitmap, go to the next chunk */
                    break;
            }
            else /* !cFree, go to the next chunk */
                break;
        }
    }

    return VERR_DBGF_NO_MORE_BP_SLOTS;
}


/**
 * Frees the given breakpoint handle.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idxL2Tbl            The L2 table index to free.
 * @param   pL2TblEntry         The L2 table entry pointer to free.
 */
static void dbgfR3BpL2TblEntryFree(PUVM pUVM, uint32_t idxL2Tbl, PDBGFBPL2ENTRY pL2TblEntry)
{
    uint32_t idChunk  = DBGF_BP_L2_IDX_GET_CHUNK_ID(idxL2Tbl);
    uint32_t idxEntry = DBGF_BP_L2_IDX_GET_ENTRY(idxL2Tbl);

    AssertReturnVoid(idChunk < DBGF_BP_L2_TBL_CHUNK_COUNT);
    AssertReturnVoid(idxEntry < DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK);

    PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[idChunk];
    AssertPtrReturnVoid(pL2Chunk->pbmAlloc);
    AssertReturnVoid(ASMBitTest(pL2Chunk->pbmAlloc, idxEntry));

    memset(pL2TblEntry, 0, sizeof(*pL2TblEntry));

    ASMAtomicBitClear(pL2Chunk->pbmAlloc, idxEntry);
    ASMAtomicIncU32(&pL2Chunk->cFree);
}


/**
 * Sets the enabled flag of the given breakpoint to the given value.
 *
 * @param   pBp                 The breakpoint to set the state.
 * @param   fEnabled            Enabled status.
 */
DECLINLINE(void) dbgfR3BpSetEnabled(PDBGFBPINT pBp, bool fEnabled)
{
    if (fEnabled)
        pBp->Pub.fFlags |= DBGF_BP_F_ENABLED;
    else
        pBp->Pub.fFlags &= ~DBGF_BP_F_ENABLED;
}


/**
 * Assigns a hardware breakpoint state to the given register breakpoint.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross-context VM structure pointer.
 * @param   hBp                 The breakpoint handle to assign.
 * @param   pBp                 The internal breakpoint state.
 *
 * @thread Any thread.
 */
static int dbgfR3BpRegAssign(PVM pVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(pBp->Pub.u.Reg.iReg == UINT8_MAX, VERR_DBGF_BP_IPE_3);

    for (uint8_t i = 0; i < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); i++)
    {
        PDBGFBPHW pHwBp = &pVM->dbgf.s.aHwBreakpoints[i];

        AssertCompileSize(DBGFBP, sizeof(uint32_t));
        if (ASMAtomicCmpXchgU32(&pHwBp->hBp, hBp, NIL_DBGFBP))
        {
            pHwBp->GCPtr    = pBp->Pub.u.Reg.GCPtr;
            pHwBp->fType    = pBp->Pub.u.Reg.fType;
            pHwBp->cb       = pBp->Pub.u.Reg.cb;
            pHwBp->fEnabled = DBGF_BP_PUB_IS_ENABLED(&pBp->Pub);

            pBp->Pub.u.Reg.iReg = i;
            return VINF_SUCCESS;
        }
    }

    return VERR_DBGF_NO_MORE_BP_SLOTS;
}


/**
 * Removes the assigned hardware breakpoint state from the given register breakpoint.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross-context VM structure pointer.
 * @param   hBp                 The breakpoint handle to remove.
 * @param   pBp                 The internal breakpoint state.
 *
 * @thread Any thread.
 */
static int dbgfR3BpRegRemove(PVM pVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(pBp->Pub.u.Reg.iReg < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints), VERR_DBGF_BP_IPE_3);

    PDBGFBPHW pHwBp = &pVM->dbgf.s.aHwBreakpoints[pBp->Pub.u.Reg.iReg];
    AssertReturn(pHwBp->hBp == hBp, VERR_DBGF_BP_IPE_4);
    AssertReturn(!pHwBp->fEnabled, VERR_DBGF_BP_IPE_5);

    pHwBp->GCPtr = 0;
    pHwBp->fType = 0;
    pHwBp->cb    = 0;
    ASMCompilerBarrier();

    ASMAtomicWriteU32(&pHwBp->hBp, NIL_DBGFBP);
    return VINF_SUCCESS;
}


/**
 * Returns the pointer to the L2 table entry from the given index.
 *
 * @returns Current context pointer to the L2 table entry or NULL if the provided index value is invalid.
 * @param   pUVM        The user mode VM handle.
 * @param   idxL2       The L2 table index to resolve.
 *
 * @note The content of the resolved L2 table entry is not validated!.
 */
DECLINLINE(PDBGFBPL2ENTRY) dbgfR3BpL2GetByIdx(PUVM pUVM, uint32_t idxL2)
{
    uint32_t idChunk  = DBGF_BP_L2_IDX_GET_CHUNK_ID(idxL2);
    uint32_t idxEntry = DBGF_BP_L2_IDX_GET_ENTRY(idxL2);

    AssertReturn(idChunk < DBGF_BP_L2_TBL_CHUNK_COUNT, NULL);
    AssertReturn(idxEntry < DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK, NULL);

    PDBGFBPL2TBLCHUNKR3 pL2Chunk = &pUVM->dbgf.s.aBpL2TblChunks[idChunk];
    AssertPtrReturn(pL2Chunk->pbmAlloc, NULL);
    AssertReturn(ASMBitTest(pL2Chunk->pbmAlloc, idxEntry), NULL);

    return &pL2Chunk->CTX_SUFF(pL2Base)[idxEntry];
}


/**
 * Creates a binary search tree with the given root and leaf nodes.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL1               The index into the L1 table where the created tree should be linked into.
 * @param   u32EntryOld         The old entry in the L1 table used to compare with in the atomic update.
 * @param   hBpRoot             The root node DBGF handle to assign.
 * @param   GCPtrRoot           The root nodes GC pointer to use as a key.
 * @param   hBpLeaf             The leafs node DBGF handle to assign.
 * @param   GCPtrLeaf           The leafs node GC pointer to use as a key.
 */
static int dbgfR3BpInt3L2BstCreate(PUVM pUVM, uint32_t idxL1, uint32_t u32EntryOld,
                                   DBGFBP hBpRoot, RTGCUINTPTR GCPtrRoot,
                                   DBGFBP hBpLeaf, RTGCUINTPTR GCPtrLeaf)
{
    AssertReturn(GCPtrRoot != GCPtrLeaf, VERR_DBGF_BP_IPE_9);
    Assert(DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(GCPtrRoot) == DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(GCPtrLeaf));

    /* Allocate two nodes. */
    uint32_t idxL2Root = 0;
    PDBGFBPL2ENTRY pL2Root = NULL;
    int rc = dbgfR3BpL2TblEntryAlloc(pUVM, &idxL2Root, &pL2Root);
    if (RT_SUCCESS(rc))
    {
        uint32_t idxL2Leaf = 0;
        PDBGFBPL2ENTRY pL2Leaf = NULL;
        rc = dbgfR3BpL2TblEntryAlloc(pUVM, &idxL2Leaf, &pL2Leaf);
        if (RT_SUCCESS(rc))
        {
            dbgfBpL2TblEntryInit(pL2Leaf, hBpLeaf, GCPtrLeaf, DBGF_BP_L2_ENTRY_IDX_END, DBGF_BP_L2_ENTRY_IDX_END, 0 /*iDepth*/);
            if (GCPtrLeaf < GCPtrRoot)
                dbgfBpL2TblEntryInit(pL2Root, hBpRoot, GCPtrRoot, idxL2Leaf, DBGF_BP_L2_ENTRY_IDX_END, 0 /*iDepth*/);
            else
                dbgfBpL2TblEntryInit(pL2Root, hBpRoot, GCPtrRoot, DBGF_BP_L2_ENTRY_IDX_END, idxL2Leaf, 0 /*iDepth*/);

            uint32_t const u32Entry = DBGF_BP_INT3_L1_ENTRY_CREATE_L2_IDX(idxL2Root);
            if (ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1], u32Entry, u32EntryOld))
                return VINF_SUCCESS;

            /* The L1 entry has changed due to another thread racing us during insertion, free nodes and try again. */
            dbgfR3BpL2TblEntryFree(pUVM, idxL2Leaf, pL2Leaf);
            rc = VINF_TRY_AGAIN;
        }

        dbgfR3BpL2TblEntryFree(pUVM, idxL2Root, pL2Root);
    }

    return rc;
}


/**
 * Inserts the given breakpoint handle into an existing binary search tree.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL2Root           The index of the tree root in the L2 table.
 * @param   hBp                 The node DBGF handle to insert.
 * @param   GCPtr               The nodes GC pointer to use as a key.
 */
static int dbgfR3BpInt2L2BstNodeInsert(PUVM pUVM, uint32_t idxL2Root, DBGFBP hBp, RTGCUINTPTR GCPtr)
{
    GCPtr = DBGF_BP_INT3_L2_KEY_EXTRACT_FROM_ADDR(GCPtr);

    /* Allocate a new node first. */
    uint32_t idxL2Nd = 0;
    PDBGFBPL2ENTRY pL2Nd = NULL;
    int rc = dbgfR3BpL2TblEntryAlloc(pUVM, &idxL2Nd, &pL2Nd);
    if (RT_SUCCESS(rc))
    {
        /* Walk the tree and find the correct node to insert to. */
        PDBGFBPL2ENTRY pL2Entry = dbgfR3BpL2GetByIdx(pUVM, idxL2Root);
        while (RT_LIKELY(pL2Entry))
        {
            /* Make a copy of the entry. */
            DBGFBPL2ENTRY L2Entry;
            L2Entry.u64GCPtrKeyAndBpHnd1       = ASMAtomicReadU64(&pL2Entry->u64GCPtrKeyAndBpHnd1);
            L2Entry.u64LeftRightIdxDepthBpHnd2 = ASMAtomicReadU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2);

            RTGCUINTPTR GCPtrL2Entry = DBGF_BP_L2_ENTRY_GET_GCPTR(L2Entry.u64GCPtrKeyAndBpHnd1);
            AssertBreak(GCPtr != GCPtrL2Entry);

            /* Not found, get to the next level. */
            uint32_t idxL2Next = GCPtr < GCPtrL2Entry
                               ? DBGF_BP_L2_ENTRY_GET_IDX_LEFT(L2Entry.u64LeftRightIdxDepthBpHnd2)
                               : DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(L2Entry.u64LeftRightIdxDepthBpHnd2);
            if (idxL2Next == DBGF_BP_L2_ENTRY_IDX_END)
            {
                /* Insert the new node here. */
                dbgfBpL2TblEntryInit(pL2Nd, hBp, GCPtr, DBGF_BP_L2_ENTRY_IDX_END, DBGF_BP_L2_ENTRY_IDX_END, 0 /*iDepth*/);
                if (GCPtr < GCPtrL2Entry)
                    dbgfBpL2TblEntryUpdateLeft(pL2Entry, idxL2Next, 0 /*iDepth*/);
                else
                    dbgfBpL2TblEntryUpdateRight(pL2Entry, idxL2Next, 0 /*iDepth*/);
                return VINF_SUCCESS;
            }

            pL2Entry = dbgfR3BpL2GetByIdx(pUVM, idxL2Next);
        }

        dbgfR3BpL2TblEntryFree(pUVM, idxL2Nd, pL2Nd);
        rc = VERR_DBGF_BP_L2_LOOKUP_FAILED;
    }

    return rc;
}


/**
 * Adds the given breakpoint handle keyed with the GC pointer to the proper L2 binary search tree
 * possibly creating a new tree.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL1               The index into the L1 table the breakpoint uses.
 * @param   hBp                 The breakpoint handle which is to be added.
 * @param   GCPtr               The GC pointer the breakpoint is keyed with.
 */
static int dbgfR3BpInt3L2BstNodeAdd(PUVM pUVM, uint32_t idxL1, DBGFBP hBp, RTGCUINTPTR GCPtr)
{
    int rc = RTSemFastMutexRequest(pUVM->dbgf.s.hMtxBpL2Wr); AssertRC(rc);

    uint32_t u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1]); /* Re-read, could get raced by a remove operation. */
    uint8_t u8Type = DBGF_BP_INT3_L1_ENTRY_GET_TYPE(u32Entry);
    if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND)
    {
        /* Create a new search tree, gather the necessary information first. */
        DBGFBP hBp2 = DBGF_BP_INT3_L1_ENTRY_GET_BP_HND(u32Entry);
        PDBGFBPINT pBp2 = dbgfR3BpGetByHnd(pUVM, hBp2);
        AssertStmt(RT_VALID_PTR(pBp2), rc = VERR_DBGF_BP_IPE_7);
        if (RT_SUCCESS(rc))
            rc = dbgfR3BpInt3L2BstCreate(pUVM, idxL1, u32Entry, hBp, GCPtr, hBp2, pBp2->Pub.u.Int3.GCPtr);
    }
    else if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX)
        rc = dbgfR3BpInt2L2BstNodeInsert(pUVM, DBGF_BP_INT3_L1_ENTRY_GET_L2_IDX(u32Entry), hBp, GCPtr);

    int rc2 = RTSemFastMutexRelease(pUVM->dbgf.s.hMtxBpL2Wr); AssertRC(rc2);
    return rc;
}


/**
 * Gets the leftmost from the given tree node start index.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL2Start          The start index to walk from.
 * @param   pidxL2Leftmost      Where to store the L2 table index of the leftmost entry.
 * @param   ppL2NdLeftmost      Where to store the pointer to the leftmost L2 table entry.
 * @param   pidxL2NdLeftParent  Where to store the L2 table index of the leftmost entries parent.
 * @param   ppL2NdLeftParent    Where to store the pointer to the leftmost L2 table entries parent.
 */
static int dbgfR33BpInt3BstGetLeftmostEntryFromNode(PUVM pUVM, uint32_t idxL2Start,
                                                    uint32_t *pidxL2Leftmost, PDBGFBPL2ENTRY *ppL2NdLeftmost,
                                                    uint32_t *pidxL2NdLeftParent, PDBGFBPL2ENTRY *ppL2NdLeftParent)
{
    uint32_t idxL2Parent = DBGF_BP_L2_ENTRY_IDX_END;
    PDBGFBPL2ENTRY pL2NdParent = NULL;

    for (;;)
    {
        PDBGFBPL2ENTRY pL2Entry = dbgfR3BpL2GetByIdx(pUVM, idxL2Start);
        AssertPtr(pL2Entry);

        uint32_t idxL2Left = DBGF_BP_L2_ENTRY_GET_IDX_LEFT(pL2Entry->u64LeftRightIdxDepthBpHnd2);
        if (idxL2Start == DBGF_BP_L2_ENTRY_IDX_END)
        {
            *pidxL2Leftmost     = idxL2Start;
            *ppL2NdLeftmost     = pL2Entry;
            *pidxL2NdLeftParent = idxL2Parent;
            *ppL2NdLeftParent   = pL2NdParent;
            break;
        }

        idxL2Parent = idxL2Start;
        idxL2Start  = idxL2Left;
        pL2NdParent = pL2Entry;
    }

    return VINF_SUCCESS;
}


/**
 * Removes the given node rearranging the tree.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL1               The index into the L1 table pointing to the binary search tree containing the node.
 * @param   idxL2Root           The L2 table index where the tree root is located.
 * @param   idxL2Nd             The node index to remove.
 * @param   pL2Nd               The L2 table entry to remove.
 * @param   idxL2NdParent       The parents index, can be DBGF_BP_L2_ENTRY_IDX_END if the root is about to be removed.
 * @param   pL2NdParent         The parents L2 table entry, can be NULL if the root is about to be removed.
 * @param   fLeftChild          Flag whether the node is the left child of the parent or the right one.
 */
static int dbgfR3BpInt3BstNodeRemove(PUVM pUVM, uint32_t idxL1, uint32_t idxL2Root,
                                     uint32_t idxL2Nd, PDBGFBPL2ENTRY pL2Nd,
                                     uint32_t idxL2NdParent, PDBGFBPL2ENTRY pL2NdParent,
                                     bool fLeftChild)
{
    /*
     * If there are only two nodes remaining the tree will get destroyed and the
     * L1 entry will be converted to the direct handle type.
     */
    uint32_t idxL2Left  = DBGF_BP_L2_ENTRY_GET_IDX_LEFT(pL2Nd->u64LeftRightIdxDepthBpHnd2);
    uint32_t idxL2Right = DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(pL2Nd->u64LeftRightIdxDepthBpHnd2);

    Assert(idxL2NdParent != DBGF_BP_L2_ENTRY_IDX_END || !pL2NdParent); RT_NOREF(idxL2NdParent);
    uint32_t idxL2ParentNew = DBGF_BP_L2_ENTRY_IDX_END;
    if (idxL2Right == DBGF_BP_L2_ENTRY_IDX_END)
        idxL2ParentNew = idxL2Left;
    else
    {
        /* Find the leftmost entry of the right subtree and move it to the to be removed nodes location in the tree. */
        PDBGFBPL2ENTRY pL2NdLeftmostParent   = NULL;
        PDBGFBPL2ENTRY pL2NdLeftmost         = NULL;
        uint32_t idxL2NdLeftmostParent = DBGF_BP_L2_ENTRY_IDX_END;
        uint32_t idxL2Leftmost = DBGF_BP_L2_ENTRY_IDX_END;
        int rc = dbgfR33BpInt3BstGetLeftmostEntryFromNode(pUVM, idxL2Right, &idxL2Leftmost ,&pL2NdLeftmost,
                                                          &idxL2NdLeftmostParent, &pL2NdLeftmostParent);
        AssertRCReturn(rc, rc);

        if (pL2NdLeftmostParent)
        {
            /* Rearrange the leftmost entries parents pointer. */
            dbgfBpL2TblEntryUpdateLeft(pL2NdLeftmostParent, DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(pL2NdLeftmost->u64LeftRightIdxDepthBpHnd2), 0 /*iDepth*/);
            dbgfBpL2TblEntryUpdateRight(pL2NdLeftmost, idxL2Right, 0 /*iDepth*/);
        }

        dbgfBpL2TblEntryUpdateLeft(pL2NdLeftmost, idxL2Left, 0 /*iDepth*/);

        /* Update the remove nodes parent to point to the new node. */
        idxL2ParentNew = idxL2Leftmost;
    }

    if (pL2NdParent)
    {
        /* Asssign the new L2 index to proper parents left or right pointer. */
        if (fLeftChild)
            dbgfBpL2TblEntryUpdateLeft(pL2NdParent, idxL2ParentNew, 0 /*iDepth*/);
        else
            dbgfBpL2TblEntryUpdateRight(pL2NdParent, idxL2ParentNew, 0 /*iDepth*/);
    }
    else
    {
        /* The root node is removed, set the new root in the L1 table. */
        Assert(idxL2ParentNew != DBGF_BP_L2_ENTRY_IDX_END);
        idxL2Root = idxL2ParentNew;
        ASMAtomicXchgU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1], DBGF_BP_INT3_L1_ENTRY_CREATE_L2_IDX(idxL2Left));
    }

    /* Free the node. */
    dbgfR3BpL2TblEntryFree(pUVM, idxL2Nd, pL2Nd);

    /*
     * Check whether the old/new root is the only node remaining and convert the L1
     * table entry to a direct breakpoint handle one in that case.
     */
    pL2Nd = dbgfR3BpL2GetByIdx(pUVM, idxL2Root);
    AssertPtr(pL2Nd);
    if (   DBGF_BP_L2_ENTRY_GET_IDX_LEFT(pL2Nd->u64LeftRightIdxDepthBpHnd2) == DBGF_BP_L2_ENTRY_IDX_END
        && DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(pL2Nd->u64LeftRightIdxDepthBpHnd2) == DBGF_BP_L2_ENTRY_IDX_END)
    {
        DBGFBP hBp = DBGF_BP_L2_ENTRY_GET_BP_HND(pL2Nd->u64GCPtrKeyAndBpHnd1, pL2Nd->u64LeftRightIdxDepthBpHnd2);
        dbgfR3BpL2TblEntryFree(pUVM, idxL2Root, pL2Nd);
        ASMAtomicXchgU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1], DBGF_BP_INT3_L1_ENTRY_CREATE_BP_HND(hBp));
    }

    return VINF_SUCCESS;
}


/**
 * Removes the given breakpoint handle keyed with the GC pointer from the L2 binary search tree
 * pointed to by the given L2 root index.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idxL1               The index into the L1 table pointing to the binary search tree.
 * @param   idxL2Root           The L2 table index where the tree root is located.
 * @param   hBp                 The breakpoint handle which is to be removed.
 * @param   GCPtr               The GC pointer the breakpoint is keyed with.
 */
static int dbgfR3BpInt3L2BstRemove(PUVM pUVM, uint32_t idxL1, uint32_t idxL2Root, DBGFBP hBp, RTGCUINTPTR GCPtr)
{
    GCPtr = DBGF_BP_INT3_L2_KEY_EXTRACT_FROM_ADDR(GCPtr);

    int rc = RTSemFastMutexRequest(pUVM->dbgf.s.hMtxBpL2Wr); AssertRC(rc);

    uint32_t idxL2Cur = idxL2Root;
    uint32_t idxL2Parent = DBGF_BP_L2_ENTRY_IDX_END;
    bool fLeftChild = false;
    PDBGFBPL2ENTRY pL2EntryParent = NULL;
    for (;;)
    {
        PDBGFBPL2ENTRY pL2Entry = dbgfR3BpL2GetByIdx(pUVM, idxL2Cur);
        AssertPtr(pL2Entry);

        /* Check whether this node is to be removed.. */
        RTGCUINTPTR GCPtrL2Entry = DBGF_BP_L2_ENTRY_GET_GCPTR(pL2Entry->u64GCPtrKeyAndBpHnd1);
        if (GCPtrL2Entry == GCPtr)
        {
            Assert(DBGF_BP_L2_ENTRY_GET_BP_HND(pL2Entry->u64GCPtrKeyAndBpHnd1, pL2Entry->u64LeftRightIdxDepthBpHnd2) == hBp); RT_NOREF(hBp);

            rc = dbgfR3BpInt3BstNodeRemove(pUVM, idxL1, idxL2Root, idxL2Cur, pL2Entry, idxL2Parent, pL2EntryParent, fLeftChild);
            break;
        }

        pL2EntryParent = pL2Entry;
        idxL2Parent    = idxL2Cur;

        if (GCPtrL2Entry < GCPtr)
        {
            fLeftChild = true;
            idxL2Cur = DBGF_BP_L2_ENTRY_GET_IDX_LEFT(pL2Entry->u64LeftRightIdxDepthBpHnd2);
        }
        else
        {
            fLeftChild = false;
            idxL2Cur = DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(pL2Entry->u64LeftRightIdxDepthBpHnd2);
        }

        AssertBreakStmt(idxL2Cur != DBGF_BP_L2_ENTRY_IDX_END, rc = VERR_DBGF_BP_L2_LOOKUP_FAILED);
    }

    int rc2 = RTSemFastMutexRelease(pUVM->dbgf.s.hMtxBpL2Wr); AssertRC(rc2);

    return rc;
}


/**
 * Adds the given int3 breakpoint to the appropriate lookup tables.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to add.
 * @param   pBp                 The internal breakpoint state.
 */
static int dbgfR3BpInt3Add(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(DBGF_BP_PUB_GET_TYPE(&pBp->Pub) == DBGFBPTYPE_INT3, VERR_DBGF_BP_IPE_3);

    int rc = VINF_SUCCESS;
    uint16_t idxL1 = DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(pBp->Pub.u.Int3.GCPtr);
    uint8_t  cTries = 16;

    while (cTries--)
    {
        uint32_t u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1]);
        if (u32Entry == DBGF_BP_INT3_L1_ENTRY_TYPE_NULL)
        {
            /*
             * No breakpoint assigned so far for this entry, create an entry containing
             * the direct breakpoint handle and try to exchange it atomically.
             */
            u32Entry = DBGF_BP_INT3_L1_ENTRY_CREATE_BP_HND(hBp);
            if (ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1], u32Entry, DBGF_BP_INT3_L1_ENTRY_TYPE_NULL))
                break;
        }
        else
        {
            rc = dbgfR3BpInt3L2BstNodeAdd(pUVM, idxL1, hBp, pBp->Pub.u.Int3.GCPtr);
            if (rc != VINF_TRY_AGAIN)
                break;
        }
    }

    if (   RT_SUCCESS(rc)
        && !cTries) /* Too much contention, abort with an error. */
        rc = VERR_DBGF_BP_INT3_ADD_TRIES_REACHED;

    return rc;
}


/**
 * Adds the given port I/O breakpoint to the appropriate lookup tables.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to add.
 * @param   pBp                 The internal breakpoint state.
 */
static int dbgfR3BpPortIoAdd(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(DBGF_BP_PUB_GET_TYPE(&pBp->Pub) == DBGFBPTYPE_PORT_IO, VERR_DBGF_BP_IPE_3);

    uint16_t uPortExcl = pBp->Pub.u.PortIo.uPort + pBp->Pub.u.PortIo.cPorts;
    uint32_t u32Entry  = DBGF_BP_INT3_L1_ENTRY_CREATE_BP_HND(hBp);
    for (uint16_t idxPort = pBp->Pub.u.PortIo.uPort; idxPort < uPortExcl; idxPort++)
    {
        bool fXchg = ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocPortIoR3[idxPort], u32Entry, DBGF_BP_INT3_L1_ENTRY_TYPE_NULL);
        if (!fXchg)
        {
            /* Something raced us, so roll back the other registrations. */
            while (idxPort > pBp->Pub.u.PortIo.uPort)
            {
                fXchg = ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocPortIoR3[idxPort], DBGF_BP_INT3_L1_ENTRY_TYPE_NULL, u32Entry);
                Assert(fXchg); RT_NOREF(fXchg);
            }

            return VERR_DBGF_BP_INT3_ADD_TRIES_REACHED; /** @todo New status code */
        }
    }

    return VINF_SUCCESS;
}


/**
 * Get a breakpoint give by address.
 *
 * @returns The breakpoint handle on success or NIL_DBGFBP if not found.
 * @param   pUVM                The user mode VM handle.
 * @param   enmType             The breakpoint type.
 * @param   GCPtr               The breakpoint address.
 * @param   ppBp                Where to store the pointer to the internal breakpoint state on success, optional.
 */
static DBGFBP dbgfR3BpGetByAddr(PUVM pUVM, DBGFBPTYPE enmType, RTGCUINTPTR GCPtr, PDBGFBPINT *ppBp)
{
    DBGFBP hBp = NIL_DBGFBP;

    switch (enmType)
    {
        case DBGFBPTYPE_REG:
        {
            PVM pVM = pUVM->pVM;
            VM_ASSERT_VALID_EXT_RETURN(pVM, NIL_DBGFBP);

            for (uint32_t i = 0; i < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); i++)
            {
                PDBGFBPHW pHwBp = &pVM->dbgf.s.aHwBreakpoints[i];

                AssertCompileSize(DBGFBP, sizeof(uint32_t));
                DBGFBP hBpTmp = ASMAtomicReadU32(&pHwBp->hBp);
                if (   pHwBp->GCPtr == GCPtr
                    && hBpTmp != NIL_DBGFBP)
                {
                    hBp = hBpTmp;
                    break;
                }
            }
            break;
        }

        case DBGFBPTYPE_INT3:
        {
            const uint16_t idxL1      = DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(GCPtr);
            const uint32_t u32L1Entry = ASMAtomicReadU32(&pUVM->dbgf.s.CTX_SUFF(paBpLocL1)[idxL1]);

            if (u32L1Entry != DBGF_BP_INT3_L1_ENTRY_TYPE_NULL)
            {
                uint8_t u8Type = DBGF_BP_INT3_L1_ENTRY_GET_TYPE(u32L1Entry);
                if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND)
                    hBp = DBGF_BP_INT3_L1_ENTRY_GET_BP_HND(u32L1Entry);
                else if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX)
                {
                    RTGCUINTPTR GCPtrKey = DBGF_BP_INT3_L2_KEY_EXTRACT_FROM_ADDR(GCPtr);
                    PDBGFBPL2ENTRY pL2Nd = dbgfR3BpL2GetByIdx(pUVM, DBGF_BP_INT3_L1_ENTRY_GET_L2_IDX(u32L1Entry));

                    for (;;)
                    {
                        AssertPtr(pL2Nd);

                        RTGCUINTPTR GCPtrL2Entry = DBGF_BP_L2_ENTRY_GET_GCPTR(pL2Nd->u64GCPtrKeyAndBpHnd1);
                        if (GCPtrKey == GCPtrL2Entry)
                        {
                            hBp = DBGF_BP_L2_ENTRY_GET_BP_HND(pL2Nd->u64GCPtrKeyAndBpHnd1, pL2Nd->u64LeftRightIdxDepthBpHnd2);
                            break;
                        }

                        /* Not found, get to the next level. */
                        uint32_t idxL2Next = GCPtrKey < GCPtrL2Entry
                                           ? DBGF_BP_L2_ENTRY_GET_IDX_LEFT(pL2Nd->u64LeftRightIdxDepthBpHnd2)
                                           : DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(pL2Nd->u64LeftRightIdxDepthBpHnd2);
                        /* Address not found if the entry denotes the end. */
                        if (idxL2Next == DBGF_BP_L2_ENTRY_IDX_END)
                            break;

                        pL2Nd = dbgfR3BpL2GetByIdx(pUVM, idxL2Next);
                    }
                }
            }
            break;
        }

        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            break;
    }

    if (   hBp != NIL_DBGFBP
        && ppBp)
        *ppBp = dbgfR3BpGetByHnd(pUVM, hBp);
    return hBp;
}


/**
 * Get a port I/O breakpoint given by the range.
 *
 * @returns The breakpoint handle on success or NIL_DBGF if not found.
 * @param   pUVM                The user mode VM handle.
 * @param   uPort               First port in the range.
 * @param   cPorts              Number of ports in the range.
 * @param   ppBp                Where to store the pointer to the internal breakpoint state on success, optional.
 */
static DBGFBP dbgfR3BpPortIoGetByRange(PUVM pUVM, RTIOPORT uPort, RTIOPORT cPorts, PDBGFBPINT *ppBp)
{
    DBGFBP hBp = NIL_DBGFBP;

    for (RTIOPORT idxPort = uPort; idxPort < uPort + cPorts; idxPort++)
    {
        const uint32_t u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.CTX_SUFF(paBpLocPortIo)[idxPort]);
        if (u32Entry != DBGF_BP_INT3_L1_ENTRY_TYPE_NULL)
        {
            hBp = DBGF_BP_INT3_L1_ENTRY_GET_BP_HND(u32Entry);
            break;
        }
    }

    if (   hBp != NIL_DBGFBP
        && ppBp)
        *ppBp =  dbgfR3BpGetByHnd(pUVM, hBp);
    return hBp;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpInt3RemoveEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    DBGFBP hBp = (DBGFBP)(uintptr_t)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PUVM pUVM = pVM->pUVM;
    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);
    AssertPtrReturn(pBp, VERR_DBGF_BP_IPE_8);

    int rc = VINF_SUCCESS;
    if (pVCpu->idCpu == 0)
    {
        uint16_t idxL1 = DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(pBp->Pub.u.Int3.GCPtr);
        uint32_t u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1]);
        AssertReturn(u32Entry != DBGF_BP_INT3_L1_ENTRY_TYPE_NULL, VERR_DBGF_BP_IPE_6);

        uint8_t u8Type = DBGF_BP_INT3_L1_ENTRY_GET_TYPE(u32Entry);
        if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND)
        {
            /* Single breakpoint, just exchange atomically with the null value. */
            if (!ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1], DBGF_BP_INT3_L1_ENTRY_TYPE_NULL, u32Entry))
            {
                /*
                 * A breakpoint addition must have raced us converting the L1 entry to an L2 index type, re-read
                 * and remove the node from the created binary search tree.
                 *
                 * This works because after the entry was converted to an L2 index it can only be converted back
                 * to a direct handle by removing one or more nodes which always goes through the fast mutex
                 * protecting the L2 table. Likewise adding a new breakpoint requires grabbing the mutex as well
                 * so there is serialization here and the node can be removed safely without having to worry about
                 * concurrent tree modifications.
                 */
                u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.paBpLocL1R3[idxL1]);
                AssertReturn(DBGF_BP_INT3_L1_ENTRY_GET_TYPE(u32Entry) == DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX, VERR_DBGF_BP_IPE_9);

                rc = dbgfR3BpInt3L2BstRemove(pUVM, idxL1, DBGF_BP_INT3_L1_ENTRY_GET_L2_IDX(u32Entry),
                                             hBp, pBp->Pub.u.Int3.GCPtr);
            }
        }
        else if (u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX)
            rc = dbgfR3BpInt3L2BstRemove(pUVM, idxL1, DBGF_BP_INT3_L1_ENTRY_GET_L2_IDX(u32Entry),
                                         hBp, pBp->Pub.u.Int3.GCPtr);
    }

    return rc;
}


/**
 * Removes the given int3 breakpoint from all lookup tables.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to remove.
 * @param   pBp                 The internal breakpoint state.
 */
static int dbgfR3BpInt3Remove(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(DBGF_BP_PUB_GET_TYPE(&pBp->Pub) == DBGFBPTYPE_INT3, VERR_DBGF_BP_IPE_3);

    /*
     * This has to be done by an EMT rendezvous in order to not have an EMT traversing
     * any L2 trees while it is being removed.
     */
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpInt3RemoveEmtWorker, (void *)(uintptr_t)hBp);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpPortIoRemoveEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    DBGFBP hBp = (DBGFBP)(uintptr_t)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PUVM pUVM = pVM->pUVM;
    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);
    AssertPtrReturn(pBp, VERR_DBGF_BP_IPE_8);

    int rc = VINF_SUCCESS;
    if (pVCpu->idCpu == 0)
    {
        /*
         * Remove the whole range, there shouldn't be any other breakpoint configured for this range as this is not
         * allowed right now.
         */
        uint16_t uPortExcl = pBp->Pub.u.PortIo.uPort + pBp->Pub.u.PortIo.cPorts;
        for (uint16_t idxPort = pBp->Pub.u.PortIo.uPort; idxPort < uPortExcl; idxPort++)
        {
            uint32_t u32Entry = ASMAtomicReadU32(&pUVM->dbgf.s.paBpLocPortIoR3[idxPort]);
            AssertReturn(u32Entry != DBGF_BP_INT3_L1_ENTRY_TYPE_NULL, VERR_DBGF_BP_IPE_6);

            uint8_t u8Type = DBGF_BP_INT3_L1_ENTRY_GET_TYPE(u32Entry);
            AssertReturn(u8Type == DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND, VERR_DBGF_BP_IPE_7);

            bool fXchg = ASMAtomicCmpXchgU32(&pUVM->dbgf.s.paBpLocPortIoR3[idxPort], DBGF_BP_INT3_L1_ENTRY_TYPE_NULL, u32Entry);
            Assert(fXchg); RT_NOREF(fXchg);
        }
    }

    return rc;
}


/**
 * Removes the given port I/O breakpoint from all lookup tables.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to remove.
 * @param   pBp                 The internal breakpoint state.
 */
static int dbgfR3BpPortIoRemove(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    AssertReturn(DBGF_BP_PUB_GET_TYPE(&pBp->Pub) == DBGFBPTYPE_PORT_IO, VERR_DBGF_BP_IPE_3);

    /*
     * This has to be done by an EMT rendezvous in order to not have an EMT accessing
     * the breakpoint while it is removed.
     */
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpPortIoRemoveEmtWorker, (void *)(uintptr_t)hBp);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3BpRegRecalcOnCpu(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF(pvUser);

#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pVM, pVCpu);
    AssertReleaseFailed();
    return VERR_NOT_IMPLEMENTED;
#else
    /*
     * CPU 0 updates the enabled hardware breakpoint counts.
     */
    if (pVCpu->idCpu == 0)
    {
        pVM->dbgf.s.cEnabledHwBreakpoints   = 0;
        pVM->dbgf.s.cEnabledHwIoBreakpoints = 0;

        for (uint32_t iBp = 0; iBp < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); iBp++)
        {
            if (pVM->dbgf.s.aHwBreakpoints[iBp].fEnabled)
            {
                pVM->dbgf.s.cEnabledHwBreakpoints   += 1;
                pVM->dbgf.s.cEnabledHwIoBreakpoints += pVM->dbgf.s.aHwBreakpoints[iBp].fType == X86_DR7_RW_IO;
            }
        }
    }

    return CPUMRecalcHyperDRx(pVCpu, UINT8_MAX);
#endif
}


/**
 * Arms the given breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to arm.
 * @param   pBp                 The internal breakpoint state pointer for the handle.
 *
 * @thread Any thread.
 */
static int dbgfR3BpArm(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    int rc;
    PVM pVM = pUVM->pVM;

    Assert(!DBGF_BP_PUB_IS_ENABLED(&pBp->Pub));
    switch (DBGF_BP_PUB_GET_TYPE(&pBp->Pub))
    {
        case DBGFBPTYPE_REG:
        {
            Assert(pBp->Pub.u.Reg.iReg < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints));
            PDBGFBPHW pBpHw = &pVM->dbgf.s.aHwBreakpoints[pBp->Pub.u.Reg.iReg];
            Assert(pBpHw->hBp == hBp); RT_NOREF(hBp);

            dbgfR3BpSetEnabled(pBp, true /*fEnabled*/);
            ASMAtomicWriteBool(&pBpHw->fEnabled, true);
            rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpRegRecalcOnCpu, NULL);
            if (RT_FAILURE(rc))
            {
                ASMAtomicWriteBool(&pBpHw->fEnabled, false);
                dbgfR3BpSetEnabled(pBp, false /*fEnabled*/);
            }
            break;
        }
        case DBGFBPTYPE_INT3:
        {
            dbgfR3BpSetEnabled(pBp, true /*fEnabled*/);

            /** @todo When we enable the first int3 breakpoint we should do this in an EMT rendezvous
             * as the VMX code intercepts #BP only when at least one int3 breakpoint is enabled.
             * A racing vCPU might trigger it and forward it to the guest causing panics/crashes/havoc. */
            /*
             * Save current byte and write the int3 instruction byte.
             */
            rc = PGMPhysSimpleReadGCPhys(pVM, &pBp->Pub.u.Int3.bOrg, pBp->Pub.u.Int3.PhysAddr, sizeof(pBp->Pub.u.Int3.bOrg));
            if (RT_SUCCESS(rc))
            {
                static const uint8_t s_bInt3 = 0xcc;
                rc = PGMPhysSimpleWriteGCPhys(pVM, pBp->Pub.u.Int3.PhysAddr, &s_bInt3, sizeof(s_bInt3));
                if (RT_SUCCESS(rc))
                {
                    ASMAtomicIncU32(&pVM->dbgf.s.cEnabledInt3Breakpoints);
                    Log(("DBGF: Set breakpoint at %RGv (Phys %RGp)\n", pBp->Pub.u.Int3.GCPtr, pBp->Pub.u.Int3.PhysAddr));
                }
            }

            if (RT_FAILURE(rc))
                dbgfR3BpSetEnabled(pBp, false /*fEnabled*/);

            break;
        }
        case DBGFBPTYPE_PORT_IO:
        {
            dbgfR3BpSetEnabled(pBp, true /*fEnabled*/);
            ASMAtomicIncU32(&pUVM->dbgf.s.cPortIoBps);
            IOMR3NotifyBreakpointCountChange(pVM, true /*fPortIo*/, false /*fMmio*/);
            rc = VINF_SUCCESS;
            break;
        }
        case DBGFBPTYPE_MMIO:
            rc = VERR_NOT_IMPLEMENTED;
            break;
        default:
            AssertMsgFailedReturn(("Invalid breakpoint type %d\n", DBGF_BP_PUB_GET_TYPE(&pBp->Pub)),
                                  VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    return rc;
}


/**
 * Disarms the given breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   hBp                 The breakpoint handle to disarm.
 * @param   pBp                 The internal breakpoint state pointer for the handle.
 *
 * @thread Any thread.
 */
static int dbgfR3BpDisarm(PUVM pUVM, DBGFBP hBp, PDBGFBPINT pBp)
{
    int rc;
    PVM pVM = pUVM->pVM;

    Assert(DBGF_BP_PUB_IS_ENABLED(&pBp->Pub));
    switch (DBGF_BP_PUB_GET_TYPE(&pBp->Pub))
    {
        case DBGFBPTYPE_REG:
        {
            Assert(pBp->Pub.u.Reg.iReg < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints));
            PDBGFBPHW pBpHw = &pVM->dbgf.s.aHwBreakpoints[pBp->Pub.u.Reg.iReg];
            Assert(pBpHw->hBp == hBp); RT_NOREF(hBp);

            dbgfR3BpSetEnabled(pBp, false /*fEnabled*/);
            ASMAtomicWriteBool(&pBpHw->fEnabled, false);
            rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3BpRegRecalcOnCpu, NULL);
            if (RT_FAILURE(rc))
            {
                ASMAtomicWriteBool(&pBpHw->fEnabled, true);
                dbgfR3BpSetEnabled(pBp, true /*fEnabled*/);
            }
            break;
        }
        case DBGFBPTYPE_INT3:
        {
            /*
             * Check that the current byte is the int3 instruction, and restore the original one.
             * We currently ignore invalid bytes.
             */
            uint8_t bCurrent = 0;
            rc = PGMPhysSimpleReadGCPhys(pVM, &bCurrent, pBp->Pub.u.Int3.PhysAddr, sizeof(bCurrent));
            if (   RT_SUCCESS(rc)
                && bCurrent == 0xcc)
            {
                rc = PGMPhysSimpleWriteGCPhys(pVM, pBp->Pub.u.Int3.PhysAddr, &pBp->Pub.u.Int3.bOrg, sizeof(pBp->Pub.u.Int3.bOrg));
                if (RT_SUCCESS(rc))
                {
                    ASMAtomicDecU32(&pVM->dbgf.s.cEnabledInt3Breakpoints);
                    dbgfR3BpSetEnabled(pBp, false /*fEnabled*/);
                    Log(("DBGF: Removed breakpoint at %RGv (Phys %RGp)\n", pBp->Pub.u.Int3.GCPtr, pBp->Pub.u.Int3.PhysAddr));
                }
            }
            break;
        }
        case DBGFBPTYPE_PORT_IO:
        {
            dbgfR3BpSetEnabled(pBp, false /*fEnabled*/);
            uint32_t cPortIoBps = ASMAtomicDecU32(&pUVM->dbgf.s.cPortIoBps);
            if (!cPortIoBps) /** @todo Need to gather all EMTs to not have a stray EMT accessing BP data when it might go away. */
                IOMR3NotifyBreakpointCountChange(pVM, false /*fPortIo*/, false /*fMmio*/);
            rc = VINF_SUCCESS;
            break;
        }
        case DBGFBPTYPE_MMIO:
            rc = VERR_NOT_IMPLEMENTED;
            break;
        default:
            AssertMsgFailedReturn(("Invalid breakpoint type %d\n", DBGF_BP_PUB_GET_TYPE(&pBp->Pub)),
                                  VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    return rc;
}


/**
 * Worker for DBGFR3BpHit() differnetiating on the breakpoint type.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The vCPU the breakpoint event happened on.
 * @param   hBp         The breakpoint handle.
 * @param   pBp         The breakpoint data.
 * @param   pBpOwner    The breakpoint owner data.
 *
 * @thread EMT
 */
static VBOXSTRICTRC dbgfR3BpHit(PVM pVM, PVMCPU pVCpu, DBGFBP hBp, PDBGFBPINT pBp, PCDBGFBPOWNERINT pBpOwner)
{
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    switch (DBGF_BP_PUB_GET_TYPE(&pBp->Pub))
    {
        case DBGFBPTYPE_REG:
        case DBGFBPTYPE_INT3:
        {
            if (DBGF_BP_PUB_IS_EXEC_BEFORE(&pBp->Pub))
                rcStrict = pBpOwner->pfnBpHitR3(pVM, pVCpu->idCpu, pBp->pvUserR3, hBp, &pBp->Pub, DBGF_BP_F_HIT_EXEC_BEFORE);
            if (rcStrict == VINF_SUCCESS)
            {
                uint8_t abInstr[DBGF_BP_INSN_MAX];
                RTGCPTR const GCPtrInstr = CPUMGetGuestFlatPC(pVCpu);
                int rc = PGMPhysSimpleReadGCPtr(pVCpu, &abInstr[0], GCPtrInstr, sizeof(abInstr));
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    /* Replace the int3 with the original instruction byte. */
                    abInstr[0] = pBp->Pub.u.Int3.bOrg;
                    rcStrict = IEMExecOneWithPrefetchedByPC(pVCpu, GCPtrInstr, &abInstr[0], sizeof(abInstr));
                    if (   rcStrict == VINF_SUCCESS
                        && DBGF_BP_PUB_IS_EXEC_AFTER(&pBp->Pub))
                    {
                        VBOXSTRICTRC rcStrict2 = pBpOwner->pfnBpHitR3(pVM, pVCpu->idCpu, pBp->pvUserR3, hBp, &pBp->Pub,
                                                                      DBGF_BP_F_HIT_EXEC_AFTER);
                        if (rcStrict2 == VINF_SUCCESS)
                            return VBOXSTRICTRC_VAL(rcStrict);
                        if (rcStrict2 != VINF_DBGF_BP_HALT)
                            return VERR_DBGF_BP_OWNER_CALLBACK_WRONG_STATUS;
                    }
                    else
                        return VBOXSTRICTRC_VAL(rcStrict);
                }
            }
            break;
        }
        case DBGFBPTYPE_PORT_IO:
        case DBGFBPTYPE_MMIO:
        {
            pVCpu->dbgf.s.fBpIoActive = false;
            rcStrict = pBpOwner->pfnBpIoHitR3(pVM, pVCpu->idCpu, pBp->pvUserR3, hBp, &pBp->Pub,
                                                pVCpu->dbgf.s.fBpIoBefore
                                              ? DBGF_BP_F_HIT_EXEC_BEFORE
                                              : DBGF_BP_F_HIT_EXEC_AFTER,
                                              pVCpu->dbgf.s.fBpIoAccess, pVCpu->dbgf.s.uBpIoAddress,
                                              pVCpu->dbgf.s.uBpIoValue);

            break;
        }
        default:
            AssertMsgFailedReturn(("Invalid breakpoint type %d\n", DBGF_BP_PUB_GET_TYPE(&pBp->Pub)),
                                  VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    return rcStrict;
}


/**
 * Creates a new breakpoint owner returning a handle which can be used when setting breakpoints.
 *
 * @returns VBox status code.
 * @retval  VERR_DBGF_BP_OWNER_NO_MORE_HANDLES if there are no more free owner handles available.
 * @param   pUVM                The user mode VM handle.
 * @param   pfnBpHit            The R3 callback which is called when a breakpoint with the owner handle is hit.
 * @param   pfnBpIoHit          The R3 callback which is called when a I/O breakpoint with the owner handle is hit.
 * @param   phBpOwner           Where to store the owner handle on success.
 *
 * @thread Any thread but might defer work to EMT on the first call.
 */
VMMR3DECL(int) DBGFR3BpOwnerCreate(PUVM pUVM, PFNDBGFBPHIT pfnBpHit, PFNDBGFBPIOHIT pfnBpIoHit, PDBGFBPOWNER phBpOwner)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pfnBpHit || pfnBpIoHit, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phBpOwner, VERR_INVALID_POINTER);

    int rc = dbgfR3BpOwnerEnsureInit(pUVM);
    AssertRCReturn(rc ,rc);

    /* Try to find a free entry in the owner table. */
    for (;;)
    {
        /* Scan the associated bitmap for a free entry. */
        int32_t iClr = ASMBitFirstClear(pUVM->dbgf.s.pbmBpOwnersAllocR3, DBGF_BP_OWNER_COUNT_MAX);
        if (iClr != -1)
        {
            /*
             * Try to allocate, we could get raced here as well. In that case
             * we try again.
             */
            if (!ASMAtomicBitTestAndSet(pUVM->dbgf.s.pbmBpOwnersAllocR3, iClr))
            {
                PDBGFBPOWNERINT pBpOwner = &pUVM->dbgf.s.paBpOwnersR3[iClr];
                pBpOwner->cRefs        = 1;
                pBpOwner->pfnBpHitR3   = pfnBpHit;
                pBpOwner->pfnBpIoHitR3 = pfnBpIoHit;

                *phBpOwner = (DBGFBPOWNER)iClr;
                return VINF_SUCCESS;
            }
            /* else Retry with another spot. */
        }
        else /* no free entry in bitmap, out of entries. */
        {
            rc = VERR_DBGF_BP_OWNER_NO_MORE_HANDLES;
            break;
        }
    }

    return rc;
}


/**
 * Destroys the owner identified by the given handle.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_HANDLE if the given owner handle is invalid.
 * @retval  VERR_DBGF_OWNER_BUSY if there are still breakpoints set with the given owner handle.
 * @param   pUVM                The user mode VM handle.
 * @param   hBpOwner            The breakpoint owner handle to destroy.
 */
VMMR3DECL(int) DBGFR3BpOwnerDestroy(PUVM pUVM, DBGFBPOWNER hBpOwner)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hBpOwner != NIL_DBGFBPOWNER, VERR_INVALID_HANDLE);

    int rc = dbgfR3BpOwnerEnsureInit(pUVM);
    AssertRCReturn(rc ,rc);

    PDBGFBPOWNERINT pBpOwner = dbgfR3BpOwnerGetByHnd(pUVM, hBpOwner);
    if (RT_LIKELY(pBpOwner))
    {
        if (ASMAtomicReadU32(&pBpOwner->cRefs) == 1)
        {
            pBpOwner->pfnBpHitR3 = NULL;
            ASMAtomicDecU32(&pBpOwner->cRefs);
            ASMAtomicBitClear(pUVM->dbgf.s.pbmBpOwnersAllocR3, hBpOwner);
        }
        else
            rc = VERR_DBGF_OWNER_BUSY;
    }
    else
        rc = VERR_INVALID_HANDLE;

    return rc;
}


/**
 * Sets a breakpoint (int 3 based).
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   idSrcCpu    The ID of the virtual CPU used for the
 *                      breakpoint address resolution.
 * @param   pAddress    The address of the breakpoint.
 * @param   iHitTrigger The hit count at which the breakpoint start triggering.
 *                      Use 0 (or 1) if it's gonna trigger at once.
 * @param   iHitDisable The hit count which disables the breakpoint.
 *                      Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp        Where to store the breakpoint handle on success.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetInt3(PUVM pUVM, VMCPUID idSrcCpu, PCDBGFADDRESS pAddress,
                               uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    return DBGFR3BpSetInt3Ex(pUVM, NIL_DBGFBPOWNER, NULL /*pvUser*/, idSrcCpu, pAddress,
                             DBGF_BP_F_DEFAULT, iHitTrigger, iHitDisable, phBp);
}


/**
 * Sets a breakpoint (int 3 based) - extended version.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hOwner          The owner handle, use NIL_DBGFBPOWNER if no special owner attached.
 * @param   pvUser          Opaque user data to pass in the owner callback.
 * @param   idSrcCpu        The ID of the virtual CPU used for the
 *                          breakpoint address resolution.
 * @param   pAddress        The address of the breakpoint.
 * @param   fFlags          Combination of DBGF_BP_F_XXX.
 * @param   iHitTrigger     The hit count at which the breakpoint start triggering.
 *                          Use 0 (or 1) if it's gonna trigger at once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp            Where to store the breakpoint handle on success.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetInt3Ex(PUVM pUVM, DBGFBPOWNER hOwner, void *pvUser,
                                 VMCPUID idSrcCpu, PCDBGFADDRESS pAddress, uint16_t fFlags,
                                 uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hOwner != NIL_DBGFBPOWNER || pvUser == NULL, VERR_INVALID_PARAMETER);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pAddress), VERR_INVALID_PARAMETER);
    AssertReturn(iHitTrigger <= iHitDisable, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phBp, VERR_INVALID_POINTER);

    int rc = dbgfR3BpEnsureInit(pUVM);
    AssertRCReturn(rc, rc);

    /*
     * Translate & save the breakpoint address into a guest-physical address.
     */
    RTGCPHYS GCPhysBpAddr = NIL_RTGCPHYS;
    rc = DBGFR3AddrToPhys(pUVM, idSrcCpu, pAddress, &GCPhysBpAddr);
    if (RT_SUCCESS(rc))
    {
        /*
         * The physical address from DBGFR3AddrToPhys() is the start of the page,
         * we need the exact byte offset into the page while writing to it in dbgfR3BpInt3Arm().
         */
        GCPhysBpAddr |= (pAddress->FlatPtr & X86_PAGE_OFFSET_MASK);

        PDBGFBPINT pBp = NULL;
        DBGFBP hBp = dbgfR3BpGetByAddr(pUVM, DBGFBPTYPE_INT3, pAddress->FlatPtr, &pBp);
        if (    hBp != NIL_DBGFBP
            &&  pBp->Pub.u.Int3.PhysAddr == GCPhysBpAddr)
        {
            rc = VINF_SUCCESS;
            if (   !DBGF_BP_PUB_IS_ENABLED(&pBp->Pub)
                && (fFlags & DBGF_BP_F_ENABLED))
                rc = dbgfR3BpArm(pUVM, hBp, pBp);
            if (RT_SUCCESS(rc))
            {
                rc = VINF_DBGF_BP_ALREADY_EXIST;
                *phBp = hBp;
            }
            return rc;
        }

        rc = dbgfR3BpAlloc(pUVM, hOwner, pvUser, DBGFBPTYPE_INT3, fFlags, iHitTrigger, iHitDisable, &hBp, &pBp);
        if (RT_SUCCESS(rc))
        {
            pBp->Pub.u.Int3.PhysAddr = GCPhysBpAddr;
            pBp->Pub.u.Int3.GCPtr    = pAddress->FlatPtr;

            /* Add the breakpoint to the lookup tables. */
            rc = dbgfR3BpInt3Add(pUVM, hBp, pBp);
            if (RT_SUCCESS(rc))
            {
                /* Enable the breakpoint if requested. */
                if (fFlags & DBGF_BP_F_ENABLED)
                    rc = dbgfR3BpArm(pUVM, hBp, pBp);
                if (RT_SUCCESS(rc))
                {
                    *phBp = hBp;
                    return VINF_SUCCESS;
                }

                int rc2 = dbgfR3BpInt3Remove(pUVM, hBp, pBp); AssertRC(rc2);
            }

            dbgfR3BpFree(pUVM, hBp, pBp);
        }
    }

    return rc;
}


/**
 * Sets a register breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pAddress        The address of the breakpoint.
 * @param   iHitTrigger     The hit count at which the breakpoint start triggering.
 *                          Use 0 (or 1) if it's gonna trigger at once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   fType           The access type (one of the X86_DR7_RW_* defines).
 * @param   cb              The access size - 1,2,4 or 8 (the latter is AMD64 long mode only.
 *                          Must be 1 if fType is X86_DR7_RW_EO.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetReg(PUVM pUVM, PCDBGFADDRESS pAddress, uint64_t iHitTrigger,
                              uint64_t iHitDisable, uint8_t fType, uint8_t cb, PDBGFBP phBp)
{
    return DBGFR3BpSetRegEx(pUVM, NIL_DBGFBPOWNER, NULL /*pvUser*/, pAddress,
                            DBGF_BP_F_DEFAULT, iHitTrigger, iHitDisable, fType, cb, phBp);
}


/**
 * Sets a register breakpoint - extended version.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hOwner          The owner handle, use NIL_DBGFBPOWNER if no special owner attached.
 * @param   pvUser          Opaque user data to pass in the owner callback.
 * @param   pAddress        The address of the breakpoint.
 * @param   fFlags          Combination of DBGF_BP_F_XXX.
 * @param   iHitTrigger     The hit count at which the breakpoint start triggering.
 *                          Use 0 (or 1) if it's gonna trigger at once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   fType           The access type (one of the X86_DR7_RW_* defines).
 * @param   cb              The access size - 1,2,4 or 8 (the latter is AMD64 long mode only.
 *                          Must be 1 if fType is X86_DR7_RW_EO.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetRegEx(PUVM pUVM, DBGFBPOWNER hOwner, void *pvUser,
                                PCDBGFADDRESS pAddress,  uint16_t fFlags,
                                uint64_t iHitTrigger, uint64_t iHitDisable,
                                uint8_t fType, uint8_t cb, PDBGFBP phBp)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hOwner != NIL_DBGFBPOWNER || pvUser == NULL, VERR_INVALID_PARAMETER);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pAddress), VERR_INVALID_PARAMETER);
    AssertReturn(iHitTrigger <= iHitDisable, VERR_INVALID_PARAMETER);
    AssertReturn(cb > 0 && cb <= 8 && RT_IS_POWER_OF_TWO(cb), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phBp, VERR_INVALID_POINTER);
    switch (fType)
    {
        case X86_DR7_RW_EO:
            AssertMsgReturn(cb == 1, ("fType=%#x cb=%d != 1\n", fType, cb), VERR_INVALID_PARAMETER);
            break;
        case X86_DR7_RW_IO:
        case X86_DR7_RW_RW:
        case X86_DR7_RW_WO:
            break;
        default:
            AssertMsgFailedReturn(("fType=%#x\n", fType), VERR_INVALID_PARAMETER);
    }

    int rc = dbgfR3BpEnsureInit(pUVM);
    AssertRCReturn(rc, rc);

    /*
     * Check if we've already got a matching breakpoint for that address.
     */
    PDBGFBPINT pBp = NULL;
    DBGFBP hBp = dbgfR3BpGetByAddr(pUVM, DBGFBPTYPE_REG, pAddress->FlatPtr, &pBp);
    if (    hBp != NIL_DBGFBP
        &&  pBp->Pub.u.Reg.cb == cb
        &&  pBp->Pub.u.Reg.fType == fType)
    {
        rc = VINF_SUCCESS;
        if (!DBGF_BP_PUB_IS_ENABLED(&pBp->Pub) && (fFlags & DBGF_BP_F_ENABLED))
            rc = dbgfR3BpArm(pUVM, hBp, pBp);
        /* else: We don't disable it when DBGF_BP_F_ENABLED isn't given. */
        if (RT_SUCCESS(rc))
        {
            rc = VINF_DBGF_BP_ALREADY_EXIST;
            *phBp = hBp;
        }
        return rc;
    }

    /*
     * Allocate new breakpoint.
     */
    rc = dbgfR3BpAlloc(pUVM, hOwner, pvUser, DBGFBPTYPE_REG, fFlags, iHitTrigger, iHitDisable, &hBp, &pBp);
    if (RT_SUCCESS(rc))
    {
        pBp->Pub.u.Reg.GCPtr = pAddress->FlatPtr;
        pBp->Pub.u.Reg.fType = fType;
        pBp->Pub.u.Reg.cb    = cb;
        pBp->Pub.u.Reg.iReg  = UINT8_MAX;
        ASMCompilerBarrier();

        /* Assign the proper hardware breakpoint. */
        rc = dbgfR3BpRegAssign(pUVM->pVM, hBp, pBp);
        if (RT_SUCCESS(rc))
        {
            /* Arm the breakpoint. */
            if (fFlags & DBGF_BP_F_ENABLED)
                rc = dbgfR3BpArm(pUVM, hBp, pBp);
            if (RT_SUCCESS(rc))
            {
                *phBp = hBp;
                return VINF_SUCCESS;
            }

            int rc2 = dbgfR3BpRegRemove(pUVM->pVM, hBp, pBp);
            AssertRC(rc2); RT_NOREF(rc2);
        }

        dbgfR3BpFree(pUVM, hBp, pBp);
    }

    return rc;
}


/**
 * This is only kept for now to not mess with the debugger implementation at this point,
 * recompiler breakpoints are not supported anymore (IEM has some API but it isn't implemented
 * and should probably be merged with the DBGF breakpoints).
 */
VMMR3DECL(int) DBGFR3BpSetREM(PUVM pUVM, PCDBGFADDRESS pAddress, uint64_t iHitTrigger,
                              uint64_t iHitDisable, PDBGFBP phBp)
{
    RT_NOREF(pUVM, pAddress, iHitTrigger, iHitDisable, phBp);
    return VERR_NOT_SUPPORTED;
}


/**
 * Sets an I/O port breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   uPort           The first I/O port.
 * @param   cPorts          The number of I/O ports, see DBGFBPIOACCESS_XXX.
 * @param   fAccess         The access we want to break on.
 * @param   iHitTrigger     The hit count at which the breakpoint start
 *                          triggering. Use 0 (or 1) if it's gonna trigger at
 *                          once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetPortIo(PUVM pUVM, RTIOPORT uPort, RTIOPORT cPorts, uint32_t fAccess,
                                 uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    return DBGFR3BpSetPortIoEx(pUVM, NIL_DBGFBPOWNER, NULL /*pvUser*/, uPort, cPorts, fAccess,
                               DBGF_BP_F_DEFAULT, iHitTrigger, iHitDisable, phBp);
}


/**
 * Sets an I/O port breakpoint - extended version.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hOwner          The owner handle, use NIL_DBGFBPOWNER if no special owner attached.
 * @param   pvUser          Opaque user data to pass in the owner callback.
 * @param   uPort           The first I/O port.
 * @param   cPorts          The number of I/O ports, see DBGFBPIOACCESS_XXX.
 * @param   fAccess         The access we want to break on.
 * @param   fFlags          Combination of DBGF_BP_F_XXX.
 * @param   iHitTrigger     The hit count at which the breakpoint start
 *                          triggering. Use 0 (or 1) if it's gonna trigger at
 *                          once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetPortIoEx(PUVM pUVM, DBGFBPOWNER hOwner, void *pvUser,
                                   RTIOPORT uPort, RTIOPORT cPorts, uint32_t fAccess,
                                   uint32_t fFlags, uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hOwner != NIL_DBGFBPOWNER || pvUser == NULL, VERR_INVALID_PARAMETER);
    AssertReturn(!(fAccess & ~DBGFBPIOACCESS_VALID_MASK_PORT_IO), VERR_INVALID_FLAGS);
    AssertReturn(fAccess, VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & ~DBGF_BP_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(fFlags, VERR_INVALID_FLAGS);
    AssertReturn(iHitTrigger <= iHitDisable, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phBp, VERR_INVALID_POINTER);
    AssertReturn(cPorts > 0, VERR_OUT_OF_RANGE);
    AssertReturn((RTIOPORT)(uPort + (cPorts - 1)) >= uPort, VERR_OUT_OF_RANGE);

    int rc = dbgfR3BpPortIoEnsureInit(pUVM);
    AssertRCReturn(rc, rc);

    PDBGFBPINT pBp = NULL;
    DBGFBP hBp = dbgfR3BpPortIoGetByRange(pUVM, uPort, cPorts, &pBp);
    if (   hBp != NIL_DBGFBP
        && pBp->Pub.u.PortIo.uPort == uPort
        && pBp->Pub.u.PortIo.cPorts == cPorts
        && pBp->Pub.u.PortIo.fAccess == fAccess)
    {
        rc = VINF_SUCCESS;
        if (!DBGF_BP_PUB_IS_ENABLED(&pBp->Pub))
            rc = dbgfR3BpArm(pUVM, hBp, pBp);
        if (RT_SUCCESS(rc))
        {
            rc = VINF_DBGF_BP_ALREADY_EXIST;
            *phBp = hBp;
        }
        return rc;
    }

    rc = dbgfR3BpAlloc(pUVM, hOwner, pvUser, DBGFBPTYPE_PORT_IO, fFlags, iHitTrigger, iHitDisable, &hBp, &pBp);
    if (RT_SUCCESS(rc))
    {
        pBp->Pub.u.PortIo.uPort   = uPort;
        pBp->Pub.u.PortIo.cPorts  = cPorts;
        pBp->Pub.u.PortIo.fAccess = fAccess;

        /* Add the breakpoint to the lookup tables. */
        rc = dbgfR3BpPortIoAdd(pUVM, hBp, pBp);
        if (RT_SUCCESS(rc))
        {
            /* Enable the breakpoint if requested. */
            if (fFlags & DBGF_BP_F_ENABLED)
                rc = dbgfR3BpArm(pUVM, hBp, pBp);
            if (RT_SUCCESS(rc))
            {
                *phBp = hBp;
                return VINF_SUCCESS;
            }

            int rc2 = dbgfR3BpPortIoRemove(pUVM, hBp, pBp); AssertRC(rc2);
        }

        dbgfR3BpFree(pUVM, hBp, pBp);
    }

    return rc;
}


/**
 * Sets a memory mapped I/O breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   GCPhys          The first MMIO address.
 * @param   cb              The size of the MMIO range to break on.
 * @param   fAccess         The access we want to break on.
 * @param   iHitTrigger     The hit count at which the breakpoint start
 *                          triggering. Use 0 (or 1) if it's gonna trigger at
 *                          once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetMmio(PUVM pUVM, RTGCPHYS GCPhys, uint32_t cb, uint32_t fAccess,
                               uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    return DBGFR3BpSetMmioEx(pUVM, NIL_DBGFBPOWNER, NULL /*pvUser*/, GCPhys, cb, fAccess,
                             DBGF_BP_F_DEFAULT, iHitTrigger, iHitDisable, phBp);
}


/**
 * Sets a memory mapped I/O breakpoint - extended version.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hOwner          The owner handle, use NIL_DBGFBPOWNER if no special owner attached.
 * @param   pvUser          Opaque user data to pass in the owner callback.
 * @param   GCPhys          The first MMIO address.
 * @param   cb              The size of the MMIO range to break on.
 * @param   fAccess         The access we want to break on.
 * @param   fFlags          Combination of DBGF_BP_F_XXX.
 * @param   iHitTrigger     The hit count at which the breakpoint start
 *                          triggering. Use 0 (or 1) if it's gonna trigger at
 *                          once.
 * @param   iHitDisable     The hit count which disables the breakpoint.
 *                          Use ~(uint64_t) if it's never gonna be disabled.
 * @param   phBp            Where to store the breakpoint handle.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpSetMmioEx(PUVM pUVM, DBGFBPOWNER hOwner, void *pvUser,
                                 RTGCPHYS GCPhys, uint32_t cb, uint32_t fAccess,
                                 uint32_t fFlags, uint64_t iHitTrigger, uint64_t iHitDisable, PDBGFBP phBp)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hOwner != NIL_DBGFBPOWNER || pvUser == NULL, VERR_INVALID_PARAMETER);
    AssertReturn(!(fAccess & ~DBGFBPIOACCESS_VALID_MASK_MMIO), VERR_INVALID_FLAGS);
    AssertReturn(fAccess, VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & ~DBGF_BP_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(fFlags, VERR_INVALID_FLAGS);
    AssertReturn(iHitTrigger <= iHitDisable, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phBp, VERR_INVALID_POINTER);
    AssertReturn(cb, VERR_OUT_OF_RANGE);
    AssertReturn(GCPhys + cb < GCPhys, VERR_OUT_OF_RANGE);

    int rc = dbgfR3BpEnsureInit(pUVM);
    AssertRCReturn(rc, rc);

    return VERR_NOT_IMPLEMENTED;
}


/**
 * Clears a breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   hBp         The handle of the breakpoint which should be removed (cleared).
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpClear(PUVM pUVM, DBGFBP hBp)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hBp != NIL_DBGFBPOWNER, VERR_INVALID_HANDLE);

    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);
    AssertPtrReturn(pBp, VERR_DBGF_BP_NOT_FOUND);

    /* Disarm the breakpoint when it is enabled. */
    if (DBGF_BP_PUB_IS_ENABLED(&pBp->Pub))
    {
        int rc = dbgfR3BpDisarm(pUVM, hBp, pBp);
        AssertRC(rc);
    }

    switch (DBGF_BP_PUB_GET_TYPE(&pBp->Pub))
    {
        case DBGFBPTYPE_REG:
        {
            int rc = dbgfR3BpRegRemove(pUVM->pVM, hBp, pBp);
            AssertRC(rc);
            break;
        }
        case DBGFBPTYPE_INT3:
        {
            int rc = dbgfR3BpInt3Remove(pUVM, hBp, pBp);
            AssertRC(rc);
            break;
        }
        case DBGFBPTYPE_PORT_IO:
        {
            int rc = dbgfR3BpPortIoRemove(pUVM, hBp, pBp);
            AssertRC(rc);
            break;
        }
        default:
            break;
    }

    dbgfR3BpFree(pUVM, hBp, pBp);
    return VINF_SUCCESS;
}


/**
 * Enables a breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   hBp         The handle of the breakpoint which should be enabled.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpEnable(PUVM pUVM, DBGFBP hBp)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hBp != NIL_DBGFBPOWNER, VERR_INVALID_HANDLE);

    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);
    AssertPtrReturn(pBp, VERR_DBGF_BP_NOT_FOUND);

    int rc;
    if (!DBGF_BP_PUB_IS_ENABLED(&pBp->Pub))
        rc = dbgfR3BpArm(pUVM, hBp, pBp);
    else
        rc = VINF_DBGF_BP_ALREADY_ENABLED;

    return rc;
}


/**
 * Disables a breakpoint.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   hBp         The handle of the breakpoint which should be disabled.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpDisable(PUVM pUVM, DBGFBP hBp)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(hBp != NIL_DBGFBPOWNER, VERR_INVALID_HANDLE);

    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);
    AssertPtrReturn(pBp, VERR_DBGF_BP_NOT_FOUND);

    int rc;
    if (DBGF_BP_PUB_IS_ENABLED(&pBp->Pub))
        rc = dbgfR3BpDisarm(pUVM, hBp, pBp);
    else
        rc = VINF_DBGF_BP_ALREADY_DISABLED;

    return rc;
}


/**
 * Enumerate the breakpoints.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   pfnCallback The callback function.
 * @param   pvUser      The user argument to pass to the callback.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3BpEnum(PUVM pUVM, PFNDBGFBPENUM pfnCallback, void *pvUser)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    for (uint32_t idChunk = 0; idChunk < RT_ELEMENTS(pUVM->dbgf.s.aBpChunks); idChunk++)
    {
        PDBGFBPCHUNKR3 pBpChunk = &pUVM->dbgf.s.aBpChunks[idChunk];

        if (pBpChunk->idChunk == DBGF_BP_CHUNK_ID_INVALID)
            break; /* Stop here as the first non allocated chunk means there is no one allocated afterwards as well. */

        if (pBpChunk->cBpsFree < DBGF_BP_COUNT_PER_CHUNK)
        {
            /* Scan the bitmap for allocated entries. */
            int32_t iAlloc = ASMBitFirstSet(pBpChunk->pbmAlloc, DBGF_BP_COUNT_PER_CHUNK);
            if (iAlloc != -1)
            {
                do
                {
                    DBGFBP hBp = DBGF_BP_HND_CREATE(idChunk, (uint32_t)iAlloc);
                    PDBGFBPINT pBp = dbgfR3BpGetByHnd(pUVM, hBp);

                    /* Make a copy of the breakpoints public data to have a consistent view. */
                    DBGFBPPUB BpPub;
                    BpPub.cHits         = ASMAtomicReadU64((volatile uint64_t *)&pBp->Pub.cHits);
                    BpPub.iHitTrigger   = ASMAtomicReadU64((volatile uint64_t *)&pBp->Pub.iHitTrigger);
                    BpPub.iHitDisable   = ASMAtomicReadU64((volatile uint64_t *)&pBp->Pub.iHitDisable);
                    BpPub.hOwner        = ASMAtomicReadU32((volatile uint32_t *)&pBp->Pub.hOwner);
                    BpPub.u16Type       = ASMAtomicReadU16((volatile uint16_t *)&pBp->Pub.u16Type); /* Actually constant. */
                    BpPub.fFlags        = ASMAtomicReadU16((volatile uint16_t *)&pBp->Pub.fFlags);
                    memcpy(&BpPub.u, &pBp->Pub.u, sizeof(pBp->Pub.u)); /* Is constant after allocation. */

                    /* Check if a removal raced us. */
                    if (ASMBitTest(pBpChunk->pbmAlloc, iAlloc))
                    {
                        int rc = pfnCallback(pUVM, pvUser, hBp, &BpPub);
                        if (RT_FAILURE(rc) || rc == VINF_CALLBACK_RETURN)
                            return rc;
                    }

                    iAlloc = ASMBitNextSet(pBpChunk->pbmAlloc, DBGF_BP_COUNT_PER_CHUNK, iAlloc);
                } while (iAlloc != -1);
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Called whenever a breakpoint event needs to be serviced in ring-3 to decide what to do.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The vCPU the breakpoint event happened on.
 *
 * @thread EMT
 */
VMMR3_INT_DECL(int) DBGFR3BpHit(PVM pVM, PVMCPU pVCpu)
{
    /* Send it straight into the debugger?. */
    if (pVCpu->dbgf.s.fBpInvokeOwnerCallback)
    {
        DBGFBP hBp = pVCpu->dbgf.s.hBpActive;
        pVCpu->dbgf.s.fBpInvokeOwnerCallback = false;

        PDBGFBPINT pBp = dbgfR3BpGetByHnd(pVM->pUVM, hBp);
        AssertReturn(pBp, VERR_DBGF_BP_IPE_9);

        /* Resolve owner (can be NIL_DBGFBPOWNER) and invoke callback if there is one. */
        if (pBp->Pub.hOwner != NIL_DBGFBPOWNER)
        {
            PCDBGFBPOWNERINT pBpOwner = dbgfR3BpOwnerGetByHnd(pVM->pUVM, pBp->Pub.hOwner);
            if (pBpOwner)
            {
                VBOXSTRICTRC rcStrict = dbgfR3BpHit(pVM, pVCpu, hBp, pBp, pBpOwner);
                if (VBOXSTRICTRC_VAL(rcStrict) == VINF_SUCCESS)
                {
                    pVCpu->dbgf.s.hBpActive = NIL_DBGFBP;
                    return VINF_SUCCESS;
                }
                if (VBOXSTRICTRC_VAL(rcStrict) != VINF_DBGF_BP_HALT) /* Guru meditation. */
                    return VERR_DBGF_BP_OWNER_CALLBACK_WRONG_STATUS;
                /* else: Halt in the debugger. */
            }
        }
    }

    return DBGFR3EventBreakpoint(pVM, DBGFEVENT_BREAKPOINT);
}

