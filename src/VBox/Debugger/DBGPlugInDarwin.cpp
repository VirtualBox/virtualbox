/* $Id: DBGPlugInDarwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInDarwin - Debugger and Guest OS Digger Plugin For Darwin / OS X.
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
#include <VBox/vmm/vmmr3vtable.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/ctype.h>
#include <iprt/formats/mach-o.h>

#undef LogRel2
#define LogRel2 LogRel


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name Internal Darwin structures
 * @{ */

/**
 * 32-bit darwin kernel module info structure (kmod_info_t).
 */
typedef struct OSX32_kmod_info
{
    uint32_t    next;
    int32_t     info_version;
    uint32_t    id;
    char        name[64];
    char        version[64];
    int32_t     reference_count;
    uint32_t    reference_list;         /**< Points to kmod_reference_t. */
    uint32_t    address;                /**< Where in memory the kext is loaded. */
    uint32_t    size;
    uint32_t    hdr_size;
    uint32_t    start;                  /**< Address of kmod_start_func_t. */
    uint32_t    stop;                   /**< Address of kmod_stop_func_t. */
} OSX32_kmod_info_t;

/**
 * 32-bit darwin kernel module info structure (kmod_info_t).
 */
#pragma pack(1)
typedef struct OSX64_kmod_info
{
    uint64_t    next;
    int32_t     info_version;
    uint32_t    id;
    char        name[64];
    char        version[64];
    int32_t     reference_count;
    uint64_t    reference_list;         /**< Points to kmod_reference_t. Misaligned, duh. */
    uint64_t    address;                /**< Where in memory the kext is loaded. */
    uint64_t    size;
    uint64_t    hdr_size;
    uint64_t    start;                  /**< Address of kmod_start_func_t. */
    uint64_t    stop;                   /**< Address of kmod_stop_func_t. */
} OSX64_kmod_info_t;
#pragma pack()

/** The value of the info_version field. */
#define OSX_KMOD_INFO_VERSION   INT32_C(1)

/** @} */


/**
 * Linux guest OS digger instance data.
 */
typedef struct DBGDIGGERDARWIN
{
    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool fValid;

    /** Set if 64-bit kernel, clear if 32-bit.
     *  Set during probing. */
    bool f64Bit;
    /** The address of an kernel version string (there are several).
     * This is set during probing. */
    DBGFADDRESS AddrKernelVersion;
    /** Kernel base address.
     * This is set during probing. */
    DBGFADDRESS AddrKernel;

    /** The kernel message log interface. */
    DBGFOSIDMESG IDmesg;
} DBGDIGGERDARWIN;
/** Pointer to the linux guest OS digger instance data. */
typedef DBGDIGGERDARWIN *PDBGDIGGERDARWIN;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a 32-bit darwin kernel address */
#define OSX32_VALID_ADDRESS(Addr)    ((Addr) > UINT32_C(0x00001000) && (Addr) < UINT32_C(0xfffff000))
/** Validates a 64-bit darwin kernel address */
#define OSX64_VALID_ADDRESS(Addr)    ((Addr) > UINT64_C(0xffff800000000000) && (Addr) < UINT64_C(0xfffffffffffff000))
/** Validates a 32-bit or 64-bit darwin kernel address. */
#define OSX_VALID_ADDRESS(a_f64Bits, a_Addr) \
    ((a_f64Bits) ? OSX64_VALID_ADDRESS(a_Addr) : OSX32_VALID_ADDRESS(a_Addr))

/** AppleOsX on little endian ASCII systems. */
#define DIG_DARWIN_MOD_TAG              UINT64_C(0x58734f656c707041)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerDarwinInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);



/**
 * @interface_method_impl{DBGFOSIDMESG,pfnQueryKernelLog}
 */
