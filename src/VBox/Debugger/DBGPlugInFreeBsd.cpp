/* $Id: DBGPlugInFreeBsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInFreeBsd - Debugger and Guest OS Digger Plugin For FreeBSD.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF /// @todo add new log group.
#include "DBGPlugIns.h"
#include "DBGPlugInCommonELF.h"
#include <VBox/vmm/vmmr3vtable.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** FreeBSD on little endian ASCII systems. */
#define DIG_FBSD_MOD_TAG     UINT64_C(0x0044534265657246)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * FreeBSD .dynstr and .dynsym location probing state.
 */
typedef enum FBSDPROBESTATE
{
    /** Invalid state. */
    FBSDPROBESTATE_INVALID = 0,
    /** Searching for the end of the .dynstr section (terminator). */
    FBSDPROBESTATE_DYNSTR_END,
    /** Last symbol was a symbol terminator character. */
    FBSDPROBESTATE_DYNSTR_SYM_TERMINATOR,
    /** Last symbol was a symbol character. */
    FBSDPROBESTATE_DYNSTR_SYM_CHAR
} FBSDPROBESTATE;

/**
 * ELF headers union.
 */
typedef union ELFEHDRS
{
    /** 32bit version of the ELF header. */
    Elf32_Ehdr    Hdr32;
    /** 64bit version of the ELF header. */
    Elf64_Ehdr    Hdr64;
} ELFEHDRS;
/** Pointer to a ELF header union. */
typedef ELFEHDRS *PELFEHDRS;
/** Pointer to const ELF header union. */
typedef ELFEHDRS const *PCELFEHDRS;

/**
 * ELF symbol entry union.
 */
typedef union ELFSYMS
{
    /** 32bit version of the ELF section header. */
    Elf32_Sym     Hdr32;
    /** 64bit version of the ELF section header. */
    Elf64_Sym     Hdr64;
} ELFSYMS;
/** Pointer to a ELF symbol entry union. */
typedef ELFSYMS *PELFSYMS;
/** Pointer to const ELF symbol entry union. */
typedef ELFSYMS const *PCELFSYMS;

/**
 * Message buffer structure.
 */
typedef union FBSDMSGBUF
{
    /** 32bit version. */
    struct
    {
        /** Message buffer pointer. */
        uint32_t        msg_ptr;
        /** Magic value to identify the structure. */
        uint32_t        msg_magic;
        /** Size of the buffer area. */
        uint32_t        msg_size;
        /** Write sequence number. */
        uint32_t        msg_wseq;
        /** Read sequence number. */
        uint32_t        msg_rseq;
        /** @todo More fields which are not required atm. */
    } Hdr32;
    /** 64bit version. */
    struct
    {
        /** Message buffer pointer. */
        uint64_t        msg_ptr;
        /** Magic value to identify the structure. */
        uint32_t        msg_magic;
        /** Size of the buffer area. */
        uint32_t        msg_size;
        /** Write sequence number. */
        uint32_t        msg_wseq;
        /** Read sequence number. */
        uint32_t        msg_rseq;
        /** @todo More fields which are not required atm. */
    } Hdr64;
} FBSDMSGBUF;
/** Pointer to a message buffer structure. */
typedef FBSDMSGBUF *PFBSDMSGBUF;
/** Pointer to a const message buffer structure. */
typedef FBSDMSGBUF const *PCFBSDMSGBUF;

/** Magic value to identify the message buffer structure. */
#define FBSD_MSGBUF_MAGIC UINT32_C(0x063062)

/**
 * FreeBSD guest OS digger instance data.
 */
typedef struct DBGDIGGERFBSD
{
    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool            fValid;
    /** 64-bit/32-bit indicator. */
    bool            f64Bit;

    /** Address of the start of the kernel ELF image,
     * set during probing. */
    DBGFADDRESS     AddrKernelElfStart;
    /** Address of the interpreter content aka "/red/herring". */
    DBGFADDRESS     AddrKernelInterp;
    /** Address of the start of the text section. */
    DBGFADDRESS     AddrKernelText;

    /** The kernel message log interface. */
    DBGFOSIDMESG    IDmesg;

} DBGDIGGERFBSD;
/** Pointer to the FreeBSD guest OS digger instance data. */
typedef DBGDIGGERFBSD *PDBGDIGGERFBSD;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Min kernel address (32bit). */
#define FBSD32_MIN_KRNL_ADDR             UINT32_C(0x80000000)
/** Max kernel address (32bit). */
#define FBSD32_MAX_KRNL_ADDR             UINT32_C(0xfffff000)

/** Min kernel address (64bit). */
#define FBSD64_MIN_KRNL_ADDR             UINT64_C(0xFFFFF80000000000)
/** Max kernel address (64bit). */
#define FBSD64_MAX_KRNL_ADDR             UINT64_C(0xFFFFFFFFFFF00000)


