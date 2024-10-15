/* $Id: DBGPlugInWinNt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInWindows - Debugger and Guest OS Digger Plugin For Windows NT.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/cpumctx.h>
#include <VBox/vmm/mm.h>
#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING
# include <VBox/vmm/vmapi.h>
# include <VBox/dis.h>
#endif
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/ctype.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/formats/pecoff.h>
#include <iprt/formats/mz.h>
#include <iprt/nt/nt-structures.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name Internal WinNT structures
 * @{ */
/**
 * PsLoadedModuleList entry for 32-bit NT aka LDR_DATA_TABLE_ENTRY.
 * Tested with XP.
 *
 * See comments in NTMTE64.
 */
typedef struct NTMTE32
{
    struct
    {
        uint32_t    Flink;
        uint32_t    Blink;
    }               InLoadOrderLinks,
                    InMemoryOrderModuleList,
                    InInitializationOrderModuleList;
    uint32_t        DllBase;
    uint32_t        EntryPoint;
    /** @note This field is not a size in NT 3.1. It's NULL for images loaded by the
     *        boot loader, for other images it looks like some kind of pointer.  */
    uint32_t        SizeOfImage;
    struct
    {
        uint16_t    Length;
        uint16_t    MaximumLength;
        uint32_t    Buffer;
    }               FullDllName,
                    BaseDllName;
    uint32_t        Flags;                              /**< NTMTE_F_XXX  */
    uint16_t        LoadCount;
    uint16_t        TlsIndex;
    /* ... there is more ... */
} NTMTE32;
typedef NTMTE32 *PNTMTE32;

/**
 * PsLoadedModuleList entry for 64-bit NT aka LDR_DATA_TABLE_ENTRY.
 *
 * Starting with XP it it's specialized for the kernel as KLDR_DATA_TABLE_ENTRY
 * with compatible layout up to a point, the non-K version is used in user land.
 * The Flags values probably differs a bit by now.
 */
typedef struct NTMTE64
{
    struct
    {
        uint64_t    Flink;
        uint64_t    Blink;
    }               InLoadOrderLinks,                  /**< 0x00 */
                    InMemoryOrderModuleList,           /**< 0x10 */
                    InInitializationOrderModuleList;   /**< 0x20 */
    uint64_t        DllBase;                           /**< 0x30 */
    uint64_t        EntryPoint;                        /**< 0x38 */
    uint32_t        SizeOfImage;                       /**< 0x40 */
    uint32_t        Alignment;                         /**< 0x44 */
    struct
    {
        uint16_t    Length;                            /**< 0x48,0x58 */
        uint16_t    MaximumLength;                     /**< 0x4a,0x5a */
        uint32_t    Alignment;                         /**< 0x4c,0x5c */
        uint64_t    Buffer;                            /**< 0x50,0x60 */
    }               FullDllName,                       /**< 0x48 */
                    BaseDllName;                       /**< 0x58 */
    uint32_t        Flags;                             /**< 0x68 NTMTE_F_XXX */
    uint16_t        LoadCount;                         /**< 0x6c */
    uint16_t        TlsIndex;                          /**< 0x6e */
    /* ... there is more ... */
} NTMTE64;
typedef NTMTE64 *PNTMTE64;

#define NTMTE_F_FORCE_INTEGRITY                     0x20 /* copy of IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY */

/** MTE union. */
typedef union NTMTE
{
    NTMTE32         vX_32;
    NTMTE64         vX_64;
} NTMTE;
typedef NTMTE *PNTMTE;


/**
 * The essential bits of the KUSER_SHARED_DATA structure.
 */
typedef struct NTKUSERSHAREDDATA
{
    uint32_t        TickCountLowDeprecated;
    uint32_t        TickCountMultiplier;
    struct
    {
        uint32_t    LowPart;
        int32_t     High1Time;
        int32_t     High2Time;

    }               InterruptTime,
                    SystemTime,
                    TimeZoneBias;
    uint16_t        ImageNumberLow;
    uint16_t        ImageNumberHigh;
    RTUTF16         NtSystemRoot[260];
    uint32_t        MaxStackTraceDepth;
    uint32_t        CryptoExponent;
    uint32_t        TimeZoneId;
    uint32_t        LargePageMinimum;
    uint32_t        Reserved2[6];
    uint32_t        NtBuildNumber;
    uint32_t        NtProductType;
    uint8_t         ProductTypeIsValid;
    uint8_t         abPadding[3];
    uint32_t        NtMajorVersion;
    uint32_t        NtMinorVersion;
    /* uint8_t         ProcessorFeatures[64];
    ...
    */
} NTKUSERSHAREDDATA;
typedef NTKUSERSHAREDDATA *PNTKUSERSHAREDDATA;

/** KI_USER_SHARED_DATA for i386 */
#define NTKUSERSHAREDDATA_WINNT32   UINT32_C(0xffdf0000)
/** KI_USER_SHARED_DATA for AMD64 */
#define NTKUSERSHAREDDATA_WINNT64   UINT64_C(0xfffff78000000000)

/** NTKUSERSHAREDDATA::NtProductType */
typedef enum NTPRODUCTTYPE
{
    kNtProductType_Invalid = 0,
    kNtProductType_WinNt = 1,
    kNtProductType_LanManNt,
    kNtProductType_Server
} NTPRODUCTTYPE;


/** NT image header union. */
typedef union NTHDRSU
{
    IMAGE_NT_HEADERS32  vX_32;
    IMAGE_NT_HEADERS64  vX_64;
} NTHDRS;
/** Pointer to NT image header union. */
typedef NTHDRS *PNTHDRS;
/** Pointer to const NT image header union. */
typedef NTHDRS const *PCNTHDRS;


/**
 * NT KD version block.
 */
typedef struct NTKDVERSIONBLOCK
{
    uint16_t        MajorVersion;
    uint16_t        MinorVersion;
    uint8_t         ProtocolVersion;
    uint8_t         KdSecondaryVersion;
    uint16_t        Flags;
    uint16_t        MachineType;
    uint8_t         MaxPacketType;
    uint8_t         MaxStateChange;
    uint8_t         MaxManipulate;
    uint8_t         Simulation;
    uint16_t        Unused;
    uint64_t        KernBase;
    uint64_t        PsLoadedModuleList;
    uint64_t        DebuggerDataList;
} NTKDVERSIONBLOCK;
/** Pointer to an NT KD version block. */
typedef NTKDVERSIONBLOCK *PNTKDVERSIONBLOCK;
/** Pointer to a const NT KD version block. */
typedef const NTKDVERSIONBLOCK *PCNTKDVERSIONBLOCK;

/** @} */



typedef enum DBGDIGGERWINNTVER
{
    DBGDIGGERWINNTVER_UNKNOWN,
    DBGDIGGERWINNTVER_3_1,
    DBGDIGGERWINNTVER_3_5,
    DBGDIGGERWINNTVER_4_0,
    DBGDIGGERWINNTVER_5_0,
    DBGDIGGERWINNTVER_5_1,
    DBGDIGGERWINNTVER_6_0
} DBGDIGGERWINNTVER;

/**
 * WinNT guest OS digger instance data.
 */
typedef struct DBGDIGGERWINNT
{
    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool                fValid;
    /** 32-bit (true) or 64-bit (false) */
    bool                f32Bit;
    /** Set if NT 3.1 was detected.
     * This implies both Misc.VirtualSize and NTMTE32::SizeOfImage are zero. */
    bool                fNt31;

    /** The NT version. */
    DBGDIGGERWINNTVER   enmVer;
    /** NTKUSERSHAREDDATA::NtProductType */
    NTPRODUCTTYPE       NtProductType;
    /** NTKUSERSHAREDDATA::NtMajorVersion */
    uint32_t            NtMajorVersion;
    /** NTKUSERSHAREDDATA::NtMinorVersion */
    uint32_t            NtMinorVersion;
    /** NTKUSERSHAREDDATA::NtBuildNumber */
    uint32_t            NtBuildNumber;

    /** The address of the bootmgr image if early during boot. */
    DBGFADDRESS         BootMgrAddr;
    /** The address of the winload.exe image if early during boot.
     * When this is set, f32Bit has also been determined.  */
    DBGFADDRESS         WinLoadAddr;

    /** The address of the ntoskrnl.exe image. */
    DBGFADDRESS         KernelAddr;
    /** The address of the ntoskrnl.exe module table entry. */
    DBGFADDRESS         KernelMteAddr;
    /** The address of PsLoadedModuleList. */
    DBGFADDRESS         PsLoadedModuleListAddr;

    /** Array of detected KPCR addresses for each vCPU. */
    PDBGFADDRESS        paKpcrAddr;
    /** Array of detected KPCRB addresses for each vCPU. */
    PDBGFADDRESS        paKpcrbAddr;

    /** The Windows NT specifics interface. */
    DBGFOSIWINNT        IWinNt;

#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING
    /** Breakpoint owner handle for the DbgPrint/vDbgPrint{,Ex}... interception. */
    DBGFBPOWNER         hBpOwnerDbgPrint;
    /** Breakpoint handle for the DbgPrint/vDbgPrint{,Ex}... interception. */
    DBGFBP              hBpDbgPrint;
#endif
} DBGDIGGERWINNT;
/** Pointer to the linux guest OS digger instance data. */
typedef DBGDIGGERWINNT *PDBGDIGGERWINNT;


/**
 * The WinNT digger's loader reader instance data.
 */
typedef struct DBGDIGGERWINNTRDR
{
    /** The VM handle (referenced). */
    PUVM                pUVM;
    /** The image base. */
    DBGFADDRESS         ImageAddr;
    /** The image size. */
    uint32_t            cbImage;
    /** The file offset of the SizeOfImage field in the optional header if it
     *  needs patching, otherwise set to UINT32_MAX. */
    uint32_t            offSizeOfImage;
    /** The correct image size. */
    uint32_t            cbCorrectImageSize;
    /** Number of entries in the aMappings table. */
    uint32_t            cMappings;
    /** Mapping hint. */
    uint32_t            iHint;
    /** Mapping file offset to memory offsets, ordered by file offset. */
    struct
    {
        /** The file offset. */
        uint32_t        offFile;
        /** The size of this mapping. */
        uint32_t        cbMem;
        /** The offset to the memory from the start of the image.  */
        uint32_t        offMem;
    } aMappings[1];
} DBGDIGGERWINNTRDR;
/** Pointer a WinNT loader reader instance data. */
typedef DBGDIGGERWINNTRDR *PDBGDIGGERWINNTRDR;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a 32-bit Windows NT kernel address */
#define WINNT32_VALID_ADDRESS(Addr)         ((Addr) >         UINT32_C(0x80000000) && (Addr) <         UINT32_C(0xfffff000))
/** Validates a 64-bit Windows NT kernel address */
#define WINNT64_VALID_ADDRESS(Addr)         ((Addr) > UINT64_C(0xffff800000000000) && (Addr) < UINT64_C(0xfffffffffffff000))
/** Validates a kernel address. */
#define WINNT_VALID_ADDRESS(pThis, Addr)    ((pThis)->f32Bit ? WINNT32_VALID_ADDRESS(Addr) : WINNT64_VALID_ADDRESS(Addr))
/** Versioned and bitness wrapper. */
#define WINNT_UNION(pThis, pUnion, Member)  ((pThis)->f32Bit ? (pUnion)->vX_32. Member : (pUnion)->vX_64. Member )

/** The length (in chars) of the kernel file name (no path). */
#define WINNT_KERNEL_BASE_NAME_LEN          12

/** WindowsNT on little endian ASCII systems. */
#define DIG_WINNT_MOD_TAG                   UINT64_C(0x54696e646f774e54)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerWinNtInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Kernel names. */
static const RTUTF16 g_wszKernelNames[][WINNT_KERNEL_BASE_NAME_LEN + 1] =
{
    { 'n', 't', 'o', 's', 'k', 'r', 'n', 'l', '.', 'e', 'x', 'e' }
};


