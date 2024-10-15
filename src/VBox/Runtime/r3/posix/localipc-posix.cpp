/* $Id: localipc-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Local IPC Server & Client, Posix.
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
#define LOG_GROUP RTLOGGROUP_LOCALIPC
#include "internal/iprt.h"
#include <iprt/localipc.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/poll.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/path.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifndef RT_OS_OS2
# include <sys/poll.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef AF_LOCAL
# define AF_LOCAL AF_UNIX
#endif

#include "internal/magics.h"
#include "internal/path.h"
#include "internal/socket.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Local IPC service instance, POSIX.
 */
typedef struct RTLOCALIPCSERVERINT
{
    /** The magic (RTLOCALIPCSERVER_MAGIC). */
    uint32_t            u32Magic;
    /** The creation flags. */
    uint32_t            fFlags;
    /** Critical section protecting the structure. */
    RTCRITSECT          CritSect;
    /** The number of references to the instance. */
    uint32_t volatile   cRefs;
    /** Indicates that there is a pending cancel request. */
    bool volatile       fCancelled;
    /** The server socket. */
    RTSOCKET            hSocket;
    /** Thread currently listening for clients. */
    RTTHREAD            hListenThread;
    /** The name we bound the server to (native charset encoding). */
    struct sockaddr_un  Name;
} RTLOCALIPCSERVERINT;
/** Pointer to a local IPC server instance (POSIX). */
typedef RTLOCALIPCSERVERINT *PRTLOCALIPCSERVERINT;


/**
 * Local IPC session instance, POSIX.
 */
typedef struct RTLOCALIPCSESSIONINT
{
    /** The magic (RTLOCALIPCSESSION_MAGIC). */
    uint32_t            u32Magic;
    /** Critical section protecting the structure. */
    RTCRITSECT          CritSect;
    /** The number of references to the instance. */
    uint32_t volatile   cRefs;
    /** Indicates that there is a pending cancel request. */
    bool volatile       fCancelled;
    /** Set if this is the server side, clear if the client. */
    bool                fServerSide;
    /** The client socket. */
    RTSOCKET            hSocket;
    /** Thread currently doing read related activites. */
    RTTHREAD            hWriteThread;
    /** Thread currently doing write related activies. */
    RTTHREAD            hReadThread;
} RTLOCALIPCSESSIONINT;
/** Pointer to a local IPC session instance (Windows). */
typedef RTLOCALIPCSESSIONINT *PRTLOCALIPCSESSIONINT;


/** Local IPC name prefix for portable names. */
#define RTLOCALIPC_POSIX_NAME_PREFIX    "/tmp/.iprt-localipc-"


/**
 * Validates the user specified name.
 *
 * @returns IPRT status code.
 * @param   pszName             The name to validate.
 * @param   fNative             Whether it's a native name or a portable name.
 */
