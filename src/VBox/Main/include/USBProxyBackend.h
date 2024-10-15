/* $Id: USBProxyBackend.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Backend (base) class.
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

#ifndef MAIN_INCLUDED_USBProxyBackend_h
#define MAIN_INCLUDED_USBProxyBackend_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/usb.h>
#include <VBox/usbfilter.h>

#include <iprt/socket.h>
#include <iprt/poll.h>
#include <iprt/semaphore.h>
#include <iprt/cpp/utils.h>

#include "VirtualBoxBase.h"
#include "VirtualBoxImpl.h"
#include "HostUSBDeviceImpl.h"
#include "USBProxyBackendWrap.h"
class USBProxyService;

/**
 * Base class for the USB Proxy Backend.
 */
class ATL_NO_VTABLE USBProxyBackend
    : public USBProxyBackendWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(USBProxyBackend)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    virtual int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
                     const com::Utf8Str &strAddress, bool fLoadingSettings);
    virtual void uninit();

    bool isActive(void);
    const com::Utf8Str &i_getId();
    const com::Utf8Str &i_getAddress();
    virtual const com::Utf8Str &i_getBackend();
    uint32_t i_getRefCount();

    virtual bool i_isDevReEnumerationRequired();

    /** @name Interface for the USBController and the Host object.
     * @{ */
    virtual void *insertFilter(PCUSBFILTER aFilter);
    virtual void removeFilter(void *aId);
    /** @} */

    /** @name Interfaces for the HostUSBDevice
     * @{ */
    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual void captureDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess);
    virtual int releaseDevice(HostUSBDevice *aDevice);
    virtual void releaseDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess);
    /** @} */

    static void freeDevice(PUSBDEVICE pDevice);

protected:
    int start(void);
    int stop(void);
    virtual void serviceThreadInit(void);
    virtual void serviceThreadTerm(void);

    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);
    uint32_t incRef();
    uint32_t decRef();

    static HRESULT setError(HRESULT aResultCode, const char *aText, ...);

    static void initFilterFromDevice(PUSBFILTER aFilter, HostUSBDevice *aDevice);
    static void freeDeviceMembers(PUSBDEVICE pDevice);

    /**
     * Backend specific callback when a device was added.
     * (Currently only Linux uses it to adjust the udev polling).
     */
    virtual void deviceAdded(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE pDev);
    virtual bool isFakeUpdateRequired();

private:

    // wrapped IUSBProxyBackend properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getType(com::Utf8Str &aType);

    static DECLCALLBACK(int) serviceThread(RTTHREAD Thread, void *pvUser);

    void updateDeviceList(PUSBDEVICE pDevices);

protected:
    /** Pointer to the owning USB Proxy Service object. */
    USBProxyService   *m_pUsbProxyService;
    /** Thread handle of the service thread. */
    RTTHREAD           mThread;
    /** Flag which stop() sets to cause serviceThread to return. */
    bool volatile      mTerminate;
    /** Id of the instance. */
    const com::Utf8Str m_strId;
    /** Address of the instance. */
    const com::Utf8Str m_strAddress;
    /** Backend identifier as used in the settings. */
    const com::Utf8Str m_strBackend;
    /** Reference counter which prevents the backend instance from being removed. */
    uint32_t           m_cRefs;
    /** List of smart HostUSBDevice pointers. */
    typedef std::list<ComObjPtr<HostUSBDevice> > HostUSBDeviceList;
    /** List of the known USB devices for this backend. */
    HostUSBDeviceList  m_llDevices;
};


# if defined(RT_OS_DARWIN) || defined(DOXYGEN_RUNNING)
#  include <VBox/param.h>
#  undef PAGE_SHIFT
#  undef PAGE_SIZE
#  define OSType Carbon_OSType
#  include <Carbon/Carbon.h>
#  undef OSType
#  undef PVM

/**
 * The Darwin hosted USB Proxy Backend.
 */
class USBProxyBackendDarwin : public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendDarwin)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

protected:
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait (void);
    virtual PUSBDEVICE getDevices (void);
    virtual void serviceThreadInit (void);
    virtual void serviceThreadTerm (void);
    virtual bool isFakeUpdateRequired();

private:
    /** Reference to the runloop of the service thread.
     * This is NULL if the service thread isn't running. */
    CFRunLoopRef mServiceRunLoopRef;
    /** The opaque value returned by DarwinSubscribeUSBNotifications. */
    void *mNotifyOpaque;
    /** A hack to work around the problem with the usb device enumeration
     * not including newly attached devices. */
    bool mWaitABitNextTime;
};
# endif /* RT_OS_DARWIN */


# if defined(RT_OS_LINUX) || defined(DOXYGEN_RUNNING)
#  include <stdio.h>
#  ifdef VBOX_USB_WITH_SYSFS
#   include <HostHardwareLinux.h>
#  endif

/**
 * The Linux hosted USB Proxy Backend.
 */
class USBProxyBackendLinux: public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendLinux)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

protected:
    int initUsbfs(void);
    int initSysfs(void);
    void doUsbfsCleanupAsNeeded(void);
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);
    virtual void deviceAdded(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE aUSBDevice);
    virtual bool isFakeUpdateRequired();

private:
    int waitUsbfs(RTMSINTERVAL aMillies);
    int waitSysfs(RTMSINTERVAL aMillies);

private:
    /** File handle to the '/proc/bus/usb/devices' file. */
    RTFILE mhFile;
    /** Pipe used to interrupt wait(), the read end. */
    RTPIPE mhWakeupPipeR;
    /** Pipe used to interrupt wait(), the write end. */
    RTPIPE mhWakeupPipeW;
    /** The root of usbfs. */
    Utf8Str mDevicesRoot;
    /** Whether we're using \<mUsbfsRoot\>/devices or /sys/whatever. */
    bool mUsingUsbfsDevices;
    /** Number of 500ms polls left to do. See usbDeterminState for details. */
    unsigned mUdevPolls;
