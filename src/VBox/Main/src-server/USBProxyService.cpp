/* $Id: USBProxyService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service (base) class.
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

#define LOG_GROUP LOG_GROUP_MAIN_USBPROXYBACKEND
#include "USBProxyService.h"
#include "HostUSBDeviceImpl.h"
#include "HostImpl.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"

#include <VBox/com/array.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/string.h>

/** Pair of a USB proxy backend and the opaque filter data assigned by the backend. */
typedef std::pair<ComObjPtr<USBProxyBackend> , void *> USBFilterPair;
/** List of USB filter pairs. */
typedef std::list<USBFilterPair> USBFilterList;

/**
 * Data for a USB device filter.
 */
struct USBFilterData
{
    USBFilterData()
        : llUsbFilters()
    { }

    USBFilterList llUsbFilters;
};

/**
 * Initialize data members.
 */
USBProxyService::USBProxyService(Host *aHost)
    : mHost(aHost), mDevices(), mBackends()
{
    LogFlowThisFunc(("aHost=%p\n", aHost));
}


/**
 * Stub needed as long as the class isn't virtual
 */
HRESULT USBProxyService::init(void)
{
# if defined(RT_OS_DARWIN)
    ComObjPtr<USBProxyBackendDarwin> UsbProxyBackendHost;
# elif defined(RT_OS_LINUX)
    ComObjPtr<USBProxyBackendLinux> UsbProxyBackendHost;
# elif defined(RT_OS_OS2)
    ComObjPtr<USBProxyBackendOs2> UsbProxyBackendHost;
# elif defined(RT_OS_SOLARIS)
    ComObjPtr<USBProxyBackendSolaris> UsbProxyBackendHost;
# elif defined(RT_OS_WINDOWS)
    ComObjPtr<USBProxyBackendWindows> UsbProxyBackendHost;
# elif defined(RT_OS_FREEBSD)
    ComObjPtr<USBProxyBackendFreeBSD> UsbProxyBackendHost;
# else
    ComObjPtr<USBProxyBackend> UsbProxyBackendHost;
# endif
    UsbProxyBackendHost.createObject();
    int vrc = UsbProxyBackendHost->init(this, Utf8Str("host"), Utf8Str(""), false /* fLoadingSettings */);
    if (RT_FAILURE(vrc))
    {
        mLastError = vrc;
    }
    else
        mBackends.push_back(static_cast<ComObjPtr<USBProxyBackend> >(UsbProxyBackendHost));

    return S_OK;
}


/**
 * Empty destructor.
 */
USBProxyService::~USBProxyService()
{
    LogFlowThisFunc(("\n"));
    while (!mBackends.empty())
        mBackends.pop_front();

    mDevices.clear();
    mBackends.clear();
    mHost = NULL;
}


/**
 * Query if the service is active and working.
 *
 * @returns true if the service is up running.
 * @returns false if the service isn't running.
 */
bool USBProxyService::isActive(void)
{
    return mBackends.size() > 0;
}


/**
 * Get last error.
 * Can be used to check why the proxy !isActive() upon construction.
 *
 * @returns VBox status code.
 */
int USBProxyService::getLastError(void)
{
    return mLastError;
}


/**
 * We're using the Host object lock.
 *
 * This is just a temporary measure until all the USB refactoring is
 * done, probably... For now it help avoiding deadlocks we don't have
 * time to fix.
 *
 * @returns Lock handle.
 */
RWLockHandle *USBProxyService::lockHandle() const
{
    return mHost->lockHandle();
}


void *USBProxyService::insertFilter(PCUSBFILTER aFilter)
{
    USBFilterData *pFilterData = new USBFilterData();

    for (USBProxyBackendList::iterator it = mBackends.begin();
         it != mBackends.end();
         ++it)
    {
        ComObjPtr<USBProxyBackend> pUsbProxyBackend = *it;
        void *pvId = pUsbProxyBackend->insertFilter(aFilter);

        pFilterData->llUsbFilters.push_back(USBFilterPair(pUsbProxyBackend, pvId));
    }

    return pFilterData;
}

