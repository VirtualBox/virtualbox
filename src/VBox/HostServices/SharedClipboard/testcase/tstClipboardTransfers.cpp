/* $Id: tstClipboardTransfers.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard transfers test case.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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

#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/test.h>
#include <iprt/thread.h>


/** @name TST_SHCL_TRANSFER_STATUS_F_XXX - Testcase transfer status flags.
 *
 * Each flag uses the numeric SHCLTRANSFERSTATUS value as its bit position.
 * This lets expected transition masks name statuses directly and keeps them
 * independent of the order of the status array used to exercise the masks.
 * @{ */
#define TST_SHCL_TRANSFER_STATUS_F(a_enmStatus)      RT_BIT_32(a_enmStatus)
#define TST_SHCL_TRANSFER_STATUS_F_NONE              TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_NONE)
#define TST_SHCL_TRANSFER_STATUS_F_REQUESTED         TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_REQUESTED)
#define TST_SHCL_TRANSFER_STATUS_F_INITIALIZED       TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_INITIALIZED)
#define TST_SHCL_TRANSFER_STATUS_F_UNINITIALIZED     TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_UNINITIALIZED)
#define TST_SHCL_TRANSFER_STATUS_F_STARTED           TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_STARTED)
#define TST_SHCL_TRANSFER_STATUS_F_COMPLETED         TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_COMPLETED)
#define TST_SHCL_TRANSFER_STATUS_F_CANCELED          TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_CANCELED)
#define TST_SHCL_TRANSFER_STATUS_F_KILLED            TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_KILLED)
#define TST_SHCL_TRANSFER_STATUS_F_ERROR             TST_SHCL_TRANSFER_STATUS_F(SHCLTRANSFERSTATUS_ERROR)
/** @} */


static int testCreateTempDir(RTTEST hTest, const char *pszTestcase, char *pszTempDir, size_t cbTempDir)
{
    char szTempDir[RTPATH_MAX];
    int rc = RTPathTemp(szTempDir, sizeof(szTempDir));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardTransfers");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreate(szTempDir, 0700, 0);
    if (rc == VERR_ALREADY_EXISTS)
        rc = VINF_SUCCESS;
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "XXXXX");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreateTemp(szTempDir, 0700);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    char szTempDirReal[RTPATH_MAX];
    rc = RTPathReal(szTempDir, szTempDirReal, sizeof(szTempDirReal));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathJoin(pszTempDir, cbTempDir, szTempDirReal, pszTestcase);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Created temporary directory: %s\n", pszTempDir);

    return rc;
}

static int testRemoveTempDir(RTTEST hTest)
{
    char szTempDir[RTPATH_MAX];
    int rc = RTPathTemp(szTempDir, sizeof(szTempDir));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardTransfers");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirRemoveRecursive(szTempDir, RTDIRRMREC_F_CONTENT_AND_DIR);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Removed temporary directory: %s\n", szTempDir);

    return rc;
}

static int testCreateDir(RTTEST hTest, const char *pszPathToCreate)
{
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Creating directory: %s\n", pszPathToCreate);

    int rc = RTDirCreateFullPath(pszPathToCreate, 0700);
    if (rc == VERR_ALREADY_EXISTS)
        rc = VINF_SUCCESS;
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    return rc;
}

static int testCreateFile(RTTEST hTest, const char *pszTempDir, const char *pszFileName, uint32_t fOpen, size_t cbSize,
                          char **ppszFilePathAbs)
{
    char szFilePath[RTPATH_MAX];

    int rc = RTStrCopy(szFilePath, sizeof(szFilePath), pszTempDir);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    rc = RTPathAppend(szFilePath, sizeof(szFilePath), pszFileName);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    char *pszDirToCreate = RTStrDup(szFilePath);
    RTTESTI_CHECK_RET(pszDirToCreate, VERR_NO_MEMORY);

    RTPathStripFilename(pszDirToCreate);

    rc = testCreateDir(hTest, pszDirToCreate);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    RTStrFree(pszDirToCreate);
    pszDirToCreate = NULL;

    if (!fOpen)
        fOpen = RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE;

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Creating file: %s\n", szFilePath);

    RTFILE hFile;
    rc = RTFileOpen(&hFile, szFilePath, fOpen);
    if (RT_SUCCESS(rc))
    {
        if (cbSize)
        {
            /** @todo Fill in some random stuff. */
        }

        rc = RTFileClose(hFile);
        RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);
    }

    if (ppszFilePathAbs)
        *ppszFilePathAbs = RTStrDup(szFilePath);

    return rc;
}

typedef struct TESTTRANSFERROOTENTRY
{
    TESTTRANSFERROOTENTRY(const RTCString &a_strPath)
        : strPath(a_strPath) { }

    RTCString strPath;
} TESTTRANSFERROOTENTRY;

static int testAddRootEntry(RTTEST hTest, const char *pszTempDir,
                            const TESTTRANSFERROOTENTRY &rootEntry, char **ppszRoots)
{
    char *pszRoots = NULL;

    const char *pszPath = rootEntry.strPath.c_str();

    char *pszPathAbs;
    int rc = testCreateFile(hTest, pszTempDir, pszPath, 0, 0, &pszPathAbs);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    rc = RTStrAAppend(&pszRoots, pszPathAbs);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszPathAbs);

    *ppszRoots = pszRoots;

    return rc;
}

