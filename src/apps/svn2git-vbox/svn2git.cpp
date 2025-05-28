/* $Id: svn2git.cpp 169030 2025-05-28 11:29:24Z alexander.eichner@oracle.com $ */
/** @file
 * svn2git - Convert a svn repository to git.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/json.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/time.h>

#include <apr_lib.h>
#include <apr_getopt.h>
#include <apr_general.h>

#include <svn_fs.h>
#include <svn_pools.h>
#include <svn_repos.h>
#include <svn_types.h>
#include <svn_version.h>
#include <svn_subst.h>
#include <svn_props.h>
#include <svn_time.h>

#include "svn2git-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Change entry.
 */
typedef struct S2GSVNREVCHANGE
{
    RTLISTNODE            NdChanges;
    const char            *pszPath;
    svn_fs_path_change2_t *pChange;
} S2GSVNREVCHANGE;
typedef S2GSVNREVCHANGE *PS2GSVNREVCHANGE;
typedef const S2GSVNREVCHANGE *PCS2GSVNREVCHANGE;


/**
 * Author entry.
 */
typedef struct S2GAUTHOR
{
    /** String space core, the subversion author is the key. */
    RTSTRSPACECORE  Core;
    /** The matching git author. */
    const char      *pszGitAuthor;
    /** The E-Mail to use for git commits. */
    const char      *pszGitEmail;
    /** The allocated string buffer for everything. */
    RT_GCC_EXTENSION
    char            achStr[RT_FLEXIBLE_ARRAY];
} S2GAUTHOR;
typedef S2GAUTHOR *PS2GAUTHOR;
typedef const S2GAUTHOR *PCS2GAUTHOR;


/**
 * Externals revision to git commit hash map.
 */
typedef struct S2GEXTREVMAP
{
    /** List node. */
    RTLISTNODE      NdExternals;
    /** Name of the external  */
    const char      *pszName;
    /** Number of entries in the map sorted by revision. */
    uint32_t        cEntries;
    /** Pointer to the array to commit ID hash strings, indexed by revision. */
    const char      **papszRev2CommitHash;
    /** The string table for the git commnit hashes. */
    RT_GCC_EXTENSION
    char            achStr[RT_FLEXIBLE_ARRAY];
} S2GEXTREVMAP;
typedef S2GEXTREVMAP *PS2GEXTREVMAP;
typedef const S2GEXTREVMAP *PCS2GEXTREVMAP;


/**
 * The state for a single revision.
 */
typedef struct S2GSVNREV
{
    /** The pool for this revision. */
    apr_pool_t      *pPoolRev;
    /** The SVN revision root. */
    svn_fs_root_t   *pSvnFsRoot;

    /** The revision number. */
    uint32_t        idRev;
    /** The Unix epoch in seconds of the svn commit. */
    int64_t         cEpochSecs;
    /** The APR time of the commit (for keyword substitution). */
    apr_time_t      AprTime;
    /** The svn author. */
    const char      *pszSvnAuthor;
    /** The commit message. */
    const char      *pszSvnLog;
    /** The sync-xref-src-repo-rev. */
    const char      *pszSvnXref;

    /** The Git author's E-Mail. */
    const char      *pszGitAuthorEmail;
    /** The Git author's name. */
    const char      *pszGitAuthor;

    /** List of changes in that revision, sorted by the path. */
    RTLISTANCHOR    LstChanges;

} S2GSVNREV;
/** Pointer to the SVN revision state. */
typedef S2GSVNREV *PS2GSVNREV;
/** Pointer to a const SVN revision state. */
typedef const S2GSVNREV *PCS2GSVNREV;


/**
 * The svn -> git conversion context.
 */
typedef struct S2GCTX
{
    /** The start revision number. */
    uint32_t        idRevStart;
    /** The end revision number. */
    uint32_t        idRevEnd;
    /** The path to the JSON config file. */
    const char      *pszCfgFilename;
    /** The input subversion repository path. */
    const char      *pszSvnRepo;
    /** Dump filename, optional. */
    const char      *pszDumpFilename;

    /** The git repository path. */
    char            *pszGitRepoPath;
    /** The default git branch. */
    const char      *pszGitDefBranch;

    /** @name Subversion related members.
     * @{ */
    /** The default pool. */
    apr_pool_t      *pPoolDefault;
    /** The scratch pool. */
    apr_pool_t      *pPoolScratch;
    /** The repository handle. */
    svn_repos_t     *pSvnRepos;
    /** The filesystem layer handle for the repository. */
    svn_fs_t        *pSvnFs;
    /** @} */

    /** @name Git repository related members.
     * @{ */
    /** The destination git repository. */
    S2GREPOSITORYGIT hGitRepo;
    /** @} */

    /** SVN -> Git author information string space. */
    RTSTRSPACE       StrSpaceAuthors;
    /** Scratch buffer. */
    S2GSCRATCHBUF    BufScratch;
    /** List of known externals with their revision to git commit hash map. */
    RTLISTANCHOR     LstExternals;
} S2GCTX;
/** Pointer to an svn -> git conversion context. */
typedef S2GCTX *PS2GCTX;
/** Pointer to a const svn -> git conversion context. */
typedef const S2GCTX *PCS2GCTX;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity level. */
static int        g_cVerbosity = 0;



/**
 * Display usage
 *
 * @returns success if stdout, syntax error if stderr.
 */
static RTEXITCODE s2gUsage(const char *argv0)
{
    RTMsgInfo("usage: %s --config <config file> [options and operations] <input subversion repository>\n"
              "\n"
              "Operations and Options (processed in place):\n"
              "  --verbose                                Noisier.\n"
              "  --quiet                                  Quiet execution.\n"
              "  --rev-start <revision>                   The revision to start conversion at\n"
              "  --rev-end   <revision>                   The last revision to convert (default is last repository revision)\n"
              "  --dump-file <file path>                  File to dump the fast-import stream to\n"
              , argv0);
    return RTEXITCODE_SUCCESS;
}


/**
 * Initializes the given context to default values.
 */
static void s2gCtxInit(PS2GCTX pThis)
{
    pThis->idRevStart      = UINT32_MAX;
    pThis->idRevEnd        = UINT32_MAX;
    pThis->pszCfgFilename  = NULL;
    pThis->pszSvnRepo      = NULL;
    pThis->pszGitRepoPath  = NULL;
    pThis->pszGitDefBranch = "main";
    pThis->pszDumpFilename = NULL;

    pThis->pPoolDefault    = NULL;
    pThis->pPoolScratch    = NULL;
    pThis->pSvnRepos       = NULL;
    pThis->pSvnFs          = NULL;

    pThis->StrSpaceAuthors = NULL;
    s2gScratchBufInit(&pThis->BufScratch);
    RTListInit(&pThis->LstExternals);
}


