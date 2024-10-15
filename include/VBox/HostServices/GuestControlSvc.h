/* $Id: GuestControlSvc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest control service - Common header for host service and guest clients.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_HostServices_GuestControlSvc_h
#define VBOX_INCLUDED_HostServices_GuestControlSvc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <VBox/hgcmsvc.h>

#include <VBox/VMMDevCoreTypes.h>
#include <VBox/GuestHost/GuestControl.h>
#include <VBox/VBoxGuestCoreTypes.h>

/* Everything defined in this file lives in this namespace. */
namespace guestControl {

/******************************************************************************
* Typedefs, constants and inlines                                             *
******************************************************************************/

#define HGCMSERVICE_NAME "VBoxGuestControlSvc"

/** Maximum number of concurrent guest sessions a VM can have. */
#define VBOX_GUESTCTRL_MAX_SESSIONS     32
/** Maximum number of concurrent guest objects (processes, files, ...)
 *  a guest session can have. */
#define VBOX_GUESTCTRL_MAX_OBJECTS      _2K
/** Maximum of callback contexts a guest process can have. */
#define VBOX_GUESTCTRL_MAX_CONTEXTS     _64K

/** Base (start) of guest control session IDs. Session
 *  ID 0 is reserved for the root process which
 *  hosts all other guest session processes. */
#define VBOX_GUESTCTRL_SESSION_ID_BASE  1

/** Builds a context ID out of the session ID, object ID and an
 *  increasing count. */
#define VBOX_GUESTCTRL_CONTEXTID_MAKE(uSession, uObject, uCount) \
    (  (uint32_t)((uSession) &   0x1f) << 27 \
     | (uint32_t)((uObject)  &  0x7ff) << 16 \
     | (uint32_t)((uCount)   & 0xffff)       \
    )
/** Creates a context ID out of a session ID. */
#define VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(uSession) \
    ((uint32_t)((uSession) & 0x1f) << 27)
/** Gets the session ID out of a context ID. */
#define VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(uContextID) \
    (((uContextID) >> 27) & 0x1f)
/** Gets the process ID out of a context ID. */
#define VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(uContextID) \
    (((uContextID) >> 16) & 0x7ff)
/** Gets the context count of a process out of a context ID. */
#define VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID) \
    ((uContextID) & 0xffff)
/** Filter context IDs by session. Can be used in conjunction
 *  with VbglR3GuestCtrlMsgFilterSet(). */
#define VBOX_GUESTCTRL_FILTER_BY_SESSION(uSession) \
    (VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(uSession) | 0xF8000000)

/**
 * Structure keeping the context of a host callback.
 */
typedef struct VBOXGUESTCTRLHOSTCBCTX
{
    /** HGCM message number. */
    uint32_t uMessage;
    /** The context ID. */
    uint32_t uContextID;
    /** Protocol version of this guest session. Might
     *  be 0 if not supported. */
    uint32_t uProtocol;
} VBOXGUESTCTRLHOSTCBCTX, *PVBOXGUESTCTRLHOSTCBCTX;

/**
 * Structure for low level HGCM host callback from
 * the guest. No deep copy.
 */
typedef struct VBOXGUESTCTRLHOSTCALLBACK
{
    /** Number of HGCM parameters. */
    uint32_t         mParms;
    /** Actual HGCM parameters. */
    PVBOXHGCMSVCPARM mpaParms;
} VBOXGUESTCTRLHOSTCALLBACK, *PVBOXGUESTCTRLHOSTCALLBACK;

/** @name Host message destination flags.
 *
 * This is ORed into the context ID parameter Main after extending it to 64-bit.
 *
 * @internal Host internal.
 * @{ */
#define VBOX_GUESTCTRL_DST_ROOT_SVC     RT_BIT_64(63)
#define VBOX_GUESTCTRL_DST_SESSION      RT_BIT_64(62)
#define VBOX_GUESTCTRL_DST_BOTH         ( VBOX_GUESTCTRL_DST_ROOT_SVC | VBOX_GUESTCTRL_DST_SESSION )
/** @} */


/**
 * The service messages which are callable by host.
 */
enum eHostMsg
{
    /**
     * The host asks the client to cancel all pending waits and exit.
     */
    HOST_MSG_CANCEL_PENDING_WAITS = 0,
    /**
     * The host wants to create a guest session.
     */
    HOST_MSG_SESSION_CREATE = 20,
    /**
     * The host wants to close a guest session.
     */
    HOST_MSG_SESSION_CLOSE = 21,
    /**
     * The host wants to execute something in the guest. This can be a command
     * line or starting a program.
     */
    HOST_MSG_EXEC_CMD = 100,
    /**
     * Sends input data for stdin to a running process executed by HOST_EXEC_CMD.
     */
    HOST_MSG_EXEC_SET_INPUT = 101,
    /**
     * Gets the current status of a running process, e.g.
     * new data on stdout/stderr, process terminated etc.
     */
    HOST_MSG_EXEC_GET_OUTPUT = 102,
    /**
     * Terminates a running guest process.
     */
    HOST_MSG_EXEC_TERMINATE = 110,
    /**
     * Waits for a certain event to happen. This can be an input, output
     * or status event.
     */
    HOST_MSG_EXEC_WAIT_FOR = 120,
    /**
     * Opens a guest file.
     */
    HOST_MSG_FILE_OPEN = 240,
    /**
     * Closes a guest file.
     */
    HOST_MSG_FILE_CLOSE,
    /**
     * Reads from an opened guest file.
     */
    HOST_MSG_FILE_READ = 250,
    /**
     * Reads from an opened guest file at a specified offset.
     */
    HOST_MSG_FILE_READ_AT,
    /**
     * Write to an opened guest file.
     */
    HOST_MSG_FILE_WRITE = 260,
    /**
     * Write to an opened guest file at a specified offset.
     */
    HOST_MSG_FILE_WRITE_AT,
    /**
     * Changes the read & write position of an opened guest file.
     */
    HOST_MSG_FILE_SEEK = 270,
    /**
     * Gets the current file position of an opened guest file.
     */
    HOST_MSG_FILE_TELL = 271,
    /**
     * Changes the file size.
     */
    HOST_MSG_FILE_SET_SIZE = 272,
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /**
     * Removes a file on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_FILE_REMOVE = 273,
    /**
     * Opens (creates) a directory on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_OPEN = 310,
    /**
     * Closes a directory on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_CLOSE = 311,
    /**
     * Reads the next directory entry on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_READ = 312,
    /**
     * Rewinds and restarts the directory reading on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_REWIND = 313,
    /**
     * Creates a directory on the guest.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_CREATE = 314,
    /**
     * Lists one or multiple directory entries at once.
     *
     * @since 7.1
     */
    HOST_MSG_DIR_LIST = 315,
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    /**
     * Removes a directory on the guest.
     */
    HOST_MSG_DIR_REMOVE = 320,
    /**
     * Renames a path on the guest.
     */
    HOST_MSG_PATH_RENAME = 330,
    /**
     * Retrieves the user's documents directory.
     */
    HOST_MSG_PATH_USER_DOCUMENTS = 331,
    /**
     * Retrieves the user's home directory.
     */
    HOST_MSG_PATH_USER_HOME = 332,
    /**
     * Issues a shutdown / reboot of the guest OS.
     */
    HOST_MSG_SHUTDOWN = 333,
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /**
     * Retrieves information about a file system object.
     *
     * @since 7.1
     */
    HOST_MSG_FS_OBJ_QUERY_INFO = 334,
    /**
     * Creates a temporary file or directory.
     *
     * @since 7.1
     */
    HOST_MSG_FS_CREATE_TEMP = 335,
    /**
     * Retrieves information about a guest file system.
     *
     * @since 7.1
     */
    HOST_MSG_FS_QUERY_INFO = 336,
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    /**
     * Retrieves the currently accessible mount points from the guest.
     *
     * @since 7.1
     */
    HOST_MSG_MOUNT_POINTS = 337,
    /** Blow the type up to 32-bits. */
    HOST_MSG_32BIT_HACK = 0x7fffffff
};


/**
 * Translates a guest control host message enum to a string.
 *
 * @returns Enum string name.
 * @param   enmMsg              The message to translate.
 */
DECLINLINE(const char *) GstCtrlHostMsgtoStr(enum eHostMsg enmMsg)
{
    switch (enmMsg)
    {
        RT_CASE_RET_STR(HOST_MSG_CANCEL_PENDING_WAITS);
        RT_CASE_RET_STR(HOST_MSG_SESSION_CREATE);
        RT_CASE_RET_STR(HOST_MSG_SESSION_CLOSE);
        RT_CASE_RET_STR(HOST_MSG_EXEC_CMD);
        RT_CASE_RET_STR(HOST_MSG_EXEC_SET_INPUT);
        RT_CASE_RET_STR(HOST_MSG_EXEC_GET_OUTPUT);
        RT_CASE_RET_STR(HOST_MSG_EXEC_TERMINATE);
        RT_CASE_RET_STR(HOST_MSG_EXEC_WAIT_FOR);
        RT_CASE_RET_STR(HOST_MSG_FILE_OPEN);
        RT_CASE_RET_STR(HOST_MSG_FILE_CLOSE);
        RT_CASE_RET_STR(HOST_MSG_FILE_READ);
        RT_CASE_RET_STR(HOST_MSG_FILE_READ_AT);
        RT_CASE_RET_STR(HOST_MSG_FILE_WRITE);
        RT_CASE_RET_STR(HOST_MSG_FILE_WRITE_AT);
        RT_CASE_RET_STR(HOST_MSG_FILE_SEEK);
        RT_CASE_RET_STR(HOST_MSG_FILE_TELL);
        RT_CASE_RET_STR(HOST_MSG_FILE_SET_SIZE);
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        RT_CASE_RET_STR(HOST_MSG_FILE_REMOVE);
        RT_CASE_RET_STR(HOST_MSG_DIR_OPEN);
        RT_CASE_RET_STR(HOST_MSG_DIR_CLOSE);
        RT_CASE_RET_STR(HOST_MSG_DIR_READ);
        RT_CASE_RET_STR(HOST_MSG_DIR_REWIND);
        RT_CASE_RET_STR(HOST_MSG_DIR_CREATE);
        RT_CASE_RET_STR(HOST_MSG_DIR_LIST);
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
        RT_CASE_RET_STR(HOST_MSG_DIR_REMOVE);
        RT_CASE_RET_STR(HOST_MSG_PATH_RENAME);
        RT_CASE_RET_STR(HOST_MSG_PATH_USER_DOCUMENTS);
        RT_CASE_RET_STR(HOST_MSG_PATH_USER_HOME);
        RT_CASE_RET_STR(HOST_MSG_SHUTDOWN);
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        RT_CASE_RET_STR(HOST_MSG_FS_OBJ_QUERY_INFO);
        RT_CASE_RET_STR(HOST_MSG_FS_CREATE_TEMP);
        RT_CASE_RET_STR(HOST_MSG_FS_QUERY_INFO);
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
        RT_CASE_RET_STR(HOST_MSG_MOUNT_POINTS);
        RT_CASE_RET_STR(HOST_MSG_32BIT_HACK);
    }
    return "Unknown";
}


/**
 * The service messages which are callable by the guest.
 *
 * @note The message numbers cannot be changed.  Please use the first non-zero
 *       number that's not in use when adding new messages.
 *
 * @note Remember to update service.cpp when adding new messages for Main,
 *       as it validates all incoming messages before passing them on.
 */
enum eGuestMsg
{
    /** Guest waits for a new message the host wants to process on the guest side.
     * This is a blocking call and can be deferred.
     *
     * @note This message is rather odd.  The above description isn't really
     *       correct.  Yes, it (1) waits for a new message and will return the
     *       mesage number and parameter count when one is available.   However, it
     *       is also (2) used to retrieve the message parameters.   For some weird
     *       reasons it was decided that it should always return VERR_TOO_MUCH_DATA
     *       when used in the first capacity.
     *
     * @note Has a problem if the guest kernel module cancels the HGCM call, as the
     *       guest cannot resume waiting till the host issues a message for it and
     *       the cancelled call returns.  The new message may potentially end up in
     *       /dev/null depending and hang the message conversation between the guest
     *       and the host (SIGCHLD).
     *
     * @deprecated Replaced by GUEST_MSG_PEEK_WAIT, GUEST_MSG_GET and
     *             GUEST_MSG_CANCEL.
     */
    GUEST_MSG_WAIT = 1,
    /** Cancels pending calls for this client session.
     *
     * This should be used if a GUEST_MSG_PEEK_WAIT or GUEST_MSG_WAIT call gets
     * interrupted on the client end, so as to prevent being rebuffed with
     * VERR_RESOURCE_BUSY when restarting the call.
     *
     * @retval  VINF_SUCCESS if cancelled any calls.
     * @retval  VWRN_NOT_FOUND if no callers.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @since   6.0
     */
    GUEST_MSG_CANCEL = 2,
    /** Guest disconnected (terminated normally or due to a crash HGCM
     * detected when calling service::clientDisconnect().
     *
     * @note This is a host side notification message that has no business in this
     *       enum.  The guest cannot use this message number, host will reject it.
     */
    GUEST_MSG_DISCONNECTED = 3,
    /** Sets a message filter to only get messages which have a certain
     * context ID scheme (that is, a specific session, object etc).
     * Since VBox 4.3+.
     * @deprecated  Replaced by GUEST_SESSION_ACCEPT.
     */
    GUEST_MSG_FILTER_SET = 4,
    /** Unsets (and resets) a previously set message filter.
     * @retval  VERR_NOT_IMPLEMENTED since 6.0.
     * @deprecated  Never needed or used,
     */
    GUEST_MSG_FILTER_UNSET = 5,
    /** Peeks at the next message, returning immediately.
     *
     * Returns two 32-bit parameters, first is the message ID and the second the
     * parameter count.  May optionally return additional 32-bit parameters with the
     * sizes of respective message parameters.  To distinguish buffer sizes from
     * integer parameters, the latter gets their sizes inverted (uint32_t is ~4U,
     * uint64_t is ~8U).
     *
     * Does also support the VM restore checking as in GUEST_MSG_PEEK_WAIT (64-bit
     * param \# 0), see documentation there.
     *
     * @retval  VINF_SUCCESS if a message was pending and is being returned.
     * @retval  VERR_TRY_AGAIN if no message pending.
     * @retval  VERR_VM_RESTORED if first parameter is a non-zero 64-bit value that
     *          does not match VbglR3GetSessionId() any more.  The new value is
     *          returned.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0
     */
    GUEST_MSG_PEEK_NOWAIT = 6,
    /** Peeks at the next message, waiting for one to arrive.
     *
     * Returns two 32-bit parameters, first is the message ID and the second the
     * parameter count.  May optionally return additional 32-bit parameters with the
     * sizes of respective message parameters.  To distinguish buffer sizes from
     * integer parameters, the latter gets their sizes inverted (uint32_t is ~4U,
     * uint64_t is ~8U).
     *
     * To facilitate VM restore checking, the first parameter can be a 64-bit
     * integer holding the VbglR3GetSessionId() value the guest knowns.  The
     * function will then check this before going to sleep and return
     * VERR_VM_RESTORED if it doesn't match, same thing happens when the VM is
     * restored.
     *
     * @retval  VINF_SUCCESS if info about an pending message is being returned.
     * @retval  VINF_TRY_AGAIN and message set to HOST_CANCEL_PENDING_WAITS if
     *          cancelled by GUEST_MSG_CANCEL.
     * @retval  VERR_RESOURCE_BUSY if another thread already made a waiting call.
     * @retval  VERR_VM_RESTORED if first parameter is a non-zero 64-bit value that
     *          does not match VbglR3GetSessionId() any more.  The new value is
     *          returned.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @note    This replaces GUEST_MSG_WAIT.
     * @since   6.0
     */
    GUEST_MSG_PEEK_WAIT = 7,
    /** Gets the next message, returning immediately.
     *
     * All parameters are specific to the message being retrieved, however if the
     * first one is an integer value it shall be an input parameter holding the
     * ID of the message being retrieved.  While it would be nice to add a separate
     * parameter for this purpose, this is difficult without breaking GUEST_MSG_WAIT
     * compatibility.
     *
     * @retval  VINF_SUCCESS if message retrieved and removed from the pending queue.
     * @retval  VERR_TRY_AGAIN if no message pending.
     * @retval  VERR_MISMATCH if the incoming message ID does not match the pending.
     * @retval  VERR_BUFFER_OVERFLOW if a parmeter buffer is too small.  The buffer
     *          size was updated to reflect the required size.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @note    This replaces GUEST_MSG_WAIT.
     * @since   6.0
     */
    GUEST_MSG_GET = 8,
    /** Skip message.
     *
     * This skips the current message, replying to the main backend as best it can.
     * Takes between zero and two parameters.  The first parameter is the 32-bit
     * VBox status code to pass onto Main when skipping the message, defaults to
     * VERR_NOT_SUPPORTED.  The second parameter is the 32-bit message ID of the
     * message to skip, by default whatever is first in the queue is removed.  This
     * is also the case if UINT32_MAX is specified.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_NOT_FOUND if no message pending.
     * @retval  VERR_MISMATCH if the specified message ID didn't match.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @since   6.0
     */
    GUEST_MSG_SKIP = 9,
    /**
     * Skips the current assigned message returned by GUEST_MSG_WAIT.
     * Needed for telling the host service to not keep stale
     * host messages in the queue.
     * @deprecated  Replaced by GUEST_MSG_SKIP.
     */
    GUEST_MSG_SKIP_OLD = 10,
    /** General reply to a host message.
     * Only contains basic data along with a simple payload.
     * @todo proper docs.
     */
    GUEST_MSG_REPLY = 11,
    /** General message for updating a pending progress for a long task.
     * @todo proper docs.
     */
    GUEST_MSG_PROGRESS_UPDATE = 12,
    /** Sets the caller as the master.
     *
     * Called by the root VBoxService to explicitly tell the host that's the master
     * service.  Required to use main VBoxGuest device node.  No parameters.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_ACCESS_DENIED if not using main VBoxGuest device not
     * @retval  VERR_RESOURCE_BUSY if there is already a master.
     * @retval  VERR_VERSION_MISMATCH if VBoxGuest didn't supply requestor info.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @since   6.0
     */
    GUEST_MSG_MAKE_ME_MASTER = 13,
    /** Prepares the starting of a session.
     *
     * VBoxService makes this call before spawning a session process (must be
     * master). The first parameter is the session ID and the second is a one time
     * key for identifying the right session process.  First parameter is a 32-bit
     * session ID with a value between 1 and 0xfff0.  The second parameter is a byte
     * buffer containing a key that GUEST_SESSION_ACCEPT checks against, minimum
     * length is 64 bytes, maximum 16384 bytes.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_OUT_OF_RESOURCES if too many pending sessions hanging around.
     * @retval  VERR_OUT_OF_RANGE if the session ID outside the allowed range.
     * @retval  VERR_BUFFER_OVERFLOW if key too large.
     * @retval  VERR_BUFFER_UNDERFLOW if key too small.
     * @retval  VERR_ACCESS_DENIED if not master or in legacy mode.
     * @retval  VERR_DUPLICATE if the session ID has been prepared already.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0
     */
    GUEST_MSG_SESSION_PREPARE = 14,
    /** Cancels a prepared session.
     *
     * VBoxService makes this call to clean up after spawning a session process
     * failed.  One parameter, 32-bit session ID.  If UINT32_MAX is passed, all
     * prepared sessions are cancelled.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VWRN_NOT_FOUND if no session with the specified ID.
     * @retval  VERR_ACCESS_DENIED if not master or in legacy mode.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0
     */
    GUEST_MSG_SESSION_CANCEL_PREPARED = 15,
    /** Accepts a prepared session.
     *
     * The session processes makes this call to accept a prepared session.  The
     * session ID is then uniquely associated with the HGCM client ID of the caller.
     * The parameters must be identical to the matching GUEST_SESSION_PREPARE call.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_NOT_FOUND if the specified session ID wasn't found.
     * @retval  VERR_OUT_OF_RANGE if the session ID outside the allowed range.
     * @retval  VERR_BUFFER_OVERFLOW if key too large.
     * @retval  VERR_BUFFER_UNDERFLOW if key too small.
     * @retval  VERR_ACCESS_DENIED if we're in legacy mode or is master.
     * @retval  VERR_RESOURCE_BUSY if the client is already associated with a session.
     * @retval  VERR_MISMATCH if the key didn't match.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0
     */
    GUEST_MSG_SESSION_ACCEPT = 16,
    /**
     * Guest reports back a guest session status.
     * @todo proper docs.
     */
    GUEST_MSG_SESSION_NOTIFY = 20,
    /**
     * Guest wants to close a specific guest session.
     * @todo proper docs.
     */
    GUEST_MSG_SESSION_CLOSE = 21,