void USBProxyService::removeFilter(void *aId)
{
    USBFilterData *pFilterData = (USBFilterData *)aId;

    for (USBFilterList::iterator it = pFilterData->llUsbFilters.begin();
         it != pFilterData->llUsbFilters.end();
         ++it)
    {
        ComObjPtr<USBProxyBackend> pUsbProxyBackend = it->first;
        pUsbProxyBackend->removeFilter(it->second);
    }

    pFilterData->llUsbFilters.clear();
    delete pFilterData;
}

/**
 * Gets the collection of USB devices, slave of Host::USBDevices.
 *
 * This is an interface for the HostImpl::USBDevices property getter.
 *
 *
 * @param   aUSBDevices     Where to store the pointer to the collection.
 *
 * @returns COM status code.
 *
 * @remarks The caller must own the write lock of the host object.
 */
HRESULT USBProxyService::getDeviceCollection(std::vector<ComPtr<IHostUSBDevice> > &aUSBDevices)
{
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    aUSBDevices.resize(mDevices.size());
    size_t i = 0;
    for (HostUSBDeviceList::const_iterator it = mDevices.begin(); it != mDevices.end(); ++it, ++i)
        aUSBDevices[i] = *it;

    return S_OK;
}


HRESULT USBProxyService::addUSBDeviceSource(const com::Utf8Str &aBackend, const com::Utf8Str &aId, const com::Utf8Str &aAddress,
                                            const std::vector<com::Utf8Str> &aPropertyNames, const std::vector<com::Utf8Str> &aPropertyValues)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = createUSBDeviceSource(aBackend, aId, aAddress, aPropertyNames,
                                        aPropertyValues, false /* fLoadingSettings */);
    if (SUCCEEDED(hrc))
    {
        alock.release();
        AutoWriteLock vboxLock(mHost->i_parent() COMMA_LOCKVAL_SRC_POS);
        return mHost->i_parent()->i_saveSettings();
    }

    return hrc;
}

HRESULT USBProxyService::removeUSBDeviceSource(const com::Utf8Str &aId)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (USBProxyBackendList::iterator it = mBackends.begin();
         it != mBackends.end();
         ++it)
    {
        ComObjPtr<USBProxyBackend> UsbProxyBackend = *it;

        if (aId.equals(UsbProxyBackend->i_getId()))
        {
            mBackends.erase(it);

            /*
             * The proxy backend uninit method will be called when the pointer goes
             * out of scope.
             */

            alock.release();
            AutoWriteLock vboxLock(mHost->i_parent() COMMA_LOCKVAL_SRC_POS);
            return mHost->i_parent()->i_saveSettings();
        }
    }

    return setError(VBOX_E_OBJECT_NOT_FOUND,
                    tr("The USB device source \"%s\" could not be found"), aId.c_str());
}

/**
 * Request capture of a specific device.
 *
 * This is in an interface for SessionMachine::CaptureUSBDevice(), which is
 * an internal worker used by Console::AttachUSBDevice() from the VM process.
 *
 * When the request is completed, SessionMachine::onUSBDeviceAttach() will
 * be called for the given machine object.
 *
 *
 * @param   aMachine        The machine to attach the device to.
 * @param   aId             The UUID of the USB device to capture and attach.
 * @param   aCaptureFilename
 *
 * @returns COM status code and error info.
 *
 * @remarks This method may operate synchronously as well as asynchronously. In the
 *          former case it will temporarily abandon locks because of IPC.
 */
