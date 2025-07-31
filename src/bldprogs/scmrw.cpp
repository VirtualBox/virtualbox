/* $Id: scmrw.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** License types. */
typedef enum SCMLICENSETYPE
{
    kScmLicenseType_Invalid = 0,
    kScmLicenseType_OseGpl,
    kScmLicenseType_OseDualGplCddl,
    kScmLicenseType_OseCddl,
    kScmLicenseType_VBoxLgpl,
    kScmLicenseType_Mit,
    kScmLicenseType_Confidential
} SCMLICENSETYPE;

/** A license. */
typedef struct SCMLICENSETEXT
{
    /** The license type. */
    SCMLICENSETYPE  enmType;
    /** The license option. */
    SCMLICENSE      enmOpt;
    /** The license text.   */
    const char     *psz;
    /** The license text length. */
    size_t          cch;
} SCMLICENSETEXT;
/** Pointer to a license. */
typedef SCMLICENSETEXT const *PCSCMLICENSETEXT;

/**
 * Copyright + license rewriter state.
 */
typedef struct SCMCOPYRIGHTINFO
{
    /** State. */
    PSCMRWSTATE         pState;                 /**< input */
    /** The comment style (neede for C/C++). */
    SCMCOMMENTSTYLE     enmCommentStyle;        /**< input */

    /** Number of comments we've parsed. */
    uint32_t            cComments;

    /** Copy of the contributed-by line if present. */
    char               *pszContributedBy;

    /** @name Common info
     * @{ */
    uint32_t            iLineComment;
    uint32_t            cLinesComment;          /**< This excludes any external license lines. */
    /** @} */

    /** @name Copyright info
     * @{ */
    uint32_t            iLineCopyright;
    uint32_t            uFirstYear;
    uint32_t            uLastYear;
    bool                fWellFormedCopyright;
    bool                fUpToDateCopyright;
    /** @} */

    /** @name License info
     * @{ */
    bool                fOpenSource;            /**< input */
    PCSCMLICENSETEXT    pExpectedLicense;       /**< input */
    PCSCMLICENSETEXT    paLicenses;             /**< input */
    SCMLICENSE          enmLicenceOpt;          /**< input */
    uint32_t            iLineLicense;
    uint32_t            cLinesLicense;
    PCSCMLICENSETEXT    pCurrentLicense;
    bool                fIsCorrectLicense;
    bool                fWellFormedLicense;
    bool                fExternalLicense;
    /** @} */

    /** @name LGPL licence notice and disclaimer info
     * @{ */
    /** Wheter to check for LGPL license notices and disclaimers. */
    bool                fCheckforLgpl;
    /** The approximate line we found the (first) LGPL licence notice on. */
    uint32_t            iLineLgplNotice;
    /** The line number after the LGPL notice comment. */
    uint32_t            iLineAfterLgplComment;
    /** The LGPL disclaimer line. */
    uint32_t            iLineLgplDisclaimer;
    /** @} */

} SCMCOPYRIGHTINFO;
typedef SCMCOPYRIGHTINFO *PSCMCOPYRIGHTINFO;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** --license-ose-gpl */
static const char g_szVBoxOseGpl[] =
    "This file is part of VirtualBox base platform packages, as\n"
    "available from https://www.virtualbox.org.\n"
    "\n"
    "This program is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU General Public License\n"
    "as published by the Free Software Foundation, in version 3 of the\n"
    "License.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, but\n"
    "WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, see <https://www.gnu.org/licenses>.\n"
    "\n"
    "SPDX-License-Identifier: GPL-3.0-only\n";

static const char g_szVBoxOseOldGpl2[] =
    "This file is part of VirtualBox Open Source Edition (OSE), as\n"
    "available from http://www.virtualbox.org. This file is free software;\n"
    "you can redistribute it and/or modify it under the terms of the GNU\n"
    "General Public License (GPL) as published by the Free Software\n"
    "Foundation, in version 2 as it comes in the \"COPYING\" file of the\n"
    "VirtualBox OSE distribution. VirtualBox OSE is distributed in the\n"
    "hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.\n";

/** --license-ose-dual */
static const char g_szVBoxOseDualGplCddl[] =
    "This file is part of VirtualBox base platform packages, as\n"
    "available from https://www.virtualbox.org.\n"
    "\n"
    "This program is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU General Public License\n"
    "as published by the Free Software Foundation, in version 3 of the\n"
    "License.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, but\n"
    "WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, see <https://www.gnu.org/licenses>.\n"
    "\n"
    "The contents of this file may alternatively be used under the terms\n"
    "of the Common Development and Distribution License Version 1.0\n"
    "(CDDL), a copy of it is provided in the \"COPYING.CDDL\" file included\n"
    "in the VirtualBox distribution, in which case the provisions of the\n"
    "CDDL are applicable instead of those of the GPL.\n"
    "\n"
    "You may elect to license modified versions of this file under the\n"
    "terms and conditions of either the GPL or the CDDL or both.\n"
    "\n"
    "SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0\n";

static const char g_szVBoxOseOldDualGpl2Cddl[] =
    "This file is part of VirtualBox Open Source Edition (OSE), as\n"
    "available from http://www.virtualbox.org. This file is free software;\n"
    "you can redistribute it and/or modify it under the terms of the GNU\n"
    "General Public License (GPL) as published by the Free Software\n"
    "Foundation, in version 2 as it comes in the \"COPYING\" file of the\n"
    "VirtualBox OSE distribution. VirtualBox OSE is distributed in the\n"
    "hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.\n"
    "\n"
    "The contents of this file may alternatively be used under the terms\n"
    "of the Common Development and Distribution License Version 1.0\n"
    "(CDDL) only, as it comes in the \"COPYING.CDDL\" file of the\n"
    "VirtualBox OSE distribution, in which case the provisions of the\n"
    "CDDL are applicable instead of those of the GPL.\n"
    "\n"
    "You may elect to license modified versions of this file under the\n"
    "terms and conditions of either the GPL or the CDDL or both.\n";

/** --license-ose-cddl   */
static const char g_szVBoxOseCddl[] =
    "This file is part of VirtualBox base platform packages, as\n"
    "available from http://www.virtualbox.org.\n"
    "\n"
    "The contents of this file are subject to the terms of the Common\n"
    "Development and Distribution License Version 1.0 (CDDL) only, as it\n"
    "comes in the \"COPYING.CDDL\" file of the VirtualBox distribution.\n"
    "\n"
    "SPDX-License-Identifier: CDDL-1.0\n";

static const char g_szVBoxOseOldCddl[] =
    "This file is part of VirtualBox Open Source Edition (OSE), as\n"
    "available from http://www.virtualbox.org. This file is free software;\n"
    "you can redistribute it and/or modify it under the terms of the Common\n"
    "Development and Distribution License Version 1.0 (CDDL) only, as it\n"
    "comes in the \"COPYING.CDDL\" file of the VirtualBox OSE distribution.\n"
    "VirtualBox OSE is distributed in the hope that it will be useful, but\n"
    "WITHOUT ANY WARRANTY of any kind.\n";

/** --license-lgpl */
static const char g_szVBoxLgpl[] =
    "This file is part of a free software library; you can redistribute\n"
    "it and/or modify it under the terms of the GNU Lesser General\n"
    "Public License version 2.1 as published by the Free Software\n"
    "Foundation and shipped in the \"COPYING.LIB\" file with this library.\n"
    "The library is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY of any kind.\n"
    "\n"
    "Oracle LGPL Disclaimer: For the avoidance of doubt, except that if\n"
    "any license choice other than GPL or LGPL is available it will\n"
    "apply instead, Oracle elects to use only the Lesser General Public\n"
    "License version 2.1 (LGPLv2) at this time for any software where\n"
    "a choice of LGPL license versions is made available with the\n"
    "language indicating that LGPLv2 or any later version may be used,\n"
    "or where a choice of which version of the LGPL is applied is\n"
    "otherwise unspecified.\n"
    "\n"
    "SPDX-License-Identifier: LGPL-2.1-only\n";

/** --license-mit
 * @note This isn't detectable as VirtualBox or Oracle specific.
 */
static const char g_szMit[] =
    "Permission is hereby granted, free of charge, to any person\n"
    "obtaining a copy of this software and associated documentation\n"
    "files (the \"Software\"), to deal in the Software without\n"
    "restriction, including without limitation the rights to use,\n"
    "copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
    "copies of the Software, and to permit persons to whom the\n"
    "Software is furnished to do so, subject to the following\n"
    "conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be\n"
    "included in all copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n"
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES\n"
    "OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n"
    "NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT\n"
    "HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,\n"
    "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
    "FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR\n"
    "OTHER DEALINGS IN THE SOFTWARE.\n";

/** --license-mit, alternative wording \#1.
 * @note This differes from g_szMit in "AUTHORS OR COPYRIGHT HOLDERS" is written
 *       "COPYRIGHT HOLDER(S) OR AUTHOR(S)". Its layout is wider, so it is a
 *       couple of lines shorter. */
static const char g_szMitAlt1[] =
    "Permission is hereby granted, free of charge, to any person obtaining a\n"
    "copy of this software and associated documentation files (the \"Software\"),\n"
    "to deal in the Software without restriction, including without limitation\n"
    "the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
    "and/or sell copies of the Software, and to permit persons to whom the\n"
    "Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in\n"
    "all copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL\n"
    "THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR\n"
    "OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,\n"
    "ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR\n"
    "OTHER DEALINGS IN THE SOFTWARE.\n";

/** --license-mit, alternative wording \#2.
 * @note This differes from g_szMit in that "AUTHORS OR COPYRIGHT HOLDERS" is
 *       replaced with "THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS".
 *       Its layout is wider, so it is a couple of lines shorter. */
static const char g_szMitAlt2[] =
    "Permission is hereby granted, free of charge, to any person obtaining a\n"
    "copy of this software and associated documentation files (the \"Software\"),\n"
    "to deal in the Software without restriction, including without limitation\n"
    "the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
    "and/or sell copies of the Software, and to permit persons to whom the\n"
    "Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in\n"
    "all copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL\n"
    "THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,\n"
    "DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR\n"
    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE\n"
    "USE OR OTHER DEALINGS IN THE SOFTWARE.\n";

/** --license-mit, alternative wording \#3.
 * @note This differes from g_szMitAlt2 in that the second and third sections
 *       have been switch. */
static const char g_szMitAlt3[] =
    "Permission is hereby granted, free of charge, to any person obtaining a\n"
    "copy of this software and associated documentation files (the \"Software\"),\n"
    "to deal in the Software without restriction, including without limitation\n"
    "the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
    "and/or sell copies of the Software, and to permit persons to whom the\n"
    "Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL\n"
    "THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,\n"
    "DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR\n"
    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE\n"
    "USE OR OTHER DEALINGS IN THE SOFTWARE.\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in\n"
    "all copies or substantial portions of the Software.\n";

/** --license-(based-on)mit, alternative wording \#4.
 * @note This differs from g_szMitAlt2 in injecting "(including the next
 *       paragraph)". */
static const char g_szMitAlt4[] =
    "Permission is hereby granted, free of charge, to any person obtaining a\n"
    "copy of this software and associated documentation files (the \"Software\"),\n"
    "to deal in the Software without restriction, including without limitation\n"
    "the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
    "and/or sell copies of the Software, and to permit persons to whom the\n"
    "Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice (including the next\n"
    "paragraph) shall be included in all copies or substantial portions of the\n"
    "Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL\n"
    "THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
    "FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER\n"
    "DEALINGS IN THE SOFTWARE.\n";

/** --license-(based-on)mit, alternative wording \#5.
 * @note This differs from g_szMitAlt3 in using "sub license" instead of
 *       "sublicense" and adding an illogical "(including the next
 *       paragraph)" remark to the final paragraph. (vbox_ttm.c) */
static const char g_szMitAlt5[] =
    "Permission is hereby granted, free of charge, to any person obtaining a\n"
    "copy of this software and associated documentation files (the\n"
    "\"Software\"), to deal in the Software without restriction, including\n"
    "without limitation the rights to use, copy, modify, merge, publish,\n"
    "distribute, sub license, and/or sell copies of the Software, and to\n"
    "permit persons to whom the Software is furnished to do so, subject to\n"
    "the following conditions:\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL\n"
    "THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,\n"
    "DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR\n"
    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE\n"
    "USE OR OTHER DEALINGS IN THE SOFTWARE.\n"
    "\n"
    "The above copyright notice and this permission notice (including the\n"
    "next paragraph) shall be included in all copies or substantial portions\n"
    "of the Software.\n";

/** Oracle confidential. */
static const char g_szOracleConfidential[] =
    "Oracle Corporation confidential\n";

/** Oracle confidential, old style. */
static const char g_szOracleConfidentialOld[] =
    "Oracle Corporation confidential\n"
    "All rights reserved\n";

/** Licenses to detect when --license-mit isn't used. */
static const SCMLICENSETEXT g_aLicenses[] =
{
    { kScmLicenseType_OseGpl,           kScmLicense_OseGpl,         RT_STR_TUPLE(g_szVBoxOseGpl)},
    { kScmLicenseType_OseGpl,           kScmLicense_OseGpl,         RT_STR_TUPLE(g_szVBoxOseOldGpl2)},
    { kScmLicenseType_OseDualGplCddl,   kScmLicense_OseDualGplCddl, RT_STR_TUPLE(g_szVBoxOseDualGplCddl) },
    { kScmLicenseType_OseDualGplCddl,   kScmLicense_OseDualGplCddl, RT_STR_TUPLE(g_szVBoxOseOldDualGpl2Cddl) },
    { kScmLicenseType_OseCddl,          kScmLicense_OseCddl,        RT_STR_TUPLE(g_szVBoxOseCddl) },
    { kScmLicenseType_OseCddl,          kScmLicense_OseCddl,        RT_STR_TUPLE(g_szVBoxOseOldCddl) },
    { kScmLicenseType_VBoxLgpl,         kScmLicense_Lgpl,           RT_STR_TUPLE(g_szVBoxLgpl)},
    { kScmLicenseType_Confidential,     kScmLicense_End,            RT_STR_TUPLE(g_szOracleConfidential) },
    { kScmLicenseType_Confidential,     kScmLicense_End,            RT_STR_TUPLE(g_szOracleConfidentialOld) },
    { kScmLicenseType_Invalid,          kScmLicense_End,            NULL, 0 },
};

/** Licenses to detect when --license-mit or --license-based-on-mit are used. */
static const SCMLICENSETEXT g_aLicensesWithMit[] =
{
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMit) },
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMitAlt1) },
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMitAlt2) },
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMitAlt3) },
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMitAlt4) },
    { kScmLicenseType_Mit,              kScmLicense_Mit,            RT_STR_TUPLE(g_szMitAlt5) },
    { kScmLicenseType_OseGpl,           kScmLicense_OseGpl,         RT_STR_TUPLE(g_szVBoxOseGpl)},
    { kScmLicenseType_OseGpl,           kScmLicense_OseGpl,         RT_STR_TUPLE(g_szVBoxOseOldGpl2)},
    { kScmLicenseType_OseDualGplCddl,   kScmLicense_OseDualGplCddl, RT_STR_TUPLE(g_szVBoxOseDualGplCddl) },
    { kScmLicenseType_OseDualGplCddl,   kScmLicense_OseDualGplCddl, RT_STR_TUPLE(g_szVBoxOseOldDualGpl2Cddl) },
    { kScmLicenseType_VBoxLgpl,         kScmLicense_Lgpl,           RT_STR_TUPLE(g_szVBoxLgpl)},
    { kScmLicenseType_Confidential,     kScmLicense_End,            RT_STR_TUPLE(g_szOracleConfidential) },
    { kScmLicenseType_Confidential,     kScmLicense_End,            RT_STR_TUPLE(g_szOracleConfidentialOld) },
    { kScmLicenseType_Invalid,          kScmLicense_End,            NULL, 0 },
};

/** Copyright holder. */
static const char g_szCopyrightHolder[] = "Oracle and/or its affiliates.";

/** Old copyright holder. */
static const char g_szOldCopyrightHolder[] = "Oracle Corporation";

/** LGPL disclaimer. */
static const char g_szLgplDisclaimer[] =
    "Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice\n"
    "other than GPL or LGPL is available it will apply instead, Oracle elects to use only\n"
    "the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where\n"
    "a choice of LGPL license versions is made available with the language indicating\n"
    "that LGPLv2 or any later version may be used, or where a choice of which version\n"
    "of the LGPL is applied is otherwise unspecified.\n";

