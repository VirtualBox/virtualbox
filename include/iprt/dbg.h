/* $Id: dbg.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debugging Routines.
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

#ifndef IPRT_INCLUDED_dbg_h
#define IPRT_INCLUDED_dbg_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/stdarg.h>
#include <iprt/ldr.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_rt_dbg    RTDbg - Debugging Routines
 * @ingroup grp_rt
 * @{
 */


/** Debug segment index. */
typedef uint32_t            RTDBGSEGIDX;
/** Pointer to a debug segment index. */
typedef RTDBGSEGIDX        *PRTDBGSEGIDX;
/** Pointer to a const debug segment index. */
typedef RTDBGSEGIDX const  *PCRTDBGSEGIDX;
/** NIL debug segment index. */
#define NIL_RTDBGSEGIDX             UINT32_C(0xffffffff)
/** The last normal segment index. */
#define RTDBGSEGIDX_LAST            UINT32_C(0xffffffef)
/** Special segment index that indicates that the offset is a relative
 * virtual address (RVA). I.e. an offset from the start of the module. */
#define RTDBGSEGIDX_RVA             UINT32_C(0xfffffff0)
/** Special segment index that indicates that the offset is a absolute. */
#define RTDBGSEGIDX_ABS             UINT32_C(0xfffffff1)
/** The last valid special segment index. */
#define RTDBGSEGIDX_SPECIAL_LAST    RTDBGSEGIDX_ABS
/** The last valid special segment index. */
#define RTDBGSEGIDX_SPECIAL_FIRST   (RTDBGSEGIDX_LAST + 1U)



/** @name RTDBGSYMADDR_FLAGS_XXX
 * Flags used when looking up a symbol by address.
 * @{ */
/** Less or equal address. (default) */
#define RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL        UINT32_C(0)
/** Greater or equal address.  */
#define RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL     UINT32_C(1)
/** Don't consider absolute symbols in deferred modules. */
#define RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED UINT32_C(2)
/** Don't search for absolute symbols if it's expensive. */
#define RTDBGSYMADDR_FLAGS_SKIP_ABS             UINT32_C(4)
/** Mask of valid flags. */
#define RTDBGSYMADDR_FLAGS_VALID_MASK           UINT32_C(7)
/** @} */

/** @name RTDBGSYMBOLADD_F_XXX - Flags for RTDbgModSymbolAdd and RTDbgAsSymbolAdd.
 * @{ */
/** Replace existing symbol with same address. */
#define RTDBGSYMBOLADD_F_REPLACE_SAME_ADDR         UINT32_C(0x00000001)
/** Replace any existing symbols overlapping the symbol range. */
#define RTDBGSYMBOLADD_F_REPLACE_ANY               UINT32_C(0x00000002)
/** Adjust sizes on address conflict.  This applies to the symbol being added
 * as well as existing symbols. */
#define RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT  UINT32_C(0x00000004)
/** Mask of valid flags. */
#define RTDBGSYMBOLADD_F_VALID_MASK                UINT32_C(0x00000007)
/** @} */

/** Max length (including '\\0') of a segment name. */
#define RTDBG_SEGMENT_NAME_LENGTH   (128 - 8 - 8 - 8 - 4 - 4)

/**
 * Debug module segment.
 */
typedef struct RTDBGSEGMENT
{
    /** The load address.
     * RTUINTPTR_MAX if not applicable. */
    RTUINTPTR           Address;
    /** The image relative virtual address of the segment.
     * RTUINTPTR_MAX if not applicable. */
    RTUINTPTR           uRva;
    /** The segment size. */
    RTUINTPTR           cb;
    /** The segment flags. (reserved) */
    uint32_t            fFlags;
    /** The segment index. */
    RTDBGSEGIDX         iSeg;
    /** Symbol name. */
    char                szName[RTDBG_SEGMENT_NAME_LENGTH];
} RTDBGSEGMENT;
/** Pointer to a debug module segment. */
typedef RTDBGSEGMENT *PRTDBGSEGMENT;
/** Pointer to a const debug module segment. */
typedef RTDBGSEGMENT const *PCRTDBGSEGMENT;


/**
 * Return type.
 */
typedef enum RTDBGRETURNTYPE
{
    /** The usual invalid 0 value. */
    RTDBGRETURNTYPE_INVALID = 0,
    /** Near 16-bit return. */
    RTDBGRETURNTYPE_NEAR16,
    /** Near 32-bit return. */
    RTDBGRETURNTYPE_NEAR32,
    /** Near 64-bit return. */
    RTDBGRETURNTYPE_NEAR64,
    /** Far 16:16 return. */
    RTDBGRETURNTYPE_FAR16,
    /** Far 16:32 return. */
    RTDBGRETURNTYPE_FAR32,
    /** Far 16:64 return. */
    RTDBGRETURNTYPE_FAR64,
    /** 16-bit iret return (e.g. real or 286 protect mode). */
    RTDBGRETURNTYPE_IRET16,
    /** 32-bit iret return. */
    RTDBGRETURNTYPE_IRET32,
    /** 32-bit iret return. */
    RTDBGRETURNTYPE_IRET32_PRIV,
    /** 32-bit iret return to V86 mode. */
    RTDBGRETURNTYPE_IRET32_V86,
    /** @todo 64-bit iret return. */
    RTDBGRETURNTYPE_IRET64,
    /** The end of the valid return types. */
    RTDBGRETURNTYPE_END,
    /** The usual 32-bit blowup. */
    RTDBGRETURNTYPE_32BIT_HACK = 0x7fffffff
} RTDBGRETURNTYPE;

/**
 * Figures the size of the return state on the stack.
 *
 * @returns number of bytes. 0 if invalid parameter.
 * @param   enmRetType  The type of return.
 */
DECLINLINE(unsigned) RTDbgReturnTypeSize(RTDBGRETURNTYPE enmRetType)
{
    switch (enmRetType)
    {
        case RTDBGRETURNTYPE_NEAR16:         return 2;
        case RTDBGRETURNTYPE_NEAR32:         return 4;
        case RTDBGRETURNTYPE_NEAR64:         return 8;
        case RTDBGRETURNTYPE_FAR16:          return 4;
        case RTDBGRETURNTYPE_FAR32:          return 4;
        case RTDBGRETURNTYPE_FAR64:          return 8;
        case RTDBGRETURNTYPE_IRET16:         return 6;
        case RTDBGRETURNTYPE_IRET32:         return 4*3;
        case RTDBGRETURNTYPE_IRET32_PRIV:    return 4*5;
        case RTDBGRETURNTYPE_IRET32_V86:     return 4*9;
        case RTDBGRETURNTYPE_IRET64:         return 5*8;

        case RTDBGRETURNTYPE_INVALID:
        case RTDBGRETURNTYPE_END:
        case RTDBGRETURNTYPE_32BIT_HACK:
            break;
    }
    return 0;
}

/**
 * Check if near return.
 *
 * @returns true if near, false if far or iret.
 * @param   enmRetType  The type of return.
 */
DECLINLINE(bool) RTDbgReturnTypeIsNear(RTDBGRETURNTYPE enmRetType)
{
    return enmRetType == RTDBGRETURNTYPE_NEAR32
        || enmRetType == RTDBGRETURNTYPE_NEAR64
        || enmRetType == RTDBGRETURNTYPE_NEAR16;
}



/** Magic value for RTDBGUNWINDSTATE::u32Magic (James Moody). */
#define RTDBGUNWINDSTATE_MAGIC          UINT32_C(0x19250326)
/** Magic value for RTDBGUNWINDSTATE::u32Magic after use. */
#define RTDBGUNWINDSTATE_MAGIC_DEAD     UINT32_C(0x20101209)

/**
 * Unwind machine state.
 */
typedef struct RTDBGUNWINDSTATE
{
    /** Structure magic (RTDBGUNWINDSTATE_MAGIC) */
    uint32_t            u32Magic;
    /** The state architecture. */
    RTLDRARCH           enmArch;

    /** The program counter register.
     * amd64/x86: RIP/EIP/IP
     * sparc: PC
     * arm32: PC / R15
     */
    uint64_t            uPc;

    /** Return type. */
    RTDBGRETURNTYPE     enmRetType;

    /** Register state (see enmArch). */
    union
    {
        /** RTLDRARCH_AMD64, RTLDRARCH_X86_32 and RTLDRARCH_X86_16. */
        struct
        {
            /** General purpose registers indexed by X86_GREG_XXX. */
            uint64_t    auRegs[16];
            /** The frame address. */
            RTFAR64     FrameAddr;
            /** Set if we're in real or virtual 8086 mode. */
            bool        fRealOrV86;
            /** The flags register. */
            uint64_t    uRFlags;
            /** Trap error code. */
            uint64_t    uErrCd;
            /** Segment registers (indexed by X86_SREG_XXX). */
            uint16_t    auSegs[6];

            /** Bitmap tracking register we've loaded and which content can possibly be trusted. */
            union
            {
                /** For effective clearing of the bits. */
                uint32_t    fAll;
                /** Detailed view. */
                struct
                {
                    /** Bitmap indicating whether a GPR was loaded (parallel to auRegs). */
                    uint16_t    fRegs;
                    /** Bitmap indicating whether a segment register was loaded (parallel to auSegs). */
                    uint8_t     fSegs;
                    /** Set if uPc was loaded. */
                    RT_GCC_EXTENSION uint8_t     fPc : 1;
                    /** Set if FrameAddr was loaded. */
                    RT_GCC_EXTENSION uint8_t     fFrameAddr : 1;
                    /** Set if uRFlags was loaded. */
                    RT_GCC_EXTENSION uint8_t     fRFlags : 1;
                    /** Set if uErrCd was loaded. */
                    RT_GCC_EXTENSION uint8_t     fErrCd : 1;
                } s;
            } Loaded;
        } x86;

        /** @todo add ARM and others as needed. */
    } u;

    /**
     * Stack read callback.
     *
     * @returns IPRT status code.
     * @param   pThis       Pointer to this structure.
     * @param   uSp         The stack pointer address.
     * @param   cbToRead    The number of bytes to read.
     * @param   pvDst       Where to put the bytes we read.
     */
    DECLCALLBACKMEMBER(int, pfnReadStack,(struct RTDBGUNWINDSTATE *pThis, RTUINTPTR uSp, size_t cbToRead, void *pvDst));
    /** User argument (useful for pfnReadStack). */
    void               *pvUser;

} RTDBGUNWINDSTATE;