/** Validates a 32-bit FreeBSD kernel address */
#define FBSD32_VALID_ADDRESS(Addr)      (   (Addr) > FBSD32_MIN_KRNL_ADDR \
                                         && (Addr) < FBSD32_MAX_KRNL_ADDR)
/** Validates a 64-bit FreeBSD kernel address */
#define FBSD64_VALID_ADDRESS(Addr)       (   (Addr) > FBSD64_MIN_KRNL_ADDR \
                                          && (Addr) < FBSD64_MAX_KRNL_ADDR)

/** Validates a FreeBSD kernel address. */
#define FBSD_VALID_ADDRESS(a_pThis, a_Addr) ((a_pThis)->f64Bit ? FBSD64_VALID_ADDRESS(a_Addr) : FBSD32_VALID_ADDRESS(a_Addr))

/** Maximum offset from the start of the ELF image we look for the /red/herring .interp section content. */
#define FBSD_MAX_INTERP_OFFSET           _16K
/** The max kernel size. */
#define FBSD_MAX_KERNEL_SIZE             UINT32_C(0x0f000000)

/** Versioned and bitness wrapper. */
#define FBSD_UNION(a_pThis, a_pUnion, a_Member)  ((a_pThis)->f64Bit ? (a_pUnion)->Hdr64. a_Member : (a_pUnion)->Hdr32. a_Member )


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerFreeBsdInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Table of common FreeBSD kernel addresses. */
static uint64_t g_au64FreeBsdKernelAddresses[] =
{
    UINT64_C(0xc0100000),
    UINT64_C(0xffffffff80100000)
};
/** Magic string which resides in the .interp section of the image. */
static const uint8_t g_abNeedleInterp[] = "/red/herring";


/**
 * Load the symbols from the .dynsym and .dynstr sections given
 * by their address in guest memory.
 *
 * @returns VBox status code.
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   pVMM            The VMM function table.
 * @param   pszName         The image name.
 * @param   uKernelStart    The kernel start address.
 * @param   cbKernel        Size of the kernel image.
 * @param   pAddrDynsym     Start address of the .dynsym section.
 * @param   cSymbols        Number of symbols in the .dynsym section.
 * @param   pAddrDynstr     Start address of the .dynstr section containing the symbol names.
 * @param   cbDynstr        Size of the .dynstr section.
 */
