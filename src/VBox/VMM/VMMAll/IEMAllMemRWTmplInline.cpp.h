/* $Id: IEMAllMemRWTmplInline.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined R/W Memory Functions Template.
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


/* Check template parameters. */
#ifndef TMPL_MEM_TYPE
# error "TMPL_MEM_TYPE is undefined"
#endif
#ifndef TMPL_MEM_TYPE_SIZE
# error "TMPL_MEM_TYPE_SIZE is undefined"
#endif
#ifndef TMPL_MEM_TYPE_ALIGN
# error "TMPL_MEM_TYPE_ALIGN is undefined"
#endif
#ifndef TMPL_MEM_FN_SUFF
# error "TMPL_MEM_FN_SUFF is undefined"
#endif
#ifndef TMPL_MEM_FMT_TYPE
# error "TMPL_MEM_FMT_TYPE is undefined"
#endif
#ifndef TMPL_MEM_FMT_DESC
# error "TMPL_MEM_FMT_DESC is undefined"
#endif


/** Helper for checking if @a a_GCPtr is acceptably aligned and fully within
 *  the page for a TMPL_MEM_TYPE.  */
#if TMPL_MEM_TYPE_ALIGN + 1 < TMPL_MEM_TYPE_SIZE
# define TMPL_MEM_ALIGN_CHECK(a_GCPtr) (   (   !((a_GCPtr) & TMPL_MEM_TYPE_ALIGN) \
                                            && ((a_GCPtr) & GUEST_PAGE_OFFSET_MASK) <= GUEST_PAGE_SIZE - sizeof(TMPL_MEM_TYPE)) \
                                        || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, (a_GCPtr), TMPL_MEM_TYPE))
#else
# define TMPL_MEM_ALIGN_CHECK(a_GCPtr) (   !((a_GCPtr) & TMPL_MEM_TYPE_ALIGN) /* If aligned, it will be within the page. */ \
                                        || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, (a_GCPtr), TMPL_MEM_TYPE))
#endif

/**
 * Values have to be passed by reference if larger than uint64_t.
 *
 * This is a restriction of the Visual C++ AMD64 calling convention,
 * the gcc AMD64 and ARM64 ABIs can easily pass and return to 128-bit via
 * registers.  For larger values like RTUINT256U, Visual C++ AMD and ARM64
 * passes them by hidden reference, whereas the gcc AMD64 ABI will use stack.
 *
 * So, to avoid passing anything on the stack, we just explictly pass values by
 * reference (pointer) if they are larger than uint64_t.  This ASSUMES 64-bit
 * host.
 */
#if TMPL_MEM_TYPE_SIZE > 8
# define TMPL_MEM_BY_REF
#else
# undef TMPL_MEM_BY_REF
#endif


#ifdef IEM_WITH_SETJMP


/*********************************************************************************************************************************
*   Fetches                                                                                                                      *
*********************************************************************************************************************************/

/**
 * Inlined fetch function that longjumps on error.
 *
 * @note The @a iSegRef is not allowed to be UINT8_MAX!
 */
#ifdef TMPL_MEM_BY_REF
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *pValue, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
#else
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
#endif
{
    AssertCompile(sizeof(TMPL_MEM_TYPE) == TMPL_MEM_TYPE_SIZE);
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR GCPtrEff = iemMemApplySegmentToReadJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_NO_MAPPINGR3  | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Fetch and return the data.
                 */
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
#  ifdef TMPL_MEM_BY_REF
                *pValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
                LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                         iSegReg, GCPtrMem, GCPtrEff, pValue));
                return;
#  else
                TMPL_MEM_TYPE const uRet = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
                LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: " TMPL_MEM_FMT_TYPE "\n",
                                         iSegReg, GCPtrMem, GCPtrEff, uRet));
                return uRet;
#  endif
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    LogEx(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
# endif
# ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, pValue, iSegReg, GCPtrMem);
# else
    return RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iSegReg, GCPtrMem);
# endif
}


