/* $Id: localipc-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Local IPC, Windows Implementation Using Named Pipes.
 *
 * @note This code only works on W2K because of the dependency on
 *       ConvertStringSecurityDescriptorToSecurityDescriptor.
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
#define LOG_GROUP RTLOGGROUP_LOCALIPC
#include <iprt/nt/nt-and-windows.h> /* Need NtCancelIoFile and a few Rtl functions. */
#include <sddl.h>
#include <aclapi.h>

#include "internal/iprt.h"
#include <iprt/localipc.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/utf16.h>

#include "internal/magics.h"
#include "internal-r3-win.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Pipe prefix string. */
#define RTLOCALIPC_WIN_PREFIX   L"\\\\.\\pipe\\IPRT-"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Local IPC service instance, Windows.
 */
typedef struct RTLOCALIPCSERVERINT
{
    /** The magic (RTLOCALIPCSERVER_MAGIC). */
    uint32_t u32Magic;
    /** The creation flags. */
    uint32_t fFlags;
    /** Critical section protecting the structure. */
    RTCRITSECT CritSect;
    /** The number of references to the instance.
     * @remarks The reference counting isn't race proof. */
    uint32_t volatile cRefs;
    /** Indicates that there is a pending cancel request. */
    bool volatile fCancelled;
    /** The named pipe handle. */
    HANDLE hNmPipe;
    /** The handle to the event object we're using for overlapped I/O. */
    HANDLE hEvent;
    /** The overlapped I/O structure. */
    OVERLAPPED OverlappedIO;
    /** The full pipe name (variable length). */
    RTUTF16 wszName[1];
} RTLOCALIPCSERVERINT;
/** Pointer to a local IPC server instance (Windows). */
typedef RTLOCALIPCSERVERINT *PRTLOCALIPCSERVERINT;


/**
 * Local IPC session instance, Windows.
 *
 * This is a named pipe and we should probably merge the pipe code with this to
 * save work and code duplication.
 */
typedef struct RTLOCALIPCSESSIONINT
{
    /** The magic (RTLOCALIPCSESSION_MAGIC). */
    uint32_t            u32Magic;
    /** Critical section protecting the structure. */
    RTCRITSECT          CritSect;
    /** The number of references to the instance.
     * @remarks The reference counting isn't race proof. */
    uint32_t volatile   cRefs;
    /** Set if the zero byte read that the poll code using is pending. */
    bool                fZeroByteRead;
    /** Indicates that there is a pending cancel request. */
    bool volatile       fCancelled;
    /** Set if this is the server side, clear if the client. */
    bool                fServerSide;
    /** The named pipe handle. */
    HANDLE              hNmPipe;
    struct
    {
        RTTHREAD        hActiveThread;
        /** The handle to the event object we're using for overlapped I/O. */
        HANDLE          hEvent;
        /** The overlapped I/O structure. */
        OVERLAPPED      OverlappedIO;
    }
    /** Overlapped reads. */
                        Read,
    /** Overlapped writes. */
                        Write;
#if 0 /* Non-blocking writes are not yet supported. */
    /** Bounce buffer for writes. */
    uint8_t            *pbBounceBuf;
    /** Amount of used buffer space. */
    size_t              cbBounceBufUsed;
    /** Amount of allocated buffer space. */
    size_t              cbBounceBufAlloc;
#endif
    /** Buffer for the zero byte read.
     * Used in RTLocalIpcSessionWaitForData(). */
    uint8_t             abBuf[8];
} RTLOCALIPCSESSIONINT;
/** Pointer to a local IPC session instance (Windows). */
typedef RTLOCALIPCSESSIONINT *PRTLOCALIPCSESSIONINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtLocalIpcWinCreateSession(PRTLOCALIPCSESSIONINT *ppSession, HANDLE hNmPipeSession);


/**
 * DACL for block all network access and local users other than the creator/owner.
 *
 * ACE format: (ace_type;ace_flags;rights;object_guid;inherit_object_guid;account_sid)
 *
 * Note! FILE_GENERIC_WRITE (SDDL_FILE_WRITE) is evil here because it includes
 *       the FILE_CREATE_PIPE_INSTANCE(=FILE_APPEND_DATA) flag. Thus the hardcoded
 *       value 0x0012019b in the client ACE. The server-side still needs
 *       setting FILE_CREATE_PIPE_INSTANCE although.
 *       It expands to:
 *          0x00000001 - FILE_READ_DATA
 *          0x00000008 - FILE_READ_EA
 *          0x00000080 - FILE_READ_ATTRIBUTES
 *          0x00020000 - READ_CONTROL
 *          0x00100000 - SYNCHRONIZE
 *          0x00000002 - FILE_WRITE_DATA
 *          0x00000010 - FILE_WRITE_EA
 *          0x00000100 - FILE_WRITE_ATTRIBUTES
 *       =  0x0012019b (client)
 *       + (only for server):
 *          0x00000004 - FILE_CREATE_PIPE_INSTANCE
 *       =  0x0012019f
 *
 * @todo Triple check this!
 * @todo EVERYONE -> AUTHENTICATED USERS or something more appropriate?
 * @todo Have trouble allowing the owner FILE_CREATE_PIPE_INSTANCE access, so for now I'm hacking
 *       it just to get progress - the service runs as local system.
 *       The CREATOR OWNER and PERSONAL SELF works (the former is only involved in inheriting
 *       it seems, which is why it won't work. The latter I've no idea about. Perhaps the solution
 *       is to go the annoying route of OpenProcessToken, QueryTokenInformation,
 *          ConvertSidToStringSid and then use the result... Suggestions are very welcome
 */
#define RTLOCALIPC_WIN_SDDL_BASE \
        SDDL_DACL SDDL_DELIMINATOR \
        SDDL_ACE_BEGIN SDDL_ACCESS_DENIED L";;" SDDL_GENERIC_ALL L";;;" SDDL_NETWORK SDDL_ACE_END \
        SDDL_ACE_BEGIN SDDL_ACCESS_ALLOWED L";;" SDDL_FILE_ALL   L";;;" SDDL_LOCAL_SYSTEM SDDL_ACE_END
#define RTLOCALIPC_WIN_SDDL_SERVER \
        RTLOCALIPC_WIN_SDDL_BASE \
        SDDL_ACE_BEGIN SDDL_ACCESS_ALLOWED L";;" L"0x0012019f"   L";;;" SDDL_EVERYONE SDDL_ACE_END
#define RTLOCALIPC_WIN_SDDL_CLIENT \
        RTLOCALIPC_WIN_SDDL_BASE \
        SDDL_ACE_BEGIN SDDL_ACCESS_ALLOWED L";;" L"0x0012019b"   L";;;" SDDL_EVERYONE SDDL_ACE_END
