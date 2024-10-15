/* $Id: IEMAllN8veLiveness.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Native Recompiler, Liveness Analysis, Common Header.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IEM
#define IEM_WITH_OPAQUE_DECODER_STATE
#include <VBox/vmm/iem.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>

#include "IEMN8veRecompiler.h"
#include "IEMThreadedFunctions.h"
#include "IEMNativeFunctions.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define NOP() ((void)0)


/*
 * BEGIN & END as well as internal workers.
 */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_MC_BEGIN_EX(a_fMcFlags, a_fCImplFlags, a_cArgs) \
    { \
        /* Define local variables that we use to accumulate the liveness state changes in. */ \
        IEMLIVENESSBIT  LiveStateBit0   = { 0 }; \
        IEMLIVENESSBIT  LiveStateBit1   = { 0 }; \
        IEMLIVENESSBIT  LiveMask        = { 0 }; \
        bool            fDoneXpctOrCall = false
#else
# define IEM_MC_BEGIN_EX(a_fMcFlags, a_fCImplFlags, a_cArgs) \
    { \
        /* Define local variables that we use to accumulate the liveness state changes in. */ \
        IEMLIVENESSENTRY LiveState       = { { 0, 0, 0, 0 } }; \
        IEMLIVENESSBIT   LiveMask        = { 0 }; \
        bool             fNoInherit      = false
#endif

#ifndef IEMLIVENESS_EXTENDED_LAYOUT
AssertCompile(IEMLIVENESS_STATE_INPUT == IEMLIVENESS_STATE_MASK);
AssertCompile(IEMLIVENESSBIT0_XCPT_OR_CALL == 0 && IEMLIVENESSBIT1_XCPT_OR_CALL != 0);
# define IEM_LIVENESS_MARK_CALL_OR_POT_CALL_INTERNAL() do { \
            if (!fDoneXpctOrCall) \
            { \
                LiveStateBit0.bm64 |= pIncoming->Bit0.bm64 & pIncoming->Bit1.bm64 & ~LiveMask.bm64; \
                LiveStateBit1.bm64 |= IEMLIVENESSBIT1_XCPT_OR_CALL; \
                \
                LiveMask.bm64   |= IEMLIVENESSBIT_MASK; /* could also use UINT64_MAX here, but makes little no(?) difference */ \
                fDoneXpctOrCall  = true;                /* when compiling with gcc and cl.exe on x86 - may on arm, though. */ \
            } \
        } while (0)
# define IEM_LIVENESS_MARK_POTENTIAL_CALL() IEM_LIVENESS_MARK_CALL_OR_POT_CALL_INTERNAL()
# define IEM_LIVENESS_MARK_CALL()           IEM_LIVENESS_MARK_CALL_OR_POT_CALL_INTERNAL()
#else
# define IEM_LIVENESS_MARK_POTENTIAL_CALL() do { \
            LiveState.aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 |= IEMLIVENESSBIT_MASK; \
        } while (0)
# define IEM_LIVENESS_MARK_CALL() do { \
            LiveState.aBits[IEMLIVENESS_BIT_CALL].bm64 |= IEMLIVENESSBIT_MASK; \
            LiveMask.bm64                              |= IEMLIVENESSBIT_MASK; \
            fNoInherit                                  = true; /* full mask */ \
        } while (0)
#endif


#ifndef IEMLIVENESS_EXTENDED_LAYOUT
AssertCompile(IEMLIVENESS_STATE_CLOBBERED == 0);
# define IEM_LIVENESS_ALL_EFLAGS_CLOBBER() do { \
            LiveMask.bm64       |= IEMLIVENESSBIT_ALL_EFL_MASK; \
        } while (0)
AssertCompile(IEMLIVENESS_STATE_INPUT == IEMLIVENESS_STATE_MASK);
# define IEM_LIVENESS_ALL_EFLAGS_INPUT() do { \
            LiveStateBit0.bm64  |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveStateBit1.bm64  |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveMask.bm64       |= IEMLIVENESSBIT_ALL_EFL_MASK; \
        } while (0)
# define IEM_LIVENESS_ALL_EFLAGS_MODIFY() IEM_LIVENESS_ALL_EFLAGS_INPUT()
#else
# define IEM_LIVENESS_ALL_EFLAGS_CLOBBER() do { \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].bm64 |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveMask.bm64                               |= IEMLIVENESSBIT_ALL_EFL_MASK; \
        } while (0)
# define IEM_LIVENESS_ALL_EFLAGS_INPUT() do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].bm64  |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveMask.bm64                               |= IEMLIVENESSBIT_ALL_EFL_MASK; \
        } while (0)
# define IEM_LIVENESS_ALL_EFLAGS_MODIFY() do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].bm64  |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].bm64 |= IEMLIVENESSBIT_ALL_EFL_MASK; \
            LiveMask.bm64                               |= IEMLIVENESSBIT_ALL_EFL_MASK; \
        } while (0)
#endif


#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_ONE_EFLAG_CLOBBER(a_Name) do { \
            LiveMask.a_Name       |= 1; \
        } while (0)
# define IEM_LIVENESS_ONE_EFLAG_INPUT(a_Name) do { \
            LiveStateBit0.a_Name  |= 1; \
            LiveStateBit1.a_Name  |= 1; \
            LiveMask.a_Name       |= 1; \
        } while (0)
# define IEM_LIVENESS_ONE_EFLAG_MODIFY(a_Name) IEM_LIVENESS_ONE_EFLAG_INPUT(a_Name)
#else
# define IEM_LIVENESS_ONE_EFLAG_CLOBBER(a_Name) do { \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].a_Name |= 1; \
            LiveMask.a_Name                               |= 1; \
        } while (0)
# define IEM_LIVENESS_ONE_EFLAG_INPUT(a_Name) do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].a_Name  |= 1; \
            LiveMask.a_Name                               |= 1; \
        } while (0)
# define IEM_LIVENESS_ONE_EFLAG_MODIFY(a_Name) do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].a_Name  |= 1; \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].a_Name |= 1; \
            LiveMask.a_Name                               |= 1; \
        } while (0)
#endif


