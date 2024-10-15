/* $Id: GuestSessionImplTasks.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest session tasks header.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestSessionImplTasks_h
#define MAIN_INCLUDED_GuestSessionImplTasks_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestSessionWrap.h"
#include "EventImpl.h"
#include "ProgressImpl.h"

#include "GuestCtrlImplPrivate.h"
#include "GuestSessionImpl.h"
#include "ThreadTask.h"

#include <iprt/vfs.h>

#include <vector>

class Guest;
class GuestSessionTask;
class GuestSessionTaskInternalStart;


/**
 * Structure for keeping a file system source specification,
 * along with options.
 */
struct GuestSessionFsSourceSpec
{
    GuestSessionFsSourceSpec()
        : enmType(FsObjType_Unknown)
        , enmPathStyle(PathStyle_Unknown)
        , fDryRun(false) { RT_ZERO(Type); }

    /** The (absolute) path to the source to use. */
    Utf8Str     strSource;
    /** Filter to use. Currently not implemented and thus ignored. */
    Utf8Str     strFilter;
    /** The root object type of this source (directory, file). */
    FsObjType_T enmType;
    /** The path style to use. */
    PathStyle_T enmPathStyle;
    /** Whether to do a dry run (e.g. not really touching anything) or not. */
    bool        fDryRun;
    /** Directory copy flags. */
    DirectoryCopyFlag_T fDirCopyFlags;
    /** File copy flags. */
    FileCopyFlag_T      fFileCopyFlags;
    /** Union to keep type-specific data. Must be a POD type (zero'ing). */
    union
    {
        /** File-specific data. */
        struct
        {
            /** Source file offset to start copying from. */
            size_t              offStart;
            /** Host file handle to use for reading from / writing to.
             *  Optional and can be NULL if not used. */
            PRTFILE             phFile;
            /** Source size (in bytes) to copy. */
            uint64_t            cbSize;
        } File;
    } Type;
};

/** A set of GuestSessionFsSourceSpec sources. */
typedef std::vector<GuestSessionFsSourceSpec> GuestSessionFsSourceSet;

/**
 * Structure for keeping a file system entry.
 */
struct FsEntry
{
    /** The entrie's file mode. */
    RTFMODE fMode;
    /** The entrie's path, relative to the list's root path. */
    Utf8Str strPath;
};

/** A vector of FsEntry entries. */
typedef std::vector<FsEntry *> FsEntries;

/**
 * Class for storing and handling file system entries, neeed for doing
 * internal file / directory operations to / from the guest.
 */
class FsList
{
public:

    FsList(const GuestSessionTask &Task);
    virtual ~FsList();

public:

    int Init(const Utf8Str &strSrcRootAbs, const Utf8Str &strDstRootAbs, const GuestSessionFsSourceSpec &SourceSpec);
    void Destroy(void);

#ifdef DEBUG
    void DumpToLog(void);
#endif

    int AddEntryFromGuest(const Utf8Str &strFile, const GuestFsObjData &fsObjData);
    int AddDirFromGuest(const Utf8Str &strPath, const Utf8Str &strSubDir = "");

    int AddEntryFromHost(const Utf8Str &strFile, PCRTFSOBJINFO pcObjInfo);
    int AddDirFromHost(const Utf8Str &strPath, const Utf8Str &strSubDir, char *pszPathReal, size_t cbPathReal, PRTDIRENTRYEX pDirEntry);

public:

    /** The guest session task object this list is working on. */
    const GuestSessionTask  &mTask;
    /** File system filter / options to use for this task. */
    GuestSessionFsSourceSpec mSourceSpec;
    /** The source' root path. Always in the source's path style!
     *
     *  For a single file list this is the full (absolute) path to a file,
     *  for a directory list this is the source root directory. */
    Utf8Str                 mSrcRootAbs;
    /** The destinations's root path. Always in the destination's path style!
     *
     *  For a single file list this is the full (absolute) path to a file,
     *  for a directory list this is the destination root directory. */
    Utf8Str                 mDstRootAbs;
    /** Total size (in bytes) of all list entries together. */
    uint64_t                mcbTotalSize;
    /** List of file system entries this list contains. */
    FsEntries               mVecEntries;
};

