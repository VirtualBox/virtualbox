/* $Id: VBoxWinDrvInst.cpp 107167 2024-11-26 09:06:00Z andreas.loeffler@oracle.com $ */
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

#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/cdefs.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#include <package-generated.h>
#include "product-generated.h"

#include <VBox/version.h>

#include <VBox/GuestHost/VBoxWinDrvDefs.h>
#include <VBox/GuestHost/VBoxWinDrvInst.h>
#include <VBox/GuestHost/VBoxWinDrvStore.h>

#include "VBoxWinDrvCommon.h"


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
#define VBOXWINDRVINST_VALID_RETURN_VOID(hFTPServer) \
    do { \
        AssertPtrReturnVoid(hFTPServer); \
        AssertReturnVoid((hFTPServer)->u32Magic == VBOXWINDRVINST_MAGIC); \
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


/**
 * Enumeration specifying the driver (un)installation mode.
 */
typedef enum VBOXWINDRVINSTMODE
{
    /** Invalid mode; do not use. */
    VBOXWINDRVINSTMODE_INVALID = 0,
    /** Install a driver. */
    VBOXWINDRVINSTMODE_INSTALL,
    /** Install by executing an INF section. */
    VBOXWINDRVINSTMODE_INSTALL_INFSECTION,
    /** Uninstall a driver. */
    VBOXWINDRVINSTMODE_UNINSTALL,
    /** Uninstall by executing an INF section. */
    VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION
} VBOXWINDRVINSTMODE;

/**
 * Structure for keeping driver (un)installation parameters.
 */
typedef struct VBOXWINDRVINSTPARMS
{
    /** Installation mode. */
    VBOXWINDRVINSTMODE enmMode;
    /** Installation flags of type VBOX_WIN_DRIVERINSTALL_F_XXX. */
    uint32_t           fFlags;
    /** INF file to use for (un)installation. */
    PRTUTF16           pwszInfFile;
    /** Indicator whether (un)installation parameters were determined from the INF file or not. */
    bool               fDeterminedFromInfFile;
    /** Union keeping specific parameters, depending on \a enmMode. */
    union
    {
        struct
        {
            /** Model including decoration (e.g. "VBoxUSB.NTAMD64"); optional and might be NULL. */
            PRTUTF16   pwszModel;
            /** Hardware (Pnp) ID; optional and might be NULL. */
            PRTUTF16   pwszPnpId;
        } UnInstall;
        struct
        {
            /** Section within in the INF file to execute. */
            PRTUTF16   pwszSection;
        } ExecuteInf;
    } u;
} VBOXWINDRVINSTPARMS;
/** Pointer to driver installation parameters. */
typedef VBOXWINDRVINSTPARMS *PVBOXWINDRVINSTPARMS;

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
 * Returns a Setup API error as a string.
 *
 * Needded to get at least a minimally meaningful error string back from Setup API.
 *
 * @returns Setup API error as a string, or NULL if not found.
 * @param   dwErr               Error code to return as a string.
 */
DECLINLINE(const char *) vboxWinDrvSetupApiErrToStr(const DWORD dwErr)
{
    switch (dwErr)
    {
        RT_CASE_RET_STR(ERROR_EXPECTED_SECTION_NAME             );
        RT_CASE_RET_STR(ERROR_BAD_SECTION_NAME_LINE             );
        RT_CASE_RET_STR(ERROR_SECTION_NAME_TOO_LONG             );
        RT_CASE_RET_STR(ERROR_GENERAL_SYNTAX                    );
        RT_CASE_RET_STR(ERROR_WRONG_INF_STYLE                   );
        RT_CASE_RET_STR(ERROR_SECTION_NOT_FOUND                 );
        RT_CASE_RET_STR(ERROR_LINE_NOT_FOUND                    );
        RT_CASE_RET_STR(ERROR_NO_BACKUP                         );
        RT_CASE_RET_STR(ERROR_NO_ASSOCIATED_CLASS               );
        RT_CASE_RET_STR(ERROR_CLASS_MISMATCH                    );
        RT_CASE_RET_STR(ERROR_DUPLICATE_FOUND                   );
        RT_CASE_RET_STR(ERROR_NO_DRIVER_SELECTED                );
        RT_CASE_RET_STR(ERROR_KEY_DOES_NOT_EXIST                );
        RT_CASE_RET_STR(ERROR_INVALID_DEVINST_NAME              );
        RT_CASE_RET_STR(ERROR_INVALID_CLASS                     );
        RT_CASE_RET_STR(ERROR_DEVINST_ALREADY_EXISTS            );
        RT_CASE_RET_STR(ERROR_DEVINFO_NOT_REGISTERED            );
        RT_CASE_RET_STR(ERROR_INVALID_REG_PROPERTY              );
        RT_CASE_RET_STR(ERROR_NO_INF                            );
        RT_CASE_RET_STR(ERROR_NO_SUCH_DEVINST                   );
        RT_CASE_RET_STR(ERROR_CANT_LOAD_CLASS_ICON              );
        RT_CASE_RET_STR(ERROR_INVALID_CLASS_INSTALLER           );
        RT_CASE_RET_STR(ERROR_DI_DO_DEFAULT                     );
        RT_CASE_RET_STR(ERROR_DI_NOFILECOPY                     );
        RT_CASE_RET_STR(ERROR_INVALID_HWPROFILE                 );
        RT_CASE_RET_STR(ERROR_NO_DEVICE_SELECTED                );
        RT_CASE_RET_STR(ERROR_DEVINFO_LIST_LOCKED               );
        RT_CASE_RET_STR(ERROR_DEVINFO_DATA_LOCKED               );
        RT_CASE_RET_STR(ERROR_DI_BAD_PATH                       );
        RT_CASE_RET_STR(ERROR_NO_CLASSINSTALL_PARAMS            );
        RT_CASE_RET_STR(ERROR_FILEQUEUE_LOCKED                  );
        RT_CASE_RET_STR(ERROR_BAD_SERVICE_INSTALLSECT           );
        RT_CASE_RET_STR(ERROR_NO_CLASS_DRIVER_LIST              );
        RT_CASE_RET_STR(ERROR_NO_ASSOCIATED_SERVICE             );
        RT_CASE_RET_STR(ERROR_NO_DEFAULT_DEVICE_INTERFACE       );
        RT_CASE_RET_STR(ERROR_DEVICE_INTERFACE_ACTIVE           );
        RT_CASE_RET_STR(ERROR_DEVICE_INTERFACE_REMOVED          );
        RT_CASE_RET_STR(ERROR_BAD_INTERFACE_INSTALLSECT         );
        RT_CASE_RET_STR(ERROR_NO_SUCH_INTERFACE_CLASS           );
        RT_CASE_RET_STR(ERROR_INVALID_REFERENCE_STRING          );
        RT_CASE_RET_STR(ERROR_INVALID_MACHINENAME               );
        RT_CASE_RET_STR(ERROR_REMOTE_COMM_FAILURE               );
        RT_CASE_RET_STR(ERROR_MACHINE_UNAVAILABLE               );
        RT_CASE_RET_STR(ERROR_NO_CONFIGMGR_SERVICES             );
        RT_CASE_RET_STR(ERROR_INVALID_PROPPAGE_PROVIDER         );
        RT_CASE_RET_STR(ERROR_NO_SUCH_DEVICE_INTERFACE          );
        RT_CASE_RET_STR(ERROR_DI_POSTPROCESSING_REQUIRED        );
        RT_CASE_RET_STR(ERROR_INVALID_COINSTALLER               );
        RT_CASE_RET_STR(ERROR_NO_COMPAT_DRIVERS                 );
        RT_CASE_RET_STR(ERROR_NO_DEVICE_ICON                    );
        RT_CASE_RET_STR(ERROR_INVALID_INF_LOGCONFIG             );
        RT_CASE_RET_STR(ERROR_DI_DONT_INSTALL                   );
        RT_CASE_RET_STR(ERROR_INVALID_FILTER_DRIVER             );
        RT_CASE_RET_STR(ERROR_NON_WINDOWS_NT_DRIVER             );
        RT_CASE_RET_STR(ERROR_NON_WINDOWS_DRIVER                );
        RT_CASE_RET_STR(ERROR_NO_CATALOG_FOR_OEM_INF            );
        RT_CASE_RET_STR(ERROR_DEVINSTALL_QUEUE_NONNATIVE        );
        RT_CASE_RET_STR(ERROR_NOT_DISABLEABLE                   );
        RT_CASE_RET_STR(ERROR_CANT_REMOVE_DEVINST               );
        RT_CASE_RET_STR(ERROR_INVALID_TARGET                    );
        RT_CASE_RET_STR(ERROR_DRIVER_NONNATIVE                  );
        RT_CASE_RET_STR(ERROR_IN_WOW64                          );
        RT_CASE_RET_STR(ERROR_SET_SYSTEM_RESTORE_POINT          );
        RT_CASE_RET_STR(ERROR_SCE_DISABLED                      );
        RT_CASE_RET_STR(ERROR_UNKNOWN_EXCEPTION                 );
        RT_CASE_RET_STR(ERROR_PNP_REGISTRY_ERROR                );
        RT_CASE_RET_STR(ERROR_REMOTE_REQUEST_UNSUPPORTED        );
        RT_CASE_RET_STR(ERROR_NOT_AN_INSTALLED_OEM_INF          );
        RT_CASE_RET_STR(ERROR_INF_IN_USE_BY_DEVICES             );
        RT_CASE_RET_STR(ERROR_DI_FUNCTION_OBSOLETE              );
        RT_CASE_RET_STR(ERROR_NO_AUTHENTICODE_CATALOG           );
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_DISALLOWED           );
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_TRUSTED_PUBLISHER    );
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_TRUST_NOT_ESTABLISHED);
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_PUBLISHER_NOT_TRUSTED);
        RT_CASE_RET_STR(ERROR_SIGNATURE_OSATTRIBUTE_MISMATCH    );
        RT_CASE_RET_STR(ERROR_ONLY_VALIDATE_VIA_AUTHENTICODE    );
        RT_CASE_RET_STR(ERROR_DEVICE_INSTALLER_NOT_READY        );
        RT_CASE_RET_STR(ERROR_DRIVER_STORE_ADD_FAILED           );
        RT_CASE_RET_STR(ERROR_DEVICE_INSTALL_BLOCKED            );
        RT_CASE_RET_STR(ERROR_DRIVER_INSTALL_BLOCKED            );
        RT_CASE_RET_STR(ERROR_WRONG_INF_TYPE                    );
        RT_CASE_RET_STR(ERROR_FILE_HASH_NOT_IN_CATALOG          );
        RT_CASE_RET_STR(ERROR_DRIVER_STORE_DELETE_FAILED        );
        RT_CASE_RET_STR(ERROR_NOT_INSTALLED                     );
        default:
            break;
    }

    return NULL;
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
    const char *pszErr = vboxWinDrvSetupApiErrToStr(dwErr);
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
    if (RT_SUCCESS(rc))
    {
        vboxWinDrvInstLogVerbose(pCtx, 4, "Resolving symbol \"%s\"  ...", pszSymbol, rc);
    }
    else
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
            RTUtf16Free(pParms->u.UnInstall.pwszPnpId);
            pParms->u.UnInstall.pwszPnpId = NULL;
            break;
        }

        case VBOXWINDRVINSTMODE_INSTALL_INFSECTION:
        case VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION:
        {
            RTUtf16Free(pParms->u.ExecuteInf.pwszSection);
            pParms->u.ExecuteInf.pwszSection = NULL;
            break;
        }

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
    /** Weak pointer to INF file being handled. */
    PRTUTF16                pwszInfFile;
    /** Weak pointer to INF section being handled. */
    PRTUTF16                pwszSection;
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
 * Uninstalls a section of a given INF file.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pwszInfFile         INF file to execute.
 * @param   pwszSection         Section within INF file to uninstall.
 *                              Can have a platform decoration (e.g. "Foobar.NTx86").
 */