/* Generic bitmap (bmGpr, bmSegBase, ++) setters. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(a_bmMember, a_iElement) do { \
            LiveMask.a_bmMember  |= RT_BIT_64(a_iElement); \
        } while (0)
# define IEM_LIVENESS_BITMAP_MEMBER_INPUT(a_bmMember, a_iElement) do { \
            LiveStateBit0.a_bmMember  |= RT_BIT_64(a_iElement); \
            LiveStateBit1.a_bmMember  |= RT_BIT_64(a_iElement); \
            LiveMask.a_bmMember       |= RT_BIT_64(a_iElement); \
        } while (0)
# define IEM_LIVENESS_BITMAP_MEMBER_MODIFY(a_bmMember, a_iElement) IEM_LIVENESS_BITMAP_MEMBER_INPUT(a_bmMember, a_iElement)
#else
# define IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(a_bmMember, a_iElement) do { \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].a_bmMember |= RT_BIT_64(a_iElement); \
            LiveMask.a_bmMember                               |= RT_BIT_64(a_iElement); \
        } while (0)
# define IEM_LIVENESS_BITMAP_MEMBER_INPUT(a_bmMember, a_iElement) do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].a_bmMember  |= RT_BIT_64(a_iElement); \
            LiveMask.a_bmMember                               |= RT_BIT_64(a_iElement); \
        } while (0)
# define IEM_LIVENESS_BITMAP_MEMBER_MODIFY(a_bmMember, a_iElement) do { \
            LiveState.aBits[IEMLIVENESS_BIT_READ].a_bmMember  |= RT_BIT_64(a_iElement); \
            LiveState.aBits[IEMLIVENESS_BIT_WRITE].a_bmMember |= RT_BIT_64(a_iElement); \
            LiveMask.a_bmMember                               |= RT_BIT_64(a_iElement); \
        } while (0)
#endif


#define IEM_LIVENESS_CR0_INPUT()                    IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fCr0,   0)
#define IEM_LIVENESS_CR4_INPUT()                    IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fCr4,   0)
#define IEM_LIVENESS_XCR0_INPUT()                   IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fXcr0,  0)


#define IEM_LIVENESS_FCW_INPUT()                    IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fFcw, 0)
#define IEM_LIVENESS_FCW_CLOBBER()                  IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(fFcw, 0)
#define IEM_LIVENESS_FCW_MODIFY()                   IEM_LIVENESS_BITMAP_MEMBER_MODIFY( fFcw, 0)


#define IEM_LIVENESS_FSW_INPUT()                    IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fFsw, 0)
#define IEM_LIVENESS_FSW_CLOBBER()                  IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(fFsw, 0)
#define IEM_LIVENESS_FSW_MODIFY()                   IEM_LIVENESS_BITMAP_MEMBER_MODIFY( fFsw, 0)


#define IEM_LIVENESS_MXCSR_INPUT()                  IEM_LIVENESS_BITMAP_MEMBER_INPUT(  fMxCsr, 0)
#define IEM_LIVENESS_MXCSR_CLOBBER()                IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(fMxCsr, 0)
#define IEM_LIVENESS_MXCSR_MODIFY()                 IEM_LIVENESS_BITMAP_MEMBER_MODIFY( fMxCsr, 0)


#define IEM_LIVENESS_GPR_CLOBBER(a_idxGpr)          IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(bmGprs, a_idxGpr)
#define IEM_LIVENESS_GPR_INPUT(a_idxGpr)            IEM_LIVENESS_BITMAP_MEMBER_INPUT(  bmGprs, a_idxGpr)
#define IEM_LIVENESS_GPR_MODIFY(a_idxGpr)           IEM_LIVENESS_BITMAP_MEMBER_MODIFY( bmGprs, a_idxGpr)


#define IEM_LIVENESS_SEG_BASE_CLOBBER(a_iSeg)       IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(bmSegBase, a_iSeg)
#define IEM_LIVENESS_SEG_BASE_INPUT(a_iSeg)         IEM_LIVENESS_BITMAP_MEMBER_INPUT(  bmSegBase, a_iSeg)
#define IEM_LIVENESS_SEG_BASE_MODIFY(a_iSeg)        IEM_LIVENESS_BITMAP_MEMBER_MODIFY( bmSegBase, a_iSeg)


#define IEM_LIVENESS_SEG_ATTRIB_CLOBBER(a_iSeg)     IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(bmSegAttrib, a_iSeg)
#define IEM_LIVENESS_SEG_ATTRIB_INPUT(a_iSeg)       IEM_LIVENESS_BITMAP_MEMBER_INPUT(  bmSegAttrib, a_iSeg)
#define IEM_LIVENESS_SEG_ATTRIB_MODIFY(a_iSeg)      IEM_LIVENESS_BITMAP_MEMBER_MODFIY( bmSegAttrib, a_iSeg)


#define IEM_LIVENESS_SEG_LIMIT_CLOBBER(a_iSeg)      IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(bmSegLimit, a_iSeg)
#define IEM_LIVENESS_SEG_LIMIT_INPUT(a_iSeg)        IEM_LIVENESS_BITMAP_MEMBER_INPUT(  bmSegLimit, a_iSeg)
#define IEM_LIVENESS_SEG_LIMIT_MODIFY(a_iSeg)       IEM_LIVENESS_BITMAP_MEMBER_MODIFY( bmSegLimit, a_iSeg)


#define IEM_LIVENESS_SEG_SEL_CLOBBER(a_iSeg)        IEM_LIVENESS_BITMAP_MEMBER_CLOBBER(bmSegSel, a_iSeg)
#define IEM_LIVENESS_SEG_SEL_INPUT(a_iSeg)          IEM_LIVENESS_BITMAP_MEMBER_INPUT(  bmSegSel, a_iSeg)
#define IEM_LIVENESS_SEG_SEL_MODIFY(a_iSeg)         IEM_LIVENESS_BITMAP_MEMBER_MODIFY( bmSegSel, a_iSeg)


#define IEM_LIVENESS_MEM(a_iSeg) do { \
        IEM_LIVENESS_MARK_POTENTIAL_CALL(); \
        IEM_LIVENESS_SEG_ATTRIB_INPUT(a_iSeg); \
        IEM_LIVENESS_SEG_BASE_INPUT(a_iSeg); \
        IEM_LIVENESS_SEG_LIMIT_INPUT(a_iSeg); \
    } while (0)

#define IEM_LIVENESS_MEM_FLAT() IEM_LIVENESS_MARK_POTENTIAL_CALL()

#define IEM_LIVENESS_STACK() do { \
        IEM_LIVENESS_MEM(X86_SREG_SS); \
        IEM_LIVENESS_GPR_MODIFY(X86_GREG_xSP); \
    } while (0)

#define IEM_LIVENESS_STACK_FLAT() do { \
        IEM_LIVENESS_MEM_FLAT(); \
        IEM_LIVENESS_GPR_MODIFY(X86_GREG_xSP); \
    } while (0)


#define IEM_LIVENESS_PC_NO_FLAGS()                  NOP()
#define IEM_LIVENESS_PC_WITH_FLAGS()                IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther)

#define IEM_LIVENESS_PC16_JMP_NO_FLAGS()            IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_SEG_LIMIT_INPUT(X86_SREG_CS)
#define IEM_LIVENESS_PC32_JMP_NO_FLAGS()            IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_SEG_LIMIT_INPUT(X86_SREG_CS)
#define IEM_LIVENESS_PC32_FLAT_JMP_NO_FLAGS()       IEM_LIVENESS_MARK_POTENTIAL_CALL()
#define IEM_LIVENESS_PC64_JMP_NO_FLAGS()            IEM_LIVENESS_MARK_POTENTIAL_CALL()
#define IEM_LIVENESS_PC64_INTRAPG_JMP_NO_FLAGS()    IEM_LIVENESS_MARK_POTENTIAL_CALL() /* Typically ends TB. */

#define IEM_LIVENESS_PC16_JMP_WITH_FLAGS()          IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther); IEM_LIVENESS_SEG_LIMIT_INPUT(X86_SREG_CS)
#define IEM_LIVENESS_PC32_JMP_WITH_FLAGS()          IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther); IEM_LIVENESS_SEG_LIMIT_INPUT(X86_SREG_CS)
#define IEM_LIVENESS_PC32_FLAT_JMP_WITH_FLAGS()     IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther)
#define IEM_LIVENESS_PC64_JMP_WITH_FLAGS()          IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther)
#define IEM_LIVENESS_PC64_INTRAPG_JMP_WITH_FLAGS()  IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther)


#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_MC_END() \
        /* Combine the incoming state with what we've accumulated in this block. */ \
        /* We can help the compiler by skipping OR'ing when having applied XPCT_OR_CALL, */ \
        /* since that already imports all the incoming state. Saves a lot with cl.exe. */ \
        if (!fDoneXpctOrCall) \
        { \
            pOutgoing->Bit0.bm64 = LiveStateBit0.bm64 | (~LiveMask.bm64 & pIncoming->Bit0.bm64); \
            pOutgoing->Bit1.bm64 = LiveStateBit1.bm64 | (~LiveMask.bm64 & pIncoming->Bit1.bm64); \
        } \
        else \
        { \
            pOutgoing->Bit0.bm64 = LiveStateBit0.bm64; \
            pOutgoing->Bit1.bm64 = LiveStateBit1.bm64; \
        } \
    }
#else
# define IEM_MC_END() \
        /* Use the mask to effect inheriting. */ \
        if (!fNoInherit) \
        { \
            uint64_t const fInhMask = ~LiveMask.bm64; \
            pOutgoing->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 = LiveState.aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 \
                                                                | (pIncoming->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 & fInhMask); \
            pOutgoing->aBits[IEMLIVENESS_BIT_READ ].bm64 = LiveState.aBits[IEMLIVENESS_BIT_READ ].bm64 \
                                                       | (pIncoming->aBits[IEMLIVENESS_BIT_READ ].bm64 & fInhMask); \
            pOutgoing->aBits[IEMLIVENESS_BIT_WRITE].bm64 = LiveState.aBits[IEMLIVENESS_BIT_WRITE].bm64 \
                                                       | (pIncoming->aBits[IEMLIVENESS_BIT_WRITE].bm64 & fInhMask); \
            pOutgoing->aBits[IEMLIVENESS_BIT_CALL ].bm64 = LiveState.aBits[IEMLIVENESS_BIT_CALL ].bm64 \
                                                       | (pIncoming->aBits[IEMLIVENESS_BIT_CALL ].bm64 & fInhMask); \
        } \
        else \
            *pOutgoing = LiveState; \
    }
#endif

/*
 * The native MC variants.
 */
#define IEM_MC_FREE_LOCAL(a_Name)   NOP()
#define IEM_MC_FREE_ARG(a_Name)     NOP()


/*
 * The THREADED MC variants.
 */

