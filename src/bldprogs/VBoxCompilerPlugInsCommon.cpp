/* $Id: VBoxCompilerPlugInsCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCompilerPlugInsCommon - Code common to the compiler plug-ins.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_COMPILER_PLUG_IN_AGNOSTIC
#include "VBoxCompilerPlugIns.h"

#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MY_ISDIGIT(c) ((c) >= '0' && (c) <= '9')

/** @name RTSTR_Z_XXX - Size modifiers
 * @{ */
#define RTSTR_Z_DEFAULT         UINT16_C(0x0001)
#define RTSTR_Z_LONG            UINT16_C(0x0002) /**< l */
#define RTSTR_Z_LONGLONG        UINT16_C(0x0004) /**< ll, L, q. */
#define RTSTR_Z_HALF            UINT16_C(0x0008) /**< h */
#define RTSTR_Z_HALFHALF        UINT16_C(0x0010) /**< hh (internally H) */
#define RTSTR_Z_SIZE            UINT16_C(0x0020) /**< z */
#define RTSTR_Z_PTRDIFF         UINT16_C(0x0040) /**< t */
#define RTSTR_Z_INTMAX          UINT16_C(0x0080) /**< j */
#define RTSTR_Z_MS_I32          UINT16_C(0x1000) /**< I32 */
#define RTSTR_Z_MS_I64          UINT16_C(0x2000) /**< I64 */
#define RTSTR_Z_ALL_INT         UINT16_C(0x30fe) /**< short hand for integers. */
/** @} */


/** @name VFMTCHKTYPE_F_XXX - Type flags.
 * @{ */
/** Pointers type. */
#define VFMTCHKTYPE_F_PTR       UINT8_C(0x01)
/** Both const and non-const pointer types. */
#define VFMTCHKTYPE_F_CPTR      (UINT8_C(0x02) | VFMTCHKTYPE_F_PTR)
/** @} */

/** @name VFMTCHKTYPE_Z_XXX - Special type sizes
 * @{ */
#define VFMTCHKTYPE_Z_CHAR       UINT8_C(0xe0)
#define VFMTCHKTYPE_Z_SHORT      UINT8_C(0xe1)
#define VFMTCHKTYPE_Z_INT        UINT8_C(0xe2)
#define VFMTCHKTYPE_Z_LONG       UINT8_C(0xe3)
#define VFMTCHKTYPE_Z_LONGLONG   UINT8_C(0xe4)
#define VFMTCHKTYPE_Z_PTR        UINT8_C(0xe5) /**< ASSUMED to be the same for 'void *', 'size_t' and 'ptrdiff_t'. */
/** @} */

/** @name VFMTCHKTYPE_NM_XXX - Standard C type names.
 * @{ */
#define VFMTCHKTYPE_NM_INT          "int"
#define VFMTCHKTYPE_NM_UINT         "unsigned int"
#define VFMTCHKTYPE_NM_LONG         "long"
#define VFMTCHKTYPE_NM_ULONG        "unsigned long"
#define VFMTCHKTYPE_NM_LONGLONG     "long long"
#define VFMTCHKTYPE_NM_ULONGLONG    "unsigned long long"
#define VFMTCHKTYPE_NM_SHORT        "short"
#define VFMTCHKTYPE_NM_USHORT       "unsigned short"
#define VFMTCHKTYPE_NM_CHAR         "char"
#define VFMTCHKTYPE_NM_SCHAR        "signed char"
#define VFMTCHKTYPE_NM_UCHAR        "unsigned char"
/** @} */


/** @name VFMTCHKDESC_F_XXX - Format descriptor flags.
 * @{ */
#define VFMTCHKDESC_F_NONE          UINT32_C(0)
#define VFMTCHKDESC_F_SIGNED        RT_BIT_32(0)
#define VFMTCHKDESC_F_UNSIGNED      RT_BIT_32(1)
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Format check type entry.
 */
typedef struct VFMTCHKTYPE
{
    /** The format size flag(s). */
    uint16_t        fSize;
    /** The argument size. */
    uint8_t         cbArg;
    /** Argument flags (VFMTCHKTYPE_F_XXX). */
    uint8_t         fFlags;
    /** List of strings with acceptable types, if NULL only check the sizes. */
    const char      *pszzTypeNames;
} VFMTCHKTYPE;
/** Pointer to a read only format check type entry. */
typedef VFMTCHKTYPE const *PCVFMTCHKTYPE;

