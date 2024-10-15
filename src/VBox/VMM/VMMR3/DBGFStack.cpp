/* $Id: DBGFStack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Call Stack Analyser.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/mm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/alloca.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/formats/pecoff.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
static DECLCALLBACK(int) dbgfR3StackReadCallback(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSp, size_t cbToRead, void *pvDst);

/**
 * Unwind context.
 *
 * @note Using a constructor and destructor here for simple+safe cleanup.
 */
typedef struct DBGFUNWINDCTX
{
    PUVM        m_pUVM;
    VMCPUID     m_idCpu;
    RTDBGAS     m_hAs;
    PCCPUMCTX   m_pInitialCtx;
    bool        m_fIsHostRing0;
    uint64_t    m_uOsScratch; /**< For passing to DBGFOSREG::pfnStackUnwindAssist. */

    RTDBGMOD    m_hCached;
    RTUINTPTR   m_uCachedMapping;
    RTUINTPTR   m_cbCachedMapping;
    RTDBGSEGIDX m_idxCachedSegMapping;

    RTDBGUNWINDSTATE m_State;

    DBGFUNWINDCTX(PUVM pUVM, VMCPUID idCpu, PCCPUMCTX pInitialCtx, RTDBGAS hAs)
    {
        m_State.u32Magic     = RTDBGUNWINDSTATE_MAGIC;
        m_State.enmArch      = RTLDRARCH_AMD64;
        m_State.pfnReadStack = dbgfR3StackReadCallback;
        m_State.pvUser       = this;
        RT_ZERO(m_State.u);
        if (pInitialCtx)
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertFailed();
#else
            m_State.u.x86.auRegs[X86_GREG_xAX] = pInitialCtx->rax;
            m_State.u.x86.auRegs[X86_GREG_xCX] = pInitialCtx->rcx;
            m_State.u.x86.auRegs[X86_GREG_xDX] = pInitialCtx->rdx;
            m_State.u.x86.auRegs[X86_GREG_xBX] = pInitialCtx->rbx;
            m_State.u.x86.auRegs[X86_GREG_xSP] = pInitialCtx->rsp;
            m_State.u.x86.auRegs[X86_GREG_xBP] = pInitialCtx->rbp;
            m_State.u.x86.auRegs[X86_GREG_xSI] = pInitialCtx->rsi;
            m_State.u.x86.auRegs[X86_GREG_xDI] = pInitialCtx->rdi;
            m_State.u.x86.auRegs[X86_GREG_x8 ] = pInitialCtx->r8;
            m_State.u.x86.auRegs[X86_GREG_x9 ] = pInitialCtx->r9;
            m_State.u.x86.auRegs[X86_GREG_x10] = pInitialCtx->r10;
            m_State.u.x86.auRegs[X86_GREG_x11] = pInitialCtx->r11;
            m_State.u.x86.auRegs[X86_GREG_x12] = pInitialCtx->r12;
            m_State.u.x86.auRegs[X86_GREG_x13] = pInitialCtx->r13;
            m_State.u.x86.auRegs[X86_GREG_x14] = pInitialCtx->r14;
            m_State.u.x86.auRegs[X86_GREG_x15] = pInitialCtx->r15;
            m_State.uPc                        = pInitialCtx->rip;
            m_State.u.x86.uRFlags              = pInitialCtx->rflags.u;
            m_State.u.x86.auSegs[X86_SREG_ES]  = pInitialCtx->es.Sel;
            m_State.u.x86.auSegs[X86_SREG_CS]  = pInitialCtx->cs.Sel;
            m_State.u.x86.auSegs[X86_SREG_SS]  = pInitialCtx->ss.Sel;
            m_State.u.x86.auSegs[X86_SREG_DS]  = pInitialCtx->ds.Sel;
            m_State.u.x86.auSegs[X86_SREG_GS]  = pInitialCtx->gs.Sel;
            m_State.u.x86.auSegs[X86_SREG_FS]  = pInitialCtx->fs.Sel;
            m_State.u.x86.fRealOrV86           = CPUMIsGuestInRealOrV86ModeEx(pInitialCtx);
#endif
        }
        else if (hAs == DBGF_AS_R0)
            VMMR3InitR0StackUnwindState(pUVM, idCpu, &m_State);

        m_pUVM            = pUVM;
        m_idCpu           = idCpu;
        m_hAs             = DBGFR3AsResolveAndRetain(pUVM, hAs);
        m_pInitialCtx     = pInitialCtx;
        m_fIsHostRing0    = hAs == DBGF_AS_R0;
        m_uOsScratch      = 0;

        m_hCached         = NIL_RTDBGMOD;
        m_uCachedMapping  = 0;
        m_cbCachedMapping = 0;
        m_idxCachedSegMapping = NIL_RTDBGSEGIDX;
    }

    ~DBGFUNWINDCTX();

} DBGFUNWINDCTX;
/** Pointer to unwind context. */
typedef DBGFUNWINDCTX *PDBGFUNWINDCTX;


static void dbgfR3UnwindCtxFlushCache(PDBGFUNWINDCTX pUnwindCtx)
{
    if (pUnwindCtx->m_hCached != NIL_RTDBGMOD)
    {
        RTDbgModRelease(pUnwindCtx->m_hCached);
        pUnwindCtx->m_hCached = NIL_RTDBGMOD;
    }
    pUnwindCtx->m_cbCachedMapping     = 0;
    pUnwindCtx->m_idxCachedSegMapping = NIL_RTDBGSEGIDX;
}


DBGFUNWINDCTX::~DBGFUNWINDCTX()
{
    dbgfR3UnwindCtxFlushCache(this);
    if (m_hAs != NIL_RTDBGAS)
    {
        RTDbgAsRelease(m_hAs);
        m_hAs = NIL_RTDBGAS;
    }
}


/**
 * @interface_method_impl{RTDBGUNWINDSTATE,pfnReadStack}
 */