/* We don't track RIP (PC) liveness. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16(a_cbInstr, a_rcNormal)              IEM_LIVENESS_PC_NO_FLAGS()
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32(a_cbInstr, a_rcNormal)              IEM_LIVENESS_PC_NO_FLAGS()
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64(a_cbInstr, a_rcNormal)              IEM_LIVENESS_PC_NO_FLAGS()
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_cbInstr, a_rcNormal)   IEM_LIVENESS_PC_WITH_FLAGS()
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_cbInstr, a_rcNormal)   IEM_LIVENESS_PC_WITH_FLAGS()
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_cbInstr, a_rcNormal)   IEM_LIVENESS_PC_WITH_FLAGS()

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16(a_i8, a_cbInstr, a_rcNormal)                                     IEM_LIVENESS_PC16_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)                     IEM_LIVENESS_PC32_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)                IEM_LIVENESS_PC32_FLAT_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)                     IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)             IEM_LIVENESS_PC64_INTRAPG_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i8, a_cbInstr, a_rcNormal)                          IEM_LIVENESS_PC16_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)          IEM_LIVENESS_PC32_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)     IEM_LIVENESS_PC32_FLAT_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)          IEM_LIVENESS_PC64_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal)  IEM_LIVENESS_PC64_INTRAPG_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC16_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC32_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT(a_i16, a_cbInstr, a_rcNormal)                              IEM_LIVENESS_PC32_FLAT_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG(a_i16, a_cbInstr, a_rcNormal)                           IEM_LIVENESS_PC64_INTRAPG_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC16_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC32_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal)                   IEM_LIVENESS_PC32_FLAT_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC64_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal)                IEM_LIVENESS_PC64_INTRAPG_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16(a_i32, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC16_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC32_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT(a_i32, a_cbInstr, a_rcNormal)                              IEM_LIVENESS_PC32_FLAT_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64(a_i32, a_cbInstr, a_rcNormal)                                   IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG(a_i32, a_cbInstr, a_rcNormal)                           IEM_LIVENESS_PC64_INTRAPG_JMP_NO_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC16_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC32_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal)                   IEM_LIVENESS_PC32_FLAT_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal)                        IEM_LIVENESS_PC64_JMP_WITH_FLAGS()
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal)                IEM_LIVENESS_PC64_INTRAPG_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16(a_u16NewIP)                                                     IEM_LIVENESS_PC16_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32(a_u16NewIP)                                                     IEM_LIVENESS_PC32_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64(a_u16NewIP)                                                     IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP)                                          IEM_LIVENESS_PC16_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP)                                          IEM_LIVENESS_PC32_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16NewIP)                                          IEM_LIVENESS_PC64_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP)                                                    IEM_LIVENESS_PC32_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP)                                                    IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP)                                         IEM_LIVENESS_PC32_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP)                                         IEM_LIVENESS_PC64_JMP_WITH_FLAGS()
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64(a_u32NewEIP)                                                    IEM_LIVENESS_PC64_JMP_NO_FLAGS()
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP)                                         IEM_LIVENESS_PC64_JMP_WITH_FLAGS()

#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr)                      do { IEM_LIVENESS_PC16_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr)                      do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr)                      do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr)           do { IEM_LIVENESS_PC16_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr)           do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr)           do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC16(a_i32, a_cbInstr)                      do { IEM_LIVENESS_PC16_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr)                      do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64(a_i32, a_cbInstr)                      do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i32, a_cbInstr)           do { IEM_LIVENESS_PC16_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr)           do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i32, a_cbInstr)           do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32(a_i64, a_cbInstr)                      do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64(a_i64, a_cbInstr)                      do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i64, a_cbInstr)           do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i64, a_cbInstr)           do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16(a_u16NewIP, a_cbInstr)                 do { IEM_LIVENESS_PC16_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32(a_u16NewIP, a_cbInstr)                 do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64(a_u16NewIP, a_cbInstr)                 do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP, a_cbInstr)      do { IEM_LIVENESS_PC16_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP, a_cbInstr)      do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16NewIP, a_cbInstr)      do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP, a_cbInstr)                do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP, a_cbInstr)                do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP, a_cbInstr)     do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP, a_cbInstr)     do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64(a_u32NewRIP, a_cbInstr)                do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewRIP, a_cbInstr)     do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)

#define IEM_MC_RETN_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr)                              do { IEM_LIVENESS_PC16_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr, a_enmEffOpSize)              do { IEM_LIVENESS_PC32_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr, a_enmEffOpSize)              do { IEM_LIVENESS_PC64_JMP_NO_FLAGS();   IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_RETN_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr)                   do { IEM_LIVENESS_PC16_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr, a_enmEffOpSize)   do { IEM_LIVENESS_PC32_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr, a_enmEffOpSize)   do { IEM_LIVENESS_PC64_JMP_WITH_FLAGS(); IEM_LIVENESS_STACK(); } while (0)

/* Effective address stuff is rather complicated... */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_16(a_GCPtrEff, a_bRm, a_u16Disp) do { \
        if (((a_bRm) & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) != 6) \
        { \
            switch ((a_bRm) & X86_MODRM_RM_MASK) \
            { \
                case 0: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xSI); break; \
                case 1: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDI); break; \
                case 2: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBP); IEM_LIVENESS_GPR_INPUT(X86_GREG_xSI); break; \
                case 3: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBP); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDI); break; \
                case 4: IEM_LIVENESS_GPR_INPUT(X86_GREG_xSI); break; \
                case 5: IEM_LIVENESS_GPR_INPUT(X86_GREG_xDI); break; \
                case 6: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBP); break; \
                case 7: IEM_LIVENESS_GPR_INPUT(X86_GREG_xBX); break; \
            } \
        } \
    } while (0)

#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_32(a_GCPtrEff, a_bRm, a_uSibAndRspOffset, a_u32Disp) do { \
        if (((a_bRm) & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) != 5) \
        { \
            uint8_t const idxReg = (a_bRm) & X86_MODRM_RM_MASK; \
            if (idxReg != 4 /*SIB*/) \
                IEM_LIVENESS_GPR_INPUT(idxReg); \
            else \
            { \
                uint8_t const idxIndex = ((a_uSibAndRspOffset) >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK; \
                if (idxIndex != 4 /*no index*/) \
                    IEM_LIVENESS_GPR_INPUT(idxIndex); \
                \
                uint8_t const idxBase = (a_uSibAndRspOffset) & X86_SIB_BASE_MASK; \
                if (idxBase != 5 || ((a_bRm) & X86_MODRM_MOD_MASK) != 0) \
                    IEM_LIVENESS_GPR_INPUT(idxBase); \
            } \
        } \
    } while (0)

#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) do { \
        if (((a_bRmEx) & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5) \
        { /* RIP */ } \
        else \
        { \
            uint8_t const idxReg = (a_bRmEx) & (X86_MODRM_RM_MASK | 0x8); /* bRmEx[bit 3] = REX.B */ \
            if ((idxReg & X86_MODRM_RM_MASK) != 4 /* not SIB */) \
                IEM_LIVENESS_GPR_INPUT(idxReg); \
            else /* SIB: */\
            { \
                uint8_t const idxIndex = (((a_uSibAndRspOffset) >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK) \
                                       | (((a_bRmEx) & 0x10) >> 1); /* bRmEx[bit 4] = REX.X */ \
                if (idxIndex != 4 /*no index*/) \
                    IEM_LIVENESS_GPR_INPUT(idxIndex); \
                \
                uint8_t const idxBase = ((a_uSibAndRspOffset) & X86_SIB_BASE_MASK) | ((a_bRmEx) & 0x8); /* bRmEx[bit 3] = REX.B */ \
                if ((idxBase & 7) != 5 /* and !13*/ || ((a_bRmEx) & X86_MODRM_MOD_MASK) != 0) \
                    IEM_LIVENESS_GPR_INPUT(idxBase); \
            } \
        } \
    } while (0)
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    IEM_MC_CALC_RM_EFF_ADDR_THREADED_64(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm)
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    IEM_MC_CALC_RM_EFF_ADDR_THREADED_64(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm)

/* At present we don't know what any CIMPL may require as input, however they
   shouldn't ever throw an exception, so it should suffice to mark them as
   unconditional calls. */
#define IEM_MC_CALL_CIMPL_1_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_LIVENESS_MARK_CALL()
#define IEM_MC_CALL_CIMPL_2_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_LIVENESS_MARK_CALL()
#define IEM_MC_CALL_CIMPL_3_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_LIVENESS_MARK_CALL()
#define IEM_MC_CALL_CIMPL_4_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    IEM_LIVENESS_MARK_CALL()
#define IEM_MC_CALL_CIMPL_5_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    IEM_LIVENESS_MARK_CALL()

#define IEM_MC_DEFER_TO_CIMPL_0_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)
#define IEM_MC_DEFER_TO_CIMPL_1_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)
#define IEM_MC_DEFER_TO_CIMPL_2_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)
#define IEM_MC_DEFER_TO_CIMPL_3_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)
#define IEM_MC_DEFER_TO_CIMPL_4_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)
#define IEM_MC_DEFER_TO_CIMPL_5_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    IEM_LIVENESS_RAW_INIT_WITH_CALL(pOutgoing, pIncoming)

/* Any 8-bit register fetch, store or modification only works on part of the register
   and must therefore be considered INPUTs. */
#define IEM_MC_FETCH_GREG_U8_THREADED(a_u8Dst, a_iGRegEx)           IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_ZX_U16_THREADED(a_u16Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_ZX_U32_THREADED(a_u32Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_ZX_U64_THREADED(a_u64Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_SX_U16_THREADED(a_u16Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_SX_U32_THREADED(a_u32Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_FETCH_GREG_U8_SX_U64_THREADED(a_u64Dst, a_iGRegEx)   IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_STORE_GREG_U8_THREADED(a_iGRegEx, a_u8Value)         IEM_LIVENESS_GPR_MODIFY(a_iGRegEx & 15)
#define IEM_MC_STORE_GREG_U8_CONST_THREADED(a_iGRegEx, a_u8Value)   IEM_LIVENESS_GPR_MODIFY(a_iGRegEx & 15)
#define IEM_MC_REF_GREG_U8_THREADED(a_pu8Dst, a_iGRegEx)            IEM_LIVENESS_GPR_MODIFY(a_iGRegEx & 15)
#define IEM_MC_REF_GREG_U8_CONST_THREADED(a_pu8Dst, a_iGRegEx)      IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_ADD_GREG_U8_TO_LOCAL_THREADED(a_u8Value, a_iGRegEx)  IEM_LIVENESS_GPR_INPUT(a_iGRegEx & 15)
#define IEM_MC_AND_GREG_U8_THREADED(a_iGRegEx, a_u8Value)           IEM_LIVENESS_GPR_MODIFY(a_iGRegEx & 15)
#define IEM_MC_OR_GREG_U8_THREADED(a_iGRegEx, a_u8Value)            IEM_LIVENESS_GPR_MODIFY(a_iGRegEx & 15)


/*
 * The other MCs.
 */

#define IEM_MC_NO_NATIVE_RECOMPILE()                                NOP()

#define IEM_MC_RAISE_DIVIDE_ERROR_IF_LOCAL_IS_ZERO(a_uVar)          IEM_LIVENESS_MARK_POTENTIAL_CALL()
#define IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE()                   IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR0_INPUT()
#define IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE()              IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR0_INPUT()
#define IEM_MC_MAYBE_RAISE_FPU_XCPT()                               IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_FSW_INPUT()
#define IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() \
    IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR0_INPUT(); IEM_LIVENESS_CR4_INPUT(); IEM_LIVENESS_XCR0_INPUT()
#define IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() \
    IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR0_INPUT(); IEM_LIVENESS_CR4_INPUT()
#define IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT() \
    IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR0_INPUT(); IEM_LIVENESS_FSW_INPUT()
#define IEM_MC_RAISE_GP0_IF_CPL_NOT_ZERO()                          IEM_LIVENESS_MARK_POTENTIAL_CALL() /**< @todo not conditional */
#define IEM_MC_RAISE_GP0_IF_EFF_ADDR_UNALIGNED(a_EffAddr, a_cbAlign) IEM_LIVENESS_MARK_POTENTIAL_CALL()
#define IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT()                          IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR4_INPUT()
#define IEM_MC_MAYBE_RAISE_NON_CANONICAL_ADDR_GP0(a_u64Addr)        IEM_LIVENESS_MARK_POTENTIAL_CALL()

#define IEM_MC_LOCAL(a_Type, a_Name)                                NOP()
#define IEM_MC_LOCAL_ASSIGN(a_Type, a_Name, a_Value)                NOP()
#define IEM_MC_LOCAL_CONST(a_Type, a_Name, a_Value)                 NOP()
#define IEM_MC_NOREF(a_Name)                                        NOP()
#define IEM_MC_ARG(a_Type, a_Name, a_iArg)                          NOP()
#define IEM_MC_ARG_CONST(a_Type, a_Name, a_Value, a_iArg)           NOP()
#define IEM_MC_ARG_LOCAL_REF(a_Type, a_Name, a_Local, a_iArg)       NOP()

#undef  IEM_MC_COMMIT_EFLAGS /* unused here */
#define IEM_MC_COMMIT_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) do { \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_CF, fEflCf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_PF, fEflPf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_AF, fEflAf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_ZF, fEflZf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_SF, fEflSf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_OF, fEflOf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, ~X86_EFL_STATUS_BITS, fEflOther); \
        Assert(!(  ((a_fEflInput) | (a_fEflOutput)) \
                 & ~(uint32_t)(X86_EFL_STATUS_BITS | X86_EFL_DF | X86_EFL_VM | X86_EFL_VIF | X86_EFL_IOPL))); \
    } while (0)
