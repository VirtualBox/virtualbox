/* $Id: svn2git.cpp 168989 2025-05-26 09:45:34Z alexander.eichner@oracle.com $ */
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
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/json.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
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
              , argv0);
    return RTEXITCODE_SUCCESS;
}


/**
 * Initializes the given context to default values.
 */
static void s2gCtxInit(PS2GCTX pThis)
{
    pThis->idRevStart      = 1;
    pThis->idRevEnd        = UINT32_MAX;
    pThis->pszCfgFilename  = NULL;
    pThis->pszSvnRepo      = NULL;
    pThis->pszGitRepoPath  = NULL;
    pThis->pszGitDefBranch = "main";

    pThis->pPoolDefault    = NULL;
    pThis->pPoolScratch    = NULL;
    pThis->pSvnRepos       = NULL;
    pThis->pSvnFs          = NULL;
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

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision: 168989 $";
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


static RTEXITCODE s2gLoadConfig(PS2GCTX pThis)
{
    RTJSONVAL hRoot = NIL_RTJSONVAL;
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    int rc = RTJsonParseFromFile(&hRoot, RTJSON_PARSE_F_JSON5, pThis->pszCfgFilename, &ErrInfo.Core);
    if (RT_SUCCESS(rc))
    {
        rc = RTJsonValueQueryStringByName(hRoot, "GitRepoPath", &pThis->pszGitRepoPath);
        RTJsonValueRelease(hRoot);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query GitRepoPath from '%s': %Rrc", pThis->pszCfgFilename, rc);

        return RTEXITCODE_SUCCESS;
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
    svn_error_t *pSvnErr = svn_fs_node_prop(&pPropVal, pThis->pSvnFsRoot, pszSvnPath, "svn:executable", pSvnPool);
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


static RTEXITCODE s2gSvnDumpBlob(PCS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath, const char *pszGitPath)
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
        svn_filesize_t cbSvnFile = 0;
        svn_error_t *pSvnErr = svn_fs_file_length(&cbSvnFile, pRev->pSvnFsRoot, pszSvnPath, pPool);
        if (!pSvnErr)
        {
            svn_stream_t pSvnStrmIn = NULL;
            pSvnErr = svn_fs_file_contents(&pSvnStrmIn, pRev->pSvnFsRoot, pszSvnPath, pPool);
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
                        }

                        pSvnStrmIn = svn_subst_stream_translated(svn_stream_disown(pSvnStrmIn, pPool),
                                                                 pszEolStr, FALSE, pHashKeywords, TRUE,
                                                                 pPool);
                        if (!pSvnStrmIn)
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to inject translated stream for '%s'", pszSvnPath);
                    }

                    /* When doing substitutions we need to determine the new stream length as substitutions change that. */
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


static RTEXITCODE s2gSvnExportSinglePath(PCS2GCTX pThis, PS2GSVNREV pRev, const char *pszSvnPath, const char *pszGitPath,
                                         bool fIsDir, svn_fs_path_change2_t *pChange)
{
    if (pChange->change_kind == svn_fs_path_change_add)
    {

    }

    RT_NOREF(pThis, pRev, pszSvnPath, pszGitPath, fIsDir, pChange);
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE s2gSvnRevisionExportPaths(PCS2GCTX pThis, PS2GSVNREV pRev)
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

            /* Ignore /trunk, /branches and /tags. */
            if (   !strcmp(pIt->pszPath, "/trunk")
                || !strcmp(pIt->pszPath, "/branches")
                || !strcmp(pIt->pszPath, "/tags"))
                continue;

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
             * If this is a directory which was just added check whether the next entry starts with the path,
             * meaning we can skip it as git doesn't handle empty directories and we don't want unnecessary .gitignore
             * files.
             */
            if (fIsDir && pIt->pChange->change_kind == svn_fs_path_change_add)
            {
                PS2GSVNREVCHANGE pNext = RTListGetNext(&pRev->LstChanges, pIt, S2GSVNREVCHANGE, NdChanges);
                if (   pNext
                    && RTStrStartsWith(pNext->pszPath, pIt->pszPath))
                    continue;
            }

            /** @todo Make this configurable. */
            if (RTStrStartsWith(pIt->pszPath, "/trunk/"))
            {
                const char *pszGitPath = pIt->pszPath + sizeof("/trunk/") - 1;
                rcExit = s2gSvnExportSinglePath(pThis, pRev, pIt->pszPath, pszGitPath, fIsDir, pIt->pChange);
            }
        }

        RT_NOREF(pThis);
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
    Rev.idRev = idRev;
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
                        /** @todo Ammend svn log with xref. */
                        rc = s2gGitTransactionCommit(pThis->hGitRepo, Rev.pszSvnAuthor, NULL /*Rev.pszSvnAuthor*/,
                                                     Rev.pszSvnLog, Rev.cEpochSecs);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to commit git transaction with: %Rrc", rc);
                    }
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to start new git transaction with: %Rrc", rc);
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


static RTEXITCODE s2gGitInit(PS2GCTX pThis)
{
    int rc = s2gGitRepositoryCreate(&pThis->hGitRepo, pThis->pszGitRepoPath, pThis->pszGitDefBranch);
    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;

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

