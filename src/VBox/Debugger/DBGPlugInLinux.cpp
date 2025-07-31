/* $Id: DBGPlugInLinux.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * DBGPlugInLinux - Debugger and Guest OS Digger Plugin For Linux.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/dis.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name InternalLinux structures
 * @{ */


/** @} */


/**
 * Config item type.
 */
typedef enum DBGDIGGERLINUXCFGITEMTYPE
{
    /** Invalid type. */
    DBGDIGGERLINUXCFGITEMTYPE_INVALID = 0,
    /** String. */
    DBGDIGGERLINUXCFGITEMTYPE_STRING,
    /** Number. */
    DBGDIGGERLINUXCFGITEMTYPE_NUMBER,
    /** Flag whether this feature is included in the
     * kernel or as a module. */
    DBGDIGGERLINUXCFGITEMTYPE_FLAG
} DBGDIGGERLINUXCFGITEMTYPE;

/**
 * Item in the config database.
 */
typedef struct DBGDIGGERLINUXCFGITEM
{
    /** String space core. */
    RTSTRSPACECORE            Core;
    /** Config item type. */
    DBGDIGGERLINUXCFGITEMTYPE enmType;
    /** Data based on the type. */
    union
    {
        /** Number. */
        int64_t               i64Num;
        /** Flag. */
        bool                  fModule;
        /** String - variable in size. */
        char                  aszString[1];
    } u;
} DBGDIGGERLINUXCFGITEM;
/** Pointer to a config database item. */
typedef DBGDIGGERLINUXCFGITEM *PDBGDIGGERLINUXCFGITEM;
/** Pointer to a const config database item. */
typedef const DBGDIGGERLINUXCFGITEM *PCDBGDIGGERLINUXCFGITEM;

/**
 * Linux guest OS digger instance data.
 */
typedef struct DBGDIGGERLINUX
{
    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool fValid;
    /** Set if 64-bit, clear if 32-bit.  */
    bool f64Bit;
    /** Set if the kallsyms table uses relative addressing, clear
     * if absolute addresses are used. */
    bool fRelKrnlAddr;
    /** The relative base when kernel symbols use offsets rather than
     * absolute addresses. */
    RTGCUINTPTR uKernelRelativeBase;
    /** The guest kernel version used for version comparisons. */
    uint32_t    uKrnlVer;
    /** The guest kernel major version. */
    uint32_t    uKrnlVerMaj;
    /** The guest kernel minor version. */
    uint32_t    uKrnlVerMin;
    /** The guest kernel build version. */
    uint32_t    uKrnlVerBld;

    /** The address of the linux banner.
     * This is set during probing. */
    DBGFADDRESS AddrLinuxBanner;
    /** Kernel base address.
     * This is set during probing, refined during kallsyms parsing. */
    DBGFADDRESS AddrKernelBase;
    /** The kernel size.   */
    uint32_t    cbKernel;

    /** The number of kernel symbols (kallsyms_num_syms).
     * This is set during init.  */
    uint32_t   cKernelSymbols;
    /** The size of the kernel name table (sizeof(kallsyms_names)).   */
    uint32_t   cbKernelNames;
    /** Number of entries in the kernel_markers table. */
    uint32_t   cKernelNameMarkers;
    /** The size of the kernel symbol token table. */
    uint32_t   cbKernelTokenTable;
    /** The address of the encoded kernel symbol names (kallsyms_names). */
    DBGFADDRESS AddrKernelNames;
    /** The address of the kernel symbol addresses (kallsyms_addresses). */
    DBGFADDRESS AddrKernelAddresses;
    /** The address of the kernel symbol name markers (kallsyms_markers). */
    DBGFADDRESS AddrKernelNameMarkers;
    /** The address of the kernel symbol token table (kallsyms_token_table). */
    DBGFADDRESS AddrKernelTokenTable;
    /** The address of the kernel symbol token index table (kallsyms_token_index). */
    DBGFADDRESS AddrKernelTokenIndex;

    /** The kernel message log interface. */
    DBGFOSIDMESG    IDmesg;

    /** The config database root. */
    RTSTRSPACE      hCfgDb;
} DBGDIGGERLINUX;
/** Pointer to the linux guest OS digger instance data. */
typedef DBGDIGGERLINUX *PDBGDIGGERLINUX;


/**
 * The current printk_log structure.
 */
typedef struct LNXPRINTKHDR
{
    /** Monotonic timestamp. */
    uint64_t nsTimestamp;
    /** The total size of this message record. */
    uint16_t cbTotal;
    /** The size of the text part (immediately follows the header). */
    uint16_t cbText;
    /** The size of the optional dictionary part (follows the text). */
    uint16_t cbDict;
    /** The syslog facility number. */
    uint8_t  bFacility;
    /** First 5 bits are internal flags, next 3 bits are log level. */
    uint8_t  fFlagsAndLevel;
} LNXPRINTKHDR;
AssertCompileSize(LNXPRINTKHDR, 2*sizeof(uint64_t));
/** Pointer to linux printk_log header. */
typedef LNXPRINTKHDR *PLNXPRINTKHDR;
/** Pointer to linux const printk_log header. */
typedef LNXPRINTKHDR const *PCLNXPRINTKHDR;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** First kernel map address for 32bit Linux hosts (__START_KERNEL_map). */
#define LNX32_KERNEL_ADDRESS_START      UINT32_C(0xc0000000)
/** First kernel map address for 64bit Linux hosts (__START_KERNEL_map). */
#define LNX64_KERNEL_ADDRESS_START      UINT64_C(0xffffffff80000000)
/** Validates a 32-bit linux kernel address */
#define LNX32_VALID_ADDRESS(Addr)       ((Addr) > UINT32_C(0x80000000) && (Addr) < UINT32_C(0xfffff000))
/** Validates a 64-bit linux kernel address */
#define LNX64_VALID_ADDRESS(Addr)       ((Addr) > UINT64_C(0xffff800000000000) && (Addr) < UINT64_C(0xfffffffffffff000))

/** The max kernel size. */
#define LNX_MAX_KERNEL_SIZE                 UINT32_C(0x0f000000)
/** Maximum kernel log buffer size. */
#define LNX_MAX_KERNEL_LOG_SIZE             (16 * _1M)

/** The maximum size we expect for kallsyms_names. */
#define LNX_MAX_KALLSYMS_NAMES_SIZE         UINT32_C(0x200000)
/** The maximum size we expect for kallsyms_token_table. */
#define LNX_MAX_KALLSYMS_TOKEN_TABLE_SIZE   UINT32_C(0x10000)
/** The minimum number of symbols we expect in kallsyms_num_syms. */
#define LNX_MIN_KALLSYMS_SYMBOLS            UINT32_C(2048)
/** The maximum number of symbols we expect in kallsyms_num_syms. */
#define LNX_MAX_KALLSYMS_SYMBOLS            UINT32_C(1048576)
/** The min length an encoded symbol in kallsyms_names is expected to have. */
#define LNX_MIN_KALLSYMS_ENC_LENGTH         UINT8_C(1)
/** The max length an encoded symbol in kallsyms_names is expected to have.
 * @todo check real life here.  */
#define LNX_MAX_KALLSYMS_ENC_LENGTH         UINT8_C(28)
/** The approximate maximum length of a string token. */
#define LNX_MAX_KALLSYMS_TOKEN_LEN          UINT16_C(32)
/** Maximum compressed config size expected. */
#define LNX_MAX_COMPRESSED_CFG_SIZE         _1M

/** Module tag for linux ('linuxmod' on little endian ASCII systems). */
#define DIG_LNX_MOD_TAG                     UINT64_C(0x545f5d78758e898c)
/** Macro for building a Linux kernel version which can be used for comparisons. */
#define LNX_MK_VER(major, minor, build)     (((major) << 22) | ((minor) << 12) | (build))


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerLinuxInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Table of common linux kernel addresses. */
static uint64_t g_au64LnxKernelAddresses[] =
{
    UINT64_C(0xc0100000),
    UINT64_C(0x90100000),
    UINT64_C(0xffffffff80200000)
};

static const uint8_t g_abLinuxVersion[] = "Linux version ";
/** The needle for searching for the kernel log area (the value is observed in pretty much all 32bit and 64bit x86 kernels).
 * This needle should appear only once in the memory due to the address being filled in by a format string. */
static const uint8_t g_abKrnlLogNeedle[] = "BIOS-e820: [mem 0x0000000000000000";


/**
 * Tries to resolve the kernel log buffer start and end by searching for needle.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pGCPtrLogBuf        Where to store the start of the kernel log buffer on success.
 * @param   pcbLogBuf           Where to store the size of the kernel log buffer on success.
 */
static int dbgDiggerLinuxKrnlLogBufFindByNeedle(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                RTGCPTR *pGCPtrLogBuf, uint32_t *pcbLogBuf)
{
    int rc = VINF_SUCCESS;

    /* Try to find the needle, it should be very early in the kernel log buffer. */
    DBGFADDRESS AddrScan;
    DBGFADDRESS AddrHit;
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrScan, pThis->f64Bit ? LNX64_KERNEL_ADDRESS_START : LNX32_KERNEL_ADDRESS_START);

    rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &AddrScan, ~(RTGCUINTPTR)0, 1 /*uAlign*/,
                                g_abKrnlLogNeedle, sizeof(g_abKrnlLogNeedle) - 1, &AddrHit);
    if (RT_SUCCESS(rc))
    {
        uint32_t cbLogBuf = 0;
        uint64_t tsLastNs = 0;
        DBGFADDRESS AddrCur;

        pVMM->pfnDBGFR3AddrSub(&AddrHit, sizeof(LNXPRINTKHDR));
        AddrCur = AddrHit;

        /* Try to find the end of the kernel log buffer. */
        for (;;)
        {
            if (cbLogBuf >= LNX_MAX_KERNEL_LOG_SIZE)
                break;

            LNXPRINTKHDR Hdr;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrCur, &Hdr, sizeof(Hdr));
            if (RT_SUCCESS(rc))
            {
                uint32_t const cbLogAlign = 4;

                /*
                 * If the header does not look valid anymore we stop.
                 * Timestamps are monotonically increasing.
                 */
                if (   !Hdr.cbTotal /* Zero entry size means there is no record anymore, doesn't make sense to look futher. */
                    || Hdr.cbText + Hdr.cbDict + sizeof(Hdr) > Hdr.cbTotal
                    || (Hdr.cbTotal & (cbLogAlign - 1)) != 0
                    || tsLastNs > Hdr.nsTimestamp)
                    break;

                /** @todo Maybe read text part and verify it is all ASCII. */

                cbLogBuf += Hdr.cbTotal;
                pVMM->pfnDBGFR3AddrAdd(&AddrCur, Hdr.cbTotal);
            }

            if (RT_FAILURE(rc))
                break;
        }

        /** @todo Go back to find the start address of the kernel log (or we loose potential kernel log messages). */

        if (   RT_SUCCESS(rc)
            && cbLogBuf)
        {
            /* Align log buffer size to a power of two. */
            uint32_t idxBitLast = ASMBitLastSetU32(cbLogBuf);
            idxBitLast--; /* There is at least one bit set, see check above. */

            if (cbLogBuf & (RT_BIT_32(idxBitLast) - 1))
                idxBitLast++;

            *pGCPtrLogBuf = AddrHit.FlatPtr;
            *pcbLogBuf    = RT_MIN(RT_BIT_32(idxBitLast), LNX_MAX_KERNEL_LOG_SIZE);
        }
        else if (RT_SUCCESS(rc))
            rc = VERR_NOT_FOUND;
    }

    return rc;
}


/**
 * Converts a given offset into an absolute address if relative kernel offsets are used for
 * kallsyms.
 *
 * @returns The absolute kernel address.
 * @param   pThis               The Linux digger data.
 * @param   uOffset             The offset to convert.
 */
DECLINLINE(RTGCUINTPTR) dbgDiggerLinuxConvOffsetToAddr(PDBGDIGGERLINUX pThis, int32_t uOffset)
{
    RTGCUINTPTR uAddr;

    /*
     * How the absolute address is calculated from the offset depends on the
     * CONFIG_KALLSYMS_ABSOLUTE_PERCPU config which is only set for 64bit
     * SMP kernels (we assume that all 64bit kernels always have SMP enabled too).
     */
    if (pThis->f64Bit)
    {
        if (uOffset >= 0)
            uAddr = uOffset;
        else
            uAddr = pThis->uKernelRelativeBase - 1 - uOffset;
    }
    else
        uAddr = pThis->uKernelRelativeBase + (uint32_t)uOffset;

    return uAddr;
}

/**
 * Disassembles a simple getter returning the value for it.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM function table.
 * @param   hMod                The module to use.
 * @param   pszSymbol           The symbol of the getter.
 * @param   pvVal               Where to store the value on success.
 * @param   cbVal               Size of the value in bytes.
 */
static int dbgDiggerLinuxDisassembleSimpleGetter(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hMod,
                                                 const char *pszSymbol, void *pvVal, uint32_t cbVal)
{
    int rc = VINF_SUCCESS;

    RTDBGSYMBOL SymInfo;
    rc = RTDbgModSymbolByName(hMod, pszSymbol, &SymInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do the diassembling. Disassemble until a ret instruction is encountered
         * or a limit is reached (don't want to disassemble for too long as the getter
         * should be short).
         * push and pop instructions are skipped as well as any mov instructions not
         * touching the rax or eax register (depending on the size of the value).
         */
        unsigned cInstrDisassembled = 0;
        uint32_t offInstr = 0;
        bool fRet = false;
        DISSTATE DisState;
        RT_ZERO(DisState);

        do
        {
            DBGFADDRESS Addr;
            RTGCPTR GCPtrCur = (RTGCPTR)SymInfo.Value + pThis->AddrKernelBase.FlatPtr + offInstr;
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, GCPtrCur);

            /* Prefetch the instruction. */
            uint8_t abInstr[32];
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &abInstr[0], sizeof(abInstr));
            if (RT_SUCCESS(rc))
            {
                uint32_t cbInstr = 0;

                rc = DISInstr(&abInstr[0], pThis->f64Bit ? DISCPUMODE_64BIT : DISCPUMODE_32BIT, &DisState, &cbInstr);
                if (RT_SUCCESS(rc))
                {
                    switch (DisState.pCurInstr->uOpcode)
                    {
                        case OP_PUSH:
                        case OP_POP:
                        case OP_NOP:
                        case OP_LEA:
                            break;
                        case OP_RETN:
                            /* Getter returned, abort disassembling. */
                            fRet = true;
                            break;
                        case OP_MOV:
                            /*
                             * Check that the destination is either rax or eax depending on the
                             * value size.
                             *
                             * aParams[0] is the destination and aParams[1] the source.
                             */
                            if (   (   (   (DisState.aParams[0].fUse & (DISUSE_BASE | DISUSE_REG_GEN32))
                                        && cbVal == sizeof(uint32_t))
                                    || (    (DisState.aParams[0].fUse & (DISUSE_BASE | DISUSE_REG_GEN64))
                                         && cbVal == sizeof(uint64_t)))
                                && DisState.aParams[0].x86.Base.idxGenReg == DISGREG_RAX)
                            {
                                /* Parse the source. */
                                if (DisState.aParams[1].fUse & (DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64))
                                    memcpy(pvVal, &DisState.aParams[1].uValue, cbVal);
                                else if (DisState.aParams[1].fUse & (DISUSE_RIPDISPLACEMENT32|DISUSE_DISPLACEMENT32|DISUSE_DISPLACEMENT64))
                                {
                                    RTGCPTR GCPtrVal = 0;

                                    if (DisState.aParams[1].fUse & DISUSE_RIPDISPLACEMENT32)
                                        GCPtrVal = GCPtrCur + DisState.aParams[1].x86.uDisp.i32 + cbInstr;
                                    else if (DisState.aParams[1].fUse & DISUSE_DISPLACEMENT32)
                                        GCPtrVal = (RTGCPTR)DisState.aParams[1].x86.uDisp.u32;
                                    else if (DisState.aParams[1].fUse & DISUSE_DISPLACEMENT64)
                                        GCPtrVal = (RTGCPTR)DisState.aParams[1].x86.uDisp.u64;
                                    else
                                        AssertMsgFailedBreakStmt(("Invalid displacement\n"), rc = VERR_INVALID_STATE);

                                    DBGFADDRESS AddrVal;
                                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrVal, GCPtrVal),
                                                                pvVal, cbVal);
                                }
                            }
                            break;
                        default:
                            /* All other instructions will cause an error for now (playing safe here). */
                            rc = VERR_INVALID_PARAMETER;
                            break;
                    }
                    cInstrDisassembled++;
                    offInstr += cbInstr;
                }
            }
        } while (   RT_SUCCESS(rc)
                 && cInstrDisassembled < 20
                 && !fRet);
    }

    return rc;
}

