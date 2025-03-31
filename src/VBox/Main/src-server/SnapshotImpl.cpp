/* $Id: SnapshotImpl.cpp 109019 2025-03-31 18:12:55Z brent.paulson@oracle.com $ */
/** @file
 * COM class implementation for Snapshot and SnapshotMachine in VBoxSVC.
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

#define LOG_GROUP LOG_GROUP_MAIN_SNAPSHOT
#include <set>
#include <map>

#include "SnapshotImpl.h"
#include "LoggingNew.h"

#include "MachineImpl.h"
#include "MediumImpl.h"
#include "MediumFormatImpl.h"
#include "ProgressImpl.h"
#include "Global.h"
#include "StringifyEnums.h"

/// @todo these three includes are required for about one or two lines, try
// to remove them and put that code in shared code in MachineImplcpp
#include "SharedFolderImpl.h"
#include "USBControllerImpl.h"
#include "USBDeviceFiltersImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"
#include "VBox/com/MultiResult.h"

#include <iprt/path.h>
#include <iprt/cpp/utils.h>

#include <VBox/param.h>
#include <iprt/errcore.h>

#include <VBox/settings.h>

////////////////////////////////////////////////////////////////////////////////
//
// Snapshot private data definition
//
////////////////////////////////////////////////////////////////////////////////

typedef std::list< ComObjPtr<Snapshot> > SnapshotsList;

struct Snapshot::Data
{
    Data()
        : pVirtualBox(NULL)
    {
        RTTimeSpecSetMilli(&timeStamp, 0);
    };

    ~Data()
    {}

    const Guid                  uuid;
    Utf8Str                     strName;
    Utf8Str                     strDescription;
    RTTIMESPEC                  timeStamp;
    ComObjPtr<SnapshotMachine>  pMachine;

    /** weak VirtualBox parent */
    VirtualBox * const          pVirtualBox;

    // pParent and llChildren are protected by the machine lock
    ComObjPtr<Snapshot>         pParent;
    SnapshotsList               llChildren;
};

////////////////////////////////////////////////////////////////////////////////
//
// Constructor / destructor
//
////////////////////////////////////////////////////////////////////////////////
DEFINE_EMPTY_CTOR_DTOR(Snapshot)

HRESULT Snapshot::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void Snapshot::FinalRelease()
{
    LogFlowThisFunc(("\n"));
    uninit();
    BaseFinalRelease();
}

/**
 *  Initializes the instance
 *
 *  @param  aVirtualBox    VirtualBox object
 *  @param  aId            id of the snapshot
 *  @param  aName          name of the snapshot
 *  @param  aDescription   name of the snapshot (NULL if no description)
 *  @param  aTimeStamp     timestamp of the snapshot, in ms since 1970-01-01 UTC
 *  @param  aMachine       machine associated with this snapshot
 *  @param  aParent        parent snapshot (NULL if no parent)
 */
HRESULT Snapshot::init(VirtualBox *aVirtualBox,
                       const Guid &aId,
                       const Utf8Str &aName,
                       const Utf8Str &aDescription,
                       const RTTIMESPEC &aTimeStamp,
                       SnapshotMachine *aMachine,
                       Snapshot *aParent)
{
    LogFlowThisFunc(("uuid=%s aParent->uuid=%s\n", aId.toString().c_str(), (aParent) ? aParent->m->uuid.toString().c_str() : ""));

    ComAssertRet(!aId.isZero() && aId.isValid() && aMachine, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data;

    /* share parent weakly */
    unconst(m->pVirtualBox) = aVirtualBox;

    m->pParent = aParent;

    unconst(m->uuid) = aId;
    m->strName = aName;
    m->strDescription = aDescription;
    m->timeStamp = aTimeStamp;
    m->pMachine = aMachine;

    if (aParent)
        aParent->m->llChildren.push_back(this);

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease(), by the parent when it gets destroyed,
 *  or by a third party when it decides this object is no more valid.
 *
 *  Since this manipulates the snapshots tree, the caller must hold the
 *  machine lock in write mode (which protects the snapshots tree)!
 *
 * @note All children of this snapshot get uninitialized, too, in a stack
 *       friendly manner.
 */
void Snapshot::uninit()
{
    LogFlowThisFunc(("\n"));

    {
        /* If "this" is already uninitialized or was never initialized, skip
         * all activity since it makes no sense. Also would cause asserts with
         * the automatic refcount updating with SnapshotList/ComPtr. Also,
         * make sure that the possible fake error is undone. */
        ErrorInfoKeeper eik;
        AutoLimitedCaller autoCaller(this);
        if (FAILED(autoCaller.hrc()))
            return;
    }

    SnapshotsList llSnapshotsTodo;
    llSnapshotsTodo.push_back(this);
    SnapshotsList llSnapshotsAll;

    while (llSnapshotsTodo.size() > 0)
    {
        /* This also guarantees that the refcount doesn't actually drop to 0
         * again while the uninit is already ongoing. */
        ComObjPtr<Snapshot> pSnapshot = llSnapshotsTodo.front();
        llSnapshotsTodo.pop_front();

        /* Enclose the state transition Ready->InUninit->NotReady */
        AutoUninitSpan autoUninitSpan(pSnapshot);
        if (autoUninitSpan.uninitDone())
            continue;

        /* Remember snapshots (depth first), for associated SnapshotMachine
         * uninitialization, which must be done in dept first order, otherwise
         * the Medium object uninit is done in the wrong order. */
        llSnapshotsAll.push_front(pSnapshot);

        Assert(pSnapshot->m->pMachine->isWriteLockOnCurrentThread());

        /* Remove initial snapshot from parent snapshot's list of children. */
        if (pSnapshot == this)
            pSnapshot->i_deparent();

        /* Paranoia. Shouldn't be set any more at processing time. */
        Assert(!pSnapshot->m || pSnapshot->m->pParent.isNull());

        /* Process all children */
        SnapshotsList::const_iterator itBegin = pSnapshot->m->llChildren.begin();
        SnapshotsList::const_iterator itEnd = pSnapshot->m->llChildren.end();
        for (SnapshotsList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            Snapshot *pChild = *it;

            if (!pChild || !pChild->m)
                continue;

            pChild->m->pParent.setNull();
            llSnapshotsTodo.push_back(pChild);
        }

        /* Children information obsolete, will be processed anyway. */
        pSnapshot->m->llChildren.clear();

        autoUninitSpan.setSucceeded();
    }

    /* Now handle SnapshotMachine uninit and free memory. */
    while (llSnapshotsAll.size() > 0)
    {
        ComObjPtr<Snapshot> pSnapshot = llSnapshotsAll.front();
        llSnapshotsAll.pop_front();

        if (pSnapshot->m->pMachine)
        {
            pSnapshot->m->pMachine->uninit();
            pSnapshot->m->pMachine.setNull();
        }

        delete pSnapshot->m;
        pSnapshot->m = NULL;
    }
}

/**
 *  Delete the current snapshot by removing it from the tree of snapshots
 *  and reparenting its children.
 *
 *  After this, the caller must call uninit() on the snapshot. We can't call
 *  that from here because if we do, the AutoUninitSpan waits forever for
 *  the number of callers to become 0 (it is 1 because of the AutoCaller in here).
 *
 *  NOTE: this does NOT lock the snapshot, it is assumed that the machine state
 *  (and the snapshots tree) is protected by the caller having requested the machine
 *  lock in write mode AND the machine state must be DeletingSnapshot.
 */
void Snapshot::i_beginSnapshotDelete()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return;

    // caller must have acquired the machine's write lock
    Assert(   m->pMachine->mData->mMachineState == MachineState_DeletingSnapshot
           || m->pMachine->mData->mMachineState == MachineState_DeletingSnapshotOnline
           || m->pMachine->mData->mMachineState == MachineState_DeletingSnapshotPaused);
    Assert(m->pMachine->isWriteLockOnCurrentThread());

    // the snapshot must have only one child when being deleted or no children at all
    AssertReturnVoid(m->llChildren.size() <= 1);

    ComObjPtr<Snapshot> parentSnapshot = m->pParent;

    /// @todo (dmik):
    //  when we introduce clones later, deleting the snapshot will affect
    //  the current and first snapshots of clones, if they are direct children
    //  of this snapshot. So we will need to lock machines associated with
    //  child snapshots as well and update mCurrentSnapshot and/or
    //  mFirstSnapshot fields.

    if (this == m->pMachine->mData->mCurrentSnapshot)
    {
        m->pMachine->mData->mCurrentSnapshot = parentSnapshot;

        /* we've changed the base of the current state so mark it as
         * modified as it no longer guaranteed to be its copy */
        m->pMachine->mData->mCurrentStateModified = TRUE;
    }

    if (this == m->pMachine->mData->mFirstSnapshot)
    {
        if (m->llChildren.size() == 1)
        {
            ComObjPtr<Snapshot> childSnapshot = m->llChildren.front();
            m->pMachine->mData->mFirstSnapshot = childSnapshot;
        }
        else
            m->pMachine->mData->mFirstSnapshot.setNull();
    }

    // reparent our children
    for (SnapshotsList::const_iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
    {
        ComObjPtr<Snapshot> child = *it;
        // no need to lock, snapshots tree is protected by machine lock
        child->m->pParent = m->pParent;
        if (m->pParent)
            m->pParent->m->llChildren.push_back(child);
    }

    // clear our own children list (since we reparented the children)
    m->llChildren.clear();
}

/**
 * Internal helper that removes "this" from the list of children of its
 * parent. Used in places when reparenting is necessary.
 *
 * The caller must hold the machine lock in write mode (which protects the snapshots tree)!
 */
void Snapshot::i_deparent()
{
    Assert(m->pMachine->isWriteLockOnCurrentThread());

    if (m->pParent.isNull())
        return;

    Assert(m->pParent->m);

    SnapshotsList &llParent = m->pParent->m->llChildren;
    for (SnapshotsList::iterator it = llParent.begin();
         it != llParent.end();
         ++it)
    {
        Snapshot *pParentsChild = *it;
        if (this == pParentsChild)
        {
            llParent.erase(it);
            break;
        }
    }

    m->pParent.setNull();
}

////////////////////////////////////////////////////////////////////////////////
//
// ISnapshot public methods
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Snapshot::getId(com::Guid &aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aId = m->uuid;

    return S_OK;
}

HRESULT Snapshot::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aName = m->strName;
    return S_OK;
}

/**
 *  @note Locks this object for writing, then calls Machine::onSnapshotChange()
 *  (see its lock requirements).
 */
HRESULT Snapshot::setName(const com::Utf8Str &aName)
{
    HRESULT hrc = S_OK;

    // prohibit setting a UUID only as the machine name, or else it can
    // never be found by findMachine()
    Guid test(aName);

    if (!test.isZero() && test.isValid())
        return setError(E_INVALIDARG, tr("A machine cannot have a UUID as its name"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->strName != aName)
    {
        m->strName = aName;
        alock.release(); /* Important! (child->parent locks are forbidden) */
        hrc = m->pMachine->i_onSnapshotChange(this);
    }

    return hrc;
}

HRESULT Snapshot::getDescription(com::Utf8Str &aDescription)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDescription = m->strDescription;
    return S_OK;
}

HRESULT Snapshot::setDescription(const com::Utf8Str &aDescription)
{
    HRESULT hrc = S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (m->strDescription != aDescription)
    {
        m->strDescription = aDescription;
        alock.release(); /* Important! (child->parent locks are forbidden) */
        hrc = m->pMachine->i_onSnapshotChange(this);
    }

    return hrc;
}

HRESULT Snapshot::getTimeStamp(LONG64 *aTimeStamp)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aTimeStamp = RTTimeSpecGetMilli(&m->timeStamp);
    return S_OK;
}

HRESULT Snapshot::getOnline(BOOL *aOnline)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aOnline = i_getStateFilePath().isNotEmpty();
    return S_OK;
}

HRESULT Snapshot::getMachine(ComPtr<IMachine> &aMachine)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->pMachine.queryInterfaceTo(aMachine.asOutParam());

    return S_OK;
}


HRESULT Snapshot::getParent(ComPtr<ISnapshot> &aParent)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->pParent.queryInterfaceTo(aParent.asOutParam());
    return S_OK;
}

HRESULT Snapshot::getChildren(std::vector<ComPtr<ISnapshot> > &aChildren)
{
    // snapshots tree is protected by machine lock
    AutoReadLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);
    aChildren.resize(0);
    for (SnapshotsList::const_iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
        aChildren.push_back(*it);
    return S_OK;
}

HRESULT Snapshot::getChildrenCount(ULONG *count)
{
    *count = i_getChildrenCount();

    return S_OK;
}

////////////////////////////////////////////////////////////////////////////////
//
// Snapshot public internal methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Returns the parent snapshot or NULL if there's none.  Must have caller + locking!
 * @return
 */
const ComObjPtr<Snapshot>& Snapshot::i_getParent() const
{
    return m->pParent;
}

/**
 * Returns the first child snapshot or NULL if there's none.  Must have caller + locking!
 * @return
 */
const ComObjPtr<Snapshot> Snapshot::i_getFirstChild() const
{
    if (!m->llChildren.size())
        return NULL;
    return m->llChildren.front();
}

/**
 *  @note
 *      Must be called from under the object's lock!
 */
const Utf8Str& Snapshot::i_getStateFilePath() const
{
    return m->pMachine->mSSData->strStateFilePath;
}

/**
 * Returns the depth in the snapshot tree for this snapshot.
 *
 * @note takes the snapshot tree lock
 */

uint32_t Snapshot::i_getDepth()
{
    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // snapshots tree is protected by machine lock
    AutoReadLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    uint32_t cDepth = 0;
    ComObjPtr<Snapshot> pSnap(this);
    while (!pSnap.isNull())
    {
        pSnap = pSnap->m->pParent;
        cDepth++;
    }

    return cDepth;
}

/**
 * Returns the number of direct child snapshots, without grandchildren.
 * @return
 */
ULONG Snapshot::i_getChildrenCount()
{
    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // snapshots tree is protected by machine lock
    AutoReadLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    return (ULONG)m->llChildren.size();
}

/**
 * Returns the number of child snapshots including all grandchildren.
 * @return
 */
ULONG Snapshot::i_getAllChildrenCount()
{
    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // snapshots tree is protected by machine lock
    AutoReadLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    std::list<const Snapshot *> llSnapshotsTodo;
    llSnapshotsTodo.push_back(this);

    ULONG cChildren = 0;

    while (llSnapshotsTodo.size() > 0)
    {
        const Snapshot *pSnapshot = llSnapshotsTodo.front();
        llSnapshotsTodo.pop_front();

        /* Check if snapshot is uninitialized already, can happen if an API
         * client asks at an inconvenient time. */
        if (!pSnapshot->m)
            continue;

        cChildren += (ULONG)pSnapshot->m->llChildren.size();

        /* count all children */
        SnapshotsList::const_iterator itBegin = pSnapshot->m->llChildren.begin();
        SnapshotsList::const_iterator itEnd = pSnapshot->m->llChildren.end();
        for (SnapshotsList::const_iterator it = itBegin; it != itEnd; ++it)
            llSnapshotsTodo.push_back(*it);
    }

    return cChildren;
}

/**
 * Returns the SnapshotMachine that this snapshot belongs to.
 * Caller must hold the snapshot's object lock!
 * @return
 */
const ComObjPtr<SnapshotMachine>& Snapshot::i_getSnapshotMachine() const
{
    return m->pMachine;
}

/**
 * Returns the UUID of this snapshot.
 * Caller must hold the snapshot's object lock!
 * @return
 */
Guid Snapshot::i_getId() const
{
    return m->uuid;
}

/**
 * Returns the name of this snapshot.
 * Caller must hold the snapshot's object lock!
 * @return
 */
const Utf8Str& Snapshot::i_getName() const
{
    return m->strName;
}

/**
 * Returns the time stamp of this snapshot.
 * Caller must hold the snapshot's object lock!
 * @return
 */
RTTIMESPEC Snapshot::i_getTimeStamp() const
{
    return m->timeStamp;
}

/**
 *  Searches for a snapshot with the given ID among children, grand-children,
 *  etc. of this snapshot. This snapshot itself is also included in the search.
 *
 *  Caller must hold the machine lock (which protects the snapshots tree!)
 */
ComObjPtr<Snapshot> Snapshot::i_findChildOrSelf(IN_GUID aId)
{
    ComObjPtr<Snapshot> child;

    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // no need to lock, uuid is const
    if (m->uuid == aId)
        child = this;
    else
    {
        for (SnapshotsList::const_iterator it = m->llChildren.begin();
             it != m->llChildren.end();
             ++it)
        {
            if ((child = (*it)->i_findChildOrSelf(aId)))
                break;
        }
    }

    return child;
}

/**
 *  Searches for a first snapshot with the given name among children,
 *  grand-children, etc. of this snapshot. This snapshot itself is also included
 *  in the search.
 *
 *  Caller must hold the machine lock (which protects the snapshots tree!)
 */
ComObjPtr<Snapshot> Snapshot::i_findChildOrSelf(const Utf8Str &aName)
{
    ComObjPtr<Snapshot> child;
    AssertReturn(!aName.isEmpty(), child);

    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->strName == aName)
        child = this;
    else
    {
        alock.release();
        for (SnapshotsList::const_iterator it = m->llChildren.begin();
             it != m->llChildren.end();
             ++it)
        {
            if ((child = (*it)->i_findChildOrSelf(aName)))
                break;
        }
    }

    return child;
}

/**
 * Internal implementation for Snapshot::updateSavedStatePaths (below).
 * @param   strOldPath
 * @param   strNewPath
 */
