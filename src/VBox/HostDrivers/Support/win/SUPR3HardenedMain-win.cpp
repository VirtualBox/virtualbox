/* $Id: SUPR3HardenedMain-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened main(), windows bits.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/nt/nt-and-windows.h>
#include <AccCtrl.h>
#include <AclApi.h>
#ifndef PROCESS_SET_LIMITED_INFORMATION
# define PROCESS_SET_LIMITED_INFORMATION        0x2000
#endif
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
# define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR       UINT32_C(0x100)
# define LOAD_LIBRARY_SEARCH_APPLICATION_DIR    UINT32_C(0x200)
# define LOAD_LIBRARY_SEARCH_USER_DIRS          UINT32_C(0x400)
# define LOAD_LIBRARY_SEARCH_SYSTEM32           UINT32_C(0x800)
#endif

#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>
#include <iprt/zero.h>

#include "SUPLibInternal.h"
#include "win/SUPHardenedVerify-win.h"
#include "../SUPDrvIOC.h"

#ifndef IMAGE_SCN_TYPE_NOLOAD
# define IMAGE_SCN_TYPE_NOLOAD 0x00000002
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The first argument of a respawed stub when respawned for the first time.
 * This just needs to be unique enough to avoid most confusion with real
 * executable names,  there are other checks in place to make sure we've respanwed. */
#define SUPR3_RESPAWN_1_ARG0  "60eaff78-4bdd-042d-2e72-669728efd737-suplib-2ndchild"

/** The first argument of a respawed stub when respawned for the second time.
 * This just needs to be unique enough to avoid most confusion with real
 * executable names,  there are other checks in place to make sure we've respanwed. */
#define SUPR3_RESPAWN_2_ARG0  "60eaff78-4bdd-042d-2e72-669728efd737-suplib-3rdchild"

/** Unconditional assertion. */
#define SUPR3HARDENED_ASSERT(a_Expr) \
    do { \
        if (!(a_Expr)) \
            supR3HardenedFatal("%s: %s\n", __FUNCTION__, #a_Expr); \
    } while (0)

/** Unconditional assertion of NT_SUCCESS. */
#define SUPR3HARDENED_ASSERT_NT_SUCCESS(a_Expr) \
    do { \
        NTSTATUS rcNtAssert = (a_Expr); \
        if (!NT_SUCCESS(rcNtAssert)) \
            supR3HardenedFatal("%s: %s -> %#x\n", __FUNCTION__, #a_Expr, rcNtAssert); \
    } while (0)

/** Unconditional assertion of a WIN32 API returning non-FALSE. */
#define SUPR3HARDENED_ASSERT_WIN32_SUCCESS(a_Expr) \
    do { \
        BOOL fRcAssert = (a_Expr); \
        if (fRcAssert == FALSE) \
            supR3HardenedFatal("%s: %s -> %#x\n", __FUNCTION__, #a_Expr, RtlGetLastWin32Error()); \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Security descriptor cleanup structure.
 */
typedef struct MYSECURITYCLEANUP
{
    union
    {
        SID                 Sid;
        uint8_t             abPadding[SECURITY_MAX_SID_SIZE];
    }                       Everyone, Owner, User, Login;
    union
    {
        ACL                 AclHdr;
        uint8_t             abPadding[1024];
    }                       Acl;
    PSECURITY_DESCRIPTOR    pSecDesc;
} MYSECURITYCLEANUP;
/** Pointer to security cleanup structure. */
typedef MYSECURITYCLEANUP *PMYSECURITYCLEANUP;


/**
 * Image verifier cache entry.
 */
typedef struct VERIFIERCACHEENTRY
{
    /** Pointer to the next entry with the same hash value. */
    struct VERIFIERCACHEENTRY * volatile pNext;
    /** Next entry in the WinVerifyTrust todo list. */
    struct VERIFIERCACHEENTRY * volatile pNextTodoWvt;

    /** The file handle. */
    HANDLE                  hFile;
    /** If fIndexNumber is set, this is an file system internal file identifier. */
    LARGE_INTEGER           IndexNumber;
    /** The path hash value. */
    uint32_t                uHash;
    /** The verification result. */
    int                     rc;
    /** Used for shutting up load and error messages after a while so they don't
     * flood the log file and fill up the disk. */
    uint32_t volatile       cHits;
    /** The validation flags (for WinVerifyTrust retry). */
    uint32_t                fFlags;
    /** Whether IndexNumber is valid  */
    bool                    fIndexNumberValid;
    /** Whether verified by WinVerifyTrust. */
    bool volatile           fWinVerifyTrust;
    /** cwcPath * sizeof(RTUTF16). */
    uint16_t                cbPath;
    /** The full path of this entry (variable size).  */
    RTUTF16                 wszPath[1];
} VERIFIERCACHEENTRY;
/** Pointer to an image verifier path entry. */
typedef VERIFIERCACHEENTRY *PVERIFIERCACHEENTRY;


/**
 * Name of an import DLL that we need to check out.
 */
typedef struct VERIFIERCACHEIMPORT
{
    /** Pointer to the next DLL in the list. */
    struct VERIFIERCACHEIMPORT * volatile pNext;
    /** The length of pwszAltSearchDir if available. */
    uint32_t                cwcAltSearchDir;
    /** This points the directory containing the DLL needing it, this will be
     * NULL for a System32 DLL. */
    PWCHAR                  pwszAltSearchDir;
    /** The name of the import DLL (variable length). */
    char                    szName[1];
} VERIFIERCACHEIMPORT;
/** Pointer to a import DLL that needs checking out. */
typedef VERIFIERCACHEIMPORT *PVERIFIERCACHEIMPORT;


/**
 * Child requests.
 */
typedef enum SUPR3WINCHILDREQ
{
    /** Perform child purification and close full access handles (must be zero). */
    kSupR3WinChildReq_PurifyChildAndCloseHandles = 0,
    /** Close the events, we're good on our own from here on.  */
    kSupR3WinChildReq_CloseEvents,
    /** Reporting error. */
    kSupR3WinChildReq_Error,
    /** End of valid requests. */
    kSupR3WinChildReq_End
} SUPR3WINCHILDREQ;

/**
 * Child process parameters.
 */
typedef struct SUPR3WINPROCPARAMS
{
    /** The event semaphore the child will be waiting on. */
    HANDLE                      hEvtChild;
    /** The event semaphore the parent will be waiting on. */
    HANDLE                      hEvtParent;

    /** The address of the NTDLL. This is only valid during the very early
     * initialization as we abuse for thread creation protection. */
    uintptr_t                   uNtDllAddr;

    /** The requested operation (set by the child). */
    SUPR3WINCHILDREQ            enmRequest;
    /** The last status. */
    int32_t                     rc;
    /** The init operation the error relates to if message, kSupInitOp_Invalid if
     *  not message. */
    SUPINITOP                   enmWhat;
    /** Where if message. */
    char                        szWhere[80];
    /** Error message / path name string space. */
    char                        szErrorMsg[16384+1024];
} SUPR3WINPROCPARAMS;


/**
 * Child process data structure for use during child process init setup and
 * purification.
 */
typedef struct SUPR3HARDNTCHILD
{
    /** Process handle. */
    HANDLE                      hProcess;
    /** Primary thread handle. */
    HANDLE                      hThread;
    /** Handle to the parent process, if we're the middle (stub) process. */
    HANDLE                      hParent;
    /** The event semaphore the child will be waiting on. */
    HANDLE                      hEvtChild;
    /** The event semaphore the parent will be waiting on. */
    HANDLE                      hEvtParent;
    /** The address of NTDLL in the child. */
    uintptr_t                   uNtDllAddr;
    /** The address of NTDLL in this process. */
    uintptr_t                   uNtDllParentAddr;
    /** Which respawn number this is (1 = stub, 2 = VM). */
    int                         iWhich;
    /** The basic process info. */
    PROCESS_BASIC_INFORMATION   BasicInfo;
    /** The probable size of the PEB. */
    size_t                      cbPeb;
    /** The pristine process environment block. */
    PEB                         Peb;
    /** The child process parameters. */
    SUPR3WINPROCPARAMS          ProcParams;
} SUPR3HARDNTCHILD;
/** Pointer to a child process data structure. */
typedef SUPR3HARDNTCHILD *PSUPR3HARDNTCHILD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Process parameters.  Specified by parent if VM process, see
 *  supR3HardenedVmProcessInit. */
static SUPR3WINPROCPARAMS   g_ProcParams = { NULL, NULL, 0, (SUPR3WINCHILDREQ)0, 0 };
/** Set if supR3HardenedEarlyProcessInit was invoked. */
bool                        g_fSupEarlyProcessInit = false;
/** Set if the stub device has been opened (stub process only). */
bool                        g_fSupStubOpened = false;

/** @name Global variables initialized by suplibHardenedWindowsMain.
 * @{ */
/** Combined windows NT version number.  See SUP_MAKE_NT_VER_COMBINED. */
uint32_t                    g_uNtVerCombined = 0;
/** Count calls to the special main function for linking santity checks. */
static uint32_t volatile    g_cSuplibHardenedWindowsMainCalls;
/** The UTF-16 windows path to the executable. */
RTUTF16                     g_wszSupLibHardenedExePath[1024];
/** The NT path of the executable. */
SUPSYSROOTDIRBUF            g_SupLibHardenedExeNtPath;
/** The NT path of the application binary directory. */
SUPSYSROOTDIRBUF            g_SupLibHardenedAppBinNtPath;
/** The offset into g_SupLibHardenedExeNtPath of the executable name (WCHAR,
 * not byte). This also gives the length of the exectuable directory path,
 * including a trailing slash. */
static uint32_t             g_offSupLibHardenedExeNtName;
/** Set if we need to use the LOAD_LIBRARY_SEARCH_USER_DIRS option. */
bool                        g_fSupLibHardenedDllSearchUserDirs = false;
/** @} */

/** @name Hook related variables.
 * @{ */
/** Pointer to the bit of assembly code that will perform the original
 *  NtCreateSection operation. */
static NTSTATUS     (NTAPI *g_pfnNtCreateSectionReal)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                                      PLARGE_INTEGER, ULONG, ULONG, HANDLE);
/** Pointer to the NtCreateSection function in NtDll (for patching purposes). */
static uint8_t             *g_pbNtCreateSection;
/** The patched NtCreateSection bytes (for restoring). */
static uint8_t              g_abNtCreateSectionPatch[16];
/** Pointer to the bit of assembly code that will perform the original
 *  LdrLoadDll operation. */
static NTSTATUS     (NTAPI *g_pfnLdrLoadDllReal)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);
/** Pointer to the LdrLoadDll function in NtDll (for patching purposes). */
static uint8_t             *g_pbLdrLoadDll;
/** The patched LdrLoadDll bytes (for restoring). */
static uint8_t              g_abLdrLoadDllPatch[16];

#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
/** Pointer to the bit of assembly code that will perform the original
 *  KiUserExceptionDispatcher operation. */
static VOID        (NTAPI *g_pfnKiUserExceptionDispatcherReal)(void);
/** Pointer to the KiUserExceptionDispatcher function in NtDll (for patching purposes). */
static uint8_t             *g_pbKiUserExceptionDispatcher;
/** The patched KiUserExceptionDispatcher bytes (for restoring). */
static uint8_t              g_abKiUserExceptionDispatcherPatch[16];
#endif

/** Pointer to the bit of assembly code that will perform the original
 *  KiUserApcDispatcher operation. */
static VOID        (NTAPI *g_pfnKiUserApcDispatcherReal)(void);
/** Pointer to the KiUserApcDispatcher function in NtDll (for patching purposes). */
static uint8_t             *g_pbKiUserApcDispatcher;
/** The patched KiUserApcDispatcher bytes (for restoring). */
static uint8_t              g_abKiUserApcDispatcherPatch[16];

/** Pointer to the LdrInitializeThunk function in NtDll for
 *  supR3HardenedMonitor_KiUserApcDispatcher_C() to use for APC vetting. */
static uintptr_t            g_pfnLdrInitializeThunk;

/** The hash table of verifier cache . */
static PVERIFIERCACHEENTRY  volatile g_apVerifierCache[128];
/** Queue of cached images which needs WinVerifyTrust to check them. */
static PVERIFIERCACHEENTRY  volatile g_pVerifierCacheTodoWvt = NULL;
/** Queue of cached images which needs their imports checked. */
static PVERIFIERCACHEIMPORT volatile g_pVerifierCacheTodoImports = NULL;

/** The windows path to dir \\SystemRoot\\System32 directory (technically
 *  this whatever \\KnownDlls\\KnownDllPath points to). */
SUPSYSROOTDIRBUF            g_System32WinPath;
/** @} */

/** Positive if the DLL notification callback has been registered, counts
 * registration attempts as negative. */
static int                  g_cDllNotificationRegistered = 0;
/** The registration cookie of the DLL notification callback. */
static PVOID                g_pvDllNotificationCookie = NULL;

/** Static error info structure used during init. */
static RTERRINFOSTATIC      g_ErrInfoStatic;

/** In the assembly file. */
extern "C" uint8_t          g_abSupHardReadWriteExecPage[PAGE_SIZE];

/** Whether we've patched our own LdrInitializeThunk or not.  We do this to
 * disable thread creation. */
static bool                 g_fSupInitThunkSelfPatched;
/** The backup of our own LdrInitializeThunk code, for enabling and disabling
 * thread creation in this process. */
static uint8_t              g_abLdrInitThunkSelfBackup[16];

/** Mask of adversaries that we've detected (SUPHARDNT_ADVERSARY_XXX). */
static uint32_t             g_fSupAdversaries = 0;
/** @name SUPHARDNT_ADVERSARY_XXX - Adversaries
 * @{ */
/** Symantec endpoint protection or similar including SysPlant.sys. */
#define SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT       RT_BIT_32(0)
/** Symantec Norton 360. */
#define SUPHARDNT_ADVERSARY_SYMANTEC_N360           RT_BIT_32(1)
/** Avast! */
#define SUPHARDNT_ADVERSARY_AVAST                   RT_BIT_32(2)
/** TrendMicro OfficeScan and probably others. */
#define SUPHARDNT_ADVERSARY_TRENDMICRO              RT_BIT_32(3)
/** TrendMicro potentially buggy sakfile.sys. */
#define SUPHARDNT_ADVERSARY_TRENDMICRO_SAKFILE      RT_BIT_32(4)
/** McAfee.  */
#define SUPHARDNT_ADVERSARY_MCAFEE                  RT_BIT_32(5)
/** Kaspersky or OEMs of it.  */
#define SUPHARDNT_ADVERSARY_KASPERSKY               RT_BIT_32(6)
/** Malwarebytes Anti-Malware (MBAM). */
#define SUPHARDNT_ADVERSARY_MBAM                    RT_BIT_32(7)
/** AVG Internet Security. */
#define SUPHARDNT_ADVERSARY_AVG                     RT_BIT_32(8)
/** Panda Security. */
#define SUPHARDNT_ADVERSARY_PANDA                   RT_BIT_32(9)
/** Microsoft Security Essentials. */
#define SUPHARDNT_ADVERSARY_MSE                     RT_BIT_32(10)
/** Comodo. */
#define SUPHARDNT_ADVERSARY_COMODO                  RT_BIT_32(11)
/** Check Point's Zone Alarm (may include Kaspersky).  */
#define SUPHARDNT_ADVERSARY_ZONE_ALARM              RT_BIT_32(12)
/** Digital guardian, old problematic version.  */
#define SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD    RT_BIT_32(13)
/** Digital guardian, new version.  */
#define SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_NEW    RT_BIT_32(14)
/** Cylance protect or something (from googling, no available sample copy). */
#define SUPHARDNT_ADVERSARY_CYLANCE                 RT_BIT_32(15)
/** BeyondTrust / PowerBroker / something (googling, no available sample copy). */
#define SUPHARDNT_ADVERSARY_BEYONDTRUST             RT_BIT_32(16)
/** Avecto / Defendpoint / Privilege Guard (details from support guy, hoping to get sample copy). */
#define SUPHARDNT_ADVERSARY_AVECTO                  RT_BIT_32(17)
/** Sophos Endpoint Defense. */
#define SUPHARDNT_ADVERSARY_SOPHOS                  RT_BIT_32(18)
/** VMware horizon view agent. */
#define SUPHARDNT_ADVERSARY_HORIZON_VIEW_AGENT      RT_BIT_32(19)
/** Unknown adversary detected while waiting on child. */
#define SUPHARDNT_ADVERSARY_UNKNOWN                 RT_BIT_32(31)
/** @} */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static NTSTATUS supR3HardenedScreenImage(HANDLE hFile, bool fImage, bool fIgnoreArch, PULONG pfAccess, PULONG pfProtect,
                                         bool *pfCallRealApi, const char *pszCaller, bool fAvoidWinVerifyTrust,
                                         bool *pfQuiet) RT_NOTHROW_PROTO;
static void     supR3HardenedWinRegisterDllNotificationCallback(void);
static void     supR3HardenedWinReInstallHooks(bool fFirst) RT_NOTHROW_PROTO;
DECLASM(void)   supR3HardenedEarlyProcessInitThunk(void);
DECLASM(void)   supR3HardenedMonitor_KiUserApcDispatcher(void);
#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
DECLASM(void)   supR3HardenedMonitor_KiUserExceptionDispatcher(void);
#endif
extern "C" void __stdcall suplibHardenedWindowsMain(void);


#if 0 /* unused */

/**
 * Simple wide char search routine.
 *
 * @returns Pointer to the first location of @a wcNeedle in @a pwszHaystack.
 *          NULL if not found.
 * @param   pwszHaystack    Pointer to the string that should be searched.
 * @param   wcNeedle        The character to search for.
 */
static PRTUTF16 suplibHardenedWStrChr(PCRTUTF16 pwszHaystack, RTUTF16 wcNeedle)
{
    for (;;)
    {
        RTUTF16 wcCur = *pwszHaystack;
        if (wcCur == wcNeedle)
            return (PRTUTF16)pwszHaystack;
        if (wcCur == '\0')
            return NULL;
        pwszHaystack++;
    }
}


/**
 * Simple wide char string length routine.
 *
 * @returns The number of characters in the given string. (Excludes the
 *          terminator.)
 * @param   pwsz            The string.
 */
static size_t suplibHardenedWStrLen(PCRTUTF16 pwsz)
{
    PCRTUTF16 pwszCur = pwsz;
    while (*pwszCur != '\0')
        pwszCur++;
    return pwszCur - pwsz;
}

#endif /* unused */


/**
 * Our version of GetTickCount.
 * @returns Millisecond timestamp.
 */
static uint64_t supR3HardenedWinGetMilliTS(void)
{
    PKUSER_SHARED_DATA pUserSharedData = (PKUSER_SHARED_DATA)(uintptr_t)0x7ffe0000;

    /* use interrupt time */
    LARGE_INTEGER Time;
    do
    {
        Time.HighPart = pUserSharedData->InterruptTime.High1Time;
        Time.LowPart  = pUserSharedData->InterruptTime.LowPart;
    } while (pUserSharedData->InterruptTime.High2Time != Time.HighPart);

    return (uint64_t)Time.QuadPart / 10000;
}


/**
 * Called when there is some /GS (or maybe /RTCsu) related stack problem.
 *
 * We don't want the CRT version living in gshandle.obj, as it uses a lot of
 * kernel32 imports, we want to report this error ourselves.
 */
extern "C" __declspec(noreturn guard(nosspro) guard(nossepi))
void __cdecl __report_rangecheckfailure(void)
{
    supR3HardenedFatal("__report_rangecheckfailure called from %p", ASMReturnAddress());
}


/**
 * Called when there is some /GS problem has been detected.
 *
 * We don't want the CRT version living in gshandle.obj, as it uses a lot of
 * kernel32 imports, we want to report this error ourselves.
 */
extern "C" __declspec(noreturn guard(nosspro) guard(nossepi))
#ifdef RT_ARCH_X86
void __cdecl __report_gsfailure(void)
#else
void __report_gsfailure(uintptr_t uCookie)
#endif
{
#ifdef RT_ARCH_X86
    supR3HardenedFatal("__report_gsfailure called from %p", ASMReturnAddress());
#else
    supR3HardenedFatal("__report_gsfailure called from %p, cookie=%p", ASMReturnAddress(), uCookie);
#endif
}


/**
 * Wrapper around LoadLibraryEx that deals with the UTF-8 to UTF-16 conversion
 * and supplies the right flags.
 *
 * @returns Module handle on success, NULL on failure.
 * @param   pszName             The full path to the DLL.
 * @param   fSystem32Only       Whether to only look for imports in the system32
 *                              directory.  If set to false, the application
 *                              directory is also searched.
 * @param   fMainFlags          The main flags (giving the location), if the DLL
 *                              being loaded is loaded from the app bin
 *                              directory and import other DLLs from there. Pass
 *                              0 (= SUPSECMAIN_FLAGS_LOC_APP_BIN) if not
 *                              applicable.  Ignored if @a fSystem32Only is set.
 *
 *                              This is only needed to load VBoxRT.dll when
 *                              executing a testcase from the testcase/ subdir.
 */
DECLHIDDEN(void *) supR3HardenedWinLoadLibrary(const char *pszName, bool fSystem32Only, uint32_t fMainFlags)
{
    WCHAR wszPath[RTPATH_MAX];
    PRTUTF16 pwszPath = wszPath;
    int rc = RTStrToUtf16Ex(pszName, RTSTR_MAX, &pwszPath, RT_ELEMENTS(wszPath), NULL);
    if (RT_SUCCESS(rc))
    {
        while (*pwszPath)
        {
            if (*pwszPath == '/')
                *pwszPath = '\\';
            pwszPath++;
        }

        DWORD fFlags = 0;
        if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0))
        {
            fFlags |= LOAD_LIBRARY_SEARCH_SYSTEM32;
            if (!fSystem32Only)
            {
                fFlags |= LOAD_LIBRARY_SEARCH_APPLICATION_DIR;
                if (g_fSupLibHardenedDllSearchUserDirs)
                    fFlags |= LOAD_LIBRARY_SEARCH_USER_DIRS;
                if ((fMainFlags & SUPSECMAIN_FLAGS_LOC_MASK) != SUPSECMAIN_FLAGS_LOC_APP_BIN)
                    fFlags |= LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
            }
        }

        void *pvRet = (void *)LoadLibraryExW(wszPath, NULL /*hFile*/, fFlags);

        /* Vista, W7, W2K8R might not work without KB2533623, so retry with no flags. */
        if (   !pvRet
            && fFlags
            && g_uNtVerCombined < SUP_MAKE_NT_VER_SIMPLE(6, 2)
            && RtlGetLastWin32Error() == ERROR_INVALID_PARAMETER)
            pvRet = (void *)LoadLibraryExW(wszPath, NULL /*hFile*/, 0);

        return pvRet;
    }
    supR3HardenedFatal("RTStrToUtf16Ex failed on '%s': %Rrc", pszName, rc);
    /* not reached */
}


/**
 * Gets the internal index number of the file.
 *
 * @returns True if we got an index number, false if not.
 * @param   hFile           The file in question.
 * @param   pIndexNumber    where to return the index number.
 */
static bool supR3HardenedWinVerifyCacheGetIndexNumber(HANDLE hFile, PLARGE_INTEGER pIndexNumber) RT_NOTHROW_DEF
{
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtQueryInformationFile(hFile, &Ios, pIndexNumber, sizeof(*pIndexNumber), FileInternalInformation);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
#ifdef DEBUG_bird
    if (!NT_SUCCESS(rcNt))
        __debugbreak();
#endif
    return NT_SUCCESS(rcNt) && pIndexNumber->QuadPart != 0;
}


/**
 * Calculates the hash value for the given UTF-16 path string.
 *
 * @returns Hash value.
 * @param   pUniStr             String to hash.
 */
static uint32_t supR3HardenedWinVerifyCacheHashPath(PCUNICODE_STRING pUniStr) RT_NOTHROW_DEF
{
    uint32_t uHash   = 0;
    unsigned cwcLeft = pUniStr->Length / sizeof(WCHAR);
    PRTUTF16 pwc     = pUniStr->Buffer;

    while (cwcLeft-- > 0)
    {
        RTUTF16 wc = *pwc++;
        if (wc < 0x80)
            wc = wc != '/' ? RT_C_TO_LOWER(wc) : '\\';
        uHash = wc + (uHash << 6) + (uHash << 16) - uHash;
    }
    return uHash;
}


/**
 * Calculates the hash value for a directory + filename combo as if they were
 * one single string.
 *
 * @returns Hash value.
 * @param   pawcDir             The directory name.
 * @param   cwcDir              The length of the directory name. RTSTR_MAX if
 *                              not available.
 * @param   pszName             The import name (UTF-8).
 */
static uint32_t supR3HardenedWinVerifyCacheHashDirAndFile(PCRTUTF16 pawcDir, uint32_t cwcDir, const char *pszName) RT_NOTHROW_DEF
{
    uint32_t uHash = 0;
    while (cwcDir-- > 0)
    {
        RTUTF16 wc = *pawcDir++;
        if (wc < 0x80)
            wc = wc != '/' ? RT_C_TO_LOWER(wc) : '\\';
        uHash = wc + (uHash << 6) + (uHash << 16) - uHash;
    }

    unsigned char ch = '\\';
    uHash = ch + (uHash << 6) + (uHash << 16) - uHash;

    while ((ch = *pszName++) != '\0')
    {
        ch = RT_C_TO_LOWER(ch);
        uHash = ch + (uHash << 6) + (uHash << 16) - uHash;
    }

    return uHash;
}


/**
 * Verify string cache compare function.
 *
 * @returns true if the strings match, false if not.
 * @param   pawcLeft            The left hand string.
 * @param   pawcRight           The right hand string.
 * @param   cwcToCompare        The number of chars to compare.
 */
static bool supR3HardenedWinVerifyCacheIsMatch(PCRTUTF16 pawcLeft, PCRTUTF16 pawcRight, uint32_t cwcToCompare) RT_NOTHROW_DEF
{
    /* Try a quick memory compare first. */
    if (memcmp(pawcLeft, pawcRight, cwcToCompare * sizeof(RTUTF16)) == 0)
        return true;

    /* Slow char by char compare. */
    while (cwcToCompare-- > 0)
    {
        RTUTF16 wcLeft  = *pawcLeft++;
        RTUTF16 wcRight = *pawcRight++;
        if (wcLeft != wcRight)
        {
            wcLeft  = wcLeft  != '/' ? RT_C_TO_LOWER(wcLeft)  : '\\';
            wcRight = wcRight != '/' ? RT_C_TO_LOWER(wcRight) : '\\';
            if (wcLeft != wcRight)
                return false;
        }
    }

    return true;
}



/**
 * Inserts the given verifier result into the cache.
 *
 * @param   pUniStr             The full path of the image.
 * @param   hFile               The file handle - must either be entered into
 *                              the cache or closed.
 * @param   rc                  The verifier result.
 * @param   fWinVerifyTrust     Whether verified by WinVerifyTrust or not.
 * @param   fFlags              The image verification flags.
 */
static void supR3HardenedWinVerifyCacheInsert(PCUNICODE_STRING pUniStr, HANDLE hFile, int rc,
                                              bool fWinVerifyTrust, uint32_t fFlags) RT_NOTHROW_DEF
{
    /*
     * Allocate and initalize a new entry.
     */
    PVERIFIERCACHEENTRY pEntry = (PVERIFIERCACHEENTRY)RTMemAllocZ(sizeof(VERIFIERCACHEENTRY) + pUniStr->Length);
    if (pEntry)
    {
        pEntry->pNext           = NULL;
        pEntry->pNextTodoWvt    = NULL;
        pEntry->hFile           = hFile;
        pEntry->uHash           = supR3HardenedWinVerifyCacheHashPath(pUniStr);
        pEntry->rc              = rc;
        pEntry->fFlags          = fFlags;
        pEntry->cHits           = 0;
        pEntry->fWinVerifyTrust = fWinVerifyTrust;
        pEntry->cbPath          = pUniStr->Length;
        memcpy(pEntry->wszPath, pUniStr->Buffer, pUniStr->Length);
        pEntry->wszPath[pUniStr->Length / sizeof(WCHAR)] = '\0';
        pEntry->fIndexNumberValid = supR3HardenedWinVerifyCacheGetIndexNumber(hFile, &pEntry->IndexNumber);

        /*
         * Try insert it, careful with concurrent code as well as potential duplicates.
         */
        uint32_t iHashTab = pEntry->uHash % RT_ELEMENTS(g_apVerifierCache);
        VERIFIERCACHEENTRY * volatile *ppEntry = &g_apVerifierCache[iHashTab];
        for (;;)
        {
            if (ASMAtomicCmpXchgPtr(ppEntry, pEntry, NULL))
            {
                if (!fWinVerifyTrust)
                    do
                        pEntry->pNextTodoWvt = g_pVerifierCacheTodoWvt;
                    while (!ASMAtomicCmpXchgPtr(&g_pVerifierCacheTodoWvt, pEntry, pEntry->pNextTodoWvt));

                SUP_DPRINTF(("supR3HardenedWinVerifyCacheInsert: %ls\n", pUniStr->Buffer));
                return;
            }

            PVERIFIERCACHEENTRY pOther = *ppEntry;
            if (!pOther)
                continue;
            if (   pOther->uHash  == pEntry->uHash
                && pOther->cbPath == pEntry->cbPath
                && supR3HardenedWinVerifyCacheIsMatch(pOther->wszPath, pEntry->wszPath, pEntry->cbPath / sizeof(RTUTF16)))
                break;
            ppEntry = &pOther->pNext;
        }

        /* Duplicate entry (may happen due to races). */
        RTMemFree(pEntry);
    }
    NtClose(hFile);
}


/**
 * Looks up an entry in the verifier hash table.
 *
 * @return  Pointer to the entry on if found, NULL if not.
 * @param   pUniStr             The full path of the image.
 * @param   hFile               The file handle.
 */
static PVERIFIERCACHEENTRY supR3HardenedWinVerifyCacheLookup(PCUNICODE_STRING pUniStr, HANDLE hFile) RT_NOTHROW_DEF
{
    PRTUTF16 const      pwszPath = pUniStr->Buffer;
    uint16_t const      cbPath   = pUniStr->Length;
    uint32_t            uHash    = supR3HardenedWinVerifyCacheHashPath(pUniStr);
    uint32_t            iHashTab = uHash % RT_ELEMENTS(g_apVerifierCache);
    PVERIFIERCACHEENTRY pCur     = g_apVerifierCache[iHashTab];
    while (pCur)
    {
        if (   pCur->uHash  == uHash
            && pCur->cbPath == cbPath
            && supR3HardenedWinVerifyCacheIsMatch(pCur->wszPath, pwszPath, cbPath / sizeof(RTUTF16)))
        {

            if (!pCur->fIndexNumberValid)
                return pCur;
            LARGE_INTEGER IndexNumber;
            bool fIndexNumberValid = supR3HardenedWinVerifyCacheGetIndexNumber(hFile, &IndexNumber);
            if (   fIndexNumberValid
                && IndexNumber.QuadPart == pCur->IndexNumber.QuadPart)
                return pCur;
#ifdef DEBUG_bird
            __debugbreak();
#endif
        }
        pCur = pCur->pNext;
    }
    return NULL;
}


/**
 * Looks up an import DLL in the verifier hash table.
 *
 * @return  Pointer to the entry on if found, NULL if not.
 * @param   pawcDir             The directory name.
 * @param   cwcDir              The length of the directory name.
 * @param   pszName             The import name (UTF-8).
 */
static PVERIFIERCACHEENTRY supR3HardenedWinVerifyCacheLookupImport(PCRTUTF16 pawcDir, uint32_t cwcDir, const char *pszName)
{
    uint32_t            uHash    = supR3HardenedWinVerifyCacheHashDirAndFile(pawcDir, cwcDir, pszName);
    uint32_t            iHashTab = uHash % RT_ELEMENTS(g_apVerifierCache);
    uint32_t const      cbPath   = (uint32_t)((cwcDir + 1 + strlen(pszName)) * sizeof(RTUTF16));
    PVERIFIERCACHEENTRY pCur     = g_apVerifierCache[iHashTab];
    while (pCur)
    {
        if (   pCur->uHash  == uHash
            && pCur->cbPath == cbPath)
        {
            if (supR3HardenedWinVerifyCacheIsMatch(pCur->wszPath, pawcDir, cwcDir))
            {
                if (pCur->wszPath[cwcDir] == '\\' || pCur->wszPath[cwcDir] == '/')
                {
                    if (RTUtf16ICmpAscii(&pCur->wszPath[cwcDir + 1], pszName))
                    {
                        return pCur;
                    }
                }
            }
        }

        pCur = pCur->pNext;
    }
    return NULL;
}


/**
 * Schedules the import DLLs for verification and entry into the cache.
 *
 * @param   hLdrMod             The loader module which imports should be
 *                              scheduled for verification.
 * @param   pwszName            The full NT path of the module.
 */
DECLHIDDEN(void) supR3HardenedWinVerifyCacheScheduleImports(RTLDRMOD hLdrMod, PCRTUTF16 pwszName)
{
    /*
     * Any imports?
     */
    uint32_t cImports;
    int rc = RTLdrQueryPropEx(hLdrMod, RTLDRPROP_IMPORT_COUNT, NULL /*pvBits*/, &cImports, sizeof(cImports), NULL);
    if (RT_SUCCESS(rc))
    {
        if (cImports)
        {
            /*
             * Figure out the DLL directory from pwszName.
             */
            PCRTUTF16 pawcDir = pwszName;
            uint32_t  cwcDir = 0;
            uint32_t  i = 0;
            RTUTF16   wc;
            while ((wc = pawcDir[i++]) != '\0')
                if ((wc == '\\' || wc == '/' || wc == ':') && cwcDir + 2 != i)
                    cwcDir = i - 1;
            if (   g_System32NtPath.UniStr.Length / sizeof(WCHAR) == cwcDir
                && supR3HardenedWinVerifyCacheIsMatch(pawcDir, g_System32NtPath.UniStr.Buffer, cwcDir))
                pawcDir = NULL;

            /*
             * Enumerate the imports.
             */
            for (i = 0; i < cImports; i++)
            {
                union
                {
                    char        szName[256];
                    uint32_t    iImport;
                } uBuf;
                uBuf.iImport = i;
                rc = RTLdrQueryPropEx(hLdrMod, RTLDRPROP_IMPORT_MODULE, NULL /*pvBits*/, &uBuf, sizeof(uBuf), NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Skip kernel32, ntdll and API set stuff.
                     */
                    RTStrToLower(uBuf.szName);
                    if (   RTStrCmp(uBuf.szName, "kernel32.dll") == 0
                        || RTStrCmp(uBuf.szName, "kernelbase.dll") == 0
                        || RTStrCmp(uBuf.szName, "ntdll.dll") == 0
                        || RTStrNCmp(uBuf.szName, RT_STR_TUPLE("api-ms-win-")) == 0
                        || RTStrNCmp(uBuf.szName, RT_STR_TUPLE("ext-ms-win-")) == 0
                       )
                    {
                        continue;
                    }

                    /*
                     * Skip to the next one if it's already in the cache.
                     */
                    if (supR3HardenedWinVerifyCacheLookupImport(g_System32NtPath.UniStr.Buffer,
                                                                g_System32NtPath.UniStr.Length / sizeof(WCHAR),
                                                                uBuf.szName) != NULL)
                    {
                        SUP_DPRINTF(("supR3HardenedWinVerifyCacheScheduleImports: '%s' cached for system32\n", uBuf.szName));
                        continue;
                    }
                    if (supR3HardenedWinVerifyCacheLookupImport(g_SupLibHardenedAppBinNtPath.UniStr.Buffer,
                                                                g_SupLibHardenedAppBinNtPath.UniStr.Length / sizeof(CHAR),
                                                                uBuf.szName) != NULL)
                    {
                        SUP_DPRINTF(("supR3HardenedWinVerifyCacheScheduleImports: '%s' cached for appdir\n", uBuf.szName));
                        continue;
                    }
                    if (pawcDir && supR3HardenedWinVerifyCacheLookupImport(pawcDir, cwcDir, uBuf.szName) != NULL)
                    {
                        SUP_DPRINTF(("supR3HardenedWinVerifyCacheScheduleImports: '%s' cached for dll dir\n", uBuf.szName));
                        continue;
                    }

                    /* We could skip already scheduled modules, but that'll require serialization and extra work... */

                    /*
                     * Add it to the todo list.
                     */
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheScheduleImports: Import todo: #%u '%s'.\n", i, uBuf.szName));
                    uint32_t cbName        = (uint32_t)strlen(uBuf.szName) + 1;
                    uint32_t cbNameAligned = RT_ALIGN_32(cbName, sizeof(RTUTF16));
                    uint32_t cbNeeded      = RT_UOFFSETOF_DYN(VERIFIERCACHEIMPORT, szName[cbNameAligned])
                                           + (pawcDir ? (cwcDir + 1) * sizeof(RTUTF16) : 0);
                    PVERIFIERCACHEIMPORT pImport = (PVERIFIERCACHEIMPORT)RTMemAllocZ(cbNeeded);
                    if (pImport)
                    {
                        /* Init it. */
                        memcpy(pImport->szName, uBuf.szName, cbName);
                        if (!pawcDir)
                        {
                            pImport->cwcAltSearchDir  = 0;
                            pImport->pwszAltSearchDir = NULL;
                        }
                        else
                        {
                            pImport->cwcAltSearchDir = cwcDir;
                            pImport->pwszAltSearchDir = (PRTUTF16)&pImport->szName[cbNameAligned];
                            memcpy(pImport->pwszAltSearchDir, pawcDir, cwcDir * sizeof(RTUTF16));
                            pImport->pwszAltSearchDir[cwcDir] = '\0';
                        }

                        /* Insert it. */
                        do
                            pImport->pNext = g_pVerifierCacheTodoImports;
                        while (!ASMAtomicCmpXchgPtr(&g_pVerifierCacheTodoImports, pImport, pImport->pNext));
                    }
                }
                else
                    SUP_DPRINTF(("RTLDRPROP_IMPORT_MODULE failed with rc=%Rrc i=%#x on '%ls'\n", rc, i, pwszName));
            }
        }
        else
            SUP_DPRINTF(("'%ls' has no imports\n", pwszName));
    }
    else
        SUP_DPRINTF(("RTLDRPROP_IMPORT_COUNT failed with rc=%Rrc on '%ls'\n", rc, pwszName));
}


/**
 * Processes the list of import todos.
 */