static int rtLocalIpcPosixValidateName(const char *pszName, bool fNative)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(*pszName, VERR_INVALID_NAME);

    if (!fNative)
    {
        for (;;)
        {
            char ch = *pszName++;
            if (!ch)
                break;
            AssertReturn(!RT_C_IS_CNTRL(ch), VERR_INVALID_NAME);
            AssertReturn((unsigned)ch < 0x80, VERR_INVALID_NAME);
            AssertReturn(ch != '\\', VERR_INVALID_NAME);
            AssertReturn(ch != '/', VERR_INVALID_NAME);
        }
    }
    else
    {
        int rc = RTStrValidateEncoding(pszName);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Constructs a local (unix) domain socket name.
 *
 * @returns IPRT status code.
 * @param   pAddr               The address structure to construct the name in.
 * @param   pcbAddr             Where to return the address size.
 * @param   pszName             The user specified name (valid).
 * @param   fNative             Whether it's a native name or a portable name.
 */
static int rtLocalIpcPosixConstructName(struct sockaddr_un *pAddr, uint8_t *pcbAddr, const char *pszName, bool fNative)
{
    const char *pszNativeName;
    int rc = rtPathToNative(&pszNativeName, pszName, NULL /*pszBasePath not support*/);
    if (RT_SUCCESS(rc))
    {
        size_t cchNativeName = strlen(pszNativeName);
        size_t cbFull = !fNative ? cchNativeName + sizeof(RTLOCALIPC_POSIX_NAME_PREFIX) : cchNativeName + 1;
        if (cbFull <= sizeof(pAddr->sun_path))
        {
            RT_ZERO(*pAddr);
#ifdef RT_OS_OS2 /* Size must be exactly right on OS/2. */
            *pcbAddr = sizeof(*pAddr);
#else
            *pcbAddr = RT_UOFFSETOF(struct sockaddr_un, sun_path) + (uint8_t)cbFull;
#endif
#ifdef HAVE_SUN_LEN_MEMBER
            pAddr->sun_len     = *pcbAddr;
#endif
            pAddr->sun_family  = AF_LOCAL;

            if (!fNative)
            {
                memcpy(pAddr->sun_path, RTLOCALIPC_POSIX_NAME_PREFIX, sizeof(RTLOCALIPC_POSIX_NAME_PREFIX) - 1);
                memcpy(&pAddr->sun_path[sizeof(RTLOCALIPC_POSIX_NAME_PREFIX) - 1], pszNativeName, cchNativeName + 1);
            }
            else
                memcpy(pAddr->sun_path, pszNativeName, cchNativeName + 1);
        }
        else
            rc = VERR_FILENAME_TOO_LONG;
        rtPathFreeNative(pszNativeName, pszName);
    }
    return rc;
}



RTDECL(int) RTLocalIpcServerCreate(PRTLOCALIPCSERVER phServer, const char *pszName, uint32_t fFlags)
{
    /*
     * Parameter validation.
     */
    AssertPtrReturn(phServer, VERR_INVALID_POINTER);
    *phServer = NIL_RTLOCALIPCSERVER;
    AssertReturn(!(fFlags & ~RTLOCALIPC_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    int rc = rtLocalIpcPosixValidateName(pszName, RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME));
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate memory for the instance and initialize it.
         */
        PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic      = RTLOCALIPCSERVER_MAGIC;
            pThis->fFlags        = fFlags;
            pThis->cRefs         = 1;
            pThis->fCancelled    = false;
            pThis->hListenThread = NIL_RTTHREAD;
            rc = RTCritSectInit(&pThis->CritSect);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create the local (unix) socket and bind to it.
                 */
                rc = rtSocketCreate(&pThis->hSocket, AF_LOCAL, SOCK_STREAM, 0 /*iProtocol*/, false /*fInheritable*/);
                if (RT_SUCCESS(rc))
                {
                    signal(SIGPIPE, SIG_IGN); /* Required on solaris, at least. */

                    uint8_t cbAddr;
                    rc = rtLocalIpcPosixConstructName(&pThis->Name, &cbAddr, pszName,
                                                      RT_BOOL(fFlags & RTLOCALIPC_FLAGS_NATIVE_NAME));
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtSocketBindRawAddr(pThis->hSocket, &pThis->Name, cbAddr);
                        if (rc == VERR_NET_ADDRESS_IN_USE)
                        {
                            unlink(pThis->Name.sun_path);
                            rc = rtSocketBindRawAddr(pThis->hSocket, &pThis->Name, cbAddr);
                        }
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtSocketListen(pThis->hSocket, 16);
                            if (RT_SUCCESS(rc))
                            {
                                LogFlow(("RTLocalIpcServerCreate: Created %p (%s)\n", pThis, pThis->Name.sun_path));
                                *phServer = pThis;
                                return VINF_SUCCESS;
                            }
                            unlink(pThis->Name.sun_path);
                        }
                    }
                    RTSocketRelease(pThis->hSocket);
                }
                RTCritSectDelete(&pThis->CritSect);
            }
            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    Log(("RTLocalIpcServerCreate: failed, rc=%Rrc\n", rc));
    return rc;
}