void Snapshot::i_updateSavedStatePathsImpl(const Utf8Str &strOldPath,
                                           const Utf8Str &strNewPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const Utf8Str &path = m->pMachine->mSSData->strStateFilePath;
    LogFlowThisFunc(("Snap[%s].statePath={%s}\n", m->strName.c_str(), path.c_str()));

    /* state file may be NULL (for offline snapshots) */
    if (    path.isNotEmpty()
         && RTPathStartsWith(path.c_str(), strOldPath.c_str())
       )
    {
        m->pMachine->mSSData->strStateFilePath = Utf8StrFmt("%s%s",
                                                            strNewPath.c_str(),
                                                            path.c_str() + strOldPath.length());
        LogFlowThisFunc(("-> updated: {%s}\n", m->pMachine->mSSData->strStateFilePath.c_str()));
    }

    for (SnapshotsList::const_iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
    {
        Snapshot *pChild = *it;
        pChild->i_updateSavedStatePathsImpl(strOldPath, strNewPath);
    }
}

/**
 *  Checks if the specified path change affects the saved state file path of
 *  this snapshot or any of its (grand-)children and updates it accordingly.
 *
 *  Intended to be called by Machine::openConfigLoader() only.
 *
 *  @param  strOldPath old path (full)
 *  @param  strNewPath new path (full)
 *
 *  @note Locks the machine (for the snapshots tree) +  this object + children for writing.
 */
void Snapshot::i_updateSavedStatePaths(const Utf8Str &strOldPath,
                                       const Utf8Str &strNewPath)
{
    LogFlowThisFunc(("aOldPath={%s} aNewPath={%s}\n", strOldPath.c_str(), strNewPath.c_str()));

    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // snapshots tree is protected by machine lock
    AutoWriteLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    // call the implementation under the tree lock
    i_updateSavedStatePathsImpl(strOldPath, strNewPath);
}

/**
 * Returns true if this snapshot or one of its children uses the given file,
 * whose path must be fully qualified, as its saved state. When invoked on a
 * machine's first snapshot, this can be used to check if a saved state file
 * is shared with any snapshots.
 *
 * Caller must hold the machine lock, which protects the snapshots tree.
 *
 * @param strPath
 * @param pSnapshotToIgnore If != NULL, this snapshot is ignored during the checks.
 * @return
 */
bool Snapshot::i_sharesSavedStateFile(const Utf8Str &strPath,
                                      Snapshot *pSnapshotToIgnore)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    std::list<const Snapshot *> llSnapshotsTodo;
    llSnapshotsTodo.push_back(this);

    while (llSnapshotsTodo.size() > 0)
    {
        const Snapshot *pSnapshot = llSnapshotsTodo.front();
        llSnapshotsTodo.pop_front();
        const Utf8Str &path = pSnapshot->m->pMachine->mSSData->strStateFilePath;

        if ((!pSnapshotToIgnore || pSnapshotToIgnore != pSnapshot) && path.isNotEmpty())
            if (path == strPath)
                return true;

        /* check all children */
        SnapshotsList::const_iterator itBegin = pSnapshot->m->llChildren.begin();
        SnapshotsList::const_iterator itEnd = pSnapshot->m->llChildren.end();
        for (SnapshotsList::const_iterator it = itBegin; it != itEnd; ++it)
            llSnapshotsTodo.push_back(*it);
    }

    return false;
}


/**
 * Internal implementation for Snapshot::updateNVRAMPaths (below).
 * @param   strOldPath
 * @param   strNewPath
 */
void Snapshot::i_updateNVRAMPathsImpl(const Utf8Str &strOldPath,
                                      const Utf8Str &strNewPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const Utf8Str path = m->pMachine->mNvramStore->i_getNonVolatileStorageFile();
    LogFlowThisFunc(("Snap[%s].nvramPath={%s}\n", m->strName.c_str(), path.c_str()));

    /* NVRAM filename may be empty */
    if (    path.isNotEmpty()
         && RTPathStartsWith(path.c_str(), strOldPath.c_str())
       )
    {
        m->pMachine->mNvramStore->i_updateNonVolatileStorageFile(Utf8StrFmt("%s%s",
                                                                              strNewPath.c_str(),
                                                                              path.c_str() + strOldPath.length()));
        LogFlowThisFunc(("-> updated: {%s}\n", m->pMachine->mNvramStore->i_getNonVolatileStorageFile().c_str()));
    }

    for (SnapshotsList::const_iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
    {
        Snapshot *pChild = *it;
        pChild->i_updateNVRAMPathsImpl(strOldPath, strNewPath);
    }
}

/**
 *  Checks if the specified path change affects the NVRAM file path of
 *  this snapshot or any of its (grand-)children and updates it accordingly.
 *
 *  Intended to be called by Machine::openConfigLoader() only.
 *
 *  @param  strOldPath old path (full)
 *  @param  strNewPath new path (full)
 *
 *  @note Locks the machine (for the snapshots tree) +  this object + children for writing.
 */
void Snapshot::i_updateNVRAMPaths(const Utf8Str &strOldPath,
                                  const Utf8Str &strNewPath)
{
    LogFlowThisFunc(("aOldPath={%s} aNewPath={%s}\n", strOldPath.c_str(), strNewPath.c_str()));

    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    // snapshots tree is protected by machine lock
    AutoWriteLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    // call the implementation under the tree lock
    i_updateNVRAMPathsImpl(strOldPath, strNewPath);
}

/**
 * Saves the settings attributes of one snapshot.
 *
 * @param data      Target for saving snapshot settings.
 * @return
 */
HRESULT Snapshot::i_saveSnapshotOne(settings::Snapshot &data) const
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data.uuid = m->uuid;
    data.strName = m->strName;
    data.timestamp = m->timeStamp;
    data.strDescription = m->strDescription;

    // state file (only if this snapshot is online)
    if (i_getStateFilePath().isNotEmpty())
        m->pMachine->i_copyPathRelativeToMachine(i_getStateFilePath(), data.strStateFile);
    else
        data.strStateFile.setNull();

    return m->pMachine->i_saveHardware(data.hardware, &data.debugging, &data.autostart, data.recordingSettings);
}

/**
 * Saves the given snapshot and all its children.
 * It is assumed that the given node is empty.
 *
 * @param data      Target for saving snapshot settings.
 */
HRESULT Snapshot::i_saveSnapshot(settings::Snapshot &data) const
{
    // snapshots tree is protected by machine lock
    AutoReadLock alock(m->pMachine COMMA_LOCKVAL_SRC_POS);

    std::list<const Snapshot *> llSnapshotsTodo;
    llSnapshotsTodo.push_back(this);
    std::list<settings::Snapshot *> llSettingsTodo;
    llSettingsTodo.push_back(&data);

    while (llSnapshotsTodo.size() > 0)
    {
        const Snapshot *pSnapshot = llSnapshotsTodo.front();
        llSnapshotsTodo.pop_front();
        settings::Snapshot *current = llSettingsTodo.front();
        llSettingsTodo.pop_front();

        HRESULT hrc = pSnapshot->i_saveSnapshotOne(*current);
        if (FAILED(hrc))
            return hrc;

        /* save all children */
        SnapshotsList::const_iterator itBegin = pSnapshot->m->llChildren.begin();
        SnapshotsList::const_iterator itEnd = pSnapshot->m->llChildren.end();
        for (SnapshotsList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            AutoCaller autoCaller(*it);
            if (FAILED(autoCaller.hrc()))
                continue;

            llSnapshotsTodo.push_back(*it);
            current->llChildSnapshots.push_back(settings::Snapshot::Empty);
            llSettingsTodo.push_back(&current->llChildSnapshots.back());
        }
    }

    return S_OK;
}

/**
 * Part of the cleanup engine of Machine::Unregister().
 *
 * This removes all medium attachments from the snapshot's machine and returns
 * the snapshot's saved state file name, if any, and then calls uninit().
 *
 * This processes children depth first, so the given MediaList receives child
 * media first before their parents. If the caller wants to close all media,
 * they should go thru the list from the beginning to the end because media
 * cannot be closed if they have children.
 *
 * This calls uninit() on itself, so the snapshots tree (beginning with a machine's pFirstSnapshot) becomes invalid after this.
 * It does not alter the main machine's snapshot pointers (pFirstSnapshot, pCurrentSnapshot).
 *
 * Caller must hold the machine write lock (which protects the snapshots tree!)
 *
 * @param writeLock Machine write lock, which can get released temporarily here.
 * @param cleanupMode Cleanup mode; see Machine::detachAllMedia().
 * @param llMedia List of media returned to caller, depending on cleanupMode.
 * @param llFilenames
 * @return
 */
HRESULT Snapshot::i_uninitAll(AutoWriteLock &writeLock,
                              CleanupMode_T cleanupMode,
                              MediaList &llMedia,
                              std::list<Utf8Str> &llFilenames)
{
    Assert(m->pMachine->isWriteLockOnCurrentThread());

    HRESULT hrc = S_OK;

    SnapshotsList llSnapshotsTodo;
    llSnapshotsTodo.push_front(this);
    SnapshotsList llSnapshotsAll;

    /* Enumerate all snapshots depth first, avoids trouble with updates. */
    while (llSnapshotsTodo.size() > 0)
    {
        ComObjPtr<Snapshot> pSnapshot = llSnapshotsTodo.front();
        llSnapshotsTodo.pop_front();

        llSnapshotsAll.push_front(pSnapshot);

        /* Process all children */
        SnapshotsList::const_iterator itBegin = pSnapshot->m->llChildren.begin();
        SnapshotsList::const_iterator itEnd = pSnapshot->m->llChildren.end();
        for (SnapshotsList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            Snapshot *pChild = *it;
            pChild->m->pParent.setNull();
            llSnapshotsTodo.push_front(pChild);
        }
    }

    /* Process all snapshots in enumeration order. */
    while (llSnapshotsAll.size() > 0)
    {
        /* This also guarantees that the refcount doesn't actually drop to 0
         * again while the uninit is already ongoing. */
        ComObjPtr<Snapshot> pSnapshot = llSnapshotsAll.front();
        llSnapshotsAll.pop_front();

        hrc = pSnapshot->m->pMachine->i_detachAllMedia(writeLock, pSnapshot, cleanupMode, llMedia);
        if (SUCCEEDED(hrc))
        {
            Utf8Str strFile;

            // report the saved state file if it's not on the list yet
            strFile = pSnapshot->m->pMachine->mSSData->strStateFilePath;
            if (strFile.isNotEmpty())
            {
                std::list<Utf8Str>::const_iterator itFound = find(llFilenames.begin(), llFilenames.end(), strFile);

                if (itFound == llFilenames.end())
                    llFilenames.push_back(strFile);
            }

            strFile = pSnapshot->m->pMachine->mNvramStore->i_getNonVolatileStorageFile();
            if (strFile.isNotEmpty() && RTFileExists(strFile.c_str()))
                llFilenames.push_back(strFile);
        }

        pSnapshot->m->pParent.setNull();
        pSnapshot->m->llChildren.clear();
        pSnapshot->uninit();
    }

    return S_OK;
}

////////////////////////////////////////////////////////////////////////////////
//
// SnapshotMachine implementation
//
////////////////////////////////////////////////////////////////////////////////

SnapshotMachine::SnapshotMachine()
    : mMachine(NULL)
{}

SnapshotMachine::~SnapshotMachine()
{}

HRESULT SnapshotMachine::FinalConstruct()
{
    LogFlowThisFunc(("\n"));

    return BaseFinalConstruct();
}

void SnapshotMachine::FinalRelease()
{
    LogFlowThisFunc(("\n"));

    uninit();

    BaseFinalRelease();
}

/**
 *  Initializes the SnapshotMachine object when taking a snapshot.
 *
 *  @param aSessionMachine  machine to take a snapshot from
 *  @param aSnapshotId      snapshot ID of this snapshot machine
 *  @param aStateFilePath   file where the execution state will be later saved
 *                          (or NULL for the offline snapshot)
 *
 *  @note The aSessionMachine must be locked for writing.
 */
HRESULT SnapshotMachine::init(SessionMachine *aSessionMachine,
                              IN_GUID aSnapshotId,
                              const Utf8Str &aStateFilePath)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("mName={%s}\n", aSessionMachine->mUserData->s.strName.c_str()));

    Guid l_guid(aSnapshotId);
    AssertReturn(aSessionMachine && (!l_guid.isZero() && l_guid.isValid()), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    AssertReturn(aSessionMachine->isWriteLockOnCurrentThread(), E_FAIL);

    mSnapshotId = aSnapshotId;
    ComObjPtr<Machine> pMachine = aSessionMachine->mPeer;

    /* mPeer stays NULL */
    /* memorize the primary Machine instance (i.e. not SessionMachine!) */
    unconst(mMachine) = pMachine;
    /* share the parent pointer */
    unconst(mParent) = pMachine->mParent;

    /* take the pointer to Data to share */
    mData.share(pMachine->mData);

    /* take the pointer to UserData to share (our UserData must always be the
     * same as Machine's data) */
    mUserData.share(pMachine->mUserData);

    /* make a private copy of all other data */
    mHWData.attachCopy(aSessionMachine->mHWData);

    /* SSData is always unique for SnapshotMachine */
    mSSData.allocate();
    mSSData->strStateFilePath = aStateFilePath;

    HRESULT hrc = S_OK;

    /* Create copies of all attachments (mMediaData after attaching a copy
     * contains just references to original objects). Additionally associate
     * media with the snapshot (Machine::uninitDataAndChildObjects() will
     * deassociate at destruction). */
    mMediumAttachments.allocate();
    for (MediumAttachmentList::const_iterator
         it = aSessionMachine->mMediumAttachments->begin();
         it != aSessionMachine->mMediumAttachments->end();
         ++it)
    {
        ComObjPtr<MediumAttachment> pAtt;
        pAtt.createObject();
        hrc = pAtt->initCopy(this, *it);
        if (FAILED(hrc)) return hrc;
        mMediumAttachments->push_back(pAtt);

        Medium *pMedium = pAtt->i_getMedium();
        if (pMedium) // can be NULL for non-harddisk
        {
            hrc = pMedium->i_addBackReference(mData->mUuid, mSnapshotId);
            AssertComRC(hrc);
        }
    }

    /* create copies of all shared folders (mHWData after attaching a copy
     * contains just references to original objects) */
    for (HWData::SharedFolderList::iterator
         it = mHWData->mSharedFolders.begin();
         it != mHWData->mSharedFolders.end();
         ++it)
    {
        ComObjPtr<SharedFolder> pFolder;
        pFolder.createObject();
        hrc = pFolder->initCopy(this, *it);
        if (FAILED(hrc)) return hrc;
        *it = pFolder;
    }

    /* create copies of all PCI device assignments (mHWData after attaching
     * a copy contains just references to original objects) */
    for (HWData::PCIDeviceAssignmentList::iterator
         it = mHWData->mPCIDeviceAssignments.begin();
         it != mHWData->mPCIDeviceAssignments.end();
         ++it)
    {
        ComObjPtr<PCIDeviceAttachment> pDev;
        pDev.createObject();
        hrc = pDev->initCopy(this, *it);
        if (FAILED(hrc)) return hrc;
        *it = pDev;
    }

    /* create copies of all storage controllers (mStorageControllerData
     * after attaching a copy contains just references to original objects) */
    mStorageControllers.allocate();
    for (StorageControllerList::const_iterator
         it = aSessionMachine->mStorageControllers->begin();
         it != aSessionMachine->mStorageControllers->end();
         ++it)
    {
        ComObjPtr<StorageController> ctrl;
        ctrl.createObject();
        hrc = ctrl->initCopy(this, *it);
        if (FAILED(hrc)) return hrc;
        mStorageControllers->push_back(ctrl);
    }

    /* create all other child objects that will be immutable private copies */

    unconst(mPlatform).createObject();
    hrc = mPlatform->initCopy(this, pMachine->mPlatform);
    if (FAILED(hrc)) return hrc;

    unconst(mPlatformProperties).createObject();
    hrc = mPlatformProperties->init(mParent); /* Does not contain any "real" data, hence only init() and not initCopy(). */
    if (FAILED(hrc)) return hrc;

    unconst(mFirmwareSettings).createObject();
    hrc = mFirmwareSettings->initCopy(this, pMachine->mFirmwareSettings);
    if (FAILED(hrc)) return hrc;

    unconst(mRecordingSettings).createObject();
    hrc = mRecordingSettings->initCopy(this, pMachine->mRecordingSettings);
    if (FAILED(hrc)) return hrc;

    unconst(mTrustedPlatformModule).createObject();
    hrc = mTrustedPlatformModule->initCopy(this, pMachine->mTrustedPlatformModule);
    if (FAILED(hrc)) return hrc;

    unconst(mNvramStore).createObject();
    hrc = mNvramStore->initCopy(this, pMachine->mNvramStore);
    if (FAILED(hrc)) return hrc;

    unconst(mGraphicsAdapter).createObject();
    hrc = mGraphicsAdapter->initCopy(this, pMachine->mGraphicsAdapter);
    if (FAILED(hrc)) return hrc;

    unconst(mVRDEServer).createObject();
    hrc = mVRDEServer->initCopy(this, pMachine->mVRDEServer);
    if (FAILED(hrc)) return hrc;

    unconst(mAudioSettings).createObject();
    hrc = mAudioSettings->initCopy(this, pMachine->mAudioSettings);
    if (FAILED(hrc)) return hrc;

    /* create copies of all USB controllers (mUSBControllerData
     * after attaching a copy contains just references to original objects) */
    mUSBControllers.allocate();
    for (USBControllerList::const_iterator
         it = aSessionMachine->mUSBControllers->begin();
         it != aSessionMachine->mUSBControllers->end();
         ++it)
    {
        ComObjPtr<USBController> ctrl;
        ctrl.createObject();
        hrc = ctrl->initCopy(this, *it);
        if (FAILED(hrc)) return hrc;
        mUSBControllers->push_back(ctrl);
    }

    unconst(mUSBDeviceFilters).createObject();
    hrc = mUSBDeviceFilters->initCopy(this, pMachine->mUSBDeviceFilters);
    if (FAILED(hrc)) return hrc;

    mNetworkAdapters.resize(pMachine->mNetworkAdapters.size());
    for (ULONG slot = 0; slot < mNetworkAdapters.size(); slot++)
    {
        unconst(mNetworkAdapters[slot]).createObject();
        hrc = mNetworkAdapters[slot]->initCopy(this, pMachine->mNetworkAdapters[slot]);
        if (FAILED(hrc)) return hrc;
    }

    for (ULONG slot = 0; slot < RT_ELEMENTS(mSerialPorts); slot++)
    {
        unconst(mSerialPorts[slot]).createObject();
        hrc = mSerialPorts[slot]->initCopy(this, pMachine->mSerialPorts[slot]);
        if (FAILED(hrc)) return hrc;
    }

    for (ULONG slot = 0; slot < RT_ELEMENTS(mParallelPorts); slot++)
    {
        unconst(mParallelPorts[slot]).createObject();
        hrc = mParallelPorts[slot]->initCopy(this, pMachine->mParallelPorts[slot]);
        if (FAILED(hrc)) return hrc;
    }

    unconst(mBandwidthControl).createObject();
    hrc = mBandwidthControl->initCopy(this, pMachine->mBandwidthControl);
    if (FAILED(hrc)) return hrc;

    unconst(mGuestDebugControl).createObject();
    hrc = mGuestDebugControl->initCopy(this, pMachine->mGuestDebugControl);
    if (FAILED(hrc)) return hrc;

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the SnapshotMachine object when loading from the settings file.
 *
 *  @param aMachine         machine the snapshot belongs to
 *  @param hardware         hardware settings
 *  @param pDbg             debuging settings
 *  @param pAutostart       autostart settings
 *  @param recording        recording settings
 *  @param aSnapshotId      snapshot ID of this snapshot machine
 *  @param aStateFilePath   file where the execution state is saved
 *                          (or NULL for the offline snapshot)
 *
 *  @note Doesn't lock anything.
 */
HRESULT SnapshotMachine::initFromSettings(Machine *aMachine,
                                          const settings::Hardware &hardware,
                                          const settings::Debugging *pDbg,
                                          const settings::Autostart *pAutostart,
                                          const settings::Recording &recording,
                                          IN_GUID aSnapshotId,
                                          const Utf8Str &aStateFilePath)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("mName={%s}\n", aMachine->mUserData->s.strName.c_str()));

    Guid l_guid(aSnapshotId);
    AssertReturn(aMachine && (!l_guid.isZero() && l_guid.isValid()), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Don't need to lock aMachine when VirtualBox is starting up */

    mSnapshotId = aSnapshotId;

    /* mPeer stays NULL */
    /* memorize the primary Machine instance (i.e. not SessionMachine!) */
    unconst(mMachine) = aMachine;
    /* share the parent pointer */
    unconst(mParent) = aMachine->mParent;

    /* take the pointer to Data to share */
    mData.share(aMachine->mData);
    /*
     *  take the pointer to UserData to share
     *  (our UserData must always be the same as Machine's data)
     */
    mUserData.share(aMachine->mUserData);
    /* allocate private copies of all other data (will be loaded from settings) */
    mHWData.allocate();
    mMediumAttachments.allocate();
    mStorageControllers.allocate();
    mUSBControllers.allocate();

    /* SSData is always unique for SnapshotMachine */
    mSSData.allocate();
    mSSData->strStateFilePath = aStateFilePath;

    /* create all other child objects that will be immutable private copies */

    unconst(mPlatform).createObject();
    mPlatform->init(this);
    mPlatform->i_loadSettings(hardware.platformSettings); /* Needed for initializing the network adapters below (chipset type). */

    unconst(mPlatformProperties).createObject();
    mPlatformProperties->init(mParent);

    unconst(mFirmwareSettings).createObject();
    mFirmwareSettings->init(this);

    unconst(mRecordingSettings).createObject();
    mRecordingSettings->init(this);

    unconst(mTrustedPlatformModule).createObject();
    mTrustedPlatformModule->init(this);

    unconst(mNvramStore).createObject();
    mNvramStore->init(this);

    unconst(mGraphicsAdapter).createObject();
    mGraphicsAdapter->init(this);

    unconst(mVRDEServer).createObject();
    mVRDEServer->init(this);

    unconst(mAudioSettings).createObject();
    mAudioSettings->init(this);

    unconst(mUSBDeviceFilters).createObject();
    mUSBDeviceFilters->init(this);

    mNetworkAdapters.resize(PlatformProperties::s_getMaxNetworkAdapters(hardware.platformSettings.chipsetType));
    for (ULONG slot = 0; slot < mNetworkAdapters.size(); slot++)
    {
        unconst(mNetworkAdapters[slot]).createObject();
        mNetworkAdapters[slot]->init(this, slot);
    }

    for (ULONG slot = 0; slot < RT_ELEMENTS(mSerialPorts); slot++)
    {
        unconst(mSerialPorts[slot]).createObject();
        mSerialPorts[slot]->init(this, slot);
    }

    for (ULONG slot = 0; slot < RT_ELEMENTS(mParallelPorts); slot++)
    {
        unconst(mParallelPorts[slot]).createObject();
        mParallelPorts[slot]->init(this, slot);
    }

    unconst(mBandwidthControl).createObject();
    mBandwidthControl->init(this);

    unconst(mGuestDebugControl).createObject();
    mGuestDebugControl->init(this);

    /* load hardware and storage settings */
    HRESULT hrc = i_loadHardware(NULL, &mSnapshotId, hardware, pDbg, pAutostart, recording);
    if (SUCCEEDED(hrc))
        /* commit all changes made during the initialization */
        i_commit();   /// @todo r=dj why do we need a commit in init?!? this is very expensive
        /// @todo r=klaus for some reason the settings loading logic backs up
        // the settings, and therefore a commit is needed. Should probably be changed.

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 *  Uninitializes this SnapshotMachine object.
 */
void SnapshotMachine::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    uninitDataAndChildObjects();

    /* free the essential data structure last */
    mData.free();

    unconst(mMachine) = NULL;
    unconst(mParent) = NULL;
    unconst(mPeer) = NULL;

    LogFlowThisFuncLeave();
}

