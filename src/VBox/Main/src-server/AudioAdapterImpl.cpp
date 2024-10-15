/* $Id: AudioAdapterImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_AUDIOADAPTER
#include "AudioAdapterImpl.h"
#include "MachineImpl.h"
#include "SystemPropertiesImpl.h"
#include "VirtualBoxImpl.h"

#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"


////////////////////////////////////////////////////////////////////////////////
//
// AudioAdapter private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct AudioAdapter::Data
{
    Data()
        : pParent(NULL)
    { }

    AudioSettings * const          pParent;
    const ComObjPtr<AudioAdapter>  pPeer;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::AudioAdapter> bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

AudioAdapter::AudioAdapter()
{
}

AudioAdapter::~AudioAdapter()
{
}

HRESULT AudioAdapter::FinalConstruct()
{
    return BaseFinalConstruct();
}

void AudioAdapter::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the audio adapter object.
 *
 * @returns HRESULT
 * @param   aParent             Pointer of the parent object.
 */
HRESULT AudioAdapter::init(AudioSettings *aParent)
{
    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;
    /* mPeer is left null */

    /* We now always default to the "Default" audio driver, to make it easier
     * to move VMs around different host OSes.
     *
     * This can be changed by the user explicitly, if needed / wanted. */
    m->bd.allocate();
    m->bd->driverType  = AudioDriverType_Default;
    m->bd->fEnabledIn  = false;
    m->bd->fEnabledOut = false;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the audio adapter object given another audio adapter object
 * (a kind of copy constructor). This object shares data with
 * the object passed as an argument.
 *
 * @note This object must be destroyed before the original object
 *       it shares data with is destroyed.
 *
 * @note Locks @a aThat object for reading.
 *
 * @returns HRESULT
 * @param   aParent             Pointer of the parent object.
 * @param   aThat               Pointer to audio adapter to use settings from.
 */
HRESULT AudioAdapter::init(AudioSettings *aParent, AudioAdapter *aThat)
{
    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;
    unconst(m->pPeer)   = aThat;

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the audio adapter object given another audio adapter object
 * (a kind of copy constructor). This object makes a private copy of data
 * of the original object passed as an argument.
 *
 * @note Locks @a aThat object for reading.
 *
 * @returns HRESULT
 * @param   aParent             Pointer of the parent object.
 * @param   aThat               Pointer to audio adapter to use settings from.
 */
HRESULT AudioAdapter::initCopy(AudioSettings *aParent, AudioAdapter *aThat)
{
    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;
    /* mPeer is left null */

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
void AudioAdapter::uninit(void)
{
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

// IAudioAdapter properties
/////////////////////////////////////////////////////////////////////////////

HRESULT AudioAdapter::getEnabled(BOOL *aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = m->bd->fEnabled;

    return S_OK;
}

HRESULT AudioAdapter::setEnabled(BOOL aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fEnabled != RT_BOOL(aEnabled))
    {
        m->bd.backup();
        m->bd->fEnabled = RT_BOOL(aEnabled);
        alock.release();

        m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
        m->pParent->i_onAdapterChanged(this);
    }

    return S_OK;
}

HRESULT AudioAdapter::getEnabledIn(BOOL *aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = RT_BOOL(m->bd->fEnabledIn);

    return S_OK;
}

HRESULT AudioAdapter::setEnabledIn(BOOL aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (RT_BOOL(aEnabled) != m->bd->fEnabledIn)
    {
        m->bd.backup();
        m->bd->fEnabledIn = RT_BOOL(aEnabled);

        alock.release();

        m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
        m->pParent->i_onAdapterChanged(this);
    }

    return S_OK;
}

HRESULT AudioAdapter::getEnabledOut(BOOL *aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = RT_BOOL(m->bd->fEnabledOut);

    return S_OK;
}

HRESULT AudioAdapter::setEnabledOut(BOOL aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (RT_BOOL(aEnabled) != m->bd->fEnabledOut)
    {
        m->bd.backup();
        m->bd->fEnabledOut = RT_BOOL(aEnabled);

        alock.release();

        m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
        m->pParent->i_onAdapterChanged(this);
    }

    return S_OK;
}

HRESULT AudioAdapter::getAudioDriver(AudioDriverType_T *aAudioDriver)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAudioDriver = m->bd->driverType;

    return S_OK;
}

HRESULT AudioAdapter::setAudioDriver(AudioDriverType_T aAudioDriver)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableOrSavedStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    if (m->bd->driverType != aAudioDriver)
    {
        if (settings::MachineConfigFile::isAudioDriverAllowedOnThisHost(aAudioDriver))
        {
            m->bd.backup();
            m->bd->driverType = aAudioDriver;

            alock.release();

            m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
        }
        else
        {
            AssertMsgFailed(("Wrong audio driver type %d\n", aAudioDriver));
            hrc = E_FAIL;
        }
    }

    return hrc;
}

HRESULT AudioAdapter::getAudioController(AudioControllerType_T *aAudioController)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAudioController = m->bd->controllerType;

    return S_OK;
}