RTDECL(int) RTLocalIpcServerGrantGroupAccess(RTLOCALIPCSERVER hServer, RTGID gid)
{
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->Name.sun_path[0] != '\0', VERR_INVALID_STATE);

    if (chown(pThis->Name.sun_path, (uid_t)-1, gid) == 0)
    {
        if (chmod(pThis->Name.sun_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) == 0)
        {
            LogRel2(("RTLocalIpcServerGrantGroupAccess: IPC socket %s access has been granted to group %RTgid\n",
                     pThis->Name.sun_path, gid));
            return VINF_SUCCESS;
        }
        LogRel(("RTLocalIpcServerGrantGroupAccess: cannot grant IPC socket %s write permission to group %RTgid: errno=%d\n",
                pThis->Name.sun_path, gid, errno));
    }
    else
        LogRel(("RTLocalIpcServerGrantGroupAccess: cannot change IPC socket %s group ownership to %RTgid: errno=%d\n",
                pThis->Name.sun_path, gid, errno));
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTLocalIpcServerSetAccessMode(RTLOCALIPCSERVER hServer, RTFMODE fMode)
{
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->Name.sun_path[0] != '\0', VERR_INVALID_STATE);

    if (chmod(pThis->Name.sun_path, fMode & RTFS_UNIX_ALL_ACCESS_PERMS) == 0)
        return VINF_SUCCESS;

    return RTErrConvertFromErrno(errno);
}


/**
 * Retains a reference to the server instance.
 *
 * @returns
 * @param   pThis               The server instance.
 */
DECLINLINE(void) rtLocalIpcServerRetain(PRTLOCALIPCSERVERINT pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2 && cRefs); RT_NOREF_PV(cRefs);
}


/**
 * Server instance destructor.
 *
 * @returns VINF_OBJECT_DESTROYED
 * @param   pThis               The server instance.
 */
static int rtLocalIpcServerDtor(PRTLOCALIPCSERVERINT pThis)
{
    pThis->u32Magic = ~RTLOCALIPCSERVER_MAGIC;
    if (RTSocketRelease(pThis->hSocket) == 0)
        Log(("rtLocalIpcServerDtor: Released socket\n"));
    else
        Log(("rtLocalIpcServerDtor: Socket still has references (impossible?)\n"));
    RTCritSectDelete(&pThis->CritSect);
    unlink(pThis->Name.sun_path);
    RTMemFree(pThis);
    return VINF_OBJECT_DESTROYED;
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
 * The core of RTLocalIpcServerCancel, used by both the destroy and cancel APIs.
 *
 * @returns IPRT status code
 * @param   pThis               The server instance.
 */
static int rtLocalIpcServerCancel(PRTLOCALIPCSERVERINT pThis)
{
    RTCritSectEnter(&pThis->CritSect);
    pThis->fCancelled = true;
    Log(("rtLocalIpcServerCancel:\n"));
    if (pThis->hListenThread != NIL_RTTHREAD)
        RTThreadPoke(pThis->hListenThread);
    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
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
     * Invalidate the server, releasing the caller's reference to the instance
     * data and making sure any other thread in the listen API will wake up.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTLOCALIPCSERVER_MAGIC, RTLOCALIPCSERVER_MAGIC), VERR_WRONG_ORDER);

    rtLocalIpcServerCancel(pThis);
    return rtLocalIpcServerRelease(pThis);
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
     * Do the job.
     */
    rtLocalIpcServerRetain(pThis);
    rtLocalIpcServerCancel(pThis);
    rtLocalIpcServerRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTLocalIpcServerListen(RTLOCALIPCSERVER hServer, PRTLOCALIPCSESSION phClientSession)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSERVERINT pThis = (PRTLOCALIPCSERVERINT)hServer;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSERVER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Begin listening.
     */
    rtLocalIpcServerRetain(pThis);
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hListenThread == NIL_RTTHREAD)
        {
            pThis->hListenThread = RTThreadSelf();

            /*
             * The listening retry loop.
             */
            for (;;)
            {
                if (!pThis->fCancelled)
                {
                    rc = RTCritSectLeave(&pThis->CritSect);
                    AssertRCBreak(rc);

                    struct sockaddr_un  Addr;
                    size_t              cbAddr = sizeof(Addr);
                    RTSOCKET            hClient;
                    Log(("RTLocalIpcServerListen: Calling rtSocketAccept...\n"));
                    rc = rtSocketAccept(pThis->hSocket, &hClient, (struct sockaddr *)&Addr, &cbAddr);
                    Log(("RTLocalIpcServerListen: rtSocketAccept returns %Rrc.\n", rc));

                    int rc2 = RTCritSectEnter(&pThis->CritSect);
                    AssertRCBreakStmt(rc2, rc = RT_SUCCESS(rc) ? rc2 : rc);

                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Create a client session.
                         */
                        PRTLOCALIPCSESSIONINT pSession = (PRTLOCALIPCSESSIONINT)RTMemAllocZ(sizeof(*pSession));
                        if (pSession)
                        {
                            pSession->u32Magic      = RTLOCALIPCSESSION_MAGIC;
                            pSession->cRefs         = 1;
                            pSession->fCancelled    = false;
                            pSession->fServerSide   = true;
                            pSession->hSocket       = hClient;
                            pSession->hReadThread   = NIL_RTTHREAD;
                            pSession->hWriteThread  = NIL_RTTHREAD;
                            rc = RTCritSectInit(&pSession->CritSect);
                            if (RT_SUCCESS(rc))
                            {
                                Log(("RTLocalIpcServerListen: Returning new client session: %p\n", pSession));
                                *phClientSession = pSession;
                                break;
                            }

                            RTMemFree(pSession);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    else if (   rc != VERR_INTERRUPTED
                             && rc != VERR_TRY_AGAIN)
                        break;
                }
                else
                {
                    rc = VERR_CANCELLED;
                    break;
                }
            }

            pThis->hListenThread = NIL_RTTHREAD;
        }
        else
        {
            AssertFailed();
            rc = VERR_RESOURCE_BUSY;
        }
        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }
    rtLocalIpcServerRelease(pThis);

    Log(("RTLocalIpcServerListen: returns %Rrc\n", rc));
    return rc;
}