#undef  IEM_MC_COMMIT_EFLAGS_OPT /* unused here */
#define IEM_MC_COMMIT_EFLAGS_OPT_EX(a_EFlags, a_fEflInput, a_fEflOutput) \
    IEM_MC_COMMIT_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput)

#define IEM_MC_ASSIGN_TO_SMALLER(a_VarDst, a_VarSrcEol)             NOP()

#define IEM_MC_FETCH_GREG_I16(a_i16Dst, a_iGReg)                    IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U16(a_u16Dst, a_iGReg)                    IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U16_ZX_U32(a_u32Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U16_ZX_U64(a_u64Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U16_SX_U32(a_u32Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U16_SX_U64(a_u64Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_I32(a_i32Dst, a_iGReg)                    IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U32(a_u32Dst, a_iGReg)                    IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U32_ZX_U64(a_u64Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U32_SX_U64(a_u64Dst, a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U64(a_u64Dst, a_iGReg)                    IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_FETCH_GREG_U64_ZX_U64                                IEM_MC_FETCH_GREG_U64
#define IEM_MC_FETCH_GREG_PAIR_U32(a_u64Dst, a_iGRegLo, a_iGRegHi) \
    do { IEM_LIVENESS_GPR_INPUT(a_iGRegLo); IEM_LIVENESS_GPR_INPUT(a_iGRegHi); } while(0)
#define IEM_MC_FETCH_GREG_PAIR_U64(a_u128Dst, a_iGRegLo, a_iGRegHi) \
    do { IEM_LIVENESS_GPR_INPUT(a_iGRegLo); IEM_LIVENESS_GPR_INPUT(a_iGRegHi); } while(0)
#define IEM_MC_FETCH_SREG_U16(a_u16Dst, a_iSReg)                    IEM_LIVENESS_SEG_SEL_INPUT(a_iSReg)
#define IEM_MC_FETCH_SREG_ZX_U32(a_u32Dst, a_iSReg)                 IEM_LIVENESS_SEG_SEL_INPUT(a_iSReg)
#define IEM_MC_FETCH_SREG_ZX_U64(a_u64Dst, a_iSReg)                 IEM_LIVENESS_SEG_SEL_INPUT(a_iSReg)
#define IEM_MC_FETCH_SREG_BASE_U64(a_u64Dst, a_iSReg)               IEM_LIVENESS_SEG_BASE_INPUT(a_iSReg)
#define IEM_MC_FETCH_SREG_BASE_U32(a_u32Dst, a_iSReg)               IEM_LIVENESS_SEG_BASE_INPUT(a_iSReg)
#undef  IEM_MC_FETCH_EFLAGS /* unused here */
#define IEM_MC_FETCH_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) do { \
        /* IEM_MC_COMMIT_EFLAGS_EX doesn't cover input-only situations.  This OTOH, leads \
           to duplication in many cases, but the compiler's optimizers should help with that. */ \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_CF, fEflCf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_PF, fEflPf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_AF, fEflAf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_ZF, fEflZf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_SF, fEflSf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_OF, fEflOf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, ~X86_EFL_STATUS_BITS, fEflOther); \
        Assert(!(  ((a_fEflInput) | (a_fEflOutput)) \
                 & ~(uint32_t)(X86_EFL_STATUS_BITS | X86_EFL_DF | X86_EFL_VM | X86_EFL_VIF | X86_EFL_IOPL))); \
    } while (0)
#define IEM_MC_FETCH_EFLAGS_U8(a_EFlags) do { \
        IEM_LIVENESS_ONE_EFLAGS_INPUT(u2Cf); \
        IEM_LIVENESS_ONE_EFLAGS_INPUT(u2Pf); \
        IEM_LIVENESS_ONE_EFLAGS_INPUT(u2Af); \
        IEM_LIVENESS_ONE_EFLAGS_INPUT(u2Zf); \
        IEM_LIVENESS_ONE_EFLAGS_INPUT(u2Sf); \
    } while (0)

#define IEM_MC_FETCH_FSW(a_u16Fsw)                                  IEM_LIVENESS_FSW_INPUT()
#define IEM_MC_FETCH_FCW(a_u16Fcw)                                  IEM_LIVENESS_FCW_INPUT()

#define IEM_MC_STORE_GREG_U16(a_iGReg, a_u16Value)                  IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_STORE_GREG_U32(a_iGReg, a_u32Value)                  IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_I32(a_iGReg, a_i32Value)                  IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_U64(a_iGReg, a_u64Value)                  IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_I64(a_iGReg, a_i64Value)                  IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_U16_CONST(a_iGReg, a_u16Const)            IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_STORE_GREG_U32_CONST(a_iGReg, a_u32Const)            IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_U64_CONST(a_iGReg, a_u32Const)            IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_STORE_GREG_PAIR_U32(a_iGRegLo, a_iGRegHi, a_u64Value) \
    do { IEM_LIVENESS_GPR_CLOBBER(a_iGRegLo); IEM_LIVENESS_GPR_CLOBBER(a_iGRegHi); } while(0)
#define IEM_MC_STORE_GREG_PAIR_U64(a_iGRegLo, a_iGRegHi, a_u128Value) \
    do { IEM_LIVENESS_GPR_CLOBBER(a_iGRegLo); IEM_LIVENESS_GPR_CLOBBER(a_iGRegHi); } while(0)
#define IEM_MC_CLEAR_HIGH_GREG_U64(a_iGReg)                         IEM_LIVENESS_GPR_MODIFY(a_iGReg) /** @todo This isn't always the case... */

#define IEM_MC_STORE_SREG_BASE_U64(a_iSReg, a_u64Value)             IEM_LIVENESS_SEG_BASE_CLOBBER(a_iSReg)
#define IEM_MC_STORE_SREG_BASE_U32(a_iSReg, a_u32Value)             IEM_LIVENESS_SEG_BASE_CLOBBER(a_iSReg)
#define IEM_MC_STORE_FPUREG_R80_SRC_REF(a_iSt, a_pr80Src)           NOP()


#define IEM_MC_REF_GREG_U16(a_pu16Dst, a_iGReg)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_REF_GREG_U16_CONST(a_pu16Dst, a_iGReg)               IEM_LIVENESS_GPR_INPUT( a_iGReg)
#define IEM_MC_REF_GREG_U32(a_pu32Dst, a_iGReg)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_REF_GREG_U32_CONST(a_pu32Dst, a_iGReg)               IEM_LIVENESS_GPR_INPUT( a_iGReg)
#define IEM_MC_REF_GREG_I32(a_pi32Dst, a_iGReg)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_REF_GREG_I32_CONST(a_pi32Dst, a_iGReg)               IEM_LIVENESS_GPR_INPUT( a_iGReg)
#define IEM_MC_REF_GREG_U64(a_pu64Dst, a_iGReg)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_REF_GREG_U64_CONST(a_pu64Dst, a_iGReg)               IEM_LIVENESS_GPR_INPUT( a_iGReg)
#define IEM_MC_REF_GREG_I64(a_pi64Dst, a_iGReg)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_REF_GREG_I64_CONST(a_pi64Dst, a_iGReg)               IEM_LIVENESS_GPR_INPUT( a_iGReg)
#define IEM_MC_REF_EFLAGS(a_pEFlags)                                IEM_LIVENESS_ALL_EFLAGS_MODIFY()
#undef IEM_MC_REF_EFLAGS /* unused */
#define IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, a_fEfl, a_Member) \
        if ((a_fEflInput) & (a_fEfl)) { \
            if ((a_fEflOutput) & (a_fEfl))       IEM_LIVENESS_ONE_EFLAG_MODIFY(a_Member); \
            else                                  IEM_LIVENESS_ONE_EFLAG_INPUT(a_Member); \
        } else if ((a_fEflOutput) & (a_fEfl)) IEM_LIVENESS_ONE_EFLAG_CLOBBER(a_Member)
#define IEM_MC_REF_EFLAGS_EX(a_pEFlags, a_fEflInput, a_fEflOutput) do { \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_CF, fEflCf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_PF, fEflPf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_AF, fEflAf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_ZF, fEflZf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_SF, fEflSf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, X86_EFL_OF, fEflOf); \
        IEMLIVENESS_EFL_HLP(a_fEflInput, a_fEflOutput, ~X86_EFL_STATUS_BITS, fEflOther); \
        Assert(!(  ((a_fEflInput) | (a_fEflOutput)) \
                 & ~(uint32_t)(X86_EFL_STATUS_BITS | X86_EFL_DF | X86_EFL_VM | X86_EFL_VIF | X86_EFL_IOPL))); \
    } while (0)
#define IEM_MC_ASSERT_EFLAGS(a_fEflInput, a_fEflOutput)             NOP()