/** Copyright+license comment start for each SCMCOMMENTSTYLE. */
static RTSTRTUPLE const g_aCopyrightCommentStart[] =
{
    { RT_STR_TUPLE("<invalid> ") },
    { RT_STR_TUPLE("/*") },
    { RT_STR_TUPLE("#") },
    { RT_STR_TUPLE("\"\"\"") },
    { RT_STR_TUPLE(";") },
    { RT_STR_TUPLE("REM") },
    { RT_STR_TUPLE("rem") },
    { RT_STR_TUPLE("Rem") },
    { RT_STR_TUPLE("--") },
    { RT_STR_TUPLE("'") },
    { RT_STR_TUPLE("<!--") },
    { RT_STR_TUPLE("<end>") },
};

/** Copyright+license line prefix for each SCMCOMMENTSTYLE. */
static RTSTRTUPLE const g_aCopyrightCommentPrefix[] =
{
    { RT_STR_TUPLE("<invalid> ") },
    { RT_STR_TUPLE(" * ") },
    { RT_STR_TUPLE("# ") },
    { RT_STR_TUPLE("") },
    { RT_STR_TUPLE("; ") },
    { RT_STR_TUPLE("REM ") },
    { RT_STR_TUPLE("rem ") },
    { RT_STR_TUPLE("Rem ") },
    { RT_STR_TUPLE("-- ") },
    { RT_STR_TUPLE("' ") },
    { RT_STR_TUPLE("    ") },
    { RT_STR_TUPLE("<end>") },
};

/** Copyright+license empty line for each SCMCOMMENTSTYLE. */
static RTSTRTUPLE const g_aCopyrightCommentEmpty[] =
{
    { RT_STR_TUPLE("<invalid>") },
    { RT_STR_TUPLE(" *") },
    { RT_STR_TUPLE("#") },
    { RT_STR_TUPLE("") },
    { RT_STR_TUPLE(";") },
    { RT_STR_TUPLE("REM") },
    { RT_STR_TUPLE("rem") },
    { RT_STR_TUPLE("Rem") },
    { RT_STR_TUPLE("--") },
    { RT_STR_TUPLE("'") },
    { RT_STR_TUPLE("") },
    { RT_STR_TUPLE("<end>") },
};

/** Copyright+license end of comment for each SCMCOMMENTSTYLE. */
static RTSTRTUPLE const g_aCopyrightCommentEnd[] =
{
    { RT_STR_TUPLE("<invalid> ") },
    { RT_STR_TUPLE(" */") },
    { RT_STR_TUPLE("#") },
    { RT_STR_TUPLE("\"\"\"") },
    { RT_STR_TUPLE(";") },
    { RT_STR_TUPLE("REM") },
    { RT_STR_TUPLE("rem") },
    { RT_STR_TUPLE("Rem") },
    { RT_STR_TUPLE("--") },
    { RT_STR_TUPLE("'") },
    { RT_STR_TUPLE("-->") },
    { RT_STR_TUPLE("<end>") },
};


/**
 * Figures out the predominant casing of the "REM" keyword in a batch file.
 *
 * @returns Predominant comment style.
 * @param   pIn         The file to scan.  Will be rewound.
 */
static SCMCOMMENTSTYLE determineBatchFileCommentStyle(PSCMSTREAM pIn)
{
    /*
     * Figure out whether it's using upper or lower case REM comments before
     * doing the work.
     */
    uint32_t    cUpper = 0;
    uint32_t    cLower = 0;
    uint32_t    cCamel = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        while (   cchLine > 2
               && RT_C_IS_SPACE(*pchLine))
        {
            pchLine++;
            cchLine--;
        }
        if (   (   cchLine > 3
                && RT_C_IS_SPACE(pchLine[2]))
            || cchLine == 3)
        {
            if (   pchLine[0] == 'R'
                && pchLine[1] == 'E'
                && pchLine[2] == 'M')
                cUpper++;
            else if (   pchLine[0] == 'r'
                     && pchLine[1] == 'e'
                     && pchLine[2] == 'm')
                cLower++;
            else if (   pchLine[0] == 'R'
                     && pchLine[1] == 'e'
                     && pchLine[2] == 'm')
                cCamel++;
        }
    }

    ScmStreamRewindForReading(pIn);

    if (cLower >= cUpper && cLower >= cCamel)
        return kScmCommentStyle_Rem_Lower;
    if (cCamel >= cLower && cCamel >= cUpper)
        return kScmCommentStyle_Rem_Camel;
    return kScmCommentStyle_Rem_Upper;
}


/**
 * Calculates the number of spaces from @a offStart to @a offEnd in @a pchLine,
 * taking tabs into account.
 */
size_t ScmCalcSpacesForSrcSpan(const char *pchLine, size_t offStart, size_t offEnd, PCSCMSETTINGSBASE pSettings)
{
    size_t cchRet = 0;
    if (offStart < offEnd)
    {
        offEnd  -= offStart; /* becomes cchLeft now */
        pchLine += offStart;
        while (offEnd > 0)
        {
            const char *pszTab = (const char *)memchr(pchLine, '\t', offEnd);
            if (!pszTab)
            {
                cchRet += offEnd;
                break;
            }
            size_t offTab   = (size_t)(pszTab - pchLine);
            size_t cchToTab = pSettings->cchTab - offTab % pSettings->cchTab;
            cchRet += offTab + cchToTab;
            offEnd -= offTab + 1;
            pchLine = pszTab + 1;
        }
    }
    return cchRet;
}


/**
 * Worker for isBlankLine.
 *
 * @returns true if blank, false if not.
 * @param   pchLine     Pointer to the start of the line.
 * @param   cchLine     The (encoded) length of the line, excluding EOL char.
 */
static bool isBlankLineSlow(const char *pchLine, size_t cchLine)
{
    /*
     * From the end, more likely to hit a non-blank char there.
     */
    while (cchLine-- > 0)
        if (!RT_C_IS_BLANK(pchLine[cchLine]))
            return false;
    return true;
}

/**
 * Helper for checking whether a line is blank.
 *
 * @returns true if blank, false if not.
 * @param   pchLine     Pointer to the start of the line.
 * @param   cchLine     The (encoded) length of the line, excluding EOL char.
 */
DECLINLINE(bool) isBlankLine(const char *pchLine, size_t cchLine)
{
    if (cchLine == 0)
        return true;
    /*
     * We're more likely to fine a non-space char at the end of the line than
     * at the start, due to source code indentation.
     */
    if (pchLine[cchLine - 1])
        return false;

    /*
     * Don't bother inlining loop code.
     */
    return isBlankLineSlow(pchLine, cchLine);
}


/**
 * Checks if there are @a cch blanks at @a pch.
 *
 * @returns true if span of @a cch blanks, false if not.
 * @param   pch                 The start of the span to check.
 * @param   cch                 The length of the span.
 */
DECLINLINE(bool) isSpanOfBlanks(const char *pch, size_t cch)
{
    while (cch-- > 0)
    {
        char const ch = *pch++;
        if (!RT_C_IS_BLANK(ch))
            return false;
    }
    return true;
}


/**
 * Strip trailing blanks (space & tab).
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_StripTrailingBlanks(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fStripTrailingBlanks)
        return kScmUnmodified;

    bool        fModified = false;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        int rc;
        if (    cchLine == 0
            ||  !RT_C_IS_BLANK(pchLine[cchLine - 1]) )
            rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        else
        {
            cchLine--;
            while (cchLine > 0 && RT_C_IS_BLANK(pchLine[cchLine - 1]))
                cchLine--;
            rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
            fModified = true;
        }
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }
    if (fModified)
        ScmVerbose(pState, 2, " * Stripped trailing blanks\n");
    return fModified ? kScmModified : kScmUnmodified;
}

/**
 * Expand tabs.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_ExpandTabs(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fConvertTabs)
        return kScmUnmodified;

    size_t const    cchTab = pSettings->cchTab;
    bool            fModified = false;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        int rc;
        const char *pchTab = (const char *)memchr(pchLine, '\t', cchLine);
        if (!pchTab)
            rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        else
        {
            size_t      offTab   = 0;
            const char *pchChunk = pchLine;
            for (;;)
            {
                size_t  cchChunk = pchTab - pchChunk;
                offTab += cchChunk;
                ScmStreamWrite(pOut, pchChunk, cchChunk);

                size_t  cchToTab = cchTab - offTab % cchTab;
                ScmStreamWrite(pOut, g_szTabSpaces, cchToTab);
                offTab += cchToTab;

                pchChunk = pchTab + 1;
                size_t  cchLeft  = cchLine - (pchChunk - pchLine);
                pchTab = (const char *)memchr(pchChunk, '\t', cchLeft);
                if (!pchTab)
                {
                    rc = ScmStreamPutLine(pOut, pchChunk, cchLeft, enmEol);
                    break;
                }
            }

            fModified = true;
        }
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }
    if (fModified)
        ScmVerbose(pState, 2, " * Expanded tabs\n");
    return fModified ? kScmModified : kScmUnmodified;
}

/**
 * Worker for rewrite_ForceNativeEol, rewrite_ForceLF and rewrite_ForceCRLF.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 * @param   enmDesiredEol       The desired end of line indicator type.
 * @param   pszDesiredSvnEol    The desired svn:eol-style.
 */
static SCMREWRITERRES rewrite_ForceEol(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings,
                                       SCMEOL enmDesiredEol, const char *pszDesiredSvnEol)
{
    if (!pSettings->fConvertEol)
        return kScmUnmodified;

    bool        fModified = false;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        if (   enmEol != enmDesiredEol
            && enmEol != SCMEOL_NONE)
        {
            fModified = true;
            enmEol = enmDesiredEol;
        }
        int rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }
    if (fModified)
        ScmVerbose(pState, 2, " * Converted EOL markers\n");

    /* Check svn:eol-style if appropriate */
    if (   pSettings->fSetSvnEol
        && ScmSvnIsInWorkingCopy(pState))
    {
        char *pszEol;
        int rc = ScmSvnQueryProperty(pState, "svn:eol-style", &pszEol);
        if (   (RT_SUCCESS(rc) && strcmp(pszEol, pszDesiredSvnEol))
            || rc == VERR_NOT_FOUND)
        {
            if (rc == VERR_NOT_FOUND)
                ScmVerbose(pState, 2, " * Setting svn:eol-style to %s (missing)\n", pszDesiredSvnEol);
            else
                ScmVerbose(pState, 2, " * Setting svn:eol-style to %s (was: %s)\n", pszDesiredSvnEol, pszEol);
            int rc2 = ScmSvnSetProperty(pState, "svn:eol-style", pszDesiredSvnEol);
            if (RT_FAILURE(rc2))
                ScmError(pState, rc2, "ScmSvnSetProperty: %Rrc\n", rc2);
        }
        if (RT_SUCCESS(rc))
            RTStrFree(pszEol);
    }

    /** @todo also check the subversion svn:eol-style state! */
    return fModified ? kScmModified : kScmUnmodified;
}

/**
 * Force native end of line indicator.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_ForceNativeEol(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    return rewrite_ForceEol(pState, pIn, pOut, pSettings, SCMEOL_CRLF, "native");
#else
    return rewrite_ForceEol(pState, pIn, pOut, pSettings, SCMEOL_LF,   "native");
#endif
}

/**
 * Force the stream to use LF as the end of line indicator.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_ForceLF(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_ForceEol(pState, pIn, pOut, pSettings, SCMEOL_LF, "LF");
}

/**
 * Force the stream to use CRLF as the end of line indicator.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_ForceCRLF(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_ForceEol(pState, pIn, pOut, pSettings, SCMEOL_CRLF, "CRLF");
}

/**
 * Strip trailing blank lines and/or make sure there is exactly one blank line
 * at the end of the file.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 *
 * @remarks ASSUMES trailing white space has been removed already.
 */
SCMREWRITERRES rewrite_AdjustTrailingLines(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (   !pSettings->fStripTrailingLines
        && !pSettings->fForceTrailingLine
        && !pSettings->fForceFinalEol)
        return kScmUnmodified;

    size_t const cLines = ScmStreamCountLines(pIn);

    /* Empty files remains empty. */
    if (cLines <= 1)
        return kScmUnmodified;

    /* Figure out if we need to adjust the number of lines or not. */
    size_t cLinesNew = cLines;

    if (   pSettings->fStripTrailingLines
        && ScmStreamIsWhiteLine(pIn, cLinesNew - 1))
    {
        while (   cLinesNew > 1
               && ScmStreamIsWhiteLine(pIn, cLinesNew - 2))
            cLinesNew--;
    }

    if (    pSettings->fForceTrailingLine
        && !ScmStreamIsWhiteLine(pIn, cLinesNew - 1))
        cLinesNew++;

    bool fFixMissingEol = pSettings->fForceFinalEol
                       && ScmStreamGetEolByLine(pIn, cLinesNew - 1) == SCMEOL_NONE;

    if (   !fFixMissingEol
        && cLines == cLinesNew)
        return kScmUnmodified;

    /* Copy the number of lines we've arrived at. */
    ScmStreamRewindForReading(pIn);

    size_t cCopied = RT_MIN(cLinesNew, cLines);
    ScmStreamCopyLines(pOut, pIn, cCopied);

    if (cCopied != cLinesNew)
    {
        while (cCopied++ < cLinesNew)
            ScmStreamPutLine(pOut, "", 0, ScmStreamGetEol(pIn));
    }
    /* Fix missing EOL if required. */
    else if (fFixMissingEol)
    {
        if (ScmStreamGetEol(pIn) == SCMEOL_LF)
            ScmStreamWrite(pOut, "\n", 1);
        else
            ScmStreamWrite(pOut, "\r\n", 2);
    }

    ScmVerbose(pState, 2, " * Adjusted trailing blank lines\n");
    return kScmModified;
}

/**
 * Make sure there is no svn:executable property on the current file.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnNoExecutable(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   !pSettings->fSetSvnExecutable
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    int rc = ScmSvnQueryProperty(pState, "svn:executable", NULL);
    if (RT_SUCCESS(rc))
    {
        ScmVerbose(pState, 2, " * removing svn:executable\n");
        rc = ScmSvnDelProperty(pState, "svn:executable");
        if (RT_FAILURE(rc))
            ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
    }
    return kScmUnmodified;
}

/**
 * Make sure there is no svn:keywords property on the current file.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnNoKeywords(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   !pSettings->fSetSvnExecutable
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    int rc = ScmSvnQueryProperty(pState, "svn:keywords", NULL);
    if (RT_SUCCESS(rc))
    {
        ScmVerbose(pState, 2, " * removing svn:keywords\n");
        rc = ScmSvnDelProperty(pState, "svn:keywords");
        if (RT_FAILURE(rc))
            ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
    }
    return kScmUnmodified;
}

/**
 * Make sure there is no svn:eol-style property on the current file.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnNoEolStyle(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   !pSettings->fSetSvnExecutable
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    int rc = ScmSvnQueryProperty(pState, "svn:eol-style", NULL);
    if (RT_SUCCESS(rc))
    {
        ScmVerbose(pState, 2, " * removing svn:eol-style\n");
        rc = ScmSvnDelProperty(pState, "svn:eol-style");
        if (RT_FAILURE(rc))
            ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
    }
    return kScmUnmodified;
}

/**
 * Makes sure the svn properties are appropriate for a binary.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnBinary(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   !pSettings->fSetSvnExecutable
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    /* remove svn:eol-style and svn:keywords */
    static const char * const s_apszRemove[] = { "svn:eol-style", "svn:keywords" };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_apszRemove); i++)
    {
        char *pszValue;
        int rc = ScmSvnQueryProperty(pState, s_apszRemove[i], &pszValue);
        if (RT_SUCCESS(rc))
        {
            ScmVerbose(pState, 2, " * removing %s=%s\n", s_apszRemove[i], pszValue);
            RTStrFree(pszValue);
            rc = ScmSvnDelProperty(pState, s_apszRemove[i]);
            if (RT_FAILURE(rc))
                ScmError(pState, rc, "ScmSvnSetProperty(,%s): %Rrc\n", s_apszRemove[i], rc);
        }
        else if (rc != VERR_NOT_FOUND)
            ScmError(pState, rc, "ScmSvnQueryProperty: %Rrc\n", rc);
    }

    /* Make sure there is a svn:mime-type set. */
    int rc = ScmSvnQueryProperty(pState, "svn:mime-type", NULL);
    if (rc == VERR_NOT_FOUND)
    {
        ScmVerbose(pState, 2, " * settings svn:mime-type\n");
        rc = ScmSvnSetProperty(pState, "svn:mime-type", "application/octet-stream");
        if (RT_FAILURE(rc))
            ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
    }
    else if (RT_FAILURE(rc))
        ScmError(pState, rc, "ScmSvnQueryProperty: %Rrc\n", rc);

    return kScmUnmodified;
}