static void s2gCtxDestroy(PS2GCTX pThis)
{
    if (pThis->pPoolScratch)
    {
        svn_pool_destroy(pThis->pPoolScratch);
        pThis->pPoolScratch = NULL;
    }

    if (pThis->pPoolDefault)
    {
        svn_pool_destroy(pThis->pPoolDefault);
        pThis->pPoolDefault = NULL;
    }

    s2gScratchBufFree(&pThis->BufScratch);
}


/**
 * Parses the arguments.
 */
static RTEXITCODE s2gParseArguments(PS2GCTX pThis, int argc, char **argv)
{
    /*
     * Option config.
     */
    static RTGETOPTDEF const s_aOpts[] =
    {
        { "--config",                           'c', RTGETOPT_REQ_STRING  },
        { "--verbose",                          'v', RTGETOPT_REQ_NOTHING },
        { "--rev-start",                        's', RTGETOPT_REQ_UINT32  },
        { "--rev-end",                          'e', RTGETOPT_REQ_UINT32  },
        { "--dump-file",                        'd', RTGETOPT_REQ_STRING  },
    };

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetOptState;
    int rc = RTGetOptInit(&GetOptState, argc, argv, &s_aOpts[0], RT_ELEMENTS(s_aOpts), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertReleaseRCReturn(rc, RTEXITCODE_FAILURE);

    /*
     * Process the options.
     */
    while ((rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'h':
                return s2gUsage(argv[0]);

            case 'c':
                if (pThis->pszCfgFilename)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Config file is already set to '%s'", pThis->pszCfgFilename);
                pThis->pszCfgFilename = ValueUnion.psz;
                break;

            case 'v':
                g_cVerbosity++;
                break;

            case 's':
                pThis->idRevStart = ValueUnion.u32;
                break;

            case 'e':
                pThis->idRevEnd = ValueUnion.u32;
                break;

            case 'd':
                pThis->pszDumpFilename = ValueUnion.psz;
                break;

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision: 169030 $";
                const char *psz = RTStrStripL(strchr(s_szRev, ' '));
                RTMsgInfo("r%.*s\n", strchr(psz, ' ') - psz, psz);
                return RTEXITCODE_SUCCESS;
            }

            case VINF_GETOPT_NOT_OPTION:
                if (pThis->pszSvnRepo)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Subversion path is already set to '%s'", pThis->pszSvnRepo);
                pThis->pszSvnRepo = ValueUnion.psz;
                break;

            /*
             * Errors and bugs.
             */
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Check that we've got all we need.
     */
    if (!pThis->pszCfgFilename)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --config <filename> argument");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE s2gLoadConfigAuthor(PS2GCTX pThis, RTJSONVAL hAuthor)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTJSONVAL hValSvnAuthor = NIL_RTJSONVAL;
    int rc = RTJsonValueQueryByName(hAuthor, "svn", &hValSvnAuthor);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hValGitAuthor = NIL_RTJSONVAL;
        rc = RTJsonValueQueryByName(hAuthor, "git", &hValGitAuthor);
        if (RT_SUCCESS(rc))
        {
            RTJSONVAL hValGitEmail = NIL_RTJSONVAL;
            rc = RTJsonValueQueryByName(hAuthor, "email", &hValGitEmail);
            if (RT_SUCCESS(rc))
            {
                const char *pszSvnAuthor = RTJsonValueGetString(hValSvnAuthor);
                const char *pszGitAuthor = RTJsonValueGetString(hValGitAuthor);
                const char *pszGitEmail  = RTJsonValueGetString(hValGitEmail);

                if (pszSvnAuthor && pszGitAuthor && pszGitEmail)
                {
                    size_t cchSvnAuthor = strlen(pszSvnAuthor);
                    size_t cchGitAuthor = strlen(pszGitAuthor);
                    size_t cchGitEmail  = strlen(pszGitEmail);
                    size_t cbAuthor = RT_UOFFSETOF_DYN(S2GAUTHOR, achStr[cchSvnAuthor + cchGitAuthor + cchGitEmail + 3]);
                    PS2GAUTHOR pAuthor = (PS2GAUTHOR)RTMemAllocZ(cbAuthor);
                    if (pAuthor)
                    {
                        memcpy(&pAuthor->achStr[0], pszSvnAuthor, cchSvnAuthor);
                        pAuthor->achStr[cchSvnAuthor] = '\0';
                        pAuthor->Core.pszString = &pAuthor->achStr[0];
                        pAuthor->Core.cchString = cchSvnAuthor;

                        pAuthor->pszGitAuthor = &pAuthor->achStr[cchSvnAuthor + 1];
                        memcpy(&pAuthor->achStr[cchSvnAuthor + 1], pszGitAuthor, cchGitAuthor);
                        pAuthor->achStr[cchSvnAuthor + 1 + cchGitAuthor] = '\0';

                        pAuthor->pszGitEmail = &pAuthor->pszGitAuthor[cchGitAuthor + 1];
                        memcpy(&pAuthor->achStr[cchSvnAuthor + 1 + cchGitAuthor + 1], pszGitEmail, cchGitEmail);
                        pAuthor->achStr[cchSvnAuthor + 1 + cchGitAuthor + 1 + cchGitEmail] = '\0';

                        if (!RTStrSpaceInsert(&pThis->StrSpaceAuthors, &pAuthor->Core))
                        {
                            RTMemFree(pAuthor);
                            rcExit  = RTMsgErrorExit(RTEXITCODE_FAILURE, "Duplicate author '%s'", pszSvnAuthor);
                        }
                        else if (g_cVerbosity >= 3)
                            RTMsgInfo(" Author map: %s %s %s\n", pAuthor->Core.pszString, pAuthor->pszGitAuthor, pAuthor->pszGitEmail);
                    }
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for author '%s'", cbAuthor, pszSvnAuthor);
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "The author object is malformed");

                RTJsonValueRelease(hValGitEmail);
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'email' from author object: %Rrc", rc);

            RTJsonValueRelease(hValGitAuthor);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'git' from author object: %Rrc", rc);

        RTJsonValueRelease(hValSvnAuthor);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'svn' from author object: %Rrc", rc);

    return rcExit;
}


static RTEXITCODE s2gLoadConfigAuthorMap(PS2GCTX pThis, RTJSONVAL hJsonValAuthorMap)
{
    if (RTJsonValueGetType(hJsonValAuthorMap) != RTJSONVALTYPE_ARRAY)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "'AuthorMap' in '%s' is not a JSON array", pThis->pszCfgFilename);

    RTJSONIT hIt = NIL_RTJSONIT;
    int rc = RTJsonIteratorBeginArray(hJsonValAuthorMap, &hIt);
    if (rc == VERR_JSON_IS_EMPTY) /* Weird but okay. */
        return RTEXITCODE_SUCCESS;
    else if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over author map: %Rrc", rc);

    AssertRC(rc);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (;;)
    {
        RTJSONVAL hAuthor = NIL_RTJSONVAL;
        rc = RTJsonIteratorQueryValue(hIt, &hAuthor,  NULL /*ppszName*/);
        if (RT_FAILURE(rc))
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over author map: %Rrc", rc);
            break;
        }

        rcExit = s2gLoadConfigAuthor(pThis, hAuthor);
        RTJsonValueRelease(hAuthor);
        if (rcExit == RTEXITCODE_FAILURE)
            break;

        rc = RTJsonIteratorNext(hIt);
        if (rc == VERR_JSON_ITERATOR_END)
            break;
        else if (RT_FAILURE(rc))
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over author map: %Rrc", rc);
            break;
        }
    }

    RTJsonIteratorFree(hIt);
    return rcExit;
}


static RTEXITCODE s2gLoadConfigExternalMap(PS2GCTX pThis, const char *pszName, const char *pszFilename)
{
    RTJSONVAL hRoot = NIL_RTJSONVAL;
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    int rc = RTJsonParseFromFile(&hRoot, RTJSON_PARSE_F_JSON5, pszFilename, &ErrInfo.Core);
    if (RT_SUCCESS(rc))
    {
        RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

        if (RTJsonValueGetType(hRoot) == RTJSONVALTYPE_OBJECT)
        {
            uint32_t cItems = RTJsonValueGetObjectMemberCount(hRoot);

            size_t cbName = (strlen(pszName) + 1) * sizeof(char);
            size_t cbExternal = RT_UOFFSETOF_DYN(S2GEXTREVMAP, achStr[  cbName
                                                                      + cItems * (RTSHA1_DIGEST_LEN + 1) * sizeof(char)]);
            PS2GEXTREVMAP pExternal = (PS2GEXTREVMAP)RTMemAllocZ(cbExternal);
            if (pExternal)
            {
                size_t offStr = cbName;
                memcpy(&pExternal->achStr[0], pszName, cbName);
                pExternal->pszName = &pExternal->achStr[0];

                pExternal->cEntries = 0;

                RTJSONIT hIt = NIL_RTJSONIT;
                rc = RTJsonIteratorBeginObject(hRoot, &hIt);
                if (RT_SUCCESS(rc))
                {
                    for (;;)
                    {
                        const char *pszShaCommitHash = NULL;
                        RTJSONVAL hRevision = NIL_RTJSONVAL;
                        rc = RTJsonIteratorQueryValue(hIt, &hRevision,  &pszShaCommitHash);
                        if (RT_FAILURE(rc))
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over revision map: %Rrc", rc);
                            break;
                        }

                        int64_t i64RevNum = 0;
                        rc = RTJsonValueQueryInteger(hRevision, &i64RevNum);
                        if (RT_FAILURE(rc))
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Revision for '%s' is not a number", pszShaCommitHash);
                            RTJsonValueRelease(hRevision);
                            break;
                        }
                        RTJsonValueRelease(hRevision);

                        if (strlen(pszShaCommitHash) != RTSHA1_DIGEST_LEN)
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Commit hash '%s' is malformed", pszShaCommitHash);
                            break;
                        }

                        if (i64RevNum < 0)
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Revision %RI64 for '%s' is negative",
                                                    i64RevNum, pszShaCommitHash);
                            break;
                        }

                        if (i64RevNum >= pExternal->cEntries)
                        {
                            const char **papszNew = (const char **)RTMemReallocZ(pExternal->papszRev2CommitHash,
                                                                                 pExternal->cEntries * sizeof(const char **),
                                                                                 (i64RevNum + 1) * sizeof(const char **));
                            if (!papszNew)
                            {
                                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate memory for the revision map");
                                break;
                            }

                            pExternal->papszRev2CommitHash = papszNew;
                            pExternal->cEntries            = i64RevNum + 1;
                        }

                        if (pExternal->papszRev2CommitHash[i64RevNum] != NULL)
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Revision %RI64 for '%s' is already used",
                                                    i64RevNum, pszShaCommitHash);
                            break;
                        }

                        pExternal->papszRev2CommitHash[i64RevNum] = &pExternal->achStr[offStr];
                        memcpy(&pExternal->achStr[offStr], pszShaCommitHash, RTSHA1_DIGEST_LEN + 1);
                        offStr += RTSHA1_DIGEST_LEN + 1;

                        rc = RTJsonIteratorNext(hIt);
                        if (rc == VERR_JSON_ITERATOR_END)
                            break;
                        else if (RT_FAILURE(rc))
                        {
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over revision map: %Rrc", rc);
                            break;
                        }
                    }
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over revision map: %Rrc", rc);

                if (rcExit == RTEXITCODE_SUCCESS)
                    RTListAppend(&pThis->LstExternals, &pExternal->NdExternals);
                else
                {
                    if (pExternal->papszRev2CommitHash)
                        RTMemFree(pExternal->papszRev2CommitHash);
                    RTMemFree(pExternal);
                }
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for the external '%s'",
                                        cbExternal, pszFilename);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "External map '%s' is not a JSON object", pszFilename);

        RTJsonValueRelease(hRoot);
        return rcExit;
    }

    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to load external map file '%s': %s", pszFilename, ErrInfo.Core.pszMsg);

}


