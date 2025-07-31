/* $Id: RecordingScreenSettingsImpl.cpp 110348 2025-07-22 15:04:28Z andreas.loeffler@oracle.com $ */
/** @file
 *
 * VirtualBox COM class implementation - Recording settings of one virtual screen.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_RECORDINGSCREENSETTINGS
#include "LoggingNew.h"

#include "RecordingScreenSettingsImpl.h"
#include "RecordingSettingsImpl.h"
#include "MachineImpl.h"

#include <iprt/asm.h> /* For ASMAtomicXXX. */
#include <iprt/path.h>
#include <iprt/cpp/utils.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "Global.h"

////////////////////////////////////////////////////////////////////////////////
//
// RecordScreenSettings private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct RecordingScreenSettings::Data
{
    Data()
        : pParent(NULL)
        , uScreenId(0)
        , cRefs(0)
    { }

    RecordingSettings * const                pParent;
    const ComObjPtr<RecordingScreenSettings> pPeer;
    uint32_t                                 uScreenId;
    /** Internal reference count to track sharing of this screen settings object among
     *  other recording settings objects. */
    int32_t                                  cRefs;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::RecordingScreen>    bd;
};

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(RecordingScreenSettings)

HRESULT RecordingScreenSettings::FinalConstruct()
{
    return BaseFinalConstruct();
}

void RecordingScreenSettings::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the recording screen settings object.
 *
 * @returns COM result indicator
 */
HRESULT RecordingScreenSettings::init(RecordingSettings *aParent, uint32_t uScreenId,
                                      const settings::RecordingScreen& aThat)
{
    LogFlowThisFunc(("aParent: %p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    /* Share the parent & machine weakly. */
    unconst(m->pParent)  = aParent;
    /* mPeer is left null. */

    /* Simply copy the settings data. */
    m->uScreenId = uScreenId;
    m->bd.allocate();
    m->bd->operator=(aThat);

    HRESULT hrc = S_OK;

    int vrc = i_initInternal();
    if (RT_SUCCESS(vrc))
    {
        autoInitSpan.setSucceeded();
    }
    else
    {
        autoInitSpan.setFailed();
        hrc = E_UNEXPECTED;
    }

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 *  Initializes the recording settings object given another recording settings object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *        it shares data with is destroyed.
 */
HRESULT RecordingScreenSettings::init(RecordingSettings *aParent, RecordingScreenSettings *aThat)
{
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;
    unconst(m->pPeer)   = aThat;

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);

    m->uScreenId = aThat->m->uScreenId;
    m->bd.share(aThat->m->bd);

    HRESULT hrc = S_OK;

    int vrc = i_initInternal();
    if (RT_SUCCESS(vrc))
    {
        autoInitSpan.setSucceeded();
    }
    else
    {
        autoInitSpan.setFailed();
        hrc = E_UNEXPECTED;
    }

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT RecordingScreenSettings::initCopy(RecordingSettings *aParent, RecordingScreenSettings *aThat)
{
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;
    /* mPeer is left null. */

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);

    m->uScreenId = aThat->m->uScreenId;
    m->bd.attachCopy(aThat->m->bd);

    HRESULT hrc = S_OK;

    int vrc = i_initInternal();
    if (RT_SUCCESS(vrc))
    {
        autoInitSpan.setSucceeded();
    }
    else
    {
        autoInitSpan.setFailed();
        hrc = E_UNEXPECTED;
    }

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void RecordingScreenSettings::uninit()
{
    LogThisFunc(("%p\n", this));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    /* Make sure nobody holds an internal reference to it anymore. */
    AssertReturnVoid(m->cRefs == 0);

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pParent) = NULL;

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}

HRESULT RecordingScreenSettings::isFeatureEnabled(RecordingFeature_T aFeature, BOOL *aEnabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    RecordingFeatureMap::const_iterator itFeature = m->bd->featureMap.find(aFeature);

    *aEnabled = (   itFeature != m->bd->featureMap.end()
                 && itFeature->second == true);

    return S_OK;
}

HRESULT RecordingScreenSettings::getId(ULONG *id)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *id = m->uScreenId;

    return S_OK;
}

HRESULT RecordingScreenSettings::getEnabled(BOOL *enabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fEnabled ? TRUE : FALSE;

    return S_OK;
}

HRESULT RecordingScreenSettings::setEnabled(BOOL enabled)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFunc(("Screen %RU32\n", m->uScreenId));

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change enabled state of screen while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fEnabled != RT_BOOL(enabled))
    {
        m->bd.backup();
        m->bd->fEnabled = RT_BOOL(enabled);
        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    LogFlowThisFunc(("Screen %RU32\n", m->uScreenId));
    return S_OK;
}

