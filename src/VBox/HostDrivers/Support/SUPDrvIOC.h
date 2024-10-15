/* $Id: SUPDrvIOC.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - IOCtl definitions.
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

#ifndef VBOX_INCLUDED_SRC_Support_SUPDrvIOC_h
#define VBOX_INCLUDED_SRC_Support_SUPDrvIOC_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <VBox/sup.h>

/*
 * IOCtl numbers.
 * We're using the Win32 type of numbers here, thus the macros below.
 * The SUP_IOCTL_FLAG macro is used to separate requests from 32-bit
 * and 64-bit processes.
 */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_SPARC64) || defined(RT_ARCH_ARM64)
# define SUP_IOCTL_FLAG     128
#elif defined(RT_ARCH_X86) || defined(RT_ARCH_SPARC)   || defined(RT_ARCH_ARM32)
# define SUP_IOCTL_FLAG     0
#else
# error "dunno which arch this is!"
#endif

#ifdef RT_OS_WINDOWS
# ifndef CTL_CODE
#  include <iprt/win/windows.h>
# endif
  /* Automatic buffering, size not encoded. */
# define SUP_CTL_CODE_SIZE(Function, Size)      CTL_CODE(FILE_DEVICE_UNKNOWN, (Function) | SUP_IOCTL_FLAG, METHOD_BUFFERED, FILE_WRITE_ACCESS)
# define SUP_CTL_CODE_BIG(Function)             CTL_CODE(FILE_DEVICE_UNKNOWN, (Function) | SUP_IOCTL_FLAG, METHOD_BUFFERED, FILE_WRITE_ACCESS)
# define SUP_CTL_CODE_FAST(Function)            CTL_CODE(FILE_DEVICE_UNKNOWN, (Function) | SUP_IOCTL_FLAG, METHOD_NEITHER,  FILE_WRITE_ACCESS)
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           (uIOCtl)

# define SUP_NT_STATUS_BASE                     UINT32_C(0xe9860000) /**< STATUS_SEVERITY_ERROR + C-bit + facility 0x986. */
# define SUP_NT_STATUS_IS_VBOX(a_rcNt)          ( ((uint32_t)(a_rcNt) & 0xffff0000) == SUP_NT_STATUS_BASE )
# define SUP_NT_STATUS_TO_VBOX(a_rcNt)          ( (int)((uint32_t)(a_rcNt) | UINT32_C(0xffff0000)) )

/** NT device name for system access. */
# define SUPDRV_NT_DEVICE_NAME_SYS              L"\\Device\\VBoxDrv"
/** NT device name for user access. */
# define SUPDRV_NT_DEVICE_NAME_USR              L"\\Device\\VBoxDrvU"
# ifdef VBOX_WITH_HARDENING
/** NT device name for hardened stub access. */
#  define SUPDRV_NT_DEVICE_NAME_STUB            L"\\Device\\VBoxDrvStub"
/** NT device name for getting error information for failed VBoxDrv or
 * VBoxDrvStub open. */
#  define SUPDRV_NT_DEVICE_NAME_ERROR_INFO      L"\\Device\\VBoxDrvErrorInfo"
# endif


#elif defined(RT_OS_SOLARIS)
  /* No automatic buffering, size limited to 255 bytes. */
# include <sys/ioccom.h>
# define SUP_CTL_CODE_SIZE(Function, Size)      _IOWRN('V', (Function) | SUP_IOCTL_FLAG, sizeof(SUPREQHDR))
# define SUP_CTL_CODE_BIG(Function)             _IOWRN('V', (Function) | SUP_IOCTL_FLAG, sizeof(SUPREQHDR))
# define SUP_CTL_CODE_FAST(Function)            _IO(   'V', (Function) | SUP_IOCTL_FLAG)
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           ((uintptr_t)(uIOCtl))

#elif defined(RT_OS_OS2)
  /* No automatic buffering, size not encoded. */
# define SUP_CTL_CATEGORY                       0xc0
# define SUP_CTL_CODE_SIZE(Function, Size)      ((unsigned char)(Function))
# define SUP_CTL_CODE_BIG(Function)             ((unsigned char)(Function))
# define SUP_CTL_CATEGORY_FAST                  0xc1
# define SUP_CTL_CODE_FAST(Function)            ((unsigned char)(Function))
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           (uIOCtl)

#elif defined(RT_OS_LINUX)
  /* No automatic buffering, size limited to 16KB. */
# include <linux/ioctl.h>
# define SUP_CTL_CODE_SIZE(Function, Size)      _IOC(_IOC_READ | _IOC_WRITE, 'V', (Function) | SUP_IOCTL_FLAG, (Size))
# define SUP_CTL_CODE_BIG(Function)             _IO('V', (Function) | SUP_IOCTL_FLAG)
# define SUP_CTL_CODE_FAST(Function)            _IO('V', (Function) | SUP_IOCTL_FLAG)
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           ((uIOCtl) & ~IOCSIZE_MASK)

#elif defined(RT_OS_L4)
  /* Implemented in suplib, no worries. */
# define SUP_CTL_CODE_SIZE(Function, Size)      (Function)
# define SUP_CTL_CODE_BIG(Function)             (Function)
# define SUP_CTL_CODE_FAST(Function)            (Function)
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           (uIOCtl)

#else /* BSD Like */
  /* Automatic buffering, size limited to 4KB on *BSD and 8KB on Darwin - commands the limit, 4KB. */
# include <sys/ioccom.h>
# define SUP_CTL_CODE_SIZE(Function, Size)      _IOC(IOC_INOUT, 'V', (Function) | SUP_IOCTL_FLAG, (Size))
# define SUP_CTL_CODE_BIG(Function)             _IO('V', (Function) | SUP_IOCTL_FLAG)
# define SUP_CTL_CODE_FAST(Function)            _IO('V', (Function) | SUP_IOCTL_FLAG)
# define SUP_CTL_CODE_NO_SIZE(uIOCtl)           ( (uIOCtl) & ~_IOC(0,0,0,IOCPARM_MASK) )
#endif

/** @name Fast path I/O control codes.
 * @note These must run parallel to SUP_VMMR0_DO_XXX
 * @note Implementations ASSUMES up to 32 I/O controls codes in the fast range.
 * @{ */
/** Fast path IOCtl: VMMR0_DO_HM_RUN */
#define SUP_IOCTL_FAST_DO_HM_RUN                SUP_CTL_CODE_FAST(64)
/** Fast path IOCtl: VMMR0_DO_NEM_RUN */
#define SUP_IOCTL_FAST_DO_NEM_RUN               SUP_CTL_CODE_FAST(65)
/** Just a NOP call for profiling the latency of a fast ioctl call to VMMR0. */
#define SUP_IOCTL_FAST_DO_NOP                   SUP_CTL_CODE_FAST(66)
/** First fast path IOCtl number. */
#define SUP_IOCTL_FAST_DO_FIRST                 SUP_IOCTL_FAST_DO_HM_RUN
/** @} */


#ifdef RT_OS_DARWIN
/** Cookie used to fend off some unwanted clients to the IOService.  */
# define SUP_DARWIN_IOSERVICE_COOKIE            0x64726962 /* 'bird' */
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
#ifdef RT_ARCH_AMD64
# pragma pack(8)                        /* paranoia. */
#elif defined(RT_ARCH_X86)
# pragma pack(4)                        /* paranoia. */
#endif


/**
 * Common In/Out header.
 */
typedef struct SUPREQHDR
{
    /** Cookie. */
    uint32_t        u32Cookie;
    /** Session cookie. */
    uint32_t        u32SessionCookie;
    /** The size of the input. */
    uint32_t        cbIn;
    /** The size of the output. */
    uint32_t        cbOut;
    /** Flags. See SUPREQHDR_FLAGS_* for details and values. */
    uint32_t        fFlags;
    /** The VBox status code of the operation, out direction only. */
    int32_t         rc;
} SUPREQHDR;
/** Pointer to a IOC header. */
typedef SUPREQHDR *PSUPREQHDR;

/** @name SUPREQHDR::fFlags values
 * @{  */
/** Masks out the magic value.  */
#define SUPREQHDR_FLAGS_MAGIC_MASK                      UINT32_C(0xff0000ff)
/** The generic mask. */
#define SUPREQHDR_FLAGS_GEN_MASK                        UINT32_C(0x0000ff00)
/** The request specific mask. */
#define SUPREQHDR_FLAGS_REQ_MASK                        UINT32_C(0x00ff0000)

