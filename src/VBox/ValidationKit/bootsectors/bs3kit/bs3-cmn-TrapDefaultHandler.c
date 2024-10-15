/* $Id: bs3-cmn-TrapDefaultHandler.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TrapDefaultHandler
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "bs3kit-template-header.h"
#if TMPL_BITS != 64
# include <VBox/VMMDevTesting.h>
# include <iprt/asm-amd64-x86.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#define g_pBs3TrapSetJmpFrame   BS3_DATA_NM(g_pBs3TrapSetJmpFrame)
extern uint32_t                 g_pBs3TrapSetJmpFrame;

#define g_Bs3TrapSetJmpCtx      BS3_DATA_NM(g_Bs3TrapSetJmpCtx)
extern BS3REGCTX                g_Bs3TrapSetJmpCtx;


#if TMPL_BITS != 64
/**
 * V86 syscall handler.
 */
static void bs3TrapDefaultHandlerV8086Syscall(PBS3TRAPFRAME pTrapFrame)
{
    /* Minimal syscall. */
    uint16_t uSyscallNo = pTrapFrame->Ctx.rax.u16;
    if (uSyscallNo == BS3_SYSCALL_PRINT_CHR)
        Bs3PrintChr(pTrapFrame->Ctx.rcx.u8);
    else if (uSyscallNo == BS3_SYSCALL_PRINT_STR)
        Bs3PrintStrN(Bs3XptrFlatToCurrent(((uint32_t)pTrapFrame->Ctx.rcx.u16 << 4) + pTrapFrame->Ctx.rsi.u16),
                     pTrapFrame->Ctx.rdx.u16);
    else if (uSyscallNo == BS3_SYSCALL_RESTORE_CTX)
        Bs3RegCtxRestore(Bs3XptrFlatToCurrent(((uint32_t)pTrapFrame->Ctx.rcx.u16 << 4) + pTrapFrame->Ctx.rsi.u16),
                         pTrapFrame->Ctx.rdx.u16);
    else if (   uSyscallNo == BS3_SYSCALL_TO_RING0
             || uSyscallNo == BS3_SYSCALL_TO_RING1
             || uSyscallNo == BS3_SYSCALL_TO_RING2
             || uSyscallNo == BS3_SYSCALL_TO_RING3)
    {
        Bs3RegCtxConvertToRingX(&pTrapFrame->Ctx, pTrapFrame->Ctx.rax.u16 - BS3_SYSCALL_TO_RING0);
    }
    else if (uSyscallNo == BS3_SYSCALL_SET_DRX)
    {
        uint32_t uValue = pTrapFrame->Ctx.rsi.u32;
        switch (pTrapFrame->Ctx.rdx.u8)
        {
            case 0: ASMSetDR0(uValue); break;
            case 1: ASMSetDR1(uValue); break;
            case 2: ASMSetDR2(uValue); break;
            case 3: ASMSetDR3(uValue); break;
            case 6: ASMSetDR6(uValue); break;
            case 7: ASMSetDR7(uValue); break;
            default: Bs3Panic();
        }
    }
    else if (uSyscallNo == BS3_SYSCALL_GET_DRX)
    {
        uint32_t uValue;
        switch (pTrapFrame->Ctx.rdx.u8)
        {
            case 0: uValue = ASMGetDR0(); break;
            case 1: uValue = ASMGetDR1(); break;
            case 2: uValue = ASMGetDR2(); break;
            case 3: uValue = ASMGetDR3(); break;
            case 6: uValue = ASMGetDR6(); break;
            case 7: uValue = ASMGetDR7(); break;
            default: uValue = 0; Bs3Panic();
        }
        pTrapFrame->Ctx.rax.u32 = uValue;
        pTrapFrame->Ctx.rdx.u32 = uValue >> 16;
    }
    else if (uSyscallNo == BS3_SYSCALL_SET_CRX)
    {
        uint32_t uValue = pTrapFrame->Ctx.rsi.u32;
        switch (pTrapFrame->Ctx.rdx.u8)
        {
            case 0: ASMSetCR0(uValue); pTrapFrame->Ctx.cr0.u32 = uValue; break;
            case 2: ASMSetCR2(uValue); pTrapFrame->Ctx.cr2.u32 = uValue; break;
            case 3: ASMSetCR3(uValue); pTrapFrame->Ctx.cr3.u32 = uValue; break;
            case 4: ASMSetCR4(uValue); pTrapFrame->Ctx.cr4.u32 = uValue; break;
            default: Bs3Panic();
        }
    }
    else if (uSyscallNo == BS3_SYSCALL_GET_CRX)
    {
        uint32_t uValue;
        switch (pTrapFrame->Ctx.rdx.u8)
        {
            case 0: uValue = ASMGetCR0(); break;
            case 2: uValue = ASMGetCR2(); break;
            case 3: uValue = ASMGetCR3(); break;
            case 4: uValue = ASMGetCR4(); break;
            default: uValue = 0; Bs3Panic();
        }
        pTrapFrame->Ctx.rax.u32 = uValue;
        pTrapFrame->Ctx.rdx.u32 = uValue >> 16;
    }
    else if (uSyscallNo == BS3_SYSCALL_SET_TR)
    {
        Bs3RegSetTr(pTrapFrame->Ctx.rdx.u16);
        pTrapFrame->Ctx.tr = pTrapFrame->Ctx.rdx.u16;
    }
    else if (uSyscallNo == BS3_SYSCALL_GET_TR)
        pTrapFrame->Ctx.rax.u16 = ASMGetTR();
    else if (uSyscallNo == BS3_SYSCALL_SET_LDTR)
    {
        Bs3RegSetLdtr(pTrapFrame->Ctx.rdx.u16);
        pTrapFrame->Ctx.ldtr = pTrapFrame->Ctx.rdx.u16;
    }
    else if (uSyscallNo == BS3_SYSCALL_GET_LDTR)
        pTrapFrame->Ctx.rax.u16 = ASMGetLDTR();
    else if (uSyscallNo == BS3_SYSCALL_SET_XCR0)
        ASMSetXcr0(RT_MAKE_U64(pTrapFrame->Ctx.rsi.u32, pTrapFrame->Ctx.rdx.u32));
    else if (uSyscallNo == BS3_SYSCALL_GET_XCR0)
    {
        uint64_t const uValue = ASMGetXcr0();
        pTrapFrame->Ctx.rax.u32 = (uint32_t)uValue;
        pTrapFrame->Ctx.rdx.u32 = (uint32_t)(uValue >> 32);
    }
    else
        Bs3Panic();
}
#endif