static NTSTATUS rtLocalIpcBuildDacl(PACL pDacl, bool fServer)
{
    static SID_IDENTIFIER_AUTHORITY s_NtAuth    = SECURITY_NT_AUTHORITY;
    static SID_IDENTIFIER_AUTHORITY s_WorldAuth = SECURITY_WORLD_SID_AUTHORITY;
    union
    {
        SID     Sid;
        uint8_t abPadding[SECURITY_MAX_SID_SIZE];
    } Network, LocalSystem, Everyone;


    /* 1. SDDL_ACCESS_DENIED L";;" SDDL_GENERIC_ALL L";;;" SDDL_NETWORK */
    NTSTATUS rcNt = RtlInitializeSid(&Network.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&Network.Sid, 0) = SECURITY_NETWORK_RID;

    rcNt = RtlAddAccessDeniedAce(pDacl, ACL_REVISION, GENERIC_ALL, &Network.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    /* 2. SDDL_ACCESS_ALLOWED L";;" SDDL_FILE_ALL   L";;;" SDDL_LOCAL_SYSTEM */
    rcNt = RtlInitializeSid(&LocalSystem.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&LocalSystem.Sid, 0) = SECURITY_LOCAL_SYSTEM_RID;

    rcNt = RtlAddAccessAllowedAce(pDacl, ACL_REVISION, FILE_ALL_ACCESS, &Network.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);


    /* 3. server: SDDL_ACCESS_ALLOWED L";;" L"0x0012019f"   L";;;" SDDL_EVERYONE
          client: SDDL_ACCESS_ALLOWED L";;" L"0x0012019b"   L";;;" SDDL_EVERYONE */
    rcNt = RtlInitializeSid(&Everyone.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&Everyone.Sid, 0) = SECURITY_WORLD_RID;

    DWORD const fAccess = FILE_READ_DATA                       /* 0x00000001 */
                        | FILE_WRITE_DATA                      /* 0x00000002 */
                        | FILE_CREATE_PIPE_INSTANCE * fServer  /* 0x00000004 */
                        | FILE_READ_EA                         /* 0x00000008 */
                        | FILE_WRITE_EA                        /* 0x00000010 */
                        | FILE_READ_ATTRIBUTES                 /* 0x00000080 */
                        | FILE_WRITE_ATTRIBUTES                /* 0x00000100 */
                        | READ_CONTROL                         /* 0x00020000 */
                        | SYNCHRONIZE;                         /* 0x00100000*/
    Assert(fAccess == (fServer ? 0x0012019fU : 0x0012019bU));

    rcNt = RtlAddAccessAllowedAce(pDacl, ACL_REVISION, fAccess, &Network.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    return true;
}


/**
 * Builds and allocates the security descriptor required for securing the local pipe.
 *
 * @return  IPRT status code.
 * @param   ppDesc              Where to store the allocated security descriptor on success.
 *                              Must be free'd using LocalFree().
 * @param   fServer             Whether it's for a server or client instance.
 */
static int rtLocalIpcServerWinAllocSecurityDescriptor(PSECURITY_DESCRIPTOR *ppDesc, bool fServer)
{
    int rc;
    PSECURITY_DESCRIPTOR pSecDesc = NULL;

#if 0
    /*
     * Resolve the API the first time around.
     */
    static bool volatile s_fResolvedApis = false;
    /** advapi32.dll API ConvertStringSecurityDescriptorToSecurityDescriptorW. */
    static decltype(ConvertStringSecurityDescriptorToSecurityDescriptorW) *s_pfnSSDLToSecDescW = NULL;

    if (!s_fResolvedApis)
    {
        s_pfnSSDLToSecDescW
            = (decltype(s_pfnSSDLToSecDescW))RTLdrGetSystemSymbol("advapi32.dll",
                                                                  "ConvertStringSecurityDescriptorToSecurityDescriptorW");
        ASMCompilerBarrier();
        s_fResolvedApis = true;
    }
    if (s_pfnSSDLToSecDescW)
    {
        /*
         * We'll create a security descriptor from a SDDL that denies
         * access to network clients (this is local IPC after all), it
         * makes some further restrictions to prevent non-authenticated
         * users from screwing around.
         */
        PCRTUTF16 pwszSDDL  = fServer ? RTLOCALIPC_WIN_SDDL_SERVER : RTLOCALIPC_WIN_SDDL_CLIENT;
        ULONG     cbSecDesc = 0;
        SetLastError(0);
        if (s_pfnSSDLToSecDescW(pwszSDDL, SDDL_REVISION_1, &pSecDesc, &cbSecDesc))
        {
            DWORD dwErr = GetLastError(); RT_NOREF(dwErr);
            AssertPtr(pSecDesc);
            *ppDesc = pSecDesc;
            return VINF_SUCCESS;
        }

        rc = RTErrConvertFromWin32(GetLastError());
    }
    else
#endif
    {
        /*
         * Manually construct the descriptor.
         *
         * This is a bit crude. The 8KB is probably 50+ times more than what we need.
         */
        uint32_t const cbAlloc = SECURITY_DESCRIPTOR_MIN_LENGTH * 2 + _8K;
        pSecDesc = LocalAlloc(LMEM_FIXED, cbAlloc);
        if (!pSecDesc)
            return VERR_NO_MEMORY;
        RT_BZERO(pSecDesc, cbAlloc);

        uint32_t const cbDacl = cbAlloc - SECURITY_DESCRIPTOR_MIN_LENGTH * 2;
        PACL const     pDacl  = (PACL)((uint8_t *)pSecDesc + SECURITY_DESCRIPTOR_MIN_LENGTH * 2);

        if (   InitializeSecurityDescriptor(pSecDesc, SECURITY_DESCRIPTOR_REVISION)
            && InitializeAcl(pDacl, cbDacl, ACL_REVISION))
        {
            if (rtLocalIpcBuildDacl(pDacl, fServer))
            {
                *ppDesc = pSecDesc;
                return VINF_SUCCESS;
            }
            rc = VERR_GENERAL_FAILURE;
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        LocalFree(pSecDesc);
    }
    return rc;
}


/**
 * Creates a named pipe instance.
 *
 * This is used by both RTLocalIpcServerCreate and RTLocalIpcServerListen.
 *
 * @return  IPRT status code.
 * @param   phNmPipe        Where to store the named pipe handle on success.
 *                          This will be set to INVALID_HANDLE_VALUE on failure.
 * @param   pwszPipeName    The named pipe name, full, UTF-16 encoded.
 * @param   fFirst          Set on the first call (from RTLocalIpcServerCreate),
 *                          otherwise clear. Governs the
 *                          FILE_FLAG_FIRST_PIPE_INSTANCE flag.
 */
static int rtLocalIpcServerWinCreatePipeInstance(PHANDLE phNmPipe, PCRTUTF16 pwszPipeName, bool fFirst)
{
    *phNmPipe = INVALID_HANDLE_VALUE;

    /*
     * Create a security descriptor blocking access to the pipe via network.
     */
    PSECURITY_DESCRIPTOR pSecDesc;
    int rc = rtLocalIpcServerWinAllocSecurityDescriptor(&pSecDesc, fFirst /* Server? */);
    if (RT_SUCCESS(rc))
    {
#if 0
        { /* Just for checking the security descriptor out in the debugger (!sd <addr> doesn't work): */
            DWORD dwRet = LookupSecurityDescriptorPartsW(NULL, NULL, NULL, NULL, NULL, NULL, pSecDesc);
            __debugbreak(); RT_NOREF(dwRet);

            PTRUSTEE_W          pOwner = NULL;
            PTRUSTEE_W          pGroup = NULL;
            ULONG               cAces  = 0;
            PEXPLICIT_ACCESS_W  paAces = NULL;
            ULONG               cAuditEntries = 0;
            PEXPLICIT_ACCESS_W  paAuditEntries = NULL;
            dwRet = LookupSecurityDescriptorPartsW(&pOwner, NULL, NULL, NULL, NULL, NULL, pSecDesc);
            dwRet = LookupSecurityDescriptorPartsW(NULL, &pGroup, NULL, NULL, NULL, NULL, pSecDesc);
            dwRet = LookupSecurityDescriptorPartsW(NULL, NULL, &cAces, &paAces, NULL, NULL, pSecDesc);
            dwRet = LookupSecurityDescriptorPartsW(NULL, NULL, NULL, NULL, &cAuditEntries, &paAuditEntries, pSecDesc);
            __debugbreak(); RT_NOREF(dwRet);
        }
#endif

        /*
         * Now, create the pipe.
         */
        SECURITY_ATTRIBUTES SecAttrs;
        SecAttrs.nLength              = sizeof(SECURITY_ATTRIBUTES);
        SecAttrs.lpSecurityDescriptor = pSecDesc;
        SecAttrs.bInheritHandle       = FALSE;

        DWORD fOpenMode = PIPE_ACCESS_DUPLEX
                        | PIPE_WAIT
                        | FILE_FLAG_OVERLAPPED;
        if (   fFirst
            && (   g_enmWinVer >= kRTWinOSType_XP
                || (   g_enmWinVer == kRTWinOSType_2K
                    && g_WinOsInfoEx.wServicePackMajor >= 2) ) )
            fOpenMode |= FILE_FLAG_FIRST_PIPE_INSTANCE; /* Introduced with W2K SP2 */

        HANDLE hNmPipe = CreateNamedPipeW(pwszPipeName,                  /* lpName */
                                          fOpenMode,                     /* dwOpenMode */
                                          PIPE_TYPE_BYTE,                /* dwPipeMode */
                                          PIPE_UNLIMITED_INSTANCES,      /* nMaxInstances */
                                          PAGE_SIZE,                     /* nOutBufferSize (advisory) */
                                          PAGE_SIZE,                     /* nInBufferSize (ditto) */
                                          30*1000,                       /* nDefaultTimeOut = 30 sec */
                                          &SecAttrs);                    /* lpSecurityAttributes */
        if (hNmPipe != INVALID_HANDLE_VALUE)
        {
#if 0 /* For checking access control stuff in windbg (doesn't work): */
            PSECURITY_DESCRIPTOR pSecDesc2 = NULL;
            PACL pDacl = NULL;
            DWORD dwRet;
            dwRet = GetSecurityInfo(hNmPipe, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pDacl, NULL, &pSecDesc2);
            PACL pSacl = NULL;
            dwRet = GetSecurityInfo(hNmPipe, SE_FILE_OBJECT, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &pSacl, &pSecDesc2);
            dwRet = GetSecurityInfo(hNmPipe, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION, NULL, NULL, &pDacl, &pSacl, &pSecDesc2);
            PSID pSidOwner = NULL;
            dwRet = GetSecurityInfo(hNmPipe, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &pSidOwner, NULL, NULL, NULL, &pSecDesc2);
            PSID pSidGroup = NULL;
            dwRet = GetSecurityInfo(hNmPipe, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, NULL, &pSidGroup, NULL, NULL, &pSecDesc2);
            __debugbreak();
            RT_NOREF(dwRet);
#endif
            *phNmPipe = hNmPipe;
            rc = VINF_SUCCESS;
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        LocalFree(pSecDesc);
    }
    return rc;
}


/**
 * Validates the user specified name.
 *
 * @returns IPRT status code.
 * @param   pszName         The name to validate.
 * @param   pcwcFullName    Where to return the UTF-16 length of the full name.
 * @param   fNative         Whether it's a native name or a portable name.
 */
static int rtLocalIpcWinValidateName(const char *pszName, size_t *pcwcFullName, bool fNative)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(*pszName, VERR_INVALID_NAME);

    if (!fNative)
    {
        size_t cwcName = RT_ELEMENTS(RTLOCALIPC_WIN_PREFIX) - 1;
        for (;;)
        {
            char ch = *pszName++;
            if (!ch)
                break;
            AssertReturn(!RT_C_IS_CNTRL(ch), VERR_INVALID_NAME);
            AssertReturn((unsigned)ch < 0x80, VERR_INVALID_NAME);
            AssertReturn(ch != '\\', VERR_INVALID_NAME);
            AssertReturn(ch != '/', VERR_INVALID_NAME);
            cwcName++;
        }
        *pcwcFullName = cwcName;
    }
    else
    {
        int rc = RTStrCalcUtf16LenEx(pszName, RTSTR_MAX, pcwcFullName);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Constructs the full pipe name as UTF-16.
 *
 * @returns IPRT status code.
 * @param   pszName         The user supplied name.  ASSUMES reasonable length
 *                          for now, so no long path prefixing needed.
 * @param   pwszFullName    The output buffer.
 * @param   cwcFullName     The output buffer size excluding the terminator.
 * @param   fNative         Whether the user supplied name is a native or
 *                          portable one.
 */
static int rtLocalIpcWinConstructName(const char *pszName, PRTUTF16 pwszFullName, size_t cwcFullName, bool fNative)
{
    if (!fNative)
    {
        static RTUTF16 const s_wszPrefix[] = RTLOCALIPC_WIN_PREFIX;
        Assert(cwcFullName * sizeof(RTUTF16) > sizeof(s_wszPrefix));
        memcpy(pwszFullName, s_wszPrefix, sizeof(s_wszPrefix));
        cwcFullName  -= RT_ELEMENTS(s_wszPrefix) - 1;
        pwszFullName += RT_ELEMENTS(s_wszPrefix) - 1;
    }
    return RTStrToUtf16Ex(pszName, RTSTR_MAX, &pwszFullName, cwcFullName + 1, NULL);
}


RTDECL(int) RTLocalIpcServerCreate(PRTLOCALIPCSERVER phServer, const char *pszName, uint32_t fFlags)
{
    /*
     * Validate parameters.
     */
    AssertPtrReturn(phServer, VERR_INVALID_POINTER);
    *phServer = NIL_RTLOCALIPCSERVER;
    AssertReturn(!(fFlags & ~RTLOCALIPC_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    size_t cwcFullName;
    int rc = rtLocalIpcWinValidateName(pszName, &cwcFullName, RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME));
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate and initialize the instance data.
         */
        size_t cbThis = RT_UOFFSETOF_DYN(RTLOCALIPCSERVERINT, wszName[cwcFullName + 1]);
        PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)RTMemAllocVar(cbThis);
        AssertReturn(pThis, VERR_NO_MEMORY);

        pThis->u32Magic   = RTLOCALIPCSERVER_MAGIC;
        pThis->cRefs      = 1; /* the one we return */
        pThis->fCancelled = false;

        rc = rtLocalIpcWinConstructName(pszName, pThis->wszName, cwcFullName, RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME));
        if (RT_SUCCESS(rc))
        {
            rc = RTCritSectInit(&pThis->CritSect);
            if (RT_SUCCESS(rc))
            {
                pThis->hEvent = CreateEvent(NULL /*lpEventAttributes*/, TRUE /*bManualReset*/,
                                            FALSE /*bInitialState*/, NULL /*lpName*/);
                if (pThis->hEvent != NULL)
                {
                    RT_ZERO(pThis->OverlappedIO);
                    pThis->OverlappedIO.Internal = STATUS_PENDING;
                    pThis->OverlappedIO.hEvent   = pThis->hEvent;

                    rc = rtLocalIpcServerWinCreatePipeInstance(&pThis->hNmPipe, pThis->wszName, true /* fFirst */);
                    if (RT_SUCCESS(rc))
                    {
                        *phServer = pThis;
                        return VINF_SUCCESS;
                    }

                    BOOL fRc = CloseHandle(pThis->hEvent);
                    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());

                int rc2 = RTCritSectDelete(&pThis->CritSect);
                AssertRC(rc2);
            }
        }
        RTMemFree(pThis);
    }
    return rc;
}


/**
 * Retains a reference to the server instance.
 *
 * @param   pThis               The server instance.
 */
DECLINLINE(void) rtLocalIpcServerRetain(PRTLOCALIPCSERVERINT pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2 && cRefs); NOREF(cRefs);
}


/**
 * Call when the reference count reaches 0.
 *
 * Caller owns the critsect.
 *
 * @returns VINF_OBJECT_DESTROYED
 * @param   pThis       The instance to destroy.
 */
DECL_NO_INLINE(static, int) rtLocalIpcServerWinDestroy(PRTLOCALIPCSERVERINT pThis)
{
    Assert(pThis->u32Magic == ~RTLOCALIPCSERVER_MAGIC);
    pThis->u32Magic = ~RTLOCALIPCSERVER_MAGIC;

    BOOL fRc = CloseHandle(pThis->hNmPipe);
    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    pThis->hNmPipe = INVALID_HANDLE_VALUE;

    fRc = CloseHandle(pThis->hEvent);
    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    pThis->hEvent = NULL;

    RTCritSectLeave(&pThis->CritSect);
    RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);
    return VINF_OBJECT_DESTROYED;
}


/**
 * Server instance destructor.
 *
 * @returns VINF_OBJECT_DESTROYED
 * @param   pThis               The server instance.
 */
DECL_NO_INLINE(static, int) rtLocalIpcServerDtor(PRTLOCALIPCSERVERINT pThis)
{
    RTCritSectEnter(&pThis->CritSect);
    return rtLocalIpcServerWinDestroy(pThis);
}


/**
 * Releases a reference to the server instance.
 *
 * @returns VINF_SUCCESS if only release, VINF_OBJECT_DESTROYED if destroyed.
 * @param   pThis               The server instance.
 */
DECLINLINE(int) rtLocalIpcServerRelease(PRTLOCALIPCSERVERINT pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        return rtLocalIpcServerDtor(pThis);
    return VINF_SUCCESS;
}


/**
 * Releases a reference to the server instance and leaves the critsect.
 *
 * @returns VINF_SUCCESS if only release, VINF_OBJECT_DESTROYED if destroyed.
 * @param   pThis               The server instance.
 */
DECLINLINE(int) rtLocalIpcServerReleaseAndUnlock(PRTLOCALIPCSERVERINT pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        return rtLocalIpcServerWinDestroy(pThis);
    return RTCritSectLeave(&pThis->CritSect);
}



RTDECL(int) RTLocalIpcServerDestroy(RTLOCALIPCSERVER hServer)
{
    /*
     * Validate input.
     */
    if (hServer == NIL_RTLOCALIPCSERVER)
        return VINF_SUCCESS;
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Cancel any thread currently busy using the server,
     * leaving the cleanup to it.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTLOCALIPCSERVER_MAGIC, RTLOCALIPCSERVER_MAGIC), VERR_WRONG_ORDER);

    RTCritSectEnter(&pThis->CritSect);

    /* Cancel everything. */
    ASMAtomicUoWriteBool(&pThis->fCancelled, true);
    if (pThis->cRefs > 1)
    {
        BOOL fRc = SetEvent(pThis->hEvent);
        AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    }

    return rtLocalIpcServerReleaseAndUnlock(pThis);
}


