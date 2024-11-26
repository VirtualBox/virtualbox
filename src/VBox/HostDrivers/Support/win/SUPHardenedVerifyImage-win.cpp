/* $Id: SUPHardenedVerifyImage-win.cpp 107180 2024-11-26 14:50:02Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library/Driver - Hardened Image Verification, Windows.
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
#ifdef IN_RING0
# ifndef IPRT_NT_MAP_TO_ZW
#  define IPRT_NT_MAP_TO_ZW
# endif
# include <iprt/nt/nt.h>
# include <ntimage.h>
#else
# include <iprt/nt/nt-and-windows.h>
# include "Wintrust.h"
# include "Softpub.h"
# include "mscat.h"
# ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#  define LOAD_LIBRARY_SEARCH_SYSTEM32           0x800
# endif
#endif

#include <VBox/sup.h>
#include <VBox/err.h>
#include <iprt/ctype.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/crypto/pkcs7.h>
#include <iprt/crypto/store.h>

#ifdef IN_RING0
# include "SUPDrvInternal.h"
#else
# include "SUPLibInternal.h"
#endif
#include "win/SUPHardenedVerify-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The size of static hash (output) buffers.
 * Avoids dynamic allocations and cleanups for of small buffers as well as extra
 * calls for getting the appropriate buffer size.  The largest digest in regular
 * use by current windows version is SHA-512, we double this and hope it's
 * enough a good while. */
#define SUPHARDNTVI_MAX_CAT_HASH_SIZE   128


#if defined(VBOX_PERMIT_EVEN_MORE) && !defined(VBOX_PERMIT_MORE)
# error "VBOX_PERMIT_EVEN_MORE without VBOX_PERMIT_MORE!"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#ifdef IN_RING3
typedef DECLCALLBACKPTR_EX(LONG, WINAPI, PFNWINVERIFYTRUST,(HWND hwnd, GUID const *pgActionID, PVOID pWVTData));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATADMINACQUIRECONTEXT,(HCATADMIN *phCatAdmin, const GUID *pGuidSubsystem,
                                                                         DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATADMINACQUIRECONTEXT2,(HCATADMIN *phCatAdmin, const GUID *pGuidSubsystem,
                                                                          PCWSTR pwszHashAlgorithm,
                                                                          struct _CERT_STRONG_SIGN_PARA const *pStrongHashPolicy,
                                                                          DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE,(HANDLE hFile, DWORD *pcbHash, BYTE *pbHash,
                                                                                 DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE2,(HCATADMIN hCatAdmin, HANDLE hFile,
                                                                                  DWORD *pcbHash, BYTE *pbHash, DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(HCATINFO, WINAPI, PFNCRYPTCATADMINENUMCATALOGFROMHASH,(HCATADMIN hCatAdmin, BYTE *pbHash, DWORD cbHash,
                                                                                  DWORD dwFlags, HCATINFO *phPrevCatInfo));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATADMINRELEASECATALOGCONTEXT,(HCATADMIN hCatAdmin, HCATINFO hCatInfo,
                                                                                DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATDADMINRELEASECONTEXT,(HCATADMIN hCatAdmin, DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCRYPTCATCATALOGINFOFROMCONTEXT,(HCATINFO hCatInfo, CATALOG_INFO *psCatInfo,
                                                                            DWORD dwFlags));

typedef DECLCALLBACKPTR_EX(HCERTSTORE, WINAPI, PFNCERTOPENSTORE,(PCSTR pszStoreProvider, DWORD dwEncodingType,
                                                                 HCRYPTPROV_LEGACY hCryptProv, DWORD dwFlags, const void *pvParam));
typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNCERTCLOSESTORE,(HCERTSTORE hCertStore, DWORD dwFlags));
typedef DECLCALLBACKPTR_EX(PCCERT_CONTEXT, WINAPI, PFNCERTENUMCERTIFICATESINSTORE,(HCERTSTORE hCertStore,
                                                                                   PCCERT_CONTEXT pPrevCertContext));

typedef DECLCALLBACKPTR_EX(NTSTATUS, WINAPI, PFNBCRYPTOPENALGORTIHMPROVIDER,(BCRYPT_ALG_HANDLE *phAlgo, PCWSTR pwszAlgoId,
                                                                             PCWSTR pwszImpl, DWORD dwFlags));
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The build certificate. */
static RTCRX509CERTIFICATE  g_BuildX509Cert;

/** Store for certificates that we put special trust it, like the build
 * certificate and the ones used by the Oracle extension pack. */
static RTCRSTORE            g_hSpecialTrustStore = NIL_RTCRSTORE;

/** Store for root software publisher certificates. */
static RTCRSTORE            g_hSpcRootStore = NIL_RTCRSTORE;
/** Store for root NT kernel certificates. */
static RTCRSTORE            g_hNtKernelRootStore = NIL_RTCRSTORE;

/** Store containing SPC, NT kernel signing, and timestamp root certificates. */
static RTCRSTORE            g_hSpcAndNtKernelRootStore = NIL_RTCRSTORE;
/** Store for supplemental certificates for use with
 * g_hSpcAndNtKernelRootStore. */
static RTCRSTORE            g_hSpcAndNtKernelSuppStore = NIL_RTCRSTORE;

/** The full \\SystemRoot\\System32 path. */
SUPSYSROOTDIRBUF            g_System32NtPath;
/** The full \\SystemRoot\\WinSxS path. */
SUPSYSROOTDIRBUF            g_WinSxSNtPath;
#if defined(IN_RING3) && !defined(VBOX_PERMIT_EVEN_MORE)
/** The full 'Program Files' path. */
SUPSYSROOTDIRBUF            g_ProgramFilesNtPath;
# ifdef RT_ARCH_AMD64
/** The full 'Program Files (x86)' path. */
SUPSYSROOTDIRBUF            g_ProgramFilesX86NtPath;
# endif
/** The full 'Common Files' path. */
SUPSYSROOTDIRBUF            g_CommonFilesNtPath;
# ifdef RT_ARCH_AMD64
/** The full 'Common Files (x86)' path. */
SUPSYSROOTDIRBUF            g_CommonFilesX86NtPath;
# endif
#endif /* IN_RING3 && !VBOX_PERMIT_MORE*/

/**
 * Blacklisted DLL names.
 */
const RTSTRTUPLE g_aSupNtViBlacklistedDlls[] =
{
    { RT_STR_TUPLE("SCROBJ.dll") },
    { NULL, 0 } /* terminator entry */
};


static union
{
    SID                     Sid;
    uint8_t                 abPadding[SECURITY_MAX_SID_SIZE];
}
/** The TrustedInstaller SID (Vista+). */
                            g_TrustedInstallerSid,
/** Local system ID (S-1-5-21). */
                            g_LocalSystemSid,
/** Builtin Administrators group alias (S-1-5-32-544). */
                            g_AdminsGroupSid;


/** Set after we've retrived other SPC root certificates from the system. */
static bool                 g_fHaveOtherRoots = false;

#if defined(IN_RING3) && !defined(IN_SUP_HARDENED_R3)
/** Combined windows NT version number.  See SUP_MAKE_NT_VER_COMBINED and
 *  SUP_MAKE_NT_VER_SIMPLE. */
uint32_t                    g_uNtVerCombined;
#endif

#ifdef IN_RING3
/** Timestamp hack working around issues with old DLLs that we ship.
 * See supHardenedWinVerifyImageByHandle() for details.  */
static uint64_t             g_uBuildTimestampHack = 0;
#endif

#ifdef IN_RING3
/** Pointer to WinVerifyTrust. */
PFNWINVERIFYTRUST                       g_pfnWinVerifyTrust;
/** Pointer to CryptCATAdminAcquireContext. */
PFNCRYPTCATADMINACQUIRECONTEXT          g_pfnCryptCATAdminAcquireContext;
/** Pointer to CryptCATAdminAcquireContext2 if available. */
PFNCRYPTCATADMINACQUIRECONTEXT2         g_pfnCryptCATAdminAcquireContext2;
/** Pointer to CryptCATAdminCalcHashFromFileHandle. */
PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE  g_pfnCryptCATAdminCalcHashFromFileHandle;
/** Pointer to CryptCATAdminCalcHashFromFileHandle2. */
PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE2 g_pfnCryptCATAdminCalcHashFromFileHandle2;
/** Pointer to CryptCATAdminEnumCatalogFromHash. */
PFNCRYPTCATADMINENUMCATALOGFROMHASH     g_pfnCryptCATAdminEnumCatalogFromHash;
/** Pointer to CryptCATAdminReleaseCatalogContext. */
PFNCRYPTCATADMINRELEASECATALOGCONTEXT   g_pfnCryptCATAdminReleaseCatalogContext;
/** Pointer to CryptCATAdminReleaseContext. */
PFNCRYPTCATDADMINRELEASECONTEXT         g_pfnCryptCATAdminReleaseContext;
/** Pointer to CryptCATCatalogInfoFromContext. */
PFNCRYPTCATCATALOGINFOFROMCONTEXT       g_pfnCryptCATCatalogInfoFromContext;

/** Where we store the TLS entry for detecting WinVerifyTrustRecursion. */
static uint32_t                         g_iTlsWinVerifyTrustRecursion = UINT32_MAX;
/** Fallback WinVerifyTrust recursion protection. */
static uint32_t volatile                g_idActiveThread = UINT32_MAX;

#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
static int supR3HardNtViCallWinVerifyTrust(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PRTERRINFO pErrInfo,
                                           PFNWINVERIFYTRUST pfnWinVerifyTrust, HRESULT *phrcWinVerifyTrust);
static int supR3HardNtViCallWinVerifyTrustCatFile(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PRTERRINFO pErrInfo,
                                                  PFNWINVERIFYTRUST pfnWinVerifyTrust);
#endif




/** @copydoc RTLDRREADER::pfnRead */
static DECLCALLBACK(int) supHardNtViRdrRead(PRTLDRREADER pReader, void *pvBuf, size_t cb, RTFOFF off)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pReader;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);
    NTSTATUS rcNt;

    /* Check for type overflow (paranoia). */
    if ((ULONG)cb != cb)
        return VERR_OUT_OF_RANGE;

#ifdef IN_RING3
    /* Make sure the event semaphore is reset (normally we don't use one). */
    if (pNtViRdr->hEvent)
    {
        rcNt = NtClearEvent(pNtViRdr->hEvent);
        if (!NT_SUCCESS(rcNt))
            return RTErrConvertFromNtStatus(rcNt);
    }
#endif

    /* Perform the read. */
    LARGE_INTEGER offNt;
    offNt.QuadPart = off;

    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    rcNt = NtReadFile(pNtViRdr->hFile,
                      pNtViRdr->hEvent,
                      NULL /*ApcRoutine*/,
                      NULL /*ApcContext*/,
                      &Ios,
                      pvBuf,
                      (ULONG)cb,
                      &offNt,
                      NULL);

#ifdef IN_RING0
    /* In ring-0 the handles shall be synchronized and not alertable. */
    AssertMsg(rcNt == STATUS_SUCCESS || !NT_SUCCESS(rcNt), ("%#x\n", rcNt));
#else
    /* In ring-3 we like our handles synchronized and non-alertable, but we
       sometimes have to take what we can get.  So, deal with pending I/O as
       best we can. */
    if (rcNt == STATUS_PENDING)
        rcNt = NtWaitForSingleObject(pNtViRdr->hEvent ? pNtViRdr->hEvent : pNtViRdr->hFile, FALSE /*Alertable*/, NULL);
#endif
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (NT_SUCCESS(rcNt))
    {
        /* We require the caller to not read beyond the end of the file since
           we don't have any way to communicate that we've read less that
           requested. */
        if (Ios.Information == cb)
        {
            pNtViRdr->off = off + cb; /* (just for show) */
            return VINF_SUCCESS;
        }
#ifdef IN_RING3
        supR3HardenedError(VERR_READ_ERROR, false,
                           "supHardNtViRdrRead: Only got %#zx bytes when requesting %#zx bytes at %#llx in '%s'.\n",
                           Ios.Information, off, cb, pNtViRdr->szFilename);
#endif
    }
    pNtViRdr->off = -1;
    return VERR_READ_ERROR;
}


/** @copydoc RTLDRREADER::pfnTell */
static DECLCALLBACK(RTFOFF) supHardNtViRdrTell(PRTLDRREADER pReader)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pReader;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);
    return pNtViRdr->off;
}


/** @copydoc RTLDRREADER::pfnSize */
static DECLCALLBACK(uint64_t) supHardNtViRdrSize(PRTLDRREADER pReader)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pReader;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);
    return pNtViRdr->cbFile;
}


/** @copydoc RTLDRREADER::pfnLogName */
static DECLCALLBACK(const char *) supHardNtViRdrLogName(PRTLDRREADER pReader)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pReader;
    return pNtViRdr->szFilename;
}


/** @copydoc RTLDRREADER::pfnMap */
static DECLCALLBACK(int) supHardNtViRdrMap(PRTLDRREADER pReader, const void **ppvBits)
{
    RT_NOREF2(pReader, ppvBits);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTLDRREADER::pfnUnmap */
static DECLCALLBACK(int) supHardNtViRdrUnmap(PRTLDRREADER pReader, const void *pvBits)
{
    RT_NOREF2(pReader, pvBits);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTLDRREADER::pfnDestroy */
static DECLCALLBACK(int) supHardNtViRdrDestroy(PRTLDRREADER pReader)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pReader;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);

    pNtViRdr->Core.uMagic = ~RTLDRREADER_MAGIC;
    pNtViRdr->hFile = NULL;
#ifdef IN_RING3
    if (pNtViRdr->hEvent)
    {
        NtClose(pNtViRdr->hEvent);
        pNtViRdr->hEvent = NULL;
    }
#endif
    RTMemFree(pNtViRdr);
    return VINF_SUCCESS;
}


/**
 * Creates a loader reader instance for the given NT file handle.
 *
 * @returns iprt status code.
 * @param   hFile           Native NT file handle.
 * @param   pwszName        Optional file name.
 * @param   fFlags          Flags, SUPHNTVI_F_XXX.
 * @param   ppNtViRdr       Where to store the reader instance on success.
 */
DECLHIDDEN(int) supHardNtViRdrCreate(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PSUPHNTVIRDR *ppNtViRdr)
{
    /*
     * Try determine the size of the file.
     */
    IO_STATUS_BLOCK             Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    FILE_STANDARD_INFORMATION   StdInfo;
    NTSTATUS rcNt = NtQueryInformationFile(hFile, &Ios, &StdInfo, sizeof(StdInfo), FileStandardInformation);
    if (!NT_SUCCESS(rcNt) || !NT_SUCCESS(Ios.Status))
        return VERR_LDRVI_FILE_LENGTH_ERROR;

    /*
     * Figure the file mode so we can see whether we'll be needing an event
     * semaphore for waiting on reads.  This may happen in very unlikely
     * NtCreateSection scenarios.
     */
#if defined(IN_RING3) || defined(VBOX_STRICT)
    Ios.Status = STATUS_UNSUCCESSFUL;
    ULONG fMode;
    rcNt = NtQueryInformationFile(hFile, &Ios, &fMode, sizeof(fMode), FileModeInformation);
    if (!NT_SUCCESS(rcNt) || !NT_SUCCESS(Ios.Status))
        return VERR_SUP_VP_FILE_MODE_ERROR;
#endif

    HANDLE hEvent = NULL;
#ifdef IN_RING3
    if (!(fMode & (FILE_SYNCHRONOUS_IO_NONALERT | FILE_SYNCHRONOUS_IO_ALERT)))
    {
        rcNt = NtCreateEvent(&hEvent, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
        if (!NT_SUCCESS(rcNt))
            return VERR_SUP_VP_CREATE_READ_EVT_SEM_FAILED;
    }
#else
    Assert(fMode & FILE_SYNCHRONOUS_IO_NONALERT);
#endif

    /*
     * Calc the file name length and allocate memory for the reader instance.
     */
    size_t cchFilename = 0;
    if (pwszName)
        cchFilename = RTUtf16CalcUtf8Len(pwszName);

    int rc = VERR_NO_MEMORY;
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)RTMemAllocZ(sizeof(*pNtViRdr) + cchFilename);
    if (!pNtViRdr)
    {
#ifdef IN_RING3
        if (hEvent != NULL)
            NtClose(hEvent);
#endif
        return VERR_NO_MEMORY;
    }

    /*
     * Initialize the structure.
     */
    if (cchFilename)
    {
        char *pszName = &pNtViRdr->szFilename[0];
        rc = RTUtf16ToUtf8Ex(pwszName, RTSTR_MAX, &pszName, cchFilename + 1, NULL);
        AssertStmt(RT_SUCCESS(rc), pNtViRdr->szFilename[0] = '\0');
    }
    else
        pNtViRdr->szFilename[0] = '\0';

    pNtViRdr->Core.uMagic     = RTLDRREADER_MAGIC;
    pNtViRdr->Core.pfnRead    = supHardNtViRdrRead;
    pNtViRdr->Core.pfnTell    = supHardNtViRdrTell;
    pNtViRdr->Core.pfnSize    = supHardNtViRdrSize;
    pNtViRdr->Core.pfnLogName = supHardNtViRdrLogName;
    pNtViRdr->Core.pfnMap     = supHardNtViRdrMap;
    pNtViRdr->Core.pfnUnmap   = supHardNtViRdrUnmap;
    pNtViRdr->Core.pfnDestroy = supHardNtViRdrDestroy;
    pNtViRdr->hFile           = hFile;
    pNtViRdr->hEvent          = hEvent;
    pNtViRdr->off             = 0;
    pNtViRdr->cbFile          = (uint64_t)StdInfo.EndOfFile.QuadPart;
    pNtViRdr->fFlags          = fFlags;
    *ppNtViRdr = pNtViRdr;
    return VINF_SUCCESS;
}


/**
 * Checks if the file is owned by TrustedInstaller (Vista+) or similar.
 *
 * @returns true if owned by TrustedInstaller of pre-Vista, false if not.
 *
 * @param   hFile               The handle to the file.
 * @param   pwszName            The name of the file.
 */
