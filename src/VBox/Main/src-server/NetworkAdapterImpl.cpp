/* $Id: NetworkAdapterImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of INetworkAdapter in VBoxSVC.
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

#define LOG_GROUP LOG_GROUP_MAIN_NETWORKADAPTER
#include "NetworkAdapterImpl.h"
#include "NATEngineImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "MachineImpl.h"
#include "GuestOSTypeImpl.h"
#include "HostImpl.h"
#include "SystemPropertiesImpl.h"
#include "VirtualBoxImpl.h"

#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <iprt/errcore.h>
#include <VBox/settings.h>

#include "AutoStateDep.h"

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

NetworkAdapter::NetworkAdapter()
    : mParent(NULL)
{
}

NetworkAdapter::~NetworkAdapter()
{
}

HRESULT NetworkAdapter::FinalConstruct()
{
    return BaseFinalConstruct();
}

void NetworkAdapter::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the network adapter object.
 *
 *  @param aParent  Handle of the parent object.
 *  @param uSlot    Slot number this network adapter is plugged into.
 */
HRESULT NetworkAdapter::init(Machine *aParent, ULONG uSlot)
{
    ComAssertRet(aParent, E_INVALIDARG);

    LogFlowThisFunc(("aParent=%p, uSlot=%d\n", aParent, uSlot));

    ChipsetType_T enmChipsetType;
    HRESULT hrc = aParent->i_getPlatform()->getChipsetType(&enmChipsetType);
    if (FAILED(hrc))
        return hrc;

    uint32_t const maxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(enmChipsetType);
    ComAssertRet(uSlot < maxNetworkAdapters, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    unconst(mNATEngine).createObject();
    mNATEngine->init(aParent, this);
    /* mPeer is left null */

    mData.allocate();

    /* initialize data */
    mData->ulSlot = uSlot;

    /* default to Am79C973 */
    mData->type = NetworkAdapterType_Am79C973;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the network adapter object given another network adapter object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @param  aParent     Parent object.
 *  @param  aThat
 *  @param  aReshare
 *      When false, the original object will remain a data owner.
 *      Otherwise, data ownership will be transferred from the original
 *      object to this one.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT NetworkAdapter::init(Machine *aParent, NetworkAdapter *aThat, bool aReshare /* = false */)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p, aReshare=%RTbool\n", aParent, aThat, aReshare));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    /* mPeer is left null */

    unconst(mNATEngine).createObject();
    mNATEngine->init(aParent, this, aThat->mNATEngine);

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    if (aReshare)
    {
        AutoWriteLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

        unconst(aThat->mPeer) = this;
        mData.attach(aThat->mData);
    }
    else
    {
        unconst(mPeer) = aThat;

        AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
        mData.share(aThat->mData);
    }

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
HRESULT NetworkAdapter::initCopy(Machine *aParent, NetworkAdapter *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    /* mPeer is left null */

    unconst(mNATEngine).createObject();
    mNATEngine->initCopy(aParent, this, aThat->mNATEngine);

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    mData.attachCopy(aThat->mData);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void NetworkAdapter::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    mData.free();

    unconst(mNATEngine).setNull();
    unconst(mPeer) = NULL;
    unconst(mParent) = NULL;
}

// wrapped INetworkAdapter properties
////////////////////////////////////////////////////////////////////////////////
HRESULT NetworkAdapter::getAdapterType(NetworkAdapterType_T *aAdapterType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAdapterType = mData->type;

    return S_OK;
}

HRESULT NetworkAdapter::setAdapterType(NetworkAdapterType_T aAdapterType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* make sure the value is allowed */
    switch (aAdapterType)
    {
        case NetworkAdapterType_Am79C970A:
        case NetworkAdapterType_Am79C973:
        case NetworkAdapterType_Am79C960:
#ifdef VBOX_WITH_E1000
        case NetworkAdapterType_I82540EM:
        case NetworkAdapterType_I82543GC:
        case NetworkAdapterType_I82545EM:
#endif
#ifdef VBOX_WITH_VIRTIO
        case NetworkAdapterType_Virtio:
#endif
        case NetworkAdapterType_NE1000:
        case NetworkAdapterType_NE2000:
        case NetworkAdapterType_WD8003:
        case NetworkAdapterType_WD8013:
        case NetworkAdapterType_ELNK2:
        case NetworkAdapterType_ELNK1:
            break;
        default:
            return setError(E_FAIL,
                            tr("Invalid network adapter type '%d'"),
                            aAdapterType);
    }

    if (mData->type != aAdapterType)
    {
        mData.backup();
        mData->type = aAdapterType;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* Changing the network adapter type during runtime is not allowed,
         * therefore no immediate change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}


HRESULT NetworkAdapter::getSlot(ULONG *uSlot)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *uSlot = mData->ulSlot;

    return S_OK;
}

HRESULT NetworkAdapter::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = mData->fEnabled;

    return S_OK;
}

HRESULT NetworkAdapter::setEnabled(BOOL aEnabled)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->fEnabled != RT_BOOL(aEnabled))
    {
        mData.backup();
        mData->fEnabled = RT_BOOL(aEnabled);
        if (RT_BOOL(aEnabled) && mData->strMACAddress.isEmpty())
            i_generateMACAddress();

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* Disabling the network adapter during runtime is not allowed
         * therefore no immediate change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getMACAddress(com::Utf8Str &aMACAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComAssertRet(!mData->fEnabled || !mData->strMACAddress.isEmpty(), E_FAIL);

    aMACAddress = mData->strMACAddress;

    return S_OK;
}

HRESULT NetworkAdapter::i_updateMacAddress(Utf8Str aMACAddress)
{
    HRESULT hrc = S_OK;

    /*
     * Are we supposed to generate a MAC?
     */
    if (mData->fEnabled && aMACAddress.isEmpty())
        i_generateMACAddress();
    else
    {
        if (mData->strMACAddress != aMACAddress)
        {
            if (mData->fEnabled || !aMACAddress.isEmpty())
            {
                /*
                 * Verify given MAC address
                 */
                char *macAddressStr = aMACAddress.mutableRaw();
                int i = 0;
                while ((i < 13) && macAddressStr && *macAddressStr && hrc == S_OK)
                {
                    char c = *macAddressStr;
                    /* canonicalize hex digits to capital letters */
                    if (c >= 'a' && c <= 'f')
                    {
                        c = (char)RTLocCToUpper(c);
                        *macAddressStr = c;
                    }
                    /* we only accept capital letters */
                    if (   (c < '0' || c > '9')
                        && (c < 'A' || c > 'F'))
                        hrc = setError(E_INVALIDARG, tr("Invalid MAC address format"));
                    /* the second digit must have even value for unicast addresses */
                    if (   (i == 1)
                        && (!!(c & 1) == (c >= '0' && c <= '9')))
                        hrc = setError(E_INVALIDARG, tr("Invalid MAC address format"));

                    macAddressStr++;
                    i++;
                }
                /* we must have parsed exactly 12 characters */
                if (i != 12)
                    hrc = setError(E_INVALIDARG, tr("Invalid MAC address format"));
            }

            if (SUCCEEDED(hrc))
                mData->strMACAddress = aMACAddress;
        }
    }

    return hrc;
}

HRESULT NetworkAdapter::setMACAddress(const com::Utf8Str &aMACAddress)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.backup();

    HRESULT hrc = i_updateMacAddress(aMACAddress);
    if (SUCCEEDED(hrc))
    {
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* Changing the MAC via the Main API during runtime is not allowed,
         * therefore no immediate change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return hrc;
}

HRESULT NetworkAdapter::getAttachmentType(NetworkAttachmentType_T *aAttachmentType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAttachmentType = mData->mode;

    return S_OK;
}

HRESULT NetworkAdapter::setAttachmentType(NetworkAttachmentType_T aAttachmentType)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->mode != aAttachmentType)
    {
        mData.backup();

        /* there must an internal network name */
        if (mData->strInternalNetworkName.isEmpty())
        {
            Log(("Internal network name not defined, setting to default \"intnet\"\n"));
            mData->strInternalNetworkName = "intnet";
        }

        /* there must a NAT network name */
        if (mData->strNATNetworkName.isEmpty())
        {
            Log(("NAT network name not defined, setting to default \"NatNetwork\"\n"));
            mData->strNATNetworkName = "NatNetwork";
        }

        NetworkAttachmentType_T oldAttachmentType = mData->mode;
        mData->mode = aAttachmentType;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        if (oldAttachmentType == NetworkAttachmentType_NATNetwork)
            i_switchFromNatNetworking(mData->strNATNetworkName);

        if (aAttachmentType == NetworkAttachmentType_NATNetwork)
            i_switchToNatNetworking(mData->strNATNetworkName);

        /* Adapt the CFGM logic and notify the guest => changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getBridgedInterface(com::Utf8Str &aBridgedInterface)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aBridgedInterface = mData->strBridgedName;

    return S_OK;
}

HRESULT NetworkAdapter::setBridgedInterface(const com::Utf8Str &aBridgedInterface)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    Bstr canonicalName = aBridgedInterface;
#ifdef RT_OS_DARWIN
    com::SafeIfaceArray<IHostNetworkInterface> hostNetworkInterfaces;
    ComPtr<IHost> host;
    HRESULT hrc = mParent->i_getVirtualBox()->COMGETTER(Host)(host.asOutParam());
    if (SUCCEEDED(hrc))
    {
        host->FindHostNetworkInterfacesOfType(HostNetworkInterfaceType_Bridged,
                                              ComSafeArrayAsOutParam(hostNetworkInterfaces));
        for (size_t i = 0; i < hostNetworkInterfaces.size(); ++i)
        {
            Bstr shortName;
            ComPtr<IHostNetworkInterface> ni = hostNetworkInterfaces[i];
            ni->COMGETTER(ShortName)(shortName.asOutParam());
            if (shortName == aBridgedInterface)
            {
                ni->COMGETTER(Name)(canonicalName.asOutParam());
                break;
            }
        }
    }
#endif /* RT_OS_DARWIN */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (Bstr(mData->strBridgedName) != canonicalName)
    {
        /* if an empty/null string is to be set, bridged interface must be
         * turned off */
        if (   canonicalName.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_Bridged)
        {
            return setError(E_FAIL,
                            tr("Empty or null bridged interface name is not valid"));
        }

        mData.backup();
        mData->strBridgedName = canonicalName;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* When changing the host adapter, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getHostOnlyInterface(com::Utf8Str &aHostOnlyInterface)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aHostOnlyInterface = mData->strHostOnlyName;

    return S_OK;
}

HRESULT NetworkAdapter::setHostOnlyInterface(const com::Utf8Str &aHostOnlyInterface)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strHostOnlyName != aHostOnlyInterface)
    {
        /* if an empty/null string is to be set, host only interface must be
         * turned off */
        if (   aHostOnlyInterface.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_HostOnly)
        {
            return setError(E_FAIL,
                            tr("Empty or null host only interface name is not valid"));
        }

        mData.backup();
        mData->strHostOnlyName = aHostOnlyInterface;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* When changing the host adapter, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}


