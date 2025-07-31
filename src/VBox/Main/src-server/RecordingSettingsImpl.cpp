/* $Id: RecordingSettingsImpl.cpp 110489 2025-07-31 08:32:38Z andreas.loeffler@oracle.com $ */
/** @file
 *
 * VirtualBox COM class implementation - Machine capture settings.
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

#define LOG_GROUP LOG_GROUP_MAIN_RECORDINGSETTINGS
#include "LoggingNew.h"

#include "RecordingSettingsImpl.h"
#include "RecordingScreenSettingsImpl.h"
#include "MachineImpl.h"

#include <iprt/cpp/utils.h>
#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "Global.h"

////////////////////////////////////////////////////////////////////////////////
//
// RecordSettings private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct RecordingSettings::Data
{
    Data()
        : pMachine(NULL)
    { }

    Machine * const                         pMachine;
    const ComObjPtr<RecordingSettings>      pPeer;
    RecordingScreenSettingsObjMap           mapScreenObj;
    /** The recording progress object.
     *  There only is one recording progress per VM, shared between multiple recording settings (if any). */
    ComPtr<IProgress>                       mProgress;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::RecordingCommon> bd;
};

DEFINE_EMPTY_CTOR_DTOR(RecordingSettings)

HRESULT RecordingSettings::FinalConstruct()
{
    return BaseFinalConstruct();
}

void RecordingSettings::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Initializes the recording settings object.
 *
 * @returns COM result indicator
 */
HRESULT RecordingSettings::init(Machine *aParent)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    /* share the parent weakly */
    unconst(m->pMachine) = aParent;

    m->bd.allocate();

    i_applyDefaults();

    /* Note: The progress object gets created in i_start(). */

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the capture settings object given another capture settings object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *        it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT RecordingSettings::init(Machine *aParent, RecordingSettings *aThat)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

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

    m->bd.share(aThat->m->bd);

    /* Make sure to add a reference when sharing the screen objects with aThat. */
    for (RecordingScreenSettingsObjMap::const_iterator itScreenThat  = aThat->m->mapScreenObj.begin();
                                                       itScreenThat != aThat->m->mapScreenObj.end();
                                                     ++itScreenThat)
        itScreenThat->second->i_reference();

    m->mapScreenObj = aThat->m->mapScreenObj;
    m->mProgress = aThat->m->mProgress;

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT RecordingSettings::initCopy(Machine *aParent, RecordingSettings *aThat)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    // mPeer is left null

    AutoReadLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.attachCopy(aThat->m->bd);

    HRESULT hrc = S_OK;

    for (RecordingScreenSettingsObjMap::const_iterator itScreenThat  = aThat->m->mapScreenObj.begin();
                                                       itScreenThat != aThat->m->mapScreenObj.end();
                                                     ++itScreenThat)
    {
        ComObjPtr<RecordingScreenSettings> pSettings;
        pSettings.createObject();
        hrc = pSettings->initCopy(this, itScreenThat->second);
        if (FAILED(hrc)) return hrc;

        try
        {
            m->mapScreenObj[itScreenThat->first] = pSettings;
        }
        catch (...)
        {
            hrc = E_OUTOFMEMORY;
        }
    }

    m->mProgress = aThat->m->mProgress;

    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void RecordingSettings::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    i_reset();

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}

// IRecordSettings properties
/////////////////////////////////////////////////////////////////////////////

HRESULT RecordingSettings::getEnabled(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fEnabled;

    return S_OK;
}

HRESULT RecordingSettings::setEnabled(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const bool fEnabled = RT_BOOL(enable);

    HRESULT hrc = S_OK;

    if (m->bd->fEnabled != fEnabled)
    {
        m->bd.backup();
        m->bd->fEnabled = fEnabled;

        alock.release();
    }

    return hrc;
}

HRESULT RecordingSettings::getPaused(BOOL *paused)
{
    RT_NOREF(paused);
    ReturnComNotImplemented();
}

