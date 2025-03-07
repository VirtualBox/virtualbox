/* $Id: VBoxWinDrvInst.cpp 108658 2025-03-07 08:00:16Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvInst - Windows driver installation handling.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/win/setupapi.h>
#include <newdev.h> /* For INSTALLFLAG_XXX. */
#include <cfgmgr32.h> /* For MAX_DEVICE_ID_LEN. */
#ifdef RT_ARCH_X86
# include <wintrust.h>
# include <softpub.h>
#endif

#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/cdefs.h>
#include <iprt/dir.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/thread.h> /* For RTThreadSleep(). */
#include <iprt/utf16.h>

#include <package-generated.h>
#include "product-generated.h"

#include <VBox/err.h> /* For VERR_PLATFORM_ARCH_NOT_SUPPORTED.*/
#include <VBox/version.h>

#include <VBox/GuestHost/VBoxWinDrvDefs.h>
#include <VBox/GuestHost/VBoxWinDrvInst.h>
#include <VBox/GuestHost/VBoxWinDrvStore.h>

#include "VBoxWinDrvCommon.h"
#include "VBoxWinDrvInstInternal.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/

/* Defines from newdev.h (WINVER >= _WIN32_WINNT_VISTA). */
#define DIIRFLAG_INF_ALREADY_COPIED             0x00000001
#define DIIRFLAG_FORCE_INF                      0x00000002
#define DIIRFLAG_HW_USING_THE_INF               0x00000004
#define DIIRFLAG_HOTPATCH                       0x00000008
#define DIIRFLAG_NOBACKUP                       0x00000010