/** For use as an initializer in VFMTCHKDESK where it indicates that
 * everything is covered by VFMTCHKDESC::paMoreTypes.   Useful for repeating
 * stuff. */
#define VFMTCHKTYPE_USE_MORE_TYPES  { 0, 0, 0, NULL }

/**
 * Format type descriptor.
 */
typedef struct VFMTCHKDESC
{
    /** The format type. */
    const char     *pszType;
    /** Recognized format flags (RTSTR_F_XXX).  */
    uint16_t        fFmtFlags;
    /** Recognized format sizes (RTSTR_Z_XXX). */
    uint16_t        fFmtSize;
    /** Flags (VFMTCHKDESC_F_XXX).  */
    uint32_t        fFlags;
    /** Primary type. */
    VFMTCHKTYPE     Type;
    /** More recognized types (optional).   */
    PCVFMTCHKTYPE   paMoreTypes;
} VFMTCHKDESC;
typedef VFMTCHKDESC const *PCVFMTCHKDESC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Integer type specs for 'x', 'd', 'u', 'i', ++
 *
 * @todo RTUINT32U and friends...  The whole type matching thing.
 */
static VFMTCHKTYPE const g_aIntTypes[] =
{
    {   RTSTR_Z_DEFAULT,        VFMTCHKTYPE_Z_INT,      0, VFMTCHKTYPE_NM_INT "\0"      VFMTCHKTYPE_NM_UINT "\0" },
    {   RTSTR_Z_LONG,           VFMTCHKTYPE_Z_LONG,     0, VFMTCHKTYPE_NM_LONG "\0"     VFMTCHKTYPE_NM_ULONG "\0" },
    {   RTSTR_Z_LONGLONG,       VFMTCHKTYPE_Z_LONGLONG, 0, VFMTCHKTYPE_NM_LONGLONG "\0" VFMTCHKTYPE_NM_ULONGLONG "\0" },
    {   RTSTR_Z_HALF,           VFMTCHKTYPE_Z_SHORT,    0, VFMTCHKTYPE_NM_SHORT "\0"    VFMTCHKTYPE_NM_USHORT "\0" },
    {   RTSTR_Z_HALFHALF,       VFMTCHKTYPE_Z_CHAR,     0, VFMTCHKTYPE_NM_SCHAR "\0"    VFMTCHKTYPE_NM_UCHAR "\0" VFMTCHKTYPE_NM_CHAR "\0" },
    {   RTSTR_Z_SIZE,           VFMTCHKTYPE_Z_PTR,      0, "size_t\0"    "RTUINTPTR\0"  "RTINTPTR\0" },
    {   RTSTR_Z_PTRDIFF,        VFMTCHKTYPE_Z_PTR,      0, "ptrdiff_t\0" "RTUINTPTR\0"  "RTINTPTR\0" },
    {   RTSTR_Z_INTMAX,         VFMTCHKTYPE_Z_PTR,      0, "uint64_t\0" "int64_t\0" "RTUINT64U\0" VFMTCHKTYPE_NM_LONGLONG "\0" VFMTCHKTYPE_NM_ULONGLONG "\0" },
    {   RTSTR_Z_MS_I32,         sizeof(uint32_t),       0, "uint32_t\0" "int32_t\0" "RTUINT32U\0" },
    {   RTSTR_Z_MS_I64,         sizeof(uint64_t),       0, "uint64_t\0" "int64_t\0" "RTUINT64U\0" },
};

/** String type specs for 's', 'ls' and 'Ls'.
 */
static VFMTCHKTYPE const g_aStringTypes[] =
{
    {   RTSTR_Z_DEFAULT,        VFMTCHKTYPE_Z_PTR,      VFMTCHKTYPE_F_CPTR, VFMTCHKTYPE_NM_CHAR "\0" },
    {   RTSTR_Z_LONG,           VFMTCHKTYPE_Z_PTR,      VFMTCHKTYPE_F_CPTR, "RTUTF16\0" },
    {   RTSTR_Z_LONGLONG,       VFMTCHKTYPE_Z_PTR,      VFMTCHKTYPE_F_CPTR, "RTUNICP\0" },
};