/**
 * Inlined flat addressing fetch function that longjumps on error.
 */
# ifdef TMPL_MEM_BY_REF
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *pValue, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
# else
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFlatFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
# endif
{
    AssertMsg(   (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
              || (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
              || (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT, ("%#x\n", pVCpu->iem.s.fExec));
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_NO_MAPPINGR3  | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Fetch and return the dword
                 */
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
#  ifdef TMPL_MEM_BY_REF
                *pValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
                LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                         GCPtrMem, pValue));
                return;
#  else
                TMPL_MEM_TYPE const uRet = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
                LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uRet));
                return uRet;
#  endif
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    LogEx(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
# ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, pValue, UINT8_MAX, GCPtrMem);
# else
    return RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, UINT8_MAX, GCPtrMem);
# endif
}


/*********************************************************************************************************************************
*   Stores                                                                                                                       *
*********************************************************************************************************************************/
# ifndef TMPL_MEM_NO_STORE

/**
 * Inlined store function that longjumps on error.
 *
 * @note The @a iSegRef is not allowed to be UINT8_MAX!
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem,
#  ifdef TMPL_MEM_BY_REF
                                                 TMPL_MEM_TYPE const *pValue) IEM_NOEXCEPT_MAY_LONGJMP
#  else
                                                 TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
#  endif
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | IEMTLBE_F_NO_MAPPINGR3   | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Store the value and return.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
#   ifdef TMPL_MEM_BY_REF
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = *pValue;
                Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs (%04x:%RX64)\n",
                                          iSegReg, GCPtrMem, GCPtrEff, pValue, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
#   else
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = uValue;
                Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: " TMPL_MEM_FMT_TYPE " (%04x:%RX64)\n",
                                          iSegReg, GCPtrMem, GCPtrEff, uValue, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
#   endif
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log6Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
#  endif
#  ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iSegReg, GCPtrMem, pValue);
#  else
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iSegReg, GCPtrMem, uValue);
#  endif
}


/**
 * Inlined flat addressing store function that longjumps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatStoreData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
#  ifdef TMPL_MEM_BY_REF
                                                    TMPL_MEM_TYPE const *pValue) IEM_NOEXCEPT_MAY_LONGJMP
#  else
                                                    TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
#  endif
{
    AssertMsg(   (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
              || (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
              || (pVCpu->iem.s.fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT, ("%#x\n", pVCpu->iem.s.fExec));
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | IEMTLBE_F_NO_MAPPINGR3   | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Store the value and return.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
#   ifdef TMPL_MEM_BY_REF
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK] = *pValue;
                Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                          GCPtrMem, pValue));
#   else
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK] = uValue;
                Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
#   endif
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log6Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
#  ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, UINT8_MAX, GCPtrMem, pValue);
#  else
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, UINT8_MAX, GCPtrMem, uValue);
#  endif
}

# endif /* !TMPL_MEM_NO_STORE */


/*********************************************************************************************************************************
*   Mapping / Direct Memory Access                                                                                               *
*********************************************************************************************************************************/
# ifndef TMPL_MEM_NO_MAPPING

/**
 * Inlined read-write memory mapping function that longjumps on error.
 *
 * Almost identical to RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtJmp).
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                 uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM RW/map " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %p\n",
                                          iSegReg, GCPtrMem, GCPtrEff, &pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwSafeJmp)(pVCpu, pbUnmapInfo, iSegReg, GCPtrMem);
}


/**
 * Inlined flat read-write memory mapping function that longjumps on error.
 *
 * Almost identical to RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,AtJmp).
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,RwJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the address doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM RW/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwSafeJmp)(pVCpu, pbUnmapInfo, UINT8_MAX, GCPtrMem);
}

#  ifdef TMPL_MEM_WITH_ATOMIC_MAPPING

/**
 * Inlined atomic read-write memory mapping function that longjumps on error.
 *
 * Almost identical to RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwJmp).
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                 uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(!(GCPtrEff & TMPL_MEM_TYPE_ALIGN))) /* strictly aligned, otherwise do fall back which knows th details. */
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM AT/map " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %p\n",
                                          iSegReg, GCPtrMem, GCPtrEff, &pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtSafeJmp)(pVCpu, pbUnmapInfo, iSegReg, GCPtrMem);
}