#undef Bs3TrapDefaultHandler
BS3_CMN_DEF(void, Bs3TrapDefaultHandler,(PBS3TRAPFRAME pTrapFrame))
{
#if TMPL_BITS != 64
    /*
     * v8086 VMM tasks.
     */
    //Bs3TestHostPrintf("Bs3____DefaultHandler: %02xh %04RX16:%08RX32 %08RX32 %04RX16:%08RX32 %d %d\n", pTrapFrame->bXcpt,
    //                  pTrapFrame->Ctx.cs, pTrapFrame->Ctx.rip.u32, pTrapFrame->Ctx.rflags.u32, pTrapFrame->Ctx.ss,
    //                  pTrapFrame->Ctx.rsp.u32, g_fBs3TrapNoV86Assist, 42);
    if ((pTrapFrame->Ctx.rflags.u32 & X86_EFL_VM))
    {
        bool                    fHandled    = true;
        uint8_t                 cBitsOpcode = 16;
        uint8_t                 bOpCode;
        uint8_t const BS3_FAR  *pbCodeStart;
        uint8_t const BS3_FAR  *pbCode;
        uint16_t BS3_FAR       *pusStack;

        pusStack    = (uint16_t      BS3_FAR *)BS3_MAKE_PROT_R0PTR_FROM_REAL(pTrapFrame->Ctx.ss, pTrapFrame->Ctx.rsp.u16);
        pbCode      = (uint8_t const BS3_FAR *)BS3_MAKE_PROT_R0PTR_FROM_REAL(pTrapFrame->Ctx.cs, pTrapFrame->Ctx.rip.u16);
        pbCodeStart = pbCode;

        /*
         * Deal with GPs in V8086 mode.
         */
        if (   pTrapFrame->bXcpt == X86_XCPT_GP
            && !g_fBs3TrapNoV86Assist)
        {
            bOpCode = *pbCode++;
            if (bOpCode == 0x66)
            {
                cBitsOpcode = 32;
                bOpCode = *pbCode++;
            }

            /* INT xx: Real mode behaviour, but intercepting and implementing most of our syscall interface. */
            if (bOpCode == 0xcd)
            {
                uint8_t bVector = *pbCode++;
                if (bVector == BS3_TRAP_SYSCALL)
                    bs3TrapDefaultHandlerV8086Syscall(pTrapFrame);
                else
                {
                    /* Real mode behaviour. */
                    uint16_t BS3_FAR *pusIvte = (uint16_t BS3_FAR *)BS3_MAKE_PROT_R0PTR_FROM_REAL(0, 0);
                    pusIvte += (uint16_t)bVector *2;

                    pusStack[0] = pTrapFrame->Ctx.rflags.u16;
                    pusStack[1] = pTrapFrame->Ctx.cs;
                    pusStack[2] = pTrapFrame->Ctx.rip.u16 + (uint16_t)(pbCode - pbCodeStart);

                    pTrapFrame->Ctx.rip.u16 = pusIvte[0];
                    pTrapFrame->Ctx.cs      = pusIvte[1];
                    pTrapFrame->Ctx.rflags.u16 &= ~X86_EFL_IF; /** @todo this isn't all, but it'll do for now, I hope. */
                    Bs3RegCtxRestore(&pTrapFrame->Ctx, 0/*fFlags*/); /* does not return. */
                }
            }
            /* PUSHF: Real mode behaviour. */
            else if (bOpCode == 0x9c)
            {
                if (cBitsOpcode == 32)
                    *pusStack++ = pTrapFrame->Ctx.rflags.au16[1] & ~(X86_EFL_VM | X86_EFL_RF);
                *pusStack++ = pTrapFrame->Ctx.rflags.u16;
                pTrapFrame->Ctx.rsp.u16 += cBitsOpcode / 8;
            }
            /* POPF:  Real mode behaviour. */
            else if (bOpCode == 0x9d)
            {
                if (cBitsOpcode == 32)
                {
                    pTrapFrame->Ctx.rflags.u32 &= ~X86_EFL_POPF_BITS;
                    pTrapFrame->Ctx.rflags.u32 |= X86_EFL_POPF_BITS & *(uint32_t const *)pusStack;
                }
                else
                {
                    pTrapFrame->Ctx.rflags.u32 &= ~(X86_EFL_POPF_BITS | UINT32_C(0xffff0000)) & ~X86_EFL_RF;
                    pTrapFrame->Ctx.rflags.u16 |= (uint16_t)X86_EFL_POPF_BITS & *pusStack;
                }
                pTrapFrame->Ctx.rsp.u16 -= cBitsOpcode / 8;
            }
            /* CLI: Real mode behaviour. */
            else if (bOpCode == 0xfa)
                pTrapFrame->Ctx.rflags.u16 &= ~X86_EFL_IF;
            /* STI: Real mode behaviour. */
            else if (bOpCode == 0xfb)
                pTrapFrame->Ctx.rflags.u16 |= X86_EFL_IF;
            /* OUT: byte I/O to VMMDev. */
            else if (   bOpCode == 0xee
                     && ((unsigned)(pTrapFrame->Ctx.rdx.u16 - VMMDEV_TESTING_IOPORT_BASE) < (unsigned)VMMDEV_TESTING_IOPORT_COUNT))
                ASMOutU8(pTrapFrame->Ctx.rdx.u16, pTrapFrame->Ctx.rax.u8);
            /* OUT: [d]word I/O to VMMDev. */
            else if (   bOpCode == 0xef
                     && ((unsigned)(pTrapFrame->Ctx.rdx.u16 - VMMDEV_TESTING_IOPORT_BASE) < (unsigned)VMMDEV_TESTING_IOPORT_COUNT))
            {
                if (cBitsOpcode != 32)
                    ASMOutU16(pTrapFrame->Ctx.rdx.u16, pTrapFrame->Ctx.rax.u16);
                else
                    ASMOutU32(pTrapFrame->Ctx.rdx.u16, pTrapFrame->Ctx.rax.u32);
            }
            /* IN: byte I/O to VMMDev. */
            else if (   bOpCode == 0xec
                     && ((unsigned)(pTrapFrame->Ctx.rdx.u16 - VMMDEV_TESTING_IOPORT_BASE) < (unsigned)VMMDEV_TESTING_IOPORT_COUNT))
                pTrapFrame->Ctx.rax.u8 = ASMInU8(pTrapFrame->Ctx.rdx.u16);
            /* IN: [d]word I/O to VMMDev. */
            else if (   bOpCode == 0xed
                     && ((unsigned)(pTrapFrame->Ctx.rdx.u16 - VMMDEV_TESTING_IOPORT_BASE) < (unsigned)VMMDEV_TESTING_IOPORT_COUNT))
            {
                if (cBitsOpcode != 32)
                    pTrapFrame->Ctx.rax.u16 = ASMInU16(pTrapFrame->Ctx.rdx.u16);
                else
                    pTrapFrame->Ctx.rax.u32 = ASMInU32(pTrapFrame->Ctx.rdx.u32);
            }
            /* Unexpected. */
            else
                fHandled = false;
        }
        /*
         * Deal with lock prefixed int xxh syscall in v8086 mode.
         */
        else if (   pTrapFrame->bXcpt == X86_XCPT_UD
                 && pTrapFrame->Ctx.cs == BS3_SEL_TEXT16
                 && pTrapFrame->Ctx.rax.u16 <= BS3_SYSCALL_LAST
                 && pbCode[0] == 0xf0
                 && pbCode[1] == 0xcd
                 && pbCode[2] == BS3_TRAP_SYSCALL)
        {
            pbCode += 3;
            bs3TrapDefaultHandlerV8086Syscall(pTrapFrame);
        }
        else
        {
            fHandled = false;
        }
        if (fHandled)
        {
            pTrapFrame->Ctx.rip.u16 += (uint16_t)(pbCode - pbCodeStart);
# if 0
            Bs3Printf("Calling Bs3RegCtxRestore\n");
            Bs3RegCtxPrint(&pTrapFrame->Ctx);
# endif
            Bs3RegCtxRestore(&pTrapFrame->Ctx, 0 /*fFlags*/); /* does not return. */
            return;
        }
    }
#endif

    /*
     * Any pending setjmp?
     */
    if (g_pBs3TrapSetJmpFrame != 0)
    {
        PBS3TRAPFRAME pSetJmpFrame = (PBS3TRAPFRAME)Bs3XptrFlatToCurrent(g_pBs3TrapSetJmpFrame);
        //Bs3Printf("Calling longjmp: pSetJmpFrame=%p (%#lx)\n", pSetJmpFrame, g_pBs3TrapSetJmpFrame);
        g_pBs3TrapSetJmpFrame = 0;
        Bs3MemCpy(pSetJmpFrame, pTrapFrame, sizeof(*pSetJmpFrame));
        //Bs3RegCtxPrint(&g_Bs3TrapSetJmpCtx);
        Bs3RegCtxRestore(&g_Bs3TrapSetJmpCtx, 0 /*fFlags*/);
    }

    /*
     * Fatal.
     *
     * Special case for #DB so we can get at the DRx values before we print anything,
     * as a aid for debugging VT-x/AMD-V code getting these out of sync.
     */
    if (pTrapFrame->bXcpt != X86_XCPT_DB)
    {
        Bs3TestPrintf("*** GURU ***\n");
        Bs3TrapPrintFrame(pTrapFrame);
    }
    else
    {
        Bs3TestPrintf("*** GURU ***\n"
#if ARCH_BITS == 64
                      "dr6=%08RX32 dr7=%08RX32 dr0=%08RX64\n"
                      "dr1=%08RX64 dr2=%08RX64 dr3=%08RX64\n"
#else
                      "dr6=%08RX32 dr7=%08RX32 dr0=%08RX32 dr1=%08RX32 dr2=%08RX32 dr3=%08RX32\n"
#endif
                      , (uint32_t)Bs3RegGetDr6(), (uint32_t)Bs3RegGetDr7(),
                      Bs3RegGetDr0(), Bs3RegGetDr1(), Bs3RegGetDr2(), Bs3RegGetDr3());
        Bs3TrapPrintFrame(pTrapFrame);
    }
    Bs3Panic();
}

