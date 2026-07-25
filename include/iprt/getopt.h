/** @file
 * IPRT - Command Line Parsing.
 */

/*
 * Copyright (C) 2007-2025 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_getopt_h
#define IPRT_INCLUDED_getopt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/errcore.h> /* for VINF_GETOPT_NOT_OPTION */

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_getopt    RTGetOpt - Command Line Parsing
 * @ingroup grp_rt
 * @{
 */

/** @name Values for RTGETOPTDEF::fFlags and the fFlags parameter of
 *        RTGetOptFetchValue.
 *
 * @remarks When neither of the RTGETOPT_FLAG_HEX, RTGETOPT_FLAG_OCT and RTGETOPT_FLAG_DEC
 *          flags are specified with a integer value format, RTGetOpt will default to
 *          decimal but recognize the 0x prefix when present. RTGetOpt will not look for
 *          for the octal prefix (0).
 * @{ */
/** Requires no extra argument.
 * (Can be assumed to be 0 for ever.) */
#define RTGETOPT_REQ_NOTHING                    0
/** A value is required or error will be returned. */
#define RTGETOPT_REQ_STRING                     1
/** The value must be a valid signed 8-bit integer or an error will be returned. */
#define RTGETOPT_REQ_INT8                       2
/** The value must be a valid unsigned 8-bit integer or an error will be returned. */
#define RTGETOPT_REQ_UINT8                      3
/** The value must be a valid signed 16-bit integer or an error will be returned. */
#define RTGETOPT_REQ_INT16                      4
/** The value must be a valid unsigned 16-bit integer or an error will be returned. */
#define RTGETOPT_REQ_UINT16                     5
/** The value must be a valid signed 32-bit integer or an error will be returned. */
#define RTGETOPT_REQ_INT32                      6
/** The value must be a valid unsigned 32-bit integer or an error will be returned. */
#define RTGETOPT_REQ_UINT32                     7
/** The value must be a valid signed 64-bit integer or an error will be returned. */
#define RTGETOPT_REQ_INT64                      8
/** The value must be a valid unsigned 64-bit integer or an error will be returned. */
#define RTGETOPT_REQ_UINT64                     9
/** The value must be a valid IPv4 address.
 * (Not a name, but 4 values in the 0..255 range with dots separating them). */
#define RTGETOPT_REQ_IPV4ADDR                   10
/** The value must be a valid IPv4 CIDR.
 * As with RTGETOPT_REQ_IPV4ADDR, no name.
 */
#define RTGETOPT_REQ_IPV4CIDR                   11
#if 0
/* take placers */
/** The value must be a valid IPv6 addr
 * @todo: Add types and parsing routines in (iprt/net.h)
 */
#define RTGETOPT_REQ_IPV6ADDR                   12
/** The value must be a valid IPv6 CIDR
 * @todo: Add types and parsing routines in (iprt/net.h)
 */
#define RTGETOPT_REQ_IPV6CIDR                   13
#endif
/** The value must be a valid ethernet MAC address. */
#define RTGETOPT_REQ_MACADDR                    14
/** The value must be a valid UUID. */
#define RTGETOPT_REQ_UUID                       15
/** The value must be a string with value as "on" or "off". */
#define RTGETOPT_REQ_BOOL_ONOFF                 16
/** Boolean option accepting a wide range of typical ways of
 * expression true and false. */
#define RTGETOPT_REQ_BOOL                       17
/** The value must two unsigned 32-bit integer values separated by a colon,
 * slash, pipe or space(s).  */
#define RTGETOPT_REQ_UINT32_PAIR                18
/** The value must two unsigned 64-bit integer values separated by a colon,
 * slash, pipe or space(s). */
#define RTGETOPT_REQ_UINT64_PAIR                19
/** The value must at least unsigned 32-bit integer value, optionally
 * followed by a second separated by a colon, slash, pipe or space(s). */
#define RTGETOPT_REQ_UINT32_OPTIONAL_PAIR       20
/** The value must at least unsigned 64-bit integer value, optionally
 * followed by a second separated by a colon, slash, pipe or space(s). */
#define RTGETOPT_REQ_UINT64_OPTIONAL_PAIR       21
/** The mask of the valid required types. */
#define RTGETOPT_REQ_MASK                       31
/** Treat the value as hexadecimal - only applicable with the RTGETOPT_REQ_*INT*. */
#define RTGETOPT_FLAG_HEX                       RT_BIT(16)
/** Treat the value as octal - only applicable with the RTGETOPT_REQ_*INT*. */
#define RTGETOPT_FLAG_OCT                       RT_BIT(17)
/** Treat the value as decimal - only applicable with the RTGETOPT_REQ_*INT*. */
#define RTGETOPT_FLAG_DEC                       RT_BIT(18)
/** The index value is attached to the argument - only valid for long arguments. */
#define RTGETOPT_FLAG_INDEX                     RT_BIT(19)
/** Used with RTGETOPT_FLAG_INDEX, setting index to zero if none given.
 * (The default is to fail with VERR_GETOPT_INDEX_MISSING.)  */
#define RTGETOPT_FLAG_INDEX_DEF_0               RT_BIT(20)
/** Used with RTGETOPT_FLAG_INDEX, setting index to one if none given.
 * (The default is to fail with VERR_GETOPT_INDEX_MISSING.)  */
#define RTGETOPT_FLAG_INDEX_DEF_1               RT_BIT(21)
/** For simplicity. */
#define RTGETOPT_FLAG_INDEX_DEF_MASK            (RT_BIT(20) | RT_BIT(21))
/** For simple conversion. */
#define RTGETOPT_FLAG_INDEX_DEF_SHIFT           20
/** For use with RTGETOPT_FLAG_INDEX_DEF_0 or RTGETOPT_FLAG_INDEX_DEF_1 to
 *  imply a dash before the index when a digit is specified.
 * This is for transitioning from options without index to optionally allow
 * index options, i.e. "--long" defaults to either index 1 or 1 using the above
 * flags, while "--long-1" explicitly gives the index ("--long-" is not valid).
 * This flag matches an "-" separating the "--long" string
 * (RTGETOPTDEFS::pszLong) from the index value.  */
#define RTGETOPT_FLAG_INDEX_DEF_DASH            RT_BIT(22)
/** Treat the long option as case insensitive. */
#define RTGETOPT_FLAG_ICASE                     RT_BIT(23)
/** Mask of valid bits - for validation. */
#define RTGETOPT_VALID_MASK                     (  RTGETOPT_REQ_MASK \
                                                 | RTGETOPT_FLAG_HEX \
                                                 | RTGETOPT_FLAG_OCT \
                                                 | RTGETOPT_FLAG_DEC \
                                                 | RTGETOPT_FLAG_INDEX \
                                                 | RTGETOPT_FLAG_INDEX_DEF_0 \
                                                 | RTGETOPT_FLAG_INDEX_DEF_1 \
                                                 | RTGETOPT_FLAG_INDEX_DEF_DASH \
                                                 | RTGETOPT_FLAG_ICASE )