    /** Report guest side feature flags and retrieve the host ones.
     *
     * VBoxService makes this call right after becoming master to indicate to the
     * host what features it support in addition.  In return the host will return
     * features the host supports.  Two 64-bit parameters are passed in from the
     * guest with the guest features (VBOX_GUESTCTRL_GF_XXX), the host replies by
     * replacing the parameter values with the host ones (VBOX_GUESTCTRL_HF_XXX).
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_ACCESS_DENIED it not master.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0.10, 5.2.32
     */
    GUEST_MSG_REPORT_FEATURES,
    /** Query the host ones feature masks.
     *
     * This is for the session sub-process so that it can get hold of the features
     * from the host.  Again, it is prudent to set the 127 bit and observe it being
     * cleared on success, as older hosts might return success without doing
     * anything.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   6.0.10, 5.2.32
     */
    GUEST_MSG_QUERY_FEATURES,

    /**
     * Guests sends output from an executed process.
     * @todo proper docs.
     */
    GUEST_MSG_EXEC_OUTPUT = 100,
    /**
     * Guest sends a status update of an executed process to the host.
     * @todo proper docs.
     */
    GUEST_MSG_EXEC_STATUS = 101,
    /**
     * Guests sends an input status notification to the host.
     * @todo proper docs.
     */
    GUEST_MSG_EXEC_INPUT_STATUS = 102,
    /**
     * Guest notifies the host about some I/O event. This can be
     * a stdout, stderr or a stdin event. The actual event only tells
     * how many data is available / can be sent without actually
     * transmitting the data.
     * @todo proper docs.
     */
    GUEST_MSG_EXEC_IO_NOTIFY = 210,
    /**
     * Guest notifies the host about some directory event.
     * @todo proper docs.
     */
    GUEST_MSG_DIR_NOTIFY = 230,
    /**
     * Guest notifies the host about some file event.
     * @todo proper docs.
     */
    GUEST_MSG_FILE_NOTIFY = 240
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /**
     *  Guest notifies the host about some file system event.
     *
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_INVALID_CLIENT_ID
     * @retval  VERR_WRONG_PARAMETER_COUNT
     * @retval  VERR_WRONG_PARAMETER_TYPE
     * @since   7.1
     */
    , GUEST_MSG_FS_NOTIFY  = 241
#endif
};

/**
 * Translates a guest control guest message enum to a string.
 *
 * @returns Enum string name.
 * @param   enmMsg              The message to translate.
 */
DECLINLINE(const char *) GstCtrlGuestMsgToStr(enum eGuestMsg enmMsg)
{
    switch (enmMsg)
    {
        RT_CASE_RET_STR(GUEST_MSG_WAIT);
        RT_CASE_RET_STR(GUEST_MSG_CANCEL);
        RT_CASE_RET_STR(GUEST_MSG_DISCONNECTED);
        RT_CASE_RET_STR(GUEST_MSG_FILTER_SET);
        RT_CASE_RET_STR(GUEST_MSG_FILTER_UNSET);
        RT_CASE_RET_STR(GUEST_MSG_PEEK_NOWAIT);
        RT_CASE_RET_STR(GUEST_MSG_PEEK_WAIT);
        RT_CASE_RET_STR(GUEST_MSG_GET);
        RT_CASE_RET_STR(GUEST_MSG_SKIP_OLD);
        RT_CASE_RET_STR(GUEST_MSG_REPLY);
        RT_CASE_RET_STR(GUEST_MSG_PROGRESS_UPDATE);
        RT_CASE_RET_STR(GUEST_MSG_SKIP);
        RT_CASE_RET_STR(GUEST_MSG_MAKE_ME_MASTER);
        RT_CASE_RET_STR(GUEST_MSG_SESSION_PREPARE);
        RT_CASE_RET_STR(GUEST_MSG_SESSION_CANCEL_PREPARED);
        RT_CASE_RET_STR(GUEST_MSG_SESSION_ACCEPT);
        RT_CASE_RET_STR(GUEST_MSG_SESSION_NOTIFY);
        RT_CASE_RET_STR(GUEST_MSG_SESSION_CLOSE);
        RT_CASE_RET_STR(GUEST_MSG_REPORT_FEATURES);
        RT_CASE_RET_STR(GUEST_MSG_QUERY_FEATURES);
        RT_CASE_RET_STR(GUEST_MSG_EXEC_OUTPUT);
        RT_CASE_RET_STR(GUEST_MSG_EXEC_STATUS);
        RT_CASE_RET_STR(GUEST_MSG_EXEC_INPUT_STATUS);
        RT_CASE_RET_STR(GUEST_MSG_EXEC_IO_NOTIFY);
        RT_CASE_RET_STR(GUEST_MSG_DIR_NOTIFY);
        RT_CASE_RET_STR(GUEST_MSG_FILE_NOTIFY);
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        RT_CASE_RET_STR(GUEST_MSG_FS_NOTIFY);
#endif
    }
    return "Unknown";
}

/**
 * Guest session notification types.
 * @sa HGCMMsgSessionNotify.
 */
enum GUEST_SESSION_NOTIFYTYPE
{
    GUEST_SESSION_NOTIFYTYPE_UNDEFINED = 0,
    /** Something went wrong (see rc). */
    GUEST_SESSION_NOTIFYTYPE_ERROR = 1,
    /** Guest session has been started. */
    GUEST_SESSION_NOTIFYTYPE_STARTED = 11,
    /** Guest session terminated normally. */
    GUEST_SESSION_NOTIFYTYPE_TEN = 20,
    /** Guest session terminated via signal. */
    GUEST_SESSION_NOTIFYTYPE_TES = 30,
    /** Guest session terminated abnormally. */
    GUEST_SESSION_NOTIFYTYPE_TEA = 40,
    /** Guest session timed out and was killed. */
    GUEST_SESSION_NOTIFYTYPE_TOK = 50,
    /** Guest session timed out and was not killed successfully. */
    GUEST_SESSION_NOTIFYTYPE_TOA = 60,
    /** Service/OS is stopping, process was killed. */
    GUEST_SESSION_NOTIFYTYPE_DWN = 150
};

/**
 * Guest directory notification types.
 * @sa HGCMMsgReplyDirNotify.
 */
enum GUEST_DIR_NOTIFYTYPE
{
    GUEST_DIR_NOTIFYTYPE_UNKNOWN = 0,
    /** Something went wrong (see rc). */
    GUEST_DIR_NOTIFYTYPE_ERROR = 1,
    /** Guest directory opened. */
    GUEST_DIR_NOTIFYTYPE_OPEN = 10,
    /** Guest directory closed. */
    GUEST_DIR_NOTIFYTYPE_CLOSE = 20,
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /** Guest directory read. */
    GUEST_DIR_NOTIFYTYPE_READ = 21,
    /** Guest directory was rewind. */
    GUEST_DIR_NOTIFYTYPE_REWIND = 22,
    /** Guest directory listing. */
    GUEST_DIR_NOTIFYTYPE_LIST = 23,
#endif
    /** Information about an open guest directory. */
    GUEST_DIR_NOTIFYTYPE_INFO = 40,
    /** Guest directory created. */
    GUEST_DIR_NOTIFYTYPE_CREATE = 70,
    /** Guest directory deleted. */
    GUEST_DIR_NOTIFYTYPE_REMOVE = 80
};

/**
 * Guest file notification types.
 * @sa HGCMMsgFileNotify.
 */
enum GUEST_FILE_NOTIFYTYPE
{
    GUEST_FILE_NOTIFYTYPE_UNKNOWN = 0,
    GUEST_FILE_NOTIFYTYPE_ERROR = 1,
    GUEST_FILE_NOTIFYTYPE_OPEN = 10,
    GUEST_FILE_NOTIFYTYPE_CLOSE = 20,
    GUEST_FILE_NOTIFYTYPE_READ = 30,
    GUEST_FILE_NOTIFYTYPE_READ_OFFSET,  /**< @since 6.0.10, 5.2.32 - VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET */
    GUEST_FILE_NOTIFYTYPE_WRITE = 40,
    GUEST_FILE_NOTIFYTYPE_WRITE_OFFSET, /**< @since 6.0.10, 5.2.32 - VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET */
    GUEST_FILE_NOTIFYTYPE_SEEK = 50,
    GUEST_FILE_NOTIFYTYPE_TELL = 60,
    GUEST_FILE_NOTIFYTYPE_SET_SIZE
};

/**
 * Guest file system notification types.
 */
enum GUEST_FS_NOTIFYTYPE
{
    /** Unknown fs notification type; do not use. */
    GUEST_FS_NOTIFYTYPE_UNKNOWN        = 0,
    /** Temporary directory creation notification from the guest.
     *  @since 7.1 */
    GUEST_FS_NOTIFYTYPE_CREATE_TEMP    = 1,
    /** File system object query information notification from the guest.
     *  @since 7.1 */
    GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO = 2,
    /** File system query information notification from the guest.
     *  @since 7.1 */
    GUEST_FS_NOTIFYTYPE_QUERY_INFO     = 3
};

/**
 * Guest file seeking types. Has to match FileSeekType in Main.
 *
 * @note This is not compatible with RTFileSeek, which is an unncessary pain.
 */
enum GUEST_FILE_SEEKTYPE
{
    GUEST_FILE_SEEKTYPE_BEGIN = 1,
    GUEST_FILE_SEEKTYPE_CURRENT = 4,
    GUEST_FILE_SEEKTYPE_END = 8
};

/** @name VBOX_GUESTCTRL_GF_XXX - Guest features.
 * @sa GUEST_MSG_REPORT_FEATURES
 * @{ */
/** Supports HOST_MSG_FILE_SET_SIZE. */
#define VBOX_GUESTCTRL_GF_0_SET_SIZE                RT_BIT_64(0)
/** Supports passing process arguments starting at argv[0] rather than argv[1].
 * Guest additions which doesn't support this feature will instead use the
 * executable image path as argv[0].
 * @sa    VBOX_GUESTCTRL_HF_0_PROCESS_ARGV0
 * @since 6.1.6  */
#define VBOX_GUESTCTRL_GF_0_PROCESS_ARGV0           RT_BIT_64(1)
/** Supports passing cmd / arguments / environment blocks bigger than
 *  GUESTPROCESS_DEFAULT_CMD_LEN / GUESTPROCESS_DEFAULT_ARGS_LEN / GUESTPROCESS_DEFAULT_ENV_LEN (bytes, in total). */
#define VBOX_GUESTCTRL_GF_0_PROCESS_DYNAMIC_SIZES   RT_BIT_64(2)
/** Supports shutting down / rebooting the guest. */
#define VBOX_GUESTCTRL_GF_0_SHUTDOWN                RT_BIT_64(3)
/** VBoxService' toolbox commands (vbox_rm, vbox_stat, ++) are supported by
 * dedicated built-in HGCM commands.
 *
 * The toolbox commands now are being marked as deprecated.
 * @since 7.1 */
#define VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS         RT_BIT_64(4)
/** Supports specifying the working directory for run / start. */
#define VBOX_GUESTCTRL_GF_0_PROCESS_CWD             RT_BIT_64(5)
/** Supports enumerating the guest mount points / drive letters.
 *  @since 7.1 */
#define VBOX_GUESTCTRL_GF_0_MOUNT_POINTS_ENUM       RT_BIT_64(6)
/** Bit that must be set in the 2nd parameter, will be cleared if the host reponds
 * correctly (old hosts might not). */
#define VBOX_GUESTCTRL_GF_1_MUST_BE_ONE             RT_BIT_64(63)
/** @} */

/** @name VBOX_GUESTCTRL_HF_XXX - Host features.
 * @sa GUEST_MSG_REPORT_FEATURES
 * @{ */
/** Host supports the GUEST_FILE_NOTIFYTYPE_READ_OFFSET and
 *  GUEST_FILE_NOTIFYTYPE_WRITE_OFFSET notification types. */
#define VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET      RT_BIT_64(0)
/** Host supports process passing arguments starting at argv[0] rather than
 * argv[1], when the guest additions reports VBOX_GUESTCTRL_GF_0_PROCESS_ARGV0.
 * @since 6.1.6 */
#define VBOX_GUESTCTRL_HF_0_PROCESS_ARGV0           RT_BIT_64(1)
/** Host sends the working directory for run / start, if guest
 * reports VBOX_GUESTCTRL_GF_0_PROCESS_CWD.
 * @since 7.1 */
#define VBOX_GUESTCTRL_HF_0_PROCESS_CWD             RT_BIT_64(2)
/** @} */


/*
 * HGCM parameter structures.
 */
#pragma pack (1)

/**
 * Waits for a host message to arrive. The structure then contains the
 * actual message type + required number of parameters needed to successfully
 * retrieve that host message (in a next round).
 */
typedef struct HGCMMsgWaitFor
{
    VBGLIOCHGCMCALL hdr;
    /** The returned message the host wants to run on the guest. */
    HGCMFunctionParameter msg;       /* OUT uint32_t */
    /** Number of parameters the message needs. */
    HGCMFunctionParameter num_parms; /* OUT uint32_t */
} HGCMMsgWaitFor;

/**
 * Asks the guest control host service to set a message
 * filter for this client. This filter will then only
 * deliver messages to the client which match the
 * wanted context ID (ranges).
 */
typedef struct HGCMMsgFilterSet
{
    VBGLIOCHGCMCALL hdr;
    /** Value to filter for after filter mask was applied. */
    HGCMFunctionParameter value;         /* IN uint32_t */
    /** Mask to add to the current set filter. */
    HGCMFunctionParameter mask_add;      /* IN uint32_t */
    /** Mask to remove from the current set filter. */
    HGCMFunctionParameter mask_remove;   /* IN uint32_t */
    /** Filter flags; currently unused. */
    HGCMFunctionParameter flags;         /* IN uint32_t */
} HGCMMsgFilterSet;

/**
 * Asks the guest control host service to disable
 * a previously set message filter again.
 */
typedef struct HGCMMsgFilterUnset
{
    VBGLIOCHGCMCALL hdr;
    /** Unset flags; currently unused. */
    HGCMFunctionParameter flags;    /* IN uint32_t */
} HGCMMsgFilterUnset;

/**
 * Asks the guest control host service to skip the
 * currently assigned host message returned by
 * VbglR3GuestCtrlMsgWaitFor().
 */
typedef struct HGCMMsgSkip
{
    VBGLIOCHGCMCALL hdr;
    /** Skip flags; currently unused. */
    HGCMFunctionParameter flags;    /* IN uint32_t */
} HGCMMsgSkip;

/**
 * Asks the guest control host service to cancel all pending (outstanding)
 * waits which were not processed yet. This is handy for a graceful shutdown.
 */
typedef struct HGCMMsgCancelPendingWaits
{
    VBGLIOCHGCMCALL hdr;
} HGCMMsgCancelPendingWaits;

/**
 * Generic reply header for reply-based messages.
 *
 * @note Be careful when changing this, as older Guest Additions might depend on this
 *       and other stuff can break, too. So better leave this alone.
 */
typedef struct HGCMReplyHdr
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Message type. */
    HGCMFunctionParameter type;
    /** IPRT result of overall operation. */
    HGCMFunctionParameter rc;
} HGCMReplyHdr;

