/* $Id: PlatformImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Platform settings.
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

#define LOG_GROUP LOG_GROUP_MAIN_PLATFORM
#include "PlatformImpl.h"
#ifdef VBOX_WITH_VIRT_ARMV8
# include "PlatformARMImpl.h"
#endif
#include "PlatformX86Impl.h"
#include "PlatformPropertiesImpl.h"
#include "MachineImpl.h"
#include "LoggingNew.h"

#include "AutoStateDep.h"

#include <iprt/cpp/utils.h>

#include <VBox/settings.h>


struct Platform::Data
{
    Data() { }

    ComObjPtr<Platform> pPeer;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::Platform> bd;
};


/*
 * Platform implementation.
 */
Platform::Platform()
    : mParent(NULL)
{
}

Platform::~Platform()
{
    uninit();
}

HRESULT Platform::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Platform::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT Platform::init(Machine *aParent)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Share the parent weakly */
    unconst(mParent) = aParent;

    m = new Data();

    m->bd.allocate();

    /* Allocates architecture-dependent stuff.
     * Note: We ignore the return value here, as the machine object expects a working platform object.
             We always want a working platform object, no matter if we support the current platform architecture or not. */
    i_initArchitecture(m->bd->architectureType);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
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
HRESULT Platform::init(Machine *aParent, Platform *aThat)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    m = new Data();
    m->pPeer = aThat;

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    /* Allocates architecture-dependent stuff.
     * Note: We ignore the return value here, as the machine object expects a working platform object.
             We always want a working platform object, no matter if we support the current platform architecture or not. */
    i_initArchitecture(aThat->m->bd->architectureType, aThat);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 * Initializes the guest object given another guest object
 * (a kind of copy constructor). This object makes a private copy of data
 * of the original object passed as an argument.
 */
HRESULT Platform::initCopy(Machine *aParent, Platform *aThat)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, aThat: %p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    m = new Data();
    // m->pPeer is left null

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS); /** @todo r=andy Shouldn't a read lock be sufficient here? */
    m->bd.attachCopy(aThat->m->bd);

    /* Allocates architecture-dependent stuff.
     * Note: We ignore the return value here, as the machine object expects a working platform object.
             We always want a working platform object, no matter if we support the current platform architecture or not. */
    i_initArchitecture(aThat->m->bd->architectureType, aThat, true /* fCopy */);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

void Platform::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;

    uninitArchitecture();

    m->bd.free();

    unconst(m->pPeer) = NULL;

    delete m;
    m = NULL;
}

/**
 * Unitializes all platform-specific objects.
 *
 * Called by uninit() and i_setArchitecture().
 */
void Platform::uninitArchitecture()
{
    if (mX86)
    {
        mX86->uninit();
        unconst(mX86).setNull();
    }

#ifdef VBOX_WITH_VIRT_ARMV8
    if (mARM)
    {
        mARM->uninit();
        unconst(mARM).setNull();
    }
#endif
}


// IPlatform properties
////////////////////////////////////////////////////////////////////////////////

HRESULT Platform::getArchitecture(PlatformArchitecture_T *aArchitecture)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aArchitecture = m->bd->architectureType;

    return S_OK;
}

HRESULT Platform::setArchitecture(PlatformArchitecture_T aArchitecture)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    RT_NOREF(aArchitecture);

    /* Currently we don't allow changing the platform architecture after the object was created.
     * Mostly makes no sense, as this would render the VMs non-bootable and confuses users. */
    return E_NOTIMPL;
}

HRESULT Platform::getProperties(ComPtr<IPlatformProperties> &aProperties)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ComObjPtr<PlatformProperties> properties;
    HRESULT hrc = properties.createObject();
    AssertComRCReturnRC(hrc);
    hrc = properties->init(mParent->mParent);
    AssertComRCReturnRC(hrc);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    hrc = properties->i_setArchitecture(m->bd->architectureType);
    AssertComRCReturnRC(hrc);

    return properties.queryInterfaceTo(aProperties.asOutParam());
}

