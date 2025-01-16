/* $Id: SUPLibInternal.h 107889 2025-01-16 00:29:04Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library - Internal header.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Support_SUPLibInternal_h
#define VBOX_INCLUDED_SRC_Support_SUPLibInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/stdarg.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** @def SUPLIB_DLL_SUFF
 * The (typical) DLL/DYLIB/SO suffix. */
#if defined(RT_OS_DARWIN)
# define SUPLIB_DLL_SUFF    ".dylib"
#elif defined(RT_OS_L4)
# define SUPLIB_DLL_SUFF    ".s.so"
#elif defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define SUPLIB_DLL_SUFF    ".dll"
#else
# define SUPLIB_DLL_SUFF    ".so"
#endif

#ifdef RT_OS_SOLARIS
/** Number of dummy files to open (2:ip4, 1:ip6, 1:extra) see
 *  @bugref{4650}. */
# define SUPLIB_FLT_DUMMYFILES 4
#endif

/** @def SUPLIB_EXE_SUFF
 * The (typical) executable suffix. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define SUPLIB_EXE_SUFF    ".exe"
#else
# define SUPLIB_EXE_SUFF    ""
#endif

/** @def SUP_HARDENED_SUID
 * Whether we're employing set-user-ID-on-execute in the hardening.
 */
#if (!defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_L4)) || defined(DOXYGEN_RUNNING)
# define SUP_HARDENED_SUID
#else
# undef  SUP_HARDENED_SUID
#endif

#ifdef IN_SUP_HARDENED_R3
/** @name Make the symbols in SUPR3HardenedStatic different from the VBoxRT ones.
 * We cannot rely on DECLHIDDEN to make this separation for us since it doesn't
 * work with all GCC versions. So, we resort to old fashion precompiler hacking.
 * @{
 */
# define supR3HardenedPathAppPrivateNoArch supR3HardenedStaticPathAppPrivateNoArch
# define supR3HardenedPathAppPrivateArch   supR3HardenedStaticPathAppPrivateArch
# define supR3HardenedPathAppSharedLibs    supR3HardenedStaticPathAppSharedLibs
# define supR3HardenedPathAppDocs          supR3HardenedStaticPathAppDocs
# define supR3HardenedPathAppBin           supR3HardenedStaticPathAppBin
# define supR3HardenedPathFilename         supR3HardenedStaticPathFilename
# define supR3HardenedFatalV               supR3HardenedStaticFatalV
# define supR3HardenedFatal                supR3HardenedStaticFatal
# define supR3HardenedFatalMsgV            supR3HardenedStaticFatalMsgV
# define supR3HardenedFatalMsg             supR3HardenedStaticFatalMsg
# define supR3HardenedErrorV               supR3HardenedStaticErrorV
# define supR3HardenedError                supR3HardenedStaticError
# define supR3HardenedOpenLog              supR3HardenedStaticOpenLog
# define supR3HardenedLogV                 supR3HardenedStaticLogV
# define supR3HardenedLog                  supR3HardenedStaticLog
# define supR3HardenedLogFlush             supR3HardenedStaticLogFlush
# define supR3HardenedVerifyAll            supR3HardenedStaticVerifyAll
# define supR3HardenedVerifyFixedDir       supR3HardenedStaticVerifyFixedDir
# define supR3HardenedVerifyFixedFile      supR3HardenedStaticVerifyFixedFile
# define supR3HardenedVerifyDir            supR3HardenedStaticVerifyDir
# define supR3HardenedVerifyFile           supR3HardenedStaticVerifyFile
# define supR3HardenedGetPreInitData       supR3HardenedStaticGetPreInitData
# define supR3HardenedRecvPreInitData      supR3HardenedStaticRecvPreInitData
/** @} */
#endif /* IN_SUP_HARDENED_R3 */


/** @name CRT function mappings (not using CRT on Windows).
 * @{
 */