static void supR3HardenedWinVerifyCacheProcessImportTodos(void)
{
    /*
     * Work until we've got nothing more todo.
     */
    for (;;)
    {
        PVERIFIERCACHEIMPORT pTodo = ASMAtomicXchgPtrT(&g_pVerifierCacheTodoImports, NULL, PVERIFIERCACHEIMPORT);
        if (!pTodo)
            break;
        do
        {
            PVERIFIERCACHEIMPORT pCur = pTodo;
            pTodo = pTodo->pNext;

            /*
             * Not in the cached already?
             */
            if (   !supR3HardenedWinVerifyCacheLookupImport(g_System32NtPath.UniStr.Buffer,
                                                            g_System32NtPath.UniStr.Length / sizeof(WCHAR),
                                                            pCur->szName)
                && !supR3HardenedWinVerifyCacheLookupImport(g_SupLibHardenedAppBinNtPath.UniStr.Buffer,
                                                            g_SupLibHardenedAppBinNtPath.UniStr.Length / sizeof(WCHAR),
                                                            pCur->szName)
                && (   pCur->cwcAltSearchDir == 0
                    || !supR3HardenedWinVerifyCacheLookupImport(pCur->pwszAltSearchDir, pCur->cwcAltSearchDir, pCur->szName)) )
            {
                /*
                 * Try locate the imported DLL and open it.
                 */
                SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: Processing '%s'...\n", pCur->szName));

                NTSTATUS    rcNt;
                NTSTATUS    rcNtRedir = 0x22222222;
                HANDLE      hFile = INVALID_HANDLE_VALUE;
                RTUTF16     wszPath[260 + 260]; /* Assumes we've limited the import name length to 256. */
                AssertCompile(sizeof(wszPath) > sizeof(g_System32NtPath));

                /*
                 * Check for DLL isolation / redirection / mapping.
                 */
                size_t      cwcName  = 260;
                PRTUTF16    pwszName = &wszPath[0];
                int rc = RTStrToUtf16Ex(pCur->szName, RTSTR_MAX, &pwszName, cwcName, &cwcName);
                if (RT_SUCCESS(rc))
                {
                    UNICODE_STRING UniStrName;
                    UniStrName.Buffer = wszPath;
                    UniStrName.Length = (USHORT)cwcName * sizeof(WCHAR);
                    UniStrName.MaximumLength = UniStrName.Length + sizeof(WCHAR);

                    UNICODE_STRING UniStrStatic;
                    UniStrStatic.Buffer = &wszPath[cwcName + 1];
                    UniStrStatic.Length = 0;
                    UniStrStatic.MaximumLength = (USHORT)(sizeof(wszPath) - cwcName * sizeof(WCHAR) - sizeof(WCHAR));

                    static UNICODE_STRING const s_DefaultSuffix = RTNT_CONSTANT_UNISTR(L".dll");
                    UNICODE_STRING  UniStrDynamic = { 0, 0, NULL };
                    PUNICODE_STRING pUniStrResult = NULL;

                    rcNtRedir = RtlDosApplyFileIsolationRedirection_Ustr(1 /*fFlags*/,
                                                                         &UniStrName,
                                                                         (PUNICODE_STRING)&s_DefaultSuffix,
                                                                         &UniStrStatic,
                                                                         &UniStrDynamic,
                                                                         &pUniStrResult,
                                                                         NULL /*pNewFlags*/,
                                                                         NULL /*pcbFilename*/,
                                                                         NULL /*pcbNeeded*/);
                    if (NT_SUCCESS(rcNtRedir))
                    {
                        IO_STATUS_BLOCK     Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                        OBJECT_ATTRIBUTES   ObjAttr;
                        InitializeObjectAttributes(&ObjAttr, pUniStrResult,
                                                   OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
                        rcNt = NtCreateFile(&hFile,
                                            FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                                            &ObjAttr,
                                            &Ios,
                                            NULL /* Allocation Size*/,
                                            FILE_ATTRIBUTE_NORMAL,
                                            FILE_SHARE_READ,
                                            FILE_OPEN,
                                            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                            NULL /*EaBuffer*/,
                                            0 /*EaLength*/);
                        if (NT_SUCCESS(rcNt))
                            rcNt = Ios.Status;
                        if (NT_SUCCESS(rcNt))
                        {
                            /* For accurate logging. */
                            size_t cwcCopy = RT_MIN(pUniStrResult->Length / sizeof(RTUTF16), RT_ELEMENTS(wszPath) - 1);
                            memcpy(wszPath, pUniStrResult->Buffer, cwcCopy * sizeof(RTUTF16));
                            wszPath[cwcCopy] = '\0';
                        }
                        else
                            hFile = INVALID_HANDLE_VALUE;
                        RtlFreeUnicodeString(&UniStrDynamic);
                    }
                }
                else
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: RTStrToUtf16Ex #1 failed: %Rrc\n", rc));

                /*
                 * If not something that gets remapped, do the half normal searching we need.
                 */
                if (hFile == INVALID_HANDLE_VALUE)
                {
                    struct
                    {
                        PRTUTF16 pawcDir;
                        uint32_t cwcDir;
                    } Tmp, aDirs[] =
                    {
                        { g_System32NtPath.UniStr.Buffer,           g_System32NtPath.UniStr.Length / sizeof(WCHAR) },
                        { g_SupLibHardenedExeNtPath.UniStr.Buffer,  g_SupLibHardenedAppBinNtPath.UniStr.Length / sizeof(WCHAR) },
                        { pCur->pwszAltSearchDir,                   pCur->cwcAltSearchDir },
                    };

                    /* Search System32 first, unless it's a 'V*' or 'm*' name, the latter for msvcrt.  */
                    if (   pCur->szName[0] == 'v'
                        || pCur->szName[0] == 'V'
                        || pCur->szName[0] == 'm'
                        || pCur->szName[0] == 'M')
                    {
                        Tmp      = aDirs[0];
                        aDirs[0] = aDirs[1];
                        aDirs[1] = Tmp;
                    }

                    for (uint32_t i = 0; i < RT_ELEMENTS(aDirs); i++)
                    {
                        if (aDirs[i].pawcDir && aDirs[i].cwcDir && aDirs[i].cwcDir < RT_ELEMENTS(wszPath) / 3 * 2)
                        {
                            memcpy(wszPath, aDirs[i].pawcDir, aDirs[i].cwcDir * sizeof(RTUTF16));
                            uint32_t cwc = aDirs[i].cwcDir;
                            wszPath[cwc++] = '\\';
                            cwcName  = RT_ELEMENTS(wszPath) - cwc;
                            pwszName = &wszPath[cwc];
                            rc = RTStrToUtf16Ex(pCur->szName, RTSTR_MAX, &pwszName, cwcName, &cwcName);
                            if (RT_SUCCESS(rc))
                            {
                                IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                                UNICODE_STRING      NtName;
                                NtName.Buffer        = wszPath;
                                NtName.Length        = (USHORT)((cwc + cwcName) * sizeof(WCHAR));
                                NtName.MaximumLength = NtName.Length + sizeof(WCHAR);
                                OBJECT_ATTRIBUTES   ObjAttr;
                                InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

                                rcNt = NtCreateFile(&hFile,
                                                    FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                                                    &ObjAttr,
                                                    &Ios,
                                                    NULL /* Allocation Size*/,
                                                    FILE_ATTRIBUTE_NORMAL,
                                                    FILE_SHARE_READ,
                                                    FILE_OPEN,
                                                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                                    NULL /*EaBuffer*/,
                                                    0 /*EaLength*/);
                                if (NT_SUCCESS(rcNt))
                                    rcNt = Ios.Status;
                                if (NT_SUCCESS(rcNt))
                                    break;
                                hFile = INVALID_HANDLE_VALUE;
                            }
                            else
                                SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: RTStrToUtf16Ex #2 failed: %Rrc\n", rc));
                        }
                    }
                }

                /*
                 * If we successfully opened it, verify it and cache the result.
                 */
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: '%s' -> '%ls' [rcNtRedir=%#x]\n",
                                 pCur->szName, wszPath, rcNtRedir));

                    ULONG fAccess = 0;
                    ULONG fProtect = 0;
                    bool  fCallRealApi = false;
                    rcNt = supR3HardenedScreenImage(hFile, true /*fImage*/, false /*fIgnoreArch*/, &fAccess, &fProtect,
                                                    &fCallRealApi, "Imports", false /*fAvoidWinVerifyTrust*/, NULL /*pfQuiet*/);
                    NtClose(hFile);
                }
                else
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: Failed to locate '%s'\n", pCur->szName));
            }
            else
                SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessImportTodos: '%s' is in the cache.\n", pCur->szName));

            RTMemFree(pCur);
        } while (pTodo);
    }
}


/**
 * Processes the list of WinVerifyTrust todos.
 */
static void supR3HardenedWinVerifyCacheProcessWvtTodos(void)
{
    PVERIFIERCACHEENTRY           pReschedule = NULL;
    PVERIFIERCACHEENTRY volatile *ppReschedLastNext = &pReschedule;

    /*
     * Work until we've got nothing more todo.
     */
    for (;;)
    {
        if (!supHardenedWinIsWinVerifyTrustCallable())
            break;
        PVERIFIERCACHEENTRY pTodo = ASMAtomicXchgPtrT(&g_pVerifierCacheTodoWvt, NULL, PVERIFIERCACHEENTRY);
        if (!pTodo)
            break;
        do
        {
            PVERIFIERCACHEENTRY pCur = pTodo;
            pTodo = pTodo->pNextTodoWvt;
            pCur->pNextTodoWvt = NULL;

            if (   !pCur->fWinVerifyTrust
                && RT_SUCCESS(pCur->rc))
            {
                bool fWinVerifyTrust = false;
                int rc = supHardenedWinVerifyImageTrust(pCur->hFile, pCur->wszPath, pCur->fFlags, pCur->rc,
                                                        &fWinVerifyTrust, NULL /* pErrInfo*/);
                if (RT_FAILURE(rc) || fWinVerifyTrust)
                {
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessWvtTodos: %d (was %d) fWinVerifyTrust=%d for '%ls'\n",
                                 rc, pCur->rc, fWinVerifyTrust, pCur->wszPath));
                    pCur->fWinVerifyTrust = true;
                    pCur->rc = rc;
                }
                else
                {
                    /* Retry it at a later time. */
                    SUP_DPRINTF(("supR3HardenedWinVerifyCacheProcessWvtTodos: %d (was %d) fWinVerifyTrust=%d for '%ls' [rescheduled]\n",
                                 rc, pCur->rc, fWinVerifyTrust, pCur->wszPath));
                    *ppReschedLastNext = pCur;
                    ppReschedLastNext = &pCur->pNextTodoWvt;
                }
            }
            /* else: already processed. */
        } while (pTodo);
    }

    /*
     * Anything to reschedule.
     */
    if (pReschedule)
    {
        do
            *ppReschedLastNext = g_pVerifierCacheTodoWvt;
        while (!ASMAtomicCmpXchgPtr(&g_pVerifierCacheTodoWvt, pReschedule, *ppReschedLastNext));
    }
}


/**
 * Translates VBox status code (from supHardenedWinVerifyImageTrust) to an NT
 * status.
 *
 * @returns NT status.
 * @param   rc                      VBox status code.
 */
static NTSTATUS supR3HardenedScreenImageCalcStatus(int rc) RT_NOTHROW_DEF
{
    /* This seems to be what LdrLoadDll returns when loading a 32-bit DLL into
       a 64-bit process.  At least here on windows 10 (2015-11-xx).

       NtCreateSection probably returns something different, possibly a warning,
       we currently don't distinguish between the too, so we stick with the
       LdrLoadDll one as it's definitely an error.*/
    if (rc == VERR_LDR_ARCH_MISMATCH)
        return STATUS_INVALID_IMAGE_FORMAT;

    return STATUS_TRUST_FAILURE;
}


/**
 * Screens an image file or file mapped with execute access.
 *
 * @returns NT status code.
 * @param   hFile                   The file handle.
 * @param   fImage                  Set if image file mapping being made
 *                                  (NtCreateSection thing).
 * @param   fIgnoreArch             Using the DONT_RESOLVE_DLL_REFERENCES flag,
 *                                  which also implies that DLL init / term code
 *                                  isn't called, so the architecture should be
 *                                  ignored.
 * @param   pfAccess                Pointer to the NtCreateSection access flags,
 *                                  so we can modify them if necessary.
 * @param   pfProtect               Pointer to the NtCreateSection protection
 *                                  flags, so we can modify them if necessary.
 * @param   pfCallRealApi           Whether it's ok to go on to the real API.
 * @param   pszCaller               Who is calling (for debugging / logging).
 * @param   fAvoidWinVerifyTrust    Whether we should avoid WinVerifyTrust.
 * @param   pfQuiet                 Where to return whether to be quiet about
 *                                  this image in the log (i.e. we've seen it
 *                                  lots of times already).  Optional.
 */
static NTSTATUS
supR3HardenedScreenImage(HANDLE hFile, bool fImage, bool fIgnoreArch, PULONG pfAccess, PULONG pfProtect,
                         bool *pfCallRealApi, const char *pszCaller, bool fAvoidWinVerifyTrust, bool *pfQuiet) RT_NOTHROW_DEF
{
    *pfCallRealApi = false;
    if (pfQuiet)
        *pfQuiet = false;

    /*
     * Query the name of the file, making sure to zero terminator the
     * string. (2nd half of buffer is used for error info, see below.)
     */
    union
    {
        UNICODE_STRING UniStr;
        uint8_t abBuffer[sizeof(UNICODE_STRING) + 2048 * sizeof(WCHAR)];
    } uBuf;
    RT_ZERO(uBuf);
    ULONG cbNameBuf;
    NTSTATUS rcNt = NtQueryObject(hFile, ObjectNameInformation, &uBuf, sizeof(uBuf) - sizeof(WCHAR) - 128, &cbNameBuf);
    if (!NT_SUCCESS(rcNt))
    {
        supR3HardenedError(VINF_SUCCESS, false,
                           "supR3HardenedScreenImage/%s: NtQueryObject -> %#x (fImage=%d fProtect=%#x fAccess=%#x)\n",
                           pszCaller, fImage, *pfProtect, *pfAccess);
        return rcNt;
    }

    if (!RTNtPathFindPossible8dot3Name(uBuf.UniStr.Buffer))
        cbNameBuf += sizeof(WCHAR);
    else
    {
        uBuf.UniStr.MaximumLength = sizeof(uBuf) - 128;
        RTNtPathExpand8dot3Path(&uBuf.UniStr, true /*fPathOnly*/);
        cbNameBuf = (uintptr_t)uBuf.UniStr.Buffer + uBuf.UniStr.Length + sizeof(WCHAR) - (uintptr_t)&uBuf.abBuffer[0];
    }

    /*
     * Check the cache.
     */
    PVERIFIERCACHEENTRY pCacheHit = supR3HardenedWinVerifyCacheLookup(&uBuf.UniStr, hFile);
    if (pCacheHit)
    {
        /* Do hit accounting and figure whether we need to be quiet or not. */
        uint32_t   cHits  = ASMAtomicIncU32(&pCacheHit->cHits);
        bool const fQuiet = cHits >= 8 && !RT_IS_POWER_OF_TWO(cHits);
        if (pfQuiet)
            *pfQuiet = fQuiet;

        /* If we haven't done the WinVerifyTrust thing, do it if we can. */
        if (   !pCacheHit->fWinVerifyTrust
            && RT_SUCCESS(pCacheHit->rc)
            && supHardenedWinIsWinVerifyTrustCallable() )
        {
            if (!fAvoidWinVerifyTrust)
            {
                SUP_DPRINTF(("supR3HardenedScreenImage/%s: cache hit (%Rrc) on %ls [redoing WinVerifyTrust]\n",
                             pszCaller, pCacheHit->rc, pCacheHit->wszPath));

                bool fWinVerifyTrust = false;
                int rc = supHardenedWinVerifyImageTrust(pCacheHit->hFile, pCacheHit->wszPath, pCacheHit->fFlags, pCacheHit->rc,
                                                        &fWinVerifyTrust, NULL /* pErrInfo*/);
                if (RT_FAILURE(rc) || fWinVerifyTrust)
                {
                    SUP_DPRINTF(("supR3HardenedScreenImage/%s: %d (was %d) fWinVerifyTrust=%d for '%ls'\n",
                                 pszCaller, rc, pCacheHit->rc, fWinVerifyTrust, pCacheHit->wszPath));
                    pCacheHit->fWinVerifyTrust = true;
                    pCacheHit->rc = rc;
                }
                else
                    SUP_DPRINTF(("supR3HardenedScreenImage/%s: WinVerifyTrust not available, rescheduling %ls\n",
                                 pszCaller, pCacheHit->wszPath));
            }
            else
                SUP_DPRINTF(("supR3HardenedScreenImage/%s: cache hit (%Rrc) on %ls [avoiding WinVerifyTrust]\n",
                             pszCaller, pCacheHit->rc, pCacheHit->wszPath));
        }
        else if (!fQuiet || !pCacheHit->fWinVerifyTrust)
            SUP_DPRINTF(("supR3HardenedScreenImage/%s: cache hit (%Rrc) on %ls%s\n",
                         pszCaller, pCacheHit->rc, pCacheHit->wszPath, pCacheHit->fWinVerifyTrust ? "" : " [lacks WinVerifyTrust]"));

        /* Return the cached value. */
        if (RT_SUCCESS(pCacheHit->rc))
        {
            *pfCallRealApi = true;
            return STATUS_SUCCESS;
        }

        if (!fQuiet)
            supR3HardenedError(VINF_SUCCESS, false,
                               "supR3HardenedScreenImage/%s: cached rc=%Rrc fImage=%d fProtect=%#x fAccess=%#x cHits=%u %ls\n",
                               pszCaller, pCacheHit->rc, fImage, *pfProtect, *pfAccess, cHits, uBuf.UniStr.Buffer);
        return supR3HardenedScreenImageCalcStatus(pCacheHit->rc);
    }

    /*
     * On XP the loader might hand us handles with just FILE_EXECUTE and
     * SYNCHRONIZE, the means reading will fail later on.  Also, we need
     * READ_CONTROL access to check the file ownership later on, and non
     * of the OS versions seems be giving us that.  So, in effect we
     * more or less always reopen the file here.
     */
    HANDLE hMyFile = NULL;
    rcNt = NtDuplicateObject(NtCurrentProcess(), hFile, NtCurrentProcess(),
                             &hMyFile,
                             FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                             0 /* Handle attributes*/, 0 /* Options */);
    if (!NT_SUCCESS(rcNt))
    {
        if (rcNt == STATUS_ACCESS_DENIED)
        {
            IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
            OBJECT_ATTRIBUTES   ObjAttr;
            InitializeObjectAttributes(&ObjAttr, &uBuf.UniStr, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

            rcNt = NtCreateFile(&hMyFile,
                                FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                                &ObjAttr,
                                &Ios,
                                NULL /* Allocation Size*/,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ,
                                FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                NULL /*EaBuffer*/,
                                0 /*EaLength*/);
            if (NT_SUCCESS(rcNt))
                rcNt = Ios.Status;
            if (!NT_SUCCESS(rcNt))
            {
                supR3HardenedError(VINF_SUCCESS, false,
                                   "supR3HardenedScreenImage/%s: Failed to duplicate and open the file: rcNt=%#x hFile=%p %ls\n",
                                   pszCaller, rcNt, hFile, uBuf.UniStr.Buffer);
                return rcNt;
            }

            /* Check that we've got the same file. */
            LARGE_INTEGER idMyFile, idInFile;
            bool fMyValid = supR3HardenedWinVerifyCacheGetIndexNumber(hMyFile, &idMyFile);
            bool fInValid = supR3HardenedWinVerifyCacheGetIndexNumber(hFile, &idInFile);
            if (   fMyValid
                && (   fMyValid != fInValid
                    || idMyFile.QuadPart != idInFile.QuadPart))
            {
                supR3HardenedError(VINF_SUCCESS, false,
                                   "supR3HardenedScreenImage/%s: Re-opened has different ID that input: %#llx vx %#llx (%ls)\n",
                                   pszCaller, rcNt, idMyFile.QuadPart, idInFile.QuadPart, uBuf.UniStr.Buffer);
                NtClose(hMyFile);
                return STATUS_TRUST_FAILURE;
            }
        }
        else
        {
            SUP_DPRINTF(("supR3HardenedScreenImage/%s: NtDuplicateObject -> %#x\n", pszCaller, rcNt));
#ifdef DEBUG

            supR3HardenedError(VINF_SUCCESS, false,
                               "supR3HardenedScreenImage/%s: NtDuplicateObject(,%#x,) failed: %#x\n", pszCaller, hFile, rcNt);
#endif
            hMyFile = hFile;
        }
    }

    /*
     * Special Kludge for Windows XP and W2K3 and their stupid attempts
     * at mapping a hidden XML file called c:\Windows\WindowsShell.Manifest
     * with executable access.  The image bit isn't set, fortunately.
     */
    if (   !fImage
        && uBuf.UniStr.Length > g_System32NtPath.UniStr.Length - sizeof(L"System32") + sizeof(WCHAR)
        && memcmp(uBuf.UniStr.Buffer, g_System32NtPath.UniStr.Buffer,
                  g_System32NtPath.UniStr.Length - sizeof(L"System32") + sizeof(WCHAR)) == 0)
    {
        PRTUTF16 pwszName = &uBuf.UniStr.Buffer[(g_System32NtPath.UniStr.Length - sizeof(L"System32") + sizeof(WCHAR)) / sizeof(WCHAR)];
        if (RTUtf16ICmpAscii(pwszName, "WindowsShell.Manifest") == 0)
        {
            /*
             * Drop all executable access to the mapping and let it continue.
             */
            SUP_DPRINTF(("supR3HardenedScreenImage/%s: Applying the drop-exec-kludge for '%ls'\n", pszCaller, uBuf.UniStr.Buffer));
            if (*pfAccess & SECTION_MAP_EXECUTE)
                *pfAccess = (*pfAccess & ~SECTION_MAP_EXECUTE) | SECTION_MAP_READ;
            if (*pfProtect & PAGE_EXECUTE)
                *pfProtect = (*pfProtect & ~PAGE_EXECUTE) | PAGE_READONLY;
            *pfProtect = (*pfProtect & ~UINT32_C(0xf0)) | ((*pfProtect & UINT32_C(0xe0)) >> 4);
            if (hMyFile != hFile)
                NtClose(hMyFile);
            *pfCallRealApi = true;
            return STATUS_SUCCESS;
        }
    }

#ifndef VBOX_PERMIT_EVEN_MORE
    /*
     * Check the path.  We don't allow DLLs to be loaded from just anywhere:
     *      1. System32      - normal code or cat signing, owner TrustedInstaller/Administrators/LocalSystem.
     *      2. WinSxS        - normal code or cat signing, owner TrustedInstaller/Administrators/LocalSystem.
     *      3. VirtualBox    - build with:
     *         - regular code signing cert: build cert code signing, owner TrustedInstaller/Administrators/LocalSystem.
     *         - kernel code signing cert:  kernel code signing and integrity checks.
     *      4. AppPatchDir   - normal code or cat signing, owner TrustedInstaller/Administrators/LocalSystem.
     *      5. Program Files - normal code or cat signing, owner TrustedInstaller/Administrators/LocalSystem.
     *      6. Common Files  - normal code or cat signing, owner TrustedInstaller/Administrators/LocalSystem.
     *      7. x86 variations of 4 & 5 - ditto.
     *
     * Note! VBOX_WITHOUT_KERNEL_CODE_SIGNING_CERT means the /IntegrityCheck does
     *       work as it doesn't seems like MS has come up with a generally accessible
     *       alternative to the expired kernel code signing scheme for using this
     *       securty enhancement.
     */
    uint32_t fFlags = 0;
    if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_System32NtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_WinSxSNtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_SupLibHardenedAppBinNtPath.UniStr, true /*fCheckSlash*/))
# ifdef VBOX_WITHOUT_WINDOWS_KERNEL_CODE_SIGNING_CERT
        /** @todo r=bird: See SUPHNTVI_F_REQUIRE_BUILD_CERT comment below (in the
         *        code that's actually used). */
        fFlags |= SUPHNTVI_F_REQUIRE_BUILD_CERT | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
# else
        fFlags |= SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING | SUPHNTVI_F_REQUIRE_SIGNATURE_ENFORCEMENT;
# endif
# ifdef VBOX_PERMIT_MORE
    else if (supHardViIsAppPatchDir(uBuf.UniStr.Buffer, uBuf.UniStr.Length / sizeof(WCHAR)))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_ProgramFilesNtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_CommonFilesNtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
#  ifdef RT_ARCH_AMD64
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_ProgramFilesX86NtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
    else if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_CommonFilesX86NtPath.UniStr, true /*fCheckSlash*/))
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
#  endif
# endif
# ifdef VBOX_PERMIT_VISUAL_STUDIO_PROFILING
    /* Hack to allow profiling our code with Visual Studio. */
    else if (   uBuf.UniStr.Length > sizeof(L"\\SamplingRuntime.dll")
             && memcmp(uBuf.UniStr.Buffer + (uBuf.UniStr.Length - sizeof(L"\\SamplingRuntime.dll") + sizeof(WCHAR)) / sizeof(WCHAR),
                       L"\\SamplingRuntime.dll", sizeof(L"\\SamplingRuntime.dll") - sizeof(WCHAR)) == 0 )
    {
        if (hMyFile != hFile)
            NtClose(hMyFile);
        *pfCallRealApi = true;
        return STATUS_SUCCESS;
    }
# endif
    else
    {
        supR3HardenedError(VINF_SUCCESS, false,
                           "supR3HardenedScreenImage/%s: Not a trusted location: '%ls' (fImage=%d fProtect=%#x fAccess=%#x)\n",
                            pszCaller, uBuf.UniStr.Buffer, fImage, *pfAccess, *pfProtect);
        if (hMyFile != hFile)
            NtClose(hMyFile);
        return STATUS_TRUST_FAILURE;
    }

#else  /* VBOX_PERMIT_EVEN_MORE */
    /*
     * Require trusted installer + some kind of signature on everything, except
     * for the VBox bits where we have extra requirements depending on the signing
     * certificate used:
     *         - regular code signing cert: build cert code signing, owner TrustedInstaller/Administrators/LocalSystem.
     *         - kernel code signing cert:  kernel code signing and integrity checks.
     */
    uint32_t fFlags = 0;
    if (supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &g_SupLibHardenedAppBinNtPath.UniStr, true /*fCheckSlash*/))
# ifdef VBOX_WITHOUT_WINDOWS_KERNEL_CODE_SIGNING_CERT
        /** @todo r=bird: Since extension packs are installed under
         * g_SupLibHardenedAppBinNtPath and I'm pretty sure that everything loaded into
         * a VBox VM process goes thru this validation step at DLL load time, this means
         * only we can now sign extension packs.
         *
         * I suspect we have to relax the signing restrictions on the ExtensionPacks
         * subdirectory to keep 3rd party extensions working.  */
        fFlags |= SUPHNTVI_F_REQUIRE_BUILD_CERT | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
# else
        fFlags |= SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING | SUPHNTVI_F_REQUIRE_SIGNATURE_ENFORCEMENT;
# endif
    else
        fFlags |= SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION | SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER;
#endif /* VBOX_PERMIT_EVEN_MORE */

    /*
     * Do the verification. For better error message we borrow what's
     * left of the path buffer for an RTERRINFO buffer.
     */
    if (fIgnoreArch)
        fFlags |= SUPHNTVI_F_IGNORE_ARCHITECTURE;
    RTERRINFO ErrInfo;
    RTErrInfoInit(&ErrInfo, (char *)&uBuf.abBuffer[cbNameBuf], sizeof(uBuf) - cbNameBuf);

    int  rc;
    bool fWinVerifyTrust = false;
    rc = supHardenedWinVerifyImageByHandle(hMyFile, uBuf.UniStr.Buffer, fFlags, fAvoidWinVerifyTrust, &fWinVerifyTrust, &ErrInfo);
    if (RT_FAILURE(rc))
    {
        supR3HardenedError(VINF_SUCCESS, false,
                           "supR3HardenedScreenImage/%s: rc=%Rrc fImage=%d fProtect=%#x fAccess=%#x %ls: %s\n",
                           pszCaller, rc, fImage, *pfAccess, *pfProtect, uBuf.UniStr.Buffer, ErrInfo.pszMsg);
        if (hMyFile != hFile)
            supR3HardenedWinVerifyCacheInsert(&uBuf.UniStr, hMyFile, rc, fWinVerifyTrust, fFlags);
        return supR3HardenedScreenImageCalcStatus(rc);
    }

    /*
     * Insert into the cache.
     */
    if (hMyFile != hFile)
        supR3HardenedWinVerifyCacheInsert(&uBuf.UniStr, hMyFile, rc, fWinVerifyTrust, fFlags);

    *pfCallRealApi = true;
    return STATUS_SUCCESS;
}


/**
 * Preloads a file into the verify cache if possible.
 *
 * This is used to avoid known cyclic LoadLibrary issues with WinVerifyTrust.
 *
 * @param   pwszName            The name of the DLL to verify.
 */
DECLHIDDEN(void) supR3HardenedWinVerifyCachePreload(PCRTUTF16 pwszName)
{
    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

    UNICODE_STRING      UniStr;
    UniStr.Buffer = (PWCHAR)pwszName;
    UniStr.Length = (USHORT)(RTUtf16Len(pwszName) * sizeof(WCHAR));
    UniStr.MaximumLength = UniStr.Length + sizeof(WCHAR);

    OBJECT_ATTRIBUTES   ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ,
                                 FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (!NT_SUCCESS(rcNt))
    {
        SUP_DPRINTF(("supR3HardenedWinVerifyCachePreload: Error %#x opening '%ls'.\n", rcNt, pwszName));
        return;
    }

    ULONG fAccess = 0;
    ULONG fProtect = 0;
    bool  fCallRealApi;
    //SUP_DPRINTF(("supR3HardenedWinVerifyCachePreload: scanning %ls\n", pwszName));
    supR3HardenedScreenImage(hFile, false, false /*fIgnoreArch*/, &fAccess, &fProtect, &fCallRealApi, "preload",
                             false /*fAvoidWinVerifyTrust*/, NULL /*pfQuiet*/);
    //SUP_DPRINTF(("supR3HardenedWinVerifyCachePreload: done %ls\n", pwszName));

    NtClose(hFile);
}



/**
 * Hook that monitors NtCreateSection calls.
 *
 * @returns NT status code.
 * @param   phSection           Where to return the section handle.
 * @param   fAccess             The desired access.
 * @param   pObjAttribs         The object attributes (optional).
 * @param   pcbSection          The section size (optional).
 * @param   fProtect            The max section protection.
 * @param   fAttribs            The section attributes.
 * @param   hFile               The file to create a section from (optional).
 */
__declspec(guard(ignore)) /* don't barf when calling g_pfnNtCreateSectionReal */
static NTSTATUS NTAPI
supR3HardenedMonitor_NtCreateSection(PHANDLE phSection, ACCESS_MASK fAccess, POBJECT_ATTRIBUTES pObjAttribs,
                                     PLARGE_INTEGER pcbSection, ULONG fProtect, ULONG fAttribs, HANDLE hFile)
{
    bool fNeedUncChecking = false;
    if (   hFile != NULL
        && hFile != INVALID_HANDLE_VALUE)
    {
        bool const fImage    = RT_BOOL(fAttribs & (SEC_IMAGE | SEC_PROTECTED_IMAGE));
        bool const fExecMap  = RT_BOOL(fAccess & SECTION_MAP_EXECUTE);
        bool const fExecProt = RT_BOOL(fProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_WRITECOPY
                                                   | PAGE_EXECUTE_READWRITE));
        if (fImage || fExecMap || fExecProt)
        {
            fNeedUncChecking = true;
            DWORD dwSavedLastError = RtlGetLastWin32Error();

            bool fCallRealApi;
            //SUP_DPRINTF(("supR3HardenedMonitor_NtCreateSection: 1\n"));
            NTSTATUS rcNt = supR3HardenedScreenImage(hFile, fImage, true /*fIgnoreArch*/, &fAccess, &fProtect, &fCallRealApi,
                                                     "NtCreateSection", true /*fAvoidWinVerifyTrust*/, NULL /*pfQuiet*/);
            //SUP_DPRINTF(("supR3HardenedMonitor_NtCreateSection: 2 rcNt=%#x fCallRealApi=%#x\n", rcNt, fCallRealApi));

            RtlRestoreLastWin32Error(dwSavedLastError);

            if (!NT_SUCCESS(rcNt))
                return rcNt;
            Assert(fCallRealApi);
            if (!fCallRealApi)
                return STATUS_TRUST_FAILURE;

        }
    }

    /*
     * Call checked out OK, call the original.
     */
    NTSTATUS rcNtReal = g_pfnNtCreateSectionReal(phSection, fAccess, pObjAttribs, pcbSection, fProtect, fAttribs, hFile);

    /*
     * Check that the image that got mapped bear some resemblance to the one that was
     * requested.  Apparently there are ways to trick the NT cache manager to map a
     * file different from hFile into memory using local UNC accesses.
     */
    if (   NT_SUCCESS(rcNtReal)
        && fNeedUncChecking)
    {
        DWORD dwSavedLastError = RtlGetLastWin32Error();

        bool fOkay = false;

        /* To get the name of the file backing the section, we unfortunately have to map it. */
        SIZE_T   cbView   = 0;
        PVOID    pvTmpMap = NULL;
        NTSTATUS rcNt = NtMapViewOfSection(*phSection, NtCurrentProcess(), &pvTmpMap, 0, 0, NULL /*poffSection*/, &cbView,
                                           ViewUnmap, MEM_TOP_DOWN, PAGE_EXECUTE);
        if (NT_SUCCESS(rcNt))
        {
            /* Query the name. */
            union
            {
                UNICODE_STRING  UniStr;
                RTUTF16         awcBuf[512];
            } uBuf;
            RT_ZERO(uBuf);
            SIZE_T   cbActual = 0;
            NTSTATUS rcNtQuery = NtQueryVirtualMemory(NtCurrentProcess(), pvTmpMap, MemorySectionName,
                                                      &uBuf, sizeof(uBuf) - sizeof(RTUTF16), &cbActual);

            /* Unmap the view. */
            rcNt = NtUnmapViewOfSection(NtCurrentProcess(), pvTmpMap);
            if (!NT_SUCCESS(rcNt))
                SUP_DPRINTF(("supR3HardenedMonitor_NtCreateSection: NtUnmapViewOfSection failed on %p (hSection=%p, hFile=%p) with %#x!\n",
                             pvTmpMap, *phSection, hFile, rcNt));

            /* Process the name query result. */
            if (NT_SUCCESS(rcNtQuery))
            {
                static UNICODE_STRING const s_UncPrefix = RTNT_CONSTANT_UNISTR(L"\\Device\\Mup");
                if (!supHardViUniStrPathStartsWithUniStr(&uBuf.UniStr, &s_UncPrefix, true /*fCheckSlash*/))
                    fOkay = true;
                else
                    supR3HardenedError(VINF_SUCCESS, false,
                                       "supR3HardenedMonitor_NtCreateSection: Image section with UNC path is not trusted: '%.*ls'\n",
                                       uBuf.UniStr.Length / sizeof(RTUTF16), uBuf.UniStr.Buffer);
            }
            else
                SUP_DPRINTF(("supR3HardenedMonitor_NtCreateSection: NtQueryVirtualMemory failed on %p (hFile=%p) with %#x -> STATUS_TRUST_FAILURE\n",
                             *phSection, hFile, rcNt));
        }
        else
            SUP_DPRINTF(("supR3HardenedMonitor_NtCreateSection: NtMapViewOfSection failed on %p (hFile=%p) with %#x -> STATUS_TRUST_FAILURE\n",
                         *phSection, hFile, rcNt));
        if (!fOkay)
        {
            NtClose(*phSection);
            *phSection = INVALID_HANDLE_VALUE;
            RtlRestoreLastWin32Error(dwSavedLastError);
            return STATUS_TRUST_FAILURE;
        }

        RtlRestoreLastWin32Error(dwSavedLastError);
    }
    return rcNtReal;
}


/**
 * Checks if the given name is a valid ApiSet name.
 *
 * This is only called on likely looking names.
 *
 * @returns true if ApiSet name, false if not.
 * @param   pName               The name to check out.
 */
static bool supR3HardenedIsApiSetDll(PUNICODE_STRING pName)
{
    /*
     * API added in Windows 8, or so they say.
     */
    if (ApiSetQueryApiSetPresence != NULL)
    {
        BOOLEAN fPresent = FALSE;
        NTSTATUS rcNt = ApiSetQueryApiSetPresence(pName, &fPresent);
        SUP_DPRINTF(("supR3HardenedIsApiSetDll: ApiSetQueryApiSetPresence(%.*ls) -> %#x, fPresent=%d\n",
                     pName->Length / sizeof(WCHAR), pName->Buffer, rcNt, fPresent));
        return fPresent != 0;
    }

    /*
     * Fallback needed for Windows 7.  Fortunately, there aren't too many fake DLLs here.
     */
    if (   g_uNtVerCombined >= SUP_NT_VER_W70
        && (   supHardViUtf16PathStartsWithEx(pName->Buffer, pName->Length / sizeof(WCHAR),
                                              L"api-ms-win-", 11, false /*fCheckSlash*/)
            || supHardViUtf16PathStartsWithEx(pName->Buffer, pName->Length / sizeof(WCHAR),
                                              L"ext-ms-win-", 11, false /*fCheckSlash*/) ))
    {
#define MY_ENTRY(a) { a, sizeof(a) - 1 }
        static const struct { const char *psz; size_t cch; } s_aKnownSets[] =
        {
            MY_ENTRY("api-ms-win-core-console-l1-1-0 "),
            MY_ENTRY("api-ms-win-core-datetime-l1-1-0"),
            MY_ENTRY("api-ms-win-core-debug-l1-1-0"),
            MY_ENTRY("api-ms-win-core-delayload-l1-1-0"),
            MY_ENTRY("api-ms-win-core-errorhandling-l1-1-0"),
            MY_ENTRY("api-ms-win-core-fibers-l1-1-0"),
            MY_ENTRY("api-ms-win-core-file-l1-1-0"),
            MY_ENTRY("api-ms-win-core-handle-l1-1-0"),
            MY_ENTRY("api-ms-win-core-heap-l1-1-0"),
            MY_ENTRY("api-ms-win-core-interlocked-l1-1-0"),
            MY_ENTRY("api-ms-win-core-io-l1-1-0"),
            MY_ENTRY("api-ms-win-core-libraryloader-l1-1-0"),
            MY_ENTRY("api-ms-win-core-localization-l1-1-0"),
            MY_ENTRY("api-ms-win-core-localregistry-l1-1-0"),
            MY_ENTRY("api-ms-win-core-memory-l1-1-0"),
            MY_ENTRY("api-ms-win-core-misc-l1-1-0"),
            MY_ENTRY("api-ms-win-core-namedpipe-l1-1-0"),
            MY_ENTRY("api-ms-win-core-processenvironment-l1-1-0"),
            MY_ENTRY("api-ms-win-core-processthreads-l1-1-0"),
            MY_ENTRY("api-ms-win-core-profile-l1-1-0"),
            MY_ENTRY("api-ms-win-core-rtlsupport-l1-1-0"),
            MY_ENTRY("api-ms-win-core-string-l1-1-0"),
            MY_ENTRY("api-ms-win-core-synch-l1-1-0"),
            MY_ENTRY("api-ms-win-core-sysinfo-l1-1-0"),
            MY_ENTRY("api-ms-win-core-threadpool-l1-1-0"),
            MY_ENTRY("api-ms-win-core-ums-l1-1-0"),
            MY_ENTRY("api-ms-win-core-util-l1-1-0"),
            MY_ENTRY("api-ms-win-core-xstate-l1-1-0"),
            MY_ENTRY("api-ms-win-security-base-l1-1-0"),
            MY_ENTRY("api-ms-win-security-lsalookup-l1-1-0"),
            MY_ENTRY("api-ms-win-security-sddl-l1-1-0"),
            MY_ENTRY("api-ms-win-service-core-l1-1-0"),
            MY_ENTRY("api-ms-win-service-management-l1-1-0"),
            MY_ENTRY("api-ms-win-service-management-l2-1-0"),
            MY_ENTRY("api-ms-win-service-winsvc-l1-1-0"),
        };
#undef MY_ENTRY

        /* drop the dll suffix if present. */
        PCRTUTF16 pawcName = pName->Buffer;
        size_t    cwcName  = pName->Length / sizeof(WCHAR);
        if (   cwcName > 5
            && (pawcName[cwcName - 1] == 'l' || pawcName[cwcName - 1] == 'L')
            && (pawcName[cwcName - 2] == 'l' || pawcName[cwcName - 2] == 'L')
            && (pawcName[cwcName - 3] == 'd' || pawcName[cwcName - 3] == 'D')
            &&  pawcName[cwcName - 4] == '.')
            cwcName -= 4;

        /* Search the table. */
        for (size_t i = 0; i < RT_ELEMENTS(s_aKnownSets); i++)
            if (   cwcName == s_aKnownSets[i].cch
                && RTUtf16NICmpAscii(pawcName, s_aKnownSets[i].psz, cwcName) == 0)
            {
                SUP_DPRINTF(("supR3HardenedIsApiSetDll: '%.*ls' -> true\n", pName->Length / sizeof(WCHAR)));
                return true;
            }

        SUP_DPRINTF(("supR3HardenedIsApiSetDll: Warning! '%.*ls' looks like an API set, but it's not in the list!\n",
                     pName->Length / sizeof(WCHAR), pName->Buffer));
    }

    SUP_DPRINTF(("supR3HardenedIsApiSetDll: '%.*ls' -> false\n", pName->Length / sizeof(WCHAR)));
    return false;
}