HRESULT Platform::getX86(ComPtr<IPlatformX86> &aX86)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (m->bd->architectureType)
    {
        case PlatformArchitecture_x86:
        {
            if (mX86.isNotNull())
            {
                /* mX86 is constant during life time, no need to lock. */
                return mX86.queryInterfaceTo(aX86.asOutParam());
            }
            break;
        }

        default:
            /* For anything else return an error. */
            break;
    }

    return setErrorBoth(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED, VERR_PLATFORM_ARCH_NOT_SUPPORTED,
                        "x86-specific platform settings are not available on this platform");
}

HRESULT Platform::getARM(ComPtr<IPlatformARM> &aARM)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (m->bd->architectureType)
    {
#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
        {
            if (mARM.isNotNull())
            {
                /* mARM is constant during life time, no need to lock. */
                return mARM.queryInterfaceTo(aARM.asOutParam());
            }
            break;
        }
#else
        RT_NOREF(aARM);
#endif
        default:
            /* For anything else return an error. */
            break;
    }

    return setErrorBoth(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED, VERR_PLATFORM_ARCH_NOT_SUPPORTED,
                        "ARM-specific platform settings are not available on this platform");
}

HRESULT Platform::getChipsetType(ChipsetType_T *aChipsetType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aChipsetType = m->bd->chipsetType;

    return S_OK;
}

HRESULT Platform::setChipsetType(ChipsetType_T aChipsetType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aChipsetType != m->bd->chipsetType)
    {
        m->bd.backup();
        m->bd->chipsetType = aChipsetType;

        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
        mParent->i_setModified(Machine::IsModified_Platform);

        // Resize network adapter array, to be finalized on commit/rollback.
        // We must not throw away entries yet, otherwise settings are lost
        // without a way to roll back.
        size_t const newCount = PlatformProperties::s_getMaxNetworkAdapters(aChipsetType);
        size_t const oldCount = mParent->mNetworkAdapters.size();
        if (newCount > oldCount)
        {
            mParent->mNetworkAdapters.resize(newCount);
            for (size_t slot = oldCount; slot < mParent->mNetworkAdapters.size(); slot++)
            {
                unconst(mParent->mNetworkAdapters[slot]).createObject();
                mParent->mNetworkAdapters[slot]->init(mParent, (ULONG)slot);
            }
        }
    }

    return S_OK;
}

HRESULT Platform::getIommuType(IommuType_T *aIommuType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIommuType = m->bd->iommuType;

    return S_OK;
}

HRESULT Platform::setIommuType(IommuType_T aIommuType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aIommuType != m->bd->iommuType)
    {
        if (aIommuType == IommuType_Intel)
        {
#ifndef VBOX_WITH_IOMMU_INTEL
            LogRelFunc(("Setting Intel IOMMU when Intel IOMMU support not available!\n"));
            return E_UNEXPECTED;
#endif
        }

        m->bd.backup();
        m->bd->iommuType = aIommuType;

        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
        mParent->i_setModified(Machine::IsModified_Platform);
    }

    return S_OK;
}

HRESULT Platform::getRTCUseUTC(BOOL *aRTCUseUTC)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aRTCUseUTC = m->bd->fRTCUseUTC;

    return S_OK;
}

HRESULT Platform::setRTCUseUTC(BOOL aRTCUseUTC)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fRTCUseUTC != RT_BOOL(aRTCUseUTC))
    {
        /* Only allow it to be set to true when PoweredOff or Aborted.
           (Clearing it is always permitted.) */
        if (aRTCUseUTC)
        {
            alock.release();

            /* the machine needs to be mutable */
            AutoMutableStateDependency adep(mParent);
            if (FAILED(adep.hrc())) return adep.hrc();

            alock.acquire();

            m->bd.backup();
            m->bd->fRTCUseUTC = RT_BOOL(aRTCUseUTC);

            alock.release();

            AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
            mParent->i_setModified(Machine::IsModified_Platform);
        }
        else
        {
            m->bd.backup();
            m->bd->fRTCUseUTC = RT_BOOL(aRTCUseUTC);
        }
    }

    return S_OK;
}


// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 * Loads settings from the given platform node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for writing.
 */
HRESULT Platform::i_loadSettings(const settings::Platform &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    m->bd.assignCopy(&data);

    /* Allocates architecture-dependent stuff. */
    HRESULT hrc = i_initArchitecture(m->bd->architectureType);
    if (SUCCEEDED(hrc))
    {
        switch (m->bd->architectureType)
        {
            case PlatformArchitecture_x86:
                return mX86->i_loadSettings(data.x86);

#ifdef VBOX_WITH_VIRT_ARMV8
            case PlatformArchitecture_ARM:
                return mARM->i_loadSettings(data.arm);
#endif
            case PlatformArchitecture_None:
                RT_FALL_THROUGH();
            default:
                break;
        }
    }

    return setErrorBoth(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED, VERR_PLATFORM_ARCH_NOT_SUPPORTED,
                        "Platform '%s' not supported", Global::stringifyPlatformArchitecture(m->bd->architectureType));
}

/**
 * Saves settings to the given platform node.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for reading.
 */
HRESULT Platform::i_saveSettings(settings::Platform &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *m->bd.data();

    switch (m->bd->architectureType)
    {
        case PlatformArchitecture_x86:
            return mX86->i_saveSettings(data.x86);

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
            return mARM->i_saveSettings(data.arm);
#endif
        case PlatformArchitecture_None:
            RT_FALL_THROUGH();
        default:
            break;
    }

    return setErrorBoth(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED, VERR_PLATFORM_ARCH_NOT_SUPPORTED,
                            "Platform '%s' not supported", Global::stringifyPlatformArchitecture(m->bd->architectureType));
}

void Platform::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m)
    {
        m->bd.rollback();

        switch (m->bd->architectureType)
        {
            case PlatformArchitecture_x86:
                if (mX86.isNotNull())
                    return mX86->i_rollback();
                break;

#ifdef VBOX_WITH_VIRT_ARMV8
            case PlatformArchitecture_ARM:
                if (mARM.isNotNull())
                    return mARM->i_rollback();
                break;
#endif
            case PlatformArchitecture_None:
                RT_FALL_THROUGH();
            default:
                break;
        }
    }
}

void Platform::i_commit()
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

    switch (m->bd->architectureType)
    {
        case PlatformArchitecture_x86:
            return mX86->i_commit();

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
            return mARM->i_commit();
#endif
        case PlatformArchitecture_None:
            RT_FALL_THROUGH();
        default:
            break;
    }

    AssertFailedReturnVoid();
}

void Platform::i_copyFrom(Platform *aThat)
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

    switch (m->bd->architectureType)
    {
        case PlatformArchitecture_x86:
            return mX86->i_copyFrom(aThat->mX86);

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
            return mARM->i_copyFrom(aThat->mARM);
#endif
        case PlatformArchitecture_None:
            RT_FALL_THROUGH();
        default:
            break;
    }

    AssertFailedReturnVoid();
}

/**
 * Initializes the platform architecture.
 *
 * @returns HRESULT
 * @retval  VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED if platform architecture is not supported.
 * @param   aArchitecture       Platform architecture to set.
 * @param   aThat               Other platform object to use for shared / copied initialization. Optional.
 * @param   fCopy               Whether to copy or share the configuration from / with \a aThat.
 *
 * @note    Creates the platform-specific sub object (e.g. x86 or ARM).
 *          Usually only called when creating a new machine or loading settings.
 */