/**
 * Try to get at the log buffer starting address and size by disassembling emit_log_char.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM function table.
 * @param   hMod                The module to use.
 * @param   pGCPtrLogBuf        Where to store the log buffer pointer on success.
 * @param   pcbLogBuf           Where to store the size of the log buffer on success.
 */
static int dbgDiggerLinuxQueryAsciiLogBufferPtrs(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hMod,
                                                 RTGCPTR *pGCPtrLogBuf, uint32_t *pcbLogBuf)
{
    int rc = VINF_SUCCESS;

    /**
     * We disassemble emit_log_char to get at the log buffer address and size.
     * This is used in case the symbols are not exported in kallsyms.
     *
     * This is what it typically looks like:
     * vmlinux!emit_log_char:
     * %00000000c01204a1 56                      push esi
     * %00000000c01204a2 8b 35 d0 1c 34 c0       mov esi, dword [0c0341cd0h]
     * %00000000c01204a8 53                      push ebx
     * %00000000c01204a9 8b 1d 74 3b 3e c0       mov ebx, dword [0c03e3b74h]
     * %00000000c01204af 8b 0d d8 1c 34 c0       mov ecx, dword [0c0341cd8h]
     * %00000000c01204b5 8d 56 ff                lea edx, [esi-001h]
     * %00000000c01204b8 21 da                   and edx, ebx
     * %00000000c01204ba 88 04 11                mov byte [ecx+edx], al
     * %00000000c01204bd 8d 53 01                lea edx, [ebx+001h]
     * %00000000c01204c0 89 d0                   mov eax, edx
     * [...]
     */
    RTDBGSYMBOL SymInfo;
    rc = RTDbgModSymbolByName(hMod, "emit_log_char", &SymInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do the diassembling. Disassemble until a ret instruction is encountered
         * or a limit is reached (don't want to disassemble for too long as the getter
         * should be short). Certain instructions found are ignored (push, nop, etc.).
         */
        unsigned cInstrDisassembled = 0;
        uint32_t offInstr = 0;
        bool fRet = false;
        DISSTATE DisState;
        unsigned cAddressesUsed = 0;
        struct { size_t cb; RTGCPTR GCPtrOrigSrc; } aAddresses[5];
        RT_ZERO(DisState);
        RT_ZERO(aAddresses);

        do
        {
            DBGFADDRESS Addr;
            RTGCPTR GCPtrCur = (RTGCPTR)SymInfo.Value + pThis->AddrKernelBase.FlatPtr + offInstr;
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, GCPtrCur);

            /* Prefetch the instruction. */
            uint8_t abInstr[32];
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &abInstr[0], sizeof(abInstr));
            if (RT_SUCCESS(rc))
            {
                uint32_t cbInstr = 0;

                rc = DISInstr(&abInstr[0], pThis->f64Bit ? DISCPUMODE_64BIT : DISCPUMODE_32BIT, &DisState, &cbInstr);
                if (RT_SUCCESS(rc))
                {
                    switch (DisState.pCurInstr->uOpcode)
                    {
                        case OP_PUSH:
                        case OP_POP:
                        case OP_NOP:
                        case OP_LEA:
                        case OP_AND:
                        case OP_CBW:
                        case OP_DEC:
                            break;
                        case OP_RETN:
                            /* emit_log_char returned, abort disassembling. */
                            rc = VERR_NOT_FOUND;
                            fRet = true;
                            break;
                        case OP_MOV:
                        case OP_MOVSXD:
                            /*
                             * If a mov is encountered writing to memory with al (or dil for amd64) being the source the
                             * character is stored and we can infer the base address and size of the log buffer from
                             * the source addresses.
                             */
                            if (   (DisState.aParams[1].fUse & DISUSE_REG_GEN8)
                                && (   (DisState.aParams[1].x86.Base.idxGenReg == DISGREG_AL && !pThis->f64Bit)
                                    || (DisState.aParams[1].x86.Base.idxGenReg == DISGREG_DIL && pThis->f64Bit))
                                && DISUSE_IS_EFFECTIVE_ADDR(DisState.aParams[0].fUse))
                            {
                                RTGCPTR GCPtrLogBuf = 0;
                                uint32_t cbLogBuf = 0;

                                /*
                                 * We can stop disassembling now and inspect all registers, look for a valid kernel address first.
                                 * Only one of the accessed registers should hold a valid kernel address.
                                 * For the log size look for the biggest non kernel address.
                                 */
                                for (unsigned i = 0; i < cAddressesUsed; i++)
                                {
                                    DBGFADDRESS AddrVal;
                                    union { uint8_t abVal[8]; uint32_t u32Val; uint64_t u64Val; } Val;

                                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrVal,
                                                                                            aAddresses[i].GCPtrOrigSrc),
                                                                &Val.abVal[0], aAddresses[i].cb);
                                    if (RT_SUCCESS(rc))
                                    {
                                        if (pThis->f64Bit && aAddresses[i].cb == sizeof(uint64_t))
                                        {
                                            if (LNX64_VALID_ADDRESS(Val.u64Val))
                                            {
                                                if (GCPtrLogBuf == 0)
                                                    GCPtrLogBuf = Val.u64Val;
                                                else
                                                {
                                                    rc = VERR_NOT_FOUND;
                                                    break;
                                                }
                                            }
                                        }
                                        else
                                        {
                                            AssertMsgBreakStmt(aAddresses[i].cb == sizeof(uint32_t),
                                                               ("Invalid value size\n"), rc = VERR_INVALID_STATE);

                                            /* Might be a kernel address or a size indicator. */
                                            if (!pThis->f64Bit && LNX32_VALID_ADDRESS(Val.u32Val))
                                            {
                                                if (GCPtrLogBuf == 0)
                                                    GCPtrLogBuf = Val.u32Val;
                                                else
                                                {
                                                    rc = VERR_NOT_FOUND;
                                                    break;
                                                }
                                            }
                                            else
                                            {
                                                /*
                                                 * The highest value will be the log buffer because the other
                                                 * accessed variables are indexes into the buffer and hence
                                                 * always smaller than the size.
                                                 */
                                                if (cbLogBuf < Val.u32Val)
                                                    cbLogBuf = Val.u32Val;
                                            }
                                        }
                                    }
                                }

                                if (   RT_SUCCESS(rc)
                                    && GCPtrLogBuf != 0
                                    && cbLogBuf != 0)
                                {
                                    *pGCPtrLogBuf = GCPtrLogBuf;
                                    *pcbLogBuf = cbLogBuf;
                                }
                                else if (RT_SUCCESS(rc))
                                    rc = VERR_NOT_FOUND;

                                fRet = true;
                                break;
                            }
                            else
                            {
                                /*
                                 * In case of a memory to register move store the destination register index and the
                                 * source address in the relation table for later processing.
                                 */
                                if (   (DisState.aParams[0].fUse & (DISUSE_BASE | DISUSE_REG_GEN32 | DISUSE_REG_GEN64))
                                    && (DisState.aParams[1].x86.cb == sizeof(uint32_t) || DisState.aParams[1].x86.cb == sizeof(uint64_t))
                                    && (DisState.aParams[1].fUse & (DISUSE_RIPDISPLACEMENT32|DISUSE_DISPLACEMENT32|DISUSE_DISPLACEMENT64)))
                                {
                                    RTGCPTR GCPtrVal = 0;

                                    if (DisState.aParams[1].fUse & DISUSE_RIPDISPLACEMENT32)
                                        GCPtrVal = GCPtrCur + DisState.aParams[1].x86.uDisp.i32 + cbInstr;
                                    else if (DisState.aParams[1].fUse & DISUSE_DISPLACEMENT32)
                                        GCPtrVal = (RTGCPTR)DisState.aParams[1].x86.uDisp.u32;
                                    else if (DisState.aParams[1].fUse & DISUSE_DISPLACEMENT64)
                                        GCPtrVal = (RTGCPTR)DisState.aParams[1].x86.uDisp.u64;
                                    else
                                        AssertMsgFailedBreakStmt(("Invalid displacement\n"), rc = VERR_INVALID_STATE);

                                    if (cAddressesUsed < RT_ELEMENTS(aAddresses))
                                    {
                                        /* movsxd reads always 32bits. */
                                        if (DisState.pCurInstr->uOpcode == OP_MOVSXD)
                                            aAddresses[cAddressesUsed].cb = sizeof(uint32_t);
                                        else
                                            aAddresses[cAddressesUsed].cb = DisState.aParams[1].x86.cb;
                                        aAddresses[cAddressesUsed].GCPtrOrigSrc = GCPtrVal;
                                        cAddressesUsed++;
                                    }
                                    else
                                    {
                                        rc = VERR_INVALID_PARAMETER;
                                        break;
                                    }
                                }
                            }
                            break;
                        default:
                            /* All other instructions will cause an error for now (playing safe here). */
                            rc = VERR_INVALID_PARAMETER;
                            break;
                    }
                    cInstrDisassembled++;
                    offInstr += cbInstr;
                }
            }
        } while (   RT_SUCCESS(rc)
                 && cInstrDisassembled < 20
                 && !fRet);
    }

    return rc;
}

/**
 * Try to get at the log buffer starting address and size by disassembling some exposed helpers.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM function table.
 * @param   hMod                The module to use.
 * @param   pGCPtrLogBuf        Where to store the log buffer pointer on success.
 * @param   pcbLogBuf           Where to store the size of the log buffer on success.
 */
static int dbgDiggerLinuxQueryLogBufferPtrs(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hMod,
                                            RTGCPTR *pGCPtrLogBuf, uint32_t *pcbLogBuf)
{
    int rc = VINF_SUCCESS;

    struct { void *pvVar; uint32_t cbHost, cbGuest; const char *pszSymbol; } aSymbols[] =
    {
        { pGCPtrLogBuf, (uint32_t)sizeof(RTGCPTR),  (uint32_t)(pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t)), "log_buf_addr_get" },
        { pcbLogBuf,    (uint32_t)sizeof(uint32_t), (uint32_t)sizeof(uint32_t),                                      "log_buf_len_get" }
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(aSymbols) && RT_SUCCESS(rc); i++)
    {
        RT_BZERO(aSymbols[i].pvVar, aSymbols[i].cbHost);
        Assert(aSymbols[i].cbHost >= aSymbols[i].cbGuest);
        rc = dbgDiggerLinuxDisassembleSimpleGetter(pThis, pUVM, pVMM, hMod, aSymbols[i].pszSymbol,
                                                   aSymbols[i].pvVar, aSymbols[i].cbGuest);
    }

    return rc;
}

/**
 * Returns whether the log buffer is a simple ascii buffer or a record based implementation
 * based on the kernel version found.
 *
 * @returns Flag whether the log buffer is the simple ascii buffer.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 */
static bool dbgDiggerLinuxLogBufferIsAsciiBuffer(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    char szTmp[128];
    char const *pszVer = &szTmp[sizeof(g_abLinuxVersion) - 1];

    RT_ZERO(szTmp);
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &pThis->AddrLinuxBanner, szTmp, sizeof(szTmp) - 1);
    if (    RT_SUCCESS(rc)
        &&  RTStrVersionCompare(pszVer, "3.4") == -1)
        return true;

    return false;
}

/**
 * Worker to get at the kernel log for pre 3.4 kernels where the log buffer was just a char buffer.
 *
 * @returns VBox status code.
 * @param   pThis       The Linux digger data.
 * @param   pUVM        The VM user mdoe handle.
 * @param   pVMM        The VMM function table.
 * @param   hMod        The debug module handle.
 * @param   fFlags      Flags reserved for future use, MBZ.
 * @param   cMessages   The number of messages to retrieve, counting from the
 *                      end of the log (i.e. like tail), use UINT32_MAX for all.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The buffer size.
 * @param   pcbActual   Where to store the number of bytes actually returned,
 *                      including zero terminator.  On VERR_BUFFER_OVERFLOW this
 *                      holds the necessary buffer size.  Optional.
 */