/**
 * Checks whether the given unicode string contains a path separator and at
 * least one dash.
 *
 * This is used to check for likely ApiSet name.  So far, all the pseudo DLL
 * names include multiple dashes, so we use that as a criteria for recognizing
 * them.  By happy coincident, most regular DLLs doesn't include dashes.
 *
 * @returns true if it contains path separator, false if only a name.
 * @param   pPath               The path to check.
 */
static bool supR3HardenedHasDashButNoPath(PUNICODE_STRING pPath)
{
    size_t    cDashes = 0;
    size_t    cwcLeft = pPath->Length / sizeof(WCHAR);
    PCRTUTF16 pwc     = pPath->Buffer;
    while (cwcLeft-- > 0)
    {
        RTUTF16 wc = *pwc++;
        switch (wc)
        {
            default:
                break;

            case '-':
                cDashes++;
                break;

            case '\\':
            case '/':
            case ':':
                return false;
        }
    }
    return cDashes > 0;
}


/**
 * Helper for supR3HardenedMonitor_LdrLoadDll.
 *
 * @returns NT status code.
 * @param   pwszPath        The path destination buffer.
 * @param   cwcPath         The size of the path buffer.
 * @param   pUniStrResult   The result string.
 * @param   pOrgName        The orignal name (for errors).
 * @param   pcwc            Where to return the actual length.
 */
static NTSTATUS supR3HardenedCopyRedirectionResult(WCHAR *pwszPath, size_t cwcPath, PUNICODE_STRING pUniStrResult,
                                                   PUNICODE_STRING pOrgName, UINT *pcwc)
{
    UINT cwc;
    *pcwc = cwc = pUniStrResult->Length / sizeof(WCHAR);
    if (pUniStrResult->Buffer == pwszPath)
        pwszPath[cwc] = '\0';
    else
    {
        if (cwc > cwcPath - 1)
        {
            supR3HardenedError(VINF_SUCCESS, false,
                               "supR3HardenedMonitor_LdrLoadDll: Name too long: %.*ls -> %.*ls (RtlDosApplyFileIoslationRedirection_Ustr)\n",
                               pOrgName->Length / sizeof(WCHAR), pOrgName->Buffer,
                               pUniStrResult->Length / sizeof(WCHAR), pUniStrResult->Buffer);
            return STATUS_NAME_TOO_LONG;
        }
        memcpy(&pwszPath[0], pUniStrResult->Buffer, pUniStrResult->Length);
        pwszPath[cwc] = '\0';
    }
    return STATUS_SUCCESS;
}


/**
 * Helper for supR3HardenedMonitor_LdrLoadDll that compares the name part of the
 * input path against a ASCII name string of a given length.
 *
 * @returns true if the name part matches
 * @param   pPath               The LdrLoadDll input path.
 * @param   pszName             The name to try match it with.
 * @param   cchName             The name length.
 */
static bool supR3HardenedIsFilenameMatchDll(PUNICODE_STRING pPath, const char *pszName, size_t cchName)
{
    if (pPath->Length < cchName * 2)
        return false;
    PCRTUTF16 pwszTmp = &pPath->Buffer[pPath->Length / sizeof(RTUTF16) - cchName];
    if (   pPath->Length != cchName
        && pwszTmp[-1] != '\\'
        && pwszTmp[-1] != '/')
        return false;
    return RTUtf16ICmpAscii(pwszTmp, pszName) == 0;
}


/**
 * Hooks that intercepts LdrLoadDll calls.
 *
 * Two purposes:
 *      -# Enforce our own search path restrictions.
 *      -# Prevalidate DLLs about to be loaded so we don't upset the loader data
 *         by doing it from within the NtCreateSection hook (WinVerifyTrust
 *         seems to be doing harm there on W7/32).
 *
 * @returns
 * @param   pwszSearchPath      The search path to use.
 * @param   pfFlags             Flags on input. DLL characteristics or something
 *                              on return?
 * @param   pName               The name of the module.
 * @param   phMod               Where the handle of the loaded DLL is to be
 *                              returned to the caller.
 */
__declspec(guard(ignore)) /* don't barf when calling g_pfnLdrLoadDllReal */
static NTSTATUS NTAPI
supR3HardenedMonitor_LdrLoadDll(PWSTR pwszSearchPath, PULONG pfFlags, PUNICODE_STRING pName, PHANDLE phMod)
{
    DWORD                   dwSavedLastError = RtlGetLastWin32Error();
    PUNICODE_STRING const   pOrgName = pName;
    NTSTATUS                rcNt;

    /*
     * Make sure the DLL notification callback is registered.  If we could, we
     * would've done this during early process init, but due to lack of heap
     * and uninitialized loader lock, it's not possible that early on.
     *
     * The callback protects our NtDll hooks from getting unhooked by
     * "friendly" fire from the AV crowd.
     */
    supR3HardenedWinRegisterDllNotificationCallback();

    /*
     * Process WinVerifyTrust todo before and after.
     */
    supR3HardenedWinVerifyCacheProcessWvtTodos();

    /*
     * Reject things we don't want to deal with.
     */
    if (!pName || pName->Length == 0)
    {
        supR3HardenedError(VINF_SUCCESS, false, "supR3HardenedMonitor_LdrLoadDll: name is NULL or have a zero length.\n");
        SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x (pName=%p)\n", STATUS_INVALID_PARAMETER, pName));
        RtlRestoreLastWin32Error(dwSavedLastError);
        return STATUS_INVALID_PARAMETER;
    }
    PCWCHAR const  pawcOrgName = pName->Buffer;
    uint32_t const cwcOrgName  = pName->Length / sizeof(WCHAR);

    /*SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: pName=%.*ls *pfFlags=%#x pwszSearchPath=%p:%ls\n",
                 (unsigned)pName->Length / sizeof(WCHAR), pName->Buffer, pfFlags ? *pfFlags : UINT32_MAX, pwszSearchPath,
                 !((uintptr_t)pwszSearchPath & 1) && (uintptr_t)pwszSearchPath >= 0x2000U ? pwszSearchPath : L"<flags>"));*/

    /*
     * Reject long paths that's close to the 260 limit without looking.
     */
    if (cwcOrgName > 256)
    {
        supR3HardenedError(VINF_SUCCESS, false, "supR3HardenedMonitor_LdrLoadDll: too long name: %#x bytes\n", pName->Length);
        SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_NAME_TOO_LONG));
        RtlRestoreLastWin32Error(dwSavedLastError);
        return STATUS_NAME_TOO_LONG;
    }

    /*
     * Reject all UNC-like paths as we cannot trust non-local files at all.
     * Note! We may have to relax this to deal with long path specifications and NT pass thrus.
     */
    if (   cwcOrgName >= 3
        && RTPATH_IS_SLASH(pawcOrgName[0])
        && RTPATH_IS_SLASH(pawcOrgName[1])
        && !RTPATH_IS_SLASH(pawcOrgName[2]))
    {
        supR3HardenedError(VINF_SUCCESS, false, "supR3HardenedMonitor_LdrLoadDll: rejecting UNC name '%.*ls'\n", cwcOrgName, pawcOrgName);
        SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_REDIRECTOR_NOT_STARTED));
        RtlRestoreLastWin32Error(dwSavedLastError);
        return STATUS_REDIRECTOR_NOT_STARTED;
    }

    /*
     * Reject PGHook.dll as it creates a thread from its DllMain that breaks
     * our preconditions respawning the 2nd process, resulting in
     * VERR_SUP_VP_THREAD_NOT_ALONE.   The DLL is being loaded by a user APC
     * scheduled during kernel32.dll load notification from a kernel driver,
     * so failing the load attempt should not upset anyone.
     */
    if (g_enmSupR3HardenedMainState == SUPR3HARDENEDMAINSTATE_WIN_EARLY_STUB_DEVICE_OPENED)
    {
        static const struct { const char *psz; size_t cch; } s_aUnwantedEarlyDlls[] =
        {
            { RT_STR_TUPLE("PGHook.dll") },
        };
        for (unsigned i = 0; i < RT_ELEMENTS(s_aUnwantedEarlyDlls); i++)
            if (supR3HardenedIsFilenameMatchDll(pName, s_aUnwantedEarlyDlls[i].psz, s_aUnwantedEarlyDlls[i].cch))
            {
                SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: Refusing to load '%.*ls' as it is expected to create undesirable threads that will upset our respawn checks (returning STATUS_TOO_MANY_THREADS)\n",
                             pName->Length / sizeof(RTUTF16), pName->Buffer));
                return STATUS_TOO_MANY_THREADS;
            }
    }

    /*
     * Resolve the path, copying the result into wszPath
     */
    NTSTATUS        rcNtResolve     = STATUS_SUCCESS;
    bool            fSkipValidation = false;
    bool            fCheckIfLoaded  = false;
    WCHAR           wszPath[260];
    static UNICODE_STRING const s_DefaultSuffix = RTNT_CONSTANT_UNISTR(L".dll");
    UNICODE_STRING  UniStrStatic   = { 0, (USHORT)sizeof(wszPath) - sizeof(WCHAR), wszPath };
    UNICODE_STRING  UniStrDynamic  = { 0, 0, NULL };
    PUNICODE_STRING pUniStrResult  = NULL;
    UNICODE_STRING  ResolvedName;

    /*
     * Process the name a little, checking if it needs a DLL suffix and is pathless.
     */
    uint32_t        offLastSlash = UINT32_MAX;
    uint32_t        offLastDot   = UINT32_MAX;
    for (uint32_t i = 0; i < cwcOrgName; i++)
        switch (pawcOrgName[i])
        {
            case '\\':
            case '/':
                offLastSlash = i;
                offLastDot = UINT32_MAX;
                break;
            case '.':
                offLastDot = i;
                break;
        }
    bool const fNeedDllSuffix = offLastDot == UINT32_MAX;
    //bool const fTrailingDot   = offLastDot == cwcOrgName - 1;

    /*
     * Absolute path?
     */
    if (   (   cwcOrgName >= 4
            && RT_C_IS_ALPHA(pawcOrgName[0])
            && pawcOrgName[1] == ':'
            && RTPATH_IS_SLASH(pawcOrgName[2]) )
        || (   cwcOrgName >= 1
            && RTPATH_IS_SLASH(pawcOrgName[0]) )
       )
    {
        rcNtResolve = RtlDosApplyFileIsolationRedirection_Ustr(1 /*fFlags*/,
                                                               pName,
                                                               (PUNICODE_STRING)&s_DefaultSuffix,
                                                               &UniStrStatic,
                                                               &UniStrDynamic,
                                                               &pUniStrResult,
                                                               NULL /*pNewFlags*/,
                                                               NULL /*pcbFilename*/,
                                                               NULL /*pcbNeeded*/);
        if (NT_SUCCESS(rcNtResolve))
        {
            UINT cwc;
            rcNt = supR3HardenedCopyRedirectionResult(wszPath, RT_ELEMENTS(wszPath), pUniStrResult, pName, &cwc);
            RtlFreeUnicodeString(&UniStrDynamic);
            if (!NT_SUCCESS(rcNt))
            {
                SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", rcNt));
                RtlRestoreLastWin32Error(dwSavedLastError);
                return rcNt;
            }

            ResolvedName.Buffer = wszPath;
            ResolvedName.Length = (USHORT)(cwc * sizeof(WCHAR));
            ResolvedName.MaximumLength = ResolvedName.Length + sizeof(WCHAR);

            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: '%.*ls' -> '%.*ls' [redir]\n",
                         (unsigned)pName->Length / sizeof(WCHAR), pName->Buffer,
                         ResolvedName.Length / sizeof(WCHAR), ResolvedName.Buffer, rcNt));
            pName = &ResolvedName;
        }
        else
        {
            /* Copy the path. */
            memcpy(wszPath, pawcOrgName, cwcOrgName * sizeof(WCHAR));
            if (!fNeedDllSuffix)
                wszPath[cwcOrgName] = '\0';
            else
            {
                if (cwcOrgName + 4 >= RT_ELEMENTS(wszPath))
                {
                    supR3HardenedError(VINF_SUCCESS, false,
                                       "supR3HardenedMonitor_LdrLoadDll: Name too long (abs): %.*ls\n", cwcOrgName, pawcOrgName);
                    SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_NAME_TOO_LONG));
                    RtlRestoreLastWin32Error(dwSavedLastError);
                    return STATUS_NAME_TOO_LONG;
                }
                memcpy(&wszPath[cwcOrgName], L".dll", 5 * sizeof(WCHAR));
            }
        }
    }
    /*
     * Not an absolute path.  Check if it's one of those special API set DLLs
     * or something we're known to use but should be taken from WinSxS.
     */
    else if (   supR3HardenedHasDashButNoPath(pName)
             && supR3HardenedIsApiSetDll(pName))
    {
        memcpy(wszPath, pName->Buffer, pName->Length);
        wszPath[pName->Length / sizeof(WCHAR)] = '\0';
        fSkipValidation = true;
    }
    /*
     * Not an absolute path or special API set.  There are two alternatives
     * now, either there is no path at all or there is a relative path.  We
     * will resolve it to an absolute path in either case, failing the call
     * if we can't.
     */
    else
    {
        /*
         * Reject relative paths for now as they might be breakout attempts.
         */
        if (offLastSlash != UINT32_MAX)
        {
            supR3HardenedError(VINF_SUCCESS, false,
                               "supR3HardenedMonitor_LdrLoadDll: relative name not permitted: %.*ls\n",
                               cwcOrgName, pawcOrgName);
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_OBJECT_NAME_INVALID));
            RtlRestoreLastWin32Error(dwSavedLastError);
            return STATUS_OBJECT_NAME_INVALID;
        }

        /*
         * Perform dll redirection to WinSxS such.  We using an undocumented
         * API here, which as always is a bit risky...  ASSUMES that the API
         * returns a full DOS path.
         */
        UINT cwc;
        rcNtResolve = RtlDosApplyFileIsolationRedirection_Ustr(1 /*fFlags*/,
                                                               pName,
                                                               (PUNICODE_STRING)&s_DefaultSuffix,
                                                               &UniStrStatic,
                                                               &UniStrDynamic,
                                                               &pUniStrResult,
                                                               NULL /*pNewFlags*/,
                                                               NULL /*pcbFilename*/,
                                                               NULL /*pcbNeeded*/);
        if (NT_SUCCESS(rcNtResolve))
        {
            rcNt = supR3HardenedCopyRedirectionResult(wszPath, RT_ELEMENTS(wszPath), pUniStrResult, pName, &cwc);
            RtlFreeUnicodeString(&UniStrDynamic);
            if (!NT_SUCCESS(rcNt))
            {
                SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", rcNt));
                RtlRestoreLastWin32Error(dwSavedLastError);
                return rcNt;
            }
        }
        else
        {
            /*
             * Search for the DLL.  Only System32 is allowed as the target of
             * a search on the API level, all VBox calls will have full paths.
             * If the DLL is not in System32, we will resort to check if it's
             * refering to an already loaded DLL (fCheckIfLoaded).
             */
            AssertCompile(sizeof(g_System32WinPath.awcBuffer) <= sizeof(wszPath));
            cwc = g_System32WinPath.UniStr.Length / sizeof(RTUTF16); Assert(cwc > 2);
            if (cwc + 1 + cwcOrgName + fNeedDllSuffix * 4 >= RT_ELEMENTS(wszPath))
            {
                supR3HardenedError(VINF_SUCCESS, false,
                                   "supR3HardenedMonitor_LdrLoadDll: Name too long (system32): %.*ls\n", cwcOrgName, pawcOrgName);
                SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_NAME_TOO_LONG));
                RtlRestoreLastWin32Error(dwSavedLastError);
                return STATUS_NAME_TOO_LONG;
            }
            memcpy(wszPath, g_System32WinPath.UniStr.Buffer, cwc * sizeof(RTUTF16));
            wszPath[cwc++] = '\\';
            memcpy(&wszPath[cwc], pawcOrgName, cwcOrgName * sizeof(WCHAR));
            cwc += cwcOrgName;
            if (!fNeedDllSuffix)
                wszPath[cwc] = '\0';
            else
            {
                memcpy(&wszPath[cwc], L".dll", 5 * sizeof(WCHAR));
                cwc += 4;
            }
            fCheckIfLoaded = true;
        }

        ResolvedName.Buffer = wszPath;
        ResolvedName.Length = (USHORT)(cwc * sizeof(WCHAR));
        ResolvedName.MaximumLength = ResolvedName.Length + sizeof(WCHAR);
        pName = &ResolvedName;
    }

#ifndef IN_SUP_R3_STATIC
    /*
     * Reject blacklisted DLLs based on input name.
     */
    for (unsigned i = 0; g_aSupNtViBlacklistedDlls[i].psz != NULL; i++)
        if (supR3HardenedIsFilenameMatchDll(pName, g_aSupNtViBlacklistedDlls[i].psz, g_aSupNtViBlacklistedDlls[i].cch))
        {
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: Refusing to load blacklisted DLL: '%.*ls'\n",
                         pName->Length / sizeof(RTUTF16), pName->Buffer));
            RtlRestoreLastWin32Error(dwSavedLastError);
            return STATUS_TOO_MANY_THREADS;
        }
#endif

    bool fQuiet = false;
    if (!fSkipValidation)
    {
        /*
         * Try open the file.  If this fails, never mind, just pass it on to
         * the real API as we've replaced any searchable name with a full name
         * and the real API can come up with a fitting status code for it.
         */
        HANDLE          hRootDir;
        UNICODE_STRING  NtPathUniStr;
        int rc = RTNtPathFromWinUtf16Ex(&NtPathUniStr, &hRootDir, wszPath, RTSTR_MAX);
        if (RT_FAILURE(rc))
        {
            supR3HardenedError(rc, false,
                               "supR3HardenedMonitor_LdrLoadDll: RTNtPathFromWinUtf16Ex failed on '%ls': %Rrc\n", wszPath, rc);
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x\n", STATUS_OBJECT_NAME_INVALID));
            RtlRestoreLastWin32Error(dwSavedLastError);
            return STATUS_OBJECT_NAME_INVALID;
        }

        HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtPathUniStr, OBJ_CASE_INSENSITIVE, hRootDir, NULL /*pSecDesc*/);

        rcNt = NtCreateFile(&hFile,
                            FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                            &ObjAttr,
                            &Ios,
                            NULL /* Allocation Size*/,
                            FILE_ATTRIBUTE_NORMAL,
                            FILE_SHARE_READ,
                            FILE_OPEN,
                            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                            NULL /*EaBuffer*/,
                            0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
            rcNt = Ios.Status;
        if (NT_SUCCESS(rcNt))
        {
            ULONG fAccess = 0;
            ULONG fProtect = 0;
            bool  fCallRealApi = false;
            rcNt = supR3HardenedScreenImage(hFile, true /*fImage*/, RT_VALID_PTR(pfFlags) && (*pfFlags & 0x2) /*fIgnoreArch*/,
                                            &fAccess, &fProtect, &fCallRealApi,
                                            "LdrLoadDll", false /*fAvoidWinVerifyTrust*/, &fQuiet);
            NtClose(hFile);
            if (!NT_SUCCESS(rcNt))
            {
                if (!fQuiet)
                {
                    if (pOrgName != pName)
                        supR3HardenedError(VINF_SUCCESS, false, "supR3HardenedMonitor_LdrLoadDll: rejecting '%ls': rcNt=%#x\n",
                                           wszPath, rcNt);
                    else
                        supR3HardenedError(VINF_SUCCESS, false, "supR3HardenedMonitor_LdrLoadDll: rejecting '%ls' (%.*ls): rcNt=%#x\n",
                                           wszPath, pOrgName->Length / sizeof(WCHAR), pOrgName->Buffer, rcNt);
                    SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x '%ls'\n", rcNt, wszPath));
                }
                RtlRestoreLastWin32Error(dwSavedLastError);
                return rcNt;
            }

            supR3HardenedWinVerifyCacheProcessImportTodos();
        }
        else
        {
            DWORD dwErr = RtlGetLastWin32Error();

            /*
             * Deal with special case where the caller (first case was MS LifeCam)
             * is using LoadLibrary instead of GetModuleHandle to find a loaded DLL.
             */
            NTSTATUS rcNtGetDll = STATUS_SUCCESS;
            if (   fCheckIfLoaded
                 && (   rcNt == STATUS_OBJECT_NAME_NOT_FOUND
                     || rcNt == STATUS_OBJECT_PATH_NOT_FOUND))
            {
                rcNtGetDll = LdrGetDllHandle(NULL /*DllPath*/, NULL /*pfFlags*/, pOrgName, phMod);
                if (NT_SUCCESS(rcNtGetDll))
                {
                    RTNtPathFree(&NtPathUniStr, &hRootDir);
                    RtlRestoreLastWin32Error(dwSavedLastError);
                    return rcNtGetDll;
                }
            }

            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: error opening '%ls': %u (NtPath=%.*ls; Input=%.*ls; rcNtGetDll=%#x\n",
                         wszPath, dwErr, NtPathUniStr.Length / sizeof(RTUTF16), NtPathUniStr.Buffer,
                         pOrgName->Length / sizeof(WCHAR), pOrgName->Buffer, rcNtGetDll));

            RTNtPathFree(&NtPathUniStr, &hRootDir);
            RtlRestoreLastWin32Error(dwSavedLastError);
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x '%ls'\n", rcNt, wszPath));
            return rcNt;
        }
        RTNtPathFree(&NtPathUniStr, &hRootDir);
    }

    /*
     * Screened successfully enough.  Call the real thing.
     */
    if (!fQuiet)
    {
        if (pOrgName != pName)
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: pName=%.*ls (Input=%.*ls, rcNtResolve=%#x) *pfFlags=%#x pwszSearchPath=%p:%ls [calling]\n",
                         (unsigned)pName->Length / sizeof(WCHAR), pName->Buffer,
                         (unsigned)pOrgName->Length / sizeof(WCHAR), pOrgName->Buffer, rcNtResolve,
                         pfFlags ? *pfFlags : UINT32_MAX, pwszSearchPath,
                         !((uintptr_t)pwszSearchPath & 1) && (uintptr_t)pwszSearchPath >= 0x2000U ? pwszSearchPath : L"<flags>"));
        else
            SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: pName=%.*ls (rcNtResolve=%#x) *pfFlags=%#x pwszSearchPath=%p:%ls [calling]\n",
                         (unsigned)pName->Length / sizeof(WCHAR), pName->Buffer, rcNtResolve,
                         pfFlags ? *pfFlags : UINT32_MAX, pwszSearchPath,
                         !((uintptr_t)pwszSearchPath & 1) && (uintptr_t)pwszSearchPath >= 0x2000U ? pwszSearchPath : L"<flags>"));
    }

    RtlRestoreLastWin32Error(dwSavedLastError);
    rcNt = g_pfnLdrLoadDllReal(pwszSearchPath, pfFlags, pName, phMod);

    /*
     * Log the result and process pending WinVerifyTrust work if we can.
     */
    dwSavedLastError = RtlGetLastWin32Error();

    if (NT_SUCCESS(rcNt) && phMod)
        SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x hMod=%p '%ls'\n", rcNt, *phMod, wszPath));
    else if (!NT_SUCCESS(rcNt) || !fQuiet)
        SUP_DPRINTF(("supR3HardenedMonitor_LdrLoadDll: returns rcNt=%#x '%ls'\n", rcNt, wszPath));

    supR3HardenedWinVerifyCacheProcessWvtTodos();

    RtlRestoreLastWin32Error(dwSavedLastError);

    return rcNt;
}


/**
 * DLL load and unload notification callback.
 *
 * This is a safety against our LdrLoadDll hook being replaced by protection
 * software.  Though, we prefer the LdrLoadDll hook to this one as it allows us
 * to call WinVerifyTrust more freely.
 *
 * @param   ulReason    The reason we're called, see
 *                      LDR_DLL_NOTIFICATION_REASON_XXX.
 * @param   pData       Reason specific data.  (Format is currently the same for
 *                      both load and unload.)
 * @param   pvUser      User parameter (ignored).
 *
 * @remarks Vista and later.
 * @remarks The loader lock is held when we're called, at least on Windows 7.
 */
static VOID CALLBACK
supR3HardenedDllNotificationCallback(ULONG ulReason, PCLDR_DLL_NOTIFICATION_DATA pData, PVOID pvUser) RT_NOTHROW_DEF
{
    NOREF(pvUser);

    /*
     * Screen the image on load.  We will normally get a verification cache
     * hit here because of the LdrLoadDll and NtCreateSection hooks, so it
     * should be relatively cheap to recheck.  In case our NtDll patches
     * got re
     *
     * This ASSUMES that we get informed after the fact as indicated by the
     * available documentation.
     */
    if (ulReason == LDR_DLL_NOTIFICATION_REASON_LOADED)
    {
        SUP_DPRINTF(("supR3HardenedDllNotificationCallback: load   %p LB %#010x %.*ls [fFlags=%#x]\n",
                     pData->Loaded.DllBase, pData->Loaded.SizeOfImage,
                     pData->Loaded.FullDllName->Length / sizeof(WCHAR), pData->Loaded.FullDllName->Buffer,
                     pData->Loaded.Flags));

        /* Convert the windows path to an NT path and open it. */
        HANDLE          hRootDir;
        UNICODE_STRING  NtPathUniStr;
        int rc = RTNtPathFromWinUtf16Ex(&NtPathUniStr, &hRootDir, pData->Loaded.FullDllName->Buffer,
                                        pData->Loaded.FullDllName->Length / sizeof(WCHAR));
        if (RT_FAILURE(rc))
        {
            supR3HardenedFatal("supR3HardenedDllNotificationCallback: RTNtPathFromWinUtf16Ex failed on '%.*ls': %Rrc\n",
                               pData->Loaded.FullDllName->Length / sizeof(WCHAR), pData->Loaded.FullDllName->Buffer, rc);
            return;
        }

        HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtPathUniStr, OBJ_CASE_INSENSITIVE, hRootDir, NULL /*pSecDesc*/);

        NTSTATUS rcNt = NtCreateFile(&hFile,
                                     FILE_READ_DATA | READ_CONTROL | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /* Allocation Size*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ,
                                     FILE_OPEN,
                                     FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
            rcNt = Ios.Status;
        if (!NT_SUCCESS(rcNt))
        {
            supR3HardenedFatal("supR3HardenedDllNotificationCallback: NtCreateFile failed on '%.*ls' / '%.*ls': %#x\n",
                               pData->Loaded.FullDllName->Length / sizeof(WCHAR), pData->Loaded.FullDllName->Buffer,
                               NtPathUniStr.Length / sizeof(WCHAR), NtPathUniStr.Buffer, rcNt);
            /* not reached */
        }

        /* Do the screening. */
        ULONG fAccess = 0;
        ULONG fProtect = 0;
        bool  fCallRealApi = false;
        bool  fQuietFailure = false;
        rcNt = supR3HardenedScreenImage(hFile, true /*fImage*/, true /*fIgnoreArch*/, &fAccess, &fProtect, &fCallRealApi,
                                        "LdrLoadDll", true /*fAvoidWinVerifyTrust*/, &fQuietFailure);
        NtClose(hFile);
        if (!NT_SUCCESS(rcNt))
        {
            supR3HardenedFatal("supR3HardenedDllNotificationCallback: supR3HardenedScreenImage failed on '%.*ls' / '%.*ls': %#x\n",
                               pData->Loaded.FullDllName->Length / sizeof(WCHAR), pData->Loaded.FullDllName->Buffer,
                               NtPathUniStr.Length / sizeof(WCHAR), NtPathUniStr.Buffer, rcNt);
            /* not reached */
        }
        RTNtPathFree(&NtPathUniStr, &hRootDir);
    }
    /*
     * Log the unload call.
     */
    else if (ulReason == LDR_DLL_NOTIFICATION_REASON_UNLOADED)
    {
        SUP_DPRINTF(("supR3HardenedDllNotificationCallback: Unload %p LB %#010x %.*ls [flags=%#x]\n",
                     pData->Unloaded.DllBase, pData->Unloaded.SizeOfImage,
                     pData->Unloaded.FullDllName->Length / sizeof(WCHAR), pData->Unloaded.FullDllName->Buffer,
                     pData->Unloaded.Flags));
    }
    /*
     * Just log things we don't know and then return without caching anything.
     */
    else
    {
        static uint32_t s_cLogEntries = 0;
        if (s_cLogEntries++ < 32)
            SUP_DPRINTF(("supR3HardenedDllNotificationCallback: ulReason=%u pData=%p\n", ulReason, pData));
        return;
    }

    /*
     * Use this opportunity to make sure our NtDll patches are still in place,
     * since they may be replaced by indecent protection software solutions.
     */
    supR3HardenedWinReInstallHooks(false /*fFirstCall */);
}


/**
 * Registers the DLL notification callback if it hasn't already been registered.
 */
static void supR3HardenedWinRegisterDllNotificationCallback(void)
{
    /*
     * The notification API was added in Vista, so it's an optional (weak) import.
     */
    if (   LdrRegisterDllNotification != NULL
        && g_cDllNotificationRegistered <= 0
        && g_cDllNotificationRegistered > -32)
    {
        NTSTATUS rcNt = LdrRegisterDllNotification(0, supR3HardenedDllNotificationCallback, NULL, &g_pvDllNotificationCookie);
        if (NT_SUCCESS(rcNt))
        {
            SUP_DPRINTF(("Registered Dll notification callback with NTDLL.\n"));
            g_cDllNotificationRegistered = 1;
        }
        else
        {
            supR3HardenedError(rcNt, false /*fFatal*/, "LdrRegisterDllNotification failed: %#x\n", rcNt);
            g_cDllNotificationRegistered--;
        }
    }
}


/**
 * Dummy replacement routine we use for passifying unwanted user APC
 * callbacks during early process initialization.
 *
 * @sa supR3HardenedMonitor_KiUserApcDispatcher_C
 */
static VOID NTAPI supR3HardenedWinDummyApcRoutine(PVOID pvArg1, PVOID pvArg2, PVOID pvArg3)
{
    SUP_DPRINTF(("supR3HardenedWinDummyApcRoutine: pvArg1=%p pvArg2=%p pvArg3=%p\n", pvArg1, pvArg2, pvArg3));
    RT_NOREF(pvArg1, pvArg2, pvArg3);
}


/**
 * This is called when ntdll!KiUserApcDispatcher is invoked (via
 * supR3HardenedMonitor_KiUserApcDispatcher).
 *
 * The parent process hooks KiUserApcDispatcher before the guest starts
 * executing. There should only be one APC request dispatched while the process
 * is being initialized, and that's the one calling ntdll!LdrInitializeThunk.
 *
 * @returns Where to go to run the original code.
 * @param   pvApcArgs   The APC dispatcher arguments.
 */
DECLASM(uintptr_t) supR3HardenedMonitor_KiUserApcDispatcher_C(void *pvApcArgs)
{
#ifdef RT_ARCH_AMD64
    PCONTEXT   pCtx        = (PCONTEXT)pvApcArgs;
    uintptr_t *ppfnRoutine = (uintptr_t *)&pCtx->P4Home;
#else
    struct X86APCCTX
    {
        uintptr_t   pfnRoutine;
        uintptr_t   pvCtx;
        uintptr_t   pvUser1;
        uintptr_t   pvUser2;
        CONTEXT     Ctx;
    } *pCtx = (struct X86APCCTX *)pvApcArgs;
    uintptr_t *ppfnRoutine = &pCtx->pfnRoutine;
#endif
    uintptr_t  pfnRoutine = *ppfnRoutine;

    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_HARDENED_MAIN_CALLED)
    {
        if (pfnRoutine == g_pfnLdrInitializeThunk) /* Note! we could use this to detect thread creation too. */
            SUP_DPRINTF(("supR3HardenedMonitor_KiUserApcDispatcher_C: pfnRoutine=%p enmState=%d - okay\n",
                         pfnRoutine, g_enmSupR3HardenedMainState));
        else
        {
            *ppfnRoutine = (uintptr_t)supR3HardenedWinDummyApcRoutine;
            SUP_DPRINTF(("supR3HardenedMonitor_KiUserApcDispatcher_C: pfnRoutine=%p enmState=%d -> supR3HardenedWinDummyApcRoutine\n",
                         pfnRoutine, g_enmSupR3HardenedMainState));
        }
    }
    return (uintptr_t)g_pfnKiUserApcDispatcherReal;
}


/**
 * SUP_DPRINTF on pCtx, with lead-in text.
 */
static void supR3HardNtDprintCtx(PCONTEXT pCtx, const char *pszLeadIn)
{
#ifdef RT_ARCH_AMD64
    SUP_DPRINTF(("%s\n"
                 "  rax=%016RX64 rbx=%016RX64 rcx=%016RX64 rdx=%016RX64\n"
                 "  rsi=%016RX64 rdi=%016RX64 r8 =%016RX64 r9 =%016RX64\n"
                 "  r10=%016RX64 r11=%016RX64 r12=%016RX64 r13=%016RX64\n"
                 "  r14=%016RX64 r15=%016RX64  P1=%016RX64  P2=%016RX64\n"
                 "  rip=%016RX64 rsp=%016RX64 rbp=%016RX64    ctxflags=%08x\n"
                 "  cs=%04x ss=%04x ds=%04x es=%04x fs=%04x gs=%04x    eflags=%08x   mxcrx=%08x\n"
                 "   P3=%016RX64  P4=%016RX64  P5=%016RX64  P6=%016RX64\n"
                 "  dr0=%016RX64 dr1=%016RX64 dr2=%016RX64 dr3=%016RX64\n"
                 "  dr6=%016RX64 dr7=%016RX64 vcr=%016RX64 dcr=%016RX64\n"
                 "  lbt=%016RX64 lbf=%016RX64 lxt=%016RX64 lxf=%016RX64\n"
                 ,
                 pszLeadIn,
                 pCtx->Rax, pCtx->Rbx, pCtx->Rcx, pCtx->Rdx,
                 pCtx->Rsi, pCtx->Rdi, pCtx->R8, pCtx->R9,
                 pCtx->R10, pCtx->R11, pCtx->R12, pCtx->R13,
                 pCtx->R14, pCtx->R15, pCtx->P1Home, pCtx->P2Home,
                 pCtx->Rip, pCtx->Rsp, pCtx->Rbp, pCtx->ContextFlags,
                 pCtx->SegCs, pCtx->SegSs, pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs, pCtx->EFlags, pCtx->MxCsr,
                 pCtx->P3Home, pCtx->P4Home, pCtx->P5Home, pCtx->P6Home,
                 pCtx->Dr0, pCtx->Dr1, pCtx->Dr2, pCtx->Dr3,
                 pCtx->Dr6, pCtx->Dr7, pCtx->VectorControl, pCtx->DebugControl,
                 pCtx->LastBranchToRip, pCtx->LastBranchFromRip, pCtx->LastExceptionToRip, pCtx->LastExceptionFromRip ));
#elif defined(RT_ARCH_X86)
    SUP_DPRINTF(("%s\n"
                 "  eax=%08RX32 ebx=%08RX32 ecx=%08RX32 edx=%08RX32 esi=%08rx64 edi=%08RX32\n"
                 "  eip=%08RX32 esp=%08RX32 ebp=%08RX32 eflags=%08RX32\n"
                 "  cs=%04RX16 ds=%04RX16 es=%04RX16 fs=%04RX16 gs=%04RX16\n"
                 "  dr0=%08RX32 dr1=%08RX32 dr2=%08RX32 dr3=%08RX32 dr6=%08RX32 dr7=%08RX32\n",
                 pszLeadIn,
                 pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx, pCtx->Esi, pCtx->Edi,
                 pCtx->Eip, pCtx->Esp, pCtx->Ebp, pCtx->EFlags,
                 pCtx->SegCs, pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs,
                 pCtx->Dr0, pCtx->Dr1, pCtx->Dr2, pCtx->Dr3, pCtx->Dr6, pCtx->Dr7));
#else
# error "Unsupported arch."
#endif
    RT_NOREF(pCtx, pszLeadIn);
}


#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
/**
 * This is called when ntdll!KiUserExceptionDispatcher is invoked (via
 * supR3HardenedMonitor_KiUserExceptionDispatcher).
 *
 * For 64-bit processes there is a return and two parameters on the stack.
 *
 * @returns Where to go to run the original code.
 * @param   pXcptRec    The exception record.
 * @param   pCtx        The exception context.
 */