static RTEXITCODE s2gLoadConfigExternal(PS2GCTX pThis, RTJSONVAL hExternal)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTJSONVAL hValName = NIL_RTJSONVAL;
    int rc = RTJsonValueQueryByName(hExternal, "name", &hValName);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hValFile = NIL_RTJSONVAL;
        rc = RTJsonValueQueryByName(hExternal, "file", &hValFile);
        if (RT_SUCCESS(rc))
        {
            const char *pszName     = RTJsonValueGetString(hValName);
            const char *pszFilename = RTJsonValueGetString(hValFile);
            if (pszName && pszFilename)
                rcExit = s2gLoadConfigExternalMap(pThis, pszName, pszFilename);
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "The external object is malformed");

            RTJsonValueRelease(hValFile);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'file' from external object: %Rrc", rc);

        RTJsonValueRelease(hValName);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'name' from external object: %Rrc", rc);

    return rcExit;
}


static RTEXITCODE s2gLoadConfigExternals(PS2GCTX pThis, RTJSONVAL hJsonValExternals)
{
    if (RTJsonValueGetType(hJsonValExternals) != RTJSONVALTYPE_ARRAY)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "'ExternalsMap' in '%s' is not a JSON array", pThis->pszCfgFilename);

    RTJSONIT hIt = NIL_RTJSONIT;
    int rc = RTJsonIteratorBeginArray(hJsonValExternals, &hIt);
    if (rc == VERR_JSON_IS_EMPTY) /* Weird but okay. */
        return RTEXITCODE_SUCCESS;
    else if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over externals map: %Rrc", rc);

    AssertRC(rc);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (;;)
    {
        RTJSONVAL hExternal = NIL_RTJSONVAL;
        rc = RTJsonIteratorQueryValue(hIt, &hExternal,  NULL /*ppszName*/);
        if (RT_FAILURE(rc))
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over externals map: %Rrc", rc);
            break;
        }

        rcExit = s2gLoadConfigExternal(pThis, hExternal);
        RTJsonValueRelease(hExternal);
        if (rcExit == RTEXITCODE_FAILURE)
            break;

        rc = RTJsonIteratorNext(hIt);
        if (rc == VERR_JSON_ITERATOR_END)
            break;
        else if (RT_FAILURE(rc))
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to iterate over externals map: %Rrc", rc);
            break;
        }
    }

    RTJsonIteratorFree(hIt);
    return rcExit;
}


