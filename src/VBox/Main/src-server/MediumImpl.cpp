/* $Id: MediumImpl.cpp 110134 2025-07-07 13:52:08Z brent.paulson@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_MAIN_MEDIUM
#include "MediumImpl.h"
#include "MediumIOImpl.h"
#include "TokenImpl.h"
#include "ProgressImpl.h"
#include "SystemPropertiesImpl.h"
#include "VirtualBoxImpl.h"
#include "ExtPackManagerImpl.h"

#include "AutoCaller.h"
#include "Global.h"
#include "LoggingNew.h"
#include "ThreadTask.h"
#include "VBox/com/MultiResult.h"
#include "VBox/com/ErrorInfo.h"

#include <VBox/err.h>
#include <VBox/settings.h>

#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/file.h>
#include <iprt/cpp/utils.h>
#include <iprt/memsafer.h>
#include <iprt/base64.h>
#include <iprt/vfs.h>
#include <iprt/fsvfs.h>

#include <VBox/vd.h>

#include <algorithm>
#include <list>
#include <set>
#include <map>


typedef std::list<Guid> GuidList;


#ifdef VBOX_WITH_EXTPACK
static const char g_szVDPlugin[] = "VDPluginCrypt";
#endif


////////////////////////////////////////////////////////////////////////////////
//
// Medium data definition
//
////////////////////////////////////////////////////////////////////////////////
#if __cplusplus < 201700 && RT_GNUC_PREREQ(11,0) /* gcc/libstdc++ 12.1.1 errors out here because unary_function is deprecated */
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

struct SnapshotRef
{
    /** Equality predicate for stdc++. */
    struct EqualsTo
#if __cplusplus < 201700 /* deprecated in C++11, removed in C++17. */
        : public std::unary_function <SnapshotRef, bool>
#endif
    {
        explicit EqualsTo(const Guid &aSnapshotId) : snapshotId(aSnapshotId) {}

#if __cplusplus < 201700 /* deprecated in C++11, removed in C++17. */
        bool operator()(const argument_type &aThat) const
#else
        bool operator()(const SnapshotRef &aThat) const
#endif
        {
            return aThat.snapshotId == snapshotId;
        }

        const Guid snapshotId;
    };

    SnapshotRef(const Guid &aSnapshotId,
                const int &aRefCnt = 1)
        : snapshotId(aSnapshotId),
          iRefCnt(aRefCnt) {}

    Guid snapshotId;
    /*
     * The number of attachments of the medium in the same snapshot.
     * Used for MediumType_Readonly. It is always equal to 1 for other types.
     * Usual int is used because any changes in the BackRef are guarded by
     * AutoWriteLock.
     */
    int  iRefCnt;
};

/** Describes how a machine refers to this medium. */
struct BackRef
{
    /** Equality predicate for stdc++. */
    struct EqualsTo
#if __cplusplus < 201700 /* deprecated in C++11, removed in C++17. */
        : public std::unary_function <BackRef, bool>
#endif
    {
        explicit EqualsTo(const Guid &aMachineId) : machineId(aMachineId) {}

#if __cplusplus < 201700 /* deprecated in C++11, removed in C++17. */
        bool operator()(const argument_type &aThat) const
#else
        bool operator()(const BackRef &aThat) const
#endif
        {
            return aThat.machineId == machineId;
        }

        const Guid machineId;
    };

    BackRef(const Guid &aMachineId,
            const Guid &aSnapshotId = Guid::Empty)
        : machineId(aMachineId),
          iRefCnt(1),
          fInCurState(aSnapshotId.isZero())
    {
        if (aSnapshotId.isValid() && !aSnapshotId.isZero())
            llSnapshotIds.push_back(SnapshotRef(aSnapshotId));
    }

    Guid machineId;
    /*
     * The number of attachments of the medium in the same machine.
     * Used for MediumType_Readonly. It is always equal to 1 for other types.
     * Usual int is used because any changes in the BackRef are guarded by
     * AutoWriteLock.
     */
    int iRefCnt;
    bool fInCurState : 1;
    std::list<SnapshotRef> llSnapshotIds;
};

typedef std::list<BackRef> BackRefList;

#if __cplusplus < 201700 && RT_GNUC_PREREQ(11,0)
# pragma GCC diagnostic pop
#endif


struct Medium::Data
{
    Data()
        : pVirtualBox(NULL),
          state(MediumState_NotCreated),
          variant(MediumVariant_Standard),
          size(0),
          readers(0),
          preLockState(MediumState_NotCreated),
          queryInfoSem(LOCKCLASS_MEDIUMQUERY, "queryInfoSem"),
          queryInfoRunning(false),
          type(MediumType_Normal),
          devType(DeviceType_HardDisk),
          logicalSize(0),
          hddOpenMode(OpenReadWrite),
          autoReset(false),
          hostDrive(false),
          implicit(false),
          fClosing(false),
          uOpenFlagsDef(VD_OPEN_FLAGS_IGNORE_FLUSH),
          numCreateDiffTasks(0),
          hTcpNetInst(NULL),
          vdDiskIfaces(NULL),
          vdImageIfaces(NULL),
          fMoveThisMedium(false)
    { }

    /** weak VirtualBox parent */
    VirtualBox * const pVirtualBox;

    // pParent and llChildren are protected by VirtualBox::i_getMediaTreeLockHandle()
    ComObjPtr<Medium> pParent;
    MediaList llChildren;           // to add a child, just call push_back; to remove
                                    // a child, call child->deparent() which does a lookup

    GuidList llRegistryIDs;         // media registries in which this medium is listed

    const Guid id;
    Utf8Str strDescription;
    MediumState_T state;
    MediumVariant_T variant;
    Utf8Str strLocationFull;
    uint64_t size;
    Utf8Str strLastAccessError;

    BackRefList backRefs;

    size_t readers;
    MediumState_T preLockState;

    /** Special synchronization for operations which must wait for
     * Medium::i_queryInfo in another thread to complete. Using a SemRW is
     * not quite ideal, but at least it is subject to the lock validator,
     * unlike the SemEventMulti which we had here for many years. Catching
     * possible deadlocks is more important than a tiny bit of efficiency. */
    RWLockHandle queryInfoSem;
    bool queryInfoRunning : 1;

    const Utf8Str strFormat;
    ComObjPtr<MediumFormat> formatObj;

    MediumType_T type;
    DeviceType_T devType;
    uint64_t logicalSize;

    HDDOpenMode hddOpenMode;

    bool autoReset : 1;

    /** New UUID to be set on the next Medium::i_queryInfo call. */
    const Guid uuidImage;
    /** New parent UUID to be set on the next Medium::i_queryInfo call. */
    const Guid uuidParentImage;

/** @todo r=bird: the boolean bitfields are pointless if they're not grouped! */
    bool hostDrive : 1;

    settings::StringsMap mapProperties;

    bool implicit : 1;
    /** Flag whether the medium is in the process of being closed. */
    bool fClosing: 1;

    /** Default flags passed to VDOpen(). */
    unsigned uOpenFlagsDef;

    uint32_t numCreateDiffTasks;

    Utf8Str vdError;        /*< Error remembered by the VD error callback. */

    VDINTERFACEERROR vdIfError;

    VDINTERFACECONFIG vdIfConfig;

    /** The handle to the default VD TCP/IP interface. */
    VDIFINST          hTcpNetInst;

    PVDINTERFACE vdDiskIfaces;
    PVDINTERFACE vdImageIfaces;

    /** Flag if the medium is going to move to a new
     *  location. */
    bool fMoveThisMedium;
    /** new location path  */
    Utf8Str strNewLocationFull;
};

typedef struct VDSOCKETINT
{
    /** Socket handle. */
    RTSOCKET hSocket;
} VDSOCKETINT, *PVDSOCKETINT;

////////////////////////////////////////////////////////////////////////////////
//
// Globals
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Medium::Task class for asynchronous operations.
 *
 * @note Instances of this class must be created using new() because the
 *       task thread function will delete them when the task is complete.
 *
 * @note The constructor of this class adds a caller on the managed Medium
 *       object which is automatically released upon destruction.
 */
class Medium::Task : public ThreadTask
{
public:
    Task(Medium *aMedium, Progress *aProgress, bool fNotifyAboutChanges = true)
        : ThreadTask("Medium::Task"),
          mVDOperationIfaces(NULL),
          mMedium(aMedium),
          mMediumCaller(aMedium),
          mProgress(aProgress),
          mVirtualBoxCaller(NULL),
          mNotifyAboutChanges(fNotifyAboutChanges)
    {
        AssertReturnVoidStmt(aMedium, mHrc = E_FAIL);
        mHrc = mMediumCaller.hrc();
        if (FAILED(mHrc))
            return;

        /* Get strong VirtualBox reference, see below. */
        VirtualBox *pVirtualBox = aMedium->m->pVirtualBox;
        mVirtualBox = pVirtualBox;
        mVirtualBoxCaller.attach(pVirtualBox);
        mHrc = mVirtualBoxCaller.hrc();
        if (FAILED(mHrc))
            return;

        /* Set up a per-operation progress interface, can be used freely (for
         * binary operations you can use it either on the source or target). */
        if (mProgress)
        {
            mVDIfProgress.pfnProgress = aProgress->i_vdProgressCallback;
            int vrc = VDInterfaceAdd(&mVDIfProgress.Core,
                                     "Medium::Task::vdInterfaceProgress",
                                     VDINTERFACETYPE_PROGRESS,
                                     mProgress,
                                     sizeof(mVDIfProgress),
                                     &mVDOperationIfaces);
            AssertRC(vrc);
            if (RT_FAILURE(vrc))
                mHrc = E_FAIL;
        }
    }

    // Make all destructors virtual. Just in case.
    virtual ~Task()
    {
        /* send the notification of completion.*/
        if (   isAsync()
            && !mProgress.isNull())
            mProgress->i_notifyComplete(mHrc);
    }

    HRESULT hrc() const { return mHrc; }
    bool isOk() const { return SUCCEEDED(hrc()); }
    bool NotifyAboutChanges() const { return mNotifyAboutChanges; }

    const ComPtr<Progress>& GetProgressObject() const {return mProgress;}

    /**
     * Runs Medium::Task::executeTask() on the current thread
     * instead of creating a new one.
     */
    HRESULT runNow()
    {
        LogFlowFuncEnter();

        mHrc = executeTask();

        LogFlowFunc(("hrc=%Rhrc\n", mHrc));
        LogFlowFuncLeave();
        return mHrc;
    }

    /**
     * Implementation code for the "create base" task.
     * Used as function for execution from a standalone thread.
     */
    void handler()
    {
        LogFlowFuncEnter();
        try
        {
            mHrc = executeTask(); /* (destructor picks up mHrc, see above) */
            LogFlowFunc(("hrc=%Rhrc\n", mHrc));
        }
        catch (...)
        {
            LogRel(("Some exception in the function Medium::Task:handler()\n"));
        }

        LogFlowFuncLeave();
    }

    PVDINTERFACE mVDOperationIfaces;

    const ComObjPtr<Medium> mMedium;
    AutoCaller mMediumCaller;

protected:
    HRESULT mHrc;

private:
    virtual HRESULT executeTask() = 0;

    const ComObjPtr<Progress> mProgress;

    VDINTERFACEPROGRESS mVDIfProgress;

    /* Must have a strong VirtualBox reference during a task otherwise the
     * reference count might drop to 0 while a task is still running. This
     * would result in weird behavior, including deadlocks due to uninit and
     * locking order issues. The deadlock often is not detectable because the
     * uninit uses event semaphores which sabotages deadlock detection. */
    ComObjPtr<VirtualBox> mVirtualBox;
    AutoCaller mVirtualBoxCaller;
    bool mNotifyAboutChanges;
};

class Medium::CreateBaseTask : public Medium::Task
{
public:
    CreateBaseTask(Medium *aMedium,
                   Progress *aProgress,
                   uint64_t aSize,
                   MediumVariant_T aVariant,
                   bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mSize(aSize),
          mVariant(aVariant)
    {
        m_strTaskName = "createBase";
    }

    uint64_t mSize;
    MediumVariant_T mVariant;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskCreateBaseHandler(*this);
    }
};

class Medium::CreateDiffTask : public Medium::Task
{
public:
    CreateDiffTask(Medium *aMedium,
                   Progress *aProgress,
                   Medium *aTarget,
                   MediumVariant_T aVariant,
                   MediumLockList *aMediumLockList,
                   bool fKeepMediumLockList = false,
                   bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mpMediumLockList(aMediumLockList),
          mTarget(aTarget),
          mVariant(aVariant),
          mTargetCaller(aTarget),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        AssertReturnVoidStmt(aTarget != NULL, mHrc = E_FAIL);
        mHrc = mTargetCaller.hrc();
        if (FAILED(mHrc))
            return;
        m_strTaskName = "createDiff";
    }

    ~CreateDiffTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    MediumLockList *mpMediumLockList;

    const ComObjPtr<Medium> mTarget;
    MediumVariant_T mVariant;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskCreateDiffHandler(*this);
    }

    AutoCaller mTargetCaller;
    bool mfKeepMediumLockList;
};

class Medium::CloneTask : public Medium::Task
{
public:
    CloneTask(Medium *aMedium,
              Progress *aProgress,
              Medium *aTarget,
              MediumVariant_T aVariant,
              Medium *aParent,
              uint32_t idxSrcImageSame,
              uint32_t idxDstImageSame,
              MediumLockList *aSourceMediumLockList,
              MediumLockList *aTargetMediumLockList,
              bool fKeepSourceMediumLockList = false,
              bool fKeepTargetMediumLockList = false,
              bool fNotifyAboutChanges = true,
              uint64_t aTargetLogicalSize = 0)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mTarget(aTarget),
          mParent(aParent),
          mTargetLogicalSize(aTargetLogicalSize),
          mpSourceMediumLockList(aSourceMediumLockList),
          mpTargetMediumLockList(aTargetMediumLockList),
          mVariant(aVariant),
          midxSrcImageSame(idxSrcImageSame),
          midxDstImageSame(idxDstImageSame),
          mTargetCaller(aTarget),
          mParentCaller(aParent),
          mfKeepSourceMediumLockList(fKeepSourceMediumLockList),
          mfKeepTargetMediumLockList(fKeepTargetMediumLockList)
    {
        AssertReturnVoidStmt(aTarget != NULL, mHrc = E_FAIL);
        mHrc = mTargetCaller.hrc();
        if (FAILED(mHrc))
            return;
        /* aParent may be NULL */
        mHrc = mParentCaller.hrc();
        if (FAILED(mHrc))
            return;
        AssertReturnVoidStmt(aSourceMediumLockList != NULL, mHrc = E_FAIL);
        AssertReturnVoidStmt(aTargetMediumLockList != NULL, mHrc = E_FAIL);
        m_strTaskName = "createClone";
    }

    ~CloneTask()
    {
        if (!mfKeepSourceMediumLockList && mpSourceMediumLockList)
            delete mpSourceMediumLockList;
        if (!mfKeepTargetMediumLockList && mpTargetMediumLockList)
            delete mpTargetMediumLockList;
    }

    const ComObjPtr<Medium> mTarget;
    const ComObjPtr<Medium> mParent;
    uint64_t mTargetLogicalSize;
    MediumLockList *mpSourceMediumLockList;
    MediumLockList *mpTargetMediumLockList;
    MediumVariant_T mVariant;
    uint32_t midxSrcImageSame;
    uint32_t midxDstImageSame;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskCloneHandler(*this);
    }

    AutoCaller mTargetCaller;
    AutoCaller mParentCaller;
    bool mfKeepSourceMediumLockList;
    bool mfKeepTargetMediumLockList;
};

class Medium::MoveTask : public Medium::Task
{
public:
    MoveTask(Medium *aMedium,
              Progress *aProgress,
              MediumVariant_T aVariant,
              MediumLockList *aMediumLockList,
              bool fKeepMediumLockList = false,
              bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mpMediumLockList(aMediumLockList),
          mVariant(aVariant),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        AssertReturnVoidStmt(aMediumLockList != NULL, mHrc = E_FAIL);
        m_strTaskName = "createMove";
    }

    ~MoveTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    MediumLockList *mpMediumLockList;
    MediumVariant_T mVariant;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskMoveHandler(*this);
    }

    bool mfKeepMediumLockList;
};

class Medium::CompactTask : public Medium::Task
{
public:
    CompactTask(Medium *aMedium,
                Progress *aProgress,
                MediumLockList *aMediumLockList,
                bool fKeepMediumLockList = false,
                bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mpMediumLockList(aMediumLockList),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        AssertReturnVoidStmt(aMediumLockList != NULL, mHrc = E_FAIL);
        m_strTaskName = "createCompact";
    }

    ~CompactTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    MediumLockList *mpMediumLockList;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskCompactHandler(*this);
    }

    bool mfKeepMediumLockList;
};

class Medium::ResizeTask : public Medium::Task
{
public:
    ResizeTask(Medium *aMedium,
               uint64_t aSize,
               Progress *aProgress,
               MediumLockList *aMediumLockList,
               bool fKeepMediumLockList = false,
               bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mSize(aSize),
          mpMediumLockList(aMediumLockList),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        AssertReturnVoidStmt(aMediumLockList != NULL, mHrc = E_FAIL);
        m_strTaskName = "createResize";
    }

    ~ResizeTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    uint64_t        mSize;
    MediumLockList *mpMediumLockList;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskResizeHandler(*this);
    }

    bool mfKeepMediumLockList;
};

class Medium::ResetTask : public Medium::Task
{
public:
    ResetTask(Medium *aMedium,
              Progress *aProgress,
              MediumLockList *aMediumLockList,
              bool fKeepMediumLockList = false,
              bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mpMediumLockList(aMediumLockList),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        m_strTaskName = "createReset";
    }

    ~ResetTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    MediumLockList *mpMediumLockList;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskResetHandler(*this);
    }

    bool mfKeepMediumLockList;
};

class Medium::DeleteTask : public Medium::Task
{
public:
    DeleteTask(Medium *aMedium,
               Progress *aProgress,
               MediumLockList *aMediumLockList,
               bool fKeepMediumLockList = false,
               bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mpMediumLockList(aMediumLockList),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        m_strTaskName = "createDelete";
    }

    ~DeleteTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
    }

    MediumLockList *mpMediumLockList;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskDeleteHandler(*this);
    }

    bool mfKeepMediumLockList;
};

class Medium::MergeTask : public Medium::Task
{
public:
    MergeTask(Medium *aMedium,
              Medium *aTarget,
              bool fMergeForward,
              Medium *aParentForTarget,
              MediumLockList *aChildrenToReparent,
              Progress *aProgress,
              MediumLockList *aMediumLockList,
              bool fKeepMediumLockList = false,
              bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mTarget(aTarget),
          mfMergeForward(fMergeForward),
          mParentForTarget(aParentForTarget),
          mpChildrenToReparent(aChildrenToReparent),
          mpMediumLockList(aMediumLockList),
          mTargetCaller(aTarget),
          mParentForTargetCaller(aParentForTarget),
          mfKeepMediumLockList(fKeepMediumLockList)
    {
        AssertReturnVoidStmt(aMediumLockList != NULL, mHrc = E_FAIL);
        m_strTaskName = "createMerge";
    }

    ~MergeTask()
    {
        if (!mfKeepMediumLockList && mpMediumLockList)
            delete mpMediumLockList;
        if (mpChildrenToReparent)
            delete mpChildrenToReparent;
    }

    const ComObjPtr<Medium> mTarget;
    bool mfMergeForward;
    /* When mpChildrenToReparent is null then mParentForTarget is non-null and
     * vice versa. In other words: they are used in different cases. */
    const ComObjPtr<Medium> mParentForTarget;
    MediumLockList *mpChildrenToReparent;
    MediumLockList *mpMediumLockList;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskMergeHandler(*this);
    }

    AutoCaller mTargetCaller;
    AutoCaller mParentForTargetCaller;
    bool mfKeepMediumLockList;
};

class Medium::ImportTask : public Medium::Task
{
public:
    ImportTask(Medium *aMedium,
               Progress *aProgress,
               const char *aFilename,
               MediumFormat *aFormat,
               MediumVariant_T aVariant,
               RTVFSIOSTREAM aVfsIosSrc,
               Medium *aParent,
               MediumLockList *aTargetMediumLockList,
               bool fKeepTargetMediumLockList = false,
               bool fNotifyAboutChanges = true)
        : Medium::Task(aMedium, aProgress, fNotifyAboutChanges),
          mFilename(aFilename),
          mFormat(aFormat),
          mVariant(aVariant),
          mParent(aParent),
          mpTargetMediumLockList(aTargetMediumLockList),
          mpVfsIoIf(NULL),
          mParentCaller(aParent),
          mfKeepTargetMediumLockList(fKeepTargetMediumLockList)
    {
        AssertReturnVoidStmt(aTargetMediumLockList != NULL, mHrc = E_FAIL);
        /* aParent may be NULL */
        mHrc = mParentCaller.hrc();
        if (FAILED(mHrc))
            return;

        mVDImageIfaces = aMedium->m->vdImageIfaces;

        int vrc = VDIfCreateFromVfsStream(aVfsIosSrc, RTFILE_O_READ, &mpVfsIoIf);
        AssertRCReturnVoidStmt(vrc, mHrc = E_FAIL);

        vrc = VDInterfaceAdd(&mpVfsIoIf->Core, "Medium::ImportTaskVfsIos",
                             VDINTERFACETYPE_IO, mpVfsIoIf,
                             sizeof(VDINTERFACEIO), &mVDImageIfaces);
        AssertRCReturnVoidStmt(vrc, mHrc = E_FAIL);
        m_strTaskName = "createImport";
    }

    ~ImportTask()
    {
        if (!mfKeepTargetMediumLockList && mpTargetMediumLockList)
            delete mpTargetMediumLockList;
        if (mpVfsIoIf)
        {
            VDIfDestroyFromVfsStream(mpVfsIoIf);
            mpVfsIoIf = NULL;
        }
    }

    Utf8Str mFilename;
    ComObjPtr<MediumFormat> mFormat;
    MediumVariant_T mVariant;
    const ComObjPtr<Medium> mParent;
    MediumLockList *mpTargetMediumLockList;
    PVDINTERFACE mVDImageIfaces;
    PVDINTERFACEIO mpVfsIoIf; /**< Pointer to the VFS I/O stream to VD I/O interface wrapper. */

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskImportHandler(*this);
    }

    AutoCaller mParentCaller;
    bool mfKeepTargetMediumLockList;
};

class Medium::EncryptTask : public Medium::Task
{
public:
    EncryptTask(Medium *aMedium,
                const com::Utf8Str &strNewPassword,
                const com::Utf8Str &strCurrentPassword,
                const com::Utf8Str &strCipher,
                const com::Utf8Str &strNewPasswordId,
                Progress *aProgress,
                MediumLockList *aMediumLockList)
        : Medium::Task(aMedium, aProgress, false),
          mstrNewPassword(strNewPassword),
          mstrCurrentPassword(strCurrentPassword),
          mstrCipher(strCipher),
          mstrNewPasswordId(strNewPasswordId),
          mpMediumLockList(aMediumLockList)
    {
        AssertReturnVoidStmt(aMediumLockList != NULL, mHrc = E_FAIL);
        /* aParent may be NULL */
        mHrc = mParentCaller.hrc();
        if (FAILED(mHrc))
            return;

        mVDImageIfaces = aMedium->m->vdImageIfaces;
        m_strTaskName = "createEncrypt";
    }

    ~EncryptTask()
    {
        if (mstrNewPassword.length())
            RTMemWipeThoroughly(mstrNewPassword.mutableRaw(), mstrNewPassword.length(), 10 /* cPasses */);
        if (mstrCurrentPassword.length())
            RTMemWipeThoroughly(mstrCurrentPassword.mutableRaw(), mstrCurrentPassword.length(), 10 /* cPasses */);

        /* Keep any errors which might be set when deleting the lock list. */
        ErrorInfoKeeper eik;
        delete mpMediumLockList;
    }

    Utf8Str mstrNewPassword;
    Utf8Str mstrCurrentPassword;
    Utf8Str mstrCipher;
    Utf8Str mstrNewPasswordId;
    MediumLockList *mpMediumLockList;
    PVDINTERFACE    mVDImageIfaces;

private:
    HRESULT executeTask()
    {
        return mMedium->i_taskEncryptHandler(*this);
    }

    AutoCaller mParentCaller;
};



/**
 * Converts the Medium device type to the VD type.
 */
static const char *getVDTypeName(VDTYPE enmType)
{
    switch (enmType)
    {
        case VDTYPE_HDD:                return "HDD";
        case VDTYPE_OPTICAL_DISC:       return "DVD";
        case VDTYPE_FLOPPY:             return "floppy";
        case VDTYPE_INVALID:            return "invalid";
        default:
            AssertFailedReturn("unknown");
    }
}

/**
 * Converts the Medium device type to the VD type.
 */
static const char *getDeviceTypeName(DeviceType_T enmType)
{
    switch (enmType)
    {
        case DeviceType_HardDisk:       return "HDD";
        case DeviceType_DVD:            return "DVD";
        case DeviceType_Floppy:         return "floppy";
        case DeviceType_Null:           return "null";
        case DeviceType_Network:        return "network";
        case DeviceType_USB:            return "USB";
        case DeviceType_SharedFolder:   return "shared folder";
        case DeviceType_Graphics3D:     return "graphics 3d";
        default:
            AssertFailedReturn("unknown");
    }
}



////////////////////////////////////////////////////////////////////////////////
//
// Medium constructor / destructor
//
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(Medium)

HRESULT Medium::FinalConstruct()
{
    m = new Data;

    /* Initialize the callbacks of the VD error interface */
    m->vdIfError.pfnError = i_vdErrorCall;
    m->vdIfError.pfnMessage = NULL;

    /* Initialize the callbacks of the VD config interface */
    m->vdIfConfig.pfnAreKeysValid = i_vdConfigAreKeysValid;
    m->vdIfConfig.pfnQuerySize = i_vdConfigQuerySize;
    m->vdIfConfig.pfnQuery = i_vdConfigQuery;
    m->vdIfConfig.pfnUpdate = i_vdConfigUpdate;
    m->vdIfConfig.pfnQueryBytes = NULL;

    /* Initialize the per-disk interface chain (could be done more globally,
     * but it's not wasting much time or space so it's not worth it). */
    int vrc;
    vrc = VDInterfaceAdd(&m->vdIfError.Core,
                         "Medium::vdInterfaceError",
                         VDINTERFACETYPE_ERROR, this,
                         sizeof(VDINTERFACEERROR), &m->vdDiskIfaces);
    AssertRCReturn(vrc, E_FAIL);

    /* Initialize the per-image interface chain */
    vrc = VDInterfaceAdd(&m->vdIfConfig.Core,
                         "Medium::vdInterfaceConfig",
                         VDINTERFACETYPE_CONFIG, this,
                         sizeof(VDINTERFACECONFIG), &m->vdImageIfaces);
    AssertRCReturn(vrc, E_FAIL);

    /* Initialize the callbacks of the VD TCP interface (we always use the host
     * IP stack for now) */
    vrc = VDIfTcpNetInstDefaultCreate(&m->hTcpNetInst, &m->vdImageIfaces);
    AssertRCReturn(vrc, E_FAIL);

    return BaseFinalConstruct();
}

void Medium::FinalRelease()
{
    uninit();

    VDIfTcpNetInstDefaultDestroy(m->hTcpNetInst);
    delete m;

    BaseFinalRelease();
}

/**
 * Initializes an empty hard disk object without creating or opening an associated
 * storage unit.
 *
 * This gets called by VirtualBox::CreateMedium() in which case uuidMachineRegistry
 * is empty since starting with VirtualBox 4.0, we no longer add opened media to a
 * registry automatically (this is deferred until the medium is attached to a machine).
 *
 * This also gets called when VirtualBox creates diff images; in this case uuidMachineRegistry
 * is set to the registry of the parent image to make sure they all end up in the same
 * file.
 *
 * For hard disks that don't have the MediumFormatCapabilities_CreateFixed or
 * MediumFormatCapabilities_CreateDynamic capability (and therefore cannot be created or deleted
 * with the means of VirtualBox) the associated storage unit is assumed to be
 * ready for use so the state of the hard disk object will be set to Created.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aFormat
 * @param aLocation     Storage unit location.
 * @param uuidMachineRegistry The registry to which this medium should be added
 *                            (global registry UUID or machine UUID or empty if none).
 * @param aDeviceType   Device Type.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     const Utf8Str &aFormat,
                     const Utf8Str &aLocation,
                     const Guid &uuidMachineRegistry,
                     const DeviceType_T aDeviceType)
{
    AssertReturn(aVirtualBox != NULL, E_FAIL);
    AssertReturn(!aFormat.isEmpty(), E_FAIL);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = S_OK;

    unconst(m->pVirtualBox) = aVirtualBox;

    if (uuidMachineRegistry.isValid() && !uuidMachineRegistry.isZero())
        m->llRegistryIDs.push_back(uuidMachineRegistry);

    /* no storage yet */
    m->state = MediumState_NotCreated;

    /* cannot be a host drive */
    m->hostDrive = false;

    m->devType = aDeviceType;

    /* No storage unit is created yet, no need to call Medium::i_queryInfo */

    hrc = i_setFormat(aFormat);
    if (FAILED(hrc)) return hrc;

    hrc = i_setLocation(aLocation);
    if (FAILED(hrc)) return hrc;

    if (!(m->formatObj->i_getCapabilities() & (  MediumFormatCapabilities_CreateFixed
                                               | MediumFormatCapabilities_CreateDynamic
                                               | MediumFormatCapabilities_File))
       )
    {
        /* Storage for media of this format can neither be explicitly
         * created by VirtualBox nor deleted, so we place the medium to
         * Inaccessible state here and also add it to the registry. The
         * state means that one has to use RefreshState() to update the
         * medium format specific fields. */
        m->state = MediumState_Inaccessible;
        // create new UUID
        unconst(m->id).create();

        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
        ComObjPtr<Medium> pMedium;

        /*
         * Check whether the UUID is taken already and create a new one
         * if required.
         * Try this only a limited amount of times in case the PRNG is broken
         * in some way to prevent an endless loop.
         */
        for (unsigned i = 0; i < 5; i++)
        {
            bool fInUse;

            fInUse = m->pVirtualBox->i_isMediaUuidInUse(m->id, aDeviceType);
            if (fInUse)
            {
                // create new UUID
                unconst(m->id).create();
            }
            else
                break;
        }

        hrc = m->pVirtualBox->i_registerMedium(this, &pMedium, treeLock);
        Assert(this == pMedium || FAILED(hrc));
    }

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();

    return hrc;
}

/**
 * Initializes the medium object by opening the storage unit at the specified
 * location. The enOpenMode parameter defines whether the medium will be opened
 * read/write or read-only.
 *
 * This gets called by VirtualBox::OpenMedium() and also by
 * Machine::AttachDevice() and createImplicitDiffs() when new diff
 * images are created.
 *
 * There is no registry for this case since starting with VirtualBox 4.0, we
 * no longer add opened media to a registry automatically (this is deferred
 * until the medium is attached to a machine).
 *
 * For hard disks, the UUID, format and the parent of this medium will be
 * determined when reading the medium storage unit. For DVD and floppy images,
 * which have no UUIDs in their storage units, new UUIDs are created.
 * If the detected or set parent is not known to VirtualBox, then this method
 * will fail.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aLocation     Storage unit location.
 * @param enOpenMode    Whether to open the medium read/write or read-only.
 * @param fForceNewUuid Whether a new UUID should be set to avoid duplicates.
 * @param aDeviceType   Device type of medium.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     const Utf8Str &aLocation,
                     HDDOpenMode enOpenMode,
                     bool fForceNewUuid,
                     DeviceType_T aDeviceType)
{
    AssertReturn(aVirtualBox, E_INVALIDARG);
    AssertReturn(!aLocation.isEmpty(), E_INVALIDARG);

    HRESULT hrc = S_OK;

    {
        /* Enclose the state transition NotReady->InInit->Ready */
        AutoInitSpan autoInitSpan(this);
        AssertReturn(autoInitSpan.isOk(), E_FAIL);

        unconst(m->pVirtualBox) = aVirtualBox;

        /* there must be a storage unit */
        m->state = MediumState_Created;

        /* remember device type for correct unregistering later */
        m->devType = aDeviceType;

        /* cannot be a host drive */
        m->hostDrive = false;

        /* remember the open mode (defaults to ReadWrite) */
        m->hddOpenMode = enOpenMode;

        if (aDeviceType == DeviceType_DVD)
            m->type = MediumType_Readonly;
        else if (aDeviceType == DeviceType_Floppy)
            m->type = MediumType_Writethrough;

        hrc = i_setLocation(aLocation);
        if (FAILED(hrc)) return hrc;

        /* get all the information about the medium from the storage unit */
        if (fForceNewUuid)
            unconst(m->uuidImage).create();

        m->state = MediumState_Inaccessible;
        m->strLastAccessError = tr("Accessibility check was not yet performed");

        /* Confirm a successful initialization before the call to i_queryInfo.
         * Otherwise we can end up with a AutoCaller deadlock because the
         * medium becomes visible but is not marked as initialized. Causes
         * locking trouble (e.g. trying to save media registries) which is
         * hard to solve. */
        autoInitSpan.setSucceeded();
    }

    /* we're normal code from now on, no longer init */
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    /* need to call i_queryInfo immediately to correctly place the medium in
     * the respective media tree and update other information such as uuid */
    hrc = i_queryInfo(fForceNewUuid /* fSetImageId */, false /* fSetParentId */, autoCaller);
    if (SUCCEEDED(hrc))
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* if the storage unit is not accessible, it's not acceptable for the
         * newly opened media so convert this into an error */
        if (m->state == MediumState_Inaccessible)
        {
            Assert(!m->strLastAccessError.isEmpty());
            hrc = setError(E_FAIL, "%s", m->strLastAccessError.c_str());
            alock.release();
            autoCaller.release();
            uninit();
        }
        else
        {
            AssertStmt(!m->id.isZero(),
                       alock.release(); autoCaller.release(); uninit(); return E_FAIL);

            /* storage format must be detected by Medium::i_queryInfo if the
             * medium is accessible */
            AssertStmt(!m->strFormat.isEmpty(),
                       alock.release(); autoCaller.release(); uninit(); return E_FAIL);
        }
    }
    else
    {
        /* opening this image failed, mark the object as dead */
        autoCaller.release();
        uninit();
    }

    return hrc;
}

/**
 * Initializes the medium object by loading its data from the given settings
 * node. The medium will always be opened read/write.
 *
 * In this case, since we're loading from a registry, uuidMachineRegistry is
 * always set: it's either the global registry UUID or a machine UUID when
 * loading from a per-machine registry.
 *
 * @param aParent       Parent medium disk or NULL for a root (base) medium.
 * @param aDeviceType   Device type of the medium.
 * @param uuidMachineRegistry The registry to which this medium should be
 *                            added (global registry UUID or machine UUID).
 * @param data          Configuration settings.
 * @param strMachineFolder The machine folder with which to resolve relative paths;
 *                         if empty, then we use the VirtualBox home directory
 *
 * @note Locks the medium tree for writing.
 */
HRESULT Medium::initOne(Medium *aParent,
                        DeviceType_T aDeviceType,
                        const Guid &uuidMachineRegistry,
                        const Utf8Str &strMachineFolder,
                        const settings::Medium &data)
{
    HRESULT hrc;

    if (uuidMachineRegistry.isValid() && !uuidMachineRegistry.isZero())
        m->llRegistryIDs.push_back(uuidMachineRegistry);

    /* register with VirtualBox/parent early, since uninit() will
     * unconditionally unregister on failure */
    if (aParent)
    {
        // differencing medium: add to parent
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
        // no need to check maximum depth as settings reading did it
        i_setParent(aParent);
    }

    /* see below why we don't call Medium::i_queryInfo (and therefore treat
     * the medium as inaccessible for now */
    m->state = MediumState_Inaccessible;
    m->strLastAccessError = tr("Accessibility check was not yet performed");

    /* required */
    unconst(m->id) = data.uuid;

    /* assume not a host drive */
    m->hostDrive = false;

    /* optional */
    m->strDescription = data.strDescription;

    /* required */
    if (aDeviceType == DeviceType_HardDisk)
    {
        AssertReturn(!data.strFormat.isEmpty(), E_FAIL);
        hrc = i_setFormat(data.strFormat);
        if (FAILED(hrc)) return hrc;
    }
    else
    {
        /// @todo handle host drive settings here as well?
        if (!data.strFormat.isEmpty())
            hrc = i_setFormat(data.strFormat);
        else
            hrc = i_setFormat("RAW");
        if (FAILED(hrc)) return hrc;
    }

    /* optional, only for diffs, default is false; we can only auto-reset
     * diff media so they must have a parent */
    if (aParent != NULL)
        m->autoReset = data.fAutoReset;
    else
        m->autoReset = false;

    /* properties (after setting the format as it populates the map). Note that
     * if some properties are not supported but present in the settings file,
     * they will still be read and accessible (for possible backward
     * compatibility; we can also clean them up from the XML upon next
     * XML format version change if we wish) */
    for (settings::StringsMap::const_iterator it = data.properties.begin();
         it != data.properties.end();
         ++it)
    {
        const Utf8Str &name = it->first;
        const Utf8Str &value = it->second;
        m->mapProperties[name] = value;
    }

    /* try to decrypt an optional iSCSI initiator secret */
    settings::StringsMap::const_iterator itCph = data.properties.find("InitiatorSecretEncrypted");
    if (   itCph != data.properties.end()
        && !itCph->second.isEmpty())
    {
        Utf8Str strPlaintext;
        int vrc = m->pVirtualBox->i_decryptSetting(&strPlaintext, itCph->second);
        if (RT_SUCCESS(vrc))
            m->mapProperties["InitiatorSecret"] = strPlaintext;
    }

    Utf8Str strFull;
    if (m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File)
    {
        // compose full path of the medium, if it's not fully qualified...
        // slightly convoluted logic here. If the caller has given us a
        // machine folder, then a relative path will be relative to that:
        if (    !strMachineFolder.isEmpty()
             && !RTPathStartsWithRoot(data.strLocation.c_str())
           )
        {
            strFull = strMachineFolder;
            strFull += RTPATH_SLASH;
            strFull += data.strLocation;
        }
        else
        {
            // Otherwise use the old VirtualBox "make absolute path" logic:
            int vrc = m->pVirtualBox->i_calculateFullPath(data.strLocation, strFull);
            if (RT_FAILURE(vrc))
                return Global::vboxStatusCodeToCOM(vrc);
        }
    }
    else
        strFull = data.strLocation;

    hrc = i_setLocation(strFull);
    if (FAILED(hrc)) return hrc;

    if (aDeviceType == DeviceType_HardDisk)
    {
        /* type is only for base hard disks */
        if (m->pParent.isNull())
            m->type = data.hdType;
    }
    else if (aDeviceType == DeviceType_DVD)
        m->type = MediumType_Readonly;
    else
        m->type = MediumType_Writethrough;

    /* remember device type for correct unregistering later */
    m->devType = aDeviceType;

    LogFlowThisFunc(("m->strLocationFull='%s', m->strFormat=%s, m->id={%RTuuid}\n",
                     m->strLocationFull.c_str(), m->strFormat.c_str(), m->id.raw()));

    return S_OK;
}

