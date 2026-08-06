/* $Id: localipc-win.cpp 114875 2026-08-06 21:35:39Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Local IPC, Windows Implementation Using Named Pipes.
 *
 * @note This code only works on W2K because of the dependency on
 *       ConvertStringSecurityDescriptorToSecurityDescriptor.
 */

/*
 * Copyright (C) 2008-2026 Oracle and/or its affiliates.
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

#include "internal/iprt.h"
#include <iprt/localipc.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/param.h>
#include <iprt/stackcheck.h>
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
/** Pipe prefix string in the caller's protected login-session namespace. */
#define RTLOCALIPC_WIN_USER_PREFIX L"\\\\.\\pipe\\LOCAL\\IPRT-"
/** Number of UTF-16 units in the hexadecimal session ID and trailing dash. */
#define RTLOCALIPC_WIN_SESSION_ID_CWC 9
/** Number of UTF-16 units in the hexadecimal logon LUID and trailing dash. */
#define RTLOCALIPC_WIN_LOGON_ID_CWC   17


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
    /** Set if the session must remain in the current Windows logon session. */
    bool                fRestricted;
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


/** Pointer to a GetNamedPipeClientProcessId or GetNamedPipeServerProcessId function. */
typedef BOOL (WINAPI *PFNRTLOCALIPCWINQUERYPIPEPROCESS)(HANDLE hPipe, PULONG pidProcess);
/** Pointer to a GetNamedPipeClientSessionId or GetNamedPipeServerSessionId function. */
typedef BOOL (WINAPI *PFNRTLOCALIPCWINQUERYPIPESESSION)(HANDLE hPipe, PULONG pidSession);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once structure for resolving the named pipe process query APIs. */
static RTONCE                               g_rtLocalIpcWinQueryProcessResolveOnce = RTONCE_INITIALIZER;
/** GetNamedPipeClientProcessId, introduced with Windows Vista. */
static PFNRTLOCALIPCWINQUERYPIPEPROCESS     g_pfnGetNamedPipeClientProcessId       = NULL;
/** GetNamedPipeServerProcessId, introduced with Windows Vista. */
static PFNRTLOCALIPCWINQUERYPIPEPROCESS     g_pfnGetNamedPipeServerProcessId       = NULL;
/** Init once structure for resolving the named pipe session query APIs. */
static RTONCE                               g_rtLocalIpcWinQuerySessionResolveOnce = RTONCE_INITIALIZER;
/** GetNamedPipeClientSessionId, introduced with Windows Vista. */
static PFNRTLOCALIPCWINQUERYPIPESESSION     g_pfnGetNamedPipeClientSessionId       = NULL;
/** GetNamedPipeServerSessionId, introduced with Windows Vista. */
static PFNRTLOCALIPCWINQUERYPIPESESSION     g_pfnGetNamedPipeServerSessionId       = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtLocalIpcWinCreateSession(PRTLOCALIPCSESSIONINT *ppSession, HANDLE hNmPipeSession, bool fRestricted);
static int rtLocalIpcWinVerifyUserSid(PSID pSid);


/** Queries the current process token's Windows session ID. */
static int rtLocalIpcWinQuerySelfSessionId(uint32_t *pidSession)
{
    AssertPtrReturn(pidSession, VERR_INVALID_POINTER);
    *pidSession = 0;

    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return RTErrConvertFromWin32(GetLastError());

    DWORD idSession = 0;
    DWORD cbSession = 0;
    BOOL const fRc = GetTokenInformation(hToken, TokenSessionId, &idSession, sizeof(idSession), &cbSession);
    DWORD const dwErr = fRc ? ERROR_SUCCESS : GetLastError();
    CloseHandle(hToken);
    if (!fRc)
        return RTErrConvertFromWin32(dwErr);
    AssertReturn(cbSession == sizeof(idSession), VERR_INVALID_PARAMETER);

    *pidSession = idSession;
    return VINF_SUCCESS;
}


/** Queries the current process token's Windows logon LUID. */
static int rtLocalIpcWinQuerySelfLogonId(uint32_t *pidLogonHigh, uint32_t *pidLogonLow)
{
    AssertPtrReturn(pidLogonHigh, VERR_INVALID_POINTER);
    AssertPtrReturn(pidLogonLow, VERR_INVALID_POINTER);
    *pidLogonHigh = 0;
    *pidLogonLow  = 0;

    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return RTErrConvertFromWin32(GetLastError());

    TOKEN_STATISTICS TokenStats;
    DWORD cbTokenStats = 0;
    BOOL const fRc = GetTokenInformation(hToken, TokenStatistics, &TokenStats, sizeof(TokenStats), &cbTokenStats);
    DWORD const dwErr = fRc ? ERROR_SUCCESS : GetLastError();
    CloseHandle(hToken);
    if (!fRc)
        return RTErrConvertFromWin32(dwErr);
    AssertReturn(cbTokenStats == sizeof(TokenStats), VERR_INVALID_PARAMETER);

    *pidLogonHigh = (uint32_t)TokenStats.AuthenticationId.HighPart;
    *pidLogonLow  = TokenStats.AuthenticationId.LowPart;
    return VINF_SUCCESS;
}


