/* $Id: IEMInlineMem-armv8.h 110422 2025-07-27 22:59:43Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined Memory Functions, ARMv8 target.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInlineMem_armv8_h
#define VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInlineMem_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/errcore.h>




/** @name   Memory access.
 *
 * @{
 */

/**
 * Checks whether alignment checks are enabled or not.
 *
 * @returns true if enabled, false if not.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_FORCE_INLINE(bool) iemMemAreAlignmentChecksEnabled(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return RT_BOOL(pVCpu->iem.s.fExec & IEM_F_ARM_A);
}


/*
 * Instantiate R/W inline templates.
 */

//#define TMPL_MEM_NO_INLINE /* for now */

/** @def TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK
 * Used to check if an unaligned access is if within the page and won't
 * trigger an \#AC.
 *
 * This can also be used to deal with misaligned accesses on platforms that are
 * senstive to such if desires.
 */
#if 1
# define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) \
    (   ((a_GCPtrEff) & GUEST_PAGE_OFFSET_MASK) <= GUEST_PAGE_SIZE - sizeof(a_TmplMemType) \
     && !((a_pVCpu)->iem.s.fExec & IEM_F_ARM_A) )
#else
# define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) 0
#endif

/**
 * Helper for IEMAllMemRWTmplInline-armv8.cpp.h that does the TLB lookup.
 *
 * Pairs with TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH().
 */
#define TMPL_MEM_IF_TLB_LOOKUP_MATCH(a_GCPtrMem, a_fTlbePrivileged, a_fTlbeUser, a_fTlbeCommon) \
    /* \
     * TLB lookup. \
     */ \
    uint64_t const      uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, (a_GCPtrMem)); \
    PCIEMTLBENTRY const pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev); \
    if (RT_LIKELY(pTlbe->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision))) \
    { \
        /* \
         * Check TLB page table level access flags. \
         */ \
        uint64_t const fTlbeAcc            = (IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) > 0 ? a_fTlbePrivileged : a_fTlbeUser) \
                                           | a_fTlbeCommon \
                                           | IEMTLBE_F_PG_UNASSIGNED \
                                           | IEMTLBE_F_NO_MAPPINGR3 \
                                           | IEMTLBE_F_EFF_DEVICE \
                                           | IEMTLBE_F_PHYS_REV \
                                           | IEMTLBE_F_REGIME_MASK \
                                           | IEMTLBE_F_NG \
                                           | IEMTLBE_F_S1_ASID \
                                           | IEMTLBE_F_S2_VMID; \
        uint64_t const uTlbPhysRevAndStuff = IEMARM_IS_POSITIVE_64BIT_ADDR(GCPtrMem) \
                                           ? pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff0 \
                                           : pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff1; \
        if (RT_LIKELY(      (pTlbe->fFlagsAndPhysRev & fTlbeAcc) \
                         == (uTlbPhysRevAndStuff     & (IEMTLBE_F_PHYS_REV | IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID \
                                                        | IEMTLBE_F_NG     | IEMTLBE_F_S1_ASID)) \
                      ||    (pTlbe->fFlagsAndPhysRev & fTlbeAcc) \
                         == (uTlbPhysRevAndStuff     & (IEMTLBE_F_PHYS_REV | IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID)) ))

/** Counterpart to TMPL_MEM_IF_TLB_LOOKUP_MATCH(). */
#define TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH() \
    } ((void)0)


#define TMPL_MEM_WITH_ATOMIC_MAPPING
#define TMPL_MEM_NO_PAIR

#define TMPL_MEM_TYPE       uint8_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  1
#define TMPL_MEM_FN_SUFF    U8
#define TMPL_MEM_FMT_TYPE   "%#04x"
#define TMPL_MEM_FMT_DESC   "byte"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"

#define TMPL_MEM_TYPE       uint16_t
#define TMPL_MEM_TYPE_ALIGN 1
#define TMPL_MEM_TYPE_SIZE  2
#define TMPL_MEM_FN_SUFF    U16
#define TMPL_MEM_FMT_TYPE   "%#06x"
#define TMPL_MEM_FMT_DESC   "word"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"

#undef  TMPL_MEM_NO_PAIR

#define TMPL_MEM_TYPE       uint32_t
#define TMPL_MEM_TYPE_ALIGN 3
#define TMPL_MEM_TYPE_SIZE  4
#define TMPL_MEM_FN_SUFF    U32
#define TMPL_MEM_FMT_TYPE   "%#010x"
#define TMPL_MEM_FMT_DESC   "dword"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"

#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN 7
#define TMPL_MEM_TYPE_SIZE  8
#define TMPL_MEM_FN_SUFF    U64
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"

/* Every template relying on unaligned accesses inside a page not being okay should go below. (?) */
#undef  TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK
#define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) 0

#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN 15
#define TMPL_MEM_TYPE_SIZE  16
#define TMPL_MEM_FN_SUFF    U128
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"

#define TMPL_MEM_NO_PAIR
#undef  TMPL_MEM_WITH_ATOMIC_MAPPING

#define TMPL_MEM_NO_MAPPING
#define TMPL_MEM_TYPE       RTUINT256U
#define TMPL_MEM_TYPE_ALIGN 31
#define TMPL_MEM_TYPE_SIZE  32
#define TMPL_MEM_FN_SUFF    U256
#define TMPL_MEM_FMT_TYPE   "%.32Rhxs"
#define TMPL_MEM_FMT_DESC   "qqword"
#include "IEMAllMemRWTmplInline-armv8.cpp.h"
#undef  TMPL_MEM_NO_MAPPING

#undef  TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK
#undef  TMPL_MEM_NO_PAIR

/** @} */

#endif /* !VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInlineMem_armv8_h */