DECLASM(uintptr_t) supR3HardenedMonitor_KiUserExceptionDispatcher_C(PEXCEPTION_RECORD pXcptRec, PCONTEXT pCtx)
{
    /*
     * Ignore the guard page violation.
     */
    if (pXcptRec->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
        return (uintptr_t)g_pfnKiUserExceptionDispatcherReal;

    /*
     * Log the exception and context.
     */
    char szLeadIn[384];
    if (pXcptRec->NumberParameters == 0)
        RTStrPrintf(szLeadIn, sizeof(szLeadIn), "KiUserExceptionDispatcher: %#x @ %p (flags=%#x)",
                    pXcptRec->ExceptionCode, pXcptRec->ExceptionAddress, pXcptRec->ExceptionFlags);
    else if (pXcptRec->NumberParameters == 1)
        RTStrPrintf(szLeadIn, sizeof(szLeadIn), "KiUserExceptionDispatcher: %#x (%p) @ %p (flags=%#x)",
                    pXcptRec->ExceptionCode, pXcptRec->ExceptionInformation[0],
                    pXcptRec->ExceptionAddress, pXcptRec->ExceptionFlags);
    else if (pXcptRec->NumberParameters == 2)
        RTStrPrintf(szLeadIn, sizeof(szLeadIn), "KiUserExceptionDispatcher: %#x (%p, %p) @ %p (flags=%#x)",
                    pXcptRec->ExceptionCode, pXcptRec->ExceptionInformation[0], pXcptRec->ExceptionInformation[1],
                    pXcptRec->ExceptionAddress, pXcptRec->ExceptionFlags);
    else if (pXcptRec->NumberParameters == 3)
        RTStrPrintf(szLeadIn, sizeof(szLeadIn), "KiUserExceptionDispatcher: %#x (%p, %p, %p) @ %p (flags=%#x)",
                    pXcptRec->ExceptionCode, pXcptRec->ExceptionInformation[0], pXcptRec->ExceptionInformation[1],
                    pXcptRec->ExceptionInformation[2], pXcptRec->ExceptionAddress, pXcptRec->ExceptionFlags);
    else
        RTStrPrintf(szLeadIn, sizeof(szLeadIn), "KiUserExceptionDispatcher: %#x (#%u: %p, %p, %p, %p, %p, %p, %p, %p, ...) @ %p (flags=%#x)",
                    pXcptRec->ExceptionCode, pXcptRec->NumberParameters,
                    pXcptRec->ExceptionInformation[0], pXcptRec->ExceptionInformation[1],
                    pXcptRec->ExceptionInformation[2], pXcptRec->ExceptionInformation[3],
                    pXcptRec->ExceptionInformation[4], pXcptRec->ExceptionInformation[5],
                    pXcptRec->ExceptionInformation[6], pXcptRec->ExceptionInformation[7],
                    pXcptRec->ExceptionAddress, pXcptRec->ExceptionFlags);
    supR3HardNtDprintCtx(pCtx, szLeadIn);

    return (uintptr_t)g_pfnKiUserExceptionDispatcherReal;
}
#endif /* !VBOX_WITHOUT_HARDENDED_XCPT_LOGGING */


static void supR3HardenedWinHookFailed(const char *pszWhich, uint8_t const *pbPrologue)
{
    supR3HardenedFatalMsg("supR3HardenedWinInstallHooks", kSupInitOp_Misc, VERR_NO_MEMORY,
                          "Failed to install %s monitor: %x %x %x %x  %x %x %x %x  %x %x %x %x  %x %x %x %x\n "
#ifdef RT_ARCH_X86
                          "(It is also possible you are running 32-bit VirtualBox under 64-bit windows.)\n"
#endif
                          ,
                          pszWhich,
                          pbPrologue[0],  pbPrologue[1],  pbPrologue[2],  pbPrologue[3],
                          pbPrologue[4],  pbPrologue[5],  pbPrologue[6],  pbPrologue[7],
                          pbPrologue[8],  pbPrologue[9],  pbPrologue[10], pbPrologue[11],
                          pbPrologue[12], pbPrologue[13], pbPrologue[14], pbPrologue[15]);
}


/**
 * IPRT thread that waits for the parent process to terminate and reacts by
 * exiting the current process.
 *
 * @returns VINF_SUCCESS
 * @param   hSelf               The current thread.  Ignored.
 * @param   pvUser              The handle of the parent process.
 */
static DECLCALLBACK(int) supR3HardenedWinParentWatcherThread(RTTHREAD hSelf, void *pvUser)
{
    HANDLE hProcWait = (HANDLE)pvUser;
    NOREF(hSelf);

    /*
     * Wait for the parent to terminate.
     */
    NTSTATUS rcNt;
    for (;;)
    {
        rcNt = NtWaitForSingleObject(hProcWait, TRUE /*Alertable*/, NULL /*pTimeout*/);
        if (   rcNt == STATUS_WAIT_0
            || rcNt == STATUS_ABANDONED_WAIT_0)
            break;
        if (   rcNt != STATUS_TIMEOUT
            && rcNt != STATUS_USER_APC
            && rcNt != STATUS_ALERTED)
            supR3HardenedFatal("NtWaitForSingleObject returned %#x\n", rcNt);
    }

    /*
     * Proxy the termination code of the child, if it exited already.
     */
    PROCESS_BASIC_INFORMATION BasicInfo;
    NTSTATUS rcNt2 = NtQueryInformationProcess(hProcWait, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (   !NT_SUCCESS(rcNt2)
        || BasicInfo.ExitStatus == STATUS_PENDING)
        BasicInfo.ExitStatus = RTEXITCODE_FAILURE;

    NtClose(hProcWait);
    SUP_DPRINTF(("supR3HardenedWinParentWatcherThread: Quitting: ExitCode=%#x rcNt=%#x\n", BasicInfo.ExitStatus, rcNt));
    suplibHardenedExit((RTEXITCODE)BasicInfo.ExitStatus);
    /* not reached */
}


/**
 * Creates the parent watcher thread that will make sure this process exits when
 * the parent does.
 *
 * This is a necessary evil to make VBoxNetDhcp and VBoxNetNat termination from
 * Main work without too much new magic.  It also makes Ctrl-C or similar work
 * in on the hardened processes in the windows console.
 *
 * @param   hVBoxRT             The VBoxRT.dll handle.  We use RTThreadCreate to
 *                              spawn the thread to avoid duplicating thread
 *                              creation and thread naming code from IPRT.
 */
DECLHIDDEN(void) supR3HardenedWinCreateParentWatcherThread(HMODULE hVBoxRT)
{
    /*
     * Resolve runtime methods that we need.
     */
    PFNRTTHREADCREATE pfnRTThreadCreate = (PFNRTTHREADCREATE)GetProcAddress(hVBoxRT, "RTThreadCreate");
    SUPR3HARDENED_ASSERT(pfnRTThreadCreate != NULL);

    /*
     * Find the parent process ID.
     */
    PROCESS_BASIC_INFORMATION BasicInfo;
    NTSTATUS rcNt = NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatal("supR3HardenedWinCreateParentWatcherThread: NtQueryInformationProcess failed: %#x\n", rcNt);

    /*
     * Open the parent process for waiting and exitcode query.
     */
    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, NULL, 0, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    CLIENT_ID ClientId;
    ClientId.UniqueProcess = (HANDLE)BasicInfo.InheritedFromUniqueProcessId;
    ClientId.UniqueThread  = NULL;

    HANDLE hParent;
    rcNt = NtOpenProcess(&hParent, SYNCHRONIZE | PROCESS_QUERY_INFORMATION, &ObjAttr, &ClientId);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinCreateParentWatcherThread", kSupInitOp_Misc, VERR_GENERAL_FAILURE,
                              "NtOpenProcess(%p.0) failed: %#x\n", ClientId.UniqueProcess, rcNt);

    /*
     * Create the thread that should do the waiting.
     */
    int rc = pfnRTThreadCreate(NULL, supR3HardenedWinParentWatcherThread, hParent, _64K /* stack */,
                               RTTHREADTYPE_DEFAULT, 0 /*fFlags*/, "ParentWatcher");
    if (RT_FAILURE(rc))
        supR3HardenedFatal("supR3HardenedWinCreateParentWatcherThread: RTThreadCreate failed: %Rrc\n", rc);
}


/**
 * Checks if the calling thread is the only one in the process.
 *
 * @returns true if we're positive we're alone, false if not.
 */
static bool supR3HardenedWinAmIAlone(void) RT_NOTHROW_DEF
{
    ULONG    fAmIAlone = 0;
    ULONG    cbIgn     = 0;
    NTSTATUS rcNt = NtQueryInformationThread(NtCurrentThread(), ThreadAmILastThread, &fAmIAlone, sizeof(fAmIAlone), &cbIgn);
    Assert(NT_SUCCESS(rcNt));
    return NT_SUCCESS(rcNt) && fAmIAlone != 0;
}


/**
 * Simplify NtProtectVirtualMemory interface.
 *
 * Modifies protection for the current process.  Caller must know the current
 * protection as it's not returned.
 *
 * @returns NT status code.
 * @param   pvMem               The memory to change protection for.
 * @param   cbMem               The amount of memory to change.
 * @param   fNewProt            The new protection.
 */
static NTSTATUS supR3HardenedWinProtectMemory(PVOID pvMem, SIZE_T cbMem, ULONG fNewProt) RT_NOTHROW_DEF
{
    ULONG fOldProt = 0;
    return NtProtectVirtualMemory(NtCurrentProcess(), &pvMem, &cbMem, fNewProt, &fOldProt);
}


/**
 * Installs or reinstalls the NTDLL patches.
 */
static void supR3HardenedWinReInstallHooks(bool fFirstCall) RT_NOTHROW_DEF
{
    struct
    {
        size_t          cbPatch;
        uint8_t const  *pabPatch;
        uint8_t       **ppbApi;
        const char     *pszName;
    } const s_aPatches[] =
    {
        { sizeof(g_abNtCreateSectionPatch),           g_abNtCreateSectionPatch,           &g_pbNtCreateSection,           "NtCreateSection"     },
        { sizeof(g_abLdrLoadDllPatch),                g_abLdrLoadDllPatch,                &g_pbLdrLoadDll,                "LdrLoadDll"          },
        { sizeof(g_abKiUserApcDispatcherPatch),       g_abKiUserApcDispatcherPatch,       &g_pbKiUserApcDispatcher,       "KiUserApcDispatcher" },
#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
        { sizeof(g_abKiUserExceptionDispatcherPatch), g_abKiUserExceptionDispatcherPatch, &g_pbKiUserExceptionDispatcher, "KiUserExceptionDispatcher" },
#endif
    };

    ULONG fAmIAlone = ~(ULONG)0;

    for (uint32_t i = 0; i < RT_ELEMENTS(s_aPatches); i++)
    {
        uint8_t *pbApi = *s_aPatches[i].ppbApi;
        if (memcmp(pbApi, s_aPatches[i].pabPatch, s_aPatches[i].cbPatch) != 0)
        {
            /*
             * Log the incident if it's not the initial call.
             */
            static uint32_t volatile s_cTimes = 0;
            if (!fFirstCall && s_cTimes < 128)
            {
                s_cTimes++;
                SUP_DPRINTF(("supR3HardenedWinReInstallHooks: Reinstalling %s (%p: %.*Rhxs).\n",
                             s_aPatches[i].pszName, pbApi, s_aPatches[i].cbPatch, pbApi));
            }

            Assert(s_aPatches[i].cbPatch >= 4);

            SUPR3HARDENED_ASSERT_NT_SUCCESS(supR3HardenedWinProtectMemory(pbApi, s_aPatches[i].cbPatch, PAGE_EXECUTE_READWRITE));

            /*
             * If we're alone, just memcpy the patch in.
             */

            if (fAmIAlone == ~(ULONG)0)
                fAmIAlone = supR3HardenedWinAmIAlone();
            if (fAmIAlone)
                memcpy(pbApi, s_aPatches[i].pabPatch, s_aPatches[i].cbPatch);
            else
            {
                /*
                 * Not alone.  Start by injecting a JMP $-2, then waste some
                 * CPU cycles to get the other threads a good chance of getting
                 * out of the code before we replace it.
                 */
                RTUINT32U uJmpDollarMinus;
                uJmpDollarMinus.au8[0] = 0xeb;
                uJmpDollarMinus.au8[1] = 0xfe;
                uJmpDollarMinus.au8[2] = pbApi[2];
                uJmpDollarMinus.au8[3] = pbApi[3];
                ASMAtomicXchgU32((uint32_t volatile *)pbApi, uJmpDollarMinus.u);

                NtYieldExecution();
                NtYieldExecution();

                /* Copy in the tail bytes of the patch, then xchg the jmp $-2. */
                if (s_aPatches[i].cbPatch > 4)
                    memcpy(&pbApi[4], &s_aPatches[i].pabPatch[4], s_aPatches[i].cbPatch - 4);
                ASMAtomicXchgU32((uint32_t volatile *)pbApi, *(uint32_t *)s_aPatches[i].pabPatch);
            }

            SUPR3HARDENED_ASSERT_NT_SUCCESS(supR3HardenedWinProtectMemory(pbApi, s_aPatches[i].cbPatch, PAGE_EXECUTE_READ));
        }
    }
}


/**
 * Install hooks for intercepting calls dealing with mapping shared libraries
 * into the process.
 *
 * This allows us to prevent undesirable shared libraries from being loaded.
 *
 * @remarks We assume we're alone in this process, so no seralizing trickery is
 *          necessary when installing the patch.
 *
 * @remarks We would normally just copy the prologue sequence somewhere and add
 *          a jump back at the end of it. But because we wish to avoid
 *          allocating executable memory, we need to have preprepared assembly
 *          "copies".  This makes the non-system call patching a little tedious
 *            and inflexible.
 */
static void supR3HardenedWinInstallHooks(void)
{
    NTSTATUS rcNt;

    /*
     * Disable hard error popups so we can quietly refuse images to be loaded.
     */
    ULONG fHardErr = 0;
    rcNt = NtQueryInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &fHardErr, sizeof(fHardErr), NULL);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinInstallHooks", kSupInitOp_Misc, VERR_GENERAL_FAILURE,
                              "NtQueryInformationProcess/ProcessDefaultHardErrorMode failed: %#x\n", rcNt);
    if (fHardErr & PROCESS_HARDERR_CRITICAL_ERROR)
    {
        fHardErr &= ~PROCESS_HARDERR_CRITICAL_ERROR;
        rcNt = NtSetInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &fHardErr, sizeof(fHardErr));
        if (!NT_SUCCESS(rcNt))
            supR3HardenedFatalMsg("supR3HardenedWinInstallHooks", kSupInitOp_Misc, VERR_GENERAL_FAILURE,
                                  "NtSetInformationProcess/ProcessDefaultHardErrorMode failed: %#x\n", rcNt);
    }

    /*
     * Locate the routines first so we can allocate memory that's near enough.
     */
    PFNRT pfnNtCreateSection = supR3HardenedWinGetRealDllSymbol("ntdll.dll", "NtCreateSection");
    SUPR3HARDENED_ASSERT(pfnNtCreateSection != NULL);
    //SUPR3HARDENED_ASSERT(pfnNtCreateSection == (FARPROC)NtCreateSection);

    PFNRT pfnLdrLoadDll = supR3HardenedWinGetRealDllSymbol("ntdll.dll", "LdrLoadDll");
    SUPR3HARDENED_ASSERT(pfnLdrLoadDll != NULL);
    //SUPR3HARDENED_ASSERT(pfnLdrLoadDll == (FARPROC)LdrLoadDll);

    PFNRT pfnKiUserApcDispatcher = supR3HardenedWinGetRealDllSymbol("ntdll.dll", "KiUserApcDispatcher");
    SUPR3HARDENED_ASSERT(pfnKiUserApcDispatcher != NULL);
    g_pfnLdrInitializeThunk = (uintptr_t)supR3HardenedWinGetRealDllSymbol("ntdll.dll", "LdrInitializeThunk");
    SUPR3HARDENED_ASSERT(g_pfnLdrInitializeThunk != NULL);

#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
    PFNRT pfnKiUserExceptionDispatcher = supR3HardenedWinGetRealDllSymbol("ntdll.dll", "KiUserExceptionDispatcher");
    SUPR3HARDENED_ASSERT(pfnKiUserExceptionDispatcher != NULL);
#endif

    /*
     * Exec page setup & management.
     */
    uint32_t offExecPage = 0;
    memset(g_abSupHardReadWriteExecPage, 0xcc, PAGE_SIZE);

    /*
     * Hook #1 - NtCreateSection.
     * Purpose: Validate everything that can be mapped into the process before
     *          it's mapped and we still have a file handle to work with.
     */
    uint8_t * const pbNtCreateSection = (uint8_t *)(uintptr_t)pfnNtCreateSection;
    g_pbNtCreateSection = pbNtCreateSection;
    memcpy(g_abNtCreateSectionPatch, pbNtCreateSection, sizeof(g_abNtCreateSectionPatch));

    g_pfnNtCreateSectionReal = NtCreateSection; /* our direct syscall */

#ifdef RT_ARCH_AMD64
    /*
     * Patch 64-bit hosts.
     */
    /* Pattern #1: XP64/W2K3-64 thru Windows 8.1
       0:000> u ntdll!NtCreateSection
       ntdll!NtCreateSection:
       00000000`779f1750 4c8bd1          mov     r10,rcx
       00000000`779f1753 b847000000      mov     eax,47h
       00000000`779f1758 0f05            syscall
       00000000`779f175a c3              ret
       00000000`779f175b 0f1f440000      nop     dword ptr [rax+rax]
       The variant is the value loaded into eax: W2K3=??, Vista=47h?, W7=47h, W80=48h, W81=49h */

    /* Assemble the patch. */
    g_abNtCreateSectionPatch[0]  = 0x48; /* mov rax, qword */
    g_abNtCreateSectionPatch[1]  = 0xb8;
    *(uint64_t *)&g_abNtCreateSectionPatch[2] = (uint64_t)supR3HardenedMonitor_NtCreateSection;
    g_abNtCreateSectionPatch[10] = 0xff; /* jmp rax */
    g_abNtCreateSectionPatch[11] = 0xe0;

#else
    /*
     * Patch 32-bit hosts.
     */
    /* Pattern #1: XP thru Windows 7
            kd> u ntdll!NtCreateSection
            ntdll!NtCreateSection:
            7c90d160 b832000000      mov     eax,32h
            7c90d165 ba0003fe7f      mov     edx,offset SharedUserData!SystemCallStub (7ffe0300)
            7c90d16a ff12            call    dword ptr [edx]
            7c90d16c c21c00          ret     1Ch
            7c90d16f 90              nop
       The variable bit is the value loaded into eax: XP=32h, W2K3=34h, Vista=4bh, W7=54h

       Pattern #2: Windows 8.1
            0:000:x86> u ntdll_6a0f0000!NtCreateSection
            ntdll_6a0f0000!NtCreateSection:
            6a15eabc b854010000      mov     eax,154h
            6a15eac1 e803000000      call    ntdll_6a0f0000!NtCreateSection+0xd (6a15eac9)
            6a15eac6 c21c00          ret     1Ch
            6a15eac9 8bd4            mov     edx,esp
            6a15eacb 0f34            sysenter
            6a15eacd c3              ret
       The variable bit is the value loaded into eax: W81=154h */

    /* Assemble the patch. */
    g_abNtCreateSectionPatch[0] = 0xe9;  /* jmp rel32 */
    *(uint32_t *)&g_abNtCreateSectionPatch[1] = (uintptr_t)supR3HardenedMonitor_NtCreateSection
                                              - (uintptr_t)&pbNtCreateSection[1+4];

#endif

    /*
     * Hook #2 - LdrLoadDll
     * Purpose: (a) Enforce LdrLoadDll search path constraints, and (b) pre-validate
     *          DLLs so we can avoid calling WinVerifyTrust from the first hook,
     *          and thus avoiding messing up the loader data on some installations.
     *
     * This differs from the above function in that is no a system call and
     * we're at the mercy of the compiler.
     */
    uint8_t * const pbLdrLoadDll = (uint8_t *)(uintptr_t)pfnLdrLoadDll;
    g_pbLdrLoadDll = pbLdrLoadDll;
    memcpy(g_abLdrLoadDllPatch, pbLdrLoadDll, sizeof(g_abLdrLoadDllPatch));

    DISSTATE Dis;
    uint32_t cbInstr;
    uint32_t offJmpBack = 0;

#ifdef RT_ARCH_AMD64
    /*
     * Patch 64-bit hosts.
     */
    /* Just use the disassembler to skip 12 bytes or more. */
    while (offJmpBack < 12)
    {
        cbInstr = 1;
        int rc = DISInstr(pbLdrLoadDll + offJmpBack, DISCPUMODE_64BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW))
            || (Dis.x86.ModRM.Bits.Mod == 0 && Dis.x86.ModRM.Bits.Rm == 5 /* wrt RIP */) )
            supR3HardenedWinHookFailed("LdrLoadDll", pbLdrLoadDll);
        offJmpBack += cbInstr;
    }

    /* Assemble the code for resuming the call.*/
    *(PFNRT *)&g_pfnLdrLoadDllReal = (PFNRT)(uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage];

    memcpy(&g_abSupHardReadWriteExecPage[offExecPage], pbLdrLoadDll, offJmpBack);
    offExecPage += offJmpBack;

    g_abSupHardReadWriteExecPage[offExecPage++] = 0xff; /* jmp qword [$+8 wrt RIP] */
    g_abSupHardReadWriteExecPage[offExecPage++] = 0x25;
    *(uint32_t *)&g_abSupHardReadWriteExecPage[offExecPage] = RT_ALIGN_32(offExecPage + 4, 8) - (offExecPage + 4);
    offExecPage = RT_ALIGN_32(offExecPage + 4, 8);
    *(uint64_t *)&g_abSupHardReadWriteExecPage[offExecPage] = (uintptr_t)&pbLdrLoadDll[offJmpBack];
    offExecPage = RT_ALIGN_32(offExecPage + 8, 16);

    /* Assemble the LdrLoadDll patch. */
    Assert(offJmpBack >= 12);
    g_abLdrLoadDllPatch[0]  = 0x48; /* mov rax, qword */
    g_abLdrLoadDllPatch[1]  = 0xb8;
    *(uint64_t *)&g_abLdrLoadDllPatch[2] = (uint64_t)supR3HardenedMonitor_LdrLoadDll;
    g_abLdrLoadDllPatch[10] = 0xff; /* jmp rax */
    g_abLdrLoadDllPatch[11] = 0xe0;

#else
    /*
     * Patch 32-bit hosts.
     */
    /* Just use the disassembler to skip 5 bytes or more. */
    while (offJmpBack < 5)
    {
        cbInstr = 1;
        int rc = DISInstr(pbLdrLoadDll + offJmpBack, DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW)) )
            supR3HardenedWinHookFailed("LdrLoadDll", pbLdrLoadDll);
        offJmpBack += cbInstr;
    }

    /* Assemble the code for resuming the call.*/
    *(PFNRT *)&g_pfnLdrLoadDllReal = (PFNRT)(uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage];

    memcpy(&g_abSupHardReadWriteExecPage[offExecPage], pbLdrLoadDll, offJmpBack);
    offExecPage += offJmpBack;

    g_abSupHardReadWriteExecPage[offExecPage++] = 0xe9; /* jmp rel32 */
    *(uint32_t *)&g_abSupHardReadWriteExecPage[offExecPage] = (uintptr_t)&pbLdrLoadDll[offJmpBack]
                                                            - (uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage + 4];
    offExecPage = RT_ALIGN_32(offExecPage + 4, 16);

    /* Assemble the LdrLoadDll patch. */
    memcpy(g_abLdrLoadDllPatch, pbLdrLoadDll, sizeof(g_abLdrLoadDllPatch));
    Assert(offJmpBack >= 5);
    g_abLdrLoadDllPatch[0] = 0xe9;
    *(uint32_t *)&g_abLdrLoadDllPatch[1] = (uintptr_t)supR3HardenedMonitor_LdrLoadDll - (uintptr_t)&pbLdrLoadDll[1+4];
#endif

    /*
     * Hook #3 - KiUserApcDispatcher
     * Purpose: Prevent user APC to memory we (or our parent) has freed from
     *          crashing the process.  Also ensures no code injection via user
     *          APC during process init given the way we're vetting the APCs.
     *
     * This differs from the first function in that is no a system call and
     * we're at the mercy of the handwritten assembly.
     *
     * Note! We depend on all waits up past the patching to be non-altertable,
     *       otherwise an APC might slip by us.
     */
    uint8_t * const pbKiUserApcDispatcher = (uint8_t *)(uintptr_t)pfnKiUserApcDispatcher;
    g_pbKiUserApcDispatcher = pbKiUserApcDispatcher;
    memcpy(g_abKiUserApcDispatcherPatch, pbKiUserApcDispatcher, sizeof(g_abKiUserApcDispatcherPatch));

#ifdef RT_ARCH_AMD64
    /*
     * Patch 64-bit hosts.
     */
    /* Just use the disassembler to skip 12 bytes or more. */
    offJmpBack = 0;
    while (offJmpBack < 12)
    {
        cbInstr = 1;
        int rc = DISInstr(pbKiUserApcDispatcher + offJmpBack, DISCPUMODE_64BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW))
            || (Dis.x86.ModRM.Bits.Mod == 0 && Dis.x86.ModRM.Bits.Rm == 5 /* wrt RIP */) )
            supR3HardenedWinHookFailed("KiUserApcDispatcher", pbKiUserApcDispatcher);
        offJmpBack += cbInstr;
    }

    /* Assemble the code for resuming the call.*/
    *(PFNRT *)&g_pfnKiUserApcDispatcherReal = (PFNRT)(uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage];

    memcpy(&g_abSupHardReadWriteExecPage[offExecPage], pbKiUserApcDispatcher, offJmpBack);
    offExecPage += offJmpBack;

    g_abSupHardReadWriteExecPage[offExecPage++] = 0xff; /* jmp qword [$+8 wrt RIP] */
    g_abSupHardReadWriteExecPage[offExecPage++] = 0x25;
    *(uint32_t *)&g_abSupHardReadWriteExecPage[offExecPage] = RT_ALIGN_32(offExecPage + 4, 8) - (offExecPage + 4);
    offExecPage = RT_ALIGN_32(offExecPage + 4, 8);
    *(uint64_t *)&g_abSupHardReadWriteExecPage[offExecPage] = (uintptr_t)&pbKiUserApcDispatcher[offJmpBack];
    offExecPage = RT_ALIGN_32(offExecPage + 8, 16);

    /* Assemble the KiUserApcDispatcher patch. */
    Assert(offJmpBack >= 12);
    g_abKiUserApcDispatcherPatch[0]  = 0x48; /* mov rax, qword */
    g_abKiUserApcDispatcherPatch[1]  = 0xb8;
    *(uint64_t *)&g_abKiUserApcDispatcherPatch[2] = (uint64_t)supR3HardenedMonitor_KiUserApcDispatcher;
    g_abKiUserApcDispatcherPatch[10] = 0xff; /* jmp rax */
    g_abKiUserApcDispatcherPatch[11] = 0xe0;

#else
    /*
     * Patch 32-bit hosts.
     */
    /* Just use the disassembler to skip 5 bytes or more. */
    offJmpBack = 0;
    while (offJmpBack < 5)
    {
        cbInstr = 1;
        int rc = DISInstr(pbKiUserApcDispatcher + offJmpBack, DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW)) )
            supR3HardenedWinHookFailed("KiUserApcDispatcher", pbKiUserApcDispatcher);
        offJmpBack += cbInstr;
    }

    /* Assemble the code for resuming the call.*/
    *(PFNRT *)&g_pfnKiUserApcDispatcherReal = (PFNRT)(uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage];

    memcpy(&g_abSupHardReadWriteExecPage[offExecPage], pbKiUserApcDispatcher, offJmpBack);
    offExecPage += offJmpBack;

    g_abSupHardReadWriteExecPage[offExecPage++] = 0xe9; /* jmp rel32 */
    *(uint32_t *)&g_abSupHardReadWriteExecPage[offExecPage] = (uintptr_t)&pbKiUserApcDispatcher[offJmpBack]
                                                            - (uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage + 4];
    offExecPage = RT_ALIGN_32(offExecPage + 4, 16);

    /* Assemble the KiUserApcDispatcher patch. */
    memcpy(g_abKiUserApcDispatcherPatch, pbKiUserApcDispatcher, sizeof(g_abKiUserApcDispatcherPatch));
    Assert(offJmpBack >= 5);
    g_abKiUserApcDispatcherPatch[0] = 0xe9;
    *(uint32_t *)&g_abKiUserApcDispatcherPatch[1] = (uintptr_t)supR3HardenedMonitor_KiUserApcDispatcher - (uintptr_t)&pbKiUserApcDispatcher[1+4];
#endif

#ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
    /*
     * Hook #4 - KiUserExceptionDispatcher
     * Purpose: Logging crashes.
     *
     * This differs from the first function in that is no a system call and
     * we're at the mercy of the handwritten assembly.  This is not mandatory,
     * so we ignore failures here.
     */
    uint8_t * const pbKiUserExceptionDispatcher = (uint8_t *)(uintptr_t)pfnKiUserExceptionDispatcher;
    g_pbKiUserExceptionDispatcher = pbKiUserExceptionDispatcher;
    memcpy(g_abKiUserExceptionDispatcherPatch, pbKiUserExceptionDispatcher, sizeof(g_abKiUserExceptionDispatcherPatch));

# ifdef RT_ARCH_AMD64
    /*
     * Patch 64-bit hosts.
     *
     * Assume the following sequence and replacing the loaded Wow64PrepareForException
     * function pointer with our callback:
     *      cld
     *      mov  rax, Wow64PrepareForException ; Wow64PrepareForException(PCONTEXT, PEXCEPTION_RECORD)
     *      test rax, rax
     *      jz   skip_wow64_callout
     *      <do_callout_thru_rax>
     * (We're not a WOW64 process, so the callout should normally never happen.)
     */
    if (   pbKiUserExceptionDispatcher[ 0] == 0xfc /* CLD */
        && pbKiUserExceptionDispatcher[ 1] == 0x48 /* MOV RAX, symbol wrt rip */
        && pbKiUserExceptionDispatcher[ 2] == 0x8b
        && pbKiUserExceptionDispatcher[ 3] == 0x05
        && pbKiUserExceptionDispatcher[ 8] == 0x48 /* TEST RAX, RAX */
        && pbKiUserExceptionDispatcher[ 9] == 0x85
        && pbKiUserExceptionDispatcher[10] == 0xc0
        && pbKiUserExceptionDispatcher[11] == 0x74)
    {
        /* Assemble the KiUserExceptionDispatcher patch. */
        g_abKiUserExceptionDispatcherPatch[1]  = 0x48; /* MOV RAX, supR3HardenedMonitor_KiUserExceptionDispatcher */
        g_abKiUserExceptionDispatcherPatch[2]  = 0xb8;
        *(uint64_t *)&g_abKiUserExceptionDispatcherPatch[3] = (uint64_t)supR3HardenedMonitor_KiUserExceptionDispatcher;
        g_abKiUserExceptionDispatcherPatch[11] = 0x90; /* NOP (was JZ) */
        g_abKiUserExceptionDispatcherPatch[12] = 0x90; /* NOP (was DISP8 of JZ) */
    }
    else
        SUP_DPRINTF(("supR3HardenedWinInstallHooks: failed to patch KiUserExceptionDispatcher (%.20Rhxs)\n",
                     pbKiUserExceptionDispatcher));
# else
    /*
     * Patch 32-bit hosts.
     */
    /* Just use the disassembler to skip 5 bytes or more. */
    offJmpBack = 0;
    while (offJmpBack < 5)
    {
        cbInstr = 1;
        int rc = DISInstr(pbKiUserExceptionDispatcher + offJmpBack, DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (   RT_FAILURE(rc)
            || (Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW)) )
        {
            SUP_DPRINTF(("supR3HardenedWinInstallHooks: failed to patch KiUserExceptionDispatcher (off %#x in %.20Rhxs)\n",
                         offJmpBack, pbKiUserExceptionDispatcher));
            break;
        }
        offJmpBack += cbInstr;
    }
    if (offJmpBack >= 5)
    {
        /* Assemble the code for resuming the call.*/
        *(PFNRT *)&g_pfnKiUserExceptionDispatcherReal = (PFNRT)(uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage];

        memcpy(&g_abSupHardReadWriteExecPage[offExecPage], pbKiUserExceptionDispatcher, offJmpBack);
        offExecPage += offJmpBack;

        g_abSupHardReadWriteExecPage[offExecPage++] = 0xe9; /* jmp rel32 */
        *(uint32_t *)&g_abSupHardReadWriteExecPage[offExecPage] = (uintptr_t)&pbKiUserExceptionDispatcher[offJmpBack]
                                                                - (uintptr_t)&g_abSupHardReadWriteExecPage[offExecPage + 4];
        offExecPage = RT_ALIGN_32(offExecPage + 4, 16);

        /* Assemble the KiUserExceptionDispatcher patch. */
        memcpy(g_abKiUserExceptionDispatcherPatch, pbKiUserExceptionDispatcher, sizeof(g_abKiUserExceptionDispatcherPatch));
        Assert(offJmpBack >= 5);
        g_abKiUserExceptionDispatcherPatch[0] = 0xe9;
        *(uint32_t *)&g_abKiUserExceptionDispatcherPatch[1] = (uintptr_t)supR3HardenedMonitor_KiUserExceptionDispatcher - (uintptr_t)&pbKiUserExceptionDispatcher[1+4];
    }
# endif
#endif /* !VBOX_WITHOUT_HARDENDED_XCPT_LOGGING */

    /*
     * Seal the rwx page.
     */
    SUPR3HARDENED_ASSERT_NT_SUCCESS(supR3HardenedWinProtectMemory(g_abSupHardReadWriteExecPage, PAGE_SIZE, PAGE_EXECUTE_READ));

    /*
     * Install the patches.
     */
    supR3HardenedWinReInstallHooks(true /*fFirstCall*/);
}






/*
 *
 * T h r e a d   c r e a t i o n   c o n t r o l
 * T h r e a d   c r e a t i o n   c o n t r o l
 * T h r e a d   c r e a t i o n   c o n t r o l
 *
 */


/**
 * Common code used for child and parent to make new threads exit immediately.
 *
 * This patches the LdrInitializeThunk code to call NtTerminateThread with
 * STATUS_SUCCESS instead of doing the NTDLL initialization.
 *
 * @returns VBox status code.
 * @param   hProcess            The process to do this to.
 * @param   pvLdrInitThunk      The address of the LdrInitializeThunk code to
 *                              override.
 * @param   pvNtTerminateThread The address of the NtTerminateThread function in
 *                              the NTDLL instance we're patching.  (Must be +/-
 *                              2GB from the thunk code.)
 * @param   pabBackup           Where to back up the original instruction bytes
 *                              at pvLdrInitThunk.
 * @param   cbBackup            The size of the backup area. Must be 16 bytes.
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 */
static int supR3HardNtDisableThreadCreationEx(HANDLE hProcess, void *pvLdrInitThunk, void *pvNtTerminateThread,
                                              uint8_t *pabBackup, size_t cbBackup, PRTERRINFO pErrInfo)
{
    SUP_DPRINTF(("supR3HardNtDisableThreadCreation: pvLdrInitThunk=%p pvNtTerminateThread=%p\n", pvLdrInitThunk, pvNtTerminateThread));
    SUPR3HARDENED_ASSERT(cbBackup == 16);
    SUPR3HARDENED_ASSERT(RT_ABS((intptr_t)pvLdrInitThunk - (intptr_t)pvNtTerminateThread) < 16*_1M);

    /*
     * Back up the thunk code.
     */
    SIZE_T  cbIgnored;
    NTSTATUS rcNt = NtReadVirtualMemory(hProcess, pvLdrInitThunk, pabBackup, cbBackup, &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtDisableThreadCreation: NtReadVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    /*
     * Cook up replacement code that calls NtTerminateThread.
     */
    uint8_t abReplacement[16];
    memcpy(abReplacement, pabBackup, sizeof(abReplacement));

#ifdef RT_ARCH_AMD64
    abReplacement[0] = 0x31;    /* xor ecx, ecx */
    abReplacement[1] = 0xc9;
    abReplacement[2] = 0x31;    /* xor edx, edx */
    abReplacement[3] = 0xd2;
    abReplacement[4] = 0xe8;    /* call near NtTerminateThread */
    *(int32_t *)&abReplacement[5] = (int32_t)((uintptr_t)pvNtTerminateThread - ((uintptr_t)pvLdrInitThunk + 9));
    abReplacement[9] = 0xcc;    /* int3 */
#elif defined(RT_ARCH_X86)
    abReplacement[0] = 0x6a;    /* push 0 */
    abReplacement[1] = 0x00;
    abReplacement[2] = 0x6a;    /* push 0 */
    abReplacement[3] = 0x00;
    abReplacement[4] = 0xe8;    /* call near NtTerminateThread */
    *(int32_t *)&abReplacement[5] = (int32_t)((uintptr_t)pvNtTerminateThread - ((uintptr_t)pvLdrInitThunk + 9));
    abReplacement[9] = 0xcc;    /* int3 */
#else
# error "Unsupported arch."
#endif

    /*
     * Install the replacment code.
     */
    PVOID  pvProt   = pvLdrInitThunk;
    SIZE_T cbProt   = cbBackup;
    ULONG  fOldProt = 0;
    rcNt = NtProtectVirtualMemory(hProcess, &pvProt, &cbProt, PAGE_EXECUTE_READWRITE, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtDisableThreadCreationEx: NtProtectVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    rcNt = NtWriteVirtualMemory(hProcess, pvLdrInitThunk, abReplacement, sizeof(abReplacement), &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtDisableThreadCreationEx: NtWriteVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    pvProt   = pvLdrInitThunk;
    cbProt   = cbBackup;
    rcNt = NtProtectVirtualMemory(hProcess, &pvProt, &cbProt, fOldProt, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtDisableThreadCreationEx: NtProtectVirtualMemory/LdrInitializeThunk/2 failed: %#x", rcNt);

    return VINF_SUCCESS;
}


/**
 * Undo the effects of supR3HardNtDisableThreadCreationEx.
 *
 * @returns VBox status code.
 * @param   hProcess            The process to do this to.
 * @param   pvLdrInitThunk      The address of the LdrInitializeThunk code to
 *                              override.
 * @param   pabBackup           Where to back up the original instruction bytes
 *                              at pvLdrInitThunk.
 * @param   cbBackup            The size of the backup area. Must be 16 bytes.
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 */
static int supR3HardNtEnableThreadCreationEx(HANDLE hProcess, void *pvLdrInitThunk, uint8_t const *pabBackup, size_t cbBackup,
                                             PRTERRINFO pErrInfo)
{
    SUP_DPRINTF(("supR3HardNtEnableThreadCreationEx:\n"));
    SUPR3HARDENED_ASSERT(cbBackup == 16);

    PVOID  pvProt   = pvLdrInitThunk;
    SIZE_T cbProt   = cbBackup;
    ULONG  fOldProt = 0;
    NTSTATUS rcNt = NtProtectVirtualMemory(hProcess, &pvProt, &cbProt, PAGE_EXECUTE_READWRITE, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtEnableThreadCreationEx: NtProtectVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    SIZE_T cbIgnored;
    rcNt = NtWriteVirtualMemory(hProcess, pvLdrInitThunk, pabBackup, cbBackup, &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtEnableThreadCreationEx: NtWriteVirtualMemory/LdrInitializeThunk[restore] failed: %#x",
                             rcNt);

    pvProt   = pvLdrInitThunk;
    cbProt   = cbBackup;
    rcNt = NtProtectVirtualMemory(hProcess, &pvProt, &cbProt, fOldProt, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                             "supR3HardNtEnableThreadCreationEx: NtProtectVirtualMemory/LdrInitializeThunk[restore] failed: %#x",
                             rcNt);

    return VINF_SUCCESS;
}


/**
 * Disable thread creation for the current process.
 *
 * @remarks Doesn't really disables it, just makes the threads exit immediately
 *          without executing any real code.
 */
static void supR3HardenedWinDisableThreadCreation(void)
{
    /* Cannot use the imported NtTerminateThread as it's pointing to our own
       syscall assembly code. */
    static PFNRT s_pfnNtTerminateThread = NULL;
    if (s_pfnNtTerminateThread == NULL)
        s_pfnNtTerminateThread = supR3HardenedWinGetRealDllSymbol("ntdll.dll", "NtTerminateThread");
    SUPR3HARDENED_ASSERT(s_pfnNtTerminateThread);

    int rc = supR3HardNtDisableThreadCreationEx(NtCurrentProcess(),
                                                (void *)(uintptr_t)&LdrInitializeThunk,
                                                (void *)(uintptr_t)s_pfnNtTerminateThread,
                                                g_abLdrInitThunkSelfBackup, sizeof(g_abLdrInitThunkSelfBackup),
                                                NULL /* pErrInfo*/);
    g_fSupInitThunkSelfPatched = RT_SUCCESS(rc);
}


/**
 * Undoes the effects of supR3HardenedWinDisableThreadCreation.
 */
DECLHIDDEN(void) supR3HardenedWinEnableThreadCreation(void)
{
    if (g_fSupInitThunkSelfPatched)
    {
        int rc = supR3HardNtEnableThreadCreationEx(NtCurrentProcess(),
                                                   (void *)(uintptr_t)&LdrInitializeThunk,
                                                   g_abLdrInitThunkSelfBackup, sizeof(g_abLdrInitThunkSelfBackup),
                                                   RTErrInfoInitStatic(&g_ErrInfoStatic));
        if (RT_FAILURE(rc))
            supR3HardenedError(rc, true /*fFatal*/, "%s", g_ErrInfoStatic.szMsg);
        g_fSupInitThunkSelfPatched = false;
    }
}




/*
 *
 * R e s p a w n
 * R e s p a w n
 * R e s p a w n
 *
 */


/**
 * Gets the SID of the user associated with the process.
 *
 * @returns @c true if we've got a login SID, @c false if not.
 * @param   pSidUser            Where to return the user SID.
 * @param   cbSidUser           The size of the user SID buffer.
 * @param   pSidLogin           Where to return the login SID.
 * @param   cbSidLogin          The size of the login SID buffer.
 */
static bool supR3HardNtChildGetUserAndLogSids(PSID pSidUser, ULONG cbSidUser, PSID pSidLogin, ULONG cbSidLogin)
{
    HANDLE hToken;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &hToken));
    union
    {
        TOKEN_USER      UserInfo;
        TOKEN_GROUPS    Groups;
        uint8_t         abPadding[4096];
    } uBuf;
    ULONG cbRet = 0;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(NtQueryInformationToken(hToken, TokenUser, &uBuf, sizeof(uBuf), &cbRet));
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlCopySid(cbSidUser, pSidUser, uBuf.UserInfo.User.Sid));

    bool fLoginSid = false;
    NTSTATUS rcNt = NtQueryInformationToken(hToken, TokenLogonSid, &uBuf, sizeof(uBuf), &cbRet);
    if (NT_SUCCESS(rcNt))
    {
        for (DWORD i = 0; i < uBuf.Groups.GroupCount; i++)
            if ((uBuf.Groups.Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            {
                SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlCopySid(cbSidLogin, pSidLogin, uBuf.Groups.Groups[i].Sid));
                fLoginSid = true;
                break;
            }
    }

    SUPR3HARDENED_ASSERT_NT_SUCCESS(NtClose(hToken));

    return fLoginSid;
}


/**
 * Build security attributes for the process or the primary thread (@a fProcess)
 *
 * Process DACLs can be bypassed using the SeDebugPrivilege (generally available
 * to admins, i.e. normal windows users), or by taking ownership and/or
 * modifying the DACL.  However, it restricts
 *
 * @param   pSecAttrs           Where to return the security attributes.
 * @param   pCleanup            Cleanup record.
 * @param   fProcess            Set if it's for the process, clear if it's for
 *                              the primary thread.
 */
static void supR3HardNtChildInitSecAttrs(PSECURITY_ATTRIBUTES pSecAttrs, PMYSECURITYCLEANUP pCleanup, bool fProcess)
{
    /*
     * Safe return values.
     */
    suplibHardenedMemSet(pCleanup, 0, sizeof(*pCleanup));

    pSecAttrs->nLength              = sizeof(*pSecAttrs);
    pSecAttrs->bInheritHandle       = FALSE;
    pSecAttrs->lpSecurityDescriptor = NULL;

/** @todo This isn't at all complete, just sketches... */

    /*
     * Create an ACL detailing the access of the above groups.
     */
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlCreateAcl(&pCleanup->Acl.AclHdr, sizeof(pCleanup->Acl), ACL_REVISION));

    ULONG fDeny  = DELETE | WRITE_DAC | WRITE_OWNER;
    ULONG fAllow = SYNCHRONIZE | READ_CONTROL;
    ULONG fAllowLogin = SYNCHRONIZE | READ_CONTROL;
    if (fProcess)
    {
        fDeny       |= PROCESS_CREATE_THREAD | PROCESS_SET_SESSIONID | PROCESS_VM_OPERATION | PROCESS_VM_WRITE
                    |  PROCESS_CREATE_PROCESS | PROCESS_DUP_HANDLE | PROCESS_SET_QUOTA
                    |  PROCESS_SET_INFORMATION | PROCESS_SUSPEND_RESUME;
        fAllow      |= PROCESS_TERMINATE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
        fAllowLogin |= PROCESS_TERMINATE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
        if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0)) /* Introduced in Vista. */
        {
            fAllow      |= PROCESS_QUERY_LIMITED_INFORMATION;
            fAllowLogin |= PROCESS_QUERY_LIMITED_INFORMATION;
        }
        if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 3)) /* Introduced in Windows 8.1. */
            fAllow  |= PROCESS_SET_LIMITED_INFORMATION;
    }
    else
    {
        fDeny       |= THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_SET_INFORMATION | THREAD_SET_THREAD_TOKEN
                    |  THREAD_IMPERSONATE | THREAD_DIRECT_IMPERSONATION;
        fAllow      |= THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;
        fAllowLogin |= THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;
        if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0)) /* Introduced in Vista. */
        {
            fAllow      |= THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION;
            fAllowLogin |= THREAD_QUERY_LIMITED_INFORMATION;
        }

    }
    fDeny |= ~fAllow & (SPECIFIC_RIGHTS_ALL | STANDARD_RIGHTS_ALL);

    /* Deny everyone access to bad bits. */