RTDECL(int) RTLocalIpcServerGrantGroupAccess(RTLOCALIPCSERVER hServer, RTGID gid)
{
    RT_NOREF_PV(hServer); RT_NOREF(gid);
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTLocalIpcServerListen(RTLOCALIPCSERVER hServer, PRTLOCALIPCSESSION phClientSession)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(phClientSession, VERR_INVALID_POINTER);

    /*
     * Enter the critsect before inspecting the object further.
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    rtLocalIpcServerRetain(pThis);
    if (!pThis->fCancelled)
    {
        ResetEvent(pThis->hEvent);

        RTCritSectLeave(&pThis->CritSect);

        /*
         * Try connect a client. We need to use overlapped I/O here because
         * of the cancellation done by RTLocalIpcServerCancel and RTLocalIpcServerDestroy.
         */
        SetLastError(NO_ERROR);
        BOOL fRc = ConnectNamedPipe(pThis->hNmPipe, &pThis->OverlappedIO);
        DWORD dwErr = fRc ? NO_ERROR : GetLastError();
        if (    !fRc
            &&  dwErr == ERROR_IO_PENDING)
        {
            WaitForSingleObject(pThis->hEvent, INFINITE);
            DWORD dwIgnored;
            fRc = GetOverlappedResult(pThis->hNmPipe, &pThis->OverlappedIO, &dwIgnored, FALSE /* bWait*/);
            dwErr = fRc ? NO_ERROR : GetLastError();
        }

        RTCritSectEnter(&pThis->CritSect);
        if (   !pThis->fCancelled /* Event signalled but not cancelled? */
            && pThis->u32Magic == RTLOCALIPCSERVER_MAGIC)
        {
            /*
             * Still alive, some error or an actual client.
             *
             * If it's the latter we'll have to create a new pipe instance that
             * replaces the current one for the server. The current pipe instance
             * will be assigned to the client session.
             */
            if (   fRc
                || dwErr == ERROR_PIPE_CONNECTED)
            {
                HANDLE hNmPipe;
                rc = rtLocalIpcServerWinCreatePipeInstance(&hNmPipe, pThis->wszName, false /* fFirst */);
                if (RT_SUCCESS(rc))
                {
                    HANDLE hNmPipeSession = pThis->hNmPipe; /* consumed */
                    pThis->hNmPipe = hNmPipe;
                    rc = rtLocalIpcWinCreateSession(phClientSession, hNmPipeSession);
                }
                else
                {
                    /*
                     * We failed to create a new instance for the server, disconnect
                     * the client and fail. Don't try service the client here.
                     */
                    fRc = DisconnectNamedPipe(pThis->hNmPipe);
                    AssertMsg(fRc, ("%d\n", GetLastError()));
                }
            }
            else
                rc = RTErrConvertFromWin32(dwErr);
        }
        else
        {
            /*
             * Cancelled.
             *
             * Cancel the overlapped io if it didn't complete (must be done
             * in the this thread) or disconnect the client.
             */
            Assert(pThis->fCancelled);
            if (    fRc
                ||  dwErr == ERROR_PIPE_CONNECTED)
                fRc = DisconnectNamedPipe(pThis->hNmPipe);
            else if (dwErr == ERROR_IO_PENDING)
            {
                IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                NTSTATUS rcNt = NtCancelIoFile(pThis->hNmPipe, &Ios);
                fRc = NT_SUCCESS(rcNt);
            }
            else
                fRc = TRUE;
            AssertMsg(fRc, ("%d\n", GetLastError()));
            rc = VERR_CANCELLED;
        }
    }
    else
    {
        /*pThis->fCancelled = false; - Terrible interface idea. Add API to clear fCancelled if ever required. */
        rc = VERR_CANCELLED;
    }
    rtLocalIpcServerReleaseAndUnlock(pThis);
    return rc;
}