HRESULT USBProxyService::captureDeviceForVM(SessionMachine *aMachine, IN_GUID aId, const com::Utf8Str &aCaptureFilename)
{
    ComAssertRet(aMachine, E_INVALIDARG);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Translate the device id into a device object.
     */
    ComObjPtr<HostUSBDevice> pHostDevice = findDeviceById(aId);
    if (pHostDevice.isNull())
        return setError(E_INVALIDARG,
                        tr("The USB device with UUID {%RTuuid} is not currently attached to the host"), Guid(aId).raw());

    /*
     * Try to capture the device
     */
    alock.release();
    return pHostDevice->i_requestCaptureForVM(aMachine, true /* aSetError */, aCaptureFilename);
}


/**
 * Notification from VM process about USB device detaching progress.
 *
 * This is in an interface for SessionMachine::DetachUSBDevice(), which is
 * an internal worker used by Console::DetachUSBDevice() from the VM process.
 *
 * @param   aMachine        The machine which is sending the notification.
 * @param   aId             The UUID of the USB device is concerns.
 * @param   aDone           \a false for the pre-action notification (necessary
 *                          for advancing the device state to avoid confusing
 *                          the guest).
 *                          \a true for the post-action notification. The device
 *                          will be subjected to all filters except those of
 *                          of \a Machine.
 *
 * @returns COM status code.
 *
 * @remarks When \a aDone is \a true this method may end up doing IPC to other
 *          VMs when running filters. In these cases it will temporarily
 *          abandon its locks.
 */
HRESULT USBProxyService::detachDeviceFromVM(SessionMachine *aMachine, IN_GUID aId, bool aDone)
{
    LogFlowThisFunc(("aMachine=%p{%s} aId={%RTuuid} aDone=%RTbool\n",
                     aMachine,
                     aMachine->i_getName().c_str(),
                     Guid(aId).raw(),
                     aDone));

    // get a list of all running machines while we're outside the lock
    // (getOpenedMachines requests locks which are incompatible with the lock of the machines list)
    SessionMachinesList llOpenedMachines;
    mHost->i_parent()->i_getOpenedMachines(llOpenedMachines);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<HostUSBDevice> pHostDevice = findDeviceById(aId);
    ComAssertRet(!pHostDevice.isNull(), E_FAIL);
    AutoWriteLock devLock(pHostDevice COMMA_LOCKVAL_SRC_POS);

    /*
     * Work the state machine.
     */
    LogFlowThisFunc(("id={%RTuuid} state=%s aDone=%RTbool name={%s}\n",
                     pHostDevice->i_getId().raw(), pHostDevice->i_getStateName(), aDone, pHostDevice->i_getName().c_str()));
    bool fRunFilters = false;
    HRESULT hrc = pHostDevice->i_onDetachFromVM(aMachine, aDone, &fRunFilters);

    /*
     * Run filters if necessary.
     */
    if (    SUCCEEDED(hrc)
        &&  fRunFilters)
    {
        Assert(aDone && pHostDevice->i_getUnistate() == kHostUSBDeviceState_HeldByProxy && pHostDevice->i_getMachine().isNull());
        devLock.release();
        alock.release();
        HRESULT hrc2 = runAllFiltersOnDevice(pHostDevice, llOpenedMachines, aMachine);
        ComAssertComRC(hrc2);
    }
    return hrc;
}


/**
 * Apply filters for the machine to all eligible USB devices.
 *
 * This is in an interface for SessionMachine::CaptureUSBDevice(), which
 * is an internal worker used by Console::AutoCaptureUSBDevices() from the
 * VM process at VM startup.
 *
 * Matching devices will be attached to the VM and may result IPC back
 * to the VM process via SessionMachine::onUSBDeviceAttach() depending
 * on whether the device needs to be captured or not. If capture is
 * required, SessionMachine::onUSBDeviceAttach() will be called
 * asynchronously by the USB proxy service thread.
 *
 * @param   aMachine        The machine to capture devices for.
 *
 * @returns COM status code, perhaps with error info.
 *
 * @remarks Temporarily locks this object, the machine object and some USB
 *          device, and the called methods will lock similar objects.
 */