RTDECL(int) RTLocalIpcSessionConnect(PRTLOCALIPCSESSION phSession, const char *pszName, uint32_t fFlags)
{
    /*
     * Parameter validation.
     */
    AssertPtrReturn(phSession, VERR_INVALID_POINTER);
    *phSession = NIL_RTLOCALIPCSESSION;

    AssertReturn(!(fFlags & ~RTLOCALIPC_C_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    int rc = rtLocalIpcPosixValidateName(pszName, RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME));
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate memory for the instance and initialize it.
         */
        PRTLOCALIPCSESSIONINT pThis = (PRTLOCALIPCSESSIONINT)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic         = RTLOCALIPCSESSION_MAGIC;
            pThis->cRefs            = 1;
            pThis->fCancelled       = false;
            pThis->fServerSide      = false;
            pThis->hSocket          = NIL_RTSOCKET;
            pThis->hReadThread      = NIL_RTTHREAD;
            pThis->hWriteThread     = NIL_RTTHREAD;
            rc = RTCritSectInit(&pThis->CritSect);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create the local (unix) socket and try connect to the server.
                 */
                rc = rtSocketCreate(&pThis->hSocket, AF_LOCAL, SOCK_STREAM, 0 /*iProtocol*/, false /*fInheritable*/);
                if (RT_SUCCESS(rc))
                {
                    signal(SIGPIPE, SIG_IGN); /* Required on solaris, at least. */

                    struct sockaddr_un  Addr;
                    uint8_t             cbAddr;
                    rc = rtLocalIpcPosixConstructName(&Addr, &cbAddr, pszName, RT_BOOL(fFlags & RTLOCALIPC_C_FLAGS_NATIVE_NAME));
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtSocketConnectRaw(pThis->hSocket, &Addr, cbAddr);
                        if (RT_SUCCESS(rc))
                        {
                            *phSession = pThis;
                            Log(("RTLocalIpcSessionConnect: Returns new session %p\n", pThis));
                            return VINF_SUCCESS;
                        }
                    }
                    RTSocketRelease(pThis->hSocket);
                }
                RTCritSectDelete(&pThis->CritSect);
            }
            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    Log(("RTLocalIpcSessionConnect: returns %Rrc\n", rc));
    return rc;
}