HRESULT NetworkAdapter::getHostOnlyNetwork(com::Utf8Str &aHostOnlyNetwork)
{
#ifdef VBOX_WITH_VMNET
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aHostOnlyNetwork = mData->strHostOnlyNetworkName;

    return S_OK;
#else /* !VBOX_WITH_VMNET */
    NOREF(aHostOnlyNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}

HRESULT NetworkAdapter::setHostOnlyNetwork(const com::Utf8Str &aHostOnlyNetwork)
{
#ifdef VBOX_WITH_VMNET
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strHostOnlyNetworkName != aHostOnlyNetwork)
    {
        /* if an empty/null string is to be set, host only Network must be
         * turned off */
        if (   aHostOnlyNetwork.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_HostOnly)
        {
            return setError(E_FAIL,
                            tr("Empty or null host only Network name is not valid"));
        }

        mData.backup();
        mData->strHostOnlyNetworkName = aHostOnlyNetwork;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* When changing the host adapter, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
#else /* !VBOX_WITH_VMNET */
    NOREF(aHostOnlyNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}


HRESULT NetworkAdapter::getInternalNetwork(com::Utf8Str &aInternalNetwork)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aInternalNetwork = mData->strInternalNetworkName;

    return S_OK;
}

HRESULT NetworkAdapter::setInternalNetwork(const com::Utf8Str &aInternalNetwork)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strInternalNetworkName != aInternalNetwork)
    {
        /* if an empty/null string is to be set, internal networking must be
         * turned off */
        if (   aInternalNetwork.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_Internal)
        {
            return setError(E_FAIL,
                            tr("Empty or null internal network name is not valid"));
        }
        mData.backup();
        mData->strInternalNetworkName = aInternalNetwork;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* When changing the internal network, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getNATNetwork(com::Utf8Str &aNATNetwork)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aNATNetwork = mData->strNATNetworkName;

    return S_OK;
}