static bool supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(HANDLE hFile, PCRTUTF16 pwszName)
{
    if (g_uNtVerCombined < SUP_NT_VER_VISTA)
        return true;

    /*
     * Get the ownership information.
     */
    union
    {
        SECURITY_DESCRIPTOR_RELATIVE    Rel;
        SECURITY_DESCRIPTOR             Abs;
        uint8_t                         abView[256];
    } uBuf;
    ULONG cbActual;
    NTSTATUS rcNt = NtQuerySecurityObject(hFile, OWNER_SECURITY_INFORMATION, &uBuf.Abs, sizeof(uBuf), &cbActual);
    if (!NT_SUCCESS(rcNt))
    {
        SUP_DPRINTF(("NtQuerySecurityObject failed with rcNt=%#x on '%ls'\n", rcNt, pwszName));
        return false;
    }

    /*
     * Check the owner.
     *
     * Initially we wished to only allow TrustedInstaller.  But a Windows CAPI
     * plugin "Program Files\Tumbleweed\Desktop Validator\tmwdcapiclient.dll"
     * turned up owned by the local system user, and we cannot operate without
     * the plugin loaded once it's installed (WinVerityTrust fails).
     *
     * We'd like to avoid allowing Builtin\Administrators here since it's the
     * default owner of anything an admin user creates (at least when elevated).
     * Seems windows update or someone ends up installing or modifying system
     * DLL ownership to this group, so for system32 and winsxs it's unavoidable.
     * And, not surprise, a bunch of products, including AV, firewalls and similar
     * ends up with their files installed with this group as owner.  For instance
     * if we wish to have NAT continue working, we need to allow this.
     *
     * Hopefully, we can limit the allowed files to these owners though, so
     * we won't be subject to ordinary (non-admin, or not elevated) users
     * downloading or be tricked into putting evil DLLs around the place...
     */
    PSID pOwner = uBuf.Rel.Control & SE_SELF_RELATIVE ? &uBuf.abView[uBuf.Rel.Owner] : uBuf.Abs.Owner;
    Assert((uintptr_t)pOwner - (uintptr_t)&uBuf < sizeof(uBuf) - sizeof(SID));
    if (RtlEqualSid(pOwner, &g_TrustedInstallerSid))
        return true;
    if (RtlEqualSid(pOwner, &g_LocalSystemSid))
        return true;
    if (RtlEqualSid(pOwner, &g_AdminsGroupSid))
    {
        SUP_DPRINTF(("%ls: Owner is administrators group.\n", pwszName));
        return true;
    }

    SUP_DPRINTF(("%ls: Owner is not trusted installer (%.*Rhxs)\n",
                 pwszName, ((uint8_t *)pOwner)[1] /*SubAuthorityCount*/ * sizeof(ULONG) + 8, pOwner));
    RT_NOREF1(pwszName);
    return false;
}


/**
 * Simple case insensitive UTF-16 / ASCII path compare.
 *
 * @returns true if equal, false if not.
 * @param   pawcLeft            The UTF-16 path string, not necessarily null
 *                              terminated.
 * @param   cwcLeft             The number of chars in the left string,
 *                              RTSTR_MAX if unknown but terminated.
 * @param   pszRight            The ascii string.
 */
DECLHIDDEN(bool) supHardViUtf16PathIsEqualEx(PCRTUTF16 pawcLeft, size_t cwcLeft, const char *pszRight)
{
    for (;;)
    {
        RTUTF16 wc;
        if (cwcLeft-- > 0)
            wc =*pawcLeft++;
        else
            wc = 0;
        uint8_t b  = *pszRight++;
        if (b != wc)
        {
            if (wc >= 0x80)
                return false;
            wc = RT_C_TO_LOWER(wc);
            if (wc != b)
            {
                b = RT_C_TO_LOWER(b);
                if (wc != b)
                {
                    if (wc == '/')
                        wc = '\\';
                    if (b == '/')
                        b = '\\';
                    if (wc != b)
                        return false;
                }
            }
        }
        if (!b)
            return true;
    }
}


/**
 * Simple case insensitive UTF-16 / ASCII path compare.
 *
 * @returns true if equal, false if not.
 * @param   pwszLeft            The UTF-16 path string.
 * @param   pszRight            The ascii string.
 */
static bool supHardViUtf16PathIsEqual(PCRTUTF16 pwszLeft, const char *pszRight)
{
    return supHardViUtf16PathIsEqualEx(pwszLeft, RTSTR_MAX, pszRight);
}


#if 0 /* unused */
/**
 * Simple case insensitive UTF-16 / ASCII ends-with path predicate.
 *
 * @returns true if equal, false if not.
 * @param   pwsz                The UTF-16 path string.
 * @param   pszSuffix           The ascii suffix string.
 */
static bool supHardViUtf16PathEndsWith(PCRTUTF16 pwsz, const char *pszSuffix)
{
    size_t cwc       = RTUtf16Len(pwsz);
    size_t cchSuffix = strlen(pszSuffix);
    if (cwc >= cchSuffix)
        return supHardViUtf16PathIsEqual(pwsz + cwc - cchSuffix, pszSuffix);
    return false;
}
#endif


/**
 * Simple case insensitive UTF-16 / ASCII starts-with path predicate.
 *
 * @returns true if starts with given string, false if not.
 * @param   pwszLeft            The UTF-16 path string.
 * @param   pszRight            The ascii prefix string.
 */
static bool supHardViUtf16PathStartsWithAscii(PCRTUTF16 pwszLeft, const char *pszRight)
{
    for (;;)
    {
        RTUTF16 wc = *pwszLeft++;
        uint8_t b  = *pszRight++;
        if (b != wc)
        {
            if (!b)
                return true;
            if (wc >= 0x80 || wc == 0)
                return false;
            wc = RT_C_TO_LOWER(wc);
            if (wc != b)
            {
                b = RT_C_TO_LOWER(b);
                if (wc != b)
                {
                    if (wc == '/')
                        wc = '\\';
                    if (b == '/')
                        b = '\\';
                    if (wc != b)
                        return false;
                }
            }
        }
    }
}


/**
 * Simple case insensitive UNICODE_STRING starts-with path predicate.
 *
 * @returns true if starts with given string, false if not.
 * @param   pwszLeft            The path to check.
 * @param   cwcLeft             The length of @a pwszLeft
 * @param   pwszRight           The starts-with path.
 * @param   cwcRight            The length of @a pwszRight.
 * @param   fCheckSlash         Check for a slash following the prefix.
 */
DECLHIDDEN(bool) supHardViUtf16PathStartsWithEx(PCRTUTF16 pwszLeft, uint32_t cwcLeft,
                                                PCRTUTF16 pwszRight, uint32_t cwcRight, bool fCheckSlash)
{
    if (cwcLeft < cwcRight || !cwcRight || !pwszRight)
        return false;

    /* See if we can get away with a case sensitive compare first. */
    if (memcmp(pwszLeft, pwszRight, cwcRight * sizeof(RTUTF16)) == 0)
        pwszLeft += cwcRight;
    else
    {
        /* No luck, do a slow case insensitive comapre.  */
        uint32_t cLeft = cwcRight;
        while (cLeft-- > 0)
        {
            RTUTF16 wcLeft  = *pwszLeft++;
            RTUTF16 wcRight = *pwszRight++;
            if (wcLeft != wcRight)
            {
                wcLeft  = wcLeft < 0x80  ? wcLeft  == '/' ? '\\' : RT_C_TO_LOWER(wcLeft)  : wcLeft;
                wcRight = wcRight < 0x80 ? wcRight == '/' ? '\\' : RT_C_TO_LOWER(wcRight) : wcRight;
                if (wcLeft != wcRight)
                    return false;
            }
        }
    }

    /* Check for slash following the prefix, if request. */
    if (   !fCheckSlash
        || *pwszLeft == '\\'
        || *pwszLeft == '/')
        return true;
    return false;
}


/**
 * Simple case insensitive UNICODE_STRING starts-with path predicate.
 *
 * @returns true if starts with given string, false if not.
 * @param   pUniStrLeft         The path to check.
 * @param   pUniStrRight        The starts-with path.
 * @param   fCheckSlash         Check for a slash following the prefix.
 */
DECLHIDDEN(bool) supHardViUniStrPathStartsWithUniStr(UNICODE_STRING const *pUniStrLeft,
                                                     UNICODE_STRING const *pUniStrRight, bool fCheckSlash)
{
    return supHardViUtf16PathStartsWithEx(pUniStrLeft->Buffer, pUniStrLeft->Length / sizeof(WCHAR),
                                          pUniStrRight->Buffer, pUniStrRight->Length / sizeof(WCHAR), fCheckSlash);
}


#ifndef IN_RING0
/**
 * Counts slashes in the given UTF-8 path string.
 *
 * @returns Number of slashes.
 * @param   pwsz                The UTF-16 path string.
 */
static uint32_t supHardViUtf16PathCountSlashes(PCRTUTF16 pwsz)
{
    uint32_t cSlashes = 0;
    RTUTF16 wc;
    while ((wc = *pwsz++) != '\0')
        if (wc == '/' || wc == '\\')
            cSlashes++;
    return cSlashes;
}
#endif


#ifdef VBOX_PERMIT_MORE
/**
 * Checks if the path goes into %windir%\apppatch\.
 *
 * @returns true if apppatch, false if not.
 * @param   pwszPath        The path to examine.
 */
DECLHIDDEN(bool) supHardViIsAppPatchDir(PCRTUTF16 pwszPath, uint32_t cwcName)
{
    uint32_t cwcWinDir = (g_System32NtPath.UniStr.Length - sizeof(L"System32")) / sizeof(WCHAR);

    if (cwcName <= cwcWinDir + sizeof("AppPatch"))
        return false;

    if (memcmp(pwszPath, g_System32NtPath.UniStr.Buffer, cwcWinDir * sizeof(WCHAR)))
        return false;

    if (!supHardViUtf16PathStartsWithAscii(&pwszPath[cwcWinDir], "\\AppPatch\\"))
        return false;

    return g_uNtVerCombined >= SUP_NT_VER_VISTA;
}
#else
# error should not get here..
#endif



/**
 * Checks if the unsigned DLL is fine or not.
 *
 * @returns VINF_LDRVI_NOT_SIGNED or @a rc.
 * @param   hLdrMod             The loader module handle.
 * @param   pwszName            The NT name of the DLL/EXE.
 * @param   fFlags              Flags.
 * @param   hFile               The file handle.
 * @param   rc                  The status code..
 */