/**
 * Make sure the Id and Revision keywords are expanded.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnKeywords(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   !pSettings->fSetSvnKeywords
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    char *pszKeywords;
    int rc = ScmSvnQueryProperty(pState, "svn:keywords", &pszKeywords);
    if (    RT_SUCCESS(rc)
        && (   !strstr(pszKeywords, "Id") /** @todo need some function for finding a word in a string.  */
            || !strstr(pszKeywords, "Revision")) )
    {
        if (!strstr(pszKeywords, "Id") && !strstr(pszKeywords, "Revision"))
            rc = RTStrAAppend(&pszKeywords, " Id Revision");
        else if (!strstr(pszKeywords, "Id"))
            rc = RTStrAAppend(&pszKeywords, " Id");
        else
            rc = RTStrAAppend(&pszKeywords, " Revision");
        if (RT_SUCCESS(rc))
        {
            ScmVerbose(pState, 2, " * changing svn:keywords to '%s'\n", pszKeywords);
            rc = ScmSvnSetProperty(pState, "svn:keywords", pszKeywords);
            if (RT_FAILURE(rc))
                ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
        }
        else
            ScmError(pState, rc, "RTStrAppend: %Rrc\n", rc);
        RTStrFree(pszKeywords);
    }
    else if (rc == VERR_NOT_FOUND)
    {
        ScmVerbose(pState, 2, " * setting svn:keywords to 'Id Revision'\n");
        rc = ScmSvnSetProperty(pState, "svn:keywords", "Id Revision");
        if (RT_FAILURE(rc))
            ScmError(pState, rc, "ScmSvnSetProperty: %Rrc\n", rc);
    }
    else if (RT_SUCCESS(rc))
        RTStrFree(pszKeywords);

    return kScmUnmodified;
}

/**
 * Checks the svn:sync-process value and that parent is exported too.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_SvnSyncProcess(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (   pSettings->fSkipSvnSyncProcess
        || !ScmSvnIsInWorkingCopy(pState))
        return kScmUnmodified;

    char *pszSyncProcess;
    int rc = ScmSvnQueryProperty(pState, "svn:sync-process", &pszSyncProcess);
    if (RT_SUCCESS(rc))
    {
        if (strcmp(pszSyncProcess, "export") == 0)
        {
            char *pszParentSyncProcess;
            rc = ScmSvnQueryParentProperty(pState, "svn:sync-process", &pszParentSyncProcess);
            if (RT_SUCCESS(rc))
            {
                if (strcmp(pszSyncProcess, "export") != 0)
                    ScmError(pState, VERR_INVALID_STATE,
                             "svn:sync-process=export, but parent directory differs: %s\n"
                             "WARNING! Make sure to unexport everything inside the directory first!\n"
                             "         Then you may export the directory and stuff inside it if you want.\n"
                             "         (Just exporting the directory will not make anything inside it externally visible.)\n"
                             , pszParentSyncProcess);
                RTStrFree(pszParentSyncProcess);
            }
            else if (rc == VERR_NOT_FOUND)
                ScmError(pState, VERR_NOT_FOUND,
                         "svn:sync-process=export, but parent directory is not exported!\n"
                         "WARNING! Make sure to unexport everything inside the directory first!\n"
                         "         Then you may export the directory and stuff inside it if you want.\n"
                         "         (Just exporting the directory will not make anything inside it externally visible.)\n");
            else
                ScmError(pState, rc, "ScmSvnQueryParentProperty: %Rrc\n", rc);
        }
        else if (strcmp(pszSyncProcess, "ignore") != 0)
            ScmError(pState, VERR_INVALID_NAME, "Bad sync-process value: %s\n", pszSyncProcess);
        RTStrFree(pszSyncProcess);
    }
    else if (rc != VERR_NOT_FOUND)
        ScmError(pState, rc, "ScmSvnQueryProperty: %Rrc\n", rc);

    return kScmUnmodified;
}

/**
 * Checks the that there is no bidirectional unicode fun in the file.
 *
 * @returns kScmUnmodified - the state carries these kinds of changes.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_UnicodeChecks(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pIn, pOut);
    if (pSettings->fSkipUnicodeChecks)
        return kScmUnmodified;

    /*
     * Just scan the input for weird stuff and fail if we find anything we don't like.
     */
    uint32_t    iLine = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        iLine++;
        const char *pchCur  = pchLine;
        size_t      cchLeft = cchLine;
        while (cchLeft > 0)
        {
            RTUNICP uc = 0;
            int rc = RTStrGetCpNEx(&pchCur, &cchLeft, &uc);
            if (RT_SUCCESS(rc))
            {
                const char *pszWhat;
                switch (uc)
                {
                    default:
                        continue;

                    /* Potentially evil bi-directional control codes (Table I, trojan-source.pdf):  */
                    case 0x202a: pszWhat = "LRE - left-to-right embedding"; break;
                    case 0x202b: pszWhat = "RLE - right-to-left embedding"; break;
                    case 0x202d: pszWhat = "LRO - left-to-right override"; break;
                    case 0x202e: pszWhat = "RLO - right-to-left override"; break;
                    case 0x2066: pszWhat = "LRI - left-to-right isolate"; break;
                    case 0x2067: pszWhat = "RLI - right-to-left isolate"; break;
                    case 0x2068: pszWhat = "FSI - first strong isolate"; break;
                    case 0x202c: pszWhat = "PDF - pop directional formatting (LRE, RLE, LRO, RLO)"; break;
                    case 0x2069: pszWhat = "PDI - pop directional isolate (LRI, RLI)"; break;

                    /** @todo add checks for homoglyphs too. */
                }
                ScmFixManually(pState, "%u:%zu: Evil unicode codepoint: %s\n", iLine, pchCur - pchLine, pszWhat);
            }
            else
                ScmFixManually(pState, "%u:%zu: Invalid UTF-8 encoding: %Rrc\n", iLine, pchCur - pchLine, rc);
        }
    }

    return kScmUnmodified;
}



/*********************************************************************************************************************************
*   Copyright & License                                                                                                          *
*********************************************************************************************************************************/

/**
 * Compares two strings word-by-word, ignoring spaces, punctuation and case.
 *
 * Assumes ASCII strings.
 *
 * @returns true if they match, false if not.
 * @param   psz1        The first string.  This is typically the known one.
 * @param   psz2        The second string.  This is typically the unknown one,
 *                      which is why we return a next pointer for this one.
 * @param   ppsz2Next   Where to return the next part of the 2nd string.  If
 *                      this is NULL, the whole string must match.
 */
static bool IsEqualWordByWordIgnoreCase(const char *psz1, const char *psz2, const char **ppsz2Next)
{
    for (;;)
    {
        /* Try compare raw strings first. */
        char ch1 = *psz1;
        char ch2 = *psz2;
        if (   ch1 == ch2
            || RT_C_TO_LOWER(ch1) == RT_C_TO_LOWER(ch2))
        {
            if (ch1)
            {
                psz1++;
                psz2++;
            }
            else
            {
                if (ppsz2Next)
                    *ppsz2Next = psz2;
                return true;
            }
        }
        else
        {
            /* Try skip spaces an punctuation. */
            while (   RT_C_IS_SPACE(ch1)
                   || RT_C_IS_PUNCT(ch1))
                ch1 = *++psz1;

            if (ch1 == '\0' && ppsz2Next)
            {
                *ppsz2Next = psz2;
                return true;
            }

            while (   RT_C_IS_SPACE(ch2)
                   || RT_C_IS_PUNCT(ch2))
                ch2 = *++psz2;

            if (   ch1 != ch2
                && RT_C_TO_LOWER(ch1) != RT_C_TO_LOWER(ch2))
            {
                if (ppsz2Next)
                    *ppsz2Next = psz2;
                return false;
            }
        }
    }
}

/**
 * Looks for @a pszFragment anywhere in @a pszText, ignoring spaces, punctuation
 * and case.
 *
 * @returns true if found, false if not.
 * @param   pszText             The haystack to search in.
 * @param   cchText             The length @a pszText.
 * @param   pszFragment         The needle to search for.
 * @param   ppszStart           Where to return the address in @a pszText where
 *                              the fragment was found.  Optional.
 * @param   ppszNext            Where to return the pointer to the first char in
 *                              @a pszText after the fragment.  Optional.
 *
 * @remarks First character of @a pszFragment must be an 7-bit ASCII character!
 *          This character must not be space or punctuation.
 */
static bool scmContainsWordByWordIgnoreCase(const char *pszText, size_t cchText, const char *pszFragment,
                                            const char **ppszStart, const char **ppszNext)
{
    Assert(!((unsigned)*pszFragment & 0x80));
    Assert(pszText[cchText] == '\0');
    Assert(!RT_C_IS_BLANK(*pszFragment));
    Assert(!RT_C_IS_PUNCT(*pszFragment));

    char chLower = RT_C_TO_LOWER(*pszFragment);
    char chUpper = RT_C_TO_UPPER(*pszFragment);
    for (;;)
    {
        const char *pszHit = (const char *)memchr(pszText, chLower, cchText);
        const char *pszHit2 = (const char *)memchr(pszText, chUpper, cchText);
        if (!pszHit && !pszHit2)
        {
            if (ppszStart)
                *ppszStart = NULL;
            if (ppszNext)
                *ppszNext = NULL;
            return false;
        }

        if (   pszHit == NULL
            || (   pszHit2 != NULL
                && ((uintptr_t)pszHit2 < (uintptr_t)pszHit)) )
            pszHit = pszHit2;

        const char *pszNext;
        if (IsEqualWordByWordIgnoreCase(pszFragment, pszHit, &pszNext))
        {
            if (ppszStart)
                *ppszStart = pszHit;
            if (ppszNext)
                *ppszNext = pszNext;
            return true;
        }

        cchText -= pszHit - pszText + 1;
        pszText = pszHit + 1;
    }
}


/**
 * Counts the number of lines in the given substring.
 *
 * @returns The number of lines.
 * @param   psz          The start of the substring.
 * @param   cch          The length of the substring.
 */
static uint32_t CountLinesInSubstring(const char *psz, size_t cch)
{
    uint32_t cLines = 0;
    for (;;)
    {
        const char *pszEol = (const char *)memchr(psz, '\n', cch);
        if (pszEol)
            cLines++;
        else
            return cLines + (*psz != '\0');
        cch -= pszEol + 1 - psz;
        if (!cch)
            return cLines;
        psz  = pszEol + 1;
    }
}


/**
 * Comment parser callback for locating copyright and license.
 */
