/* $Id: scm.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "scm.h"
#include "scmdiff.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The name of the settings files. */
#define SCM_SETTINGS_FILENAME           ".scm-settings"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Option identifiers.
 *
 * @note    The first chunk, down to SCMOPT_TAB_SIZE, are alternately set &
 *          clear.  So, the option setting a flag (boolean) will have an even
 *          number and the one clearing it will have an odd number.
 * @note    Down to SCMOPT_LAST_SETTINGS corresponds exactly to SCMSETTINGSBASE.
 */
typedef enum SCMOPT
{
    SCMOPT_CONVERT_EOL = 10000,
    SCMOPT_NO_CONVERT_EOL,
    SCMOPT_CONVERT_TABS,
    SCMOPT_NO_CONVERT_TABS,
    SCMOPT_FORCE_FINAL_EOL,
    SCMOPT_NO_FORCE_FINAL_EOL,
    SCMOPT_FORCE_TRAILING_LINE,
    SCMOPT_NO_FORCE_TRAILING_LINE,
    SCMOPT_STRIP_TRAILING_BLANKS,
    SCMOPT_NO_STRIP_TRAILING_BLANKS,
    SCMOPT_STRIP_TRAILING_LINES,
    SCMOPT_NO_STRIP_TRAILING_LINES,
    SCMOPT_FIX_FLOWER_BOX_MARKERS,
    SCMOPT_NO_FIX_FLOWER_BOX_MARKERS,
    SCMOPT_FIX_HEADER_GUARDS,
    SCMOPT_NO_FIX_HEADER_GUARDS,
    SCMOPT_PRAGMA_ONCE,
    SCMOPT_NO_PRAGMA_ONCE,
    SCMOPT_FIX_HEADER_GUARD_ENDIF,
    SCMOPT_NO_FIX_HEADER_GUARD_ENDIF,
    SCMOPT_ENDIF_GUARD_COMMENT,
    SCMOPT_NO_ENDIF_GUARD_COMMENT,
    SCMOPT_GUARD_PREFIX,
    SCMOPT_GUARD_RELATIVE_TO_DIR,
    SCMOPT_FIX_TODOS,
    SCMOPT_NO_FIX_TODOS,
    SCMOPT_FIX_ERR_H,
    SCMOPT_NO_FIX_ERR_H,
    SCMOPT_ONLY_GUEST_HOST_PAGE,
    SCMOPT_NO_ASM_MEM_PAGE_USE,
    SCMOPT_UNRESTRICTED_ASM_MEM_PAGE_USE,
    SCMOPT_NO_PAGE_RESTRICTIONS,
    SCMOPT_NO_RC_USE,
    SCMOPT_UNRESTRICTED_RC_USE,
    SCMOPT_STANDARIZE_KMK,
    SCMOPT_NO_STANDARIZE_KMK,
    SCMOPT_UPDATE_COPYRIGHT_YEAR,
    SCMOPT_NO_UPDATE_COPYRIGHT_YEAR,
    SCMOPT_EXTERNAL_COPYRIGHT,
    SCMOPT_NO_EXTERNAL_COPYRIGHT,
    SCMOPT_NO_UPDATE_LICENSE,
    SCMOPT_LICENSE_OSE_GPL,
    SCMOPT_LICENSE_OSE_DUAL_GPL_CDDL,
    SCMOPT_LICENSE_OSE_CDDL,
    SCMOPT_LICENSE_LGPL,
    SCMOPT_LICENSE_MIT,
    SCMOPT_LICENSE_BASED_ON_MIT,
    SCMOPT_LGPL_DISCLAIMER,
    SCMOPT_NO_LGPL_DISCLAIMER,
    SCMOPT_MIN_BLANK_LINES_BEFORE_FLOWER_BOX_MARKERS,
    SCMOPT_ONLY_SVN_DIRS,
    SCMOPT_NOT_ONLY_SVN_DIRS,
    SCMOPT_ONLY_SVN_FILES,
    SCMOPT_NOT_ONLY_SVN_FILES,
    SCMOPT_SET_SVN_EOL,
    SCMOPT_DONT_SET_SVN_EOL,
    SCMOPT_SET_SVN_EXECUTABLE,
    SCMOPT_DONT_SET_SVN_EXECUTABLE,
    SCMOPT_SET_SVN_KEYWORDS,
    SCMOPT_DONT_SET_SVN_KEYWORDS,
    SCMOPT_SKIP_SVN_SYNC_PROCESS,
    SCMOPT_DONT_SKIP_SVN_SYNC_PROCESS,
    SCMOPT_SKIP_UNICODE_CHECKS,
    SCMOPT_DONT_SKIP_UNICODE_CHECKS,
    SCMOPT_TAB_SIZE,
    SCMOPT_WIDTH,
    SCMOPT_FILTER_OUT_DIRS,
    SCMOPT_FILTER_FILES,
    SCMOPT_FILTER_OUT_FILES,
    SCMOPT_TREAT_AS,
    SCMOPT_ADD_ACTION,
    SCMOPT_DEL_ACTION,
    SCMOPT_LAST_SETTINGS = SCMOPT_DEL_ACTION,
    //
    SCMOPT_CHECK_RUN,
    SCMOPT_DIFF_IGNORE_EOL,
    SCMOPT_DIFF_NO_IGNORE_EOL,
    SCMOPT_DIFF_IGNORE_SPACE,
    SCMOPT_DIFF_NO_IGNORE_SPACE,
    SCMOPT_DIFF_IGNORE_LEADING_SPACE,
    SCMOPT_DIFF_NO_IGNORE_LEADING_SPACE,
    SCMOPT_DIFF_IGNORE_TRAILING_SPACE,
    SCMOPT_DIFF_NO_IGNORE_TRAILING_SPACE,
    SCMOPT_DIFF_SPECIAL_CHARS,
    SCMOPT_DIFF_NO_SPECIAL_CHARS,
    SCMOPT_HELP_CONFIG,
    SCMOPT_HELP_ACTIONS,
    SCMOPT_END
} SCMOPT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
const char          g_szTabSpaces[16+1]     = "                ";
const char          g_szAsterisks[255+1]    =
"****************************************************************************************************"
"****************************************************************************************************"
"*******************************************************";
const char          g_szSpaces[255+1]       =
"                                                                                                    "
"                                                                                                    "
"                                                       ";
static const char   g_szProgName[]          = "scm";
static const char  *g_pszChangedSuff        = "";
static bool         g_fDryRun               = true;
static bool         g_fDiffSpecialChars     = true;
static bool         g_fDiffIgnoreEol        = false;
static bool         g_fDiffIgnoreLeadingWS  = false;
static bool         g_fDiffIgnoreTrailingWS = false;
static int          g_iVerbosity            = 2;//99; //0;
uint32_t            g_uYear                 = 0; /**< The current year. */
/** @name Statistics
 * @{ */
static uint32_t     g_cDirsProcessed        = 0;
static uint32_t     g_cFilesProcessed       = 0;
static uint32_t     g_cFilesModified        = 0;
static uint32_t     g_cFilesSkipped         = 0;
static uint32_t     g_cFilesNotInSvn        = 0;
static uint32_t     g_cFilesNoRewriters     = 0;
static uint32_t     g_cFilesBinaries        = 0;
static uint32_t     g_cFilesRequiringManualFixing = 0;
/** @} */

/** The global settings. */
static SCMSETTINGSBASE const g_Defaults =
{
    /* .fConvertEol = */                            true,
    /* .fConvertTabs = */                           true,
    /* .fForceFinalEol = */                         true,
    /* .fForceTrailingLine = */                     false,
    /* .fStripTrailingBlanks = */                   true,
    /* .fStripTrailingLines = */                    true,
    /* .fFixFlowerBoxMarkers = */                   true,
    /* .cMinBlankLinesBeforeFlowerBoxMakers = */    2,
    /* .fFixHeaderGuards = */                       true,
    /* .fPragmaOnce = */                            true,
    /* .fFixHeaderGuardEndif = */                   true,
    /* .fEndifGuardComment = */                     true,
    /* .pszGuardPrefix = */                         (char *)"VBOX_INCLUDED_SRC_",
    /* .pszGuardRelativeToDir = */                  (char *)"{parent}",
    /* .fFixTodos = */                              true,
    /* .fFixErrH = */                               true,
    /* .fOnlyGuestHostPage = */                     false,
    /* .fNoASMMemPageUse = */                       false,
    /* .fOnlyHrcVrcInsteadOfRc */                   false,
    /* .fStandarizeKmk */                           true,
    /* .fUpdateCopyrightYear = */                   false,
    /* .fExternalCopyright = */                     false,
    /* .fLgplDisclaimer = */                        false,
    /* .enmUpdateLicense = */                       kScmLicense_OseGpl,
    /* .fOnlySvnFiles = */                          false,
    /* .fOnlySvnDirs = */                           false,
    /* .fSetSvnEol = */                             false,
    /* .fSetSvnExecutable = */                      false,
    /* .fSetSvnKeywords = */                        false,
    /* .fSkipSvnSyncProcess = */                    false,
    /* .fSkipUnicodeChecks = */                     false,
    /* .cchTab = */                                 8,
    /* .cchWidth = */                               130,
    /* .fFreeTreatAs = */                           false,
    /* .pTreatAs = */                               NULL,
    /* .pszFilterFiles = */                         (char *)"",
    /* .pszFilterOutFiles = */                      (char *)"*.exe|*.com|20*-*-*.log",
    /* .pszFilterOutDirs = */                       (char *)".svn|.hg|.git|CVS",
};

/** Option definitions for the base settings. */
static RTGETOPTDEF  g_aScmOpts[] =
{
    /* rewriters */
    { "--convert-eol",                      SCMOPT_CONVERT_EOL,                     RTGETOPT_REQ_NOTHING },
    { "--no-convert-eol",                   SCMOPT_NO_CONVERT_EOL,                  RTGETOPT_REQ_NOTHING },
    { "--convert-tabs",                     SCMOPT_CONVERT_TABS,                    RTGETOPT_REQ_NOTHING },
    { "--no-convert-tabs",                  SCMOPT_NO_CONVERT_TABS,                 RTGETOPT_REQ_NOTHING },
    { "--force-final-eol",                  SCMOPT_FORCE_FINAL_EOL,                 RTGETOPT_REQ_NOTHING },
    { "--no-force-final-eol",               SCMOPT_NO_FORCE_FINAL_EOL,              RTGETOPT_REQ_NOTHING },
    { "--force-trailing-line",              SCMOPT_FORCE_TRAILING_LINE,             RTGETOPT_REQ_NOTHING },
    { "--no-force-trailing-line",           SCMOPT_NO_FORCE_TRAILING_LINE,          RTGETOPT_REQ_NOTHING },
    { "--strip-trailing-blanks",            SCMOPT_STRIP_TRAILING_BLANKS,           RTGETOPT_REQ_NOTHING },
    { "--no-strip-trailing-blanks",         SCMOPT_NO_STRIP_TRAILING_BLANKS,        RTGETOPT_REQ_NOTHING },
    { "--strip-trailing-lines",             SCMOPT_STRIP_TRAILING_LINES,            RTGETOPT_REQ_NOTHING },
    { "--strip-no-trailing-lines",          SCMOPT_NO_STRIP_TRAILING_LINES,         RTGETOPT_REQ_NOTHING },
    { "--min-blank-lines-before-flower-box-makers", SCMOPT_MIN_BLANK_LINES_BEFORE_FLOWER_BOX_MARKERS,  RTGETOPT_REQ_UINT8 },
    { "--fix-flower-box-markers",           SCMOPT_FIX_FLOWER_BOX_MARKERS,          RTGETOPT_REQ_NOTHING },
    { "--no-fix-flower-box-markers",        SCMOPT_NO_FIX_FLOWER_BOX_MARKERS,       RTGETOPT_REQ_NOTHING },
    { "--fix-header-guards",                SCMOPT_FIX_HEADER_GUARDS,               RTGETOPT_REQ_NOTHING },
    { "--no-fix-header-guards",             SCMOPT_NO_FIX_HEADER_GUARDS,            RTGETOPT_REQ_NOTHING },
    { "--pragma-once",                      SCMOPT_PRAGMA_ONCE,                     RTGETOPT_REQ_NOTHING },
    { "--no-pragma-once",                   SCMOPT_NO_PRAGMA_ONCE,                  RTGETOPT_REQ_NOTHING },
    { "--fix-header-guard-endif",           SCMOPT_FIX_HEADER_GUARD_ENDIF,          RTGETOPT_REQ_NOTHING },
    { "--no-fix-header-guard-endif",        SCMOPT_NO_FIX_HEADER_GUARD_ENDIF,       RTGETOPT_REQ_NOTHING },
    { "--endif-guard-comment",              SCMOPT_ENDIF_GUARD_COMMENT,             RTGETOPT_REQ_NOTHING },
    { "--no-endif-guard-comment",           SCMOPT_NO_ENDIF_GUARD_COMMENT,          RTGETOPT_REQ_NOTHING },
    { "--guard-prefix",                     SCMOPT_GUARD_PREFIX,                    RTGETOPT_REQ_STRING },
    { "--guard-relative-to-dir",            SCMOPT_GUARD_RELATIVE_TO_DIR,           RTGETOPT_REQ_STRING },
    { "--fix-todos",                        SCMOPT_FIX_TODOS,                       RTGETOPT_REQ_NOTHING },
    { "--no-fix-todos",                     SCMOPT_NO_FIX_TODOS,                    RTGETOPT_REQ_NOTHING },
    { "--fix-err-h",                        SCMOPT_FIX_ERR_H,                       RTGETOPT_REQ_NOTHING },
    { "--no-fix-err-h",                     SCMOPT_NO_FIX_ERR_H,                    RTGETOPT_REQ_NOTHING },
    { "--only-guest-host-page",             SCMOPT_ONLY_GUEST_HOST_PAGE,            RTGETOPT_REQ_NOTHING },
    { "--no-page-restrictions",             SCMOPT_NO_PAGE_RESTRICTIONS,            RTGETOPT_REQ_NOTHING },
    { "--no-ASMMemPage-use",                SCMOPT_NO_ASM_MEM_PAGE_USE,             RTGETOPT_REQ_NOTHING },
    { "--unrestricted-ASMMemPage-use",      SCMOPT_UNRESTRICTED_ASM_MEM_PAGE_USE,   RTGETOPT_REQ_NOTHING },
    { "--no-rc-use",                        SCMOPT_NO_RC_USE,                       RTGETOPT_REQ_NOTHING },
    { "--unrestricted-rc-use",              SCMOPT_UNRESTRICTED_RC_USE,             RTGETOPT_REQ_NOTHING },
    { "--standarize-kmk",                   SCMOPT_STANDARIZE_KMK,                  RTGETOPT_REQ_NOTHING },
    { "--no-standarize-kmk",                SCMOPT_NO_STANDARIZE_KMK,               RTGETOPT_REQ_NOTHING },
    { "--update-copyright-year",            SCMOPT_UPDATE_COPYRIGHT_YEAR,           RTGETOPT_REQ_NOTHING },
    { "--no-update-copyright-year",         SCMOPT_NO_UPDATE_COPYRIGHT_YEAR,        RTGETOPT_REQ_NOTHING },
    { "--external-copyright",               SCMOPT_EXTERNAL_COPYRIGHT,              RTGETOPT_REQ_NOTHING },
    { "--no-external-copyright",            SCMOPT_NO_EXTERNAL_COPYRIGHT,           RTGETOPT_REQ_NOTHING },
    { "--no-update-license",                SCMOPT_NO_UPDATE_LICENSE,               RTGETOPT_REQ_NOTHING },
    { "--license-ose-gpl",                  SCMOPT_LICENSE_OSE_GPL,                 RTGETOPT_REQ_NOTHING },
    { "--license-ose-dual",                 SCMOPT_LICENSE_OSE_DUAL_GPL_CDDL,       RTGETOPT_REQ_NOTHING },
    { "--license-ose-cddl",                 SCMOPT_LICENSE_OSE_CDDL,                RTGETOPT_REQ_NOTHING },
    { "--license-lgpl",                     SCMOPT_LICENSE_LGPL,                    RTGETOPT_REQ_NOTHING },
    { "--license-mit",                      SCMOPT_LICENSE_MIT,                     RTGETOPT_REQ_NOTHING },
    { "--license-based-on-mit",             SCMOPT_LICENSE_BASED_ON_MIT,            RTGETOPT_REQ_NOTHING },
    { "--lgpl-disclaimer",                  SCMOPT_LGPL_DISCLAIMER,                 RTGETOPT_REQ_NOTHING },
    { "--no-lgpl-disclaimer",               SCMOPT_NO_LGPL_DISCLAIMER,              RTGETOPT_REQ_NOTHING },
    { "--set-svn-eol",                      SCMOPT_SET_SVN_EOL,                     RTGETOPT_REQ_NOTHING },
    { "--dont-set-svn-eol",                 SCMOPT_DONT_SET_SVN_EOL,                RTGETOPT_REQ_NOTHING },
    { "--set-svn-executable",               SCMOPT_SET_SVN_EXECUTABLE,              RTGETOPT_REQ_NOTHING },
    { "--dont-set-svn-executable",          SCMOPT_DONT_SET_SVN_EXECUTABLE,         RTGETOPT_REQ_NOTHING },
    { "--set-svn-keywords",                 SCMOPT_SET_SVN_KEYWORDS,                RTGETOPT_REQ_NOTHING },
    { "--dont-set-svn-keywords",            SCMOPT_DONT_SET_SVN_KEYWORDS,           RTGETOPT_REQ_NOTHING },
    { "--skip-svn-sync-process",            SCMOPT_SKIP_SVN_SYNC_PROCESS,           RTGETOPT_REQ_NOTHING },
    { "--dont-skip-svn-sync-process",       SCMOPT_DONT_SKIP_SVN_SYNC_PROCESS,      RTGETOPT_REQ_NOTHING },
    { "--skip-unicode-checks",              SCMOPT_SKIP_UNICODE_CHECKS,             RTGETOPT_REQ_NOTHING },
    { "--dont-skip-unicode-checks",         SCMOPT_DONT_SKIP_UNICODE_CHECKS,        RTGETOPT_REQ_NOTHING },
    { "--tab-size",                         SCMOPT_TAB_SIZE,                        RTGETOPT_REQ_UINT8   },
    { "--width",                            SCMOPT_WIDTH,                           RTGETOPT_REQ_UINT8   },

    /* input selection */
    { "--only-svn-dirs",                    SCMOPT_ONLY_SVN_DIRS,                   RTGETOPT_REQ_NOTHING },
    { "--not-only-svn-dirs",                SCMOPT_NOT_ONLY_SVN_DIRS,               RTGETOPT_REQ_NOTHING },
    { "--only-svn-files",                   SCMOPT_ONLY_SVN_FILES,                  RTGETOPT_REQ_NOTHING },
    { "--not-only-svn-files",               SCMOPT_NOT_ONLY_SVN_FILES,              RTGETOPT_REQ_NOTHING },
    { "--filter-out-dirs",                  SCMOPT_FILTER_OUT_DIRS,                 RTGETOPT_REQ_STRING  },
    { "--filter-files",                     SCMOPT_FILTER_FILES,                    RTGETOPT_REQ_STRING  },
    { "--filter-out-files",                 SCMOPT_FILTER_OUT_FILES,                RTGETOPT_REQ_STRING  },

    /* rewriter selection */
    { "--treat-as",                         SCMOPT_TREAT_AS,                        RTGETOPT_REQ_STRING  },
    { "--add-action",                       SCMOPT_ADD_ACTION,                      RTGETOPT_REQ_STRING  },
    { "--del-action",                       SCMOPT_DEL_ACTION,                      RTGETOPT_REQ_STRING  },

    /* Additional help */
    { "--help-config",                      SCMOPT_HELP_CONFIG,                     RTGETOPT_REQ_NOTHING },
    { "--help-actions",                     SCMOPT_HELP_ACTIONS,                    RTGETOPT_REQ_NOTHING },
};