/** @} */

/**
 * An option definition.
 */
typedef struct RTGETOPTDEF
{
    /** The long option.
     * This is optional */
    const char     *pszLong;
    /** The short option character.
     * This doesn't have to be a character, it may also be a \#define or enum value if
     * there isn't any short version of this option. Must be greater than 0. */
    int             iShort;
    /** The flags (RTGETOPT_*). */
    unsigned        fFlags;
} RTGETOPTDEF;
/** Pointer to an option definition. */
typedef RTGETOPTDEF *PRTGETOPTDEF;
/** Pointer to an const option definition. */
typedef const RTGETOPTDEF *PCRTGETOPTDEF;

/**
 * Option argument union.
 *
 * What ends up here depends on argument format in the option definition.
 */
typedef union RTGETOPTUNION
{
    /** Pointer to the definition on failure or when the option doesn't take an argument.
     * This can be NULL for some errors. */
    PCRTGETOPTDEF   pDef;
    /** A RTGETOPT_REQ_STRING option argument. */
    const char     *psz;

    /** A RTGETOPT_REQ_INT8 option argument. */
    int8_t          i8;
    /** A RTGETOPT_REQ_UINT8 option argument . */
    uint8_t         u8;
    /** A RTGETOPT_REQ_INT16 option argument. */
    int16_t         i16;
    /** A RTGETOPT_REQ_UINT16 option argument . */
    uint16_t        u16;
    /** A RTGETOPT_REQ_INT16 option argument. */
    int32_t         i32;
    /** A RTGETOPT_REQ_UINT32 option argument . */
    uint32_t        u32;
    /** A RTGETOPT_REQ_INT64 option argument. */
    int64_t         i64;
    /** A RTGETOPT_REQ_UINT64 option argument. */
    uint64_t        u64;
#ifdef IPRT_INCLUDED_net_h
    /** A RTGETOPT_REQ_IPV4ADDR option argument. */
    RTNETADDRIPV4   IPv4Addr;
    /** A RTGETOPT_REQ_IPV4CIDR option argument. */
    struct
    {
        RTNETADDRIPV4 IPv4Network;
        RTNETADDRIPV4 IPv4Netmask;
    } CidrIPv4;
#endif
    /** A RTGETOPT_REQ_MACADDR option argument. */
    RTMAC           MacAddr;
    /** A RTGETOPT_REQ_UUID option argument. */
    RTUUID          Uuid;
    /** A boolean flag. */
    bool            f;
    /** A RTGETOPT_REQ_UINT32_PAIR or RTGETOPT_REQ_UINT32_OPTIONAL_PAIR option
     *  argument. */
    struct
    {
        uint32_t    uFirst;
        uint32_t    uSecond; /**< Set to UINT32_MAX if optional and not present. */
    } PairU32;
    /** A RTGETOPT_REQ_UINT64_COLON_PAIR option argument. */
    struct
    {
        uint64_t    uFirst;
        uint64_t    uSecond; /**< Set to UINT64_MAX if optional and not present. */
    } PairU64;
} RTGETOPTUNION;
/** Pointer to an option argument union. */
typedef RTGETOPTUNION *PRTGETOPTUNION;
/** Pointer to a const option argument union. */
typedef RTGETOPTUNION const *PCRTGETOPTUNION;