/**
 * Initializes and registers the medium object and its children by loading its
 * data from the given settings node. The medium will always be opened
 * read/write.
 *
 * @todo r=bird: What's that stuff about 'always be opened read/write'?
 *
 * In this case, since we're loading from a registry, uuidMachineRegistry is
 * always set: it's either the global registry UUID or a machine UUID when
 * loading from a per-machine registry.
 *
 * The only caller is currently VirtualBox::initMedia().
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aDeviceType   Device type of the medium.
 * @param uuidMachineRegistry The registry to which this medium should be added
 *                      (global registry UUID or machine UUID).
 * @param strMachineFolder The machine folder with which to resolve relative
 *                      paths; if empty, then we use the VirtualBox home directory
 * @param data          Configuration settings.
 * @param mediaTreeLock Autolock.
 * @param uIdsForNotify List to be updated with newly registered media.
 *
 * @note Assumes that the medium tree lock is held for writing. May release
 * and lock it again. At the end it is always held.
 */
/* static */
HRESULT Medium::initFromSettings(VirtualBox *aVirtualBox,
                                 DeviceType_T aDeviceType,
                                 const Guid &uuidMachineRegistry,
                                 const Utf8Str &strMachineFolder,
                                 const settings::Medium &data,
                                 AutoWriteLock &mediaTreeLock,
                                 std::list<std::pair<Guid, DeviceType_T> > &uIdsForNotify)
{
    Assert(aVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    AssertReturn(aVirtualBox, E_INVALIDARG);

    HRESULT hrc = S_OK;

    MediaList llMediaTocleanup;

    std::list<const settings::Medium *> llSettingsTodo;
    llSettingsTodo.push_back(&data);
    MediaList llParentsTodo;
    llParentsTodo.push_back(NULL);

    while (!llSettingsTodo.empty())
    {
        const settings::Medium *current = llSettingsTodo.front();
        llSettingsTodo.pop_front();
        ComObjPtr<Medium> pParent = llParentsTodo.front();
        llParentsTodo.pop_front();

        bool fReleasedMediaTreeLock = false;
        ComObjPtr<Medium> pMedium;
        hrc = pMedium.createObject();
        if (FAILED(hrc))
            break;
        ComObjPtr<Medium> pActualMedium(pMedium);

        {
            AutoInitSpan autoInitSpan(pMedium);
            AssertBreakStmt(autoInitSpan.isOk(), hrc = E_FAIL);

            unconst(pMedium->m->pVirtualBox) = aVirtualBox;
            hrc = pMedium->initOne(pParent, aDeviceType, uuidMachineRegistry, strMachineFolder, *current);
            if (FAILED(hrc))
                break;
            hrc = aVirtualBox->i_registerMedium(pActualMedium, &pActualMedium, mediaTreeLock, true /*fCalledFromMediumInit*/);
            if (SUCCEEDED(hrc) && pActualMedium == pMedium)
            {
                /* It is a truly new medium, remember details for cleanup. */
                autoInitSpan.setSucceeded();
                llMediaTocleanup.push_front(pMedium);
            }
            else
            {
                /* Since the newly created medium was replaced by an already
                 * known one when merging medium trees, we can immediately mark
                 * it as failed. */
                autoInitSpan.setFailed();
                mediaTreeLock.release();
                fReleasedMediaTreeLock = true;
            }
        }
        if (fReleasedMediaTreeLock)
        {
            /* With the InitSpan out of the way it's safe to let the refcount
             * drop to 0 without causing uninit trouble. */
            pMedium.setNull();
            mediaTreeLock.acquire();

            if (FAILED(hrc))
                break;
        }

        /* create all children */
        std::list<settings::Medium>::const_iterator itBegin = current->llChildren.begin();
        std::list<settings::Medium>::const_iterator itEnd = current->llChildren.end();
        for (std::list<settings::Medium>::const_iterator it = itBegin; it != itEnd; ++it)
        {
            llSettingsTodo.push_back(&*it);
            llParentsTodo.push_back(pActualMedium);
        }
    }

    if (SUCCEEDED(hrc))
    {
        /* Check for consistency. */
        Assert(llSettingsTodo.size() == 0);
        Assert(llParentsTodo.size() == 0);
        /* Create the list of notifications, parent first. */
        for (MediaList::const_reverse_iterator it = llMediaTocleanup.rbegin(); it != llMediaTocleanup.rend(); ++it)
        {
            ComObjPtr<Medium> pMedium = *it;
            AutoCaller mediumCaller(pMedium);
            if (mediumCaller.isOk())
            {
                const Guid &id = pMedium->i_getId();
                uIdsForNotify.push_back(std::pair<Guid, DeviceType_T>(id, aDeviceType));
            }
        }
    }
    else
    {
        /* Forget state of the settings processing. */
        llSettingsTodo.clear();
        llParentsTodo.clear();
        /* Unregister all accumulated medium objects in the right order (last
         * created to first created, avoiding config leftovers). */
        MediaList::const_iterator itBegin = llMediaTocleanup.begin();
        MediaList::const_iterator itEnd = llMediaTocleanup.end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            ComObjPtr<Medium> pMedium = *it;
            pMedium->i_unregisterWithVirtualBox();
        }
        /* Forget the only references to all newly created medium objects,
         * triggering freeing (uninit happened in unregistering above). */
        mediaTreeLock.release();
        llMediaTocleanup.clear();
        mediaTreeLock.acquire();
    }

    return hrc;
}

/**
 * Initializes the medium object by providing the host drive information.
 * Not used for anything but the host floppy/host DVD case.
 *
 * There is no registry for this case.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aDeviceType   Device type of the medium.
 * @param aLocation     Location of the host drive.
 * @param aDescription  Comment for this host drive.
 *
 * @note Locks VirtualBox lock for writing.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     DeviceType_T aDeviceType,
                     const Utf8Str &aLocation,
                     const Utf8Str &aDescription /* = Utf8Str::Empty */)
{
    ComAssertRet(aDeviceType == DeviceType_DVD || aDeviceType == DeviceType_Floppy, E_INVALIDARG);
    ComAssertRet(!aLocation.isEmpty(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(m->pVirtualBox) = aVirtualBox;

    // We do not store host drives in VirtualBox.xml or anywhere else, so if we want
    // host drives to be identifiable by UUID and not give the drive a different UUID
    // every time VirtualBox starts, we need to fake a reproducible UUID here:
    RTUUID uuid;
    RTUuidClear(&uuid);
    if (aDeviceType == DeviceType_DVD)
        memcpy(&uuid.au8[0], "DVD", 3);
    else
        memcpy(&uuid.au8[0], "FD", 2);
    /* use device name, adjusted to the end of uuid, shortened if necessary */
    size_t lenLocation = aLocation.length();
    if (lenLocation > 12)
        memcpy(&uuid.au8[4], aLocation.c_str() + (lenLocation - 12), 12);
    else
        memcpy(&uuid.au8[4 + 12 - lenLocation], aLocation.c_str(), lenLocation);
    unconst(m->id) = uuid;

    if (aDeviceType == DeviceType_DVD)
        m->type = MediumType_Readonly;
    else
        m->type = MediumType_Writethrough;
    m->devType = aDeviceType;
    m->state = MediumState_Created;
    m->hostDrive = true;
    HRESULT hrc = i_setFormat("RAW");
    if (FAILED(hrc)) return hrc;
    hrc = i_setLocation(aLocation);
    if (FAILED(hrc)) return hrc;
    m->strDescription = aDescription;

    autoInitSpan.setSucceeded();
    return S_OK;
}

/**
 * Uninitializes the instance.
 *
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 *
 * @note All children of this medium get uninitialized, too, in a stack
 *       friendly manner.
 */
void Medium::uninit()
{
    /* It is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, and in this case we don't need to continue.
     * Normally this would be handled through the AutoUninitSpan magic, however
     * this cannot be done at this point as the media tree must be locked
     * before reaching the AutoUninitSpan, otherwise deadlocks can happen.
     *
     * NOTE: The tree lock is higher priority than the medium caller and medium
     * object locks, i.e. the medium caller may have to be released and be
     * re-acquired in the right place later. See Medium::getParent() for sample
     * code how to do this safely. */
    VirtualBox *pVirtualBox = m->pVirtualBox;
    if (!pVirtualBox)
        return;

    /* Caller must not hold the object (checked below) or media tree lock. */
    Assert(!pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    AutoWriteLock treeLock(pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    /* Must use a list without refcounting help since "this" might already have
     * reached 0, and then the refcount must not be increased again since it
     * would otherwise trigger a double free. For all other list entries this
     * needs manual refcount updating, to make sure the refcount for children
     * does not drop to 0 too early. */
    std::list<Medium *> llMediaTodo;
    llMediaTodo.push_back(this);

    while (!llMediaTodo.empty())
    {
        Medium *pMedium = llMediaTodo.front();
        llMediaTodo.pop_front();

        /* Enclose the state transition Ready->InUninit->NotReady */
        AutoUninitSpan autoUninitSpan(pMedium);
        if (autoUninitSpan.uninitDone())
        {
            if (pMedium != this)
                pMedium->Release();
            continue;
        }

        Assert(!pMedium->isWriteLockOnCurrentThread());
#ifdef DEBUG
        if (!pMedium->m->backRefs.empty())
            pMedium->i_dumpBackRefs();
#endif
        Assert(pMedium->m->backRefs.empty());

        pMedium->m->formatObj.setNull();

        if (pMedium->m->state == MediumState_Deleting)
        {
            /* This medium has been already deleted (directly or as part of a
             * merge).  Reparenting has already been done. */
            Assert(pMedium->m->pParent.isNull());
            Assert(pMedium->m->llChildren.empty());
            if (pMedium != this)
                pMedium->Release();
            continue;
        }

        //Assert(!pMedium->m->pParent);
        /** @todo r=klaus Should not be necessary, since the caller should be
         * doing the deparenting. No time right now to test everything. */
        if (pMedium == this && pMedium->m->pParent)
            pMedium->i_deparent();

        /* Process all children */
        MediaList::const_iterator itBegin = pMedium->m->llChildren.begin();
        MediaList::const_iterator itEnd = pMedium->m->llChildren.end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            Medium *pChild = *it;
            pChild->m->pParent.setNull();
            pChild->AddRef();
            llMediaTodo.push_back(pChild);
        }

        /* Children information obsolete, will be processed anyway. */
        pMedium->m->llChildren.clear();

        unconst(pMedium->m->pVirtualBox) = NULL;

        if (pMedium != this)
            pMedium->Release();

        /*
         * Special case. Invalidate the tracked object.
         * Works ONLY in conjunction with setTracked() in init() or FinalConstruct()!
         * The Medium object may stay in memory after the call uninit(). See i_close(), in instance.
         * The object state is "Not ready" in this case. From user perspective the object doesn't exist anymore.
         * In this case we still track the object but mark the object as "invalid".
         */
        invalidateTracked();

        autoUninitSpan.setSucceeded();
    }
}

/**
 * Internal helper that removes "this" from the list of children of its
 * parent. Used in uninit() and other places when reparenting is necessary.
 *
 * The caller must hold the medium tree lock!
 */
void Medium::i_deparent()
{
    MediaList &llParent = m->pParent->m->llChildren;
    for (MediaList::iterator it = llParent.begin();
         it != llParent.end();
         ++it)
    {
        Medium *pParentsChild = *it;
        if (this == pParentsChild)
        {
            llParent.erase(it);
            break;
        }
    }
    m->pParent.setNull();
}

/**
 * Internal helper that removes "this" from the list of children of its
 * parent. Used in uninit() and other places when reparenting is necessary.
 *
 * The caller must hold the medium tree lock!
 */
void Medium::i_setParent(const ComObjPtr<Medium> &pParent)
{
    m->pParent = pParent;
    if (pParent)
        pParent->m->llChildren.push_back(this);
}


////////////////////////////////////////////////////////////////////////////////
//
// IMedium public methods
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Medium::getId(com::Guid &aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aId = m->id;

    return S_OK;
}

HRESULT Medium::getDescription(AutoCaller &autoCaller, com::Utf8Str &aDescription)
{
    NOREF(autoCaller);
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDescription = m->strDescription;

    return S_OK;
}

HRESULT Medium::setDescription(AutoCaller &autoCaller, const com::Utf8Str &aDescription)
{
    /// @todo update m->strDescription and save the global registry (and local
    /// registries of portable VMs referring to this medium), this will also
    /// require to add the mRegistered flag to data

    HRESULT hrc = S_OK;

    MediumLockList *pMediumLockList(new MediumLockList());

    try
    {
        autoCaller.release();

        // to avoid redundant locking, which just takes a time, just call required functions.
        // the error will be just stored and will be reported after locks will be acquired again

        const char *pszError = NULL;


        /* Build the lock list. */
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                     this /* pToLockWrite */,
                                     true /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        if (FAILED(hrc))
            pszError = tr("Failed to create medium lock list for '%s'");
        else
        {
            hrc = pMediumLockList->Lock();
            if (FAILED(hrc))
                pszError = tr("Failed to lock media '%s'");
        }

        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (FAILED(hrc))
            throw setError(hrc, pszError, i_getLocationFull().c_str());

        /* Set a new description */
        m->strDescription = aDescription;

        // save the settings
        alock.release();
        autoCaller.release();
        treeLock.release();
        i_markRegistriesModified();
        m->pVirtualBox->i_saveModifiedRegistries();
        m->pVirtualBox->i_onMediumConfigChanged(this);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    delete pMediumLockList;

    return hrc;
}

HRESULT Medium::getState(MediumState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aState = m->state;

    return S_OK;
}

HRESULT Medium::getVariant(std::vector<MediumVariant_T> &aVariant)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    const size_t cBits = sizeof(MediumVariant_T) * 8;
    aVariant.resize(cBits);
    for (size_t i = 0; i < cBits; ++i)
        aVariant[i] = (MediumVariant_T)(m->variant & RT_BIT(i));

    return S_OK;
}

HRESULT Medium::getLocation(com::Utf8Str &aLocation)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aLocation = m->strLocationFull;

    return S_OK;
}

HRESULT Medium::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = i_getName();

    return S_OK;
}

HRESULT Medium::getDeviceType(DeviceType_T *aDeviceType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDeviceType = m->devType;

    return S_OK;
}

HRESULT Medium::getHostDrive(BOOL *aHostDrive)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aHostDrive = m->hostDrive;

    return S_OK;
}

HRESULT Medium::getSize(LONG64 *aSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aSize = (LONG64)m->size;

    return S_OK;
}

HRESULT Medium::getFormat(com::Utf8Str &aFormat)
{
    /* no need to lock, m->strFormat is const */

    aFormat = m->strFormat;
    return S_OK;
}

HRESULT Medium::getMediumFormat(ComPtr<IMediumFormat> &aMediumFormat)
{
    /* no need to lock, m->formatObj is const */
    m->formatObj.queryInterfaceTo(aMediumFormat.asOutParam());

    return S_OK;
}

HRESULT Medium::getType(AutoCaller &autoCaller, MediumType_T *aType)
{
    NOREF(autoCaller);
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aType = m->type;

    return S_OK;
}

HRESULT Medium::setType(AutoCaller &autoCaller, MediumType_T aType)
{
    autoCaller.release();

    /* It is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, see #uninit(). */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

    // we access m->pParent
    AutoReadLock treeLock(!pVirtualBox.isNull() ? &pVirtualBox->i_getMediaTreeLockHandle() : NULL COMMA_LOCKVAL_SRC_POS);

    autoCaller.add();
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock mlock(this COMMA_LOCKVAL_SRC_POS);

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    while (m->queryInfoRunning)
    {
        mlock.release();
        autoCaller.release();
        treeLock.release();
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs
         * this lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        /* must not hold the object lock now */
        Assert(!isWriteLockOnCurrentThread());
        {
            AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
        }
        treeLock.acquire();
        autoCaller.add();
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();
        mlock.acquire();
    }

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return i_setStateError();
    }

    if (m->type == aType)
    {
        /* Nothing to do */
        return S_OK;
    }

    DeviceType_T devType = i_getDeviceType();
    // DVD media can only be readonly.
    if (devType == DeviceType_DVD && aType != MediumType_Readonly)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Cannot change the type of DVD medium '%s'"),
                        m->strLocationFull.c_str());
    // Floppy media can only be writethrough or readonly.
    if (   devType == DeviceType_Floppy
        && aType != MediumType_Writethrough
        && aType != MediumType_Readonly)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Cannot change the type of floppy medium '%s'"),
                        m->strLocationFull.c_str());

    /* cannot change the type of a differencing medium */
    if (m->pParent)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Cannot change the type of medium '%s' because it is a differencing medium"),
                        m->strLocationFull.c_str());

    /* Cannot change the type of a medium being in use by more than one VM.
     * If the change is to Immutable or MultiAttach then it must not be
     * directly attached to any VM, otherwise the assumptions about indirect
     * attachment elsewhere are violated and the VM becomes inaccessible.
     * Attaching an immutable medium triggers the diff creation, and this is
     * vital for the correct operation. */
    if (   m->backRefs.size() > 1
        || (   (   aType == MediumType_Immutable
                || aType == MediumType_MultiAttach)
            && m->backRefs.size() > 0))
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Cannot change the type of medium '%s' because it is attached to %d virtual machines",
                           "", m->backRefs.size()),
                        m->strLocationFull.c_str(), m->backRefs.size());

    switch (aType)
    {
        case MediumType_Normal:
        case MediumType_Immutable:
        case MediumType_MultiAttach:
        {
            /* normal can be easily converted to immutable and vice versa even
             * if they have children as long as they are not attached to any
             * machine themselves */
            break;
        }
        case MediumType_Writethrough:
        case MediumType_Shareable:
        case MediumType_Readonly:
        {
            /* cannot change to writethrough, shareable or readonly
             * if there are children */
            if (i_getChildren().size() != 0)
                return setError(VBOX_E_OBJECT_IN_USE,
                                tr("Cannot change type for medium '%s' since it has %d child media", "", i_getChildren().size()),
                                m->strLocationFull.c_str(), i_getChildren().size());
            if (aType == MediumType_Shareable)
            {
                MediumVariant_T variant = i_getVariant();
                if (!(variant & MediumVariant_Fixed))
                    return setError(VBOX_E_INVALID_OBJECT_STATE,
                                    tr("Cannot change type for medium '%s' to 'Shareable' since it is a dynamic medium storage unit"),
                                    m->strLocationFull.c_str());
            }
            else if (aType == MediumType_Readonly && devType == DeviceType_HardDisk)
            {
                // Readonly hard disks are not allowed, this medium type is reserved for
                // DVDs and floppy images at the moment. Later we might allow readonly hard
                // disks, but that's extremely unusual and many guest OSes will have trouble.
                return setError(VBOX_E_INVALID_OBJECT_STATE,
                                tr("Cannot change type for medium '%s' to 'Readonly' since it is a hard disk"),
                                m->strLocationFull.c_str());
            }
            break;
        }
        default:
            AssertFailedReturn(E_FAIL);
    }

    if (aType == MediumType_MultiAttach)
    {
        // This type is new with VirtualBox 4.0 and therefore requires settings
        // version 1.11 in the settings backend. Unfortunately it is not enough to do
        // the usual routine in MachineConfigFile::bumpSettingsVersionIfNeeded() for
        // two reasons: The medium type is a property of the media registry tree, which
        // can reside in the global config file (for pre-4.0 media); we would therefore
        // possibly need to bump the global config version. We don't want to do that though
        // because that might make downgrading to pre-4.0 impossible.
        // As a result, we can only use these two new types if the medium is NOT in the
        // global registry:
        const Guid &uuidGlobalRegistry = m->pVirtualBox->i_getGlobalRegistryId();
        if (i_isInRegistry(uuidGlobalRegistry))
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("Cannot change type for medium '%s': the media type 'MultiAttach' can only be used "
                               "on media registered with a machine that was created with VirtualBox 4.0 or later"),
                            m->strLocationFull.c_str());
    }

    m->type = aType;

    // save the settings
    mlock.release();
    autoCaller.release();
    treeLock.release();
    i_markRegistriesModified();
    m->pVirtualBox->i_saveModifiedRegistries();
    m->pVirtualBox->i_onMediumConfigChanged(this);

    return S_OK;
}

HRESULT Medium::getAllowedTypes(std::vector<MediumType_T> &aAllowedTypes)
{
    NOREF(aAllowedTypes);
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ReturnComNotImplemented();
}

HRESULT Medium::getParent(AutoCaller &autoCaller, ComPtr<IMedium> &aParent)
{
    autoCaller.release();

    /* It is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, see #uninit(). */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

    /* we access m->pParent */
    AutoReadLock treeLock(!pVirtualBox.isNull() ? &pVirtualBox->i_getMediaTreeLockHandle() : NULL COMMA_LOCKVAL_SRC_POS);

    autoCaller.add();
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    m->pParent.queryInterfaceTo(aParent.asOutParam());

    return S_OK;
}

HRESULT Medium::getChildren(AutoCaller &autoCaller, std::vector<ComPtr<IMedium> > &aChildren)
{
    autoCaller.release();

    /* It is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, see #uninit(). */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

    /* we access children */
    AutoReadLock treeLock(!pVirtualBox.isNull() ? &pVirtualBox->i_getMediaTreeLockHandle() : NULL COMMA_LOCKVAL_SRC_POS);

    autoCaller.add();
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    MediaList children(this->i_getChildren());
    aChildren.resize(children.size());
    size_t i = 0;
    for (MediaList::const_iterator it =  children.begin(); it != children.end(); ++it, ++i)
        (*it).queryInterfaceTo(aChildren[i].asOutParam());
    return S_OK;
}

HRESULT Medium::getBase(AutoCaller &autoCaller, ComPtr<IMedium> &aBase)
{
    autoCaller.release();

    /* i_getBase() will do callers/locking */
    i_getBase().queryInterfaceTo(aBase.asOutParam());

    return S_OK;
}

HRESULT Medium::getReadOnly(AutoCaller &autoCaller, BOOL *aReadOnly)
{
    autoCaller.release();

    /* isReadOnly() will do locking */
    *aReadOnly = i_isReadOnly();

    return S_OK;
}

HRESULT Medium::getLogicalSize(LONG64 *aLogicalSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aLogicalSize = (LONG64)m->logicalSize;

    return S_OK;
}

HRESULT Medium::getAutoReset(BOOL *aAutoReset)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->pParent.isNull())
        *aAutoReset = FALSE;
    else
        *aAutoReset = m->autoReset;

    return S_OK;
}

HRESULT Medium::setAutoReset(BOOL aAutoReset)
{
    AutoWriteLock mlock(this COMMA_LOCKVAL_SRC_POS);

    if (m->pParent.isNull())
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Medium '%s' is not differencing"),
                        m->strLocationFull.c_str());

    if (m->autoReset != !!aAutoReset)
    {
        m->autoReset = !!aAutoReset;

        // save the settings
        mlock.release();
        i_markRegistriesModified();
        m->pVirtualBox->i_saveModifiedRegistries();
        m->pVirtualBox->i_onMediumConfigChanged(this);
    }

    return S_OK;
}

HRESULT Medium::getLastAccessError(com::Utf8Str &aLastAccessError)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aLastAccessError = m->strLastAccessError;

    return S_OK;
}

HRESULT Medium::getMachineIds(std::vector<com::Guid> &aMachineIds)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->backRefs.size() != 0)
    {
        BackRefList brlist(m->backRefs);
        aMachineIds.resize(brlist.size());
        size_t i = 0;
        for (BackRefList::const_iterator it = brlist.begin(); it != brlist.end(); ++it, ++i)
             aMachineIds[i] = it->machineId;
    }

    return S_OK;
}

HRESULT Medium::setIds(AutoCaller &autoCaller,
                       BOOL aSetImageId,
                       const com::Guid &aImageId,
                       BOOL aSetParentId,
                       const com::Guid &aParentId)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    if (m->queryInfoRunning)
    {
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs this
         * lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        while (m->queryInfoRunning)
        {
            alock.release();
            /* must not hold the object lock now */
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            alock.acquire();
        }
    }

    switch (m->state)
    {
        case MediumState_Created:
            break;
        default:
            return i_setStateError();
    }

    Guid imageId, parentId;
    if (aSetImageId)
    {
        if (aImageId.isZero())
            imageId.create();
        else
        {
            imageId = aImageId;
            if (!imageId.isValid())
                return setError(E_INVALIDARG, tr("Argument %s is invalid"), "aImageId");
        }
    }
    if (aSetParentId)
    {
        if (aParentId.isZero())
            parentId.create();
        else
            parentId = aParentId;
    }

    const Guid uPrevImage = m->uuidImage;
    unconst(m->uuidImage) = imageId;
    ComObjPtr<Medium> pPrevParent = i_getParent();
    unconst(m->uuidParentImage) = parentId;

    // must not hold any locks before calling Medium::i_queryInfo
    alock.release();

    HRESULT hrc = i_queryInfo(!!aSetImageId /* fSetImageId */,
                              !!aSetParentId /* fSetParentId */,
                              autoCaller);

    AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);
    const Guid uCurrImage = m->uuidImage;
    ComObjPtr<Medium> pCurrParent = i_getParent();
    arlock.release();

    if (SUCCEEDED(hrc))
    {
        if (uCurrImage != uPrevImage)
            m->pVirtualBox->i_onMediumConfigChanged(this);
        if (pPrevParent != pCurrParent)
        {
            if (pPrevParent)
                m->pVirtualBox->i_onMediumConfigChanged(pPrevParent);
            if (pCurrParent)
                m->pVirtualBox->i_onMediumConfigChanged(pCurrParent);
        }
    }

    return hrc;
}

HRESULT Medium::refreshState(AutoCaller &autoCaller, MediumState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        {
            // must not hold any locks before calling Medium::i_queryInfo
            alock.release();

            hrc = i_queryInfo(false /* fSetImageId */, false /* fSetParentId */, autoCaller);

            alock.acquire();
            break;
        }
        default:
            break;
    }

    *aState = m->state;

    return hrc;
}

HRESULT Medium::getSnapshotIds(const com::Guid &aMachineId,
                               std::vector<com::Guid> &aSnapshotIds)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (BackRefList::const_iterator it = m->backRefs.begin();
         it != m->backRefs.end(); ++it)
    {
        if (it->machineId == aMachineId)
        {
            size_t size = it->llSnapshotIds.size();

            /* if the medium is attached to the machine in the current state, we
             * return its ID as the first element of the array */
            if (it->fInCurState)
                ++size;

            if (size > 0)
            {
                aSnapshotIds.resize(size);

                size_t j = 0;
                if (it->fInCurState)
                    aSnapshotIds[j++] = it->machineId.toUtf16();

                for(std::list<SnapshotRef>::const_iterator jt = it->llSnapshotIds.begin(); jt != it->llSnapshotIds.end(); ++jt, ++j)
                    aSnapshotIds[j] = jt->snapshotId;
            }

            break;
        }
    }

    return S_OK;
}

HRESULT Medium::lockRead(ComPtr<IToken> &aToken)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    if (m->queryInfoRunning)
    {
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs this
         * lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        while (m->queryInfoRunning)
        {
            alock.release();
            /* must not hold the object lock now */
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            alock.acquire();
        }
    }

    HRESULT hrc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        {
            ++m->readers;

            ComAssertMsgBreak(m->readers != 0, (tr("Counter overflow")), hrc = E_FAIL);

            /* Remember pre-lock state */
            if (m->state != MediumState_LockedRead)
                m->preLockState = m->state;

            LogFlowThisFunc(("Okay - prev state=%d readers=%d\n", m->state, m->readers));
            m->state = MediumState_LockedRead;

            ComObjPtr<MediumLockToken> pToken;
            hrc = pToken.createObject();
            if (SUCCEEDED(hrc))
                hrc = pToken->init(this, false /* fWrite */);
            if (FAILED(hrc))
            {
                --m->readers;
                if (m->readers == 0)
                    m->state = m->preLockState;
                return hrc;
            }

            pToken.queryInterfaceTo(aToken.asOutParam());
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d\n", m->state));
            hrc = i_setStateError();
            break;
        }
    }

    return hrc;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
HRESULT Medium::i_unlockRead(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    switch (m->state)
    {
        case MediumState_LockedRead:
        {
            ComAssertMsgBreak(m->readers != 0, (tr("Counter underflow")), hrc = E_FAIL);
            --m->readers;

            /* Reset the state after the last reader */
            if (m->readers == 0)
            {
                m->state = m->preLockState;
                /* There are cases where we inject the deleting state into
                 * a medium locked for reading. Make sure #unmarkForDeletion()
                 * gets the right state afterwards. */
                if (m->preLockState == MediumState_Deleting)
                    m->preLockState = MediumState_Created;
            }

            LogFlowThisFunc(("new state=%d\n", m->state));
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d\n", m->state));
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE, tr("Medium '%s' is not locked for reading"), m->strLocationFull.c_str());
            break;
        }
    }

    /* return the current state after */
    if (aState)
        *aState = m->state;

    return hrc;
}
HRESULT Medium::lockWrite(ComPtr<IToken> &aToken)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    if (m->queryInfoRunning)
    {
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs this
         * lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        while (m->queryInfoRunning)
        {
            alock.release();
            /* must not hold the object lock now */
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            alock.acquire();
        }
    }

    HRESULT hrc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        {
            m->preLockState = m->state;

            LogFlowThisFunc(("Okay - prev state=%d locationFull=%s\n", m->state, i_getLocationFull().c_str()));
            m->state = MediumState_LockedWrite;

            ComObjPtr<MediumLockToken> pToken;
            hrc = pToken.createObject();
            if (SUCCEEDED(hrc))
                hrc = pToken->init(this, true /* fWrite */);
            if (FAILED(hrc))
            {
                m->state = m->preLockState;
                return hrc;
            }

            pToken.queryInterfaceTo(aToken.asOutParam());
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d locationFull=%s\n", m->state, i_getLocationFull().c_str()));
            hrc = i_setStateError();
            break;
        }
    }

    return hrc;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
HRESULT Medium::i_unlockWrite(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    switch (m->state)
    {
        case MediumState_LockedWrite:
        {
            m->state = m->preLockState;
            /* There are cases where we inject the deleting state into
             * a medium locked for writing. Make sure #unmarkForDeletion()
             * gets the right state afterwards. */
            if (m->preLockState == MediumState_Deleting)
                m->preLockState = MediumState_Created;
            LogFlowThisFunc(("new state=%d locationFull=%s\n", m->state, i_getLocationFull().c_str()));
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d locationFull=%s\n", m->state, i_getLocationFull().c_str()));
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE, tr("Medium '%s' is not locked for writing"), m->strLocationFull.c_str());
            break;
        }
    }

    /* return the current state after */
    if (aState)
        *aState = m->state;

    return hrc;
}

HRESULT Medium::close(AutoCaller &aAutoCaller)
{
    // make a copy of VirtualBox pointer which gets nulled by uninit()
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

    Guid uId = i_getId();
    DeviceType_T devType = i_getDeviceType();
    MultiResult mrc = i_close(aAutoCaller);

    pVirtualBox->i_saveModifiedRegistries();

    if (SUCCEEDED(mrc) && uId.isValid() && !uId.isZero())
        pVirtualBox->i_onMediumRegistered(uId, devType, FALSE);

    return mrc;
}

HRESULT Medium::getProperty(const com::Utf8Str &aName,
                            com::Utf8Str &aValue)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::StringsMap::const_iterator it = m->mapProperties.find(aName);
    if (it == m->mapProperties.end())
    {
        if (!aName.startsWith("Special/"))
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Property '%s' does not exist"), aName.c_str());
        else
            /* be more silent here */
            return VBOX_E_OBJECT_NOT_FOUND;
    }

    aValue = it->second;

    return S_OK;
}

HRESULT Medium::setProperty(const com::Utf8Str &aName,
                            const com::Utf8Str &aValue)
{
    AutoWriteLock mlock(this COMMA_LOCKVAL_SRC_POS);

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    if (m->queryInfoRunning)
    {
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs this
         * lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        while (m->queryInfoRunning)
        {
            mlock.release();
            /* must not hold the object lock now */
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            mlock.acquire();
        }
    }

    switch (m->state)
    {
        case MediumState_NotCreated:
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return i_setStateError();
    }

    settings::StringsMap::iterator it = m->mapProperties.find(aName);
    if (   !aName.startsWith("Special/")
        && !i_isPropertyForFilter(aName))
    {
        if (it == m->mapProperties.end())
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Property '%s' does not exist"),
                            aName.c_str());
        it->second = aValue;
    }
    else
    {
        if (it == m->mapProperties.end())
        {
            if (!aValue.isEmpty())
                m->mapProperties[aName] = aValue;
        }
        else
        {
            if (!aValue.isEmpty())
                it->second = aValue;
            else
                m->mapProperties.erase(it);
        }
    }

    // save the settings
    mlock.release();
    i_markRegistriesModified();
    m->pVirtualBox->i_saveModifiedRegistries();
    m->pVirtualBox->i_onMediumConfigChanged(this);

    return S_OK;
}

HRESULT Medium::getProperties(const com::Utf8Str &aNames,
                              std::vector<com::Utf8Str> &aReturnNames,
                              std::vector<com::Utf8Str> &aReturnValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /// @todo make use of aNames according to the documentation
    NOREF(aNames);

    aReturnNames.resize(m->mapProperties.size());
    aReturnValues.resize(m->mapProperties.size());
    size_t i = 0;
    for (settings::StringsMap::const_iterator it = m->mapProperties.begin();
         it != m->mapProperties.end();
         ++it, ++i)
    {
        aReturnNames[i] = it->first;
        aReturnValues[i] = it->second;
    }
    return S_OK;
}

HRESULT Medium::setProperties(const std::vector<com::Utf8Str> &aNames,
                              const std::vector<com::Utf8Str> &aValues)
{
    AutoWriteLock mlock(this COMMA_LOCKVAL_SRC_POS);

    /* first pass: validate names */
    for (size_t i = 0;
         i < aNames.size();
         ++i)
    {
        Utf8Str strName(aNames[i]);
        if (   !strName.startsWith("Special/")
            && !i_isPropertyForFilter(strName)
            && m->mapProperties.find(strName) == m->mapProperties.end())
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Property '%s' does not exist"), strName.c_str());
    }

    /* second pass: assign */
    for (size_t i = 0;
         i < aNames.size();
         ++i)
    {
        Utf8Str strName(aNames[i]);
        Utf8Str strValue(aValues[i]);
        settings::StringsMap::iterator it = m->mapProperties.find(strName);
        if (   !strName.startsWith("Special/")
            && !i_isPropertyForFilter(strName))
        {
            AssertReturn(it != m->mapProperties.end(), E_FAIL);
            it->second = strValue;
        }
        else
        {
            if (it == m->mapProperties.end())
            {
                if (!strValue.isEmpty())
                    m->mapProperties[strName] = strValue;
            }
            else
            {
                if (!strValue.isEmpty())
                    it->second = strValue;
                else
                    m->mapProperties.erase(it);
            }
        }
    }

    // save the settings
    mlock.release();
    i_markRegistriesModified();
    m->pVirtualBox->i_saveModifiedRegistries();
    m->pVirtualBox->i_onMediumConfigChanged(this);

    return S_OK;
}

HRESULT Medium::createBaseStorage(LONG64 aLogicalSize,
                                  const std::vector<MediumVariant_T> &aVariant,
                                  ComPtr<IProgress> &aProgress)
{
    if (aLogicalSize < 0)
        return setError(E_INVALIDARG, tr("The medium size argument (%lld) is negative"), aLogicalSize);

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        ULONG mediumVariantFlags = 0;

        if (aVariant.size())
        {
            for (size_t i = 0; i < aVariant.size(); i++)
                mediumVariantFlags |= (ULONG)aVariant[i];
        }

        mediumVariantFlags &= ((unsigned)~MediumVariant_Diff);

        if (    !(mediumVariantFlags & MediumVariant_Fixed)
            &&  !(m->formatObj->i_getCapabilities() & MediumFormatCapabilities_CreateDynamic))
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium format '%s' does not support dynamic storage creation"),
                           m->strFormat.c_str());

        if (    (mediumVariantFlags & MediumVariant_Fixed)
            &&  !(m->formatObj->i_getCapabilities() & MediumFormatCapabilities_CreateFixed))
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium format '%s' does not support fixed storage creation"),
                           m->strFormat.c_str());

        if (    (mediumVariantFlags & MediumVariant_Formatted)
            &&  i_getDeviceType() != DeviceType_Floppy)
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium variant 'formatted' applies to floppy images only"));

        if (m->state != MediumState_NotCreated)
            throw i_setStateError();

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast<IMedium*>(this),
                              mediumVariantFlags & MediumVariant_Fixed
                              ? BstrFmt(tr("Creating fixed medium storage unit '%s'"), m->strLocationFull.c_str()).raw()
                              : BstrFmt(tr("Creating dynamic medium storage unit '%s'"), m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
            throw hrc;

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::CreateBaseTask(this, pProgress, (uint64_t)aLogicalSize,
                                           (MediumVariant_T)mediumVariantFlags);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;

        m->state = MediumState_Creating;
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;

        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

HRESULT Medium::deleteStorage(ComPtr<IProgress> &aProgress)
{
    ComObjPtr<Progress> pProgress;

    MultiResult mrc = i_deleteStorage(&pProgress,
                                      false /* aWait */,
                                      true /* aNotify */);
    /* Must save the registries in any case, since an entry was removed. */
    m->pVirtualBox->i_saveModifiedRegistries();

    if (SUCCEEDED(mrc))
        pProgress.queryInterfaceTo(aProgress.asOutParam());

    return mrc;
}

HRESULT Medium::createDiffStorage(AutoCaller &autoCaller,
                                  const ComPtr<IMedium> &aTarget,
                                  const std::vector<MediumVariant_T> &aVariant,
                                  ComPtr<IProgress> &aProgress)
{
    IMedium *aT = aTarget;
    ComObjPtr<Medium> diff = static_cast<Medium*>(aT);

    autoCaller.release();

    /* It is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, see #uninit(). */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

    // we access m->pParent
    AutoReadLock treeLock(!pVirtualBox.isNull() ? &pVirtualBox->i_getMediaTreeLockHandle() : NULL COMMA_LOCKVAL_SRC_POS);

    autoCaller.add();
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoMultiWriteLock2 alock(this, diff COMMA_LOCKVAL_SRC_POS);

    if (m->type == MediumType_Writethrough)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Medium type of '%s' is Writethrough"),
                        m->strLocationFull.c_str());
    else if (m->type == MediumType_Shareable)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Medium type of '%s' is Shareable"),
                        m->strLocationFull.c_str());
    else if (m->type == MediumType_Readonly)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Medium type of '%s' is Readonly"),
                        m->strLocationFull.c_str());

    /* Apply the normal locking logic to the entire chain. */
    MediumLockList *pMediumLockList(new MediumLockList());
    alock.release();
    autoCaller.release();
    treeLock.release();
    HRESULT hrc = diff->i_createMediumLockList(true /* fFailIfInaccessible */,
                                               diff /* pToLockWrite */,
                                               false /* fMediumLockWriteAll */,
                                               this,
                                               *pMediumLockList);
    treeLock.acquire();
    autoCaller.add();
    if (FAILED(autoCaller.hrc()))
        hrc = autoCaller.hrc();
    alock.acquire();
    if (FAILED(hrc))
    {
        delete pMediumLockList;
        return hrc;
    }

    alock.release();
    autoCaller.release();
    treeLock.release();
    hrc = pMediumLockList->Lock();
    treeLock.acquire();
    autoCaller.add();
    if (FAILED(autoCaller.hrc()))
        hrc = autoCaller.hrc();
    alock.acquire();
    if (FAILED(hrc))
    {
        delete pMediumLockList;

        return setError(hrc, tr("Could not lock medium when creating diff '%s'"),
                        diff->i_getLocationFull().c_str());
    }

    Guid parentMachineRegistry;
    if (i_getFirstRegistryMachineId(parentMachineRegistry))
    {
        /* since this medium has been just created it isn't associated yet */
        diff->m->llRegistryIDs.push_back(parentMachineRegistry);
        alock.release();
        autoCaller.release();
        treeLock.release();
        diff->i_markRegistriesModified();
        treeLock.acquire();
        autoCaller.add();
        alock.acquire();
    }

    alock.release();
    autoCaller.release();
    treeLock.release();

    ComObjPtr<Progress> pProgress;

    ULONG mediumVariantFlags = 0;

    if (aVariant.size())
    {
        for (size_t i = 0; i < aVariant.size(); i++)
            mediumVariantFlags |= (ULONG)aVariant[i];
    }

    if (mediumVariantFlags & MediumVariant_Formatted)
    {
        delete pMediumLockList;
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Medium variant 'formatted' applies to floppy images only"));
    }

    hrc = i_createDiffStorage(diff, (MediumVariant_T)mediumVariantFlags, pMediumLockList,
                              &pProgress, false /* aWait */, true /* aNotify */);
    if (FAILED(hrc))
        delete pMediumLockList;
    else
        pProgress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