/**
 * Retains a reference to the session instance.
 *
 * @param   pThis               The server instance.
 */
DECLINLINE(void) rtLocalIpcSessionRetain(PRTLOCALIPCSESSIONINT pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2 && cRefs); RT_NOREF_PV(cRefs);
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
 * Session instance destructor.
 *
 * @returns VINF_OBJECT_DESTROYED
 * @param   pThis               The server instance.
 */
static int rtLocalIpcSessionDtor(PRTLOCALIPCSESSIONINT pThis)
{
    pThis->u32Magic = ~RTLOCALIPCSESSION_MAGIC;
    if (RTSocketRelease(pThis->hSocket) == 0)
        Log(("rtLocalIpcSessionDtor: Released socket\n"));
    else
        Log(("rtLocalIpcSessionDtor: Socket still has references (impossible?)\n"));
    RTCritSectDelete(&pThis->CritSect);
    RTMemFree(pThis);
    return VINF_OBJECT_DESTROYED;
}


/**
 * Releases a reference to the session instance.
 *
 * @returns VINF_SUCCESS or VINF_OBJECT_DESTROYED as appropriate.
 * @param   pThis               The session instance.
 */
DECLINLINE(int) rtLocalIpcSessionRelease(PRTLOCALIPCSESSIONINT pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        return rtLocalIpcSessionDtor(pThis);
    Log(("rtLocalIpcSessionRelease: %u refs left\n", cRefs));
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
        rtLocalIpcSessionDtor(pThis);
    return cRefs;
}


/**
 * The core of RTLocalIpcSessionCancel, used by both the destroy and cancel APIs.
 *
 * @returns IPRT status code
 * @param   pThis               The session instance.
 */
static int rtLocalIpcSessionCancel(PRTLOCALIPCSESSIONINT pThis)
{
    RTCritSectEnter(&pThis->CritSect);
    pThis->fCancelled = true;
    Log(("rtLocalIpcSessionCancel:\n"));
    if (pThis->hReadThread != NIL_RTTHREAD)
        RTThreadPoke(pThis->hReadThread);
    if (pThis->hWriteThread != NIL_RTTHREAD)
        RTThreadPoke(pThis->hWriteThread);
    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


RTDECL(int) RTLocalIpcSessionClose(RTLOCALIPCSESSION hSession)
{
    /*
     * Validate input.
     */
    if (hSession == NIL_RTLOCALIPCSESSION)
        return VINF_SUCCESS;
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the session, releasing the caller's reference to the instance
     * data and making sure any other thread in the listen API will wake up.
     */
    Log(("RTLocalIpcSessionClose:\n"));

    rtLocalIpcSessionCancel(pThis);
    return rtLocalIpcSessionRelease(pThis);
}


RTDECL(int) RTLocalIpcSessionCancel(RTLOCALIPCSESSION hSession)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    rtLocalIpcSessionRetain(pThis);
    rtLocalIpcSessionCancel(pThis);
    rtLocalIpcSessionRelease(pThis);
    return VINF_SUCCESS;
}


/**
 * Checks if the socket has has a HUP condition after reading zero bytes.
 *
 * @returns true if HUP, false if no.
 * @param   pThis       The IPC session handle.
 */
static bool rtLocalIpcPosixHasHup(PRTLOCALIPCSESSIONINT pThis)
{
    int fdNative = RTSocketToNative(pThis->hSocket);

#if !defined(RT_OS_OS2) && !defined(RT_OS_SOLARIS)
    struct pollfd PollFd;
    RT_ZERO(PollFd);
    PollFd.fd      = fdNative;
    PollFd.events  = POLLHUP | POLLERR;
    if (poll(&PollFd, 1, 0) <= 0)
        return false;
    if (!(PollFd.revents & (POLLHUP | POLLERR)))
        return false;
#else  /* RT_OS_OS2 || RT_OS_SOLARIS */
    /*
     * OS/2:    No native poll, do zero byte send to check for EPIPE.
     * Solaris: We don't get POLLHUP.
     */
    uint8_t bDummy;
    ssize_t rcSend = send(fdNative, &bDummy, 0, 0);
    if (rcSend >= 0 || (errno != EPIPE && errno != ECONNRESET))
        return false;
#endif /* RT_OS_OS2 || RT_OS_SOLARIS */

    /*
     * We've established EPIPE.  Now make sure there aren't any last bytes to
     * read that came in between the recv made by the caller and the disconnect.
     */
    uint8_t bPeek;
    ssize_t rcRecv = recv(fdNative, &bPeek, 1, MSG_DONTWAIT | MSG_PEEK);
    return rcRecv <= 0;
}


