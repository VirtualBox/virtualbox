/* $Id: VBoxInstallHelper.cpp 108135 2025-01-30 16:51:51Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxInstallHelper - Various helper routines for Windows host installer.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#if defined(VBOX_WITH_NETFLT) || defined(VBOX_WITH_NETADP)
# include "VBox/VBoxNetCfg-win.h"
# include "VBox/VBoxDrvCfg-win.h"
#endif

#define _WIN32_DCOM
#include <iprt/win/windows.h>

#include <aclapi.h>
#include <msi.h>
#include <msiquery.h>

#include <shellapi.h>
#define INITGUID
#include <guiddef.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <sddl.h> /* For ConvertSidToStringSidW. */

#include <iprt/win/objbase.h>
#include <iprt/win/setupapi.h>
#include <iprt/win/shlobj.h>

#include <VBox/version.h>

#include <iprt/assert.h>
#include <iprt/alloca.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/path.h>   /* RTPATH_MAX, RTPATH_IS_SLASH */
#include <iprt/string.h> /* RT_ZERO */
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>

#include <VBox/GuestHost/VBoxWinDrvInst.h>

#include "VBoxCommon.h"
#ifndef VBOX_OSE
# include "internal/VBoxSerial.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG
# define NonStandardAssert(_expr) Assert(_expr)
#else
# define NonStandardAssert(_expr) do{ }while(0)
#endif

#define MY_WTEXT_HLP(a_str) L##a_str
#define MY_WTEXT(a_str)     MY_WTEXT_HLP(a_str)


/*********************************************************************************************************************************
*   Internal structures                                                                                                          *
*********************************************************************************************************************************/
/**
 * Structure for keeping a target's directory security context.
 */
typedef struct TGTDIRSECCTX
{
    /** Initialized status. */
    bool     fInitialized;
    /** Handle of the target's parent directory.
     *
     * Kept open while the context is around and initialized. */
    RTDIR    hParentDir;
    /** Absolute (resolved) path of the target directory. */
    char     szTargetDirAbs[RTPATH_MAX];
    /** Access mask which is forbidden for an ACE of type ACCESS_ALLOWED_ACE_TYPE. */
    uint32_t fAccessMaskForbidden;
    /** Array of well-known SIDs which are forbidden. */
    PSID    *paWellKnownSidsForbidden;
    /** Number of entries in \a paWellKnownSidsForbidden. */
    size_t   cWellKnownSidsForbidden;
} TGTDIRSECCTX;
/** Pointer to a target's directory security context. */
typedef TGTDIRSECCTX *PTGTDIRSECCTX;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static void destroyTargetDirSecurityCtx(PTGTDIRSECCTX pCtx);


/*********************************************************************************************************************************
*   Globals                                                                                                                      *
*********************************************************************************************************************************/
static uint32_t     g_cRef = 0;
/** Our target directory security context.
 *
 * Has to be global in order to keep it around as long as the DLL is being loaded. */
static TGTDIRSECCTX g_TargetDirSecCtx = { 0 };


/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HANDLE hInst, ULONG uReason, LPVOID pReserved)
{
    RT_NOREF(hInst, pReserved);

#ifdef DEBUG
    WCHAR wszMsg[128];
    RTUtf16Printf(wszMsg, RT_ELEMENTS(wszMsg), "DllMain: hInst=%#x, uReason=%u (PID %u), g_cRef=%RU32\n",
                  hInst, uReason, GetCurrentProcessId(), g_cRef);
    OutputDebugStringW(wszMsg);
#endif

    switch (uReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            int rc = RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            if (RT_FAILURE(rc))
                return FALSE;

            g_cRef++;
#if 0
            /*
             * This is a trick for allowing the debugger to be attached, don't know if
             * there is an official way to do that, but this is a pretty efficient.
             *
             * Monitor the debug output in DbgView and be ready to start windbg when
             * the message below appear.  This will happen 3-4 times during install,
             * and 2-3 times during uninstall.
             *
             */
            RTUtf16Printf(wszMsg, RT_ELEMENTS(wszMsg), "Waiting for debugger to attach: windbg -g -G -p %u\n", GetCurrentProcessId());
            for (unsigned i = 0; i < 128 && !IsDebuggerPresent(); i++)
            {
                OutputDebugStringW(wszMsg);
                Sleep(1001);
            }
            Sleep(1002);
            __debugbreak();
#endif
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            /** @todo RTR3Term(); */

            g_cRef--;
            break;
        }

        default:
            break;
    }

    return TRUE;
}

/**
 * Format a log message and print it to whatever is there (i.e. to the MSI log).
 *
 * UTF-16 strings are formatted using '%ls' (lowercase).
 * ANSI strings are formatted using '%s' (uppercase).
 *
 * @returns VBox status code.
 * @param   hInstall            MSI installer handle. Optional and can be NULL.
 * @param   pszFmt              Format string.
 * @param   ...                 Variable arguments for format string.
 */
static int logStringF(MSIHANDLE hInstall, const char *pszFmt, ...)
{
    RTUTF16 wszVa[RTPATH_MAX + 256];
    va_list va;
    va_start(va, pszFmt);
    ssize_t cwc = RTUtf16PrintfV(wszVa, RT_ELEMENTS(wszVa), pszFmt, va);
    va_end(va);

    RTUTF16 wszMsg[RTPATH_MAX + 256];
    cwc = RTUtf16Printf(wszMsg, sizeof(wszMsg), "VBoxInstallHelper: %ls", wszVa);
    if (cwc <= 0)
        return VERR_BUFFER_OVERFLOW;

#ifdef DEBUG
    OutputDebugStringW(wszMsg);
#endif
#ifdef TESTCASE
    RTPrintf("%ls\n", wszMsg);
#endif
    PMSIHANDLE hMSI = MsiCreateRecord(2 /* cParms */);
    if (hMSI)
    {
        MsiRecordSetStringW(hMSI, 0, wszMsg);
        MsiProcessMessage(hInstall, INSTALLMESSAGE(INSTALLMESSAGE_INFO), hMSI);
        MsiCloseHandle(hMSI);
    }

    return cwc < RT_ELEMENTS(wszVa) ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
}

UINT __stdcall IsSerialCheckNeeded(MSIHANDLE hModule)
{
#ifndef VBOX_OSE
    /*BOOL fRet =*/ serialCheckNeeded(hModule);
#else
    RT_NOREF(hModule);
#endif
    return ERROR_SUCCESS;
}

UINT __stdcall CheckSerial(MSIHANDLE hModule)
{
#ifndef VBOX_OSE
    /*BOOL bRet =*/ serialIsValid(hModule);
#else
    RT_NOREF(hModule);
#endif
    return ERROR_SUCCESS;
}

/**
 * Initializes a target security context.
 *
 * @returns VBox status code.
 * @param   pCtx                Target directory security context to initialize.
 * @param   hModule             Windows installer module handle.
 * @param   pszPath             Target directory path to use.
 */