RTDECL(int) RTLocalIpcServerCancel(RTLOCALIPCSERVER hServer)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Enter the critical section, then set the cancellation flag
     * and signal the event (to wake up anyone in/at WaitForSingleObject).
     */
    rtLocalIpcServerRetain(pThis);
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        ASMAtomicUoWriteBool(&pThis->fCancelled, true);

        BOOL fRc = SetEvent(pThis->hEvent);
        if (fRc)
            rc = VINF_SUCCESS;
        else
        {
            DWORD dwErr = GetLastError();
            AssertMsgFailed(("dwErr=%u\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }

        rtLocalIpcServerReleaseAndUnlock(pThis);
    }
    else
        rtLocalIpcServerRelease(pThis);
    return rc;
}


/**
 * Create a session instance for a new server client or a client connect.
 *
 * @returns IPRT status code.
 *
 * @param   ppSession       Where to store the session handle on success.
 * @param   hNmPipeSession  The named pipe handle if server calling,
 *                          INVALID_HANDLE_VALUE if client connect.  This will
 *                          be consumed by this session, meaning on failure to
 *                          create the session it will be closed.
 */
static int rtLocalIpcWinCreateSession(PRTLOCALIPCSESSIONINT *ppSession, HANDLE hNmPipeSession)
{
    AssertPtr(ppSession);

    /*
     * Allocate and initialize the session instance data.
     */
    int rc;
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic         = RTLOCALIPCSESSION_MAGIC;
        pThis->cRefs            = 1; /* our ref */
        pThis->fCancelled       = false;
        pThis->fZeroByteRead    = false;
        pThis->fServerSide      = hNmPipeSession != INVALID_HANDLE_VALUE;
        pThis->hNmPipe          = hNmPipeSession;
#if 0 /* Non-blocking writes are not yet supported. */
        pThis->pbBounceBuf      = NULL;
        pThis->cbBounceBufAlloc = 0;
        pThis->cbBounceBufUsed  = 0;
#endif
        rc = RTCritSectInit(&pThis->CritSect);
        if (RT_SUCCESS(rc))
        {
            pThis->Read.hEvent = CreateEvent(NULL /*lpEventAttributes*/, TRUE /*bManualReset*/,
                                             FALSE /*bInitialState*/, NULL /*lpName*/);
            if (pThis->Read.hEvent != NULL)
            {
                pThis->Read.OverlappedIO.Internal = STATUS_PENDING;
                pThis->Read.OverlappedIO.hEvent   = pThis->Read.hEvent;
                pThis->Read.hActiveThread         = NIL_RTTHREAD;

                pThis->Write.hEvent = CreateEvent(NULL /*lpEventAttributes*/, TRUE /*bManualReset*/,
                                                  FALSE /*bInitialState*/, NULL /*lpName*/);
                if (pThis->Write.hEvent != NULL)
                {
                    pThis->Write.OverlappedIO.Internal = STATUS_PENDING;
                    pThis->Write.OverlappedIO.hEvent   = pThis->Write.hEvent;
                    pThis->Write.hActiveThread         = NIL_RTTHREAD;

                    *ppSession = pThis;
                    return VINF_SUCCESS;
                }

                CloseHandle(pThis->Read.hEvent);
            }

            /* bail out */
            rc = RTErrConvertFromWin32(GetLastError());
            RTCritSectDelete(&pThis->CritSect);
        }
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    if (hNmPipeSession != INVALID_HANDLE_VALUE)
    {
        BOOL fRc = CloseHandle(hNmPipeSession);
        AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    }
    return rc;
}


RTDECL(int) RTLocalIpcSessionConnect(PRTLOCALIPCSESSION phSession, const char *pszName, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phSession, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTLOCALIPC_C_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    size_t cwcFullName;
    int rc = rtLocalIpcWinValidateName(pszName, &cwcFullName, RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a session (shared with server client session creation).
         */
        PRTLOCALIPCSESSIONINT pThis;
        rc = rtLocalIpcWinCreateSession(&pThis, INVALID_HANDLE_VALUE);
        if (RT_SUCCESS(rc))
        {
            /*
             * Try open the pipe.
             */
            PSECURITY_DESCRIPTOR pSecDesc;
            rc = rtLocalIpcServerWinAllocSecurityDescriptor(&pSecDesc, false /*fServer*/);
            if (RT_SUCCESS(rc))
            {
                PRTUTF16 pwszFullName = RTUtf16Alloc((cwcFullName + 1) * sizeof(RTUTF16));
                if (pwszFullName)
                    rc = rtLocalIpcWinConstructName(pszName, pwszFullName, cwcFullName,
                                                    RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME));
                else
                    rc = VERR_NO_UTF16_MEMORY;
                if (RT_SUCCESS(rc))
                {
                    SECURITY_ATTRIBUTES SecAttrs;
                    SecAttrs.nLength              = sizeof(SECURITY_ATTRIBUTES);
                    SecAttrs.lpSecurityDescriptor = pSecDesc;
                    SecAttrs.bInheritHandle       = FALSE;

                    /* The SECURITY_XXX flags are needed in order to prevent the server from impersonating with
                       this thread's security context (supported at least back to NT 3.51). See @bugref{9773}. */
                    HANDLE hPipe = CreateFileW(pwszFullName,
                                               GENERIC_READ | GENERIC_WRITE,
                                               0 /*no sharing*/,
                                               &SecAttrs,
                                               OPEN_EXISTING,
                                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                                               NULL /*no template handle*/);
                    if (hPipe != INVALID_HANDLE_VALUE)
                    {
                        pThis->hNmPipe = hPipe;

                        LocalFree(pSecDesc);
                        RTUtf16Free(pwszFullName);

                        /*
                         * We're done!
                         */
                        *phSession = pThis;
                        return VINF_SUCCESS;
                    }

                    rc = RTErrConvertFromWin32(GetLastError());
                }

                RTUtf16Free(pwszFullName);
                LocalFree(pSecDesc);
            }

            /* destroy the session handle. */
            CloseHandle(pThis->Read.hEvent);
            CloseHandle(pThis->Write.hEvent);
            RTCritSectDelete(&pThis->CritSect);

            RTMemFree(pThis);
        }
    }
    return rc;
}


