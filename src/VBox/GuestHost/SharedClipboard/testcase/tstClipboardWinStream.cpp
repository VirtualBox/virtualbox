/* $Id: tstClipboardWinStream.cpp 115132 2026-08-27 08:32:32Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Windows stream testcase.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/SharedClipboard-win.h>

#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/** @page pg_tstClipboardWinStream  Shared Clipboard Windows stream testcase
 *
 * This testcase exercises the Windows data object and IStream adapter.  It
 * verifies recursive directory enumeration, exact stream boundary cases, and
 * deterministic randomized reads crossing the default 64 KiB transfer-chunk
 * boundary.
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Size of the original boundary regression read. */
#define TST_WIN_STREAM_BOUNDARY_READ_SIZE       (_64K + 1)
/** Amount of data reserved for the final read past EOF. */
#define TST_WIN_STREAM_FINAL_DATA_SIZE          (_64K + 37)
/** Maximum randomized IStream read size. */
#define TST_WIN_STREAM_MAX_READ_SIZE            (_128K + 17)
/** Number of deterministic randomized IStream reads. */
#define TST_WIN_STREAM_RANDOM_READ_COUNT        32
/** Seed for the deterministic randomized reads. */
#define TST_WIN_STREAM_RANDOM_SEED              UINT64_C(0x4f1bbcdc676f2c35)
/** Maximum number of mock-provider reads recorded. */
#define TST_WIN_STREAM_MAX_PROVIDER_READS       256
/** Value used to detect writes beyond the returned data. */
#define TST_WIN_STREAM_BUFFER_FILL              UINT8_C(0xa5)
/** Object handle returned by the mock provider. */
#define TST_WIN_STREAM_OBJ_HANDLE               UINT64_C(1)

/** Explicit boundary read sizes, performed from unaligned stream offsets. */
static uint32_t const g_acbBoundaryReads[] =
{
    17,
    _64K - 1,
    _64K,
    _64K + 1,
    _128K - 1,
    _128K,
    _128K + 1
};


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** A recorded mock-provider read. */
typedef struct TSTWINSTREAMPROVIDERREAD
{
    /** Source offset before the read. */
    uint32_t                    offData;
    /** Requested byte count. */
    uint32_t                    cbRequested;
    /** Returned byte count. */
    uint32_t                    cbRead;
} TSTWINSTREAMPROVIDERREAD;

/** State for the mock transfer provider. */
typedef struct TSTWINSTREAMPROVIDER
{
    /** Deterministic source data. */
    uint8_t                    *pbData;
    /** Size of the source data. */
    uint32_t                    cbData;
    /** Current source offset. */
    uint32_t                    offData;
    /** Maximum transfer chunk size. */
    uint32_t                    cbMaxChunkSize;
    /** Result to return from object reads. */
    int                         rcRead;
    /** Recorded object reads. */
    TSTWINSTREAMPROVIDERREAD    aReads[TST_WIN_STREAM_MAX_PROVIDER_READS];
    /** Number of object opens. */
    uint32_t                    cOpens;
    /** Number of object reads. */
    uint32_t                    cReads;
    /** Number of object closes. */
    uint32_t                    cCloses;
    /** Whether the read trace overflowed. */
    bool                        fReadLogOverflow;
} TSTWINSTREAMPROVIDER;
/** Pointer to mock transfer provider state. */
typedef TSTWINSTREAMPROVIDER *PTSTWINSTREAMPROVIDER;

/** Transfer-end callback state for native data-object cancellation. */
typedef struct TSTWINDATAOBJECTEND
{
    /** Number of transfer-end callbacks. */
    uint32_t                    cCalls;
    /** Transfer supplied to the callback. */
    PSHCLTRANSFER               pTransfer;
    /** Transfer result supplied to the callback. */
    int                         rcTransfer;
} TSTWINDATAOBJECTEND;
/** Pointer to native data-object transfer-end callback state. */
typedef TSTWINDATAOBJECTEND *PTSTWINDATAOBJECTEND;

/** Test wrapper exposing data-object completion state setup and inspection. */
class TstWinDataObject : public ShClWinDataObject
{
public:
    using ShClWinDataObject::createFileGroupDescriptorFromTransfer;
    using ShClWinDataObject::readDir;

    /** Adds a synthetic descriptor entry. */
    int addEntry(const char *pszPath, RTFMODE fMode, RTFOFF cbObject)
    {
        FSOBJENTRY Entry;
        Entry.pszPath = RTStrDup(pszPath);
        AssertPtrReturn(Entry.pszPath, VERR_NO_MEMORY);
        RT_ZERO(Entry.objInfo);
        Entry.objInfo.Attr.fMode = fMode;
        Entry.objInfo.cbObject   = cbObject;

        lock();
        m_lstEntries.push_back(Entry);
        unlock();
        return VINF_SUCCESS;
    }

    /** Initializes per-descriptor completion tracking and enters Running state. */
    int startCompletionTracking(void)
    {
        lock();
        m_afObjCompleted.assign(m_lstEntries.size(), false);
        m_cFileEntries = 0;
        FsObjEntryList::const_iterator itEntry = m_lstEntries.cbegin();
        while (itEntry != m_lstEntries.end())
        {
            if (RTFS_IS_FILE(itEntry->objInfo.Attr.fMode))
                m_cFileEntries++;
            ++itEntry;
        }
        m_cFileEntriesCompleted = 0;
        int rc = setStatusLocked(Running);
        unlock();
        return rc;
    }

    /** Returns the current status and unique completed-file count. */
    Status queryCompletionStatus(size_t *pcCompleted, int *prcStatus = NULL)
    {
        lock();
        Status const enmStatus = m_enmStatus;
        *pcCompleted = m_cFileEntriesCompleted;
        if (prcStatus)
            *prcStatus = m_rcStatus;
        unlock();
        return enmStatus;
    }

    /** Returns the current COM reference count for focused lifetime checks. */
    ULONG queryRefCount(void) const
    {
        return m_lRefCount;
    }

    /** Reports a performed drop effect through IDataObject::SetData. */
    HRESULT reportDropEffect(DWORD dwEffect)
    {
        HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(DWORD));
        AssertReturn(hGlobal != NULL, E_OUTOFMEMORY);
        DWORD *pdwEffect = (DWORD *)GlobalLock(hGlobal);
        if (!pdwEffect)
        {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }
        *pdwEffect = dwEffect;
        GlobalUnlock(hGlobal);

        return reportDropEffectMedium(hGlobal, TRUE /* fRelease */);
    }

    /** Reports a caller-supplied performed-drop storage medium. */
    HRESULT reportDropEffectMedium(HGLOBAL hGlobal, BOOL fRelease)
    {

        FORMATETC FormatEtc;
        RT_ZERO(FormatEtc);
        FormatEtc.cfFormat = m_cfPerformedDropEffect;
        FormatEtc.dwAspect = DVASPECT_CONTENT;
        FormatEtc.lindex   = -1;
        FormatEtc.tymed    = TYMED_HGLOBAL;

        STGMEDIUM Medium;
        RT_ZERO(Medium);
        Medium.tymed   = TYMED_HGLOBAL;
        Medium.hGlobal = hGlobal;
        return SetData(&FormatEtc, &Medium, fRelease);
    }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/** Tests recursive enumeration of a directory containing only a nested directory. */
