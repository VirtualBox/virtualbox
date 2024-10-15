/* $Id: VBoxServiceControl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceControl.h - Internal guest control definitions.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_common_VBoxService_VBoxServiceControl_h
#define GA_INCLUDED_SRC_common_VBoxService_VBoxServiceControl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
# include <iprt/dir.h>
#endif
#include <iprt/list.h>
#include <iprt/req.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/GuestControl.h>
#include <VBox/HostServices/GuestControlSvc.h>

#include "VBoxServiceUtils.h" /* For VGSVCIDCACHE. */


/**
 * Pipe IDs for handling the guest process poll set.
 */
typedef enum VBOXSERVICECTRLPIPEID
{
    VBOXSERVICECTRLPIPEID_UNKNOWN           = 0,
    VBOXSERVICECTRLPIPEID_STDIN             = 10,
    VBOXSERVICECTRLPIPEID_STDIN_WRITABLE    = 11,
    /** Pipe for reading from guest process' stdout. */
    VBOXSERVICECTRLPIPEID_STDOUT            = 40,
    /** Pipe for reading from guest process' stderr. */
    VBOXSERVICECTRLPIPEID_STDERR            = 50,
    /** Notification pipe for waking up the guest process
     *  control thread. */
    VBOXSERVICECTRLPIPEID_IPC_NOTIFY        = 100
} VBOXSERVICECTRLPIPEID;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Structure for one (opened) guest directory.
 */
typedef struct VBOXSERVICECTRLDIR
{
    /** Pointer to list archor of following list node.
     *  @todo Would be nice to have a RTListGetAnchor(). */
    PRTLISTANCHOR                   pAnchor;
    /** Node to global guest control directory list. */
    /** @todo Use a map later? */
    RTLISTNODE                      Node;
    /** The (absolute) directory path. */
    char                           *pszPathAbs;
    /** The directory handle on the guest. */
    RTDIR                           hDir;
    /** Directory handle to identify this directory. */
    uint32_t                        uHandle;
    /** Context ID. */
    uint32_t                        uContextID;
    /** Flags for reading directory entries. */
    uint32_t                        fRead;
    /** Additional attributes enumeration to use for reading directory entries. */
    GSTCTLFSOBJATTRADD              enmReadAttrAdd;
    /** Scratch buffer for holding the directory reading entry.
     *  Currently NOT serialized, i.e. only can be used for one read at a time. */
    PRTDIRENTRYEX                   pDirEntryEx;
    /** Size (in bytes) of \a pDirEntryEx. */
    size_t                          cbDirEntryEx;
} VBOXSERVICECTRLDIR;
/** Pointer to a guest directory. */
typedef VBOXSERVICECTRLDIR *PVBOXSERVICECTRLDIR;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

/**
 * Structure for one (opened) guest file.
 */
typedef struct VBOXSERVICECTRLFILE
{
    /** Pointer to list archor of following list node.
     *  @todo Would be nice to have a RTListGetAnchor(). */
    PRTLISTANCHOR                   pAnchor;
    /** Node to global guest control file list. */
    /** @todo Use a map later? */
    RTLISTNODE                      Node;
    /** The file name. */
    char                           *pszName;
    /** The file handle on the guest. */
    RTFILE                          hFile;
    /** File handle to identify this file. */
    uint32_t                        uHandle;
    /** Context ID. */
    uint32_t                        uContextID;
    /** RTFILE_O_XXX flags. */
    uint64_t                        fOpen;
} VBOXSERVICECTRLFILE;
/** Pointer to a guest file. */
typedef VBOXSERVICECTRLFILE *PVBOXSERVICECTRLFILE;

/**
 * Structure for a guest session thread to
 * observe/control the forked session instance from
 * the VBoxService main executable.
 */
typedef struct VBOXSERVICECTRLSESSIONTHREAD
{
    /** Node to global guest control session list. */
    /** @todo Use a map later? */
    RTLISTNODE                      Node;
    /** The sessions's startup info. */
    PVBGLR3GUESTCTRLSESSIONSTARTUPINFO
                                    pStartupInfo;
    /** Critical section for thread-safe use. */
    RTCRITSECT                      CritSect;
    /** The worker thread. */
    RTTHREAD                        Thread;
    /** Process handle for forked child. */
    RTPROCESS                       hProcess;
    /** Shutdown indicator; will be set when the thread
      * needs (or is asked) to shutdown. */
    bool volatile                   fShutdown;
    /** Indicator set by the service thread exiting. */
    bool volatile                   fStopped;
    /** Whether the thread was started or not. */
    bool                            fStarted;
#if 0 /* Pipe IPC not used yet. */
    /** Pollset containing all the pipes. */
    RTPOLLSET                       hPollSet;
    RTPIPE                          hStdInW;
    RTPIPE                          hStdOutR;
    RTPIPE                          hStdErrR;
    struct StdPipe
    {
        RTHANDLE  hChild;
        PRTHANDLE phChild;
    }                               StdIn,
                                    StdOut,
                                    StdErr;
    /** The notification pipe associated with this guest session.
     *  This is NIL_RTPIPE for output pipes. */
    RTPIPE                          hNotificationPipeW;
    /** The other end of hNotificationPipeW. */
    RTPIPE                          hNotificationPipeR;
#endif
    /** Pipe for handing the secret key to the session process. */
    RTPIPE                          hKeyPipe;
    /** Secret key. */
    uint8_t                         abKey[_4K];
} VBOXSERVICECTRLSESSIONTHREAD;
/** Pointer to thread data. */
typedef VBOXSERVICECTRLSESSIONTHREAD *PVBOXSERVICECTRLSESSIONTHREAD;