static int testAddRootEntries(RTTEST hTest, const char *pszTempDir,
                              RTCList<TESTTRANSFERROOTENTRY> &lstBase, RTCList<TESTTRANSFERROOTENTRY> lstToExtend,
                              char **ppszRoots)
{
    int rc = VINF_SUCCESS;

    char *pszRoots = NULL;

    for (size_t i = 0; i < lstBase.size(); ++i)
    {
        char *pszEntry = NULL;
        rc = testAddRootEntry(hTest, pszTempDir, lstBase.at(i), &pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        rc = RTStrAAppend(&pszRoots, pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        RTStrFree(pszEntry);
    }

    for (size_t i = 0; i < lstToExtend.size(); ++i)
    {
        char *pszEntry = NULL;
        rc = testAddRootEntry(hTest, pszTempDir, lstToExtend.at(i), &pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        rc = RTStrAAppend(&pszRoots, pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        RTStrFree(pszEntry);
    }

    if (RT_SUCCESS(rc))
        *ppszRoots = pszRoots;

    return rc;
}

static void testTransferRootsSetSingle(RTTEST hTest,
                                       RTCList<TESTTRANSFERROOTENTRY> &lstBase, RTCList<TESTTRANSFERROOTENTRY> lstToExtend,
                                       int rcExpected)
{
    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    SHCLTXPROVIDER Provider;
    RTTESTI_CHECK(ShClTransferProviderLocalQueryInterface(&Provider) != NULL);
    RTTESTI_CHECK_RC_OK(ShClTransferSetProvider(pTransfer, &Provider));

    char szTestTransferRootsSetDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferRootsSet", szTestTransferRootsSetDir, sizeof(szTestTransferRootsSetDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    /* This is the file we're trying to access (but not supposed to). */
    rc = testCreateFile(hTest, szTestTransferRootsSetDir, "must-not-access-this", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    char *pszRoots;
    rc = testAddRootEntries(hTest, szTestTransferRootsSetDir, lstBase, lstToExtend, &pszRoots);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC(rc, rcExpected);

    RTStrFree(pszRoots);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testTransferObjOpenSingle(RTTEST hTest,
                                      RTCList<TESTTRANSFERROOTENTRY> &lstRoots, const char *pszObjPath, int rcExpected)
{
    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferObjOpenDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferObjOpen", szTestTransferObjOpenDir, sizeof(szTestTransferObjOpenDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    /* This is the file we're trying to access (but not supposed to). */
    rc = testCreateFile(hTest, szTestTransferObjOpenDir, "file1.txt", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    RTCList<TESTTRANSFERROOTENTRY> lstToExtendEmpty;

    char *pszRoots;
    rc = testAddRootEntries(hTest, szTestTransferObjOpenDir, lstRoots, lstToExtendEmpty, &pszRoots);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLOBJOPENCREATEPARMS openCreateParms;
    rc = ShClTransferObjOpenParmsInit(&openCreateParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, pszObjPath);
    RTTESTI_CHECK_RC_OK(rc);

    SHCLOBJHANDLE hObj;
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC(rc, rcExpected);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferObjClose(pTransfer, hObj);
        RTTESTI_CHECK_RC_OK(rc);
    }

    ShClTransferObjOpenParmsDestroy(&openCreateParms);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testPathSanitize(void)
{
    RTTestISub("Testing path sanitizing");

    /* Valid dotted names: expect success and no path changes. */
    char szValid[] = "dir.with.dots/file...txt";
    int rc = ShClPathSanitize(szValid, sizeof(szValid));
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(!strcmp(szValid, "dir.with.dots/file...txt"));

    /* Current-directory components are not transferable. */
    char szDot[] = "dir/./file.txt";
    rc = ShClPathSanitize(szDot, sizeof(szDot));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Parent-directory components are not transferable. */
    char szDotDot[] = "dir/../file.txt";
    rc = ShClPathSanitize(szDotDot, sizeof(szDotDot));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Malformed UTF-8 paths are rejected before use. */
    uint8_t abInvalidUtf8[] = { 0xc0, 0xaf, 0 };
    rc = ShClPathSanitize((char *)abInvalidUtf8, sizeof(abInvalidUtf8));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_UTF8_ENCODING);

    /* Unterminated bounded path buffers are rejected. */
    char szUnterminated[] = { 'a', 'b' };
    rc = ShClPathSanitize(szUnterminated, sizeof(szUnterminated));
    RTTESTI_CHECK_RC(rc, VERR_BUFFER_OVERFLOW);
}

static void testEvents(void)
{
    RTTestISub("Testing events");

    SHCLEVENTSOURCE Source;
    RTTESTI_CHECK_RC_OK(ShClEventSourceInit(&Source, 0));
    RTTESTI_CHECK(ShClEventSourceGetLast(&Source) == NULL); /* Should be empty. */
    RTTESTI_CHECK_RC_OK(ShClEventSourceTerm(&Source));
    RTTESTI_CHECK_RC_OK(ShClEventSourceTerm(&Source)); /* Destroying a second time, intentional. */

    RTTESTI_CHECK_RC_OK(ShClEventSourceInit(&Source, 42));
    PSHCLEVENT pEvent;
    RTTESTI_CHECK_RC_OK(ShClEventSourceGenerateAndRegisterEvent(&Source, &pEvent));
    ShClEventSourceReset(&Source);
    RTTESTI_CHECK(ShClEventSourceGetLast(&Source) == NULL); /* Event still valid, but removed from the source. */
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0); /* Free'd event, as ref count is 0. */
    RTTESTI_CHECK(ShClEventSourceGetLast(&Source) == NULL); /* Now it should be empty. */
    RTTESTI_CHECK_RC_OK(ShClEventSourceTerm(&Source));

    /* Test delayed destruction of the event by retaining it. */
    RTTESTI_CHECK_RC_OK(ShClEventSourceInit(&Source, 42));
    RTTESTI_CHECK_RC_OK(ShClEventSourceGenerateAndRegisterEvent(&Source, &pEvent));
    RTTESTI_CHECK_RC_OK(ShClEventRetain(pEvent));
    RTTESTI_CHECK(ShClEventGetRefs(pEvent) == 2);
    RTTESTI_CHECK_RC_OK(ShClEventSourceTerm(&Source));
    RTTESTI_CHECK(ShClEventGetRefs(pEvent) == 2); /* Make sure the ref count didn't drop due to ShClEventSourceDestroy(). */
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 1);
    RTTESTI_CHECK(ShClEventGetRefs(pEvent) == 1);
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0); /* Free'd event, as ref count is 0. */
    RTTESTI_CHECK_RC_OK(ShClEventSourceTerm(&Source)); /* Try to destruct again. */
}

static void testTransferBasics(void)
{
    RTTestISub("Testing transfer basics");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(ShClTransferGetID(pTransfer) == NIL_SHCLTRANSFERID);
    RTTESTI_CHECK(ShClTransferGetSessionId(pTransfer) == NIL_SHCLSESSIONID);
    RTTESTI_CHECK(ShClTransferGetGeneration(pTransfer) == NIL_SHCLTRANSFERGEN);
    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    pTransfer = NULL; /* Was free'd above. */
    rc = ShClTransferDestroy(pTransfer); /* Second time, intentional. */
    RTTESTI_CHECK_RC_OK(rc);

    PSHCLLIST pList = ShClTransferListAlloc();
    RTTESTI_CHECK(pList != NULL);
    rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    ShClTransferListFree(pList);
    pList = NULL;
    ShClTransferListFree(pList); /* Second time, intentional. */

    SHCLLISTENTRY Entry;
    RTTESTI_CHECK_RC_OK(ShClTransferListEntryInit(&Entry));
    ShClTransferListEntryDestroy(&Entry);
    ShClTransferListEntryDestroy(&Entry); /* Second time, intentional. */

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

/**
 * Tests common Shared Clipboard and transfer validation.
 */
static void testTransferValidation(void)
{
    RTTestISub("Testing Shared Clipboard validation");

    RTTESTI_CHECK(ShClFormatIsValid(VBOX_SHCL_FMT_UNICODETEXT));
    RTTESTI_CHECK(ShClFormatIsValid(VBOX_SHCL_FMT_BITMAP));
    RTTESTI_CHECK(ShClFormatIsValid(VBOX_SHCL_FMT_HTML));
    RTTESTI_CHECK(ShClFormatIsValid(VBOX_SHCL_FMT_URI_LIST));
    RTTESTI_CHECK(!ShClFormatIsValid(VBOX_SHCL_FMT_NONE));
    RTTESTI_CHECK(!ShClFormatIsValid(VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_BITMAP));
    RTTESTI_CHECK(!ShClFormatIsValid(VBOX_SHCL_FMT_VALID_MASK + 1));

    RTTESTI_CHECK(ShClFormatsAreValid(VBOX_SHCL_FMT_NONE));
    RTTESTI_CHECK(ShClFormatsAreValid(VBOX_SHCL_FMT_VALID_MASK));
    RTTESTI_CHECK(!ShClFormatsAreValid(VBOX_SHCL_FMT_VALID_MASK + 1));

    RTTESTI_CHECK(ShClTransferDirIsValid(SHCLTRANSFERDIR_FROM_REMOTE));
    RTTESTI_CHECK(ShClTransferDirIsValid(SHCLTRANSFERDIR_TO_REMOTE));
    RTTESTI_CHECK(!ShClTransferDirIsValid(SHCLTRANSFERDIR_UNKNOWN));
    RTTESTI_CHECK(!ShClTransferDirIsValid(SHCLTRANSFERDIR_32BIT_HACK));

    RTTESTI_CHECK(ShClSourceIsValid(SHCLSOURCE_LOCAL));
    RTTESTI_CHECK(ShClSourceIsValid(SHCLSOURCE_REMOTE));
    RTTESTI_CHECK(!ShClSourceIsValid(SHCLSOURCE_INVALID));
    RTTESTI_CHECK(!ShClSourceIsValid(SHCLSOURCE_32BIT_HACK));

    RTTESTI_CHECK(ShClTransferIdIsValid(1));
    RTTESTI_CHECK(ShClTransferIdIsValid(VBOX_SHCL_MAX_TRANSFERS - 2));
    RTTESTI_CHECK(!ShClTransferIdIsValid(0));
    RTTESTI_CHECK(!ShClTransferIdIsValid(VBOX_SHCL_MAX_TRANSFERS - 1));
    RTTESTI_CHECK(!ShClTransferIdIsValid(NIL_SHCLTRANSFERID));
    RTTESTI_CHECK(!ShClTransferIdIsValid(UINT32_MAX));

    RTTESTI_CHECK(ShClTransferKeyIsValid(1, 1, 1));
    RTTESTI_CHECK(ShClTransferKeyIsValid(1, VBOX_SHCL_MAX_TRANSFERS - 2, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(0, 1, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(NIL_SHCLSESSIONID, 1, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, 0, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, VBOX_SHCL_MAX_TRANSFERS - 1, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, NIL_SHCLTRANSFERID, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, UINT32_MAX, 1));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, 1, 0));
    RTTESTI_CHECK(!ShClTransferKeyIsValid(1, 1, NIL_SHCLTRANSFERGEN));

    /** Expected classification and transitions for each transfer status. */
    static struct
    {
        /** Transfer status under test. */
        SHCLTRANSFERSTATUS enmStatus;
        /** Whether the status is terminal. */
        bool               fTerminal;
        /** Flags identifying statuses to which the status may transition. */
        uint32_t           fTransitions;
    } const s_aStatusTests[] =
    {
        { SHCLTRANSFERSTATUS_NONE,          false, TST_SHCL_TRANSFER_STATUS_F_NONE },
        { SHCLTRANSFERSTATUS_REQUESTED,     false, TST_SHCL_TRANSFER_STATUS_F_REQUESTED | TST_SHCL_TRANSFER_STATUS_F_INITIALIZED | TST_SHCL_TRANSFER_STATUS_F_UNINITIALIZED | TST_SHCL_TRANSFER_STATUS_F_CANCELED | TST_SHCL_TRANSFER_STATUS_F_KILLED | TST_SHCL_TRANSFER_STATUS_F_ERROR },
        { SHCLTRANSFERSTATUS_INITIALIZED,   false, TST_SHCL_TRANSFER_STATUS_F_INITIALIZED | TST_SHCL_TRANSFER_STATUS_F_UNINITIALIZED | TST_SHCL_TRANSFER_STATUS_F_STARTED | TST_SHCL_TRANSFER_STATUS_F_COMPLETED | TST_SHCL_TRANSFER_STATUS_F_CANCELED | TST_SHCL_TRANSFER_STATUS_F_KILLED | TST_SHCL_TRANSFER_STATUS_F_ERROR },
        { SHCLTRANSFERSTATUS_UNINITIALIZED, true,  TST_SHCL_TRANSFER_STATUS_F_UNINITIALIZED },
        { SHCLTRANSFERSTATUS_STARTED,       false, TST_SHCL_TRANSFER_STATUS_F_UNINITIALIZED | TST_SHCL_TRANSFER_STATUS_F_STARTED | TST_SHCL_TRANSFER_STATUS_F_COMPLETED | TST_SHCL_TRANSFER_STATUS_F_CANCELED | TST_SHCL_TRANSFER_STATUS_F_KILLED | TST_SHCL_TRANSFER_STATUS_F_ERROR },
        { SHCLTRANSFERSTATUS_COMPLETED,     true,  TST_SHCL_TRANSFER_STATUS_F_COMPLETED },
        { SHCLTRANSFERSTATUS_CANCELED,      true,  TST_SHCL_TRANSFER_STATUS_F_CANCELED },
        { SHCLTRANSFERSTATUS_KILLED,        true,  TST_SHCL_TRANSFER_STATUS_F_KILLED },
        { SHCLTRANSFERSTATUS_ERROR,         true,  TST_SHCL_TRANSFER_STATUS_F_ERROR }
    };

    for (size_t i = 0; i < RT_ELEMENTS(s_aStatusTests); ++i)
    {
        SHCLTRANSFERSTATUS const enmStatus = s_aStatusTests[i].enmStatus;
        RTTESTI_CHECK(ShClTransferStatusIsValid(enmStatus));
        RTTESTI_CHECK(ShClTransferStatusIsTerminal(enmStatus) == s_aStatusTests[i].fTerminal);

        bool const fFailureStatus =    enmStatus == SHCLTRANSFERSTATUS_KILLED
                                   || enmStatus == SHCLTRANSFERSTATUS_ERROR;
        RTTESTI_CHECK(ShClTransferStatusResultIsValid(enmStatus, VINF_SUCCESS)
                      == (   !fFailureStatus
                          && enmStatus != SHCLTRANSFERSTATUS_CANCELED));
        RTTESTI_CHECK(ShClTransferStatusResultIsValid(enmStatus, VERR_GENERAL_FAILURE) == fFailureStatus);
        RTTESTI_CHECK(ShClTransferStatusResultIsValid(enmStatus, VERR_CANCELLED)
                      == (fFailureStatus || enmStatus == SHCLTRANSFERSTATUS_CANCELED));

        for (size_t j = 0; j < RT_ELEMENTS(s_aStatusTests); ++j)
            RTTESTI_CHECK(ShClTransferStatusTransitionIsValid(enmStatus, s_aStatusTests[j].enmStatus)
                          == RT_BOOL(s_aStatusTests[i].fTransitions
                                     & TST_SHCL_TRANSFER_STATUS_F(s_aStatusTests[j].enmStatus)));
    }

    SHCLTRANSFERSTATUS const enmInvalid = UINT32_C(0xfeed);
    RTTESTI_CHECK(!ShClTransferStatusIsValid(SHCLTRANSFERSTATUS_32BIT_SIZE_HACK));
    RTTESTI_CHECK(!ShClTransferStatusIsValid(enmInvalid));
    RTTESTI_CHECK(!ShClTransferStatusIsTerminal(enmInvalid));
    RTTESTI_CHECK(!ShClTransferStatusResultIsValid(enmInvalid, VINF_SUCCESS));
    RTTESTI_CHECK(!ShClTransferStatusTransitionIsValid(enmInvalid, enmInvalid));
    RTTESTI_CHECK(!ShClTransferStatusTransitionIsValid(SHCLTRANSFERSTATUS_REQUESTED, enmInvalid));
}

/**
 * Tests zero-length object data chunk duplication.
 */
static void testTransferObjDataChunkDupZeroLength(void)
{
    RTTestISub("Testing zero-length transfer object data chunk duplication");

    SHCLOBJDATACHUNK Chunk;
    RT_ZERO(Chunk);
    Chunk.uHandle = 42;
    Chunk.pvData  = NULL;
    Chunk.cbData  = 0;

    PSHCLOBJDATACHUNK pDup = ShClTransferObjDataChunkDup(&Chunk);
    RTTESTI_CHECK_RETV(pDup != NULL);
    RTTESTI_CHECK(pDup->uHandle == Chunk.uHandle);
    RTTESTI_CHECK(pDup->pvData == NULL);
    RTTESTI_CHECK(pDup->cbData == 0);
    ShClTransferObjDataChunkFree(pDup);

    Chunk.cbData = 1;
    pDup = ShClTransferObjDataChunkDup(&Chunk);
    RTTESTI_CHECK(pDup == NULL);
}


static void testTransferContextIdentity(void)
{
    RTTestISub("Testing transfer context identity");

    SHCLTRANSFERCTX Ctx;
    int rc = ShClTransferCtxInit(&Ctx);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferCtxBeginSession(&Ctx, 7);
    RTTESTI_CHECK_RC_OK(rc);

    PSHCLTRANSFER pTransfer;
    rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTRANSFERID idTransfer = NIL_SHCLTRANSFERID;
    rc = ShClTransferCtxRegister(&Ctx, pTransfer, &idTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(idTransfer != NIL_SHCLTRANSFERID);
    RTTESTI_CHECK(ShClTransferGetSessionId(pTransfer) == 7);
    RTTESTI_CHECK(ShClTransferGetGeneration(pTransfer) != 0);
    RTTESTI_CHECK(ShClTransferGetGeneration(pTransfer) != NIL_SHCLTRANSFERGEN);

    SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 7, idTransfer, uGeneration) == pTransfer);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 8, idTransfer, uGeneration) == NULL);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 7, idTransfer, uGeneration + 1) == NULL);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 7, idTransfer + 1, uGeneration) == NULL);

    SHCLTRANSFERID idDuplicate = NIL_SHCLTRANSFERID;
    rc = ShClTransferCtxRegister(&Ctx, pTransfer, &idDuplicate);
    RTTESTI_CHECK_RC(rc, VERR_ALREADY_EXISTS);
    rc = ShClTransferCtxRegisterById(&Ctx, pTransfer, idTransfer + 1);
    RTTESTI_CHECK_RC(rc, VERR_ALREADY_EXISTS);

    rc = ShClTransferCtxUnregisterById(&Ctx, idTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    PSHCLTRANSFER pTransferReuse;
    rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransferReuse);
    RTTESTI_CHECK_RC_OK_RETV(rc);
    rc = ShClTransferCtxRegisterById(&Ctx, pTransferReuse, idTransfer);
    RTTESTI_CHECK_RC(rc, VERR_ALREADY_EXISTS);
    rc = ShClTransferDestroy(pTransferReuse);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferCtxBeginSession(&Ctx, 8);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransferReuse);
    RTTESTI_CHECK_RC_OK_RETV(rc);
    rc = ShClTransferCtxRegisterById(&Ctx, pTransferReuse, idTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(ShClTransferGetSessionId(pTransferReuse) == 8);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 7, idTransfer, uGeneration) == NULL);
    RTTESTI_CHECK(ShClTransferCtxGetTransferByKey(&Ctx, 8, idTransfer,
                                                  ShClTransferGetGeneration(pTransferReuse)) == pTransferReuse);

    rc = ShClTransferCtxUnregisterById(&Ctx, idTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    rc = ShClTransferDestroy(pTransferReuse);
    RTTESTI_CHECK_RC_OK(rc);

    ShClTransferCtxDestroy(&Ctx);
}