static int dbgDiggerFreeBsdLoadSymbols(PDBGDIGGERFBSD pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszName,
                                       RTGCUINTPTR uKernelStart, size_t cbKernel, PDBGFADDRESS pAddrDynsym, uint32_t cSymbols,
                                       PDBGFADDRESS pAddrDynstr, size_t cbDynstr)
{
    LogFlowFunc(("pThis=%#p pszName=%s uKernelStart=%RGv cbKernel=%zu pAddrDynsym=%#p{%RGv} cSymbols=%u pAddrDynstr=%#p{%RGv} cbDynstr=%zu\n",
                 pThis, pszName, uKernelStart, cbKernel, pAddrDynsym, pAddrDynsym->FlatPtr, cSymbols, pAddrDynstr, pAddrDynstr->FlatPtr, cbDynstr));

    char *pbDynstr = (char *)RTMemAllocZ(cbDynstr + 1); /* Extra terminator. */
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pAddrDynstr, pbDynstr, cbDynstr);
    if (RT_SUCCESS(rc))
    {
        uint32_t cbDynsymEnt = pThis->f64Bit ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym);
        uint8_t *pbDynsym = (uint8_t *)RTMemAllocZ(cSymbols * cbDynsymEnt);
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pAddrDynsym, pbDynsym, cSymbols * cbDynsymEnt);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create a module for the kernel.
             */
            RTDBGMOD hMod;
            rc = RTDbgModCreate(&hMod, pszName, cbKernel, 0 /*fFlags*/);
            if (RT_SUCCESS(rc))
            {
                rc = RTDbgModSetTag(hMod, DIG_FBSD_MOD_TAG); AssertRC(rc);
                rc = VINF_SUCCESS;

                /*
                 * Enumerate the symbols.
                 */
                uint32_t cLeft = cSymbols;
                while (cLeft-- > 0 && RT_SUCCESS(rc))
                {
                    PCELFSYMS   pSym      = (PCELFSYMS)&pbDynsym[cLeft * cbDynsymEnt];
                    uint32_t    idxSymStr = FBSD_UNION(pThis, pSym, st_name);
                    uint8_t     uType     = FBSD_UNION(pThis, pSym, st_info);
                    RTGCUINTPTR AddrVal   = FBSD_UNION(pThis, pSym, st_value);
                    size_t      cbSymVal  = FBSD_UNION(pThis, pSym, st_size);

                    /* Add it without the type char. */
                    RT_NOREF(uType);
                    if (   AddrVal <= uKernelStart + cbKernel
                        && idxSymStr < cbDynstr)
                    {
                        rc = RTDbgModSymbolAdd(hMod, &pbDynstr[idxSymStr], RTDBGSEGIDX_RVA, AddrVal - uKernelStart,
                                               cbSymVal, 0 /*fFlags*/, NULL);
                        if (RT_FAILURE(rc))
                        {
                            if (   rc == VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE
                                || rc == VERR_DBG_INVALID_RVA
                                || rc == VERR_DBG_ADDRESS_CONFLICT
                                || rc == VERR_DBG_DUPLICATE_SYMBOL)
                            {
                                Log2(("dbgDiggerFreeBsdLoadSymbols: RTDbgModSymbolAdd(,%s,) failed %Rrc (ignored)\n",
                                      &pbDynstr[idxSymStr], rc));
                                rc = VINF_SUCCESS;
                            }
                            else
                                Log(("dbgDiggerFreeBsdLoadSymbols: RTDbgModSymbolAdd(,%s,) failed %Rrc\n",
                                     &pbDynstr[idxSymStr], rc));
                        }
                    }
                }

                /*
                 * Link the module into the address space.
                 */
                if (RT_SUCCESS(rc))
                {
                    RTDBGAS hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
                    if (hAs != NIL_RTDBGAS)
                        rc = RTDbgAsModuleLink(hAs, hMod, uKernelStart, RTDBGASLINK_FLAGS_REPLACE);
                    else
                        rc = VERR_INTERNAL_ERROR;
                    RTDbgAsRelease(hAs);
                }
                else
                    Log(("dbgDiggerFreeBsdLoadSymbols: Failed: %Rrc\n", rc));
                RTDbgModRelease(hMod);
            }
            else
                Log(("dbgDiggerFreeBsdLoadSymbols: RTDbgModCreate failed: %Rrc\n", rc));
        }
        else
            Log(("dbgDiggerFreeBsdLoadSymbols: Reading symbol table at %RGv failed: %Rrc\n",
                 pAddrDynsym->FlatPtr, rc));
        RTMemFree(pbDynsym);
    }
    else
        Log(("dbgDiggerFreeBsdLoadSymbols: Reading symbol string table at %RGv failed: %Rrc\n",
             pAddrDynstr->FlatPtr, rc));
    RTMemFree(pbDynstr);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Process the kernel image.
 *
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   pVMM            The VMM function table.
 * @param   pszName         The image name.
 */