static void tstWinDataObjectNestedDirectoryEnumeration(void)
{
    RTTestISub("Nested directory enumeration");

    char szTempDir[RTPATH_MAX] = "";
    char szRootDir[RTPATH_MAX] = "";
    char szChildDir[RTPATH_MAX] = "";
    char szNestedFile[RTPATH_MAX] = "";
    bool fTempDirCreated = false;
    bool fRootDirCreated = false;
    bool fChildDirCreated = false;
    bool fNestedFileCreated = false;
    bool fDataObjectInitialized = false;
    RTFILE hFile = NIL_RTFILE;
    HGLOBAL hGlobal = NULL;
    PSHCLTRANSFER pTransfer = NULL;

    uint8_t bFrontendCtx = 0;
    TstWinDataObject DataObject;
    ShClWinDataObject::CALLBACKS Callbacks;
    RT_ZERO(Callbacks);

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);

    int rc;
    do
    {
        rc = RTPathTemp(szTempDir, sizeof(szTempDir));
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardWinStream-XXXXXX");
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = RTDirCreateTemp(szTempDir, 0700);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fTempDirCreated = true;

        rc = RTStrCopy(szRootDir, sizeof(szRootDir), szTempDir);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szRootDir, sizeof(szRootDir), "root");
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = RTDirCreate(szRootDir, 0700, 0);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fRootDirCreated = true;

        rc = RTStrCopy(szChildDir, sizeof(szChildDir), szRootDir);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szChildDir, sizeof(szChildDir), "child");
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = RTDirCreate(szChildDir, 0700, 0);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fChildDirCreated = true;

        rc = RTStrCopy(szNestedFile, sizeof(szNestedFile), szChildDir);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szNestedFile, sizeof(szNestedFile), "nested.bin");
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = RTFileOpen(&hFile, szNestedFile, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fNestedFileCreated = true;

        uint8_t const abContents[] = { 0x42, 0x69, 0x6e };
        rc = RTFileWrite(hFile, abContents, sizeof(abContents), NULL);
        int rc2 = RTFileClose(hFile);
        hFile = NIL_RTFILE;
        if (RT_SUCCESS(rc))
            rc = rc2;
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        RTTESTI_CHECK(ShClTransferProviderLocalQueryInterface(&Provider) != NULL);
        if (!Provider.Interface.pfnListOpen)
            break;
        rc = ShClTransferCreate(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, NULL, &pTransfer);
        if (RT_SUCCESS(rc))
            rc = ShClTransferSetProvider(pTransfer, &Provider);
        if (RT_SUCCESS(rc))
            rc = ShClTransferRootsSetFromPath(pTransfer, szRootDir);
        if (RT_SUCCESS(rc))
            rc = ShClTransferInit(pTransfer);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = DataObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fDataObjectInitialized = true;

        PCSHCLLISTENTRY pRootEntry = ShClTransferRootsEntryGet(pTransfer, 0);
        RTTESTI_CHECK(pRootEntry != NULL);
        if (!pRootEntry)
            break;
        bool const fRootEntryValid =    pRootEntry->pszName
                                    && pRootEntry->fInfo == VBOX_SHCL_INFO_F_FSOBJINFO
                                    && pRootEntry->pvInfo
                                    && pRootEntry->cbInfo == sizeof(SHCLFSOBJINFO);
        RTTESTI_CHECK(fRootEntryValid);
        if (!fRootEntryValid)
            break;

        PCSHCLFSOBJINFO pRootInfo = (PCSHCLFSOBJINFO)pRootEntry->pvInfo;
        rc = DataObject.addEntry(pRootEntry->pszName, pRootInfo->Attr.fMode, pRootInfo->cbObject);
        if (RT_SUCCESS(rc))
            rc = DataObject.readDir(pTransfer, Utf8Str(pRootEntry->pszName));
        if (RT_SUCCESS(rc))
            rc = DataObject.createFileGroupDescriptorFromTransfer(pTransfer, true /* fUnicode */, &hGlobal);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        FILEGROUPDESCRIPTORW const *pDescriptor = (FILEGROUPDESCRIPTORW const *)GlobalLock(hGlobal);
        RTTESTI_CHECK(pDescriptor != NULL);
        if (!pDescriptor)
            break;

        Utf8Str const strRoot(pRootEntry->pszName);
        Utf8Str const strChild = strRoot + Utf8Str("\\child");
        Utf8Str const strNested = strChild + Utf8Str("\\nested.bin");
        bool fFoundRoot = false;
        bool fFoundChild = false;
        bool fFoundNested = false;
        for (UINT i = 0; i < pDescriptor->cItems; ++i)
        {
            PCRTUTF16 const pwszName = (PCRTUTF16)pDescriptor->fgd[i].cFileName;
            fFoundRoot   |= RTUtf16CmpUtf8(pwszName, strRoot.c_str()) == 0;
            fFoundChild  |= RTUtf16CmpUtf8(pwszName, strChild.c_str()) == 0;
            fFoundNested |= RTUtf16CmpUtf8(pwszName, strNested.c_str()) == 0;
        }

        RTTESTI_CHECK_MSG(pDescriptor->cItems == 3, ("cItems=%RU32, expected 3\n", pDescriptor->cItems));
        RTTESTI_CHECK_MSG(fFoundRoot, ("Missing root descriptor '%s'\n", strRoot.c_str()));
        RTTESTI_CHECK_MSG(fFoundChild, ("Missing child directory descriptor '%s'\n", strChild.c_str()));
        RTTESTI_CHECK_MSG(fFoundNested, ("Missing nested file descriptor '%s'\n", strNested.c_str()));
        GlobalUnlock(hGlobal);
    } while (0);

    if (hGlobal)
        GlobalFree(hGlobal);
    if (fDataObjectInitialized)
        DataObject.Uninit();
    if (pTransfer)
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
    if (hFile != NIL_RTFILE)
        RTTESTI_CHECK_RC_OK(RTFileClose(hFile));
    if (fNestedFileCreated)
        RTTESTI_CHECK_RC_OK(RTFileDelete(szNestedFile));
    if (fChildDirCreated)
        RTTESTI_CHECK_RC_OK(RTDirRemove(szChildDir));
    if (fRootDirCreated)
        RTTESTI_CHECK_RC_OK(RTDirRemove(szRootDir));
    if (fTempDirCreated)
        RTTESTI_CHECK_RC_OK(RTDirRemove(szTempDir));
}


/** Records a native data-object transfer-end callback. */
static DECLCALLBACK(int) tstWinDataObjectTransferEnd(ShClWinDataObject::PCALLBACKCTX pCallbackCtx,
                                                     PSHCLTRANSFER pTransfer, int rcTransfer)
{
    PTSTWINDATAOBJECTEND pEnd = (PTSTWINDATAOBJECTEND)pCallbackCtx->pvUser;
    pEnd->cCalls++;
    pEnd->pTransfer  = pTransfer;
    pEnd->rcTransfer = rcTransfer;
    return VINF_SUCCESS;
}