HRESULT NetworkAdapter::setNATNetwork(const com::Utf8Str &aNATNetwork)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strNATNetworkName != aNATNetwork)
    {
        /* if an empty/null string is to be set, host only interface must be
         * turned off */
        if (   aNATNetwork.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_NATNetwork)
            return setError(E_FAIL,
                            tr("Empty or null NAT network name is not valid"));

        mData.backup();

        Bstr oldNatNetworkName = mData->strNATNetworkName;
        mData->strNATNetworkName = aNATNetwork;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        if (mData->mode == NetworkAttachmentType_NATNetwork)
        {
            i_switchFromNatNetworking(oldNatNetworkName.raw());
            i_switchToNatNetworking(aNATNetwork);
        }

        /* When changing the host adapter, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getGenericDriver(com::Utf8Str &aGenericDriver)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aGenericDriver = mData->strGenericDriver;

    return S_OK;
}

HRESULT NetworkAdapter::setGenericDriver(const com::Utf8Str &aGenericDriver)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strGenericDriver != aGenericDriver)
    {
        mData.backup();
        mData->strGenericDriver = aGenericDriver;

        /* leave the lock before informing callbacks */
        alock.release();

        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}


HRESULT NetworkAdapter::getCloudNetwork(com::Utf8Str &aCloudNetwork)
{
#ifdef VBOX_WITH_CLOUD_NET
   AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aCloudNetwork = mData->strCloudNetworkName;

    return S_OK;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aCloudNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}