HRESULT RecordingSettings::setPaused(BOOL paused)
{
    RT_NOREF(paused);
    ReturnComNotImplemented();
}

HRESULT RecordingSettings::getScreens(std::vector<ComPtr<IRecordingScreenSettings> > &aRecordScreenSettings)
{
    LogFlowThisFuncEnter();

    AssertPtr(m->pMachine);
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    m->pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    ULONG cMonitors = 0;
    if (!pGraphicsAdapter.isNull())
        pGraphicsAdapter->COMGETTER(MonitorCount)(&cMonitors);

    i_syncToMachineDisplays(cMonitors);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    try
    {
        aRecordScreenSettings.clear();
        aRecordScreenSettings.resize(m->mapScreenObj.size());
    }
    catch (...)
    {
        hrc = E_OUTOFMEMORY;
    }

    if (FAILED(hrc))
        return hrc;

    RecordingScreenSettingsObjMap::const_iterator itScreenObj = m->mapScreenObj.begin();
    size_t i = 0;
    while (itScreenObj != m->mapScreenObj.end())
    {
        itScreenObj->second.queryInterfaceTo(aRecordScreenSettings[i].asOutParam());
        AssertBreakStmt(aRecordScreenSettings[i].isNotNull(), hrc = E_POINTER);
        ++i;
        ++itScreenObj;
    }

    Assert(aRecordScreenSettings.size() == m->mapScreenObj.size());

    return hrc;
}

HRESULT RecordingSettings::getProgress(ComPtr<IProgress> &aProgress)
{
#ifndef VBOX_WITH_RECORDING
    RT_NOREF(aProgress);
    ReturnComNotImplemented();
#else
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->mProgress.isNull())
        return setError(E_FAIL, tr("Recording not started"));

    return m->mProgress.queryInterfaceTo(aProgress.asOutParam());
#endif
}

HRESULT RecordingSettings::getScreenSettings(ULONG uScreenId, ComPtr<IRecordingScreenSettings> &aRecordScreenSettings)
{
    LogFlowThisFuncEnter();

    AssertPtr(m->pMachine);
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    m->pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    ULONG cMonitors = 0;
    if (!pGraphicsAdapter.isNull())
        pGraphicsAdapter->COMGETTER(MonitorCount)(&cMonitors);

    i_syncToMachineDisplays(cMonitors);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (uScreenId + 1 > m->mapScreenObj.size())
        return setError(E_INVALIDARG, tr("Invalid screen ID specified"));

    RecordingScreenSettingsObjMap::const_iterator itScreen = m->mapScreenObj.find(uScreenId);
    if (itScreen != m->mapScreenObj.end())
    {
        itScreen->second.queryInterfaceTo(aRecordScreenSettings.asOutParam());
        return S_OK;
    }

    return VBOX_E_OBJECT_NOT_FOUND;
}

HRESULT RecordingSettings::resume(void)
{
    ReturnComNotImplemented();
}

HRESULT RecordingSettings::start(ComPtr<IProgress> &aProgress)
{
#ifndef VBOX_WITH_RECORDING
    RT_NOREF(aProgress);
    ReturnComNotImplemented();
#else
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    /* Recording not explicitly enabled before? Do so now. */
    if (!m->bd->fEnabled)
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        m->bd.backup();
        m->bd->fEnabled = true;

        alock.release();

        /* Note: m->bd->fEnabled is transient here, i.e. we don't save the settings,
                 as this would otherwise start the recording on VM startup the next time. */
    }

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->mProgress.isNotNull())
    {
        BOOL fCompleted = FALSE;
        HRESULT const hrc = m->mProgress->COMGETTER(Completed)(&fCompleted);
        ComAssertComRCRetRC(hrc);
        if (!fCompleted)
            return setError(E_FAIL, tr("Recording already started"));
    }

    m->mProgress.setNull(); /* Make sure to release a dangling object from a former run. */

    alock.release();

    int const vrc = i_start();
    if (RT_FAILURE(vrc))
    {
        /* Make the progress' error info available to the caller on failure. */
        if (m->mProgress.isNotNull()) /* Progress object available (yet)? */
        {
            ComObjPtr<IVirtualBoxErrorInfo> pErrorInfo;
            m->mProgress->COMGETTER(ErrorInfo)(pErrorInfo.asOutParam());
            return setError(pErrorInfo);
        }

        return setErrorBoth(VBOX_E_RECORDING_ERROR, vrc, "Starting recording failed with %Rrc", vrc);
    }

    return m->mProgress.queryInterfaceTo(aProgress.asOutParam());