static int supHardNtViCheckIfNotSignedOk(RTLDRMOD hLdrMod, PCRTUTF16 pwszName, uint32_t fFlags, HANDLE hFile, int rc)
{
    RT_NOREF1(hLdrMod);

    if (fFlags & (  SUPHNTVI_F_REQUIRE_BUILD_CERT
                  | SUPHNTVI_F_REQUIRE_SPECIAL_TRUST_CERT
                  | SUPHNTVI_F_REQUIRE_CODE_SIGNING
                  | SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING))
        return rc;

    /*
     * Version macros.
     */
    uint32_t const uNtVer = g_uNtVerCombined;
#define IS_XP()    ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(5, 1) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(5, 2) )
#define IS_W2K3()  ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(5, 2) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(5, 3) )
#define IS_VISTA() ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(6, 0) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 1) )
#define IS_W70()   ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(6, 1) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 2) )
#define IS_W80()   ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(6, 2) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 3) )
#define IS_W81()   ( uNtVer >= SUP_MAKE_NT_VER_SIMPLE(6, 3) && uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 4) )

    /*
     * The System32 directory.
     *
     * System32 is full of unsigned DLLs shipped by microsoft, graphics
     * hardware vendors, input device/method vendors and whatnot else that
     * actually needs to be loaded into a process for it to work correctly.
     * We have to ASSUME that anything our process attempts to load from
     * System32 is trustworthy and that the Windows system with the help of
     * anti-virus software make sure there is nothing evil lurking in System32
     * or being loaded from it.
     *
     * A small measure of protection is to list DLLs we know should be signed
     * and decline loading unsigned versions of them, assuming they have been
     * replaced by an adversary with evil intentions.
     */
    PCRTUTF16 pwsz;
    uint32_t cwcName = (uint32_t)RTUtf16Len(pwszName);
    uint32_t cwcOther = g_System32NtPath.UniStr.Length / sizeof(WCHAR);
    if (supHardViUtf16PathStartsWithEx(pwszName, cwcName, g_System32NtPath.UniStr.Buffer, cwcOther, true /*fCheckSlash*/))
    {
        pwsz = pwszName + cwcOther + 1;

        /* Must be owned by trusted installer. (This test is superfuous, thus no relaxation here.) */
        if (   !(fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
            && !supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(hFile, pwszName))
            return rc;

        /* Core DLLs. */
        if (supHardViUtf16PathIsEqual(pwsz, "ntdll.dll"))
            return uNtVer < SUP_NT_VER_VISTA ? VINF_LDRVI_NOT_SIGNED : rc;
        if (supHardViUtf16PathIsEqual(pwsz, "kernel32.dll"))
            return uNtVer < SUP_NT_VER_W81 ? VINF_LDRVI_NOT_SIGNED : rc;
        if (supHardViUtf16PathIsEqual(pwsz, "kernelbase.dll"))
            return IS_W80() || IS_W70() ? VINF_LDRVI_NOT_SIGNED : rc;
        if (supHardViUtf16PathIsEqual(pwsz, "apisetschema.dll"))
            return IS_W70() ? VINF_LDRVI_NOT_SIGNED : rc;
        if (supHardViUtf16PathIsEqual(pwsz, "apphelp.dll"))
            return VINF_LDRVI_NOT_SIGNED; /* So far, never signed... */
#ifdef VBOX_PERMIT_VERIFIER_DLL
        if (supHardViUtf16PathIsEqual(pwsz, "verifier.dll"))
            return uNtVer < SUP_NT_VER_W81 ? VINF_LDRVI_NOT_SIGNED : rc;
#endif
#ifdef VBOX_PERMIT_MORE
        if (uNtVer >= SUP_NT_VER_W70) /* hard limit: user32.dll is unwanted prior to w7. */
        {
            if (supHardViUtf16PathIsEqual(pwsz, "sfc.dll"))
                return uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 4) ? VINF_LDRVI_NOT_SIGNED : rc;
            if (supHardViUtf16PathIsEqual(pwsz, "sfc_os.dll"))
                return uNtVer < SUP_MAKE_NT_VER_SIMPLE(6, 4) ? VINF_LDRVI_NOT_SIGNED : rc;
            if (supHardViUtf16PathIsEqual(pwsz, "user32.dll"))
                return uNtVer < SUP_NT_VER_W81 ? VINF_LDRVI_NOT_SIGNED : rc;
        }
#endif

#ifndef IN_RING0
        /* Check that this DLL isn't supposed to be signed on this windows
           version.  If it should, it's likely to be a fake. */
        /** @todo list of signed dlls for various windows versions.  */
        return VINF_LDRVI_NOT_SIGNED;
#else
        return rc;
#endif /* IN_RING0 */
    }


#ifndef IN_RING0
    /*
     * The WinSxS white list.
     *
     * Just like with System32 there are potentially a number of DLLs that
     * could be required from WinSxS.
     */
    cwcOther = g_WinSxSNtPath.UniStr.Length / sizeof(WCHAR);
    if (supHardViUtf16PathStartsWithEx(pwszName, cwcName, g_WinSxSNtPath.UniStr.Buffer, cwcOther, true /*fCheckSlash*/))
    {
        pwsz = pwszName + cwcOther + 1;
        cwcName -= cwcOther + 1;

        /* The WinSxS layout means everything worth loading is exactly one level down. */
        uint32_t cSlashes = supHardViUtf16PathCountSlashes(pwsz);
        if (cSlashes != 1)
            return rc;

        /* Must be owned by trusted installer. */
        if (   !(fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
            && !supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(hFile, pwszName))
            return rc;
        return VINF_LDRVI_NOT_SIGNED;
    }
#endif /* !IN_RING0 */


#ifdef VBOX_PERMIT_MORE
    /*
     * AppPatch whitelist.
     */
    if (supHardViIsAppPatchDir(pwszName, cwcName))
    {
        cwcOther = g_System32NtPath.UniStr.Length / sizeof(WCHAR); /* ASSUMES System32 is called System32. */
        pwsz = pwszName + cwcOther + 1;

        if (   !(fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
            && !supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(hFile, pwszName))
            return rc;

# ifndef VBOX_PERMIT_EVEN_MORE
        if (supHardViUtf16PathIsEqual(pwsz, "acres.dll"))
            return VINF_LDRVI_NOT_SIGNED;

#  ifdef RT_ARCH_AMD64
        if (supHardViUtf16PathIsEqual(pwsz, "AppPatch64\\AcGenral.dll"))
            return VINF_LDRVI_NOT_SIGNED;
#  elif defined(RT_ARCH_X86)
        if (supHardViUtf16PathIsEqual(pwsz, "AcGenral.dll"))
            return VINF_LDRVI_NOT_SIGNED;
#  endif
# endif /* !VBOX_PERMIT_EVEN_MORE */

# ifdef IN_RING0
        return rc;
# else
        return VINF_LDRVI_NOT_SIGNED;
# endif
    }
#endif /* VBOX_PERMIT_MORE */


#ifndef IN_RING0
# if defined(VBOX_PERMIT_MORE) && !defined(VBOX_PERMIT_EVEN_MORE)
    /*
     * Program files and common files.
     * Permit anything that's signed and correctly installed.
     */
    if (   supHardViUtf16PathStartsWithEx(pwszName, cwcName,
                                          g_ProgramFilesNtPath.UniStr.Buffer, g_ProgramFilesNtPath.UniStr.Length,
                                          true /*fCheckSlash*/)
        || supHardViUtf16PathStartsWithEx(pwszName, cwcName,
                                          g_CommonFilesNtPath.UniStr.Buffer, g_CommonFilesNtPath.UniStr.Length,
                                          true /*fCheckSlash*/)
# ifdef RT_ARCH_AMD64
        || supHardViUtf16PathStartsWithEx(pwszName, cwcName,
                                          g_ProgramFilesX86NtPath.UniStr.Buffer, g_ProgramFilesX86NtPath.UniStr.Length,
                                          true /*fCheckSlash*/)
        || supHardViUtf16PathStartsWithEx(pwszName, cwcName,
                                          g_CommonFilesX86NtPath.UniStr.Buffer, g_CommonFilesX86NtPath.UniStr.Length,
                                          true /*fCheckSlash*/)
# endif
       )
    {
        if (   !(fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
            && !supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(hFile, pwszName))
            return rc;
        return VINF_LDRVI_NOT_SIGNED;
    }

# elif defined(VBOX_PERMIT_MORE) && defined(VBOX_PERMIT_EVEN_MORE)
    /*
     * Anything that's owned by the trusted installer.
     */
    if (   (fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
        || supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(hFile, pwszName))
        return VINF_LDRVI_NOT_SIGNED;

# endif
#endif /* !IN_RING0 */

    /*
     * Not permitted.
     */
    return rc;
}


/**
 * @callback_method_impl{FNRTDUMPPRINTFV, Formats into RTERRINFO. }
 */
static DECLCALLBACK(void) supHardNtViAsn1DumpToErrInfo(void *pvUser, const char *pszFormat, va_list va)
{
    PRTERRINFO pErrInfo = (PRTERRINFO)pvUser;
    RTErrInfoAddV(pErrInfo, pErrInfo->rc, pszFormat, va);
}


/**
 * Attempts to locate a root certificate in the specified store.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if found.
 * @retval  VWRN_NOT_FOUND if not found.
 *
 * @param   hRootStore      The root certificate store to search.
 * @param   pSubject        The root certificate subject.
 * @param   pPublicKeyInfo  The public key of the root certificate to find.
 */
static int supHardNtViCertVerifyFindRootCert(RTCRSTORE hRootStore, PCRTCRX509NAME pSubject,
                                             PCRTCRX509SUBJECTPUBLICKEYINFO pPublicKeyInfo)
{
    RTCRSTORECERTSEARCH Search;
    int rc = RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280(hRootStore, pSubject, &Search);
    AssertRCReturn(rc, rc);

    rc = VWRN_NOT_FOUND;
    PCRTCRCERTCTX pCertCtx;
    while ((pCertCtx = RTCrStoreCertSearchNext(hRootStore, &Search)) != NULL)
    {
        PCRTCRX509SUBJECTPUBLICKEYINFO pCertPubKeyInfo = NULL;
        if (pCertCtx->pCert)
            pCertPubKeyInfo = &pCertCtx->pCert->TbsCertificate.SubjectPublicKeyInfo;
        else if (pCertCtx->pTaInfo)
            pCertPubKeyInfo = &pCertCtx->pTaInfo->PubKey;
        else
            pCertPubKeyInfo = NULL;
        if (   pCertPubKeyInfo
            && RTCrX509SubjectPublicKeyInfo_Compare(pCertPubKeyInfo, pPublicKeyInfo) == 0)
        {
            RTCrCertCtxRelease(pCertCtx);
            rc = VINF_SUCCESS;
            break;
        }
        RTCrCertCtxRelease(pCertCtx);
    }

    int rc2 = RTCrStoreCertSearchDestroy(hRootStore, &Search);
    AssertRC(rc2);
    return rc;
}


/**
 * @callback_method_impl{FNRTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
static DECLCALLBACK(int) supHardNtViCertVerifyCallback(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths,
                                                       uint32_t fFlags, void *pvUser, PRTERRINFO pErrInfo)
{
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pvUser;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);

    /*
     * If there is no certificate path build & validator associated with this
     * callback, it must be because of the build certificate.  We trust the
     * build certificate without any second thoughts.
     */
    if (RTCrX509Certificate_Compare(pCert, &g_BuildX509Cert) == 0)
    {
#ifdef VBOX_STRICT
        Assert(RTCrX509CertPathsGetPathCount(hCertPaths) == 1);
        bool     fTrusted = false;
        uint32_t cNodes = UINT32_MAX;
        int      rcVerify = -1;
        int rc = RTCrX509CertPathsQueryPathInfo(hCertPaths, 0, &fTrusted, &cNodes, NULL, NULL, NULL, NULL, &rcVerify);
        AssertRC(rc); AssertRC(rcVerify); Assert(fTrusted); Assert(cNodes == 1);
#endif
        return VINF_SUCCESS;
    }

    /*
     * Standard code signing capabilites required (SUPHNTVI_F_REQUIRE_CODE_SIGNING is implied here).
     */
    int rc = RTCrPkcs7VerifyCertCallbackCodeSigning(pCert, hCertPaths, fFlags, NULL, pErrInfo);
    if (   RT_SUCCESS(rc)
        && (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA))
    {
        /*
         * For kernel code signing there are two options for a valid certificate path:
         *  1. Anchored by the microsoft kernel signing root certificate (g_hNtKernelRootStore).
         *  2. Anchored by an SPC root and signing entity including a 1.3.6.1.4.1.311.10.3.5 (WHQL)
         *     or 1.3.6.1.4.1.311.10.3.5.1 (WHQL attestation) extended usage key.
         */
        if (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING)
        {
            uint32_t cPaths = RTCrX509CertPathsGetPathCount(hCertPaths);
            uint32_t cFound = 0;
            uint32_t cValid = 0;
            for (uint32_t iPath = 0; iPath < cPaths; iPath++)
            {
                bool                            fTrusted;
                PCRTCRX509NAME                  pSubject;
                PCRTCRX509SUBJECTPUBLICKEYINFO  pPublicKeyInfo;
                int                             rcVerify;
                rc = RTCrX509CertPathsQueryPathInfo(hCertPaths, iPath, &fTrusted, NULL /*pcNodes*/, &pSubject, &pPublicKeyInfo,
                                                    NULL, NULL /*pCertCtx*/, &rcVerify);
                AssertRCBreak(rc);

                if (RT_SUCCESS(rcVerify))
                {
                    Assert(fTrusted);
                    cValid++;

                    /*
                     * 1. Search the kernel signing root store for a matching anchor.
                     */
                    rc = supHardNtViCertVerifyFindRootCert(g_hNtKernelRootStore, pSubject, pPublicKeyInfo);
                    if (rc == VINF_SUCCESS)
                        cFound++;
                    /*
                     * 2. Check for WHQL EKU and make sure it has a SPC root.
                     */
                    else if (   rc == VWRN_NOT_FOUND
                             && (  pCert->TbsCertificate.T3.fExtKeyUsage
                                 & (RTCRX509CERT_EKU_F_MS_ATTEST_WHQL_CRYPTO | RTCRX509CERT_EKU_F_MS_WHQL_CRYPTO)))
                    {
                        rc = supHardNtViCertVerifyFindRootCert(g_hSpcRootStore, pSubject, pPublicKeyInfo);
                        if (rc == VINF_SUCCESS)
                            cFound++;
                    }
                    AssertRCBreak(rc);
                }
            }
            if (RT_SUCCESS(rc) && cFound == 0)
                rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_NOT_VALID_KERNEL_CODE_SIGNATURE,
                                   "Signature #%u/%u: Not valid kernel code signature.",
                                   pNtViRdr->iCurSignature + 1, pNtViRdr->cTotalSignatures);


            if (RT_SUCCESS(rc) && cValid < 2 && g_fHaveOtherRoots)
                rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_UNEXPECTED_VALID_PATH_COUNT,
                                   "Signature #%u/%u: Expected at least %u valid paths, not %u.",
                                   pNtViRdr->iCurSignature + 1, pNtViRdr->cTotalSignatures, 2, cValid);
            if (rc == VWRN_NOT_FOUND)
                rc = VINF_SUCCESS;
        }
    }

    /*
     * More requirements? NT5 build lab?
     */

    return rc;
}


/**
 * RTTimeNow equivaltent that handles ring-3 where we cannot use it.
 *
 * @returns pNow
 * @param   pNow                Where to return the current time.
 */
static PRTTIMESPEC supHardNtTimeNow(PRTTIMESPEC pNow)
{
#ifdef IN_RING3
    /*
     * Just read system time.
     */
    KUSER_SHARED_DATA volatile *pUserSharedData = (KUSER_SHARED_DATA volatile *)MM_SHARED_USER_DATA_VA;
# ifdef RT_ARCH_AMD64
    uint64_t uRet = *(uint64_t volatile *)&pUserSharedData->SystemTime; /* This is what KeQuerySystemTime does (missaligned). */
    return RTTimeSpecSetNtTime(pNow, uRet);
# else

    LARGE_INTEGER NtTime;
    do
    {
        NtTime.HighPart = pUserSharedData->SystemTime.High1Time;
        NtTime.LowPart  = pUserSharedData->SystemTime.LowPart;
    } while (pUserSharedData->SystemTime.High2Time != NtTime.HighPart);
    return RTTimeSpecSetNtTime(pNow, NtTime.QuadPart);
# endif
#else  /* IN_RING0 */
    return RTTimeNow(pNow);
#endif /* IN_RING0 */
}


/**
 * @callback_method_impl{FNRTLDRVALIDATESIGNEDDATA}
 */
static DECLCALLBACK(int) supHardNtViCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo, PRTERRINFO pErrInfo, void *pvUser)
{
    RT_NOREF(hLdrMod);

    /*
     * Check out the input.
     */
    PSUPHNTVIRDR pNtViRdr = (PSUPHNTVIRDR)pvUser;
    Assert(pNtViRdr->Core.uMagic == RTLDRREADER_MAGIC);
    pNtViRdr->cTotalSignatures = pInfo->cSignatures;
    pNtViRdr->iCurSignature    = pInfo->iSignature;

    AssertReturn(pInfo->enmType == RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA, VERR_INTERNAL_ERROR_5);
    AssertReturn(!pInfo->pvExternalData, VERR_INTERNAL_ERROR_5);
    AssertReturn(pInfo->cbSignature == sizeof(RTCRPKCS7CONTENTINFO), VERR_INTERNAL_ERROR_5);
    PCRTCRPKCS7CONTENTINFO pContentInfo = (PCRTCRPKCS7CONTENTINFO)pInfo->pvSignature;
    AssertReturn(RTCrPkcs7ContentInfo_IsSignedData(pContentInfo), VERR_INTERNAL_ERROR_5);
    AssertReturn(pContentInfo->u.pSignedData->SignerInfos.cItems == 1, VERR_INTERNAL_ERROR_5);
    PCRTCRPKCS7SIGNERINFO pSignerInfo = pContentInfo->u.pSignedData->SignerInfos.papItems[0];


    /*
     * If special certificate requirements, check them out before validating
     * the signature.  These only apply to the first signature (for now).
     */
    if (   (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_BUILD_CERT)
        && pInfo->iSignature == 0)
    {
        if (!RTCrX509Certificate_MatchIssuerAndSerialNumber(&g_BuildX509Cert,
                                                            &pSignerInfo->IssuerAndSerialNumber.Name,
                                                            &pSignerInfo->IssuerAndSerialNumber.SerialNumber))
            return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_NOT_SIGNED_WITH_BUILD_CERT,
                                 "Signature #%u/%u: Not signed with the build certificate (serial %.*Rhxs, expected %.*Rhxs)",
                                 pInfo->iSignature + 1, pInfo->cSignatures,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.uData.pv,
                                 g_BuildX509Cert.TbsCertificate.SerialNumber.Asn1Core.cb,
                                 g_BuildX509Cert.TbsCertificate.SerialNumber.Asn1Core.uData.pv);
    }
    /** @todo apply these to all signatures, but don't fail in a bad way for
     *        stuff with extra signatures (typically from microsoft). */
    else if (   (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_SPECIAL_TRUST_CERT)
             && pInfo->iSignature == 0)
    {
        PCRTCRCERTCTX const pCertCtx = RTCrStoreCertByIssuerAndSerialNo(g_hSpecialTrustStore,
                                                                        &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                        &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
        if (!pCertCtx)
            return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_NOT_SIGNED_WITH_SPECIALLY_TRUSTED_CERT,
                                 "Signature #%u/%u: Not signed with the build certificate or any of the specially trusted ones (serial %.*Rhxs)",
                                 pInfo->iSignature + 1, pInfo->cSignatures,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.uData.pv);
        RTCrCertCtxRelease(pCertCtx);
    }

    /*
     * We instruction the verifier to use the signing time counter signature
     * when present, but provides the linker time then the current time as
     * fallbacks should the timestamp be missing or unusable.
     *
     * Update: Save the first timestamp we validate with build cert and
     *         use this as a minimum timestamp for further build cert
     *         validations.  This works around issues with old DLLs that
     *         we sign against with our certificate (crt, sdl, qt).
     *
     * Update: If the validation fails, retry with the current timestamp. This
     *         is a workaround for NTDLL.DLL in build 14971 having a weird
     *         timestamp: 0xDF1E957E (Sat Aug 14 14:05:18 2088).
     */
    uint32_t fFlags = RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                    | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                    | RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY;

    /* In ring-0 we don't have all the necessary timestamp server root certificate
     * info, so we have to allow using counter signatures unverified there.
     * Ditto for the early period of ring-3 hardened stub execution. */
#ifndef IN_RING0
    if (!g_fHaveOtherRoots)
#endif
        fFlags |= RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED | RTCRPKCS7VERIFY_SD_F_USE_MS_TIMESTAMP_UNVERIFIED;

    /* Fallback timestamps to try: */
    struct { RTTIMESPEC TimeSpec; const char *pszDesc; } aTimes[2];
    unsigned cTimes = 0;

    /* 1. The linking timestamp: */
    uint64_t uTimestamp = 0;
    int rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp, sizeof(uTimestamp));
    if (RT_SUCCESS(rc))
    {
#ifdef IN_RING3 /* Hack alert! (see above) */
        if (   (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING)
            && (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_SIGNATURE_ENFORCEMENT)
            && uTimestamp < g_uBuildTimestampHack)
            uTimestamp = g_uBuildTimestampHack;
#endif
        RTTimeSpecSetSeconds(&aTimes[0].TimeSpec, uTimestamp);
        aTimes[0].pszDesc = "link";
        cTimes++;
    }
    else
        SUP_DPRINTF(("RTLdrQueryProp/RTLDRPROP_TIMESTAMP_SECONDS failed on %s: %Rrc", pNtViRdr->szFilename, rc));

    /* 2. Current time. */
    supHardNtTimeNow(&aTimes[cTimes].TimeSpec);
    aTimes[cTimes].pszDesc = "now";
    cTimes++;

    /* Make the verfication attempts. */
    for (unsigned i = 0; ; i++)
    {
        Assert(i < cTimes);
        rc = RTCrPkcs7VerifySignedData(pContentInfo, fFlags, g_hSpcAndNtKernelSuppStore, g_hSpcAndNtKernelRootStore,
                                       &aTimes[i].TimeSpec, supHardNtViCertVerifyCallback, pNtViRdr, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            if (rc != VINF_SUCCESS)
            {
                SUP_DPRINTF(("%s: Signature #%u/%u: info status: %d\n", pNtViRdr->szFilename, pInfo->iSignature + 1, pInfo->cSignatures, rc));
                if (pNtViRdr->rcLastSignatureFailure == VINF_SUCCESS)
                    pNtViRdr->rcLastSignatureFailure = rc;
            }
            pNtViRdr->cOkaySignatures++;

#ifdef IN_RING3 /* Hack alert! (see above) */
            if (   (pNtViRdr->fFlags & (SUPHNTVI_F_REQUIRE_BUILD_CERT | SUPHNTVI_F_REQUIRE_SPECIAL_TRUST_CERT))
                && g_uBuildTimestampHack == 0
                && cTimes > 1)
                g_uBuildTimestampHack = uTimestamp;
#endif
            return VINF_SUCCESS;
        }

        if (rc == VERR_CR_X509_CPV_NOT_VALID_AT_TIME && i + 1 < cTimes)
            SUP_DPRINTF(("%s: Signature #%u/%u: VERR_CR_X509_CPV_NOT_VALID_AT_TIME for %#RX64; retrying against current time: %#RX64.\n",
                         pNtViRdr->szFilename, pInfo->iSignature + 1, pInfo->cSignatures,
                         RTTimeSpecGetSeconds(&aTimes[0].TimeSpec), RTTimeSpecGetSeconds(&aTimes[1].TimeSpec)));
        else
        {
            /* There are a couple of failures we can tollerate if there are more than
               one signature and one of them works out fine.  The RTLdrVerifySignature
               caller will have to check the failure counts though to make sure
               something succeeded.

               VERR_CR_PKCS7_KEY_USAGE_MISMATCH: Nvidia 391.35 nvldumpx.dll has an misconfigured
               certificate "CN=NVIDIA Corporation PE Sign v2016" without valid Key Usage.  It is
               rooted by "CN=NVIDIA Subordinate CA 2016 v2,DC=nvidia,DC=com", so homebrewn.
               Sysinternals' sigcheck util ignores it, while MS sigtool doesn't trust the root.
               It's possible we're being too strict, but well, it's the only case so far, so no
               need to relax the Key Usage restrictions just for a certificate w/o a trusted root.

               VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION: Intel 27.20.100.9126 igdumdim64.dll
               has three signatures, the first is signed with a certificate (C=US,ST=CA,
               L=Santa Clara,O=Intel Corporation,CN=IntelGraphicsPE2021) that has a critical
               subject key identifier.  This used to trip up the path validator.  However, the
               other two signatures are from microsoft and checks out fine.  So, in future
               situations like this it would be nice to simply continue with the next signature.
               See bugref{10130} for details.

               VERR_SUP_VP_NOT_VALID_KERNEL_CODE_SIGNATURE: Is related to the above intel problem,
               but this is what we get if suppressing the unknown critical subjectKeyIdentifier
               in IPRT.  We don't need all signatures to be valid kernel signatures, we should be
               happy with just one and ignore any additional signatures as long as they don't look
               like they've been compromised. Thus continue with this status too. */
            pNtViRdr->rcLastSignatureFailure = rc;
            if (   rc == VERR_CR_X509_CPV_NOT_VALID_AT_TIME
                || rc == VERR_CR_X509_CPV_NO_TRUSTED_PATHS
                || rc == VERR_CR_PKCS7_KEY_USAGE_MISMATCH
                || rc == VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION
                || rc == VERR_SUP_VP_NOT_VALID_KERNEL_CODE_SIGNATURE)
            {
                SUP_DPRINTF(("%s: Signature #%u/%u: %s (%d) w/ timestamp=%#RX64/%s.\n", pNtViRdr->szFilename, pInfo->iSignature + 1, pInfo->cSignatures,
                             rc == VERR_CR_X509_CPV_NOT_VALID_AT_TIME            ? "VERR_CR_X509_CPV_NOT_VALID_AT_TIME"
                             : rc == VERR_CR_X509_CPV_NO_TRUSTED_PATHS           ? "VERR_CR_X509_CPV_NO_TRUSTED_PATHS"
                             : rc == VERR_CR_PKCS7_KEY_USAGE_MISMATCH            ? "VERR_CR_PKCS7_KEY_USAGE_MISMATCH"
                             : rc == VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION ? "VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION"
                                                                                 : "VERR_SUP_VP_NOT_VALID_KERNEL_CODE_SIGNATURE",
                             rc, RTTimeSpecGetSeconds(&aTimes[i].TimeSpec), aTimes[i].pszDesc));

                /* This leniency is not applicable to build certificate requirements (signature #1 only). */
                if (  !(pNtViRdr->fFlags & (SUPHNTVI_F_REQUIRE_BUILD_CERT | SUPHNTVI_F_REQUIRE_SPECIAL_TRUST_CERT))
                    || pInfo->iSignature != 0)
                {
                    pNtViRdr->cNokSignatures++;
                    rc = VINF_SUCCESS;
                }
            }
            else
                SUP_DPRINTF(("%s: Signature #%u/%u: %Rrc w/ timestamp=%#RX64/%s.\n", pNtViRdr->szFilename, pInfo->iSignature + 1, pInfo->cSignatures,
                             rc, RTTimeSpecGetSeconds(&aTimes[i].TimeSpec), aTimes[i].pszDesc));
            return rc;
        }
    }
}