static int dbgDiggerLinuxLogBufferQueryAscii(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hMod,
                                             uint32_t fFlags, uint32_t cMessages,
                                             char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    RT_NOREF2(fFlags, cMessages);
    int rc = VINF_SUCCESS;
    RTGCPTR  GCPtrLogBuf;
    uint32_t cbLogBuf;

    struct { void *pvVar; size_t cbHost, cbGuest; const char *pszSymbol; } aSymbols[] =
    {
        { &GCPtrLogBuf, sizeof(GCPtrLogBuf),    pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t),   "log_buf" },
        { &cbLogBuf,    sizeof(cbLogBuf),       sizeof(cbLogBuf),                                      "log_buf_len" },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(aSymbols); i++)
    {
        RTDBGSYMBOL SymInfo;
        rc = RTDbgModSymbolByName(hMod, aSymbols[i].pszSymbol, &SymInfo);
        if (RT_SUCCESS(rc))
        {
            RT_BZERO(aSymbols[i].pvVar, aSymbols[i].cbHost);
            Assert(aSymbols[i].cbHost >= aSymbols[i].cbGuest);
            DBGFADDRESS Addr;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr,
                                                                    (RTGCPTR)SymInfo.Value + pThis->AddrKernelBase.FlatPtr),
                                        aSymbols[i].pvVar,  aSymbols[i].cbGuest);
            if (RT_SUCCESS(rc))
                continue;
            LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Reading '%s' at %RGv: %Rrc\n", aSymbols[i].pszSymbol, Addr.FlatPtr, rc));
        }
        else
            LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Error looking up '%s': %Rrc\n", aSymbols[i].pszSymbol, rc));
        rc = VERR_NOT_FOUND;
        break;
    }

    /*
     * Some kernels don't expose the variables in kallsyms so we have to try disassemble
     * some public helpers to get at the addresses.
     *
     * @todo: Maybe cache those values so we don't have to do the heavy work every time?
     */
    if (rc == VERR_NOT_FOUND)
    {
        rc = dbgDiggerLinuxQueryAsciiLogBufferPtrs(pThis, pUVM, pVMM, hMod, &GCPtrLogBuf, &cbLogBuf);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Check if the values make sense.
     */
    if (pThis->f64Bit ? !LNX64_VALID_ADDRESS(GCPtrLogBuf) : !LNX32_VALID_ADDRESS(GCPtrLogBuf))
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_buf' value %RGv is not valid.\n", GCPtrLogBuf));
        return VERR_NOT_FOUND;
    }
    if (   cbLogBuf < 4096
        || !RT_IS_POWER_OF_TWO(cbLogBuf)
        || cbLogBuf > 16*_1M)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_buf_len' value %#x is not valid.\n", cbLogBuf));
        return VERR_NOT_FOUND;
    }

    /*
     * Read the whole log buffer.
     */
    uint8_t *pbLogBuf = (uint8_t *)RTMemAlloc(cbLogBuf);
    if (!pbLogBuf)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Failed to allocate %#x bytes for log buffer\n", cbLogBuf));
        return VERR_NO_MEMORY;
    }
    DBGFADDRESS Addr;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, GCPtrLogBuf), pbLogBuf, cbLogBuf);
    if (RT_FAILURE(rc))
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Error reading %#x bytes of log buffer at %RGv: %Rrc\n",
                cbLogBuf, Addr.FlatPtr, rc));
        RTMemFree(pbLogBuf);
        return VERR_NOT_FOUND;
    }

    /** @todo Try to parse where the single messages start to make use of cMessages. */
    size_t cchLength = RTStrNLen((const char *)pbLogBuf, cbLogBuf);
    memcpy(&pszBuf[0], pbLogBuf, RT_MIN(cbBuf, cchLength));

    /* Done with the buffer. */
    RTMemFree(pbLogBuf);

    /* Set return size value. */
    if (pcbActual)
        *pcbActual = RT_MIN(cbBuf, cchLength);

    return cbBuf <= cchLength ? VERR_BUFFER_OVERFLOW : VINF_SUCCESS;
}


/**
 * Worker to process a given record based kernel log.
 *
 * @returns VBox status code.
 * @param   pThis       The Linux digger data.
 * @param   pUVM        The VM user mode handle.
 * @param   pVMM        The VMM function table.
 * @param   GCPtrLogBuf Flat guest address of the start of the log buffer.
 * @param   cbLogBuf    Power of two aligned size of the log buffer.
 * @param   idxFirst    Index in the log bfufer of the first message.
 * @param   idxNext     Index where to write hte next message in the log buffer.
 * @param   fFlags      Flags reserved for future use, MBZ.
 * @param   cMessages   The number of messages to retrieve, counting from the
 *                      end of the log (i.e. like tail), use UINT32_MAX for all.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The buffer size.
 * @param   pcbActual   Where to store the number of bytes actually returned,
 *                      including zero terminator.  On VERR_BUFFER_OVERFLOW this
 *                      holds the necessary buffer size.  Optional.
 */
static int dbgDiggerLinuxKrnLogBufferProcess(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTGCPTR GCPtrLogBuf,
                                             uint32_t cbLogBuf, uint32_t idxFirst, uint32_t idxNext,
                                             uint32_t fFlags, uint32_t cMessages, char *pszBuf, size_t cbBuf,
                                             size_t *pcbActual)
{
    RT_NOREF(fFlags);

    /*
     * Check if the values make sense.
     */
    if (pThis->f64Bit ? !LNX64_VALID_ADDRESS(GCPtrLogBuf) : !LNX32_VALID_ADDRESS(GCPtrLogBuf))
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_buf' value %RGv is not valid.\n", GCPtrLogBuf));
        return VERR_NOT_FOUND;
    }
    if (   cbLogBuf < _4K
        || !RT_IS_POWER_OF_TWO(cbLogBuf)
        || cbLogBuf > LNX_MAX_KERNEL_LOG_SIZE)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_buf_len' value %#x is not valid.\n", cbLogBuf));
        return VERR_NOT_FOUND;
    }
    uint32_t const cbLogAlign = 4;
    if (   idxFirst > cbLogBuf - sizeof(LNXPRINTKHDR)
        || (idxFirst & (cbLogAlign - 1)) != 0)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_first_idx' value %#x is not valid.\n", idxFirst));
        return VERR_NOT_FOUND;
    }
    if (   idxNext > cbLogBuf - sizeof(LNXPRINTKHDR)
        || (idxNext & (cbLogAlign - 1)) != 0)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: 'log_next_idx' value %#x is not valid.\n", idxNext));
        return VERR_NOT_FOUND;
    }

    /*
     * Read the whole log buffer.
     */
    uint8_t *pbLogBuf = (uint8_t *)RTMemAlloc(cbLogBuf);
    if (!pbLogBuf)
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Failed to allocate %#x bytes for log buffer\n", cbLogBuf));
        return VERR_NO_MEMORY;
    }
    DBGFADDRESS Addr;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, GCPtrLogBuf), pbLogBuf, cbLogBuf);
    if (RT_FAILURE(rc))
    {
        LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Error reading %#x bytes of log buffer at %RGv: %Rrc\n",
                cbLogBuf, Addr.FlatPtr, rc));
        RTMemFree(pbLogBuf);
        return VERR_NOT_FOUND;
    }

    /*
     * Count the messages in the buffer while doing some basic validation.
     */
    uint32_t const cbUsed = idxFirst == idxNext ? cbLogBuf /* could be empty... */
                          : idxFirst < idxNext  ? idxNext - idxFirst : cbLogBuf - idxFirst + idxNext;
    uint32_t cbLeft    = cbUsed;
    uint32_t offCur    = idxFirst;
    uint32_t cLogMsgs  = 0;

    while (cbLeft > 0)
    {
        PCLNXPRINTKHDR pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
        if (!pHdr->cbTotal)
        {
            /* Wrap around packet, most likely... */
            if (cbLogBuf - offCur >= cbLeft)
                break;
            offCur = 0;
            pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
        }
        if (RT_UNLIKELY(   pHdr->cbTotal > cbLogBuf - sizeof(*pHdr) - offCur
                        || pHdr->cbTotal > cbLeft
                        || (pHdr->cbTotal & (cbLogAlign - 1)) != 0
                        || pHdr->cbTotal < (uint32_t)pHdr->cbText + (uint32_t)pHdr->cbDict + sizeof(*pHdr) ))
        {
            LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Invalid printk_log record at %#x: cbTotal=%#x cbText=%#x cbDict=%#x cbLogBuf=%#x cbLeft=%#x\n",
                    offCur, pHdr->cbTotal, pHdr->cbText, pHdr->cbDict, cbLogBuf, cbLeft));
            break;
        }

        if (pHdr->cbText > 0)
            cLogMsgs++;

        /* next */
        offCur += pHdr->cbTotal;
        cbLeft -= pHdr->cbTotal;
    }
    if (!cLogMsgs)
    {
        RTMemFree(pbLogBuf);
        return VERR_NOT_FOUND;
    }

    /*
     * Copy the messages into the output buffer.
     */
    offCur = idxFirst;
    cbLeft = cbUsed - cbLeft;

    /* Skip messages that the caller doesn't want. */
    if (cMessages < cLogMsgs)
    {
        uint32_t cToSkip = cLogMsgs - cMessages;
        cLogMsgs -= cToSkip;

        while (cToSkip > 0)
        {
            PCLNXPRINTKHDR pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
            if (!pHdr->cbTotal)
            {
                offCur = 0;
                pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
            }
            if (pHdr->cbText > 0)
                cToSkip--;

            /* next */
            offCur += pHdr->cbTotal;
            cbLeft -= pHdr->cbTotal;
        }
    }

    /* Now copy the messages. */
    size_t offDst = 0;
    while (cbLeft > 0)
    {
        PCLNXPRINTKHDR pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
        if (   !pHdr->cbTotal
            || !cLogMsgs)
        {
            if (cbLogBuf - offCur >= cbLeft)
                break;
            offCur = 0;
            pHdr = (PCLNXPRINTKHDR)&pbLogBuf[offCur];
        }

        if (pHdr->cbText > 0)
        {
            char  *pchText = (char *)(pHdr + 1);
            size_t cchText = RTStrNLen(pchText, pHdr->cbText);
            if (offDst + cchText < cbBuf)
            {
                memcpy(&pszBuf[offDst], pHdr + 1, cchText);
                pszBuf[offDst + cchText] = '\n';
            }
            else if (offDst < cbBuf)
                memcpy(&pszBuf[offDst], pHdr + 1, cbBuf - offDst);
            offDst += cchText + 1;
        }

        /* next */
        offCur += pHdr->cbTotal;
        cbLeft -= pHdr->cbTotal;
    }

    /* Done with the buffer. */
    RTMemFree(pbLogBuf);

    /* Make sure we've reserved a char for the terminator. */
    if (!offDst)
        offDst = 1;

    /* Set return size value. */
    if (pcbActual)
        *pcbActual = offDst;

    if (offDst <= cbBuf)
        return VINF_SUCCESS;
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Worker to get at the kernel log for post 3.4 kernels where the log buffer contains records.
 *
 * @returns VBox status code.
 * @param   pThis       The Linux digger data.
 * @param   pUVM        The VM user mdoe handle.
 * @param   pVMM        The VMM function table.
 * @param   hMod        The debug module handle.
 * @param   fFlags      Flags reserved for future use, MBZ.
 * @param   cMessages   The number of messages to retrieve, counting from the
 *                      end of the log (i.e. like tail), use UINT32_MAX for all.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The buffer size.
 * @param   pcbActual   Where to store the number of bytes actually returned,
 *                      including zero terminator.  On VERR_BUFFER_OVERFLOW this
 *                      holds the necessary buffer size.  Optional.
 */
static int dbgDiggerLinuxLogBufferQueryRecords(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hMod,
                                               uint32_t fFlags, uint32_t cMessages,
                                               char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    int rc = VINF_SUCCESS;
    RTGCPTR  GCPtrLogBuf;
    uint32_t cbLogBuf;
    uint32_t idxFirst;
    uint32_t idxNext;

    struct { void *pvVar; size_t cbHost, cbGuest; const char *pszSymbol; } aSymbols[] =
    {
        { &GCPtrLogBuf, sizeof(GCPtrLogBuf),    pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t),   "log_buf" },
        { &cbLogBuf,    sizeof(cbLogBuf),       sizeof(cbLogBuf),                                      "log_buf_len" },
        { &idxFirst,    sizeof(idxFirst),       sizeof(idxFirst),                                      "log_first_idx" },
        { &idxNext,     sizeof(idxNext),        sizeof(idxNext),                                       "log_next_idx" },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(aSymbols); i++)
    {
        RTDBGSYMBOL SymInfo;
        rc = RTDbgModSymbolByName(hMod, aSymbols[i].pszSymbol, &SymInfo);
        if (RT_SUCCESS(rc))
        {
            RT_BZERO(aSymbols[i].pvVar, aSymbols[i].cbHost);
            Assert(aSymbols[i].cbHost >= aSymbols[i].cbGuest);
            DBGFADDRESS Addr;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr,
                                                                    (RTGCPTR)SymInfo.Value + pThis->AddrKernelBase.FlatPtr),
                                        aSymbols[i].pvVar,  aSymbols[i].cbGuest);
            if (RT_SUCCESS(rc))
                continue;
            LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Reading '%s' at %RGv: %Rrc\n", aSymbols[i].pszSymbol, Addr.FlatPtr, rc));
        }
        else
            LogRel(("dbgDiggerLinuxIDmsg_QueryKernelLog: Error looking up '%s': %Rrc\n", aSymbols[i].pszSymbol, rc));
        rc = VERR_NOT_FOUND;
        break;
    }

    /*
     * Some kernels don't expose the variables in kallsyms so we have to try disassemble
     * some public helpers to get at the addresses.
     *
     * @todo: Maybe cache those values so we don't have to do the heavy work every time?
     */
    if (rc == VERR_NOT_FOUND)
    {
        idxFirst = 0;
        idxNext = 0;
        rc = dbgDiggerLinuxQueryLogBufferPtrs(pThis, pUVM, pVMM, hMod, &GCPtrLogBuf, &cbLogBuf);
        if (RT_FAILURE(rc))
        {
            /*
             * Last resort, scan for a known value which should appear only once in the kernel log buffer
             * and try to deduce the boundaries from there.
             */
            return dbgDiggerLinuxKrnlLogBufFindByNeedle(pThis, pUVM, pVMM, &GCPtrLogBuf, &cbLogBuf);
        }
    }

    return dbgDiggerLinuxKrnLogBufferProcess(pThis, pUVM, pVMM, GCPtrLogBuf, cbLogBuf, idxFirst, idxNext,
                                             fFlags, cMessages, pszBuf, cbBuf, pcbActual);
}

/**
 * @interface_method_impl{DBGFOSIDMESG,pfnQueryKernelLog}
 */