#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING

/**
 * Queries the string from guest memory with the pointer in the given register, sanitizing it.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The CPU ID.
 * @param   enmReg          The register to query the string pointer from.
 * @param   pszBuf          Where to store the sanitized string.
 * @param   cbBuf           Size of the buffer in number of bytes.
 */
static int dbgDiggerWinNtDbgPrintQueryStringFromReg(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, char *pszBuf, size_t cbBuf)
{
    uint64_t u64RegPtr = 0;
    int rc = DBGFR3RegCpuQueryU64(pUVM, idCpu, enmReg, &u64RegPtr);
    if (   rc == VINF_SUCCESS
        || rc == VINF_DBGF_ZERO_EXTENDED_REGISTER) /* Being strict about what we expect here. */
    {
        DBGFADDRESS AddrStr;
        DBGFR3AddrFromFlat(pUVM, &AddrStr, u64RegPtr);
        rc = DBGFR3MemRead(pUVM, idCpu, &AddrStr, pszBuf, cbBuf);
        if (RT_SUCCESS(rc))
        {
            /* Check that there is a zero terminator and purge invalid encoding (expecting UTF-8 here). */
            size_t idx = 0;
            for (idx = 0; idx < cbBuf; idx++)
                if (pszBuf[idx] == '\0')
                    break;

            if (idx == cbBuf)
                pszBuf[cbBuf - 1] = '\0'; /* Force terminator, truncating the string. */
            else
                memset(&pszBuf[idx], 0, cbBuf - idx); /* Clear everything afterwards. */

            /* Purge the string encoding. */
            RTStrPurgeEncoding(pszBuf);
        }
    }
    else if (RT_SUCCESS(rc))
        rc = VERR_INVALID_STATE;

    return rc;
}


/**
 * @copydoc{FNDBGFBPHIT, Breakpoint callback for the DbgPrint interception.}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgDiggerWinNtDbgPrintHit(PVM pVM, VMCPUID idCpu, void *pvUserBp, DBGFBP hBp, PCDBGFBPPUB pBpPub, uint16_t fFlags)
{
    RT_NOREF(hBp, pBpPub, fFlags);
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvUserBp;
    PUVM pUVM = VMR3GetUVM(pVM);

    /*
     * The worker prototype looks like the following:
     *      vDbgPrintExWorker(PCCH Prefix, ULONG ComponentId, ULONG Level, PCCH Format, va_list arglist, BOOL fUnknown)
     *
     * Depending on the bitness the parameters are grabbed from the appropriate registers and stack locations.
     * For amd64 reading the following is recommended:
     *     https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019
     *     https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=vs-2019
     *     https://docs.microsoft.com/en-us/cpp/build/stack-usage?view=vs-2019
     *
     * @todo 32bit
     */
    int rc = VINF_SUCCESS;
    uint32_t idComponent = 0;
    uint32_t iLevel = 0;
    char aszPrefixStr[128]; /* Restricted size. */
    char aszFmtStr[_1K]; /* Restricted size. */
    DBGFADDRESS AddrVaList;
    if (!pThis->f32Bit)
    {
        /*
         * Grab the prefix, component, level, format string pointer from the registers and the argument list from the
         * stack (mind the home area for the register arguments).
         */
        rc = dbgDiggerWinNtDbgPrintQueryStringFromReg(pUVM, idCpu, DBGFREG_RCX, &aszPrefixStr[0], sizeof(aszPrefixStr));
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU32(pUVM, idCpu, DBGFREG_RDX, &idComponent);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU32(pUVM, idCpu, DBGFREG_R8, &iLevel);
        if (RT_SUCCESS(rc))
            rc = dbgDiggerWinNtDbgPrintQueryStringFromReg(pUVM, idCpu, DBGFREG_R9, &aszFmtStr[0], sizeof(aszFmtStr));
        if (RT_SUCCESS(rc))
        {
            /* Grabbing the pointer to the va list. The stack layout when we are here looks like (each entry is 64bit):
             *     +-------------+
             *     |     ...     |
             *     | VA list ptr |
             *     |  (arg3/r9)  |
             *     |  (arg2/r8)  |
             *     |  (arg1/rdx) |
             *     |  (arg0/rcx) |
             *     | return RIP  |
             *     +-------------+ <- RSP
             */
            uint64_t uRegRsp = 0;
            rc = DBGFR3RegCpuQueryU64(pUVM, idCpu, DBGFREG_RSP, &uRegRsp);
            if (rc == VINF_SUCCESS)
            {
                DBGFADDRESS AddrVaListPtr;
                RTGCUINTPTR GCPtrVaList = 0;

                DBGFR3AddrFromFlat(pUVM, &AddrVaListPtr, uRegRsp + 5 * sizeof(RTGCUINTPTR));
                rc = DBGFR3MemRead(pUVM, idCpu, &AddrVaListPtr, &GCPtrVaList, sizeof(GCPtrVaList));
                if (RT_SUCCESS(rc))
                    DBGFR3AddrFromFlat(pUVM, &AddrVaList, GCPtrVaList);
            }
            else
                rc = VERR_INVALID_STATE;
        }
    }
    else
        rc = VERR_NOT_IMPLEMENTED; /** @todo */

    if (RT_SUCCESS(rc))
    {
        LogRel(("DigWinNt/DbgPrint: Queried arguments %s %#x %u %s %RGv\n", &aszPrefixStr[0], idComponent, iLevel, &aszFmtStr[0], AddrVaList.FlatPtr));
        /** @todo Continue here. */
    }
    else
        LogRel(("DigWinNt/DbgPrint: Failed to query all arguments with rc=%Rrc\n", rc));

    return VINF_SUCCESS;
}


/**
 * Disassembles the given instruction and checks whether it is a call with a fixed address.
 *
 * @returns Flag whether the insturction at the given address is a call.
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   pAddrInsn       Guest address of the instruction.
 * @param   pAddrCall       Where to store the destination if the instruction is a call.
 */
static bool dbgDiggerWinNtDbgPrintWrapperInsnIsCall(PDBGDIGGERWINNT pThis, PUVM pUVM, PCDBGFADDRESS pAddrInsn, PDBGFADDRESS pAddrCall)
{
    DISSTATE DisState;
    RT_ZERO(DisState);

    /* Prefetch the instruction. */
    uint8_t abInstr[32];
    int rc = DBGFR3MemRead(pUVM, 0 /*idCpu*/, pAddrInsn, &abInstr[0], sizeof(abInstr));
    if (RT_SUCCESS(rc))
    {
        uint32_t cbInsn = 0;
        rc = DISInstr(&abInstr[0], pThis->f32Bit ? DISCPUMODE_32BIT : DISCPUMODE_64BIT, &DisState, &cbInsn);
        if (   RT_SUCCESS(rc)
            && DisState.pCurInstr->uOpcode == OP_CALL
            && DisState.Param1.fUse & DISUSE_IMMEDIATE)
        {
            if (DisState.Param1.fUse & (DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64))
                DBGFR3AddrFromFlat(pUVM, pAddrCall, DisState.Param1.uValue);
            else if (DisState.Param1.fUse & (DISUSE_IMMEDIATE32_REL | DISUSE_IMMEDIATE64_REL))
            {
                *pAddrCall = *pAddrInsn;
                DBGFR3AddrAdd(pAddrCall, DisState.Param1.uValue + cbInsn);
            }

            return true;
        }
    }

    return false;
}


/**
 * Tries to find the single call instruction of the DbgPrint/etc. worker in the given control flow graph
 * (single basic block assumed).
 *
 * @returns VBox status code.
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   hFlow           The control flow graph handle.
 * @param   pAddr           Where to store the worker address on success.
 */
static int dbgDiggerWinNtDbgPrintResolveWorker(PDBGDIGGERWINNT pThis, PUVM pUVM, DBGFFLOW hFlow, PDBGFADDRESS pAddr)
{
    DBGFFLOWBB hBb;
    int rc = DBGFR3FlowQueryStartBb(hFlow, &hBb);
    if (RT_SUCCESS(rc))
    {
        bool fCallFound = false;

        for (uint32_t i = 0; i < DBGFR3FlowBbGetInstrCount(hBb) && RT_SUCCESS(rc); i++)
        {
            DBGFADDRESS AddrInsn;
            uint32_t cbInsn;
            rc = DBGFR3FlowBbQueryInstr(hBb, i, &AddrInsn, &cbInsn, NULL);
            if (RT_SUCCESS(rc))
            {
                DBGFADDRESS AddrCall;
                if (dbgDiggerWinNtDbgPrintWrapperInsnIsCall(pThis, pUVM, &AddrInsn, &AddrCall))
                {
                    if (!fCallFound)
                    {
                        *pAddr = AddrCall;
                        fCallFound = true;
                    }
                    else
                    {
                        LogRel(("DigWinNt/DbgPrint: nt!vDbgPrintEx contains multiple call instructions!\n"));
                        rc = VERR_ALREADY_EXISTS;
                    }
                }
            }
        }

        DBGFR3FlowBbRelease(hBb);
    }

    return rc;
}


/**
 * Tries to resolve and hook into the worker for all the DbgPrint like wrappers to be able
 * to gather debug information from the system.
 *
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 */
static void dbgDiggerWinNtDbgPrintHook(PDBGDIGGERWINNT pThis, PUVM pUVM)
{
    /*
     * This is a multi step process:
     *     1. Try to resolve the address of vDbgPrint() (available since XP).
     *     2. Create a control flow graph from the code and verify the following assumptions:
     *         1. Only a single basic block.
     *         2. Just one call instruction.
     *         @todo More?
     *     3. Get the address from the called worker
     *     4. Set a hardware breakpoint with our callback.
     */
    RTDBGAS hAs = DBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    if (hAs != NIL_RTDBGAS)
    {
        RTDBGSYMBOL SymInfo;
        int rc = RTDbgAsSymbolByName(hAs, "nt!vDbgPrintEx", &SymInfo, NULL /*phMod*/);
        if (RT_SUCCESS(rc))
        {
            DBGFADDRESS Addr;
            DBGFR3AddrFromFlat(pUVM, &Addr, (RTGCPTR)SymInfo.Value);

            LogRel(("DigWinNt/DbgPrint: nt!vDbgPrintEx resolved to %RGv\n", SymInfo.Value));

            DBGFFLOW hCfg;
            rc = DBGFR3FlowCreate(pUVM, 0 /*idCpu*/, &Addr, 512 /*cbDisasmMax*/,
                                  0 /*fFlagsFlow*/, DBGF_DISAS_FLAGS_UNPATCHED_BYTES | DBGF_DISAS_FLAGS_ANNOTATE_PATCHED | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                                  &hCfg);
            if (RT_SUCCESS(rc))
            {
                /* Verify assumptions. */
                if (DBGFR3FlowGetBbCount(hCfg) == 1)
                {
                    rc = dbgDiggerWinNtDbgPrintResolveWorker(pThis, pUVM, hCfg, &Addr);
                    if (RT_SUCCESS(rc))
                    {
                        /* Try to hook the worker. */
                        LogRel(("DigWinNt/DbgPrint: Worker for nt!vDbgPrintEx resolved to %RGv\n", Addr.FlatPtr));
                        rc = DBGFR3BpOwnerCreate(pUVM, dbgDiggerWinNtDbgPrintHit, NULL /*pfnBpIoHit*/, &pThis->hBpOwnerDbgPrint);
                        if (RT_SUCCESS(rc))
                        {
                            rc = DBGFR3BpSetInt3Ex(pUVM, pThis->hBpOwnerDbgPrint, pThis, 0 /*idCpu*/, &Addr, DBGF_BP_F_DEFAULT,
                                                   0 /*iHitTrigger*/, 0 /*iHitDisable*/, &pThis->hBpDbgPrint);
                            if (RT_SUCCESS(rc))
                                LogRel(("DigWinNt/DbgPrint: Hooked nt!vDbgPrintEx worker hBp=%#x\n", pThis->hBpDbgPrint));
                            else
                            {
                                LogRel(("DigWinNt/DbgPrint: Setting hardware breakpoint for nt!vDbgPrintEx worker failed with rc=%Rrc\n", rc));
                                int rc2 = DBGFR3BpOwnerDestroy(pUVM, pThis->hBpOwnerDbgPrint);
                                pThis->hBpOwnerDbgPrint = NIL_DBGFBPOWNER;
                                AssertRC(rc2);
                            }
                        }
                    }
                    /* else LogRel() already done */
                }
                else
                    LogRel(("DigWinNt/DbgPrint: Control flow graph for nt!vDbgPrintEx has more than one basic block (%u)\n",
                            DBGFR3FlowGetBbCount(hCfg)));

                DBGFR3FlowRelease(hCfg);
            }
            else
                LogRel(("DigWinNt/DbgPrint: Failed to create control flow graph from nt!vDbgPrintEx rc=%Rrc\n", rc));
        }
        else
            LogRel(("DigWinNt/DbgPrint: Failed to resolve nt!vDbgPrintEx -> rc=%Rrc\n", rc));
        RTDbgAsRelease(hAs);
    }
    else
        LogRel(("DigWinNt/DbgPrint: Failed to resolve kernel address space handle\n"));
}