/**
 * Inlined flat read-write memory mapping function that longjumps on error.
 *
 * Almost identical to RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,RwJmp).
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,AtJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the address doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(!(GCPtrMem & TMPL_MEM_TYPE_ALIGN))) /* strictly aligned, otherwise do fall back which knows th details. */
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM AT/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtSafeJmp)(pVCpu, pbUnmapInfo, UINT8_MAX, GCPtrMem);
}

#  endif /* TMPL_MEM_WITH_ATOMIC_MAPPING */

/**
 * Inlined write-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                 uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY   | IEMTLBE_F_PT_NO_WRITE
                                                         | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM WO/map " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %p\n",
                                          iSegReg, GCPtrMem, GCPtrEff, &pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoSafeJmp)(pVCpu, pbUnmapInfo, iSegReg, GCPtrMem);
}


/**
 * Inlined flat write-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,WoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the address doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM WO/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoSafeJmp)(pVCpu, pbUnmapInfo, UINT8_MAX, GCPtrMem);
}


/**
 * Inlined read-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE const *)
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                 uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Convert from segmented to flat address and check that it doesn't cross a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToReadJmp(pVCpu, iSegReg, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log3Ex(LOG_GROUP_IEM_MEM,("IEM RO/map " TMPL_MEM_FMT_DESC " %d|%RGv=%RGv: %p\n",
                                          iSegReg, GCPtrMem, GCPtrEff, &pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log4Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %u:%RGv falling back\n", LOG_FN_NAME, iSegReg, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoSafeJmp)(pVCpu, pbUnmapInfo, iSegReg, GCPtrMem);
}


/**
 * Inlined read-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE const *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,RoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the address doesn't cross a page boundrary.
     */
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Return the address.
                 */
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log3Ex(LOG_GROUP_IEM_MEM,("IEM RO/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log4Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoSafeJmp)(pVCpu, pbUnmapInfo, UINT8_MAX, GCPtrMem);
}

# endif /* !TMPL_MEM_NO_MAPPING */


/*********************************************************************************************************************************
*   Stack Access                                                                                                                 *
*********************************************************************************************************************************/
# ifdef TMPL_MEM_WITH_STACK
#  if TMPL_MEM_TYPE_SIZE > 8
#   error "Stack not supported for this type size - please #undef TMPL_MEM_WITH_STACK"
#  endif
#  if TMPL_MEM_TYPE_SIZE > 1 && TMPL_MEM_TYPE_ALIGN + 1 < TMPL_MEM_TYPE_SIZE
#   error "Stack not supported for this alignment size - please #undef TMPL_MEM_WITH_STACK"
#  endif
#  ifdef IEM_WITH_SETJMP

/**
 * Stack store function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#   if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Apply segmentation and check that the item doesn't cross a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#    if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#    endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the store and return.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrEff, uValue));
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = uValue;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#   endif
    RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, uValue);
}


#   ifdef TMPL_WITH_PUSH_SREG
/**
 * Stack segment store function that longjmps on error.
 *
 * For a detailed discussion of the behaviour see the fallback functions
 * iemMemStoreStackUxxSRegSafeJmp and iemMemStackPushUxxSRegSafeJmp.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SRegJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                      TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Apply segmentation to the address and check that the item doesn't cross
     * a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(   !(GCPtrEff & (sizeof(uint16_t) - 1U))
                  || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, GCPtrEff, uint16_t) ))
#     endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#      ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#      endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE " [sreg]\n", GCPtrEff, uValue));
                *(uint16_t *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = (uint16_t)uValue;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#     endif
    RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SRegSafeJmp)(pVCpu, GCPtrMem, uValue);
}
#   endif /* TMPL_WITH_PUSH_SREG */


/**
 * Flat stack store function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatStoreStack,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                      TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(   IEM_IS_64BIT_CODE(pVCpu)
           || (    pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig
                && pVCpu->cpum.GstCtx.ss.Attr.n.u4Type == X86_SEL_TYPE_RW_ACC
                && pVCpu->cpum.GstCtx.ss.u32Limit == UINT32_MAX
                && pVCpu->cpum.GstCtx.ss.u64Base == 0));

#   if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the item doesn't cross a page boundrary.
     */
#    if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#    endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#    ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#    endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (<-%RX64): " TMPL_MEM_FMT_TYPE "\n",
                                           GCPtrMem, pVCpu->cpum.GstCtx.rsp, uValue));
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK] = uValue;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#   endif
    RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, uValue);
}