HRESULT NetworkAdapter::setCloudNetwork(const com::Utf8Str &aCloudNetwork)
{
#ifdef VBOX_WITH_CLOUD_NET
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strCloudNetworkName != aCloudNetwork)
    {
        /* if an empty/null string is to be set, Cloud networking must be
         * turned off */
        if (   aCloudNetwork.isEmpty()
            && mData->fEnabled
            && mData->mode == NetworkAttachmentType_Cloud)
        {
            return setError(E_FAIL,
                            tr("Empty or null Cloud network name is not valid"));
        }
        mData.backup();
        mData->strCloudNetworkName = aCloudNetwork;

        // leave the lock before informing callbacks
        alock.release();

#if 0
        /// @todo Implement dynamic re-attachment of cloud network
        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* When changing the internal network, adapt the CFGM logic to make this
         * change immediately effect and to notify the guest that the network
         * might have changed, therefore changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
#else
        mParent->i_onNetworkAdapterChange(this, FALSE);
#endif
    }
    return S_OK;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aCloudNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}


HRESULT NetworkAdapter::getCableConnected(BOOL *aConnected)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aConnected = mData->fCableConnected;

    return S_OK;
}


HRESULT NetworkAdapter::setCableConnected(BOOL aConnected)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (RT_BOOL(aConnected) != mData->fCableConnected)
    {
        mData.backup();
        mData->fCableConnected = RT_BOOL(aConnected);

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* No change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}