HRESULT USBProxyService::autoCaptureDevicesForVM(SessionMachine *aMachine)
{
    LogFlowThisFunc(("aMachine=%p{%s}\n",
                     aMachine,
                     aMachine->i_getName().c_str()));

    /*
     * Make a copy of the list because we cannot hold the lock protecting it.
     * (This will not make copies of any HostUSBDevice objects, only reference them.)
     */
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HostUSBDeviceList ListCopy = mDevices;
    alock.release();

    for (HostUSBDeviceList::iterator it = ListCopy.begin();
         it != ListCopy.end();
         ++it)
    {
        ComObjPtr<HostUSBDevice> pHostDevice = *it;
        AutoReadLock devLock(pHostDevice COMMA_LOCKVAL_SRC_POS);
        if (   pHostDevice->i_getUnistate() == kHostUSBDeviceState_HeldByProxy
            || pHostDevice->i_getUnistate() == kHostUSBDeviceState_Unused
            || pHostDevice->i_getUnistate() == kHostUSBDeviceState_Capturable)
        {
            devLock.release();
            runMachineFilters(aMachine, pHostDevice);
        }
    }

    return S_OK;
}


/**
 * Detach all USB devices currently attached to a VM.
 *
 * This is in an interface for SessionMachine::DetachAllUSBDevices(), which
 * is an internal worker used by Console::powerDown() from the VM process
 * at VM startup, and SessionMachine::uninit() at VM abend.
 *
 * This is, like #detachDeviceFromVM(), normally a two stage journey
 * where \a aDone indicates where we are. In addition we may be called
 * to clean up VMs that have abended, in which case there will be no
 * preparatory call. Filters will be applied to the devices in the final
 * call with the risk that we have to do some IPC when attaching them
 * to other VMs.
 *
 * @param   aMachine        The machine to detach devices from.
 * @param   aDone
 * @param   aAbnormal
 *
 * @returns COM status code, perhaps with error info.
 *
 * @remarks Write locks the host object and may temporarily abandon
 *          its locks to perform IPC.
 */
HRESULT USBProxyService::detachAllDevicesFromVM(SessionMachine *aMachine, bool aDone, bool aAbnormal)
{
    // get a list of all running machines while we're outside the lock
    // (getOpenedMachines requests locks which are incompatible with the host object lock)
    SessionMachinesList llOpenedMachines;
    mHost->i_parent()->i_getOpenedMachines(llOpenedMachines);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Make a copy of the device list (not the HostUSBDevice objects, just
     * the list) since we may end up performing IPC and temporarily have
     * to abandon locks when applying filters.
     */
    HostUSBDeviceList ListCopy = mDevices;

    for (HostUSBDeviceList::iterator it = ListCopy.begin();
         it != ListCopy.end();
         ++it)
    {
        ComObjPtr<HostUSBDevice> pHostDevice = *it;
        AutoWriteLock devLock(pHostDevice COMMA_LOCKVAL_SRC_POS);
        if (pHostDevice->i_getMachine() == aMachine)
        {
            /*
             * Same procedure as in detachUSBDevice().
             */
            bool fRunFilters = false;
            HRESULT hrc = pHostDevice->i_onDetachFromVM(aMachine, aDone, &fRunFilters, aAbnormal);
            if (    SUCCEEDED(hrc)
                &&  fRunFilters)
            {
                Assert(   aDone
                       && pHostDevice->i_getUnistate() == kHostUSBDeviceState_HeldByProxy
                       && pHostDevice->i_getMachine().isNull());
                devLock.release();
                alock.release();
                HRESULT hrc2 = runAllFiltersOnDevice(pHostDevice, llOpenedMachines, aMachine);
                ComAssertComRC(hrc2);
                alock.acquire();
            }
        }
    }

    return S_OK;
}


// Internals
/////////////////////////////////////////////////////////////////////////////


/**
 * Loads the given settings and constructs the additional USB device sources.
 *
 * @returns COM status code.
 * @param   llUSBDeviceSources    The list of additional device sources.
 */