HRESULT Medium::mergeTo(const ComPtr<IMedium> &aTarget,
                        ComPtr<IProgress> &aProgress)
{
    IMedium *aT = aTarget;

    ComAssertRet(aT != this, E_INVALIDARG);

    ComObjPtr<Medium> pTarget = static_cast<Medium*>(aT);

    bool fMergeForward = false;
    ComObjPtr<Medium> pParentForTarget;
    MediumLockList *pChildrenToReparent = NULL;
    MediumLockList *pMediumLockList = NULL;

    HRESULT hrc = i_prepareMergeTo(pTarget, NULL, NULL, true, fMergeForward,
                                   pParentForTarget, pChildrenToReparent, pMediumLockList);
    if (FAILED(hrc)) return hrc;

    ComObjPtr<Progress> pProgress;

    hrc = i_mergeTo(pTarget, fMergeForward, pParentForTarget, pChildrenToReparent,
                    pMediumLockList, &pProgress, false /* aWait */, true /* aNotify */);
    if (FAILED(hrc))
        i_cancelMergeTo(pChildrenToReparent, pMediumLockList);
    else
        pProgress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

HRESULT Medium::cloneToBase(const ComPtr<IMedium> &aTarget,
                            const std::vector<MediumVariant_T> &aVariant,
                            ComPtr<IProgress> &aProgress)
{
     return cloneTo(aTarget, aVariant, NULL, aProgress);
}

HRESULT Medium::cloneTo(const ComPtr<IMedium> &aTarget,
                        const std::vector<MediumVariant_T> &aVariant,
                        const ComPtr<IMedium> &aParent,
                        ComPtr<IProgress> &aProgress)
{
    return resizeAndCloneTo(aTarget, 0, aVariant, aParent, aProgress);
}

/**
 * This is a helper function that combines the functionality of
 * Medium::cloneTo() and Medium::resize(). The target medium will take the
 * contents of the calling medium.
 *
 * @param aTarget           Medium to resize and clone to
 * @param aLogicalSize      Desired size for targer medium
 * @param aVariant
 * @param aParent
 * @param aProgress
 * @return HRESULT
 */
HRESULT Medium::resizeAndCloneTo(const ComPtr<IMedium> &aTarget,
                                 LONG64 aLogicalSize,
                                 const std::vector<MediumVariant_T> &aVariant,
                                 const ComPtr<IMedium> &aParent,
                                 ComPtr<IProgress> &aProgress)
{
    /* Check for valid args */
    ComAssertRet(aTarget != this, E_INVALIDARG);
    CheckComArgExpr(aLogicalSize, aLogicalSize >= 0);

    /* Convert args to usable/needed types */
    IMedium *aT = aTarget;
    ComObjPtr<Medium> pTarget = static_cast<Medium*>(aT);
    ComObjPtr<Medium> pParent;
    if (aParent)
    {
        IMedium *aP = aParent;
        pParent = static_cast<Medium*>(aP);
    }

    /* Set up variables. Fetch needed data in lockable blocks */
    HRESULT hrc = S_OK;
    Medium::Task *pTask = NULL;

    Utf8Str strSourceName;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        strSourceName = i_getName();
    }

    uint64_t uTargetExistingSize = 0;
    Utf8Str strTargetName;
    {
        AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
        uTargetExistingSize = pTarget->i_getLogicalSize();
        strTargetName = pTarget->i_getName();
    }

    /* Set up internal multi-subprocess progress object */
    ComObjPtr<Progress> pProgress;
    pProgress.createObject();
    hrc = pProgress->init(m->pVirtualBox,
                          static_cast<IMedium*>(this),
                          BstrFmt(tr("Resizing medium and cloning into it")).raw(),
                          TRUE, /* aCancelable */
                          2, /* Number of opearations */
                          BstrFmt(tr("Resizing medium before clone")).raw()
                          );
    if (FAILED(hrc))
        return hrc;

    /** @todo r=jack: check below block  for correct lock handling */
    /* If target does not exist, handle resize. */
    if (pTarget->m->state != MediumState_NotCreated && aLogicalSize > 0)
    {
        if ((LONG64)uTargetExistingSize != aLogicalSize) {
            if (!i_isMediumFormatFile())
            {
                hrc = setError(VBOX_E_NOT_SUPPORTED,
                               tr("Sizes of '%s' and '%s' are different and medium format does not support resing"),
                               strSourceName.c_str(), strTargetName.c_str());
                return hrc;
            }

            /**
             * Need to lock the target medium as i_resize does do so
             * automatically.
             */

            ComPtr<IToken> pToken;
            hrc = pTarget->LockWrite(pToken.asOutParam());

            if (FAILED(hrc)) return hrc;

            /**
             * Have to make own lock list, because "resize" method resizes only
             * last image in the lock chain.
             */

            MediumLockList* pMediumLockListForResize = new MediumLockList();
            pMediumLockListForResize->Append(pTarget, pTarget->m->state == MediumState_LockedWrite);

            hrc = pMediumLockListForResize->Lock(true /* fSkipOverLockedMedia */);
            if (FAILED(hrc))
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                hrc = setError(hrc,
                               tr("Failed to lock the medium '%s' to resize before merge"),
                               strTargetName.c_str());
                delete pMediumLockListForResize;
                return hrc;
            }


            hrc = pTarget->i_resize((uint64_t)aLogicalSize, pMediumLockListForResize, &pProgress, true, false);
            if (FAILED(hrc))
            {
                /* No need to setError becasue i_resize and i_taskResizeHandler handle this automatically. */
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                delete pMediumLockListForResize;
                return hrc;
            }

            delete pMediumLockListForResize;

            pTarget->m->logicalSize = (uint64_t)aLogicalSize;

            pToken->Abandon();
            pToken.setNull();
        }
    }

    /* Report progress to supplied progress argument */
    if (SUCCEEDED(hrc))
    {
        pProgress.queryInterfaceTo(aProgress.asOutParam());
    }

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        uint32_t    cHandles    = 3;
        LockHandle* pHandles[4] = { &m->pVirtualBox->i_getMediaTreeLockHandle(),
                                    this->lockHandle(),
                                    pTarget->lockHandle() };
        /* Only add parent to the lock if it is not null */
        if (!pParent.isNull())
            pHandles[cHandles++] = pParent->lockHandle();
        AutoWriteLock alock(cHandles,
                            pHandles
                            COMMA_LOCKVAL_SRC_POS);

        if (    pTarget->m->state != MediumState_NotCreated
            &&  pTarget->m->state != MediumState_Created)
            throw pTarget->i_setStateError();

        /* Determine if cloning within the list */
        bool cloningWithinList = false;
        ComObjPtr<Medium> pMedium = i_getParent();
        while (!pMedium.isNull() && pMedium != pTarget)
            pMedium = pMedium->i_getParent();
        if (pMedium == pTarget)
            cloningWithinList = true;

        /* Build the source lock list. */
        MediumLockList *pSourceMediumLockList(new MediumLockList());
        alock.release();
        if (!cloningWithinList)
            hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                         NULL /* pToLockWrite */,
                                         false /* fMediumLockWriteAll */,
                                         NULL,
                                         *pSourceMediumLockList);
        else
            hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                         pTarget /* pToLockWrite */,
                                         false /* fMediumLockWriteAll */,
                                         NULL,
                                         *pSourceMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            throw hrc;
        }

        /* Build the target lock list (including the to-be parent chain). */
        MediumLockList *pTargetMediumLockList(new MediumLockList());
        alock.release();
        if (!cloningWithinList)
            hrc = pTarget->i_createMediumLockList(true /* fFailIfInaccessible */,
                                                  pTarget /* pToLockWrite */,
                                                  false /* fMediumLockWriteAll */,
                                                  pParent,
                                                  *pTargetMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw hrc;
        }

        alock.release();
        hrc = pSourceMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock source media '%s'"),
                           i_getLocationFull().c_str());
        }
        alock.release();
        hrc = pTargetMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock target media '%s'"),
                           pTarget->i_getLocationFull().c_str());
        }

        ULONG mediumVariantFlags = 0;

        if (aVariant.size())
        {
            for (size_t i = 0; i < aVariant.size(); i++)
                mediumVariantFlags |= (ULONG)aVariant[i];
        }

        if (mediumVariantFlags & MediumVariant_Formatted)
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium variant 'formatted' applies to floppy images only"));
        }

        if (pTarget->m->state != MediumState_NotCreated || aLogicalSize == 0)
        {
            /* setup task object to carry out the operation asynchronously */
            pTask = new Medium::CloneTask(this, pProgress, pTarget,
                                          (MediumVariant_T)mediumVariantFlags,
                                          pParent, UINT32_MAX, UINT32_MAX,
                                          pSourceMediumLockList, pTargetMediumLockList,
                                          false, false, true, 0);
        }
        else
        {
            /* setup task object to carry out the operation asynchronously */
            pTask = new Medium::CloneTask(this, pProgress, pTarget,
                                          (MediumVariant_T)mediumVariantFlags,
                                          pParent, UINT32_MAX, UINT32_MAX,
                                          pSourceMediumLockList, pTargetMediumLockList,
                                          false, false, true, (uint64_t)aLogicalSize);
        }

        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;

        if (pTarget->m->state == MediumState_NotCreated)
            pTarget->m->state = MediumState_Creating;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

HRESULT Medium::moveTo(AutoCaller &autoCaller, const com::Utf8Str &aLocation, ComPtr<IProgress> &aProgress)
{
    ComObjPtr<Medium> pParent;
    ComObjPtr<Progress> pProgress;
    HRESULT hrc = S_OK;
    Medium::Task *pTask = NULL;

    try
    {
    /// @todo NEWMEDIA for file names, add the default extension if no extension
    /// is present (using the information from the VD backend which also implies
    /// that one more parameter should be passed to moveTo() requesting
    /// that functionality since it is only allowed when called from this method

    /// @todo NEWMEDIA rename the file and set m->location on success, then save
    /// the global registry (and local registries of portable VMs referring to
    /// this medium), this will also require to add the mRegistered flag to data

        autoCaller.release();

        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* play with locations */
        {
            /* get source path and filename */
            Utf8Str sourcePath = i_getLocationFull();
            Utf8Str sourceFName = i_getName();

            if (aLocation.isEmpty())
            {
                hrc = setErrorVrc(VERR_PATH_ZERO_LENGTH,
                                  tr("Medium '%s' can't be moved. Destination path is empty."),
                                  i_getLocationFull().c_str());
                throw hrc;
            }

            /* extract destination path and filename */
            Utf8Str destPath(aLocation);
            Utf8Str destFName(destPath);
            destFName.stripPath();

            if (destFName.isNotEmpty() && !RTPathHasSuffix(destFName.c_str()))
            {
                /*
                 * The target path has no filename: Either "/path/to/new/location" or
                 * just "newname" (no trailing backslash or there is no filename extension).
                 */
                if (destPath.equals(destFName))
                {
                    /* new path contains only "newname", no path, no extension */
                    destFName.append(RTPathSuffix(sourceFName.c_str()));
                    destPath = destFName;
                }
                else
                {
                    /* new path looks like "/path/to/new/location" */
                    destFName.setNull();
                    destPath.append(RTPATH_SLASH);
                }
            }

            if (destFName.isEmpty())
            {
                /* No target name */
                destPath.append(sourceFName);
            }
            else
            {
                if (destPath.equals(destFName))
                {
                    /*
                     * The target path contains of only a filename without a directory.
                     * Move the medium within the source directory to the new name
                     * (actually rename operation).
                     * Scratches sourcePath!
                     */
                    destPath = sourcePath.stripFilename().append(RTPATH_SLASH).append(destFName);
                }

                const char *pszSuffix = RTPathSuffix(sourceFName.c_str());

                /* Suffix is empty and one is deduced from the medium format */
                if (pszSuffix == NULL)
                {
                    Utf8Str strExt = i_getFormat();
                    if (strExt.compare("RAW", Utf8Str::CaseInsensitive) == 0)
                    {
                        DeviceType_T devType = i_getDeviceType();
                        switch (devType)
                        {
                            case DeviceType_DVD:
                                strExt = "iso";
                                break;
                            case DeviceType_Floppy:
                                strExt = "img";
                                break;
                            default:
                                hrc = setErrorVrc(VERR_NOT_A_FILE, /** @todo r=bird: Mixing status codes again. */
                                                  tr("Medium '%s' has RAW type. \"Move\" operation isn't supported for this type."),
                                                  i_getLocationFull().c_str());
                                throw hrc;
                        }
                    }
                    else if (strExt.compare("Parallels", Utf8Str::CaseInsensitive) == 0)
                    {
                        strExt = "hdd";
                    }

                    /* Set the target extension like on the source. Any conversions are prohibited */
                    strExt.toLower();
                    destPath.stripSuffix().append('.').append(strExt);
                }
                else
                    destPath.stripSuffix().append(pszSuffix);
            }

            /* Simple check for existence */
            if (RTFileExists(destPath.c_str()))
                throw setError(VBOX_E_FILE_ERROR,
                               tr("The given path '%s' is an existing file. Delete or rename this file."),
                               destPath.c_str());

            if (!i_isMediumFormatFile())
                throw setErrorVrc(VERR_NOT_A_FILE,
                                  tr("Medium '%s' isn't a file object. \"Move\" operation isn't supported."),
                                  i_getLocationFull().c_str());
            /* Path must be absolute */
            if (!RTPathStartsWithRoot(destPath.c_str()))
                throw setError(VBOX_E_FILE_ERROR,
                               tr("The given path '%s' is not fully qualified"),
                               destPath.c_str());
            /* Check path for a new file object */
            hrc = VirtualBox::i_ensureFilePathExists(destPath, true);
            if (FAILED(hrc))
                throw hrc;

            /* Set needed variables for "moving" procedure. It'll be used later in separate thread task */
            hrc = i_preparationForMoving(destPath);
            if (FAILED(hrc))
                throw setErrorVrc(VERR_NO_CHANGE,
                                  tr("Medium '%s' is already in the correct location"),
                                  i_getLocationFull().c_str());
        }

        /* Check VMs which have this medium attached to*/
        std::vector<com::Guid> aMachineIds;
        hrc = getMachineIds(aMachineIds);
        if (FAILED(hrc))
            throw setError(hrc, tr("Failed to get machine list '%s' is attached to"), i_getLocationFull().c_str());

        std::vector<com::Guid>::const_iterator currMachineID = aMachineIds.begin();
        std::vector<com::Guid>::const_iterator lastMachineID = aMachineIds.end();

        while (currMachineID != lastMachineID)
        {
            Guid id(*currMachineID);
            ComObjPtr<Machine> aMachine;

            alock.release();
            autoCaller.release();
            treeLock.release();
            hrc = m->pVirtualBox->i_findMachine(id, false, true, &aMachine);
            treeLock.acquire();
            autoCaller.add();
            AssertComRCThrowRC(autoCaller.hrc());
            alock.acquire();

            if (SUCCEEDED(hrc))
            {
                ComObjPtr<SessionMachine> sm;
                ComPtr<IInternalSessionControl> ctl;

                alock.release();
                autoCaller.release();
                treeLock.release();
                bool ses = aMachine->i_isSessionOpenVM(sm, &ctl);
                treeLock.acquire();
                autoCaller.add();
                AssertComRCThrowRC(autoCaller.hrc());
                alock.acquire();

                if (ses)
                    throw setError(VBOX_E_INVALID_VM_STATE,
                                   tr("At least the VM '%s' to whom this medium '%s' attached has currently an opened session. Stop all VMs before relocating this medium"),
                                   id.toString().c_str(), i_getLocationFull().c_str());
            }
            ++currMachineID;
        }

        /* Build the source lock list. */
        MediumLockList *pMediumLockList(new MediumLockList());
        alock.release();
        autoCaller.release();
        treeLock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                     this /* pToLockWrite */,
                                     true /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        treeLock.acquire();
        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc, tr("Failed to create medium lock list for '%s'"), i_getLocationFull().c_str());
        }
        alock.release();
        autoCaller.release();
        treeLock.release();
        hrc = pMediumLockList->Lock();
        treeLock.acquire();
        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc, tr("Failed to lock media '%s'"), i_getLocationFull().c_str());
        }

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast <IMedium *>(this),
                              BstrFmt(tr("Moving medium '%s'"), m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);

        /* Do the disk moving. */
        if (SUCCEEDED(hrc))
        {
            ULONG mediumVariantFlags = i_getVariant();

            /* setup task object to carry out the operation asynchronously */
            pTask = new Medium::MoveTask(this, pProgress,
                                         (MediumVariant_T)mediumVariantFlags,
                                         pMediumLockList);
            hrc = pTask->hrc();
            AssertComRC(hrc);
            if (FAILED(hrc))
                throw hrc;
        }

    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else
    {
        if (pTask)
            delete pTask;
    }

    return hrc;
}

HRESULT Medium::setLocation(const com::Utf8Str &aLocation)
{
    HRESULT hrc = S_OK;

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        AutoCaller autoCaller(this);
        AssertComRCThrowRC(autoCaller.hrc());

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        Utf8Str destPath(aLocation);

        // some check for file based medium
        if (i_isMediumFormatFile())
        {
            /* Path must be absolute */
            if (!RTPathStartsWithRoot(destPath.c_str()))
                throw setError(VBOX_E_FILE_ERROR, tr("The given path '%s' is not fully qualified"), destPath.c_str());

            /* Simple check for existence */
            if (!RTFileExists(destPath.c_str()))
                throw setError(VBOX_E_FILE_ERROR,
                               tr("The given path '%s' is not an existing file. New location is invalid."),
                               destPath.c_str());
        }

        /* Check VMs which have this medium attached to*/
        std::vector<com::Guid> aMachineIds;
        hrc = getMachineIds(aMachineIds);

        // switch locks only if there are machines with this medium attached
        if (!aMachineIds.empty())
        {
            std::vector<com::Guid>::const_iterator currMachineID = aMachineIds.begin();
            std::vector<com::Guid>::const_iterator lastMachineID = aMachineIds.end();

            alock.release();
            autoCaller.release();
            treeLock.release();

            while (currMachineID != lastMachineID)
            {
                Guid id(*currMachineID);
                ComObjPtr<Machine> aMachine;
                hrc = m->pVirtualBox->i_findMachine(id, false, true, &aMachine);
                if (SUCCEEDED(hrc))
                {
                    ComObjPtr<SessionMachine> sm;
                    ComPtr<IInternalSessionControl> ctl;

                    bool ses = aMachine->i_isSessionOpenVM(sm, &ctl);
                    if (ses)
                    {
                        treeLock.acquire();
                        autoCaller.add();
                        AssertComRCThrowRC(autoCaller.hrc());
                        alock.acquire();

                        throw setError(VBOX_E_INVALID_VM_STATE,
                                       tr("At least the VM '%s' to whom this medium '%s' attached has currently an opened session. Stop all VMs before set location for this medium"),
                                       id.toString().c_str(),
                                       i_getLocationFull().c_str());
                    }
                }
                ++currMachineID;
            }

            treeLock.acquire();
            autoCaller.add();
            AssertComRCThrowRC(autoCaller.hrc());
            alock.acquire();
        }

        m->strLocationFull = destPath;

        // save the settings
        alock.release();
        autoCaller.release();
        treeLock.release();

        i_markRegistriesModified();
        m->pVirtualBox->i_saveModifiedRegistries();

        MediumState_T mediumState;
        refreshState(autoCaller, &mediumState);
        m->pVirtualBox->i_onMediumConfigChanged(this);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    return hrc;
}

HRESULT Medium::compact(ComPtr<IProgress> &aProgress)
{
    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* Build the medium lock list. */
        MediumLockList *pMediumLockList(new MediumLockList());
        alock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */ ,
                                     this /* pToLockWrite */,
                                     false /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        alock.release();
        hrc = pMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock media when compacting '%s'"),
                           i_getLocationFull().c_str());
        }

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast <IMedium *>(this),
                              BstrFmt(tr("Compacting medium '%s'"), m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::CompactTask(this, pProgress, pMediumLockList);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

HRESULT Medium::resize(LONG64 aLogicalSize,
                       ComPtr<IProgress> &aProgress)
{
    CheckComArgExpr(aLogicalSize, aLogicalSize > 0);
    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;

    /* Build the medium lock list. */
    MediumLockList *pMediumLockList(new MediumLockList());

    try
    {
        const char *pszError = NULL;

        hrc = i_createMediumLockList(true /* fFailIfInaccessible */ ,
                                     this /* pToLockWrite */,
                                     false /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        if (FAILED(hrc))
            pszError = tr("Failed to create medium lock list when resizing '%s'");
        else
        {
            hrc = pMediumLockList->Lock();
            if (FAILED(hrc))
                pszError = tr("Failed to lock media when resizing '%s'");
        }


        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (FAILED(hrc))
        {
            throw setError(hrc, pszError, i_getLocationFull().c_str());
        }

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast <IMedium *>(this),
                              BstrFmt(tr("Resizing medium '%s'"), m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
        {
            throw hrc;
        }
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
        hrc = i_resize((uint64_t)aLogicalSize, pMediumLockList, &pProgress, false /* aWait */, true /* aNotify */);

    if (SUCCEEDED(hrc))
        pProgress.queryInterfaceTo(aProgress.asOutParam());
    else
        delete pMediumLockList;

    return hrc;
}

HRESULT Medium::reset(AutoCaller &autoCaller, ComPtr<IProgress> &aProgress)
{
    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        autoCaller.release();

        /* It is possible that some previous/concurrent uninit has already
         * cleared the pVirtualBox reference, see #uninit(). */
        ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);

        /* i_canClose() needs the tree lock */
        AutoMultiWriteLock2 multilock(!pVirtualBox.isNull() ? &pVirtualBox->i_getMediaTreeLockHandle() : NULL,
                                      this->lockHandle()
                                      COMMA_LOCKVAL_SRC_POS);

        autoCaller.add();
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

        LogFlowThisFunc(("ENTER for medium %s\n", m->strLocationFull.c_str()));

        if (m->pParent.isNull())
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium type of '%s' is not differencing"),
                           m->strLocationFull.c_str());

        hrc = i_canClose();
        if (FAILED(hrc))
            throw hrc;

        /* Build the medium lock list. */
        MediumLockList *pMediumLockList(new MediumLockList());
        multilock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                     this /* pToLockWrite */,
                                     false /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        multilock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        multilock.release();
        hrc = pMediumLockList->Lock();
        multilock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock media when resetting '%s'"),
                           i_getLocationFull().c_str());
        }

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast<IMedium*>(this),
                              BstrFmt(tr("Resetting differencing medium '%s'"), m->strLocationFull.c_str()).raw(),
                              FALSE /* aCancelable */);
        if (FAILED(hrc))
            throw hrc;

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::ResetTask(this, pProgress, pMediumLockList);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else if (pTask != NULL)
        delete pTask;

    LogFlowThisFunc(("LEAVE, hrc=%Rhrc\n", hrc));

    return hrc;
}

HRESULT Medium::changeEncryption(const com::Utf8Str &aCurrentPassword, const com::Utf8Str &aCipher,
                                 const com::Utf8Str &aNewPassword, const com::Utf8Str &aNewPasswordId,
                                 ComPtr<IProgress> &aProgress)
{
    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        DeviceType_T devType = i_getDeviceType();
        /* Cannot encrypt DVD or floppy images so far. */
        if (   devType == DeviceType_DVD
            || devType == DeviceType_Floppy)
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("Cannot encrypt DVD or Floppy medium '%s'"),
                            m->strLocationFull.c_str());

        /* Cannot encrypt media which are attached to more than one virtual machine. */
        if (m->backRefs.size() > 1)
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("Cannot encrypt medium '%s' because it is attached to %d virtual machines", "", m->backRefs.size()),
                            m->strLocationFull.c_str(), m->backRefs.size());

        if (i_getChildren().size() != 0)
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("Cannot encrypt medium '%s' because it has %d children", "", i_getChildren().size()),
                            m->strLocationFull.c_str(), i_getChildren().size());

        /* Build the medium lock list. */
        MediumLockList *pMediumLockList(new MediumLockList());
        alock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */ ,
                                     this /* pToLockWrite */,
                                     true /* fMediumLockAllWrite */,
                                     NULL,
                                     *pMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        alock.release();
        hrc = pMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock media for encryption '%s'"),
                           i_getLocationFull().c_str());
        }

        /*
         * Check all media in the chain to not contain any branches or references to
         * other virtual machines, we support encrypting only a list of differencing media at the moment.
         */
        MediumLockList::Base::const_iterator mediumListBegin = pMediumLockList->GetBegin();
        MediumLockList::Base::const_iterator mediumListEnd = pMediumLockList->GetEnd();
        for (MediumLockList::Base::const_iterator it = mediumListBegin;
             it != mediumListEnd;
             ++it)
        {
            const MediumLock &mediumLock = *it;
            const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
            AutoReadLock mediumReadLock(pMedium COMMA_LOCKVAL_SRC_POS);

            Assert(pMedium->m->state == MediumState_LockedWrite);

            if (pMedium->m->backRefs.size() > 1)
            {
                hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Cannot encrypt medium '%s' because it is attached to %d virtual machines", "",
                                  pMedium->m->backRefs.size()),
                               pMedium->m->strLocationFull.c_str(), pMedium->m->backRefs.size());
                break;
            }
            else if (pMedium->i_getChildren().size() > 1)
            {
                hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Cannot encrypt medium '%s' because it has %d children", "", pMedium->i_getChildren().size()),
                               pMedium->m->strLocationFull.c_str(), pMedium->i_getChildren().size());
                break;
            }
        }

        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        const char *pszAction = tr("Encrypting medium");
        if (   aCurrentPassword.isNotEmpty()
            && aCipher.isEmpty())
            pszAction = tr("Decrypting medium");

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast <IMedium *>(this),
                              BstrFmt("%s '%s'", pszAction, m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::EncryptTask(this, aNewPassword, aCurrentPassword,
                                        aCipher, aNewPasswordId, pProgress, pMediumLockList);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress.asOutParam());
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

HRESULT Medium::getEncryptionSettings(AutoCaller &autoCaller, com::Utf8Str &aCipher, com::Utf8Str &aPasswordId)
{
#ifndef VBOX_WITH_EXTPACK
    RT_NOREF(aCipher, aPasswordId);
#endif
    HRESULT hrc = S_OK;

    try
    {
        autoCaller.release();
        ComObjPtr<Medium> pBase = i_getBase();
        autoCaller.add();
        if (FAILED(autoCaller.hrc()))
            throw hrc;
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* Check whether encryption is configured for this medium. */
        settings::StringsMap::iterator it = pBase->m->mapProperties.find("CRYPT/KeyStore");
        if (it == pBase->m->mapProperties.end())
            throw VBOX_E_NOT_SUPPORTED;

# ifdef VBOX_WITH_EXTPACK
        ExtPackManager *pExtPackManager = m->pVirtualBox->i_getExtPackManager();
        if (pExtPackManager->i_isExtPackUsable(ORACLE_PUEL_EXTPACK_NAME))
        {
            /* Load the plugin */
            Utf8Str strPlugin;
            hrc = pExtPackManager->i_getLibraryPathForExtPack(g_szVDPlugin, ORACLE_PUEL_EXTPACK_NAME, &strPlugin);
            if (SUCCEEDED(hrc))
            {
                int vrc = VDPluginLoadFromFilename(strPlugin.c_str());
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Retrieving encryption settings of the image failed because the encryption plugin could not be loaded (%s)"),
                                       i_vdError(vrc).c_str());
            }
            else
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Encryption is not supported because the extension pack '%s' is missing the encryption plugin (old extension pack installed?)"),
                               ORACLE_PUEL_EXTPACK_NAME);
        }
        else
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Encryption is not supported because the extension pack '%s' is missing"),
                           ORACLE_PUEL_EXTPACK_NAME);

        PVDISK pDisk = NULL;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &pDisk);
        ComAssertRCThrow(vrc, E_FAIL);

        MediumCryptoFilterSettings CryptoSettings;

        i_taskEncryptSettingsSetup(&CryptoSettings, NULL, it->second.c_str(), NULL, false /* fCreateKeyStore */);
        vrc = VDFilterAdd(pDisk, "CRYPT", VD_FILTER_FLAGS_READ | VD_FILTER_FLAGS_INFO, CryptoSettings.vdFilterIfaces);
        if (RT_FAILURE(vrc))
            throw setErrorBoth(VBOX_E_INVALID_OBJECT_STATE, vrc,
                               tr("Failed to load the encryption filter: %s"),
                               i_vdError(vrc).c_str());

        it = pBase->m->mapProperties.find("CRYPT/KeyId");
        if (it == pBase->m->mapProperties.end())
            throw setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Image is configured for encryption but doesn't has a KeyId set"));

        aPasswordId = it->second.c_str();
        aCipher = CryptoSettings.pszCipherReturned;
        RTStrFree(CryptoSettings.pszCipherReturned);

        VDDestroy(pDisk);
# else
        throw setError(VBOX_E_NOT_SUPPORTED,
                       tr("Encryption is not supported because extension pack support is not built in"));
# endif
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    return hrc;
}

HRESULT Medium::checkEncryptionPassword(const com::Utf8Str &aPassword)
{
    HRESULT hrc = S_OK;

    try
    {
        ComObjPtr<Medium> pBase = i_getBase();
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        settings::StringsMap::iterator it = pBase->m->mapProperties.find("CRYPT/KeyStore");
        if (it == pBase->m->mapProperties.end())
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("The image is not configured for encryption"));

        if (aPassword.isEmpty())
            throw setError(E_INVALIDARG,
                           tr("The given password must not be empty"));

# ifdef VBOX_WITH_EXTPACK
        ExtPackManager *pExtPackManager = m->pVirtualBox->i_getExtPackManager();
        if (pExtPackManager->i_isExtPackUsable(ORACLE_PUEL_EXTPACK_NAME))
        {
            /* Load the plugin */
            Utf8Str strPlugin;
            hrc = pExtPackManager->i_getLibraryPathForExtPack(g_szVDPlugin, ORACLE_PUEL_EXTPACK_NAME, &strPlugin);
            if (SUCCEEDED(hrc))
            {
                int vrc = VDPluginLoadFromFilename(strPlugin.c_str());
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Retrieving encryption settings of the image failed because the encryption plugin could not be loaded (%s)"),
                                       i_vdError(vrc).c_str());
            }
            else
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Encryption is not supported because the extension pack '%s' is missing the encryption plugin (old extension pack installed?)"),
                               ORACLE_PUEL_EXTPACK_NAME);
        }
        else
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Encryption is not supported because the extension pack '%s' is missing"),
                           ORACLE_PUEL_EXTPACK_NAME);

        PVDISK pDisk = NULL;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &pDisk);
        ComAssertRCThrow(vrc, E_FAIL);

        MediumCryptoFilterSettings CryptoSettings;

        i_taskEncryptSettingsSetup(&CryptoSettings, NULL, it->second.c_str(), aPassword.c_str(),
                                   false /* fCreateKeyStore */);
        vrc = VDFilterAdd(pDisk, "CRYPT", VD_FILTER_FLAGS_READ, CryptoSettings.vdFilterIfaces);
        if (vrc == VERR_VD_PASSWORD_INCORRECT)
            throw setError(VBOX_E_PASSWORD_INCORRECT,
                           tr("The given password is incorrect"));
        else if (RT_FAILURE(vrc))
            throw setErrorBoth(VBOX_E_INVALID_OBJECT_STATE, vrc,
                               tr("Failed to load the encryption filter: %s"),
                               i_vdError(vrc).c_str());

        VDDestroy(pDisk);
# else
        throw setError(VBOX_E_NOT_SUPPORTED,
                       tr("Encryption is not supported because extension pack support is not built in"));
# endif
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    return hrc;
}

HRESULT Medium::openForIO(BOOL aWritable, com::Utf8Str const &aPassword, ComPtr<IMediumIO> &aMediumIO)
{
    /*
     * Input validation.
     */
    if (aWritable && i_isReadOnly())
        return setError(E_ACCESSDENIED, tr("Write access denied: read-only"));

    com::Utf8Str const strKeyId = i_getKeyId();
    if (strKeyId.isEmpty() && aPassword.isNotEmpty())
        return setError(E_INVALIDARG, tr("Password given for unencrypted medium"));
    if (strKeyId.isNotEmpty() && aPassword.isEmpty())
        return setError(E_INVALIDARG, tr("Password needed for encrypted medium"));

    /*
     * Create IO object and return it.
     */
    ComObjPtr<MediumIO> ptrIO;
    HRESULT hrc = ptrIO.createObject();
    if (SUCCEEDED(hrc))
    {
        hrc = ptrIO->initForMedium(this, m->pVirtualBox, aWritable != FALSE, strKeyId, aPassword);
        if (SUCCEEDED(hrc))
            ptrIO.queryInterfaceTo(aMediumIO.asOutParam());
    }
    return hrc;
}


////////////////////////////////////////////////////////////////////////////////
//
// Medium public internal methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Internal method to return the medium's parent medium. Must have caller + locking!
 * @return
 */
const ComObjPtr<Medium>& Medium::i_getParent() const
{
    return m->pParent;
}

/**
 * Internal method to return the medium's list of child media. Must have caller + locking!
 * @return
 */
const MediaList& Medium::i_getChildren() const
{
    return m->llChildren;
}

/**
 * Internal method to return the medium's GUID. Must have caller + locking!
 * @return
 */
const Guid& Medium::i_getId() const
{
    return m->id;
}

/**
 * Internal method to return the medium's state. Must have caller + locking!
 * @return
 */
MediumState_T Medium::i_getState() const
{
    return m->state;
}

/**
 * Internal method to return the medium's variant. Must have caller + locking!
 * @return
 */
MediumVariant_T Medium::i_getVariant() const
{
    return m->variant;
}

/**
 * Internal method which returns true if this medium represents a host drive.
 * @return
 */
bool Medium::i_isHostDrive() const
{
    return m->hostDrive;
}

/**
 * Internal method which returns true if this medium is in the process of being closed.
 * @return
 */
bool Medium::i_isClosing() const
{
    return m->fClosing;
}

/**
 * Internal method to return the medium's full location. Must have caller + locking!
 * @return
 */
const Utf8Str& Medium::i_getLocationFull() const
{
    return m->strLocationFull;
}

/**
 * Internal method to return the medium's format string. Must have caller + locking!
 * @return
 */
const Utf8Str& Medium::i_getFormat() const
{
    return m->strFormat;
}

/**
 * Internal method to return the medium's format object. Must have caller + locking!
 * @return
 */
const ComObjPtr<MediumFormat>& Medium::i_getMediumFormat() const
{
    return m->formatObj;
}

/**
 * Internal method that returns true if the medium is represented by a file on the host disk
 * (and not iSCSI or something).
 * @return
 */
bool Medium::i_isMediumFormatFile() const
{
    if (    m->formatObj
         && (m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File)
       )
        return true;
    return false;
}

/**
 * Internal method to return the medium's size. Must have caller + locking!
 * @return
 */
uint64_t Medium::i_getSize() const
{
    return m->size;
}

/**
 * Internal method to return the medium's size. Must have caller + locking!
 * @return
 */
uint64_t Medium::i_getLogicalSize() const
{
    return m->logicalSize;
}

/**
 * Returns the medium device type. Must have caller + locking!
 * @return
 */
DeviceType_T Medium::i_getDeviceType() const
{
    return m->devType;
}

/**
 * Returns the medium type. Must have caller + locking!
 * @return
 */
MediumType_T Medium::i_getType() const
{
    return m->type;
}

/**
 * Returns a short version of the location attribute.
 *
 * @note Must be called from under this object's read or write lock.
 */
Utf8Str Medium::i_getName()
{
    Utf8Str name = RTPathFilename(m->strLocationFull.c_str());
    return name;
}

/**
 * Same as i_addRegistry() except that we don't check the object state, making
 * it safe to call with initFromSettings() on the call stack.
 */
bool Medium::i_addRegistryNoCallerCheck(const Guid &id)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bool fAdd = true;

    // hard disks cannot be in more than one registry
    if (   m->devType == DeviceType_HardDisk
        && m->llRegistryIDs.size() > 0)
        fAdd = false;

    // no need to add the UUID twice
    if (fAdd)
    {
        for (GuidList::const_iterator it = m->llRegistryIDs.begin();
             it != m->llRegistryIDs.end();
             ++it)
        {
            if ((*it) == id)
            {
                fAdd = false;
                break;
            }
        }
    }

    if (fAdd)
        m->llRegistryIDs.push_back(id);

    return fAdd;
}

/**
 * This adds the given UUID to the list of media registries in which this
 * medium should be registered. The UUID can either be a machine UUID,
 * to add a machine registry, or the global registry UUID as returned by
 * VirtualBox::getGlobalRegistryId().
 *
 * Note that for hard disks, this method does nothing if the medium is
 * already in another registry to avoid having hard disks in more than
 * one registry, which causes trouble with keeping diff images in sync.
 * See getFirstRegistryMachineId() for details.
 *
 * @param id
 * @return true if the registry was added; false if the given id was already on the list.
 */
bool Medium::i_addRegistry(const Guid &id)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return false;
    return i_addRegistryNoCallerCheck(id);
}

/**
 * This adds the given UUID to the list of media registries in which this
 * medium should be registered. The UUID can either be a machine UUID,
 * to add a machine registry, or the global registry UUID as returned by
 * VirtualBox::getGlobalRegistryId(). Thisis applied to all children.
 *
 * Note that for hard disks, this method does nothing if the medium is
 * already in another registry to avoid having hard disks in more than
 * one registry, which causes trouble with keeping diff images in sync.
 * See getFirstRegistryMachineId() for details.
 *
 * @note the caller must hold the media tree lock for reading.
 *
 * @param id
 * @return true if the registry was added; false if the given id was already on the list.
 */
bool Medium::i_addRegistryAll(const Guid &id)
{
    MediaList llMediaTodo;
    llMediaTodo.push_back(this);

    bool fAdd = false;

    while (!llMediaTodo.empty())
    {
        ComObjPtr<Medium> pMedium = llMediaTodo.front();
        llMediaTodo.pop_front();

        AutoCaller mediumCaller(pMedium);
        if (FAILED(mediumCaller.hrc())) continue;

        fAdd |= pMedium->i_addRegistryNoCallerCheck(id);

        // protected by the medium tree lock held by our original caller
        MediaList::const_iterator itBegin = pMedium->i_getChildren().begin();
        MediaList::const_iterator itEnd = pMedium->i_getChildren().end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
            llMediaTodo.push_back(*it);
    }

    return fAdd;
}

/**
 * Removes the given UUID from the list of media registry UUIDs of this medium.
 *
 * @param id
 * @return true if the UUID was found or false if not.
 */