/**
 * Verifies the given loader image.
 *
 * @returns IPRT status code.
 * @param   hLdrMod             File handle to the executable file.
 * @param   pwszName            Full NT path to the DLL in question, used for
 *                              dealing with unsigned system dlls as well as for
 *                              error/logging.
 * @param   pNtViRdr            The reader instance /w flags.
 * @param   fAvoidWinVerifyTrust Whether to avoid WinVerifyTrust because of
 *                              deadlock or other loader related dangers.
 * @param   pfWinVerifyTrust    Where to return whether WinVerifyTrust was used.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardenedWinVerifyImageByLdrMod(RTLDRMOD hLdrMod, PCRTUTF16 pwszName, PSUPHNTVIRDR pNtViRdr,
                                                  bool fAvoidWinVerifyTrust, bool *pfWinVerifyTrust, PRTERRINFO pErrInfo)
{
    if (pfWinVerifyTrust)
        *pfWinVerifyTrust = false;

#ifdef IN_RING3
    /* Check that the caller has performed the necessary library initialization. */
    if (!RTCrX509Certificate_IsPresent(&g_BuildX509Cert))
        return RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER,
                            "supHardenedWinVerifyImageByHandle: supHardenedWinInitImageVerifier was not called.");
#endif

    /*
     * Check the trusted installer bit first, if requested as it's somewhat
     * cheaper than the rest.
     *
     * We relax this for system32 and a little for WinSxS, like we used to, as
     * there are apparently  some systems out there where the user, admin, or
     * someone has changed the ownership of core windows DLLs like user32.dll
     * and comctl32.dll.  Since we need user32.dll  and will be checking it's
     * digital signature, it's reasonably safe to let this thru. (The report
     * was of SECURITY_BUILTIN_DOMAIN_RID + DOMAIN_ALIAS_RID_ADMINS
     * owning user32.dll, see public ticket 13187, VBoxStartup.3.log.)
     *
     * We've also had problems with graphics driver components like ig75icd64.dll
     * and atig6pxx.dll not being owned by TrustedInstaller, with the result
     * that 3D got broken (mod by zero issue in test build 5).  These were also
     * SECURITY_BUILTIN_DOMAIN_RID + DOMAIN_ALIAS_RID_ADMINS.
     *
     * In one report by 'thor' the WinSxS resident comctl32.dll was owned by
     * SECURITY_BUILTIN_DOMAIN_RID + DOMAIN_ALIAS_RID_ADMINS (with 4.3.16).
     */
    /** @todo Since we're now allowing Builtin\\Administrators after all, perhaps we
     *        could drop these system32 + winsxs hacks?? */
    if (   (pNtViRdr->fFlags & SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER)
        && !supHardNtViCheckIsOwnedByTrustedInstallerOrSimilar(pNtViRdr->hFile, pwszName))
    {
        if (supHardViUtf16PathStartsWithEx(pwszName, (uint32_t)RTUtf16Len(pwszName),
                                           g_System32NtPath.UniStr.Buffer, g_System32NtPath.UniStr.Length / sizeof(WCHAR),
                                           true /*fCheckSlash*/))
            SUP_DPRINTF(("%ls: Relaxing the TrustedInstaller requirement for this DLL (it's in system32).\n", pwszName));
        else if (supHardViUtf16PathStartsWithEx(pwszName, (uint32_t)RTUtf16Len(pwszName),
                                                g_WinSxSNtPath.UniStr.Buffer, g_WinSxSNtPath.UniStr.Length / sizeof(WCHAR),
                                                true /*fCheckSlash*/))
            SUP_DPRINTF(("%ls: Relaxing the TrustedInstaller requirement for this DLL (it's in WinSxS).\n", pwszName));
        else
            return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_NOT_OWNED_BY_TRUSTED_INSTALLER,
                                 "supHardenedWinVerifyImageByHandle: TrustedInstaller is not the owner of '%ls'.", pwszName);
    }

    /*
     * Verify it.
     *
     * The PKCS #7 SignedData signature is checked in the callback. Any
     * signing certificate restrictions are also enforced there.
     */
    pNtViRdr->cOkaySignatures        = 0;
    pNtViRdr->cNokSignatures         = 0;
    pNtViRdr->cTotalSignatures       = 0;
    pNtViRdr->rcLastSignatureFailure = VINF_SUCCESS;
    int rc = RTLdrVerifySignature(hLdrMod, supHardNtViCallback, pNtViRdr, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        Assert(pNtViRdr->cOkaySignatures + pNtViRdr->cNokSignatures == pNtViRdr->cTotalSignatures);
        if (   !pNtViRdr->cOkaySignatures
            || pNtViRdr->cOkaySignatures + pNtViRdr->cNokSignatures < pNtViRdr->cTotalSignatures /* paranoia */)
        {
            rc = pNtViRdr->rcLastSignatureFailure;
            AssertStmt(RT_FAILURE_NP(rc), rc = VERR_INTERNAL_ERROR_3);
        }
        else if (rc == VINF_SUCCESS && RT_SUCCESS(pNtViRdr->rcLastSignatureFailure))
            rc = pNtViRdr->rcLastSignatureFailure;
    }

    /*
     * Microsoft doesn't sign a whole bunch of DLLs, so we have to
     * ASSUME that a bunch of system DLLs are fine.
     */
    if (rc == VERR_LDRVI_NOT_SIGNED)
        rc = supHardNtViCheckIfNotSignedOk(hLdrMod, pwszName, pNtViRdr->fFlags, pNtViRdr->hFile, rc);
    if (RT_FAILURE(rc))
        RTErrInfoAddF(pErrInfo, rc, ": %ls", pwszName);

    /*
     * Check for the signature checking enforcement, if requested to do so.
     */
    if (RT_SUCCESS(rc) && (pNtViRdr->fFlags & SUPHNTVI_F_REQUIRE_SIGNATURE_ENFORCEMENT))
    {
        bool fEnforced = false;
        int rc2 = RTLdrQueryProp(hLdrMod, RTLDRPROP_SIGNATURE_CHECKS_ENFORCED, &fEnforced, sizeof(fEnforced));
        if (RT_FAILURE(rc2))
            rc = RTErrInfoSetF(pErrInfo, rc2, "Querying RTLDRPROP_SIGNATURE_CHECKS_ENFORCED failed on %ls: %Rrc.",
                               pwszName, rc2);
        else if (!fEnforced)
            rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_SIGNATURE_CHECKS_NOT_ENFORCED,
                               "The image '%ls' was not linked with /IntegrityCheck.", pwszName);
    }

#ifdef IN_RING3
    /*
     * Pass it thru WinVerifyTrust when possible.
     */
    if (!fAvoidWinVerifyTrust)
        rc = supHardenedWinVerifyImageTrust(pNtViRdr->hFile, pwszName, pNtViRdr->fFlags, rc, pfWinVerifyTrust, pErrInfo);
#else
    RT_NOREF1(fAvoidWinVerifyTrust);
#endif

    /*
     * Check for blacklisted DLLs, both internal name and filename.
     */
    if (RT_SUCCESS(rc))
    {
        size_t const cwcName = RTUtf16Len(pwszName);
        char         szIntName[64];
        int rc2 = RTLdrQueryProp(hLdrMod, RTLDRPROP_INTERNAL_NAME, szIntName, sizeof(szIntName));
        if (RT_SUCCESS(rc2))
        {
            size_t const cchIntName = strlen(szIntName);
            for (unsigned i = 0; g_aSupNtViBlacklistedDlls[i].psz != NULL; i++)
                if (   cchIntName == g_aSupNtViBlacklistedDlls[i].cch
                    && RTStrICmpAscii(szIntName, g_aSupNtViBlacklistedDlls[i].psz) == 0)
                {
                    rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_UNDESIRABLE_MODULE,
                                       "The image '%ls' is listed as undesirable.", pwszName);
                    break;
                }
        }
        if (RT_SUCCESS(rc))
        {
            for (unsigned i = 0; g_aSupNtViBlacklistedDlls[i].psz != NULL; i++)
                if (cwcName >= g_aSupNtViBlacklistedDlls[i].cch)
                {
                    PCRTUTF16 pwszTmp = &pwszName[cwcName - g_aSupNtViBlacklistedDlls[i].cch];
                    if (   (   cwcName == g_aSupNtViBlacklistedDlls[i].cch
                            || pwszTmp[-1] == '\\'
                            || pwszTmp[-1] == '/')
                        && RTUtf16ICmpAscii(pwszTmp, g_aSupNtViBlacklistedDlls[i].psz) == 0)
                    {
                        rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_UNDESIRABLE_MODULE,
                                           "The image '%ls' is listed as undesirable.", pwszName);
                        break;
                    }
                }
        }
    }

#ifdef IN_SUP_HARDENED_R3
    /*
     * Hook for the LdrLoadDll code to schedule scanning of imports.
     */
    if (RT_SUCCESS(rc))
        supR3HardenedWinVerifyCacheScheduleImports(hLdrMod, pwszName);
#endif

    return rc;
}


/**
 * Verifies the given executable image.
 *
 * @returns IPRT status code.
 * @param   hFile               File handle to the executable file.
 * @param   pwszName            Full NT path to the DLL in question, used for
 *                              dealing with unsigned system dlls as well as for
 *                              error/logging.
 * @param   fFlags              Flags, SUPHNTVI_F_XXX.
 * @param   fAvoidWinVerifyTrust Whether to avoid WinVerifyTrust because of
 *                              deadlock or other loader related dangers.
 * @param   pfWinVerifyTrust    Where to return whether WinVerifyTrust was used.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardenedWinVerifyImageByHandle(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags,
                                                  bool fAvoidWinVerifyTrust, bool *pfWinVerifyTrust, PRTERRINFO pErrInfo)
{
    /*
     * Create a reader instance.
     */
    PSUPHNTVIRDR pNtViRdr;
    int rc = supHardNtViRdrCreate(hFile, pwszName, fFlags, &pNtViRdr);
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the image.
         */
        RTLDRMOD  hLdrMod;
        RTLDRARCH enmArch   = fFlags & SUPHNTVI_F_RC_IMAGE ? RTLDRARCH_X86_32 : RTLDRARCH_HOST;
        uint32_t  fLdrFlags = RTLDR_O_FOR_VALIDATION | RTLDR_O_IGNORE_ARCH_IF_NO_CODE;
        if (fFlags & SUPHNTVI_F_IGNORE_ARCHITECTURE)
            fLdrFlags |= RTLDR_O_IGNORE_ARCH_IF_NO_CODE;
        rc = RTLdrOpenWithReader(&pNtViRdr->Core, fLdrFlags, enmArch, &hLdrMod, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /*
             * Verify it.
             */
            rc = supHardenedWinVerifyImageByLdrMod(hLdrMod, pwszName, pNtViRdr, fAvoidWinVerifyTrust, pfWinVerifyTrust, pErrInfo);
            int rc2 = RTLdrClose(hLdrMod); AssertRC(rc2);
        }
        else
            supHardNtViRdrDestroy(&pNtViRdr->Core);
    }
    SUP_DPRINTF(("supHardenedWinVerifyImageByHandle: -> %d (%ls)%s\n",
                 rc, pwszName, pfWinVerifyTrust && *pfWinVerifyTrust ? " WinVerifyTrust" : ""));
    return rc;
}


#ifdef IN_RING3
/**
 * supHardenedWinVerifyImageByHandle version without the name.
 *
 * The name is derived from the handle.
 *
 * @returns IPRT status code.
 * @param   hFile       File handle to the executable file.
 * @param   fFlags      Flags, SUPHNTVI_F_XXX.
 * @param   pErrInfo    Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardenedWinVerifyImageByHandleNoName(HANDLE hFile, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * Determine the NT name and call the verification function.
     */
    union
    {
        UNICODE_STRING UniStr;
        uint8_t abBuffer[(MAX_PATH + 8 + 1) * 2];
    } uBuf;

    ULONG cbIgn;
    NTSTATUS rcNt = NtQueryObject(hFile,
                                  ObjectNameInformation,
                                  &uBuf,
                                  sizeof(uBuf) - sizeof(WCHAR),
                                  &cbIgn);
    if (NT_SUCCESS(rcNt))
        uBuf.UniStr.Buffer[uBuf.UniStr.Length / sizeof(WCHAR)] = '\0';
    else
        uBuf.UniStr.Buffer = (WCHAR *)L"TODO3";

    return supHardenedWinVerifyImageByHandle(hFile, uBuf.UniStr.Buffer, fFlags, false /*fAvoidWinVerifyTrust*/,
                                             NULL /*pfWinVerifyTrust*/, pErrInfo);
}
#endif /* IN_RING3 */


/**
 * Retrieves the full official path to the system root or one of it's sub
 * directories.
 *
 * This code is also used by the support driver.
 *
 * @returns VBox status code.
 * @param   pvBuf               The output buffer.  This will contain a
 *                              UNICODE_STRING followed (at the kernel's
 *                              discretion) the string buffer.
 * @param   cbBuf               The size of the buffer @a pvBuf points to.
 * @param   enmDir              Which directory under the system root we're
 *                              interested in.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardNtGetSystemRootDir(void *pvBuf, uint32_t cbBuf, SUPHARDNTSYSROOTDIR enmDir, PRTERRINFO pErrInfo)
{
    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

    UNICODE_STRING      NtName;
    switch (enmDir)
    {
        case kSupHardNtSysRootDir_System32:
        {
            static const WCHAR  s_wszNameSystem32[] = L"\\SystemRoot\\System32\\";
            NtName.Buffer        = (PWSTR)s_wszNameSystem32;
            NtName.Length        = sizeof(s_wszNameSystem32) - sizeof(WCHAR);
            NtName.MaximumLength = sizeof(s_wszNameSystem32);
            break;
        }
        case kSupHardNtSysRootDir_WinSxS:
        {
            static const WCHAR  s_wszNameWinSxS[] = L"\\SystemRoot\\WinSxS\\";
            NtName.Buffer        = (PWSTR)s_wszNameWinSxS;
            NtName.Length        = sizeof(s_wszNameWinSxS) - sizeof(WCHAR);
            NtName.MaximumLength = sizeof(s_wszNameWinSxS);
            break;
        }
        default:
            AssertFailed();
            return VERR_INVALID_PARAMETER;
    }

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 FILE_READ_DATA | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN,
                                 FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (NT_SUCCESS(rcNt))
    {
        ULONG cbIgn;
        rcNt = NtQueryObject(hFile,
                             ObjectNameInformation,
                             pvBuf,
                             cbBuf - sizeof(WCHAR),
                             &cbIgn);
        NtClose(hFile);
        if (NT_SUCCESS(rcNt))
        {
            PUNICODE_STRING pUniStr = (PUNICODE_STRING)pvBuf;
            if (pUniStr->Length > 0)
            {
                /* Make sure it's terminated so it can safely be printed.*/
                pUniStr->Buffer[pUniStr->Length / sizeof(WCHAR)] = '\0';
                return VINF_SUCCESS;
            }

            return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_SYSTEM32_PATH,
                                 "NtQueryObject returned an empty path for '%ls'", NtName.Buffer);
        }
        return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_SYSTEM32_PATH, "NtQueryObject failed on '%ls' dir: %#x", NtName.Buffer, rcNt);
    }
    return RTErrInfoSetF(pErrInfo, VERR_SUP_VP_SYSTEM32_PATH, "Failure to open '%ls': %#x", NtName.Buffer, rcNt);
}


/**
 * Initialize one certificate entry.
 *
 * @returns VBox status code.
 * @param   pCert               The X.509 certificate representation to init.
 * @param   pabCert             The raw DER encoded certificate.
 * @param   cbCert              The size of the raw certificate.
 * @param   pErrInfo            Where to return extended error info. Optional.
 * @param   pszErrorTag         Error tag.
 */