static DECLCALLBACK(int) dbgDiggerLinuxIDmsg_QueryKernelLog(PDBGFOSIDMESG pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, uint32_t fFlags,
                                                            uint32_t cMessages, char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    PDBGDIGGERLINUX pData = RT_FROM_MEMBER(pThis, DBGDIGGERLINUX, IDmesg);

    if (cMessages < 1)
        return VERR_INVALID_PARAMETER;

    /*
     * Resolve the symbols we need and read their values.
     */
    RTDBGAS  hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    RTDBGMOD hMod;
    int rc = RTDbgAsModuleByName(hAs, "vmlinux", 0, &hMod);
    RTDbgAsRelease(hAs);

    size_t cbActual = 0;
    if (RT_SUCCESS(rc))
    {
        /*
         * Check whether the kernel log buffer is a simple char buffer or the newer
         * record based implementation.
         * The record based implementation was presumably introduced with kernel 3.4,
         * see: http://thread.gmane.org/gmane.linux.kernel/1284184
         */
        if (dbgDiggerLinuxLogBufferIsAsciiBuffer(pData, pUVM, pVMM))
            rc = dbgDiggerLinuxLogBufferQueryAscii(pData, pUVM, pVMM, hMod, fFlags, cMessages, pszBuf, cbBuf, &cbActual);
        else
            rc = dbgDiggerLinuxLogBufferQueryRecords(pData, pUVM, pVMM, hMod, fFlags, cMessages, pszBuf, cbBuf, &cbActual);

        /* Release the module in any case. */
        RTDbgModRelease(hMod);
    }
    else
    {
        /*
         * For the record based kernel versions we have a last resort heuristic which doesn't
         * require any symbols, try that here.
         */
        if (!dbgDiggerLinuxLogBufferIsAsciiBuffer(pData, pUVM, pVMM))
        {
            RTGCPTR GCPtrLogBuf = 0;
            uint32_t cbLogBuf = 0;

            rc = dbgDiggerLinuxKrnlLogBufFindByNeedle(pData, pUVM, pVMM, &GCPtrLogBuf, &cbLogBuf);
            if (RT_SUCCESS(rc))
                rc = dbgDiggerLinuxKrnLogBufferProcess(pData, pUVM, pVMM, GCPtrLogBuf, cbLogBuf, 0 /*idxFirst*/, 0 /*idxNext*/,
                                                       fFlags, cMessages, pszBuf, cbBuf, &cbActual);
        }
        else
            rc = VERR_NOT_FOUND;
    }

    if (RT_FAILURE(rc) && rc != VERR_BUFFER_OVERFLOW)
        return rc;

    if (pcbActual)
        *pcbActual = cbActual;

    /*
     * All VBox strings are UTF-8 and bad things may in theory happen if we
     * pass bad UTF-8 to code which assumes it's all valid.  So, we enforce
     * UTF-8 upon the guest kernel messages here even if they (probably) have
     * no defined code set in reality.
     */
    if (   RT_SUCCESS(rc)
        && cbActual <= cbBuf)
    {
        pszBuf[cbActual - 1] = '\0';
        RTStrPurgeEncoding(pszBuf);
        return VINF_SUCCESS;
    }

    if (cbBuf)
    {
        pszBuf[cbBuf - 1] = '\0';
        RTStrPurgeEncoding(pszBuf);
    }
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Worker destroying the config database.
 */
static DECLCALLBACK(int) dbgDiggerLinuxCfgDbDestroyWorker(PRTSTRSPACECORE pStr, void *pvUser)
{
    PDBGDIGGERLINUXCFGITEM pCfgItem = (PDBGDIGGERLINUXCFGITEM)pStr;
    RTStrFree((char *)pCfgItem->Core.pszString);
    RTMemFree(pCfgItem);
    NOREF(pvUser);
    return 0;
}


/**
 * Destroy the config database.
 *
 * @param   pThis               The Linux digger data.
 */
static void dbgDiggerLinuxCfgDbDestroy(PDBGDIGGERLINUX pThis)
{
    RTStrSpaceDestroy(&pThis->hCfgDb, dbgDiggerLinuxCfgDbDestroyWorker, NULL);
}


/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerLinuxStackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                         PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState, PCCPUMCTX pInitialCtx,
                                                         RTDBGAS hAs, uint64_t *puScratch)
{
    RT_NOREF(pUVM, pVMM, pvData, idCpu, pFrame, pState, pInitialCtx, hAs, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerLinuxQueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
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
static DECLCALLBACK(int)  dbgDiggerLinuxQueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                     char *pszVersion, size_t cchVersion)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
    Assert(pThis->fValid);

    /*
     * It's all in the linux banner.
     */
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &pThis->AddrLinuxBanner, pszVersion, cchVersion);
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
        RTStrPrintf(pszVersion, cchVersion, "DBGFR3MemRead -> %Rrc", rc);

    return rc;
}


/**
 * @copydoc DBGFOSREG::pfnTerm
 */
static DECLCALLBACK(void)  dbgDiggerLinuxTerm(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
    Assert(pThis->fValid);

    /*
     * Destroy configuration database.
     */
    dbgDiggerLinuxCfgDbDestroy(pThis);

    /*
     * Unlink and release our modules.
     */
    RTDBGAS hDbgAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    if (hDbgAs != NIL_RTDBGAS)
    {
        uint32_t iMod = RTDbgAsModuleCount(hDbgAs);
        while (iMod-- > 0)
        {
            RTDBGMOD hMod = RTDbgAsModuleByIndex(hDbgAs, iMod);
            if (hMod != NIL_RTDBGMOD)
            {
                if (RTDbgModGetTag(hMod) == DIG_LNX_MOD_TAG)
                {
                    int rc = RTDbgAsModuleUnlink(hDbgAs, hMod);
                    AssertRC(rc);
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hDbgAs);
    }

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerLinuxRefresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
    RT_NOREF(pThis);
    Assert(pThis->fValid);

    /*
     * For now we'll flush and reload everything.
     */
    dbgDiggerLinuxTerm(pUVM, pVMM, pvData);
    return dbgDiggerLinuxInit(pUVM, pVMM, pvData);
}


/**
 * Worker for dbgDiggerLinuxFindStartOfNamesAndSymbolCount that update the
 * digger data.
 *
 * @returns VINF_SUCCESS.
 * @param   pThis               The Linux digger data to update.
 * @param   pVMM                The VMM function table.
 * @param   pAddrKernelNames    The kallsyms_names address.
 * @param   cKernelSymbols      The number of kernel symbol.
 * @param   cbAddress           The guest address size.
 */
static int dbgDiggerLinuxFoundStartOfNames(PDBGDIGGERLINUX pThis, PCVMMR3VTABLE pVMM, PCDBGFADDRESS pAddrKernelNames,
                                           uint32_t cKernelSymbols, uint32_t cbAddress)
{
    pThis->cKernelSymbols = cKernelSymbols;
    pThis->AddrKernelNames = *pAddrKernelNames;
    pThis->AddrKernelAddresses = *pAddrKernelNames;
    uint32_t cbSymbolsSkip = (pThis->fRelKrnlAddr ? 2 : 1) * cbAddress; /* Relative addressing introduces kallsyms_relative_base. */
    uint32_t cbOffsets = pThis->fRelKrnlAddr ? sizeof(int32_t) : cbAddress; /* Offsets are always 32bits wide for relative addressing. */
    uint32_t cbAlign = 0;

    /*
     * If the number of symbols is odd there is padding to align the following guest pointer
     * sized data properly on 64bit systems with relative addressing.
     */
    if (   pThis->fRelKrnlAddr
        && pThis->f64Bit
        && (pThis->cKernelSymbols & 1))
        cbAlign = sizeof(int32_t);
    pVMM->pfnDBGFR3AddrSub(&pThis->AddrKernelAddresses, cKernelSymbols * cbOffsets + cbSymbolsSkip + cbAlign);

    Log(("dbgDiggerLinuxFoundStartOfNames: AddrKernelAddresses=%RGv\n"
         "dbgDiggerLinuxFoundStartOfNames: cKernelSymbols=%#x (at %RGv)\n"
         "dbgDiggerLinuxFoundStartOfNames: AddrKernelName=%RGv\n",
         pThis->AddrKernelAddresses.FlatPtr,
         pThis->cKernelSymbols, pThis->AddrKernelNames.FlatPtr - cbAddress,
         pThis->AddrKernelNames.FlatPtr));
    return VINF_SUCCESS;
}


/**
 * Tries to find the address of the kallsyms_names, kallsyms_num_syms and
 * kallsyms_addresses symbols.
 *
 * The kallsyms_num_syms is read and stored in pThis->cKernelSymbols, while the
 * addresses of the other two are stored as pThis->AddrKernelNames and
 * pThis->AddrKernelAddresses.
 *
 * @returns VBox status code, success indicating that all three variables have
 *          been found and taken down.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 * @param   pHitAddr            An address we think is inside kallsyms_names.
 */
static int dbgDiggerLinuxFindStartOfNamesAndSymbolCount(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis,
                                                        PCDBGFADDRESS pHitAddr)
{
    /*
     * Search backwards in chunks.
     */
    union
    {
        uint8_t  ab[0x1000];
        uint32_t au32[0x1000 / sizeof(uint32_t)];
        uint64_t au64[0x1000 / sizeof(uint64_t)];
    } uBuf;
    uint32_t        cbLeft  = LNX_MAX_KALLSYMS_NAMES_SIZE;
    uint32_t        cbBuf   = pHitAddr->FlatPtr & (sizeof(uBuf) - 1);
    DBGFADDRESS     CurAddr = *pHitAddr;
    pVMM->pfnDBGFR3AddrSub(&CurAddr, cbBuf);
    cbBuf += sizeof(uint64_t) - 1;      /* In case our kobj hit is in the first 4/8 bytes. */
    for (;;)
    {
        int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &CurAddr, &uBuf, sizeof(uBuf));
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Since Linux 4.6 there are two different methods to store the kallsyms addresses
         * in the image.
         *
         * The first and longer existing method is to store the absolute addresses in an
         * array starting at kallsyms_addresses followed by a field which stores the number
         * of kernel symbols called kallsyms_num_syms.
         * The newer method is to use offsets stored in kallsyms_offsets and have a base pointer
         * to relate the offsets to called kallsyms_relative_base. One entry in kallsyms_offsets is
         * always 32bit wide regardless of the guest pointer size (this halves the table on 64bit
         * systems) but means more work for us for the 64bit case.
         *
         * When absolute addresses are used the following assumptions hold:
         *
         *     We assume that the three symbols are aligned on guest pointer boundary.
         *
         *     The boundary between the two tables should be noticable as the number
         *     is unlikely to be more than 16 millions, there will be at least one zero
         *     byte where it is, 64-bit will have 5 zero bytes.  Zero bytes aren't all
         *     that common in the kallsyms_names table.
         *
         *     Also the kallsyms_names table starts with a length byte, which means
         *     we're likely to see a byte in the range 1..31.
         *
         *     The kallsyms_addresses are mostly sorted (except for the start where the
         *     absolute symbols are), so we'll spot a bunch of kernel addresses
         *     immediately preceding the kallsyms_num_syms field.
         *
         *     Lazy bird: If kallsyms_num_syms is on a buffer boundrary, we skip
         *                the check for kernel addresses preceding it.
         *
         * For relative offsets most of the assumptions from above are true too
         * except that we have to distinguish between the relative base address and the offsets.
         * Every observed kernel has a valid kernel address fo the relative base and kallsyms_relative_base
         * always comes before kallsyms_num_syms and is aligned on a guest pointer boundary.
         * Offsets are stored before kallsyms_relative_base and don't contain valid kernel addresses.
         *
         * To distinguish between absolute and relative offsetting we check the data before a candidate
         * for kallsyms_num_syms. If all entries before the kallsyms_num_syms candidate are valid kernel
         * addresses absolute addresses are assumed. If this is not the case but the first entry before
         * kallsyms_num_syms is a valid kernel address we check whether the data before and the possible
         * relative base form a valid kernel address and assume relative offsets.
         *
         * Other notable changes between various Linux kernel versions:
         *
         *     4.20.0+: Commit 80ffbaa5b1bd98e80e3239a3b8cfda2da433009a made kallsyms_num_syms 32bit
         *              even on 64bit systems but the alignment of the variables makes the code below work for now
         *              (tested with a 5.4 and 5.12 kernel) do we keep it that way to avoid making the code even
         *              messy.
         */
        if (pThis->f64Bit)
        {
            uint32_t i = cbBuf / sizeof(uint64_t) - 1;
            while (i-- > 0)
                if (   uBuf.au64[i] <= LNX_MAX_KALLSYMS_SYMBOLS
                    && uBuf.au64[i] >= LNX_MIN_KALLSYMS_SYMBOLS)
                {
                    uint8_t *pb = (uint8_t *)&uBuf.au64[i + 1];
                    if (   pb[0] <= LNX_MAX_KALLSYMS_ENC_LENGTH
                        && pb[0] >= LNX_MIN_KALLSYMS_ENC_LENGTH)
                    {
                        /*
                         * Check whether we have a valid kernel address and try to distinguish
                         * whether the kernel uses relative offsetting or absolute addresses.
                         */
                        if (   (i >= 1 && LNX64_VALID_ADDRESS(uBuf.au64[i - 1]))
                            && (i >= 2 && !LNX64_VALID_ADDRESS(uBuf.au64[i - 2]))
                            && (i >= 3 && !LNX64_VALID_ADDRESS(uBuf.au64[i - 3])))
                        {
                            RTGCUINTPTR uKrnlRelBase = uBuf.au64[i - 1];
                            DBGFADDRESS RelAddr = CurAddr;
                            int32_t aiRelOff[3];
                            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                        pVMM->pfnDBGFR3AddrAdd(&RelAddr,
                                                                               (i - 1) * sizeof(uint64_t) - sizeof(aiRelOff)),
                                                        &aiRelOff[0], sizeof(aiRelOff));
                            if (   RT_SUCCESS(rc)
                                && LNX64_VALID_ADDRESS(uKrnlRelBase + aiRelOff[0])
                                && LNX64_VALID_ADDRESS(uKrnlRelBase + aiRelOff[1])
                                && LNX64_VALID_ADDRESS(uKrnlRelBase + aiRelOff[2]))
                            {
                                Log(("dbgDiggerLinuxFindStartOfNamesAndSymbolCount: relative base %RGv (at %RGv)\n",
                                     uKrnlRelBase, CurAddr.FlatPtr + (i - 1) * sizeof(uint64_t)));
                                pThis->fRelKrnlAddr = true;
                                pThis->uKernelRelativeBase = uKrnlRelBase;
                                return dbgDiggerLinuxFoundStartOfNames(pThis, pVMM,
                                                                       pVMM->pfnDBGFR3AddrAdd(&CurAddr, (i + 1) * sizeof(uint64_t)),
                                                                       (uint32_t)uBuf.au64[i], sizeof(uint64_t));
                            }
                        }

                        if (   (i <= 0 || LNX64_VALID_ADDRESS(uBuf.au64[i - 1]))
                            && (i <= 1 || LNX64_VALID_ADDRESS(uBuf.au64[i - 2]))
                            && (i <= 2 || LNX64_VALID_ADDRESS(uBuf.au64[i - 3])))
                            return dbgDiggerLinuxFoundStartOfNames(pThis, pVMM,
                                                                   pVMM->pfnDBGFR3AddrAdd(&CurAddr, (i + 1) * sizeof(uint64_t)),
                                                                   (uint32_t)uBuf.au64[i], sizeof(uint64_t));
                    }
                }
        }
        else
        {
            uint32_t i = cbBuf / sizeof(uint32_t) - 1;
            while (i-- > 0)
                if (   uBuf.au32[i] <= LNX_MAX_KALLSYMS_SYMBOLS
                    && uBuf.au32[i] >= LNX_MIN_KALLSYMS_SYMBOLS)
                {
                    uint8_t *pb = (uint8_t *)&uBuf.au32[i + 1];
                    if (   pb[0] <= LNX_MAX_KALLSYMS_ENC_LENGTH
                        && pb[0] >= LNX_MIN_KALLSYMS_ENC_LENGTH)
                    {
                        /* Check for relative base addressing. */
                        if (i >= 1 && LNX32_VALID_ADDRESS(uBuf.au32[i - 1]))
                        {
                            RTGCUINTPTR uKrnlRelBase = uBuf.au32[i - 1];
                            if (   (i <= 1 || LNX32_VALID_ADDRESS(uKrnlRelBase + uBuf.au32[i - 2]))
                                && (i <= 2 || LNX32_VALID_ADDRESS(uKrnlRelBase + uBuf.au32[i - 3])))
                            {
                                Log(("dbgDiggerLinuxFindStartOfNamesAndSymbolCount: relative base %RGv (at %RGv)\n",
                                     uKrnlRelBase, CurAddr.FlatPtr + (i - 1) * sizeof(uint32_t)));
                                pThis->fRelKrnlAddr = true;
                                pThis->uKernelRelativeBase = uKrnlRelBase;
                                return dbgDiggerLinuxFoundStartOfNames(pThis, pVMM,
                                                                       pVMM->pfnDBGFR3AddrAdd(&CurAddr, (i + 1) * sizeof(uint32_t)),
                                                                       uBuf.au32[i], sizeof(uint32_t));
                            }
                        }

                        if (   (i <= 0 || LNX32_VALID_ADDRESS(uBuf.au32[i - 1]))
                            && (i <= 1 || LNX32_VALID_ADDRESS(uBuf.au32[i - 2]))
                            && (i <= 2 || LNX32_VALID_ADDRESS(uBuf.au32[i - 3])))
                            return dbgDiggerLinuxFoundStartOfNames(pThis, pVMM,
                                                                   pVMM->pfnDBGFR3AddrAdd(&CurAddr, (i + 1) * sizeof(uint32_t)),
                                                                   uBuf.au32[i], sizeof(uint32_t));
                    }
                }
        }

        /*
         * Advance
         */
        if (RT_UNLIKELY(cbLeft <= sizeof(uBuf)))
        {
            Log(("dbgDiggerLinuxFindStartOfNamesAndSymbolCount: failed (pHitAddr=%RGv)\n", pHitAddr->FlatPtr));
            return VERR_NOT_FOUND;
        }
        cbLeft -= sizeof(uBuf);
        pVMM->pfnDBGFR3AddrSub(&CurAddr, sizeof(uBuf));
        cbBuf = sizeof(uBuf);
    }
}