#   ifdef TMPL_WITH_PUSH_SREG
/**
 * Flat stack segment store function that longjmps on error.
 *
 * For a detailed discussion of the behaviour see the fallback functions
 * iemMemStoreStackUxxSRegSafeJmp and iemMemStackPushUxxSRegSafeJmp.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatStoreStack,TMPL_MEM_FN_SUFF,SRegJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                          TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the item doesn't cross a page boundrary.
     */
    if (RT_LIKELY(   !(GCPtrMem & (sizeof(uint16_t) - 1))
                  || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, GCPtrMem, uint16_t) ))
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (<-%RX64): " TMPL_MEM_FMT_TYPE " [sreg]\n",
                                           GCPtrMem, pVCpu->cpum.GstCtx.rsp, uValue));
                *(uint16_t *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK] = (uint16_t)uValue;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#    endif
    RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SRegSafeJmp)(pVCpu, GCPtrMem, uValue);
}
#   endif /* TMPL_WITH_PUSH_SREG */


/**
 * Stack fetch function that longjmps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFetchStack,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#   if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Apply segmentation to the address and check that the item doesn't cross
     * a page boundrary.
     */
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, sizeof(TMPL_MEM_TYPE), GCPtrMem);
#    if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#    endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the pop.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                TMPL_MEM_TYPE const uValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
                Log9Ex(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrEff, uValue));
                return uValue;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log10Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#   endif
    return RT_CONCAT3(iemMemFetchStack,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem);
}


/**
 * Flat stack fetch function that longjmps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFlatFetchStack,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#   if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that the item doesn't cross a page boundrary.
     */
#    if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
#    endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the pop.
                 */
#    ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#    endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                TMPL_MEM_TYPE const uValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_PAGE_OFFSET_MASK];
                Log9Ex(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
                return uValue;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log10Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#   endif
    return RT_CONCAT3(iemMemFetchStack,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem);
}


/**
 * Stack push function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Decrement the stack pointer (prep), apply segmentation and check that
     * the item doesn't cross a page boundrary.
     */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPush(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, sizeof(TMPL_MEM_TYPE), GCPtrTop);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#   ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#   endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE "\n",
                                           GCPtrEff, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = uValue;
                pVCpu->cpum.GstCtx.rsp = uNewRsp;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#  endif
    RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, uValue);
}


/**
 * Stack pop greg function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStackPopGReg,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(iGReg < 16);

#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Increment the stack pointer (prep), apply segmentation and check that
     * the item doesn't cross a page boundrary.
     */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPop(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, sizeof(TMPL_MEM_TYPE), GCPtrTop);
#  if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrEff)))
#  endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the pop.
                 */
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                TMPL_MEM_TYPE const uValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
                Log9Ex(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " (r%u)\n",
                                          GCPtrEff, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue, iGReg));
                pVCpu->cpum.GstCtx.rsp               = uNewRsp;  /* must be first for 16-bit */
#  if TMPL_MEM_TYPE_SIZE == 2
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u16 = uValue;
#  elif TMPL_MEM_TYPE_SIZE == 4 || TMPL_MEM_TYPE_SIZE == 8
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u   = uValue;
#  else
#   error "TMPL_MEM_TYPE_SIZE"
#  endif
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log10Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#  endif
    RT_CONCAT3(iemMemStackPopGReg,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iGReg);
}