static DECLCALLBACK(int)
rewrite_Copyright_CommentCallback(PCSCMCOMMENTINFO pInfo, const char *pszBody, size_t cchBody, void *pvUser)
{
    PSCMCOPYRIGHTINFO pState = (PSCMCOPYRIGHTINFO)pvUser;
    Assert(strlen(pszBody) == cchBody);
    //RTPrintf("--- comment at %u, type %u ---\n%s\n--- end ---\n", pInfo->iLineStart, pInfo->enmType, pszBody);
    ScmVerbose(pState->pState, 5,
               "--- comment at %u col %u, %u lines, type %u, %u lines before body, %u lines after body\n",
               pInfo->iLineStart, pInfo->offStart, pInfo->iLineEnd - pInfo->iLineStart + 1, pInfo->enmType,
               pInfo->cBlankLinesBefore, pInfo->cBlankLinesAfter);

    pState->cComments++;

    uint32_t iLine = pInfo->iLineStart + pInfo->cBlankLinesBefore;

    /*
     * Look for a 'contributed by' or 'includes contributions from' line, these
     * comes first when present.
     */
    const char *pchContributedBy = NULL;
    size_t      cchContributedBy = 0;
    size_t      cBlankLinesAfterContributedBy = 0;
    if (    pState->pszContributedBy == NULL
        && (   pState->iLineCopyright == UINT32_MAX
            || pState->iLineLicense == UINT32_MAX)
        && (   (    cchBody > sizeof("Contributed by")
                && RTStrNICmp(pszBody, RT_STR_TUPLE("contributed by")) == 0)
            || (    cchBody > sizeof("Includes contributions from")
                && RTStrNICmp(pszBody, RT_STR_TUPLE("Includes contributions from")) == 0) ) )
    {
        const char *pszNextLine = (const char *)memchr(pszBody, '\n', cchBody);
        while (pszNextLine && pszNextLine[1] != '\n')
            pszNextLine = (const char *)memchr(pszNextLine + 1, '\n', cchBody);
        if (pszNextLine)
        {
            pchContributedBy = pszBody;
            cchContributedBy = pszNextLine - pszBody;

            /* Skip the copyright line and any blank lines following it. */
            cchBody -= cchContributedBy + 1;
            pszBody  = pszNextLine + 1;
            iLine   += 1;
            while (*pszBody == '\n')
            {
                pszBody++;
                cchBody--;
                iLine++;
                cBlankLinesAfterContributedBy++;
            }
        }
    }

    /*
     * Look for the copyright line.
     */
    bool     fFoundCopyright = false;
    uint32_t cBlankLinesAfterCopyright = 0;
    if (   pState->iLineCopyright == UINT32_MAX
        && cchBody > sizeof("Copyright") + RT_MIN(sizeof(g_szCopyrightHolder), sizeof(g_szOldCopyrightHolder))
        && RTStrNICmp(pszBody, RT_STR_TUPLE("copyright")) == 0)
    {
        const char *pszNextLine = (const char *)memchr(pszBody, '\n', cchBody);

        /* Oracle copyright? */
        const char *pszEnd  = pszNextLine ? pszNextLine : &pszBody[cchBody];
        while (RT_C_IS_SPACE(pszEnd[-1]))
            pszEnd--;
        if (   (   (uintptr_t)(pszEnd - pszBody) > sizeof(g_szCopyrightHolder)
                && (*(unsigned char *)(pszEnd - sizeof(g_szCopyrightHolder) + 1) & 0x80) == 0 /* to avoid annoying assertion */
                && RTStrNICmp(pszEnd - sizeof(g_szCopyrightHolder) + 1, RT_STR_TUPLE(g_szCopyrightHolder)) == 0)
            || (   (uintptr_t)(pszEnd - pszBody) > sizeof(g_szOldCopyrightHolder)
                && (*(unsigned char *)(pszEnd - sizeof(g_szOldCopyrightHolder) + 1) & 0x80) == 0 /* to avoid annoying assertion */
                && RTStrNICmp(pszEnd - sizeof(g_szOldCopyrightHolder) + 1, RT_STR_TUPLE(g_szOldCopyrightHolder)) == 0) )
        {
            /* Parse out the year(s). */
            const char *psz = pszBody + sizeof("copyright");
            while ((uintptr_t)psz < (uintptr_t)pszEnd && !RT_C_IS_DIGIT(*psz))
                psz++;
            if (RT_C_IS_DIGIT(*psz))
            {
                char *pszNext;
                int rc = RTStrToUInt32Ex(psz, &pszNext, 10, &pState->uFirstYear);
                if (   RT_SUCCESS(rc)
                    && rc != VWRN_NUMBER_TOO_BIG
                    && rc != VWRN_NEGATIVE_UNSIGNED)
                {
                    if (   pState->uFirstYear < 1975
                        || pState->uFirstYear > 3000)
                    {
                        char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                        RTStrPurgeEncoding(pszCopy);
                        ScmError(pState->pState, VERR_OUT_OF_RANGE, "Copyright year is out of range: %u ('%s')\n",
                                 pState->uFirstYear, pszCopy);
                        RTStrFree(pszCopy);
                        pState->uFirstYear = UINT32_MAX;
                    }

                    while (RT_C_IS_SPACE(*pszNext))
                        pszNext++;
                    if (*pszNext == '-')
                    {
                        do
                            pszNext++;
                        while (RT_C_IS_SPACE(*pszNext));
                        rc = RTStrToUInt32Ex(pszNext, &pszNext, 10, &pState->uLastYear);
                        if (   RT_SUCCESS(rc)
                            && rc != VWRN_NUMBER_TOO_BIG
                            && rc != VWRN_NEGATIVE_UNSIGNED)
                        {
                            if (   pState->uLastYear < 1975
                                || pState->uLastYear > 3000)
                            {
                                char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                                RTStrPurgeEncoding(pszCopy);
                                ScmError(pState->pState, VERR_OUT_OF_RANGE, "Second copyright year is out of range: %u ('%s')\n",
                                         pState->uLastYear, pszCopy);
                                RTStrFree(pszCopy);
                                pState->uLastYear = UINT32_MAX;
                            }
                            else if (pState->uFirstYear > pState->uLastYear)
                            {
                                char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                                RTStrPurgeEncoding(pszCopy);
                                RTMsgWarning("Copyright years switched(?): '%s'\n", pszCopy);
                                RTStrFree(pszCopy);
                                uint32_t iTmp = pState->uLastYear;
                                pState->uLastYear = pState->uFirstYear;
                                pState->uFirstYear = iTmp;
                            }
                        }
                        else
                        {
                            pState->uLastYear = UINT32_MAX;
                            char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                            RTStrPurgeEncoding(pszCopy);
                            ScmError(pState->pState, RT_SUCCESS(rc) ? -rc : rc,
                                     "Failed to parse second copyright year: '%s'\n", pszCopy);
                            RTMemFree(pszCopy);
                        }
                    }
                    else if (*pszNext != g_szCopyrightHolder[0])
                    {
                        char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                        RTStrPurgeEncoding(pszCopy);
                        ScmError(pState->pState, VERR_PARSE_ERROR,
                                 "Failed to parse copyright: '%s'\n", pszCopy);
                        RTMemFree(pszCopy);
                    } else
                        pState->uLastYear = pState->uFirstYear;
                }
                else
                {
                    pState->uFirstYear = UINT32_MAX;
                    char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
                    RTStrPurgeEncoding(pszCopy);
                    ScmError(pState->pState, RT_SUCCESS(rc) ? -rc : rc,
                             "Failed to parse copyright year: '%s'\n", pszCopy);
                    RTMemFree(pszCopy);
                }
            }

            /* The copyright comment must come before the license. */
            if (pState->iLineLicense != UINT32_MAX)
                ScmError(pState->pState, VERR_WRONG_ORDER, "Copyright (line %u) must come before the license (line %u)!\n",
                         iLine, pState->iLineLicense);

            /* In C/C++ code, this must be a multiline comment.  While in python it
               must be a */
            if (pState->enmCommentStyle == kScmCommentStyle_C && pInfo->enmType != kScmCommentType_MultiLine)
                ScmError(pState->pState, VERR_WRONG_ORDER, "Copyright must appear in a multiline comment (no doxygen stuff)\n");
            else if (pState->enmCommentStyle == kScmCommentStyle_Python && pInfo->enmType != kScmCommentType_DocString)
                ScmError(pState->pState, VERR_WRONG_ORDER, "Copyright must appear in a doc-string\n");

            /* The copyright must be followed by the license. */
            if (!pszNextLine)
                ScmError(pState->pState, VERR_WRONG_ORDER, "Copyright should be followed by the license text!\n");

            /* Quit if we've flagged a failure. */
            if (RT_FAILURE(pState->pState->rc))
                return VERR_CALLBACK_RETURN;

            /* Check if it's well formed and up to date. */
            char   szWellFormed[256];
            size_t cchWellFormed;
            if (pState->uFirstYear == pState->uLastYear)
                cchWellFormed = RTStrPrintf(szWellFormed, sizeof(szWellFormed), "Copyright (C) %u %s",
                                            pState->uFirstYear, g_szCopyrightHolder);
            else
                cchWellFormed = RTStrPrintf(szWellFormed, sizeof(szWellFormed), "Copyright (C) %u-%u %s",
                                            pState->uFirstYear, pState->uLastYear, g_szCopyrightHolder);
            pState->fUpToDateCopyright   = pState->uLastYear == g_uYear;
            pState->iLineCopyright       = iLine;
            pState->fWellFormedCopyright = cchWellFormed == (uintptr_t)(pszEnd - pszBody)
                                        && memcmp(pszBody, szWellFormed, cchWellFormed) == 0;
            if (!pState->fWellFormedCopyright)
                ScmVerbose(pState->pState, 1, "* copyright isn't well formed\n");

            /* If there wasn't exactly one blank line before the comment, trigger a rewrite. */
            if (pInfo->cBlankLinesBefore != 1)
            {
                ScmVerbose(pState->pState, 1, "* copyright comment is preceded by %u blank lines instead of 1\n",
                           pInfo->cBlankLinesBefore);
                pState->fWellFormedCopyright = false;
            }

            /* If the comment doesn't start in column 1, trigger rewrite. */
            if (pInfo->offStart != 0)
            {
                ScmVerbose(pState->pState, 1, "* copyright comment starts in column %u instead of 1\n", pInfo->offStart + 1);
                pState->fWellFormedCopyright = false;
                /** @todo check that there isn't any code preceding the comment. */
            }

            if (pchContributedBy)
            {
                pState->pszContributedBy = RTStrDupN(pchContributedBy, cchContributedBy);
                if (cBlankLinesAfterContributedBy != 1)
                {
                    ScmVerbose(pState->pState, 1, "* %u blank lines between contributed by and copyright, should be 1\n",
                               cBlankLinesAfterContributedBy);
                    pState->fWellFormedCopyright = false;
                }
            }

            fFoundCopyright = true;
            ScmVerbose(pState->pState, 3, "oracle copyright %u-%u: up-to-date=%RTbool well-formed=%RTbool\n",
                       pState->uFirstYear, pState->uLastYear, pState->fUpToDateCopyright, pState->fWellFormedCopyright);
        }
        else
        {
            char *pszCopy = RTStrDupN(pszBody, pszEnd - pszBody);
            RTStrPurgeEncoding(pszCopy);
            ScmVerbose(pState->pState, 3, "not oracle copyright: '%s'\n", pszCopy);
            RTStrFree(pszCopy);
        }

        if (!pszNextLine)
            return VINF_SUCCESS;

        /* Skip the copyright line and any blank lines following it. */
        cchBody -= pszNextLine - pszBody + 1;
        pszBody  = pszNextLine + 1;
        iLine   += 1;
        while (*pszBody == '\n')
        {
            pszBody++;
            cchBody--;
            iLine++;
            cBlankLinesAfterCopyright++;
        }

        /*
         * If we have a based-on-mit scenario, check for the lead in now and
         * complain if not found.
         */
        if (   fFoundCopyright
            && pState->enmLicenceOpt == kScmLicense_BasedOnMit
            && pState->iLineLicense == UINT32_MAX)
        {
            if (RTStrNICmp(pszBody, RT_STR_TUPLE("This file is based on ")) == 0)
            {
                /* Take down a comment area which goes up to 'this file is based on'.
                   The license line and length isn't used but gets set to cover the current line. */
                pState->iLineComment        = pInfo->iLineStart;
                pState->cLinesComment       = iLine - pInfo->iLineStart;
                pState->iLineLicense        = iLine;
                pState->cLinesLicense       = 1;
                pState->fExternalLicense    = true;
                pState->fIsCorrectLicense   = true;
                pState->fWellFormedLicense  = true;

                /* Check if we've got a MIT a license here or not. */
                pState->pCurrentLicense     = NULL;
                do
                {
                    const char *pszEol = (const char *)memchr(pszBody, '\n', cchBody);
                    if (!pszEol || pszEol[1] == '\0')
                    {
                        pszBody += cchBody;
                        cchBody = 0;
                        break;
                    }
                    cchBody -= pszEol - pszBody + 1;
                    pszBody  = pszEol + 1;
                    iLine++;

                    for (PCSCMLICENSETEXT pCur = pState->paLicenses; pCur->cch > 0; pCur++)
                    {
                        const char *pszNext;
                        if (   pCur->cch <= cchBody + 32 /* (+ 32 since we don't compare spaces and punctuation) */
                            && IsEqualWordByWordIgnoreCase(pCur->psz, pszBody, &pszNext))
                        {
                            pState->pCurrentLicense = pCur;
                            break;
                        }
                    }
                } while (!pState->pCurrentLicense);
                if (!pState->pCurrentLicense)
                    ScmError(pState->pState, VERR_NOT_FOUND, "Could not find the based-on license!\n");
                else if (pState->pCurrentLicense->enmType != kScmLicenseType_Mit)
                    ScmError(pState->pState, VERR_NOT_FOUND, "The based-on license is not MIT (%.32s...)\n",
                             pState->pCurrentLicense->psz);
            }
            else
                ScmError(pState->pState, VERR_WRONG_ORDER, "Expected 'This file is based on ...' after our copyright!\n");
            return VINF_SUCCESS;
        }
    }

    /*
     * Look for LGPL like text in the comment.
     */
    if (pState->fCheckforLgpl && cchBody > 128)
    {
        /* We look for typical LGPL notices. */
        if (pState->iLineLgplNotice == UINT32_MAX)
        {
            static const char * const s_apszFragments[] =
            {
                "under the terms of the GNU Lesser General Public License",
            };
            for (unsigned i = 0; i < RT_ELEMENTS(s_apszFragments); i++)
                if (scmContainsWordByWordIgnoreCase(pszBody, cchBody, s_apszFragments[i], NULL, NULL))
                {
                    pState->iLineLgplNotice = iLine;
                    pState->iLineAfterLgplComment = pInfo->iLineEnd + 1;
                    ScmVerbose(pState->pState, 3, "Found LGPL notice at %u\n", iLine);
                    break;
                }
        }

        if (   pState->iLineLgplDisclaimer == UINT32_MAX
            && scmContainsWordByWordIgnoreCase(pszBody, cchBody, g_szLgplDisclaimer, NULL, NULL))
        {
            pState->iLineLgplDisclaimer = iLine;
            ScmVerbose(pState->pState, 3, "Found LGPL disclaimer at %u\n", iLine);
        }
    }

    /*
     * Look for the license text.
     */
    if (pState->iLineLicense == UINT32_MAX)
    {
        for (PCSCMLICENSETEXT pCur = pState->paLicenses; pCur->cch > 0; pCur++)
        {
            const char *pszNext;
            if (   pCur->cch <= cchBody + 32 /* (+ 32 since we don't compare spaces and punctuation) */
                && IsEqualWordByWordIgnoreCase(pCur->psz, pszBody, &pszNext))
            {
                while (   RT_C_IS_SPACE(*pszNext)
                       || (RT_C_IS_PUNCT(*pszNext) && *pszNext != '-'))
                    pszNext++;

                uint32_t cDashes = 0;
                while (*pszNext == '-')
                    cDashes++, pszNext++;
                bool fExternal = cDashes > 10;

                if (   *pszNext == '\0'
                    || fExternal)
                {
                    /* In C/C++ code, this must be a multiline comment.  While in python it
                       must be a doc-string. */
                    if (pState->enmCommentStyle == kScmCommentStyle_C && pInfo->enmType != kScmCommentType_MultiLine)
                        ScmError(pState->pState, VERR_WRONG_ORDER, "License must appear in a multiline comment (no doxygen stuff)\n");
                    else if (pState->enmCommentStyle == kScmCommentStyle_Python && pInfo->enmType != kScmCommentType_DocString)
                        ScmError(pState->pState, VERR_WRONG_ORDER, "License must appear in a doc-string\n");

                    /* Quit if we've flagged a failure. */
                    if (RT_FAILURE(pState->pState->rc))
                        return VERR_CALLBACK_RETURN;

                    /* Record it. */
                    pState->iLineLicense        = iLine;
                    pState->cLinesLicense       = CountLinesInSubstring(pszBody, pszNext - pszBody) - fExternal;
                    pState->pCurrentLicense     = pCur;
                    pState->fExternalLicense    = fExternal;
                    pState->fIsCorrectLicense   = pCur == pState->pExpectedLicense;
                    pState->fWellFormedLicense  = memcmp(pszBody, pCur->psz, pCur->cch - 1) == 0;
                    if (!pState->fWellFormedLicense)
                        ScmVerbose(pState->pState, 1, "* license text isn't well-formed\n");

                    /* If there was more than one blank line between the copyright and the
                       license text, extend the license text area and force a rewrite of it. */
                    if (cBlankLinesAfterCopyright > 1)
                    {
                        ScmVerbose(pState->pState, 1, "* %u blank lines between copyright and license text, instead of 1\n",
                                   cBlankLinesAfterCopyright);
                        pState->iLineLicense -= cBlankLinesAfterCopyright - 1;
                        pState->cLinesLicense += cBlankLinesAfterCopyright - 1;
                        pState->fWellFormedLicense = false;
                    }

                    /* If there was more than one blank line after the license, trigger a rewrite. */
                    if (!fExternal && pInfo->cBlankLinesAfter != 1)
                    {
                        ScmVerbose(pState->pState, 1, "* copyright comment is followed by %u blank lines instead of 1\n",
                                   pInfo->cBlankLinesAfter);
                        pState->fWellFormedLicense = false;
                    }

                    /** @todo Check that the last comment line doesn't have any code on it. */
                    /** @todo Check that column 2 contains '*' for C/C++ files. */

                    ScmVerbose(pState->pState, 3,
                               "Found license %d/%d at %u..%u: is-correct=%RTbool well-formed=%RTbool external-part=%RTbool open-source=%RTbool\n",
                               pCur->enmType, pCur->enmOpt, pState->iLineLicense, pState->iLineLicense + pState->cLinesLicense,
                               pState->fIsCorrectLicense, pState->fWellFormedLicense,
                               pState->fExternalLicense, pState->fOpenSource);

                    if (fFoundCopyright)
                    {
                        pState->iLineComment  = pInfo->iLineStart;
                        pState->cLinesComment = (fExternal ? pState->iLineLicense + pState->cLinesLicense : pInfo->iLineEnd + 1)
                                              - pInfo->iLineStart;
                    }
                    else
                        ScmError(pState->pState, VERR_WRONG_ORDER, "License should be preceded by the copyright!\n");
                    break;
                }
            }
        }
    }

    if (fFoundCopyright && pState->iLineLicense == UINT32_MAX)
        ScmError(pState->pState, VERR_WRONG_ORDER, "Copyright should be followed by the license text!\n");

    /*
     * Stop looking for stuff after 100 comments.
     */
    if (pState->cComments > 100)
        return VERR_CALLBACK_RETURN;
    return VINF_SUCCESS;
}

/**
 * Writes comment body text.
 *
 * @returns Stream status.
 * @param   pOut                The output stream.
 * @param   pszText             The text to write.
 * @param   cchText             The length of the text.
 * @param   enmCommentStyle     The comment style.
 * @param   enmEol              The EOL style.
 */
static int scmWriteCommentBody(PSCMSTREAM pOut, const char *pszText, size_t cchText,
                               SCMCOMMENTSTYLE enmCommentStyle, SCMEOL enmEol)
{
    Assert(pszText[cchText - 1] == '\n');
    Assert(pszText[cchText - 2] != '\n');
    NOREF(cchText);
    do
    {
        const char *pszEol = strchr(pszText, '\n');
        if (pszEol != pszText)
        {
            ScmStreamWrite(pOut, g_aCopyrightCommentPrefix[enmCommentStyle].psz,
                           g_aCopyrightCommentPrefix[enmCommentStyle].cch);
            ScmStreamWrite(pOut, pszText, pszEol - pszText);
            ScmStreamPutEol(pOut, enmEol);
        }
        else
            ScmStreamPutLine(pOut, g_aCopyrightCommentEmpty[enmCommentStyle].psz,
                             g_aCopyrightCommentEmpty[enmCommentStyle].cch, enmEol);
        pszText = pszEol + 1;
    } while (*pszText != '\0');
    return ScmStreamGetStatus(pOut);
}


/**
 * Updates the copyright year and/or license text.
 *
 * @returns Modification state.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 * @param   enmCommentStyle     The comment style used by the file.
 */