/** There is extra input that needs copying on some platforms. */
#define SUPREQHDR_FLAGS_EXTRA_IN                        UINT32_C(0x00000100)
/** There is extra output that needs copying on some platforms. */
#define SUPREQHDR_FLAGS_EXTRA_OUT                       UINT32_C(0x00000200)

/** The magic value. */
#define SUPREQHDR_FLAGS_MAGIC                           UINT32_C(0x42000042)
/** The default value. Use this when no special stuff is requested. */
#define SUPREQHDR_FLAGS_DEFAULT                         SUPREQHDR_FLAGS_MAGIC
/** @} */


/** @name SUP_IOCTL_COOKIE
 * @{
 */
/** Negotiate cookie. */
#define SUP_IOCTL_COOKIE                                SUP_CTL_CODE_SIZE(1, SUP_IOCTL_COOKIE_SIZE)
/** The request size. */
#define SUP_IOCTL_COOKIE_SIZE                           sizeof(SUPCOOKIE)
/** The SUPREQHDR::cbIn value. */
#define SUP_IOCTL_COOKIE_SIZE_IN                        sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPCOOKIE, u.In)
/** The SUPREQHDR::cbOut value. */
#define SUP_IOCTL_COOKIE_SIZE_OUT                       sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPCOOKIE, u.Out)
/** SUPCOOKIE_IN magic word. */
#define SUPCOOKIE_MAGIC                                 "The Magic Word!"
/** The initial cookie. */
#define SUPCOOKIE_INITIAL_COOKIE                        0x69726f74 /* 'tori' */

/** Current interface version.
 * The upper 16-bit is the major version, the lower the minor version.
 * When incompatible changes are made, the upper major number has to be changed.
 *
 * Update rules:
 *  -# Only update the major number when incompatible changes have been made to
 *     the IOC interface or the ABI provided via the functions returned by
 *     SUPQUERYFUNCS.
 *  -# When adding new features (new IOC number, new flags, new exports, ++)
 *     only update the minor number and change SUPLib.cpp to require the
 *     new IOC version.
 *  -# When incrementing the major number, clear the minor part and reset
 *     any IOC version requirements in SUPLib.cpp.
 *  -# When increment the major number, execute all pending work.
 *
 * @todo Pending work on next major version change:
 *          - nothing
 */
#define SUPDRV_IOC_VERSION                              0x00340001

/** SUP_IOCTL_COOKIE. */
typedef struct SUPCOOKIE
{
    /** The header.
     * u32Cookie must be set to SUPCOOKIE_INITIAL_COOKIE.
     * u32SessionCookie should be set to some random value. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Magic word. */
            char            szMagic[16];
            /** The requested interface version number. */
            uint32_t        u32ReqVersion;
            /** The minimum interface version number. */
            uint32_t        u32MinVersion;
        } In;
        struct
        {
            /** Cookie. */
            uint32_t        u32Cookie;
            /** Session cookie. */
            uint32_t        u32SessionCookie;
            /** Interface version for this session. */
            uint32_t        u32SessionVersion;
            /** The actual interface version in the driver. */
            uint32_t        u32DriverVersion;
            /** Number of functions available for the SUP_IOCTL_QUERY_FUNCS request. */
            uint32_t        cFunctions;
            /** Session handle. */
            R0PTRTYPE(PSUPDRVSESSION)   pSession;
        } Out;
    } u;
} SUPCOOKIE, *PSUPCOOKIE;
/** @} */


/** @name SUP_IOCTL_QUERY_FUNCS
 * Query SUPR0 functions.
 * @{
 */
#define SUP_IOCTL_QUERY_FUNCS(cFuncs)                   SUP_CTL_CODE_BIG(2)
#define SUP_IOCTL_QUERY_FUNCS_SIZE(cFuncs)              RT_UOFFSETOF_DYN(SUPQUERYFUNCS, u.Out.aFunctions[(cFuncs)])
#define SUP_IOCTL_QUERY_FUNCS_SIZE_IN                   sizeof(SUPREQHDR)
#define SUP_IOCTL_QUERY_FUNCS_SIZE_OUT(cFuncs)          SUP_IOCTL_QUERY_FUNCS_SIZE(cFuncs)

/** A function. */
typedef struct SUPFUNC
{
    /** Name - mangled. */
    char            szName[47];
    /** For internal checking. Ignore. */
    uint8_t         cArgs;
    /** Address. */
    RTR0PTR         pfn;
} SUPFUNC, *PSUPFUNC;

typedef struct SUPQUERYFUNCS
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Number of functions returned. */
            uint32_t        cFunctions;
            /** Array of functions. */
            SUPFUNC         aFunctions[1];
        } Out;
    } u;
} SUPQUERYFUNCS, *PSUPQUERYFUNCS;
/** @} */


/** @name SUP_IOCTL_LDR_OPEN
 * Open an image.
 * @{
 */
#define SUP_IOCTL_LDR_OPEN                              SUP_CTL_CODE_SIZE(3, SUP_IOCTL_LDR_OPEN_SIZE)
#define SUP_IOCTL_LDR_OPEN_SIZE                         sizeof(SUPLDROPEN)
#define SUP_IOCTL_LDR_OPEN_SIZE_IN                      sizeof(SUPLDROPEN)
#define SUP_IOCTL_LDR_OPEN_SIZE_OUT                     (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPLDROPEN, u.Out))
typedef struct SUPLDROPEN
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Size of the image we'll be loading (including all tables).
             * Zero if the caller does not wish to prepare loading anything, then
             * cbImageBits must be zero too ofc. */
            uint32_t        cbImageWithEverything;
            /** The size of the image bits. (Less or equal to cbImageWithTabs.)
             * Zero if the caller does not wish to prepare loading anything. */
            uint32_t        cbImageBits;
            /** Image name.
             * This is the NAME of the image, not the file name. It is used
             * to share code with other processes. (Max len is 32 chars!)  */
            char            szName[32];
            /** Image file name.
             * This can be used to load the image using a native loader. */
            char            szFilename[260];
        } In;
        struct
        {
            /** The base address of the image. */
            RTR0PTR         pvImageBase;
            /** Indicate whether or not the image requires loading. */
            bool            fNeedsLoading;
            /** Indicates that we're using the native ring-0 loader. */
            bool            fNativeLoader;
        } Out;
    } u;
} SUPLDROPEN, *PSUPLDROPEN;
/** @} */


/** @name SUP_IOCTL_LDR_LOAD
 * Upload the image bits.
 * @{
 */
#define SUP_IOCTL_LDR_LOAD                              SUP_CTL_CODE_BIG(4)
#define SUP_IOCTL_LDR_LOAD_SIZE(cbImage)                RT_MAX(RT_UOFFSETOF_DYN(SUPLDRLOAD, u.In.abImage[cbImage]), SUP_IOCTL_LDR_LOAD_SIZE_OUT)
#define SUP_IOCTL_LDR_LOAD_SIZE_IN(cbImage)             RT_UOFFSETOF_DYN(SUPLDRLOAD, u.In.abImage[cbImage])
#define SUP_IOCTL_LDR_LOAD_SIZE_OUT                     (RT_UOFFSETOF(SUPLDRLOAD, u.Out.szError) + RT_SIZEOFMEMB(SUPLDRLOAD, u.Out.szError))

/**
 * Module initialization callback function.
 * This is called once after the module has been loaded.
 *
 * @returns 0 on success.
 * @returns Appropriate error code on failure.
 * @param   hMod        Image handle for use in APIs.
 */
typedef DECLCALLBACKTYPE(int, FNR0MODULEINIT,(void *hMod));
/** Pointer to a FNR0MODULEINIT(). */
typedef R0PTRTYPE(FNR0MODULEINIT *) PFNR0MODULEINIT;

/**
 * Module termination callback function.
 * This is called once right before the module is being unloaded.
 *
 * @param   hMod        Image handle for use in APIs.
 */
typedef DECLCALLBACKTYPE(void, FNR0MODULETERM,(void *hMod));
/** Pointer to a FNR0MODULETERM(). */
typedef R0PTRTYPE(FNR0MODULETERM *) PFNR0MODULETERM;

/**
 * Symbol table entry.
 */
typedef struct SUPLDRSYM
{
    /** Offset into of the string table. */
    uint32_t        offName;
    /** Offset of the symbol relative to the image load address.
     * @remarks When used inside the SUPDrv to calculate real addresses, it
     *          must be cast to int32_t for the sake of native loader support
     *          on Solaris.  (The loader puts the and data in different
     *          memory areans, and the text one is generally higher.) */
    uint32_t        offSymbol;
} SUPLDRSYM;
/** Pointer to a symbol table entry. */
typedef SUPLDRSYM *PSUPLDRSYM;
/** Pointer to a const symbol table entry. */
typedef SUPLDRSYM const *PCSUPLDRSYM;

