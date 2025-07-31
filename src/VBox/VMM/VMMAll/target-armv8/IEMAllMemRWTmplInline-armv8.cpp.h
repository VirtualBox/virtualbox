/* $Id: IEMAllMemRWTmplInline-armv8.cpp.h 110422 2025-07-27 22:59:43Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined R/W Memory Functions Template, ARMv8 target.
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
                                            && ((a_GCPtr) & GUEST_MIN_PAGE_OFFSET_MASK) <= GUEST_MIN_PAGE_SIZE - sizeof(TMPL_MEM_TYPE)) \
                                        || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, (a_GCPtr), TMPL_MEM_TYPE))
#else
# define TMPL_MEM_ALIGN_CHECK(a_GCPtr) (   !((a_GCPtr) & TMPL_MEM_TYPE_ALIGN) /* If aligned, it will be within the page. */ \
                                        || TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(pVCpu, (a_GCPtr), TMPL_MEM_TYPE))
#endif

/** Helper for checking if @a a_GCPtr is acceptably aligned for a data pair and
 *  that the pair is fully within the page for a TMPL_MEM_TYPE.  */
#if TMPL_MEM_TYPE_ALIGN + 1 < TMPL_MEM_TYPE_SIZE + TMPL_MEM_TYPE_SIZE
# define TMPL_MEM_PAIR_ALIGN_CHECK(a_GCPtr) (   !((a_GCPtr) & TMPL_MEM_TYPE_ALIGN) \
                                             &&    ((a_GCPtr) & GUEST_MIN_PAGE_OFFSET_MASK) \
                                                <= GUEST_MIN_PAGE_SIZE - sizeof(TMPL_MEM_TYPE) - sizeof(TMPL_MEM_TYPE))
#elif !defined(TMPL_MEM_NO_PAIR)
# error Unexpected alignment.
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


/*********************************************************************************************************************************
*   Fetches                                                                                                                      *
*********************************************************************************************************************************/

/**
 * Inlined flat addressing fetch function that longjumps on error.
 */
#ifdef TMPL_MEM_BY_REF
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *pValue, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
#else
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFlatFetchData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
#endif
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary and do a TLB lookup.
     */
# if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
# endif
    {
        TMPL_MEM_IF_TLB_LOOKUP_MATCH(GCPtrMem, IEMTLBE_F_EFF_P_NO_READ, IEMTLBE_F_EFF_U_NO_READ, IEMTLBE_F_PG_NO_READ)
        {
            /*
             * Fetch and return the data unit.
             */
# ifdef IEM_WITH_TLB_STATISTICS
            pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
# endif
            Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
            Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
# ifdef TMPL_MEM_BY_REF
            *pValue = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                     GCPtrMem, pValue));
            return;
# else
            TMPL_MEM_TYPE const uRet = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uRet));
            return uRet;
# endif
        }
        TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH();
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    LogEx(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#endif
#ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, pValue, GCPtrMem);
#else
    return RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem);
#endif
}


#ifndef TMPL_MEM_NO_PAIR
/**
 * Inlined flat addressing pair fetch function that longjumps on error.
 */
# ifdef TMPL_MEM_BY_REF
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatFetchDataPair,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, TMPL_MEM_TYPE *pValue1, TMPL_MEM_TYPE *pValue2) IEM_NOEXCEPT_MAY_LONGJMP
# else
DECL_INLINE_THROW(TMPL_MEM_TYPE)
RT_CONCAT3(iemMemFlatFetchDataPair,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, TMPL_MEM_TYPE *pValue2) IEM_NOEXCEPT_MAY_LONGJMP
# endif
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary and do a TLB lookup.
     */
    if (RT_LIKELY(TMPL_MEM_PAIR_ALIGN_CHECK(GCPtrMem)))
    {
        TMPL_MEM_IF_TLB_LOOKUP_MATCH(GCPtrMem, IEMTLBE_F_EFF_P_NO_READ, IEMTLBE_F_EFF_U_NO_READ, IEMTLBE_F_PG_NO_READ)
        {
            /*
             * Fetch and return the data unit.
             */
#  ifdef IEM_WITH_TLB_STATISTICS
            pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
            Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
            Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
#  ifdef TMPL_MEM_BY_REF
            *pValue1 = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            *pValue2 = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[(GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK) + TMPL_MEM_TYPE_SIZE];
            LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs  %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                     GCPtrMem, pValue1, pValue2));
            return;
