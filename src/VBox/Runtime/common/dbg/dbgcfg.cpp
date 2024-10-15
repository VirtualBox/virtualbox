/* $Id: dbgcfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debugging Configuration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DBG
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/env.h>
#include <iprt/file.h>
#ifdef  IPRT_WITH_HTTP
# include <iprt/http.h>
#endif
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * String list entry.
 */
typedef struct RTDBGCFGSTR
{
    /** List entry. */
    RTLISTNODE  ListEntry;
    /** Domain specific flags. */
    uint16_t    fFlags;
    /** The length of the string. */
    uint16_t    cch;
    /** The string. */
    char        sz[1];
} RTDBGCFGSTR;
/** Pointer to a string list entry. */
typedef RTDBGCFGSTR *PRTDBGCFGSTR;


/**
 * Configuration instance.
 */
typedef struct RTDBGCFGINT
{
    /** The magic value (RTDBGCFG_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** Flags, see RTDBGCFG_FLAGS_XXX. */
    uint64_t            fFlags;

    /** List of paths to search for debug files and executable images. */
    RTLISTANCHOR        PathList;
    /** List of debug file suffixes. */
    RTLISTANCHOR        SuffixList;
    /** List of paths to search for source files. */
    RTLISTANCHOR        SrcPathList;

#ifdef RT_OS_WINDOWS
    /** The _NT_ALT_SYMBOL_PATH and _NT_SYMBOL_PATH combined. */
    RTLISTANCHOR        NtSymbolPathList;
    /** The _NT_EXECUTABLE_PATH. */
    RTLISTANCHOR        NtExecutablePathList;
    /** The _NT_SOURCE_PATH. */
    RTLISTANCHOR        NtSourcePath;
#endif

    /** Log callback function. */
    PFNRTDBGCFGLOG      pfnLogCallback;
    /** User argument to pass to the log callback. */
    void               *pvLogUser;

    /** Critical section protecting the instance data. */
    RTCRITSECTRW        CritSect;
} *PRTDBGCFGINT;

/**
 * Mnemonics map entry for a 64-bit unsigned property value.
 */
typedef struct RTDBGCFGU64MNEMONIC
{
    /** The flags to set or clear. */
    uint64_t    fFlags;
    /** The mnemonic. */
    const char *pszMnemonic;
    /** The length of the mnemonic. */
    uint8_t     cchMnemonic;
    /** If @c true, the bits in fFlags will be set, if @c false they will be
     *  cleared. */
    bool        fSet;
} RTDBGCFGU64MNEMONIC;
/** Pointer to a read only mnemonic map entry for a uint64_t property. */
typedef RTDBGCFGU64MNEMONIC const *PCRTDBGCFGU64MNEMONIC;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a debug module handle and returns rc if not valid. */
#define RTDBGCFG_VALID_RETURN_RC(pThis, rc) \
    do { \
        AssertPtrReturn((pThis), (rc)); \
        AssertReturn((pThis)->u32Magic == RTDBGCFG_MAGIC, (rc)); \
        AssertReturn((pThis)->cRefs > 0, (rc)); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Mnemonics map for RTDBGCFGPROP_FLAGS. */
static const RTDBGCFGU64MNEMONIC g_aDbgCfgFlags[] =
{
    {   RTDBGCFG_FLAGS_DEFERRED,                RT_STR_TUPLE("deferred"),       true  },
    {   RTDBGCFG_FLAGS_DEFERRED,                RT_STR_TUPLE("nodeferred"),     false },
    {   RTDBGCFG_FLAGS_NO_SYM_SRV,              RT_STR_TUPLE("symsrv"),         false },
    {   RTDBGCFG_FLAGS_NO_SYM_SRV,              RT_STR_TUPLE("nosymsrv"),       true  },
    {   RTDBGCFG_FLAGS_NO_SYSTEM_PATHS,         RT_STR_TUPLE("syspaths"),       false },
    {   RTDBGCFG_FLAGS_NO_SYSTEM_PATHS,         RT_STR_TUPLE("nosyspaths"),     true  },
    {   RTDBGCFG_FLAGS_NO_RECURSIV_SEARCH,      RT_STR_TUPLE("rec"),            false },
    {   RTDBGCFG_FLAGS_NO_RECURSIV_SEARCH,      RT_STR_TUPLE("norec"),          true  },
    {   RTDBGCFG_FLAGS_NO_RECURSIV_SRC_SEARCH,  RT_STR_TUPLE("recsrc"),         false },
    {   RTDBGCFG_FLAGS_NO_RECURSIV_SRC_SEARCH,  RT_STR_TUPLE("norecsrc"),       true  },
    {   0,                                      NULL, 0,                        false }
};


/** Interesting bundle suffixes. */
static const char * const g_apszBundleSuffixes[] =
{
    ".kext",
    ".app",
    ".framework",
    ".component",
    ".action",
    ".caction",
    ".bundle",
    ".sourcebundle",
    ".menu",
    ".plugin",
    ".ppp",
    ".monitorpanel",
    ".scripting",
    ".prefPane",
    ".qlgenerator",
    ".brailledriver",
    ".saver",
    ".SpeechVoice",
    ".SpeechRecognizer",
    ".SpeechSynthesizer",
    ".mdimporter",
    ".spreporter",
    ".xpc",
    NULL
};

/** Debug bundle suffixes. (Same as above + .dSYM) */
static const char * const g_apszDSymBundleSuffixes[] =
{
    ".dSYM",
    ".kext.dSYM",
    ".app.dSYM",
    ".framework.dSYM",
    ".component.dSYM",
    ".action.dSYM",
    ".caction.dSYM",
    ".bundle.dSYM",
    ".sourcebundle.dSYM",
    ".menu.dSYM",
    ".plugin.dSYM",
    ".ppp.dSYM",
    ".monitorpanel.dSYM",
    ".scripting.dSYM",
    ".prefPane.dSYM",
    ".qlgenerator.dSYM",
    ".brailledriver.dSYM",
    ".saver.dSYM",
    ".SpeechVoice.dSYM",
    ".SpeechRecognizer.dSYM",
    ".SpeechSynthesizer.dSYM",
    ".mdimporter.dSYM",
    ".spreporter.dSYM",
    ".xpc.dSYM",
    NULL
};



/**
 * Runtime logging, level 1.
 *
 * @param   pThis               The debug config instance data.
 * @param   pszFormat           The message format string.
 * @param   ...                 Arguments references in the format string.
 */
static void rtDbgCfgLog1(PRTDBGCFGINT pThis, const char *pszFormat, ...)
{
    if (LogIsEnabled() || (pThis && pThis->pfnLogCallback))
    {
        va_list va;
        va_start(va, pszFormat);
        char *pszMsg = RTStrAPrintf2V(pszFormat, va);
        va_end(va);

        Log(("RTDbgCfg: %s", pszMsg));
        if (pThis && pThis->pfnLogCallback)
            pThis->pfnLogCallback(pThis, 1, pszMsg, pThis->pvLogUser);
        RTStrFree(pszMsg);
    }
}


/**
 * Runtime logging, level 2.
 *
 * @param   pThis               The debug config instance data.
 * @param   pszFormat           The message format string.
 * @param   ...                 Arguments references in the format string.
 */
static void rtDbgCfgLog2(PRTDBGCFGINT pThis, const char *pszFormat, ...)
{
    if (LogIs2Enabled() || (pThis && pThis->pfnLogCallback))
    {
        va_list va;
        va_start(va, pszFormat);
        char *pszMsg = RTStrAPrintf2V(pszFormat, va);
        va_end(va);

        Log(("RTDbgCfg: %s", pszMsg));
        if (pThis && pThis->pfnLogCallback)
            pThis->pfnLogCallback(pThis, 2, pszMsg, pThis->pvLogUser);
        RTStrFree(pszMsg);
    }
}


/**
 * Checks if the file system at the given path is case insensitive or not.
 *
 * @returns true / false
 * @param   pszPath             The path to query about.
 */
static int rtDbgCfgIsFsCaseInsensitive(const char *pszPath)
{
    RTFSPROPERTIES Props;
    int rc = RTFsQueryProperties(pszPath, &Props);
    if (RT_FAILURE(rc))
        return RT_OPSYS == RT_OPSYS_DARWIN
            || RT_OPSYS == RT_OPSYS_DOS
            || RT_OPSYS == RT_OPSYS_OS2
            || RT_OPSYS == RT_OPSYS_NT
            || RT_OPSYS == RT_OPSYS_WINDOWS;
    return !Props.fCaseSensitive;
}


/**
 * Worker that does case sensitive file/dir searching.
 *
 * @returns true / false.
 * @param   pszPath         The path buffer containing an existing directory and
 *                          at @a offLastComp the name we're looking for.
 *                          RTPATH_MAX in size.  On success, this last component
 *                          will have the correct case.  On failure, the last
 *                          component is stripped off.
 * @param   offLastComp     The offset of the last component (for chopping it
 *                          off).
 * @param   enmType         What kind of thing we're looking for.
 */
static bool rtDbgCfgIsXxxxAndFixCaseWorker(char *pszPath, size_t offLastComp, RTDIRENTRYTYPE enmType)
{
    /** @todo IPRT should generalize this so we can use host specific tricks to
     *        speed it up. */

    char *pszName = &pszPath[offLastComp];

    /* Return straight away if the name isn't case foldable. */
    if (!RTStrIsCaseFoldable(pszName))
    {
        *pszName = '\0';
        return false;
    }

    /*
     * Try some simple case folding games.
     */
    RTStrToLower(pszName);
    if (RTFileExists(pszPath))
        return true;

    RTStrToUpper(pszName);
    if (RTFileExists(pszPath))
        return true;

    /*
     * Open the directory and check each entry in it.
     */
    char chSaved = *pszName;
    *pszName = '\0';

    RTDIR hDir;
    int rc = RTDirOpen(&hDir, pszPath);
    if (RT_FAILURE(rc))
        return false;

    *pszName = chSaved;

    for (;;)
    {
        /* Read the next entry. */
        union
        {
            RTDIRENTRY  Entry;
            uint8_t     ab[_4K];
        } u;
        size_t cbBuf = sizeof(u);
        rc = RTDirRead(hDir, &u.Entry, &cbBuf);
        if (RT_FAILURE(rc))
            break;

        if (   !RTStrICmp(pszName, u.Entry.szName)
            && (   u.Entry.enmType == enmType
                || u.Entry.enmType == RTDIRENTRYTYPE_UNKNOWN
                || u.Entry.enmType == RTDIRENTRYTYPE_SYMLINK) )
        {
            strcpy(pszName, u.Entry.szName);
            if (u.Entry.enmType != enmType)
                RTDirQueryUnknownType(pszPath, true /*fFollowSymlinks*/, &u.Entry.enmType);
            if (u.Entry.enmType == enmType)
            {
                RTDirClose(hDir);
                return true;
            }
        }
    }

    RTDirClose(hDir);
    *pszName = '\0';

    return false;
}


/**
 * Appends @a pszSubDir to @a pszPath and check whether it exists and is a
 * directory.
 *
 * If @a fCaseInsensitive is set, we will do a case insensitive search for a
 * matching sub directory.
 *
 * @returns true / false
 * @param   pszPath             The path buffer containing an existing
 *                              directory.  RTPATH_MAX in size.
 * @param   pszSubDir           The sub directory to append.
 * @param   fCaseInsensitive    Whether case insensitive searching is required.
 */
static bool rtDbgCfgIsDirAndFixCase(char *pszPath, const char *pszSubDir, bool fCaseInsensitive)
{
    /* Save the length of the input path so we can restore it in the case
       insensitive branch further down. */
    size_t const cchPath = strlen(pszPath);

    /*
     * Append the sub directory and check if we got a hit.
     */
    int rc = RTPathAppend(pszPath, RTPATH_MAX, pszSubDir);
    if (RT_FAILURE(rc))
        return false;

    if (RTDirExists(pszPath))
        return true;

    /*
     * Do case insensitive lookup if requested.
     */
    if (fCaseInsensitive)
        return rtDbgCfgIsXxxxAndFixCaseWorker(pszPath, cchPath, RTDIRENTRYTYPE_DIRECTORY);

    pszPath[cchPath] = '\0';
    return false;
}


/**
 * Appends @a pszSubDir1 and @a pszSuffix to @a pszPath and check whether it
 * exists and is a directory.
 *
 * If @a fCaseInsensitive is set, we will do a case insensitive search for a
 * matching sub directory.
 *
 * @returns true / false
 * @param   pszPath             The path buffer containing an existing
 *                              directory.  RTPATH_MAX in size.
 * @param   pszSubDir           The sub directory to append.
 * @param   pszSuffix           The suffix to append.
 * @param   fCaseInsensitive    Whether case insensitive searching is required.
 */
static bool rtDbgCfgIsDirAndFixCase2(char *pszPath, const char *pszSubDir, const char *pszSuffix, bool fCaseInsensitive)
{
    Assert(!strpbrk(pszSuffix, ":/\\"));

    /* Save the length of the input path so we can restore it in the case
       insensitive branch further down. */
    size_t const cchPath = strlen(pszPath);

    /*
     * Append the subdirectory and suffix, then check if we got a hit.
     */
    int rc = RTPathAppend(pszPath, RTPATH_MAX, pszSubDir);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCat(pszPath, RTPATH_MAX, pszSuffix);
        if (RT_SUCCESS(rc))
        {
            if (RTDirExists(pszPath))
                return true;

            /*
             * Do case insensitive lookup if requested.
             */
            if (fCaseInsensitive)
                return rtDbgCfgIsXxxxAndFixCaseWorker(pszPath, cchPath, RTDIRENTRYTYPE_DIRECTORY);
        }
    }

    pszPath[cchPath] = '\0';
    return false;
}