HRESULT USBProxyService::i_loadSettings(const settings::USBDeviceSourcesList &llUSBDeviceSources)
{
    HRESULT hrc = S_OK;

    for (settings::USBDeviceSourcesList::const_iterator it = llUSBDeviceSources.begin();
         it != llUSBDeviceSources.end() && SUCCEEDED(hrc);
         ++it)
    {
        std::vector<com::Utf8Str> vecPropNames, vecPropValues;
        const settings::USBDeviceSource &src = *it;
        hrc = createUSBDeviceSource(src.strBackend, src.strName, src.strAddress,
                                    vecPropNames, vecPropValues, true /* fLoadingSettings */);
    }

    return hrc;
}

/**
 * Saves the additional device sources in the given settings.
 *
 * @returns COM status code.
 * @param   llUSBDeviceSources    The list of additional device sources.
 */
HRESULT USBProxyService::i_saveSettings(settings::USBDeviceSourcesList &llUSBDeviceSources)
{
    for (USBProxyBackendList::iterator it = mBackends.begin();
         it != mBackends.end();
         ++it)
    {
        USBProxyBackend *pUsbProxyBackend = *it;

        /* Host backends are not saved as they are always created during startup. */
        if (!pUsbProxyBackend->i_getBackend().equals("host"))
        {
            settings::USBDeviceSource src;

            src.strBackend = pUsbProxyBackend->i_getBackend();
            src.strName    = pUsbProxyBackend->i_getId();
            src.strAddress = pUsbProxyBackend->i_getAddress();

            llUSBDeviceSources.push_back(src);
        }
    }

    return S_OK;
}

/**
 * Performs the required actions when a device has been added.
 *
 * This means things like running filters and subsequent capturing and
 * VM attaching. This may result in IPC and temporary lock abandonment.
 *
 * @param   aDevice     The device in question.
 * @param   pDev        The USB device structure.
 */
void USBProxyService::i_deviceAdded(ComObjPtr<HostUSBDevice> &aDevice,
                                    PUSBDEVICE pDev)
{
    /*
     * Validate preconditions.
     */
    AssertReturnVoid(!isWriteLockOnCurrentThread());
    AssertReturnVoid(!aDevice->isWriteLockOnCurrentThread());
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%p name={%s} state=%s id={%RTuuid}\n",
                     (HostUSBDevice *)aDevice,
                     aDevice->i_getName().c_str(),
                     aDevice->i_getStateName(),
                     aDevice->i_getId().raw()));

    /* Add to our list. */
    HostUSBDeviceList::iterator it = mDevices.begin();
    while (it != mDevices.end())
    {
        ComObjPtr<HostUSBDevice> pHostDevice = *it;

        /* Assert that the object is still alive. */
        AutoCaller devCaller(pHostDevice);
        AssertComRC(devCaller.hrc());

        AutoWriteLock curLock(pHostDevice COMMA_LOCKVAL_SRC_POS);
        if (   pHostDevice->i_getUsbProxyBackend() == aDevice->i_getUsbProxyBackend()
            && pHostDevice->i_compare(pDev) < 0)
            break;

        ++it;
    }

    mDevices.insert(it, aDevice);

    /*
     * Run filters on the device.
     */
    if (aDevice->i_isCapturableOrHeld())
    {
        devLock.release();
        alock.release();
        SessionMachinesList llOpenedMachines;
        mHost->i_parent()->i_getOpenedMachines(llOpenedMachines);
        HRESULT hrc = runAllFiltersOnDevice(aDevice, llOpenedMachines, NULL /* aIgnoreMachine */);
        AssertComRC(hrc);
    }
}

/**
 * Remove device notification hook for the USB proxy service.
 *
 * @param   aDevice     The device in question.
 */