static void dbgDiggerFreeBsdProcessKernelImage(PDBGDIGGERFBSD pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszName)
{
    /*
     * FreeBSD has parts of the kernel ELF image in guest memory, starting with the
     * ELF header and the content of the sections which are indicated to be loaded
     * into memory (text, rodata, etc.) of course. Whats missing are the section headers
     * which is understandable but unfortunate because it would make our life easier.
     *
     * All checked FreeBSD kernels so far have the following layout in the kernel:
     *     [.interp]   - contains the /red/herring string we used for probing earlier
     *     [.hash]     - contains the hashes of the symbol names, 8 byte alignment on 64bit, 4 byte on 32bit
     *     [.gnu.hash] - GNU hash section. (introduced somewhere between 10.0 and 12.0 @todo Find out when exactly)
     *     [.dynsym]   - contains the ELF symbol descriptors, 8 byte alignment, 4 byte on 32bit
     *     [.dynstr]   - contains the symbol names as a string table, 1 byte alignmnt
     *     [.text]     - contains the executable code, 16 byte alignment.
     *
     * To find the start of the .dynsym and .dynstr sections we scan backwards from the start of the .text section
     * and check for all characters allowed for symbol names and count the amount of symbols found. When the start of the
     * .dynstr section is reached the number of entries in .dynsym is known and we can deduce the start address.
     *
     * This applied to the old code before the FreeBSD kernel introduced the .gnu.hash section
     * (keeping it here for informational pruposes):
     *     The sections are always adjacent (sans alignment) so we just parse the .hash section right after
     *     .interp, ELF states that it can contain 32bit or 64bit words but all observed kernels
     *     always use 32bit words. It contains two counters at the beginning which we can use to
     *     deduct the .hash section size and the beginning of .dynsym.
     *     .dynsym contains an array of symbol descriptors which have a fixed size depending on the
     *     guest bitness.
     *     Finding the end of .dynsym is not easily doable as there is no counter available (it lives
     *     in the section headers) at this point so we just have to check whether the record is valid
     *     and if not check if it contains an ASCII string which marks the start of the .dynstr section.
     */

#if 0
    DBGFADDRESS AddrInterpEnd = pThis->AddrKernelInterp;
    DBGFR3AddrAdd(&AddrInterpEnd, sizeof(g_abNeedleInterp));

    DBGFADDRESS AddrCur = pThis->AddrKernelText;
    int rc = VINF_SUCCESS;
    uint32_t cSymbols = 0;
    size_t cbKernel = 512 * _1M;
    RTGCUINTPTR uKernelStart = pThis->AddrKernelElfStart.FlatPtr;
    FBSDPROBESTATE enmState = FBSDPROBESTATE_DYNSTR_END; /* Start searching for the end of the .dynstr section. */

    while (AddrCur.FlatPtr > AddrInterpEnd.FlatPtr)
    {
        char achBuf[_16K];
        size_t cbToRead = RT_MIN(sizeof(achBuf), AddrCur.FlatPtr - AddrInterpEnd.FlatPtr);

        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrSub(&AddrCur, cbToRead), &achBuf[0], cbToRead);
        if (RT_FAILURE(rc))
            break;

        for (unsigned i = cbToRead; i > 0; i--)
        {
            char ch = achBuf[i - 1];

            switch (enmState)
            {
                case FBSDPROBESTATE_DYNSTR_END:
                {
                    if (ch != '\0')
                        enmState = FBSDPROBESTATE_DYNSTR_SYM_CHAR;
                    break;
                }
                case FBSDPROBESTATE_DYNSTR_SYM_TERMINATOR:
                {
                    if (   RT_C_IS_ALNUM(ch)
                        || ch == '_'
                        || ch == '.')
                        enmState = FBSDPROBESTATE_DYNSTR_SYM_CHAR;
                    else
                    {
                        /* Two consecutive terminator symbols mean end of .dynstr section. */
                        pVMM->pfnDBGFR3AddrAdd(&AddrCur, i);
                        DBGFADDRESS AddrDynstrStart = AddrCur;
                        DBGFADDRESS AddrDynsymStart = AddrCur;
                        pVMM->pfnDBGFR3AddrSub(&AddrDynsymStart, cSymbols * (pThis->f64Bit ? sizeof(Elf64_Sym) : sizeof(Elf64_Sym)));
                        LogFlowFunc(("Found all required section start addresses (.dynsym=%RGv cSymbols=%u, .dynstr=%RGv cb=%u)\n",
                                     AddrDynsymStart.FlatPtr, cSymbols, AddrDynstrStart.FlatPtr,
                                     pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr));
                        dbgDiggerFreeBsdLoadSymbols(pThis, pUVM, pVMM, pszName, uKernelStart, cbKernel,
                                                    &AddrDynsymStart, cSymbols, &AddrDynstrStart,
                                                    pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr);
                        return;
                    }
                    break;
                }
                case FBSDPROBESTATE_DYNSTR_SYM_CHAR:
                {
                    if (   !RT_C_IS_ALNUM(ch)
                        && ch != '_'
                        && ch != '.')
                    {
                        /* Non symbol character. */
                        if (ch == '\0')
                        {
                            enmState = FBSDPROBESTATE_DYNSTR_SYM_TERMINATOR;
                            cSymbols++;
                        }
                        else
                        {
                            /* Indicates the end of the .dynstr section. */
                            pVMM->pfnDBGFR3AddrAdd(&AddrCur, i);
                            DBGFADDRESS AddrDynstrStart = AddrCur;
                            DBGFADDRESS AddrDynsymStart = AddrCur;
                            pVMM->pfnDBGFR3AddrSub(&AddrDynsymStart, cSymbols * (pThis->f64Bit ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym)));
                            LogFlowFunc(("Found all required section start addresses (.dynsym=%RGv cSymbols=%u, .dynstr=%RGv cb=%u)\n",
                                         AddrDynsymStart.FlatPtr, cSymbols, AddrDynstrStart.FlatPtr,
                                         pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr));
                            dbgDiggerFreeBsdLoadSymbols(pThis, pUVM, pVMM, pszName, uKernelStart, cbKernel,
                                                        &AddrDynsymStart, cSymbols, &AddrDynstrStart,
                                                        pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr);
                            return;
                        }
                    }
                    break;
                }
                default:
                    AssertFailedBreak();
            }
        }
    }

    LogFlow(("Failed to find valid .dynsym and .dynstr sections (%Rrc), can't load kernel symbols\n", rc));