#if 1
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlInitializeSid(&pCleanup->Everyone.Sid, &SIDAuthWorld, 1));
    *RtlSubAuthoritySid(&pCleanup->Everyone.Sid, 0) = SECURITY_WORLD_RID;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessDeniedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                          fDeny, &pCleanup->Everyone.Sid));
#endif

#if 0
    /* Grant some access to the owner - doesn't work. */
    SID_IDENTIFIER_AUTHORITY SIDAuthCreator = SECURITY_CREATOR_SID_AUTHORITY;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlInitializeSid(&pCleanup->Owner.Sid, &SIDAuthCreator, 1));
    *RtlSubAuthoritySid(&pCleanup->Owner.Sid, 0) = SECURITY_CREATOR_OWNER_RID;

    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessDeniedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                          fDeny, &pCleanup->Owner.Sid));
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessAllowedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                           fAllow, &pCleanup->Owner.Sid));
#endif

#if 1
    bool fHasLoginSid = supR3HardNtChildGetUserAndLogSids(&pCleanup->User.Sid, sizeof(pCleanup->User),
                                                          &pCleanup->Login.Sid, sizeof(pCleanup->Login));

# if 1
    /* Grant minimal access to the user. */
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessDeniedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                          fDeny, &pCleanup->User.Sid));
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessAllowedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                           fAllow, &pCleanup->User.Sid));
# endif

# if 1
    /* Grant very limited access to the login sid. */
    if (fHasLoginSid)
    {
        SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlAddAccessAllowedAce(&pCleanup->Acl.AclHdr, ACL_REVISION,
                                                               fAllowLogin, &pCleanup->Login.Sid));
    }
# endif

#endif

    /*
     * Create a security descriptor with the above ACL.
     */
    PSECURITY_DESCRIPTOR pSecDesc = (PSECURITY_DESCRIPTOR)RTMemAllocZ(SECURITY_DESCRIPTOR_MIN_LENGTH);
    pCleanup->pSecDesc = pSecDesc;

    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlCreateSecurityDescriptor(pSecDesc, SECURITY_DESCRIPTOR_REVISION));
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlSetDaclSecurityDescriptor(pSecDesc, TRUE /*fDaclPresent*/, &pCleanup->Acl.AclHdr,
                                                                 FALSE /*fDaclDefaulted*/));
    pSecAttrs->lpSecurityDescriptor = pSecDesc;
}


/**
 * Predicate function which tests whether @a ch is a argument separator
 * character.
 *
 * @returns True/false.
 * @param   ch                  The character to examine.
 */
DECLINLINE(bool) suplibCommandLineIsArgSeparator(int ch)
{
    return ch == ' '
        || ch == '\t'
        || ch == '\n'
        || ch == '\r';
}


/**
 * Construct the new command line.
 *
 * Since argc/argv are both derived from GetCommandLineW (see
 * suplibHardenedWindowsMain), we skip the argument by argument UTF-8 -> UTF-16
 * conversion and quoting by going to the original source.
 *
 * The executable name, though, is replaced in case it's not a fullly
 * qualified path.
 *
 * The re-spawn indicator is added immediately after the executable name
 * so that we don't get tripped up missing close quote chars in the last
 * argument.
 *
 * @returns Pointer to a command line string (heap).
 * @param   pString         Unicode string structure to initialize to the
 *                          command line. Optional.
 * @param   iWhich          Which respawn we're to check for, 1 being the first
 *                          one, and 2 the second and final.
 */
static PRTUTF16 supR3HardNtChildConstructCmdLine(PUNICODE_STRING pString, int iWhich)
{
    SUPR3HARDENED_ASSERT(iWhich == 1 || iWhich == 2);

    /*
     * Get the command line and skip the executable name.
     */
    PUNICODE_STRING pCmdLineStr = &NtCurrentPeb()->ProcessParameters->CommandLine;
    PCRTUTF16 pawcArgs = pCmdLineStr->Buffer;
    uint32_t  cwcArgs  = pCmdLineStr->Length / sizeof(WCHAR);

    /* Skip leading space (shouldn't be any, but whatever). */
    while (cwcArgs > 0 && suplibCommandLineIsArgSeparator(*pawcArgs) )
        cwcArgs--, pawcArgs++;
    SUPR3HARDENED_ASSERT(cwcArgs > 0 && *pawcArgs != '\0');

    /* Walk to the end of it. */
    int fQuoted = false;
    do
    {
        if (*pawcArgs == '"')
        {
            fQuoted = !fQuoted;
            cwcArgs--; pawcArgs++;
        }
        else if (*pawcArgs != '\\' || (pawcArgs[1] != '\\' && pawcArgs[1] != '"'))
            cwcArgs--, pawcArgs++;
        else
        {
            unsigned cSlashes = 0;
            do
            {
                cSlashes++;
                cwcArgs--;
                pawcArgs++;
            }
            while (cwcArgs > 0 && *pawcArgs == '\\');
            if (cwcArgs > 0 && *pawcArgs == '"' && (cSlashes & 1))
                cwcArgs--, pawcArgs++; /* odd number of slashes == escaped quote */
        }
    } while (cwcArgs > 0 && (fQuoted || !suplibCommandLineIsArgSeparator(*pawcArgs)));

    /* Skip trailing spaces. */
    while (cwcArgs > 0 && suplibCommandLineIsArgSeparator(*pawcArgs))
        cwcArgs--, pawcArgs++;

    /*
     * Allocate a new buffer.
     */
    AssertCompile(sizeof(SUPR3_RESPAWN_1_ARG0) == sizeof(SUPR3_RESPAWN_2_ARG0));
    size_t cwcCmdLine = (sizeof(SUPR3_RESPAWN_1_ARG0) - 1) / sizeof(SUPR3_RESPAWN_1_ARG0[0]) /* Respawn exe name. */
                      + !!cwcArgs + cwcArgs; /* if arguments present, add space + arguments. */
    if (cwcCmdLine * sizeof(WCHAR) >= 0xfff0)
        supR3HardenedFatalMsg("supR3HardNtChildConstructCmdLine", kSupInitOp_Misc, VERR_OUT_OF_RANGE,
                              "Command line is too long (%u chars)!", cwcCmdLine);

    PRTUTF16 pwszCmdLine = (PRTUTF16)RTMemAlloc((cwcCmdLine + 1) * sizeof(RTUTF16));
    SUPR3HARDENED_ASSERT(pwszCmdLine != NULL);

    /*
     * Construct the new command line.
     */
    PRTUTF16 pwszDst = pwszCmdLine;
    for (const char *pszSrc = iWhich == 1 ? SUPR3_RESPAWN_1_ARG0 : SUPR3_RESPAWN_2_ARG0; *pszSrc; pszSrc++)
        *pwszDst++ = *pszSrc;

    if (cwcArgs)
    {
        *pwszDst++ = ' ';
        suplibHardenedMemCopy(pwszDst, pawcArgs, cwcArgs * sizeof(RTUTF16));
        pwszDst += cwcArgs;
    }

    *pwszDst = '\0';
    SUPR3HARDENED_ASSERT((uintptr_t)(pwszDst - pwszCmdLine) == cwcCmdLine);

    if (pString)
    {
        pString->Buffer = pwszCmdLine;
        pString->Length = (USHORT)(cwcCmdLine * sizeof(WCHAR));
        pString->MaximumLength = pString->Length + sizeof(WCHAR);
    }
    return pwszCmdLine;
}


/**
 * Terminates the child process.
 *
 * @param   hProcess            The process handle.
 * @param   pszWhere            Who's having child rasing troubles.
 * @param   rc                  The status code to report.
 * @param   pszFormat           The message format string.
 * @param   ...                 Message format arguments.
 */
static void supR3HardenedWinKillChild(HANDLE hProcess, const char *pszWhere, int rc, const char *pszFormat, ...)
{
    /*
     * Terminate the process ASAP and display error.
     */
    NtTerminateProcess(hProcess, RTEXITCODE_FAILURE);

    va_list va;
    va_start(va, pszFormat);
    supR3HardenedErrorV(rc, false /*fFatal*/, pszFormat, va);
    va_end(va);

    /*
     * Wait for the process to really go away.
     */
    PROCESS_BASIC_INFORMATION BasicInfo;
    NTSTATUS rcNtExit = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    bool fExitOk = NT_SUCCESS(rcNtExit) && BasicInfo.ExitStatus != STATUS_PENDING;
    if (!fExitOk)
    {
        NTSTATUS rcNtWait;
        uint64_t uMsTsStart = supR3HardenedWinGetMilliTS();
        do
        {
            NtTerminateProcess(hProcess, DBG_TERMINATE_PROCESS);

            LARGE_INTEGER Timeout;
            Timeout.QuadPart = -20000000; /* 2 second */
            rcNtWait = NtWaitForSingleObject(hProcess, TRUE /*Alertable*/, &Timeout);

            rcNtExit = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
            fExitOk = NT_SUCCESS(rcNtExit) && BasicInfo.ExitStatus != STATUS_PENDING;
        } while (   !fExitOk
                 && (   rcNtWait == STATUS_TIMEOUT
                     || rcNtWait == STATUS_USER_APC
                     || rcNtWait == STATUS_ALERTED)
                 && supR3HardenedWinGetMilliTS() - uMsTsStart < 60 * 1000);
        if (fExitOk)
            supR3HardenedError(rc, false /*fFatal*/,
                               "NtDuplicateObject failed and we failed to kill child: rc=%u (%#x) rcNtWait=%#x hProcess=%p\n",
                               rc, rc, rcNtWait, hProcess);
    }

    /*
     * Final error message.
     */
    va_start(va, pszFormat);
    supR3HardenedFatalMsgV(pszWhere, kSupInitOp_Misc, rc, pszFormat, va);
    /* not reached */
}


/**
 * Checks the child process when hEvtParent is signalled.
 *
 * This will read the request data from the child and check it against expected
 * request.  If an error is signalled, we'll raise it and make sure the child
 * terminates before terminating the calling process.
 *
 * @param   pThis               The child process data structure.
 * @param   enmExpectedRequest  The expected child request.
 * @param   pszWhat             What we're waiting for.
 */
static void supR3HardNtChildProcessRequest(PSUPR3HARDNTCHILD pThis, SUPR3WINCHILDREQ enmExpectedRequest, const char *pszWhat)
{
    /*
     * Read the process parameters from the child.
     */
    uintptr_t           uChildAddr = (uintptr_t)pThis->Peb.ImageBaseAddress
                                   + ((uintptr_t)&g_ProcParams - (uintptr_t)NtCurrentPeb()->ImageBaseAddress);
    SIZE_T              cbIgnored  = 0;
    RT_ZERO(pThis->ProcParams);
    NTSTATUS rcNt = NtReadVirtualMemory(pThis->hProcess, (PVOID)uChildAddr,
                                        &pThis->ProcParams, sizeof(pThis->ProcParams), &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildProcessRequest", rcNt,
                                  "NtReadVirtualMemory(,%p,) failed reading child process status: %#x\n", uChildAddr, rcNt);

    /*
     * Is it the expected request?
     */
    if (pThis->ProcParams.enmRequest == enmExpectedRequest)
        return;

    /*
     * No, not the expected request. If it's an error request, tell the child
     * to terminate itself, otherwise we'll have to terminate it.
     */
    pThis->ProcParams.szErrorMsg[sizeof(pThis->ProcParams.szErrorMsg) - 1] = '\0';
    pThis->ProcParams.szWhere[sizeof(pThis->ProcParams.szWhere) - 1] = '\0';
    SUP_DPRINTF(("supR3HardenedWinCheckChild: enmRequest=%d rc=%d enmWhat=%d %s: %s\n",
                 pThis->ProcParams.enmRequest, pThis->ProcParams.rc, pThis->ProcParams.enmWhat,
                 pThis->ProcParams.szWhere, pThis->ProcParams.szErrorMsg));

    if (pThis->ProcParams.enmRequest != kSupR3WinChildReq_Error)
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinCheckChild", VERR_INVALID_PARAMETER,
                                  "Unexpected child request #%d. Was expecting #%d (%s).\n",
                                  pThis->ProcParams.enmRequest, enmExpectedRequest, pszWhat);

    rcNt = NtSetEvent(pThis->hEvtChild, NULL);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildProcessRequest", rcNt, "NtSetEvent failed: %#x\n", rcNt);

    /* Wait for it to terminate. */
    LARGE_INTEGER Timeout;
    Timeout.QuadPart = -50000000; /* 5 seconds */
    rcNt = NtWaitForSingleObject(pThis->hProcess, FALSE /*Alertable*/, &Timeout);
    if (rcNt != STATUS_WAIT_0)
    {
        SUP_DPRINTF(("supR3HardNtChildProcessRequest: Child is taking too long to quit (rcWait=%#x), killing it...\n", rcNt));
        NtTerminateProcess(pThis->hProcess, DBG_TERMINATE_PROCESS);
    }

    /*
     * Report the error in the same way as it occured in the guest.
     */
    if (pThis->ProcParams.enmWhat == kSupInitOp_Invalid)
        supR3HardenedFatalMsg("supR3HardenedWinCheckChild", kSupInitOp_Misc, pThis->ProcParams.rc,
                              "%s", pThis->ProcParams.szErrorMsg);
    else
        supR3HardenedFatalMsg(pThis->ProcParams.szWhere, pThis->ProcParams.enmWhat, pThis->ProcParams.rc,
                              "%s", pThis->ProcParams.szErrorMsg);
}


/**
 * Waits for the child to make a certain request or terminate.
 *
 * The stub process will also wait on it's parent to terminate.
 * This call will only return if the child made the expected request.
 *
 * @param   pThis               The child process data structure.
 * @param   enmExpectedRequest  The child request to wait for.
 * @param   cMsTimeout          The number of milliseconds to wait (at least).
 * @param   pszWhat             What we're waiting for.
 */
static void supR3HardNtChildWaitFor(PSUPR3HARDNTCHILD pThis, SUPR3WINCHILDREQ enmExpectedRequest, RTMSINTERVAL cMsTimeout,
                                    const char *pszWhat)
{
    /*
     * The wait loop.
     * Will return when the expected request arrives.
     * Will break out when one of the processes terminates.
     */
    NTSTATUS      rcNtWait;
    LARGE_INTEGER Timeout;
    uint64_t      uMsTsStart = supR3HardenedWinGetMilliTS();
    uint64_t      cMsElapsed = 0;
    for (;;)
    {
        /*
         * Assemble handles to wait for.
         */
        ULONG  cHandles = 1;
        HANDLE ahHandles[3];
        ahHandles[0] = pThis->hProcess;
        if (pThis->hEvtParent)
            ahHandles[cHandles++] = pThis->hEvtParent;
        if (pThis->hParent)
            ahHandles[cHandles++] = pThis->hParent;

        /*
         * Do the waiting according to the callers wishes.
         */
        if (   enmExpectedRequest == kSupR3WinChildReq_End
            || cMsTimeout  == RT_INDEFINITE_WAIT)
            rcNtWait = NtWaitForMultipleObjects(cHandles, &ahHandles[0], WaitAnyObject, TRUE /*Alertable*/, NULL /*Timeout*/);
        else
        {
            Timeout.QuadPart = -(int64_t)(cMsTimeout - cMsElapsed) * 10000;
            rcNtWait = NtWaitForMultipleObjects(cHandles, &ahHandles[0], WaitAnyObject, TRUE /*Alertable*/, &Timeout);
        }

        /*
         * Process child request.
         */
        if (rcNtWait == STATUS_WAIT_0 + 1 && pThis->hEvtParent != NULL)
        {
            supR3HardNtChildProcessRequest(pThis, enmExpectedRequest, pszWhat);
            SUP_DPRINTF(("supR3HardNtChildWaitFor: Found expected request %d (%s) after %llu ms.\n",
                         enmExpectedRequest, pszWhat, supR3HardenedWinGetMilliTS() - uMsTsStart));
            return; /* Expected request received. */
        }

        /*
         * Process termination?
         */
        if (   (ULONG)rcNtWait - (ULONG)STATUS_WAIT_0           < cHandles
            || (ULONG)rcNtWait - (ULONG)STATUS_ABANDONED_WAIT_0 < cHandles)
            break;

        /*
         * Check sanity.
         */
        if (   rcNtWait != STATUS_TIMEOUT
            && rcNtWait != STATUS_USER_APC
            && rcNtWait != STATUS_ALERTED)
            supR3HardenedWinKillChild(pThis, "supR3HardNtChildWaitFor", rcNtWait,
                                      "NtWaitForMultipleObjects returned %#x waiting for #%d (%s)\n",
                                      rcNtWait, enmExpectedRequest, pszWhat);

        /*
         * Calc elapsed time for the next timeout calculation, checking to see
         * if we've timed out already.
         */
        cMsElapsed = supR3HardenedWinGetMilliTS() - uMsTsStart;
        if (   cMsElapsed > cMsTimeout
            && cMsTimeout != RT_INDEFINITE_WAIT
            && enmExpectedRequest != kSupR3WinChildReq_End)
        {
            if (rcNtWait == STATUS_USER_APC || rcNtWait == STATUS_ALERTED)
                cMsElapsed = cMsTimeout - 1; /* try again */
            else
            {
                /* We timed out. */
                supR3HardenedWinKillChild(pThis, "supR3HardNtChildWaitFor", rcNtWait,
                                          "Timed out after %llu ms waiting for child request #%d (%s).\n",
                                          cMsElapsed, enmExpectedRequest, pszWhat);
            }
        }
    }

    /*
     * Proxy the termination code of the child, if it exited already.
     */
    PROCESS_BASIC_INFORMATION BasicInfo;
    NTSTATUS rcNt1 = NtQueryInformationProcess(pThis->hProcess, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    NTSTATUS rcNt2 = STATUS_PENDING;
    NTSTATUS rcNt3 = STATUS_PENDING;
    if (   !NT_SUCCESS(rcNt1)
        || BasicInfo.ExitStatus == STATUS_PENDING)
    {
        rcNt2 = NtTerminateProcess(pThis->hProcess, RTEXITCODE_FAILURE);
        Timeout.QuadPart = NT_SUCCESS(rcNt2) ? -20000000 /* 2 sec */ : -1280000 /* 128 ms */;
        rcNt3 = NtWaitForSingleObject(pThis->hProcess, FALSE /*Alertable*/, NULL /*Timeout*/);
        BasicInfo.ExitStatus = RTEXITCODE_FAILURE;
    }

    SUP_DPRINTF(("supR3HardNtChildWaitFor[%d]: Quitting: ExitCode=%#x (rcNtWait=%#x, rcNt1=%#x, rcNt2=%#x, rcNt3=%#x, %llu ms, %s);\n",
                 pThis->iWhich, BasicInfo.ExitStatus, rcNtWait, rcNt1, rcNt2, rcNt3,
                 supR3HardenedWinGetMilliTS() - uMsTsStart, pszWhat));
    suplibHardenedExit((RTEXITCODE)BasicInfo.ExitStatus);
}


/**
 * Closes full access child thread and process handles, making a harmless
 * duplicate of the process handle first.
 *
 * The hProcess member of the child process data structure will be change to the
 * harmless handle, while the hThread will be set to NULL.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildCloseFullAccessHandles(PSUPR3HARDNTCHILD pThis)
{
    /*
     * The thread handle.
     */
    NTSTATUS rcNt = NtClose(pThis->hThread);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinReSpawn", rcNt, "NtClose(hThread) failed: %#x", rcNt);
    pThis->hThread = NULL;

    /*
     * Duplicate the process handle into a harmless one.
     */
    HANDLE hProcWait;
    ULONG fRights = SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_VM_READ;
    if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0)) /* Introduced in Vista. */
        fRights |= PROCESS_QUERY_LIMITED_INFORMATION;
    else
        fRights |= PROCESS_QUERY_INFORMATION;
    rcNt = NtDuplicateObject(NtCurrentProcess(), pThis->hProcess,
                             NtCurrentProcess(), &hProcWait,
                             fRights, 0 /*HandleAttributes*/, 0);
    if (rcNt == STATUS_ACCESS_DENIED)
    {
        supR3HardenedError(rcNt, false /*fFatal*/,
                           "supR3HardenedWinDoReSpawn: NtDuplicateObject(,,,,%#x,,) -> %#x, retrying with only %#x...\n",
                           fRights, rcNt, SYNCHRONIZE);
        rcNt = NtDuplicateObject(NtCurrentProcess(), pThis->hProcess,
                                 NtCurrentProcess(), &hProcWait,
                                 SYNCHRONIZE, 0 /*HandleAttributes*/, 0);
    }
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinReSpawn", rcNt,
                                  "NtDuplicateObject failed on child process handle: %#x\n", rcNt);
    /*
     * Close the process handle and replace it with the harmless one.
     */
    rcNt = NtClose(pThis->hProcess);
    pThis->hProcess = hProcWait;
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinReSpawn", VERR_INVALID_NAME,
                                  "NtClose failed on child process handle: %#x\n", rcNt);
}


/**
 * This restores the child PEB and tweaks a couple of fields before we do the
 * child purification and let the process run normally.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildSanitizePeb(PSUPR3HARDNTCHILD pThis)
{
    /*
     * Make a copy of the pre-execution PEB.
     */
    PEB Peb = pThis->Peb;

#if 0
    /*
     * There should not be any activation context, so if there is, we scratch the memory associated with it.
     */
    int rc = 0;
    if (RT_SUCCESS(rc) && Peb.pShimData && !((uintptr_t)Peb.pShimData & PAGE_OFFSET_MASK))
        rc = supR3HardenedWinScratchChildMemory(hProcess, Peb.pShimData, PAGE_SIZE, "pShimData", pErrInfo);
    if (RT_SUCCESS(rc) && Peb.ActivationContextData && !((uintptr_t)Peb.ActivationContextData & PAGE_OFFSET_MASK))
        rc = supR3HardenedWinScratchChildMemory(hProcess, Peb.ActivationContextData, PAGE_SIZE, "ActivationContextData", pErrInfo);
    if (RT_SUCCESS(rc) && Peb.ProcessAssemblyStorageMap && !((uintptr_t)Peb.ProcessAssemblyStorageMap & PAGE_OFFSET_MASK))
        rc = supR3HardenedWinScratchChildMemory(hProcess, Peb.ProcessAssemblyStorageMap, PAGE_SIZE, "ProcessAssemblyStorageMap", pErrInfo);
    if (RT_SUCCESS(rc) && Peb.SystemDefaultActivationContextData && !((uintptr_t)Peb.SystemDefaultActivationContextData & PAGE_OFFSET_MASK))
        rc = supR3HardenedWinScratchChildMemory(hProcess, Peb.ProcessAssemblyStorageMap, PAGE_SIZE, "SystemDefaultActivationContextData", pErrInfo);
    if (RT_SUCCESS(rc) && Peb.SystemAssemblyStorageMap && !((uintptr_t)Peb.SystemAssemblyStorageMap & PAGE_OFFSET_MASK))
        rc = supR3HardenedWinScratchChildMemory(hProcess, Peb.SystemAssemblyStorageMap, PAGE_SIZE, "SystemAssemblyStorageMap", pErrInfo);
    if (RT_FAILURE(rc))
        return rc;
#endif

    /*
     * Clear compatibility and activation related fields.
     */
    Peb.AppCompatFlags.QuadPart             = 0;
    Peb.AppCompatFlagsUser.QuadPart         = 0;
    Peb.pShimData                           = NULL;
    Peb.AppCompatInfo                       = NULL;
#if 0
    Peb.ActivationContextData               = NULL;
    Peb.ProcessAssemblyStorageMap           = NULL;
    Peb.SystemDefaultActivationContextData  = NULL;
    Peb.SystemAssemblyStorageMap            = NULL;
    /*Peb.Diff0.W6.IsProtectedProcess = 1;*/
#endif

    /*
     * Write back the PEB.
     */
    SIZE_T cbActualMem = pThis->cbPeb;
    NTSTATUS rcNt = NtWriteVirtualMemory(pThis->hProcess, pThis->BasicInfo.PebBaseAddress, &Peb, pThis->cbPeb, &cbActualMem);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildSanitizePeb", rcNt,
                                  "NtWriteVirtualMemory/Peb failed: %#x", rcNt);

}


/**
 * Purifies the child process after very early init has been performed.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildPurify(PSUPR3HARDNTCHILD pThis)
{
    /*
     * We loop until we no longer make any fixes.  This is similar to what
     * we do (or used to do, really) in the fAvastKludge case of
     * supR3HardenedWinInit.  We might be up against asynchronous changes,
     * which we fudge by waiting a short while before earch purification. This
     * is arguably a fragile technique, but it's currently the best we've got.
     * Fortunately, most AVs seems to either favor immediate action on initial
     * load events or (much better for us) later events like kernel32.
     */
    uint64_t uMsTsOuterStart = supR3HardenedWinGetMilliTS();
    uint32_t cMsFudge        = g_fSupAdversaries ? 512 : 256;
    uint32_t cTotalFixes     = 0;
    uint32_t cFixes          = 0; /* (MSC wrongly thinks this maybe used uninitialized) */
    for (uint32_t iLoop = 0; iLoop < 16; iLoop++)
    {
        /*
         * Delay.
         */
        uint32_t cSleeps = 0;
        uint64_t uMsTsStart = supR3HardenedWinGetMilliTS();
        do
        {
            NtYieldExecution();
            LARGE_INTEGER Time;
            Time.QuadPart = -8000000 / 100; /* 8ms in 100ns units, relative time. */
            NtDelayExecution(FALSE, &Time);
            cSleeps++;
        } while (   supR3HardenedWinGetMilliTS() - uMsTsStart <= cMsFudge
                 || cSleeps < 8);
        SUP_DPRINTF(("supR3HardNtChildPurify: Startup delay kludge #1/%u: %u ms, %u sleeps\n",
                     iLoop, supR3HardenedWinGetMilliTS() - uMsTsStart, cSleeps));

        /*
         * Purify.
         */
        cFixes = 0;
        int rc = supHardenedWinVerifyProcess(pThis->hProcess, pThis->hThread, SUPHARDNTVPKIND_CHILD_PURIFICATION,
                                             g_fSupAdversaries & (  SUPHARDNT_ADVERSARY_TRENDMICRO_SAKFILE
                                                                  | SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD)
                                             ? SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW : 0,
                                             &cFixes, RTErrInfoInitStatic(&g_ErrInfoStatic));
        if (RT_FAILURE(rc))
            supR3HardenedWinKillChild(pThis, "supR3HardNtChildPurify", rc,
                                      "supHardenedWinVerifyProcess failed with %Rrc: %s", rc, g_ErrInfoStatic.szMsg);
        if (cFixes == 0)
        {
            SUP_DPRINTF(("supR3HardNtChildPurify: Done after %llu ms and %u fixes (loop #%u).\n",
                         supR3HardenedWinGetMilliTS() - uMsTsOuterStart, cTotalFixes, iLoop));
            return; /* We're probably good. */
        }
        cTotalFixes += cFixes;

        if (!g_fSupAdversaries)
            g_fSupAdversaries |= SUPHARDNT_ADVERSARY_UNKNOWN;
        cMsFudge = 512;

        /*
         * Log the KiOpPrefetchPatchCount value if available, hoping it might
         * sched some light on spider38's case.
         */
        ULONG cPatchCount = 0;
        NTSTATUS rcNt = NtQuerySystemInformation(SystemInformation_KiOpPrefetchPatchCount,
                                                 &cPatchCount, sizeof(cPatchCount), NULL);
        if (NT_SUCCESS(rcNt))
            SUP_DPRINTF(("supR3HardNtChildPurify: cFixes=%u g_fSupAdversaries=%#x cPatchCount=%#u\n",
                         cFixes, g_fSupAdversaries, cPatchCount));
        else
            SUP_DPRINTF(("supR3HardNtChildPurify: cFixes=%u g_fSupAdversaries=%#x\n", cFixes, g_fSupAdversaries));
    }

    /*
     * We've given up fixing the child process.  Probably fighting someone
     * that monitors their patches or/and our activities.
     */
    supR3HardenedWinKillChild(pThis, "supR3HardNtChildPurify", VERR_TRY_AGAIN,
                              "Unable to purify child process! After 16 tries over %llu ms, we still %u fix(es) in the last pass.",
                              supR3HardenedWinGetMilliTS() - uMsTsOuterStart, cFixes);
}


/**
 * Sets up the early process init.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildSetUpChildInit(PSUPR3HARDNTCHILD pThis)
{
    uintptr_t const uChildExeAddr = (uintptr_t)pThis->Peb.ImageBaseAddress;

    /*
     * Plant the process parameters.  This ASSUMES the handle inheritance is
     * performed when creating the child process.
     */
    RT_ZERO(pThis->ProcParams);
    pThis->ProcParams.hEvtChild  = pThis->hEvtChild;
    pThis->ProcParams.hEvtParent = pThis->hEvtParent;
    pThis->ProcParams.uNtDllAddr = pThis->uNtDllAddr;
    pThis->ProcParams.enmRequest = kSupR3WinChildReq_Error;
    pThis->ProcParams.rc         = VINF_SUCCESS;

    uintptr_t uChildAddr = uChildExeAddr + ((uintptr_t)&g_ProcParams - (uintptr_t)NtCurrentPeb()->ImageBaseAddress);
    SIZE_T    cbIgnored;
    NTSTATUS  rcNt = NtWriteVirtualMemory(pThis->hProcess, (PVOID)uChildAddr, &pThis->ProcParams,
                                          sizeof(pThis->ProcParams), &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rcNt,
                                  "NtWriteVirtualMemory(,%p,) failed writing child process parameters: %#x\n", uChildAddr, rcNt);

    /*
     * Locate the LdrInitializeThunk address in the child as well as pristine
     * code bits for it.
     */
    PSUPHNTLDRCACHEENTRY pLdrEntry;
    int rc = supHardNtLdrCacheOpen("ntdll.dll", &pLdrEntry, NULL /*pErrInfo*/);
    if (RT_FAILURE(rc))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rc,
                                  "supHardNtLdrCacheOpen failed on NTDLL: %Rrc\n", rc);

    uint8_t *pbChildNtDllBits;
    rc = supHardNtLdrCacheEntryGetBits(pLdrEntry, &pbChildNtDllBits, pThis->uNtDllAddr, NULL, NULL, NULL /*pErrInfo*/);
    if (RT_FAILURE(rc))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rc,
                                  "supHardNtLdrCacheEntryGetBits failed on NTDLL: %Rrc\n", rc);

    RTLDRADDR uLdrInitThunk;
    rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbChildNtDllBits, pThis->uNtDllAddr, UINT32_MAX,
                          "LdrInitializeThunk", &uLdrInitThunk);
    if (RT_FAILURE(rc))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rc,
                                  "Error locating LdrInitializeThunk in NTDLL: %Rrc", rc);
    PVOID pvLdrInitThunk = (PVOID)(uintptr_t)uLdrInitThunk;
    SUP_DPRINTF(("supR3HardenedWinSetupChildInit: uLdrInitThunk=%p\n", (uintptr_t)uLdrInitThunk));

    /*
     * Calculate the address of our code in the child process.
     */
    uintptr_t uEarlyProcInitEP = uChildExeAddr + (  (uintptr_t)&supR3HardenedEarlyProcessInitThunk
                                                  - (uintptr_t)NtCurrentPeb()->ImageBaseAddress);

    /*
     * Compose the LdrInitializeThunk replacement bytes.
     * Note! The amount of code we replace here must be less or equal to what
     *       the process verification code ignores.
     */
    uint8_t abNew[16];
    memcpy(abNew, pbChildNtDllBits + ((uintptr_t)uLdrInitThunk - pThis->uNtDllAddr), sizeof(abNew));
#ifdef RT_ARCH_AMD64
    abNew[0] = 0xff;
    abNew[1] = 0x25;
    *(uint32_t *)&abNew[2] = 0;
    *(uint64_t *)&abNew[6] = uEarlyProcInitEP;
#elif defined(RT_ARCH_X86)
    abNew[0] = 0xe9;
    *(uint32_t *)&abNew[1] = uEarlyProcInitEP - ((uint32_t)uLdrInitThunk + 5);
#else
# error "Unsupported arch."
#endif

    /*
     * Install the LdrInitializeThunk replacement code in the child process.
     */
    PVOID   pvProt = pvLdrInitThunk;
    SIZE_T  cbProt = sizeof(abNew);
    ULONG   fOldProt;
    rcNt = NtProtectVirtualMemory(pThis->hProcess, &pvProt, &cbProt, PAGE_EXECUTE_READWRITE, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rcNt,
                                  "NtProtectVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    rcNt = NtWriteVirtualMemory(pThis->hProcess, pvLdrInitThunk, abNew, sizeof(abNew), &cbIgnored);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rcNt,
                                  "NtWriteVirtualMemory/LdrInitializeThunk failed: %#x", rcNt);

    pvProt = pvLdrInitThunk;
    cbProt = sizeof(abNew);
    rcNt = NtProtectVirtualMemory(pThis->hProcess, &pvProt, &cbProt, fOldProt, &fOldProt);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rcNt,
                                  "NtProtectVirtualMemory/LdrInitializeThunk[restore] failed: %#x", rcNt);

    /*
     * Check the sanity of the thread context.
     */
    CONTEXT Ctx;
    RT_ZERO(Ctx);
    Ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
    rcNt = NtGetContextThread(pThis->hThread, &Ctx);
    if (NT_SUCCESS(rcNt))
    {
#ifdef RT_ARCH_AMD64
        DWORD64 *pPC = &Ctx.Rip;
#elif defined(RT_ARCH_X86)
        DWORD   *pPC = &Ctx.Eip;
#else
# error "Unsupported arch."
#endif
        supR3HardNtDprintCtx(&Ctx, "supR3HardenedWinSetupChildInit: Initial context:");

        /* Entrypoint for the executable: */
        uintptr_t const uChildMain = uChildExeAddr + (  (uintptr_t)&suplibHardenedWindowsMain
                                                      - (uintptr_t)NtCurrentPeb()->ImageBaseAddress);

        /* NtDll size and the more recent default thread start entrypoint (Vista+?): */
        RTLDRADDR uSystemThreadStart;
        rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbChildNtDllBits, pThis->uNtDllAddr, UINT32_MAX,
                              "RtlUserThreadStart", &uSystemThreadStart);
        if (RT_FAILURE(rc))
            uSystemThreadStart = 0;

        /* Kernel32 for thread start of older windows version, only XP64/W2K3-64 has an actual
           export for it.  Unfortunately, it is not yet loaded into the child, so we have to
           assume same location as in the parent (safe): */
        PSUPHNTLDRCACHEENTRY pLdrEntryKernel32;
        rc = supHardNtLdrCacheOpen("kernel32.dll", &pLdrEntryKernel32, NULL /*pErrInfo*/);
        if (RT_FAILURE(rc))
            supR3HardenedWinKillChild(pThis, "supR3HardenedWinSetupChildInit", rc,
                                      "supHardNtLdrCacheOpen failed on KERNEL32: %Rrc\n", rc);
        size_t const cbKernel32 = RTLdrSize(pLdrEntryKernel32->hLdrMod);