/**
 * Worker for dbgDiggerLinuxFindEndNames that records the findings.
 *
 * @returns VINF_SUCCESS
 * @param   pThis           The linux digger data to update.
 * @param   pVMM                The VMM function table.
 * @param   pAddrMarkers    The address of the marker (kallsyms_markers).
 * @param   cbMarkerEntry   The size of a marker entry (32-bit or 64-bit).
 */
static int dbgDiggerLinuxFoundMarkers(PDBGDIGGERLINUX pThis, PCVMMR3VTABLE pVMM,
                                      PCDBGFADDRESS pAddrMarkers, uint32_t cbMarkerEntry)
{
    pThis->cbKernelNames         = pAddrMarkers->FlatPtr - pThis->AddrKernelNames.FlatPtr;
    pThis->AddrKernelNameMarkers = *pAddrMarkers;
    pThis->cKernelNameMarkers    = RT_ALIGN_32(pThis->cKernelSymbols, 256) / 256;
    pThis->AddrKernelTokenTable  = *pAddrMarkers;
    pVMM->pfnDBGFR3AddrAdd(&pThis->AddrKernelTokenTable, pThis->cKernelNameMarkers * cbMarkerEntry);

    Log(("dbgDiggerLinuxFoundMarkers: AddrKernelNames=%RGv cbKernelNames=%#x\n"
         "dbgDiggerLinuxFoundMarkers: AddrKernelNameMarkers=%RGv cKernelNameMarkers=%#x\n"
         "dbgDiggerLinuxFoundMarkers: AddrKernelTokenTable=%RGv\n",
         pThis->AddrKernelNames.FlatPtr, pThis->cbKernelNames,
         pThis->AddrKernelNameMarkers.FlatPtr, pThis->cKernelNameMarkers,
         pThis->AddrKernelTokenTable.FlatPtr));
    return VINF_SUCCESS;
}


/**
 * Tries to find the end of kallsyms_names and thereby the start of
 * kallsyms_markers and kallsyms_token_table.
 *
 * The kallsyms_names size is stored in pThis->cbKernelNames, the addresses of
 * the two other symbols in pThis->AddrKernelNameMarkers and
 * pThis->AddrKernelTokenTable.  The number of marker entries is stored in
 * pThis->cKernelNameMarkers.
 *
 * @returns VBox status code, success indicating that all three variables have
 *          been found and taken down.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 * @param   pHitAddr            An address we think is inside kallsyms_names.
 */
static int dbgDiggerLinuxFindEndOfNamesAndMore(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis, PCDBGFADDRESS pHitAddr)
{
    /*
     * Search forward in chunks.
     */
    union
    {
        uint8_t  ab[0x1000];
        uint32_t au32[0x1000 / sizeof(uint32_t)];
        uint64_t au64[0x1000 / sizeof(uint64_t)];
    } uBuf;
    bool            fPendingZeroHit = false;
    uint32_t        cbLeft  = LNX_MAX_KALLSYMS_NAMES_SIZE + sizeof(uBuf);
    uint32_t        offBuf  = pHitAddr->FlatPtr & (sizeof(uBuf) - 1);
    DBGFADDRESS     CurAddr = *pHitAddr;
    pVMM->pfnDBGFR3AddrSub(&CurAddr, offBuf);
    for (;;)
    {
        int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &CurAddr, &uBuf, sizeof(uBuf));
        if (RT_FAILURE(rc))
            return rc;

        /*
         * The kallsyms_names table is followed by kallsyms_markers we assume,
         * using sizeof(unsigned long) alignment like the preceding symbols.
         *
         * The kallsyms_markers table has entried sizeof(unsigned long) and
         * contains offsets into kallsyms_names.  The kallsyms_markers used to
         * index kallsyms_names and reduce seek time when looking up the name
         * of an address/symbol.  Each entry in kallsyms_markers covers 256
         * symbol names.
         *
         * Because of this, the first entry is always zero and all the entries
         * are ascending.  It also follows that the size of the table can be
         * calculated from kallsyms_num_syms.
         *
         * Note! We could also have walked kallsyms_names by skipping
         *       kallsyms_num_syms names, but this is faster and we will
         *       validate the encoded names later.
         *
         * git commit 80ffbaa5b1bd98e80e3239a3b8cfda2da433009a (which became 4.20+) makes kallsyms_markers
         * and kallsyms_num_syms uint32_t, even on 64bit systems. Take that into account.
         */
        if (   pThis->f64Bit
            && pThis->uKrnlVer < LNX_MK_VER(4, 20, 0))
        {
            if (   RT_UNLIKELY(fPendingZeroHit)
                && uBuf.au64[0] >= (LNX_MIN_KALLSYMS_ENC_LENGTH + 1) * 256
                && uBuf.au64[0] <= (LNX_MAX_KALLSYMS_ENC_LENGTH + 1) * 256)
                return dbgDiggerLinuxFoundMarkers(pThis, pVMM,
                                                  pVMM->pfnDBGFR3AddrSub(&CurAddr, sizeof(uint64_t)), sizeof(uint64_t));

            uint32_t const cEntries = sizeof(uBuf) / sizeof(uint64_t);
            for (uint32_t i = offBuf / sizeof(uint64_t); i < cEntries; i++)
                if (uBuf.au64[i] == 0)
                {
                    if (RT_UNLIKELY(i + 1 >= cEntries))
                    {
                        fPendingZeroHit = true;
                        break;
                    }
                    if (   uBuf.au64[i + 1] >= (LNX_MIN_KALLSYMS_ENC_LENGTH + 1) * 256
                        && uBuf.au64[i + 1] <= (LNX_MAX_KALLSYMS_ENC_LENGTH + 1) * 256)
                        return dbgDiggerLinuxFoundMarkers(pThis, pVMM,
                                                          pVMM->pfnDBGFR3AddrAdd(&CurAddr, i * sizeof(uint64_t)), sizeof(uint64_t));
                }
        }
        else
        {
            if (   RT_UNLIKELY(fPendingZeroHit)
                && uBuf.au32[0] >= (LNX_MIN_KALLSYMS_ENC_LENGTH + 1) * 256
                && uBuf.au32[0] <= (LNX_MAX_KALLSYMS_ENC_LENGTH + 1) * 256)
                return dbgDiggerLinuxFoundMarkers(pThis, pVMM,
                                                  pVMM->pfnDBGFR3AddrSub(&CurAddr, sizeof(uint32_t)), sizeof(uint32_t));

            uint32_t const cEntries = sizeof(uBuf) / sizeof(uint32_t);
            for (uint32_t i = offBuf / sizeof(uint32_t); i < cEntries; i++)
                if (uBuf.au32[i] == 0)
                {
                    if (RT_UNLIKELY(i + 1 >= cEntries))
                    {
                        fPendingZeroHit = true;
                        break;
                    }
                    if (   uBuf.au32[i + 1] >= (LNX_MIN_KALLSYMS_ENC_LENGTH + 1) * 256
                        && uBuf.au32[i + 1] <= (LNX_MAX_KALLSYMS_ENC_LENGTH + 1) * 256)
                        return dbgDiggerLinuxFoundMarkers(pThis, pVMM,
                                                          pVMM->pfnDBGFR3AddrAdd(&CurAddr, i * sizeof(uint32_t)), sizeof(uint32_t));
                }
        }

        /*
         * Advance
         */
        if (RT_UNLIKELY(cbLeft <= sizeof(uBuf)))
        {
            Log(("dbgDiggerLinuxFindEndOfNamesAndMore: failed (pHitAddr=%RGv)\n", pHitAddr->FlatPtr));
            return VERR_NOT_FOUND;
        }
        cbLeft -= sizeof(uBuf);
        pVMM->pfnDBGFR3AddrAdd(&CurAddr, sizeof(uBuf));
        offBuf = 0;
    }
}


/**
 * Locates the kallsyms_token_index table.
 *
 * Storing the address in pThis->AddrKernelTokenIndex and the size of the token
 * table in pThis->cbKernelTokenTable.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 */
static int dbgDiggerLinuxFindTokenIndex(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis)
{
    /*
     * The kallsyms_token_table is very much like a string table.  Due to the
     * nature of the compression algorithm it is reasonably short (one example
     * here is 853 bytes), so we'll not be reading it in chunks but in full.
     * To be on the safe side, we read 8KB, ASSUMING we won't run into unmapped
     * memory or any other nasty stuff...
     */
    union
    {
        uint8_t  ab[0x2000];
        uint16_t au16[0x2000 / sizeof(uint16_t)];
    } uBuf;
    DBGFADDRESS CurAddr = pThis->AddrKernelTokenTable;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &CurAddr, &uBuf, sizeof(uBuf));
    if (RT_FAILURE(rc))
        return rc;

    /*
     * We've got two choices here, either walk the string table or look for
     * the next structure, kallsyms_token_index.
     *
     * The token index is a table of 256 uint16_t entries (index by bytes
     * from kallsyms_names) that gives offsets in kallsyms_token_table.  It
     * starts with a zero entry and the following entries are sorted in
     * ascending order.  The range of the entries are reasonably small since
     * kallsyms_token_table is small.
     *
     * The alignment seems to be sizeof(unsigned long), just like
     * kallsyms_token_table.
     *
     * So, we start by looking for a zero 16-bit entry.
     */
    uint32_t cIncr = (pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t)) / sizeof(uint16_t);

    for (uint32_t i = 0; i < sizeof(uBuf) / sizeof(uint16_t) - 16; i += cIncr)
        if (   uBuf.au16[i] == 0
            && uBuf.au16[i + 1] >  0
            && uBuf.au16[i + 1] <= LNX_MAX_KALLSYMS_TOKEN_LEN
            && (uint16_t)(uBuf.au16[i + 2] - uBuf.au16[i + 1] - 1U) <= (uint16_t)LNX_MAX_KALLSYMS_TOKEN_LEN
            && (uint16_t)(uBuf.au16[i + 3] - uBuf.au16[i + 2] - 1U) <= (uint16_t)LNX_MAX_KALLSYMS_TOKEN_LEN
            && (uint16_t)(uBuf.au16[i + 4] - uBuf.au16[i + 3] - 1U) <= (uint16_t)LNX_MAX_KALLSYMS_TOKEN_LEN
            && (uint16_t)(uBuf.au16[i + 5] - uBuf.au16[i + 4] - 1U) <= (uint16_t)LNX_MAX_KALLSYMS_TOKEN_LEN
            && (uint16_t)(uBuf.au16[i + 6] - uBuf.au16[i + 5] - 1U) <= (uint16_t)LNX_MAX_KALLSYMS_TOKEN_LEN
            )
        {
            pThis->AddrKernelTokenIndex = CurAddr;
            pVMM->pfnDBGFR3AddrAdd(&pThis->AddrKernelTokenIndex, i * sizeof(uint16_t));
            pThis->cbKernelTokenTable = i * sizeof(uint16_t);
            return VINF_SUCCESS;
        }

    Log(("dbgDiggerLinuxFindTokenIndex: Failed (%RGv..%RGv)\n", CurAddr.FlatPtr, CurAddr.FlatPtr + (RTGCUINTPTR)sizeof(uBuf)));
    return VERR_NOT_FOUND;
}


/**
 * Loads the kernel symbols from the given kallsyms offset table decoding the symbol names
 * (worker common for dbgDiggerLinuxLoadKernelSymbolsAbsolute() and dbgDiggerLinuxLoadKernelSymbolsRelative()).
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 * @param   uKernelStart        Flat kernel start address.
 * @param   cbKernel            Size of the kernel in bytes.
 * @param   pauSymOff           Pointer to the array of symbol offsets in the kallsyms table
 *                              relative to the start of the kernel.
 */