static DECLCALLBACK(int) dbgfR3StackReadCallback(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSp, size_t cbToRead, void *pvDst)
{
    Assert(   pThis->enmArch == RTLDRARCH_AMD64
           || pThis->enmArch == RTLDRARCH_X86_32);

    PDBGFUNWINDCTX pUnwindCtx = (PDBGFUNWINDCTX)pThis->pvUser;
    DBGFADDRESS SrcAddr;
    int rc = VINF_SUCCESS;
    if (pUnwindCtx->m_fIsHostRing0)
        DBGFR3AddrFromHostR0(&SrcAddr, uSp);
    else
    {
        if (   pThis->enmArch == RTLDRARCH_X86_32
            || pThis->enmArch == RTLDRARCH_X86_16)
        {
            if (!pThis->u.x86.fRealOrV86)
                rc = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &SrcAddr, pThis->u.x86.auSegs[X86_SREG_SS], uSp);
            else
                DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &SrcAddr, uSp + ((uint32_t)pThis->u.x86.auSegs[X86_SREG_SS] << 4));
        }
        else
            DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &SrcAddr, uSp);
    }
    if (RT_SUCCESS(rc))
        rc = DBGFR3MemRead(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &SrcAddr, pvDst, cbToRead);
    if (RT_SUCCESS(rc))
        return rc;
    return -rc; /* Ignore read errors. */
}


/**
 * Sets PC and SP.
 *
 * @returns true.
 * @param   pUnwindCtx          The unwind context.
 * @param   pAddrPC             The program counter (PC) value to set.
 * @param   pAddrStack          The stack pointer (SP) value to set.
 */
static bool dbgfR3UnwindCtxSetPcAndSp(PDBGFUNWINDCTX pUnwindCtx, PCDBGFADDRESS pAddrPC, PCDBGFADDRESS pAddrStack)
{
    Assert(   pUnwindCtx->m_State.enmArch == RTLDRARCH_AMD64
           || pUnwindCtx->m_State.enmArch == RTLDRARCH_X86_32);

    if (!DBGFADDRESS_IS_FAR(pAddrPC))
        pUnwindCtx->m_State.uPc = pAddrPC->FlatPtr;
    else
    {
        pUnwindCtx->m_State.uPc                       = pAddrPC->off;
        pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_CS] = pAddrPC->Sel;
    }
    if (!DBGFADDRESS_IS_FAR(pAddrStack))
        pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xSP] = pAddrStack->FlatPtr;
    else
    {
        pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xSP] = pAddrStack->off;
        pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_SS]  = pAddrStack->Sel;
    }
    return true;
}


/**
 * Tries to unwind one frame using unwind info.
 *
 * @returns true on success, false on failure.
 * @param   pUnwindCtx      The unwind context.
 */
static bool dbgfR3UnwindCtxDoOneFrame(PDBGFUNWINDCTX pUnwindCtx)
{
    /*
     * Need to load it into the cache?
     */
    RTUINTPTR offCache = pUnwindCtx->m_State.uPc - pUnwindCtx->m_uCachedMapping;
    if (offCache >= pUnwindCtx->m_cbCachedMapping)
    {
        RTDBGMOD        hDbgMod = NIL_RTDBGMOD;
        RTUINTPTR       uBase   = 0;
        RTDBGSEGIDX     idxSeg  = NIL_RTDBGSEGIDX;
        int rc = RTDbgAsModuleByAddr(pUnwindCtx->m_hAs, pUnwindCtx->m_State.uPc, &hDbgMod, &uBase, &idxSeg);
        if (RT_SUCCESS(rc))
        {
            dbgfR3UnwindCtxFlushCache(pUnwindCtx);
            pUnwindCtx->m_hCached             = hDbgMod;
            pUnwindCtx->m_uCachedMapping      = uBase;
            pUnwindCtx->m_idxCachedSegMapping = idxSeg;
            pUnwindCtx->m_cbCachedMapping     = idxSeg == NIL_RTDBGSEGIDX ? RTDbgModImageSize(hDbgMod)
                                              : RTDbgModSegmentSize(hDbgMod, idxSeg);
            offCache = pUnwindCtx->m_State.uPc - uBase;
        }
        else
            return false;
    }

    /*
     * Do the lookup.
     */
    AssertCompile(UINT32_MAX == NIL_RTDBGSEGIDX);
    int rc = RTDbgModUnwindFrame(pUnwindCtx->m_hCached, pUnwindCtx->m_idxCachedSegMapping, offCache, &pUnwindCtx->m_State);
    if (RT_SUCCESS(rc))
        return true;
    return false;
}


/**
 * Read stack memory, will init entire buffer.
 */
DECLINLINE(int) dbgfR3StackRead(PUVM pUVM, VMCPUID idCpu, void *pvBuf, PCDBGFADDRESS pSrcAddr, size_t cb, size_t *pcbRead)
{
    int rc = DBGFR3MemRead(pUVM, idCpu, pSrcAddr, pvBuf, cb);
    if (RT_FAILURE(rc))
    {
        /* fallback: byte by byte and zero the ones we fail to read. */
        size_t cbRead;
        for (cbRead = 0; cbRead < cb; cbRead++)
        {
            DBGFADDRESS Addr = *pSrcAddr;
            rc = DBGFR3MemRead(pUVM, idCpu, DBGFR3AddrAdd(&Addr, cbRead), (uint8_t *)pvBuf + cbRead, 1);
            if (RT_FAILURE(rc))
                break;
        }
        if (cbRead)
            rc = VINF_SUCCESS;
        memset((char *)pvBuf + cbRead, 0, cb - cbRead);
        *pcbRead = cbRead;
    }
    else
        *pcbRead = cb;
    return rc;
}

/**
 * Collects sure registers on frame exit.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pUVM        The user mode VM handle for the allocation.
 * @param   pFrame      The frame in question.
 * @param   pState      The unwind state.
 */