/**
 *  Overrides VirtualBoxBase::lockHandle() in order to share the lock handle
 *  with the primary Machine instance (mMachine) if it exists.
 */
RWLockHandle *SnapshotMachine::lockHandle() const
{
    AssertReturn(mMachine != NULL, NULL);
    return mMachine->lockHandle();
}

////////////////////////////////////////////////////////////////////////////////
//
// SnapshotMachine public internal methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 *  Called by the snapshot object associated with this SnapshotMachine when
 *  snapshot data such as name or description is changed.
 *
 *  @warning Caller must hold no locks when calling this.
 */
HRESULT SnapshotMachine::i_onSnapshotChange(Snapshot *aSnapshot)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock slock(aSnapshot COMMA_LOCKVAL_SRC_POS);
    Guid uuidMachine(mData->mUuid),
         uuidSnapshot(aSnapshot->i_getId());
    bool fNeedsGlobalSaveSettings = false;

    /* Flag the machine as dirty or change won't get saved. We disable the
     * modification of the current state flag, cause this snapshot data isn't
     * related to the current state. */
    mMachine->i_setModified(Machine::IsModified_Snapshots, false /* fAllowStateModification */);
    slock.release();
    HRESULT hrc = mMachine->i_saveSettings(&fNeedsGlobalSaveSettings,
                                           alock,
                                           SaveS_Force);        // we know we need saving, no need to check
    alock.release();

    if (SUCCEEDED(hrc) && fNeedsGlobalSaveSettings)
    {
        // save the global settings
        AutoWriteLock vboxlock(mParent COMMA_LOCKVAL_SRC_POS);
        hrc = mParent->i_saveSettings();
    }

    /* inform callbacks */
    mParent->i_onSnapshotChanged(uuidMachine, uuidSnapshot);

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// SessionMachine task records
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Still abstract base class for SessionMachine::TakeSnapshotTask,
 * SessionMachine::RestoreSnapshotTask and SessionMachine::DeleteSnapshotTask.
 */
class SessionMachine::SnapshotTask
    : public SessionMachine::Task
{
public:
    SnapshotTask(SessionMachine *m,
                 Progress *p,
                 const Utf8Str &t,
                 Snapshot *s)
        : Task(m, p, t),
          m_pSnapshot(s)
    {}

    ComObjPtr<Snapshot> m_pSnapshot;
};

/** Take snapshot task */
class SessionMachine::TakeSnapshotTask
    : public SessionMachine::SnapshotTask
{
public:
    TakeSnapshotTask(SessionMachine *m,
                     Progress *p,
                     const Utf8Str &t,
                     Snapshot *s,
                     const Utf8Str &strName,
                     const Utf8Str &strDescription,
                     const Guid &uuidSnapshot,
                     bool fPause,
                     uint32_t uMemSize,
                     bool fTakingSnapshotOnline)
        : SnapshotTask(m, p, t, s)
        , m_strName(strName)
        , m_strDescription(strDescription)
        , m_uuidSnapshot(uuidSnapshot)
        , m_fPause(fPause)
#if 0 /*unused*/
        , m_uMemSize(uMemSize)
#endif
        , m_fTakingSnapshotOnline(fTakingSnapshotOnline)
    {
        RT_NOREF(uMemSize);
        if (fTakingSnapshotOnline)
            m_pDirectControl = m->mData->mSession.mDirectControl;
        // If the VM is already paused then there's no point trying to pause
        // again during taking an (always online) snapshot.
        if (m_machineStateBackup == MachineState_Paused)
            m_fPause = false;
    }

private:
    void handler()
    {
        try
        {
            ((SessionMachine *)(Machine *)m_pMachine)->i_takeSnapshotHandler(*this);
        }
        catch(...)
        {
            LogRel(("Some exception in the function i_takeSnapshotHandler()\n"));
        }
    }

    Utf8Str m_strName;
    Utf8Str m_strDescription;
    Guid m_uuidSnapshot;
    Utf8Str m_strStateFilePath;
    ComPtr<IInternalSessionControl> m_pDirectControl;
    bool m_fPause;
#if 0 /*unused*/
    uint32_t m_uMemSize;
#endif
    bool m_fTakingSnapshotOnline;

    friend HRESULT SessionMachine::i_finishTakingSnapshot(TakeSnapshotTask &task, AutoWriteLock &alock, bool aSuccess);
    friend void SessionMachine::i_takeSnapshotHandler(TakeSnapshotTask &task);
    friend void SessionMachine::i_takeSnapshotProgressCancelCallback(void *pvUser);
};

/** Restore snapshot task */
class SessionMachine::RestoreSnapshotTask
    : public SessionMachine::SnapshotTask
{
public:
    RestoreSnapshotTask(SessionMachine *m,
                        Progress *p,
                        const Utf8Str &t,
                        Snapshot *s)
        : SnapshotTask(m, p, t, s)
    {}

private:
    void handler()
    {
        try
        {
            ((SessionMachine *)(Machine *)m_pMachine)->i_restoreSnapshotHandler(*this);
        }
        catch(...)
        {
            LogRel(("Some exception in the function i_restoreSnapshotHandler()\n"));
        }
    }
};

/** Delete snapshot task */
class SessionMachine::DeleteSnapshotTask
    : public SessionMachine::SnapshotTask
{
public:
    DeleteSnapshotTask(SessionMachine *m,
                       Progress *p,
                       const Utf8Str &t,
                       bool fDeleteOnline,
                       Snapshot *s)
        : SnapshotTask(m, p, t, s),
          m_fDeleteOnline(fDeleteOnline)
    {}

private:
    void handler()
    {
        try
        {
            ((SessionMachine *)(Machine *)m_pMachine)->i_deleteSnapshotHandler(*this);
        }
        catch(...)
        {
            LogRel(("Some exception in the function i_deleteSnapshotHandler()\n"));
        }
    }

    bool m_fDeleteOnline;
    friend void SessionMachine::i_deleteSnapshotHandler(DeleteSnapshotTask &task);
};