static DECLCALLBACK(int) dbgDiggerDarwinIDmsg_QueryKernelLog(PDBGFOSIDMESG pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, uint32_t fFlags,
                                                             uint32_t cMessages, char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    RT_NOREF1(fFlags);
    PDBGDIGGERDARWIN pData = RT_FROM_MEMBER(pThis, DBGDIGGERDARWIN, IDmesg);

    if (cMessages < 1)
        return VERR_INVALID_PARAMETER;

    /*
     * The 'msgbufp' variable points to a struct msgbuf (bsd/kern/subr_log.c).
     */
    RTDBGAS  hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    RTDBGMOD hMod;
    int rc = RTDbgAsModuleByName(hAs, "mach_kernel", 0, &hMod);
    if (RT_FAILURE(rc))
        return VERR_NOT_FOUND;
    RTDbgAsRelease(hAs);

    DBGFADDRESS Addr;
    RTGCPTR     GCPtrMsgBufP = 0;
    RTDBGSYMBOL SymInfo;
    rc = RTDbgModSymbolByName(hMod, "_msgbufp", &SymInfo);
    if (RT_SUCCESS(rc))
    {
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SymInfo.Value + pData->AddrKernel.FlatPtr),
                                    &GCPtrMsgBufP, pData->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t));
        if (RT_FAILURE(rc))
        {
            LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: failed to read _msgbufp at %RGv: %Rrc\n", Addr.FlatPtr, rc));
            return VERR_NOT_FOUND;
        }
        if (!OSX_VALID_ADDRESS(pData->f64Bit, GCPtrMsgBufP))
        {
            LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: Invalid address for _msgbufp: %RGv\n", GCPtrMsgBufP));
            return VERR_NOT_FOUND;
        }
    }
    else
    {
        rc = RTDbgModSymbolByName(hMod, "_msgbuf", &SymInfo);
        if (RT_FAILURE(rc))
        {
            LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: failed to find _msgbufp and _msgbuf: %Rrc\n", rc));
            return VERR_NOT_FOUND;
        }
        GCPtrMsgBufP = SymInfo.Value + pData->AddrKernel.FlatPtr;
        if (!OSX_VALID_ADDRESS(pData->f64Bit, GCPtrMsgBufP))
        {
            LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: Invalid address for _msgbuf: %RGv\n", GCPtrMsgBufP));
            return VERR_NOT_FOUND;
        }
    }

    /*
     * Read the msgbuf structure.
     */
    struct
    {
        uint32_t msg_magic;
        uint32_t msg_size;
        uint32_t msg_bufx;
        uint32_t msg_bufr;
        uint64_t msg_bufc; /**< Size depends on windows size. */
    } MsgBuf;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, GCPtrMsgBufP),
                                &MsgBuf, sizeof(MsgBuf) - (pData->f64Bit ? 0 : sizeof(uint32_t)) );
    if (RT_FAILURE(rc))
    {
        LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: failed to read msgbuf struct at %RGv: %Rrc\n", Addr.FlatPtr, rc));
        return VERR_NOT_FOUND;
    }
    if (!pData->f64Bit)
        MsgBuf.msg_bufc &= UINT32_MAX;

    /*
     * Validate the structure.
     */
    if (   MsgBuf.msg_magic != UINT32_C(0x63061)
        || MsgBuf.msg_size < UINT32_C(4096)
        || MsgBuf.msg_size > 16*_1M
        || MsgBuf.msg_bufx > MsgBuf.msg_size
        || MsgBuf.msg_bufr > MsgBuf.msg_size
        || !OSX_VALID_ADDRESS(pData->f64Bit, MsgBuf.msg_bufc) )
    {
        LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: Invalid MsgBuf data: magic=%#x size=%#x bufx=%#x bufr=%#x bufc=%RGv\n",
                MsgBuf.msg_magic, MsgBuf.msg_size, MsgBuf.msg_bufx, MsgBuf.msg_bufr, MsgBuf.msg_bufc));
        return VERR_INVALID_STATE;
    }

    /*
     * Read the buffer.
     */
    char *pchMsgBuf = (char *)RTMemAlloc(MsgBuf.msg_size);
    if (!pchMsgBuf)
    {
        LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: Failed to allocate %#x bytes of memory for the log buffer\n",
                MsgBuf.msg_size));
        return VERR_INVALID_STATE;
    }
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, MsgBuf.msg_bufc), pchMsgBuf, MsgBuf.msg_size);
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy it out raw.
         */
        uint32_t offDst = 0;
        if (MsgBuf.msg_bufr < MsgBuf.msg_bufx)
        {
            /* Single chunk between the read and write offsets. */
            uint32_t cbToCopy = MsgBuf.msg_bufx - MsgBuf.msg_bufr;
            if (cbToCopy < cbBuf)
            {
                memcpy(pszBuf, &pchMsgBuf[MsgBuf.msg_bufr], cbToCopy);
                pszBuf[cbToCopy] = '\0';
                rc = VINF_SUCCESS;
            }
            else
            {
                if (cbBuf)
                {
                    memcpy(pszBuf, &pchMsgBuf[MsgBuf.msg_bufr], cbBuf - 1);
                    pszBuf[cbBuf - 1] = '\0';
                }
                rc = VERR_BUFFER_OVERFLOW;
            }
            offDst = cbToCopy + 1;
        }
        else
        {
            /* Two chunks, read offset to end, start to write offset. */
            uint32_t cbFirst  = MsgBuf.msg_size - MsgBuf.msg_bufr;
            uint32_t cbSecond = MsgBuf.msg_bufx;
            if (cbFirst + cbSecond < cbBuf)
            {
                memcpy(pszBuf, &pchMsgBuf[MsgBuf.msg_bufr], cbFirst);
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
                    memcpy(pszBuf, &pchMsgBuf[MsgBuf.msg_bufr], cbFirst);
                    memcpy(&pszBuf[cbFirst], pchMsgBuf, cbBuf - cbFirst);
                    pszBuf[cbBuf - 1] = '\0';
                }
                else if (cbBuf)
                {
                    memcpy(pszBuf, &pchMsgBuf[MsgBuf.msg_bufr], cbBuf - 1);
                    pszBuf[cbBuf - 1] = '\0';
                }
                rc = VERR_BUFFER_OVERFLOW;
            }
        }

        if (pcbActual)
            *pcbActual = offDst;
    }
    else
        LogRel(("dbgDiggerDarwinIDmsg_QueryKernelLog: Error reading %#x bytes at %RGv: %Rrc\n",
                MsgBuf.msg_size, MsgBuf.msg_bufc, rc));
    RTMemFree(pchMsgBuf);
    return rc;
}


