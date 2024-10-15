/* $Id: FirmwareSettingsImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Machine firmware settings.
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

#define LOG_GROUP LOG_GROUP_MAIN_FIRMWARESETTINGS
#include "FirmwareSettingsImpl.h"
#include "MachineImpl.h"
#include "GuestOSTypeImpl.h"

#include <iprt/cpp/utils.h>
#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"


////////////////////////////////////////////////////////////////////////////////
//
// FirmwareSettings private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct FirmwareSettings::Data
{
    Data()
        : pMachine(NULL)
    { }

    Machine * const             pMachine;
    ComObjPtr<FirmwareSettings> pPeer;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::FirmwareSettings> bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(FirmwareSettings)

HRESULT FirmwareSettings::FinalConstruct()
{
    return BaseFinalConstruct();
}

void FirmwareSettings::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the BIOS settings object.
 *
 * @returns COM result indicator
 */
HRESULT FirmwareSettings::init(Machine *aParent)
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

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the firmware settings object given another firmware settings object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 */
HRESULT FirmwareSettings::init(Machine *aParent, FirmwareSettings *that)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, that: %p\n", aParent, that));

    ComAssertRet(aParent && that, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    m->pPeer = that;

    AutoWriteLock thatlock(that COMMA_LOCKVAL_SRC_POS);
    m->bd.share(that->m->bd);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT FirmwareSettings::initCopy(Machine *aParent, FirmwareSettings *that)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, that: %p\n", aParent, that));

    ComAssertRet(aParent && that, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    // mPeer is left null

    AutoWriteLock thatlock(that COMMA_LOCKVAL_SRC_POS); /** @todo r=andy Shouldn't a read lock be sufficient here? */
    m->bd.attachCopy(that->m->bd);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void FirmwareSettings::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}

// IFirmwareSettings properties
/////////////////////////////////////////////////////////////////////////////


HRESULT FirmwareSettings::getLogoFadeIn(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fLogoFadeIn;

    return S_OK;
}

HRESULT FirmwareSettings::setLogoFadeIn(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fLogoFadeIn = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getLogoFadeOut(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fLogoFadeOut;

    return S_OK;
}

HRESULT FirmwareSettings::setLogoFadeOut(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fLogoFadeOut = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getLogoDisplayTime(ULONG *displayTime)
{
    if (!displayTime)
        return E_POINTER;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *displayTime = m->bd->ulLogoDisplayTime;

    return S_OK;
}

HRESULT FirmwareSettings::setLogoDisplayTime(ULONG displayTime)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->ulLogoDisplayTime = displayTime;

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getLogoImagePath(com::Utf8Str &imagePath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    imagePath = m->bd->strLogoImagePath;
    return S_OK;
}

HRESULT FirmwareSettings::setLogoImagePath(const com::Utf8Str &imagePath)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->strLogoImagePath = imagePath;

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}

HRESULT FirmwareSettings::getBootMenuMode(FirmwareBootMenuMode_T *bootMenuMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *bootMenuMode = m->bd->enmBootMenuMode;
    return S_OK;
}

HRESULT FirmwareSettings::setBootMenuMode(FirmwareBootMenuMode_T bootMenuMode)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->enmBootMenuMode = bootMenuMode;

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getACPIEnabled(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fACPIEnabled;

    return S_OK;
}

HRESULT FirmwareSettings::setACPIEnabled(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fACPIEnabled = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getIOAPICEnabled(BOOL *aIOAPICEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIOAPICEnabled = m->bd->fIOAPICEnabled;

    return S_OK;
}

HRESULT FirmwareSettings::getFirmwareType(FirmwareType_T *aType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aType = m->bd->firmwareType;

    return S_OK;
}

HRESULT FirmwareSettings::setFirmwareType(FirmwareType_T aType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->firmwareType = aType;

    alock.release();

    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // pMachine is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);
    Utf8Str strNVRAM = m->pMachine->i_getDefaultNVRAMFilename();
    mlock.release();

    m->pMachine->i_getNVRAMStore()->i_updateNonVolatileStorageFile(strNVRAM);

    return S_OK;
}

HRESULT FirmwareSettings::setIOAPICEnabled(BOOL aIOAPICEnabled)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fIOAPICEnabled = RT_BOOL(aIOAPICEnabled);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getAPICMode(APICMode_T *aAPICMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAPICMode = m->bd->apicMode;

    return S_OK;
}

HRESULT FirmwareSettings::setAPICMode(APICMode_T aAPICMode)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->apicMode = aAPICMode;

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getPXEDebugEnabled(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fPXEDebugEnabled;

    return S_OK;
}

HRESULT FirmwareSettings::setPXEDebugEnabled(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fPXEDebugEnabled = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getTimeOffset(LONG64 *offset)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *offset = m->bd->llTimeOffset;

    return S_OK;
}

HRESULT FirmwareSettings::setTimeOffset(LONG64 offset)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->llTimeOffset = offset;

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}


HRESULT FirmwareSettings::getSMBIOSUuidLittleEndian(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fSmbiosUuidLittleEndian;

    return S_OK;
}

HRESULT FirmwareSettings::setSMBIOSUuidLittleEndian(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fSmbiosUuidLittleEndian = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}

HRESULT FirmwareSettings::getAutoSerialNumGen(BOOL *enabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *enabled = m->bd->fAutoSerialNumGen;

    return S_OK;
}

HRESULT FirmwareSettings::setAutoSerialNumGen(BOOL enable)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fAutoSerialNumGen = RT_BOOL(enable);

    alock.release();
    AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);  // mParent is const, needs no locking
    m->pMachine->i_setModified(Machine::IsModified_Firmware);

    return S_OK;
}

// IFirmwareSettings methods
/////////////////////////////////////////////////////////////////////////////

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given machine node.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT FirmwareSettings::i_loadSettings(const settings::FirmwareSettings &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    m->bd.assignCopy(&data);
    return S_OK;
}

/**
 *  Saves settings to the given machine node.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT FirmwareSettings::i_saveSettings(settings::FirmwareSettings &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *m->bd.data();

    return S_OK;
}

FirmwareType_T FirmwareSettings::i_getFirmwareType() const
{
    return m->bd->firmwareType;
}

void FirmwareSettings::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->bd.rollback();
}

void FirmwareSettings::i_commit()
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

void FirmwareSettings::i_copyFrom(FirmwareSettings *aThat)
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

void FirmwareSettings::i_applyDefaults(GuestOSType *aOsType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Initialize default firmware settings here */
    if (aOsType)
    {
        HRESULT hrc = aOsType->COMGETTER(RecommendedFirmware)(&m->bd->firmwareType);
        AssertComRC(hrc);

        m->bd->fIOAPICEnabled = aOsType->i_recommendedIOAPIC();
    }
    else
    {
        m->bd->firmwareType   = FirmwareType_BIOS;
        m->bd->fIOAPICEnabled = true;
    }

    /// @todo r=andy BUGBUG Is this really enough here? What about the other stuff?
}