/* SetupUninstallOEMInf Flags values. */
#define SUOI_FORCEDELETE                        0x00000001


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The magic value for RTFTPSERVERINTERNAL::u32Magic. */
#define VBOXWINDRVINST_MAGIC               UINT32_C(0x20171201)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define VBOXWINDRVINST_VALID_RETURN_RC(hDrvInst, a_rc) \
    do { \
        AssertPtrReturn((hDrvInst), (a_rc)); \
        AssertReturn((hDrvInst)->u32Magic == VBOXWINDRVINST_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define VBOXWINDRVINST_VALID_RETURN(hDrvInst) VBOXWINDRVINST_VALID_RETURN_RC((hDrvInst), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define VBOXWINDRVINST_VALID_RETURN_VOID(hDrvInst) \
    do { \
        AssertPtrReturnVoid(hDrvInst); \
        AssertReturnVoid((hDrvInst)->u32Magic == VBOXWINDRVINST_MAGIC); \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* newdev.dll: */
typedef BOOL(WINAPI* PFNDIINSTALLDRIVERW) (HWND hwndParent, LPCWSTR InfPath, DWORD Flags, PBOOL NeedReboot);
typedef BOOL(WINAPI* PFNDIUNINSTALLDRIVERW) (HWND hwndParent, LPCWSTR InfPath, DWORD Flags, PBOOL NeedReboot);
typedef BOOL(WINAPI* PFNUPDATEDRIVERFORPLUGANDPLAYDEVICESW) (HWND hwndParent, LPCWSTR HardwareId, LPCWSTR FullInfPath, DWORD InstallFlags, PBOOL bRebootRequired);
/* setupapi.dll: */
typedef VOID(WINAPI* PFNINSTALLHINFSECTIONW) (HWND Window, HINSTANCE ModuleHandle, PCWSTR CommandLine, INT ShowCommand);
typedef BOOL(WINAPI* PFNSETUPCOPYOEMINFW) (PCWSTR SourceInfFileName, PCWSTR OEMSourceMediaLocation, DWORD OEMSourceMediaType, DWORD CopyStyle, PWSTR DestinationInfFileName, DWORD DestinationInfFileNameSize, PDWORD RequiredSize, PWSTR DestinationInfFileNameComponent);
typedef HINF(WINAPI* PFNSETUPOPENINFFILEW) (PCWSTR FileName, PCWSTR InfClass, DWORD InfStyle, PUINT ErrorLine);
typedef VOID(WINAPI* PFNSETUPCLOSEINFFILE) (HINF InfHandle);
typedef BOOL(WINAPI* PFNSETUPDIGETINFCLASSW) (PCWSTR, LPGUID, PWSTR, DWORD, PDWORD);
typedef BOOL(WINAPI* PFNSETUPUNINSTALLOEMINFW) (PCWSTR InfFileName, DWORD Flags, PVOID Reserved);
typedef BOOL(WINAPI *PFNSETUPSETNONINTERACTIVEMODE) (BOOL NonInteractiveFlag);
/* advapi32.dll: */
typedef BOOL(WINAPI *PFNQUERYSERVICESTATUSEX) (SC_HANDLE, SC_STATUS_TYPE, LPBYTE, DWORD, LPDWORD);

/** Function pointer for a general try INF section callback. */
typedef int (*PFNVBOXWINDRVINST_TRYINFSECTION_CALLBACK)(HINF hInf, PCRTUTF16 pwszSection, void *pvCtx);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once structure for run-as-user functions we need. */
DECL_HIDDEN_DATA(RTONCE)                                 g_vboxWinDrvInstResolveOnce              = RTONCE_INITIALIZER;

/* newdev.dll: */
DECL_HIDDEN_DATA(PFNDIINSTALLDRIVERW)                    g_pfnDiInstallDriverW                    = NULL; /* For Vista+ .*/
DECL_HIDDEN_DATA(PFNDIUNINSTALLDRIVERW)                  g_pfnDiUninstallDriverW                  = NULL; /* Since Win 10 version 1703. */
DECL_HIDDEN_DATA(PFNUPDATEDRIVERFORPLUGANDPLAYDEVICESW)  g_pfnUpdateDriverForPlugAndPlayDevicesW  = NULL; /* For < Vista .*/
/* setupapi.dll: */
DECL_HIDDEN_DATA(PFNINSTALLHINFSECTIONW)                 g_pfnInstallHinfSectionW                 = NULL; /* For W2K+. */
DECL_HIDDEN_DATA(PFNSETUPCOPYOEMINFW)                    g_pfnSetupCopyOEMInfW                    = NULL; /* For W2K+. */
DECL_HIDDEN_DATA(PFNSETUPOPENINFFILEW)                   g_pfnSetupOpenInfFileW                   = NULL; /* For W2K+. */
DECL_HIDDEN_DATA(PFNSETUPCLOSEINFFILE)                   g_pfnSetupCloseInfFile                   = NULL; /* For W2K+. */
DECL_HIDDEN_DATA(PFNSETUPDIGETINFCLASSW)                 g_pfnSetupDiGetINFClassW                 = NULL; /* For W2K+. */
DECL_HIDDEN_DATA(PFNSETUPUNINSTALLOEMINFW)               g_pfnSetupUninstallOEMInfW               = NULL; /* For XP+.  */
DECL_HIDDEN_DATA(PFNSETUPSETNONINTERACTIVEMODE)          g_pfnSetupSetNonInteractiveMode          = NULL; /* For W2K+. */
/* advapi32.dll: */
DECL_HIDDEN_DATA(PFNQUERYSERVICESTATUSEX)                g_pfnQueryServiceStatusEx                = NULL; /* For W2K+. */

/**
 * Structure for keeping the internal Windows driver context.
 */
typedef struct VBOXWINDRVINSTINTERNAL
{
    /** Magic value. */
    uint32_t               u32Magic;
    /** Callback function for logging output. Optional and can be NULL. */
    PFNVBOXWINDRIVERLOGMSG pfnLog;
    /** User-supplied pointer for \a pfnLog. Optional and can be NULL. */
    void                  *pvUser;
    /** Currently set verbosity level. */
    unsigned               uVerbosity;
    /** Number of (logged) warnings. */
    unsigned               cWarnings;
    /** Number of (logged) errors. */
    unsigned               cErrors;
    /** Whether a reboot is needed in order to perform the current (un)installation. */
    bool                   fReboot;
    /** OS version to use. Detected on creation. RTSYSTEM_NT_VERSION_GET_XXX style.
     *  Can be overwritten via VBoxWinDrvInstSetOsVersion(). */
    uint64_t               uOsVer;
    /** Parameters for (un)installation. */
    VBOXWINDRVINSTPARMS    Parms;
    /** Driver store entry to use. */
    PVBOXWINDRVSTORE       pStore;
} VBOXWINDRVINSTINTERNAL;
/** Pointer to an internal Windows driver installation context. */
typedef VBOXWINDRVINSTINTERNAL *PVBOXWINDRVINSTINTERNAL;

/**
 * Structure for holding a single DLL import symbol.
 *
 * Ordinal currently ignored.
 */
typedef struct VBOXWINDRVINSTIMPORTSYMBOL
{
    /** Symbol name. */
    const char *pszSymbol;
    /** Function pointer. */
    void       **pfnFunc;
} VBOXWINDRVINSTIMPORTSYMBOL;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int vboxWinDrvParmsDetermine(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms, bool fForce);


/*********************************************************************************************************************************
*   Import tables                                                                                                                *
*********************************************************************************************************************************/

/* setupapi.dll: */
static VBOXWINDRVINSTIMPORTSYMBOL s_aSetupApiImports[] =
{
    { "InstallHinfSectionW", (void **)&g_pfnInstallHinfSectionW },
    { "SetupCopyOEMInfW", (void **)&g_pfnSetupCopyOEMInfW },
    { "SetupUninstallOEMInfW", (void **)&g_pfnSetupUninstallOEMInfW },
    { "SetupOpenInfFileW", (void **)&g_pfnSetupOpenInfFileW },
    { "SetupCloseInfFile", (void **)&g_pfnSetupCloseInfFile },
    { "SetupDiGetINFClassW", (void **)&g_pfnSetupDiGetINFClassW },
    { "SetupSetNonInteractiveMode", (void **)&g_pfnSetupSetNonInteractiveMode }
};

/* newdev.dll: */
static VBOXWINDRVINSTIMPORTSYMBOL s_aNewDevImports[] =
{
    /* Only for Vista / 2008 Server and up. */
    { "DiInstallDriverW", (void **)&g_pfnDiInstallDriverW },
    { "DiUninstallDriverW", (void **)&g_pfnDiUninstallDriverW },
    /* Anything older (must support Windows 2000). */
    { "UpdateDriverForPlugAndPlayDevicesW", (void **)&g_pfnUpdateDriverForPlugAndPlayDevicesW }
};

/* newdev.dll: */
static VBOXWINDRVINSTIMPORTSYMBOL s_aAdvApi32Imports[] =
{
    /* Only for Windows 2000 and up. */
    { "QueryServiceStatusEx", (void **)&g_pfnQueryServiceStatusEx }
};


/*********************************************************************************************************************************
*   Implementation                                                                                                               *
*********************************************************************************************************************************/

/**
 * Logs message, va_list version.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   enmType             Log type to use.
 * @param   pszFormat           Format string to log.
 * @param   args                va_list to use.
 */
DECLINLINE(void) vboxWinDrvInstLogExV(PVBOXWINDRVINSTINTERNAL pCtx,
                                      VBOXWINDRIVERLOGTYPE enmType, const char *pszFormat, va_list args)
{
    if (!pCtx->pfnLog)
        return;

    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    AssertPtrReturnVoid(psz);

    pCtx->pfnLog(enmType, psz, pCtx->pvUser);
    RTStrFree(psz);
}

/**
 * Logs message, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   enmType             Log type to use.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogEx(PVBOXWINDRVINSTINTERNAL pCtx,
                                     VBOXWINDRIVERLOGTYPE enmType, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vboxWinDrvInstLogExV(pCtx, enmType, pszFormat, args);
    va_end(args);
}

/**
 * Logs an error message.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogError(PVBOXWINDRVINSTINTERNAL pCtx, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vboxWinDrvInstLogExV(pCtx, VBOXWINDRIVERLOGTYPE_ERROR, pszFormat, args);
    va_end(args);

    pCtx->cErrors++;
}

/**
 * Logs a warning message.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogWarn(PVBOXWINDRVINSTINTERNAL pCtx, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vboxWinDrvInstLogExV(pCtx, VBOXWINDRIVERLOGTYPE_WARN, pszFormat, args);
    va_end(args);

    pCtx->cWarnings++;
}

/**
 * Logs an information message.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogInfo(PVBOXWINDRVINSTINTERNAL pCtx, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vboxWinDrvInstLogExV(pCtx, VBOXWINDRIVERLOGTYPE_INFO, pszFormat, args);
    va_end(args);
}

/**
 * Logs a verbose message.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   uVerbosity          Verbosity level to use for logging.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogVerbose(PVBOXWINDRVINSTINTERNAL pCtx, unsigned uVerbosity, const char *pszFormat, ...)
{
    if (uVerbosity <= pCtx->uVerbosity)
    {
        va_list args;
        va_start(args, pszFormat);
        vboxWinDrvInstLogExV(pCtx, VBOXWINDRIVERLOGTYPE_VERBOSE, pszFormat, args);
        va_end(args);
    }
}

/**
 * Logs (and indicates) that a reboot is needed.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(void) vboxWinDrvInstLogRebootNeeded(PVBOXWINDRVINSTINTERNAL pCtx, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vboxWinDrvInstLogExV(pCtx, VBOXWINDRIVERLOGTYPE_REBOOT_NEEDED, pszFormat, args);
    va_end(args);
}

/**
 * Logs the last Windows error given via GetLastError().
 *
 * @returns Last Windows error translated into VBox status code.
 * @retval  VERR_INSTALLATION_FAILED if a translation to a VBox status code wasn't possible.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFormat           Format string to log.
 */
DECLINLINE(int) vboxWinDrvInstLogLastError(PVBOXWINDRVINSTINTERNAL pCtx, const char *pszFormat, ...)
{
    DWORD const dwErr = GetLastError();

    va_list args;
    va_start(args, pszFormat);

    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    AssertPtrReturn(psz, VERR_NO_MEMORY);

    int rc = VERR_INSTALLATION_FAILED;

#ifdef DEBUG_andy
    bool const fAssertMayPanic = RTAssertMayPanic();
    RTAssertSetMayPanic(false);
#endif

    /* Try resolving Setup API errors first (we don't handle those in IPRT). */
    const char *pszErr = VBoxWinDrvSetupApiErrToStr(dwErr);
    if (!pszErr) /* Also ask for special winerr.h codes we don't handle in IPRT. */
        pszErr = VBoxWinDrvWinErrToStr(dwErr);
    if (!pszErr)
        rc = RTErrConvertFromWin32(dwErr);

#ifdef DEBUG_andy
    RTAssertSetMayPanic(fAssertMayPanic);
#endif

    if (pszErr)
        vboxWinDrvInstLogError(pCtx, "%s: %s (%ld / %#x)", psz, pszErr, dwErr, dwErr);
    else
        vboxWinDrvInstLogError(pCtx, "%s: %Rrc (%ld / %#x)", psz, rc, dwErr, dwErr);

    RTStrFree(psz);

    va_end(args);

    return rc;
}

/**
 * Resolves a single symbol of a module (DLL).
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   hMod                Module handle to use.
 * @param   pszSymbol           Name of symbol to resolve.
 * @param   pfnFunc             Where to return the function pointer for resolved symbol on success.
 */
DECLINLINE(int) vboxWinDrvInstInstModResolveSym(PVBOXWINDRVINSTINTERNAL pCtx, RTLDRMOD hMod, const char *pszSymbol,
                                                void **pfnFunc)
{
    int rc = RTLdrGetSymbol(hMod, pszSymbol, pfnFunc);
    if (RT_FAILURE(rc))
    {
        vboxWinDrvInstLogVerbose(pCtx, 1, "Warning: Symbol \"%s\" not found (%Rrc)", pszSymbol, rc);
        *pfnFunc = NULL;
    }

    return rc;
}

/**
 * Resolves symbols of a specific module (DLL).
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pszFilename         Path of module to resolve symbols for.
 * @param   pSymbols            Table of symbols to resolve.
 * @param   cSymbols            Number of symbols within \a pSymbols to resolve.
 */
static DECLCALLBACK(int) vboxWinDrvInstResolveMod(PVBOXWINDRVINSTINTERNAL pCtx,
                                                  const char *pszFilename, VBOXWINDRVINSTIMPORTSYMBOL *pSymbols, size_t cSymbols)
{
    vboxWinDrvInstLogVerbose(pCtx, 1, "Resolving symbols for module \"%s\"  ...", pszFilename);

    RTLDRMOD hMod;
    int rc = RTLdrLoadSystem(pszFilename, true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        for (size_t i = 0; i < cSymbols; i++)
        {
            void *pfnFunc;
            rc = vboxWinDrvInstInstModResolveSym(pCtx, hMod, pSymbols[i].pszSymbol, &pfnFunc);
            if (RT_SUCCESS(rc))
                *pSymbols[i].pfnFunc = pfnFunc;
        }

        RTLdrClose(hMod);
    }
    else
        vboxWinDrvInstLogError(pCtx, "Unabled to load module \"%s\" (%Rrc)", pszFilename, rc);

    return rc;
}

/**
 * Initialize the import APIs for run-as-user and special environment support.
 *
 * @returns VBox status code.
 * @param   pvUser              Pointer to VBOXWINDRVINSTINTERNAL.
 */
static DECLCALLBACK(int) vboxWinDrvInstResolveOnce(void *pvUser)
{
    PVBOXWINDRVINSTINTERNAL pCtx = (PVBOXWINDRVINSTINTERNAL)pvUser;

    /*
     * Note: Any use of Difx[app|api].dll imports is forbidden (and also marked as being deprecated since Windows 10)!
     */

    /* rc ignored, keep going */ vboxWinDrvInstResolveMod(pCtx, "setupapi.dll",
                                                          s_aSetupApiImports, RT_ELEMENTS(s_aSetupApiImports));
    /* rc ignored, keep going */ vboxWinDrvInstResolveMod(pCtx, "newdev.dll",
                                                          s_aNewDevImports, RT_ELEMENTS(s_aNewDevImports));
    /* rc ignored, keep going */ vboxWinDrvInstResolveMod(pCtx, "advapi32.dll",
                                                          s_aAdvApi32Imports, RT_ELEMENTS(s_aAdvApi32Imports));
    return VINF_SUCCESS;
}

/**
 * Initializes a driver installation parameters structure.
 *
 * @param   pParms              Installation parameters structure to initialize.
 */
static void vboxWinDrvInstParmsInit(PVBOXWINDRVINSTPARMS pParms)
{
    RT_BZERO(pParms, sizeof(VBOXWINDRVINSTPARMS));
}

/**
 * Destroys a driver installation parameters structure.
 *
 * @param   pParms              Installation parameters structure to destroy.
 */
static void vboxWinDrvInstParmsDestroy(PVBOXWINDRVINSTPARMS pParms)
{
    switch (pParms->enmMode)
    {
        case VBOXWINDRVINSTMODE_INSTALL:
        case VBOXWINDRVINSTMODE_UNINSTALL:
        {
            RTUtf16Free(pParms->u.UnInstall.pwszModel);
            pParms->u.UnInstall.pwszModel = NULL;
            RTUtf16Free(pParms->u.UnInstall.pwszPnpId);
            pParms->u.UnInstall.pwszPnpId = NULL;
            RTUtf16Free(pParms->u.UnInstall.pwszSection);
            pParms->u.UnInstall.pwszSection = NULL;
            break;
        }

        case VBOXWINDRVINSTMODE_INSTALL_INFSECTION:
        case VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION:
        {
            RTUtf16Free(pParms->u.ExecuteInf.pwszSection);
            pParms->u.ExecuteInf.pwszSection = NULL;
            break;
        }

        case VBOXWINDRVINSTMODE_INVALID:
            break;

        default:
            AssertFailed();
            break;
    }

    RTUtf16Free(pParms->pwszInfFile);
    pParms->pwszInfFile = NULL;
}

/**
 * Structure for keeping callback data for vboxDrvInstExecuteInfFileCallback().
 */
typedef struct VBOXDRVINSTINFCALLBACKCTX
{
    /** Pointer to driver installer instance. */
    PVBOXWINDRVINSTINTERNAL pThis;
    /** Weak pointer to INF section being handled. */
    PCRTUTF16               pwszSection;
    /** User-supplied context pointer. */
    void                   *pvSetupContext;
} VBOXDRVINSTINFCALLBACKCTX;
/** Pointer to structure for keeping callback data for vboxDrvInstExecuteInfFileCallback(). */
typedef VBOXDRVINSTINFCALLBACKCTX *PVBOXDRVINSTINFCALLBACKCTX;

/** Callback for SetupInstallFromInfSectionW(). */
RT_C_DECLS_BEGIN /** @todo r=andy Not sure if we have something else to use. */
static UINT WINAPI vboxDrvInstExecuteInfFileCallback(PVOID    pvCtx,
                                                     UINT     uNotification,
                                                     UINT_PTR Param1,
                                                     UINT_PTR Param2)
{
    RT_NOREF(Param2);

    PVBOXDRVINSTINFCALLBACKCTX pCtx = (PVBOXDRVINSTINFCALLBACKCTX)pvCtx;

    vboxWinDrvInstLogVerbose(pCtx->pThis, 4, "Got installation notification %#x", uNotification);

    switch (uNotification)
    {
        case SPFILENOTIFY_NEEDMEDIA:
        {
            PSOURCE_MEDIA_W pSourceMedia = (PSOURCE_MEDIA_W)Param1;
            vboxWinDrvInstLogInfo(pCtx->pThis, "Requesting installation media \"%ls\\%ls\"...",
                                  pSourceMedia->SourcePath, pSourceMedia->SourceFile);
            break;
        }

        case SPFILENOTIFY_STARTCOPY:
        case SPFILENOTIFY_ENDCOPY:
        {
            PFILEPATHS_W pFilePaths = (PFILEPATHS_W)Param1;
            vboxWinDrvInstLogInfo(pCtx->pThis, "%s copying \"%ls\" -> \"%ls\"",
                                  uNotification == SPFILENOTIFY_STARTCOPY
                                  ? "Started" : "Finished", pFilePaths->Source, pFilePaths->Target);
            break;
        }

        case SPFILENOTIFY_RENAMEERROR:
        case SPFILENOTIFY_DELETEERROR:
        case SPFILENOTIFY_COPYERROR:
        {
            PFILEPATHS_W pFilePaths = (PFILEPATHS_W)Param1;
            vboxWinDrvInstLogError(pCtx->pThis, "Rename/Delete/Copy error \"%ls\" -> \"%s\" (%#x)",
                                   pFilePaths->Source, pFilePaths->Target, pFilePaths->Win32Error);
            break;
        }

        case SPFILENOTIFY_TARGETNEWER:
            vboxWinDrvInstLogInfo(pCtx->pThis, "A newer version of the specified file exists on the target");
            break;

        case SPFILENOTIFY_TARGETEXISTS:
            vboxWinDrvInstLogInfo(pCtx->pThis, "A copy of the specified file already exists on the target");
            break;

        default:
            break;
    }

    return SetupDefaultQueueCallbackW(pCtx->pvSetupContext, uNotification, Param1, Param2);;
}
RT_C_DECLS_END

/**
 * Generic function to for probing a list of well-known sections for [un]installation.
 *
 * Due to the nature of INF files this function tries different combinations of decorations (e.g. SectionName[.NTAMD64|.X86])
 * and invokes the given callback for the first found section.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   hInf                Handle of INF file.
 * @param   pwszSection         Section to invoke for [un]installation.
 *                              If NULL, the "DefaultInstall" / "DefaultUninstall" section will be tried.
 * @param   pfnCallback         Callback to invoke for each found section.
 */
static int vboxWinDrvTryInfSectionEx(PVBOXWINDRVINSTINTERNAL pCtx, HINF hInf, PCRTUTF16 pwszSection,
                                     PFNVBOXWINDRVINST_TRYINFSECTION_CALLBACK pfnCallback)
{
    if (pwszSection)
        vboxWinDrvInstLogVerbose(pCtx, 1, "Trying section \"%ls\"", pwszSection);

    /* Sorted by most likely-ness. */
    PCRTUTF16 apwszTryInstallSections[] =
    {
        /* The more specific (using decorations), the better. Try these first. Might be NULL. */
        pwszSection,
        /* The Default[Un]Install sections apply to primitive (and legacy) drivers. */
           pCtx->Parms.enmMode == VBOXWINDRVINSTMODE_INSTALL
        ?  L"DefaultInstall" : L"DefaultUninstall"
    };

    PCRTUTF16 apwszTryInstallDecorations[] =
    {
        /* No decoration. Try that first. */
        L"",
        /* Native architecture. */
        L"" VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR
    };

    int rc = VERR_NOT_FOUND;

    for (size_t i = 0; i < RT_ELEMENTS(apwszTryInstallSections); i++)
    {
        PCRTUTF16 const pwszTrySection = apwszTryInstallSections[i];
        if (!pwszTrySection)
            continue;

        for (size_t d = 0; d < RT_ELEMENTS(apwszTryInstallDecorations); d++)
        {
            RTUTF16 wszTrySection[64];
            rc = RTUtf16Copy(wszTrySection, sizeof(wszTrySection), pwszTrySection);
            AssertRCBreak(rc);
            rc = RTUtf16Cat(wszTrySection, sizeof(wszTrySection), apwszTryInstallDecorations[d]);
            AssertRCBreak(rc);

            rc = pfnCallback(hInf, wszTrySection, pCtx /* pvCtx */);
            if (RT_SUCCESS(rc))
                break;

            if (rc == VERR_FILE_NOT_FOUND) /* File gone already. */
            {
                rc = VINF_SUCCESS;
                break;
            }

            if (rc != VERR_NOT_FOUND)
                vboxWinDrvInstLogError(pCtx, "Trying INF section failed with %Rrc", rc);
        }

        if (RT_SUCCESS(rc)) /* Bail out if callback above succeeded. */
            break;
    }

    if (rc == VERR_NOT_FOUND)
    {
        vboxWinDrvInstLogWarn(pCtx, "No matching sections to try found -- buggy driver?");
        rc = VINF_SUCCESS;
    }

    return rc;
}

/**
 * Generic function to for probing a list of well-known sections for [un]installation.
 *
 * Due to the nature of INF files this function tries different combinations of decorations (e.g. SectionName[.NTAMD64|.X86])
 * and invokes the given callback for the first found section.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pwszInfPathAbs      Absolute path of INF file to use for [un]installation.
 * @param   pwszSection         Section to invoke for [un]installation.
 *                              If NULL, the "DefaultInstall" / "DefaultUninstall" section will be tried.
 * @param   pfnCallback         Callback to invoke for each found section.
 */
static int vboxWinDrvTryInfSection(PVBOXWINDRVINSTINTERNAL pCtx, PCRTUTF16 pwszInfPathAbs, PCRTUTF16 pwszSection,
                                   PFNVBOXWINDRVINST_TRYINFSECTION_CALLBACK pfnCallback)
{
    HINF hInf;
    int rc = VBoxWinDrvInfOpen(pwszInfPathAbs, &hInf);
    if (RT_FAILURE(rc))
    {
        vboxWinDrvInstLogError(pCtx, "Unable to open INF file: %Rrc\n", rc);
        return rc;
    }
    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" opened", pwszInfPathAbs);

    rc = vboxWinDrvTryInfSectionEx(pCtx, hInf, pwszSection, pfnCallback);

    VBoxWinDrvInfClose(hInf);
    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" closed", pwszInfPathAbs);

    return rc;
}

/**
 * Uninstalls a section of a given INF file.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the given section has not been found.
 * @param   pCtx                Windows driver installer context.
 * @param   hInf                Handle of INF file.
 * @param   pwszSection         Section within INF file to uninstall.
 *                              Can have a platform decoration (e.g. "Foobar.NTx86").
 */
static int vboxWinDrvUninstallInfSectionEx(PVBOXWINDRVINSTINTERNAL pCtx, HINF hInf, PCRTUTF16 pwszSection)
{
    AssertPtrReturn(pwszSection, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    vboxWinDrvInstLogInfo(pCtx, "Uninstalling INF section \"%ls\" ...", pwszSection);

    /*
     * Uninstall services (if any).
     */
    RTUTF16 wszSection[64];
    ssize_t const cwchSection = RTUtf16Printf(wszSection, RT_ELEMENTS(wszSection),
                                              "%ls%s", pwszSection, ".Services");
    if (cwchSection > 0)
    {
        /* We always want to be the first service tag in the group order list (if any). */
        DWORD fFlags = SPSVCINST_TAGTOFRONT;
        BOOL fRc = SetupInstallServicesFromInfSectionW(hInf, wszSection, fFlags);
        if (!fRc)
        {
            DWORD const dwErr = GetLastError();
            if (dwErr == ERROR_SECTION_NOT_FOUND)
            {
                vboxWinDrvInstLogVerbose(pCtx, 1, "INF section \"%ls\" not found", wszSection);
                rc = VERR_NOT_FOUND;
            }
            else
            {
                rc = vboxWinDrvInstLogLastError(pCtx, "Could not uninstall INF services section \"%ls\"", wszSection);
                if (rc == VERR_FILE_NOT_FOUND)
                {
                    /* Hint: Getting VERR_FILE_NOT_FOUND here might mean that an old service entry still is dangling around.
                     *       'sc query <service name> won't show this, however.
                     *       Use 'sc delete <service name>' to delete the leftover. */
                    vboxWinDrvInstLogError(pCtx,
                                           "Hint: An old service (SCM) entry might be dangling around.\n"
                                           "Try removing it via 'sc delete <service name>' and try again.");
                }
            }
        }
        else
            vboxWinDrvInstLogInfo(pCtx, "Uninstalling INF services section \"%ls\" successful", wszSection);

    }
    else
    {
        vboxWinDrvInstLogError(pCtx, "Unable to build uninstallation section string");
        rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}

/**
 * Installs a section of a given INF file.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the given section has not been found.
 * @param   pCtx                Windows driver installer context.
 * @param   hInf                Handle of INF file.
 * @param   pwszSection         Section within INF file to install.
 *                              Can have a platform decoration (e.g. "Foobar.NTx86").
 */
static int vboxWinDrvInstallInfSectionEx(PVBOXWINDRVINSTINTERNAL pCtx, HINF hInf, PCRTUTF16 pwszSection)
{
    AssertPtrReturn(pwszSection, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    vboxWinDrvInstLogInfo(pCtx, "Installing INF section \"%ls\" ...", pwszSection);

    VBOXDRVINSTINFCALLBACKCTX CallbackCtx;
    RT_ZERO(CallbackCtx);
    CallbackCtx.pThis = pCtx;
    CallbackCtx.pwszSection = pwszSection;
    CallbackCtx.pvSetupContext = SetupInitDefaultQueueCallback(NULL);

    BOOL fRc = SetupInstallFromInfSectionW(NULL, // hWndOwner
                                           hInf,
                                           pwszSection,
                                           SPINST_ALL, // Flags
                                           NULL, // RelativeKeyRoot
                                           NULL, // SourceRootPath
                                             SP_COPY_NOSKIP
                                           | SP_COPY_IN_USE_NEEDS_REBOOT,
                                           vboxDrvInstExecuteInfFileCallback,
                                           &CallbackCtx,
                                           NULL,  // DeviceInfoSet
                                           NULL); // DeviceInfoData
    if (fRc)
    {
        vboxWinDrvInstLogInfo(pCtx, "Installing INF section \"%ls\" successful", pwszSection);
    }
    else
    {
        DWORD const dwErr = GetLastError();
        /* Seems like newer Windows OSes (seen on Win10) don't like undecorated sections with SetupInstallFromInfSectionW().
         * So ignore this and continue. */
        if (dwErr == ERROR_BADKEY)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "Installing INF section \"%ls\" failed with %#x (%d), ignoring",
                                     pwszSection, dwErr, dwErr);
        }
        else
           rc = vboxWinDrvInstLogLastError(pCtx, "Installing INF section \"%ls\" failed", pwszSection);
    }

    /*
     * Try install services.
     */
    RTUTF16 wszSection[64];
    ssize_t const cwchSection = RTUtf16Printf(wszSection, RT_ELEMENTS(wszSection),
                                              "%ls%ls%s", pwszSection, VBOXWINDRVINF_DECORATION_SEP_UTF16_STR, "Services");
    if (cwchSection > 0)
    {
        /* We always want to be the first service tag in the group order list (if any). */
        DWORD const fFlags = SPSVCINST_TAGTOFRONT;
        fRc = SetupInstallServicesFromInfSectionW(hInf, wszSection, fFlags);
        if (!fRc)
        {
            DWORD const dwErr = GetLastError();
            if (dwErr == ERROR_SECTION_NOT_FOUND)
            {
                vboxWinDrvInstLogVerbose(pCtx, 1, "INF section \"%ls\" not found, skipping", wszSection);
                rc = VERR_NOT_FOUND;
            }
            else if (dwErr == ERROR_SERVICE_MARKED_FOR_DELETE)
                vboxWinDrvInstLogWarn(pCtx, "Service in INF section \"%ls\" already marked for deletion, skipping", wszSection);
            else if (dwErr == ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
                vboxWinDrvInstLogWarn(pCtx, "Service in INF section \"%ls\" does not accept any control commands (probably in starting/stopping state), skipping", wszSection);
            else
            {
                rc = vboxWinDrvInstLogLastError(pCtx, "Could not install INF services section \"%ls\"", wszSection);
                if (rc == VERR_FILE_NOT_FOUND)
                {
                    /* Hint: Getting VERR_FILE_NOT_FOUND here might mean that an old service entry still is dangling around.
                     *       'sc query <service name> won't show this, however.
                     *       Use 'sc delete <service name>' to delete the leftover. */
                    vboxWinDrvInstLogError(pCtx, "An old service (SCM) entry might be dangling around.");
                    vboxWinDrvInstLogInfo (pCtx, "Try removing it via 'sc delete <service name>' and try again.");
                }
            }
        }
        else
            vboxWinDrvInstLogInfo(pCtx, "Installing INF services section \"%ls\" successful", wszSection);

    }
    else
    {
        vboxWinDrvInstLogError(pCtx, "Unable to build section string");
        rc = VERR_BUFFER_OVERFLOW;
    }

    if (CallbackCtx.pvSetupContext)
    {
        SetupTermDefaultQueueCallback(CallbackCtx.pvSetupContext);
        CallbackCtx.pvSetupContext = NULL;
    }

    return rc;
}

/**
 * Installs a section of a given INF file.
 *
 * Only supported for the VBOXWINDRVINSTMODE_INSTALL_INFSECTION + VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION modes.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the given section has not been found.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvInstallInfSection(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    AssertReturn(   pParms->enmMode == VBOXWINDRVINSTMODE_INSTALL_INFSECTION
                 || pParms->enmMode == VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION, VERR_INVALID_PARAMETER);

    HINF hInf;
    int rc = VBoxWinDrvInfOpen(pParms->pwszInfFile, &hInf);
    if (RT_FAILURE(rc))
    {
        vboxWinDrvInstLogError(pCtx, "Unable to open INF file: %Rrc\n", rc);
        return rc;
    }

    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" opened", pParms->pwszInfFile);

    rc = vboxWinDrvInstallInfSectionEx(pCtx, hInf, pParms->u.ExecuteInf.pwszSection);

    VBoxWinDrvInfClose(hInf);
    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" closed", pParms->pwszInfFile);

    return rc;
}

/**
 * Callback implementation for invoking a section for installation.
 *
 * @returns VBox status code.
 * @param   hInf                Handle of INF file to use.
 * @param   pwszSection         Section to invoke.
 * @param   pvCtx               User-supplied pointer. Usually PVBOXWINDRVINSTINTERNAL.
 */
DECLCALLBACK(int) vboxWinDrvInstallTryInfSectionCallback(HINF hInf, PCRTUTF16 pwszSection, void *pvCtx)
{
    PVBOXWINDRVINSTINTERNAL pCtx = (PVBOXWINDRVINSTINTERNAL)pvCtx;

    return vboxWinDrvInstallInfSectionEx(pCtx, hInf, pwszSection);
}

#ifdef RT_ARCH_X86
/** @todo Make use of the regular logging facilities of VBoxWinDrvInst. */
DECLINLINE(int) vboxWinDrvInterceptedWinVerifyTrustError(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    AssertPtrReturn(psz, -1);

    RTStrmPrintf(g_pStdErr, "Error: %s\n", psz);

    RTStrFree(psz);
    va_end(args);

    return -1;
}

/** @todo Make use of the regular logging facilities of VBoxWinDrvInst. */
DECLINLINE(void) vboxWinDrvInterceptedWinVerifyTrustPrint(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    AssertPtrReturnVoid(psz);

    RTStrmPrintf(g_pStdOut, "%s\n", psz);

    RTStrFree(psz);
    va_end(args);
}

/**
 * Interceptor WinVerifyTrust function for SetupApi.dll on Windows 2000, XP,
 * W2K3 and XP64.
 *
 * This crudely modifies the driver verification request from a WHQL/logo driver
 * check to a simple Authenticode check.
 */
static LONG WINAPI vboxWinDrvInterceptedWinVerifyTrust(HWND hwnd, GUID *pActionId, void *pvData)
{
    /*
     * Resolve the real WinVerifyTrust function.
     */
    static decltype(WinVerifyTrust) * volatile s_pfnRealWinVerifyTrust = NULL;
    decltype(WinVerifyTrust) *pfnRealWinVerifyTrust = s_pfnRealWinVerifyTrust;
    if (!pfnRealWinVerifyTrust)
    {
        HMODULE hmod = GetModuleHandleW(L"WINTRUST.DLL");
        if (!hmod)
            hmod = LoadLibraryW(L"WINTRUST.DLL");
        if (!hmod)
        {
            vboxWinDrvInterceptedWinVerifyTrustError("InterceptedWinVerifyTrust: Failed to load wintrust.dll");
            return TRUST_E_SYSTEM_ERROR;
        }
        pfnRealWinVerifyTrust = (decltype(WinVerifyTrust) *)GetProcAddress(hmod, "WinVerifyTrust");
        if (!pfnRealWinVerifyTrust)
        {
            vboxWinDrvInterceptedWinVerifyTrustError("InterceptedWinVerifyTrust: Failed to locate WinVerifyTrust in wintrust.dll");
            return TRUST_E_SYSTEM_ERROR;
        }
        s_pfnRealWinVerifyTrust = pfnRealWinVerifyTrust;
    }

    /*
     * Modify the ID if appropriate.
     */
    static const GUID s_GuidDriverActionVerify       = DRIVER_ACTION_VERIFY;
    static const GUID s_GuidActionGenericChainVerify = WINTRUST_ACTION_GENERIC_CHAIN_VERIFY;
    static const GUID s_GuidActionGenericVerify2     = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    if (pActionId)
    {
        if (memcmp(pActionId, &s_GuidDriverActionVerify, sizeof(*pActionId)) == 0)
        {
            /** @todo don't apply to obvious NT components... */
            vboxWinDrvInterceptedWinVerifyTrustPrint("DRIVER_ACTION_VERIFY: Changing it to WINTRUST_ACTION_GENERIC_VERIFY_V2");
            pActionId = (GUID *)&s_GuidActionGenericVerify2;
        }
        else if (memcmp(pActionId, &s_GuidActionGenericChainVerify, sizeof(*pActionId)) == 0)
            vboxWinDrvInterceptedWinVerifyTrustPrint("WINTRUST_ACTION_GENERIC_CHAIN_VERIFY");
        else if (memcmp(pActionId, &s_GuidActionGenericVerify2, sizeof(*pActionId)) == 0)
            vboxWinDrvInterceptedWinVerifyTrustPrint("WINTRUST_ACTION_GENERIC_VERIFY_V2");
        else
            vboxWinDrvInterceptedWinVerifyTrustPrint("WINTRUST_ACTION_UNKNOWN");
    }

    /*
     * Log the data.
     */
    if (pvData)
    {
        WINTRUST_DATA *pData = (WINTRUST_DATA *)pvData;
        vboxWinDrvInterceptedWinVerifyTrustPrint("                  cbStruct = %ld", pData->cbStruct);
# ifdef DEBUG
        vboxWinDrvInterceptedWinVerifyTrustPrint("                dwUIChoice = %ld", pData->dwUIChoice);
        vboxWinDrvInterceptedWinVerifyTrustPrint("       fdwRevocationChecks = %ld", pData->fdwRevocationChecks);
        vboxWinDrvInterceptedWinVerifyTrustPrint("             dwStateAction = %ld", pData->dwStateAction);
        vboxWinDrvInterceptedWinVerifyTrustPrint("             hWVTStateData = %p", (uintptr_t)pData->hWVTStateData);
# endif
        if (pData->cbStruct >= 7*sizeof(uint32_t))
        {
            switch (pData->dwUnionChoice)
            {
                case WTD_CHOICE_FILE:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("                     pFile = %p", (uintptr_t)pData->pFile);
                    if (RT_VALID_PTR(pData->pFile))
                    {
                        vboxWinDrvInterceptedWinVerifyTrustPrint("           pFile->cbStruct = %ld", pData->pFile->cbStruct);
# ifndef DEBUG
                        if (pData->pFile->hFile)
# endif
                            vboxWinDrvInterceptedWinVerifyTrustPrint("              pFile->hFile = %p", (uintptr_t)pData->pFile->hFile);
                        if (RT_VALID_PTR(pData->pFile->pcwszFilePath))
                            vboxWinDrvInterceptedWinVerifyTrustPrint("      pFile->pcwszFilePath = %ls", pData->pFile->pcwszFilePath);
# ifdef DEBUG
                        else
                            vboxWinDrvInterceptedWinVerifyTrustPrint("      pFile->pcwszFilePath = %ls", (uintptr_t)pData->pFile->pcwszFilePath);
                        vboxWinDrvInterceptedWinVerifyTrustPrint("     pFile->pgKnownSubject = %p", (uintptr_t)pData->pFile->pgKnownSubject);
# endif
                    }
                    break;

                case WTD_CHOICE_CATALOG:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("                  pCatalog = %p", (uintptr_t)pData->pCatalog);
                    if (RT_VALID_PTR(pData->pCatalog))
                    {
                        vboxWinDrvInterceptedWinVerifyTrustPrint("            pCat->cbStruct = %ld", pData->pCatalog->cbStruct);
# ifdef DEBUG
                        vboxWinDrvInterceptedWinVerifyTrustPrint("    pCat->dwCatalogVersion = %ld", pData->pCatalog->dwCatalogVersion);
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszCatalogFilePath))
                            vboxWinDrvInterceptedWinVerifyTrustPrint("pCat->pcwszCatalogFilePath = %ls", pData->pCatalog->pcwszCatalogFilePath);
# ifdef DEBUG
                        else
                            vboxWinDrvInterceptedWinVerifyTrustPrint("pCat->pcwszCatalogFilePath =  %ls", (uintptr_t)pData->pCatalog->pcwszCatalogFilePath);
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszMemberTag))
                            vboxWinDrvInterceptedWinVerifyTrustPrint("      pCat->pcwszMemberTag = %ls", pData->pCatalog->pcwszMemberTag);
# ifdef DEBUG
                        else
                            vboxWinDrvInterceptedWinVerifyTrustPrint("      pCat->pcwszMemberTag = %ls", (uintptr_t)pData->pCatalog->pcwszMemberTag);
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszMemberFilePath))
                            vboxWinDrvInterceptedWinVerifyTrustPrint(" pCat->pcwszMemberFilePath = %ls", pData->pCatalog->pcwszMemberFilePath);
# ifdef DEBUG
                        else
                            vboxWinDrvInterceptedWinVerifyTrustPrint(" pCat->pcwszMemberFilePath = %ls", (uintptr_t)pData->pCatalog->pcwszMemberFilePath);
# else
                        if (pData->pCatalog->hMemberFile)
# endif
                            vboxWinDrvInterceptedWinVerifyTrustPrint("         pCat->hMemberFile = %p", (uintptr_t)pData->pCatalog->hMemberFile);
# ifdef DEBUG
                        vboxWinDrvInterceptedWinVerifyTrustPrint("pCat->pbCalculatedFileHash = %p", (uintptr_t)pData->pCatalog->pbCalculatedFileHash);
                        vboxWinDrvInterceptedWinVerifyTrustPrint("pCat->cbCalculatedFileHash = %ld", pData->pCatalog->cbCalculatedFileHash);
                        vboxWinDrvInterceptedWinVerifyTrustPrint("    pCat->pcCatalogContext = %p", (uintptr_t)pData->pCatalog->pcCatalogContext);
# endif
                    }
                    break;

                case WTD_CHOICE_BLOB:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("                     pBlob = %p\n", (uintptr_t)pData->pBlob);
                    break;

                case WTD_CHOICE_SIGNER:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("                     pSgnr = %p", (uintptr_t)pData->pSgnr);
                    break;

                case WTD_CHOICE_CERT:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("                     pCert = %p", (uintptr_t)pData->pCert);
                    break;

                default:
                    vboxWinDrvInterceptedWinVerifyTrustPrint("             dwUnionChoice = %ld", pData->dwUnionChoice);
                    break;
            }
        }
    }

    /*
     * Make the call.
     */
    vboxWinDrvInterceptedWinVerifyTrustPrint("Calling WinVerifyTrust ...");
    LONG iRet = pfnRealWinVerifyTrust(hwnd, pActionId, pvData);
    vboxWinDrvInterceptedWinVerifyTrustPrint("WinVerifyTrust returns %ld", iRet);

    return iRet;
}