#define SUPLDR_PROT_READ    1   /**< Grant read access (RTMEM_PROT_READ). */
#define SUPLDR_PROT_WRITE   2   /**< Grant write access (RTMEM_PROT_WRITE). */
#define SUPLDR_PROT_EXEC    4   /**< Grant execute access (RTMEM_PROT_EXEC). */

/**
 * A segment table entry - chiefly for conveying memory protection.
 */
typedef struct SUPLDRSEG
{
    /** The RVA of the segment. */
    uint32_t        off;
    /** The size of the segment. */
    uint32_t        cb : 28;
    /** The segment protection (SUPLDR_PROT_XXX). */
    uint32_t        fProt : 3;
    /** MBZ. */
    uint32_t        fUnused;
} SUPLDRSEG;
/** Pointer to a segment table entry. */
typedef SUPLDRSEG *PSUPLDRSEG;
/** Pointer to a const segment table entry. */
typedef SUPLDRSEG const *PCSUPLDRSEG;

/**
 * SUPLDRLOAD::u::In::EP type.
 */
typedef enum SUPLDRLOADEP
{
    SUPLDRLOADEP_NOTHING = 0,
    SUPLDRLOADEP_VMMR0,
    SUPLDRLOADEP_SERVICE,
    SUPLDRLOADEP_32BIT_HACK = 0x7fffffff
} SUPLDRLOADEP;

typedef struct SUPLDRLOAD
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The address of module initialization function. Similar to _DLL_InitTerm(hmod, 0). */
            RTR0PTR pfnModuleInit;
            /** The address of module termination function. Similar to _DLL_InitTerm(hmod, 1). */
            RTR0PTR pfnModuleTerm;
            /** Special entry points. */
            union
            {
                /** SUPLDRLOADEP_VMMR0. */
                struct
                {
                    /** Address of VMMR0EntryFast function. */
                    RTR0PTR                 pvVMMR0EntryFast;
                    /** Address of VMMR0EntryEx function. */
                    RTR0PTR                 pvVMMR0EntryEx;
                } VMMR0;
                /** SUPLDRLOADEP_SERVICE. */
                struct
                {
                    /** The service request handler.
                     * (PFNR0SERVICEREQHANDLER isn't defined yet.) */
                    RTR0PTR                 pfnServiceReq;
                    /** Reserved, must be NIL. */
                    RTR0PTR                 apvReserved[3];
                } Service;
            }               EP;
            /** Address. */
            RTR0PTR         pvImageBase;
            /** Entry point type. */
            SUPLDRLOADEP    eEPType;
            /** The size of the image bits (starting at offset 0 and
             * approaching offSymbols). */
            uint32_t        cbImageBits;
            /** The offset of the symbol table (SUPLDRSYM array). */
            uint32_t        offSymbols;
            /** The number of entries in the symbol table. */
            uint32_t        cSymbols;
            /** The offset of the string table. */
            uint32_t        offStrTab;
            /** Size of the string table. */
            uint32_t        cbStrTab;
            /** Offset to the segment table (SUPLDRSEG array). */
            uint32_t        offSegments;
            /** Number of segments. */
            uint32_t        cSegments;
            /** Size of image data in achImage. */
            uint32_t        cbImageWithEverything;
            /** Flags (SUPLDRLOAD_F_XXX). */
            uint32_t        fFlags;
            /** The image data. */
            uint8_t         abImage[1];
        } In;
        struct
        {
            /** Magic value indicating whether extended error information is
             * present or not (SUPLDRLOAD_ERROR_MAGIC). */
            uint64_t        uErrorMagic;
            /** Extended error information. */
            char            szError[2048];
        } Out;
    } u;
} SUPLDRLOAD, *PSUPLDRLOAD;
/** Magic value that indicates that there is a valid error information string
 * present on SUP_IOCTL_LDR_LOAD failure.
 * @remarks The value is choosen to be an unlikely init and term address. */
#define SUPLDRLOAD_ERROR_MAGIC      UINT64_C(0xabcdefef0feddcb9)
/** The module depends on VMMR0. */
#define SUPLDRLOAD_F_DEP_VMMR0      RT_BIT_32(0)
/** Valid flag mask. */
#define SUPLDRLOAD_F_VALID_MASK     UINT32_C(0x00000001)
/** @} */


/** @name SUP_IOCTL_LDR_FREE
 * Free an image.
 * @{
 */
#define SUP_IOCTL_LDR_FREE                              SUP_CTL_CODE_SIZE(5, SUP_IOCTL_LDR_FREE_SIZE)
#define SUP_IOCTL_LDR_FREE_SIZE                         sizeof(SUPLDRFREE)
#define SUP_IOCTL_LDR_FREE_SIZE_IN                      sizeof(SUPLDRFREE)
#define SUP_IOCTL_LDR_FREE_SIZE_OUT                     sizeof(SUPREQHDR)
typedef struct SUPLDRFREE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Address. */
            RTR0PTR         pvImageBase;
        } In;
    } u;
} SUPLDRFREE, *PSUPLDRFREE;
/** @} */


/** @name SUP_IOCTL_LDR_LOCK_DOWN
 * Lock down the image loader interface.
 * @{
 */
#define SUP_IOCTL_LDR_LOCK_DOWN                         SUP_CTL_CODE_SIZE(38, SUP_IOCTL_LDR_LOCK_DOWN_SIZE)
#define SUP_IOCTL_LDR_LOCK_DOWN_SIZE                    sizeof(SUPREQHDR)
#define SUP_IOCTL_LDR_LOCK_DOWN_SIZE_IN                 sizeof(SUPREQHDR)
#define SUP_IOCTL_LDR_LOCK_DOWN_SIZE_OUT                sizeof(SUPREQHDR)
/** @} */


/** @name SUP_IOCTL_LDR_GET_SYMBOL
 * Get address of a symbol within an image.
 * @{
 */
#define SUP_IOCTL_LDR_GET_SYMBOL                        SUP_CTL_CODE_SIZE(6, SUP_IOCTL_LDR_GET_SYMBOL_SIZE)
#define SUP_IOCTL_LDR_GET_SYMBOL_SIZE                   sizeof(SUPLDRGETSYMBOL)
#define SUP_IOCTL_LDR_GET_SYMBOL_SIZE_IN                sizeof(SUPLDRGETSYMBOL)
#define SUP_IOCTL_LDR_GET_SYMBOL_SIZE_OUT               (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPLDRGETSYMBOL, u.Out))
typedef struct SUPLDRGETSYMBOL
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Address. */
            RTR0PTR         pvImageBase;
            /** The symbol name. */
            char            szSymbol[64];
        } In;
        struct
        {
            /** The symbol address. */
            RTR0PTR         pvSymbol;
        } Out;
    } u;
} SUPLDRGETSYMBOL, *PSUPLDRGETSYMBOL;
/** @} */


/** @name SUP_IOCTL_CALL_VMMR0
 * Call the R0 VMM Entry point.
 * @{
 */
#define SUP_IOCTL_CALL_VMMR0(cbReq)                     SUP_CTL_CODE_SIZE(7, SUP_IOCTL_CALL_VMMR0_SIZE(cbReq))
#define SUP_IOCTL_CALL_VMMR0_NO_SIZE()                  SUP_CTL_CODE_SIZE(7, 0)
#define SUP_IOCTL_CALL_VMMR0_SIZE(cbReq)                RT_UOFFSETOF_DYN(SUPCALLVMMR0, abReqPkt[cbReq])
#define SUP_IOCTL_CALL_VMMR0_SIZE_IN(cbReq)             SUP_IOCTL_CALL_VMMR0_SIZE(cbReq)
#define SUP_IOCTL_CALL_VMMR0_SIZE_OUT(cbReq)            SUP_IOCTL_CALL_VMMR0_SIZE(cbReq)
typedef struct SUPCALLVMMR0
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The VM handle. */
            PVMR0           pVMR0;
            /** VCPU id. */
            uint32_t        idCpu;
            /** Which operation to execute. */
            uint32_t        uOperation;
            /** Argument to use when no request packet is supplied. */
            uint64_t        u64Arg;
        } In;
    } u;
    /** The VMMR0Entry request packet. */
    uint8_t                 abReqPkt[1];
} SUPCALLVMMR0, *PSUPCALLVMMR0;
/** @} */