/**
 * Cancells all pending I/O operations, forcing the methods to return with
 * VERR_CANCELLED (unless they've got actual data to return).
 *
 * Used by RTLocalIpcSessionCancel and RTLocalIpcSessionClose.
 *
 * @returns IPRT status code.
 * @param   pThis               The client session instance.
 */
static int rtLocalIpcWinCancel(PRTLOCALIPCSESSIONINT pThis)
{
    ASMAtomicUoWriteBool(&pThis->fCancelled, true);

    /*
     * Call NtCancelIoFile since this call cancels both read and write
     * oriented operations.
     */
    if (   pThis->fZeroByteRead
        || pThis->Read.hActiveThread != NIL_RTTHREAD
        || pThis->Write.hActiveThread != NIL_RTTHREAD)
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NtCancelIoFile(pThis->hNmPipe, &Ios);
    }

    /*
     * Set both event semaphores.
     */
    BOOL fRc = SetEvent(pThis->Read.hEvent);
    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    fRc = SetEvent(pThis->Write.hEvent);
    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);

    return VINF_SUCCESS;
}


/**
 * Retains a reference to the session instance.
 *
 * @param   pThis               The client session instance.
 */
DECLINLINE(void) rtLocalIpcSessionRetain(PRTLOCALIPCSESSIONINT pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2 && cRefs); NOREF(cRefs);
}