HRESULT RecordingScreenSettings::getFeatures(std::vector<RecordingFeature_T> &aFeatures)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFeatures.clear();

    RecordingFeatureMap::const_iterator itFeature = m->bd->featureMap.begin();
    while (itFeature != m->bd->featureMap.end())
    {
        if (itFeature->second) /* Is feature enabled? */
            aFeatures.push_back(itFeature->first);

        ++itFeature;
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::setFeatures(const std::vector<RecordingFeature_T> &aFeatures)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change features while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();

    RecordingFeatureMap featureMapOld = m->bd->featureMap;
    m->bd->featureMap.clear();

    for (size_t i = 0; i < aFeatures.size(); i++)
    {
        switch (aFeatures[i])
        {
            case RecordingFeature_Audio:
                m->bd->featureMap[RecordingFeature_Audio] = true;
                break;

            case RecordingFeature_Video:
                m->bd->featureMap[RecordingFeature_Video] = true;
                break;

            default:
                break;
        }
    }

    if (m->bd->featureMap != featureMapOld)
    {
        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getDestination(RecordingDestination_T *aDestination)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDestination = m->bd->enmDest;

    return S_OK;
}

HRESULT RecordingScreenSettings::setDestination(RecordingDestination_T aDestination)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change destination type while recording is enabled"));

    if (aDestination != RecordingDestination_File)
        return setError(E_INVALIDARG, tr("Destination type invalid / not supported"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->enmDest != aDestination)
    {
        m->bd.backup();
        m->bd->enmDest = aDestination;

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getFilename(com::Utf8Str &aFilename)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Get default file name if an empty string or a single "." is set. */
    if (   m->bd->File.strName.isEmpty()
        || m->bd->File.strName.equals("."))
    {
        int vrc = m->pParent->i_getDefaultFilename(aFilename, m->uScreenId, true /* fWithFileExtension */);
        if (RT_FAILURE(vrc))
            return setErrorBoth(E_INVALIDARG, vrc, tr("Error retrieving default file name"));

        /* Important: Don't assign the default file name to File.strName, as this woulnd't be considered
         *            as default settings anymore! */
    }
    else /* Return custom file name. */
        aFilename = m->bd->File.strName;

    return S_OK;
}

HRESULT RecordingScreenSettings::setFilename(const com::Utf8Str &aFilename)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change file name while recording is enabled"));

    if (aFilename.isNotEmpty())
    {
        if (!RTPathStartsWithRoot(aFilename.c_str()))
            return setError(E_INVALIDARG, tr("Recording file name '%s' is not absolute"), aFilename.c_str());
    }

    /** @todo Add more sanity? */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Note: When setting an empty file name, this will return the screen's default file name when using ::getFileName(). */
    if (m->bd->File.strName != aFilename)
    {
        Utf8Str strName;
        int vrc = m->pParent->i_getFilename(strName, m->uScreenId, aFilename);
        if (RT_SUCCESS(vrc))
        {
            m->bd.backup();
            m->bd->File.strName = strName;

            alock.release();

            m->pParent->i_onSettingsChanged();
        }
        else
            return setErrorBoth(E_ACCESSDENIED, vrc, tr("Could not set file name for recording screen"));
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getMaxTime(ULONG *aMaxTimeS)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMaxTimeS =  m->bd->ulMaxTimeS;

    return S_OK;
}

HRESULT RecordingScreenSettings::setMaxTime(ULONG aMaxTimeS)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change maximum time while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->ulMaxTimeS != aMaxTimeS)
    {
        m->bd.backup();
        m->bd->ulMaxTimeS = aMaxTimeS;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getMaxFileSize(ULONG *aMaxFileSizeMB)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMaxFileSizeMB = m->bd->File.ulMaxSizeMB;

    return S_OK;
}

HRESULT RecordingScreenSettings::setMaxFileSize(ULONG aMaxFileSize)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change maximum file size while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->File.ulMaxSizeMB != aMaxFileSize)
    {
        m->bd.backup();
        m->bd->File.ulMaxSizeMB = aMaxFileSize;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getOptions(com::Utf8Str &aOptions)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aOptions = m->bd->strOptions;

    return S_OK;
}