void USBProxyService::i_deviceRemoved(ComObjPtr<HostUSBDevice> &aDevice)
{
    /*
     * Validate preconditions.
     */
    AssertReturnVoid(!isWriteLockOnCurrentThread());
    AssertReturnVoid(!aDevice->isWriteLockOnCurrentThread());
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%p name={%s} state=%s id={%RTuuid}\n",
                     (HostUSBDevice *)aDevice,
                     aDevice->i_getName().c_str(),
                     aDevice->i_getStateName(),
                     aDevice->i_getId().raw()));

    mDevices.remove(aDevice);

    /*
     * Detach the device from any machine currently using it,
     * reset all data and uninitialize the device object.
     */
    devLock.release();
    alock.release();
    aDevice->i_onPhysicalDetached();
}

/**
 * Updates the device state.
 *
 * This is responsible for calling HostUSBDevice::updateState().
 *
 * @param   aDevice         The device in question.
 * @param   aUSBDevice      The USB device structure for the last enumeration.
 * @param   fFakeUpdate     Flag whether to fake updating state.
 */
void USBProxyService::i_updateDeviceState(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE aUSBDevice, bool fFakeUpdate)
{
    AssertReturnVoid(aDevice);
    AssertReturnVoid(!aDevice->isWriteLockOnCurrentThread());

    bool fRunFilters = false;
    SessionMachine *pIgnoreMachine = NULL;
    bool fDevChanged = false;
    if (fFakeUpdate)
        fDevChanged = aDevice->i_updateStateFake(aUSBDevice, &fRunFilters, &pIgnoreMachine);
    else
        fDevChanged = aDevice->i_updateState(aUSBDevice, &fRunFilters, &pIgnoreMachine);

    if (fDevChanged)
        deviceChanged(aDevice, fRunFilters, pIgnoreMachine);
}


/**
 * Handle a device which state changed in some significant way.
 *
 * This means things like running filters and subsequent capturing and
 * VM attaching. This may result in IPC and temporary lock abandonment.
 *
 * @param   aDevice         The device.
 * @param   fRunFilters     Flag whether to run filters.
 * @param   aIgnoreMachine  Machine to ignore when running filters.
 */
void USBProxyService::deviceChanged(ComObjPtr<HostUSBDevice> &aDevice, bool fRunFilters,
                                    SessionMachine *aIgnoreMachine)
{
    /*
     * Validate preconditions.
     */
    AssertReturnVoid(!isWriteLockOnCurrentThread());
    AssertReturnVoid(!aDevice->isWriteLockOnCurrentThread());
    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%p name={%s} state=%s id={%RTuuid} aRunFilters=%RTbool aIgnoreMachine=%p\n",
                     (HostUSBDevice *)aDevice,
                     aDevice->i_getName().c_str(),
                     aDevice->i_getStateName(),
                     aDevice->i_getId().raw(),
                     fRunFilters,
                     aIgnoreMachine));
    devLock.release();

    /*
     * Run filters if requested to do so.
     */
    if (fRunFilters)
    {
        SessionMachinesList llOpenedMachines;
        mHost->i_parent()->i_getOpenedMachines(llOpenedMachines);
        HRESULT hrc = runAllFiltersOnDevice(aDevice, llOpenedMachines, aIgnoreMachine);
        AssertComRC(hrc);
    }
}


/**
 * Runs all the filters on the specified device.
 *
 * All filters mean global and active VM, with the exception of those
 * belonging to \a aMachine. If a global ignore filter matched or if
 * none of the filters matched, the device will be released back to
 * the host.
 *
 * The device calling us here will be in the HeldByProxy, Unused, or
 * Capturable state. The caller is aware that locks held might have
 * to be abandond because of IPC and that the device might be in
 * almost any state upon return.
 *
 *
 * @returns COM status code (only parameter & state checks will fail).
 * @param   aDevice         The USB device to apply filters to.
 * @param   llOpenedMachines The list of opened machines.
 * @param   aIgnoreMachine  The machine to ignore filters from (we've just
 *                          detached the device from this machine).
 *
 * @note    The caller is expected to own no locks.
 */