static int supHardNtViCertInit(PRTCRX509CERTIFICATE pCert, unsigned char const *pabCert, unsigned cbCert,
                               PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    AssertReturn(cbCert > 16 && cbCert < _128K,
                 RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_3, "%s: cbCert=%#x out of range", pszErrorTag, cbCert));
    AssertReturn(!RTCrX509Certificate_IsPresent(pCert),
                 RTErrInfoSetF(pErrInfo, VERR_WRONG_ORDER, "%s: Certificate already decoded?", pszErrorTag));

    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pabCert, cbCert, pErrInfo, &g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, NULL);
    int rc = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, pCert, pszErrorTag);
    if (RT_SUCCESS(rc))
        rc = RTCrX509Certificate_CheckSanity(pCert, 0, pErrInfo, pszErrorTag);
    return rc;
}


static int supHardNtViCertStoreAddArray(RTCRSTORE hStore, PCSUPTAENTRY paCerts, unsigned cCerts, PRTERRINFO pErrInfo)
{
    for (uint32_t i = 0; i < cCerts; i++)
    {
        int rc = RTCrStoreCertAddEncoded(hStore, !paCerts[i].fIsCert ? RTCRCERTCTX_F_ENC_TAF_DER : RTCRCERTCTX_F_ENC_X509_DER,
                                         paCerts[i].pch, paCerts[i].cb, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Initialize a certificate table.
 *
 * @param   phStore             Where to return the store pointer.
 * @param   paCerts1            Pointer to the first certificate table.
 * @param   cCerts1             Entries in the first certificate table.
 * @param   paCerts2            Pointer to the second certificate table.
 * @param   cCerts2             Entries in the second certificate table.
 * @param   paCerts3            Pointer to the third certificate table.
 * @param   cCerts3             Entries in the third certificate table.
 * @param   pErrInfo            Where to return extended error info. Optional.
 * @param   pszErrorTag         Error tag.
 */
static int supHardNtViCertStoreInit(PRTCRSTORE phStore,
                                    PCSUPTAENTRY paCerts1, unsigned cCerts1,
                                    PCSUPTAENTRY paCerts2, unsigned cCerts2,
                                    PCSUPTAENTRY paCerts3, unsigned cCerts3,
                                    PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    AssertReturn(*phStore == NIL_RTCRSTORE, VERR_WRONG_ORDER);
    RT_NOREF1(pszErrorTag);

    int rc = RTCrStoreCreateInMem(phStore, cCerts1 + cCerts2);
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "RTCrStoreCreateMemoryStore failed: %Rrc", rc);

    rc = supHardNtViCertStoreAddArray(*phStore, paCerts1, cCerts1, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = supHardNtViCertStoreAddArray(*phStore, paCerts2, cCerts2, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = supHardNtViCertStoreAddArray(*phStore, paCerts3, cCerts3, pErrInfo);
    return rc;
}


#if defined(IN_RING3) && !defined(VBOX_PERMIT_EVEN_MORE)
/**
 * Initializes the windows paths.
 */
static void supHardenedWinInitImageVerifierWinPaths(void)
{
    /*
     * Windows paths that we're interested in.
     */
    static const struct
    {
        SUPSYSROOTDIRBUF   *pNtPath;
        WCHAR const        *pwszRegValue;
        const char         *pszLogName;
    } s_aPaths[] =
    {
        { &g_ProgramFilesNtPath,    L"ProgramFilesDir",         "ProgDir" },
        { &g_CommonFilesNtPath,     L"CommonFilesDir",          "ComDir" },
# ifdef RT_ARCH_AMD64
        { &g_ProgramFilesX86NtPath, L"ProgramFilesDir (x86)",   "ProgDir32" },
        { &g_CommonFilesX86NtPath,  L"CommonFilesDir (x86)",    "ComDir32" },
# endif
    };

    /*
     * Open the registry key containing the paths.
     */
    UNICODE_STRING NtName = RTNT_CONSTANT_UNISTR(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion");
    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
    HANDLE hKey;
    NTSTATUS rcNt = NtOpenKey(&hKey, KEY_QUERY_VALUE, &ObjAttr);
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Loop over the paths and resolve their NT paths.
         */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aPaths); i++)
        {
            /*
             * Query the value first.
             */
            UNICODE_STRING ValueName;
            ValueName.Buffer = (WCHAR *)s_aPaths[i].pwszRegValue;
            ValueName.Length = (USHORT)(RTUtf16Len(s_aPaths[i].pwszRegValue) * sizeof(WCHAR));
            ValueName.MaximumLength = ValueName.Length + sizeof(WCHAR);

            union
            {
                KEY_VALUE_PARTIAL_INFORMATION   PartialInfo;
                uint8_t                         abPadding[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(WCHAR) * 128];
                uint64_t                        uAlign;
            } uBuf;

            ULONG cbActual = 0;
            rcNt = NtQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, &uBuf, sizeof(uBuf) - sizeof(WCHAR), &cbActual);
            if (NT_SUCCESS(rcNt))
            {
                /*
                 * Must be a simple string value, terminate it.
                 */
                if (   uBuf.PartialInfo.Type == REG_EXPAND_SZ
                    || uBuf.PartialInfo.Type == REG_SZ)
                {
                    /*
                     * Expand any environment variable references before opening it.
                     * We use the result buffer as storage for the expaneded path,
                     * reserving space for the windows name space prefix.
                     */
                    UNICODE_STRING Src;
                    Src.Buffer = (WCHAR *)uBuf.PartialInfo.Data;
                    Src.Length = uBuf.PartialInfo.DataLength;
                    if (Src.Length >= sizeof(WCHAR) && Src.Buffer[Src.Length / sizeof(WCHAR) - 1] == '\0')
                        Src.Length -= sizeof(WCHAR);
                    Src.MaximumLength = Src.Length + sizeof(WCHAR);
                    Src.Buffer[uBuf.PartialInfo.DataLength / sizeof(WCHAR)] = '\0';

                    s_aPaths[i].pNtPath->awcBuffer[0] = '\\';
                    s_aPaths[i].pNtPath->awcBuffer[1] = '?';
                    s_aPaths[i].pNtPath->awcBuffer[2] = '?';
                    s_aPaths[i].pNtPath->awcBuffer[3] = '\\';
                    UNICODE_STRING Dst;
                    Dst.Buffer = &s_aPaths[i].pNtPath->awcBuffer[4];
                    Dst.MaximumLength = sizeof(s_aPaths[i].pNtPath->awcBuffer) - sizeof(WCHAR) * 5;
                    Dst.Length = Dst.MaximumLength;

                    if (uBuf.PartialInfo.Type == REG_EXPAND_SZ)
                        rcNt = RtlExpandEnvironmentStrings_U(NULL, &Src, &Dst, NULL);
                    else
                    {
                        memcpy(Dst.Buffer, Src.Buffer, Src.Length);
                        Dst.Length = Src.Length;
                    }
                    if (NT_SUCCESS(rcNt))
                    {
                        Dst.Buffer[Dst.Length / sizeof(WCHAR)] = '\0';

                        /*
                         * Include the \\??\\ prefix in the result and open the path.
                         */
                        Dst.Buffer        -= 4;
                        Dst.Length        += 4 * sizeof(WCHAR);
                        Dst.MaximumLength += 4 * sizeof(WCHAR);
                        InitializeObjectAttributes(&ObjAttr, &Dst, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
                        HANDLE          hFile = INVALID_HANDLE_VALUE;
                        IO_STATUS_BLOCK Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                        NTSTATUS rcNt = NtCreateFile(&hFile,
                                                     FILE_READ_DATA | SYNCHRONIZE,
                                                     &ObjAttr,
                                                     &Ios,
                                                     NULL /* Allocation Size*/,
                                                     FILE_ATTRIBUTE_NORMAL,
                                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                     FILE_OPEN,
                                                     FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT
                                                     | FILE_SYNCHRONOUS_IO_NONALERT,
                                                     NULL /*EaBuffer*/,
                                                     0 /*EaLength*/);
                        if (NT_SUCCESS(rcNt))
                            rcNt = Ios.Status;
                        if (NT_SUCCESS(rcNt))
                        {
                            /*
                             * Query the real NT name.
                             */
                            ULONG cbIgn;
                            rcNt = NtQueryObject(hFile,
                                                 ObjectNameInformation,
                                                 s_aPaths[i].pNtPath,
                                                 sizeof(*s_aPaths[i].pNtPath) - sizeof(WCHAR),
                                                 &cbIgn);
                            if (NT_SUCCESS(rcNt))
                            {
                                if (s_aPaths[i].pNtPath->UniStr.Length > 0)
                                {
                                    /* Make sure it's terminated.*/
                                    s_aPaths[i].pNtPath->UniStr.Buffer[s_aPaths[i].pNtPath->UniStr.Length / sizeof(WCHAR)] = '\0';
                                    SUP_DPRINTF(("%s:%*s %ls\n", s_aPaths[i].pszLogName, 9 - strlen(s_aPaths[i].pszLogName), "",
                                                 s_aPaths[i].pNtPath->UniStr.Buffer));
                                }
                                else
                                {
                                    SUP_DPRINTF(("%s: NtQueryObject returned empty string\n", s_aPaths[i].pszLogName));
                                    rcNt = STATUS_INVALID_PARAMETER;
                                }
                            }
                            else
                                SUP_DPRINTF(("%s: NtQueryObject failed: %#x\n", s_aPaths[i].pszLogName, rcNt));
                            NtClose(hFile);
                        }
                        else
                            SUP_DPRINTF(("%s: NtCreateFile failed: %#x (%ls)\n",
                                         s_aPaths[i].pszLogName, rcNt, Dst.Buffer));
                    }
                    else
                        SUP_DPRINTF(("%s: RtlExpandEnvironmentStrings_U failed: %#x (%ls)\n",
                                     s_aPaths[i].pszLogName, rcNt, Src.Buffer));
                }
                else
                {
                    SUP_DPRINTF(("%s: type mismatch: %#x\n", s_aPaths[i].pszLogName, uBuf.PartialInfo.Type));
                    rcNt = STATUS_INVALID_PARAMETER;
                }
            }
            else
                SUP_DPRINTF(("%s: NtQueryValueKey failed: %#x\n", s_aPaths[i].pszLogName, rcNt));

            /* Stub the entry on failure. */
            if (!NT_SUCCESS(rcNt))
            {
                s_aPaths[i].pNtPath->UniStr.Length = 0;
                s_aPaths[i].pNtPath->UniStr.Buffer = NULL;
            }
        }
        NtClose(hKey);
    }
    else
    {
        SUP_DPRINTF(("NtOpenKey(%ls) failed: %#x\n", NtName.Buffer, rcNt));

        /* Stub all the entries on failure. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aPaths); i++)
        {
            s_aPaths[i].pNtPath->UniStr.Length = 0;
            s_aPaths[i].pNtPath->UniStr.Buffer = NULL;
        }
    }
}
#endif /* IN_RING3 && !VBOX_PERMIT_EVEN_MORE */


/**
 * This initializes the certificates globals so we don't have to reparse them
 * every time we need to verify an image.
 *
 * @returns IPRT status code.
 * @param   pErrInfo            Where to return extended error info. Optional.
 */
DECLHIDDEN(int) supHardenedWinInitImageVerifier(PRTERRINFO pErrInfo)
{
    AssertReturn(!RTCrX509Certificate_IsPresent(&g_BuildX509Cert), VERR_WRONG_ORDER);

    /*
     * Get the system root paths.
     */
    int rc = supHardNtGetSystemRootDir(&g_System32NtPath, sizeof(g_System32NtPath), kSupHardNtSysRootDir_System32, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = supHardNtGetSystemRootDir(&g_WinSxSNtPath, sizeof(g_WinSxSNtPath), kSupHardNtSysRootDir_WinSxS, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        SUP_DPRINTF(("System32:  %ls\n", g_System32NtPath.UniStr.Buffer));
        SUP_DPRINTF(("WinSxS:    %ls\n", g_WinSxSNtPath.UniStr.Buffer));
#if defined(IN_RING3) && !defined(VBOX_PERMIT_EVEN_MORE)
        supHardenedWinInitImageVerifierWinPaths();
#endif

        /*
         * Initialize it, leaving the cleanup to the termination call.
         */
        rc = supHardNtViCertInit(&g_BuildX509Cert, g_abSUPBuildCert, g_cbSUPBuildCert, pErrInfo, "BuildCertificate");
        SUPTAENTRY const aBuildCerts[1] = { { g_abSUPBuildCert, g_cbSUPBuildCert, true }, };
        if (RT_SUCCESS(rc))
            rc = supHardNtViCertStoreInit(&g_hSpecialTrustStore,
                                          aBuildCerts, RT_ELEMENTS(aBuildCerts),
                                          g_aSUPTrustedTAs, g_cSUPTrustedTAs,
                                          NULL, 0,
                                          pErrInfo, "SpecialTrustStore");
        if (RT_SUCCESS(rc))
            rc = supHardNtViCertStoreInit(&g_hSpcRootStore, g_aSUPSpcRootTAs, g_cSUPSpcRootTAs,
                                          NULL, 0, NULL, 0, pErrInfo, "SpcRoot");
        if (RT_SUCCESS(rc))
            rc = supHardNtViCertStoreInit(&g_hNtKernelRootStore, g_aSUPNtKernelRootTAs, g_cSUPNtKernelRootTAs,
                                          NULL, 0, NULL, 0, pErrInfo, "NtKernelRoot");
        if (RT_SUCCESS(rc))
            rc = supHardNtViCertStoreInit(&g_hSpcAndNtKernelRootStore,
                                          g_aSUPSpcRootTAs, g_cSUPSpcRootTAs,
                                          g_aSUPNtKernelRootTAs, g_cSUPNtKernelRootTAs,
                                          g_aSUPTimestampTAs, g_cSUPTimestampTAs,
                                          pErrInfo, "SpcAndNtKernelRoot");
        if (RT_SUCCESS(rc))
            rc = supHardNtViCertStoreInit(&g_hSpcAndNtKernelSuppStore,
                                          NULL, 0, NULL, 0, NULL, 0,
                                          pErrInfo, "SpcAndNtKernelSupplemental");

#if 0 /* For the time being, always trust the build certificate. It bypasses the timestamp issues of CRT and SDL. */
        /* If the build certificate is a test singing certificate, it must be a
           trusted root or we'll fail to validate anything. */
        if (   RT_SUCCESS(rc)
            && RTCrX509Name_Compare(&g_BuildX509Cert.TbsCertificate.Subject, &g_BuildX509Cert.TbsCertificate.Issuer) == 0)
#else
        if (RT_SUCCESS(rc))
#endif
            rc = RTCrStoreCertAddEncoded(g_hSpcAndNtKernelRootStore, RTCRCERTCTX_F_ENC_X509_DER,
                                         g_abSUPBuildCert, g_cbSUPBuildCert, pErrInfo);

        if (RT_SUCCESS(rc))
        {
            /*
             * Finally initialize known SIDs that we use.
             */
            SID_IDENTIFIER_AUTHORITY s_NtAuth = SECURITY_NT_AUTHORITY;
            NTSTATUS rcNt = RtlInitializeSid(&g_TrustedInstallerSid, &s_NtAuth, SECURITY_SERVICE_ID_RID_COUNT);
            if (NT_SUCCESS(rcNt))
            {
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 0) = SECURITY_SERVICE_ID_BASE_RID;
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 1) = 956008885;
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 2) = 3418522649;
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 3) = 1831038044;
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 4) = 1853292631;
                *RtlSubAuthoritySid(&g_TrustedInstallerSid, 5) = 2271478464;

                rcNt = RtlInitializeSid(&g_LocalSystemSid, &s_NtAuth, 1);
                if (NT_SUCCESS(rcNt))
                {
                    *RtlSubAuthoritySid(&g_LocalSystemSid, 0) = SECURITY_LOCAL_SYSTEM_RID;

                    rcNt = RtlInitializeSid(&g_AdminsGroupSid, &s_NtAuth, 2);
                    if (NT_SUCCESS(rcNt))
                    {
                        *RtlSubAuthoritySid(&g_AdminsGroupSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
                        *RtlSubAuthoritySid(&g_AdminsGroupSid, 1) = DOMAIN_ALIAS_RID_ADMINS;
                        return VINF_SUCCESS;
                    }
                }
            }
            rc = RTErrConvertFromNtStatus(rcNt);
        }
        supHardenedWinTermImageVerifier();
    }
    return rc;
}


/**
 * Releases resources allocated by supHardenedWinInitImageVerifier.
 */
DECLHIDDEN(void) supHardenedWinTermImageVerifier(void)
{
    if (RTCrX509Certificate_IsPresent(&g_BuildX509Cert))
        RTAsn1VtDelete(&g_BuildX509Cert.SeqCore.Asn1Core);

    RTCrStoreRelease(g_hSpcAndNtKernelSuppStore);
    g_hSpcAndNtKernelSuppStore = NIL_RTCRSTORE;
    RTCrStoreRelease(g_hSpcAndNtKernelRootStore);
    g_hSpcAndNtKernelRootStore = NIL_RTCRSTORE;

    RTCrStoreRelease(g_hNtKernelRootStore);
    g_hNtKernelRootStore = NIL_RTCRSTORE;
    RTCrStoreRelease(g_hSpcRootStore);
    g_hSpcRootStore = NIL_RTCRSTORE;

    RTCrStoreRelease(g_hSpecialTrustStore);
    g_hSpecialTrustStore = NIL_RTCRSTORE;
}

#ifdef IN_RING3

/**
 * This is a hardcoded list of certificates we thing we might need.
 *
 * @returns true if wanted, false if not.
 * @param   pCert               The certificate.
 */
static bool supR3HardenedWinIsDesiredRootCA(PCRTCRX509CERTIFICATE pCert)
{
    char szSubject[512];
    szSubject[sizeof(szSubject) - 1] = '\0';
    RTCrX509Name_FormatAsString(&pCert->TbsCertificate.Subject, szSubject, sizeof(szSubject) - 1, NULL);

    /*
     * Check that it's a plausible root certificate.
     */
    if (!RTCrX509Certificate_IsSelfSigned(pCert))
    {
        SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping - not-self-signed: %s\n", szSubject));
        return false;
    }

    if (RTAsn1Integer_UnsignedCompareWithU32(&pCert->TbsCertificate.T0.Version, 3) > 0)
    {
        if (   !(pCert->TbsCertificate.T3.fExtKeyUsage & RTCRX509CERT_KEY_USAGE_F_KEY_CERT_SIGN)
            && (pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE) )
        {
            SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping - non-cert-sign: %s\n", szSubject));
            return false;
        }
        if (   pCert->TbsCertificate.T3.pBasicConstraints
            && !pCert->TbsCertificate.T3.pBasicConstraints->CA.fValue)
        {
            SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping - non-CA: %s\n", szSubject));
            return false;
        }
    }
    if (pCert->TbsCertificate.SubjectPublicKeyInfo.SubjectPublicKey.cBits < 256) /* mostly for u64KeyId reading. */
    {
        SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping - key too small: %u bits %s\n",
                     pCert->TbsCertificate.SubjectPublicKeyInfo.SubjectPublicKey.cBits, szSubject));
        return false;
    }
    uint64_t const u64KeyId = pCert->TbsCertificate.SubjectPublicKeyInfo.SubjectPublicKey.uBits.pu64[1];