/** Tests Windows filename sanitization and transfer-list name validation. */
static void tstWinPathSanitizeFilename(void)
{
    static const char * const s_apszValid[] =
    {
        "test-64KiB-name-url-#%+&=,.bin",
        "unicode-\xed\x9e\xb0-\xee\x80\x80-\xf0\x9f\x98\x80.bin",
        "COM0.txt",
        "COM10.txt",
        "LPT0.txt",
        "CONSOLE.txt",
        "NULL.txt"
    };
    static const char * const s_apszReserved[] =
    {
        "CON",
        "con.txt",
        "PRN.tar.gz",
        "AUX",
        "NUL.txt",
        "COM1",
        "com9.log",
        "LPT1",
        "lpt9.txt",
        "COM\xc2\xb9.txt",
        "LPT\xc2\xb2",
        "COM\xc2\xb3.bin",
        "directory/CON.txt"
    };
    char szPath[128];

    RTTestISub("Windows filename sanitization");
    for (size_t i = 0; i < RT_ELEMENTS(s_apszValid); i++)
    {
        RTTESTI_CHECK_RC_OK(RTStrCopy(szPath, sizeof(szPath), s_apszValid[i]));
        RTTESTI_CHECK_RC_OK(ShClPathSanitize(szPath, sizeof(szPath)));
        RTTESTI_CHECK_MSG(!strcmp(szPath, s_apszValid[i]),
                          ("Valid name '%s' was changed to '%s'\n", s_apszValid[i], szPath));
        RTTESTI_CHECK_RC_OK(ShClTransferValidatePath(s_apszValid[i], false /* fMustExist */));
    }

    for (size_t i = 0; i < RT_ELEMENTS(s_apszReserved); i++)
    {
        RTTESTI_CHECK_RC_OK(RTStrCopy(szPath, sizeof(szPath), s_apszReserved[i]));
        RTTESTI_CHECK_RC_OK(ShClPathSanitize(szPath, sizeof(szPath)));
        RTTESTI_CHECK_MSG(strcmp(szPath, s_apszReserved[i]),
                          ("Reserved name '%s' was not changed\n", s_apszReserved[i]));
        RTTESTI_CHECK_RC(ShClTransferValidatePath(s_apszReserved[i], false /* fMustExist */), VERR_INVALID_PARAMETER);
    }
}


/** Tests transfer-list name length and termination invariants. */
static void tstTransferListEntryNameValidation(void)
{
    RTTestISub("Transfer-list entry name validation");

    char szValid[] = "valid.bin";
    RTTESTI_CHECK_RC(ShClTransferValidatePathEx(NULL, sizeof(szValid), false /* fMustExist */), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC_OK(ShClTransferValidatePathEx(szValid, sizeof(szValid), false /* fMustExist */));
    RTTESTI_CHECK_RC(ShClTransferValidatePathEx(szValid, 0, false /* fMustExist */), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(ShClTransferValidatePathEx(szValid, 1, false /* fMustExist */), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(ShClTransferValidatePathEx(szValid, sizeof(szValid) + 1, false /* fMustExist */),
                     VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(ShClTransferValidatePathEx(szValid, SHCLLISTENTRY_MAX_NAME + 1, false /* fMustExist */),
                     VERR_INVALID_PARAMETER);

    char szEmpty[] = "";
    RTTESTI_CHECK_RC_OK(ShClTransferValidatePathEx(szEmpty, sizeof(szEmpty), false /* fMustExist */));

    SHCLLISTENTRY Entry;
    RT_ZERO(Entry);
    Entry.pszName = szValid;
    Entry.cbName  = sizeof(szValid);
    RTTESTI_CHECK(ShClTransferListEntryIsValid(&Entry));

    Entry.pszName = szEmpty;
    Entry.cbName  = sizeof(szEmpty);
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));
    Entry.pszName = szValid;

    Entry.cbName = 0;
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    Entry.cbName = 1;
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    Entry.cbName = sizeof(szValid) + 1;
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    Entry.cbName = SHCLLISTENTRY_MAX_NAME + 1;
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    char szEmbeddedNul[] = { 'n', 'a', 'm', 'e', '\0', 'x', '\0' };
    Entry.pszName = szEmbeddedNul;
    Entry.cbName  = sizeof(szEmbeddedNul);
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    char szNoTerminator[] = { 'n', 'a', 'm', 'e' };
    Entry.pszName = szNoTerminator;
    Entry.cbName  = sizeof(szNoTerminator);
    RTTESTI_CHECK(!ShClTransferListEntryIsValid(&Entry));

    char szTooLong[SHCLLISTENTRY_MAX_NAME];
    memset(szTooLong, 'a', sizeof(szTooLong));
    RTTESTI_CHECK_RC(ShClTransferListEntryInitEx(&Entry, VBOX_SHCL_INFO_F_NONE, szTooLong,
                                                 NULL /* pvInfo */, 0 /* cbInfo */), VERR_INVALID_PARAMETER);

    szTooLong[sizeof(szTooLong) - 1] = '\0';
    RTTESTI_CHECK_RC_OK(ShClTransferListEntryInitEx(&Entry, VBOX_SHCL_INFO_F_NONE, szTooLong,
                                                    NULL /* pvInfo */, 0 /* cbInfo */));
    RTTESTI_CHECK(Entry.cbName == SHCLLISTENTRY_MAX_NAME);
    ShClTransferListEntryDestroy(&Entry);

    RTTESTI_CHECK_RC_OK(ShClTransferListEntryInitEx(&Entry, VBOX_SHCL_INFO_F_NONE, szValid,
                                                    NULL /* pvInfo */, 0 /* cbInfo */));
    RTTESTI_CHECK(Entry.cbName == sizeof(szValid));
    ShClTransferListEntryDestroy(&Entry);
}


/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) tstWinStreamProviderRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    return VINF_SUCCESS;
}


/** @copydoc SHCLTXPROVIDERIFACE::pfnObjOpen */
static DECLCALLBACK(int) tstWinStreamProviderObjOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms,
                                                     PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,        VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTWINSTREAMPROVIDER), VERR_INVALID_PARAMETER);

    PTSTWINSTREAMPROVIDER const pThis = (PTSTWINSTREAMPROVIDER)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    pThis->offData = 0;
    pThis->cOpens++;
    *phObj = TST_WIN_STREAM_OBJ_HANDLE;
    return VINF_SUCCESS;
}


/** @copydoc SHCLTXPROVIDERIFACE::pfnObjRead */
static DECLCALLBACK(int) tstWinStreamProviderObjRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData,
                                                     uint32_t cbData, uint32_t fFlags, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,  VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTWINSTREAMPROVIDER), VERR_INVALID_PARAMETER);
    AssertReturn(hObj == TST_WIN_STREAM_OBJ_HANDLE, VERR_INVALID_HANDLE);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    PTSTWINSTREAMPROVIDER const pThis = (PTSTWINSTREAMPROVIDER)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pThis->pbData, VERR_INVALID_POINTER);
    AssertReturn(pThis->offData <= pThis->cbData, VERR_OUT_OF_RANGE);
    AssertReturn(cbData <= pThis->cbMaxChunkSize, VERR_BUFFER_OVERFLOW);

    if (RT_FAILURE(pThis->rcRead))
    {
        pThis->cReads++;
        return pThis->rcRead;
    }

    uint32_t const offData     = pThis->offData;
    uint32_t const cbRemaining = pThis->cbData - offData;
    uint32_t const cbRead = RT_MIN(cbData, cbRemaining);

    uint32_t const iRead = pThis->cReads++;
    if (iRead < RT_ELEMENTS(pThis->aReads))
    {
        pThis->aReads[iRead].offData     = offData;
        pThis->aReads[iRead].cbRequested = cbData;
        pThis->aReads[iRead].cbRead      = cbRead;
    }
    else
        pThis->fReadLogOverflow = true;

    memcpy(pvData, &pThis->pbData[offData], cbRead);
    pThis->offData += cbRead;

    if (pcbRead)
        *pcbRead = cbRead;
    return VINF_SUCCESS;
}