static int dbgDiggerLinuxLoadKernelSymbolsWorker(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis, RTGCUINTPTR uKernelStart,
                                                 RTGCUINTPTR cbKernel, RTGCUINTPTR *pauSymOff)
{
    uint8_t *pbNames = (uint8_t *)RTMemAllocZ(pThis->cbKernelNames);
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->AddrKernelNames, pbNames, pThis->cbKernelNames);
    if (RT_SUCCESS(rc))
    {
        char *pszzTokens = (char *)RTMemAllocZ(pThis->cbKernelTokenTable);
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->AddrKernelTokenTable, pszzTokens, pThis->cbKernelTokenTable);
        if (RT_SUCCESS(rc))
        {
            uint16_t *paoffTokens = (uint16_t *)RTMemAllocZ(256 * sizeof(uint16_t));
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->AddrKernelTokenIndex, paoffTokens, 256 * sizeof(uint16_t));
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create a module for the kernel.
                 */
                RTDBGMOD hMod;
                rc = RTDbgModCreate(&hMod, "vmlinux", cbKernel, 0 /*fFlags*/);
                if (RT_SUCCESS(rc))
                {
                    rc = RTDbgModSetTag(hMod, DIG_LNX_MOD_TAG); AssertRC(rc);
                    rc = VINF_SUCCESS;

                    /*
                     * Enumerate the symbols.
                     */
                    uint32_t        offName   = 0;
                    uint32_t        cLeft = pThis->cKernelSymbols;
                    while (cLeft-- > 0 && RT_SUCCESS(rc))
                    {
                        /* Decode the symbol name first. */
                        if (RT_LIKELY(offName < pThis->cbKernelNames))
                        {
                            uint8_t cbName = pbNames[offName++];
                            if (RT_LIKELY(offName + cbName <= pThis->cbKernelNames))
                            {
                                char     szSymbol[4096];
                                uint32_t offSymbol = 0;
                                while (cbName-- > 0)
                                {
                                    uint8_t  bEnc     = pbNames[offName++];
                                    uint16_t offToken = paoffTokens[bEnc];
                                    if (RT_LIKELY(offToken < pThis->cbKernelTokenTable))
                                    {
                                        const char *pszToken = &pszzTokens[offToken];
                                        char ch;
                                        while ((ch = *pszToken++) != '\0')
                                            if (offSymbol < sizeof(szSymbol) - 1)
                                                szSymbol[offSymbol++] = ch;
                                    }
                                    else
                                    {
                                        rc = VERR_INVALID_UTF8_ENCODING;
                                        break;
                                    }
                                }
                                szSymbol[offSymbol < sizeof(szSymbol) ? offSymbol : sizeof(szSymbol) - 1] = '\0';

                                /* The offset. */
                                RTGCUINTPTR uSymOff = *pauSymOff;
                                pauSymOff++;

                                /* Add it without the type char. */
                                if (uSymOff <= cbKernel)
                                {
                                    rc = RTDbgModSymbolAdd(hMod, &szSymbol[1], RTDBGSEGIDX_RVA, uSymOff,
                                                           0 /*cb*/, 0 /*fFlags*/, NULL);
                                    if (RT_FAILURE(rc))
                                    {
                                        if (   rc == VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE
                                            || rc == VERR_DBG_INVALID_RVA
                                            || rc == VERR_DBG_ADDRESS_CONFLICT
                                            || rc == VERR_DBG_DUPLICATE_SYMBOL)
                                        {
                                            Log2(("dbgDiggerLinuxLoadKernelSymbols: RTDbgModSymbolAdd(,%s,) failed %Rrc (ignored)\n", szSymbol, rc));
                                            rc = VINF_SUCCESS;
                                        }
                                        else
                                            Log(("dbgDiggerLinuxLoadKernelSymbols: RTDbgModSymbolAdd(,%s,) failed %Rrc\n", szSymbol, rc));
                                    }
                                }
                            }
                            else
                            {
                                rc = VERR_END_OF_STRING;
                                Log(("dbgDiggerLinuxLoadKernelSymbols: offName=%#x cLeft=%#x cbName=%#x cbKernelNames=%#x\n",
                                     offName, cLeft, cbName, pThis->cbKernelNames));
                            }
                        }
                        else
                        {
                            rc = VERR_END_OF_STRING;
                            Log(("dbgDiggerLinuxLoadKernelSymbols: offName=%#x cLeft=%#x cbKernelNames=%#x\n",
                                 offName, cLeft, pThis->cbKernelNames));
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
                        Log(("dbgDiggerLinuxLoadKernelSymbols: Failed: %Rrc\n", rc));
                    RTDbgModRelease(hMod);
                }
                else
                    Log(("dbgDiggerLinuxLoadKernelSymbols: RTDbgModCreate failed: %Rrc\n", rc));
            }
            else
                Log(("dbgDiggerLinuxLoadKernelSymbols: Reading token index at %RGv failed: %Rrc\n",
                     pThis->AddrKernelTokenIndex.FlatPtr, rc));
            RTMemFree(paoffTokens);
        }
        else
            Log(("dbgDiggerLinuxLoadKernelSymbols: Reading token table at %RGv failed: %Rrc\n",
                 pThis->AddrKernelTokenTable.FlatPtr, rc));
        RTMemFree(pszzTokens);
    }
    else
        Log(("dbgDiggerLinuxLoadKernelSymbols: Reading encoded names at %RGv failed: %Rrc\n",
             pThis->AddrKernelNames.FlatPtr, rc));
    RTMemFree(pbNames);

    return rc;
}

/**
 * Loads the kernel symbols from the kallsyms table if it contains absolute addresses
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 */
static int dbgDiggerLinuxLoadKernelSymbolsAbsolute(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis)
{
    /*
     * Allocate memory for temporary table copies, reading the tables as we go.
     */
    uint32_t const cbGuestAddr = pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
    void *pvAddresses = RTMemAllocZ(pThis->cKernelSymbols * cbGuestAddr);
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->AddrKernelAddresses,
                                    pvAddresses, pThis->cKernelSymbols * cbGuestAddr);
    if (RT_SUCCESS(rc))
    {
        /*
         * Figure out the kernel start and end and convert the absolute addresses to relative offsets.
         */
        RTGCUINTPTR uKernelStart = pThis->AddrKernelAddresses.FlatPtr;
        RTGCUINTPTR uKernelEnd   = pThis->AddrKernelTokenIndex.FlatPtr + 256 * sizeof(uint16_t);
        RTGCUINTPTR *pauSymOff   = (RTGCUINTPTR *)RTMemTmpAllocZ(pThis->cKernelSymbols * sizeof(RTGCUINTPTR));
        uint32_t    i;
        if (cbGuestAddr == sizeof(uint64_t))
        {
            uint64_t *pauAddrs = (uint64_t *)pvAddresses;
            for (i = 0; i < pThis->cKernelSymbols; i++)
                if (   pauAddrs[i] < uKernelStart
                    && LNX64_VALID_ADDRESS(pauAddrs[i])
                    && uKernelStart - pauAddrs[i] < LNX_MAX_KERNEL_SIZE)
                    uKernelStart = pauAddrs[i];

            for (i = pThis->cKernelSymbols - 1; i > 0; i--)
                if (   pauAddrs[i] > uKernelEnd
                    && LNX64_VALID_ADDRESS(pauAddrs[i])
                    && pauAddrs[i] - uKernelEnd < LNX_MAX_KERNEL_SIZE)
                    uKernelEnd = pauAddrs[i];

            for (i = 0; i < pThis->cKernelSymbols; i++)
                pauSymOff[i] = pauAddrs[i] - uKernelStart;
        }
        else
        {
            uint32_t *pauAddrs = (uint32_t *)pvAddresses;
            for (i = 0; i < pThis->cKernelSymbols; i++)
                if (   pauAddrs[i] < uKernelStart
                    && LNX32_VALID_ADDRESS(pauAddrs[i])
                    && uKernelStart - pauAddrs[i] < LNX_MAX_KERNEL_SIZE)
                    uKernelStart = pauAddrs[i];

            for (i = pThis->cKernelSymbols - 1; i > 0; i--)
                if (   pauAddrs[i] > uKernelEnd
                    && LNX32_VALID_ADDRESS(pauAddrs[i])
                    && pauAddrs[i] - uKernelEnd < LNX_MAX_KERNEL_SIZE)
                    uKernelEnd = pauAddrs[i];

            for (i = 0; i < pThis->cKernelSymbols; i++)
                pauSymOff[i] = pauAddrs[i] - uKernelStart;
        }

        RTGCUINTPTR cbKernel = uKernelEnd - uKernelStart;
        pThis->cbKernel = (uint32_t)cbKernel;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &pThis->AddrKernelBase, uKernelStart);
        Log(("dbgDiggerLinuxLoadKernelSymbolsAbsolute: uKernelStart=%RGv cbKernel=%#x\n", uKernelStart, cbKernel));

        rc = dbgDiggerLinuxLoadKernelSymbolsWorker(pUVM, pVMM, pThis, uKernelStart, cbKernel, pauSymOff);
        if (RT_FAILURE(rc))
            Log(("dbgDiggerLinuxLoadKernelSymbolsAbsolute: Loading symbols from given offset table failed: %Rrc\n", rc));
        RTMemTmpFree(pauSymOff);
    }
    else
        Log(("dbgDiggerLinuxLoadKernelSymbolsAbsolute: Reading symbol addresses at %RGv failed: %Rrc\n",
             pThis->AddrKernelAddresses.FlatPtr, rc));
    RTMemFree(pvAddresses);

    return rc;
}


/**
 * Loads the kernel symbols from the kallsyms table if it contains absolute addresses
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 */
static int dbgDiggerLinuxLoadKernelSymbolsRelative(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis)
{
    /*
     * Allocate memory for temporary table copies, reading the tables as we go.
     */
    int32_t *pai32Offsets = (int32_t *)RTMemAllocZ(pThis->cKernelSymbols * sizeof(int32_t));
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->AddrKernelAddresses,
                                    pai32Offsets, pThis->cKernelSymbols * sizeof(int32_t));
    if (RT_SUCCESS(rc))
    {
        /*
         * Figure out the kernel start and end and convert the absolute addresses to relative offsets.
         */
        RTGCUINTPTR uKernelStart = pThis->AddrKernelAddresses.FlatPtr;
        RTGCUINTPTR uKernelEnd   = pThis->AddrKernelTokenIndex.FlatPtr + 256 * sizeof(uint16_t);
        RTGCUINTPTR *pauSymOff   = (RTGCUINTPTR *)RTMemTmpAllocZ(pThis->cKernelSymbols * sizeof(RTGCUINTPTR));
        uint32_t    i;

        for (i = 0; i < pThis->cKernelSymbols; i++)
        {
            RTGCUINTPTR uSymAddr = dbgDiggerLinuxConvOffsetToAddr(pThis, pai32Offsets[i]);

            if (   uSymAddr < uKernelStart
                && (pThis->f64Bit ? LNX64_VALID_ADDRESS(uSymAddr) : LNX32_VALID_ADDRESS(uSymAddr))
                && uKernelStart - uSymAddr < LNX_MAX_KERNEL_SIZE)
                uKernelStart = uSymAddr;
        }

        for (i = pThis->cKernelSymbols - 1; i > 0; i--)
        {
            RTGCUINTPTR uSymAddr = dbgDiggerLinuxConvOffsetToAddr(pThis, pai32Offsets[i]);

            if (   uSymAddr > uKernelEnd
                && (pThis->f64Bit ? LNX64_VALID_ADDRESS(uSymAddr) : LNX32_VALID_ADDRESS(uSymAddr))
                && uSymAddr - uKernelEnd < LNX_MAX_KERNEL_SIZE)
                uKernelEnd = uSymAddr;

            /* Store the offset from the derived kernel start address. */
            pauSymOff[i] = uSymAddr - uKernelStart;
        }

        RTGCUINTPTR cbKernel = uKernelEnd - uKernelStart;
        pThis->cbKernel = (uint32_t)cbKernel;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &pThis->AddrKernelBase, uKernelStart);
        Log(("dbgDiggerLinuxLoadKernelSymbolsRelative: uKernelStart=%RGv cbKernel=%#x\n", uKernelStart, cbKernel));

        rc = dbgDiggerLinuxLoadKernelSymbolsWorker(pUVM, pVMM, pThis, uKernelStart, cbKernel, pauSymOff);
        if (RT_FAILURE(rc))
            Log(("dbgDiggerLinuxLoadKernelSymbolsRelative: Loading symbols from given offset table failed: %Rrc\n", rc));
        RTMemTmpFree(pauSymOff);
    }
    else
        Log(("dbgDiggerLinuxLoadKernelSymbolsRelative: Reading symbol addresses at %RGv failed: %Rrc\n",
             pThis->AddrKernelAddresses.FlatPtr, rc));
    RTMemFree(pai32Offsets);

    return rc;
}


/**
 * Loads the kernel symbols.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pThis               The Linux digger data.
 */
static int dbgDiggerLinuxLoadKernelSymbols(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERLINUX pThis)
{
    /*
     * First the kernel itself.
     */
    if (pThis->fRelKrnlAddr)
        return dbgDiggerLinuxLoadKernelSymbolsRelative(pUVM, pVMM, pThis);
    return dbgDiggerLinuxLoadKernelSymbolsAbsolute(pUVM, pVMM, pThis);
}


/*
 * The module structure changed it was easier to produce different code for
 * each version of the structure.  The C preprocessor rules!
 */
#define LNX_TEMPLATE_HEADER "DBGPlugInLinuxModuleCodeTmpl.cpp.h"

#define LNX_BIT_SUFFIX      _amd64
#define LNX_PTR_T           uint64_t
#define LNX_64BIT           1
#include "DBGPlugInLinuxModuleVerTmpl.cpp.h"

#define LNX_BIT_SUFFIX      _x86
#define LNX_PTR_T           uint32_t
#define LNX_64BIT           0
#include "DBGPlugInLinuxModuleVerTmpl.cpp.h"

#undef  LNX_TEMPLATE_HEADER

static const struct
{
    uint32_t    uVersion;
    bool        f64Bit;
    uint64_t  (*pfnProcessModule)(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGFADDRESS pAddrModule);
} g_aModVersions[] =
{
#define LNX_TEMPLATE_HEADER "DBGPlugInLinuxModuleTableEntryTmpl.cpp.h"

#define LNX_BIT_SUFFIX      _amd64
#define LNX_64BIT           1
#include "DBGPlugInLinuxModuleVerTmpl.cpp.h"

#define LNX_BIT_SUFFIX      _x86
#define LNX_64BIT           0
#include "DBGPlugInLinuxModuleVerTmpl.cpp.h"

#undef  LNX_TEMPLATE_HEADER
};


/**
 * Tries to find and process the module list.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 */
static int dbgDiggerLinuxLoadModules(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    /*
     * Locate the list head.
     */
    RTDBGAS     hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    RTDBGSYMBOL SymInfo;
    int rc = RTDbgAsSymbolByName(hAs, "vmlinux!modules", &SymInfo, NULL);
    RTDbgAsRelease(hAs);
    if (RT_FAILURE(rc))
        return VERR_NOT_FOUND;

    if (RT_FAILURE(rc))
    {
        LogRel(("dbgDiggerLinuxLoadModules: Failed to locate the module list (%Rrc).\n", rc));
        return VERR_NOT_FOUND;
    }

    /*
     * Read the list anchor.
     */
    union
    {
        uint32_t volatile u32Pair[2];
        uint64_t u64Pair[2];
    } uListAnchor;
    DBGFADDRESS Addr;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SymInfo.Value),
                                &uListAnchor, pThis->f64Bit ? sizeof(uListAnchor.u64Pair) : sizeof(uListAnchor.u32Pair));
    if (RT_FAILURE(rc))
    {
        LogRel(("dbgDiggerLinuxLoadModules: Error reading list anchor at %RX64: %Rrc\n", SymInfo.Value, rc));
        return VERR_NOT_FOUND;
    }
    if (!pThis->f64Bit)
    {
        uListAnchor.u64Pair[1] = uListAnchor.u32Pair[1];
        ASMCompilerBarrier();
        uListAnchor.u64Pair[0] = uListAnchor.u32Pair[0];
    }

    if (pThis->uKrnlVer == 0)
    {
        LogRel(("dbgDiggerLinuxLoadModules: No valid kernel version given: %#x\n", pThis->uKrnlVer));
        return VERR_NOT_FOUND;
    }

    /*
     * Find the g_aModVersion entry that fits the best.
     * ASSUMES strict descending order by bitcount and version.
     */
    Assert(g_aModVersions[0].f64Bit == true);
    unsigned i = 0;
    if (!pThis->f64Bit)
        while (i < RT_ELEMENTS(g_aModVersions) && g_aModVersions[i].f64Bit)
            i++;
    while (   i < RT_ELEMENTS(g_aModVersions)
           && g_aModVersions[i].f64Bit == pThis->f64Bit
           && pThis->uKrnlVer < g_aModVersions[i].uVersion)
        i++;
    if (i >= RT_ELEMENTS(g_aModVersions))
    {
        LogRel(("dbgDiggerLinuxLoadModules: Failed to find anything matching version: %u.%u.%u\n",
                pThis->uKrnlVerMaj, pThis->uKrnlVerMin, pThis->uKrnlVerBld));
        return VERR_NOT_FOUND;
    }

    /*
     * Walk the list.
     */
    uint64_t uModAddr = uListAnchor.u64Pair[0];
    for (size_t iModule = 0; iModule < 4096 && uModAddr != SymInfo.Value && uModAddr != 0; iModule++)
        uModAddr = g_aModVersions[i].pfnProcessModule(pThis, pUVM, pVMM, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uModAddr));

    return VINF_SUCCESS;
}


