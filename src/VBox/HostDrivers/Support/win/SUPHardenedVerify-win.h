/* $Id: SUPHardenedVerify-win.h 107180 2024-11-26 14:50:02Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library/Driver - Hardened Verification, Windows.
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

#ifndef VBOX_INCLUDED_SRC_Support_win_SUPHardenedVerify_win_h
#define VBOX_INCLUDED_SRC_Support_win_SUPHardenedVerify_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/crypto/x509.h>
#ifndef SUP_CERTIFICATES_ONLY
# ifdef RT_OS_WINDOWS
#  include <iprt/ldr.h>
# endif
#endif


RT_C_DECLS_BEGIN

#ifndef SUP_CERTIFICATES_ONLY
# ifdef RT_OS_WINDOWS
DECLHIDDEN(int)     supHardenedWinInitImageVerifier(PRTERRINFO pErrInfo);
DECLHIDDEN(void)    supHardenedWinTermImageVerifier(void);
DECLHIDDEN(void)    supR3HardenedWinVerifyCacheScheduleImports(RTLDRMOD hLdrMod, PCRTUTF16 pwszName);
DECLHIDDEN(void)    supR3HardenedWinVerifyCachePreload(PCRTUTF16 pwszName);


typedef enum SUPHARDNTVPKIND
{
    SUPHARDNTVPKIND_VERIFY_ONLY = 1,
    SUPHARDNTVPKIND_CHILD_PURIFICATION,
    SUPHARDNTVPKIND_SELF_PURIFICATION,
    SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED,
    SUPHARDNTVPKIND_32BIT_HACK = 0x7fffffff
} SUPHARDNTVPKIND;
/** @name SUPHARDNTVP_F_XXX - Flags for supHardenedWinVerifyProcess
 * @{ */
/** Replace unwanted executable memory allocations with a new one that's filled
 * with a safe read-write copy (default is just to free it).
 *
 * This is one way we attempt to work around buggy protection software that
 * either result in host BSOD or VBox application malfunction.  Here the current
 * shit list:
 *  - Trend Micro's data protection software includes a buggy driver called
 *    sakfile.sys that has been observed crashing accessing user memory that we
 *    probably freed.  I'd love to report this to Trend Micro, but unfortunately
 *    they doesn't advertise (or have?) an email address for reporting security
 *    vulnerabilities in the their software.  Having wasted time looking and not
 *    very sorry for having to disclosing the bug here.
 *  - Maybe one more.
 */
#define SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW        RT_BIT_32(0)
/** @} */
DECLHIDDEN(int)     supHardenedWinVerifyProcess(HANDLE hProcess, HANDLE hThread, SUPHARDNTVPKIND enmKind,
                                                uint32_t fFlags, uint32_t *pcFixes, PRTERRINFO pErrInfo);
DECLHIDDEN(int)     supHardNtVpThread(HANDLE hProcess, HANDLE hThread, PRTERRINFO pErrInfo);
DECLHIDDEN(int)     supHardNtVpDebugger(HANDLE hProcess, PRTERRINFO pErrInfo);

DECLHIDDEN(bool)    supHardViUtf16PathIsEqualEx(PCRTUTF16 pawcLeft, size_t cwcLeft, const char *pszRight);
DECLHIDDEN(bool)    supHardViUniStrPathStartsWithUniStr(UNICODE_STRING const *pUniStrLeft,
                                                        UNICODE_STRING const *pUniStrRight, bool fCheckSlash);
DECLHIDDEN(bool)    supHardViUtf16PathStartsWithEx(PCRTUTF16 pwszLeft, uint32_t cwcLeft,
                                                   PCRTUTF16 pwszRight, uint32_t cwcRight, bool fCheckSlash);
DECLHIDDEN(bool)    supHardViIsAppPatchDir(PCRTUTF16 pwszPath, uint32_t cwcName);


/**
 * SUP image verifier loader reader instance.
 */
