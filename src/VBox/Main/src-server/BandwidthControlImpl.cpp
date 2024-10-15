/* $Id: BandwidthControlImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_BANDWIDTHCONTROL
#include "BandwidthControlImpl.h"
#include "BandwidthGroupImpl.h"
#include "MachineImpl.h"
#include "Global.h"

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

#include <iprt/cpp/utils.h>
#include <VBox/com/array.h>
#include <VBox/param.h>
#include <algorithm>

// defines
/////////////////////////////////////////////////////////////////////////////

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
DEFINE_EMPTY_CTOR_DTOR(BandwidthControl)


HRESULT BandwidthControl::FinalConstruct()
{
    return BaseFinalConstruct();
}

void BandwidthControl::FinalRelease()
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
 */
HRESULT BandwidthControl::init(Machine *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* m->pPeer is left null */

    m->llBandwidthGroups.allocate();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the object given another object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for writing if @a aReshare is @c true, or for
 *  reading if @a aReshare is false.
 */
HRESULT BandwidthControl::init(Machine *aParent,
                               BandwidthControl *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    unconst(m->pPeer) = aThat;
    AutoWriteLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

    /* create copies of all groups */
    m->llBandwidthGroups.allocate();
    BandwidthGroupList::const_iterator it;
    for (it = aThat->m->llBandwidthGroups->begin();
         it != aThat->m->llBandwidthGroups->end();
         ++it)
    {
        ComObjPtr<BandwidthGroup> group;
        group.createObject();
        group->init(this, *it);
        m->llBandwidthGroups->push_back(group);
    }

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the bandwidth control object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT BandwidthControl::initCopy(Machine *aParent, BandwidthControl *aThat)
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

    /* create copies of all groups */
    m->llBandwidthGroups.allocate();
    BandwidthGroupList::const_iterator it;
    for (it = aThat->m->llBandwidthGroups->begin();
         it != aThat->m->llBandwidthGroups->end();
         ++it)
    {
        ComObjPtr<BandwidthGroup> group;
        group.createObject();
        group->initCopy(this, *it);
        m->llBandwidthGroups->push_back(group);
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void BandwidthControl::i_copyFrom(BandwidthControl *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* even more sanity */
    AutoAnyStateDependency adep(m->pParent);
    AssertComRCReturnVoid(adep.hrc());
    /* Machine::copyFrom() may not be called when the VM is running */
    AssertReturnVoid(!Global::IsOnline(adep.machineState()));

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* create private copies of all bandwidth groups */
    m->llBandwidthGroups.backup();
    m->llBandwidthGroups->clear();
    BandwidthGroupList::const_iterator it;
    for (it = aThat->m->llBandwidthGroups->begin();
         it != aThat->m->llBandwidthGroups->end();
         ++it)
    {
        ComObjPtr<BandwidthGroup> group;
        group.createObject();
        group->initCopy(this, *it);
        m->llBandwidthGroups->push_back(group);
    }
}

/** @note Locks objects for writing! */
void BandwidthControl::i_rollback()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* we need the machine state */
    AutoAnyStateDependency adep(m->pParent);
    AssertComRCReturnVoid(adep.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    BandwidthGroupList::const_iterator it;

    if (!m->llBandwidthGroups.isNull())
    {
        if (m->llBandwidthGroups.isBackedUp())
        {
            /* unitialize all new groups (absent in the backed up list). */
            BandwidthGroupList *backedList = m->llBandwidthGroups.backedUpData();
            for (it  = m->llBandwidthGroups->begin();
                 it != m->llBandwidthGroups->end();
                 ++it)
            {
                if (   std::find(backedList->begin(), backedList->end(), *it)
                    == backedList->end())
                    (*it)->uninit();
            }

            /* restore the list */
            m->llBandwidthGroups.rollback();
        }

        /* rollback any changes to groups after restoring the list */
        for (it = m->llBandwidthGroups->begin();
             it != m->llBandwidthGroups->end();
             ++it)
            (*it)->i_rollback();
    }
}

void BandwidthControl::i_commit()
{
    bool commitBandwidthGroups = false;
    BandwidthGroupList::const_iterator it;

    if (m->llBandwidthGroups.isBackedUp())
    {
        m->llBandwidthGroups.commit();

        if (m->pPeer)
        {
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);

            /* Commit all changes to new groups (this will reshare data with
             * peers for those who have peers) */
            BandwidthGroupList *newList = new BandwidthGroupList();
            for (it = m->llBandwidthGroups->begin();
                 it != m->llBandwidthGroups->end();
                 ++it)
            {
                (*it)->i_commit();

                /* look if this group has a peer group */
                ComObjPtr<BandwidthGroup> peer = (*it)->i_getPeer();
                if (!peer)
                {
                    /* no peer means the device is a newly created one;
                     * create a peer owning data this device share it with */
                    peer.createObject();
                    peer->init(m->pPeer, *it, true /* aReshare */);
                }
                else
                {
                    /* remove peer from the old list */
                    m->pPeer->m->llBandwidthGroups->remove(peer);
                }
                /* and add it to the new list */
                newList->push_back(peer);
            }

            /* uninit old peer's groups that are left */
            for (it = m->pPeer->m->llBandwidthGroups->begin();
                 it != m->pPeer->m->llBandwidthGroups->end();
                 ++it)
                (*it)->uninit();

            /* attach new list of groups to our peer */
            m->pPeer->m->llBandwidthGroups.attach(newList);
        }
        else
        {
            /* we have no peer (our parent is the newly created machine);
             * just commit changes to devices */
            commitBandwidthGroups = true;
        }
    }
    else
    {
        /* the list of groups itself is not changed,
         * just commit changes to groups themselves */
        commitBandwidthGroups = true;
    }

    if (commitBandwidthGroups)
    {
        for (it = m->llBandwidthGroups->begin();
             it != m->llBandwidthGroups->end();
             ++it)
            (*it)->i_commit();
    }
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void BandwidthControl::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    // uninit all groups on the list (it's a standard std::list not an ObjectsList
    // so we must uninit() manually)
    BandwidthGroupList::iterator it;
    for (it = m->llBandwidthGroups->begin();
         it != m->llBandwidthGroups->end();
         ++it)
        (*it)->uninit();

    m->llBandwidthGroups.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pParent) = NULL;

    delete m;
    m = NULL;
}

/**
 * Returns a bandwidth group object with the given name.
 *
 *  @param aName                 bandwidth group name to find
 *  @param aBandwidthGroup where to return the found bandwidth group
 *  @param aSetError             true to set extended error info on failure
 */
HRESULT BandwidthControl::i_getBandwidthGroupByName(const com::Utf8Str &aName,
                                                    ComObjPtr<BandwidthGroup> &aBandwidthGroup,
                                                    bool aSetError /* = false */)
{
    AssertReturn(!aName.isEmpty(), E_INVALIDARG);

    for (BandwidthGroupList::const_iterator it = m->llBandwidthGroups->begin();
         it != m->llBandwidthGroups->end();
         ++it)
    {
        if ((*it)->i_getName() == aName)
        {
            aBandwidthGroup = (*it);
            return S_OK;
        }
    }

    if (aSetError)
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                        tr("Could not find a bandwidth group named '%s'"),
                        aName.c_str());
    return VBOX_E_OBJECT_NOT_FOUND;
}
// To do
HRESULT BandwidthControl::createBandwidthGroup(const com::Utf8Str &aName,
                                               BandwidthGroupType_T aType,
                                               LONG64 aMaxBytesPerSec)
{
    /*
     * Validate input.
     */
    if (aMaxBytesPerSec < 0)
        return setError(E_INVALIDARG, tr("Bandwidth group limit cannot be negative"));
    switch (aType)
    {
        case BandwidthGroupType_Null: /*??*/
        case BandwidthGroupType_Disk:
            break;
        case BandwidthGroupType_Network:
            if (aName.length() > PDM_NET_SHAPER_MAX_NAME_LEN)
                return setError(E_INVALIDARG, tr("Bandwidth name is too long: %zu, max %u"),
                                aName.length(), PDM_NET_SHAPER_MAX_NAME_LEN);
            break;
        default:
            AssertFailedReturn(setError(E_INVALIDARG, tr("Invalid group type: %d"), aType));
    }
    if (aName.isEmpty())
        return setError(E_INVALIDARG, tr("Bandwidth group name must not be empty")); /* ConsoleImpl2.cpp fails then */

    /*
     * The machine needs to be mutable:
     */
    AutoMutableOrSavedStateDependency adep(m->pParent);
    HRESULT hrc = adep.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /*
         * Check that the group doesn't already exist:
         */
        ComObjPtr<BandwidthGroup> group;
        hrc = i_getBandwidthGroupByName(aName, group, false /* aSetError */);
        if (FAILED(hrc))
        {
            /*
             * There is an upper limit of the number of network groups imposed by PDM.
             */
            size_t cNetworkGroups = 0;
            if (aType == BandwidthGroupType_Network)
                for (BandwidthGroupList::const_iterator it = m->llBandwidthGroups->begin();
                     it != m->llBandwidthGroups->end();
                     ++it)
                    if ((*it)->i_getType() == BandwidthGroupType_Network)
                        cNetworkGroups++;
            if (cNetworkGroups < PDM_NET_SHAPER_MAX_GROUPS)
            {
                /*
                 * Create the new group.
                 */
                hrc = group.createObject();
                if (SUCCEEDED(hrc))
                {
                    hrc = group->init(this, aName, aType, aMaxBytesPerSec);
                    if (SUCCEEDED(hrc))
                    {
                        /*
                         * Add it to the settings.
                         */
                        m->pParent->i_setModified(Machine::IsModified_BandwidthControl);
                        m->llBandwidthGroups.backup();
                        m->llBandwidthGroups->push_back(group);
                        hrc = S_OK;
                    }
                }
            }
            else
                hrc = setError(E_FAIL, tr("Too many network bandwidth groups (max %u)"), PDM_NET_SHAPER_MAX_GROUPS);
        }
        else
            hrc = setError(VBOX_E_OBJECT_IN_USE, tr("Bandwidth group named '%s' already exists"), aName.c_str());
    }
    return hrc;
}