static int initTargetDirSecurityCtx(PTGTDIRSECCTX pCtx, MSIHANDLE hModule, const char *pszPath)
{
    if (pCtx->fInitialized)
        return VINF_SUCCESS;

#ifdef DEBUG
    logStringF(hModule, "initTargetDirSecurityCtx: pszPath=%s\n", pszPath);
#endif

    char szPathTemp[RTPATH_MAX];
    int vrc = RTStrCopy(szPathTemp, sizeof(szPathTemp), pszPath);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Try to find a parent path which exists. */
    char szPathParentAbs[RTPATH_MAX] = { 0 };
    for (int i = 0; i < 256; i++) /* Failsafe counter. */
    {
        RTPathStripTrailingSlash(szPathTemp);
        RTPathStripFilename(szPathTemp);
        vrc = RTPathReal(szPathTemp, szPathParentAbs, sizeof(szPathParentAbs));
        if (RT_SUCCESS(vrc))
            break;
    }

    if (RT_FAILURE(vrc))
    {
        logStringF(hModule, "initTargetDirSecurityCtx: No existing / valid parent directory found (%Rrc), giving up\n", vrc);
        return vrc;
    }

    RTDIR hParentDir;
    vrc = RTDirOpen(&hParentDir, szPathParentAbs);
    if (RT_FAILURE(vrc))
    {
        logStringF(hModule, "initTargetDirSecurityCtx: Locking parent directory '%s' failed with %Rrc\n", szPathParentAbs, vrc);
        return vrc;
    }

#ifdef DEBUG
    logStringF(hModule, "initTargetDirSecurityCtx: Locked parent directory '%s'\n", szPathParentAbs);
#endif

    char szPathTargetAbs[RTPATH_MAX];
    vrc = RTPathReal(pszPath, szPathTargetAbs, sizeof(szPathTargetAbs));
    if (RT_FAILURE(vrc))
        vrc = RTStrCopy(szPathTargetAbs, sizeof(szPathTargetAbs), pszPath);
    if (RT_FAILURE(vrc))
    {
        logStringF(hModule, "initTargetDirSecurityCtx: Failed to resolve absolute target path (%Rrc)\n", vrc);
        return vrc;
    }

#ifdef DEBUG
    logStringF(hModule, "initTargetDirSecurityCtx: szPathTargetAbs=%s, szPathParentAbs=%s\n", szPathTargetAbs, szPathParentAbs);
#endif

    /* Target directory validation. */
    if (   !RTStrCmp(szPathTargetAbs, szPathParentAbs) /* Don't allow installation into root directories. */
        ||  RTStrStr(szPathTargetAbs, ".."))
    {
        logStringF(hModule, "initTargetDirSecurityCtx: Directory '%s' invalid", szPathTargetAbs);
        vrc = VERR_INVALID_NAME;
    }

    if (RT_SUCCESS(vrc))
    {
        RTFSOBJINFO fsObjInfo;
        vrc = RTPathQueryInfo(szPathParentAbs, &fsObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(vrc))
        {
            if (RTFS_IS_DIRECTORY(fsObjInfo.Attr.fMode)) /* No symlinks or other fun stuff. */
            {
                static WELL_KNOWN_SID_TYPE aForbiddenWellKnownSids[] =
                {
                    WinNullSid,
                    WinWorldSid,
                    WinAuthenticatedUserSid,
                    WinBuiltinUsersSid,
                    WinBuiltinGuestsSid,
                    WinBuiltinPowerUsersSid
                };

                pCtx->paWellKnownSidsForbidden = (PSID *)RTMemAlloc(sizeof(PSID) * RT_ELEMENTS(aForbiddenWellKnownSids));
                AssertPtrReturn(pCtx->paWellKnownSidsForbidden, VERR_NO_MEMORY);

                size_t i = 0;
                for(; i < RT_ELEMENTS(aForbiddenWellKnownSids); i++)
                {
                    pCtx->paWellKnownSidsForbidden[i] = RTMemAlloc(SECURITY_MAX_SID_SIZE);
                    AssertPtrBreakStmt(pCtx->paWellKnownSidsForbidden, vrc = VERR_NO_MEMORY);
                    DWORD cbSid = SECURITY_MAX_SID_SIZE;
                    if (!CreateWellKnownSid(aForbiddenWellKnownSids[i], NULL, pCtx->paWellKnownSidsForbidden[i], &cbSid))
                    {
                        vrc = RTErrConvertFromWin32(GetLastError());
                        logStringF(hModule, "initTargetDirSecurityCtx: Creating SID (index %zu) failed with %Rrc\n", i, vrc);
                        break;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    vrc = RTStrCopy(pCtx->szTargetDirAbs, sizeof(pCtx->szTargetDirAbs), szPathTargetAbs);
                    if (RT_SUCCESS(vrc))
                    {
                        pCtx->fInitialized            = true;
                        pCtx->hParentDir              = hParentDir;
                        pCtx->cWellKnownSidsForbidden = i;
                        pCtx->fAccessMaskForbidden    = FILE_WRITE_DATA
                                                      | FILE_APPEND_DATA
                                                      | FILE_WRITE_ATTRIBUTES
                                                      | FILE_WRITE_EA;

                        RTFILE fh;
                        RTFileOpen(&fh, "c:\\temp\\targetdir.ctx", RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE | RTFILE_O_WRITE);
                        RTFileClose(fh);

                        return VINF_SUCCESS;
                    }
                }
            }
            else
                vrc = VERR_INVALID_NAME;
        }
    }

    RTDirClose(hParentDir);

    while (pCtx->cWellKnownSidsForbidden--)
    {
        RTMemFree(pCtx->paWellKnownSidsForbidden[pCtx->cWellKnownSidsForbidden]);
        pCtx->paWellKnownSidsForbidden[pCtx->cWellKnownSidsForbidden] = NULL;
    }

    logStringF(hModule, "initTargetDirSecurityCtx: Initialization failed failed with %Rrc\n", vrc);
    return vrc;
}

/**
 * Destroys a target security context.
 *
 * @returns VBox status code.
 * @param   pCtx                Target directory security context to destroy.
 */
static void destroyTargetDirSecurityCtx(PTGTDIRSECCTX pCtx)
{
    if (   !pCtx
        || !pCtx->fInitialized)
        return;

    if (pCtx->hParentDir != NIL_RTDIR)
    {
        RTDirClose(pCtx->hParentDir);
        pCtx->hParentDir = NIL_RTDIR;
    }
    RT_ZERO(pCtx->szTargetDirAbs);

    for (size_t i = 0; i < pCtx->cWellKnownSidsForbidden; i++)
        RTMemFree(pCtx->paWellKnownSidsForbidden[i]);
    pCtx->cWellKnownSidsForbidden = 0;

    RTMemFree(pCtx->paWellKnownSidsForbidden);
    pCtx->paWellKnownSidsForbidden = NULL;

    RTFileDelete("c:\\temp\\targetdir.ctx");

    logStringF(NULL, "destroyTargetDirSecurityCtx\n");
}

#ifdef DEBUG
/**
 * Returns a stingified version of an ACE type.
 *
 * @returns Stingified version of an ACE type.
 * @param   uType               ACE type.
 */
inline const char *dbgAceTypeToString(uint8_t uType)
{
    switch (uType)
    {
        RT_CASE_RET_STR(ACCESS_ALLOWED_ACE_TYPE);
        RT_CASE_RET_STR(ACCESS_DENIED_ACE_TYPE);
        RT_CASE_RET_STR(SYSTEM_AUDIT_ACE_TYPE);
        RT_CASE_RET_STR(SYSTEM_ALARM_ACE_TYPE);
        default: break;
    }

    return "<Invalid>";
}

/**
 * Returns an allocated string for a SID containing the user/domain name.
 *
 * @returns Allocated string (UTF-8). Must be free'd using RTStrFree().
 * @param   pSid                SID to return allocated string for.
 */
inline char *dbgSidToNameA(const PSID pSid)
{
    char *pszName = NULL;
    int   vrc     = VINF_SUCCESS;

    LPWSTR pwszSid = NULL;
    if (ConvertSidToStringSid(pSid, &pwszSid))
    {
        SID_NAME_USE SidNameUse;

        WCHAR wszUser[MAX_PATH];
        DWORD cbUser   = sizeof(wszUser);
        WCHAR wszDomain[MAX_PATH];
        DWORD cbDomain = sizeof(wszDomain);
        if (LookupAccountSid(NULL, pSid, wszUser, &cbUser, wszDomain, &cbDomain, &SidNameUse))
        {
            RTUTF16 wszName[RTPATH_MAX];
            if (RTUtf16Printf(wszName, RT_ELEMENTS(wszName), "%ls%s%ls (%ls)",
                              wszUser, wszDomain[0] == L'\0' ? "" : "\\", wszDomain, pwszSid))
            {
                vrc = RTUtf16ToUtf8(wszName, &pszName);
            }
            else
                vrc = VERR_NO_MEMORY;
        }
        else
            vrc = RTStrAPrintf(&pszName, "<Lookup Error>");

        LocalFree(pwszSid);
    }
    else
        vrc = VERR_NOT_FOUND;

    return RT_SUCCESS(vrc) ? pszName : "<Invalid>";
}
#endif /* DEBUG */

/**
 * Checks a single target path whether it's safe to use or not.
 *
 * We check if the given path is owned by "NT Service\TrustedInstaller" and therefore assume that it's safe to use.
 *
 * @returns VBox status code. On error the path should be considered unsafe.
 * @retval  VERR_INVALID_NAME if the given path is considered unsafe.
 * @retval  VINF_SUCCESS if the given path is found to be safe to use.
 * @param   hModule             Windows installer module handle.
 * @param   pszPath             Path to check.
 */
static int checkTargetDirOne(MSIHANDLE hModule, PTGTDIRSECCTX pCtx, const char *pszPath)
{
    logStringF(hModule, "checkTargetDirOne: Checking '%s' ...", pszPath);

    PRTUTF16 pwszPath;
    int vrc = RTStrToUtf16(pszPath, &pwszPath);
    if (RT_FAILURE(vrc))
        return vrc;

    PACL      pDacl = NULL;
    PSECURITY_DESCRIPTOR pSecurityDescriptor = { 0 };
    DWORD dwErr = GetNamedSecurityInfo(pwszPath, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                       NULL, NULL, NULL, NULL, &pSecurityDescriptor);
    if (dwErr == ERROR_SUCCESS)
    {
        BOOL fDaclPresent          = FALSE;
        BOOL fDaclDefaultedIgnored = FALSE;
        if (GetSecurityDescriptorDacl(pSecurityDescriptor, &fDaclPresent,
                                      &pDacl, &fDaclDefaultedIgnored))
        {
            if (   !fDaclPresent
                || !pDacl)
            {
                /* Bail out early if the DACL isn't provided or is missing. */
                vrc = VERR_INVALID_NAME;
            }
            else
            {
                ACL_SIZE_INFORMATION aclSizeInfo;
                RT_ZERO(aclSizeInfo);
                if (GetAclInformation(pDacl, &aclSizeInfo, sizeof(aclSizeInfo), AclSizeInformation))
                {
                    for(DWORD idxACE = 0; idxACE < aclSizeInfo.AceCount; idxACE++)
                    {
                        ACE_HEADER *pAceHdr = NULL;
                        if (GetAce(pDacl, idxACE, (LPVOID *)&pAceHdr))
                        {
#ifdef DEBUG
                            logStringF(hModule, "checkTargetDirOne: ACE type=%s, flags=%#x, size=%#x",
                                       dbgAceTypeToString(pAceHdr->AceType), pAceHdr->AceFlags, pAceHdr->AceSize);
#endif
                            /* Note: We print the ACEs in canonoical order. */
                            switch (pAceHdr->AceType)
                            {
                                case ACCESS_ALLOWED_ACE_TYPE: /* We're only interested in the ALLOW ACE. */
                                {
                                    ACCESS_ALLOWED_ACE const *pAce = (ACCESS_ALLOWED_ACE *)pAceHdr;
                                    PSID const pSid                = (PSID)&pAce->SidStart;
#ifdef DEBUG
                                    char *pszSid = dbgSidToNameA(pSid);
                                    logStringF(hModule, "checkTargetDirOne:\t%s fMask=%#x", pszSid, pAce->Mask);
                                    RTStrFree(pszSid);
#endif
                                    /* We check the flags here first for performance reasons. */
                                    if ((pAce->Mask & pCtx->fAccessMaskForbidden) == pCtx->fAccessMaskForbidden)
                                    {
                                        for (size_t idxSID = 0; idxSID < pCtx->cWellKnownSidsForbidden; idxSID++)
                                        {
                                            PSID const pSidForbidden = pCtx->paWellKnownSidsForbidden[idxSID];
                                            bool const fForbidden    = EqualSid(pSid, pSidForbidden);
#ifdef DEBUG
                                            char *pszName = dbgSidToNameA(pSidForbidden);
                                            logStringF(hModule, "checkTargetDirOne:\t%s : %s",
                                                       fForbidden ? "** FORBIDDEN **" : "ALLOWED        ", pszName);
                                            RTStrFree(pszName);
#endif /* DEBUG */
                                            if (fForbidden)
                                            {
                                                vrc = VERR_INVALID_NAME;
                                                break;
                                            }
                                        }
                                    }

                                    break;
                                }
#ifdef DEBUG
                                case ACCESS_DENIED_ACE_TYPE: /* We're only interested in the ALLOW ACE. */
                                {
                                    ACCESS_DENIED_ACE const *pAce = (ACCESS_DENIED_ACE *)pAceHdr;

                                    LPWSTR pwszSid = NULL;
                                    ConvertSidToStringSid((PSID)&pAce->SidStart, &pwszSid);

                                    logStringF(hModule, "checkTargetDirOne:\t%ls fMask=%#x (generic %#x specific %#x)",
                                               pwszSid ? pwszSid : L"<Allocation Error>", pAce->Mask);

                                    LocalFree(pwszSid);
                                    break;
                                }
#endif /* DEBUG */
                                default:
                                    /* Ignore everything else. */
                                    break;
                            }
                        }
                        else
                            dwErr = GetLastError();

                        /* No point in checking further if we failed somewhere above. */
                        if (RT_FAILURE(vrc))
                            break;

                    } /* for ACE */
                }
                else
                    dwErr = GetLastError();
            }
        }
        else
            dwErr = GetLastError();

        LocalFree(pSecurityDescriptor);
    }
    else
        dwErr = GetLastError();

    if (RT_SUCCESS(vrc))
        vrc = RTErrConvertFromWin32(dwErr);

#ifdef DEBUG
    logStringF(hModule, "checkTargetDirOne: Returning %Rrc", vrc);
#endif

    if (   RT_FAILURE(vrc)
        && vrc != VERR_INVALID_NAME)
        logStringF(hModule, "checkTargetDirOne: Failed with %Rrc (%#x)", vrc, dwErr);

    return vrc;
}

/**
 * Checks whether the path in the public property INSTALLDIR has the correct ACL permissions and returns whether
 * it's valid or not.
 *
 * Called from the MSI installer as a custom action.
 *
 * @returns Success status (acccording to MSI custom actions).
 * @retval  ERROR_SUCCESS if checking the target directory turned out to be valid.
 * @retval  ERROR_NO_NET_OR_BAD_PATH is the target directory is invalid.
 * @param   hModule             Windows installer module handle.
 *
 * @note    Sets private property VBox_Target_Dir_Is_Valid to "1" (true) if the given target path is valid,
 *          or "0" (false) if it is not. An empty target directory is considered to be valid (i.e. INSTALLDIR not set yet).
 *
 * @sa      @bugref{10616}
 */
UINT __stdcall CheckTargetDir(MSIHANDLE hModule)
{
    char *pszTargetDir;

    int vrc = VBoxMsiQueryPropUtf8(hModule, "INSTALLDIR", &pszTargetDir);
    if (vrc == VERR_NOT_FOUND) /* Target directory might not set yet. */
    {
        logStringF(hModule, "CheckTargetDir: No INSTALLDIR set (yet), skipping ...");

        VBoxMsiSetProp(hModule, L"VBox_Target_Dir_Is_Valid", L"1");
        vrc = VINF_SUCCESS;
    }
    else if (RT_SUCCESS(vrc))
    {
        logStringF(hModule, "CheckTargetDir: Checking target directory '%s' ...", pszTargetDir);

        union
        {
            RTPATHPARSED    Parsed;
            uint8_t         ab[RTPATH_MAX];
        } u;

        vrc = RTPathParse(pszTargetDir, &u.Parsed, sizeof(u), RTPATH_STR_F_STYLE_DOS);
        if (RT_SUCCESS(vrc))
        {
            if (u.Parsed.fProps & RTPATH_PROP_DOTDOT_REFS)
                vrc = VERR_INVALID_PARAMETER;
            if (RT_SUCCESS(vrc))
            {
                vrc = initTargetDirSecurityCtx(&g_TargetDirSecCtx, hModule, pszTargetDir);
                if (RT_SUCCESS(vrc))
                {
                    uint16_t idxComp = u.Parsed.cComps;
                    char     szPathToCheck[RTPATH_MAX];
                    while (idxComp > 1) /* We traverse backwards from INSTALLDIR and leave out the root (e.g. C:\"). */
                    {
                        u.Parsed.cComps = idxComp;
                        vrc = RTPathParsedReassemble(pszTargetDir, &u.Parsed, RTPATH_STR_F_STYLE_DOS,
                                                     szPathToCheck, sizeof(szPathToCheck));
                        if (RT_FAILURE(vrc))
                            break;
                        if (RTDirExists(szPathToCheck))
                        {
                            vrc = checkTargetDirOne(hModule, &g_TargetDirSecCtx, szPathToCheck);
                            if (RT_FAILURE(vrc))
                                break;
                        }
                        else
                            logStringF(hModule, "CheckTargetDir: Path '%s' does not exist (yet)", szPathToCheck);
                        idxComp--;
                    }

                    destroyTargetDirSecurityCtx(&g_TargetDirSecCtx);
                }
                else
                    logStringF(hModule, "CheckTargetDir: initTargetDirSecurityCtx failed with %Rrc\n", vrc);

                if (RT_SUCCESS(vrc))
                    VBoxMsiSetProp(hModule, L"VBox_Target_Dir_Is_Valid", L"1");
            }
        }
        else
            logStringF(hModule, "CheckTargetDir: Parsing path failed with %Rrc", vrc);

        RTStrFree(pszTargetDir);
    }

    if (RT_FAILURE(vrc)) /* On failure (or when in doubt), mark the installation directory as invalid. */
    {
        logStringF(hModule, "CheckTargetDir: Checking failed with %Rrc", vrc);
        VBoxMsiSetProp(hModule, L"VBox_Target_Dir_Is_Valid", L"0");
    }

    /* Return back outcome to the MSI engine. */
    return RT_SUCCESS(vrc) ? ERROR_SUCCESS : ERROR_NO_NET_OR_BAD_PATH;
}

/**
 * Runs an executable on the OS.
 *
 * @returns Windows error code.
 * @param   hModule     Windows installer module handle.
 * @param   pwszImage   The executable to run.
 * @param   pwszArgs    The arguments (command line w/o executable).
 */
static UINT procRun(MSIHANDLE hModule, const wchar_t *pwszImage, wchar_t const *pwszArgs)
{
    /*
     * Construct a full command line.
     */
    size_t const cwcImage = RTUtf16Len(pwszImage);
    size_t const cwcArgs  = RTUtf16Len(pwszArgs);

    wchar_t *pwszCmdLine = (wchar_t *)alloca((1 + cwcImage + 1 + 1 + cwcArgs + 1) * sizeof(wchar_t));
    pwszCmdLine[0] = '"';
    memcpy(&pwszCmdLine[1], pwszImage, cwcImage * sizeof(wchar_t));
    pwszCmdLine[1 + cwcImage] = '"';
    pwszCmdLine[1 + cwcImage + 1] = ' ';
    memcpy(&pwszCmdLine[1 + cwcImage + 1 + 1], pwszArgs, (cwcArgs + 1) * sizeof(wchar_t));

    /*
     * Construct startup info.
     */
    STARTUPINFOW StartupInfo;
    RT_ZERO(StartupInfo);
    StartupInfo.cb          = sizeof(StartupInfo);
    StartupInfo.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput  = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError   = GetStdHandle(STD_ERROR_HANDLE);
    StartupInfo.dwFlags     = STARTF_USESTDHANDLES;
#ifndef DEBUG
    StartupInfo.dwFlags    |= STARTF_USESHOWWINDOW;
    StartupInfo.wShowWindow = SW_HIDE;
#endif

    /*
     * Start it.
     */
    UINT rcWin;
    PROCESS_INFORMATION ChildInfo = { NULL, NULL, 0, 0 };
    if (CreateProcessW(pwszImage, pwszCmdLine, NULL /*pProcessAttribs*/, NULL /*pThreadAttribs*/, TRUE /*fInheritHandles*/,
                       0 /*fFlags*/, NULL /*pwszEnv*/, NULL /*pwszCwd*/, &StartupInfo, &ChildInfo))
    {
        logStringF(hModule, "procRun: Info: Started process %u: %ls", ChildInfo.dwProcessId, pwszCmdLine);
        CloseHandle(ChildInfo.hThread);
        DWORD const dwWait = WaitForSingleObject(ChildInfo.hProcess, RT_MS_30SEC);
        DWORD dwExitCode = 0xf00dface;
        if (GetExitCodeProcess(ChildInfo.hProcess, &dwExitCode))
        {
            if (dwExitCode == 0)
            {
                logStringF(hModule, "procRun: Info: Process '%ls' terminated exit code zero", pwszCmdLine);
                rcWin = ERROR_SUCCESS;
            }
            else
            {
                logStringF(hModule, "procRun: Process '%ls' terminated with non-zero exit code: %u (%#x)",
                           pwszCmdLine, dwExitCode, dwExitCode);
                rcWin = ERROR_GEN_FAILURE;
            }
        }
        else
        {
            rcWin = GetLastError();
            logStringF(hModule, "procRun: Process '%ls' is probably still running: rcWin=%u dwWait=%u (%#x)",
                       pwszCmdLine, rcWin, dwWait, dwWait);
        }
    }
    else
    {
        rcWin = GetLastError();
        logStringF(hModule, "procRun: Creating process '%ls' failed: rcWin=%u\n", pwszCmdLine, rcWin);
    }
    return rcWin;
}

/**
 * Tries to retrieve the Python installation path on the system, extended version.
 *
 * @returns Windows error code.
 * @param   hModule         Windows installer module handle.
 * @param   hKeyRoot        Registry root key to use, e.g. HKEY_LOCAL_MACHINE.
 * @param   pwszPythonPath  Buffer to return the path for python.exe in.
 * @param   cwcPythonPath   Buffer size in UTF-16 units.
 * @param   fReturnExe      Return the path to python.exe if true, otherwise
 *                          just the python install directory.
 */
static UINT getPythonPathEx(MSIHANDLE hModule, HKEY hKeyRoot, wchar_t *pwszPythonPath, size_t cwcPythonPath, bool fReturnExe)
{
    *pwszPythonPath = '\0';

    /*
     * Enumerate the subkeys of python core installation key.
     *
     * Note: The loop ASSUMES that later found versions are higher, e.g. newer
     *       Python versions.  For now we always go by the newest version.
     */
    HKEY hKeyPythonCore = NULL;
    LSTATUS dwErr = RegOpenKeyExW(hKeyRoot, L"SOFTWARE\\Python\\PythonCore", 0, KEY_READ, &hKeyPythonCore);
    if (dwErr != ERROR_SUCCESS)
        return dwErr;

    UINT rcWinRet = ERROR_PATH_NOT_FOUND;
    for (DWORD i = 0; i < 16384; ++i)
    {
        static wchar_t const s_wszInstallPath[] = L"\\InstallPath";
        static wchar_t const s_wszPythonExe[]   = L"python.exe";

        /* Get key name: */
        wchar_t wszBuf[RTPATH_MAX + RT_MAX(RT_ELEMENTS(s_wszInstallPath), RT_ELEMENTS(s_wszPythonExe)) + 2];
        DWORD   cwcKeyNm  = RTPATH_MAX;
        DWORD   dwKeyType = REG_SZ;
        dwErr = RegEnumKeyExW(hKeyPythonCore, i, wszBuf, &cwcKeyNm, NULL, NULL, NULL, NULL);
        if (dwErr == ERROR_NO_MORE_ITEMS)
            break;
        if (dwErr != ERROR_SUCCESS)
            continue;
        if (dwKeyType != REG_SZ)
            continue;
        if (cwcKeyNm == 0)
            continue;
        NonStandardAssert(cwcKeyNm <= sizeof(wszBuf));

        /* Try Open the InstallPath subkey: */
        memcpy(&wszBuf[cwcKeyNm], s_wszInstallPath, sizeof(s_wszInstallPath));

        HKEY hKeyInstallPath = NULL;
        dwErr = RegOpenKeyExW(hKeyPythonCore, wszBuf, 0, KEY_READ, &hKeyInstallPath);
        if (dwErr != ERROR_SUCCESS)
            continue;

        /* Query the value.  We double buffer this so we don't overwrite an okay
           return value with this.  Use the smaller of cwcPythonPath and wszValue
           so RegQueryValueExW can do all the buffer overflow checking for us.
           For paranoid reasons, we reserve a space for a terminator as well as
           a slash. (ASSUMES reasonably sized output buffer.) */
        NonStandardAssert(cwcPythonPath > RT_ELEMENTS(s_wszPythonExe) + 16);
        DWORD cbValue = (DWORD)RT_MIN(  cwcPythonPath * sizeof(wchar_t)
                                      - (fReturnExe ? sizeof(s_wszInstallPath) - sizeof(wchar_t) * 2 : sizeof(wchar_t) * 2),
                                      RTPATH_MAX * sizeof(wchar_t));
        DWORD dwValueType = REG_SZ;
        dwErr = RegQueryValueExW(hKeyInstallPath, L"", NULL, &dwValueType, (LPBYTE)wszBuf, &cbValue);
        RegCloseKey(hKeyInstallPath);
        if (   dwErr       == ERROR_SUCCESS
            && dwValueType == REG_SZ
            && cbValue     >= sizeof(L"C:\\") - sizeof(L""))
        {
            /* Find length in wchar_t unit w/o terminator: */
            DWORD cwc = cbValue / sizeof(wchar_t);
            while (cwc > 0 && wszBuf[cwc - 1] == '\0')
                cwc--;
            wszBuf[cwc] = '\0';
            if (cwc > 2)
            {
                /* Check if the path leads to a directory with a python.exe file in it. */
                if (!RTPATH_IS_SLASH(wszBuf[cwc - 1]))
                    wszBuf[cwc++] = '\\';
                memcpy(&wszBuf[cwc], s_wszPythonExe, sizeof(s_wszPythonExe));
                DWORD const fAttribs = GetFileAttributesW(wszBuf);
                if (fAttribs != INVALID_FILE_ATTRIBUTES)
                {
                    if (!(fAttribs & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        /* Okay, we found something that can be returned. */
                        if (fReturnExe)
                            cwc += RT_ELEMENTS(s_wszPythonExe) - 1;
                        wszBuf[cwc] = '\0';
                        logStringF(hModule, "getPythonPath: Found: \"%ls\"", wszBuf);

                        NonStandardAssert(cwcPythonPath > cwc);
                        memcpy(pwszPythonPath, wszBuf, cwc * sizeof(wchar_t));
                        pwszPythonPath[cwc] = '\0';
                        rcWinRet = ERROR_SUCCESS;
                    }
                    else
                        logStringF(hModule, "getPythonPath: Warning: Skipping \"%ls\": is a directory (%#x)", wszBuf, fAttribs);
                }
                else
                    logStringF(hModule, "getPythonPath: Warning: Skipping \"%ls\": Does not exist (%u)", wszBuf, GetLastError());
            }
        }
    }

    RegCloseKey(hKeyPythonCore);
    if (rcWinRet != ERROR_SUCCESS)
        logStringF(hModule, "getPythonPath: Unable to find python");
    return rcWinRet;
}

/**
 * Retrieves the absolute path of the Python installation.
 *
 * @returns Windows error code.
 * @param   hModule         Windows installer module handle.
 * @param   pwszPythonPath  Buffer to return the path for python.exe in.
 * @param   cwcPythonPath   Buffer size in UTF-16 units.
 * @param   fReturnExe      Return the path to python.exe if true, otherwise
 *                          just the python install directory.
 */
static UINT getPythonPath(MSIHANDLE hModule, wchar_t *pwszPythonPath, size_t cwcPythonPath, bool fReturnExe = false)
{
    UINT rcWin = getPythonPathEx(hModule, HKEY_LOCAL_MACHINE, pwszPythonPath, cwcPythonPath, fReturnExe);
    if (rcWin != ERROR_SUCCESS)
        rcWin = getPythonPathEx(hModule, HKEY_CURRENT_USER, pwszPythonPath, cwcPythonPath, fReturnExe);
    return rcWin;
}

/**
 * Retrieves the absolute path of the Python executable.
 *
 * @returns Windows error code.
 * @param   hModule         Windows installer module handle.
 * @param   pwszPythonExe   Buffer to return the path for python.exe in.
 * @param   cwcPythonExe    Buffer size in UTF-16 units.
 */
static UINT getPythonExe(MSIHANDLE hModule, wchar_t *pwszPythonExe, size_t cwcPythonExe)
{
    return getPythonPath(hModule, pwszPythonExe, cwcPythonExe, true /*fReturnExe*/);
}

/**
 * Checks if all dependencies for running the VBox Python API bindings are met.
 *
 * @returns VBox status code, or error if depedencies are not met.
 * @param   hModule             Windows installer module handle.
 * @param   pwszPythonExe       Path to Python interpreter image (.exe).
 */
static int checkPythonDependencies(MSIHANDLE hModule, const wchar_t *pwszPythonExe)
{
    /*
     * Check if importing the win32api module works.
     * This is a prerequisite for setting up the VBox API.
     */
    logStringF(hModule, "checkPythonDependencies: Checking for win32api extensions ...");

    UINT rcWin = procRun(hModule, pwszPythonExe, L"-c \"import win32api\"");
    if (rcWin == ERROR_SUCCESS)
        logStringF(hModule, "checkPythonDependencies: win32api found\n");
    else
        logStringF(hModule, "checkPythonDependencies: Importing win32api failed with %u (%#x)\n", rcWin, rcWin);

    return rcWin;
}

/**
 * Checks for a valid Python installation on the system.
 *
 * Called from the MSI installer as custom action.
 *
 * @returns Always ERROR_SUCCESS.
 *          Sets public property VBOX_PYTHON_INSTALLED to "0" (false) or "1" (success).
 *          Sets public property VBOX_PYTHON_PATH to the Python installation path (if found).
 *
 * @param   hModule             Windows installer module handle.
 */
UINT __stdcall IsPythonInstalled(MSIHANDLE hModule)
{
    wchar_t wszPythonPath[RTPATH_MAX];
    UINT rcWin = getPythonPath(hModule, wszPythonPath, RTPATH_MAX);
    if (rcWin == ERROR_SUCCESS)
    {
        logStringF(hModule, "IsPythonInstalled: Python installation found at \"%ls\"", wszPythonPath);
        VBoxMsiSetProp(hModule, L"VBOX_PYTHON_PATH", wszPythonPath);
        VBoxMsiSetProp(hModule, L"VBOX_PYTHON_INSTALLED", L"1");
    }
    else
    {
        logStringF(hModule, "IsPythonInstalled: Error: No suitable Python installation found (%u), skipping installation.", rcWin);
        logStringF(hModule, "IsPythonInstalled: Python seems not to be installed; please download + install the Python Core package.");
        VBoxMsiSetProp(hModule, L"VBOX_PYTHON_INSTALLED", L"0");
    }

    return ERROR_SUCCESS; /* Never return failure. */
}

/**
 * Checks if all dependencies for running the VBox Python API bindings are met.
 *
 * Called from the MSI installer as custom action.
 *
 * @returns Always ERROR_SUCCESS.
 *          Sets public property VBOX_PYTHON_DEPS_INSTALLED to "0" (false) or "1" (success).
 *
 * @param   hModule             Windows installer module handle.
 */
UINT __stdcall ArePythonAPIDepsInstalled(MSIHANDLE hModule)
{
    wchar_t wszPythonExe[RTPATH_MAX];
    UINT dwErr = getPythonExe(hModule, wszPythonExe, RTPATH_MAX);
    if (dwErr == ERROR_SUCCESS)
    {
        dwErr = checkPythonDependencies(hModule, wszPythonExe);
        if (dwErr == ERROR_SUCCESS)
            logStringF(hModule, "ArePythonAPIDepsInstalled: Dependencies look good.");
    }

    if (dwErr != ERROR_SUCCESS)
        logStringF(hModule, "ArePythonAPIDepsInstalled: Failed with dwErr=%u", dwErr);

    VBoxMsiSetProp(hModule, L"VBOX_PYTHON_DEPS_INSTALLED", dwErr == ERROR_SUCCESS ? L"1" : L"0");
    return ERROR_SUCCESS; /* Never return failure. */
}

/**
 * Checks if all required MS CRTs (Visual Studio Redistributable Package) are installed on the system.
 *
 * Called from the MSI installer as custom action.
 *
 * @returns Always ERROR_SUCCESS.
 *          Sets public property VBOX_MSCRT_INSTALLED to "" (false, to use "NOT" in WiX) or "1" (success).
 *
 *          Also exposes public properties VBOX_MSCRT_VER_MIN + VBOX_MSCRT_VER_MAJ strings
 *          with the most recent MSCRT version detected.
 *
 * @param   hModule             Windows installer module handle.
 *
 * @sa      https://docs.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170
 */
UINT __stdcall IsMSCRTInstalled(MSIHANDLE hModule)
{
    HKEY hKeyVS = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X64",
                                0, KEY_READ, &hKeyVS);
    if (lrc == ERROR_SUCCESS)
    {
        DWORD dwVal = 0;
        DWORD cbVal = sizeof(dwVal);
        DWORD dwValueType = REG_DWORD; /** @todo r=bird: output only parameter, optional, so pointless. */
        lrc = RegQueryValueExW(hKeyVS, L"Installed", NULL, &dwValueType, (LPBYTE)&dwVal, &cbVal);
        if (lrc == ERROR_SUCCESS)
        {
            if (dwVal >= 1)
            {
                DWORD dwMaj = 0; /** @todo r=bird: It's purdent to initialize values if you don't bother to check the type and size! */
                lrc = RegQueryValueExW(hKeyVS, L"Major", NULL, &dwValueType, (LPBYTE)&dwMaj, &cbVal);
                if (lrc == ERROR_SUCCESS)
                {
                    VBoxMsiSetPropDWORD(hModule, L"VBOX_MSCRT_VER_MAJ", dwMaj);

                    DWORD dwMin = 0;
                    lrc = RegQueryValueExW(hKeyVS, L"Minor", NULL, &dwValueType, (LPBYTE)&dwMin, &cbVal);
                    if (lrc == ERROR_SUCCESS)
                    {
                        VBoxMsiSetPropDWORD(hModule, L"VBOX_MSCRT_VER_MIN", dwMin);

                        logStringF(hModule, "IsMSCRTInstalled: Found v%u.%u\n", dwMaj, dwMin);

                        /* Check for at least 2019. */
                        if (dwMaj > 14 || (dwMaj == 14 && dwMin >= 20))
                            VBoxMsiSetProp(hModule, L"VBOX_MSCRT_INSTALLED", L"1");
                    }
                    else
                        logStringF(hModule, "IsMSCRTInstalled: Found, but 'Minor' key not present (lrc=%d)", lrc);
                }
                else
                    logStringF(hModule, "IsMSCRTInstalled: Found, but 'Major' key not present (lrc=%d)", lrc);
            }
            else
            {
                logStringF(hModule, "IsMSCRTInstalled: Found, but not marked as installed");
                lrc = ERROR_NOT_INSTALLED;
            }
        }
        else
            logStringF(hModule, "IsMSCRTInstalled: Found, but 'Installed' key not present (lrc=%d)", lrc);
    }

    if (lrc != ERROR_SUCCESS)
        logStringF(hModule, "IsMSCRTInstalled: Failed with lrc=%ld", lrc);

    return ERROR_SUCCESS; /* Never return failure. */
}

/**
 * Checks if the running OS is (at least) Windows 10 (e.g. >= build 10000).
 *
 * Called from the MSI installer as custom action.
 *
 * @returns Always ERROR_SUCCESS.
 *          Sets public property VBOX_IS_WINDOWS_10 to "" (empty / false) or "1" (success).
 *
 * @param   hModule             Windows installer module handle.
 */
UINT __stdcall IsWindows10(MSIHANDLE hModule)
{
    /*
     * Note: We cannot use RtlGetVersion() / GetVersionExW() here, as the Windows Installer service
     *       all shims this, unfortunately. So we have to go another route by querying the major version
     *       number from the registry.
     */
    HKEY hKeyCurVer = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKeyCurVer);
    if (lrc == ERROR_SUCCESS)
    {
        DWORD dwVal = 0;
        DWORD cbVal = sizeof(dwVal);
        DWORD dwValueType = REG_DWORD; /** @todo r=bird: Again, the type is an optional output parameter. pointless to init or pass it unless you check.  */
        lrc = RegQueryValueExW(hKeyCurVer, L"CurrentMajorVersionNumber", NULL, &dwValueType, (LPBYTE)&dwVal, &cbVal);
        if (lrc == ERROR_SUCCESS)
        {
            logStringF(hModule, "IsWindows10/CurrentMajorVersionNumber: %u", dwVal);

            VBoxMsiSetProp(hModule, L"VBOX_IS_WINDOWS_10", dwVal >= 10 ? L"1" : L"");
        }
        else
            logStringF(hModule, "IsWindows10/RegOpenKeyExW: Error reading CurrentMajorVersionNumber (%ld)", lrc);

        RegCloseKey(hKeyCurVer);
    }
    else
        logStringF(hModule, "IsWindows10/RegOpenKeyExW: Error opening CurrentVersion key (%ld)", lrc);

    return ERROR_SUCCESS; /* Never return failure. */
}