////////////////////////////////////////////////////////////////////////////////
//
// TakeSnapshot methods (Machine and related tasks)
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Machine::takeSnapshot(const com::Utf8Str &aName,
                              const com::Utf8Str &aDescription,
                              BOOL fPause,
                              com::Guid &aId,
                              ComPtr<IProgress> &aProgress)
{
    NOREF(aName);
    NOREF(aDescription);
    NOREF(fPause);
    NOREF(aId);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

HRESULT SessionMachine::takeSnapshot(const com::Utf8Str &aName,
                                     const com::Utf8Str &aDescription,
                                     BOOL fPause,
                                     com::Guid &aId,
                                     ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aName='%s' mMachineState=%d\n", aName.c_str(), mData->mMachineState));

    if (Global::IsTransient(mData->mMachineState))
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot take a snapshot of the virtual machine while it is changing state (machine state: %s)"),
                        Global::stringifyMachineState(mData->mMachineState));

    if (!fPause && mData->mMachineState != MachineState_Running)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot take a live snapshot of a virtual machine unless it is running."));

    HRESULT hrc = i_checkStateDependency(MutableOrSavedOrRunningStateDep);
    if (FAILED(hrc))
        return hrc;

    // prepare the progress object:
    // a) count the no. of hard disk attachments to get a matching no. of progress sub-operations
    ULONG cOperations = 2;              // always at least setting up + finishing up
    ULONG ulTotalOperationsWeight = 2;  // one each for setting up + finishing up

    for (MediumAttachmentList::iterator
         it = mMediumAttachments->begin();
         it != mMediumAttachments->end();
         ++it)
    {
        const ComObjPtr<MediumAttachment> pAtt(*it);
        AutoReadLock attlock(pAtt COMMA_LOCKVAL_SRC_POS);
        AutoCaller attCaller(pAtt);
        if (pAtt->i_getType() == DeviceType_HardDisk)
        {
            ++cOperations;

            // assume that creating a diff image takes as long as saving a 1MB state
            ulTotalOperationsWeight += 1;
        }
    }

    // b) one extra sub-operations for online snapshots OR offline snapshots that have a saved state (needs to be copied)
    const bool fTakingSnapshotOnline = Global::IsOnline(mData->mMachineState);
    LogFlowThisFunc(("fTakingSnapshotOnline = %d\n", fTakingSnapshotOnline));
    if (fTakingSnapshotOnline)
    {
        ++cOperations;
        ulTotalOperationsWeight += mHWData->mMemorySize;
    }

    // finally, create the progress object
    ComObjPtr<Progress> pProgress;
    pProgress.createObject();
    hrc = pProgress->init(mParent,
                          static_cast<IMachine *>(this),
                          Bstr(tr("Taking a snapshot of the virtual machine")).raw(),
                          fTakingSnapshotOnline /* aCancelable */,
                          cOperations,
                          ulTotalOperationsWeight,
                          Bstr(tr("Setting up snapshot operation")).raw(),      // first sub-op description
                          1);        // ulFirstOperationWeight
    if (FAILED(hrc))
        return hrc;

    /* create an ID for the snapshot */
    Guid snapshotId;
    snapshotId.create();

    /* create and start the task on a separate thread (note that it will not
     * start working until we release alock) */
    TakeSnapshotTask *pTask = new TakeSnapshotTask(this,
                                                   pProgress,
                                                   "TakeSnap",
                                                   NULL /* pSnapshot */,
                                                   aName,
                                                   aDescription,
                                                   snapshotId,
                                                   !!fPause,
                                                   mHWData->mMemorySize,
                                                   fTakingSnapshotOnline);
    MachineState_T const machineStateBackup = pTask->m_machineStateBackup;
    hrc = pTask->createThread();
    pTask = NULL;
    if (FAILED(hrc))
        return hrc;

    /* set the proper machine state (note: after creating a Task instance) */
    if (fTakingSnapshotOnline)
    {
        if (machineStateBackup != MachineState_Paused && !fPause)
            i_setMachineState(MachineState_LiveSnapshotting);
        else
            i_setMachineState(MachineState_OnlineSnapshotting);
        i_updateMachineStateOnClient();
    }
    else
        i_setMachineState(MachineState_Snapshotting);

    aId = snapshotId;
    pProgress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

/**
 * Task thread implementation for SessionMachine::TakeSnapshot(), called from
 * SessionMachine::taskHandler().
 *
 * @note Locks this object for writing.
 *
 * @param task
 */
void SessionMachine::i_takeSnapshotHandler(TakeSnapshotTask &task)
{
    LogFlowThisFuncEnter();

    // Taking a snapshot consists of the following:
    // 1) creating a Snapshot object with the current state of the machine
    //    (hardware + storage)
    // 2) creating a diff image for each virtual hard disk, into which write
    //    operations go after the snapshot has been created
    // 3) if the machine is online: saving the state of the virtual machine
    //    (in the VM process)
    // 4) reattach the hard disks
    // 5) update the various snapshot/machine objects, save settings

    HRESULT hrc = S_OK;
    AutoCaller autoCaller(this);
    LogFlowThisFunc(("state=%d\n", getObjectState().getState()));
    if (FAILED(autoCaller.hrc()))
    {
        /* we might have been uninitialized because the session was accidentally
         * closed by the client, so don't assert */
        hrc = setError(E_FAIL, tr("The session has been accidentally closed"));
        task.m_pProgress->i_notifyComplete(hrc);
        LogFlowThisFuncLeave();
        return;
    }

    LogRel(("Taking snapshot %s\n", task.m_strName.c_str()));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bool fBeganTakingSnapshot = false;
    BOOL fSuspendedBySave     = FALSE;

    std::set<ComObjPtr<Medium> > pMediaForNotify;
    std::map<Guid, DeviceType_T> uIdsForNotify;

    try
    {
        /// @todo at this point we have to be in the right state!!!!
        AssertStmt(   mData->mMachineState == MachineState_Snapshotting
                   || mData->mMachineState == MachineState_OnlineSnapshotting
                   || mData->mMachineState == MachineState_LiveSnapshotting, throw E_FAIL);
        AssertStmt(task.m_machineStateBackup != mData->mMachineState, throw E_FAIL);
        AssertStmt(task.m_pSnapshot.isNull(), throw E_FAIL);

        if (   mData->mCurrentSnapshot
            && mData->mCurrentSnapshot->i_getDepth() >= SETTINGS_SNAPSHOT_DEPTH_MAX)
        {
            throw setError(VBOX_E_INVALID_OBJECT_STATE,
                           tr("Cannot take another snapshot for machine '%s', because it exceeds the maximum snapshot depth limit. Please delete some earlier snapshot which you no longer need"),
                           mUserData->s.strName.c_str());
        }

        /* save settings to ensure current changes are committed and
         * hard disks are fixed up */
        hrc = i_saveSettings(NULL, alock); /******************1 */
            // no need to check for whether VirtualBox.xml needs changing since
            // we can't have a machine XML rename pending at this point
        if (FAILED(hrc))
            throw hrc;

        /* task.m_strStateFilePath is "" when the machine is offline or saved */
        if (task.m_fTakingSnapshotOnline)
        {
            Bstr value;
            hrc = GetExtraData(Bstr("VBoxInternal2/ForceTakeSnapshotWithoutState").raw(), value.asOutParam());
            if (FAILED(hrc) || value != "1")
                // creating a new online snapshot: we need a fresh saved state file
                i_composeSavedStateFilename(task.m_strStateFilePath);
        }
        else if (task.m_machineStateBackup == MachineState_Saved || task.m_machineStateBackup == MachineState_AbortedSaved)
            // taking an offline snapshot from machine in "saved" state: use existing state file
            task.m_strStateFilePath = mSSData->strStateFilePath;

        if (task.m_strStateFilePath.isNotEmpty())
        {
            // ensure the directory for the saved state file exists
            hrc = VirtualBox::i_ensureFilePathExists(task.m_strStateFilePath, true /* fCreate */);
            if (FAILED(hrc))
                throw hrc;
        }

        /* STEP 1: create the snapshot object */

        /* create a snapshot machine object */
        ComObjPtr<SnapshotMachine> pSnapshotMachine;
        pSnapshotMachine.createObject();
        hrc = pSnapshotMachine->init(this, task.m_uuidSnapshot.ref(), task.m_strStateFilePath);
        AssertComRCThrowRC(hrc);

        /* create a snapshot object */
        RTTIMESPEC time;
        RTTimeNow(&time);
        task.m_pSnapshot.createObject();
        hrc = task.m_pSnapshot->init(mParent,
                                     task.m_uuidSnapshot,
                                     task.m_strName,
                                     task.m_strDescription,
                                     time,
                                     pSnapshotMachine,
                                     mData->mCurrentSnapshot);
        AssertComRCThrowRC(hrc);

        /* STEP 2: create the diff images */
        LogFlowThisFunc(("Creating differencing hard disks (online=%d)...\n", task.m_fTakingSnapshotOnline));

        // Backup the media data so we can recover if something goes wrong.
        // The matching commit() is in fixupMedia() during SessionMachine::i_finishTakingSnapshot()
        i_setModified(IsModified_Storage);
        mMediumAttachments.backup();

        alock.release();
        /* create new differencing hard disks and attach them to this machine */
        hrc = i_createImplicitDiffs(task.m_pProgress,
                                    1,            // operation weight; must be the same as in Machine::TakeSnapshot()
                                    task.m_fTakingSnapshotOnline);
        if (FAILED(hrc))
            throw hrc;
        alock.acquire();

        // MUST NOT save the settings or the media registry here, because
        // this causes trouble with rolling back settings if the user cancels
        // taking the snapshot after the diff images have been created.

        fBeganTakingSnapshot = true;

        // STEP 3: save the VM state (if online)
        if (task.m_fTakingSnapshotOnline)
        {
            task.m_pProgress->SetNextOperation(Bstr(tr("Saving the machine state")).raw(),
                                               mHWData->mMemorySize);   // operation weight, same as computed
                                                                        // when setting up progress object

            if (task.m_strStateFilePath.isNotEmpty())
            {
                alock.release();
                task.m_pProgress->i_setCancelCallback(i_takeSnapshotProgressCancelCallback, &task);
                hrc = task.m_pDirectControl->SaveStateWithReason(Reason_Snapshot,
                                                                 task.m_pProgress,
                                                                 task.m_pSnapshot,
                                                                 Bstr(task.m_strStateFilePath).raw(),
                                                                 task.m_fPause,
                                                                 &fSuspendedBySave);
                task.m_pProgress->i_setCancelCallback(NULL, NULL);
                alock.acquire();
                if (FAILED(hrc))
                    throw hrc;
            }
            else
                LogRel(("Machine: skipped saving state as part of online snapshot\n"));

            if (FAILED(task.m_pProgress->NotifyPointOfNoReturn()))
                throw setError(E_FAIL, tr("Canceled"));

            // STEP 4: reattach hard disks
            LogFlowThisFunc(("Reattaching new differencing hard disks...\n"));

            task.m_pProgress->SetNextOperation(Bstr(tr("Reconfiguring medium attachments")).raw(),
                                               1);       // operation weight, same as computed when setting up progress object

            com::SafeIfaceArray<IMediumAttachment> atts;
            hrc = COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(atts));
            if (FAILED(hrc))
                throw hrc;

            alock.release();
            hrc = task.m_pDirectControl->ReconfigureMediumAttachments(ComSafeArrayAsInParam(atts));
            alock.acquire();
            if (FAILED(hrc))
                throw hrc;
        }

        // Handle NVRAM file snapshotting
        Utf8Str strNVRAM = mNvramStore->i_getNonVolatileStorageFile();
        Utf8Str strNVRAMSnap = pSnapshotMachine->i_getSnapshotNVRAMFilename();
        if (   strNVRAM.isNotEmpty()
            && strNVRAMSnap.isNotEmpty()
            && RTFileExists(strNVRAM.c_str())
            && mFirmwareSettings->i_getFirmwareType() != FirmwareType_BIOS)
        {
            Utf8Str strNVRAMSnapAbs;
            i_calculateFullPath(strNVRAMSnap, strNVRAMSnapAbs);
            hrc = VirtualBox::i_ensureFilePathExists(strNVRAMSnapAbs, true /* fCreate */);
            if (FAILED(hrc))
                throw hrc;
            int vrc = RTFileCopy(strNVRAM.c_str(), strNVRAMSnapAbs.c_str());
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Could not copy NVRAM file '%s' to '%s' (%Rrc)"),
                                   strNVRAM.c_str(), strNVRAMSnapAbs.c_str(), vrc);
            pSnapshotMachine->mNvramStore->i_updateNonVolatileStorageFile(strNVRAMSnap);
        }

        // store parent of newly created diffs before commit for notify
        {
            MediumAttachmentList &oldAtts = *mMediumAttachments.backedUpData();
            for (MediumAttachmentList::const_iterator
                 it = mMediumAttachments->begin();
                 it != mMediumAttachments->end();
                 ++it)
            {
                MediumAttachment *pAttach = *it;
                Medium *pMedium = pAttach->i_getMedium();
                if (!pMedium)
                    continue;

                bool fFound = false;
                /* was this medium attached before? */
                for (MediumAttachmentList::iterator
                     oldIt = oldAtts.begin();
                     oldIt != oldAtts.end();
                     ++oldIt)
                {
                    MediumAttachment *pOldAttach = *oldIt;
                    if (pOldAttach->i_getMedium() == pMedium)
                    {
                        fFound = true;
                        break;
                    }
                }
                if (!fFound)
                {
                    pMediaForNotify.insert(pMedium->i_getParent());
                    uIdsForNotify[pMedium->i_getId()] = pMedium->i_getDeviceType();
                }
            }
        }

        /*
         * Finalize the requested snapshot object. This will reset the
         * machine state to the state it had at the beginning.
         */
        hrc = i_finishTakingSnapshot(task, alock, true /*aSuccess*/); /*******************2+3 */
        // do not throw hrc here because we can't call i_finishTakingSnapshot() twice
        LogFlowThisFunc(("i_finishTakingSnapshot -> %Rhrc [mMachineState=%s]\n", hrc, ::stringifyMachineState(mData->mMachineState)));
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
        LogThisFunc(("Caught %Rhrc [mMachineState=%s]\n", hrc, ::stringifyMachineState(mData->mMachineState)));

        /// @todo r=klaus check that the implicit diffs created above are cleaned up im the relevant error cases

        /* preserve existing error info */
        ErrorInfoKeeper eik;

        if (fBeganTakingSnapshot)
            i_finishTakingSnapshot(task, alock, false /*aSuccess*/);

        // have to postpone this to the end as i_finishTakingSnapshot() needs
        // it for various cleanup steps
        if (task.m_pSnapshot)
        {
            task.m_pSnapshot->uninit();
            task.m_pSnapshot.setNull();
        }
    }
    Assert(alock.isWriteLockOnCurrentThread());

    {
        // Keep all error information over the cleanup steps
        ErrorInfoKeeper eik;

        /*
         * Fix up the machine state.
         *
         * For offline snapshots we just update the local copy, for the other
         * variants do the entire work. This ensures that the state is in sync
         * with the VM process (in particular the VM execution state).
         */
        bool fNeedClientMachineStateUpdate = false;
        if (   mData->mMachineState == MachineState_LiveSnapshotting
            || mData->mMachineState == MachineState_OnlineSnapshotting
            || mData->mMachineState == MachineState_Snapshotting)
        {
            if (!task.m_fTakingSnapshotOnline)
                i_setMachineState(task.m_machineStateBackup); /**************** 4 Machine::i_saveStateSettings*/
            else
            {
                MachineState_T enmMachineState = MachineState_Null;
                HRESULT hrc2 = task.m_pDirectControl->COMGETTER(NominalState)(&enmMachineState);
                if (FAILED(hrc2) || enmMachineState == MachineState_Null)
                {
                    AssertMsgFailed(("state=%s\n", ::stringifyMachineState(enmMachineState)));
                    // pure nonsense, try to continue somehow
                    enmMachineState = MachineState_Aborted;
                }
                if (enmMachineState == MachineState_Paused)
                {
                    if (fSuspendedBySave)
                    {
                        alock.release();
                        hrc2 = task.m_pDirectControl->ResumeWithReason(Reason_Snapshot);
                        alock.acquire();
                        if (SUCCEEDED(hrc2))
                            enmMachineState = task.m_machineStateBackup;
                    }
                    else
                        enmMachineState = task.m_machineStateBackup;
                }
                if (enmMachineState != mData->mMachineState)
                {
                    fNeedClientMachineStateUpdate = true;
                    i_setMachineState(enmMachineState);
                }
            }
        }

        /* check the remote state to see that we got it right. */
        MachineState_T enmMachineState = MachineState_Null;
        if (!task.m_pDirectControl.isNull())
        {
            ComPtr<IConsole> pConsole;
            task.m_pDirectControl->COMGETTER(RemoteConsole)(pConsole.asOutParam());
            if (!pConsole.isNull())
                pConsole->COMGETTER(State)(&enmMachineState);
        }
        LogFlowThisFunc(("local mMachineState=%s remote mMachineState=%s\n",
                         ::stringifyMachineState(mData->mMachineState), ::stringifyMachineState(enmMachineState)));

        if (fNeedClientMachineStateUpdate)
            i_updateMachineStateOnClient();
    }

    task.m_pProgress->i_notifyComplete(hrc);

    if (SUCCEEDED(hrc))
        mParent->i_onSnapshotTaken(mData->mUuid, task.m_uuidSnapshot);

    if (SUCCEEDED(hrc))
    {
        for (std::map<Guid, DeviceType_T>::const_iterator it = uIdsForNotify.begin();
             it != uIdsForNotify.end();
             ++it)
        {
            mParent->i_onMediumRegistered(it->first, it->second, TRUE);
        }

        for (std::set<ComObjPtr<Medium> >::const_iterator it = pMediaForNotify.begin();
             it != pMediaForNotify.end();
             ++it)
        {
            if (it->isNotNull())
                mParent->i_onMediumConfigChanged(*it);
        }
    }
    LogRel(("Finished taking snapshot %s\n", task.m_strName.c_str()));
    LogFlowThisFuncLeave();
}


/**
 * Progress cancelation callback employed by SessionMachine::i_takeSnapshotHandler.
 */
/*static*/
void SessionMachine::i_takeSnapshotProgressCancelCallback(void *pvUser)
{
    TakeSnapshotTask *pTask = (TakeSnapshotTask *)pvUser;
    AssertPtrReturnVoid(pTask);
    AssertReturnVoid(!pTask->m_pDirectControl.isNull());
    pTask->m_pDirectControl->CancelSaveStateWithReason();
}


/**
 * Called by the Console when it's done saving the VM state into the snapshot
 * (if online) and reconfiguring the hard disks. See BeginTakingSnapshot() above.
 *
 * This also gets called if the console part of snapshotting failed after the
 * BeginTakingSnapshot() call, to clean up the server side.
 *
 * @note Locks VirtualBox and this object for writing.
 *
 * @param   task
 * @param   alock
 * @param   aSuccess    Whether Console was successful with the client-side
 *                      snapshot things.
 * @return
 */
HRESULT SessionMachine::i_finishTakingSnapshot(TakeSnapshotTask &task, AutoWriteLock &alock, bool aSuccess)
{
    LogFlowThisFunc(("\n"));

    Assert(alock.isWriteLockOnCurrentThread());

    AssertReturn(   !aSuccess
                 || mData->mMachineState == MachineState_Snapshotting
                 || mData->mMachineState == MachineState_OnlineSnapshotting
                 || mData->mMachineState == MachineState_LiveSnapshotting, E_FAIL);

    ComObjPtr<Snapshot> pOldFirstSnap = mData->mFirstSnapshot;
    ComObjPtr<Snapshot> pOldCurrentSnap = mData->mCurrentSnapshot;

    HRESULT hrc = S_OK;

    if (aSuccess)
    {
        // new snapshot becomes the current one
        mData->mCurrentSnapshot = task.m_pSnapshot;

        /* memorize the first snapshot if necessary */
        if (!mData->mFirstSnapshot)
            mData->mFirstSnapshot = mData->mCurrentSnapshot;

        int flSaveSettings = SaveS_Force;       // do not do a deep compare in machine settings,
                                                // snapshots change, so we know we need to save
        if (!task.m_fTakingSnapshotOnline)
            /* the machine was powered off or saved when taking a snapshot, so
             * reset the mCurrentStateModified flag */
            flSaveSettings |= SaveS_ResetCurStateModified;

        hrc = i_saveSettings(NULL, alock, flSaveSettings); /******************2 */
    }

    if (aSuccess && SUCCEEDED(hrc))
    {
        /* associate old hard disks with the snapshot and do locking/unlocking*/
        i_commitMedia(task.m_fTakingSnapshotOnline);
        alock.release();
    }
    else
    {
        /* delete all differencing hard disks created (this will also attach
         * their parents back by rolling back mMediaData) */
        alock.release();

        i_rollbackMedia();

        mData->mFirstSnapshot = pOldFirstSnap;      // might have been changed above
        mData->mCurrentSnapshot = pOldCurrentSnap;      // might have been changed above

        // delete the saved state file (it might have been already created)
        if (task.m_fTakingSnapshotOnline)
            // no need to test for whether the saved state file is shared: an online
            // snapshot means that a new saved state file was created, which we must
            // clean up now
            RTFileDelete(task.m_pSnapshot->i_getStateFilePath().c_str());

        alock.acquire();

        task.m_pSnapshot->uninit();
        alock.release();

    }

    /* clear out the snapshot data */
    task.m_pSnapshot.setNull();

    /* alock has been released already */
    mParent->i_saveModifiedRegistries(); /**************3 */

    alock.acquire();

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// RestoreSnapshot methods (Machine and related tasks)
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Machine::restoreSnapshot(const ComPtr<ISnapshot> &aSnapshot,
                                 ComPtr<IProgress> &aProgress)
{
    NOREF(aSnapshot);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

/**
 * Restoring a snapshot happens entirely on the server side, the machine cannot be running.
 *
 * This creates a new thread that does the work and returns a progress object to the client.
 * Actual work then takes place in RestoreSnapshotTask::handler().
 *
 * @note Locks this + children objects for writing!
 *
 * @param aSnapshot in: the snapshot to restore.
 * @param aProgress out: progress object to monitor restore thread.
 * @return
 */
HRESULT SessionMachine::restoreSnapshot(const ComPtr<ISnapshot> &aSnapshot,
                                        ComPtr<IProgress> &aProgress)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // machine must not be running
    if (Global::IsOnlineOrTransient(mData->mMachineState))
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot delete the current state of the running machine (machine state: %s)"),
                        Global::stringifyMachineState(mData->mMachineState));

    HRESULT hrc = i_checkStateDependency(MutableOrSavedStateDep);
    if (FAILED(hrc))
        return hrc;

    /* We need to explicitly check if the given snapshot is valid and bail out if not. */
    if (aSnapshot.isNull())
    {
        if (aSnapshot == mData->mCurrentSnapshot)
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("This VM does not have any current snapshot"));

        return setError(E_INVALIDARG, tr("The given snapshot is invalid"));
    }

    ISnapshot* iSnapshot = aSnapshot;
    ComObjPtr<Snapshot> pSnapshot(static_cast<Snapshot*>(iSnapshot));
    ComObjPtr<SnapshotMachine> pSnapMachine = pSnapshot->i_getSnapshotMachine();

    // create a progress object. The number of operations is:
    // 1 (preparing) + # of hard disks + 1 (if we need to copy the saved state file) */
    LogFlowThisFunc(("Going thru snapshot machine attachments to determine progress setup\n"));

    ULONG ulOpCount = 1;            // one for preparations
    ULONG ulTotalWeight = 1;        // one for preparations
    for (MediumAttachmentList::iterator
         it = pSnapMachine->mMediumAttachments->begin();
         it != pSnapMachine->mMediumAttachments->end();
         ++it)
    {
        ComObjPtr<MediumAttachment> &pAttach = *it;
        AutoReadLock attachLock(pAttach COMMA_LOCKVAL_SRC_POS);
        if (pAttach->i_getType() == DeviceType_HardDisk)
        {
            ++ulOpCount;
            ++ulTotalWeight;         // assume one MB weight for each differencing hard disk to manage
            Assert(pAttach->i_getMedium());
            LogFlowThisFunc(("op %d: considering hard disk attachment %s\n", ulOpCount,
                             pAttach->i_getMedium()->i_getName().c_str()));
        }
    }

    ComObjPtr<Progress> pProgress;
    pProgress.createObject();
    pProgress->init(mParent, static_cast<IMachine*>(this),
                    BstrFmt(tr("Restoring snapshot '%s'"), pSnapshot->i_getName().c_str()).raw(),
                    FALSE /* aCancelable */,
                    ulOpCount,
                    ulTotalWeight,
                    Bstr(tr("Restoring machine settings")).raw(),
                    1);

    /* create and start the task on a separate thread (note that it will not
     * start working until we release alock) */
    RestoreSnapshotTask *pTask = new RestoreSnapshotTask(this,
                                                         pProgress,
                                                         "RestoreSnap",
                                                         pSnapshot);
    hrc = pTask->createThread();
    pTask = NULL;
    if (FAILED(hrc))
        return hrc;

    /* set the proper machine state (note: after creating a Task instance) */
    i_setMachineState(MachineState_RestoringSnapshot);

    /* return the progress to the caller */
    pProgress.queryInterfaceTo(aProgress.asOutParam());

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 * Worker method for the restore snapshot thread created by SessionMachine::RestoreSnapshot().
 * This method gets called indirectly through SessionMachine::taskHandler() which then
 * calls RestoreSnapshotTask::handler().
 *
 * The RestoreSnapshotTask contains the progress object returned to the console by
 * SessionMachine::RestoreSnapshot, through which progress and results are reported.
 *
 * @note Locks mParent + this object for writing.
 *
 * @param task Task data.
 */