typedef struct SUPHNTVIRDR
{
    /** The core reader structure. */
    RTLDRREADER Core;
    /** The file handle. */
    HANDLE      hFile;
    /** Handle to event sempahore in case we're force to deal with asynchronous I/O. */
    HANDLE      hEvent;
    /** Current file offset. */
    RTFOFF      off;
    /** The file size. */
    uint64_t    cbFile;
    /** Flags for the verification callback, SUPHNTVI_F_XXX. */
    uint32_t    fFlags;
    /** Number of signatures that verified okay. */
    uint16_t    cOkaySignatures;
    /** Number of signatures that couldn't be successfully verified (time stamp
     * issues, no certificate path, etc) but weren't fatal. */
    uint16_t    cNokSignatures;
    /** Total number of signatures. */
    uint16_t    cTotalSignatures;
    /** The current signature (for passing to supHardNtViCertVerifyCallback). */
    uint16_t    iCurSignature;
    /** The last non-fatal signature failure. */
    int         rcLastSignatureFailure;
    /** Log name. */
    char        szFilename[1];
} SUPHNTVIRDR;
/** Pointer to an SUP image verifier loader reader instance. */
typedef SUPHNTVIRDR *PSUPHNTVIRDR;
DECLHIDDEN(int)  supHardNtViRdrCreate(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PSUPHNTVIRDR *ppNtViRdr);
DECLHIDDEN(bool) supHardenedWinIsWinVerifyTrustCallable(void);
DECLHIDDEN(int)  supHardenedWinVerifyImageTrust(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, int rc,
                                                bool *pfWinVerifyTrust, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  supHardenedWinVerifyImageByHandle(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags,
                                                   bool fAvoidWinVerifyTrust, bool *pfWinVerifyTrust, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  supHardenedWinVerifyImageByHandleNoName(HANDLE hFile, uint32_t fFlags, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  supHardenedWinVerifyImageByLdrMod(RTLDRMOD hLdrMod, PCRTUTF16 pwszName, PSUPHNTVIRDR pNtViRdr,
                                                   bool fAvoidWinVerifyTrust, bool *pfWinVerifyTrust, PRTERRINFO pErrInfo);
/** @name SUPHNTVI_F_XXX - Flags for supHardenedWinVerifyImageByHandle.
 * @{ */
/** The signing certificate must be the same as the one the VirtualBox build
 * was signed with. */
#  define SUPHNTVI_F_REQUIRE_BUILD_CERT             RT_BIT(0)
/** The signing certificate must be one the build certificate or one of the
 * one used by Oracle to sign the extension pack. */
#  define SUPHNTVI_F_REQUIRE_SPECIAL_TRUST_CERT     RT_BIT(1)
/** Require regular code signing. */
#  define SUPHNTVI_F_REQUIRE_CODE_SIGNING           RT_BIT(2)
/** Require kernel code signing level. */
#  define SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING    RT_BIT(3)
/** Require the image to force the memory mapper to do signature checking. */
#  define SUPHNTVI_F_REQUIRE_SIGNATURE_ENFORCEMENT  RT_BIT(4)
/** Whether to allow image verification by catalog file. */
#  define SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION    RT_BIT(5)
/** The file owner must be TrustedInstaller, Builtin\\Administrators
 *  (S-1-5-32-544) or local system (S-1-5-21) on Vista+. */
#  define SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER RT_BIT(6)
/** Ignore the image architecture (otherwise it must match the verification
 * code).  Used with resource images and such. */
#  define SUPHNTVI_F_IGNORE_ARCHITECTURE            RT_BIT(30)
/** Raw-mode context image, always 32-bit. */
#  define SUPHNTVI_F_RC_IMAGE                       RT_BIT(31)
/** @} */

/* Array in SUPHardenedVerifyImage-win.cpp */
extern const RTSTRTUPLE g_aSupNtViBlacklistedDlls[];

/**
 * Loader cache entry.
 *
 * This is for avoiding loading and signature checking a file multiple times,
 * due to multiple passes thru the process validation code (and syscall import
 * code of NTDLL).
 */
typedef struct SUPHNTLDRCACHEENTRY
{
    /** The file name (from g_apszSupNtVpAllowedDlls or
     *  g_apszSupNtVpAllowedVmExes). */
    const char         *pszName;
    /** Load module associated with the image during content verfication. */
    RTLDRMOD            hLdrMod;
    /** The file reader. */
    PSUPHNTVIRDR        pNtViRdr;
    /** The module file handle, if we've opened it.
     * (pNtviRdr does not close the file handle on destruction.)  */
    HANDLE              hFile;
    /** Bits buffer. */
    uint8_t            *pbBits;
    /** Set if verified. */
    bool                fVerified;
    /** Whether we've got valid cacheable image bits. */
    bool                fValidBits;
    /** The image base address. */
    uintptr_t           uImageBase;
} SUPHNTLDRCACHEENTRY;
/** Pointer to a loader cache entry. */
typedef SUPHNTLDRCACHEENTRY *PSUPHNTLDRCACHEENTRY;
DECLHIDDEN(int)  supHardNtLdrCacheOpen(const char *pszName, PSUPHNTLDRCACHEENTRY *ppEntry, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  supHardNtLdrCacheEntryVerify(PSUPHNTLDRCACHEENTRY pEntry, PCRTUTF16 pwszName, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  supHardNtLdrCacheEntryGetBits(PSUPHNTLDRCACHEENTRY pEntry, uint8_t **ppbBits, RTLDRADDR uBaseAddress,
                                               PFNRTLDRIMPORT pfnGetImport, void *pvUser, PRTERRINFO pErrInfo);


/** Which directory under the system root to get. */
typedef enum SUPHARDNTSYSROOTDIR
{
    kSupHardNtSysRootDir_System32 = 0,
    kSupHardNtSysRootDir_WinSxS,
} SUPHARDNTSYSROOTDIR;

DECLHIDDEN(int) supHardNtGetSystemRootDir(void *pvBuf, uint32_t cbBuf, SUPHARDNTSYSROOTDIR enmDir, PRTERRINFO pErrInfo);

#  ifndef SUPHNTVI_NO_NT_STUFF

/** Typical system root directory buffer. */
typedef struct SUPSYSROOTDIRBUF
{
    UNICODE_STRING  UniStr;
    WCHAR           awcBuffer[260];
} SUPSYSROOTDIRBUF;
extern SUPSYSROOTDIRBUF g_System32NtPath;
extern SUPSYSROOTDIRBUF g_WinSxSNtPath;
#if defined(IN_RING3) && !defined(VBOX_PERMIT_EVEN_MORE)
extern SUPSYSROOTDIRBUF g_ProgramFilesNtPath;
extern SUPSYSROOTDIRBUF g_CommonFilesNtPath;
# if ARCH_BITS == 64
extern SUPSYSROOTDIRBUF g_ProgramFilesX86NtPath;
extern SUPSYSROOTDIRBUF g_CommonFilesX86NtPath;
# endif
#endif /* IN_RING3 && !VBOX_PERMIT_EVEN_MORE */
extern SUPSYSROOTDIRBUF g_SupLibHardenedExeNtPath;

#   ifdef IN_RING0
/** Pointer to NtQueryVirtualMemory. */
typedef DECLCALLBACKPTR_EX(NTSTATUS, NTAPI, PFNNTQUERYVIRTUALMEMORY,(HANDLE, void const *, MEMORY_INFORMATION_CLASS,
                                                                     PVOID, SIZE_T, PSIZE_T));
extern PFNNTQUERYVIRTUALMEMORY g_pfnNtQueryVirtualMemory;
#   endif

#  endif /* SUPHNTVI_NO_NT_STUFF */

/** Creates a combined NT version number for simple comparisons. */
#define SUP_MAKE_NT_VER_COMBINED(a_uMajor, a_uMinor, a_uBuild, a_uSpMajor, a_uSpMinor) \
    (   ((uint32_t)((a_uMajor)   & UINT32_C(0xf))    << 28) \
      | ((uint32_t)((a_uMinor)   & UINT32_C(0xf))    << 24) \
      | ((uint32_t)((a_uBuild)   & UINT32_C(0xffff)) << 8) \
      | ((uint32_t)((a_uSpMajor) & UINT32_C(0xf))    << 4) \
      |  (uint32_t)((a_uSpMinor) & UINT32_C(0xf)) )
/** Simple version of SUP_MAKE_NT_VER_COMBINED. */
#define SUP_MAKE_NT_VER_SIMPLE(a_uMajor, a_uMinor) SUP_MAKE_NT_VER_COMBINED(a_uMajor, a_uMinor, 0, 0, 0)
extern uint32_t         g_uNtVerCombined;

/** @name NT version constants for less-than checks.
 * @{ */
/** Combined NT version number for XP. */
#define SUP_NT_VER_XP       SUP_MAKE_NT_VER_SIMPLE(5,1)
/** Combined NT version number for Windows server 2003 & XP64. */
#define SUP_NT_VER_W2K3     SUP_MAKE_NT_VER_SIMPLE(5,2)
/** Combined NT version number for Vista. */
#define SUP_NT_VER_VISTA    SUP_MAKE_NT_VER_SIMPLE(6,0)
/** Combined NT version number for Vista with SP1. */
#define SUP_NT_VER_VISTA_SP1 SUP_MAKE_NT_VER_COMBINED(6,0,6001,1,0)
/** Combined NT version number for Windows 7. */
#define SUP_NT_VER_W70      SUP_MAKE_NT_VER_SIMPLE(6,1)
/** Combined NT version number for Windows 8.0. */
#define SUP_NT_VER_W80      SUP_MAKE_NT_VER_SIMPLE(6,2)
/** Combined NT version number for Windows 8.1. */
#define SUP_NT_VER_W81      SUP_MAKE_NT_VER_SIMPLE(6,3)
/** @} */

# endif

# ifndef IN_SUP_HARDENED_R3
#  include <iprt/mem.h>
#  include <iprt/string.h>

#  define suplibHardenedMemComp      memcmp
#  define suplibHardenedMemCopy      memcpy
#  define suplibHardenedMemSet       memset
#  define suplibHardenedStrCopy      strcpy
#  define suplibHardenedStrLen       strlen
#  define suplibHardenedStrCat       strcat
#  define suplibHardenedStrCmp       strcmp
#  define suplibHardenedStrNCmp      strncmp
# else   /* IN_SUP_HARDENED_R3 */
#  include <iprt/mem.h>
#  if 0
#  define memcmp                     suplibHardenedMemComp
#  define memcpy                     suplibHardenedMemCopy
#  define memset                     suplibHardenedMemSet
#  define strcpy                     suplibHardenedStrCopy
#  define strlen                     suplibHardenedStrLen
#  define strcat                     suplibHardenedStrCat
#  define strcmp                     suplibHardenedStrCmp
#  define strncmp                    suplibHardenedStrNCmp
#  endif
# endif  /* IN_SUP_HARDENED_R3 */

#endif /* SUP_CERTIFICATES_ONLY */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Support_win_SUPHardenedVerify_win_h */