/**
 * Installs an WinVerifyTrust interceptor in setupapi.dll on Windows 2000, XP,
 * W2K3 and XP64.
 *
 * This is a very crude hack to lower the WHQL check to just require a valid
 * Authenticode signature by intercepting the verification call.
 *
 * @return Ignored, just a convenience for saving space in error paths.
 */
static int vboxWinDrvInstallWinVerifyTrustInterceptorInSetupApi(void)
{
    /* Check the version: */
    OSVERSIONINFOW VerInfo = { sizeof(VerInfo) };
    GetVersionExW(&VerInfo);
    if (VerInfo.dwMajorVersion != 5)
        return 1;

    /* The the target module: */
    HMODULE hModSetupApi = GetModuleHandleW(L"SETUPAPI.DLL");
    if (!hModSetupApi)
    {
        DWORD const dwLastErr = GetLastError();
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to locate SETUPAPI.DLL in the process: %ld / %#x",
                                                        dwLastErr, dwLastErr);
    }

    /*
     * Find the delayed import table (at least that's how it's done in the RTM).
     */
    IMAGE_DOS_HEADER const *pDosHdr = (IMAGE_DOS_HEADER const *)hModSetupApi;
    IMAGE_NT_HEADERS const *pNtHdrs = (IMAGE_NT_HEADERS const *)(  (uintptr_t)hModSetupApi
                                                                 + (  pDosHdr->e_magic == IMAGE_DOS_SIGNATURE
                                                                    ? pDosHdr->e_lfanew : 0));
    if (pNtHdrs->Signature != IMAGE_NT_SIGNATURE)
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
    if (pNtHdrs->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
    if (pNtHdrs->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");

    uint32_t const cbDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size;
    if (cbDir < sizeof(IMAGE_DELAYLOAD_DESCRIPTOR))
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
    uint32_t const cbImages = pNtHdrs->OptionalHeader.SizeOfImage;
    if (cbDir >= cbImages)
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
    uint32_t const offDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (offDir > cbImages - cbDir)
        return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");

    /*
     * Scan the entries looking for wintrust.dll.
     */
    IMAGE_DELAYLOAD_DESCRIPTOR const * const paEntries = (IMAGE_DELAYLOAD_DESCRIPTOR const *)((uintptr_t)hModSetupApi + offDir);
    uint32_t const                           cEntries  = cbDir / sizeof(paEntries[0]);
    for (uint32_t iImp = 0; iImp < cEntries; iImp++)
    {
        const char * const pchRva2Ptr = paEntries[iImp].Attributes.RvaBased ? (const char *)hModSetupApi : (const char *)0;
        const char * const pszDllName = &pchRva2Ptr[paEntries[iImp].DllNameRVA];
        if (RTStrICmpAscii(pszDllName, "WINTRUST.DLL") == 0)
        {
            /*
             * Scan the symbol names.
             */
            //uint32_t const    cbHdrs      = pNtHdrs->OptionalHeader.SizeOfHeaders;
            uint32_t * const  pauNameRvas = (uint32_t  *)&pchRva2Ptr[paEntries[iImp].ImportNameTableRVA];
            uintptr_t * const paIat       = (uintptr_t *)&pchRva2Ptr[paEntries[iImp].ImportAddressTableRVA];
            for (uint32_t iSym = 0; pauNameRvas[iSym] != NULL; iSym++)
            {
                IMAGE_IMPORT_BY_NAME const * const pName = (IMAGE_IMPORT_BY_NAME const *)&pchRva2Ptr[pauNameRvas[iSym]];
                if (RTStrCmp(pName->Name, "WinVerifyTrust") == 0)
                {
                    vboxWinDrvInterceptedWinVerifyTrustPrint("Intercepting WinVerifyTrust for SETUPAPI.DLL (old: %p)", paIat[iSym]);
                    paIat[iSym] = (uintptr_t)vboxWinDrvInterceptedWinVerifyTrust;
                    return 0;
                }
            }
            return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
        }
    }
    return vboxWinDrvInterceptedWinVerifyTrustError("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception:");
}
#endif /* RT_ARCH_X86 */

/**
 * Performs the actual driver installation.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvInstallPerform(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    int rc = vboxWinDrvParmsDetermine(pCtx, pParms, false /* fForce */);
    if (RT_FAILURE(rc))
        return rc;

    switch (pParms->enmMode)
    {
        case VBOXWINDRVINSTMODE_INSTALL:
        {
            BOOL fRc = FALSE;
            BOOL fReboot = FALSE;

#ifdef RT_ARCH_X86
            vboxWinDrvInstallWinVerifyTrustInterceptorInSetupApi();
#endif
            /*
             * Pre-install driver.
             */
            DWORD dwInstallFlags = 0;
            if (pCtx->uOsVer >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0)) /* for Vista / 2008 Server and up. */
            {
                if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                    dwInstallFlags |= DIIRFLAG_FORCE_INF;

                vboxWinDrvInstLogVerbose(pCtx, 1, "Using g_pfnDiInstallDriverW(), dwInstallFlags=%#x", dwInstallFlags);

                if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
                    fRc = g_pfnDiInstallDriverW(NULL /* hWndParent */, pParms->pwszInfFile, dwInstallFlags, &fReboot);
                else
                    fRc = TRUE;
                if (!fRc)
                {
                    DWORD const dwErr = GetLastError();

                    /*
                     * Work around an error code wich only appears on old(er) Windows Server editions (e.g. 2012 R2 or 2016)
                     * where SetupAPI tells "unable to mark devices that match new inf", which ultimately results in
                     * ERROR_LINE_NOT_FOUND. This probably is because of primitive drivers which don't have a PnP ID set in
                     * the INF file.
                     *
                     * pnputil.exe also gives the same error in the SetupAPI log when handling the very same INF file.
                     *
                     * So skip this error and pretend everything is fine. */
                    if (dwErr == ERROR_LINE_NOT_FOUND)
                        fRc = true;

                    /*
                     * Work around an error which occurs on Windows Vista, where DiInstallDriverW() can't handle
                     * primitive drivers (i.e. [Manufacturer] section is missing). So try installing the detected
                     * INF section in the next block below.
                     */
                    if (dwErr == ERROR_WRONG_INF_TYPE)
                        fRc = true;

                    /* For anything else we want to get notified that something isn't working. */
                    if (!fRc)
                    {
                        switch (dwErr)
                        {
                            case ERROR_AUTHENTICODE_TRUST_NOT_ESTABLISHED:
                            {
                                /* For silent installs give a clue why this might have failed. */
                                if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT)
                                    vboxWinDrvInstLogWarn(pCtx, "Silent installation was selected, but required certificates "
                                                                "were not pre-installed into the Windows drvier store, so "
                                                                "the installation will be rejected automatically");
                                RT_FALL_THROUGH();
                            }

                            default:
                                rc = vboxWinDrvInstLogLastError(pCtx, "DiInstallDriverW() failed");
                                break;
                        }
                    }
                }

                if (fRc)
                    rc = vboxWinDrvTryInfSection(pCtx,
                                                 pParms->pwszInfFile, pParms->u.UnInstall.pwszSection,
                                                 vboxWinDrvInstallTryInfSectionCallback);
            }
            else /* For Windows 2000 and below. */
            {
                if (pParms->u.UnInstall.pwszPnpId)
                {
                    if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT)
                    {
                        /* Using INSTALLFLAG_NONINTERACTIVE will trigger an invalid parameter error on Windows 2000. */
                        if (pCtx->uOsVer >= RTSYSTEM_MAKE_NT_VERSION(5, 1, 0))
                            dwInstallFlags |= INSTALLFLAG_NONINTERACTIVE;
                        else
                            vboxWinDrvInstLogWarn(pCtx, "This version of Windows does not support silent installs");
                    }

                    if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                        dwInstallFlags |= INSTALLFLAG_FORCE;

                    vboxWinDrvInstLogVerbose(pCtx, 4, "Using g_pfnUpdateDriverForPlugAndPlayDevicesW(), pwszPnpId=%ls, pwszInfFile=%ls, dwInstallFlags=%#x",
                                             pParms->u.UnInstall.pwszPnpId, pParms->pwszInfFile, dwInstallFlags);

                    if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
                        fRc = g_pfnUpdateDriverForPlugAndPlayDevicesW(NULL /* hWndParent */,
                                                                      pParms->u.UnInstall.pwszPnpId,
                                                                      pParms->pwszInfFile, dwInstallFlags, &fReboot);
                    else
                        fRc = TRUE;

                    if (!fRc)
                    {
                        DWORD const dwErr = GetLastError();
                        switch (dwErr)
                        {
                            case ERROR_NO_SUCH_DEVINST:
                            {
                                vboxWinDrvInstLogInfo(pCtx, "Device (\"%ls\") not found (yet), pre-installing driver ...",
                                                      pParms->u.UnInstall.pwszPnpId);
                                break;
                            }

                            case ERROR_NO_DRIVER_SELECTED:
                            {
                                vboxWinDrvInstLogWarn(pCtx, "Not able to select a driver from the given INF, using given model");
                                break;
                            }

                            case ERROR_AUTHENTICODE_TRUST_NOT_ESTABLISHED:
                            {
                                /* For silent installs give a clue why this might have failed. */
                                if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT)
                                    vboxWinDrvInstLogWarn(pCtx, "Silent installation was selected, but required certificates "
                                                                "were not pre-installed into the Windows drvier store, so "
                                                                "the installation will be rejected automatically");
                                RT_FALL_THROUGH();
                            }

                            default:
                                rc = vboxWinDrvInstLogLastError(pCtx, "Installation(UpdateDriverForPlugAndPlayDevicesW) failed");
                                break;
                        }
                    }
                }

                if (RT_FAILURE(rc))
                    break;

                /*
                 * Pre-install driver.
                 */
                RTUTF16 wszInfFileAbs[RTPATH_MAX] = { 0 };
                LPWSTR  pwszInfFile = NULL;
                if (   GetFullPathNameW(pParms->pwszInfFile, RT_ELEMENTS(wszInfFileAbs), wszInfFileAbs, &pwszInfFile)
                    && pwszInfFile)
                {
                    RTUTF16 wszSrcPathAbs[RTPATH_MAX] = { 0 };
                    rc = RTUtf16CopyEx(wszSrcPathAbs, RT_ELEMENTS(wszSrcPathAbs), wszInfFileAbs,
                                       RTUtf16Len(wszInfFileAbs) - RTUtf16Len(pwszInfFile));
                    if (RT_SUCCESS(rc))
                    {
                        RTUTF16 wszDstPathAbs[RTPATH_MAX] = { 0 };
                        if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
                            fRc = g_pfnSetupCopyOEMInfW(wszInfFileAbs, wszSrcPathAbs, SPOST_PATH, 0,
                                                        wszDstPathAbs, RT_ELEMENTS(wszDstPathAbs), NULL, NULL);
                        else
                            fRc = TRUE;

                        vboxWinDrvInstLogVerbose(pCtx, 1, "   INF file: %ls", wszInfFileAbs);
                        vboxWinDrvInstLogVerbose(pCtx, 1, "Source path: %ls", wszSrcPathAbs);
                        vboxWinDrvInstLogVerbose(pCtx, 1, "  Dest path: %ls", wszDstPathAbs);

                        if (fRc)
                            vboxWinDrvInstLogInfo(pCtx, "Copying OEM INF successful");
                        else
                            rc = vboxWinDrvInstLogLastError(pCtx, "Installation(SetupCopyOEMInfW) failed");
                    }
                }
                else
                    rc = vboxWinDrvInstLogLastError(pCtx, "GetFullPathNameW() failed");

                rc = vboxWinDrvTryInfSection(pCtx,
                                             pParms->pwszInfFile, pParms->u.UnInstall.pwszSection,
                                             vboxWinDrvInstallTryInfSectionCallback);
            }

            if (RT_FAILURE(rc))
                break;

            pCtx->fReboot = RT_BOOL(fReboot);
            break;
        }

        case VBOXWINDRVINSTMODE_INSTALL_INFSECTION:
        {
            rc = vboxWinDrvInstallInfSection(pCtx, pParms);
            break;
        }

        default:
            break;
    }

    return rc;
}