#define IEM_MC_ADD_GREG_U16(a_iGReg, a_u16Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_ADD_GREG_U32(a_iGReg, a_u32Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_ADD_GREG_U64(a_iGReg, a_u64Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)

#define IEM_MC_SUB_GREG_U16(a_iGReg, a_u8Const)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_SUB_GREG_U32(a_iGReg, a_u8Const)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_SUB_GREG_U64(a_iGReg, a_u8Const)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_SUB_LOCAL_U16(a_u16Value, a_u16Const)                NOP()

#define IEM_MC_ADD_GREG_U16_TO_LOCAL(a_u16Value, a_iGReg)           IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_ADD_GREG_U32_TO_LOCAL(a_u32Value, a_iGReg)           IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_ADD_GREG_U64_TO_LOCAL(a_u64Value, a_iGReg)           IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(a_EffAddr, a_i16)          NOP()
#define IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(a_EffAddr, a_i32)          NOP()
#define IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(a_EffAddr, a_i64)          NOP()

#define IEM_MC_AND_LOCAL_U8(a_u8Local, a_u8Mask)                    NOP()
#define IEM_MC_AND_LOCAL_U16(a_u16Local, a_u16Mask)                 NOP()
#define IEM_MC_AND_LOCAL_U32(a_u32Local, a_u32Mask)                 NOP()
#define IEM_MC_AND_LOCAL_U64(a_u64Local, a_u64Mask)                 NOP()

#define IEM_MC_AND_ARG_U16(a_u16Arg, a_u16Mask)                     NOP()
#define IEM_MC_AND_ARG_U32(a_u32Arg, a_u32Mask)                     NOP()
#define IEM_MC_AND_ARG_U64(a_u64Arg, a_u64Mask)                     NOP()

#define IEM_MC_OR_LOCAL_U8(a_u8Local, a_u8Mask)                     NOP()
#define IEM_MC_OR_LOCAL_U16(a_u16Local, a_u16Mask)                  NOP()
#define IEM_MC_OR_LOCAL_U32(a_u32Local, a_u32Mask)                  NOP()

#define IEM_MC_SAR_LOCAL_S16(a_i16Local, a_cShift)                  NOP()
#define IEM_MC_SAR_LOCAL_S32(a_i32Local, a_cShift)                  NOP()
#define IEM_MC_SAR_LOCAL_S64(a_i64Local, a_cShift)                  NOP()

#define IEM_MC_SHR_LOCAL_U8(a_u8Local, a_cShift)                    NOP()

#define IEM_MC_SHL_LOCAL_S16(a_i16Local, a_cShift)                  NOP()
#define IEM_MC_SHL_LOCAL_S32(a_i32Local, a_cShift)                  NOP()
#define IEM_MC_SHL_LOCAL_S64(a_i64Local, a_cShift)                  NOP()

#define IEM_MC_AND_2LOCS_U32(a_u32Local, a_u32Mask)                 NOP()

#define IEM_MC_OR_2LOCS_U32(a_u32Local, a_u32Mask)                  NOP()

#define IEM_MC_AND_GREG_U16(a_iGReg, a_u16Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_AND_GREG_U32(a_iGReg, a_u32Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_AND_GREG_U64(a_iGReg, a_u64Value)                    IEM_LIVENESS_GPR_MODIFY(a_iGReg)

#define IEM_MC_OR_GREG_U16(a_iGReg, a_u16Value)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_OR_GREG_U32(a_iGReg, a_u32Value)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)
#define IEM_MC_OR_GREG_U64(a_iGReg, a_u64Value)                     IEM_LIVENESS_GPR_MODIFY(a_iGReg)

#define IEM_MC_BSWAP_LOCAL_U16(a_u16Local)                          NOP()
#define IEM_MC_BSWAP_LOCAL_U32(a_u32Local)                          NOP()
#define IEM_MC_BSWAP_LOCAL_U64(a_u64Local)                          NOP()

#define IEM_MC_SET_EFL_BIT(a_fBit) do { \
        if ((a_fBit) == X86_EFL_CF)      IEM_LIVENESS_ONE_EFLAG_CLOBBER(fEflCf); \
        else if ((a_fBit) == X86_EFL_DF) IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther); \
        else { AssertFailed();           IEM_LIVENESS_ALL_EFLAGS_MODIFY(); } \
    } while (0)
#define IEM_MC_CLEAR_EFL_BIT(a_fBit) do { \
        if ((a_fBit) == X86_EFL_CF)      IEM_LIVENESS_ONE_EFLAG_CLOBBER(fEflCf); \
        else if ((a_fBit) == X86_EFL_DF) IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflOther); \
        else { AssertFailed();           IEM_LIVENESS_ALL_EFLAGS_MODIFY(); } \
    } while (0)
#define IEM_MC_FLIP_EFL_BIT(a_fBit) do { \
        if ((a_fBit) == X86_EFL_CF)      IEM_LIVENESS_ONE_EFLAG_MODIFY(fEflCf); \
        else { AssertFailed();           IEM_LIVENESS_ALL_EFLAGS_MODIFY(); } \
    } while (0)

#define IEM_MC_CLEAR_FSW_EX()                                       IEM_LIVENESS_FCW_MODIFY()
#define IEM_MC_FPU_TO_MMX_MODE()                                    IEM_LIVENESS_FCW_MODIFY()
#define IEM_MC_FPU_FROM_MMX_MODE()                                  IEM_LIVENESS_FCW_MODIFY()

#define IEM_MC_FETCH_MREG_U64(a_u64Value, a_iMReg)                  NOP()
#define IEM_MC_FETCH_MREG_U32(a_u32Value, a_iMReg, a_iDWord)        NOP()
#define IEM_MC_FETCH_MREG_U16(a_u16Value, a_iMReg, a_iWord)         NOP()
#define IEM_MC_FETCH_MREG_U8(a_u8Value, a_iMReg, a_iByte)           NOP()
#define IEM_MC_STORE_MREG_U64(a_iMReg, a_u64Value)                  NOP()
#define IEM_MC_STORE_MREG_U32(a_iMReg, a_iDword, a_u32Value)        NOP()
#define IEM_MC_STORE_MREG_U16(a_iMReg, a_iWord, a_u16Value)         NOP()
#define IEM_MC_STORE_MREG_U8(a_iMReg, a_iByte, a_u8Value)           NOP()
#define IEM_MC_STORE_MREG_U32_ZX_U64(a_iMReg, a_u32Value)           NOP()
#define IEM_MC_REF_MREG_U64(a_pu64Dst, a_iMReg)                     NOP()
#define IEM_MC_REF_MREG_U64_CONST(a_pu64Dst, a_iMReg)               NOP()
#define IEM_MC_REF_MREG_U32_CONST(a_pu32Dst, a_iMReg)               NOP()
#define IEM_MC_MODIFIED_MREG(a_iMReg)                               NOP()
#define IEM_MC_MODIFIED_MREG_BY_REF(a_pu64Dst)                      NOP()

#define IEM_MC_CLEAR_XREG_U32_MASK(a_iXReg, a_bMask)                NOP()
#define IEM_MC_FETCH_XREG_U128(a_u128Value, a_iXReg)                NOP()
#define IEM_MC_FETCH_XREG_XMM(a_XmmValue, a_iXReg)                  NOP()
#define IEM_MC_FETCH_XREG_U64(a_u64Value, a_iXReg, a_iQWord)        NOP()
#define IEM_MC_FETCH_XREG_R64(a_r64Value, a_iXReg, a_iQWord)        NOP()
#define IEM_MC_FETCH_XREG_U32(a_u32Value, a_iXReg, a_iDWord)        NOP()
#define IEM_MC_FETCH_XREG_R32(a_r32Value, a_iXReg, a_iDWord)        NOP()
#define IEM_MC_FETCH_XREG_U16(a_u16Value, a_iXReg, a_iWord)         NOP()
#define IEM_MC_FETCH_XREG_U8( a_u8Value,  a_iXReg, a_iByte)         NOP()
#define IEM_MC_FETCH_XREG_PAIR_U128(a_Dst, a_iXReg1, a_iXReg2)      NOP()
#define IEM_MC_FETCH_XREG_PAIR_XMM(a_Dst, a_iXReg1, a_iXReg2)       NOP()
#define IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iXReg2) \
    do { IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)
#define IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iXReg2) \
    do { IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)
#define IEM_MC_STORE_XREG_U128(a_iXReg, a_u128Value)                NOP()
#define IEM_MC_STORE_XREG_XMM(a_iXReg, a_XmmValue)                  NOP()
#define IEM_MC_STORE_XREG_XMM_U32(a_iXReg, a_iDword, a_XmmValue)    NOP()
#define IEM_MC_STORE_XREG_XMM_U64(a_iXReg, a_iQword, a_XmmValue)    NOP()
#define IEM_MC_STORE_XREG_U64(a_iXReg, a_iQword, a_u64Value)        NOP()
#define IEM_MC_STORE_XREG_U32(a_iXReg, a_iDword, a_u32Value)        NOP()
#define IEM_MC_STORE_XREG_U16(a_iXReg, a_iWord, a_u16Value)         NOP()
#define IEM_MC_STORE_XREG_U8(a_iXReg,  a_iByte, a_u8Value)          NOP()
#define IEM_MC_STORE_XREG_U64_ZX_U128(a_iXReg, a_u64Value)          NOP()
#define IEM_MC_STORE_XREG_U32_U128(a_iXReg, a_iDwDst, a_u128Value, a_iDwSrc) NOP()
#define IEM_MC_STORE_XREG_R32(a_iXReg, a_r32Value)                  NOP()
#define IEM_MC_STORE_XREG_R64(a_iXReg, a_r64Value)                  NOP()
#define IEM_MC_STORE_XREG_U32_ZX_U128(a_iXReg, a_u32Value)          NOP()

