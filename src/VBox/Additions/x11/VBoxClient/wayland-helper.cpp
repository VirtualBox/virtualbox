/* $Id: wayland-helper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Common code for Wayland Desktop Environment helpers.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <iprt/env.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/time.h>

#include "VBoxClient.h"
#include "wayland-helper.h"

static const char *g_pcszSessionDescClipardCopyToGuest      = "Copy clipboard to the guest";
static const char *g_pcszSessionDescClipardAnnounceToHost   = "Announce and copy clipboard to the host";
static const char *g_pcszSessionDescClipardCopyToHost       = "Copy clipboard to the host";

/**
 * Interaction between VBoxClient and Wayland environment implies
 * a sequential data exchange between both parties. In order to
 * temporary store and protect this data, we put it into a Session.

 * Session can be started, joined or ended.

 * Each time when either host (via VBoxClient) or Wayland client
 * initiates a new data transfer (such as clipboard sharing or drag-n-drop
 * operation) a new session should be started. Due to current implementation,
 * no more than one session can run in the same time. Therefore, before
 * starting new session, previous one needs to be ended.

 * When either VBoxClient or Wayland thread needs to access session data,
 * it need to join session at first. Multiple threads can join the same
 * session in parallel.

 * When sequence of operations which were required to transfer data to
 * either side is complete, session should be ended.
 *
 * Session has the following attributes:
 *
 * STATE            - synchronization point which is used in order to prevent
 *                    access to session data when it is not allowed (see session
 *                    states description).
 * TYPE             - unique attribute which should be used for sanity checks
 *                    when accessing session data.
 * DESCRIPTION      - a text attribute which is mostly used for logging purposes
 *                    when monitoring multiple sequential sessions flow.
 * REFERENCE COUNT  - attribute which shows a number of session users who currently
 *                    joined a session.
 *
 * Session state represents synchronization point when session data needs to
 * be accessed. It can have permanent or intermediate value. The following states
 * are defined: IDLE, STARTING, STARTED and TERMINATING.
 *
 * Default state is IDLE. It is set when session is just initialized or when
 * it is ended.
 *
 * When user starts a new session, its state will be atomically switched from
 * IDLE to STARTING. Then, initialization callback, provided by user, will be invoked.
 * And, finally, state will be atomically transitioned from STARTING to STARTED.
 * Intermediate state STARTING will ensure that no other user will be allowed to
 * start, join or end session during this initialization step.
 *
 * User is only allowed to join session when it is in STARTED state. Once
 * session is joined, its reference counted will be increased by 1, provided
 * user callback will be called and, finally, reference counter will be dropped by 1.
 *
 * When user ends session, at first, its state will be atomically transitioned
 * from STARTED to TERMINATING ensuring that no other user can start, join or end
 * session in the same time. Then session will wait until all the users who joined
 * the session are left by looking into session's reference counter. After this, it
 * will invoke provided by user termination callback and atomically transition its state
 * from TERMINATING to IDLE, allowing to start a new session.
 */

/**
 * Try atomically enter session state within time interval.
 *
 * @returns IPRT status code.
 * @param   pSessionState   Session state.
 * @param   to              New session state ID to switch to.
 * @param   from            Old session state ID which will be
 *                          atomically compared with the current
 *                          state before switching.
 * @param   u64TimeoutMs    Operation timeout.
 */
static int vbcl_wayland_session_enter_state(volatile vbcl_wl_session_state_t *pSessionState,
                                            vbcl_wl_session_state_t to, vbcl_wl_session_state_t from, uint64_t u64TimeoutMs)
{
    bool fRc = false;
    uint64_t tsStart = RTTimeMilliTS();

    while (   !(fRc = ASMAtomicCmpXchgU8((volatile uint8_t *)pSessionState, to, from))
           && (RTTimeMilliTS() - tsStart) < u64TimeoutMs)
    {
        RTThreadSleep(VBCL_WAYLAND_RELAX_INTERVAL_MS);
    }

    return fRc ? VINF_SUCCESS : VERR_RESOURCE_BUSY;
}



RTDECL(void) vbcl_wayland_session_init(vbcl_wl_session_t *pSession)
{
    AssertPtrReturnVoid(pSession);

    /* Reset internal data to default values. */
    ASMAtomicWriteU32(&pSession->u32Magic, VBCL_WAYLAND_SESSION_MAGIC);
    ASMAtomicWriteU8((volatile uint8_t *)&pSession->enmState, VBCL_WL_SESSION_STATE_IDLE);

    pSession->enmType = VBCL_WL_SESSION_TYPE_INVALID;
    pSession->pcszDesc = NULL;

    ASMAtomicWriteU32(&pSession->cUsers, 0);
}