/**
 * Resolves the optional named pipe process query APIs.
 *
 * @returns IPRT status code.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtLocalIpcWinQueryProcessResolveOnce(void *pvUser)
{
    RT_NOREF(pvUser);

    g_pfnGetNamedPipeClientProcessId = (PFNRTLOCALIPCWINQUERYPIPEPROCESS)GetProcAddress(g_hModKernel32,
                                                                                       "GetNamedPipeClientProcessId");
    g_pfnGetNamedPipeServerProcessId = (PFNRTLOCALIPCWINQUERYPIPEPROCESS)GetProcAddress(g_hModKernel32,
                                                                                       "GetNamedPipeServerProcessId");
    if (   g_pfnGetNamedPipeClientProcessId
        && g_pfnGetNamedPipeServerProcessId)
        return VINF_SUCCESS;
    return VERR_NOT_SUPPORTED;
}


/**
 * Resolves the optional named pipe session query APIs.
 *
 * @returns IPRT status code.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtLocalIpcWinQuerySessionResolveOnce(void *pvUser)
{
    RT_NOREF(pvUser);

    g_pfnGetNamedPipeClientSessionId = (PFNRTLOCALIPCWINQUERYPIPESESSION)GetProcAddress(g_hModKernel32,
                                                                                       "GetNamedPipeClientSessionId");
    g_pfnGetNamedPipeServerSessionId = (PFNRTLOCALIPCWINQUERYPIPESESSION)GetProcAddress(g_hModKernel32,
                                                                                       "GetNamedPipeServerSessionId");
    if (   g_pfnGetNamedPipeClientSessionId
        && g_pfnGetNamedPipeServerSessionId)
        return VINF_SUCCESS;
    return VERR_NOT_SUPPORTED;
}


/**
 * Queries the user information carried by a Windows access token.
 *
 * @returns IPRT status code.
 * @param   hToken              The token to query.
 * @param   ppTokenUser         Where to return the allocated token user
 *                              information.  Free with RTMemTmpFree().
 */
static int rtLocalIpcWinQueryTokenUser(HANDLE hToken, PTOKEN_USER *ppTokenUser)
{
    AssertReturn(hToken != NULL && hToken != INVALID_HANDLE_VALUE, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppTokenUser, VERR_INVALID_POINTER);
    *ppTokenUser = NULL;

    DWORD cbTokenUser = 0;
    if (GetTokenInformation(hToken, TokenUser, NULL, 0, &cbTokenUser))
        return VERR_INTERNAL_ERROR;
    DWORD const dwErr = GetLastError();
    if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        return RTErrConvertFromWin32(dwErr);
    AssertReturn(cbTokenUser >= sizeof(TOKEN_USER), VERR_INVALID_PARAMETER);

    PTOKEN_USER pTokenUser = (PTOKEN_USER)RTMemTmpAlloc(cbTokenUser);
    if (!pTokenUser)
        return VERR_NO_TMP_MEMORY;
    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, cbTokenUser, &cbTokenUser))
    {
        int const rc = RTErrConvertFromWin32(GetLastError());
        RTMemTmpFree(pTokenUser);
        return rc;
    }
    if (!IsValidSid(pTokenUser->User.Sid))
    {
        RTMemTmpFree(pTokenUser);
        return VERR_INVALID_PARAMETER;
    }

    *ppTokenUser = pTokenUser;
    return VINF_SUCCESS;
}


/**
 * Queries the logon SID carried by a Windows access token.
 *
 * @returns IPRT status code.
 * @param   hToken              The token to query.
 * @param   ppLogonSid          Where to return the allocated logon SID.  Free
 *                              with RTMemTmpFree().
 */
