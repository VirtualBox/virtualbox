/* $Id: VBoxStub.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxStub - VirtualBox's Windows installer stub.
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
#include <iprt/win/windows.h>
#include <iprt/win/commctrl.h>
#include <lmerr.h>
#include <msiquery.h>
#include <iprt/win/objbase.h>
#include <iprt/win/shlobj.h>

#include <VBox/version.h>

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>

#ifndef IPRT_NO_CRT
# include <stdio.h>
# include <stdlib.h>
#endif

#include "VBoxStub.h"
#include "../StubBld/VBoxStubBld.h"
#include "resource.h"

#ifdef VBOX_WITH_CODE_SIGNING
# include "VBoxStubCertUtil.h"
# include "VBoxStubPublicCert.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MY_UNICODE_SUB(str) L ##str
#define MY_UNICODE(str)     MY_UNICODE_SUB(str)

/* Use an own console window if run in verbose mode. */
#define VBOX_STUB_WITH_OWN_CONSOLE


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Cleanup record.
 */
typedef struct STUBCLEANUPREC
{
    /** List entry. */
    RTLISTNODE  ListEntry;
    /** Stub package index (zero-based) this record belongs to. */
    unsigned    idxPkg;
    /** True if file, false if directory. */
    bool        fFile;
    /** Set if we should not delete the file/directory.
     * This is used for user supplied extraction directories. */
    bool        fDontDelete;
    union
    {
        /** File handle (if \a fFile is \c true). */
        RTFILE  hFile;
        /** Directory handle (if \a fFile is \c false). */
        RTDIR   hDir;
    };
    /** The path to the file or directory to clean up. */
    char        szPath[1];
} STUBCLEANUPREC;
/** Pointer to a cleanup record. */
typedef STUBCLEANUPREC *PSTUBCLEANUPREC;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static PSTUBCLEANUPREC AddCleanupRec(const char *pszPath, bool fIsFile);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether it's a silent or interactive GUI driven install. */
static bool             g_fSilent = false;
/** List of temporary files. */
static RTLISTANCHOR     g_TmpFiles;
/** Verbosity flag. */
static int              g_iVerbosity = 0;



/**
 * Shows an error message box with a printf() style formatted string.
 *
 * @returns RTEXITCODE_FAILURE
 * @param   pszFmt              Printf-style format string to show in the message box body.
 *
 */