#ifdef RT_ARCH_AMD64
        if (!uSystemThreadStart)
        {
            rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbChildNtDllBits, pLdrEntryKernel32->uImageBase, UINT32_MAX,
                                  "BaseProcessStart", &uSystemThreadStart);
            if (RT_FAILURE(rc))
                uSystemThreadStart = 0;
        }
#endif

        bool fUpdateContext = false;

        /* Check if the RIP looks half sane, try correct it if it isn't.
           It should point to RtlUserThreadStart (Vista and later it seem), though only
           tested on win10.  The first parameter is the executable entrypoint, the 2nd
           is probably the PEB.  Before Vista it should point to Kernel32!BaseProcessStart,
           though the symbol is only exported in 5.2/AMD64. */
        if (   (  uSystemThreadStart
                ? *pPC == uSystemThreadStart
                : *pPC - (  pLdrEntryKernel32->uImageBase != ~(uintptr_t)0 ? pLdrEntryKernel32->uImageBase
                          : (uintptr_t)GetModuleHandleW(L"kernel32.dll")) <= cbKernel32)
            || *pPC == uChildMain)
        { }
        else
        {
            SUP_DPRINTF(("Warning! Bogus RIP: %p (uSystemThreadStart=%p; kernel32 %p LB %p; uChildMain=%p)\n",
                         *pPC, uSystemThreadStart, pLdrEntryKernel32->uImageBase, cbKernel32, uChildMain));
            if (uSystemThreadStart)
            {
                SUP_DPRINTF(("Correcting RIP from to %p hoping that it might work...\n", (uintptr_t)uSystemThreadStart));
                *pPC = uSystemThreadStart;
                fUpdateContext = true;
            }
        }
#ifdef RT_ARCH_AMD64
        if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(10, 0)) /* W2K3: CS=33 SS=DS=ES=GS=2b FS=53 */
        {
            if (Ctx.SegDs != 0)
                SUP_DPRINTF(("Warning! Bogus DS: %04x, expected zero\n", Ctx.SegDs));
            if (Ctx.SegEs != 0)
                SUP_DPRINTF(("Warning! Bogus ES: %04x, expected zero\n", Ctx.SegEs));
            if (Ctx.SegFs != 0)
                SUP_DPRINTF(("Warning! Bogus FS: %04x, expected zero\n", Ctx.SegFs));
            if (Ctx.SegGs != 0)
                SUP_DPRINTF(("Warning! Bogus GS: %04x, expected zero\n", Ctx.SegGs));
        }
        if (Ctx.Rcx != uChildMain)
            SUP_DPRINTF(("Warning! Bogus RCX: %016RX64, expected %016RX64\n", Ctx.Rcx, uChildMain));
        if (Ctx.Rdx & PAGE_OFFSET_MASK)
            SUP_DPRINTF(("Warning! Bogus RDX: %016RX64, expected page aligned\n", Ctx.Rdx)); /* PEB */
        if ((Ctx.Rsp & 15) != 8)
            SUP_DPRINTF(("Warning! Misaligned RSP: %016RX64\n", Ctx.Rsp));
#endif
        if (Ctx.SegCs != ASMGetCS())
            SUP_DPRINTF(("Warning! Bogus CS: %04x, expected %04x\n", Ctx.SegCs, ASMGetCS()));
        if (Ctx.SegSs != ASMGetSS())
            SUP_DPRINTF(("Warning! Bogus SS: %04x, expected %04x\n", Ctx.SegSs, ASMGetSS()));
        if (Ctx.Dr0 != 0)
            SUP_DPRINTF(("Warning! Bogus DR0: %016RX64, expected zero\n", Ctx.Dr0));
        if (Ctx.Dr1 != 0)
            SUP_DPRINTF(("Warning! Bogus DR1: %016RX64, expected zero\n", Ctx.Dr1));
        if (Ctx.Dr2 != 0)
            SUP_DPRINTF(("Warning! Bogus DR2: %016RX64, expected zero\n", Ctx.Dr2));
        if (Ctx.Dr3 != 0)
            SUP_DPRINTF(("Warning! Bogus DR3: %016RX64, expected zero\n", Ctx.Dr3));
        if (Ctx.Dr6 != 0)
            SUP_DPRINTF(("Warning! Bogus DR6: %016RX64, expected zero\n", Ctx.Dr6));
        if (Ctx.Dr7 != 0)
        {
            SUP_DPRINTF(("Warning! Bogus DR7: %016RX64, expected zero\n", Ctx.Dr7));
            Ctx.Dr7 = 0;
            fUpdateContext = true;
        }

        if (fUpdateContext)
        {
            rcNt = NtSetContextThread(pThis->hThread, &Ctx);
            if (!NT_SUCCESS(rcNt))
                SUP_DPRINTF(("Error! NtSetContextThread failed: %#x\n", rcNt));
        }
    }

    /* Caller starts child execution. */
    SUP_DPRINTF(("supR3HardenedWinSetupChildInit: Start child.\n"));
}



/**
 * This messes with the child PEB before we trigger the initial image events.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildScrewUpPebForInitialImageEvents(PSUPR3HARDNTCHILD pThis)
{
    /*
     * Not sure if any of the cracker software uses the PEB at this point, but
     * just in case they do make some of the PEB fields a little less useful.
     */
    PEB Peb = pThis->Peb;

    /* Make ImageBaseAddress useless. */
    Peb.ImageBaseAddress = (PVOID)((uintptr_t)Peb.ImageBaseAddress ^ UINT32_C(0x5f139000));
#ifdef RT_ARCH_AMD64
    Peb.ImageBaseAddress = (PVOID)((uintptr_t)Peb.ImageBaseAddress | UINT64_C(0x0313000000000000));
#endif

    /*
     * Write the PEB.
     */
    SIZE_T cbActualMem = pThis->cbPeb;
    NTSTATUS rcNt = NtWriteVirtualMemory(pThis->hProcess, pThis->BasicInfo.PebBaseAddress, &Peb, pThis->cbPeb, &cbActualMem);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildScrewUpPebForInitialImageEvents", rcNt,
                                  "NtWriteVirtualMemory/Peb failed: %#x", rcNt);
}


/**
 * Check if the zero terminated NT unicode string is the path to the given
 * system32 DLL.
 *
 * @returns true if it is, false if not.
 * @param   pUniStr             The zero terminated NT unicode string path.
 * @param   pszName             The name of the system32 DLL.
 */
static bool supR3HardNtIsNamedSystem32Dll(PUNICODE_STRING pUniStr, const char *pszName)
{
    if (pUniStr->Length > g_System32NtPath.UniStr.Length)
    {
        if (memcmp(pUniStr->Buffer, g_System32NtPath.UniStr.Buffer, g_System32NtPath.UniStr.Length) == 0)
        {
            if (pUniStr->Buffer[g_System32NtPath.UniStr.Length / sizeof(WCHAR)] == '\\')
            {
                if (RTUtf16ICmpAscii(&pUniStr->Buffer[g_System32NtPath.UniStr.Length / sizeof(WCHAR) + 1], pszName) == 0)
                    return true;
            }
        }
    }

    return false;
}


/**
 * Worker for supR3HardNtChildGatherData that locates NTDLL in the child
 * process.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildFindNtdll(PSUPR3HARDNTCHILD pThis)
{
    /*
     * Find NTDLL in this process first and take that as a starting point.
     */
    pThis->uNtDllParentAddr = (uintptr_t)GetModuleHandleW(L"ntdll.dll");
    SUPR3HARDENED_ASSERT(pThis->uNtDllParentAddr != 0 && !(pThis->uNtDllParentAddr & PAGE_OFFSET_MASK));
    pThis->uNtDllAddr = pThis->uNtDllParentAddr;

    /*
     * Scan the virtual memory of the child.
     */
    uintptr_t   cbAdvance = 0;
    uintptr_t   uPtrWhere = 0;
    for (uint32_t i = 0; i < 1024; i++)
    {
        /* Query information. */
        SIZE_T                      cbActual = 0;
        MEMORY_BASIC_INFORMATION    MemInfo  = { 0, 0, 0, 0, 0, 0, 0 };
        NTSTATUS rcNt = NtQueryVirtualMemory(pThis->hProcess,
                                             (void const *)uPtrWhere,
                                             MemoryBasicInformation,
                                             &MemInfo,
                                             sizeof(MemInfo),
                                             &cbActual);
        if (!NT_SUCCESS(rcNt))
            break;

        if (   MemInfo.Type == SEC_IMAGE
            || MemInfo.Type == SEC_PROTECTED_IMAGE
            || MemInfo.Type == (SEC_IMAGE | SEC_PROTECTED_IMAGE))
        {
            if (MemInfo.BaseAddress == MemInfo.AllocationBase)
            {
                /* Get the image name. */
                union
                {
                    UNICODE_STRING UniStr;
                    uint8_t abPadding[4096];
                } uBuf;
                rcNt = NtQueryVirtualMemory(pThis->hProcess,
                                            MemInfo.BaseAddress,
                                            MemorySectionName,
                                            &uBuf,
                                            sizeof(uBuf) - sizeof(WCHAR),
                                            &cbActual);
                if (NT_SUCCESS(rcNt))
                {
                    uBuf.UniStr.Buffer[uBuf.UniStr.Length / sizeof(WCHAR)] = '\0';
                    if (supR3HardNtIsNamedSystem32Dll(&uBuf.UniStr, "ntdll.dll"))
                    {
                        pThis->uNtDllAddr = (uintptr_t)MemInfo.AllocationBase;
                        SUP_DPRINTF(("supR3HardNtPuChFindNtdll: uNtDllParentAddr=%p uNtDllChildAddr=%p\n",
                                     pThis->uNtDllParentAddr, pThis->uNtDllAddr));
                        return;
                    }
                }
            }
        }

        /*
         * Advance.
         */
        cbAdvance = MemInfo.RegionSize;
        if (uPtrWhere + cbAdvance <= uPtrWhere)
            break;
        uPtrWhere += MemInfo.RegionSize;
    }

    supR3HardenedWinKillChild(pThis, "supR3HardNtChildFindNtdll", VERR_MODULE_NOT_FOUND, "ntdll.dll not found in child process.");
}


/**
 * Gather child data.
 *
 * @param   pThis               The child process data structure.
 */
static void supR3HardNtChildGatherData(PSUPR3HARDNTCHILD pThis)
{
    /*
     * Basic info.
     */
    ULONG cbActual = 0;
    NTSTATUS rcNt = NtQueryInformationProcess(pThis->hProcess, ProcessBasicInformation,
                                              &pThis->BasicInfo, sizeof(pThis->BasicInfo), &cbActual);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildGatherData", rcNt,
                                  "NtQueryInformationProcess/ProcessBasicInformation failed: %#x", rcNt);

    /*
     * If this is the middle (stub) process, we wish to wait for both child
     * and parent.  So open the parent process.  Not fatal if we cannnot.
     */
    if (pThis->iWhich > 1)
    {
        PROCESS_BASIC_INFORMATION SelfInfo;
        rcNt = NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &SelfInfo, sizeof(SelfInfo), &cbActual);
        if (NT_SUCCESS(rcNt))
        {
            OBJECT_ATTRIBUTES ObjAttr;
            InitializeObjectAttributes(&ObjAttr, NULL, 0, NULL /*hRootDir*/, NULL /*pSecDesc*/);

            CLIENT_ID ClientId;
            ClientId.UniqueProcess = (HANDLE)SelfInfo.InheritedFromUniqueProcessId;
            ClientId.UniqueThread  = NULL;

            rcNt = NtOpenProcess(&pThis->hParent, SYNCHRONIZE | PROCESS_QUERY_INFORMATION, &ObjAttr, &ClientId);
#ifdef DEBUG
            SUPR3HARDENED_ASSERT_NT_SUCCESS(rcNt);
#endif
            if (!NT_SUCCESS(rcNt))
            {
                pThis->hParent = NULL;
                SUP_DPRINTF(("supR3HardNtChildGatherData: Failed to open parent process (%#p): %#x\n", ClientId.UniqueProcess, rcNt));
            }
        }

    }

    /*
     * Process environment block.
     */
    if (g_uNtVerCombined < SUP_NT_VER_W2K3)
        pThis->cbPeb = PEB_SIZE_W51;
    else if (g_uNtVerCombined < SUP_NT_VER_VISTA)
        pThis->cbPeb = PEB_SIZE_W52;
    else if (g_uNtVerCombined < SUP_NT_VER_W70)
        pThis->cbPeb = PEB_SIZE_W6;
    else if (g_uNtVerCombined < SUP_NT_VER_W80)
        pThis->cbPeb = PEB_SIZE_W7;
    else if (g_uNtVerCombined < SUP_NT_VER_W81)
        pThis->cbPeb = PEB_SIZE_W80;
    else
        pThis->cbPeb = PEB_SIZE_W81;

    SUP_DPRINTF(("supR3HardNtChildGatherData: PebBaseAddress=%p cbPeb=%#x\n",
                 pThis->BasicInfo.PebBaseAddress, pThis->cbPeb));

    SIZE_T cbActualMem;
    RT_ZERO(pThis->Peb);
    rcNt = NtReadVirtualMemory(pThis->hProcess, pThis->BasicInfo.PebBaseAddress, &pThis->Peb, sizeof(pThis->Peb), &cbActualMem);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(pThis, "supR3HardNtChildGatherData", rcNt,
                                  "NtReadVirtualMemory/Peb failed: %#x", rcNt);

    /*
     * Locate NtDll.
     */
    supR3HardNtChildFindNtdll(pThis);
}


/**
 * Does the actually respawning.
 *
 * @returns Never, will call exit or raise fatal error.
 * @param   iWhich              Which respawn we're to check for, 1 being the
 *                              first one, and 2 the second and final.
 */
static DECL_NO_RETURN(void) supR3HardenedWinDoReSpawn(int iWhich)
{
    NTSTATUS                        rcNt;
    PPEB                            pPeb              = NtCurrentPeb();
    PRTL_USER_PROCESS_PARAMETERS    pParentProcParams = pPeb->ProcessParameters;

    SUPR3HARDENED_ASSERT(g_cSuplibHardenedWindowsMainCalls == 1);

    /*
     * Init the child process data structure, creating the child communication
     * event sempahores.
     */
    SUPR3HARDNTCHILD This;
    RT_ZERO(This);
    This.iWhich = iWhich;

    OBJECT_ATTRIBUTES ObjAttrs;
    This.hEvtChild = NULL;
    InitializeObjectAttributes(&ObjAttrs, NULL /*pName*/, OBJ_INHERIT, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    SUPR3HARDENED_ASSERT_NT_SUCCESS(NtCreateEvent(&This.hEvtChild, EVENT_ALL_ACCESS, &ObjAttrs, SynchronizationEvent, FALSE));

    This.hEvtParent = NULL;
    InitializeObjectAttributes(&ObjAttrs, NULL /*pName*/, OBJ_INHERIT, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    SUPR3HARDENED_ASSERT_NT_SUCCESS(NtCreateEvent(&This.hEvtParent, EVENT_ALL_ACCESS, &ObjAttrs, SynchronizationEvent, FALSE));

    /*
     * Set up security descriptors.
     */
    SECURITY_ATTRIBUTES ProcessSecAttrs;
    MYSECURITYCLEANUP   ProcessSecAttrsCleanup;
    supR3HardNtChildInitSecAttrs(&ProcessSecAttrs, &ProcessSecAttrsCleanup, true /*fProcess*/);

    SECURITY_ATTRIBUTES ThreadSecAttrs;
    MYSECURITYCLEANUP   ThreadSecAttrsCleanup;
    supR3HardNtChildInitSecAttrs(&ThreadSecAttrs, &ThreadSecAttrsCleanup, false /*fProcess*/);

#if 1
    /*
     * Configure the startup info and creation flags.
     */
    DWORD dwCreationFlags = CREATE_SUSPENDED;

    STARTUPINFOEXW SiEx;
    suplibHardenedMemSet(&SiEx, 0, sizeof(SiEx));
    if (1)
        SiEx.StartupInfo.cb = sizeof(SiEx.StartupInfo);
    else
    {
        SiEx.StartupInfo.cb = sizeof(SiEx);
        dwCreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
        /** @todo experiment with protected process stuff later on. */
    }

    SiEx.StartupInfo.dwFlags |= pParentProcParams->WindowFlags & STARTF_USESHOWWINDOW;
    SiEx.StartupInfo.wShowWindow = (WORD)pParentProcParams->ShowWindowFlags;

    SiEx.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    SiEx.StartupInfo.hStdInput  = pParentProcParams->StandardInput;
    SiEx.StartupInfo.hStdOutput = pParentProcParams->StandardOutput;
    SiEx.StartupInfo.hStdError  = pParentProcParams->StandardError;

    /*
     * Construct the command line and launch the process.
     */
    PRTUTF16 pwszCmdLine = supR3HardNtChildConstructCmdLine(NULL, iWhich);

    supR3HardenedWinEnableThreadCreation();
    PROCESS_INFORMATION ProcessInfoW32 = { NULL, NULL, 0, 0 };
    if (!CreateProcessW(g_wszSupLibHardenedExePath,
                        pwszCmdLine,
                        &ProcessSecAttrs,
                        &ThreadSecAttrs,
                        TRUE /*fInheritHandles*/,
                        dwCreationFlags,
                        NULL /*pwszzEnvironment*/,
                        NULL /*pwszCurDir*/,
                        &SiEx.StartupInfo,
                        &ProcessInfoW32))
        supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Misc, VERR_INVALID_NAME,
                              "Error relaunching VirtualBox VM process: %u\n"
                              "Command line: '%ls %ls'",
                              RtlGetLastWin32Error(), g_wszSupLibHardenedExePath, pwszCmdLine);
    supR3HardenedWinDisableThreadCreation();

    SUP_DPRINTF(("supR3HardenedWinDoReSpawn(%d): New child %x.%x [kernel32].\n",
                 iWhich, ProcessInfoW32.dwProcessId, ProcessInfoW32.dwThreadId));
    This.hProcess = ProcessInfoW32.hProcess;
    This.hThread  = ProcessInfoW32.hThread;

#else

    /*
     * Construct the process parameters.
     */
    UNICODE_STRING W32ImageName;
    W32ImageName.Buffer = g_wszSupLibHardenedExePath; /* Yes the windows name for the process parameters. */
    W32ImageName.Length = (USHORT)RTUtf16Len(g_wszSupLibHardenedExePath) * sizeof(WCHAR);
    W32ImageName.MaximumLength = W32ImageName.Length + sizeof(WCHAR);

    UNICODE_STRING CmdLine;
    supR3HardNtChildConstructCmdLine(&CmdLine, iWhich);

    PRTL_USER_PROCESS_PARAMETERS pProcParams = NULL;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(RtlCreateProcessParameters(&pProcParams,
                                                               &W32ImageName,
                                                               NULL /* DllPath - inherit from this process */,
                                                               NULL /* CurrentDirectory - inherit from this process */,
                                                               &CmdLine,
                                                               NULL /* Environment - inherit from this process */,
                                                               NULL /* WindowsTitle - none */,
                                                               NULL /* DesktopTitle - none. */,
                                                               NULL /* ShellInfo - none. */,
                                                               NULL /* RuntimeInfo - none (byte array for MSVCRT file info) */)
                                    );

    /** @todo this doesn't work. :-( */
    pProcParams->ConsoleHandle  = pParentProcParams->ConsoleHandle;
    pProcParams->ConsoleFlags   = pParentProcParams->ConsoleFlags;
    pProcParams->StandardInput  = pParentProcParams->StandardInput;
    pProcParams->StandardOutput = pParentProcParams->StandardOutput;
    pProcParams->StandardError  = pParentProcParams->StandardError;

    RTL_USER_PROCESS_INFORMATION ProcessInfoNt = { sizeof(ProcessInfoNt) };
    rcNt = RtlCreateUserProcess(&g_SupLibHardenedExeNtPath.UniStr,
                                OBJ_INHERIT | OBJ_CASE_INSENSITIVE /*Attributes*/,
                                pProcParams,
                                NULL, //&ProcessSecAttrs,
                                NULL, //&ThreadSecAttrs,
                                NtCurrentProcess() /* ParentProcess */,
                                FALSE /*fInheritHandles*/,
                                NULL /* DebugPort */,
                                NULL /* ExceptionPort */,
                                &ProcessInfoNt);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Misc, VERR_INVALID_NAME,
                              "Error relaunching VirtualBox VM process: %#x\n"
                              "Command line: '%ls'",
                              rcNt, CmdLine.Buffer);

    SUP_DPRINTF(("supR3HardenedWinDoReSpawn(%d): New child %x.%x [ntdll].\n",
                 iWhich, ProcessInfo.ClientId.UniqueProcess, ProcessInfo.ClientId.UniqueThread));
    RtlDestroyProcessParameters(pProcParams);

    This.hProcess = ProcessInfoNt.ProcessHandle;
    This.hThread  = ProcessInfoNt.ThreadHandle;
#endif

#ifndef VBOX_WITHOUT_DEBUGGER_CHECKS
    /*
     * Apply anti debugger notification trick to the thread.  (Also done in
     * supR3HardenedWinInit.)  This may fail with STATUS_ACCESS_DENIED and
     * maybe other errors.  (Unfortunately, recent (SEP 12.1) of symantec's
     * sysplant.sys driver will cause process deadlocks and a shutdown/reboot
     * denial of service problem if we hide the initial thread, so we postpone
     * this action if we've detected SEP.)
     */
    if (!(g_fSupAdversaries & (SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT | SUPHARDNT_ADVERSARY_SYMANTEC_N360)))
    {
        rcNt = NtSetInformationThread(This.hThread, ThreadHideFromDebugger, NULL, 0);
        if (!NT_SUCCESS(rcNt))
            SUP_DPRINTF(("supR3HardenedWinReSpawn: NtSetInformationThread/ThreadHideFromDebugger failed: %#x (harmless)\n", rcNt));
    }
#endif

    /*
     * Perform very early child initialization.
     */
    supR3HardNtChildGatherData(&This);
    supR3HardNtChildScrewUpPebForInitialImageEvents(&This);
    supR3HardNtChildSetUpChildInit(&This);

    ULONG cSuspendCount = 0;
    rcNt = NtResumeThread(This.hThread, &cSuspendCount);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(&This, "supR3HardenedWinDoReSpawn", rcNt, "NtResumeThread failed: %#x", rcNt);

    /*
     * Santizie the pre-NTDLL child when it's ready.
     *
     * AV software and other things injecting themselves into the embryonic
     * and budding process to intercept API calls and what not.  Unfortunately
     * this is also the behavior of viruses, malware and other unfriendly
     * software, so we won't stand for it.  AV software can scan our image
     * as they are loaded via kernel hooks, that's sufficient.  No need for
     * patching half of NTDLL or messing with the import table of the
     * process executable.
     */
    supR3HardNtChildWaitFor(&This, kSupR3WinChildReq_PurifyChildAndCloseHandles, 2000 /*ms*/, "PurifyChildAndCloseHandles");
    supR3HardNtChildPurify(&This);
    supR3HardNtChildSanitizePeb(&This);

    /*
     * Close the unrestricted access handles.  Since we need to wait on the
     * child process, we'll reopen the process with limited access before doing
     * away with the process handle returned by CreateProcess.
     */
    supR3HardNtChildCloseFullAccessHandles(&This);

    /*
     * Signal the child that we've closed the unrestricted handles and it can
     * safely try open the driver.
     */
    rcNt = NtSetEvent(This.hEvtChild, NULL);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedWinKillChild(&This, "supR3HardenedWinReSpawn", VERR_INVALID_NAME,
                                  "NtSetEvent failed on child process handle: %#x\n", rcNt);

    /*
     * Ditch the loader cache so we don't sit on too much memory while waiting.
     */
    supR3HardenedWinFlushLoaderCache();
    supR3HardenedWinCompactHeaps();

    /*
     * Enable thread creation at this point so Ctrl-C and Ctrl-Break can be processed.
     */
    supR3HardenedWinEnableThreadCreation();

    /*
     * Wait for the child to get to suplibHardenedWindowsMain so we can close the handles.
     */
    supR3HardNtChildWaitFor(&This, kSupR3WinChildReq_CloseEvents, 60000 /*ms*/, "CloseEvents");

    NtClose(This.hEvtChild);
    NtClose(This.hEvtParent);
    This.hEvtChild  = NULL;
    This.hEvtParent = NULL;

    /*
     * Wait for the process to terminate.
     */
    supR3HardNtChildWaitFor(&This, kSupR3WinChildReq_End, RT_INDEFINITE_WAIT, "the end");
    supR3HardenedFatal("supR3HardenedWinDoReSpawn: supR3HardNtChildWaitFor unexpectedly returned!\n");
    /* not reached*/
}


/**
 * Logs the content of the given object directory.
 *
 * @returns true if it exists, false if not.
 * @param   pszDir             The path of the directory to log (ASCII).
 */
static void supR3HardenedWinLogObjDir(const char *pszDir)
{
    /*
     * Open the driver object directory.
     */
    RTUTF16 wszDir[128];
    int rc = RTUtf16CopyAscii(wszDir, RT_ELEMENTS(wszDir), pszDir);
    if (RT_FAILURE(rc))
    {
        SUP_DPRINTF(("supR3HardenedWinLogObjDir: RTUtf16CopyAscii -> %Rrc on '%s'\n", rc, pszDir));
        return;
    }

    UNICODE_STRING NtDirName;
    NtDirName.Buffer = (WCHAR *)wszDir;
    NtDirName.Length = (USHORT)(RTUtf16Len(wszDir) * sizeof(WCHAR));
    NtDirName.MaximumLength = NtDirName.Length + sizeof(WCHAR);

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtDirName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    HANDLE hDir;
    NTSTATUS rcNt = NtOpenDirectoryObject(&hDir, DIRECTORY_QUERY | FILE_LIST_DIRECTORY, &ObjAttr);
    SUP_DPRINTF(("supR3HardenedWinLogObjDir: %ls => %#x\n", wszDir, rcNt));
    if (!NT_SUCCESS(rcNt))
        return;

    /*
     * Enumerate it, looking for the driver.
     */
    ULONG uObjDirCtx = 0;
    for (;;)
    {
        uint32_t    abBuffer[_64K + _1K];
        ULONG       cbActual;
        rcNt = NtQueryDirectoryObject(hDir,
                                      abBuffer,
                                      sizeof(abBuffer) - 4, /* minus four for string terminator space. */
                                      FALSE /*ReturnSingleEntry */,
                                      FALSE /*RestartScan*/,
                                      &uObjDirCtx,
                                      &cbActual);
        if (!NT_SUCCESS(rcNt) || cbActual < sizeof(OBJECT_DIRECTORY_INFORMATION))
        {
            SUP_DPRINTF(("supR3HardenedWinLogObjDir: NtQueryDirectoryObject => rcNt=%#x cbActual=%#x\n", rcNt, cbActual));
            break;
        }

        POBJECT_DIRECTORY_INFORMATION pObjDir = (POBJECT_DIRECTORY_INFORMATION)abBuffer;
        while (pObjDir->Name.Length != 0)
        {
            SUP_DPRINTF(("  %.*ls  %.*ls\n",
                         pObjDir->TypeName.Length / sizeof(WCHAR), pObjDir->TypeName.Buffer,
                         pObjDir->Name.Length / sizeof(WCHAR), pObjDir->Name.Buffer));

            /* Next directory entry. */
            pObjDir++;
        }
    }

    /*
     * Clean up and return.
     */
    NtClose(hDir);
}


/**
 * Tries to open VBoxDrvErrorInfo and read extra error info from it.
 *
 * @returns pszErrorInfo.
 * @param   pszErrorInfo        The destination buffer.  Will always be
 *                              terminated.
 * @param   cbErrorInfo         The size of the destination buffer.
 * @param   pszPrefix           What to prefix the error info with, if we got
 *                              anything.
 */
DECLHIDDEN(char *) supR3HardenedWinReadErrorInfoDevice(char *pszErrorInfo, size_t cbErrorInfo, const char *pszPrefix)
{
    RT_BZERO(pszErrorInfo, cbErrorInfo);

    /*
     * Try open the device.
     */
    HANDLE              hFile  = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios    = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    UNICODE_STRING      NtName = RTNT_CONSTANT_UNISTR(SUPDRV_NT_DEVICE_NAME_ERROR_INFO);
    OBJECT_ATTRIBUTES   ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 GENERIC_READ, /* No SYNCHRONIZE. */
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE, /* No FILE_SYNCHRONOUS_IO_NONALERT. */
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Try read error info.
         */
        size_t cchPrefix = strlen(pszPrefix);
        if (cchPrefix + 3 < cbErrorInfo)
        {
            LARGE_INTEGER offRead;
            offRead.QuadPart = 0;
            rcNt = NtReadFile(hFile, NULL /*hEvent*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/, &Ios,
                              &pszErrorInfo[cchPrefix], (ULONG)(cbErrorInfo - cchPrefix - 1), &offRead, NULL);
            if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status) && Ios.Information > 0)
            {
                memcpy(pszErrorInfo, pszPrefix, cchPrefix);
                pszErrorInfo[RT_MIN(cbErrorInfo - 1, cchPrefix + Ios.Information)] = '\0';
                SUP_DPRINTF(("supR3HardenedWinReadErrorInfoDevice: '%s'", &pszErrorInfo[cchPrefix]));
            }
            else
            {
                *pszErrorInfo = '\0';
                if (rcNt != STATUS_END_OF_FILE || Ios.Status != STATUS_END_OF_FILE)
                    SUP_DPRINTF(("supR3HardenedWinReadErrorInfoDevice: NtReadFile -> %#x / %#x / %p\n",
                                 rcNt, Ios.Status, Ios.Information));
            }
        }
        else
            RTStrCopy(pszErrorInfo, cbErrorInfo, "error info buffer too small");
        NtClose(hFile);
    }
    else
        SUP_DPRINTF(("supR3HardenedWinReadErrorInfoDevice: NtCreateFile -> %#x\n", rcNt));

    return pszErrorInfo;
}



/**
 * Checks if the driver exists.
 *
 * This checks whether the driver is present in the /Driver object directory.
 * Drivers being initialized or terminated will have an object there
 * before/after their devices nodes are created/deleted.
 *
 * @returns true if it exists, false if not.
 * @param   pszDriver           The driver name.
 */
static bool supR3HardenedWinDriverExists(const char *pszDriver)
{
    /*
     * Open the driver object directory.
     */
    UNICODE_STRING NtDirName = RTNT_CONSTANT_UNISTR(L"\\Driver");

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtDirName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    HANDLE hDir;
    NTSTATUS rcNt = NtOpenDirectoryObject(&hDir, DIRECTORY_QUERY | FILE_LIST_DIRECTORY, &ObjAttr);
#ifdef VBOX_STRICT
    SUPR3HARDENED_ASSERT_NT_SUCCESS(rcNt);
#endif
    if (!NT_SUCCESS(rcNt))
        return true;

    /*
     * Enumerate it, looking for the driver.
     */
    bool  fFound = true;
    ULONG uObjDirCtx = 0;
    do
    {
        uint32_t    abBuffer[_64K + _1K];
        ULONG       cbActual;
        rcNt = NtQueryDirectoryObject(hDir,
                                      abBuffer,
                                      sizeof(abBuffer) - 4, /* minus four for string terminator space. */
                                      FALSE /*ReturnSingleEntry */,
                                      FALSE /*RestartScan*/,
                                      &uObjDirCtx,
                                      &cbActual);
        if (!NT_SUCCESS(rcNt) || cbActual < sizeof(OBJECT_DIRECTORY_INFORMATION))
            break;

        POBJECT_DIRECTORY_INFORMATION pObjDir = (POBJECT_DIRECTORY_INFORMATION)abBuffer;
        while (pObjDir->Name.Length != 0)
        {
            WCHAR wcSaved = pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)];
            pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)] = '\0';
            if (   pObjDir->Name.Length > 1
                && RTUtf16ICmpAscii(pObjDir->Name.Buffer, pszDriver) == 0)
            {
                fFound = true;
                break;
            }
            pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)] = wcSaved;

            /* Next directory entry. */
            pObjDir++;
        }
    } while (!fFound);

    /*
     * Clean up and return.
     */
    NtClose(hDir);

    return fFound;
}


/**
 * Open the stub device before the 2nd respawn.
 */
static void supR3HardenedWinOpenStubDevice(void)
{
    if (g_fSupStubOpened)
        return;

    /*
     * Retry if we think driver might still be initializing (STATUS_NO_SUCH_DEVICE + \Drivers\VBoxDrv).
     */
    static const WCHAR  s_wszName[] = SUPDRV_NT_DEVICE_NAME_STUB;
    uint64_t const      uMsTsStart = supR3HardenedWinGetMilliTS();
    NTSTATUS            rcNt;
    uint32_t            iTry;

    for (iTry = 0;; iTry++)
    {
        HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

        UNICODE_STRING      NtName;
        NtName.Buffer        = (PWSTR)s_wszName;
        NtName.Length        = sizeof(s_wszName) - sizeof(WCHAR);
        NtName.MaximumLength = sizeof(s_wszName);

        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

        rcNt = NtCreateFile(&hFile,
                            GENERIC_READ | GENERIC_WRITE, /* No SYNCHRONIZE. */
                            &ObjAttr,
                            &Ios,
                            NULL /* Allocation Size*/,
                            FILE_ATTRIBUTE_NORMAL,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            FILE_OPEN,
                            FILE_NON_DIRECTORY_FILE, /* No FILE_SYNCHRONOUS_IO_NONALERT. */
                            NULL /*EaBuffer*/,
                            0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
            rcNt = Ios.Status;

        /* The STATUS_NO_SUCH_DEVICE might be returned if the device is not
           completely initialized.  Delay a little bit and try again. */
        if (rcNt != STATUS_NO_SUCH_DEVICE)
            break;
        if (iTry > 0 && supR3HardenedWinGetMilliTS() - uMsTsStart > 5000)  /* 5 sec, at least two tries */
            break;
        if (!supR3HardenedWinDriverExists("VBoxDrv"))
        {
            /** @todo Consider starting the VBoxdrv.sys service. Requires 2nd process
             *        though, rather complicated actually as CreateProcess causes all
             *        kind of things to happen to this process which would make it hard to
             *        pass the process verification tests... :-/ */
            break;
        }

        LARGE_INTEGER Time;
        if (iTry < 8)
            Time.QuadPart = -1000000 / 100; /* 1ms in 100ns units, relative time. */
        else
            Time.QuadPart = -32000000 / 100; /* 32ms in 100ns units, relative time. */
        NtDelayExecution(TRUE, &Time);
    }

    if (NT_SUCCESS(rcNt))
        g_fSupStubOpened = true;
    else
    {
        /*
         * Report trouble (fatal).  For some errors codes we try gather some
         * extra information that goes into VBoxStartup.log so that we stand a
         * better chance resolving the issue.
         */
        char szErrorInfo[16384];
        int rc = VERR_OPEN_FAILED;
        if (SUP_NT_STATUS_IS_VBOX(rcNt)) /* See VBoxDrvNtErr2NtStatus. */
        {
            rc = SUP_NT_STATUS_TO_VBOX(rcNt);

            /*
             * \Windows\ApiPort open trouble.  So far only
             * STATUS_OBJECT_TYPE_MISMATCH has been observed.
             */
            if (rc == VERR_SUPDRV_APIPORT_OPEN_ERROR)
            {
                SUP_DPRINTF(("Error opening VBoxDrvStub: VERR_SUPDRV_APIPORT_OPEN_ERROR\n"));

                uint32_t uSessionId = NtCurrentPeb()->SessionId;
                SUP_DPRINTF(("  SessionID=%#x\n", uSessionId));
                char szDir[64];
                if (uSessionId == 0)
                    RTStrCopy(szDir, sizeof(szDir), "\\Windows");
                else
                {
                    RTStrPrintf(szDir, sizeof(szDir), "\\Sessions\\%u\\Windows", uSessionId);
                    supR3HardenedWinLogObjDir(szDir);
                }
                supR3HardenedWinLogObjDir("\\Windows");
                supR3HardenedWinLogObjDir("\\Sessions");

                supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Misc, rc,
                                      "NtCreateFile(%ls) failed: VERR_SUPDRV_APIPORT_OPEN_ERROR\n"
                                      "\n"
                                      "Error getting %s\\ApiPort in the driver from vboxsup.\n"
                                      "\n"
                                      "Could be due to security software is redirecting access to it, so please include full "
                                      "details of such software in a bug report. VBoxStartup.log may contain details important "
                                      "to resolving the issue.%s"
                                      , s_wszName, szDir,
                                      supR3HardenedWinReadErrorInfoDevice(szErrorInfo, sizeof(szErrorInfo),
                                                                          "\n\nVBoxDrvStub error: "));
            }

            /*
             * Generic VBox failure message.
             */
            supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Driver, rc,
                                  "NtCreateFile(%ls) failed: %Rrc (rcNt=%#x)%s", s_wszName, rc, rcNt,
                                  supR3HardenedWinReadErrorInfoDevice(szErrorInfo, sizeof(szErrorInfo),
                                                                      "\nVBoxDrvStub error: "));
        }
        else
        {
            const char *pszDefine;
            switch (rcNt)
            {
                case STATUS_NO_SUCH_DEVICE:         pszDefine = " STATUS_NO_SUCH_DEVICE"; break;
                case STATUS_OBJECT_NAME_NOT_FOUND:  pszDefine = " STATUS_OBJECT_NAME_NOT_FOUND"; break;
                case STATUS_ACCESS_DENIED:          pszDefine = " STATUS_ACCESS_DENIED"; break;
                case STATUS_TRUST_FAILURE:          pszDefine = " STATUS_TRUST_FAILURE"; break;
                default:                            pszDefine = ""; break;
            }

            /*
             * Problems opening the device is generally due to driver load/
             * unload issues.  Check whether the driver is loaded and make
             * suggestions accordingly.
             */
/** @todo don't fail during early init, wait till later and try load the driver if missing or at least query the service manager for additional information. */
            if (   rcNt == STATUS_NO_SUCH_DEVICE
                || rcNt == STATUS_OBJECT_NAME_NOT_FOUND)
            {
                SUP_DPRINTF(("Error opening VBoxDrvStub: %s\n", pszDefine));
                if (supR3HardenedWinDriverExists("VBoxDrv"))
                    supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Driver, VERR_OPEN_FAILED,
                                          "NtCreateFile(%ls) failed: %#x%s (%u retries)\n"
                                          "\n"
                                          "Driver is probably stuck stopping/starting. Try 'sc.exe query vboxsup' to get more "
                                          "information about its state. Rebooting may actually help.%s"
                                          , s_wszName, rcNt, pszDefine, iTry,
                                          supR3HardenedWinReadErrorInfoDevice(szErrorInfo, sizeof(szErrorInfo),
                                                                              "\nVBoxDrvStub error: "));
                else
                    supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Driver, VERR_OPEN_FAILED,
                                          "NtCreateFile(%ls) failed: %#x%s (%u retries)\n"
                                          "\n"
                                          "Driver is does not appear to be loaded. Try 'sc.exe start vboxsup', reinstall "
                                          "VirtualBox or reboot.%s"
                                          , s_wszName, rcNt, pszDefine, iTry,
                                          supR3HardenedWinReadErrorInfoDevice(szErrorInfo, sizeof(szErrorInfo),
                                                                              "\nVBoxDrvStub error: "));
            }

            /* Generic NT failure message. */
            supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Driver, VERR_OPEN_FAILED,
                                  "NtCreateFile(%ls) failed: %#x%s (%u retries)%s",
                                  s_wszName, rcNt, pszDefine, iTry,
                                  supR3HardenedWinReadErrorInfoDevice(szErrorInfo, sizeof(szErrorInfo),
                                                                      "\nVBoxDrvStub error: "));
        }
    }
}


