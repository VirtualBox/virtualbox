/* $Id: tstLdrDisasmTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTLdr test object.
 *
 * We use precompiled versions of this object for testing all the loaders.
 *
 * This is not supposed to be pretty or usable code, just something which
 * make life difficult for the loader.
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
#include <VBox/dis.h>
#include <VBox/sup.h>
#include <iprt/string.h>

#if defined(IN_RING0)
# define MY_PRINTF(a) SUPR0Printf a
#else
# define MY_PRINTF(a) do {} while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/* 32-bit code */
static const uint8_t g_ab32BitCode[] =
{
    0x55,               // 1000ab50 55              push    ebp
    0x8b,0xec,          // 1000ab51 8bec            mov     ebp,esp
    0x8b,0x45,0x08,     // 1000ab53 8b4508          mov     eax,dword ptr [ebp+8]
    0x81,0x38,0x07,0x07,// 1000ab56 813807076419    cmp     dword ptr [eax],19640707h
    0x64,0x19,
    0x75,0x09,          // 1000ab5c 7509            jne     kLdr!kLdrModMap+0x17 (1000ab67)
    0x8b,0x4d,0x08,     // 1000ab5e 8b4d08          mov     ecx,dword ptr [ebp+8]
    0x83,0x79,0x2c,0x00,// 1000ab61 83792c00        cmp     dword ptr [ecx+2Ch],0
    0x75,0x07,          // 1000ab65 7507            jne     kLdr!kLdrModMap+0x1e (1000ab6e)
    0xb8,0xc0,0x68,0x06,// 1000ab67 b8c0680600      mov     eax,668C0h
    0x00,
    0xeb,0x14,          // 1000ab6c eb14            jmp     kLdr!kLdrModMap+0x32 (1000ab82)
    0x33,0xd2,          // 1000ab6e 33d2            xor     edx,edx
    0x75,0xe1,          // 1000ab70 75e1            jne     kLdr!kLdrModMap+0x3 (1000ab53)
    0x8b,0x45,0x08,     // 1000ab72 8b4508          mov     eax,dword ptr [ebp+8]
    0x50,               // 1000ab75 50              push    eax
    0x8b,0x4d,0x08,     // 1000ab76 8b4d08          mov     ecx,dword ptr [ebp+8]
    0x8b,0x51,0x2c,     // 1000ab79 8b512c          mov     edx,dword ptr [ecx+2Ch]
    0xff,0x52,0x3c,     // 1000ab7c ff523c          call    dword ptr [edx+3Ch]
    0x83,0xc4,0x04,     // 1000ab7f 83c404          add     esp,4
    0x5d,               // 1000ab82 5d              pop     ebp
    0xc3,               // 1000ab83 c3              ret
    0xcc
};


/**
 * @callback_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) DisasmTest1ReadCode(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    size_t cb = cbMaxRead;
    if (cb + pDis->uInstrAddr + offInstr > sizeof(g_ab32BitCode))
        cb = cbMinRead;
    memcpy(&pDis->Instr.ab[offInstr], &g_ab32BitCode[pDis->uInstrAddr + offInstr], cb);
    pDis->cbCachedInstr = offInstr + (uint8_t)cb;
    return VINF_SUCCESS;
}


/*
 * Use an inline function here just to test '__textcoal_nt' sections on darwin.
 */
DECLINLINE(int) MyDisasm(uintptr_t CodeIndex, PDISSTATE pDis, uint32_t *pcb)
{
    uint32_t cb;
    int rc = DISInstrWithReader(CodeIndex, DISCPUMODE_32BIT, DisasmTest1ReadCode, 0, pDis, &cb);
    *pcb = cb;
    MY_PRINTF(("DISCoreOneEx -> rc=%d cb=%d  Cpu: bOpCode=%#x pCurInstr=%p (42=%d)\n", \
               rc, cb, pDis->x86.bOpCode, pDis->pCurInstr, 42)); \
    return rc;
}


extern "C" DECLEXPORT(int) DisasmTest1(void)
{
    DISSTATE  Dis;
    uintptr_t CodeIndex = 0;
    uint32_t cb;
    int rc;
    MY_PRINTF(("DisasmTest1: %p\n", &DisasmTest1));

#if defined(IN_RING0)
    MY_PRINTF(("GIP: g_pSUPGlobalInfoPage=%p\n", g_pSUPGlobalInfoPage));
    MY_PRINTF(("GIP: magic=%#x version=%#x mode=%d cCpus=%d\n", g_pSUPGlobalInfoPage->u32Magic, g_pSUPGlobalInfoPage->u32Version,
               g_pSUPGlobalInfoPage->u32Mode, g_pSUPGlobalInfoPage->cCpus));
    if (g_pSUPGlobalInfoPage->u32Magic != SUPGLOBALINFOPAGE_MAGIC)
        return 0xc001;
    if (g_pSUPGlobalInfoPage->u32Version != SUPGLOBALINFOPAGE_VERSION)
        return 0xc002;
    if (g_pSUPGlobalInfoPage->u32Mode != SUPGIPMODE_INVARIANT_TSC)
        return 0xc003;
    if (g_pSUPGlobalInfoPage->cCpus != 42)
        return 0xc004;
#endif

    memset(&Dis, 0, sizeof(Dis));

#define DISAS_AND_CHECK(cbInstr, enmOp) \
        do { \
            rc = MyDisasm(CodeIndex, &Dis, &cb); \
            if (RT_FAILURE(rc)) \
                return CodeIndex | 0xf000; \
            if (Dis.pCurInstr->uOpcode != (enmOp)) \
                return CodeIndex| 0xe000; \
            if (cb != (cbInstr)) \
                return CodeIndex | 0xd000; \
            CodeIndex += cb; \
        } while (0)

    DISAS_AND_CHECK(1, OP_PUSH);
    DISAS_AND_CHECK(2, OP_MOV);
    DISAS_AND_CHECK(3, OP_MOV);
    DISAS_AND_CHECK(6, OP_CMP);
    DISAS_AND_CHECK(2, OP_JNE);
    DISAS_AND_CHECK(3, OP_MOV);
    DISAS_AND_CHECK(4, OP_CMP);
    DISAS_AND_CHECK(2, OP_JNE);
    DISAS_AND_CHECK(5, OP_MOV);
    DISAS_AND_CHECK(2, OP_JMP);
    DISAS_AND_CHECK(2, OP_XOR);
    DISAS_AND_CHECK(2, OP_JNE);
    DISAS_AND_CHECK(3, OP_MOV);
    DISAS_AND_CHECK(1, OP_PUSH);
    DISAS_AND_CHECK(3, OP_MOV);
    DISAS_AND_CHECK(3, OP_MOV);
    DISAS_AND_CHECK(3, OP_CALL);
    DISAS_AND_CHECK(3, OP_ADD);
    DISAS_AND_CHECK(1, OP_POP);
    DISAS_AND_CHECK(1, OP_RETN);
    DISAS_AND_CHECK(1, OP_INT3);

    return rc;
}

