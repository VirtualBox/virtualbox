/* $Id: SUPR3HardenedMain-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened main(), posix bits.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/sup.h>

#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include <dlfcn.h>
#include <sys/mman.h>
#if defined(RT_OS_SOLARIS)
# include <link.h>
#endif
#include <stdio.h>
#include <stdint.h>

#include "SUPLibInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/**
 * Memory for code patching.
 */
#define DLOPEN_PATCH_MEMORY_SIZE   _4K


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifndef SUP_HARDENED_WITHOUT_DLOPEN_PATCHING
/**
 * Callback (SUPHARDENEDPOSIXHOOK::pfnResolv) for triggering lazy GOT resolver.
 *
 * This generally just calls the API in a harmless manner and triggers the lazy
 * resolving of the symbol, ensuring a proper address in the GOT/PLT entry.
 *
 * On Solaris dlsym() will return the value in the GOT/PLT entry.  We don't wish
 * to patch the lazy loader trampoline function, but rather the real function!
 */
typedef DECLCALLBACKTYPE(void, FNSUPHARDENEDSYMRESOLVE,(void));
/** Pointer to FNSUPHARDENEDSYMRESOLVE. */
typedef FNSUPHARDENEDSYMRESOLVE *PFNSUPHARDENEDSYMRESOLVE;

/**
 * A hook descriptor.
 */
typedef struct SUPHARDENEDPOSIXHOOK
{
    /** The symbol to hook. */
    const char              *pszSymbol;
    /** The intercepting wrapper doing additional checks. */
    PFNRT                    pfnHook;
    /** Where to store the pointer to the code into patch memory
     * which resumes the original call.
     * @note uintptr_t instead of PFNRT is for Clang 11. */
    uintptr_t               *ppfnRealResume;
    /** Pointer to the resolver method used on Solaris. */
    PFNSUPHARDENEDSYMRESOLVE pfnResolve;
} SUPHARDENEDPOSIXHOOK;
/** Pointer to a hook descriptor. */
typedef SUPHARDENEDPOSIXHOOK *PSUPHARDENEDPOSIXHOOK;
/** Pointer to a const hook descriptor. */
typedef const SUPHARDENEDPOSIXHOOK *PCSUPHARDENEDPOSIXHOOK;

/** dlopen() declaration. */
typedef void *FNDLOPEN(const char *pszFilename, int fFlags);
/** Pointer to dlopen. */
typedef FNDLOPEN *PFNDLOPEN;

#ifdef SUP_HARDENED_WITH_DLMOPEN
/** dlmopen() declaration */
typedef void *FNDLMOPEN(Lmid_t idLm, const char *pszFilename, int fFlags);
/** Pointer to dlmopen. */
typedef FNDLMOPEN *PFNDLMOPEN;
#endif

#endif /* SUP_HARDENED_WITHOUT_DLOPEN_PATCHING */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef SUP_HARDENED_WITHOUT_DLOPEN_PATCHING
static FNSUPHARDENEDSYMRESOLVE supR3HardenedPosixMonitorDlopenResolve;
#ifdef SUP_HARDENED_WITH_DLMOPEN
static FNSUPHARDENEDSYMRESOLVE supR3HardenedPosixMonitorDlmopenResolve;
#endif

/* SUPR3HardenedMainA-posix.asm: */
DECLASM(void) supR3HardenedPosixMonitor_Dlopen(const char *pszFilename, int fFlags);
#ifdef SUP_HARDENED_WITH_DLMOPEN
DECLASM(void) supR3HardenedPosixMonitor_Dlmopen(Lmid_t idLm, const char *pszFilename, int fFlags);
#endif
#endif /* SUP_HARDENED_WITHOUT_DLOPEN_PATCHING */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef SUP_HARDENED_WITHOUT_DLOPEN_PATCHING