/**
 * RTGetOpt state.
 */
typedef struct RTGETOPTSTATE
{
    /** The next argument. */
    int             iNext;
    /** Argument array. */
    char          **argv;
    /** Number of items in argv. */
    int             argc;
    /** Option definition array. */
    PCRTGETOPTDEF   paOptions;
    /** Number of items in paOptions. */
    size_t          cOptions;
    /** The next short option.
     * (For parsing ls -latrT4 kind of option lists.) */
    const char     *pszNextShort;
    /** The option definition which matched. NULL otherwise. */
    PCRTGETOPTDEF   pDef;
    /** The index of an index option, otherwise UINT32_MAX. */
    uint32_t        uIndex;
    /** The flags passed to RTGetOptInit.  */
    uint32_t        fFlags;
    /** Number of non-options that we're skipping during a sorted get.  The value
     * INT32_MAX is used to indicate that there are no more options.  This is used
     * to implement '--'.   */
    int32_t         cNonOptions;

    /* More members may be added later for dealing with new features. */
} RTGETOPTSTATE;
/** Pointer to RTGetOpt state. */
typedef RTGETOPTSTATE *PRTGETOPTSTATE;


/**
 * Initialize the RTGetOpt state.
 *
 * The passed in argument vector may be sorted if fFlags indicates that this is
 * desired (to be implemented).
 *
 * @returns VINF_SUCCESS, VERR_INVALID_PARAMETER or VERR_INVALID_POINTER.
 * @param   pState      The state.
 *
 * @param   argc        Argument count, to be copied from what comes in with
 *                      main().
 * @param   argv        Argument array, to be copied from what comes in with
 *                      main(). This may end up being modified by the
 *                      option/argument sorting.
 * @param   paOptions   Array of RTGETOPTDEF structures, which must specify what
 *                      options are understood by the program.
 * @param   cOptions    Number of array items passed in with paOptions.
 * @param   iFirst      The argument to start with (in argv).
 * @param   fFlags      The flags, see RTGETOPTINIT_FLAGS_XXX.
 */
RTDECL(int) RTGetOptInit(PRTGETOPTSTATE pState, int argc, char **argv,
                         PCRTGETOPTDEF paOptions, size_t cOptions,
                         int iFirst, uint32_t fFlags);

/** @name RTGetOptInit flags.
 * @{ */
