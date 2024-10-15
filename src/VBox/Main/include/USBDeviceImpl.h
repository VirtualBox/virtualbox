/* $Id: USBDeviceImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Header file for the OUSBDevice (IUSBDevice) class, VBoxC.
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

#ifndef MAIN_INCLUDED_USBDeviceImpl_h
#define MAIN_INCLUDED_USBDeviceImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "USBDeviceWrap.h"

/**
 * Object class used for maintaining devices attached to a USB controller.
 * Generally this contains much less information.
 */
class ATL_NO_VTABLE OUSBDevice :
    public USBDeviceWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(OUSBDevice)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(IUSBDevice *a_pUSBDevice);
    void uninit();

    // public methods only for internal purposes
    const Guid &i_id() const { return mData.id; }

private:

    // Wrapped IUSBDevice properties
    HRESULT getId(com::Guid &aId);
    HRESULT getVendorId(USHORT *aVendorId);
    HRESULT getProductId(USHORT *aProductId);
    HRESULT getRevision(USHORT *aRevision);
    HRESULT getManufacturer(com::Utf8Str &aManufacturer);
    HRESULT getProduct(com::Utf8Str &aProduct);
    HRESULT getSerialNumber(com::Utf8Str &aSerialNumber);
    HRESULT getAddress(com::Utf8Str &aAddress);
    HRESULT getPort(USHORT *aPort);
    HRESULT getPortPath(com::Utf8Str &aPortPath);
    HRESULT getVersion(USHORT *aVersion);
    HRESULT getSpeed(USBConnectionSpeed_T *aSpeed);
    HRESULT getRemote(BOOL *aRemote);
    HRESULT getBackend(com::Utf8Str &aBackend);
    HRESULT getDeviceInfo(std::vector<com::Utf8Str> &aInfo);

    struct Data
    {
        Data() : vendorId(0), productId(0), revision(0), port(0),
                 version(1), speed(USBConnectionSpeed_Null),
                 remote(FALSE) {}

        /** The UUID of this device. */
        const Guid id;

        /** The vendor id of this USB device. */
        const USHORT vendorId;
        /** The product id of this USB device. */
        const USHORT productId;
        /** The product revision number of this USB device.
         * (high byte = integer; low byte = decimal) */
        const USHORT revision;
        /** The Manufacturer string. (Quite possibly NULL.) */
        const com::Utf8Str manufacturer;
        /** The Product string. (Quite possibly NULL.) */
        const com::Utf8Str product;
        /** The SerialNumber string. (Quite possibly NULL.) */
        const com::Utf8Str serialNumber;
        /** The host specific address of the device. */
        const com::Utf8Str address;
        /** The device specific backend. */
        const com::Utf8Str backend;
        /** The host port number. */
        const USHORT port;
        /** The host port path. */
        const com::Utf8Str portPath;
        /** The major USB version number of the device. */
        const USHORT version;
        /** The speed at which the device is communicating. */
        const USBConnectionSpeed_T speed;
        /** Remote (VRDP) or local device. */
        const BOOL remote;
    };

    Data mData;
};

#endif /* !MAIN_INCLUDED_USBDeviceImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