/** Number of HGCM parameters the HGCMReplyHdr has. */
#define GSTCTL_HGCM_REPLY_HDR_PARMS     3

/**
 * Generic reply message from guest to the host.
 */
typedef struct HGCMMsgReply
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Message type. */
    HGCMFunctionParameter type;
    /** IPRT result of overall operation. */
    HGCMFunctionParameter rc;
    /** Optional payload to this reply
     *  Uses the REPLY_PAYLOAD_XXX structs. */
    HGCMFunctionParameter payload;
} HGCMMsgReply;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Creates a temporary directory / file on the guest.
 */
typedef struct HGCMMsgFsCreateTemp
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Template name to use for file/directory creation.
     *  If \a tmpdir is set, this path will be relative to \a tmpdir and  must not be an absolute path. */
    HGCMFunctionParameter template_name;
    /** Temporary directory to use.
     *  If empty, the guest OS' temporary directory will be determined via IPRT on the guest side. */
    HGCMFunctionParameter tmpdir;
    /** Creation flags.
     *  See GSTCTL_CREATETEMP_F_XXX. */
    HGCMFunctionParameter flags;
    /** File mode to use for creation (ignored if GSTCTL_CREATETEMP_F_SECURE is defined).
     *  See GSTCTL_CREATETEMP_F_XXX. */
    HGCMFunctionParameter mode;
} HGCMMsgFsCreateTemp;