static int dbgfR3StackWalkCollectRegisterChanges(PUVM pUVM, PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState)
{
    pFrame->cSureRegs  = 0;
    pFrame->paSureRegs = NULL;

    if (   pState->enmArch == RTLDRARCH_AMD64
        || pState->enmArch == RTLDRARCH_X86_32
        || pState->enmArch == RTLDRARCH_X86_16)
    {
        if (pState->u.x86.Loaded.fAll)
        {
            /*
             * Count relevant registers.
             */
            uint32_t cRegs = 0;
            if (pState->u.x86.Loaded.s.fRegs)
                for (uint32_t f = 1; f < RT_BIT_32(RT_ELEMENTS(pState->u.x86.auRegs)); f <<= 1)
                    if (pState->u.x86.Loaded.s.fRegs & f)
                        cRegs++;
            if (pState->u.x86.Loaded.s.fSegs)
                for (uint32_t f = 1; f < RT_BIT_32(RT_ELEMENTS(pState->u.x86.auSegs)); f <<= 1)
                    if (pState->u.x86.Loaded.s.fSegs & f)
                        cRegs++;
            if (pState->u.x86.Loaded.s.fRFlags)
                cRegs++;
            if (pState->u.x86.Loaded.s.fErrCd)
                cRegs++;
            if (cRegs > 0)
            {
                /*
                 * Allocate the arrays.
                 */
                PDBGFREGVALEX paSureRegs = (PDBGFREGVALEX)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_STACK, sizeof(DBGFREGVALEX) * cRegs);
                AssertReturn(paSureRegs, VERR_NO_MEMORY);
                pFrame->paSureRegs = paSureRegs;
                pFrame->cSureRegs  = cRegs;

                /*
                 * Popuplate the arrays.
                 */
                uint32_t iReg = 0;
                if (pState->u.x86.Loaded.s.fRegs)
                    for (uint32_t i = 0; i < RT_ELEMENTS(pState->u.x86.auRegs); i++)
                        if (pState->u.x86.Loaded.s.fRegs & RT_BIT(i))
                        {
                            paSureRegs[iReg].Value.u64 = pState->u.x86.auRegs[i];
                            paSureRegs[iReg].enmType   = DBGFREGVALTYPE_U64;
                            paSureRegs[iReg].enmReg    = (DBGFREG)(DBGFREG_RAX + i);
                            iReg++;
                        }

                if (pState->u.x86.Loaded.s.fSegs)
                    for (uint32_t i = 0; i < RT_ELEMENTS(pState->u.x86.auSegs); i++)
                        if (pState->u.x86.Loaded.s.fSegs & RT_BIT(i))
                        {
                            paSureRegs[iReg].Value.u16 = pState->u.x86.auSegs[i];
                            paSureRegs[iReg].enmType   = DBGFREGVALTYPE_U16;
                            switch (i)
                            {
                                case X86_SREG_ES: paSureRegs[iReg].enmReg = DBGFREG_ES; break;
                                case X86_SREG_CS: paSureRegs[iReg].enmReg = DBGFREG_CS; break;
                                case X86_SREG_SS: paSureRegs[iReg].enmReg = DBGFREG_SS; break;
                                case X86_SREG_DS: paSureRegs[iReg].enmReg = DBGFREG_DS; break;
                                case X86_SREG_FS: paSureRegs[iReg].enmReg = DBGFREG_FS; break;
                                case X86_SREG_GS: paSureRegs[iReg].enmReg = DBGFREG_GS; break;
                                default:          AssertFailedBreak();
                            }
                            iReg++;
                        }

                if (iReg < cRegs)
                {
                    if (pState->u.x86.Loaded.s.fRFlags)
                    {
                        paSureRegs[iReg].Value.u64 = pState->u.x86.uRFlags;
                        paSureRegs[iReg].enmType   = DBGFREGVALTYPE_U64;
                        paSureRegs[iReg].enmReg    = DBGFREG_RFLAGS;
                        iReg++;
                    }
                    if (pState->u.x86.Loaded.s.fErrCd)
                    {
                        paSureRegs[iReg].Value.u64 = pState->u.x86.uErrCd;
                        paSureRegs[iReg].enmType   = DBGFREGVALTYPE_U64;
                        paSureRegs[iReg].enmReg    = DBGFREG_END;
                        paSureRegs[iReg].pszName   = "trap-errcd";
                        iReg++;
                    }
                }
                Assert(iReg == cRegs);
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Internal worker routine.
 *
 * On x86 the typical stack frame layout is like this:
 *     ..  ..
 *     16  parameter 2
 *     12  parameter 1
 *      8  parameter 0
 *      4  return address
 *      0  old ebp; current ebp points here
 */
DECL_NO_INLINE(static, int) dbgfR3StackWalk(PDBGFUNWINDCTX pUnwindCtx, PDBGFSTACKFRAME pFrame, bool fFirst)
{
    /*
     * Stop if we got a read error in the previous run.
     */
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_LAST)
        return VERR_NO_MORE_FILES;

    /*
     * Advance the frame (except for the first).
     */
    if (!fFirst) /** @todo we can probably eliminate this fFirst business... */
    {
        /* frame, pc and stack is taken from the existing frames return members. */
        pFrame->AddrFrame = pFrame->AddrReturnFrame;
        pFrame->AddrPC    = pFrame->AddrReturnPC;
        pFrame->pSymPC    = pFrame->pSymReturnPC;
        pFrame->pLinePC   = pFrame->pLineReturnPC;

        /* increment the frame number. */
        pFrame->iFrame++;

        /* UNWIND_INFO_RET -> USED_UNWIND; return type */
        if (!(pFrame->fFlags & DBGFSTACKFRAME_FLAGS_UNWIND_INFO_RET))
            pFrame->fFlags &= ~DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO;
        else
        {
            pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO;
            pFrame->fFlags &= ~DBGFSTACKFRAME_FLAGS_UNWIND_INFO_RET;
            if (pFrame->enmReturnFrameReturnType != RTDBGRETURNTYPE_INVALID)
            {
                pFrame->enmReturnType = pFrame->enmReturnFrameReturnType;
                pFrame->enmReturnFrameReturnType = RTDBGRETURNTYPE_INVALID;
            }
        }
        pFrame->fFlags &= ~DBGFSTACKFRAME_FLAGS_TRAP_FRAME;
    }

    /*
     * Figure the return address size and use the old PC to guess stack item size.
     */
    /** @todo this is bogus... */
    unsigned cbRetAddr = RTDbgReturnTypeSize(pFrame->enmReturnType);
    unsigned cbStackItem;
    switch (pFrame->AddrPC.fFlags & DBGFADDRESS_FLAGS_TYPE_MASK)
    {
        case DBGFADDRESS_FLAGS_FAR16: cbStackItem = 2; break;
        case DBGFADDRESS_FLAGS_FAR32: cbStackItem = 4; break;
        case DBGFADDRESS_FLAGS_FAR64: cbStackItem = 8; break;
        case DBGFADDRESS_FLAGS_RING0: cbStackItem = sizeof(RTHCUINTPTR); break;
        default:
            switch (pFrame->enmReturnType)
            {
                case RTDBGRETURNTYPE_FAR16:
                case RTDBGRETURNTYPE_IRET16:
                case RTDBGRETURNTYPE_IRET32_V86:
                case RTDBGRETURNTYPE_NEAR16: cbStackItem = 2; break;

                case RTDBGRETURNTYPE_FAR32:
                case RTDBGRETURNTYPE_IRET32:
                case RTDBGRETURNTYPE_IRET32_PRIV:
                case RTDBGRETURNTYPE_NEAR32: cbStackItem = 4; break;

                case RTDBGRETURNTYPE_FAR64:
                case RTDBGRETURNTYPE_IRET64:
                case RTDBGRETURNTYPE_NEAR64: cbStackItem = 8; break;

                default:
                    AssertMsgFailed(("%d\n", pFrame->enmReturnType));
                    cbStackItem = 4;
                    break;
            }
    }

    /*
     * Read the raw frame data.
     * We double cbRetAddr in case we have a far return.
     */
    union
    {
        uint64_t *pu64;
        uint32_t *pu32;
        uint16_t *pu16;
        uint8_t  *pb;
        void     *pv;
    } u, uRet, uArgs, uBp;
    size_t cbRead = cbRetAddr*2 + cbStackItem + sizeof(pFrame->Args);
    u.pv = alloca(cbRead);
    uBp = u;
    uRet.pb = u.pb + cbStackItem;
    uArgs.pb = u.pb + cbStackItem + cbRetAddr;

    Assert(DBGFADDRESS_IS_VALID(&pFrame->AddrFrame));
    int rc = dbgfR3StackRead(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, u.pv, &pFrame->AddrFrame, cbRead, &cbRead);
    if (   RT_FAILURE(rc)
        || cbRead < cbRetAddr + cbStackItem)
        pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_LAST;

    /*
     * Return Frame address.
     *
     * If we used unwind info to get here, the unwind register context will be
     * positioned after the return instruction has been executed.  We start by
     * picking up the rBP register here for return frame and will try improve
     * on it further down by using unwind info.
     */
    pFrame->AddrReturnFrame = pFrame->AddrFrame;
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO)
    {
        if (   pFrame->enmReturnType == RTDBGRETURNTYPE_IRET32_PRIV
            || pFrame->enmReturnType == RTDBGRETURNTYPE_IRET64)
            DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnFrame,
                                 pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_SS], pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xBP]);
        else if (pFrame->enmReturnType == RTDBGRETURNTYPE_IRET32_V86)
            DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &pFrame->AddrReturnFrame,
                                 ((uint32_t)pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_SS] << 4)
                               + pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xBP]);
        else
        {
            pFrame->AddrReturnFrame.off      = pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xBP];
            pFrame->AddrReturnFrame.FlatPtr += pFrame->AddrReturnFrame.off - pFrame->AddrFrame.off;
        }
    }
    else
    {
        switch (cbStackItem)
        {
            case 2:     pFrame->AddrReturnFrame.off = *uBp.pu16; break;
            case 4:     pFrame->AddrReturnFrame.off = *uBp.pu32; break;
            case 8:     pFrame->AddrReturnFrame.off = *uBp.pu64; break;
            default:    AssertMsgFailedReturn(("cbStackItem=%d\n", cbStackItem), VERR_DBGF_STACK_IPE_1);
        }

        /* Watcom tries to keep the frame pointer odd for far returns. */
        if (   cbStackItem <= 4
            && !(pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO))
        {
            if (pFrame->AddrReturnFrame.off & 1)
            {
                pFrame->AddrReturnFrame.off &= ~(RTGCUINTPTR)1;
                if (pFrame->enmReturnType == RTDBGRETURNTYPE_NEAR16)
                {
                    pFrame->fFlags       |= DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN;
                    pFrame->enmReturnType = RTDBGRETURNTYPE_FAR16;
                    cbRetAddr = 4;
                }
                else if (pFrame->enmReturnType == RTDBGRETURNTYPE_NEAR32)
                {
#if 1
                    /* Assumes returning 32-bit code. */
                    pFrame->fFlags       |= DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN;
                    pFrame->enmReturnType = RTDBGRETURNTYPE_FAR32;
                    cbRetAddr = 8;
#else
                    /* Assumes returning 16-bit code. */
                    pFrame->fFlags       |= DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN;
                    pFrame->enmReturnType = RTDBGRETURNTYPE_FAR16;
                    cbRetAddr = 4;
#endif
                }
            }
            else if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN)
            {
                if (pFrame->enmReturnType == RTDBGRETURNTYPE_FAR16)
                {
                    pFrame->enmReturnType = RTDBGRETURNTYPE_NEAR16;
                    cbRetAddr = 2;
                }
                else if (pFrame->enmReturnType == RTDBGRETURNTYPE_NEAR32)
                {
                    pFrame->enmReturnType = RTDBGRETURNTYPE_FAR32;
                    cbRetAddr = 4;
                }
                pFrame->fFlags &= ~DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN;
            }
            uArgs.pb = u.pb + cbStackItem + cbRetAddr;
        }

        pFrame->AddrReturnFrame.FlatPtr += pFrame->AddrReturnFrame.off - pFrame->AddrFrame.off;
    }

    /*
     * Return Stack Address.
     */
    pFrame->AddrReturnStack = pFrame->AddrReturnFrame;
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO)
    {
        if (   pFrame->enmReturnType == RTDBGRETURNTYPE_IRET32_PRIV
            || pFrame->enmReturnType == RTDBGRETURNTYPE_IRET64)
            DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnStack,
                                 pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_SS], pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xSP]);
        else if (pFrame->enmReturnType == RTDBGRETURNTYPE_IRET32_V86)
            DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &pFrame->AddrReturnStack,
                                 ((uint32_t)pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_SS] << 4)
                               + pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xSP]);
        else
        {
            pFrame->AddrReturnStack.off      = pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xSP];
            pFrame->AddrReturnStack.FlatPtr += pFrame->AddrReturnStack.off - pFrame->AddrStack.off;
        }
    }
    else
    {
        pFrame->AddrReturnStack.off     += cbStackItem + cbRetAddr;
        pFrame->AddrReturnStack.FlatPtr += cbStackItem + cbRetAddr;
    }

    /*
     * Return PC.
     */
    pFrame->AddrReturnPC = pFrame->AddrPC;
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO)
    {
        if (RTDbgReturnTypeIsNear(pFrame->enmReturnType))
        {
            pFrame->AddrReturnPC.off      = pUnwindCtx->m_State.uPc;
            pFrame->AddrReturnPC.FlatPtr += pFrame->AddrReturnPC.off - pFrame->AddrPC.off;
        }
        else
            DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC,
                                 pUnwindCtx->m_State.u.x86.auSegs[X86_SREG_CS], pUnwindCtx->m_State.uPc);
    }
    else
    {
        int rc2;
        switch (pFrame->enmReturnType)
        {
            case RTDBGRETURNTYPE_NEAR16:
                if (DBGFADDRESS_IS_VALID(&pFrame->AddrReturnPC))
                {
                    pFrame->AddrReturnPC.FlatPtr += *uRet.pu16 - pFrame->AddrReturnPC.off;
                    pFrame->AddrReturnPC.off      = *uRet.pu16;
                }
                else
                    DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &pFrame->AddrReturnPC, *uRet.pu16);
                break;
            case RTDBGRETURNTYPE_NEAR32:
                if (DBGFADDRESS_IS_VALID(&pFrame->AddrReturnPC))
                {
                    pFrame->AddrReturnPC.FlatPtr += *uRet.pu32 - pFrame->AddrReturnPC.off;
                    pFrame->AddrReturnPC.off      = *uRet.pu32;
                }
                else
                    DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &pFrame->AddrReturnPC, *uRet.pu32);
                break;
            case RTDBGRETURNTYPE_NEAR64:
                if (DBGFADDRESS_IS_VALID(&pFrame->AddrReturnPC))
                {
                    pFrame->AddrReturnPC.FlatPtr += *uRet.pu64 - pFrame->AddrReturnPC.off;
                    pFrame->AddrReturnPC.off      = *uRet.pu64;
                }
                else
                    DBGFR3AddrFromFlat(pUnwindCtx->m_pUVM, &pFrame->AddrReturnPC, *uRet.pu64);
                break;
            case RTDBGRETURNTYPE_FAR16:
                rc2 = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[1], uRet.pu16[0]);
                if (RT_SUCCESS(rc2))
                    break;
                rc2 = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, pFrame->AddrPC.Sel, uRet.pu16[0]);
                if (RT_SUCCESS(rc2))
                    pFrame->enmReturnType = RTDBGRETURNTYPE_NEAR16;
                else
                    DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[1], uRet.pu16[0]);
                break;
            case RTDBGRETURNTYPE_FAR32:
                rc2 = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[2], uRet.pu32[0]);
                if (RT_SUCCESS(rc2))
                    break;
                rc2 = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, pFrame->AddrPC.Sel, uRet.pu32[0]);
                if (RT_SUCCESS(rc2))
                    pFrame->enmReturnType = RTDBGRETURNTYPE_NEAR32;
                else
                    DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[2], uRet.pu32[0]);
                break;
            case RTDBGRETURNTYPE_FAR64:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[4], uRet.pu64[0]);
                break;
            case RTDBGRETURNTYPE_IRET16:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[1], uRet.pu16[0]);
                break;
            case RTDBGRETURNTYPE_IRET32:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[2], uRet.pu32[0]);
                break;
            case RTDBGRETURNTYPE_IRET32_PRIV:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[2], uRet.pu32[0]);
                break;
            case RTDBGRETURNTYPE_IRET32_V86:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[2], uRet.pu32[0]);
                break;
            case RTDBGRETURNTYPE_IRET64:
                DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &pFrame->AddrReturnPC, uRet.pu16[4], uRet.pu64[0]);
                break;
            default:
                AssertMsgFailed(("enmReturnType=%d\n", pFrame->enmReturnType));
                return VERR_INVALID_PARAMETER;
        }
    }


    pFrame->pSymReturnPC  = DBGFR3AsSymbolByAddrA(pUnwindCtx->m_pUVM, pUnwindCtx->m_hAs, &pFrame->AddrReturnPC,
                                                  RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                                  NULL /*poffDisp*/, NULL /*phMod*/);
    pFrame->pLineReturnPC = DBGFR3AsLineByAddrA(pUnwindCtx->m_pUVM, pUnwindCtx->m_hAs, &pFrame->AddrReturnPC,
                                                NULL /*poffDisp*/, NULL /*phMod*/);

    /*
     * Frame bitness flag.
     */
    /** @todo use previous return type for this? */
    pFrame->fFlags &= ~(DBGFSTACKFRAME_FLAGS_16BIT | DBGFSTACKFRAME_FLAGS_32BIT | DBGFSTACKFRAME_FLAGS_64BIT);
    switch (cbStackItem)
    {
        case 2: pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_16BIT; break;
        case 4: pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_32BIT; break;
        case 8: pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_64BIT; break;
        default:    AssertMsgFailedReturn(("cbStackItem=%d\n", cbStackItem), VERR_DBGF_STACK_IPE_2);
    }

    /*
     * The arguments.
     */
    memcpy(&pFrame->Args, uArgs.pv, sizeof(pFrame->Args));

    /*
     * Collect register changes.
     * Then call the OS layer to assist us (e.g. NT trap frames).
     */
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO)
    {
        rc = dbgfR3StackWalkCollectRegisterChanges(pUnwindCtx->m_pUVM, pFrame, &pUnwindCtx->m_State);
        if (RT_FAILURE(rc))
            return rc;

        if (   pUnwindCtx->m_pInitialCtx
            && pUnwindCtx->m_hAs != NIL_RTDBGAS)
        {
            rc = dbgfR3OSStackUnwindAssist(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, pFrame, &pUnwindCtx->m_State,
                                           pUnwindCtx->m_pInitialCtx, pUnwindCtx->m_hAs, &pUnwindCtx->m_uOsScratch);
            if (RT_FAILURE(rc))
                return rc;
        }
    }

    /*
     * Try use unwind information to locate the return frame pointer (for the
     * next loop iteration).
     */
    Assert(!(pFrame->fFlags & DBGFSTACKFRAME_FLAGS_UNWIND_INFO_RET));
    pFrame->enmReturnFrameReturnType = RTDBGRETURNTYPE_INVALID;
    if (!(pFrame->fFlags & DBGFSTACKFRAME_FLAGS_LAST))
    {
        /* Set PC and SP if we didn't unwind our way here (context will then point
           and the return PC and SP already). */
        if (!(pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO))
        {
            dbgfR3UnwindCtxSetPcAndSp(pUnwindCtx, &pFrame->AddrReturnPC, &pFrame->AddrReturnStack);
            pUnwindCtx->m_State.u.x86.auRegs[X86_GREG_xBP] = pFrame->AddrReturnFrame.off;
        }
        /** @todo Reevaluate CS if the previous frame return type isn't near. */
        if (   pUnwindCtx->m_State.enmArch == RTLDRARCH_AMD64
            || pUnwindCtx->m_State.enmArch == RTLDRARCH_X86_32
            || pUnwindCtx->m_State.enmArch == RTLDRARCH_X86_16)
            pUnwindCtx->m_State.u.x86.Loaded.fAll = 0;
        else
            AssertFailed();
        if (dbgfR3UnwindCtxDoOneFrame(pUnwindCtx))
        {
            if (pUnwindCtx->m_fIsHostRing0)
                DBGFR3AddrFromHostR0(&pFrame->AddrReturnFrame, pUnwindCtx->m_State.u.x86.FrameAddr.off);
            else
            {
                DBGFADDRESS AddrReturnFrame = pFrame->AddrReturnFrame;
                rc = DBGFR3AddrFromSelOff(pUnwindCtx->m_pUVM, pUnwindCtx->m_idCpu, &AddrReturnFrame,
                                          pUnwindCtx->m_State.u.x86.FrameAddr.sel, pUnwindCtx->m_State.u.x86.FrameAddr.off);
                if (RT_SUCCESS(rc))
                    pFrame->AddrReturnFrame = AddrReturnFrame;
            }
            pFrame->enmReturnFrameReturnType = pUnwindCtx->m_State.enmRetType;
            pFrame->fFlags                  |= DBGFSTACKFRAME_FLAGS_UNWIND_INFO_RET;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Walks the entire stack allocating memory as we walk.
 */
static DECLCALLBACK(int) dbgfR3StackWalkCtxFull(PUVM pUVM, VMCPUID idCpu, PCCPUMCTX pCtx, RTDBGAS hAs,
                                                DBGFCODETYPE enmCodeType,
                                                PCDBGFADDRESS pAddrFrame,
                                                PCDBGFADDRESS pAddrStack,
                                                PCDBGFADDRESS pAddrPC,
                                                RTDBGRETURNTYPE const *penmReturnType,
                                                PCDBGFSTACKFRAME *ppFirstFrame)
{
    RTDBGRETURNTYPE const enmReturnType = *penmReturnType; /* darwin/arm64 fun, see @bugref{10725} */
    DBGFUNWINDCTX         UnwindCtx(pUVM, idCpu, pCtx, hAs);

    /* alloc first frame. */
    PDBGFSTACKFRAME pCur = (PDBGFSTACKFRAME)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_STACK, sizeof(*pCur));
    if (!pCur)
        return VERR_NO_MEMORY;

    /*
     * Initialize the frame.
     */
    pCur->pNextInternal = NULL;
    pCur->pFirstInternal = pCur;

    int rc = VINF_SUCCESS;
#if defined(VBOX_VMM_TARGET_ARMV8)
    if (pAddrPC)
        pCur->AddrPC = *pAddrPC;
    else
        DBGFR3AddrFromFlat(pUVM, &pCur->AddrPC, pCtx->Pc.u64);
#else
    if (pAddrPC)
        pCur->AddrPC = *pAddrPC;
    else if (enmCodeType != DBGFCODETYPE_GUEST)
        DBGFR3AddrFromFlat(pUVM, &pCur->AddrPC, pCtx->rip);
    else
        rc = DBGFR3AddrFromSelOff(pUVM, idCpu, &pCur->AddrPC, pCtx->cs.Sel, pCtx->rip);
#endif
    if (RT_SUCCESS(rc))
    {
        uint64_t fAddrMask;
        if (enmCodeType == DBGFCODETYPE_RING0)
            fAddrMask = HC_ARCH_BITS == 64 ? UINT64_MAX : UINT32_MAX;
        else if (enmCodeType == DBGFCODETYPE_HYPER)
            fAddrMask = UINT32_MAX;
        else if (DBGFADDRESS_IS_FAR16(&pCur->AddrPC))
            fAddrMask = UINT16_MAX;
        else if (DBGFADDRESS_IS_FAR32(&pCur->AddrPC))
            fAddrMask = UINT32_MAX;
        else if (DBGFADDRESS_IS_FAR64(&pCur->AddrPC))
            fAddrMask = UINT64_MAX;
        else
        {
            PVMCPU const   pVCpu      = pUVM->pVM->apCpusR3[idCpu];
            CPUMMODE const enmCpuMode = CPUMGetGuestMode(pVCpu);
            if (enmCpuMode == CPUMMODE_REAL)
            {
                fAddrMask = UINT16_MAX;
                if (enmReturnType == RTDBGRETURNTYPE_INVALID)
                    pCur->enmReturnType = RTDBGRETURNTYPE_NEAR16;
            }
            else if (   enmCpuMode == CPUMMODE_PROTECTED
                     || !CPUMIsGuestIn64BitCode(pVCpu))
            {
                fAddrMask = UINT32_MAX;
                if (enmReturnType == RTDBGRETURNTYPE_INVALID)
                    pCur->enmReturnType = RTDBGRETURNTYPE_NEAR32;
            }
            else
            {
                fAddrMask = UINT64_MAX;
                if (enmReturnType == RTDBGRETURNTYPE_INVALID)
                    pCur->enmReturnType = RTDBGRETURNTYPE_NEAR64;
            }
        }

        if (enmReturnType == RTDBGRETURNTYPE_INVALID)
            switch (pCur->AddrPC.fFlags & DBGFADDRESS_FLAGS_TYPE_MASK)
            {
                case DBGFADDRESS_FLAGS_FAR16: pCur->enmReturnType = RTDBGRETURNTYPE_NEAR16; break;
                case DBGFADDRESS_FLAGS_FAR32: pCur->enmReturnType = RTDBGRETURNTYPE_NEAR32; break;
                case DBGFADDRESS_FLAGS_FAR64: pCur->enmReturnType = RTDBGRETURNTYPE_NEAR64; break;
                case DBGFADDRESS_FLAGS_RING0:
                    pCur->enmReturnType = HC_ARCH_BITS == 64 ? RTDBGRETURNTYPE_NEAR64 : RTDBGRETURNTYPE_NEAR32;
                    break;
                default:
                    pCur->enmReturnType = RTDBGRETURNTYPE_NEAR32;
                    break;
            }


#if defined(VBOX_VMM_TARGET_ARMV8)
        RT_NOREF(pAddrFrame, pAddrStack);
        AssertFailed();
        rc = VERR_NOT_IMPLEMENTED;
#else
        if (pAddrStack)
            pCur->AddrStack = *pAddrStack;
        else if (enmCodeType != DBGFCODETYPE_GUEST)
            DBGFR3AddrFromFlat(pUVM, &pCur->AddrStack, pCtx->rsp & fAddrMask);
        else
            rc = DBGFR3AddrFromSelOff(pUVM, idCpu, &pCur->AddrStack, pCtx->ss.Sel, pCtx->rsp & fAddrMask);

        Assert(!(pCur->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO));
        if (pAddrFrame)
            pCur->AddrFrame = *pAddrFrame;
        else if (enmCodeType != DBGFCODETYPE_GUEST)
            DBGFR3AddrFromFlat(pUVM, &pCur->AddrFrame, pCtx->rbp & fAddrMask);
        else if (RT_SUCCESS(rc))
            rc = DBGFR3AddrFromSelOff(pUVM, idCpu, &pCur->AddrFrame, pCtx->ss.Sel, pCtx->rbp & fAddrMask);
#endif

        /*
         * Try unwind and get a better frame pointer and state.
         */
        if (   RT_SUCCESS(rc)
            && dbgfR3UnwindCtxSetPcAndSp(&UnwindCtx, &pCur->AddrPC, &pCur->AddrStack)
            && dbgfR3UnwindCtxDoOneFrame(&UnwindCtx))
        {
            pCur->enmReturnType = UnwindCtx.m_State.enmRetType;
            pCur->fFlags |= DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO;
            if (!UnwindCtx.m_fIsHostRing0)
                rc = DBGFR3AddrFromSelOff(UnwindCtx.m_pUVM, UnwindCtx.m_idCpu, &pCur->AddrFrame,
                                          UnwindCtx.m_State.u.x86.FrameAddr.sel, UnwindCtx.m_State.u.x86.FrameAddr.off);
            else
                DBGFR3AddrFromHostR0(&pCur->AddrFrame, UnwindCtx.m_State.u.x86.FrameAddr.off);
        }
        /*
         * The first frame.
         */
        if (RT_SUCCESS(rc))
        {
            if (DBGFADDRESS_IS_VALID(&pCur->AddrPC))
            {
                pCur->pSymPC  = DBGFR3AsSymbolByAddrA(pUVM, hAs, &pCur->AddrPC,
                                                      RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                                      NULL /*poffDisp*/, NULL /*phMod*/);
                pCur->pLinePC = DBGFR3AsLineByAddrA(pUVM, hAs, &pCur->AddrPC, NULL /*poffDisp*/, NULL /*phMod*/);
            }

            rc = dbgfR3StackWalk(&UnwindCtx, pCur, true /*fFirst*/);
        }
    }
    else
        pCur->enmReturnType = enmReturnType;
    if (RT_FAILURE(rc))
    {
        DBGFR3StackWalkEnd(pCur);
        return rc;
    }

    /*
     * The other frames.
     */
    DBGFSTACKFRAME Next = *pCur;
    while (!(pCur->fFlags & (DBGFSTACKFRAME_FLAGS_LAST | DBGFSTACKFRAME_FLAGS_MAX_DEPTH | DBGFSTACKFRAME_FLAGS_LOOP)))
    {
        Next.cSureRegs  = 0;
        Next.paSureRegs = NULL;

        /* try walk. */
        rc = dbgfR3StackWalk(&UnwindCtx, &Next, false /*fFirst*/);
        if (RT_FAILURE(rc))
            break;

        /* add the next frame to the chain. */
        PDBGFSTACKFRAME pNext = (PDBGFSTACKFRAME)MMR3HeapAllocU(pUVM, MM_TAG_DBGF_STACK, sizeof(*pNext));
        if (!pNext)
        {
            DBGFR3StackWalkEnd(pCur);
            return VERR_NO_MEMORY;
        }
        *pNext = Next;
        pCur->pNextInternal = pNext;
        pCur = pNext;
        Assert(pCur->pNextInternal == NULL);

        /* check for loop */
        for (PCDBGFSTACKFRAME pLoop = pCur->pFirstInternal;
             pLoop && pLoop != pCur;
             pLoop = pLoop->pNextInternal)
            if (pLoop->AddrFrame.FlatPtr == pCur->AddrFrame.FlatPtr)
            {
                pCur->fFlags |= DBGFSTACKFRAME_FLAGS_LOOP;
                break;
            }

        /* check for insane recursion */
        if (pCur->iFrame >= 2048)
            pCur->fFlags |= DBGFSTACKFRAME_FLAGS_MAX_DEPTH;
    }

    *ppFirstFrame = pCur->pFirstInternal;
    return rc;
}


/**
 * Common worker for DBGFR3StackWalkBeginGuestEx, DBGFR3StackWalkBeginHyperEx,
 * DBGFR3StackWalkBeginGuest and DBGFR3StackWalkBeginHyper.
 */
static int dbgfR3StackWalkBeginCommon(PUVM pUVM,
                                      VMCPUID idCpu,
                                      DBGFCODETYPE enmCodeType,
                                      PCDBGFADDRESS pAddrFrame,
                                      PCDBGFADDRESS pAddrStack,
                                      PCDBGFADDRESS pAddrPC,
                                      RTDBGRETURNTYPE enmReturnType,
                                      PCDBGFSTACKFRAME *ppFirstFrame)
{
    /*
     * Validate parameters.
     */
    *ppFirstFrame = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);
    if (pAddrFrame)
        AssertReturn(DBGFR3AddrIsValid(pUVM, pAddrFrame), VERR_INVALID_PARAMETER);
    if (pAddrStack)
        AssertReturn(DBGFR3AddrIsValid(pUVM, pAddrStack), VERR_INVALID_PARAMETER);
    if (pAddrPC)
        AssertReturn(DBGFR3AddrIsValid(pUVM, pAddrPC), VERR_INVALID_PARAMETER);
    AssertReturn(enmReturnType >= RTDBGRETURNTYPE_INVALID && enmReturnType < RTDBGRETURNTYPE_END, VERR_INVALID_PARAMETER);

    /*
     * Get the CPUM context pointer and pass it on the specified EMT.
     */
    RTDBGAS     hAs;
    PCCPUMCTX   pCtx;
    switch (enmCodeType)
    {
        case DBGFCODETYPE_GUEST:
            pCtx = CPUMQueryGuestCtxPtr(pVM->apCpusR3[idCpu]);
            hAs  = DBGF_AS_GLOBAL;
            break;
        case DBGFCODETYPE_HYPER:
            pCtx = CPUMQueryGuestCtxPtr(pVM->apCpusR3[idCpu]);
            hAs  = DBGF_AS_RC_AND_GC_GLOBAL;
            break;
        case DBGFCODETYPE_RING0:
            pCtx = NULL;    /* No valid context present. */
            hAs  = DBGF_AS_R0;
            break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3StackWalkCtxFull, 10 | VMREQ_F_EXTRA_ARGS_ALL_PTRS,
                                    pUVM, idCpu, pCtx, hAs, enmCodeType, pAddrFrame, pAddrStack, pAddrPC,
                                    &enmReturnType, ppFirstFrame);
}


