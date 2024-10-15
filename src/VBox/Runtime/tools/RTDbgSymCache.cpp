/* $Id: RTDbgSymCache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Symbol Cache Utility.
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
#include <iprt/zip.h>

#include <iprt/buildconfig.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/fsvfs.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>
#include <iprt/formats/mach-o.h>
#include <iprt/formats/pecoff.h>
#include <iprt/formats/pdb.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Cache file type.
 */
typedef enum RTDBGSYMCACHEFILETYPE
{
    RTDBGSYMCACHEFILETYPE_INVALID,
    RTDBGSYMCACHEFILETYPE_DIR,
    RTDBGSYMCACHEFILETYPE_DIR_FILTER,
    RTDBGSYMCACHEFILETYPE_DEBUG_FILE,
    RTDBGSYMCACHEFILETYPE_IMAGE_FILE,
    RTDBGSYMCACHEFILETYPE_DEBUG_BUNDLE,
    RTDBGSYMCACHEFILETYPE_IMAGE_BUNDLE,
    RTDBGSYMCACHEFILETYPE_IGNORE
} RTDBGSYMCACHEFILETYPE;


/**
 * Configuration for the 'add' command.
 */
typedef struct RTDBGSYMCACHEADDCFG
{
    bool        fRecursive;
    bool        fOverwriteOnConflict;
    const char *pszFilter;
    const char *pszCache;
} RTDBGSYMCACHEADDCFG;
/** Pointer to a read only 'add' config. */
typedef RTDBGSYMCACHEADDCFG const *PCRTDBGSYMCACHEADDCFG;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Bundle suffixes. */
static const char * const g_apszBundleSuffixes[] =
{
    ".kext",
    ".app",
    ".framework",  /** @todo framework is different. */
    ".component",
    ".action",
    ".caction",
    ".bundle",
    ".sourcebundle",
    ".plugin",
    ".ppp",
    ".menu",
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
    ".dSYM",
    NULL
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtDbgSymCacheAddDirWorker(char *pszPath, size_t cchPath, PRTDIRENTRYEX pDirEntry, PCRTDBGSYMCACHEADDCFG pCfg);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbositity level. */
static uint32_t g_iLogLevel = 99;


/**
 * Display the version of the cache program.
 *
 * @returns exit code.
 */
static RTEXITCODE rtDbgSymCacheVersion(void)
{
    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
    return RTEXITCODE_SUCCESS;
}


/**
 * Shows the usage of the cache program.
 *
 * @returns Exit code.
 * @param   pszArg0             Program name.
 * @param   pszCommand          Command selector, NULL if all.
 */
static RTEXITCODE rtDbgSymCacheUsage(const char *pszArg0, const char *pszCommand)
{
    if (!pszCommand || !strcmp(pszCommand, "add"))
        RTPrintf("Usage: %s add [-Rno] <cache-root-dir> <file1[=cache-name]> [fileN..]\n"
                 "\n"
                 "Options:\n"
                 "  -R, --recursive\n"
                 "      Process directory arguments recursively.\n"
                 "  -n, --no-recursive\n"
                 "      No recursion. (default)\n"
                 "  -o, --overwrite-on-conflict\n"
                 "      Overwrite existing cache entry.\n"
                 , RTPathFilename(pszArg0));


    if (!pszCommand || !strcmp(pszCommand, "get"))
        RTPrintf("Usage: %s get <query-options> <cache-options> [--output|-o <path>]\n"
                 "\n"
                 "Query Options:\n"
                 "  --for-exe[cutable] <path>\n"
                 "      Get debug file for the given executable.\n"
                 "  --dwo, --dwarf, --dwarf-external\n"
                 "      Get external DWARF debug file.  Needs --name and --dwo-crc32.\n"
                 "  --dsym\n"
                 "      Get DWARF debug file from .dSYM bundle.  Needs --uuid or --name.\n"
                 "  --dbg\n"
                 "      Get NT DBG debug file.  Needs --name, --timestamp and --size.\n"
                 "  --pdb20\n"
                 "      Get PDB 2.0 debug file.  Needs --name, --timestamp, --size\n"
                 "      and --pdb-age (if non-zero).\n"
                 "  --pdb70\n"
                 "      Get PDB 7.0 debug file.  Needs --name, --uuid, and --pdb-age\n"
                 "      (if non-zero).\n"
                 "  --macho\n"
                 "      Get Mach-O image file.  Needs --uuid or --name.\n"
                 "  --pe\n"
                 "      Get PE image file.  Needs --name, --timestamp and --size.\n"
                 "  --timestamp, --ts, -t <timestamp>\n"
                 "      The timestamp (32-bit) for the file to get.  Used with --dbg, --pdb20\n"
                 "      and --pe.\n"
                 "  --uuid, -u, <uuid>\n"
                 "      The UUID for the file to get.  Used with  --dsym, --pdb70 and --macho\n"
                 "  --image-size, --size, -z <size>\n"
                 "      The image size (32-bit) for the file to get.  Used with --dbg,\n"
                 "      --pdb20, --pdb70 and --pe.\n"
                 "  --pdb-age, -a <age>\n"
                 "      The PDB age (32-bit) for the file to get.  Used with --pdb20 and --pdb70.\n"
                 "  --dwo-crc32, -c <crc32>\n"
                 "      The CRC32 for the file to get.  Used with --dwo.\n"
                 "  --name, -n <name>\n"
                 "      The name (in the cache) of the file to get.\n"
                 "\n"
                 "Debug Cache Options:\n"
                 "  --sym-path, -s <path>\n"
                 "      Adds the path to the debug configuration, NT style with 'srv*' and\n"
                 "      'cache*' prefixes as well as our own 'rec*' and 'norec*' recursion\n"
                 "      prefixes.\n"
                 "  --env-prefix, -p <prefix>\n"
                 "      The enviornment variable prefix, default is 'IPRT_' making the\n"
                 "      symbol path variable 'IPRT_PATH'.\n"
                 "  --use-native-paths (default), --no-native-paths\n"
                 "      Pick up native symbol paths from the environment.\n"
                 "\n"
                 "Output Options:\n"
                 "  --output, -o <path>\n"
                 "      The output filename or directory.  Directories must end with a\n"
                 "      path separator.  The default filename that in the cache.\n"
                 "\n"
                 "This is handy for triggering downloading of symbol files from a server.  Say\n"
                 "you have the executable but want the corrsponding PDB or .dSYM file:\n"
                 "    %s get --for-executable VBoxRT.dll\n"
                 "    %s get --for-executable VBoxRT.dylib\n"
                 "    "
                 , RTPathFilename(pszArg0), RTPathFilename(pszArg0), RTPathFilename(pszArg0));

    return RTEXITCODE_SUCCESS;
}


/**
 * @callback_method_impl{FNRTDBGCFGLOG}
 */
static DECLCALLBACK(void) rtDbgSymCacheLogCallback(RTDBGCFG hDbgCfg, uint32_t iLevel, const char *pszMsg, void *pvUser)
{
    RT_NOREF(hDbgCfg, pvUser);
    if (iLevel <= g_iLogLevel)
    {
        size_t cchMsg = strlen(pszMsg);
        if (cchMsg > 0 && pszMsg[cchMsg - 1] == '\n')
            RTMsgInfo("[%u] %s", iLevel, pszMsg);
        else if (cchMsg > 0)
            RTMsgInfo("[%u] %s\n", iLevel, pszMsg);
    }
}


/**
 * Creates a UUID mapping for the file.
 *
 * @returns IPRT status code.
 * @param   pszCacheFile    The path to the file in the cache.
 * @param   pFileUuid       The UUID of the file.
 * @param   pszUuidMapDir   The UUID map subdirectory in the cache, if this is
 *                          wanted, otherwise NULL.
 * @param   pCfg            The configuration.
 */
static int rtDbgSymCacheAddCreateUuidMapping(const char *pszCacheFile, PRTUUID pFileUuid,
                                             const char *pszUuidMapDir, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Create the UUID map entry first, deep.
     */
    char szMapPath[RTPATH_MAX];
    int rc = RTPathJoin(szMapPath, sizeof(szMapPath) - sizeof("/xxxx/yyyy/xxxx/yyyy/xxxx/zzzzzzzzzzzz") + 1,
                        pCfg->pszCache, pszUuidMapDir);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error constructing UUID map path (RTPathJoin): %Rrc", rc);

    size_t cch = strlen(szMapPath);
    szMapPath[cch] = '-';

    rc = RTUuidToStr(pFileUuid, &szMapPath[cch + 2], sizeof(szMapPath) - cch);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error constructing UUID map path (RTUuidToStr): %Rrc", rc);

    /* Uppercase the whole lot. */
    RTStrToUpper(&szMapPath[cch + 2]);

    /* Split the first dword in two. */
    szMapPath[cch + 1] = szMapPath[cch + 2];
    szMapPath[cch + 2] = szMapPath[cch + 3];
    szMapPath[cch + 3] = szMapPath[cch + 4];
    szMapPath[cch + 4] = szMapPath[cch + 5];
    szMapPath[cch + 5] = '-';

    /*
     * Create the directories in the path.
     */
    for (unsigned i = 0; i < 6; i++, cch += 5)
    {
        Assert(szMapPath[cch] == '-');
        szMapPath[cch] = '\0';
        if (!RTDirExists(szMapPath))
        {
            rc = RTDirCreate(szMapPath, 0755, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(rc, "RTDirCreate failed on '%s' (UUID map path): %Rrc", szMapPath, rc);
        }
        szMapPath[cch] = RTPATH_SLASH;
    }
    cch -= 5;

    /*
     * Calculate a relative path from there to the actual file.
     */
    char szLinkTarget[RTPATH_MAX];
    szMapPath[cch] = '\0';
    rc = RTPathCalcRelative(szLinkTarget, sizeof(szLinkTarget), szMapPath, false /*fFromFile*/, pszCacheFile);
    szMapPath[cch] = RTPATH_SLASH;
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Failed to calculate relative path from '%s' to '%s': %Rrc", szMapPath, pszCacheFile, rc);

    /*
     * If there is already a link there, check if it matches or whether
     * perhaps it's target doesn't exist.
     */
    RTFSOBJINFO ObjInfo;
    rc = RTPathQueryInfoEx(szMapPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
        {
            rc = RTPathQueryInfoEx(szMapPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
            if (RT_SUCCESS(rc))
            {
                char *pszCurTarget = NULL;
                rc = RTSymlinkReadA(szMapPath, &pszCurTarget);
                if (RT_FAILURE(rc))
                    return RTMsgErrorRc(rc, "UUID map: failed to read existing symlink '%s': %Rrc", szMapPath, rc);
                if (RTPathCompare(pszCurTarget, szLinkTarget) == 0)
                    RTMsgInfo("UUID map: existing link '%s' has the same target ('%s').", szMapPath, pszCurTarget);
                else
                {
                    RTMsgError("UUID map: Existing mapping '%s' pointing to '%s' insted of '%s'",
                               szMapPath, pszCurTarget, szLinkTarget);
                    rc = VERR_ALREADY_EXISTS;
                }
                RTStrFree(pszCurTarget);
                return rc;
            }
            else
                RTMsgInfo("UUID map: replacing dangling link '%s'", szMapPath);
            RTSymlinkDelete(szMapPath, 0 /*fFlags*/);
        }
        else if (RTFS_IS_FILE(ObjInfo.Attr.fMode))
            return RTMsgErrorRc(VERR_IS_A_FILE,
                                "UUID map: found file at '%s', expect symbolic link or nothing.", szMapPath);
        else if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            return RTMsgErrorRc(VERR_IS_A_DIRECTORY,
                                "UUID map: found directory at '%s', expect symbolic link or nothing.", szMapPath);
        else
            return RTMsgErrorRc(VERR_NOT_SYMLINK,
                                "UUID map: Expected symbolic link or nothing at '%s', found: fMode=%#x",
                                szMapPath, ObjInfo.Attr.fMode);
    }

    /*
     * Create the symbolic link.
     */
    rc = RTSymlinkCreate(szMapPath, szLinkTarget, RTSYMLINKTYPE_FILE, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Failed to create UUID map symlink '%s' to '%s': %Rrc", szMapPath, szLinkTarget, rc);
    RTMsgInfo("UUID map: %s  =>  %s", szMapPath, szLinkTarget);
    return VINF_SUCCESS;
}


/**
 * Adds a file to the cache.
 *
 * @returns IPRT status code.
 * @param   pszSrcPath      Path to the source file.
 * @param   pszDstName      The name of the destionation file (no path stuff).
 * @param   pszExtraSuff    Optional extra suffix. Mach-O dSYM hack.
 * @param   pszDstSubDir    The subdirectory to file it under.  This is the
 *                          stringification of a relatively unique identifier of
 *                          the file in question.
 * @param   pAddToUuidMap   Optional file UUID that is used to create a UUID map
 *                          entry.
 * @param   pszUuidMapDir   The UUID map subdirectory in the cache, if this is
 *                          wanted, otherwise NULL.
 * @param   pCfg            The configuration.
 */
static int rtDbgSymCacheAddOneFile(const char *pszSrcPath, const char *pszDstName, const char *pszExtraSuff,
                                   const char *pszDstSubDir, PRTUUID pAddToUuidMap, const char *pszUuidMapDir,
                                   PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Build and create the destination path, step by step.
     */
    char szDstPath[RTPATH_MAX];
    int rc = RTPathJoin(szDstPath, sizeof(szDstPath), pCfg->pszCache, pszDstName);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error constructing cache path for '%s': %Rrc", pszSrcPath, rc);

    if (!RTDirExists(szDstPath))
    {
        rc = RTDirCreate(szDstPath, 0755, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "Error creating '%s': %Rrc", szDstPath, rc);
    }

    rc = RTPathAppend(szDstPath, sizeof(szDstPath), pszDstSubDir);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error constructing cache path for '%s': %Rrc", pszSrcPath, rc);

    if (!RTDirExists(szDstPath))
    {
        rc = RTDirCreate(szDstPath, 0755, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "Error creating '%s': %Rrc", szDstPath, rc);
    }

    rc = RTPathAppend(szDstPath, sizeof(szDstPath), pszDstName);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error constructing cache path for '%s': %Rrc", pszSrcPath, rc);
    if (pszExtraSuff)
    {
        rc = RTStrCat(szDstPath, sizeof(szDstPath), pszExtraSuff);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "Error constructing cache path for '%s': %Rrc", pszSrcPath, rc);
    }

    /*
     * If the file exists, we compare the two and throws an error if the doesn't match.
     */
    if (RTPathExists(szDstPath))
    {
        rc = RTFileCompare(pszSrcPath, szDstPath);
        if (RT_SUCCESS(rc))
        {
            RTMsgInfo("%s is already in the cache.", pszSrcPath);
            if (pAddToUuidMap && pszUuidMapDir)
                return rtDbgSymCacheAddCreateUuidMapping(szDstPath, pAddToUuidMap, pszUuidMapDir, pCfg);
            return VINF_SUCCESS;
        }
        if (rc == VERR_NOT_EQUAL)
            RTMsgInfo("Cache conflict with existing entry '%s' when inserting '%s'.", szDstPath, pszSrcPath);
        else
            RTMsgInfo("Error comparing '%s' with '%s': %Rrc", pszSrcPath, szDstPath, rc);
        if (!pCfg->fOverwriteOnConflict)
            return rc;
    }

    /*
     * The file doesn't exist or we should overwrite it,
     */
    RTMsgInfo("Copying '%s' to '%s'...", pszSrcPath, szDstPath);
    rc = RTFileCopy(pszSrcPath, szDstPath);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error copying '%s' to '%s': %Rrc", pszSrcPath, szDstPath, rc);
    if (pAddToUuidMap && pszUuidMapDir)
        return rtDbgSymCacheAddCreateUuidMapping(szDstPath, pAddToUuidMap, pszUuidMapDir, pCfg);
    return VINF_SUCCESS;
}


/**
 * Worker that add the image file to the right place.
 *
 * @returns IPRT status code.
 * @param   pszPath             Path to the image file.
 * @param   pszDstName          Add to the cache under this name.  Typically the
 *                              filename part of @a pszPath.
 * @param   pCfg                Configuration data.
 * @param   hLdrMod             Image handle.
 * @param   pszExtraSuff        Optional extra suffix.  Mach-O dSYM hack.
 * @param   pszUuidMapDir       Optional UUID map cache directory if the image
 *                              should be mapped by UUID.
 *                              The map is a Mac OS X debug feature supported by
 *                              the two native debuggers gdb and lldb.  Look for
 *                              descriptions of DBGFileMappedPaths in the
 *                              com.apple.DebugSymbols in the user defaults.
 */
static int rtDbgSymCacheAddImageFileWorker(const char *pszPath, const char *pszDstName, PCRTDBGSYMCACHEADDCFG pCfg,
                                           RTLDRMOD hLdrMod, const char *pszExtraSuff, const char *pszUuidMapDir)
{
    /*
     * Determine which subdirectory to put the files in.
     */
    RTUUID      Uuid;
    PRTUUID     pUuid = NULL;
    int         rc;
    char        szSubDir[48];
    RTLDRFMT    enmFmt = RTLdrGetFormat(hLdrMod);
    switch (enmFmt)
    {
        case RTLDRFMT_MACHO:
        {
            rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_UUID, &Uuid, sizeof(Uuid));
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(rc, "Error quering image UUID from image '%s': %Rrc", pszPath, rc);

            rc = RTUuidToStr(&Uuid, szSubDir, sizeof(szSubDir));
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(rc, "Error convering UUID for image '%s' to string: %Rrc", pszPath, rc);
            pUuid = &Uuid;
            break;
        }

        case RTLDRFMT_PE:
        {
            uint32_t uTimestamp;
            rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp, sizeof(uTimestamp));
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(rc, "Error quering timestamp from image '%s': %Rrc", pszPath, rc);

            size_t cbImage = RTLdrSize(hLdrMod);
            if (cbImage == ~(size_t)0)
                return RTMsgErrorRc(rc, "Error quering size of image '%s': %Rrc", pszPath, rc);

            RTStrPrintf(szSubDir, sizeof(szSubDir), "%08X%x", uTimestamp, cbImage);
            break;
        }

        case RTLDRFMT_AOUT:
            return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Caching of a.out image has not yet been implemented: %s", pszPath);
        case RTLDRFMT_ELF:
            return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Caching of ELF image has not yet been implemented: %s", pszPath);
        case RTLDRFMT_LX:
            return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Caching of LX image has not yet been implemented: %s", pszPath);
        default:
            return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Unknown loader format for '%s': %d", pszPath, enmFmt);
    }

    /*
     * Now add it.
     */
    return rtDbgSymCacheAddOneFile(pszPath, pszDstName, pszExtraSuff, szSubDir, pUuid, pszUuidMapDir, pCfg);
}


/**
 * Adds what we think is an image file to the cache.
 *
 * @returns IPRT status code.
 * @param   pszPath         Path to the image file.
 * @param   pszDstName      Add to the cache under this name.  Typically the
 *                          filename part of @a pszPath.
 * @param   pszExtraSuff    Optional extra suffix. Mach-O dSYM hack.
 * @param   pszUuidMapDir   The UUID map subdirectory in the cache, if this is
 *                          wanted, otherwise NULL.
 * @param   pCfg            Configuration data.
 */
static int rtDbgSymCacheAddImageFile(const char *pszPath, const char *pszDstName, const char *pszExtraSuff,
                                     const char *pszUuidMapDir, PCRTDBGSYMCACHEADDCFG pCfg)
{
    RTERRINFOSTATIC ErrInfo;

    /*
     * Use the loader to open the alleged image file.  We need to open it with
     * arch set to amd64 and x86_32 in order to handle FAT images from the mac
     * guys (we should actually enumerate archs, but that's currently not
     * implemented nor necessary for our current use).
     */
    /* Open it as AMD64. */
    RTLDRMOD hLdrMod64;
    int rc = RTLdrOpenEx(pszPath, RTLDR_O_FOR_DEBUG, RTLDRARCH_AMD64, &hLdrMod64, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        if (rc != VERR_LDR_ARCH_MISMATCH)
        {
            if (rc != VERR_INVALID_EXE_SIGNATURE)
                return RTMsgErrorRc(rc, "RTLdrOpen failed opening '%s' [arch=amd64]: %Rrc%s%s", pszPath, rc,
                                    RTErrInfoIsSet(&ErrInfo.Core) ? " - " : "",
                                    RTErrInfoIsSet(&ErrInfo.Core) ? ErrInfo.Core.pszMsg : "");

            RTMsgInfo("Skipping '%s', no a recognizable image file...", pszPath);
            return VINF_SUCCESS;
        }
        hLdrMod64 = NIL_RTLDRMOD;
    }

    /* Open it as X86. */
    RTLDRMOD hLdrMod32;
    rc = RTLdrOpenEx(pszPath, RTLDR_O_FOR_DEBUG, RTLDRARCH_X86_32, &hLdrMod32, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        if (rc != VERR_LDR_ARCH_MISMATCH)
        {
            RTLdrClose(hLdrMod64);
            return RTMsgErrorRc(rc, "RTLdrOpen failed opening '%s' [arch=x86]: %Rrc%s%s", pszPath, rc,
                                RTErrInfoIsSet(&ErrInfo.Core) ? " - " : "",
                                RTErrInfoIsSet(&ErrInfo.Core) ? ErrInfo.Core.pszMsg : "");
        }
        hLdrMod32 = NIL_RTLDRMOD;
    }

    /*
     * Add the file.
     */
    if (hLdrMod32 == NIL_RTLDRMOD)
        rc = rtDbgSymCacheAddImageFileWorker(pszPath, pszDstName, pCfg, hLdrMod64, pszExtraSuff, pszUuidMapDir);
    else if (hLdrMod64 == NIL_RTLDRMOD)
        rc = rtDbgSymCacheAddImageFileWorker(pszPath, pszDstName, pCfg, hLdrMod32, pszExtraSuff, pszUuidMapDir);
    else
    {
        /*
         * Do we need to add it once or twice?
         */
        RTLDRFMT enmFmt = RTLdrGetFormat(hLdrMod32);
        bool     fSame  = enmFmt == RTLdrGetFormat(hLdrMod64);
        if (fSame && enmFmt == RTLDRFMT_MACHO)
        {
            RTUUID Uuid32, Uuid64;
            int rc32 = RTLdrQueryProp(hLdrMod32, RTLDRPROP_UUID, &Uuid32, sizeof(Uuid32));
            int rc64 = RTLdrQueryProp(hLdrMod64, RTLDRPROP_UUID, &Uuid64, sizeof(Uuid64));
            fSame = RT_SUCCESS(rc32) == RT_SUCCESS(rc64);
            if (fSame && RT_SUCCESS(rc32))
                fSame = RTUuidCompare(&Uuid32, &Uuid64) == 0;
        }
        else if (fSame && enmFmt == RTLDRFMT_PE)
        {
            fSame = RTLdrSize(hLdrMod32) == RTLdrSize(hLdrMod64);
            if (fSame)
            {
                uint32_t uTimestamp32, uTimestamp64;
                int rc32 = RTLdrQueryProp(hLdrMod32, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp32, sizeof(uTimestamp32));
                int rc64 = RTLdrQueryProp(hLdrMod64, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp64, sizeof(uTimestamp64));
                fSame = RT_SUCCESS(rc32) == RT_SUCCESS(rc64);
                if (fSame && RT_SUCCESS(rc32))
                    fSame = uTimestamp32 == uTimestamp64;
            }
        }

        rc = rtDbgSymCacheAddImageFileWorker(pszPath, pszDstName, pCfg, hLdrMod64, pszExtraSuff, pszUuidMapDir);
        if (!fSame)
        {
            /** @todo should symlink or hardlink this second copy. */
            int rc2 = rtDbgSymCacheAddImageFileWorker(pszPath, pszDstName, pCfg, hLdrMod32, pszExtraSuff, pszUuidMapDir);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
    }

    RTLdrClose(hLdrMod32);
    RTLdrClose(hLdrMod64);
    return VINF_SUCCESS;
}


/**
 * Worker for rtDbgSymCacheAddDebugFile that adds a Mach-O debug file to the
 * cache.
 *
 * @returns IPRT status code
 * @param   pszPath             The path to the PDB file.
 * @param   pszDstName          Add to the cache under this name.  Typically the
 *                              filename part of @a pszPath.
 * @param   pCfg                The configuration.
 * @param   hFile               Handle to the file.
 */
static int rtDbgSymCacheAddDebugMachO(const char *pszPath, const char *pszDstName, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /* This shouldn't happen, figure out what to do if it does. */
    RT_NOREF(pCfg, pszDstName);
    return RTMsgErrorRc(VERR_NOT_IMPLEMENTED,
                        "'%s' is an OS X image file, did you point me to a file inside a .dSYM or .sym file?",
                        pszPath);
}


/**
 * Worker for rtDbgSymCacheAddDebugFile that adds DBGs to the cache.
 *
 * @returns IPRT status code
 * @param   pszPath             The path to the PDB file.
 * @param   pszDstName          Add to the cache under this name.  Typically the
 *                              filename part of @a pszPath.
 * @param   pCfg                The configuration.
 * @param   pHdr                The DBG file header.
 */
static int rtDbgSymCacheAddDebugDbg(const char *pszPath, const char *pszDstName, PCRTDBGSYMCACHEADDCFG pCfg,
                                    PCIMAGE_SEPARATE_DEBUG_HEADER pHdr)
{
    if (   pHdr->SizeOfImage == 0
        || pHdr->SizeOfImage >= UINT32_MAX / 2
        || pHdr->TimeDateStamp < 16
        || pHdr->TimeDateStamp >= UINT32_MAX - 16
        || pHdr->NumberOfSections >= UINT16_MAX / 2)
        return RTMsgErrorRc(VERR_OUT_OF_RANGE,
                            "Bogus separate debug header in '%s': SizeOfImage=%#RX32 TimeDateStamp=%#RX32 NumberOfSections=%#RX16",
                            pszPath, pHdr->SizeOfImage, pHdr->TimeDateStamp, pHdr->NumberOfSections);
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%08X%x", pHdr->TimeDateStamp, pHdr->SizeOfImage);
    return rtDbgSymCacheAddOneFile(pszPath, pszDstName, NULL, szSubDir, NULL, NULL, pCfg);
}


/**
 * Worker for rtDbgSymCacheAddDebugFile that adds v7 PDBs to the cache.
 *
 * @returns IPRT status code
 * @param   pszPath             The path to the PDB file.
 * @param   pszDstName          Add to the cache under this name.  Typically the
 *                              filename part of @a pszPath.
 * @param   pCfg                The configuration.
 * @param   hFile               Handle to the file.
 */
static int rtDbgSymCacheAddDebugPdb(const char *pszPath, const char *pszDstName, PCRTDBGSYMCACHEADDCFG pCfg, RTFILE hFile)
{
    /*
     * Open the PDB as a VFS.
     */
    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int rc = RTVfsFileFromRTFile(hFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, true /*fLeaveOpen*/, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        RTERRINFOSTATIC ErrInfo;
        RTVFS           hVfsPdb = NIL_RTVFS;
        rc = RTFsPdbVolOpen(hVfsFile, 0, &hVfsPdb, RTErrInfoInitStatic(&ErrInfo));
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(rc))
        {
            /*
             * The cache subdirectory name can be retrieved via RTVFSQIEX_VOL_LABEL,
             * the primary volume label.  Then we just add it to the cache.
             */
            char szSubDir[64];
            rc = RTVfsQueryLabel(hVfsPdb, false /*fAlternative*/, szSubDir, sizeof(szSubDir), NULL);
            if (RT_SUCCESS(rc))
                rc = rtDbgSymCacheAddOneFile(pszPath, pszDstName, NULL, szSubDir, NULL, NULL, pCfg);
            else
                RTMsgErrorRc(rc, "RTVfsQueryLabel failed on '%s': %Rrc", pszPath, rc);
            RTVfsRelease(hVfsPdb);
        }
        else
            RTMsgErrorRc(rc, "RTFsPdbVolOpen failed on '%s': %Rrc%#RTeim", pszPath, rc, &ErrInfo.Core);
    }
    else
        RTMsgErrorRc(rc, "RTVfsFileFromRTFile failed on '%s': %Rrc", pszPath, rc);
    return rc;

}


/**
 * Adds a debug file to the cache.
 *
 * @returns IPRT status code
 * @param   pszPath             The path to the debug file in question.
 * @param   pszDstName          Add to the cache under this name.  Typically the
 *                              filename part of @a pszPath.
 * @param   pCfg                The configuration.
 */
static int rtDbgSymCacheAddDebugFile(const char *pszPath, const char *pszDstName, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Need to extract an identifier of sorts here in order to put them in
     * the right place in the cache.  Currently only implemnted for Mach-O
     * files since these use executable containers.
     *
     * We take a look at the file header in hope to figure out what to do
     * with the file.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Error opening '%s': %Rrc", pszPath, rc);

    uint64_t cbFile = 0;
    rc = RTFileQuerySize(hFile, &cbFile);
    if (RT_SUCCESS(rc))
    {

        union
        {
            uint64_t au64[16];
            uint32_t au32[16];
            uint16_t au16[32];
            uint8_t  ab[64];
            RTPDB70HDR Pdb70Hdr;
            RTPDB20HDR Pdb20Hdr;
            IMAGE_SEPARATE_DEBUG_HEADER DbgHdr;
        } uBuf;
        rc = RTFileRead(hFile, &uBuf, sizeof(uBuf), NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Look for magics and call workers.
             */
            if (   memcmp(uBuf.Pdb70Hdr.szSignature, RTPDB_SIGNATURE_700, sizeof(uBuf.Pdb70Hdr.szSignature)) == 0
                || memcmp(uBuf.Pdb20Hdr.szSignature, RTPDB_SIGNATURE_200, sizeof(uBuf.Pdb20Hdr.szSignature)) == 0)
                rc = rtDbgSymCacheAddDebugPdb(pszPath, pszDstName, pCfg, hFile);
            else if (uBuf.au16[0] == IMAGE_SEPARATE_DEBUG_SIGNATURE)
                rc = rtDbgSymCacheAddDebugDbg(pszPath, pszDstName, pCfg, &uBuf.DbgHdr);
            else if (   uBuf.au32[0] == IMAGE_FAT_SIGNATURE
                     || uBuf.au32[0] == IMAGE_FAT_SIGNATURE_OE
                     || uBuf.au32[0] == IMAGE_MACHO32_SIGNATURE
                     || uBuf.au32[0] == IMAGE_MACHO64_SIGNATURE
                     || uBuf.au32[0] == IMAGE_MACHO32_SIGNATURE_OE
                     || uBuf.au32[0] == IMAGE_MACHO64_SIGNATURE_OE)
                rc = rtDbgSymCacheAddDebugMachO(pszPath, pszDstName, pCfg);
            else
                rc = RTMsgErrorRc(VERR_INVALID_MAGIC, "Unsupported debug file '%s' magic: %#010x", pszPath, uBuf.au32[0]);
        }
        else
            rc = RTMsgErrorRc(rc, "Error reading '%s': %Rrc", pszPath, rc);
    }
    else
        rc = RTMsgErrorRc(rc, "Error query size of '%s': %Rrc", pszPath, rc);

    /* close the file. */
    int rc2 = RTFileClose(hFile);
    if (RT_FAILURE(rc2))
    {
        RTMsgError("Error closing '%s': %Rrc", pszPath, rc2);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}


/**
 * Constructs the path to the file instide the bundle that we're keen on.
 *
 * @returns IPRT status code.
 * @param   pszPath             Path to the bundle on input, on successful
 *                              return it's the path to the desired file.  This
 *                              a RTPATH_MAX size buffer.
 * @param   cchPath             The length of the path up to the bundle name.
 * @param   cchName             The length of the bundle name.
 * @param   pszSubDir           The bundle subdirectory the file lives in.
 * @param   papszSuffixes       Pointer to an array of bundle suffixes.
 */
static int rtDbgSymCacheConstructBundlePath(char *pszPath, size_t cchPath, size_t cchName, const char *pszSubDir,
                                            const char * const *papszSuffixes)
{
    /*
     * Calc the name without the bundle extension.
     */
    size_t const cchOrgName = cchName;
    const char  *pszEnd     = &pszPath[cchPath + cchName];
    for (unsigned i = 0; papszSuffixes[i]; i++)
    {
        Assert(papszSuffixes[i][0] == '.');
        size_t cchSuff = strlen(papszSuffixes[i]);
        if (   cchSuff < cchName
            && !memcmp(&pszEnd[-(ssize_t)cchSuff], papszSuffixes[i], cchSuff))
        {
            cchName -= cchSuff;
            break;
        }
    }

    /*
     * Check the immediate directory first, in case it's layed out like
     * IOPCIFamily.kext.
     */
    int rc = RTPathAppendEx(pszPath, RTPATH_MAX, &pszPath[cchPath], cchName, RTPATH_STR_F_STYLE_HOST);
    if (RT_FAILURE(rc) || !RTFileExists(pszPath))
    {
        /*
         * Not there, ok then try the given subdirectory + name.
         */
        pszPath[cchPath + cchOrgName] = '\0';
        rc = RTPathAppend(pszPath, RTPATH_MAX, pszSubDir);
        if (RT_SUCCESS(rc))
            rc = RTPathAppendEx(pszPath, RTPATH_MAX, &pszPath[cchPath], cchName, RTPATH_STR_F_STYLE_HOST);
        if (RT_FAILURE(rc))
        {
            pszPath[cchPath + cchOrgName] = '\0';
            return RTMsgErrorRc(rc, "Error constructing image bundle path for '%s': %Rrc", pszPath, rc);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Adds a image bundle of some sort.
 *
 * @returns IPRT status code.
 * @param   pszPath         Path to the bundle. This a RTPATH_MAX size
 *                          buffer that we can write to when creating the
 *                          path to the file inside the bundle that we're
 *                          interested in.
 * @param   cchPath         The length of the path up to the bundle name.
 * @param   cchName         The length of the bundle name.
 * @param   pszDstName      Add to the cache under this name, NULL if not
 *                          specified.
 * @param   pDirEntry       The directory entry buffer, for handling bundle
 *                          within bundle recursion.
 * @param   pCfg            The configuration.
 */
static int rtDbgSymCacheAddImageBundle(char *pszPath, size_t cchPath, size_t cchName, const char *pszDstName,
                                       PRTDIRENTRYEX pDirEntry, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Assuming these are kexts or simple applications, we only add the image
     * file itself to the cache.  No Info.plist or other files.
     */
    /** @todo consider looking for Frameworks and handling framework bundles. */
    int rc = rtDbgSymCacheConstructBundlePath(pszPath, cchPath, cchName, "Contents/MacOS/", g_apszBundleSuffixes);
    if (RT_SUCCESS(rc))
    {
        if (!pszDstName)
            pszDstName = RTPathFilename(pszPath);
        rc = rtDbgSymCacheAddImageFile(pszPath, pszDstName, NULL, RTDBG_CACHE_UUID_MAP_DIR_IMAGES, pCfg);
    }

    /*
     * Look for plugins and other sub-bundles.
     */
    if (pCfg->fRecursive)
    {
        static char const * const s_apszSubBundleDirs[] =
        {
            "Contents/Plugins/",
            /** @todo Frameworks ++ */
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszSubBundleDirs); i++)
        {
            pszPath[cchPath + cchName] = '\0';
            int rc2 = RTPathAppend(pszPath, RTPATH_MAX - 1, s_apszSubBundleDirs[i]);
            if (RT_SUCCESS(rc2))
            {
                if (RTDirExists(pszPath))
                {
                    size_t cchPath2 = strlen(pszPath);
                    if (!RTPATH_IS_SLASH(pszPath[cchPath2 - 1]))
                    {
                        pszPath[cchPath2++] = RTPATH_SLASH;
                        pszPath[cchPath2]   = '\0';
                    }
                    rc2 = rtDbgSymCacheAddDirWorker(pszPath, cchPath2, pDirEntry, pCfg);
                }
            }
            else
            {
                pszPath[cchPath + cchName] = '\0';
                RTMsgError("Error constructing bundle subdir path for '%s' + '%s': %Rrc", pszPath, s_apszSubBundleDirs[i], rc);
            }
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
    }

    return rc;
}


/**
 * Adds a debug bundle.
 *
 * @returns IPRT status code.
 * @param   pszPath         Path to the bundle. This a RTPATH_MAX size
 *                          buffer that we can write to when creating the
 *                          path to the file inside the bundle that we're
 *                          interested in.
 * @param   cchPath         The length of the path up to the bundle name.
 * @param   cchName         The length of the bundle name.
 * @param   pszDstName      Add to the cache under this name, NULL if not
 *                          specified.
 * @param   pCfg            The configuration.
 */
static int rtDbgSymCacheAddDebugBundle(char *pszPath, size_t cchPath, size_t cchName, const char *pszDstName,
                                       PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * The current policy is not to add the whole .dSYM (or .sym) bundle, but
     * rather just the dwarf image instide it.  The <UUID>.plist and Info.plist
     * files generally doesn't contain much extra information that's really
     * necessary, I hope.  At least this is what the uuidmap example in the
     * lldb hints at (it links to the dwarf file, not the .dSYM dir).
     *
     * To avoid confusion with a .dSYM bundle, as well as collision with the
     * image file, we use .dwarf suffix for the file.
     *
     * For details on the uuid map see rtDbgSymCacheAddImageFile as well as
     * http://lldb.llvm.org/symbols.html .
     *
     * ASSUMES bundles contains Mach-O DWARF files.
     */
    int rc = rtDbgSymCacheConstructBundlePath(pszPath, cchPath, cchName, "Contents/Resources/DWARF/", g_apszDSymBundleSuffixes);
    if (RT_SUCCESS(rc))
    {
        if (!pszDstName)
            pszDstName = RTPathFilename(pszPath);
        rc = rtDbgSymCacheAddImageFile(pszPath, pszDstName, RTDBG_CACHE_DSYM_FILE_SUFFIX, RTDBG_CACHE_UUID_MAP_DIR_DSYMS, pCfg);
    }
    return rc;
}


/**
 * Figure the type of a file/dir based on path and FS object info.
 *
 * @returns The type.
 * @param   pszPath             The path to the file/dir.
 * @param   pObjInfo            The object information, symlinks followed.
 */
static RTDBGSYMCACHEFILETYPE rtDbgSymCacheFigureType2(const char *pszPath, PCRTFSOBJINFO pObjInfo)
{
    const char *pszName = RTPathFilename(pszPath);
    const char *pszExt  = RTPathSuffix(pszName);
    if (pszExt)
        pszExt++;
    else
        pszExt = "";

    if (   RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode)
        || (pObjInfo->Attr.fMode & RTFS_DOS_DIRECTORY)) /** @todo OS X samba reports reparse points in /Volumes/ that we cannot resolve. */
    {
        /* Skip directories shouldn't bother with. */
        if (   !RTStrICmp(pszName, ".Trashes")
            || !RTStrICmp(pszName, ".$RESCYCLE.BIN")
            || !RTStrICmp(pszName, "System.kext") /* Usually only plugins here, so skip it. */
           )
            return RTDBGSYMCACHEFILETYPE_IGNORE;

        /* Directories can also be bundles on the mac. */
        if (!RTStrICmp(pszExt, "dSYM"))
            return RTDBGSYMCACHEFILETYPE_DEBUG_BUNDLE;

        for (unsigned i = 0; i < RT_ELEMENTS(g_apszBundleSuffixes) - 1; i++)
            if (!RTStrICmp(pszExt, &g_apszBundleSuffixes[i][1]))
                return RTDBGSYMCACHEFILETYPE_IMAGE_BUNDLE;

        return RTDBGSYMCACHEFILETYPE_DIR;
    }

    if (!RTFS_IS_FILE(pObjInfo->Attr.fMode))
        return RTDBGSYMCACHEFILETYPE_INVALID;

    /* Select image vs debug info based on extension. */
    if (   !RTStrICmp(pszExt, "pdb")
        || !RTStrICmp(pszExt, "dbg")
        || !RTStrICmp(pszExt, "sym")
        || !RTStrICmp(pszExt, "dwo")
        || !RTStrICmp(pszExt, "dwp")
        || !RTStrICmp(pszExt, "debug")
        || !RTStrICmp(pszExt, "dsym")
        || !RTStrICmp(pszExt, "dwarf")
        || !RTStrICmp(pszExt, "map")
        || !RTStrICmp(pszExt, "cv"))
        return RTDBGSYMCACHEFILETYPE_DEBUG_FILE;

    /* Filter out a bunch of files which obviously shouldn't be images. */
    if (   !RTStrICmp(pszExt, "txt")
        || !RTStrICmp(pszExt, "html")
        || !RTStrICmp(pszExt, "htm")
        || !RTStrICmp(pszExt, "rtf")
        || !RTStrICmp(pszExt, "zip")
        || !RTStrICmp(pszExt, "doc")
        || !RTStrICmp(pszExt, "gz")
        || !RTStrICmp(pszExt, "bz2")
        || !RTStrICmp(pszExt, "xz")
        || !RTStrICmp(pszExt, "kmk")
        || !RTStrICmp(pszExt, "c")
        || !RTStrICmp(pszExt, "cpp")
        || !RTStrICmp(pszExt, "h")
        || !RTStrICmp(pszExt, "m")
        || !RTStrICmp(pszExt, "mm")
        || !RTStrICmp(pszExt, "asm")
        || !RTStrICmp(pszExt, "S")
        || !RTStrICmp(pszExt, "inc")
        || !RTStrICmp(pszExt, "sh")
       )
        return RTDBGSYMCACHEFILETYPE_IGNORE;
    if (   !RTStrICmp(pszName, "Makefile")
        || !RTStrICmp(pszName, "GNUmakefile")
        || !RTStrICmp(pszName, "createsymbolfiles")
        || !RTStrICmp(pszName, "kgmacros")
       )
        return RTDBGSYMCACHEFILETYPE_IGNORE;

    return RTDBGSYMCACHEFILETYPE_IMAGE_FILE;
}


/**
 * Figure file type based on name, will stat the file/dir.
 *
 * @returns File type.
 * @param   pszPath             The path to the file/dir to figure.
 */
static RTDBGSYMCACHEFILETYPE rtDbgSymCacheFigureType(const char *pszPath)
{
    const char *pszName = RTPathFilename(pszPath);

    /* Trailing slash. */
    if (!pszName)
        return RTDBGSYMCACHEFILETYPE_DIR;

    /* Wildcard means listing directory and filtering.  */
    if (strpbrk(pszName, "?*"))
        return RTDBGSYMCACHEFILETYPE_DIR_FILTER;

    /* Get object info, following links. */
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    if (RT_FAILURE(rc))
        return RTDBGSYMCACHEFILETYPE_INVALID;
    return rtDbgSymCacheFigureType2(pszPath, &ObjInfo);
}


/**
 * Recursive worker for rtDbgSymCacheAddDir, for minimal stack wasting.
 *
 * @returns IPRT status code (fully bitched).
 * @param   pszPath             Pointer to a RTPATH_MAX size buffer containing
 *                              the path to the current directory ending with a
 *                              slash.
 * @param   cchPath             The size of the current directory path.
 * @param   pDirEntry           Pointer to the RTDIRENTRYEX structure to use.
 * @param   pCfg                The configuration.
 */
static int rtDbgSymCacheAddDirWorker(char *pszPath, size_t cchPath, PRTDIRENTRYEX pDirEntry, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Open the directory.
     */
    RTDIR hDir;
    int rc, rc2;
    if (pCfg->pszFilter)
    {
        rc = RTStrCopy(&pszPath[cchPath], RTPATH_MAX - cchPath, pCfg->pszFilter);
        if (RT_FAILURE(rc))
        {
            pszPath[cchPath] = '\0';
            return RTMsgErrorRc(rc, "Filename too long (%Rrc): '%s" RTPATH_SLASH_STR "%s'", rc, pszPath, pCfg->pszFilter);
        }
        rc = RTDirOpenFiltered(&hDir, pszPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    }
    else
        rc = RTDirOpen(&hDir, pszPath);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "RTDirOpen%s failed on '%s': %Rrc", pCfg->pszFilter ? "Filtered" : "", pszPath, rc);

    /*
     * Enumerate the files.
     */
    for (;;)
    {
        rc2 = RTDirReadEx(hDir, pDirEntry, NULL, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (RT_FAILURE(rc2))
        {
            pszPath[cchPath] = '\0';
            if (rc2 != VERR_NO_MORE_FILES)
            {
                RTMsgError("RTDirReadEx failed in '%s': %Rrc\n", pszPath, rc2);
                rc = rc2;
            }
            break;
        }

        /* Skip dot and dot-dot. */
        if (RTDirEntryExIsStdDotLink(pDirEntry))
            continue;

        /* Construct a full path. */
        rc = RTStrCopy(&pszPath[cchPath], RTPATH_MAX, pDirEntry->szName);
        if (RT_FAILURE(rc))
        {
            pszPath[cchPath] = '\0';
            RTMsgError("File name too long in '%s': '%s' (%Rrc)", pszPath, pDirEntry->szName, rc);
            break;
        }

        switch (rtDbgSymCacheFigureType2(pszPath, &pDirEntry->Info))
        {
            case RTDBGSYMCACHEFILETYPE_DIR:
                if (!pCfg->fRecursive)
                    RTMsgInfo("Skipping directory '%s'...", pszPath);
                else
                {
                    if (cchPath + pDirEntry->cbName + 3 <= RTPATH_MAX)
                    {
                        pszPath[cchPath + pDirEntry->cbName] = RTPATH_SLASH;
                        pszPath[cchPath + pDirEntry->cbName + 1] = '\0';
                        rc2 = rtDbgSymCacheAddDirWorker(pszPath, cchPath + pDirEntry->cbName + 1, pDirEntry, pCfg);
                    }
                    else
                    {
                        RTMsgError("File name too long in '%s': '%s' (%Rrc)", pszPath, pDirEntry->szName, rc);
                        rc2 = VERR_FILENAME_TOO_LONG;
                    }
                }
                break;

            case RTDBGSYMCACHEFILETYPE_DEBUG_FILE:
                rc2 = rtDbgSymCacheAddDebugFile(pszPath, pDirEntry->szName, pCfg);
                break;

            case RTDBGSYMCACHEFILETYPE_IMAGE_FILE:
                rc2 = rtDbgSymCacheAddImageFile(pszPath, pDirEntry->szName, NULL /*pszExtraSuff*/, RTDBG_CACHE_UUID_MAP_DIR_IMAGES, pCfg);
                break;

            case RTDBGSYMCACHEFILETYPE_DEBUG_BUNDLE:
                rc2 = rtDbgSymCacheAddDebugBundle(pszPath, cchPath, pDirEntry->cbName, NULL /*pszDstName*/, pCfg);
                break;

            case RTDBGSYMCACHEFILETYPE_IMAGE_BUNDLE:
                rc2 = rtDbgSymCacheAddImageBundle(pszPath, cchPath, pDirEntry->cbName, NULL /*pszDstName*/, pDirEntry, pCfg);
                break;

            case RTDBGSYMCACHEFILETYPE_DIR_FILTER:
            case RTDBGSYMCACHEFILETYPE_INVALID:
                rc2 = RTMsgErrorRc(VERR_INTERNAL_ERROR_2, "Invalid: '%s'", pszPath);
                break;

            case RTDBGSYMCACHEFILETYPE_IGNORE:
                rc2 = VINF_SUCCESS;
                break;
        }

        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    /*
     * Clean up.
     */
    rc2 = RTDirClose(hDir);
    if (RT_FAILURE(rc2))
    {
        RTMsgError("RTDirClose failed in '%s': %Rrc", pszPath, rc);
        rc = rc2;
    }
    return rc;
}


/**
 * Adds a directory.
 *
 * @returns IPRT status code (fully bitched).
 * @param   pszPath             The directory path.
 * @param   pCfg                The configuration.
 */
static int rtDbgSymCacheAddDir(const char *pszPath, PCRTDBGSYMCACHEADDCFG pCfg)
{
    /*
     * Set up the path buffer, stripping any filter.
     */
    char szPath[RTPATH_MAX];
    int rc = RTStrCopy(szPath, sizeof(szPath) - 2, pszPath);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Path too long: '%s'", pszPath);

    size_t cchPath = strlen(pszPath);
    if (!cchPath)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Path empty: '%s'", pszPath);

    if (pCfg->pszFilter)
        szPath[cchPath - strlen(pCfg->pszFilter)] = '\0';
    cchPath = RTPathStripTrailingSlash(szPath);
    if (!RTPATH_IS_SEP(pszPath[cchPath - 1]))
    {
        szPath[cchPath++] = RTPATH_SLASH;
        szPath[cchPath]   = '\0';
    }

    /*
     * Let the worker do the rest.
     */
    RTDIRENTRYEX DirEntry;
    return rtDbgSymCacheAddDirWorker(szPath, cchPath, &DirEntry, pCfg);
}


/**
 * Adds a file or directory.
 *
 * @returns Program exit code.
 * @param   pszPath                 The user supplied path to the file or directory.
 * @param   pszCache                The path to the cache.
 * @param   fRecursive              Whether to process directories recursively.
 * @param   fOverwriteOnConflict    Whether to overwrite existing cache entry on
 *                                  conflict, or just leave it.
 */
static RTEXITCODE rtDbgSymCacheAddFileOrDir(const char *pszPath, const char *pszCache, bool fRecursive,
                                            bool fOverwriteOnConflict)
{
    RT_NOREF1(fOverwriteOnConflict);
    RTDBGSYMCACHEADDCFG Cfg;
    Cfg.fRecursive      = fRecursive;
    Cfg.pszCache        = pszCache;
    Cfg.pszFilter       = NULL;

    /* If the filename contains an equal ('=') char, treat the left as the file
       to add tne right part as the name to add it under (handy for kernels). */
    char *pszFree = NULL;
    const char *pszDstName = RTPathFilename(pszPath);
    const char *pszEqual   = pszDstName ? strchr(pszDstName, '=') : NULL;
    if (pszEqual)
    {
        pszPath = pszFree = RTStrDupN(pszPath, pszEqual - pszPath);
        if (!pszFree)
            return RTMsgErrorExitFailure("out of memory!\n");
        pszDstName = pszEqual + 1;
        if (!*pszDstName)
            return RTMsgErrorExitFailure("add-as filename is empty!\n");
    }

    int rc;
    RTDBGSYMCACHEFILETYPE enmType = rtDbgSymCacheFigureType(pszPath);
    switch (enmType)
    {
        default:
        case RTDBGSYMCACHEFILETYPE_INVALID:
            rc = RTMsgErrorRc(VERR_INVALID_PARAMETER, "Invalid: '%s'", pszPath);
            break;

        case RTDBGSYMCACHEFILETYPE_DIR_FILTER:
            Cfg.pszFilter = RTPathFilename(pszPath);
            RT_FALL_THRU();
        case RTDBGSYMCACHEFILETYPE_DIR:
            if (!pszEqual)
                rc = rtDbgSymCacheAddDir(pszPath, &Cfg);
            else
                rc = RTMsgErrorRc(VERR_INVALID_PARAMETER, "Add-as filename is not applicable to directories!");
            break;

        case RTDBGSYMCACHEFILETYPE_DEBUG_FILE:
            rc = rtDbgSymCacheAddDebugFile(pszPath, pszDstName, &Cfg);
            break;

        case RTDBGSYMCACHEFILETYPE_IMAGE_FILE:
            rc = rtDbgSymCacheAddImageFile(pszPath, pszDstName, NULL /*pszExtraSuff*/, RTDBG_CACHE_UUID_MAP_DIR_IMAGES, &Cfg);
            break;

        case RTDBGSYMCACHEFILETYPE_DEBUG_BUNDLE:
        case RTDBGSYMCACHEFILETYPE_IMAGE_BUNDLE:
        {
            size_t cchPath     = strlen(pszPath);
            size_t cchFilename = strlen(RTPathFilename(pszPath));
            char szPathBuf[RTPATH_MAX];
            if (cchPath < sizeof(szPathBuf))
            {
                memcpy(szPathBuf, pszPath, cchPath + 1);
                if (enmType == RTDBGSYMCACHEFILETYPE_DEBUG_BUNDLE)
                    rc = rtDbgSymCacheAddDebugBundle(szPathBuf, cchPath - cchFilename, cchFilename,
                                                     pszEqual ? pszDstName : NULL, &Cfg);
                else
                {
                    RTDIRENTRYEX DirEntry;
                    rc = rtDbgSymCacheAddImageBundle(szPathBuf, cchPath - cchFilename, cchFilename,
                                                     pszEqual ? pszDstName : NULL, &DirEntry, &Cfg);
                }
            }
            else
                rc = RTMsgErrorRc(VERR_FILENAME_TOO_LONG, "Filename too long: '%s'", pszPath);
            break;
        }

        case RTDBGSYMCACHEFILETYPE_IGNORE:
            rc = RTMsgErrorRc(VERR_INVALID_PARAMETER, "Invalid file: '%s'", pszPath);
            break;
    }

    RTStrFree(pszFree);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Handles the 'add' command.
 *
 * @returns Program exit code.
 * @param   pszArg0             The program name.
 * @param   cArgs               The number of arguments to the 'add' command.
 * @param   papszArgs           The argument vector, starting after 'add'.
 */
static RTEXITCODE rtDbgSymCacheCmdAdd(const char *pszArg0, int cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--recursive",                'R', RTGETOPT_REQ_NOTHING },
        { "--no-recursive",             'n', RTGETOPT_REQ_NOTHING },
        { "--overwrite-on-conflict",    'o', RTGETOPT_REQ_NOTHING },
    };

    const char *pszCache                = NULL;
    bool        fRecursive              = false;
    bool        fOverwriteOnConflict    = false;

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 0,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    //uint32_t        cAdded = 0;
    RTGETOPTUNION   ValueUnion;
    int             chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'R':
                fRecursive = true;
                break;

            case 'n':
                fRecursive = false;
                break;

            case 'o':
                fOverwriteOnConflict = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                /* The first non-option is a cache directory. */
                if (!pszCache)
                {
                    pszCache = ValueUnion.psz;
                    if (!RTPathExists(pszCache))
                    {
                        rc = RTDirCreate(pszCache, 0755, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
                        if (RT_FAILURE(rc))
                            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Error creating cache directory '%s': %Rrc", pszCache, rc);
                    }
                    else if (!RTDirExists(pszCache))
                        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Specified cache directory is not a directory: '%s'", pszCache);
                }
                /* Subsequent non-options are files to be added to the cache. */
                else
                {
                    RTEXITCODE rcExit = rtDbgSymCacheAddFileOrDir(ValueUnion.psz, pszCache, fRecursive, fOverwriteOnConflict);
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                }
                break;

            case 'h':
                return rtDbgSymCacheUsage(pszArg0, "add");
            case 'V':
                return rtDbgSymCacheVersion();
            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    if (!pszCache)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No cache directory or files to add were specified.");
    return RTEXITCODE_SUCCESS;
}


/**
 * Debug info + external path for the 'get' command.
 */
typedef struct MYDBGINFO
{
    /** The kind of debug info. */
    RTLDRDBGINFOTYPE    enmType;
    /** The CRC32 of the external file (RTLDRDBGINFOTYPE_DWARF_DWO). */
    uint32_t            uDwoCrc32;
    /** The PE image size (RTLDRDBGINFOTYPE_CODEVIEW_DBG,
     * RTLDRDBGINFOTYPE_CODEVIEW_PDB20, RTLDRDBGINFOTYPE_CODEVIEW_PDB70 (,
     * RTLDRDBGINFOTYPE_CODEVIEW, RTLDRDBGINFOTYPE_COFF)). */
    uint32_t            cbImage;
    /** Timestamp in seconds since unix epoch (RTLDRDBGINFOTYPE_CODEVIEW_DBG,
     * RTLDRDBGINFOTYPE_CODEVIEW_PDB20 (, RTLDRDBGINFOTYPE_CODEVIEW,
     * RTLDRDBGINFOTYPE_COFF)). */
    uint32_t            uTimestamp;
    /** The PDB age (RTLDRDBGINFOTYPE_CODEVIEW_PDB20, RTLDRDBGINFOTYPE_CODEVIEW_PDB70). */
    uint32_t            uPdbAge;
    /** The UUID of the PDB or mach-o image (RTLDRDBGINFOTYPE_CODEVIEW_PDB70, +). */
    RTUUID              Uuid;
    /** External path (can be empty). */
    char                szExtFile[RTPATH_MAX];
} MYDBGINFO;

/**
 * @callback_method_impl{FNRTLDRENUMDBG, For the 'get' command.}
 */
static DECLCALLBACK(int) rtDbgSymCacheCmdGetForExeDbgInfoCallback(RTLDRMOD hLdrMod, PCRTLDRDBGINFO pDbgInfo, void *pvUser)
{
    RT_NOREF(hLdrMod);
    if (!pDbgInfo->pszExtFile)
        switch (pDbgInfo->enmType)
        {
            case RTLDRDBGINFOTYPE_CODEVIEW_PDB20:
            case RTLDRDBGINFOTYPE_CODEVIEW_PDB70:
            case RTLDRDBGINFOTYPE_CODEVIEW_DBG:
                break;
            default:
                return VINF_SUCCESS;
        }

    /* Copy the info: */
    MYDBGINFO *pMyInfo = (MYDBGINFO *)pvUser;
    RT_ZERO(*pMyInfo);
    pMyInfo->enmType = pDbgInfo->enmType;
    int rc = VINF_SUCCESS;
    if (pDbgInfo->pszExtFile)
        rc = RTStrCopy(pMyInfo->szExtFile, sizeof(pMyInfo->szExtFile), pDbgInfo->pszExtFile);

    switch (pDbgInfo->enmType)
    {
        case RTLDRDBGINFOTYPE_DWARF_DWO:
            pMyInfo->uDwoCrc32 = pDbgInfo->u.Dwo.uCrc32;
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW:
        case RTLDRDBGINFOTYPE_COFF:
            pMyInfo->cbImage    = pDbgInfo->u.Cv.cbImage;
            pMyInfo->uTimestamp = pDbgInfo->u.Cv.uTimestamp;
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW_DBG:
            pMyInfo->cbImage    = pDbgInfo->u.Dbg.cbImage;
            pMyInfo->uTimestamp = pDbgInfo->u.Dbg.uTimestamp;
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW_PDB20:
            pMyInfo->cbImage    = pDbgInfo->u.Pdb20.cbImage;
            pMyInfo->uTimestamp = pDbgInfo->u.Pdb20.uTimestamp;
            pMyInfo->uPdbAge    = pDbgInfo->u.Pdb20.uAge;
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW_PDB70:
            pMyInfo->cbImage    = pDbgInfo->u.Pdb70.cbImage;
            pMyInfo->Uuid       = pDbgInfo->u.Pdb70.Uuid;
            pMyInfo->uPdbAge    = pDbgInfo->u.Pdb70.uAge;
            break;

        default:
            return VINF_SUCCESS;
    }

    return rc;
}


/**
 * @callback_method_impl{FNRTDBGCFGOPEN}
 */
static DECLCALLBACK(int) rtDbgSymCacheCmdGetCallback(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    RT_NOREF(hDbgCfg, pvUser2);

    char       *pszJoined = NULL;
    const char *pszOutput = (const char *)pvUser1;
    if (!pszOutput || *pszOutput == '\0')
        pszOutput = RTPathFilename(pszFilename);
    else if (RTPathFilename(pszOutput) == NULL)
        pszOutput = pszJoined = RTPathJoinA(pszOutput, RTPathFilename(pszFilename));

    if (g_iLogLevel > 0)                 // --pe --name wintypes.dll --image-size 1388544 --timestamp 0x57F8D9F0
        RTMsgInfo("Copying '%s' to '%s...", pszFilename, pszOutput);
    int rc = RTFileCopy(pszFilename, pszOutput);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_ALREADY_EXISTS)
        {
            rc = RTFileCompare(pszFilename, pszOutput);
            if (RT_SUCCESS(rc))
                RTMsgInfo("Output '%s' exists and matches '%s'.", pszOutput, pszFilename);
            else
                RTMsgError("Output '%s' already exists (does not match '%s')", pszOutput, pszFilename);
        }
        else
            RTMsgError("Copying '%s' to '%s failed: %Rrc", pszFilename, pszOutput, rc);
    }
    RTStrFree(pszJoined);
    if (RT_SUCCESS(rc))
        return VINF_CALLBACK_RETURN;
    return rc;
}


/**
 * Handles the 'get' command.
 *
 * @returns Program exit code.
 * @param   pszArg0             The program name.
 * @param   cArgs               The number of arguments to the 'add' command.
 * @param   papszArgs           The argument vector, starting after 'add'.
 */
static RTEXITCODE rtDbgSymCacheCmdGet(const char *pszArg0, int cArgs, char **papszArgs)
{
    RTERRINFOSTATIC ErrInfo;

    /*
     * Parse the command line.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--output",                   'o', RTGETOPT_REQ_STRING },

        /* Query: */
        { "--for-exe",                  'e', RTGETOPT_REQ_STRING },
        { "--for-executable",           'e', RTGETOPT_REQ_STRING },
        { "--uuid",                     'u', RTGETOPT_REQ_UUID },
        { "--ts",                       't', RTGETOPT_REQ_UINT32 },
        { "--timestamp",                't', RTGETOPT_REQ_UINT32 },
        { "--size",                     'z', RTGETOPT_REQ_UINT32 },
        { "--image-size",               'z', RTGETOPT_REQ_UINT32 },
        { "--pdb-age",                  'a', RTGETOPT_REQ_UINT32 },
        { "--dwo-crc32",                'c', RTGETOPT_REQ_UINT32 },
        { "--name",                     'n', RTGETOPT_REQ_STRING },

        { "--dwo",                      'd', RTGETOPT_REQ_NOTHING },
        { "--dwarf",                    'd', RTGETOPT_REQ_NOTHING },
        { "--dwarf-external",           'd', RTGETOPT_REQ_NOTHING },
        { "--dsym",                     'D', RTGETOPT_REQ_NOTHING },
        { "--dbg",                      '0', RTGETOPT_REQ_NOTHING },
        { "--pdb20",                    '2', RTGETOPT_REQ_NOTHING },
        { "--pdb70",                    '7', RTGETOPT_REQ_NOTHING },

        { "--pe",                       'P', RTGETOPT_REQ_NOTHING },
        { "--macho",                    'M', RTGETOPT_REQ_NOTHING },
        { "--elf",                      'E', RTGETOPT_REQ_NOTHING },

        /* RTDbgCfg: */
        { "--env-prefix",               'p', RTGETOPT_REQ_STRING },
        { "--sym-path",                 's', RTGETOPT_REQ_STRING },
        { "--use-native-paths",          1000, RTGETOPT_REQ_NOTHING },
        { "--no-native-paths",           1001, RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 0,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    const char     *pszOutput               = NULL;

    bool            fGetExeImage            = true;
    const char     *pszForExec              = NULL;
    const char     *pszName                 = NULL;
    RTLDRARCH       enmImageArch            = RTLDRARCH_WHATEVER;
    RTLDRFMT        enmImageFmt             = RTLDRFMT_INVALID;
    MYDBGINFO       DbgInfo;
    RT_ZERO(DbgInfo);

    const char     *pszEnvPrefix            = "IPRT_";
    bool            fNativePaths            = true;
    const char     *apszSymPaths[12]        = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    unsigned        cSymPaths               = 0;

    RTGETOPTUNION   ValueUnion;
    int             chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'o':
                pszOutput = ValueUnion.psz;
                break;

            /*
             * Query elements:
             */
            case 'z':
                DbgInfo.cbImage = ValueUnion.u32;
                break;

            case 't':
                DbgInfo.uTimestamp = ValueUnion.u32;
                enmImageFmt        = RTLDRFMT_PE;
                break;

            case 'u':
                DbgInfo.Uuid = ValueUnion.Uuid;
                enmImageFmt  = RTLDRFMT_MACHO;
                break;

            case 'a':
                DbgInfo.uPdbAge = ValueUnion.u32;
                if (DbgInfo.enmType != RTLDRDBGINFOTYPE_CODEVIEW_PDB20)
                    DbgInfo.enmType = RTLDRDBGINFOTYPE_CODEVIEW_PDB70;
                break;

            case 'c':
                DbgInfo.uDwoCrc32 = ValueUnion.u32;
                DbgInfo.enmType   = RTLDRDBGINFOTYPE_DWARF_DWO;
                break;

            case 'n':
                pszName = ValueUnion.psz;
                DbgInfo.szExtFile[0] = '\0';
                break;

            case 'd':
                fGetExeImage    = false;
                DbgInfo.enmType = RTLDRDBGINFOTYPE_DWARF_DWO;
                break;

            case 'D':
                fGetExeImage    = false;
                DbgInfo.enmType = RTLDRDBGINFOTYPE_DWARF; /* == dSYM */
                break;

            case '0':
                fGetExeImage    = false;
                DbgInfo.enmType = RTLDRDBGINFOTYPE_CODEVIEW_DBG;
                break;

            case '2':
                fGetExeImage    = false;
                DbgInfo.enmType = RTLDRDBGINFOTYPE_CODEVIEW_PDB20;
                break;

            case '7':
                fGetExeImage    = false;
                DbgInfo.enmType = RTLDRDBGINFOTYPE_CODEVIEW_PDB70;
                break;

            case 'E':
                fGetExeImage = true;
                enmImageFmt  = RTLDRFMT_ELF;
                break;

            case 'M':
                fGetExeImage = true;
                enmImageFmt  = RTLDRFMT_MACHO;
                break;

            case 'P':
                fGetExeImage = true;
                enmImageFmt  = RTLDRFMT_PE;
                break;

            case 'e':
            {
                /* Open the executable and retrieve the query parameters from it: */
                fGetExeImage = false;
                pszForExec = ValueUnion.psz;
                if (!pszName)
                    pszName = RTPathFilename(pszForExec);

                RTLDRMOD hLdrMod;
                rc = RTLdrOpenEx(pszForExec, RTLDR_O_FOR_DEBUG, enmImageArch, &hLdrMod, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("Failed to open image '%s': %Rrc%#RTeim", pszForExec, rc, &ErrInfo);

                DbgInfo.cbImage = (uint32_t)RTLdrSize(hLdrMod);
                enmImageFmt = RTLdrGetFormat(hLdrMod);
                if (enmImageFmt == RTLDRFMT_MACHO)
                {
                    DbgInfo.enmType = RTLDRDBGINFOTYPE_DWARF; /* .dSYM */
                    rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_UUID, &DbgInfo.Uuid, sizeof(DbgInfo.Uuid));
                    if (RT_FAILURE(rc))
                        RTMsgError("Failed to query image UUID from '%s': %Rrc", pszForExec, rc);
                }
                else
                {
                    rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &DbgInfo.uTimestamp, sizeof(DbgInfo.uTimestamp));
                    if (RT_SUCCESS(rc) || (rc == VERR_NOT_FOUND && enmImageFmt != RTLDRFMT_PE))
                    {
                        RT_ZERO(DbgInfo);
                        rc = RTLdrEnumDbgInfo(hLdrMod, NULL, rtDbgSymCacheCmdGetForExeDbgInfoCallback, &DbgInfo);
                        if (RT_FAILURE(rc))
                            RTMsgError("RTLdrEnumDbgInfo failed on '%s': %Rrc", pszForExec, rc);
                    }
                    else if (RT_FAILURE(rc))
                        RTMsgError("Failed to query image timestamp from '%s': %Rrc", pszForExec, rc);
                }

                RTLdrClose(hLdrMod);
                if (RT_FAILURE(rc))
                    return RTEXITCODE_FAILURE;
                break;
            }

            /*
             * RTDbgCfg setup:
             */
            case 'p':
                pszEnvPrefix = ValueUnion.psz;
                break;

            case 's':
                if (cSymPaths < RT_ELEMENTS(apszSymPaths))
                    apszSymPaths[cSymPaths++] = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --sym-paths arguments: max %u", RT_ELEMENTS(apszSymPaths));
                break;

            case 1000:
                fNativePaths = true;
                break;

            case 1001:
                fNativePaths = false;
                break;

            case 'h':
                return rtDbgSymCacheUsage(pszArg0, "get");
            case 'V':
                return rtDbgSymCacheVersion();
            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    /*
     * Instantiate the debug config we'll be querying.
     */
    RTDBGCFG hDbgCfg;
    rc = RTDbgCfgCreate(&hDbgCfg, pszEnvPrefix, fNativePaths);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDbgCfgCreate failed: %Rrc", rc);

    rc = RTDbgCfgSetLogCallback(hDbgCfg, rtDbgSymCacheLogCallback, NULL);
    AssertRCStmt(rc, RTMsgError("RTDbgCfgSetLogCallback failed: %Rrc", rc));

    for (unsigned i = 0; i < cSymPaths && RT_SUCCESS(rc); i++)
    {
        rc = RTDbgCfgChangeString(hDbgCfg, RTDBGCFGPROP_PATH, RTDBGCFGOP_APPEND, apszSymPaths[i]);
        if (RT_FAILURE(rc))
            RTMsgError("Failed to append symbol path '%s': %Rrc", apszSymPaths[i], rc);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Do the getting.
         */
        if (fGetExeImage)
        {
            if (enmImageFmt == RTLDRFMT_INVALID)
            {
                if (!RTUuidIsNull(&DbgInfo.Uuid))
                    enmImageFmt = RTLDRFMT_MACHO;
                else if (DbgInfo.cbImage && DbgInfo.uTimestamp)
                    enmImageFmt = RTLDRFMT_PE;
                else
                    rc = RTMsgErrorRc(VERR_NOT_IMPLEMENTED, "Not enough to go on to find executable");
            }
            if (enmImageFmt == RTLDRFMT_PE)
                rc = RTDbgCfgOpenPeImage(hDbgCfg, pszName, DbgInfo.cbImage, DbgInfo.uTimestamp,
                                         rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
            else if (enmImageFmt == RTLDRFMT_MACHO)
                rc = RTDbgCfgOpenMachOImage(hDbgCfg, pszName, &DbgInfo.Uuid,
                                            rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
            else if (enmImageFmt != RTLDRFMT_INVALID)
                rc = RTMsgErrorRc(VERR_NOT_IMPLEMENTED, "Format not implemented: %s", RTLdrGetFormat);
        }
        else if (DbgInfo.enmType == RTLDRDBGINFOTYPE_CODEVIEW_PDB70)
            rc = RTDbgCfgOpenPdb70(hDbgCfg, DbgInfo.szExtFile[0] ? DbgInfo.szExtFile : pszName, &DbgInfo.Uuid, DbgInfo.uPdbAge,
                                   rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
        else if (DbgInfo.enmType == RTLDRDBGINFOTYPE_CODEVIEW_PDB20)
            rc = RTDbgCfgOpenPdb20(hDbgCfg, DbgInfo.szExtFile[0] ? DbgInfo.szExtFile : pszName, DbgInfo.cbImage,
                                   DbgInfo.uTimestamp, DbgInfo.uPdbAge, rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
        else if (DbgInfo.enmType == RTLDRDBGINFOTYPE_CODEVIEW_DBG)
            rc = RTDbgCfgOpenDbg(hDbgCfg, DbgInfo.szExtFile[0] ? DbgInfo.szExtFile : pszName, DbgInfo.cbImage,
                                 DbgInfo.uTimestamp, rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
        else if (DbgInfo.enmType == RTLDRDBGINFOTYPE_DWARF_DWO)
            rc = RTDbgCfgOpenDwo(hDbgCfg, DbgInfo.szExtFile[0] ? DbgInfo.szExtFile : pszName, DbgInfo.uDwoCrc32,
                                 rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
        else if (DbgInfo.enmType == RTLDRDBGINFOTYPE_DWARF)
            rc = RTDbgCfgOpenDsymBundle(hDbgCfg, DbgInfo.szExtFile[0] ? DbgInfo.szExtFile : pszName, &DbgInfo.Uuid,
                                        rtDbgSymCacheCmdGetCallback, (void *)pszOutput, NULL);
        else
            rc = RTMsgErrorRc(VERR_NOT_IMPLEMENTED, "Format not implemented");
    }

    RTDbgCfgRelease(hDbgCfg);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Switch on the command.
     */
    RTEXITCODE rcExit = RTEXITCODE_SYNTAX;
    if (argc < 2)
        rtDbgSymCacheUsage(argv[0], NULL);
    else if (!strcmp(argv[1], "add"))
        rcExit = rtDbgSymCacheCmdAdd(argv[0], argc - 2, argv + 2);
    else if (!strcmp(argv[1], "get"))
        rcExit = rtDbgSymCacheCmdGet(argv[0], argc - 2, argv + 2);
    else if (   !strcmp(argv[1], "-h")
             || !strcmp(argv[1], "-?")
             || !strcmp(argv[1], "--help"))
        rcExit = rtDbgSymCacheUsage(argv[0], NULL);
    else if (   !strcmp(argv[1], "-V")
             || !strcmp(argv[1], "--version"))
        rcExit = rtDbgSymCacheVersion();
    else
        RTMsgError("Unknown command: '%s'", argv[1]);

    return rcExit;
}