static RTEXITCODE s2gLoadConfig(PS2GCTX pThis)
{
    RTJSONVAL hRoot = NIL_RTJSONVAL;
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    int rc = RTJsonParseFromFile(&hRoot, RTJSON_PARSE_F_JSON5, pThis->pszCfgFilename, &ErrInfo.Core);
    if (RT_SUCCESS(rc))
    {
        RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

        rc = RTJsonValueQueryStringByName(hRoot, "GitRepoPath", &pThis->pszGitRepoPath);
        if (RT_SUCCESS(rc))
        {
            RTJSONVAL hAuthorMap = NIL_RTJSONVAL;
            rc = RTJsonValueQueryByName(hRoot, "AuthorMap", &hAuthorMap);
            if (RT_SUCCESS(rc))
            {
                rcExit = s2gLoadConfigAuthorMap(pThis, hAuthorMap);
                RTJsonValueRelease(hAuthorMap);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    RTJSONVAL hExternalsMap = NIL_RTJSONVAL;
                    rc = RTJsonValueQueryByName(hRoot, "ExternalsMap", &hExternalsMap);
                    if (RT_SUCCESS(rc))
                    {
                        rcExit = s2gLoadConfigExternals(pThis, hExternalsMap);
                        RTJsonValueRelease(hExternalsMap);
                    }
                    else if (rc != VERR_NOT_FOUND)
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query ExternalsMap from '%s': %Rrc", pThis->pszCfgFilename, rc);
                }
            }
            else if (rc != VERR_NOT_FOUND)
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query AuthorMap from '%s': %Rrc", pThis->pszCfgFilename, rc);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query GitRepoPath from '%s': %Rrc", pThis->pszCfgFilename, rc);

        RTJsonValueRelease(hRoot);
        return rcExit;
    }

    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to load config file '%s': %s", pThis->pszCfgFilename, ErrInfo.Core.pszMsg);
}