#endif /* VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING */

/**
 * Tries to resolve the KPCR and KPCRB addresses for each vCPU.
 *
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   pVMM            The VMM function table.
 */
static void dbgDiggerWinNtResolveKpcr(PDBGDIGGERWINNT pThis, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    /*
     * Getting at the KPCR and KPCRB is explained here:
     *     https://www.geoffchappell.com/studies/windows/km/ntoskrnl/structs/kpcr.htm
     * Together with the available offsets from:
     *     https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/shared/ksamd64.inc#L883
     * we can verify that the found addresses are valid by cross checking that the GDTR and self reference
     * match what we expect.
     */
    VMCPUID cCpus = pVMM->pfnDBGFR3CpuGetCount(pUVM);
    pThis->paKpcrAddr = (PDBGFADDRESS)RTMemAllocZ(cCpus * 2 * sizeof(DBGFADDRESS));
    if (RT_LIKELY(pThis->paKpcrAddr))
    {
        pThis->paKpcrbAddr = &pThis->paKpcrAddr[cCpus];

        /* Work each CPU, unexpected values in each CPU make the whole thing fail to play safe. */
        int rc = VINF_SUCCESS;
        for (VMCPUID idCpu = 0; (idCpu < cCpus) && RT_SUCCESS(rc); idCpu++)
        {
            PDBGFADDRESS pKpcrAddr = &pThis->paKpcrAddr[idCpu];
            PDBGFADDRESS pKpcrbAddr = &pThis->paKpcrbAddr[idCpu];

            if (pThis->f32Bit)
            {
                /* Read FS base */
                uint32_t GCPtrKpcrBase = 0;

                rc = pVMM->pfnDBGFR3RegCpuQueryU32(pUVM, idCpu, DBGFREG_FS_BASE, &GCPtrKpcrBase);
                if (   RT_SUCCESS(rc)
                    && WINNT32_VALID_ADDRESS(GCPtrKpcrBase))
                {
                    /*
                     * Read the start of the KPCR (@todo Probably move this to a global header)
                     * and verify its content.
                     */
                    struct
                    {
                        uint8_t     abOoi[28]; /* Out of interest */
                        uint32_t    GCPtrSelf;
                        uint32_t    GCPtrCurrentPrcb;
                        uint32_t    u32Irql;
                        uint32_t    u32Iir;
                        uint32_t    u32IirActive;
                        uint32_t    u32Idr;
                        uint32_t    GCPtrKdVersionBlock;
                        uint32_t    GCPtrIdt;
                        uint32_t    GCPtrGdt;
                        uint32_t    GCPtrTss;
                    } Kpcr;

                    LogFlow(("DigWinNt/KPCR[%u]: GS Base %RGv\n", idCpu, GCPtrKpcrBase));
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, pKpcrAddr, GCPtrKpcrBase);

                    rc = pVMM->pfnDBGFR3MemRead(pUVM, idCpu, pKpcrAddr, &Kpcr, sizeof(Kpcr));
                    if (RT_SUCCESS(rc))
                    {
                        uint32_t GCPtrGdt = 0;
                        uint32_t GCPtrIdt = 0;

                        rc = pVMM->pfnDBGFR3RegCpuQueryU32(pUVM, idCpu, DBGFREG_GDTR_BASE, &GCPtrGdt);
                        if (RT_SUCCESS(rc))
                            rc = pVMM->pfnDBGFR3RegCpuQueryU32(pUVM, idCpu, DBGFREG_IDTR_BASE, &GCPtrIdt);
                        if (RT_SUCCESS(rc))
                        {
                            if (   Kpcr.GCPtrGdt == GCPtrGdt
                                && Kpcr.GCPtrIdt == GCPtrIdt
                                && Kpcr.GCPtrSelf == pKpcrAddr->FlatPtr)
                            {
                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, pKpcrbAddr, Kpcr.GCPtrCurrentPrcb);
                                LogRel(("DigWinNt/KPCR[%u]: KPCR=%RGv KPCRB=%RGv\n", idCpu, pKpcrAddr->FlatPtr, pKpcrbAddr->FlatPtr));

                                /*
                                 * Try to extract the NT build number from the KD version block if it exists,
                                 * the shared user data might have set it to 0.
                                 *
                                 * @todo We can use this method to get at the kern base and loaded module list if the other detection
                                 *       method fails (seen with Windows 10 x86).
                                 * @todo On 32bit Windows the debugger data list is also always accessible this way contrary to
                                 *       the amd64 version where it is only available with "/debug on" set.
                                 */
                                if (!pThis->NtBuildNumber)
                                {
                                    NTKDVERSIONBLOCK KdVersBlock;
                                    DBGFADDRESS AddrKdVersBlock;

                                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrKdVersBlock, Kpcr.GCPtrKdVersionBlock);
                                    rc = pVMM->pfnDBGFR3MemRead(pUVM, idCpu, &AddrKdVersBlock, &KdVersBlock, sizeof(KdVersBlock));
                                    if (RT_SUCCESS(rc))
                                        pThis->NtBuildNumber = KdVersBlock.MinorVersion;
                                }
                            }
                            else
                                LogRel(("DigWinNt/KPCR[%u]: KPCR validation error GDT=(%RGv vs %RGv) KPCR=(%RGv vs %RGv)\n", idCpu,
                                        Kpcr.GCPtrGdt, GCPtrGdt, Kpcr.GCPtrSelf, pKpcrAddr->FlatPtr));
                        }
                        else
                            LogRel(("DigWinNt/KPCR[%u]: Getting GDT or IDT base register failed with %Rrc\n", idCpu, rc));
                    }
                }
                else
                    LogRel(("DigWinNt/KPCR[%u]: Getting FS base register failed with %Rrc (%RGv)\n", idCpu, rc, GCPtrKpcrBase));
            }
            else
            {
                /* Read GS base which points to the base of the KPCR for each CPU. */
                RTGCUINTPTR GCPtrTmp = 0;
                rc = pVMM->pfnDBGFR3RegCpuQueryU64(pUVM, idCpu, DBGFREG_GS_BASE, &GCPtrTmp);
                if (   RT_SUCCESS(rc)
                    && !WINNT64_VALID_ADDRESS(GCPtrTmp))
                {
                    /*
                     * Could be a user address when we stopped the VM right in usermode,
                     * read the GS kernel base MSR instead.
                     */
                    rc = pVMM->pfnDBGFR3RegCpuQueryU64(pUVM, idCpu, DBGFREG_MSR_K8_KERNEL_GS_BASE, &GCPtrTmp);
                }

                if (   RT_SUCCESS(rc)
                    && WINNT64_VALID_ADDRESS(GCPtrTmp))
                {
                    LogFlow(("DigWinNt/KPCR[%u]: GS Base %RGv\n", idCpu, GCPtrTmp));
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, pKpcrAddr, GCPtrTmp);

                    rc = pVMM->pfnDBGFR3RegCpuQueryU64(pUVM, idCpu, DBGFREG_GDTR_BASE, &GCPtrTmp);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Read the start of the KPCR (@todo Probably move this to a global header)
                         * and verify its content.
                         */
                        struct
                        {
                            RTGCUINTPTR GCPtrGdt;
                            RTGCUINTPTR GCPtrTss;
                            RTGCUINTPTR GCPtrUserRsp;
                            RTGCUINTPTR GCPtrSelf;
                            RTGCUINTPTR GCPtrCurrentPrcb;
                        } Kpcr;

                        rc = pVMM->pfnDBGFR3MemRead(pUVM, idCpu, pKpcrAddr, &Kpcr, sizeof(Kpcr));
                        if (RT_SUCCESS(rc))
                        {
                            if (   Kpcr.GCPtrGdt == GCPtrTmp
                                && Kpcr.GCPtrSelf == pKpcrAddr->FlatPtr
                                /** @todo && TSS */ )
                            {
                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, pKpcrbAddr, Kpcr.GCPtrCurrentPrcb);
                                LogRel(("DigWinNt/KPCR[%u]: KPCR=%RGv KPCRB=%RGv\n", idCpu, pKpcrAddr->FlatPtr, pKpcrbAddr->FlatPtr));
                            }
                            else
                                LogRel(("DigWinNt/KPCR[%u]: KPCR validation error GDT=(%RGv vs %RGv) KPCR=(%RGv vs %RGv)\n", idCpu,
                                        Kpcr.GCPtrGdt, GCPtrTmp, Kpcr.GCPtrSelf, pKpcrAddr->FlatPtr));
                        }
                        else
                            LogRel(("DigWinNt/KPCR[%u]: Reading KPCR start at %RGv failed with %Rrc\n", idCpu, pKpcrAddr->FlatPtr, rc));
                    }
                    else
                        LogRel(("DigWinNt/KPCR[%u]: Getting GDT base register failed with %Rrc\n", idCpu, rc));
                }
                else
                    LogRel(("DigWinNt/KPCR[%u]: Getting GS base register failed with %Rrc\n", idCpu, rc));
            }
        }

        if (RT_FAILURE(rc))
        {
            LogRel(("DigWinNt/KPCR: Failed to detmine KPCR and KPCRB rc=%Rrc\n", rc));
            RTMemFree(pThis->paKpcrAddr);
            pThis->paKpcrAddr  = NULL;
            pThis->paKpcrbAddr = NULL;
        }
    }
    else
        LogRel(("DigWinNt/KPCR: Failed to allocate %u entries for the KPCR/KPCRB addresses\n", cCpus * 2));
}


/**
 * Checks if the given headers are for the BootMgr image loaded very early
 * during the BIOS boot process.
 */