/**
 * Returns whether the given (in)installation parameters are valid or not.
 *
 * @returns \c true if valid, \c false if not.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver (un)installation parameters to validate.
 */
static bool vboxWinDrvParmsAreValid(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    if (pParms->u.UnInstall.pwszPnpId)
    {
        size_t const cchPnpId = RTUtf16Len(pParms->u.UnInstall.pwszPnpId);
        if (   !cchPnpId
            || cchPnpId > MAX_DEVICE_ID_LEN)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "PnP ID not specified explicitly or invalid");
            return false;
        }
    }

    return true;
}

/**
 * Determines (un)installation parameters from a given set of parameters, logged.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if no valid parameters could be determined.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to determine for.
 * @param   fForce              Whether to overwrite already set parameters or not.
 *
 * @note    Only can deal with the first model / PnP ID found for now.
 */
static int vboxWinDrvParmsDetermine(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms, bool fForce)
{
    int rc;

    /* INF file given? */
    if (pParms->pwszInfFile)
    {
        HINF hInf;
        rc = VBoxWinDrvInfOpen(pParms->pwszInfFile, &hInf);
        if (RT_SUCCESS(rc))
        {
            /* Get the INF type first. */
            PRTUTF16 pwszMainSection;
            VBOXWINDRVINFTYPE enmType = VBoxWinDrvInfGetTypeEx(hInf, &pwszMainSection);
            if (enmType != VBOXWINDRVINFTYPE_INVALID)
            {
                vboxWinDrvInstLogVerbose(pCtx, 1, "INF type is: %s",
                                           enmType == VBOXWINDRVINFTYPE_NORMAL
                                         ? "Normal" : "Primitive");
                /*
                 * Determine model.
                 */
                if (   !pParms->u.UnInstall.pwszModel
                    || fForce)
                {
                    vboxWinDrvInstLogVerbose(pCtx, 1, "Determining model ...");
                    if (fForce)
                    {
                        RTUtf16Free(pParms->u.UnInstall.pwszModel);
                        pParms->u.UnInstall.pwszModel = NULL;
                    }
                    rc = VBoxWinDrvInfQueryFirstModel(hInf, pwszMainSection, &pParms->u.UnInstall.pwszModel);
                    if (RT_SUCCESS(rc))
                    {
                        RTUtf16Free(pParms->u.UnInstall.pwszSection);
                        pParms->u.UnInstall.pwszSection = NULL;

                        /* Now that we have determined the model, try if there is a section in the INF file for this model. */
                        rc = VBoxWinDrvInfQueryInstallSection(hInf, pParms->u.UnInstall.pwszModel,
                                                              &pParms->u.UnInstall.pwszSection);
                        if (RT_FAILURE(rc))
                        {
                            switch (enmType)
                            {
                                case VBOXWINDRVINFTYPE_NORMAL:
                                {
                                    vboxWinDrvInstLogError(pCtx, "No section to install found, can't continue");
                                    break;
                                }

                                case VBOXWINDRVINFTYPE_PRIMITIVE:
                                {
                                    /* If for the given model there is no install section, set the section to main section
                                     * we got when we determined the INF type.
                                     *
                                     * This will be mostly the case for primitive drivers. */
                                    if (rc == VERR_NOT_FOUND)
                                    {
                                        pParms->u.UnInstall.pwszSection = RTUtf16Dup(pwszMainSection);
                                        if (pParms->u.UnInstall.pwszSection)
                                        {
                                            rc = VINF_SUCCESS;
                                        }
                                        else
                                            rc = VERR_NO_MEMORY;
                                    }
                                    break;
                                }

                                default:
                                    AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
                                    break;
                            }
                        }
                    }
                    else
                    {
                        switch (rc)
                        {
                            case VERR_PLATFORM_ARCH_NOT_SUPPORTED:
                            {
                                vboxWinDrvInstLogError(pCtx, "Model found, but platform is not supported");
                                break;
                            }

                            case VERR_NOT_FOUND:
                            {
                                vboxWinDrvInstLogError(pCtx, "No model found to install found -- buggy driver?");
                                break;
                            }

                            default:
                                break;
                        }
                    }
                }

                /*
                 * Determine PnP ID.
                 *
                 * Only available in non-primitive drivers.
                 */
                if (   enmType == VBOXWINDRVINFTYPE_NORMAL
                    && (   !pParms->u.UnInstall.pwszPnpId
                        || fForce))
                {
                    if (pParms->u.UnInstall.pwszModel)
                    {
                        vboxWinDrvInstLogVerbose(pCtx, 1, "Determining PnP ID ...");
                        if (fForce)
                        {
                            RTUtf16Free(pParms->u.UnInstall.pwszPnpId);
                            pParms->u.UnInstall.pwszPnpId = NULL;
                        }
                        /* ignore rc */ VBoxWinDrvInfQueryFirstPnPId(hInf,
                                                                     pParms->u.UnInstall.pwszModel, &pParms->u.UnInstall.pwszPnpId);
                    }
                    else
                        vboxWinDrvInstLogVerbose(pCtx, 1, "No first model found/set, skipping determining PnP ID");
                }

                RTUtf16Free(pwszMainSection);
            }
            else
            {
                vboxWinDrvInstLogError(pCtx, "INF file is invalid");
                rc = VERR_INVALID_PARAMETER;
            }

            VBoxWinDrvInfClose(hInf);
        }
    }
    /* No INF file given but either the model or the PnP ID? */
    else if (   pParms->u.UnInstall.pwszModel
             || pParms->u.UnInstall.pwszPnpId)
    {
        /* Nothing to do for us here. */
        rc = VINF_SUCCESS;
    }
    else
    {
        vboxWinDrvInstLogError(pCtx, "Neither INF file nor model/PnP ID given; can't continue");
        rc = VERR_INVALID_PARAMETER;
    }

    if (RT_SUCCESS(rc))
    {
        vboxWinDrvInstLogVerbose(pCtx, 1, "Determined parameters:");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\tINF File: %ls",
                                 pParms->pwszInfFile ? pParms->pwszInfFile : L"<None>");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\t   Model: %ls",
                                 pParms->u.UnInstall.pwszModel ? pParms->u.UnInstall.pwszModel : L"<None>");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\t  PnP ID: %ls",
                                 pParms->u.UnInstall.pwszPnpId ? pParms->u.UnInstall.pwszPnpId : L"<None>");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\t Section: %ls",
                                 pParms->u.UnInstall.pwszSection ? pParms->u.UnInstall.pwszSection : L"<None>");
    }

    return rc;
}