void SessionMachine::i_restoreSnapshotHandler(RestoreSnapshotTask &task)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);

    LogFlowThisFunc(("state=%d\n", getObjectState().getState()));
    if (!autoCaller.isOk())
    {
        /* we might have been uninitialized because the session was accidentally
         * closed by the client, so don't assert */
        task.m_pProgress->i_notifyComplete(E_FAIL,
                                           COM_IIDOF(IMachine),
                                           getComponentName(),
                                           tr("The session has been accidentally closed"));

        LogFlowThisFuncLeave();
        return;
    }

    HRESULT hrc = S_OK;
    Guid snapshotId;
    std::set<ComObjPtr<Medium> > pMediaForNotify;
    std::map<Guid, std::pair<DeviceType_T, BOOL> > uIdsForNotify;

    try
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* Discard all current changes to mUserData (name, OSType etc.).
         * Note that the machine is powered off, so there is no need to inform
         * the direct session. */
        if (mData->flModifications)
            i_rollback(false /* aNotify */);

        /* Delete the saved state file if the machine was Saved prior to this
         * operation */
        if (task.m_machineStateBackup == MachineState_Saved || task.m_machineStateBackup == MachineState_AbortedSaved)
        {
            Assert(!mSSData->strStateFilePath.isEmpty());

            // release the saved state file AFTER unsetting the member variable
            // so that releaseSavedStateFile() won't think it's still in use
            Utf8Str strStateFile(mSSData->strStateFilePath);
            mSSData->strStateFilePath.setNull();
            i_releaseSavedStateFile(strStateFile, NULL /* pSnapshotToIgnore */ );

            task.modifyBackedUpState(MachineState_PoweredOff);

            hrc = i_saveStateSettings(SaveSTS_StateFilePath);
            if (FAILED(hrc))
                throw hrc;
        }

        RTTIMESPEC snapshotTimeStamp;
        RTTimeSpecSetMilli(&snapshotTimeStamp, 0);

        {
            AutoReadLock snapshotLock(task.m_pSnapshot COMMA_LOCKVAL_SRC_POS);

            /* remember the timestamp of the snapshot we're restoring from */
            snapshotTimeStamp = task.m_pSnapshot->i_getTimeStamp();

            // save the snapshot ID (paranoia, here we hold the lock)
            snapshotId = task.m_pSnapshot->i_getId();

            ComPtr<SnapshotMachine> pSnapshotMachine(task.m_pSnapshot->i_getSnapshotMachine());

            /* copy all hardware data from the snapshot */
            i_copyFrom(pSnapshotMachine);

            LogFlowThisFunc(("Restoring hard disks from the snapshot...\n"));

            // restore the attachments from the snapshot
            i_setModified(IsModified_Storage);
            mMediumAttachments.backup();
            mMediumAttachments->clear();
            for (MediumAttachmentList::const_iterator
                 it = pSnapshotMachine->mMediumAttachments->begin();
                 it != pSnapshotMachine->mMediumAttachments->end();
                 ++it)
            {
                ComObjPtr<MediumAttachment> pAttach;
                pAttach.createObject();
                pAttach->initCopy(this, *it);
                mMediumAttachments->push_back(pAttach);
            }

            /* release the locks before the potentially lengthy operation */
            snapshotLock.release();
            alock.release();

            hrc = i_createImplicitDiffs(task.m_pProgress, 1, false /* aOnline */);
            if (FAILED(hrc))
                throw hrc;

            alock.acquire();
            snapshotLock.acquire();

            /* Note: on success, current (old) hard disks will be
             * deassociated/deleted on #commit() called from #i_saveSettings() at
             * the end. On failure, newly created implicit diffs will be
             * deleted by #rollback() at the end. */

            /* should not have a saved state file associated at this point */
            Assert(mSSData->strStateFilePath.isEmpty());

            const Utf8Str &strSnapshotStateFile = task.m_pSnapshot->i_getStateFilePath();

            if (strSnapshotStateFile.isNotEmpty())
                // online snapshot: then share the state file
                mSSData->strStateFilePath = strSnapshotStateFile;

            const Utf8Str srcNVRAM(pSnapshotMachine->mNvramStore->i_getNonVolatileStorageFile());
            const Utf8Str dstNVRAM(mNvramStore->i_getNonVolatileStorageFile());
            if (dstNVRAM.isNotEmpty() && RTFileExists(dstNVRAM.c_str()))
                RTFileDelete(dstNVRAM.c_str());
            if (srcNVRAM.isNotEmpty() && dstNVRAM.isNotEmpty() && RTFileExists(srcNVRAM.c_str()))
                RTFileCopy(srcNVRAM.c_str(), dstNVRAM.c_str());

            LogFlowThisFunc(("Setting new current snapshot {%RTuuid}\n", task.m_pSnapshot->i_getId().raw()));
            /* make the snapshot we restored from the current snapshot */
            mData->mCurrentSnapshot = task.m_pSnapshot;
        }

        // store parent of newly created diffs for notify
        {
            MediumAttachmentList &oldAtts = *mMediumAttachments.backedUpData();
            for (MediumAttachmentList::const_iterator
                 it = mMediumAttachments->begin();
                 it != mMediumAttachments->end();
                 ++it)
            {
                MediumAttachment *pAttach = *it;
                Medium *pMedium = pAttach->i_getMedium();
                if (!pMedium)
                    continue;

                bool fFound = false;
                /* was this medium attached before? */
                for (MediumAttachmentList::iterator
                     oldIt = oldAtts.begin();
                     oldIt != oldAtts.end();
                     ++oldIt)
                {
                    MediumAttachment *pOldAttach = *oldIt;
                    if (pOldAttach->i_getMedium() == pMedium)
                    {
                        fFound = true;
                        break;
                    }
                }
                if (!fFound)
                {
                    pMediaForNotify.insert(pMedium->i_getParent());
                    uIdsForNotify[pMedium->i_getId()] = std::pair<DeviceType_T, BOOL>(pMedium->i_getDeviceType(), TRUE);
                }
            }
        }

        /* grab differencing hard disks from the old attachments that will
         * become unused and need to be auto-deleted */
        std::list< ComObjPtr<MediumAttachment> > llDiffAttachmentsToDelete;

        for (MediumAttachmentList::const_iterator
             it = mMediumAttachments.backedUpData()->begin();
             it != mMediumAttachments.backedUpData()->end();
             ++it)
        {
            ComObjPtr<MediumAttachment> pAttach = *it;
            ComObjPtr<Medium> pMedium = pAttach->i_getMedium();

            /* while the hard disk is attached, the number of children or the
             * parent cannot change, so no lock */
            if (    !pMedium.isNull()
                 && pAttach->i_getType() == DeviceType_HardDisk
                 && !pMedium->i_getParent().isNull()
                 && pMedium->i_getChildren().size() == 0
               )
            {
                LogFlowThisFunc(("Picked differencing image '%s' for deletion\n", pMedium->i_getName().c_str()));

                llDiffAttachmentsToDelete.push_back(pAttach);
            }
        }

        /* we have already deleted the current state, so set the execution
         * state accordingly no matter of the delete snapshot result */
        if (mSSData->strStateFilePath.isNotEmpty())
            task.modifyBackedUpState(MachineState_Saved);
        else
            task.modifyBackedUpState(MachineState_PoweredOff);

        /* Paranoia: no one must have saved the settings in the mean time. If
         * it happens nevertheless we'll close our eyes and continue below. */
        Assert(mMediumAttachments.isBackedUp());

        /* assign the timestamp from the snapshot */
        Assert(RTTimeSpecGetMilli(&snapshotTimeStamp) != 0);
        mData->mLastStateChange = snapshotTimeStamp;

        // detach the current-state diffs that we detected above and build a list of
        // image files to delete _after_ i_saveSettings()

        MediaList llDiffsToDelete;

        for (std::list< ComObjPtr<MediumAttachment> >::iterator it = llDiffAttachmentsToDelete.begin();
             it != llDiffAttachmentsToDelete.end();
             ++it)
        {
            ComObjPtr<MediumAttachment> pAttach = *it;        // guaranteed to have only attachments where medium != NULL
            ComObjPtr<Medium> pMedium = pAttach->i_getMedium();

            AutoWriteLock mlock(pMedium COMMA_LOCKVAL_SRC_POS);

            LogFlowThisFunc(("Detaching old current state in differencing image '%s'\n", pMedium->i_getName().c_str()));

            // Normally we "detach" the medium by removing the attachment object
            // from the current machine data; i_saveSettings() below would then
            // compare the current machine data with the one in the backup
            // and actually call Medium::removeBackReference(). But that works only half
            // the time in our case so instead we force a detachment here:
            // remove from machine data
            mMediumAttachments->remove(pAttach);
            // Remove it from the backup or else i_saveSettings will try to detach
            // it again and assert. The paranoia check avoids crashes (see
            // assert above) if this code is buggy and saves settings in the
            // wrong place.
            if (mMediumAttachments.isBackedUp())
                mMediumAttachments.backedUpData()->remove(pAttach);
            // then clean up backrefs
            pMedium->i_removeBackReference(mData->mUuid);

            llDiffsToDelete.push_back(pMedium);
        }

        // save machine settings, reset the modified flag and commit;
        bool fNeedsGlobalSaveSettings = false;
        hrc = i_saveSettings(&fNeedsGlobalSaveSettings, alock, SaveS_ResetCurStateModified);
        if (FAILED(hrc))
            throw hrc;

        // release the locks before updating registry and deleting image files
        alock.release();

        // unconditionally add the parent registry.
        mParent->i_markRegistryModified(mParent->i_getGlobalRegistryId());

        // from here on we cannot roll back on failure any more

        for (MediaList::iterator it = llDiffsToDelete.begin();
             it != llDiffsToDelete.end();
             ++it)
        {
            ComObjPtr<Medium> &pMedium = *it;
            LogFlowThisFunc(("Deleting old current state in differencing image '%s'\n", pMedium->i_getName().c_str()));

            ComObjPtr<Medium> pParent = pMedium->i_getParent();
            // store the id here because it becomes NULL after deleting storage.
            com::Guid id = pMedium->i_getId();
            HRESULT hrc2 = pMedium->i_deleteStorage(NULL /* aProgress */, true /* aWait */, false /* aNotify */);
            // ignore errors here because we cannot roll back after i_saveSettings() above
            if (SUCCEEDED(hrc2))
            {
                pMediaForNotify.insert(pParent);
                uIdsForNotify[id] = std::pair<DeviceType_T, BOOL>(pMedium->i_getDeviceType(), FALSE);
                pMedium->uninit();
            }
        }
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    if (FAILED(hrc))
    {
        /* preserve existing error info */
        ErrorInfoKeeper eik;

        /* undo all changes on failure */
        i_rollback(false /* aNotify */);

    }

    mParent->i_saveModifiedRegistries();

    /* restore the machine state */
    i_setMachineState(task.m_machineStateBackup);

    /* set the result (this will try to fetch current error info on failure) */
    task.m_pProgress->i_notifyComplete(hrc);

    if (SUCCEEDED(hrc))
    {
        mParent->i_onSnapshotRestored(mData->mUuid, snapshotId);
        for (std::map<Guid, std::pair<DeviceType_T, BOOL> >::const_iterator it = uIdsForNotify.begin();
             it != uIdsForNotify.end();
             ++it)
        {
            mParent->i_onMediumRegistered(it->first, it->second.first, it->second.second);
        }
        for (std::set<ComObjPtr<Medium> >::const_iterator it = pMediaForNotify.begin();
             it != pMediaForNotify.end();
             ++it)
        {
            if (it->isNotNull())
                mParent->i_onMediumConfigChanged(*it);
        }
    }

    LogFlowThisFunc(("Done restoring snapshot (hrc=%08X)\n", hrc));

    LogFlowThisFuncLeave();
}

////////////////////////////////////////////////////////////////////////////////
//
// DeleteSnapshot methods (SessionMachine and related tasks)
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Machine::deleteSnapshot(const com::Guid &aId, ComPtr<IProgress> &aProgress)
{
    NOREF(aId);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

HRESULT SessionMachine::deleteSnapshot(const com::Guid &aId, ComPtr<IProgress> &aProgress)
{
    return i_deleteSnapshot(aId, aId,
                            FALSE /* fDeleteAllChildren */,
                            aProgress);
}

HRESULT Machine::deleteSnapshotAndAllChildren(const com::Guid &aId, ComPtr<IProgress> &aProgress)
{
    NOREF(aId);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

HRESULT SessionMachine::deleteSnapshotAndAllChildren(const com::Guid &aId, ComPtr<IProgress> &aProgress)
{
    return i_deleteSnapshot(aId, aId,
                            TRUE /* fDeleteAllChildren */,
                            aProgress);
}

HRESULT Machine::deleteSnapshotRange(const com::Guid &aStartId, const com::Guid &aEndId, ComPtr<IProgress> &aProgress)
{
    NOREF(aStartId);
    NOREF(aEndId);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

HRESULT SessionMachine::deleteSnapshotRange(const com::Guid &aStartId, const com::Guid &aEndId, ComPtr<IProgress> &aProgress)
{
    return i_deleteSnapshot(aStartId, aEndId,
                            FALSE /* fDeleteAllChildren */,
                            aProgress);
}


/**
 * Implementation for SessionMachine::i_deleteSnapshot().
 *
 * Gets called from SessionMachine::DeleteSnapshot(). Deleting a snapshot
 * happens entirely on the server side if the machine is not running, and
 * if it is running then the merges are done via internal session callbacks.
 *
 * This creates a new thread that does the work and returns a progress
 * object to the client.
 *
 * Actual work then takes place in SessionMachine::i_deleteSnapshotHandler().
 *
 * @note Locks mParent + this + children objects for writing!
 */
HRESULT SessionMachine::i_deleteSnapshot(const com::Guid &aStartId,
                                         const com::Guid &aEndId,
                                         BOOL aDeleteAllChildren,
                                         ComPtr<IProgress> &aProgress)
{
    LogFlowThisFuncEnter();

    AssertReturn(!aStartId.isZero() && !aEndId.isZero() && aStartId.isValid() && aEndId.isValid(), E_INVALIDARG);

    /** @todo implement the "and all children" and "range" variants */
    if (aDeleteAllChildren || aStartId != aEndId)
        ReturnComNotImplemented();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (Global::IsTransient(mData->mMachineState))
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot delete a snapshot of the machine while it is changing the state (machine state: %s)"),
                        Global::stringifyMachineState(mData->mMachineState));

    // be very picky about machine states
    if (   Global::IsOnlineOrTransient(mData->mMachineState)
        && mData->mMachineState != MachineState_PoweredOff
        && mData->mMachineState != MachineState_Saved
        && mData->mMachineState != MachineState_Teleported
        && mData->mMachineState != MachineState_Aborted
        && mData->mMachineState != MachineState_AbortedSaved
        && mData->mMachineState != MachineState_Running
        && mData->mMachineState != MachineState_Paused)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Invalid machine state: %s"),
                        Global::stringifyMachineState(mData->mMachineState));

    HRESULT hrc = i_checkStateDependency(MutableOrSavedOrRunningStateDep);
    if (FAILED(hrc))
        return hrc;

    ComObjPtr<Snapshot> pSnapshot;
    hrc = i_findSnapshotById(aStartId, pSnapshot, true /* aSetError */);
    if (FAILED(hrc))
        return hrc;

    AutoWriteLock snapshotLock(pSnapshot COMMA_LOCKVAL_SRC_POS);
    Utf8Str str;

    size_t childrenCount = pSnapshot->i_getChildrenCount();
    if (childrenCount > 1)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Snapshot '%s' of the machine '%s' cannot be deleted, because it has %d child snapshots, which is more than the one snapshot allowed for deletion",
                           "", childrenCount),
                        pSnapshot->i_getName().c_str(),
                        mUserData->s.strName.c_str(),
                        childrenCount);

    if (pSnapshot == mData->mCurrentSnapshot && childrenCount >= 1)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("Snapshot '%s' of the machine '%s' cannot be deleted, because it is the current snapshot and has one child snapshot"),
                        pSnapshot->i_getName().c_str(),
                        mUserData->s.strName.c_str());

    /* If the snapshot being deleted is the current one, ensure current
     * settings are committed and saved.
     */
    if (pSnapshot == mData->mCurrentSnapshot)
    {
        if (mData->flModifications)
        {
            snapshotLock.release();
            hrc = i_saveSettings(NULL, alock);
            snapshotLock.acquire();
                // no need to change for whether VirtualBox.xml needs saving since
                // we can't have a machine XML rename pending at this point
            if (FAILED(hrc)) return hrc;
        }
    }

    ComObjPtr<SnapshotMachine> pSnapMachine = pSnapshot->i_getSnapshotMachine();

    /* create a progress object. The number of operations is:
     *   1 (preparing) + 1 if the snapshot is online + # of normal hard disks
     */
    LogFlowThisFunc(("Going thru snapshot machine attachments to determine progress setup\n"));

    ULONG ulOpCount = 1;            // one for preparations
    ULONG ulTotalWeight = 1;        // one for preparations

    if (pSnapshot->i_getStateFilePath().isNotEmpty())
    {
        ++ulOpCount;
        ++ulTotalWeight;            // assume 1 MB for deleting the state file
    }

    bool fDeleteOnline = mData->mMachineState == MachineState_Running || mData->mMachineState == MachineState_Paused;

    // count normal hard disks and add their sizes to the weight
    for (MediumAttachmentList::iterator
         it = pSnapMachine->mMediumAttachments->begin();
         it != pSnapMachine->mMediumAttachments->end();
         ++it)
    {
        ComObjPtr<MediumAttachment> &pAttach = *it;
        AutoReadLock attachLock(pAttach COMMA_LOCKVAL_SRC_POS);
        if (pAttach->i_getType() == DeviceType_HardDisk)
        {
            ComObjPtr<Medium> pHD = pAttach->i_getMedium();
            Assert(pHD);
            AutoReadLock mlock(pHD COMMA_LOCKVAL_SRC_POS);

            MediumType_T type = pHD->i_getType();
            // writethrough and shareable images are unaffected by snapshots,
            // so do nothing for them
            if (   type != MediumType_Writethrough
                && type != MediumType_Shareable
                && type != MediumType_Readonly)
            {
                // normal or immutable media need attention
                ++ulOpCount;
                // offline merge includes medium resizing
                if (!fDeleteOnline)
                    ++ulOpCount;
                ulTotalWeight += (ULONG)(pHD->i_getSize() / _1M);
            }
            LogFlowThisFunc(("op %d: considering hard disk attachment %s\n", ulOpCount, pHD->i_getName().c_str()));
        }
    }

    ComObjPtr<Progress> pProgress;
    pProgress.createObject();
    pProgress->init(mParent, static_cast<IMachine*>(this),
                    BstrFmt(tr("Deleting snapshot '%s'"), pSnapshot->i_getName().c_str()).raw(),
                    FALSE /* aCancelable */,
                    ulOpCount,
                    ulTotalWeight,
                    Bstr(tr("Setting up")).raw(),
                    1);

    /* create and start the task on a separate thread */
    DeleteSnapshotTask *pTask = new DeleteSnapshotTask(this, pProgress,
                                                       "DeleteSnap",
                                                       fDeleteOnline,
                                                       pSnapshot);
    hrc = pTask->createThread();
    pTask = NULL;
    if (FAILED(hrc))
        return hrc;

    // the task might start running but will block on acquiring the machine's write lock
    // which we acquired above; once this function leaves, the task will be unblocked;
    // set the proper machine state here now (note: after creating a Task instance)
    if (mData->mMachineState == MachineState_Running)
    {
        i_setMachineState(MachineState_DeletingSnapshotOnline);
        i_updateMachineStateOnClient();
    }
    else if (mData->mMachineState == MachineState_Paused)
    {
        i_setMachineState(MachineState_DeletingSnapshotPaused);
        i_updateMachineStateOnClient();
    }
    else
        i_setMachineState(MachineState_DeletingSnapshot);

    /* return the progress to the caller */
    pProgress.queryInterfaceTo(aProgress.asOutParam());

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 * Helper struct for SessionMachine::deleteSnapshotHandler().
 */
