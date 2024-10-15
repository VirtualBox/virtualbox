/* $Id: MediumAttachmentImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_MEDIUMATTACHMENT
#include "MediumAttachmentImpl.h"
#include "MachineImpl.h"
#include "MediumImpl.h"
#include "Global.h"
#include "StringifyEnums.h"

#include "AutoCaller.h"
#include "LoggingNew.h"

#include <iprt/cpp/utils.h>

////////////////////////////////////////////////////////////////////////////////
//
// private member data definition
//
////////////////////////////////////////////////////////////////////////////////

struct BackupableMediumAttachmentData
{
    BackupableMediumAttachmentData()
          : fImplicit(false)
    { }

    ComObjPtr<Medium>        pMedium;
    /* Since MediumAttachment is not a first class citizen when it
     * comes to managing settings, having a reference to the storage
     * controller will not work - when settings are changed it will point
     * to the old, uninitialized instance. Changing this requires
     * substantial changes to MediumImpl.cpp. */
    /* Same counts for the assigned bandwidth group */
    bool                     fImplicit;
    const Utf8Str            strControllerName;
    settings::AttachedDevice mData;
};

struct MediumAttachment::Data
{
    Data(Machine * const aMachine = NULL)
        : pMachine(aMachine),
          fIsEjected(false)
    { }

    /** Reference to Machine object, for checking mutable state. */
    Machine * const                            pMachine;
    /* later: const ComObjPtr<MediumAttachment> mPeer; */
    bool                                       fIsEjected;
    Backupable<BackupableMediumAttachmentData> bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(MediumAttachment)

HRESULT MediumAttachment::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void MediumAttachment::FinalRelease()
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the medium attachment object.
 *
 * @param aParent           Machine object.
 * @param aMedium           Medium object.
 * @param aControllerName   Controller the hard disk is attached to.
 * @param aPort             Port number.
 * @param aDevice           Device number on the port.
 * @param aType             Device type.
 * @param aImplicit
 * @param aPassthrough      Whether accesses are directly passed to the host drive.
 * @param aTempEject        Whether guest-triggered eject results in unmounting the medium.
 * @param aNonRotational    Whether this medium is non-rotational (aka SSD).
 * @param aDiscard          Whether this medium supports discarding unused blocks.
 * @param aHotPluggable     Whether this medium is hot-pluggable.
 * @param strBandwidthGroup Bandwidth group.
 */
HRESULT MediumAttachment::init(Machine *aParent,
                               Medium *aMedium,
                               const Utf8Str &aControllerName,
                               LONG aPort,
                               LONG aDevice,
                               DeviceType_T aType,
                               bool aImplicit,
                               bool aPassthrough,
                               bool aTempEject,
                               bool aNonRotational,
                               bool aDiscard,
                               bool aHotPluggable,
                               const Utf8Str &strBandwidthGroup)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent=%p aMedium=%p aControllerName=%s aPort=%d aDevice=%d aType=%d aImplicit=%d aPassthrough=%d aTempEject=%d aNonRotational=%d aDiscard=%d aHotPluggable=%d strBandwithGroup=%s\n", aParent, aMedium, aControllerName.c_str(), aPort, aDevice, aType, aImplicit, aPassthrough, aTempEject, aNonRotational, aDiscard, aHotPluggable, strBandwidthGroup.c_str()));

