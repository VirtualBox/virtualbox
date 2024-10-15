/* $Id: USBProxyService.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service (base) class.
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_USBProxyService_h
#define MAIN_INCLUDED_USBProxyService_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/usb.h>
#include <VBox/usbfilter.h>

#include "VirtualBoxBase.h"
#include "VirtualBoxImpl.h"
#include "HostUSBDeviceImpl.h"
#include "USBProxyBackend.h"

class Host;

namespace settings
{
    struct USBDeviceSource;
    typedef std::list<USBDeviceSource> USBDeviceSourcesList;
}


/**
 * Base class for the USB Proxy service.
 */
class USBProxyService
    : public Lockable
{
public:
    DECLARE_TRANSLATE_METHODS(USBProxyService)

    USBProxyService(Host *aHost);
    virtual HRESULT init(void);
    virtual ~USBProxyService();

    /**
     * Override of the default locking class to be used for validating lock
     * order with the standard member lock handle.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        // the USB proxy service uses the Host object lock, so return the
        // same locking class as the host
        return LOCKCLASS_HOSTOBJECT;
    }

    void uninit(void);

    bool isActive(void);
    int getLastError(void);

    RWLockHandle *lockHandle() const;

    /** @name Interface for the USBController and the Host object.
     * @{ */
    void *insertFilter(PCUSBFILTER aFilter);
    void removeFilter(void *aId);
    /** @} */

    /** @name Host Interfaces
     * @{ */
    HRESULT getDeviceCollection(std::vector<ComPtr<IHostUSBDevice> > &aUSBDevices);
    HRESULT addUSBDeviceSource(const com::Utf8Str &aBackend, const com::Utf8Str &aId, const com::Utf8Str &aAddress,
                               const std::vector<com::Utf8Str> &aPropertyNames, const std::vector<com::Utf8Str> &aPropertyValues);
    HRESULT removeUSBDeviceSource(const com::Utf8Str &aId);
    /** @} */

    /** @name SessionMachine Interfaces
     * @{ */
    HRESULT captureDeviceForVM(SessionMachine *aMachine, IN_GUID aId, const com::Utf8Str &aCaptureFilename);
    HRESULT detachDeviceFromVM(SessionMachine *aMachine, IN_GUID aId, bool aDone);
    HRESULT autoCaptureDevicesForVM(SessionMachine *aMachine);
    HRESULT detachAllDevicesFromVM(SessionMachine *aMachine, bool aDone, bool aAbnormal);
    /** @} */

    typedef std::list< ComObjPtr<HostUSBDeviceFilter> > USBDeviceFilterList;

    HRESULT i_loadSettings(const settings::USBDeviceSourcesList &llUSBDeviceSources);
    HRESULT i_saveSettings(settings::USBDeviceSourcesList &llUSBDeviceSources);

    void i_deviceAdded(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE aUSBDevice);
    void i_deviceRemoved(ComObjPtr<HostUSBDevice> &aDevice);
    void i_updateDeviceState(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE aUSBDevice, bool fFakeUpdate);

protected:
    ComObjPtr<HostUSBDevice> findDeviceById(IN_GUID aId);

    static HRESULT setError(HRESULT aResultCode, const char *aText, ...);

    USBProxyBackend *findUsbProxyBackendById(const com::Utf8Str &strId);

    HRESULT createUSBDeviceSource(const com::Utf8Str &aBackend, const com::Utf8Str &aId,
                                  const com::Utf8Str &aAddress, const std::vector<com::Utf8Str> &aPropertyNames,
                                  const std::vector<com::Utf8Str> &aPropertyValues, bool fLoadingSettings);

private:

    HRESULT runAllFiltersOnDevice(ComObjPtr<HostUSBDevice> &aDevice,
                                  SessionMachinesList &llOpenedMachines,
                                  SessionMachine *aIgnoreMachine);
    bool runMachineFilters(SessionMachine *aMachine, ComObjPtr<HostUSBDevice> &aDevice);

    void deviceChanged(ComObjPtr<HostUSBDevice> &aDevice, bool fRunFilters, SessionMachine *aIgnoreMachine);

    /** Pointer to the Host object. */
    Host *mHost;
    /** List of smart HostUSBDevice pointers. */
    typedef std::list<ComObjPtr<HostUSBDevice> > HostUSBDeviceList;
    /** List of the known USB devices. */
    HostUSBDeviceList mDevices;
    /** List of USBProxyBackend pointers. */
    typedef std::list<ComObjPtr<USBProxyBackend> > USBProxyBackendList;
    /** List of active USB backends. */
    USBProxyBackendList mBackends;
    int                 mLastError;
};

#endif /* !MAIN_INCLUDED_USBProxyService_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