struct MediumDeleteRec
{
    MediumDeleteRec()
        : mfNeedsOnlineMerge(false),
          mpMediumLockList(NULL)
    {}

    MediumDeleteRec(const ComObjPtr<Medium> &aHd,
                    const ComObjPtr<Medium> &aSource,
                    const ComObjPtr<Medium> &aTarget,
                    const ComObjPtr<MediumAttachment> &aOnlineMediumAttachment,
                    bool fMergeForward,
                    const ComObjPtr<Medium> &aParentForTarget,
                    MediumLockList *aChildrenToReparent,
                    bool fNeedsOnlineMerge,
                    MediumLockList *aMediumLockList,
                    const ComPtr<IToken> &aHDLockToken)
        : mpHD(aHd),
          mpSource(aSource),
          mpTarget(aTarget),
          mpOnlineMediumAttachment(aOnlineMediumAttachment),
          mfMergeForward(fMergeForward),
          mpParentForTarget(aParentForTarget),
          mpChildrenToReparent(aChildrenToReparent),
          mfNeedsOnlineMerge(fNeedsOnlineMerge),
          mpMediumLockList(aMediumLockList),
          mpHDLockToken(aHDLockToken)
    {}

    MediumDeleteRec(const ComObjPtr<Medium> &aHd,
                    const ComObjPtr<Medium> &aSource,
                    const ComObjPtr<Medium> &aTarget,
                    const ComObjPtr<MediumAttachment> &aOnlineMediumAttachment,
                    bool fMergeForward,
                    const ComObjPtr<Medium> &aParentForTarget,
                    MediumLockList *aChildrenToReparent,
                    bool fNeedsOnlineMerge,
                    MediumLockList *aMediumLockList,
                    const ComPtr<IToken> &aHDLockToken,
                    const Guid &aMachineId,
                    const Guid &aSnapshotId)
        : mpHD(aHd),
          mpSource(aSource),
          mpTarget(aTarget),
          mpOnlineMediumAttachment(aOnlineMediumAttachment),
          mfMergeForward(fMergeForward),
          mpParentForTarget(aParentForTarget),
          mpChildrenToReparent(aChildrenToReparent),
          mfNeedsOnlineMerge(fNeedsOnlineMerge),
          mpMediumLockList(aMediumLockList),
          mpHDLockToken(aHDLockToken),
          mMachineId(aMachineId),
          mSnapshotId(aSnapshotId)
    {}

    ComObjPtr<Medium> mpHD;
    ComObjPtr<Medium> mpSource;
    ComObjPtr<Medium> mpTarget;
    ComObjPtr<MediumAttachment> mpOnlineMediumAttachment;
    bool mfMergeForward;
    ComObjPtr<Medium> mpParentForTarget;
    MediumLockList *mpChildrenToReparent;
    bool mfNeedsOnlineMerge;
    MediumLockList *mpMediumLockList;
    /** optional lock token, used only in case mpHD is not merged/deleted */
    ComPtr<IToken> mpHDLockToken;
    /* these are for reattaching the hard disk in case of a failure: */
    Guid mMachineId;
    Guid mSnapshotId;
};

typedef std::list<MediumDeleteRec> MediumDeleteRecList;

/**
 * Worker method for the delete snapshot thread created by
 * SessionMachine::DeleteSnapshot().  This method gets called indirectly
 * through SessionMachine::taskHandler() which then calls
 * DeleteSnapshotTask::handler().
 *
 * The DeleteSnapshotTask contains the progress object returned to the console
 * by SessionMachine::DeleteSnapshot, through which progress and results are
 * reported.
 *
 * SessionMachine::DeleteSnapshot() has set the machine state to
 * MachineState_DeletingSnapshot right after creating this task. Since we block
 * on the machine write lock at the beginning, once that has been acquired, we
 * can assume that the machine state is indeed that.
 *
 * @note Locks the machine + the snapshot + the media tree for writing!
 *
 * @param task Task data.
 */
void SessionMachine::i_deleteSnapshotHandler(DeleteSnapshotTask &task)
{
    LogFlowThisFuncEnter();

    MultiResult mrc(S_OK);
    AutoCaller autoCaller(this);
    LogFlowThisFunc(("state=%d\n", getObjectState().getState()));
    if (FAILED(autoCaller.hrc()))
    {
        /* we might have been uninitialized because the session was accidentally
         * closed by the client, so don't assert */
        mrc = setError(E_FAIL,
                       tr("The session has been accidentally closed"));
        task.m_pProgress->i_notifyComplete(mrc);
        LogFlowThisFuncLeave();
        return;
    }

    MediumDeleteRecList toDelete;
    Guid snapshotId;
    std::set<ComObjPtr<Medium> > pMediaForNotify;
    std::map<Guid,DeviceType_T> uIdsForNotify;

    try
    {
        HRESULT hrc = S_OK;

        /* Locking order:  */
        AutoMultiWriteLock2 multiLock(this->lockHandle(),                   // machine
                                      task.m_pSnapshot->lockHandle()        // snapshot
                                      COMMA_LOCKVAL_SRC_POS);
        // once we have this lock, we know that SessionMachine::DeleteSnapshot()
        // has exited after setting the machine state to MachineState_DeletingSnapshot

        AutoWriteLock treeLock(mParent->i_getMediaTreeLockHandle()
                               COMMA_LOCKVAL_SRC_POS);

        ComObjPtr<SnapshotMachine> pSnapMachine = task.m_pSnapshot->i_getSnapshotMachine();
        // no need to lock the snapshot machine since it is const by definition
        Guid machineId = pSnapMachine->i_getId();

        // save the snapshot ID (for callbacks)
        snapshotId = task.m_pSnapshot->i_getId();

        // first pass:
        LogFlowThisFunc(("1: Checking hard disk merge prerequisites...\n"));

        // Go thru the attachments of the snapshot machine (the media in here
        // point to the disk states _before_ the snapshot was taken, i.e. the
        // state we're restoring to; for each such medium, we will need to
        // merge it with its one and only child (the diff image holding the
        // changes written after the snapshot was taken).
        for (MediumAttachmentList::iterator
             it = pSnapMachine->mMediumAttachments->begin();
             it != pSnapMachine->mMediumAttachments->end();
             ++it)
        {
            ComObjPtr<MediumAttachment> &pAttach = *it;
            AutoReadLock attachLock(pAttach COMMA_LOCKVAL_SRC_POS);
            if (pAttach->i_getType() != DeviceType_HardDisk)
                continue;

            ComObjPtr<Medium> pHD = pAttach->i_getMedium();
            Assert(!pHD.isNull());

            {
                // writethrough, shareable and readonly images are
                // unaffected by snapshots, skip them
                AutoReadLock medlock(pHD COMMA_LOCKVAL_SRC_POS);
                MediumType_T type = pHD->i_getType();
                if (   type == MediumType_Writethrough
                    || type == MediumType_Shareable
                    || type == MediumType_Readonly)
                    continue;
            }

#ifdef DEBUG
            pHD->i_dumpBackRefs();
#endif

            // needs to be merged with child or deleted, check prerequisites
            ComObjPtr<Medium> pTarget;
            ComObjPtr<Medium> pSource;
            bool fMergeForward = false;
            ComObjPtr<Medium> pParentForTarget;
            MediumLockList *pChildrenToReparent = NULL;
            bool fNeedsOnlineMerge = false;
            bool fOnlineMergePossible = task.m_fDeleteOnline;
            MediumLockList *pMediumLockList = NULL;
            MediumLockList *pVMMALockList = NULL;
            ComPtr<IToken> pHDLockToken;
            ComObjPtr<MediumAttachment> pOnlineMediumAttachment;
            if (fOnlineMergePossible)
            {
                // Look up the corresponding medium attachment in the currently
                // running VM. Any failure prevents a live merge. Could be made
                // a tad smarter by trying a few candidates, so that e.g. disks
                // which are simply moved to a different controller slot do not
                // prevent online merging in general.
                pOnlineMediumAttachment =
                    i_findAttachment(*mMediumAttachments.data(),
                                     pAttach->i_getControllerName(),
                                     pAttach->i_getPort(),
                                     pAttach->i_getDevice());
                if (pOnlineMediumAttachment)
                {
                    hrc = mData->mSession.mLockedMedia.Get(pOnlineMediumAttachment, pVMMALockList);
                    if (FAILED(hrc))
                        fOnlineMergePossible = false;
                }
                else
                    fOnlineMergePossible = false;
            }

            // no need to hold the lock any longer
            attachLock.release();

            treeLock.release();
            hrc = i_prepareDeleteSnapshotMedium(pHD, machineId, snapshotId,
                                                fOnlineMergePossible,
                                                pVMMALockList, pSource, pTarget,
                                                fMergeForward, pParentForTarget,
                                                pChildrenToReparent,
                                                fNeedsOnlineMerge,
                                                pMediumLockList,
                                                pHDLockToken);
            treeLock.acquire();
            if (FAILED(hrc))
                throw hrc;

            // For simplicity, prepareDeleteSnapshotMedium selects the merge
            // direction in the following way: we merge pHD onto its child
            // (forward merge), not the other way round, because that saves us
            // from unnecessarily shuffling around the attachments for the
            // machine that follows the snapshot (next snapshot or current
            // state), unless it's a base image. Backwards merges of the first
            // snapshot into the base image is essential, as it ensures that
            // when all snapshots are deleted the only remaining image is a
            // base image. Important e.g. for medium formats which do not have
            // a file representation such as iSCSI.

            // not going to merge a big source into a small target on online merge. Otherwise it will be resized
            if (fNeedsOnlineMerge && pSource->i_getLogicalSize() > pTarget->i_getLogicalSize())
                throw setError(E_FAIL,
                               tr("Unable to merge storage '%s', because it is smaller than the source image. If you resize it to have a capacity of at least %lld bytes you can retry",
                                  "", pSource->i_getLogicalSize()),
                               pTarget->i_getLocationFull().c_str(), pSource->i_getLogicalSize());

            // a couple paranoia checks for backward merges
            if (pMediumLockList != NULL && !fMergeForward)
            {
                // parent is null -> this disk is a base hard disk: we will
                // then do a backward merge, i.e. merge its only child onto the
                // base disk. Here we need then to update the attachment that
                // refers to the child and have it point to the parent instead
                Assert(pHD->i_getChildren().size() == 1);

                ComObjPtr<Medium> pReplaceHD = pHD->i_getChildren().front();

                ComAssertThrow(pReplaceHD == pSource, E_FAIL);
            }

            Guid replaceMachineId;
            Guid replaceSnapshotId;

            const Guid *pReplaceMachineId = pSource->i_getFirstMachineBackrefId();
            // minimal sanity checking
            Assert(!pReplaceMachineId || *pReplaceMachineId == mData->mUuid);
            if (pReplaceMachineId)
                replaceMachineId = *pReplaceMachineId;

            const Guid *pSnapshotId = pSource->i_getFirstMachineBackrefSnapshotId();
            if (pSnapshotId)
                replaceSnapshotId = *pSnapshotId;

            if (replaceMachineId.isValid() && !replaceMachineId.isZero())
            {
                // Adjust the backreferences, otherwise merging will assert.
                // Note that the medium attachment object stays associated
                // with the snapshot until the merge was successful.
                HRESULT hrc2 = pSource->i_removeBackReference(replaceMachineId, replaceSnapshotId);
                AssertComRC(hrc2);

                toDelete.push_back(MediumDeleteRec(pHD, pSource, pTarget,
                                                   pOnlineMediumAttachment,
                                                   fMergeForward,
                                                   pParentForTarget,
                                                   pChildrenToReparent,
                                                   fNeedsOnlineMerge,
                                                   pMediumLockList,
                                                   pHDLockToken,
                                                   replaceMachineId,
                                                   replaceSnapshotId));
            }
            else
                toDelete.push_back(MediumDeleteRec(pHD, pSource, pTarget,
                                                   pOnlineMediumAttachment,
                                                   fMergeForward,
                                                   pParentForTarget,
                                                   pChildrenToReparent,
                                                   fNeedsOnlineMerge,
                                                   pMediumLockList,
                                                   pHDLockToken));
        }

        {
            /* check available space on the storage */
            RTFOFF pcbTotal = 0;
            RTFOFF pcbFree = 0;
            uint32_t pcbBlock = 0;
            uint32_t pcbSector = 0;
            std::multimap<uint32_t, uint64_t> neededStorageFreeSpace;
            std::map<uint32_t, const char*> serialMapToStoragePath;

            for (MediumDeleteRecList::const_iterator
                 it = toDelete.begin();
                 it != toDelete.end();
                 ++it)
            {
                uint64_t diskSize = 0;
                uint32_t pu32Serial = 0;
                ComObjPtr<Medium> pSource_local = it->mpSource;
                ComObjPtr<Medium> pTarget_local = it->mpTarget;
                ComPtr<IMediumFormat> pTargetFormat;

                {
                    if (   pSource_local.isNull()
                        || pSource_local == pTarget_local)
                        continue;
                }

                hrc = pTarget_local->COMGETTER(MediumFormat)(pTargetFormat.asOutParam());
                if (FAILED(hrc))
                    throw hrc;

                if (pTarget_local->i_isMediumFormatFile())
                {
                    int vrc = RTFsQuerySerial(pTarget_local->i_getLocationFull().c_str(), &pu32Serial);
                    if (RT_FAILURE(vrc))
                        throw setError(E_FAIL,
                                       tr("Unable to merge storage '%s'. Can't get storage UID"),
                                       pTarget_local->i_getLocationFull().c_str());

                    pSource_local->COMGETTER(Size)((LONG64*)&diskSize);

                    /** @todo r=klaus this is too pessimistic... should take
                     * the current size and maximum size of the target image
                     * into account, because a X GB image with Y GB capacity
                     * can only grow by Y-X GB (ignoring overhead, which
                     * unfortunately is hard to estimate, some have next to
                     * nothing, some have a certain percentage...) */
                    /* store needed free space in multimap */
                    neededStorageFreeSpace.insert(std::make_pair(pu32Serial, diskSize));
                    /* linking storage UID with snapshot path, it is a helper container (just for easy finding needed path) */
                    serialMapToStoragePath.insert(std::make_pair(pu32Serial, pTarget_local->i_getLocationFull().c_str()));
                }
            }

            while (!neededStorageFreeSpace.empty())
            {
                std::pair<std::multimap<uint32_t,uint64_t>::iterator,std::multimap<uint32_t,uint64_t>::iterator> ret;
                uint64_t commonSourceStoragesSize = 0;

                /* find all records in multimap with identical storage UID */
                ret = neededStorageFreeSpace.equal_range(neededStorageFreeSpace.begin()->first);
                std::multimap<uint32_t,uint64_t>::const_iterator it_ns = ret.first;

                for (; it_ns != ret.second ; ++it_ns)
                {
                    commonSourceStoragesSize += it_ns->second;
                }

                /* find appropriate path by storage UID */
                std::map<uint32_t,const char*>::const_iterator it_sm = serialMapToStoragePath.find(ret.first->first);
                /* get info about a storage */
                if (it_sm == serialMapToStoragePath.end())
                {
                    LogFlowThisFunc(("Path to the storage wasn't found...\n"));

                    throw setError(E_INVALIDARG,
                                   tr("Unable to merge storage '%s'. Path to the storage wasn't found"),
                                   it_sm->second);
                }

                int vrc = RTFsQuerySizes(it_sm->second, &pcbTotal, &pcbFree, &pcbBlock, &pcbSector);
                if (RT_FAILURE(vrc))
                {
                    throw setError(E_FAIL,
                                   tr("Unable to merge storage '%s'. Can't get the storage size"),
                                   it_sm->second);
                }

                if (commonSourceStoragesSize > (uint64_t)pcbFree)
                {
                    LogFlowThisFunc(("Not enough free space to merge...\n"));

                    throw setError(E_OUTOFMEMORY,
                                   tr("Unable to merge storage '%s'. Not enough free storage space"),
                                   it_sm->second);
                }

                neededStorageFreeSpace.erase(ret.first, ret.second);
            }

            serialMapToStoragePath.clear();
        }

        // we can release the locks now since the machine state is MachineState_DeletingSnapshot
        treeLock.release();
        multiLock.release();

        /* Now we checked that we can successfully merge all normal hard disks
         * (unless a runtime error like end-of-disc happens). Now get rid of
         * the saved state (if present), as that will free some disk space.
         * The snapshot itself will be deleted as late as possible, so that
         * the user can repeat the delete operation if he runs out of disk
         * space or cancels the delete operation. */

        /* second pass: */
        LogFlowThisFunc(("2: Deleting saved state...\n"));

        {
            // saveAllSnapshots() needs a machine lock, and the snapshots
            // tree is protected by the machine lock as well
            AutoWriteLock machineLock(this COMMA_LOCKVAL_SRC_POS);

            Utf8Str stateFilePath = task.m_pSnapshot->i_getStateFilePath();
            if (!stateFilePath.isEmpty())
            {
                task.m_pProgress->SetNextOperation(Bstr(tr("Deleting the execution state")).raw(),
                                                   1);        // weight

                i_releaseSavedStateFile(stateFilePath, task.m_pSnapshot /* pSnapshotToIgnore */);

                // machine will need saving now
                machineLock.release();
                mParent->i_markRegistryModified(i_getId());
            }
        }

        /* third pass: */
        LogFlowThisFunc(("3: Performing actual hard disk merging...\n"));

        /// @todo NEWMEDIA turn the following errors into warnings because the
        /// snapshot itself has been already deleted (and interpret these
        /// warnings properly on the GUI side)
        for (MediumDeleteRecList::iterator it = toDelete.begin();
             it != toDelete.end();)
        {
            const ComObjPtr<Medium> &pMedium(it->mpHD);
            ULONG ulWeight;

            {
                AutoReadLock alock(pMedium COMMA_LOCKVAL_SRC_POS);
                ulWeight = (ULONG)(pMedium->i_getSize() / _1M);
            }

            const char *pszOperationText = it->mfNeedsOnlineMerge ?
                                             tr("Merging differencing image '%s'")
                                           : tr("Resizing before merge differencing image '%s'");

            task.m_pProgress->SetNextOperation(BstrFmt(pszOperationText,
                                               pMedium->i_getName().c_str()).raw(),
                                               ulWeight);

            bool fNeedSourceUninit = false;
            bool fReparentTarget = false;
            if (it->mpMediumLockList == NULL)
            {
                /* no real merge needed, just updating state and delete
                 * diff files if necessary */
                AutoMultiWriteLock2 mLock(&mParent->i_getMediaTreeLockHandle(), pMedium->lockHandle() COMMA_LOCKVAL_SRC_POS);

                Assert(   !it->mfMergeForward
                       || pMedium->i_getChildren().size() == 0);

                /* Delete the differencing hard disk (has no children). Two
                 * exceptions: if it's the last medium in the chain or if it's
                 * a backward merge we don't want to handle due to complexity.
                 * In both cases leave the image in place. If it's the first
                 * exception the user can delete it later if he wants. */
                if (!pMedium->i_getParent().isNull())
                {
                    Assert(pMedium->i_getState() == MediumState_Deleting);
                    /* No need to hold the lock any longer. */
                    mLock.release();
                    ComObjPtr<Medium> pParent = pMedium->i_getParent();
                    Guid uMedium = pMedium->i_getId();
                    DeviceType_T uMediumType = pMedium->i_getDeviceType();
                    hrc = pMedium->i_deleteStorage(&task.m_pProgress, true /* aWait */, false /* aNotify */);
                    if (FAILED(hrc))
                        throw hrc;

                    pMediaForNotify.insert(pParent);
                    uIdsForNotify[uMedium] = uMediumType;

                    // need to uninit the deleted medium
                    fNeedSourceUninit = true;
                }
            }
            else
            {
                {
                    //store ids before merging for notify
                    pMediaForNotify.insert(it->mpTarget);
                    if (it->mfMergeForward)
                        pMediaForNotify.insert(it->mpSource->i_getParent());
                    else
                    {
                        //children which will be reparented to target
                        for (MediaList::const_iterator iit = it->mpSource->i_getChildren().begin();
                             iit != it->mpSource->i_getChildren().end();
                             ++iit)
                        {
                            pMediaForNotify.insert(*iit);
                        }
                    }
                    if (it->mfMergeForward)
                    {
                        for (ComObjPtr<Medium> pTmpMedium = it->mpTarget->i_getParent();
                             pTmpMedium && pTmpMedium != it->mpSource;
                             pTmpMedium = pTmpMedium->i_getParent())
                        {
                            uIdsForNotify[pTmpMedium->i_getId()] = pTmpMedium->i_getDeviceType();
                        }
                        uIdsForNotify[it->mpSource->i_getId()] = it->mpSource->i_getDeviceType();
                    }
                    else
                    {
                        for (ComObjPtr<Medium> pTmpMedium = it->mpSource;
                             pTmpMedium && pTmpMedium != it->mpTarget;
                             pTmpMedium = pTmpMedium->i_getParent())
                        {
                            uIdsForNotify[pTmpMedium->i_getId()] = pTmpMedium->i_getDeviceType();
                        }
                    }
                }

                bool fNeedsSave = false;
                if (it->mfNeedsOnlineMerge)
                {
                    // Put the medium merge information (MediumDeleteRec) where
                    // SessionMachine::FinishOnlineMergeMedium can get at it.
                    // This callback will arrive while onlineMergeMedium is
                    // still executing, and there can't be two tasks.
                    /// @todo r=klaus this hack needs to go, and the logic needs to be "unconvoluted", putting SessionMachine in charge of coordinating the reconfig/resume.
                    mConsoleTaskData.mDeleteSnapshotInfo = (void *)&(*it);
                    // online medium merge, in the direction decided earlier
                    hrc = i_onlineMergeMedium(it->mpOnlineMediumAttachment,
                                              it->mpSource,
                                              it->mpTarget,
                                              it->mfMergeForward,
                                              it->mpParentForTarget,
                                              it->mpChildrenToReparent,
                                              it->mpMediumLockList,
                                              task.m_pProgress,
                                              &fNeedsSave);
                    mConsoleTaskData.mDeleteSnapshotInfo = NULL;
                }
                else
                {
                    // normal medium merge, in the direction decided earlier
                    hrc = it->mpSource->i_mergeTo(it->mpTarget,
                                                  it->mfMergeForward,
                                                  it->mpParentForTarget,
                                                  it->mpChildrenToReparent,
                                                  it->mpMediumLockList,
                                                  &task.m_pProgress,
                                                  true /* aWait */,
                                                  false /* aNotify */);
                }

                // If the merge failed, we need to do our best to have a usable
                // VM configuration afterwards. The return code doesn't tell
                // whether the merge completed and so we have to check if the
                // source medium (diff images are always file based at the
                // moment) is still there or not. Be careful not to lose the
                // error code below, before the "Delayed failure exit".
                if (FAILED(hrc))
                {
                    AutoReadLock mlock(it->mpSource COMMA_LOCKVAL_SRC_POS);
                    if (!it->mpSource->i_isMediumFormatFile())
                        // Diff medium not backed by a file - cannot get status so
                        // be pessimistic.
                        throw hrc;
                    const Utf8Str &loc = it->mpSource->i_getLocationFull();
                    // Source medium is still there, so merge failed early.
                    if (RTFileExists(loc.c_str()))
                        throw hrc;

                    // Source medium is gone. Assume the merge succeeded and
                    // thus it's safe to remove the attachment. We use the
                    // "Delayed failure exit" below.
                }

                // need to change the medium attachment for backward merges
                fReparentTarget = !it->mfMergeForward;

                if (!it->mfNeedsOnlineMerge)
                {
                    // need to uninit the medium deleted by the merge
                    fNeedSourceUninit = true;

                    // delete the no longer needed medium lock list, which
                    // implicitly handled the unlocking
                    delete it->mpMediumLockList;
                    it->mpMediumLockList = NULL;
                }
            }

            // Now that the medium is successfully merged/deleted/whatever,
            // remove the medium attachment from the snapshot. For a backwards
            // merge the target attachment needs to be removed from the
            // snapshot, as the VM will take it over. For forward merges the
            // source medium attachment needs to be removed.
            ComObjPtr<MediumAttachment> pAtt;
            if (fReparentTarget)
            {
                pAtt = i_findAttachment(*(pSnapMachine->mMediumAttachments.data()),
                                        it->mpTarget);
                it->mpTarget->i_removeBackReference(machineId, snapshotId);
            }
            else
                pAtt = i_findAttachment(*(pSnapMachine->mMediumAttachments.data()),
                                        it->mpSource);
            pSnapMachine->mMediumAttachments->remove(pAtt);

            if (fReparentTarget)
            {
                // Search for old source attachment and replace with target.
                // There can be only one child snapshot in this case.
                ComObjPtr<Machine> pMachine = this;
                Guid childSnapshotId;
                ComObjPtr<Snapshot> pChildSnapshot = task.m_pSnapshot->i_getFirstChild();
                if (pChildSnapshot)
                {
                    pMachine = pChildSnapshot->i_getSnapshotMachine();
                    childSnapshotId = pChildSnapshot->i_getId();
                }
                pAtt = i_findAttachment(*(pMachine->mMediumAttachments).data(), it->mpSource);
                if (pAtt)
                {
                    AutoWriteLock attLock(pAtt COMMA_LOCKVAL_SRC_POS);
                    pAtt->i_updateMedium(it->mpTarget);
                    it->mpTarget->i_addBackReference(pMachine->mData->mUuid, childSnapshotId);
                }
                else
                {
                    // If no attachment is found do not change anything. Maybe
                    // the source medium was not attached to the snapshot.
                    // If this is an online deletion the attachment was updated
                    // already to allow the VM continue execution immediately.
                    // Needs a bit of special treatment due to this difference.
                    if (it->mfNeedsOnlineMerge)
                        it->mpTarget->i_addBackReference(pMachine->mData->mUuid, childSnapshotId);
                }
            }

            if (fNeedSourceUninit)
            {
                // make sure that the diff image to be deleted has no parent,
                // even in error cases (where the deparenting may be missing)
                if (it->mpSource->i_getParent())
                    it->mpSource->i_deparent();
                it->mpSource->uninit();
            }

            // One attachment is merged, must save the settings
            mParent->i_markRegistryModified(i_getId());

            // prevent calling cancelDeleteSnapshotMedium() for this attachment
            it = toDelete.erase(it);

            // Delayed failure exit when the merge cleanup failed but the
            // merge actually succeeded.
            if (FAILED(hrc))
                throw hrc;
        }

        /* 3a: delete NVRAM file if present. */
        {
            Utf8Str NVRAMPath = pSnapMachine->mNvramStore->i_getNonVolatileStorageFile();
            if (NVRAMPath.isNotEmpty() && RTFileExists(NVRAMPath.c_str()))
                RTFileDelete(NVRAMPath.c_str());
        }

        /* third pass: */
        {
            // beginSnapshotDelete() needs the machine lock, and the snapshots
            // tree is protected by the machine lock as well
            AutoWriteLock machineLock(this COMMA_LOCKVAL_SRC_POS);

            task.m_pSnapshot->i_beginSnapshotDelete();
            task.m_pSnapshot->uninit();

            machineLock.release();
            mParent->i_markRegistryModified(i_getId());
        }
    }
    catch (HRESULT hrcXcpt)
    {
        mrc = hrcXcpt;
    }

    if (FAILED(mrc))
    {
        // preserve existing error info so that the result can
        // be properly reported to the progress object below
        ErrorInfoKeeper eik;

        AutoMultiWriteLock2 multiLock(this->lockHandle(),                   // machine
                                      &mParent->i_getMediaTreeLockHandle()    // media tree
                                      COMMA_LOCKVAL_SRC_POS);

        // un-prepare the remaining hard disks
        for (MediumDeleteRecList::const_iterator it = toDelete.begin();
             it != toDelete.end();
             ++it)
            i_cancelDeleteSnapshotMedium(it->mpHD, it->mpSource,
                                         it->mpChildrenToReparent,
                                         it->mfNeedsOnlineMerge,
                                         it->mpMediumLockList, it->mpHDLockToken,
                                         it->mMachineId, it->mSnapshotId);
    }

    // whether we were successful or not, we need to set the machine
    // state and save the machine settings;
    {
        // preserve existing error info so that the result can
        // be properly reported to the progress object below
        ErrorInfoKeeper eik;

        // restore the machine state that was saved when the
        // task was started
        i_setMachineState(task.m_machineStateBackup);
        if (Global::IsOnline(mData->mMachineState))
            i_updateMachineStateOnClient();

        mParent->i_saveModifiedRegistries();
    }

    // report the result (this will try to fetch current error info on failure)
    task.m_pProgress->i_notifyComplete(mrc);

    if (SUCCEEDED(mrc))
    {
        mParent->i_onSnapshotDeleted(mData->mUuid, snapshotId);
        for (std::map<Guid, DeviceType_T>::const_iterator it = uIdsForNotify.begin();
             it != uIdsForNotify.end();
             ++it)
        {
            mParent->i_onMediumRegistered(it->first, it->second, FALSE);
        }
        for (std::set<ComObjPtr<Medium> >::const_iterator it = pMediaForNotify.begin();
             it != pMediaForNotify.end();
             ++it)
        {
            if (it->isNotNull())
                mParent->i_onMediumConfigChanged(*it);
        }
    }

    LogFlowThisFunc(("Done deleting snapshot (mrc=%08X)\n", (HRESULT)mrc));
    LogFlowThisFuncLeave();
}