/** @copydoc SHCLTXPROVIDERIFACE::pfnObjClose */
static DECLCALLBACK(int) tstWinStreamProviderObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTWINSTREAMPROVIDER), VERR_INVALID_PARAMETER);
    AssertReturn(hObj == TST_WIN_STREAM_OBJ_HANDLE, VERR_INVALID_HANDLE);

    PTSTWINSTREAMPROVIDER const pThis = (PTSTWINSTREAMPROVIDER)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    pThis->cCloses++;
    return VINF_SUCCESS;
}


/**
 * Performs and validates one non-empty IStream read.
 */
static bool tstWinStreamReadAndCheck(IStream *pStream, PTSTWINSTREAMPROVIDER pProvider, uint8_t *pbBuffer,
                                     uint32_t cbBuffer, uint32_t *poffExpected, uint32_t cbRequest, uint32_t iStreamRead)
{
    AssertPtrReturn(pStream,      false);
    AssertPtrReturn(pProvider,    false);
    AssertPtrReturn(pbBuffer,     false);
    AssertPtrReturn(poffExpected, false);
    AssertReturn(cbRequest > 0, false);
    AssertReturn(cbBuffer > cbRequest, false);
    AssertReturn(*poffExpected <= pProvider->cbData, false);

    uint32_t const offExpected = *poffExpected;
    uint32_t const cbExpected  = RT_MIN(cbRequest, pProvider->cbData - offExpected);
    HRESULT const hrcExpected  = cbExpected == cbRequest ? S_OK : S_FALSE;
    uint32_t const iProviderReadFirst = pProvider->cReads;

    memset(pbBuffer, TST_WIN_STREAM_BUFFER_FILL, cbRequest + 1);

    ULONG cbRead = UINT32_MAX;
    HRESULT const hrc = pStream->Read(pbBuffer, cbRequest, &cbRead);
    RTTESTI_CHECK_MSG(hrc == hrcExpected,
                      ("read=%RU32 off=%RU32 cbRequest=%RU32: hrc=%Rhrc, expected %Rhrc\n",
                       iStreamRead, offExpected, cbRequest, hrc, hrcExpected));
    RTTESTI_CHECK_MSG(cbRead == cbExpected,
                      ("read=%RU32 off=%RU32 cbRequest=%RU32: cbRead=%RU32, expected %RU32\n",
                       iStreamRead, offExpected, cbRequest, cbRead, cbExpected));
    RTTESTI_CHECK_MSG(memcmp(pbBuffer, &pProvider->pbData[offExpected], cbExpected) == 0,
                      ("read=%RU32 off=%RU32 cbRequest=%RU32: data mismatch\n",
                       iStreamRead, offExpected, cbRequest));

    bool fTailUntouched = true;
    for (uint32_t off = cbExpected; off <= cbRequest; off++)
        if (pbBuffer[off] != TST_WIN_STREAM_BUFFER_FILL)
        {
            fTailUntouched = false;
            break;
        }
    RTTESTI_CHECK_MSG(fTailUntouched,
                      ("read=%RU32 off=%RU32 cbRequest=%RU32: buffer overwritten past returned data\n",
                       iStreamRead, offExpected, cbRequest));

    uint32_t const cProviderReadsExpected = cbExpected
                                          ? (cbExpected + pProvider->cbMaxChunkSize - 1) / pProvider->cbMaxChunkSize
                                          : 0;
    uint32_t const cProviderReadsActual = pProvider->cReads - iProviderReadFirst;
    RTTESTI_CHECK_MSG(cProviderReadsActual == cProviderReadsExpected,
                      ("read=%RU32 off=%RU32 cbRequest=%RU32: provider reads=%RU32, expected %RU32\n",
                       iStreamRead, offExpected, cbRequest, cProviderReadsActual, cProviderReadsExpected));
    RTTESTI_CHECK_MSG(!pProvider->fReadLogOverflow,
                      ("read=%RU32: provider read trace overflowed\n", iStreamRead));

    if (   !pProvider->fReadLogOverflow
        && cProviderReadsActual == cProviderReadsExpected
        && pProvider->cReads <= RT_ELEMENTS(pProvider->aReads))
    {
        uint32_t offTrace = offExpected;
        uint32_t cbLeft   = cbExpected;
        for (uint32_t i = 0; i < cProviderReadsExpected; i++)
        {
            uint32_t const cbChunkExpected = RT_MIN(cbLeft, pProvider->cbMaxChunkSize);
            TSTWINSTREAMPROVIDERREAD const *pRead = &pProvider->aReads[iProviderReadFirst + i];
            RTTESTI_CHECK_MSG(pRead->offData == offTrace,
                              ("read=%RU32 chunk=%RU32: offData=%RU32, expected %RU32\n",
                               iStreamRead, i, pRead->offData, offTrace));
            RTTESTI_CHECK_MSG(pRead->cbRequested == cbChunkExpected,
                              ("read=%RU32 chunk=%RU32: cbRequested=%RU32, expected %RU32\n",
                               iStreamRead, i, pRead->cbRequested, cbChunkExpected));
            RTTESTI_CHECK_MSG(pRead->cbRead == cbChunkExpected,
                              ("read=%RU32 chunk=%RU32: cbRead=%RU32, expected %RU32\n",
                               iStreamRead, i, pRead->cbRead, cbChunkExpected));
            offTrace += cbChunkExpected;
            cbLeft   -= cbChunkExpected;
        }
    }

    bool const fSucceeded = hrc == hrcExpected && cbRead == cbExpected;
    if (fSucceeded)
        *poffExpected += cbExpected;
    return fSucceeded;
}


