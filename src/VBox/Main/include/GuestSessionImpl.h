/* $Id: GuestSessionImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest session handling.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestSessionImpl_h
#define MAIN_INCLUDED_GuestSessionImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestSessionWrap.h"
#include "EventImpl.h"

#include "GuestCtrlImplPrivate.h"
#include "GuestProcessImpl.h"
#include "GuestDirectoryImpl.h"
#include "GuestFileImpl.h"
#include "GuestFsObjInfoImpl.h"
#include "GuestSessionImplTasks.h"

#include <iprt/asm.h> /** @todo r=bird: Needed for ASMBitSet() in GuestSession::Data constructor.  Removed when
                       *        that is moved into the class implementation file as it should be. */
#include <deque>

class GuestSessionTaskInternalStart; /* Needed for i_startSessionThreadTask(). */

/**
 * Guest session implementation.
 */
class ATL_NO_VTABLE GuestSession
    : public GuestSessionWrap
    , public GuestBase
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestSession)

    int     init(Guest *pGuest, const GuestSessionStartupInfo &ssInfo, const GuestCredentials &guestCreds);
    void    uninit(void);
    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

private:

    /** Wrapped @name IGuestSession properties.
     * @{ */
    HRESULT getUser(com::Utf8Str &aUser);
    HRESULT getDomain(com::Utf8Str &aDomain);
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getId(ULONG *aId);
    HRESULT getTimeout(ULONG *aTimeout);
    HRESULT setTimeout(ULONG aTimeout);
    HRESULT getProtocolVersion(ULONG *aProtocolVersion);
    HRESULT getStatus(GuestSessionStatus_T *aStatus);
    HRESULT getEnvironmentChanges(std::vector<com::Utf8Str> &aEnvironmentChanges);
    HRESULT setEnvironmentChanges(const std::vector<com::Utf8Str> &aEnvironmentChanges);
    HRESULT getEnvironmentBase(std::vector<com::Utf8Str> &aEnvironmentBase);
    HRESULT getProcesses(std::vector<ComPtr<IGuestProcess> > &aProcesses);
    HRESULT getPathStyle(PathStyle_T *aPathStyle);
    HRESULT getCurrentDirectory(com::Utf8Str &aCurrentDirectory);
    HRESULT setCurrentDirectory(const com::Utf8Str &aCurrentDirectory);
    HRESULT getUserDocuments(com::Utf8Str &aUserDocuments);
    HRESULT getUserHome(com::Utf8Str &aUserHome);
    HRESULT getMountPoints(std::vector<com::Utf8Str> &aMountPoints);
    HRESULT getDirectories(std::vector<ComPtr<IGuestDirectory> > &aDirectories);
    HRESULT getFiles(std::vector<ComPtr<IGuestFile> > &aFiles);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    /** @}  */

    /** Wrapped @name IGuestSession methods.
     * @{ */
    HRESULT close();

    HRESULT copyFromGuest(const std::vector<com::Utf8Str> &aSources,
                          const std::vector<com::Utf8Str> &aFilters,
                          const std::vector<com::Utf8Str> &aFlags,
                          const com::Utf8Str &aDestination,
                          ComPtr<IProgress> &aProgress);
    HRESULT copyToGuest(const std::vector<com::Utf8Str> &aSources,
                        const std::vector<com::Utf8Str> &aFilters,
                        const std::vector<com::Utf8Str> &aFlags,
                        const com::Utf8Str &aDestination,
                        ComPtr<IProgress> &aProgress);

    HRESULT directoryCopy(const com::Utf8Str &aSource,
                          const com::Utf8Str &aDestination,
                          const std::vector<DirectoryCopyFlag_T> &aFlags,
                          ComPtr<IProgress> &aProgress);
    HRESULT directoryCopyFromGuest(const com::Utf8Str &aSource,
                                   const com::Utf8Str &aDestination,
                                   const std::vector<DirectoryCopyFlag_T> &aFlags,
                                   ComPtr<IProgress> &aProgress);
    HRESULT directoryCopyToGuest(const com::Utf8Str &aSource,
                                 const com::Utf8Str &aDestination,
                                 const std::vector<DirectoryCopyFlag_T> &aFlags,
                                 ComPtr<IProgress> &aProgress);
    HRESULT directoryCreate(const com::Utf8Str &aPath,
                            ULONG aMode,
                            const std::vector<DirectoryCreateFlag_T> &aFlags);
    HRESULT directoryCreateTemp(const com::Utf8Str &aTemplateName,
                                ULONG aMode,
                                const com::Utf8Str &aPath,
                                BOOL aSecure,
                                com::Utf8Str &aDirectory);
    HRESULT directoryExists(const com::Utf8Str &aPath,
                            BOOL aFollowSymlinks,
                            BOOL *aExists);
    HRESULT directoryOpen(const com::Utf8Str &aPath,
                          const com::Utf8Str &aFilter,
                          const std::vector<DirectoryOpenFlag_T> &aFlags,
                          ComPtr<IGuestDirectory> &aDirectory);
    HRESULT directoryRemove(const com::Utf8Str &aPath);
    HRESULT directoryRemoveRecursive(const com::Utf8Str &aPath,
                                     const std::vector<DirectoryRemoveRecFlag_T> &aFlags,
                                     ComPtr<IProgress> &aProgress);
    HRESULT environmentScheduleSet(const com::Utf8Str &aName,
                                   const com::Utf8Str &aValue);
    HRESULT environmentScheduleUnset(const com::Utf8Str &aName);
    HRESULT environmentGetBaseVariable(const com::Utf8Str &aName,
                                       com::Utf8Str &aValue);
    HRESULT environmentDoesBaseVariableExist(const com::Utf8Str &aName,
                                             BOOL *aExists);

    HRESULT fileCopy(const com::Utf8Str &aSource,
                     const com::Utf8Str &aDestination,
                     const std::vector<FileCopyFlag_T> &aFlags,
                     ComPtr<IProgress> &aProgress);
    HRESULT fileCopyToGuest(const com::Utf8Str &aSource,
                            const com::Utf8Str &aDestination,
                            const std::vector<FileCopyFlag_T> &aFlags,
                            ComPtr<IProgress> &aProgress);
    HRESULT fileCopyFromGuest(const com::Utf8Str &aSource,
                              const com::Utf8Str &aDestination,
                              const std::vector<FileCopyFlag_T> &aFlags,
                              ComPtr<IProgress> &aProgress);
    HRESULT fileCreateTemp(const com::Utf8Str &aTemplateName,
                           ULONG aMode,
                           const com::Utf8Str &aPath,
                           BOOL aSecure,
                           ComPtr<IGuestFile> &aFile);
    HRESULT fileExists(const com::Utf8Str &aPath,
                       BOOL aFollowSymlinks,
                       BOOL *aExists);
    HRESULT fileOpen(const com::Utf8Str &aPath,
                     FileAccessMode_T aAccessMode,
                     FileOpenAction_T aOpenAction,
                     ULONG aCreationMode,
                     ComPtr<IGuestFile> &aFile);
    HRESULT fileOpenEx(const com::Utf8Str &aPath,
                       FileAccessMode_T aAccessMode,
                       FileOpenAction_T aOpenAction,
                       FileSharingMode_T aSharingMode,
                       ULONG aCreationMode,
                       const std::vector<FileOpenExFlag_T> &aFlags,
                       ComPtr<IGuestFile> &aFile);
    HRESULT fileQuerySize(const com::Utf8Str &aPath,
                          BOOL aFollowSymlinks,
                          LONG64 *aSize);
    HRESULT fsQueryFreeSpace(const com::Utf8Str &aPath, LONG64 *aFreeSpace);
    HRESULT fsQueryInfo(const com::Utf8Str &aPath, ComPtr<IGuestFsInfo> &aInfo);
    HRESULT fsObjExists(const com::Utf8Str &aPath,
                        BOOL aFollowSymlinks,
                        BOOL *pfExists);
    HRESULT fsObjQueryInfo(const com::Utf8Str &aPath,
                           BOOL aFollowSymlinks,
                           ComPtr<IGuestFsObjInfo> &aInfo);
    HRESULT fsObjRemove(const com::Utf8Str &aPath);
    HRESULT fsObjRemoveArray(const std::vector<com::Utf8Str> &aPaths,
                             ComPtr<IProgress> &aProgress);
    HRESULT fsObjRename(const com::Utf8Str &aOldPath,
                        const com::Utf8Str &aNewPath,
                        const std::vector<FsObjRenameFlag_T> &aFlags);
    HRESULT fsObjMove(const com::Utf8Str &aSource,
                      const com::Utf8Str &aDestination,
                      const std::vector<FsObjMoveFlag_T> &aFlags,
                      ComPtr<IProgress> &aProgress);
    HRESULT fsObjMoveArray(const std::vector<com::Utf8Str> &aSource,
                           const com::Utf8Str &aDestination,
                           const std::vector<FsObjMoveFlag_T> &aFlags,
                           ComPtr<IProgress> &aProgress);
    HRESULT fsObjCopyArray(const std::vector<com::Utf8Str> &aSource,
                           const com::Utf8Str &aDestination,
                           const std::vector<FileCopyFlag_T> &aFlags,
                           ComPtr<IProgress> &aProgress);
    HRESULT fsObjSetACL(const com::Utf8Str &aPath,
                        BOOL aFollowSymlinks,
                        const com::Utf8Str &aAcl,
                        ULONG aMode);
    HRESULT processCreate(const com::Utf8Str &aCommand,
                          const std::vector<com::Utf8Str> &aArguments,
                          const com::Utf8Str &aCwd,
                          const std::vector<com::Utf8Str> &aEnvironment,
                          const std::vector<ProcessCreateFlag_T> &aFlags,
                          ULONG aTimeoutMS,
                          ComPtr<IGuestProcess> &aGuestProcess);
    HRESULT processCreateEx(const com::Utf8Str &aCommand,
                            const std::vector<com::Utf8Str> &aArguments,
                            const com::Utf8Str &aCwd,
                            const std::vector<com::Utf8Str> &aEnvironment,
                            const std::vector<ProcessCreateFlag_T> &aFlags,
                            ULONG aTimeoutMS,
                            ProcessPriority_T aPriority,
                            const std::vector<LONG> &aAffinity,
                            ComPtr<IGuestProcess> &aGuestProcess);
    HRESULT processGet(ULONG aPid,
                       ComPtr<IGuestProcess> &aGuestProcess);
    HRESULT symlinkCreate(const com::Utf8Str &aSource,
                          const com::Utf8Str &aTarget,
                          SymlinkType_T aType);
    HRESULT symlinkExists(const com::Utf8Str &aSymlink,
                          BOOL *aExists);
    HRESULT symlinkRead(const com::Utf8Str &aSymlink,
                        const std::vector<SymlinkReadFlag_T> &aFlags,
                        com::Utf8Str &aTarget);
    HRESULT waitFor(ULONG aWaitFor,
                    ULONG aTimeoutMS,
                    GuestSessionWaitResult_T *aReason);
    HRESULT waitForArray(const std::vector<GuestSessionWaitForFlag_T> &aWaitFor,
                         ULONG aTimeoutMS,
                         GuestSessionWaitResult_T *aReason);
    /** @}  */

    /** Map of guest directories. The key specifies the internal directory ID. */
    typedef std::map <uint32_t, ComObjPtr<GuestDirectory> > SessionDirectories;
    /** Map of guest files. The key specifies the internal file ID. */
    typedef std::map <uint32_t, ComObjPtr<GuestFile> > SessionFiles;
    /** Map of guest processes. The key specifies the internal process number.
     *  To retrieve the process' guest PID use the Id() method of the IProcess interface. */
    typedef std::map <uint32_t, ComObjPtr<GuestProcess> > SessionProcesses;

    /** Guest session object type enumeration. */
    enum SESSIONOBJECTTYPE
    {
        /** Invalid session object type. */
        SESSIONOBJECTTYPE_INVALID    = 0,
        /** Session object. */
        SESSIONOBJECTTYPE_SESSION    = 1,
        /** Directory object. */
        SESSIONOBJECTTYPE_DIRECTORY  = 2,
        /** File object. */
        SESSIONOBJECTTYPE_FILE       = 3,
        /** Process object. */
        SESSIONOBJECTTYPE_PROCESS    = 4
    };

    struct SessionObject
    {
        /** Creation timestamp (in ms).
         * @note not used by anyone at the moment.  */
        uint64_t          msBirth;
        /** The object type. */
        SESSIONOBJECTTYPE enmType;
        /** Weak pointer to the object itself.
         * Is NULL for SESSIONOBJECTTYPE_SESSION because GuestSession doesn't
         * inherit from GuestObject. */
        GuestObject      *pObject;
    };

    /** Map containing all objects bound to a guest session.
     *  The key specifies the (global) context ID. */
    typedef std::map<uint32_t, SessionObject> SessionObjects;