#   ifdef TMPL_WITH_PUSH_SREG
/**
 * Stack segment push function that longjmps on error.
 *
 * For a detailed discussion of the behaviour see the fallback functions
 * iemMemStackPushUxxSRegSafeJmp.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SRegJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /* See fallback for details on this weirdness: */
    bool const    fIsIntel = IEM_IS_GUEST_CPU_INTEL(pVCpu);
    uint8_t const cbAccess = fIsIntel && !IEM_IS_REAL_MODE(pVCpu) ? sizeof(uint16_t) : sizeof(TMPL_MEM_TYPE);

    /*
     * Decrement the stack pointer (prep), apply segmentation and check that
     * the item doesn't cross a page boundrary.
     */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPush(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);
    RTGCPTR const GCPtrEff = iemMemApplySegmentToWriteJmp(pVCpu, X86_SREG_SS, cbAccess, GCPtrTop);
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(   !(GCPtrEff & (cbAccess - 1U))
                  || (   cbAccess == sizeof(uint16_t)
                      ? TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, GCPtrEff, uint16_t)
                      : TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, GCPtrEff, TMPL_MEM_TYPE) ) ))
#    endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(GCPtrEff);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#    ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#    endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                if (cbAccess == sizeof(uint16_t))
                {
                    Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR 'word' SS|%RGv (%RX64->%RX64): %#06x [sreg/i]\n",
                                               GCPtrEff, pVCpu->cpum.GstCtx.rsp, uNewRsp, (uint16_t)uValue));
                    *(uint16_t *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK] = (uint16_t)uValue;
                }
                else
                {
                    TMPL_MEM_TYPE * const puSlot = (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrEff & GUEST_PAGE_OFFSET_MASK];
                    if (fIsIntel)
                    {
                        Assert(IEM_IS_REAL_MODE(pVCpu));
                        uValue = (uint16_t)uValue | (pVCpu->cpum.GstCtx.eflags.u & (UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK));
                        Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " [sreg/ir]\n",
                                                   GCPtrEff, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
                    }
                    else
                        Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " [sreg]\n",
                                                   GCPtrEff, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
                    *puSlot = uValue;
                }
                pVCpu->cpum.GstCtx.rsp = uNewRsp;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrEff));
#    endif
    RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SRegSafeJmp)(pVCpu, uValue);
}
#   endif /* TMPL_WITH_PUSH_SREG */

#   if TMPL_MEM_TYPE_SIZE != 8

/**
 * 32-bit flat stack push function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlat32StackPush,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(   pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig
           && pVCpu->cpum.GstCtx.ss.Attr.n.u4Type == X86_SEL_TYPE_RW_ACC
           && pVCpu->cpum.GstCtx.ss.u32Limit == UINT32_MAX
           && pVCpu->cpum.GstCtx.ss.u64Base == 0);
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Calculate the new stack pointer and check that the item doesn't cross a page boundrary.
     */
    uint32_t const uNewEsp = pVCpu->cpum.GstCtx.esp - sizeof(TMPL_MEM_TYPE);
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(uNewEsp)))
#     endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV((RTGCPTR)uNewEsp); /* Doesn't work w/o casting to RTGCPTR (win /3 hangs). */
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RX32 (<-%RX32): " TMPL_MEM_FMT_TYPE "\n",
                                           uNewEsp, pVCpu->cpum.GstCtx.esp, uValue));
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[uNewEsp & GUEST_PAGE_OFFSET_MASK] = uValue;
                pVCpu->cpum.GstCtx.rsp = uNewEsp;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RX32 falling back\n", LOG_FN_NAME, uNewEsp));
#    endif
    RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, uValue);
}