/** @name SUP_IOCTL_CALL_VMMR0_BIG
 * Version of SUP_IOCTL_CALL_VMMR0 for dealing with large requests.
 * @{
 */
#define SUP_IOCTL_CALL_VMMR0_BIG                        SUP_CTL_CODE_BIG(27)
#define SUP_IOCTL_CALL_VMMR0_BIG_SIZE(cbReq)            RT_UOFFSETOF_DYN(SUPCALLVMMR0, abReqPkt[cbReq])
#define SUP_IOCTL_CALL_VMMR0_BIG_SIZE_IN(cbReq)         SUP_IOCTL_CALL_VMMR0_SIZE(cbReq)
#define SUP_IOCTL_CALL_VMMR0_BIG_SIZE_OUT(cbReq)        SUP_IOCTL_CALL_VMMR0_SIZE(cbReq)
/** @} */


/** @name SUP_IOCTL_LOW_ALLOC
 * Allocate memory below 4GB (physically).
 * @{
 */
#define SUP_IOCTL_LOW_ALLOC                             SUP_CTL_CODE_BIG(8)
#define SUP_IOCTL_LOW_ALLOC_SIZE(cPages)                ((uint32_t)RT_UOFFSETOF_DYN(SUPLOWALLOC, u.Out.aPages[cPages]))
#define SUP_IOCTL_LOW_ALLOC_SIZE_IN                     (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPLOWALLOC, u.In))
#define SUP_IOCTL_LOW_ALLOC_SIZE_OUT(cPages)            SUP_IOCTL_LOW_ALLOC_SIZE(cPages)
typedef struct SUPLOWALLOC
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Number of pages to allocate. */
            uint32_t        cPages;
        } In;
        struct
        {
            /** The ring-3 address of the allocated memory. */
            RTR3PTR         pvR3;
            /** The ring-0 address of the allocated memory. */
            RTR0PTR         pvR0;
            /** Array of pages. */
            RTHCPHYS        aPages[1];
        } Out;
    } u;
} SUPLOWALLOC, *PSUPLOWALLOC;
/** @} */


/** @name SUP_IOCTL_LOW_FREE
 * Free low memory.
 * @{
 */
#define SUP_IOCTL_LOW_FREE                              SUP_CTL_CODE_SIZE(9, SUP_IOCTL_LOW_FREE_SIZE)
#define SUP_IOCTL_LOW_FREE_SIZE                         sizeof(SUPLOWFREE)
#define SUP_IOCTL_LOW_FREE_SIZE_IN                      sizeof(SUPLOWFREE)
#define SUP_IOCTL_LOW_FREE_SIZE_OUT                     sizeof(SUPREQHDR)
typedef struct SUPLOWFREE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The ring-3 address of the memory to free. */
            RTR3PTR         pvR3;
        } In;
    } u;
} SUPLOWFREE, *PSUPLOWFREE;
/** @} */


/** @name SUP_IOCTL_PAGE_ALLOC_EX
 * Allocate memory and map it into kernel and/or user space. The memory is of
 * course locked. The result should be freed using SUP_IOCTL_PAGE_FREE.
 *
 * @remarks Allocations without a kernel mapping may fail with
 *          VERR_NOT_SUPPORTED on some platforms.
 *
 * @{
 */
#define SUP_IOCTL_PAGE_ALLOC_EX                         SUP_CTL_CODE_BIG(10)
#define SUP_IOCTL_PAGE_ALLOC_EX_SIZE(cPages)            RT_UOFFSETOF_DYN(SUPPAGEALLOCEX, u.Out.aPages[cPages])
#define SUP_IOCTL_PAGE_ALLOC_EX_SIZE_IN                 (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPPAGEALLOCEX, u.In))
#define SUP_IOCTL_PAGE_ALLOC_EX_SIZE_OUT(cPages)        SUP_IOCTL_PAGE_ALLOC_EX_SIZE(cPages)
typedef struct SUPPAGEALLOCEX
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Number of pages to allocate */
            uint32_t        cPages;
            /** Whether it should have kernel mapping. */
            bool            fKernelMapping;
            /** Whether it should have a user mapping. */
            bool            fUserMapping;
            /** Reserved. Must be false. */
            bool            fReserved0;
            /** Reserved. Must be false. */
            bool            fReserved1;
        } In;
        struct
        {
            /** Returned ring-3 address. */
            RTR3PTR         pvR3;
            /** Returned ring-0 address. */
            RTR0PTR         pvR0;
            /** The physical addresses of the allocated pages. */
            RTHCPHYS        aPages[1];
        } Out;
    } u;
} SUPPAGEALLOCEX, *PSUPPAGEALLOCEX;
/** @} */


/** @name SUP_IOCTL_PAGE_MAP_KERNEL
 * Maps a portion of memory allocated by SUP_IOCTL_PAGE_ALLOC_EX /
 * SUPR0PageAllocEx into kernel space for use by a device or similar.
 *
 * The mapping will be freed together with the ring-3 mapping when
 * SUP_IOCTL_PAGE_FREE or SUPR0PageFree is called.
 *
 * @remarks Not necessarily supported on all platforms.
 *
 * @{
 */
#define SUP_IOCTL_PAGE_MAP_KERNEL                       SUP_CTL_CODE_SIZE(11, SUP_IOCTL_PAGE_MAP_KERNEL_SIZE)
#define SUP_IOCTL_PAGE_MAP_KERNEL_SIZE                  sizeof(SUPPAGEMAPKERNEL)
#define SUP_IOCTL_PAGE_MAP_KERNEL_SIZE_IN               sizeof(SUPPAGEMAPKERNEL)
#define SUP_IOCTL_PAGE_MAP_KERNEL_SIZE_OUT              sizeof(SUPPAGEMAPKERNEL)
typedef struct SUPPAGEMAPKERNEL
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The pointer of to the previously allocated memory. */
            RTR3PTR         pvR3;
            /** The offset to start mapping from. */
            uint32_t        offSub;
            /** Size of the section to map. */
            uint32_t        cbSub;
            /** Flags reserved for future fun. */
            uint32_t        fFlags;
        } In;
        struct
        {
            /** The ring-0 address corresponding to pvR3 + offSub. */
            RTR0PTR         pvR0;
        } Out;
    } u;
} SUPPAGEMAPKERNEL, *PSUPPAGEMAPKERNEL;
/** @} */


/** @name SUP_IOCTL_PAGE_PROTECT
 * Changes the page level protection of the user and/or kernel mappings of
 * memory previously allocated by SUPR0PageAllocEx.
 *
 * @remarks Not necessarily supported on all platforms.
 *
 * @{
 */
#define SUP_IOCTL_PAGE_PROTECT                          SUP_CTL_CODE_SIZE(12, SUP_IOCTL_PAGE_PROTECT_SIZE)
#define SUP_IOCTL_PAGE_PROTECT_SIZE                     sizeof(SUPPAGEPROTECT)
#define SUP_IOCTL_PAGE_PROTECT_SIZE_IN                  sizeof(SUPPAGEPROTECT)
#define SUP_IOCTL_PAGE_PROTECT_SIZE_OUT                 sizeof(SUPPAGEPROTECT)
typedef struct SUPPAGEPROTECT
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The pointer of to the previously allocated memory.
             * Pass NIL_RTR3PTR if the ring-0 mapping should remain unaffected. */
            RTR3PTR         pvR3;
            /** The pointer of to the previously allocated memory.
             * Pass NIL_RTR0PTR if the ring-0 mapping should remain unaffected. */
            RTR0PTR         pvR0;
            /** The offset to start changing protection at. */
            uint32_t        offSub;
            /** Size of the portion that should be changed. */
            uint32_t        cbSub;
            /** Protection flags, RTMEM_PROT_*. */
            uint32_t        fProt;
        } In;
    } u;
} SUPPAGEPROTECT, *PSUPPAGEPROTECT;
/** @} */


/** @name SUP_IOCTL_PAGE_FREE
 * Free memory allocated with SUP_IOCTL_PAGE_ALLOC_EX.
 * @{
 */
#define SUP_IOCTL_PAGE_FREE                             SUP_CTL_CODE_SIZE(13, SUP_IOCTL_PAGE_FREE_SIZE_IN)
#define SUP_IOCTL_PAGE_FREE_SIZE                        sizeof(SUPPAGEFREE)
#define SUP_IOCTL_PAGE_FREE_SIZE_IN                     sizeof(SUPPAGEFREE)
#define SUP_IOCTL_PAGE_FREE_SIZE_OUT                    sizeof(SUPREQHDR)
typedef struct SUPPAGEFREE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Address of memory range to free. */
            RTR3PTR         pvR3;
        } In;
    } u;
} SUPPAGEFREE, *PSUPPAGEFREE;
/** @} */




