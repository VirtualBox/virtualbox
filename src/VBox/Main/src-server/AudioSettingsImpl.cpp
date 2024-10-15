/* $Id: AudioSettingsImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Audio settings for a VM.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_AUDIOSETTINGS
#include "AudioSettingsImpl.h"
#include "MachineImpl.h"

#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"


////////////////////////////////////////////////////////////////////////////////
//
// AudioSettings private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct AudioSettings::Data
{
    Data()
        : pMachine(NULL)
    { }

    Machine * const                pMachine;
    const ComObjPtr<AudioAdapter>  pAdapter;
    const ComObjPtr<AudioSettings> pPeer;
};

DEFINE_EMPTY_CTOR_DTOR(AudioSettings)

HRESULT AudioSettings::FinalConstruct()
{
    return BaseFinalConstruct();
}

void AudioSettings::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the audio settings object.
 *
 * @returns HRESULT
 * @param   aParent             Pointer of the parent object.
 */
HRESULT AudioSettings::init(Machine *aParent)
{
    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    /* share the parent weakly */
    unconst(m->pMachine) = aParent;

    /* create the audio adapter object (always present, default is disabled) */
    unconst(m->pAdapter).createObject();
    m->pAdapter->init(this);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the audio settings object given another audio settings object
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
HRESULT AudioSettings::init(Machine *aParent, AudioSettings *aThat)
{
    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    unconst(m->pPeer)    = aThat;

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = unconst(m->pAdapter).createObject();
    ComAssertComRCRet(hrc, hrc);
    hrc = m->pAdapter->init(this, aThat->m->pAdapter);
    ComAssertComRCRet(hrc, hrc);

    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the guest object given another guest object
 * (a kind of copy constructor). This object makes a private copy of data
 * of the original object passed as an argument.
 *
 * @note Locks @a aThat object for reading.
 *
 * @returns HRESULT
 * @param   aParent             Pointer of the parent object.
 * @param   aThat               Pointer to audio adapter to use settings from.
 */
HRESULT AudioSettings::initCopy(Machine *aParent, AudioSettings *aThat)
{
    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    // pPeer is left null

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = unconst(m->pAdapter).createObject();
    ComAssertComRCRet(hrc, hrc);
    hrc = m->pAdapter->init(this);
    ComAssertComRCRet(hrc, hrc);
    m->pAdapter->i_copyFrom(aThat->m->pAdapter);

    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void AudioSettings::uninit(void)
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(m->pPeer)    = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;
}


// IAudioSettings properties
////////////////////////////////////////////////////////////////////////////////

HRESULT AudioSettings::getAdapter(ComPtr<IAudioAdapter> &aAdapter)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aAdapter = m->pAdapter;

    return S_OK;
}


// IAudioSettings methods
////////////////////////////////////////////////////////////////////////////////

HRESULT AudioSettings::getHostAudioDevice(AudioDirection_T aUsage, ComPtr<IHostAudioDevice> &aDevice)
{
    RT_NOREF(aUsage, aDevice);
    ReturnComNotImplemented();
}

HRESULT AudioSettings::setHostAudioDevice(const ComPtr<IHostAudioDevice> &aDevice, AudioDirection_T aUsage)
{
    RT_NOREF(aDevice, aUsage);
    ReturnComNotImplemented();
}


// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 * Determines whether the audio settings currently can be changed or not.
 *
 * @returns \c true if the settings can be changed, \c false if not.
 */
bool AudioSettings::i_canChangeSettings(void)
{
    AutoAnyStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc()))
        return false;

    /** @todo Do some more checks here? */
    return true;
}

/**
 * Returns a (weak) pointer of the audio setting's machine object.
 *
 * @returns Wweak pointer of the audio setting's machine object.
 */
Machine *AudioSettings::i_getMachine(void)
{
    return m->pMachine; // m->pMachine is const, needs no locking
}

/**
 * Gets called when the machine object needs to know that audio adapter settings
 * have been changed.
 *
 * @param   pAdapter             Pointer to audio adapter which has changed.
 */
void AudioSettings::i_onAdapterChanged(IAudioAdapter *pAdapter)
{
    AssertPtrReturnVoid(pAdapter);
    m->pMachine->i_onAudioAdapterChange(pAdapter); // m->pMachine is const, needs no locking
}