bool Medium::i_removeRegistry(const Guid &id)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return false;
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bool fRemove = false;

    /// @todo r=klaus eliminate this code, replace it by using find.
    for (GuidList::iterator it = m->llRegistryIDs.begin();
         it != m->llRegistryIDs.end();
         ++it)
    {
        if ((*it) == id)
        {
            // getting away with this as the iterator isn't used after
            m->llRegistryIDs.erase(it);
            fRemove = true;
            break;
        }
    }

    return fRemove;
}

/**
 * Removes the given UUID from the list of media registry UUIDs, for this
 * medium and all its children.
 *
 * @note the caller must hold the media tree lock for reading.
 *
 * @param id
 * @return true if the UUID was found or false if not.
 */
bool Medium::i_removeRegistryAll(const Guid &id)
{
    MediaList llMediaTodo;
    llMediaTodo.push_back(this);

    bool fRemove = false;

    while (!llMediaTodo.empty())
    {
        ComObjPtr<Medium> pMedium = llMediaTodo.front();
        llMediaTodo.pop_front();

        AutoCaller mediumCaller(pMedium);
        if (FAILED(mediumCaller.hrc())) continue;

        fRemove |= pMedium->i_removeRegistry(id);

        // protected by the medium tree lock held by our original caller
        MediaList::const_iterator itBegin = pMedium->i_getChildren().begin();
        MediaList::const_iterator itEnd = pMedium->i_getChildren().end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
            llMediaTodo.push_back(*it);
    }

    return fRemove;
}

/**
 * Returns true if id is in the list of media registries for this medium.
 *
 * Must have caller + read locking!
 *
 * @param id
 * @return
 */
bool Medium::i_isInRegistry(const Guid &id)
{
    /// @todo r=klaus eliminate this code, replace it by using find.
    for (GuidList::const_iterator it = m->llRegistryIDs.begin();
         it != m->llRegistryIDs.end();
         ++it)
    {
        if (*it == id)
            return true;
    }

    return false;
}

/**
 * Internal method to return the medium's first registry machine (i.e. the machine in whose
 * machine XML this medium is listed).
 *
 * Every attached medium must now (4.0) reside in at least one media registry, which is identified
 * by a UUID. This is either a machine UUID if the machine is from 4.0 or newer, in which case
 * machines have their own media registries, or it is the pseudo-UUID of the VirtualBox
 * object if the machine is old and still needs the global registry in VirtualBox.xml.
 *
 * By definition, hard disks may only be in one media registry, in which all its children
 * will be stored as well. Otherwise we run into problems with having keep multiple registries
 * in sync. (This is the "cloned VM" case in which VM1 may link to the disks of VM2; in this
 * case, only VM2's registry is used for the disk in question.)
 *
 * If there is no medium registry, particularly if the medium has not been attached yet, this
 * does not modify uuid and returns false.
 *
 * ISOs and RAWs, by contrast, can be in more than one repository to make things easier for
 * the user.
 *
 * Must have caller + locking!
 *
 * @param uuid Receives first registry machine UUID, if available.
 * @return true if uuid was set.
 */
bool Medium::i_getFirstRegistryMachineId(Guid &uuid) const
{
    if (m->llRegistryIDs.size())
    {
        uuid = m->llRegistryIDs.front();
        return true;
    }
    return false;
}

/**
 * Marks all the registries in which this medium is registered as modified.
 */
void Medium::i_markRegistriesModified()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return;

    // Get local copy, as keeping the lock over VirtualBox::markRegistryModified
    // causes trouble with the lock order
    GuidList llRegistryIDs;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        llRegistryIDs = m->llRegistryIDs;
    }

    autoCaller.release();

    /* Save the error information now, the implicit restore when this goes
     * out of scope will throw away spurious additional errors created below. */
    ErrorInfoKeeper eik;
    for (GuidList::const_iterator it = llRegistryIDs.begin();
         it != llRegistryIDs.end();
         ++it)
    {
        m->pVirtualBox->i_markRegistryModified(*it);
    }
}

/**
 * Adds the given machine and optionally the snapshot to the list of the objects
 * this medium is attached to.
 *
 * @param aMachineId    Machine ID.
 * @param aSnapshotId   Snapshot ID; when non-empty, adds a snapshot attachment.
 */
HRESULT Medium::i_addBackReference(const Guid &aMachineId,
                                   const Guid &aSnapshotId /*= Guid::Empty*/)
{
    AssertReturn(aMachineId.isValid(), E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("ENTER: medium: '%s' aMachineId: {%RTuuid}, aSnapshotId: {%RTuuid}\n",
        i_getLocationFull().c_str(), aMachineId.raw(), aSnapshotId.raw()));

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        case MediumState_LockedWrite:
            break;

        default:
            return i_setStateError();
    }

    if (m->numCreateDiffTasks > 0)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot attach medium '%s' {%RTuuid}: %u differencing child media are being created", "",
                           m->numCreateDiffTasks),
                        m->strLocationFull.c_str(),
                        m->id.raw(),
                        m->numCreateDiffTasks);

    BackRefList::iterator it = std::find_if(m->backRefs.begin(),
                                            m->backRefs.end(),
                                            BackRef::EqualsTo(aMachineId));
    if (it == m->backRefs.end())
    {
        BackRef ref(aMachineId, aSnapshotId);
        m->backRefs.push_back(ref);

        return S_OK;
    }
    bool fDvd = false;
    {
        AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);
        /*
         *  Check the medium is DVD and readonly. It's for the case if DVD
         *  will be able to be writable sometime in the future.
         */
        fDvd = m->type == MediumType_Readonly && m->devType == DeviceType_DVD;
    }

    // if the caller has not supplied a snapshot ID, then we're attaching
    // to a machine a medium which represents the machine's current state,
    // so set the flag

    if (aSnapshotId.isZero())
    {
        // Allow DVD having MediumType_Readonly to be attached twice.
        // (the medium already had been added to back reference)
        if (fDvd)
        {
            it->iRefCnt++;
            return S_OK;
        }

        /* sanity: no duplicate attachments */
        if (it->fInCurState)
            return setError(VBOX_E_OBJECT_IN_USE,
                            tr("Cannot attach medium '%s' {%RTuuid}: medium is already associated with the current state of machine uuid {%RTuuid}!"),
                            m->strLocationFull.c_str(),
                            m->id.raw(),
                            aMachineId.raw());
        it->fInCurState = true;

        return S_OK;
    }

    // otherwise: a snapshot medium is being attached

    /* sanity: no duplicate attachments */
    for (std::list<SnapshotRef>::iterator jt = it->llSnapshotIds.begin();
         jt != it->llSnapshotIds.end();
         ++jt)
    {
        const Guid &idOldSnapshot = jt->snapshotId;

        if (idOldSnapshot == aSnapshotId)
        {
            if (fDvd)
            {
                jt->iRefCnt++;
                return S_OK;
            }
#ifdef DEBUG
            i_dumpBackRefs();
#endif
            return setError(VBOX_E_OBJECT_IN_USE,
                            tr("Cannot attach medium '%s' {%RTuuid} from snapshot '%RTuuid': medium is already in use by this snapshot!"),
                            m->strLocationFull.c_str(),
                            m->id.raw(),
                            aSnapshotId.raw());
        }
    }

    it->llSnapshotIds.push_back(SnapshotRef(aSnapshotId));
    // Do not touch fInCurState, as the image may be attached to the current
    // state *and* a snapshot, otherwise we lose the current state association!

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 * Removes the given machine and optionally the snapshot from the list of the
 * objects this medium is attached to.
 *
 * @param aMachineId    Machine ID.
 * @param aSnapshotId   Snapshot ID; when non-empty, removes the snapshot
 *                      attachment.
 */
HRESULT Medium::i_removeBackReference(const Guid &aMachineId,
                                      const Guid &aSnapshotId /*= Guid::Empty*/)
{
    AssertReturn(aMachineId.isValid(), E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("ENTER: medium: '%s' aMachineId: {%RTuuid}, aSnapshotId: {%RTuuid}\n",
        i_getLocationFull().c_str(), aMachineId.raw(), aSnapshotId.raw()));

    BackRefList::iterator it =
        std::find_if(m->backRefs.begin(), m->backRefs.end(),
                     BackRef::EqualsTo(aMachineId));
    AssertReturn(it != m->backRefs.end(), E_FAIL);

    bool fDvd = false;
    {
        AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);
        /*
         *  Check the medium is DVD and readonly. It's for the case if DVD
         *  will be able to be writable sometime in the future.
         */
        fDvd = m->type == MediumType_Readonly && m->devType == DeviceType_DVD;
    }

    if (aSnapshotId.isZero())
    {
        /* Attached removable media which are part of a snapshot can be added to a
         * back reference more than once (as described above in i_addBackReference())
         * so we need to balance out the reference counting for DVDs here. */
        if (fDvd && it->iRefCnt > 1)
            it->iRefCnt--;
        it->iRefCnt--;
        if (it->iRefCnt > 0)
            return S_OK;

        /* Mark the medium as no longer in use by the current machine although it
         * may still be included in one or more snapshots (it->llSnapshotIds). */
        it->fInCurState = false;
    }
    else
    {
        /* remove the snapshot attachment */
        std::list<SnapshotRef>::iterator jt =
                std::find_if(it->llSnapshotIds.begin(),
                             it->llSnapshotIds.end(),
                             SnapshotRef::EqualsTo(aSnapshotId));

        AssertReturn(jt != it->llSnapshotIds.end(), E_FAIL);

        jt->iRefCnt--;
        if (jt->iRefCnt > 0)
            return S_OK;

        it->llSnapshotIds.erase(jt);

        /* The BackRef() constructor in i_addBackReference() creates back references
         * for attached hard disk mediums associated with a snapshot with the reference
         * count set to '1' and fInCurState=false before adding the snapshot to
         * it->llSnapshotIds.  When removing that snapshot reference here we need to
         * decrement the reference count since there won't be a separate
         * i_removeBackReference() call with aSnapshotId.isZero() since this hard disk
         * medium is only associated with this snapshot.  Removable media like DVDs
         * associated with a snapshot on the other hand can be attached twice, once for
         * the snapshot and once for the machine (aSnapshotId.isZero()=true) and their
         * reference counting is balanced out in the machine case above. */
        if (!fDvd && it->fInCurState == false && it->iRefCnt == 1 && it->llSnapshotIds.size() == 0)
            it->iRefCnt--;
    }

    /* The common case is that the backref will become "empty" with the reference
     * count dropping to zero (iRefCnt=0), fInCurState=false, and no more snapshot
     * references (llSnapshotIds.size()=0) and thus the back reference is removed
     * at that point.  However there is an exception and that is when a restored
     * snapshot is running and in that case there will be no i_removeBackReference()
     * call for any attached removable device referencing just the machine
     * (aSnapshotId.isZero()) so in such cases iRefCnt will be '1' and won't ever
     * drop to zero so remove such back references here as well. */
    if (   it->fInCurState == false
        && it->llSnapshotIds.size() == 0
        && (it->iRefCnt == 0 || (fDvd && it->iRefCnt == 1)))
        m->backRefs.erase(it);

    return S_OK;
}

/**
 * Internal method to return the medium's list of backrefs. Must have caller + locking!
 * @return
 */
const Guid* Medium::i_getFirstMachineBackrefId() const
{
    if (!m->backRefs.size())
        return NULL;

    return &m->backRefs.front().machineId;
}

/**
 * Internal method which returns a machine that either this medium or one of its children
 * is attached to. This is used for finding a replacement media registry when an existing
 * media registry is about to be deleted in VirtualBox::unregisterMachine().
 *
 * Must have caller + locking, *and* caller must hold the media tree lock!
 * @param aId   Id to ignore when looking for backrefs.
 * @return
 */
const Guid* Medium::i_getAnyMachineBackref(const Guid &aId) const
{
    std::list<const Medium *> llMediaTodo;
    llMediaTodo.push_back(this);

    while (!llMediaTodo.empty())
    {
        const Medium *pMedium = llMediaTodo.front();
        llMediaTodo.pop_front();

        if (pMedium->m->backRefs.size())
        {
            if (pMedium->m->backRefs.front().machineId != aId)
                return &pMedium->m->backRefs.front().machineId;
            if (pMedium->m->backRefs.size() > 1)
            {
                BackRefList::const_iterator it = pMedium->m->backRefs.begin();
                ++it;
                return &it->machineId;
            }
        }

        MediaList::const_iterator itBegin = pMedium->i_getChildren().begin();
        MediaList::const_iterator itEnd = pMedium->i_getChildren().end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
            llMediaTodo.push_back(*it);
    }

    return NULL;
}

const Guid* Medium::i_getFirstMachineBackrefSnapshotId() const
{
    if (!m->backRefs.size())
        return NULL;

    const BackRef &ref = m->backRefs.front();
    if (ref.llSnapshotIds.empty())
        return NULL;

    return &ref.llSnapshotIds.front().snapshotId;
}

size_t Medium::i_getMachineBackRefCount() const
{
    return m->backRefs.size();
}

#ifdef DEBUG
/**
 * Debugging helper that gets called after VirtualBox initialization that writes all
 * machine backreferences to the debug log.
 */
void Medium::i_dumpBackRefs()
{
    AutoCaller autoCaller(this);
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("Dumping backrefs for medium '%s':\n", m->strLocationFull.c_str()));

    for (BackRefList::iterator it2 = m->backRefs.begin();
         it2 != m->backRefs.end();
         ++it2)
    {
        const BackRef &ref = *it2;
        LogFlowThisFunc(("  Backref from machine={%RTuuid} (fInCurState=%d, iRefCnt=%d)\n", ref.machineId.raw(), ref.fInCurState, ref.iRefCnt));

        for (std::list<SnapshotRef>::const_iterator jt2 = it2->llSnapshotIds.begin();
             jt2 != it2->llSnapshotIds.end();
             ++jt2)
        {
            const Guid &id = jt2->snapshotId;
            LogFlowThisFunc(("  Backref from snapshot={%RTuuid} (iRefCnt=%d)\n", id.raw(), jt2->iRefCnt));
        }
    }
}
#endif

/**
 * Checks if the given change of \a aOldPath to \a aNewPath affects the location
 * of this media and updates it if necessary to reflect the new location.
 *
 * @param strOldPath  Old path (full).
 * @param strNewPath  New path (full).
 *
 * @note Locks this object for writing.
 */
HRESULT Medium::i_updatePath(const Utf8Str &strOldPath, const Utf8Str &strNewPath)
{
    AssertReturn(!strOldPath.isEmpty(), E_FAIL);
    AssertReturn(!strNewPath.isEmpty(), E_FAIL);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("locationFull.before='%s'\n", m->strLocationFull.c_str()));

    const char *pcszMediumPath = m->strLocationFull.c_str();

    if (RTPathStartsWith(pcszMediumPath, strOldPath.c_str()))
    {
        Utf8Str newPath(strNewPath);
        newPath.append(pcszMediumPath + strOldPath.length());
        unconst(m->strLocationFull) = newPath;

        m->pVirtualBox->i_onMediumConfigChanged(this);

        LogFlowThisFunc(("locationFull.after='%s'\n", m->strLocationFull.c_str()));
        // we changed something
        return S_OK;
    }

    // no change was necessary, signal error which the caller needs to interpret
    return VBOX_E_FILE_ERROR;
}

/**
 * Returns the base medium of the media chain this medium is part of.
 *
 * The base medium is found by walking up the parent-child relationship axis.
 * If the medium doesn't have a parent (i.e. it's a base medium), it
 * returns itself in response to this method.
 *
 * @param aLevel    Where to store the number of ancestors of this medium
 *                  (zero for the base), may be @c NULL.
 *
 * @note Locks medium tree for reading.
 */
ComObjPtr<Medium> Medium::i_getBase(uint32_t *aLevel /*= NULL*/)
{
    ComObjPtr<Medium> pBase;

    /* it is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, and in this case we don't need to continue */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);
    if (!pVirtualBox)
        return pBase;

    /* we access m->pParent */
    AutoReadLock treeLock(pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    AutoCaller autoCaller(this);
    AssertReturn(autoCaller.isOk(), pBase);

    pBase = this;
    uint32_t level = 0;

    if (m->pParent)
    {
        for (;;)
        {
            AutoCaller baseCaller(pBase);
            AssertReturn(baseCaller.isOk(), pBase);

            if (pBase->m->pParent.isNull())
                break;

            pBase = pBase->m->pParent;
            ++level;
        }
    }

    if (aLevel != NULL)
        *aLevel = level;

    return pBase;
}

/**
 * Returns the depth of this medium in the media chain.
 *
 * @note Locks medium tree for reading.
 */
uint32_t Medium::i_getDepth()
{
    /* it is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, and in this case we don't need to continue */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);
    if (!pVirtualBox)
        return 1;

    /* we access m->pParent */
    AutoReadLock treeLock(pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    uint32_t cDepth = 0;
    ComObjPtr<Medium> pMedium(this);
    while (!pMedium.isNull())
    {
        AutoCaller autoCaller(this);
        AssertReturn(autoCaller.isOk(), cDepth + 1);

        pMedium = pMedium->m->pParent;
        cDepth++;
    }

    return cDepth;
}

/**
 * Returns @c true if this medium cannot be modified because it has
 * dependents (children) or is part of the snapshot. Related to the medium
 * type and posterity, not to the current media state.
 *
 * @note Locks this object and medium tree for reading.
 */
bool Medium::i_isReadOnly()
{
    /* it is possible that some previous/concurrent uninit has already cleared
     * the pVirtualBox reference, and in this case we don't need to continue */
    ComObjPtr<VirtualBox> pVirtualBox(m->pVirtualBox);
    if (!pVirtualBox)
        return false;

    /* we access children */
    AutoReadLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), false);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (m->type)
    {
        case MediumType_Normal:
        {
            if (i_getChildren().size() != 0)
                return true;

            for (BackRefList::const_iterator it = m->backRefs.begin();
                 it != m->backRefs.end(); ++it)
                if (it->llSnapshotIds.size() != 0)
                    return true;

            if (m->variant & MediumVariant_VmdkStreamOptimized)
                return true;

            return false;
        }
        case MediumType_Immutable:
        case MediumType_MultiAttach:
            return true;
        case MediumType_Writethrough:
        case MediumType_Shareable:
        case MediumType_Readonly: /* explicit readonly media has no diffs */
            return false;
        default:
            break;
    }

    AssertFailedReturn(false);
}

/**
 * Internal method to update the medium's id. Must have caller + locking!
 */
void Medium::i_updateId(const Guid &id)
{
    unconst(m->id) = id;
}

/**
 * Saves the settings of one medium.
 *
 * @note Caller MUST take care of the medium tree lock and caller.
 *
 * @param data      Settings struct to be updated.
 * @param strHardDiskFolder Folder for which paths should be relative.
 */
void Medium::i_saveSettingsOne(settings::Medium &data, const Utf8Str &strHardDiskFolder)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data.uuid = m->id;

    // make path relative if needed
    if (    !strHardDiskFolder.isEmpty()
         && RTPathStartsWith(m->strLocationFull.c_str(), strHardDiskFolder.c_str())
       )
        data.strLocation = m->strLocationFull.substr(strHardDiskFolder.length() + 1);
    else
        data.strLocation = m->strLocationFull;
    data.strFormat = m->strFormat;

    /* optional, only for diffs, default is false */
    if (m->pParent)
        data.fAutoReset = m->autoReset;
    else
        data.fAutoReset = false;

    /* optional */
    data.strDescription = m->strDescription;

    /* optional properties */
    data.properties.clear();

    /* handle iSCSI initiator secrets transparently */
    bool fHaveInitiatorSecretEncrypted = false;
    Utf8Str strCiphertext;
    settings::StringsMap::const_iterator itPln = m->mapProperties.find("InitiatorSecret");
    if (   itPln != m->mapProperties.end()
        && !itPln->second.isEmpty())
    {
        /* Encrypt the plain secret. If that does not work (i.e. no or wrong settings key
         * specified), just use the encrypted secret (if there is any). */
        int vrc = m->pVirtualBox->i_encryptSetting(itPln->second, &strCiphertext);
        if (RT_SUCCESS(vrc))
            fHaveInitiatorSecretEncrypted = true;
    }
    for (settings::StringsMap::const_iterator it = m->mapProperties.begin();
         it != m->mapProperties.end();
         ++it)
    {
        /* only save properties that have non-default values */
        if (!it->second.isEmpty())
        {
            const Utf8Str &name = it->first;
            const Utf8Str &value = it->second;
            bool fCreateOnly = false;
            for (MediumFormat::PropertyArray::const_iterator itf = m->formatObj->i_getProperties().begin();
                 itf != m->formatObj->i_getProperties().end();
                 ++itf)
            {
                if (   itf->strName.equals(name)
                    && (itf->flags & VD_CFGKEY_CREATEONLY))
                {
                    fCreateOnly = true;
                    break;
                }
            }
            if (!fCreateOnly)
                /* do NOT store the plain InitiatorSecret */
                if (   !fHaveInitiatorSecretEncrypted
                    || !name.equals("InitiatorSecret"))
                    data.properties[name] = value;
        }
    }
    if (fHaveInitiatorSecretEncrypted)
        data.properties["InitiatorSecretEncrypted"] = strCiphertext;

    /* only for base media */
    if (m->pParent.isNull())
        data.hdType = m->type;
}

/**
 * Saves medium data by putting it into the provided data structure.
 * The settings of all children is saved, too.
 *
 * @param data      Settings struct to be updated.
 * @param strHardDiskFolder Folder for which paths should be relative.
 *
 * @note Locks this object, medium tree and children for reading.
 */
HRESULT Medium::i_saveSettings(settings::Medium &data,
                               const Utf8Str &strHardDiskFolder)
{
    /* we access m->pParent */
    AutoReadLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    MediaList llMediaTodo;
    llMediaTodo.push_back(this);
    std::list<settings::Medium *> llSettingsTodo;
    llSettingsTodo.push_back(&data);

    while (!llMediaTodo.empty())
    {
        ComObjPtr<Medium> pMedium = llMediaTodo.front();
        llMediaTodo.pop_front();
        settings::Medium *current = llSettingsTodo.front();
        llSettingsTodo.pop_front();

        AutoCaller mediumCaller(pMedium);
        if (FAILED(mediumCaller.hrc())) return mediumCaller.hrc();

        pMedium->i_saveSettingsOne(*current, strHardDiskFolder);

        /* save all children */
        MediaList::const_iterator itBegin = pMedium->i_getChildren().begin();
        MediaList::const_iterator itEnd = pMedium->i_getChildren().end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            llMediaTodo.push_back(*it);
            current->llChildren.push_back(settings::Medium::Empty);
            llSettingsTodo.push_back(&current->llChildren.back());
        }
    }

    return S_OK;
}

/**
 * Constructs a medium lock list for this medium. The lock is not taken.
 *
 * @note Caller MUST NOT hold the media tree or medium lock.
 *
 * @param fFailIfInaccessible If true, this fails with an error if a medium is inaccessible. If false,
 *          inaccessible media are silently skipped and not locked (i.e. their state remains "Inaccessible");
 *          this is necessary for a VM's removable media VM startup for which we do not want to fail.
 * @param pToLockWrite         If not NULL, associate a write lock with this medium object.
 * @param fMediumLockWriteAll  Whether to associate a write lock to all other media too.
 * @param pToBeParent          Medium which will become the parent of this medium.
 * @param mediumLockList       Where to store the resulting list.
 */
HRESULT Medium::i_createMediumLockList(bool fFailIfInaccessible,
                                       Medium *pToLockWrite,
                                       bool fMediumLockWriteAll,
                                       Medium *pToBeParent,
                                       MediumLockList &mediumLockList)
{
    /** @todo r=klaus this needs to be reworked, as the code below uses
     * i_getParent without holding the tree lock, and changing this is
     * a significant amount of effort. */
    Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    Assert(!isWriteLockOnCurrentThread());

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;

    /* paranoid sanity checking if the medium has a to-be parent medium */
    if (pToBeParent)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        ComAssertRet(i_getParent().isNull(), E_FAIL);
        ComAssertRet(i_getChildren().size() == 0, E_FAIL);
    }

    ErrorInfoKeeper eik;
    MultiResult mrc(S_OK);

    ComObjPtr<Medium> pMedium = this;
    while (!pMedium.isNull())
    {
        AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

        /* Accessibility check must be first, otherwise locking interferes
         * with getting the medium state. Lock lists are not created for
         * fun, and thus getting the medium status is no luxury. */
        MediumState_T mediumState = pMedium->i_getState();
        if (mediumState == MediumState_Inaccessible)
        {
            alock.release();
            hrc = pMedium->i_queryInfo(false /* fSetImageId */, false /* fSetParentId */, autoCaller);
            alock.acquire();
            if (FAILED(hrc)) return hrc;

            mediumState = pMedium->i_getState();
            if (mediumState == MediumState_Inaccessible)
            {
                // ignore inaccessible ISO media and silently return S_OK,
                // otherwise VM startup (esp. restore) may fail without good reason
                if (!fFailIfInaccessible)
                    return S_OK;

                // otherwise report an error
                Bstr error;
                hrc = pMedium->COMGETTER(LastAccessError)(error.asOutParam());
                if (FAILED(hrc)) return hrc;

                /* collect multiple errors */
                eik.restore();
                Assert(!error.isEmpty());
                mrc = setError(E_FAIL,
                               "%ls",
                               error.raw());
                    // error message will be something like
                    // "Could not open the medium ... VD: error VERR_FILE_NOT_FOUND opening image file ... (VERR_FILE_NOT_FOUND).
                eik.fetch();
            }
        }

        if (pMedium == pToLockWrite)
            mediumLockList.Prepend(pMedium, true);
        else
            mediumLockList.Prepend(pMedium, fMediumLockWriteAll);

        pMedium = pMedium->i_getParent();
        if (pMedium.isNull() && pToBeParent)
        {
            pMedium = pToBeParent;
            pToBeParent = NULL;
        }
    }

    return mrc;
}

/**
 * Creates a new differencing storage unit using the format of the given target
 * medium and the location. Note that @c aTarget must be NotCreated.
 *
 * The @a aMediumLockList parameter contains the associated medium lock list,
 * which must be in locked state. If @a aWait is @c true then the caller is
 * responsible for unlocking.
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a
 * new progress object will be created and assigned to @a *aProgress on
 * success, otherwise the existing progress object is used. If @a aProgress is
 * NULL, then no progress object is created/used at all.
 *
 * When @a aWait is @c false, this method will create a thread to perform the
 * create operation asynchronously and will return immediately. Otherwise, it
 * will perform the operation on the calling thread and will not return to the
 * caller until the operation is completed. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aTarget           Target medium.
 * @param aVariant          Precise medium variant to create.
 * @param aMediumLockList   List of media which should be locked.
 * @param aProgress         Where to find/store a Progress object to track
 *                          operation completion.
 * @param aWait             @c true if this method should block instead of
 *                          creating an asynchronous thread.
 * @param aNotify           Notify about media for which metadata is changed
 *                          during execution of the function.
 *
 * @note Locks this object and @a aTarget for writing.
 */
HRESULT Medium::i_createDiffStorage(ComObjPtr<Medium> &aTarget,
                                    MediumVariant_T aVariant,
                                    MediumLockList *aMediumLockList,
                                    ComObjPtr<Progress> *aProgress,
                                    bool aWait,
                                    bool aNotify)
{
    AssertReturn(!aTarget.isNull(), E_FAIL);
    AssertReturn(aMediumLockList, E_FAIL);
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoCaller targetCaller(aTarget);
    if (FAILED(targetCaller.hrc())) return targetCaller.hrc();

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        AutoMultiWriteLock2 alock(this, aTarget COMMA_LOCKVAL_SRC_POS);

        ComAssertThrow(   m->type != MediumType_Writethrough
                       && m->type != MediumType_Shareable
                       && m->type != MediumType_Readonly, E_FAIL);
        ComAssertThrow(m->state == MediumState_LockedRead, E_FAIL);

        if (aTarget->m->state != MediumState_NotCreated)
            throw aTarget->i_setStateError();

        /* Check that the medium is not attached to the current state of
         * any VM referring to it. */
        for (BackRefList::const_iterator it = m->backRefs.begin();
             it != m->backRefs.end();
             ++it)
        {
            if (it->fInCurState)
            {
                /* Note: when a VM snapshot is being taken, all normal media
                 * attached to the VM in the current state will be, as an
                 * exception, also associated with the snapshot which is about
                 * to create (see SnapshotMachine::init()) before deassociating
                 * them from the current state (which takes place only on
                 * success in Machine::fixupHardDisks()), so that the size of
                 * snapshotIds will be 1 in this case. The extra condition is
                 * used to filter out this legal situation. */
                if (it->llSnapshotIds.size() == 0)
                    throw setError(VBOX_E_INVALID_OBJECT_STATE,
                                   tr("Medium '%s' is attached to a virtual machine with UUID {%RTuuid}. No differencing media based on it may be created until it is detached"),
                                   m->strLocationFull.c_str(), it->machineId.raw());

                Assert(it->llSnapshotIds.size() == 1);
            }
        }

        if (aProgress != NULL)
        {
            /* use the existing progress object... */
            pProgress = *aProgress;

            /* ...but create a new one if it is null */
            if (pProgress.isNull())
            {
                pProgress.createObject();
                hrc = pProgress->init(m->pVirtualBox,
                                      static_cast<IMedium*>(this),
                                      BstrFmt(tr("Creating differencing medium storage unit '%s'"),
                                              aTarget->m->strLocationFull.c_str()).raw(),
                                      TRUE /* aCancelable */);
                if (FAILED(hrc))
                    throw hrc;
            }
        }

        /* setup task object to carry out the operation sync/async */
        pTask = new Medium::CreateDiffTask(this, pProgress, aTarget, aVariant,
                                           aMediumLockList,
                                           aWait /* fKeepMediumLockList */,
                                           aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
             throw hrc;

        /* register a task (it will deregister itself when done) */
        ++m->numCreateDiffTasks;
        Assert(m->numCreateDiffTasks != 0); /* overflow? */

        aTarget->m->state = MediumState_Creating;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        if (aWait)
        {
            hrc = pTask->runNow();
            delete pTask;
        }
        else
            hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc) && aProgress != NULL)
            *aProgress = pProgress;
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

/**
 * Returns a preferred format for differencing media.
 */
Utf8Str Medium::i_getPreferredDiffFormat()
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), Utf8Str::Empty);

    /* check that our own format supports diffs */
    if (!(m->formatObj->i_getCapabilities() & MediumFormatCapabilities_Differencing))
    {
        /* use the default format if not */
        Utf8Str tmp;
        m->pVirtualBox->i_getDefaultHardDiskFormat(tmp);
        return tmp;
    }

    /* m->strFormat is const, no need to lock */
    return m->strFormat;
}

/**
 * Returns a preferred variant for differencing media.
 */
MediumVariant_T Medium::i_getPreferredDiffVariant()
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), MediumVariant_Standard);

    /* check that our own format supports diffs */
    if (!(m->formatObj->i_getCapabilities() & MediumFormatCapabilities_Differencing))
        return MediumVariant_Standard;

    /* m->variant is const, no need to lock */
    ULONG mediumVariantFlags = (ULONG)m->variant;
    mediumVariantFlags &= ~(ULONG)(MediumVariant_Fixed | MediumVariant_VmdkStreamOptimized | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk);
    mediumVariantFlags |= MediumVariant_Diff;
    return (MediumVariant_T)mediumVariantFlags;
}

/**
 * Implementation for the public Medium::Close() with the exception of calling
 * VirtualBox::saveRegistries(), in case someone wants to call this for several
 * media.
 *
 * After this returns with success, uninit() has been called on the medium, and
 * the object is no longer usable ("not ready" state).
 *
 * @param autoCaller AutoCaller instance which must have been created on the caller's
 *                              stack for this medium. This gets released hereupon
 *                              which the Medium instance gets uninitialized.
 * @return
 */
HRESULT Medium::i_close(AutoCaller &autoCaller)
{
    // must temporarily drop the caller, need the tree lock first
    autoCaller.release();

    // we're accessing parent/child and backrefs, so lock the tree first, then ourselves
    AutoMultiWriteLock2 multilock(&m->pVirtualBox->i_getMediaTreeLockHandle(),
                                  this->lockHandle()
                                  COMMA_LOCKVAL_SRC_POS);

    autoCaller.add();
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* Wait for a concurrently running Medium::i_queryInfo to complete. */
    while (m->queryInfoRunning)
    {
        autoCaller.release();
        multilock.release();
        /* Must not hold the media tree lock, as Medium::i_queryInfo needs
         * this lock and thus we would run into a deadlock here. */
        Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
        /* must not hold the object lock now */
        Assert(!isWriteLockOnCurrentThread());
        {
            AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
        }
        multilock.acquire();
        autoCaller.add();
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();
    }

    LogFlowFunc(("ENTER for %s\n", i_getLocationFull().c_str()));

    bool wasCreated = true;

    switch (m->state)
    {
        case MediumState_NotCreated:
            wasCreated = false;
            break;
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return i_setStateError();
    }

    if (m->fClosing)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Medium '%s' cannot be closed because it is already in the process of being closed", ""),
                        m->strLocationFull.c_str());

    if (m->backRefs.size() != 0)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Medium '%s' cannot be closed because it is still attached to %d virtual machines", "",
                           m->backRefs.size()),
                        m->strLocationFull.c_str(), m->backRefs.size());

    // perform extra media-dependent close checks
    HRESULT hrc = i_canClose();
    if (FAILED(hrc)) return hrc;

    m->fClosing = true;

    if (wasCreated)
    {
        // remove from the list of known media before performing actual
        // uninitialization (to keep the media registry consistent on
        // failure to do so)
        hrc = i_unregisterWithVirtualBox();
        if (FAILED(hrc)) return hrc;

        multilock.release();
        // Release the AutoCaller now, as otherwise uninit() will simply hang.
        // Needs to be done before mark the registries as modified and saving
        // the registry, as otherwise there may be a deadlock with someone else
        // closing this object while we're in i_saveModifiedRegistries(), which
        // needs the media tree lock, which the other thread holds until after
        // uninit() below.
        autoCaller.release();
        i_markRegistriesModified();
        m->pVirtualBox->i_saveModifiedRegistries();
    }
    else
    {
        multilock.release();
        // release the AutoCaller, as otherwise uninit() will simply hang
        autoCaller.release();
    }

    uninit();

    LogFlowFuncLeave();

    return hrc;
}

/**
 * Deletes the medium storage unit.
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If Progress is NULL, then no
 * progress object is created/used at all.
 *
 * When @a aWait is @c false, this method will create a thread to perform the
 * delete operation asynchronously and will return immediately. Otherwise, it
 * will perform the operation on the calling thread and will not return to the
 * caller until the operation is completed. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 * @param aNotify       Notify about media for which metadata is changed
 *                      during execution of the function.
 *
 * @note Locks mVirtualBox and this object for writing. Locks medium tree for
 *       writing.
 */
HRESULT Medium::i_deleteStorage(ComObjPtr<Progress> *aProgress,
                                bool aWait, bool aNotify)
{
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        /* we're accessing the media tree, and i_canClose() needs it too */
        AutoWriteLock treelock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        AutoCaller autoCaller(this);
        AssertComRCThrowRC(autoCaller.hrc());

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        LogFlowThisFunc(("aWait=%RTbool locationFull=%s\n", aWait, i_getLocationFull().c_str() ));

        if (    !(m->formatObj->i_getCapabilities() & (  MediumFormatCapabilities_CreateDynamic
                                                       | MediumFormatCapabilities_CreateFixed
                                                       | MediumFormatCapabilities_File)))
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Medium format '%s' does not support storage deletion"),
                           m->strFormat.c_str());

        /* Wait for a concurrently running Medium::i_queryInfo to complete. */
        /** @todo r=klaus would be great if this could be moved to the async
         * part of the operation as it can take quite a while */
        while (m->queryInfoRunning)
        {
            alock.release();
            autoCaller.release();
            treelock.release();
            /* Must not hold the media tree lock or the object lock, as
             * Medium::i_queryInfo needs this lock and thus we would run
             * into a deadlock here. */
            Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            treelock.acquire();
            autoCaller.add();
            AssertComRCThrowRC(autoCaller.hrc());
            alock.acquire();
        }

        /* Note that we are fine with Inaccessible state too: a) for symmetry
         * with create calls and b) because it doesn't really harm to try, if
         * it is really inaccessible, the delete operation will fail anyway.
         * Accepting Inaccessible state is especially important because all
         * registered media are initially Inaccessible upon VBoxSVC startup
         * until COMGETTER(RefreshState) is called. Accept Deleting state
         * because some callers need to put the medium in this state early
         * to prevent races. */
        switch (m->state)
        {
            case MediumState_Created:
            case MediumState_Deleting:
            case MediumState_Inaccessible:
                break;
            default:
                throw i_setStateError();
        }

        if (m->backRefs.size() != 0)
        {
            Utf8Str strMachines;
            for (BackRefList::const_iterator it = m->backRefs.begin();
                it != m->backRefs.end();
                ++it)
            {
                const BackRef &b = *it;
                if (strMachines.length())
                    strMachines.append(", ");
                strMachines.append(b.machineId.toString().c_str());
            }
#ifdef DEBUG
            i_dumpBackRefs();
#endif
            throw setError(VBOX_E_OBJECT_IN_USE,
                           tr("Cannot delete storage: medium '%s' is still attached to the following %d virtual machine(s): %s",
                              "", m->backRefs.size()),
                           m->strLocationFull.c_str(),
                           m->backRefs.size(),
                           strMachines.c_str());
        }

        hrc = i_canClose();
        if (FAILED(hrc))
            throw hrc;

        /* go to Deleting state, so that the medium is not actually locked */
        if (m->state != MediumState_Deleting)
        {
            hrc = i_markForDeletion();
            if (FAILED(hrc))
                throw hrc;
        }

        /* Build the medium lock list. */
        MediumLockList *pMediumLockList(new MediumLockList());
        alock.release();
        autoCaller.release();
        treelock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                     this /* pToLockWrite */,
                                     false /* fMediumLockWriteAll */,
                                     NULL,
                                     *pMediumLockList);
        treelock.acquire();
        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw hrc;
        }

        alock.release();
        autoCaller.release();
        treelock.release();
        hrc = pMediumLockList->Lock();
        treelock.acquire();
        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock media when deleting '%s'"),
                           i_getLocationFull().c_str());
        }

        /* try to remove from the list of known media before performing
         * actual deletion (we favor the consistency of the media registry
         * which would have been broken if unregisterWithVirtualBox() failed
         * after we successfully deleted the storage) */
        hrc = i_unregisterWithVirtualBox();
        if (FAILED(hrc))
            throw hrc;
        // no longer need lock
        alock.release();
        autoCaller.release();
        treelock.release();
        i_markRegistriesModified();

        if (aProgress != NULL)
        {
            /* use the existing progress object... */
            pProgress = *aProgress;

            /* ...but create a new one if it is null */
            if (pProgress.isNull())
            {
                pProgress.createObject();
                hrc = pProgress->init(m->pVirtualBox,
                                      static_cast<IMedium*>(this),
                                      BstrFmt(tr("Deleting medium storage unit '%s'"), m->strLocationFull.c_str()).raw(),
                                      FALSE /* aCancelable */);
                if (FAILED(hrc))
                    throw hrc;
            }
        }

        /* setup task object to carry out the operation sync/async */
        pTask = new Medium::DeleteTask(this, pProgress, pMediumLockList, false, aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        if (aWait)
        {
            hrc = pTask->runNow();
            delete pTask;
        }
        else
            hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc) && aProgress != NULL)
            *aProgress = pProgress;
    }
    else
    {
        if (pTask)
            delete pTask;

        /* Undo deleting state if necessary. */
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        /* Make sure that any error signalled by unmarkForDeletion() is not
         * ending up in the error list (if the caller uses MultiResult). It
         * usually is spurious, as in most cases the medium hasn't been marked
         * for deletion when the error was thrown above. */
        ErrorInfoKeeper eik;
        i_unmarkForDeletion();
    }

    return hrc;
}