#endif
}

// IRecordSettings methods
/////////////////////////////////////////////////////////////////////////////

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Adds a screen settings object to a particular map.
 *
 * @returns IPRT status code. VERR_ALREADY_EXISTS if the object in question already exists.
 * @param   screenSettingsMap   Map to add screen settings to.
 * @param   idScreen            Screen ID to add settings for.
 * @param   data                Recording screen settings to use for that screen.
 */
int RecordingSettings::i_createScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap,
                                         uint32_t idScreen, const settings::RecordingScreen &data)
{
    AssertReturn(screenSettingsMap.find(idScreen) == screenSettingsMap.end(), VERR_ALREADY_EXISTS);

    int vrc = VINF_SUCCESS;

    ComObjPtr<RecordingScreenSettings> recordingScreenSettings;
    HRESULT hrc = recordingScreenSettings.createObject();
    if (SUCCEEDED(hrc))
    {
        hrc = recordingScreenSettings->init(this, idScreen, data);
        if (SUCCEEDED(hrc))
        {
            try
            {
                screenSettingsMap[idScreen] = recordingScreenSettings;
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }
        }
    }

    LogThisFunc(("%p: Screen %RU32 -> %Rrc\n", recordingScreenSettings.m_p, idScreen, vrc));
    return vrc;
}

/**
 * Removes a screen settings object from a particular map.
 *
 * If the internal reference count hits 0, the screen settings object will be destroyed.
 * This means that this screen settings object is not being used anymore by other recording settings (as shared data).
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if specified screen was not found.
 * @param   screenSettingsMap   Map to remove screen settings from.
 * @param   idScreen            ID of screen to remove.
 */
int RecordingSettings::i_destroyScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap, uint32_t idScreen)
{
    AssertReturn(screenSettingsMap.find(idScreen) != screenSettingsMap.end(), VERR_NOT_FOUND);

    RecordingScreenSettingsObjMap::iterator itScreen = screenSettingsMap.find(idScreen);

    /* Make sure to consume the pointer before the one of the
     * iterator gets released. */
    ComObjPtr<RecordingScreenSettings> pScreenSettings = itScreen->second;

    screenSettingsMap.erase(itScreen);

    LogThisFunc(("%p: Screen %RU32, cRefs=%RI32\n", pScreenSettings.m_p, idScreen, pScreenSettings->i_getReferences()));

    pScreenSettings->i_release();

    /* Only destroy the object if nobody else keeps a reference to it anymore. */
    if (pScreenSettings->i_getReferences() == 0)
    {
        LogThisFunc(("%p: Screen %RU32 -> Null\n", pScreenSettings.m_p, idScreen));
        pScreenSettings.setNull();
    }

    return VINF_SUCCESS;
}

/**
 * Destroys all screen settings objects of a particular map.
 *
 * @returns IPRT status code.
 * @param   screenSettingsMap   Map to destroy screen settings objects for.
 */
int RecordingSettings::i_destroyAllScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap)
{
    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    RecordingScreenSettingsObjMap::iterator itScreen = screenSettingsMap.begin();
    while (itScreen != screenSettingsMap.end())
    {
        vrc = i_destroyScreenObj(screenSettingsMap, itScreen->first);
        if (RT_FAILURE(vrc))
            break;

        itScreen = screenSettingsMap.begin();
    }

    Assert(screenSettingsMap.size() == 0);
    return vrc;
}