#if defined(IN_SUP_HARDENED_R3) && defined(RT_OS_WINDOWS)
# define SUP_HARDENED_NEED_CRT_FUNCTIONS
DECLHIDDEN(int)     suplibHardenedMemComp(void const *pvDst, const void *pvSrc, size_t cbToComp);
DECLHIDDEN(void *)  suplibHardenedMemCopy(void *pvDst, const void *pvSrc, size_t cbToCopy);
DECLHIDDEN(void *)  suplibHardenedMemSet(void *pvDst, int ch, size_t cbToSet);
DECLHIDDEN(char *)  suplibHardenedStrCopy(char *pszDst, const char *pszSrc);
DECLHIDDEN(size_t)  suplibHardenedStrLen(const char *psz);
DECLHIDDEN(char *)  suplibHardenedStrCat(char *pszDst, const char *pszSrc);
DECLHIDDEN(int)     suplibHardenedStrCmp(const char *psz1, const char *psz2);
DECLHIDDEN(int)     suplibHardenedStrNCmp(const char *psz1, const char *psz2, size_t cchMax);
#else
# undef SUP_HARDENED_NEED_CRT_FUNCTIONS
# define suplibHardenedMemComp memcmp
# define suplibHardenedMemCopy memcpy
# define suplibHardenedMemSet  memset
# define suplibHardenedStrCopy strcpy
# define suplibHardenedStrLen  strlen
# define suplibHardenedStrCat  strcat
# define suplibHardenedStrCmp  strcmp
# define suplibHardenedStrNCmp strncmp
#endif
DECLHIDDEN(DECL_NO_RETURN(void)) suplibHardenedExit(RTEXITCODE rcExit);
DECLHIDDEN(void)    suplibHardenedPrintF(const char *pszFormat, ...);
DECLHIDDEN(void)    suplibHardenedPrintFV(const char *pszFormat, va_list va);

/** @} */

/** Debug output macro. */
#ifdef IN_SUP_HARDENED_R3
# if defined(DEBUG_bird) && defined(RT_OS_WINDOWS)
#  define SUP_DPRINTF(a)    do { supR3HardenedStaticLog a; suplibHardenedPrintF a; } while (0)
# else
#  define SUP_DPRINTF(a)    do { supR3HardenedStaticLog a; } while (0)
# endif
#else
# if defined(DEBUG_bird) && defined(RT_OS_WINDOWS)
#  define SUP_DPRINTF(a)    RTLogPrintf a
# else
#  define SUP_DPRINTF(a)    do { } while (0)
# endif
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * The type of an installed file.
 */
typedef enum SUPINSTFILETYPE
{
    kSupIFT_Invalid = 0,
    kSupIFT_Exe,
    kSupIFT_Dll,
    kSupIFT_Rc,
    kSupIFT_Sys,
    kSupIFT_Script,
    kSupIFT_Data,
    kSupIFT_TestExe,
    kSupIFT_TestDll,
    kSupIFT_End
} SUPINSTFILETYPE;

/**
 * Installation directory specifier.
 */
typedef enum SUPINSTDIR
{
    kSupID_Invalid = 0,
    kSupID_AppBin,
    kSupID_AppSharedLib,
    kSupID_AppPrivArch,
    kSupID_AppPrivArchComp,
    kSupID_AppPrivNoArch,
    kSupID_Testcase,
#ifdef RT_OS_DARWIN
    kSupID_AppMacHelper,
#endif
    kSupID_End
} SUPINSTDIR;

/**
 * Installed file.
 */
typedef struct SUPINSTFILE
{
    /** File type. */
    SUPINSTFILETYPE enmType;
    /** Install directory. */
    SUPINSTDIR enmDir;
    /** Optional (true) or mandatory (false. */
    bool fOptional;
    /** File name. */
    const char *pszFile;
} SUPINSTFILE;
typedef SUPINSTFILE *PSUPINSTFILE;
typedef SUPINSTFILE const *PCSUPINSTFILE;

/**
 * Status data for a verified file.
 */