HRESULT NetworkAdapter::getLineSpeed(ULONG *aSpeed)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aSpeed = mData->ulLineSpeed;

    return S_OK;
}

HRESULT NetworkAdapter::setLineSpeed(ULONG aSpeed)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aSpeed != mData->ulLineSpeed)
    {
        mData.backup();
        mData->ulLineSpeed = aSpeed;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* No change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getPromiscModePolicy(NetworkAdapterPromiscModePolicy_T *aPromiscModePolicy)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aPromiscModePolicy = mData->enmPromiscModePolicy;

    return S_OK;
}

HRESULT NetworkAdapter::setPromiscModePolicy(NetworkAdapterPromiscModePolicy_T aPromiscModePolicy)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    switch (aPromiscModePolicy)
    {
        case NetworkAdapterPromiscModePolicy_Deny:
        case NetworkAdapterPromiscModePolicy_AllowNetwork:
        case NetworkAdapterPromiscModePolicy_AllowAll:
            break;
        default:
            return setError(E_INVALIDARG, tr("Invalid promiscuous mode policy (%d)"), aPromiscModePolicy);
    }

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();

    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (aPromiscModePolicy != mData->enmPromiscModePolicy)
        {
            mData.backup();
            mData->enmPromiscModePolicy = aPromiscModePolicy;

            alock.release();
            mParent->i_setModifiedLock(Machine::IsModified_NetworkAdapters);
            mParent->i_onNetworkAdapterChange(this, TRUE);
        }
    }

    return hrc;
}


HRESULT NetworkAdapter::getTraceEnabled(BOOL *aEnabled)
{

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = mData->fTraceEnabled;

    return S_OK;
}

HRESULT NetworkAdapter::setTraceEnabled(BOOL aEnabled)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (RT_BOOL(aEnabled) != mData->fTraceEnabled)
    {
        mData.backup();
        mData->fTraceEnabled = RT_BOOL(aEnabled);

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* Adapt the CFGM logic changeAdapter=TRUE */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getTraceFile(com::Utf8Str &aTraceFile)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aTraceFile = mData->strTraceFile;

    return S_OK;
}


HRESULT NetworkAdapter::setTraceFile(const com::Utf8Str &aTraceFile)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strTraceFile != aTraceFile)
    {
        mData.backup();
        mData->strTraceFile = aTraceFile;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* We change the 'File' => changeAdapter=TRUE. */
        mParent->i_onNetworkAdapterChange(this, TRUE);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getNATEngine(ComPtr<INATEngine> &aNATEngine)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aNATEngine  = mNATEngine;

    return S_OK;
}

HRESULT NetworkAdapter::getBootPriority(ULONG *aBootPriority)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aBootPriority = mData->ulBootPriority;

    return S_OK;
}

HRESULT NetworkAdapter::setBootPriority(ULONG aBootPriority)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aBootPriority != mData->ulBootPriority)
    {
        mData.backup();
        mData->ulBootPriority = aBootPriority;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);       // mParent is const, no need to lock
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* No change in CFGM logic => changeAdapter=FALSE. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    return S_OK;
}

// wrapped INetworkAdapter methods
////////////////////////////////////////////////////////////////////////////////

HRESULT NetworkAdapter::getProperty(const com::Utf8Str &aKey, com::Utf8Str &aValue)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aValue = "";
    settings::StringsMap::const_iterator it = mData->genericProperties.find(aKey);
    if (it != mData->genericProperties.end())
        aValue = it->second; // source is a Utf8Str

    return S_OK;
}