static RTEXITCODE ShowError(const char *pszFmt, ...)
{
    char       *pszMsg;
    va_list     va;

    va_start(va, pszFmt);
    if (RTStrAPrintfV(&pszMsg, pszFmt, va))
    {
        if (g_fSilent)
            RTMsgError("%s", pszMsg);
        else
        {
            PRTUTF16 pwszMsg;
            int rc = RTStrToUtf16(pszMsg, &pwszMsg);
            if (RT_SUCCESS(rc))
            {
                MessageBoxW(GetDesktopWindow(), pwszMsg, MY_UNICODE(VBOX_STUB_TITLE), MB_ICONERROR);
                RTUtf16Free(pwszMsg);
            }
            else
                MessageBoxA(GetDesktopWindow(), pszMsg, VBOX_STUB_TITLE, MB_ICONERROR);
        }
        RTStrFree(pszMsg);
    }
    else /* Should never happen! */
        AssertMsgFailed(("Failed to format error text of format string: %s!\n", pszFmt));
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/**
 * Same as ShowError, only it returns RTEXITCODE_SYNTAX.
 */
static RTEXITCODE ShowSyntaxError(const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    ShowError("%N", pszFmt, &va);
    va_end(va);
    return RTEXITCODE_SYNTAX;
}


/**
 * Shows a message box with a printf() style formatted string.
 *
 * @param   uType               Type of the message box (see MSDN).
 * @param   pszFmt              Printf-style format string to show in the message box body.
 *
 */
static void ShowInfo(const char *pszFmt, ...)
{
    char       *pszMsg;
    va_list     va;
    va_start(va, pszFmt);
    int rc = RTStrAPrintfV(&pszMsg, pszFmt, va);
    va_end(va);
    if (rc >= 0)
    {
        if (g_fSilent)
            RTPrintf("%s\n", pszMsg);
        else
        {
            PRTUTF16 pwszMsg;
            rc = RTStrToUtf16(pszMsg, &pwszMsg);
            if (RT_SUCCESS(rc))
            {
                MessageBoxW(GetDesktopWindow(), pwszMsg, MY_UNICODE(VBOX_STUB_TITLE), MB_ICONINFORMATION);
                RTUtf16Free(pwszMsg);
            }
            else
                MessageBoxA(GetDesktopWindow(), pszMsg, VBOX_STUB_TITLE, MB_ICONINFORMATION);
        }
    }
    else /* Should never happen! */
        AssertMsgFailed(("Failed to format error text of format string: %s!\n", pszFmt));
    RTStrFree(pszMsg);
}


/** Logs error details to stderr. */
static void LogError(const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    RTStrmPrintf(g_pStdErr, "error: %N\n", pszFmt, &va);
    va_end(va);
}


/** Logs error details to stderr, returning @a rc. */
static int LogErrorRc(int rc, const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    RTStrmPrintf(g_pStdErr, "error: %N\n", pszFmt, &va);
    va_end(va);
    return rc;
}


/** Logs error details to stderr, RTEXITCODE_FAILURE. */
static RTEXITCODE LogErrorExitFailure(const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    RTStrmPrintf(g_pStdErr, "error: %N\n", pszFmt, &va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/**
 * Finds the specified in the resource section of the executable.
 *
 * @returns IPRT status code.
 *
 * @param   pszDataName     Name of resource to read.
 * @param   ppbResource     Where to return the pointer to the data.
 * @param   pcbResource     Where to return the size of the data (if found).
 *                          Optional.
 */
static int FindData(const char *pszDataName, uint8_t const **ppbResource, DWORD *pcbResource)
{
    AssertReturn(pszDataName, VERR_INVALID_PARAMETER);
    HINSTANCE hInst = NULL;             /* indicates the executable image */

    /* Find our resource. */
    PRTUTF16 pwszDataName;
    int rc = RTStrToUtf16(pszDataName, &pwszDataName);
    AssertRCReturn(rc, rc);
    HRSRC hRsrc = FindResourceExW(hInst,
                                  (LPWSTR)RT_RCDATA,
                                  pwszDataName,
                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    RTUtf16Free(pwszDataName);
    AssertReturn(hRsrc, VERR_IO_GEN_FAILURE);

    /* Get resource size. */
    DWORD cb = SizeofResource(hInst, hRsrc);
    AssertReturn(cb > 0, VERR_NO_DATA);
    if (pcbResource)
        *pcbResource = cb;

    /* Get pointer to resource. */
    HGLOBAL hData = LoadResource(hInst, hRsrc);
    AssertReturn(hData, VERR_IO_GEN_FAILURE);

    /* Lock resource. */
    *ppbResource = (uint8_t const *)LockResource(hData);
    AssertReturn(*ppbResource, VERR_IO_GEN_FAILURE);
    return VINF_SUCCESS;
}


/**
 * Finds the header for the given package.
 *
 * @returns Pointer to the package header on success.  On failure NULL is
 *          returned after ShowError has been invoked.
 * @param   iPackage            The package number.
 */
static const VBOXSTUBPKG *FindPackageHeader(unsigned iPackage)
{
    char szHeaderName[32];
    RTStrPrintf(szHeaderName, sizeof(szHeaderName), "HDR_%02d", iPackage);

    VBOXSTUBPKG const *pPackage;
    int rc = FindData(szHeaderName, (uint8_t const **)&pPackage, NULL);
    if (RT_FAILURE(rc))
    {
        ShowError("Internal error: Could not find package header #%u: %Rrc", iPackage, rc);
        return NULL;
    }

    /** @todo validate it. */
    return pPackage;
}



/**
 * Constructs a full temporary file path from the given parameters.
 *
 * @returns iprt status code.
 *
 * @param   pszTempPath         The pure path to use for construction.
 * @param   pszTargetFileName   The pure file name to use for construction.
 * @param   ppszTempFile        Pointer to the constructed string.  Must be freed
 *                              using RTStrFree().
 */
static int GetTempFileAlloc(const char  *pszTempPath,
                            const char  *pszTargetFileName,
                            char       **ppszTempFile)
{
    if (RTStrAPrintf(ppszTempFile, "%s\\%s", pszTempPath, pszTargetFileName) >= 0)
        return VINF_SUCCESS;
    return VERR_NO_STR_MEMORY;
}


/**
 * Extracts a built-in resource to disk.
 *
 * @returns iprt status code.
 *
 * @param   pszResourceName     The resource name to extract.
 * @param   pszTempFile         The full file path + name to extract the resource to.
 * @param   hFile               Handle to pszTempFile if RTFileCreateUnique was
 *                              used to generate the name, otherwise NIL_RTFILE.
 * @param   idxPackage          The package index for annotating the cleanup
 *                              record with (HACK ALERT).
 */
static int ExtractFile(const char *pszResourceName, const char *pszTempFile, RTFILE hFile, unsigned idxPackage)
{
    AssertPtrReturn(pszResourceName, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTempFile, VERR_INVALID_POINTER);

    /* Create new (and replace any old) file. */
    if (hFile == NIL_RTFILE)
    {
        int rc = RTFileOpen(&hFile, pszTempFile,
                            RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE
                            | (0700 << RTFILE_O_CREATE_MODE_SHIFT));
        AssertRCReturn(rc, LogErrorRc(rc, "#%u: Failed to create/replace '%s' for writing: %Rrc", idxPackage, pszTempFile, rc));
    }

    /* Add a cleanup record, so that we can properly clean up (partially run) stuff. */
    int rc = VERR_NO_MEMORY;
    PSTUBCLEANUPREC pCleanupRec = AddCleanupRec(pszTempFile, true /*fIsFile*/);
    AssertReturn(pCleanupRec, VERR_NO_MEMORY);

    pCleanupRec->idxPkg = idxPackage;
    pCleanupRec->hFile  = hFile;

    /* Find the data of the built-in resource. */
    uint8_t const *pbData = NULL;
    DWORD          cbData = 0;
    rc = FindData(pszResourceName, &pbData, &cbData);
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: Failed to locate resource '%s': %Rrc", idxPackage, pszResourceName, rc));

    /* Write the contents to the file. */
    rc = RTFileWrite(hFile, pbData, cbData, NULL);
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: RTFileWrite('%s',, %#x,) failed: %Rrc", idxPackage, pszTempFile, cbData, rc));

    /*
     * We now wish to keep the file open, however since we've got it open in write
     * mode with deny-write sharing (effectively exclusive write mode) this will
     * prevent the MSI API from opening it in deny-write mode for reading purposes.
     *
     * So we have to do the best we can to transition this to a read-only handle
     * that denies write (and deletion/renaming).  First we open it again in
     * read-only mode only denying deletion, not writing.  Then close the original
     * handle.  Finally open a read-only handle that denies both reading and
     * deletion/renaming, and verify that the file content is still the same.
     *
     * Note! DuplicateHandle to read-only and closing the original does not work,
     *       as the kernel doesn't update the sharing access info for the handles.
     */
    RTFSOBJINFO ObjInfo1;
    rc = RTFileQueryInfo(hFile, &ObjInfo1, RTFSOBJATTRADD_UNIX);
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: RTFileQueryInfo failed on '%s': %Rrc", idxPackage, pszTempFile, rc));

    RTFILE hFile2 = NIL_RTFILE;
    rc = RTFileOpen(&hFile2, pszTempFile,
                    RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE | (0700 << RTFILE_O_CREATE_MODE_SHIFT));
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: First re-opening of '%s' failed: %Rrc", idxPackage, pszTempFile, rc));

    rc = RTFileClose(hFile);
    AssertRCReturnStmt(rc, RTFileClose(hFile2),
                       LogErrorRc(rc, "#%u: RTFileClose('%s') failed: %Rrc", idxPackage, pszTempFile, rc));
    pCleanupRec->hFile = hFile2;

    rc = RTFileOpen(&hFile, pszTempFile, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE);
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: Second re-opening of '%s' failed: %Rrc", idxPackage, pszTempFile, rc));
    pCleanupRec->hFile = hFile;

    rc = RTFileClose(hFile2);
    AssertRCStmt(rc, LogError("#%u: Failed to close 2nd handle to '%s': %Rrc", idxPackage, pszTempFile, rc));

    /* check the size and inode number. */
    RTFSOBJINFO ObjInfo2;
    rc = RTFileQueryInfo(hFile, &ObjInfo2, RTFSOBJATTRADD_UNIX);
    AssertRCReturn(rc, LogErrorRc(rc, "#%u: RTFileQueryInfo failed on '%s': %Rrc", idxPackage, pszTempFile, rc));

    AssertReturn(ObjInfo2.cbObject == cbData,
                 LogErrorRc(VERR_STATE_CHANGED, "#%u: File size of '%s' changed: %'RU64, expected %'RU32",
                            idxPackage, pszTempFile, ObjInfo2.cbObject, pbData));

    AssertReturn(ObjInfo2.Attr.u.Unix.INodeId == ObjInfo1.Attr.u.Unix.INodeId,
                 LogErrorRc(VERR_STATE_CHANGED, "#%u: File ID of '%s' changed: %#RX64, expected %#RX64",
                            idxPackage, pszTempFile, ObjInfo2.Attr.u.Unix.INodeId, ObjInfo1.Attr.u.Unix.INodeId));


    /* Check the content. */
    uint32_t off = 0;
    while (off < cbData)
    {
        uint8_t abBuf[_64K];
        size_t  cbToRead = RT_MIN(cbData - off, sizeof(abBuf));
        rc = RTFileRead(hFile, abBuf, cbToRead, NULL);
        AssertRCReturn(rc, LogErrorRc(rc, "#%u: RTFileRead failed on '%s' at offset %#RX32: %Rrc",
                                      idxPackage, pszTempFile, off, rc));
        AssertReturn(memcmp(abBuf, &pbData[off], cbToRead) == 0,
                     LogErrorRc(VERR_STATE_CHANGED, "#%u: File '%s' has change (mismatch in %#zx byte block at %#RX32)",
                                idxPackage, pszTempFile, cbToRead, off));
        off += cbToRead;
    }

    return VINF_SUCCESS;
}


/**
 * Extracts a built-in resource to disk.
 *
 * @returns iprt status code.
 *
 * @param   pPackage            Pointer to a VBOXSTUBPKG struct that contains the resource.
 * @param   pszTempFile         The full file path + name to extract the resource to.
 * @param   hFile               Handle to pszTempFile if RTFileCreateUnique was
 *                              used to generate the name, otherwise NIL_RTFILE.
 * @param   idxPackage          The package index for annotating the cleanup
 *                              record with (HACK ALERT).
 */
static int Extract(VBOXSTUBPKG const *pPackage, const char *pszTempFile, RTFILE hFile, unsigned idxPackage)
{
    return ExtractFile(pPackage->szResourceName, pszTempFile, hFile, idxPackage);
}


/**
 * Detects whether we're running on a 32- or 64-bit platform and returns the result.
 *
 * @returns TRUE if we're running on a 64-bit OS, FALSE if not.
 */
static BOOL IsWow64(void)
{
    BOOL fIsWow64 = TRUE;
    fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
    if (NULL != fnIsWow64Process)
    {
        if (!fnIsWow64Process(GetCurrentProcess(), &fIsWow64))
        {
            /* Error in retrieving process type - assume that we're running on 32bit. */
            return FALSE;
        }
    }
    return fIsWow64;
}


/**
 * Decides whether we need a specified package to handle or not.
 *
 * @returns @c true if we need to handle the specified package, @c false if not.
 *
 * @param   pPackage            Pointer to a VBOXSTUBPKG struct that contains the resource.
 */
static bool PackageIsNeeded(VBOXSTUBPKG const *pPackage)
{
    if (pPackage->enmArch == VBOXSTUBPKGARCH_ALL)
        return true;
    VBOXSTUBPKGARCH enmArch = IsWow64() ? VBOXSTUBPKGARCH_AMD64 : VBOXSTUBPKGARCH_X86;
    return pPackage->enmArch == enmArch;
}


/**
 * Adds a cleanup record.
 *
 * The caller must set the hFile or hDir if so desired.
 *
 * @returns Pointer to the cleanup record on success, fully complained NULL on
 *          failure.
 * @param   pszPath             The path to the file or directory to clean up.
 * @param   fIsFile             @c true if file, @c false if directory.
 */
static PSTUBCLEANUPREC AddCleanupRec(const char *pszPath, bool fIsFile)
{
    size_t cchPath = strlen(pszPath); Assert(cchPath > 0);
    PSTUBCLEANUPREC pRec = (PSTUBCLEANUPREC)RTMemAllocZ(RT_UOFFSETOF_DYN(STUBCLEANUPREC, szPath[cchPath + 1]));
    if (pRec)
    {
        pRec->idxPkg    = ~0U;
        pRec->fFile     = fIsFile;
        if (fIsFile)
            pRec->hFile = NIL_RTFILE;
        else
            pRec->hDir  = NIL_RTDIR;
        memcpy(pRec->szPath, pszPath, cchPath + 1);

        RTListPrepend(&g_TmpFiles, &pRec->ListEntry);
    }
    else
        ShowError("Out of memory!");
    return pRec;
}


/**
 * Cleans up all the extracted files and optionally removes the package
 * directory.
 *
 * @param   pszPkgDir           The package directory, NULL if it shouldn't be
 *                              removed.
 */
static void CleanUp(const char *pszPkgDir)
{
    for (int i = 0; i < 5; i++)
    {
        bool const fFinalTry = i == 4;

        PSTUBCLEANUPREC pCur, pNext;
        RTListForEachSafe(&g_TmpFiles, pCur, pNext, STUBCLEANUPREC, ListEntry)
        {
            int rc = VINF_SUCCESS;
            if (pCur->fFile)
            {
                if (pCur->hFile != NIL_RTFILE)
                {
                    if (RTFileIsValid(pCur->hFile))
                    {
                        int rcCloseFile = RTFileClose(pCur->hFile);
                        AssertRCStmt(rcCloseFile, LogError("Cleanup file '%s' for #%u: RTFileClose(%p) failed: %Rrc",
                                                           pCur->szPath, pCur->idxPkg, pCur->hFile, rcCloseFile));
                    }
                    pCur->hFile = NIL_RTFILE;
                }
                if (!pCur->fDontDelete)
                    rc = RTFileDelete(pCur->szPath);
            }
            else /* Directory */
            {
                if (pCur->hDir != NIL_RTDIR)
                {
                    if (RTDirIsValid(pCur->hDir))
                    {
                        int rcCloseDir = RTDirClose(pCur->hDir);
                        AssertRCStmt(rcCloseDir, LogError("Cleanup dir '%s' for #%u: RTDirClose(%p) failed: %Rrc",
                                                          pCur->szPath, pCur->idxPkg, pCur->hDir, rcCloseDir));
                    }
                    pCur->hDir = NIL_RTDIR;
                }

                /* Note: Not removing the directory recursively, as we should have separate cleanup records for that. */
                if (!pCur->fDontDelete)
                {
                    rc = RTDirRemove(pCur->szPath);
                    if (rc == VERR_DIR_NOT_EMPTY && fFinalTry)
                        rc = VINF_SUCCESS;
                }
            }
            if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
                rc = VINF_SUCCESS;
            if (RT_SUCCESS(rc))
            {
                RTListNodeRemove(&pCur->ListEntry);
                RTMemFree(pCur);
            }
            else if (fFinalTry)
            {
                if (pCur->fFile)
                    ShowError("Failed to delete temporary file '%s': %Rrc", pCur->szPath, rc);
                else
                    ShowError("Failed to delete temporary directory '%s': %Rrc", pCur->szPath, rc);
            }
        }

        if (RTListIsEmpty(&g_TmpFiles) || fFinalTry)
        {
            if (!pszPkgDir)
                return;
            int rc = RTDirRemove(pszPkgDir);
            if (RT_SUCCESS(rc) || rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND || fFinalTry)
                return;
        }

        /* Delay a little and try again. */
        RTThreadSleep(i == 0 ? 100 : 3000);
    }
}


/**
 * Processes an MSI package.
 *
 * @returns Fully complained exit code.
 * @param   pszMsi              The path to the MSI to process.
 * @param   pszMsiArgs          Any additional installer (MSI) argument
 * @param   pszMsiLogFile       Where to let MSI log its output to. NULL if logging is disabled.
 */
static RTEXITCODE ProcessMsiPackage(const char *pszMsi, const char *pszMsiArgs, const char *pszMsiLogFile)
{
    int rc;

    /*
     * Set UI level.
     */
    INSTALLUILEVEL enmDesiredUiLevel = g_fSilent ? INSTALLUILEVEL_NONE : INSTALLUILEVEL_FULL;
    INSTALLUILEVEL enmRet = MsiSetInternalUI(enmDesiredUiLevel, NULL);
    if (enmRet == INSTALLUILEVEL_NOCHANGE /* means error */)
        return ShowError("Internal error: MsiSetInternalUI failed.");

    /*
     * Enable logging?
     */
    if (pszMsiLogFile)
    {
        PRTUTF16 pwszLogFile;
        rc = RTStrToUtf16(pszMsiLogFile, &pwszLogFile);
        if (RT_FAILURE(rc))
            return ShowError("RTStrToUtf16 failed on '%s': %Rrc", pszMsiLogFile, rc);

        UINT uLogLevel = MsiEnableLogW(INSTALLLOGMODE_VERBOSE,
                                       pwszLogFile,
                                       INSTALLLOGATTRIBUTES_FLUSHEACHLINE);
        RTUtf16Free(pwszLogFile);
        if (uLogLevel != ERROR_SUCCESS)
            return ShowError("MsiEnableLogW failed");
    }

    /*
     * Initialize the common controls (extended version). This is necessary to
     * run the actual .MSI installers with the new fancy visual control
     * styles (XP+). Also, an integrated manifest is required.
     */
    INITCOMMONCONTROLSEX ccEx;
    ccEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
    ccEx.dwICC = ICC_LINK_CLASS | ICC_LISTVIEW_CLASSES | ICC_PAGESCROLLER_CLASS |
                 ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES |
                 ICC_UPDOWN_CLASS | ICC_USEREX_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&ccEx); /* Ignore failure. */

    /*
     * Convert both strings to UTF-16 and start the installation.
     */
    PRTUTF16 pwszMsi;
    rc = RTStrToUtf16(pszMsi, &pwszMsi);
    if (RT_FAILURE(rc))
        return ShowError("RTStrToUtf16 failed on '%s': %Rrc", pszMsi, rc);
    PRTUTF16 pwszMsiArgs;
    rc = RTStrToUtf16(pszMsiArgs, &pwszMsiArgs);
    if (RT_FAILURE(rc))
    {
        RTUtf16Free(pwszMsi);
        return ShowError("RTStrToUtf16 failed on '%s': %Rrc", pszMsiArgs, rc);
    }

    UINT uStatus = MsiInstallProductW(pwszMsi, pwszMsiArgs);
    RTUtf16Free(pwszMsi);
    RTUtf16Free(pwszMsiArgs);

    if (uStatus == ERROR_SUCCESS)
        return RTEXITCODE_SUCCESS;
    if (uStatus == ERROR_SUCCESS_REBOOT_REQUIRED)
    {
        if (g_fSilent)
            RTMsgInfo("Reboot required (by %s)\n", pszMsi);
        return (RTEXITCODE)uStatus;
    }

    /*
     * Installation failed. Figure out what to say.
     */
    switch (uStatus)
    {
        case ERROR_INSTALL_USEREXIT:
            /* Don't say anything? */
            break;

        case ERROR_INSTALL_PACKAGE_VERSION:
            ShowError("This installation package cannot be installed by the Windows Installer service.\n"
                      "You must install a Windows service pack that contains a newer version of the Windows Installer service.");
            break;

        case ERROR_INSTALL_PLATFORM_UNSUPPORTED:
            ShowError("This installation package is not supported on this platform.");
            break;

        default:
        {
            /*
             * Try get windows to format the message.
             */
            DWORD dwFormatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                                | FORMAT_MESSAGE_IGNORE_INSERTS
                                | FORMAT_MESSAGE_FROM_SYSTEM;
            HMODULE hModule = NULL;
            if (uStatus >= NERR_BASE && uStatus <= MAX_NERR)
            {
                hModule = LoadLibraryExW(L"netmsg.dll",
                                         NULL,
                                         LOAD_LIBRARY_AS_DATAFILE);
                if (hModule != NULL)
                    dwFormatFlags |= FORMAT_MESSAGE_FROM_HMODULE;
            }

            PWSTR pwszMsg;
            if (FormatMessageW(dwFormatFlags,
                               hModule, /* If NULL, load system stuff. */
                               uStatus,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               (PWSTR)&pwszMsg,
                               0,
                               NULL) > 0)
            {
                ShowError("Installation failed! Error: %ls", pwszMsg);
                LocalFree(pwszMsg);
            }
            else /* If text lookup failed, show at least the error number. */
                ShowError("Installation failed! Error: %u", uStatus);

            if (hModule)
                FreeLibrary(hModule);
            break;
        }
    }

    return RTEXITCODE_FAILURE;
}


/**
 * Processes a package.
 *
 * @returns Fully complained exit code.
 * @param   iPackage            The package number.
 * @param   pszMsiArgs          Any additional installer (MSI) argument
 * @param   pszMsiLogFile       Where to let MSI log its output to. NULL if logging is disabled.
 */
static RTEXITCODE ProcessPackage(unsigned iPackage, const char *pszMsiArgs, const char *pszMsiLogFile)
{
    /*
     * Get the package header and check if it's needed.
     */
    VBOXSTUBPKG const * const pPackage = FindPackageHeader(iPackage);
    if (pPackage == NULL)
        return RTEXITCODE_FAILURE;

    if (!PackageIsNeeded(pPackage))
        return RTEXITCODE_SUCCESS;

    /*
     * Get the cleanup record for the package so we can get the extracted
     * filename (pPackage is read-only and thus cannot assist here).
     */
    PSTUBCLEANUPREC pRec = NULL;
    PSTUBCLEANUPREC pCur;
    RTListForEach(&g_TmpFiles, pCur, STUBCLEANUPREC, ListEntry)
    {
        if (pCur->idxPkg == iPackage)
        {
            pRec = pCur;
            break;
        }
    }
    AssertReturn(pRec != NULL, LogErrorExitFailure("Package #%u not found in cleanup records", iPackage));

    /*
     * Deal with the file based on it's extension.
     */
    RTPathChangeToDosSlashes(pRec->szPath, true /* Force conversion. */); /* paranoia */

    RTEXITCODE rcExit;
    const char *pszSuff = RTPathSuffix(pRec->szPath);
    if (RTStrICmpAscii(pszSuff, ".msi") == 0)
        rcExit = ProcessMsiPackage(pRec->szPath, pszMsiArgs, pszMsiLogFile);
    else if (RTStrICmpAscii(pszSuff, ".cab") == 0)
        rcExit = RTEXITCODE_SUCCESS; /* Ignore .cab files, they're generally referenced by other files. */
    else
        rcExit = ShowError("Internal error: Do not know how to handle file '%s' (%s).", pPackage->szFilename, pRec->szPath);
    return rcExit;
}

#ifdef VBOX_WITH_CODE_SIGNING

# ifdef VBOX_WITH_VBOX_LEGACY_TS_CA
/**
 * Install the timestamp CA currently needed to support legacy Windows versions.
 *
 * See @bugref{8691} for details.
 *
 * @returns Fully complained exit code.
 */
static RTEXITCODE InstallTimestampCA(bool fForce)
{
    /*
     * Windows 10 desktop should be fine with attestation signed drivers, however
     * the driver guard (DG) may alter that.  Not sure yet how to detect, but
     * OTOH 1809 and later won't accept the SHA-1 stuff regardless, so out of
     * options there.
     *
     * The Windows 2016 server and later is not fine with attestation signed
     * drivers, so we need to do the legacy trick there.
     */
    if (   !fForce
        && RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(10, 0, 0)
        && RTSystemGetNtProductType() == VER_NT_WORKSTATION)
        return RTEXITCODE_SUCCESS;

    if (!addCertToStore(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", g_abVBoxLegacyWinCA, sizeof(g_abVBoxLegacyWinCA)))
        return ShowError("Failed add the legacy Windows timestamp CA to the root certificate store.");
    return RTEXITCODE_SUCCESS;
}
# endif  /* VBOX_WITH_VBOX_LEGACY_TS_CA*/

/**
 * Install the public certificate into TrustedPublishers so the installer won't
 * prompt the user during silent installs.
 *
 * @returns Fully complained exit code.
 */
static RTEXITCODE InstallCertificates(void)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aVBoxStubTrustedCerts); i++)
    {
        if (!addCertToStore(CERT_SYSTEM_STORE_LOCAL_MACHINE,
                            "TrustedPublisher",
                            g_aVBoxStubTrustedCerts[i].pab,
                            g_aVBoxStubTrustedCerts[i].cb))
            return ShowError("Failed to add our certificate(s) to trusted publisher store.");
    }
    return RTEXITCODE_SUCCESS;
}

#endif /* VBOX_WITH_CODE_SIGNING */

/**
 * Copies the "<exepath>.custom" directory to the extraction path if it exists.
 *
 * This is used by the MSI packages from the resource section.
 *
 * @returns Fully complained exit code.
 * @param   pszDstDir       The destination directory.
 */
static RTEXITCODE CopyCustomDir(const char *pszDstDir)
{
    char szSrcDir[RTPATH_MAX];
    int rc = RTPathExecDir(szSrcDir, sizeof(szSrcDir));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szSrcDir, sizeof(szSrcDir), ".custom");
    if (RT_FAILURE(rc))
        return ShowError("Failed to construct '.custom' dir path: %Rrc", rc);

    if (RTDirExists(szSrcDir))
    {
        /*
         * Use SHFileOperation w/ FO_COPY to do the job.  This API requires an
         * extra zero at the end of both source and destination paths.
         */
        size_t   cwc;
        RTUTF16  wszSrcDir[RTPATH_MAX + 1];
        PRTUTF16 pwszSrcDir = wszSrcDir;
        rc = RTStrToUtf16Ex(szSrcDir, RTSTR_MAX, &pwszSrcDir, RTPATH_MAX, &cwc);
        if (RT_FAILURE(rc))
            return ShowError("RTStrToUtf16Ex failed on '%s': %Rrc", szSrcDir, rc);
        wszSrcDir[cwc] = '\0';

        RTUTF16  wszDstDir[RTPATH_MAX + 1];
        PRTUTF16 pwszDstDir = wszSrcDir;
        rc = RTStrToUtf16Ex(pszDstDir, RTSTR_MAX, &pwszDstDir, RTPATH_MAX, &cwc);
        if (RT_FAILURE(rc))
            return ShowError("RTStrToUtf16Ex failed on '%s': %Rrc", pszDstDir, rc);
        wszDstDir[cwc] = '\0';

        SHFILEOPSTRUCTW FileOp;
        RT_ZERO(FileOp); /* paranoia */
        FileOp.hwnd     = NULL;
        FileOp.wFunc    = FO_COPY;
        FileOp.pFrom    = wszSrcDir;
        FileOp.pTo      = wszDstDir;
        FileOp.fFlags   = FOF_SILENT
                        | FOF_NOCONFIRMATION
                        | FOF_NOCONFIRMMKDIR
                        | FOF_NOERRORUI;
        FileOp.fAnyOperationsAborted = FALSE;
        FileOp.hNameMappings = NULL;
        FileOp.lpszProgressTitle = NULL;

        rc = SHFileOperationW(&FileOp);
        if (rc != 0)    /* Not a Win32 status code! */
            return ShowError("Copying the '.custom' dir failed: %#x", rc);

        /*
         * Add a cleanup record for recursively deleting the destination
         * .custom directory.  We should actually add this prior to calling
         * SHFileOperationW since it may partially succeed...
         */
        char *pszDstSubDir = RTPathJoinA(pszDstDir, ".custom");
        if (!pszDstSubDir)
            return ShowError("Out of memory!");

        PSTUBCLEANUPREC pCleanupRec = AddCleanupRec(pszDstSubDir, false /*fIsFile*/);
        AssertReturn(pCleanupRec, RTEXITCODE_FAILURE);

        /*
         * Open the directory to make it difficult to replace or delete (see @bugref{10201}).
         */
        /** @todo this is still race prone, given that SHFileOperationW is the one
         *        creating it and we're really a bit late opening it here.  Anyway,
         *        it's harmless as this code isn't used at present. */
        RTDIR hDstSubDir;
        rc = RTDirOpen(&hDstSubDir, pszDstSubDir);
        if (RT_FAILURE(rc))
            return ShowError("Unable to open the destination .custom directory: %Rrc", rc);
        pCleanupRec->hDir = hDstSubDir;

        RTStrFree(pszDstSubDir);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Extracts the files for all needed packages to @a pszDstDir.
 *
 * @returns
 * @param   cPackages       Number of packages to consinder.
 * @param   pszDstDir       Where to extract the files.
 * @param   fExtractOnly    Set if only extracting and not doing any installing.
 * @param   ppExtractDirRec Where we keep the cleanup record for @a pszDstDir.
 *                          This may have been created by the caller already.
 */
static RTEXITCODE ExtractFiles(unsigned cPackages, const char *pszDstDir, bool fExtractOnly, PSTUBCLEANUPREC *ppExtractDirRec)
{
    int rc;

    /*
     * Make sure the directory exists (normally WinMain created it for us).
     */
    PSTUBCLEANUPREC pCleanupRec = *ppExtractDirRec;
    if (!RTDirExists(pszDstDir))
    {
        AssertReturn(!pCleanupRec, ShowError("RTDirExists failed on '%s' which we just created!", pszDstDir));

        rc = RTDirCreate(pszDstDir, 0700, 0);
        if (RT_FAILURE(rc))
            return ShowError("Failed to create extraction path '%s': %Rrc", pszDstDir, rc);

        *ppExtractDirRec = pCleanupRec = AddCleanupRec(pszDstDir, false /*fFile*/);
        AssertReturn(pCleanupRec, LogErrorExitFailure("Failed to add cleanup record for dir '%s'", pszDstDir));
    }
    /*
     * If we need to create the cleanup record, the caller did not create the
     * directory so we should not delete it when done.
     */
    else if (!pCleanupRec)
    {
        *ppExtractDirRec = pCleanupRec = AddCleanupRec(pszDstDir, false /*fFile*/);
        AssertReturn(pCleanupRec, LogErrorExitFailure("Failed to add cleanup record for existing dir '%s'", pszDstDir));
        pCleanupRec->fDontDelete = true;
    }

    /*
     * Open up the directory to make it difficult to delete / replace.
     */
    rc = RTDirOpen(&pCleanupRec->hDir, pszDstDir);
    if (RT_FAILURE(rc))
        return ShowError("Failed to open extraction path '%s': %Rrc", pszDstDir, rc);

    /*
     * Change current directory to the extraction directory for the same reason
     * as we open it above.
     */
    RTPathSetCurrent(pszDstDir);

    /*
     * Extract files.
     */
    for (unsigned k = 0; k < cPackages; k++)
    {
        VBOXSTUBPKG const * const pPackage = FindPackageHeader(k);
        if (!pPackage)
            return RTEXITCODE_FAILURE; /* Done complaining already. */

        if (fExtractOnly || PackageIsNeeded(pPackage))
        {
            /* If we only extract or if it's a common file, use the original file name,
               otherwise generate a random name with the same file extension (@bugref{10201}). */
            RTFILE hFile = NIL_RTFILE;
            char   szDstFile[RTPATH_MAX];
            if (fExtractOnly || pPackage->enmArch == VBOXSTUBPKGARCH_ALL)
                rc = RTPathJoin(szDstFile, sizeof(szDstFile), pszDstDir, pPackage->szFilename);
            else
            {
                rc = RTPathJoin(szDstFile, sizeof(szDstFile), pszDstDir, "XXXXXXXXXXXXXXXXXXXXXXXX");
                if (RT_SUCCESS(rc))
                {
                    const char *pszSuffix = RTPathSuffix(pPackage->szFilename);
                    if (pszSuffix)
                        rc = RTStrCat(szDstFile, sizeof(szDstFile), pszSuffix);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTFileCreateUnique(&hFile, szDstFile,
                                                RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE
                                                | (0700 << RTFILE_O_CREATE_MODE_SHIFT));
                        if (RT_FAILURE(rc))
                            return ShowError("Failed to create unique filename for '%s' in '%s': %Rrc",
                                             pPackage->szFilename, pszDstDir, rc);
                    }
                }
            }
            if (RT_FAILURE(rc))
                return ShowError("Internal error: Build extraction file name failed: %Rrc", rc);

            rc = Extract(pPackage, szDstFile, hFile, k);
            if (RT_FAILURE(rc))
                return ShowError("Error extracting package #%u (%s): %Rrc", k, pPackage->szFilename, rc);
        }
    }

    return RTEXITCODE_SUCCESS;
}

int main(int argc, char **argv)
{
    /*
     * Init IPRT. This is _always_ the very first thing we do.
     */
    int vrc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_STANDALONE_APP);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);

    /*
     * Parse arguments.
     */

    /* Parameter variables. */
    bool fExtractOnly              = false;
    bool fEnableLogging            = false;
#ifdef VBOX_WITH_CODE_SIGNING
    bool fEnableSilentCert         = true;
    bool fInstallTimestampCA       = true;
    bool fForceTimestampCaInstall  = false;
#endif
    bool fIgnoreReboot             = false;
    char szExtractPath[RTPATH_MAX] = {0};
    char szMSIArgs[_4K]            = {0};
    char szMSILogFile[RTPATH_MAX]  = {0};

    /* Argument enumeration IDs. */
    enum KVBOXSTUBOPT
    {
        KVBOXSTUBOPT_MSI_LOG_FILE = 1000
    };

    /* Parameter definitions. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /** @todo Replace short parameters with enums since they're not
         *        used (and not documented to the public). */
        { "--extract",          'x',                         RTGETOPT_REQ_NOTHING },
        { "-extract",           'x',                         RTGETOPT_REQ_NOTHING },
        { "/extract",           'x',                         RTGETOPT_REQ_NOTHING },
        { "--silent",           's',                         RTGETOPT_REQ_NOTHING },
        { "-silent",            's',                         RTGETOPT_REQ_NOTHING },
        { "/silent",            's',                         RTGETOPT_REQ_NOTHING },
#ifdef VBOX_WITH_CODE_SIGNING
        { "--no-silent-cert",   'c',                         RTGETOPT_REQ_NOTHING },
        { "-no-silent-cert",    'c',                         RTGETOPT_REQ_NOTHING },
        { "/no-silent-cert",    'c',                         RTGETOPT_REQ_NOTHING },
        { "--no-install-timestamp-ca", 't',                  RTGETOPT_REQ_NOTHING },
        { "--force-install-timestamp-ca", 'T',               RTGETOPT_REQ_NOTHING },
#endif
        { "--logging",          'l',                         RTGETOPT_REQ_NOTHING },
        { "-logging",           'l',                         RTGETOPT_REQ_NOTHING },
        { "--msi-log-file",     KVBOXSTUBOPT_MSI_LOG_FILE,   RTGETOPT_REQ_STRING  },
        { "-msilogfile",        KVBOXSTUBOPT_MSI_LOG_FILE,   RTGETOPT_REQ_STRING  },
        { "/logging",           'l',                         RTGETOPT_REQ_NOTHING },
        { "--path",             'p',                         RTGETOPT_REQ_STRING  },
        { "-path",              'p',                         RTGETOPT_REQ_STRING  },
        { "/path",              'p',                         RTGETOPT_REQ_STRING  },
        { "--msiparams",        'm',                         RTGETOPT_REQ_STRING  },
        { "-msiparams",         'm',                         RTGETOPT_REQ_STRING  },
        { "--msi-prop",         'P',                         RTGETOPT_REQ_STRING  },
        { "--reinstall",        'f',                         RTGETOPT_REQ_NOTHING },
        { "-reinstall",         'f',                         RTGETOPT_REQ_NOTHING },
        { "/reinstall",         'f',                         RTGETOPT_REQ_NOTHING },
        { "--ignore-reboot",    'r',                         RTGETOPT_REQ_NOTHING },
        { "--verbose",          'v',                         RTGETOPT_REQ_NOTHING },
        { "-verbose",           'v',                         RTGETOPT_REQ_NOTHING },
        { "/verbose",           'v',                         RTGETOPT_REQ_NOTHING },
        { "--version",          'V',                         RTGETOPT_REQ_NOTHING },
        { "-version",           'V',                         RTGETOPT_REQ_NOTHING },
        { "/version",           'V',                         RTGETOPT_REQ_NOTHING },
        { "--help",             'h',                         RTGETOPT_REQ_NOTHING },
        { "-help",              'h',                         RTGETOPT_REQ_NOTHING },
        { "/help",              'h',                         RTGETOPT_REQ_NOTHING },
        { "/?",                 'h',                         RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, ShowError("RTGetOptInit failed: %Rrc", vrc));

    /* Loop over the arguments. */
    int ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'f': /* Force re-installation. */
                if (szMSIArgs[0])
                    vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), " ");
                if (RT_SUCCESS(vrc))
                    vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), "REINSTALLMODE=vomus REINSTALL=ALL");
                if (RT_FAILURE(vrc))
                    return ShowSyntaxError("Out of space for MSI parameters and properties");
                break;

            case 'x':
                fExtractOnly = true;
                break;

            case 's':
                g_fSilent = true;
                break;