/** A set of FsList lists. */
typedef std::vector<FsList *> FsLists;

/**
 * Abstract base class for a lenghtly per-session operation which
 * runs in a Main worker thread.
 */
class GuestSessionTask
    : public ThreadTask
{
public:
    DECLARE_TRANSLATE_METHODS(GuestSessionTask)

    GuestSessionTask(GuestSession *pSession);

    virtual ~GuestSessionTask(void);

public:

    /**
     * Function which implements the actual task to perform.
     *
     * @returns VBox status code.
     */
    virtual int Run(void) = 0;

    void handler()
    {
        int vrc = Run();
        if (RT_FAILURE(vrc)) /* Could be VERR_INTERRUPTED if the user manually canceled the task. */
        {
            /* Make sure to let users know if there is a buggy task which failed but didn't set the progress object
             * to a failed state, and if not canceled manually by the user. */
            BOOL fCanceled;
            if (SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled))))
            {
                if (!fCanceled)
                {
                    BOOL fCompleted;
                    if (SUCCEEDED(mProgress->COMGETTER(Completed(&fCompleted))))
                    {
                        if (!fCompleted)
                            setProgressErrorMsg(E_UNEXPECTED,
                                                Utf8StrFmt(tr("Task '%s' failed with %Rrc, but progress is still pending. Please report this bug!\n"),
                                                           mDesc.c_str(), vrc));
                    }
                    else
                        AssertReleaseMsgFailed(("Guest Control: Unable to retrieve progress completion status for task '%s' (task result is %Rrc)\n",
                                                mDesc.c_str(), vrc));
                }
            }
            else
                AssertReleaseMsgFailed(("Guest Control: Unable to retrieve progress cancellation status for task '%s' (task result is %Rrc)\n",
                                        mDesc.c_str(), vrc));
        }
    }

    // unused: int RunAsync(const Utf8Str &strDesc, ComObjPtr<Progress> &pProgress);

    virtual HRESULT Init(const Utf8Str &strTaskDesc)
    {
        setTaskDesc(strTaskDesc);
        int vrc = createAndSetProgressObject(); /* Single operation by default. */
        if (RT_SUCCESS(vrc))
            return S_OK;
        return E_FAIL;
    }

    /** Returns the task's progress object. */
    const ComObjPtr<Progress>& GetProgressObject(void) const { return mProgress; }

    /** Returns the task's guest session object. */
    const ComObjPtr<GuestSession>& GetSession(void) const { return mSession; }

protected:

    /** @name Directory handling primitives.
     * @{ */
    int directoryCreateOnGuest(const com::Utf8Str &strPath,
                               uint32_t fMode, DirectoryCreateFlag_T enmDirectoryCreateFlags,
                               bool fFollowSymlinks, bool fCanExist);
    int directoryCreateOnHost(const com::Utf8Str &strPath, uint32_t fMode, uint32_t fCreate, bool fCanExist);
    /** @}  */

    /** @name File handling primitives.
     * @{ */
    int fileClose(const ComObjPtr<GuestFile> &file);
    int fileCopyFromGuestInner(const Utf8Str &strSrcFile, ComObjPtr<GuestFile> &srcFile,
                               const Utf8Str &strDstFile, PRTFILE phDstFile,
                               FileCopyFlag_T fFileCopyFlags, uint64_t offCopy, uint64_t cbSize);
    int fileCopyFromGuest(const Utf8Str &strSource, const Utf8Str &strDest, FileCopyFlag_T fFileCopyFlags);
    int fileCopyToGuestInner(const Utf8Str &strSrcFile, RTVFSFILE hSrcFile,
                             const Utf8Str &strDstFile, ComObjPtr<GuestFile> &dstFile,
                             FileCopyFlag_T fFileCopyFlags, uint64_t offCopy, uint64_t cbSize);

    int fileCopyToGuest(const Utf8Str &strSource, const Utf8Str &strDest, FileCopyFlag_T fFileCopyFlags);
    /** @}  */

    /** @name Guest property handling primitives.
     * @{ */
    int getGuestProperty(const ComObjPtr<Guest> &pGuest, const Utf8Str &strPath, Utf8Str &strValue);
    /** @}  */

    int setProgress(ULONG uPercent);
    int setProgressSuccess(void);
    HRESULT setProgressErrorMsg(HRESULT hrc, const Utf8Str &strMsg);
    HRESULT setProgressErrorMsg(HRESULT hrc, const Utf8Str &strMsg, const GuestErrorInfo &guestErrorInfo);

    inline void setTaskDesc(const Utf8Str &strTaskDesc) throw()
    {
        mDesc = strTaskDesc;
    }

    int createAndSetProgressObject(ULONG cOperations  = 1);