static SCMREWRITERRES rewrite_Copyright_Common(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut,
                                                 PCSCMSETTINGSBASE pSettings, SCMCOMMENTSTYLE enmCommentStyle)
{
    if (   !pSettings->fUpdateCopyrightYear
        && pSettings->enmUpdateLicense == kScmLicense_LeaveAlone)
        return kScmUnmodified;

    /*
     * Try locate the relevant comments.
     */
    SCMCOPYRIGHTINFO Info =
    {
        /*.pState = */                  pState,
        /*.enmCommentStyle = */         enmCommentStyle,

        /*.cComments = */               0,

        /*.pszContributedBy = */        NULL,

        /*.iLineComment = */            UINT32_MAX,
        /*.cLinesComment = */           0,

        /*.iLineCopyright = */          UINT32_MAX,
        /*.uFirstYear = */              UINT32_MAX,
        /*.uLastYear = */               UINT32_MAX,
        /*.fWellFormedCopyright = */    false,
        /*.fUpToDateCopyright = */      false,

        /*.fOpenSource = */             true,
        /*.pExpectedLicense = */        NULL,
        /*.paLicenses = */                   pSettings->enmUpdateLicense != kScmLicense_Mit
                                          && pSettings->enmUpdateLicense != kScmLicense_BasedOnMit
                                        ? &g_aLicenses[0] : &g_aLicensesWithMit[0],
        /*.enmLicenceOpt = */           pSettings->enmUpdateLicense,
        /*.iLineLicense = */            UINT32_MAX,
        /*.cLinesLicense = */           0,
        /*.pCurrentLicense = */         NULL,
        /*.fIsCorrectLicense = */       false,
        /*.fWellFormedLicense = */      false,
        /*.fExternalLicense = */        false,

        /*.fCheckForLgpl = */           true,
        /*.iLineLgplNotice = */         UINT32_MAX,
        /*.iLineAfterLgplComment = */   UINT32_MAX,
        /*.iLineLgplDisclaimer = */     UINT32_MAX,
    };

    /* Figure Info.fOpenSource and the desired license: */
    char *pszSyncProcess;
    int rc = ScmSvnQueryProperty(pState, "svn:sync-process", &pszSyncProcess);
    if (RT_SUCCESS(rc))
    {
        Info.fOpenSource = strcmp(RTStrStrip(pszSyncProcess), "export") == 0;
        RTStrFree(pszSyncProcess);
    }
    else if (rc == VERR_NOT_FOUND)
        Info.fOpenSource = false;
    else
        return ScmError(pState, rc, "ScmSvnQueryProperty(svn:sync-process): %Rrc\n", rc);

    Info.pExpectedLicense = Info.paLicenses;
    if (Info.fOpenSource)
    {
        if (   pSettings->enmUpdateLicense != kScmLicense_Mit
            && pSettings->enmUpdateLicense != kScmLicense_BasedOnMit)
            while (Info.pExpectedLicense->enmOpt != pSettings->enmUpdateLicense)
                Info.pExpectedLicense++;
        else
            Assert(Info.pExpectedLicense->enmOpt == kScmLicense_Mit);
    }
    else
        while (Info.pExpectedLicense->enmType != kScmLicenseType_Confidential)
            Info.pExpectedLicense++;

    /* Scan the comments. */
    rc = ScmEnumerateComments(pIn, enmCommentStyle, rewrite_Copyright_CommentCallback, &Info);
    if (   (rc == VERR_CALLBACK_RETURN || RT_SUCCESS(rc))
        && RT_SUCCESS(pState->rc))
    {
        /*
         * Do conformity checks.
         */
        bool fAddLgplDisclaimer = false;
        if (Info.fCheckforLgpl)
        {
            if (   Info.iLineLgplNotice != UINT32_MAX
                && Info.iLineLgplDisclaimer == UINT32_MAX)
            {
                if (!pSettings->fLgplDisclaimer) /** @todo reconcile options with common sense. */
                    ScmError(pState, VERR_NOT_FOUND, "LGPL licence notice on line %u, but no LGPL disclaimer was found!\n",
                             Info.iLineLgplNotice + 1);
                else
                {
                    ScmVerbose(pState, 1, "* Need to add LGPL disclaimer\n");
                    fAddLgplDisclaimer = true;
                }
            }
            else if (   Info.iLineLgplNotice == UINT32_MAX
                     && Info.iLineLgplDisclaimer != UINT32_MAX)
                ScmError(pState, VERR_NOT_FOUND, "LGPL disclaimer on line %u, but no LGPL copyright notice!\n",
                         Info.iLineLgplDisclaimer + 1);
        }

        if (!pSettings->fExternalCopyright)
        {
            if (Info.iLineCopyright == UINT32_MAX)
                ScmError(pState, VERR_NOT_FOUND, "Missing copyright!\n");
            if (Info.iLineLicense == UINT32_MAX)
                ScmError(pState, VERR_NOT_FOUND, "Missing license!\n");
        }
        else if (Info.iLineCopyright != UINT32_MAX)
            ScmError(pState, VERR_NOT_FOUND,
                     "Marked as external copyright only, but found non-external copyright statement at line %u!\n",
                     Info.iLineCopyright + 1);


        if (RT_SUCCESS(pState->rc))
        {
            /*
             * Do we need to make any changes?
             */
            bool fUpdateCopyright = !pSettings->fExternalCopyright
                                 && (   !Info.fWellFormedCopyright
                                     || (!Info.fUpToDateCopyright && pSettings->fUpdateCopyrightYear));
            bool fUpdateLicense   = !pSettings->fExternalCopyright
                                 && Info.enmLicenceOpt != kScmLicense_LeaveAlone
                                 && (   !Info.fWellFormedLicense
                                     || !Info.fIsCorrectLicense);
            if (   fUpdateCopyright
                || fUpdateLicense
                || fAddLgplDisclaimer)
            {
                Assert(Info.iLineComment != UINT32_MAX);
                Assert(Info.cLinesComment > 0);

                /*
                 * Okay, do the work.
                 */
                ScmStreamRewindForReading(pIn);

                if (pSettings->fUpdateCopyrightYear)
                    Info.uLastYear = g_uYear;

                uint32_t    iLine = 0;
                SCMEOL      enmEol;
                size_t      cchLine;
                const char *pchLine;
                while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
                {
                    if (   iLine == Info.iLineComment
                        && (fUpdateCopyright || fUpdateLicense) )
                    {
                        /* Leading blank line. */
                        ScmStreamPutLine(pOut, g_aCopyrightCommentStart[enmCommentStyle].psz,
                                         g_aCopyrightCommentStart[enmCommentStyle].cch, enmEol);

                        /* Contributed by someone? */
                        if (Info.pszContributedBy)
                        {
                            const char *psz = Info.pszContributedBy;
                            for (;;)
                            {
                                const char *pszEol = strchr(psz, '\n');
                                size_t cchContribLine = pszEol ? pszEol - psz : strlen(psz);
                                ScmStreamWrite(pOut, g_aCopyrightCommentPrefix[enmCommentStyle].psz,
                                               g_aCopyrightCommentPrefix[enmCommentStyle].cch);
                                ScmStreamWrite(pOut, psz, cchContribLine);
                                ScmStreamPutEol(pOut, enmEol);
                                if (!pszEol)
                                    break;
                                psz = pszEol + 1;
                            }

                            ScmStreamPutLine(pOut, g_aCopyrightCommentEmpty[enmCommentStyle].psz,
                                             g_aCopyrightCommentEmpty[enmCommentStyle].cch, enmEol);
                        }

                        /* Write the copyright comment line. */
                        ScmStreamWrite(pOut, g_aCopyrightCommentPrefix[enmCommentStyle].psz,
                                       g_aCopyrightCommentPrefix[enmCommentStyle].cch);

                        char   szCopyright[256];
                        size_t cchCopyright;
                        if (Info.uFirstYear == Info.uLastYear)
                            cchCopyright = RTStrPrintf(szCopyright, sizeof(szCopyright), "Copyright (C) %u %s",
                                                       Info.uFirstYear, g_szCopyrightHolder);
                        else
                            cchCopyright = RTStrPrintf(szCopyright, sizeof(szCopyright), "Copyright (C) %u-%u %s",
                                                       Info.uFirstYear, Info.uLastYear, g_szCopyrightHolder);

                        ScmStreamWrite(pOut, szCopyright, cchCopyright);
                        ScmStreamPutEol(pOut, enmEol);

                        if (pSettings->enmUpdateLicense != kScmLicense_BasedOnMit)
                        {
                            /* Blank line separating the two. */
                            ScmStreamPutLine(pOut, g_aCopyrightCommentEmpty[enmCommentStyle].psz,
                                             g_aCopyrightCommentEmpty[enmCommentStyle].cch, enmEol);

                            /* Write the license text. */
                            scmWriteCommentBody(pOut, Info.pExpectedLicense->psz, Info.pExpectedLicense->cch,
                                                enmCommentStyle, enmEol);

                            /* Final comment line. */
                            if (!Info.fExternalLicense)
                                ScmStreamPutLine(pOut, g_aCopyrightCommentEnd[enmCommentStyle].psz,
                                                 g_aCopyrightCommentEnd[enmCommentStyle].cch, enmEol);
                        }
                        else
                            Assert(Info.fExternalLicense);

                        /* Skip the copyright and license text in the input file. */
                        rc = ScmStreamGetStatus(pOut);
                        if (RT_SUCCESS(rc))
                        {
                            iLine = Info.iLineComment + Info.cLinesComment;
                            rc = ScmStreamSeekByLine(pIn, iLine);
                        }
                    }
                    /*
                     * Add LGPL disclaimer?
                     */
                    else if (   iLine == Info.iLineAfterLgplComment
                             && fAddLgplDisclaimer)
                    {
                        ScmStreamPutEol(pOut, enmEol);
                        ScmStreamPutLine(pOut, g_aCopyrightCommentStart[enmCommentStyle].psz,
                                         g_aCopyrightCommentStart[enmCommentStyle].cch, enmEol);
                        scmWriteCommentBody(pOut, g_szLgplDisclaimer, sizeof(g_szLgplDisclaimer) - 1,
                                            enmCommentStyle, enmEol);
                        ScmStreamPutLine(pOut, g_aCopyrightCommentEnd[enmCommentStyle].psz,
                                         g_aCopyrightCommentEnd[enmCommentStyle].cch, enmEol);

                        /* put the actual line */
                        rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
                        iLine++;
                    }
                    else
                    {
                        rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
                        iLine++;
                    }
                    if (RT_FAILURE(rc))
                    {
                        RTStrFree(Info.pszContributedBy);
                        return kScmUnmodified;
                    }
                } /* for each source line */

                RTStrFree(Info.pszContributedBy);
                return kScmModified;
            }
        }
    }
    else
        ScmError(pState, rc,  "ScmEnumerateComments: %Rrc\n", rc);
    NOREF(pState); NOREF(pOut);
    RTStrFree(Info.pszContributedBy);
    return kScmUnmodified;
}


/** Copyright updater for C-style comments.   */
SCMREWRITERRES rewrite_Copyright_CstyleComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_C);
}

/** Copyright updater for hash-prefixed comments.   */
SCMREWRITERRES rewrite_Copyright_HashComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Hash);
}

/** Copyright updater for REM-prefixed comments.   */
SCMREWRITERRES rewrite_Copyright_RemComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, determineBatchFileCommentStyle(pIn));
}

/** Copyright updater for python comments.   */
SCMREWRITERRES rewrite_Copyright_PythonComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Python);
}

/** Copyright updater for semicolon-prefixed comments.   */
SCMREWRITERRES rewrite_Copyright_SemicolonComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut,
                                                  PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Semicolon);
}

/** Copyright updater for sql  comments.   */
SCMREWRITERRES rewrite_Copyright_SqlComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Sql);
}

/** Copyright updater for tick-prefixed comments.   */
SCMREWRITERRES rewrite_Copyright_TickComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Tick);
}

/** Copyright updater for XML comments.   */
SCMREWRITERRES rewrite_Copyright_XmlComment(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    return rewrite_Copyright_Common(pState, pIn, pOut, pSettings, kScmCommentStyle_Xml);
}



/*********************************************************************************************************************************
*   Flower Box Section Markers                                                                                                   *
*********************************************************************************************************************************/

static bool isFlowerBoxSectionMarker(PSCMSTREAM pIn, const char *pchLine, size_t cchLine, uint32_t cchWidth,
                                     const char **ppchText, size_t *pcchText, bool *pfNeedFixing)
{
    *ppchText = NULL;
    *pcchText = 0;
    *pfNeedFixing = false;

    /*
     * The first line.
     */
    if (pchLine[0] != '/')
        return false;
    size_t offLine = 1;
    while (offLine < cchLine && pchLine[offLine] == '*')
        offLine++;
    if (offLine < 20)                   /* (Code below depend on a reasonable minimum here.) */
        return false;
    while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
        offLine++;
    if (offLine != cchLine)
        return false;

    size_t const cchBox = cchLine;
    *pfNeedFixing = cchBox != cchWidth;

    /*
     * The next line, extracting the text.
     */
    SCMEOL enmEol;
    pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
    if (cchLine < cchBox - 3)
        return false;

    offLine = 0;
    if (RT_C_IS_BLANK(pchLine[0]))
    {
        *pfNeedFixing = true;
        offLine = RT_C_IS_BLANK(pchLine[1]) ? 2 : 1;
    }

    if (pchLine[offLine] != '*')
        return false;
    offLine++;

    if (!RT_C_IS_BLANK(pchLine[offLine + 1]))
        return false;
    offLine++;

    while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
        offLine++;
    if (offLine >= cchLine)
        return false;
    if (!RT_C_IS_UPPER(pchLine[offLine]))
        return false;

    if (offLine != 4 || cchLine != cchBox)
        *pfNeedFixing = true;

    *ppchText = &pchLine[offLine];
    size_t const offText = offLine;

    /* From the end now. */
    offLine = cchLine - 1;
    while (RT_C_IS_BLANK(pchLine[offLine]))
        offLine--;

    if (pchLine[offLine] != '*')
        return false;
    offLine--;
    if (!RT_C_IS_BLANK(pchLine[offLine]))
        return false;
    offLine--;
    while (RT_C_IS_BLANK(pchLine[offLine]))
        offLine--;
    *pcchText = offLine - offText + 1;

    /*
     * Third line closes the box.
     */
    pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
    if (cchLine < cchBox - 3)
        return false;

    offLine = 0;
    if (RT_C_IS_BLANK(pchLine[0]))
    {
        *pfNeedFixing = true;
        offLine = RT_C_IS_BLANK(pchLine[1]) ? 2 : 1;
    }
    while (offLine < cchLine && pchLine[offLine] == '*')
        offLine++;
    if (offLine < cchBox - 4)
        return false;

    if (pchLine[offLine] != '/')
        return false;
    offLine++;

    if (offLine != cchBox)
        *pfNeedFixing = true;

    while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
        offLine++;
    if (offLine != cchLine)
        return false;

    return true;
}


/**
 * Flower box marker comments in C and C++ code.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_FixFlowerBoxMarkers(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fFixFlowerBoxMarkers)
        return kScmUnmodified;

    /*
     * Work thru the file line by line looking for flower box markers.
     */
    size_t      cChanges = 0;
    size_t      cBlankLines = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        /*
         * Get a likely match for a first line.
         */
        if (   pchLine[0] == '/'
            && cchLine > 20
            && pchLine[1] == '*'
            && pchLine[2] == '*'
            && pchLine[3] == '*')
        {
            size_t const offSaved = ScmStreamTell(pIn);
            char const  *pchText;
            size_t       cchText;
            bool         fNeedFixing;
            bool         fIsFlowerBoxSection = isFlowerBoxSectionMarker(pIn, pchLine, cchLine, pSettings->cchWidth,
                                                                        &pchText, &cchText, &fNeedFixing);
            if (   fIsFlowerBoxSection
                && (   fNeedFixing
                    || cBlankLines < pSettings->cMinBlankLinesBeforeFlowerBoxMakers) )
            {
                while (cBlankLines < pSettings->cMinBlankLinesBeforeFlowerBoxMakers)
                {
                    ScmStreamPutEol(pOut, enmEol);
                    cBlankLines++;
                }

                ScmStreamPutCh(pOut, '/');
                ScmStreamWrite(pOut, g_szAsterisks, pSettings->cchWidth - 1);
                ScmStreamPutEol(pOut, enmEol);

                static const char s_szLead[] = "*   ";
                ScmStreamWrite(pOut, s_szLead, sizeof(s_szLead) - 1);
                ScmStreamWrite(pOut, pchText, cchText);
                size_t offCurPlus1 = sizeof(s_szLead) - 1 + cchText + 1;
                ScmStreamWrite(pOut, g_szSpaces, offCurPlus1 < pSettings->cchWidth ? pSettings->cchWidth - offCurPlus1 : 1);
                ScmStreamPutCh(pOut, '*');
                ScmStreamPutEol(pOut, enmEol);

                ScmStreamWrite(pOut, g_szAsterisks, pSettings->cchWidth - 1);
                ScmStreamPutCh(pOut, '/');
                ScmStreamPutEol(pOut, enmEol);

                cChanges++;
                cBlankLines = 0;
                continue;
            }

            int rc = ScmStreamSeekAbsolute(pIn, offSaved);
            if (RT_FAILURE(rc))
                return kScmUnmodified;
        }

        int rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;

        /* Do blank line accounting so we can ensure at least two blank lines
           before each section marker. */
        if (!isBlankLine(pchLine, cchLine))
            cBlankLines = 0;
        else
            cBlankLines++;
    }
    if (cChanges > 0)
        ScmVerbose(pState, 2, " * Converted %zu flower boxer markers\n", cChanges);
    return cChanges != 0 ? kScmModified : kScmUnmodified;
}