HRESULT RecordingScreenSettings::setOptions(const com::Utf8Str &aOptions)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change options while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Note: Parsing and validation is done at codec level. */

    m->bd.backup();
    m->bd->strOptions = aOptions;

    alock.release();

    m->pParent->i_onSettingsChanged();

    return S_OK;
}

HRESULT RecordingScreenSettings::getAudioCodec(RecordingAudioCodec_T *aCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCodec = m->bd->Audio.enmCodec;

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioCodec(RecordingAudioCodec_T aCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio codec while recording is enabled"));

    if (aCodec != RecordingAudioCodec_OggVorbis)
        return setError(E_INVALIDARG, tr("Audio codec not supported"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Audio.enmCodec != aCodec)
    {
        m->bd.backup();
        m->bd->Audio.enmCodec = aCodec;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getAudioDeadline(RecordingCodecDeadline_T *aDeadline)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDeadline = m->bd->Audio.enmDeadline;

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioDeadline(RecordingCodecDeadline_T aDeadline)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio deadline while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Audio.enmDeadline != aDeadline)
    {
        m->bd.backup();
        m->bd->Audio.enmDeadline = aDeadline;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getAudioRateControlMode(RecordingRateControlMode_T *aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMode = RecordingRateControlMode_VBR; /** @todo Implement CBR. */

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioRateControlMode(RecordingRateControlMode_T aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio rate control mode while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo Implement this. */
    RT_NOREF(aMode);

    return E_NOTIMPL;
}

HRESULT RecordingScreenSettings::getAudioHz(ULONG *aHz)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aHz = m->bd->Audio.uHz;

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioHz(ULONG aHz)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio Hertz rate while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Audio.uHz != (uint16_t)aHz)
    {
        m->bd.backup();
        m->bd->Audio.uHz = (uint16_t)aHz;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getAudioBits(ULONG *aBits)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aBits = m->bd->Audio.cBits;

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioBits(ULONG aBits)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio bits while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Audio.cBits != (uint8_t)aBits)
    {
        m->bd.backup();
        m->bd->Audio.cBits = (uint8_t)aBits;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getAudioChannels(ULONG *aChannels)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aChannels = m->bd->Audio.cChannels;

    return S_OK;
}

HRESULT RecordingScreenSettings::setAudioChannels(ULONG aChannels)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change audio channels while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Audio.cChannels != (uint8_t)aChannels)
    {
        m->bd.backup();
        m->bd->Audio.cChannels = (uint8_t)aChannels;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoCodec(RecordingVideoCodec_T *aCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCodec = m->bd->Video.enmCodec;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoCodec(RecordingVideoCodec_T aCodec)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video codec while recording is enabled"));

    if (aCodec != RecordingVideoCodec_VP8)
        return setError(E_INVALIDARG, tr("Video codec not supported"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.enmCodec != aCodec)
    {
        m->bd.backup();
        m->bd->Video.enmCodec = aCodec;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoDeadline(RecordingCodecDeadline_T *aDeadline)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aDeadline = m->bd->Video.enmDeadline;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoDeadline(RecordingCodecDeadline_T aDeadline)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video deadline while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.enmDeadline != aDeadline)
    {
        m->bd.backup();
        m->bd->Video.enmDeadline = aDeadline;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoWidth(ULONG *aVideoWidth)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aVideoWidth = m->bd->Video.ulWidth;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoWidth(ULONG aVideoWidth)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video width while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.ulWidth != aVideoWidth)
    {
        m->bd.backup();
        m->bd->Video.ulWidth = aVideoWidth;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoHeight(ULONG *aVideoHeight)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aVideoHeight = m->bd->Video.ulHeight;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoHeight(ULONG aVideoHeight)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video height while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.ulHeight != aVideoHeight)
    {
        m->bd.backup();
        m->bd->Video.ulHeight = aVideoHeight;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoRate(ULONG *aVideoRate)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aVideoRate = m->bd->Video.ulRate;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoRate(ULONG aVideoRate)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video rate while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.ulRate != aVideoRate)
    {
        m->bd.backup();
        m->bd->Video.ulRate = aVideoRate;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoRateControlMode(RecordingRateControlMode_T *aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMode = RecordingRateControlMode_VBR; /** @todo Implement CBR. */

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoRateControlMode(RecordingRateControlMode_T aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video rate control mode while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo Implement this. */
    RT_NOREF(aMode);

    return E_NOTIMPL;
}

HRESULT RecordingScreenSettings::getVideoFPS(ULONG *aVideoFPS)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aVideoFPS = m->bd->Video.ulFPS;

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoFPS(ULONG aVideoFPS)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video FPS while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->Video.ulFPS != aVideoFPS)
    {
        m->bd.backup();
        m->bd->Video.ulFPS = aVideoFPS;

        alock.release();

        m->pParent->i_onSettingsChanged();
    }

    return S_OK;
}

HRESULT RecordingScreenSettings::getVideoScalingMode(RecordingVideoScalingMode_T *aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMode = RecordingVideoScalingMode_None; /** @todo Implement this. */

    return S_OK;
}

HRESULT RecordingScreenSettings::setVideoScalingMode(RecordingVideoScalingMode_T aMode)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (!m->pParent->i_canChangeSettings())
        return setError(E_INVALIDARG, tr("Cannot change video scaling mode while recording is enabled"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo Implement this. */
    RT_NOREF(aMode);

    return E_NOTIMPL;
}

/**
 * Initializes data, internal version.
 *
 * @returns VBox status code.
 */
int RecordingScreenSettings::i_initInternal(void)
{
    AssertPtrReturn(m, VERR_INVALID_POINTER);

    i_reference();

    switch (m->bd->enmDest)
    {
        case RecordingDestination_File:
        {
            /* Note: Leave the file name empty here, which means using the default setting.
             *       Important when comparing with the default settings! */
            break;
        }

        default:
            break;
    }

    return VINF_SUCCESS;
}


// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 * Loads settings from the given machine node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   Settings            Screen settings to load.
 */
HRESULT RecordingScreenSettings::i_loadSettings(const settings::RecordingScreen &Settings)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    m->bd.assignCopy(&Settings);
    return S_OK;
}

/**
 *  Saves settings to the given machine node.
 *
 *  @returns HRESULT
 *  @param   Settings           Screen settings to save to.
 */
HRESULT RecordingScreenSettings::i_saveSettings(settings::RecordingScreen &Settings)
{
    LogThisFunc(("%p: Screen %RU32\n", this, m ? m->uScreenId : UINT32_MAX));

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Settings = *m->bd.data();

    return S_OK;
}

void RecordingScreenSettings::i_rollback(void)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->bd.rollback();
}

void RecordingScreenSettings::i_commit(void)
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

void RecordingScreenSettings::i_copyFrom(RecordingScreenSettings *aThat)
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
 * Applies default screen recording settings.
 *
 * @note Locks this object for writing.
 */
void RecordingScreenSettings::i_applyDefaults(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd->applyDefaults();
}

settings::RecordingScreen &RecordingScreenSettings::i_getData(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRC(autoCaller.hrc());

    AssertPtr(m);
    return *m->bd.data();
}

/**
 * Increments the reference count.
 *
 * @returns New reference count.
 *
 * @note    Internal reference count, to track object sharing across different recording settings objects
 *          which share the same screen recording data.
 */
int32_t RecordingScreenSettings::i_reference(void)
{
    int cNewRefs = ASMAtomicIncS32(&m->cRefs); RT_NOREF(cNewRefs);
    LogThisFunc(("%p: cRefs -> %RI32\n", this, cNewRefs));
    return cNewRefs;
}

/**
 * Decrements the reference count.
 *
 * @returns New reference count.
 *
 * @note    Internal reference count, to track object sharing across different recording settings objects
 *          which share the same screen recording data.
 */
int32_t RecordingScreenSettings::i_release(void)
{
    int32_t cNewRefs = ASMAtomicDecS32(&m->cRefs); RT_NOREF(cNewRefs);
    LogThisFunc(("%p: cRefs -> %RI32\n", this, cNewRefs));
    AssertReturn(cNewRefs >= 0, 0);
    return cNewRefs;
}

/**
 * Returns the current reference count.
 *
 * @returns Current reference count.
 *
 * @note    Internal reference count, to track object sharing across different recording settings objects
 *          which share the same screen recording data.
 */
int32_t RecordingScreenSettings::i_getReferences(void)
{
    return ASMAtomicReadS32(&m->cRefs);
}