/**
 * Queries OEM INF files from the driver store.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 * @param   ppList              Where to return the list of found Windows driver store entries on success.
 *                              Needs to be destroyed with VBoxWinDrvStoreListFree().
 */
static int vboxWinDrvQueryFromDriverStore(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms,
                                          PVBOXWINDRVSTORELIST *ppList)
{
    int rc = vboxWinDrvParmsDetermine(pCtx, pParms, false /* fForce */);
    if (RT_SUCCESS(rc))
    {
        PVBOXWINDRVSTORELIST pList     = NULL;
        char                *pszNeedle = NULL;

        if (pParms->u.UnInstall.pwszPnpId)
        {
            rc = RTUtf16ToUtf8(pParms->u.UnInstall.pwszPnpId, &pszNeedle);
            if (RT_SUCCESS(rc))
                rc = VBoxWinDrvStoreQueryByPnpId(pCtx->pStore, pszNeedle, &pList);
        }
        else if (pParms->u.UnInstall.pwszModel)
        {
            rc = RTUtf16ToUtf8(pParms->u.UnInstall.pwszModel, &pszNeedle);
            if (RT_SUCCESS(rc))
                rc = VBoxWinDrvStoreQueryByModelName(pCtx->pStore, pszNeedle, &pList);
        }
        else if (pParms->pwszInfFile)
        {
            rc = VERR_NOT_IMPLEMENTED;
        }

        RTStrFree(pszNeedle);
        pszNeedle = NULL;

        if (RT_SUCCESS(rc))
        {
            *ppList = pList;
        }
        else
            VBoxWinDrvStoreListFree(pList);
    }

    return rc;
}

