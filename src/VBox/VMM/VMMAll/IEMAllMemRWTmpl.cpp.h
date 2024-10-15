/* $Id: IEMAllMemRWTmpl.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - R/W Memory Functions Template.
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
 * This is used by CImpl code, so it needs to be kept even when IEM_WITH_SETJMP
 * is defined.
 */
VBOXSTRICTRC RT_CONCAT(iemMemFetchData,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *puDst,
                                                         uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puSrc, &bUnmapInfo, sizeof(*puSrc), iSegReg, GCPtrMem,
                                IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puDst = *puSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log2(("IEM RD " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, *puDst));
    }
    return rc;
}


#ifdef IEM_WITH_SETJMP
/**
 * Safe/fallback fetch function that longjmps on error.
 */
# ifdef TMPL_MEM_BY_REF
void
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *pDst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
#  endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *pSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*pSrc), iSegReg, GCPtrMem,
                                                                        IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    *pDst = *pSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, pDst));
}
# else /* !TMPL_MEM_BY_REF */
TMPL_MEM_TYPE
RT_CONCAT3(iemMemFetchData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
#  endif
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puSrc), iSegReg, GCPtrMem,
                                                                         IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    TMPL_MEM_TYPE const  uRet = *puSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log2(("IEM RD " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, uRet));
    return uRet;
}
# endif /* !TMPL_MEM_BY_REF */
#endif /* IEM_WITH_SETJMP */



/**
 * Standard store function.
 *
 * This is used by CImpl code, so it needs to be kept even when IEM_WITH_SETJMP
 * is defined.
 */