#ifdef VBOX_WITH_CODE_SIGNING
            case 'c':
                fEnableSilentCert = false;
                break;
            case 't':
                fInstallTimestampCA = false;
                break;
            case 'T':
                fForceTimestampCaInstall = fInstallTimestampCA = true;
                break;
#endif
            case 'l':
                fEnableLogging = true;
                break;

            case KVBOXSTUBOPT_MSI_LOG_FILE:
                if (*ValueUnion.psz == '\0')
                    szMSILogFile[0] = '\0';
                else
                {
                    vrc = RTPathAbs(ValueUnion.psz, szMSILogFile, sizeof(szMSILogFile));
                    if (RT_FAILURE(vrc))
                        return ShowSyntaxError("MSI log file path is too long (%Rrc)", vrc);
                }
                break;

            case 'p':
                if (*ValueUnion.psz == '\0')
                    szExtractPath[0] = '\0';
                else
                {
                    vrc = RTPathAbs(ValueUnion.psz, szExtractPath, sizeof(szExtractPath));
                    if (RT_FAILURE(vrc))
                        return ShowSyntaxError("Extraction path is too long (%Rrc)", vrc);
                }
                break;

            case 'm':
                if (szMSIArgs[0])
                    vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), " ");
                if (RT_SUCCESS(vrc))
                    vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), ValueUnion.psz);
                if (RT_FAILURE(vrc))
                    return ShowSyntaxError("Out of space for MSI parameters and properties");
                break;

            case 'P':
            {
                const char *pszProp = ValueUnion.psz;
                if (strpbrk(pszProp, " \t\n\r") == NULL)
                {
                    vrc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(vrc))
                    {
                        size_t cchMsiArgs = strlen(szMSIArgs);
                        if (RTStrPrintf2(&szMSIArgs[cchMsiArgs], sizeof(szMSIArgs) - cchMsiArgs,
                                         strpbrk(ValueUnion.psz, " \t\n\r") == NULL ? "%s%s=%s" : "%s%s=\"%s\"",
                                         cchMsiArgs ? " " : "", pszProp, ValueUnion.psz) <= 1)
                            return ShowSyntaxError("Out of space for MSI parameters and properties");
                    }
                    else if (vrc == VERR_GETOPT_REQUIRED_ARGUMENT_MISSING)
                        return ShowSyntaxError("--msi-prop takes two arguments, the 2nd is missing");
                    else
                        return ShowSyntaxError("Failed to get 2nd --msi-prop argument: %Rrc", vrc);
                }
                else
                    return ShowSyntaxError("The first argument to --msi-prop must not contain spaces: %s", pszProp);
                break;
            }

            case 'r':
                fIgnoreReboot = true;
                break;

            case 'V':
                ShowInfo("Version: %u.%u.%ur%u", VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV);
                return RTEXITCODE_SUCCESS;

            case 'v':
                g_iVerbosity++;
                break;

            case 'h':
                ShowInfo("-- %s v%u.%u.%ur%u --\n"
                         "\n"
                         "Command Line Parameters:\n\n"
                         "--extract\n"
                         "    Extract file contents to temporary directory\n"
                         "--logging\n"
                         "    Enables MSI installer logging (to extract path)\n"
                         "--msi-log-file <path/to/file>\n"
                         "    Sets MSI logging to <file>\n"
                         "--msiparams <parameters>\n"
                         "    Specifies extra parameters for the MSI installers\n"
                         "    double quoted arguments must be doubled and put\n"
                         "    in quotes: --msiparams \"PROP=\"\"a b c\"\"\"\n"
                         "--msi-prop <prop> <value>\n"
                         "    Adds <prop>=<value> to the MSI parameters,\n"
                         "    quoting the property value if necessary\n"
#ifdef VBOX_WITH_CODE_SIGNING
                         "--no-silent-cert\n"
                         "    Do not install VirtualBox Certificate automatically\n"
                         "    when --silent option is specified\n"
#endif
#ifdef VBOX_WITH_VBOX_LEGACY_TS_CA
                         "--force-install-timestamp-ca\n"
                         "    Install the timestamp CA needed for supporting\n"
                         "    legacy Windows versions regardless of the version or\n"
                         "    type of Windows VirtualBox is being installed on.\n"
                         "    Default: All except Windows 10 & 11 desktop\n"
                         "--no-install-timestamp-ca\n"
                         "    Do not install the above mentioned timestamp CA.\n"
#endif
                         "--path\n"
                         "    Sets the path of the extraction directory\n"
                         "--reinstall\n"
                         "    Forces VirtualBox to get re-installed\n"
                         "--ignore-reboot\n"
                         "   Do not set exit code to 3010 if a reboot is required\n"
                         "--silent\n"
                         "   Enables silent mode installation\n"
                         "--version\n"
                         "   Displays version number and exit\n"
                         "-?, -h, --help\n"
                         "   Displays this help text and exit\n"
                         "\n"
                         "Examples:\n"
                         "  %s --msiparams \"INSTALLDIR=\"\"C:\\Program Files\\VirtualBox\"\"\"\n"
                         "  %s --extract -path C:\\VBox",
                         VBOX_STUB_TITLE, VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV,
                         argv[0], argv[0]);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                /* Are (optional) MSI parameters specified and this is the last
                 * parameter? Append everything to the MSI parameter list then. */
                /** @todo r=bird: this makes zero sense */
                if (szMSIArgs[0])
                {
                    vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), " ");
                    if (RT_SUCCESS(vrc))
                        vrc = RTStrCat(szMSIArgs, sizeof(szMSIArgs), ValueUnion.psz);
                    if (RT_FAILURE(vrc))
                        return ShowSyntaxError("Out of space for MSI parameters and properties");
                    continue;
                }
                /* Fall through is intentional. */

            default:
                if (g_fSilent)
                    return RTGetOptPrintError(ch, &ValueUnion);
                if (ch == VERR_GETOPT_UNKNOWN_OPTION)
                    return ShowSyntaxError("Unknown option \"%s\"\n"
                                           "Please refer to the command line help by specifying \"-?\"\n"
                                           "to get more information.", ValueUnion.psz);
                return ShowSyntaxError("Parameter parsing error: %Rrc\n"
                                       "Please refer to the command line help by specifying \"-?\"\n"
                                       "to get more information.", ch);
        }
    }

    /*
     * Check if we're already running and jump out if so (this is mainly to
     * protect the TEMP directory usage, right?).
     */
    SetLastError(0);
    HANDLE hMutexAppRunning = CreateMutexW(NULL, FALSE, L"VBoxStubInstaller");
    if (   hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutexAppRunning); /* close it so we don't keep it open while showing the error message. */
        return ShowError("Another installer is already running");
    }