static VFMTCHKDESC const g_aFmtDescs[] =
{
    {   "s",
        RTSTR_F_LEFT | RTSTR_F_WIDTH | RTSTR_F_PRECISION,
        RTSTR_Z_DEFAULT | RTSTR_Z_LONG | RTSTR_Z_LONGLONG,
        VFMTCHKDESC_F_UNSIGNED,
        VFMTCHKTYPE_USE_MORE_TYPES,
        g_aStringTypes
    },
    {   "x",
        RTSTR_F_LEFT | RTSTR_F_ZEROPAD | RTSTR_F_SPECIAL | RTSTR_F_WIDTH | RTSTR_F_PRECISION,
        RTSTR_Z_ALL_INT,
        VFMTCHKDESC_F_UNSIGNED,
        VFMTCHKTYPE_USE_MORE_TYPES,
        g_aIntTypes
    },
    {   "RX32",
        RTSTR_F_LEFT | RTSTR_F_ZEROPAD | RTSTR_F_SPECIAL | RTSTR_F_WIDTH | RTSTR_F_PRECISION,
        RTSTR_Z_ALL_INT,
        VFMTCHKDESC_F_UNSIGNED,
        { RTSTR_Z_DEFAULT,      sizeof(uint32_t),       0, "uint32_t\0" "int32_t\0" },
        NULL
    },



};


/**
 * Does the actual format string checking.
 *
 * @todo    Move this to different file common to both GCC and CLANG later.
 *
 * @param   pState              The format string checking state.
 * @param   pszFmt              The format string.
 */