HRESULT NetworkAdapter::setProperty(const com::Utf8Str &aKey, const com::Utf8Str &aValue)
{
    LogFlowThisFunc(("\n"));
    /* The machine needs to be mutable. */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    bool fGenericChange = (mData->mode == NetworkAttachmentType_Generic);
    /* Generic properties processing.
     * Look up the old value first; if nothing's changed then do nothing.
     */
    Utf8Str strOldValue;
    settings::StringsMap::const_iterator it = mData->genericProperties.find(aKey);
    if (it != mData->genericProperties.end())
        strOldValue = it->second;

    if (strOldValue != aValue)
    {
        if (aValue.isEmpty())
            mData->genericProperties.erase(aKey);
        else
            mData->genericProperties[aKey] = aValue;

        /* leave the lock before informing callbacks */
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /* Avoid deadlock when the event triggers a call to a method of this
         * interface. */
        adep.release();

        mParent->i_onNetworkAdapterChange(this, fGenericChange);
    }

    return S_OK;
}

HRESULT NetworkAdapter::getProperties(const com::Utf8Str &aNames,
                                      std::vector<com::Utf8Str>  &aReturnNames,
                                      std::vector<com::Utf8Str>  &aReturnValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /// @todo make use of aNames according to the documentation
    NOREF(aNames);
    aReturnNames.resize(mData->genericProperties.size());
    aReturnValues.resize(mData->genericProperties.size());

    size_t i = 0;

    for (settings::StringsMap::const_iterator it = mData->genericProperties.begin();
         it != mData->genericProperties.end();
         ++it, ++i)
    {
        aReturnNames[i] = it->first;
        aReturnValues[i] = it->second;
    }

    return S_OK;
}



// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given adapter node.
 *  May be called once right after this object creation.
 *
 *  @param bwctl bandwidth control object.
 *  @param data  Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT NetworkAdapter::i_loadSettings(BandwidthControl *bwctl,
                                       const settings::NetworkAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Note: we assume that the default values for attributes of optional
     * nodes are assigned in the Data::Data() constructor and don't do it
     * here. It implies that this method may only be called after constructing
     * a new FirmwareSettings object while all its data fields are in the default
     * values. Exceptions are fields whose creation time defaults don't match
     * values that should be applied when these fields are not explicitly set
     * in the settings file (for backwards compatibility reasons). This takes
     * place when a setting of a newly created object must default to A while
     * the same setting of an object loaded from the old settings file must
     * default to B. */

    /* MAC address (can be null) */
    HRESULT hrc = i_updateMacAddress(data.strMACAddress);
    if (FAILED(hrc)) return hrc;

    mData.assignCopy(&data);

    if (mData->strBandwidthGroup.isNotEmpty())
    {
        ComObjPtr<BandwidthGroup> group;
        hrc = bwctl->i_getBandwidthGroupByName(data.strBandwidthGroup, group, true);
        if (FAILED(hrc)) return hrc;
        group->i_reference();
    }

    // Load NAT engine settings.
    mNATEngine->i_loadSettings(data.nat);

    // leave the lock before setting attachment type
    alock.release();

    hrc = COMSETTER(AttachmentType)(data.mode);
    if (FAILED(hrc)) return hrc;

    return S_OK;
}

/**
 *  Saves settings to the given adapter node.
 *
 *  Note that the given Adapter node is completely empty on input.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT NetworkAdapter::i_saveSettings(settings::NetworkAdapter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *mData.data();

    mNATEngine->i_saveSettings(data.nat);

    return S_OK;
}

/**
 * Returns true if any setter method has modified settings of this instance.
 * @return
 */
bool NetworkAdapter::i_isModified()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bool fChanged = mData.isBackedUp();
    fChanged     |= mNATEngine->i_isModified();
    return fChanged;
}

/**
 *  @note Locks this object for writing.
 */
void NetworkAdapter::i_rollback()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mNATEngine->i_rollback();

    mData.rollback();
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void NetworkAdapter::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(mPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(mPeer, this COMMA_LOCKVAL_SRC_POS);

    mNATEngine->i_commit();

    if (mData.isBackedUp())
    {
        mData.commit();
        if (mPeer)
        {
            /* attach new data to the peer and reshare it */
            mPeer->mData.attach(mData);
        }
    }
}

/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void NetworkAdapter::i_copyFrom(NetworkAdapter *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    mNATEngine->i_copyFrom(aThat->mNATEngine);

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    mData.assignCopy(aThat->mData);

}