/** Consider files matching the following patterns (base names only). */
static const char  *g_pszFileFilter         = NULL;

/* The rewriter configuration. */
#define SCM_REWRITER_CFG(a_Global, a_szName, fnRewriter) static const SCMREWRITERCFG a_Global = { &fnRewriter, a_szName }
SCM_REWRITER_CFG(g_StripTrailingBlanks,             "strip-trailing-blanks",        rewrite_StripTrailingBlanks);
SCM_REWRITER_CFG(g_ExpandTabs,                      "expand-tabs",                  rewrite_ExpandTabs);
SCM_REWRITER_CFG(g_ForceNativeEol,                  "force-native-eol",             rewrite_ForceNativeEol);
SCM_REWRITER_CFG(g_ForceLF,                         "force-lf",                     rewrite_ForceLF);
SCM_REWRITER_CFG(g_ForceCRLF,                       "force-crlf",                   rewrite_ForceCRLF);
SCM_REWRITER_CFG(g_AdjustTrailingLines,             "adjust-trailing-lines",        rewrite_AdjustTrailingLines);
SCM_REWRITER_CFG(g_SvnNoExecutable,                 "svn-no-executable",            rewrite_SvnNoExecutable);
SCM_REWRITER_CFG(g_SvnNoKeywords,                   "svn-no-keywords",              rewrite_SvnNoKeywords);
SCM_REWRITER_CFG(g_SvnNoEolStyle,                   "svn-no-eol-style",             rewrite_SvnNoEolStyle);
SCM_REWRITER_CFG(g_SvnBinary,                       "svn-binary",                   rewrite_SvnBinary);
SCM_REWRITER_CFG(g_SvnKeywords,                     "svn-keywords",                 rewrite_SvnKeywords);
SCM_REWRITER_CFG(g_SvnSyncProcess,                  "svn-sync-process",             rewrite_SvnSyncProcess);
SCM_REWRITER_CFG(g_UnicodeChecks,                   "unicode-checks",               rewrite_UnicodeChecks);
SCM_REWRITER_CFG(g_PageChecks,                      "page-checks",                  rewrite_PageChecks);
SCM_REWRITER_CFG(g_ForceHrcVrcInsteadOfRc,          "force-hrc-vrc-no-rc",          rewrite_ForceHrcVrcInsteadOfRc);
SCM_REWRITER_CFG(g_Copyright_CstyleComment,         "copyright-c-style",            rewrite_Copyright_CstyleComment);
SCM_REWRITER_CFG(g_Copyright_HashComment,           "copyright-hash-style",         rewrite_Copyright_HashComment);
SCM_REWRITER_CFG(g_Copyright_PythonComment,         "copyright-python-style",       rewrite_Copyright_PythonComment);
SCM_REWRITER_CFG(g_Copyright_RemComment,            "copyright-rem-style",          rewrite_Copyright_RemComment);
SCM_REWRITER_CFG(g_Copyright_SemicolonComment,      "copyright-semicolon-style",    rewrite_Copyright_SemicolonComment);
SCM_REWRITER_CFG(g_Copyright_SqlComment,            "copyright-sql-style",          rewrite_Copyright_SqlComment);
SCM_REWRITER_CFG(g_Copyright_TickComment,           "copyright-tick-style",         rewrite_Copyright_TickComment);
SCM_REWRITER_CFG(g_Copyright_XmlComment,            "copyright-xml-style",          rewrite_Copyright_XmlComment);
SCM_REWRITER_CFG(g_Makefile_kup,                    "makefile-kup",                 rewrite_Makefile_kup);
SCM_REWRITER_CFG(g_Makefile_kmk,                    "makefile-kmk",                 rewrite_Makefile_kmk);
SCM_REWRITER_CFG(g_FixFlowerBoxMarkers,             "fix-flower-boxes",             rewrite_FixFlowerBoxMarkers);
SCM_REWRITER_CFG(g_FixHeaderGuards,                 "fix-header-guard",             rewrite_FixHeaderGuards);
SCM_REWRITER_CFG(g_Fix_C_and_CPP_Todos,             "fix-c-todos",                  rewrite_Fix_C_and_CPP_Todos);
SCM_REWRITER_CFG(g_Fix_Err_H,                       "fix-err-h",                    rewrite_Fix_Err_H);
SCM_REWRITER_CFG(g_C_and_CPP,                       "c-and-cpp",                    rewrite_C_and_CPP);

/** The rewriter actions. */
static PCSCMREWRITERCFG const g_papRewriterActions[] =
{
    &g_StripTrailingBlanks,
    &g_ExpandTabs,
    &g_ForceNativeEol,
    &g_ForceLF,
    &g_ForceCRLF,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnNoKeywords,
    &g_SvnNoEolStyle,
    &g_SvnBinary,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_Copyright_CstyleComment,
    &g_Copyright_HashComment,
    &g_Copyright_PythonComment,
    &g_Copyright_RemComment,
    &g_Copyright_SemicolonComment,
    &g_Copyright_SqlComment,
    &g_Copyright_TickComment,
    &g_Makefile_kup,
    &g_Makefile_kmk,
    &g_FixFlowerBoxMarkers,
    &g_FixHeaderGuards,
    &g_Fix_C_and_CPP_Todos,
    &g_Fix_Err_H,
    &g_UnicodeChecks,
    &g_PageChecks,
    &g_ForceHrcVrcInsteadOfRc,
    &g_C_and_CPP,
};


static PCSCMREWRITERCFG const g_apRewritersFor_Makefile_kup[] =
{
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Makefile_kup
};

static PCSCMREWRITERCFG const g_apRewritersFor_Makefile_kmk[] =
{
    &g_ForceNativeEol,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
    &g_Makefile_kmk
};