/**
 * Appends @a pszFilename to @a pszPath and check whether it exists and is a
 * directory.
 *
 * If @a fCaseInsensitive is set, we will do a case insensitive search for a
 * matching filename.
 *
 * @returns true / false
 * @param   pszPath             The path buffer containing an existing
 *                              directory.  RTPATH_MAX in size.
 * @param   pszFilename         The filename to append.
 * @param   pszSuffix           Optional filename suffix to append.
 * @param   fCaseInsensitive    Whether case insensitive searching is required.
 * @param   fMsCompressed       Whether to look for the MS compressed file name
 *                              variant.
 * @param   pfProbablyCompressed    This is set to true if a MS compressed
 *                                  filename variant is returned.  Optional.
 */
static bool rtDbgCfgIsFileAndFixCase(char *pszPath, const char *pszFilename, const char *pszSuffix, bool fCaseInsensitive,
                                     bool fMsCompressed, bool *pfProbablyCompressed)
{
    /* Save the length of the input path so we can restore it in the case
       insensitive branch further down. */
    size_t cchPath = strlen(pszPath);
    if (pfProbablyCompressed)
        *pfProbablyCompressed = false;

    /*
     * Append the filename and optionally suffix, then check if we got a hit.
     */
    int rc = RTPathAppend(pszPath, RTPATH_MAX, pszFilename);
    if (RT_FAILURE(rc))
        return false;
    if (pszSuffix)
    {
        Assert(!fMsCompressed);
        rc = RTStrCat(pszPath, RTPATH_MAX, pszSuffix);
        if (RT_FAILURE(rc))
            return false;
    }

    if (RTFileExists(pszPath))
        return true;

    /*
     * Do case insensitive file lookup if requested.
     */
    if (fCaseInsensitive)
    {
        if (rtDbgCfgIsXxxxAndFixCaseWorker(pszPath, cchPath, RTDIRENTRYTYPE_FILE))
            return true;
    }

    /*
     * Look for MS compressed file if requested.
     */
    if (   fMsCompressed
        && (unsigned char)pszFilename[strlen(pszFilename) - 1] < 0x7f)
    {
        pszPath[cchPath] = '\0';
        rc = RTPathAppend(pszPath, RTPATH_MAX, pszFilename);
        AssertRCReturn(rc, false);
        pszPath[strlen(pszPath) - 1] = '_';

        if (pfProbablyCompressed)
            *pfProbablyCompressed = true;

        if (   RTFileExists(pszPath)
            || (   fCaseInsensitive
                && rtDbgCfgIsXxxxAndFixCaseWorker(pszPath, cchPath, RTDIRENTRYTYPE_FILE) ))
            return true;

        if (pfProbablyCompressed)
            *pfProbablyCompressed = false;
    }

    pszPath[cchPath] = '\0';
    return false;
}


static int rtDbgCfgTryOpenDir(PRTDBGCFGINT pThis, char *pszPath, PRTPATHSPLIT pSplitFn, uint32_t fFlags,
                              PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    int rcRet = VWRN_NOT_FOUND;
    int rc2;

    /* If the directory doesn't exist, just quit immediately.
       Note! Our case insensitivity doesn't extend to the search dirs themselfs,
             only to the bits under neath them. */
    if (!RTDirExists(pszPath))
    {
        rtDbgCfgLog2(pThis, "Dir does not exist: '%s'\n", pszPath);
        return rcRet;
    }

    /* Figure out whether we have to do a case sensitive search or not.
       Note! As a simplification, we don't ask for case settings in each
             directory under the user specified path, we assume the file
             systems that mounted there have compatible settings. Faster
             that way. */
    bool const fCaseInsensitive = (fFlags & RTDBGCFG_O_CASE_INSENSITIVE)
                               && !rtDbgCfgIsFsCaseInsensitive(pszPath);

    size_t const cchPath = strlen(pszPath);

    /*
     * Look for the file with less and less of the original path given.
     */
    for (unsigned i = RTPATH_PROP_HAS_ROOT_SPEC(pSplitFn->fProps); i < pSplitFn->cComps; i++)
    {
        pszPath[cchPath] = '\0';

        rc2 = VINF_SUCCESS;
        for (unsigned j = i; j < pSplitFn->cComps - 1U && RT_SUCCESS(rc2); j++)
            if (!rtDbgCfgIsDirAndFixCase(pszPath, pSplitFn->apszComps[i], fCaseInsensitive))
                rc2 = VERR_FILE_NOT_FOUND;

        if (RT_SUCCESS(rc2))
        {
            if (rtDbgCfgIsFileAndFixCase(pszPath, pSplitFn->apszComps[pSplitFn->cComps - 1], NULL /*pszSuffix*/,
                                         fCaseInsensitive, false, NULL))
            {
                rtDbgCfgLog1(pThis, "Trying '%s'...\n", pszPath);
                rc2 = pfnCallback(pThis, pszPath, pvUser1, pvUser2);
                if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                {
                    if (rc2 == VINF_CALLBACK_RETURN)
                        rtDbgCfgLog1(pThis, "Found '%s'.\n", pszPath);
                    else
                        rtDbgCfgLog1(pThis, "Error opening '%s'.\n", pszPath);
                    return rc2;
                }
                rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, pszPath);
                if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                    rcRet = rc2;
            }
        }
    }

    /*
     * Do a recursive search if requested.
     */
    if (   (fFlags & RTDBGCFG_O_RECURSIVE)
        && pThis
        && !(pThis->fFlags & RTDBGCFG_FLAGS_NO_RECURSIV_SEARCH) )
    {
        /** @todo Recursive searching will be done later. */
    }

    return rcRet;
}

static int rtDbgCfgUnpackMsCacheFile(PRTDBGCFGINT pThis, char *pszPath, const char *pszFilename)
{
    rtDbgCfgLog2(pThis, "Unpacking '%s'...\n", pszPath);

    /*
     * Duplicate the source file path, just for simplicity and restore the
     * final character in the orignal.  We cheerfully ignorining any
     * possibility of multibyte UTF-8 sequences just like the caller did when
     * setting it to '_'.
     */
    char *pszSrcArchive = RTStrDup(pszPath);
    if (!pszSrcArchive)
        return VERR_NO_STR_MEMORY;

    pszPath[strlen(pszPath) - 1] = RT_C_TO_LOWER(pszFilename[strlen(pszFilename) - 1]);


    /*
     * Figuring out the argument list for the platform specific unpack util.
     */
#ifdef RT_OS_WINDOWS
    RTPathChangeToDosSlashes(pszSrcArchive, false /*fForce*/);
    RTPathChangeToDosSlashes(pszPath, false /*fForce*/);
    const char *papszArgs[] =
    {
        "expand.exe",
        pszSrcArchive,
        pszPath,
        NULL
    };

#else
    char szExtractDir[RTPATH_MAX];
    strcpy(szExtractDir, pszPath);
    RTPathStripFilename(szExtractDir);

    const char *papszArgs[] =
    {
        "cabextract",
        "-L",                   /* Lower case extracted files. */
        "-d", szExtractDir,     /* Extraction path */
        pszSrcArchive,
        NULL
    };
#endif

    /*
     * Do the unpacking.
     */
    RTPROCESS hChild;
    int rc = RTProcCreate(papszArgs[0], papszArgs, RTENV_DEFAULT,
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                          RTPROC_FLAGS_NO_WINDOW | RTPROC_FLAGS_HIDDEN | RTPROC_FLAGS_SEARCH_PATH,
#else
                          RTPROC_FLAGS_SEARCH_PATH,
#endif
                          &hChild);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS ProcStatus;
        rc = RTProcWait(hChild, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus);
        if (RT_SUCCESS(rc))
        {
            if (   ProcStatus.enmReason == RTPROCEXITREASON_NORMAL
                && ProcStatus.iStatus   == 0)
            {
                if (RTPathExists(pszPath))
                {
                    rtDbgCfgLog1(pThis, "Successfully unpacked '%s' to '%s'.\n", pszSrcArchive, pszPath);
                    rc = VINF_SUCCESS;
                }
                else
                {
                    rtDbgCfgLog1(pThis, "Successfully ran unpacker on '%s', but '%s' is missing!\n", pszSrcArchive, pszPath);
                    rc = VERR_ZIP_ERROR;
                }
            }
            else
            {
                rtDbgCfgLog2(pThis, "Unpacking '%s' failed: iStatus=%d enmReason=%d\n",
                             pszSrcArchive, ProcStatus.iStatus, ProcStatus.enmReason);
                rc = VERR_ZIP_CORRUPTED;
            }
        }
        else
            rtDbgCfgLog1(pThis, "Error waiting for process: %Rrc\n", rc);

    }
    else
        rtDbgCfgLog1(pThis, "Error starting unpack process '%s': %Rrc\n", papszArgs[0], rc);

    return rc;
}