HRESULT Platform::i_initArchitecture(PlatformArchitecture_T aArchitecture, Platform *aThat /* = NULL */, bool fCopy /* = false */)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* Currently we only keep the current platform-specific object around,
     * e.g. we destroy any data for the former architecture (if any). */
    uninitArchitecture();

    switch (aArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            /* Create associated x86-specific platform object. */
            Assert(mX86.isNull());
            hrc = unconst(mX86).createObject();
            ComAssertComRCRetRC(hrc);
            if (aThat)
            {
                if (fCopy)
                    hrc = mX86->initCopy(this, mParent, aThat->mX86);
                else
                    hrc = mX86->init(this, mParent, aThat->mX86);
            }
            else
                hrc = mX86->init(this, mParent);
            break;
        }

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
        {
            /* Create associated ARM-specific platform object. */
            Assert(mARM.isNull());
            unconst(mARM).createObject();
            ComAssertComRCRetRC(hrc);
            if (aThat)
            {
                if (fCopy)
                    hrc = mARM->initCopy(this, mParent, aThat->mARM);
                else
                    hrc = mARM->init(this, mParent, aThat->mARM);
            }
            else
                hrc = mARM->init(this, mParent);
            break;
        }
#endif
        default:
            hrc = VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED;
            break;
    }

    if (SUCCEEDED(hrc))
    {
        m->bd->architectureType = aArchitecture;
        LogRel(("Platform architecture set to '%s'\n", Global::stringifyPlatformArchitecture(m->bd->architectureType)));
    }

    return hrc;
}

HRESULT Platform::i_applyDefaults(GuestOSType *aOsType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), autoCaller.hrc());

    HRESULT hrc = S_OK;

    ChipsetType_T enmChipsetType = ChipsetType_Null;
    IommuType_T   enmIommuType   = IommuType_None;
    BOOL          fRTCUseUTC     = FALSE;

    if (aOsType)
    {
        hrc = aOsType->COMGETTER(RecommendedChipset)(&enmChipsetType);
        AssertComRCReturnRC(hrc);

        hrc = aOsType->COMGETTER(RecommendedIommuType)(&enmIommuType);
        AssertComRCReturnRC(hrc);

        hrc = aOsType->COMGETTER(RecommendedRTCUseUTC)(&fRTCUseUTC);
        AssertComRCReturnRC(hrc);
    }
    else
    {
        /* No guest OS type object. Pick some plausible defaults which the
         * host can handle. There's no way to know or validate anything. */
        switch (m->bd->architectureType)
        {
            case PlatformArchitecture_x86:
            {
                /* Note: These are the value we ever had, so default to these. */
                enmChipsetType = ChipsetType_PIIX3;
                enmIommuType   = IommuType_None;
                fRTCUseUTC     = FALSE;
                break;
            }

            case PlatformArchitecture_ARM:
            {
                /* Note: These are the value we ever had, so default to these. */
                enmChipsetType = ChipsetType_ARMv8Virtual;
                enmIommuType   = IommuType_None;
                fRTCUseUTC     = TRUE; /** @todo BUGBUG Is this correct for ARM? */
                break;
            }

            default:
                AssertFailed();
                break;
        }
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd->chipsetType = enmChipsetType;
    m->bd->iommuType   = enmIommuType;
    m->bd->fRTCUseUTC  = fRTCUseUTC;

    alock.release();

    /* Allocates architecture-dependent stuff. */
    hrc = i_initArchitecture(m->bd->architectureType);
    AssertComRCReturnRC(hrc);

    /* Do the same for the platform specifics. */
    switch (m->bd->architectureType)
    {
        case PlatformArchitecture_x86:
            hrc = mX86->i_applyDefaults(aOsType);
            break;

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
            hrc = mARM->i_applyDefaults(aOsType);
            break;
#endif
        case PlatformArchitecture_None:
            RT_FALL_THROUGH();
        default:
            hrc = VBOX_E_NOT_SUPPORTED;
            break;
    }
    AssertComRCReturnRC(hrc);

    /* Sanity. */
    Assert(enmChipsetType != ChipsetType_Null);

    return hrc;
}

/**
 * Internal helper to return the currently set architecture of this platform.
 *
 * @returns Currently set architecture.
 *
 * @note    Takes the read lock.
 */
PlatformArchitecture_T Platform::i_getArchitecture(void)
{
    PlatformArchitecture_T enmArchitecture;
    HRESULT const hrc = getArchitecture(&enmArchitecture);
    AssertComRCReturn(hrc, PlatformArchitecture_None);
    return enmArchitecture;
}