/**
 * Installs and compiles the VBox Python bindings.
 *
 * Called from the MSI installer as custom action.
 *
 * @returns Always ERROR_SUCCESS.
 *          Sets public property VBOX_API_INSTALLED to "0" (false) or "1" (success).
 *
 * @param   hModule             Windows installer module handle.
 */
UINT __stdcall InstallPythonAPI(MSIHANDLE hModule)
{
    logStringF(hModule, "InstallPythonAPI: Checking for installed Python environment(s) ...");

    /** @todo r=bird: Can't we get the VBOX_PYTHON_PATH property here? */
    wchar_t wszPythonExe[RTPATH_MAX];
    UINT rcWin = getPythonExe(hModule, wszPythonExe, RTPATH_MAX);
    if (rcWin != ERROR_SUCCESS)
    {
        VBoxMsiSetProp(hModule, L"VBOX_API_INSTALLED", L"0");
        return ERROR_SUCCESS;
    }

    /*
     * Set up the VBox API.
     */
    /* Get the VBox API setup string. */
    WCHAR wszVBoxPythonInstallerPath[RTPATH_MAX];
    int rc = VBoxMsiQueryProp(hModule, L"CustomActionData", wszVBoxPythonInstallerPath, RT_ELEMENTS(wszVBoxPythonInstallerPath));
    if (RT_SUCCESS(rc))
    {
        /* Make sure our current working directory is the VBox installation path. */
        if (SetCurrentDirectoryW(wszVBoxPythonInstallerPath))
        {
            /* Set required environment variables. */
            /** @todo r=andy: That can't be right!
             *
             *  r=bird: The variable probably isn't used because VBOX_MSI_INSTALL_PATH is
             *          set by VBoxMergeApp.wxi. */
            if (SetEnvironmentVariableW(L"VBOX_INSTALL_PATH", wszVBoxPythonInstallerPath))
            {
                logStringF(hModule, "InstallPythonAPI: Invoking vboxapisetup.py in \"%ls\" ...", wszVBoxPythonInstallerPath);

                rcWin = procRun(hModule, wszPythonExe, L"vboxapisetup.py install");
                if (rcWin == ERROR_SUCCESS)
                {
                    logStringF(hModule, "InstallPythonAPI: Installation of vboxapisetup.py successful");

                    /*
                     * Do some sanity checking if the VBox API works.
                     */
                    logStringF(hModule, "InstallPythonAPI: Validating VBox API ...");

                    rcWin = procRun(hModule, wszPythonExe, L"-c \"from vboxapi import VirtualBoxManager\"");
                    if (rcWin == ERROR_SUCCESS)
                    {
                        logStringF(hModule, "InstallPythonAPI: VBox API looks good.");
                        VBoxMsiSetProp(hModule, L"VBOX_API_INSTALLED", L"1");
                        return ERROR_SUCCESS;
                    }

                    /* failed */
                    logStringF(hModule, "InstallPythonAPI: Validating VBox API failed with %u (%#x)", rcWin, rcWin);
                }
                else
                    logStringF(hModule, "InstallPythonAPI: Calling vboxapisetup.py failed with %u (%#x)", rcWin, rcWin);
            }
            else
                logStringF(hModule, "InstallPythonAPI: Could set environment variable VBOX_INSTALL_PATH: LastError=%u",
                           GetLastError());
        }
        else
            logStringF(hModule, "InstallPythonAPI: Could set working directory to \"%ls\": LastError=%u",
                       wszVBoxPythonInstallerPath, GetLastError());
    }
    else
        logStringF(hModule, "InstallPythonAPI: Unable to retrieve VBox installation directory: rc=%Rrc", rc);

    VBoxMsiSetProp(hModule, L"VBOX_API_INSTALLED", L"0");
    logStringF(hModule, "InstallPythonAPI: Installation failed");
    return ERROR_SUCCESS; /* Do not fail here. */
}