    if (aType == DeviceType_HardDisk)
        AssertReturn(aMedium, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;

    m->bd.allocate();
    m->bd->pMedium = aMedium;
    m->bd->mData.strBwGroup = strBandwidthGroup;
    unconst(m->bd->strControllerName) = aControllerName;
    m->bd->mData.lPort = aPort;
    m->bd->mData.lDevice = aDevice;
    m->bd->mData.deviceType = aType;

    m->bd->mData.fPassThrough = aPassthrough;
    m->bd->mData.fTempEject = aTempEject;
    m->bd->mData.fNonRotational = aNonRotational;
    m->bd->mData.fDiscard = aDiscard;
    m->bd->fImplicit = aImplicit;
    m->bd->mData.fHotPluggable = aHotPluggable;

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

    /* Construct a short log name for this attachment. */
    i_updateLogName();

    LogFlowThisFunc(("LEAVE - %s\n", i_getLogName()));
    return S_OK;
}

/**
 *  Initializes the medium attachment object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT MediumAttachment::initCopy(Machine *aParent, MediumAttachment *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);
    /* m->pPeer is left null */

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.attachCopy(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    /* Construct a short log name for this attachment. */
    i_updateLogName();

    LogFlowThisFunc(("LEAVE - %s\n", i_getLogName()));
    return S_OK;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void MediumAttachment::uninit()
{
    LogFlowThisFunc(("ENTER - %s\n", i_getLogName()));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->bd.free();

    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}

// IHardDiskAttachment properties
/////////////////////////////////////////////////////////////////////////////


HRESULT MediumAttachment::getMachine(ComPtr<IMachine> &aMachine)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<Machine> pMachine(m->pMachine);
    pMachine.queryInterfaceTo(aMachine.asOutParam());

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getMedium(ComPtr<IMedium> &aHardDisk)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aHardDisk = m->bd->pMedium;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getController(com::Utf8Str &aController)
{
    LogFlowThisFuncEnter();

    /* m->controller is constant during life time, no need to lock */
    aController = Utf8Str(m->bd->strControllerName);

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getPort(LONG *aPort)
{
    LogFlowThisFuncEnter();

    /* m->bd->port is constant during life time, no need to lock */
    *aPort = m->bd->mData.lPort;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT  MediumAttachment::getDevice(LONG *aDevice)
{
    LogFlowThisFuncEnter();

    /* m->bd->device is constant during life time, no need to lock */
    *aDevice = m->bd->mData.lDevice;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT MediumAttachment::getType(DeviceType_T *aType)
{
    LogFlowThisFuncEnter();

    /* m->bd->type is constant during life time, no need to lock */
    *aType = m->bd->mData.deviceType;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getPassthrough(BOOL *aPassthrough)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aPassthrough = m->bd->mData.fPassThrough;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getTemporaryEject(BOOL *aTemporaryEject)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aTemporaryEject = m->bd->mData.fTempEject;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getIsEjected(BOOL *aEjected)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aEjected = m->fIsEjected;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getNonRotational(BOOL *aNonRotational)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aNonRotational = m->bd->mData.fNonRotational;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT MediumAttachment::getDiscard(BOOL *aDiscard)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aDiscard = m->bd->mData.fDiscard;

    LogFlowThisFuncLeave();
    return S_OK;
}


HRESULT MediumAttachment::getBandwidthGroup(ComPtr<IBandwidthGroup> &aBandwidthGroup)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;
    if (m->bd->mData.strBwGroup.isNotEmpty())
    {
        ComObjPtr<BandwidthGroup> pBwGroup;
        hrc = m->pMachine->i_getBandwidthGroup(m->bd->mData.strBwGroup, pBwGroup, true /* fSetError */);

        Assert(SUCCEEDED(hrc)); /* This is not allowed to fail because the existence of the
                                   group was checked when it was attached. */

        if (SUCCEEDED(hrc))
            pBwGroup.queryInterfaceTo(aBandwidthGroup.asOutParam());
    }

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT MediumAttachment::getHotPluggable(BOOL *aHotPluggable)
{
    LogFlowThisFuncEnter();

    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);

    *aHotPluggable = m->bd->mData.fHotPluggable;

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  @note Locks this object for writing.
 */
void MediumAttachment::i_rollback()
{
    LogFlowThisFunc(("ENTER - %s\n", i_getLogName()));

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();

    LogFlowThisFunc(("LEAVE - %s\n", i_getLogName()));
}

/**
 *  @note Locks this object for writing.
 */
void MediumAttachment::i_commit()
{
    LogFlowThisFunc(("ENTER - %s\n", i_getLogName()));

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
        m->bd.commit();

    LogFlowThisFunc(("LEAVE - %s\n", i_getLogName()));
}

bool MediumAttachment::i_isImplicit() const
{
    return m->bd->fImplicit;
}

void MediumAttachment::i_setImplicit(bool aImplicit)
{
    Assert(!m->pMachine->i_isSnapshotMachine());
    m->bd->fImplicit = aImplicit;

    /* Construct a short log name for this attachment. */
    i_updateLogName();
}

const ComObjPtr<Medium>& MediumAttachment::i_getMedium() const
{
    return m->bd->pMedium;
}

const Utf8Str &MediumAttachment::i_getControllerName() const
{
    return m->bd->strControllerName;
}

LONG MediumAttachment::i_getPort() const
{
    return m->bd->mData.lPort;
}

LONG MediumAttachment::i_getDevice() const
{
    return m->bd->mData.lDevice;
}

DeviceType_T MediumAttachment::i_getType() const
{
    return m->bd->mData.deviceType;
}

bool MediumAttachment::i_getPassthrough() const
{
    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);
    return m->bd->mData.fPassThrough;
}

bool MediumAttachment::i_getTempEject() const
{
    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);
    return m->bd->mData.fTempEject;
}

bool MediumAttachment::i_getNonRotational() const
{
    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);
    return m->bd->mData.fNonRotational;
}

bool MediumAttachment::i_getDiscard() const
{
    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);
    return m->bd->mData.fDiscard;
}

bool MediumAttachment::i_getHotPluggable() const
{
    AutoReadLock lock(this COMMA_LOCKVAL_SRC_POS);
    return m->bd->mData.fHotPluggable;
}

Utf8Str& MediumAttachment::i_getBandwidthGroup() const
{
    return m->bd->mData.strBwGroup;
}

bool MediumAttachment::i_matches(const Utf8Str &aControllerName, LONG aPort, LONG aDevice)
{
    return (    aControllerName == m->bd->strControllerName
             && aPort == m->bd->mData.lPort
             && aDevice == m->bd->mData.lDevice);
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateName(const Utf8Str &aName)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    unconst(m->bd->strControllerName) = aName;

    /* Construct a short log name for this attachment. */
    i_updateLogName();
}

/**
 * Sets the medium of this attachment and unsets the "implicit" flag.
 * @param aMedium
 */
void MediumAttachment::i_updateMedium(const ComObjPtr<Medium> &aMedium)
{
    Assert(isWriteLockOnCurrentThread());
    /* No assertion for a snapshot. Method used in deleting snapshot. */

    m->bd.backup();
    m->bd->pMedium = aMedium;
    m->bd->fImplicit = false;
    m->fIsEjected = false;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updatePassthrough(bool aPassthrough)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.fPassThrough = aPassthrough;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateTempEject(bool aTempEject)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.fTempEject = aTempEject;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateEjected()
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->fIsEjected = true;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateNonRotational(bool aNonRotational)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.fNonRotational = aNonRotational;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateDiscard(bool aDiscard)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.fDiscard = aDiscard;
}

/** Must be called from under this object's write lock. */
void MediumAttachment::i_updateHotPluggable(bool aHotPluggable)
{
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.fHotPluggable = aHotPluggable;
}

void MediumAttachment::i_updateBandwidthGroup(const Utf8Str &aBandwidthGroup)
{
    LogFlowThisFuncEnter();
    Assert(isWriteLockOnCurrentThread());
    Assert(!m->pMachine->i_isSnapshotMachine());

    m->bd.backup();
    m->bd->mData.strBwGroup = aBandwidthGroup;

    LogFlowThisFuncLeave();
}

void MediumAttachment::i_updateParentMachine(Machine * const pMachine)
{
    LogFlowThisFunc(("ENTER - %s\n", i_getLogName()));
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());
    Assert(!m->pMachine->i_isSnapshotMachine());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    unconst(m->pMachine) = pMachine;

    LogFlowThisFunc(("LEAVE - %s\n", i_getLogName()));
}

void MediumAttachment::i_updateLogName()
{
    const char *pszName = m->bd->strControllerName.c_str();
    const char *pszEndNick = strpbrk(pszName, " \t:-");
    mLogName = Utf8StrFmt("MA%p[%.*s:%u:%u:%s%s]",
                          this,
                          pszEndNick ? pszEndNick - pszName : 4, pszName,
                          m->bd->mData.lPort, m->bd->mData.lDevice, ::stringifyDeviceType(m->bd->mData.deviceType),
                          m->bd->fImplicit ? ":I" : "");
}