/** Sort the arguments so that options comes first, then non-options. */
#define RTGETOPTINIT_FLAGS_OPTS_FIRST   RT_BIT_32(0)
/** Prevent add the standard version and help options:
 *     - "--help", "-h" and "-?" returns 'h'.
 *     - "--version" and "-V" return 'V'.
 */
#define RTGETOPTINIT_FLAGS_NO_STD_OPTS  RT_BIT_32(1)
/** @} */

/**
 * Command line argument parser, handling both long and short options and checking
 * argument formats, if desired.
 *
 * This is to be called in a loop until it returns 0 (meaning that all options
 * were parsed) or a negative value (meaning that an error occurred). How non-option
 * arguments are dealt with depends on the flags passed to RTGetOptInit. The default
 * (fFlags = 0) is to return VINF_GETOPT_NOT_OPTION with pValueUnion->psz pointing to
 * the argument string.
 *
 * For example, for a program which takes the following options:
 *
 *   --optwithstring (or -s) and a string argument;
 *   --optwithint (or -i) and a 32-bit signed integer argument;
 *   --verbose (or -v) with no arguments,
 *
 * code would look something like this:
 *
 * @code
int main(int argc, char **argv)
{
     int rc = RTR3Init();
     if (RT_FAILURE(rc))
         return RTMsgInitFailure(rc);

     static const RTGETOPTDEF s_aOptions[] =
     {
         { "--optwithstring",    's', RTGETOPT_REQ_STRING },
         { "--optwithint",       'i', RTGETOPT_REQ_INT32 },
         { "--verbose",          'v', 0 },
     };

     int ch;
     RTGETOPTUNION ValueUnion;
     RTGETOPTSTATE GetState;
     RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
     while ((ch = RTGetOpt(&GetState, &ValueUnion)))
     {
         // for options that require an argument, ValueUnion has received the value
         switch (ch)
         {
             case 's': // --optwithstring or -s
                 // string argument, copy ValueUnion.psz
                 break;

             case 'i': // --optwithint or -i
                 // integer argument, copy ValueUnion.i32
                 break;

             case 'v': // --verbose or -v
                 g_fOptVerbose = true;
                 break;

             case VINF_GETOPT_NOT_OPTION:
                 // handle non-option argument in ValueUnion.psz.
                 break;

             default:
                 return RTGetOptPrintError(ch, &ValueUnion);
         }
     }

     return RTEXITCODE_SUCCESS;
}
   @endcode
 *
 * @returns 0 when done parsing.
 * @returns the iShort value of the option. pState->pDef points to the option
 *          definition which matched.
 * @returns IPRT error status on parse error.
 * @returns VINF_GETOPT_NOT_OPTION when encountering a non-option argument and
 *          RTGETOPTINIT_FLAGS_OPTS_FIRST was not specified. pValueUnion->psz
 *          points to the argument string.
 * @returns VERR_GETOPT_UNKNOWN_OPTION when encountering an unknown option.
 *          pValueUnion->psz points to the option string.
 * @returns VERR_GETOPT_REQUIRED_ARGUMENT_MISSING and pValueUnion->pDef if
 *          a required argument (aka value) was missing for an option.
 * @returns VERR_GETOPT_INVALID_ARGUMENT_FORMAT and pValueUnion->pDef if
 *          argument (aka value) conversion failed.
 *
 * @param   pState      The state previously initialized with RTGetOptInit.
 * @param   pValueUnion Union with value; in the event of an error, psz member
 *                      points to erroneous parameter; otherwise, for options
 *                      that require an argument, this contains the value of
 *                      that argument, depending on the type that is required.
 */
RTDECL(int) RTGetOpt(PRTGETOPTSTATE pState, PRTGETOPTUNION pValueUnion);

/**
 * Variant of RTGetOpt that allows extra option arrays to be specified.
 *
 * @returns See RTGetOpt.
 * @param   pState          The state previously initialized with RTGetOptInit.
 * @param   pValueUnion     Union with value; in the event of an error, psz
 *                          member points to erroneous parameter; otherwise, for
 *                          options that require an argument, this contains the
 *                          value of that argument, depending on the type that
 *                          is required.
 * @param   paExtraOptions  Pointer to the array of extra options. Optional.
 * @param   cExtraOptions   Number of options in @a paExtraOptions. Must be zero
 *                          if @a paExtraOptions is NULL.
 */