/** @name SUP_IOCTL_PAGE_LOCK
 * Pin down physical pages.
 * @{
 */
#define SUP_IOCTL_PAGE_LOCK                             SUP_CTL_CODE_BIG(14)
#define SUP_IOCTL_PAGE_LOCK_SIZE(cPages)                (RT_MAX((size_t)SUP_IOCTL_PAGE_LOCK_SIZE_IN, (size_t)SUP_IOCTL_PAGE_LOCK_SIZE_OUT(cPages)))
#define SUP_IOCTL_PAGE_LOCK_SIZE_IN                     (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPPAGELOCK, u.In))
#define SUP_IOCTL_PAGE_LOCK_SIZE_OUT(cPages)            RT_UOFFSETOF_DYN(SUPPAGELOCK, u.Out.aPages[cPages])
typedef struct SUPPAGELOCK
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Start of page range. Must be PAGE aligned. */
            RTR3PTR         pvR3;
            /** The range size given as a page count. */
            uint32_t        cPages;
        } In;

        struct
        {
            /** Array of pages. */
            RTHCPHYS        aPages[1];
        } Out;
    } u;
} SUPPAGELOCK, *PSUPPAGELOCK;
/** @} */


/** @name SUP_IOCTL_PAGE_UNLOCK
 * Unpin physical pages.
 * @{ */
#define SUP_IOCTL_PAGE_UNLOCK                           SUP_CTL_CODE_SIZE(15, SUP_IOCTL_PAGE_UNLOCK_SIZE)
#define SUP_IOCTL_PAGE_UNLOCK_SIZE                      sizeof(SUPPAGEUNLOCK)
#define SUP_IOCTL_PAGE_UNLOCK_SIZE_IN                   sizeof(SUPPAGEUNLOCK)
#define SUP_IOCTL_PAGE_UNLOCK_SIZE_OUT                  sizeof(SUPREQHDR)
typedef struct SUPPAGEUNLOCK
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Start of page range of a range previously pinned. */
            RTR3PTR         pvR3;
        } In;
    } u;
} SUPPAGEUNLOCK, *PSUPPAGEUNLOCK;
/** @} */


/** @name SUP_IOCTL_CONT_ALLOC
 * Allocate continuous memory.
 * @{
 */
#define SUP_IOCTL_CONT_ALLOC                            SUP_CTL_CODE_SIZE(16, SUP_IOCTL_CONT_ALLOC_SIZE)
#define SUP_IOCTL_CONT_ALLOC_SIZE                       sizeof(SUPCONTALLOC)
#define SUP_IOCTL_CONT_ALLOC_SIZE_IN                    (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPCONTALLOC, u.In))
#define SUP_IOCTL_CONT_ALLOC_SIZE_OUT                   sizeof(SUPCONTALLOC)
typedef struct SUPCONTALLOC
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The allocation size given as a page count. */
            uint32_t        cPages;
        } In;

        struct
        {
            /** The address of the ring-0 mapping of the allocated memory. */
            RTR0PTR         pvR0;
            /** The address of the ring-3 mapping of the allocated memory. */
            RTR3PTR         pvR3;
            /** The physical address of the allocation. */
            RTHCPHYS        HCPhys;
        } Out;
    } u;
} SUPCONTALLOC, *PSUPCONTALLOC;
/** @} */


/** @name SUP_IOCTL_CONT_FREE Input.
 * @{
 */
/** Free continuous memory. */
#define SUP_IOCTL_CONT_FREE                             SUP_CTL_CODE_SIZE(17, SUP_IOCTL_CONT_FREE_SIZE)
#define SUP_IOCTL_CONT_FREE_SIZE                        sizeof(SUPCONTFREE)
#define SUP_IOCTL_CONT_FREE_SIZE_IN                     sizeof(SUPCONTFREE)
#define SUP_IOCTL_CONT_FREE_SIZE_OUT                    sizeof(SUPREQHDR)
typedef struct SUPCONTFREE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The ring-3 address of the memory to free. */
            RTR3PTR         pvR3;
        } In;
    } u;
} SUPCONTFREE, *PSUPCONTFREE;
/** @} */


/** @name SUP_IOCTL_GET_PAGING_MODE
 * Get the host paging mode.
 * @{
 */
#define SUP_IOCTL_GET_PAGING_MODE                       SUP_CTL_CODE_SIZE(18, SUP_IOCTL_GET_PAGING_MODE_SIZE)
#define SUP_IOCTL_GET_PAGING_MODE_SIZE                  sizeof(SUPGETPAGINGMODE)
#define SUP_IOCTL_GET_PAGING_MODE_SIZE_IN               sizeof(SUPREQHDR)
#define SUP_IOCTL_GET_PAGING_MODE_SIZE_OUT              sizeof(SUPGETPAGINGMODE)
typedef struct SUPGETPAGINGMODE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The paging mode. */
            SUPPAGINGMODE   enmMode;
        } Out;
    } u;
} SUPGETPAGINGMODE, *PSUPGETPAGINGMODE;
/** @} */


/** @name SUP_IOCTL_SET_VM_FOR_FAST
 * Set the VM handle for doing fast call ioctl calls.
 * @{
 */
#define SUP_IOCTL_SET_VM_FOR_FAST                       SUP_CTL_CODE_SIZE(19, SUP_IOCTL_SET_VM_FOR_FAST_SIZE)
#define SUP_IOCTL_SET_VM_FOR_FAST_SIZE                  sizeof(SUPSETVMFORFAST)
#define SUP_IOCTL_SET_VM_FOR_FAST_SIZE_IN               sizeof(SUPSETVMFORFAST)
#define SUP_IOCTL_SET_VM_FOR_FAST_SIZE_OUT              sizeof(SUPREQHDR)
typedef struct SUPSETVMFORFAST
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The ring-0 VM handle (pointer). */
            PVMR0           pVMR0;
        } In;
    } u;
} SUPSETVMFORFAST, *PSUPSETVMFORFAST;
/** @} */


/** @name SUP_IOCTL_GIP_MAP
 * Map the GIP into user space.
 * @{
 */
#define SUP_IOCTL_GIP_MAP                               SUP_CTL_CODE_SIZE(20, SUP_IOCTL_GIP_MAP_SIZE)
#define SUP_IOCTL_GIP_MAP_SIZE                          sizeof(SUPGIPMAP)
#define SUP_IOCTL_GIP_MAP_SIZE_IN                       sizeof(SUPREQHDR)
#define SUP_IOCTL_GIP_MAP_SIZE_OUT                      sizeof(SUPGIPMAP)
typedef struct SUPGIPMAP
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The physical address of the GIP. */
            RTHCPHYS        HCPhysGip;
            /** Pointer to the read-only usermode GIP mapping for this session. */
            R3PTRTYPE(PSUPGLOBALINFOPAGE)   pGipR3;
            /** Pointer to the supervisor mode GIP mapping. */
            R0PTRTYPE(PSUPGLOBALINFOPAGE)   pGipR0;
        } Out;
    } u;
} SUPGIPMAP, *PSUPGIPMAP;
/** @} */


/** @name SUP_IOCTL_GIP_UNMAP
 * Unmap the GIP.
 * @{
 */
#define SUP_IOCTL_GIP_UNMAP                             SUP_CTL_CODE_SIZE(21, SUP_IOCTL_GIP_UNMAP_SIZE)
#define SUP_IOCTL_GIP_UNMAP_SIZE                        sizeof(SUPGIPUNMAP)
#define SUP_IOCTL_GIP_UNMAP_SIZE_IN                     sizeof(SUPGIPUNMAP)
#define SUP_IOCTL_GIP_UNMAP_SIZE_OUT                    sizeof(SUPGIPUNMAP)
typedef struct SUPGIPUNMAP
{
    /** The header. */
    SUPREQHDR               Hdr;
} SUPGIPUNMAP, *PSUPGIPUNMAP;
/** @} */


/** @name SUP_IOCTL_CALL_SERVICE
 * Call the a ring-0 service.
 *
 * @todo    Might have to convert this to a big request, just like
 *          SUP_IOCTL_CALL_VMMR0
 * @{
 */