RTDECL(uint32_t) RTLocalIpcSessionRetain(RTLOCALIPCSESSION hSession)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2 && cRefs);
    return cRefs;
}


/**
 * Call when the reference count reaches 0.
 *
 * Caller owns the critsect.
 *
 * @returns VINF_OBJECT_DESTROYED
 * @param   pThis       The instance to destroy.
 */
DECL_NO_INLINE(static, int) rtLocalIpcSessionWinDestroy(PRTLOCALIPCSESSIONINT pThis)
{
    BOOL fRc = CloseHandle(pThis->hNmPipe);
    AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
    pThis->hNmPipe = INVALID_HANDLE_VALUE;

    fRc = CloseHandle(pThis->Write.hEvent);
    AssertMsg(fRc, ("%d\n", GetLastError()));
    pThis->Write.hEvent = NULL;

    fRc = CloseHandle(pThis->Read.hEvent);
    AssertMsg(fRc, ("%d\n", GetLastError()));
    pThis->Read.hEvent = NULL;

    int rc2 = RTCritSectLeave(&pThis->CritSect); AssertRC(rc2);
    RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);
    return VINF_OBJECT_DESTROYED;
}


/**
 * Releases a reference to the session instance and unlock it.
 *
 * @returns VINF_SUCCESS or VINF_OBJECT_DESTROYED as appropriate.
 * @param   pThis               The session instance.
 */