typedef struct SUPVERIFIEDFILE
{
    /** The file handle or descriptor. -1 if not open. */
    intptr_t    hFile;
    /** Whether the file has been validated. */
    bool        fValidated;
#ifdef RT_OS_WINDOWS
    /** Whether we've checked the signature of the file. */
    bool        fCheckedSignature;
#endif
} SUPVERIFIEDFILE;
typedef SUPVERIFIEDFILE *PSUPVERIFIEDFILE;
typedef SUPVERIFIEDFILE const *PCSUPVERIFIEDFILE;

/**
 * Status data for a verified directory.
 */
typedef struct SUPVERIFIEDDIR
{
    /** The directory handle or descriptor. -1 if not open. */
    intptr_t    hDir;
    /** Whether the directory has been validated. */
    bool        fValidated;
} SUPVERIFIEDDIR;
typedef SUPVERIFIEDDIR *PSUPVERIFIEDDIR;
typedef SUPVERIFIEDDIR const *PCSUPVERIFIEDDIR;


/**
 * SUPLib instance data.
 *
 * This is data that is passed from the static to the dynamic SUPLib
 * in a hardened setup.
 */
typedef struct SUPLIBDATA
{
    /** The device handle. */
#if defined(RT_OS_WINDOWS)
    void               *hDevice;
#else
    int                 hDevice;
#endif
    /** Indicates whether we have unrestricted (true) or restricted access to the
     * support device. */
    bool                fUnrestricted;
    /** Set if we're in driverless mode. */
    bool                fDriverless;
#if   defined(RT_OS_DARWIN)
    /** The connection to the VBoxSupDrv service. */
    uintptr_t           uConnection;
#elif defined(RT_OS_LINUX)
    /** Indicates whether madvise(,,MADV_DONTFORK) works. */
    bool                fSysMadviseWorks;
#elif defined(RT_OS_SOLARIS)
    /** Extra dummy file descriptors to prevent growing file-descriptor table on
     *  clean up (see @bugref{4650}). */
    int                 ahDummy[SUPLIB_FLT_DUMMYFILES];
#elif defined(RT_OS_WINDOWS)
#endif
} SUPLIBDATA;
/** Pointer to the pre-init data. */
typedef SUPLIBDATA *PSUPLIBDATA;
/** Pointer to const pre-init data. */
typedef SUPLIBDATA const *PCSUPLIBDATA;

/** The NIL value of SUPLIBDATA::hDevice. */
#if defined(RT_OS_WINDOWS)
# define SUP_HDEVICE_NIL NULL
#else
# define SUP_HDEVICE_NIL (-1)
#endif


/**
 * Pre-init data that is handed over from the hardened executable stub.
 */
typedef struct SUPPREINITDATA
{
    /** Magic value (SUPPREINITDATA_MAGIC). */
    uint32_t            u32Magic;
    /** The SUPLib instance data. */
    SUPLIBDATA          Data;
    /** The number of entries in paInstallFiles and paVerifiedFiles. */
    size_t              cInstallFiles;
    /** g_aSupInstallFiles. */
    PCSUPINSTFILE       paInstallFiles;
    /** g_aSupVerifiedFiles. */
    PCSUPVERIFIEDFILE   paVerifiedFiles;
    /** The number of entries in paVerifiedDirs. */
    size_t              cVerifiedDirs;
    /** g_aSupVerifiedDirs. */
    PCSUPVERIFIEDDIR    paVerifiedDirs;
    /** Magic value (SUPPREINITDATA_MAGIC). */
    uint32_t            u32EndMagic;
} SUPPREINITDATA;
typedef SUPPREINITDATA *PSUPPREINITDATA;
typedef SUPPREINITDATA const *PCSUPPREINITDATA;

/** Magic value for SUPPREINITDATA::u32Magic and SUPPREINITDATA::u32EndMagic. */
#define SUPPREINITDATA_MAGIC    UINT32_C(0xbeef0001)

/** @copydoc supR3PreInit */
typedef DECLCALLBACKTYPE(int, FNSUPR3PREINIT,(PSUPPREINITDATA pPreInitData, uint32_t fFlags));
/** Pointer to supR3PreInit. */
typedef FNSUPR3PREINIT *PFNSUPR3PREINIT;