# if 0
    /*
     * Whitelist - Array of names and key clues of the certificates we want.
     */
    static struct
    {
        uint64_t    u64KeyId;
        const char *pszName;
    } const s_aWanted[] =
    {
        /* SPC */
        { UINT64_C(0xffffffffffffffff), "C=US, O=VeriSign, Inc., OU=Class 3 Public Primary Certification Authority" },
        { UINT64_C(0xffffffffffffffff), "L=Internet, O=VeriSign, Inc., OU=VeriSign Commercial Software Publishers CA" },
        { UINT64_C(0x491857ead79dde00), "C=US, O=The Go Daddy Group, Inc., OU=Go Daddy Class 2 Certification Authority" },

        /* TS */
        { UINT64_C(0xffffffffffffffff), "O=Microsoft Trust Network, OU=Microsoft Corporation, OU=Microsoft Time Stamping Service Root, OU=Copyright (c) 1997 Microsoft Corp." },
        { UINT64_C(0xffffffffffffffff), "O=VeriSign Trust Network, OU=VeriSign, Inc., OU=VeriSign Time Stamping Service Root, OU=NO LIABILITY ACCEPTED, (c)97 VeriSign, Inc." },
        { UINT64_C(0xffffffffffffffff), "C=ZA, ST=Western Cape, L=Durbanville, O=Thawte, OU=Thawte Certification, CN=Thawte Timestamping CA" },

        /* Additional Windows 8.1 list: */
        { UINT64_C(0x5ad46780fa5df300), "DC=com, DC=microsoft, CN=Microsoft Root Certificate Authority" },
        { UINT64_C(0x3be670c1bd02a900), "OU=Copyright (c) 1997 Microsoft Corp., OU=Microsoft Corporation, CN=Microsoft Root Authority" },
        { UINT64_C(0x4d3835aa4180b200), "C=US, ST=Washington, L=Redmond, O=Microsoft Corporation, CN=Microsoft Root Certificate Authority 2011" },
        { UINT64_C(0x646e3fe3ba08df00), "C=US, O=MSFT, CN=Microsoft Authenticode(tm) Root Authority" },
        { UINT64_C(0xece4e4289e08b900), "C=US, ST=Washington, L=Redmond, O=Microsoft Corporation, CN=Microsoft Root Certificate Authority 2010" },
        { UINT64_C(0x59faf1086271bf00), "C=US, ST=Arizona, L=Scottsdale, O=GoDaddy.com, Inc., CN=Go Daddy Root Certificate Authority - G2" },
        { UINT64_C(0x3d98ab22bb04a300), "C=IE, O=Baltimore, OU=CyberTrust, CN=Baltimore CyberTrust Root" },
        { UINT64_C(0x91e3728b8b40d000), "C=GB, ST=Greater Manchester, L=Salford, O=COMODO CA Limited, CN=COMODO Certification Authority" },
        { UINT64_C(0x61a3a33f81aace00), "C=US, ST=UT, L=Salt Lake City, O=The USERTRUST Network, OU=http://www.usertrust.com, CN=UTN-USERFirst-Object" },
        { UINT64_C(0x9e5bc2d78b6a3636), "C=ZA, ST=Western Cape, L=Cape Town, O=Thawte Consulting cc, OU=Certification Services Division, CN=Thawte Premium Server CA, Email=premium-server@thawte.com" },
        { UINT64_C(0xf4fd306318ccda00), "C=US, O=GeoTrust Inc., CN=GeoTrust Global CA" },
        { UINT64_C(0xa0ee62086758b15d), "C=US, O=Equifax, OU=Equifax Secure Certificate Authority" },
        { UINT64_C(0x8ff6fc03c1edbd00), "C=US, ST=Arizona, L=Scottsdale, O=Starfield Technologies, Inc., CN=Starfield Root Certificate Authority - G2" },
        { UINT64_C(0xa3ce8d99e60eda00), "C=BE, O=GlobalSign nv-sa, OU=Root CA, CN=GlobalSign Root CA" },
        { UINT64_C(0xa671e9fec832b700), "C=US, O=Starfield Technologies, Inc., OU=Starfield Class 2 Certification Authority" },
        { UINT64_C(0xa8de7211e13be200), "C=US, O=DigiCert Inc, OU=www.digicert.com, CN=DigiCert Global Root CA" },
        { UINT64_C(0x0ff3891b54348328), "C=US, O=Entrust.net, OU=www.entrust.net/CPS incorp. by ref. (limits liab.), OU=(c) 1999 Entrust.net Limited, CN=Entrust.netSecure Server Certification Authority" },
        { UINT64_C(0x7ae89c50f0b6a00f), "C=US, O=GTE Corporation, OU=GTE CyberTrust Solutions, Inc., CN=GTE CyberTrust Global Root" },
        { UINT64_C(0xd45980fbf0a0ac00), "C=US, O=thawte, Inc., OU=Certification Services Division, OU=(c) 2006 thawte, Inc. - For authorized use only, CN=thawte Primary Root CA" },
        { UINT64_C(0x9e5bc2d78b6a3636), "C=ZA, ST=Western Cape, L=Cape Town, O=Thawte Consulting cc, OU=Certification Services Division, CN=Thawte Premium Server CA, Email=premium-server@thawte.com" },
        { UINT64_C(0x7c4fd32ec1b1ce00), "C=PL, O=Unizeto Sp. z o.o., CN=Certum CA" },
        { UINT64_C(0xd4fbe673e5ccc600), "C=US, O=DigiCert Inc, OU=www.digicert.com, CN=DigiCert High Assurance EV Root CA" },
        { UINT64_C(0x16e64d2a56ccf200), "C=US, ST=Arizona, L=Scottsdale, O=Starfield Technologies, Inc., OU=http://certificates.starfieldtech.com/repository/, CN=Starfield Services Root Certificate Authority" },
        { UINT64_C(0x6e2ba21058eedf00), "C=US, ST=UT, L=Salt Lake City, O=The USERTRUST Network, OU=http://www.usertrust.com, CN=UTN - DATACorp SGC" },
        { UINT64_C(0xb28612a94b4dad00), "O=Entrust.net, OU=www.entrust.net/CPS_2048 incorp. by ref. (limits liab.), OU=(c) 1999 Entrust.net Limited, CN=Entrust.netCertification Authority (2048)" },
        { UINT64_C(0x357a29080824af00), "C=US, O=VeriSign, Inc., OU=VeriSign Trust Network, OU=(c) 2006 VeriSign, Inc. - For authorized use only, CN=VeriSign Class3 Public Primary Certification Authority - G5" },
        { UINT64_C(0x466cbc09db88c100), "C=IL, O=StartCom Ltd., OU=Secure Digital Certificate Signing, CN=StartCom Certification Authority" },
        { UINT64_C(0x9259c8abe5ca713a), "L=ValiCert Validation Network, O=ValiCert, Inc., OU=ValiCert Class 2 Policy Validation Authority, CN=http://www.valicert.com/, Email=info@valicert.com" },
        { UINT64_C(0x1f78fc529cbacb00), "C=US, O=VeriSign, Inc., OU=VeriSign Trust Network, OU=(c) 1999 VeriSign, Inc. - For authorized use only, CN=VeriSign Class3 Public Primary Certification Authority - G3" },
        { UINT64_C(0x8043e4ce150ead00), "C=US, O=DigiCert Inc, OU=www.digicert.com, CN=DigiCert Assured ID Root CA" },
        { UINT64_C(0x00f2e6331af7b700), "C=SE, O=AddTrust AB, OU=AddTrust External TTP Network, CN=AddTrust External CA Root" },
    };


    uint32_t i = RT_ELEMENTS(s_aWanted);
    while (i-- > 0)
        if (   s_aWanted[i].u64KeyId == u64KeyId
            || s_aWanted[i].u64KeyId == UINT64_MAX)
            if (RTCrX509Name_MatchWithString(&pCert->TbsCertificate.Subject, s_aWanted[i].pszName))
            {
                SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: Adding %#llx %s\n", u64KeyId, szSubject));
                return true;
            }

    SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping %#llx %s\n", u64KeyId, szSubject));
    return false;
# else
    /*
     * Blacklist approach.
     */
    static struct
    {
        uint64_t    u64KeyId;
        const char *pszName;
    } const s_aUnwanted[] =
    {
        { UINT64_C(0xffffffffffffffff), "C=US, O=U.S. Robots and Mechanical Men, Inc., OU=V.I.K.I." }, /* dummy entry */
    };

    uint32_t i = RT_ELEMENTS(s_aUnwanted);
    while (i-- > 0)
        if (   s_aUnwanted[i].u64KeyId == u64KeyId
            || s_aUnwanted[i].u64KeyId == UINT64_MAX)
            if (RTCrX509Name_MatchWithString(&pCert->TbsCertificate.Subject, s_aUnwanted[i].pszName))
            {
                SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: skipping - blacklisted: %#llx %s\n", u64KeyId, szSubject));
                return false;
            }

    SUP_DPRINTF(("supR3HardenedWinIsDesiredRootCA: Adding %#llx %s\n", u64KeyId, szSubject));
    return true;
# endif
}


/**
 * Loads a module in the system32 directory.
 *
 * @returns Module handle on success. Won't return on failure if fMandatory = true.
 * @param   pszName             The name of the DLL to load.
 * @param   fMandatory          Whether the library is mandatory.
 */
DECLHIDDEN(HMODULE) supR3HardenedWinLoadSystem32Dll(const char *pszName, bool fMandatory)
{
    WCHAR wszName[200+60];
    UINT cwcDir = GetSystemDirectoryW(wszName, RT_ELEMENTS(wszName) - 60);
    wszName[cwcDir] = '\\';
    RTUtf16CopyAscii(&wszName[cwcDir + 1], RT_ELEMENTS(wszName) - cwcDir, pszName);

    DWORD fFlags = 0;
    if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0))
       fFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;
    HMODULE hMod = LoadLibraryExW(wszName, NULL, fFlags);
    if (   hMod == NULL
        && fFlags
        && g_uNtVerCombined < SUP_MAKE_NT_VER_SIMPLE(6, 2)
        && RtlGetLastWin32Error() == ERROR_INVALID_PARAMETER)
    {
        fFlags = 0;
        hMod = LoadLibraryExW(wszName, NULL, fFlags);
    }
    if (   hMod == NULL
        && fMandatory)
        supR3HardenedFatal("Error loading '%s': %u [%ls]", pszName, RtlGetLastWin32Error(), wszName);
    return hMod;
}


/**
 * Called by supR3HardenedWinResolveVerifyTrustApiAndHookThreadCreation to
 * import selected root CAs from the system certificate store.
 *
 * These certificates permits us to correctly validate third party DLLs.
 */
static void supR3HardenedWinRetrieveTrustedRootCAs(void)
{
    uint32_t cAdded = 0;

    /*
     * Load crypt32.dll and resolve the APIs we need.
     */
    HMODULE hCrypt32 = supR3HardenedWinLoadSystem32Dll("crypt32.dll", true /*fMandatory*/);

#define RESOLVE_CRYPT32_API(a_Name, a_pfnType) \
    a_pfnType pfn##a_Name = (a_pfnType)GetProcAddress(hCrypt32, #a_Name); \
    if (pfn##a_Name == NULL) supR3HardenedFatal("Error locating '" #a_Name "' in 'crypt32.dll': %u", RtlGetLastWin32Error())
    RESOLVE_CRYPT32_API(CertOpenStore, PFNCERTOPENSTORE);
    RESOLVE_CRYPT32_API(CertCloseStore, PFNCERTCLOSESTORE);
    RESOLVE_CRYPT32_API(CertEnumCertificatesInStore, PFNCERTENUMCERTIFICATESINSTORE);
#undef RESOLVE_CRYPT32_API

    /*
     * Open the root store and look for the certificates we wish to use.
     */
    DWORD fOpenStore = CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG;
    HCERTSTORE hStore = pfnCertOpenStore(CERT_STORE_PROV_SYSTEM_W, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                         NULL /* hCryptProv = default */, CERT_SYSTEM_STORE_LOCAL_MACHINE | fOpenStore, L"Root");
    if (!hStore)
        hStore = pfnCertOpenStore(CERT_STORE_PROV_SYSTEM_W, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                  NULL /* hCryptProv = default */, CERT_SYSTEM_STORE_CURRENT_USER | fOpenStore, L"Root");
    if (hStore)
    {
        PCCERT_CONTEXT pCurCtx  = NULL;
        while ((pCurCtx = pfnCertEnumCertificatesInStore(hStore, pCurCtx)) != NULL)
        {
            if (pCurCtx->dwCertEncodingType & X509_ASN_ENCODING)
            {
                RTERRINFOSTATIC StaticErrInfo;
                RTASN1CURSORPRIMARY PrimaryCursor;
                RTAsn1CursorInitPrimary(&PrimaryCursor, pCurCtx->pbCertEncoded, pCurCtx->cbCertEncoded,
                                        RTErrInfoInitStatic(&StaticErrInfo),
                                        &g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, "CurCtx");
                RTCRX509CERTIFICATE MyCert;
                int rc = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, &MyCert, "Cert");
                if (RT_SUCCESS(rc))
                {
                    if (supR3HardenedWinIsDesiredRootCA(&MyCert))
                    {
                        rc = RTCrStoreCertAddEncoded(g_hSpcRootStore, RTCRCERTCTX_F_ENC_X509_DER,
                                                     pCurCtx->pbCertEncoded, pCurCtx->cbCertEncoded, NULL /*pErrInfo*/);
                        AssertRC(rc);

                        rc = RTCrStoreCertAddEncoded(g_hSpcAndNtKernelRootStore, RTCRCERTCTX_F_ENC_X509_DER,
                                                     pCurCtx->pbCertEncoded, pCurCtx->cbCertEncoded, NULL /*pErrInfo*/);
                        AssertRC(rc);
                        cAdded++;
                    }

                    RTCrX509Certificate_Delete(&MyCert);
                }
                /* XP root certificate "C&W HKT SecureNet CA SGC Root" has non-standard validity
                   timestamps, the UTC formatting isn't Zulu time but specifies timezone offsets.
                   Ignore these failures and certificates. */
                else if (rc != VERR_ASN1_INVALID_UTC_TIME_ENCODING)
                    AssertMsgFailed(("RTCrX509Certificate_DecodeAsn1 failed: rc=%#x: %s\n", rc, StaticErrInfo.szMsg));
            }
        }
        pfnCertCloseStore(hStore, CERT_CLOSE_STORE_CHECK_FLAG);
        g_fHaveOtherRoots = true;
    }
    SUP_DPRINTF(("supR3HardenedWinRetrieveTrustedRootCAs: cAdded=%u\n", cAdded));
}


/**
 * Resolves the WinVerifyTrust API after the process has been verified and
 * installs a thread creation hook.
 *
 * The WinVerifyTrust API is used in addition our own Authenticode verification
 * code.  If the image has the IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY flag
 * set, it will be checked again by the kernel.  All our image has this flag set
 * and we require all VBox extensions to have it set as well.  In effect, the
 * authenticode signature will be checked two or three times.
 *
 * @param   pszProgName     The program name.
 */