#define IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX(a_iXRegDst, a_u8Src)      NOP()
#define IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX(a_iXRegDst, a_u16Src)    NOP()
#define IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX(a_iXRegDst, a_u32Src)    NOP()
#define IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX(a_iXRegDst, a_u64Src)    NOP()

#define IEM_MC_REF_XREG_U128(a_pu128Dst, a_iXReg)                   NOP()
#define IEM_MC_REF_XREG_XMM(a_puXmmDst, a_iXReg)                    NOP()
#define IEM_MC_REF_XREG_U128_CONST(a_pu128Dst, a_iXReg)             NOP()
#define IEM_MC_REF_XREG_XMM_CONST(a_pXmmDst, a_iXReg)               NOP()
#define IEM_MC_REF_XREG_U32_CONST(a_pu32Dst, a_iXReg)               NOP()
#define IEM_MC_REF_XREG_U64_CONST(a_pu64Dst, a_iXReg)               NOP()
#define IEM_MC_REF_XREG_R32_CONST(a_pr32Dst, a_iXReg)               NOP()
#define IEM_MC_REF_XREG_R64_CONST(a_pr64Dst, a_iXReg)               NOP()
#define IEM_MC_COPY_XREG_U128(a_iXRegDst, a_iXRegSrc)               NOP()

#define IEM_MC_FETCH_YREG_U32(a_u32Dst, a_iYRegSrc)                 NOP()
#define IEM_MC_FETCH_YREG_U64(a_u64Dst, a_iYRegSrc, a_iQWord)       NOP()
#define IEM_MC_FETCH_YREG_U128(a_u128Dst, a_iYRegSrc, a_iDQWord)    NOP()
#define IEM_MC_FETCH_YREG_U256(a_u256Dst, a_iYRegSrc)               NOP()
#define IEM_MC_FETCH_YREG_YMM(a_uYmmDst, a_iYRegSrc)                NOP()

#define IEM_MC_STORE_YREG_U128(a_iYRegDst, a_iDQword, a_u128Value)  NOP()

#define IEM_MC_INT_CLEAR_ZMM_256_UP(a_iXRegDst)                     NOP()
#define IEM_MC_STORE_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src)        NOP()
#define IEM_MC_STORE_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src)        NOP()
#define IEM_MC_STORE_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src)      NOP()
#define IEM_MC_STORE_YREG_U256_ZX_VLMAX(a_iYRegDst, a_u256Src)      NOP()
#define IEM_MC_STORE_YREG_YMM_ZX_VLMAX(a_iYRegDst, a_uYmmSrc)       NOP()

#define IEM_MC_STORE_YREG_U32_U256(a_iYRegDst, a_iDwDst, a_u256Value, a_iDwSrc)      NOP()
#define IEM_MC_STORE_YREG_U64_U256(a_iYRegDst, a_iQwDst, a_u256Value, a_iQwSrc)      NOP()
#define IEM_MC_STORE_YREG_U64(a_iYRegDst, a_iQword, a_u64Value)                      NOP()

#define IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX(a_iYRegDst, a_u8Src)      NOP()
#define IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX(a_iYRegDst, a_u16Src)    NOP()
#define IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src)    NOP()
#define IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src)    NOP()
#define IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src)  NOP()

#define IEM_MC_REF_YREG_U128(a_pu128Dst, a_iYReg)                   NOP()
#define IEM_MC_REF_YREG_U128_CONST(a_pu128Dst, a_iYReg)             NOP()
#define IEM_MC_REF_YREG_U64_CONST(a_pu64Dst, a_iYReg)               NOP()
#define IEM_MC_CLEAR_YREG_128_UP(a_iYReg)                           NOP()

#define IEM_MC_COPY_YREG_U256_ZX_VLMAX(a_iYRegDst, a_iYRegSrc)      NOP()
#define IEM_MC_COPY_YREG_U128_ZX_VLMAX(a_iYRegDst, a_iYRegSrc)      NOP()
#define IEM_MC_COPY_YREG_U64_ZX_VLMAX(a_iYRegDst, a_iYRegSrc)       NOP()

#define IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX(a_iYRegDst, a_iYRegSrc32, a_iYRegSrcHx)      NOP()
#define IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx)      NOP()
#define IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx)  NOP()
#define IEM_MC_MERGE_YREG_U64HI_U64HI_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx)  NOP()
#define IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX(a_iYRegDst, a_iYRegSrcHx, a_u64Local) NOP()
#define IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX(a_iYRegDst, a_u64Local, a_iYRegSrcHx) NOP()

#define IEM_MC_CLEAR_ZREG_256_UP(a_iYReg)                                               NOP()

#define IEM_MC_FETCH_MEM_U8(a_u8Dst, a_iSeg, a_GCPtrMem)                                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM16_U8(a_u8Dst, a_iSeg, a_GCPtrMem16)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM32_U8(a_u8Dst, a_iSeg, a_GCPtrMem32)                                    IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U8(a_u8Dst, a_GCPtrMem)                                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM16_FLAT_U8(a_u8Dst, a_GCPtrMem16)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM32_FLAT_U8(a_u8Dst, a_GCPtrMem32)                                       IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U16(a_u16Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U16_DISP(a_u16Dst, a_iSeg, a_GCPtrMem, a_offDisp)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_I16(a_i16Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_I16_DISP(a_i16Dst, a_iSeg, a_GCPtrMem, a_offDisp)                      IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U16(a_u16Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U16_DISP(a_u16Dst, a_GCPtrMem, a_offDisp)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_I16(a_i16Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_I16_DISP(a_i16Dst, a_GCPtrMem, a_offDisp)                         IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U32(a_u32Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U32_DISP(a_u32Dst, a_iSeg, a_GCPtrMem, a_offDisp)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_I32(a_i32Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_I32_DISP(a_i32Dst, a_iSeg, a_GCPtrMem, a_offDisp)                      IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U32(a_u32Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U32_DISP(a_u32Dst, a_GCPtrMem, a_offDisp)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_I32(a_i32Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_I32_DISP(a_i32Dst, a_GCPtrMem, a_offDisp)                         IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U64_DISP(a_u64Dst, a_iSeg, a_GCPtrMem, a_offDisp)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U64_ALIGN_U128(a_u64Dst, a_iSeg, a_GCPtrMem)                           IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_I64(a_i64Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U64(a_u64Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U64_DISP(a_u64Dst, a_GCPtrMem, a_offDisp)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U64_ALIGN_U128(a_u64Dst, a_GCPtrMem)                              IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_I64(a_i64Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_R32(a_r32Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_R64(a_r64Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_R80(a_r80Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_D80(a_d80Dst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_R32(a_r32Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_R64(a_r64Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_R80(a_r80Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_D80(a_d80Dst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U128(a_u128Dst, a_iSeg, a_GCPtrMem)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U128_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem)                              IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U128_ALIGN_SSE(a_u128Dst, a_iSeg, a_GCPtrMem)                          IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_XMM(a_XmmDst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_XMM_NO_AC(a_XmmDst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(a_XmmDst, a_iSeg, a_GCPtrMem)                            IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U128(a_u128Dst, a_GCPtrMem)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U128_NO_AC(a_u128Dst, a_GCPtrMem)                                 IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE(a_u128Dst, a_GCPtrMem)                             IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_FLAT_XMM(a_XmmDst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC(a_XmmDst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE(a_XmmDst, a_GCPtrMem)                               IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2)              IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_GCPtrMem2)                  IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2)      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_GCPtrMem2)          IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_iSeg2, a_GCPtrMem2) IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_FLAT_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_GCPtrMem2)     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_iSeg2, a_GCPtrMem2) IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_FLAT_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_GCPtrMem2)     IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) \
    do { IEM_LIVENESS_MEM(a_iSeg2); IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)
#define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) \
    do { IEM_LIVENESS_MEM(a_iSeg2); IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)

#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) \
    do { IEM_LIVENESS_MEM_FLAT(); IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)
#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) \
    do { IEM_LIVENESS_MEM_FLAT(); IEM_LIVENESS_GPR_INPUT(X86_GREG_xAX); IEM_LIVENESS_GPR_INPUT(X86_GREG_xDX); } while (0)