RTDECL(int) RTLocalIpcSessionRead(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hReadThread == NIL_RTTHREAD)
        {
            pThis->hReadThread = RTThreadSelf();

            for (;;)
            {
                if (!pThis->fCancelled)
                {
                    rc = RTCritSectLeave(&pThis->CritSect);
                    AssertRCBreak(rc);

                    rc = RTSocketRead(pThis->hSocket, pvBuf, cbToRead, pcbRead);

                    /* Detect broken pipe. */
                    if (rc == VINF_SUCCESS)
                    {
                        if (!pcbRead || *pcbRead)
                        { /* likely */ }
                        else if (rtLocalIpcPosixHasHup(pThis))
                            rc = VERR_BROKEN_PIPE;
                    }
                    else if (rc == VERR_NET_CONNECTION_RESET_BY_PEER || rc == VERR_NET_SHUTDOWN)
                        rc = VERR_BROKEN_PIPE;

                    int rc2 = RTCritSectEnter(&pThis->CritSect);
                    AssertRCBreakStmt(rc2, rc = RT_SUCCESS(rc) ? rc2 : rc);

                    if (   rc == VERR_INTERRUPTED
                        || rc == VERR_TRY_AGAIN)
                        continue;
                }
                else
                    rc = VERR_CANCELLED;
                break;
            }

            pThis->hReadThread = NIL_RTTHREAD;
        }
        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    rtLocalIpcSessionRelease(pThis);
    return rc;
}


RTDECL(int) RTLocalIpcSessionReadNB(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hReadThread == NIL_RTTHREAD)
        {
            pThis->hReadThread = RTThreadSelf(); /* not really required, but whatever. */

            for (;;)
            {
                if (!pThis->fCancelled)
                {
                    rc = RTSocketReadNB(pThis->hSocket, pvBuf, cbToRead, pcbRead);

                    /* Detect broken pipe. */
                    if (rc == VINF_SUCCESS)
                    {
                        if (!pcbRead || *pcbRead)
                        { /* likely */ }
                        else if (rtLocalIpcPosixHasHup(pThis))
                            rc = VERR_BROKEN_PIPE;
                    }
                    else if (rc == VERR_NET_CONNECTION_RESET_BY_PEER || rc == VERR_NET_SHUTDOWN)
                        rc = VERR_BROKEN_PIPE;

                    if (rc == VERR_INTERRUPTED)
                        continue;
                }
                else
                    rc = VERR_CANCELLED;
                break;
            }

            pThis->hReadThread = NIL_RTTHREAD;
        }
        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    rtLocalIpcSessionRelease(pThis);
    return rc;
}


RTDECL(int) RTLocalIpcSessionWrite(RTLOCALIPCSESSION hSession, const void *pvBuf, size_t cbToWrite)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hWriteThread == NIL_RTTHREAD)
        {
            pThis->hWriteThread = RTThreadSelf();

            for (;;)
            {
                if (!pThis->fCancelled)
                {
                    rc = RTCritSectLeave(&pThis->CritSect);
                    AssertRCBreak(rc);

                    rc = RTSocketWrite(pThis->hSocket, pvBuf, cbToWrite);

                    int rc2 = RTCritSectEnter(&pThis->CritSect);
                    AssertRCBreakStmt(rc2, rc = RT_SUCCESS(rc) ? rc2 : rc);

                    if (   rc == VERR_INTERRUPTED
                        || rc == VERR_TRY_AGAIN)
                        continue;
                }
                else
                    rc = VERR_CANCELLED;
                break;
            }

            pThis->hWriteThread = NIL_RTTHREAD;
        }
        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    rtLocalIpcSessionRelease(pThis);
    return rc;
}