static LONG installBrandingValue(MSIHANDLE hModule,
                                 const WCHAR *pwszFileName,
                                 const WCHAR *pwszSection,
                                 const WCHAR *pwszValue)
{
    LONG rc;
    WCHAR wszValue[MAX_PATH];
    if (GetPrivateProfileStringW(pwszSection, pwszValue, NULL, wszValue, sizeof(wszValue), pwszFileName) > 0)
    {
        WCHAR wszKey[MAX_PATH + 64];
        if (RTUtf16ICmpAscii(pwszSection, "General") != 0)
            RTUtf16Printf(wszKey, RT_ELEMENTS(wszKey), "SOFTWARE\\%s\\VirtualBox\\Branding\\%ls", VBOX_VENDOR_SHORT, pwszSection);
        else
            RTUtf16Printf(wszKey, RT_ELEMENTS(wszKey), "SOFTWARE\\%s\\VirtualBox\\Branding", VBOX_VENDOR_SHORT);

        HKEY hkBranding = NULL;
        rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszKey, 0, KEY_WRITE, &hkBranding);
        if (rc == ERROR_SUCCESS)
        {
            rc = RegSetValueExW(hkBranding,
                                pwszValue,
                                NULL,
                                REG_SZ,
                                (BYTE *)wszValue,
                                (DWORD)RTUtf16Len(wszValue));
            if (rc != ERROR_SUCCESS)
                logStringF(hModule, "InstallBranding: Could not write value %s! Error %d", pwszValue, rc);
            RegCloseKey(hkBranding);
        }
    }
    else
        rc = ERROR_NOT_FOUND;
    return rc;
}

/**
 * @note Both paths strings must have an extra terminator.
 */
static UINT CopyDir(MSIHANDLE hModule, const WCHAR *pwszzDstDir, const WCHAR *pwszzSrcDir)
{
    NonStandardAssert(pwszzDstDir[RTUtf16Len(pwszzDstDir) + 1] == '\0');
    NonStandardAssert(pwszzSrcDir[RTUtf16Len(pwszzSrcDir) + 1] == '\0');

    SHFILEOPSTRUCTW s = {0};
    s.hwnd = NULL;
    s.wFunc = FO_COPY;
    s.pTo = pwszzDstDir;
    s.pFrom = pwszzSrcDir;
    s.fFlags = FOF_SILENT
             | FOF_NOCONFIRMATION
             | FOF_NOCONFIRMMKDIR
             | FOF_NOERRORUI;

    logStringF(hModule, "CopyDir: pwszzDstDir=%ls, pwszzSrcDir=%ls", pwszzDstDir, pwszzSrcDir);
    int r = SHFileOperationW(&s);
    if (r == 0)
        return ERROR_SUCCESS;
    logStringF(hModule, "CopyDir: Copy operation returned status %#x", r);
    return ERROR_GEN_FAILURE;
}

/**
 * @note The directory string must have two zero terminators!
 */
static UINT RemoveDir(MSIHANDLE hModule, const WCHAR *pwszzDstDir)
{
    NonStandardAssert(pwszzDstDir[RTUtf16Len(pwszzDstDir) + 1] == '\0');

    SHFILEOPSTRUCTW s = {0};
    s.hwnd = NULL;
    s.wFunc = FO_DELETE;
    s.pFrom = pwszzDstDir;
    s.fFlags = FOF_SILENT
             | FOF_NOCONFIRMATION
             | FOF_NOCONFIRMMKDIR
             | FOF_NOERRORUI;

    logStringF(hModule, "RemoveDir: pwszzDstDir=%ls", pwszzDstDir);
    int r = SHFileOperationW(&s);
    if (r == 0)
        return ERROR_SUCCESS;
    logStringF(hModule, "RemoveDir: Remove operation returned status %#x", r);
    return ERROR_GEN_FAILURE;
}

/**
 * @note Both paths strings must have an extra terminator.
 */
static UINT RenameDir(MSIHANDLE hModule, const WCHAR *pwszzDstDir, const WCHAR *pwszzSrcDir)
{
    NonStandardAssert(pwszzDstDir[RTUtf16Len(pwszzDstDir) + 1] == '\0');
    NonStandardAssert(pwszzSrcDir[RTUtf16Len(pwszzSrcDir) + 1] == '\0');

    SHFILEOPSTRUCTW s = {0};
    s.hwnd = NULL;
    s.wFunc = FO_RENAME;
    s.pTo = pwszzDstDir;
    s.pFrom = pwszzSrcDir;
    s.fFlags = FOF_SILENT
             | FOF_NOCONFIRMATION
             | FOF_NOCONFIRMMKDIR
             | FOF_NOERRORUI;

    logStringF(hModule, "RenameDir: pwszzDstDir=%ls, pwszzSrcDir=%ls", pwszzDstDir, pwszzSrcDir);
    int r = SHFileOperationW(&s);
    if (r == 0)
        return ERROR_SUCCESS;
    logStringF(hModule, "RenameDir: Rename operation returned status %#x", r);
    return ERROR_GEN_FAILURE;
}

/** RTPathAppend-like function. */
static UINT AppendToPath(wchar_t *pwszPath, size_t cwcPath, const wchar_t *pwszAppend, bool fDoubleTerm = false)
{
    size_t cwcCurPath = RTUtf16Len(pwszPath);
    size_t cwcSlash   = cwcCurPath > 1 && RTPATH_IS_SLASH(pwszPath[cwcCurPath - 1]) ? 0 : 1;
    while (RTPATH_IS_SLASH(*pwszAppend))
        pwszAppend++;
    size_t cwcAppend  = RTUtf16Len(pwszAppend);
    if (cwcCurPath + cwcCurPath + cwcAppend + fDoubleTerm < cwcPath)
    {
        if (cwcSlash)
            pwszPath[cwcCurPath++] = '\\';
        memcpy(&pwszPath[cwcCurPath], pwszAppend, (cwcAppend + 1) * sizeof(wchar_t));
        if (fDoubleTerm)
            pwszPath[cwcCurPath + cwcAppend + 1] = '\0';
        return ERROR_SUCCESS;
    }
    return ERROR_BUFFER_OVERFLOW;
}

/** RTPathJoin-like function. */
static UINT JoinPaths(wchar_t *pwszPath, size_t cwcPath, wchar_t *pwszPath1, const wchar_t *pwszAppend, bool fDoubleTerm = false)
{
    size_t cwcCurPath = RTUtf16Len(pwszPath1);
    if (cwcCurPath < cwcPath)
    {
        memcpy(pwszPath, pwszPath1, (cwcCurPath + 1) * sizeof(wchar_t));
        return AppendToPath(pwszPath, cwcPath, pwszAppend, fDoubleTerm);
    }
    return ERROR_BUFFER_OVERFLOW;
}

UINT __stdcall UninstallBranding(MSIHANDLE hModule)
{
    logStringF(hModule, "UninstallBranding: Handling branding file ...");

    WCHAR wszPath[RTPATH_MAX];
    int rc = VBoxMsiQueryProp(hModule, L"CustomActionData", wszPath, RT_ELEMENTS(wszPath));
    if (RT_SUCCESS(rc))
    {
        size_t const cwcPath = RTUtf16Len(wszPath);
        rc = AppendToPath(wszPath, RTPATH_MAX, L"custom", true /*fDoubleTerm*/);
        if (rc == ERROR_SUCCESS)
            rc = RemoveDir(hModule, wszPath);

        /* Check for .custom directory from a failed install and remove it. */
        wszPath[cwcPath] = '\0';
        rc = AppendToPath(wszPath, RTPATH_MAX, L".custom", true /*fDoubleTerm*/);
        if (rc == ERROR_SUCCESS)
            rc = RemoveDir(hModule, wszPath);
    }

    logStringF(hModule, "UninstallBranding: Handling done. (rc=%Rrc (ignored))", rc);
    return ERROR_SUCCESS; /* Do not fail here. */
}