VBOXSTRICTRC RT_CONCAT(iemMemStoreData,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem,
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
                                iSegReg, GCPtrMem, IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
#ifdef TMPL_MEM_BY_REF
        *puDst = *pValue;
#else
        *puDst = uValue;
#endif
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
#ifdef TMPL_MEM_BY_REF
        Log6(("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, pValue));
#else
        Log6(("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, uValue));
#endif
    }
    return rc;
}


#ifdef IEM_WITH_SETJMP
/**
 * Stores a data byte, longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   uValue              The value to store.
 */
void RT_CONCAT3(iemMemStoreData,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem,
#ifdef TMPL_MEM_BY_REF
                                                          TMPL_MEM_TYPE const *pValue) IEM_NOEXCEPT_MAY_LONGJMP
#else
                                                          TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
#endif
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
#ifdef TMPL_MEM_BY_REF
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, pValue));
#else
    Log6(("IEM WR " TMPL_MEM_FMT_DESC " %d|%RGv: " TMPL_MEM_FMT_TYPE "\n", iSegReg, GCPtrMem, uValue));
#endif
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst = (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(*puDst), iSegReg, GCPtrMem,
                                                             IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
#ifdef TMPL_MEM_BY_REF
    *puDst = *pValue;
#else
    *puDst = uValue;
#endif
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}
#endif /* IEM_WITH_SETJMP */


#ifdef IEM_WITH_SETJMP

/**
 * Maps a data buffer for atomic read+write direct access (or via a bounce
 * buffer), longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,AtSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
    Log8(("IEM AT/map " TMPL_MEM_FMT_DESC " %d|%RGv\n", iSegReg, GCPtrMem));
    *pbUnmapInfo = 1 | ((IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE) << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), iSegReg, GCPtrMem,
                                             IEM_ACCESS_DATA_ATOMIC, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for read+write direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RwSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
    Log8(("IEM RW/map " TMPL_MEM_FMT_DESC " %d|%RGv\n", iSegReg, GCPtrMem));
    *pbUnmapInfo = 1 | ((IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE) << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), iSegReg, GCPtrMem,
                                             IEM_ACCESS_DATA_RW, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for writeonly direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,WoSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
    Log8(("IEM WO/map " TMPL_MEM_FMT_DESC " %d|%RGv\n", iSegReg, GCPtrMem));
    *pbUnmapInfo = 1 | (IEM_ACCESS_TYPE_WRITE << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), iSegReg, GCPtrMem,
                                             IEM_ACCESS_DATA_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}


/**
 * Maps a data buffer for readonly direct access (or via a bounce buffer),
 * longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbUnmapInfo         Pointer to unmap info variable.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
TMPL_MEM_TYPE const *
RT_CONCAT3(iemMemMapData,TMPL_MEM_FN_SUFF,RoSafeJmp)(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo,
                                                     uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif
    Log4(("IEM RO/map " TMPL_MEM_FMT_DESC " %d|%RGv\n", iSegReg, GCPtrMem));
    *pbUnmapInfo = 1 | (IEM_ACCESS_TYPE_READ << 4); /* zero is for the TLB hit */
    return (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, pbUnmapInfo, sizeof(TMPL_MEM_TYPE), iSegReg, GCPtrMem,
                                             IEM_ACCESS_DATA_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
}

#endif /* IEM_WITH_SETJMP */


#ifdef TMPL_MEM_WITH_STACK

/**
 * Pops a general purpose register off the stack.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   iGReg               The GREG to load the popped value into.
 */
VBOXSTRICTRC RT_CONCAT(iemMemStackPopGReg,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, uint8_t iGReg) RT_NOEXCEPT
{
    Assert(iGReg < 16);

    /* Increment the stack pointer. */
    uint64_t    uNewRsp;
    RTGCPTR     GCPtrTop = iemRegGetRspForPop(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Load the word the lazy way. */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puSrc, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                IEM_ACCESS_STACK_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        TMPL_MEM_TYPE const uValue = *puSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);

        /* Commit the register and new RSP values. */
        if (rc == VINF_SUCCESS)
        {
            Log10(("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " (r%u)\n",
                   GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue, iGReg));
            pVCpu->cpum.GstCtx.rsp = uNewRsp;
            if (sizeof(TMPL_MEM_TYPE) != sizeof(uint16_t))
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u   = uValue;
            else
                pVCpu->cpum.GstCtx.aGRegs[iGReg].u16 = uValue;
            return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * Pushes an item onto the stack, regular version.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   uValue              The value to push.
 */
VBOXSTRICTRC RT_CONCAT(iemMemStackPush,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) RT_NOEXCEPT
{
    /* Increment the stack pointer. */
    uint64_t    uNewRsp;
    RTGCPTR     GCPtrTop = iemRegGetRspForPush(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Write the dword the lazy way. */
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst;
    VBOXSTRICTRC   rc = iemMemMap(pVCpu, (void **)&puDst, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                  IEM_ACCESS_STACK_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puDst = uValue;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);

        /* Commit the new RSP value unless we an access handler made trouble. */
        if (rc == VINF_SUCCESS)
        {
            Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE "\n",
                   GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
            pVCpu->cpum.GstCtx.rsp = uNewRsp;
            return VINF_SUCCESS;
        }
    }

    return rc;
}


/**
 * Pops a generic item off the stack, regular version.
 *
 * This is used by C-implementation code.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   puValue             Where to store the popped value.
 */
VBOXSTRICTRC RT_CONCAT(iemMemStackPop,TMPL_MEM_FN_SUFF)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *puValue) RT_NOEXCEPT
{
    /* Increment the stack pointer. */
    uint64_t    uNewRsp;
    RTGCPTR     GCPtrTop = iemRegGetRspForPop(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Write the word the lazy way. */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puSrc, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                IEM_ACCESS_STACK_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puValue = *puSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);

        /* Commit the new RSP value. */
        if (rc == VINF_SUCCESS)
        {
            Log10(("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE "\n",
                   GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, *puValue));
            pVCpu->cpum.GstCtx.rsp = uNewRsp;
            return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * Pushes an item onto the stack, using a temporary stack pointer.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   uValue              The value to push.
 * @param   pTmpRsp             Pointer to the temporary stack pointer.
 */
VBOXSTRICTRC RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,Ex)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue, PRTUINT64U pTmpRsp) RT_NOEXCEPT
{
    /* Increment the stack pointer. */
    RTUINT64U   NewRsp = *pTmpRsp;
    RTGCPTR     GCPtrTop = iemRegGetRspForPushEx(pVCpu, &NewRsp, sizeof(TMPL_MEM_TYPE));

    /* Write the word the lazy way. */
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst;
    VBOXSTRICTRC   rc = iemMemMap(pVCpu, (void **)&puDst, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                  IEM_ACCESS_STACK_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puDst = uValue;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);

        /* Commit the new RSP value unless we an access handler made trouble. */
        if (rc == VINF_SUCCESS)
        {
            Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " [ex]\n",
                   GCPtrTop, pTmpRsp->u, NewRsp.u, uValue));
            *pTmpRsp = NewRsp;
            return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * Pops an item off the stack, using a temporary stack pointer.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   puValue             Where to store the popped value.
 * @param   pTmpRsp             Pointer to the temporary stack pointer.
 */
VBOXSTRICTRC
RT_CONCAT3(iemMemStackPop,TMPL_MEM_FN_SUFF,Ex)(PVMCPUCC pVCpu, TMPL_MEM_TYPE *puValue, PRTUINT64U pTmpRsp) RT_NOEXCEPT
{
    /* Increment the stack pointer. */
    RTUINT64U   NewRsp = *pTmpRsp;
    RTGCPTR     GCPtrTop = iemRegGetRspForPopEx(pVCpu, &NewRsp, sizeof(TMPL_MEM_TYPE));

    /* Write the word the lazy way. */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&puSrc, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                IEM_ACCESS_STACK_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    if (rc == VINF_SUCCESS)
    {
        *puValue = *puSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);

        /* Commit the new RSP value. */
        if (rc == VINF_SUCCESS)
        {
            Log10(("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " [ex]\n",
                   GCPtrTop, pTmpRsp->u, NewRsp.u, *puValue));
            *pTmpRsp = NewRsp;
            return VINF_SUCCESS;
        }
    }
    return rc;
}


# ifdef IEM_WITH_SETJMP

/**
 * Safe/fallback stack store function that longjmps on error.
 */
void RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                           TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
#  if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
#  endif

    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst = (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrMem,
                                                             IEM_ACCESS_STACK_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    *puDst = uValue;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);

    Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
}