/**
 * Mark a medium for deletion.
 *
 * @note Caller must hold the write lock on this medium!
 */
HRESULT Medium::i_markForDeletion()
{
    ComAssertRet(isWriteLockOnCurrentThread(), E_FAIL);
    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            m->preLockState = m->state;
            m->state = MediumState_Deleting;
            return S_OK;
        default:
            return i_setStateError();
    }
}

/**
 * Removes the "mark for deletion".
 *
 * @note Caller must hold the write lock on this medium!
 */
HRESULT Medium::i_unmarkForDeletion()
{
    ComAssertRet(isWriteLockOnCurrentThread(), E_FAIL);
    switch (m->state)
    {
        case MediumState_Deleting:
            m->state = m->preLockState;
            return S_OK;
        default:
            return i_setStateError();
    }
}

/**
 * Mark a medium for deletion which is in locked state.
 *
 * @note Caller must hold the write lock on this medium!
 */
HRESULT Medium::i_markLockedForDeletion()
{
    ComAssertRet(isWriteLockOnCurrentThread(), E_FAIL);
    if (   (   m->state == MediumState_LockedRead
            || m->state == MediumState_LockedWrite)
        && m->preLockState == MediumState_Created)
    {
        m->preLockState = MediumState_Deleting;
        return S_OK;
    }
    else
        return i_setStateError();
}

/**
 * Removes the "mark for deletion" for a medium in locked state.
 *
 * @note Caller must hold the write lock on this medium!
 */
HRESULT Medium::i_unmarkLockedForDeletion()
{
    ComAssertRet(isWriteLockOnCurrentThread(), E_FAIL);
    if (   (   m->state == MediumState_LockedRead
            || m->state == MediumState_LockedWrite)
        && m->preLockState == MediumState_Deleting)
    {
        m->preLockState = MediumState_Created;
        return S_OK;
    }
    else
        return i_setStateError();
}

/**
 * Queries the preferred merge direction from this to the other medium, i.e.
 * the one which requires the least amount of I/O and therefore time and
 * disk consumption.
 *
 * @returns Status code.
 * @retval  E_FAIL in case determining the merge direction fails for some reason,
 *          for example if getting the size of the media fails. There is no
 *          error set though and the caller is free to continue to find out
 *          what was going wrong later. Leaves fMergeForward unset.
 * @retval  VBOX_E_INVALID_OBJECT_STATE if both media are not related to each other
 *          An error is set.
 * @param pOther           The other medium to merge with.
 * @param fMergeForward    Resulting preferred merge direction (out).
 */
HRESULT Medium::i_queryPreferredMergeDirection(const ComObjPtr<Medium> &pOther,
                                               bool &fMergeForward)
{
    AssertReturn(pOther != NULL, E_FAIL);
    AssertReturn(pOther != this, E_FAIL);

    HRESULT hrc = S_OK;
    bool fThisParent = false; /**<< Flag whether this medium is the parent of pOther. */

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        AutoCaller autoCaller(this);
        AssertComRCThrowRC(autoCaller.hrc());

        AutoCaller otherCaller(pOther);
        AssertComRCThrowRC(otherCaller.hrc());

        /* more sanity checking and figuring out the current merge direction */
        ComObjPtr<Medium> pMedium = i_getParent();
        while (!pMedium.isNull() && pMedium != pOther)
            pMedium = pMedium->i_getParent();
        if (pMedium == pOther)
            fThisParent = false;
        else
        {
            pMedium = pOther->i_getParent();
            while (!pMedium.isNull() && pMedium != this)
                pMedium = pMedium->i_getParent();
            if (pMedium == this)
                fThisParent = true;
            else
            {
                Utf8Str tgtLoc;
                {
                    AutoReadLock alock(pOther COMMA_LOCKVAL_SRC_POS);
                    tgtLoc = pOther->i_getLocationFull();
                }

                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Media '%s' and '%s' are unrelated"),
                               m->strLocationFull.c_str(), tgtLoc.c_str());
            }
        }

        /*
         * Figure out the preferred merge direction. The current way is to
         * get the current sizes of file based images and select the merge
         * direction depending on the size.
         *
         * Can't use the VD API to get current size here as the media might
         * be write locked by a running VM. Resort to RTFileQuerySize().
         */
        int vrc = VINF_SUCCESS;
        uint64_t cbMediumThis = 0;
        uint64_t cbMediumOther = 0;

        if (i_isMediumFormatFile() && pOther->i_isMediumFormatFile())
        {
            vrc = RTFileQuerySizeByPath(this->i_getLocationFull().c_str(), &cbMediumThis);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTFileQuerySizeByPath(pOther->i_getLocationFull().c_str(),
                                      &cbMediumOther);
            }

            if (RT_FAILURE(vrc))
                hrc = E_FAIL;
            else
            {
                /*
                 * Check which merge direction might be more optimal.
                 * This method is not bullet proof of course as there might
                 * be overlapping blocks in the images so the file size is
                 * not the best indicator but it is good enough for our purpose
                 * and everything else is too complicated, especially when the
                 * media are used by a running VM.
                 */

                uint32_t mediumVariants =  MediumVariant_Fixed | MediumVariant_VmdkStreamOptimized;
                uint32_t mediumCaps = MediumFormatCapabilities_CreateDynamic | MediumFormatCapabilities_File;

                bool fDynamicOther  = pOther->i_getMediumFormat()->i_getCapabilities() & mediumCaps
                                   && pOther->i_getVariant() & ~mediumVariants;
                bool fDynamicThis   = i_getMediumFormat()->i_getCapabilities() & mediumCaps
                                   && i_getVariant() & ~mediumVariants;
                bool fMergeIntoThis = (fDynamicThis && !fDynamicOther)
                                   || (fDynamicThis == fDynamicOther && cbMediumThis > cbMediumOther);
                fMergeForward = fMergeIntoThis != fThisParent;
            }
        }
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    return hrc;
}

/**
 * Prepares this (source) medium, target medium and all intermediate media
 * for the merge operation.
 *
 * This method is to be called prior to calling the #mergeTo() to perform
 * necessary consistency checks and place involved media to appropriate
 * states. If #mergeTo() is not called or fails, the state modifications
 * performed by this method must be undone by #i_cancelMergeTo().
 *
 * See #mergeTo() for more information about merging.
 *
 * @param pTarget       Target medium.
 * @param aMachineId    Allowed machine attachment. NULL means do not check.
 * @param aSnapshotId   Allowed snapshot attachment. NULL or empty UUID means
 *                      do not check.
 * @param fLockMedia    Flag whether to lock the medium lock list or not.
 *                      If set to false and the medium lock list locking fails
 *                      later you must call #i_cancelMergeTo().
 * @param fMergeForward Resulting merge direction (out).
 * @param pParentForTarget New parent for target medium after merge (out).
 * @param aChildrenToReparent Medium lock list containing all children of the
 *                      source which will have to be reparented to the target
 *                      after merge (out).
 * @param aMediumLockList Medium locking information (out).
 *
 * @note Locks medium tree for reading. Locks this object, aTarget and all
 *       intermediate media for writing.
 */
HRESULT Medium::i_prepareMergeTo(const ComObjPtr<Medium> &pTarget,
                                 const Guid *aMachineId,
                                 const Guid *aSnapshotId,
                                 bool fLockMedia,
                                 bool &fMergeForward,
                                 ComObjPtr<Medium> &pParentForTarget,
                                 MediumLockList * &aChildrenToReparent,
                                 MediumLockList * &aMediumLockList)
{
    AssertReturn(pTarget != NULL, E_FAIL);
    AssertReturn(pTarget != this, E_FAIL);

    HRESULT hrc = S_OK;
    fMergeForward = false;
    pParentForTarget.setNull();
    Assert(aChildrenToReparent == NULL);
    aChildrenToReparent = NULL;
    Assert(aMediumLockList == NULL);
    aMediumLockList = NULL;

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        AutoCaller autoCaller(this);
        AssertComRCThrowRC(autoCaller.hrc());

        AutoCaller targetCaller(pTarget);
        AssertComRCThrowRC(targetCaller.hrc());

        /* more sanity checking and figuring out the merge direction */
        ComObjPtr<Medium> pMedium = i_getParent();
        while (!pMedium.isNull() && pMedium != pTarget)
            pMedium = pMedium->i_getParent();
        if (pMedium == pTarget)
            fMergeForward = false;
        else
        {
            pMedium = pTarget->i_getParent();
            while (!pMedium.isNull() && pMedium != this)
                pMedium = pMedium->i_getParent();
            if (pMedium == this)
                fMergeForward = true;
            else
            {
                Utf8Str tgtLoc;
                {
                    AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
                    tgtLoc = pTarget->i_getLocationFull();
                }

                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Media '%s' and '%s' are unrelated"),
                               m->strLocationFull.c_str(), tgtLoc.c_str());
            }
        }

        /* Build the lock list. */
        aMediumLockList = new MediumLockList();
        targetCaller.release();
        autoCaller.release();
        treeLock.release();
        if (fMergeForward)
            hrc = pTarget->i_createMediumLockList(true /* fFailIfInaccessible */,
                                                  pTarget /* pToLockWrite */,
                                                  false /* fMediumLockWriteAll */,
                                                  NULL,
                                                  *aMediumLockList);
        else
            hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                         pTarget /* pToLockWrite */,
                                         false /* fMediumLockWriteAll */,
                                         NULL,
                                         *aMediumLockList);
        treeLock.acquire();
        autoCaller.add();
        AssertComRCThrowRC(autoCaller.hrc());
        targetCaller.add();
        AssertComRCThrowRC(targetCaller.hrc());
        if (FAILED(hrc))
            throw hrc;

        /* Sanity checking, must be after lock list creation as it depends on
         * valid medium states. The medium objects must be accessible. Only
         * do this if immediate locking is requested, otherwise it fails when
         * we construct a medium lock list for an already running VM. Snapshot
         * deletion uses this to simplify its life. */
        if (fLockMedia)
        {
            {
                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                if (m->state != MediumState_Created)
                    throw i_setStateError();
            }
            {
                AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
                if (pTarget->m->state != MediumState_Created)
                    throw pTarget->i_setStateError();
            }
        }

        /* check medium attachment and other sanity conditions */
        if (fMergeForward)
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (i_getChildren().size() > 1)
            {
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' involved in the merge operation has more than one child medium (%d)"),
                               m->strLocationFull.c_str(), i_getChildren().size());
            }
            /* One backreference is only allowed if the machine ID is not empty
             * and it matches the machine the medium is attached to (including
             * the snapshot ID if not empty). */
            if (   m->backRefs.size() != 0
                && (   !aMachineId
                    || m->backRefs.size() != 1
                    || aMachineId->isZero()
                    || *i_getFirstMachineBackrefId() != *aMachineId
                    || (   (!aSnapshotId || !aSnapshotId->isZero())
                        && *i_getFirstMachineBackrefSnapshotId() != *aSnapshotId)))
                throw setError(VBOX_E_OBJECT_IN_USE,
                               tr("Medium '%s' is attached to %d virtual machines", "", m->backRefs.size()),
                               m->strLocationFull.c_str(), m->backRefs.size());
            if (m->type == MediumType_Immutable)
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is immutable"),
                               m->strLocationFull.c_str());
            if (m->type == MediumType_MultiAttach)
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is multi-attach"),
                               m->strLocationFull.c_str());
        }
        else
        {
            AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
            if (pTarget->i_getChildren().size() > 1)
            {
                throw setError(VBOX_E_OBJECT_IN_USE,
                               tr("Medium '%s' involved in the merge operation has more than one child medium (%d)"),
                               pTarget->m->strLocationFull.c_str(),
                               pTarget->i_getChildren().size());
            }
            if (pTarget->m->type == MediumType_Immutable)
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is immutable"),
                               pTarget->m->strLocationFull.c_str());
            if (pTarget->m->type == MediumType_MultiAttach)
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is multi-attach"),
                               pTarget->m->strLocationFull.c_str());
        }
        ComObjPtr<Medium> pLast(fMergeForward ? (Medium *)pTarget : this);
        ComObjPtr<Medium> pLastIntermediate = pLast->i_getParent();
        for (pLast = pLastIntermediate;
             !pLast.isNull() && pLast != pTarget && pLast != this;
             pLast = pLast->i_getParent())
        {
            AutoReadLock alock(pLast COMMA_LOCKVAL_SRC_POS);
            if (pLast->i_getChildren().size() > 1)
            {
                throw setError(VBOX_E_OBJECT_IN_USE,
                               tr("Medium '%s' involved in the merge operation has more than one child medium (%d)"),
                               pLast->m->strLocationFull.c_str(),
                               pLast->i_getChildren().size());
            }
            if (pLast->m->backRefs.size() != 0)
                throw setError(VBOX_E_OBJECT_IN_USE,
                               tr("Medium '%s' is attached to %d virtual machines", "", pLast->m->backRefs.size()),
                               pLast->m->strLocationFull.c_str(),
                               pLast->m->backRefs.size());

        }

        /* Update medium states appropriately */
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

            if (m->state == MediumState_Created)
            {
                hrc = i_markForDeletion();
                if (FAILED(hrc))
                    throw hrc;
            }
            else
            {
                if (fLockMedia)
                    throw i_setStateError();
                else if (   m->state == MediumState_LockedWrite
                         || m->state == MediumState_LockedRead)
                {
                    /* Either mark it for deletion in locked state or allow
                     * others to have done so. */
                    if (m->preLockState == MediumState_Created)
                        i_markLockedForDeletion();
                    else if (m->preLockState != MediumState_Deleting)
                        throw i_setStateError();
                }
                else
                    throw i_setStateError();
            }
        }

        if (fMergeForward)
        {
            /* we will need parent to reparent target */
            pParentForTarget = i_getParent();
        }
        else
        {
            /* we will need to reparent children of the source */
            aChildrenToReparent = new MediumLockList();
            for (MediaList::const_iterator it = i_getChildren().begin();
                 it != i_getChildren().end();
                 ++it)
            {
                pMedium = *it;
                aChildrenToReparent->Append(pMedium, true /* fLockWrite */);
            }
            if (fLockMedia && aChildrenToReparent)
            {
                targetCaller.release();
                autoCaller.release();
                treeLock.release();
                hrc = aChildrenToReparent->Lock();
                treeLock.acquire();
                autoCaller.add();
                AssertComRCThrowRC(autoCaller.hrc());
                targetCaller.add();
                AssertComRCThrowRC(targetCaller.hrc());
                if (FAILED(hrc))
                    throw hrc;
            }
        }
        for (pLast = pLastIntermediate;
             !pLast.isNull() && pLast != pTarget && pLast != this;
             pLast = pLast->i_getParent())
        {
            AutoWriteLock alock(pLast COMMA_LOCKVAL_SRC_POS);
            if (pLast->m->state == MediumState_Created)
            {
                hrc = pLast->i_markForDeletion();
                if (FAILED(hrc))
                    throw hrc;
            }
            else
                throw pLast->i_setStateError();
        }

        /* Tweak the lock list in the backward merge case, as the target
         * isn't marked to be locked for writing yet. */
        if (!fMergeForward)
        {
            MediumLockList::Base::iterator lockListBegin =
                aMediumLockList->GetBegin();
            MediumLockList::Base::iterator lockListEnd =
                aMediumLockList->GetEnd();
            ++lockListEnd;
            for (MediumLockList::Base::iterator it = lockListBegin;
                 it != lockListEnd;
                 ++it)
            {
                MediumLock &mediumLock = *it;
                if (mediumLock.GetMedium() == pTarget)
                {
                    HRESULT hrc2 = mediumLock.UpdateLock(true);
                    AssertComRC(hrc2);
                    break;
                }
            }
        }

        if (fLockMedia)
        {
            targetCaller.release();
            autoCaller.release();
            treeLock.release();
            hrc = aMediumLockList->Lock();
            treeLock.acquire();
            autoCaller.add();
            AssertComRCThrowRC(autoCaller.hrc());
            targetCaller.add();
            AssertComRCThrowRC(targetCaller.hrc());
            if (FAILED(hrc))
            {
                AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
                throw setError(hrc,
                               tr("Failed to lock media when merging to '%s'"),
                               pTarget->i_getLocationFull().c_str());
            }
        }
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (FAILED(hrc))
    {
        if (aMediumLockList)
        {
            delete aMediumLockList;
            aMediumLockList = NULL;
        }
        if (aChildrenToReparent)
        {
            delete aChildrenToReparent;
            aChildrenToReparent = NULL;
        }
    }

    return hrc;
}

/**
 * Merges this medium to the specified medium which must be either its
 * direct ancestor or descendant.
 *
 * Given this medium is SOURCE and the specified medium is TARGET, we will
 * get two variants of the merge operation:
 *
 *                forward merge
 *                ------------------------->
 *  [Extra] <- SOURCE <- Intermediate <- TARGET
 *  Any        Del       Del             LockWr
 *
 *
 *                            backward merge
 *                <-------------------------
 *             TARGET <- Intermediate <- SOURCE <- [Extra]
 *             LockWr    Del             Del       LockWr
 *
 * Each diagram shows the involved media on the media chain where
 * SOURCE and TARGET belong. Under each medium there is a state value which
 * the medium must have at a time of the mergeTo() call.
 *
 * The media in the square braces may be absent (e.g. when the forward
 * operation takes place and SOURCE is the base medium, or when the backward
 * merge operation takes place and TARGET is the last child in the chain) but if
 * they present they are involved too as shown.
 *
 * Neither the source medium nor intermediate media may be attached to
 * any VM directly or in the snapshot, otherwise this method will assert.
 *
 * The #i_prepareMergeTo() method must be called prior to this method to place
 * all involved to necessary states and perform other consistency checks.
 *
 * If @a aWait is @c true then this method will perform the operation on the
 * calling thread and will not return to the caller until the operation is
 * completed. When this method succeeds, all intermediate medium objects in
 * the chain will be uninitialized, the state of the target medium (and all
 * involved extra media) will be restored. @a aMediumLockList will not be
 * deleted, whether the operation is successful or not. The caller has to do
 * this if appropriate. Note that this (source) medium is not uninitialized
 * because of possible AutoCaller instances held by the caller of this method
 * on the current thread. It's therefore the responsibility of the caller to
 * call Medium::uninit() after releasing all callers.
 *
 * If @a aWait is @c false then this method will create a thread to perform the
 * operation asynchronously and will return immediately. If the operation
 * succeeds, the thread will uninitialize the source medium object and all
 * intermediate medium objects in the chain, reset the state of the target
 * medium (and all involved extra media) and delete @a aMediumLockList.
 * If the operation fails, the thread will only reset the states of all
 * involved media and delete @a aMediumLockList.
 *
 * When this method fails (regardless of the @a aWait mode), it is a caller's
 * responsibility to undo state changes and delete @a aMediumLockList using
 * #i_cancelMergeTo().
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If Progress is NULL, then no
 * progress object is created/used at all. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param pTarget       Target medium.
 * @param fMergeForward Merge direction.
 * @param pParentForTarget New parent for target medium after merge.
 * @param aChildrenToReparent List of children of the source which will have
 *                      to be reparented to the target after merge.
 * @param aMediumLockList Medium locking information.
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 * @param aNotify       Notify about media for which metadata is changed
 *                      during execution of the function.
 *
 * @note Locks the tree lock for writing. Locks the media from the chain
 *       for writing.
 */
HRESULT Medium::i_mergeTo(const ComObjPtr<Medium> &pTarget,
                          bool fMergeForward,
                          const ComObjPtr<Medium> &pParentForTarget,
                          MediumLockList *aChildrenToReparent,
                          MediumLockList *aMediumLockList,
                          ComObjPtr<Progress> *aProgress,
                          bool aWait, bool aNotify)
{
    AssertReturn(pTarget != NULL, E_FAIL);
    AssertReturn(pTarget != this, E_FAIL);
    AssertReturn(aMediumLockList != NULL, E_FAIL);
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller targetCaller(pTarget);
    AssertComRCReturnRC(targetCaller.hrc());

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        if (aProgress != NULL)
        {
            /* use the existing progress object... */
            pProgress = *aProgress;

            /* ...but create a new one if it is null */
            if (pProgress.isNull())
            {
                Utf8Str tgtName;
                {
                    AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
                    tgtName = pTarget->i_getName();
                }

                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

                pProgress.createObject();
                hrc = pProgress->init(m->pVirtualBox,
                                      static_cast<IMedium*>(this),
                                      BstrFmt(tr("Merging medium '%s' to '%s'"),
                                              i_getName().c_str(),
                                              tgtName.c_str()).raw(),
                                      TRUE, /* aCancelable */
                                      2, /* Number of opearations */
                                      BstrFmt(tr("Resizing medium '%s' before merge"),
                                              tgtName.c_str()).raw()
                                      );
                if (FAILED(hrc))
                    throw hrc;
            }
        }

        /* setup task object to carry out the operation sync/async */
        pTask = new Medium::MergeTask(this, pTarget, fMergeForward,
                                      pParentForTarget, aChildrenToReparent,
                                      pProgress, aMediumLockList,
                                      aWait /* fKeepMediumLockList */,
                                      aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        if (aWait)
        {
            hrc = pTask->runNow();
            delete pTask;
        }
        else
            hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc) && aProgress != NULL)
            *aProgress = pProgress;
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

/**
 * Undoes what #i_prepareMergeTo() did. Must be called if #mergeTo() is not
 * called or fails. Frees memory occupied by @a aMediumLockList and unlocks
 * the medium objects in @a aChildrenToReparent.
 *
 * @param aChildrenToReparent List of children of the source which will have
 *                      to be reparented to the target after merge.
 * @param aMediumLockList Medium locking information.
 *
 * @note Locks the tree lock for writing. Locks the media from the chain
 *       for writing.
 */
void Medium::i_cancelMergeTo(MediumLockList *aChildrenToReparent,
                             MediumLockList *aMediumLockList)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertReturnVoid(aMediumLockList != NULL);

    /* Revert media marked for deletion to previous state. */
    MediumLockList::Base::const_iterator mediumListBegin =
        aMediumLockList->GetBegin();
    MediumLockList::Base::const_iterator mediumListEnd =
        aMediumLockList->GetEnd();
    for (MediumLockList::Base::const_iterator it = mediumListBegin;
         it != mediumListEnd;
         ++it)
    {
        const MediumLock &mediumLock = *it;
        const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
        AutoWriteLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

        if (pMedium->m->state == MediumState_Deleting)
        {
            HRESULT hrc = pMedium->i_unmarkForDeletion();
            AssertComRC(hrc);
        }
        else if (   (   pMedium->m->state == MediumState_LockedWrite
                     || pMedium->m->state == MediumState_LockedRead)
                 && pMedium->m->preLockState == MediumState_Deleting)
        {
            HRESULT hrc = pMedium->i_unmarkLockedForDeletion();
            AssertComRC(hrc);
        }
    }

    /* the destructor will do the work */
    delete aMediumLockList;

    /* unlock the children which had to be reparented, the destructor will do
     * the work */
    if (aChildrenToReparent)
        delete aChildrenToReparent;
}

/**
 * Resizes the media.
 *
 * If @a aWait is @c true then this method will perform the operation on the
 * calling thread and will not return to the caller until the operation is
 * completed. When this method succeeds, the state of the target medium (and all
 * involved extra media) will be restored. @a aMediumLockList will not be
 * deleted, whether the operation is successful or not. The caller has to do
 * this if appropriate.
 *
 * If @a aWait is @c false then this method will create a thread to perform the
 * operation asynchronously and will return immediately. The thread will reset
 * the state of the target medium (and all involved extra media) and delete
 * @a aMediumLockList.
 *
 * When this method fails (regardless of the @a aWait mode), it is a caller's
 * responsibility to undo state changes and delete @a aMediumLockList.
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If Progress is NULL, then no
 * progress object is created/used at all. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aLogicalSize  New nominal capacity of the medium in bytes.
 * @param aMediumLockList Medium locking information.
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 * @param aNotify       Notify about media for which metadata is changed
 *                      during execution of the function.
 *
 * @note Locks the media from the chain for writing.
 */

HRESULT Medium::i_resize(uint64_t aLogicalSize,
                         MediumLockList *aMediumLockList,
                         ComObjPtr<Progress> *aProgress,
                         bool aWait,
                         bool aNotify)
{
    AssertReturn(aMediumLockList != NULL, E_FAIL);
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        if (aProgress != NULL)
        {
            /* use the existing progress object... */
            pProgress = *aProgress;

            /* ...but create a new one if it is null */
            if (pProgress.isNull())
            {
                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

                pProgress.createObject();
                hrc = pProgress->init(m->pVirtualBox,
                                      static_cast <IMedium *>(this),
                                      BstrFmt(tr("Resizing medium '%s'"), m->strLocationFull.c_str()).raw(),
                                      TRUE /* aCancelable */);
                if (FAILED(hrc))
                    throw hrc;
            }
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::ResizeTask(this,
                                       aLogicalSize,
                                       pProgress,
                                       aMediumLockList,
                                       aWait /* fKeepMediumLockList */,
                                       aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        if (aWait)
        {
            hrc = pTask->runNow();
            delete pTask;
        }
        else
            hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc) && aProgress != NULL)
            *aProgress = pProgress;
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

/**
 * Fix the parent UUID of all children to point to this medium as their
 * parent.
 */
HRESULT Medium::i_fixParentUuidOfChildren(MediumLockList *pChildrenToReparent)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. Likewise the code using this method seems
     * problematic. */
    Assert(!isWriteLockOnCurrentThread());
    Assert(!m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    MediumLockList mediumLockList;
    HRESULT hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                         NULL /* pToLockWrite */,
                                         false /* fMediumLockWriteAll */,
                                         this,
                                         mediumLockList);
    AssertComRCReturnRC(hrc);

    try
    {
        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            MediumLockList::Base::iterator lockListBegin =
                mediumLockList.GetBegin();
            MediumLockList::Base::iterator lockListEnd =
                mediumLockList.GetEnd();
            for (MediumLockList::Base::iterator it = lockListBegin;
                 it != lockListEnd;
                 ++it)
            {
                MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                // open the medium
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             VD_OPEN_FLAGS_READONLY | m->uOpenFlagsDef,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw vrc;
            }

            MediumLockList::Base::iterator childrenBegin = pChildrenToReparent->GetBegin();
            MediumLockList::Base::iterator childrenEnd = pChildrenToReparent->GetEnd();
            for (MediumLockList::Base::iterator it = childrenBegin;
                 it != childrenEnd;
                 ++it)
            {
                Medium *pMedium = it->GetMedium();
                /* VD_OPEN_FLAGS_INFO since UUID is wrong yet */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             VD_OPEN_FLAGS_INFO | m->uOpenFlagsDef,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw vrc;

                vrc = VDSetParentUuid(hdd, VD_LAST_IMAGE, m->id.raw());
                if (RT_FAILURE(vrc))
                    throw vrc;

                vrc = VDClose(hdd, false /* fDelete */);
                if (RT_FAILURE(vrc))
                    throw vrc;
            }
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }
        catch (int vrcXcpt)
        {
            hrc = setErrorBoth(E_FAIL, vrcXcpt,
                               tr("Could not update medium UUID references to parent '%s' (%s)"),
                               m->strLocationFull.c_str(),
                               i_vdError(vrcXcpt).c_str());
        }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    return hrc;
}

/**
 *
 * @note    Similar code exists in i_taskExportHandler.
 */
HRESULT Medium::i_addRawToFss(const char *aFilename, SecretKeyStore *pKeyStore, RTVFSFSSTREAM hVfsFssDst,
                              const ComObjPtr<Progress> &aProgress, bool fSparse)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Get a readonly hdd for this medium.
         */
        MediumCryptoFilterSettings      CryptoSettingsRead;
        MediumLockList                  SourceMediumLockList;
        PVDISK                          pHdd;
        hrc = i_openForIO(false /*fWritable*/, pKeyStore, &pHdd, &SourceMediumLockList, &CryptoSettingsRead);
        if (SUCCEEDED(hrc))
        {
            /*
             * Create a VFS file interface to the HDD and attach a progress wrapper
             * that monitors the progress reading of the raw image.  The image will
             * be read twice if hVfsFssDst does sparse processing.
             */
            RTVFSFILE hVfsFileDisk = NIL_RTVFSFILE;
            int vrc = VDCreateVfsFileFromDisk(pHdd, 0 /*fFlags*/, &hVfsFileDisk);
            if (RT_SUCCESS(vrc))
            {
                RTVFSFILE hVfsFileProgress = NIL_RTVFSFILE;
                vrc = RTVfsCreateProgressForFile(hVfsFileDisk, aProgress->i_iprtProgressCallback, &*aProgress,
                                                 RTVFSPROGRESS_F_CANCELABLE | RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ,
                                                 VDGetSize(pHdd, VD_LAST_IMAGE) * (fSparse ? 2 : 1) /*cbExpectedRead*/,
                                                 0 /*cbExpectedWritten*/, &hVfsFileProgress);
                RTVfsFileRelease(hVfsFileDisk);
                if (RT_SUCCESS(vrc))
                {
                    RTVFSOBJ hVfsObj = RTVfsObjFromFile(hVfsFileProgress);
                    RTVfsFileRelease(hVfsFileProgress);

                    vrc = RTVfsFsStrmAdd(hVfsFssDst, aFilename, hVfsObj, 0 /*fFlags*/);
                    RTVfsObjRelease(hVfsObj);
                    if (RT_FAILURE(vrc))
                        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to add '%s' to output (%Rrc)"), aFilename, vrc);
                }
                else
                    hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("RTVfsCreateProgressForFile failed when processing '%s' (%Rrc)"), aFilename, vrc);
            }
            else
                hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("VDCreateVfsFileFromDisk failed for '%s' (%Rrc)"), aFilename, vrc);
            VDDestroy(pHdd);
        }
    }
    return hrc;
}

/**
 * Used by IAppliance to export disk images.
 *
 * @param aFilename         Filename to create (UTF8).
 * @param aFormat           Medium format for creating @a aFilename.
 * @param aVariant          Which exact image format variant to use for the
 *                          destination image.
 * @param pKeyStore         The optional key store for decrypting the data for
 *                          encrypted media during the export.
 * @param hVfsIosDst        The destination I/O stream object.
 * @param aProgress         Progress object to use.
 * @return
 *
 * @note The source format is defined by the Medium instance.
 */
HRESULT Medium::i_exportFile(const char *aFilename,
                             const ComObjPtr<MediumFormat> &aFormat,
                             MediumVariant_T aVariant,
                             SecretKeyStore *pKeyStore,
                             RTVFSIOSTREAM hVfsIosDst,
                             const ComObjPtr<Progress> &aProgress)
{
    AssertPtrReturn(aFilename, E_INVALIDARG);
    AssertReturn(aFormat.isNotNull(), E_INVALIDARG);
    AssertReturn(aProgress.isNotNull(), E_INVALIDARG);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Setup VD interfaces.
         */
        PVDINTERFACE   pVDImageIfaces = m->vdImageIfaces;
        PVDINTERFACEIO pVfsIoIf;
        int vrc = VDIfCreateFromVfsStream(hVfsIosDst, RTFILE_O_WRITE, &pVfsIoIf);
        if (RT_SUCCESS(vrc))
        {
            vrc = VDInterfaceAdd(&pVfsIoIf->Core, "Medium::ExportTaskVfsIos", VDINTERFACETYPE_IO,
                                 pVfsIoIf, sizeof(VDINTERFACEIO), &pVDImageIfaces);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Get a readonly hdd for this medium (source).
                 */
                MediumCryptoFilterSettings      CryptoSettingsRead;
                MediumLockList                  SourceMediumLockList;
                PVDISK                          pSrcHdd;
                hrc = i_openForIO(false /*fWritable*/, pKeyStore, &pSrcHdd, &SourceMediumLockList, &CryptoSettingsRead);
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Create the target medium.
                     */
                    Utf8Str strDstFormat(aFormat->i_getId());

                    /* ensure the target directory exists */
                    uint64_t fDstCapabilities = aFormat->i_getCapabilities();
                    if (fDstCapabilities & MediumFormatCapabilities_File)
                    {
                        Utf8Str strDstLocation(aFilename);
                        hrc = VirtualBox::i_ensureFilePathExists(strDstLocation.c_str(),
                                                                 !(aVariant & MediumVariant_NoCreateDir) /* fCreate */);
                    }
                    if (SUCCEEDED(hrc))
                    {
                        PVDISK pDstHdd;
                        vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &pDstHdd);
                        if (RT_SUCCESS(vrc))
                        {
                            /*
                             * Create an interface for getting progress callbacks.
                             */
                            VDINTERFACEPROGRESS ProgressIf = VDINTERFACEPROGRESS_INITALIZER(aProgress->i_vdProgressCallback);
                            PVDINTERFACE        pProgress = NULL;
                            vrc = VDInterfaceAdd(&ProgressIf.Core, "export-progress", VDINTERFACETYPE_PROGRESS,
                                                 &*aProgress, sizeof(ProgressIf), &pProgress);
                            AssertRC(vrc);

                            /*
                             * Do the exporting.
                             */
                            vrc = VDCopy(pSrcHdd,
                                         VD_LAST_IMAGE,
                                         pDstHdd,
                                         strDstFormat.c_str(),
                                         aFilename,
                                         false /* fMoveByRename */,
                                         0 /* cbSize */,
                                         aVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                                         NULL /* pDstUuid */,
                                         VD_OPEN_FLAGS_NORMAL | VD_OPEN_FLAGS_SEQUENTIAL,
                                         pProgress,
                                         pVDImageIfaces,
                                         NULL);
                            if (RT_SUCCESS(vrc))
                                hrc = S_OK;
                            else
                                hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Could not create the exported medium '%s'%s"),
                                                   aFilename, i_vdError(vrc).c_str());
                            VDDestroy(pDstHdd);
                        }
                        else
                            hrc = setErrorVrc(vrc);
                    }
                    VDDestroy(pSrcHdd);
                }
            }
            else
                hrc = setErrorVrc(vrc, "VDInterfaceAdd -> %Rrc", vrc);
            VDIfDestroyFromVfsStream(pVfsIoIf);
        }
        else
            hrc = setErrorVrc(vrc, "VDIfCreateFromVfsStream -> %Rrc", vrc);
    }
    return hrc;
}

/**
 * Used by IAppliance to import disk images.
 *
 * @param aFilename             Filename to read (UTF8).
 * @param aFormat               Medium format for reading @a aFilename.
 * @param aVariant              Which exact image format variant to use
 *                              for the destination image.
 * @param aVfsIosSrc            Handle to the source I/O stream.
 * @param aParent               Parent medium. May be NULL.
 * @param aProgress             Progress object to use.
 * @param aNotify               Notify about media for which metadata is changed
 *                              during execution of the function.
 * @return
 * @note The destination format is defined by the Medium instance.
 *
 * @todo The only consumer of this method (Appliance::i_importOneDiskImage) is
 *       already on a worker thread, so perhaps consider bypassing the thread
 *       here and run in the task synchronously?  VBoxSVC has enough threads as
 *       it is...
 */