/**
 * Looks for the start of a todo comment.
 *
 * @returns Offset into the line of the comment start sequence.
 * @param   pchLine             The line to search.
 * @param   cchLineBeforeTodo   The length of the line before the todo.
 * @param   pfSameLine          Indicates whether it's refering to a statemtn on
 *                              the same line comment (true), or the next
 *                              statement (false).
 */
static size_t findTodoCommentStart(char const *pchLine, size_t cchLineBeforeTodo, bool *pfSameLine)
{
    *pfSameLine = false;

    /* Skip one '@' or  '\\'. */
    char ch;
    if (   cchLineBeforeTodo > 2
        && (   ((ch = pchLine[cchLineBeforeTodo - 1]) == '@')
            || ch == '\\' ) )
        cchLineBeforeTodo--;

    /* Skip blanks. */
    while (   cchLineBeforeTodo > 2
           && RT_C_IS_BLANK(pchLine[cchLineBeforeTodo - 1]))
        cchLineBeforeTodo--;

    /* Look for same line indicator. */
    if (   cchLineBeforeTodo > 0
        && pchLine[cchLineBeforeTodo - 1] == '<')
    {
        *pfSameLine = true;
        cchLineBeforeTodo--;
    }

    /* Skip *s */
    while (   cchLineBeforeTodo > 1
           && pchLine[cchLineBeforeTodo - 1] == '*')
        cchLineBeforeTodo--;

    /* Do we have a comment opening sequence. */
    if (   cchLineBeforeTodo > 0
        && pchLine[cchLineBeforeTodo - 1] == '/'
        && (   (   cchLineBeforeTodo >= 2
                && pchLine[cchLineBeforeTodo - 2] == '/')
            || pchLine[cchLineBeforeTodo] == '*'))
    {
        /* Skip slashes at the start. */
        while (   cchLineBeforeTodo > 0
               && pchLine[cchLineBeforeTodo - 1] == '/')
            cchLineBeforeTodo--;

        return cchLineBeforeTodo;
    }

    return ~(size_t)0;
}


/**
 * Looks for a TODO or todo in the given line.
 *
 * @returns Offset into the line of found, ~(size_t)0 if not.
 * @param   pchLine             The line to search.
 * @param   cchLine             The length of the line.
 */
static size_t findTodo(char const *pchLine, size_t cchLine)
{
    if (cchLine >= 4 + 2)
    {
        /* We don't search the first to chars because we need the start of a comment.
           Also, skip the last three chars since we need at least four for a match. */
        size_t const cchLineT = cchLine - 3;
        if (   memchr(pchLine + 2, 't', cchLineT - 2) != NULL
            || memchr(pchLine + 2, 'T', cchLineT - 2) != NULL)
        {
            for (size_t off = 2; off < cchLineT; off++)
            {
                char ch = pchLine[off];
                if (   (   ch != 't'
                        && ch != 'T')
                    || (   (ch = pchLine[off + 1]) != 'o'
                        && ch != 'O')
                    || (   (ch = pchLine[off + 2]) != 'd'
                        && ch != 'D')
                    || (   (ch = pchLine[off + 3]) != 'o'
                        && ch != 'O')
                    || (   off + 4 != cchLine
                        && (ch = pchLine[off + 4]) != ' '
                        && ch != '\t'
                        && ch != ':'                /** @todo */
                        && (ch != '*' || off + 5 > cchLine || pchLine[off + 5] != '/')  /** @todo */
                        ) )
                { /* not a hit - likely */ }
                else
                    return off;
            }
        }
    }
    return ~(size_t)0;
}


/**
 * Doxygen todos in C and C++ code.
 *
 * @returns Modification state.
 * @param   pState              The rewriter state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_Fix_C_and_CPP_Todos(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fFixTodos)
        return kScmUnmodified;

    /*
     * Work thru the file line by line looking for the start of todo comments.
     */
    size_t      cChanges = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        /*
         * Look for the word 'todo' in the line.  We're currently only trying
         * to catch comments starting with the word todo and adjust the start of
         * the doxygen statement.
         */
        size_t offTodo = findTodo(pchLine, cchLine);
        if (   offTodo != ~(size_t)0
            && offTodo >= 2)
        {
            /* Work backwards to find the start of the comment. */
            bool fSameLine = false;
            size_t offCommentStart = findTodoCommentStart(pchLine, offTodo, &fSameLine);
            if (offCommentStart != ~(size_t)0)
            {
                char    szNew[64];
                size_t  cchNew = 0;
                szNew[cchNew++] = '/';
                szNew[cchNew++] = pchLine[offCommentStart + 1];
                szNew[cchNew++] = pchLine[offCommentStart + 1];
                if (fSameLine)
                    szNew[cchNew++] = '<';
                szNew[cchNew++] = ' ';
                szNew[cchNew++] = '@';
                szNew[cchNew++] = 't';
                szNew[cchNew++] = 'o';
                szNew[cchNew++] = 'd';
                szNew[cchNew++] = 'o';

                /* Figure out wheter to continue after the @todo statement opening, we'll strip ':'
                   but need to take into account that we might be at the end of the line before
                   adding the space. */
                size_t offTodoAfter = offTodo + 4;
                if (   offTodoAfter < cchLine
                    && pchLine[offTodoAfter] == ':')
                    offTodoAfter++;
                if (   offTodoAfter < cchLine
                    && RT_C_IS_BLANK(pchLine[offTodoAfter]))
                    offTodoAfter++;
                if (offTodoAfter < cchLine)
                    szNew[cchNew++] = ' ';

                /* Write it out. */
                ScmStreamWrite(pOut, pchLine, offCommentStart);
                ScmStreamWrite(pOut, szNew, cchNew);
                if (offTodoAfter < cchLine)
                    ScmStreamWrite(pOut, &pchLine[offTodoAfter], cchLine - offTodoAfter);
                ScmStreamPutEol(pOut, enmEol);

                /* Check whether we actually made any changes. */
                if (   cchNew != offTodoAfter - offCommentStart
                    || memcmp(szNew, &pchLine[offCommentStart], cchNew))
                    cChanges++;
                continue;
            }
        }

        int rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }
    if (cChanges > 0)
        ScmVerbose(pState, 2, " * Converted %zu todo statements.\n", cChanges);
    return cChanges != 0 ? kScmModified : kScmUnmodified;
}


/**
 * Tries to parse a C/C++ preprocessor include directive.
 *
 * This is resonably forgiving and expects sane input.
 *
 * @retval  kScmIncludeDir_Invalid if not a valid include directive.
 * @retval  kScmIncludeDir_Quoted
 * @retval  kScmIncludeDir_Bracketed
 * @retval  kScmIncludeDir_Macro
 *
 * @param   pState          The rewriter state (for repording malformed
 *                          directives).
 * @param   pchLine         The line to try parse as an include statement.
 * @param   cchLine         The line length.
 * @param   ppchFilename    Where to return the pointer to the filename part.
 * @param   pcchFilename    Where to return the length of the filename.
 */
SCMINCLUDEDIR ScmMaybeParseCIncludeLine(PSCMRWSTATE pState, const char *pchLine, size_t cchLine,
                                        const char **ppchFilename, size_t *pcchFilename)
{
    /* Skip leading spaces: */
    while (cchLine > 0 && RT_C_IS_BLANK(*pchLine))
        cchLine--, pchLine++;

    /* Check for '#': */
    if (cchLine > 0 && *pchLine == '#')
    {
        cchLine--;
        pchLine++;

        /* Skip spaces after '#' (optional): */
        while (cchLine > 0 && RT_C_IS_BLANK(*pchLine))
            cchLine--, pchLine++;

        /* Check for 'include': */
        static char const s_szInclude[] = "include";
        if (   cchLine >= sizeof(s_szInclude)
            && memcmp(pchLine, RT_STR_TUPLE(s_szInclude)) == 0)
        {
            cchLine -= sizeof(s_szInclude) - 1;
            pchLine += sizeof(s_szInclude) - 1;

            /* Skip spaces after 'include' word (optional): */
            while (cchLine > 0 && RT_C_IS_BLANK(*pchLine))
                cchLine--, pchLine++;
            if (cchLine > 0)
            {
                /* Quoted or bracketed? */
                char const chFirst = *pchLine;
                if (chFirst == '"' || chFirst == '<')
                {
                    cchLine--;
                    pchLine++;
                    const char *pchEnd = (const char *)memchr(pchLine, chFirst == '"' ? '"' : '>', cchLine);
                    if (pchEnd)
                    {
                        if (ppchFilename)
                            *ppchFilename = pchLine;
                        if (pcchFilename)
                            *pcchFilename = pchEnd - pchLine;
                        return chFirst == '"' ? kScmIncludeDir_Quoted : kScmIncludeDir_Bracketed;
                    }
                    ScmError(pState, VERR_PARSE_ERROR, "Unbalanced #include filename %s: %.*s\n",
                             chFirst == '"' ? "quotes" : "brackets" , cchLine, pchLine);
                }
                /* C prepreprocessor macro? */
                else if (ScmIsCIdentifierLeadChar(chFirst))
                {
                    size_t cchFilename = 1;
                    while (   cchFilename < cchLine
                           && ScmIsCIdentifierChar(pchLine[cchFilename]))
                        cchFilename++;
                    if (ppchFilename)
                        *ppchFilename = pchLine;
                    if (pcchFilename)
                        *pcchFilename = cchFilename;
                    return kScmIncludeDir_Macro;
                }
                else
                    ScmError(pState, VERR_PARSE_ERROR, "Malformed #include filename part: %.*s\n", cchLine, pchLine);
            }
            else
                ScmError(pState, VERR_PARSE_ERROR, "Missing #include filename!\n");
        }
    }

    if (ppchFilename)
        *ppchFilename = NULL;
    if (pcchFilename)
        *pcchFilename = 0;
    return kScmIncludeDir_Invalid;
}


/**
 * Fix err.h/errcore.h usage.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_Fix_Err_H(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fFixErrH)
        return kScmUnmodified;

    static struct
    {
        const char *pszHeader;
        unsigned    cchHeader;
        int         iLevel;
    } const s_aHeaders[] =
    {
        { RT_STR_TUPLE("iprt/errcore.h"), 1 },
        { RT_STR_TUPLE("iprt/err.h"),     2 },
        { RT_STR_TUPLE("VBox/err.h"),     3 },
    };
    static RTSTRTUPLE const g_aLevel1Statuses[] =  /* Note! Keep in sync with errcore.h content! */
    {
        { RT_STR_TUPLE("VINF_SUCCESS") },
        { RT_STR_TUPLE("VERR_GENERAL_FAILURE") },
        { RT_STR_TUPLE("VERR_INVALID_PARAMETER") },
        { RT_STR_TUPLE("VWRN_INVALID_PARAMETER") },
        { RT_STR_TUPLE("VERR_INVALID_MAGIC") },
        { RT_STR_TUPLE("VWRN_INVALID_MAGIC") },
        { RT_STR_TUPLE("VERR_INVALID_HANDLE") },
        { RT_STR_TUPLE("VWRN_INVALID_HANDLE") },
        { RT_STR_TUPLE("VERR_INVALID_POINTER") },
        { RT_STR_TUPLE("VERR_NO_MEMORY") },
        { RT_STR_TUPLE("VERR_PERMISSION_DENIED") },
        { RT_STR_TUPLE("VINF_PERMISSION_DENIED") },
        { RT_STR_TUPLE("VERR_VERSION_MISMATCH") },
        { RT_STR_TUPLE("VERR_NOT_IMPLEMENTED") },
        { RT_STR_TUPLE("VERR_INVALID_FLAGS") },
        { RT_STR_TUPLE("VERR_WRONG_ORDER") },
        { RT_STR_TUPLE("VERR_INVALID_FUNCTION") },
        { RT_STR_TUPLE("VERR_NOT_SUPPORTED") },
        { RT_STR_TUPLE("VINF_NOT_SUPPORTED") },
        { RT_STR_TUPLE("VERR_ACCESS_DENIED") },
        { RT_STR_TUPLE("VERR_INTERRUPTED") },
        { RT_STR_TUPLE("VINF_INTERRUPTED") },
        { RT_STR_TUPLE("VERR_TIMEOUT") },
        { RT_STR_TUPLE("VINF_TIMEOUT") },
        { RT_STR_TUPLE("VERR_BUFFER_OVERFLOW") },
        { RT_STR_TUPLE("VINF_BUFFER_OVERFLOW") },
        { RT_STR_TUPLE("VERR_TOO_MUCH_DATA") },
        { RT_STR_TUPLE("VERR_TRY_AGAIN") },
        { RT_STR_TUPLE("VINF_TRY_AGAIN") },
        { RT_STR_TUPLE("VERR_PARSE_ERROR") },
        { RT_STR_TUPLE("VERR_OUT_OF_RANGE") },
        { RT_STR_TUPLE("VERR_NUMBER_TOO_BIG") },
        { RT_STR_TUPLE("VWRN_NUMBER_TOO_BIG") },
        { RT_STR_TUPLE("VERR_CANCELLED") },
        { RT_STR_TUPLE("VERR_TRAILING_CHARS") },
        { RT_STR_TUPLE("VWRN_TRAILING_CHARS") },
        { RT_STR_TUPLE("VERR_TRAILING_SPACES") },
        { RT_STR_TUPLE("VWRN_TRAILING_SPACES") },
        { RT_STR_TUPLE("VERR_NOT_FOUND") },
        { RT_STR_TUPLE("VWRN_NOT_FOUND") },
        { RT_STR_TUPLE("VERR_INVALID_STATE") },
        { RT_STR_TUPLE("VWRN_INVALID_STATE") },
        { RT_STR_TUPLE("VERR_OUT_OF_RESOURCES") },
        { RT_STR_TUPLE("VWRN_OUT_OF_RESOURCES") },
        { RT_STR_TUPLE("VERR_END_OF_STRING") },
        { RT_STR_TUPLE("VERR_CALLBACK_RETURN") },
        { RT_STR_TUPLE("VINF_CALLBACK_RETURN") },
        { RT_STR_TUPLE("VERR_DUPLICATE") },
        { RT_STR_TUPLE("VERR_MISSING") },
        { RT_STR_TUPLE("VERR_BUFFER_UNDERFLOW") },
        { RT_STR_TUPLE("VINF_BUFFER_UNDERFLOW") },
        { RT_STR_TUPLE("VERR_NOT_AVAILABLE") },
        { RT_STR_TUPLE("VERR_MISMATCH") },
        { RT_STR_TUPLE("VERR_WRONG_TYPE") },
        { RT_STR_TUPLE("VWRN_WRONG_TYPE") },
        { RT_STR_TUPLE("VERR_WRONG_PARAMETER_COUNT") },
        { RT_STR_TUPLE("VERR_WRONG_PARAMETER_TYPE") },
        { RT_STR_TUPLE("VERR_INVALID_CLIENT_ID") },
        { RT_STR_TUPLE("VERR_INVALID_SESSION_ID") },
        { RT_STR_TUPLE("VERR_INCOMPATIBLE_CONFIG") },
        { RT_STR_TUPLE("VERR_INTERNAL_ERROR") },
        { RT_STR_TUPLE("VINF_GETOPT_NOT_OPTION") },
        { RT_STR_TUPLE("VERR_GETOPT_UNKNOWN_OPTION") },
    };

    /*
     * First pass: Scout #include err.h/errcore.h locations and usage.
     *
     * Note! This isn't entirely optimal since it's also parsing comments and
     *       strings, not just code.  However it does a decent job for now.
     */
    int         iIncludeLevel = 0;
    int         iUsageLevel   = 0;
    uint32_t    iLine         = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        iLine++;
        if (cchLine < 6)
            continue;

        /*
         * Look for #includes.
         */
        const char *pchHash = (const char *)memchr(pchLine, '#', cchLine);
        if (    pchHash
            && isSpanOfBlanks(pchLine, pchHash - pchLine))
        {
            const char     *pchFilename;
            size_t          cchFilename;
            SCMINCLUDEDIR   enmIncDir = ScmMaybeParseCIncludeLine(pState, pchLine, cchLine, &pchFilename, &cchFilename);
            if (   enmIncDir == kScmIncludeDir_Bracketed
                || enmIncDir == kScmIncludeDir_Quoted)
            {
                unsigned i = RT_ELEMENTS(s_aHeaders);
                while (i-- > 0)
                    if (   s_aHeaders[i].cchHeader == cchFilename
                        && RTStrNICmpAscii(pchFilename, s_aHeaders[i].pszHeader, cchFilename) == 0)
                    {
                        if (iIncludeLevel < s_aHeaders[i].iLevel)
                            iIncludeLevel = s_aHeaders[i].iLevel;
                        break;
                    }

                /* Special hack for error info. */
                if (cchFilename == sizeof("errmsgdata.h") - 1 && memcmp(pchFilename, RT_STR_TUPLE("errmsgdata.h")) == 0)
                    iUsageLevel = 4;

                /* Special hack for code templates. */
                if (   cchFilename >= sizeof(".cpp.h")
                    && memcmp(&pchFilename[cchFilename - sizeof(".cpp.h") + 1], RT_STR_TUPLE(".cpp.h")) == 0)
                    iUsageLevel = 4;
                continue;
            }
        }
        /*
         * Look for VERR_, VWRN_, VINF_ prefixed identifiers in the current line.
         */
        const char *pchHit = (const char *)memchr(pchLine, 'V', cchLine);
        if (pchHit)
        {
            const char *pchLeft = pchLine;
            size_t      cchLeft = cchLine;
            do
            {
                size_t cchLeftHit = &pchLeft[cchLeft] - pchHit;
                if (cchLeftHit < 6)
                    break;
                if (    pchHit[4] == '_'
                    && (   pchHit == pchLine
                        || !ScmIsCIdentifierChar(pchHit[-1]))
                    && (   (pchHit[1] == 'E' && pchHit[2] == 'R' && pchHit[3] == 'R')
                        || (pchHit[1] == 'W' && pchHit[2] == 'R' && pchHit[3] == 'N')
                        || (pchHit[1] == 'I' && pchHit[2] == 'N' && pchHit[3] == 'F') ) )
                {
                    size_t cchIdentifier = 5;
                    while (cchIdentifier < cchLeftHit && ScmIsCIdentifierChar(pchHit[cchIdentifier]))
                        cchIdentifier++;
                    ScmVerbose(pState, 4, "--- status code at %u col %zu: %.*s\n",
                               iLine, pchHit - pchLine, cchIdentifier, pchHit);

                    if (iUsageLevel <= 1)
                    {
                        iUsageLevel = 3; /* Cannot distingish between iprt/err.h and VBox/err.h, so pick the latter for now. */
                        for (unsigned i = 0; i < RT_ELEMENTS(g_aLevel1Statuses); i++)
                            if (   cchIdentifier == g_aLevel1Statuses[i].cch
                                && memcmp(pchHit, g_aLevel1Statuses[i].psz, cchIdentifier) == 0)
                            {
                                iUsageLevel = 1;
                                break;
                            }
                    }

                    pchLeft = pchHit     + cchIdentifier;
                    cchLeft = cchLeftHit - cchIdentifier;
                }
                else
                {
                    pchLeft = pchHit     + 1;
                    cchLeft = cchLeftHit - 1;
                }
                pchHit  = (const char *)memchr(pchLeft, 'V', cchLeft);
            } while (pchHit != NULL);
        }
    }
    ScmVerbose(pState, 3, "--- iIncludeLevel=%d iUsageLevel=%d\n", iIncludeLevel, iUsageLevel);

    /*
     * Second pass: Change err.h to errcore.h if we detected a need for change.
     */
    if (   iIncludeLevel <= iUsageLevel
        || iIncludeLevel <= 1 /* we cannot safely eliminate errcore.h includes atm. */)
        return kScmUnmodified;

    unsigned cChanges = 0;
    ScmStreamRewindForReading(pIn);
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        /*
         * Look for #includes to modify.
         */
        if (cchLine >= 6)
        {
            const char *pchHash = (const char *)memchr(pchLine, '#', cchLine);
            if (    pchHash
                && isSpanOfBlanks(pchLine, pchHash - pchLine))
            {
                const char     *pchFilename;
                size_t          cchFilename;
                SCMINCLUDEDIR   enmIncDir = ScmMaybeParseCIncludeLine(pState, pchLine, cchLine, &pchFilename, &cchFilename);
                if (   enmIncDir == kScmIncludeDir_Bracketed
                    || enmIncDir == kScmIncludeDir_Quoted)
                {
                    unsigned i = RT_ELEMENTS(s_aHeaders);
                    while (i-- > 0)
                        if (   s_aHeaders[i].cchHeader == cchFilename
                            && RTStrNICmpAscii(pchFilename, s_aHeaders[i].pszHeader, cchFilename) == 0)
                        {
                            ScmStreamWrite(pOut, pchLine, pchFilename - pchLine - 1);
                            ScmStreamWrite(pOut, RT_STR_TUPLE("<iprt/errcore.h>"));
                            size_t cchTrailing = &pchLine[cchLine] - &pchFilename[cchFilename + 1];
                            if (cchTrailing > 0)
                                ScmStreamWrite(pOut, &pchFilename[cchFilename + 1], cchTrailing);
                            ScmStreamPutEol(pOut, enmEol);
                            cChanges++;
                            pchLine = NULL;
                            break;
                        }
                    if (!pchLine)
                        continue;
                }
            }
        }

        int rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }
    ScmVerbose(pState, 2, " * Converted %zu err.h/errcore.h include statements.\n", cChanges);
    return kScmModified;
}