#  else
            TMPL_MEM_TYPE const uRet = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            *pValue2 = *(TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[(GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK) + TMPL_MEM_TYPE_SIZE];
            LogEx(LOG_GROUP_IEM_MEM,("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n",
                                     GCPtrMem, uRet, *pValue2));
            return uRet;
#  endif
        }
        TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH();
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    LogEx(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
# ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemFetchDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, pValue1, pValue2);
# else
    return RT_CONCAT3(iemMemFetchDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, pValue2);
# endif
}
#endif /* !TMPL_MEM_NO_PAIR */



/*********************************************************************************************************************************
*   Stores                                                                                                                       *
*********************************************************************************************************************************/

/**
 * Inlined flat addressing store function that longjumps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatStoreData,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
#ifdef TMPL_MEM_BY_REF
                                                    TMPL_MEM_TYPE const *pValue) IEM_NOEXCEPT_MAY_LONGJMP
#else
                                                    TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
#endif
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary and do a TLB lookup.
     */
# if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(TMPL_MEM_ALIGN_CHECK(GCPtrMem)))
# endif
    {
        TMPL_MEM_IF_TLB_LOOKUP_MATCH(GCPtrMem, IEMTLBE_F_EFF_P_NO_WRITE, IEMTLBE_F_EFF_U_NO_WRITE,
                                     IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_EFF_NO_DIRTY)
        {
            /*
             * Store the value and return.
             */
# ifdef IEM_WITH_TLB_STATISTICS
            pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
# endif
            Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
            Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
# ifdef TMPL_MEM_BY_REF
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK] = *pValue;
            Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                      GCPtrMem, pValue));
# else
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK] = uValue;
            Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
# endif
            return;
        }
        TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH();
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log6Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#endif
#ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, pValue);
#else
    RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, uValue);
#endif
}


#ifndef TMPL_MEM_NO_PAIR
/**
 * Inlined flat addressing pair store function that longjumps on error.
 */
DECL_INLINE_THROW(void)
RT_CONCAT3(iemMemFlatStoreDataPair,TMPL_MEM_FN_SUFF,Jmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
# ifdef TMPL_MEM_BY_REF
                                                         TMPL_MEM_TYPE const *pValue1, TMPL_MEM_TYPE const *pValue2) IEM_NOEXCEPT_MAY_LONGJMP
# else
                                                         TMPL_MEM_TYPE uValue1, TMPL_MEM_TYPE uValue2) IEM_NOEXCEPT_MAY_LONGJMP
# endif
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
    /*
     * Check that it doesn't cross a page boundrary and do a TLB lookup.
     */
    if (RT_LIKELY(TMPL_MEM_PAIR_ALIGN_CHECK(GCPtrMem)))
    {
        TMPL_MEM_IF_TLB_LOOKUP_MATCH(GCPtrMem, IEMTLBE_F_EFF_P_NO_WRITE, IEMTLBE_F_EFF_U_NO_WRITE,
                                     IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_EFF_NO_DIRTY)
        {
            /*
             * Store the value and return.
             */
#  ifdef IEM_WITH_TLB_STATISTICS
            pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
            Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
            Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
#  ifdef TMPL_MEM_BY_REF
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]                        = *pValue1;
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[(GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK) + TMPL_MEM_TYPE_SIZE] = *pValue2;
            Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs %." RT_XSTR(TMPL_MEM_TYPE_SIZE) "Rhxs\n",
                                      GCPtrMem, pValue1, pValue2));