/**
 * Called by the main code if supR3HardenedWinIsReSpawnNeeded returns @c true.
 *
 * @returns Program exit code.
 */
DECLHIDDEN(int) supR3HardenedWinReSpawn(int iWhich)
{
    /*
     * Before the 2nd respawn we set up a child protection deal with the
     * support driver via /Devices/VBoxDrvStub.  (We tried to do this
     * during the early init, but in case we had trouble accessing vboxdrv
     * (renamed to vboxsup in 7.0 and 6.1.34) we retry it here where we
     * have kernel32.dll and others to pull in for better diagnostics.)
     */
    if (iWhich == 2)
        supR3HardenedWinOpenStubDevice();

    /*
     * Make sure we're alone in the stub process before creating the VM process
     * and that there aren't any debuggers attached.
     */
    if (iWhich == 2)
    {
        int rc = supHardNtVpDebugger(NtCurrentProcess(), RTErrInfoInitStatic(&g_ErrInfoStatic));
        if (RT_SUCCESS(rc))
            rc = supHardNtVpThread(NtCurrentProcess(), NtCurrentThread(), RTErrInfoInitStatic(&g_ErrInfoStatic));
        if (RT_FAILURE(rc))
            supR3HardenedFatalMsg("supR3HardenedWinReSpawn", kSupInitOp_Integrity, rc, "%s", g_ErrInfoStatic.szMsg);
    }


    /*
     * Respawn the process with kernel protection for the new process.
     */
    supR3HardenedWinDoReSpawn(iWhich);
    /* not reached! */
}


/**
 * Checks if re-spawning is required, replacing the respawn argument if not.
 *
 * @returns true if required, false if not. In the latter case, the first
 *          argument in the vector is replaced.
 * @param   iWhich              Which respawn we're to check for, 1 being the
 *                              first one, and 2 the second and final.
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           Pointer to the argument vector.
 */
DECLHIDDEN(bool) supR3HardenedWinIsReSpawnNeeded(int iWhich, int cArgs, char **papszArgs)
{
    SUPR3HARDENED_ASSERT(g_cSuplibHardenedWindowsMainCalls == 1);
    SUPR3HARDENED_ASSERT(iWhich == 1 || iWhich == 2);

    if (cArgs < 1)
        return true;

    if (suplibHardenedStrCmp(papszArgs[0], SUPR3_RESPAWN_1_ARG0) == 0)
    {
        if (iWhich > 1)
            return true;
    }
    else if (suplibHardenedStrCmp(papszArgs[0], SUPR3_RESPAWN_2_ARG0) == 0)
    {
        if (iWhich < 2)
            return false;
    }
    else
        return true;

    /* Replace the argument. */
    papszArgs[0] = g_szSupLibHardenedExePath;
    return false;
}


/**
 * Initializes the windows verficiation bits and other things we're better off
 * doing after main() has passed on it's data.
 *
 * @param   fFlags          The main flags.
 * @param   fAvastKludge    Whether to apply the avast kludge.
 */
DECLHIDDEN(void) supR3HardenedWinInit(uint32_t fFlags, bool fAvastKludge)
{
    NTSTATUS rcNt;

#ifndef VBOX_WITHOUT_DEBUGGER_CHECKS
    /*
     * Install a anti debugging hack before we continue.  This prevents most
     * notifications from ending up in the debugger. (Also applied to the
     * child process when respawning.)
     */
    rcNt = NtSetInformationThread(NtCurrentThread(), ThreadHideFromDebugger, NULL, 0);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinInit", kSupInitOp_Misc, VERR_GENERAL_FAILURE,
                              "NtSetInformationThread/ThreadHideFromDebugger failed: %#x\n", rcNt);
#endif

    /*
     * Init the verifier.
     */
    RTErrInfoInitStatic(&g_ErrInfoStatic);
    int rc = supHardenedWinInitImageVerifier(&g_ErrInfoStatic.Core);
    if (RT_FAILURE(rc))
        supR3HardenedFatalMsg("supR3HardenedWinInit", kSupInitOp_Misc, rc,
                              "supHardenedWinInitImageVerifier failed: %s", g_ErrInfoStatic.szMsg);

    /*
     * Get the windows system directory from the KnownDlls dir.
     */
    HANDLE              hSymlink = INVALID_HANDLE_VALUE;
    UNICODE_STRING      UniStr = RTNT_CONSTANT_UNISTR(L"\\KnownDlls\\KnownDllPath");
    OBJECT_ATTRIBUTES   ObjAttrs;
    InitializeObjectAttributes(&ObjAttrs, &UniStr, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    rcNt = NtOpenSymbolicLinkObject(&hSymlink, SYMBOLIC_LINK_QUERY, &ObjAttrs);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinInit", kSupInitOp_Misc, rcNt, "Error opening '%ls': %#x", UniStr.Buffer, rcNt);

    g_System32WinPath.UniStr.Buffer = g_System32WinPath.awcBuffer;
    g_System32WinPath.UniStr.Length = 0;
    g_System32WinPath.UniStr.MaximumLength = sizeof(g_System32WinPath.awcBuffer) - sizeof(RTUTF16);
    rcNt = NtQuerySymbolicLinkObject(hSymlink, &g_System32WinPath.UniStr, NULL);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("supR3HardenedWinInit", kSupInitOp_Misc, rcNt, "Error querying '%ls': %#x", UniStr.Buffer, rcNt);
    g_System32WinPath.UniStr.Buffer[g_System32WinPath.UniStr.Length / sizeof(RTUTF16)] = '\0';

    SUP_DPRINTF(("KnownDllPath: %ls\n", g_System32WinPath.UniStr.Buffer));
    NtClose(hSymlink);

    if (!(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV))
    {
        if (fAvastKludge)
        {
            /*
             * Do a self purification to cure avast's weird NtOpenFile write-thru
             * change in GetBinaryTypeW change in kernel32.  Unfortunately, avast
             * uses a system thread to perform the process modifications, which
             * means it's hard to make sure it had the chance to make them...
             *
             * We have to resort to kludge doing yield and sleep fudging for a
             * number of milliseconds and schedulings before we can hope that avast
             * and similar products have done what they need to do.  If we do any
             * fixes, we wait for a while again and redo it until we're clean.
             *
             * This is unfortunately kind of fragile.
             */
            uint32_t cMsFudge = g_fSupAdversaries ? 512 : 128;
            uint32_t cFixes;
            for (uint32_t iLoop = 0; iLoop < 16; iLoop++)
            {
                uint32_t cSleeps = 0;
                uint64_t uMsTsStart = supR3HardenedWinGetMilliTS();
                do
                {
                    NtYieldExecution();
                    LARGE_INTEGER Time;
                    Time.QuadPart = -8000000 / 100; /* 8ms in 100ns units, relative time. */
                    NtDelayExecution(FALSE, &Time);
                    cSleeps++;
                } while (   supR3HardenedWinGetMilliTS() - uMsTsStart <= cMsFudge
                         || cSleeps < 8);
                SUP_DPRINTF(("supR3HardenedWinInit: Startup delay kludge #2/%u: %u ms, %u sleeps\n",
                             iLoop, supR3HardenedWinGetMilliTS() - uMsTsStart, cSleeps));

                cFixes = 0;
                rc = supHardenedWinVerifyProcess(NtCurrentProcess(), NtCurrentThread(), SUPHARDNTVPKIND_SELF_PURIFICATION,
                                                 0 /*fFlags*/, &cFixes, NULL /*pErrInfo*/);
                if (RT_FAILURE(rc) || cFixes == 0)
                    break;

                if (!g_fSupAdversaries)
                    g_fSupAdversaries |= SUPHARDNT_ADVERSARY_UNKNOWN;
                cMsFudge = 512;

                /* Log the KiOpPrefetchPatchCount value if available, hoping it might sched some light on spider38's case. */
                ULONG cPatchCount = 0;
                rcNt = NtQuerySystemInformation(SystemInformation_KiOpPrefetchPatchCount,
                                                &cPatchCount, sizeof(cPatchCount), NULL);
                if (NT_SUCCESS(rcNt))
                    SUP_DPRINTF(("supR3HardenedWinInit: cFixes=%u g_fSupAdversaries=%#x cPatchCount=%#u\n",
                                 cFixes, g_fSupAdversaries, cPatchCount));
                else
                    SUP_DPRINTF(("supR3HardenedWinInit: cFixes=%u g_fSupAdversaries=%#x\n", cFixes, g_fSupAdversaries));
            }
        }

        /*
         * Install the hooks.
         */
        supR3HardenedWinInstallHooks();
    }
    else if (fFlags & SUPSECMAIN_FLAGS_FIRST_PROCESS)
    {
        /*
         * Try shake anyone (e.g. easyhook) patching process creation code in
         * kernelbase, kernel32 or ntdll so they won't so easily cause the child
         * to crash when we respawn and purify it.
         */
        SUP_DPRINTF(("supR3HardenedWinInit: Performing a limited self purification...\n"));
        uint32_t cFixes = 0;
        rc = supHardenedWinVerifyProcess(NtCurrentProcess(), NtCurrentThread(), SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED,
                                         0 /*fFlags*/, &cFixes, NULL /*pErrInfo*/);
        SUP_DPRINTF(("supR3HardenedWinInit: SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED -> %Rrc, cFixes=%d\n", rc, cFixes));
        RT_NOREF(rc); /* ignored on purpose */
    }

#ifndef VBOX_WITH_VISTA_NO_SP
    /*
     * Complain about Vista w/o service pack if we're launching a VM.
     */
    if (   !(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV)
        && g_uNtVerCombined >= SUP_NT_VER_VISTA
        && g_uNtVerCombined <  SUP_MAKE_NT_VER_COMBINED(6, 0, 6001, 0, 0))
        supR3HardenedFatalMsg("supR3HardenedWinInit", kSupInitOp_Misc, VERR_NOT_SUPPORTED,
                              "Window Vista without any service pack installed is not supported. Please install the latest service pack.");
#endif
}


/**
 * Modifies the DLL search path for testcases.
 *
 * This makes sure the application binary path is in the search path.  When
 * starting a testcase executable in the testcase/ subdirectory this isn't the
 * case by default.  So, unless we do something about it we won't be able to
 * import VBox DLLs.
 *
 * @param   fFlags          The main flags (giving the location).
 * @param   pszAppBinPath   The path to the application binary directory
 *                          (windows style).
 */
DECLHIDDEN(void) supR3HardenedWinModifyDllSearchPath(uint32_t fFlags, const char *pszAppBinPath)
{
    /*
     * For the testcases to work, we must add the app bin directory to the
     * DLL search list before the testcase dll is loaded or it won't be
     * able to find the VBox DLLs.  This is done _after_ VBoxRT.dll is
     * initialized and sets its defaults.
     */
    switch (fFlags & SUPSECMAIN_FLAGS_LOC_MASK)
    {
        case SUPSECMAIN_FLAGS_LOC_TESTCASE:
            break;
        default:
            return;
    }

    /*
     * Dynamically resolve the two APIs we need (the latter uses forwarders on w7).
     */
    HMODULE hModKernel32 = GetModuleHandleW(L"kernel32.dll");

    typedef BOOL (WINAPI *PFNSETDLLDIRECTORY)(LPCWSTR);
    PFNSETDLLDIRECTORY pfnSetDllDir;
    pfnSetDllDir     = (PFNSETDLLDIRECTORY)GetProcAddress(hModKernel32, "SetDllDirectoryW");

    typedef BOOL (WINAPI *PFNSETDEFAULTDLLDIRECTORIES)(DWORD);
    PFNSETDEFAULTDLLDIRECTORIES pfnSetDefDllDirs;
    pfnSetDefDllDirs = (PFNSETDEFAULTDLLDIRECTORIES)GetProcAddress(hModKernel32, "SetDefaultDllDirectories");

    if (pfnSetDllDir != NULL)
    {
        /*
         * Convert the path to UTF-16 and try set it.
         */
        PRTUTF16 pwszAppBinPath = NULL;
        int rc = RTStrToUtf16(pszAppBinPath, &pwszAppBinPath);
        if (RT_SUCCESS(rc))
        {
            if (pfnSetDllDir(pwszAppBinPath))
            {
                SUP_DPRINTF(("supR3HardenedWinModifyDllSearchPath: Set dll dir to '%ls'\n", pwszAppBinPath));
                g_fSupLibHardenedDllSearchUserDirs = true;

                /*
                 * We set it alright, on W7 and later we also must modify the
                 * default DLL search order.  See @bugref{6861} for details on
                 * why we don't do this on Vista (also see init-win.cpp in IPRT).
                 */
                if (   pfnSetDefDllDirs
                    && g_uNtVerCombined >= SUP_NT_VER_W70)
                {
                    if (pfnSetDefDllDirs(  LOAD_LIBRARY_SEARCH_APPLICATION_DIR
                                         | LOAD_LIBRARY_SEARCH_SYSTEM32
                                         | LOAD_LIBRARY_SEARCH_USER_DIRS))
                        SUP_DPRINTF(("supR3HardenedWinModifyDllSearchPath: Successfully modified search dirs.\n"));
                    else
                        supR3HardenedFatal("supR3HardenedWinModifyDllSearchPath: SetDllDirectoryW(%ls) failed: %d\n",
                                           pwszAppBinPath, RtlGetLastWin32Error());
                }
            }
            else
                supR3HardenedFatal("supR3HardenedWinModifyDllSearchPath: SetDllDirectoryW(%ls) failed: %d\n",
                                   pwszAppBinPath, RtlGetLastWin32Error());
            RTUtf16Free(pwszAppBinPath);
        }
        else
            supR3HardenedFatal("supR3HardenedWinModifyDllSearchPath: RTStrToUtf16(%s) failed: %d\n", pszAppBinPath, rc);
    }
}


/**
 * Initializes the application binary directory path.
 *
 * This is called once or twice.
 *
 * @param   fFlags          The main flags (giving the location).
 */
DECLHIDDEN(void) supR3HardenedWinInitAppBin(uint32_t fFlags)
{
    USHORT cwc = (USHORT)g_offSupLibHardenedExeNtName - 1;
    g_SupLibHardenedAppBinNtPath.UniStr.Buffer = g_SupLibHardenedAppBinNtPath.awcBuffer;
    memcpy(g_SupLibHardenedAppBinNtPath.UniStr.Buffer, g_SupLibHardenedExeNtPath.UniStr.Buffer, cwc * sizeof(WCHAR));

    switch (fFlags & SUPSECMAIN_FLAGS_LOC_MASK)
    {
        case SUPSECMAIN_FLAGS_LOC_APP_BIN:
            break;
        case SUPSECMAIN_FLAGS_LOC_TESTCASE:
        {
            /* Drop one directory level. */
            USHORT off = cwc;
            WCHAR  wc;
            while (   off > 1
                   && (wc = g_SupLibHardenedAppBinNtPath.UniStr.Buffer[off - 1]) != '\0')
                if (wc != '\\' && wc != '/')
                    off--;
                else
                {
                    if (g_SupLibHardenedAppBinNtPath.UniStr.Buffer[off - 2] == ':')
                        cwc = off;
                    else
                        cwc = off - 1;
                    break;
                }
            break;
        }
        default:
            supR3HardenedFatal("supR3HardenedWinInitAppBin: Unknown program binary location: %#x\n", fFlags);
    }

    g_SupLibHardenedAppBinNtPath.UniStr.Buffer[cwc]   = '\0';
    g_SupLibHardenedAppBinNtPath.UniStr.Length        = cwc * sizeof(WCHAR);
    g_SupLibHardenedAppBinNtPath.UniStr.MaximumLength = sizeof(g_SupLibHardenedAppBinNtPath.awcBuffer);
    SUP_DPRINTF(("supR3HardenedWinInitAppBin(%#x): '%ls'\n", fFlags, g_SupLibHardenedAppBinNtPath.UniStr.Buffer));
}


/**
 * Converts the Windows command line string (UTF-16) to an array of UTF-8
 * arguments suitable for passing to main().
 *
 * @returns Pointer to the argument array.
 * @param   pawcCmdLine         The UTF-16 windows command line to parse.
 * @param   cwcCmdLine          The length of the command line.
 * @param   pcArgs              Where to return the number of arguments.
 */
static char **suplibCommandLineToArgvWStub(PCRTUTF16 pawcCmdLine, size_t cwcCmdLine, int *pcArgs)
{
    /*
     * Convert the command line string to UTF-8.
     */
    char *pszCmdLine = NULL;
    SUPR3HARDENED_ASSERT(RT_SUCCESS(RTUtf16ToUtf8Ex(pawcCmdLine, cwcCmdLine, &pszCmdLine, 0, NULL)));

    /*
     * Parse the command line, carving argument strings out of it.
     */
    int    cArgs          = 0;
    int    cArgsAllocated = 4;
    char **papszArgs      = (char **)RTMemAllocZ(sizeof(char *) * cArgsAllocated);
    char  *pszSrc         = pszCmdLine;
    for (;;)
    {
        /* skip leading blanks. */
        char ch = *pszSrc;
        while (suplibCommandLineIsArgSeparator(ch))
            ch = *++pszSrc;
        if (!ch)
            break;

        /* Add argument to the vector. */
        if (cArgs + 2 >= cArgsAllocated)
        {
            cArgsAllocated *= 2;
            papszArgs = (char **)RTMemRealloc(papszArgs, sizeof(char *) * cArgsAllocated);
        }
        papszArgs[cArgs++] = pszSrc;
        papszArgs[cArgs]   = NULL;

        /* Unquote and unescape the string. */
        char *pszDst = pszSrc++;
        bool fQuoted = false;
        do
        {
            if (ch == '"')
                fQuoted = !fQuoted;
            else if (ch != '\\' || (*pszSrc != '\\' && *pszSrc != '"'))
                *pszDst++ = ch;
            else
            {
                unsigned cSlashes = 0;
                while ((ch = *pszSrc++) == '\\')
                    cSlashes++;
                if (ch == '"')
                {
                    while (cSlashes >= 2)
                    {
                        cSlashes -= 2;
                        *pszDst++ = '\\';
                    }
                    if (cSlashes)
                        *pszDst++ = '"';
                    else
                        fQuoted = !fQuoted;
                }
                else
                {
                    pszSrc--;
                    while (cSlashes-- > 0)
                        *pszDst++ = '\\';
                }
            }

            ch = *pszSrc++;
        } while (ch != '\0' && (fQuoted || !suplibCommandLineIsArgSeparator(ch)));

        /* Terminate the argument. */
        *pszDst = '\0';
        if (!ch)
            break;
    }

    *pcArgs = cArgs;
    return papszArgs;
}


/**
 * Worker for supR3HardenedFindVersionRsrcOffset.
 *
 * @returns RVA the version resource data, UINT32_MAX if not found.
 * @param   pRootDir            The root resource directory.  Expects data to
 *                              follow.
 * @param   cbBuf               The amount of data at pRootDir.
 * @param   offData             The offset to the data entry.
 * @param   pcbData             Where to return the size of the data.
 */
static uint32_t supR3HardenedGetRvaFromRsrcDataEntry(PIMAGE_RESOURCE_DIRECTORY pRootDir, uint32_t cbBuf, uint32_t offData,
                                                     uint32_t *pcbData)
{
    if (   offData <= cbBuf
        && offData + sizeof(IMAGE_RESOURCE_DATA_ENTRY) <= cbBuf)
    {
        PIMAGE_RESOURCE_DATA_ENTRY pRsrcData = (PIMAGE_RESOURCE_DATA_ENTRY)((uintptr_t)pRootDir + offData);
        SUP_DPRINTF(("    [Raw version resource data: %#x LB %#x, codepage %#x (reserved %#x)]\n",
                     pRsrcData->OffsetToData, pRsrcData->Size, pRsrcData->CodePage, pRsrcData->Reserved));
        if (pRsrcData->Size > 0)
        {
            *pcbData = pRsrcData->Size;
            return pRsrcData->OffsetToData;
        }
    }
    else
        SUP_DPRINTF(("    Version resource data (%#x) is outside the buffer (%#x)! :-(\n", offData, cbBuf));

    *pcbData = 0;
    return UINT32_MAX;
}


/** @def SUP_RSRC_DPRINTF
 * Dedicated debug printf for resource directory parsing.
 * @sa SUP_DPRINTF
 */
#if 0 /* more details */
# define SUP_RSRC_DPRINTF(a) SUP_DPRINTF(a)
#else
# define SUP_RSRC_DPRINTF(a) do { } while (0)
#endif

/**
 * Scans the resource directory for a version resource.
 *
 * @returns RVA of the version resource data, UINT32_MAX if not found.
 * @param   pRootDir            The root resource directory.  Expects data to
 *                              follow.
 * @param   cbBuf               The amount of data at pRootDir.
 * @param   pcbData             Where to return the size of the version data.
 */
static uint32_t supR3HardenedFindVersionRsrcRva(PIMAGE_RESOURCE_DIRECTORY pRootDir, uint32_t cbBuf, uint32_t *pcbData)
{
    SUP_RSRC_DPRINTF(("    ResDir: Char=%#x Time=%#x Ver=%d%d #NamedEntries=%#x #IdEntries=%#x\n",
                      pRootDir->Characteristics,
                      pRootDir->TimeDateStamp,
                      pRootDir->MajorVersion,
                      pRootDir->MinorVersion,
                      pRootDir->NumberOfNamedEntries,
                      pRootDir->NumberOfIdEntries));

    PIMAGE_RESOURCE_DIRECTORY_ENTRY paEntries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(pRootDir + 1);
    unsigned cMaxEntries = (cbBuf - sizeof(IMAGE_RESOURCE_DIRECTORY)) / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
    unsigned cEntries    = pRootDir->NumberOfNamedEntries + pRootDir->NumberOfIdEntries;
    if (cEntries > cMaxEntries)
        cEntries = cMaxEntries;
    for (unsigned i = 0; i < cEntries; i++)
    {
        if (!paEntries[i].NameIsString)
        {
            if (!paEntries[i].DataIsDirectory)
                SUP_RSRC_DPRINTF(("    #%u:   ID: #%#06x  Data: %#010x\n",
                                i, paEntries[i].Id, paEntries[i].OffsetToData));
            else
                SUP_RSRC_DPRINTF(("    #%u:   ID: #%#06x  Dir: %#010x\n",
                                i, paEntries[i].Id, paEntries[i].OffsetToDirectory));
        }
        else
        {
            if (!paEntries[i].DataIsDirectory)
                SUP_RSRC_DPRINTF(("    #%u: Name: #%#06x  Data: %#010x\n",
                                i, paEntries[i].NameOffset, paEntries[i].OffsetToData));
            else
                SUP_RSRC_DPRINTF(("    #%u: Name: #%#06x  Dir: %#010x\n",
                                i, paEntries[i].NameOffset, paEntries[i].OffsetToDirectory));
        }

        /*
         * Look for the version resource type.  Skip to the next entry if not found.
         */
        if (paEntries[i].NameIsString)
            continue;
        if (paEntries[i].Id != 0x10 /*RT_VERSION*/)
            continue;
        if (!paEntries[i].DataIsDirectory)
        {
            SUP_DPRINTF(("    #%u:   ID: #%#06x  Data: %#010x - WEIRD!\n", i, paEntries[i].Id, paEntries[i].OffsetToData));
            continue;
        }
        SUP_RSRC_DPRINTF(("    Version resource dir entry #%u: dir offset: %#x (cbBuf=%#x)\n",
                          i, paEntries[i].OffsetToDirectory, cbBuf));

        /*
         * Locate the sub-resource directory for it.
         */
        if (paEntries[i].OffsetToDirectory >= cbBuf)
        {
            SUP_DPRINTF(("    Version resource dir is outside the buffer! :-(\n"));
            continue;
        }
        uint32_t cbMax = cbBuf - paEntries[i].OffsetToDirectory;
        if (cbMax < sizeof(IMAGE_RESOURCE_DIRECTORY) + sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))
        {
            SUP_DPRINTF(("    Version resource dir entry #0 is outside the buffer! :-(\n"));
            continue;
        }
        PIMAGE_RESOURCE_DIRECTORY pVerDir = (PIMAGE_RESOURCE_DIRECTORY)((uintptr_t)pRootDir + paEntries[i].OffsetToDirectory);
        SUP_RSRC_DPRINTF(("    VerDir: Char=%#x Time=%#x Ver=%d%d #NamedEntries=%#x #IdEntries=%#x\n",
                          pVerDir->Characteristics,
                          pVerDir->TimeDateStamp,
                          pVerDir->MajorVersion,
                          pVerDir->MinorVersion,
                          pVerDir->NumberOfNamedEntries,
                          pVerDir->NumberOfIdEntries));
        PIMAGE_RESOURCE_DIRECTORY_ENTRY paVerEntries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(pVerDir + 1);
        unsigned cMaxVerEntries = (cbMax - sizeof(IMAGE_RESOURCE_DIRECTORY)) / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
        unsigned cVerEntries    = pVerDir->NumberOfNamedEntries + pVerDir->NumberOfIdEntries;
        if (cVerEntries > cMaxVerEntries)
            cVerEntries = cMaxVerEntries;
        for (unsigned iVer = 0; iVer < cVerEntries; iVer++)
        {
            if (!paVerEntries[iVer].NameIsString)
            {
                if (!paVerEntries[iVer].DataIsDirectory)
                    SUP_RSRC_DPRINTF(("      #%u:   ID: #%#06x  Data: %#010x\n",
                                      iVer, paVerEntries[iVer].Id, paVerEntries[iVer].OffsetToData));
                else
                    SUP_RSRC_DPRINTF(("      #%u:   ID: #%#06x  Dir: %#010x\n",
                                      iVer, paVerEntries[iVer].Id, paVerEntries[iVer].OffsetToDirectory));
            }
            else
            {
                if (!paVerEntries[iVer].DataIsDirectory)
                    SUP_RSRC_DPRINTF(("      #%u: Name: #%#06x  Data: %#010x\n",
                                      iVer, paVerEntries[iVer].NameOffset, paVerEntries[iVer].OffsetToData));
                else
                    SUP_RSRC_DPRINTF(("      #%u: Name: #%#06x  Dir: %#010x\n",
                                      iVer, paVerEntries[iVer].NameOffset, paVerEntries[iVer].OffsetToDirectory));
            }
            if (!paVerEntries[iVer].DataIsDirectory)
            {
                SUP_DPRINTF(("    [Version info resource found at %#x! (ID/Name: #%#x)]\n",
                             paVerEntries[iVer].OffsetToData, paVerEntries[iVer].Name));
                return supR3HardenedGetRvaFromRsrcDataEntry(pRootDir, cbBuf, paVerEntries[iVer].OffsetToData, pcbData);
            }

            /*
             * Check out the next directory level.
             */
            if (paVerEntries[iVer].OffsetToDirectory >= cbBuf)
            {
                SUP_DPRINTF(("    Version resource subdir is outside the buffer! :-(\n"));
                continue;
            }
            cbMax = cbBuf - paVerEntries[iVer].OffsetToDirectory;
            if (cbMax < sizeof(IMAGE_RESOURCE_DIRECTORY) + sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))
            {
                SUP_DPRINTF(("    Version resource subdir entry #0 is outside the buffer! :-(\n"));
                continue;
            }
            PIMAGE_RESOURCE_DIRECTORY pVerSubDir = (PIMAGE_RESOURCE_DIRECTORY)((uintptr_t)pRootDir + paVerEntries[iVer].OffsetToDirectory);
            SUP_RSRC_DPRINTF(("      VerSubDir#%u: Char=%#x Time=%#x Ver=%d%d #NamedEntries=%#x #IdEntries=%#x\n",
                              iVer,
                              pVerSubDir->Characteristics,
                              pVerSubDir->TimeDateStamp,
                              pVerSubDir->MajorVersion,
                              pVerSubDir->MinorVersion,
                              pVerSubDir->NumberOfNamedEntries,
                              pVerSubDir->NumberOfIdEntries));
            PIMAGE_RESOURCE_DIRECTORY_ENTRY paVerSubEntries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(pVerSubDir + 1);
            unsigned cMaxVerSubEntries = (cbMax - sizeof(IMAGE_RESOURCE_DIRECTORY)) / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
            unsigned cVerSubEntries    = pVerSubDir->NumberOfNamedEntries + pVerSubDir->NumberOfIdEntries;
            if (cVerSubEntries > cMaxVerSubEntries)
                cVerSubEntries = cMaxVerSubEntries;
            for (unsigned iVerSub = 0; iVerSub < cVerSubEntries; iVerSub++)
            {
                if (!paVerSubEntries[iVerSub].NameIsString)
                {
                    if (!paVerSubEntries[iVerSub].DataIsDirectory)
                        SUP_RSRC_DPRINTF(("        #%u:   ID: #%#06x  Data: %#010x\n",
                                          iVerSub, paVerSubEntries[iVerSub].Id, paVerSubEntries[iVerSub].OffsetToData));
                    else
                        SUP_RSRC_DPRINTF(("        #%u:   ID: #%#06x  Dir: %#010x\n",
                                          iVerSub, paVerSubEntries[iVerSub].Id, paVerSubEntries[iVerSub].OffsetToDirectory));
                }
                else
                {
                    if (!paVerSubEntries[iVerSub].DataIsDirectory)
                        SUP_RSRC_DPRINTF(("        #%u: Name: #%#06x  Data: %#010x\n",
                                          iVerSub, paVerSubEntries[iVerSub].NameOffset, paVerSubEntries[iVerSub].OffsetToData));
                    else
                        SUP_RSRC_DPRINTF(("        #%u: Name: #%#06x  Dir: %#010x\n",
                                          iVerSub, paVerSubEntries[iVerSub].NameOffset, paVerSubEntries[iVerSub].OffsetToDirectory));
                }
                if (!paVerSubEntries[iVerSub].DataIsDirectory)
                {
                    SUP_DPRINTF(("    [Version info resource found at %#x! (ID/Name: %#x; SubID/SubName: %#x)]\n",
                                 paVerSubEntries[iVerSub].OffsetToData, paVerEntries[iVer].Name, paVerSubEntries[iVerSub].Name));
                    return supR3HardenedGetRvaFromRsrcDataEntry(pRootDir, cbBuf, paVerSubEntries[iVerSub].OffsetToData, pcbData);
                }
            }
        }
    }

    *pcbData = 0;
    return UINT32_MAX;
}


/**
 * Logs information about a file from a protection product or from Windows,
 * optionally returning the file version.
 *
 * The purpose here is to better see which version of the product is installed
 * and not needing to depend on the user supplying the correct information.
 *
 * @param   pwszFile        The NT path to the file.
 * @param   pwszFileVersion Where to return the file version, if found. NULL if
 *                          not interested.
 * @param   cwcFileVersion  The size of the file version buffer (UTF-16 units).
 */
static void supR3HardenedLogFileInfo(PCRTUTF16 pwszFile, PRTUTF16 pwszFileVersion, size_t cwcFileVersion)
{
    /*
     * Make sure the file version is always set when we return.
     */
    if (pwszFileVersion && cwcFileVersion)
        *pwszFileVersion = '\0';

    /*
     * Open the file.
     */
    HANDLE              hFile  = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios    = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    UNICODE_STRING      UniStrName;
    UniStrName.Buffer = (WCHAR *)pwszFile;
    UniStrName.Length = (USHORT)(RTUtf16Len(pwszFile) * sizeof(WCHAR));
    UniStrName.MaximumLength = UniStrName.Length + sizeof(WCHAR);
    OBJECT_ATTRIBUTES   ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &UniStrName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 GENERIC_READ | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ,
                                 FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (NT_SUCCESS(rcNt))
    {
        SUP_DPRINTF(("%ls:\n", pwszFile));
        union
        {
            uint64_t                    u64AlignmentInsurance;
            FILE_BASIC_INFORMATION      BasicInfo;
            FILE_STANDARD_INFORMATION   StdInfo;
            uint8_t                     abBuf[32768];
            RTUTF16                     awcBuf[16384];
            IMAGE_DOS_HEADER            MzHdr;
            IMAGE_RESOURCE_DIRECTORY    ResDir;
        } u;
        RTTIMESPEC  TimeSpec;
        char        szTmp[64];

        /*
         * Print basic file information available via NtQueryInformationFile.
         */
        RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
        rcNt = NtQueryInformationFile(hFile, &Ios, &u.BasicInfo, sizeof(u.BasicInfo), FileBasicInformation);
        if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
        {
            SUP_DPRINTF(("    CreationTime:    %s\n", RTTimeSpecToString(RTTimeSpecSetNtTime(&TimeSpec, u.BasicInfo.CreationTime.QuadPart), szTmp, sizeof(szTmp))));
            /*SUP_DPRINTF(("    LastAccessTime:  %s\n", RTTimeSpecToString(RTTimeSpecSetNtTime(&TimeSpec, u.BasicInfo.LastAccessTime.QuadPart), szTmp, sizeof(szTmp))));*/
            SUP_DPRINTF(("    LastWriteTime:   %s\n", RTTimeSpecToString(RTTimeSpecSetNtTime(&TimeSpec, u.BasicInfo.LastWriteTime.QuadPart), szTmp, sizeof(szTmp))));
            SUP_DPRINTF(("    ChangeTime:      %s\n", RTTimeSpecToString(RTTimeSpecSetNtTime(&TimeSpec, u.BasicInfo.ChangeTime.QuadPart), szTmp, sizeof(szTmp))));
            SUP_DPRINTF(("    FileAttributes:  %#x\n", u.BasicInfo.FileAttributes));
        }
        else
            SUP_DPRINTF(("    FileBasicInformation -> %#x %#x\n", rcNt, Ios.Status));

        RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
        rcNt = NtQueryInformationFile(hFile, &Ios, &u.StdInfo, sizeof(u.StdInfo), FileStandardInformation);
        if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
            SUP_DPRINTF(("    Size:            %#llx\n", u.StdInfo.EndOfFile.QuadPart));
        else
            SUP_DPRINTF(("    FileStandardInformation -> %#x %#x\n", rcNt, Ios.Status));

        /*
         * Read the image header and extract the timestamp and other useful info.
         */
        RT_ZERO(u);
        RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
        LARGE_INTEGER offRead;
        offRead.QuadPart = 0;
        rcNt = NtReadFile(hFile, NULL /*hEvent*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/, &Ios,
                          &u, (ULONG)sizeof(u), &offRead, NULL);
        if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
        {
            uint32_t offNtHdrs = 0;
            if (u.MzHdr.e_magic == IMAGE_DOS_SIGNATURE)
                offNtHdrs = u.MzHdr.e_lfanew;
            if (offNtHdrs < sizeof(u) - sizeof(IMAGE_NT_HEADERS))
            {
                PIMAGE_NT_HEADERS64 pNtHdrs64 = (PIMAGE_NT_HEADERS64)&u.abBuf[offNtHdrs];
                PIMAGE_NT_HEADERS32 pNtHdrs32 = (PIMAGE_NT_HEADERS32)&u.abBuf[offNtHdrs];
                if (pNtHdrs64->Signature == IMAGE_NT_SIGNATURE)
                {
                    SUP_DPRINTF(("    NT Headers:      %#x\n", offNtHdrs));
                    SUP_DPRINTF(("    Timestamp:       %#x\n", pNtHdrs64->FileHeader.TimeDateStamp));
                    SUP_DPRINTF(("    Machine:         %#x%s\n", pNtHdrs64->FileHeader.Machine,
                                 pNtHdrs64->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 ? " - i386"
                                 : pNtHdrs64->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ? " - amd64" : ""));
                    SUP_DPRINTF(("    Timestamp:       %#x\n", pNtHdrs64->FileHeader.TimeDateStamp));
                    SUP_DPRINTF(("    Image Version:   %u.%u\n",
                                 pNtHdrs64->OptionalHeader.MajorImageVersion, pNtHdrs64->OptionalHeader.MinorImageVersion));
                    SUP_DPRINTF(("    SizeOfImage:     %#x (%u)\n", pNtHdrs64->OptionalHeader.SizeOfImage, pNtHdrs64->OptionalHeader.SizeOfImage));

                    /*
                     * Very crude way to extract info from the file version resource.
                     */
                    PIMAGE_SECTION_HEADER paSectHdrs = (PIMAGE_SECTION_HEADER)(  (uintptr_t)&pNtHdrs64->OptionalHeader
                                                                               + pNtHdrs64->FileHeader.SizeOfOptionalHeader);
                    IMAGE_DATA_DIRECTORY  RsrcDir = { 0, 0 };
                    if (   pNtHdrs64->FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64)
                        && pNtHdrs64->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_RESOURCE)
                        RsrcDir = pNtHdrs64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
                    else if (   pNtHdrs64->FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER32)
                             && pNtHdrs32->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_RESOURCE)
                        RsrcDir = pNtHdrs32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
                    SUP_DPRINTF(("    Resource Dir:    %#x LB %#x\n", RsrcDir.VirtualAddress, RsrcDir.Size));
                    if (   RsrcDir.VirtualAddress > offNtHdrs
                        && RsrcDir.Size > 0
                        &&    (uintptr_t)&u + sizeof(u) - (uintptr_t)paSectHdrs
                           >= pNtHdrs64->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) )
                    {
                        uint32_t uRvaRsrcSect = 0;
                        uint32_t cbRsrcSect   = 0;
                        uint32_t offRsrcSect  = 0;
                        offRead.QuadPart = 0;
                        for (uint32_t i = 0; i < pNtHdrs64->FileHeader.NumberOfSections; i++)
                        {
                            uRvaRsrcSect = paSectHdrs[i].VirtualAddress;
                            cbRsrcSect   = paSectHdrs[i].Misc.VirtualSize;
                            offRsrcSect  = paSectHdrs[i].PointerToRawData;
                            if (   RsrcDir.VirtualAddress - uRvaRsrcSect < cbRsrcSect
                                && offRsrcSect > offNtHdrs)
                            {
                                offRead.QuadPart = offRsrcSect + (RsrcDir.VirtualAddress - uRvaRsrcSect);
                                break;
                            }
                        }
                        if (offRead.QuadPart > 0)
                        {
                            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
                            RT_ZERO(u);
                            rcNt = NtReadFile(hFile, NULL /*hEvent*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/, &Ios,
                                              &u, (ULONG)sizeof(u), &offRead, NULL);
                            PCRTUTF16 pwcVersionData = &u.awcBuf[0];
                            size_t    cbVersionData  = sizeof(u);

                            if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
                            {
                                /* Make it less crude by try find the version resource data. */
                                uint32_t  cbVersion;
                                uint32_t  uRvaVersion = supR3HardenedFindVersionRsrcRva(&u.ResDir, sizeof(u), &cbVersion);
                                NOREF(uRvaVersion);
                                if (   uRvaVersion != UINT32_MAX
                                    && cbVersion < cbRsrcSect
                                    && uRvaVersion - uRvaRsrcSect <= cbRsrcSect - cbVersion)
                                {
                                    uint32_t const offVersion = uRvaVersion - uRvaRsrcSect;
                                    if (   offVersion < sizeof(u)
                                        && offVersion + cbVersion <= sizeof(u))
                                    {
                                        pwcVersionData = (PCRTUTF16)&u.abBuf[offVersion];
                                        cbVersionData  = cbVersion;
                                    }
                                    else
                                    {
                                        offRead.QuadPart = offVersion + offRsrcSect;
                                        RT_ZERO(u);
                                        rcNt = NtReadFile(hFile, NULL /*hEvent*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/, &Ios,
                                                          &u, (ULONG)sizeof(u), &offRead, NULL);
                                        pwcVersionData = &u.awcBuf[0];
                                        cbVersionData  = RT_MIN(cbVersion, sizeof(u));
                                    }
                                }
                            }

                            if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
                            {
                                static const struct { PCRTUTF16 pwsz; size_t cb; bool fRet; } s_abFields[] =
                                {
#define MY_WIDE_STR_TUPLE(a_sz, a_fRet) { L ## a_sz, sizeof(L ## a_sz) - sizeof(RTUTF16), a_fRet }
                                    MY_WIDE_STR_TUPLE("ProductName",        false),
                                    MY_WIDE_STR_TUPLE("ProductVersion",     false),
                                    MY_WIDE_STR_TUPLE("FileVersion",        true),
                                    MY_WIDE_STR_TUPLE("SpecialBuild",       false),
                                    MY_WIDE_STR_TUPLE("PrivateBuild",       false),
                                    MY_WIDE_STR_TUPLE("FileDescription",    false),
#undef MY_WIDE_STR_TUPLE
                                };
                                for (uint32_t i = 0; i < RT_ELEMENTS(s_abFields); i++)
                                {
                                    if (cbVersionData <= s_abFields[i].cb + 10)
                                        continue;
                                    size_t          cwcLeft = (cbVersionData - s_abFields[i].cb - 10) / sizeof(RTUTF16);
                                    PCRTUTF16       pwc     = pwcVersionData;
                                    RTUTF16 const   wcFirst = *s_abFields[i].pwsz;
                                    while (cwcLeft-- > 0)
                                    {
                                        if (   pwc[0] == 1 /* wType == text */
                                            && pwc[1] == wcFirst)
                                        {
                                            if (memcmp(pwc + 1, s_abFields[i].pwsz, s_abFields[i].cb + sizeof(RTUTF16)) == 0)
                                            {
                                                size_t cwcField = s_abFields[i].cb / sizeof(RTUTF16);
                                                pwc     += cwcField + 2;
                                                cwcLeft -= cwcField + 2;
                                                for (uint32_t iPadding = 0; iPadding < 3; iPadding++, pwc++, cwcLeft--)
                                                    if (*pwc)
                                                        break;
                                                int rc = RTUtf16ValidateEncodingEx(pwc, cwcLeft,
                                                                                   RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                                                if (RT_SUCCESS(rc))
                                                {
                                                    SUP_DPRINTF(("    %ls:%*s %ls",
                                                                 s_abFields[i].pwsz, cwcField < 15 ? 15 - cwcField : 0, "", pwc));
                                                    if (   s_abFields[i].fRet
                                                        && pwszFileVersion
                                                        && cwcFileVersion > 1)
                                                        RTUtf16Copy(pwszFileVersion, cwcFileVersion, pwc);
                                                }
                                                else
                                                    SUP_DPRINTF(("    %ls:%*s rc=%Rrc",
                                                                 s_abFields[i].pwsz, cwcField < 15 ? 15 - cwcField : 0, "", rc));

                                                break;
                                            }
                                        }
                                        pwc++;
                                    }
                                }
                            }
                            else
                                SUP_DPRINTF(("    NtReadFile @%#llx -> %#x %#x\n", offRead.QuadPart, rcNt, Ios.Status));
                        }
                        else
                            SUP_DPRINTF(("    Resource section not found.\n"));
                    }
                }
                else
                    SUP_DPRINTF(("    Nt Headers @%#x: Invalid signature\n", offNtHdrs));
            }
            else
                SUP_DPRINTF(("    Nt Headers @%#x: out side buffer\n", offNtHdrs));
        }
        else
            SUP_DPRINTF(("    NtReadFile @0 -> %#x %#x\n", rcNt, Ios.Status));
        NtClose(hFile);
    }
}