/**
 * Loads settings from the given settings.
 * May be called once right after this object creation.
 *
 * @param Settings                  Recording settings to load from.
 *
 * @note Locks this object for writing.
 */
HRESULT RecordingSettings::i_loadSettings(const settings::Recording &Settings)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    LogFlowThisFunc(("Data has %zu screens\n", Settings.mapScreens.size()));

    settings::RecordingScreenSettingsMap::const_iterator itScreenData = Settings.mapScreens.begin();
    while (itScreenData != Settings.mapScreens.end())
    {
        RecordingScreenSettingsObjMap::iterator itScreen = m->mapScreenObj.find(itScreenData->first);
        if (itScreen != m->mapScreenObj.end())
        {
            hrc = itScreen->second->i_loadSettings(itScreenData->second);
            if (FAILED(hrc))
                break;
        }
        else
        {
            int vrc = i_createScreenObj(m->mapScreenObj,
                                        itScreenData->first /* uScreenId */, itScreenData->second /* Settings */);
            if (RT_FAILURE(vrc))
            {
                hrc = E_OUTOFMEMORY; /* Most likely. */
                break;
            }
        }

        ++itScreenData;
    }

    if (SUCCEEDED(hrc))
    {
        ComAssertComRCRet(hrc, hrc);
        AssertReturn(m->mapScreenObj.size() == Settings.mapScreens.size(), E_UNEXPECTED);

        // simply copy
        m->bd.assignCopy(&Settings.common);
    }

    LogFlowThisFunc(("Returning %Rhrc\n", hrc));
    return hrc;
}

/**
 * Resets the internal object state by destroying all screen settings objects.
 */
void RecordingSettings::i_reset(void)
{
    LogFlowThisFuncEnter();

    i_stop();

    /* Make sure to destroy screen objects attached to this object.
     * Note: This also decrements the refcount of a screens object, in case it's shared among other recording settings. */
    i_destroyAllScreenObj(m->mapScreenObj);
}

/**
 * Saves settings to the given settings.
 *
 * @param Settings                  Where to store the recording settings to.
 *
 * @note Locks this object for reading.
 */
HRESULT RecordingSettings::i_saveSettings(settings::Recording &Settings)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AssertPtr(m->pMachine);
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    m->pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    ULONG cMonitors = 0;
    if (!pGraphicsAdapter.isNull())
        pGraphicsAdapter->COMGETTER(MonitorCount)(&cMonitors);

    int vrc2 = i_syncToMachineDisplays(cMonitors);
    AssertRC(vrc2);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Settings.common = *m->bd.data();

    HRESULT hrc = S_OK;

    for (RecordingScreenSettingsObjMap::const_iterator itScreen  = m->mapScreenObj.begin();
                                                       itScreen != m->mapScreenObj.end();
                                                     ++itScreen)
    {
        hrc = itScreen->second->i_saveSettings(Settings.mapScreens[itScreen->first /* Screen ID */]);
        if (FAILED(hrc))
            break;
    }

    LogFlowThisFuncLeave();
    return hrc;
}

void RecordingSettings::i_rollback(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();

    for (RecordingScreenSettingsObjMap::const_iterator itScreen  = m->mapScreenObj.begin();
                                                       itScreen != m->mapScreenObj.end();
                                                     ++itScreen)
    {
        itScreen->second->i_rollback();
    }
}

void RecordingSettings::i_commit(void)
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

        for (RecordingScreenSettingsObjMap::const_iterator itScreenObj  = m->mapScreenObj.begin();
                                                           itScreenObj != m->mapScreenObj.end();
                                                         ++itScreenObj)
        {
            itScreenObj->second->i_commit();
            if (m->pPeer)
                m->pPeer->i_commit();
        }
    }
}