typedef struct
{
    const char     *pch;
    uint8_t         cch;
    uint8_t         cchSpaces;          /**< Number of expected spaces before the word. */
    bool            fSpacesBefore : 1;  /**< Whether there may be spaces or tabs before the word. */
    bool            fIdentifier   : 1;  /**< Whether we're to expect a C/C++ identifier rather than pch/cch. */
} SCMMATCHWORD;


static int ScmMatchWords(const char *pchLine, size_t cchLine, SCMMATCHWORD const *paWords, size_t cWords,
                         size_t *poffNext, PRTSTRTUPLE paIdentifiers, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;

    size_t offLine = 0;
    for (size_t i = 0; i < cWords; i++)
    {
        SCMMATCHWORD const *pWord = &paWords[i];

        /*
         * Deal with spaces preceding the word first:
         */
        if (pWord->fSpacesBefore)
        {
            size_t cchSpaces = 0;
            size_t cchTabs   = 0;
            while (offLine < cchLine)
            {
                const char ch = pchLine[offLine];
                if (ch == ' ')
                    cchSpaces++;
                else if (ch == '\t')
                    cchTabs++;
                else
                    break;
                offLine++;
            }

            if (cchSpaces == pWord->cchSpaces && cchTabs == 0)
            { /* likely */ }
            else if (cchSpaces == 0 && cchTabs == 0)
                return RTErrInfoSetF(pErrInfo, VERR_PARSE_ERROR, "expected space at offset %u", offLine);
            else
                rc = VWRN_TRAILING_SPACES;
        }
        else
            Assert(pWord->cchSpaces == 0);

        /*
         * C/C++ identifier?
         */
        if (pWord->fIdentifier)
        {
            if (offLine >= cchLine)
                return RTErrInfoSetF(pErrInfo, VERR_END_OF_STRING,
                                     "expected '%.*s' (C/C++ identifier) at offset %u, not end of string",
                                     pWord->cch, pWord->pch, offLine);
            if (!ScmIsCIdentifierLeadChar(pchLine[offLine]))
                return RTErrInfoSetF(pErrInfo, VERR_MISMATCH, "expected '%.*s' (C/C++ identifier) at offset %u",
                                     pWord->cch, pWord->pch, offLine);
            size_t const offStart = offLine++;
            while (offLine < cchLine && ScmIsCIdentifierChar(pchLine[offLine]))
                offLine++;
            if (paIdentifiers)
            {
                paIdentifiers->cch = offLine - offStart;
                paIdentifiers->psz = &pchLine[offStart];
                paIdentifiers++;
            }
        }
        /*
         * Match the exact word.
         */
        else if (   pWord->cch == 0
                 || (   pWord->cch <= cchLine - offLine
                     && !memcmp(pWord->pch, &pchLine[offLine], pWord->cch)))
            offLine += pWord->cch;
        else
            return RTErrInfoSetF(pErrInfo, VERR_MISMATCH, "expected '%.*s' at offset %u", pWord->cch, pWord->pch, offLine);
    }

    /*
     * Check for trailing characters/whatnot.
     */
    if (poffNext)
        *poffNext = offLine;
    else if (offLine != cchLine)
        rc = RTErrInfoSetF(pErrInfo, VERR_TRAILING_CHARS, "unexpected trailing characters at offset %u", offLine);
    return rc;
}