/**
 * Queries information of a file system on the guest.
 */
typedef struct HGCMMsgFsQueryInfo
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Path to query file system information for. */
    HGCMFunctionParameter path;
} HGCMMsgFsQueryInfo;

/**
 * Queries information for a file system object on the guest.
 */
typedef struct HGCMMsgFsObjQueryInfo
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Path to query information for. */
    HGCMFunctionParameter path;
    /** Additional file system attributes to lookup (GSTCTLFSOBJATTRADD). */
    HGCMFunctionParameter add_attributes;
    /** Flags (GSTCTL_PATH_F_XXX). */
    HGCMFunctionParameter flags;
} HGCMMsgFsObjQueryInfo;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

/**
 * Creates a guest session.
 */
typedef struct HGCMMsgSessionOpen
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The guest control protocol version this
     *  session is about to use. */
    HGCMFunctionParameter protocol;
    /** The user name to run the guest session under. */
    HGCMFunctionParameter username;
    /** The user's password. */
    HGCMFunctionParameter password;
    /** The domain to run the guest session under. */
    HGCMFunctionParameter domain;
    /** Session creation flags. */
    HGCMFunctionParameter flags;
} HGCMMsgSessionOpen;

/**
 * Terminates (closes) a guest session.
 */
typedef struct HGCMMsgSessionClose
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Session termination flags. */
    HGCMFunctionParameter flags;
} HGCMMsgSessionClose;