#define IEM_MC_FETCH_MEM_U256(a_u256Dst, a_iSeg, a_GCPtrMem)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U256_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem)                              IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U256_ALIGN_AVX(a_u256Dst, a_iSeg, a_GCPtrMem)                          IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_YMM(a_YmmDst, a_iSeg, a_GCPtrMem)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_YMM_NO_AC(a_YmmDst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_YMM_ALIGN_AVX(a_YmmDst, a_iSeg, a_GCPtrMem)                            IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U256(a_u256Dst, a_GCPtrMem)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U256_NO_AC(a_u256Dst, a_GCPtrMem)                                 IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX(a_u256Dst, a_GCPtrMem)                             IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_FLAT_YMM(a_YmmDst, a_GCPtrMem)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC(a_YmmDst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX(a_YmmDst, a_GCPtrMem)                               IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U8_ZX_U16(a_u16Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U8_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U8_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U16_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U16_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U32_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16(a_u16Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32(a_u32Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64(a_u64Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32(a_u32Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64(a_u64Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64(a_u64Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_FETCH_MEM_U8_SX_U16(a_u16Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U8_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U8_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U16_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U16_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_FETCH_MEM_U32_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem)                               IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U16(a_u16Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U32(a_u32Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U64(a_u64Dst, a_GCPtrMem)                                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U32(a_u32Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U64(a_u64Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_FETCH_MEM_FLAT_U32_SX_U64(a_u64Dst, a_GCPtrMem)                                  IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_STORE_MEM_U8(a_iSeg, a_GCPtrMem, a_u8Value)                                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U16(a_iSeg, a_GCPtrMem, a_u16Value)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U32(a_iSeg, a_GCPtrMem, a_u32Value)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U64(a_iSeg, a_GCPtrMem, a_u64Value)                                    IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_STORE_MEM_FLAT_U8(a_GCPtrMem, a_u8Value)                                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U16(a_GCPtrMem, a_u16Value)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U32(a_GCPtrMem, a_u32Value)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U64(a_GCPtrMem, a_u64Value)                                       IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_STORE_MEM_U8_CONST(a_iSeg, a_GCPtrMem, a_u8C)                                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U16_CONST(a_iSeg, a_GCPtrMem, a_u16C)                                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U32_CONST(a_iSeg, a_GCPtrMem, a_u32C)                                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U64_CONST(a_iSeg, a_GCPtrMem, a_u64C)                                  IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_STORE_MEM_FLAT_U8_CONST(a_GCPtrMem, a_u8C)                                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U16_CONST(a_GCPtrMem, a_u16C)                                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U32_CONST(a_GCPtrMem, a_u32C)                                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U64_CONST(a_GCPtrMem, a_u64C)                                     IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_STORE_MEM_I8_CONST_BY_REF( a_pi8Dst,  a_i8C)                                     NOP()
#define IEM_MC_STORE_MEM_I16_CONST_BY_REF(a_pi16Dst, a_i16C)                                    NOP()
#define IEM_MC_STORE_MEM_I32_CONST_BY_REF(a_pi32Dst, a_i32C)                                    NOP()
#define IEM_MC_STORE_MEM_I64_CONST_BY_REF(a_pi64Dst, a_i64C)                                    NOP()
#define IEM_MC_STORE_MEM_NEG_QNAN_R32_BY_REF(a_pr32Dst)                                         NOP()
#define IEM_MC_STORE_MEM_NEG_QNAN_R64_BY_REF(a_pr64Dst)                                         NOP()
#define IEM_MC_STORE_MEM_NEG_QNAN_R80_BY_REF(a_pr80Dst)                                         NOP()
#define IEM_MC_STORE_MEM_INDEF_D80_BY_REF(a_pd80Dst)                                            NOP()

#define IEM_MC_STORE_MEM_U128(a_iSeg, a_GCPtrMem, a_u128Value)                                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U128_ALIGN_SSE(a_iSeg, a_GCPtrMem, a_u128Value)                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U128_NO_AC(a_iSeg, a_GCPtrMem, a_u128Value)                            IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_STORE_MEM_FLAT_U128(a_GCPtrMem, a_u128Value)                                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE(a_GCPtrMem, a_u128Value)                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U128_NO_AC(a_GCPtrMem, a_u128Value)                               IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_STORE_MEM_U256(a_iSeg, a_GCPtrMem, a_u256Value)                                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U256_ALIGN_AVX(a_iSeg, a_GCPtrMem, a_u256Value)                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_STORE_MEM_U256_NO_AC(a_iSeg, a_GCPtrMem, a_u256Value)                            IEM_LIVENESS_MEM(a_iSeg)

#define IEM_MC_STORE_MEM_FLAT_U256(a_GCPtrMem, a_u256Value)                                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX(a_GCPtrMem, a_u256Value)                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_STORE_MEM_FLAT_U256_NO_AC(a_GCPtrMem, a_u256Value)                               IEM_LIVENESS_MEM_FLAT()

#define IEM_MC_PUSH_U16(a_u16Value)                  IEM_LIVENESS_STACK()
#define IEM_MC_PUSH_U32(a_u32Value)                  IEM_LIVENESS_STACK()
#define IEM_MC_PUSH_U32_SREG(a_uSegVal)              IEM_LIVENESS_STACK()
#define IEM_MC_PUSH_U64(a_u64Value)                  IEM_LIVENESS_STACK()

#define IEM_MC_POP_GREG_U16(a_iGReg)            do { IEM_LIVENESS_STACK();  IEM_LIVENESS_GPR_MODIFY(a_iGReg); } while (0)
#define IEM_MC_POP_GREG_U32(a_iGReg)            do { IEM_LIVENESS_STACK();  IEM_LIVENESS_GPR_CLOBBER(a_iGReg); } while (0)
#define IEM_MC_POP_GREG_U64(a_iGReg)            do { IEM_LIVENESS_STACK();  IEM_LIVENESS_GPR_CLOBBER(a_iGReg); } while (0)

/* 32-bit flat stack push and pop: */
#define IEM_MC_FLAT32_PUSH_U16(a_u16Value)           IEM_LIVENESS_STACK_FLAT()
#define IEM_MC_FLAT32_PUSH_U32(a_u32Value)           IEM_LIVENESS_STACK_FLAT()
#define IEM_MC_FLAT32_PUSH_U32_SREG(a_uSegVal)       IEM_LIVENESS_STACK_FLAT()

#define IEM_MC_FLAT32_POP_GREG_U16(a_iGReg)     do { IEM_LIVENESS_STACK_FLAT(); IEM_LIVENESS_GPR_MODIFY(a_iGReg); } while (0)
#define IEM_MC_FLAT32_POP_GREG_U32(a_iGReg)     do { IEM_LIVENESS_STACK_FLAT(); IEM_LIVENESS_GPR_CLOBBER(a_iGReg); } while (0)

/* 64-bit flat stack push and pop: */
#define IEM_MC_FLAT64_PUSH_U16(a_u16Value)           IEM_LIVENESS_STACK_FLAT()
#define IEM_MC_FLAT64_PUSH_U64(a_u64Value)           IEM_LIVENESS_STACK_FLAT()

#define IEM_MC_FLAT64_POP_GREG_U16(a_iGReg)     do { IEM_LIVENESS_STACK_FLAT(); IEM_LIVENESS_GPR_MODIFY(a_iGReg); } while (0)
#define IEM_MC_FLAT64_POP_GREG_U64(a_iGReg)     do { IEM_LIVENESS_STACK_FLAT(); IEM_LIVENESS_GPR_CLOBBER(a_iGReg); } while (0)


#define IEM_MC_MEM_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                        IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem)                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem)                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem)                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem)                           IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem)                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem)                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                  IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem)                     IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                    IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem)                   IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem)                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem)                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_FLAT_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem)                       IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()
#define IEM_MC_MEM_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)                      IEM_LIVENESS_MEM(a_iSeg)
#define IEM_MC_MEM_FLAT_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_GCPtrMem)                         IEM_LIVENESS_MEM_FLAT()


#define IEM_MC_MEM_COMMIT_AND_UNMAP_RW(a_bMapInfo)                                              NOP()
#define IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(a_bMapInfo)                                          NOP()
#define IEM_MC_MEM_COMMIT_AND_UNMAP_WO(a_bMapInfo)                                              NOP()
#define IEM_MC_MEM_COMMIT_AND_UNMAP_RO(a_bMapInfo)                                              NOP()
#define IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO(a_bMapInfo, a_u16FSW)                      NOP()
#define IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO(a_bMapInfo)                                            NOP()

#define IEM_MC_NATIVE_IF(a_fSupportedHosts)                                                     {
#define IEM_MC_NATIVE_ELSE()                                                                    } {
#define IEM_MC_NATIVE_ENDIF()                                                                   } ((void)0)

#define IEM_MC_NATIVE_EMIT_0(a_fnEmitter)
#define IEM_MC_NATIVE_EMIT_1(a_fnEmitter, a0)                                                   NOP()
#define IEM_MC_NATIVE_EMIT_2(a_fnEmitter, a0, a1)                                               NOP()
#define IEM_MC_NATIVE_EMIT_2_EX(a_fnEmitter, a0, a1)                                            NOP()
#define IEM_MC_NATIVE_EMIT_3(a_fnEmitter, a0, a1, a2)                                           NOP()
#define IEM_MC_NATIVE_EMIT_4(a_fnEmitter, a0, a1, a2, a3)                                       NOP()
#define IEM_MC_NATIVE_EMIT_5(a_fnEmitter, a0, a1, a2, a3, a4)                                   NOP()
#define IEM_MC_NATIVE_EMIT_6(a_fnEmitter, a0, a1, a2, a3, a4, a5)                               NOP()
#define IEM_MC_NATIVE_EMIT_7(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6)                           NOP()
#define IEM_MC_NATIVE_EMIT_8(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6, a7)                       NOP()

#define IEM_MC_NATIVE_SET_AMD64_HOST_REG_FOR_LOCAL(a_VarNm, a_idxHostReg)                       NOP()

#define IEM_MC_CALL_VOID_AIMPL_0(a_pfn)                                                         NOP()
#define IEM_MC_CALL_VOID_AIMPL_1(a_pfn, a0)                                                     NOP()
#define IEM_MC_CALL_VOID_AIMPL_2(a_pfn, a0, a1)                                                 NOP()
#define IEM_MC_CALL_VOID_AIMPL_3(a_pfn, a0, a1, a2)                                             NOP()
#define IEM_MC_CALL_VOID_AIMPL_4(a_pfn, a0, a1, a2, a3)                                         NOP()
#define IEM_MC_CALL_AIMPL_3(a_rcType, a_rc, a_pfn, a0, a1, a2)                                  NOP()
#define IEM_MC_CALL_AIMPL_4(a_rcType, a_rc, a_pfn, a0, a1, a2, a3)                              NOP()

#define IEM_MC_CALL_FPU_AIMPL_1(a_pfnAImpl, a0)                                                 NOP()
#define IEM_MC_CALL_FPU_AIMPL_2(a_pfnAImpl, a0, a1)                                             NOP()
#define IEM_MC_CALL_FPU_AIMPL_3(a_pfnAImpl, a0, a1, a2)                                         NOP()

#define IEM_MC_SET_FPU_RESULT(a_FpuData, a_FSW, a_pr80Value)                                    NOP()

#define IEM_MC_PUSH_FPU_RESULT(a_FpuData, a_uFpuOpcode)                                         NOP()
#define IEM_MC_PUSH_FPU_RESULT_MEM_OP(a_FpuData, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)           NOP()
#define IEM_MC_PUSH_FPU_RESULT_TWO(a_FpuDataTwo, a_uFpuOpcode)                                  NOP()