#  ifdef TMPL_WITH_PUSH_SREG
/**
 * Safe/fallback stack SREG store function that longjmps on error.
 */
void RT_CONCAT3(iemMemStoreStack,TMPL_MEM_FN_SUFF,SRegSafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem,
                                                               TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif

    /* bs3-cpu-weird-1 explores this instruction. AMD 3990X does it by the book,
      with a zero extended DWORD write.  While my Intel 10890XE goes all weird
      in real mode where it will write a DWORD with the top word of EFLAGS in
      the top half.  In all other modes it does a WORD access. */

   /** @todo Docs indicate the behavior changed maybe in Pentium or Pentium Pro.
    *  Check ancient hardware when it actually did change. */
    uint8_t bUnmapInfo;
    if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
    {
        if (!IEM_IS_REAL_MODE(pVCpu))
        {
            /* WORD per intel specs. */
            uint16_t *puDst = (uint16_t *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(uint16_t), X86_SREG_SS, GCPtrMem,
                                                           IEM_ACCESS_STACK_W, (sizeof(uint16_t) - 1) | TMPL_MEM_MAP_FLAGS_ADD); /** @todo 2 or 4 alignment check for PUSH SS? */
            *puDst = (uint16_t)uValue;
            iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
            Log12(("IEM WR 'word' SS|%RGv: %#06x [sreg/i]\n", GCPtrMem, (uint16_t)uValue));
        }
        else
        {
            /* DWORD real mode weirness observed on 10980XE. */
            /** @todo Check this on other intel CPUs and when pushing registers other
             *        than FS (which all that bs3-cpu-weird-1 does atm).  (Maybe this is
             *        something for the CPU profile... Hope not.) */
            uint32_t *puDst = (uint32_t *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(uint32_t), X86_SREG_SS, GCPtrMem,
                                                           IEM_ACCESS_STACK_W, (sizeof(uint32_t) - 1) | TMPL_MEM_MAP_FLAGS_ADD);
            *puDst = (uint16_t)uValue | (pVCpu->cpum.GstCtx.eflags.u & (UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK));
            iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
             Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE " [sreg/ir]\n", GCPtrMem, uValue));
        }
    }
    else
    {
        /* DWORD per spec. */
        uint32_t *puDst = (uint32_t *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(uint32_t), X86_SREG_SS, GCPtrMem,
                                                       IEM_ACCESS_STACK_W, (sizeof(uint32_t) - 1) | TMPL_MEM_MAP_FLAGS_ADD);
        *puDst = uValue;
        iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
        Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE " [sreg]\n", GCPtrMem, uValue));
    }
}
#  endif /* TMPL_WITH_PUSH_SREG */


/**
 * Safe/fallback stack fetch function that longjmps on error.
 */
TMPL_MEM_TYPE RT_CONCAT3(iemMemFetchStack,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
# endif

    /* Read the data. */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS,
                                                                         GCPtrMem, IEM_ACCESS_STACK_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    TMPL_MEM_TYPE const  uValue = *puSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);

    /* Commit the register and RSP values. */
    Log10(("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv: " TMPL_MEM_FMT_TYPE "\n", GCPtrMem, uValue));
    return uValue;
}