#else
    /* Calculate the start of the .hash section. */
    DBGFADDRESS AddrHashStart = pThis->AddrKernelInterp;
    pVMM->pfnDBGFR3AddrAdd(&AddrHashStart, sizeof(g_abNeedleInterp));
    AddrHashStart.FlatPtr = RT_ALIGN_GCPT(AddrHashStart.FlatPtr, pThis->f64Bit ? 8 : 4, RTGCUINTPTR);
    uint32_t au32Counters[2];
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrHashStart, &au32Counters[0], sizeof(au32Counters));
    if (RT_SUCCESS(rc))
    {
        size_t cbHash = (au32Counters[0] + au32Counters[1] + 2) * sizeof(uint32_t);
        if (AddrHashStart.FlatPtr + cbHash < pThis->AddrKernelText.FlatPtr) /* Should be much smaller */
        {
            DBGFADDRESS AddrDynsymStart = AddrHashStart;
            uint32_t cSymbols = 0;
            size_t cbKernel = 0;
            RTGCUINTPTR uKernelStart = pThis->AddrKernelElfStart.FlatPtr;

            pVMM->pfnDBGFR3AddrAdd(&AddrDynsymStart, cbHash);
            AddrDynsymStart.FlatPtr = RT_ALIGN_GCPT(AddrDynsymStart.FlatPtr, pThis->f64Bit ? 8 : 4, RTGCUINTPTR);

            DBGFADDRESS AddrDynstrStart = AddrDynsymStart;
            while (AddrDynstrStart.FlatPtr < pThis->AddrKernelText.FlatPtr)
            {
                size_t cbDynSymEnt = pThis->f64Bit ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym);
                uint8_t abBuf[_16K];
                size_t cbToRead = RT_MIN(sizeof(abBuf), pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr);

                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrDynstrStart, &abBuf[0], cbToRead);
                if (RT_FAILURE(rc))
                    break;

                for (unsigned i = 0; i < cbToRead / cbDynSymEnt; i++)
                {
                    PCELFSYMS   pSym      = (PCELFSYMS)&abBuf[i * cbDynSymEnt];
                    uint32_t    idxSymStr = FBSD_UNION(pThis, pSym, st_name);
                    uint8_t     uType     = FBSD_UNION(pThis, pSym, st_info);
                    RTGCUINTPTR AddrVal   = FBSD_UNION(pThis, pSym, st_value);
                    size_t      cbSymVal  = FBSD_UNION(pThis, pSym, st_size);

                    /*
                     * If the entry doesn't look valid check whether it contains an ASCII string,
                     * we then found the start of the .dynstr section.
                     */
                    RT_NOREF(uType);
                    if (   ELF32_ST_TYPE(uType) != STT_NOTYPE
                        && (   !FBSD_VALID_ADDRESS(pThis, AddrVal)
                            || cbSymVal > FBSD_MAX_KERNEL_SIZE
                            || idxSymStr > pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr))
                    {
                        LogFlowFunc(("Invalid symbol table entry found at %RGv\n",
                                     AddrDynstrStart.FlatPtr + i * cbDynSymEnt));

                        uint8_t *pbBuf = &abBuf[i * cbDynSymEnt];
                        size_t cbLeft = cbToRead - i * cbDynSymEnt;
                        /*
                         * Check to the end of the buffer whether it contains only a certain set of
                         * ASCII characters and 0 terminators.
                         */
                        while (   cbLeft > 0
                               && (   RT_C_IS_ALNUM(*pbBuf)
                                   || *pbBuf == '_'
                                   || *pbBuf == '\0'
                                   || *pbBuf == '.'))
                        {
                            cbLeft--;
                            pbBuf++;
                        }

                        if (!cbLeft)
                        {
                            pVMM->pfnDBGFR3AddrAdd(&AddrDynstrStart, i * cbDynSymEnt);
                            LogFlowFunc(("Found all required section start addresses (.dynsym=%RGv cSymbols=%u, .dynstr=%RGv cb=%u)\n",
                                         AddrDynsymStart.FlatPtr, cSymbols, AddrDynstrStart.FlatPtr,
                                         pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr));
                            dbgDiggerFreeBsdLoadSymbols(pThis, pUVM, pVMM, pszName, uKernelStart, cbKernel,
                                                        &AddrDynsymStart, cSymbols, &AddrDynstrStart,
                                                        pThis->AddrKernelText.FlatPtr - AddrDynstrStart.FlatPtr);
                            return;
                        }
                        else
                            LogFlowFunc(("Found invalid ASCII character in .dynstr section candidate: %#x\n", *pbBuf));
                    }
                    else
                    {
                        cSymbols++;
                        if (   ELF32_ST_TYPE(uType) != STT_NOTYPE
                            && FBSD_VALID_ADDRESS(pThis, AddrVal))
                        {
                            uKernelStart = RT_MIN(uKernelStart, AddrVal);
                            cbKernel = RT_MAX(cbKernel, AddrVal + cbSymVal - uKernelStart);
                        }
                    }
                }

                /* Don't account incomplete entries. */
                pVMM->pfnDBGFR3AddrAdd(&AddrDynstrStart, (cbToRead / cbDynSymEnt) * cbDynSymEnt);
            }
        }
        else
            LogFlowFunc((".hash section overlaps with .text section: %zu (expected much less than %u)\n", cbHash,
                         pThis->AddrKernelText.FlatPtr - AddrHashStart.FlatPtr));
    }