/** Defines the prefix being used for telling our service executable that we're going
 *  to spawn a new (Guest Control) user session. */
#define VBOXSERVICECTRLSESSION_GETOPT_PREFIX             "guestsession"

/** Flag indicating that this session has been spawned from
 *  the main executable. */
#define VBOXSERVICECTRLSESSION_FLAG_SPAWN                RT_BIT(0)
/** Flag indicating that this session is anonymous, that is,
 *  it will run start guest processes with the same credentials
 *  as the main executable. */
#define VBOXSERVICECTRLSESSION_FLAG_ANONYMOUS            RT_BIT(1)
/** Flag indicating that started guest processes will dump their
 *  stdout output to a separate file on disk. For debugging. */
#define VBOXSERVICECTRLSESSION_FLAG_DUMPSTDOUT           RT_BIT(2)
/** Flag indicating that started guest processes will dump their
 *  stderr output to a separate file on disk. For debugging. */
#define VBOXSERVICECTRLSESSION_FLAG_DUMPSTDERR           RT_BIT(3)

/**
 * Structure for maintaining a guest session. This also
 * contains all started threads (e.g. for guest processes).
 *
 * This structure can act in two different ways:
 * - For legacy guest control handling (protocol version < 2)
 *   this acts as a per-guest process structure containing all
 *   the information needed to get a guest process up and running.
 * - For newer guest control protocols (>= 2) this structure is
 *   part of the forked session child, maintaining all guest
 *   control objects under it.
 */
typedef struct VBOXSERVICECTRLSESSION
{
    /* The session's startup information. */
    VBGLR3GUESTCTRLSESSIONSTARTUPINFO
                                    StartupInfo;
    /** List of active guest process threads
     *  (VBOXSERVICECTRLPROCESS). */
    RTLISTANCHOR                    lstProcesses;
    /** Number of guest processes in the process list. */
    uint32_t                        cProcesses;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /** List of guest control files (VBOXSERVICECTRLDIR). */
    RTLISTANCHOR                    lstDirs;
    /** Number of guest directories in \a lstDirs. */
    uint32_t                        cDirs;
#endif
    /** List of guest control files (VBOXSERVICECTRLFILE). */
    RTLISTANCHOR                    lstFiles;
    /** Number of guest files in \a lstFiles. */
    uint32_t                        cFiles;
    /** The session's critical section. */
    RTCRITSECT                      CritSect;
    /** Internal session flags, not related
     *  to StartupInfo stuff.
     *  @sa VBOXSERVICECTRLSESSION_FLAG_* flags. */
    uint32_t                        fFlags;
    /** How many processes do we allow keeping around at a time? */
    uint32_t                        uProcsMaxKept;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    /** The uid cache for this session. */
    VGSVCIDCACHE                    UidCache;
    /** The gid cache for this session. */
    VGSVCIDCACHE                    GidCache;
#endif
} VBOXSERVICECTRLSESSION;
/** Pointer to guest session. */
typedef VBOXSERVICECTRLSESSION *PVBOXSERVICECTRLSESSION;

/**
 * Structure for holding data for one (started) guest process.
 */