static bool dbgDiggerWinNtIsBootMgr(PUVM pUVM, PCVMMR3VTABLE pVMM, PCDBGFADDRESS pAddr,
                                    uint8_t const *pbHdrs, size_t cbHdrs, uint32_t *pcbImage)
{
    if (pcbImage)
        *pcbImage = 0;

    /*
     * Check and skip the DOS header.
     */
    AssertReturn(sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS32) < cbHdrs, false);
    IMAGE_DOS_HEADER const * const pMzHdr = (IMAGE_DOS_HEADER const *)pbHdrs;
    if (   pMzHdr->e_magic  != IMAGE_DOS_SIGNATURE
        || pMzHdr->e_lfanew < 0x40
        || pMzHdr->e_lfanew > 0x400)
        return false;

    /*
     * Check the NT headers.
     */
    AssertReturn(pMzHdr->e_lfanew + sizeof(IMAGE_NT_HEADERS32) <= cbHdrs, false);
    IMAGE_NT_HEADERS32 const * const pHdrs = (PCIMAGE_NT_HEADERS32)&pbHdrs[pMzHdr->e_lfanew];
    if (pHdrs->Signature != IMAGE_NT_SIGNATURE)
        return false;
    /* Not so many sections here, typically 3: */
    if (   pHdrs->FileHeader.NumberOfSections <= 1
        || pHdrs->FileHeader.NumberOfSections >= 6)
        return false;
    if (   pHdrs->FileHeader.Machine              != IMAGE_FILE_MACHINE_I386
        || pHdrs->FileHeader.SizeOfOptionalHeader != sizeof(pHdrs->OptionalHeader))
        return false;
    if (pHdrs->OptionalHeader.Magic               != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    if (pHdrs->OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        return false;
    /* Special subsystem with version 1.0: */
    /** @todo which subsystem type does pre win11 use?  (this is win11/amd64)  */
    if (pHdrs->OptionalHeader.Subsystem           != IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
        return false;
    if (   pHdrs->OptionalHeader.MajorSubsystemVersion != 1
        || pHdrs->OptionalHeader.MinorSubsystemVersion != 0)
        return false;
    /* Image OS version and windows version value are all zero: */
    if (   pHdrs->OptionalHeader.MajorOperatingSystemVersion != 0
        || pHdrs->OptionalHeader.MinorOperatingSystemVersion != 0
        || pHdrs->OptionalHeader.Win32VersionValue           != 0)
        return false;
    /* DLL image with 32-bit machine code: */
    if (   (pHdrs->FileHeader.Characteristics & (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL | IMAGE_FILE_32BIT_MACHINE))
        !=                                      (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL | IMAGE_FILE_32BIT_MACHINE))
        return false;
    /* No imports: */
    if (   pHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size != 0
        || pHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size    != 0)
        return false;
    /* Has a bunch of exports ('BootLib.dll'), for win11/amd64 it's 2630 bytes: */
    IMAGE_DATA_DIRECTORY const ExpRvaSize = pHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]; /* cache for later */
    if (ExpRvaSize.Size < 1024)
        return false;
    if ((uint64_t)ExpRvaSize.VirtualAddress + ExpRvaSize.Size > pHdrs->OptionalHeader.SizeOfImage)
        return false;
    /* Check the image size is reasonable (win11/amd64 is 872448): */
    /** @todo adjust for older windows versions...  */
    if (   pHdrs->OptionalHeader.SizeOfImage < _64K
        || pHdrs->OptionalHeader.SizeOfImage > _4M)
        return false;
    /* The image base is 0x00400000 (relocations typically stripped): */
    if (pHdrs->OptionalHeader.ImageBase != UINT32_C(0x00400000))
        return false;

    /*
     * Final check is that export directory name is 'BootLib.dll'.
     */
    static char const       s_szBootLibDll[] = "BootLib.dll";
    DBGFADDRESS             Addr2;
    IMAGE_EXPORT_DIRECTORY  ExpDir;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0,
                                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr2, pAddr->FlatPtr + ExpRvaSize.VirtualAddress),
                                    &ExpDir, sizeof(ExpDir));
    if (RT_FAILURE(rc))
        return false;
    /* There ought to be a few named exports here (win11 has 94): */
    if (ExpDir.NumberOfNames < 48 || ExpDir.NumberOfNames > _4K)
        return false;

    if (   ExpDir.Name < pMzHdr->e_lfanew + pHdrs->OptionalHeader.SizeOfHeaders
        || (uint64_t)ExpDir.Name + sizeof(s_szBootLibDll) > pHdrs->OptionalHeader.SizeOfImage)
        return false;

    char szActualName[sizeof(s_szBootLibDll)];
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr2, pAddr->FlatPtr + ExpDir.Name),
                                &szActualName, sizeof(szActualName));
    if (RT_FAILURE(rc))
        return false;
    if (szActualName[sizeof(s_szBootLibDll) - 1] != '\0')
        return false;
    if (RTStrICmpAscii(szActualName, s_szBootLibDll) != 0)
        return false;

    if (pcbImage)
        *pcbImage = pHdrs->OptionalHeader.SizeOfImage;
    return true;
}


/**
 * Checks if the given headers are for the WinLoad.exe/efi image loaded very
 * early during the BIOS or EFI boot process.
 */
static bool dbgDiggerWinNtIsWinLoad(PUVM pUVM, PCVMMR3VTABLE pVMM, PCDBGFADDRESS pAddr, uint8_t const *pbHdrs, size_t cbHdrs,
                                    bool *pf32Bit, uint32_t *pcbImage, uint32_t *puNtMajorVersion, uint32_t *puNtMinorVersion)
{
    if (pf32Bit)
        *pf32Bit = false;
    if (pcbImage)
        *pcbImage = 0;
    if (puNtMajorVersion)
        *puNtMajorVersion = 0;
    if (puNtMinorVersion)
        *puNtMinorVersion = 0;

    /*
     * Check and skip the DOS header.
     */
    AssertReturn(sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS64) < cbHdrs, false);
    IMAGE_DOS_HEADER const * const pMzHdr = (IMAGE_DOS_HEADER const *)pbHdrs;
    if (   pMzHdr->e_magic  != IMAGE_DOS_SIGNATURE
        || pMzHdr->e_lfanew < 0x40
        || pMzHdr->e_lfanew > 0x400)
        return false;

    /*
     * Check the NT headers.
     *
     * ASSUMES that the 64-bit and 32-bit optional headers match up to
     * SizeOfStackReserve, if you exclude ImageBase and BaseOfData.
     */
    AssertReturn(pMzHdr->e_lfanew + sizeof(IMAGE_NT_HEADERS64) <= cbHdrs, false);
    IMAGE_NT_HEADERS32 const * const pHdrs32 = (PCIMAGE_NT_HEADERS32)&pbHdrs[pMzHdr->e_lfanew];
    IMAGE_NT_HEADERS64 const * const pHdrs64 = (PCIMAGE_NT_HEADERS64)&pbHdrs[pMzHdr->e_lfanew];
    if (pHdrs32->Signature != IMAGE_NT_SIGNATURE)
        return false;
    /* There are a few extra sections here, but not too many: */
    if (   pHdrs32->FileHeader.NumberOfSections <= 4
        || pHdrs32->FileHeader.NumberOfSections >= 15)
        return false;
    bool f32Bit;
    if (pHdrs32->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        f32Bit = true;
    else if (pHdrs32->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
        f32Bit = false;
    else
        return false;
    if (pHdrs32->FileHeader.SizeOfOptionalHeader != (f32Bit ? sizeof(IMAGE_OPTIONAL_HEADER32) : sizeof(IMAGE_OPTIONAL_HEADER64)))
        return false;
    if (pHdrs32->OptionalHeader.Magic            != (f32Bit ? IMAGE_NT_OPTIONAL_HDR32_MAGIC   : IMAGE_NT_OPTIONAL_HDR64_MAGIC))
        return false;
    /* Special subsystem with version 6.0 or later: */
    /** @todo which subsystem type does pre win11 use?  (this is win11/amd64)  */
    if (pHdrs32->OptionalHeader.Subsystem        != IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
        return false;
    if (pHdrs32->OptionalHeader.MajorSubsystemVersion < 6)
        return false;
    /* Image OS version and windows version value are all zero: */
    if (   pHdrs32->OptionalHeader.MajorOperatingSystemVersion != 0
        || pHdrs32->OptionalHeader.MinorOperatingSystemVersion != 0
        || pHdrs32->OptionalHeader.Win32VersionValue           != 0)
        return false;
    /* DLL image: */
    if (   (pHdrs32->FileHeader.Characteristics & (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL))
        !=                                        (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL))
        return false;
    /* Check the image size is reasonable (win11/amd64 is 1769472 for BIOS and ~2MB for EFI): */
    /** @todo adjust for older windows versions...  */
    if (   pHdrs32->OptionalHeader.SizeOfImage < _1M
        || pHdrs32->OptionalHeader.SizeOfImage > _8M)
        return false;

    /* The rest of the fields differs in placement between 32-bit and 64-bit. */

    if (   (f32Bit ? pHdrs32->OptionalHeader.NumberOfRvaAndSizes : pHdrs64->OptionalHeader.NumberOfRvaAndSizes)
        != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        return false;

    /* No imports: */
    IMAGE_DATA_DIRECTORY const * const paDataDirs = f32Bit
                                                  ? pHdrs32->OptionalHeader.DataDirectory : pHdrs64->OptionalHeader.DataDirectory;
    if (   paDataDirs[IMAGE_DIRECTORY_ENTRY_IMPORT].Size != 0
        || paDataDirs[IMAGE_DIRECTORY_ENTRY_IAT].Size    != 0)
        return false;
    /* Has a bunch of exports ('winload.sys'), for win11/amd64 it's 11734 bytes: */
    IMAGE_DATA_DIRECTORY const ExpRvaSize = paDataDirs[IMAGE_DIRECTORY_ENTRY_EXPORT]; /* cache for later */
    if (ExpRvaSize.Size < 1024)
        return false;
    if ((uint64_t)ExpRvaSize.VirtualAddress + ExpRvaSize.Size > pHdrs32->OptionalHeader.SizeOfImage)
        return false;

    /*
     * Final check is that export directory name is 'winload.sys'.
     */
    static char const       s_szWinLoadSys[] = "winload.sys";
    DBGFADDRESS             Addr2;
    IMAGE_EXPORT_DIRECTORY  ExpDir;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0,
                                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr2, pAddr->FlatPtr + ExpRvaSize.VirtualAddress),
                                    &ExpDir, sizeof(ExpDir));
    if (RT_FAILURE(rc))
        return false;
    /* There ought to be a few named exports here (win11 has 373): */
    if (ExpDir.NumberOfNames < 128 || ExpDir.NumberOfNames > _4K)
        return false;

    if (   ExpDir.Name < pMzHdr->e_lfanew + pHdrs32->OptionalHeader.SizeOfHeaders
        || (uint64_t)ExpDir.Name + sizeof(s_szWinLoadSys) > pHdrs32->OptionalHeader.SizeOfImage)
        return false;

    char szActualName[sizeof(s_szWinLoadSys)];
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr2, pAddr->FlatPtr + ExpDir.Name),
                                &szActualName, sizeof(szActualName));
    if (RT_FAILURE(rc))
        return false;
    if (szActualName[sizeof(s_szWinLoadSys) - 1] != '\0')
        return false;
    if (RTStrICmpAscii(szActualName, s_szWinLoadSys) != 0)
        return false;

    if (pf32Bit)
        *pf32Bit  = f32Bit;
    if (pcbImage)
        *pcbImage = pHdrs32->OptionalHeader.SizeOfImage;
    /* Note! We could get more accurate version info from the resource section if we wanted to... */
    if (puNtMajorVersion)
        *puNtMajorVersion = pHdrs32->OptionalHeader.MajorSubsystemVersion;
    if (puNtMinorVersion)
        *puNtMinorVersion = pHdrs32->OptionalHeader.MinorSubsystemVersion;

    return true;
}


/**
 * Process a PE image found in guest memory.
 *
 * @param   pThis           The instance data.
 * @param   pUVM            The user mode VM handle.
 * @param   pVMM            The VMM function table.
 * @param   pszName         The module name.
 * @param   pszFilename     The image filename.
 * @param   pImageAddr      The image address.
 * @param   cbImage         The size of the image.
 * @param   enmArch         Module architecture override.  Default is determined
 *                          by DBGDIGGERWINNT::f32Bit.
 */