#endif
}


/**
 * @interface_method_impl{DBGFOSIDMESG,pfnQueryKernelLog}
 */
static DECLCALLBACK(int) dbgDiggerFreeBsdIDmsg_QueryKernelLog(PDBGFOSIDMESG pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, uint32_t fFlags,
                                                              uint32_t cMessages, char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    PDBGDIGGERFBSD pData = RT_FROM_MEMBER(pThis, DBGDIGGERFBSD, IDmesg);
    RT_NOREF(fFlags);

    if (cMessages < 1)
        return VERR_INVALID_PARAMETER;

    /* Resolve the message buffer address from the msgbufp symbol. */
    RTDBGSYMBOL SymInfo;
    int rc = pVMM->pfnDBGFR3AsSymbolByName(pUVM, DBGF_AS_KERNEL, "kernel!msgbufp", &SymInfo, NULL);
    if (RT_SUCCESS(rc))
    {
        DBGFADDRESS AddrMsgBuf;

        /* Read the message buffer pointer. */
        RTGCPTR     GCPtrMsgBufP = 0;
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrMsgBuf, SymInfo.Value),
                                    &GCPtrMsgBufP, pData->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t));
        if (RT_FAILURE(rc))
        {
            Log(("dbgDiggerFreeBsdIDmsg_QueryKernelLog: failed to read msgbufp at %RGv: %Rrc\n", AddrMsgBuf.FlatPtr, rc));
            return VERR_NOT_FOUND;
        }
        if (!FBSD_VALID_ADDRESS(pData, GCPtrMsgBufP))
        {
            Log(("dbgDiggerFreeBsdIDmsg_QueryKernelLog: Invalid address for msgbufp: %RGv\n", GCPtrMsgBufP));
            return VERR_NOT_FOUND;
        }

        /* Read the structure. */
        FBSDMSGBUF MsgBuf;
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrMsgBuf, GCPtrMsgBufP),
                                    &MsgBuf, sizeof(MsgBuf));
        if (RT_SUCCESS(rc))
        {
            RTGCUINTPTR AddrBuf = FBSD_UNION(pData, &MsgBuf, msg_ptr);
            uint32_t cbMsgBuf = FBSD_UNION(pData, &MsgBuf, msg_size);
            uint32_t uMsgBufSeqR = FBSD_UNION(pData, &MsgBuf, msg_rseq);
            uint32_t uMsgBufSeqW = FBSD_UNION(pData, &MsgBuf, msg_wseq);

            /*
             * Validate the structure.
             */
            if (   FBSD_UNION(pData, &MsgBuf, msg_magic) != FBSD_MSGBUF_MAGIC
                || cbMsgBuf < UINT32_C(4096)
                || cbMsgBuf > 16*_1M
                || FBSD_UNION(pData, &MsgBuf, msg_rseq) > cbMsgBuf
                || FBSD_UNION(pData, &MsgBuf, msg_wseq) > cbMsgBuf
                || !FBSD_VALID_ADDRESS(pData, AddrBuf) )
            {
                Log(("dbgDiggerFreeBsdIDmsg_QueryKernelLog: Invalid MsgBuf data: msg_magic=%#x msg_size=%#x msg_rseq=%#x msg_wseq=%#x msg_ptr=%RGv\n",
                     FBSD_UNION(pData, &MsgBuf, msg_magic), cbMsgBuf, uMsgBufSeqR, uMsgBufSeqW, AddrBuf));
                return VERR_INVALID_STATE;
            }

            /*
             * Read the buffer.
             */
            char *pchMsgBuf = (char *)RTMemAlloc(cbMsgBuf);
            if (!pchMsgBuf)
            {
                Log(("dbgDiggerFreeBsdIDmsg_QueryKernelLog: Failed to allocate %#x bytes of memory for the log buffer\n",
                     cbMsgBuf));
                return VERR_INVALID_STATE;
            }
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrMsgBuf, AddrBuf),
                                        pchMsgBuf, cbMsgBuf);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Copy it out raw.
                 */
                uint32_t offDst = 0;
                if (uMsgBufSeqR < uMsgBufSeqW)
                {
                    /* Single chunk between the read and write offsets. */
                    uint32_t cbToCopy = uMsgBufSeqW - uMsgBufSeqR;
                    if (cbToCopy < cbBuf)
                    {
                        memcpy(pszBuf, &pchMsgBuf[uMsgBufSeqR], cbToCopy);
                        pszBuf[cbToCopy] = '\0';
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        if (cbBuf)
                        {
                            memcpy(pszBuf, &pchMsgBuf[uMsgBufSeqR], cbBuf - 1);
                            pszBuf[cbBuf - 1] = '\0';
                        }
                        rc = VERR_BUFFER_OVERFLOW;
                    }
                    offDst = cbToCopy + 1;
                }
                else
                {
                    /* Two chunks, read offset to end, start to write offset. */
                    uint32_t cbFirst  = cbMsgBuf - uMsgBufSeqR;
                    uint32_t cbSecond = uMsgBufSeqW;
                    if (cbFirst + cbSecond < cbBuf)
                    {
                        memcpy(pszBuf, &pchMsgBuf[uMsgBufSeqR], cbFirst);
                        memcpy(&pszBuf[cbFirst], pchMsgBuf, cbSecond);
                        offDst = cbFirst + cbSecond;
                        pszBuf[offDst++] = '\0';
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        offDst = cbFirst + cbSecond + 1;
                        if (cbFirst < cbBuf)
                        {
                            memcpy(pszBuf, &pchMsgBuf[uMsgBufSeqR], cbFirst);
                            memcpy(&pszBuf[cbFirst], pchMsgBuf, cbBuf - cbFirst);
                            pszBuf[cbBuf - 1] = '\0';
                        }
                        else if (cbBuf)
                        {
                            memcpy(pszBuf, &pchMsgBuf[uMsgBufSeqR], cbBuf - 1);
                            pszBuf[cbBuf - 1] = '\0';
                        }
                        rc = VERR_BUFFER_OVERFLOW;
                    }
                }

                if (pcbActual)
                    *pcbActual = offDst;
            }
            else
                Log(("dbgDiggerFreeBsdIDmsg_QueryKernelLog: Error reading %#x bytes at %RGv: %Rrc\n", cbBuf, AddrBuf, rc));
            RTMemFree(pchMsgBuf);
        }
        else
            LogFlowFunc(("Failed to read message buffer header: %Rrc\n", rc));
    }

    return rc;
}