/**
 * Callback implementation for invoking a section for uninstallation.
 *
 * @returns VBox status code.
 * @param   hInf                Handle to INF file.
 * @param   pwszSection         Section to invoke.
 * @param   pvCtx               User-supplied pointer. Usually PVBOXWINDRVINSTINTERNAL.
 */
DECLCALLBACK(int) vboxWinDrvUninstallTryInfSectionCallback(HINF hInf, PCRTUTF16 pwszSection, void *pvCtx)
{
    PVBOXWINDRVINSTINTERNAL pCtx = (PVBOXWINDRVINSTINTERNAL)pvCtx;

    return vboxWinDrvUninstallInfSectionEx(pCtx, hInf, pwszSection);
}

/**
 * Removes OEM INF files from the driver store.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver uninstallation parameters to use.
 * @param   pList               Driver store list with OEM INF entries to remove.
 */
static int vboxWinDrvUninstallFromDriverStore(PVBOXWINDRVINSTINTERNAL pCtx,
                                              PVBOXWINDRVINSTPARMS pParms, PVBOXWINDRVSTORELIST pList)
{

    int rc = VINF_SUCCESS;

    const char *pszDrvStorePath = VBoxWinDrvStoreBackendGetLocation(pCtx->pStore);

    vboxWinDrvInstLogInfo(pCtx, "Uninstalling %zu matching entr%s", pList->cEntries, pList->cEntries == 1 ? "y" : "ies");
    PVBOXWINDRVSTOREENTRY pCur;
    RTListForEach(&pList->List, pCur, VBOXWINDRVSTOREENTRY, Node)
    {
        bool fRc = FALSE;

        /*
         * Running the uninstalling section(s) first before removing the driver from the driver store below.
         */
        RTUTF16 wszInfPathAbs[RTPATH_MAX];
        ssize_t const cwchInfPathAbs = RTUtf16Printf(wszInfPathAbs, RT_ELEMENTS(wszInfPathAbs),
                                                     "%s\\%ls", pszDrvStorePath, pCur->wszInfFile);
        AssertBreakStmt(cwchInfPathAbs > 0, rc = VERR_BUFFER_OVERFLOW);

        vboxWinDrvInstLogInfo(pCtx, "Uninstalling %ls (%ls)", pCur->wszModel, wszInfPathAbs);

        /* Only calltry calling the "DefaultUninstall" section here, as there aren't any other section(s)
         * to handle for uninstalling a driver via INF files. */
        /* rc ignored, keep going */ vboxWinDrvTryInfSection(pCtx, wszInfPathAbs, NULL /* DefaultUninstall */,
                                                             vboxWinDrvUninstallTryInfSectionCallback);

        /*
         * Remove the driver from the driver store.
         */
        if (g_pfnDiUninstallDriverW)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "Using DiUninstallDriverW()");

            BOOL fReboot = FALSE;
            if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
            {
                /* Takes the fully qualified path of the INF file to uninstall. */
                fRc = g_pfnDiUninstallDriverW(NULL /* hWndParent */, wszInfPathAbs, 0 /* Flags */, &fReboot);
            }
            else
                fRc = TRUE;
            if (fRc)
                pCtx->fReboot = RT_BOOL(fReboot);
            else
            {
                /* Not fatal, try next block. */
                DWORD const dwErr = GetLastError();
                vboxWinDrvInstLogVerbose(pCtx, 1, "DiUninstallDriverW() failed with %#x (%d)", dwErr, dwErr);
            }
        }

        /* Not (yet) successful? Try harder using an older API. */
        if (   !fRc
            && g_pfnSetupUninstallOEMInfW)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "Using SetupUninstallOEMInfW()");

            if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
            {
                DWORD dwUninstallFlags = 0;
                if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                    dwUninstallFlags |= SUOI_FORCEDELETE;

                /* Takes the oemXXX.inf file (without a path), as found in the Windows INF directory. */
                fRc = g_pfnSetupUninstallOEMInfW(pCur->wszInfFile, dwUninstallFlags, NULL /* pReserved */);
            }
        }

        int rc2 = VINF_SUCCESS;

        if (fRc)
            vboxWinDrvInstLogInfo(pCtx, "Uninstalling OEM INF \"%ls\" successful", wszInfPathAbs);
        else
        {
            DWORD const dwErr = GetLastError();
            if (dwErr == ERROR_INF_IN_USE_BY_DEVICES)
                vboxWinDrvInstLogError(pCtx, "Unable to uninstall OEM INF \"%ls\": Driver still in use by device", wszInfPathAbs);
            else
                rc2 = vboxWinDrvInstLogLastError(pCtx, "Uninstalling OEM INF \"%ls\" failed", wszInfPathAbs);
        }

        /* If anything failed above, try removing stuff ourselves as good as we can. */
        if (RT_FAILURE(rc2))
            /* rc ignored, keep going */ vboxWinDrvTryInfSection(pCtx, wszInfPathAbs, pCur->wszModel,
                                                                 vboxWinDrvUninstallTryInfSectionCallback);

        if (RT_SUCCESS(rc)) /* Keep first error if already set. */
            rc = rc2;

        /* Keep going. */
    }

    return rc;
}

/**
 * Performs the actual driver uninstallation.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvUninstallPerform(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    int rc;
    switch (pParms->enmMode)
    {
        case VBOXWINDRVINSTMODE_UNINSTALL:
        {
            PVBOXWINDRVSTORELIST pList = NULL;
            rc = vboxWinDrvQueryFromDriverStore(pCtx, pParms, &pList);
            if (RT_SUCCESS(rc))
            {
                rc = vboxWinDrvUninstallFromDriverStore(pCtx, pParms, pList);

                VBoxWinDrvStoreListFree(pList);
                pList = NULL;
            }
            break;
        }

        case VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION:
        {
            rc = vboxWinDrvInstallInfSection(pCtx, pParms);
            break;
        }

        default:
            rc = VINF_SUCCESS;
            break;
    }

    return rc;
}

/**
 * Main function to perform the actual driver [un]installation.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvInstMain(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    /* Note: Other parameters might be optional, depending on the mode. */
    AssertReturn(!(pParms->fFlags & ~VBOX_WIN_DRIVERINSTALL_F_VALID_MASK), VERR_INVALID_PARAMETER);

    bool const fInstall = pParms->enmMode == VBOXWINDRVINSTMODE_INSTALL
                       || pParms->enmMode == VBOXWINDRVINSTMODE_INSTALL_INFSECTION;

    if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN)
        vboxWinDrvInstLogWarn(pCtx, "Dry-run mode active -- no installation performed!");

    const char * const pszLogAction = fInstall ? "Installing" : "Uninstalling";
    if (pParms->pwszInfFile)
        vboxWinDrvInstLogInfo(pCtx, "%s driver \"%ls\" ... ", pszLogAction, pParms->pwszInfFile);
    else if (pParms->u.UnInstall.pwszModel)
        vboxWinDrvInstLogInfo(pCtx, "%s driver model \"%ls\" ... ", pszLogAction, pParms->u.UnInstall.pwszModel);
    else if (pParms->u.UnInstall.pwszPnpId)
        vboxWinDrvInstLogInfo(pCtx, "%s PnP ID \"%ls\" ... ", pszLogAction, pParms->u.UnInstall.pwszPnpId);

    if (   pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT
        && g_pfnSetupSetNonInteractiveMode)
    {
        vboxWinDrvInstLogInfo(pCtx, "Setting non-interactive mode ...");
        if (!(pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_DRYRUN))
            g_pfnSetupSetNonInteractiveMode(TRUE /* fEnable */);
    }

    if (!vboxWinDrvParmsAreValid(pCtx, pParms))
    {
        vboxWinDrvInstLogError(pCtx, "%s parameters are invalid, can't continue", fInstall ? "Installation" : "Uninstallation");
        return VERR_INVALID_PARAMETER;
    }

    int rc;
    if (fInstall)
        rc = vboxWinDrvInstallPerform(pCtx, pParms);
    else
        rc = vboxWinDrvUninstallPerform(pCtx, pParms);

    if (RT_SUCCESS(rc))
    {
        vboxWinDrvInstLogInfo(pCtx, "Driver was %sinstalled successfully", fInstall ? "" : "un");
        if (pCtx->fReboot)
        {
            vboxWinDrvInstLogRebootNeeded(pCtx, "A reboot is needed in order to complete the driver %sinstallation.",
                                          fInstall ? "" : "un");
            rc = VINF_REBOOT_NEEDED;
        }
    }

    /* Note: Call vboxWinDrvInstLogEx() to not increase the error/warn count here. */
    if (pCtx->cErrors)
        vboxWinDrvInstLogEx(pCtx, VBOXWINDRIVERLOGTYPE_ERROR, "%sstalling driver(s) failed with %u errors, %u warnings (rc=%Rrc)",
                            fInstall ? "In" : "Unin", pCtx->cErrors, pCtx->cWarnings, rc);
    else if (pCtx->cWarnings)
        vboxWinDrvInstLogEx(pCtx, VBOXWINDRIVERLOGTYPE_WARN, "%sstalling driver(s) succeeded with %u warnings",
                            fInstall ? "In" : "Unin", pCtx->cWarnings);

    return rc;
}

/**
 * Creates a Windows driver installer instance, extended version.
 *
 * @returns VBox status code.
 * @param   phDrvInst           where to return the created driver installer handle on success.
 * @param   uVerbosity          Sets the initial verbosity level.
 * @param   pfnLog              Log callback function to set.
 * @param   pvUser              User-supplied pointer to set. Optional and might be NULL.
 */