#  else
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]                        = uValue1;
            *(TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[(GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK) + TMPL_MEM_TYPE_SIZE] = uValue2;
            Log5Ex(LOG_GROUP_IEM_MEM,("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n",
                                      GCPtrMem, uValue1, uValue2));
#  endif
            return;
        }
        TMPL_MEM_ENDIF_TLB_LOOKUP_MATCH();
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log6Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
# ifdef TMPL_MEM_BY_REF
    RT_CONCAT3(iemMemStoreDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, pValue1, pValue2);
# else
    RT_CONCAT3(iemMemStoreDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(pVCpu, GCPtrMem, uValue1, uValue2);
# endif
}
#endif /* !TMPL_MEM_NO_PAIR */



/*********************************************************************************************************************************
*   Mapping / Direct Memory Access                                                                                               *
*********************************************************************************************************************************/
#if 0 /// @todo ndef TMPL_MEM_NO_MAPPING

/**
 * Inlined flat read-write memory mapping function that longjumps on error.
 *
 * Almost identical to RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,AtJmp).
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,RwJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
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
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
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
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM RW/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwSafeJmp)(pVCpu, pbUnmapInfo, GCPtrMem);
}


# ifdef TMPL_MEM_WITH_ATOMIC_MAPPING
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
#   if TMPL_MEM_TYPE_SIZE > 1
    if (RT_LIKELY(!(GCPtrMem & TMPL_MEM_TYPE_ALIGN))) /* strictly aligned, otherwise do fall back which knows th details. */
#   endif
    {
        /*
         * TLB lookup.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
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
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM AT/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
#  endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtSafeJmp)(pVCpu, pbUnmapInfo, GCPtrMem);
}
# endif /* TMPL_MEM_WITH_ATOMIC_MAPPING */


/**
 * Inlined flat write-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,WoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
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
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
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
#  ifdef IEM_WITH_TLB_STATISTICS
                pVCpu->iem.s.DataTlb.cTlbInlineCodeHits++;
#  endif
                Assert(pTlbe->pbMappingR3); /* (Only ever cleared by the owning EMT.) */
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log7Ex(LOG_GROUP_IEM_MEM,("IEM WO/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log8Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoSafeJmp)(pVCpu, pbUnmapInfo, GCPtrMem);
}


/**
 * Inlined read-only memory mapping function that longjumps on error.
 */
DECL_INLINE_THROW(TMPL_MEM_TYPE const *)
RT_CONCAT3(iemMemFlatMapData,TMPL_MEM_FN_SUFF,RoJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3) && !defined(TMPL_MEM_NO_INLINE)
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
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
        PCIEMTLBENTRY  pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
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
                Assert(!((uintptr_t)pTlbe->pbMappingR3 & GUEST_MIN_PAGE_OFFSET_MASK));
                *pbUnmapInfo = 0;
                Log3Ex(LOG_GROUP_IEM_MEM,("IEM RO/map " TMPL_MEM_FMT_DESC " %RGv: %p\n",
                                          GCPtrMem, &pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK]));
                return (TMPL_MEM_TYPE const *)&pTlbe->pbMappingR3[GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK];
            }
        }
    }

    /* Fall back on the slow careful approach in case of TLB miss, MMIO, exception
       outdated page pointer, or other troubles.  (This will do a TLB load.) */
    Log4Ex(LOG_GROUP_IEM_MEM,(LOG_FN_FMT ": %RGv falling back\n", LOG_FN_NAME, GCPtrMem));
# endif
    return RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoSafeJmp)(pVCpu, pbUnmapInfo, GCPtrMem);
}

#endif /* !TMPL_MEM_NO_MAPPING */


#undef TMPL_MEM_TYPE
#undef TMPL_MEM_TYPE_ALIGN
#undef TMPL_MEM_TYPE_SIZE
#undef TMPL_MEM_FN_SUFF
#undef TMPL_MEM_FMT_TYPE
#undef TMPL_MEM_FMT_DESC
#undef TMPL_MEM_ALIGN_CHECK
#undef TMPL_MEM_PAIR_ALIGN_CHECK
#undef TMPL_MEM_BY_REF
