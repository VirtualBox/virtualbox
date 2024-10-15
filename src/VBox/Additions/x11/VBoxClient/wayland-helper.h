/* $Id: wayland-helper.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Definitions for Wayland helpers.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_h
#define GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/time.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/clipboard-helper.h>

#include "clipboard.h"

/** Helper capabilities list. */

/** Indicates that helper does not support any functionality (initializer). */
#define VBOX_WAYLAND_HELPER_CAP_NONE            (0)
/** Indicates that helper supported shared clipboard functionality. */
#define VBOX_WAYLAND_HELPER_CAP_CLIPBOARD       RT_BIT(1)
/** Indicates that helper supported drag-and-drop functionality. */
#define VBOX_WAYLAND_HELPER_CAP_DND             RT_BIT(2)

/** Default time interval to wait for value to arrive over IPC. */
#define VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS      (1000)
/** Default time interval to wait for clipboard content to arrive over IPC. */
#define VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS       (2000)
/** Generic relax interval while polling value changes. */
#define VBCL_WAYLAND_RELAX_INTERVAL_MS          (50)
/** Maximum number of participants who can join session. */
#define VBCL_WAYLAND_SESSION_USERS_MAX          (10)
/** Value which determines if session structure was initialized. */
#define VBCL_WAYLAND_SESSION_MAGIC              (0xDEADBEEF)

/** Session states. */
typedef enum
{
    /** Session is not active. */
    VBCL_WL_SESSION_STATE_IDLE,
    /** Session is being initialized. */
    VBCL_WL_SESSION_STATE_STARTING,
    /** Session has started and now can be joined.  */
    VBCL_WL_SESSION_STATE_STARTED,
    /** Session is terminating. */
    VBCL_WL_SESSION_STATE_TERMINATING
} vbcl_wl_session_state_t;

/** Session type.
 *
 * Type determines the purpose of session. It is
 * also serves a sanity check purpose when different
 * participants join session with certain intention.
 * Session type can only be set when session is
 * in STARTING state and reset when session is TERMINATING.
 */
typedef enum
{
    /** Initializer. */
    VBCL_WL_SESSION_TYPE_INVALID,
    /** Copy clipboard data to the guest. */
    VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
    /** Announce clipboard formats to the host. */
    VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST,
    /** Copy clipboard data to the host. */
    VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST
} vbcl_wl_session_type_t;

/** Session private data. */
typedef struct
{
    /** Magic number which indicates if session
     *  was previously initialized. */
    volatile uint32_t u32Magic;

    /** Session state, synchronization element. */
    volatile vbcl_wl_session_state_t enmState;

    /** Session type. */
    vbcl_wl_session_type_t enmType;

    /** Session description, used for logging purpose
     *  to distinguish between operations flow. */
    const char *pcszDesc;

    /** Current number of session users. When session
     *  switches into TERMINATING state, it will wait
     *  number of participants to drop to 1 before releasing
     * session resources and resetting internal data to
     * default values. */
    volatile uint32_t cUsers;
} vbcl_wl_session_t;

/** Session state change callback.
 *
 * Data which belongs to a session must be accessed from
 * session callback only. It ensures session data integrity
 * and prevents access to data when session is not yet
 * initialized or already terminated.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, provided for consistency
 *                              check to make sure given callback is
 *                              intended to be triggered in context of
 *                              given session type.
 * @param   pvUser              Optional user data.
 */
typedef DECLCALLBACKTYPE(int, FNVBCLWLSESSIONCB, (vbcl_wl_session_type_t enmSessionType, void *pvUser));
/** Pointer to FNVBCLWLSESSIONCB. */
typedef FNVBCLWLSESSIONCB *PFNVBCLWLSESSIONCB;

/**
 * Wayland Desktop Environment helper definition structure.
 */
typedef struct
{
    /** A short helper name. 16 chars maximum (RTTHREAD_NAME_LEN). */
    const char *pszName;

    /**
     * Probing callback.
     *
     * Called in attempt to detect if user is currently running Desktop Environment
     * which is compatible with the helper.
     *
     * @returns Helpercapabilities bitmask as described by VBOX_WAYLAND_HELPER_CAP_XXX.
     */
    DECLCALLBACKMEMBER(int, pfnProbe, (void));

    /**
     * Initialization callback.
     *
     * @returns IPRT status code.
     */
    DECLCALLBACKMEMBER(int, pfnInit, (void));

    /**
     * Termination callback.
     *
     * @returns IPRT status code.
     */
    DECLCALLBACKMEMBER(int, pfnTerm, (void));

    /**
     * Callback to set host clipboard connection handle.
     *
     * @param   pCtx    Host service connection context.
     */
    DECLCALLBACKMEMBER(void, pfnSetClipboardCtx, (PVBGLR3SHCLCMDCTX pCtx));

    /**
     * Callback to force guest to announce its clipboard content.
     *
     * @returns IPRT status code.
     */
    DECLCALLBACKMEMBER(int, pfnPopup, (void));

    PFNHOSTCLIPREPORTFMTS pfnHGClipReport;
    PFNHOSTCLIPREAD pfnGHClipRead;

} VBCLWAYLANDHELPER;

