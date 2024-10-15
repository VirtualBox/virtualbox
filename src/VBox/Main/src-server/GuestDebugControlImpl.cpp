/* $Id: GuestDebugControlImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox/GuestDebugControl COM class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_GUESTDEBUGCONTROL
#include "GuestDebugControlImpl.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

//////////////////////////////////////////////////////////////////////////////////
//
// GuestDebugControl private data definition
//
//////////////////////////////////////////////////////////////////////////////////

struct GuestDebugControl::Data
{
    Data()
        : pMachine(NULL)
    { }

    Machine * const                     pMachine;
    const ComObjPtr<GuestDebugControl>  pPeer;
    Backupable<settings::Debugging>     bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestDebugControl)

HRESULT GuestDebugControl::FinalConstruct()
{
    return BaseFinalConstruct();
}

void GuestDebugControl::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the Guest Debug Control object.
 *
 *  @param aParent  Handle of the parent object.
 */
HRESULT GuestDebugControl::init(Machine *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    /* m->pPeer is left null */

    m->bd.allocate();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the Guest Debug Control object given another Guest Debug Control object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT GuestDebugControl::init(Machine *aParent, GuestDebugControl *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    unconst(m->pPeer) = aThat;

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT GuestDebugControl::initCopy(Machine *aParent, GuestDebugControl *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    /* pPeer is left null */

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.attachCopy(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void GuestDebugControl::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;
}

// IGuestDebugControl properties
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDebugControl::getDebugProvider(GuestDebugProvider_T *aDebugProvider)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDebugProvider = m->bd->enmDbgProvider;
    return S_OK;
}


HRESULT GuestDebugControl::setDebugProvider(GuestDebugProvider_T aDebugProvider)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->enmDbgProvider != aDebugProvider)
    {
        m->bd.backup();
        m->bd->enmDbgProvider = aDebugProvider;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_GuestDebugControl);
        mlock.release();

        m->pMachine->i_onGuestDebugControlChange(this);
    }

    return S_OK;
}


HRESULT GuestDebugControl::getDebugIoProvider(GuestDebugIoProvider_T *aDebugIoProvider)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDebugIoProvider = m->bd->enmIoProvider;
    return S_OK;
}

HRESULT GuestDebugControl::setDebugIoProvider(GuestDebugIoProvider_T aDebugIoProvider)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->enmIoProvider != aDebugIoProvider)
    {
        m->bd.backup();
        m->bd->enmIoProvider = aDebugIoProvider;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_GuestDebugControl);
        mlock.release();

        m->pMachine->i_onGuestDebugControlChange(this);
    }

    return S_OK;
}

HRESULT GuestDebugControl::getDebugAddress(com::Utf8Str &aAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aAddress = m->bd->strAddress;

    return S_OK;
}


HRESULT GuestDebugControl::setDebugAddress(const com::Utf8Str &aAddress)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aAddress != m->bd->strAddress)
    {
        m->bd.backup();
        m->bd->strAddress = aAddress;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_GuestDebugControl);
        mlock.release();

        m->pMachine->i_onGuestDebugControlChange(this);
    }

    return S_OK;
}

HRESULT GuestDebugControl::getDebugPort(ULONG *aPort)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aPort = m->bd->ulPort;
    return S_OK;
}

HRESULT GuestDebugControl::setDebugPort(ULONG aPort)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->ulPort != aPort)
    {
        m->bd.backup();
        m->bd->ulPort = aPort;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_GuestDebugControl);
        mlock.release();

        m->pMachine->i_onGuestDebugControlChange(this);
    }

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 *  Loads debug settings from the given settings.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT GuestDebugControl::i_loadSettings(const settings::Debugging &data)
{

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    *m->bd.data() = data;

    return S_OK;
}

/**
 *  Saves the debug settings to the given settings.
 *
 *  Note that the given Port node is completely empty on input.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT GuestDebugControl::i_saveSettings(settings::Debugging &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    data = *m->bd.data();

    return S_OK;
}

/**
 *  @note Locks this object for writing.
 */
void GuestDebugControl::i_rollback()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void GuestDebugControl::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (pPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void GuestDebugControl::i_copyFrom(GuestDebugControl *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);
}