/**
 * Begins a guest stack walk, extended version.
 *
 * This will walk the current stack, constructing a list of info frames which is
 * returned to the caller. The caller uses DBGFR3StackWalkNext to traverse the
 * list and DBGFR3StackWalkEnd to release it.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_NO_MEMORY if we're out of memory.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of the virtual CPU which stack we want to walk.
 * @param   enmCodeType     Code type
 * @param   pAddrFrame      Frame address to start at. (Optional)
 * @param   pAddrStack      Stack address to start at. (Optional)
 * @param   pAddrPC         Program counter to start at. (Optional)
 * @param   enmReturnType   The return address type. (Optional)
 * @param   ppFirstFrame    Where to return the pointer to the first info frame.
 */
VMMR3DECL(int) DBGFR3StackWalkBeginEx(PUVM pUVM,
                                      VMCPUID idCpu,
                                      DBGFCODETYPE enmCodeType,
                                      PCDBGFADDRESS pAddrFrame,
                                      PCDBGFADDRESS pAddrStack,
                                      PCDBGFADDRESS pAddrPC,
                                      RTDBGRETURNTYPE enmReturnType,
                                      PCDBGFSTACKFRAME *ppFirstFrame)
{
    return dbgfR3StackWalkBeginCommon(pUVM, idCpu, enmCodeType, pAddrFrame, pAddrStack, pAddrPC, enmReturnType, ppFirstFrame);
}


