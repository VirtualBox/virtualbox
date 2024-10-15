/* $Id: DBGFInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Internal header file containing the inlined functions.
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

#ifndef VMM_INCLUDED_SRC_include_DBGFInline_h
#define VMM_INCLUDED_SRC_include_DBGFInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Initializes the given L2 table entry with the given values.
 *
 * @param   pL2Entry            The L2 entry to intialize.
 * @param   hBp                 The breakpoint handle.
 * @param   GCPtr               The GC pointer used as the key (only the upper 6 bytes are used).
 * @param   idxL2Left           The left L2 table index.
 * @param   idxL2Right          The right L2 table index.
 * @param   iDepth              The depth of the node in the tree.
 */
DECLINLINE(void) dbgfBpL2TblEntryInit(PDBGFBPL2ENTRY pL2Entry, DBGFBP hBp, RTGCPTR GCPtr,
                                      uint32_t idxL2Left, uint32_t idxL2Right, uint8_t iDepth)
{
    uint64_t u64GCPtrKeyAndBpHnd1 =   ((uint64_t)hBp & DBGF_BP_L2_ENTRY_BP_1ST_MASK) << DBGF_BP_L2_ENTRY_BP_1ST_SHIFT
                                    | DBGF_BP_INT3_L2_KEY_EXTRACT_FROM_ADDR(GCPtr);
    uint64_t u64LeftRightIdxDepthBpHnd2 =   (((uint64_t)hBp & DBGF_BP_L2_ENTRY_BP_2ND_MASK) >> 16) << DBGF_BP_L2_ENTRY_BP_2ND_SHIFT
                                          | ((uint64_t)iDepth << DBGF_BP_L2_ENTRY_DEPTH_SHIFT)
                                          | ((uint64_t)idxL2Right << DBGF_BP_L2_ENTRY_RIGHT_IDX_SHIFT)
                                          | ((uint64_t)idxL2Left << DBGF_BP_L2_ENTRY_LEFT_IDX_SHIFT);

    ASMAtomicWriteU64(&pL2Entry->u64GCPtrKeyAndBpHnd1, u64GCPtrKeyAndBpHnd1);
    ASMAtomicWriteU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2, u64LeftRightIdxDepthBpHnd2);
}


/**
 * Updates the given L2 table entry with the new pointers.
 *
 * @param   pL2Entry            The L2 entry to update.
 * @param   idxL2Left           The new left L2 table index.
 * @param   idxL2Right          The new right L2 table index.
 * @param   iDepth              The new depth of the tree.
 */
DECLINLINE(void) dbgfBpL2TblEntryUpdate(PDBGFBPL2ENTRY pL2Entry, uint32_t idxL2Left, uint32_t idxL2Right,
                                        uint8_t iDepth)
{
    uint64_t u64LeftRightIdxDepthBpHnd2 = ASMAtomicReadU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2) & DBGF_BP_L2_ENTRY_BP_2ND_L2_ENTRY_MASK;
    u64LeftRightIdxDepthBpHnd2 |=   ((uint64_t)iDepth << DBGF_BP_L2_ENTRY_DEPTH_SHIFT)
                                  | ((uint64_t)idxL2Right << DBGF_BP_L2_ENTRY_RIGHT_IDX_SHIFT)
                                  | ((uint64_t)idxL2Left << DBGF_BP_L2_ENTRY_LEFT_IDX_SHIFT);

    ASMAtomicWriteU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2, u64LeftRightIdxDepthBpHnd2);
}


/**
 * Updates the given L2 table entry with the left pointer.
 *
 * @param   pL2Entry            The L2 entry to update.
 * @param   idxL2Left           The new left L2 table index.
 * @param   iDepth              The new depth of the tree.
 */
DECLINLINE(void) dbgfBpL2TblEntryUpdateLeft(PDBGFBPL2ENTRY pL2Entry, uint32_t idxL2Left, uint8_t iDepth)
{
    uint64_t u64LeftRightIdxDepthBpHnd2 = ASMAtomicReadU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2) & (  DBGF_BP_L2_ENTRY_BP_2ND_L2_ENTRY_MASK
                                                                                                     | DBGF_BP_L2_ENTRY_RIGHT_IDX_MASK);

    u64LeftRightIdxDepthBpHnd2 |=   ((uint64_t)iDepth << DBGF_BP_L2_ENTRY_DEPTH_SHIFT)
                                  | ((uint64_t)idxL2Left << DBGF_BP_L2_ENTRY_LEFT_IDX_SHIFT);

    ASMAtomicWriteU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2, u64LeftRightIdxDepthBpHnd2);
}


/**
 * Updates the given L2 table entry with the right pointer.
 *
 * @param   pL2Entry            The L2 entry to update.
 * @param   idxL2Right          The new right L2 table index.
 * @param   iDepth              The new depth of the tree.
 */
DECLINLINE(void) dbgfBpL2TblEntryUpdateRight(PDBGFBPL2ENTRY pL2Entry, uint32_t idxL2Right, uint8_t iDepth)
{
    uint64_t u64LeftRightIdxDepthBpHnd2 = ASMAtomicReadU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2) & (  DBGF_BP_L2_ENTRY_BP_2ND_L2_ENTRY_MASK
                                                                                                     | DBGF_BP_L2_ENTRY_LEFT_IDX_MASK);

    u64LeftRightIdxDepthBpHnd2 |=   ((uint64_t)iDepth << DBGF_BP_L2_ENTRY_DEPTH_SHIFT)
                                  | ((uint64_t)idxL2Right << DBGF_BP_L2_ENTRY_RIGHT_IDX_SHIFT);

    ASMAtomicWriteU64(&pL2Entry->u64LeftRightIdxDepthBpHnd2, u64LeftRightIdxDepthBpHnd2);
}

#ifdef IN_RING3
/**
 * Returns the internal breakpoint owner state for the given handle.
 *
 * @returns Pointer to the internal breakpoint owner state or NULL if the handle is invalid.
 * @param   pUVM                The user mode VM handle.
 * @param   hBpOwner            The breakpoint owner handle to resolve.
 */
DECLINLINE(PDBGFBPOWNERINT) dbgfR3BpOwnerGetByHnd(PUVM pUVM, DBGFBPOWNER hBpOwner)
{
    AssertReturn(hBpOwner < DBGF_BP_OWNER_COUNT_MAX, NULL);
    AssertPtrReturn(pUVM->dbgf.s.pbmBpOwnersAllocR3, NULL);

    AssertReturn(ASMBitTest(pUVM->dbgf.s.pbmBpOwnersAllocR3, hBpOwner), NULL);
    return &pUVM->dbgf.s.paBpOwnersR3[hBpOwner];
}
#endif

#endif /* !VMM_INCLUDED_SRC_include_DBGFInline_h */