/**
 * Reports back a guest session's status.
 */
typedef struct HGCMMsgSessionNotify
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Notification type. */
    HGCMFunctionParameter type;
    /** Notification result. */
    HGCMFunctionParameter result;
} HGCMMsgSessionNotify;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Opens a guest directory.
 */
typedef struct HGCMMsgDirOpen
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Path of directory to open. */
    HGCMFunctionParameter path;
    /** Filter type to use when walking the directory (GSTCTLDIRFILTER). */
    HGCMFunctionParameter filter;
    /** Directory open flags (GSTCTLDIR_F_XXX). */
    HGCMFunctionParameter flags;
    /** Additional directory attributes to use
     *  (GSTCTLFSOBJATTRADD, for subsequent directory entry read calls). */
    HGCMFunctionParameter read_attr_add;
    /** Directory reading flags (for subsequent directory entry read calls).
     *  GSTCTL_PATH_F_ON_LINK or GSTCTL_PATH_F_FOLLOW_LINK. */
    HGCMFunctionParameter read_flags;
} HGCMMsgDirOpen;

/**
 * Closes a guest directory.
 */
typedef struct HGCMMsgDirClose
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Directory handle to close. */
    HGCMFunctionParameter handle;
} HGCMMsgDirClose;

/**
 * Reads the next entry of a guest directory.
 */