static RTEXITCODE s2gSvnInit(PS2GCTX pThis)
{
    /* Initialize APR first. */
    if (apr_initialize() != APR_SUCCESS)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "apr_initialize() failed\n");

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    pThis->pPoolDefault = svn_pool_create(NULL);
    if (pThis->pPoolDefault)
    {
        pThis->pPoolScratch = svn_pool_create(NULL);
        if (pThis->pPoolScratch)
        {
            svn_error_t *pSvnErr = svn_repos_open3(&pThis->pSvnRepos, pThis->pszSvnRepo, NULL, pThis->pPoolDefault, pThis->pPoolScratch);
            if (!pSvnErr)
            {
                pThis->pSvnFs = svn_repos_fs(pThis->pSvnRepos);
                if (pThis->idRevEnd == UINT32_MAX)
                {
                    svn_revnum_t idRevYoungest;
                    svn_fs_youngest_rev(&idRevYoungest, pThis->pSvnFs, pThis->pPoolDefault);
                    pThis->idRevEnd = idRevYoungest;
                }
            }
            else
            {
                svn_error_trace(pSvnErr);
                rcExit = RTEXITCODE_FAILURE;
            }
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create scratch APR pool\n");
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create default APR pool\n");

    return rcExit;
}


static const char *s2gSvnChangeKindToStr(svn_fs_path_change_kind_t enmKind)
{
    switch (enmKind)
    {
        case svn_fs_path_change_modify:
            return "Modified";
        case svn_fs_path_change_add:
            return "Added";
        case svn_fs_path_change_delete:
            return "Deleted";
        case svn_fs_path_change_replace:
            return "Replaced";
        case svn_fs_path_change_reset:
            return "Resetted";
    }

    AssertFailed();
    return "<UNKNOWN>";
}


static bool s2gPathIsExec(PCS2GSVNREV pRev, const char *pszSvnPath, apr_pool_t *pSvnPool)
{
    svn_string_t *pPropVal = NULL;
    svn_error_t *pSvnErr = svn_fs_node_prop(&pPropVal, pRev->pSvnFsRoot, pszSvnPath, "svn:executable", pSvnPool);
    if (pSvnErr)
        svn_error_trace(pSvnErr);

    return pPropVal != NULL;
}


static bool s2gPathIsSymlink(PCS2GSVNREV pRev, const char *pszSvnPath, apr_pool_t *pSvnPool)
{
    svn_string_t *pPropVal = NULL;
    svn_error_t *pSvnErr = svn_fs_node_prop(&pPropVal, pRev->pSvnFsRoot, pszSvnPath, "svn:special", pSvnPool);
    if (pSvnErr)
        svn_error_trace(pSvnErr);

    return pPropVal != NULL;
}


static RTEXITCODE s2gSvnDumpBlob(PS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath, const char *pszGitPath)
{
    /* Create a new temporary pool. */
    apr_pool_t *pPool = svn_pool_create(pRev->pPoolRev);
    if (!pPool)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Allocating pool trying to dump '%s' failed", pszSvnPath);

    bool fIsExec = s2gPathIsExec(pRev, pszSvnPath, pPool);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /** @todo Symlinks. */
    if (!s2gPathIsSymlink(pRev, pszSvnPath, pPool))
    {
        svn_stream_t *pSvnStrmIn = NULL;
        svn_error_t *pSvnErr = svn_fs_file_contents(&pSvnStrmIn, pRev->pSvnFsRoot, pszSvnPath, pPool);
        if (!pSvnErr)
        {
            /* Do EOL style conversions and keyword substitutions. */
            apr_hash_t *pProps = NULL;
            pSvnErr = svn_fs_node_proplist(&pProps, pRev->pSvnFsRoot, pszSvnPath, pPool);
            if (!pSvnErr)
            {
                svn_string_t *pSvnStrEolStyle = (svn_string_t *)apr_hash_get(pProps, SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING);
                svn_string_t *pSvnStrKeywords = (svn_string_t *)apr_hash_get(pProps, SVN_PROP_KEYWORDS,  APR_HASH_KEY_STRING);
                if (pSvnStrEolStyle || pSvnStrKeywords)
                {
                    apr_hash_t *pHashKeywords = NULL;
                    const char *pszEolStr = NULL;
                    svn_subst_eol_style_t SvnEolStyle = svn_subst_eol_style_none;

                    if (pSvnStrEolStyle)
                        svn_subst_eol_style_from_value(&SvnEolStyle, &pszEolStr, pSvnStrEolStyle->data);

                    if (pSvnStrKeywords)
                    {
                        /** @todo */
                        char aszRev[32];
                        snprintf(aszRev, sizeof(aszRev), "%u", pRev->idRev);

                        char aszUrl[4096];
                        snprintf(aszUrl, sizeof(aszUrl), "https://localhost/vbox/svn");
                        char *pb = &aszUrl[sizeof("https://localhost/vbox/svn") - 1];
                        while (*pszSvnPath)
                        {
                            if (*pszSvnPath == ' ')
                            {
                                *pb++ = '%';
                                *pb++ = '2';
                                *pb++ = '0';
                            }
                            else
                                *pb++ = *pszSvnPath;
                            pszSvnPath++;
                        }
                        *pb = '\0';

                        pSvnErr = svn_subst_build_keywords3(&pHashKeywords, pSvnStrKeywords->data,
                                                            aszRev, aszUrl,
                                                            "https://localhost/vbox/svn", pRev->AprTime,
                                                            pRev->pszGitAuthorEmail, pPool);
                    }

                    if (!pSvnErr)
                    {
                        pSvnStrmIn = svn_subst_stream_translated(svn_stream_disown(pSvnStrmIn, pPool),
                                                                 pszEolStr, FALSE, pHashKeywords, TRUE,
                                                                 pPool);
                        if (!pSvnStrmIn)
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to inject translated stream for '%s'", pszSvnPath);
                    }
                }

                if (!pSvnErr && rcExit == RTEXITCODE_SUCCESS)
                {
                    /* Determine stream length, due to substitutions this is  almost always different compared to what svn reports. */
                    s2gScratchBufReset(&pThis->BufScratch);
                    uint64_t cbFile = 0;
                    for (;;)
                    {
                        void *pv = s2gScratchBufEnsureSize(&pThis->BufScratch, _4K);
                        apr_size_t cbThisRead = _4K;
                        pSvnErr = svn_stream_read_full(pSvnStrmIn, (char *)pv, &cbThisRead);
                        if (pSvnErr)
                            break;
                        s2gScratchBufAdvance(&pThis->BufScratch, cbThisRead);

                        cbFile += cbThisRead;
                        if (cbThisRead < _4K)
                            break;
                    }

                    /* Add the file and stream the data. */
                    int rc = s2gGitTransactionFileAdd(pThis->hGitRepo, pszGitPath, fIsExec, cbFile);
                    if (RT_SUCCESS(rc))
                    {
                        rc = s2gGitTransactionFileWriteData(pThis->hGitRepo, pThis->BufScratch.pbBuf, cbFile);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write data for file '%s' to git repository under '%s': %Rrc",
                                                    pszSvnPath, pszGitPath, rc);
                    }
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add file '%s' to git repository under '%s': %Rrc",
                                                pszSvnPath, pszGitPath, rc);
                }
            }
        }

        if (pSvnErr)
        {
            svn_error_trace(pSvnErr);
            rcExit = RTEXITCODE_FAILURE;
        }
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "'%s' is a special file (probably symlink), NOT IMPLEMENTED", pszSvnPath);

    svn_pool_destroy(pPool);
    return rcExit;
}


static RTEXITCODE s2gSvnProcessExternals(PS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath)
{
    svn_string_t *pProp = NULL;
    svn_error_t *pSvnErr = svn_fs_node_prop(&pProp, pRev->pSvnFsRoot, pszSvnPath, "svn:externals", pRev->pPoolRev);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (!pSvnErr)
    {
        if (pProp)
        {
            const char *pszExternals = pProp->data;
            /* Go through all known externals and add them as a submodule if existing. */
            PCS2GEXTREVMAP pIt;
            RTListForEach(&pThis->LstExternals, pIt, S2GEXTREVMAP, NdExternals)
            {
                const char *pszExternal = RTStrStr(pszExternals, pIt->pszName);
                if (pszExternal)
                {
                    pszExternal += strlen(pIt->pszName);

                    /* We need a revision parameter, otherwise we can't map it to a commit hash. */
                    while (   *pszExternal == ' '
                           || *pszExternal == '\t')
                        *pszExternal++;

                    if (strcmp(pszExternal, "-r") == 0)
                    {
                        pszExternal += sizeof("-r") - 1;

                        while (   *pszExternal == ' '
                               || *pszExternal == '\t')
                            *pszExternal++;

                        /* Try to convert to a revisio number. */
                        uint32_t idExternalRev = 0;
                        int rc = RTStrToUInt32Full(pszExternal, 10, &idExternalRev);
                        if (   rc == VINF_SUCCESS
                            || rc == VERR_TRAILING_SPACES
                            || rc == VERR_TRAILING_CHARS)
                        {
                            if (   idExternalRev < pIt->cEntries
                                && pIt->papszRev2CommitHash[idExternalRev])
                            {
                                rc = s2gGitTransactionSubmoduleAdd(pThis->hGitRepo, pIt->pszName, pIt->papszRev2CommitHash[idExternalRev]);
                                if (RT_FAILURE(rc))
                                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE,
                                                            "Adding submodule for external '%s' with commit hash '%s' for revision number r%u failed: %Rrc",
                                                            pIt->pszName, pIt->papszRev2CommitHash[idExternalRev], idExternalRev);
                            }
                            else
                                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Revision number r%u for external '%s' lacks a git commit hash",
                                                        idExternalRev, pIt->pszName);
                        }
                        else
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to extract revision number for external '%s': %Rrc",
                                                    pIt->pszName, rc);
                    }
                    else
                        RTMsgWarning("No revision parameter for external '%s', skipping", pIt->pszName);

                    break;
                }
            }
        }
    }
    else
    {
        svn_error_trace(pSvnErr);
        rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}