HRESULT BandwidthControl::deleteBandwidthGroup(const com::Utf8Str &aName)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedStateDependency adep(m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<BandwidthGroup> group;
    HRESULT hrc = i_getBandwidthGroupByName(aName, group, true /* aSetError */);
    if (FAILED(hrc)) return hrc;

    if (group->i_getReferences() != 0)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("The bandwidth group '%s' is still in use"), aName.c_str());

    /* We can remove it now. */
    m->pParent->i_setModified(Machine::IsModified_BandwidthControl);
    m->llBandwidthGroups.backup();

    group->i_unshare();

    m->llBandwidthGroups->remove(group);

    /* inform the direct session if any */
    alock.release();
    //onStorageControllerChange(); @todo

    return S_OK;
}

HRESULT BandwidthControl::getNumGroups(ULONG *aNumGroups)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aNumGroups = (ULONG)m->llBandwidthGroups->size();

    return S_OK;
}

HRESULT BandwidthControl::getBandwidthGroup(const com::Utf8Str &aName, ComPtr<IBandwidthGroup> &aBandwidthGroup)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<BandwidthGroup> group;
    HRESULT hrc = i_getBandwidthGroupByName(aName, group, true /* aSetError */);
    if (SUCCEEDED(hrc))
        group.queryInterfaceTo(aBandwidthGroup.asOutParam());

    return hrc;
}