RT_C_DECLS_BEGIN
/** Resume patch for dlopen(), jumped to form assembly stub. */
DECL_HIDDEN_DATA(PFNDLOPEN)     g_pfnDlopenReal  = NULL;
#ifdef SUP_HARDENED_WITH_DLMOPEN
/** Resume patch for dlmopen(), jumped to form assembly stub. */
DECL_HIDDEN_DATA(PFNDLMOPEN)    g_pfnDlmopenReal = NULL;
#endif
RT_C_DECLS_END

/** Memory allocated for the patches. */
static uint8_t *g_pbExecMemory = NULL;
/** Offset into the patch memory which is not used. */
static uint32_t g_offExecMemory = 0;

/**
 * Array of hooks to install.
 */
static SUPHARDENEDPOSIXHOOK const g_aHooks[] =
{
    /* pszSymbol,       pfnHook,                                         ppfnRealResume,   pfnResolve */
    { "dlopen",  (PFNRT)supR3HardenedPosixMonitor_Dlopen,  (uintptr_t *)&g_pfnDlopenReal,  supR3HardenedPosixMonitorDlopenResolve  },
#ifdef SUP_HARDENED_WITH_DLMOPEN
    { "dlmopen", (PFNRT)supR3HardenedPosixMonitor_Dlmopen, (uintptr_t *)&g_pfnDlmopenReal, supR3HardenedPosixMonitorDlmopenResolve }
#endif
};



/**
 * Verifies the given library for proper access rights for further loading
 * into the process.
 *
 * @returns Flag whether the access rights of the library look sane and loading
 *          it is not considered a security risk. Returns true if the library
 *          looks sane, false otherwise.
 * @param   pszFilename         The library to load, this can be an absolute or relative path
 *                              or just the filename of the library when the default paths should
 *                              be searched. NULL is allowed too to indicate opening the main
 *                              binary.
 */
DECLASM(bool) supR3HardenedPosixMonitor_VerifyLibrary(const char *pszFilename)
{
    /*
     * Giving NULL as the filename indicates opening the main program which is fine
     * We are already loaded and executing after all.
     *
     * Filenames without any path component (whether absolute or relative) are allowed
     * unconditionally too as the loader will only search the default paths configured by root.
     */
    bool fAllow = true;

    if (   pszFilename
        && strchr(pszFilename, '/') != NULL)
    {
#if defined(RT_OS_LINUX)
        int rc = supR3HardenedVerifyFileFollowSymlinks(pszFilename, RTHCUINTPTR_MAX, true /* fMaybe3rdParty */,
                                                       NULL /* pErrInfo */);
#else
        int rc = supR3HardenedVerifyFile(pszFilename, RTHCUINTPTR_MAX, true /* fMaybe3rdParty */,
                                         NULL /* pErrInfo */);
#endif

        if (RT_FAILURE(rc))
            fAllow = false;
    }

    return fAllow;
}


/**
 * Returns the start address of the given symbol if found or NULL otherwise.
 *
 * @returns Start address of the symbol or NULL if not found.
 * @param   pszSymbol           The symbol name.
 * @param   pfnResolve          The resolver to call before trying to query the start address.
 */
static void *supR3HardenedMainPosixGetStartBySymbol(const char *pszSymbol, PFNSUPHARDENEDSYMRESOLVE pfnResolve)
{
#ifndef RT_OS_SOLARIS
    RT_NOREF(pfnResolve);
    return dlsym(RTLD_DEFAULT, pszSymbol);

#else  /* RT_OS_SOLARIS */
    /*
     * Solaris is tricky as dlsym doesn't return the actual start address of
     * the symbol but the start of the trampoline in the PLT of the caller.
     *
     * Disassemble the first jmp instruction to get at the entry in the global
     * offset table where the actual address is stored.
     *
     * To counter lazy symbol resolving, we first have to call the API before
     * trying to resolve and disassemble it.
     */
    pfnResolve();

    uint8_t *pbSym = (uint8_t *)dlsym(RTLD_DEFAULT, pszSymbol);

# ifdef RT_ARCH_AMD64
    DISSTATE Dis;
    uint32_t cbInstr = 1;
    int rc = DISInstr(pbSym, DISCPUMODE_64BIT, &Dis, &cbInstr);
    if (   RT_FAILURE(rc)
        || Dis.pCurInstr->uOpcode != OP_JMP
        || !(Dis.x86.ModRM.Bits.Mod == 0 && Dis.x86.ModRM.Bits.Rm == 5 /* wrt RIP */))
        return NULL;

    /* Extract start address. */
    pbSym = (pbSym + cbInstr + Dis.aParams[0].x86.uDisp.i32);
    pbSym = (uint8_t *)*((uintptr_t *)pbSym);
# else
#  error "Unsupported architecture"
# endif

    return pbSym;
#endif /* RT_OS_SOLARIS */
}