#define IEM_MC_STORE_FPU_RESULT(a_FpuData, a_iStReg, a_uFpuOpcode)                              NOP()
#define IEM_MC_STORE_FPU_RESULT_THEN_POP(a_FpuData, a_iStReg, a_uFpuOpcode)                     NOP()
#define IEM_MC_STORE_FPU_RESULT_MEM_OP(a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) NOP()
#define IEM_MC_STORE_FPU_RESULT_WITH_MEM_OP_THEN_POP(a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) NOP()

#define IEM_MC_UPDATE_FPU_OPCODE_IP(a_uFpuOpcode)                                               NOP()
#define IEM_MC_FPU_STACK_FREE(a_iStReg)                                                         NOP()
#define IEM_MC_FPU_STACK_INC_TOP()                                                              NOP()
#define IEM_MC_FPU_STACK_DEC_TOP()                                                              NOP()

#define IEM_MC_UPDATE_FSW(a_u16FSW, a_uFpuOpcode)                                               NOP()
#define IEM_MC_UPDATE_FSW_CONST(a_u16FSW, a_uFpuOpcode)                                         NOP()
#define IEM_MC_UPDATE_FSW_WITH_MEM_OP(a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)            NOP()
#define IEM_MC_UPDATE_FSW_THEN_POP(a_u16FSW, a_uFpuOpcode)                                      NOP()
#define IEM_MC_UPDATE_FSW_WITH_MEM_OP_THEN_POP(a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)   NOP()
#define IEM_MC_UPDATE_FSW_THEN_POP_POP(a_u16FSW, a_uFpuOpcode)                                  NOP()

#define IEM_MC_FPU_STACK_UNDERFLOW(a_iStDst, a_uFpuOpcode)                                      NOP()
#define IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP(a_iStDst, a_uFpuOpcode)                             NOP()
#define IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP(a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)        NOP()
#define IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP_THEN_POP(a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) NOP()
#define IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP_POP(a_uFpuOpcode)                                   NOP()
#define IEM_MC_FPU_STACK_PUSH_UNDERFLOW(a_uFpuOpcode)                                           NOP()
#define IEM_MC_FPU_STACK_PUSH_UNDERFLOW_TWO(a_uFpuOpcode)                                       NOP()

#define IEM_MC_FPU_STACK_PUSH_OVERFLOW(a_uFpuOpcode)                                            NOP()
#define IEM_MC_FPU_STACK_PUSH_OVERFLOW_MEM_OP(a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)              NOP()

#define IEM_MC_PREPARE_FPU_USAGE()                                                              IEM_LIVENESS_MXCSR_INPUT() /* fxrstor */
#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ()                                                   IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE()                                                 IEM_LIVENESS_MXCSR_INPUT()

#define IEM_MC_PREPARE_SSE_USAGE()                                                              IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ()                                                   IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE()                                                 IEM_LIVENESS_MXCSR_INPUT()

#define IEM_MC_PREPARE_AVX_USAGE()                                                              IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ()                                                   IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE()                                                 IEM_LIVENESS_MXCSR_INPUT()

#define IEM_MC_CALL_MMX_AIMPL_2(a_pfnAImpl, a0, a1)                                             NOP()
#define IEM_MC_CALL_MMX_AIMPL_3(a_pfnAImpl, a0, a1, a2)                                         NOP()
#define IEM_MC_CALL_SSE_AIMPL_2(a_pfnAImpl, a0, a1)                                             IEM_LIVENESS_MXCSR_MODIFY(); IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR4_INPUT()
#define IEM_MC_CALL_SSE_AIMPL_3(a_pfnAImpl, a0, a1, a2)                                         IEM_LIVENESS_MXCSR_MODIFY(); IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR4_INPUT()
#define IEM_MC_CALL_AVX_AIMPL_2(a_pfnAImpl, a0, a1)                                             IEM_LIVENESS_MXCSR_MODIFY(); IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR4_INPUT()
#define IEM_MC_CALL_AVX_AIMPL_3(a_pfnAImpl, a0, a1, a2)                                         IEM_LIVENESS_MXCSR_MODIFY(); IEM_LIVENESS_MARK_POTENTIAL_CALL(); IEM_LIVENESS_CR4_INPUT()

#define IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit) \
    do { if (     (a_fBit) == X86_EFL_CF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflCf); \
         else if ((a_fBit) == X86_EFL_PF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflPf); \
         else if ((a_fBit) == X86_EFL_AF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflAf); \
         else if ((a_fBit) == X86_EFL_ZF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflZf); \
         else if ((a_fBit) == X86_EFL_SF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflSf); \
         else if ((a_fBit) == X86_EFL_OF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflOf); \
         else if ((a_fBit) == X86_EFL_DF) IEM_LIVENESS_ONE_EFLAG_INPUT(fEflOther); /* loadsb and friends */ \
         else { AssertMsgFailed(("#s (%#x)\n", #a_fBit, (a_fBit)));  IEM_LIVENESS_ALL_EFLAGS_INPUT(); } \
    } while (0)

#define IEM_MC_IF_EFL_BIT_SET(a_fBit)                   IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit); {
#define IEM_MC_IF_EFL_BIT_NOT_SET(a_fBit)               IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit); {
#define IEM_MC_IF_EFL_ANY_BITS_SET(a_fBits) \
    do { if ((a_fBits) == (X86_EFL_CF | X86_EFL_ZF)) \
         { IEM_LIVENESS_ONE_EFLAG_INPUT(fEflCf); IEM_LIVENESS_ONE_EFLAG_INPUT(fEflZf); } \
         else { AssertMsgFailed(("#s (%#x)\n", #a_fBits, (a_fBits)));  IEM_LIVENESS_ALL_EFLAGS_INPUT(); } \
    } while (0);                                        {
#define IEM_MC_IF_EFL_NO_BITS_SET(a_fBits) \
    do { if ((a_fBits) == (X86_EFL_CF | X86_EFL_ZF)) \
         { IEM_LIVENESS_ONE_EFLAG_INPUT(fEflCf); IEM_LIVENESS_ONE_EFLAG_INPUT(fEflZf); } \
         else { AssertMsgFailed(("#s (%#x)\n", #a_fBits, (a_fBits)));  IEM_LIVENESS_ALL_EFLAGS_INPUT(); } \
    } while (0);                                        {
#define IEM_MC_IF_EFL_BITS_NE(a_fBit1, a_fBit2) \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit1); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit2);       {
#define IEM_MC_IF_EFL_BITS_EQ(a_fBit1, a_fBit2) \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit1); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit2);       {
#define IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(a_fBit, a_fBit1, a_fBit2) \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit1); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit2);       {
#define IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ(a_fBit, a_fBit1, a_fBit2) \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit1); \
    IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit2);       {
#define IEM_MC_IF_CX_IS_NZ()                            IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_ECX_IS_NZ()                           IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_RCX_IS_NZ()                           IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_CX_IS_NOT_ONE()                       IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_ECX_IS_NOT_ONE()                      IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_RCX_IS_NOT_ONE()                      IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); {
#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        IEM_LIVENESS_GPR_INPUT(X86_GREG_xCX); \
        IEM_LIVENESS_ONE_STATUS_EFLAG_INPUT(a_fBit);    {
#define IEM_MC_IF_LOCAL_IS_Z(a_Local)                   {
#define IEM_MC_IF_GREG_BIT_SET(a_iGReg, a_iBitNo)       IEM_LIVENESS_GPR_INPUT(a_iGReg); {

#define IEM_MC_REF_FPUREG(a_pr80Dst, a_iSt)             NOP()
#define IEM_MC_IF_FPUREG_IS_EMPTY(a_iSt)                {
#define IEM_MC_IF_FPUREG_NOT_EMPTY(a_iSt)               {
#define IEM_MC_IF_FPUREG_NOT_EMPTY_REF_R80(a_pr80Dst, a_iSt) {
#define IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80(a_pr80Dst0, a_iSt0, a_pr80Dst1, a_iSt1) {
#define IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80_FIRST(a_pr80Dst0, a_iSt0, a_iSt1) {
#define IEM_MC_IF_FCW_IM()                              {

#define IEM_MC_ELSE()                                   } /*else*/ {
#define IEM_MC_ENDIF()                                  } do {} while (0)

#define IEM_MC_HINT_FLUSH_GUEST_SHADOW(g_fGstShwFlush)  NOP()

#define IEM_MC_LIVENESS_GREG_INPUT(a_iGReg)             IEM_LIVENESS_GPR_INPUT(a_iGReg)
#define IEM_MC_LIVENESS_GREG_CLOBBER(a_iGReg)           IEM_LIVENESS_GPR_CLOBBER(a_iGReg)
#define IEM_MC_LIVENESS_GREG_MODIFY(a_iGReg)            IEM_LIVENESS_GPR_MODIFY(a_iGReg)

#define IEM_MC_LIVENESS_MREG_INPUT(a_iMReg)             NOP()
#define IEM_MC_LIVENESS_MREG_CLOBBER(a_iMReg)           NOP()
#define IEM_MC_LIVENESS_MREG_MODIFY(a_iMReg)            NOP()

#define IEM_MC_LIVENESS_XREG_INPUT(a_iXReg)             NOP()
#define IEM_MC_LIVENESS_XREG_CLOBBER(a_iXReg)           NOP()
#define IEM_MC_LIVENESS_XREG_MODIFY(a_iXReg)            NOP()

#define IEM_MC_LIVENESS_MXCSR_INPUT()                   IEM_LIVENESS_MXCSR_INPUT()
#define IEM_MC_LIVENESS_MXCSR_CLOBBER()                 IEM_LIVENESS_MXCSR_CLOBBER()
#define IEM_MC_LIVENESS_MXCSR_MODIFY()                  IEM_LIVENESS_MXCSR_MODIFY()