HRESULT USBProxyService::runAllFiltersOnDevice(ComObjPtr<HostUSBDevice> &aDevice,
                                               SessionMachinesList &llOpenedMachines,
                                               SessionMachine *aIgnoreMachine)
{
    LogFlowThisFunc(("{%s} ignoring=%p\n", aDevice->i_getName().c_str(), aIgnoreMachine));

    /*
     * Verify preconditions.
     */
    AssertReturn(!isWriteLockOnCurrentThread(), E_FAIL);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), E_FAIL);

    /*
     * Get the lists we'll iterate.
     */
    Host::USBDeviceFilterList globalFilters;
    mHost->i_getUSBFilters(&globalFilters);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    AssertMsgReturn(aDevice->i_isCapturableOrHeld(), ("{%s} %s\n", aDevice->i_getName().c_str(),
                                                      aDevice->i_getStateName()), E_FAIL);

    /*
     * Run global filters filters first.
     */
    bool fHoldIt = false;
    for (Host::USBDeviceFilterList::const_iterator it = globalFilters.begin();
         it != globalFilters.end();
         ++it)
    {
        AutoWriteLock filterLock(*it COMMA_LOCKVAL_SRC_POS);
        const HostUSBDeviceFilter::BackupableUSBDeviceFilterData &data = (*it)->i_getData();
        if (aDevice->i_isMatch(data))
        {
            USBDeviceFilterAction_T action = USBDeviceFilterAction_Null;
            (*it)->COMGETTER(Action)(&action);
            if (action == USBDeviceFilterAction_Ignore)
            {
                /*
                 * Release the device to the host and we're done.
                 */
                filterLock.release();
                devLock.release();
                alock.release();
                aDevice->i_requestReleaseToHost();
                return S_OK;
            }
            if (action == USBDeviceFilterAction_Hold)
            {
                /*
                 * A device held by the proxy needs to be subjected
                 * to the machine filters.
                 */
                fHoldIt = true;
                break;
            }
            AssertMsgFailed(("action=%d\n", action));
        }
    }
    globalFilters.clear();

    /*
     * Run the per-machine filters.
     */
    for (SessionMachinesList::const_iterator it = llOpenedMachines.begin();
         it != llOpenedMachines.end();
         ++it)
    {
        ComObjPtr<SessionMachine> pMachine = *it;

        /* Skip the machine the device was just detached from. */
        if (    aIgnoreMachine
            &&  pMachine == aIgnoreMachine)
            continue;

        /* runMachineFilters takes care of checking the machine state. */
        devLock.release();
        alock.release();
        if (runMachineFilters(pMachine, aDevice))
        {
            LogFlowThisFunc(("{%s} attached to %p\n", aDevice->i_getName().c_str(), (void *)pMachine));
            return S_OK;
        }
        alock.acquire();
        devLock.acquire();
    }

    /*
     * No matching machine, so request hold or release depending
     * on global filter match.
     */
    devLock.release();
    alock.release();
    if (fHoldIt)
        aDevice->i_requestHold();
    else
        aDevice->i_requestReleaseToHost();
    return S_OK;
}


/**
 * Runs the USB filters of the machine on the device.
 *
 * If a match is found we will request capture for VM. This may cause
 * us to temporary abandon locks while doing IPC.
 *
 * @param   aMachine    Machine whose filters are to be run.
 * @param   aDevice     The USB device in question.
 * @returns @c true if the device has been or is being attached to the VM, @c false otherwise.
 *
 * @note    Locks several objects temporarily for reading or writing.
 */