static int rtLocalIpcWinQueryTokenLogonSid(HANDLE hToken, PSID *ppLogonSid)
{
    AssertReturn(hToken != NULL && hToken != INVALID_HANDLE_VALUE, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppLogonSid, VERR_INVALID_POINTER);
    *ppLogonSid = NULL;

    DWORD cbTokenGroups = 0;
    if (GetTokenInformation(hToken, TokenGroups, NULL, 0, &cbTokenGroups))
        return VERR_INTERNAL_ERROR;
    DWORD const dwErr = GetLastError();
    if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        return RTErrConvertFromWin32(dwErr);
    AssertReturn(cbTokenGroups >= sizeof(TOKEN_GROUPS), VERR_INVALID_PARAMETER);

    PTOKEN_GROUPS pTokenGroups = (PTOKEN_GROUPS)RTMemTmpAlloc(cbTokenGroups);
    if (!pTokenGroups)
        return VERR_NO_TMP_MEMORY;
    if (!GetTokenInformation(hToken, TokenGroups, pTokenGroups, cbTokenGroups, &cbTokenGroups))
    {
        int const rc = RTErrConvertFromWin32(GetLastError());
        RTMemTmpFree(pTokenGroups);
        return rc;
    }

    int rc = VERR_ACCESS_DENIED;
    for (DWORD i = 0; i < pTokenGroups->GroupCount; i++)
        if ((pTokenGroups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
        {
            PSID const pTokenLogonSid = pTokenGroups->Groups[i].Sid;
            if (IsValidSid(pTokenLogonSid))
            {
                DWORD const cbLogonSid = GetLengthSid(pTokenLogonSid);
                PSID const pLogonSid = (PSID)RTMemTmpAlloc(cbLogonSid);
                if (pLogonSid)
                {
                    if (CopySid(cbLogonSid, pLogonSid, pTokenLogonSid))
                    {
                        *ppLogonSid = pLogonSid;
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        RTMemTmpFree(pLogonSid);
                    }
                }
                else
                    rc = VERR_NO_TMP_MEMORY;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;
        }

    RTMemTmpFree(pTokenGroups);
    return rc;
}


/** Returns whether @a pSid is the LocalSystem SID. */
static bool rtLocalIpcWinIsLocalSystemSid(PSID pSid)
{
    AssertReturn(pSid && IsValidSid(pSid), false);

    static SID_IDENTIFIER_AUTHORITY s_NtAuth = SECURITY_NT_AUTHORITY;
    union
    {
        SID     Sid;
        uint8_t abPadding[SECURITY_MAX_SID_SIZE];
    } LocalSystem;

    NTSTATUS const rcNt = RtlInitializeSid(&LocalSystem.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), false);
    *RtlSubAuthoritySid(&LocalSystem.Sid, 0) = SECURITY_LOCAL_SYSTEM_RID;
    return EqualSid(pSid, &LocalSystem.Sid) != FALSE;
}


/** Returns whether @a dwErr means that a named pipe peer disconnected. */
static bool rtLocalIpcWinIsPeerGoneError(DWORD dwErr)
{
    return    dwErr == ERROR_BROKEN_PIPE
           || dwErr == ERROR_NO_DATA
           || dwErr == ERROR_PIPE_NOT_CONNECTED;
}


/** Verifies that the named pipe peer belongs to the current Windows session. */
static int rtLocalIpcWinVerifyPeerSession(HANDLE hPipe, bool fServerSide)
{
    AssertReturn(hPipe != NULL && hPipe != INVALID_HANDLE_VALUE, VERR_INVALID_HANDLE);

    uint32_t idSelfSession = 0;
    int rc = rtLocalIpcWinQuerySelfSessionId(&idSelfSession);
    if (RT_SUCCESS(rc))
    {
        rc = RTOnce(&g_rtLocalIpcWinQuerySessionResolveOnce, rtLocalIpcWinQuerySessionResolveOnce, NULL);
        if (RT_SUCCESS(rc))
        {
            ULONG idPeerSession = 0;
            BOOL const fRc = fServerSide
                           ? g_pfnGetNamedPipeClientSessionId(hPipe, &idPeerSession)
                           : g_pfnGetNamedPipeServerSessionId(hPipe, &idPeerSession);
            if (fRc)
                rc = idPeerSession == idSelfSession ? VINF_SUCCESS : VERR_ACCESS_DENIED;
            else
            {
                DWORD const dwErr = GetLastError();
                rc = fServerSide && rtLocalIpcWinIsPeerGoneError(dwErr) ? VERR_ACCESS_DENIED
                                                                       : RTErrConvertFromWin32(dwErr);
            }
        }
    }
    return rc;
}


/** Verifies that the named pipe owner is the current process token's user. */
static int rtLocalIpcWinVerifyPipeOwnerUser(HANDLE hPipe)
{
    AssertReturn(hPipe != NULL && hPipe != INVALID_HANDLE_VALUE, VERR_INVALID_HANDLE);

    /* GetSecurityInfo is unavailable on the NT 3.1 import baseline. */
    DWORD cbSecDesc = 0;
    if (GetKernelObjectSecurity(hPipe, OWNER_SECURITY_INFORMATION, NULL, 0, &cbSecDesc))
        return VERR_INTERNAL_ERROR;
    DWORD const dwErr = GetLastError();
    if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        return RTErrConvertFromWin32(dwErr);

    PSECURITY_DESCRIPTOR pSecDesc = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
    if (!pSecDesc)
        return VERR_NO_TMP_MEMORY;

    int rc;
    if (GetKernelObjectSecurity(hPipe, OWNER_SECURITY_INFORMATION, pSecDesc, cbSecDesc, &cbSecDesc))
    {
        PSID pOwner = NULL;
        BOOL fOwnerDefaulted = FALSE;
        if (GetSecurityDescriptorOwner(pSecDesc, &pOwner, &fOwnerDefaulted))
        {
            RT_NOREF(fOwnerDefaulted);
            rc = rtLocalIpcWinVerifyUserSid(pOwner);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    RTMemTmpFree(pSecDesc);
    return rc;
}


/** Verifies that @a pSid is the current process token's user SID. */
static int rtLocalIpcWinVerifyUserSid(PSID pSid)
{
    if (!pSid || !IsValidSid(pSid))
        return VERR_INVALID_PARAMETER;

    HANDLE hSelfToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hSelfToken))
        return RTErrConvertFromWin32(GetLastError());

    PTOKEN_USER pSelfTokenUser = NULL;
    int rc = rtLocalIpcWinQueryTokenUser(hSelfToken, &pSelfTokenUser);
    if (RT_SUCCESS(rc) && !EqualSid(pSid, pSelfTokenUser->User.Sid))
        rc = VERR_ACCESS_DENIED;

    RTMemTmpFree(pSelfTokenUser);
    CloseHandle(hSelfToken);
    return rc;
}


/**
 * DACL blocking network access while permitting the creating logon session
 * and LocalSystem.
 *
 * ACE format: (ace_type;ace_flags;rights;object_guid;inherit_object_guid;account_sid)
 *
 * Note! FILE_GENERIC_WRITE is evil here because it includes
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
 * @returns NT status code.
 * @param   pDacl               The initialized ACL to populate.
 * @param   pAccessSid          The creating process token's logon SID.
 * @param   fServer             Whether the ACL is for a server pipe end.
 */
static NTSTATUS rtLocalIpcBuildDacl(PACL pDacl, PSID pAccessSid, bool fServer)
{
    AssertReturn(pAccessSid && IsValidSid(pAccessSid), STATUS_INVALID_PARAMETER);

    static SID_IDENTIFIER_AUTHORITY s_NtAuth = SECURITY_NT_AUTHORITY;
    union
    {
        SID     Sid;
        uint8_t abPadding[SECURITY_MAX_SID_SIZE];
    } Network, LocalSystem, OwnerRights;


    /* 1. Deny all access from network logons. */
    NTSTATUS rcNt = RtlInitializeSid(&Network.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&Network.Sid, 0) = SECURITY_NETWORK_RID;

    rcNt = RtlAddAccessDeniedAce(pDacl, ACL_REVISION, GENERIC_ALL, &Network.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    /* 2. Suppress the account owner's implicit WRITE_DAC access. */
    static SID_IDENTIFIER_AUTHORITY s_CreatorAuth = SECURITY_CREATOR_SID_AUTHORITY;
    rcNt = RtlInitializeSid(&OwnerRights.Sid, &s_CreatorAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&OwnerRights.Sid, 0) = SECURITY_CREATOR_OWNER_RIGHTS_RID;

    rcNt = RtlAddAccessDeniedAce(pDacl, ACL_REVISION, WRITE_DAC | WRITE_OWNER, &OwnerRights.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    /* 3. Grant LocalSystem full access. */
    rcNt = RtlInitializeSid(&LocalSystem.Sid, &s_NtAuth, 1);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);
    *RtlSubAuthoritySid(&LocalSystem.Sid, 0) = SECURITY_LOCAL_SYSTEM_RID;

    rcNt = RtlAddAccessAllowedAce(pDacl, ACL_REVISION, FILE_ALL_ACCESS, &LocalSystem.Sid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    /* 4. Grant the creating logon session the access required by this pipe end. */
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

    rcNt = RtlAddAccessAllowedAce(pDacl, ACL_REVISION, fAccess, pAccessSid);
    AssertReturn(NT_SUCCESS(rcNt), rcNt);

    return STATUS_SUCCESS;
}


/**
 * Builds and allocates the security descriptor required for securing the local pipe.
 *
 * @return  IPRT status code.
 * @param   ppDesc              Where to store the allocated security descriptor on success.
 *                              Must be free'd using LocalFree().
 * @param   fServer             Whether it's for a server or client instance.
 * @param   fRestrictToUser     Whether to restrict access to the creating logon
 *                              session and LocalSystem.
 */
static int rtLocalIpcServerWinAllocSecurityDescriptor(PSECURITY_DESCRIPTOR *ppDesc, bool fServer,
                                                      bool fRestrictToUser)
{
    int rc;
    PSECURITY_DESCRIPTOR pSecDesc = NULL;

    if (!fRestrictToUser)
    {
        /*
         * Preserve the legacy descriptor behavior.  The initialized ACL was
         * historically not attached to the descriptor, so Windows selected
         * the creating token's default DACL.
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
            *ppDesc = pSecDesc;
            return VINF_SUCCESS;
        }

        rc = RTErrConvertFromWin32(GetLastError());
        LocalFree(pSecDesc);
        return rc;
    }

    {
        /*
         * Manually construct the descriptor.
         *
         * This is a bit crude. The 8KB is probably 50+ times more than what we need.
         */
        HANDLE hSelfToken = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hSelfToken))
            return RTErrConvertFromWin32(GetLastError());

        PTOKEN_USER pSelfTokenUser = NULL;
        rc = rtLocalIpcWinQueryTokenUser(hSelfToken, &pSelfTokenUser);
        PSID pSelfLogonSid = NULL;
        if (RT_SUCCESS(rc))
        {
            rc = rtLocalIpcWinQueryTokenLogonSid(hSelfToken, &pSelfLogonSid);
            if (   rc == VERR_ACCESS_DENIED
                && rtLocalIpcWinIsLocalSystemSid(pSelfTokenUser->User.Sid))
            {
                DWORD const cbSystemSid = GetLengthSid(pSelfTokenUser->User.Sid);
                pSelfLogonSid = (PSID)RTMemTmpAlloc(cbSystemSid);
                if (!pSelfLogonSid)
                    rc = VERR_NO_TMP_MEMORY;
                else if (CopySid(cbSystemSid, pSelfLogonSid, pSelfTokenUser->User.Sid))
                    rc = VINF_SUCCESS;
                else
                {
                    rc = RTErrConvertFromWin32(GetLastError());
                    RTMemTmpFree(pSelfLogonSid);
                    pSelfLogonSid = NULL;
                }
            }
        }
        CloseHandle(hSelfToken);
        if (RT_FAILURE(rc))
        {
            RTMemTmpFree(pSelfLogonSid);
            RTMemTmpFree(pSelfTokenUser);
            return rc;
        }

        DWORD const cbOwnerSid = GetLengthSid(pSelfTokenUser->User.Sid);
        AssertReturnStmt(cbOwnerSid > 0 && cbOwnerSid <= SECURITY_MAX_SID_SIZE,
                         RTMemTmpFree(pSelfLogonSid); RTMemTmpFree(pSelfTokenUser), VERR_INVALID_PARAMETER);
        DWORD const cbLogonSid = GetLengthSid(pSelfLogonSid);
        AssertReturnStmt(cbLogonSid > 0 && cbLogonSid <= SECURITY_MAX_SID_SIZE,
                         RTMemTmpFree(pSelfLogonSid); RTMemTmpFree(pSelfTokenUser), VERR_INVALID_PARAMETER);

        uint32_t const cbDacl  = _8K;
        uint32_t const cbAlloc = SECURITY_DESCRIPTOR_MIN_LENGTH * 2 + cbDacl + SECURITY_MAX_SID_SIZE * 2;
        pSecDesc = LocalAlloc(LMEM_FIXED, cbAlloc);
        if (!pSecDesc)
        {
            RTMemTmpFree(pSelfLogonSid);
            RTMemTmpFree(pSelfTokenUser);
            return VERR_NO_MEMORY;
        }
        RT_BZERO(pSecDesc, cbAlloc);

        PACL const     pDacl  = (PACL)((uint8_t *)pSecDesc + SECURITY_DESCRIPTOR_MIN_LENGTH * 2);
        PSID const     pOwner = (PSID)((uint8_t *)pDacl + cbDacl);
        PSID const     pAccessSid = (PSID)((uint8_t *)pOwner + SECURITY_MAX_SID_SIZE);

        if (   CopySid(SECURITY_MAX_SID_SIZE, pOwner, pSelfTokenUser->User.Sid)
            && CopySid(SECURITY_MAX_SID_SIZE, pAccessSid, pSelfLogonSid)
            && InitializeSecurityDescriptor(pSecDesc, SECURITY_DESCRIPTOR_REVISION)
            && InitializeAcl(pDacl, cbDacl, ACL_REVISION))
        {
            NTSTATUS rcNt = rtLocalIpcBuildDacl(pDacl, pAccessSid, fServer);
            if (NT_SUCCESS(rcNt))
            {
                rcNt = RtlSetDaclSecurityDescriptor(pSecDesc, TRUE /*fDaclPresent*/, pDacl, FALSE /*fDaclDefaulted*/);
                if (   NT_SUCCESS(rcNt)
                    && SetSecurityDescriptorOwner(pSecDesc, pOwner, FALSE /*bOwnerDefaulted*/))
                {
                    *ppDesc = pSecDesc;
                    RTMemTmpFree(pSelfLogonSid);
                    RTMemTmpFree(pSelfTokenUser);
                    return VINF_SUCCESS;
                }
                if (NT_SUCCESS(rcNt))
                    rc = RTErrConvertFromWin32(GetLastError());
                else
                    rc = RTErrConvertFromNtStatus(rcNt);
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        RTMemTmpFree(pSelfLogonSid);
        RTMemTmpFree(pSelfTokenUser);
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
 * @param   fFlags          The RTLOCALIPC_FLAGS_* used to create the server.
 * @param   fFirst          Set on the first call (from RTLocalIpcServerCreate),
 *                          otherwise clear. Governs the
 *                          FILE_FLAG_FIRST_PIPE_INSTANCE flag.
 */
static int rtLocalIpcServerWinCreatePipeInstance(PHANDLE phNmPipe, PCRTUTF16 pwszPipeName, uint32_t fFlags, bool fFirst)
{
    *phNmPipe = INVALID_HANDLE_VALUE;

    /*
     * Create the legacy descriptor or, when requested, one blocking network
     * and other-user access.
     */
    PSECURITY_DESCRIPTOR pSecDesc;
    int rc = rtLocalIpcServerWinAllocSecurityDescriptor(&pSecDesc, fFirst /* Server? */,
                                                        RT_BOOL(fFlags & RTLOCALIPC_FLAGS_RESTRICT_TO_USER));
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
 * @param   fRestrictToUser Whether portable names use the login-session
 *                          namespace.
 */
static int rtLocalIpcWinValidateName(const char *pszName, size_t *pcwcFullName, bool fNative, bool fRestrictToUser)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(*pszName, VERR_INVALID_NAME);

    if (!fNative)
    {
        size_t cwcName = fRestrictToUser ? RT_ELEMENTS(RTLOCALIPC_WIN_USER_PREFIX) - 1
                                         : RT_ELEMENTS(RTLOCALIPC_WIN_PREFIX) - 1;
        if (fRestrictToUser)
            cwcName += RTLOCALIPC_WIN_SESSION_ID_CWC + RTLOCALIPC_WIN_LOGON_ID_CWC;
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
 * @param   fRestrictToUser Whether portable names use the login-session
 *                          namespace.
 */
static int rtLocalIpcWinConstructName(const char *pszName, PRTUTF16 pwszFullName, size_t cwcFullName,
                                     bool fNative, bool fRestrictToUser)
{
    if (!fNative)
    {
        static RTUTF16 const s_wszPrefix[]     = RTLOCALIPC_WIN_PREFIX;
        static RTUTF16 const s_wszUserPrefix[] = RTLOCALIPC_WIN_USER_PREFIX;
        PCRTUTF16 const pwszPrefix = fRestrictToUser ? s_wszUserPrefix : s_wszPrefix;
        size_t const    cwcPrefix  = fRestrictToUser ? RT_ELEMENTS(s_wszUserPrefix) - 1
                                                    : RT_ELEMENTS(s_wszPrefix) - 1;
        Assert(cwcFullName > cwcPrefix);
        memcpy(pwszFullName, pwszPrefix, cwcPrefix * sizeof(RTUTF16));
        cwcFullName  -= cwcPrefix;
        pwszFullName += cwcPrefix;

        if (fRestrictToUser)
        {
            uint32_t idSession = 0;
            int rc = rtLocalIpcWinQuerySelfSessionId(&idSession);
            if (RT_FAILURE(rc))
                return rc;
            uint32_t idLogonHigh = 0;
            uint32_t idLogonLow  = 0;
            rc = rtLocalIpcWinQuerySelfLogonId(&idLogonHigh, &idLogonLow);
            if (RT_FAILURE(rc))
                return rc;
            AssertReturn(cwcFullName >= RTLOCALIPC_WIN_SESSION_ID_CWC, VERR_BUFFER_OVERFLOW);
            ssize_t const cwcSession = RTUtf16Printf(pwszFullName, RTLOCALIPC_WIN_SESSION_ID_CWC + 1,
                                                     "%08RX32-", idSession);
            AssertReturn(cwcSession == RTLOCALIPC_WIN_SESSION_ID_CWC, VERR_INTERNAL_ERROR);
            cwcFullName  -= RTLOCALIPC_WIN_SESSION_ID_CWC;
            pwszFullName += RTLOCALIPC_WIN_SESSION_ID_CWC;

            AssertReturn(cwcFullName >= RTLOCALIPC_WIN_LOGON_ID_CWC, VERR_BUFFER_OVERFLOW);
            ssize_t const cwcLogon = RTUtf16Printf(pwszFullName, RTLOCALIPC_WIN_LOGON_ID_CWC + 1,
                                                   "%08RX32%08RX32-", idLogonHigh, idLogonLow);
            AssertReturn(cwcLogon == RTLOCALIPC_WIN_LOGON_ID_CWC, VERR_INTERNAL_ERROR);
            cwcFullName  -= RTLOCALIPC_WIN_LOGON_ID_CWC;
            pwszFullName += RTLOCALIPC_WIN_LOGON_ID_CWC;
        }
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
    int rc = rtLocalIpcWinValidateName(pszName, &cwcFullName,
                                       RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME),
                                       RT_BOOL(fFlags & RTLOCALIPC_FLAGS_RESTRICT_TO_USER));
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate and initialize the instance data.
         */
        size_t cbThis = RT_UOFFSETOF_DYN(RTLOCALIPCSERVERINT, wszName[cwcFullName + 1]);
        PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)RTMemAllocVar(cbThis);
        AssertReturn(pThis, VERR_NO_MEMORY);

        pThis->u32Magic   = RTLOCALIPCSERVER_MAGIC;
        pThis->fFlags     = fFlags;
        pThis->cRefs      = 1; /* the one we return */
        pThis->fCancelled = false;

        rc = rtLocalIpcWinConstructName(pszName, pThis->wszName, cwcFullName,
                                        RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME),
                                        RT_BOOL(fFlags & RTLOCALIPC_FLAGS_RESTRICT_TO_USER));
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

                    rc = rtLocalIpcServerWinCreatePipeInstance(&pThis->hNmPipe, pThis->wszName, pThis->fFlags,
                                                               true /* fFirst */);
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
    RT_STACK_CHECK_RET_ADDR();

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
                rc = rtLocalIpcServerWinCreatePipeInstance(&hNmPipe, pThis->wszName, pThis->fFlags, false /* fFirst */);
                if (RT_SUCCESS(rc))
                {
                    HANDLE hNmPipeSession = pThis->hNmPipe; /* consumed */
                    pThis->hNmPipe = hNmPipe;
                    rc = rtLocalIpcWinCreateSession(phClientSession, hNmPipeSession,
                                                    RT_BOOL(pThis->fFlags & RTLOCALIPC_FLAGS_RESTRICT_TO_USER));
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
            else if (   (pThis->fFlags & RTLOCALIPC_FLAGS_RESTRICT_TO_USER)
                     && rtLocalIpcWinIsPeerGoneError(dwErr))
            {
                fRc = DisconnectNamedPipe(pThis->hNmPipe);
                DWORD const dwDisconnectErr = fRc ? ERROR_SUCCESS : GetLastError();
                if (fRc || rtLocalIpcWinIsPeerGoneError(dwDisconnectErr))
                    rc = VERR_TRY_AGAIN;
                else
                    rc = RTErrConvertFromWin32(dwDisconnectErr);
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
 * @param   fRestricted     Whether to enforce matching Windows session IDs.
 */
static int rtLocalIpcWinCreateSession(PRTLOCALIPCSESSIONINT *ppSession, HANDLE hNmPipeSession, bool fRestricted)
{
    AssertPtr(ppSession);

    int rc;
    if (fRestricted && hNmPipeSession != INVALID_HANDLE_VALUE)
    {
        rc = rtLocalIpcWinVerifyPeerSession(hNmPipeSession, true /*fServerSide*/);
        if (RT_FAILURE(rc))
        {
            BOOL const fRc = CloseHandle(hNmPipeSession);
            AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
            if (rc == VERR_ACCESS_DENIED)
                rc = VERR_TRY_AGAIN;
            return rc;
        }
    }

    /*
     * Allocate and initialize the session instance data.
     */
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic         = RTLOCALIPCSESSION_MAGIC;
        pThis->cRefs            = 1; /* our ref */
        pThis->fCancelled       = false;
        pThis->fZeroByteRead    = false;
        pThis->fServerSide      = hNmPipeSession != INVALID_HANDLE_VALUE;
        pThis->fRestricted      = fRestricted;
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
    int rc = rtLocalIpcWinValidateName(pszName, &cwcFullName,
                                       RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME),
                                       RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a session (shared with server client session creation).
         */
        PRTLOCALIPCSESSIONINT pThis;
        rc = rtLocalIpcWinCreateSession(&pThis, INVALID_HANDLE_VALUE,
                                        RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER));
        if (RT_SUCCESS(rc))
        {
            /*
             * Try open the pipe.
             */
            PSECURITY_DESCRIPTOR pSecDesc;
            rc = rtLocalIpcServerWinAllocSecurityDescriptor(&pSecDesc, false /*fServer*/, false /*fRestrictToUser*/);
            if (RT_SUCCESS(rc))
            {
                PRTUTF16 pwszFullName = RTUtf16Alloc((cwcFullName + 1) * sizeof(RTUTF16));
                if (pwszFullName)
                    rc = rtLocalIpcWinConstructName(pszName, pwszFullName, cwcFullName,
                                                    RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME),
                                                    RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER));
                else
                    rc = VERR_NO_UTF16_MEMORY;
                if (RT_SUCCESS(rc))
                {
                    SECURITY_ATTRIBUTES SecAttrs;
                    SecAttrs.nLength              = sizeof(SECURITY_ATTRIBUTES);
                    SecAttrs.lpSecurityDescriptor = pSecDesc;
                    SecAttrs.bInheritHandle       = FALSE;

                    /* Default to an anonymous security context.  Callers that explicitly need same-user
                       verification may permit the server to identify, but not impersonate, the client. */
                    DWORD const fSecurityQos = fFlags & RTLOCALIPC_C_FLAGS_ALLOW_IDENTIFICATION
                                             ? SECURITY_IDENTIFICATION : SECURITY_ANONYMOUS;
                    HANDLE hPipe = CreateFileW(pwszFullName,
                                               GENERIC_READ | GENERIC_WRITE,
                                               0 /*no sharing*/,
                                               &SecAttrs,
                                               OPEN_EXISTING,
                                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | fSecurityQos,
                                               NULL /*no template handle*/);
                    if (hPipe != INVALID_HANDLE_VALUE)
                    {
                        if (!(fFlags & RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER))
                            rc = VINF_SUCCESS;
                        else
                            rc = rtLocalIpcWinVerifyPeerSession(hPipe, false /*fServerSide*/);
                        if (   RT_SUCCESS(rc)
                            && (fFlags & RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER))
                            rc = rtLocalIpcWinVerifyPipeOwnerUser(hPipe);
                        if (RT_SUCCESS(rc))
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

                        BOOL const fRc = CloseHandle(hPipe);
                        AssertMsg(fRc, ("%d\n", GetLastError())); NOREF(fRc);
                    }
                    else
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
    RT_STACK_CHECK_RET_ADDR();
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
    RT_STACK_CHECK_RET_ADDR();

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
    RT_STACK_CHECK_RET_ADDR();

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
    RT_STACK_CHECK_RET_ADDR();
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
    RT_STACK_CHECK_RET_ADDR();

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
    RT_STACK_CHECK_RET_ADDR();

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
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    *pProcess = NIL_RTPROCESS;

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (!pThis->fCancelled)
        {
            rc = RTOnce(&g_rtLocalIpcWinQueryProcessResolveOnce, rtLocalIpcWinQueryProcessResolveOnce, NULL);
            if (RT_SUCCESS(rc))
            {
                ULONG idProcess = 0;
                BOOL const fRc = pThis->fServerSide
                               ? g_pfnGetNamedPipeClientProcessId(pThis->hNmPipe, &idProcess)
                               : g_pfnGetNamedPipeServerProcessId(pThis->hNmPipe, &idProcess);
                if (fRc)
                {
                    *pProcess = (RTPROCESS)idProcess;
                    rc = VINF_SUCCESS;
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }
        }
        else
            rc = VERR_CANCELLED;
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }

    return rc;
}


RTDECL(int) RTLocalIpcSessionVerifySameUser(RTLOCALIPCSESSION hSession)
{
    PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rtLocalIpcSessionRetain(pThis);
        if (!pThis->fCancelled)
        {
            if (pThis->fRestricted)
            {
                rc = rtLocalIpcWinVerifyPeerSession(pThis->hNmPipe, pThis->fServerSide);
                if (RT_FAILURE(rc))
                {
                    rtLocalIpcSessionReleaseAndUnlock(pThis);
                    return rc;
                }
            }

            if (pThis->fServerSide)
            {
                HANDLE hPeerToken = NULL;
                if (ImpersonateNamedPipeClient(pThis->hNmPipe))
                {
                    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE /*OpenAsSelf*/, &hPeerToken))
                        rc = VERR_ACCESS_DENIED;

                    if (!RevertToSelf())
                    {
                        DWORD const dwErr = GetLastError();
                        BOOL const fCleared = SetThreadToken(NULL, NULL);
                        AssertMsg(fCleared, ("SetThreadToken failed: %u (RevertToSelf: %u)\n", GetLastError(), dwErr));
                        RT_NOREF(fCleared);
                        rc = RTErrConvertFromWin32(dwErr);
                    }

                    if (RT_SUCCESS(rc))
                    {
                        PTOKEN_USER pPeerTokenUser = NULL;
                        rc = rtLocalIpcWinQueryTokenUser(hPeerToken, &pPeerTokenUser);
                        if (RT_SUCCESS(rc))
                            rc = rtLocalIpcWinVerifyUserSid(pPeerTokenUser->User.Sid);
                        RTMemTmpFree(pPeerTokenUser);
                    }
                }
                else
                    rc = VERR_ACCESS_DENIED;

                if (hPeerToken != NULL)
                    CloseHandle(hPeerToken);
            }
            else
                rc = rtLocalIpcWinVerifyPipeOwnerUser(pThis->hNmPipe);
        }
        else
            rc = VERR_CANCELLED;
        rtLocalIpcSessionReleaseAndUnlock(pThis);
    }
    return rc;
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
