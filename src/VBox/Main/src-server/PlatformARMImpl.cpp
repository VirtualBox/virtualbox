/* $Id: PlatformARMImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - ARM platform settings.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_PLATFORMARM
#include "MachineImpl.h"
#include "PlatformARMImpl.h"
#include "PlatformImpl.h"
#include "LoggingNew.h"

#include <VBox/settings.h>

#include <iprt/cpp/utils.h>


/**
 * ARM-specific platform data.
 *
 * This data is unique for a machine and for every machine snapshot.
 * Stored using the util::Backupable template in the |mPlatformARMData| variable.
 *
 * SessionMachine instances can alter this data and discard changes.
 */
struct Data
{
    Data() { }

    ComObjPtr<PlatformARM> pPeer;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::PlatformARM> bd;
};


/*
 * PlatformARM implementation.
 */
PlatformARM::PlatformARM()
{
}

PlatformARM::~PlatformARM()
{
    uninit();
}

HRESULT PlatformARM::FinalConstruct()
{
    return BaseFinalConstruct();
}

void PlatformARM::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT PlatformARM::init(Platform *aParent, Machine *aMachine)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share the parent + machine weakly */
    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data;
    m->bd.allocate();

    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the platform object given another platform object
 * (a kind of copy constructor). This object shares data with
 * the object passed as an argument.
 *
 * @note This object must be destroyed before the original object
 *       it shares data with is destroyed.
 */
HRESULT PlatformARM::init(Platform *aParent, Machine *aMachine, PlatformARM *aThat)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    ComAssertRet(aParent && aParent, E_INVALIDARG);

    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data();
    m->pPeer = aThat;

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the guest object given another guest object
 * (a kind of copy constructor). This object makes a private copy of data
 * of the original object passed as an argument.
 */
HRESULT PlatformARM::initCopy(Platform *aParent, Machine *aMachine, PlatformARM *aThat)
{
    ComAssertRet(aParent && aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data();
    // m->pPeer is left null

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS); /** @todo r=andy Shouldn't a read lock be sufficient here? */
    m->bd.attachCopy(aThat->m->bd);

    autoInitSpan.setSucceeded();

    return S_OK;
}

void PlatformARM::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mMachine) = NULL;

    if (m)
    {
        m->bd.free();
        unconst(m->pPeer) = NULL;

        delete m;
        m = NULL;
    }
}

void PlatformARM::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (m)
        m->bd.rollback();
}

void PlatformARM::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

void PlatformARM::i_copyFrom(PlatformARM *aThat)
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
 * Loads settings from the given platform ARM node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for writing.
 */
HRESULT PlatformARM::i_loadSettings(const settings::PlatformARM &data)
{
    RT_NOREF(data);

    /* Nothing here yet. */
    return S_OK;
}

/**
 * Saves settings to the given platform ARM node.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for reading.
 */
HRESULT PlatformARM::i_saveSettings(settings::PlatformARM &data)
{
    RT_NOREF(data);

    /* Nothing here yet. */
    return S_OK;
}

HRESULT PlatformARM::i_applyDefaults(GuestOSType *aOsType)
{
    RT_NOREF(aOsType);

    /* Nothing here yet. */
    return S_OK;
}