typedef struct HGCMMsgDirRead
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Handle of directory listing to read the next entry for. */
    HGCMFunctionParameter handle;
} HGCMMsgDirRead;

/**
 * Rewinds the listing of a guest directory.
 */
typedef struct HGCMMsgDirRewind
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Handle of directory listing to rewind. */
    HGCMFunctionParameter handle;
} HGCMMsgDirRewind;

/**
 * Creates a directory on the guest.
 */
typedef struct HGCMMsgDirCreate
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Path of directory to create. */
    HGCMFunctionParameter path;
    /** Creation mode. */
    HGCMFunctionParameter mode;
    /** Creation flags (GSTCTL_CREATEDIRECTORY_F_XXX). */
    HGCMFunctionParameter flags;
} HGCMMsgDirCreate;

/**
 * Lists the entries of a directory on the guest.
 */
typedef struct HGCMMsgDirList
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Handle of directory listing to list. */
    HGCMFunctionParameter handle;
    /** Number of entries to read at once.
     *  Specify UINT32_MAX to read as much as possible. 0 is not allowed. */
    HGCMFunctionParameter num_entries;
    /** Listing flags (GSTCTL_DIRLIST_F_XXX). */
    HGCMFunctionParameter flags;
} HGCMMsgDirList;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

/**
 * Renames a path on the guest.
 */