/**
 * Gets called when the machine object needs to know that a host audio device
 * has been changed.
 *
 * @param   pDevice             Host audio device which has changed.
 * @param   fIsNew              Set to \c true if this is a new device (i.e. has not been present before), \c false if not.
 * @param   enmState            The current state of the device.
 * @param   pErrInfo            Additional error information in case of error(s).
 */
void AudioSettings::i_onHostDeviceChanged(IHostAudioDevice *pDevice,
                                          bool fIsNew, AudioDeviceState_T enmState, IVirtualBoxErrorInfo *pErrInfo)
{
    AssertPtrReturnVoid(pDevice);
    m->pMachine->i_onHostAudioDeviceChange(pDevice, fIsNew, enmState, pErrInfo); // m->pMachine is const, needs no locking
}

/**
 * Gets called when the machine object needs to know that the audio settings
 * have been changed.
 */
void AudioSettings::i_onSettingsChanged(void)
{
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
    m->pMachine->i_setModified(Machine::IsModified_AudioSettings);
    mlock.release();
}

/**
 * Loads settings from the given machine node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   data                Audio adapter configuration settings to load from.
 *
 * @note Locks this object for writing.
 */
HRESULT AudioSettings::i_loadSettings(const settings::AudioAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->pAdapter->i_loadSettings(data);

    /* Note: The host audio device selection is run-time only, e.g. won't be serialized in the settings! */
    return S_OK;
}

/**
 * Saves audio settings to the given machine node.
 *
 * @returns HRESULT
 * @param   data                Audio configuration settings to save to.
 *
 * @note Locks this object for reading.
 */
HRESULT AudioSettings::i_saveSettings(settings::AudioAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->pAdapter->i_saveSettings(data);

    /* Note: The host audio device selection is run-time only, e.g. won't be serialized in the settings! */
    return S_OK;
}

/**
 * Copies settings from a given audio settings object.
 *
 * This object makes a private copy of data of the original object passed as
 * an argument.
 *
 * @note Locks this object for writing, together with the peer object
 *       represented by @a aThat (locked for reading).
 *
 * @param aThat                 Audio settings to load from.
 */
void AudioSettings::i_copyFrom(AudioSettings *aThat)
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

    m->pAdapter->i_copyFrom(aThat->m->pAdapter);
}

/**
 * Applies default audio settings, based on the given guest OS type.
 *
 * @returns HRESULT
 * @param   aGuestOsType        Guest OS type to use for basing the default settings on.
 */
HRESULT AudioSettings::i_applyDefaults(ComObjPtr<GuestOSType> &aGuestOsType)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AudioControllerType_T audioController;
    HRESULT hrc = aGuestOsType->COMGETTER(RecommendedAudioController)(&audioController);
    if (FAILED(hrc)) return hrc;

    hrc = m->pAdapter->COMSETTER(AudioController)(audioController);
    if (FAILED(hrc)) return hrc;

    AudioCodecType_T audioCodec;
    hrc = aGuestOsType->COMGETTER(RecommendedAudioCodec)(&audioCodec);
    if (FAILED(hrc)) return hrc;

    hrc = m->pAdapter->COMSETTER(AudioCodec)(audioCodec);
    if (FAILED(hrc)) return hrc;

    hrc = m->pAdapter->COMSETTER(Enabled)(true);
    if (FAILED(hrc)) return hrc;

    hrc = m->pAdapter->COMSETTER(EnabledOut)(true);
    if (FAILED(hrc)) return hrc;

    /* Note: We do NOT enable audio input by default due to security reasons!
     *       This always has to be done by the user manually. */

    /* Note: Does not touch the host audio device selection, as this is a run-time only setting. */
    return S_OK;
}

/**
 * Rolls back the current configuration to a former state.
 *
 * @note Locks this object for writing.
 */
void AudioSettings::i_rollback(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->pAdapter->i_rollback();

    /* Note: Does not touch the host audio device selection, as this is a run-time only setting. */
}

/**
 * Commits the current settings and propagates those to a peer (if assigned).
 *
 * @note Locks this object for writing, together with the peer object (also
 *       for writing) if there is one.
 */
void AudioSettings::i_commit(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    m->pAdapter->i_commit();

    /* Note: Does not touch the host audio device selection, as this is a run-time only setting. */
}