/**
 * 32-bit flat stack greg pop function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlat32StackPopGReg,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(iGReg < 16);
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Calculate the new stack pointer and check that the item doesn't cross a page boundrary.
     */
    uint32_t const uOldEsp = pVCpu->cpum.GstCtx.esp;
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(uOldEsp)))
#     endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV((RTGCPTR)uOldEsp); /* Cast is required! 2023-08-11 */
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the pop and update the register values.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                TMPL_MEM_TYPE const uValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[uOldEsp & GUEST_PAGE_OFFSET_MASK];
                pVCpu->cpum.GstCtx.rsp = uOldEsp + sizeof(TMPL_MEM_TYPE); /* must be first for 16-bit */
#     if TMPL_MEM_TYPE_SIZE == 2
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u16 = uValue;
#     elif TMPL_MEM_TYPE_SIZE == 4
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u   = uValue;
#     else
#      error "TMPL_MEM_TYPE_SIZE"
#     endif
                Log9Ex(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " SS|%RX32 (->%RX32): " TMPL_MEM_FMT_TYPE " (r%u)\n",
                                          uOldEsp, uOldEsp + sizeof(TMPL_MEM_TYPE), uValue, iGReg));
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log10Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RX32 falling back\n", LOG_FN_NAME, uOldEsp));
#    endif
    RT_CONCAT3(iemMemStackPopGReg,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iGReg);
}

#   endif /* TMPL_MEM_TYPE_SIZE != 8*/

#   ifdef TMPL_WITH_PUSH_SREG
/**
 * 32-bit flat stack segment push function that longjmps on error.
 *
 * For a detailed discussion of the behaviour see the fallback functions
 * iemMemStackPushUxxSRegSafeJmp.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlat32StackPush,TMPL_MEM_FN_SUFF,SRegJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /* See fallback for details on this weirdness: */
    bool const    fIsIntel = IEM_IS_GUEST_CPU_INTEL(pVCpu);
    uint8_t const cbAccess = fIsIntel && !IEM_IS_REAL_MODE(pVCpu) ? sizeof(uint16_t) : sizeof(TMPL_MEM_TYPE);

    /*
     * Calculate the new stack pointer and check that the item doesn't cross a page boundrary.
     */
    uint32_t const uNewEsp = pVCpu->cpum.GstCtx.esp - sizeof(TMPL_MEM_TYPE);
    if (RT_LIKELY(   !(uNewEsp & (cbAccess - 1))
                  || (cbAccess == sizeof(uint16_t)
                      ? TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, uNewEsp, uint16_t)
                      : TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, uNewEsp, TMPL_MEM_TYPE))  ))
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV((RTGCPTR)uNewEsp); /* Doesn't work w/o casting to RTGCPTR (win /3 hangs). */
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                if (cbAccess == sizeof(uint16_t))
                {
                    Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR 'word' SS|%RX32 (<-%RX32): %#06x [sreg/i]\n",
                                               uNewEsp, pVCpu->cpum.GstCtx.esp, (uint16_t)uValue));
                    *(uint16_t *)&pTlbe->pbMappingR3[uNewEsp & GUEST_PAGE_OFFSET_MASK] = (uint16_t)uValue;
                }
                else
                {
                    TMPL_MEM_TYPE * const puSlot = (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[uNewEsp & GUEST_PAGE_OFFSET_MASK];
                    if (fIsIntel)
                    {
                        Assert(IEM_IS_REAL_MODE(pVCpu));
                        uValue = (uint16_t)uValue | (pVCpu->cpum.GstCtx.eflags.u & (UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK));
                        Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RX32 (<-RX32): " TMPL_MEM_FMT_TYPE " [sreg/ir]\n",
                                                   uNewEsp, pVCpu->cpum.GstCtx.esp, uValue));
                    }
                    else
                        Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RX32 (<-RX32): " TMPL_MEM_FMT_TYPE " [sreg]\n",
                                                   uNewEsp, pVCpu->cpum.GstCtx.esp, uValue));
                    *puSlot = uValue;
                }
                pVCpu->cpum.GstCtx.rsp = uNewEsp;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RX32 falling back\n", LOG_FN_NAME, uNewEsp));
#    endif
    RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SRegSafeJmp)(pVCpu, uValue);
}
#   endif /* TMPL_WITH_PUSH_SREG */