public:
    /** @name Public internal methods.
     * @todo r=bird: Most of these are public for no real reason...
     * @{ */
    HRESULT                 i_copyFromGuest(const GuestSessionFsSourceSet &SourceSet, const com::Utf8Str &strDestination,
                                            ComPtr<IProgress> &pProgress);
    HRESULT                 i_copyToGuest(const GuestSessionFsSourceSet &SourceSet, const com::Utf8Str &strDestination,
                                          ComPtr<IProgress> &pProgress);
    int                     i_closeSession(uint32_t uFlags, uint32_t uTimeoutMS, int *pvrcGuest);
    HRESULT                 i_directoryCopyFlagFromStr(const com::Utf8Str &strFlags, bool fStrict, DirectoryCopyFlag_T *pfFlags);
    bool                    i_directoryExists(const Utf8Str &strPath);
    inline bool             i_directoryExists(uint32_t uDirID, ComObjPtr<GuestDirectory> *pDir);
    int                     i_directoryUnregister(GuestDirectory *pDirectory);
    int                     i_directoryRemove(const Utf8Str &strPath, uint32_t fFlags, int *pvrcGuest);
    int                     i_directoryCreate(const Utf8Str &strPath, uint32_t uMode, uint32_t uFlags, int *pvrcGuest);
    int                     i_directoryOpen(const GuestDirectoryOpenInfo &openInfo,
                                            ComObjPtr<GuestDirectory> &pDirectory, int *pvrcGuest);
    int                     i_directoryQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest);
    int                     i_dispatchToObject(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int                     i_dispatchToThis(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    HRESULT                 i_fileCopyFlagFromStr(const com::Utf8Str &strFlags, bool fStrict, FileCopyFlag_T *pfFlags);
    inline bool             i_fileExists(uint32_t uFileID, ComObjPtr<GuestFile> *pFile);
    int                     i_fileUnregister(GuestFile *pFile);
    int                     i_fileRemove(const Utf8Str &strPath, int *pvrcGuest);
    int                     i_fileOpenEx(const com::Utf8Str &aPath, FileAccessMode_T aAccessMode, FileOpenAction_T aOpenAction,
                                         FileSharingMode_T aSharingMode, ULONG aCreationMode,
                                         const std::vector<FileOpenExFlag_T> &aFlags,
                                         ComObjPtr<GuestFile> &pFile, int *pvrcGuest);
    int                     i_fileOpen(const GuestFileOpenInfo &openInfo, ComObjPtr<GuestFile> &pFile, int *pvrcGuest);
    int                     i_fileQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest);
    int                     i_fileQuerySize(const Utf8Str &strPath, bool fFollowSymlinks, int64_t *pllSize, int *pvrcGuest);
    int                     i_fsCreateTemp(const Utf8Str &strTemplate, const Utf8Str &strPath, bool fDirectory,
                                           Utf8Str &strName, uint32_t fMode, bool fSecure, int *pvrcGuest);
    int                     i_fsQueryInfo(const Utf8Str &strPath, PGSTCTLFSINFO pFsInfo, int *pvrcGuest);
    int                     i_fsObjQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest);
    const GuestCredentials &i_getCredentials(void);
    EventSource            *i_getEventSource(void) { return mEventSource; }
    Utf8Str                 i_getName(void);
    ULONG                   i_getId(void) { return mData.mSession.mID; }
    bool                    i_isStarted(void) const;
    HRESULT                 i_isStartedExternal(void);
    bool                    i_isReady(void);
    bool                    i_isTerminated(void) const;
    int                     i_onRemove(void);
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    int                     i_onFsNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
#endif
    int                     i_onSessionStatusChange(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    PathStyle_T             i_getGuestPathStyle(void);
    static PathStyle_T      i_getHostPathStyle(void);
    int                     i_startSession(int *pvrcGuest);
    int                     i_startSessionAsync(void);
    Guest                  *i_getParent(void) { return mParent; }
    uint32_t                i_getProtocolVersion(void) { return mData.mProtocolVersion; }
    int                     i_objectRegister(GuestObject *pObject, SESSIONOBJECTTYPE enmType, uint32_t *pidObject);
    int                     i_objectUnregister(uint32_t uObjectID);
    int                     i_objectsUnregister(void);
    int                     i_objectsNotifyAboutStatusChange(GuestSessionStatus_T enmSessionStatus);
    int                     i_pathRename(const Utf8Str &strSource, const Utf8Str &strDest, uint32_t uFlags, int *pvrcGuest);
    int                     i_pathUserDocuments(Utf8Str &strPath, int *pvrcGuest);
    int                     i_getMountPoints(std::vector<com::Utf8Str> &vecMountPoints, int *pvrcGuest);
    int                     i_pathUserHome(Utf8Str &strPath, int *pvrcGuest);
    int                     i_processUnregister(GuestProcess *pProcess);
    int                     i_processCreateEx(GuestProcessStartupInfo &procInfo, ComObjPtr<GuestProcess> &pProgress);
    inline bool             i_processExists(uint32_t uProcessID, ComObjPtr<GuestProcess> *pProcess);
    inline int              i_processGetByPID(ULONG uPID, ComObjPtr<GuestProcess> *pProcess);
    int                     i_sendMessage(uint32_t uFunction, uint32_t uParms, PVBOXHGCMSVCPARM paParms,
                                          uint64_t fDst = VBOX_GUESTCTRL_DST_SESSION);
    int                     i_setSessionStatus(GuestSessionStatus_T sessionStatus, int vrcSession);
    int                     i_signalWaiters(GuestSessionWaitResult_T enmWaitResult, int vrc /*= VINF_SUCCESS */);
    int                     i_shutdown(uint32_t fFlags, int *pvrcGuest);
    int                     i_determineProtocolVersion(void);
    int                     i_waitFor(uint32_t fWaitFlags, ULONG uTimeoutMS, GuestSessionWaitResult_T &waitResult, int *pvrcGuest);
    int                     i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t fWaitFlags, uint32_t uTimeoutMS,
                                                  GuestSessionStatus_T *pSessionStatus, int *pvrcGuest);
    /** @}  */

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
    /** @name Public internal methods for supporting older Guest Additions via
     *        VBoxService' built-in toolbox (< 7.1). Deprecated, do not use anymore.
     * @{  */
    int                     i_directoryCreateViaToolbox(const Utf8Str &strPath, uint32_t uMode, uint32_t uFlags, int *pvrcGuest);
    int                     i_fileRemoveViaToolbox(const Utf8Str &strPath, int *pvrcGuest);
    int                     i_fsCreateTempViaToolbox(const Utf8Str &strTemplate, const Utf8Str &strPath, bool fDirectory, Utf8Str &strName,
                                                     uint32_t fMode, bool fSecure, int *pvrcGuest);
    int                     i_fsObjQueryInfoViaToolbox(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest);
    /** @}  */
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