RTDECL(int) RTLocalIpcSessionFlush(RTLOCALIPCSESSION hSession)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * This is a no-op because apparently write doesn't return until the
     * result is read.  At least that's what the reply to a 2003-04-08 LKML
     * posting title "fsync() on unix domain sockets?" indicates.
     *
     * For conformity, make sure there isn't any active writes concurrent to this call.
     */
    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hWriteThread == NIL_RTTHREAD)
            rc = RTCritSectLeave(&pThis->CritSect);
        else
        {
            rc = RTCritSectLeave(&pThis->CritSect);
            if (RT_SUCCESS(rc))
                rc = VERR_RESOURCE_BUSY;
        }
    }

    rtLocalIpcSessionRelease(pThis);
    return rc;
}


RTDECL(int) RTLocalIpcSessionWaitForData(RTLOCALIPCSESSION hSession, uint32_t cMillies)
{
    /*
     * Validate input.
     */
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->hReadThread == NIL_RTTHREAD)
        {
            pThis->hReadThread = RTThreadSelf();
            uint64_t const msStart = RTTimeMilliTS();
            RTMSINTERVAL const cMsOriginalTimeout = cMillies;

            for (;;)
            {
                if (!pThis->fCancelled)
                {
                    rc = RTCritSectLeave(&pThis->CritSect);
                    AssertRCBreak(rc);

                    uint32_t fEvents = 0;
#ifdef RT_OS_OS2
                    /* This doesn't give us any error condition on hangup, so use HUP check. */
                    Log(("RTLocalIpcSessionWaitForData: Calling RTSocketSelectOneEx...\n"));
                    rc = RTSocketSelectOneEx(pThis->hSocket, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, &fEvents, cMillies);
                    Log(("RTLocalIpcSessionWaitForData: RTSocketSelectOneEx returns %Rrc, fEvents=%#x\n", rc, fEvents));
                    if (RT_SUCCESS(rc) && fEvents == RTPOLL_EVT_READ && rtLocalIpcPosixHasHup(pThis))
                        rc = VERR_BROKEN_PIPE;
#else
/** @todo RTSocketPoll? */
                    /* POLLHUP will be set on hangup. */
                    struct pollfd PollFd;
                    RT_ZERO(PollFd);
                    PollFd.fd      = RTSocketToNative(pThis->hSocket);
                    PollFd.events  = POLLHUP | POLLERR | POLLIN;
                    Log(("RTLocalIpcSessionWaitForData: Calling poll...\n"));
                    int cFds = poll(&PollFd, 1, cMillies == RT_INDEFINITE_WAIT ? -1 : (int)cMillies);
                    if (cFds >= 1)
                    {
                        /* Linux & Darwin sets both POLLIN and POLLHUP when the pipe is
                           broken and but no more data to read.  Google hints at NetBSD
                           returning more sane values (POLLIN till no more data, then
                           POLLHUP).  Solairs OTOH, doesn't ever seem to return POLLHUP. */
                        fEvents = RTPOLL_EVT_READ;
                        if (   (PollFd.revents & (POLLHUP | POLLERR))
                            && !(PollFd.revents & POLLIN))
                            fEvents = RTPOLL_EVT_ERROR;
# if defined(RT_OS_SOLARIS)
                        else if (PollFd.revents & POLLIN)
# else
                        else if ((PollFd.revents & (POLLIN | POLLHUP)) == (POLLIN | POLLHUP))
# endif
                        {
                            /* Check if there is actually data available. */
                            uint8_t bPeek;
                            ssize_t rcRecv = recv(PollFd.fd, &bPeek, 1, MSG_DONTWAIT | MSG_PEEK);
                            if (rcRecv <= 0)
                                fEvents = RTPOLL_EVT_ERROR;
                        }
                        rc = VINF_SUCCESS;
                    }
                    else if (rc == 0)
                        rc = VERR_TIMEOUT;
                    else
                        rc = RTErrConvertFromErrno(errno);
                    Log(("RTLocalIpcSessionWaitForData: poll returns %u (rc=%d), revents=%#x\n", cFds, rc, PollFd.revents));
#endif

                    int rc2 = RTCritSectEnter(&pThis->CritSect);
                    AssertRCBreakStmt(rc2, rc = RT_SUCCESS(rc) ? rc2 : rc);

                    if (RT_SUCCESS(rc))
                    {
                        if (pThis->fCancelled)
                            rc = VERR_CANCELLED;
                        else if (fEvents & RTPOLL_EVT_ERROR)
                            rc = VERR_BROKEN_PIPE;
                    }
                    else if (   rc == VERR_INTERRUPTED
                             || rc == VERR_TRY_AGAIN)
                    {
                        /* Recalc cMillies. */
                        if (cMsOriginalTimeout != RT_INDEFINITE_WAIT)
                        {
                            uint64_t cMsElapsed = RTTimeMilliTS() - msStart;
                            cMillies = cMsElapsed >= cMsOriginalTimeout ? 0 : cMsOriginalTimeout - (RTMSINTERVAL)cMsElapsed;
                        }
                        continue;
                    }
                }
                else
                    rc = VERR_CANCELLED;
                break;
            }

            pThis->hReadThread = NIL_RTTHREAD;
        }
        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    rtLocalIpcSessionRelease(pThis);
    return rc;
}