int VBoxWinDrvInstCreateEx(PVBOXWINDRVINST phDrvInst, unsigned uVerbosity, PFNVBOXWINDRIVERLOGMSG pfnLog, void *pvUser)
{
    int rc;

    PVBOXWINDRVINSTINTERNAL pCtx = (PVBOXWINDRVINSTINTERNAL)RTMemAllocZ(sizeof(VBOXWINDRVINSTINTERNAL));
    if (pCtx)
    {
        pCtx->u32Magic   = VBOXWINDRVINST_MAGIC;
        pCtx->uVerbosity = uVerbosity;
        pCtx->pfnLog     = pfnLog;
        pCtx->pvUser     = pvUser;

        /* 1. Detect the Windows version using API calls. */
        pCtx->uOsVer     = RTSystemGetNtVersion(); /* Might be overwritten later via VBoxWinDrvInstSetOsVersion(). */

        /* 2. Detect the Windows from the registry. */
        HKEY hKey;
        LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey);
        if (lrc == ERROR_SUCCESS)
        {
            DWORD dwMaj;
            rc = VBoxWinDrvRegQueryDWORD(hKey, "CurrentMajorVersionNumber", &dwMaj);
            if (RT_SUCCESS(rc))
            {
                DWORD dwMin;
                rc = VBoxWinDrvRegQueryDWORD(hKey, "CurrentMinorVersionNumber", &dwMin);
                if (RT_SUCCESS(rc))
                {
                    uint64_t const uRegOsVer = RTSYSTEM_MAKE_NT_VERSION(dwMaj, dwMin, 0 /* Build, ignored */);

                    /* When running this code in context of an MSI installer, the MSI engine might shim the Windows
                     * version which is being reported via API calls. So compare the both OS versions
                     * and prefer the one being reported via the registry if they don't match.
                     * Ignore the build number (too specific).
                     *
                     * The OS version to use still can be later tweaked using VBoxWinDrvInstSetOsVersion(). */
                    if (   (   RTSYSTEM_NT_VERSION_GET_MAJOR(pCtx->uOsVer) != RTSYSTEM_NT_VERSION_GET_MAJOR(uRegOsVer)
                            || RTSYSTEM_NT_VERSION_GET_MINOR(pCtx->uOsVer) != RTSYSTEM_NT_VERSION_GET_MINOR(uRegOsVer))
                        && RTSYSTEM_NT_VERSION_GET_MAJOR(uRegOsVer) > 4 /* Only XP+ */)
                    {
                        vboxWinDrvInstLogInfo(pCtx, "Detected Windows version (%u.%u) does not match the one stored in the registry (%u.%u)",
                                              RTSYSTEM_NT_VERSION_GET_MAJOR(pCtx->uOsVer),
                                              RTSYSTEM_NT_VERSION_GET_MINOR(pCtx->uOsVer),
                                              RTSYSTEM_NT_VERSION_GET_MAJOR(uRegOsVer),
                                              RTSYSTEM_NT_VERSION_GET_MINOR(uRegOsVer));
                        vboxWinDrvInstLogInfo(pCtx, "This might be due a compatibility layer or MSI installer engine shimming the Windows version");

                        /* Override the OS version from the API with the one found in the registry. */
                        VBoxWinDrvInstSetOsVersion(pCtx, uRegOsVer);
                    }
                }
            }

            RegCloseKey(hKey);
        }
        /* else not fatal. */

        vboxWinDrvInstLogInfo(pCtx, VBOX_PRODUCT " Version " VBOX_VERSION_STRING " - r%s", RTBldCfgRevisionStr());
        vboxWinDrvInstLogInfo(pCtx, "Using Windows version %d.%d.%d (%s)", RTSYSTEM_NT_VERSION_GET_MAJOR(pCtx->uOsVer),
                                                                           RTSYSTEM_NT_VERSION_GET_MINOR(pCtx->uOsVer),
                                                                           RTSYSTEM_NT_VERSION_GET_BUILD(pCtx->uOsVer),
                                                                           RTBldCfgTargetArch());

        rc = RTOnce(&g_vboxWinDrvInstResolveOnce, vboxWinDrvInstResolveOnce, pCtx);
        if (RT_SUCCESS(rc))
        {
            rc = VBoxWinDrvStoreCreate(&pCtx->pStore);
            if (RT_SUCCESS(rc))
            {
                *phDrvInst = (VBOXWINDRVINST)pCtx;
                return VINF_SUCCESS;
            }
            else
                vboxWinDrvInstLogError(pCtx, "Creating driver store failed with %Rrc", rc);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    VBoxWinDrvStoreDestroy(pCtx->pStore);
    VBoxWinDrvInstDestroy(pCtx);
    return rc;
}

/**
 * Creates a Windows driver installer instance.
 *
 * @returns VBox status code.
 * @param   phDrvInst           where to return the created driver installer handle on success.
 */
int VBoxWinDrvInstCreate(PVBOXWINDRVINST phDrvInst)
{
    return VBoxWinDrvInstCreateEx(phDrvInst, 0 /* uVerbosity */, NULL /* pfnLog */, NULL /* pvUser */);
}

/**
 * Destroys a Windows driver installer instance.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to destroy.
 *                              The handle will be invalid after calling this function.
 */
int VBoxWinDrvInstDestroy(VBOXWINDRVINST hDrvInst)
{
    if (hDrvInst == NIL_VBOXWINDRVINST)
        return VINF_SUCCESS;

    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    vboxWinDrvInstParmsDestroy(&pCtx->Parms);

    VBoxWinDrvStoreDestroy(pCtx->pStore);
    pCtx->pStore = NULL;

    RTMemFree(pCtx);

    return VINF_SUCCESS;
}

/**
 * Returns the number of (logged) warnings so far.
 *
 * @returns Number of (logged) warnings so far.
 * @param   hDrvInst            Windows driver installer handle.
 */
unsigned VBoxWinDrvInstGetWarnings(VBOXWINDRVINST hDrvInst)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_RC(pCtx, UINT8_MAX);

    return pCtx->cWarnings;
}

/**
 * Returns the number of (logged) errors so far.
 *
 * @returns Number of (logged) errors so far..
 * @param   hDrvInst            Windows driver installer handle.
 */
unsigned VBoxWinDrvInstGetErrors(VBOXWINDRVINST hDrvInst)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_RC(pCtx, UINT8_MAX);

    return pCtx->cErrors;
}

/**
 * Sets (overwrites) the current OS version used for the (un)installation code.
 *
 * @param   hDrvInst            Windows driver installer handle.
 * @param   uOsVer              OS version to set. RTSYSTEM_MAKE_NT_VERSION style.
 */
void VBoxWinDrvInstSetOsVersion(VBOXWINDRVINST hDrvInst, uint64_t uOsVer)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_VOID(pCtx);

    pCtx->uOsVer = uOsVer;

    vboxWinDrvInstLogInfo(pCtx, "Set OS version to: %u.%u", RTSYSTEM_NT_VERSION_GET_MAJOR(pCtx->uOsVer),
                                                            RTSYSTEM_NT_VERSION_GET_MINOR(pCtx->uOsVer));
}

/**
 * Sets the verbosity of a Windows driver installer instance.
 *
 * @returns The old verbosity level.
 * @param   hDrvInst            Windows driver installer handle to set verbosity for.
 * @param   uVerbosity          Verbosity level to set.
 */
unsigned VBoxWinDrvInstSetVerbosity(VBOXWINDRVINST hDrvInst, unsigned uVerbosity)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_RC(pCtx, UINT8_MAX);

    unsigned const uOldVerbosity = hDrvInst->uVerbosity;
    hDrvInst->uVerbosity = uVerbosity;
    return uOldVerbosity;
}

/**
 * Sets the log callback of a Windows driver installer instance.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to set log callback for.
 * @param   pfnLog              Log callback function to set.
 * @param   pvUser              User-supplied pointer to set. Optional and might be NULL.
 */
void VBoxWinDrvInstSetLogCallback(VBOXWINDRVINST hDrvInst, PFNVBOXWINDRIVERLOGMSG pfnLog, void *pvUser)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_VOID(pCtx);

    pCtx->pfnLog = pfnLog;
    pCtx->pvUser = pvUser;
}

/**
 * Installs a driver, extended version.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file to use.
 * @param   pszModel            Model name to use. Optional and can be NULL.
 * @param   pszPnpId            PnP ID to use. NT-style wildcards supported. Optional and can be NULL.
 * @param   fFlags              Installation flags (of type VBOX_WIN_DRIVERINSTALL_F_XXX) to use.
 */
int VBoxWinDrvInstInstallEx(VBOXWINDRVINST hDrvInst,
                            const char *pszInfFile, const char *pszModel, const char *pszPnpId, uint32_t fFlags)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    AssertPtrReturn(pszInfFile, VERR_INVALID_PARAMETER);

    vboxWinDrvInstParmsInit(&pCtx->Parms);

    /* Resolve the INF file's absolute path, as as Setup API functions tend to need this. */
    char szInfPathAbs[RTPATH_MAX];
    int rc = RTPathReal(pszInfFile, szInfPathAbs, sizeof(szInfPathAbs));
    if (RT_SUCCESS(rc))
    {
        rc = RTStrToUtf16(szInfPathAbs, &pCtx->Parms.pwszInfFile);
        if (RT_FAILURE(rc))
            vboxWinDrvInstLogError(pCtx, "Failed to build path for INF file \"%s\", rc=%Rrc", pszInfFile, rc);
    }
    else
        vboxWinDrvInstLogError(pCtx, "Determining real path for INF file \"%s\" failed, rc=%Rrc", pszInfFile, rc);

    if (RT_SUCCESS(rc) && pszModel) /* Model is optional. */
        rc = RTStrToUtf16(pszModel, &pCtx->Parms.u.UnInstall.pwszModel);
    if (RT_SUCCESS(rc) && pszPnpId) /* Ditto. */
        rc = RTStrToUtf16(pszPnpId, &pCtx->Parms.u.UnInstall.pwszPnpId);

    if (RT_SUCCESS(rc))
    {
        pCtx->Parms.enmMode = VBOXWINDRVINSTMODE_INSTALL;
        pCtx->Parms.fFlags  = fFlags;

        rc = vboxWinDrvInstMain(pCtx, &pCtx->Parms);
    }

    if (!(fFlags & VBOX_WIN_DRIVERINSTALL_F_NO_DESTROY))
        vboxWinDrvInstParmsDestroy(&pCtx->Parms);

    if (RT_FAILURE(rc))
        vboxWinDrvInstLogError(pCtx, "Driver installation failed with %Rrc", rc);

    return rc;
}

/**
 * Installs a driver.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file to use.
 * @param   fFlags              Installation flags (of type VBOX_WIN_DRIVERINSTALL_F_XXX) to use.
 *
 * @note    This function tries determining the model / PnP ID from the given INF file.
 *          To control the behavior exactly, use VBoxWinDrvInstInstallEx().
 */
int VBoxWinDrvInstInstall(VBOXWINDRVINST hDrvInst, const char *pszInfFile, uint32_t fFlags)
{
    return VBoxWinDrvInstInstallEx(hDrvInst, pszInfFile, NULL /* pszModel */, NULL /* pszPnpId */, fFlags);
}

/**
 * Uninstalls a driver.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file within driver store to uninstall.
 *                              Optional and can be NULL.
 * @param   pszModel            Model to uninstall (e.g. "VBoxUSB.AMD64"). NT-style wildcards supported.
 *                              Optional and can be NULL.
 * @param   pszPnpId            PnP ID to use (e.g. "USB\\VID_80EE&PID_CAFE"). NT-style wildcards supported.
 *                              Optional and can be NULL.
 * @param   fFlags              Installation flags (of type VBOX_WIN_DRIVERINSTALL_F_XXX) to use.
 */
int VBoxWinDrvInstUninstall(VBOXWINDRVINST hDrvInst, const char *pszInfFile, const char *pszModel, const char *pszPnpId,
                            uint32_t fFlags)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    int rc = VINF_SUCCESS;

    vboxWinDrvInstParmsInit(&pCtx->Parms);

    /* If given, get the sole INF file name (without path), to make it searchable within the driver store.
     * Note: This only will work with "oemXXX.inf" files for the (legcy) driver store. */
    if (pszInfFile && *pszInfFile)
    {
        char *pszInfFileName = RTPathFilename(pszInfFile);
        if (pszInfFileName)
            rc = RTStrToUtf16(pszInfFileName, &pCtx->Parms.pwszInfFile);
        else
            rc = VERR_FILE_NOT_FOUND;
    }

    if (RT_SUCCESS(rc) && pszModel && *pszModel)
        rc = RTStrToUtf16(pszModel, &pCtx->Parms.u.UnInstall.pwszModel);
    if (RT_SUCCESS(rc) && pszPnpId && *pszPnpId)
        rc = RTStrToUtf16(pszPnpId, &pCtx->Parms.u.UnInstall.pwszPnpId);

    pCtx->Parms.enmMode = VBOXWINDRVINSTMODE_UNINSTALL;
    pCtx->Parms.fFlags  = fFlags;

    if (RT_SUCCESS(rc))
        rc = vboxWinDrvInstMain(pCtx, &pCtx->Parms);

    if (!(fFlags & VBOX_WIN_DRIVERINSTALL_F_NO_DESTROY))
        vboxWinDrvInstParmsDestroy(&pCtx->Parms);

    if (RT_FAILURE(rc))
        vboxWinDrvInstLogError(pCtx, "Driver uninstallation failed with %Rrc", rc);

    return rc;
}