/** Tests out-of-order and duplicate FILEGROUPDESCRIPTOR completion reports. */
static void tstWinDataObjectCompletionOrder(void)
{
    RTTestISub("Out-of-order stream completion");

    TstWinDataObject DataObject;
    ShClWinDataObject::CALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    uint8_t bFrontendCtx = 0;
    int rc = DataObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = DataObject.addEntry("first.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = DataObject.addEntry("directory", RTFS_TYPE_DIRECTORY, 0);
    if (RT_SUCCESS(rc))
        rc = DataObject.addEntry("second.bin", RTFS_TYPE_FILE, 2);
    if (RT_SUCCESS(rc))
        rc = DataObject.addEntry("empty.bin", RTFS_TYPE_FILE, 0);
    if (RT_SUCCESS(rc))
        rc = DataObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        RTTESTI_CHECK_RC_OK(DataObject.SetFileCompleted(2));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Running);
        RTTESTI_CHECK(cCompleted == 1);

        RTTESTI_CHECK_RC_OK(DataObject.SetFileCompleted(2));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Running);
        RTTESTI_CHECK(cCompleted == 1);

        RTTESTI_CHECK_RC(DataObject.SetFileCompleted(1), VERR_INVALID_PARAMETER);
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Running);
        RTTESTI_CHECK(cCompleted == 1);

        RTTESTI_CHECK_RC_OK(DataObject.SetFileCompleted(0));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Running);
        RTTESTI_CHECK(cCompleted == 2);

        RTTESTI_CHECK_RC_OK(DataObject.SetFileCompleted(3));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 3);

        RTTESTI_CHECK_RC_OK(DataObject.SetFileCompleted(0));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 3);

        RTTESTI_CHECK_RC_OK(DataObject.SetStatus(ShClWinDataObject::Canceled));
        RTTESTI_CHECK_RC_OK(DataObject.SetStatus(ShClWinDataObject::Error, VERR_READ_ERROR));
        RTTESTI_CHECK(DataObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
    }


    RTTestISub("Performed drop effect completion");

    TstWinDataObject DirectoryObject;
    RT_ZERO(Callbacks);
    rc = DirectoryObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = DirectoryObject.addEntry("directory", RTFS_TYPE_DIRECTORY, 0);
    if (RT_SUCCESS(rc))
        rc = DirectoryObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        RTTESTI_CHECK(DirectoryObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Running);
        RTTESTI_CHECK(DirectoryObject.reportDropEffect(DROPEFFECT_COPY) == S_OK);
        RTTESTI_CHECK(DirectoryObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 0);
    }

    TstWinDataObject MixedObject;
    RT_ZERO(Callbacks);
    rc = MixedObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = MixedObject.addEntry("file.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = MixedObject.addEntry("directory", RTFS_TYPE_DIRECTORY, 0);
    if (RT_SUCCESS(rc))
        rc = MixedObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        RTTESTI_CHECK(MixedObject.reportDropEffect(DROPEFFECT_COPY) == S_OK);
        RTTESTI_CHECK(MixedObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 0);
    }

    TstWinDataObject DroppedObject;
    RT_ZERO(Callbacks);
    rc = DroppedObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = DroppedObject.addEntry("file.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = DroppedObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        RTTESTI_CHECK(DroppedObject.reportDropEffect(DROPEFFECT_NONE) == S_OK);
        RTTESTI_CHECK(DroppedObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Canceled);
        RTTESTI_CHECK(cCompleted == 0);

        RTTESTI_CHECK(DroppedObject.reportDropEffectMedium(NULL, FALSE /* fRelease */) == DV_E_STGMEDIUM);

        HGLOBAL hSmall = GlobalAlloc(GHND, sizeof(uint8_t));
        RTTESTI_CHECK(hSmall != NULL);
        if (hSmall)
        {
            RTTESTI_CHECK(DroppedObject.reportDropEffectMedium(hSmall, FALSE /* fRelease */) == DV_E_STGMEDIUM);
            GlobalFree(hSmall);
        }
    }


    RTTestISub("Canceled state remains terminal");

    TstWinDataObject CanceledObject;
    RT_ZERO(Callbacks);
    rc = CanceledObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = CanceledObject.addEntry("file.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = CanceledObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        int rcStatus = VERR_INTERNAL_ERROR;
        RTTESTI_CHECK_RC_OK(CanceledObject.SetStatus(ShClWinDataObject::Canceled));
        RTTESTI_CHECK_RC_OK(CanceledObject.SetStatus(ShClWinDataObject::Error, VERR_READ_ERROR));
        RTTESTI_CHECK_RC(CanceledObject.SetFileCompleted(0), VERR_STATE_CHANGED);
        RTTESTI_CHECK(CanceledObject.queryCompletionStatus(&cCompleted, &rcStatus) == ShClWinDataObject::Canceled);
        RTTESTI_CHECK_RC_OK(rcStatus);
        RTTESTI_CHECK(cCompleted == 0);
    }


    RTTestISub("Error state remains terminal");

    TstWinDataObject ErrorObject;
    RT_ZERO(Callbacks);
    rc = ErrorObject.Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = ErrorObject.addEntry("file.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = ErrorObject.startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        size_t cCompleted = 0;
        int rcStatus = VINF_SUCCESS;
        RTTESTI_CHECK_RC_OK(ErrorObject.SetStatus(ShClWinDataObject::Error, VERR_READ_ERROR));
        RTTESTI_CHECK_RC_OK(ErrorObject.SetStatus(ShClWinDataObject::Error, VERR_WRITE_ERROR));
        RTTESTI_CHECK_RC_OK(ErrorObject.SetStatus(ShClWinDataObject::Completed));
        RTTESTI_CHECK(ErrorObject.queryCompletionStatus(&cCompleted, &rcStatus) == ShClWinDataObject::Error);
        RTTESTI_CHECK_RC(rcStatus, VERR_READ_ERROR);
        RTTESTI_CHECK_RC_OK(ErrorObject.SetStatus(ShClWinDataObject::Uninitialized));
        RTTESTI_CHECK(ErrorObject.queryCompletionStatus(&cCompleted) == ShClWinDataObject::Uninitialized);
    }
}


/** Tests that Explorer-native cancellation immediately ends the native transfer. */
static void tstWinDataObjectNativeCancellation(void)
{
    RTTestISub("Native data-object cancellation");

    TSTWINDATAOBJECTEND EndCtx;
    RT_ZERO(EndCtx);

    ShClWinDataObject::CALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnTransferEnd = tstWinDataObjectTransferEnd;

    PSHCLTRANSFER pTransfer = NULL;
    TstWinDataObject *pDataObject = NULL;

    int rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                NULL /* pCallbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        ShClWinTransferCtx *pWinTransferCtx = new ShClWinTransferCtx();
        RTTESTI_CHECK(pWinTransferCtx != NULL);
        if (pWinTransferCtx)
        {
            rc = RTCritSectInit(&pWinTransferCtx->CritSect);
            if (RT_SUCCESS(rc))
            {
                pTransfer->pvUser = pWinTransferCtx;
                pTransfer->cbUser = sizeof(*pWinTransferCtx);
            }
            else
                delete pWinTransferCtx;
        }
        else
            rc = VERR_NO_MEMORY;
        RTTESTI_CHECK_RC_OK(rc);
    }
    if (RT_SUCCESS(rc))
    {
        pDataObject = new TstWinDataObject();
        RTTESTI_CHECK(pDataObject != NULL);
        if (pDataObject)
        {
            pDataObject->AddRef(); /* Testcase reference. */
            rc = pDataObject->Init((PSHCLCONTEXT)&EndCtx, &Callbacks);
            RTTESTI_CHECK_RC_OK(rc);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        rc = pDataObject->SetTransfer(pTransfer);
        RTTESTI_CHECK_RC_OK(rc);
    }
    if (RT_SUCCESS(rc))
        rc = pDataObject->addEntry("cancel.bin", RTFS_TYPE_FILE, 1);
    if (RT_SUCCESS(rc))
        rc = pDataObject->startCompletionTracking();
    RTTESTI_CHECK_RC_OK(rc);

    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(pDataObject->reportDropEffect(DROPEFFECT_NONE) == S_OK);
        RTTESTI_CHECK(ShClTransferGetStatus(pTransfer) == SHCLTRANSFERSTATUS_CANCELED);
        RTTESTI_CHECK(EndCtx.cCalls == 1);
        RTTESTI_CHECK(EndCtx.pTransfer == pTransfer);
        RTTESTI_CHECK_RC(EndCtx.rcTransfer, VERR_CANCELLED);

        size_t cCompleted = 0;
        RTTESTI_CHECK(pDataObject->queryCompletionStatus(&cCompleted) == ShClWinDataObject::Canceled);
        RTTESTI_CHECK(cCompleted == 0);

        RTTESTI_CHECK(pDataObject->reportDropEffect(DROPEFFECT_NONE) == S_OK);
        RTTESTI_CHECK(EndCtx.cCalls == 1);
    }

    if (pDataObject)
    {
        RTTESTI_CHECK_RC_OK(pDataObject->SetTransfer(NULL));
        pDataObject->Release();
    }
    if (pTransfer)
    {
        ShClWinTransferCtx *pWinTransferCtx = (ShClWinTransferCtx *)pTransfer->pvUser;
        if (pWinTransferCtx)
        {
            RTTESTI_CHECK_RC_OK(RTCritSectDelete(&pWinTransferCtx->CritSect));
            delete pWinTransferCtx;
            pTransfer->pvUser = NULL;
            pTransfer->cbUser = 0;
        }
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
    }
}