/**
 * Try read a 16-bit value off the stack.
 *
 * @returns pfnReadStack result.
 * @param   pThis           The unwind state.
 * @param   uSrcAddr        The stack address.
 * @param   puDst           The read destination.
 */
DECLINLINE(int) RTDbgUnwindLoadStackU16(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSrcAddr, uint16_t *puDst)
{
    return pThis->pfnReadStack(pThis, uSrcAddr, sizeof(*puDst), puDst);
}

/**
 * Try read a 32-bit value off the stack.
 *
 * @returns pfnReadStack result.
 * @param   pThis           The unwind state.
 * @param   uSrcAddr        The stack address.
 * @param   puDst           The read destination.
 */
DECLINLINE(int) RTDbgUnwindLoadStackU32(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSrcAddr, uint32_t *puDst)
{
    return pThis->pfnReadStack(pThis, uSrcAddr, sizeof(*puDst), puDst);
}

/**
 * Try read a 64-bit value off the stack.
 *
 * @returns pfnReadStack result.
 * @param   pThis           The unwind state.
 * @param   uSrcAddr        The stack address.
 * @param   puDst           The read destination.
 */
DECLINLINE(int) RTDbgUnwindLoadStackU64(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSrcAddr, uint64_t *puDst)
{
    return pThis->pfnReadStack(pThis, uSrcAddr, sizeof(*puDst), puDst);
}



/** Max length (including '\\0') of a symbol name. */
#define RTDBG_SYMBOL_NAME_LENGTH    (512 - 8 - 8 - 8 - 4 - 4 - 8)

/**
 * Debug symbol.
 */
typedef struct RTDBGSYMBOL
{
    /** Symbol value (address).
     * This depends a bit who you ask. It will be the same as offSeg when you
     * as RTDbgMod, but the mapping address if you ask RTDbgAs. */
    RTUINTPTR           Value;
    /** Symbol size. */
    RTUINTPTR           cb;
    /** Offset into the segment specified by iSeg. */
    RTUINTPTR           offSeg;
    /** Segment number. */
    RTDBGSEGIDX         iSeg;
    /** Symbol Flags. (reserved). */
    uint32_t            fFlags;
    /** Symbol ordinal.
     * This is set to UINT32_MAX if the ordinals aren't supported. */
    uint32_t            iOrdinal;
    /** Symbol name. */
    char                szName[RTDBG_SYMBOL_NAME_LENGTH];
} RTDBGSYMBOL;
/** Pointer to debug symbol. */
typedef RTDBGSYMBOL *PRTDBGSYMBOL;
/** Pointer to const debug symbol. */
typedef const RTDBGSYMBOL *PCRTDBGSYMBOL;


/**
 * Allocate a new symbol structure.
 *
 * @returns Pointer to a new structure on success, NULL on failure.
 */
RTDECL(PRTDBGSYMBOL)    RTDbgSymbolAlloc(void);

/**
 * Duplicates a symbol structure.
 *
 * @returns Pointer to duplicate on success, NULL on failure.
 *
 * @param   pSymInfo        The symbol info to duplicate.
 */
RTDECL(PRTDBGSYMBOL)    RTDbgSymbolDup(PCRTDBGSYMBOL pSymInfo);

/**
 * Free a symbol structure previously allocated by a RTDbg method.
 *
 * @param   pSymInfo        The symbol info to free. NULL is ignored.
 */
RTDECL(void)            RTDbgSymbolFree(PRTDBGSYMBOL pSymInfo);


/** Max length (including '\\0') of a debug info file name. */
#define RTDBG_FILE_NAME_LENGTH      (260)


/**
 * Debug line number information.
 */
typedef struct RTDBGLINE
{
    /** Address.
     * This depends a bit who you ask. It will be the same as offSeg when you
     * as RTDbgMod, but the mapping address if you ask RTDbgAs. */
    RTUINTPTR           Address;
    /** Offset into the segment specified by iSeg. */
    RTUINTPTR           offSeg;
    /** Segment number. */
    RTDBGSEGIDX         iSeg;
    /** Line number. */
    uint32_t            uLineNo;
    /** Symbol ordinal.
     * This is set to UINT32_MAX if the ordinals aren't supported. */
    uint32_t            iOrdinal;
    /** Filename. */
    char                szFilename[RTDBG_FILE_NAME_LENGTH];
} RTDBGLINE;
/** Pointer to debug line number. */
typedef RTDBGLINE *PRTDBGLINE;
/** Pointer to const debug line number. */
typedef const RTDBGLINE *PCRTDBGLINE;

/**
 * Allocate a new line number structure.
 *
 * @returns Pointer to a new structure on success, NULL on failure.
 */
RTDECL(PRTDBGLINE)      RTDbgLineAlloc(void);

/**
 * Duplicates a line number structure.
 *
 * @returns Pointer to duplicate on success, NULL on failure.
 *
 * @param   pLine           The line number to duplicate.
 */
RTDECL(PRTDBGLINE)      RTDbgLineDup(PCRTDBGLINE pLine);

/**
 * Free a line number structure previously allocated by a RTDbg method.
 *
 * @param   pLine           The line number to free. NULL is ignored.
 */
RTDECL(void)            RTDbgLineFree(PRTDBGLINE pLine);


/**
 * Dump the stack of the current thread into @a pszStack.
 *
 * This could be a little slow as it reads image and debug info again for each call.
 *
 * @returns Length of string returned in @a pszStack.
 * @param   pszStack        The output buffer.
 * @param   cbStack         The size of the output buffer.
 * @param   fFlags          Future flags, MBZ.
 *
 * @remarks Not present on all systems and contexts.
 */
RTDECL(size_t)          RTDbgStackDumpSelf(char *pszStack, size_t cbStack, uint32_t fFlags);


# ifdef IN_RING3

/** @defgroup grp_rt_dbgcfg     RTDbgCfg - Debugging Configuration
 *
 * The settings used when loading and processing debug info is kept in a
 * RTDBGCFG instance since it's generally shared for a whole debugging session
 * and anyhow would be a major pain to pass as individual parameters to each
 * call.  The debugging config API not only keeps the settings information but
 * also provide APIs for making use of it, and in some cases, like for instance
 * symbol severs, retriving and maintaining it.
 *
 * @todo Work in progress - APIs are still missing, adding when needed.
 *
 * @{
 */

/** Debugging configuration handle.  */
typedef struct RTDBGCFGINT *RTDBGCFG;
/** Pointer to a debugging configuration handle. */
typedef RTDBGCFG           *PRTDBGCFG;
/** NIL debug configuration handle. */
#define NIL_RTDBGCFG        ((RTDBGCFG)0)

/** @name RTDBGCFG_FLAGS_XXX - Debugging configuration flags.
 * @{ */
/** Use deferred loading. */
#define RTDBGCFG_FLAGS_DEFERRED                     RT_BIT_64(0)
/** Don't use the symbol server (http). */
#define RTDBGCFG_FLAGS_NO_SYM_SRV                   RT_BIT_64(1)
/** Don't use system search paths.
 * On windows this means not using _NT_ALT_SYMBOL_PATH, _NT_SYMBOL_PATH,
 * _NT_SOURCE_PATH, and _NT_EXECUTABLE_PATH.
 * On other systems the effect has yet to be determined. */
#define RTDBGCFG_FLAGS_NO_SYSTEM_PATHS              RT_BIT_64(2)
/** Don't search the debug and image paths recursively. */
#define RTDBGCFG_FLAGS_NO_RECURSIV_SEARCH           RT_BIT_64(3)
/** Don't search the source paths recursively. */
#define RTDBGCFG_FLAGS_NO_RECURSIV_SRC_SEARCH       RT_BIT_64(4)
/** @} */

/**
 * Debugging configuration properties.
 *
 * The search paths are using the DOS convention of semicolon as separator
 * character.  The the special 'srv' + asterisk syntax known from the windows
 * debugger search paths are also supported to some extent, as is 'cache' +
 * asterisk.
 */