HRESULT AudioAdapter::setAudioController(AudioControllerType_T aAudioController)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    if (m->bd->controllerType != aAudioController)
    {
        AudioCodecType_T defaultCodec;

        /*
         * which audio hardware type are we supposed to use?
         */
        switch (aAudioController)
        {
            /* codec type needs to match the controller. */
            case AudioControllerType_AC97:
                defaultCodec = AudioCodecType_STAC9700;
                break;
            case AudioControllerType_SB16:
                defaultCodec = AudioCodecType_SB16;
                break;
            case AudioControllerType_HDA:
                defaultCodec = AudioCodecType_STAC9221;
                break;
            case AudioControllerType_VirtioSound:
                defaultCodec = AudioCodecType_Null;
                break;

            default:
                AssertMsgFailed(("Wrong audio controller type %d\n", aAudioController));
                defaultCodec = AudioCodecType_Null; /* Shut up MSC */
                hrc = E_FAIL;
        }

        if (SUCCEEDED(hrc))
        {
            m->bd.backup();
            m->bd->controllerType = aAudioController;
            m->bd->codecType      = defaultCodec;

            alock.release();

            m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
        }
    }

    return hrc;
}

HRESULT AudioAdapter::getAudioCodec(AudioCodecType_T *aAudioCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAudioCodec = m->bd->codecType;

    return S_OK;
}

HRESULT AudioAdapter::setAudioCodec(AudioCodecType_T aAudioCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /*
     * ensure that the codec type matches the audio controller
     */
    switch (m->bd->controllerType)
    {
        case AudioControllerType_AC97:
        {
            if (   (aAudioCodec != AudioCodecType_STAC9700)
                && (aAudioCodec != AudioCodecType_AD1980))
                hrc = E_INVALIDARG;
            break;
        }

        case AudioControllerType_SB16:
        {
            if (aAudioCodec != AudioCodecType_SB16)
                hrc = E_INVALIDARG;
            break;
        }

        case AudioControllerType_HDA:
        {
            if (aAudioCodec != AudioCodecType_STAC9221)
                hrc = E_INVALIDARG;
            break;
        }

        case AudioControllerType_VirtioSound:
        {
            hrc = S_OK; /* Don't return an error here, even if this is not implemented yet. Will confuse callers. */
            break;
        }

        default:
            AssertMsgFailed(("Wrong audio controller type %d\n",
                             m->bd->controllerType));
            hrc = E_FAIL;
    }

    if (!SUCCEEDED(hrc))
        return setError(hrc,
                        tr("Invalid audio codec type %d"),
                        aAudioCodec);

    if (m->bd->codecType != aAudioCodec)
    {
        m->bd.backup();
        m->bd->codecType = aAudioCodec;

        alock.release();

        m->pParent->i_onSettingsChanged(); // m->pParent is const, needs no locking
    }

    return hrc;
}

HRESULT AudioAdapter::getPropertiesList(std::vector<com::Utf8Str>& aProperties)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    using namespace settings;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aProperties.resize(0);
    StringsMap::const_iterator cit = m->bd->properties.begin();
    while(cit != m->bd->properties.end())
    {
        Utf8Str key = cit->first;
        aProperties.push_back(cit->first);
        ++cit;
    }

    return S_OK;
}

HRESULT AudioAdapter::getProperty(const com::Utf8Str &aKey, com::Utf8Str &aValue)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::StringsMap::const_iterator cit = m->bd->properties.find(aKey);
    if (cit != m->bd->properties.end())
        aValue = cit->second;

    return S_OK;
}

HRESULT AudioAdapter::setProperty(const com::Utf8Str &aKey, const com::Utf8Str &aValue)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Generic properties processing.
     * Look up the old value first; if nothing's changed then do nothing.
     */
    Utf8Str strOldValue;

    settings::StringsMap::const_iterator cit = m->bd->properties.find(aKey);
    if (cit != m->bd->properties.end())
        strOldValue = cit->second;

    if (strOldValue != aValue)
    {
        if (aValue.isEmpty())
            m->bd->properties.erase(aKey);
        else
            m->bd->properties[aKey] = aValue;
    }

    alock.release();

    return S_OK;
}

// IAudioAdapter methods
/////////////////////////////////////////////////////////////////////////////

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Loads settings from the given machine node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   data                Audio adapter configuration settings to load from.
 *
 * @note Locks this object for writing.
 */
HRESULT AudioAdapter::i_loadSettings(const settings::AudioAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Note: we assume that the default values for attributes of optional
     * nodes are assigned in the Data::Data() constructor and don't do it
     * here. It implies that this method may only be called after constructing
     * a new AudioAdapter object while all its data fields are in the default
     * values. Exceptions are fields whose creation time defaults don't match
     * values that should be applied when these fields are not explicitly set
     * in the settings file (for backwards compatibility reasons). This takes
     * place when a setting of a newly created object must default to A while
     * the same setting of an object loaded from the old settings file must
     * default to B. */
    m->bd.assignCopy(&data);

    return S_OK;
}

/**
 * Saves settings to the given machine node.
 *
 * @returns HRESULT
 * @param   data                Audio adapter configuration settings to save to.
 *
 * @note Locks this object for reading.
 */
HRESULT AudioAdapter::i_saveSettings(settings::AudioAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *m->bd.data();

    return S_OK;
}

/**
 * Rolls back the current configuration to a former state.
 *
 * @note Locks this object for writing.
 */
void AudioAdapter::i_rollback()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();
}

/**
 * Commits the current settings and propagates those to a peer (if assigned).
 *
 * @note Locks this object for writing, together with the peer object (also
 *       for writing) if there is one.
 */
void AudioAdapter::i_commit()
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
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

/**
 * Copies settings from a given audio adapter object.
 *
 * This object makes a private copy of data of the original object passed as
 * an argument.
 *
 * @note Locks this object for writing, together with the peer object
 *       represented by @a aThat (locked for reading).
 *
 * @param aThat                 Audio adapter to load settings from.
 */
void AudioAdapter::i_copyFrom(AudioAdapter *aThat)
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
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