/**
 * Safe/fallback stack push function that longjmps on error.
 */
void RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif

    /* Decrement the stack pointer (prep). */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPush(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Write the data. */
    uint8_t        bUnmapInfo;
    TMPL_MEM_TYPE *puDst = (TMPL_MEM_TYPE *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS, GCPtrTop,
                                                             IEM_ACCESS_STACK_W, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    *puDst = uValue;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);

    /* Commit the RSP change. */
    Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE "\n",
           GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
    pVCpu->cpum.GstCtx.rsp = uNewRsp;
}


/**
 * Safe/fallback stack pop greg function that longjmps on error.
 */
void RT_CONCAT3(iemMemStackPopGReg,TMPL_MEM_FN_SUFF,SafeJmp)(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeReadPath++;
# endif

    /* Increment the stack pointer. */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPop(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Read the data. */
    uint8_t              bUnmapInfo;
    TMPL_MEM_TYPE const *puSrc = (TMPL_MEM_TYPE const *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(TMPL_MEM_TYPE), X86_SREG_SS,
                                                                         GCPtrTop, IEM_ACCESS_STACK_R, TMPL_MEM_TYPE_ALIGN | TMPL_MEM_MAP_FLAGS_ADD);
    TMPL_MEM_TYPE const  uValue = *puSrc;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);

    /* Commit the register and RSP values. */
    Log10(("IEM RD " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " (r%u)\n",
           GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue, iGReg));
    pVCpu->cpum.GstCtx.rsp = uNewRsp;
    if (sizeof(TMPL_MEM_TYPE) != sizeof(uint16_t))
        pVCpu->cpum.GstCtx.aGRegs[iGReg].u   = uValue;
    else
        pVCpu->cpum.GstCtx.aGRegs[iGReg].u16 = uValue;
}

#  ifdef TMPL_WITH_PUSH_SREG
/**
 * Safe/fallback stack push function that longjmps on error.
 */
void RT_CONCAT3(iemMemStackPush,TMPL_MEM_FN_SUFF,SRegSafeJmp)(PVMCPUCC pVCpu, TMPL_MEM_TYPE uValue) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    pVCpu->iem.s.DataTlb.cTlbSafeWritePath++;
# endif

    /* Decrement the stack pointer (prep). */
    uint64_t      uNewRsp;
    RTGCPTR const GCPtrTop = iemRegGetRspForPush(pVCpu, sizeof(TMPL_MEM_TYPE), &uNewRsp);

    /* Write the data. */
    /* The intel docs talks about zero extending the selector register
       value.  My actual intel CPU here might be zero extending the value
       but it still only writes the lower word... */
    /** @todo Test this on new HW and on AMD and in 64-bit mode.  Also test what
     * happens when crossing an electric page boundrary, is the high word checked
     * for write accessibility or not? Probably it is.  What about segment limits?
     * It appears this behavior is also shared with trap error codes.
     *
     * Docs indicate the behavior changed maybe in Pentium or Pentium Pro. Check
     * ancient hardware when it actually did change. */
    uint8_t   bUnmapInfo;
    uint16_t *puDst = (uint16_t *)iemMemMapSafeJmp(pVCpu, &bUnmapInfo, sizeof(uint16_t), X86_SREG_SS, GCPtrTop,
                                                   IEM_ACCESS_STACK_W, (sizeof(uint16_t) - 1) | TMPL_MEM_MAP_FLAGS_ADD); /** @todo 2 or 4 alignment check for PUSH SS? */
    *puDst = (uint16_t)uValue;
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);

    /* Commit the RSP change. */
    Log12(("IEM WR " TMPL_MEM_FMT_DESC " SS|%RGv (%RX64->%RX64): " TMPL_MEM_FMT_TYPE " [sreg]\n",
           GCPtrTop, pVCpu->cpum.GstCtx.rsp, uNewRsp, uValue));
    pVCpu->cpum.GstCtx.rsp = uNewRsp;
}
#  endif /* TMPL_WITH_PUSH_SREG */

# endif /* IEM_WITH_SETJMP */

#endif /* TMPL_MEM_WITH_STACK */

/* clean up */
#undef TMPL_MEM_TYPE
#undef TMPL_MEM_TYPE_ALIGN
#undef TMPL_MEM_FN_SUFF
#undef TMPL_MEM_FMT_TYPE
#undef TMPL_MEM_FMT_DESC
#undef TMPL_WITH_PUSH_SREG
#undef TMPL_MEM_MAP_FLAGS_ADD