static int rtDbgCfgTryDownloadAndOpen(PRTDBGCFGINT pThis, const char *pszServer, char *pszPath,
                                      const char *pszCacheSubDir, const char *pszUuidMappingSubDir,
                                      PRTPATHSPLIT pSplitFn, const char *pszCacheSuffix, uint32_t fFlags,
                                      PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    RT_NOREF_PV(pszUuidMappingSubDir); /** @todo do we bother trying pszUuidMappingSubDir? */
    RT_NOREF_PV(pszCacheSuffix); /** @todo do we bother trying pszUuidMappingSubDir? */
    RT_NOREF_PV(fFlags);

    if (pThis->fFlags & RTDBGCFG_FLAGS_NO_SYM_SRV)
        return VWRN_NOT_FOUND;
    if (!pszCacheSubDir || !*pszCacheSubDir)
        return VWRN_NOT_FOUND;
    if (   !(fFlags & RTDBGCFG_O_SYMSRV)
        && !(fFlags & RTDBGCFG_O_DEBUGINFOD))
        return VWRN_NOT_FOUND;

    /*
     * Create the path.
     */
    size_t cchTmp = strlen(pszPath);

    int rc = RTDirCreateFullPath(pszPath, 0766);
    if (!RTDirExists(pszPath))
    {
        Log(("Error creating cache dir '%s': %Rrc\n", pszPath, rc));
        return rc;
    }

    const char *pszFilename = pSplitFn->apszComps[pSplitFn->cComps - 1];
    rc = RTPathAppend(pszPath, RTPATH_MAX, pszFilename);
    if (RT_FAILURE(rc))
        return rc;
    RTStrToLower(&pszPath[cchTmp]);
    if (!RTDirExists(pszPath))
    {
        rc = RTDirCreate(pszPath, 0766, 0);
        if (RT_FAILURE(rc))
        {
            Log(("RTDirCreate(%s) -> %Rrc\n", pszPath, rc));
        }
    }

    rc = RTPathAppend(pszPath, RTPATH_MAX, pszCacheSubDir);
    if (RT_FAILURE(rc))
        return rc;
    if (!RTDirExists(pszPath))
    {
        rc = RTDirCreate(pszPath, 0766, 0);
        if (RT_FAILURE(rc))
        {
            Log(("RTDirCreate(%s) -> %Rrc\n", pszPath, rc));
        }
    }

    /* Prepare the destination file name while we're here. */
    cchTmp = strlen(pszPath);
    RTStrToLower(&pszPath[cchTmp]);
    rc = RTPathAppend(pszPath, RTPATH_MAX, pszFilename);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Download/copy the file.
     */
    char szUrl[_2K];
    /* Download URL? */
    if (   RTStrIStartsWith(pszServer, "http://")
        || RTStrIStartsWith(pszServer, "https://")
        || RTStrIStartsWith(pszServer, "ftp://") )
    {
#ifdef IPRT_WITH_HTTP
        RTHTTP hHttp;
        rc = RTHttpCreate(&hHttp);
        if (RT_SUCCESS(rc))
        {
            RTHttpUseSystemProxySettings(hHttp);
            RTHttpSetFollowRedirects(hHttp, 8);

            static const char * const s_apszHeadersMsSymSrv[] =
            {
                "User-Agent: Microsoft-Symbol-Server/6.6.0999.9",
                "Pragma: no-cache",
            };

            static const char * const s_apszHeadersDebuginfod[] =
            {
                "User-Agent: IPRT DbgCfg 1.0",
                "Pragma: no-cache",
            };

            if (fFlags & RTDBGCFG_O_SYMSRV)
                rc = RTHttpSetHeaders(hHttp, RT_ELEMENTS(s_apszHeadersMsSymSrv), s_apszHeadersMsSymSrv);
            else /* Must be debuginfod. */
                rc = RTHttpSetHeaders(hHttp, RT_ELEMENTS(s_apszHeadersDebuginfod), s_apszHeadersDebuginfod);
            if (RT_SUCCESS(rc))
            {
                if (fFlags & RTDBGCFG_O_SYMSRV)
                    RTStrPrintf(szUrl, sizeof(szUrl), "%s/%s/%s/%s", pszServer, pszFilename, pszCacheSubDir, pszFilename);
                else
                    RTStrPrintf(szUrl, sizeof(szUrl), "%s/buildid/%s/debuginfo", pszServer, pszCacheSubDir);

                /** @todo Use some temporary file name and rename it after the operation
                 *        since not all systems support read-deny file sharing
                 *        settings. */
                rtDbgCfgLog2(pThis, "Downloading '%s' to '%s'...\n", szUrl, pszPath);
                rc = RTHttpGetFile(hHttp, szUrl, pszPath);
                if (RT_FAILURE(rc))
                {
                    RTFileDelete(pszPath);
                    rtDbgCfgLog1(pThis, "%Rrc on URL '%s'\n", rc, szUrl);
                }
                if (   rc == VERR_HTTP_NOT_FOUND
                    && (fFlags & RTDBGCFG_O_SYMSRV))
                {
                    /* Try the compressed version of the file. */
                    pszPath[strlen(pszPath) - 1] = '_';
                    szUrl[strlen(szUrl)     - 1] = '_';
                    rtDbgCfgLog2(pThis, "Downloading '%s' to '%s'...\n", szUrl, pszPath);
                    rc = RTHttpGetFile(hHttp, szUrl, pszPath);
                    if (RT_SUCCESS(rc))
                        rc = rtDbgCfgUnpackMsCacheFile(pThis, pszPath, pszFilename);
                    else
                    {
                        rtDbgCfgLog1(pThis, "%Rrc on URL '%s'\n", rc, pszPath);
                        RTFileDelete(pszPath);
                    }
                }
            }

            RTHttpDestroy(hHttp);
        }
#else
        rc = VWRN_NOT_FOUND;
#endif
    }
    /* No download, assume dir on server share. */
    else
    {
        if (RTStrIStartsWith(pszServer, "file:///"))
            pszServer += 4 + 1 + 3 - 1;

        /* Compose the path to the uncompressed file on the server. */
        rc = RTPathJoin(szUrl, sizeof(szUrl), pszServer, pszFilename);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szUrl, sizeof(szUrl), pszCacheSubDir);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szUrl, sizeof(szUrl), pszFilename);
        if (RT_SUCCESS(rc))
        {
            rtDbgCfgLog2(pThis, "Copying '%s' to '%s'...\n", szUrl, pszPath);
            rc = RTFileCopy(szUrl, pszPath);
            if (RT_FAILURE(rc))
            {
                RTFileDelete(pszPath);
                rtDbgCfgLog1(pThis, "%Rrc on '%s'\n", rc, szUrl);

                /* Try the compressed version. */
                pszPath[strlen(pszPath) - 1] = '_';
                szUrl[strlen(szUrl)     - 1] = '_';
                rtDbgCfgLog2(pThis, "Copying '%s' to '%s'...\n", szUrl, pszPath);
                rc = RTFileCopy(szUrl, pszPath);
                if (RT_SUCCESS(rc))
                    rc = rtDbgCfgUnpackMsCacheFile(pThis, pszPath, pszFilename);
                else
                {
                    rtDbgCfgLog1(pThis, "%Rrc on '%s'\n", rc, pszPath);
                    RTFileDelete(pszPath);
                }
            }
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Succeeded in downloading it. Add UUID mapping?
         */
        if (pszUuidMappingSubDir)
        {
            /** @todo UUID mapping when downloading. */
        }

        /*
         * Give the file a try.
         */
        Assert(RTFileExists(pszPath));
        rtDbgCfgLog1(pThis, "Trying '%s'...\n", pszPath);
        rc = pfnCallback(pThis, pszPath, pvUser1, pvUser2);
        if (rc == VINF_CALLBACK_RETURN)
            rtDbgCfgLog1(pThis, "Found '%s'.\n", pszPath);
        else if (rc == VERR_CALLBACK_RETURN)
            rtDbgCfgLog1(pThis, "Error opening '%s'.\n", pszPath);
        else
            rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc, pszPath);
    }

    return rc;
}


static int rtDbgCfgCopyFileToCache(PRTDBGCFGINT pThis, char const *pszSrc, const char *pchCache, size_t cchCache,
                                   const char *pszCacheSubDir, const char *pszUuidMappingSubDir, PRTPATHSPLIT pSplitFn)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(pszSrc); RT_NOREF_PV(pchCache); RT_NOREF_PV(cchCache);
    RT_NOREF_PV(pszUuidMappingSubDir); RT_NOREF_PV(pSplitFn);

    if (!pszCacheSubDir || !*pszCacheSubDir)
        return VINF_SUCCESS;

    /** @todo copy to cache */
    return VINF_SUCCESS;
}


static int rtDbgCfgTryOpenCache(PRTDBGCFGINT pThis, char *pszPath, size_t cchCachePath,
                                const char *pszCacheSubDir, const char *pszUuidMappingSubDir,
                                PCRTPATHSPLIT pSplitFn, const char *pszCacheSuffix, uint32_t fFlags,
                                PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    Assert(pszPath[cchCachePath] == '\0');

    /*
     * If the cache doesn't exist, fail right away.
     */
    if (!pszCacheSubDir || !*pszCacheSubDir)
        return VWRN_NOT_FOUND;
    if (!RTDirExists(pszPath))
    {
        rtDbgCfgLog2(pThis, "Cache does not exist: '%s'\n", pszPath);
        return VWRN_NOT_FOUND;
    }

    /*
     * If we got a UUID mapping option, try it first as we can hopefully
     * dispense with case folding.
     */
    if (pszUuidMappingSubDir)
    {
        int rc = RTPathAppend(pszPath, RTPATH_MAX, pszUuidMappingSubDir);
        if (   RT_SUCCESS(rc)
            && RTFileExists(pszPath))
        {
            /* Try resolve the path before presenting it to the client, a
               12 digit filename is of little worth. */
            char szBackup[RTPATH_MAX];
            strcpy(szBackup, pszPath);
            rc = RTPathAbs(szBackup, pszPath, RTPATH_MAX);
            if (RT_FAILURE(rc))
                strcpy(pszPath, szBackup);

            /* Do the callback thing. */
            rtDbgCfgLog1(pThis, "Trying '%s'...\n", pszPath);
            int rc2 = pfnCallback(pThis, pszPath, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Found '%s' via uuid mapping.\n", pszPath);
            else if (rc2 == VERR_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Error opening '%s'.\n", pszPath);
            else
                rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, pszPath);
            if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                return rc2;

            /* Failed, restore the cache path. */
            memcpy(pszPath, szBackup, cchCachePath);
        }
        pszPath[cchCachePath] = '\0';
    }

    /*
     * Carefully construct the cache path with case insensitivity in mind.
     */
    bool const fCaseInsensitive = (fFlags & RTDBGCFG_O_CASE_INSENSITIVE)
                               && !rtDbgCfgIsFsCaseInsensitive(pszPath);
    const char *pszFilename = pSplitFn->apszComps[pSplitFn->cComps - 1];

    if (!rtDbgCfgIsDirAndFixCase(pszPath, pszFilename, fCaseInsensitive))
        return VWRN_NOT_FOUND;

    if (!rtDbgCfgIsDirAndFixCase(pszPath, pszCacheSubDir, fCaseInsensitive))
        return VWRN_NOT_FOUND;

    bool fProbablyCompressed = false;
    if (!rtDbgCfgIsFileAndFixCase(pszPath, pszFilename, pszCacheSuffix, fCaseInsensitive,
                                  RT_BOOL(fFlags & RTDBGCFG_O_MAYBE_COMPRESSED_MS), &fProbablyCompressed))
        return VWRN_NOT_FOUND;
    if (fProbablyCompressed)
    {
        int rc = rtDbgCfgUnpackMsCacheFile(pThis, pszPath, pszFilename);
        if (RT_FAILURE(rc))
            return VWRN_NOT_FOUND;
    }

    rtDbgCfgLog1(pThis, "Trying '%s'...\n", pszPath);
    int rc2 = pfnCallback(pThis, pszPath, pvUser1, pvUser2);
    if (rc2 == VINF_CALLBACK_RETURN)
        rtDbgCfgLog1(pThis, "Found '%s'.\n", pszPath);
    else if (rc2 == VERR_CALLBACK_RETURN)
        rtDbgCfgLog1(pThis, "Error opening '%s'.\n", pszPath);
    else
        rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, pszPath);
    return rc2;
}