/**
 * Get IPC session socket peer credentials.
 *
 * @returns IPRT status code.
 * @param   hSession    IPC session handle.
 * @param   pProcess    Where to return the remote peer's PID (can be NULL).
 * @param   pUid        Where to return the remote peer's UID (can be NULL).
 * @param   pGid        Where to return the remote peer's GID (can be NULL).
 */
static int rtLocalIpcSessionQueryUcred(RTLOCALIPCSESSION hSession, PRTPROCESS pProcess, PRTUID pUid, PRTGID pGid)
{
    PRTLOCALIPCSESSIONINT pThis = hSession;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTLOCALIPCSESSION_MAGIC, VERR_INVALID_HANDLE);

#if defined(RT_OS_LINUX)
    struct ucred PeerCred   = { (pid_t)NIL_RTPROCESS, (uid_t)NIL_RTUID, (gid_t)NIL_RTGID };
    socklen_t    cbPeerCred = sizeof(PeerCred);

    rtLocalIpcSessionRetain(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);;
    if (RT_SUCCESS(rc))
    {
        if (getsockopt(RTSocketToNative(pThis->hSocket), SOL_SOCKET, SO_PEERCRED, &PeerCred, &cbPeerCred) >= 0)
        {
            if (pProcess)
                *pProcess = PeerCred.pid;
            if (pUid)
                *pUid = PeerCred.uid;
            if (pGid)
                *pGid = PeerCred.gid;
            rc = VINF_SUCCESS;
        }
        else
            rc = RTErrConvertFromErrno(errno);

        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    rtLocalIpcSessionRelease(pThis);

    return rc;

#else
    /** @todo Implement on other platforms too (mostly platform specific this).
     *        Solaris: getpeerucred?  Darwin: LOCALPEERCRED or getpeereid? */
    RT_NOREF(pProcess, pUid, pGid);
    return VERR_NOT_SUPPORTED;
#endif
}


RTDECL(int) RTLocalIpcSessionQueryProcess(RTLOCALIPCSESSION hSession, PRTPROCESS pProcess)
{
    return rtLocalIpcSessionQueryUcred(hSession, pProcess, NULL, NULL);
}


RTDECL(int) RTLocalIpcSessionQueryUserId(RTLOCALIPCSESSION hSession, PRTUID pUid)
{
    return rtLocalIpcSessionQueryUcred(hSession, NULL, pUid, NULL);
}

RTDECL(int) RTLocalIpcSessionQueryGroupId(RTLOCALIPCSESSION hSession, PRTGID pGid)
{
    return rtLocalIpcSessionQueryUcred(hSession, NULL, NULL, pGid);
}

