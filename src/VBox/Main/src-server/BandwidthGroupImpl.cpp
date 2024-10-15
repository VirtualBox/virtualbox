/* $Id: BandwidthGroupImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_BANDWIDTHGROUP
#include "BandwidthGroupImpl.h"
#include "MachineImpl.h"
#include "Global.h"

#include "AutoCaller.h"
#include "LoggingNew.h"

#include <iprt/cpp/utils.h>

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
//
DEFINE_EMPTY_CTOR_DTOR(BandwidthGroup)

HRESULT BandwidthGroup::FinalConstruct()
{
    return BaseFinalConstruct();
}

void BandwidthGroup::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the bandwidth group object.
 *
 * @returns COM result indicator.
 * @param aParent       Pointer to our parent object.
 * @param aName         Name of the bandwidth group.
 * @param aType         Type of the bandwidth group (net, disk).
 * @param aMaxBytesPerSec Maximum bandwidth for the bandwidth group.
 */
HRESULT BandwidthGroup::init(BandwidthControl *aParent,
                             const Utf8Str &aName,
                             BandwidthGroupType_T aType,
                             LONG64 aMaxBytesPerSec)
{
    LogFlowThisFunc(("aParent=%p aName=\"%s\"\n",
                     aParent, aName.c_str()));

    ComAssertRet(aParent && !aName.isEmpty(), E_INVALIDARG);
    if (   (aType <= BandwidthGroupType_Null)
        || (aType >  BandwidthGroupType_Network))
        return setError(E_INVALIDARG,
                        tr("Invalid bandwidth group type"));

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* m->pPeer is left null */

    m->bd.allocate();

    m->bd->mData.strName = aName;
    m->bd->mData.enmType = aType;
    m->bd->cReferences = 0;
    m->bd->mData.cMaxBytesPerSec = (uint64_t)aMaxBytesPerSec;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the object given another object
 * (a kind of copy constructor). This object shares data with
 * the object passed as an argument.
 *
 * @param   aParent  Pointer to our parent object.
 * @param   aThat
 * @param   aReshare
 *     When false, the original object will remain a data owner.
 *     Otherwise, data ownership will be transferred from the original
 *     object to this one.
 *
 * @note This object must be destroyed before the original object
 * it shares data with is destroyed.
 *
 * @note Locks @a aThat object for writing if @a aReshare is @c true, or for
 * reading if @a aReshare is false.
 */
HRESULT BandwidthGroup::init(BandwidthControl *aParent,
                             BandwidthGroup *aThat,
                             bool aReshare /* = false */)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p, aReshare=%RTbool\n",
                      aParent, aThat, aReshare));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    if (aReshare)
    {
        AutoWriteLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

        unconst(aThat->m->pPeer) = this;
        m->bd.attach(aThat->m->bd);
    }
    else
    {
        unconst(m->pPeer) = aThat;

        AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
        m->bd.share(aThat->m->bd);
    }

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the bandwidth group object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT BandwidthGroup::initCopy(BandwidthControl *aParent, BandwidthGroup *aThat)
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

    return S_OK;
}


/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void BandwidthGroup::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pParent) = NULL;

    delete m;
    m = NULL;
}

HRESULT BandwidthGroup::getName(com::Utf8Str &aName)
{
    /* mName is constant during life time, no need to lock */
    aName = m->bd.data()->mData.strName;

    return S_OK;
}

HRESULT BandwidthGroup::getType(BandwidthGroupType_T *aType)
{
    /* type is constant during life time, no need to lock */
    *aType = m->bd->mData.enmType;

    return S_OK;
}

HRESULT BandwidthGroup::getReference(ULONG *aReferences)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aReferences = m->bd->cReferences;

    return S_OK;
}

HRESULT BandwidthGroup::getMaxBytesPerSec(LONG64 *aMaxBytesPerSec)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMaxBytesPerSec = (LONG64)m->bd->mData.cMaxBytesPerSec;

    return S_OK;
}

HRESULT BandwidthGroup::setMaxBytesPerSec(LONG64 aMaxBytesPerSec)
{
    if (aMaxBytesPerSec < 0)
        return setError(E_INVALIDARG,
                        tr("Bandwidth group limit cannot be negative"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->mData.cMaxBytesPerSec = (uint64_t)aMaxBytesPerSec;

    /* inform direct session if any. */
    ComObjPtr<Machine> pMachine = m->pParent->i_getMachine();
    alock.release();
    pMachine->i_onBandwidthGroupChange(this);

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/** @note Locks objects for writing! */
void BandwidthGroup::i_rollback()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void BandwidthGroup::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (m->pPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            // attach new data to the peer and reshare it
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}


/**
 *  Cancels sharing (if any) by making an independent copy of data.
 *  This operation also resets this object's peer to NULL.
 *
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void BandwidthGroup::i_unshare()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* peer is not modified, lock it for reading (m->pPeer is "master" so locked
     * first) */
    AutoReadLock rl(m->pPeer COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isShared())
    {
        if (!m->bd.isBackedUp())
            m->bd.backup();

        m->bd.commit();
    }

    unconst(m->pPeer) = NULL;
}

void BandwidthGroup::i_reference()
{
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);
    m->bd.backup();
    m->bd->cReferences++;
}

void BandwidthGroup::i_release()
{
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);
    m->bd.backup();
    m->bd->cReferences--;
}