HRESULT BandwidthControl::getAllBandwidthGroups(std::vector<ComPtr<IBandwidthGroup> > &aBandwidthGroups)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aBandwidthGroups.resize(0);
    BandwidthGroupList::const_iterator it;
    for (it = m->llBandwidthGroups->begin();
         it != m->llBandwidthGroups->end();
         ++it)
        aBandwidthGroups.push_back(*it);

    return S_OK;
}

HRESULT BandwidthControl::i_loadSettings(const settings::IOSettings &data)
{
    HRESULT hrc = S_OK;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());
    settings::BandwidthGroupList::const_iterator it;
    for (it = data.llBandwidthGroups.begin();
         it != data.llBandwidthGroups.end();
         ++it)
    {
        const settings::BandwidthGroup &gr = *it;
        hrc = createBandwidthGroup(gr.strName, gr.enmType, (LONG64)gr.cMaxBytesPerSec);
        if (FAILED(hrc)) break;
    }

    return hrc;
}

HRESULT BandwidthControl::i_saveSettings(settings::IOSettings &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    data.llBandwidthGroups.clear();
    BandwidthGroupList::const_iterator it;
    for (it = m->llBandwidthGroups->begin();
         it != m->llBandwidthGroups->end();
         ++it)
    {
        AutoWriteLock groupLock(*it COMMA_LOCKVAL_SRC_POS);
        settings::BandwidthGroup group;

        group.strName      = (*it)->i_getName();
        group.enmType      = (*it)->i_getType();
        group.cMaxBytesPerSec = (uint64_t)(*it)->i_getMaxBytesPerSec();

        data.llBandwidthGroups.push_back(group);
    }

    return S_OK;
}

Machine * BandwidthControl::i_getMachine() const
{
    return m->pParent;
}