static void dbgDiggerWinNtProcessImage(PDBGDIGGERWINNT pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszName,
                                       const char *pszFilename, PCDBGFADDRESS pImageAddr, uint32_t cbImage,
                                       RTLDRARCH enmArch = RTLDRARCH_WHATEVER)
{
    LogFlow(("DigWinNt: %RGp %#x %s\n", pImageAddr->FlatPtr, cbImage, pszName));

    /*
     * Do some basic validation first.
     */
    if (   (cbImage < sizeof(IMAGE_NT_HEADERS64) && !pThis->fNt31)
        || cbImage >= _1M * 256)
    {
        Log(("DigWinNt: %s: Bad image size: %#x\n", pszName, cbImage));
        return;
    }

    /*
     * Use the common in-memory module reader to create a debug module.
     */
    if (enmArch == RTLDRARCH_WHATEVER)
        enmArch = pThis->f32Bit ? RTLDRARCH_X86_32 : RTLDRARCH_AMD64;
    RTERRINFOSTATIC ErrInfo;
    RTDBGMOD        hDbgMod = NIL_RTDBGMOD;
    int rc = pVMM->pfnDBGFR3ModInMem(pUVM, pImageAddr, pThis->fNt31 ? DBGFMODINMEM_F_PE_NT31 : 0, pszName, pszFilename,
                                     enmArch, cbImage, &hDbgMod, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        /*
         * Tag the module.
         */
        rc = RTDbgModSetTag(hDbgMod, DIG_WINNT_MOD_TAG);
        AssertRC(rc);

        /*
         * Link the module.
         */
        RTDBGAS hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
        if (hAs != NIL_RTDBGAS)
            rc = RTDbgAsModuleLink(hAs, hDbgMod, pImageAddr->FlatPtr, RTDBGASLINK_FLAGS_REPLACE /*fFlags*/);
        else
            rc = VERR_INTERNAL_ERROR;
        if (RT_FAILURE(rc))
            LogRel(("DigWinNt: %s: Linking module into address space failed with %Rrc\n", pszName, rc));
        RTDbgModRelease(hDbgMod);
        RTDbgAsRelease(hAs);
    }
    else if (RTErrInfoIsSet(&ErrInfo.Core))
        Log(("DigWinNt: %s: DBGFR3ModInMem failed: %Rrc - %s\n", pszName, rc, ErrInfo.Core.pszMsg));
    else
        Log(("DigWinNt: %s: DBGFR3ModInMem failed: %Rrc\n", pszName, rc));
}


/**
 * Generate a debugger compatible module name from a filename.
 *
 * @returns Pointer to module name (doesn't need to be pszName).
 * @param   pszFilename         The source filename.
 * @param   pszName             Buffer to put the module name in.
 * @param   cbName              Buffer size.
 */
static const char *dbgDiggerWintNtFilenameToModuleName(const char *pszFilename, char *pszName, size_t cbName)
{
    /* Skip to the filename part of the filename. :-) */
    pszFilename = RTPathFilenameEx(pszFilename, RTPATH_STR_F_STYLE_DOS);

    /* We try use 'nt' for the kernel. */
    if (   RTStrICmpAscii(pszFilename, "ntoskrnl.exe") == 0
        || RTStrICmpAscii(pszFilename, "ntkrnlmp.exe") == 0)
        return "nt";


    /* Drop the extension if .dll or .sys. */
    size_t cchFilename = strlen(pszFilename);
    if (   cchFilename > 4
        && pszFilename[cchFilename - 4] == '.')
    {
        if (   RTStrICmpAscii(&pszFilename[cchFilename - 4], ".sys") == 0
            || RTStrICmpAscii(&pszFilename[cchFilename - 4], ".dll") == 0)
            cchFilename -= 4;
    }

    /* Copy and do replacements. */
    if (cchFilename >= cbName)
        cchFilename = cbName - 1;
    size_t off;
    for (off = 0; off < cchFilename; off++)
    {
        char ch = pszFilename[off];
        if (!RT_C_IS_ALNUM(ch))
            ch = '_';
        pszName[off] = ch;
    }
    pszName[off] = '\0';
    return pszName;
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryVersion}
 */
static DECLCALLBACK(int) dbgDiggerWinNtIWinNt_QueryVersion(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                           uint32_t *puVersMajor, uint32_t *puVersMinor,
                                                           uint32_t *puBuildNumber, bool *pf32Bit)
{
    PDBGDIGGERWINNT pData = RT_FROM_MEMBER(pThis, DBGDIGGERWINNT, IWinNt);
    RT_NOREF(pUVM, pVMM);

    if (puVersMajor)
        *puVersMajor = pData->NtMajorVersion;
    if (puVersMinor)
        *puVersMinor = pData->NtMinorVersion;
    if (puBuildNumber)
        *puBuildNumber = pData->NtBuildNumber;
    if (pf32Bit)
        *pf32Bit = pData->f32Bit;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryKernelPtrs}
 */
static DECLCALLBACK(int) dbgDiggerWinNtIWinNt_QueryKernelPtrs(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                              PRTGCUINTPTR pGCPtrKernBase, PRTGCUINTPTR pGCPtrPsLoadedModuleList)
{
    PDBGDIGGERWINNT pData = RT_FROM_MEMBER(pThis, DBGDIGGERWINNT, IWinNt);
    RT_NOREF(pUVM, pVMM);

    *pGCPtrKernBase           = pData->KernelAddr.FlatPtr;
    *pGCPtrPsLoadedModuleList = pData->PsLoadedModuleListAddr.FlatPtr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryKpcrForVCpu}
 */
static DECLCALLBACK(int) dbgDiggerWinNtIWinNt_QueryKpcrForVCpu(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                               VMCPUID idCpu, PRTGCUINTPTR pKpcr, PRTGCUINTPTR pKpcrb)
{
    PDBGDIGGERWINNT pData = RT_FROM_MEMBER(pThis, DBGDIGGERWINNT, IWinNt);

    if (!pData->paKpcrAddr)
        return VERR_NOT_SUPPORTED;

    AssertReturn(idCpu < pVMM->pfnDBGFR3CpuGetCount(pUVM), VERR_INVALID_CPU_ID);

    if (pKpcr)
        *pKpcr = pData->paKpcrAddr[idCpu].FlatPtr;
    if (pKpcrb)
        *pKpcrb = pData->paKpcrbAddr[idCpu].FlatPtr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryCurThrdForVCpu}
 */
static DECLCALLBACK(int) dbgDiggerWinNtIWinNt_QueryCurThrdForVCpu(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                                  VMCPUID idCpu, PRTGCUINTPTR pCurThrd)
{
    PDBGDIGGERWINNT pData = RT_FROM_MEMBER(pThis, DBGDIGGERWINNT, IWinNt);

    if (!pData->paKpcrAddr)
        return VERR_NOT_SUPPORTED;

    AssertReturn(idCpu < pVMM->pfnDBGFR3CpuGetCount(pUVM), VERR_INVALID_CPU_ID);

    DBGFADDRESS AddrCurThrdPtr = pData->paKpcrbAddr[idCpu];
    pVMM->pfnDBGFR3AddrAdd(&AddrCurThrdPtr, 0x08); /** @todo Make this prettier. */
    return pVMM->pfnDBGFR3MemRead(pUVM, idCpu, &AddrCurThrdPtr, pCurThrd, sizeof(*pCurThrd));
}


/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerWinNtStackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                         PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState, PCCPUMCTX pInitialCtx,
                                                         RTDBGAS hAs, uint64_t *puScratch)
{
    Assert(pInitialCtx);

    /*
     * We want to locate trap frames here.  The trap frame structure contains
     * the 64-bit IRET frame, so given unwind information it's easy to identify
     * using the return type and frame address.
     */
    if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_64BIT)
    {
        /*
         * Is this a trap frame?  If so, try read the trap frame.
         */
        if (   pFrame->enmReturnType == RTDBGRETURNTYPE_IRET64
            && !(pFrame->AddrFrame.FlatPtr & 0x7)
            && WINNT64_VALID_ADDRESS(pFrame->AddrFrame.FlatPtr) )
        {
            KTRAP_FRAME_AMD64 TrapFrame;
            RT_ZERO(TrapFrame);
            uint64_t const uTrapFrameAddr = pFrame->AddrFrame.FlatPtr
                                          - RT_UOFFSETOF(KTRAP_FRAME_AMD64, ErrCdOrXcptFrameOrS);
            int rc = pState->pfnReadStack(pState, uTrapFrameAddr, sizeof(TrapFrame), &TrapFrame);
            if (RT_SUCCESS(rc))
            {
                /* Valid?  Not too much else we can check here (EFlags isn't
                   reliable in manually construct frames). */
                if (TrapFrame.ExceptionActive <= 2)
                {
                    pFrame->fFlags |= DBGFSTACKFRAME_FLAGS_TRAP_FRAME;

                    /*
                     * Add sure 'register' information from the frame to the frame.
                     *
                     * To avoid code duplication, we do this in two steps in a loop.
                     * The first iteration only figures out how many registers we're
                     * going to save and allocates room for them.  The second iteration
                     * does the actual adding.
                     */
                    uint32_t      cRegs      = pFrame->cSureRegs;
                    PDBGFREGVALEX paSureRegs = NULL;
#define ADD_REG_NAMED(a_Type, a_ValMemb, a_Value, a_pszName) do { \
                            if (paSureRegs) \
                            { \
                                paSureRegs[iReg].pszName         = a_pszName;\
                                paSureRegs[iReg].enmReg          = DBGFREG_END; \
                                paSureRegs[iReg].enmType         = a_Type; \
                                paSureRegs[iReg].Value.a_ValMemb = (a_Value); \
                            } \
                            iReg++; \
                        } while (0)
#define MAYBE_ADD_GREG(a_Value, a_enmReg, a_idxReg) do { \
                            if (!(pState->u.x86.Loaded.s.fRegs & RT_BIT(a_idxReg))) \
                            { \
                                if (paSureRegs) \
                                { \
                                    pState->u.x86.Loaded.s.fRegs    |= RT_BIT(a_idxReg); \
                                    pState->u.x86.auRegs[a_idxReg]   = (a_Value); \
                                    paSureRegs[iReg].Value.u64       = (a_Value); \
                                    paSureRegs[iReg].enmReg          = a_enmReg; \
                                    paSureRegs[iReg].enmType         = DBGFREGVALTYPE_U64; \
                                    paSureRegs[iReg].pszName         = NULL; \
                                } \
                                iReg++; \
                            } \
                        } while (0)
                    for (unsigned iLoop = 0; iLoop < 2; iLoop++)
                    {
                        uint32_t iReg = pFrame->cSureRegs;
                        ADD_REG_NAMED(DBGFREGVALTYPE_U64, u64, uTrapFrameAddr, "TrapFrame");
                        ADD_REG_NAMED(DBGFREGVALTYPE_U8,   u8, TrapFrame.ExceptionActive, "ExceptionActive");
                        if (TrapFrame.ExceptionActive == 0)
                        {
                            ADD_REG_NAMED(DBGFREGVALTYPE_U8,   u8, TrapFrame.PreviousIrql, "PrevIrql");
                            ADD_REG_NAMED(DBGFREGVALTYPE_U8,   u8, (uint8_t)TrapFrame.ErrCdOrXcptFrameOrS, "IntNo");
                        }
                        else if (   TrapFrame.ExceptionActive == 1
                                 && TrapFrame.FaultIndicator == ((TrapFrame.ErrCdOrXcptFrameOrS >> 1) & 0x9))
                            ADD_REG_NAMED(DBGFREGVALTYPE_U64, u64, TrapFrame.FaultAddrOrCtxRecOrTS, "cr2-probably");
                        if (TrapFrame.SegCs & X86_SEL_RPL)
                            ADD_REG_NAMED(DBGFREGVALTYPE_U8,   u8, 1, "UserMode");
                        else
                            ADD_REG_NAMED(DBGFREGVALTYPE_U8,   u8, 1, "KernelMode");
                        if (TrapFrame.ExceptionActive <= 1)
                        {
                            MAYBE_ADD_GREG(TrapFrame.Rax, DBGFREG_RAX, X86_GREG_xAX);
                            MAYBE_ADD_GREG(TrapFrame.Rcx, DBGFREG_RCX, X86_GREG_xCX);
                            MAYBE_ADD_GREG(TrapFrame.Rdx, DBGFREG_RDX, X86_GREG_xDX);
                            MAYBE_ADD_GREG(TrapFrame.R8,  DBGFREG_R8,  X86_GREG_x8);
                            MAYBE_ADD_GREG(TrapFrame.R9,  DBGFREG_R9,  X86_GREG_x9);
                            MAYBE_ADD_GREG(TrapFrame.R10, DBGFREG_R10, X86_GREG_x10);
                            MAYBE_ADD_GREG(TrapFrame.R11, DBGFREG_R11, X86_GREG_x11);
                        }
                        else if (TrapFrame.ExceptionActive == 2)
                        {
                            MAYBE_ADD_GREG(TrapFrame.Rbx, DBGFREG_RBX, X86_GREG_xBX);
                            MAYBE_ADD_GREG(TrapFrame.Rsi, DBGFREG_RSI, X86_GREG_xSI);
                            MAYBE_ADD_GREG(TrapFrame.Rdi, DBGFREG_RDI, X86_GREG_xDI);
                        }
                        // MAYBE_ADD_GREG(TrapFrame.Rbp, DBGFREG_RBP, X86_GREG_xBP); - KiInterrupt[Sub]Dispatch* may leave this invalid.

                        /* Done? */
                        if (iLoop > 0)
                        {
                            Assert(cRegs == iReg);
                            break;
                        }

                        /* Resize the array, zeroing the extension. */
                        if (pFrame->cSureRegs)
                            paSureRegs = (PDBGFREGVALEX)pVMM->pfnMMR3HeapRealloc(pFrame->paSureRegs, iReg * sizeof(paSureRegs[0]));
                        else
                            paSureRegs = (PDBGFREGVALEX)pVMM->pfnMMR3HeapAllocU(pUVM, MM_TAG_DBGF_STACK, iReg * sizeof(paSureRegs[0]));
                        AssertReturn(paSureRegs, VERR_NO_MEMORY);

                        pFrame->paSureRegs = paSureRegs;
                        RT_BZERO(&paSureRegs[pFrame->cSureRegs], (iReg - pFrame->cSureRegs) * sizeof(paSureRegs[0]));
                        cRegs = iReg;
                    }
#undef ADD_REG_NAMED
#undef MAYBE_ADD_GREG

                    /* Commit the register update. */
                    pFrame->cSureRegs = cRegs;
                }
            }
        }
    }

    RT_NOREF(pUVM, pVMM, pvData, idCpu, hAs, pInitialCtx, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerWinNtQueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;

    switch (enmIf)
    {
        case DBGFOSINTERFACE_WINNT:
            return &pThis->IWinNt;
        default:
            return NULL;
    }
}