/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerFreeBsdStackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                           PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState,
                                                           PCCPUMCTX pInitialCtx, RTDBGAS hAs, uint64_t *puScratch)
{
    RT_NOREF(pUVM, pVMM, pvData, idCpu, pFrame, pState, pInitialCtx, hAs, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerFreeBsdQueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    RT_NOREF(pUVM, pVMM);

    switch (enmIf)
    {
        case DBGFOSINTERFACE_DMESG:
            return &pThis->IDmesg;

        default:
            return NULL;
    }
}


/**
 * @copydoc DBGFOSREG::pfnQueryVersion
 */
static DECLCALLBACK(int)  dbgDiggerFreeBsdQueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                       char *pszVersion, size_t cchVersion)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    Assert(pThis->fValid); RT_NOREF(pThis);

    RTDBGSYMBOL SymInfo;
    int rc = pVMM->pfnDBGFR3AsSymbolByName(pUVM, DBGF_AS_KERNEL, "kernel!version", &SymInfo, NULL);
    if (RT_SUCCESS(rc))
    {
        DBGFADDRESS AddrVersion;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrVersion, SymInfo.Value);

        rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &AddrVersion, pszVersion, cchVersion);
        if (RT_SUCCESS(rc))
        {
            char *pszEnd = RTStrEnd(pszVersion, cchVersion);
            AssertReturn(pszEnd, VERR_BUFFER_OVERFLOW);
            while (     pszEnd > pszVersion
                   &&   RT_C_IS_SPACE(pszEnd[-1]))
                pszEnd--;
            *pszEnd = '\0';
        }
        else
            RTStrPrintf(pszVersion, cchVersion, "DBGFR3MemReadString -> %Rrc", rc);
    }

    return rc;
}



/**
 * @copydoc DBGFOSREG::pfnTerm
 */