static void testTransferRootsSet(RTTEST hTest)
{
    RTTestISub("Testing setting transfer roots");

    /* Define the (valid) transfer root set. */
    RTCList<TESTTRANSFERROOTENTRY> lstBase;
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/sub1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file1.txt"));

    RTCList<TESTTRANSFERROOTENTRY> lstBreakout;
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VINF_SUCCESS);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("does-not-exist/file1.txt"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/./../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("../does-not-exist"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    RTCList<TESTTRANSFERROOTENTRY> lstBoundaryBase;
    lstBoundaryBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/file1.txt"));

    RTCList<TESTTRANSFERROOTENTRY> lstBoundaryBreakout;
    lstBoundaryBreakout.append(TESTTRANSFERROOTENTRY("my-transfer-10/file2.txt"));
    /* Sibling prefix confusion is rejected (my-transfer-1 vs my-transfer-10). */
    testTransferRootsSetSingle(hTest, lstBoundaryBase, lstBoundaryBreakout, VERR_PATH_DOES_NOT_START_WITH_ROOT);
}

static void testTransferObjOpenPrefixBoundary(RTTEST hTest)
{
    RTTestISub("Testing transfer object root prefix boundaries");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferObjOpenDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferObjOpenPrefixBoundary", szTestTransferObjOpenDir,
                           sizeof(szTestTransferObjOpenDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    RTCList<TESTTRANSFERROOTENTRY> lstRoots;
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/foo"));

    RTCList<TESTTRANSFERROOTENTRY> lstToExtendEmpty;

    char *pszRoots;
    rc = testAddRootEntries(hTest, szTestTransferObjOpenDir, lstRoots, lstToExtendEmpty, &pszRoots);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = testCreateFile(hTest, szTestTransferObjOpenDir, "my-transfer-1/foobar", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLOBJOPENCREATEPARMS openCreateParms;
    rc = ShClTransferObjOpenParmsInit(&openCreateParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, "foo");
    RTTESTI_CHECK_RC_OK(rc);

    /* Exact authorized root entry opens successfully. */
    SHCLOBJHANDLE hObj;
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferObjClose(pTransfer, hObj);
        RTTESTI_CHECK_RC_OK(rc);
    }

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, "foobar");
    RTTESTI_CHECK_RC_OK(rc);

    /* Sibling path sharing the same string prefix is not authorized. */
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC(rc, VERR_PATH_NOT_FOUND);

    ShClTransferObjOpenParmsDestroy(&openCreateParms);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

/**
 * Tests that resetting a transfer closes active object handles.
 *
 * @param   hTest       The test handle.
 */
static void testTransferResetClosesObjectHandles(RTTEST hTest)
{
    RTTestISub("Testing transfer reset closes object handles");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreateEx(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE,
                                  SHCL_TRANSFER_DEFAULT_MAX_LIST_HANDLES, 1 /* cMaxObjHandles */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferResetDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferResetHandleAccounting", szTestTransferResetDir,
                           sizeof(szTestTransferResetDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char *pszFilePathAbs = NULL;
    rc = testCreateFile(hTest, szTestTransferResetDir, "file1.txt", 0, 0, &pszFilePathAbs);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char *pszRoots = NULL;
    rc = RTStrAAppend(&pszRoots, pszFilePathAbs);
    RTTESTI_CHECK_RC_OK(rc);
    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);
    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    SHCLOBJOPENCREATEPARMS OpenParms;
    rc = ShClTransferObjOpenParmsInit(&OpenParms);
    RTTESTI_CHECK_RC_OK(rc);
    rc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, "file1.txt");
    RTTESTI_CHECK_RC_OK(rc);

    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
    rc = ShClTransferObjOpen(pTransfer, &OpenParms, &hObj);
    RTTESTI_CHECK_RC_OK(rc);

    ShClTransferReset(pTransfer);

    rc = ShClTransferObjClose(pTransfer, hObj);
    RTTESTI_CHECK_RC(rc, VERR_NOT_FOUND);

    ShClTransferObjOpenParmsDestroy(&OpenParms);
    RTStrFree(pszRoots);
    RTStrFree(pszFilePathAbs);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}


static void testTransferObjReadWriteValidation(void)
{
    RTTestISub("Testing transfer object read/write validation");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreateEx(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, 4 /* cbMaxChunkSize */,
                                  SHCL_TRANSFER_DEFAULT_MAX_LIST_HANDLES, SHCL_TRANSFER_DEFAULT_MAX_OBJ_HANDLES, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char abBuf[8];
    uint32_t cbActual = 0;

    /* Invalid read handles, pointers, sizes, chunks and flags are rejected with their precise status codes. */
    rc = ShClTransferObjRead(pTransfer, NIL_SHCLOBJHANDLE, abBuf, 1, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_HANDLE);
    rc = ShClTransferObjRead(pTransfer, 1, NULL, 1, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_POINTER);
    rc = ShClTransferObjRead(pTransfer, 1, abBuf, 0, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    rc = ShClTransferObjRead(pTransfer, 1, abBuf, sizeof(abBuf), 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_BUFFER_OVERFLOW);
    rc = ShClTransferObjRead(pTransfer, 1, abBuf, 1, 1 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    /* Invalid write handles, pointers, sizes, chunks and flags are rejected with their precise status codes. */
    rc = ShClTransferObjWrite(pTransfer, NIL_SHCLOBJHANDLE, abBuf, 1, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_HANDLE);
    rc = ShClTransferObjWrite(pTransfer, 1, NULL, 1, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_POINTER);
    rc = ShClTransferObjWrite(pTransfer, 1, abBuf, 0, 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    rc = ShClTransferObjWrite(pTransfer, 1, abBuf, sizeof(abBuf), 0 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_BUFFER_OVERFLOW);
    rc = ShClTransferObjWrite(pTransfer, 1, abBuf, 1, 1 /* fFlags */, &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testTransferListOpenEmptyPath(RTTEST hTest)
{
    RTTestISub("Testing transfer list empty-path rejection");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferListOpenDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferListOpenEmptyPath", szTestTransferListOpenDir,
                           sizeof(szTestTransferListOpenDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char szRootDir[RTPATH_MAX];
    rc = RTPathJoin(szRootDir, sizeof(szRootDir), szTestTransferListOpenDir, "my-transfer-1/dir");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = testCreateDir(hTest, szRootDir);
    RTTESTI_CHECK_RC_OK(rc);
    rc = testCreateFile(hTest, szTestTransferListOpenDir, "my-transfer-1/dir/file1.txt", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    char *pszRoots = RTStrDup(szRootDir);
    RTTESTI_CHECK_RETV(pszRoots);
    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLLISTOPENPARMS openParms;
    rc = ShClTransferListOpenParmsInit(&openParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(openParms.pszPath, openParms.cbPath, "");
    RTTESTI_CHECK_RC_OK(rc);
    rc = RTStrCopy(openParms.pszFilter, openParms.cbFilter, "");
    RTTESTI_CHECK_RC_OK(rc);

    /* Empty list-open paths must not enumerate the common root. */
    SHCLLISTHANDLE hList;
    rc = ShClTransferListOpen(pTransfer, &openParms, &hList);
    RTTESTI_CHECK_RC(rc, VERR_PATH_NOT_FOUND);

    ShClTransferListOpenParmsDestroy(&openParms);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

/**
 * Tests local-provider list handling for a single file root.
 *
 * @param   hTest               Test handle.
 */
static void testTransferListSingleFileRoot(RTTEST hTest)
{
    RTTestISub("Testing single-file transfer list handling");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferListDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferListSingleFileRoot", szTestTransferListDir, sizeof(szTestTransferListDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char *pszRootFileAbs = NULL;
    rc = testCreateFile(hTest, szTestTransferListDir, "my-transfer-1/file1.txt", 0, 0, &pszRootFileAbs);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char *pszRoots = RTStrDup(pszRootFileAbs);
    RTTESTI_CHECK_RETV(pszRoots);
    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLLISTOPENPARMS OpenParms;
    rc = ShClTransferListOpenParmsInit(&OpenParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, "file1.txt");
    RTTESTI_CHECK_RC_OK(rc);
    rc = RTStrCopy(OpenParms.pszFilter, OpenParms.cbFilter, "");
    RTTESTI_CHECK_RC_OK(rc);

    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
    rc = ShClTransferListOpen(pTransfer, &OpenParms, &hList);
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        SHCLLISTHDR Hdr;
        rc = ShClTransferListGetHeader(pTransfer, hList, &Hdr);
        RTTESTI_CHECK_RC_OK(rc);
        RTTESTI_CHECK(Hdr.cEntries == 1);

        SHCLLISTENTRY Entry;
        rc = ShClTransferListEntryInit(&Entry);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_SUCCESS(rc))
        {
            rc = ShClTransferListRead(pTransfer, hList, &Entry);
            RTTESTI_CHECK_RC_OK(rc);
            if (RT_SUCCESS(rc))
                RTTESTI_CHECK(strcmp(Entry.pszName, "file1.txt") == 0);
            ShClTransferListEntryDestroy(&Entry);
        }

        SHCLLISTENTRY WriteEntry;
        rc = ShClTransferListEntryInit(&WriteEntry);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_SUCCESS(rc))
        {
            rc = ShClTransferListWrite(pTransfer, hList, &WriteEntry);
            RTTESTI_CHECK_RC(rc, VERR_NOT_SUPPORTED);
            ShClTransferListEntryDestroy(&WriteEntry);
        }

        rc = ShClTransferListClose(pTransfer, hList);
        RTTESTI_CHECK_RC_OK(rc);
    }

    ShClTransferListOpenParmsDestroy(&OpenParms);
    RTStrFree(pszRootFileAbs);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}


static void testTransferSymlinkRejection(RTTEST hTest)
{
    RTTestISub("Testing transfer symlink rejection");

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    SHCLTXPROVIDER Provider;
    ShClTransferProviderLocalQueryInterface(&Provider);

    rc = ShClTransferSetProvider(pTransfer, &Provider);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferSymlinkDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferSymlinkRejection", szTestTransferSymlinkDir,
                           sizeof(szTestTransferSymlinkDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char szRootDir[RTPATH_MAX];
    rc = RTPathJoin(szRootDir, sizeof(szRootDir), szTestTransferSymlinkDir, "my-transfer-1/dir");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = testCreateDir(hTest, szRootDir);
    RTTESTI_CHECK_RC_OK(rc);

    char *pszTargetFileAbs = NULL;
    rc = testCreateFile(hTest, szTestTransferSymlinkDir, "my-transfer-1/target.txt", 0, 0, &pszTargetFileAbs);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char szLinkFile[RTPATH_MAX];
    rc = RTPathJoin(szLinkFile, sizeof(szLinkFile), szRootDir, "link-file");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = RTSymlinkCreate(szLinkFile, pszTargetFileAbs, RTSYMLINKTYPE_FILE, 0 /* fCreate */);
    if (RT_FAILURE(rc))
    {
        RTTestSkipped(hTest, "RTSymlinkCreate(file) failed: %Rrc", rc);
        RTStrFree(pszTargetFileAbs);
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
        return;
    }

    char szEscapeDir[RTPATH_MAX];
    rc = RTPathJoin(szEscapeDir, sizeof(szEscapeDir), szTestTransferSymlinkDir, "escape");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = testCreateDir(hTest, szEscapeDir);
    RTTESTI_CHECK_RC_OK(rc);
    rc = testCreateFile(hTest, szTestTransferSymlinkDir, "escape/secret.txt", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    char szLinkDir[RTPATH_MAX];
    rc = RTPathJoin(szLinkDir, sizeof(szLinkDir), szRootDir, "link-out");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = RTSymlinkCreate(szLinkDir, szEscapeDir, RTSYMLINKTYPE_DIR, 0 /* fCreate */);
    if (RT_FAILURE(rc))
    {
        RTTestSkipped(hTest, "RTSymlinkCreate(dir) failed: %Rrc", rc);
        RTTESTI_CHECK_RC_OK(RTSymlinkDelete(szLinkFile, 0 /* fDelete */));
        RTStrFree(pszTargetFileAbs);
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
        return;
    }

    char *pszRoots = RTStrDup(szRootDir);
    RTTESTI_CHECK_RETV(pszRoots);
    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLOBJOPENCREATEPARMS openCreateParms;
    rc = ShClTransferObjOpenParmsInit(&openCreateParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, "dir/link-file");
    RTTESTI_CHECK_RC_OK(rc);

    /* Final-component symlinks are rejected before file open. */
    SHCLOBJHANDLE hObj;
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC(rc, VERR_IS_A_SYMLINK);

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, "dir/link-out/secret.txt");
    RTTESTI_CHECK_RC_OK(rc);

    /* Intermediate directory symlink breakout is rejected. */
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC(rc, VERR_IS_A_SYMLINK);

    ShClTransferObjOpenParmsDestroy(&openCreateParms);
    RTStrFree(pszTargetFileAbs);
    RTTESTI_CHECK_RC_OK(RTSymlinkDelete(szLinkFile, 0 /* fDelete */));
    RTTESTI_CHECK_RC_OK(RTSymlinkDelete(szLinkDir, 0 /* fDelete */));

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testTransferObjOpen(RTTEST hTest)
{
    RTTestISub("Testing setting transfer object open");

    /* Define the (valid) transfer root set. */
    RTCList<TESTTRANSFERROOTENTRY> lstRoots;
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/file2..txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/file2...txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/sub1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file2..txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file2...txt"));

    testTransferObjOpenSingle(hTest, lstRoots, "file1.txt", VINF_SUCCESS);
    testTransferObjOpenSingle(hTest, lstRoots, "file2..txt", VINF_SUCCESS);
    testTransferObjOpenSingle(hTest, lstRoots, "file2...txt", VINF_SUCCESS);
    testTransferObjOpenSingle(hTest, lstRoots, "dir2/sub1/file2...txt", VINF_SUCCESS);
    testTransferObjOpenSingle(hTest, lstRoots, "does-not-exist.txt", VERR_PATH_NOT_FOUND);
    testTransferObjOpenSingle(hTest, lstRoots, "dir1/does-not-exist.txt", VERR_PATH_NOT_FOUND);
    testTransferObjOpenSingle(hTest, lstRoots, "../must-not-access-this.txt", VERR_INVALID_PARAMETER);
    testTransferObjOpenSingle(hTest, lstRoots, "dir1/../../must-not-access-this.txt", VERR_INVALID_PARAMETER);
}

int main(int argc, char *argv[])
{
    /*
     * Init the runtime, test and say hello.
     */
    const char *pcszExecName;
    NOREF(argc);
    pcszExecName = strrchr(argv[0], '/');
    pcszExecName = pcszExecName ? pcszExecName + 1 : argv[0];
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate(pcszExecName, &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /* For negative stuff that may assert: */
    bool const fMayPanic = RTAssertSetMayPanic(false);
    bool const fQuiet    = RTAssertSetQuiet(true);

    testPathSanitize();
    testEvents();
    testTransferBasics();
    testTransferValidation();
    testTransferObjDataChunkDupZeroLength();
    testTransferContextIdentity();
    testTransferResetClosesObjectHandles(hTest);
    testTransferRootsSet(hTest);
    testTransferObjOpenPrefixBoundary(hTest);
    testTransferObjReadWriteValidation();
    testTransferListOpenEmptyPath(hTest);
    testTransferListSingleFileRoot(hTest);
    testTransferSymlinkRejection(hTest);
    testTransferObjOpen(hTest);

    int rc = testRemoveTempDir(hTest);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    RTAssertSetMayPanic(fMayPanic);
    RTAssertSetQuiet(fQuiet);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}