/**
 * Checks if there is a likely kallsyms_names fragment at pHitAddr.
 *
 * @returns true if it's a likely fragment, false if not.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pHitAddr            The address where paNeedle was found.
 * @param   pabNeedle           The fragment we've been searching for.
 * @param   cbNeedle            The length of the fragment.
 */
static bool dbgDiggerLinuxIsLikelyNameFragment(PUVM pUVM, PCVMMR3VTABLE pVMM, PCDBGFADDRESS pHitAddr,
                                               uint8_t const *pabNeedle, uint8_t cbNeedle)
{
    /*
     * Examples of lead and tail bytes of our choosen needle in a randomly
     * picked kernel:
     *         k  o  b  j
     *     22  6b 6f 62 6a  aa
     *     fc  6b 6f 62 6a  aa
     *     82  6b 6f 62 6a  5f      - ascii trail byte (_).
     *     ee  6b 6f 62 6a  aa
     *     fc  6b 6f 62 6a  5f      - ascii trail byte (_).
     *  0a 74  6b 6f 62 6a  5f ea   - ascii lead (t) and trail (_) bytes.
     *  0b 54  6b 6f 62 6a  aa      - ascii lead byte (T).
     * ... omitting 29 samples similar to the last two ...
     *     d8  6b 6f 62 6a  aa
     *     d8  6b 6f 62 6a  aa
     *     d8  6b 6f 62 6a  aa
     *     d8  6b 6f 62 6a  aa
     *  f9 5f  6b 6f 62 6a  5f 94   - ascii lead and trail bytes (_)
     *  f9 5f  6b 6f 62 6a  0c      - ascii lead byte (_).
     *     fd  6b 6f 62 6a  0f
     *  ... enough.
     */
    uint8_t         abBuf[32];
    DBGFADDRESS     ReadAddr = *pHitAddr;
    pVMM->pfnDBGFR3AddrSub(&ReadAddr, 2);
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &ReadAddr, abBuf, 2 + cbNeedle + 2);
    if (RT_SUCCESS(rc))
    {
        if (memcmp(&abBuf[2], pabNeedle, cbNeedle) == 0) /* paranoia */
        {
            uint8_t const bLead = abBuf[1] == '_' || abBuf[1] == 'T' || abBuf[1] == 't' ? abBuf[0] : abBuf[1];
            uint8_t const offTail = 2 + cbNeedle;
            uint8_t const bTail = abBuf[offTail] == '_' ? abBuf[offTail] : abBuf[offTail + 1];
            if (   bLead >= 1 && (bLead < 0x20 || bLead >= 0x80)
                && bTail >= 1 && (bTail < 0x20 || bTail >= 0x80))
                return true;
            Log(("dbgDiggerLinuxIsLikelyNameFragment: failed at %RGv: bLead=%#x bTail=%#x (offTail=%#x)\n",
                 pHitAddr->FlatPtr, bLead, bTail, offTail));
        }
        else
            Log(("dbgDiggerLinuxIsLikelyNameFragment: failed at %RGv: Needle changed!\n", pHitAddr->FlatPtr));
    }
    else
        Log(("dbgDiggerLinuxIsLikelyNameFragment: failed at %RGv: %Rrc\n", pHitAddr->FlatPtr, rc));

    return false;
}

/**
 * Tries to find and load the kernel symbol table with the given needle.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pabNeedle           The needle to use for searching.
 * @param   cbNeedle            Size of the needle in bytes.
 */
static int dbgDiggerLinuxFindSymbolTableFromNeedle(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                   uint8_t const *pabNeedle, uint8_t cbNeedle)
{
    /*
     * Go looking for the kallsyms table.  If it's there, it will be somewhere
     * after the linux_banner symbol, so use it for starting the search.
     */
    int         rc      = VINF_SUCCESS;
    DBGFADDRESS CurAddr = pThis->AddrLinuxBanner;
    uint32_t    cbLeft  = LNX_MAX_KERNEL_SIZE;
    while (cbLeft > 4096)
    {
        DBGFADDRESS          HitAddr;
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &CurAddr, cbLeft, 1 /*uAlign*/,
                                    pabNeedle, cbNeedle, &HitAddr);
        if (RT_FAILURE(rc))
            break;
        if (dbgDiggerLinuxIsLikelyNameFragment(pUVM, pVMM, &HitAddr, pabNeedle, cbNeedle))
        {
            /* There will be another hit near by. */
            pVMM->pfnDBGFR3AddrAdd(&HitAddr, 1);
            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &HitAddr, LNX_MAX_KALLSYMS_NAMES_SIZE, 1 /*uAlign*/,
                                        pabNeedle, cbNeedle, &HitAddr);
            if (   RT_SUCCESS(rc)
                && dbgDiggerLinuxIsLikelyNameFragment(pUVM, pVMM, &HitAddr, pabNeedle, cbNeedle))
            {
                /*
                 * We've got a very likely candidate for a location inside kallsyms_names.
                 * Try find the start of it, that is to say, try find kallsyms_num_syms.
                 * kallsyms_num_syms is aligned on sizeof(unsigned long) boundrary
                 */
                rc = dbgDiggerLinuxFindStartOfNamesAndSymbolCount(pUVM, pVMM, pThis, &HitAddr);
                if (RT_SUCCESS(rc))
                    rc = dbgDiggerLinuxFindEndOfNamesAndMore(pUVM, pVMM, pThis, &HitAddr);
                if (RT_SUCCESS(rc))
                    rc = dbgDiggerLinuxFindTokenIndex(pUVM, pVMM, pThis);
                if (RT_SUCCESS(rc))
                    rc = dbgDiggerLinuxLoadKernelSymbols(pUVM, pVMM, pThis);
                if (RT_SUCCESS(rc))
                {
                    rc = dbgDiggerLinuxLoadModules(pThis, pUVM, pVMM);
                    break;
                }
            }
        }

        /*
         * Advance.
         */
        RTGCUINTPTR cbDistance = HitAddr.FlatPtr - CurAddr.FlatPtr + cbNeedle;
        if (RT_UNLIKELY(cbDistance >= cbLeft))
        {
            Log(("dbgDiggerLinuxInit: Failed to find kallsyms\n"));
            break;
        }
        cbLeft -= cbDistance;
        pVMM->pfnDBGFR3AddrAdd(&CurAddr, cbDistance);
    }

    return rc;
}

/**
 * Skips whitespace and comments in the given config returning the pointer
 * to the first non whitespace character.
 *
 * @returns Pointer to the first non whitespace character or NULL if the end
 *          of the string was reached.
 * @param   pszCfg              The config string.
 */
static const char *dbgDiggerLinuxCfgSkipWhitespace(const char *pszCfg)
{
    do
    {
        while (   *pszCfg != '\0'
               && (   RT_C_IS_SPACE(*pszCfg)
                   || *pszCfg == '\n'))
            pszCfg++;

        /* Do we have a comment? Skip it. */
        if (*pszCfg == '#')
        {
            while (   *pszCfg != '\n'
                   && *pszCfg != '\0')
                pszCfg++;
        }
    } while (   *pszCfg != '\0'
             && (   RT_C_IS_SPACE(*pszCfg)
                 || *pszCfg == '\n'
                 || *pszCfg == '#'));

    return pszCfg;
}

/**
 * Parses an identifier at the given position.
 *
 * @returns VBox status code.
 * @param   pszCfg              The config data.
 * @param   ppszCfgNext         Where to store the pointer to the data following the identifier.
 * @param   ppszIde             Where to store the pointer to the identifier on success.
 *                              Free with RTStrFree().
 */
static int dbgDiggerLinuxCfgParseIde(const char *pszCfg, const char **ppszCfgNext, char **ppszIde)
{
    int rc = VINF_SUCCESS;
    size_t cchIde = 0;

    while (   *pszCfg != '\0'
           && (   RT_C_IS_ALNUM(*pszCfg)
               || *pszCfg == '_'))
    {
        cchIde++;
        pszCfg++;
    }

    if (cchIde)
    {
        *ppszIde = RTStrDupN(pszCfg - cchIde, cchIde);
        if (!*ppszIde)
            rc = VERR_NO_STR_MEMORY;
    }

    *ppszCfgNext = pszCfg;
    return rc;
}

/**
 * Parses a value for a config item.
 *
 * @returns VBox status code.
 * @param   pszCfg              The config data.
 * @param   ppszCfgNext         Where to store the pointer to the data following the identifier.
 * @param   ppCfgItem           Where to store the created config item on success.
 */
static int dbgDiggerLinuxCfgParseVal(const char *pszCfg, const char **ppszCfgNext,
                                     PDBGDIGGERLINUXCFGITEM *ppCfgItem)
{
    int rc = VINF_SUCCESS;
    PDBGDIGGERLINUXCFGITEM pCfgItem = NULL;

    if (RT_C_IS_DIGIT(*pszCfg) || *pszCfg == '-')
    {
        /* Parse the number. */
        int64_t i64Num;
        rc = RTStrToInt64Ex(pszCfg, (char **)ppszCfgNext, 0, &i64Num);
        if (   RT_SUCCESS(rc)
            || rc == VWRN_TRAILING_CHARS
            || rc == VWRN_TRAILING_SPACES)
        {
            pCfgItem = (PDBGDIGGERLINUXCFGITEM)RTMemAllocZ(sizeof(DBGDIGGERLINUXCFGITEM));
            if (pCfgItem)
            {
                pCfgItem->enmType = DBGDIGGERLINUXCFGITEMTYPE_NUMBER;
                pCfgItem->u.i64Num = i64Num;
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    else if (*pszCfg == '\"')
    {
        /* Parse a string. */
        const char *pszCfgCur = pszCfg + 1;
        while (   *pszCfgCur != '\0'
               && *pszCfgCur != '\"')
            pszCfgCur++;

        if (*pszCfgCur == '\"')
        {
            pCfgItem = (PDBGDIGGERLINUXCFGITEM)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGDIGGERLINUXCFGITEM,
                                                                            u.aszString[pszCfgCur - pszCfg + 1]));
            if (pCfgItem)
            {
                pCfgItem->enmType = DBGDIGGERLINUXCFGITEMTYPE_STRING;
                RTStrCopyEx(&pCfgItem->u.aszString[0], pszCfgCur - pszCfg + 1, pszCfg, pszCfgCur - pszCfg);
                *ppszCfgNext = pszCfgCur + 1;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_INVALID_STATE;
    }
    else if (   *pszCfg == 'y'
             || *pszCfg == 'm')
    {
        /* Included or module. */
        pCfgItem = (PDBGDIGGERLINUXCFGITEM)RTMemAllocZ(sizeof(DBGDIGGERLINUXCFGITEM));
        if (pCfgItem)
        {
            pCfgItem->enmType = DBGDIGGERLINUXCFGITEMTYPE_FLAG;
            pCfgItem->u.fModule = *pszCfg == 'm';
        }
        else
            rc = VERR_NO_MEMORY;
        pszCfg++;
        *ppszCfgNext = pszCfg;
    }
    else
        rc = VERR_INVALID_STATE;

    if (RT_SUCCESS(rc))
        *ppCfgItem = pCfgItem;
    else if (pCfgItem)
        RTMemFree(pCfgItem);

    return rc;
}

/**
 * Parses the given kernel config and creates the config database.
 *
 * @returns VBox status code
 * @param   pThis               The Linux digger data.
 * @param   pszCfg              The config string.
 */
static int dbgDiggerLinuxCfgParse(PDBGDIGGERLINUX pThis, const char *pszCfg)
{
    int rc = VINF_SUCCESS;

    /*
     * The config is a text file with the following elements:
     *     # starts a comment which goes till the end of the line
     *     <Ide>=<val> where <Ide> is an identifier consisting of
     *                 alphanumerical characters (including _)
     *     <val> denotes the value for the identifier and can have the following
     *           formats:
     *               (-)[0-9]* for numbers
     *               "..."     for a string value
     *               m         when a feature is enabled as a module
     *               y         when a feature is enabled
     * Newlines are used as a separator between values and mark the end
     * of a comment
     */
    const char *pszCfgCur = pszCfg;
    while (   RT_SUCCESS(rc)
           && *pszCfgCur != '\0')
    {
        /* Start skipping the whitespace. */
        pszCfgCur = dbgDiggerLinuxCfgSkipWhitespace(pszCfgCur);
        if (   pszCfgCur
            && *pszCfgCur != '\0')
        {
            char *pszIde = NULL;
            /* Must be an identifier, parse it. */
            rc = dbgDiggerLinuxCfgParseIde(pszCfgCur, &pszCfgCur, &pszIde);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Skip whitespace again (shouldn't be required because = follows immediately
                 * in the observed configs).
                 */
                pszCfgCur = dbgDiggerLinuxCfgSkipWhitespace(pszCfgCur);
                if (   pszCfgCur
                    && *pszCfgCur == '=')
                {
                    pszCfgCur++;
                    pszCfgCur = dbgDiggerLinuxCfgSkipWhitespace(pszCfgCur);
                    if (   pszCfgCur
                        && *pszCfgCur != '\0')
                    {
                        /* Get the value. */
                        PDBGDIGGERLINUXCFGITEM pCfgItem = NULL;
                        rc = dbgDiggerLinuxCfgParseVal(pszCfgCur, &pszCfgCur, &pCfgItem);
                        if (RT_SUCCESS(rc))
                        {
                            pCfgItem->Core.pszString = pszIde;
                            bool fRc = RTStrSpaceInsert(&pThis->hCfgDb, &pCfgItem->Core);
                            if (!fRc)
                            {
                                RTStrFree(pszIde);
                                RTMemFree(pCfgItem);
                                rc = VERR_INVALID_STATE;
                            }
                        }
                    }
                    else
                        rc = VERR_EOF;
                }
                else
                    rc = VERR_INVALID_STATE;
            }

            if (RT_FAILURE(rc))
                RTStrFree(pszIde);
        }
        else
            break; /* Reached the end of the config. */
    }

    if (RT_FAILURE(rc))
        dbgDiggerLinuxCfgDbDestroy(pThis);

    return rc;
}

/**
 * Decompresses the given config and validates the UTF-8 encoding.
 *
 * @returns VBox status code.
 * @param   pbCfgComp           The compressed config.
 * @param   cbCfgComp           Size of the compressed config.
 * @param   ppszCfg             Where to store the pointer to the decompressed config
 *                              on success.
 */
static int dbgDiggerLinuxCfgDecompress(const uint8_t *pbCfgComp, size_t cbCfgComp, char **ppszCfg)
{
    int rc = VINF_SUCCESS;
    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;

    rc = RTVfsIoStrmFromBuffer(RTFILE_O_READ, pbCfgComp, cbCfgComp, &hVfsIos);
    if (RT_SUCCESS(rc))
    {
        RTVFSIOSTREAM hVfsIosDecomp = NIL_RTVFSIOSTREAM;
        rc = RTZipGzipDecompressIoStream(hVfsIos, RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR, &hVfsIosDecomp);
        if (RT_SUCCESS(rc))
        {
            char *pszCfg = NULL;
            size_t cchCfg = 0;
            size_t cbRead = 0;

            do
            {
                uint8_t abBuf[_64K];
                rc = RTVfsIoStrmRead(hVfsIosDecomp, abBuf, sizeof(abBuf), true /*fBlocking*/, &cbRead);
                if (rc == VINF_EOF && cbRead == 0)
                    rc = VINF_SUCCESS;
                if (   RT_SUCCESS(rc)
                    && cbRead > 0)
                {
                    /* Append data. */
                    char *pszCfgNew = pszCfg;
                    rc = RTStrRealloc(&pszCfgNew, cchCfg + cbRead + 1);
                    if (RT_SUCCESS(rc))
                    {
                        pszCfg = pszCfgNew;
                        memcpy(pszCfg + cchCfg, &abBuf[0], cbRead);
                        cchCfg += cbRead;
                        pszCfg[cchCfg] = '\0'; /* Enforce string termination. */
                    }
                }
            } while (RT_SUCCESS(rc) && cbRead > 0);

            if (RT_SUCCESS(rc))
                *ppszCfg = pszCfg;
            else if (RT_FAILURE(rc) && pszCfg)
                RTStrFree(pszCfg);

            RTVfsIoStrmRelease(hVfsIosDecomp);
        }
        RTVfsIoStrmRelease(hVfsIos);
    }

    return rc;
}

/**
 * Reads and decodes the compressed kernel config.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   pAddrStart          The start address of the compressed config.
 * @param   cbCfgComp           The size of the compressed config.
 */
static int dbgDiggerLinuxCfgDecode(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                   PCDBGFADDRESS pAddrStart, size_t cbCfgComp)
{
    int rc = VINF_SUCCESS;
    uint8_t *pbCfgComp = (uint8_t *)RTMemTmpAlloc(cbCfgComp);
    if (!pbCfgComp)
        return VERR_NO_MEMORY;

    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pAddrStart, pbCfgComp, cbCfgComp);
    if (RT_SUCCESS(rc))
    {
        char *pszCfg = NULL;
        rc = dbgDiggerLinuxCfgDecompress(pbCfgComp, cbCfgComp, &pszCfg);
        if (RT_SUCCESS(rc))
        {
            if (RTStrIsValidEncoding(pszCfg))
                rc = dbgDiggerLinuxCfgParse(pThis, pszCfg);
            else
                rc = VERR_INVALID_UTF8_ENCODING;
            RTStrFree(pszCfg);
        }
    }

    RTMemFree(pbCfgComp);
    return rc;
}

/**
 * Tries to find the compressed kernel config in the kernel address space
 * and sets up the config database.
 *
 * @returns VBox status code.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 */
static int dbgDiggerLinuxCfgFind(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    /*
     * Go looking for the IKCFG_ST string which indicates the start
     * of the compressed config file.
     */
    static const uint8_t s_abCfgNeedleStart[] = "IKCFG_ST";
    static const uint8_t s_abCfgNeedleEnd[] = "IKCFG_ED";
    int         rc      = VINF_SUCCESS;
    DBGFADDRESS CurAddr = pThis->AddrLinuxBanner;
    uint32_t    cbLeft  = LNX_MAX_KERNEL_SIZE;
    while (cbLeft > 4096)
    {
        DBGFADDRESS HitAddrStart;
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &CurAddr, cbLeft, 1 /*uAlign*/,
                                    s_abCfgNeedleStart, sizeof(s_abCfgNeedleStart) - 1, &HitAddrStart);
        if (RT_FAILURE(rc))
            break;

        /* Check for the end marker which shouldn't be that far away. */
        pVMM->pfnDBGFR3AddrAdd(&HitAddrStart, sizeof(s_abCfgNeedleStart) - 1);
        DBGFADDRESS HitAddrEnd;
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /* idCpu */, &HitAddrStart, LNX_MAX_COMPRESSED_CFG_SIZE,
                                    1 /* uAlign */, s_abCfgNeedleEnd, sizeof(s_abCfgNeedleEnd) - 1, &HitAddrEnd);
        if (RT_SUCCESS(rc))
        {
            /* Allocate a buffer to hold the compressed data between the markers and fetch it. */
            RTGCUINTPTR cbCfg = HitAddrEnd.FlatPtr - HitAddrStart.FlatPtr;
            Assert(cbCfg == (size_t)cbCfg);
            rc = dbgDiggerLinuxCfgDecode(pThis, pUVM, pVMM, &HitAddrStart, cbCfg);
            if (RT_SUCCESS(rc))
                break;
        }

        /*
         * Advance.
         */
        RTGCUINTPTR cbDistance = HitAddrStart.FlatPtr - CurAddr.FlatPtr + sizeof(s_abCfgNeedleStart) - 1;
        if (RT_UNLIKELY(cbDistance >= cbLeft))
        {
            LogFunc(("Failed to find compressed kernel config\n"));
            break;
        }
        cbLeft -= cbDistance;
        pVMM->pfnDBGFR3AddrAdd(&CurAddr, cbDistance);
    }

    return rc;
}