static DECLCALLBACK(void)  dbgDiggerFreeBsdTerm(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    Assert(pThis->fValid);
    RT_NOREF(pUVM, pVMM);

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerFreeBsdRefresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    NOREF(pThis);
    Assert(pThis->fValid);

    dbgDiggerFreeBsdTerm(pUVM, pVMM, pvData);
    return dbgDiggerFreeBsdInit(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerFreeBsdInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    Assert(!pThis->fValid);

    RT_NOREF1(pUVM);

    dbgDiggerFreeBsdProcessKernelImage(pThis, pUVM, pVMM, "kernel");
    pThis->fValid = true;
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerFreeBsdProbe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;

    /*
     * Look for the magic ELF header near the known start addresses.
     * If one is found look for the magic "/red/herring" string which is in the
     * "interp" section not far away and then validate the start of the ELF header
     * to be sure.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_au64FreeBsdKernelAddresses); i++)
    {
        static const uint8_t s_abNeedle[] = ELFMAG;
        DBGFADDRESS KernelAddr;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &KernelAddr, g_au64FreeBsdKernelAddresses[i]);
        DBGFADDRESS HitAddr;
        uint32_t    cbLeft  = FBSD_MAX_KERNEL_SIZE;

        while (cbLeft > X86_PAGE_4K_SIZE)
        {
            int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &KernelAddr, cbLeft, 1,
                                            s_abNeedle, sizeof(s_abNeedle) - 1, &HitAddr);
            if (RT_FAILURE(rc))
                break;

            /*
             * Look for the magic "/red/herring" near the header and verify the basic
             * ELF header.
             */
            DBGFADDRESS HitAddrInterp;
            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &HitAddr, FBSD_MAX_INTERP_OFFSET, 1,
                                        g_abNeedleInterp, sizeof(g_abNeedleInterp), &HitAddrInterp);
            if (RT_SUCCESS(rc))
            {
                union
                {
                    uint8_t    ab[2 * X86_PAGE_4K_SIZE];
                    Elf32_Ehdr Hdr32;
                    Elf64_Ehdr Hdr64;
                } ElfHdr;
                AssertCompileMembersSameSizeAndOffset(Elf64_Ehdr, e_ident,   Elf32_Ehdr, e_ident);
                AssertCompileMembersSameSizeAndOffset(Elf64_Ehdr, e_type,    Elf32_Ehdr, e_type);
                AssertCompileMembersSameSizeAndOffset(Elf64_Ehdr, e_machine, Elf32_Ehdr, e_machine);
                AssertCompileMembersSameSizeAndOffset(Elf64_Ehdr, e_version, Elf32_Ehdr, e_version);

                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &HitAddr, &ElfHdr.ab[0], X86_PAGE_4K_SIZE);
                if (RT_SUCCESS(rc))
                {
                    /* We verified the magic above already by scanning for it. */
                    if (   (   ElfHdr.Hdr32.e_ident[EI_CLASS] == ELFCLASS32
                            || ElfHdr.Hdr32.e_ident[EI_CLASS] == ELFCLASS64)
                        && ElfHdr.Hdr32.e_ident[EI_DATA] == ELFDATA2LSB
                        && ElfHdr.Hdr32.e_ident[EI_VERSION] == EV_CURRENT
                        && ElfHdr.Hdr32.e_ident[EI_OSABI] == ELFOSABI_FREEBSD
                        && ElfHdr.Hdr32.e_type == ET_EXEC
                        && (   ElfHdr.Hdr32.e_machine == EM_386
                            || ElfHdr.Hdr32.e_machine == EM_X86_64)
                        && ElfHdr.Hdr32.e_version == EV_CURRENT)
                    {
                        pThis->f64Bit = ElfHdr.Hdr32.e_ident[EI_CLASS] == ELFCLASS64;
                        pThis->AddrKernelElfStart = HitAddr;
                        pThis->AddrKernelInterp = HitAddrInterp;
                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &pThis->AddrKernelText, FBSD_UNION(pThis, &ElfHdr, e_entry));
                        LogFunc(("Found %s FreeBSD kernel at %RGv (.interp section at %RGv, .text section at %RGv)\n",
                                 pThis->f64Bit ? "amd64" : "i386", pThis->AddrKernelElfStart.FlatPtr,
                                 pThis->AddrKernelInterp.FlatPtr, pThis->AddrKernelText.FlatPtr));
                        return true;
                    }
                }
            }

            /*
             * Advance.
             */
            RTGCUINTPTR cbDistance = HitAddr.FlatPtr - KernelAddr.FlatPtr + sizeof(s_abNeedle) - 1;
            if (RT_UNLIKELY(cbDistance >= cbLeft))
                break;

            cbLeft -= cbDistance;
            pVMM->pfnDBGFR3AddrAdd(&KernelAddr, cbDistance);
        }
    }
    return false;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerFreeBsdDestruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerFreeBsdConstruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERFBSD pThis = (PDBGDIGGERFBSD)pvData;
    RT_NOREF(pUVM, pVMM);

    pThis->fValid = false;
    pThis->f64Bit = false;
    pThis->IDmesg.u32Magic = DBGFOSIDMESG_MAGIC;
    pThis->IDmesg.pfnQueryKernelLog = dbgDiggerFreeBsdIDmsg_QueryKernelLog;
    pThis->IDmesg.u32EndMagic = DBGFOSIDMESG_MAGIC;

    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerFreeBsd =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGERFBSD),
    /* .szName = */                 "FreeBSD",
    /* .pfnConstruct = */           dbgDiggerFreeBsdConstruct,
    /* .pfnDestruct = */            dbgDiggerFreeBsdDestruct,
    /* .pfnProbe = */               dbgDiggerFreeBsdProbe,
    /* .pfnInit = */                dbgDiggerFreeBsdInit,
    /* .pfnRefresh = */             dbgDiggerFreeBsdRefresh,
    /* .pfnTerm = */                dbgDiggerFreeBsdTerm,
    /* .pfnQueryVersion = */        dbgDiggerFreeBsdQueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerFreeBsdQueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerFreeBsdStackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};