/**
 * Checks that this hard disk (part of a snapshot) may be deleted/merged and
 * performs necessary state changes. Must not be called for writethrough disks
 * because there is nothing to delete/merge then.
 *
 * This method is to be called prior to calling #deleteSnapshotMedium().
 * If #deleteSnapshotMedium() is not called or fails, the state modifications
 * performed by this method must be undone by #cancelDeleteSnapshotMedium().
 *
 * @return COM status code
 * @param aHD           Hard disk which is connected to the snapshot.
 * @param aMachineId    UUID of machine this hard disk is attached to.
 * @param aSnapshotId   UUID of snapshot this hard disk is attached to. May
 *                      be a zero UUID if no snapshot is applicable.
 * @param fOnlineMergePossible Flag whether an online merge is possible.
 * @param aVMMALockList Medium lock list for the medium attachment of this VM.
 *                      Only used if @a fOnlineMergePossible is @c true, and
 *                      must be non-NULL in this case.
 * @param aSource       Source hard disk for merge (out).
 * @param aTarget       Target hard disk for merge (out).
 * @param aMergeForward Merge direction decision (out).
 * @param aParentForTarget New parent if target needs to be reparented (out).
 * @param aChildrenToReparent MediumLockList with children which have to be
 *                      reparented to the target (out).
 * @param fNeedsOnlineMerge Whether this merge needs to be done online (out).
 *                      If this is set to @a true then the @a aVMMALockList
 *                      parameter has been modified and is returned as
 *                      @a aMediumLockList.
 * @param aMediumLockList Where to store the created medium lock list (may
 *                      return NULL if no real merge is necessary).
 * @param aHDLockToken  Where to store the write lock token for aHD, in case
 *                      it is not merged or deleted (out).
 *
 * @note Caller must hold media tree lock for writing. This locks this object
 *       and every medium object on the merge chain for writing.
 */