/**
 * Scans the Driver directory for drivers which may invade our processes.
 *
 * @returns Mask of SUPHARDNT_ADVERSARY_XXX flags.
 *
 * @remarks The enumeration of \\Driver normally requires administrator
 *          privileges.  So, the detection we're doing here isn't always gonna
 *          work just based on that.
 *
 * @todo    Find drivers in \\FileSystems as well, then we could detect VrNsdDrv
 *          from ViRobot APT Shield 2.0.
 */
static uint32_t supR3HardenedWinFindAdversaries(void)
{
    static const struct
    {
        uint32_t    fAdversary;
        const char *pszDriver;
    } s_aDrivers[] =
    {
        { SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT,    "SysPlant" },

        { SUPHARDNT_ADVERSARY_SYMANTEC_N360,        "SRTSPX" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360,        "SymDS" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360,        "SymEvent" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360,        "SymIRON" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360,        "SymNetS" },

        { SUPHARDNT_ADVERSARY_AVAST,                "aswHwid" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswMonFlt" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswRdr2" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswRvrt" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswSnx" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswsp" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswStm" },
        { SUPHARDNT_ADVERSARY_AVAST,                "aswVmm" },

        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmcomm" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmactmon" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmevtmgr" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmtdi" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmebc64" },  /* Titanium internet security, not officescan. */
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmeevw" },   /* Titanium internet security, not officescan. */
        { SUPHARDNT_ADVERSARY_TRENDMICRO,           "tmciesc" },  /* Titanium internet security, not officescan. */

        { SUPHARDNT_ADVERSARY_MCAFEE,               "cfwids" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "McPvDrv" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfeapfk" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfeavfk" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfefirek" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfehidk" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfencbdc" },
        { SUPHARDNT_ADVERSARY_MCAFEE,               "mfewfpk" },

        { SUPHARDNT_ADVERSARY_KASPERSKY,            "kl1" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "klflt" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "klif" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "KLIM6" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "klkbdflt" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "klmouflt" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "kltdi" },
        { SUPHARDNT_ADVERSARY_KASPERSKY,            "kneps" },

        { SUPHARDNT_ADVERSARY_MBAM,                 "MBAMWebAccessControl" },
        { SUPHARDNT_ADVERSARY_MBAM,                 "mbam" },
        { SUPHARDNT_ADVERSARY_MBAM,                 "mbamchameleon" },
        { SUPHARDNT_ADVERSARY_MBAM,                 "mwav" },
        { SUPHARDNT_ADVERSARY_MBAM,                 "mbamswissarmy" },

        { SUPHARDNT_ADVERSARY_AVG,                  "avgfwfd" },
        { SUPHARDNT_ADVERSARY_AVG,                  "avgtdia" },

        { SUPHARDNT_ADVERSARY_PANDA,                "PSINAflt" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSINFile" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSINKNC" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSINProc" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSINProt" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSINReg" },
        { SUPHARDNT_ADVERSARY_PANDA,                "PSKMAD" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSAlpc" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSHttp" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNShttps" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSIds" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSNAHSL" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSpicc" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSPihsw" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSPop3" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSProt" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSPrv" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSSmtp" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNSStrm" },
        { SUPHARDNT_ADVERSARY_PANDA,                "NNStlsc" },

        { SUPHARDNT_ADVERSARY_MSE,                  "NisDrv" },

        /*{ SUPHARDNT_ADVERSARY_COMODO, "cmdguard" }, file system */
        { SUPHARDNT_ADVERSARY_COMODO,               "inspect" },
        { SUPHARDNT_ADVERSARY_COMODO,               "cmdHlp" },

        { SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD, "dgmaster" },

        { SUPHARDNT_ADVERSARY_CYLANCE,              "cyprotectdrv" }, /* Not verified. */

        { SUPHARDNT_ADVERSARY_BEYONDTRUST,          "privman" },   /* Not verified. */
        { SUPHARDNT_ADVERSARY_BEYONDTRUST,          "privmanfi" }, /* Not verified. */

        { SUPHARDNT_ADVERSARY_AVECTO,               "PGDriver" },

        { SUPHARDNT_ADVERSARY_SOPHOS,               "SophosED" }, /* Not verified. */

        { SUPHARDNT_ADVERSARY_HORIZON_VIEW_AGENT,   "vmwicpdr" },
    };

    static const struct
    {
        uint32_t    fAdversary;
        PCRTUTF16   pwszFile;
    } s_aFiles[] =
    {
        { SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT, L"\\SystemRoot\\System32\\drivers\\SysPlant.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT, L"\\SystemRoot\\System32\\sysfer.dll" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_SYSPLANT, L"\\SystemRoot\\System32\\sysferThunk.dll" },

        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\ccsetx64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\ironx64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\srtsp64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\srtspx64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\symds64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\symefa64.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\symelam.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\N360x64\\1505000.013\\symnets.sys" },
        { SUPHARDNT_ADVERSARY_SYMANTEC_N360, L"\\SystemRoot\\System32\\drivers\\symevent64x86.sys" },

        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswHwid.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswMonFlt.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswRdr2.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswRvrt.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswSnx.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswsp.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswStm.sys" },
        { SUPHARDNT_ADVERSARY_AVAST, L"\\SystemRoot\\System32\\drivers\\aswVmm.sys" },

        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmcomm.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmactmon.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmevtmgr.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmtdi.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmebc64.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmeevw.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\tmciesc.sys" },
        { SUPHARDNT_ADVERSARY_TRENDMICRO_SAKFILE, L"\\SystemRoot\\System32\\drivers\\sakfile.sys" },  /* Data Loss Prevention, not officescan. */
        { SUPHARDNT_ADVERSARY_TRENDMICRO, L"\\SystemRoot\\System32\\drivers\\sakcd.sys" },  /* Data Loss Prevention, not officescan. */


        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\cfwids.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\McPvDrv.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfeapfk.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfeavfk.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfefirek.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfehidk.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfencbdc.sys" },
        { SUPHARDNT_ADVERSARY_MCAFEE, L"\\SystemRoot\\System32\\drivers\\mfewfpk.sys" },

        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\kl1.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\klflt.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\klif.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\klim6.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\klkbdflt.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\klmouflt.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\kltdi.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\drivers\\kneps.sys" },
        { SUPHARDNT_ADVERSARY_KASPERSKY, L"\\SystemRoot\\System32\\klfphc.dll" },

        { SUPHARDNT_ADVERSARY_MBAM, L"\\SystemRoot\\System32\\drivers\\MBAMSwissArmy.sys" },
        { SUPHARDNT_ADVERSARY_MBAM, L"\\SystemRoot\\System32\\drivers\\mwac.sys" },
        { SUPHARDNT_ADVERSARY_MBAM, L"\\SystemRoot\\System32\\drivers\\mbamchameleon.sys" },
        { SUPHARDNT_ADVERSARY_MBAM, L"\\SystemRoot\\System32\\drivers\\mbam.sys" },

        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgrkx64.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgmfx64.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgidsdrivera.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgidsha.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgtdia.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgloga.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgldx64.sys" },
        { SUPHARDNT_ADVERSARY_AVG, L"\\SystemRoot\\System32\\drivers\\avgdiska.sys" },

        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINAflt.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINFile.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINKNC.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINProc.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINProt.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSINReg.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\PSKMAD.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSAlpc.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSHttp.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNShttps.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSIds.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSNAHSL.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSpicc.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSPihsw.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSPop3.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSProt.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSPrv.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSSmtp.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNSStrm.sys" },
        { SUPHARDNT_ADVERSARY_PANDA, L"\\SystemRoot\\System32\\drivers\\NNStlsc.sys" },

        { SUPHARDNT_ADVERSARY_MSE, L"\\SystemRoot\\System32\\drivers\\MpFilter.sys" },
        { SUPHARDNT_ADVERSARY_MSE, L"\\SystemRoot\\System32\\drivers\\NisDrvWFP.sys" },

        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\cmdguard.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\cmderd.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\inspect.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\cmdhlp.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\cfrmd.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\drivers\\hmd.sys" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\guard64.dll" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\cmdvrt64.dll" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\cmdkbd64.dll" },
        { SUPHARDNT_ADVERSARY_COMODO, L"\\SystemRoot\\System32\\cmdcsr.dll" },

        { SUPHARDNT_ADVERSARY_ZONE_ALARM, L"\\SystemRoot\\System32\\drivers\\vsdatant.sys" },
        { SUPHARDNT_ADVERSARY_ZONE_ALARM, L"\\SystemRoot\\System32\\AntiTheftCredentialProvider.dll" },

        { SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD, L"\\SystemRoot\\System32\\drivers\\dgmaster.sys" },

        { SUPHARDNT_ADVERSARY_CYLANCE, L"\\SystemRoot\\System32\\drivers\\cyprotectdrv32.sys" },
        { SUPHARDNT_ADVERSARY_CYLANCE, L"\\SystemRoot\\System32\\drivers\\cyprotectdrv64.sys" },

        { SUPHARDNT_ADVERSARY_BEYONDTRUST, L"\\SystemRoot\\System32\\drivers\\privman.sys" },
        { SUPHARDNT_ADVERSARY_BEYONDTRUST, L"\\SystemRoot\\System32\\drivers\\privmanfi.sys" },
        { SUPHARDNT_ADVERSARY_BEYONDTRUST, L"\\SystemRoot\\System32\\privman64.dll" },
        { SUPHARDNT_ADVERSARY_BEYONDTRUST, L"\\SystemRoot\\System32\\privman32.dll" },

        { SUPHARDNT_ADVERSARY_AVECTO, L"\\SystemRoot\\System32\\drivers\\PGDriver.sys" },

        { SUPHARDNT_ADVERSARY_SOPHOS, L"\\SystemRoot\\System32\\drivers\\SophosED.sys" }, // not verified

        { SUPHARDNT_ADVERSARY_HORIZON_VIEW_AGENT, L"\\SystemRoot\\System32\\drivers\\vmwicpdr.sys" },
        { SUPHARDNT_ADVERSARY_HORIZON_VIEW_AGENT, L"\\SystemRoot\\System32\\drivers\\ftsjail.sys" },
    };

    uint32_t fFound = 0;

    /*
     * Open the driver object directory.
     */
    UNICODE_STRING NtDirName = RTNT_CONSTANT_UNISTR(L"\\Driver");

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtDirName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    HANDLE hDir;
    NTSTATUS rcNt = NtOpenDirectoryObject(&hDir, DIRECTORY_QUERY | FILE_LIST_DIRECTORY, &ObjAttr);
#ifdef VBOX_STRICT
    if (rcNt != STATUS_ACCESS_DENIED) /* non-admin */
        SUPR3HARDENED_ASSERT_NT_SUCCESS(rcNt);
#endif
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Enumerate it, looking for the driver.
         */
        ULONG    uObjDirCtx = 0;
        for (;;)
        {
            uint32_t    abBuffer[_64K + _1K];
            ULONG       cbActual;
            rcNt = NtQueryDirectoryObject(hDir,
                                          abBuffer,
                                          sizeof(abBuffer) - 4, /* minus four for string terminator space. */
                                          FALSE /*ReturnSingleEntry */,
                                          FALSE /*RestartScan*/,
                                          &uObjDirCtx,
                                          &cbActual);
            if (!NT_SUCCESS(rcNt) || cbActual < sizeof(OBJECT_DIRECTORY_INFORMATION))
                break;

            POBJECT_DIRECTORY_INFORMATION pObjDir = (POBJECT_DIRECTORY_INFORMATION)abBuffer;
            while (pObjDir->Name.Length != 0)
            {
                WCHAR wcSaved = pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)];
                pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)] = '\0';

                for (uint32_t i = 0; i < RT_ELEMENTS(s_aDrivers); i++)
                    if (RTUtf16ICmpAscii(pObjDir->Name.Buffer, s_aDrivers[i].pszDriver) == 0)
                    {
                        fFound |= s_aDrivers[i].fAdversary;
                        SUP_DPRINTF(("Found driver %s (%#x)\n", s_aDrivers[i].pszDriver, s_aDrivers[i].fAdversary));
                        break;
                    }

                pObjDir->Name.Buffer[pObjDir->Name.Length / sizeof(WCHAR)] = wcSaved;

                /* Next directory entry. */
                pObjDir++;
            }
        }

        NtClose(hDir);
    }
    else
        SUP_DPRINTF(("NtOpenDirectoryObject failed on \\Driver: %#x\n", rcNt));

    /*
     * Look for files.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aFiles); i++)
    {
        HANDLE              hFile  = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios    = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        UNICODE_STRING      UniStrName;
        UniStrName.Buffer = (WCHAR *)s_aFiles[i].pwszFile;
        UniStrName.Length = (USHORT)(RTUtf16Len(s_aFiles[i].pwszFile) * sizeof(WCHAR));
        UniStrName.MaximumLength = UniStrName.Length + sizeof(WCHAR);
        InitializeObjectAttributes(&ObjAttr, &UniStrName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
        rcNt = NtCreateFile(&hFile, GENERIC_READ | SYNCHRONIZE, &ObjAttr, &Ios, NULL /* Allocation Size*/,
                            FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL /*EaBuffer*/, 0 /*EaLength*/);
        if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
        {
            fFound |= s_aFiles[i].fAdversary;
            NtClose(hFile);
        }
    }

    /*
     * Log details and upgrade select adversaries.
     */
    SUP_DPRINTF(("supR3HardenedWinFindAdversaries: %#x\n", fFound));
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aFiles); i++)
        if (s_aFiles[i].fAdversary & fFound)
        {
            if (!(s_aFiles[i].fAdversary & SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD))
                supR3HardenedLogFileInfo(s_aFiles[i].pwszFile, NULL, 0);
            else
            {
                /*
                 * See if it's a newer version of the driver which doesn't BSODs when we free
                 * its memory.  To use RTStrVersionCompare we do a rough UTF-16 -> ASCII conversion.
                 */
                union
                {
                    char    szFileVersion[64];
                    RTUTF16 wszFileVersion[32];
                } uBuf;
                supR3HardenedLogFileInfo(s_aFiles[i].pwszFile, uBuf.wszFileVersion, RT_ELEMENTS(uBuf.wszFileVersion));
                if (uBuf.wszFileVersion[0])
                {
                    for (uint32_t off = 0; off < RT_ELEMENTS(uBuf.wszFileVersion); off++)
                    {
                        RTUTF16 wch = uBuf.wszFileVersion[off];
                        uBuf.szFileVersion[off] = (char)wch;
                        if (!wch)
                            break;
                    }
                    uBuf.szFileVersion[RT_ELEMENTS(uBuf.wszFileVersion)] = '\0';
#define VER_IN_RANGE(a_pszFirst, a_pszLast) \
    (RTStrVersionCompare(uBuf.szFileVersion, a_pszFirst) >= 0 && RTStrVersionCompare(uBuf.szFileVersion, a_pszLast) <= 0)
                    if (   VER_IN_RANGE("7.3.2.0000", "999999999.9.9.9999")
                        || VER_IN_RANGE("7.3.1.1000", "7.3.1.3000")
                        || VER_IN_RANGE("7.3.0.3000", "7.3.0.999999999")
                        || VER_IN_RANGE("7.2.1.3000", "7.2.999999999.999999999") )
                    {
                        uint32_t const fOldFound = fFound;
                        fFound = (fOldFound & ~SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_OLD)
                               |               SUPHARDNT_ADVERSARY_DIGITAL_GUARDIAN_NEW;
                        SUP_DPRINTF(("supR3HardenedWinFindAdversaries: Found newer version: %#x -> %#x\n", fOldFound, fFound));
                    }
                }
            }
        }

    return fFound;
}


extern "C" int main(int argc, char **argv, char **envp);

/**
 * The executable entry point.
 *
 * This is normally taken care of by the C runtime library, but we don't want to
 * get involved with anything as complicated like the CRT in this setup.  So, we
 * it everything ourselves, including parameter parsing.
 */
extern "C" void __stdcall suplibHardenedWindowsMain(void)
{
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;

    g_cSuplibHardenedWindowsMainCalls++;
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_EP_CALLED;

    /*
     * Initialize the NTDLL API wrappers. This aims at bypassing patched NTDLL
     * in all the processes leading up the VM process.
     */
    supR3HardenedWinInitImports();
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED;

    /*
     * Notify the parent process that we're probably capable of reporting our
     * own errors.
     */
    if (g_ProcParams.hEvtParent || g_ProcParams.hEvtChild)
    {
        SUPR3HARDENED_ASSERT(g_fSupEarlyProcessInit);

        g_ProcParams.enmRequest = kSupR3WinChildReq_CloseEvents;
        NtSetEvent(g_ProcParams.hEvtParent, NULL);

        NtClose(g_ProcParams.hEvtParent);
        NtClose(g_ProcParams.hEvtChild);
        g_ProcParams.hEvtParent = NULL;
        g_ProcParams.hEvtChild  = NULL;
    }
    else
        SUPR3HARDENED_ASSERT(!g_fSupEarlyProcessInit);

    /*
     * After having resolved imports we patch the LdrInitializeThunk code so
     * that it's more difficult to invade our privacy by CreateRemoteThread.
     * We'll re-enable this after opening the driver or temporarily while respawning.
     */
    supR3HardenedWinDisableThreadCreation();

    /*
     * Init g_uNtVerCombined. (The code is shared with SUPR3.lib and lives in
     * SUPHardenedVerfiyImage-win.cpp.)
     */
    supR3HardenedWinInitVersion(false /*fEarly*/);
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_VERSION_INITIALIZED;

    /*
     * Convert the arguments to UTF-8 and open the log file if specified.
     * This must be done as early as possible since the code below may fail.
     */
    PUNICODE_STRING pCmdLineStr = &NtCurrentPeb()->ProcessParameters->CommandLine;
    int    cArgs;
    char **papszArgs = suplibCommandLineToArgvWStub(pCmdLineStr->Buffer, pCmdLineStr->Length / sizeof(WCHAR), &cArgs);

    supR3HardenedOpenLog(&cArgs, papszArgs);

    /*
     * Log information about important system files.
     */
    supR3HardenedLogFileInfo(L"\\SystemRoot\\System32\\ntdll.dll",          NULL /*pwszFileVersion*/, 0 /*cwcFileVersion*/);
    supR3HardenedLogFileInfo(L"\\SystemRoot\\System32\\kernel32.dll",       NULL /*pwszFileVersion*/, 0 /*cwcFileVersion*/);
    supR3HardenedLogFileInfo(L"\\SystemRoot\\System32\\KernelBase.dll",     NULL /*pwszFileVersion*/, 0 /*cwcFileVersion*/);
    supR3HardenedLogFileInfo(L"\\SystemRoot\\System32\\apisetschema.dll",   NULL /*pwszFileVersion*/, 0 /*cwcFileVersion*/);

    /*
     * Scan the system for adversaries, logging information about them.
     */
    g_fSupAdversaries = supR3HardenedWinFindAdversaries();

    /*
     * Get the executable name, make sure it's the long version.
     */
    DWORD cwcExecName = GetModuleFileNameW(GetModuleHandleW(NULL), g_wszSupLibHardenedExePath,
                                           RT_ELEMENTS(g_wszSupLibHardenedExePath));
    if (cwcExecName >= RT_ELEMENTS(g_wszSupLibHardenedExePath))
        supR3HardenedFatalMsg("suplibHardenedWindowsMain", kSupInitOp_Integrity, VERR_BUFFER_OVERFLOW,
                              "The executable path is too long.");

    RTUTF16 wszLong[RT_ELEMENTS(g_wszSupLibHardenedExePath)];
    DWORD cwcLong = GetLongPathNameW(g_wszSupLibHardenedExePath, wszLong, RT_ELEMENTS(wszLong));
    if (cwcLong > 0)
    {
        memcpy(g_wszSupLibHardenedExePath, wszLong, (cwcLong + 1) * sizeof(RTUTF16));
        cwcExecName = cwcLong;
    }

    /* The NT version of it. */
    HANDLE hFile = CreateFileW(g_wszSupLibHardenedExePath, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecurityAttributes*/,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL /*hTemplateFile*/);
    if (hFile == NULL || hFile == INVALID_HANDLE_VALUE)
        supR3HardenedFatalMsg("suplibHardenedWindowsMain", kSupInitOp_Integrity, RTErrConvertFromWin32(RtlGetLastWin32Error()),
                              "Error opening the executable: %u (%ls).", RtlGetLastWin32Error());
    RT_ZERO(g_SupLibHardenedExeNtPath);
    ULONG cbIgn;
    NTSTATUS rcNt = NtQueryObject(hFile, ObjectNameInformation, &g_SupLibHardenedExeNtPath,
                                  sizeof(g_SupLibHardenedExeNtPath) - sizeof(WCHAR), &cbIgn);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatalMsg("suplibHardenedWindowsMain", kSupInitOp_Integrity, RTErrConvertFromNtStatus(rcNt),
                              "NtQueryObject -> %#x (on %ls)\n", rcNt, g_wszSupLibHardenedExePath);
    NtClose(hFile);

    /* The NT executable name offset / dir path length. */
    g_offSupLibHardenedExeNtName = g_SupLibHardenedExeNtPath.UniStr.Length / sizeof(WCHAR);
    while (   g_offSupLibHardenedExeNtName > 1
           && g_SupLibHardenedExeNtPath.UniStr.Buffer[g_offSupLibHardenedExeNtName - 1] != '\\' )
        g_offSupLibHardenedExeNtName--;

    /*
     * Preliminary app binary path init.  May change when SUPR3HardenedMain is
     * called (via main below).
     */
    supR3HardenedWinInitAppBin(SUPSECMAIN_FLAGS_LOC_APP_BIN);

    /*
     * If we've done early init already, register the DLL load notification
     * callback and reinstall the NtDll patches.
     */
    if (g_fSupEarlyProcessInit)
    {
        supR3HardenedWinRegisterDllNotificationCallback();
        supR3HardenedWinReInstallHooks(false /*fFirstCall */);

        /*
         * Flush user APCs before the g_enmSupR3HardenedMainState changes
         * and disables the APC restrictions.
         */
        NtTestAlert();
    }

    /*
     * Call the C/C++ main function.
     */
    SUP_DPRINTF(("Calling main()\n"));
    rcExit = (RTEXITCODE)main(cArgs, papszArgs, NULL);

    /*
     * Exit the process (never return).
     */
    SUP_DPRINTF(("Terminating the normal way: rcExit=%d\n", rcExit));
    suplibHardenedExit(rcExit);
}


/**
 * Reports an error to the parent process via the process parameter structure.
 *
 * @param   pszWhere            Where this error occured, if fatal message. NULL
 *                              if not message.
 * @param   enmWhat             Which init operation went wrong if fatal
 *                              message. kSupInitOp_Invalid if not message.
 * @param   rc                  The status code to report.
 * @param   pszFormat           The format string.
 * @param   va                  The format arguments.
 */
DECLHIDDEN(void) supR3HardenedWinReportErrorToParent(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                     const char *pszFormat, va_list va)
{
    if (pszWhere)
        RTStrCopy(g_ProcParams.szWhere, sizeof(g_ProcParams.szWhere), pszWhere);
    else
        g_ProcParams.szWhere[0] = '\0';
    RTStrPrintfV(g_ProcParams.szErrorMsg, sizeof(g_ProcParams.szErrorMsg), pszFormat, va);
    g_ProcParams.enmWhat = enmWhat;
    g_ProcParams.rc      = RT_SUCCESS(rc) ? VERR_INTERNAL_ERROR_2 : rc;
    g_ProcParams.enmRequest = kSupR3WinChildReq_Error;

    NtClearEvent(g_ProcParams.hEvtChild);
    NTSTATUS rcNt = NtSetEvent(g_ProcParams.hEvtParent, NULL);
    if (NT_SUCCESS(rcNt))
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -300000000; /* 30 second */
        /*NTSTATUS rcNt =*/ NtWaitForSingleObject(g_ProcParams.hEvtChild, FALSE /*Alertable*/, &Timeout);
    }
}


/**
 * Routine called by the supR3HardenedEarlyProcessInitThunk assembly routine
 * when LdrInitializeThunk is executed during process initialization.
 *
 * This initializes the Stub and VM processes, hooking NTDLL APIs and opening
 * the device driver before any other DLLs gets loaded into the process.  This
 * greately reduces and controls the trusted code base of the process compared
 * to opening the driver from SUPR3HardenedMain.  It also avoids issues with so
 * call protection software that is in the habit of patching half of the ntdll
 * and kernel32 APIs in the process, making it almost indistinguishable from
 * software that is up to no good.  Once we've opened vboxdrv (renamed to
 * vboxsup in 7.0 and 6.1.34), the process should be locked down so tightly
 * that only kernel software and csrss can mess with the process.
 */
DECLASM(uintptr_t) supR3HardenedEarlyProcessInit(void)
{
    /*
     * When the first thread gets here we wait for the parent to continue with
     * the process purifications.  The primary thread must execute for image
     * load notifications to trigger, at least in more recent windows versions.
     * The old trick of starting a different thread that terminates immediately
     * thus doesn't work.
     *
     * We are not allowed to modify any data at this point because it will be
     * reset by the child process purification the parent does when we stop. To
     * sabotage thread creation during purification, and to avoid unnecessary
     * work for the parent, we reset g_ProcParams before signalling the parent
     * here.
     */
    if (g_enmSupR3HardenedMainState != SUPR3HARDENEDMAINSTATE_NOT_YET_CALLED)
    {
        NtTerminateThread(0, 0);
        return 0x22; /* crash */
    }

    /* Retrieve the data we need. */
    uintptr_t uNtDllAddr = ASMAtomicXchgPtrT(&g_ProcParams.uNtDllAddr, 0, uintptr_t);
    if (!RT_VALID_PTR(uNtDllAddr))
    {
        NtTerminateThread(0, 0);
        return 0x23; /* crash */
    }

    HANDLE hEvtChild  = g_ProcParams.hEvtChild;
    HANDLE hEvtParent = g_ProcParams.hEvtParent;
    if (   hEvtChild  == NULL
        || hEvtChild  == RTNT_INVALID_HANDLE_VALUE
        || hEvtParent == NULL
        || hEvtParent == RTNT_INVALID_HANDLE_VALUE)
    {
        NtTerminateThread(0, 0);
        return 0x24; /* crash */
    }

    /* Resolve the APIs we need. */
    PFNNTWAITFORSINGLEOBJECT    pfnNtWaitForSingleObject;
    PFNNTSETEVENT               pfnNtSetEvent;
    supR3HardenedWinGetVeryEarlyImports(uNtDllAddr, &pfnNtWaitForSingleObject, &pfnNtSetEvent);

    /* Signal the parent that we're ready for purification. */
    RT_ZERO(g_ProcParams);
    g_ProcParams.enmRequest = kSupR3WinChildReq_PurifyChildAndCloseHandles;
    NTSTATUS rcNt = pfnNtSetEvent(hEvtParent, NULL);
    if (rcNt != STATUS_SUCCESS)
        return 0x33; /* crash */

    /* Wait up to 2 mins for the parent to exorcise evil. */
    LARGE_INTEGER Timeout;
    Timeout.QuadPart = -1200000000; /* 120 second */
    rcNt = pfnNtWaitForSingleObject(hEvtChild, FALSE /*Alertable (never alertable before hooking!) */, &Timeout);
    if (rcNt != STATUS_SUCCESS)
        return 0x34; /* crash */

    /*
     * We're good to go, work global state and restore process parameters.
     * Note that we will not restore uNtDllAddr since that is our first defence
     * against unwanted threads (see above).
     */
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_EARLY_INIT_CALLED;
    g_fSupEarlyProcessInit      = true;

    g_ProcParams.hEvtChild      = hEvtChild;
    g_ProcParams.hEvtParent     = hEvtParent;
    g_ProcParams.enmRequest     = kSupR3WinChildReq_Error;
    g_ProcParams.rc             = VINF_SUCCESS;

    /*
     * Initialize the NTDLL imports that we consider usable before the
     * process has been initialized.
     */
    supR3HardenedWinInitImportsEarly(uNtDllAddr);
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_EARLY_IMPORTS_RESOLVED;

    /*
     * Init g_uNtVerCombined as well as we can at this point.
     */
    supR3HardenedWinInitVersion(true /*fEarly*/);

    /*
     * Convert the arguments to UTF-8 so we can open the log file if specified.
     * We may have to normalize the pointer on older windows version (not w7/64 +).
     * Note! This leaks memory at present.
     */
    PRTL_USER_PROCESS_PARAMETERS pUserProcParams = NtCurrentPeb()->ProcessParameters;
    UNICODE_STRING CmdLineStr = pUserProcParams->CommandLine;
    if (   CmdLineStr.Buffer != NULL
        && !(pUserProcParams->Flags & RTL_USER_PROCESS_PARAMS_FLAG_NORMALIZED) )
        CmdLineStr.Buffer = (WCHAR *)((uintptr_t)CmdLineStr.Buffer + (uintptr_t)pUserProcParams);
    int    cArgs;
    char **papszArgs = suplibCommandLineToArgvWStub(CmdLineStr.Buffer, CmdLineStr.Length / sizeof(WCHAR), &cArgs);
    supR3HardenedOpenLog(&cArgs, papszArgs);
    SUP_DPRINTF(("supR3HardenedVmProcessInit: uNtDllAddr=%p g_uNtVerCombined=%#x (stack ~%p)\n",
                 uNtDllAddr, g_uNtVerCombined, &Timeout));

    /*
     * Set up the direct system calls so we can more easily hook NtCreateSection.
     */
    RTERRINFOSTATIC ErrInfo;
    supR3HardenedWinInitSyscalls(true /*fReportErrors*/, RTErrInfoInitStatic(&ErrInfo));

    /*
     * Determine the executable path and name.  Will NOT determine the windows style
     * executable path here as we don't need it.
     */
    SIZE_T cbActual = 0;
    rcNt = NtQueryVirtualMemory(NtCurrentProcess(), &g_ProcParams, MemorySectionName, &g_SupLibHardenedExeNtPath,
                                sizeof(g_SupLibHardenedExeNtPath) - sizeof(WCHAR), &cbActual);
    if (   !NT_SUCCESS(rcNt)
        || g_SupLibHardenedExeNtPath.UniStr.Length == 0
        || g_SupLibHardenedExeNtPath.UniStr.Length & 1)
        supR3HardenedFatal("NtQueryVirtualMemory/MemorySectionName failed in supR3HardenedVmProcessInit: %#x\n", rcNt);

    /* The NT executable name offset / dir path length. */
    g_offSupLibHardenedExeNtName = g_SupLibHardenedExeNtPath.UniStr.Length / sizeof(WCHAR);
    while (   g_offSupLibHardenedExeNtName > 1
           && g_SupLibHardenedExeNtPath.UniStr.Buffer[g_offSupLibHardenedExeNtName - 1] != '\\' )
        g_offSupLibHardenedExeNtName--;

    /*
     * Preliminary app binary path init.  May change when SUPR3HardenedMain is called.
     */
    supR3HardenedWinInitAppBin(SUPSECMAIN_FLAGS_LOC_APP_BIN);

    /*
     * Initialize the image verification stuff (hooks LdrLoadDll and NtCreateSection).
     */
    supR3HardenedWinInit(0, false /*fAvastKludge*/);

    /*
     * Open the driver.
     */
    if (cArgs >= 1 && suplibHardenedStrCmp(papszArgs[0], SUPR3_RESPAWN_1_ARG0) == 0)
    {
        SUP_DPRINTF(("supR3HardenedVmProcessInit: Opening vboxsup stub...\n"));
        supR3HardenedWinOpenStubDevice();
        g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_EARLY_STUB_DEVICE_OPENED;
    }
    else if (cArgs >= 1 && suplibHardenedStrCmp(papszArgs[0], SUPR3_RESPAWN_2_ARG0) == 0)
    {
        SUP_DPRINTF(("supR3HardenedVmProcessInit: Opening vboxsup...\n"));
        supR3HardenedMainOpenDevice();
        g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_EARLY_REAL_DEVICE_OPENED;
    }
    else
        supR3HardenedFatal("Unexpected first argument '%s'!\n", papszArgs[0]);

    /*
     * Reinstall the NtDll patches since there is a slight possibility that
     * someone undid them while we where busy opening the device.
     */
    supR3HardenedWinReInstallHooks(false /*fFirstCall */);

    /*
     * Restore the LdrInitializeThunk code so we can initialize the process
     * normally when we return.
     */
    SUP_DPRINTF(("supR3HardenedVmProcessInit: Restoring LdrInitializeThunk...\n"));
    PSUPHNTLDRCACHEENTRY pLdrEntry;
    int rc = supHardNtLdrCacheOpen("ntdll.dll", &pLdrEntry, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        supR3HardenedFatal("supR3HardenedVmProcessInit: supHardNtLdrCacheOpen failed on NTDLL: %Rrc %s\n",
                           rc, ErrInfo.Core.pszMsg);

    uint8_t *pbBits;
    rc = supHardNtLdrCacheEntryGetBits(pLdrEntry, &pbBits, uNtDllAddr, NULL, NULL, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        supR3HardenedFatal("supR3HardenedVmProcessInit: supHardNtLdrCacheEntryGetBits failed on NTDLL: %Rrc %s\n",
                           rc, ErrInfo.Core.pszMsg);

    RTLDRADDR uValue;
    rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbBits, uNtDllAddr, UINT32_MAX, "LdrInitializeThunk", &uValue);
    if (RT_FAILURE(rc))
        supR3HardenedFatal("supR3HardenedVmProcessInit: Failed to find LdrInitializeThunk (%Rrc).\n", rc);

    PVOID pvLdrInitThunk = (PVOID)(uintptr_t)uValue;
    SUPR3HARDENED_ASSERT_NT_SUCCESS(supR3HardenedWinProtectMemory(pvLdrInitThunk, 16, PAGE_EXECUTE_READWRITE));
    memcpy(pvLdrInitThunk, pbBits + ((uintptr_t)uValue - uNtDllAddr), 16);
    SUPR3HARDENED_ASSERT_NT_SUCCESS(supR3HardenedWinProtectMemory(pvLdrInitThunk, 16, PAGE_EXECUTE_READ));

    SUP_DPRINTF(("supR3HardenedVmProcessInit: Returning to LdrInitializeThunk...\n"));
    return (uintptr_t)pvLdrInitThunk;
}