/**
 * Applies the defaults for this network adapter.
 *
 * @note This method currently assumes that the object is in the state after
 * calling init(), it does not set defaults from an arbitrary state.
 */
void NetworkAdapter::i_applyDefaults(GuestOSType *aOsType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    mNATEngine->i_applyDefaults();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bool e1000enabled = false;
#ifdef VBOX_WITH_E1000
    e1000enabled = true;
#endif // VBOX_WITH_E1000

    NetworkAdapterType_T defaultType;
    if (aOsType)
        defaultType = aOsType->i_networkAdapterType();
    else
    {
#ifdef VBOX_WITH_E1000
        defaultType = NetworkAdapterType_I82540EM;
#else
        defaultType = NetworkAdapterType_Am79C973A;
#endif
    }


    /* Set default network adapter for this OS type */
    if (defaultType == NetworkAdapterType_I82540EM ||
        defaultType == NetworkAdapterType_I82543GC ||
        defaultType == NetworkAdapterType_I82545EM)
    {
        if (e1000enabled)
            mData->type = defaultType;
    }
    else
        mData->type = defaultType;

    /* Enable the first one adapter and set it to NAT */
    /** @todo r=klaus remove this long term, since a newly created VM should
     * have no additional hardware components unless configured either
     * explicitly or through Machine::applyDefaults. */
    if (aOsType && mData->ulSlot == 0)
    {
        mData->fEnabled = true;
        if (mData->strMACAddress.isEmpty())
            i_generateMACAddress();
        mData->mode = NetworkAttachmentType_NAT;
    }
    mData->fCableConnected = true;
}

bool NetworkAdapter::i_hasDefaults()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), true);

    ComObjPtr<GuestOSType> pGuestOSType;
    HRESULT hrc = mParent->i_getVirtualBox()->i_findGuestOSType(mParent->i_getOSTypeId(), pGuestOSType);
    if (FAILED(hrc))
        return false;

    NetworkAdapterType_T defaultType = pGuestOSType->i_networkAdapterType();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   !mData->fEnabled
        && mData->strMACAddress.isEmpty()
        && mData->type == defaultType
        && mData->fCableConnected
        && mData->ulLineSpeed == 0
        && mData->enmPromiscModePolicy == NetworkAdapterPromiscModePolicy_Deny
        && mData->mode == NetworkAttachmentType_Null
        && mData->strBridgedName.isEmpty()
        && mData->strInternalNetworkName.isEmpty()
        && mData->strHostOnlyName.isEmpty()
        && mData->strNATNetworkName.isEmpty()
        && mData->strGenericDriver.isEmpty()
        && mData->genericProperties.size() == 0)
    {
        /* Could be default, check NAT defaults. */
        return mNATEngine->i_hasDefaults();
    }

    return false;
}

ComObjPtr<NetworkAdapter> NetworkAdapter::i_getPeer()
{
    return mPeer;
}


// private methods
////////////////////////////////////////////////////////////////////////////////

/**
 *  Generates a new unique MAC address based on our vendor ID and
 *  parts of a GUID.
 *
 *  @note Must be called from under the object's write lock or within the init
 *  span.
 */
void NetworkAdapter::i_generateMACAddress()
{
    Utf8Str mac;
    Host::i_generateMACAddress(mac);
    LogFlowThisFunc(("generated MAC: '%s'\n", mac.c_str()));
    mData->strMACAddress = mac;
}