HRESULT RecordingSettings::i_copyFrom(RecordingSettings *aThat)
{
    AssertPtrReturn(aThat, E_INVALIDARG);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VBOX_E_INVALID_OBJECT_STATE);

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturn(thatCaller.hrc(), VBOX_E_INVALID_OBJECT_STATE);

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);

    HRESULT hrc = S_OK;

    for (RecordingScreenSettingsObjMap::const_iterator itScreenThat  = aThat->m->mapScreenObj.begin();
                                                       itScreenThat != aThat->m->mapScreenObj.end();
                                                     ++itScreenThat)
    {
        RecordingScreenSettingsObjMap::iterator itScreen = m->mapScreenObj.find(itScreenThat->first);
        if (itScreen != m->mapScreenObj.end())
        {
            itScreen->second->i_copyFrom(itScreenThat->second);
        }
        else
        {
            int vrc = i_createScreenObj(m->mapScreenObj,
                                        itScreenThat->first /* uScreenId */, itScreenThat->second->i_getData() /* Settings */);
            if (RT_FAILURE(vrc))
            {
                hrc = E_OUTOFMEMORY; /* Most likely. */
                break;
            }
        }
    }

    return hrc;
}

void RecordingSettings::i_applyDefaults(void)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertPtr(m->pMachine);
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    m->pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    ULONG cMonitors = 0;
    if (!pGraphicsAdapter.isNull())
        pGraphicsAdapter->COMGETTER(MonitorCount)(&cMonitors);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Initialize default capturing settings here. */
    m->bd->fEnabled = false;

    /* First, do a reset so that all internal screen settings objects are destroyed. */
    i_reset();
    /* Second, sync (again) to configured machine displays to (re-)create screen settings objects. */
    i_syncToMachineDisplays(cMonitors);
}

/**
 * Returns the full path to the default recording file.
 *
 * @returns VBox status code.
 * @param   strFile             Where to return the final file name on success.
 * @param   idScreen            Screen ID the file is associated to.
 * @param   fWithFileExtension  Whether to include the default file extension ('.webm') or not.
 */
int RecordingSettings::i_getDefaultFilename(Utf8Str &strFile, uint32_t idScreen, bool fWithFileExtension)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    strFile = m->pMachine->i_getSettingsFileFull(); // path/to/machinesfolder/vmname/vmname.vbox
    strFile.stripSuffix();
    strFile.append(Utf8StrFmt("-screen%RU32", idScreen));
    if (fWithFileExtension)
        strFile.append(".webm");

    return VINF_SUCCESS;
}

/**
 * Gets a standardized file name from a given template file name.
 *
 * @returns VBox status code.
 * @param   strFile             Where to return the final file name on success.
 * @param   idScreen            Screen ID the file is associated to.
 * @param   strTemplate         Template file name to use.
 *                              A default file name will be used when empty.
 */
int RecordingSettings::i_getFilename(Utf8Str &strFile, uint32_t idScreen, const Utf8Str &strTemplate)
{
    strFile = strTemplate;

    if (strFile.isEmpty())
        return i_getDefaultFilename(strFile, idScreen, true /* fWithFileExtension */);

    /* We force adding a .webm suffix to (hopefully) not let the user overwrite other important stuff. */
    strFile.stripSuffix();

    Utf8Str strDotExt = ".webm";

    /* We also force adding the screen id suffix, at least for the moment, as FE/Qt only offers settings a single file name
     * for *all* enabled screens. */
    char szSuffScreen[] = "-screen";
    Utf8Str strSuff = Utf8StrFmt("%s%RU32", szSuffScreen, idScreen);
    if (!strFile.endsWith(strSuff, Utf8Str::CaseInsensitive))
    {
        /** @todo The following line checks whether there already is a screen suffix, as FE/Qt currently always works with
         *        screen 0 as the file name. Remove the following if block when FE/Qt supports this properly. */
        Utf8Str strSuffScreen0 = Utf8StrFmt("%s%RU32", szSuffScreen, 0);
        if (strFile.endsWith(strSuffScreen0, Utf8Str::CaseInsensitive))
            strFile.truncate(strFile.length() - strSuffScreen0.length());

        strFile += strSuff; /* Add the suffix with the correct screen ID. */
    }

    strFile += strDotExt;

    LogRel2(("Recording: File name '%s' -> '%s'\n", strTemplate.c_str(), strFile.c_str()));

    return VINF_SUCCESS;
}