UINT __stdcall InstallBranding(MSIHANDLE hModule)
{
    logStringF(hModule, "InstallBranding: Handling branding file ...");

    /*
     * Get the paths.
     */
    wchar_t wszSrcPath[RTPATH_MAX];
    int rc = VBoxMsiQueryProp(hModule, L"SOURCEDIR", wszSrcPath, RT_ELEMENTS(wszSrcPath));
    if (RT_SUCCESS(rc))
    {
        wchar_t wszDstPath[RTPATH_MAX];
        rc = VBoxMsiQueryProp(hModule, L"CustomActionData", wszDstPath, RT_ELEMENTS(wszDstPath));
        if (RT_SUCCESS(rc))
        {
            /*
             * First we copy the src\.custom dir to the target.
             */
            UINT rcWin = AppendToPath(wszSrcPath, RT_ELEMENTS(wszSrcPath) - 1, L".custom", true /*fDoubleTerm*/);
            if (rcWin == ERROR_SUCCESS)
            {
                rcWin = CopyDir(hModule, wszDstPath, wszSrcPath);
                if (rcWin == ERROR_SUCCESS)
                {
                    /*
                     * The rename the '.custom' directory we now got in the target area to 'custom'.
                     */
                    rcWin = JoinPaths(wszSrcPath, RT_ELEMENTS(wszSrcPath), wszDstPath, L".custom", true /*fDoubleTerm*/);
                    if (rc == ERROR_SUCCESS)
                    {
                        rcWin = AppendToPath(wszDstPath, RT_ELEMENTS(wszDstPath), L"custom", true /*fDoubleTerm*/);
                        if (rc == ERROR_SUCCESS)
                            rcWin = RenameDir(hModule, wszDstPath, wszSrcPath);
                    }
                }
            }

            logStringF(hModule, "InstallBranding: Handling done. (rc=%Rrc (ignored))", rc);
        }
    }

    logStringF(hModule, "InstallBranding: Handling done. (rc=%Rrc (ignored))", rc);
    return ERROR_SUCCESS; /* Do not fail here. */
}

static DECLCALLBACK(void) vboxWinDrvInstLogCallback(VBOXWINDRIVERLOGTYPE enmType, const char *pszMsg, void *pvUser)
{
    MSIHANDLE *phModule = (MSIHANDLE *)pvUser;

    switch (enmType)
    {
        case VBOXWINDRIVERLOGTYPE_ERROR:
            logStringF(*phModule, "*** Error: %s", pszMsg);
            break;

        default:
            logStringF(*phModule, "%s", pszMsg);
            break;
    }
}

UINT __stdcall DriverInstall(MSIHANDLE hModule)
{
    logStringF(hModule, "Installing driver ...");

    char *pszInfFile = NULL;
    int rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvInstInfFile", &pszInfFile);
    if (RT_SUCCESS(rc))
    {
        char *pszInfSection = NULL;
        rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvInstInfSection", &pszInfSection);
        if (   RT_SUCCESS(rc)
            || rc == VERR_NOT_FOUND) /* VBoxDrvInstInfSection is optional. */
        {
            char *pszModel = NULL;
            rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvInstModel", &pszModel);
            if (   RT_SUCCESS(rc)
                || rc == VERR_NOT_FOUND) /* VBoxDrvInstModel is optional. */
            {
                char *pszPnpId = NULL;
                rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvInstPnpId", &pszPnpId);
                if (   RT_SUCCESS(rc)
                    || rc == VERR_NOT_FOUND) /* VBoxDrvInstPnpId is optional. */
                {
                    uint32_t fFlags = VBOX_WIN_DRIVERINSTALL_F_NONE;

                    DWORD dwVal;
                    rc = VBoxMsiQueryPropInt32(hModule, "VBoxDrvInstFlagForce", &dwVal);
                    if (RT_SUCCESS(rc))
                        fFlags |= VBOX_WIN_DRIVERINSTALL_F_FORCE;
                    rc = VBoxMsiQueryPropInt32(hModule, "VBoxDrvInstFlagSilent", &dwVal);
                    if (RT_SUCCESS(rc))
                        fFlags |= VBOX_WIN_DRIVERINSTALL_F_SILENT;

                    VBOXWINDRVINST hWinDrvInst;
                    rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 1 /* uVerbostiy */, &vboxWinDrvInstLogCallback, &hModule /* pvUser */);
                    if (RT_SUCCESS(rc))
                    {
                        if (pszInfSection && *pszInfSection)
                            rc = VBoxWinDrvInstInstallExecuteInf(hWinDrvInst, pszInfFile, pszInfSection, fFlags);
                        else
                            rc = VBoxWinDrvInstInstallEx(hWinDrvInst, pszInfFile, pszModel, pszPnpId, fFlags);

                        VBoxWinDrvInstDestroy(hWinDrvInst);
                    }

                    RTStrFree(pszPnpId);
                }

                RTStrFree(pszModel);
            }

            RTStrFree(pszInfSection);
        }

        RTStrFree(pszInfFile);
    }
    else
    {
        logStringF(hModule, "DriverInstall: No INF or invalid file to install specified!");
        if (rc == VERR_NOT_FOUND) /* Give a better clue. */
            rc = VERR_INVALID_PARAMETER;
    }

    logStringF(hModule, "DriverInstall: Handling done (rc=%Rrc)", rc);
    return RT_SUCCESS(rc) ? ERROR_SUCCESS : ERROR_DRIVER_INSTALL_BLOCKED /* Close enough */;
}

UINT __stdcall DriverUninstall(MSIHANDLE hModule)
{
    char *pszInfFile = NULL;
    int rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvUninstInfFile", &pszInfFile);
    if (   RT_SUCCESS(rc)
        || rc == VERR_NOT_FOUND) /* VBoxDrvUninstInfFile is optional. */
    {
        char *pszInfSection = NULL;
        rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvUninstInfSection", &pszInfSection);
        if (   RT_SUCCESS(rc)
            || rc == VERR_NOT_FOUND) /* VBoxDrvUninstInfSection is optional. */
        {
            char *pszModel = NULL;
            rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvUninstModel", &pszModel);
            if (   RT_SUCCESS(rc)
                || rc == VERR_NOT_FOUND) /* VBoxDrvUninstModel is optional. */
            {
                char *pszPnpId = NULL;
                rc = VBoxMsiQueryPropUtf8(hModule, "VBoxDrvUninstPnpId", &pszPnpId);
                if (   RT_SUCCESS(rc)
                    || rc == VERR_NOT_FOUND) /* VBoxDrvUninstPnpId is optional. */
                {
                    VBOXWINDRVINST hWinDrvInst;
                    rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 1 /* uVerbostiy */,
                                                &vboxWinDrvInstLogCallback, &hModule /* pvUser */);
                    if (RT_SUCCESS(rc))
                    {
                        if (pszInfSection && *pszInfSection)
                            rc = VBoxWinDrvInstUninstallExecuteInf(hWinDrvInst, pszInfFile, pszInfSection,
                                                                   VBOX_WIN_DRIVERINSTALL_F_NONE);
                        else
                            rc = VBoxWinDrvInstUninstall(hWinDrvInst, pszInfFile, pszModel, pszPnpId,
                                                         VBOX_WIN_DRIVERINSTALL_F_NONE);

                        VBoxWinDrvInstDestroy(hWinDrvInst);
                    }

                    RTStrFree(pszPnpId);
                }

                RTStrFree(pszModel);
            }

            RTStrFree(pszInfSection);
        }

        RTStrFree(pszInfFile);
    }

    logStringF(hModule, "DriverUninstall: Handling done (rc=%Rrc)", rc);
    return RT_SUCCESS(rc) ? ERROR_SUCCESS : ERROR_DRIVER_STORE_DELETE_FAILED /* Close enough */;
}

UINT __stdcall ServiceControl(MSIHANDLE hModule)
{
    char *pszSvcCtlName;
    int rc = VBoxMsiQueryPropUtf8(hModule, "VBoxSvcCtlName", &pszSvcCtlName);
    if (RT_SUCCESS(rc))
    {
        char *pszSvcCtlFn;
        rc = VBoxMsiQueryPropUtf8(hModule, "VBoxSvcCtlFn", &pszSvcCtlFn);
        if (RT_SUCCESS(rc))
        {
            VBOXWINDRVSVCFN enmFn = VBOXWINDRVSVCFN_INVALID; /* Shut up MSVC. */
            if (!RTStrICmp(pszSvcCtlFn, "start"))
                enmFn = VBOXWINDRVSVCFN_START;
            else if (!RTStrICmp(pszSvcCtlFn, "stop"))
                enmFn = VBOXWINDRVSVCFN_STOP;
            else if (!RTStrICmp(pszSvcCtlFn, "restart"))
                enmFn = VBOXWINDRVSVCFN_RESTART;
            else
                rc = VERR_INVALID_PARAMETER;

            if (RT_SUCCESS(rc))
            {
                RTMSINTERVAL msTimeout = 0; /* Don't wait by default. */
                rc = VBoxMsiQueryPropInt32(hModule, "VBoxSvcCtlWaitMs", (DWORD *)&msTimeout);
                if (   RT_SUCCESS(rc)
                    || rc == VERR_NOT_FOUND) /* VBoxSvcCtlWaitMs is optional. */
                {
                    VBOXWINDRVINST hWinDrvInst;
                    rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 1 /* uVerbostiy */,
                                                &vboxWinDrvInstLogCallback, &hModule /* pvUser */);
                    if (RT_SUCCESS(rc))
                    {
                        rc = VBoxWinDrvInstControlServiceEx(hWinDrvInst, pszSvcCtlName, enmFn,
                                                            msTimeout == 0 ? VBOXWINDRVSVCFN_F_NONE : VBOXWINDRVSVCFN_F_WAIT,
                                                            msTimeout);
                        VBoxWinDrvInstDestroy(hWinDrvInst);
                    }
                }
            }

            RTStrFree(pszSvcCtlFn);
        }

        RTStrFree(pszSvcCtlName);
    }

    logStringF(hModule, "ServiceControl: Handling done (rc=%Rrc)", rc);
    return RT_SUCCESS(rc) ? ERROR_SUCCESS : ERROR_INVALID_SERVICE_CONTROL;
}

#if defined(VBOX_WITH_NETFLT) || defined(VBOX_WITH_NETADP)

/** @todo should use some real VBox app name */
#define VBOX_NETCFG_APP_NAME L"VirtualBox Installer"
#define VBOX_NETCFG_MAX_RETRIES 10
#define NETFLT_PT_INF_REL_PATH L"VBoxNetFlt.inf"
#define NETFLT_MP_INF_REL_PATH L"VBoxNetFltM.inf"
#define NETFLT_ID  L"sun_VBoxNetFlt" /** @todo Needs to be changed (?). */
#define NETADP_ID  L"sun_VBoxNetAdp" /** @todo Needs to be changed (?). */

#define NETLWF_INF_NAME L"VBoxNetLwf.inf"

static MSIHANDLE g_hCurrentModule = NULL;

static UINT _uninstallNetFlt(MSIHANDLE hModule);
static UINT _uninstallNetLwf(MSIHANDLE hModule);

static VOID vboxDrvLoggerCallback(VBOXDRVCFG_LOG_SEVERITY_T enmSeverity, char *pszMsg, void *pvContext)
{
    RT_NOREF1(pvContext);
    switch (enmSeverity)
    {
        case VBOXDRVCFG_LOG_SEVERITY_FLOW:
        case VBOXDRVCFG_LOG_SEVERITY_REGULAR:
            break;
        case VBOXDRVCFG_LOG_SEVERITY_REL:
            if (g_hCurrentModule)
                logStringF(g_hCurrentModule, "%s", pszMsg);
            break;
        default:
            break;
    }
}

static DECLCALLBACK(void) netCfgLoggerCallback(const char *pszString)
{
    if (g_hCurrentModule)
        logStringF(g_hCurrentModule, "%s", pszString);
}

static VOID netCfgLoggerDisable()
{
    if (g_hCurrentModule)
    {
        VBoxNetCfgWinSetLogging(NULL);
        g_hCurrentModule = NULL;
    }
}

static VOID netCfgLoggerEnable(MSIHANDLE hModule)
{
    NonStandardAssert(hModule);

    if (g_hCurrentModule)
        netCfgLoggerDisable();

    g_hCurrentModule = hModule;

    VBoxNetCfgWinSetLogging(netCfgLoggerCallback);
    /* uncomment next line if you want to add logging information from VBoxDrvCfg.cpp */
//    VBoxDrvCfgLoggerSet(vboxDrvLoggerCallback, NULL);
}

static UINT errorConvertFromHResult(MSIHANDLE hModule, HRESULT hr)
{
    UINT uRet;
    switch (hr)
    {
        case S_OK:
            uRet = ERROR_SUCCESS;
            break;

        case NETCFG_S_REBOOT:
        {
            logStringF(hModule, "Reboot required, setting REBOOT property to \"force\"");
            HRESULT hr2 = MsiSetPropertyW(hModule, L"REBOOT", L"Force");
            if (hr2 != ERROR_SUCCESS)
                logStringF(hModule, "Failed to set REBOOT property, error = %#x", hr2);
            uRet = ERROR_SUCCESS; /* Never fail here. */
            break;
        }

        default:
            logStringF(hModule, "Converting unhandled HRESULT (%#x) to ERROR_GEN_FAILURE", hr);
            uRet = ERROR_GEN_FAILURE;
    }
    return uRet;
}

static MSIHANDLE createNetCfgLockedMsgRecord(MSIHANDLE hModule)
{
    MSIHANDLE hRecord = MsiCreateRecord(2);
    if (hRecord)
    {
        UINT uErr = MsiRecordSetInteger(hRecord, 1, 25001);
        if (uErr != ERROR_SUCCESS)
        {
            logStringF(hModule, "createNetCfgLockedMsgRecord: MsiRecordSetInteger failed, error = %#x", uErr);
            MsiCloseHandle(hRecord);
            hRecord = NULL;
        }
    }
    else
        logStringF(hModule, "createNetCfgLockedMsgRecord: Failed to create a record");

    return hRecord;
}