/** The current SUPR3HardenedMain state / location. */
typedef enum SUPR3HARDENEDMAINSTATE
{
    SUPR3HARDENEDMAINSTATE_NOT_YET_CALLED = 0,
    SUPR3HARDENEDMAINSTATE_WIN_EARLY_INIT_CALLED,
    SUPR3HARDENEDMAINSTATE_WIN_EARLY_IMPORTS_RESOLVED,
    SUPR3HARDENEDMAINSTATE_WIN_EARLY_STUB_DEVICE_OPENED,
    SUPR3HARDENEDMAINSTATE_WIN_EARLY_REAL_DEVICE_OPENED,
    SUPR3HARDENEDMAINSTATE_WIN_EP_CALLED,
    SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED,
    SUPR3HARDENEDMAINSTATE_WIN_VERSION_INITIALIZED,
    SUPR3HARDENEDMAINSTATE_WIN_VERIFY_TRUST_READY,
    SUPR3HARDENEDMAINSTATE_HARDENED_MAIN_CALLED,
    SUPR3HARDENEDMAINSTATE_INIT_RUNTIME,
    SUPR3HARDENEDMAINSTATE_GET_TRUSTED_MAIN,
    SUPR3HARDENEDMAINSTATE_CALLED_TRUSTED_MAIN,
    SUPR3HARDENEDMAINSTATE_END,
    SUPR3HARDENEDMAINSTATE_32BIT_HACK = 0x7fffffff
} SUPR3HARDENEDMAINSTATE;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
extern DECL_HIDDEN_DATA(uint32_t)               g_u32Cookie;
extern DECL_HIDDEN_DATA(uint32_t)               g_u32SessionCookie;
extern DECL_HIDDEN_DATA(uint32_t)               g_uSupSessionVersion;
extern DECL_HIDDEN_DATA(SUPLIBDATA)             g_supLibData;
extern DECL_HIDDEN_DATA(uint32_t)               g_uSupFakeMode;
extern DECL_HIDDEN_DATA(PSUPGLOBALINFOPAGE)     g_pSUPGlobalInfoPageR0;
#ifdef VBOX_INCLUDED_SRC_Support_SUPDrvIOC_h
extern DECL_HIDDEN_DATA(PSUPQUERYFUNCS)         g_pSupFunctions;
#endif
extern DECL_HIDDEN_DATA(SUPR3HARDENEDMAINSTATE) g_enmSupR3HardenedMainState;
#ifdef RT_OS_WINDOWS
extern DECL_HIDDEN_DATA(bool)                   g_fSupEarlyProcessInit;
#endif


/*******************************************************************************
*   OS Specific Function                                                       *
*******************************************************************************/
RT_C_DECLS_BEGIN
DECLHIDDEN(int)     suplibOsInstall(void);
DECLHIDDEN(int)     suplibOsUninstall(void);
DECLHIDDEN(int)     suplibOsInit(PSUPLIBDATA pThis, bool fPreInited, uint32_t fFlags, SUPINITOP *penmWhat, PRTERRINFO pErrInfo);
DECLHIDDEN(int)     suplibOsTerm(PSUPLIBDATA pThis);
DECLHIDDEN(int)     suplibOsHardenedVerifyInit(void);
DECLHIDDEN(int)     suplibOsHardenedVerifyTerm(void);
DECLHIDDEN(int)     suplibOsIOCtl(PSUPLIBDATA pThis, uintptr_t uFunction, void *pvReq, size_t cbReq);
DECLHIDDEN(int)     suplibOsIOCtlFast(PSUPLIBDATA pThis, uintptr_t uFunction, uintptr_t idCpu);
DECLHIDDEN(int)     suplibOsPageAlloc(PSUPLIBDATA pThis, size_t cPages, uint32_t fFlags, void **ppvPages);
DECLHIDDEN(int)     suplibOsPageFree(PSUPLIBDATA pThis, void *pvPages, size_t cPages);
DECLHIDDEN(int)     suplibOsQueryVTxSupported(const char **ppszWhy);
DECLHIDDEN(int)     suplibOsQueryMicrocodeRev(uint32_t *puMicrocodeRev);
DECLHIDDEN(bool)    suplibOsIsNemSupportedWhenNoVtxOrAmdV(void);