HRESULT Medium::i_importFile(const char *aFilename,
                             const ComObjPtr<MediumFormat> &aFormat,
                             MediumVariant_T aVariant,
                             RTVFSIOSTREAM aVfsIosSrc,
                             const ComObjPtr<Medium> &aParent,
                             const ComObjPtr<Progress> &aProgress,
                             bool aNotify)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    AssertPtrReturn(aFilename, E_INVALIDARG);
    AssertReturn(!aFormat.isNull(), E_INVALIDARG);
    AssertReturn(!aProgress.isNull(), E_INVALIDARG);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;
    Medium::Task *pTask = NULL;

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        uint32_t    cHandles    = 2;
        LockHandle* pHandles[3] = { &m->pVirtualBox->i_getMediaTreeLockHandle(),
                                    this->lockHandle() };
        /* Only add parent to the lock if it is not null */
        if (!aParent.isNull())
            pHandles[cHandles++] = aParent->lockHandle();
        AutoWriteLock alock(cHandles,
                            pHandles
                            COMMA_LOCKVAL_SRC_POS);

        if (   m->state != MediumState_NotCreated
            && m->state != MediumState_Created)
            throw i_setStateError();

        /* Build the target lock list. */
        MediumLockList *pTargetMediumLockList(new MediumLockList());
        alock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                     this /* pToLockWrite */,
                                     false /* fMediumLockWriteAll */,
                                     aParent,
                                     *pTargetMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pTargetMediumLockList;
            throw hrc;
        }

        alock.release();
        hrc = pTargetMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pTargetMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock target media '%s'"),
                           i_getLocationFull().c_str());
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::ImportTask(this, aProgress, aFilename, aFormat, aVariant,
                                       aVfsIosSrc, aParent, pTargetMediumLockList, false, aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;

        if (m->state == MediumState_NotCreated)
            m->state = MediumState_Creating;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

/**
 * Internal version of the public CloneTo API which allows to enable certain
 * optimizations to improve speed during VM cloning.
 *
 * @param aTarget            Target medium
 * @param aVariant           Which exact image format variant to use
 *                           for the destination image.
 * @param aParent            Parent medium. May be NULL.
 * @param aProgress          Progress object to use.
 * @param idxSrcImageSame    The last image in the source chain which has the
 *                           same content as the given image in the destination
 *                           chain. Use UINT32_MAX to disable this optimization.
 * @param idxDstImageSame    The last image in the destination chain which has the
 *                           same content as the given image in the source chain.
 *                           Use UINT32_MAX to disable this optimization.
 * @param aNotify            Notify about media for which metadata is changed
 *                           during execution of the function.
 * @return
 */
HRESULT Medium::i_cloneToEx(const ComObjPtr<Medium> &aTarget, MediumVariant_T aVariant,
                            const ComObjPtr<Medium> &aParent, IProgress **aProgress,
                            uint32_t idxSrcImageSame, uint32_t idxDstImageSame, bool aNotify)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    CheckComArgNotNull(aTarget);
    CheckComArgOutPointerValid(aProgress);
    ComAssertRet(aTarget != this, E_INVALIDARG);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    Medium::Task *pTask = NULL;

    try
    {
        // locking: we need the tree lock first because we access parent pointers
        // and we need to write-lock the media involved
        uint32_t    cHandles    = 3;
        LockHandle* pHandles[4] = { &m->pVirtualBox->i_getMediaTreeLockHandle(),
                                    this->lockHandle(),
                                    aTarget->lockHandle() };
        /* Only add parent to the lock if it is not null */
        if (!aParent.isNull())
            pHandles[cHandles++] = aParent->lockHandle();
        AutoWriteLock alock(cHandles,
                            pHandles
                            COMMA_LOCKVAL_SRC_POS);

        if (    aTarget->m->state != MediumState_NotCreated
            &&  aTarget->m->state != MediumState_Created)
            throw aTarget->i_setStateError();

        /* Build the source lock list. */
        MediumLockList *pSourceMediumLockList(new MediumLockList());
        alock.release();
        hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                    NULL /* pToLockWrite */,
                                    false /* fMediumLockWriteAll */,
                                    NULL,
                                    *pSourceMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            throw hrc;
        }

        /* Build the target lock list (including the to-be parent chain). */
        MediumLockList *pTargetMediumLockList(new MediumLockList());
        alock.release();
        hrc = aTarget->i_createMediumLockList(true /* fFailIfInaccessible */,
                                              aTarget /* pToLockWrite */,
                                              false /* fMediumLockWriteAll */,
                                              aParent,
                                              *pTargetMediumLockList);
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw hrc;
        }

        alock.release();
        hrc = pSourceMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock source media '%s'"),
                           i_getLocationFull().c_str());
        }
        alock.release();
        hrc = pTargetMediumLockList->Lock();
        alock.acquire();
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw setError(hrc,
                           tr("Failed to lock target media '%s'"),
                           aTarget->i_getLocationFull().c_str());
        }

        pProgress.createObject();
        hrc = pProgress->init(m->pVirtualBox,
                              static_cast <IMedium *>(this),
                              BstrFmt(tr("Creating clone medium '%s'"), aTarget->m->strLocationFull.c_str()).raw(),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
        {
            delete pSourceMediumLockList;
            delete pTargetMediumLockList;
            throw hrc;
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new Medium::CloneTask(this, pProgress, aTarget, aVariant,
                                      aParent, idxSrcImageSame,
                                      idxDstImageSame, pSourceMediumLockList,
                                      pTargetMediumLockList, false, false, aNotify);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;

        if (aTarget->m->state == MediumState_NotCreated)
            aTarget->m->state = MediumState_Creating;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
            pProgress.queryInterfaceTo(aProgress);
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

/**
 * Returns the key identifier for this medium if encryption is configured.
 *
 * @returns Key identifier or empty string if no encryption is configured.
 */
const Utf8Str& Medium::i_getKeyId()
{
    ComObjPtr<Medium> pBase = i_getBase();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::StringsMap::const_iterator it = pBase->m->mapProperties.find("CRYPT/KeyId");
    if (it == pBase->m->mapProperties.end())
        return Utf8Str::Empty;

    return it->second;
}


/**
 * Returns all filter related properties.
 *
 * @returns COM status code.
 * @param   aReturnNames    Where to store the properties names on success.
 * @param   aReturnValues   Where to store the properties values on success.
 */
HRESULT Medium::i_getFilterProperties(std::vector<com::Utf8Str> &aReturnNames,
                                      std::vector<com::Utf8Str> &aReturnValues)
{
    std::vector<com::Utf8Str> aPropNames;
    std::vector<com::Utf8Str> aPropValues;
    HRESULT hrc = getProperties(Utf8Str(""), aPropNames, aPropValues);

    if (SUCCEEDED(hrc))
    {
        unsigned cReturnSize = 0;
        aReturnNames.resize(0);
        aReturnValues.resize(0);
        for (unsigned idx = 0; idx < aPropNames.size(); idx++)
        {
            if (i_isPropertyForFilter(aPropNames[idx]))
            {
                aReturnNames.resize(cReturnSize + 1);
                aReturnValues.resize(cReturnSize + 1);
                aReturnNames[cReturnSize] = aPropNames[idx];
                aReturnValues[cReturnSize] = aPropValues[idx];
                cReturnSize++;
            }
        }
    }

    return hrc;
}

/**
 * Preparation to move this medium to a new location
 *
 * @param aLocation Location of the storage unit. If the location is a FS-path,
 *                  then it can be relative to the VirtualBox home directory.
 *
 * @note Must be called from under this object's write lock.
 */
HRESULT Medium::i_preparationForMoving(const Utf8Str &aLocation)
{
    HRESULT hrc = E_FAIL;

    if (i_getLocationFull() != aLocation)
    {
        m->strNewLocationFull = aLocation;
        m->fMoveThisMedium = true;
        hrc = S_OK;
    }

    return hrc;
}

/**
 * Checking whether current operation "moving" or not
 */
bool Medium::i_isMoveOperation(const ComObjPtr<Medium> &aTarget) const
{
    RT_NOREF(aTarget);
    return m->fMoveThisMedium;
}

bool Medium::i_resetMoveOperationData()
{
    m->strNewLocationFull.setNull();
    m->fMoveThisMedium = false;
    return true;
}

Utf8Str Medium::i_getNewLocationForMoving() const
{
    if (m->fMoveThisMedium == true)
        return m->strNewLocationFull;
    else
        return Utf8Str();
}
////////////////////////////////////////////////////////////////////////////////
//
// Private methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Queries information from the medium.
 *
 * As a result of this call, the accessibility state and data members such as
 * size and description will be updated with the current information.
 *
 * @note This method may block during a system I/O call that checks storage
 *       accessibility.
 *
 * @note Caller MUST NOT hold the media tree or medium lock.
 *
 * @note Locks m->pParent for reading. Locks this object for writing.
 *
 * @param fSetImageId   Whether to reset the UUID contained in the image file
 *                      to the UUID in the medium instance data (see SetIDs())
 * @param fSetParentId  Whether to reset the parent UUID contained in the image
 *                      file to the parent UUID in the medium instance data (see
 *                      SetIDs())
 * @param autoCaller
 * @return
 */
HRESULT Medium::i_queryInfo(bool fSetImageId, bool fSetParentId, AutoCaller &autoCaller)
{
    Assert(!isWriteLockOnCurrentThread());
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   (   m->state != MediumState_Created
            && m->state != MediumState_Inaccessible
            && m->state != MediumState_LockedRead)
        || m->fClosing)
        return E_FAIL;

    HRESULT hrc = S_OK;

    int vrc = VINF_SUCCESS;

    /* check if a blocking i_queryInfo() call is in progress on some other thread,
     * and wait for it to finish if so instead of querying data ourselves */
    if (m->queryInfoRunning)
    {
        Assert(   m->state == MediumState_LockedRead
               || m->state == MediumState_LockedWrite);

        while (m->queryInfoRunning)
        {
            alock.release();
            /* must not hold the object lock now */
            Assert(!isWriteLockOnCurrentThread());
            {
                AutoReadLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);
            }
            alock.acquire();
        }

        return S_OK;
    }

    bool success = false;
    Utf8Str lastAccessError;

    /* are we dealing with a new medium constructed using the existing
     * location? */
    bool isImport = m->id.isZero();
    unsigned uOpenFlags = VD_OPEN_FLAGS_INFO;

    /* Note that we don't use VD_OPEN_FLAGS_READONLY when opening new
     * media because that would prevent necessary modifications
     * when opening media of some third-party formats for the first
     * time in VirtualBox (such as VMDK for which VDOpen() needs to
     * generate an UUID if it is missing) */
    if (    m->hddOpenMode == OpenReadOnly
         || m->type == MediumType_Readonly
         || (!isImport && !fSetImageId && !fSetParentId)
       )
        uOpenFlags |= VD_OPEN_FLAGS_READONLY;

    /* Open shareable medium with the appropriate flags */
    if (m->type == MediumType_Shareable)
        uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;

    /* Lock the medium, which makes the behavior much more consistent, must be
     * done before dropping the object lock and setting queryInfoRunning. */
    ComPtr<IToken> pToken;
    if (uOpenFlags & (VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_SHAREABLE))
        hrc = LockRead(pToken.asOutParam());
    else
        hrc = LockWrite(pToken.asOutParam());
    if (FAILED(hrc)) return hrc;

    /* Copies of the input state fields which are not read-only,
     * as we're dropping the lock. CAUTION: be extremely careful what
     * you do with the contents of this medium object, as you will
     * create races if there are concurrent changes. */
    Utf8Str format(m->strFormat);
    Utf8Str location(m->strLocationFull);
    ComObjPtr<MediumFormat> formatObj = m->formatObj;

    /* "Output" values which can't be set because the lock isn't held
     * at the time the values are determined. */
    Guid mediumId = m->id;
    uint64_t mediumSize = 0;
    uint64_t mediumLogicalSize = 0;

    /* Flag whether a base image has a non-zero parent UUID and thus
     * need repairing after it was closed again. */
    bool fRepairImageZeroParentUuid = false;

    ComObjPtr<VirtualBox> pVirtualBox = m->pVirtualBox;

    /* must be set before leaving the object lock the first time */
    m->queryInfoRunning = true;

    /* must leave object lock now, because a lock from a higher lock class
     * is needed and also a lengthy operation is coming */
    alock.release();
    autoCaller.release();

    /* Note that taking the queryInfoSem after leaving the object lock above
     * can lead to short spinning of the loops waiting for i_queryInfo() to
     * complete. This is unavoidable since the other order causes a lock order
     * violation: here it would be requesting the object lock (at the beginning
     * of the method), then queryInfoSem, and below the other way round. */
    AutoWriteLock qlock(m->queryInfoSem COMMA_LOCKVAL_SRC_POS);

    /* take the opportunity to have a media tree lock, released initially */
    Assert(!isWriteLockOnCurrentThread());
    Assert(!pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    AutoWriteLock treeLock(pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
    treeLock.release();

    /* re-take the caller, but not the object lock, to keep uninit away */
    autoCaller.add();
    if (FAILED(autoCaller.hrc()))
    {
        m->queryInfoRunning = false;
        return autoCaller.hrc();
    }

    try
    {
        /* skip accessibility checks for host drives */
        if (m->hostDrive)
        {
            success = true;
            throw S_OK;
        }

        PVDISK hdd;
        vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /** @todo This kind of opening of media is assuming that diff
             * media can be opened as base media. Should be documented that
             * it must work for all medium format backends. */
            vrc = VDOpen(hdd,
                         format.c_str(),
                         location.c_str(),
                         uOpenFlags | m->uOpenFlagsDef,
                         m->vdImageIfaces);
            if (RT_FAILURE(vrc))
            {
                lastAccessError = Utf8StrFmt(tr("Could not open the medium '%s'%s"),
                                             location.c_str(), i_vdError(vrc).c_str());
                throw S_OK;
            }

            if (formatObj->i_getCapabilities() & MediumFormatCapabilities_Uuid)
            {
                /* Modify the UUIDs if necessary. The associated fields are
                 * not modified by other code, so no need to copy. */
                if (fSetImageId)
                {
                    alock.acquire();
                    vrc = VDSetUuid(hdd, 0, m->uuidImage.raw());
                    alock.release();
                    if (RT_FAILURE(vrc))
                    {
                        lastAccessError = Utf8StrFmt(tr("Could not update the UUID of medium '%s'%s"),
                                         location.c_str(), i_vdError(vrc).c_str());
                        throw S_OK;
                    }
                    mediumId = m->uuidImage;
                }
                if (fSetParentId)
                {
                    alock.acquire();
                    vrc = VDSetParentUuid(hdd, 0, m->uuidParentImage.raw());
                    alock.release();
                    if (RT_FAILURE(vrc))
                    {
                        lastAccessError = Utf8StrFmt(tr("Could not update the parent UUID of medium '%s'%s"),
                                         location.c_str(), i_vdError(vrc).c_str());
                        throw S_OK;
                    }
                }
                /* zap the information, these are no long-term members */
                alock.acquire();
                unconst(m->uuidImage).clear();
                unconst(m->uuidParentImage).clear();
                alock.release();

                /* check the UUID */
                RTUUID uuid;
                vrc = VDGetUuid(hdd, 0, &uuid);
                ComAssertRCThrow(vrc, E_FAIL);

                if (isImport)
                {
                    mediumId = uuid;

                    if (mediumId.isZero() && (m->hddOpenMode == OpenReadOnly))
                        // only when importing a VDMK that has no UUID, create one in memory
                        mediumId.create();
                }
                else
                {
                    Assert(!mediumId.isZero());

                    if (mediumId != uuid)
                    {
                        /** @todo r=klaus this always refers to VirtualBox.xml as the medium registry, even for new VMs */
                        lastAccessError = Utf8StrFmt(
                                tr("UUID {%RTuuid} of the medium '%s' does not match the value {%RTuuid} stored in the media registry ('%s')"),
                                &uuid,
                                location.c_str(),
                                mediumId.raw(),
                                pVirtualBox->i_settingsFilePath().c_str());
                        throw S_OK;
                    }
                }
            }
            else
            {
                /* the backend does not support storing UUIDs within the
                 * underlying storage so use what we store in XML */

                if (fSetImageId)
                {
                    /* set the UUID if an API client wants to change it */
                    alock.acquire();
                    mediumId = m->uuidImage;
                    alock.release();
                }
                else if (isImport)
                {
                    /* generate an UUID for an imported UUID-less medium */
                    mediumId.create();
                }
            }

            /* set the image uuid before the below parent uuid handling code
             * might place it somewhere in the media tree, so that the medium
             * UUID is valid at this point */
            alock.acquire();
            if (isImport || fSetImageId)
                unconst(m->id) = mediumId;
            alock.release();

            /* get the medium variant */
            unsigned uImageFlags;
            vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
            ComAssertRCThrow(vrc, E_FAIL);
            alock.acquire();
            m->variant = (MediumVariant_T)uImageFlags;
            alock.release();

            /* check/get the parent uuid and update corresponding state */
            if (uImageFlags & VD_IMAGE_FLAGS_DIFF)
            {
                RTUUID parentId;
                vrc = VDGetParentUuid(hdd, 0, &parentId);
                ComAssertRCThrow(vrc, E_FAIL);

                /* streamOptimized VMDK images are only accepted as base
                 * images, as this allows automatic repair of OVF appliances.
                 * Since such images don't support random writes they will not
                 * be created for diff images. Only an overly smart user might
                 * manually create this case. Too bad for him. */
                if (   (isImport || fSetParentId)
                    && !(uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
                {
                    /* the parent must be known to us. Note that we freely
                     * call locking methods of mVirtualBox and parent, as all
                     * relevant locks must be already held. There may be no
                     * concurrent access to the just opened medium on other
                     * threads yet (and init() will fail if this method reports
                     * MediumState_Inaccessible) */

                    ComObjPtr<Medium> pParent;
                    if (RTUuidIsNull(&parentId))
                        hrc = VBOX_E_OBJECT_NOT_FOUND;
                    else
                        hrc = pVirtualBox->i_findHardDiskById(Guid(parentId), false /* aSetError */, &pParent);
                    if (FAILED(hrc))
                    {
                        if (fSetImageId && !fSetParentId)
                        {
                            /* If the image UUID gets changed for an existing
                             * image then the parent UUID can be stale. In such
                             * cases clear the parent information. The parent
                             * information may/will be re-set later if the
                             * API client wants to adjust a complete medium
                             * hierarchy one by one. */
                            hrc = S_OK;
                            alock.acquire();
                            RTUuidClear(&parentId);
                            vrc = VDSetParentUuid(hdd, 0, &parentId);
                            alock.release();
                            ComAssertRCThrow(vrc, E_FAIL);
                        }
                        else
                        {
                            lastAccessError = Utf8StrFmt(tr("Parent medium with UUID {%RTuuid} of the medium '%s' is not found in the media registry ('%s')"),
                                                         &parentId, location.c_str(),
                                                         pVirtualBox->i_settingsFilePath().c_str());
                            throw S_OK;
                        }
                    }

                    /* must drop the caller before taking the tree lock */
                    autoCaller.release();
                    /* we set m->pParent & children() */
                    treeLock.acquire();
                    autoCaller.add();
                    if (FAILED(autoCaller.hrc()))
                        throw autoCaller.hrc();

                    if (m->pParent)
                        i_deparent();

                    if (!pParent.isNull())
                        if (pParent->i_getDepth() >= SETTINGS_MEDIUM_DEPTH_MAX)
                        {
                            AutoReadLock plock(pParent COMMA_LOCKVAL_SRC_POS);
                            throw setError(VBOX_E_INVALID_OBJECT_STATE,
                                           tr("Cannot open differencing image for medium '%s', because it exceeds the medium tree depth limit. Please merge some images which you no longer need"),
                                           pParent->m->strLocationFull.c_str());
                        }
                    i_setParent(pParent);

                    treeLock.release();
                }
                else
                {
                    /* must drop the caller before taking the tree lock */
                    autoCaller.release();
                    /* we access m->pParent */
                    treeLock.acquire();
                    autoCaller.add();
                    if (FAILED(autoCaller.hrc()))
                        throw autoCaller.hrc();

                    /* check that parent UUIDs match. Note that there's no need
                     * for the parent's AutoCaller (our lifetime is bound to
                     * it) */

                    if (m->pParent.isNull())
                    {
                        /* Due to a bug in VDCopy() in VirtualBox 3.0.0-3.0.14
                         * and 3.1.0-3.1.8 there are base images out there
                         * which have a non-zero parent UUID. No point in
                         * complaining about them, instead automatically
                         * repair the problem. Later we can bring back the
                         * error message, but we should wait until really
                         * most users have repaired their images, either with
                         * VBoxFixHdd or this way. */
#if 1
                        fRepairImageZeroParentUuid = true;
#else /* 0 */
                        lastAccessError = Utf8StrFmt(
                                tr("Medium type of '%s' is differencing but it is not associated with any parent medium in the media registry ('%s')"),
                                location.c_str(),
                                pVirtualBox->settingsFilePath().c_str());
                        treeLock.release();
                        throw S_OK;
#endif /* 0 */
                    }

                    {
                        autoCaller.release();
                        AutoReadLock parentLock(m->pParent COMMA_LOCKVAL_SRC_POS);
                        autoCaller.add();
                        if (FAILED(autoCaller.hrc()))
                            throw autoCaller.hrc();

                        if (   !fRepairImageZeroParentUuid
                            && m->pParent->i_getState() != MediumState_Inaccessible
                            && m->pParent->i_getId() != parentId)
                        {
                            /** @todo r=klaus this always refers to VirtualBox.xml as the medium registry, even for new VMs */
                            lastAccessError = Utf8StrFmt(
                                    tr("Parent UUID {%RTuuid} of the medium '%s' does not match UUID {%RTuuid} of its parent medium stored in the media registry ('%s')"),
                                    &parentId, location.c_str(),
                                    m->pParent->i_getId().raw(),
                                    pVirtualBox->i_settingsFilePath().c_str());
                            parentLock.release();
                            treeLock.release();
                            throw S_OK;
                        }
                    }

                    /// @todo NEWMEDIA what to do if the parent is not
                    /// accessible while the diff is? Probably nothing. The
                    /// real code will detect the mismatch anyway.

                    treeLock.release();
                }
            }

            mediumSize = VDGetFileSize(hdd, 0);
            mediumLogicalSize = VDGetSize(hdd, 0);

            success = true;
        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }

        vrc = VDDestroy(hdd);
        if (RT_FAILURE(vrc))
        {
            lastAccessError.printf(tr("Could not update and close the medium '%s'%s"),
                                   location.c_str(), i_vdError(vrc).c_str());
            success = false;
            throw S_OK;
        }
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    autoCaller.release();
    treeLock.acquire();
    autoCaller.add();
    if (FAILED(autoCaller.hrc()))
    {
        m->queryInfoRunning = false;
        return autoCaller.hrc();
    }
    alock.acquire();

    if (success)
    {
        m->size = mediumSize;
        m->logicalSize = mediumLogicalSize;
        m->strLastAccessError.setNull();
    }
    else
    {
        m->strLastAccessError = lastAccessError;
        Log1WarningFunc(("'%s' is not accessible (error='%s', hrc=%Rhrc, vrc=%Rrc)\n",
                         location.c_str(), m->strLastAccessError.c_str(), hrc, vrc));
    }

    /* Set the proper state according to the result of the check */
    if (success)
        m->preLockState = MediumState_Created;
    else
        m->preLockState = MediumState_Inaccessible;

    /* unblock anyone waiting for the i_queryInfo results */
    qlock.release();
    m->queryInfoRunning = false;

    pToken->Abandon();
    pToken.setNull();

    if (FAILED(hrc))
        return hrc;

    /* If this is a base image which incorrectly has a parent UUID set,
     * repair the image now by zeroing the parent UUID. This is only done
     * when we have structural information from a config file, on import
     * this is not possible. If someone would accidentally call openMedium
     * with a diff image before the base is registered this would destroy
     * the diff. Not acceptable. */
    do
    {
        if (fRepairImageZeroParentUuid)
        {
            hrc = LockWrite(pToken.asOutParam());
            if (FAILED(hrc))
                break;

            alock.release();

            try
            {
                PVDISK hdd;
                vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                try
                {
                    vrc = VDOpen(hdd,
                                 format.c_str(),
                                 location.c_str(),
                                 (uOpenFlags & ~VD_OPEN_FLAGS_READONLY) | m->uOpenFlagsDef,
                                 m->vdImageIfaces);
                    if (RT_FAILURE(vrc))
                        throw S_OK;

                    RTUUID zeroParentUuid;
                    RTUuidClear(&zeroParentUuid);
                    vrc = VDSetParentUuid(hdd, 0, &zeroParentUuid);
                    ComAssertRCThrow(vrc, E_FAIL);
                }
                catch (HRESULT hrcXcpt)
                {
                    hrc = hrcXcpt;
                }

                VDDestroy(hdd);
            }
            catch (HRESULT hrcXcpt)
            {
                hrc = hrcXcpt;
            }

            pToken->Abandon();
            pToken.setNull();
            if (FAILED(hrc))
                break;
        }
    } while(0);

    return hrc;
}

/**
 * Performs extra checks if the medium can be closed and returns S_OK in
 * this case. Otherwise, returns a respective error message. Called by
 * Close() under the medium tree lock and the medium lock.
 *
 * @note Also reused by Medium::Reset().
 *
 * @note Caller must hold the media tree write lock!
 */
HRESULT Medium::i_canClose()
{
    Assert(m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    if (i_getChildren().size() != 0)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot close medium '%s' because it has %d child media", "", i_getChildren().size()),
                        m->strLocationFull.c_str(), i_getChildren().size());

    return S_OK;
}

/**
 * Unregisters this medium with mVirtualBox. Called by close() under the medium tree lock.
 *
 * @note Caller must have locked the media tree lock for writing!
 */
HRESULT Medium::i_unregisterWithVirtualBox()
{
    /* Note that we need to de-associate ourselves from the parent to let
     * VirtualBox::i_unregisterMedium() properly save the registry */

    /* we modify m->pParent and access children */
    Assert(m->pVirtualBox->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    Medium *pParentBackup = m->pParent;
    AssertReturn(i_getChildren().size() == 0, E_FAIL);
    if (m->pParent)
        i_deparent();

    HRESULT hrc = m->pVirtualBox->i_unregisterMedium(this);
    if (FAILED(hrc))
    {
        if (pParentBackup)
        {
            // re-associate with the parent as we are still relatives in the registry
            i_setParent(pParentBackup);
        }
    }

    return hrc;
}

/**
 * Like SetProperty but do not trigger a settings store. Only for internal use!
 */
HRESULT Medium::i_setPropertyDirect(const Utf8Str &aName, const Utf8Str &aValue)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock mlock(this COMMA_LOCKVAL_SRC_POS);

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return i_setStateError();
    }

    m->mapProperties[aName] = aValue;

    return S_OK;
}

/**
 * Sets the extended error info according to the current media state.
 *
 * @note Must be called from under this object's write or read lock.
 */
HRESULT Medium::i_setStateError()
{
    HRESULT hrc;

    switch (m->state)
    {
        case MediumState_NotCreated:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Storage for the medium '%s' is not created"),
                           m->strLocationFull.c_str());
            break;
        case MediumState_Created:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Storage for the medium '%s' is already created"),
                           m->strLocationFull.c_str());
            break;
        case MediumState_LockedRead:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Medium '%s' is locked for reading by another task"),
                           m->strLocationFull.c_str());
            break;
        case MediumState_LockedWrite:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Medium '%s' is locked for writing by another task"),
                           m->strLocationFull.c_str());
            break;
        case MediumState_Inaccessible:
            /* be in sync with Console::powerUpThread() */
            if (!m->strLastAccessError.isEmpty())
                hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is not accessible. %s"),
                               m->strLocationFull.c_str(), m->strLastAccessError.c_str());
            else
                hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Medium '%s' is not accessible"),
                               m->strLocationFull.c_str());
            break;
        case MediumState_Creating:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Storage for the medium '%s' is being created"),
                           m->strLocationFull.c_str());
            break;
        case MediumState_Deleting:
            hrc = setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Storage for the medium '%s' is being deleted"),
                           m->strLocationFull.c_str());
            break;
        default:
            AssertFailed();
            hrc = E_FAIL;
            break;
    }

    return hrc;
}

/**
 * Sets the value of m->strLocationFull. The given location must be a fully
 * qualified path; relative paths are not supported here.
 *
 * As a special exception, if the specified location is a file path that ends with '/'
 * then the file name part will be generated by this method automatically in the format
 * '{\<uuid\>}.\<ext\>' where \<uuid\> is a fresh UUID that this method will generate
 * and assign to this medium, and \<ext\> is the default extension for this
 * medium's storage format. Note that this procedure requires the media state to
 * be NotCreated and will return a failure otherwise.
 *
 * @param aLocation Location of the storage unit. If the location is a FS-path,
 *                  then it can be relative to the VirtualBox home directory.
 * @param aFormat   Optional fallback format if it is an import and the format
 *                  cannot be determined.
 *
 * @note Must be called from under this object's write lock.
 */
HRESULT Medium::i_setLocation(const Utf8Str &aLocation,
                              const Utf8Str &aFormat /* = Utf8Str::Empty */)
{
    AssertReturn(!aLocation.isEmpty(), E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* formatObj may be null only when initializing from an existing path and
     * no format is known yet */
    AssertReturn(    (!m->strFormat.isEmpty() && !m->formatObj.isNull())
                  || (    getObjectState().getState() == ObjectState::InInit
                       && m->state != MediumState_NotCreated
                       && m->id.isZero()
                       && m->strFormat.isEmpty()
                       && m->formatObj.isNull()),
                 E_FAIL);

    /* are we dealing with a new medium constructed using the existing
     * location? */
    bool isImport = m->strFormat.isEmpty();

    if (   isImport
        || (   (m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File)
            && !m->hostDrive))
    {
        Guid id;

        Utf8Str locationFull(aLocation);

        if (m->state == MediumState_NotCreated)
        {
            /* must be a file (formatObj must be already known) */
            Assert(m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File);

            if (RTPathFilename(aLocation.c_str()) == NULL)
            {
                /* no file name is given (either an empty string or ends with a
                 * slash), generate a new UUID + file name if the state allows
                 * this */

                ComAssertMsgRet(!m->formatObj->i_getFileExtensions().empty(),
                                (tr("Must be at least one extension if it is MediumFormatCapabilities_File\n")),
                                E_FAIL);

                Utf8Str strExt = m->formatObj->i_getFileExtensions().front();
                ComAssertMsgRet(!strExt.isEmpty(),
                                (tr("Default extension must not be empty\n")),
                                E_FAIL);

                id.create();

                locationFull = Utf8StrFmt("%s{%RTuuid}.%s",
                                          aLocation.c_str(), id.raw(), strExt.c_str());
            }
        }

        // we must always have full paths now (if it refers to a file)
        if (   (   m->formatObj.isNull()
                || m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File)
            && !RTPathStartsWithRoot(locationFull.c_str()))
            return setError(VBOX_E_FILE_ERROR,
                            tr("The given path '%s' is not fully qualified"),
                            locationFull.c_str());

        /* detect the backend from the storage unit if importing */
        if (isImport)
        {
            VDTYPE const enmDesiredType = i_convertDeviceType();
            VDTYPE enmType = VDTYPE_INVALID;
            char *backendName = NULL;

            /* is it a file? */
            RTFILE hFile;
            int vrc = RTFileOpen(&hFile, locationFull.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
            if (RT_SUCCESS(vrc))
            {
                RTFileClose(hFile);
                vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                                  locationFull.c_str(), enmDesiredType, &backendName, &enmType);
            }
            else if (   vrc != VERR_FILE_NOT_FOUND
                     && vrc != VERR_PATH_NOT_FOUND
                     && vrc != VERR_ACCESS_DENIED
                     && locationFull != aLocation)
            {
                /* assume it's not a file, restore the original location */
                locationFull = aLocation;
                vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                                  locationFull.c_str(), enmDesiredType, &backendName, &enmType);
            }

            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_ACCESS_DENIED)
                    return setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                        tr("Permission problem accessing the file for the medium '%s' (%Rrc)"),
                                        locationFull.c_str(), vrc);
                if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
                    return setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                        tr("Could not find file for the medium '%s' (%Rrc)"),
                                        locationFull.c_str(), vrc);
                if (aFormat.isEmpty())
                    return setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                        tr("Could not get the storage format of the medium '%s' (%Rrc)"),
                                        locationFull.c_str(), vrc);
                HRESULT hrc = i_setFormat(aFormat);
                /* setFormat() must not fail since we've just used the backend so
                         * the format object must be there */
                AssertComRCReturnRC(hrc);
            }
            else if (   enmType == VDTYPE_INVALID
                     || m->devType != i_convertToDeviceType(enmType))
            {
                /*
                 * The user tried to use a image as a device which is not supported
                 * by the backend.
                 */
                RTStrFree(backendName);
                return setError(E_FAIL,
                                tr("The medium '%s' can't be used as the requested device type (%s, detected %s)"),
                                locationFull.c_str(), getDeviceTypeName(m->devType), getVDTypeName(enmType));
            }
            else
            {
                ComAssertRet(backendName != NULL && *backendName != '\0', E_FAIL);

                HRESULT hrc = i_setFormat(backendName);
                RTStrFree(backendName);

                /* setFormat() must not fail since we've just used the backend so
                 * the format object must be there */
                AssertComRCReturnRC(hrc);
            }
        }

        m->strLocationFull = locationFull;

        /* is it still a file? */
        if (    (m->formatObj->i_getCapabilities() & MediumFormatCapabilities_File)
             && (m->state == MediumState_NotCreated)
           )
            /* assign a new UUID (this UUID will be used when calling
             * VDCreateBase/VDCreateDiff as a wanted UUID). Note that we
             * also do that if we didn't generate it to make sure it is
             * either generated by us or reset to null */
            unconst(m->id) = id;
    }
    else
        m->strLocationFull = aLocation;

    return S_OK;
}

/**
 * Checks that the format ID is valid and sets it on success.
 *
 * Note that this method will caller-reference the format object on success!
 * This reference must be released somewhere to let the MediumFormat object be
 * uninitialized.
 *
 * @note Must be called from under this object's write lock.
 */
HRESULT Medium::i_setFormat(const Utf8Str &aFormat)
{
    /* get the format object first */
    {
        SystemProperties *pSysProps = m->pVirtualBox->i_getSystemProperties();
        AutoReadLock propsLock(pSysProps COMMA_LOCKVAL_SRC_POS);

        unconst(m->formatObj) = pSysProps->i_mediumFormat(aFormat);
        if (m->formatObj.isNull())
            return setError(E_INVALIDARG,
                            tr("Invalid medium storage format '%s'"),
                            aFormat.c_str());

        /* get properties (preinsert them as keys in the map). Note that the
         * map doesn't grow over the object life time since the set of
         * properties is meant to be constant. */

        Assert(m->mapProperties.empty());

        for (MediumFormat::PropertyArray::const_iterator it = m->formatObj->i_getProperties().begin();
             it != m->formatObj->i_getProperties().end();
             ++it)
        {
            m->mapProperties.insert(std::make_pair(it->strName, Utf8Str::Empty));
        }
    }

    unconst(m->strFormat) = aFormat;

    return S_OK;
}

/**
 * Converts the Medium device type to the VD type.
 */
VDTYPE Medium::i_convertDeviceType()
{
    VDTYPE enmType;

    switch (m->devType)
    {
        case DeviceType_HardDisk:
            enmType = VDTYPE_HDD;
            break;
        case DeviceType_DVD:
            enmType = VDTYPE_OPTICAL_DISC;
            break;
        case DeviceType_Floppy:
            enmType = VDTYPE_FLOPPY;
            break;
        default:
            ComAssertFailedRet(VDTYPE_INVALID);
    }

    return enmType;
}

/**
 * Converts from the VD type to the medium type.
 */
DeviceType_T Medium::i_convertToDeviceType(VDTYPE enmType)
{
    DeviceType_T devType;

    switch (enmType)
    {
        case VDTYPE_HDD:
            devType = DeviceType_HardDisk;
            break;
        case VDTYPE_OPTICAL_DISC:
            devType = DeviceType_DVD;
            break;
        case VDTYPE_FLOPPY:
            devType = DeviceType_Floppy;
            break;
        default:
            ComAssertFailedRet(DeviceType_Null);
    }

    return devType;
}

/**
 * Internal method which checks whether a property name is for a filter plugin.
 */
bool Medium::i_isPropertyForFilter(const com::Utf8Str &aName)
{
    /* If the name contains "/" use the part before as a filter name and lookup the filter. */
    size_t offSlash;
    if ((offSlash = aName.find("/", 0)) != aName.npos)
    {
        com::Utf8Str strFilter;
        com::Utf8Str strKey;

        HRESULT hrc = strFilter.assignEx(aName, 0, offSlash);
        if (FAILED(hrc))
            return false;

        hrc = strKey.assignEx(aName, offSlash + 1, aName.length() - offSlash - 1); /* Skip slash */
        if (FAILED(hrc))
            return false;

        VDFILTERINFO FilterInfo;
        int vrc = VDFilterInfoOne(strFilter.c_str(), &FilterInfo);
        if (RT_SUCCESS(vrc))
        {
            /* Check that the property exists. */
            PCVDCONFIGINFO paConfig = FilterInfo.paConfigInfo;
            while (paConfig->pszKey)
            {
                if (strKey.equals(paConfig->pszKey))
                    return true;
                paConfig++;
            }
        }
    }

    return false;
}

/**
 * Returns the last error message collected by the i_vdErrorCall callback and
 * resets it.
 *
 * The error message is returned prepended with a dot and a space, like this:
 * <code>
 *   ". <error_text> (%Rrc)"
 * </code>
 * to make it easily appendable to a more general error message. The @c %Rrc
 * format string is given @a aVRC as an argument.
 *
 * If there is no last error message collected by i_vdErrorCall or if it is a
 * null or empty string, then this function returns the following text:
 * <code>
 *   " (%Rrc)"
 * </code>
 *
 * @note Doesn't do any object locking; it is assumed that the caller makes sure
 *       the callback isn't called by more than one thread at a time.
 *
 * @param aVRC  VBox error code to use when no error message is provided.
 */
Utf8Str Medium::i_vdError(int aVRC)
{
    Utf8Str error;

    if (m->vdError.isEmpty())
        error.printf(" (%Rrc)", aVRC);
    else
        error.printf(".\n%s", m->vdError.c_str());

    m->vdError.setNull();

    return error;
}

/**
 * Error message callback.
 *
 * Puts the reported error message to the m->vdError field.
 *
 * @note Doesn't do any object locking; it is assumed that the caller makes sure
 *       the callback isn't called by more than one thread at a time.
 *
 * @param   pvUser          The opaque data passed on container creation.
 * @param   vrc             The VBox error code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments.
 */
/*static*/
DECLCALLBACK(void) Medium::i_vdErrorCall(void *pvUser, int vrc, RT_SRC_POS_DECL,
                                         const char *pszFormat, va_list va)
{
    NOREF(pszFile); NOREF(iLine); NOREF(pszFunction); /* RT_SRC_POS_DECL */

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturnVoid(that != NULL);

    va_list vaCopy; /* For gcc */
    va_copy(vaCopy, va);
    if (that->m->vdError.isEmpty())
        that->m->vdError.printf("%N (%Rrc)", pszFormat, &vaCopy, vrc);
    else
        that->m->vdError.appendPrintf(".\n%N (%Rrc)", pszFormat, &vaCopy, vrc);
    va_end(vaCopy);
}

/* static */
DECLCALLBACK(bool) Medium::i_vdConfigAreKeysValid(void *pvUser,
                                                  const char * /* pszzValid */)
{
    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, false);

    /* we always return true since the only keys we have are those found in
     * VDBACKENDINFO */
    return true;
}

/* static */
DECLCALLBACK(int) Medium::i_vdConfigQuerySize(void *pvUser,
                                              const char *pszName,
                                              size_t *pcbValue)
{
    AssertPtrReturn(pcbValue, VERR_INVALID_POINTER);

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, VERR_GENERAL_FAILURE);

    settings::StringsMap::const_iterator it = that->m->mapProperties.find(Utf8Str(pszName));
    if (it == that->m->mapProperties.end())
        return VERR_CFGM_VALUE_NOT_FOUND;

    /* we interpret null values as "no value" in Medium */
    if (it->second.isEmpty())
        return VERR_CFGM_VALUE_NOT_FOUND;

    *pcbValue = it->second.length() + 1 /* include terminator */;

    return VINF_SUCCESS;
}

/* static */
DECLCALLBACK(int) Medium::i_vdConfigQuery(void *pvUser,
                                          const char *pszName,
                                          char *pszValue,
                                          size_t cchValue)
{
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, VERR_GENERAL_FAILURE);

    settings::StringsMap::const_iterator it = that->m->mapProperties.find(Utf8Str(pszName));
    if (it == that->m->mapProperties.end())
        return VERR_CFGM_VALUE_NOT_FOUND;

    /* we interpret null values as "no value" in Medium */
    if (it->second.isEmpty())
        return VERR_CFGM_VALUE_NOT_FOUND;

    const Utf8Str &value = it->second;
    if (value.length() >= cchValue)
        return VERR_CFGM_NOT_ENOUGH_SPACE;

    memcpy(pszValue, value.c_str(), value.length() + 1);

    return VINF_SUCCESS;
}

DECLCALLBACK(bool) Medium::i_vdCryptoConfigAreKeysValid(void *pvUser, const char *pszzValid)
{
    /* Just return always true here. */
    NOREF(pvUser);
    NOREF(pszzValid);
    return true;
}

DECLCALLBACK(int) Medium::i_vdCryptoConfigQuerySize(void *pvUser, const char *pszName, size_t *pcbValue)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertPtrReturn(pcbValue, VERR_INVALID_POINTER);

    size_t cbValue = 0;
    if (!strcmp(pszName, "Algorithm"))
        cbValue = strlen(pSettings->pszCipher) + 1;
    else if (!strcmp(pszName, "KeyId"))
        cbValue = sizeof("irrelevant");
    else if (!strcmp(pszName, "KeyStore"))
    {
        if (!pSettings->pszKeyStoreLoad)
            return VERR_CFGM_VALUE_NOT_FOUND;
        cbValue = strlen(pSettings->pszKeyStoreLoad) + 1;
    }
    else if (!strcmp(pszName, "CreateKeyStore"))
        cbValue = 2; /* Single digit + terminator. */
    else
        return VERR_CFGM_VALUE_NOT_FOUND;

    *pcbValue = cbValue + 1 /* include terminator */;

    return VINF_SUCCESS;
}