/** @todo
 *
 *  Split the remainder up in functions and simplify the code flow!!
 *
 *   */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTListInit(&g_TmpFiles);

    /*
     * Create a random extraction directory in the temporary directory if none
     * was given by the user (see @bugref{10201}).
     */
    PSTUBCLEANUPREC pExtractDirRec = NULL; /* This also indicates that */
    if (szExtractPath[0] == '\0')
    {
        vrc = RTPathTemp(szExtractPath, sizeof(szExtractPath));
        if (RT_FAILURE(vrc))
        {
            CloseHandle(hMutexAppRunning); /* close it so we don't keep it open while showing the error message. */
            return ShowError("Failed to find temporary directory: %Rrc", vrc);
        }
        if (!fExtractOnly) /* Only use a random sub-dir if we extract + run (and not just extract). */
        {
            vrc = RTPathAppend(szExtractPath, sizeof(szExtractPath), "XXXXXXXXXXXXXXXXXXXXXXXX");
            if (RT_SUCCESS(vrc))
                /** @todo Need something that return a handle as well as a path. */
                vrc = RTDirCreateTemp(szExtractPath, 0700);
            if (RT_FAILURE(vrc))
            {
                CloseHandle(hMutexAppRunning); /* close it so we don't keep it open while showing the error message. */
                return ShowError("Failed to create extraction path: %Rrc", vrc);
            }
            pExtractDirRec = AddCleanupRec(szExtractPath, false /*fIsFile*/);
        }
    }
    RTPathChangeToDosSlashes(szExtractPath, true /* Force conversion. */); /* MSI requirement. */

    /*
     * Create a console for output if we're in verbose mode.
     */