static RTEXITCODE s2gSvnAddGitIgnore(PS2GCTX pThis, const char *pszGitPath, const void *pvData, size_t cbData)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    char szPath[RTPATH_MAX];
    int rc;
    if (*pszGitPath == '\0')
    {
        strcpy(&szPath[0], ".gitignore");
        rc = 1;
    }
    else
        rc = RTStrPrintf2(&szPath[0], sizeof(szPath), "%s/%s", pszGitPath, ".gitignore");
    if (rc > 0)
    {
        rc = s2gGitTransactionFileAdd(pThis->hGitRepo, szPath, false /*fIsExec*/, cbData);
        if (RT_SUCCESS(rc))
            rc = s2gGitTransactionFileWriteData(pThis->hGitRepo, pvData, cbData);

        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add .gitignore '%s': %Rrc", szPath, rc);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to prepare .gitginore path for '%s'", pszGitPath);

    return rcExit;
}


static RTEXITCODE s2gSvnProcessIgnoreContent(PS2GSCRATCHBUF pBuf, const char *pszSvnIgnore, bool fGlobal)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /** @todo This assumes no whitespace fun in filenames. */
    while (*pszSvnIgnore != '\0')
    {
        /* Skip any new line characters. */
        while (   *pszSvnIgnore == '\r'
               || *pszSvnIgnore == '\n')
            pszSvnIgnore++;

        if (*pszSvnIgnore != '\0')
        {
            size_t cchName = 0;
            char *pch = (char *)s2gScratchBufEnsureSize(pBuf, _1K); /** @todo A single filename can't be longer than this. */
            if (!fGlobal)
                *pch++ = '/';

            while (   *pszSvnIgnore != '\r'
                   && *pszSvnIgnore != '\n'
                   && *pszSvnIgnore != '\0')
            {
                /* Patterns containing slashes or backslashes are not supported by git. */
                if (   *pszSvnIgnore == '/'
                    || *pszSvnIgnore == '\\')
                {
                    while (   *pszSvnIgnore != '\r'
                           && *pszSvnIgnore != '\n'
                           && *pszSvnIgnore != '\0')
                        pszSvnIgnore++;
                    cchName = 0;
                    break;
                }

                cchName++;
                if (*pszSvnIgnore == '*')
                {
                    /* Multiple asterisks are not supported by git, convert to a single one. */
                    pszSvnIgnore++;
                    *pch++ = '*';
                    while (*pszSvnIgnore == '*')
                        pszSvnIgnore++;
                }
                else
                    *pch++ = *pszSvnIgnore++;
            }

            if (cchName)
            {
                if (!fGlobal)
                    cchName++;
                *pch++ = '\n';
                cchName++;
                s2gScratchBufAdvance(pBuf, cchName);
            }
        }
    }

    return rcExit;
}