/**
 * Performs the pre-initialization of the support library.
 *
 * This is dynamically resolved and invoked by the static library before it
 * calls RTR3InitEx and thereby SUPR3Init.
 *
 * @returns IPRT status code.
 * @param   pPreInitData    The pre init data.
 * @param   fFlags          The SUPR3HardenedMain flags.
 */
DECL_NOTHROW(DECLEXPORT(int)) supR3PreInit(PSUPPREINITDATA pPreInitData, uint32_t fFlags);


/** @copydoc RTPathAppPrivateNoArch */
DECLHIDDEN(int)     supR3HardenedPathAppPrivateNoArch(char *pszPath, size_t cchPath);
/** @copydoc RTPathAppPrivateArch */
DECLHIDDEN(int)     supR3HardenedPathAppPrivateArch(char *pszPath, size_t cchPath);
/** @copydoc RTPathSharedLibs */
DECLHIDDEN(int)     supR3HardenedPathAppSharedLibs(char *pszPath, size_t cchPath);
/** @copydoc RTPathAppDocs */
DECLHIDDEN(int)     supR3HardenedPathAppDocs(char *pszPath, size_t cchPath);
/** @copydoc RTPathExecDir */
DECLHIDDEN(int)     supR3HardenedPathAppBin(char *pszPath, size_t cchPath);
/** @copydoc RTPathFilename */
DECLHIDDEN(char *)  supR3HardenedPathFilename(const char *pszPath);

/**
 * Display a fatal error and try call TrustedError or quit.
 */
DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalMsgV(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                        const char *pszMsgFmt, va_list va);

/**
 * Display a fatal error and try call TrustedError or quit.
 */
DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalMsg(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                       const char *pszMsgFmt, ...);

/**
 * Display a fatal error and quit.
 */
DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalV(const char *pszFormat, va_list va);

/**
 * Display a fatal error and quit.
 */
DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatal(const char *pszFormat, ...);

/**
 * Display an error which may or may not be fatal.
 */
DECLHIDDEN(int)     supR3HardenedErrorV(int rc, bool fFatal, const char *pszFormat, va_list va);

/**
 * Display an error which may or may not be fatal.
 */
DECLHIDDEN(int)     supR3HardenedError(int rc, bool fFatal, const char *pszFormat, ...);

/**
 * Open any startup log file specified in the argument.
 */
DECLHIDDEN(void)    supR3HardenedOpenLog(int *pcArgs, char **papszArgs);

/**
 * Write to the startup log file.
 */
DECLHIDDEN(void)    supR3HardenedLogV(const char *pszFormat, va_list va);

/**
 * Write to the startup log file.
 */
DECLHIDDEN(void)    supR3HardenedLog(const char *pszFormat, ...);

/**
 * Flushes the log file.
 */
DECLHIDDEN(void)    supR3HardenedLogFlush(void);


DECLHIDDEN(int)     supR3HardenedVerifyAll(bool fFatal, const char *pszProgName, const char *pszExePath, uint32_t fMainFlags);
DECLHIDDEN(int)     supR3HardenedVerifyFixedDir(SUPINSTDIR enmDir, bool fFatal, PCSUPINSTFILE pFile);
DECLHIDDEN(int)     supR3HardenedVerifyFixedFile(const char *pszFilename, bool fFatal);
DECLHIDDEN(int)     supR3HardenedVerifyDir(const char *pszDirPath, bool fRecursive, bool fCheckFiles, PRTERRINFO pErrInfo);
DECLHIDDEN(int)     supR3HardenedVerifyFile(const char *pszFilename, RTHCUINTPTR hNativeFile, bool fMaybe3rdParty,
                                            PRTERRINFO pErrInfo);
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
DECLHIDDEN(int)     supR3HardenedVerifyFileFollowSymlinks(const char *pszFilename, RTHCUINTPTR hNativeFile,
                                                          bool fMaybe3rdParty, PRTERRINFO pErrInfo);