/**
 * Allocates executable patch memory with the given constraints.
 *
 * @returns VBox status code.
 * @param   cb                  Size of the patch memory in bytes.
 * @param   pvHint              Where to try allocating nearby.
 * @param   fRipRelAddr         Flag whether the executable memory must be within
 *                              2GB before or after the hint as it will contain
 *                              instructions using RIP relative addressing
 */
static uint8_t *supR3HardenedMainPosixExecMemAlloc(size_t cb, void *pvHint, bool fRipRelAddr)
{
    AssertReturn(cb < _1K, NULL);

    /* Lazy allocation of exectuable memory. */
    if (!g_pbExecMemory)
    {
        g_pbExecMemory = (uint8_t *)mmap(pvHint, DLOPEN_PATCH_MEMORY_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        g_offExecMemory = 0;
        if (g_pbExecMemory == MAP_FAILED)
            return NULL;

        memset(g_pbExecMemory, 0xcc, DLOPEN_PATCH_MEMORY_SIZE);
    }

    if (g_offExecMemory + cb >= DLOPEN_PATCH_MEMORY_SIZE)
        return NULL;

    uint8_t *pb = &g_pbExecMemory[g_offExecMemory];

    if (fRipRelAddr)
    {
        /* Check that we allocated within 2GB of the hint. */
        uintptr_t uPtrHint     = (uintptr_t)pvHint;
        uintptr_t uPtrPatchMem = (uintptr_t)pb;
        uintptr_t cbDistance   = uPtrHint < uPtrPatchMem
                               ? uPtrPatchMem - uPtrHint
                               : uPtrHint - uPtrPatchMem;

        if (cbDistance >= _2G - _4K)
            return NULL;
    }

    g_offExecMemory = RT_ALIGN_32(g_offExecMemory + cb, 16);
    return pb;
}


/**
 * Hooks the given method to execute the given one first.
 *
 * @returns VBox status code.
 * @param   pszSymbol           The symbol to hook.
 * @param   pfnHook             The hook to install.
 * @param   ppfnReal            Where to store the pointer to entry point of the real method
 *                              (somewhere in patch memory).
 * @param   pfnResolve          The resolver to call before trying to query the start address.
 */
static int supR3HardenedMainPosixHookOne(const char *pszSymbol, PFNRT pfnHook, uintptr_t /*PFNRT*/ *ppfnReal,
                                         PFNSUPHARDENEDSYMRESOLVE pfnResolve)
{
    void *pfnTarget = supR3HardenedMainPosixGetStartBySymbol(pszSymbol, pfnResolve);
    if (!pfnTarget)
        return VERR_NOT_FOUND;

    /*
     * Make the target memory writeable to be able to insert the patch.
     * Unprotect two pages in case the code crosses a page boundary.
     */
    void *pvTargetBase = (void *)(((uintptr_t)pfnTarget) & ~(uintptr_t)(_4K - 1));
    int rcPsx = mprotect(pvTargetBase, 2 * _4K, PROT_WRITE | PROT_READ | PROT_EXEC);
    if (rcPsx == -1)
        return VERR_SUPLIB_TEXT_NOT_WRITEABLE;

    uint8_t * const pbTarget = (uint8_t *)(uintptr_t)pfnTarget;

    DISSTATE Dis;
    uint32_t cbInstr;
    uint32_t offJmpBack = 0;
    uint32_t cbPatchMem = 0;

#ifdef RT_ARCH_AMD64
    /*
     * Patch 64-bit hosts.
     */
    uint32_t cRipRelMovs = 0;
    uint32_t cRelCalls = 0;

    /* Just use the disassembler to skip 12 bytes or more, we might need to
       rewrite mov instructions using RIP relative addressing. */
    while (offJmpBack < 12)
    {
        cbInstr = 1;
        int rc = DISInstr(pbTarget + offJmpBack, DISCPUMODE_64BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (   Dis.pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW
                && Dis.pCurInstr->uOpcode != OP_CALL)
            || (   Dis.x86.ModRM.Bits.Mod == 0
                && Dis.x86.ModRM.Bits.Rm  == 5 /* wrt RIP */
                && Dis.pCurInstr->uOpcode != OP_MOV))
            return VERR_SUPLIB_UNEXPECTED_INSTRUCTION;

        if (Dis.x86.ModRM.Bits.Mod == 0 && Dis.x86.ModRM.Bits.Rm == 5 /* wrt RIP */)
            cRipRelMovs++;
        if (   Dis.pCurInstr->uOpcode == OP_CALL
            && (Dis.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW))
            cRelCalls++;

        offJmpBack += cbInstr;
        cbPatchMem += cbInstr;
    }

    /*
     * Each relative call requires extra bytes as it is converted to a pushq imm32
     * + mov [RSP+4], imm32 + a jmp qword [$+8 wrt RIP] to avoid clobbering registers.
     */
    cbPatchMem += cRelCalls * RT_ALIGN_32(13 + 6 + 8, 8);
    cbPatchMem += 14; /* jmp qword [$+8 wrt RIP] + 8 byte address to jump to. */
    cbPatchMem = RT_ALIGN_32(cbPatchMem, 8);

    /* Allocate suitable executable memory available. */
    bool fConvRipRelMovs = false;
    uint8_t *pbPatchMem = supR3HardenedMainPosixExecMemAlloc(cbPatchMem, pbTarget, cRipRelMovs > 0);
    if (!pbPatchMem)
    {
        /*
         * Try to allocate memory again without the RIP relative mov addressing constraint
         * Makes it a bit more difficult for us later on but there is no way around it.
         * We need to increase the patch memory because we create two instructions for one
         * (7 bytes for the RIP relative mov vs. 13 bytes for the two instructions replacing it ->
         * need to allocate 6 bytes more per RIP relative mov).
         */
        fConvRipRelMovs = true;
        if (cRipRelMovs > 0)
            pbPatchMem = supR3HardenedMainPosixExecMemAlloc(cbPatchMem + cRipRelMovs * 6,
                                                            pbTarget, false /*fRipRelAddr*/);

        if (!pbPatchMem)
            return VERR_NO_MEMORY;
    }

    /* Assemble the code for resuming the call.*/
    *ppfnReal = (uintptr_t)pbPatchMem;

    /* Go through the instructions to patch and fixup any rip relative mov instructions. */
    uint32_t offInsn = 0;
    while (offInsn < offJmpBack)
    {
        cbInstr = 1;
        int rc = DISInstr(pbTarget + offInsn, DISCPUMODE_64BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (   Dis.pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW
                && Dis.pCurInstr->uOpcode != OP_CALL))
            return VERR_SUPLIB_UNEXPECTED_INSTRUCTION;

        if (   Dis.x86.ModRM.Bits.Mod == 0
            && Dis.x86.ModRM.Bits.Rm  == 5 /* wrt RIP */
            && Dis.pCurInstr->uOpcode == OP_MOV)
        {
            /* Deduce destination register and write out new instruction. */
            if (RT_UNLIKELY(!(   (Dis.aParams[0].fUse & (DISUSE_BASE | DISUSE_REG_GEN64))
                              && (Dis.aParams[1].fUse & DISUSE_RIPDISPLACEMENT32))))
                return VERR_SUPLIB_UNEXPECTED_INSTRUCTION;

            uintptr_t uAddr = (uintptr_t)&pbTarget[offInsn + cbInstr] + (intptr_t)Dis.aParams[1].x86.uDisp.i32;

            if (fConvRipRelMovs)
            {
                /*
                 * Create two instructions, first one moves the address as a constant to the destination register
                 * and the second one loads the data from the memory into the destination register.
                 */

                *pbPatchMem++ = 0x48;
                *pbPatchMem++ = 0xb8 + Dis.aParams[0].x86.Base.idxGenReg;
                *(uintptr_t *)pbPatchMem = uAddr;
                pbPatchMem   += sizeof(uintptr_t);

                *pbPatchMem++ = 0x48;
                *pbPatchMem++ = 0x8b;
                *pbPatchMem++ = (Dis.aParams[0].x86.Base.idxGenReg << X86_MODRM_REG_SHIFT) | Dis.aParams[0].x86.Base.idxGenReg;
            }
            else
            {
                intptr_t  iDispNew   = uAddr - (uintptr_t)&pbPatchMem[3 + sizeof(int32_t)];
                Assert(iDispNew == (int32_t)iDispNew);

                /* Assemble the mov to register instruction with the updated rip relative displacement. */
                *pbPatchMem++ = 0x48;
                *pbPatchMem++ = 0x8b;
                *pbPatchMem++ = (Dis.aParams[0].x86.Base.idxGenReg << X86_MODRM_REG_SHIFT) | 5;
                *(int32_t *)pbPatchMem = (int32_t)iDispNew;
                pbPatchMem   += sizeof(int32_t);
            }
        }
        else if (   Dis.pCurInstr->uOpcode == OP_CALL
                 && (Dis.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW))
        {
            /* Convert to absolute jump. */
            uintptr_t uAddr = (uintptr_t)&pbTarget[offInsn + cbInstr] + (intptr_t)Dis.aParams[0].uValue;

            /* Skip the push instructions till the return address is known. */
            uint8_t *pbPatchMemPush = pbPatchMem;
            pbPatchMem += 13;

            *pbPatchMem++ = 0xff; /* jmp qword [$+8 wrt RIP] */
            *pbPatchMem++ = 0x25;
            *(uint32_t *)pbPatchMem = (uint32_t)(RT_ALIGN_PT(pbPatchMem + 4, 8, uint8_t *) - (pbPatchMem + 4));
            pbPatchMem = RT_ALIGN_PT(pbPatchMem + 4, 8, uint8_t *);
            *(uint64_t *)pbPatchMem = uAddr;
            pbPatchMem += sizeof(uint64_t);

            /* Push the return address onto stack. Difficult on amd64 without clobbering registers... */
            uintptr_t uAddrReturn = (uintptr_t)pbPatchMem;
            *pbPatchMemPush++ = 0x68; /* push imm32 sign-extended as 64-bit*/
            *(uint32_t *)pbPatchMemPush = RT_LO_U32(uAddrReturn);
            pbPatchMemPush += sizeof(uint32_t);
            *pbPatchMemPush++ = 0xc7;
            *pbPatchMemPush++ = 0x44;
            *pbPatchMemPush++ = 0x24;
            *pbPatchMemPush++ = 0x04; /* movl [RSP+4], imm32 */
            *(uint32_t *)pbPatchMemPush = RT_HI_U32(uAddrReturn);
        }
        else
        {
            memcpy(pbPatchMem, pbTarget + offInsn, cbInstr);
            pbPatchMem += cbInstr;
        }

        offInsn += cbInstr;
    }

    *pbPatchMem++ = 0xff; /* jmp qword [$+8 wrt RIP] */
    *pbPatchMem++ = 0x25;
    *(uint32_t *)pbPatchMem = (uint32_t)(RT_ALIGN_PT(pbPatchMem + 4, 8, uint8_t *) - (pbPatchMem + 4));
    pbPatchMem = RT_ALIGN_PT(pbPatchMem + 4, 8, uint8_t *);
    *(uint64_t *)pbPatchMem = (uintptr_t)&pbTarget[offJmpBack];

    /* Assemble the patch. */
    Assert(offJmpBack >= 12);
    pbTarget[0]  = 0x48; /* mov rax, qword */
    pbTarget[1]  = 0xb8;
    *(uintptr_t *)&pbTarget[2] = (uintptr_t)pfnHook;
    pbTarget[10] = 0xff; /* jmp rax */
    pbTarget[11] = 0xe0;

#else  /* !RT_ARCH_AMD64 */
    /*
     * Patch 32-bit hosts.
     */
    /* Just use the disassembler to skip 5 bytes or more. */
    while (offJmpBack < 5)
    {
        cbInstr = 1;
        int rc = DISInstr(pbTarget + offJmpBack, DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (   (Dis.pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW)
                && Dis.pCurInstr->uOpcode != OP_CALL))
            return VERR_SUPLIB_UNEXPECTED_INSTRUCTION;

        if (   Dis.pCurInstr->uOpcode == OP_CALL
            && (Dis.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW))
            cbPatchMem += 10; /* push imm32 + jmp rel32 */
        else
            cbPatchMem += cbInstr;

        offJmpBack += cbInstr;
    }

    /* Allocate suitable exectuable memory available. */
    uint8_t *pbPatchMem = supR3HardenedMainPosixExecMemAlloc(cbPatchMem, pbTarget, false /* fRipRelAddr */);
    if (!pbPatchMem)
        return VERR_NO_MEMORY;

    /* Assemble the code for resuming the call.*/
    *ppfnReal = (uintptr_t)pbPatchMem;

    /* Go through the instructions to patch and fixup any relative call instructions. */
    uint32_t offInsn = 0;
    while (offInsn < offJmpBack)
    {
        cbInstr = 1;
        int rc = DISInstr(pbTarget + offInsn, DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (   (Dis.pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW)
                && Dis.pCurInstr->uOpcode != OP_CALL))
            return VERR_SUPLIB_UNEXPECTED_INSTRUCTION;

        if (   Dis.pCurInstr->uOpcode == OP_CALL
            && (Dis.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW))
        {
            /*
             * Don't use a call instruction directly but push the original return address
             * onto the stack and use a relative jump to the call target.
             * The reason here is that on Linux the called method saves the return
             * address from the stack which will be different from the original because
             * the code is executed from our patch memory.
             *
             * Luckily the call instruction is 5 bytes long which means it is always the
             * last instruction to patch and we don't need to return from the call
             * to patch memory anyway but can use this method to resume the original call.
             */
            AssertReturn(offInsn + cbInstr >= offJmpBack, VERR_SUPLIB_UNEXPECTED_INSTRUCTION); /* Must be last instruction! */

            /* push return address */
            uint32_t const uAddrReturn = (uintptr_t)&pbTarget[offInsn + cbInstr]; /* The return address to push to the stack. */

            *pbPatchMem++           = 0x68; /* push dword */
            *(uint32_t *)pbPatchMem = uAddrReturn;
            pbPatchMem             += sizeof(uint32_t);

            /* jmp rel32 to the call target */
            uintptr_t const uAddr      = uAddrReturn + (int32_t)Dis.aParams[0].uValue;
            int32_t   const i32DispNew = uAddr - (uintptr_t)&pbPatchMem[5];

            *pbPatchMem++          = 0xe9; /* jmp rel32 */
            *(int32_t *)pbPatchMem = i32DispNew;
            pbPatchMem            += sizeof(int32_t);
        }
        else
        {
            memcpy(pbPatchMem, pbTarget + offInsn, cbInstr);
            pbPatchMem += cbInstr;
        }

        offInsn += cbInstr;
    }

    *pbPatchMem++ = 0xe9; /* jmp rel32 */
    *(uint32_t *)pbPatchMem = (uintptr_t)&pbTarget[offJmpBack] - ((uintptr_t)pbPatchMem + 4);

    /* Assemble the patch. */
    Assert(offJmpBack >= 5);
    pbTarget[0] = 0xe9;
    *(uint32_t *)&pbTarget[1] = (uintptr_t)pfnHook - (uintptr_t)&pbTarget[1+4];
#endif /* !RT_ARCH_AMD64 */

    /*
     * Re-seal target (ASSUMING that the shared object either has page aligned
     * section or that the patch target is far enough from the writable parts).
     */
    rcPsx = mprotect(pvTargetBase, 2 * _4K, PROT_READ | PROT_EXEC);
    if (rcPsx == -1)
        return VERR_SUPLIB_TEXT_NOT_SEALED;

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSUPHARDENEDSYMRESOLVE, dlopen}
 */
static DECLCALLBACK(void) supR3HardenedPosixMonitorDlopenResolve(void)
{
    /* Make harmless dlopen call. */
    void *pv = dlopen(NULL, RTLD_LAZY);
    if (pv)
        dlclose(pv);
}


#ifdef SUP_HARDENED_WITH_DLMOPEN
/**
 * @callback_method_impl{FNSUPHARDENEDSYMRESOLVE, dlmopen}
 */
static DECLCALLBACK(void) supR3HardenedPosixMonitorDlmopenResolve(void)
{
    /* Make harmless dlmopen call. */
    void *pv = dlmopen(LM_ID_BASE, NULL, RTLD_LAZY);
    if (pv)
        dlclose(pv);
}
#endif

#endif /* SUP_HARDENED_WITHOUT_DLOPEN_PATCHING */


/**
 * Hardening initialization for POSIX compatible hosts.
 *
 * @note Doesn't return on error.
 */
DECLHIDDEN(void) supR3HardenedPosixInit(void)
{
#ifndef SUP_HARDENED_WITHOUT_DLOPEN_PATCHING
    for (unsigned i = 0; i < RT_ELEMENTS(g_aHooks); i++)
    {
        PCSUPHARDENEDPOSIXHOOK pHook = &g_aHooks[i];
        int rc = supR3HardenedMainPosixHookOne(pHook->pszSymbol, pHook->pfnHook, pHook->ppfnRealResume, pHook->pfnResolve);
        if (RT_FAILURE(rc))
            supR3HardenedFatalMsg("supR3HardenedPosixInit", kSupInitOp_Integrity, rc,
                                  "Failed to hook the %s interface", pHook->pszSymbol);
    }
#endif
}



/*
 * assert.cpp
 *
 * ASSUMES working DECLHIDDEN or there will be symbol confusion!
 */

RTDATADECL(char)                     g_szRTAssertMsg1[1024];
RTDATADECL(char)                     g_szRTAssertMsg2[4096];
RTDATADECL(const char * volatile)    g_pszRTAssertExpr;
RTDATADECL(const char * volatile)    g_pszRTAssertFile;
RTDATADECL(uint32_t volatile)        g_u32RTAssertLine;
RTDATADECL(const char * volatile)    g_pszRTAssertFunction;

RTDECL(bool) RTAssertMayPanic(void)
{
    return true;
}


RTDECL(void) RTAssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * Fill in the globals.
     */
    g_pszRTAssertExpr       = pszExpr;
    g_pszRTAssertFile       = pszFile;
    g_pszRTAssertFunction   = pszFunction;
    g_u32RTAssertLine       = uLine;
    snprintf(g_szRTAssertMsg1, sizeof(g_szRTAssertMsg1),
             "\n!!Assertion Failed!!\n"
             "Expression: %s\n"
             "Location  : %s(%u) %s\n",
             pszExpr, pszFile, uLine, pszFunction);
}


RTDECL(void) RTAssertMsg2V(const char *pszFormat, va_list va)
{
    vsnprintf(g_szRTAssertMsg2, sizeof(g_szRTAssertMsg2), pszFormat, va);
    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_CALLED_TRUSTED_MAIN)
        supR3HardenedFatalMsg(g_pszRTAssertExpr, kSupInitOp_Misc, VERR_INTERNAL_ERROR,
                              "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
    else
        supR3HardenedError(VERR_INTERNAL_ERROR, false/*fFatal*/, "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
}