#define SUP_IOCTL_CALL_SERVICE(cbReq)                   SUP_CTL_CODE_SIZE(22, SUP_IOCTL_CALL_SERVICE_SIZE(cbReq))
#define SUP_IOCTL_CALL_SERVICE_NO_SIZE()                SUP_CTL_CODE_SIZE(22, 0)
#define SUP_IOCTL_CALL_SERVICE_SIZE(cbReq)              RT_UOFFSETOF_DYN(SUPCALLSERVICE, abReqPkt[cbReq])
#define SUP_IOCTL_CALL_SERVICE_SIZE_IN(cbReq)           SUP_IOCTL_CALL_SERVICE_SIZE(cbReq)
#define SUP_IOCTL_CALL_SERVICE_SIZE_OUT(cbReq)          SUP_IOCTL_CALL_SERVICE_SIZE(cbReq)
typedef struct SUPCALLSERVICE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The service name. */
            char            szName[28];
            /** Which operation to execute. */
            uint32_t        uOperation;
            /** Argument to use when no request packet is supplied. */
            uint64_t        u64Arg;
        } In;
    } u;
    /** The request packet passed to SUP. */
    uint8_t                 abReqPkt[1];
} SUPCALLSERVICE, *PSUPCALLSERVICE;
/** @} */


/** @name SUP_IOCTL_LOGGER_SETTINGS
 * Changes the ring-0 release or debug logger settings.
 * @{
 */
#define SUP_IOCTL_LOGGER_SETTINGS(cbStrTab)             SUP_CTL_CODE_SIZE(23, SUP_IOCTL_LOGGER_SETTINGS_SIZE(cbStrTab))
#define SUP_IOCTL_LOGGER_SETTINGS_NO_SIZE()             SUP_CTL_CODE_SIZE(23, 0)
#define SUP_IOCTL_LOGGER_SETTINGS_SIZE(cbStrTab)        RT_UOFFSETOF_DYN(SUPLOGGERSETTINGS, u.In.szStrings[cbStrTab])
#define SUP_IOCTL_LOGGER_SETTINGS_SIZE_IN(cbStrTab)     RT_UOFFSETOF_DYN(SUPLOGGERSETTINGS, u.In.szStrings[cbStrTab])
#define SUP_IOCTL_LOGGER_SETTINGS_SIZE_OUT              sizeof(SUPREQHDR)
typedef struct SUPLOGGERSETTINGS
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Which logger. */
            uint32_t        fWhich;
            /** What to do with it. */
            uint32_t        fWhat;
            /** Offset of the flags setting string. */
            uint32_t        offFlags;
            /** Offset of the groups setting string. */
            uint32_t        offGroups;
            /** Offset of the destination setting string. */
            uint32_t        offDestination;
            /** The string table. */
            char            szStrings[1];
        } In;
    } u;
} SUPLOGGERSETTINGS, *PSUPLOGGERSETTINGS;

/** Debug logger. */
#define SUPLOGGERSETTINGS_WHICH_DEBUG       0
/** Release logger. */
#define SUPLOGGERSETTINGS_WHICH_RELEASE     1

/** Change the settings. */
#define SUPLOGGERSETTINGS_WHAT_SETTINGS     0
/** Create the logger instance. */
#define SUPLOGGERSETTINGS_WHAT_CREATE       1
/** Destroy the logger instance. */
#define SUPLOGGERSETTINGS_WHAT_DESTROY      2

/** @} */


/** @name Semaphore Types
 * @{ */
#define SUP_SEM_TYPE_EVENT                  0
#define SUP_SEM_TYPE_EVENT_MULTI            1
/** @} */


/** @name SUP_IOCTL_SEM_OP2
 * Semaphore operations.
 * @remarks This replaces the old SUP_IOCTL_SEM_OP interface.
 * @{
 */
#define SUP_IOCTL_SEM_OP2                               SUP_CTL_CODE_SIZE(24, SUP_IOCTL_SEM_OP2_SIZE)
#define SUP_IOCTL_SEM_OP2_SIZE                          sizeof(SUPSEMOP2)
#define SUP_IOCTL_SEM_OP2_SIZE_IN                       sizeof(SUPSEMOP2)
#define SUP_IOCTL_SEM_OP2_SIZE_OUT                      sizeof(SUPREQHDR)
typedef struct SUPSEMOP2
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The semaphore type. */
            uint32_t        uType;
            /** The semaphore handle. */
            uint32_t        hSem;
            /** The operation. */
            uint32_t        uOp;
            /** Reserved, must be zero. */
            uint32_t        uReserved;
            /** The number of milliseconds to wait if it's a wait operation. */
            union
            {
                /** Absolute timeout (RTTime[System]NanoTS).
                 * Used by SUPSEMOP2_WAIT_NS_ABS. */
                uint64_t    uAbsNsTimeout;
                /** Relative nanosecond timeout.
                 * Used by SUPSEMOP2_WAIT_NS_REL. */
                uint64_t    cRelNsTimeout;
                /** Relative millisecond timeout.
                 * Used by SUPSEMOP2_WAIT_MS_REL. */
                uint32_t    cRelMsTimeout;
                /** Generic 64-bit accessor.
                 * ASSUMES little endian!  */
                uint64_t    u64;
            } uArg;
        } In;
    } u;
} SUPSEMOP2, *PSUPSEMOP2;

/** Wait for a number of milliseconds. */
#define SUPSEMOP2_WAIT_MS_REL       0
/** Wait until the specified deadline is reached. */
#define SUPSEMOP2_WAIT_NS_ABS       1
/** Wait for a number of nanoseconds. */
#define SUPSEMOP2_WAIT_NS_REL       2
/** Signal the semaphore. */
#define SUPSEMOP2_SIGNAL            3
/** Reset the semaphore (only applicable to SUP_SEM_TYPE_EVENT_MULTI). */
#define SUPSEMOP2_RESET             4
/** Close the semaphore handle. */
#define SUPSEMOP2_CLOSE             5
/** @} */


/** @name SUP_IOCTL_SEM_OP3
 * Semaphore operations.
 * @{
 */
#define SUP_IOCTL_SEM_OP3                               SUP_CTL_CODE_SIZE(25, SUP_IOCTL_SEM_OP3_SIZE)
#define SUP_IOCTL_SEM_OP3_SIZE                          sizeof(SUPSEMOP3)
#define SUP_IOCTL_SEM_OP3_SIZE_IN                       sizeof(SUPSEMOP3)
#define SUP_IOCTL_SEM_OP3_SIZE_OUT                      sizeof(SUPSEMOP3)
typedef struct SUPSEMOP3
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The semaphore type. */
            uint32_t        uType;
            /** The semaphore handle. */
            uint32_t        hSem;
            /** The operation. */
            uint32_t        uOp;
            /** Reserved, must be zero. */
            uint32_t        u32Reserved;
            /** Reserved for future use. */
            uint64_t        u64Reserved;
        } In;
        union
        {
            /** The handle of the created semaphore.
             * Used by SUPSEMOP3_CREATE. */
            uint32_t        hSem;
            /** The semaphore resolution in nano seconds.
             * Used by SUPSEMOP3_GET_RESOLUTION. */
            uint32_t        cNsResolution;
            /** The 32-bit view. */
            uint32_t        u32;
            /** Reserved some space for later expansion. */
            uint64_t        u64Reserved;
        } Out;
    } u;
} SUPSEMOP3, *PSUPSEMOP3;

/** Get the wait resolution.  */
#define SUPSEMOP3_CREATE            0
/** Get the wait resolution.  */
#define SUPSEMOP3_GET_RESOLUTION    1
/** @} */


/** @name SUP_IOCTL_VT_CAPS
 * Get the VT-x/AMD-V capabilities.
 *
 * @todo Intended for main, which means we need to relax the privilege requires
 *       when accessing certain vboxdrv functions.
 *
 * @{
 */
#define SUP_IOCTL_VT_CAPS                               SUP_CTL_CODE_SIZE(26, SUP_IOCTL_VT_CAPS_SIZE)
#define SUP_IOCTL_VT_CAPS_SIZE                          sizeof(SUPVTCAPS)
#define SUP_IOCTL_VT_CAPS_SIZE_IN                       sizeof(SUPREQHDR)
#define SUP_IOCTL_VT_CAPS_SIZE_OUT                      sizeof(SUPVTCAPS)
typedef struct SUPVTCAPS
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The VT capability dword. */
            uint32_t        fCaps;
        } Out;
    } u;
} SUPVTCAPS, *PSUPVTCAPS;
/** @} */