/**
 * Determines whether the recording settings currently can be changed or not.
 *
 * @returns \c true if the settings can be changed, \c false if not.
 */
bool RecordingSettings::i_canChangeSettings(void)
{
    AutoAnyStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc()))
        return false;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Only allow settings to be changed when recording is disabled when the machine is running. */
    if (   Global::IsOnline(adep.machineState())
        && m->bd->fEnabled)
    {
        return false;
    }

    return true;
}

/**
 * Gets called when the machine object needs to know that the recording settings
 * have been changed.
 */
void RecordingSettings::i_onSettingsChanged(void)
{
    LogFlowThisFuncEnter();

    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
    m->pMachine->i_setModified(Machine::IsModified_Recording);
    mlock.release();

    LogFlowThisFuncLeave();
}

/**
 * Starts recording.
 *
 * @returns VBox status code.
 */
int RecordingSettings::i_start(void)
{
    AssertReturn(m->mProgress.isNull(), VERR_WRONG_ORDER);

    HRESULT hrc = m->pMachine->i_onRecordingStateChange(RecordingState_Started, m->mProgress.asOutParam());
    if (FAILED(hrc))
        return VERR_RECORDING_INIT_FAILED;

    return VINF_SUCCESS;
}

/**
 * Stops recording. Does nothing if recording already has been stopped.
 *
 * @returns VBox status code.
 */
int RecordingSettings::i_stop(void)
{
    if (m->mProgress.isNull()) /* Not started? */
        return VINF_SUCCESS;

    /* Note: Returned progress object is just a dummy / not needed for disabling recording. */
    HRESULT hrc = m->pMachine->i_onRecordingStateChange(RecordingState_Stopped, m->mProgress.asOutParam());
    if (SUCCEEDED(hrc))
        m->mProgress.setNull();

    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    return VINF_SUCCESS;
}

/**
 * Synchronizes the screen settings (COM) objects and configuration data
 * to the number of the machine's configured displays.
 *
 * Note: This function ASSUMES that we always have configured VM displays
 *       as a consequtive sequence with no holes in between.
 */
int RecordingSettings::i_syncToMachineDisplays(uint32_t cDisplays)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogThisFunc(("%p: cDisplays=%RU32 vs. %zu\n", this, cDisplays, m->mapScreenObj.size()));

    /* If counts match, take a shortcut. */
    if (cDisplays == m->mapScreenObj.size())
        return VINF_SUCCESS;

    /* Create all new screen settings objects which are not there yet. */
    for (ULONG i = 0; i < cDisplays; i++)
    {
        if (m->mapScreenObj.find(i) == m->mapScreenObj.end())
        {
            settings::RecordingScreen defaultScreenSettings(i /* Screen ID */); /* Apply default settings. */

            int vrc2 = i_createScreenObj(m->mapScreenObj, i /* Screen ID */, defaultScreenSettings);
            AssertRC(vrc2);
        }
    }

    /* Remove all left over screen settings objects which are not needed anymore. */
    for (ULONG i = cDisplays; i < (ULONG)m->mapScreenObj.size(); i++)
    {
        int vrc2 = i_destroyScreenObj(m->mapScreenObj, i /* Screen ID */);
        AssertRC(vrc2);
    }

    Assert(m->mapScreenObj.size() == cDisplays);

    LogFlowThisFuncLeave();
    return VINF_SUCCESS;
}