typedef enum RTDBGCFGPROP
{
    /** The customary invalid 0 value. */
    RTDBGCFGPROP_INVALID = 0,
    /** RTDBGCFG_FLAGS_XXX.
     * Env: _FLAGS
     * The environment variable can be specified as a unsigned value or one or more
     * mnemonics separated by spaces. */
    RTDBGCFGPROP_FLAGS,
    /** List of paths to search for symbol files and images.
     * Env: _PATH  */
    RTDBGCFGPROP_PATH,
    /** List of symbol file suffixes (semicolon separated).
     * Env: _SUFFIXES  */
    RTDBGCFGPROP_SUFFIXES,
    /** List of paths to search for source files.
     * Env: _SRC_PATH   */
    RTDBGCFGPROP_SRC_PATH,
    /** End of valid values. */
    RTDBGCFGPROP_END,
    /** The customary 32-bit type hack. */
    RTDBGCFGPROP_32BIT_HACK = 0x7fffffff
} RTDBGCFGPROP;

/**
 * Configuration property change operation.
 */
typedef enum RTDBGCFGOP
{
    /** Customary invalid 0 value. */
    RTDBGCFGOP_INVALID = 0,
    /** Replace the current value with the given one. */
    RTDBGCFGOP_SET,
    /** Append the given value to the existing one.  For integer values this is
     *  considered a bitwise OR operation.  */
    RTDBGCFGOP_APPEND,
    /** Prepend the given value to the existing one.  For integer values this is
     *  considered a bitwise OR operation.  */
    RTDBGCFGOP_PREPEND,
    /** Removes the value from the existing one.  For interger values the value is
     * complemented and ANDed with the existing one, clearing all the specified
     * flags/bits. */
    RTDBGCFGOP_REMOVE,
    /** End of valid values. */
    RTDBGCFGOP_END,
    /** Customary 32-bit type hack. */
    RTDBGCFGOP_32BIT_HACK = 0x7fffffff
} RTDBGCFGOP;



/**
 * Initializes a debugging configuration.
 *
 * @returns IPRT status code.
 * @param   phDbgCfg            Where to return the configuration handle.
 * @param   pszEnvVarPrefix     The environment variable prefix.  If NULL, the
 *                              environment is not consulted.
 * @param   fNativePaths        Whether to pick up native paths from the
 *                              environment.
 *
 * @sa  RTDbgCfgChangeString, RTDbgCfgChangeUInt.
 */
RTDECL(int) RTDbgCfgCreate(PRTDBGCFG phDbgCfg, const char *pszEnvVarPrefix, bool fNativePaths);

/**
 * Retains a new reference to a debugging config.
 *
 * @returns New reference count.
 *          UINT32_MAX is returned if the handle is invalid (asserted).
 * @param   hDbgCfg             The config handle.
 */
RTDECL(uint32_t) RTDbgCfgRetain(RTDBGCFG hDbgCfg);

/**
 * Releases a references to a debugging config.
 *
 * @returns New reference count, if 0 the config was freed.  UINT32_MAX is
 *          returned if the handle is invalid (asserted).
 * @param   hDbgCfg             The config handle.
 */
RTDECL(uint32_t) RTDbgCfgRelease(RTDBGCFG hDbgCfg);

/**
 * Changes a property value by string.
 *
 * For string values the string is used more or less as given.  For integer
 * values and flags, it can contains both values (ORed together) or property
 * specific mnemonics (ORed / ~ANDed).
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_CFG_INVALID_VALUE
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   enmProp             The property to change.
 * @param   enmOp               How to change the property.
 * @param   pszValue            The property value to apply.
 */
RTDECL(int) RTDbgCfgChangeString(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, RTDBGCFGOP enmOp, const char *pszValue);

/**
 * Changes a property value by unsigned integer (64-bit).
 *
 * This can only be applied to integer and flag properties.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_CFG_NOT_UINT_PROP
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   enmProp             The property to change.
 * @param   enmOp               How to change the property.
 * @param   uValue              The property value to apply.
 */
RTDECL(int) RTDbgCfgChangeUInt(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, RTDBGCFGOP enmOp, uint64_t uValue);

/**
 * Query a property value as string.
 *
 * Integer and flags properties are returned as a list of mnemonics if possible,
 * otherwise as simple hex values.
 *
 * @returns IPRT status code.
 * @retval  VERR_BUFFER_OVERFLOW if there isn't sufficient buffer space. Nothing
 *          is written.
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   enmProp             The property to change.
 * @param   pszValue            The output buffer.
 * @param   cbValue             The size of the output buffer.
 */
RTDECL(int) RTDbgCfgQueryString(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, char *pszValue, size_t cbValue);

/**
 * Query a property value as unsigned integer (64-bit).
 *
 * Only integer and flags properties can be queried this way.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_CFG_NOT_UINT_PROP
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   enmProp             The property to change.
 * @param   puValue             Where to return the value.
 */
RTDECL(int) RTDbgCfgQueryUInt(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, uint64_t *puValue);

/**
 * Log callback.
 *
 * @param   hDbgCfg         The debug config instance.
 * @param   iLevel          The message level.
 * @param   pszMsg          The message.
 * @param   pvUser          User argument.
 */
typedef DECLCALLBACKTYPE(void, FNRTDBGCFGLOG,(RTDBGCFG hDbgCfg, uint32_t iLevel, const char *pszMsg, void *pvUser));
/** Pointer to a log callback. */
typedef FNRTDBGCFGLOG *PFNRTDBGCFGLOG;

/**
 * Sets the log callback for the configuration.
 *
 * This will fail if there is already a log callback present, unless pfnCallback
 * is NULL.
 *
 * @returns IPRT status code.
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   pfnCallback         The callback function.  NULL to unset.
 * @param   pvUser              The user argument.
 */
RTDECL(int) RTDbgCfgSetLogCallback(RTDBGCFG hDbgCfg, PFNRTDBGCFGLOG pfnCallback, void *pvUser);

/**
 * Callback used by the RTDbgCfgOpen function to try out a file that was found.
 *
 * @returns On statuses other than VINF_CALLBACK_RETURN and
 *          VERR_CALLBACK_RETURN the search will continue till the end of the
 *          list.  These status codes will not necessarily be propagated to the
 *          caller in any consistent manner.
 * @retval  VINF_CALLBACK_RETURN if successfully opened the file and it's time
 *          to return
 * @retval  VERR_CALLBACK_RETURN if we should stop searching immediately.
 *
 * @param   hDbgCfg             The debugging configuration handle.
 * @param   pszFilename         The path to the file that should be tried out.
 * @param   pvUser1             First user parameter.
 * @param   pvUser2             Second user parameter.
 */
typedef DECLCALLBACKTYPE(int, FNRTDBGCFGOPEN,(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2));
/** Pointer to a open-file callback used to the RTDbgCfgOpen functions. */
typedef FNRTDBGCFGOPEN *PFNRTDBGCFGOPEN;