DECLCALLBACK(int) Medium::i_vdCryptoConfigQuery(void *pvUser, const char *pszName,
                                                char *pszValue, size_t cchValue)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    const char *psz = NULL;
    if (!strcmp(pszName, "Algorithm"))
        psz = pSettings->pszCipher;
    else if (!strcmp(pszName, "KeyId"))
        psz = "irrelevant";
    else if (!strcmp(pszName, "KeyStore"))
        psz = pSettings->pszKeyStoreLoad;
    else if (!strcmp(pszName, "CreateKeyStore"))
    {
        if (pSettings->fCreateKeyStore)
            psz = "1";
        else
            psz = "0";
    }
    else
        return VERR_CFGM_VALUE_NOT_FOUND;

    size_t cch = strlen(psz);
    if (cch >= cchValue)
        return VERR_CFGM_NOT_ENOUGH_SPACE;

    memcpy(pszValue, psz, cch + 1);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) Medium::i_vdConfigUpdate(void *pvUser,
                                           bool fCreate,
                                           const char *pszName,
                                           const char *pszValue)
{
    Medium *that = (Medium *)pvUser;

    // Detect if this runs inside i_queryInfo() on the current thread.
    // Skip if not. Check does not need synchronization.
    if (!that->m || !that->m->queryInfoRunning || !that->m->queryInfoSem.isWriteLockOnCurrentThread())
        return VINF_SUCCESS;

    // It's guaranteed that this code is executing inside Medium::i_queryInfo,
    // can assume it took care of synchronization.
    int rv = VINF_SUCCESS;
    Utf8Str strName(pszName);
    settings::StringsMap::const_iterator it = that->m->mapProperties.find(strName);
    if (it == that->m->mapProperties.end() && !fCreate)
        rv = VERR_CFGM_VALUE_NOT_FOUND;
    else
        that->m->mapProperties[strName] = Utf8Str(pszValue);
    return rv;
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyRetain(void *pvUser, const char *pszId,
                                              const uint8_t **ppbKey, size_t *pcbKey)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    NOREF(pszId);
    NOREF(ppbKey);
    NOREF(pcbKey);
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertMsgFailedReturn(("This method should not be called here!\n"), VERR_INVALID_STATE);
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyRelease(void *pvUser, const char *pszId)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    NOREF(pszId);
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertMsgFailedReturn(("This method should not be called here!\n"), VERR_INVALID_STATE);
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyStorePasswordRetain(void *pvUser, const char *pszId, const char **ppszPassword)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    NOREF(pszId);
    *ppszPassword = pSettings->pszPassword;
    return VINF_SUCCESS;
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyStorePasswordRelease(void *pvUser, const char *pszId)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    NOREF(pszId);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyStoreSave(void *pvUser, const void *pvKeyStore, size_t cbKeyStore)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    pSettings->pszKeyStore = (char *)RTMemAllocZ(cbKeyStore);
    if (!pSettings->pszKeyStore)
        return VERR_NO_MEMORY;

    memcpy(pSettings->pszKeyStore, pvKeyStore, cbKeyStore);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) Medium::i_vdCryptoKeyStoreReturnParameters(void *pvUser, const char *pszCipher,
                                                             const uint8_t *pbDek, size_t cbDek)
{
    MediumCryptoFilterSettings *pSettings = (MediumCryptoFilterSettings *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    pSettings->pszCipherReturned = RTStrDup(pszCipher);
    pSettings->pbDek             = pbDek;
    pSettings->cbDek             = cbDek;

    return pSettings->pszCipherReturned ? VINF_SUCCESS : VERR_NO_MEMORY;
}

/**
 * Creates a VDISK instance for this medium.
 *
 * @note    Caller should not hold any medium related locks as this method will
 *          acquire the medium lock for writing and others (VirtualBox).
 *
 * @returns COM status code.
 * @param   fWritable               Whether to return a writable VDISK instance
 *                                  (true) or a read-only one (false).
 * @param   pKeyStore               The key store.
 * @param   ppHdd                   Where to return the pointer to the VDISK on
 *                                  success.
 * @param   pMediumLockList         The lock list to populate and lock.  Caller
 *                                  is responsible for calling the destructor or
 *                                  MediumLockList::Clear() after destroying
 *                                  @a *ppHdd
 * @param   pCryptoSettings         The crypto settings to use for setting up
 *                                  decryption/encryption of the VDISK.  This object
 *                                  must be alive until the VDISK is destroyed!
 */
HRESULT Medium::i_openForIO(bool fWritable, SecretKeyStore *pKeyStore, PVDISK *ppHdd, MediumLockList *pMediumLockList,
                            MediumCryptoFilterSettings *pCryptoSettings)
{
    /*
     * Create the media lock list and lock the media.
     */
    HRESULT hrc = i_createMediumLockList(true /* fFailIfInaccessible */,
                                         fWritable ? this : NULL /* pToLockWrite */,
                                         false /* fMediumLockWriteAll */,
                                         NULL,
                                         *pMediumLockList);
    if (SUCCEEDED(hrc))
        hrc = pMediumLockList->Lock();
    if (FAILED(hrc))
        return hrc;

    /*
     * Get the base medium before write locking this medium.
     */
    ComObjPtr<Medium> pBase = i_getBase();
    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Create the VDISK instance.
     */
    PVDISK pHdd;
    int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &pHdd);
    AssertRCReturn(vrc, E_FAIL);

    /*
     * Goto avoidance using try/catch/throw(HRESULT).
     */
    try
    {
        settings::StringsMap::iterator itKeyStore = pBase->m->mapProperties.find("CRYPT/KeyStore");
        if (itKeyStore != pBase->m->mapProperties.end())
        {
#ifdef VBOX_WITH_EXTPACK
            settings::StringsMap::iterator itKeyId = pBase->m->mapProperties.find("CRYPT/KeyId");

            ExtPackManager *pExtPackManager = m->pVirtualBox->i_getExtPackManager();
            if (pExtPackManager->i_isExtPackUsable(ORACLE_PUEL_EXTPACK_NAME))
            {
                /* Load the plugin */
                Utf8Str strPlugin;
                hrc = pExtPackManager->i_getLibraryPathForExtPack(g_szVDPlugin, ORACLE_PUEL_EXTPACK_NAME, &strPlugin);
                if (SUCCEEDED(hrc))
                {
                    vrc = VDPluginLoadFromFilename(strPlugin.c_str());
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                           tr("Retrieving encryption settings of the image failed because the encryption plugin could not be loaded (%s)"),
                                           i_vdError(vrc).c_str());
                }
                else
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                   tr("Encryption is not supported because the extension pack '%s' is missing the encryption plugin (old extension pack installed?)"),
                                   ORACLE_PUEL_EXTPACK_NAME);
            }
            else
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Encryption is not supported because the extension pack '%s' is missing"),
                               ORACLE_PUEL_EXTPACK_NAME);

            if (itKeyId == pBase->m->mapProperties.end())
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Image '%s' is configured for encryption but doesn't has a key identifier set"),
                               pBase->m->strLocationFull.c_str());

            /* Find the proper secret key in the key store. */
            if (!pKeyStore)
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Image '%s' is configured for encryption but there is no key store to retrieve the password from"),
                               pBase->m->strLocationFull.c_str());

            SecretKey *pKey = NULL;
            vrc = pKeyStore->retainSecretKey(itKeyId->second, &pKey);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_INVALID_OBJECT_STATE, vrc,
                                   tr("Failed to retrieve the secret key with ID \"%s\" from the store (%Rrc)"),
                                   itKeyId->second.c_str(), vrc);

            i_taskEncryptSettingsSetup(pCryptoSettings, NULL, itKeyStore->second.c_str(), (const char *)pKey->getKeyBuffer(),
                                       false /* fCreateKeyStore */);
            vrc = VDFilterAdd(pHdd, "CRYPT", VD_FILTER_FLAGS_DEFAULT, pCryptoSettings->vdFilterIfaces);
            pKeyStore->releaseSecretKey(itKeyId->second);
            if (vrc == VERR_VD_PASSWORD_INCORRECT)
                throw setErrorBoth(VBOX_E_PASSWORD_INCORRECT, vrc, tr("The password to decrypt the image is incorrect"));
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_INVALID_OBJECT_STATE, vrc, tr("Failed to load the decryption filter: %s"),
                                   i_vdError(vrc).c_str());
#else
            RT_NOREF(pKeyStore, pCryptoSettings);
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Encryption is not supported because extension pack support is not built in"));
#endif /* VBOX_WITH_EXTPACK */
        }

        /*
         * Open all media in the source chain.
         */
        MediumLockList::Base::const_iterator sourceListBegin = pMediumLockList->GetBegin();
        MediumLockList::Base::const_iterator sourceListEnd   = pMediumLockList->GetEnd();
        MediumLockList::Base::const_iterator mediumListLast  = sourceListEnd;
        --mediumListLast;

        for (MediumLockList::Base::const_iterator it = sourceListBegin; it != sourceListEnd; ++it)
        {
            const MediumLock &mediumLock = *it;
            const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
            AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

            /* sanity check */
            Assert(pMedium->m->state == (fWritable && it == mediumListLast ? MediumState_LockedWrite : MediumState_LockedRead));

            /* Open all media in read-only mode. */
            vrc = VDOpen(pHdd,
                         pMedium->m->strFormat.c_str(),
                         pMedium->m->strLocationFull.c_str(),
                         m->uOpenFlagsDef | (fWritable && it == mediumListLast ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY),
                         pMedium->m->vdImageIfaces);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not open the medium storage unit '%s'%s"),
                                   pMedium->m->strLocationFull.c_str(),
                                   i_vdError(vrc).c_str());
        }

        Assert(m->state == (fWritable ? MediumState_LockedWrite : MediumState_LockedRead));

        /*
         * Done!
         */
        *ppHdd = pHdd;
        return S_OK;
    }
    catch (HRESULT hrc2)
    {
        hrc = hrc2;
    }

    VDDestroy(pHdd);
    return hrc;

}

/**
 * Implementation code for the "create base" task.
 *
 * This only gets started from Medium::CreateBaseStorage() and always runs
 * asynchronously. As a result, we always save the VirtualBox.xml file when
 * we're done here.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskCreateBaseHandler(Medium::CreateBaseTask &task)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    HRESULT hrc = S_OK;

    /* these parameters we need after creation */
    uint64_t size = 0, logicalSize = 0;
    MediumVariant_T variant = MediumVariant_Standard;
    bool fGenerateUuid = false;

    try
    {
        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

        /* The object may request a specific UUID (through a special form of
        * the moveTo() argument). Otherwise we have to generate it */
        Guid id = m->id;

        fGenerateUuid = id.isZero();
        if (fGenerateUuid)
        {
            id.create();
            /* VirtualBox::i_registerMedium() will need UUID */
            unconst(m->id) = id;
        }

        Utf8Str format(m->strFormat);
        Utf8Str location(m->strLocationFull);
        uint64_t capabilities = m->formatObj->i_getCapabilities();
        ComAssertThrow(capabilities & (  MediumFormatCapabilities_CreateFixed
                                       | MediumFormatCapabilities_CreateDynamic), E_FAIL);
        Assert(m->state == MediumState_Creating);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        /* unlock before the potentially lengthy operation */
        thisLock.release();

        try
        {
            /* ensure the directory exists */
            if (capabilities & MediumFormatCapabilities_File)
            {
                hrc = VirtualBox::i_ensureFilePathExists(location, !(task.mVariant & MediumVariant_NoCreateDir) /* fCreate */);
                if (FAILED(hrc))
                    throw hrc;
            }

            VDGEOMETRY geo = { 0, 0, 0 }; /* auto-detect */

            vrc = VDCreateBase(hdd,
                               format.c_str(),
                               location.c_str(),
                               task.mSize,
                               task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted),
                               NULL,
                               &geo,
                               &geo,
                               id.raw(),
                               VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                               m->vdImageIfaces,
                               task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_VD_INVALID_TYPE)
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Parameters for creating the medium storage unit '%s' are invalid%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
                else
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not create the medium storage unit '%s'%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
            }

            if (task.mVariant & MediumVariant_Formatted)
            {
                RTVFSFILE hVfsFile;
                vrc = VDCreateVfsFileFromDisk(hdd, 0 /*fFlags*/, &hVfsFile);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Opening medium storage unit '%s' failed%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
                RTERRINFOSTATIC ErrInfo;
                vrc = RTFsFatVolFormat(hVfsFile, 0 /* offVol */, 0 /* cbVol */, RTFSFATVOL_FMT_F_FULL,
                                       0 /* cbSector */, 0 /* cbSectorPerCluster */, RTFSFATTYPE_INVALID,
                                       0 /* cHeads */, 0 /* cSectorsPerTrack*/, 0 /* bMedia */,
                                       0 /* cRootDirEntries */, 0 /* cHiddenSectors */,
                                       RTErrInfoInitStatic(&ErrInfo));
                RTVfsFileRelease(hVfsFile);
                if (RT_FAILURE(vrc) && RTErrInfoIsSet(&ErrInfo.Core))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Formatting medium storage unit '%s' failed: %s"),
                                       location.c_str(), ErrInfo.Core.pszMsg);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Formatting medium storage unit '%s' failed%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
            }

            size = VDGetFileSize(hdd, 0);
            logicalSize = VDGetSize(hdd, 0);
            unsigned uImageFlags;
            vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
            if (RT_SUCCESS(vrc))
                variant = (MediumVariant_T)uImageFlags;
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        /* register with mVirtualBox as the last step and move to
         * Created state only on success (leaving an orphan file is
         * better than breaking media registry consistency) */
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
        ComObjPtr<Medium> pMedium;
        hrc = m->pVirtualBox->i_registerMedium(this, &pMedium, treeLock);
        Assert(pMedium == NULL || this == pMedium);
    }

    // re-acquire the lock before changing state
    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    if (SUCCEEDED(hrc))
    {
        m->state = MediumState_Created;

        m->size = size;
        m->logicalSize = logicalSize;
        m->variant = variant;

        thisLock.release();
        i_markRegistriesModified();
        if (task.isAsync())
        {
            // in asynchronous mode, save settings now
            m->pVirtualBox->i_saveModifiedRegistries();
        }
    }
    else
    {
        /* back to NotCreated on failure */
        m->state = MediumState_NotCreated;

        /* reset UUID to prevent it from being reused next time */
        if (fGenerateUuid)
            unconst(m->id).clear();
    }

    if (task.NotifyAboutChanges() && SUCCEEDED(hrc))
    {
        m->pVirtualBox->i_onMediumConfigChanged(this);
        m->pVirtualBox->i_onMediumRegistered(m->id, m->devType, TRUE);
    }

    return hrc;
}

/**
 * Implementation code for the "create diff" task.
 *
 * This task always gets started from Medium::createDiffStorage() and can run
 * synchronously or asynchronously depending on the "wait" parameter passed to
 * that function. If we run synchronously, the caller expects the medium
 * registry modification to be set before returning; otherwise (in asynchronous
 * mode), we save the settings ourselves.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskCreateDiffHandler(Medium::CreateDiffTask &task)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    HRESULT hrcTmp = S_OK;

    const ComObjPtr<Medium> &pTarget = task.mTarget;

    uint64_t size = 0, logicalSize = 0;
    MediumVariant_T variant = MediumVariant_Standard;
    bool fGenerateUuid = false;

    try
    {
        if (i_getDepth() >= SETTINGS_MEDIUM_DEPTH_MAX)
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
            throw setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Cannot create differencing image for medium '%s', because it exceeds the medium tree depth limit. Please merge some images which you no longer need"),
                           m->strLocationFull.c_str());
        }

        /* Lock both in {parent,child} order. */
        AutoMultiWriteLock2 mediaLock(this, pTarget COMMA_LOCKVAL_SRC_POS);

        /* The object may request a specific UUID (through a special form of
         * the moveTo() argument). Otherwise we have to generate it */
        Guid targetId = pTarget->m->id;

        fGenerateUuid = targetId.isZero();
        if (fGenerateUuid)
        {
            targetId.create();
            /* VirtualBox::i_registerMedium() will need UUID */
            unconst(pTarget->m->id) = targetId;
        }

        Guid id = m->id;

        Utf8Str targetFormat(pTarget->m->strFormat);
        Utf8Str targetLocation(pTarget->m->strLocationFull);
        uint64_t capabilities = pTarget->m->formatObj->i_getCapabilities();
        ComAssertThrow(capabilities & MediumFormatCapabilities_CreateDynamic, E_FAIL);

        Assert(pTarget->m->state == MediumState_Creating);
        Assert(m->state == MediumState_LockedRead);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        /* the two media are now protected by their non-default states;
         * unlock the media before the potentially lengthy operation */
        mediaLock.release();

        try
        {
            /* Open all media in the target chain but the last. */
            MediumLockList::Base::const_iterator targetListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator targetListEnd =
                task.mpMediumLockList->GetEnd();
            for (MediumLockList::Base::const_iterator it = targetListBegin;
                 it != targetListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();

                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /* Skip over the target diff medium */
                if (pMedium->m->state == MediumState_Creating)
                    continue;

                /* sanity check */
                Assert(pMedium->m->state == MediumState_LockedRead);

                /* Open all media in appropriate mode. */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO | m->uOpenFlagsDef,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());
            }

            /* ensure the target directory exists */
            if (capabilities & MediumFormatCapabilities_File)
            {
                HRESULT hrc = VirtualBox::i_ensureFilePathExists(targetLocation,
                                                                 !(task.mVariant & MediumVariant_NoCreateDir) /* fCreate */);
                if (FAILED(hrc))
                    throw hrc;
            }

            vrc = VDCreateDiff(hdd,
                               targetFormat.c_str(),
                               targetLocation.c_str(),
                                 (task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk))
                               | VD_IMAGE_FLAGS_DIFF,
                               NULL,
                               targetId.raw(),
                               id.raw(),
                               VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                               pTarget->m->vdImageIfaces,
                               task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_VD_INVALID_TYPE)
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Parameters for creating the differencing medium storage unit '%s' are invalid%s"),
                                       targetLocation.c_str(), i_vdError(vrc).c_str());
                else
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not create the differencing medium storage unit '%s'%s"),
                                       targetLocation.c_str(), i_vdError(vrc).c_str());
            }

            size = VDGetFileSize(hdd, VD_LAST_IMAGE);
            logicalSize = VDGetSize(hdd, VD_LAST_IMAGE);
            unsigned uImageFlags;
            vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
            if (RT_SUCCESS(vrc))
                variant = (MediumVariant_T)uImageFlags;
        }
        catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

    MultiResult mrc(hrcTmp);

    if (SUCCEEDED(mrc))
    {
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        Assert(pTarget->m->pParent.isNull());

        /* associate child with the parent, maximum depth was checked above */
        pTarget->i_setParent(this);

        /* diffs for immutable media are auto-reset by default */
        bool fAutoReset;
        {
            ComObjPtr<Medium> pBase = i_getBase();
            AutoReadLock block(pBase COMMA_LOCKVAL_SRC_POS);
            fAutoReset = (pBase->m->type == MediumType_Immutable);
        }
        {
            AutoWriteLock tlock(pTarget COMMA_LOCKVAL_SRC_POS);
            pTarget->m->autoReset = fAutoReset;
        }

        /* register with mVirtualBox as the last step and move to
         * Created state only on success (leaving an orphan file is
         * better than breaking media registry consistency) */
        ComObjPtr<Medium> pMedium;
        mrc = m->pVirtualBox->i_registerMedium(pTarget, &pMedium, treeLock);
        Assert(pTarget == pMedium);

        if (FAILED(mrc))
            /* break the parent association on failure to register */
            i_deparent();
    }

    AutoMultiWriteLock2 mediaLock(this, pTarget COMMA_LOCKVAL_SRC_POS);

    if (SUCCEEDED(mrc))
    {
        pTarget->m->state = MediumState_Created;

        pTarget->m->size = size;
        pTarget->m->logicalSize = logicalSize;
        pTarget->m->variant = variant;
    }
    else
    {
        /* back to NotCreated on failure */
        pTarget->m->state = MediumState_NotCreated;

        pTarget->m->autoReset = false;

        /* reset UUID to prevent it from being reused next time */
        if (fGenerateUuid)
            unconst(pTarget->m->id).clear();
    }

    // deregister the task registered in createDiffStorage()
    Assert(m->numCreateDiffTasks != 0);
    --m->numCreateDiffTasks;

    mediaLock.release();
    i_markRegistriesModified();
    if (task.isAsync())
    {
        // in asynchronous mode, save settings now
        m->pVirtualBox->i_saveModifiedRegistries();
    }

    /* Note that in sync mode, it's the caller's responsibility to
     * unlock the medium. */

    if (task.NotifyAboutChanges() && SUCCEEDED(mrc))
    {
        m->pVirtualBox->i_onMediumConfigChanged(this);
        m->pVirtualBox->i_onMediumRegistered(m->id, m->devType, TRUE);
    }

    return mrc;
}

/**
 * Implementation code for the "merge" task.
 *
 * This task always gets started from Medium::mergeTo() and can run
 * synchronously or asynchronously depending on the "wait" parameter passed to
 * that function. If we run synchronously, the caller expects the medium
 * registry modification to be set before returning; otherwise (in asynchronous
 * mode), we save the settings ourselves.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskMergeHandler(Medium::MergeTask &task)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    HRESULT hrcTmp = S_OK;

    const ComObjPtr<Medium> &pTarget = task.mTarget;

    try
    {
        if (!task.mParentForTarget.isNull())
            if (task.mParentForTarget->i_getDepth() >= SETTINGS_MEDIUM_DEPTH_MAX)
            {
                AutoReadLock plock(task.mParentForTarget COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Cannot merge image for medium '%s', because it exceeds the medium tree depth limit. Please merge some images which you no longer need"),
                               task.mParentForTarget->m->strLocationFull.c_str());
            }

        // Resize target to source size, if possible. Otherwise throw an error.
        // It's offline resizing. Online resizing will be called in the
        // SessionMachine::onlineMergeMedium.

        uint64_t sourceSize = 0;
        Utf8Str sourceName;
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
            sourceSize = i_getLogicalSize();
            sourceName = i_getName();
        }
        uint64_t targetSize = 0;
        Utf8Str targetName;
        {
            AutoReadLock alock(pTarget COMMA_LOCKVAL_SRC_POS);
            targetSize = pTarget->i_getLogicalSize();
            targetName = pTarget->i_getName();
        }

        //reducing vm disks are not implemented yet
        if (sourceSize > targetSize)
        {
            if (i_isMediumFormatFile())
            {
                /// @todo r=klaus Can this use the standard code for creating a medium lock list?
                // Have to make own lock list, because "resize" method resizes the last image
                // in the lock chain only. The lock chain is already in the task.mpMediumLockList,
                // so just make new lock list based on it, with the right last medium. The own
                // lock list skips double locking and therefore does not affect the general lock
                // state after the "resize" method.
                MediumLockList* pMediumLockListForResize = new MediumLockList();

                for (MediumLockList::Base::iterator it = task.mpMediumLockList->GetBegin();
                     it != task.mpMediumLockList->GetEnd();
                     ++it)
                {
                    ComObjPtr<Medium> pMedium = it->GetMedium();
                    pMediumLockListForResize->Append(pMedium, pMedium->m->state == MediumState_LockedWrite);
                    if (pMedium == pTarget)
                        break;
                }

                // just to switch internal state of the lock list to avoid errors during list deletion,
                // because all media in the list already locked by task.mpMediumLockList
                HRESULT hrc = pMediumLockListForResize->Lock(true /* fSkipOverLockedMedia */);
                if (FAILED(hrc))
                {
                    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                    delete pMediumLockListForResize;
                    throw  setError(hrc,
                                    tr("Failed to lock the medium '%s' to resize before merge"),
                                    targetName.c_str());
                }

                ComObjPtr<Progress> pProgress(task.GetProgressObject());
                hrc = pTarget->i_resize(sourceSize, pMediumLockListForResize, &pProgress, true, false);
                if (FAILED(hrc))
                {
                    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                    delete pMediumLockListForResize;
                    throw setError(hrc,
                                   tr("Failed to set size of '%s' to size of '%s'"),
                                   targetName.c_str(), sourceName.c_str());
                }
                delete pMediumLockListForResize;
            }
            else
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Sizes of '%s' and '%s' are different and medium format does not support resing"),
                               sourceName.c_str(), targetName.c_str());
            }
        }

        task.GetProgressObject()->SetNextOperation(BstrFmt(tr("Merging medium '%s' to '%s'"),
                                                           i_getName().c_str(),
                                                           targetName.c_str()).raw(),
                                                   1);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            // Similar code appears in SessionMachine::onlineMergeMedium, so
            // if you make any changes below check whether they are applicable
            // in that context as well.

            unsigned uTargetIdx = VD_LAST_IMAGE;
            unsigned uSourceIdx = VD_LAST_IMAGE;
            /* Open all media in the chain. */
            MediumLockList::Base::iterator lockListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::iterator lockListEnd =
                task.mpMediumLockList->GetEnd();
            unsigned i = 0;
            for (MediumLockList::Base::iterator it = lockListBegin;
                 it != lockListEnd;
                 ++it)
            {
                MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();

                if (pMedium == this)
                    uSourceIdx = i;
                else if (pMedium == pTarget)
                    uTargetIdx = i;

                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /*
                 * complex sanity (sane complexity)
                 *
                 * The current medium must be in the Deleting (medium is merged)
                 * or LockedRead (parent medium) state if it is not the target.
                 * If it is the target it must be in the LockedWrite state.
                 */
                Assert(   (   pMedium != pTarget
                           && (   pMedium->m->state == MediumState_Deleting
                               || pMedium->m->state == MediumState_LockedRead))
                       || (   pMedium == pTarget
                           && pMedium->m->state == MediumState_LockedWrite));
                /*
                 * Medium must be the target, in the LockedRead state
                 * or Deleting state where it is not allowed to be attached
                 * to a virtual machine.
                 */
                Assert(   pMedium == pTarget
                       || pMedium->m->state == MediumState_LockedRead
                       || (   pMedium->m->backRefs.size() == 0
                           && pMedium->m->state == MediumState_Deleting));
                /* The source medium must be in Deleting state. */
                Assert(  pMedium != this
                       || pMedium->m->state == MediumState_Deleting);

                unsigned uOpenFlags = VD_OPEN_FLAGS_NORMAL;

                if (   pMedium->m->state == MediumState_LockedRead
                    || pMedium->m->state == MediumState_Deleting)
                    uOpenFlags = VD_OPEN_FLAGS_READONLY;
                if (pMedium->m->type == MediumType_Shareable)
                    uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;

                /* Open the medium */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             uOpenFlags | m->uOpenFlagsDef,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw vrc;

                i++;
            }

            ComAssertThrow(   uSourceIdx != VD_LAST_IMAGE
                           && uTargetIdx != VD_LAST_IMAGE, E_FAIL);

            vrc = VDMerge(hdd, uSourceIdx, uTargetIdx,
                          task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
                throw vrc;

            /* update parent UUIDs */
            if (!task.mfMergeForward)
            {
                /* we need to update UUIDs of all source's children
                 * which cannot be part of the container at once so
                 * add each one in there individually */
                if (task.mpChildrenToReparent)
                {
                    MediumLockList::Base::iterator childrenBegin = task.mpChildrenToReparent->GetBegin();
                    MediumLockList::Base::iterator childrenEnd = task.mpChildrenToReparent->GetEnd();
                    for (MediumLockList::Base::iterator it = childrenBegin;
                         it != childrenEnd;
                         ++it)
                    {
                        Medium *pMedium = it->GetMedium();
                        /* VD_OPEN_FLAGS_INFO since UUID is wrong yet */
                        vrc = VDOpen(hdd,
                                     pMedium->m->strFormat.c_str(),
                                     pMedium->m->strLocationFull.c_str(),
                                     VD_OPEN_FLAGS_INFO | m->uOpenFlagsDef,
                                     pMedium->m->vdImageIfaces);
                        if (RT_FAILURE(vrc))
                            throw vrc;

                        vrc = VDSetParentUuid(hdd, VD_LAST_IMAGE,
                                              pTarget->m->id.raw());
                        if (RT_FAILURE(vrc))
                            throw vrc;

                        vrc = VDClose(hdd, false /* fDelete */);
                        if (RT_FAILURE(vrc))
                            throw vrc;
                    }
                }
            }
        }
        catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }
        catch (int aVRC)
        {
            hrcTmp = setErrorBoth(VBOX_E_FILE_ERROR, aVRC,
                                  tr("Could not merge the medium '%s' to '%s'%s"),
                                  m->strLocationFull.c_str(),
                                  pTarget->m->strLocationFull.c_str(),
                                  i_vdError(aVRC).c_str());
        }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

    ErrorInfoKeeper eik;
    MultiResult mrc(hrcTmp);
    HRESULT hrc2;

    std::set<ComObjPtr<Medium> > pMediaForNotify;
    std::map<Guid, DeviceType_T> uIdsForNotify;

    if (SUCCEEDED(mrc))
    {
        /* all media but the target were successfully deleted by
         * VDMerge; reparent the last one and uninitialize deleted media. */

        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        if (task.mfMergeForward)
        {
            /* first, unregister the target since it may become a base
             * medium which needs re-registration */
            hrc2 = m->pVirtualBox->i_unregisterMedium(pTarget);
            AssertComRC(hrc2);

            /* then, reparent it and disconnect the deleted branch at both ends
             * (chain->parent() is source's parent). Depth check above. */
            pTarget->i_deparent();
            pTarget->i_setParent(task.mParentForTarget);
            if (task.mParentForTarget)
            {
                i_deparent();
                if (task.NotifyAboutChanges())
                    pMediaForNotify.insert(task.mParentForTarget);
            }

            /* then, register again */
            ComObjPtr<Medium> pMedium;
            hrc2 = m->pVirtualBox->i_registerMedium(pTarget, &pMedium, treeLock);
            AssertComRC(hrc2);
        }
        else
        {
            Assert(pTarget->i_getChildren().size() == 1);
            Medium *targetChild = pTarget->i_getChildren().front();

            /* disconnect the deleted branch at the elder end */
            targetChild->i_deparent();

            /* reparent source's children and disconnect the deleted
             * branch at the younger end */
            if (task.mpChildrenToReparent)
            {
                /* obey {parent,child} lock order */
                AutoWriteLock sourceLock(this COMMA_LOCKVAL_SRC_POS);

                MediumLockList::Base::iterator childrenBegin = task.mpChildrenToReparent->GetBegin();
                MediumLockList::Base::iterator childrenEnd = task.mpChildrenToReparent->GetEnd();
                for (MediumLockList::Base::iterator it = childrenBegin;
                     it != childrenEnd;
                     ++it)
                {
                    Medium *pMedium = it->GetMedium();
                    AutoWriteLock childLock(pMedium COMMA_LOCKVAL_SRC_POS);

                    pMedium->i_deparent();  // removes pMedium from source
                    // no depth check, reduces depth
                    pMedium->i_setParent(pTarget);

                    if (task.NotifyAboutChanges())
                        pMediaForNotify.insert(pMedium);
                }
            }
            pMediaForNotify.insert(pTarget);
        }

        /* unregister and uninitialize all media removed by the merge */
        MediumLockList::Base::iterator lockListBegin =
            task.mpMediumLockList->GetBegin();
        MediumLockList::Base::iterator lockListEnd =
            task.mpMediumLockList->GetEnd();
        for (MediumLockList::Base::iterator it = lockListBegin;
             it != lockListEnd;
             )
        {
            MediumLock &mediumLock = *it;
            /* Create a real copy of the medium pointer, as the medium
             * lock deletion below would invalidate the referenced object. */
            const ComObjPtr<Medium> pMedium = mediumLock.GetMedium();

            /* The target and all media not merged (readonly) are skipped */
            if (   pMedium == pTarget
                || pMedium->m->state == MediumState_LockedRead)
            {
                ++it;
                continue;
            }

            uIdsForNotify[pMedium->i_getId()] = pMedium->i_getDeviceType();
            hrc2 = pMedium->m->pVirtualBox->i_unregisterMedium(pMedium);
            AssertComRC(hrc2);

            /* now, uninitialize the deleted medium (note that
             * due to the Deleting state, uninit() will not touch
             * the parent-child relationship so we need to
             * uninitialize each disk individually) */

            /* note that the operation initiator medium (which is
             * normally also the source medium) is a special case
             * -- there is one more caller added by Task to it which
             * we must release. Also, if we are in sync mode, the
             * caller may still hold an AutoCaller instance for it
             * and therefore we cannot uninit() it (it's therefore
             * the caller's responsibility) */
            if (pMedium == this)
            {
                Assert(i_getChildren().size() == 0);
                Assert(m->backRefs.size() == 0);
                task.mMediumCaller.release();
            }

            /* Delete the medium lock list entry, which also releases the
             * caller added by MergeChain before uninit() and updates the
             * iterator to point to the right place. */
            hrc2 = task.mpMediumLockList->RemoveByIterator(it);
            AssertComRC(hrc2);

            if (task.isAsync() || pMedium != this)
            {
                treeLock.release();
                pMedium->uninit();
                treeLock.acquire();
            }
        }
    }

    i_markRegistriesModified();
    if (task.isAsync())
    {
        // in asynchronous mode, save settings now
        eik.restore();
        m->pVirtualBox->i_saveModifiedRegistries();
        eik.fetch();
    }

    if (FAILED(mrc))
    {
        /* Here we come if either VDMerge() failed (in which case we
         * assume that it tried to do everything to make a further
         * retry possible -- e.g. not deleted intermediate media
         * and so on) or VirtualBox::saveRegistries() failed (where we
         * should have the original tree but with intermediate storage
         * units deleted by VDMerge()). We have to only restore states
         * (through the MergeChain dtor) unless we are run synchronously
         * in which case it's the responsibility of the caller as stated
         * in the mergeTo() docs. The latter also implies that we
         * don't own the merge chain, so release it in this case. */
        if (task.isAsync())
            i_cancelMergeTo(task.mpChildrenToReparent, task.mpMediumLockList);
    }
    else if (task.NotifyAboutChanges())
    {
        for (std::set<ComObjPtr<Medium> >::const_iterator it = pMediaForNotify.begin();
             it != pMediaForNotify.end();
             ++it)
        {
            if (it->isNotNull())
                m->pVirtualBox->i_onMediumConfigChanged(*it);
        }
        for (std::map<Guid, DeviceType_T>::const_iterator it = uIdsForNotify.begin();
             it != uIdsForNotify.end();
             ++it)
        {
            m->pVirtualBox->i_onMediumRegistered(it->first, it->second, FALSE);
        }
    }

    return mrc;
}

/**
 * Implementation code for the "clone" task.
 *
 * This only gets started from Medium::CloneTo() and always runs asynchronously.
 * As a result, we always save the VirtualBox.xml file when we're done here.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskCloneHandler(Medium::CloneTask &task)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    HRESULT hrcTmp = S_OK;

    const ComObjPtr<Medium> &pTarget = task.mTarget;
    const ComObjPtr<Medium> &pParent = task.mParent;

    bool fCreatingTarget = false;
    bool cloningWithinList = false;
    unsigned uTargetIdx = VD_LAST_IMAGE;

    uint64_t size = 0, logicalSize = 0;
    MediumVariant_T variant = MediumVariant_Standard;
    bool fGenerateUuid = false;

    try
    {
        if (!pParent.isNull())
        {

            if (pParent->i_getDepth() >= SETTINGS_MEDIUM_DEPTH_MAX)
            {
                AutoReadLock plock(pParent COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Cannot clone image for medium '%s', because it exceeds the medium tree depth limit. Please merge some images which you no longer need"),
                               pParent->m->strLocationFull.c_str());
            }
        }

        /* Lock all in {parent,child} order. The lock is also used as a
         * signal from the task initiator (which releases it only after
         * RTThreadCreate()) that we can start the job. */
        AutoMultiWriteLock3 thisLock(this, pTarget, pParent COMMA_LOCKVAL_SRC_POS);

        fCreatingTarget = pTarget->m->state == MediumState_Creating;

        /* The object may request a specific UUID (through a special form of
         * the moveTo() argument). Otherwise we have to generate it */
        Guid targetId = pTarget->m->id;

        fGenerateUuid = targetId.isZero();
        if (fGenerateUuid)
        {
            targetId.create();
            /* VirtualBox::registerMedium() will need UUID */
            unconst(pTarget->m->id) = targetId;
        }

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /* Open all media in the source chain. */
            MediumLockList::Base::const_iterator sourceListBegin =
                task.mpSourceMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator sourceListEnd =
                task.mpSourceMediumLockList->GetEnd();
            unsigned i=0;
            for (MediumLockList::Base::const_iterator it = sourceListBegin;
                 it != sourceListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                if (pMedium == pTarget)
                {
                    cloningWithinList = true;
                    uTargetIdx = i;
                    AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);
                    Assert(pMedium->m->state == MediumState_LockedWrite);
                    unsigned uOpenFlags = VD_OPEN_FLAGS_NORMAL;
                    if (pMedium->m->type == MediumType_Shareable)
                        uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;
                    /* Open target in appropriate mode. */
                    vrc = VDOpen(hdd,
                                 pMedium->m->strFormat.c_str(),
                                 pMedium->m->strLocationFull.c_str(),
                                 uOpenFlags | m->uOpenFlagsDef,
                                 pMedium->m->vdImageIfaces);
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Could not open the medium storage unit '%s'%s"),
                                           pMedium->m->strLocationFull.c_str(),
                                           i_vdError(vrc).c_str());
                }
                i++;
                if (!cloningWithinList || pMedium != pTarget)
                {
                    AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);
                    /* sanity check */
                    Assert(pMedium->m->state == MediumState_LockedRead);
                    /** Open all media in read-only mode. */
                    vrc = VDOpen(hdd,
                                 pMedium->m->strFormat.c_str(),
                                 pMedium->m->strLocationFull.c_str(),
                                 VD_OPEN_FLAGS_READONLY | m->uOpenFlagsDef,
                                 pMedium->m->vdImageIfaces);
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Could not open the medium storage unit '%s'%s"),
                                           pMedium->m->strLocationFull.c_str(),
                                           i_vdError(vrc).c_str());
                }
            }

            Utf8Str targetFormat(pTarget->m->strFormat);
            Utf8Str targetLocation(pTarget->m->strLocationFull);
            uint64_t capabilities = pTarget->m->formatObj->i_getCapabilities();

            Assert(   pTarget->m->state == MediumState_Creating
                   || pTarget->m->state == MediumState_LockedWrite);
            Assert(m->state == MediumState_LockedRead);
            Assert(   pParent.isNull()
                   || pParent->m->state == MediumState_LockedRead);

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            /* ensure the target directory exists */
            if (capabilities & MediumFormatCapabilities_File)
            {
                HRESULT hrc = VirtualBox::i_ensureFilePathExists(targetLocation,
                                                                 !(task.mVariant & MediumVariant_NoCreateDir) /* fCreate */);
                if (FAILED(hrc))
                    throw hrc;
            }

            PVDISK targetHdd;
            vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &targetHdd);
            ComAssertRCThrow(vrc, E_FAIL);

            try
            {
                if (!cloningWithinList)
                {
                    /* Open all media in the target chain. */
                    MediumLockList::Base::const_iterator targetListBegin =
                        task.mpTargetMediumLockList->GetBegin();
                    MediumLockList::Base::const_iterator targetListEnd =
                        task.mpTargetMediumLockList->GetEnd();
                    for (MediumLockList::Base::const_iterator it = targetListBegin;
                         it != targetListEnd;
                         ++it)
                    {
                        const MediumLock &mediumLock = *it;
                        const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                        /* If the target medium is not created yet there's no
                        * reason to open it. */
                        if (pMedium == pTarget && fCreatingTarget)
                            continue;
                        AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);
                        /* sanity check */
                        Assert(    pMedium->m->state == MediumState_LockedRead
                               || pMedium->m->state == MediumState_LockedWrite);
                        unsigned uOpenFlags = VD_OPEN_FLAGS_NORMAL;
                        if (pMedium->m->state != MediumState_LockedWrite)
                            uOpenFlags = VD_OPEN_FLAGS_READONLY;
                        if (pMedium->m->type == MediumType_Shareable)
                            uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;
                        /* Open all media in appropriate mode. */
                        vrc = VDOpen(targetHdd,
                                     pMedium->m->strFormat.c_str(),
                                     pMedium->m->strLocationFull.c_str(),
                                     uOpenFlags | m->uOpenFlagsDef,
                                     pMedium->m->vdImageIfaces);
                        if (RT_FAILURE(vrc))
                            throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                               tr("Could not open the medium storage unit '%s'%s"),
                                               pMedium->m->strLocationFull.c_str(),
                                               i_vdError(vrc).c_str());
                    }
                    /* target isn't locked, but no changing data is accessed */
                    if (task.midxSrcImageSame == UINT32_MAX)
                    {
                        vrc = VDCopy(hdd,
                                     VD_LAST_IMAGE,
                                     targetHdd,
                                     targetFormat.c_str(),
                                     (fCreatingTarget) ? targetLocation.c_str() : (char *)NULL,
                                     false /* fMoveByRename */,
                                     task.mTargetLogicalSize /* cbSize */,
                                     task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                                     targetId.raw(),
                                     VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                                     NULL /* pVDIfsOperation */,
                                     pTarget->m->vdImageIfaces,
                                     task.mVDOperationIfaces);
                    }
                    else
                    {
                        vrc = VDCopyEx(hdd,
                                       VD_LAST_IMAGE,
                                       targetHdd,
                                       VD_LAST_IMAGE,
                                       targetFormat.c_str(),
                                       (fCreatingTarget) ? targetLocation.c_str() : (char *)NULL,
                                       false /* fMoveByRename */,
                                       task.mTargetLogicalSize /* cbSize */,
                                       task.midxSrcImageSame,
                                       task.midxDstImageSame,
                                       task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                                       targetId.raw(),
                                       VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                                       NULL /* pVDIfsOperation */,
                                       pTarget->m->vdImageIfaces,
                                       task.mVDOperationIfaces);
                    }
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Could not create the clone medium '%s'%s"),
                                           targetLocation.c_str(), i_vdError(vrc).c_str());

                    size = VDGetFileSize(targetHdd, VD_LAST_IMAGE);
                    logicalSize = VDGetSize(targetHdd, VD_LAST_IMAGE);
                    unsigned uImageFlags;
                    vrc = VDGetImageFlags(targetHdd, 0, &uImageFlags);
                    if (RT_SUCCESS(vrc))
                        variant = (MediumVariant_T)uImageFlags;
                }
                else //cloningWithinList - only use source chain
                {
                    if (task.midxSrcImageSame == UINT32_MAX)
                    {
                        vrc = VDCopyEx(hdd,
                                       VD_LAST_IMAGE,
                                       hdd,
                                       uTargetIdx,
                                       targetFormat.c_str(),
                                       (fCreatingTarget) ? targetLocation.c_str() : (char *)NULL,
                                       false /* fMoveByRename */,
                                       task.mTargetLogicalSize /* cbSize */,
                                       VD_IMAGE_CONTENT_UNKNOWN,
                                       VD_IMAGE_CONTENT_UNKNOWN,
                                       task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                                       targetId.raw(),
                                       VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                                       NULL /* pVDIfsOperation */,
                                       pTarget->m->vdImageIfaces,
                                       task.mVDOperationIfaces);
                    }
                    else
                    {
                        vrc = VDCopyEx(hdd,
                                       VD_LAST_IMAGE,
                                       hdd,
                                       uTargetIdx,
                                       targetFormat.c_str(),
                                       (fCreatingTarget) ? targetLocation.c_str() : (char *)NULL,
                                       false /* fMoveByRename */,
                                       task.mTargetLogicalSize /* cbSize */,
                                       task.midxSrcImageSame,
                                       task.midxDstImageSame,
                                       task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                                       targetId.raw(),
                                       VD_OPEN_FLAGS_NORMAL | m->uOpenFlagsDef,
                                       NULL /* pVDIfsOperation */,
                                       pTarget->m->vdImageIfaces,
                                       task.mVDOperationIfaces);
                    }
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Could not create the clone medium '%s'%s"),
                                           targetLocation.c_str(), i_vdError(vrc).c_str());

                    size = VDGetFileSize(hdd, uTargetIdx);
                    logicalSize = VDGetSize(hdd, uTargetIdx);
                    unsigned uImageFlags;
                    vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
                    if (RT_SUCCESS(vrc))
                        variant = (MediumVariant_T)uImageFlags;

                }
            }
            catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

            VDDestroy(targetHdd);
        }
        catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

    ErrorInfoKeeper eik;
    MultiResult mrc(hrcTmp);

    /* Only do the parent changes for newly created media. */
    if (SUCCEEDED(mrc) && fCreatingTarget)
    {
        /* we set m->pParent & children() */
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        Assert(pTarget->m->pParent.isNull());

        if (pParent)
        {
            /* Associate the clone with the parent and deassociate
             * from VirtualBox. Depth check above. */
            pTarget->i_setParent(pParent);

            /* register with mVirtualBox as the last step and move to
             * Created state only on success (leaving an orphan file is
             * better than breaking media registry consistency) */
            eik.restore();
            ComObjPtr<Medium> pMedium;
            mrc = pParent->m->pVirtualBox->i_registerMedium(pTarget, &pMedium,
                                                            treeLock);
            Assert(   FAILED(mrc)
                   || pTarget == pMedium);
            eik.fetch();

            if (FAILED(mrc))
                /* break parent association on failure to register */
                pTarget->i_deparent();     // removes target from parent
        }
        else
        {
            /* just register  */
            eik.restore();
            ComObjPtr<Medium> pMedium;
            mrc = m->pVirtualBox->i_registerMedium(pTarget, &pMedium,
                                                   treeLock);
            Assert(   FAILED(mrc)
                   || pTarget == pMedium);
            eik.fetch();
        }
    }

    if (fCreatingTarget)
    {
        AutoWriteLock mLock(pTarget COMMA_LOCKVAL_SRC_POS);

        if (SUCCEEDED(mrc))
        {
            pTarget->m->state = MediumState_Created;

            pTarget->m->size = size;
            pTarget->m->logicalSize = logicalSize;
            pTarget->m->variant = variant;
        }
        else
        {
            /* back to NotCreated on failure */
            pTarget->m->state = MediumState_NotCreated;

            /* reset UUID to prevent it from being reused next time */
            if (fGenerateUuid)
                unconst(pTarget->m->id).clear();
        }
    }

    /* Copy any filter related settings over to the target. */
    if (SUCCEEDED(mrc))
    {
        /* Copy any filter related settings over. */
        ComObjPtr<Medium> pBase = i_getBase();
        ComObjPtr<Medium> pTargetBase = pTarget->i_getBase();
        std::vector<com::Utf8Str> aFilterPropNames;
        std::vector<com::Utf8Str> aFilterPropValues;
        mrc = pBase->i_getFilterProperties(aFilterPropNames, aFilterPropValues);
        if (SUCCEEDED(mrc))
        {
            /* Go through the properties and add them to the target medium. */
            for (unsigned idx = 0; idx < aFilterPropNames.size(); idx++)
            {
                mrc = pTargetBase->i_setPropertyDirect(aFilterPropNames[idx], aFilterPropValues[idx]);
                if (FAILED(mrc)) break;
            }

            // now, at the end of this task (always asynchronous), save the settings
            if (SUCCEEDED(mrc))
            {
                // save the settings
                i_markRegistriesModified();
                /* collect multiple errors */
                eik.restore();
                m->pVirtualBox->i_saveModifiedRegistries();
                eik.fetch();

                if (task.NotifyAboutChanges())
                {
                    if (!fCreatingTarget)
                    {
                        if (!aFilterPropNames.empty())
                            m->pVirtualBox->i_onMediumConfigChanged(pTargetBase);
                        if (pParent)
                            m->pVirtualBox->i_onMediumConfigChanged(pParent);
                    }
                    else
                    {
                        m->pVirtualBox->i_onMediumRegistered(pTarget->i_getId(), pTarget->i_getDeviceType(), TRUE);
                    }
                }
            }
        }
    }

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the source chain. */

    /* Make sure the source chain is released early. It could happen
     * that we get a deadlock in Appliance::Import when Medium::Close
     * is called & the source chain is released at the same time. */
    task.mpSourceMediumLockList->Clear();

    return mrc;
}