DECLHIDDEN(void) supR3HardenedWinResolveVerifyTrustApiAndHookThreadCreation(const char *pszProgName)
{
# ifdef IN_SUP_HARDENED_R3
    /*
     * Load our the support library DLL that does the thread hooking as the
     * security API may trigger the creation of COM worker threads (or
     * whatever they are).
     *
     * The thread creation hook makes the threads very slippery to debuggers by
     * irreversably disabling most (if not all) debug events for them.
     */
    char szPath[RTPATH_MAX];
    supR3HardenedPathAppSharedLibs(szPath, sizeof(szPath) - sizeof("/VBoxSupLib.DLL"));
    suplibHardenedStrCat(szPath, "/VBoxSupLib.DLL");
    HMODULE hSupLibMod = (HMODULE)supR3HardenedWinLoadLibrary(szPath, true /*fSystem32Only*/, 0 /*fMainFlags*/);
    if (hSupLibMod == NULL)
        supR3HardenedFatal("Error loading '%s': %u", szPath, RtlGetLastWin32Error());
# endif

    /*
     * Allocate TLS entry for WinVerifyTrust recursion prevention.
     */
    DWORD iTls = TlsAlloc();
    if (iTls != TLS_OUT_OF_INDEXES)
        g_iTlsWinVerifyTrustRecursion = iTls;
    else
        supR3HardenedError(RtlGetLastWin32Error(), false /*fFatal*/, "TlsAlloc failed");

    /*
     * Resolve the imports we need.
     */
    HMODULE hWintrust = supR3HardenedWinLoadSystem32Dll("Wintrust.dll", true /*fMandatory*/);
#define RESOLVE_CRYPT_API(a_Name, a_pfnType, a_uMinWinVer) \
    do { \
        g_pfn##a_Name = (a_pfnType)GetProcAddress(hWintrust, #a_Name); \
        if (g_pfn##a_Name == NULL && (a_uMinWinVer) < g_uNtVerCombined) \
            supR3HardenedFatal("Error locating '" #a_Name "' in 'Wintrust.dll': %u", RtlGetLastWin32Error()); \
    } while (0)

    PFNWINVERIFYTRUST pfnWinVerifyTrust = (PFNWINVERIFYTRUST)GetProcAddress(hWintrust, "WinVerifyTrust");
    if (!pfnWinVerifyTrust)
        supR3HardenedFatal("Error locating 'WinVerifyTrust' in 'Wintrust.dll': %u", RtlGetLastWin32Error());

    RESOLVE_CRYPT_API(CryptCATAdminAcquireContext,           PFNCRYPTCATADMINACQUIRECONTEXT,          0);
    RESOLVE_CRYPT_API(CryptCATAdminCalcHashFromFileHandle,   PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE,  0);
    RESOLVE_CRYPT_API(CryptCATAdminEnumCatalogFromHash,      PFNCRYPTCATADMINENUMCATALOGFROMHASH,     0);
    RESOLVE_CRYPT_API(CryptCATAdminReleaseCatalogContext,    PFNCRYPTCATADMINRELEASECATALOGCONTEXT,   0);
    RESOLVE_CRYPT_API(CryptCATAdminReleaseContext,           PFNCRYPTCATDADMINRELEASECONTEXT,         0);
    RESOLVE_CRYPT_API(CryptCATCatalogInfoFromContext,        PFNCRYPTCATCATALOGINFOFROMCONTEXT,       0);

    RESOLVE_CRYPT_API(CryptCATAdminAcquireContext2,          PFNCRYPTCATADMINACQUIRECONTEXT2,         SUP_NT_VER_W80);
    RESOLVE_CRYPT_API(CryptCATAdminCalcHashFromFileHandle2,  PFNCRYPTCATADMINCALCHASHFROMFILEHANDLE2, SUP_NT_VER_W80);

# ifdef IN_SUP_HARDENED_R3
    /*
     * Load bcrypt.dll and instantiate a few hashing and signing providers to
     * make sure the providers are cached for later us.  Avoid recursion issues.
     */
    HMODULE hBCrypt = supR3HardenedWinLoadSystem32Dll("bcrypt.dll", false /*fMandatory*/);
    if (hBCrypt)
    {
        PFNBCRYPTOPENALGORTIHMPROVIDER pfnOpenAlgoProvider;
        pfnOpenAlgoProvider = (PFNBCRYPTOPENALGORTIHMPROVIDER)GetProcAddress(hBCrypt, "BCryptOpenAlgorithmProvider");
        if (pfnOpenAlgoProvider)
        {
            SUP_DPRINTF(("bcrypt.dll loaded at %p, BCryptOpenAlgorithmProvider at %p, preloading providers:\n",
                         hBCrypt, pfnOpenAlgoProvider));
#  define PRELOAD_ALGO_PROVIDER(a_Name) \
                do { \
                    BCRYPT_ALG_HANDLE hAlgo = NULL; \
                    NTSTATUS rcNt = pfnOpenAlgoProvider(&hAlgo, a_Name, NULL, 0); \
                    SUP_DPRINTF(("%sBCryptOpenAlgorithmProvider(,'%ls',0,0) -> %#x (hAlgo=%p)\n", \
                                 NT_SUCCESS(rcNt) ? "    " : "warning: ", a_Name, rcNt, hAlgo)); \
                } while (0)
            PRELOAD_ALGO_PROVIDER(BCRYPT_MD2_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_MD4_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_MD5_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_SHA1_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_SHA256_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_SHA512_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_RSA_ALGORITHM);
            PRELOAD_ALGO_PROVIDER(BCRYPT_DSA_ALGORITHM);
#  undef PRELOAD_ALGO_PROVIDER
        }
        else
            SUP_DPRINTF(("Warning! Failed to find BCryptOpenAlgorithmProvider in bcrypt.dll\n"));
    }
    else
        SUP_DPRINTF(("Warning! Failed to load bcrypt.dll\n"));

    /*
     * Call the verification API on ourselves and ntdll to make sure it works
     * and loads more stuff it needs, preventing any recursive fun we'd run
     * into after we set g_pfnWinVerifyTrust.
     */
    RTERRINFOSTATIC ErrInfoStatic;
    RTErrInfoInitStatic(&ErrInfoStatic);
    int rc = supR3HardNtViCallWinVerifyTrust(NULL, g_SupLibHardenedExeNtPath.UniStr.Buffer, 0,
                                             &ErrInfoStatic.Core, pfnWinVerifyTrust, NULL);
    if (RT_FAILURE(rc))
        supR3HardenedFatalMsg(pszProgName, kSupInitOp_Integrity, rc,
                              "WinVerifyTrust failed on stub executable: %s", ErrInfoStatic.szMsg);
# else
    RT_NOREF1(pszProgName);
# endif

    if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0)) /* ntdll isn't signed on XP, assuming this is the case on W2K3 for now. */
        supR3HardNtViCallWinVerifyTrust(NULL, L"\\SystemRoot\\System32\\ntdll.dll", 0, NULL, pfnWinVerifyTrust, NULL);
    supR3HardNtViCallWinVerifyTrustCatFile(NULL, L"\\SystemRoot\\System32\\ntdll.dll", 0, NULL, pfnWinVerifyTrust);

    g_pfnWinVerifyTrust = pfnWinVerifyTrust;
    SUP_DPRINTF(("g_pfnWinVerifyTrust=%p\n", pfnWinVerifyTrust));

# ifdef IN_SUP_HARDENED_R3
    /*
     * Load some problematic DLLs into the verifier cache to prevent
     * recursion trouble.
     */
    supR3HardenedWinVerifyCachePreload(L"\\SystemRoot\\System32\\crypt32.dll");
    supR3HardenedWinVerifyCachePreload(L"\\SystemRoot\\System32\\Wintrust.dll");
# endif

    /*
     * Now, get trusted root CAs so we can verify a broader scope of signatures.
     */
    supR3HardenedWinRetrieveTrustedRootCAs();
}


static int supR3HardNtViNtToWinPath(PCRTUTF16 pwszNtName, PCRTUTF16 *ppwszWinPath,
                                    PRTUTF16 pwszWinPathBuf, size_t cwcWinPathBuf)
{
    static const RTUTF16 s_wszPrefix[] = L"\\\\.\\GLOBALROOT";

    if (*pwszNtName != '\\' && *pwszNtName != '/')
        return VERR_PATH_DOES_NOT_START_WITH_ROOT;

    size_t cwcNtName = RTUtf16Len(pwszNtName);
    if (RT_ELEMENTS(s_wszPrefix) + cwcNtName > cwcWinPathBuf)
        return VERR_FILENAME_TOO_LONG;

    memcpy(pwszWinPathBuf, s_wszPrefix, sizeof(s_wszPrefix));
    memcpy(&pwszWinPathBuf[sizeof(s_wszPrefix) / sizeof(RTUTF16) - 1], pwszNtName, (cwcNtName + 1) * sizeof(RTUTF16));
    *ppwszWinPath = pwszWinPathBuf;
    return VINF_SUCCESS;
}


/**
 * Calls WinVerifyTrust to verify an PE image.
 *
 * @returns VBox status code.
 * @param   hFile               File handle to the executable file.
 * @param   pwszName            Full NT path to the DLL in question, used for
 *                              dealing with unsigned system dlls as well as for
 *                              error/logging.
 * @param   fFlags              Flags, SUPHNTVI_F_XXX.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 * @param   pfnWinVerifyTrust   Pointer to the API.
 * @param   phrcWinVerifyTrust  Where to WinVerifyTrust error status on failure,
 *                              optional.
 */
static int supR3HardNtViCallWinVerifyTrust(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PRTERRINFO pErrInfo,
                                           PFNWINVERIFYTRUST pfnWinVerifyTrust, HRESULT *phrcWinVerifyTrust)
{
    RT_NOREF1(fFlags);
    if (phrcWinVerifyTrust)
        *phrcWinVerifyTrust = S_OK;

    /*
     * Convert the name into a Windows name.
     */
    RTUTF16 wszWinPathBuf[MAX_PATH];
    PCRTUTF16 pwszWinPath;
    int rc = supR3HardNtViNtToWinPath(pwszName, &pwszWinPath, wszWinPathBuf, RT_ELEMENTS(wszWinPathBuf));
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "Bad path passed to supR3HardNtViCallWinVerifyTrust: rc=%Rrc '%ls'", rc, pwszName);

    /*
     * Construct input parameters and call the API.
     */
    WINTRUST_FILE_INFO FileInfo;
    RT_ZERO(FileInfo);
    FileInfo.cbStruct = sizeof(FileInfo);
    FileInfo.pcwszFilePath = pwszWinPath;
    FileInfo.hFile = hFile;

    GUID PolicyActionGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA TrustData;
    RT_ZERO(TrustData);
    TrustData.cbStruct = sizeof(TrustData);
    TrustData.fdwRevocationChecks = WTD_REVOKE_NONE;  /* Keep simple for now. */
    TrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    TrustData.dwUIChoice = WTD_UI_NONE;
    TrustData.dwProvFlags = 0;
    if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0))
        TrustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    else
        TrustData.dwProvFlags = WTD_REVOCATION_CHECK_NONE;
    TrustData.dwUnionChoice = WTD_CHOICE_FILE;
    TrustData.pFile = &FileInfo;

    HRESULT hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &PolicyActionGuid, &TrustData);
# ifdef DEBUG_bird /* TEMP HACK */
    if (hrc == CERT_E_EXPIRED)
        hrc = S_OK;
# endif
    if (hrc == S_OK)
        rc = VINF_SUCCESS;
    else
    {
        /*
         * Failed. Format a nice error message.
         */
# ifdef DEBUG_bird
        if (hrc != CERT_E_CHAINING /* Un-updated vistas, XPs, ++ */)
            __debugbreak();
# endif
        const char *pszErrConst = NULL;
        switch (hrc)
        {
            case TRUST_E_SYSTEM_ERROR:            pszErrConst = "TRUST_E_SYSTEM_ERROR";         break;
            case TRUST_E_NO_SIGNER_CERT:          pszErrConst = "TRUST_E_NO_SIGNER_CERT";       break;
            case TRUST_E_COUNTER_SIGNER:          pszErrConst = "TRUST_E_COUNTER_SIGNER";       break;
            case TRUST_E_CERT_SIGNATURE:          pszErrConst = "TRUST_E_CERT_SIGNATURE";       break;
            case TRUST_E_TIME_STAMP:              pszErrConst = "TRUST_E_TIME_STAMP";           break;
            case TRUST_E_BAD_DIGEST:              pszErrConst = "TRUST_E_BAD_DIGEST";           break;
            case TRUST_E_BASIC_CONSTRAINTS:       pszErrConst = "TRUST_E_BASIC_CONSTRAINTS";    break;
            case TRUST_E_FINANCIAL_CRITERIA:      pszErrConst = "TRUST_E_FINANCIAL_CRITERIA";   break;
            case TRUST_E_PROVIDER_UNKNOWN:        pszErrConst = "TRUST_E_PROVIDER_UNKNOWN";     break;
            case TRUST_E_ACTION_UNKNOWN:          pszErrConst = "TRUST_E_ACTION_UNKNOWN";       break;
            case TRUST_E_SUBJECT_FORM_UNKNOWN:    pszErrConst = "TRUST_E_SUBJECT_FORM_UNKNOWN"; break;
            case TRUST_E_SUBJECT_NOT_TRUSTED:     pszErrConst = "TRUST_E_SUBJECT_NOT_TRUSTED";  break;
            case TRUST_E_NOSIGNATURE:             pszErrConst = "TRUST_E_NOSIGNATURE";          break;
            case TRUST_E_FAIL:                    pszErrConst = "TRUST_E_FAIL";                 break;
            case TRUST_E_EXPLICIT_DISTRUST:       pszErrConst = "TRUST_E_EXPLICIT_DISTRUST";    break;
            case CERT_E_EXPIRED:                  pszErrConst = "CERT_E_EXPIRED";               break;
            case CERT_E_VALIDITYPERIODNESTING:    pszErrConst = "CERT_E_VALIDITYPERIODNESTING"; break;
            case CERT_E_ROLE:                     pszErrConst = "CERT_E_ROLE";                  break;
            case CERT_E_PATHLENCONST:             pszErrConst = "CERT_E_PATHLENCONST";          break;
            case CERT_E_CRITICAL:                 pszErrConst = "CERT_E_CRITICAL";              break;
            case CERT_E_PURPOSE:                  pszErrConst = "CERT_E_PURPOSE";               break;
            case CERT_E_ISSUERCHAINING:           pszErrConst = "CERT_E_ISSUERCHAINING";        break;
            case CERT_E_MALFORMED:                pszErrConst = "CERT_E_MALFORMED";             break;
            case CERT_E_UNTRUSTEDROOT:            pszErrConst = "CERT_E_UNTRUSTEDROOT";         break;
            case CERT_E_CHAINING:                 pszErrConst = "CERT_E_CHAINING";              break;
            case CERT_E_REVOKED:                  pszErrConst = "CERT_E_REVOKED";               break;
            case CERT_E_UNTRUSTEDTESTROOT:        pszErrConst = "CERT_E_UNTRUSTEDTESTROOT";     break;
            case CERT_E_REVOCATION_FAILURE:       pszErrConst = "CERT_E_REVOCATION_FAILURE";    break;
            case CERT_E_CN_NO_MATCH:              pszErrConst = "CERT_E_CN_NO_MATCH";           break;
            case CERT_E_WRONG_USAGE:              pszErrConst = "CERT_E_WRONG_USAGE";           break;
            case CERT_E_UNTRUSTEDCA:              pszErrConst = "CERT_E_UNTRUSTEDCA";           break;
            case CERT_E_INVALID_POLICY:           pszErrConst = "CERT_E_INVALID_POLICY";        break;
            case CERT_E_INVALID_NAME:             pszErrConst = "CERT_E_INVALID_NAME";          break;
            case CRYPT_E_FILE_ERROR:              pszErrConst = "CRYPT_E_FILE_ERROR";           break;
            case CRYPT_E_REVOKED:                 pszErrConst = "CRYPT_E_REVOKED";              break;
        }
        if (pszErrConst)
            rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_UNSUPPORTED_ARCH,
                               "WinVerifyTrust failed with hrc=%s on '%ls'", pszErrConst, pwszName);
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_UNSUPPORTED_ARCH,
                               "WinVerifyTrust failed with hrc=%Rhrc on '%ls'", hrc, pwszName);
        SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrust: WinVerifyTrust failed with %#x (%s) on '%ls'\n",
                     hrc, pszErrConst, pwszName));
        if (phrcWinVerifyTrust)
            *phrcWinVerifyTrust = hrc;
    }

    /* clean up state data. */
    TrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    FileInfo.hFile = NULL;
    hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &PolicyActionGuid, &TrustData);

    return rc;
}


/**
 * Calls WinVerifyTrust to verify an PE image via catalog files.
 *
 * @returns VBox status code.
 * @param   hFile               File handle to the executable file.
 * @param   pwszName            Full NT path to the DLL in question, used for
 *                              dealing with unsigned system dlls as well as for
 *                              error/logging.
 * @param   fFlags              Flags, SUPHNTVI_F_XXX.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 * @param   pfnWinVerifyTrust   Pointer to the API.
 */