HRESULT NetworkAdapter::getBandwidthGroup(ComPtr<IBandwidthGroup> &aBandwidthGroup)
{
    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->strBandwidthGroup.isNotEmpty())
    {
        ComObjPtr<BandwidthGroup> pBwGroup;
        hrc = mParent->i_getBandwidthGroup(mData->strBandwidthGroup, pBwGroup, true /* fSetError */);

        Assert(SUCCEEDED(hrc)); /* This is not allowed to fail because the existence
                                 * of the group was checked when it was attached. */
        if (SUCCEEDED(hrc))
            pBwGroup.queryInterfaceTo(aBandwidthGroup.asOutParam());
    }

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT NetworkAdapter::setBandwidthGroup(const ComPtr<IBandwidthGroup> &aBandwidthGroup)
{
    LogFlowThisFuncEnter();

    /* the machine needs to be mutable */
    AutoMutableOrSavedStateDependency adep(mParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    IBandwidthGroup *iBw = aBandwidthGroup;
    Utf8Str strBwGroup;
    if (aBandwidthGroup)
        strBwGroup = static_cast<BandwidthGroup *>(iBw)->i_getName();

    if (mData->strBandwidthGroup != strBwGroup)
    {
        ComObjPtr<BandwidthGroup> pBwGroup;
        if (!strBwGroup.isEmpty())
        {
            HRESULT hrc = mParent->i_getBandwidthGroup(strBwGroup, pBwGroup, false /* fSetError */);
            NOREF(hrc);
            Assert(SUCCEEDED(hrc)); /* This is not allowed to fail because the existence
                                       of the group was checked when it was attached. */
        }

        i_updateBandwidthGroup(pBwGroup);

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
        mlock.release();

        /** @todo changeAdapter=???. */
        mParent->i_onNetworkAdapterChange(this, FALSE);
    }

    LogFlowThisFuncLeave();
    return S_OK;
}

void NetworkAdapter::i_updateBandwidthGroup(BandwidthGroup *aBwGroup)
{
    LogFlowThisFuncEnter();
    Assert(isWriteLockOnCurrentThread());

    ComObjPtr<BandwidthGroup> pOldBwGroup;
    if (!mData->strBandwidthGroup.isEmpty())
        {
            HRESULT hrc = mParent->i_getBandwidthGroup(mData->strBandwidthGroup, pOldBwGroup, false /* fSetError */);
            NOREF(hrc);
            Assert(SUCCEEDED(hrc)); /* This is not allowed to fail because the existence of
                                       the group was checked when it was attached. */
        }

    mData.backup();
    if (!pOldBwGroup.isNull())
    {
        pOldBwGroup->i_release();
        mData->strBandwidthGroup = Utf8Str::Empty;
    }

    if (aBwGroup)
    {
        mData->strBandwidthGroup = aBwGroup->i_getName();
        aBwGroup->i_reference();
    }

    LogFlowThisFuncLeave();
}


HRESULT NetworkAdapter::i_switchFromNatNetworking(const com::Utf8Str &networkName)
{
    HRESULT hrc;
    MachineState_T state;

    hrc = mParent->COMGETTER(State)(&state);
    if (FAILED(hrc))
        return hrc;

    if (   state == MachineState_Running
        || state == MachineState_Paused)
    {
        Bstr bstrName;
        hrc = mParent->COMGETTER(Name)(bstrName.asOutParam());
        LogRel(("VM '%ls' stops using NAT network '%s'\n", bstrName.raw(), networkName.c_str()));
        int natCount = mParent->i_getVirtualBox()->i_natNetworkRefDec(Bstr(networkName).raw());
        if (natCount == -1)
            return E_INVALIDARG; /* no such network */
    }

    return S_OK;
}


HRESULT NetworkAdapter::i_switchToNatNetworking(const com::Utf8Str &aNatNetworkName)
{
    HRESULT hrc;
    MachineState_T state;

    hrc = mParent->COMGETTER(State)(&state);
    if (FAILED(hrc))
        return hrc;

    if (   state == MachineState_Running
        || state == MachineState_Paused)
    {
        Bstr bstrName;
        hrc = mParent->COMGETTER(Name)(bstrName.asOutParam());
        LogRel(("VM '%ls' starts using NAT network '%s'\n", bstrName.raw(), aNatNetworkName.c_str()));
        int natCount = mParent->i_getVirtualBox()->i_natNetworkRefInc(Bstr(aNatNetworkName).raw());
        if (natCount == -1)
            return E_INVALIDARG; /* not found */
    }

    return S_OK;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