/**
 * Begins a guest stack walk.
 *
 * This will walk the current stack, constructing a list of info frames which is
 * returned to the caller. The caller uses DBGFR3StackWalkNext to traverse the
 * list and DBGFR3StackWalkEnd to release it.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_NO_MEMORY if we're out of memory.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of the virtual CPU which stack we want to walk.
 * @param   enmCodeType     Code type
 * @param   ppFirstFrame    Where to return the pointer to the first info frame.
 */
VMMR3DECL(int) DBGFR3StackWalkBegin(PUVM pUVM, VMCPUID idCpu, DBGFCODETYPE enmCodeType, PCDBGFSTACKFRAME *ppFirstFrame)
{
    return dbgfR3StackWalkBeginCommon(pUVM, idCpu, enmCodeType, NULL, NULL, NULL, RTDBGRETURNTYPE_INVALID, ppFirstFrame);
}

/**
 * Gets the next stack frame.
 *
 * @returns Pointer to the info for the next stack frame.
 *          NULL if no more frames.
 *
 * @param   pCurrent    Pointer to the current stack frame.
 *
 */
VMMR3DECL(PCDBGFSTACKFRAME) DBGFR3StackWalkNext(PCDBGFSTACKFRAME pCurrent)
{
    return pCurrent
         ? pCurrent->pNextInternal
         : NULL;
}