/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerDarwinStackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                          PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState, PCCPUMCTX pInitialCtx,
                                                          RTDBGAS hAs, uint64_t *puScratch)
{
    RT_NOREF(pUVM, pVMM, pvData, idCpu, pFrame, pState, pInitialCtx, hAs, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerDarwinQueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;
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
static DECLCALLBACK(int)  dbgDiggerDarwinQueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                      char *pszVersion, size_t cchVersion)
{
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;
    Assert(pThis->fValid);

    /*
     * It's all in the linux banner.
     */
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, &pThis->AddrKernelVersion, pszVersion, cchVersion);
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
static DECLCALLBACK(void)  dbgDiggerDarwinTerm(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerDarwinRefresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;
    NOREF(pThis);
    Assert(pThis->fValid);

    /*
     * For now we'll flush and reload everything.
     */
    dbgDiggerDarwinTerm(pUVM, pVMM, pvData);
    return dbgDiggerDarwinInit(pUVM, pVMM, pvData);
}


/**
 * Helper function that tries to accertain whether a segment (__LINKEDIT) is
 * present or not.
 *
 * @returns true if present, false if not.
 * @param   pUVM                The user mode VM structure.
 * @param   pVMM                The VMM function table.
 * @param   uSegAddr            The segment addresss.
 * @param   cbSeg               The segment size.
 * @param   uMinAddr            Lowest allowed address.
 * @param   uMaxAddr            Highest allowed address.
 */
static bool dbgDiggerDarwinIsSegmentPresent(PUVM pUVM, PCVMMR3VTABLE pVMM, uint64_t uSegAddr, uint64_t cbSeg,
                                            uint64_t uMinAddr, uint64_t uMaxAddr)
{
    /*
     * Validate the size and address.
     */
    if (cbSeg < 32)
    {
        LogRel(("OSXDig: __LINKEDIT too small %#RX64\n", cbSeg));
        return false;
    }
    if (cbSeg > uMaxAddr - uMinAddr)
    {
        LogRel(("OSXDig: __LINKEDIT too big %#RX64, max %#RX64\n", cbSeg, uMaxAddr - uMinAddr));
        return false;
    }

    if (uSegAddr < uMinAddr)
    {
        LogRel(("OSXDig: __LINKEDIT too low %#RX64, min %#RX64\n", uSegAddr, uMinAddr));
        return false;
    }
    if (uSegAddr > uMaxAddr)
    {
        LogRel(("OSXDig: __LINKEDIT too high %#RX64, max %#RX64\n", uSegAddr, uMaxAddr));
        return false;
    }
    if (uSegAddr + cbSeg > uMaxAddr)
    {
        LogRel(("OSXDig: __LINKEDIT ends too high %#RX64 (%#RX64+%#RX64), max %#RX64\n",
                 uSegAddr + cbSeg, uSegAddr, cbSeg, uMaxAddr));
        return false;
    }

    /*
     * Check that all the pages are present.
     */
    cbSeg    += uSegAddr & GUEST_PAGE_OFFSET_MASK;
    uSegAddr &= ~(uint64_t)GUEST_PAGE_OFFSET_MASK;
    for (;;)
    {
        uint8_t     abBuf[8];
        DBGFADDRESS Addr;
        int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uSegAddr),
                                        abBuf, sizeof(abBuf));
        if (RT_FAILURE(rc))
        {
            LogRel(("OSXDig: __LINKEDIT read error at %#RX64: %Rrc\n", uSegAddr, rc));
            return false;
        }

        /* Advance */
        if (cbSeg <= GUEST_PAGE_SIZE)
            return true;
        cbSeg    -= GUEST_PAGE_SIZE;
        uSegAddr += GUEST_PAGE_SIZE;
    }
}


/**
 * Helper function that validates a segment (or section) name.
 *
 * @returns true if valid, false if not.
 * @param   pszName             The name string.
 * @param   cbName              The size of the string, including terminator.
 */
static bool dbgDiggerDarwinIsValidSegOrSectName(const char *pszName, size_t cbName)
{
    /* ascii chars */
    char ch;
    size_t off = 0;
    while (off < cbName && (ch = pszName[off]))
    {
        if (RT_C_IS_CNTRL(ch) || ch >= 127)
            return false;
        off++;
    }

    /* Not empty nor 100% full. */
    if (off == 0 || off == cbName)
        return false;

    /* remainder should be zeros. */
    while (off < cbName)
    {
        if (pszName[off])
            return false;
        off++;
    }

    return true;
}