public:

    /** @name Static helper methods.
     * @{ */
    static Utf8Str          i_guestErrorToString(int guestRc);
    static bool             i_isTerminated(GuestSessionStatus_T enmStatus);
    static int              i_startSessionThreadTask(GuestSessionTaskInternalStart *pTask);
    /** @}  */

private:

    /* Console object. */
    ComObjPtr<Console>              mConsole;
    /* Guest object. */
    ComObjPtr<Guest>                mParent;
    /**
     * The session's event source. This source is used for
     * serving the internal listener as well as all other
     * external listeners that may register to it.
     *
     * Note: This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization.
     */
    const ComObjPtr<EventSource>    mEventSource;

    /** @todo r=bird: One of the core points of the DATA sub-structures in Main is
     * hinding implementation details and stuff that requires including iprt/asm.h.
     * The way it's used here totally defeats that purpose.  You need to make it
     * a pointer to a anynmous Data struct and define that structure in
     * GuestSessionImpl.cpp and allocate it in the Init() function.
     */
    struct Data
    {
        /** The session credentials. */
        GuestCredentials            mCredentials;
        /** The session's startup info. */
        GuestSessionStartupInfo     mSession;
        /** The session's object ID.
         *  Needed for registering wait events which are bound directly to this session. */
        uint32_t                    mObjectID;
        /** The session's current status. */
        GuestSessionStatus_T        mStatus;
        /** The set of environment changes for the session for use when
         *  creating new guest processes. */
        GuestEnvironmentChanges     mEnvironmentChanges;
        /** Pointer to the immutable base environment for the session.
         * @note This is not allocated until the guest reports it to the host. It is
         *       also shared with child processes.
         * @todo This is actually not yet implemented, see
         *       GuestSession::i_onSessionStatusChange. */
        GuestEnvironment const     *mpBaseEnvironment;
        /** Directory objects bound to this session. */
        SessionDirectories          mDirectories;
        /** File objects bound to this session. */
        SessionFiles                mFiles;
        /** Process objects bound to this session. */
        SessionProcesses            mProcesses;
        /** Map of registered session objects (files, directories, ...). */
        SessionObjects              mObjects;
        /** Guest control protocol version to be used.
         * Guest Additions < VBox 4.3 have version 1, any newer version will have version 2.
         * Set to 0 if a valid Guest Additions version was not found (yet). */
        uint32_t                    mProtocolVersion;
        /** Session timeout (in ms). */
        uint32_t                    mTimeout;
        /** The last returned session VBox status status returned from the guest side. */
        int                         mVrc;
        /** Object ID allocation bitmap; clear bits are free, set bits are busy. */
        uint64_t                    bmObjectIds[VBOX_GUESTCTRL_MAX_OBJECTS / sizeof(uint64_t) / 8];

        Data(void)
            : mpBaseEnvironment(NULL)
        {
            RT_ZERO(bmObjectIds);
            ASMBitSet(&bmObjectIds, VBOX_GUESTCTRL_MAX_OBJECTS - 1);    /* Reserved for the session itself? */
            ASMBitSet(&bmObjectIds, 0);                                 /* Let's reserve this too. */
        }
        Data(const Data &rThat)
            : mCredentials(rThat.mCredentials)
            , mSession(rThat.mSession)
            , mStatus(rThat.mStatus)
            , mEnvironmentChanges(rThat.mEnvironmentChanges)
            , mpBaseEnvironment(NULL)
            , mDirectories(rThat.mDirectories)
            , mFiles(rThat.mFiles)
            , mProcesses(rThat.mProcesses)
            , mObjects(rThat.mObjects)
            , mProtocolVersion(rThat.mProtocolVersion)
            , mTimeout(rThat.mTimeout)
            , mVrc(rThat.mVrc)
        {
            memcpy(&bmObjectIds, &rThat.bmObjectIds, sizeof(bmObjectIds));
        }
        ~Data(void)
        {
            if (mpBaseEnvironment)
            {
                mpBaseEnvironment->releaseConst();
                mpBaseEnvironment = NULL;
            }
        }
    } mData;
};

#endif /* !MAIN_INCLUDED_GuestSessionImpl_h */