/**
 * Ends a stack walk process.
 *
 * This *must* be called after a successful first call to any of the stack
 * walker functions. If not called we will leak memory or other resources.
 *
 * @param   pFirstFrame     The frame returned by one of the begin functions.
 */
VMMR3DECL(void) DBGFR3StackWalkEnd(PCDBGFSTACKFRAME pFirstFrame)
{
    if (    !pFirstFrame
        ||  !pFirstFrame->pFirstInternal)
        return;

    PDBGFSTACKFRAME pFrame = (PDBGFSTACKFRAME)pFirstFrame->pFirstInternal;
    while (pFrame)
    {
        PDBGFSTACKFRAME pCur = pFrame;
        pFrame = (PDBGFSTACKFRAME)pCur->pNextInternal;
        if (pFrame)
        {
            if (pCur->pSymReturnPC == pFrame->pSymPC)
                pFrame->pSymPC = NULL;
            if (pCur->pSymReturnPC == pFrame->pSymReturnPC)
                pFrame->pSymReturnPC = NULL;

            if (pCur->pSymPC == pFrame->pSymPC)
                pFrame->pSymPC = NULL;
            if (pCur->pSymPC == pFrame->pSymReturnPC)
                pFrame->pSymReturnPC = NULL;

            if (pCur->pLineReturnPC == pFrame->pLinePC)
                pFrame->pLinePC = NULL;
            if (pCur->pLineReturnPC == pFrame->pLineReturnPC)
                pFrame->pLineReturnPC = NULL;

            if (pCur->pLinePC == pFrame->pLinePC)
                pFrame->pLinePC = NULL;
            if (pCur->pLinePC == pFrame->pLineReturnPC)
                pFrame->pLineReturnPC = NULL;
        }

        RTDbgSymbolFree(pCur->pSymPC);
        RTDbgSymbolFree(pCur->pSymReturnPC);
        RTDbgLineFree(pCur->pLinePC);
        RTDbgLineFree(pCur->pLineReturnPC);

        if (pCur->paSureRegs)
        {
            MMR3HeapFree(pCur->paSureRegs);
            pCur->paSureRegs = NULL;
            pCur->cSureRegs = 0;
        }

        pCur->pNextInternal = NULL;
        pCur->pFirstInternal = NULL;
        pCur->fFlags = 0;
        MMR3HeapFree(pCur);
    }
}