typedef struct HGCMMsgPathRename
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** Source to rename. */
    HGCMFunctionParameter source;
    /** Destination to rename source to. */
    HGCMFunctionParameter dest;
    /** UInt32: Rename flags. */
    HGCMFunctionParameter flags;
} HGCMMsgPathRename;

/**
 * Retrieves the user's personal documents directory from the guest.
 */
typedef struct HGCMMsgPathUserDocuments
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
} HGCMMsgPathUserDocuments;

/**
 * Retrieves the user's home directory from the guest.
 */
typedef struct HGCMMsgPathUserHome
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
} HGCMMsgPathUserHome;

/**
 * Retrieves mount points / drive letters from the guest.
 *
 * @since 7.1
 */
typedef struct HGCMMsgMountPoints
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** UInt32: Flags; currently unused. */
    HGCMFunctionParameter flags;
} HGCMMsgMountPoints;

/**
 * Shuts down / reboots the guest.
 *
 * @since 7.0
 */
typedef struct HGCMMsgShutdown
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** UInt32: Action flags. */
    HGCMFunctionParameter action;
} HGCMMsgShutdown;

/**
 * Executes a command inside the guest.
 */
typedef struct HGCMMsgProcExec
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The command to execute on the guest. */
    HGCMFunctionParameter cmd;
    /** Execution flags (see IGuest::ProcessCreateFlag_*). */
    HGCMFunctionParameter flags;
    /** Number of arguments. */
    HGCMFunctionParameter num_args;
    /** The actual arguments. */
    HGCMFunctionParameter args;
    /** Number of environment value pairs. */
    HGCMFunctionParameter num_env;
    /** Size (in bytes) of environment block, including terminating zeros. */
    HGCMFunctionParameter cb_env;
    /** The actual environment block. */
    HGCMFunctionParameter env;
    union
    {
        struct
        {
            /** The user name to run the executed command under.
             *  Only for VBox < 4.3 hosts. */
            HGCMFunctionParameter username;
            /** The user's password.
             *  Only for VBox < 4.3 hosts. */
            HGCMFunctionParameter password;
            /** Timeout (in msec) which either specifies the
             *  overall lifetime of the process or how long it
             *  can take to bring the process up and running -
             *  (depends on the IGuest::ProcessCreateFlag_*). */
            HGCMFunctionParameter timeout;
        } v1;
        struct
        {
            /** Timeout (in ms) which either specifies the
             *  overall lifetime of the process or how long it
             *  can take to bring the process up and running -
             *  (depends on the IGuest::ProcessCreateFlag_*). */
            HGCMFunctionParameter timeout;
            /** Process priority. */
            HGCMFunctionParameter priority;
            /** Number of process affinity blocks. */
            HGCMFunctionParameter num_affinity;
            /** Pointer to process affinity blocks (uint64_t). */
            HGCMFunctionParameter affinity;
        } v2;
    } u;
    /** Working directory request, filled if guest
     *  reports VBOX_GUESTCTRL_GF_0_PROCESS_CWD.
     *  @since 7.1 */
    HGCMFunctionParameter cwd;
} HGCMMsgProcExec;

/**
 * Sends input to a guest process via stdin.
 */
typedef struct HGCMMsgProcInput
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID) to send the input to. */
    HGCMFunctionParameter pid;
    /** Input flags (see IGuest::ProcessInputFlag_*). */
    HGCMFunctionParameter flags;
    /** Data buffer. */
    HGCMFunctionParameter data;
    /** Actual size of data (in bytes). */
    HGCMFunctionParameter size;
} HGCMMsgProcInput;

/**
 * Retrieves ouptut from a previously executed process
 * from stdout/stderr.
 */
typedef struct HGCMMsgProcOutput
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID). */
    HGCMFunctionParameter pid;
    /** The pipe handle ID (stdout/stderr). */
    HGCMFunctionParameter handle;
    /** Optional flags. */
    HGCMFunctionParameter flags;
    /** Data buffer. */
    HGCMFunctionParameter data;
} HGCMMsgProcOutput;

/**
 * Reports the current status of a guest process.
 */
typedef struct HGCMMsgProcStatus
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID). */
    HGCMFunctionParameter pid;
    /** The process status. */
    HGCMFunctionParameter status;
    /** Optional flags (based on status). */
    HGCMFunctionParameter flags;
    /** Optional data buffer (not used atm). */
    HGCMFunctionParameter data;
} HGCMMsgProcStatus;

/**
 * Reports back the status of data written to a process.
 */
typedef struct HGCMMsgProcStatusInput
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID). */
    HGCMFunctionParameter pid;
    /** Status of the operation. */
    HGCMFunctionParameter status;
    /** Optional flags. */
    HGCMFunctionParameter flags;
    /** Data written. */
    HGCMFunctionParameter written;
} HGCMMsgProcStatusInput;

/*
 * Guest control 2.0 messages.
 */

/**
 * Terminates a guest process.
 */
typedef struct HGCMMsgProcTerminate
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID). */
    HGCMFunctionParameter pid;
} HGCMMsgProcTerminate;

/**
 * Waits for certain events to happen.
 */
typedef struct HGCMMsgProcWaitFor
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** The process ID (PID). */
    HGCMFunctionParameter pid;
    /** Wait (event) flags. */
    HGCMFunctionParameter flags;
    /** Timeout (in ms). */
    HGCMFunctionParameter timeout;
} HGCMMsgProcWaitFor;

typedef struct HGCMMsgDirRemove
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** Directory to remove. */
    HGCMFunctionParameter path;
    /** UInt32: Removement flags. */
    HGCMFunctionParameter flags;
} HGCMMsgDirRemove;

/**
 * Opens a guest file.
 */
typedef struct HGCMMsgFileOpen
{
    VBGLIOCHGCMCALL hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** File to open. */
    HGCMFunctionParameter filename;
    /** Open mode. */
    HGCMFunctionParameter openmode;
    /** Disposition mode. */
    HGCMFunctionParameter disposition;
    /** Sharing mode. */
    HGCMFunctionParameter sharing;
    /** UInt32: Creation mode. */
    HGCMFunctionParameter creationmode;
    /** UInt64: Initial offset. */
    HGCMFunctionParameter offset;
} HGCMMsgFileOpen;

/**
 * Closes a guest file.
 */
typedef struct HGCMMsgFileClose
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to close. */
    HGCMFunctionParameter handle;
} HGCMMsgFileClose;

/**
 * Reads from a guest file.
 */
typedef struct HGCMMsgFileRead
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to read from. */
    HGCMFunctionParameter handle;
    /** Size (in bytes) to read. */
    HGCMFunctionParameter size;
} HGCMMsgFileRead;

/**
 * Reads at a specified offset from a guest file.
 */
typedef struct HGCMMsgFileReadAt
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to read from. */
    HGCMFunctionParameter handle;
    /** Offset where to start reading from. */
    HGCMFunctionParameter offset;
    /** Actual size of data (in bytes). */
    HGCMFunctionParameter size;
} HGCMMsgFileReadAt;