RTDECL(int) RTGetOptEx(PRTGETOPTSTATE pState, PRTGETOPTUNION pValueUnion, PCRTGETOPTDEF paExtraOptions, size_t cExtraOptions);

/**
 * Fetch a value.
 *
 * Used to retrive a value argument in a manner similar to what RTGetOpt does
 * (@a fFlags -> @a pValueUnion).  This can be used when handling
 * VINF_GETOPT_NOT_OPTION, but is equally useful for decoding options that
 * takes more than one value.
 *
 * @returns VINF_SUCCESS on success.
 * @returns IPRT error status on parse error.
 * @returns VERR_INVALID_PARAMETER if the flags are wrong.
 * @returns VERR_GETOPT_UNKNOWN_OPTION when pState->pDef is null.
 * @returns VERR_GETOPT_REQUIRED_ARGUMENT_MISSING if there are no more
 *          available arguments. pValueUnion->pDef is NULL.
 * @returns VERR_GETOPT_INVALID_ARGUMENT_FORMAT and pValueUnion->pDef is
 *          unchanged if value conversion failed.
 *
 * @param   pState      The state previously initialized with RTGetOptInit.
 * @param   pValueUnion Union with value; in the event of an error, psz member
 *                      points to erroneous parameter; otherwise, for options
 *                      that require an argument, this contains the value of
 *                      that argument, depending on the type that is required.
 * @param   fFlags      What to get, that is RTGETOPT_REQ_XXX.
 */
RTDECL(int) RTGetOptFetchValue(PRTGETOPTSTATE pState, PRTGETOPTUNION pValueUnion, uint32_t fFlags);

/**
 * Gets the pointer to the argv entry of the current non-option argument.
 *
 * This function ASSUMES the previous RTGetOpt() call returned
 * VINF_GETOPT_NOT_OPTION and require RTGETOPTINIT_FLAGS_OPTS_FIRST to be
 * specified to RTGetOptInit().
 *
 * @returns Pointer to the argv entry of the current non-option.  NULL if
 *          (detectable) precondition isn't fullfilled (asserted)
 * @param   pState      The state previously initialized with RTGetOptInit.
 */
RTDECL(char **) RTGetOptNonOptionArrayPtr(PRTGETOPTSTATE pState);


/**
 * Processes @a pszValue using @a fFlags, storing the result in @a pValueUnion.
 *
 * @returns IPRT status code.  May return other error statuses than the ones
 *          listed, in case of parse errors.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_PARAMETER if the flags are wrong.
 * @retval  VERR_GETOPT_INVALID_ARGUMENT_FORMAT (pValueUnion->pDef is NULL).
 * @param   pszValue    The value to parse according to @a fFlags.
 * @param   fFlags      How to treat pszValue, that is RTGETOPT_REQ_XXX.
 * @param   pValueUnion Union with the converted value.  In the event of an
 *                      error, this will either be set to pszValue or NULL.
 */
RTDECL(int) RTGetOptStringToValue(const char *pszValue, uint32_t fFlags, PRTGETOPTUNION pValueUnion);

/**
 * Print error messages for a RTGetOpt default case.
 *
 * Uses RTMsgError.
 *
 * @returns Suitable exit code.
 *
 * @param   ch          The RTGetOpt return value.
 * @param   pValueUnion The value union returned by RTGetOpt.
 */
RTDECL(RTEXITCODE) RTGetOptPrintError(int ch, PCRTGETOPTUNION pValueUnion);

/**
 * Formats error messages for a RTGetOpt default case.
 *
 * @returns On success, positive count of formatted character excluding the
 *          terminator.  On buffer overflow, negative number giving the required
 *          buffer size (including terminator char).  (RTStrPrintf2 style.)
 *
 * @param   pszBuf      The buffer to format into.
 * @param   cbBuf       The size of the buffer @a pszBuf points to.
 * @param   ch          The RTGetOpt return value.
 * @param   pValueUnion The value union returned by RTGetOpt.
 */
RTDECL(ssize_t) RTGetOptFormatError(char *pszBuf, size_t cbBuf, int ch, PCRTGETOPTUNION pValueUnion);