static int rtDbgCfgTryOpenList(PRTDBGCFGINT pThis, PRTLISTANCHOR pList, PRTPATHSPLIT pSplitFn, const char *pszCacheSubDir,
                               const char *pszUuidMappingSubDir, uint32_t fFlags, char *pszPath,
                               PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    int rcRet = VWRN_NOT_FOUND;
    int rc2 = VINF_SUCCESS;

    const char *pchCache = NULL;
    size_t      cchCache = 0;
    int         rcCache  = VWRN_NOT_FOUND;

    PRTDBGCFGSTR pCur;
    RTListForEach(pList, pCur, RTDBGCFGSTR, ListEntry)
    {
        size_t      cchDir = pCur->cch;
        const char *pszDir = pCur->sz;
        rtDbgCfgLog2(pThis, "Path list entry: '%s'\n", pszDir);

        /* This is very simplistic, but we have a unreasonably large path
           buffer, so it'll work just fine and simplify things greatly below. */
        if (cchDir >= RTPATH_MAX - 8U)
        {
            if (RT_SUCCESS_NP(rcRet))
                rcRet = VERR_FILENAME_TOO_LONG;
            continue;
        }

        /*
         * Process the path according to it's type.
         */
        if (!RTStrNICmp(pszDir, RT_STR_TUPLE("srv*")))
        {
            /*
             * Symbol server.
             */
            pszDir += sizeof("srv*") - 1;
            cchDir -= sizeof("srv*") - 1;
            bool        fSearchCache = false;
            const char *pszServer = (const char *)memchr(pszDir, '*', cchDir);
            if (!pszServer)
                pszServer = pszDir;
            else if (pszServer == pszDir)
                continue;
            else
            {
                fSearchCache = true;
                pchCache = pszDir;
                cchCache = pszServer - pszDir;
                pszServer++;
            }

            /* We don't have any default cache directory, so skip if the cache is missing. */
            if (cchCache == 0)
                continue;

            /* Search the cache first (if we haven't already done so). */
            if (fSearchCache)
            {
                memcpy(pszPath, pchCache, cchCache);
                pszPath[cchCache] = '\0';
                RTPathChangeToUnixSlashes(pszPath, false);

                rcCache = rc2 = rtDbgCfgTryOpenCache(pThis, pszPath, cchCache, pszCacheSubDir, pszUuidMappingSubDir,
                                                     pSplitFn, NULL /*pszCacheSuffix*/, fFlags, pfnCallback, pvUser1, pvUser2);
                if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                    return rc2;
            }

            /* Try downloading the file. */
            if (rcCache == VWRN_NOT_FOUND)
            {
                memcpy(pszPath, pchCache, cchCache);
                pszPath[cchCache] = '\0';
                RTPathChangeToUnixSlashes(pszPath, false);

                rc2 = rtDbgCfgTryDownloadAndOpen(pThis, pszServer, pszPath, pszCacheSubDir, pszUuidMappingSubDir,
                                                 pSplitFn, NULL /*pszCacheSuffix*/, fFlags, pfnCallback, pvUser1, pvUser2);
                if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                    return rc2;
            }
        }
        else if (!RTStrNICmp(pszDir, RT_STR_TUPLE("cache*")))
        {
            /*
             * Cache directory.
             */
            pszDir += sizeof("cache*") - 1;
            cchDir -= sizeof("cache*") - 1;
            if (!cchDir)
                continue;
            pchCache = pszDir;
            cchCache = cchDir;

            memcpy(pszPath, pchCache, cchCache);
            pszPath[cchCache] = '\0';
            RTPathChangeToUnixSlashes(pszPath, false);

            rcCache = rc2 = rtDbgCfgTryOpenCache(pThis, pszPath, cchCache, pszCacheSubDir, pszUuidMappingSubDir,
                                                 pSplitFn, NULL /*pszCacheSuffix*/, fFlags, pfnCallback, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                return rc2;
        }
        else
        {
            /*
             * Normal directory. Check for our own 'rec*' and 'norec*' prefix
             * flags governing recursive searching.
             */
            uint32_t fFlagsDir = fFlags;
            if (!RTStrNICmp(pszDir, RT_STR_TUPLE("rec*")))
            {
                pszDir += sizeof("rec*") - 1;
                cchDir -= sizeof("rec*") - 1;
                fFlagsDir |= RTDBGCFG_O_RECURSIVE;
            }
            else if (!RTStrNICmp(pszDir, RT_STR_TUPLE("norec*")))
            {
                pszDir += sizeof("norec*") - 1;
                cchDir -= sizeof("norec*") - 1;
                fFlagsDir &= ~RTDBGCFG_O_RECURSIVE;
            }

            /* Copy the path into the buffer and do the searching. */
            memcpy(pszPath, pszDir, cchDir);
            pszPath[cchDir] = '\0';
            RTPathChangeToUnixSlashes(pszPath, false);

            rc2 = rtDbgCfgTryOpenDir(pThis, pszPath, pSplitFn, fFlagsDir, pfnCallback, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
            {
                if (   rc2 == VINF_CALLBACK_RETURN
                    && cchCache > 0)
                    rtDbgCfgCopyFileToCache(pThis, pszPath, pchCache, cchCache,
                                            pszCacheSubDir, pszUuidMappingSubDir, pSplitFn);
                return rc2;
            }
        }

        /* Propagate errors. */
        if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
            rcRet = rc2;
    }

    return rcRet;
}


/**
 * Common worker routine for Image and debug info opening.
 *
 * This will not search using for suffixes.
 *
 * @returns IPRT status code.
 * @param   hDbgCfg                 The debugging configuration handle.
 *                                  NIL_RTDBGCFG is accepted, but the result is
 *                                  that no paths will be searched beyond the
 *                                  given and the current directory.
 * @param   pszFilename             The filename to search for.  This may or may
 *                                  not include a full or partial path.
 * @param   pszCacheSubDir          The cache subdirectory to look in.
 * @param   pszUuidMappingSubDir    UUID mapping subdirectory to check, NULL if
 *                                  no mapping wanted.
 * @param   fFlags                  Flags and hints.
 * @param   pfnCallback             The open callback routine.
 * @param   pvUser1                 User parameter 1.
 * @param   pvUser2                 User parameter 2.
 */
static int rtDbgCfgOpenWithSubDir(RTDBGCFG hDbgCfg, const char *pszFilename, const char *pszCacheSubDir,
                                  const char *pszUuidMappingSubDir, uint32_t fFlags,
                                  PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    int rcRet = VINF_SUCCESS;
    int rc2;

    /*
     * Do a little validating first.
     */
    PRTDBGCFGINT pThis = hDbgCfg;
    if (pThis != NIL_RTDBGCFG)
        RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    else
        pThis = NULL;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(pszCacheSubDir, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTDBGCFG_O_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Do some guessing as to the way we should parse the filename and whether
     * it's case exact or not.
     */
    bool fDosPath = RT_OPSYS_USES_DOS_PATHS(fFlags & RTDBGCFG_O_OPSYS_MASK)
                 || (fFlags & RTDBGCFG_O_CASE_INSENSITIVE)
                 || strchr(pszFilename, ':')  != NULL
                 || strchr(pszFilename, '\\') != NULL;
    if (fDosPath)
        fFlags |= RTDBGCFG_O_CASE_INSENSITIVE;

    rtDbgCfgLog2(pThis, "Looking for '%s' w/ cache subdir '%s' and %#x flags...\n", pszFilename, pszCacheSubDir, fFlags);

    PRTPATHSPLIT pSplitFn;
    rc2 = RTPathSplitA(pszFilename, &pSplitFn, fDosPath ? RTPATH_STR_F_STYLE_DOS : RTPATH_STR_F_STYLE_UNIX);
    if (RT_FAILURE(rc2))
        return rc2;
    AssertReturnStmt(pSplitFn->fProps & RTPATH_PROP_FILENAME, RTPathSplitFree(pSplitFn), VERR_IS_A_DIRECTORY);

    /*
     * Try the stored file name first if it has a kind of absolute path.
     */
    char szPath[RTPATH_MAX];
    if (RTPATH_PROP_HAS_ROOT_SPEC(pSplitFn->fProps))
    {
        rc2 = RTPathSplitReassemble(pSplitFn, RTPATH_STR_F_STYLE_HOST, szPath, sizeof(szPath));
        if (RT_SUCCESS(rc2) && RTFileExists(szPath))
        {
            RTPathChangeToUnixSlashes(szPath, false);
            rtDbgCfgLog1(pThis, "Trying '%s'...\n", szPath);
            rc2 = pfnCallback(pThis, szPath, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Found '%s'.\n", szPath);
            else if (rc2 == VERR_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Error opening '%s'.\n", szPath);
            else
                rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, szPath);
        }
    }
    if (   rc2 != VINF_CALLBACK_RETURN
        && rc2 != VERR_CALLBACK_RETURN)
    {
        /*
         * Try the current directory (will take cover relative paths
         * skipped above).
         */
        rc2 = RTPathGetCurrent(szPath, sizeof(szPath));
        if (RT_FAILURE(rc2))
            strcpy(szPath, ".");
        RTPathChangeToUnixSlashes(szPath, false);

        rc2 = rtDbgCfgTryOpenDir(pThis, szPath, pSplitFn, fFlags, pfnCallback, pvUser1, pvUser2);
        if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
            rcRet = rc2;

        if (   rc2 != VINF_CALLBACK_RETURN
            && rc2 != VERR_CALLBACK_RETURN
            && pThis)
        {
            rc2 = RTCritSectRwEnterShared(&pThis->CritSect);
            if (RT_SUCCESS(rc2))
            {
                /*
                 * Run the applicable lists.
                 */
                rc2 = rtDbgCfgTryOpenList(pThis, &pThis->PathList, pSplitFn, pszCacheSubDir,
                                          pszUuidMappingSubDir, fFlags, szPath, pfnCallback, pvUser1, pvUser2);
                if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                    rcRet = rc2;

#ifdef RT_OS_WINDOWS
                if (   rc2 != VINF_CALLBACK_RETURN
                    && rc2 != VERR_CALLBACK_RETURN
                    && (fFlags & RTDBGCFG_O_EXECUTABLE_IMAGE)
                    && !(fFlags & RTDBGCFG_O_NO_SYSTEM_PATHS)
                    && !(pThis->fFlags & RTDBGCFG_FLAGS_NO_SYSTEM_PATHS) )
                {
                    rc2 = rtDbgCfgTryOpenList(pThis, &pThis->NtExecutablePathList, pSplitFn, pszCacheSubDir,
                                              pszUuidMappingSubDir, fFlags, szPath, pfnCallback, pvUser1, pvUser2);
                    if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                        rcRet = rc2;
                }

                if (   rc2 != VINF_CALLBACK_RETURN
                    && rc2 != VERR_CALLBACK_RETURN
                    && !(fFlags & RTDBGCFG_O_NO_SYSTEM_PATHS)
                    && !(pThis->fFlags & RTDBGCFG_FLAGS_NO_SYSTEM_PATHS) )
                {
                    rc2 = rtDbgCfgTryOpenList(pThis, &pThis->NtSymbolPathList, pSplitFn, pszCacheSubDir,
                                              pszUuidMappingSubDir, fFlags, szPath, pfnCallback, pvUser1, pvUser2);
                    if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                        rcRet = rc2;
                }
#endif
                RTCritSectRwLeaveShared(&pThis->CritSect);
            }
            else if (RT_SUCCESS(rcRet))
                rcRet = rc2;
        }
    }

    RTPathSplitFree(pSplitFn);
    if (   rc2 == VINF_CALLBACK_RETURN
        || rc2 == VERR_CALLBACK_RETURN)
        rcRet = rc2;
    else if (RT_SUCCESS(rcRet))
        rcRet = VERR_NOT_FOUND;
    return rcRet;
}


RTDECL(int) RTDbgCfgOpenEx(RTDBGCFG hDbgCfg, const char *pszFilename, const char *pszCacheSubDir,
                           const char *pszUuidMappingSubDir, uint32_t fFlags,
                           PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, pszCacheSubDir, pszUuidMappingSubDir, fFlags,
                                  pfnCallback, pvUser1, pvUser2);
}




RTDECL(int) RTDbgCfgOpenPeImage(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp,
                                PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%08X%x", uTimestamp, cbImage);
    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, szSubDir, NULL,
                                  RT_OPSYS_WINDOWS /* approx */ | RTDBGCFG_O_SYMSRV | RTDBGCFG_O_CASE_INSENSITIVE
                                  | RTDBGCFG_O_MAYBE_COMPRESSED_MS | RTDBGCFG_O_EXECUTABLE_IMAGE,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenPdb70(RTDBGCFG hDbgCfg, const char *pszFilename, PCRTUUID pUuid, uint32_t uAge,
                              PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    char szSubDir[64];
    if (!pUuid)
        szSubDir[0] = '\0';
    else
    {
        /* Stringify the UUID and remove the dashes. */
        int rc2 = RTUuidToStr(pUuid, szSubDir, sizeof(szSubDir));
        AssertRCReturn(rc2, rc2);

        char *pszSrc = szSubDir;
        char *pszDst = szSubDir;
        char ch;
        while ((ch = *pszSrc++))
            if (ch != '-')
                *pszDst++ = RT_C_TO_UPPER(ch);

        RTStrPrintf(pszDst, &szSubDir[sizeof(szSubDir)] - pszDst, "%X", uAge);
    }

    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, szSubDir, NULL,
                                  RT_OPSYS_WINDOWS /* approx */ | RTDBGCFG_O_SYMSRV | RTDBGCFG_O_CASE_INSENSITIVE
                                  | RTDBGCFG_O_MAYBE_COMPRESSED_MS | RTDBGCFG_O_EXT_DEBUG_FILE,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenPdb20(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp, uint32_t uAge,
                              PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    RT_NOREF_PV(cbImage);
    /** @todo test this! */
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%08X%x", uTimestamp, uAge);
    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, szSubDir, NULL,
                                  RT_OPSYS_WINDOWS /* approx */ | RTDBGCFG_O_SYMSRV | RTDBGCFG_O_CASE_INSENSITIVE
                                  | RTDBGCFG_O_MAYBE_COMPRESSED_MS | RTDBGCFG_O_EXT_DEBUG_FILE,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenDbg(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t cbImage, uint32_t uTimestamp,
                            PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%08X%x", uTimestamp, cbImage);
    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, szSubDir, NULL,
                                  RT_OPSYS_WINDOWS /* approx */ | RTDBGCFG_O_SYMSRV | RTDBGCFG_O_CASE_INSENSITIVE
                                  | RTDBGCFG_O_MAYBE_COMPRESSED_MS | RTDBGCFG_O_EXT_DEBUG_FILE,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenDwo(RTDBGCFG hDbgCfg, const char *pszFilename, uint32_t uCrc32,
                            PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%08x", uCrc32);
    return rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, szSubDir, NULL,
                                  RT_OPSYS_UNKNOWN | RTDBGCFG_O_EXT_DEBUG_FILE,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenDwoBuildId(RTDBGCFG hDbgCfg, const char *pszFilename, const uint8_t *pbBuildId,
                                   size_t cbBuildId, PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    char *pszSubDir = NULL;
    int rc = RTStrAPrintf(&pszSubDir, "%#.*Rhxs", cbBuildId, pbBuildId);
    if (RT_SUCCESS(rc))
    {
        rc = rtDbgCfgOpenWithSubDir(hDbgCfg, pszFilename, pszSubDir, NULL,
                                    RTDBGCFG_O_DEBUGINFOD | RT_OPSYS_UNKNOWN | RTDBGCFG_O_EXT_DEBUG_FILE,
                                    pfnCallback, pvUser1, pvUser2);
        RTStrFree(pszSubDir);
    }

    return rc;
}



/*
 *
 *  D a r w i n   . d S Y M   b u n d l e s
 *  D a r w i n   . d S Y M   b u n d l e s
 *  D a r w i n   . d S Y M   b u n d l e s
 *
 */

/**
 * Very similar to rtDbgCfgTryOpenDir.
 */
static int rtDbgCfgTryOpenDsymBundleInDir(PRTDBGCFGINT pThis, char *pszPath, PRTPATHSPLIT pSplitFn,
                                          const char * const *papszSuffixes, uint32_t fFlags,
                                          PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    int rcRet = VWRN_NOT_FOUND;
    int rc2;

    /* If the directory doesn't exist, just quit immediately.
       Note! Our case insensitivity doesn't extend to the search dirs themselfs,
             only to the bits under neath them. */
    if (!RTDirExists(pszPath))
    {
        rtDbgCfgLog2(pThis, "Dir does not exist: '%s'\n", pszPath);
        return rcRet;
    }

    /* Figure out whether we have to do a case sensitive search or not.
       Note! As a simplification, we don't ask for case settings in each
             directory under the user specified path, we assume the file
             systems that mounted there have compatible settings. Faster
             that way. */
    bool const fCaseInsensitive = (fFlags & RTDBGCFG_O_CASE_INSENSITIVE)
                               && !rtDbgCfgIsFsCaseInsensitive(pszPath);

    size_t const cchPath = strlen(pszPath);

    /*
     * Look for the file with less and less of the original path given.
     * Also try out typical bundle extension variations.
     */
    const char *pszName = pSplitFn->apszComps[pSplitFn->cComps - 1];
    for (unsigned i = RTPATH_PROP_HAS_ROOT_SPEC(pSplitFn->fProps); i < pSplitFn->cComps; i++)
    {
        pszPath[cchPath] = '\0';

        rc2 = VINF_SUCCESS;
        for (unsigned j = i; j < pSplitFn->cComps - 1U && RT_SUCCESS(rc2); j++)
            if (!rtDbgCfgIsDirAndFixCase(pszPath, pSplitFn->apszComps[i], fCaseInsensitive))
                rc2 = VERR_FILE_NOT_FOUND;
        if (RT_SUCCESS(rc2))
        {
            for (uint32_t iSuffix = 0; papszSuffixes[iSuffix]; iSuffix++)
            {
                if (   !rtDbgCfgIsDirAndFixCase2(pszPath, pszName, papszSuffixes[iSuffix], fCaseInsensitive)
                    && !rtDbgCfgIsDirAndFixCase(pszPath, "Contents", fCaseInsensitive)
                    && !rtDbgCfgIsDirAndFixCase(pszPath, "Resources", fCaseInsensitive)
                    && !rtDbgCfgIsDirAndFixCase(pszPath, "DWARF", fCaseInsensitive))
                {
                    if (rtDbgCfgIsFileAndFixCase(pszPath, pszName, NULL /*pszSuffix*/, fCaseInsensitive, false, NULL))
                    {
                        rtDbgCfgLog1(pThis, "Trying '%s'...\n", pszPath);
                        rc2 = pfnCallback(pThis, pszPath, pvUser1, pvUser2);
                        if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                        {
                            if (rc2 == VINF_CALLBACK_RETURN)
                                rtDbgCfgLog1(pThis, "Found '%s'.\n", pszPath);
                            else
                                rtDbgCfgLog1(pThis, "Error opening '%s'.\n", pszPath);
                            return rc2;
                        }
                        rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, pszPath);
                        if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                            rcRet = rc2;
                    }
                }
            }
        }
        rc2 = VERR_FILE_NOT_FOUND;
    }

    /*
     * Do a recursive search if requested.
     */
    if (   (fFlags & RTDBGCFG_O_RECURSIVE)
        && !(pThis->fFlags & RTDBGCFG_FLAGS_NO_RECURSIV_SEARCH) )
    {
        /** @todo Recursive searching will be done later. */
    }

    return rcRet;
}


/**
 * Very similar to rtDbgCfgTryOpenList.
 */
static int rtDbgCfgTryOpenBundleInList(PRTDBGCFGINT pThis, PRTLISTANCHOR pList, PRTPATHSPLIT pSplitFn,
                                       const char * const *papszSuffixes, const char *pszCacheSubDir,
                                       const char *pszCacheSuffix, const char *pszUuidMappingSubDir,
                                       uint32_t fFlags, char *pszPath,
                                       PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    int rcRet = VWRN_NOT_FOUND;
    int rc2;

    const char *pchCache = NULL;
    size_t      cchCache = 0;
    int         rcCache  = VWRN_NOT_FOUND;

    PRTDBGCFGSTR pCur;
    RTListForEach(pList, pCur, RTDBGCFGSTR, ListEntry)
    {
        size_t      cchDir = pCur->cch;
        const char *pszDir = pCur->sz;
        rtDbgCfgLog2(pThis, "Path list entry: '%s'\n", pszDir);

        /* This is very simplistic, but we have a unreasonably large path
           buffer, so it'll work just fine and simplify things greatly below. */
        if (cchDir >= RTPATH_MAX - 8U)
        {
            if (RT_SUCCESS_NP(rcRet))
                rcRet = VERR_FILENAME_TOO_LONG;
            continue;
        }

        /*
         * Process the path according to it's type.
         */
        rc2 = VINF_SUCCESS;
        if (!RTStrNICmp(pszDir, RT_STR_TUPLE("srv*")))
        {
            /*
             * Symbol server.
             */
            pszDir += sizeof("srv*") - 1;
            cchDir -= sizeof("srv*") - 1;
            bool        fSearchCache = false;
            const char *pszServer = (const char *)memchr(pszDir, '*', cchDir);
            if (!pszServer)
                pszServer = pszDir;
            else if (pszServer == pszDir)
                continue;
            else
            {
                fSearchCache = true;
                pchCache = pszDir;
                cchCache = pszServer - pszDir;
                pszServer++;
            }

            /* We don't have any default cache directory, so skip if the cache is missing. */
            if (cchCache == 0)
                continue;

            /* Search the cache first (if we haven't already done so). */
            if (fSearchCache)
            {
                memcpy(pszPath, pchCache, cchCache);
                pszPath[cchCache] = '\0';
                RTPathChangeToUnixSlashes(pszPath, false);

                rcCache = rc2 = rtDbgCfgTryOpenCache(pThis, pszPath, cchCache, pszCacheSubDir, pszUuidMappingSubDir,
                                                     pSplitFn, pszCacheSuffix, fFlags, pfnCallback, pvUser1, pvUser2);
                if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                    return rc2;
            }

            /* Try downloading the file. */
            if (rcCache == VWRN_NOT_FOUND)
            {
                memcpy(pszPath, pchCache, cchCache);
                pszPath[cchCache] = '\0';
                RTPathChangeToUnixSlashes(pszPath, false);

                rc2 = rtDbgCfgTryDownloadAndOpen(pThis, pszServer, pszPath, pszCacheSubDir, pszUuidMappingSubDir,
                                                 pSplitFn, pszCacheSuffix, fFlags, pfnCallback, pvUser1, pvUser2);
                if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                    return rc2;
            }
        }
        else if (!RTStrNICmp(pszDir, RT_STR_TUPLE("cache*")))
        {
            /*
             * Cache directory.
             */
            pszDir += sizeof("cache*") - 1;
            cchDir -= sizeof("cache*") - 1;
            if (!cchDir)
                continue;
            pchCache = pszDir;
            cchCache = cchDir;

            memcpy(pszPath, pchCache, cchCache);
            pszPath[cchCache] = '\0';
            RTPathChangeToUnixSlashes(pszPath, false);

            rcCache = rc2 = rtDbgCfgTryOpenCache(pThis, pszPath, cchCache, pszCacheSubDir, pszUuidMappingSubDir,
                                                 pSplitFn, pszCacheSuffix, fFlags, pfnCallback, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
                return rc2;
        }
        else
        {
            /*
             * Normal directory. Check for our own 'rec*' and 'norec*' prefix
             * flags governing recursive searching.
             */
            uint32_t fFlagsDir = fFlags;
            if (!RTStrNICmp(pszDir, RT_STR_TUPLE("rec*")))
            {
                pszDir += sizeof("rec*") - 1;
                cchDir -= sizeof("rec*") - 1;
                fFlagsDir |= RTDBGCFG_O_RECURSIVE;
            }
            else if (!RTStrNICmp(pszDir, RT_STR_TUPLE("norec*")))
            {
                pszDir += sizeof("norec*") - 1;
                cchDir -= sizeof("norec*") - 1;
                fFlagsDir &= ~RTDBGCFG_O_RECURSIVE;
            }

            /* Copy the path into the buffer and do the searching. */
            memcpy(pszPath, pszDir, cchDir);
            pszPath[cchDir] = '\0';
            RTPathChangeToUnixSlashes(pszPath, false);

            rc2 = rtDbgCfgTryOpenDsymBundleInDir(pThis, pszPath, pSplitFn, papszSuffixes, fFlagsDir,
                                                 pfnCallback, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN || rc2 == VERR_CALLBACK_RETURN)
            {
                if (   rc2 == VINF_CALLBACK_RETURN
                    && cchCache > 0)
                    rtDbgCfgCopyFileToCache(pThis, pszPath, pchCache, cchCache,
                                            pszCacheSubDir, pszUuidMappingSubDir, pSplitFn);
                return rc2;
            }
        }

        /* Propagate errors. */
        if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
            rcRet = rc2;
    }

    return rcRet;
}


/**
 * Creating a UUID mapping subdirectory path for use in caches.
 *
 * @returns IPRT status code.
 * @param   pszSubDir           The output buffer.
 * @param   cbSubDir            The size of the output buffer. (Top dir length +
 *                              slash + UUID string len + extra dash.)
 * @param   pszTopDir           The top level cache directory name. No slashes
 *                              or other directory separators, please.
 * @param   pUuid               The UUID.
 */
static int rtDbgCfgConstructUuidMappingSubDir(char *pszSubDir, size_t cbSubDir, const char *pszTopDir, PCRTUUID pUuid)
{
    Assert(!strpbrk(pszTopDir, ":/\\"));

    size_t cchTopDir = strlen(pszTopDir);
    if (cchTopDir + 1 + 1 + RTUUID_STR_LENGTH + 1 > cbSubDir)
        return VERR_BUFFER_OVERFLOW;
    memcpy(pszSubDir, pszTopDir, cchTopDir);

    pszSubDir += cchTopDir;
    *pszSubDir++ = RTPATH_SLASH;
    cbSubDir  -= cchTopDir + 1;

    /* ed5a8336-35c2-4892-9122-21d5572924a3 -> ED5A/8336/35C2/4892/9122/21D5572924A3 */
    int rc = RTUuidToStr(pUuid, pszSubDir + 1, cbSubDir - 1); AssertRCReturn(rc, rc);
    RTStrToUpper(pszSubDir + 1);
    memmove(pszSubDir, pszSubDir + 1, 4);
    pszSubDir += 4;
    *pszSubDir = RTPATH_SLASH;
    pszSubDir += 5;
    *pszSubDir = RTPATH_SLASH;
    pszSubDir += 5;
    *pszSubDir = RTPATH_SLASH;
    pszSubDir += 5;
    *pszSubDir = RTPATH_SLASH;
    pszSubDir += 5;
    *pszSubDir = RTPATH_SLASH;

    return VINF_SUCCESS;
}


static int rtDbgCfgOpenBundleFile(RTDBGCFG hDbgCfg, const char *pszImage, const char * const *papszSuffixes,
                                  const char *pszBundleSubDir, PCRTUUID pUuid, const char *pszUuidMapDirName,
                                  const char *pszCacheSuffix, bool fOpenImage,
                                  PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    /*
     * Bundles are directories, means we can forget about sharing code much
     * with the other RTDbgCfgOpenXXX methods.  Thus we're duplicating a lot of
     * code from rtDbgCfgOpenWithSubDir with .dSYM/.kext/.dylib/.app/.* related
     * adjustments, so, a bug found here or there probably means the other
     * version needs updating.
     */
    int rcRet = VINF_SUCCESS;
    int rc2;

    /*
     * Do a little validating first.
     */
    PRTDBGCFGINT pThis = hDbgCfg;
    if (pThis != NIL_RTDBGCFG)
        RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    else
        pThis = NULL;
    AssertPtrReturn(pszImage, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    /*
     * Set up rtDbgCfgOpenWithSubDir and uuid map parameters.
     */
    uint32_t fFlags = RTDBGCFG_O_EXT_DEBUG_FILE | RT_OPSYS_DARWIN;
    const char *pszCacheSubDir = NULL;
    char szCacheSubDir[RTUUID_STR_LENGTH];
    const char *pszUuidMappingSubDir = NULL;
    char szUuidMappingSubDir[RTUUID_STR_LENGTH + 16];
    if (pUuid)
    {
        /* Since Mac debuggers uses UUID mappings, we just the slashing default
           UUID string representation instead of stripping dashes like for PDB. */
        RTUuidToStr(pUuid, szCacheSubDir, sizeof(szCacheSubDir));
        pszCacheSubDir = szCacheSubDir;

        rc2 = rtDbgCfgConstructUuidMappingSubDir(szUuidMappingSubDir, sizeof(szUuidMappingSubDir), pszUuidMapDirName, pUuid);
        AssertRCReturn(rc2, rc2);
        pszUuidMappingSubDir = szUuidMappingSubDir;
    }

    /*
     * Do some guessing as to the way we should parse the filename and whether
     * it's case exact or not.
     */
    bool fDosPath = strchr(pszImage, ':')  != NULL
                 || strchr(pszImage, '\\') != NULL
                 || RT_OPSYS_USES_DOS_PATHS(fFlags & RTDBGCFG_O_OPSYS_MASK)
                 || (fFlags & RTDBGCFG_O_CASE_INSENSITIVE);
    if (fDosPath)
        fFlags |= RTDBGCFG_O_CASE_INSENSITIVE;

    rtDbgCfgLog2(pThis, "Looking for '%s' with %#x flags...\n", pszImage, fFlags);

    PRTPATHSPLIT pSplitFn;
    rc2 = RTPathSplitA(pszImage, &pSplitFn, fDosPath ? RTPATH_STR_F_STYLE_DOS : RTPATH_STR_F_STYLE_UNIX);
    if (RT_FAILURE(rc2))
        return rc2;
    AssertReturnStmt(pSplitFn->fProps & RTPATH_PROP_FILENAME, RTPathSplitFree(pSplitFn), VERR_IS_A_DIRECTORY);

    /*
     * Try the image directory first.
     */
    char szPath[RTPATH_MAX];
    if (pSplitFn->cComps > 0)
    {
        rc2 = RTPathSplitReassemble(pSplitFn, RTPATH_STR_F_STYLE_HOST, szPath, sizeof(szPath));
        if (fOpenImage && RT_SUCCESS(rc2))
        {
            rc2 = RTStrCat(szPath, sizeof(szPath), papszSuffixes[0]);
            if (RT_SUCCESS(rc2))
                rc2 = RTStrCat(szPath, sizeof(szPath), pszBundleSubDir);
            if (RT_SUCCESS(rc2))
                rc2 = RTPathAppend(szPath, sizeof(szPath), pSplitFn->apszComps[pSplitFn->cComps - 1]);
        }
        if (RT_SUCCESS(rc2) && RTPathExists(szPath))
        {
            RTPathChangeToUnixSlashes(szPath, false);
            rtDbgCfgLog1(pThis, "Trying '%s'...\n", szPath);
            rc2 = pfnCallback(pThis, szPath, pvUser1, pvUser2);
            if (rc2 == VINF_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Found '%s'.\n", szPath);
            else if (rc2 == VERR_CALLBACK_RETURN)
                rtDbgCfgLog1(pThis, "Error opening '%s'.\n", szPath);
            else
                rtDbgCfgLog1(pThis, "Error %Rrc opening '%s'.\n", rc2, szPath);
        }
    }
    if (   rc2 != VINF_CALLBACK_RETURN
        && rc2 != VERR_CALLBACK_RETURN)
    {
        /*
         * Try the current directory (will take cover relative paths
         * skipped above).
         */
        rc2 = RTPathGetCurrent(szPath, sizeof(szPath));
        if (RT_FAILURE(rc2))
            strcpy(szPath, ".");
        RTPathChangeToUnixSlashes(szPath, false);

        rc2 = rtDbgCfgTryOpenDsymBundleInDir(pThis, szPath, pSplitFn, g_apszDSymBundleSuffixes,
                                             fFlags, pfnCallback, pvUser1, pvUser2);
        if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
            rcRet = rc2;

        if (   rc2 != VINF_CALLBACK_RETURN
            && rc2 != VERR_CALLBACK_RETURN
            && pThis)
        {
            rc2 = RTCritSectRwEnterShared(&pThis->CritSect);
            if (RT_SUCCESS(rc2))
            {
                /*
                 * Run the applicable lists.
                 */
                rc2 = rtDbgCfgTryOpenBundleInList(pThis, &pThis->PathList, pSplitFn, g_apszDSymBundleSuffixes,
                                                  pszCacheSubDir, pszCacheSuffix,
                                                  pszUuidMappingSubDir, fFlags, szPath,
                                                  pfnCallback, pvUser1, pvUser2);
                if (RT_FAILURE(rc2) && RT_SUCCESS_NP(rcRet))
                    rcRet = rc2;

                RTCritSectRwLeaveShared(&pThis->CritSect);
            }
            else if (RT_SUCCESS(rcRet))
                rcRet = rc2;
        }
    }

    RTPathSplitFree(pSplitFn);
    if (   rc2 == VINF_CALLBACK_RETURN
        || rc2 == VERR_CALLBACK_RETURN)
        rcRet = rc2;
    else if (RT_SUCCESS(rcRet))
        rcRet = VERR_NOT_FOUND;
    return rcRet;
}


RTDECL(int) RTDbgCfgOpenDsymBundle(RTDBGCFG hDbgCfg, const char *pszImage, PCRTUUID pUuid,
                                   PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    return rtDbgCfgOpenBundleFile(hDbgCfg, pszImage, g_apszDSymBundleSuffixes,
                                  "Contents" RTPATH_SLASH_STR "Resources" RTPATH_SLASH_STR "DWARF",
                                  pUuid, RTDBG_CACHE_UUID_MAP_DIR_DSYMS, RTDBG_CACHE_DSYM_FILE_SUFFIX, false /* fOpenImage */,
                                  pfnCallback, pvUser1, pvUser2);
}


RTDECL(int) RTDbgCfgOpenMachOImage(RTDBGCFG hDbgCfg, const char *pszImage, PCRTUUID pUuid,
                                   PFNRTDBGCFGOPEN pfnCallback, void *pvUser1, void *pvUser2)
{
    return rtDbgCfgOpenBundleFile(hDbgCfg, pszImage, g_apszBundleSuffixes,
                                  "Contents" RTPATH_SLASH_STR "MacOS",
                                  pUuid, RTDBG_CACHE_UUID_MAP_DIR_IMAGES, NULL /*pszCacheSuffix*/, true /* fOpenImage */,
                                  pfnCallback, pvUser1, pvUser2);
}



RTDECL(int) RTDbgCfgSetLogCallback(RTDBGCFG hDbgCfg, PFNRTDBGCFGLOG pfnCallback, void *pvUser)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pfnCallback, VERR_INVALID_POINTER);

    int rc = RTCritSectRwEnterExcl(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (   pThis->pfnLogCallback == NULL
            || pfnCallback == NULL
            || pfnCallback == pThis->pfnLogCallback)
        {
            pThis->pfnLogCallback = NULL;
            pThis->pvLogUser      = NULL;
            ASMCompilerBarrier(); /* paranoia */
            pThis->pvLogUser      = pvUser;
            pThis->pfnLogCallback = pfnCallback;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_ACCESS_DENIED;
        RTCritSectRwLeaveExcl(&pThis->CritSect);
    }

    return rc;
}


/**
 * Frees a string list.
 *
 * @param   pList               The list to free.
 */
static void rtDbgCfgFreeStrList(PRTLISTANCHOR pList)
{
    PRTDBGCFGSTR pCur;
    PRTDBGCFGSTR pNext;
    RTListForEachSafe(pList, pCur, pNext, RTDBGCFGSTR, ListEntry)
    {
        RTListNodeRemove(&pCur->ListEntry);
        RTMemFree(pCur);
    }
}


/**
 * Make changes to a string list, given a semicolon separated input string.
 *
 * @returns VINF_SUCCESS, VERR_FILENAME_TOO_LONG, VERR_NO_MEMORY
 * @param   pThis               The config instance.
 * @param   enmOp               The change operation.
 * @param   pszValue            The input strings separated by semicolon.
 * @param   fPaths              Indicates that this is a path list and that we
 *                              should look for srv and cache prefixes.
 * @param   pList               The string list anchor.
 */
static int rtDbgCfgChangeStringList(PRTDBGCFGINT pThis, RTDBGCFGOP enmOp, const char *pszValue, bool fPaths,
                                    PRTLISTANCHOR pList)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(fPaths);

    if (enmOp == RTDBGCFGOP_SET)
        rtDbgCfgFreeStrList(pList);

    PRTLISTNODE pPrependTo = pList;
    while (*pszValue)
    {
        /* Skip separators. */
        while (*pszValue == ';')
            pszValue++;
        if (!*pszValue)
            break;

        /* Find the end of this path. */
        const char *pchPath = pszValue++;
        char ch;
        while ((ch = *pszValue) && ch != ';')
            pszValue++;
        size_t cchPath = pszValue - pchPath;
        if (cchPath >= UINT16_MAX)
            return VERR_FILENAME_TOO_LONG;

        if (enmOp == RTDBGCFGOP_REMOVE)
        {
            /*
             * Remove all occurences.
             */
            PRTDBGCFGSTR pCur;
            PRTDBGCFGSTR pNext;
            RTListForEachSafe(pList, pCur, pNext, RTDBGCFGSTR, ListEntry)
            {
                if (   pCur->cch == cchPath
                    && !memcmp(pCur->sz, pchPath, cchPath))
                {
                    RTListNodeRemove(&pCur->ListEntry);
                    RTMemFree(pCur);
                }
            }
        }
        else
        {
            /*
             * We're adding a new one.
             */
            PRTDBGCFGSTR pNew = (PRTDBGCFGSTR)RTMemAlloc(RT_UOFFSETOF_DYN(RTDBGCFGSTR, sz[cchPath + 1]));
            if (!pNew)
                return VERR_NO_MEMORY;
            pNew->cch = (uint16_t)cchPath;
            pNew->fFlags = 0;
            memcpy(pNew->sz, pchPath, cchPath);
            pNew->sz[cchPath] = '\0';

            if (enmOp == RTDBGCFGOP_PREPEND)
            {
                RTListNodeInsertAfter(pPrependTo, &pNew->ListEntry);
                pPrependTo = &pNew->ListEntry;
            }
            else
                RTListAppend(pList, &pNew->ListEntry);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Make changes to a 64-bit value
 *
 * @returns VINF_SUCCESS, VERR_DBG_CFG_INVALID_VALUE.
 * @param   pThis               The config instance.
 * @param   enmOp               The change operation.
 * @param   pszValue            The input value.
 * @param   paMnemonics         The mnemonics map for this value.
 * @param   puValue             The value to change.
 */
static int rtDbgCfgChangeStringU64(PRTDBGCFGINT pThis, RTDBGCFGOP enmOp, const char *pszValue,
                                   PCRTDBGCFGU64MNEMONIC paMnemonics, uint64_t *puValue)
{
    RT_NOREF_PV(pThis);

    uint64_t    uNew = enmOp == RTDBGCFGOP_SET ? 0 : *puValue;
    char        ch;
    while ((ch = *pszValue))
    {
        /* skip whitespace and separators */
        while (RT_C_IS_SPACE(ch) || RT_C_IS_CNTRL(ch) || ch == ';' || ch == ':')
            ch = *++pszValue;
        if (!ch)
            break;

        if (RT_C_IS_DIGIT(ch))
        {
            uint64_t uTmp;
            int rc = RTStrToUInt64Ex(pszValue, (char **)&pszValue, 0, &uTmp);
            if (RT_FAILURE(rc) || rc == VWRN_NUMBER_TOO_BIG)
                return VERR_DBG_CFG_INVALID_VALUE;

            if (enmOp != RTDBGCFGOP_REMOVE)
                uNew |= uTmp;
            else
                uNew &= ~uTmp;
        }
        else
        {
            /* A mnemonic, find the end of it. */
            const char *pszMnemonic = pszValue - 1;
            do
                ch = *++pszValue;
            while (ch && !RT_C_IS_SPACE(ch) && !RT_C_IS_CNTRL(ch) && ch != ';' && ch != ':');
            size_t cchMnemonic = pszValue - pszMnemonic;

            /* Look it up in the map and apply it. */
            unsigned i = 0;
            while (paMnemonics[i].pszMnemonic)
            {
                if (   cchMnemonic == paMnemonics[i].cchMnemonic
                    && !memcmp(pszMnemonic, paMnemonics[i].pszMnemonic, cchMnemonic))
                {
                    if (paMnemonics[i].fSet ? enmOp != RTDBGCFGOP_REMOVE : enmOp == RTDBGCFGOP_REMOVE)
                        uNew |= paMnemonics[i].fFlags;
                    else
                        uNew &= ~paMnemonics[i].fFlags;
                   break;
                }
                i++;
            }

            if (!paMnemonics[i].pszMnemonic)
                return VERR_DBG_CFG_INVALID_VALUE;
        }
    }

    *puValue = uNew;
    return VINF_SUCCESS;
}


RTDECL(int) RTDbgCfgChangeString(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, RTDBGCFGOP enmOp, const char *pszValue)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmProp > RTDBGCFGPROP_INVALID && enmProp < RTDBGCFGPROP_END, VERR_INVALID_PARAMETER);
    AssertReturn(enmOp   > RTDBGCFGOP_INVALID   && enmOp   < RTDBGCFGOP_END,   VERR_INVALID_PARAMETER);
    if (!pszValue)
        pszValue = "";
    else
        AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    int rc = RTCritSectRwEnterExcl(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        switch (enmProp)
        {
            case RTDBGCFGPROP_FLAGS:
                rc = rtDbgCfgChangeStringU64(pThis, enmOp, pszValue, g_aDbgCfgFlags, &pThis->fFlags);
                break;
            case RTDBGCFGPROP_PATH:
                rc = rtDbgCfgChangeStringList(pThis, enmOp, pszValue, true, &pThis->PathList);
                break;
            case RTDBGCFGPROP_SUFFIXES:
                rc = rtDbgCfgChangeStringList(pThis, enmOp, pszValue, false, &pThis->SuffixList);
                break;
            case RTDBGCFGPROP_SRC_PATH:
                rc = rtDbgCfgChangeStringList(pThis, enmOp, pszValue, true, &pThis->SrcPathList);
                break;
            default:
                AssertFailed();
                rc = VERR_INTERNAL_ERROR_3;
        }

        RTCritSectRwLeaveExcl(&pThis->CritSect);
    }

    return rc;
}


RTDECL(int) RTDbgCfgChangeUInt(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, RTDBGCFGOP enmOp, uint64_t uValue)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmProp > RTDBGCFGPROP_INVALID && enmProp < RTDBGCFGPROP_END, VERR_INVALID_PARAMETER);
    AssertReturn(enmOp   > RTDBGCFGOP_INVALID   && enmOp   < RTDBGCFGOP_END,   VERR_INVALID_PARAMETER);

    int rc = RTCritSectRwEnterExcl(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        uint64_t *puValue = NULL;
        switch (enmProp)
        {
            case RTDBGCFGPROP_FLAGS:
                puValue = &pThis->fFlags;
                break;
            default:
                rc = VERR_DBG_CFG_NOT_UINT_PROP;
        }
        if (RT_SUCCESS(rc))
        {
            switch (enmOp)
            {
                case RTDBGCFGOP_SET:
                    *puValue = uValue;
                    break;
                case RTDBGCFGOP_APPEND:
                case RTDBGCFGOP_PREPEND:
                    *puValue |= uValue;
                    break;
                case RTDBGCFGOP_REMOVE:
                    *puValue &= ~uValue;
                    break;
                default:
                    AssertFailed();
                    rc = VERR_INTERNAL_ERROR_2;
            }
        }

        RTCritSectRwLeaveExcl(&pThis->CritSect);
    }

    return rc;
}


/**
 * Querys a string list as a single string (semicolon separators).
 *
 * @returns VINF_SUCCESS, VERR_BUFFER_OVERFLOW.
 * @param   hDbgCfg             The config instance handle.
 * @param   pList               The string list anchor.
 * @param   pszValue            The output buffer.
 * @param   cbValue             The size of the output buffer.
 */
static int rtDbgCfgQueryStringList(RTDBGCFG hDbgCfg, PRTLISTANCHOR pList,
                                   char *pszValue, size_t cbValue)
{
    RT_NOREF_PV(hDbgCfg);

    /*
     * Check the length first.
     */
    size_t cbReq = 1;
    PRTDBGCFGSTR pCur;
    RTListForEach(pList, pCur, RTDBGCFGSTR, ListEntry)
        cbReq += pCur->cch + 1;
    if (cbReq > cbValue)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Construct the string list in the buffer.
     */
    char *psz = pszValue;
    RTListForEach(pList, pCur, RTDBGCFGSTR, ListEntry)
    {
        if (psz != pszValue)
            *psz++ = ';';
        memcpy(psz, pCur->sz, pCur->cch);
        psz += pCur->cch;
    }
    *psz = '\0';

    return VINF_SUCCESS;
}


/**
 * Querys the string value of a 64-bit unsigned int.
 *
 * @returns VINF_SUCCESS, VERR_BUFFER_OVERFLOW.
 * @param   hDbgCfg             The config instance handle.
 * @param   uValue              The value to query.
 * @param   paMnemonics         The mnemonics map for this value.
 * @param   pszValue            The output buffer.
 * @param   cbValue             The size of the output buffer.
 */
static int rtDbgCfgQueryStringU64(RTDBGCFG hDbgCfg, uint64_t uValue, PCRTDBGCFGU64MNEMONIC paMnemonics,
                                  char *pszValue, size_t cbValue)
{
    RT_NOREF_PV(hDbgCfg);

    /*
     * If no mnemonics, just return the hex value.
     */
    if (!paMnemonics || paMnemonics[0].pszMnemonic)
    {
        char szTmp[64];
        size_t cch = RTStrPrintf(szTmp, sizeof(szTmp), "%#x", uValue);
        if (cch + 1 > cbValue)
            return VERR_BUFFER_OVERFLOW;
        memcpy(pszValue, szTmp, cbValue);
        return VINF_SUCCESS;
    }

    /*
     * Check that there is sufficient buffer space first.
     */
    size_t cbReq = 1;
    for (unsigned i = 0; paMnemonics[i].pszMnemonic; i++)
        if (  paMnemonics[i].fSet
            ? (paMnemonics[i].fFlags & uValue)
            : !(paMnemonics[i].fFlags & uValue))
            cbReq += (cbReq != 1) + paMnemonics[i].cchMnemonic;
    if (cbReq > cbValue)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Construct the string.
     */
    char *psz = pszValue;
    for (unsigned i = 0; paMnemonics[i].pszMnemonic; i++)
        if (  paMnemonics[i].fSet
            ? (paMnemonics[i].fFlags & uValue)
            : !(paMnemonics[i].fFlags & uValue))
        {
            if (psz != pszValue)
                *psz++ = ' ';
            memcpy(psz, paMnemonics[i].pszMnemonic, paMnemonics[i].cchMnemonic);
            psz += paMnemonics[i].cchMnemonic;
        }
    *psz = '\0';
    return VINF_SUCCESS;
}


RTDECL(int) RTDbgCfgQueryString(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, char *pszValue, size_t cbValue)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmProp > RTDBGCFGPROP_INVALID && enmProp < RTDBGCFGPROP_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    int rc = RTCritSectRwEnterShared(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        switch (enmProp)
        {
            case RTDBGCFGPROP_FLAGS:
                rc = rtDbgCfgQueryStringU64(pThis, pThis->fFlags, g_aDbgCfgFlags, pszValue, cbValue);
                break;
            case RTDBGCFGPROP_PATH:
                rc = rtDbgCfgQueryStringList(pThis, &pThis->PathList, pszValue, cbValue);
                break;
            case RTDBGCFGPROP_SUFFIXES:
                rc = rtDbgCfgQueryStringList(pThis, &pThis->SuffixList, pszValue, cbValue);
                break;
            case RTDBGCFGPROP_SRC_PATH:
                rc = rtDbgCfgQueryStringList(pThis, &pThis->SrcPathList, pszValue, cbValue);
                break;
            default:
                AssertFailed();
                rc = VERR_INTERNAL_ERROR_3;
        }

        RTCritSectRwLeaveShared(&pThis->CritSect);
    }

    return rc;
}


RTDECL(int) RTDbgCfgQueryUInt(RTDBGCFG hDbgCfg, RTDBGCFGPROP enmProp, uint64_t *puValue)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmProp > RTDBGCFGPROP_INVALID && enmProp < RTDBGCFGPROP_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puValue, VERR_INVALID_POINTER);

    int rc = RTCritSectRwEnterShared(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        switch (enmProp)
        {
            case RTDBGCFGPROP_FLAGS:
                *puValue = pThis->fFlags;
                break;
            default:
                rc = VERR_DBG_CFG_NOT_UINT_PROP;
        }

        RTCritSectRwLeaveShared(&pThis->CritSect);
    }

    return rc;
}

RTDECL(uint32_t) RTDbgCfgRetain(RTDBGCFG hDbgCfg)
{
    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    return cRefs;
}


RTDECL(uint32_t) RTDbgCfgRelease(RTDBGCFG hDbgCfg)
{
    if (hDbgCfg == NIL_RTDBGCFG)
        return 0;

    PRTDBGCFGINT pThis = hDbgCfg;
    RTDBGCFG_VALID_RETURN_RC(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (!cRefs)
    {
        /*
         * Last reference - free all memory.
         */
        ASMAtomicWriteU32(&pThis->u32Magic, ~RTDBGCFG_MAGIC);
        rtDbgCfgFreeStrList(&pThis->PathList);
        rtDbgCfgFreeStrList(&pThis->SuffixList);
        rtDbgCfgFreeStrList(&pThis->SrcPathList);
#ifdef RT_OS_WINDOWS
        rtDbgCfgFreeStrList(&pThis->NtSymbolPathList);
        rtDbgCfgFreeStrList(&pThis->NtExecutablePathList);
        rtDbgCfgFreeStrList(&pThis->NtSourcePath);
#endif
        RTCritSectRwDelete(&pThis->CritSect);
        RTMemFree(pThis);
    }
    else
        Assert(cRefs < UINT32_MAX / 2);
    return cRefs;
}


RTDECL(int) RTDbgCfgCreate(PRTDBGCFG phDbgCfg, const char *pszEnvVarPrefix, bool fNativePaths)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phDbgCfg, VERR_INVALID_POINTER);
    if (pszEnvVarPrefix)
    {
        AssertPtrReturn(pszEnvVarPrefix, VERR_INVALID_POINTER);
        AssertReturn(*pszEnvVarPrefix, VERR_INVALID_PARAMETER);
    }

    /*
     * Allocate and initialize a new instance.
     */
    PRTDBGCFGINT pThis = (PRTDBGCFGINT)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic   = RTDBGCFG_MAGIC;
    pThis->cRefs      = 1;
    RTListInit(&pThis->PathList);
    RTListInit(&pThis->SuffixList);
    RTListInit(&pThis->SrcPathList);
#ifdef RT_OS_WINDOWS
    RTListInit(&pThis->NtSymbolPathList);
    RTListInit(&pThis->NtExecutablePathList);
    RTListInit(&pThis->NtSourcePath);
#endif

    int rc = RTCritSectRwInit(&pThis->CritSect);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    /*
     * Read configurtion from the environment if requested to do so.
     */
    if (pszEnvVarPrefix || fNativePaths)
    {
        const size_t cbEnvVar = 256;
        const size_t cbEnvVal = 65536 - cbEnvVar;
        char        *pszEnvVar = (char *)RTMemTmpAlloc(cbEnvVar + cbEnvVal);
        if (pszEnvVar)
        {
            char *pszEnvVal = pszEnvVar + cbEnvVar;

            if (pszEnvVarPrefix)
            {
                static struct
                {
                    RTDBGCFGPROP    enmProp;
                    const char     *pszVar;
                } const s_aProps[] =
                {
                    { RTDBGCFGPROP_FLAGS,       "FLAGS"    },
                    { RTDBGCFGPROP_PATH,        "PATH"     },
                    { RTDBGCFGPROP_SUFFIXES,    "SUFFIXES" },
                    { RTDBGCFGPROP_SRC_PATH,    "SRC_PATH" },
                };

                for (unsigned i = 0; i < RT_ELEMENTS(s_aProps); i++)
                {
                    size_t cchEnvVar = RTStrPrintf(pszEnvVar, cbEnvVar, "%s_%s", pszEnvVarPrefix, s_aProps[i].pszVar);
                    if (cchEnvVar >= cbEnvVar - 1)
                    {
                        rc = VERR_BUFFER_OVERFLOW;
                        break;
                    }

                    rc = RTEnvGetEx(RTENV_DEFAULT, pszEnvVar, pszEnvVal, cbEnvVal, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTDbgCfgChangeString(pThis, s_aProps[i].enmProp, RTDBGCFGOP_SET, pszEnvVal);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    else if (rc != VERR_ENV_VAR_NOT_FOUND)
                        break;
                    else
                        rc = VINF_SUCCESS;
                }
            }

            /*
             * Pick up system specific search paths.
             */
            if (RT_SUCCESS(rc) && fNativePaths)
            {
                struct
                {
                    PRTLISTANCHOR   pList;
                    const char     *pszVar;
                    char            chSep;
                } aNativePaths[] =
                {
#ifdef RT_OS_WINDOWS
                    { &pThis->NtExecutablePathList, "_NT_EXECUTABLE_PATH",  ';' },
                    { &pThis->NtSymbolPathList,     "_NT_ALT_SYMBOL_PATH",  ';' },
                    { &pThis->NtSymbolPathList,     "_NT_SYMBOL_PATH",      ';' },
                    { &pThis->NtSourcePath,         "_NT_SOURCE_PATH",      ';' },
#endif
                    { NULL, NULL, 0 }
                };
                for (unsigned i = 0; aNativePaths[i].pList; i++)
                {
                    Assert(aNativePaths[i].chSep == ';'); /* fix when needed */
                    rc = RTEnvGetEx(RTENV_DEFAULT, aNativePaths[i].pszVar, pszEnvVal, cbEnvVal, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtDbgCfgChangeStringList(pThis, RTDBGCFGOP_APPEND, pszEnvVal, true, aNativePaths[i].pList);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    else if (rc != VERR_ENV_VAR_NOT_FOUND)
                        break;
                    else
                        rc = VINF_SUCCESS;
                }
            }
            RTMemTmpFree(pszEnvVar);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
        if (RT_FAILURE(rc))
        {
            /*
             * Error, bail out.
             */
            RTDbgCfgRelease(pThis);
            return rc;
        }
    }

    /*
     * Returns successfully.
     */
    *phDbgCfg = pThis;

    return VINF_SUCCESS;
}