/**
 * Fix header file include guards and \#pragma once.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_FixHeaderGuards(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fFixHeaderGuards)
        return kScmUnmodified;

    /* always skip .cpp.h files */
    size_t cchFilename = strlen(pState->pszFilename);
    if (   cchFilename > sizeof(".cpp.h")
        && RTStrICmpAscii(&pState->pszFilename[cchFilename - sizeof(".cpp.h") + 1], ".cpp.h") == 0)
        return kScmUnmodified;

    RTERRINFOSTATIC ErrInfo;
    char            szNormalized[168];
    size_t          cchNormalized = 0;
    int             rc;
    bool            fRet = false;

    /*
     * Calculate the expected guard for this file, if so tasked.
     * ASSUMES pState->pszFilename is absolute as is pSettings->pszGuardRelativeToDir.
     */
    szNormalized[0] = '\0';
    if (pSettings->pszGuardRelativeToDir)
    {
        rc = RTStrCopy(szNormalized, sizeof(szNormalized), pSettings->pszGuardPrefix);
        if (RT_FAILURE(rc))
            return ScmError(pState, rc, "Guard prefix too long (or something): %s\n", pSettings->pszGuardPrefix);
        cchNormalized = strlen(szNormalized);
        if (strcmp(pSettings->pszGuardRelativeToDir, "{dir}") == 0)
            rc = RTStrCopy(&szNormalized[cchNormalized], sizeof(szNormalized) - cchNormalized,
                           RTPathFilename(pState->pszFilename));
        else if (strcmp(pSettings->pszGuardRelativeToDir, "{parent}") == 0)
        {
            const char *pszSrc = RTPathFilename(pState->pszFilename);
            if (!pszSrc || (uintptr_t)&pszSrc[-2] < (uintptr_t)pState->pszFilename || !RTPATH_IS_SLASH(pszSrc[-1]))
                return ScmError(pState, VERR_INTERNAL_ERROR, "Error calculating {parent} header guard!\n");
            pszSrc -= 2;
            while (   (uintptr_t)pszSrc > (uintptr_t)pState->pszFilename
                   && !RTPATH_IS_SLASH(pszSrc[-1])
                   && !RTPATH_IS_VOLSEP(pszSrc[-1]))
                pszSrc--;
            rc = RTStrCopy(&szNormalized[cchNormalized], sizeof(szNormalized) - cchNormalized, pszSrc);
        }
        else
            rc = RTPathCalcRelative(&szNormalized[cchNormalized], sizeof(szNormalized) - cchNormalized,
                                    pSettings->pszGuardRelativeToDir, false /*fFromFile*/, pState->pszFilename);
        if (RT_FAILURE(rc))
            return ScmError(pState, rc, "Error calculating guard prefix (RTPathCalcRelative): %Rrc\n", rc);
        char ch;
        while ((ch = szNormalized[cchNormalized]) != '\0')
        {
            if (!ScmIsCIdentifierChar(ch))
                szNormalized[cchNormalized] = '_';
            cchNormalized++;
        }
    }

    /*
     * First part looks for the #ifndef xxxx paired with #define xxxx.
     *
     * We blindly assume the first preprocessor directive in the file is the guard
     * and will be upset if this isn't the case.
     */
    RTSTRTUPLE  Guard       = { NULL, 0 };
    uint32_t    cBlankLines = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    for (;;)
    {
        pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        if (pchLine == NULL)
            return ScmError(pState, VERR_PARSE_ERROR, "Did not find any include guards!\n");
        if (cchLine >= 2)
        {
            const char *pchHash = (const char *)memchr(pchLine, '#', cchLine);
            if (    pchHash
                && isSpanOfBlanks(pchLine, pchHash - pchLine))
            {
                /* #ifndef xxxx */
                static const SCMMATCHWORD s_aIfndefGuard[] =
                {
                    { RT_STR_TUPLE("#"),                0, true, false },
                    { RT_STR_TUPLE("ifndef"),           0, true, false },
                    { RT_STR_TUPLE("IDENTIFIER"),       1, true, true },
                    { RT_STR_TUPLE(""),                 0, true, false },
                };
                rc = ScmMatchWords(pchLine, cchLine, s_aIfndefGuard, RT_ELEMENTS(s_aIfndefGuard),
                                   NULL /*poffNext*/, &Guard, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    return ScmError(pState, rc, "%u: Expected first preprocessor directive to be '#ifndef xxxx'. %s (%.*s)\n",
                                    ScmStreamTellLine(pIn) - 1, ErrInfo.Core.pszMsg, cchLine, pchLine);
                fRet |= rc != VINF_SUCCESS;
                ScmVerbose(pState, 3, "line %u in %s: #ifndef %.*s\n",
                           ScmStreamTellLine(pIn) - 1, pState->pszFilename, Guard.cch, Guard.psz);

                /* #define xxxx */
                pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
                if (!pchLine)
                    return ScmError(pState, VERR_PARSE_ERROR, "%u: Unexpected end of file after '#ifndef %.*s'\n",
                                    ScmStreamTellLine(pIn) - 1, Guard.cch, Guard.psz);
                const SCMMATCHWORD aDefineGuard[] =
                {
                    { RT_STR_TUPLE("#"),                0, true, false },
                    { RT_STR_TUPLE("define"),           0, true, false },
                    { Guard.psz, (uint8_t)Guard.cch,    1, true, false },
                    { RT_STR_TUPLE(""),                 0, true, false },
                };
                rc = ScmMatchWords(pchLine, cchLine, aDefineGuard, RT_ELEMENTS(aDefineGuard),
                                   NULL /*poffNext*/, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    return ScmError(pState, rc, "%u: Expected '#define %.*s' to follow '#ifndef %.*s'. %s (%.*s)\n",
                                    ScmStreamTellLine(pIn) - 1, Guard.cch, Guard.psz, Guard.cch, Guard.psz,
                                    ErrInfo.Core.pszMsg, cchLine, pchLine);
                fRet |= rc != VINF_SUCCESS;

                if (Guard.cch >= sizeof(szNormalized))
                    return ScmError(pState, VERR_BUFFER_OVERFLOW, "%u: Guard macro too long! %.*s\n",
                                    ScmStreamTellLine(pIn) - 2, Guard.cch, Guard.psz);

                if (szNormalized[0] != '\0')
                {
                    if (   Guard.cch != cchNormalized
                        || memcmp(Guard.psz, szNormalized, cchNormalized) != 0)
                    {
                        ScmVerbose(pState, 2, "guard changed from %.*s to %s\n", Guard.cch, Guard.psz, szNormalized);
                        ScmVerbose(pState, 2, "grep -rw %.*s ${WCROOT} | grep -Fv %s\n",
                                   Guard.cch, Guard.psz, pState->pszFilename);
                        fRet = true;
                    }
                    Guard.psz = szNormalized;
                    Guard.cch = cchNormalized;
                }

                /*
                 * Write guard, making sure we've got a single blank line preceding it.
                 */
                ScmStreamPutEol(pOut, enmEol);
                ScmStreamWrite(pOut, RT_STR_TUPLE("#ifndef "));
                ScmStreamWrite(pOut, Guard.psz, Guard.cch);
                ScmStreamPutEol(pOut, enmEol);
                ScmStreamWrite(pOut, RT_STR_TUPLE("#define "));
                ScmStreamWrite(pOut, Guard.psz, Guard.cch);
                rc = ScmStreamPutEol(pOut, enmEol);
                if (RT_FAILURE(rc))
                    return kScmUnmodified;
                break;
            }
        }

        if (!isBlankLine(pchLine, cchLine))
        {
            while (cBlankLines-- > 0)
                ScmStreamPutEol(pOut, enmEol);
            cBlankLines = 0;
            rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
            if (RT_FAILURE(rc))
                return kScmUnmodified;
        }
        else
            cBlankLines++;
    }

    /*
     * Look for pragma once wrapped in #ifndef RT_WITHOUT_PRAGMA_ONCE.
     */
    size_t const iPragmaOnce = ScmStreamTellLine(pIn);
    static const SCMMATCHWORD s_aIfndefRtWithoutPragmaOnce[] =
    {
        { RT_STR_TUPLE("#"),                        0, true, false },
        { RT_STR_TUPLE("ifndef"),                   0, true, false },
        { RT_STR_TUPLE("RT_WITHOUT_PRAGMA_ONCE"),   1, true, false },
        { RT_STR_TUPLE(""),                         0, true, false },
    };
    static const SCMMATCHWORD s_aPragmaOnce[] =
    {
        { RT_STR_TUPLE("#"),                        0, true, false },
        { RT_STR_TUPLE("pragma"),                   1, true, false },
        { RT_STR_TUPLE("once"),                     1, true, false},
        { RT_STR_TUPLE(""),                         0, true, false },
    };
    static const SCMMATCHWORD s_aEndif[] =
    {
        { RT_STR_TUPLE("#"),                        0, true, false },
        { RT_STR_TUPLE("endif"),                    0, true, false },
        { RT_STR_TUPLE(""),                         0, true, false },
    };

    /* #ifndef RT_WITHOUT_PRAGMA_ONCE */
    pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
    if (!pchLine)
        return ScmError(pState, VERR_PARSE_ERROR, "%u: Unexpected end of file after header guard!\n", iPragmaOnce + 1);
    size_t offNext;
    rc = ScmMatchWords(pchLine, cchLine, s_aIfndefRtWithoutPragmaOnce, RT_ELEMENTS(s_aIfndefRtWithoutPragmaOnce),
                       &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        fRet |= rc != VINF_SUCCESS;
        if (offNext != cchLine)
            return ScmError(pState, VERR_PARSE_ERROR, "%u: Characters trailing '#ifndef RT_WITHOUT_PRAGMA_ONCE' (%.*s)\n",
                            iPragmaOnce + 1, cchLine, pchLine);

        /* # pragma once */
        pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        if (!pchLine)
            return ScmError(pState, VERR_PARSE_ERROR, "%u: Unexpected end of file after '#ifndef RT_WITHOUT_PRAGMA_ONCE'\n",
                            iPragmaOnce + 2);
        rc = ScmMatchWords(pchLine, cchLine, s_aPragmaOnce, RT_ELEMENTS(s_aPragmaOnce),
                           NULL /*poffNext*/, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
            fRet |= rc != VINF_SUCCESS;
        else
            return ScmError(pState, rc, "%u: Expected '# pragma once' to follow '#ifndef RT_WITHOUT_PRAGMA_ONCE'! %s (%.*s)\n",
                            iPragmaOnce + 2, ErrInfo.Core.pszMsg, cchLine, pchLine);

        /* #endif */
        pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        if (!pchLine)
            return ScmError(pState, VERR_PARSE_ERROR, "%u: Unexpected end of file after '#ifndef RT_WITHOUT_PRAGMA_ONCE' and '#pragma once'\n",
                            iPragmaOnce + 3);
        rc = ScmMatchWords(pchLine, cchLine, s_aEndif, RT_ELEMENTS(s_aEndif),
                           NULL /*poffNext*/, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
            fRet |= rc != VINF_SUCCESS;
        else
            return ScmError(pState, rc,
                            "%u: Expected '#endif' to follow '#ifndef RT_WITHOUT_PRAGMA_ONCE' and '# pragma once'! %s (%.*s)\n",
                            iPragmaOnce + 3, ErrInfo.Core.pszMsg, cchLine, pchLine);
        ScmVerbose(pState, 3, "Found pragma once\n");
        fRet |= !pSettings->fPragmaOnce;
    }
    else
    {
        rc = ScmStreamSeekByLine(pIn, iPragmaOnce);
        if (RT_FAILURE(rc))
            return ScmError(pState, rc, "seek error\n");
        fRet |= pSettings->fPragmaOnce;
        ScmVerbose(pState, pSettings->fPragmaOnce ? 2 : 3, "Missing #pragma once\n");
    }

    /*
     * Write the pragma once stuff.
     */
    if (pSettings->fPragmaOnce)
    {
        ScmStreamPutLine(pOut, RT_STR_TUPLE("#ifndef RT_WITHOUT_PRAGMA_ONCE"), enmEol);
        ScmStreamPutLine(pOut, RT_STR_TUPLE("# pragma once"), enmEol);
        rc = ScmStreamPutLine(pOut, RT_STR_TUPLE("#endif"), enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }

    /*
     * Copy the rest of the file and remove pragma once statements, while
     * looking for the last #endif in the file.
     */
    size_t iEndIfIn = 0;
    size_t iEndIfOut = 0;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        if (cchLine > 2)
        {
            const char *pchHash = (const char *)memchr(pchLine, '#', cchLine);
            if (    pchHash
                && isSpanOfBlanks(pchLine, pchHash - pchLine))
            {
                size_t off = pchHash - pchLine + 1;
                while (off < cchLine && RT_C_IS_BLANK(pchLine[off]))
                    off++;
                /* #pragma once */
                if (   off + sizeof("pragma") - 1 <= cchLine
                    && !memcmp(&pchLine[off], RT_STR_TUPLE("pragma")))
                {
                    rc = ScmMatchWords(pchLine, cchLine, s_aPragmaOnce, RT_ELEMENTS(s_aPragmaOnce),
                                       &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        fRet = true;
                        continue;
                    }
                }
                /* #endif */
                else if (   off + sizeof("endif") - 1 <= cchLine
                         && !memcmp(&pchLine[off], RT_STR_TUPLE("endif")))
                {
                    iEndIfIn  = ScmStreamTellLine(pIn) - 1;
                    iEndIfOut = ScmStreamTellLine(pOut);
                }
            }
        }

        rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
    }

    /*
     * Check out the last endif, making sure it's well formed and make sure it has the
     * right kind of comment following it.
     */
    if (pSettings->fFixHeaderGuardEndif)
    {
        if (iEndIfOut == 0)
            return ScmError(pState, VERR_PARSE_ERROR, "Expected '#endif' at the end of the file...\n");
        rc = ScmStreamSeekByLine(pIn, iEndIfIn);
        if (RT_FAILURE(rc))
            return kScmUnmodified;
        rc = ScmStreamSeekByLine(pOut, iEndIfOut);
        if (RT_FAILURE(rc))
            return kScmUnmodified;

        pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        if (!pchLine)
            return ScmError(pState, VERR_INTERNAL_ERROR, "ScmStreamGetLine failed re-reading #endif!\n");

        char   szTmp[64 + sizeof(szNormalized)];
        size_t cchTmp;
        if (pSettings->fEndifGuardComment)
            cchTmp = RTStrPrintf(szTmp, sizeof(szTmp), "#endif /* !%.*s */", Guard.cch, Guard.psz);
        else
            cchTmp = RTStrPrintf(szTmp, sizeof(szTmp), "#endif"); /* lazy bird */
        fRet |= cchTmp != cchLine || memcmp(szTmp, pchLine, cchTmp) != 0;
        rc = ScmStreamPutLine(pOut, szTmp, cchTmp, enmEol);
        if (RT_FAILURE(rc))
            return kScmUnmodified;

        /* Copy out the remaining lines (assumes no #pragma once here). */
        while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
        {
            rc = ScmStreamPutLine(pOut, pchLine, cchLine, enmEol);
            if (RT_FAILURE(rc))
                return kScmUnmodified;
        }
    }

    return fRet ? kScmModified : kScmUnmodified;
}


/**
 * Checks for PAGE_SIZE, PAGE_SHIFT and PAGE_OFFSET_MASK w/o a GUEST_ or HOST_
 * prefix as well as banning PAGE_BASE_HC_MASK, PAGE_BASE_GC_MASK and
 * PAGE_BASE_MASK.
 *
 * @returns kScmUnmodified - requires manual fix.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_PageChecks(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF(pOut);
    if (!pSettings->fOnlyGuestHostPage && !pSettings->fNoASMMemPageUse)
        return kScmUnmodified;

    static RTSTRTUPLE const g_aWords[] =
    {
        { RT_STR_TUPLE("PAGE_SIZE") },
        { RT_STR_TUPLE("PAGE_SHIFT") },
        { RT_STR_TUPLE("PAGE_OFFSET_MASK") },
        { RT_STR_TUPLE("PAGE_BASE_MASK") },
        { RT_STR_TUPLE("PAGE_BASE_GC_MASK") },
        { RT_STR_TUPLE("PAGE_BASE_HC_MASK") },
        { RT_STR_TUPLE("PAGE_ADDRESS") },
        { RT_STR_TUPLE("PHYS_PAGE_ADDRESS") },
        { RT_STR_TUPLE("ASMMemIsZeroPage") },
        { RT_STR_TUPLE("ASMMemZeroPage") },
    };
    size_t const iFirstWord = pSettings->fOnlyGuestHostPage ? 0 : 7;
    size_t const iEndWords  = pSettings->fNoASMMemPageUse   ? 9 : 7;

    uint32_t    iLine = 0;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        iLine++;
        for (size_t i = iFirstWord; i < iEndWords; i++)
        {
            size_t const cchWord = g_aWords[i].cch;
            if (cchLine >= cchWord)
            {
                const char * const pszWord = g_aWords[i].psz;
                const char        *pchHit  = (const char *)memchr(pchLine, *pszWord, cchLine);
                while (pchHit)
                {
                    size_t cchLeft = (uintptr_t)&pchLine[cchLine] - (uintptr_t)pchHit;
                    if (   cchLeft >= cchWord
                        && memcmp(pchHit, pszWord, cchWord) == 0
                        && (   pchHit == pchLine
                            || !ScmIsCIdentifierChar(pchHit[-1]))
                        && (   cchLeft == cchWord
                            || !ScmIsCIdentifierChar(pchHit[cchWord])) )
                    {
                        if (i < 3)
                            ScmFixManually(pState, "%u:%zu: %s is not allow! Use GUEST_%s or HOST_%s instead.\n",
                                           iLine, pchHit - pchLine + 1, pszWord, pszWord, pszWord);
                        else if (i < 7)
                            ScmFixManually(pState, "%u:%zu: %s is not allow! Rewrite using GUEST/HOST_PAGE_OFFSET_MASK.\n",
                                           iLine, pchHit - pchLine + 1, pszWord);
                        else
                            ScmFixManually(pState, "%u:%zu: %s is not allow! Use %s with correct page size instead.\n",
                                           iLine, pchHit - pchLine + 1, pszWord, i == 3 ? "ASMMemIsZero" : "RT_BZERO");
                    }

                    /* next */
                    cchLeft -= 1;
                    if (cchLeft < cchWord)
                        break;
                    pchHit = (const char *)memchr(pchHit + 1, *pszWord, cchLeft);
                }
            }
        }
    }

    return kScmUnmodified;
}


/**
 * Checks for usage of rc in code instead of vrc for IPRT status codes (int) and hrc for COM
 * status codes (HRESULT).
 *
 * @returns kScmUnmodified - requires manual fix.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 *
 * @note Used in Main to avoid ambiguity when just using rc.
 */
SCMREWRITERRES rewrite_ForceHrcVrcInsteadOfRc(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF(pOut);
    if (!pSettings->fOnlyHrcVrcInsteadOfRc)
        return kScmUnmodified;

    static const SCMMATCHWORD s_aHresultVrc[] =
    {
        { RT_STR_TUPLE("HRESULT"),                  0, true, false },
        { RT_STR_TUPLE("vrc"),                      1, true, false }
    };

    static const SCMMATCHWORD s_aIntHrc[] =
    {
        { RT_STR_TUPLE("int"),                      0, true, false },
        { RT_STR_TUPLE("hrc"),                      1, true, false }
    };

    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char      *pchLine;
    RTERRINFOSTATIC ErrInfo;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        iLine++;

        /* Look for forbidden declarations first. */
        size_t offNext = 0;
        int rc = ScmMatchWords(pchLine, cchLine, s_aHresultVrc, RT_ELEMENTS(s_aHresultVrc),
                               &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            ScmFixManually(pState, "%u:%zu: 'HRESULT vrc' is not allowed! Use 'HRESULT hrc' instead.\n",
                           iLine, offNext);
            continue;
        }

        rc = ScmMatchWords(pchLine, cchLine, s_aIntHrc, RT_ELEMENTS(s_aIntHrc),
                           &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            ScmFixManually(pState, "%u:%zu: 'int hrc' is not allowed! Use 'int vrc' instead.\n",
                           iLine, offNext);
            continue;
        }

#if 0 /* This is too broad and triggers on things we don't want to trigger on (like autoCaller.rc()). */
        const RTSTRTUPLE RcTuple = { RT_STR_TUPLE("rc") };
        size_t const cchWord = RcTuple.cch;
        if (cchLine >= cchWord)
        {
            const char        *pchHit  = (const char *)memchr(pchLine, *RcTuple.psz, cchLine);
            while (pchHit)
            {
                size_t cchLeft = (uintptr_t)&pchLine[cchLine] - (uintptr_t)pchHit;
                if (   cchLeft >= cchWord
                    && memcmp(pchHit, RcTuple.psz, cchWord) == 0
                    && (   pchHit == pchLine
                        || !ScmIsCIdentifierChar(pchHit[-1]))
                    && (   cchLeft == cchWord
                        || !ScmIsCIdentifierChar(pchHit[cchWord])) )
                    ScmFixManually(pState, "%u:%zu: %s is not allowed! Use hrc or vrc instead.\n",
                                   iLine, pchHit - pchLine + 1, RcTuple.psz);

                /* next */
                cchLeft -= 1;
                if (cchLeft < cchWord)
                    break;
                pchHit = (const char *)memchr(pchHit + 1, *RcTuple.psz, cchLeft);
            }
        }
#else
        /* Trigger on declarations of 'HRESULT rc' and 'int rc'. */
        static const SCMMATCHWORD s_aHresultRc[] =
        {
            { RT_STR_TUPLE("HRESULT"),                  0, true, false },
            { RT_STR_TUPLE("rc"),                       1, true, false }
        };

        static const SCMMATCHWORD s_aIntRc[] =
        {
            { RT_STR_TUPLE("int"),                      0, true, false },
            { RT_STR_TUPLE("rc"),                       1, true, false }
        };

        rc = ScmMatchWords(pchLine, cchLine, s_aHresultRc, RT_ELEMENTS(s_aHresultRc),
                           &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            ScmFixManually(pState, "%u:%zu: 'HRESULT rc' is not allowed! Use 'HRESULT hrc' instead.\n",
                           iLine, offNext);
            continue;
        }

        rc = ScmMatchWords(pchLine, cchLine, s_aIntRc, RT_ELEMENTS(s_aIntRc),
                           &offNext, NULL /*paIdentifiers*/, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            ScmFixManually(pState, "%u:%zu: 'int rc' is not allowed! Use 'int vrc' instead.\n",
                           iLine, offNext);
            continue;
        }
#endif
    }

    return kScmUnmodified;
}


/**
 * Rewrite a C/C++ source or header file.
 *
 * @returns Modification state.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 *
 * @todo
 *
 * Ideas for C/C++:
 *      - space after if, while, for, switch
 *      - spaces in for (i=0;i<x;i++)
 *      - complex conditional, bird style.
 *      - remove unnecessary parentheses.
 *      - sort defined RT_OS_*||  and RT_ARCH
 *      - sizeof without parenthesis.
 *      - defined without parenthesis.
 *      - trailing spaces.
 *      - parameter indentation.
 *      - space after comma.
 *      - while (x--); -> multi line + comment.
 *      - else statement;
 *      - space between function and left parenthesis.
 *      - TODO, XXX, @todo cleanup.
 *      - Space before/after '*'.
 *      - ensure new line at end of file.
 *      - Indentation of precompiler statements (#ifdef, #defines).
 *      - space between functions.
 *      - string.h -> iprt/string.h, stdarg.h -> iprt/stdarg.h, etc.
 */
SCMREWRITERRES rewrite_C_and_CPP(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{

    RT_NOREF4(pState, pIn, pOut, pSettings);
    return kScmUnmodified;
}