static int supR3HardNtViCallWinVerifyTrustCatFile(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, PRTERRINFO pErrInfo,
                                                  PFNWINVERIFYTRUST pfnWinVerifyTrust)
{
    RT_NOREF1(fFlags);
    SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: hFile=%p pwszName=%ls\n", hFile, pwszName));

    /*
     * Convert the name into a Windows name.
     */
    RTUTF16 wszWinPathBuf[MAX_PATH];
    PCRTUTF16 pwszWinPath;
    int rc = supR3HardNtViNtToWinPath(pwszName, &pwszWinPath, wszWinPathBuf, RT_ELEMENTS(wszWinPathBuf));
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "Bad path passed to supR3HardNtViCallWinVerifyTrustCatFile: rc=%Rrc '%ls'", rc, pwszName);

    /*
     * Open the file if we didn't get a handle.
     */
    HANDLE hFileClose = NULL;
    if (hFile == RTNT_INVALID_HANDLE_VALUE || hFile == NULL)
    {
        hFile = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

        UNICODE_STRING      NtName;
        NtName.Buffer = (PWSTR)pwszName;
        NtName.Length = (USHORT)(RTUtf16Len(pwszName) * sizeof(WCHAR));
        NtName.MaximumLength = NtName.Length + sizeof(WCHAR);

        OBJECT_ATTRIBUTES ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

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
            return RTErrInfoSetF(pErrInfo, RTErrConvertFromNtStatus(rcNt),
                                 "NtCreateFile returned %#x opening '%ls'.", rcNt, pwszName);
        hFileClose = hFile;
    }

    /*
     * On Windows 8.0 and later there are more than one digest choice.
     */
    int fNoSignedCatalogFound = -1;
    rc = VERR_LDRVI_NOT_SIGNED;
    static struct
    {
        /** The digest algorithm name. */
        const WCHAR        *pszAlgorithm;
        /** Cached catalog admin handle. */
        HCATADMIN volatile  hCachedCatAdmin;
    } s_aHashes[] =
    {
        { NULL,      NULL },
        { L"SHA256", NULL },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aHashes); i++)
    {
        /*
         * Another loop for dealing with different trust provider policies
         * required for successfully validating different catalog signatures.
         */
        bool                fTryNextPolicy;
        uint32_t            iPolicy = 0;
        static const GUID   s_aPolicies[] =
        {
            DRIVER_ACTION_VERIFY,              /* Works with microsoft bits. Most frequently used, thus first. */
            WINTRUST_ACTION_GENERIC_VERIFY_V2, /* Works with ATI and other SPC kernel-code signed stuff. */
        };
        do
        {
            /*
             * Create a context.
             */
            fTryNextPolicy = false;
            bool fFreshContext = false;
            BOOL fRc;
            HCATADMIN hCatAdmin = ASMAtomicXchgPtr(&s_aHashes[i].hCachedCatAdmin, NULL);
            if (hCatAdmin)
            {
                SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: Cached context %p\n", hCatAdmin));
                fFreshContext = false;
                fRc = TRUE;
            }
            else
            {
l_fresh_context:
                fFreshContext = true;
                if (g_pfnCryptCATAdminAcquireContext2)
                    fRc = g_pfnCryptCATAdminAcquireContext2(&hCatAdmin, &s_aPolicies[iPolicy], s_aHashes[i].pszAlgorithm,
                                                            NULL /*pStrongHashPolicy*/, 0 /*dwFlags*/);
                else
                    fRc = g_pfnCryptCATAdminAcquireContext(&hCatAdmin, &s_aPolicies[iPolicy], 0 /*dwFlags*/);
                SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: New context %p\n", hCatAdmin));
            }
            if (fRc)
            {
                SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: hCatAdmin=%p\n", hCatAdmin));

                /*
                 * Hash the file.
                 */
                BYTE  abHash[SUPHARDNTVI_MAX_CAT_HASH_SIZE];
                DWORD cbHash = sizeof(abHash);
                if (g_pfnCryptCATAdminCalcHashFromFileHandle2)
                    fRc = g_pfnCryptCATAdminCalcHashFromFileHandle2(hCatAdmin, hFile, &cbHash, abHash, 0 /*dwFlags*/);
                else
                    fRc = g_pfnCryptCATAdminCalcHashFromFileHandle(hFile, &cbHash, abHash, 0 /*dwFlags*/);
                if (fRc)
                {
                    /* Produce a string version of it that we can pass to WinVerifyTrust. */
                    RTUTF16 wszDigest[SUPHARDNTVI_MAX_CAT_HASH_SIZE * 2 + 1];
                    int rc2 = RTUtf16PrintHexBytes(wszDigest, RT_ELEMENTS(wszDigest), abHash, cbHash, RTSTRPRINTHEXBYTES_F_UPPER);
                    if (RT_SUCCESS(rc2))
                    {
                        SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: cbHash=%u wszDigest=%ls\n", cbHash, wszDigest));

                        /*
                         * Enumerate catalog information that matches the hash.
                         */
                        uint32_t iCat = 0;
                        HCATINFO hCatInfoPrev = NULL;
                        do
                        {
                            /* Get the next match. */
                            HCATINFO hCatInfo = g_pfnCryptCATAdminEnumCatalogFromHash(hCatAdmin, abHash, cbHash, 0, &hCatInfoPrev);
                            if (!hCatInfo)
                            {
                                if (!fFreshContext)
                                {
                                    SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: Retrying with fresh context (CryptCATAdminEnumCatalogFromHash -> %u; iCat=%#x)\n", RtlGetLastWin32Error(), iCat));
                                    if (hCatInfoPrev != NULL)
                                        g_pfnCryptCATAdminReleaseCatalogContext(hCatAdmin, hCatInfoPrev, 0 /*dwFlags*/);
                                    g_pfnCryptCATAdminReleaseContext(hCatAdmin, 0 /*dwFlags*/);
                                    goto l_fresh_context;
                                }
                                ULONG ulErr = RtlGetLastWin32Error();
                                fNoSignedCatalogFound = ulErr == ERROR_NOT_FOUND && fNoSignedCatalogFound != 0;
                                if (iCat == 0)
                                    SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: CryptCATAdminEnumCatalogFromHash failed ERROR_NOT_FOUND (%u)\n", ulErr));
                                else if (iCat == 0)
                                    SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: CryptCATAdminEnumCatalogFromHash failed %u\n", ulErr));
                                break;
                            }
                            fNoSignedCatalogFound = 0;
                            Assert(hCatInfoPrev == NULL);
                            hCatInfoPrev = hCatInfo;

                            /*
                             * Call WinVerifyTrust.
                             */
                            CATALOG_INFO CatInfo;
                            CatInfo.cbStruct = sizeof(CatInfo);
                            CatInfo.wszCatalogFile[0] = '\0';
                            if (g_pfnCryptCATCatalogInfoFromContext(hCatInfo, &CatInfo, 0 /*dwFlags*/))
                            {
                                WINTRUST_CATALOG_INFO WtCatInfo;
                                RT_ZERO(WtCatInfo);
                                WtCatInfo.cbStruct              = sizeof(WtCatInfo);
                                WtCatInfo.dwCatalogVersion      = 0;
                                WtCatInfo.pcwszCatalogFilePath  = CatInfo.wszCatalogFile;
                                WtCatInfo.pcwszMemberTag        = wszDigest;
                                WtCatInfo.pcwszMemberFilePath   = pwszWinPath;
                                WtCatInfo.pbCalculatedFileHash  = abHash;
                                WtCatInfo.cbCalculatedFileHash  = cbHash;
                                WtCatInfo.pcCatalogContext      = NULL;

                                WINTRUST_DATA TrustData;
                                RT_ZERO(TrustData);
                                TrustData.cbStruct              = sizeof(TrustData);
                                TrustData.fdwRevocationChecks   = WTD_REVOKE_NONE;  /* Keep simple for now. */
                                TrustData.dwStateAction         = WTD_STATEACTION_VERIFY;
                                TrustData.dwUIChoice            = WTD_UI_NONE;
                                TrustData.dwProvFlags           = 0;
                                if (g_uNtVerCombined >= SUP_MAKE_NT_VER_SIMPLE(6, 0))
                                    TrustData.dwProvFlags       = WTD_CACHE_ONLY_URL_RETRIEVAL;
                                else
                                    TrustData.dwProvFlags       = WTD_REVOCATION_CHECK_NONE;
                                TrustData.dwUnionChoice         = WTD_CHOICE_CATALOG;
                                TrustData.pCatalog              = &WtCatInfo;

                                HRESULT hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &s_aPolicies[iPolicy], &TrustData);
                                SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: WinVerifyTrust => %#x; cat='%ls'; file='%ls'\n",
                                             hrc, CatInfo.wszCatalogFile, pwszName));

                                if (SUCCEEDED(hrc))
                                    rc = VINF_SUCCESS;
                                else if (hrc == TRUST_E_NOSIGNATURE)
                                { /* ignore because it's useless. */ }
                                else if (hrc == ERROR_INVALID_PARAMETER)
                                { /* This is returned if the given file isn't found in the catalog, it seems. */ }
                                else
                                {
                                    rc = RTErrInfoSetF(pErrInfo, VERR_SUP_VP_WINTRUST_CAT_FAILURE,
                                                       "WinVerifyTrust failed with hrc=%#x on '%ls' and .cat-file='%ls'.",
                                                       hrc, pwszWinPath, CatInfo.wszCatalogFile);
                                    fTryNextPolicy |= (hrc == CERT_E_UNTRUSTEDROOT);
                                }

                                /* clean up state data. */
                                TrustData.dwStateAction = WTD_STATEACTION_CLOSE;
                                hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &s_aPolicies[iPolicy], &TrustData);
                                Assert(SUCCEEDED(hrc));
                            }
                            else
                            {
                                rc = RTErrInfoSetF(pErrInfo, RTErrConvertFromWin32(RtlGetLastWin32Error()),
                                                   "CryptCATCatalogInfoFromContext failed: %d [file=%s]",
                                                   RtlGetLastWin32Error(), pwszName);
                                SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile: CryptCATCatalogInfoFromContext failed\n"));
                            }
                            iCat++;
                        } while (rc == VERR_LDRVI_NOT_SIGNED && iCat < 128);

                        if (hCatInfoPrev != NULL)
                            if (!g_pfnCryptCATAdminReleaseCatalogContext(hCatAdmin, hCatInfoPrev, 0 /*dwFlags*/))
                                AssertFailed();
                    }
                    else
                        rc = RTErrInfoSetF(pErrInfo, rc2, "RTUtf16PrintHexBytes failed: %Rrc", rc);
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, RTErrConvertFromWin32(RtlGetLastWin32Error()),
                                       "CryptCATAdminCalcHashFromFileHandle[2] failed: %d [file=%s]", RtlGetLastWin32Error(), pwszName);

                if (!ASMAtomicCmpXchgPtr(&s_aHashes[i].hCachedCatAdmin, hCatAdmin, NULL))
                    if (!g_pfnCryptCATAdminReleaseContext(hCatAdmin, 0 /*dwFlags*/))
                        AssertFailed();
            }
            else
                rc = RTErrInfoSetF(pErrInfo, RTErrConvertFromWin32(RtlGetLastWin32Error()),
                                   "CryptCATAdminAcquireContext[2] failed: %d [file=%s]", RtlGetLastWin32Error(), pwszName);
             iPolicy++;
        } while (   fTryNextPolicy
                 && iPolicy < RT_ELEMENTS(s_aPolicies));

        /*
         * Only repeat if we've got g_pfnCryptCATAdminAcquireContext2 and can specify the hash algorithm.
         */
        if (!g_pfnCryptCATAdminAcquireContext2)
            break;
        if (rc != VERR_LDRVI_NOT_SIGNED)
            break;
    }

    if (hFileClose != NULL)
        NtClose(hFileClose);

    /*
     * DLLs that are likely candidates for local modifications.
     */
    if (rc == VERR_LDRVI_NOT_SIGNED)
    {
        bool        fCoreSystemDll = false;
        PCRTUTF16   pwsz;
        uint32_t    cwcName  = (uint32_t)RTUtf16Len(pwszName);
        uint32_t    cwcOther = g_System32NtPath.UniStr.Length / sizeof(WCHAR);
        if (supHardViUtf16PathStartsWithEx(pwszName, cwcName, g_System32NtPath.UniStr.Buffer, cwcOther, true /*fCheckSlash*/))
        {
            pwsz = pwszName + cwcOther + 1;
            if (   supHardViUtf16PathIsEqual(pwsz, "uxtheme.dll")
                || supHardViUtf16PathIsEqual(pwsz, "user32.dll")
                || supHardViUtf16PathIsEqual(pwsz, "gdi32.dll")
                || supHardViUtf16PathIsEqual(pwsz, "opengl32.dll")
                || (fCoreSystemDll = supHardViUtf16PathIsEqual(pwsz, "KernelBase.dll"))
                || (fCoreSystemDll = supHardViUtf16PathIsEqual(pwsz, "kernel32.dll"))
                || (fCoreSystemDll = supHardViUtf16PathIsEqual(pwsz, "ntdll.dll"))
                )
            {
                if (RTErrInfoIsSet(pErrInfo))
                    RTErrInfoAdd(pErrInfo, rc, "\n");
                RTErrInfoAddF(pErrInfo, rc, "'%ls' is most likely modified.", pwszName);
            }
        }

        /* Kludge for ancient windows versions we don't want to support but
           users still wants to use.  Keep things as safe as possible without
           unnecessary effort.  Problem is that 3rd party catalog files cannot
           easily be found.  Showstopper for ATI users. */
        if (   fNoSignedCatalogFound == 1
            && g_uNtVerCombined < SUP_NT_VER_VISTA
            && !fCoreSystemDll)
        {
            rc = VINF_LDRVI_NOT_SIGNED;
        }
    }

    return rc;
}


/**
 * Verifies the given image using WinVerifyTrust in some way.
 *
 * This is used by supHardenedWinVerifyImageByLdrMod as well as
 * supR3HardenedScreenImage.
 *
 * @returns IPRT status code, modified @a rc.
 * @param   hFile               Handle of the file to verify.
 * @param   pwszName            Full NT path to the DLL in question, used for
 *                              dealing with unsigned system dlls as well as for
 *                              error/logging.
 * @param   fFlags              SUPHNTVI_F_XXX.
 * @param   rc                  The current status code.
 * @param   pfWinVerifyTrust    Where to return whether WinVerifyTrust was
 *                              actually used.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardenedWinVerifyImageTrust(HANDLE hFile, PCRTUTF16 pwszName, uint32_t fFlags, int rc,
                                               bool *pfWinVerifyTrust, PRTERRINFO pErrInfo)
{
    if (pfWinVerifyTrust)
        *pfWinVerifyTrust = false;

    /*
     * Call the windows verify trust API if we've resolved it and aren't in
     * some obvious recursion.
     */
    if (g_pfnWinVerifyTrust != NULL)
    {
        uint32_t const idCurrentThread = RTNtCurrentThreadId();

        /* Check if loader lock owner. */
        struct _RTL_CRITICAL_SECTION volatile *pLoaderLock = NtCurrentPeb()->LoaderLock;
        bool fOwnsLoaderLock = pLoaderLock
                            && pLoaderLock->OwningThread == (HANDLE)(uintptr_t)idCurrentThread
                            && pLoaderLock->RecursionCount > 0;
        if (!fOwnsLoaderLock)
        {
            /* Check for recursion. */
            bool fNoRecursion;
            if (g_iTlsWinVerifyTrustRecursion != UINT32_MAX)
            {
                fNoRecursion = TlsGetValue(g_iTlsWinVerifyTrustRecursion) == 0;
                if (fNoRecursion)
                    TlsSetValue(g_iTlsWinVerifyTrustRecursion, (void *)1);
            }
            else
                fNoRecursion = ASMAtomicCmpXchgU32(&g_idActiveThread, idCurrentThread, UINT32_MAX);

            if (fNoRecursion && !fOwnsLoaderLock)
            {
                /* We can call WinVerifyTrust. */
                if (pfWinVerifyTrust)
                    *pfWinVerifyTrust = true;

                if (rc != VERR_LDRVI_NOT_SIGNED)
                {
                    if (rc == VINF_LDRVI_NOT_SIGNED)
                    {
                        if (fFlags & SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION)
                        {
                            int rc2 = supR3HardNtViCallWinVerifyTrustCatFile(hFile, pwszName, fFlags, pErrInfo,
                                                                             g_pfnWinVerifyTrust);
                            SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile -> %d (org %d)\n", rc2, rc));
                            rc = rc2;
                        }
                        else
                        {
                            AssertFailed();
                            rc = VERR_LDRVI_NOT_SIGNED;
                        }
                    }
                    else if (RT_SUCCESS(rc))
                    {
                        HRESULT hrcWinVerifyTrust;
                        rc = supR3HardNtViCallWinVerifyTrust(hFile, pwszName, fFlags, pErrInfo, g_pfnWinVerifyTrust,
                                                             &hrcWinVerifyTrust);

                        /* DLLs signed with special roots, like "Microsoft Digital Media Authority 2005",
                           may fail here because the root cert is not in the normal certificate stores
                           (if any).  Our verification code has the basics of these certificates included
                           and can verify them, which is why we end up here instead of in the
                           VINF_LDRVI_NOT_SIGNED case above.  Current workaround is to do as above.
                           (Intel graphics driver DLLs, like igdusc64.dll. */
                        if (   RT_FAILURE(rc)
                            && hrcWinVerifyTrust == CERT_E_CHAINING
                            && (fFlags & SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION))
                        {
                            rc = supR3HardNtViCallWinVerifyTrustCatFile(hFile, pwszName, fFlags, pErrInfo, g_pfnWinVerifyTrust);
                            SUP_DPRINTF(("supR3HardNtViCallWinVerifyTrustCatFile -> %d (was CERT_E_CHAINING)\n", rc));
                        }
                    }
                    else
                    {
                        int rc2 = supR3HardNtViCallWinVerifyTrust(hFile, pwszName, fFlags, pErrInfo, g_pfnWinVerifyTrust, NULL);
                        AssertMsg(RT_FAILURE_NP(rc2),
                                  ("rc=%Rrc, rc2=%Rrc %s", rc, rc2, pErrInfo ? pErrInfo->pszMsg : "<no-err-info>"));
                        RT_NOREF_PV(rc2);
                    }
                }

                /* Unwind recursion. */
                if (g_iTlsWinVerifyTrustRecursion != UINT32_MAX)
                    TlsSetValue(g_iTlsWinVerifyTrustRecursion, (void *)0);
                else
                    ASMAtomicWriteU32(&g_idActiveThread, UINT32_MAX);
            }
            /*
             * No can do.
             */
            else
                SUP_DPRINTF(("Detected WinVerifyTrust recursion: rc=%Rrc '%ls'.\n", rc, pwszName));
        }
        else
            SUP_DPRINTF(("Detected loader lock ownership: rc=%Rrc '%ls'.\n", rc, pwszName));
    }
    return rc;
}


/**
 * Checks if WinVerifyTrust is callable on the current thread.
 *
 * Used by the main code to figure whether it makes sense to try revalidate an
 * image that hasn't passed thru WinVerifyTrust yet.
 *
 * @returns true if callable on current thread, false if not.
 */
DECLHIDDEN(bool) supHardenedWinIsWinVerifyTrustCallable(void)
{
    return g_pfnWinVerifyTrust != NULL
        && (   g_iTlsWinVerifyTrustRecursion != UINT32_MAX
            ?  (uintptr_t)TlsGetValue(g_iTlsWinVerifyTrustRecursion) == 0
            : g_idActiveThread != RTNtCurrentThreadId() );
}



/**
 * Initializes g_uNtVerCombined and g_NtVerInfo.
 * Called from suplibHardenedWindowsMain and suplibOsInit.
 */
DECLHIDDEN(void) supR3HardenedWinInitVersion(bool fEarly)
{
    /*
     * Get the windows version.  Use RtlGetVersion as GetVersionExW and
     * GetVersion might not be telling the whole truth (8.0 on 8.1 depending on
     * the application manifest).
     *
     * Note! Windows 10 build 14267+ touches BSS when calling RtlGetVersion, so we
     *       have to use the fallback for the call from the early init code.
     */
    OSVERSIONINFOEXW NtVerInfo;

    RT_ZERO(NtVerInfo);
    NtVerInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
    if (   fEarly
        || !NT_SUCCESS(RtlGetVersion((PRTL_OSVERSIONINFOW)&NtVerInfo)))
    {
        RT_ZERO(NtVerInfo);
        PPEB pPeb = NtCurrentPeb();
        NtVerInfo.dwMajorVersion = pPeb->OSMajorVersion;
        NtVerInfo.dwMinorVersion = pPeb->OSMinorVersion;
        NtVerInfo.dwBuildNumber  = pPeb->OSBuildNumber;
    }

    g_uNtVerCombined = SUP_MAKE_NT_VER_COMBINED(NtVerInfo.dwMajorVersion, NtVerInfo.dwMinorVersion, NtVerInfo.dwBuildNumber,
                                                NtVerInfo.wServicePackMajor, NtVerInfo.wServicePackMinor);
}

#endif /* IN_RING3 */