/**
 * Worker function for executing a section of an INF file.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   fInstall            Whether to execute the section to install or uninstall.
 * @param   pszInfFile          INF file to use.
 * @param   pszSection          Section within the INF file to execute.
 * @param   fFlags              Installation flags to use.
 */
int VBoxWinDrvInstExecuteInfWorker(VBOXWINDRVINST hDrvInst,
                                   bool fInstall, const char *pszInfFile, const char *pszSection, uint32_t fFlags)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    AssertPtrReturn(pszInfFile, VERR_INVALID_POINTER);

    vboxWinDrvInstParmsInit(&pCtx->Parms);

    int rc = RTStrToUtf16(pszInfFile, &pCtx->Parms.pwszInfFile);
    if (RT_SUCCESS(rc) && pszSection) /* pszSection is optional. */
        rc = RTStrToUtf16(pszSection, &pCtx->Parms.u.ExecuteInf.pwszSection);

    pCtx->Parms.enmMode = fInstall ? VBOXWINDRVINSTMODE_INSTALL_INFSECTION : VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION;
    pCtx->Parms.fFlags  = fFlags;

    rc = vboxWinDrvInstMain(pCtx, &pCtx->Parms);

    if (!(fFlags & VBOX_WIN_DRIVERINSTALL_F_NO_DESTROY))
        vboxWinDrvInstParmsDestroy(&pCtx->Parms);

    return rc;
}

/**
 * Executes a section of an INF file for installation.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file to use.
 * @param   pszSection          Section within the INF file to execute.
 * @param   fFlags              Installation flags to use.
 */
int VBoxWinDrvInstInstallExecuteInf(VBOXWINDRVINST hDrvInst, const char *pszInfFile, const char *pszSection, uint32_t fFlags)
{
    return VBoxWinDrvInstExecuteInfWorker(hDrvInst, true /* fInstall */, pszInfFile, pszSection, fFlags);
}

/**
 * Executes a section of an INF file for uninstallation.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file to use.
 * @param   pszSection          Section within the INF file to execute.
 * @param   fFlags              Installation flags to use.
 */
int VBoxWinDrvInstUninstallExecuteInf(VBOXWINDRVINST hDrvInst, const char *pszInfFile, const char *pszSection, uint32_t fFlags)
{
    return VBoxWinDrvInstExecuteInfWorker(hDrvInst, false /* fInstall */, pszInfFile, pszSection, fFlags);
}

/**
 * Controls a Windows service, internal version.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the given service was not found.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszService          Name of service to control.
 * @param   enmFn               Service control function to use.
 *                              VBOXWINDRVSVCFN_RESTART is not implemented and must be composed of
 *                              VBOXWINDRVSVCFN_START + VBOXWINDRVSVCFN_STOP by the caller.
 * @param   fFlags              Service control flags (of type VBOXWINDRVSVCFN_F_XXX) to use.
 * @param   msTimeout           Timeout (in ms) to use. Ignored if VBOXWINDRVSVCFN_F_WAIT is missing in \a fFlags.
 */
static int vboxWinDrvInstControlServiceEx(PVBOXWINDRVINSTINTERNAL pCtx,
                                          const char *pszService, VBOXWINDRVSVCFN enmFn, uint32_t fFlags, RTMSINTERVAL msTimeout)
{
    AssertPtrReturn(pszService, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~VBOXWINDRVSVCFN_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(enmFn > VBOXWINDRVSVCFN_INVALID && enmFn < VBOXWINDRVSVCFN_END, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & VBOXWINDRVSVCFN_F_WAIT) || msTimeout, VERR_INVALID_PARAMETER);

    PRTUTF16 pwszService;
    int rc = RTStrToUtf16(pszService, &pwszService);
    if (RT_FAILURE(rc))
        return rc;

    SC_HANDLE hSvc = NULL;
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM != NULL)
    {
        hSvc = OpenServiceW(hSCM, pwszService, SERVICE_ALL_ACCESS | SERVICE_QUERY_STATUS);
        if (hSvc == NULL)
        {
            rc = RTErrConvertFromWin32(GetLastError());
            if (rc != VERR_NOT_FOUND)
                rc = vboxWinDrvInstLogLastError(pCtx, "Opening service '%s' failed", pszService);
        }
    }
    else
        rc = vboxWinDrvInstLogLastError(pCtx, "Opening Service Control Manager (SCM) failed");

    if (RT_FAILURE(rc))
    {
        RTUtf16Free(pwszService);
        if (hSvc)
            CloseServiceHandle(hSvc);
        if (hSCM)
            CloseServiceHandle(hSCM);
        return rc;
    }

    SERVICE_STATUS_PROCESS enmSvcSts;

    switch (enmFn)
    {
        case VBOXWINDRVSVCFN_START:
        {
            if (!StartService(hSvc, 0, NULL))
            {
                if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
                    break;

                /** @todo Also handle disabled services here? */

                rc = vboxWinDrvInstLogLastError(pCtx, "Starting service '%s' failed", pszService);
            }
            else
                vboxWinDrvInstLogInfo(pCtx, "Starting service '%s' ...", pszService);
            break;
        }

        case VBOXWINDRVSVCFN_STOP:
        {
            if (!ControlService(hSvc, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&enmSvcSts))
            {
                DWORD const dwErr = GetLastError();

                /* A not active or disabled service is not an error, so just skip. */
                if (   dwErr == ERROR_SERVICE_DISABLED
                    || dwErr == ERROR_SERVICE_NOT_ACTIVE)
                    break;

                rc = vboxWinDrvInstLogLastError(pCtx, "Stopping service '%s' failed", pszService);
            }
            else
                vboxWinDrvInstLogInfo(pCtx, "Stopping service '%s' ...", pszService);
            break;
        }

        case VBOXWINDRVSVCFN_DELETE:
        {
            if (!DeleteService(hSvc))
                rc = vboxWinDrvInstLogLastError(pCtx, "Deleting service '%s' failed", pszService);
            else
            {
                vboxWinDrvInstLogInfo(pCtx, "Successfully deleted service '%s'", pszService);
                fFlags &= ~VBOXWINDRVSVCFN_F_WAIT; /* Drop the wait flag, makes no sense here. */
            }
            break;
        }

        default:
            AssertFailedStmt(rc = VERR_NOT_SUPPORTED);
            break;
    }

    if (RT_SUCCESS(rc))
    {
        if (fFlags & VBOXWINDRVSVCFN_F_WAIT)
        {
            uint64_t const msStartTS = RTTimeMilliTS();

            rc = VERR_NO_CHANGE; /* No change yet. */

            if (!g_pfnQueryServiceStatusEx)
            {
                vboxWinDrvInstLogWarn(pCtx, "Waiting for status change of service '%s' not supported on this OS, skipping",
                                      pszService);
                rc = VINF_SUCCESS;
            }
            else
            {
                vboxWinDrvInstLogInfo(pCtx, "Waiting for status change of service '%s' (%ums timeout) ...", pszService, msTimeout);
                for (;;)
                {
                    DWORD dwBytes;
                    if (!g_pfnQueryServiceStatusEx(hSvc,
                                                   SC_STATUS_PROCESS_INFO,
                                                   (LPBYTE)&enmSvcSts,
                                                   sizeof(SERVICE_STATUS_PROCESS),
                                                   &dwBytes))
                    {
                        rc = vboxWinDrvInstLogLastError(pCtx, "Failed to query service status");
                        break;
                    }

                    if ((RTTimeMilliTS() - msStartTS) % RT_MS_1SEC == 0) /* Don't spam. */
                        vboxWinDrvInstLogVerbose(pCtx, 3, "Service '%s' status is %#x: %u",
                                                 pszService, enmSvcSts.dwCurrentState, (RTTimeMilliTS() - msStartTS) % 100 == 0);

                    switch (enmSvcSts.dwCurrentState)
                    {
                        case SERVICE_STOP_PENDING:
                        case SERVICE_START_PENDING:
                            RTThreadSleep(100); /* Wait a bit before retrying. */
                            break;

                        case SERVICE_RUNNING:
                        {
                            if (enmFn == VBOXWINDRVSVCFN_START)
                                rc = VINF_SUCCESS;
                            break;
                        }

                        case SERVICE_STOPPED:
                        {
                            if (enmFn == VBOXWINDRVSVCFN_START)
                            {
                                vboxWinDrvInstLogError(pCtx, "Service '%s' stopped unexpectedly", pszService);
                                rc = VERR_INVALID_STATE;
                            }
                            else
                                rc = VINF_SUCCESS;
                            break;
                        }

                        default:
                        {
                            vboxWinDrvInstLogError(pCtx, "Service '%s' reported an unexpected state (%#x)",
                                                   pszService, enmSvcSts.dwCurrentState);
                            rc = VERR_INVALID_STATE;
                        }
                    }

                    if (   RT_FAILURE(rc)
                        && rc != VERR_NO_CHANGE)
                        break;

                    if (RT_SUCCESS(rc))
                        break;

                    if (RTTimeMilliTS() - msStartTS >= msTimeout)
                    {
                        vboxWinDrvInstLogError(pCtx, "Waiting for service '%s' timed out (%ums)", pszService, msTimeout);
                        rc = VERR_TIMEOUT;
                        break;
                    }
                }
            }

            if (RT_SUCCESS(rc))
                vboxWinDrvInstLogInfo(pCtx, "Service '%s' successfully %s",
                                      pszService, enmFn == VBOXWINDRVSVCFN_START ? "started" : "stopped");
        }
        else
            vboxWinDrvInstLogVerbose(pCtx, 1, "Service '%s' was %s asynchronously",
                                     pszService, enmFn == VBOXWINDRVSVCFN_START ? "started" : "stopped");
    }

    RTUtf16Free(pwszService);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return rc;
}

/**
 * Controls a Windows service, extended version.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszService          Name of service to control.
 * @param   enmFn               Service control function to use.
 * @param   fFlags              Service control flags (of type VBOXWINDRVSVCFN_F_XXX) to use.
 * @param   msTimeout           Timeout (in ms) to use. Only being used if VBOXWINDRVSVCFN_F_WAIT is specified in \a fFlags.
 */
int VBoxWinDrvInstControlServiceEx(VBOXWINDRVINST hDrvInst,
                                    const char *pszService, VBOXWINDRVSVCFN enmFn, uint32_t fFlags, RTMSINTERVAL msTimeout)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

#define CONTROL_SERVICE(a_Fn) \
    vboxWinDrvInstControlServiceEx(pCtx, pszService, a_Fn, fFlags, msTimeout);

    int rc;
    if (enmFn == VBOXWINDRVSVCFN_RESTART)
    {
        rc = CONTROL_SERVICE(VBOXWINDRVSVCFN_STOP);
        if (RT_SUCCESS(rc))
            rc = CONTROL_SERVICE(VBOXWINDRVSVCFN_START);
    }
    else
        rc = CONTROL_SERVICE(enmFn);

#undef CONTROL_SERVICE
    return rc;
}

/**
 * Controls a Windows service.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszService          Name of service to control.
 * @param   enmFn               Service control function to use.
 *
 * @note    Function waits 30s for the service to reach the desired control function.
 *          Use VBooxWinDrvInstControlServiceEx() for more flexibility.
 */
int VBoxWinDrvInstControlService(VBOXWINDRVINST hDrvInst, const char *pszService, VBOXWINDRVSVCFN enmFn)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    return VBoxWinDrvInstControlServiceEx(pCtx, pszService, enmFn, VBOXWINDRVSVCFN_F_WAIT, RT_MS_30SEC);
}

#ifdef TESTCASE
/**
 * Returns the internal parameters of an (un)installation.
 *
 * @returns Internal parameters of an (un)installation.
 * @param   hDrvInst            Windows driver installer handle to use.
 */
PVBOXWINDRVINSTPARMS VBoxWinDrvInstTestGetParms(VBOXWINDRVINST hDrvInst)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN_RC((hDrvInst), NULL);

    return &pCtx->Parms;
}

/**
 * Detroys internal parameters of an (un)installation.
 *
 * @param   pParms              Internal parameters of an (un)installation to destroy.
 */
void VBoxWinDrvInstTestParmsDestroy(PVBOXWINDRVINSTPARMS pParms)
{
    vboxWinDrvInstParmsDestroy(pParms);
}
#endif /* TESTCASE */