HRESULT SessionMachine::i_prepareDeleteSnapshotMedium(const ComObjPtr<Medium> &aHD,
                                                      const Guid &aMachineId,
                                                      const Guid &aSnapshotId,
                                                      bool fOnlineMergePossible,
                                                      MediumLockList *aVMMALockList,
                                                      ComObjPtr<Medium> &aSource,
                                                      ComObjPtr<Medium> &aTarget,
                                                      bool &aMergeForward,
                                                      ComObjPtr<Medium> &aParentForTarget,
                                                      MediumLockList * &aChildrenToReparent,
                                                      bool &fNeedsOnlineMerge,
                                                      MediumLockList * &aMediumLockList,
                                                      ComPtr<IToken> &aHDLockToken)
{
    Assert(!mParent->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    Assert(!fOnlineMergePossible || RT_VALID_PTR(aVMMALockList));

    AutoWriteLock alock(aHD COMMA_LOCKVAL_SRC_POS);

    // Medium must not be writethrough/shareable/readonly at this point
    MediumType_T type = aHD->i_getType();
    AssertReturn(   type != MediumType_Writethrough
                 && type != MediumType_Shareable
                 && type != MediumType_Readonly, E_FAIL);

    aChildrenToReparent = NULL;
    aMediumLockList = NULL;
    fNeedsOnlineMerge = false;

    if (aHD->i_getChildren().size() == 0)
    {
        /* This technically is no merge, set those values nevertheless.
         * Helps with updating the medium attachments. */
        aSource = aHD;
        aTarget = aHD;

        /* special treatment of the last hard disk in the chain: */
        if (aHD->i_getParent().isNull())
        {
            /* lock only, to prevent any usage until the snapshot deletion
             * is completed */
            alock.release();
            return aHD->LockWrite(aHDLockToken.asOutParam());
        }

        /* the differencing hard disk w/o children will be deleted, protect it
         * from attaching to other VMs (this is why Deleting) */
        return aHD->i_markForDeletion();
   }

    /* not going multi-merge as it's too expensive */
    if (aHD->i_getChildren().size() > 1)
        return setError(E_FAIL,
                        tr("Hard disk '%s' has more than one child hard disk (%d)"),
                        aHD->i_getLocationFull().c_str(),
                        aHD->i_getChildren().size());

    ComObjPtr<Medium> pChild = aHD->i_getChildren().front();

    AutoWriteLock childLock(pChild COMMA_LOCKVAL_SRC_POS);

    /* the rest is a normal merge setup */
    if (aHD->i_getParent().isNull())
    {
        /* base hard disk, backward merge */
        const Guid *pMachineId1 = pChild->i_getFirstMachineBackrefId();
        const Guid *pMachineId2 = aHD->i_getFirstMachineBackrefId();
        if (pMachineId1 && pMachineId2 && *pMachineId1 != *pMachineId2)
        {
            /* backward merge is too tricky, we'll just detach on snapshot
             * deletion, so lock only, to prevent any usage */
            childLock.release();
            alock.release();
            return aHD->LockWrite(aHDLockToken.asOutParam());
        }

        aSource = pChild;
        aTarget = aHD;
    }
    else
    {
        /* Determine best merge direction. */
        bool fMergeForward = true;

        childLock.release();
        alock.release();
        HRESULT hrc = aHD->i_queryPreferredMergeDirection(pChild, fMergeForward);
        alock.acquire();
        childLock.acquire();

        if (FAILED(hrc) && hrc != E_FAIL)
            return hrc;

        if (fMergeForward)
        {
            aSource = aHD;
            aTarget = pChild;
            LogFlowThisFunc(("Forward merging selected\n"));
        }
        else
        {
            aSource = pChild;
            aTarget = aHD;
            LogFlowThisFunc(("Backward merging selected\n"));
        }
    }

    HRESULT hrc;
    childLock.release();
    alock.release();
    hrc = aSource->i_prepareMergeTo(aTarget, &aMachineId, &aSnapshotId,
                                    !fOnlineMergePossible /* fLockMedia */,
                                    aMergeForward, aParentForTarget,
                                    aChildrenToReparent, aMediumLockList);
    alock.acquire();
    childLock.acquire();
    if (SUCCEEDED(hrc) && fOnlineMergePossible)
    {
        /* Try to lock the newly constructed medium lock list. If it succeeds
         * this can be handled as an offline merge, i.e. without the need of
         * asking the VM to do the merging. Only continue with the online
         * merging preparation if applicable. */
        childLock.release();
        alock.release();
        hrc = aMediumLockList->Lock();
        alock.acquire();
        childLock.acquire();
        if (FAILED(hrc))
        {
            /* Locking failed, this cannot be done as an offline merge. Try to
             * combine the locking information into the lock list of the medium
             * attachment in the running VM. If that fails or locking the
             * resulting lock list fails then the merge cannot be done online.
             * It can be repeated by the user when the VM is shut down. */
            MediumLockList::Base::iterator lockListVMMABegin =
                aVMMALockList->GetBegin();
            MediumLockList::Base::iterator lockListVMMAEnd =
                aVMMALockList->GetEnd();
            MediumLockList::Base::iterator lockListBegin =
                aMediumLockList->GetBegin();
            MediumLockList::Base::iterator lockListEnd =
                aMediumLockList->GetEnd();
            for (MediumLockList::Base::iterator it = lockListVMMABegin,
                 it2 = lockListBegin;
                 it2 != lockListEnd;
                 ++it, ++it2)
            {
                if (   it == lockListVMMAEnd
                    || it->GetMedium() != it2->GetMedium())
                {
                    fOnlineMergePossible = false;
                    break;
                }
                bool fLockReq = (it2->GetLockRequest() || it->GetLockRequest());
                childLock.release();
                alock.release();
                hrc = it->UpdateLock(fLockReq);
                alock.acquire();
                childLock.acquire();
                if (FAILED(hrc))
                {
                    // could not update the lock, trigger cleanup below
                    fOnlineMergePossible = false;
                    break;
                }
            }

            if (fOnlineMergePossible)
            {
                /* we will lock the children of the source for reparenting */
                if (aChildrenToReparent && !aChildrenToReparent->IsEmpty())
                {
                    /* Cannot just call aChildrenToReparent->Lock(), as one of
                     * the children is the one under which the current state of
                     * the VM is located, and this means it is already locked
                     * (for reading). Note that no special unlocking is needed,
                     * because cancelMergeTo will unlock everything locked in
                     * its context (using the unlock on destruction), and both
                     * cancelDeleteSnapshotMedium (in case something fails) and
                     * FinishOnlineMergeMedium re-define the read/write lock
                     * state of everything which the VM need, search for the
                     * UpdateLock method calls. */
                    childLock.release();
                    alock.release();
                    hrc = aChildrenToReparent->Lock(true /* fSkipOverLockedMedia */);
                    alock.acquire();
                    childLock.acquire();
                    MediumLockList::Base::iterator childrenToReparentBegin = aChildrenToReparent->GetBegin();
                    MediumLockList::Base::iterator childrenToReparentEnd = aChildrenToReparent->GetEnd();
                    for (MediumLockList::Base::iterator it = childrenToReparentBegin;
                         it != childrenToReparentEnd;
                         ++it)
                    {
                        ComObjPtr<Medium> pMedium = it->GetMedium();
                        AutoReadLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
                        if (!it->IsLocked())
                        {
                            mediumLock.release();
                            childLock.release();
                            alock.release();
                            hrc = aVMMALockList->Update(pMedium, true);
                            alock.acquire();
                            childLock.acquire();
                            mediumLock.acquire();
                            if (FAILED(hrc))
                                throw hrc;
                        }
                    }
                }
            }

            if (fOnlineMergePossible)
            {
                childLock.release();
                alock.release();
                hrc = aVMMALockList->Lock();
                alock.acquire();
                childLock.acquire();
                if (FAILED(hrc))
                {
                    aSource->i_cancelMergeTo(aChildrenToReparent, aMediumLockList);
                    hrc = setError(hrc,
                                   tr("Cannot lock hard disk '%s' for a live merge"),
                                   aHD->i_getLocationFull().c_str());
                }
                else
                {
                    delete aMediumLockList;
                    aMediumLockList = aVMMALockList;
                    fNeedsOnlineMerge = true;
                }
            }
            else
            {
                aSource->i_cancelMergeTo(aChildrenToReparent, aMediumLockList);
                hrc = setError(hrc,
                               tr("Failed to construct lock list for a live merge of hard disk '%s'"),
                               aHD->i_getLocationFull().c_str());
            }

            // fix the VM's lock list if anything failed
            if (FAILED(hrc))
            {
                lockListVMMABegin = aVMMALockList->GetBegin();
                lockListVMMAEnd = aVMMALockList->GetEnd();
                MediumLockList::Base::iterator lockListLast = lockListVMMAEnd;
                --lockListLast;
                for (MediumLockList::Base::iterator it = lockListVMMABegin;
                     it != lockListVMMAEnd;
                     ++it)
                {
                    childLock.release();
                    alock.release();
                    it->UpdateLock(it == lockListLast);
                    alock.acquire();
                    childLock.acquire();
                    ComObjPtr<Medium> pMedium = it->GetMedium();
                    AutoWriteLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
                    // blindly apply this, only needed for medium objects which
                    // would be deleted as part of the merge
                    pMedium->i_unmarkLockedForDeletion();
                }
            }
        }
    }
    else if (FAILED(hrc))
    {
        aSource->i_cancelMergeTo(aChildrenToReparent, aMediumLockList);
        hrc = setError(hrc,
                       tr("Cannot lock hard disk '%s' when deleting a snapshot"),
                       aHD->i_getLocationFull().c_str());
    }

    return hrc;
}

/**
 * Cancels the deletion/merging of this hard disk (part of a snapshot). Undoes
 * what #prepareDeleteSnapshotMedium() did. Must be called if
 * #deleteSnapshotMedium() is not called or fails.
 *
 * @param aHD           Hard disk which is connected to the snapshot.
 * @param aSource       Source hard disk for merge.
 * @param aChildrenToReparent Children to unlock.
 * @param fNeedsOnlineMerge Whether this merge needs to be done online.
 * @param aMediumLockList Medium locks to cancel.
 * @param aHDLockToken  Optional write lock token for aHD.
 * @param aMachineId    Machine id to attach the medium to.
 * @param aSnapshotId   Snapshot id to attach the medium to.
 *
 * @note Locks the medium tree and the hard disks in the chain for writing.
 */
void SessionMachine::i_cancelDeleteSnapshotMedium(const ComObjPtr<Medium> &aHD,
                                                  const ComObjPtr<Medium> &aSource,
                                                  MediumLockList *aChildrenToReparent,
                                                  bool fNeedsOnlineMerge,
                                                  MediumLockList *aMediumLockList,
                                                  const ComPtr<IToken> &aHDLockToken,
                                                  const Guid &aMachineId,
                                                  const Guid &aSnapshotId)
{
    if (aMediumLockList == NULL)
    {
        AutoMultiWriteLock2 mLock(&mParent->i_getMediaTreeLockHandle(), aHD->lockHandle() COMMA_LOCKVAL_SRC_POS);

        Assert(aHD->i_getChildren().size() == 0);

        if (aHD->i_getParent().isNull())
        {
            Assert(!aHDLockToken.isNull());
            if (!aHDLockToken.isNull())
            {
                HRESULT hrc = aHDLockToken->Abandon();
                AssertComRC(hrc);
            }
        }
        else
        {
            HRESULT hrc = aHD->i_unmarkForDeletion();
            AssertComRC(hrc);
        }
    }
    else
    {
        if (fNeedsOnlineMerge)
        {
            // Online merge uses the medium lock list of the VM, so give
            // an empty list to cancelMergeTo so that it works as designed.
            aSource->i_cancelMergeTo(aChildrenToReparent, new MediumLockList());

            // clean up the VM medium lock list ourselves
            MediumLockList::Base::iterator lockListBegin =
                aMediumLockList->GetBegin();
            MediumLockList::Base::iterator lockListEnd =
                aMediumLockList->GetEnd();
            MediumLockList::Base::iterator lockListLast = lockListEnd;
            --lockListLast;
            for (MediumLockList::Base::iterator it = lockListBegin;
                 it != lockListEnd;
                 ++it)
            {
                ComObjPtr<Medium> pMedium = it->GetMedium();
                AutoWriteLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
                if (pMedium->i_getState() == MediumState_Deleting)
                    pMedium->i_unmarkForDeletion();
                else
                {
                    // blindly apply this, only needed for medium objects which
                    // would be deleted as part of the merge
                    pMedium->i_unmarkLockedForDeletion();
                }
                mediumLock.release();
                it->UpdateLock(it == lockListLast);
                mediumLock.acquire();
            }
        }
        else
        {
            aSource->i_cancelMergeTo(aChildrenToReparent, aMediumLockList);
        }
    }

    if (aMachineId.isValid() && !aMachineId.isZero())
    {
        // reattach the source media to the snapshot
        HRESULT hrc = aSource->i_addBackReference(aMachineId, aSnapshotId);
        AssertComRC(hrc);
    }
}

/**
 * Perform an online merge of a hard disk, i.e. the equivalent of
 * Medium::mergeTo(), just for running VMs. If this fails you need to call
 * #cancelDeleteSnapshotMedium().
 *
 * @return COM status code
 * @param aMediumAttachment Identify where the disk is attached in the VM.
 * @param aSource       Source hard disk for merge.
 * @param aTarget       Target hard disk for merge.
 * @param fMergeForward Merge direction.
 * @param aParentForTarget New parent if target needs to be reparented.
 * @param aChildrenToReparent Medium lock list with children which have to be
 *                      reparented to the target.
 * @param aMediumLockList Where to store the created medium lock list (may
 *                      return NULL if no real merge is necessary).
 * @param aProgress     Progress indicator.
 * @param pfNeedsMachineSaveSettings Whether the VM settings need to be saved (out).
 */
HRESULT SessionMachine::i_onlineMergeMedium(const ComObjPtr<MediumAttachment> &aMediumAttachment,
                                            const ComObjPtr<Medium> &aSource,
                                            const ComObjPtr<Medium> &aTarget,
                                            bool fMergeForward,
                                            const ComObjPtr<Medium> &aParentForTarget,
                                            MediumLockList *aChildrenToReparent,
                                            MediumLockList *aMediumLockList,
                                            ComObjPtr<Progress> &aProgress,
                                            bool *pfNeedsMachineSaveSettings)
{
    AssertReturn(aSource != NULL, E_FAIL);
    AssertReturn(aTarget != NULL, E_FAIL);
    AssertReturn(aSource != aTarget, E_FAIL);
    AssertReturn(aMediumLockList != NULL, E_FAIL);
    NOREF(fMergeForward);
    NOREF(aParentForTarget);
    NOREF(aChildrenToReparent);

    HRESULT hrc = S_OK;

    try
    {
        // Similar code appears in Medium::taskMergeHandle, so
        // if you make any changes below check whether they are applicable
        // in that context as well.

        unsigned uTargetIdx = (unsigned)-1;
        unsigned uSourceIdx = (unsigned)-1;
        /* Sanity check all hard disks in the chain. */
        MediumLockList::Base::iterator lockListBegin =
            aMediumLockList->GetBegin();
        MediumLockList::Base::iterator lockListEnd =
            aMediumLockList->GetEnd();
        unsigned i = 0;
        for (MediumLockList::Base::iterator it = lockListBegin;
             it != lockListEnd;
             ++it)
        {
            MediumLock &mediumLock = *it;
            const ComObjPtr<Medium> &pMedium = mediumLock.GetMedium();

            if (pMedium == aSource)
                uSourceIdx = i;
            else if (pMedium == aTarget)
                uTargetIdx = i;

            // In Medium::taskMergeHandler there is lots of consistency
            // checking which we cannot do here, as the state details are
            // impossible to get outside the Medium class. The locking should
            // have done the checks already.

            i++;
        }

        ComAssertThrow(   uSourceIdx != (unsigned)-1
                       && uTargetIdx != (unsigned)-1, E_FAIL);

        ComPtr<IInternalSessionControl> directControl;
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

            if (mData->mSession.mState != SessionState_Locked)
                throw setError(VBOX_E_INVALID_VM_STATE,
                                tr("Machine is not locked by a session (session state: %s)"),
                                Global::stringifySessionState(mData->mSession.mState));
            directControl = mData->mSession.mDirectControl;
        }

        // Must not hold any locks here, as this will call back to finish
        // updating the medium attachment, chain linking and state.
        hrc = directControl->OnlineMergeMedium(aMediumAttachment, uSourceIdx, uTargetIdx, aProgress);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    // The callback mentioned above takes care of update the medium state

    if (pfNeedsMachineSaveSettings)
        *pfNeedsMachineSaveSettings = true;

    return hrc;
}

/**
 * Implementation for IInternalMachineControl::finishOnlineMergeMedium().
 *
 * Gets called after the successful completion of an online merge from
 * Console::onlineMergeMedium(), which gets invoked indirectly above in
 * the call to IInternalSessionControl::onlineMergeMedium.
 *
 * This updates the medium information and medium state so that the VM
 * can continue with the updated state of the medium chain.
 */
HRESULT SessionMachine::finishOnlineMergeMedium()
{
    HRESULT hrc = S_OK;
    MediumDeleteRec *pDeleteRec = (MediumDeleteRec *)mConsoleTaskData.mDeleteSnapshotInfo;
    AssertReturn(pDeleteRec, E_FAIL);
    bool fSourceHasChildren = false;

    // all hard disks but the target were successfully deleted by
    // the merge; reparent target if necessary and uninitialize media

    AutoWriteLock treeLock(mParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    // Declare this here to make sure the object does not get uninitialized
    // before this method completes. Would normally happen as halfway through
    // we delete the last reference to the no longer existing medium object.
    ComObjPtr<Medium> targetChild;

    if (pDeleteRec->mfMergeForward)
    {
        // first, unregister the target since it may become a base
        // hard disk which needs re-registration
        hrc = mParent->i_unregisterMedium(pDeleteRec->mpTarget);
        AssertComRC(hrc);

        // then, reparent it and disconnect the deleted branch at
        // both ends (chain->parent() is source's parent)
        pDeleteRec->mpTarget->i_deparent();
        pDeleteRec->mpTarget->i_setParent(pDeleteRec->mpParentForTarget);
        if (pDeleteRec->mpParentForTarget)
            pDeleteRec->mpSource->i_deparent();

        // then, register again
        hrc = mParent->i_registerMedium(pDeleteRec->mpTarget, &pDeleteRec->mpTarget, treeLock);
        AssertComRC(hrc);
    }
    else
    {
        Assert(pDeleteRec->mpTarget->i_getChildren().size() == 1);
        targetChild = pDeleteRec->mpTarget->i_getChildren().front();

        // disconnect the deleted branch at the elder end
        targetChild->i_deparent();

        // Update parent UUIDs of the source's children, reparent them and
        // disconnect the deleted branch at the younger end
        if (pDeleteRec->mpChildrenToReparent && !pDeleteRec->mpChildrenToReparent->IsEmpty())
        {
            fSourceHasChildren = true;
            // Fix the parent UUID of the images which needs to be moved to
            // underneath target. The running machine has the images opened,
            // but only for reading since the VM is paused. If anything fails
            // we must continue. The worst possible result is that the images
            // need manual fixing via VBoxManage to adjust the parent UUID.
            treeLock.release();
            pDeleteRec->mpTarget->i_fixParentUuidOfChildren(pDeleteRec->mpChildrenToReparent);
            // The childen are still write locked, unlock them now and don't
            // rely on the destructor doing it very late.
            pDeleteRec->mpChildrenToReparent->Unlock();
            treeLock.acquire();

            // obey {parent,child} lock order
            AutoWriteLock sourceLock(pDeleteRec->mpSource COMMA_LOCKVAL_SRC_POS);

            MediumLockList::Base::iterator childrenBegin = pDeleteRec->mpChildrenToReparent->GetBegin();
            MediumLockList::Base::iterator childrenEnd = pDeleteRec->mpChildrenToReparent->GetEnd();
            for (MediumLockList::Base::iterator it = childrenBegin;
                 it != childrenEnd;
                 ++it)
            {
                Medium *pMedium = it->GetMedium();
                AutoWriteLock childLock(pMedium COMMA_LOCKVAL_SRC_POS);

                pMedium->i_deparent();  // removes pMedium from source
                pMedium->i_setParent(pDeleteRec->mpTarget);
            }
        }
    }

    /* unregister and uninitialize all hard disks removed by the merge */
    MediumLockList *pMediumLockList = NULL;
    hrc = mData->mSession.mLockedMedia.Get(pDeleteRec->mpOnlineMediumAttachment, pMediumLockList);
    const ComObjPtr<Medium> &pLast = pDeleteRec->mfMergeForward ? pDeleteRec->mpTarget : pDeleteRec->mpSource;
    AssertReturn(SUCCEEDED(hrc) && pMediumLockList, E_FAIL);
    MediumLockList::Base::iterator lockListBegin =
        pMediumLockList->GetBegin();
    MediumLockList::Base::iterator lockListEnd =
        pMediumLockList->GetEnd();
    for (MediumLockList::Base::iterator it = lockListBegin;
         it != lockListEnd;
         )
    {
        MediumLock &mediumLock = *it;
        /* Create a real copy of the medium pointer, as the medium
         * lock deletion below would invalidate the referenced object. */
        const ComObjPtr<Medium> pMedium = mediumLock.GetMedium();

        /* The target and all images not merged (readonly) are skipped */
        if (   pMedium == pDeleteRec->mpTarget
            || pMedium->i_getState() == MediumState_LockedRead)
        {
            ++it;
        }
        else
        {
            hrc = mParent->i_unregisterMedium(pMedium);
            AssertComRC(hrc);

            /* now, uninitialize the deleted hard disk (note that
             * due to the Deleting state, uninit() will not touch
             * the parent-child relationship so we need to
             * uninitialize each disk individually) */

            /* note that the operation initiator hard disk (which is
             * normally also the source hard disk) is a special case
             * -- there is one more caller added by Task to it which
             * we must release. Also, if we are in sync mode, the
             * caller may still hold an AutoCaller instance for it
             * and therefore we cannot uninit() it (it's therefore
             * the caller's responsibility) */
            if (pMedium == pDeleteRec->mpSource)
            {
                Assert(pDeleteRec->mpSource->i_getChildren().size() == 0);
                Assert(pDeleteRec->mpSource->i_getFirstMachineBackrefId() == NULL);
            }

            /* Delete the medium lock list entry, which also releases the
             * caller added by MergeChain before uninit() and updates the
             * iterator to point to the right place. */
            hrc = pMediumLockList->RemoveByIterator(it);
            AssertComRC(hrc);

            treeLock.release();
            pMedium->uninit();
            treeLock.acquire();
        }

        /* Stop as soon as we reached the last medium affected by the merge.
         * The remaining images must be kept unchanged. */
        if (pMedium == pLast)
            break;
    }

    /* Could be in principle folded into the previous loop, but let's keep
     * things simple. Update the medium locking to be the standard state:
     * all parent images locked for reading, just the last diff for writing. */
    lockListBegin = pMediumLockList->GetBegin();
    lockListEnd = pMediumLockList->GetEnd();
    MediumLockList::Base::iterator lockListLast = lockListEnd;
    --lockListLast;
    for (MediumLockList::Base::iterator it = lockListBegin;
         it != lockListEnd;
         ++it)
    {
         it->UpdateLock(it == lockListLast);
    }

    /* If this is a backwards merge of the only remaining snapshot (i.e. the
     * source has no children) then update the medium associated with the
     * attachment, as the previously associated one (source) is now deleted.
     * Without the immediate update the VM could not continue running. */
    if (!pDeleteRec->mfMergeForward && !fSourceHasChildren)
    {
        AutoWriteLock attLock(pDeleteRec->mpOnlineMediumAttachment COMMA_LOCKVAL_SRC_POS);
        pDeleteRec->mpOnlineMediumAttachment->i_updateMedium(pDeleteRec->mpTarget);
    }

    return S_OK;
}