static PCSCMREWRITERCFG const g_apRewritersFor_OtherMakefiles[] =
{
    &g_ForceNativeEol,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_C_and_CPP[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_PageChecks,
    &g_ForceHrcVrcInsteadOfRc,
    &g_Copyright_CstyleComment,
    &g_FixFlowerBoxMarkers,
    &g_Fix_C_and_CPP_Todos,
    &g_Fix_Err_H,
    &g_C_and_CPP,
};

static PCSCMREWRITERCFG const g_apRewritersFor_H_and_HPP[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_PageChecks,
    &g_ForceHrcVrcInsteadOfRc,
    &g_Copyright_CstyleComment,
    /// @todo &g_FixFlowerBoxMarkers,
    &g_FixHeaderGuards,
    &g_C_and_CPP
};

static PCSCMREWRITERCFG const g_apRewritersFor_RC[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_CstyleComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_DTrace[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_CstyleComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_DSL[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_CstyleComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_ASM[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SemicolonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_DEF[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SemicolonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_ShellScripts[] =
{
    &g_ForceLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_BatchFiles[] =
{
    &g_ForceCRLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_RemComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_BasicScripts[] =
{
    &g_ForceCRLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_TickComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_SedScripts[] =
{
    &g_ForceLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Python[] =
{
    /** @todo &g_ForceLFIfExecutable */
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_PythonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Perl[] =
{
    /** @todo &g_ForceLFIfExecutable */
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_DriverInfFiles[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SemicolonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_NsisFiles[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SemicolonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Java[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_CstyleComment,
    &g_FixFlowerBoxMarkers,
    &g_Fix_C_and_CPP_Todos,
};

static PCSCMREWRITERCFG const g_apRewritersFor_ScmSettings[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Images[] =
{
    &g_SvnNoExecutable,
    &g_SvnBinary,
    &g_SvnSyncProcess,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Xslt[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_XmlComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Xml[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_XmlComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_Wix[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_XmlComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_QtProject[] =
{
    &g_ForceNativeEol,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_QtResourceFiles[] =
{
    &g_ForceNativeEol,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    /** @todo figure out copyright for Qt resource XML files. */
};

static PCSCMREWRITERCFG const g_apRewritersFor_QtTranslations[] =
{
    &g_ForceNativeEol,
    &g_SvnNoExecutable,
};

static PCSCMREWRITERCFG const g_apRewritersFor_QtUiFiles[] =
{
    &g_ForceNativeEol,
    &g_SvnNoExecutable,
    &g_SvnKeywords,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    /** @todo copyright is in an XML 'comment' element. */
};

static PCSCMREWRITERCFG const g_apRewritersFor_SifFiles[] =
{
    &g_ForceCRLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SemicolonComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_SqlFiles[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_SqlComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_GnuAsm[] =
{
    &g_ForceNativeEol,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_CstyleComment,
};

static PCSCMREWRITERCFG const g_apRewritersFor_TextFiles[] =
{
    &g_ForceNativeEol,
    &g_StripTrailingBlanks,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    /** @todo check for plain copyright + license in text files. */
};

static PCSCMREWRITERCFG const g_apRewritersFor_PlainTextFiles[] =
{
    &g_ForceNativeEol,
    &g_StripTrailingBlanks,
    &g_SvnKeywords,
    &g_SvnNoExecutable,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
};

static PCSCMREWRITERCFG const g_apRewritersFor_BinaryFiles[] =
{
    &g_SvnBinary,
    &g_SvnSyncProcess,
};

static PCSCMREWRITERCFG const g_apRewritersFor_FileLists[] = /* both makefile and shell script */
{
    &g_ForceLF,
    &g_ExpandTabs,
    &g_StripTrailingBlanks,
    &g_AdjustTrailingLines,
    &g_SvnSyncProcess,
    &g_UnicodeChecks,
    &g_Copyright_HashComment,
};


/**
 * Array of standard rewriter configurations.
 */
static SCMCFGENTRY const g_aConfigs[] =
{
#define SCM_CFG_ENTRY(a_szName, a_aRewriters, a_fBinary, a_szFilePatterns) \
    { RT_ELEMENTS(a_aRewriters), &a_aRewriters[0], a_fBinary, a_szFilePatterns, a_szName }
    SCM_CFG_ENTRY("kup",        g_apRewritersFor_Makefile_kup,     false, "Makefile.kup" ),
    SCM_CFG_ENTRY("kmk",        g_apRewritersFor_Makefile_kmk,     false, "*.kmk" ),
    SCM_CFG_ENTRY("c",          g_apRewritersFor_C_and_CPP,        false, "*.c|*.cpp|*.C|*.CPP|*.cxx|*.cc|*.m|*.mm|*.lds" ),
    SCM_CFG_ENTRY("h",          g_apRewritersFor_H_and_HPP,        false, "*.h|*.hpp" ),
    SCM_CFG_ENTRY("rc",         g_apRewritersFor_RC,               false, "*.rc" ),
    SCM_CFG_ENTRY("asm",        g_apRewritersFor_ASM,              false, "*.asm|*.mac|*.inc" ),
    SCM_CFG_ENTRY("dtrace",     g_apRewritersFor_DTrace,           false, "*.d" ),
    SCM_CFG_ENTRY("def",        g_apRewritersFor_DEF,              false, "*.def" ),
    SCM_CFG_ENTRY("iasl",       g_apRewritersFor_DSL,              false, "*.dsl" ),
    SCM_CFG_ENTRY("shell",      g_apRewritersFor_ShellScripts,     false, "*.sh|configure" ),
    SCM_CFG_ENTRY("batch",      g_apRewritersFor_BatchFiles,       false, "*.bat|*.cmd|*.btm" ),
    SCM_CFG_ENTRY("vbs",        g_apRewritersFor_BasicScripts,     false, "*.vbs|*.vb" ),
    SCM_CFG_ENTRY("sed",        g_apRewritersFor_SedScripts,       false, "*.sed" ),
    SCM_CFG_ENTRY("python",     g_apRewritersFor_Python,           false, "*.py" ),
    SCM_CFG_ENTRY("perl",       g_apRewritersFor_Perl,             false, "*.pl|*.pm" ),
    SCM_CFG_ENTRY("drvinf",     g_apRewritersFor_DriverInfFiles,   false, "*.inf" ),
    SCM_CFG_ENTRY("nsis",       g_apRewritersFor_NsisFiles,        false, "*.nsh|*.nsi|*.nsis" ),
    SCM_CFG_ENTRY("java",       g_apRewritersFor_Java,             false, "*.java" ),
    SCM_CFG_ENTRY("scm",        g_apRewritersFor_ScmSettings,      false, "*.scm-settings" ),
    SCM_CFG_ENTRY("image",      g_apRewritersFor_Images,           true,  "*.png|*.bmp|*.jpg|*.pnm|*.ico|*.icns|*.tiff|*.tif|"
                                                                          "*.xcf|*.gif|*.jar|*.dll|*.exe|*.ttf|*.woff|*.woff2" ),
    SCM_CFG_ENTRY("xslt",       g_apRewritersFor_Xslt,             false, "*.xsl" ),
    SCM_CFG_ENTRY("xml",        g_apRewritersFor_Xml,              false, "*.xml|*.dist|*.qhcp" ),
    SCM_CFG_ENTRY("wix",        g_apRewritersFor_Wix,              false, "*.wxi|*.wxs|*.wxl" ),
    SCM_CFG_ENTRY("qt-pro",     g_apRewritersFor_QtProject,        false, "*.pro" ),
    SCM_CFG_ENTRY("qt-rc",      g_apRewritersFor_QtResourceFiles,  false, "*.qrc" ),
    SCM_CFG_ENTRY("qt-ts",      g_apRewritersFor_QtTranslations,   false, "*.ts" ),
    SCM_CFG_ENTRY("qt-ui",      g_apRewritersFor_QtUiFiles,        false, "*.ui" ),
    SCM_CFG_ENTRY("sif",        g_apRewritersFor_SifFiles,         false, "*.sif" ),
    SCM_CFG_ENTRY("sql",        g_apRewritersFor_SqlFiles,         false, "*.pgsql|*.sql" ),
    SCM_CFG_ENTRY("gas",        g_apRewritersFor_GnuAsm,           false, "*.S" ),
    SCM_CFG_ENTRY("binary",     g_apRewritersFor_BinaryFiles,      true,  "*.bin|*.pdf|*.zip|*.bz2|*.gz" ),
    /* These should be be last: */
    SCM_CFG_ENTRY("make",       g_apRewritersFor_OtherMakefiles,   false, "Makefile|makefile|GNUmakefile|SMakefile|Makefile.am|Makefile.in|*.cmake|*.gmk" ),
    SCM_CFG_ENTRY("text",       g_apRewritersFor_TextFiles,        false, "*.txt|README*|readme*|ReadMe*|NOTE*|TODO*" ),
    SCM_CFG_ENTRY("plaintext",  g_apRewritersFor_PlainTextFiles,   false, "LICENSE|ChangeLog|FAQ|AUTHORS|INSTALL|NEWS" ),
    SCM_CFG_ENTRY("file-list",  g_apRewritersFor_FileLists,        false, "files_*" ),
};



/* -=-=-=-=-=- settings -=-=-=-=-=- */

/**
 * Delete the given config entry.
 *
 * @param   pEntry              The configuration entry to delete.
 */
static void scmCfgEntryDelete(PSCMCFGENTRY pEntry)
{
    RTMemFree((void *)pEntry->paRewriters);
    pEntry->paRewriters = NULL;
    RTMemFree(pEntry);
}

/**
 * Create a new configuration entry.
 *
 * @returns The new entry. NULL if out of memory.
 * @param   pEntry              The configuration entry to duplicate.
 */
static PSCMCFGENTRY scmCfgEntryNew(void)
{
    PSCMCFGENTRY pNew = (PSCMCFGENTRY)RTMemAlloc(sizeof(*pNew));
    if (pNew)
    {
        pNew->pszName        = "custom";
        pNew->pszFilePattern = "custom";
        pNew->cRewriters     = 0;
        pNew->paRewriters    = NULL;
        pNew->fBinary        = false;
    }
    return pNew;
}

/**
 * Duplicate the given config entry.
 *
 * @returns The duplicate. NULL if out of memory.
 * @param   pEntry              The configuration entry to duplicate.
 */
static PSCMCFGENTRY scmCfgEntryDup(PCSCMCFGENTRY pEntry)
{
    if (pEntry)
    {
        PSCMCFGENTRY pDup = (PSCMCFGENTRY)RTMemDup(pEntry, sizeof(*pEntry));
        if (pDup)
        {
            size_t cbSrcRewriters = sizeof(pEntry->paRewriters[0]) * pEntry->cRewriters;
            size_t cbDstRewriters = sizeof(pEntry->paRewriters[0]) * RT_ALIGN_Z(pEntry->cRewriters, 8);
            pDup->paRewriters = (PCSCMREWRITERCFG const *)RTMemDupEx(pEntry->paRewriters, cbSrcRewriters,
                                                                     cbDstRewriters - cbSrcRewriters);
            if (pDup->paRewriters)
                return pDup;

            RTMemFree(pDup);
        }
        return NULL;
    }
    return scmCfgEntryNew();
}

/**
 * Adds a rewriter action to the given config entry (--add-action).
 *
 * @returns VINF_SUCCESS.
 * @param   pEntry              The configuration entry.
 * @param   pAction             The rewriter action to add.
 */
static int scmCfgEntryAddAction(PSCMCFGENTRY pEntry, PCSCMREWRITERCFG pAction)
{
    PCSCMREWRITERCFG *paRewriters = (PCSCMREWRITERCFG *)pEntry->paRewriters;
    if (pEntry->cRewriters % 8 == 0)
    {
        size_t cbRewriters = sizeof(pEntry->paRewriters[0]) * RT_ALIGN_Z((pEntry->cRewriters + 1), 8);
        void *pvNew = RTMemRealloc(paRewriters, cbRewriters);
        if (pvNew)
            pEntry->paRewriters = paRewriters = (PCSCMREWRITERCFG *)pvNew;
        else
            return VERR_NO_MEMORY;
    }

    paRewriters[pEntry->cRewriters++] = pAction;
    return VINF_SUCCESS;
}

/**
 * Delets an rewriter action from the given config entry (--del-action).
 *
 * @param   pEntry              The configuration entry.
 * @param   pAction             The rewriter action to remove.
 */
static void scmCfgEntryDelAction(PSCMCFGENTRY pEntry, PCSCMREWRITERCFG pAction)
{
    PCSCMREWRITERCFG *paRewriters = (PCSCMREWRITERCFG *)pEntry->paRewriters;
    size_t const cEntries = pEntry->cRewriters;
    size_t       iDst = 0;
    for (size_t iSrc = 0; iSrc < cEntries; iSrc++)
    {
        PCSCMREWRITERCFG pCurAction = paRewriters[iSrc];
        if (pCurAction != pAction)
            paRewriters[iDst++] = pCurAction;
    }
    pEntry->cRewriters = iDst;
}

/**
 * Init a settings structure with settings from @a pSrc.
 *
 * @returns IPRT status code
 * @param   pSettings           The settings.
 * @param   pSrc                The source settings.
 */
static int scmSettingsBaseInitAndCopy(PSCMSETTINGSBASE pSettings, PCSCMSETTINGSBASE pSrc)
{
    *pSettings = *pSrc;

    int rc = RTStrDupEx(&pSettings->pszFilterFiles, pSrc->pszFilterFiles);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrDupEx(&pSettings->pszFilterOutFiles, pSrc->pszFilterOutFiles);
        if (RT_SUCCESS(rc))
        {
            rc = RTStrDupEx(&pSettings->pszFilterOutDirs, pSrc->pszFilterOutDirs);
            if (RT_SUCCESS(rc))
            {
                rc = RTStrDupEx(&pSettings->pszGuardPrefix, pSrc->pszGuardPrefix);
                if (RT_SUCCESS(rc))
                {
                    if (pSrc->pszGuardRelativeToDir)
                        rc = RTStrDupEx(&pSettings->pszGuardRelativeToDir, pSrc->pszGuardRelativeToDir);
                    if (RT_SUCCESS(rc))
                    {

                        if (!pSrc->fFreeTreatAs)
                            return VINF_SUCCESS;

                        pSettings->pTreatAs = scmCfgEntryDup(pSrc->pTreatAs);
                        if (pSettings->pTreatAs)
                            return VINF_SUCCESS;

                        RTStrFree(pSettings->pszGuardRelativeToDir);
                    }
                    RTStrFree(pSettings->pszGuardPrefix);
                }
            }
            RTStrFree(pSettings->pszFilterOutFiles);
        }
        RTStrFree(pSettings->pszFilterFiles);
    }

    pSettings->pszGuardRelativeToDir = NULL;
    pSettings->pszGuardPrefix = NULL;
    pSettings->pszFilterFiles = NULL;
    pSettings->pszFilterOutFiles = NULL;
    pSettings->pszFilterOutDirs = NULL;
    pSettings->pTreatAs = NULL;
    return rc;
}

/**
 * Init a settings structure.
 *
 * @returns IPRT status code
 * @param   pSettings           The settings.
 */
static int scmSettingsBaseInit(PSCMSETTINGSBASE pSettings)
{
    return scmSettingsBaseInitAndCopy(pSettings, &g_Defaults);
}

/**
 * Deletes the settings, i.e. free any dynamically allocated content.
 *
 * @param   pSettings           The settings.
 */
static void scmSettingsBaseDelete(PSCMSETTINGSBASE pSettings)
{
    if (pSettings)
    {
        Assert(pSettings->cchTab != UINT8_MAX);
        pSettings->cchTab = UINT8_MAX;

        RTStrFree(pSettings->pszGuardPrefix);
        RTStrFree(pSettings->pszGuardRelativeToDir);
        RTStrFree(pSettings->pszFilterFiles);
        RTStrFree(pSettings->pszFilterOutFiles);
        RTStrFree(pSettings->pszFilterOutDirs);
        if (pSettings->fFreeTreatAs)
            scmCfgEntryDelete((PSCMCFGENTRY)pSettings->pTreatAs);

        pSettings->pszGuardPrefix = NULL;
        pSettings->pszGuardRelativeToDir = NULL;
        pSettings->pszFilterOutDirs = NULL;
        pSettings->pszFilterOutFiles = NULL;
        pSettings->pszFilterFiles = NULL;
        pSettings->pTreatAs = NULL;
        pSettings->fFreeTreatAs = false;
    }
}

/**
 * Processes a RTGetOpt result.
 *
 * @retval  VINF_SUCCESS if handled.
 * @retval  VERR_OUT_OF_RANGE if the option value was out of range.
 * @retval  VERR_GETOPT_UNKNOWN_OPTION if the option was not recognized.
 *
 * @param   pSettings           The settings to change.
 * @param   rc                  The RTGetOpt return value.
 * @param   pValueUnion         The RTGetOpt value union.
 * @param   pchDir              The absolute path to the directory relative
 *                              components in pchLine should be relative to.
 * @param   cchDir              The length of the @a pchDir string.
 */
static int scmSettingsBaseHandleOpt(PSCMSETTINGSBASE pSettings, int rc, PRTGETOPTUNION pValueUnion,
                                    const char *pchDir, size_t cchDir)
{
    Assert(pchDir[cchDir - 1] == '/');

    switch (rc)
    {
        case SCMOPT_CONVERT_EOL:
            pSettings->fConvertEol = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_CONVERT_EOL:
            pSettings->fConvertEol = false;
            return VINF_SUCCESS;

        case SCMOPT_CONVERT_TABS:
            pSettings->fConvertTabs = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_CONVERT_TABS:
            pSettings->fConvertTabs = false;
            return VINF_SUCCESS;

        case SCMOPT_FORCE_FINAL_EOL:
            pSettings->fForceFinalEol = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FORCE_FINAL_EOL:
            pSettings->fForceFinalEol = false;
            return VINF_SUCCESS;

        case SCMOPT_FORCE_TRAILING_LINE:
            pSettings->fForceTrailingLine = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FORCE_TRAILING_LINE:
            pSettings->fForceTrailingLine = false;
            return VINF_SUCCESS;


        case SCMOPT_STRIP_TRAILING_BLANKS:
            pSettings->fStripTrailingBlanks = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_STRIP_TRAILING_BLANKS:
            pSettings->fStripTrailingBlanks = false;
            return VINF_SUCCESS;

        case SCMOPT_MIN_BLANK_LINES_BEFORE_FLOWER_BOX_MARKERS:
            pSettings->cMinBlankLinesBeforeFlowerBoxMakers = pValueUnion->u8;
            return VINF_SUCCESS;


        case SCMOPT_STRIP_TRAILING_LINES:
            pSettings->fStripTrailingLines = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_STRIP_TRAILING_LINES:
            pSettings->fStripTrailingLines = false;
            return VINF_SUCCESS;

        case SCMOPT_FIX_FLOWER_BOX_MARKERS:
            pSettings->fFixFlowerBoxMarkers = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FIX_FLOWER_BOX_MARKERS:
            pSettings->fFixFlowerBoxMarkers = false;
            return VINF_SUCCESS;

        case SCMOPT_FIX_HEADER_GUARDS:
            pSettings->fFixHeaderGuards = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FIX_HEADER_GUARDS:
            pSettings->fFixHeaderGuards = false;
            return VINF_SUCCESS;

        case SCMOPT_PRAGMA_ONCE:
            pSettings->fPragmaOnce = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_PRAGMA_ONCE:
            pSettings->fPragmaOnce = false;
            return VINF_SUCCESS;

        case SCMOPT_FIX_HEADER_GUARD_ENDIF:
            pSettings->fFixHeaderGuardEndif = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FIX_HEADER_GUARD_ENDIF:
            pSettings->fFixHeaderGuardEndif = false;
            return VINF_SUCCESS;

        case SCMOPT_ENDIF_GUARD_COMMENT:
            pSettings->fEndifGuardComment = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_ENDIF_GUARD_COMMENT:
            pSettings->fEndifGuardComment = false;
            return VINF_SUCCESS;

        case SCMOPT_GUARD_PREFIX:
            RTStrFree(pSettings->pszGuardPrefix);
            pSettings->pszGuardPrefix = NULL;
            return RTStrDupEx(&pSettings->pszGuardPrefix, pValueUnion->psz);

        case SCMOPT_GUARD_RELATIVE_TO_DIR:
            RTStrFree(pSettings->pszGuardRelativeToDir);
            pSettings->pszGuardRelativeToDir = NULL;
            if (*pValueUnion->psz != '\0')
            {
                if (   strcmp(pValueUnion->psz, "{dir}") == 0
                    || strcmp(pValueUnion->psz, "{parent}") == 0)
                    return RTStrDupEx(&pSettings->pszGuardRelativeToDir, pValueUnion->psz);
                if (cchDir == 1 && *pchDir == '/')
                {
                    pSettings->pszGuardRelativeToDir = RTPathAbsDup(pValueUnion->psz);
                    if (pSettings->pszGuardRelativeToDir)
                        return VINF_SUCCESS;
                }
                else
                {
                    char *pszDir = RTStrDupN(pchDir, cchDir);
                    if (pszDir)
                    {
                        pSettings->pszGuardRelativeToDir = RTPathAbsExDup(pszDir, pValueUnion->psz, RTPATH_STR_F_STYLE_HOST);
                        RTStrFree(pszDir);
                        if (pSettings->pszGuardRelativeToDir)
                            return VINF_SUCCESS;
                    }
                }
                RTMsgError("Failed to abspath --guard-relative-to-dir value '%s' - probably out of memory\n", pValueUnion->psz);
                return VERR_NO_STR_MEMORY;
            }
            return VINF_SUCCESS;

        case SCMOPT_FIX_TODOS:
            pSettings->fFixTodos = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FIX_TODOS:
            pSettings->fFixTodos = false;
            return VINF_SUCCESS;

        case SCMOPT_FIX_ERR_H:
            pSettings->fFixErrH = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_FIX_ERR_H:
            pSettings->fFixErrH = false;
            return VINF_SUCCESS;

        case SCMOPT_ONLY_GUEST_HOST_PAGE:
            pSettings->fOnlyGuestHostPage = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_PAGE_RESTRICTIONS:
            pSettings->fOnlyGuestHostPage = false;
            return VINF_SUCCESS;

        case SCMOPT_NO_ASM_MEM_PAGE_USE:
            pSettings->fNoASMMemPageUse = true;
            return VINF_SUCCESS;
        case SCMOPT_UNRESTRICTED_ASM_MEM_PAGE_USE:
            pSettings->fNoASMMemPageUse = false;
            return VINF_SUCCESS;

        case SCMOPT_NO_RC_USE:
            pSettings->fOnlyHrcVrcInsteadOfRc = true;
            return VINF_SUCCESS;
        case SCMOPT_UNRESTRICTED_RC_USE:
            pSettings->fOnlyHrcVrcInsteadOfRc = false;
            return VINF_SUCCESS;

        case SCMOPT_STANDARIZE_KMK:
            pSettings->fStandarizeKmk = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_STANDARIZE_KMK:
            pSettings->fStandarizeKmk = false;
            return VINF_SUCCESS;

        case SCMOPT_UPDATE_COPYRIGHT_YEAR:
            pSettings->fUpdateCopyrightYear = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_UPDATE_COPYRIGHT_YEAR:
            pSettings->fUpdateCopyrightYear = false;
            return VINF_SUCCESS;

        case SCMOPT_EXTERNAL_COPYRIGHT:
            pSettings->fExternalCopyright = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_EXTERNAL_COPYRIGHT:
            pSettings->fExternalCopyright = false;
            return VINF_SUCCESS;

        case SCMOPT_NO_UPDATE_LICENSE:
            pSettings->enmUpdateLicense = kScmLicense_LeaveAlone;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_OSE_GPL:
            pSettings->enmUpdateLicense = kScmLicense_OseGpl;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_OSE_DUAL_GPL_CDDL:
            pSettings->enmUpdateLicense = kScmLicense_OseDualGplCddl;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_OSE_CDDL:
            pSettings->enmUpdateLicense = kScmLicense_OseCddl;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_LGPL:
            pSettings->enmUpdateLicense = kScmLicense_Lgpl;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_MIT:
            pSettings->enmUpdateLicense = kScmLicense_Mit;
            return VINF_SUCCESS;
        case SCMOPT_LICENSE_BASED_ON_MIT:
            pSettings->enmUpdateLicense = kScmLicense_BasedOnMit;
            return VINF_SUCCESS;

        case SCMOPT_LGPL_DISCLAIMER:
            pSettings->fLgplDisclaimer = true;
            return VINF_SUCCESS;
        case SCMOPT_NO_LGPL_DISCLAIMER:
            pSettings->fLgplDisclaimer = false;
            return VINF_SUCCESS;

        case SCMOPT_ONLY_SVN_DIRS:
            pSettings->fOnlySvnDirs = true;
            return VINF_SUCCESS;
        case SCMOPT_NOT_ONLY_SVN_DIRS:
            pSettings->fOnlySvnDirs = false;
            return VINF_SUCCESS;

        case SCMOPT_ONLY_SVN_FILES:
            pSettings->fOnlySvnFiles = true;
            return VINF_SUCCESS;
        case SCMOPT_NOT_ONLY_SVN_FILES:
            pSettings->fOnlySvnFiles = false;
            return VINF_SUCCESS;

        case SCMOPT_SET_SVN_EOL:
            pSettings->fSetSvnEol = true;
            return VINF_SUCCESS;
        case SCMOPT_DONT_SET_SVN_EOL:
            pSettings->fSetSvnEol = false;
            return VINF_SUCCESS;

        case SCMOPT_SET_SVN_EXECUTABLE:
            pSettings->fSetSvnExecutable = true;
            return VINF_SUCCESS;
        case SCMOPT_DONT_SET_SVN_EXECUTABLE:
            pSettings->fSetSvnExecutable = false;
            return VINF_SUCCESS;

        case SCMOPT_SET_SVN_KEYWORDS:
            pSettings->fSetSvnKeywords = true;
            return VINF_SUCCESS;
        case SCMOPT_DONT_SET_SVN_KEYWORDS:
            pSettings->fSetSvnKeywords = false;
            return VINF_SUCCESS;

        case SCMOPT_SKIP_SVN_SYNC_PROCESS:
            pSettings->fSkipSvnSyncProcess = true;
            return VINF_SUCCESS;
        case SCMOPT_DONT_SKIP_SVN_SYNC_PROCESS:
            pSettings->fSkipSvnSyncProcess = false;
            return VINF_SUCCESS;

        case SCMOPT_SKIP_UNICODE_CHECKS:
            pSettings->fSkipUnicodeChecks = true;
            return VINF_SUCCESS;
        case SCMOPT_DONT_SKIP_UNICODE_CHECKS:
            pSettings->fSkipUnicodeChecks = false;
            return VINF_SUCCESS;

        case SCMOPT_TAB_SIZE:
            if (   pValueUnion->u8 < 1
                || pValueUnion->u8 >= RT_ELEMENTS(g_szTabSpaces))
            {
                RTMsgError("Invalid tab size: %u - must be in {1..%u}\n",
                           pValueUnion->u8, RT_ELEMENTS(g_szTabSpaces) - 1);
                return VERR_OUT_OF_RANGE;
            }
            pSettings->cchTab = pValueUnion->u8;
            return VINF_SUCCESS;

        case SCMOPT_WIDTH:
            if (pValueUnion->u8 < 20 || pValueUnion->u8 > 200)
            {
                RTMsgError("Invalid width size: %u - must be in {20..200} range\n", pValueUnion->u8);
                return VERR_OUT_OF_RANGE;
            }
            pSettings->cchWidth = pValueUnion->u8;
            return VINF_SUCCESS;

        case SCMOPT_FILTER_OUT_DIRS:
        case SCMOPT_FILTER_FILES:
        case SCMOPT_FILTER_OUT_FILES:
        {
            char **ppsz = NULL;
            switch (rc)
            {
                case SCMOPT_FILTER_OUT_DIRS:    ppsz = &pSettings->pszFilterOutDirs; break;
                case SCMOPT_FILTER_FILES:       ppsz = &pSettings->pszFilterFiles; break;
                case SCMOPT_FILTER_OUT_FILES:   ppsz = &pSettings->pszFilterOutFiles; break;
            }

            /*
             * An empty string zaps the current list.
             */
            if (!*pValueUnion->psz)
                return RTStrATruncate(ppsz, 0);

            /*
             * Non-empty strings are appended to the pattern list.
             *
             * Strip leading and trailing pattern separators before attempting
             * to append it.  If it's just separators, don't do anything.
             */
            const char *pszSrc = pValueUnion->psz;
            while (*pszSrc == '|')
                pszSrc++;
            size_t cchSrc = strlen(pszSrc);
            while (cchSrc > 0 && pszSrc[cchSrc - 1] == '|')
                cchSrc--;
            if (!cchSrc)
                return VINF_SUCCESS;

            /* Append it pattern by pattern, turning settings-relative paths into absolute ones. */
            for (;;)
            {
                const char *pszEnd = (const char *)memchr(pszSrc, '|', cchSrc);
                size_t cchPattern = pszEnd ? pszEnd - pszSrc : cchSrc;
                int rc2;
                if (*pszSrc == '/')
                    rc2 = RTStrAAppendExN(ppsz, 3,
                                          "|", *ppsz && **ppsz != '\0' ? (size_t)1 : (size_t)0,
                                          pchDir, cchDir - 1,
                                          pszSrc, cchPattern);
                else
                    rc2 = RTStrAAppendExN(ppsz, 2,
                                          "|", *ppsz && **ppsz != '\0' ? (size_t)1 : (size_t)0,
                                          pszSrc, cchPattern);
                if (RT_FAILURE(rc2))
                    return rc2;

                /* next */
                cchSrc -= cchPattern;
                if (!cchSrc)
                    return VINF_SUCCESS;
                cchSrc -= 1;
                pszSrc += cchPattern + 1;
            }
            /* not reached */
#if RT_MSC_PREREQ(RT_MSC_VER_VS2022) /* VS 2022 complains about fall thru, other compilers complains about unreachable code. */
           break;
#endif
        }

        case SCMOPT_TREAT_AS:
            if (pSettings->fFreeTreatAs)
            {
                scmCfgEntryDelete((PSCMCFGENTRY)pSettings->pTreatAs);
                pSettings->pTreatAs = NULL;
                pSettings->fFreeTreatAs = false;
            }

            if (*pValueUnion->psz)
            {
                /* first check the names, then patterns (legacy). */
                for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
                    if (strcmp(g_aConfigs[iCfg].pszName, pValueUnion->psz) == 0)
                    {
                        pSettings->pTreatAs = &g_aConfigs[iCfg];
                        return VINF_SUCCESS;
                    }
                for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
                    if (RTStrSimplePatternMultiMatch(g_aConfigs[iCfg].pszFilePattern, RTSTR_MAX,
                                                     pValueUnion->psz, RTSTR_MAX, NULL))
                    {
                        pSettings->pTreatAs = &g_aConfigs[iCfg];
                        return VINF_SUCCESS;
                    }
                /* Special help for listing the possibilities?  */
                if (strcmp(pValueUnion->psz, "help") == 0)
                {
                    RTPrintf("Possible --treat-as values:\n");
                    for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
                        RTPrintf("    %s (%s)\n", g_aConfigs[iCfg].pszName, g_aConfigs[iCfg].pszFilePattern);
                }
                return VERR_NOT_FOUND;
            }

            pSettings->pTreatAs = NULL;
            return VINF_SUCCESS;

        case SCMOPT_ADD_ACTION:
            for (uint32_t iAction = 0; iAction < RT_ELEMENTS(g_papRewriterActions); iAction++)
                if (strcmp(g_papRewriterActions[iAction]->pszName, pValueUnion->psz) == 0)
                {
                    PSCMCFGENTRY pEntry = (PSCMCFGENTRY)pSettings->pTreatAs;
                    if (!pSettings->fFreeTreatAs)
                    {
                        pEntry = scmCfgEntryDup(pEntry);
                        if (!pEntry)
                            return VERR_NO_MEMORY;
                        pSettings->pTreatAs = pEntry;
                        pSettings->fFreeTreatAs = true;
                    }
                    return scmCfgEntryAddAction(pEntry, g_papRewriterActions[iAction]);
                }
            RTMsgError("Unknown --add-action value '%s'.  Try --help-actions for a list.", pValueUnion->psz);
            return VERR_NOT_FOUND;

        case SCMOPT_DEL_ACTION:
        {
            uint32_t cActions = 0;
            for (uint32_t iAction = 0; iAction < RT_ELEMENTS(g_papRewriterActions); iAction++)
                if (RTStrSimplePatternMatch(pValueUnion->psz, g_papRewriterActions[iAction]->pszName))
                {
                    cActions++;
                    PSCMCFGENTRY pEntry = (PSCMCFGENTRY)pSettings->pTreatAs;
                    if (!pSettings->fFreeTreatAs)
                    {
                        pEntry = scmCfgEntryDup(pEntry);
                        if (!pEntry)
                            return VERR_NO_MEMORY;
                        pSettings->pTreatAs = pEntry;
                        pSettings->fFreeTreatAs = true;
                    }
                    scmCfgEntryDelAction(pEntry, g_papRewriterActions[iAction]);
                    if (!strchr(pValueUnion->psz, '*'))
                        return VINF_SUCCESS;
                }
            if (cActions > 0)
                return VINF_SUCCESS;
            RTMsgError("Unknown --del-action value '%s'.  Try --help-actions for a list.", pValueUnion->psz);
            return VERR_NOT_FOUND;
        }

        default:
            return VERR_GETOPT_UNKNOWN_OPTION;
    }
}

/**
 * Parses an option string.
 *
 * @returns IPRT status code.
 * @param   pBase               The base settings structure to apply the options
 *                              to.
 * @param   pszOptions          The options to parse.
 * @param   pchDir              The absolute path to the directory relative
 *                              components in pchLine should be relative to.
 * @param   cchDir              The length of the @a pchDir string.
 */
static int scmSettingsBaseParseString(PSCMSETTINGSBASE pBase, const char *pszLine, const char *pchDir, size_t cchDir)
{
    int    cArgs;
    char **papszArgs;
    int rc = RTGetOptArgvFromString(&papszArgs, &cArgs, pszLine, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL);
    if (RT_SUCCESS(rc))
    {
        RTGETOPTUNION   ValueUnion;
        RTGETOPTSTATE   GetOptState;
        rc = RTGetOptInit(&GetOptState, cArgs, papszArgs, &g_aScmOpts[0], RT_ELEMENTS(g_aScmOpts), 0, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            while ((rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
            {
                rc = scmSettingsBaseHandleOpt(pBase, rc, &ValueUnion, pchDir, cchDir);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        RTGetOptArgvFree(papszArgs);
    }

    return rc;
}

/**
 * Parses an unterminated option string.
 *
 * @returns IPRT status code.
 * @param   pBase               The base settings structure to apply the options
 *                              to.
 * @param   pchLine             The line.
 * @param   cchLine             The line length.
 * @param   pchDir              The absolute path to the directory relative
 *                              components in pchLine should be relative to.
 * @param   cchDir              The length of the @a pchDir string.
 */
static int scmSettingsBaseParseStringN(PSCMSETTINGSBASE pBase, const char *pchLine, size_t cchLine,
                                       const char *pchDir, size_t cchDir)
{
    char *pszLine = RTStrDupN(pchLine, cchLine);
    if (!pszLine)
        return VERR_NO_MEMORY;
    int rc = scmSettingsBaseParseString(pBase, pszLine, pchDir, cchDir);
    RTStrFree(pszLine);
    return rc;
}

/**
 * Verifies the options string.
 *
 * @returns IPRT status code.
 * @param   pszOptions          The options to verify .
 */
static int scmSettingsBaseVerifyString(const char *pszOptions)
{
    SCMSETTINGSBASE Base;
    int rc = scmSettingsBaseInit(&Base);
    if (RT_SUCCESS(rc))
    {
        rc = scmSettingsBaseParseString(&Base, pszOptions, "/", 1);
        scmSettingsBaseDelete(&Base);
    }
    return rc;
}

/**
 * Loads settings found in editor and SCM settings directives within the
 * document (@a pStream).
 *
 * @returns IPRT status code.
 * @param   pBase               The settings base to load settings into.
 * @param   pStream             The stream to scan for settings directives.
 */
static int scmSettingsBaseLoadFromDocument(PSCMSETTINGSBASE pBase, PSCMSTREAM pStream)
{
    /** @todo Editor and SCM settings directives in documents.  */
    RT_NOREF2(pBase, pStream);
    return VINF_SUCCESS;
}

/**
 * Creates a new settings file struct, cloning @a pSettings.
 *
 * @returns IPRT status code.
 * @param   ppSettings          Where to return the new struct.
 * @param   pSettingsBase       The settings to inherit from.
 */
static int scmSettingsCreate(PSCMSETTINGS *ppSettings, PCSCMSETTINGSBASE pSettingsBase)
{
    PSCMSETTINGS pSettings = (PSCMSETTINGS)RTMemAlloc(sizeof(*pSettings));
    if (!pSettings)
        return VERR_NO_MEMORY;
    int rc = scmSettingsBaseInitAndCopy(&pSettings->Base, pSettingsBase);
    if (RT_SUCCESS(rc))
    {
        pSettings->pDown   = NULL;
        pSettings->pUp     = NULL;
        pSettings->paPairs = NULL;
        pSettings->cPairs  = 0;
        *ppSettings = pSettings;
        return VINF_SUCCESS;
    }
    RTMemFree(pSettings);
    return rc;
}

/**
 * Destroys a settings structure.
 *
 * @param   pSettings           The settings structure to destroy.  NULL is OK.
 */
static void scmSettingsDestroy(PSCMSETTINGS pSettings)
{
    if (pSettings)
    {
        scmSettingsBaseDelete(&pSettings->Base);
        for (size_t i = 0; i < pSettings->cPairs; i++)
        {
            RTStrFree(pSettings->paPairs[i].pszPattern);
            RTStrFree(pSettings->paPairs[i].pszOptions);
            RTStrFree(pSettings->paPairs[i].pszRelativeTo);
            pSettings->paPairs[i].pszPattern = NULL;
            pSettings->paPairs[i].pszOptions = NULL;
            pSettings->paPairs[i].pszRelativeTo = NULL;
        }
        RTMemFree(pSettings->paPairs);
        pSettings->paPairs = NULL;
        RTMemFree(pSettings);
    }
}

/**
 * Adds a pattern/options pair to the settings structure.
 *
 * @returns IPRT status code.
 * @param   pSettings           The settings.
 * @param   pchLine             The line containing the unparsed pair.
 * @param   cchLine             The length of the line.
 * @param   offColon            The offset of the colon into the line.
 * @param   pchDir              The absolute path to the directory relative
 *                              components in pchLine should be relative to.
 * @param   cchDir              The length of the @a pchDir string.
 */
static int scmSettingsAddPair(PSCMSETTINGS pSettings, const char *pchLine, size_t cchLine, size_t offColon,
                              const char *pchDir, size_t cchDir)
{
    Assert(pchLine[offColon] == ':' && offColon < cchLine);
    Assert(pchDir[cchDir - 1] == '/');

    /*
     * Split the string.
     */
    size_t cchPattern = offColon;
    size_t cchOptions = cchLine - cchPattern - 1;

    /* strip spaces everywhere */
    while (cchPattern > 0 && RT_C_IS_SPACE(pchLine[cchPattern - 1]))
        cchPattern--;
    while (cchPattern > 0 && RT_C_IS_SPACE(*pchLine))
        cchPattern--, pchLine++;

    const char *pchOptions = &pchLine[offColon + 1];
    while (cchOptions > 0 && RT_C_IS_SPACE(pchOptions[cchOptions - 1]))
        cchOptions--;
    while (cchOptions > 0 && RT_C_IS_SPACE(*pchOptions))
        cchOptions--, pchOptions++;

    /* Quietly ignore empty patterns and empty options. */
    if (!cchOptions || !cchPattern)
        return VINF_SUCCESS;

    /*
     * Prepair the pair and verify the option string.
     */
    uint32_t iPair = pSettings->cPairs;
    if ((iPair % 32) == 0)
    {
        void *pvNew = RTMemRealloc(pSettings->paPairs, (iPair + 32) * sizeof(pSettings->paPairs[0]));
        if (!pvNew)
            return VERR_NO_MEMORY;
        pSettings->paPairs = (PSCMPATRNOPTPAIR)pvNew;
    }

    pSettings->paPairs[iPair].pszPattern    = RTStrDupN(pchLine, cchPattern);
    pSettings->paPairs[iPair].pszOptions    = RTStrDupN(pchOptions, cchOptions);
    pSettings->paPairs[iPair].pszRelativeTo = RTStrDupN(pchDir, cchDir);
    int rc;
    if (   pSettings->paPairs[iPair].pszPattern
        && pSettings->paPairs[iPair].pszOptions
        && pSettings->paPairs[iPair].pszRelativeTo)
        rc = scmSettingsBaseVerifyString(pSettings->paPairs[iPair].pszOptions);
    else
        rc = VERR_NO_MEMORY;

    /*
     * If it checked out fine, expand any relative paths in the pattern.
     */
    if (RT_SUCCESS(rc))
    {
        size_t cPattern = 1;
        size_t cRelativePaths = 0;
        const char *pszSrc = pSettings->paPairs[iPair].pszPattern;
        for (;;)
        {
            if (*pszSrc == '/')
                cRelativePaths++;
            pszSrc = strchr(pszSrc, '|');
            if (!pszSrc)
                break;
            pszSrc++;
            cPattern++;
        }
        pSettings->paPairs[iPair].fMultiPattern = cPattern > 1;
        if (cRelativePaths > 0)
        {
            char *pszNewPattern = RTStrAlloc(cchPattern + cRelativePaths * (cchDir - 1) + 1);
            if (pszNewPattern)
            {
                char *pszDst = pszNewPattern;
                pszSrc = pSettings->paPairs[iPair].pszPattern;
                for (;;)
                {
                    if (*pszSrc == '/')
                    {
                        memcpy(pszDst, pchDir, cchDir);
                        pszDst += cchDir;
                        pszSrc += 1;
                    }

                    /* Look for the next relative path. */
                    const char *pszSrcNext = strchr(pszSrc, '|');
                    while (pszSrcNext && pszSrcNext[1] != '/')
                        pszSrcNext = strchr(pszSrcNext, '|');
                    if (!pszSrcNext)
                        break;

                    /* Copy stuff between current and the next path. */
                    pszSrcNext++;
                    memcpy(pszDst, pszSrc, pszSrcNext - pszSrc);
                    pszDst += pszSrcNext - pszSrc;
                    pszSrc = pszSrcNext;
                }

                /* Copy the final portion and replace the pattern. */
                strcpy(pszDst, pszSrc);

                RTStrFree(pSettings->paPairs[iPair].pszPattern);
                pSettings->paPairs[iPair].pszPattern = pszNewPattern;
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    if (RT_SUCCESS(rc))
        /*
         * Commit the pair.
         */
        pSettings->cPairs = iPair + 1;
    else
    {
        RTStrFree(pSettings->paPairs[iPair].pszPattern);
        RTStrFree(pSettings->paPairs[iPair].pszOptions);
        RTStrFree(pSettings->paPairs[iPair].pszRelativeTo);
    }
    return rc;
}

/**
 * Loads in the settings from @a pszFilename.
 *
 * @returns IPRT status code.
 * @param   pSettings           Where to load the settings file.
 * @param   pszFilename         The file to load.
 */
static int scmSettingsLoadFile(PSCMSETTINGS pSettings, const char *pszFilename)
{
    ScmVerbose(NULL, 3, "Loading settings file '%s'...\n", pszFilename);

    /* Turn filename into an absolute path and drop the filename. */
    char szAbsPath[RTPATH_MAX];
    int rc = RTPathAbs(pszFilename, szAbsPath, sizeof(szAbsPath));
    if (RT_FAILURE(rc))
    {
        RTMsgError("%s: RTPathAbs -> %Rrc\n", pszFilename, rc);
        return rc;
    }
    RTPathChangeToUnixSlashes(szAbsPath, true);
    size_t cchDir = RTPathFilename(szAbsPath) - &szAbsPath[0];

    /* Try open it.*/
    SCMSTREAM Stream;
    rc = ScmStreamInitForReading(&Stream, pszFilename);
    if (RT_SUCCESS(rc))
    {
        SCMEOL      enmEol;
        const char *pchLine;
        size_t      cchLine;
        while ((pchLine = ScmStreamGetLine(&Stream, &cchLine, &enmEol)) != NULL)
        {
            /* Ignore leading spaces. */
            while (cchLine > 0 && RT_C_IS_SPACE(*pchLine))
                pchLine++, cchLine--;

            /* Ignore empty lines and comment lines. */
            if (cchLine < 1 || *pchLine == '#')
                continue;

            /* Deal with escaped newlines. */
            size_t  iFirstLine  = ~(size_t)0;
            char   *pszFreeLine = NULL;
            if (   pchLine[cchLine - 1] == '\\'
                && (   cchLine < 2
                    || pchLine[cchLine - 2] != '\\') )
            {
                iFirstLine = ScmStreamTellLine(&Stream);

                cchLine--;
                while (cchLine > 0 && RT_C_IS_SPACE(pchLine[cchLine - 1]))
                    cchLine--;

                size_t cchTotal = cchLine;
                pszFreeLine = RTStrDupN(pchLine, cchLine);
                if (pszFreeLine)
                {
                    /* Append following lines. */
                    while ((pchLine = ScmStreamGetLine(&Stream, &cchLine, &enmEol)) != NULL)
                    {
                        while (cchLine > 0 && RT_C_IS_SPACE(*pchLine))
                            pchLine++, cchLine--;

                        bool const fDone = cchLine == 0
                                        || pchLine[cchLine - 1] != '\\'
                                        || (cchLine >= 2 && pchLine[cchLine - 2] == '\\');
                        if (!fDone)
                        {
                            cchLine--;
                            while (cchLine > 0 && RT_C_IS_SPACE(pchLine[cchLine - 1]))
                                cchLine--;
                        }

                        rc = RTStrRealloc(&pszFreeLine, cchTotal + 1 + cchLine + 1);
                        if (RT_FAILURE(rc))
                            break;
                        pszFreeLine[cchTotal++] = ' ';
                        memcpy(&pszFreeLine[cchTotal], pchLine, cchLine);
                        cchTotal += cchLine;
                        pszFreeLine[cchTotal] = '\0';

                        if (fDone)
                            break;
                    }
                }
                else
                    rc = VERR_NO_STR_MEMORY;

                if (RT_FAILURE(rc))
                {
                    RTStrFree(pszFreeLine);
                    rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: Ran out of memory deal with escaped newlines", pszFilename);
                    break;
                }

                pchLine = pszFreeLine;
                cchLine = cchTotal;
            }

            /* What kind of line is it? */
            const char *pchColon = (const char *)memchr(pchLine, ':', cchLine);
            if (pchColon)
                rc = scmSettingsAddPair(pSettings, pchLine, cchLine, pchColon - pchLine, szAbsPath, cchDir);
            else
                rc = scmSettingsBaseParseStringN(&pSettings->Base, pchLine, cchLine, szAbsPath, cchDir);
            if (pszFreeLine)
                RTStrFree(pszFreeLine);
            if (RT_FAILURE(rc))
            {
                RTMsgError("%s:%d: %Rrc\n",
                           pszFilename, iFirstLine == ~(size_t)0 ? ScmStreamTellLine(&Stream) : iFirstLine, rc);
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            rc = ScmStreamGetStatus(&Stream);
            if (RT_FAILURE(rc))
                RTMsgError("%s: ScmStreamGetStatus- > %Rrc\n", pszFilename, rc);
        }
        ScmStreamDelete(&Stream);
    }
    else
        RTMsgError("%s: ScmStreamInitForReading -> %Rrc\n", pszFilename, rc);
    return rc;
}

#if 0 /* unused */
/**
 * Parse the specified settings file creating a new settings struct from it.
 *
 * @returns IPRT status code
 * @param   ppSettings          Where to return the new settings.
 * @param   pszFilename         The file to parse.
 * @param   pSettingsBase       The base settings we inherit from.
 */
static int scmSettingsCreateFromFile(PSCMSETTINGS *ppSettings, const char *pszFilename, PCSCMSETTINGSBASE pSettingsBase)
{
    PSCMSETTINGS pSettings;
    int rc = scmSettingsCreate(&pSettings, pSettingsBase);
    if (RT_SUCCESS(rc))
    {
        rc = scmSettingsLoadFile(pSettings, pszFilename, RTPathFilename(pszFilename) - pszFilename);
        if (RT_SUCCESS(rc))
        {
            *ppSettings = pSettings;
            return VINF_SUCCESS;
        }

        scmSettingsDestroy(pSettings);
    }
    *ppSettings = NULL;
    return rc;
}
#endif


/**
 * Create an initial settings structure when starting processing a new file or
 * directory.
 *
 * This will look for .scm-settings files from the root and down to the
 * specified directory, combining them into the returned settings structure.
 *
 * @returns IPRT status code.
 * @param   ppSettings          Where to return the pointer to the top stack
 *                              object.
 * @param   pBaseSettings       The base settings we inherit from (globals
 *                              typically).
 * @param   pszPath             The absolute path to the new directory or file.
 */
static int scmSettingsCreateForPath(PSCMSETTINGS *ppSettings, PCSCMSETTINGSBASE pBaseSettings, const char *pszPath)
{
    *ppSettings = NULL;                 /* try shut up gcc. */

    /*
     * We'll be working with a stack copy of the path.
     */
    char    szFile[RTPATH_MAX];
    size_t  cchDir = strlen(pszPath);
    if (cchDir >= sizeof(szFile) - sizeof(SCM_SETTINGS_FILENAME))
        return VERR_FILENAME_TOO_LONG;

    /*
     * Create the bottom-most settings.
     */
    PSCMSETTINGS pSettings;
    int rc = scmSettingsCreate(&pSettings, pBaseSettings);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Enumerate the path components from the root and down. Load any setting
     * files we find.
     */
    size_t cComponents = RTPathCountComponents(pszPath);
    for (size_t i = 1; i <= cComponents; i++)
    {
        rc = RTPathCopyComponents(szFile, sizeof(szFile), pszPath, i);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szFile, sizeof(szFile), SCM_SETTINGS_FILENAME);
        if (RT_FAILURE(rc))
            break;
        RTPathChangeToUnixSlashes(szFile, true);

        if (RTFileExists(szFile))
        {
            rc = scmSettingsLoadFile(pSettings, szFile);
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (RT_SUCCESS(rc))
        *ppSettings = pSettings;
    else
        scmSettingsDestroy(pSettings);
    return rc;
}

/**
 * Pushes a new settings set onto the stack.
 *
 * @param   ppSettingsStack     The pointer to the pointer to the top stack
 *                              element.  This will be used as input and output.
 * @param   pSettings           The settings to push onto the stack.
 */
static void scmSettingsStackPush(PSCMSETTINGS *ppSettingsStack, PSCMSETTINGS pSettings)
{
    PSCMSETTINGS pOld = *ppSettingsStack;
    pSettings->pDown  = pOld;
    pSettings->pUp    = NULL;
    if (pOld)
        pOld->pUp = pSettings;
    *ppSettingsStack = pSettings;
}

/**
 * Pushes the settings of the specified directory onto the stack.
 *
 * We will load any .scm-settings in the directory.  A stack entry is added even
 * if no settings file was found.
 *
 * @returns IPRT status code.
 * @param   ppSettingsStack     The pointer to the pointer to the top stack
 *                              element.  This will be used as input and output.
 * @param   pszDir              The directory to do this for.
 */
static int scmSettingsStackPushDir(PSCMSETTINGS *ppSettingsStack, const char *pszDir)
{
    char szFile[RTPATH_MAX];
    int rc = RTPathJoin(szFile, sizeof(szFile), pszDir, SCM_SETTINGS_FILENAME);
    if (RT_SUCCESS(rc))
    {
        RTPathChangeToUnixSlashes(szFile, true);

        PSCMSETTINGS pSettings;
        rc = scmSettingsCreate(&pSettings, &(*ppSettingsStack)->Base);
        if (RT_SUCCESS(rc))
        {
            if (RTFileExists(szFile))
                rc = scmSettingsLoadFile(pSettings, szFile);
            if (RT_SUCCESS(rc))
            {
                scmSettingsStackPush(ppSettingsStack, pSettings);
                return VINF_SUCCESS;
            }

            scmSettingsDestroy(pSettings);
        }
    }
    return rc;
}


/**
 * Pops a settings set off the stack.
 *
 * @returns The popped settings.
 * @param   ppSettingsStack     The pointer to the pointer to the top stack
 *                              element.  This will be used as input and output.
 */
static PSCMSETTINGS scmSettingsStackPop(PSCMSETTINGS *ppSettingsStack)
{
    PSCMSETTINGS pRet = *ppSettingsStack;
    PSCMSETTINGS pNew = pRet ? pRet->pDown : NULL;
    *ppSettingsStack = pNew;
    if (pNew)
        pNew->pUp    = NULL;
    if (pRet)
    {
        pRet->pUp    = NULL;
        pRet->pDown  = NULL;
    }
    return pRet;
}

/**
 * Pops and destroys the top entry of the stack.
 *
 * @param   ppSettingsStack     The pointer to the pointer to the top stack
 *                              element.  This will be used as input and output.
 */
static void scmSettingsStackPopAndDestroy(PSCMSETTINGS *ppSettingsStack)
{
    scmSettingsDestroy(scmSettingsStackPop(ppSettingsStack));
}

/**
 * Constructs the base settings for the specified file name.
 *
 * @returns IPRT status code.
 * @param   pSettingsStack      The top element on the settings stack.
 * @param   pszFilename         The file name.
 * @param   pszBasename         The base name (pointer within @a pszFilename).
 * @param   cchBasename         The length of the base name.  (For passing to
 *                              RTStrSimplePatternMultiMatch.)
 * @param   pBase               Base settings to initialize.
 */
static int scmSettingsStackMakeFileBase(PCSCMSETTINGS pSettingsStack, const char *pszFilename,
                                        const char *pszBasename, size_t cchBasename, PSCMSETTINGSBASE pBase)
{
    ScmVerbose(NULL, 5, "scmSettingsStackMakeFileBase(%s, %.*s)\n", pszFilename, cchBasename, pszBasename);

    int rc = scmSettingsBaseInitAndCopy(pBase, &pSettingsStack->Base);
    if (RT_SUCCESS(rc))
    {
        /* find the bottom entry in the stack. */
        PCSCMSETTINGS pCur = pSettingsStack;
        while (pCur->pDown)
            pCur = pCur->pDown;

        /* Work our way up thru the stack and look for matching pairs. */
        while (pCur)
        {
            size_t const cPairs = pCur->cPairs;
            if (cPairs)
            {
                for (size_t i = 0; i < cPairs; i++)
                    if (   !pCur->paPairs[i].fMultiPattern
                        ?    RTStrSimplePatternNMatch(pCur->paPairs[i].pszPattern, RTSTR_MAX,
                                                      pszBasename,  cchBasename)
                          || RTStrSimplePatternMatch(pCur->paPairs[i].pszPattern, pszFilename)
                        :    RTStrSimplePatternMultiMatch(pCur->paPairs[i].pszPattern, RTSTR_MAX,
                                                          pszBasename,  cchBasename, NULL)
                          || RTStrSimplePatternMultiMatch(pCur->paPairs[i].pszPattern, RTSTR_MAX,
                                                          pszFilename,  RTSTR_MAX, NULL))
                    {
                        ScmVerbose(NULL, 5, "scmSettingsStackMakeFileBase: Matched '%s' : '%s'\n",
                                   pCur->paPairs[i].pszPattern, pCur->paPairs[i].pszOptions);
                        rc = scmSettingsBaseParseString(pBase, pCur->paPairs[i].pszOptions,
                                                        pCur->paPairs[i].pszRelativeTo, strlen(pCur->paPairs[i].pszRelativeTo));
                        if (RT_FAILURE(rc))
                            break;
                    }
                if (RT_FAILURE(rc))
                    break;
            }

            /* advance */
            pCur = pCur->pUp;
        }
    }
    if (RT_FAILURE(rc))
        scmSettingsBaseDelete(pBase);
    return rc;
}


/* -=-=-=-=-=- misc -=-=-=-=-=- */


/**
 * Prints the per file banner needed and the message level is high enough.
 *
 * @param   pState              The rewrite state.
 * @param   iLevel              The required verbosity level.
 */
void ScmVerboseBanner(PSCMRWSTATE pState, int iLevel)
{
    if (iLevel <= g_iVerbosity && !pState->fFirst)
    {
        RTPrintf("%s: info: --= Rewriting '%s' =--\n", g_szProgName, pState->pszFilename);
        pState->fFirst = true;
    }
}


/**
 * Prints a verbose message if the level is high enough.
 *
 * @param   pState              The rewrite state.  Optional.
 * @param   iLevel              The required verbosity level.
 * @param   pszFormat           The message format string.  Can be NULL if we
 *                              only want to trigger the per file message.
 * @param   ...                 Format arguments.
 */
void ScmVerbose(PSCMRWSTATE pState, int iLevel, const char *pszFormat, ...)
{
    if (iLevel <= g_iVerbosity)
    {
        if (pState && !pState->fFirst)
        {
            RTPrintf("%s: info: --= Rewriting '%s' =--\n", g_szProgName, pState->pszFilename);
            pState->fFirst = true;
        }
        RTPrintf(pState
                 ? "%s: info:   "
                 : "%s: info: ",
                 g_szProgName);
        va_list va;
        va_start(va, pszFormat);
        RTPrintfV(pszFormat, va);
        va_end(va);
    }
}


/**
 * Prints an error message.
 *
 * @returns kScmUnmodified
 * @param   pState              The rewrite state.  Optional.
 * @param   rc                  The error code.
 * @param   pszFormat           The message format string.
 * @param   ...                 Format arguments.
 */
SCMREWRITERRES ScmError(PSCMRWSTATE pState, int rc, const char *pszFormat, ...)
{
    if (RT_SUCCESS(pState->rc))
        pState->rc = rc;

    if (!pState->fFirst)
    {
        RTPrintf("%s: info: --= Rewriting '%s' =--\n", g_szProgName, pState->pszFilename);
        pState->fFirst = true;
    }
    va_list va;
    va_start(va, pszFormat);
    RTPrintf("%s: error: %s: %N", g_szProgName, pState->pszFilename, pszFormat, &va);
    va_end(va);

    return kScmUnmodified;
}


/**
 * Prints message indicating that something requires manual fixing.
 *
 * @returns false
 * @param   pState              The rewrite state.  Optional.
 * @param   rc                  The error code.
 * @param   pszFormat           The message format string.
 * @param   ...                 Format arguments.
 */
bool ScmFixManually(PSCMRWSTATE pState, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    ScmFixManuallyV(pState, pszFormat, va);
    va_end(va);
    return false;
}


/**
 * Prints message indicating that something requires manual fixing.
 *
 * @returns false
 * @param   pState              The rewrite state.  Optional.
 * @param   rc                  The error code.
 * @param   pszFormat           The message format string.
 * @param   va                  Format arguments.
 */
bool ScmFixManuallyV(PSCMRWSTATE pState, const char *pszFormat, va_list va)
{
    pState->fNeedsManualRepair = true;

    if (!pState->fFirst)
    {
        RTPrintf("%s: info: --= Rewriting '%s' =--\n", g_szProgName, pState->pszFilename);
        pState->fFirst = true;
    }
    va_list vaCopy;
    va_copy(vaCopy, va);
    RTPrintf("%s: error/fixme: %s: %N", g_szProgName, pState->pszFilename, pszFormat, &vaCopy);
    va_end(vaCopy);

    return false;
}


/* -=-=-=-=-=- file and directory processing -=-=-=-=-=- */


/**
 * Processes a file.
 *
 * @returns IPRT status code.
 * @param   pState              The rewriter state.
 * @param   pszFilename         The file name.
 * @param   pszBasename         The base name (pointer within @a pszFilename).
 * @param   cchBasename         The length of the base name.  (For passing to
 *                              RTStrSimplePatternMultiMatch.)
 * @param   pBaseSettings       The base settings to use.  It's OK to modify
 *                              these.
 */
static int scmProcessFileInner(PSCMRWSTATE pState, const char *pszFilename, const char *pszBasename, size_t cchBasename,
                               PSCMSETTINGSBASE pBaseSettings)
{
    /*
     * Do the file level filtering.
     */
    if (   pBaseSettings->pszFilterFiles
        && *pBaseSettings->pszFilterFiles
        && !RTStrSimplePatternMultiMatch(pBaseSettings->pszFilterFiles, RTSTR_MAX, pszBasename, cchBasename, NULL))
    {
        ScmVerbose(NULL, 5, "skipping '%s': file filter mismatch\n", pszFilename);
        g_cFilesSkipped++;
        return VINF_SUCCESS;
    }
    if (   pBaseSettings->pszFilterOutFiles
        && *pBaseSettings->pszFilterOutFiles
        && (   RTStrSimplePatternMultiMatch(pBaseSettings->pszFilterOutFiles, RTSTR_MAX, pszBasename, cchBasename, NULL)
            || RTStrSimplePatternMultiMatch(pBaseSettings->pszFilterOutFiles, RTSTR_MAX, pszFilename, RTSTR_MAX, NULL)) )
    {
        ScmVerbose(NULL, 5, "skipping '%s': filterd out\n", pszFilename);
        g_cFilesSkipped++;
        return VINF_SUCCESS;
    }
    if (   pBaseSettings->fOnlySvnFiles
        && !ScmSvnIsInWorkingCopy(pState))
    {
        ScmVerbose(NULL, 5, "skipping '%s': not in SVN WC\n", pszFilename);
        g_cFilesNotInSvn++;
        return VINF_SUCCESS;
    }

    /*
     * Create an input stream from the file and check that it's text.
     */
    SCMSTREAM Stream1;
    int rc = ScmStreamInitForReading(&Stream1, pszFilename);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Failed to read '%s': %Rrc\n", pszFilename, rc);
        return rc;
    }
    bool const fIsText = ScmStreamIsText(&Stream1);

    /*
     * Try find a matching rewrite config for this filename.
     */
    PCSCMCFGENTRY pCfg = pBaseSettings->pTreatAs;
    if (!pCfg)
    {
        for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
            if (RTStrSimplePatternMultiMatch(g_aConfigs[iCfg].pszFilePattern, RTSTR_MAX, pszBasename, cchBasename, NULL))
            {
                pCfg = &g_aConfigs[iCfg];
                break;
            }
        if (!pCfg)
        {
            /* On failure try check for hash-bang stuff before giving up. */
            if (fIsText)
            {
                SCMEOL      enmIgn;
                size_t      cchFirst;
                const char *pchFirst = ScmStreamGetLine(&Stream1, &cchFirst, &enmIgn);
                if (cchFirst >= 9 && pchFirst && *pchFirst == '#')
                {
                    do
                    {
                        pchFirst++;
                        cchFirst--;
                    } while (cchFirst > 0 && RT_C_IS_BLANK(*pchFirst));
                    if (*pchFirst == '!')
                    {
                        do
                        {
                            pchFirst++;
                            cchFirst--;
                        } while (cchFirst > 0 && RT_C_IS_BLANK(*pchFirst));
                        const char *pszTreatAs = NULL;
                        if (   (cchFirst >= 7 && strncmp(pchFirst, "/bin/sh", 7) == 0)
                            || (cchFirst >= 9 && strncmp(pchFirst, "/bin/bash", 9) == 0)
                            || (cchFirst >= 4+9 && strncmp(pchFirst, "/usr/bin/bash", 4+9) == 0) )
                            pszTreatAs = "shell";
                        else if (   (cchFirst >= 15 && strncmp(pchFirst, "/usr/bin/python", 15) == 0)
                                 || (cchFirst >= 19 && strncmp(pchFirst, "/usr/bin/env python", 19) == 0) )
                            pszTreatAs = "python";
                        else if (   (cchFirst >= 13 && strncmp(pchFirst, "/usr/bin/perl", 13) == 0)
                                 || (cchFirst >= 17 && strncmp(pchFirst, "/usr/bin/env perl", 17) == 0) )
                            pszTreatAs = "perl";
                        if (pszTreatAs)
                        {
                            for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
                                if (strcmp(pszTreatAs, g_aConfigs[iCfg].pszName) == 0)
                                {
                                    pCfg = &g_aConfigs[iCfg];
                                    break;
                                }
                            Assert(pCfg);
                        }
                    }
                }
                ScmStreamRewindForReading(&Stream1);
            }
            if (!pCfg)
            {
                ScmVerbose(NULL, 2, "skipping '%s': no rewriters configured\n", pszFilename);
                g_cFilesNoRewriters++;
                ScmStreamDelete(&Stream1);
                return VINF_SUCCESS;
            }
        }
        ScmVerbose(pState, 4, "matched \"%s\" (%s)\n", pCfg->pszFilePattern, pCfg->pszName);
    }
    else
        ScmVerbose(pState, 4, "treat-as \"%s\"\n", pCfg->pszName);

    if (fIsText || pCfg->fBinary)
    {
        ScmVerboseBanner(pState, 3);

        /*
         * Gather SCM and editor settings from the stream.
         */
        rc = scmSettingsBaseLoadFromDocument(pBaseSettings, &Stream1);
        if (RT_SUCCESS(rc))
        {
            ScmStreamRewindForReading(&Stream1);

            /*
             * Create two more streams for output and push the text thru all the
             * rewriters, switching the two streams around when something is
             * actually rewritten.  Stream1 remains unchanged.
             */
            SCMSTREAM Stream2;
            rc = ScmStreamInitForWriting(&Stream2, &Stream1);
            if (RT_SUCCESS(rc))
            {
                SCMSTREAM Stream3;
                rc = ScmStreamInitForWriting(&Stream3, &Stream1);
                if (RT_SUCCESS(rc))
                {
                    bool        fModified = false;
                    PSCMSTREAM  pIn       = &Stream1;
                    PSCMSTREAM  pOut      = &Stream2;
                    for (size_t iRw = 0; iRw < pCfg->cRewriters; iRw++)
                    {
                        pState->rc = VINF_SUCCESS;
                        SCMREWRITERRES enmRes = pCfg->paRewriters[iRw]->pfnRewriter(pState, pIn, pOut, pBaseSettings);
                        if (RT_FAILURE(pState->rc))
                            break;
                        if (enmRes == kScmMaybeModified)
                            enmRes = ScmStreamAreIdentical(pIn, pOut) ? kScmUnmodified : kScmModified;
                        if (enmRes == kScmModified)
                        {
                            PSCMSTREAM pTmp = pOut;
                            pOut = pIn == &Stream1 ? &Stream3 : pIn;
                            pIn  = pTmp;
                            fModified = true;
                        }

                        ScmStreamRewindForReading(pIn);
                        ScmStreamRewindForWriting(pOut);
                    }

                    rc = pState->rc;
                    if (RT_SUCCESS(rc))
                    {
                        rc = ScmStreamGetStatus(&Stream1);
                        if (RT_SUCCESS(rc))
                            rc = ScmStreamGetStatus(&Stream2);
                        if (RT_SUCCESS(rc))
                            rc = ScmStreamGetStatus(&Stream3);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * If rewritten, write it back to disk.
                             */
                            if (fModified && !pCfg->fBinary)
                            {
                                if (!g_fDryRun)
                                {
                                    ScmVerbose(pState, 1, "writing modified file to \"%s%s\"\n", pszFilename, g_pszChangedSuff);
                                    rc = ScmStreamWriteToFile(pIn, "%s%s", pszFilename, g_pszChangedSuff);
                                    if (RT_FAILURE(rc))
                                        RTMsgError("Error writing '%s%s': %Rrc\n", pszFilename, g_pszChangedSuff, rc);
                                }
                                else
                                {
                                    ScmVerboseBanner(pState, 1);
                                    ScmDiffStreams(pszFilename, &Stream1, pIn, g_fDiffIgnoreEol,
                                                   g_fDiffIgnoreLeadingWS, g_fDiffIgnoreTrailingWS, g_fDiffSpecialChars,
                                                   pBaseSettings->cchTab, g_pStdOut);
                                    ScmVerbose(pState, 2, "would have modified the file \"%s%s\"\n",
                                               pszFilename, g_pszChangedSuff);
                                }
                                g_cFilesModified++;
                            }
                            else if (fModified)
                                rc = RTMsgErrorRc(VERR_INTERNAL_ERROR, "Rewriters modified binary file! Impossible!");

                            /*
                             * If pending SVN property changes, apply them.
                             */
                            if (pState->cSvnPropChanges && RT_SUCCESS(rc))
                            {
                                if (!g_fDryRun)
                                {
                                    rc = ScmSvnApplyChanges(pState);
                                    if (RT_FAILURE(rc))
                                        RTMsgError("%s: failed to apply SVN property changes (%Rrc)\n", pszFilename, rc);
                                }
                                else
                                    ScmSvnDisplayChanges(pState);
                                if (!fModified)
                                    g_cFilesModified++;
                            }

                            if (!fModified && !pState->cSvnPropChanges)
                                ScmVerbose(pState, 3, "%s: no change\n", pszFilename);
                        }
                        else
                            RTMsgError("%s: stream error %Rrc\n", pszFilename, rc);
                    }
                    ScmStreamDelete(&Stream3);
                }
                else
                    RTMsgError("Failed to init stream for writing: %Rrc\n", rc);
                ScmStreamDelete(&Stream2);
            }
            else
                RTMsgError("Failed to init stream for writing: %Rrc\n", rc);
        }
        else
            RTMsgError("scmSettingsBaseLoadFromDocument: %Rrc\n", rc);
    }
    else
    {
        ScmVerbose(pState, 2, "not text file: \"%s\"\n", pszFilename);
        g_cFilesBinaries++;
    }
    ScmStreamDelete(&Stream1);

    return rc;
}

/**
 * Processes a file.
 *
 * This is just a wrapper for scmProcessFileInner for avoid wasting stack in the
 * directory recursion method.
 *
 * @returns IPRT status code.
 * @param   pszFilename         The file name.
 * @param   pszBasename         The base name (pointer within @a pszFilename).
 * @param   cchBasename         The length of the base name.  (For passing to
 *                              RTStrSimplePatternMultiMatch.)
 * @param   pSettingsStack      The settings stack (pointer to the top element).
 */
static int scmProcessFile(const char *pszFilename, const char *pszBasename, size_t cchBasename,
                          PSCMSETTINGS pSettingsStack)
{
    SCMSETTINGSBASE Base;
    int rc = scmSettingsStackMakeFileBase(pSettingsStack, pszFilename, pszBasename, cchBasename, &Base);
    if (RT_SUCCESS(rc))
    {
        SCMRWSTATE State;
        State.pszFilename           = pszFilename;
        State.fFirst                = false;
        State.fNeedsManualRepair    = false;
        State.fIsInSvnWorkingCopy   = 0;
        State.cSvnPropChanges       = 0;
        State.paSvnPropChanges      = NULL;
        State.rc                    = VINF_SUCCESS;

        rc = scmProcessFileInner(&State, pszFilename, pszBasename, cchBasename, &Base);

        size_t i = State.cSvnPropChanges;
        while (i-- > 0)
        {
            RTStrFree(State.paSvnPropChanges[i].pszName);
            RTStrFree(State.paSvnPropChanges[i].pszValue);
        }
        RTMemFree(State.paSvnPropChanges);

        scmSettingsBaseDelete(&Base);

        if (State.fNeedsManualRepair)
            g_cFilesRequiringManualFixing++;
        g_cFilesProcessed++;
    }
    return rc;
}

/**
 * Tries to correct RTDIRENTRY_UNKNOWN.
 *
 * @returns Corrected type.
 * @param   pszPath             The path to the object in question.
 */
static RTDIRENTRYTYPE scmFigureUnknownType(const char *pszPath)
{
    RTFSOBJINFO Info;
    int rc = RTPathQueryInfo(pszPath, &Info, RTFSOBJATTRADD_NOTHING);
    if (RT_FAILURE(rc))
        return RTDIRENTRYTYPE_UNKNOWN;
    if (RTFS_IS_DIRECTORY(Info.Attr.fMode))
        return RTDIRENTRYTYPE_DIRECTORY;
    if (RTFS_IS_FILE(Info.Attr.fMode))
        return RTDIRENTRYTYPE_FILE;
    return RTDIRENTRYTYPE_UNKNOWN;
}

/**
 * Recurse into a sub-directory and process all the files and directories.
 *
 * @returns IPRT status code.
 * @param   pszBuf              Path buffer containing the directory path on
 *                              entry.  This ends with a dot.  This is passed
 *                              along when recursing in order to save stack space
 *                              and avoid needless copying.
 * @param   cchDir              Length of our path in pszbuf.
 * @param   pEntry              Directory entry buffer.  This is also passed
 *                              along when recursing to save stack space.
 * @param   pSettingsStack      The settings stack (pointer to the top element).
 * @param   iRecursion          The recursion depth.  This is used to restrict
 *                              the recursions.
 */
static int scmProcessDirTreeRecursion(char *pszBuf, size_t cchDir, PRTDIRENTRY pEntry,
                                      PSCMSETTINGS pSettingsStack, unsigned iRecursion)
{
    int rc;
    Assert(cchDir > 1 && pszBuf[cchDir - 1] == '.');

    /*
     * Make sure we stop somewhere.
     */
    if (iRecursion > 128)
    {
        RTMsgError("recursion too deep: %d\n", iRecursion);
        return VINF_SUCCESS; /* ignore */
    }

    /*
     * Check if it's excluded by --only-svn-dir.
     */
    if (pSettingsStack->Base.fOnlySvnDirs)
    {
        if (!ScmSvnIsDirInWorkingCopy(pszBuf))
            return VINF_SUCCESS;
    }
    g_cDirsProcessed++;

    /*
     * Try open and read the directory.
     */
    RTDIR hDir;
    rc = RTDirOpenFiltered(&hDir, pszBuf, RTDIRFILTER_NONE, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Failed to enumerate directory '%s': %Rrc", pszBuf, rc);
        return rc;
    }
    for (;;)
    {
        /* Read the next entry. */
        rc = RTDirRead(hDir, pEntry, NULL);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_NO_MORE_FILES)
                rc = VINF_SUCCESS;
            else
                RTMsgError("RTDirRead -> %Rrc\n", rc);
            break;
        }

        /* Skip '.' and '..'. */
        if (    pEntry->szName[0] == '.'
            &&  (   pEntry->cbName == 1
                 || (   pEntry->cbName == 2
                     && pEntry->szName[1] == '.')))
            continue;

        /* Enter it into the buffer so we've got a full name to work
           with when needed. */
        if (pEntry->cbName + cchDir >= RTPATH_MAX)
        {
            RTMsgError("Skipping too long entry: %s", pEntry->szName);
            continue;
        }
        memcpy(&pszBuf[cchDir - 1], pEntry->szName, pEntry->cbName + 1);

        /* Figure the type. */
        RTDIRENTRYTYPE enmType = pEntry->enmType;
        if (enmType == RTDIRENTRYTYPE_UNKNOWN)
            enmType = scmFigureUnknownType(pszBuf);

        /* Process the file or directory, skip the rest. */
        if (enmType == RTDIRENTRYTYPE_FILE)
            rc = scmProcessFile(pszBuf, pEntry->szName, pEntry->cbName, pSettingsStack);
        else if (enmType == RTDIRENTRYTYPE_DIRECTORY)
        {
            /* Append the dot for the benefit of the pattern matching. */
            if (pEntry->cbName + cchDir + 5 >= RTPATH_MAX)
            {
                RTMsgError("Skipping too deep dir entry: %s", pEntry->szName);
                continue;
            }
            memcpy(&pszBuf[cchDir - 1 + pEntry->cbName], "/.", sizeof("/."));
            size_t cchSubDir = cchDir - 1 + pEntry->cbName + sizeof("/.") - 1;

            if (   !pSettingsStack->Base.pszFilterOutDirs
                || !*pSettingsStack->Base.pszFilterOutDirs
                || (   !RTStrSimplePatternMultiMatch(pSettingsStack->Base.pszFilterOutDirs, RTSTR_MAX,
                                                     pEntry->szName, pEntry->cbName, NULL)
                    && !RTStrSimplePatternMultiMatch(pSettingsStack->Base.pszFilterOutDirs, RTSTR_MAX,
                                                     pszBuf, cchSubDir, NULL)
                   )
               )
            {
                rc = scmSettingsStackPushDir(&pSettingsStack, pszBuf);
                if (RT_SUCCESS(rc))
                {
                    rc = scmProcessDirTreeRecursion(pszBuf, cchSubDir, pEntry, pSettingsStack, iRecursion + 1);
                    scmSettingsStackPopAndDestroy(&pSettingsStack);
                }
            }
        }
        if (RT_FAILURE(rc))
            break;
    }
    RTDirClose(hDir);
    return rc;

}

/**
 * Process a directory tree.
 *
 * @returns IPRT status code.
 * @param   pszDir              The directory to start with.  This is pointer to
 *                              a RTPATH_MAX sized buffer.
 */
static int scmProcessDirTree(char *pszDir, PSCMSETTINGS pSettingsStack)
{
    /*
     * Setup the recursion.
     */
    int rc = RTPathAppend(pszDir, RTPATH_MAX, ".");
    if (RT_SUCCESS(rc))
    {
        RTPathChangeToUnixSlashes(pszDir, true);

        RTDIRENTRY Entry;
        rc = scmProcessDirTreeRecursion(pszDir, strlen(pszDir), &Entry, pSettingsStack, 0);
    }
    else
        RTMsgError("RTPathAppend: %Rrc\n", rc);
    return rc;
}


/**
 * Processes a file or directory specified as an command line argument.
 *
 * @returns IPRT status code
 * @param   pszSomething        What we found in the command line arguments.
 * @param   pSettingsStack      The settings stack (pointer to the top element).
 */
static int scmProcessSomething(const char *pszSomething, PSCMSETTINGS pSettingsStack)
{
    char szBuf[RTPATH_MAX];
    int rc = RTPathAbs(pszSomething, szBuf, sizeof(szBuf));
    if (RT_SUCCESS(rc))
    {
        RTPathChangeToUnixSlashes(szBuf, false /*fForce*/);

        PSCMSETTINGS pSettings;
        rc = scmSettingsCreateForPath(&pSettings, &pSettingsStack->Base, szBuf);
        if (RT_SUCCESS(rc))
        {
            scmSettingsStackPush(&pSettingsStack, pSettings);

            if (RTFileExists(szBuf))
            {
                const char *pszBasename = RTPathFilename(szBuf);
                if (pszBasename)
                {
                    size_t cchBasename = strlen(pszBasename);
                    rc = scmProcessFile(szBuf, pszBasename, cchBasename, pSettingsStack);
                }
                else
                {
                    RTMsgError("RTPathFilename: NULL\n");
                    rc = VERR_IS_A_DIRECTORY;
                }
            }
            else
                rc = scmProcessDirTree(szBuf, pSettingsStack);

            PSCMSETTINGS pPopped = scmSettingsStackPop(&pSettingsStack);
            Assert(pPopped == pSettings); RT_NOREF_PV(pPopped);
            scmSettingsDestroy(pSettings);
        }
        else
            RTMsgError("scmSettingsInitStack: %Rrc\n", rc);
    }
    else
        RTMsgError("RTPathAbs: %Rrc\n", rc);
    return rc;
}

/**
 * Print some stats.
 */
static void scmPrintStats(void)
{
    ScmVerbose(NULL, 0,
               g_fDryRun
               ? "%u out of %u file%s in %u dir%s would be modified (%u without rewriter%s, %u binar%s, %u not in svn, %u skipped)\n"
               : "%u out of %u file%s in %u dir%s was modified (%u without rewriter%s, %u binar%s, %u not in svn, %u skipped)\n",
               g_cFilesModified,
               g_cFilesProcessed, g_cFilesProcessed == 1 ? "" : "s",
               g_cDirsProcessed,  g_cDirsProcessed == 1 ? "" : "s",
               g_cFilesNoRewriters, g_cFilesNoRewriters == 1 ? "" : "s",
               g_cFilesBinaries,  g_cFilesBinaries == 1 ? "y" : "ies",
               g_cFilesNotInSvn, g_cFilesSkipped);
}

/**
 * Display the rewriter actions.
 *
 * @returns RTEXITCODE_SUCCESS.
 */
static int scmHelpActions(void)
{
    RTPrintf("Available rewriter actions:\n");
    for (uint32_t i = 0; i < RT_ELEMENTS(g_papRewriterActions); i++)
        RTPrintf("  %s\n", g_papRewriterActions[i]->pszName);
    return RTEXITCODE_SUCCESS;
}

/**
 * Display the default configuration.
 *
 * @returns RTEXITCODE_SUCCESS.
 */
static int scmHelpConfig(void)
{
    RTPrintf("Rewriter configuration:\n");
    for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
    {
        RTPrintf("\n  %s%s - %s:\n",
                 g_aConfigs[iCfg].pszName, g_aConfigs[iCfg].fBinary ? " (binary)" : "", g_aConfigs[iCfg].pszFilePattern);
        for (size_t i = 0; i < g_aConfigs[iCfg].cRewriters; i++)
            RTPrintf("    %s\n", g_aConfigs[iCfg].paRewriters[i]->pszName);
    }
    return RTEXITCODE_SUCCESS;
}

/**
 * Display the primary help text.
 *
 * @returns RTEXITCODE_SUCCESS.
 * @param   paOpts              Options.
 * @param   cOpts               Number of options.
 */
static int scmHelp(PCRTGETOPTDEF paOpts, size_t cOpts)
{
    RTPrintf("VirtualBox Source Code Massager\n"
             "\n"
             "Usage: %s [options] <files & dirs>\n"
             "\n"
             "General options:\n", g_szProgName);
    for (size_t i = 0; i < cOpts; i++)
    {
        /* Grouping. */
        switch (paOpts[i].iShort)
        {
            case SCMOPT_DIFF_IGNORE_EOL:
                RTPrintf("\nDiff options (dry runs):\n");
                break;
            case SCMOPT_CONVERT_EOL:
                RTPrintf("\nRewriter action options:\n");
                break;
            case SCMOPT_ONLY_SVN_DIRS:
                RTPrintf("\nInput selection options:\n");
                break;
            case SCMOPT_TREAT_AS:
                RTPrintf("\nMisc options:\n");
                break;
        }

        size_t cExtraAdvance = 0;
        if ((paOpts[i].fFlags & RTGETOPT_REQ_MASK) == RTGETOPT_REQ_NOTHING)
        {
            cExtraAdvance = i + 1 < cOpts
                         && (   strstr(paOpts[i+1].pszLong, "-no-") != NULL
                             || strstr(paOpts[i+1].pszLong, "-not-") != NULL
                             || strstr(paOpts[i+1].pszLong, "-dont-") != NULL
                             || strstr(paOpts[i+1].pszLong, "-unrestricted-") != NULL
                             || (paOpts[i].iShort == 'q' && paOpts[i+1].iShort == 'v')
                             || (paOpts[i].iShort == 'd' && paOpts[i+1].iShort == 'D')
                            );
            if (cExtraAdvance)
                RTPrintf("  %s, %s\n", paOpts[i].pszLong, paOpts[i + 1].pszLong);
            else if (paOpts[i].iShort != SCMOPT_NO_UPDATE_LICENSE || i + 6 >= cOpts /* paranoia */)
                RTPrintf("  %s\n", paOpts[i].pszLong);
            else
            {
                RTPrintf("  %s,\n"
                         "  %s,\n"
                         "  %s,\n"
                         "  %s,\n"
                         "  %s,\n"
                         "  %s,\n"
                         "  %s\n",
                         paOpts[i].pszLong,
                         paOpts[i + 1].pszLong,
                         paOpts[i + 2].pszLong,
                         paOpts[i + 3].pszLong,
                         paOpts[i + 4].pszLong,
                         paOpts[i + 5].pszLong,
                         paOpts[i + 6].pszLong);
                cExtraAdvance = 6;
            }
        }
        else if ((paOpts[i].fFlags & RTGETOPT_REQ_MASK) == RTGETOPT_REQ_STRING)
            switch (paOpts[i].iShort)
            {
                case SCMOPT_DEL_ACTION:
                    RTPrintf("  %s pattern\n", paOpts[i].pszLong);
                    break;
                case SCMOPT_FILTER_OUT_DIRS:
                case SCMOPT_FILTER_FILES:
                case SCMOPT_FILTER_OUT_FILES:
                    RTPrintf("  %s multi-pattern\n", paOpts[i].pszLong);
                    break;
                default:
                    RTPrintf("  %s string\n", paOpts[i].pszLong);
            }
        else
            RTPrintf("  %s value\n", paOpts[i].pszLong);
        switch (paOpts[i].iShort)
        {
            case 'd':
            case 'D':                           RTPrintf("      Default: --dry-run\n"); break;
            case SCMOPT_CHECK_RUN:              RTPrintf("      Default: --dry-run\n"); break;
            case 'f':                           RTPrintf("      Default: none\n"); break;
            case 'q':
            case 'v':                           RTPrintf("      Default: -vv\n"); break;
            case SCMOPT_HELP_CONFIG:            RTPrintf("      Shows the standard file rewriter configurations.\n"); break;
            case SCMOPT_HELP_ACTIONS:           RTPrintf("      Shows the available rewriter actions.\n"); break;

            case SCMOPT_DIFF_IGNORE_EOL:        RTPrintf("      Default: false\n"); break;
            case SCMOPT_DIFF_IGNORE_SPACE:      RTPrintf("      Default: false\n"); break;
            case SCMOPT_DIFF_IGNORE_LEADING_SPACE:  RTPrintf("      Default: false\n"); break;
            case SCMOPT_DIFF_IGNORE_TRAILING_SPACE: RTPrintf("      Default: false\n"); break;
            case SCMOPT_DIFF_SPECIAL_CHARS:     RTPrintf("      Default: true\n"); break;

            case SCMOPT_CONVERT_EOL:            RTPrintf("      Default: %RTbool\n", g_Defaults.fConvertEol); break;
            case SCMOPT_CONVERT_TABS:           RTPrintf("      Default: %RTbool\n", g_Defaults.fConvertTabs); break;
            case SCMOPT_FORCE_FINAL_EOL:        RTPrintf("      Default: %RTbool\n", g_Defaults.fForceFinalEol); break;
            case SCMOPT_FORCE_TRAILING_LINE:    RTPrintf("      Default: %RTbool\n", g_Defaults.fForceTrailingLine); break;
            case SCMOPT_STRIP_TRAILING_BLANKS:  RTPrintf("      Default: %RTbool\n", g_Defaults.fStripTrailingBlanks); break;
            case SCMOPT_STRIP_TRAILING_LINES:   RTPrintf("      Default: %RTbool\n", g_Defaults.fStripTrailingLines); break;
            case SCMOPT_FIX_FLOWER_BOX_MARKERS: RTPrintf("      Default: %RTbool\n", g_Defaults.fFixFlowerBoxMarkers); break;
            case SCMOPT_MIN_BLANK_LINES_BEFORE_FLOWER_BOX_MARKERS: RTPrintf("      Default: %u\n", g_Defaults.cMinBlankLinesBeforeFlowerBoxMakers); break;

            case SCMOPT_FIX_HEADER_GUARDS:
                RTPrintf("      Fix header guards and #pragma once.  Default: %RTbool\n", g_Defaults.fFixHeaderGuards);
                break;
            case SCMOPT_PRAGMA_ONCE:
                RTPrintf("      Whether to include #pragma once with the header guard.  Default: %RTbool\n", g_Defaults.fPragmaOnce);
                break;
            case SCMOPT_FIX_HEADER_GUARD_ENDIF:
                RTPrintf("      Whether to fix the #endif of a header guard.  Default: %RTbool\n", g_Defaults.fFixHeaderGuardEndif);
                break;
            case SCMOPT_ENDIF_GUARD_COMMENT:
                RTPrintf("      Put a comment on the header guard #endif or not.  Default: %RTbool\n", g_Defaults.fEndifGuardComment);
                break;
            case SCMOPT_GUARD_RELATIVE_TO_DIR:
                RTPrintf("      Header guard should be normalized relative to given dir.\n"
                         "      When relative to settings files, no preceding slash.\n"
                         "      Header relative directory specification: {dir} and {parent}\n"
                         "      If empty no normalization takes place.  Default: '%s'\n", g_Defaults.pszGuardRelativeToDir);
                break;
            case SCMOPT_GUARD_PREFIX:
                RTPrintf("      Prefix to use with --guard-relative-to-dir.  Default: %s\n", g_Defaults.pszGuardPrefix);
                break;
            case SCMOPT_FIX_TODOS:
                RTPrintf("      Fix @todo statements so doxygen sees them.  Default: %RTbool\n", g_Defaults.fFixTodos);
                break;
            case SCMOPT_FIX_ERR_H:
                RTPrintf("      Fix err.h/errcore.h usage.  Default: %RTbool\n", g_Defaults.fFixErrH);
                break;
            case SCMOPT_ONLY_GUEST_HOST_PAGE:
                RTPrintf("      No PAGE_SIZE, PAGE_SHIFT or PAGE_OFFSET_MASK allowed, must have\n"
                         "      GUEST_ or HOST_ prefix.  Also forbids use of PAGE_BASE_MASK,\n"
                         "      PAGE_BASE_HC_MASK, PAGE_BASE_GC_MASK, PAGE_ADDRESS,\n"
                         "      PHYS_PAGE_ADDRESS.  Default: %RTbool\n", g_Defaults.fOnlyGuestHostPage);
                break;
            case SCMOPT_NO_ASM_MEM_PAGE_USE:
                RTPrintf("      No ASMMemIsZeroPage or ASMMemZeroPage allowed, must instead use\n"
                         "      ASMMemIsZero and RT_BZERO with appropriate page size.  Default: %RTbool\n",
                         g_Defaults.fNoASMMemPageUse);
                break;
            case SCMOPT_NO_RC_USE:
                RTPrintf("      No rc declaration allowed, must instead use\n"
                         "      vrc for IPRT status codes and hrc for COM status codes.  Default: %RTbool\n",
                         g_Defaults.fOnlyHrcVrcInsteadOfRc);
                break;
            case SCMOPT_STANDARIZE_KMK:
                RTPrintf("      Clean up kmk files (the makefile-kmk action).  Default: %RTbool\n", g_Defaults.fStandarizeKmk);
                break;
            case SCMOPT_UPDATE_COPYRIGHT_YEAR:
                RTPrintf("      Update the copyright year.  Default: %RTbool\n", g_Defaults.fUpdateCopyrightYear);
                break;
            case SCMOPT_EXTERNAL_COPYRIGHT:
                RTPrintf("      Only external copyright holders.  Default: %RTbool\n", g_Defaults.fExternalCopyright);
                break;
            case SCMOPT_NO_UPDATE_LICENSE:
                RTPrintf("      License selection.  Default: --license-ose-gpl\n");
                break;

            case SCMOPT_LGPL_DISCLAIMER:
                RTPrintf("      Include LGPL version disclaimer.  Default: --no-lgpl-disclaimer\n");
                break;

            case SCMOPT_SET_SVN_EOL:            RTPrintf("      Default: %RTbool\n", g_Defaults.fSetSvnEol); break;
            case SCMOPT_SET_SVN_EXECUTABLE:     RTPrintf("      Default: %RTbool\n", g_Defaults.fSetSvnExecutable); break;
            case SCMOPT_SET_SVN_KEYWORDS:       RTPrintf("      Default: %RTbool\n", g_Defaults.fSetSvnKeywords); break;
            case SCMOPT_SKIP_SVN_SYNC_PROCESS:  RTPrintf("      Default: %RTbool\n", g_Defaults.fSkipSvnSyncProcess); break;
            case SCMOPT_SKIP_UNICODE_CHECKS:    RTPrintf("      Default: %RTbool\n", g_Defaults.fSkipUnicodeChecks); break;
            case SCMOPT_TAB_SIZE:               RTPrintf("      Default: %u\n", g_Defaults.cchTab); break;
            case SCMOPT_WIDTH:                  RTPrintf("      Default: %u\n", g_Defaults.cchWidth); break;

            case SCMOPT_ONLY_SVN_DIRS:          RTPrintf("      Default: %RTbool\n", g_Defaults.fOnlySvnDirs); break;
            case SCMOPT_ONLY_SVN_FILES:         RTPrintf("      Default: %RTbool\n", g_Defaults.fOnlySvnFiles); break;
            case SCMOPT_FILTER_OUT_DIRS:        RTPrintf("      Default: %s\n", g_Defaults.pszFilterOutDirs); break;
            case SCMOPT_FILTER_FILES:           RTPrintf("      Default: %s\n", g_Defaults.pszFilterFiles); break;
            case SCMOPT_FILTER_OUT_FILES:       RTPrintf("      Default: %s\n", g_Defaults.pszFilterOutFiles); break;

            case SCMOPT_TREAT_AS:
                RTPrintf("      For treat the input file(s) differently, restting any --add-action.\n"
                         "      If the value is empty defaults will be used again.  Possible values:\n");
                for (size_t iCfg = 0; iCfg < RT_ELEMENTS(g_aConfigs); iCfg++)
                    RTPrintf("          %s (%s)\n", g_aConfigs[iCfg].pszName, g_aConfigs[iCfg].pszFilePattern);
                break;

            case SCMOPT_ADD_ACTION:
                RTPrintf("      Adds a rewriter action.  The first use after a --treat-as will copy and\n"
                         "      the action list selected by the --treat-as.  The action list will be\n"
                         "      flushed by --treat-as.\n");
                break;

            case SCMOPT_DEL_ACTION:
                RTPrintf("      Deletes one or more rewriter action (pattern). Best used after\n"
                         "      a --treat-as.\n");
                break;

            default: AssertMsgFailed(("i=%d %d %s\n", i, paOpts[i].iShort, paOpts[i].pszLong));
        }
        i += cExtraAdvance;
    }

    return RTEXITCODE_SUCCESS;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return 1;

    /*
     * Init the current year.
     */
    RTTIMESPEC  Now;
    RTTIME      Time;
    RTTimeExplode(&Time, RTTimeNow(&Now));
    g_uYear = Time.i32Year;

    /*
     * Init the settings.
     */
    PSCMSETTINGS pSettings;
    rc = scmSettingsCreate(&pSettings, &g_Defaults);
    if (RT_FAILURE(rc))
    {
        RTMsgError("scmSettingsCreate: %Rrc\n", rc);
        return 1;
    }

    /*
     * Parse arguments and process input in order (because this is the only
     * thing that works at the moment).
     */
    static RTGETOPTDEF s_aOpts[14 + RT_ELEMENTS(g_aScmOpts)] =
    {
        { "--dry-run",                          'd',                                    RTGETOPT_REQ_NOTHING },
        { "--real-run",                         'D',                                    RTGETOPT_REQ_NOTHING },
        { "--check-run",                        SCMOPT_CHECK_RUN,                       RTGETOPT_REQ_NOTHING },
        { "--file-filter",                      'f',                                    RTGETOPT_REQ_STRING  },
        { "--quiet",                            'q',                                    RTGETOPT_REQ_NOTHING },
        { "--verbose",                          'v',                                    RTGETOPT_REQ_NOTHING },
        { "--diff-ignore-eol",                  SCMOPT_DIFF_IGNORE_EOL,                 RTGETOPT_REQ_NOTHING },
        { "--diff-no-ignore-eol",               SCMOPT_DIFF_NO_IGNORE_EOL,              RTGETOPT_REQ_NOTHING },
        { "--diff-ignore-space",                SCMOPT_DIFF_IGNORE_SPACE,               RTGETOPT_REQ_NOTHING },
        { "--diff-no-ignore-space",             SCMOPT_DIFF_NO_IGNORE_SPACE,            RTGETOPT_REQ_NOTHING },
        { "--diff-ignore-leading-space",        SCMOPT_DIFF_IGNORE_LEADING_SPACE,       RTGETOPT_REQ_NOTHING },
        { "--diff-no-ignore-leading-space",     SCMOPT_DIFF_NO_IGNORE_LEADING_SPACE,    RTGETOPT_REQ_NOTHING },
        { "--diff-ignore-trailing-space",       SCMOPT_DIFF_IGNORE_TRAILING_SPACE,      RTGETOPT_REQ_NOTHING },
        { "--diff-no-ignore-trailing-space",    SCMOPT_DIFF_NO_IGNORE_TRAILING_SPACE,   RTGETOPT_REQ_NOTHING },
        { "--diff-special-chars",               SCMOPT_DIFF_SPECIAL_CHARS,              RTGETOPT_REQ_NOTHING },
        { "--diff-no-special-chars",            SCMOPT_DIFF_NO_SPECIAL_CHARS,           RTGETOPT_REQ_NOTHING },
    };
    memcpy(&s_aOpts[RT_ELEMENTS(s_aOpts) - RT_ELEMENTS(g_aScmOpts)], &g_aScmOpts[0], sizeof(g_aScmOpts));

    bool            fCheckRun = false;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetOptState;
    rc = RTGetOptInit(&GetOptState, argc, argv, &s_aOpts[0], RT_ELEMENTS(s_aOpts), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertReleaseRCReturn(rc, 1);

    while (   (rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'd':
                g_fDryRun = true;
                fCheckRun = false;
                break;
            case 'D':
                g_fDryRun = fCheckRun = false;
                break;
            case SCMOPT_CHECK_RUN:
                g_fDryRun = fCheckRun = true;
                break;

            case 'f':
                g_pszFileFilter = ValueUnion.psz;
                break;

            case 'h':
                return scmHelp(s_aOpts, RT_ELEMENTS(s_aOpts));

            case SCMOPT_HELP_CONFIG:
                return scmHelpConfig();

            case SCMOPT_HELP_ACTIONS:
                return scmHelpActions();

            case 'q':
                g_iVerbosity = 0;
                break;

            case 'v':
                g_iVerbosity++;
                break;

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision: 110494 $";
                const char *psz = RTStrStripL(strchr(s_szRev, ' '));
                RTPrintf("r%.*s\n", strchr(psz, ' ') - psz, psz);
                return 0;
            }

            case SCMOPT_DIFF_IGNORE_EOL:
                g_fDiffIgnoreEol = true;
                break;
            case SCMOPT_DIFF_NO_IGNORE_EOL:
                g_fDiffIgnoreEol = false;
                break;

            case SCMOPT_DIFF_IGNORE_SPACE:
                g_fDiffIgnoreTrailingWS = g_fDiffIgnoreLeadingWS = true;
                break;
            case SCMOPT_DIFF_NO_IGNORE_SPACE:
                g_fDiffIgnoreTrailingWS = g_fDiffIgnoreLeadingWS = false;
                break;

            case SCMOPT_DIFF_IGNORE_LEADING_SPACE:
                g_fDiffIgnoreLeadingWS = true;
                break;
            case SCMOPT_DIFF_NO_IGNORE_LEADING_SPACE:
                g_fDiffIgnoreLeadingWS = false;
                break;

            case SCMOPT_DIFF_IGNORE_TRAILING_SPACE:
                g_fDiffIgnoreTrailingWS = true;
                break;
            case SCMOPT_DIFF_NO_IGNORE_TRAILING_SPACE:
                g_fDiffIgnoreTrailingWS = false;
                break;

            case SCMOPT_DIFF_SPECIAL_CHARS:
                g_fDiffSpecialChars = true;
                break;
            case SCMOPT_DIFF_NO_SPECIAL_CHARS:
                g_fDiffSpecialChars = false;
                break;

            default:
            {
                int rc2 = scmSettingsBaseHandleOpt(&pSettings->Base, rc, &ValueUnion, "/", 1);
                if (RT_SUCCESS(rc2))
                    break;
                if (rc2 != VERR_GETOPT_UNKNOWN_OPTION)
                    return 2;
                return RTGetOptPrintError(rc, &ValueUnion);
            }
        }
    }

    /*
     * Process non-options.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (rc == VINF_GETOPT_NOT_OPTION)
    {
        ScmSvnInit();

        bool fWarned = g_fDryRun;
        while (rc == VINF_GETOPT_NOT_OPTION)
        {
            if (!fWarned)
            {
                RTPrintf("%s: Warning! This program will make changes to your source files and\n"
                         "%s:          there is a slight risk that bugs or a full disk may cause\n"
                         "%s:          LOSS OF DATA.   So, please make sure you have checked in\n"
                         "%s:          all your changes already.  If you didn't, then don't blame\n"
                         "%s:          anyone for not warning you!\n"
                         "%s:\n"
                         "%s:          Press any key to continue...\n",
                         g_szProgName, g_szProgName, g_szProgName, g_szProgName, g_szProgName,
                         g_szProgName, g_szProgName);
                RTStrmGetCh(g_pStdIn);
                fWarned = true;
            }

            rc = scmProcessSomething(ValueUnion.psz, pSettings);
            if (RT_FAILURE(rc))
            {
                rcExit = RTEXITCODE_FAILURE;
                break;
            }

            /* next */
            rc = RTGetOpt(&GetOptState, &ValueUnion);
            if (RT_FAILURE(rc))
                rcExit = RTGetOptPrintError(rc, &ValueUnion);
        }

        scmPrintStats();
        ScmSvnTerm();
    }
    else
        RTMsgWarning("No files or directories specified. Doing nothing");

    scmSettingsDestroy(pSettings);

    /* If we're in checking mode, fail if any files needed modification. */
    if (   rcExit == RTEXITCODE_SUCCESS
        && fCheckRun
        && g_cFilesModified > 0)
    {
        RTMsgError("Checking mode failed! %u file%s needs modifications", g_cFilesBinaries, g_cFilesBinaries > 1 ? "s" : "");
        rcExit = RTEXITCODE_FAILURE;
    }

    /* Fail if any files require manual repair. */
    if (g_cFilesRequiringManualFixing > 0)
    {
        RTMsgError("%u file%s needs manual modifications", g_cFilesRequiringManualFixing,
                   g_cFilesRequiringManualFixing > 1 ? "s" : "");
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}

