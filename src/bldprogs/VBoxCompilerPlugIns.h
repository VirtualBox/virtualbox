/* $Id: VBoxCompilerPlugIns.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCompilerPlugIns - Types, Prototypes and Macros common to the VBox compiler plug-ins.
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

#ifndef VBOX_INCLUDED_SRC_bldprogs_VBoxCompilerPlugIns_h
#define VBOX_INCLUDED_SRC_bldprogs_VBoxCompilerPlugIns_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <stdio.h>


/** @def dprintf
 * Macro for debug printing using fprintf.  Only active when DEBUG is defined.
 */
#ifdef DEBUG
# define dprintf(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
# define dprintf(...) do { } while (0)
#endif


/**
 * Checker state.
 */
typedef struct VFMTCHKSTATE
{
    long            iFmt;           /**< The format argument number from the attribute. */
    long            iArgs;          /**< The arguments argument number from the attribute. */
    bool            fMaybeNull;     /**< Set if in iprt_format_maybe_null mode. */
    const char     *pszFmt;         /**< The format string being validated, if a constant. */

#if defined(__GNUC__) && !defined(VBOX_COMPILER_PLUG_IN_AGNOSTIC)
    /** @name GCC specifics
     * @{ */
# if RT_GNUC_PREREQ(6, 0)
    gimple const   *hStmt;
# else
    gimple          hStmt;
# endif
    location_t      hFmtLoc; /**< The statement or format-string location */
    /** @} */
#endif
} VFMTCHKSTATE;
/** Pointer to my checker state. */
typedef VFMTCHKSTATE *PVFMTCHKSTATE;

#define MYSTATE_FMT_FILE(a_pState)      VFmtChkGetFmtLocFile(a_pState)
#define MYSTATE_FMT_LINE(a_pState)      VFmtChkGetFmtLocLine(a_pState)
#define MYSTATE_FMT_COLUMN(a_pState)    VFmtChkGetFmtLocColumn(a_pState)

const char     *VFmtChkGetFmtLocFile(PVFMTCHKSTATE pState);

unsigned int    VFmtChkGetFmtLocLine(PVFMTCHKSTATE pState);

unsigned int    VFmtChkGetFmtLocColumn(PVFMTCHKSTATE pState);

/**
 * Implements checking format string replacement (%M).
 *
 * Caller will have checked all that can be checked.  This means that there is a
 * string argument present, or it won't make the call.
 *
 * @param   pState              The format string checking state.
 * @param   pszPctM             The position of the '%M'.
 * @param   iArg                The next argument number.
 */
void            VFmtChkHandleReplacementFormatString(PVFMTCHKSTATE pState, const char *pszPctM, unsigned iArg);

/**
 * Warning.
 *
 * @returns
 * @param   pState              .
 * @param   pszLoc              .
 * @param   pszFormat           .
 * @param   ...                 .
 */
void            VFmtChkWarnFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...);

/**
 * Error.
 *
 * @returns
 * @param   pState              .
 * @param   pszLoc              .
 * @param   pszFormat           .
 * @param   ...                 .
 */
void            VFmtChkErrFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...);

/**
 * Checks that @a iFmtArg isn't present or a valid final dummy argument.
 *
 * Will issue warning/error if there are more arguments at @a iFmtArg.
 *
 * @param   pState              The format string checking state.
 * @param   iArg                The index of the end of arguments, this is
 *                              relative to VFMTCHKSTATE::iArgs.
 */
void            VFmtChkVerifyEndOfArgs(PVFMTCHKSTATE pState, unsigned iArg);

bool            VFmtChkRequirePresentArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireIntArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireStringArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireVaListPtrArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

/* VBoxCompilerPlugInsCommon.cpp */
void            MyCheckFormatCString(PVFMTCHKSTATE pState, const char *pszFmt);


#endif /* !VBOX_INCLUDED_SRC_bldprogs_VBoxCompilerPlugIns_h */

