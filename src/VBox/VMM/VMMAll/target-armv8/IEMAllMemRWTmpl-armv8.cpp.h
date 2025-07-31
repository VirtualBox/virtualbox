/* $Id: IEMAllMemRWTmpl-armv8.cpp.h 110422 2025-07-27 22:59:43Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - R/W Memory Functions Template, ARMv8 target.
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
#ifndef TMPL_MEM_TYPE_ALIGN
# define TMPL_MEM_TYPE_ALIGN     (sizeof(TMPL_MEM_TYPE) - 1)
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
#ifndef TMPL_MEM_MAP_FLAGS_ADD
# define TMPL_MEM_MAP_FLAGS_ADD  (0)
#endif


/**
 * Standard fetch function.
 *
 * This is used by CImpl code.
 */
VBOXSTRICTRC RT_CONCAT(iemMemFetchData,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *puDst, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puSrc, &bUnmapInfo, sizeof(*puSrc), GCPtrMem,
                                IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puDst = *puSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log2(("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, *puDst));
    }
    return rc;
}


/**
 * Safe/fallback fetch function that longjmps on error.
 */
#ifdef TMPL_MEM_BY_REF
void
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *pDst, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
# endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *pSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*pSrc), GCPtrMem,
                                                                        IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    *pDst = *pSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, pDst));
}
#else /* !TMPL_MEM_BY_REF */
TMPL_MEM_TYPE
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
# endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puSrc), GCPtrMem,
                                                                         IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    TMPL_MEM_TYPE const  uRet = *puSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uRet));
    return uRet;
}
#endif /* !TMPL_MEM_BY_REF */


#ifndef TMPL_MEM_NO_PAIR
/**
 * Safe/fallback fetch data pair function that longjmps on error.
 */
# ifdef TMPL_MEM_BY_REF
void
RT_CONCAT3(iemMemFetchDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                         TMPL_MEM_TYPE *pDst1, TMPL_MEM_TYPE *pDst2) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
#  endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *pSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*pSrc), GCPtrMem,
                                                                        IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    *pDst1 = pSrc[0];
    *pDst2 = pSrc[1];
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, pDst1, pDst2));
}
# else /* !TMPL_MEM_BY_REF */
TMPL_MEM_TYPE
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, TMPL_MEM_TYPE *pDst2) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
#  endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puSrc), GCPtrMem,
                                                                         IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    TMPL_MEM_TYPE const  uRet1 = puSrc[0]; /** @todo combine these two if possible... */
    TMPL_MEM_TYPE const  uRet2 = puSrc[1];
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    *pDst2 = uRet2;
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uRet1, uRet2));
    return uRet1;
}
# endif /* !TMPL_MEM_BY_REF */
#endif /* !TMPL_MEM_NO_PAIR */


/**
 * Standard store function.
 *
 * This is used by CImpl code.
 */
VBOXSTRICTRC RT_CONCAT(iemMemStoreData,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
#ifdef TMPL_MEM_BY_REF
                                                         TMPL_MEM_TYPE const *pValue) RT_NOEXCEPT
#else
                                                         TMPL_MEM_TYPE uValue) RT_NOEXCEPT
#endif
{
    /* The lazy approach for now... */
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puDst, &bUnmapInfo, sizeof(*puDst),
                                GCPtrMem, IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
#ifdef TMPL_MEM_BY_REF
        *puDst = *pValue;
#else
        *puDst = uValue;
#endif
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
#ifdef TMPL_MEM_BY_REF
        Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, pValue));
#else
        Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
#endif
    }
    return rc;
}


/**
 * Stores a data item, longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   uValue              The value to store.
 */
void RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
#ifdef TMPL_MEM_BY_REF
                                                          TMPL_MEM_TYPE const *pValue) IEM_NOEXCEPT_MAY_LONGJMP
#else
                                                          TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
#endif
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#endif
#ifdef TMPL_MEM_BY_REF
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, pValue));
#else
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
#endif
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst = (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puDst), GCPtrMem,
                                                             IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
#ifdef TMPL_MEM_BY_REF
    *puDst = *pValue;
#else
    *puDst = uValue;
#endif
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}


#ifndef TMPL_MEM_NO_PAIR
/**
 * Stores a data pair, longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   uValue1             The first value to store.
 * @param   uValue2             The second value to store.
 */
void RT_CONCAT3(iemMemStoreDataPair,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
# ifdef TMPL_MEM_BY_REF
                                                              TMPL_MEM_TYPE const *pValue1, TMPL_MEM_TYPE const *pValue2) IEM_NOEXCEPT_MAY_LONGJMP
# else
                                                              TMPL_MEM_TYPE uValue1, TMPL_MEM_TYPE uValue2) IEM_NOEXCEPT_MAY_LONGJMP
# endif
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
# ifdef TMPL_MEM_BY_REF
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, pValue1, pValue2));
# else
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %RGv: " TMPL_MEM_FMT_TYPE " " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue2, uValue2));
# endif
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst = (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puDst), GCPtrMem,
                                                             IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
# ifdef TMPL_MEM_BY_REF
    puDst[0] = *pValue1;
    puDst[1] = *pValue2;
# else
    puDst[0] = uValue1; /** @todo combine these two if possible... */
    puDst[1] = uValue2;
# endif
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}
#endif /* !TMPL_MEM_NO_PAIR */


/**
 * Maps a data buffer for atomic read+write direct access (or via a bounce
 * buffer), longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#endif
    Log8(("IEM AT/map " TMPL_MEM_FMT_DESC " %RGv\n", GCPtrMem));
    *pbUnmapInfo = 1 | ((IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE) << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), GCPtrMem,
                                             IEM_ACCESS_DATA_ATOMIC, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for read+write direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#endif
    Log8(("IEM RW/map " TMPL_MEM_FMT_DESC " %RGv\n", GCPtrMem));
    *pbUnmapInfo = 1 | ((IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE) << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), GCPtrMem,
                                             IEM_ACCESS_DATA_RW, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for writeonly direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#endif
    Log8(("IEM WO/map " TMPL_MEM_FMT_DESC " %RGv\n", GCPtrMem));
    *pbUnmapInfo = 1 | (IEM_ACCESS_TYPE_WRITE << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), GCPtrMem,
                                             IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for readonly direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE const *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#endif
    Log4(("IEM RO/map " TMPL_MEM_FMT_DESC " %RGv\n", GCPtrMem));
    *pbUnmapInfo = 1 | (IEM_ACCESS_TYPE_READ << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), GCPtrMem,
                                             IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/* clean up */
#undef TMPL_MEM_TYPE
#undef TMPL_MEM_TYPE_ALIGN
#undef TMPL_MEM_FN_SUFF
#undef TMPL_MEM_FMT_TYPE
#undef TMPL_MEM_FMT_DESC
#undef TMPL_MEM_MAP_FLAGS_ADD