#ifdef VBOX_STUB_WITH_OWN_CONSOLE
    if (g_iVerbosity)
    {
        if (!AllocConsole())
            return ShowError("Unable to allocate console: LastError=%u\n", GetLastError());

# ifdef IPRT_NO_CRT
        PRTSTREAM pNewStdOutErr = NULL;
        vrc = RTStrmOpen("CONOUT$", "a", &pNewStdOutErr);
        if (RT_SUCCESS(vrc))
        {
            RTStrmSetBufferingMode(pNewStdOutErr, RTSTRMBUFMODE_UNBUFFERED);
            g_pStdErr  = pNewStdOutErr;
            g_pStdOut  = pNewStdOutErr;
        }
# else
        freopen("CONOUT$", "w", stdout);
        setvbuf(stdout, NULL, _IONBF, 0);
        freopen("CONOUT$", "w", stderr);
# endif
    }
#endif /* VBOX_STUB_WITH_OWN_CONSOLE */

    /* Convenience: Enable logging if a log file (via --log-file) is specified. */
    if (   !fEnableLogging
        && szMSILogFile[0] != '\0')
        fEnableLogging = true;

    if (   fEnableLogging
        && szMSILogFile[0] == '\0') /* No log file explicitly specified? Use the extract path by default. */
    {
        vrc = RTStrCopy(szMSILogFile, sizeof(szMSILogFile), szExtractPath);
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppend(szMSILogFile, sizeof(szMSILogFile), "VBoxInstallLog.txt");
        if (RT_FAILURE(vrc))
            return ShowError("Error creating MSI log file name, rc=%Rrc", vrc);
    }

    if (g_iVerbosity)
    {
        RTPrintf("Extraction path          : %s\n",      szExtractPath);
        RTPrintf("Silent installation      : %RTbool\n", g_fSilent);
#ifdef VBOX_WITH_CODE_SIGNING
        RTPrintf("Certificate installation : %RTbool\n", fEnableSilentCert);
#endif
        RTPrintf("Additional MSI parameters: %s\n",      szMSIArgs[0] ? szMSIArgs : "<None>");
        RTPrintf("Logging to file          : %s\n",      szMSILogFile[0] ? szMSILogFile : "<None>");
    }

    /*
     * 32-bit is not officially supported any more.
     */
    if (   !fExtractOnly
        && !g_fSilent
        && !IsWow64())
        rcExit = ShowError("32-bit Windows hosts are not supported by this VirtualBox release.");
    else
    {
        /*
         * Read our manifest.
         */
        VBOXSTUBPKGHEADER const *pHeader = NULL;
        vrc = FindData("MANIFEST", (uint8_t const **)&pHeader, NULL);
        if (RT_SUCCESS(vrc))
        {
            /** @todo If we could, we should validate the header. Only the magic isn't
             *        commonly defined, nor the version number... */

            /*
             * Up to this point, we haven't done anything that requires any cleanup.
             * From here on, we do everything in functions so we can counter clean up.
             */
            rcExit = ExtractFiles(pHeader->cPackages, szExtractPath, fExtractOnly, &pExtractDirRec);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                if (fExtractOnly)
                    ShowInfo("Files were extracted to: %s", szExtractPath);
                else
                {
                    rcExit = CopyCustomDir(szExtractPath);
#ifdef VBOX_WITH_CODE_SIGNING
# ifdef VBOX_WITH_VBOX_LEGACY_TS_CA
                    if (rcExit == RTEXITCODE_SUCCESS && fInstallTimestampCA)
                        rcExit = InstallTimestampCA(fForceTimestampCaInstall);
# endif
                    if (rcExit == RTEXITCODE_SUCCESS && fEnableSilentCert && g_fSilent)
                        rcExit = InstallCertificates();
#endif
                    unsigned iPackage = 0;
                    while (   iPackage < pHeader->cPackages
                           && (rcExit == RTEXITCODE_SUCCESS || rcExit == (RTEXITCODE)ERROR_SUCCESS_REBOOT_REQUIRED))
                    {
                        RTEXITCODE rcExit2 = ProcessPackage(iPackage, szMSIArgs, szMSILogFile[0] ? szMSILogFile : NULL);
                        if (rcExit2 != RTEXITCODE_SUCCESS)
                            rcExit = rcExit2;
                        iPackage++;
                    }
                }
            }

            /*
             * Do cleanups unless we're only extracting (ignoring failures for now).
             */
            if (!fExtractOnly)
            {
                RTPathSetCurrent("..");
                CleanUp(!fEnableLogging && pExtractDirRec && !pExtractDirRec->fDontDelete ? szExtractPath : NULL);
            }

            /* Free any left behind cleanup records (not strictly needed). */
            PSTUBCLEANUPREC pCur, pNext;
            RTListForEachSafe(&g_TmpFiles, pCur, pNext, STUBCLEANUPREC, ListEntry)
            {
                RTListNodeRemove(&pCur->ListEntry);
                RTMemFree(pCur);
            }
        }
        else
            rcExit = ShowError("Internal package error: Manifest not found (%Rrc)", vrc);
    }

#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0501
# ifdef VBOX_STUB_WITH_OWN_CONSOLE
    if (g_iVerbosity)
        FreeConsole();
# endif /* VBOX_STUB_WITH_OWN_CONSOLE */
#endif

    /*
     * Release instance mutex just to be on the safe side.
     */
    if (hMutexAppRunning != NULL)
        CloseHandle(hMutexAppRunning);

    return rcExit != (RTEXITCODE)ERROR_SUCCESS_REBOOT_REQUIRED || !fIgnoreReboot ? rcExit : RTEXITCODE_SUCCESS;
}

#ifndef IPRT_NO_CRT
int WINAPI WinMain(HINSTANCE  hInstance,
                   HINSTANCE  hPrevInstance,
                   char      *lpCmdLine,
                   int        nCmdShow)
{
    RT_NOREF(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    return main(__argc, __argv);
}
#endif