/**
 * Parses the @a pszCmdLine string into an argv array.
 *
 * This is useful for converting a response file or similar to an argument
 * vector that can be used with RTGetOptInit().
 *
 * This function aims at following the bourne shell string quoting rules.
 *
 * @returns IPRT status code.
 *
 * @param   ppapszArgv      Where to return the argument vector.  This must be
 *                          freed by calling RTGetOptArgvFreeEx or
 *                          RTGetOptArgvFree.
 * @param   pcArgs          Where to return the argument count.
 * @param   pszCmdLine      The string to parse.
 * @param   fFlags          A combination of the RTGETOPTARGV_CNV_XXX flags,
 *                          except RTGETOPTARGV_CNV_UNQUOTED is not supported.
 * @param   pszSeparators   String containing the argument separators. If NULL,
 *                          then space, tab, line feed (\\n) and return (\\r)
 *                          are used.
 */
RTDECL(int) RTGetOptArgvFromString(char ***ppapszArgv, int *pcArgs, const char *pszCmdLine, uint32_t fFlags,
                                   const char *pszSeparators);

/**
 * Frees and argument vector returned by RTGetOptStringToArgv.
 *
 * @param   papszArgv       Argument vector.  NULL is fine.
 */
RTDECL(void) RTGetOptArgvFree(char **papszArgv);

/**
 * Frees and argument vector returned by RTGetOptStringToArgv, taking
 * RTGETOPTARGV_CNV_MODIFY_INPUT into account.
 *
 * @param   papszArgv       Argument vector.  NULL is fine.
 * @param   fFlags          The flags passed to RTGetOptStringToArgv.
 */
RTDECL(void) RTGetOptArgvFreeEx(char **papszArgv, uint32_t fFlags);

/**
 * Turns an argv array into a command line string.
 *
 * This is useful for calling CreateProcess on Windows, but can also be used for
 * displaying an argv array.
 *
 * This function aims at following the bourn shell string quoting rules.
 *
 * @returns IPRT status code.
 *
 * @param   ppszCmdLine     Where to return the command line string.  This must
 *                          be freed by calling RTStrFree.
 * @param   papszArgv       The argument vector to convert.
 * @param   fFlags          A combination of the RTGETOPTARGV_CNV_XXX flags.
 */
RTDECL(int) RTGetOptArgvToString(char **ppszCmdLine, const char * const *papszArgv, uint32_t fFlags);

/** @name RTGetOptArgvToString, RTGetOptArgvToUtf16String and
 *        RTGetOptArgvFromString flags
 * @{ */
/** Quote strings according to the Microsoft CRT rules. */
#define RTGETOPTARGV_CNV_QUOTE_MS_CRT       UINT32_C(0x00000000)
/** Quote strings according to the Unix Bourne Shell. */
#define RTGETOPTARGV_CNV_QUOTE_BOURNE_SH    UINT32_C(0x00000001)
/** Don't quote any strings at all. */
#define RTGETOPTARGV_CNV_UNQUOTED           UINT32_C(0x00000002)
/** Mask for the quoting style. */
#define RTGETOPTARGV_CNV_QUOTE_MASK         UINT32_C(0x00000003)
/** Allow RTGetOptArgvFromString to modifying the command line input string.
 * @note Must use RTGetOptArgvFreeEx to free. */
#define RTGETOPTARGV_CNV_MODIFY_INPUT       UINT32_C(0x00000004)
/** Valid bits. */
#define RTGETOPTARGV_CNV_VALID_MASK         UINT32_C(0x00000007)
/** @} */

/**
 * Convenience wrapper around RTGetOpArgvToString and RTStrToUtf16.
 *
 * @returns IPRT status code.
 *
 * @param   ppwszCmdLine    Where to return the command line string.  This must
 *                          be freed by calling RTUtf16Free.
 * @param   papszArgv       The argument vector to convert.
 * @param   fFlags          A combination of the RTGETOPTARGV_CNV_XXX flags.
 */
RTDECL(int) RTGetOptArgvToUtf16String(PRTUTF16 *ppwszCmdLine, const char * const *papszArgv, uint32_t fFlags);

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_getopt_h */