static int dbgDiggerDarwinAddModule(PDBGDIGGERDARWIN pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                    uint64_t uModAddr, const char *pszName, bool *pf64Bit)
{
    RT_NOREF1(pThis);
    union
    {
        uint8_t             ab[2 * GUEST_PAGE_SIZE];
        mach_header_64_t    Hdr64;
        mach_header_32_t    Hdr32;
    } uBuf;

    /* Read the first page of the image. */
    DBGFADDRESS ModAddr;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModAddr, uModAddr), uBuf.ab, GUEST_PAGE_SIZE);
    if (RT_FAILURE(rc))
        return rc;

    /* Validate the header. */
    AssertCompileMembersSameSizeAndOffset(mach_header_64_t, magic,   mach_header_32_t, magic);
    if (   uBuf.Hdr64.magic != IMAGE_MACHO64_SIGNATURE
        && uBuf.Hdr32.magic != IMAGE_MACHO32_SIGNATURE)
        return VERR_INVALID_EXE_SIGNATURE;
    AssertCompileMembersSameSizeAndOffset(mach_header_64_t, cputype, mach_header_32_t, cputype);
    bool f64Bit = uBuf.Hdr64.magic == IMAGE_MACHO64_SIGNATURE;
    if (uBuf.Hdr32.cputype != (f64Bit ? CPU_TYPE_X86_64 : CPU_TYPE_I386))
        return VERR_LDR_ARCH_MISMATCH;
    AssertCompileMembersSameSizeAndOffset(mach_header_64_t, filetype, mach_header_32_t, filetype);
    if (   uBuf.Hdr32.filetype != MH_EXECUTE
        && uBuf.Hdr32.filetype != (f64Bit ? MH_KEXT_BUNDLE : MH_OBJECT))
        return VERR_BAD_EXE_FORMAT;
    AssertCompileMembersSameSizeAndOffset(mach_header_64_t, ncmds, mach_header_32_t, ncmds);
    if (uBuf.Hdr32.ncmds > 256)
        return VERR_BAD_EXE_FORMAT;
    AssertCompileMembersSameSizeAndOffset(mach_header_64_t, sizeofcmds, mach_header_32_t, sizeofcmds);
    if (uBuf.Hdr32.sizeofcmds > GUEST_PAGE_SIZE * 2 - sizeof(mach_header_64_t))
        return VERR_BAD_EXE_FORMAT;

    /* Do we need to read a 2nd page to get all the load commands? If so, do it. */
    if (uBuf.Hdr32.sizeofcmds + (f64Bit ? sizeof(mach_header_64_t) : sizeof(mach_header_32_t)) > GUEST_PAGE_SIZE)
    {
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModAddr, uModAddr + GUEST_PAGE_SIZE),
                                    &uBuf.ab[GUEST_PAGE_SIZE], GUEST_PAGE_SIZE);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Process the load commands.
     */
    RTUUID          Uuid   = RTUUID_INITIALIZE_NULL;
    RTDBGSEGMENT    aSegs[24];
    uint32_t        cSegs  = 0;
    bool            fHasLinkEdit = false;
    uint32_t        cLeft  = uBuf.Hdr32.ncmds;
    uint32_t        cbLeft = uBuf.Hdr32.sizeofcmds;
    union
    {
        uint8_t const              *pb;
        load_command_t const       *pGenric;
        segment_command_32_t const *pSeg32;
        segment_command_64_t const *pSeg64;
        uuid_command_t const       *pUuid;
    } uLCmd;
    uLCmd.pb = &uBuf.ab[f64Bit ? sizeof(mach_header_64_t) : sizeof(mach_header_32_t)];

    while (cLeft-- > 0)
    {
        uint32_t const cbCmd = uLCmd.pGenric->cmdsize;
        if (cbCmd > cbLeft || cbCmd < sizeof(load_command_t))
            return VERR_BAD_EXE_FORMAT;

        switch (uLCmd.pGenric->cmd)
        {
            case LC_SEGMENT_32:
                if (cbCmd != sizeof(segment_command_32_t) + uLCmd.pSeg32->nsects * sizeof(section_32_t))
                    return VERR_BAD_EXE_FORMAT;
                if (!dbgDiggerDarwinIsValidSegOrSectName(uLCmd.pSeg32->segname, sizeof(uLCmd.pSeg32->segname)))
                    return VERR_INVALID_NAME;
                if (   !strcmp(uLCmd.pSeg32->segname, "__LINKEDIT")
                    && !(fHasLinkEdit = dbgDiggerDarwinIsSegmentPresent(pUVM, pVMM, uLCmd.pSeg32->vmaddr, uLCmd.pSeg32->vmsize,
                                                                        uModAddr, uModAddr + _64M)))
                    break; /* This usually is discarded or not loaded at all. */
                if (cSegs >= RT_ELEMENTS(aSegs))
                    return VERR_BUFFER_OVERFLOW;
                aSegs[cSegs].Address = uLCmd.pSeg32->vmaddr;
                aSegs[cSegs].uRva    = uLCmd.pSeg32->vmaddr - uModAddr;
                aSegs[cSegs].cb      = uLCmd.pSeg32->vmsize;
                aSegs[cSegs].fFlags  = uLCmd.pSeg32->flags; /* Abusing the flags field here... */
                aSegs[cSegs].iSeg    = cSegs;
                AssertCompile(RTDBG_SEGMENT_NAME_LENGTH > sizeof(uLCmd.pSeg32->segname));
                strcpy(aSegs[cSegs].szName, uLCmd.pSeg32->segname);
                cSegs++;
                break;

            case LC_SEGMENT_64:
                if (cbCmd != sizeof(segment_command_64_t) + uLCmd.pSeg64->nsects * sizeof(section_64_t))
                    return VERR_BAD_EXE_FORMAT;
                if (!dbgDiggerDarwinIsValidSegOrSectName(uLCmd.pSeg64->segname, sizeof(uLCmd.pSeg64->segname)))
                    return VERR_INVALID_NAME;
                if (   !strcmp(uLCmd.pSeg64->segname, "__LINKEDIT")
                    && !(fHasLinkEdit = dbgDiggerDarwinIsSegmentPresent(pUVM, pVMM, uLCmd.pSeg64->vmaddr, uLCmd.pSeg64->vmsize,
                                                                        uModAddr, uModAddr + _128M)))
                    break; /* This usually is discarded or not loaded at all. */
                if (cSegs >= RT_ELEMENTS(aSegs))
                    return VERR_BUFFER_OVERFLOW;
                aSegs[cSegs].Address = uLCmd.pSeg64->vmaddr;
                aSegs[cSegs].uRva    = uLCmd.pSeg64->vmaddr - uModAddr;
                aSegs[cSegs].cb      = uLCmd.pSeg64->vmsize;
                aSegs[cSegs].fFlags  = uLCmd.pSeg64->flags; /* Abusing the flags field here... */
                aSegs[cSegs].iSeg    = cSegs;
                AssertCompile(RTDBG_SEGMENT_NAME_LENGTH > sizeof(uLCmd.pSeg64->segname));
                strcpy(aSegs[cSegs].szName, uLCmd.pSeg64->segname);
                cSegs++;
                break;

            case LC_UUID:
                if (cbCmd != sizeof(uuid_command_t))
                    return VERR_BAD_EXE_FORMAT;
                if (RTUuidIsNull((PCRTUUID)&uLCmd.pUuid->uuid[0]))
                    return VERR_BAD_EXE_FORMAT;
                memcpy(&Uuid, &uLCmd.pUuid->uuid[0], sizeof(uLCmd.pUuid->uuid));
                break;

            default:
                /* Current known max plus a lot of slack. */
                if (uLCmd.pGenric->cmd > LC_DYLIB_CODE_SIGN_DRS + 32)
                    return VERR_BAD_EXE_FORMAT;
                break;
        }

        /* next */
        cbLeft   -= cbCmd;
        uLCmd.pb += cbCmd;
    }

    if (cbLeft != 0)
    {
        LogRel(("OSXDig: uModAddr=%#RX64 - %u bytes of command left over!\n", uModAddr, cbLeft));
        return VERR_BAD_EXE_FORMAT;
    }

    /*
     * Some post processing checks.
     */
    uint32_t iSeg;
    for (iSeg = 0; iSeg < cSegs; iSeg++)
        if (aSegs[iSeg].Address == uModAddr)
            break;
    if (iSeg >= cSegs)
    {
        LogRel2(("OSXDig: uModAddr=%#RX64 was not found among the segments segments\n", uModAddr));
        return VERR_ADDRESS_CONFLICT;
    }

    /*
     * Create a debug module.
     */
    RTDBGMOD hMod;
    rc = RTDbgModCreateFromMachOImage(&hMod, pszName, NULL, f64Bit ? RTLDRARCH_AMD64 : RTLDRARCH_X86_32, NULL /*phLdrModIn*/,
                                      0 /*cbImage*/, cSegs, aSegs, &Uuid, pVMM->pfnDBGFR3AsGetConfig(pUVM),
                                      RTDBGMOD_F_NOT_DEFERRED | (fHasLinkEdit ? RTDBGMOD_F_MACHO_LOAD_LINKEDIT : 0));


    /*
     * If module creation failed and we've got a linkedit segment, try open the
     * image in-memory, because that will at a minimum give us symbol table symbols.
     */
    if (RT_FAILURE(rc) && fHasLinkEdit)
    {
        DBGFADDRESS DbgfAddr;
        RTERRINFOSTATIC ErrInfo;
        rc = pVMM->pfnDBGFR3ModInMem(pUVM, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &DbgfAddr, uModAddr),
                                     DBGFMODINMEM_F_NO_CONTAINER_FALLBACK,
                                     pszName, NULL /*pszFilename*/, f64Bit ? RTLDRARCH_AMD64 : RTLDRARCH_X86_32, 0 /*cbImage */,
                                     &hMod, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            LogRel(("OSXDig: Failed to do an in-memory-opening of '%s' at %#RX64: %Rrc%s%s\n", pszName, uModAddr, rc,
                    RTErrInfoIsSet(&ErrInfo.Core) ? " - " : "", RTErrInfoIsSet(&ErrInfo.Core) ? ErrInfo.Core.pszMsg : ""));
    }

    /*
     * Final fallback is a container module.
     */
    if (RT_FAILURE(rc))
    {
        rc = RTDbgModCreate(&hMod, pszName, 0, 0);
        if (RT_FAILURE(rc))
            return rc;

        uint64_t uRvaNext = 0;
        for (iSeg = 0; iSeg < cSegs && RT_SUCCESS(rc); iSeg++)
        {
            if (   aSegs[iSeg].uRva > uRvaNext
                && aSegs[iSeg].uRva - uRvaNext < _1M)
                uRvaNext = aSegs[iSeg].uRva;
            rc = RTDbgModSegmentAdd(hMod, aSegs[iSeg].uRva, aSegs[iSeg].cb, aSegs[iSeg].szName, 0, NULL);
            if (aSegs[iSeg].cb > 0 && RT_SUCCESS(rc))
            {
                static const char s_szSuffix[] = "_start";
                size_t const      cchName = RTStrNLen(aSegs[iSeg].szName, sizeof(aSegs[iSeg].szName));
                char              szTmp[sizeof(aSegs[iSeg].szName) + sizeof(s_szSuffix)];
                memcpy(mempcpy(szTmp, aSegs[iSeg].szName, cchName), s_szSuffix, sizeof(s_szSuffix));
                rc = RTDbgModSymbolAdd(hMod, szTmp, iSeg, 0 /*uRva*/, 0 /*cb*/, 0 /*fFlags*/, NULL);
            }
            uRvaNext += aSegs[iSeg].cb;
        }

        if (RT_FAILURE(rc))
        {
            RTDbgModRelease(hMod);
            return rc;
        }
    }

    /* Tag the module. */
    rc = RTDbgModSetTag(hMod, DIG_DARWIN_MOD_TAG);
    AssertRC(rc);

    /*
     * Link the module.
     */
    RTDBGAS hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    if (hAs != NIL_RTDBGAS)
    {
        //uint64_t uRvaNext = 0; - what was this?
        uint32_t cLinked  = 0;
        iSeg = cSegs;
        while (iSeg-- > 0) /* HACK: Map in reverse order to avoid replacing __TEXT. */
            if (aSegs[iSeg].cb)
            {
                /* Find matching segment in the debug module. */
                uint32_t iDbgSeg = 0;
                while (iDbgSeg < cSegs)
                {
                    RTDBGSEGMENT SegInfo;
                    int rc3 = RTDbgModSegmentByIndex(hMod, iDbgSeg, &SegInfo);
                    if (RT_SUCCESS(rc3) && !strcmp(SegInfo.szName, aSegs[iSeg].szName))
                        break;
                    iDbgSeg++;
                }
                AssertMsgStmt(iDbgSeg < cSegs, ("%s\n", aSegs[iSeg].szName), continue);

                /* Map it. */
                int rc2 = RTDbgAsModuleLinkSeg(hAs, hMod, iDbgSeg, aSegs[iSeg].Address, RTDBGASLINK_FLAGS_REPLACE /*fFlags*/);
                if (RT_SUCCESS(rc2))
                    cLinked++;
                else if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        if (RT_FAILURE(rc) && cLinked != 0)
            rc = -rc;
    }
    else
        rc = VERR_INTERNAL_ERROR;

    RTDbgModRelease(hMod);
    RTDbgAsRelease(hAs);

    if (pf64Bit)
        *pf64Bit = f64Bit;
    return rc;
}