/**
 * Verifies that invalidating a stream after transfer cancellation performs no
 * further remote object operation.
 */
static void tstWinStreamCanceledInvalidate(void)
{
    RTTestISub("Canceled stream invalidation");

    uint8_t abData[] = { 0x12, 0x34 };
    TSTWINSTREAMPROVIDER ProviderCtx;
    RT_ZERO(ProviderCtx);
    ProviderCtx.pbData         = abData;
    ProviderCtx.cbData         = sizeof(abData);
    ProviderCtx.cbMaxChunkSize = _64K;

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    Provider.Interface.pfnRootListRead = tstWinStreamProviderRootListRead;
    Provider.Interface.pfnObjOpen      = tstWinStreamProviderObjOpen;
    Provider.Interface.pfnObjRead      = tstWinStreamProviderObjRead;
    Provider.Interface.pfnObjClose     = tstWinStreamProviderObjClose;
    Provider.pvUser                    = &ProviderCtx;
    Provider.cbUser                    = sizeof(ProviderCtx);

    PSHCLTRANSFER pTransfer = NULL;
    TstWinDataObject *pParent = NULL;
    IStream *pStream = NULL;

    int rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                NULL /* pCallbacks */, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = ShClTransferSetProvider(pTransfer, &Provider);
    if (RT_SUCCESS(rc))
        rc = ShClTransferRootListRead(pTransfer);
    if (RT_SUCCESS(rc))
        rc = ShClTransferInit(pTransfer);
    if (RT_SUCCESS(rc))
        rc = ShClTransferStart(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    uint8_t bFrontendCtx = 0;
    if (RT_SUCCESS(rc))
    {
        pParent = new TstWinDataObject();
        RTTESTI_CHECK(pParent != NULL);
        if (pParent)
        {
            pParent->AddRef();
            ShClWinDataObject::CALLBACKS Callbacks;
            RT_ZERO(Callbacks);
            rc = pParent->Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        SHCLFSOBJINFO ObjInfo;
        RT_ZERO(ObjInfo);
        ObjInfo.Attr.fMode = RTFS_TYPE_FILE;
        ObjInfo.cbObject   = sizeof(abData);
        HRESULT const hrc = ShClWinStreamImpl::Create(pParent, pTransfer, 0 /* uObjIdx */,
                                                       Utf8Str("canceled.bin"), &ObjInfo, &pStream);
        RTTESTI_CHECK_MSG(hrc == S_OK, ("creating stream failed: %Rhrc\n", hrc));
        if (FAILED(hrc))
            rc = VERR_GENERAL_FAILURE;
    }

    if (RT_SUCCESS(rc))
    {
        uint8_t bRead = 0;
        ULONG cbRead = 0;
        HRESULT const hrc = pStream->Read(&bRead, 1, &cbRead);
        RTTESTI_CHECK_MSG(hrc == S_OK, ("initial stream read failed: %Rhrc\n", hrc));
        RTTESTI_CHECK(cbRead == 1);
        RTTESTI_CHECK(bRead == abData[0]);
        RTTESTI_CHECK(ProviderCtx.cOpens == 1);
        RTTESTI_CHECK(ProviderCtx.cCloses == 0);
        RTTESTI_CHECK(pParent->queryRefCount() == 2);

        ProviderCtx.rcRead = VERR_CANCELLED;
        cbRead = UINT32_MAX;
        HRESULT const hrcCanceled = pStream->Read(&bRead, 1, &cbRead);
        RTTESTI_CHECK_MSG(hrcCanceled == COPYENGINE_E_USER_CANCELLED,
                          ("canceled read returned %Rhrc\n", hrcCanceled));
        RTTESTI_CHECK(cbRead == 0);
        size_t cCompleted = UINT32_MAX;
        RTTESTI_CHECK(pParent->queryCompletionStatus(&cCompleted) == ShClWinDataObject::Canceled);
        ProviderCtx.rcRead = VINF_SUCCESS;

        RTTESTI_CHECK_RC_OK(ShClTransferCancel(pTransfer));
        RTTESTI_CHECK(ShClTransferGetStatus(pTransfer) == SHCLTRANSFERSTATUS_CANCELED);

        uint32_t const cOpensBeforeInvalidate = ProviderCtx.cOpens;
        uint32_t const cReadsBeforeInvalidate = ProviderCtx.cReads;
        uint32_t const cClosesBeforeInvalidate = ProviderCtx.cCloses;
        static_cast<ShClWinStreamImpl *>(pStream)->Invalidate();
        RTTESTI_CHECK(ProviderCtx.cOpens == cOpensBeforeInvalidate);
        RTTESTI_CHECK(ProviderCtx.cReads == cReadsBeforeInvalidate);
        RTTESTI_CHECK_MSG(ProviderCtx.cCloses == cClosesBeforeInvalidate,
                          ("terminal invalidation issued %RU32 remote close(s)\n", ProviderCtx.cCloses));
        RTTESTI_CHECK(ASMAtomicReadU32(&pTransfer->cRefs) == 0);
        RTTESTI_CHECK(pParent->queryRefCount() == 1);

        static_cast<ShClWinStreamImpl *>(pStream)->Invalidate();
        RTTESTI_CHECK(ProviderCtx.cCloses == 0);

        cbRead = UINT32_MAX;
        RTTESTI_CHECK(pStream->Read(&bRead, 1, &cbRead) == COPYENGINE_E_USER_CANCELLED);
        RTTESTI_CHECK(cbRead == 0);
        RTTESTI_CHECK(ProviderCtx.cReads == cReadsBeforeInvalidate);
    }

    if (pStream)
        pStream->Release();
    if (pParent)
        pParent->Release();
    if (pTransfer)
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
}


/**
 * Tests varied IStream reads which cross the default chunk boundary.
 */
static void tstWinStreamReadsAcrossChunkBoundaries(void)
{
    RTTestISub("64 KiB + 1 byte IStream read");

    RTRAND hRand = NIL_RTRAND;
    uint32_t acbRandomReads[TST_WIN_STREAM_RANDOM_READ_COUNT];
    RT_ZERO(acbRandomReads);

    int rc = RTRandAdvCreateParkMiller(&hRand);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        rc = RTRandAdvSeed(hRand, TST_WIN_STREAM_RANDOM_SEED);
        RTTESTI_CHECK_RC_OK(rc);
    }

    uint64_t cbData = TST_WIN_STREAM_BOUNDARY_READ_SIZE + TST_WIN_STREAM_FINAL_DATA_SIZE;
    for (size_t i = 0; i < RT_ELEMENTS(g_acbBoundaryReads); i++)
        cbData += g_acbBoundaryReads[i];
    if (RT_SUCCESS(rc))
    {
        for (size_t i = 0; i < RT_ELEMENTS(acbRandomReads); i++)
        {
            acbRandomReads[i] = RTRandAdvU32Ex(hRand, 1, TST_WIN_STREAM_MAX_READ_SIZE);
            cbData += acbRandomReads[i];
        }
    }
    RTTESTI_CHECK_MSG(cbData <= UINT32_MAX, ("cbData=%RU64\n", cbData));
    if (cbData > UINT32_MAX)
        rc = VERR_OUT_OF_RANGE;

    TSTWINSTREAMPROVIDER ProviderCtx;
    RT_ZERO(ProviderCtx);
    ProviderCtx.cbData = (uint32_t)cbData;

    uint8_t *pbBuffer = NULL;
    if (RT_SUCCESS(rc))
    {
        ProviderCtx.pbData = (uint8_t *)RTMemAlloc(ProviderCtx.cbData);
        pbBuffer = (uint8_t *)RTMemAlloc(TST_WIN_STREAM_MAX_READ_SIZE + 1);
        RTTESTI_CHECK(ProviderCtx.pbData != NULL);
        RTTESTI_CHECK(pbBuffer != NULL);
        if (!ProviderCtx.pbData || !pbBuffer)
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
        RTRandAdvBytes(hRand, ProviderCtx.pbData, ProviderCtx.cbData);

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    Provider.Interface.pfnRootListRead = tstWinStreamProviderRootListRead;
    Provider.Interface.pfnObjOpen      = tstWinStreamProviderObjOpen;
    Provider.Interface.pfnObjRead      = tstWinStreamProviderObjRead;
    Provider.Interface.pfnObjClose     = tstWinStreamProviderObjClose;
    Provider.pvUser                    = &ProviderCtx;
    Provider.cbUser                    = sizeof(ProviderCtx);

    PSHCLTRANSFER pTransfer = NULL;
    TstWinDataObject *pParent = NULL;
    IStream *pStream = NULL;
    uint8_t bFrontendCtx = 0;

    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, NULL /* pCallbacks */, &pTransfer);
        RTTESTI_CHECK_RC_OK(rc);
    }
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(pTransfer->cbMaxChunkSize == _64K);
        ProviderCtx.cbMaxChunkSize = pTransfer->cbMaxChunkSize;
        rc = ShClTransferSetProvider(pTransfer, &Provider);
        RTTESTI_CHECK_RC_OK(rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferRootListRead(pTransfer);
        RTTESTI_CHECK_RC_OK(rc);
    }
    if (RT_SUCCESS(rc))
    {
        pParent = new TstWinDataObject();
        RTTESTI_CHECK(pParent != NULL);
        if (pParent)
        {
            pParent->AddRef(); /* Testcase reference. */

            ShClWinDataObject::CALLBACKS Callbacks;
            RT_ZERO(Callbacks);
            rc = pParent->Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
            RTTESTI_CHECK_RC_OK(rc);
            if (RT_SUCCESS(rc))
                rc = pParent->addEntry("random-boundaries.bin", RTFS_TYPE_FILE, ProviderCtx.cbData);
            if (RT_SUCCESS(rc))
                rc = pParent->startCompletionTracking();
            RTTESTI_CHECK_RC_OK(rc);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        SHCLFSOBJINFO ObjInfo;
        RT_ZERO(ObjInfo);
        ObjInfo.cbObject = ProviderCtx.cbData;

        HRESULT const hrc = ShClWinStreamImpl::Create(pParent, pTransfer, 0 /* uObjIdx */,
                                                      Utf8Str("random-boundaries.bin"), &ObjInfo, &pStream);
        RTTESTI_CHECK_MSG(hrc == S_OK, ("hrc=%Rhrc\n", hrc));
        if (FAILED(hrc))
            rc = VERR_GENERAL_FAILURE;
    }
    if (RT_SUCCESS(rc))
    {
        ULONG cbRead = UINT32_MAX;
        HRESULT const hrc = pStream->Read(pbBuffer, 0, &cbRead);
        RTTESTI_CHECK_MSG(hrc == S_OK, ("zero-byte read hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(cbRead == 0, ("zero-byte read cbRead=%RU32\n", cbRead));
        RTTESTI_CHECK_MSG(ProviderCtx.cOpens == 0, ("zero-byte read cOpens=%RU32\n", ProviderCtx.cOpens));
        RTTESTI_CHECK_MSG(ProviderCtx.cReads == 0, ("zero-byte read cReads=%RU32\n", ProviderCtx.cReads));
    }

    uint32_t offExpected = 0;
    uint32_t iStreamRead = 0;
    bool fCanContinue = RT_SUCCESS(rc);
    if (fCanContinue)
    {
        fCanContinue = tstWinStreamReadAndCheck(pStream, &ProviderCtx, pbBuffer, TST_WIN_STREAM_MAX_READ_SIZE + 1,
                                                &offExpected, TST_WIN_STREAM_BOUNDARY_READ_SIZE, iStreamRead++);
        RTTESTI_CHECK_MSG(ProviderCtx.cReads == 2, ("cReads=%RU32\n", ProviderCtx.cReads));
        RTTESTI_CHECK_MSG(ProviderCtx.cOpens == 1, ("cOpens=%RU32\n", ProviderCtx.cOpens));
        RTTESTI_CHECK_MSG(ProviderCtx.cCloses == 0, ("cCloses=%RU32\n", ProviderCtx.cCloses));
    }

    RTTestISubF("%u randomized boundary reads (seed %#RX64)",
                TST_WIN_STREAM_RANDOM_READ_COUNT, TST_WIN_STREAM_RANDOM_SEED);

    for (size_t i = 0; fCanContinue && i < RT_ELEMENTS(g_acbBoundaryReads); i++)
        fCanContinue = tstWinStreamReadAndCheck(pStream, &ProviderCtx, pbBuffer, TST_WIN_STREAM_MAX_READ_SIZE + 1,
                                                &offExpected, g_acbBoundaryReads[i], iStreamRead++);

    uint32_t cRandomMultiChunkReads    = 0;
    uint32_t cRandomFileBoundaryReads = 0;
    for (size_t i = 0; fCanContinue && i < RT_ELEMENTS(acbRandomReads); i++)
    {
        uint32_t const cbRequest = acbRandomReads[i];
        if (cbRequest > ProviderCtx.cbMaxChunkSize)
            cRandomMultiChunkReads++;
        if (offExpected / ProviderCtx.cbMaxChunkSize != (offExpected + cbRequest - 1) / ProviderCtx.cbMaxChunkSize)
            cRandomFileBoundaryReads++;

        fCanContinue = tstWinStreamReadAndCheck(pStream, &ProviderCtx, pbBuffer, TST_WIN_STREAM_MAX_READ_SIZE + 1,
                                                &offExpected, cbRequest, iStreamRead++);
    }

    if (fCanContinue)
    {
        RTTESTI_CHECK_MSG(cRandomMultiChunkReads > 0,
                          ("No randomized read exceeded the transfer chunk size\n"));
        RTTESTI_CHECK_MSG(cRandomFileBoundaryReads > 0,
                          ("No randomized read crossed a file chunk boundary\n"));
        RTTESTI_CHECK_MSG(offExpected == ProviderCtx.cbData - TST_WIN_STREAM_FINAL_DATA_SIZE,
                          ("offExpected=%RU32, expected %RU32\n",
                           offExpected, ProviderCtx.cbData - TST_WIN_STREAM_FINAL_DATA_SIZE));
        RTTESTI_CHECK_MSG(ProviderCtx.cCloses == 0, ("pre-EOF cCloses=%RU32\n", ProviderCtx.cCloses));

        fCanContinue = tstWinStreamReadAndCheck(pStream, &ProviderCtx, pbBuffer, TST_WIN_STREAM_MAX_READ_SIZE + 1,
                                                &offExpected, TST_WIN_STREAM_FINAL_DATA_SIZE + 1, iStreamRead++);
    }

    if (fCanContinue)
    {
        RTTESTI_CHECK_MSG(offExpected == ProviderCtx.cbData,
                          ("offExpected=%RU32, expected %RU32\n", offExpected, ProviderCtx.cbData));
        RTTESTI_CHECK_MSG(ProviderCtx.offData == ProviderCtx.cbData,
                          ("provider offData=%RU32, expected %RU32\n", ProviderCtx.offData, ProviderCtx.cbData));
        RTTESTI_CHECK_MSG(ProviderCtx.cOpens == 1, ("cOpens=%RU32\n", ProviderCtx.cOpens));
        RTTESTI_CHECK_MSG(ProviderCtx.cCloses == 1, ("cCloses=%RU32\n", ProviderCtx.cCloses));
        RTTESTI_CHECK_MSG(!ProviderCtx.fReadLogOverflow, ("Provider read trace overflowed\n"));

        RTTestISub("Completed stream invalidation");

        /* Simulate the terminal-status acknowledgement racing Explorer's
         * additional read to observe EOF. */
        static_cast<ShClWinStreamImpl *>(pStream)->Invalidate();

        uint32_t const cProviderReadsBefore = ProviderCtx.cReads;
        memset(pbBuffer, TST_WIN_STREAM_BUFFER_FILL, 2);
        ULONG cbRead = UINT32_MAX;
        HRESULT const hrc = pStream->Read(pbBuffer, 1, &cbRead);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("post-EOF read hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(cbRead == 0, ("post-EOF read cbRead=%RU32\n", cbRead));
        RTTESTI_CHECK_MSG(ProviderCtx.cReads == cProviderReadsBefore,
                          ("post-EOF cReads=%RU32, expected %RU32\n", ProviderCtx.cReads, cProviderReadsBefore));
        RTTESTI_CHECK_MSG(pbBuffer[0] == TST_WIN_STREAM_BUFFER_FILL,
                          ("post-EOF read modified the buffer\n"));

        size_t cCompleted = 0;
        RTTESTI_CHECK(pParent->queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 1);
    }

    RTTestISub("Zero-byte stream completion");

    TstWinDataObject *pZeroParent = NULL;
    IStream *pZeroStream = NULL;
    if (RT_SUCCESS(rc))
    {
        pZeroParent = new TstWinDataObject();
        RTTESTI_CHECK(pZeroParent != NULL);
        if (pZeroParent)
        {
            pZeroParent->AddRef();
            ShClWinDataObject::CALLBACKS Callbacks;
            RT_ZERO(Callbacks);
            rc = pZeroParent->Init((PSHCLCONTEXT)&bFrontendCtx, &Callbacks);
            if (RT_SUCCESS(rc))
                rc = pZeroParent->addEntry("empty.bin", RTFS_TYPE_FILE, 0);
            if (RT_SUCCESS(rc))
                rc = pZeroParent->startCompletionTracking();
            RTTESTI_CHECK_RC_OK(rc);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        SHCLFSOBJINFO ObjInfo;
        RT_ZERO(ObjInfo);
        ObjInfo.Attr.fMode = RTFS_TYPE_FILE;
        ObjInfo.cbObject = 0;
        HRESULT const hrc = ShClWinStreamImpl::Create(pZeroParent, pTransfer, 0 /* uObjIdx */,
                                                      Utf8Str("empty.bin"), &ObjInfo, &pZeroStream);
        RTTESTI_CHECK_MSG(hrc == S_OK, ("creating zero-byte stream failed: %Rhrc\n", hrc));
        if (FAILED(hrc))
            rc = VERR_GENERAL_FAILURE;
    }
    if (RT_SUCCESS(rc))
    {
        uint32_t const cOpensBefore = ProviderCtx.cOpens;
        uint32_t const cReadsBefore = ProviderCtx.cReads;
        uint32_t const cClosesBefore = ProviderCtx.cCloses;
        ULONG cbRead = UINT32_MAX;
        HRESULT const hrc = pZeroStream->Read(pbBuffer, 1, &cbRead);
        RTTESTI_CHECK_MSG(hrc == S_FALSE, ("zero-byte stream read hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(cbRead == 0, ("zero-byte stream cbRead=%RU32\n", cbRead));
        RTTESTI_CHECK_MSG(ProviderCtx.cOpens == cOpensBefore + 1,
                          ("zero-byte stream cOpens=%RU32\n", ProviderCtx.cOpens));
        RTTESTI_CHECK_MSG(ProviderCtx.cReads == cReadsBefore,
                          ("zero-byte stream cReads=%RU32\n", ProviderCtx.cReads));
        RTTESTI_CHECK_MSG(ProviderCtx.cCloses == cClosesBefore + 1,
                          ("zero-byte stream cCloses=%RU32\n", ProviderCtx.cCloses));
        size_t cCompleted = 0;
        RTTESTI_CHECK(pZeroParent->queryCompletionStatus(&cCompleted) == ShClWinDataObject::Completed);
        RTTESTI_CHECK(cCompleted == 1);
    }

    if (pZeroStream)
        pZeroStream->Release();
    if (pZeroParent)
        pZeroParent->Release();

    if (pStream)
        pStream->Release();
    if (pParent)
        pParent->Release();
    if (pTransfer)
        RTTESTI_CHECK_RC_OK(ShClTransferDestroy(pTransfer));
    RTMemFree(pbBuffer);
    RTMemFree(ProviderCtx.pbData);
    if (hRand != NIL_RTRAND)
        RTTESTI_CHECK_RC_OK(RTRandAdvDestroy(hRand));
}


/** Testcase entry point. */
int main(void)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardWinStream", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    tstWinPathSanitizeFilename();
    tstTransferListEntryNameValidation();
    tstWinDataObjectNestedDirectoryEnumeration();
    tstWinDataObjectCompletionOrder();
    tstWinDataObjectNativeCancellation();
    tstWinStreamCanceledInvalidate();
    tstWinStreamReadsAcrossChunkBoundaries();

    return RTTestSummaryAndDestroy(hTest);
}