/** @name SUP_IOCTL_TRACER_OPEN
 * Open the tracer.
 *
 * Should be matched by an SUP_IOCTL_TRACER_CLOSE call.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_OPEN                           SUP_CTL_CODE_SIZE(28, SUP_IOCTL_TRACER_OPEN_SIZE)
#define SUP_IOCTL_TRACER_OPEN_SIZE                      sizeof(SUPTRACEROPEN)
#define SUP_IOCTL_TRACER_OPEN_SIZE_IN                   sizeof(SUPTRACEROPEN)
#define SUP_IOCTL_TRACER_OPEN_SIZE_OUT                  sizeof(SUPREQHDR)
typedef struct SUPTRACEROPEN
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Tracer cookie.  Used to make sure we only open a matching tracer. */
            uint32_t        uCookie;
            /** Tracer specific argument. */
            RTHCUINTPTR     uArg;
        } In;
    } u;
} SUPTRACEROPEN, *PSUPTRACEROPEN;
/** @} */


/** @name SUP_IOCTL_TRACER_CLOSE
 * Close the tracer.
 *
 * Must match a SUP_IOCTL_TRACER_OPEN call.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_CLOSE                          SUP_CTL_CODE_SIZE(29, SUP_IOCTL_TRACER_CLOSE_SIZE)
#define SUP_IOCTL_TRACER_CLOSE_SIZE                     sizeof(SUPREQHDR)
#define SUP_IOCTL_TRACER_CLOSE_SIZE_IN                  sizeof(SUPREQHDR)
#define SUP_IOCTL_TRACER_CLOSE_SIZE_OUT                 sizeof(SUPREQHDR)
/** @} */


/** @name SUP_IOCTL_TRACER_IOCTL
 * Speak UNIX ioctl() with the tracer.
 *
 * The session must have opened the tracer prior to issuing this request.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_IOCTL                          SUP_CTL_CODE_SIZE(30, SUP_IOCTL_TRACER_IOCTL_SIZE)
#define SUP_IOCTL_TRACER_IOCTL_SIZE                     sizeof(SUPTRACERIOCTL)
#define SUP_IOCTL_TRACER_IOCTL_SIZE_IN                  sizeof(SUPTRACERIOCTL)
#define SUP_IOCTL_TRACER_IOCTL_SIZE_OUT                 (RT_UOFFSETOF(SUPTRACERIOCTL, u.Out.iRetVal) + sizeof(int32_t))
typedef struct SUPTRACERIOCTL
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The command. */
            RTHCUINTPTR     uCmd;
            /** Argument to the command. */
            RTHCUINTPTR     uArg;
        } In;

        struct
        {
            /** The return value. */
            int32_t         iRetVal;
        } Out;
    } u;
} SUPTRACERIOCTL, *PSUPTRACERIOCTL;
/** @} */


/** @name SUP_IOCTL_TRACER_UMOD_REG
 * Registers tracepoints in a user mode module.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_UMOD_REG                       SUP_CTL_CODE_SIZE(31, SUP_IOCTL_TRACER_UMOD_REG_SIZE)
#define SUP_IOCTL_TRACER_UMOD_REG_SIZE                  sizeof(SUPTRACERUMODREG)
#define SUP_IOCTL_TRACER_UMOD_REG_SIZE_IN               sizeof(SUPTRACERUMODREG)
#define SUP_IOCTL_TRACER_UMOD_REG_SIZE_OUT              sizeof(SUPREQHDR)
typedef struct SUPTRACERUMODREG
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The address at which the VTG header actually resides.
             * This will differ from R3PtrVtgHdr for raw-mode context
             * modules. */
            RTUINTPTR       uVtgHdrAddr;
            /** The ring-3 pointer of the VTG header. */
            RTR3PTR         R3PtrVtgHdr;
            /** The ring-3 pointer of the probe location string table. */
            RTR3PTR         R3PtrStrTab;
            /** The size of the string table. */
            uint32_t        cbStrTab;
            /** Future flags, MBZ.  */
            uint32_t        fFlags;
            /** The module name. */
            char            szName[64];
        } In;
    } u;
} SUPTRACERUMODREG, *PSUPTRACERUMODREG;
/** @} */


/** @name SUP_IOCTL_TRACER_UMOD_DEREG
 * Deregisters tracepoints in a user mode module.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_UMOD_DEREG                     SUP_CTL_CODE_SIZE(32, SUP_IOCTL_TRACER_UMOD_DEREG_SIZE)
#define SUP_IOCTL_TRACER_UMOD_DEREG_SIZE                sizeof(SUPTRACERUMODDEREG)
#define SUP_IOCTL_TRACER_UMOD_DEREG_SIZE_IN             sizeof(SUPTRACERUMODDEREG)
#define SUP_IOCTL_TRACER_UMOD_DEREG_SIZE_OUT            sizeof(SUPREQHDR)
typedef struct SUPTRACERUMODDEREG
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Pointer to the VTG header. */
            RTR3PTR         pVtgHdr;
        } In;
    } u;
} SUPTRACERUMODDEREG, *PSUPTRACERUMODDEREG;
/** @} */


/** @name SUP_IOCTL_TRACER_UMOD_FIRE_PROBE
 * Fire a probe in a user tracepoint module.
 *
 * @{
 */
#define SUP_IOCTL_TRACER_UMOD_FIRE_PROBE            SUP_CTL_CODE_SIZE(33, SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE)
#define SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE       sizeof(SUPTRACERUMODFIREPROBE)
#define SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE_IN    sizeof(SUPTRACERUMODFIREPROBE)
#define SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE_OUT   sizeof(SUPREQHDR)
typedef struct SUPTRACERUMODFIREPROBE
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        SUPDRVTRACERUSRCTX  In;
    } u;
} SUPTRACERUMODFIREPROBE, *PSUPTRACERUMODFIREPROBE;
/** @} */


/** @name SUP_IOCTL_MSR_PROBER
 * MSR probing interface, not available in normal builds.
 *
 * @{
 */
#define SUP_IOCTL_MSR_PROBER                        SUP_CTL_CODE_SIZE(34, SUP_IOCTL_MSR_PROBER_SIZE)
#define SUP_IOCTL_MSR_PROBER_SIZE                   sizeof(SUPMSRPROBER)
#define SUP_IOCTL_MSR_PROBER_SIZE_IN                sizeof(SUPMSRPROBER)
#define SUP_IOCTL_MSR_PROBER_SIZE_OUT               sizeof(SUPMSRPROBER)

typedef enum SUPMSRPROBEROP
{
    SUPMSRPROBEROP_INVALID = 0,                     /**< The customary invalid zero value. */
    SUPMSRPROBEROP_READ,                            /**< Read an MSR. */
    SUPMSRPROBEROP_WRITE,                           /**< Write a value to an MSR (use with care!). */
    SUPMSRPROBEROP_MODIFY,                          /**< Read-modify-restore-flushall. */
    SUPMSRPROBEROP_MODIFY_FASTER,                   /**< Read-modify-restore, skip the flushing. */
    SUPMSRPROBEROP_END,                             /**< End of valid values. */
    SUPMSRPROBEROP_32BIT_HACK = 0x7fffffff          /**< The customary 32-bit type hack. */
} SUPMSRPROBEROP;

typedef struct SUPMSRPROBER
{
    /** The header. */
    SUPREQHDR               Hdr;

    /** Input/output union. */
    union
    {
        /** Inputs.  */
        struct
        {
            /** The operation. */
            SUPMSRPROBEROP          enmOp;
            /** The MSR to test. */
            uint32_t                uMsr;
            /** The CPU to perform the operation on.
             * Use UINT32_MAX to indicate that any CPU will do. */
            uint32_t                idCpu;
            /** Alignment padding. */
            uint32_t                u32Padding;
            /** Operation specific arguments. */
            union
            {
                /* SUPMSRPROBEROP_READ takes no extra arguments. */

                /** For SUPMSRPROBEROP_WRITE. */
                struct
                {
                    /** The value to write. */
                    uint64_t        uToWrite;
                } Write;

                /** For SUPMSRPROBEROP_MODIFY and SUPMSRPROBEROP_MODIFY_FASTER. */
                struct
                {
                    /** The value to AND the current MSR value with to construct the value to
                     *  write.  This applied first. */
                    uint64_t        fAndMask;
                    /** The value to OR the result of the above mentioned AND operation with
                     * attempting to modify the MSR. */
                    uint64_t        fOrMask;
                } Modify;

                /** Reserve space for the future. */
                uint64_t        auPadding[3];
            } uArgs;
        } In;

        /** Outputs. */
        struct
        {
            /** Operation specific results. */
            union
            {
                /** For SUPMSRPROBEROP_READ. */
                struct
                {
                    /** The value we've read. */
                    uint64_t        uValue;
                    /** Set if we GPed while reading it. */
                    bool            fGp;
                } Read;

                /** For SUPMSRPROBEROP_WRITE. */
                struct
                {
                    /** Set if we GPed while writing it. */
                    bool            fGp;
                } Write;

                /** For SUPMSRPROBEROP_MODIFY and SUPMSRPROBEROP_MODIFY_FASTER. */
                SUPMSRPROBERMODIFYRESULT Modify;

                /** Size padding/aligning. */
                uint64_t        auPadding[5];
            } uResults;
        } Out;
    } u;
} SUPMSRPROBER, *PSUPMSRPROBER;
AssertCompileMemberAlignment(SUPMSRPROBER, u, 8);
AssertCompileMemberAlignment(SUPMSRPROBER, u.In.uArgs, 8);
AssertCompileMembersSameSizeAndOffset(SUPMSRPROBER, u.In, SUPMSRPROBER, u.Out);
/** @} */