RTDECL(int) vbcl_wayland_session_start(vbcl_wl_session_t *pSession,
                                       vbcl_wl_session_type_t enmType,
                                       PFNVBCLWLSESSIONCB pfnStart,
                                       void *pvUser)
{
    int rc;
    const char *pcszDesc;

    /* Make sure mandatory parameters were provided. */
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfnStart, VERR_INVALID_PARAMETER);

    /* Make sure session was initialized. */
    AssertReturn(ASMAtomicReadU32(&pSession->u32Magic) == VBCL_WAYLAND_SESSION_MAGIC,
                 VERR_SEM_DESTROYED);

    /* Set session description according to its type. */
    if      (enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
        pcszDesc = g_pcszSessionDescClipardCopyToGuest;
    else if (enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
        pcszDesc = g_pcszSessionDescClipardAnnounceToHost;
    else if (enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
        pcszDesc = g_pcszSessionDescClipardCopyToHost;
    else
        pcszDesc = NULL;

    rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                          VBCL_WL_SESSION_STATE_STARTING,
                                          VBCL_WL_SESSION_STATE_IDLE,
                                          RT_MS_1SEC);
    if (RT_SUCCESS(rc))
    {
        int rcStart;

        /* Make sure nobody is using the session. */
        Assert(ASMAtomicReadU32(&pSession->cUsers) == 0);

        /* Set session type. */
        pSession->enmType = enmType;
        /* Set session description. */
        pSession->pcszDesc = pcszDesc;
        /* Reset reference counter. */
        ASMAtomicWriteU32(&pSession->cUsers, 1);

        rcStart = pfnStart(enmType, pvUser);

        rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                              VBCL_WL_SESSION_STATE_STARTED,
                                              VBCL_WL_SESSION_STATE_STARTING,
                                              RT_MS_1SEC);

        VBClLogVerbose(2, "session start [%s], rc=%Rrc, rcStart=%Rrc\n",
                       pSession->pcszDesc, rc, rcStart);

        if (RT_SUCCESS(rc))
            rc = rcStart;
    }
    else
        VBClLogError("cannot start session [%s], another session [%s] is currently in progress, rc=%Rrc\n",
                     pcszDesc, pSession->pcszDesc, rc);

    return rc;
}

RTDECL(int) vbcl_wayland_session_join_ex(vbcl_wl_session_t *pSession,
                                         PFNVBCLWLSESSIONCB pfnJoin, void *pvUser,
                                         const char *pcszCallee)
{
    int rc;

    /* Make sure mandatory parameters were provided. */
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfnJoin, VERR_INVALID_PARAMETER);

    /* Make sure session was initialized. */
    AssertReturn(ASMAtomicReadU32(&pSession->u32Magic) == VBCL_WAYLAND_SESSION_MAGIC,
                 VERR_SEM_DESTROYED);

    rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                          VBCL_WL_SESSION_STATE_STARTED,
                                          VBCL_WL_SESSION_STATE_STARTED,
                                          RT_MS_1SEC);
    if (RT_SUCCESS(rc))
    {
        uint32_t cUsers;
        const char *pcszDesc = pSession->pcszDesc;

        /* Take session reference. */
        cUsers = ASMAtomicIncU32(&pSession->cUsers);
        VBClLogVerbose(2, "session join [%s @ %s], cUsers=%u\n", pcszDesc, pcszCallee, cUsers);

        /* Process callback while holding reference to session. */
        rc = pfnJoin(pSession->enmType, pvUser);

        /* Release session reference. */
        cUsers = ASMAtomicDecU32(&pSession->cUsers);
        VBClLogVerbose(2, "session leave [%s @ %s], cUsers=%u, rc=%Rrc\n",
                       pcszDesc, pcszCallee, cUsers, rc);

    }
    else
        VBClLogError("cannot join session from %s, rc=%Rrc\n", pcszCallee, rc);

    return rc;
}

RTDECL(int) vbcl_wayland_session_end(vbcl_wl_session_t *pSession,
                                     PFNVBCLWLSESSIONCB pfnEnd, void *pvUser)
{
    int rc;

    /* Make sure mandatory parameters were provided.. */
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);

    /* Make sure session was initialized. */
    AssertReturn(ASMAtomicReadU32(&pSession->u32Magic) == VBCL_WAYLAND_SESSION_MAGIC,
                 VERR_SEM_DESTROYED);

    /* Session not running? */
    rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                          VBCL_WL_SESSION_STATE_IDLE,
                                          VBCL_WL_SESSION_STATE_IDLE,
                                          0 /* u64TimeoutMs */);
    if (RT_SUCCESS(rc))
    {
        /* NOP. */
        VBClLogVerbose(2, "session end: nothing to do, no active session is running\n");
    }
    else
    {
        rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                              VBCL_WL_SESSION_STATE_TERMINATING, VBCL_WL_SESSION_STATE_STARTED,
                                              RT_MS_1SEC);
        if (RT_SUCCESS(rc))
        {
            int rcEnd = VINF_SUCCESS;
            const char *pcszDesc = pSession->pcszDesc;

            /* Wait until for other callers to release session reference. */
            while (ASMAtomicReadU32(&pSession->cUsers) > 1)
                RTThreadSleep(VBCL_WAYLAND_RELAX_INTERVAL_MS);

            if (RT_VALID_PTR(pfnEnd))
                rcEnd = pfnEnd(pSession->enmType, pvUser);

            pSession->enmType = VBCL_WL_SESSION_TYPE_INVALID;
            pSession->pcszDesc = NULL;

            ASMAtomicDecU32(&pSession->cUsers);

            rc = vbcl_wayland_session_enter_state(&pSession->enmState,
                                                  VBCL_WL_SESSION_STATE_IDLE, VBCL_WL_SESSION_STATE_TERMINATING,
                                                  RT_MS_1SEC);

            VBClLogVerbose(2, "session end [%s], rc=%Rrc, rcEnd=%Rrc\n", pcszDesc, rc, rcEnd);

            if (RT_SUCCESS(rc))
                rc = rcEnd;
        }
        else
            VBClLogError("cannot end session, rc=%Rrc\n", rc);
    }

    return rc;
}

RTDECL(bool) vbcl_wayland_session_is_started(vbcl_wl_session_t *pSession)
{
    /* Make sure mandatory parameters were provided. */
    AssertPtrReturn(pSession, false);

    /* Make sure session was initialized. */
    AssertReturn(ASMAtomicReadU32(&pSession->u32Magic) == VBCL_WAYLAND_SESSION_MAGIC, false);

    return RT_BOOL(ASMAtomicReadU8((volatile uint8_t *)&pSession->enmState) == VBCL_WL_SESSION_STATE_STARTED);
}