/**
 * @copydoc DBGFOSREG::pfnQueryVersion
 */
static DECLCALLBACK(int)  dbgDiggerWinNtQueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                     char *pszVersion, size_t cchVersion)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    Assert(pThis->fValid);

    const char *pszNtProductType;
    switch (pThis->NtProductType)
    {
        case kNtProductType_WinNt:      pszNtProductType = "-WinNT";        break;
        case kNtProductType_LanManNt:   pszNtProductType = "-LanManNT";     break;
        case kNtProductType_Server:     pszNtProductType = "-Server";       break;
        default:                        pszNtProductType = "";              break;
    }

    RTStrPrintf(pszVersion, cchVersion, "%u.%u-%s%s (BuildNumber %u)", pThis->NtMajorVersion, pThis->NtMinorVersion,
                pThis->f32Bit ? "x86" : "AMD64", pszNtProductType, pThis->NtBuildNumber);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnTerm
 */
static DECLCALLBACK(void)  dbgDiggerWinNtTerm(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF1(pUVM);
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    Assert(pThis->fValid);

#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING
    if (pThis->hBpDbgPrint != NIL_DBGFBP)
    {
        int rc = DBGFR3BpClear(pUVM, pThis->hBpDbgPrint);
        AssertRC(rc);
        pThis->hBpDbgPrint = NIL_DBGFBP;
    }

    if (pThis->hBpOwnerDbgPrint != NIL_DBGFBPOWNER)
    {
        int rc = DBGFR3BpOwnerDestroy(pUVM, pThis->hBpOwnerDbgPrint);
        AssertRC(rc);
        pThis->hBpOwnerDbgPrint = NIL_DBGFBPOWNER;
    }
#endif

    /*
     * As long as we're using our private LDR reader implementation,
     * we must unlink and ditch the modules we created.
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
                if (RTDbgModGetTag(hMod) == DIG_WINNT_MOD_TAG)
                {
                    int rc = RTDbgAsModuleUnlink(hDbgAs, hMod);
                    AssertRC(rc);
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hDbgAs);
    }

    if (pThis->paKpcrAddr)
        RTMemFree(pThis->paKpcrAddr);
    /* pThis->paKpcrbAddr comes from the same allocation as pThis->paKpcrAddr. */

    pThis->paKpcrAddr  = NULL;
    pThis->paKpcrbAddr = NULL;

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerWinNtRefresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    NOREF(pThis);
    Assert(pThis->fValid);

    /*
     * For now we'll flush and reload everything.
     */
    dbgDiggerWinNtTerm(pUVM, pVMM, pvData);

    return dbgDiggerWinNtInit(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerWinNtInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    Assert(!pThis->fValid);

    union
    {
        uint8_t             au8[GUEST_PAGE_SIZE * 2];
        RTUTF16             wsz[GUEST_PAGE_SIZE];
        NTKUSERSHAREDDATA   UserSharedData;
    }               u;
    DBGFADDRESS     Addr;
    int             rc;

    /*
     * Load the bootmgr module if it was detected and is still present.
     * The boot manager is always 32-bit on x86.
     */
    if (DBGFADDRESS_IS_VALID(&pThis->BootMgrAddr))
    {
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->BootMgrAddr, &u, sizeof(u));
        if (RT_SUCCESS(rc))
        {
            uint32_t cbImage = 0;
            if (dbgDiggerWinNtIsBootMgr(pUVM, pVMM, &pThis->BootMgrAddr, &u.au8[0], sizeof(u), &cbImage))
                dbgDiggerWinNtProcessImage(pThis, pUVM, pVMM, "BootMgr", "BootMgr.exe",
                                           &pThis->BootMgrAddr, cbImage, RTLDRARCH_X86_32);
        }
    }

    /*
     * Load the winload module if it was detected and still present.
     */
    if (DBGFADDRESS_IS_VALID(&pThis->WinLoadAddr))
    {
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &pThis->WinLoadAddr, &u, sizeof(u));
        if (RT_SUCCESS(rc))
        {
            uint32_t cbImage         = 0;
            uint32_t uNtMajorVersion = 0;
            uint32_t uNtMinorVersion = 0;
            if (dbgDiggerWinNtIsWinLoad(pUVM, pVMM, &pThis->WinLoadAddr, &u.au8[0], sizeof(u),
                                        NULL /*pf32Bit*/, &cbImage, &uNtMajorVersion, &uNtMinorVersion))
            {
                dbgDiggerWinNtProcessImage(pThis, pUVM, pVMM, "WinLoad", "WinLoad.exe", &pThis->WinLoadAddr, cbImage);
                pThis->NtMajorVersion = uNtMajorVersion;
                pThis->NtMinorVersion = uNtMinorVersion;
            }
        }
    }

    /*
     * Try figure the NT version.
     */
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pThis->f32Bit ? NTKUSERSHAREDDATA_WINNT32 : NTKUSERSHAREDDATA_WINNT64);
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &u, GUEST_PAGE_SIZE);
    if (RT_SUCCESS(rc))
    {
        pThis->NtProductType  = u.UserSharedData.ProductTypeIsValid && u.UserSharedData.NtProductType <= kNtProductType_Server
                              ? (NTPRODUCTTYPE)u.UserSharedData.NtProductType
                              : kNtProductType_Invalid;
        pThis->NtMajorVersion = u.UserSharedData.NtMajorVersion;
        pThis->NtMinorVersion = u.UserSharedData.NtMinorVersion;
        pThis->NtBuildNumber  = u.UserSharedData.NtBuildNumber;
    }
    else if (pThis->fNt31)
    {
        pThis->NtProductType  = kNtProductType_WinNt;
        pThis->NtMajorVersion = 3;
        pThis->NtMinorVersion = 1;
        pThis->NtBuildNumber  = 0;
    }
    else
    {
        Log(("DigWinNt: Error reading KUSER_SHARED_DATA: %Rrc\n", rc));
        if (   !DBGFADDRESS_IS_VALID(&pThis->KernelAddr)
            || !DBGFADDRESS_IS_VALID(&pThis->KernelMteAddr)
            || !DBGFADDRESS_IS_VALID(&pThis->PsLoadedModuleListAddr))
            return DBGFADDRESS_IS_VALID(&pThis->BootMgrAddr) || DBGFADDRESS_IS_VALID(&pThis->WinLoadAddr) ? VINF_SUCCESS : rc;
    }

    /*
     * Dig out the module chain.
     */
    DBGFADDRESS AddrPrev = pThis->PsLoadedModuleListAddr;
    Addr                 = pThis->KernelMteAddr;
    do
    {
        /* Read the validate the MTE. */
        NTMTE Mte;
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &Mte, pThis->f32Bit ? sizeof(Mte.vX_32) : sizeof(Mte.vX_64));
        if (RT_FAILURE(rc))
            break;
        if (WINNT_UNION(pThis, &Mte, InLoadOrderLinks.Blink) != AddrPrev.FlatPtr)
        {
            Log(("DigWinNt: Bad Mte At %RGv - backpointer\n", Addr.FlatPtr));
            break;
        }
        if (!WINNT_VALID_ADDRESS(pThis, WINNT_UNION(pThis, &Mte, InLoadOrderLinks.Flink)) )
        {
            Log(("DigWinNt: Bad Mte at %RGv - forward pointer\n", Addr.FlatPtr));
            break;
        }
        if (!WINNT_VALID_ADDRESS(pThis, WINNT_UNION(pThis, &Mte, BaseDllName.Buffer)))
        {
            Log(("DigWinNt: Bad Mte at %RGv - BaseDllName=%llx\n", Addr.FlatPtr, WINNT_UNION(pThis, &Mte, BaseDllName.Buffer)));
            break;
        }
        if (!WINNT_VALID_ADDRESS(pThis, WINNT_UNION(pThis, &Mte, FullDllName.Buffer)))
        {
            Log(("DigWinNt: Bad Mte at %RGv - FullDllName=%llx\n", Addr.FlatPtr, WINNT_UNION(pThis, &Mte, FullDllName.Buffer)));
            break;
        }
        if (!WINNT_VALID_ADDRESS(pThis, WINNT_UNION(pThis, &Mte, DllBase)))
        {
            Log(("DigWinNt: Bad Mte at %RGv - DllBase=%llx\n", Addr.FlatPtr, WINNT_UNION(pThis, &Mte, DllBase) ));
            break;
        }

        uint32_t const cbImageMte = !pThis->fNt31 ? WINNT_UNION(pThis, &Mte, SizeOfImage) : 0;
        if (   !pThis->fNt31
            && (   cbImageMte > _256M
                || WINNT_UNION(pThis, &Mte, EntryPoint) - WINNT_UNION(pThis, &Mte, DllBase) > cbImageMte) )
        {
            Log(("DigWinNt: Bad Mte at %RGv - EntryPoint=%llx SizeOfImage=%x DllBase=%llx\n",
                 Addr.FlatPtr, WINNT_UNION(pThis, &Mte, EntryPoint), cbImageMte, WINNT_UNION(pThis, &Mte, DllBase)));
            break;
        }

        /* Read the full name. */
        DBGFADDRESS AddrName;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrName, WINNT_UNION(pThis, &Mte, FullDllName.Buffer));
        uint16_t    cbName = WINNT_UNION(pThis, &Mte, FullDllName.Length);
        if (cbName < sizeof(u))
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrName, &u, cbName);
        else
            rc = VERR_OUT_OF_RANGE;
        if (RT_FAILURE(rc))
        {
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrName, WINNT_UNION(pThis, &Mte, BaseDllName.Buffer));
            cbName = WINNT_UNION(pThis, &Mte, BaseDllName.Length);
            if (cbName < sizeof(u))
                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrName, &u, cbName);
            else
                rc = VERR_OUT_OF_RANGE;
        }
        if (RT_SUCCESS(rc))
        {
            u.wsz[cbName / 2] = '\0';

            char *pszFilename;
            rc = RTUtf16ToUtf8(u.wsz, &pszFilename);
            if (RT_SUCCESS(rc))
            {
                char        szModName[128];
                const char *pszModName = dbgDiggerWintNtFilenameToModuleName(pszFilename, szModName, sizeof(szModName));

                /* Read the start of the PE image and pass it along to a worker. */
                DBGFADDRESS ImageAddr;
                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ImageAddr, WINNT_UNION(pThis, &Mte, DllBase));
                dbgDiggerWinNtProcessImage(pThis, pUVM, pVMM, pszModName, pszFilename, &ImageAddr, cbImageMte);
                RTStrFree(pszFilename);
            }
        }

        /* next */
        AddrPrev = Addr;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, WINNT_UNION(pThis, &Mte, InLoadOrderLinks.Flink));
    } while (   Addr.FlatPtr != pThis->KernelMteAddr.FlatPtr
             && Addr.FlatPtr != pThis->PsLoadedModuleListAddr.FlatPtr);

    /* Try resolving the KPCR and KPCRB addresses for each vCPU. */
    dbgDiggerWinNtResolveKpcr(pThis, pUVM, pVMM);