typedef struct VBOXSERVICECTRLPROCESS
{
    /** Node. */
    RTLISTNODE                      Node;
    /** Process handle. */
    RTPROCESS                       hProcess;
    /** Number of references using this struct. */
    uint32_t                        cRefs;
    /** The worker thread. */
    RTTHREAD                        Thread;
    /** The session this guest process
     *  is bound to. */
    PVBOXSERVICECTRLSESSION         pSession;
    /** Shutdown indicator; will be set when the thread
      * needs (or is asked) to shutdown. */
    bool volatile                   fShutdown;
    /** Whether the guest process thread was stopped or not. */
    bool volatile                   fStopped;
    /** Whether the guest process thread was started or not. */
    bool                            fStarted;
    /** Context ID. */
    uint32_t                        uContextID;
    /** Critical section for thread-safe use. */
    RTCRITSECT                      CritSect;
    /** Process startup information. */
    PVBGLR3GUESTCTRLPROCSTARTUPINFO
                                    pStartupInfo;
    /** The process' PID assigned by the guest OS. */
    uint32_t                        uPID;
    /** The process' request queue to handle requests
     *  from the outside, e.g. the session. */
    RTREQQUEUE                      hReqQueue;
    /** Our pollset, used for accessing the process'
     *  std* pipes + the notification pipe. */
    RTPOLLSET                       hPollSet;
    /** StdIn pipe for addressing writes to the
     *  guest process' stdin.*/
    RTPIPE                          hPipeStdInW;
    /** StdOut pipe for addressing reads from
     *  guest process' stdout.*/
    RTPIPE                          hPipeStdOutR;
    /** StdOut pipe for addressing reads from
     *  guest process' stderr.*/
    RTPIPE                          hPipeStdErrR;

    /** The write end of the notification pipe that is used to poke the thread
     * monitoring the process.
     *  This is NIL_RTPIPE for output pipes. */
    RTPIPE                          hNotificationPipeW;
    /** The other end of hNotificationPipeW, read by vgsvcGstCtrlProcessProcLoop(). */
    RTPIPE                          hNotificationPipeR;
} VBOXSERVICECTRLPROCESS;
/** Pointer to thread data. */
typedef VBOXSERVICECTRLPROCESS *PVBOXSERVICECTRLPROCESS;

RT_C_DECLS_BEGIN

extern RTLISTANCHOR             g_lstControlSessionThreads;
extern VBOXSERVICECTRLSESSION   g_Session;
extern uint32_t                 g_idControlSvcClient;
extern uint64_t                 g_fControlHostFeatures0;
extern bool                     g_fControlSupportsOptimizations;


/** @name Guest session thread handling.
 * @{ */
extern int                      VGSvcGstCtrlSessionThreadCreate(PRTLISTANCHOR pList, const PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pSessionStartupInfo, PVBOXSERVICECTRLSESSIONTHREAD *ppSessionThread);
extern int                      VGSvcGstCtrlSessionThreadDestroy(PVBOXSERVICECTRLSESSIONTHREAD pSession, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionThreadDestroyAll(PRTLISTANCHOR pList, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionThreadTerminate(PVBOXSERVICECTRLSESSIONTHREAD pSession);
extern RTEXITCODE               VGSvcGstCtrlSessionSpawnInit(int argc, char **argv);
/** @} */
/** @name Per-session functions.
 * @{ */
extern PVBOXSERVICECTRLPROCESS  VGSvcGstCtrlSessionRetainProcess(PVBOXSERVICECTRLSESSION pSession, uint32_t uPID);
extern int                      VGSvcGstCtrlSessionClose(PVBOXSERVICECTRLSESSION pSession);
extern int                      VGSvcGstCtrlSessionDestroy(PVBOXSERVICECTRLSESSION pSession);
extern int                      VGSvcGstCtrlSessionInit(PVBOXSERVICECTRLSESSION pSession, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionHandler(PVBOXSERVICECTRLSESSION pSession, uint32_t uMsg, PVBGLR3GUESTCTRLCMDCTX pHostCtx, void *pvScratchBuf, size_t cbScratchBuf, volatile bool *pfShutdown);
extern int                      VGSvcGstCtrlSessionProcessAdd(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlSessionProcessRemove(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlSessionProcessStartAllowed(const PVBOXSERVICECTRLSESSION pSession, bool *pfAllowed);
extern int                      VGSvcGstCtrlSessionReapProcesses(PVBOXSERVICECTRLSESSION pSession);
/** @} */
/** @name Per-guest process functions.
 * @{ */
extern int                      VGSvcGstCtrlProcessFree(PVBOXSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessHandleInput(PVBOXSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx, bool fPendingClose, void *pvBuf, uint32_t cbBuf);
extern int                      VGSvcGstCtrlProcessHandleOutput(PVBOXSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx, uint32_t uHandle, uint32_t cbToRead, uint32_t uFlags);
extern int                      VGSvcGstCtrlProcessHandleTerm(PVBOXSERVICECTRLPROCESS pProcess);
extern void                     VGSvcGstCtrlProcessRelease(PVBOXSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessStart(const PVBOXSERVICECTRLSESSION pSession, const PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo, uint32_t uContext);
extern int                      VGSvcGstCtrlProcessStop(PVBOXSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessWait(const PVBOXSERVICECTRLPROCESS pProcess, RTMSINTERVAL msTimeout, int *pRc);
/** @} */

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_common_VBoxService_VBoxServiceControl_h */