namespace vbcl
{
    /**
     * This is abstract one-shot data type which can be set by writer and waited
     * by reader in a thread-safe way.
     *
     * Method wait() will wait within predefined interval of time until
     * value of this type will be changed to anything different from default
     * value which is defined during initialization. Reader must compare
     * returned value with what defaults() method returns in order to make
     * sure that value was actually set by writer.
     *
     * Method reset() will atomically reset current value to defaults and
     * return previous value. This is useful when writer has previously
     * dynamically allocated chunk of memory and reader needs to deallocete
     * it in the end.
     */
    template <class T> class Waitable
    {
        public:

            Waitable()
            {};

            /**
             * Initialize data type.
             *
             * @param   default_value   Default value, used while
             *                          waiting for value change.
             * @param   timeoutMs       Time interval to wait for
             *                          value change before returning.
             */
            void init(T default_value, uint64_t timeoutMs)
            {
                m_Value = default_value;
                m_Default = default_value;
                m_TimeoutMs = timeoutMs;
            }

            /**
             * Atomically set value.
             *
             * @param   value   Value to set.
             */
            void set(T value)
            {
                ASMAtomicWriteU64(&m_Value, value);
            }

            /**
             * Atomically reset value to defaults and return previous value.
             *
             * @returns Value which was assigned before reset.
             */
            uint64_t reset()
            {
                return ASMAtomicXchgU64(&m_Value, m_Default);
            }

            /**
             * Wait until value will be changed from defaults and return it.
             *
             * @returns Current value.
             */
            T wait()
            {
                uint64_t tsStart = RTTimeMilliTS();

                while(   (RTTimeMilliTS() - tsStart) < m_TimeoutMs
                      && (ASMAtomicReadU64(&m_Value)) == m_Default)
                {
                    RTThreadSleep(VBCL_WAYLAND_RELAX_INTERVAL_MS);
                }

                return m_Value;
            }

            /**
             * Get default value which was set during initialization.
             *
             * @returns Default value.
             */
            T defaults()
            {
                return m_Default;
            }

        protected:

            /** Value itself. */
            uint64_t m_Value;
            /** Default value. */
            uint64_t m_Default;
            /** Value change waiting timeout. */
            uint64_t m_TimeoutMs;
    };
}

/**
 * Initialize session.
 *
 * This function should be called only once, during initialization step.
 *
 * @param   pSession    A pointer to session data.
 */
RTDECL(void) vbcl_wayland_session_init(vbcl_wl_session_t *pSession);

/**
 * Start new session.
 *
 * Attempt to change session state from IDLE to STARTED and
 * execute initialization callback in between.  If current
 * session state is different from IDLE, state transition will
 * not be possible and error will be returned.
 *
 * @returns IPRT status code.
 * @param   pSession    Session object.
 * @param   enmType     Session type.
 * @param   pfnStart    Initialization callback.
 * @param   pvUser      User data to pass to initialization callback.
 */
RTDECL(int) vbcl_wayland_session_start(vbcl_wl_session_t *pSession,
                                       vbcl_wl_session_type_t enmType,
                                       PFNVBCLWLSESSIONCB pfnStart,
                                       void *pvUser);

/**
 * Join session.
 *
 * Attempt to grab a reference to a session, execute provided
 * callback while holding a reference and release reference.
 * This function will fail if current session state is different
 * from STARTED.
 *
 * @returns IPRT status code.
 * @param   pSession    Session object.
 * @param   pfnJoin     A callback to run while holding session reference.
 * @param   pvUser      User data to pass to callback.
 * @param   pcszCallee  Text tag which corresponds to calling function (only
 *                      for logging)
 */
RTDECL(int) vbcl_wayland_session_join_ex(vbcl_wl_session_t *pSession,
                                         PFNVBCLWLSESSIONCB pfnJoin, void *pvUser,
                                         const char *pcszCallee);

/**
 * Join session (wrapper for vbcl_wayland_session_join_ex).
 */
#define vbcl_wayland_session_join(pSession, pfnJoin, pvUser) \
    vbcl_wayland_session_join_ex(pSession, pfnJoin, pvUser, __func__)

/**
 * End session.
 *
 * Attempt to wait until session is no longer in use, execute
 * terminating callback and reset session to IDLE state.
 *
 * @returns IPRT status code.
 * @param   pSession    Session object.
 * @param   pfnEnd      Termination callback.
 * @param   pvUser      User data to pass to termination callback.
 */
RTDECL(int) vbcl_wayland_session_end(vbcl_wl_session_t *pSession,
                                     PFNVBCLWLSESSIONCB pfnEnd, void *pvUser);

/**
 * Check if session was started.
 *
 * @returns True if session is started, False otherwise.
 * @param   pSession    Session object.
 */
RTDECL(bool) vbcl_wayland_session_is_started(vbcl_wl_session_t *pSession);

/** Wayland helper which uses GTK library. */
extern const VBCLWAYLANDHELPER g_WaylandHelperGtk;
/** Wayland helper which uses Data Control Protocol. */
extern const VBCLWAYLANDHELPER g_WaylandHelperDcp;

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_h */