#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING
    /* Try to hook into the DbgPrint/vDbgPrint... code so we can gather information from the drivers. */
    dbgDiggerWinNtDbgPrintHook(pThis, pUVM);
#endif

    pThis->fValid = true;
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerWinNtProbe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    bool            fRet  = false;
    DBGFADDRESS     Addr;
    union
    {
        uint8_t             au8[8192];
        uint16_t            au16[8192/2];
        uint32_t            au32[8192/4];
        IMAGE_DOS_HEADER    MzHdr;
        RTUTF16             wsz[8192/2];
        X86DESCGATE         a32Gates[X86_XCPT_PF + 1];
        X86DESC64GATE       a64Gates[X86_XCPT_PF + 1];
    } u;

    union
    {
        NTMTE32 v32;
        NTMTE64 v64;
    } uMte, uMte2, uMte3;

    /*
     * Reset the state (paranoia)
     */
    RT_ZERO(pThis->BootMgrAddr);
    RT_ZERO(pThis->WinLoadAddr);
    RT_ZERO(pThis->KernelAddr);
    RT_ZERO(pThis->KernelMteAddr);
    RT_ZERO(pThis->PsLoadedModuleListAddr);
    pThis->f32Bit = false;
    pThis->fNt31  = false;

    /*
     * Look for the BootMgr/BootLib.dll module at 0x400000.
     * This is only relevant when booting via BIOS.
     */
    if (1/** @todo BIOS + x86/amd64 only */)
    {
        int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, UINT32_C(0x400000)),
                                        &u.au8[0], sizeof(u));
        if (RT_SUCCESS(rc) && dbgDiggerWinNtIsBootMgr(pUVM, pVMM, &Addr, &u.au8[0], sizeof(u), NULL))
        {
            pThis->BootMgrAddr = Addr;
            fRet = true;
        }
    }

    /*
     * Look for the winload.exe/winload.sys module that's, from the looks of it,
     * responsible for loading the kernel and boot drivers.
     */
    if (DBGFADDRESS_IS_VALID(&pThis->BootMgrAddr))
    {
        static const char s_szNeedle[] = "PAGER32C"; /* Section name. No terminator. */
        uint32_t const    uEndAddr     = _16M + _1M;
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pThis->BootMgrAddr.FlatPtr + _64K);
        do
        {
            int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &Addr, uEndAddr - Addr.FlatPtr,
                                            8 /*uAlign*/, s_szNeedle, sizeof(s_szNeedle) - 1, &Addr);
            if (RT_FAILURE(rc))
                break;

            DBGFADDRESS AddrMz;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrMz,
                                                                    Addr.FlatPtr & ~(RTGCUINTPTR)GUEST_PAGE_OFFSET_MASK),
                                        &u.au8[0], sizeof(u));
            bool f32Bit = false;
            if (RT_SUCCESS(rc) && dbgDiggerWinNtIsWinLoad(pUVM, pVMM, &AddrMz, &u.au8[0], sizeof(u), &f32Bit, NULL, NULL, NULL))
            {
                pThis->WinLoadAddr = AddrMz;
                pThis->f32Bit      = f32Bit;
                fRet = true;
                break;
            }
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, AddrMz.FlatPtr + GUEST_PAGE_SIZE);
        } while (Addr.FlatPtr + _64K < uEndAddr);
    }

    /*
     * NT only runs in protected or long mode.
     */
    CPUMMODE const enmMode = pVMM->pfnDBGFR3CpuGetMode(pUVM, 0 /*idCpu*/);
    if (enmMode != CPUMMODE_PROTECTED && enmMode != CPUMMODE_LONG)
        return fRet;
    bool const      f64Bit = enmMode == CPUMMODE_LONG
                          || (DBGFADDRESS_IS_VALID(&pThis->WinLoadAddr) && !pThis->f32Bit);
    uint64_t const  uStart = f64Bit ? UINT64_C(0xffff800000000000) : UINT32_C(0x80001000);
    uint64_t const  uEnd   = f64Bit ? UINT64_C(0xffffffffffff0000) : UINT32_C(0xffff0000);

    /*
     * To approximately locate the kernel we examine the IDTR handlers.
     *
     * The exception/trap/fault handlers are all in NT kernel image, we pick
     * KiPageFault here.
     */
    uint64_t uIdtrBase = 0;
    uint16_t uIdtrLimit = 0;
    int rc = pVMM->pfnDBGFR3RegCpuQueryXdtr(pUVM, 0, DBGFREG_IDTR, &uIdtrBase, &uIdtrLimit);
    AssertRCReturn(rc, fRet);

    const uint16_t cbMinIdtr = (X86_XCPT_PF + 1) * (f64Bit ? sizeof(X86DESC64GATE) : sizeof(X86DESCGATE));
    if (uIdtrLimit < cbMinIdtr)
        return fRet;

    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uIdtrBase), &u, cbMinIdtr);
    if (RT_FAILURE(rc))
        return fRet;

    uint64_t uKrnlStart = uStart;
    uint64_t uKrnlEnd   = uEnd;
    if (f64Bit)
    {
        uint64_t uHandler = u.a64Gates[X86_XCPT_PF].u16OffsetLow
                          | ((uint32_t)u.a64Gates[X86_XCPT_PF].u16OffsetHigh << 16)
                          | ((uint64_t)u.a64Gates[X86_XCPT_PF].u32OffsetTop  << 32);
        if (uHandler >= uStart && uHandler <= uEnd)
        {
            uKrnlStart = (uHandler & ~(uint64_t)_4M) - _512M;
            uKrnlEnd   = (uHandler + (uint64_t)_4M) & ~(uint64_t)_4M;
        }
        else if (!DBGFADDRESS_IS_VALID(&pThis->WinLoadAddr))
            return fRet;
    }
    else
    {
        uint32_t uHandler = RT_MAKE_U32(u.a32Gates[X86_XCPT_PF].u16OffsetLow, u.a32Gates[X86_XCPT_PF].u16OffsetHigh);
        if (uHandler >= uStart && uHandler <= uEnd)
        {
            uKrnlStart = (uHandler & ~(uint64_t)_4M) - _64M;
            uKrnlEnd   = (uHandler + (uint64_t)_4M) & ~(uint64_t)_4M;
        }
        else if (!DBGFADDRESS_IS_VALID(&pThis->WinLoadAddr))
            return fRet;
    }

    /*
     * Look for the PAGELK section name that seems to be a part of all kernels.
     * Then try find the module table entry for it.  Since it's the first entry
     * in the PsLoadedModuleList we can easily validate the list head and report
     * success.
     *
     * Note! We ASSUME the section name is 8 byte aligned.
     */
    DBGFADDRESS KernelAddr;
    for (pVMM->pfnDBGFR3AddrFromFlat(pUVM, &KernelAddr, uKrnlStart);
         KernelAddr.FlatPtr < uKrnlEnd;
         KernelAddr.FlatPtr += GUEST_PAGE_SIZE)
    {
        bool fNt31 = false;
        DBGFADDRESS const RetryAddress = KernelAddr;
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &KernelAddr, uEnd - KernelAddr.FlatPtr,
                                    8, "PAGELK\0", sizeof("PAGELK\0"), &KernelAddr);
        if (   rc == VERR_DBGF_MEM_NOT_FOUND
            && !f64Bit)
        {
            /* NT3.1 didn't have a PAGELK section, so look for _TEXT instead.  The
               following VirtualSize is zero, so check for that too. */
            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &RetryAddress, uEnd - RetryAddress.FlatPtr,
                                        8, "_TEXT\0\0\0\0\0\0", sizeof("_TEXT\0\0\0\0\0\0"), &KernelAddr);
            fNt31 = true;
        }
        if (RT_FAILURE(rc))
            break;
        pVMM->pfnDBGFR3AddrSub(&KernelAddr, KernelAddr.FlatPtr & GUEST_PAGE_OFFSET_MASK);

        /* MZ + PE header. */
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &KernelAddr, &u, sizeof(u));
        if (    RT_SUCCESS(rc)
            &&  u.MzHdr.e_magic == IMAGE_DOS_SIGNATURE
            &&  !(u.MzHdr.e_lfanew & 0x7)
            &&  u.MzHdr.e_lfanew >= 0x080
            &&  u.MzHdr.e_lfanew <= 0x400) /* W8 is at 0x288*/
        {
            if (!f64Bit)
            {
                IMAGE_NT_HEADERS32 const *pHdrs = (IMAGE_NT_HEADERS32 const *)&u.au8[u.MzHdr.e_lfanew];
                if (    pHdrs->Signature                            == IMAGE_NT_SIGNATURE
                    &&  pHdrs->FileHeader.Machine                   == IMAGE_FILE_MACHINE_I386
                    &&  pHdrs->FileHeader.SizeOfOptionalHeader      == sizeof(pHdrs->OptionalHeader)
                    &&  pHdrs->FileHeader.NumberOfSections          >= 10 /* the kernel has lots */
                    &&  (pHdrs->FileHeader.Characteristics & (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL)) == IMAGE_FILE_EXECUTABLE_IMAGE
                    &&  pHdrs->OptionalHeader.Magic                 == IMAGE_NT_OPTIONAL_HDR32_MAGIC
                    &&  pHdrs->OptionalHeader.NumberOfRvaAndSizes   == IMAGE_NUMBEROF_DIRECTORY_ENTRIES
                    )
                {
                    /* Find the MTE. */
                    RT_ZERO(uMte);
                    uMte.v32.DllBase     = KernelAddr.FlatPtr;
                    uMte.v32.EntryPoint  = KernelAddr.FlatPtr + pHdrs->OptionalHeader.AddressOfEntryPoint;
                    uMte.v32.SizeOfImage = !fNt31 ? pHdrs->OptionalHeader.SizeOfImage : 0; /* NT 3.1 didn't set the size. */
                    DBGFADDRESS HitAddr;
                    rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &KernelAddr, uEnd - KernelAddr.FlatPtr,
                                                4 /*align*/, &uMte.v32.DllBase, 3 * sizeof(uint32_t), &HitAddr);
                    while (RT_SUCCESS(rc))
                    {
                        /* check the name. */
                        DBGFADDRESS MteAddr = HitAddr;
                        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                    pVMM->pfnDBGFR3AddrSub(&MteAddr, RT_OFFSETOF(NTMTE32, DllBase)),
                                                    &uMte2.v32, sizeof(uMte2.v32));
                        if (    RT_SUCCESS(rc)
                            &&  uMte2.v32.DllBase     == uMte.v32.DllBase
                            &&  uMte2.v32.EntryPoint  == uMte.v32.EntryPoint
                            &&  uMte2.v32.SizeOfImage == uMte.v32.SizeOfImage
                            &&  WINNT32_VALID_ADDRESS(uMte2.v32.InLoadOrderLinks.Flink)
                            &&  WINNT32_VALID_ADDRESS(uMte2.v32.BaseDllName.Buffer)
                            &&  WINNT32_VALID_ADDRESS(uMte2.v32.FullDllName.Buffer)
                            &&  uMte2.v32.BaseDllName.Length <= 128
                            &&  uMte2.v32.FullDllName.Length <= 260
                           )
                        {
                            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uMte2.v32.BaseDllName.Buffer),
                                                        u.wsz, uMte2.v32.BaseDllName.Length);
                            u.wsz[uMte2.v32.BaseDllName.Length / 2] = '\0';
                            if (    RT_SUCCESS(rc)
                                &&  (   !RTUtf16ICmp(u.wsz, g_wszKernelNames[0])
                                  /* || !RTUtf16ICmp(u.wsz, g_wszKernelNames[1]) */
                                    )
                               )
                            {
                                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr,
                                                                                        uMte2.v32.InLoadOrderLinks.Blink),
                                                            &uMte3.v32, RT_SIZEOFMEMB(NTMTE32, InLoadOrderLinks));
                                if (   RT_SUCCESS(rc)
                                    && uMte3.v32.InLoadOrderLinks.Flink == MteAddr.FlatPtr
                                    && WINNT32_VALID_ADDRESS(uMte3.v32.InLoadOrderLinks.Blink) )
                                {
                                    Log(("DigWinNt: MteAddr=%RGv KernelAddr=%RGv SizeOfImage=%x &PsLoadedModuleList=%RGv (32-bit)\n",
                                         MteAddr.FlatPtr, KernelAddr.FlatPtr, uMte2.v32.SizeOfImage, Addr.FlatPtr));
                                    pThis->KernelAddr               = KernelAddr;
                                    pThis->KernelMteAddr            = MteAddr;
                                    pThis->PsLoadedModuleListAddr   = Addr;
                                    pThis->f32Bit                   = true;
                                    pThis->fNt31                    = fNt31;
                                    return true;
                                }
                            }
                            else if (RT_SUCCESS(rc))
                            {
                                Log2(("DigWinNt: Wrong module: MteAddr=%RGv ImageAddr=%RGv SizeOfImage=%#x '%ls'\n",
                                      MteAddr.FlatPtr, KernelAddr.FlatPtr, uMte2.v32.SizeOfImage, u.wsz));
                                break; /* Not NT kernel */
                            }
                        }

                        /* next */
                        pVMM->pfnDBGFR3AddrAdd(&HitAddr, 4);
                        if (HitAddr.FlatPtr < uEnd)
                            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &HitAddr, uEnd - HitAddr.FlatPtr,
                                                        4 /*align*/, &uMte.v32.DllBase, 3 * sizeof(uint32_t), &HitAddr);
                        else
                            rc = VERR_DBGF_MEM_NOT_FOUND;
                    }
                }
            }
            else
            {
                IMAGE_NT_HEADERS64 const *pHdrs = (IMAGE_NT_HEADERS64 const *)&u.au8[u.MzHdr.e_lfanew];
                if (    pHdrs->Signature                            == IMAGE_NT_SIGNATURE
                    &&  pHdrs->FileHeader.Machine                   == IMAGE_FILE_MACHINE_AMD64
                    &&  pHdrs->FileHeader.SizeOfOptionalHeader      == sizeof(pHdrs->OptionalHeader)
                    &&  pHdrs->FileHeader.NumberOfSections          >= 10 /* the kernel has lots */
                    &&      (pHdrs->FileHeader.Characteristics & (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL))
                         == IMAGE_FILE_EXECUTABLE_IMAGE
                    &&  pHdrs->OptionalHeader.Magic                 == IMAGE_NT_OPTIONAL_HDR64_MAGIC
                    &&  pHdrs->OptionalHeader.NumberOfRvaAndSizes   == IMAGE_NUMBEROF_DIRECTORY_ENTRIES
                    )
                {
                    /* Find the MTE. */
                    RT_ZERO(uMte.v64);
                    uMte.v64.DllBase     = KernelAddr.FlatPtr;
                    uMte.v64.EntryPoint  = KernelAddr.FlatPtr + pHdrs->OptionalHeader.AddressOfEntryPoint;
                    uMte.v64.SizeOfImage = pHdrs->OptionalHeader.SizeOfImage;
                    DBGFADDRESS ScanAddr;
                    DBGFADDRESS HitAddr;
                    rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ScanAddr, uStart),
                                                uEnd - uStart, 8 /*align*/, &uMte.v64.DllBase, 5 * sizeof(uint32_t), &HitAddr);
                    while (RT_SUCCESS(rc))
                    {
                        /* Read the start of the MTE and check some basic members. */
                        DBGFADDRESS MteAddr = HitAddr;
                        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                    pVMM->pfnDBGFR3AddrSub(&MteAddr, RT_OFFSETOF(NTMTE64, DllBase)),
                                                    &uMte2.v64, sizeof(uMte2.v64));
                        if (    RT_SUCCESS(rc)
                            &&  uMte2.v64.DllBase     == uMte.v64.DllBase
                            &&  uMte2.v64.EntryPoint  == uMte.v64.EntryPoint
                            &&  uMte2.v64.SizeOfImage == uMte.v64.SizeOfImage
                            &&  WINNT64_VALID_ADDRESS(uMte2.v64.InLoadOrderLinks.Flink)
                            &&  WINNT64_VALID_ADDRESS(uMte2.v64.BaseDllName.Buffer)
                            &&  WINNT64_VALID_ADDRESS(uMte2.v64.FullDllName.Buffer)
                            &&  uMte2.v64.BaseDllName.Length <= 128
                            &&  uMte2.v64.FullDllName.Length <= 260
                            )
                        {
                            /* Try read the base name and compare with known NT kernel names. */
                            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uMte2.v64.BaseDllName.Buffer),
                                                        u.wsz, uMte2.v64.BaseDllName.Length);
                            u.wsz[uMte2.v64.BaseDllName.Length / 2] = '\0';
                            if (    RT_SUCCESS(rc)
                                &&  (   !RTUtf16ICmp(u.wsz, g_wszKernelNames[0])
                                  /* || !RTUtf16ICmp(u.wsz, g_wszKernelNames[1]) */
                                    )
                               )
                            {
                                /* Read the link entry of the previous entry in the list and check that its
                                   forward pointer points at the MTE we've found. */
                                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                                            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr,
                                                                                        uMte2.v64.InLoadOrderLinks.Blink),
                                                            &uMte3.v64, RT_SIZEOFMEMB(NTMTE64, InLoadOrderLinks));
                                if (   RT_SUCCESS(rc)
                                    && uMte3.v64.InLoadOrderLinks.Flink == MteAddr.FlatPtr
                                    && WINNT64_VALID_ADDRESS(uMte3.v64.InLoadOrderLinks.Blink) )
                                {
                                    Log(("DigWinNt: MteAddr=%RGv KernelAddr=%RGv SizeOfImage=%x &PsLoadedModuleList=%RGv (32-bit)\n",
                                         MteAddr.FlatPtr, KernelAddr.FlatPtr, uMte2.v64.SizeOfImage, Addr.FlatPtr));
                                    pThis->KernelAddr               = KernelAddr;
                                    pThis->KernelMteAddr            = MteAddr;
                                    pThis->PsLoadedModuleListAddr   = Addr;
                                    pThis->f32Bit                   = false;
                                    pThis->fNt31                    = false;
                                    return true;
                                }
                            }
                            else if (RT_SUCCESS(rc))
                            {
                                Log2(("DigWinNt: Wrong module: MteAddr=%RGv ImageAddr=%RGv SizeOfImage=%#x '%ls'\n",
                                      MteAddr.FlatPtr, KernelAddr.FlatPtr, uMte2.v64.SizeOfImage, u.wsz));
                                break; /* Not NT kernel */
                            }
                        }

                        /* next */
                        pVMM->pfnDBGFR3AddrAdd(&HitAddr, 8);
                        if (HitAddr.FlatPtr < uEnd)
                            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &HitAddr, uEnd - HitAddr.FlatPtr,
                                                        8 /*align*/, &uMte.v64.DllBase, 3 * sizeof(uint32_t), &HitAddr);
                        else
                            rc = VERR_DBGF_MEM_NOT_FOUND;
                    }
                }
            }
        }
    }

    return fRet;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerWinNtDestruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerWinNtConstruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERWINNT pThis = (PDBGDIGGERWINNT)pvData;
    pThis->fValid      = false;
    pThis->f32Bit      = false;
    pThis->enmVer      = DBGDIGGERWINNTVER_UNKNOWN;

    pThis->IWinNt.u32Magic               = DBGFOSIWINNT_MAGIC;
    pThis->IWinNt.pfnQueryVersion        = dbgDiggerWinNtIWinNt_QueryVersion;
    pThis->IWinNt.pfnQueryKernelPtrs     = dbgDiggerWinNtIWinNt_QueryKernelPtrs;
    pThis->IWinNt.pfnQueryKpcrForVCpu    = dbgDiggerWinNtIWinNt_QueryKpcrForVCpu;
    pThis->IWinNt.pfnQueryCurThrdForVCpu = dbgDiggerWinNtIWinNt_QueryCurThrdForVCpu;
    pThis->IWinNt.u32EndMagic            = DBGFOSIWINNT_MAGIC;

#ifdef VBOX_DEBUGGER_WITH_WIN_DBG_PRINT_HOOKING
    pThis->hBpDbgPrint      = NIL_DBGFBP;
    pThis->hBpOwnerDbgPrint = NIL_DBGFBPOWNER;
#endif

    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerWinNt =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGERWINNT),
    /* .szName = */                 "WinNT",
    /* .pfnConstruct = */           dbgDiggerWinNtConstruct,
    /* .pfnDestruct = */            dbgDiggerWinNtDestruct,
    /* .pfnProbe = */               dbgDiggerWinNtProbe,
    /* .pfnInit = */                dbgDiggerWinNtInit,
    /* .pfnRefresh = */             dbgDiggerWinNtRefresh,
    /* .pfnTerm = */                dbgDiggerWinNtTerm,
    /* .pfnQueryVersion = */        dbgDiggerWinNtQueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerWinNtQueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerWinNtStackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};