/**
 * Probes for a Linux kernel starting at the given address.
 *
 * @returns Flag whether something which looks like a valid Linux kernel was found.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 * @param   uAddrStart          The address to start scanning at.
 * @param   cbScan              How much to scan.
 */
static bool dbgDiggerLinuxProbeWithAddr(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                        RTGCUINTPTR uAddrStart, size_t cbScan)
{
    /*
     * Look for "Linux version " at the start of the rodata segment.
     * Hope that this comes before any message buffer or other similar string.
     */
    DBGFADDRESS KernelAddr;
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &KernelAddr, uAddrStart);
    DBGFADDRESS HitAddr;
    int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &KernelAddr, cbScan, 1,
                                    g_abLinuxVersion, sizeof(g_abLinuxVersion) - 1, &HitAddr);
    if (RT_SUCCESS(rc))
    {
        char szTmp[128];
        char const *pszX = &szTmp[sizeof(g_abLinuxVersion) - 1];
        rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &HitAddr, szTmp, sizeof(szTmp));
        if (    RT_SUCCESS(rc)
            &&  (   (   pszX[0] == '2'  /* 2.x.y with x in {0..6} */
                     && pszX[1] == '.'
                     && pszX[2] >= '0'
                     && pszX[2] <= '6')
                 || (   pszX[0] >= '3'  /* 3.x, 4.x, ... 9.x */
                     && pszX[0] <= '9'
                     && pszX[1] == '.'
                     && pszX[2] >= '0'
                     && pszX[2] <= '9')
                 )
            )
        {
            pThis->AddrKernelBase  = KernelAddr;
            pThis->AddrLinuxBanner = HitAddr;
            return true;
        }
    }

    return false;
}

/**
 * Probes for a Linux kernel which has KASLR enabled.
 *
 * @returns Flag whether a possible candidate location was found.
 * @param   pThis               The Linux digger data.
 * @param   pUVM                The user mode VM handle.
 * @param   pVMM                The VMM function table.
 */
static bool dbgDiggerLinuxProbeKaslr(PDBGDIGGERLINUX pThis, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    /**
     * With KASLR the kernel is loaded at a different address at each boot making detection
     * more difficult for us.
     *
     * The randomization is done in arch/x86/boot/compressed/kaslr.c:choose_random_location() (as of Nov 2017).
     * At the end of the method a random offset is chosen using find_random_virt_addr() which is added to the
     * kernel map start in the caller (the start of the kernel depends on the bit size, see LNX32_KERNEL_ADDRESS_START
     * and LNX64_KERNEL_ADDRESS_START for 32bit and 64bit kernels respectively).
     * The lowest offset possible is LOAD_PHYSICAL_ADDR which is defined in arch/x86/include/asm/boot.h
     * using CONFIG_PHYSICAL_START aligned to CONFIG_PHYSICAL_ALIGN.
     * The default CONFIG_PHYSICAL_START and CONFIG_PHYSICAL_ALIGN are both 0x1000000 no matter whether a 32bit
     * or a 64bit kernel is used. So the lowest offset to the kernel start address is 0x1000000.
     * The find_random_virt_addr() the number of possible slots where the kernel can be placed based on the image size
     * is calculated using the following formula:
     *    cSlots = ((KERNEL_IMAGE_SIZE - 0x1000000 (minimum) - image_size) / 0x1000000 (CONFIG_PHYSICAL_ALIGN)) + 1
     *
     * KERNEL_IMAGE_SIZE is 1GB for 64bit kernels and 512MB for 32bit kernels, so the maximum number of slots (resulting
     * in the largest possible offset) can be achieved when image_size (which contains the real size of the kernel image
     * which is unknown for us) goes to 0 and a 1GB KERNEL_IMAGE_SIZE is assumed. With that the biggest cSlots which can be
     * achieved is 64. The chosen random offset is taken from a random long integer using kaslr_get_random_long() modulo the
     * number of slots which selects a slot between 0 and 63. The final offset is calculated using:
     *    offAddr = random_addr * 0x1000000 (CONFIG_PHYSICAL_ALIGN) + 0x1000000 (minimum)
     *
     * So the highest offset the kernel can start is 0x40000000 which is 1GB (plus the maximum kernel size we defined).
     */
    if (dbgDiggerLinuxProbeWithAddr(pThis, pUVM, pVMM, LNX64_KERNEL_ADDRESS_START, _1G + LNX_MAX_KERNEL_SIZE))
        return true;

    /*
     * 32bit variant, makes sure we don't exceed the 4GB address space or DBGFR3MemScan() returns VERR_DBGF_MEM_NOT_FOUND immediately
     * without searching the remainder of the address space.
     *
     * The default split is 3GB userspace and 1GB kernel, so we just search the entire upper 1GB kernel space.
     */
    if (dbgDiggerLinuxProbeWithAddr(pThis, pUVM, pVMM, LNX32_KERNEL_ADDRESS_START, _4G - LNX32_KERNEL_ADDRESS_START))
        return true;

    return false;
}

/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerLinuxInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
    Assert(!pThis->fValid);

    char szVersion[256] = "Linux version 4.19.0";
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &pThis->AddrLinuxBanner, &szVersion[0], sizeof(szVersion));
    if (RT_SUCCESS(rc))
    {
        /*
         * Get a numerical version number.
         */
        const char *pszVersion = szVersion;
        while (*pszVersion && !RT_C_IS_DIGIT(*pszVersion))
            pszVersion++;

        size_t   offVersion = 0;
        uint32_t uMajor = 0;
        while (pszVersion[offVersion] && RT_C_IS_DIGIT(pszVersion[offVersion]))
            uMajor = uMajor * 10 + pszVersion[offVersion++] - '0';

        if (pszVersion[offVersion] == '.')
            offVersion++;

        uint32_t uMinor = 0;
        while (pszVersion[offVersion] && RT_C_IS_DIGIT(pszVersion[offVersion]))
            uMinor = uMinor * 10 + pszVersion[offVersion++] - '0';

        if (pszVersion[offVersion] == '.')
            offVersion++;

        uint32_t uBuild = 0;
        while (pszVersion[offVersion] && RT_C_IS_DIGIT(pszVersion[offVersion]))
            uBuild = uBuild * 10 + pszVersion[offVersion++] - '0';

        pThis->uKrnlVer = LNX_MK_VER(uMajor, uMinor, uBuild);
        pThis->uKrnlVerMaj = uMajor;
        pThis->uKrnlVerMin = uMinor;
        pThis->uKrnlVerBld = uBuild;
        if (pThis->uKrnlVer == 0)
            LogRel(("dbgDiggerLinuxInit: Failed to parse version string: %s\n", pszVersion));
    }

    /*
     * Assume 64-bit kernels all live way beyond 32-bit address space.
     */
    pThis->f64Bit = pThis->AddrLinuxBanner.FlatPtr > UINT32_MAX;
    pThis->fRelKrnlAddr = false;

    pThis->hCfgDb = NULL;

    /*
     * Try to find the compressed kernel config and parse it before we try
     * to get the symbol table, the config database is required to select
     * the method to use.
     */
    rc = dbgDiggerLinuxCfgFind(pThis, pUVM, pVMM);
    if (RT_FAILURE(rc))
        LogFlowFunc(("Failed to find kernel config (%Rrc), no config database available\n", rc));

    static const uint8_t s_abNeedle[] = "kobj";
    rc = dbgDiggerLinuxFindSymbolTableFromNeedle(pThis, pUVM, pVMM, s_abNeedle, sizeof(s_abNeedle) - 1);
    if (RT_FAILURE(rc))
    {
        /* Try alternate needle (seen on older x86 Linux kernels). */
        static const uint8_t s_abNeedleAlt[] = "kobjec";
        rc = dbgDiggerLinuxFindSymbolTableFromNeedle(pThis, pUVM, pVMM, s_abNeedleAlt, sizeof(s_abNeedleAlt) - 1);
        if (RT_FAILURE(rc))
        {
            static const uint8_t s_abNeedleOSuseX86[] = "nmi"; /* OpenSuSe 10.2 x86 */
            rc = dbgDiggerLinuxFindSymbolTableFromNeedle(pThis, pUVM, pVMM, s_abNeedleOSuseX86, sizeof(s_abNeedleOSuseX86) - 1);
            if (RT_FAILURE(rc))
                LogRel(("dbgDiggerLinuxInit: Failed to find symbol table from needle kobj, kobjec or nmi -> %Rrc\n", rc));
        }
    }

    pThis->fValid = true;
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerLinuxProbe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;

    for (unsigned i = 0; i < RT_ELEMENTS(g_au64LnxKernelAddresses); i++)
    {
        if (dbgDiggerLinuxProbeWithAddr(pThis, pUVM, pVMM, g_au64LnxKernelAddresses[i], LNX_MAX_KERNEL_SIZE))
            return true;
    }

    /* Maybe the kernel uses KASLR. */
    if (dbgDiggerLinuxProbeKaslr(pThis, pUVM, pVMM))
        return true;

    return false;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerLinuxDestruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerLinuxConstruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERLINUX pThis = (PDBGDIGGERLINUX)pvData;
    pThis->IDmesg.u32Magic = DBGFOSIDMESG_MAGIC;
    pThis->IDmesg.pfnQueryKernelLog = dbgDiggerLinuxIDmsg_QueryKernelLog;
    pThis->IDmesg.u32EndMagic = DBGFOSIDMESG_MAGIC;

    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerLinux =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGERLINUX),
    /* .szName = */                 "Linux",
    /* .pfnConstruct = */           dbgDiggerLinuxConstruct,
    /* .pfnDestruct = */            dbgDiggerLinuxDestruct,
    /* .pfnProbe = */               dbgDiggerLinuxProbe,
    /* .pfnInit = */                dbgDiggerLinuxInit,
    /* .pfnRefresh = */             dbgDiggerLinuxRefresh,
    /* .pfnTerm = */                dbgDiggerLinuxTerm,
    /* .pfnQueryVersion = */        dbgDiggerLinuxQueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerLinuxQueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerLinuxStackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};