static int vboxWinDrvUninstallInfSectionEx(PVBOXWINDRVINSTINTERNAL pCtx, PRTUTF16 pwszInfFile, PRTUTF16 pwszSection)
{
    AssertPtrReturn(pwszInfFile, VERR_INVALID_POINTER);
    AssertPtrReturn(pwszSection, VERR_INVALID_POINTER);

    vboxWinDrvInstLogInfo(pCtx, "Uninstalling INF section \"%ls\" ...", pwszSection);

    HINF hInf;
    int rc = VBoxWinDrvInfOpen(pwszInfFile, &hInf);
    if (RT_FAILURE(rc))
    {
        vboxWinDrvInstLogError(pCtx, "Unable to open INF file: %Rrc\n", rc);
        return rc;
    }

    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" successfully opened", pwszInfFile);

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
                vboxWinDrvInstLogVerbose(pCtx, 1, "INF section \"%ls\" not found, skpping", wszSection);
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

    VBoxWinDrvInfClose(hInf);

    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" closed", pwszInfFile);

    return rc;
}

/**
 * Installs a section of a given INF file.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pwszInfFile         INF file to execute.
 * @param   pwszSection         Section within INF file to install.
 *                              Can have a platform decoration (e.g. "Foobar.NTx86").
 */
static int vboxWinDrvInstallInfSectionEx(PVBOXWINDRVINSTINTERNAL pCtx, PRTUTF16 pwszInfFile, PRTUTF16 pwszSection)
{
    AssertPtrReturn(pwszInfFile, VERR_INVALID_POINTER);
    AssertPtrReturn(pwszSection, VERR_INVALID_POINTER);

    vboxWinDrvInstLogInfo(pCtx, "Installing INF section \"%ls\" ...", pwszSection);

    HINF hInf;
    int rc = VBoxWinDrvInfOpen(pwszInfFile, &hInf);
    if (RT_FAILURE(rc))
    {
        vboxWinDrvInstLogError(pCtx, "Unable to open INF file: %Rrc\n", rc);
        return rc;
    }

    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" successfully opened", pwszInfFile);

    VBOXDRVINSTINFCALLBACKCTX CallbackCtx;
    RT_ZERO(CallbackCtx);
    CallbackCtx.pThis = pCtx;
    CallbackCtx.pwszInfFile = pwszInfFile;
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
        rc = vboxWinDrvInstLogLastError(pCtx, "Installing INF section \"%ls\" failed", pwszSection);

    /*
     * Install services (if any).
     */
    RTUTF16 wszSection[64];
    ssize_t const cwchSection = RTUtf16Printf(wszSection, RT_ELEMENTS(wszSection),
                                              "%ls%s", pwszSection, ".Services");
    if (cwchSection > 0)
    {
        /* We always want to be the first service tag in the group order list (if any). */
        DWORD const fFlags = SPSVCINST_TAGTOFRONT;
        fRc = SetupInstallServicesFromInfSectionW(hInf, wszSection, fFlags);
        if (!fRc)
        {
            DWORD const dwErr = GetLastError();
            if (dwErr == ERROR_SECTION_NOT_FOUND)
                vboxWinDrvInstLogVerbose(pCtx, 1, "INF section \"%ls\" not found, skipping", wszSection);
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
                    vboxWinDrvInstLogError(pCtx,
                                           "Hint: An old service (SCM) entry might be dangling around.\n"
                                           "Try removing it via 'sc delete <service name>' and try again.");
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

    VBoxWinDrvInfClose(hInf);

    vboxWinDrvInstLogVerbose(pCtx, 1, "INF file \"%ls\" closed", pwszInfFile);

    return rc;
}

/**
 * Installs a section of a given INF file.
 *
 * Only supported for the VBOXWINDRVINSTMODE_INSTALL_EXECINF + VBOXWINDRVINSTMODE_UNINSTALL_EXECINF modes.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvInstallInfSection(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    AssertReturn(   pParms->enmMode == VBOXWINDRVINSTMODE_INSTALL_INFSECTION
                 || pParms->enmMode == VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION, VERR_INVALID_PARAMETER);

    return vboxWinDrvInstallInfSectionEx(pCtx, pParms->pwszInfFile, pParms->u.ExecuteInf.pwszSection);
}

/**
 * Performs the actual driver installation.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to use.
 */
static int vboxWinDrvInstallPerform(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms)
{
    int rc = VINF_SUCCESS;

    switch (pParms->enmMode)
    {
        case VBOXWINDRVINSTMODE_INSTALL:
        {
            BOOL fRc = FALSE;
            BOOL fReboot = FALSE;

            if (pParms->u.UnInstall.pwszPnpId)
                vboxWinDrvInstLogInfo(pCtx, "Using hardware ID \"%ls\"", pParms->u.UnInstall.pwszPnpId);

            uint64_t const uNtVer = RTSystemGetNtVersion();

            /*
             * Pre-install driver.
             */
            DWORD dwInstallFlags = 0;
            if (uNtVer >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0)) /* for Vista / 2008 Server and up. */
            {
                if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                    dwInstallFlags |= DIIRFLAG_FORCE_INF;

                vboxWinDrvInstLogVerbose(pCtx, 1, "Using g_pfnDiInstallDriverW(), dwInstallFlags=%#x", dwInstallFlags);

                fRc = g_pfnDiInstallDriverW(NULL /* hWndParent */, pParms->pwszInfFile, dwInstallFlags, &fReboot);
                if (fRc)
                {
                    rc = vboxWinDrvParmsDetermine(pCtx, pParms, true /* fForce */);
                    if (RT_SUCCESS(rc))
                    {
                        if (pParms->u.UnInstall.pwszModel)
                            rc = vboxWinDrvInstallInfSectionEx(pCtx, pCtx->Parms.pwszInfFile,
                                                               pParms->u.UnInstall.pwszModel);
                    }
                }
                else
                    rc = vboxWinDrvInstLogLastError(pCtx, "DiInstallDriverW() failed");
            }
            else /* For Windows 2000 and below. */
            {
                /* If no PnP ID (Hardware ID) is explicitly given, try guessing the PnP ID from the given INF file. */
                size_t cchPnpId = RTUtf16Len(pParms->u.UnInstall.pwszPnpId);
                if (   !cchPnpId
                    || cchPnpId > MAX_DEVICE_ID_LEN)
                {
                    vboxWinDrvInstLogInfo(pCtx, "PnP ID not specified explicitly or invalid, determining from given INF file");
                    rc = vboxWinDrvParmsDetermine(pCtx, pParms, true /* fForce */);
                    if (RT_SUCCESS(rc))
                        cchPnpId = RTUtf16Len(pParms->u.UnInstall.pwszPnpId);
                }

                if (RT_FAILURE(rc))
                    break;

                /* Still not having any PnP ID? */
                if (cchPnpId == 0)
                {
                    vboxWinDrvInstLogError(pCtx, "No (valid) PnP ID found; can't continue");
                    rc = VERR_NOT_FOUND;
                    break;
                }
                else
                {
                    if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT)
                    {
                        /* Using INSTALLFLAG_NONINTERACTIVE will trigger an invalid parameter error on Windows 2000. */
                        if (uNtVer >= RTSYSTEM_MAKE_NT_VERSION(5, 1, 0))
                            dwInstallFlags |= INSTALLFLAG_NONINTERACTIVE;
                        else
                            vboxWinDrvInstLogWarn(pCtx, "This version of Windows does not support silent installs");
                    }

                    if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                        dwInstallFlags |= INSTALLFLAG_FORCE;

                    vboxWinDrvInstLogVerbose(pCtx, 4, "Using g_pfnUpdateDriverForPlugAndPlayDevicesW(), pwszPnpId=%ls, pwszInfFile=%ls, dwInstallFlags=%#x",
                                             pParms->u.UnInstall.pwszPnpId, pParms->pwszInfFile, dwInstallFlags);

                    fRc = g_pfnUpdateDriverForPlugAndPlayDevicesW(NULL /* hWndParent */,
                                                                  pParms->u.UnInstall.pwszPnpId,
                                                                  pParms->pwszInfFile, dwInstallFlags, &fReboot);
                    if (!fRc)
                    {
                        DWORD const dwErr = GetLastError();
                        switch (dwErr)
                        {
                            case ERROR_NO_SUCH_DEVINST:
                            {
                                vboxWinDrvInstLogInfo(pCtx, "Device (\"%ls\") not found (yet), pre-installing driver ...",
                                                      pParms->u.UnInstall.pwszPnpId);

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
                                        fRc = g_pfnSetupCopyOEMInfW(wszInfFileAbs, wszSrcPathAbs, SPOST_PATH, 0,
                                                                    wszDstPathAbs, RT_ELEMENTS(wszDstPathAbs), NULL, NULL);

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
                                break;
                            }

                            case ERROR_NO_DRIVER_SELECTED:
                            {
                                rc = vboxWinDrvParmsDetermine(pCtx, pParms, true /* fForce */);
                                if (RT_SUCCESS(rc))
                                {
                                    if (pParms->u.UnInstall.pwszModel)
                                        rc = vboxWinDrvInstallInfSectionEx(pCtx, pCtx->Parms.pwszInfFile,
                                                                           pParms->u.UnInstall.pwszModel);
                                }
                                break;
                            }

                            default:
                                rc = vboxWinDrvInstLogLastError(pCtx, "Installation(UpdateDriverForPlugAndPlayDevicesW) failed");
                                break;
                        }
                    }
                }
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
 * Determines (un)installation parameters from a given set of parameters, logged.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if no valid parameters could be determined.
 * @param   pCtx                Windows driver installer context.
 * @param   pParms              Windows driver installation parameters to determine for.
 * @param   fForce              Whether to overwrite already set parameters or not.
 */
static int vboxWinDrvParmsDetermine(PVBOXWINDRVINSTINTERNAL pCtx, PVBOXWINDRVINSTPARMS pParms, bool fForce)
{
    int rc;
    /* INF file given? */
    if (    pParms->pwszInfFile
        && !pParms->fDeterminedFromInfFile)
    {
        HINF hInf;
        rc = VBoxWinDrvInfOpen(pParms->pwszInfFile, &hInf);
        if (RT_SUCCESS(rc))
        {
            /* Note: Model must come first for getting the PnP ID. */
            if (!pParms->u.UnInstall.pwszModel || fForce)
            {
                vboxWinDrvInstLogVerbose(pCtx, 1, "Determining first model ...");
                VBoxWinDrvInfQueryFirstModel(hInf, &pParms->u.UnInstall.pwszModel);
            }
            if (!pParms->u.UnInstall.pwszPnpId  || fForce)
            {
                vboxWinDrvInstLogVerbose(pCtx, 1, "Determining first PnP ID ...");
                VBoxWinDrvInfQueryFirstPnPId(hInf,
                                             pParms->u.UnInstall.pwszModel, &pParms->u.UnInstall.pwszPnpId);
            }
            VBoxWinDrvInfClose(hInf);
        }

        pParms->fDeterminedFromInfFile = true;
    }
    /* No INF file given but either the model or the PnP ID? */
    else if (   pParms->u.UnInstall.pwszModel
             || pParms->u.UnInstall.pwszPnpId)
    {
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    if (RT_SUCCESS(rc))
    {
        vboxWinDrvInstLogVerbose(pCtx, 1, "Determined parameters:");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\tINF File: %ls",
                                 pParms->pwszInfFile ? pParms->pwszInfFile : L"<None>");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\t   Model: %ls",
                                 pParms->u.UnInstall.pwszModel ? pParms->u.UnInstall.pwszModel : L"<None>");
        vboxWinDrvInstLogVerbose(pCtx, 1, "\t  PnP ID: %ls",
                                 pParms->u.UnInstall.pwszPnpId ? pParms->u.UnInstall.pwszPnpId : L"<None>");
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

        vboxWinDrvInstLogInfo(pCtx, "Uninstalling OEM INF \"%ls\" ...", wszInfPathAbs);

        int rc2 = vboxWinDrvUninstallInfSectionEx(pCtx, wszInfPathAbs,
                                                  L"DefaultUninstall" VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR);
        if (RT_FAILURE(rc2))
            vboxWinDrvInstLogError(pCtx, "Uninstalling INF section failed with %Rrc", rc2);

        /*
         * Remove the driver from the driver store.
         */
        if (g_pfnDiUninstallDriverW)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "Using DiUninstallDriverW()");
            BOOL fReboot = FALSE;
            fRc = g_pfnDiUninstallDriverW(NULL /* hWndParent */, pCur->wszInfFile, 0 /* Flags */, &fReboot);
            if (fRc)
                pCtx->fReboot = RT_BOOL(fReboot);
        }

        /* Not (yet) successful? Try harder using an older API. */
        if (   !fRc
            && g_pfnSetupUninstallOEMInfW)
        {
            vboxWinDrvInstLogVerbose(pCtx, 1, "Using SetupUninstallOEMInfW()");

            DWORD dwUninstallFlags = 0;
            if (pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_FORCE)
                dwUninstallFlags |= SUOI_FORCEDELETE;

            fRc = g_pfnSetupUninstallOEMInfW(pCur->wszInfFile, dwUninstallFlags, NULL /* pReserved */);
        }

        if (fRc)
            vboxWinDrvInstLogInfo(pCtx, "Uninstalling OEM INF \"%ls\" successful", wszInfPathAbs);
        else
            rc = vboxWinDrvInstLogLastError(pCtx, "Uninstalling OEM INF \"%ls\" failed", wszInfPathAbs);
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
    int rc = VINF_SUCCESS;

    uint64_t const uNtVer = RTSystemGetNtVersion();

    switch (pParms->enmMode)
    {
        case VBOXWINDRVINSTMODE_UNINSTALL:
        {
            PVBOXWINDRVSTORELIST pList = NULL;
            rc = vboxWinDrvQueryFromDriverStore(pCtx, pParms, &pList);
            if (RT_SUCCESS(rc))
            {
                rc = vboxWinDrvUninstallFromDriverStore(pCtx, pParms, pList);
                if (RT_SUCCESS(rc))
                {

                }

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

    bool const fInstall =    pParms->enmMode == (VBOXWINDRVINSTMODE_INSTALL)
                          || pParms->enmMode == (VBOXWINDRVINSTMODE_INSTALL_INFSECTION);

    uint64_t const uNtVer   = RTSystemGetNtVersion();

    if (pParms->pwszInfFile)
        vboxWinDrvInstLogInfo(pCtx, "%s driver \"%ls\" ... ", fInstall ? "Installing" : "Uninstalling", pParms->pwszInfFile);
    else if (pParms->u.UnInstall.pwszModel)
        vboxWinDrvInstLogInfo(pCtx, "%s driver model \"%ls\" ... ", fInstall ? "Installing" : "Uninstalling",
                              pParms->u.UnInstall.pwszModel);
    else if (pParms->u.UnInstall.pwszPnpId)
        vboxWinDrvInstLogInfo(pCtx, "%s PnP ID \"%ls\" ... ", fInstall ? "Installing" : "Uninstalling",
                              pParms->u.UnInstall.pwszPnpId);

    if (   pParms->fFlags & VBOX_WIN_DRIVERINSTALL_F_SILENT
        && g_pfnSetupSetNonInteractiveMode)
    {
        vboxWinDrvInstLogInfo(pCtx, "Setting non-interactive mode ...");
        g_pfnSetupSetNonInteractiveMode(TRUE /* fEnable */);
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
    else
        vboxWinDrvInstLogError(pCtx, "Unable to %sinstall driver, rc=%Rrc", fInstall ? "" : "un", rc);

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
        uint64_t const uNtVer = RTSystemGetNtVersion();

        vboxWinDrvInstLogInfo(pCtx, VBOX_PRODUCT " Version " VBOX_VERSION_STRING " - r%s", RTBldCfgRevisionStr());
        vboxWinDrvInstLogInfo(pCtx, "Detected Windows version %d.%d.%d (%s)", RTSYSTEM_NT_VERSION_GET_MAJOR(uNtVer),
                                                                              RTSYSTEM_NT_VERSION_GET_MINOR(uNtVer),
                                                                              RTSYSTEM_NT_VERSION_GET_BUILD(uNtVer),
                                                                              RTBldCfgTargetArch());
        pCtx->u32Magic   = VBOXWINDRVINST_MAGIC;
        pCtx->uVerbosity = uVerbosity;
        pCtx->pfnLog     = pfnLog;
        pCtx->pvUser     = pvUser;
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
 * Installs a driver.
 *
 * @returns VBox status code.
 * @param   hDrvInst            Windows driver installer handle to use.
 * @param   pszInfFile          INF file to use.
 * @param   pszPnpId            PnP ID to use. NT-style wildcards supported. Optional and can be NULL.
 * @param   fFlags              Installation flags (of type VBOX_WIN_DRIVERINSTALL_F_XXX) to use.
 */
int VBoxWinDrvInstInstall(VBOXWINDRVINST hDrvInst, const char *pszInfFile, const char *pszPnpId, uint32_t fFlags)
{
    PVBOXWINDRVINSTINTERNAL pCtx = hDrvInst;
    VBOXWINDRVINST_VALID_RETURN(pCtx);

    AssertPtrReturn(pszInfFile, VERR_INVALID_PARAMETER);

    vboxWinDrvInstParmsInit(&pCtx->Parms);

    /* Resolve the INF file's absolute path, as as Setup API functions tend to need this. */
    char szInfPathAbs[RTPATH_MAX];
    int rc = RTPathReal(pszInfFile, szInfPathAbs, sizeof(szInfPathAbs));
    if (RT_SUCCESS(rc))
        rc = RTStrToUtf16(szInfPathAbs, &pCtx->Parms.pwszInfFile);

    if (RT_FAILURE(rc))
        vboxWinDrvInstLogError(pCtx, "Failed to build path for INF file \"%s\", rc=%Rrc", pszInfFile, rc);

    if (RT_SUCCESS(rc) && pszPnpId) /* PnP ID is optional. */
        rc = RTStrToUtf16(pszPnpId, &pCtx->Parms.u.UnInstall.pwszPnpId);

    if (RT_SUCCESS(rc))
    {
        pCtx->Parms.enmMode = VBOXWINDRVINSTMODE_INSTALL;
        pCtx->Parms.fFlags  = fFlags;

        rc = vboxWinDrvInstMain(pCtx, &pCtx->Parms);
    }

    vboxWinDrvInstParmsDestroy(&pCtx->Parms);

    if (RT_FAILURE(rc))
        vboxWinDrvInstLogError(pCtx, "Driver installation failed with %Rrc", rc);

    return rc;
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