static RTEXITCODE s2gSvnProcessIgnores(PS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath, const char *pszGitPath)
{
    s2gScratchBufReset(&pThis->BufScratch);

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    svn_string_t *pProp = NULL;
    svn_error_t *pSvnErr = svn_fs_node_prop(&pProp, pRev->pSvnFsRoot, pszSvnPath, "svn:ignore", pRev->pPoolRev);
    if (!pSvnErr)
    {
        if (pProp)
            rcExit = s2gSvnProcessIgnoreContent(&pThis->BufScratch, pProp->data, false /*fGlobal*/);

        /* Process global ignores only in the root path. */
        if (   rcExit == RTEXITCODE_SUCCESS
            && *pszGitPath == '\0')
        {
            pProp = NULL;
            pSvnErr = svn_fs_node_prop(&pProp, pRev->pSvnFsRoot, pszSvnPath, "svn:global-ignores", pRev->pPoolRev);
            if (!pSvnErr)
            {
                if (pProp)
                    rcExit = s2gSvnProcessIgnoreContent(&pThis->BufScratch, pProp->data, true /*fGlobal*/);
            }
            else
            {
                svn_error_trace(pSvnErr);
                rcExit = RTEXITCODE_FAILURE;
            }
        }

        if (   rcExit == RTEXITCODE_SUCCESS
            && pThis->BufScratch.offBuf)
            rcExit = s2gSvnAddGitIgnore(pThis, pszGitPath, pThis->BufScratch.pbBuf, pThis->BufScratch.offBuf);
    }
    else
    {
        svn_error_trace(pSvnErr);
        rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}


static RTEXITCODE s2gSvnPathIsEmptyDir(PCS2GSVNREV pRev, const char *pszSvnPath, bool *pfIsEmpty)
{
    apr_hash_t *pEntries = NULL;
    svn_error_t *pSvnErr = svn_fs_dir_entries(&pEntries, pRev->pSvnFsRoot, pszSvnPath, pRev->pPoolRev);
    if (pSvnErr)
    {
        svn_error_trace(pSvnErr);
        return RTEXITCODE_FAILURE;
    }

    *pfIsEmpty = apr_hash_count(pEntries) == 0;
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE s2gSvnExportSinglePath(PS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath, const char *pszGitPath,
                                         bool fIsDir, svn_fs_path_change2_t *pChange)
{
    /* Different strategies depending on the type and change type. */
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    if (fIsDir)
    {
        if (   pChange->change_kind == svn_fs_path_change_add
            || pChange->change_kind == svn_fs_path_change_modify)
        {
            /* Check properties. */
            if (pChange->prop_mod)
            {
                rcExit = s2gSvnProcessExternals(pThis, pRev, pszSvnPath);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    /* Process svn:ignore. */
                    rcExit = s2gSvnProcessIgnores(pThis, pRev, pszSvnPath, pszGitPath);
                }
            }
            else
            {
                /* If the directory is empty we need to add a .gitignore. */
                bool fIsEmpty = false;
                rcExit = s2gSvnPathIsEmptyDir(pRev, pszSvnPath, &fIsEmpty);
                if (   rcExit == RTEXITCODE_SUCCESS
                    && fIsEmpty)
                    rcExit = s2gSvnAddGitIgnore(pThis, pszGitPath, NULL /*pvData*/, 0 /*cbData*/);
            }
        }
        else
            AssertReleaseFailed();
    }
    else
    {
        /* File being added, just dump the contents to the git repository. */
        /** @todo Handle copies from branches where the source branch is part of the git repository. */
        if (   pChange->change_kind == svn_fs_path_change_add
            || pChange->change_kind == svn_fs_path_change_modify
            || pChange->change_kind == svn_fs_path_change_replace)
            rcExit = s2gSvnDumpBlob(pThis, pRev, pszSvnPath, pszGitPath);
        else if (pChange->change_kind == svn_fs_path_change_delete)
        {
            int rc = s2gGitTransactionFileRemove(pThis->hGitRepo, pszGitPath);
            if (RT_SUCCESS(rc))
            {
                /** @todo Check whether the directory is empty now and add a .gitignore file if it has not already due to
                 * svn:ignore properties.
                 */
                rcExit = RTEXITCODE_SUCCESS;
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to remove '%s' from git repository", pszGitPath);
        }
        else
            AssertReleaseFailed();
    }

    return rcExit;
}


static RTEXITCODE s2gSvnRevisionExportPaths(PS2GCTX pThis, PS2GSVNREV pRev)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    apr_hash_t *pSvnChanges;

    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    svn_error_t *pSvnErr = svn_fs_paths_changed2(&pSvnChanges, pRev->pSvnFsRoot, pRev->pPoolRev);
    RT_GCC_NO_WARN_DEPRECATED_END
    if (!pSvnErr)
    {
        for (apr_hash_index_t *pIt = apr_hash_first(pRev->pPoolRev, pSvnChanges); pIt; pIt = apr_hash_next(pIt))
        {
            const void *vkey;
            void *value;
            apr_hash_this(pIt, &vkey, NULL, &value);
            const char *pszPath = (const char *)vkey;
            svn_fs_path_change2_t *pChange = (svn_fs_path_change2_t *)value;

            /* Insert the change into the list sorted by path. */
            /** @todo Speedup. */
            PS2GSVNREVCHANGE pItChanges;
            RTListForEach(&pRev->LstChanges, pItChanges, S2GSVNREVCHANGE, NdChanges)
            {
                int iCmp = strcmp(pItChanges->pszPath, pszPath);
                if (!iCmp)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Duplicate key found in rev %d: %s", pRev->idRev, pszPath);
                else if (iCmp > 0)
                    break;
            }
            PS2GSVNREVCHANGE pNew = (PS2GSVNREVCHANGE)RTMemAllocZ(sizeof(*pNew));
            if (!pNew)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate new change record for path: %s", pszPath);
            pNew->pszPath = pszPath;
            pNew->pChange = pChange;
            RTListNodeInsertBefore(&pItChanges->NdChanges, &pNew->NdChanges);
        }

        /* Work on the changes. */
        PS2GSVNREVCHANGE pIt;
        RTListForEach(&pRev->LstChanges, pIt, S2GSVNREVCHANGE, NdChanges)
        {
            if (g_cVerbosity > 1)
                RTMsgInfo("    %s %s\n", pIt->pszPath, s2gSvnChangeKindToStr(pIt->pChange->change_kind));

            /* Query whether this is a directory. */
            bool fIsDir = false;
            svn_boolean_t SvnIsDir;
            pSvnErr = svn_fs_is_dir(&SvnIsDir, pRev->pSvnFsRoot, pIt->pszPath, pRev->pPoolRev);
            if (pSvnErr)
            {
                svn_error_trace(pSvnErr);
                rcExit = RTEXITCODE_FAILURE;
                break;
            }
            fIsDir = RT_BOOL(SvnIsDir);

            /*
             * If this is a directory which was just added without any properties check whether the next entry starts with the path,
             * meaning we can skip it as git doesn't handle empty directories and we don't want unnecessary .gitignore
             * files.
             */
            if (   fIsDir
                && pIt->pChange->change_kind == svn_fs_path_change_add
                && pIt->pChange->prop_mod == 0)
            {
                PS2GSVNREVCHANGE pNext = RTListGetNext(&pRev->LstChanges, pIt, S2GSVNREVCHANGE, NdChanges);
                if (   pNext
                    && RTStrStartsWith(pNext->pszPath, pIt->pszPath))
                    continue;
            }

            if (RTStrStartsWith(pIt->pszPath, "/trunk"))
            {
                const char *pszGitPath = pIt->pszPath + sizeof("/trunk") - 1;
                if (*pszGitPath == '/')
                    pszGitPath++;

                rcExit = s2gSvnExportSinglePath(pThis, pRev, pIt->pszPath, pszGitPath, fIsDir, pIt->pChange);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
            }
        }
    }
    else
    {
        svn_error_trace(pSvnErr);
        rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}


static RTEXITCODE s2gSvnExportRevision(PS2GCTX pThis, uint32_t idRev)
{
    S2GSVNREV Rev;

    RTMsgInfo("Exporting revision r%u\n", idRev);

    RTListInit(&Rev.LstChanges);
    Rev.pPoolRev = svn_pool_create(pThis->pPoolDefault);
    if (!Rev.pPoolRev)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create APR pool for revision r%u", idRev);

    /* Open revision and fetch properties. */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    Rev.idRev             = idRev;
    Rev.pszGitAuthor      = NULL;
    Rev.pszGitAuthorEmail = NULL;
    svn_error_t *pSvnErr = svn_fs_revision_root(&Rev.pSvnFsRoot, pThis->pSvnFs, idRev, Rev.pPoolRev);
    if (!pSvnErr)
    {
        apr_hash_t *pRevPros = NULL;

        RT_GCC_NO_WARN_DEPRECATED_BEGIN
        pSvnErr = svn_fs_revision_proplist(&pRevPros, pThis->pSvnFs, idRev, Rev.pPoolRev);
        RT_GCC_NO_WARN_DEPRECATED_END
        if (!pSvnErr)
        {
            svn_string_t *pSvnAuthor = (svn_string_t *)apr_hash_get(pRevPros, "svn:author",                 APR_HASH_KEY_STRING);
            svn_string_t *pSvnDate   = (svn_string_t *)apr_hash_get(pRevPros, "svn:date",                   APR_HASH_KEY_STRING);
            svn_string_t *pSvnLog    = (svn_string_t *)apr_hash_get(pRevPros, "svn:log",                    APR_HASH_KEY_STRING);
            svn_string_t *pSvnXRef   = (svn_string_t *)apr_hash_get(pRevPros, "svn:sync-xref-src-repo-rev", APR_HASH_KEY_STRING);

            AssertRelease(pSvnAuthor && pSvnDate && pSvnLog);
            pSvnErr = svn_time_from_cstring(&Rev.AprTime, pSvnDate->data, Rev.pPoolRev);
            if (!pSvnErr)
            {
                Rev.pszSvnAuthor = pSvnAuthor->data;
                Rev.pszSvnLog    = pSvnLog->data;
                Rev.pszSvnXref   = pSvnXRef ? pSvnXRef->data : NULL;

                PCS2GAUTHOR pAuthor = (PCS2GAUTHOR)RTStrSpaceGet(&pThis->StrSpaceAuthors, Rev.pszSvnAuthor);
                if (pAuthor)
                {
                    Rev.pszGitAuthor      = pAuthor->pszGitAuthor;
                    Rev.pszGitAuthorEmail = pAuthor->pszGitEmail;

                    RTTIMESPEC Tm;
                    RTTimeSpecFromString(&Tm, pSvnDate->data);
                    Rev.cEpochSecs = RTTimeSpecGetSeconds(&Tm);
                    if (g_cVerbosity)
                        RTMsgInfo("    %s %s %s\n", Rev.pszSvnAuthor, pSvnDate->data, Rev.pszSvnXref ? Rev.pszSvnXref : "");

                    int rc = s2gGitTransactionStart(pThis->hGitRepo);
                    if (RT_SUCCESS(rc))
                    {
                        rcExit = s2gSvnRevisionExportPaths(pThis, &Rev);
                        if (rcExit == RTEXITCODE_SUCCESS)
                        {
                            size_t cchRevLog = strlen(Rev.pszSvnLog);

                            s2gScratchBufReset(&pThis->BufScratch);
                            rc = s2gScratchBufPrintf(&pThis->BufScratch, "%s%s\nsvn:sync-xref-src-repo-rev: r%s\n",
                                                     Rev.pszSvnLog, Rev.pszSvnLog[cchRevLog] == '\n' ? "" : "\n",
                                                     Rev.pszSvnXref);
                            if (RT_SUCCESS(rc))
                                rc = s2gGitTransactionCommit(pThis->hGitRepo, Rev.pszGitAuthor, Rev.pszGitAuthorEmail,
                                                             pThis->BufScratch.pbBuf, Rev.cEpochSecs);
                            if (RT_FAILURE(rc))
                                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to commit git transaction with: %Rrc", rc);
                        }
                    }
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to start new git transaction with: %Rrc", rc);
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Author '%s' is not known", Rev.pszSvnAuthor);
            }
            else
            {
                svn_error_trace(pSvnErr);
                rcExit = RTEXITCODE_FAILURE;
            }
        }
        else
        {
            svn_error_trace(pSvnErr);
            rcExit = RTEXITCODE_FAILURE;
        }
    }
    else
    {
        svn_error_trace(pSvnErr);
        rcExit = RTEXITCODE_FAILURE;
    }

    svn_pool_destroy(Rev.pPoolRev);
    return rcExit;
}


static RTEXITCODE s2gSvnExport(PS2GCTX pThis)
{
    for (uint32_t idRev = pThis->idRevStart; idRev <= pThis->idRevEnd; idRev++)
    {
        RTEXITCODE rcExit = s2gSvnExportRevision(pThis, idRev);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE s2gSvnFindMatchingRevision(PS2GCTX pThis, uint32_t idRevInternal, uint32_t *pidRev)
{
    /* Work backwards from the youngest revision and try to get svn:sync-xref-src-repo-rev and check whether it matches. */
    uint32_t idRev = pThis->idRevEnd;
    apr_pool_t *pPool = svn_pool_create(NULL);
    if (!pPool)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create APR pool");

    while (idRev)
    {
        svn_string_t *pSvnXRef = NULL;
        RT_GCC_NO_WARN_DEPRECATED_BEGIN
        svn_error_t *pSvnErr = svn_fs_revision_prop(&pSvnXRef, pThis->pSvnFs, idRev, "svn:sync-xref-src-repo-rev", pPool);
        RT_GCC_NO_WARN_DEPRECATED_END
        if (!pSvnErr)
        {
            if (g_cVerbosity >= 4)
                RTMsgInfo("Searching r%u: %s\n", idRev, pSvnXRef ? pSvnXRef->data : "");

            if (pSvnXRef)
            {
                uint32_t idRevRef = RTStrToUInt32(pSvnXRef->data);
                if (idRevRef)
                {
                    if (idRevRef == idRevInternal)
                    {
                        *pidRev = idRev;
                        svn_pool_destroy(pPool);
                        return RTEXITCODE_SUCCESS;
                    }
                }
                else
                {
                    RTMsgErrorExit(RTEXITCODE_FAILURE, "r%u's svn:sync-xref-src-repo-rev property contains invalid data: %s",
                                   idRev, pSvnXRef->data);
                    break;
                }
            }
            else
            {
                RTMsgErrorExit(RTEXITCODE_FAILURE, "r%u misses svn:sync-xref-src-repo-rev property", idRev);
                break;
            }
        }
        else
        {
            svn_error_trace(pSvnErr);
            break;
        }

        idRev--;
        svn_pool_clear(pPool);
    }

    svn_pool_destroy(pPool);
    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Couldn't match internal revision r%u to external one", idRevInternal);
}


static RTEXITCODE s2gGitInit(PS2GCTX pThis)
{
    uint32_t idRevLast = 0;
    int rc = s2gGitRepositoryCreate(&pThis->hGitRepo, pThis->pszGitRepoPath, pThis->pszGitDefBranch,
                                    pThis->pszDumpFilename, &idRevLast);
    if (RT_SUCCESS(rc))
    {
        if (   pThis->idRevStart == UINT32_MAX
            && idRevLast != 0)
        {
            /*
             * We need to match the revision to the one of the repository as svn:sync-xref-src-repo-rev
             * is a property.
             */
            uint32_t idRevPublic = 0;
            RTEXITCODE rcExit = s2gSvnFindMatchingRevision(pThis, idRevLast + 1, &idRevPublic);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;

            RTMsgInfo("Matched internal revision r%u to public r%u, continuing at that revision\n",
                      idRevLast + 1, idRevPublic);

            pThis->idRevStart = idRevPublic;
        }
        else
            pThis->idRevStart = 1;
        return RTEXITCODE_SUCCESS;
    }

    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Creating the git repository under '%s' failed with: %Rrc",
                          pThis->pszGitRepoPath, rc);
}


int main(int argc, char *argv[])
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    S2GCTX This;
    s2gCtxInit(&This);
    RTEXITCODE rcExit = s2gParseArguments(&This, argc, argv);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        rcExit = s2gLoadConfig(&This);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            rcExit = s2gSvnInit(&This);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                rcExit = s2gGitInit(&This);
                if (rcExit == RTEXITCODE_SUCCESS)
                    rcExit = s2gSvnExport(&This);

                s2gGitRepositoryClose(This.hGitRepo);
            }
        }
    }
    s2gCtxDestroy(&This);

    return rcExit;
}

