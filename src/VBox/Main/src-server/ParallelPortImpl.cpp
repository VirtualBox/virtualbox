/* $Id: ParallelPortImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_PARALLELPORT
#include "ParallelPortImpl.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"

#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

////////////////////////////////////////////////////////////////////////////////
//
// ParallelPort private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct ParallelPort::Data
{
    Data()
        : fModified(false),
          pMachine(NULL)
    { }

    bool                                    fModified;

    Machine * const                         pMachine;
    const ComObjPtr<ParallelPort>           pPeer;

    Backupable<settings::ParallelPort>      bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
DEFINE_EMPTY_CTOR_DTOR(ParallelPort)

HRESULT ParallelPort::FinalConstruct()
{
    return BaseFinalConstruct();
}

void ParallelPort::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the Parallel Port object.
 *
 *  @param aParent  Handle of the parent object.
 *  @param aSlot    Slotnumber this parallel port is plugged into.
 */
HRESULT ParallelPort::init(Machine *aParent, ULONG aSlot)
{
    LogFlowThisFunc(("aParent=%p, aSlot=%d\n", aParent, aSlot));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data;

    unconst(m->pMachine) = aParent;
    /* m->pPeer is left null */

    m->bd.allocate();

    /* initialize data */
    m->bd->ulSlot = aSlot;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the Parallel Port object given another serial port object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT ParallelPort::init(Machine *aParent, ParallelPort *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data;

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
HRESULT ParallelPort::initCopy(Machine *aParent, ParallelPort *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data;

    unconst(m->pMachine) = aParent;
    /* m->pPeer is left null */

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
void ParallelPort::uninit()
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

// IParallelPort properties
/////////////////////////////////////////////////////////////////////////////

HRESULT ParallelPort::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = m->bd->fEnabled;

    return S_OK;
}

HRESULT ParallelPort::setEnabled(BOOL aEnabled)
{
    LogFlowThisFunc(("aEnabled=%RTbool\n", aEnabled));
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fEnabled != RT_BOOL(aEnabled))
    {
        m->bd.backup();
        m->bd->fEnabled = RT_BOOL(aEnabled);

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_ParallelPorts);
        mlock.release();

        m->pMachine->i_onParallelPortChange(this);
    }

    return S_OK;
}

HRESULT ParallelPort::getSlot(ULONG *aSlot)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aSlot = m->bd->ulSlot;

    return S_OK;
}

HRESULT ParallelPort::getIRQ(ULONG *aIRQ)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIRQ = m->bd->ulIRQ;

    return S_OK;
}

HRESULT ParallelPort::setIRQ(ULONG aIRQ)
{
    /* check IRQ limits
     * (when changing this, make sure it corresponds to XML schema */
    if (aIRQ > 255)
        return setError(E_INVALIDARG,
                        tr("Invalid IRQ number of the parallel port %d: %lu (must be in range [0, %lu])"),
                        m->bd->ulSlot, aIRQ, 255);

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->ulIRQ != aIRQ)
    {
        m->bd.backup();
        m->bd->ulIRQ = aIRQ;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_ParallelPorts);
        mlock.release();

        m->pMachine->i_onParallelPortChange(this);
    }

    return S_OK;
}

HRESULT ParallelPort::getIOBase(ULONG *aIOBase)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIOBase = m->bd->ulIOBase;

    return S_OK;
}

HRESULT ParallelPort::setIOBase(ULONG aIOBase)
{
    /* check IOBase limits
     * (when changing this, make sure it corresponds to XML schema */
    if (aIOBase > 0xFFFF)
        return setError(E_INVALIDARG,
                        tr("Invalid I/O port base address of the parallel port %d: %lu (must be in range [0, 0x%X])"),
                        m->bd->ulSlot, aIOBase, 0, 0xFFFF);

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->ulIOBase != aIOBase)
    {
        m->bd.backup();
        m->bd->ulIOBase = aIOBase;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_ParallelPorts);
        mlock.release();

        m->pMachine->i_onParallelPortChange(this);
    }

    return S_OK;
}


HRESULT ParallelPort::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aPath = m->bd->strPath;
    return S_OK;
}


HRESULT ParallelPort::setPath(const com::Utf8Str &aPath)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aPath != m->bd->strPath)
    {
        m->bd.backup();
        m->bd->strPath = aPath;

        m->fModified = true;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_ParallelPorts);
        mlock.release();

        return m->pMachine->i_onParallelPortChange(this);
    }

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given port node.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT ParallelPort::i_loadSettings(const settings::ParallelPort &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    *m->bd.data() = data;

    return S_OK;
}

/**
 *  Saves settings to the given port node.
 *
 *  Note that the given Port node is completely empty on input.
 *
 *  @param  data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT ParallelPort::i_saveSettings(settings::ParallelPort &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    data = *m->bd.data();

    return S_OK;
}

/**
 * Returns true if any setter method has modified settings of this instance.
 * @return
 */
bool ParallelPort::i_isModified()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->fModified;
}

/**
 *  @note Locks this object for writing.
 */
void ParallelPort::i_rollback()
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
void ParallelPort::i_commit()
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
            /* attach new data to the peer and reshare it */
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void ParallelPort::i_copyFrom(ParallelPort *aThat)
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

/**
 * Applies the defaults for this parallel port.
 *
 * @note This method currently assumes that the object is in the state after
 * calling init(), it does not set defaults from an arbitrary state.
 */
void ParallelPort::i_applyDefaults()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Set some more defaults based on the slot. */
    switch (m->bd->ulSlot)
    {
        case 0:
        {
            m->bd->ulIOBase = 0x378;
            m->bd->ulIRQ = 7;
            break;
        }
        case 1:
        {
            m->bd->ulIOBase = 0x278;
            m->bd->ulIRQ = 5;
            break;
        }
        default:
            AssertMsgFailed(("Parallel port slot %u exceeds limit\n", m->bd->ulSlot));
            break;
    }
}

bool ParallelPort::i_hasDefaults()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), true);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!m->bd->fEnabled)
    {
        /* Could be default, check the IO base and IRQ. */
        switch (m->bd->ulSlot)
        {
            case 0:
                if (m->bd->ulIOBase == 0x378 && m->bd->ulIRQ == 7)
                    return true;
                break;
            case 1:
                if (m->bd->ulIOBase == 0x278 && m->bd->ulIRQ == 5)
                    return true;
                break;
            default:
                AssertMsgFailed(("Parallel port slot %u exceeds limit\n", m->bd->ulSlot));
                break;
        }

        /* Detect old-style defaults (0x378, irq 4) in any slot, they are still
         * in place for many VMs created by old VirtualBox versions. */
        if (m->bd->ulIOBase == 0x378 && m->bd->ulIRQ == 4)
            return true;
    }

    return false;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