#  ifdef VBOX_USB_WITH_SYSFS
    /** Object used for polling for hotplug events from hal. */
    VBoxMainHotplugWaiter *mpWaiter;
#  endif
};
# endif /* RT_OS_LINUX */


# if defined(RT_OS_OS2) || defined(DOXYGEN_RUNNING)
#  include <usbcalls.h>

/**
 * The Linux hosted USB Proxy Backend.
 */
class USBProxyBackendOs2 : public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendOs2)

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

protected:
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);
    int addDeviceToChain(PUSBDEVICE pDev, PUSBDEVICE *ppFirst, PUSBDEVICE **pppNext, int rc);

private:
    /** The notification event semaphore */
    HEV mhev;
    /** The notification id. */
    USBNOTIFY mNotifyId;
    /** The usbcalls.dll handle. */
    HMODULE mhmod;
    /** UsbRegisterChangeNotification */
    APIRET (APIENTRY *mpfnUsbRegisterChangeNotification)(PUSBNOTIFY, HEV, HEV);
    /** UsbDeregisterNotification */
    APIRET (APIENTRY *mpfnUsbDeregisterNotification)(USBNOTIFY);
    /** UsbQueryNumberDevices */
    APIRET (APIENTRY *mpfnUsbQueryNumberDevices)(PULONG);
    /** UsbQueryDeviceReport */
    APIRET (APIENTRY *mpfnUsbQueryDeviceReport)(ULONG, PULONG, PVOID);
};
# endif /* RT_OS_OS2 */


# if defined(RT_OS_SOLARIS) || defined(DOXYGEN_RUNNING)
#  include <libdevinfo.h>

/**
 * The Solaris hosted USB Proxy Backend.
 */
class USBProxyBackendSolaris : public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendSolaris)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual void *insertFilter (PCUSBFILTER aFilter);
    virtual void removeFilter (void *aID);

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);
    virtual void captureDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess);
    virtual void releaseDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess);

    virtual bool i_isDevReEnumerationRequired();

protected:
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);

private:
    RTSEMEVENT mNotifyEventSem;
    /** Whether we've successfully initialized the USBLib and should call USBLibTerm in the destructor. */
    bool mUSBLibInitialized;
};
#endif  /* RT_OS_SOLARIS */


# if defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
/**
 * The Windows hosted USB Proxy Backend.
 */
class USBProxyBackendWindows : public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendWindows)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual void *insertFilter (PCUSBFILTER aFilter);
    virtual void removeFilter (void *aID);

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

    virtual bool i_isDevReEnumerationRequired();

protected:
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);

private:

    HANDLE mhEventInterrupt;
};
# endif /* RT_OS_WINDOWS */

# if defined(RT_OS_FREEBSD) || defined(DOXYGEN_RUNNING)
/**
 * The FreeBSD hosted USB Proxy Backend.
 */
class USBProxyBackendFreeBSD : public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendFreeBSD)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

protected:
    int initUsbfs(void);
    int initSysfs(void);
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);
    int addDeviceToChain(PUSBDEVICE pDev, PUSBDEVICE *ppFirst, PUSBDEVICE **pppNext, int rc);
    virtual bool isFakeUpdateRequired();

private:
    RTSEMEVENT mNotifyEventSem;
};
# endif /* RT_OS_FREEBSD */

/**
 * USB/IP Proxy receive state.
 */
typedef enum USBIPRECVSTATE
{
    /** Invalid state. */
    kUsbIpRecvState_Invalid = 0,
    /** There is no request waiting for an answer. */
    kUsbIpRecvState_None,
    /** Waiting for the complete reception of UsbIpRetDevList. */
    kUsbIpRecvState_Hdr,
    /** Waiting for the complete reception of a UsbIpExportedDevice structure. */
    kUsbIpRecvState_ExportedDevice,
    /** Waiting for a complete reception of a UsbIpDeviceInterface structure to skip. */
    kUsbIpRecvState_DeviceInterface,
    /** 32bit hack. */
    kUsbIpRecvState_32Bit_Hack = 0x7fffffff
} USBIPRECVSTATE;
/** Pointer to a USB/IP receive state enum. */
typedef USBIPRECVSTATE *PUSBIPRECVSTATE;

struct UsbIpExportedDevice;

/**
 * The USB/IP Proxy Backend.
 */
class USBProxyBackendUsbIp: public USBProxyBackend
{
public:
    DECLARE_COMMON_CLASS_METHODS(USBProxyBackendUsbIp)

    int init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
             const com::Utf8Str &strAddress, bool fLoadingSettings);
    void uninit();

    virtual int captureDevice(HostUSBDevice *aDevice);
    virtual int releaseDevice(HostUSBDevice *aDevice);

protected:
    virtual int wait(RTMSINTERVAL aMillies);
    virtual int interruptWait(void);
    virtual PUSBDEVICE getDevices(void);
    virtual bool isFakeUpdateRequired();

private:
    int  updateDeviceList(bool *pfDeviceListChanged);
    bool hasDevListChanged(PUSBDEVICE pDevices);
    void freeDeviceList(PUSBDEVICE pHead);
    void resetRecvState();
    int  reconnect();
    void disconnect();
    int  startListExportedDevicesReq();
    void advanceState(USBIPRECVSTATE enmRecvState);
    int  receiveData();
    int  processData();
    int  addDeviceToList(UsbIpExportedDevice *pDev);

    struct Data;            // opaque data struct, defined in USBProxyBackendUsbIp.cpp
    Data *m;
};

#endif /* !MAIN_INCLUDED_USBProxyBackend_h */