/**
 * Implementation code for the "move" task.
 *
 * This only gets started from Medium::MoveTo() and always
 * runs asynchronously.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskMoveHandler(Medium::MoveTask &task)
{
    LogFlowFuncEnter();
    HRESULT hrcOut = S_OK;

    /* pTarget is equal "this" in our case */
    const ComObjPtr<Medium> &pTarget = task.mMedium;

    /*
     * it's exactly moving, not cloning
     */
    if (!i_isMoveOperation(pTarget))
    {
        LogFlowFunc(("LEAVE: hrc=VBOX_E_FILE_ERROR (early)\n"));
        return setError(VBOX_E_FILE_ERROR,
                        tr("Wrong preconditions for moving the medium %s"),
                        pTarget->m->strLocationFull.c_str());
    }

    try
    {
        /* Lock all in {parent,child} order. The lock is also used as a
         * signal from the task initiator (which releases it only after
         * RTThreadCreate()) that we can start the job. */

        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /* Open all media in the source chain. */
            MediumLockList::Base::const_iterator sourceListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator sourceListEnd =
                task.mpMediumLockList->GetEnd();
            for (MediumLockList::Base::const_iterator it = sourceListBegin;
                 it != sourceListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                AutoWriteLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /* sanity check */
                Assert(pMedium->m->state == MediumState_LockedWrite);

                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             VD_OPEN_FLAGS_NORMAL,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());
            }

            /* we can directly use pTarget->m->"variables" but for better reading we use local copies */
            Guid targetId = pTarget->m->id;
            Utf8Str targetFormat(pTarget->m->strFormat);
            uint64_t targetCapabilities = pTarget->m->formatObj->i_getCapabilities();

            /*
             * change target location
             * m->strNewLocationFull has been set already together with m->fMoveThisMedium in
             * i_preparationForMoving()
             */
            Utf8Str targetLocation = i_getNewLocationForMoving();

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            /* ensure the target directory exists */
            if (targetCapabilities & MediumFormatCapabilities_File)
            {
                HRESULT hrc = VirtualBox::i_ensureFilePathExists(targetLocation,
                                                                 !(task.mVariant & MediumVariant_NoCreateDir) /* fCreate */);
                if (FAILED(hrc))
                    throw hrc;
            }

            try
            {
                vrc = VDCopy(hdd,
                             VD_LAST_IMAGE,
                             hdd,
                             targetFormat.c_str(),
                             targetLocation.c_str(),
                             true /* fMoveByRename */,
                             0 /* cbSize */,
                             VD_IMAGE_FLAGS_NONE,
                             targetId.raw(),
                             VD_OPEN_FLAGS_NORMAL,
                             NULL /* pVDIfsOperation */,
                             pTarget->m->vdImageIfaces,
                             task.mVDOperationIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not move medium '%s'%s"),
                                       targetLocation.c_str(), i_vdError(vrc).c_str());

                /*
                 * set current location, because VDCopy\VDCopyEx doesn't do it.
                 * also reset moving flag
                 */
                i_resetMoveOperationData();
                m->strLocationFull = targetLocation;

            }
            catch (HRESULT hrcXcpt) { hrcOut = hrcXcpt; }

        }
        catch (HRESULT hrcXcpt) { hrcOut = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrcOut = hrcXcpt; }

    ErrorInfoKeeper eik;
    MultiResult mrc(hrcOut);

    // now, at the end of this task (always asynchronous), save the settings
    if (SUCCEEDED(mrc))
    {
        // save the settings
        i_markRegistriesModified();
        /* collect multiple errors */
        eik.restore();
        m->pVirtualBox->i_saveModifiedRegistries();
        eik.fetch();
    }

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the source chain. */

    task.mpMediumLockList->Clear();

    if (task.NotifyAboutChanges() && SUCCEEDED(mrc))
        m->pVirtualBox->i_onMediumConfigChanged(this);

    LogFlowFunc(("LEAVE: mrc=%Rhrc\n", (HRESULT)mrc));
    return mrc;
}

/**
 * Implementation code for the "delete" task.
 *
 * This task always gets started from Medium::deleteStorage() and can run
 * synchronously or asynchronously depending on the "wait" parameter passed to
 * that function.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskDeleteHandler(Medium::DeleteTask &task)
{
    NOREF(task);
    HRESULT hrc = S_OK;

    try
    {
        /* The lock is also used as a signal from the task initiator (which
         * releases it only after RTThreadCreate()) that we can start the job */
        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        Utf8Str format(m->strFormat);
        Utf8Str location(m->strLocationFull);

        /* unlock before the potentially lengthy operation */
        Assert(m->state == MediumState_Deleting);
        thisLock.release();

        try
        {
            vrc = VDOpen(hdd,
                         format.c_str(),
                         location.c_str(),
                         VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO | m->uOpenFlagsDef,
                         m->vdImageIfaces);
            if (RT_SUCCESS(vrc))
                vrc = VDClose(hdd, true /* fDelete */);

            if (RT_FAILURE(vrc) && vrc != VERR_FILE_NOT_FOUND)
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not delete the medium storage unit '%s'%s"),
                                   location.c_str(), i_vdError(vrc).c_str());

        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    /* go to the NotCreated state even on failure since the storage
     * may have been already partially deleted and cannot be used any
     * more. One will be able to manually re-open the storage if really
     * needed to re-register it. */
    m->state = MediumState_NotCreated;

    /* Reset UUID to prevent Create* from reusing it again */
    com::Guid uOldId = m->id;
    unconst(m->id).clear();

    if (task.NotifyAboutChanges() && SUCCEEDED(hrc))
    {
        if (m->pParent.isNotNull())
            m->pVirtualBox->i_onMediumConfigChanged(m->pParent);
        m->pVirtualBox->i_onMediumRegistered(uOldId, m->devType, FALSE);
    }

    return hrc;
}

/**
 * Implementation code for the "reset" task.
 *
 * This always gets started asynchronously from Medium::Reset().
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskResetHandler(Medium::ResetTask &task)
{
    HRESULT hrc = S_OK;

    uint64_t size = 0, logicalSize = 0;
    MediumVariant_T variant = MediumVariant_Standard;

    try
    {
        /* The lock is also used as a signal from the task initiator (which
         * releases it only after RTThreadCreate()) that we can start the job */
        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

        /// @todo Below we use a pair of delete/create operations to reset
        /// the diff contents but the most efficient way will of course be
        /// to add a VDResetDiff() API call

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        Guid id = m->id;
        Utf8Str format(m->strFormat);
        Utf8Str location(m->strLocationFull);

        Medium *pParent = m->pParent;
        Guid parentId = pParent->m->id;
        Utf8Str parentFormat(pParent->m->strFormat);
        Utf8Str parentLocation(pParent->m->strLocationFull);

        Assert(m->state == MediumState_LockedWrite);

        /* unlock before the potentially lengthy operation */
        thisLock.release();

        try
        {
            /* Open all media in the target chain but the last. */
            MediumLockList::Base::const_iterator targetListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator targetListEnd =
                task.mpMediumLockList->GetEnd();
            for (MediumLockList::Base::const_iterator it = targetListBegin;
                 it != targetListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();

                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /* sanity check, "this" is checked above */
                Assert(   pMedium == this
                       || pMedium->m->state == MediumState_LockedRead);

                /* Open all media in appropriate mode. */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             VD_OPEN_FLAGS_READONLY | m->uOpenFlagsDef,
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());

                /* Done when we hit the media which should be reset */
                if (pMedium == this)
                    break;
            }

            /* first, delete the storage unit */
            vrc = VDClose(hdd, true /* fDelete */);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not delete the medium storage unit '%s'%s"),
                                   location.c_str(), i_vdError(vrc).c_str());

            /* next, create it again */
            vrc = VDOpen(hdd,
                         parentFormat.c_str(),
                         parentLocation.c_str(),
                         VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO | m->uOpenFlagsDef,
                         m->vdImageIfaces);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not open the medium storage unit '%s'%s"),
                                   parentLocation.c_str(), i_vdError(vrc).c_str());

            vrc = VDCreateDiff(hdd,
                               format.c_str(),
                               location.c_str(),
                               /// @todo use the same medium variant as before
                               VD_IMAGE_FLAGS_NONE,
                               NULL,
                               id.raw(),
                               parentId.raw(),
                               VD_OPEN_FLAGS_NORMAL,
                               m->vdImageIfaces,
                               task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_VD_INVALID_TYPE)
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Parameters for creating the differencing medium storage unit '%s' are invalid%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
                else
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not create the differencing medium storage unit '%s'%s"),
                                       location.c_str(), i_vdError(vrc).c_str());
            }

            size = VDGetFileSize(hdd, VD_LAST_IMAGE);
            logicalSize = VDGetSize(hdd, VD_LAST_IMAGE);
            unsigned uImageFlags;
            vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
            if (RT_SUCCESS(vrc))
                variant = (MediumVariant_T)uImageFlags;
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    m->size = size;
    m->logicalSize = logicalSize;
    m->variant = variant;

    if (task.NotifyAboutChanges() && SUCCEEDED(hrc))
        m->pVirtualBox->i_onMediumConfigChanged(this);

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the media chain. */

    return hrc;
}

/**
 * Implementation code for the "compact" task.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskCompactHandler(Medium::CompactTask &task)
{
    HRESULT hrc = S_OK;

    /* Lock all in {parent,child} order. The lock is also used as a
     * signal from the task initiator (which releases it only after
     * RTThreadCreate()) that we can start the job. */
    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    try
    {
        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /* Open all media in the chain. */
            MediumLockList::Base::const_iterator mediumListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator mediumListEnd =
                task.mpMediumLockList->GetEnd();
            MediumLockList::Base::const_iterator mediumListLast =
                mediumListEnd;
            --mediumListLast;
            for (MediumLockList::Base::const_iterator it = mediumListBegin;
                 it != mediumListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /* sanity check */
                if (it == mediumListLast)
                    Assert(pMedium->m->state == MediumState_LockedWrite);
                else
                    Assert(pMedium->m->state == MediumState_LockedRead);

                /* Open all media but last in read-only mode. Do not handle
                 * shareable media, as compaction and sharing are mutually
                 * exclusive. */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             m->uOpenFlagsDef | (it == mediumListLast ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY),
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());
            }

            Assert(m->state == MediumState_LockedWrite);

            Utf8Str location(m->strLocationFull);

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            vrc = VDCompact(hdd, VD_LAST_IMAGE, task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_NOT_SUPPORTED)
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Compacting is not yet supported for medium '%s'"),
                                       location.c_str());
                else if (vrc == VERR_NOT_IMPLEMENTED)
                    throw setErrorBoth(E_NOTIMPL, vrc,
                                       tr("Compacting is not implemented, medium '%s'"),
                                       location.c_str());
                else
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not compact medium '%s'%s"),
                                       location.c_str(),
                                       i_vdError(vrc).c_str());
            }
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (task.NotifyAboutChanges() && SUCCEEDED(hrc))
        m->pVirtualBox->i_onMediumConfigChanged(this);

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the media chain. */

    return hrc;
}

/**
 * Implementation code for the "resize" task.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskResizeHandler(Medium::ResizeTask &task)
{
    HRESULT hrc = S_OK;

    uint64_t size = 0, logicalSize = 0;

    try
    {
        /* The lock is also used as a signal from the task initiator (which
         * releases it only after RTThreadCreate()) that we can start the job */
        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /* Open all media in the chain. */
            MediumLockList::Base::const_iterator mediumListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator mediumListEnd =
                task.mpMediumLockList->GetEnd();
            MediumLockList::Base::const_iterator mediumListLast =
                mediumListEnd;
            --mediumListLast;
            for (MediumLockList::Base::const_iterator it = mediumListBegin;
                 it != mediumListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                /* sanity check */
                if (it == mediumListLast)
                    Assert(pMedium->m->state == MediumState_LockedWrite);
                else
                    Assert(pMedium->m->state == MediumState_LockedRead ||
                           // Allow resize the target image during mergeTo in case
                           // of direction from parent to child because all intermediate
                           // images are marked to MediumState_Deleting and will be
                           // destroyed after successful merge
                           pMedium->m->state == MediumState_Deleting);

                /* Open all media but last in read-only mode. Do not handle
                 * shareable media, as compaction and sharing are mutually
                 * exclusive. */
                vrc = VDOpen(hdd,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             m->uOpenFlagsDef | (it == mediumListLast ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY),
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());
            }

            Assert(m->state == MediumState_LockedWrite);

            Utf8Str location(m->strLocationFull);

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            VDGEOMETRY geo = {0, 0, 0}; /* auto */
            vrc = VDResize(hdd, task.mSize, &geo, &geo, task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_VD_SHRINK_NOT_SUPPORTED)
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Shrinking is not yet supported for medium '%s'"),
                                       location.c_str());
                if (vrc == VERR_NOT_SUPPORTED)
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Resizing to new size %llu is not yet supported for medium '%s'"),
                                       task.mSize, location.c_str());
                else if (vrc == VERR_NOT_IMPLEMENTED)
                    throw setErrorBoth(E_NOTIMPL, vrc,
                                       tr("Resizing is not implemented, medium '%s'"),
                                       location.c_str());
                else
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not resize medium '%s'%s"),
                                       location.c_str(),
                                       i_vdError(vrc).c_str());
            }
            size = VDGetFileSize(hdd, VD_LAST_IMAGE);
            logicalSize = VDGetSize(hdd, VD_LAST_IMAGE);
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);
        m->size = size;
        m->logicalSize = logicalSize;

        if (task.NotifyAboutChanges())
            m->pVirtualBox->i_onMediumConfigChanged(this);
    }

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the media chain. */

    return hrc;
}

/**
 * Implementation code for the "import" task.
 *
 * This only gets started from Medium::importFile() and always runs
 * asynchronously. It potentially touches the media registry, so we
 * always save the VirtualBox.xml file when we're done here.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskImportHandler(Medium::ImportTask &task)
{
    /** @todo r=klaus The code below needs to be double checked with regard
     * to lock order violations, it probably causes lock order issues related
     * to the AutoCaller usage. */
    HRESULT hrcTmp = S_OK;

    const ComObjPtr<Medium> &pParent = task.mParent;

    bool fCreatingTarget = false;

    uint64_t size = 0, logicalSize = 0;
    MediumVariant_T variant = MediumVariant_Standard;
    bool fGenerateUuid = false;

    try
    {
        if (!pParent.isNull())
            if (pParent->i_getDepth() >= SETTINGS_MEDIUM_DEPTH_MAX)
            {
                AutoReadLock plock(pParent COMMA_LOCKVAL_SRC_POS);
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("Cannot import image for medium '%s', because it exceeds the medium tree depth limit. Please merge some images which you no longer need"),
                               pParent->m->strLocationFull.c_str());
            }

        /* Lock all in {parent,child} order. The lock is also used as a
         * signal from the task initiator (which releases it only after
         * RTThreadCreate()) that we can start the job. */
        AutoMultiWriteLock2 thisLock(this, pParent COMMA_LOCKVAL_SRC_POS);

        fCreatingTarget = m->state == MediumState_Creating;

        /* The object may request a specific UUID (through a special form of
         * the moveTo() argument). Otherwise we have to generate it */
        Guid targetId = m->id;

        fGenerateUuid = targetId.isZero();
        if (fGenerateUuid)
        {
            targetId.create();
            /* VirtualBox::i_registerMedium() will need UUID */
            unconst(m->id) = targetId;
        }


        PVDISK hdd;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /* Open source medium. */
            vrc = VDOpen(hdd,
                         task.mFormat->i_getId().c_str(),
                         task.mFilename.c_str(),
                         VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_SEQUENTIAL | m->uOpenFlagsDef,
                         task.mVDImageIfaces);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not open the medium storage unit '%s'%s"),
                                   task.mFilename.c_str(),
                                   i_vdError(vrc).c_str());

            Utf8Str targetFormat(m->strFormat);
            Utf8Str targetLocation(m->strLocationFull);
            uint64_t capabilities = task.mFormat->i_getCapabilities();

            Assert(   m->state == MediumState_Creating
                   || m->state == MediumState_LockedWrite);
            Assert(   pParent.isNull()
                   || pParent->m->state == MediumState_LockedRead);

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            /* ensure the target directory exists */
            if (capabilities & MediumFormatCapabilities_File)
            {
                HRESULT hrc = VirtualBox::i_ensureFilePathExists(targetLocation,
                                                                 !(task.mVariant & MediumVariant_NoCreateDir) /* fCreate */);
                if (FAILED(hrc))
                    throw hrc;
            }

            PVDISK targetHdd;
            vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &targetHdd);
            ComAssertRCThrow(vrc, E_FAIL);

            try
            {
                /* Open all media in the target chain. */
                MediumLockList::Base::const_iterator targetListBegin =
                    task.mpTargetMediumLockList->GetBegin();
                MediumLockList::Base::const_iterator targetListEnd =
                    task.mpTargetMediumLockList->GetEnd();
                for (MediumLockList::Base::const_iterator it = targetListBegin;
                     it != targetListEnd;
                     ++it)
                {
                    const MediumLock &mediumLock = *it;
                    const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();

                    /* If the target medium is not created yet there's no
                     * reason to open it. */
                    if (pMedium == this && fCreatingTarget)
                        continue;

                    AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                    /* sanity check */
                    Assert(    pMedium->m->state == MediumState_LockedRead
                            || pMedium->m->state == MediumState_LockedWrite);

                    unsigned uOpenFlags = VD_OPEN_FLAGS_NORMAL;
                    if (pMedium->m->state != MediumState_LockedWrite)
                        uOpenFlags = VD_OPEN_FLAGS_READONLY;
                    if (pMedium->m->type == MediumType_Shareable)
                        uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;

                    /* Open all media in appropriate mode. */
                    vrc = VDOpen(targetHdd,
                                 pMedium->m->strFormat.c_str(),
                                 pMedium->m->strLocationFull.c_str(),
                                 uOpenFlags | m->uOpenFlagsDef,
                                 pMedium->m->vdImageIfaces);
                    if (RT_FAILURE(vrc))
                        throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Could not open the medium storage unit '%s'%s"),
                                           pMedium->m->strLocationFull.c_str(),
                                           i_vdError(vrc).c_str());
                }

                vrc = VDCopy(hdd,
                             VD_LAST_IMAGE,
                             targetHdd,
                             targetFormat.c_str(),
                             (fCreatingTarget) ? targetLocation.c_str() : (char *)NULL,
                             false /* fMoveByRename */,
                             0 /* cbSize */,
                             task.mVariant & ~(MediumVariant_NoCreateDir | MediumVariant_Formatted | MediumVariant_VmdkESX | MediumVariant_VmdkRawDisk),
                             targetId.raw(),
                             VD_OPEN_FLAGS_NORMAL,
                             NULL /* pVDIfsOperation */,
                             m->vdImageIfaces,
                             task.mVDOperationIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not create the imported medium '%s'%s"),
                                       targetLocation.c_str(), i_vdError(vrc).c_str());

                size = VDGetFileSize(targetHdd, VD_LAST_IMAGE);
                logicalSize = VDGetSize(targetHdd, VD_LAST_IMAGE);
                unsigned uImageFlags;
                vrc = VDGetImageFlags(targetHdd, 0, &uImageFlags);
                if (RT_SUCCESS(vrc))
                    variant = (MediumVariant_T)uImageFlags;
            }
            catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

            VDDestroy(targetHdd);
        }
        catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

        VDDestroy(hdd);
    }
    catch (HRESULT hrcXcpt) { hrcTmp = hrcXcpt; }

    ErrorInfoKeeper eik;
    MultiResult mrc(hrcTmp);

    /* Only do the parent changes for newly created media. */
    if (SUCCEEDED(mrc) && fCreatingTarget)
    {
        /* we set m->pParent & children() */
        AutoWriteLock treeLock(m->pVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        Assert(m->pParent.isNull());

        if (pParent)
        {
            /* Associate the imported medium with the parent and deassociate
             * from VirtualBox. Depth check above. */
            i_setParent(pParent);

            /* register with mVirtualBox as the last step and move to
             * Created state only on success (leaving an orphan file is
             * better than breaking media registry consistency) */
            eik.restore();
            ComObjPtr<Medium> pMedium;
            mrc = pParent->m->pVirtualBox->i_registerMedium(this, &pMedium,
                                                            treeLock);
            Assert(this == pMedium);
            eik.fetch();

            if (FAILED(mrc))
                /* break parent association on failure to register */
                this->i_deparent();     // removes target from parent
        }
        else
        {
            /* just register  */
            eik.restore();
            ComObjPtr<Medium> pMedium;
            mrc = m->pVirtualBox->i_registerMedium(this, &pMedium, treeLock);
            Assert(this == pMedium);
            eik.fetch();
        }
    }

    if (fCreatingTarget)
    {
        AutoWriteLock mLock(this COMMA_LOCKVAL_SRC_POS);

        if (SUCCEEDED(mrc))
        {
            m->state = MediumState_Created;

            m->size = size;
            m->logicalSize = logicalSize;
            m->variant = variant;
        }
        else
        {
            /* back to NotCreated on failure */
            m->state = MediumState_NotCreated;

            /* reset UUID to prevent it from being reused next time */
            if (fGenerateUuid)
                unconst(m->id).clear();
        }
    }

    // now, at the end of this task (always asynchronous), save the settings
    {
        // save the settings
        i_markRegistriesModified();
        /* collect multiple errors */
        eik.restore();
        m->pVirtualBox->i_saveModifiedRegistries();
        eik.fetch();
    }

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the target chain. */

    /* Make sure the target chain is released early, otherwise it can
     * lead to deadlocks with concurrent IAppliance activities. */
    task.mpTargetMediumLockList->Clear();

    if (task.NotifyAboutChanges() && SUCCEEDED(mrc))
    {
        if (pParent)
            m->pVirtualBox->i_onMediumConfigChanged(pParent);
        if (fCreatingTarget)
            m->pVirtualBox->i_onMediumConfigChanged(this);
        else
            m->pVirtualBox->i_onMediumRegistered(m->id, m->devType, TRUE);
    }

    return mrc;
}

/**
 * Sets up the encryption settings for a filter.
 */
void Medium::i_taskEncryptSettingsSetup(MediumCryptoFilterSettings *pSettings, const char *pszCipher,
                                        const char *pszKeyStore, const char *pszPassword,
                                        bool fCreateKeyStore)
{
    pSettings->pszCipher       = pszCipher;
    pSettings->pszPassword     = pszPassword;
    pSettings->pszKeyStoreLoad = pszKeyStore;
    pSettings->fCreateKeyStore = fCreateKeyStore;
    pSettings->pbDek           = NULL;
    pSettings->cbDek           = 0;
    pSettings->vdFilterIfaces  = NULL;

    pSettings->vdIfCfg.pfnAreKeysValid = i_vdCryptoConfigAreKeysValid;
    pSettings->vdIfCfg.pfnQuerySize    = i_vdCryptoConfigQuerySize;
    pSettings->vdIfCfg.pfnQuery        = i_vdCryptoConfigQuery;
    pSettings->vdIfCfg.pfnQueryBytes   = NULL;

    pSettings->vdIfCrypto.pfnKeyRetain                = i_vdCryptoKeyRetain;
    pSettings->vdIfCrypto.pfnKeyRelease               = i_vdCryptoKeyRelease;
    pSettings->vdIfCrypto.pfnKeyStorePasswordRetain   = i_vdCryptoKeyStorePasswordRetain;
    pSettings->vdIfCrypto.pfnKeyStorePasswordRelease  = i_vdCryptoKeyStorePasswordRelease;
    pSettings->vdIfCrypto.pfnKeyStoreSave             = i_vdCryptoKeyStoreSave;
    pSettings->vdIfCrypto.pfnKeyStoreReturnParameters = i_vdCryptoKeyStoreReturnParameters;

    int vrc = VDInterfaceAdd(&pSettings->vdIfCfg.Core,
                             "Medium::vdInterfaceCfgCrypto",
                             VDINTERFACETYPE_CONFIG, pSettings,
                             sizeof(VDINTERFACECONFIG), &pSettings->vdFilterIfaces);
    AssertRC(vrc);

    vrc = VDInterfaceAdd(&pSettings->vdIfCrypto.Core,
                         "Medium::vdInterfaceCrypto",
                         VDINTERFACETYPE_CRYPTO, pSettings,
                         sizeof(VDINTERFACECRYPTO), &pSettings->vdFilterIfaces);
    AssertRC(vrc);
}

/**
 * Implementation code for the "encrypt" task.
 *
 * @param task
 * @return
 */
HRESULT Medium::i_taskEncryptHandler(Medium::EncryptTask &task)
{
# ifndef VBOX_WITH_EXTPACK
    RT_NOREF(task);
# endif
    HRESULT hrc = S_OK;

    /* Lock all in {parent,child} order. The lock is also used as a
     * signal from the task initiator (which releases it only after
     * RTThreadCreate()) that we can start the job. */
    ComObjPtr<Medium> pBase = i_getBase();
    AutoWriteLock thisLock(this COMMA_LOCKVAL_SRC_POS);

    try
    {
# ifdef VBOX_WITH_EXTPACK
        ExtPackManager *pExtPackManager = m->pVirtualBox->i_getExtPackManager();
        if (pExtPackManager->i_isExtPackUsable(ORACLE_PUEL_EXTPACK_NAME))
        {
            /* Load the plugin */
            Utf8Str strPlugin;
            hrc = pExtPackManager->i_getLibraryPathForExtPack(g_szVDPlugin, ORACLE_PUEL_EXTPACK_NAME, &strPlugin);
            if (SUCCEEDED(hrc))
            {
                int vrc = VDPluginLoadFromFilename(strPlugin.c_str());
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                       tr("Encrypting the image failed because the encryption plugin could not be loaded (%s)"),
                                       i_vdError(vrc).c_str());
            }
            else
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Encryption is not supported because the extension pack '%s' is missing the encryption plugin (old extension pack installed?)"),
                               ORACLE_PUEL_EXTPACK_NAME);
        }
        else
            throw setError(VBOX_E_NOT_SUPPORTED,
                           tr("Encryption is not supported because the extension pack '%s' is missing"),
                           ORACLE_PUEL_EXTPACK_NAME);

        PVDISK pDisk = NULL;
        int vrc = VDCreate(m->vdDiskIfaces, i_convertDeviceType(), &pDisk);
        ComAssertRCThrow(vrc, E_FAIL);

        MediumCryptoFilterSettings CryptoSettingsRead;
        MediumCryptoFilterSettings CryptoSettingsWrite;

        void *pvBuf = NULL;
        const char *pszPasswordNew = NULL;
        try
        {
            /* Set up disk encryption filters. */
            if (task.mstrCurrentPassword.isEmpty())
            {
                /*
                 * Query whether the medium property indicating that encryption is
                 * configured is existing.
                 */
                settings::StringsMap::iterator it = pBase->m->mapProperties.find("CRYPT/KeyStore");
                if (it != pBase->m->mapProperties.end())
                    throw setError(VBOX_E_PASSWORD_INCORRECT,
                                   tr("The password given for the encrypted image is incorrect"));
            }
            else
            {
                settings::StringsMap::iterator it = pBase->m->mapProperties.find("CRYPT/KeyStore");
                if (it == pBase->m->mapProperties.end())
                    throw setError(VBOX_E_INVALID_OBJECT_STATE,
                                   tr("The image is not configured for encryption"));

                i_taskEncryptSettingsSetup(&CryptoSettingsRead, NULL, it->second.c_str(), task.mstrCurrentPassword.c_str(),
                                           false /* fCreateKeyStore */);
                vrc = VDFilterAdd(pDisk, "CRYPT", VD_FILTER_FLAGS_READ, CryptoSettingsRead.vdFilterIfaces);
                if (vrc == VERR_VD_PASSWORD_INCORRECT)
                    throw setError(VBOX_E_PASSWORD_INCORRECT,
                                   tr("The password to decrypt the image is incorrect"));
                else if (RT_FAILURE(vrc))
                    throw setError(VBOX_E_INVALID_OBJECT_STATE,
                                   tr("Failed to load the decryption filter: %s"),
                                   i_vdError(vrc).c_str());
            }

            if (task.mstrCipher.isNotEmpty())
            {
                if (   task.mstrNewPassword.isEmpty()
                    && task.mstrNewPasswordId.isEmpty()
                    && task.mstrCurrentPassword.isNotEmpty())
                {
                    /* An empty password and password ID will default to the current password. */
                    pszPasswordNew = task.mstrCurrentPassword.c_str();
                }
                else if (task.mstrNewPassword.isEmpty())
                    throw setError(VBOX_E_OBJECT_NOT_FOUND,
                                   tr("A password must be given for the image encryption"));
                else if (task.mstrNewPasswordId.isEmpty())
                    throw setError(VBOX_E_INVALID_OBJECT_STATE,
                                   tr("A valid identifier for the password must be given"));
                else
                    pszPasswordNew = task.mstrNewPassword.c_str();

                i_taskEncryptSettingsSetup(&CryptoSettingsWrite, task.mstrCipher.c_str(), NULL,
                                           pszPasswordNew, true /* fCreateKeyStore */);
                vrc = VDFilterAdd(pDisk, "CRYPT", VD_FILTER_FLAGS_WRITE, CryptoSettingsWrite.vdFilterIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_INVALID_OBJECT_STATE, vrc,
                                       tr("Failed to load the encryption filter: %s"),
                                       i_vdError(vrc).c_str());
            }
            else if (task.mstrNewPasswordId.isNotEmpty() || task.mstrNewPassword.isNotEmpty())
                throw setError(VBOX_E_INVALID_OBJECT_STATE,
                               tr("The password and password identifier must be empty if the output should be unencrypted"));

            /* Open all media in the chain. */
            MediumLockList::Base::const_iterator mediumListBegin =
                task.mpMediumLockList->GetBegin();
            MediumLockList::Base::const_iterator mediumListEnd =
                task.mpMediumLockList->GetEnd();
            MediumLockList::Base::const_iterator mediumListLast =
                mediumListEnd;
            --mediumListLast;
            for (MediumLockList::Base::const_iterator it = mediumListBegin;
                 it != mediumListEnd;
                 ++it)
            {
                const MediumLock &mediumLock = *it;
                const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();
                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);

                Assert(pMedium->m->state == MediumState_LockedWrite);

                /* Open all media but last in read-only mode. Do not handle
                 * shareable media, as compaction and sharing are mutually
                 * exclusive. */
                vrc = VDOpen(pDisk,
                             pMedium->m->strFormat.c_str(),
                             pMedium->m->strLocationFull.c_str(),
                             m->uOpenFlagsDef | (it == mediumListLast ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY),
                             pMedium->m->vdImageIfaces);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("Could not open the medium storage unit '%s'%s"),
                                       pMedium->m->strLocationFull.c_str(),
                                       i_vdError(vrc).c_str());
            }

            Assert(m->state == MediumState_LockedWrite);

            Utf8Str location(m->strLocationFull);

            /* unlock before the potentially lengthy operation */
            thisLock.release();

            vrc = VDPrepareWithFilters(pDisk, task.mVDOperationIfaces);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                   tr("Could not prepare disk images for encryption (%Rrc): %s"),
                                   vrc, i_vdError(vrc).c_str());

            thisLock.acquire();
            /* If everything went well set the new key store. */
            settings::StringsMap::iterator it = pBase->m->mapProperties.find("CRYPT/KeyStore");
            if (it != pBase->m->mapProperties.end())
                pBase->m->mapProperties.erase(it);

            /* Delete KeyId if encryption is removed or the password did change. */
            if (   task.mstrNewPasswordId.isNotEmpty()
                || task.mstrCipher.isEmpty())
            {
                it = pBase->m->mapProperties.find("CRYPT/KeyId");
                if (it != pBase->m->mapProperties.end())
                    pBase->m->mapProperties.erase(it);
            }

            if (CryptoSettingsWrite.pszKeyStore)
            {
                pBase->m->mapProperties["CRYPT/KeyStore"] = Utf8Str(CryptoSettingsWrite.pszKeyStore);
                if (task.mstrNewPasswordId.isNotEmpty())
                    pBase->m->mapProperties["CRYPT/KeyId"] = task.mstrNewPasswordId;
            }

            if (CryptoSettingsRead.pszCipherReturned)
                RTStrFree(CryptoSettingsRead.pszCipherReturned);

            if (CryptoSettingsWrite.pszCipherReturned)
                RTStrFree(CryptoSettingsWrite.pszCipherReturned);

            thisLock.release();
            pBase->i_markRegistriesModified();
            m->pVirtualBox->i_saveModifiedRegistries();
        }
        catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

        if (pvBuf)
            RTMemFree(pvBuf);

        VDDestroy(pDisk);
# else
        throw setError(VBOX_E_NOT_SUPPORTED,
                       tr("Encryption is not supported because extension pack support is not built in"));
# endif
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    /* Everything is explicitly unlocked when the task exits,
     * as the task destruction also destroys the media chain. */

    return hrc;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