protected:

    Utf8Str                 mDesc;
    /** The guest session object this task is working on. */
    ComObjPtr<GuestSession> mSession;
    /** Progress object for getting updated when running
     *  asynchronously. Optional. */
    ComObjPtr<Progress>     mProgress;
    /** The guest's path style as char representation (depending on the guest OS type set). */
    Utf8Str                 mstrGuestPathStyle;
};

/**
 * Task for opening a guest session.
 */
class GuestSessionTaskOpen : public GuestSessionTask
{
public:

    GuestSessionTaskOpen(GuestSession *pSession,
                    uint32_t uFlags,
                    uint32_t uTimeoutMS);
    virtual ~GuestSessionTaskOpen(void);
    int Run(void);

protected:

    /** Session creation flags. */
    uint32_t mFlags;
    /** Session creation timeout (in ms). */
    uint32_t mTimeoutMS;
};

class GuestSessionCopyTask : public GuestSessionTask
{
public:
    DECLARE_TRANSLATE_METHODS(GuestSessionCopyTask)

    GuestSessionCopyTask(GuestSession *pSession);
    virtual ~GuestSessionCopyTask();

protected:

    /** Source set. */
    GuestSessionFsSourceSet mSources;
    /** Destination to copy to. */
    Utf8Str                 mDest;
    /** Vector of file system lists to handle.
     *  This either can be from the guest or the host side. */
    FsLists                 mVecLists;
};

/**
 * Guest session task for copying files / directories from guest to the host.
 */
class GuestSessionTaskCopyFrom : public GuestSessionCopyTask
{
public:
    DECLARE_TRANSLATE_METHODS(GuestSessionTaskCopyFrom)

    GuestSessionTaskCopyFrom(GuestSession *pSession, GuestSessionFsSourceSet const &vecSrc, const Utf8Str &strDest);
    virtual ~GuestSessionTaskCopyFrom(void);

    HRESULT Init(const Utf8Str &strTaskDesc);
    int Run(void);
};

/**
 * Task for copying directories from host to the guest.
 */
class GuestSessionTaskCopyTo : public GuestSessionCopyTask
{
public:
    DECLARE_TRANSLATE_METHODS(GuestSessionTaskCopyTo)

    GuestSessionTaskCopyTo(GuestSession *pSession, GuestSessionFsSourceSet const &vecSrc, const Utf8Str &strDest);
    virtual ~GuestSessionTaskCopyTo(void);

    HRESULT Init(const Utf8Str &strTaskDesc);
    int Run(void);
};

/**
 * Implementation for a Guest Additions update process for logging process output to the release log.
 */
class UpdateAdditionsProcess : public GuestProcessWrapper
{
public:

    UpdateAdditionsProcess(void) { }

    virtual ~UpdateAdditionsProcess();

    int onOutputCallback(uint32_t uHandle, const BYTE *pbData, size_t cbData);

protected:

    /** Current line of stdout. */
    Utf8Str mLineStdOut;
    /** Current line of stderr. */
    Utf8Str mLineStdErr;
};

/**
 * Tweaked startup info for a guest Guest Additions update process.
 */
class UpdateAdditionsStartupInfo : public GuestProcessStartupInfo
{
public:

    UpdateAdditionsStartupInfo(void)
    {
        /* We want to have stdout / stderr handled by default for update processes
         * (disabled as a workaround for #10776). */
        mFlags = ProcessCreateFlag_None;
    }
};

/**
 * Guest session task for automatically updating the Guest Additions on the guest.
 */
class GuestSessionTaskUpdateAdditions : public GuestSessionTask
{
public:
    DECLARE_TRANSLATE_METHODS(GuestSessionTaskUpdateAdditions)

    GuestSessionTaskUpdateAdditions(GuestSession *pSession, const Utf8Str &strSource,
                                    const ProcessArguments &aArguments, uint32_t fFlags);
    virtual ~GuestSessionTaskUpdateAdditions(void);
    int Run(void);

protected:

    /**
     * Suported OS types for automatic updating.
     */
    enum eOSType
    {
        eOSType_Unknown = 0,
        eOSType_Windows = 1,
        eOSType_Linux   = 2,
        eOSType_Solaris = 3
    };

    /**
     * Structure representing a file to
     * get off the .ISO, copied to the guest.
     */
    struct ISOFile
    {
        ISOFile(const Utf8Str &aSource,
                const Utf8Str &aDest,
                uint32_t       aFlags = 0)
            : strSource(aSource),
              strDest(aDest),
              fFlags(aFlags) { }

        ISOFile(const Utf8Str                 &aSource,
                const Utf8Str                 &aDest,
                uint32_t                       aFlags,
                const GuestProcessStartupInfo &aStartupInfo)
            : strSource(aSource),
              strDest(aDest),
              fFlags(aFlags),
              mProcInfo(aStartupInfo)
        {
            mProcInfo.mExecutable = strDest;
            if (mProcInfo.mName.isEmpty())
                mProcInfo.mName = strDest;
        }

        /** Source file on .ISO. */
        Utf8Str                 strSource;
        /** Destination file on the guest. */
        Utf8Str                 strDest;
        /** ISO file flags (see ISOFILE_FLAG_ defines). */
        uint32_t                fFlags;
        /** Optional arguments if this file needs to be
         *  executed. */
        GuestProcessStartupInfo mProcInfo;
    };

    int addProcessArguments(ProcessArguments &aArgumentsDest, const ProcessArguments &aArgumentsSource);
    int copyFileToGuest(GuestSession *pSession, RTVFS hVfsIso, Utf8Str const &strFileSource, const Utf8Str &strFileDest, bool fOptional);
    int runFileOnGuest(GuestSession *pSession, GuestProcessStartupInfo &procInfo, bool fSilent = false);
    HRESULT setUpdateErrorMsg(HRESULT hrc, const Utf8Str &strMsg);
    HRESULT setUpdateErrorMsg(HRESULT hrc, const Utf8Str &strMsg, const GuestErrorInfo &guestErrorInfo);

    int checkGuestAdditionsStatus(GuestSession *pSession, eOSType osType);
    int waitForGuestSession(ComObjPtr<Guest> pGuest, eOSType osType, ComObjPtr<GuestSession> &pNewSession);

    PlatformArchitecture_T getPlatformArch(void);

    /** Files to handle. */
    std::vector<ISOFile>        mFiles;
    /** The (optionally) specified Guest Additions .ISO on the host
     *  which will be used for the updating process. */
    Utf8Str                     mSource;
    /** (Optional) installer command line arguments. */
    ProcessArguments            mArguments;
    /** Update flags. */
    uint32_t                    mFlags;
};
#endif /* !MAIN_INCLUDED_GuestSessionImplTasks_h */