static UINT doNetCfgInit(MSIHANDLE hModule, INetCfg **ppnc, BOOL bWrite)
{
    MSIHANDLE hMsg = NULL;
    UINT uErr = ERROR_GEN_FAILURE;
    int MsgResult;
    int cRetries = 0;

    do
    {
        LPWSTR lpszLockedBy;
        HRESULT hr = VBoxNetCfgWinQueryINetCfg(ppnc, bWrite, VBOX_NETCFG_APP_NAME, 10000, &lpszLockedBy);
        if (hr != NETCFG_E_NO_WRITE_LOCK)
        {
            if (FAILED(hr))
                logStringF(hModule, "doNetCfgInit: VBoxNetCfgWinQueryINetCfg failed, error = %#x", hr);
            uErr = errorConvertFromHResult(hModule, hr);
            break;
        }

        /* hr == NETCFG_E_NO_WRITE_LOCK */

        if (!lpszLockedBy)
        {
            logStringF(hModule, "doNetCfgInit: lpszLockedBy == NULL, breaking");
            break;
        }

        /* on vista the 6to4svc.dll periodically maintains the lock for some reason,
         * if this is the case, increase the wait period by retrying multiple times
         * NOTE: we could alternatively increase the wait timeout,
         * however it seems unneeded for most cases, e.g. in case some network connection property
         * dialog is opened, it would be better to post a notification to the user as soon as possible
         * rather than waiting for a longer period of time before displaying it */
        if (   cRetries < VBOX_NETCFG_MAX_RETRIES
            && RTUtf16ICmpAscii(lpszLockedBy, "6to4svc.dll") == 0)
        {
            cRetries++;
            logStringF(hModule, "doNetCfgInit: lpszLockedBy is 6to4svc.dll, retrying %d out of %d", cRetries, VBOX_NETCFG_MAX_RETRIES);
            MsgResult = IDRETRY;
        }
        else
        {
            if (!hMsg)
            {
                hMsg = createNetCfgLockedMsgRecord(hModule);
                if (!hMsg)
                {
                    logStringF(hModule, "doNetCfgInit: Failed to create a message record, breaking");
                    CoTaskMemFree(lpszLockedBy);
                    break;
                }
            }

            UINT rTmp = MsiRecordSetStringW(hMsg, 2, lpszLockedBy);
            NonStandardAssert(rTmp == ERROR_SUCCESS);
            if (rTmp != ERROR_SUCCESS)
            {
                logStringF(hModule, "doNetCfgInit: MsiRecordSetStringW failed, error = #%x", rTmp);
                CoTaskMemFree(lpszLockedBy);
                break;
            }

            MsgResult = MsiProcessMessage(hModule, (INSTALLMESSAGE)(INSTALLMESSAGE_USER | MB_RETRYCANCEL), hMsg);
            NonStandardAssert(MsgResult == IDRETRY || MsgResult == IDCANCEL);
            logStringF(hModule, "doNetCfgInit: MsiProcessMessage returned (%#x)", MsgResult);
        }
        CoTaskMemFree(lpszLockedBy);
    } while(MsgResult == IDRETRY);

    if (hMsg)
        MsiCloseHandle(hMsg);

    return uErr;
}
#endif /* defined(VBOX_WITH_NETFLT) || defined(VBOX_WITH_NETADP) */

#ifdef VBOX_WITH_NETFLT
static UINT vboxNetFltQueryInfArray(MSIHANDLE hModule, OUT LPWSTR pwszPtInf, DWORD cwcPtInf,
                                    OUT LPWSTR pwszMpInf, DWORD cwcMpInf)
{
    DWORD cwcEffBuf = cwcPtInf - RT_MAX(sizeof(NETFLT_PT_INF_REL_PATH), sizeof(NETFLT_MP_INF_REL_PATH)) / sizeof(WCHAR);
    UINT uErr = MsiGetPropertyW(hModule, L"CustomActionData", pwszPtInf, &cwcEffBuf);
    if (   uErr == ERROR_SUCCESS
        && cwcEffBuf > 0)
    {
        int vrc = RTUtf16Copy(pwszMpInf, cwcMpInf, pwszPtInf);
        AssertRCReturn(vrc, ERROR_BUFFER_OVERFLOW);

        vrc = RTUtf16Cat(pwszPtInf, cwcPtInf, NETFLT_PT_INF_REL_PATH);
        AssertRCReturn(vrc, ERROR_BUFFER_OVERFLOW);
        logStringF(hModule, "vboxNetFltQueryInfArray: INF 1: %ls", pwszPtInf);

        vrc = RTUtf16Cat(pwszMpInf, cwcMpInf, NETFLT_MP_INF_REL_PATH);
        AssertRCReturn(vrc, ERROR_BUFFER_OVERFLOW);
        logStringF(hModule, "vboxNetFltQueryInfArray: INF 2: %ls", pwszMpInf);
    }
    else if (uErr != ERROR_SUCCESS)
        logStringF(hModule, "vboxNetFltQueryInfArray: MsiGetPropertyW failed, error = %#x", uErr);
    else
    {
        logStringF(hModule, "vboxNetFltQueryInfArray: Empty installation directory");
        uErr = ERROR_GEN_FAILURE;
    }

    return uErr;
}

static UINT _uninstallNetFlt(MSIHANDLE hModule)
{
    INetCfg *pNetCfg;
    UINT uErr;

    netCfgLoggerEnable(hModule);

    BOOL bOldIntMode = SetupSetNonInteractiveMode(FALSE);

    __try
    {
        logStringF(hModule, "Uninstalling NetFlt");

        uErr = doNetCfgInit(hModule, &pNetCfg, TRUE);
        if (uErr == ERROR_SUCCESS)
        {
            HRESULT hr = VBoxNetCfgWinNetFltUninstall(pNetCfg);
            if (hr != S_OK)
                logStringF(hModule, "UninstallNetFlt: VBoxNetCfgWinUninstallComponent failed, error = %#x", hr);

            uErr = errorConvertFromHResult(hModule, hr);

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);

            logStringF(hModule, "Uninstalling NetFlt done, error = %#x", uErr);
        }
        else
            logStringF(hModule, "UninstallNetFlt: doNetCfgInit failed, error = %#x", uErr);
    }
    __finally
    {
        if (bOldIntMode)
        {
            /* The prev mode != FALSE, i.e. non-interactive. */
            SetupSetNonInteractiveMode(bOldIntMode);
        }
        netCfgLoggerDisable();
    }

    /* Never fail the uninstall even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETFLT */

UINT __stdcall UninstallNetFlt(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETFLT
    _uninstallNetLwf(hModule);
    return _uninstallNetFlt(hModule);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETFLT
static UINT _installNetFlt(MSIHANDLE hModule)
{
    UINT uErr;
    INetCfg *pNetCfg;

    netCfgLoggerEnable(hModule);

    BOOL bOldIntMode = SetupSetNonInteractiveMode(FALSE);

    __try
    {

        logStringF(hModule, "InstallNetFlt: Installing NetFlt");

        uErr = doNetCfgInit(hModule, &pNetCfg, TRUE);
        if (uErr == ERROR_SUCCESS)
        {
            WCHAR wszPtInf[MAX_PATH];
            WCHAR wszMpInf[MAX_PATH];
            uErr = vboxNetFltQueryInfArray(hModule, wszPtInf, RT_ELEMENTS(wszPtInf), wszMpInf, RT_ELEMENTS(wszMpInf));
            if (uErr == ERROR_SUCCESS)
            {
                LPCWSTR const apwszInfs[] = { wszPtInf, wszMpInf };
                HRESULT hr = VBoxNetCfgWinNetFltInstall(pNetCfg, &apwszInfs[0], RT_ELEMENTS(apwszInfs));
                if (FAILED(hr))
                    logStringF(hModule, "InstallNetFlt: VBoxNetCfgWinNetFltInstall failed, error = %#x", hr);

                uErr = errorConvertFromHResult(hModule, hr);
            }
            else
                logStringF(hModule, "InstallNetFlt: vboxNetFltQueryInfArray failed, error = %#x", uErr);

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);

            logStringF(hModule, "InstallNetFlt: Done");
        }
        else
            logStringF(hModule, "InstallNetFlt: doNetCfgInit failed, error = %#x", uErr);
    }
    __finally
    {
        if (bOldIntMode)
        {
            /* The prev mode != FALSE, i.e. non-interactive. */
            SetupSetNonInteractiveMode(bOldIntMode);
        }
        netCfgLoggerDisable();
    }

    /* Never fail the install even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETFLT */

UINT __stdcall InstallNetFlt(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETFLT
    _uninstallNetLwf(hModule);
    return _installNetFlt(hModule);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETFLT
static UINT _uninstallNetLwf(MSIHANDLE hModule)
{
    INetCfg *pNetCfg;
    UINT uErr;

    netCfgLoggerEnable(hModule);

    BOOL bOldIntMode = SetupSetNonInteractiveMode(FALSE);

    __try
    {
        logStringF(hModule, "Uninstalling NetLwf");

        uErr = doNetCfgInit(hModule, &pNetCfg, TRUE);
        if (uErr == ERROR_SUCCESS)
        {
            HRESULT hr = VBoxNetCfgWinNetLwfUninstall(pNetCfg);
            if (hr != S_OK)
                logStringF(hModule, "UninstallNetLwf: VBoxNetCfgWinUninstallComponent failed, error = %#x", hr);

            uErr = errorConvertFromHResult(hModule, hr);

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);

            logStringF(hModule, "Uninstalling NetLwf done, error = %#x", uErr);
        }
        else
            logStringF(hModule, "UninstallNetLwf: doNetCfgInit failed, error = %#x", uErr);
    }
    __finally
    {
        if (bOldIntMode)
        {
            /* The prev mode != FALSE, i.e. non-interactive. */
            SetupSetNonInteractiveMode(bOldIntMode);
        }
        netCfgLoggerDisable();
    }

    /* Never fail the uninstall even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETFLT */

UINT __stdcall UninstallNetLwf(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETFLT
    _uninstallNetFlt(hModule);
    return _uninstallNetLwf(hModule);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETFLT
static UINT _installNetLwf(MSIHANDLE hModule)
{
    UINT uErr;
    INetCfg *pNetCfg;

    netCfgLoggerEnable(hModule);

    BOOL bOldIntMode = SetupSetNonInteractiveMode(FALSE);

    __try
    {

        logStringF(hModule, "InstallNetLwf: Installing NetLwf");

        uErr = doNetCfgInit(hModule, &pNetCfg, TRUE);
        if (uErr == ERROR_SUCCESS)
        {
            WCHAR wszInf[MAX_PATH];
            DWORD cwcInf = RT_ELEMENTS(wszInf) - sizeof(NETLWF_INF_NAME) - 1;
            uErr = MsiGetPropertyW(hModule, L"CustomActionData", wszInf, &cwcInf);
            if (uErr == ERROR_SUCCESS)
            {
                if (cwcInf)
                {
                    if (wszInf[cwcInf - 1] != L'\\')
                    {
                        wszInf[cwcInf++] = L'\\';
                        wszInf[cwcInf]   = L'\0';
                    }

                    int vrc = RTUtf16Cat(wszInf, RT_ELEMENTS(wszInf), NETLWF_INF_NAME);
                    AssertRC(vrc);

                    HRESULT hr = VBoxNetCfgWinNetLwfInstall(pNetCfg, wszInf);
                    if (FAILED(hr))
                        logStringF(hModule, "InstallNetLwf: VBoxNetCfgWinNetLwfInstall failed, error = %#x", hr);

                    uErr = errorConvertFromHResult(hModule, hr);
                }
                else
                {
                    logStringF(hModule, "vboxNetFltQueryInfArray: Empty installation directory");
                    uErr = ERROR_GEN_FAILURE;
                }
            }
            else
                logStringF(hModule, "vboxNetFltQueryInfArray: MsiGetPropertyW failed, error = %#x", uErr);

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);

            logStringF(hModule, "InstallNetLwf: Done");
        }
        else
            logStringF(hModule, "InstallNetLwf: doNetCfgInit failed, error = %#x", uErr);
    }
    __finally
    {
        if (bOldIntMode)
        {
            /* The prev mode != FALSE, i.e. non-interactive. */
            SetupSetNonInteractiveMode(bOldIntMode);
        }
        netCfgLoggerDisable();
    }

    /* Never fail the install even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETFLT */

UINT __stdcall InstallNetLwf(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETFLT
    _uninstallNetFlt(hModule);
    return _installNetLwf(hModule);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}


#if 0 /** @todo r=andy Remove this? */
static BOOL RenameHostOnlyConnectionsCallback(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDev, PVOID pContext)
{
    WCHAR DevName[256];
    DWORD winEr;

    if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, pDev,
            SPDRP_FRIENDLYNAME , /* IN DWORD  Property,*/
              NULL, /*OUT PDWORD  PropertyRegDataType,  OPTIONAL*/
              (PBYTE)DevName, /*OUT PBYTE  PropertyBuffer,*/
              sizeof(DevName), /* IN DWORD  PropertyBufferSize,*/
              NULL /*OUT PDWORD  RequiredSize  OPTIONAL*/
            ))
    {
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, pDev,
                DICS_FLAG_GLOBAL, /* IN DWORD  Scope,*/
                0, /*IN DWORD  HwProfile, */
                DIREG_DRV, /* IN DWORD  KeyType, */
                KEY_READ /*IN REGSAM  samDesired*/
                );
        NonStandardAssert(hKey != INVALID_HANDLE_VALUE);
        if (hKey != INVALID_HANDLE_VALUE)
        {
            WCHAR guid[50];
            DWORD cbGuid=sizeof(guid);
            winEr = RegQueryValueExW(hKey,
              L"NetCfgInstanceId", /*__in_opt     LPCTSTR lpValueName,*/
              NULL, /*__reserved   LPDWORD lpReserved,*/
              NULL, /*__out_opt    LPDWORD lpType,*/
              (LPBYTE)guid, /*__out_opt    LPBYTE lpData,*/
              &cbGuid /*guid__inout_opt  LPDWORD lpcbData*/
            );
            NonStandardAssert(winEr == ERROR_SUCCESS);
            if (winEr == ERROR_SUCCESS)
            {
                WCHAR ConnectoinName[128];
                ULONG cbName = sizeof(ConnectoinName);

                HRESULT hr = VBoxNetCfgWinGenHostonlyConnectionName(DevName, ConnectoinName, &cbName);
                NonStandardAssert(hr == S_OK);
                if (SUCCEEDED(hr))
                {
                    hr = VBoxNetCfgWinRenameConnection(guid, ConnectoinName);
                    NonStandardAssert(hr == S_OK);
                }
            }
        }
        RegCloseKey(hKey);
    }
    else
    {
        NonStandardAssert(0);
    }

    return TRUE;
}
#endif /* 0 */