RTDECL(int) RTDbgCfgOpenEx(RTDBGCFG hDbgCfg, const char *pszFilename, const char *pszCacheSubDir,
                           const char *pszUuidMappingSubDir, uint32_t fFlags,
                           PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenPeImage(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp,
                                PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenPdb70(RTDBGCFG hDbgCfg, const char *pszFilename, PCRTUUID pUuid, uint32_t uAge,
                              PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenPdb20(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp, uint32_t uAge,
                              PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenDbg(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp,
                            PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenDwo(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t uCrc32,
                            PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenDwoBuildId(RTDBGCFG hDbgCfg, const char *pszFilename, const uint8_t *pbBuildId,
                                   size_t cbBuildId, PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenDsymBundle(RTDBGCFG hDbgCfg, const char *pszFilename, PCRTUUID pUuid,
                                   PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);
RTDECL(int) RTDbgCfgOpenMachOImage(RTDBGCFG hDbgCfg, const char *pszFilename, PCRTUUID pUuid,
                                   PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2);

/** @name RTDBGCFG_O_XXX - Open flags for RTDbgCfgOpen.
 * @{ */
/** The operative system mask.  The values are RT_OPSYS_XXX. */
#define RTDBGCFG_O_OPSYS_MASK           UINT32_C(0x000000ff)
/** Use debuginfod style symbol servers when encountered in the path. */
#define RTDBGCFG_O_DEBUGINFOD           RT_BIT_32(24)
/** Same as RTDBGCFG_FLAGS_NO_SYSTEM_PATHS. */
#define RTDBGCFG_O_NO_SYSTEM_PATHS      RT_BIT_32(25)
/** The files may be compressed MS styled. */
#define RTDBGCFG_O_MAYBE_COMPRESSED_MS  RT_BIT_32(26)
/** Whether to make a recursive search. */
#define RTDBGCFG_O_RECURSIVE            RT_BIT_32(27)
/** We're looking for a separate debug file. */
#define RTDBGCFG_O_EXT_DEBUG_FILE       RT_BIT_32(28)
/** We're looking for an executable image. */
#define RTDBGCFG_O_EXECUTABLE_IMAGE     RT_BIT_32(29)
/** The file search should be done in an case insensitive fashion. */
#define RTDBGCFG_O_CASE_INSENSITIVE     RT_BIT_32(30)
/** Use Windbg style symbol servers when encountered in the path. */
#define RTDBGCFG_O_SYMSRV               RT_BIT_32(31)
/** Mask of valid flags. */
#define RTDBGCFG_O_VALID_MASK           UINT32_C(0xff0000ff)
/** @} */


/** @name Static symbol cache configuration
 * @{ */
/** The cache subdirectory containing the UUID mappings for .dSYM bundles.
 * The UUID mappings implemented by IPRT are splitting the image/dsym UUID up
 * into five 4 digit parts that maps to directories and one twelve digit part
 * that maps to a symbolic link.  The symlink points to the file in the
 * Contents/Resources/DWARF/ directory of the .dSYM bundle for a .dSYM map, and
 * to the image file (Contents/MacOS/bundlename for bundles) for image map.
 *
 * According to available documentation, both lldb and gdb are able to use these
 * UUID maps to find debug info while debugging.  See:
 *      http://lldb.llvm.org/symbols.html
 */
#define RTDBG_CACHE_UUID_MAP_DIR_DSYMS   "dsym-uuids"
/** The cache subdirectory containing the UUID mappings for image files. */
#define RTDBG_CACHE_UUID_MAP_DIR_IMAGES  "image-uuids"
/** Suffix used for the cached .dSYM debug files.
 * In .dSYM bundles only the .dSYM/Contents/Resources/DWARF/debug-file is
 * copied into the cache, and in order to not clash with the stripped/rich image
 * file, the cache tool slaps this suffix onto the name. */
#define RTDBG_CACHE_DSYM_FILE_SUFFIX     ".dwarf"
/** @} */

# endif /* IN_RING3 */

/** @} */


/** @defgroup grp_rt_dbgas      RTDbgAs - Debug Address Space
 * @{
 */

/**
 * Creates an empty address space.
 *
 * @returns IPRT status code.
 *
 * @param   phDbgAs         Where to store the address space handle on success.
 * @param   FirstAddr       The first address in the address space.
 * @param   LastAddr        The last address in the address space.
 * @param   pszName         The name of the address space.
 */
RTDECL(int) RTDbgAsCreate(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr, const char *pszName);

/**
 * Variant of RTDbgAsCreate that takes a name format string.
 *
 * @returns IPRT status code.
 *
 * @param   phDbgAs         Where to store the address space handle on success.
 * @param   FirstAddr       The first address in the address space.
 * @param   LastAddr        The last address in the address space.
 * @param   pszNameFmt      The name format of the address space.
 * @param   va              Format arguments.
 */
RTDECL(int) RTDbgAsCreateV(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr,
                           const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR(4, 0);

/**
 * Variant of RTDbgAsCreate that takes a name format string.
 *
 * @returns IPRT status code.
 *
 * @param   phDbgAs         Where to store the address space handle on success.
 * @param   FirstAddr       The first address in the address space.
 * @param   LastAddr        The last address in the address space.
 * @param   pszNameFmt      The name format of the address space.
 * @param   ...             Format arguments.
 */
RTDECL(int) RTDbgAsCreateF(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr,
                           const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(4, 5);

/**
 * Retains a reference to the address space.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 *
 * @param   hDbgAs          The address space handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(uint32_t) RTDbgAsRetain(RTDBGAS hDbgAs);

/**
 * Release a reference to the address space.
 *
 * When the reference count reaches zero, the address space is destroyed.
 * That means unlinking all the modules it currently contains, potentially
 * causing some or all of them to be destroyed as they are managed by
 * reference counting.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 *
 * @param   hDbgAs          The address space handle. The NIL handle is quietly
 *                          ignored and 0 is returned.
 *
 * @remarks Will not take any locks.
 */
RTDECL(uint32_t) RTDbgAsRelease(RTDBGAS hDbgAs);

/**
 * Locks the address space for exclusive access.
 *
 * @returns IRPT status code
 * @param   hDbgAs          The address space handle.
 */
RTDECL(int) RTDbgAsLockExcl(RTDBGAS hDbgAs);

/**
 * Counters the actions of one RTDbgAsUnlockExcl call.
 *
 * @returns IRPT status code
 * @param   hDbgAs          The address space handle.
 */
RTDECL(int) RTDbgAsUnlockExcl(RTDBGAS hDbgAs);

/**
 * Gets the name of an address space.
 *
 * @returns read only address space name.
 *          NULL if hDbgAs is invalid.
 *
 * @param   hDbgAs          The address space handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(const char *) RTDbgAsName(RTDBGAS hDbgAs);

/**
 * Gets the first address in an address space.
 *
 * @returns The address.
 *          0 if hDbgAs is invalid.
 *
 * @param   hDbgAs          The address space handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(RTUINTPTR) RTDbgAsFirstAddr(RTDBGAS hDbgAs);

/**
 * Gets the last address in an address space.
 *
 * @returns The address.
 *          0 if hDbgAs is invalid.
 *
 * @param   hDbgAs          The address space handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(RTUINTPTR) RTDbgAsLastAddr(RTDBGAS hDbgAs);

/**
 * Gets the number of modules in the address space.
 *
 * This can be used together with RTDbgAsModuleByIndex
 * to enumerate the modules.
 *
 * @returns The number of modules.
 *
 * @param   hDbgAs          The address space handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(uint32_t) RTDbgAsModuleCount(RTDBGAS hDbgAs);

/** @name Flags for RTDbgAsModuleLink and RTDbgAsModuleLinkSeg
 * @{ */
/** Replace all conflicting module.
 * (The conflicting modules will be removed the address space and their
 * references released.) */
#define RTDBGASLINK_FLAGS_REPLACE       RT_BIT_32(0)
/** Mask containing the valid flags. */
#define RTDBGASLINK_FLAGS_VALID_MASK    UINT32_C(0x00000001)
/** @} */

/**
 * Links a module into the address space at the give address.
 *
 * The size of the mapping is determined using RTDbgModImageSize().
 *
 * @returns IPRT status code.
 * @retval  VERR_OUT_OF_RANGE if the specified address will put the module
 *          outside the address space.
 * @retval  VERR_ADDRESS_CONFLICT if the mapping clashes with existing mappings.
 *
 * @param   hDbgAs          The address space handle.
 * @param   hDbgMod         The module handle of the module to be linked in.
 * @param   ImageAddr       The address to link the module at.
 * @param   fFlags          See RTDBGASLINK_FLAGS_*.
 */
RTDECL(int) RTDbgAsModuleLink(RTDBGAS hDbgAs, RTDBGMOD hDbgMod, RTUINTPTR ImageAddr, uint32_t fFlags);

/**
 * Links a segment into the address space at the give address.
 *
 * The size of the mapping is determined using RTDbgModSegmentSize().
 *
 * @returns IPRT status code.
 * @retval  VERR_OUT_OF_RANGE if the specified address will put the module
 *          outside the address space.
 * @retval  VERR_ADDRESS_CONFLICT if the mapping clashes with existing mappings.
 *
 * @param   hDbgAs          The address space handle.
 * @param   hDbgMod         The module handle.
 * @param   iSeg            The segment number (0-based) of the segment to be
 *                          linked in.
 * @param   SegAddr         The address to link the segment at.
 * @param   fFlags          See RTDBGASLINK_FLAGS_*.
 */
RTDECL(int) RTDbgAsModuleLinkSeg(RTDBGAS hDbgAs, RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR SegAddr, uint32_t fFlags);

/**
 * Unlinks all the mappings of a module from the address space.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if the module wasn't found.
 *
 * @param   hDbgAs          The address space handle.
 * @param   hDbgMod         The module handle of the module to be unlinked.
 */
RTDECL(int) RTDbgAsModuleUnlink(RTDBGAS hDbgAs, RTDBGMOD hDbgMod);

/**
 * Unlinks the mapping at the specified address.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if no module or segment is mapped at that address.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            The address within the mapping to be unlinked.
 */
RTDECL(int) RTDbgAsModuleUnlinkByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr);

/**
 * Get a the handle of a module in the address space by is index.
 *
 * @returns A retained handle to the specified module. The caller must release
 *          the returned reference.
 *          NIL_RTDBGMOD if invalid index or handle.
 *
 * @param   hDbgAs          The address space handle.
 * @param   iModule         The index of the module to get.
 *
 * @remarks The module indexes may change after calls to RTDbgAsModuleLink,
 *          RTDbgAsModuleLinkSeg, RTDbgAsModuleUnlink and
 *          RTDbgAsModuleUnlinkByAddr.
 */
RTDECL(RTDBGMOD) RTDbgAsModuleByIndex(RTDBGAS hDbgAs, uint32_t iModule);

/**
 * Queries mapping module information by handle.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if no mapping was found at the specified address.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            Address within the mapping of the module or segment.
 * @param   phMod           Where to the return the retained module handle.
 *                          Optional.
 * @param   pAddr           Where to return the base address of the mapping.
 *                          Optional.
 * @param   piSeg           Where to return the segment index. This is set to
 *                          NIL if the entire module is mapped as a single
 *                          mapping. Optional.
 */
RTDECL(int) RTDbgAsModuleByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTDBGMOD phMod, PRTUINTPTR pAddr, PRTDBGSEGIDX piSeg);

/**
 * Queries mapping module information by name.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if no mapping was found at the specified address.
 * @retval  VERR_OUT_OF_RANGE if the name index was out of range.
 *
 * @param   hDbgAs          The address space handle.
 * @param   pszName         The module name.
 * @param   iName           There can be more than one module by the same name
 *                          in an address space. This argument indicates which
 *                          is meant. (0 based)
 * @param   phMod           Where to the return the retained module handle.
 */
RTDECL(int) RTDbgAsModuleByName(RTDBGAS hDbgAs, const char *pszName, uint32_t iName, PRTDBGMOD phMod);

/**
 * Information about a mapping.
 *
 * This is used by RTDbgAsModuleGetMapByIndex.
 */
typedef struct RTDBGASMAPINFO
{
    /** The mapping address. */
    RTUINTPTR       Address;
    /** The segment mapped there.
     *  This is NIL_RTDBGSEGIDX if the entire module image is mapped here. */
    RTDBGSEGIDX     iSeg;
} RTDBGASMAPINFO;
/** Pointer to info about an address space mapping. */
typedef RTDBGASMAPINFO *PRTDBGASMAPINFO;
/** Pointer to const info about an address space mapping. */
typedef RTDBGASMAPINFO const *PCRTDBGASMAPINFO;

/**
 * Queries mapping information for a module given by index.
 *
 * @returns IRPT status code.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_OUT_OF_RANGE if the name index was out of range.
 * @retval  VINF_BUFFER_OVERFLOW if the array is too small and the returned
 *          information is incomplete.
 *
 * @param   hDbgAs          The address space handle.
 * @param   iModule         The index of the module to get.
 * @param   paMappings      Where to return the mapping information.  The buffer
 *                          size is given by *pcMappings.
 * @param   pcMappings      IN: Size of the paMappings array. OUT: The number of
 *                          entries returned.
 * @param   fFlags          Flags for reserved for future use. MBZ.
 *
 * @remarks See remarks for RTDbgAsModuleByIndex regarding the volatility of the
 *          iModule parameter.
 */
RTDECL(int) RTDbgAsModuleQueryMapByIndex(RTDBGAS hDbgAs, uint32_t iModule, PRTDBGASMAPINFO paMappings, uint32_t *pcMappings, uint32_t fFlags);

/**
 * Adds a symbol to a module in the address space.
 *
 * @returns IPRT status code. See RTDbgModSymbolAdd for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if no module was found at the specified address.
 * @retval  VERR_NOT_SUPPORTED if the module interpret doesn't support adding
 *          custom symbols.
 *
 * @param   hDbgAs          The address space handle.
 * @param   pszSymbol       The symbol name.
 * @param   Addr            The address of the symbol.
 * @param   cb              The size of the symbol.
 * @param   fFlags          Symbol flags, RTDBGSYMBOLADD_F_XXX.
 * @param   piOrdinal       Where to return the symbol ordinal on success. If
 *                          the interpreter doesn't do ordinals, this will be set to
 *                          UINT32_MAX. Optional
 */
RTDECL(int) RTDbgAsSymbolAdd(RTDBGAS hDbgAs, const char *pszSymbol, RTUINTPTR Addr, RTUINTPTR cb, uint32_t fFlags, uint32_t *piOrdinal);

/**
 * Query a symbol by address.
 *
 * @returns IPRT status code. See RTDbgModSymbolAddr for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if the address couldn't be mapped to a module.
 * @retval  VERR_INVALID_PARAMETER if incorrect flags.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            The address which closest symbol is requested.
 * @param   fFlags          Symbol search flags, see RTDBGSYMADDR_FLAGS_XXX.
 * @param   poffDisp        Where to return the distance between the symbol
 *                          and address. Optional.
 * @param   pSymbol         Where to return the symbol info.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsSymbolByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, uint32_t fFlags,
                                PRTINTPTR poffDisp, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod);

/**
 * Query a symbol by address.
 *
 * @returns IPRT status code. See RTDbgModSymbolAddrA for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if the address couldn't be mapped to a module.
 * @retval  VERR_INVALID_PARAMETER if incorrect flags.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            The address which closest symbol is requested.
 * @param   fFlags          Symbol search flags, see RTDBGSYMADDR_FLAGS_XXX.
 * @param   poffDisp        Where to return the distance between the symbol
 *                          and address. Optional.
 * @param   ppSymInfo       Where to return the pointer to the allocated symbol
 *                          info. Always set. Free with RTDbgSymbolFree.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsSymbolByAddrA(RTDBGAS hDbgAs, RTUINTPTR Addr, uint32_t fFlags,
                                 PRTINTPTR poffDisp, PRTDBGSYMBOL *ppSymInfo, PRTDBGMOD phMod);

/**
 * Query a symbol by name.
 *
 * @returns IPRT status code.
 * @retval  VERR_SYMBOL_NOT_FOUND if not found.
 *
 * @param   hDbgAs          The address space handle.
 * @param   pszSymbol       The symbol name. It is possible to limit the scope
 *                          of the search by prefixing the symbol with a module
 *                          name pattern followed by a bang (!) character.
 *                          RTStrSimplePatternNMatch is used for the matching.
 * @param   pSymbol         Where to return the symbol info.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsSymbolByName(RTDBGAS hDbgAs, const char *pszSymbol, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod);

/**
 * Query a symbol by name, allocating the returned symbol structure.
 *
 * @returns IPRT status code.
 * @retval  VERR_SYMBOL_NOT_FOUND if not found.
 *
 * @param   hDbgAs          The address space handle.
 * @param   pszSymbol       The symbol name. See RTDbgAsSymbolByName for more.
 * @param   ppSymbol        Where to return the pointer to the allocated
 *                          symbol info. Always set. Free with RTDbgSymbolFree.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsSymbolByNameA(RTDBGAS hDbgAs, const char *pszSymbol, PRTDBGSYMBOL *ppSymbol, PRTDBGMOD phMod);

/**
 * Adds a line number to a module in the address space.
 *
 * @returns IPRT status code. See RTDbgModLineAdd for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if no module was found at the specified address.
 * @retval  VERR_NOT_SUPPORTED if the module interpret doesn't support adding
 *          custom symbols.
 *
 * @param   hDbgAs          The address space handle.
 * @param   pszFile         The file name.
 * @param   uLineNo         The line number.
 * @param   Addr            The address of the symbol.
 * @param   piOrdinal       Where to return the line number ordinal on success.
 *                          If the interpreter doesn't do ordinals, this will be
 *                          set to UINT32_MAX. Optional.
 */
RTDECL(int) RTDbgAsLineAdd(RTDBGAS hDbgAs, const char *pszFile, uint32_t uLineNo, RTUINTPTR Addr, uint32_t *piOrdinal);

/**
 * Query a line number by address.
 *
 * @returns IPRT status code. See RTDbgModLineAddrA for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if the address couldn't be mapped to a module.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            The address which closest symbol is requested.
 * @param   poffDisp        Where to return the distance between the line
 *                          number and address.
 * @param   pLine           Where to return the line number information.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsLineByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTINTPTR poffDisp, PRTDBGLINE pLine, PRTDBGMOD phMod);

/**
 * Query a line number by address.
 *
 * @returns IPRT status code. See RTDbgModLineAddrA for more specific ones.
 * @retval  VERR_INVALID_HANDLE if hDbgAs is invalid.
 * @retval  VERR_NOT_FOUND if the address couldn't be mapped to a module.
 *
 * @param   hDbgAs          The address space handle.
 * @param   Addr            The address which closest symbol is requested.
 * @param   poffDisp        Where to return the distance between the line
 *                          number and address.
 * @param   ppLine          Where to return the pointer to the allocated line
 *                          number info. Always set. Free with RTDbgLineFree.
 * @param   phMod           Where to return the module handle. Optional.
 */
RTDECL(int) RTDbgAsLineByAddrA(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTINTPTR poffDisp, PRTDBGLINE *ppLine, PRTDBGMOD phMod);

/** @todo Missing some bits here. */

/** @} */


# ifdef IN_RING3
/** @defgroup grp_rt_dbgmod     RTDbgMod - Debug Module Interpreter
 * @{
 */

/**
 * Creates a module based on the default debug info container.
 *
 * This can be used to manually load a module and its symbol. The primary user
 * group is the debug info interpreters, which use this API to create an
 * efficient debug info container behind the scenes and forward all queries to
 * it once the info has been loaded.
 *
 * @returns IPRT status code.
 *
 * @param   phDbgMod        Where to return the module handle.
 * @param   pszName         The name of the module (mandatory).
 * @param   cbSeg           The size of initial segment. If zero, segments will
 *                          have to be added manually using RTDbgModSegmentAdd.
 * @param   fFlags          Flags reserved for future extensions, MBZ for now.
 */
RTDECL(int)         RTDbgModCreate(PRTDBGMOD phDbgMod, const char *pszName, RTUINTPTR cbSeg, uint32_t fFlags);

RTDECL(int)         RTDbgModCreateFromImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                            RTLDRARCH enmArch, RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromMap(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName, RTUINTPTR uSubtrahend,
                                          RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromPeImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                              PRTLDRMOD phLdrMod, uint32_t cbImage, uint32_t uTimeDateStamp, RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromDbg(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName, uint32_t cbImage,
                                          uint32_t uTimeDateStamp, RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromPdb(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName, uint32_t cbImage,
                                          PCRTUUID pUuid, uint32_t Age, RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromDwo(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName, uint32_t cbImage,
                                          uint32_t uCrc32, RTDBGCFG hDbgCfg);
RTDECL(int)         RTDbgModCreateFromMachOImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                                 RTLDRARCH enmArch, PRTLDRMOD phLdrModIn, uint32_t cbImage, uint32_t cSegs,
                                                 PCRTDBGSEGMENT paSegs, PCRTUUID pUuid, RTDBGCFG hDbgCfg, uint32_t fFlags);

/** @name Flags for RTDbgModCreate and friends.
 * @{ */
/** Overrides the hDbgCfg settings and forces an image and/or symbol file
 *  search.  RTDbgModCreate will quietly ignore this flag. */
#define RTDBGMOD_F_NOT_DEFERRED         RT_BIT_32(0)
/** Mach-O: Load the __LINKEDIT segment (@sa RTLDR_O_MACHO_LOAD_LINKEDIT). */
#define RTDBGMOD_F_MACHO_LOAD_LINKEDIT  RT_BIT_32(1)
/** Valid flag mask. */
#define RTDBGMOD_F_VALID_MASK           UINT32_C(0x00000003)
/** @} */


/**
 * Retains another reference to the module.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 *
 * @param   hDbgMod         The module handle.
 *
 * @remarks Will not take any locks.
 */
RTDECL(uint32_t)    RTDbgModRetain(RTDBGMOD hDbgMod);

/**
 * Release a reference to the module.
 *
 * When the reference count reaches zero, the module is destroyed.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 *
 * @param   hDbgMod         The module handle. The NIL handle is quietly ignored
 *                          and 0 is returned.
 *
 * @remarks Will not take any locks.
 */
RTDECL(uint32_t)    RTDbgModRelease(RTDBGMOD hDbgMod);

/**
 * Removes all content from the debug module (container), optionally only
 * leaving segments and image size intact.
 *
 * This is only possible on container modules, i.e. created by RTDbgModCreate().
 *
 * @returns IPRT status code.
 * @param   hDbgMod         The module handle.
 * @param   fLeaveSegments  Whether to leave segments (and image size) as is.
 */
RTDECL(int)         RTDbgModRemoveAll(RTDBGMOD hDbgMod, bool fLeaveSegments);

/**
 * Gets the module name.
 *
 * @returns Pointer to a read only string containing the name.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(const char *) RTDbgModName(RTDBGMOD hDbgMod);

/**
 * Gets the name of the debug info file we're using.
 *
 * @returns Pointer to a read only string containing the filename, NULL if we
 *          don't use one.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(const char *) RTDbgModDebugFile(RTDBGMOD hDbgMod);

/**
 * Gets the image filename (as specified by the user).
 *
 * @returns Pointer to a read only string containing the filename.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(const char *) RTDbgModImageFile(RTDBGMOD hDbgMod);

/**
 * Gets the image filename actually used if it differs from RTDbgModImageFile.
 *
 * @returns Pointer to a read only string containing the filename, NULL if same
 *          as RTDBgModImageFile.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(const char *) RTDbgModImageFileUsed(RTDBGMOD hDbgMod);

/**
 * Checks if the loading of the debug info has been postponed.
 *
 * @returns true if postponed, false if not or invalid handle.
 * @param   hDbgMod         The module handle.
 */
RTDECL(bool)        RTDbgModIsDeferred(RTDBGMOD hDbgMod);

/**
 * Checks if the debug info is exports only.
 *
 * @returns true if exports only, false if not or invalid handle.
 * @param   hDbgMod         The module handle.
 */
RTDECL(bool)        RTDbgModIsExports(RTDBGMOD hDbgMod);

/**
 * Converts an image relative address to a segment:offset address.
 *
 * @returns Segment index on success.
 *          NIL_RTDBGSEGIDX is returned if the module handle or the RVA are
 *          invalid.
 *
 * @param   hDbgMod         The module handle.
 * @param   uRva            The image relative address to convert.
 * @param   poffSeg         Where to return the segment offset. Optional.
 */
RTDECL(RTDBGSEGIDX) RTDbgModRvaToSegOff(RTDBGMOD hDbgMod, RTUINTPTR uRva, PRTUINTPTR poffSeg);

/**
 * Gets the module tag value if any.
 *
 * @returns The tag. 0 if hDbgMod is invalid.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(uint64_t)    RTDbgModGetTag(RTDBGMOD hDbgMod);

/**
 * Tags or untags the module.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 *
 * @param   hDbgMod         The module handle.
 * @param   uTag            The tag value.  The convention is that 0 is no tag
 *                          and any other value means it's tagged.  It's adviced
 *                          to use some kind of unique number like an address
 *                          (global or string cache for instance) to avoid
 *                          collisions with other users
 */
RTDECL(int)         RTDbgModSetTag(RTDBGMOD hDbgMod, uint64_t uTag);


/**
 * Image size when mapped if segments are mapped adjacently.
 *
 * For ELF, PE, and Mach-O images this is (usually) a natural query, for LX and
 * NE and such it's a bit odder and the answer may not make much sense for them.
 *
 * @returns Image mapped size.
 *          RTUINTPTR_MAX is returned if the handle is invalid.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(RTUINTPTR)   RTDbgModImageSize(RTDBGMOD hDbgMod);

/**
 * Gets the image format.
 *
 * @returns Image format.
 * @retval  RTLDRFMT_INVALID if the handle is invalid or if the format isn't known.
 * @param   hDbgMod         The debug module handle.
 * @sa      RTLdrGetFormat
 */
RTDECL(RTLDRFMT)    RTDbgModImageGetFormat(RTDBGMOD hDbgMod);

/**
 * Gets the image architecture.
 *
 * @returns Image architecture.
 * @retval  RTLDRARCH_INVALID if the handle is invalid.
 * @retval  RTLDRARCH_WHATEVER if unknown.
 * @param   hDbgMod         The debug module handle.
 * @sa      RTLdrGetArch
 */
RTDECL(RTLDRARCH)   RTDbgModImageGetArch(RTDBGMOD hDbgMod);

/**
 * Generic method for querying image properties.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the property query isn't supported (either all
 *          or that specific property).  The caller must handle this result.
 * @retval  VERR_NOT_FOUND the property was not found in the module.  The caller
 *          must also normally deal with this.
 * @retval  VERR_INVALID_FUNCTION if the function value is wrong.
 * @retval  VERR_INVALID_PARAMETER if the fixed buffer size is wrong. Correct
 *          size in @a *pcbRet.
 * @retval  VERR_BUFFER_OVERFLOW if the function doesn't have a fixed size
 *          buffer and the buffer isn't big enough. Correct size in @a *pcbRet.
 * @retval  VERR_INVALID_HANDLE if the handle is invalid.
 *
 * @param   hDbgMod         The debug module handle.
 * @param   enmProp         The property to query.
 * @param   pvBuf           Pointer to the input / output buffer.  In most cases
 *                          it's only used for returning data.
 * @param   cbBuf           The size of the buffer.
 * @param   pcbRet          Where to return the amount of data returned.  On
 *                          buffer size errors, this is set to the correct size.
 *                          Optional.
 * @sa      RTLdrQueryPropEx
 */
RTDECL(int)         RTDbgModImageQueryProp(RTDBGMOD hDbgMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf, size_t *pcbRet);


/**
 * Adds a segment to the module. Optional feature.
 *
 * This method is intended used for manually constructing debug info for a
 * module. The main usage is from other debug info interpreters that want to
 * avoid writing a debug info database and instead uses the standard container
 * behind the scenes.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if this feature isn't support by the debug info
 *          interpreter. This is a common return code.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_ADDRESS_WRAP if uRva+cb wraps around.
 * @retval  VERR_DBG_SEGMENT_NAME_OUT_OF_RANGE if pszName is too short or long.
 * @retval  VERR_INVALID_PARAMETER if fFlags contains undefined flags.
 * @retval  VERR_DBG_SPECIAL_SEGMENT if *piSeg is a special segment.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if *piSeg doesn't meet expectations.
 *
 * @param   hDbgMod             The module handle.
 * @param   uRva                The image relative address of the segment.
 * @param   cb                  The size of the segment.
 * @param   pszName             The segment name. Does not normally need to be
 *                              unique, although this is somewhat up to the
 *                              debug interpreter to decide.
 * @param   fFlags              Segment flags. Reserved for future used, MBZ.
 * @param   piSeg               The segment index or NIL_RTDBGSEGIDX on input.
 *                              The assigned segment index on successful return.
 *                              Optional.
 */
RTDECL(int)         RTDbgModSegmentAdd(RTDBGMOD hDbgMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                                       uint32_t fFlags, PRTDBGSEGIDX piSeg);

/**
 * Gets the number of segments in the module.
 *
 * This is can be used to determine the range which can be passed to
 * RTDbgModSegmentByIndex and derivates.
 *
 * @returns The segment relative address.
 *          NIL_RTDBGSEGIDX if the handle is invalid.
 *
 * @param   hDbgMod         The module handle.
 */
RTDECL(RTDBGSEGIDX) RTDbgModSegmentCount(RTDBGMOD hDbgMod);

/**
 * Query information about a segment.
 *
 * This can be used together with RTDbgModSegmentCount to enumerate segments.
 * The index starts a 0 and stops one below RTDbgModSegmentCount.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if iSeg is too high.
 * @retval  VERR_DBG_SPECIAL_SEGMENT if iSeg indicates a special segment.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 *
 * @param   hDbgMod         The module handle.
 * @param   iSeg            The segment index. No special segments.
 * @param   pSegInfo        Where to return the segment info. The
 *                          RTDBGSEGMENT::Address member will be set to
 *                          RTUINTPTR_MAX or the load address used at link time.
 */
RTDECL(int)         RTDbgModSegmentByIndex(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo);

/**
 * Gets the size of a segment.
 *
 * This is a just a wrapper around RTDbgModSegmentByIndex.
 *
 * @returns The segment size.
 *          RTUINTPTR_MAX is returned if either the handle and segment index are
 *          invalid.
 *
 * @param   hDbgMod         The module handle.
 * @param   iSeg            The segment index. RTDBGSEGIDX_ABS is not allowed.
 *                          If RTDBGSEGIDX_RVA is used, the functions returns
 *                          the same value as RTDbgModImageSize.
 */
RTDECL(RTUINTPTR)   RTDbgModSegmentSize(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg);

/**
 * Gets the image relative address of a segment.
 *
 * This is a just a wrapper around RTDbgModSegmentByIndex.
 *
 * @returns The segment relative address.
 *          RTUINTPTR_MAX is returned if either the handle and segment index are
 *          invalid.
 *
 * @param   hDbgMod         The module handle.
 * @param   iSeg            The segment index. No special segment indexes
 *                          allowed (asserted).
 */
RTDECL(RTUINTPTR)   RTDbgModSegmentRva(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg);


/**
 * Adds a line number to the module.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the module interpret doesn't support adding
 *          custom symbols. This is a common place occurrence.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE if the symbol name is too long or
 *          short.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 * @retval  VERR_DBG_ADDRESS_WRAP if off+cb wraps around.
 * @retval  VERR_INVALID_PARAMETER if the symbol flags sets undefined bits.
 * @retval  VERR_DBG_DUPLICATE_SYMBOL
 * @retval  VERR_DBG_ADDRESS_CONFLICT
 *
 * @param   hDbgMod         The module handle.
 * @param   pszSymbol       The symbol name.
 * @param   iSeg            The segment index.
 * @param   off             The segment offset.
 * @param   cb              The size of the symbol. Can be zero, although this
 *                          may depend somewhat on the debug interpreter.
 * @param   fFlags          Symbol flags, RTDBGSYMBOLADD_F_XXX.
 * @param   piOrdinal       Where to return the symbol ordinal on success. If
 *                          the interpreter doesn't do ordinals, this will be set to
 *                          UINT32_MAX. Optional.
 */
RTDECL(int)         RTDbgModSymbolAdd(RTDBGMOD hDbgMod, const char *pszSymbol, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                      RTUINTPTR cb, uint32_t fFlags, uint32_t *piOrdinal);

/**
 * Gets the symbol count.
 *
 * This can be used together wtih RTDbgModSymbolByOrdinal or
 * RTDbgModSymbolByOrdinalA to enumerate all the symbols.
 *
 * @returns The number of symbols in the module.
 *          UINT32_MAX is returned if the module handle is invalid or some other
 *          error occurs.
 *
 * @param   hDbgMod             The module handle.
 */
RTDECL(uint32_t)    RTDbgModSymbolCount(RTDBGMOD hDbgMod);

/**
 * Queries symbol information by ordinal number.
 *
 * @returns IPRT status code.
 * @retval  VERR_SYMBOL_NOT_FOUND if there is no symbol at the given number.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_NOT_SUPPORTED if lookup by ordinal is not supported.
 *
 * @param   hDbgMod             The module handle.
 * @param   iOrdinal            The symbol ordinal number. 0-based. The highest
 *                              number is RTDbgModSymbolCount() - 1.
 * @param   pSymInfo            Where to store the symbol information.
 */
RTDECL(int)         RTDbgModSymbolByOrdinal(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo);

/**
 * Queries symbol information by ordinal number.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_NOT_SUPPORTED if lookup by ordinal is not supported.
 * @retval  VERR_SYMBOL_NOT_FOUND if there is no symbol at the given number.
 * @retval  VERR_NO_MEMORY if RTDbgSymbolAlloc fails.
 *
 * @param   hDbgMod             The module handle.
 * @param   iOrdinal            The symbol ordinal number. 0-based. The highest
 *                              number is RTDbgModSymbolCount() - 1.
 * @param   ppSymInfo           Where to store the pointer to the returned
 *                              symbol information. Always set. Free with
 *                              RTDbgSymbolFree.
 */
RTDECL(int)         RTDbgModSymbolByOrdinalA(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGSYMBOL *ppSymInfo);

/**
 * Queries symbol information by address.
 *
 * The returned symbol is what the debug info interpreter considers the symbol
 * most applicable to the specified address. This usually means a symbol with an
 * address equal or lower than the requested.
 *
 * @returns IPRT status code.
 * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 * @retval  VERR_INVALID_PARAMETER if incorrect flags.
 *
 * @param   hDbgMod             The module handle.
 * @param   iSeg                The segment number.
 * @param   off                 The offset into the segment.
 * @param   fFlags              Symbol search flags, see RTDBGSYMADDR_FLAGS_XXX.
 * @param   poffDisp            Where to store the distance between the
 *                              specified address and the returned symbol.
 *                              Optional.
 * @param   pSymInfo            Where to store the symbol information.
 */
RTDECL(int)         RTDbgModSymbolByAddr(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                         PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo);

/**
 * Queries symbol information by address.
 *
 * The returned symbol is what the debug info interpreter considers the symbol
 * most applicable to the specified address. This usually means a symbol with an
 * address equal or lower than the requested.
 *
 * @returns IPRT status code.
 * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 * @retval  VERR_NO_MEMORY if RTDbgSymbolAlloc fails.
 * @retval  VERR_INVALID_PARAMETER if incorrect flags.
 *
 * @param   hDbgMod             The module handle.
 * @param   iSeg                The segment index.
 * @param   off                 The offset into the segment.
 * @param   fFlags              Symbol search flags, see RTDBGSYMADDR_FLAGS_XXX.
 * @param   poffDisp            Where to store the distance between the
 *                              specified address and the returned symbol. Optional.
 * @param   ppSymInfo           Where to store the pointer to the returned
 *                              symbol information. Always set. Free with
 *                              RTDbgSymbolFree.
 */
RTDECL(int)         RTDbgModSymbolByAddrA(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                          PRTINTPTR poffDisp, PRTDBGSYMBOL *ppSymInfo);

/**
 * Queries symbol information by symbol name.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
 * @retval  VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE if the symbol name is too long or
 *          short.
 *
 * @param   hDbgMod             The module handle.
 * @param   pszSymbol           The symbol name.
 * @param   pSymInfo            Where to store the symbol information.
 */
RTDECL(int)         RTDbgModSymbolByName(RTDBGMOD hDbgMod, const char *pszSymbol, PRTDBGSYMBOL pSymInfo);

/**
 * Queries symbol information by symbol name.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
 * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
 * @retval  VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE if the symbol name is too long or
 *          short.
 * @retval  VERR_NO_MEMORY if RTDbgSymbolAlloc fails.
 *
 * @param   hDbgMod             The module handle.
 * @param   pszSymbol           The symbol name.
 * @param   ppSymInfo           Where to store the pointer to the returned
 *                              symbol information. Always set. Free with
 *                              RTDbgSymbolFree.
 */
RTDECL(int)         RTDbgModSymbolByNameA(RTDBGMOD hDbgMod, const char *pszSymbol, PRTDBGSYMBOL *ppSymInfo);

/**
 * Adds a line number to the module.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the module interpret doesn't support adding
 *          custom symbols. This should be consider a normal response.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_FILE_NAME_OUT_OF_RANGE if the file name is too longer or
 *          empty.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 * @retval  VERR_INVALID_PARAMETER if the line number flags sets undefined bits.
 *
 * @param   hDbgMod             The module handle.
 * @param   pszFile             The file name.
 * @param   uLineNo             The line number.
 * @param   iSeg                The segment index.
 * @param   off                 The segment offset.
 * @param   piOrdinal           Where to return the line number ordinal on
 *                              success. If  the interpreter doesn't do ordinals,
 *                              this will be set to UINT32_MAX. Optional.
 */
RTDECL(int)         RTDbgModLineAdd(RTDBGMOD hDbgMod, const char *pszFile, uint32_t uLineNo,
                                    RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t *piOrdinal);

/**
 * Gets the line number count.
 *
 * This can be used together wtih RTDbgModLineByOrdinal or RTDbgModSymbolByLineA
 * to enumerate all the line number information.
 *
 * @returns The number of line numbers in the module.
 *          UINT32_MAX is returned if the module handle is invalid or some other
 *          error occurs.
 *
 * @param   hDbgMod             The module handle.
 */
RTDECL(uint32_t)    RTDbgModLineCount(RTDBGMOD hDbgMod);

/**
 * Queries line number information by ordinal number.
 *
 * This can be used to enumerate the line numbers for the module. Use
 * RTDbgModLineCount() to figure the end of the ordinals.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
 * @retval  VERR_DBG_LINE_NOT_FOUND if there is no line number with that
 *          ordinal.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.

 * @param   hDbgMod             The module handle.
 * @param   iOrdinal            The line number ordinal number.
 * @param   pLineInfo           Where to store the information about the line
 *                              number.
 */
RTDECL(int)         RTDbgModLineByOrdinal(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo);

/**
 * Queries line number information by ordinal number.
 *
 * This can be used to enumerate the line numbers for the module. Use
 * RTDbgModLineCount() to figure the end of the ordinals.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
 * @retval  VERR_DBG_LINE_NOT_FOUND if there is no line number with that
 *          ordinal.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_NO_MEMORY if RTDbgLineAlloc fails.
 *
 * @param   hDbgMod             The module handle.
 * @param   iOrdinal            The line number ordinal number.
 * @param   ppLineInfo          Where to store the pointer to the returned line
 *                              number information. Always set. Free with
 *                              RTDbgLineFree.
 */
RTDECL(int)         RTDbgModLineByOrdinalA(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGLINE *ppLineInfo);

/**
 * Queries line number information by address.
 *
 * The returned line number is what the debug info interpreter considers the
 * one most applicable to the specified address. This usually means a line
 * number with an address equal or lower than the requested.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
 * @retval  VERR_DBG_LINE_NOT_FOUND if no suitable line number was found.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 *
 * @param   hDbgMod             The module handle.
 * @param   iSeg                The segment number.
 * @param   off                 The offset into the segment.
 * @param   poffDisp            Where to store the distance between the
 *                              specified address and the returned symbol.
 *                              Optional.
 * @param   pLineInfo           Where to store the line number information.
 */
RTDECL(int)         RTDbgModLineByAddr(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTINTPTR poffDisp, PRTDBGLINE pLineInfo);

/**
 * Queries line number information by address.
 *
 * The returned line number is what the debug info interpreter considers the
 * one most applicable to the specified address. This usually means a line
 * number with an address equal or lower than the requested.
 *
 * @returns IPRT status code.
 * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
 * @retval  VERR_DBG_LINE_NOT_FOUND if no suitable line number was found.
 * @retval  VERR_INVALID_HANDLE if hDbgMod is invalid.
 * @retval  VERR_DBG_INVALID_RVA if an image relative address is specified and
 *          it's not inside any of the segments defined by the module.
 * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if the segment index isn't valid.
 * @retval  VERR_DBG_INVALID_SEGMENT_OFFSET if the segment offset is beyond the
 *          end of the segment.
 * @retval  VERR_NO_MEMORY if RTDbgLineAlloc fails.
 *
 * @param   hDbgMod             The module handle.
 * @param   iSeg                The segment number.
 * @param   off                 The offset into the segment.
 * @param   poffDisp            Where to store the distance between the
 *                              specified address and the returned symbol.
 *                              Optional.
 * @param   ppLineInfo          Where to store the pointer to the returned line
 *                              number information. Always set. Free with
 *                              RTDbgLineFree.
 */
RTDECL(int)         RTDbgModLineByAddrA(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTINTPTR poffDisp, PRTDBGLINE *ppLineInfo);

/**
 * Try use unwind information to unwind one frame.
 *
 * @returns IPRT status code.  Last informational status from stack reader callback.
 * @retval  VERR_DBG_NO_UNWIND_INFO if the module contains no unwind information.
 * @retval  VERR_DBG_UNWIND_INFO_NOT_FOUND if no unwind information was found
 *          for the location given by iSeg:off.
 *
 * @param   hDbgMod             The module handle.
 * @param   iSeg                The segment number of the program counter.
 * @param   off                 The offset into @a iSeg.  Together with @a iSeg
 *                              this corresponds to the RTDBGUNWINDSTATE::uPc
 *                              value pointed to by @a pState.
 * @param   pState              The unwind state to work.
 *
 * @sa      RTLdrUnwindFrame
 */
RTDECL(int)         RTDbgModUnwindFrame(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState);

/** @} */
# endif /* IN_RING3 */



/** @name Kernel Debug Info API
 *
 * This is a specialized API for obtaining symbols and structure information
 * about the running kernel.  It is relatively OS specific.  Its purpose and
 * operation is doesn't map all that well onto RTDbgMod, so a few dedicated
 * functions was created for it.
 *
 * @{ */

/** Handle to the kernel debug info. */
typedef struct RTDBGKRNLINFOINT *RTDBGKRNLINFO;
/** Pointer to a kernel debug info handle. */
typedef RTDBGKRNLINFO           *PRTDBGKRNLINFO;
/** Nil kernel debug info handle. */
#define NIL_RTDBGKRNLINFO       ((RTDBGKRNLINFO)0)

/**
 * Opens the kernel debug info.
 *
 * @returns IPRT status code.  Can fail for any number of reasons.
 *
 * @param   phKrnlInfo      Where to return the kernel debug info handle on
 *                          success.
 * @param   fFlags          Flags reserved for future use. Must be zero.
 */
RTR0DECL(int)       RTR0DbgKrnlInfoOpen(PRTDBGKRNLINFO phKrnlInfo, uint32_t fFlags);

/**
 * Retains a reference to the kernel debug info handle.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 * @param   hKrnlInfo       The kernel info handle.
 */
RTR0DECL(uint32_t)  RTR0DbgKrnlInfoRetain(RTDBGKRNLINFO hKrnlInfo);


/**
 * Releases a reference to the kernel debug info handle, destroying it when the
 * counter reaches zero.
 *
 * @returns New reference count, UINT32_MAX on invalid handle (asserted).
 * @param   hKrnlInfo       The kernel info handle. NIL_RTDBGKRNLINFO is
 *                          quietly ignored.
 */
RTR0DECL(uint32_t)  RTR0DbgKrnlInfoRelease(RTDBGKRNLINFO hKrnlInfo);

/**
 * Queries the offset (in bytes) of a member of a kernel structure.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and offset at @a poffMember.
 * @retval  VERR_NOT_FOUND if the structure or the member was not found.
 * @retval  VERR_INVALID_HANDLE if hKrnlInfo is bad.
 * @retval  VERR_INVALID_POINTER if any of the pointers are bad.
 *
 * @param   hKrnlInfo       The kernel info handle.
 * @param   pszModule       The name of the module to search, pass NULL to
 *                          search the default kernel module(s).
 * @param   pszStructure    The structure name.
 * @param   pszMember       The member name.
 * @param   poffMember      Where to return the offset.
 */
RTR0DECL(int)       RTR0DbgKrnlInfoQueryMember(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszStructure,
                                               const char *pszMember, size_t *poffMember);


/**
 * Queries the value (usually the address) of a kernel symbol.
 *
 * This may go looking for the symbol in other modules, in which case it will
 * always check the kernel symbol table first.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and value at @a ppvSymbol.
 * @retval  VERR_SYMBOL_NOT_FOUND
 * @retval  VERR_INVALID_HANDLE if hKrnlInfo is bad.
 * @retval  VERR_INVALID_POINTER if any of the pointers are bad.
 *
 * @param   hKrnlInfo       The kernel info handle.
 * @param   pszModule       The name of the module to search, pass NULL to
 *                          search the default kernel module(s).
 * @param   pszSymbol       The C name of the symbol.
 *                          On Windows NT there are the following special symbols:
 *                              - __ImageBase: The base address of the module.
 *                              - __ImageSize: The size of the module.
 *                              - __ImageNtHdrs: Address of the NT headers.
 * @param   ppvSymbol       Where to return the symbol value, passing NULL is
 *                          OK. This may be modified even on failure, in
 *                          particular, it will be set to NULL when
 *                          VERR_SYMBOL_NOT_FOUND is returned.
 *
 * @sa      RTR0DbgKrnlInfoGetSymbol, RTLdrGetSymbol
 */
RTR0DECL(int)       RTR0DbgKrnlInfoQuerySymbol(RTDBGKRNLINFO hKrnlInfo, const char *pszModule,
                                               const char *pszSymbol, void **ppvSymbol);

/**
 * Wrapper around RTR0DbgKrnlInfoQuerySymbol that returns the symbol.
 *
 * @return  Symbol address if found, NULL if not found or some invalid parameter
 *          or something.
 * @param   hKrnlInfo       The kernel info handle.
 * @param   pszModule       The name of the module to search, pass NULL to
 *                          search the default kernel module(s).
 * @param   pszSymbol       The C name of the symbol.
 *                          On Windows NT there are the following special symbols:
 *                              - __ImageBase: The base address of the module.
 *                              - __ImageSize: The size of the module.
 *                              - __ImageNtHdrs: Address of the NT headers.
 * @sa      RTR0DbgKrnlInfoQuerySymbol, RTLdrGetSymbol
 */
RTR0DECL(void *)    RTR0DbgKrnlInfoGetSymbol(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszSymbol);

/**
 * Queries the size (in bytes) of a kernel data type.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and size at @a pcbType.
 * @retval  VERR_NOT_FOUND if the type was not found.
 * @retval  VERR_INVALID_HANDLE if hKrnlInfo is bad.
 * @retval  VERR_INVALID_POINTER if any of the pointers are bad.
 * @retval  VERR_WRONG_TYPE if the type was not a valid data type (e.g. a
 *          function)
 *
 * @param   hKrnlInfo       The kernel info handle.
 * @param   pszModule       The name of the module to search, pass NULL to
 *                          search the default kernel module(s).
 * @param   pszType         The type name.
 * @param   pcbType         Where to return the size of the type.
 */
RTR0DECL(int)       RTR0DbgKrnlInfoQuerySize(RTDBGKRNLINFO hKrnlInfo, const char *pszModule,
                                             const char *pszType, size_t *pcbType);
/** @} */

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_dbg_h */