bool USBProxyService::runMachineFilters(SessionMachine *aMachine, ComObjPtr<HostUSBDevice> &aDevice)
{
    LogFlowThisFunc(("{%s} aMachine=%p \n", aDevice->i_getName().c_str(), aMachine));

    /*
     * Validate preconditions.
     */
    AssertReturn(aMachine, false);
    AssertReturn(!isWriteLockOnCurrentThread(), false);
    AssertReturn(!aMachine->isWriteLockOnCurrentThread(), false);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), false);
    /* Let HostUSBDevice::requestCaptureToVM() validate the state. */

    /*
     * Do the job.
     */
    ULONG ulMaskedIfs;
    if (aMachine->i_hasMatchingUSBFilter(aDevice, &ulMaskedIfs))
    {
        /* try to capture the device */
        HRESULT hrc = aDevice->i_requestCaptureForVM(aMachine, false /* aSetError */, Utf8Str(), ulMaskedIfs);
        return SUCCEEDED(hrc)
            || hrc == E_UNEXPECTED /* bad device state, give up */;
    }

    return false;
}


/**
 * Searches the list of devices (mDevices) for the given device.
 *
 *
 * @returns Smart pointer to the device on success, NULL otherwise.
 * @param   aId             The UUID of the device we're looking for.
 */
ComObjPtr<HostUSBDevice> USBProxyService::findDeviceById(IN_GUID aId)
{
    Guid Id(aId);
    ComObjPtr<HostUSBDevice> Dev;
    for (HostUSBDeviceList::iterator it = mDevices.begin();
         it != mDevices.end();
         ++it)
        if ((*it)->i_getId() == Id)
        {
            Dev = (*it);
            break;
        }

    return Dev;
}

/**
 * Creates a new USB device source.
 *
 * @returns COM status code.
 * @param   aBackend          The backend to use.
 * @param   aId               The ID of the source.
 * @param   aAddress          The backend specific address.
 * @param   aPropertyNames    Vector of optional property keys the backend supports.
 * @param   aPropertyValues   Vector of optional property values the backend supports.
 * @param   fLoadingSettings  Flag whether the USB device source is created while the
 *                            settings are loaded or through the Main API.
 */
HRESULT USBProxyService::createUSBDeviceSource(const com::Utf8Str &aBackend, const com::Utf8Str &aId,
                                               const com::Utf8Str &aAddress, const std::vector<com::Utf8Str> &aPropertyNames,
                                               const std::vector<com::Utf8Str> &aPropertyValues,
                                               bool fLoadingSettings)
{
    HRESULT hrc = S_OK;

    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

    /** @todo */
    NOREF(aPropertyNames);
    NOREF(aPropertyValues);

    /* Check whether the ID is used first. */
    for (USBProxyBackendList::iterator it = mBackends.begin();
         it != mBackends.end();
         ++it)
    {
        USBProxyBackend *pUsbProxyBackend = *it;

        if (aId.equals(pUsbProxyBackend->i_getId()))
            return setError(VBOX_E_OBJECT_IN_USE,
                            tr("The USB device source \"%s\" exists already"), aId.c_str());
    }

    /* Create appropriate proxy backend. */
    if (aBackend.equalsIgnoreCase("USBIP"))
    {
        ComObjPtr<USBProxyBackendUsbIp> UsbProxyBackend;

        UsbProxyBackend.createObject();
        int vrc = UsbProxyBackend->init(this, aId, aAddress, fLoadingSettings);
        if (RT_FAILURE(vrc))
            hrc = setError(E_FAIL,
                           tr("Creating the USB device source \"%s\" using backend \"%s\" failed with %Rrc"),
                           aId.c_str(), aBackend.c_str(), vrc);
        else
            mBackends.push_back(static_cast<ComObjPtr<USBProxyBackend> >(UsbProxyBackend));
    }
    else
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("The USB backend \"%s\" is not supported"), aBackend.c_str());

    return hrc;
}

/*static*/
HRESULT USBProxyService::setError(HRESULT aResultCode, const char *aText, ...)
{
    va_list va;
    va_start(va, aText);
    HRESULT hrc = VirtualBoxBase::setErrorInternalV(aResultCode,
                                                    COM_IIDOF(IHost),
                                                    "USBProxyService",
                                                    aText, va,
                                                    false /* aWarning*/,
                                                    true /* aLogIt*/);
    va_end(va);
    return hrc;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