#endif
DECLHIDDEN(void)    supR3HardenedGetPreInitData(PSUPPREINITDATA pPreInitData);
DECLHIDDEN(int)     supR3HardenedRecvPreInitData(PCSUPPREINITDATA pPreInitData);

#ifdef RT_OS_WINDOWS
DECLHIDDEN(void)    supR3HardenedWinInit(uint32_t fFlags, bool fAvastKludge);
DECLHIDDEN(void)    supR3HardenedWinInitAppBin(uint32_t fFlags);
DECLHIDDEN(void)    supR3HardenedWinInitVersion(bool fEarlyInit);
DECLHIDDEN(void)    supR3HardenedWinInitImports(void);
DECLHIDDEN(void)    supR3HardenedWinModifyDllSearchPath(uint32_t fFlags, const char *pszAppBinPath);
# ifdef IPRT_INCLUDED_nt_nt_h
DECLHIDDEN(void)    supR3HardenedWinGetVeryEarlyImports(uintptr_t uNtDllAddr, PFNNTWAITFORSINGLEOBJECT *ppfnNtWaitForSingleObject,
                                                        PFNNTSETEVENT *ppfnNtSetEvent);
# endif
DECLHIDDEN(void)    supR3HardenedWinInitImportsEarly(uintptr_t uNtDllAddr);
DECLHIDDEN(void)    supR3HardenedWinInitSyscalls(bool fReportErrors, PRTERRINFO pErrInfo);
DECLHIDDEN(PFNRT)   supR3HardenedWinGetRealDllSymbol(const char *pszDll, const char *pszProcedure);
DECLHIDDEN(void)    supR3HardenedWinEnableThreadCreation(void);
DECLHIDDEN(void)    supR3HardenedWinResolveVerifyTrustApiAndHookThreadCreation(const char *pszProgName);
DECLHIDDEN(void)    supR3HardenedWinFlushLoaderCache();
DECLHIDDEN(bool)    supR3HardenedWinIsReSpawnNeeded(int iWhich, int cArgs, char **papszArgs);
DECLHIDDEN(int)     supR3HardenedWinReSpawn(int iWhich);
# ifdef _WINDEF_
DECLHIDDEN(void)    supR3HardenedWinCreateParentWatcherThread(HMODULE hVBoxRT);
# endif
DECLHIDDEN(void *)  supR3HardenedWinLoadLibrary(const char *pszName, bool fSystem32Only, uint32_t fMainFlags);
extern RTUTF16      g_wszSupLibHardenedExePath[1024];
# ifdef RTPATH_MAX
extern char         g_szSupLibHardenedExePath[RTPATH_MAX];
# endif
DECLHIDDEN(void)    supR3HardenedWinCompactHeaps(void);
DECLHIDDEN(void)    supR3HardenedMainOpenDevice(void);
DECLHIDDEN(char *)  supR3HardenedWinReadErrorInfoDevice(char *pszErrorInfo, size_t cbErrorInfo, const char *pszPrefix);
DECLHIDDEN(void)    supR3HardenedWinReportErrorToParent(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                        const char *pszFormat, va_list va);
#else   /* !RT_OS_WINDOWS */
# if !defined(RT_OS_DARWIN)
DECLHIDDEN(void)    supR3HardenedPosixInit(void);
# else  /* !RT_OS_DARWIN */
DECLHIDDEN(void)    supR3HardenedDarwinInit(void);
#endif  /* !RT_OS_DARWIN */
#endif  /* !RT_OS_WINDOWS */

SUPR3DECL(int)      supR3PageLock(void *pvStart, size_t cPages, PSUPPAGE paPages);
SUPR3DECL(int)      supR3PageUnlock(void *pvStart);

RT_C_DECLS_END


#endif /* !VBOX_INCLUDED_SRC_Support_SUPLibInternal_h */