DECLINLINE(int) rtLocalIpcSessionReleaseAndUnlock(PRTLOCALIPCSESSIONINT pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        return rtLocalIpcSessionWinDestroy(pThis);

    int rc2 = RTCritSectLeave(&pThis->CritSect); AssertRC(rc2);
    Log(("rtLocalIpcSessionReleaseAndUnlock: %u refs left\n", cRefs));
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTLocalIpcSessionRelease(RTLOCALIPCSESSION hSession)
{
    if (hSession == NIL_RTLOCALIPCSESSION)
        return 0;

    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (cRefs)
        Log(("RTLocalIpcSessionRelease: %u refs left\n", cRefs));
    else
    {
        RTCritSectEnter(&pThis->CritSect);
        rtLocalIpcSessionWinDestroy(pThis);
    }
    return cRefs;
}


RTDECL(int) RTLocalIpcSessionClose(RTLOCALIPCSESSION hSession)
{
    /*
     * Validate input.
     */
    if (hSession == NIL_RTLOCALIPCSESSION)
        return VINF_SUCCESS;
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the instance, cancel all outstanding I/O and drop our reference.
     */
    RTCritSectEnter(&pThis->CritSect);
    rtLocalIpcWinCancel(pThis);
    return rtLocalIpcSessionReleaseAndUnlock(pThis);
}


/**
 * Handles WaitForSingleObject return value when waiting for a zero byte read.
 *
 * The zero byte read is started by the RTLocalIpcSessionWaitForData method and
 * left pending when the function times out.  This saves us the problem of
 * NtCancelIoFile messing with all active I/O operations and the trouble of
 * restarting the zero byte read the next time the method is called.  However
 * should RTLocalIpcSessionRead be called after a failed
 * RTLocalIpcSessionWaitForData call, the zero byte read will still be pending
 * and it must wait for it to complete before the OVERLAPPEDIO structure can be
 * reused.
 *
 * Thus, both functions will do WaitForSingleObject and share this routine to
 * handle the outcome.
 *
 * @returns IPRT status code.
 * @param   pThis               The session instance.
 * @param   rcWait              The WaitForSingleObject return code.
 */
static int rtLocalIpcWinGetZeroReadResult(PRTLOCALIPCSESSIONINT pThis, DWORD rcWait)
{
    int rc;
    DWORD cbRead = 42;
    if (rcWait == WAIT_OBJECT_0)
    {
        if (GetOverlappedResult(pThis->hNmPipe, &pThis->Read.OverlappedIO, &cbRead, !pThis->fCancelled /*fWait*/))
        {
            Assert(cbRead == 0);
            rc = VINF_SUCCESS;
            pThis->fZeroByteRead = false;
        }
        else if (pThis->fCancelled)
            rc = VERR_CANCELLED;
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
    {
        /* We try get the result here too, just in case we're lucky, but no waiting. */
        DWORD dwErr = GetLastError();
        if (GetOverlappedResult(pThis->hNmPipe, &pThis->Read.OverlappedIO, &cbRead, FALSE /*fWait*/))
        {
            Assert(cbRead == 0);
            rc = VINF_SUCCESS;
            pThis->fZeroByteRead = false;
        }
        else if (rcWait == WAIT_TIMEOUT)
            rc = VERR_TIMEOUT;
        else if (rcWait == WAIT_ABANDONED)
            rc = VERR_INVALID_HANDLE;
        else
            rc = RTErrConvertFromWin32(dwErr);
    }
    return rc;
}


RTDECL(int) RTLocalIpcSessionRead(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    /* pcbRead is optional. */

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (pThis->Read.hActiveThread == NIL_RTTHREAD)
        {
            pThis->Read.hActiveThread = RTThreadSelf();

            size_t cbTotalRead = 0;
            while (cbToRead > 0)
            {
                DWORD cbRead = 0;
                if (!pThis->fCancelled)
                {
                    /*
                     * Wait for pending zero byte read, if necessary.
                     * Note! It cannot easily be cancelled due to concurrent current writes.
                     */
                    if (!pThis->fZeroByteRead)
                    { /* likely */ }
                    else
                    {
                        RTCritSectLeave(&pThis->CritSect);
                        DWORD rcWait = WaitForSingleObject(pThis->Read.OverlappedIO.hEvent, RT_MS_1MIN);
                        RTCritSectEnter(&pThis->CritSect);

                        rc = rtLocalIpcWinGetZeroReadResult(pThis, rcWait);
                        if (RT_SUCCESS(rc) || rc == VERR_TIMEOUT)
                            continue;
                        break;
                    }

                    /*
                     * Kick of a an overlapped read.  It should return immediately if
                     * there is bytes in the buffer.  If not, we'll cancel it and see
                     * what we get back.
                     */
                    rc = ResetEvent(pThis->Read.OverlappedIO.hEvent); Assert(rc == TRUE);
                    RTCritSectLeave(&pThis->CritSect);

                    if (ReadFile(pThis->hNmPipe, pvBuf,
                                 cbToRead <= ~(DWORD)0 ? (DWORD)cbToRead : ~(DWORD)0,
                                 &cbRead, &pThis->Read.OverlappedIO))
                    {
                        RTCritSectEnter(&pThis->CritSect);
                        rc = VINF_SUCCESS;
                    }
                    else if (GetLastError() == ERROR_IO_PENDING)
                    {
                        WaitForSingleObject(pThis->Read.OverlappedIO.hEvent, INFINITE);

                        RTCritSectEnter(&pThis->CritSect);
                        if (GetOverlappedResult(pThis->hNmPipe, &pThis->Read.OverlappedIO, &cbRead, TRUE /*fWait*/))
                            rc = VINF_SUCCESS;
                        else
                        {
                            if (pThis->fCancelled)
                                rc = VERR_CANCELLED;
                            else
                                rc = RTErrConvertFromWin32(GetLastError());
                            break;
                        }
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        AssertMsgFailedBreak(("%Rrc\n", rc));
                    }
                }
                else
                {
                    rc = VERR_CANCELLED;
                    break;
                }

                /* Advance. */
                cbToRead    -= cbRead;
                cbTotalRead += cbRead;
                pvBuf     = (uint8_t *)pvBuf + cbRead;
            }

            if (pcbRead)
            {
                *pcbRead = cbTotalRead;
                if (   RT_FAILURE(rc)
                    && cbTotalRead
                    && rc != VERR_INVALID_POINTER)
                    rc = VINF_SUCCESS;
            }

            pThis->Read.hActiveThread = NIL_RTTHREAD;
        }
        else
            rc = VERR_WRONG_ORDER;
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


RTDECL(int) RTLocalIpcSessionReadNB(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);
    *pcbRead = 0;

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (pThis->Read.hActiveThread == NIL_RTTHREAD)
        {
            pThis->Read.hActiveThread = RTThreadSelf();

            for (;;)
            {
                DWORD cbRead = 0;
                if (!pThis->fCancelled)
                {
                    /*
                     * Wait for pending zero byte read, if necessary.
                     * Note! It cannot easily be cancelled due to concurrent current writes.
                     */
                    if (!pThis->fZeroByteRead)
                    { /* likely */ }
                    else
                    {
                        RTCritSectLeave(&pThis->CritSect);
                        DWORD rcWait = WaitForSingleObject(pThis->Read.OverlappedIO.hEvent, 0);
                        RTCritSectEnter(&pThis->CritSect);

                        rc = rtLocalIpcWinGetZeroReadResult(pThis, rcWait);
                        if (RT_SUCCESS(rc))
                            continue;

                        if (rc == VERR_TIMEOUT)
                            rc = VINF_TRY_AGAIN;
                        break;
                    }

                    /*
                     * Figure out how much we can read (cannot try and cancel here
                     * like in the anonymous pipe code).
                     */
                    DWORD cbAvailable;
                    if (PeekNamedPipe(pThis->hNmPipe, NULL, 0, NULL, &cbAvailable, NULL))
                    {
                        if (cbAvailable == 0 || cbToRead == 0)
                        {
                            *pcbRead = 0;
                            rc = VINF_TRY_AGAIN;
                            break;
                        }
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        break;
                    }
                    if (cbAvailable > cbToRead)
                        cbAvailable = (DWORD)cbToRead;

                    /*
                     * Kick of a an overlapped read.  It should return immediately, so we
                     * don't really need to leave the critsect here.
                     */
                    rc = ResetEvent(pThis->Read.OverlappedIO.hEvent); Assert(rc == TRUE);
                    if (ReadFile(pThis->hNmPipe, pvBuf, cbAvailable, &cbRead, &pThis->Read.OverlappedIO))
                    {
                        *pcbRead = cbRead;
                        rc = VINF_SUCCESS;
                    }
                    else if (GetLastError() == ERROR_IO_PENDING)
                    {
                        DWORD rcWait = WaitForSingleObject(pThis->Read.OverlappedIO.hEvent, 0);
                        if (rcWait == WAIT_TIMEOUT)
                        {
                            RTCritSectLeave(&pThis->CritSect);
                            rcWait = WaitForSingleObject(pThis->Read.OverlappedIO.hEvent, INFINITE);
                            RTCritSectEnter(&pThis->CritSect);
                        }
                        if (GetOverlappedResult(pThis->hNmPipe, &pThis->Read.OverlappedIO, &cbRead, TRUE /*fWait*/))
                        {
                            *pcbRead = cbRead;
                            rc = VINF_SUCCESS;
                        }
                        else
                        {
                            if (pThis->fCancelled)
                                rc = VERR_CANCELLED;
                            else
                                rc = RTErrConvertFromWin32(GetLastError());
                        }
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        AssertMsgFailedBreak(("%Rrc\n", rc));
                    }
                }
                else
                    rc = VERR_CANCELLED;
                break;
            }

            pThis->Read.hActiveThread = NIL_RTTHREAD;
        }
        else
            rc = VERR_WRONG_ORDER;
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


#if 0 /* Non-blocking writes are not yet supported. */
/**
 * Common worker for handling I/O completion.
 *
 * This is used by RTLocalIpcSessionClose and RTLocalIpcSessionWrite.
 *
 * @returns IPRT status code.
 * @param   pThis               The pipe instance handle.
 */
static int rtLocalIpcSessionWriteCheckCompletion(PRTLOCALIPCSESSIONINT pThis)
{
    int rc;
    DWORD rcWait = WaitForSingleObject(pThis->OverlappedIO.hEvent, 0);
    if (rcWait == WAIT_OBJECT_0)
    {
        DWORD cbWritten = 0;
        if (GetOverlappedResult(pThis->hNmPipe, &pThis->OverlappedIO, &cbWritten, TRUE))
        {
            for (;;)
            {
                if (cbWritten >= pThis->cbBounceBufUsed)
                {
                    pThis->fIOPending = false;
                    rc = VINF_SUCCESS;
                    break;
                }

                /* resubmit the remainder of the buffer - can this actually happen? */
                memmove(&pThis->pbBounceBuf[0], &pThis->pbBounceBuf[cbWritten], pThis->cbBounceBufUsed - cbWritten);
                rc = ResetEvent(pThis->OverlappedIO.hEvent); Assert(rc == TRUE);
                if (!WriteFile(pThis->hNmPipe, pThis->pbBounceBuf, (DWORD)pThis->cbBounceBufUsed,
                               &cbWritten, &pThis->OverlappedIO))
                {
                    DWORD dwErr = GetLastError();
                    if (dwErr == ERROR_IO_PENDING)
                        rc = VINF_TRY_AGAIN;
                    else
                    {
                        pThis->fIOPending = false;
                        if (dwErr == ERROR_NO_DATA)
                            rc = VERR_BROKEN_PIPE;
                        else
                            rc = RTErrConvertFromWin32(dwErr);
                    }
                    break;
                }
                Assert(cbWritten > 0);
            }
        }
        else
        {
            pThis->fIOPending = false;
            rc = RTErrConvertFromWin32(GetLastError());
        }
    }
    else if (rcWait == WAIT_TIMEOUT)
        rc = VINF_TRY_AGAIN;
    else
    {
        pThis->fIOPending = false;
        if (rcWait == WAIT_ABANDONED)
            rc = VERR_INVALID_HANDLE;
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    return rc;
}
#endif


RTDECL(int) RTLocalIpcSessionWrite(RTLOCALIPCSESSION hSession, const void *pvBuf, size_t cbToWrite)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbToWrite, VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (pThis->Write.hActiveThread == NIL_RTTHREAD)
        {
            pThis->Write.hActiveThread = RTThreadSelf();

            /*
             * Try write everything. No bounce buffering necessary.
             */
            size_t cbTotalWritten = 0;
            while (cbToWrite > 0)
            {
                DWORD cbWritten = 0;
                if (!pThis->fCancelled)
                {
                    BOOL fRc = ResetEvent(pThis->Write.OverlappedIO.hEvent); Assert(fRc == TRUE);
                    RTCritSectLeave(&pThis->CritSect);

                    DWORD const cbToWriteInThisIteration = cbToWrite <= ~(DWORD)0 ? (DWORD)cbToWrite : ~(DWORD)0;
                    fRc = WriteFile(pThis->hNmPipe, pvBuf, cbToWriteInThisIteration, &cbWritten, &pThis->Write.OverlappedIO);
                    if (fRc)
                        rc = VINF_SUCCESS;
                    else
                    {
                        DWORD dwErr = GetLastError();
                        if (dwErr == ERROR_IO_PENDING)
                        {
                            DWORD rcWait = WaitForSingleObject(pThis->Write.OverlappedIO.hEvent, INFINITE);
                            if (rcWait == WAIT_OBJECT_0)
                            {
                                if (GetOverlappedResult(pThis->hNmPipe, &pThis->Write.OverlappedIO, &cbWritten, TRUE /*fWait*/))
                                    rc = VINF_SUCCESS;
                                else
                                    rc = RTErrConvertFromWin32(GetLastError());
                            }
                            else if (rcWait == WAIT_TIMEOUT)
                                rc = VERR_TIMEOUT;
                            else if (rcWait == WAIT_ABANDONED)
                                rc = VERR_INVALID_HANDLE;
                            else
                                rc = RTErrConvertFromWin32(GetLastError());
                        }
                        else if (dwErr == ERROR_NO_DATA)
                            rc = VERR_BROKEN_PIPE;
                        else
                            rc = RTErrConvertFromWin32(dwErr);
                    }

                    if (cbWritten > cbToWriteInThisIteration) /* paranoia^3 */
                        cbWritten = cbToWriteInThisIteration;

                    RTCritSectEnter(&pThis->CritSect);
                    if (RT_FAILURE(rc))
                        break;
                }
                else
                {
                    rc = VERR_CANCELLED;
                    break;
                }

                /* Advance. */
                pvBuf           = (char const *)pvBuf + cbWritten;
                cbTotalWritten += cbWritten;
                cbToWrite      -= cbWritten;
            }

            pThis->Write.hActiveThread = NIL_RTTHREAD;
        }
        else
            rc = VERR_WRONG_ORDER;
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


RTDECL(int) RTLocalIpcSessionFlush(RTLOCALIPCSESSION hSession)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->Write.hActiveThread == NIL_RTTHREAD)
        {
            /* No flushing on Windows needed since RTLocalIpcSessionWrite will block until
             * all data was written (or an error occurred). */
            /** @todo r=bird: above comment is misinformed.
             *        Implement this as soon as we want an explicit asynchronous version of
             *        RTLocalIpcSessionWrite on Windows. */
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


RTDECL(int) RTLocalIpcSessionWaitForData(RTLOCALIPCSESSION hSession, uint32_t cMillies)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    uint64_t const msStart = RTTimeMilliTS();

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (pThis->Read.hActiveThread == NIL_RTTHREAD)
        {
            pThis->Read.hActiveThread = RTThreadSelf();

            /*
             * Wait loop.
             */
            for (unsigned iLoop = 0;; iLoop++)
            {
                /*
                 * Check for cancellation before we continue.
                 */
                if (!pThis->fCancelled)
                { /* likely */ }
                else
                {
                    rc = VERR_CANCELLED;
                    break;
                }

                /*
                 * Prep something we can wait on.
                 */
                HANDLE hWait = INVALID_HANDLE_VALUE;
                if (pThis->fZeroByteRead)
                    hWait = pThis->Read.OverlappedIO.hEvent;
                else
                {
                    /* Peek at the pipe buffer and see how many bytes it contains. */
                    DWORD cbAvailable;
                    if (   PeekNamedPipe(pThis->hNmPipe, NULL, 0, NULL, &cbAvailable, NULL)
                        && cbAvailable)
                    {
                        rc = VINF_SUCCESS;
                        break;
                    }

                    /* Start a zero byte read operation that we can wait on. */
                    if (cMillies == 0)
                    {
                        rc = VERR_TIMEOUT;
                        break;
                    }
                    BOOL fRc = ResetEvent(pThis->Read.OverlappedIO.hEvent); Assert(fRc == TRUE); NOREF(fRc);
                    DWORD cbRead = 0;
                    if (ReadFile(pThis->hNmPipe, pThis->abBuf, 0 /*cbToRead*/, &cbRead, &pThis->Read.OverlappedIO))
                    {
                        rc = VINF_SUCCESS;
                        if (iLoop > 10)
                            RTThreadYield();
                    }
                    else if (GetLastError() == ERROR_IO_PENDING)
                    {
                        pThis->fZeroByteRead = true;
                        hWait = pThis->Read.OverlappedIO.hEvent;
                    }
                    else
                        rc = RTErrConvertFromWin32(GetLastError());
                    if (RT_FAILURE(rc))
                        break;
                }

                /*
                 * Check for timeout.
                 */
                DWORD cMsMaxWait = INFINITE; /* (MSC maybe used uninitialized) */
                if (cMillies == RT_INDEFINITE_WAIT)
                    cMsMaxWait = INFINITE;
                else if (   hWait != INVALID_HANDLE_VALUE
                         || iLoop > 10)
                {
                    uint64_t cMsElapsed = RTTimeMilliTS() - msStart;
                    if (cMsElapsed <= cMillies)
                        cMsMaxWait = cMillies - (uint32_t)cMsElapsed;
                    else if (iLoop == 0)
                        cMsMaxWait = cMillies ? 1 : 0;
                    else
                    {
                        rc = VERR_TIMEOUT;
                        break;
                    }
                }

                /*
                 * Wait and collect the result.
                 */
                if (hWait != INVALID_HANDLE_VALUE)
                {
                    RTCritSectLeave(&pThis->CritSect);

                    DWORD rcWait = WaitForSingleObject(hWait, cMsMaxWait);

                    int rc2 = RTCritSectEnter(&pThis->CritSect);
                    AssertRC(rc2);

                    rc = rtLocalIpcWinGetZeroReadResult(pThis, rcWait);
                    break;
                }
            }

            pThis->Read.hActiveThread = NIL_RTTHREAD;
        }

        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


RTDECL(int) RTLocalIpcSessionCancel(RTLOCALIPCSESSION hSession)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Enter the critical section, then set the cancellation flag
     * and signal the event (to wake up anyone in/at WaitForSingleObject).
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        rc = rtLocalIpcWinCancel(pThis);
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


RTDECL(int) RTLocalIpcSessionQueryProcess(RTLOCALIPCSESSION hSession, PRTPROCESS pProcess)
{
    RT_NOREF_PV(hSession); RT_NOREF_PV(pProcess);
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTLocalIpcSessionQueryUserId(RTLOCALIPCSESSION hSession, PRTUID pUid)
{
    RT_NOREF_PV(hSession); RT_NOREF_PV(pUid);
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTLocalIpcSessionQueryGroupId(RTLOCALIPCSESSION hSession, PRTGID pGid)
{
    RT_NOREF_PV(hSession); RT_NOREF_PV(pGid);
    return VERR_NOT_SUPPORTED;
}