/**
 * Writes to a guest file.
 */
typedef struct HGCMMsgFileWrite
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to write to. */
    HGCMFunctionParameter handle;
    /** Actual size of data (in bytes). */
    HGCMFunctionParameter size;
    /** Data buffer to write to the file. */
    HGCMFunctionParameter data;
} HGCMMsgFileWrite;

/**
 * Writes at a specified offset to a guest file.
 */
typedef struct HGCMMsgFileWriteAt
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to write to. */
    HGCMFunctionParameter handle;
    /** Offset where to start reading from. */
    HGCMFunctionParameter offset;
    /** Actual size of data (in bytes). */
    HGCMFunctionParameter size;
    /** Data buffer to write to the file. */
    HGCMFunctionParameter data;
} HGCMMsgFileWriteAt;

/**
 * Seeks the read/write position of a guest file.
 */
typedef struct HGCMMsgFileSeek
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to seek. */
    HGCMFunctionParameter handle;
    /** The seeking method. */
    HGCMFunctionParameter method;
    /** The seeking offset. */
    HGCMFunctionParameter offset;
} HGCMMsgFileSeek;

/**
 * Tells the current read/write position of a guest file.
 */
typedef struct HGCMMsgFileTell
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** File handle to get the current position for. */
    HGCMFunctionParameter handle;
} HGCMMsgFileTell;

/**
 * Changes the file size.
 */
typedef struct HGCMMsgFileSetSize
{
    VBGLIOCHGCMCALL         Hdr;
    /** Context ID. */
    HGCMFunctionParameter   id32Context;
    /** File handle to seek. */
    HGCMFunctionParameter   id32Handle;
    /** The new file size. */
    HGCMFunctionParameter   cb64NewSize;
} HGCMMsgFileSetSize;

/**
 * Removes (deletes) a guest file.
 *
 * @since 7.1
 */
typedef struct HGCMMsgFileRemove
{
    VBGLIOCHGCMCALL       hdr;
    /** UInt32: Context ID. */
    HGCMFunctionParameter context;
    /** File to open. */
    HGCMFunctionParameter filename;
} HGCMMsgFileRemove;


/******************************************************************************
* HGCM replies from the guest. These are handled in Main's low-level HGCM     *
* callbacks and dispatched to the appropriate guest object.                   *
******************************************************************************/

/**
 * Reply from a guest file operation.
 */
typedef struct HGCMReplyFileNotify
{
    VBGLIOCHGCMCALL hdr;
    /** Context ID. */
    HGCMFunctionParameter context;
    /** Notification type. */
    HGCMFunctionParameter type;
    /** IPRT result of overall operation. */
    HGCMFunctionParameter rc;
    union
    {
        struct
        {
            /** Guest file handle. */
            HGCMFunctionParameter handle;
        } open;
        /** Note: Close does not have any additional data (yet). */
        struct
        {
            /** Actual data read (if any). */
            HGCMFunctionParameter data;
        } read;
        struct
        {
            /** Actual data read (if any). */
            HGCMFunctionParameter pvData;
            /** The new file offset (signed).  Negative value if non-seekable files. */
            HGCMFunctionParameter off64New;
        } ReadOffset;
        struct
        {
            /** How much data (in bytes) have been successfully written. */
            HGCMFunctionParameter written;
        } write;
        struct
        {
            /** Number of bytes that was successfully written. */
            HGCMFunctionParameter cb32Written;
            /** The new file offset (signed).  Negative value if non-seekable files. */
            HGCMFunctionParameter off64New;
        } WriteOffset;
        struct
        {
            HGCMFunctionParameter offset;
        } seek;
        struct
        {
            HGCMFunctionParameter offset;
        } tell;
        struct
        {
            HGCMFunctionParameter cb64Size;
        } SetSize;
    } u;
} HGCMReplyFileNotify;

/**
 * Reply from a guest directory operation.
 */
typedef struct HGCMReplyDirNotify
{
    /** The generic reply header. */
    HGCMReplyHdr reply_hdr;
    /** Union based on \a reply_hdr.type. */
    union
    {
        /**
         * Parameters used for \a reply_hdr.type GUEST_DIR_NOTIFYTYPE_OPEN.
         *
         * @since 7.1
         */
        struct
        {
            /** Guest directory handle. */
            HGCMFunctionParameter handle;
        } open;
        /**
         * Parameters used for \a reply_hdr.type GUEST_DIR_NOTIFYTYPE_READ.
         *
         * @since 7.1
         */
        struct
        {
            /** Current read directory entry (GSTCTLDIRENTRYEX). */
            HGCMFunctionParameter entry;
            /** Resolved user ID as a string (uid). */
            HGCMFunctionParameter user;
            /** Resolved group IDs as a string.
             *
             *  Multiple groups are delimited by GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR, whereas
             *  the first group always is the primary group. */
            HGCMFunctionParameter groups;
        } read;
        /**
         * Parameters used for \a reply_hdr.type GUEST_DIR_NOTIFYTYPE_LIST.
         *
         * @since 7.1
         */
        struct
        {
            /** Number of entries in \a buffer. */
            HGCMFunctionParameter num_entries;
            /** Buffer containing the GSTCTLDIRENTRYEX entries, immediately followed
             *  by resolved user + groups as a string (empty strings if not resolved). */
            HGCMFunctionParameter buffer;
        } list;
    } u;
} HGCMReplyDirNotify;

/**
 * Reply to a HOST_MSG_FS_QUERY_INFO or HOST_MSG_FS_CREATE_TEMP message.
 *
 * @since 7.1
 */
typedef struct HGCMReplyFsNotify
{
    /** The generic reply header. */
    HGCMReplyHdr reply_hdr;
    /** Union based on \a reply_hdr.type. */
    union
    {
        /**
         * Parameters used for \a type GUEST_FS_NOTIFYTYPE_CREATE_TEMP.
         *
         * @since 7.1
         */
        struct
        {
            /** The create temporary file / directory when \a rc
             *  indicates success. */
            HGCMFunctionParameter path;
        } createtemp;
        /**
         * Parameters used for \a type GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO.
         *
         * @since 7.1
         */
        struct
        {
            /** File system object information (GSTCTLFSOBJINFO). */
            HGCMFunctionParameter obj_info;
            /** Resolved user ID as a string (uid). */
            HGCMFunctionParameter user;
            /** Resolved group IDs as a string.
             *
             *  Multiple groups are delimited by GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR, whereas
             *  the first group always is the primary group. */
            HGCMFunctionParameter groups;
        } queryobjinfo;
        /**
         * Parameters used for \a type GUEST_FS_NOTIFYTYPE_QUERY_INFO.
         *
         * @since 7.1
         */
        struct
        {
            /** File system object information (GSTCTLFSINFO). */
            HGCMFunctionParameter fs_info;
        } queryinfo;
    } u;
} HGCMReplyFsNotify;
#pragma pack ()
} /* namespace guestControl */

#endif /* !VBOX_INCLUDED_HostServices_GuestControlSvc_h */