#   if TMPL_MEM_TYPE_SIZE != 4

/**
 * 64-bit flat stack push function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlat64StackPush,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Calculate the new stack pointer and check that the item doesn't cross a page boundrary.
     */
    uint64_t const uNewRsp = pVCpu->cpum.GstCtx.rsp - sizeof(TMPL_MEM_TYPE);
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(uNewRsp)))
#     endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(uNewRsp);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_WRITE
                                                         | IEMTLBE_F_PT_NO_ACCESSED | IEMTLBE_F_PT_NO_DIRTY
                                                         | IEMTLBE_F_PT_NO_WRITE    | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                Log11Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " SS|%RX64 (<-%RX64): " TMPL_MEM_FMT_TYPE "\n",
                                           uNewRsp, pVCpu->cpum.GstCtx.esp, uValue));
                *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[uNewRsp & GUEST_PAGE_OFFSET_MASK] = uValue;
                pVCpu->cpum.GstCtx.rsp = uNewRsp;
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log12Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RX64 falling back\n", LOG_FN_NAME, uNewRsp));
#    endif
    RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, uValue);
}


/**
 * 64-bit flat stack pop function that longjmps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlat64StackPopGReg,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(iGReg < 16);
#    if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Calculate the new stack pointer and check that the item doesn't cross a page boundrary.
     */
    uint64_t const uOldRsp = pVCpu->cpum.GstCtx.rsp;
#     if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(uOldRsp)))
#     endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(uOldRsp);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
        if (RT_LIKELY(   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
                      || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)))
        {
            /*
             * Check TLB page table level access flags.
             */
            AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
            uint64_t const fNoUser = (IEM_GET_CPL(pVCpu) + 1) & IEMTLBE_F_PT_NO_USER;
            if (RT_LIKELY(   (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PHYS_REV       | IEMTLBE_F_NO_MAPPINGR3
                                                         | IEMTLBE_F_PG_UNASSIGNED  | IEMTLBE_F_PG_NO_READ
                                                         | IEMTLBE_F_PT_NO_ACCESSED | fNoUser))
                          == pVCpu->iem.s.DataTlb.uTlbPhysRev))
            {
                /*
                 * Do the push and return.
                 */
#     ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#     endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_PAGE_OFFSET_MASK));
                TMPL_MEM_TYPE const uValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[uOldRsp & GUEST_PAGE_OFFSET_MASK];
                pVCpu->cpum.GstCtx.rsp = uOldRsp + sizeof(TMPL_MEM_TYPE);  /* must be first for 16-bit */
#     if TMPL_MEM_TYPE_SIZE == 2
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u16 = uValue;
#     elif TMPL_MEM_TYPE_SIZE == 8
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u   = uValue;
#     else
#      error "TMPL_MEM_TYPE_SIZE"
#     endif
                Log9Ex(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " SS|%RX64 (->%RX64): " TMPL_MEM_FMT_TYPE " (r%u)\n",
                                          uOldRsp, uOldRsp + sizeof(TMPL_MEM_TYPE), uValue, iGReg));
                return;
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log10Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RX64 falling back\n", LOG_FN_NAME, uOldRsp));
#   endif
    RT_CONCAT3(iemMemStackPopGReg,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, iGReg);
}

#   endif /* TMPL_MEM_TYPE_SIZE != 4 */

#  endif /* IEM_WITH_SETJMP */
# endif /* TMPL_MEM_WITH_STACK */


#endif /* IEM_WITH_SETJMP */

#undef TMPL_MEM_TYPE
#undef TMPL_MEM_TYPE_ALIGN
#undef TMPL_MEM_TYPE_SIZE
#undef TMPL_MEM_FN_SUFF
#undef TMPL_MEM_FMT_TYPE
#undef TMPL_MEM_FMT_DESC
#undef TMPL_MEM_NO_STORE
#undef TMPL_MEM_ALIGN_CHECK
#undef TMPL_MEM_BY_REF