/** @name SUP_IOCTL_RESUME_SUSPENDED_KBDS
 * Resume suspended keyboard devices if any found in the system.
 *
 * @{
 */
#define SUP_IOCTL_RESUME_SUSPENDED_KBDS                 SUP_CTL_CODE_SIZE(35, SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE)
#define SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE            sizeof(SUPREQHDR)
#define SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE_IN         sizeof(SUPREQHDR)
#define SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE_OUT        sizeof(SUPREQHDR)
/** @} */


/** @name SUP_IOCTL_TSC_DELTA_MEASURE
 * Measure the TSC-delta between the specified CPU and the master TSC.
 *
 * To call this I/O control, the client must first have mapped the GIP.
 *
 * @{
 */
#define SUP_IOCTL_TSC_DELTA_MEASURE                     SUP_CTL_CODE_SIZE(36, SUP_IOCTL_TSC_DELTA_MEASURE_SIZE)
#define SUP_IOCTL_TSC_DELTA_MEASURE_SIZE                sizeof(SUPTSCDELTAMEASURE)
#define SUP_IOCTL_TSC_DELTA_MEASURE_SIZE_IN             sizeof(SUPTSCDELTAMEASURE)
#define SUP_IOCTL_TSC_DELTA_MEASURE_SIZE_OUT            sizeof(SUPREQHDR)
typedef struct SUPTSCDELTAMEASURE
{
    /** The header. */
    SUPREQHDR               Hdr;

    /** Input/output union. */
    union
    {
        struct
        {
            /** Which CPU to take the TSC-delta measurement for. */
            RTCPUID         idCpu;
            /** Number of times to retry on failure (specify 0 for default). */
            uint8_t         cRetries;
            /** Number of milliseconds to wait before each retry. */
            uint8_t         cMsWaitRetry;
            /** Whether to force taking a measurement if one exists already. */
            bool            fForce;
            /** Whether to do the measurement asynchronously (if possible). */
            bool            fAsync;
        } In;
    } u;
} SUPTSCDELTAMEASURE, *PSUPTSCDELTAMEASURE;
AssertCompileMemberAlignment(SUPTSCDELTAMEASURE, u, 8);
AssertCompileSize(SUPTSCDELTAMEASURE, 6*4 + 4+1+1+1+1);
/** @} */


/** @name SUP_IOCTL_TSC_READ
 * Reads the TSC and apply TSC-delta if applicable, determining the delta if
 * necessary (i64TSCDelta = INT64_MAX).
 *
 * This latter function is the primary use case of this I/O control.  To call
 * this I/O control, the client must first have mapped the GIP.
 *
 * @{
 */
#define SUP_IOCTL_TSC_READ                              SUP_CTL_CODE_SIZE(37, SUP_IOCTL_TSC_READ_SIZE)
#define SUP_IOCTL_TSC_READ_SIZE                         sizeof(SUPTSCREAD)
#define SUP_IOCTL_TSC_READ_SIZE_IN                      sizeof(SUPREQHDR)
#define SUP_IOCTL_TSC_READ_SIZE_OUT                     sizeof(SUPTSCREAD)
typedef struct SUPTSCREAD
{
    /** The header. */
    SUPREQHDR               Hdr;

    /** Input/output union. */
    union
    {
        struct
        {
            /** The TSC after applying the relevant delta. */
            uint64_t        u64AdjustedTsc;
            /** The APIC Id of the CPU where the TSC was read. */
            uint16_t        idApic;
            /** Explicit alignment padding. */
            uint16_t        auPadding[3];
        } Out;
    } u;
} SUPTSCREAD, *PSUPTSCREAD;
AssertCompileMemberAlignment(SUPTSCREAD, u, 8);
AssertCompileSize(SUPTSCREAD, 6*4 + 2*8);
/** @} */


/** @name SUP_IOCTL_GIP_SET_FLAGS
 * Set GIP flags.
 *
 * @{
 */
#define SUP_IOCTL_GIP_SET_FLAGS                         SUP_CTL_CODE_SIZE(39, SUP_IOCTL_GIP_SET_FLAGS_SIZE)
#define SUP_IOCTL_GIP_SET_FLAGS_SIZE                    sizeof(SUPGIPSETFLAGS)
#define SUP_IOCTL_GIP_SET_FLAGS_SIZE_IN                 sizeof(SUPGIPSETFLAGS)
#define SUP_IOCTL_GIP_SET_FLAGS_SIZE_OUT                sizeof(SUPREQHDR)
typedef struct SUPGIPSETFLAGS
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The AND flags mask, see SUPGIP_FLAGS_XXX. */
            uint32_t        fAndMask;
            /** The OR flags mask, see SUPGIP_FLAGS_XXX. */
            uint32_t        fOrMask;
        } In;
    } u;
} SUPGIPSETFLAGS, *PSUPGIPSETFLAGS;
/** @} */


/** @name SUP_IOCTL_UCODE_REV
 * Get the CPU microcode revision.
 *
 * @{
 */
#define SUP_IOCTL_UCODE_REV                             SUP_CTL_CODE_SIZE(40, SUP_IOCTL_UCODE_REV_SIZE)
#define SUP_IOCTL_UCODE_REV_SIZE                        sizeof(SUPUCODEREV)
#define SUP_IOCTL_UCODE_REV_SIZE_IN                     sizeof(SUPREQHDR)
#define SUP_IOCTL_UCODE_REV_SIZE_OUT                    sizeof(SUPUCODEREV)
typedef struct SUPUCODEREV
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** The microcode revision dword. */
            uint32_t        MicrocodeRev;
        } Out;
    } u;
} SUPUCODEREV, *PSUPUCODEREV;
/** @} */


/** @name SUP_IOCTL_HWVIRT_MSRS
 * Get hardware-virtualization MSRs.
 *
 * This queries a lot more information than merely VT-x/AMD-V basic capabilities
 * provided by SUP_IOCTL_VT_CAPS.
 *
 * @{
 */
#define SUP_IOCTL_GET_HWVIRT_MSRS                       SUP_CTL_CODE_SIZE(41, SUP_IOCTL_GET_HWVIRT_MSRS_SIZE)
#define SUP_IOCTL_GET_HWVIRT_MSRS_SIZE                  sizeof(SUPGETHWVIRTMSRS)
#define SUP_IOCTL_GET_HWVIRT_MSRS_SIZE_IN               (sizeof(SUPREQHDR) + RT_SIZEOFMEMB(SUPGETHWVIRTMSRS, u.In))
#define SUP_IOCTL_GET_HWVIRT_MSRS_SIZE_OUT              sizeof(SUPGETHWVIRTMSRS)

typedef struct SUPGETHWVIRTMSRS
{
    /** The header. */
    SUPREQHDR               Hdr;
    union
    {
        struct
        {
            /** Whether to force re-querying of MSRs. */
            bool                fForce;
            /** Reserved. Must be false. */
            bool                fReserved0;
            /** Reserved. Must be false. */
            bool                fReserved1;
            /** Reserved. Must be false. */
            bool                fReserved2;
        } In;

        struct
        {
            /** Hardware-virtualization MSRs. */
            SUPHWVIRTMSRS      HwvirtMsrs;
        } Out;
    } u;
} SUPGETHWVIRTMSRS, *PSUPGETHWVIRTMSRS;
/** @} */


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# pragma pack()                         /* paranoia */
#endif

#endif /* !VBOX_INCLUDED_SRC_Support_SUPDrvIOC_h */