void MyCheckFormatCString(PVFMTCHKSTATE pState, const char *pszFmt)
{
    dprintf("checker2: \"%s\" at %s:%d col %d\n", pszFmt,
            MYSTATE_FMT_FILE(pState), MYSTATE_FMT_LINE(pState), MYSTATE_FMT_COLUMN(pState));
    pState->pszFmt = pszFmt;

    unsigned iArg = 0;
    for (;;)
    {
        /*
         * Skip to the next argument.
         * Quits the loop with the first char following the '%' in 'ch'.
         */
        char ch;
        for (;;)
        {
            ch = *pszFmt++;
            if (ch == '%')
            {
                ch = *pszFmt++;
                if (ch != '%')
                    break;
            }
            else if (ch == '\0')
            {
                VFmtChkVerifyEndOfArgs(pState, iArg);
                return;
            }
        }
        const char * const pszPct = pszFmt - 2;

        /*
         * Flags
         */
        uint32_t fFmtFlags = 0;
        for (;;)
        {
            uint32_t fFlag;
            switch (ch)
            {
                case '#':   fFlag = RTSTR_F_SPECIAL;      break;
                case '-':   fFlag = RTSTR_F_LEFT;         break;
                case '+':   fFlag = RTSTR_F_PLUS;         break;
                case ' ':   fFlag = RTSTR_F_BLANK;        break;
                case '0':   fFlag = RTSTR_F_ZEROPAD;      break;
                case '\'':  fFlag = RTSTR_F_THOUSAND_SEP; break;
                default:    fFlag = 0;                    break;
            }
            if (!fFlag)
                break;
            if (fFmtFlags & fFlag)
                VFmtChkWarnFmt(pState, pszPct, "duplicate flag '%c'", ch);
            fFmtFlags |= fFlag;
            ch = *pszFmt++;
        }

        /*
         * Width.
         */
        int cchWidth = -1;
        if (MY_ISDIGIT(ch))
        {
            cchWidth = ch - '0';
            while (   (ch = *pszFmt++) != '\0'
                   && MY_ISDIGIT(ch))
            {
                cchWidth *= 10;
                cchWidth += ch - '0';
            }
            fFmtFlags |= RTSTR_F_WIDTH;
        }
        else if (ch == '*')
        {
            VFmtChkRequireIntArg(pState, pszPct, iArg, "width should be an 'int' sized argument");
            iArg++;
            cchWidth = 0;
            fFmtFlags |= RTSTR_F_WIDTH;
            ch = *pszFmt++;
        }

        /*
         * Precision
         */
        int cchPrecision = -1;
        if (ch == '.')
        {
            ch = *pszFmt++;
            if (MY_ISDIGIT(ch))
            {
                cchPrecision = ch - '0';
                while (   (ch = *pszFmt++) != '\0'
                       && MY_ISDIGIT(ch))
                {
                    cchPrecision *= 10;
                    cchPrecision += ch - '0';
                }
            }
            else if (ch == '*')
            {
                VFmtChkRequireIntArg(pState, pszPct, iArg, "precision should be an 'int' sized argument");
                iArg++;
                cchPrecision = 0;
                ch = *pszFmt++;
            }
            else
                VFmtChkErrFmt(pState, pszPct, "Missing precision value, only got the '.'");
            if (cchPrecision < 0)
            {
                VFmtChkErrFmt(pState, pszPct, "Negative precision value: %d", cchPrecision);
                cchPrecision = 0;
            }
            fFmtFlags |= RTSTR_F_PRECISION;
        }

        /*
         * Argument size.
         */
        uint16_t fFmtSize = RTSTR_Z_DEFAULT;
        switch (ch)
        {
            default:
                fFmtSize = RTSTR_Z_DEFAULT;
                break;

            case 'z':
                fFmtSize = RTSTR_Z_SIZE;
                ch = *pszFmt++;
                break;
            case 'j':
                fFmtSize = RTSTR_Z_INTMAX;
                ch = *pszFmt++;
                break;
            case 't':
                fFmtSize = RTSTR_Z_PTRDIFF;
                ch = *pszFmt++;
                break;

            case 'l':
                fFmtSize = RTSTR_Z_LONG;
                ch = *pszFmt++;
                if (ch == 'l')
                {
                    fFmtSize = RTSTR_Z_LONGLONG;
                    ch = *pszFmt++;
                }
                break;

            case 'q': /* Used on BSD platforms. */
            case 'L':
                fFmtSize = RTSTR_Z_LONGLONG;
                ch = *pszFmt++;
                break;

            case 'h':
                fFmtSize = RTSTR_Z_HALF;
                ch = *pszFmt++;
                if (ch == 'h')
                {
                    fFmtSize = RTSTR_Z_HALFHALF;
                    ch = *pszFmt++;
                }
                break;

            case 'I': /* Used by Win32/64 compilers. */
                if (   pszFmt[0] == '6'
                    && pszFmt[1] == '4')
                {
                    pszFmt += 2;
                    fFmtSize = RTSTR_Z_MS_I64;
                }
                else if (   pszFmt[0] == '3'
                         && pszFmt[1] == '2')
                {
                    pszFmt += 2;
                    fFmtSize = RTSTR_Z_MS_I32;
                }
                else
                {
                    VFmtChkErrFmt(pState, pszFmt, "Unknow format type/size/flag 'I%c'", pszFmt[0]);
                    fFmtSize = RTSTR_Z_INTMAX;
                }
                ch = *pszFmt++;
                break;
        }

        /*
         * The type.
         */
        switch (ch)
        {
            /*
             * Nested extensions.
             */
            case 'M': /* replace the format string (not stacked yet). */
            {
                if (*pszFmt)
                    VFmtChkErrFmt(pState, pszFmt, "Characters following '%%M' will be ignored");
                if (fFmtSize != RTSTR_Z_DEFAULT)
                    VFmtChkWarnFmt(pState, pszFmt, "'%%M' does not support any size flags (%#x)", fFmtSize);
                if (fFmtFlags != 0)
                    VFmtChkWarnFmt(pState, pszFmt, "'%%M' does not support any format flags (%#x)", fFmtFlags);
                if (VFmtChkRequireStringArg(pState, pszPct, iArg, "'%M' expects a format string"))
                    VFmtChkHandleReplacementFormatString(pState, pszPct, iArg);
                return;
            }

            case 'N': /* real nesting. */
            {
                if (fFmtSize != RTSTR_Z_DEFAULT)
                    VFmtChkWarnFmt(pState, pszFmt, "'%%N' does not support any size flags (%#x)", fFmtSize);
                if (fFmtFlags != 0)
                    VFmtChkWarnFmt(pState, pszFmt, "'%%N' does not support any format flags (%#x)", fFmtFlags);
                VFmtChkRequireStringArg(pState, pszPct, iArg,        "'%N' expects a string followed by a va_list pointer");
                VFmtChkRequireVaListPtrArg(pState, pszPct, iArg + 1, "'%N' expects a string followed by a va_list pointer");
                iArg += 2;
                break;
            }

            case 'R':
                if (   pszFmt[0] == 'h'
                    && pszFmt[1] == 'X')
                {
                    VFmtChkRequirePresentArg(pState, pszPct, iArg, "Expected argument");
                    iArg++;
                }
                RT_FALL_THROUGH();

            default:
                VFmtChkRequirePresentArg(pState, pszPct, iArg, "Expected argument");
                iArg++;
                break;
        }
    }
}