static bool dbgDiggerDarwinIsValidName(const char *pszName)
{
    char ch;
    while ((ch = *pszName++) != '\0')
    {
        if (ch < 0x20 || ch >= 127)
            return false;
    }
    return true;
}


static bool dbgDiggerDarwinIsValidVersion(const char *pszVersion)
{
    char ch;
    while ((ch = *pszVersion++) != '\0')
    {
        if (ch < 0x20 || ch >= 127)
            return false;
    }
    return true;
}


/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerDarwinInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;
    Assert(!pThis->fValid);

    /*
     * Add the kernel module.
     */
    bool f64Bit;
    int rc = dbgDiggerDarwinAddModule(pThis, pUVM, pVMM, pThis->AddrKernel.FlatPtr, "mach_kernel", &f64Bit);
    if (RT_SUCCESS(rc))
    {
        /*
         * The list of modules can be found at the 'kmod' symbol, that means
         * that we currently require some kind of symbol file for the kernel
         * to be loaded at this point.
         *
         * Note! Could also use the 'gLoadedKextSummaries', but I don't think
         *       it's any easier to find without any kernel map than 'kmod'.
         */
        RTDBGSYMBOL SymInfo;
        rc = pVMM->pfnDBGFR3AsSymbolByName(pUVM, DBGF_AS_KERNEL, "mach_kernel!kmod", &SymInfo, NULL);
        if (RT_FAILURE(rc))
            rc = pVMM->pfnDBGFR3AsSymbolByName(pUVM, DBGF_AS_KERNEL, "mach_kernel!_kmod", &SymInfo, NULL);
        if (RT_SUCCESS(rc))
        {
            DBGFADDRESS AddrModInfo;
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrModInfo, SymInfo.Value);

            /* Read the variable. */
            RTUINT64U uKmodValue = { 0 };
            if (f64Bit)
                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrModInfo, &uKmodValue.u, sizeof(uKmodValue.u));
            else
                rc = pVMM->pfnDBGFR3MemRead (pUVM, 0 /*idCpu*/, &AddrModInfo, &uKmodValue.s.Lo, sizeof(uKmodValue.s.Lo));
            if (RT_SUCCESS(rc))
            {
                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrModInfo, uKmodValue.u);

                /* Walk the list of modules. */
                uint32_t cIterations = 0;
                while (AddrModInfo.FlatPtr != 0)
                {
                    /* Some extra loop conditions... */
                    if (!OSX_VALID_ADDRESS(f64Bit, AddrModInfo.FlatPtr))
                    {
                        LogRel(("OSXDig: Invalid kmod_info pointer: %RGv\n", AddrModInfo.FlatPtr));
                        break;
                    }
                    if (AddrModInfo.FlatPtr == uKmodValue.u && cIterations != 0)
                    {
                        LogRel(("OSXDig: kmod_info list looped back to the start.\n"));
                        break;
                    }
                    if (cIterations++ >= 2048)
                    {
                        LogRel(("OSXDig: Too many mod_info loops (%u)\n", cIterations));
                        break;
                    }

                    /*
                     * Read the kmod_info_t structure.
                     */
                    union
                    {
                        OSX64_kmod_info_t   Info64;
                        OSX32_kmod_info_t   Info32;
                    } uMod;
                    RT_ZERO(uMod);
                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &AddrModInfo, &uMod,
                                                f64Bit ? sizeof(uMod.Info64) : sizeof(uMod.Info32));
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("OSXDig: Error reading kmod_info structure at %RGv: %Rrc\n", AddrModInfo.FlatPtr, rc));
                        break;
                    }

                    /*
                     * Validate the kmod_info_t structure.
                     */
                    int32_t iInfoVer = f64Bit ? uMod.Info64.info_version : uMod.Info32.info_version;
                    if (iInfoVer != OSX_KMOD_INFO_VERSION)
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad info_version %d\n", AddrModInfo.FlatPtr, iInfoVer));
                        break;
                    }

                    const char *pszName = f64Bit ? uMod.Info64.name : uMod.Info32.name;
                    if (   !*pszName
                        || !RTStrEnd(pszName, sizeof(uMod.Info64.name))
                        || !dbgDiggerDarwinIsValidName(pszName) )
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad name '%.*s'\n", AddrModInfo.FlatPtr,
                                sizeof(uMod.Info64.name), pszName));
                        break;
                    }

                    const char *pszVersion = f64Bit ? uMod.Info64.version : uMod.Info32.version;
                    if (   !RTStrEnd(pszVersion, sizeof(uMod.Info64.version))
                        || !dbgDiggerDarwinIsValidVersion(pszVersion) )
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad version '%.*s'\n", AddrModInfo.FlatPtr,
                             sizeof(uMod.Info64.version), pszVersion));
                        break;
                    }

                    int32_t cRefs = f64Bit ? uMod.Info64.reference_count : uMod.Info32.reference_count;
                    if (cRefs < -1 || cRefs > 16384)
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad reference_count %d\n", AddrModInfo.FlatPtr, cRefs));
                        break;
                    }

                    uint64_t uImageAddr = f64Bit ? uMod.Info64.address : uMod.Info32.address;
                    if (!OSX_VALID_ADDRESS(f64Bit, uImageAddr))
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad address %#llx\n", AddrModInfo.FlatPtr, uImageAddr));
                        break;
                    }

                    uint64_t cbImage = f64Bit ? uMod.Info64.size : uMod.Info32.size;
                    if (cbImage > 64U*_1M)
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad size %#llx\n", AddrModInfo.FlatPtr, cbImage));
                        break;
                    }

                    uint64_t cbHdr = f64Bit ? uMod.Info64.hdr_size : uMod.Info32.hdr_size;
                    if (cbHdr > 16U*_1M)
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad hdr_size %#llx\n", AddrModInfo.FlatPtr, cbHdr));
                        break;
                    }

                    uint64_t uStartAddr = f64Bit ? uMod.Info64.start : uMod.Info32.start;
                    if (!uStartAddr && !OSX_VALID_ADDRESS(f64Bit, uStartAddr))
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad start function %#llx\n", AddrModInfo.FlatPtr, uStartAddr));
                        break;
                    }

                    uint64_t uStopAddr = f64Bit ? uMod.Info64.stop : uMod.Info32.stop;
                    if (!uStopAddr && !OSX_VALID_ADDRESS(f64Bit, uStopAddr))
                    {
                        LogRel(("OSXDig: kmod_info @%RGv: Bad stop function %#llx\n", AddrModInfo.FlatPtr, uStopAddr));
                        break;
                    }

                    /*
                     * Try add the module.
                     */
                    rc = dbgDiggerDarwinAddModule(pThis, pUVM, pVMM, uImageAddr, pszName, NULL);
                    LogRel(("OSXDig: kmod_info @%RGv: '%s' ver '%s', image @%#llx LB %#llx cbHdr=%#llx -> %Rrc\n", AddrModInfo.FlatPtr,
                            pszName, pszVersion, uImageAddr, cbImage, cbHdr, rc));

                    /*
                     * Advance to the next kmod_info entry.
                     */
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrModInfo, f64Bit ? uMod.Info64.next : uMod.Info32.next);
                }
            }
            else
                LogRel(("OSXDig: Error reading the 'kmod' variable: %Rrc\n", rc));
        }
        else
            LogRel(("OSXDig: Failed to locate the 'kmod' variable in mach_kernel.\n"));

        pThis->fValid = true;
        return VINF_SUCCESS;
    }

    return rc;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerDarwinProbe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;

    /*
     * Look for a section + segment combo that normally only occures in
     * mach_kernel.  Follow it up with probing of the rest of the executable
     * header.  We must search a largish area because the more recent versions
     * of darwin have random load address for security raisins.
     */
    static struct { uint64_t uStart, uEnd; } const s_aRanges[] =
    {
        /* 64-bit: */
        { UINT64_C(0xffffff8000000000), UINT64_C(0xffffff81ffffffff), },

        /* 32-bit - always search for this because of the hybrid 32-bit kernel
           with cpu in long mode that darwin used for a number of versions. */
        { UINT64_C(0x00001000), UINT64_C(0x0ffff000), }
    };
    for (unsigned iRange = pVMM->pfnDBGFR3CpuGetMode(pUVM, 0 /*idCpu*/) != CPUMMODE_LONG;
          iRange < RT_ELEMENTS(s_aRanges);
          iRange++)
    {
        DBGFADDRESS     KernelAddr;
        for (pVMM->pfnDBGFR3AddrFromFlat(pUVM, &KernelAddr, s_aRanges[iRange].uStart);
             KernelAddr.FlatPtr < s_aRanges[iRange].uEnd;
             KernelAddr.FlatPtr += GUEST_PAGE_SIZE)
        {
            static const uint8_t s_abNeedle[16 + 16] =
            {
                '_','_','t','e','x','t',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* section_32_t::sectname */
                '_','_','K','L','D',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* section_32_t::segname. */
            };

            int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &KernelAddr, s_aRanges[iRange].uEnd - KernelAddr.FlatPtr,
                                            1, s_abNeedle, sizeof(s_abNeedle), &KernelAddr);
            if (RT_FAILURE(rc))
                break;
            pVMM->pfnDBGFR3AddrSub(&KernelAddr, KernelAddr.FlatPtr & GUEST_PAGE_OFFSET_MASK);

            /*
             * Read the first page of the image and check the headers.
             */
            union
            {
                uint8_t             ab[GUEST_PAGE_SIZE];
                mach_header_64_t    Hdr64;
                mach_header_32_t    Hdr32;
            } uBuf;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &KernelAddr, uBuf.ab, GUEST_PAGE_SIZE);
            if (RT_FAILURE(rc))
                continue;
            AssertCompileMembersSameSizeAndOffset(mach_header_64_t, magic,   mach_header_32_t, magic);
            if (   uBuf.Hdr64.magic != IMAGE_MACHO64_SIGNATURE
                && uBuf.Hdr32.magic != IMAGE_MACHO32_SIGNATURE)
                continue;
            AssertCompileMembersSameSizeAndOffset(mach_header_64_t, cputype, mach_header_32_t, cputype);
            bool f64Bit = uBuf.Hdr64.magic == IMAGE_MACHO64_SIGNATURE;
            if (uBuf.Hdr32.cputype != (f64Bit ? CPU_TYPE_X86_64 : CPU_TYPE_I386))
                continue;
            AssertCompileMembersSameSizeAndOffset(mach_header_64_t, filetype, mach_header_32_t, filetype);
            if (uBuf.Hdr32.filetype != MH_EXECUTE)
                continue;
            AssertCompileMembersSameSizeAndOffset(mach_header_64_t, ncmds, mach_header_32_t, ncmds);
            if (uBuf.Hdr32.ncmds > 256)
                continue;
            AssertCompileMembersSameSizeAndOffset(mach_header_64_t, sizeofcmds, mach_header_32_t, sizeofcmds);
            if (uBuf.Hdr32.sizeofcmds > GUEST_PAGE_SIZE * 2 - sizeof(mach_header_64_t))
                continue;

            /* Seems good enough for now.

               If the above causes false positives, check the segments and make
               sure there is a kernel version string in the right one. */
            pThis->AddrKernel = KernelAddr;
            pThis->f64Bit     = f64Bit;

            /*
             * Finally, find the kernel version string.
             */
            rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, &KernelAddr, 32*_1M, 1, RT_STR_TUPLE("Darwin Kernel Version"),
                                        &pThis->AddrKernelVersion);
            if (RT_FAILURE(rc))
                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &pThis->AddrKernelVersion, 0);
            return true;
        }
    }
    return false;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerDarwinDestruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerDarwinConstruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM);
    PDBGDIGGERDARWIN pThis = (PDBGDIGGERDARWIN)pvData;

    pThis->IDmesg.u32Magic = DBGFOSIDMESG_MAGIC;
    pThis->IDmesg.pfnQueryKernelLog = dbgDiggerDarwinIDmsg_QueryKernelLog;
    pThis->IDmesg.u32EndMagic = DBGFOSIDMESG_MAGIC;

    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerDarwin =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGERDARWIN),
    /* .szName = */                 "Darwin",
    /* .pfnConstruct = */           dbgDiggerDarwinConstruct,
    /* .pfnDestruct = */            dbgDiggerDarwinDestruct,
    /* .pfnProbe = */               dbgDiggerDarwinProbe,
    /* .pfnInit = */                dbgDiggerDarwinInit,
    /* .pfnRefresh = */             dbgDiggerDarwinRefresh,
    /* .pfnTerm = */                dbgDiggerDarwinTerm,
    /* .pfnQueryVersion = */        dbgDiggerDarwinQueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerDarwinQueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerDarwinStackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};