#ifdef VBOX_WITH_NETADP
static UINT _createHostOnlyInterface(MSIHANDLE hModule, LPCWSTR pwszId, LPCWSTR pwszInfName)
{
    netCfgLoggerEnable(hModule);

    BOOL fSetupModeInteractive = SetupSetNonInteractiveMode(FALSE);

    logStringF(hModule, "CreateHostOnlyInterface: Creating host-only interface");

    HRESULT hr = E_FAIL;
    GUID guid;
    WCHAR wszMpInf[MAX_PATH];
    DWORD cwcMpInf = RT_ELEMENTS(wszMpInf) - (DWORD)RTUtf16Len(pwszInfName) - 1 - 1;
    LPCWSTR pwszInfPath = NULL;
    bool fIsFile = false;
    UINT uErr = MsiGetPropertyW(hModule, L"CustomActionData", wszMpInf, &cwcMpInf);
    if (uErr == ERROR_SUCCESS)
    {
        if (cwcMpInf)
        {
            logStringF(hModule, "CreateHostOnlyInterface: NetAdpDir property = %ls", wszMpInf);
            if (wszMpInf[cwcMpInf - 1] != L'\\')
            {
                wszMpInf[cwcMpInf++] = L'\\';
                wszMpInf[cwcMpInf]   = L'\0';
            }

            int vrc = RTUtf16Cat(wszMpInf, RT_ELEMENTS(wszMpInf), pwszInfName);
            AssertRC(vrc);

            pwszInfPath = wszMpInf;
            fIsFile = true;

            logStringF(hModule, "CreateHostOnlyInterface: Resulting INF path = %ls", pwszInfPath);
        }
        else
            logStringF(hModule, "CreateHostOnlyInterface: VBox installation path is empty");
    }
    else
        logStringF(hModule, "CreateHostOnlyInterface: Unable to retrieve VBox installation path, error = %#x", uErr);

    /* Make sure the inf file is installed. */
    if (pwszInfPath != NULL && fIsFile)
    {
        logStringF(hModule, "CreateHostOnlyInterface: Calling VBoxDrvCfgInfInstall(%ls)", pwszInfPath);
        hr = VBoxDrvCfgInfInstall(pwszInfPath);
        logStringF(hModule, "CreateHostOnlyInterface: VBoxDrvCfgInfInstall returns %#x", hr);
        if (FAILED(hr))
            logStringF(hModule, "CreateHostOnlyInterface: Failed to install INF file, error = %#x", hr);
    }

    if (SUCCEEDED(hr))
    {
        //first, try to update Host Only Network Interface
        BOOL fRebootRequired = FALSE;
        hr = VBoxNetCfgWinUpdateHostOnlyNetworkInterface(pwszInfPath, &fRebootRequired, pwszId);
        if (SUCCEEDED(hr))
        {
            if (fRebootRequired)
            {
                logStringF(hModule, "CreateHostOnlyInterface: Reboot required for update, setting REBOOT property to force");
                HRESULT hr2 = MsiSetPropertyW(hModule, L"REBOOT", L"Force");
                if (hr2 != ERROR_SUCCESS)
                    logStringF(hModule, "CreateHostOnlyInterface: Failed to set REBOOT property for update, error = %#x", hr2);
            }
        }
        else
        {
            //in fail case call CreateHostOnlyInterface
            logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinUpdateHostOnlyNetworkInterface failed, hr = %#x", hr);
            logStringF(hModule, "CreateHostOnlyInterface: calling VBoxNetCfgWinCreateHostOnlyNetworkInterface");
# ifdef VBOXNETCFG_DELAYEDRENAME
            BSTR devId;
            hr = VBoxNetCfgWinCreateHostOnlyNetworkInterface(pwszInfPath, fIsFile, NULL, &guid, &devId, NULL);
# else /* !VBOXNETCFG_DELAYEDRENAME */
            hr = VBoxNetCfgWinCreateHostOnlyNetworkInterface(pwszInfPath, fIsFile, NULL, &guid, NULL, NULL);
# endif /* !VBOXNETCFG_DELAYEDRENAME */
            logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinCreateHostOnlyNetworkInterface returns %#x", hr);
            if (SUCCEEDED(hr))
            {
                ULONG ip = inet_addr("192.168.56.1");
                ULONG mask = inet_addr("255.255.255.0");
                logStringF(hModule, "CreateHostOnlyInterface: calling VBoxNetCfgWinEnableStaticIpConfig");
                hr = VBoxNetCfgWinEnableStaticIpConfig(&guid, ip, mask);
                logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinEnableStaticIpConfig returns %#x", hr);
                if (FAILED(hr))
                    logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinEnableStaticIpConfig failed, error = %#x", hr);
# ifdef VBOXNETCFG_DELAYEDRENAME
                hr = VBoxNetCfgWinRenameHostOnlyConnection(&guid, devId, NULL);
                if (FAILED(hr))
                    logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinRenameHostOnlyConnection failed, error = %#x", hr);
                SysFreeString(devId);
# endif /* VBOXNETCFG_DELAYEDRENAME */
            }
            else
                logStringF(hModule, "CreateHostOnlyInterface: VBoxNetCfgWinCreateHostOnlyNetworkInterface failed, error = %#x", hr);
        }
    }

    if (SUCCEEDED(hr))
        logStringF(hModule, "CreateHostOnlyInterface: Creating host-only interface done");

    /* Restore original setup mode. */
    logStringF(hModule, "CreateHostOnlyInterface: Almost done...");
    if (fSetupModeInteractive)
        SetupSetNonInteractiveMode(fSetupModeInteractive);

    netCfgLoggerDisable();

    logStringF(hModule, "CreateHostOnlyInterface: Returns success (ignoring all failures)");
    /* Never fail the install even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETADP */

UINT __stdcall CreateHostOnlyInterface(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _createHostOnlyInterface(hModule, NETADP_ID, L"VBoxNetAdp.inf");
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

UINT __stdcall Ndis6CreateHostOnlyInterface(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
# if 0 /* Trick for allowing the debugger to be attached. */
    for (unsigned i = 0; i < 128 && !IsDebuggerPresent(); i++)
    {
        logStringF(hModule, "Waiting for debugger to attach: windbg -p %u", GetCurrentProcessId());
        Sleep(1001);
    }
    Sleep(1002);
    __debugbreak();
# endif
    return _createHostOnlyInterface(hModule, NETADP_ID, L"VBoxNetAdp6.inf");
#else /* !VBOX_WITH_NETADP */
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETADP
static UINT _removeHostOnlyInterfaces(MSIHANDLE hModule, LPCWSTR pwszId)
{
    netCfgLoggerEnable(hModule);

    logStringF(hModule, "RemoveHostOnlyInterfaces: Removing all host-only interfaces");

    BOOL fSetupModeInteractive = SetupSetNonInteractiveMode(FALSE);

    HRESULT hr = VBoxNetCfgWinRemoveAllNetDevicesOfId(pwszId);
    if (SUCCEEDED(hr))
    {
        hr = VBoxDrvCfgInfUninstallAllSetupDi(&GUID_DEVCLASS_NET, L"Net", pwszId, SUOI_FORCEDELETE/* could be SUOI_FORCEDELETE */);
        if (FAILED(hr))
            logStringF(hModule, "RemoveHostOnlyInterfaces: NetAdp uninstalled successfully, but failed to remove INF files");
        else
            logStringF(hModule, "RemoveHostOnlyInterfaces: NetAdp uninstalled successfully");
    }
    else
        logStringF(hModule, "RemoveHostOnlyInterfaces: NetAdp uninstall failed, hr = %#x", hr);

    /* Restore original setup mode. */
    if (fSetupModeInteractive)
        SetupSetNonInteractiveMode(fSetupModeInteractive);

    netCfgLoggerDisable();

    /* Never fail the uninstall even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETADP */

UINT __stdcall RemoveHostOnlyInterfaces(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _removeHostOnlyInterfaces(hModule, NETADP_ID);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETADP
static UINT _stopHostOnlyInterfaces(MSIHANDLE hModule, LPCWSTR pwszId)
{
    netCfgLoggerEnable(hModule);

    logStringF(hModule, "StopHostOnlyInterfaces: Stopping all host-only interfaces");

    BOOL fSetupModeInteractive = SetupSetNonInteractiveMode(FALSE);

    HRESULT hr = VBoxNetCfgWinPropChangeAllNetDevicesOfId(pwszId, VBOXNECTFGWINPROPCHANGE_TYPE_DISABLE);
    if (SUCCEEDED(hr))
        logStringF(hModule, "StopHostOnlyInterfaces: Disabling host interfaces was successful, hr = %#x", hr);
    else
        logStringF(hModule, "StopHostOnlyInterfaces: Disabling host interfaces failed, hr = %#x", hr);

    /* Restore original setup mode. */
    if (fSetupModeInteractive)
        SetupSetNonInteractiveMode(fSetupModeInteractive);

    netCfgLoggerDisable();

    /* Never fail the uninstall even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETADP */

UINT __stdcall StopHostOnlyInterfaces(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _stopHostOnlyInterfaces(hModule, NETADP_ID);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETADP
static UINT _updateHostOnlyInterfaces(MSIHANDLE hModule, LPCWSTR pwszInfName, LPCWSTR pwszId)
{
    netCfgLoggerEnable(hModule);

    logStringF(hModule, "UpdateHostOnlyInterfaces: Updating all host-only interfaces");

    BOOL fSetupModeInteractive = SetupSetNonInteractiveMode(FALSE);

    WCHAR wszMpInf[MAX_PATH];
    DWORD cwcMpInf = RT_ELEMENTS(wszMpInf) - (DWORD)RTUtf16Len(pwszInfName) - 1 - 1;
    LPCWSTR pwszInfPath = NULL;
    bool fIsFile = false;
    UINT uErr = MsiGetPropertyW(hModule, L"CustomActionData", wszMpInf, &cwcMpInf);
    if (uErr == ERROR_SUCCESS)
    {
        if (cwcMpInf)
        {
            logStringF(hModule, "UpdateHostOnlyInterfaces: NetAdpDir property = %ls", wszMpInf);
            if (wszMpInf[cwcMpInf - 1] != L'\\')
            {
                wszMpInf[cwcMpInf++] = L'\\';
                wszMpInf[cwcMpInf]   = L'\0';
            }

            int vrc = RTUtf16Cat(wszMpInf, RT_ELEMENTS(wszMpInf), pwszInfName);
             AssertRC(vrc);
            pwszInfPath = wszMpInf;
            fIsFile = true;

            logStringF(hModule, "UpdateHostOnlyInterfaces: Resulting INF path = %ls", pwszInfPath);

            DWORD attrFile = GetFileAttributesW(pwszInfPath);
            if (attrFile == INVALID_FILE_ATTRIBUTES)
            {
                DWORD dwErr = GetLastError();
                logStringF(hModule, "UpdateHostOnlyInterfaces: File \"%ls\" not found, dwErr=%ld", pwszInfPath, dwErr);
            }
            else
            {
                logStringF(hModule, "UpdateHostOnlyInterfaces: File \"%ls\" exists", pwszInfPath);

                BOOL fRebootRequired = FALSE;
                HRESULT hr = VBoxNetCfgWinUpdateHostOnlyNetworkInterface(pwszInfPath, &fRebootRequired, pwszId);
                if (SUCCEEDED(hr))
                {
                    if (fRebootRequired)
                    {
                        logStringF(hModule, "UpdateHostOnlyInterfaces: Reboot required, setting REBOOT property to force");
                        HRESULT hr2 = MsiSetPropertyW(hModule, L"REBOOT", L"Force");
                        if (hr2 != ERROR_SUCCESS)
                            logStringF(hModule, "UpdateHostOnlyInterfaces: Failed to set REBOOT property, error = %#x", hr2);
                    }
                }
                else
                    logStringF(hModule, "UpdateHostOnlyInterfaces: VBoxNetCfgWinUpdateHostOnlyNetworkInterface failed, hr = %#x", hr);
            }
        }
        else
            logStringF(hModule, "UpdateHostOnlyInterfaces: VBox installation path is empty");
    }
    else
        logStringF(hModule, "UpdateHostOnlyInterfaces: Unable to retrieve VBox installation path, error = %#x", uErr);

    /* Restore original setup mode. */
    if (fSetupModeInteractive)
        SetupSetNonInteractiveMode(fSetupModeInteractive);

    netCfgLoggerDisable();

    /* Never fail the update even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETADP */

UINT __stdcall UpdateHostOnlyInterfaces(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _updateHostOnlyInterfaces(hModule, L"VBoxNetAdp.inf", NETADP_ID);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

UINT __stdcall Ndis6UpdateHostOnlyInterfaces(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _updateHostOnlyInterfaces(hModule, L"VBoxNetAdp6.inf", NETADP_ID);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

#ifdef VBOX_WITH_NETADP
static UINT _uninstallNetAdp(MSIHANDLE hModule, LPCWSTR pwszId)
{
    INetCfg *pNetCfg;
    UINT uErr;

    netCfgLoggerEnable(hModule);

    BOOL bOldIntMode = SetupSetNonInteractiveMode(FALSE);

    __try
    {
        logStringF(hModule, "Uninstalling NetAdp");

        uErr = doNetCfgInit(hModule, &pNetCfg, TRUE);
        if (uErr == ERROR_SUCCESS)
        {
            HRESULT hr = VBoxNetCfgWinNetAdpUninstall(pNetCfg, pwszId);
            if (hr != S_OK)
                logStringF(hModule, "UninstallNetAdp: VBoxNetCfgWinUninstallComponent failed, error = %#x", hr);

            uErr = errorConvertFromHResult(hModule, hr);

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);

            logStringF(hModule, "Uninstalling NetAdp done, error = %#x", uErr);
        }
        else
            logStringF(hModule, "UninstallNetAdp: doNetCfgInit failed, error = %#x", uErr);
    }
    __finally
    {
        if (bOldIntMode)
        {
            /* The prev mode != FALSE, i.e. non-interactive. */
            SetupSetNonInteractiveMode(bOldIntMode);
        }
        netCfgLoggerDisable();
    }

    /* Never fail the uninstall even if we did not succeed. */
    return ERROR_SUCCESS;
}
#endif /* VBOX_WITH_NETADP */

UINT __stdcall UninstallNetAdp(MSIHANDLE hModule)
{
#ifdef VBOX_WITH_NETADP
    return _uninstallNetAdp(hModule, NETADP_ID);
#else
    RT_NOREF(hModule);
    return ERROR_SUCCESS;
#endif
}

static bool isTAPDevice(const WCHAR *pwszGUID)
{
    HKEY hNetcard;
    bool bIsTapDevice = false;
    LONG lStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                 L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}",
                                 0, KEY_READ, &hNetcard);
    if (lStatus != ERROR_SUCCESS)
        return false;

    int i = 0;
    for (;;)
    {
        WCHAR wszEnumName[256];
        WCHAR wszNetCfgInstanceId[256];
        DWORD dwKeyType;
        HKEY  hNetCardGUID;

        DWORD dwLen = sizeof(wszEnumName);
        lStatus = RegEnumKeyExW(hNetcard, i, wszEnumName, &dwLen, NULL, NULL, NULL, NULL);
        if (lStatus != ERROR_SUCCESS)
            break;

        lStatus = RegOpenKeyExW(hNetcard, wszEnumName, 0, KEY_READ, &hNetCardGUID);
        if (lStatus == ERROR_SUCCESS)
        {
            dwLen = sizeof(wszNetCfgInstanceId);
            lStatus = RegQueryValueExW(hNetCardGUID, L"NetCfgInstanceId", NULL, &dwKeyType, (LPBYTE)wszNetCfgInstanceId, &dwLen);
            if (   lStatus == ERROR_SUCCESS
                && dwKeyType == REG_SZ)
            {
                WCHAR wszNetProductName[256];
                WCHAR wszNetProviderName[256];

                wszNetProductName[0] = 0;
                dwLen = sizeof(wszNetProductName);
                lStatus = RegQueryValueExW(hNetCardGUID, L"ProductName", NULL, &dwKeyType, (LPBYTE)wszNetProductName, &dwLen);

                wszNetProviderName[0] = 0;
                dwLen = sizeof(wszNetProviderName);
                lStatus = RegQueryValueExW(hNetCardGUID, L"ProviderName", NULL, &dwKeyType, (LPBYTE)wszNetProviderName, &dwLen);

                if (   !RTUtf16Cmp(wszNetCfgInstanceId, pwszGUID)
                    && !RTUtf16Cmp(wszNetProductName, L"VirtualBox TAP Adapter")
                    && (   (!RTUtf16Cmp(wszNetProviderName, L"innotek GmbH")) /* Legacy stuff. */
                        || (!RTUtf16Cmp(wszNetProviderName, L"Sun Microsystems, Inc.")) /* Legacy stuff. */
                        || (!RTUtf16Cmp(wszNetProviderName, MY_WTEXT(VBOX_VENDOR))) /* Reflects current vendor string. */
                       )
                   )
                {
                    bIsTapDevice = true;
                    RegCloseKey(hNetCardGUID);
                    break;
                }
            }
            RegCloseKey(hNetCardGUID);
        }
        ++i;
    }

    RegCloseKey(hNetcard);
    return bIsTapDevice;
}

/** @todo r=andy BUGBUG WTF! Why do we a) set the rc to 0 (success), and b) need this macro at all!?
 *
 * @todo r=bird: Because it's returning a bool, not int? The return code is
 * ignored anyway, both internally in removeNetworkInterface and in it's caller.
 * There is similar code in VBoxNetCfg.cpp, which is probably where it was copied from. */
#define SetErrBreak(args) \
    if (1) { \
        rc = 0; \
        logStringF args; \
        break; \
    } else do {} while (0)

int removeNetworkInterface(MSIHANDLE hModule, const WCHAR *pwszGUID)
{
    int rc = 1;
    do /* break-loop */
    {
        WCHAR wszPnPInstanceId[512] = {0};

        /* We have to find the device instance ID through a registry search */

        HKEY hkeyNetwork = 0;
        HKEY hkeyConnection = 0;

        do /* break-loop */
        {
            WCHAR wszRegLocation[256];
            RTUtf16Printf(wszRegLocation, RT_ELEMENTS(wszRegLocation),
                          "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\%ls", pwszGUID);
            LONG lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszRegLocation, 0, KEY_READ, &hkeyNetwork);
            if (lrc != ERROR_SUCCESS || !hkeyNetwork)
                SetErrBreak((hModule, "VBox HostInterfaces: Host interface network was not found in registry (%ls)! (lrc=%d) [1]",
                             wszRegLocation, lrc));

            lrc = RegOpenKeyExW(hkeyNetwork, L"Connection", 0, KEY_READ, &hkeyConnection);
            if (lrc != ERROR_SUCCESS || !hkeyConnection)
                SetErrBreak((hModule, "VBox HostInterfaces: Host interface network was not found in registry (%ls)! (lrc=%d) [2]",
                             wszRegLocation, lrc));

            DWORD len = sizeof(wszPnPInstanceId);
            DWORD dwKeyType;
            lrc = RegQueryValueExW(hkeyConnection, L"PnPInstanceID", NULL, &dwKeyType, (LPBYTE)&wszPnPInstanceId[0], &len);
            if (lrc != ERROR_SUCCESS || dwKeyType != REG_SZ)
                SetErrBreak((hModule, "VBox HostInterfaces: Host interface network was not found in registry (%ls)! (lrc=%d) [3]",
                             wszRegLocation, lrc));
        }
        while (0);

        if (hkeyConnection)
            RegCloseKey(hkeyConnection);
        if (hkeyNetwork)
            RegCloseKey(hkeyNetwork);

        /*
         * Now we are going to enumerate all network devices and
         * wait until we encounter the right device instance ID
         */

        HDEVINFO hDeviceInfo = INVALID_HANDLE_VALUE;
        BOOL fResult;

        do /* break-loop */
        {
            /* initialize the structure size */
            SP_DEVINFO_DATA DeviceInfoData = { sizeof(DeviceInfoData) };

            /* copy the net class GUID */
            GUID netGuid;
            memcpy(&netGuid, &GUID_DEVCLASS_NET, sizeof (GUID_DEVCLASS_NET));

            /* return a device info set contains all installed devices of the Net class */
            hDeviceInfo = SetupDiGetClassDevs(&netGuid, NULL, NULL, DIGCF_PRESENT);
            if (hDeviceInfo == INVALID_HANDLE_VALUE)
            {
                logStringF(hModule, "VBox HostInterfaces: SetupDiGetClassDevs failed (0x%08X)!", GetLastError());
                SetErrBreak((hModule, "VBox HostInterfaces: Uninstallation failed!"));
            }

            /* enumerate the driver info list */
            BOOL fFoundDevice = FALSE;
            for (DWORD index = 0;; index++)
            {
                fResult = SetupDiEnumDeviceInfo(hDeviceInfo, index, &DeviceInfoData);
                if (!fResult)
                {
                    if (GetLastError() == ERROR_NO_MORE_ITEMS)
                        break;
                    continue;
                }

                /* try to get the hardware ID registry property */
                WCHAR *pwszDeviceHwid;
                DWORD size = 0;
                fResult = SetupDiGetDeviceRegistryProperty(hDeviceInfo,
                                                           &DeviceInfoData,
                                                           SPDRP_HARDWAREID,
                                                           NULL,
                                                           NULL,
                                                           0,
                                                           &size);
                if (!fResult)
                {
                    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                        continue;

                    pwszDeviceHwid = (WCHAR *)RTMemAllocZ(size);
                    if (!pwszDeviceHwid)
                        continue;

                    fResult = SetupDiGetDeviceRegistryProperty(hDeviceInfo,
                                                               &DeviceInfoData,
                                                               SPDRP_HARDWAREID,
                                                               NULL,
                                                               (PBYTE)pwszDeviceHwid,
                                                               size,
                                                               &size);
                    if (!fResult)
                    {
                        RTMemFree(pwszDeviceHwid);
                        continue;
                    }
                }
                else
                {
                    /* something is wrong.  This shouldn't have worked with a NULL buffer */
                    continue;
                }

                for (WCHAR *t = pwszDeviceHwid;
                     *t && t < &pwszDeviceHwid[size / sizeof(WCHAR)];
                     t += RTUtf16Len(t) + 1)
                {
                    if (RTUtf16ICmpAscii(t, "vboxtap") == 0)
                    {
                        /* get the device instance ID */
                        WCHAR wszDevID[MAX_DEVICE_ID_LEN];
                        if (CM_Get_Device_IDW(DeviceInfoData.DevInst, wszDevID, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
                        {
                            /* compare to what we determined before */
                            if (RTUtf16Cmp(wszDevID, wszPnPInstanceId) == 0)
                            {
                                fFoundDevice = TRUE;
                                break;
                            }
                        }
                    }
                }

                RTMemFree(pwszDeviceHwid);

                if (fFoundDevice)
                    break;
            }

            if (fFoundDevice)
            {
                fResult = SetupDiSetSelectedDevice(hDeviceInfo, &DeviceInfoData);
                if (!fResult)
                {
                    logStringF(hModule, "VBox HostInterfaces: SetupDiSetSelectedDevice failed (0x%08X)!", GetLastError());
                    SetErrBreak((hModule, "VBox HostInterfaces: Uninstallation failed!"));
                }

                fResult = SetupDiCallClassInstaller(DIF_REMOVE, hDeviceInfo, &DeviceInfoData);
                if (!fResult)
                {
                    logStringF(hModule, "VBox HostInterfaces: SetupDiCallClassInstaller (DIF_REMOVE) failed (0x%08X)!", GetLastError());
                    SetErrBreak((hModule, "VBox HostInterfaces: Uninstallation failed!"));
                }
            }
            else
                SetErrBreak((hModule, "VBox HostInterfaces: Host interface network device not found!"));
        } while (0);

        /* clean up the device info set */
        if (hDeviceInfo != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(hDeviceInfo);
    } while (0);
    return rc;
}

UINT __stdcall UninstallTAPInstances(MSIHANDLE hModule)
{
    static const wchar_t s_wszNetworkKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}";
    HKEY hCtrlNet;

    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_wszNetworkKey, 0, KEY_READ, &hCtrlNet);
    if (lrc == ERROR_SUCCESS)
    {
        logStringF(hModule, "VBox HostInterfaces: Enumerating interfaces ...");
        for (int i = 0; ; ++i)
        {
            WCHAR wszNetworkGUID[256] = { 0 };
            DWORD dwLen = (DWORD)sizeof(wszNetworkGUID);
            lrc = RegEnumKeyExW(hCtrlNet, i, wszNetworkGUID, &dwLen, NULL, NULL, NULL, NULL);
            if (lrc != ERROR_SUCCESS)
            {
                switch (lrc)
                {
                    case ERROR_NO_MORE_ITEMS:
                        logStringF(hModule, "VBox HostInterfaces: No interfaces found.");
                        break;
                    default:
                        logStringF(hModule, "VBox HostInterfaces: Enumeration failed: %ld", lrc);
                        break;
                }
                break;
            }

            if (isTAPDevice(wszNetworkGUID))
            {
                logStringF(hModule, "VBox HostInterfaces: Removing interface \"%ls\" ...", wszNetworkGUID);
                removeNetworkInterface(hModule, wszNetworkGUID);
                lrc = RegDeleteKeyW(hCtrlNet, wszNetworkGUID);
            }
        }
        RegCloseKey(hCtrlNet);
        logStringF(hModule, "VBox HostInterfaces: Removing interfaces done.");
    }
    return ERROR_SUCCESS;
}


/**
 * This is used to remove the old VBoxDrv service before installation.
 *
 * The current service name is VBoxSup but the INF file won't remove the old
 * one, so we do it manually to try prevent trouble as the device nodes are the
 * same and we would fail starting VBoxSup.sys if VBoxDrv.sys is still loading.
 *
 * Status code is ignored for now as a reboot should fix most potential trouble
 * here (and I don't want to break stuff too badly).
 *
 * @sa @bugref{10162}
 */
UINT __stdcall UninstallVBoxDrv(MSIHANDLE hModule)
{
    /*
     * Try open the service.
     */
    SC_HANDLE hSMgr = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (hSMgr)
    {
        SC_HANDLE hService = OpenServiceW(hSMgr, L"VBoxDrv", DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (hService)
        {
            /*
             * Try stop it before we delete it.
             */
            SERVICE_STATUS Status = { 0, 0, 0, 0, 0, 0, 0 };
            QueryServiceStatus(hService, &Status);
            if (Status.dwCurrentState == SERVICE_STOPPED)
                logStringF(hModule, "VBoxDrv: The service old service was already stopped");
            else
            {
                logStringF(hModule, "VBoxDrv: Stopping the service (state %u)", Status.dwCurrentState);
                if (ControlService(hService, SERVICE_CONTROL_STOP, &Status))
                {
                    /* waiting for it to stop: */
                    int iWait = 100;
                    while (Status.dwCurrentState == SERVICE_STOP_PENDING && iWait-- > 0)
                    {
                        Sleep(100);
                        QueryServiceStatus(hService, &Status);
                    }

                    if (Status.dwCurrentState == SERVICE_STOPPED)
                        logStringF(hModule, "VBoxDrv: Stopped service");
                    else
                        logStringF(hModule, "VBoxDrv: Failed to stop the service, status: %u", Status.dwCurrentState);
                }
                else
                {
                    DWORD const dwErr = GetLastError();
                    if (   Status.dwCurrentState == SERVICE_STOP_PENDING
                        && dwErr == ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
                        logStringF(hModule, "VBoxDrv: Failed to stop the service: stop pending, not accepting control messages");
                    else
                        logStringF(hModule, "VBoxDrv: Failed to stop the service: dwErr=%u status=%u", dwErr, Status.dwCurrentState);
                }
            }

            /*
             * Delete the service, or at least mark it for deletion.
             */
            if (DeleteService(hService))
                logStringF(hModule, "VBoxDrv: Successfully delete service");
            else
                logStringF(hModule, "VBoxDrv: Failed to delete the service: %u", GetLastError());

            CloseServiceHandle(hService);
        }
        else
        {
            DWORD const dwErr = GetLastError();
            if (dwErr == ERROR_SERVICE_DOES_NOT_EXIST)
                logStringF(hModule, "VBoxDrv: Nothing to do, the old service does not exist");
            else
                logStringF(hModule, "VBoxDrv: Failed to open the service: %u", dwErr);
        }

        CloseServiceHandle(hSMgr);
    }
    else
        logStringF(hModule, "VBoxDrv: Failed to open service manager (%u).", GetLastError());

    return ERROR_SUCCESS;
}

