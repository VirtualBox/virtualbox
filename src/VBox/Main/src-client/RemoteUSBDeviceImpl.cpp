/* $Id: RemoteUSBDeviceImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox IHostUSBDevice COM interface implementation for remote (VRDP) USB devices.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTUSBDEVICE
#include "LoggingNew.h"

#include "RemoteUSBDeviceImpl.h"

#include "AutoCaller.h"

#include <iprt/cpp/utils.h>

#include <iprt/errcore.h>

#include <VBox/RemoteDesktop/VRDE.h>
#include <VBox/vrdpusb.h>


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(RemoteUSBDevice)

HRESULT RemoteUSBDevice::FinalConstruct()
{
    return BaseFinalConstruct();
}

void RemoteUSBDevice::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/** @todo (sunlover) REMOTE_USB Device states. */

/**
 * Initializes the remote USB device object.
 */
HRESULT RemoteUSBDevice::init(uint32_t u32ClientId, VRDEUSBDEVICEDESC const *pDevDesc, bool fDescExt)
{
    LogFlowThisFunc(("u32ClientId=%d,pDevDesc=%p\n", u32ClientId, pDevDesc));

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mData.id).create();

    unconst(mData.vendorId)     = pDevDesc->idVendor;
    unconst(mData.productId)    = pDevDesc->idProduct;
    unconst(mData.revision)     = pDevDesc->bcdRev;

    unconst(mData.manufacturer) = pDevDesc->oManufacturer ? (char *)pDevDesc + pDevDesc->oManufacturer : "";
    unconst(mData.product)      = pDevDesc->oProduct ? (char *)pDevDesc + pDevDesc->oProduct : "";
    unconst(mData.serialNumber) = pDevDesc->oSerialNumber ? (char *)pDevDesc + pDevDesc->oSerialNumber : "";

    char id[64];
    RTStrPrintf(id, sizeof(id), REMOTE_USB_BACKEND_PREFIX_S "0x%08X&0x%08X", pDevDesc->id, u32ClientId);
    unconst(mData.address)      = id;
    unconst(mData.backend)      = "vrdp";

    char port[16];
    RTStrPrintf(port, sizeof(port), "%u", pDevDesc->idPort);
    unconst(mData.portPath)     = port;

    unconst(mData.port)         = pDevDesc->idPort;
    unconst(mData.version)      = (uint16_t)(pDevDesc->bcdUSB >> 8);
    if (fDescExt)
    {
        VRDEUSBDEVICEDESCEXT *pDevDescExt = (VRDEUSBDEVICEDESCEXT *)pDevDesc;
        switch (pDevDescExt->u16DeviceSpeed)
        {
            default:
            case VRDE_USBDEVICESPEED_UNKNOWN:
            case VRDE_USBDEVICESPEED_LOW:
            case VRDE_USBDEVICESPEED_FULL:
                unconst(mData.speed) = USBConnectionSpeed_Full;
                break;

            case VRDE_USBDEVICESPEED_HIGH:
            case VRDE_USBDEVICESPEED_VARIABLE:
                unconst(mData.speed) = USBConnectionSpeed_High;
                break;

            case VRDE_USBDEVICESPEED_SUPERSPEED:
                unconst(mData.speed) = USBConnectionSpeed_Super;
                break;
        }
    }
    else
    {
        unconst(mData.speed) = mData.version == 3 ? USBConnectionSpeed_Super
                             : mData.version == 2 ? USBConnectionSpeed_High
                             :                      USBConnectionSpeed_Full;
    }

    mData.state                  = USBDeviceState_Available;

    mData.dirty                  = false;
    unconst(mData.devId)        = (uint16_t)pDevDesc->id;

    unconst(mData.clientId)     = u32ClientId;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void RemoteUSBDevice::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mData.id).clear();

    unconst(mData.vendorId) = 0;
    unconst(mData.productId) = 0;
    unconst(mData.revision) = 0;

    unconst(mData.manufacturer).setNull();
    unconst(mData.product).setNull();
    unconst(mData.serialNumber).setNull();

    unconst(mData.address).setNull();
    unconst(mData.backend).setNull();

    unconst(mData.port) = 0;
    unconst(mData.portPath).setNull();
    unconst(mData.version) = 1;

    unconst(mData.dirty) = FALSE;

    unconst(mData.devId) = 0;
    unconst(mData.clientId) = 0;
}

// IUSBDevice properties
/////////////////////////////////////////////////////////////////////////////

HRESULT RemoteUSBDevice::getId(com::Guid &aId)
{
    aId = mData.id;

    return S_OK;
}

HRESULT RemoteUSBDevice::getVendorId(USHORT *aVendorId)
{
    /* this is const, no need to lock */
    *aVendorId = mData.vendorId;

    return S_OK;
}

HRESULT RemoteUSBDevice::getProductId(USHORT *aProductId)
{
    /* this is const, no need to lock */
    *aProductId = mData.productId;

    return S_OK;
}

HRESULT RemoteUSBDevice::getRevision(USHORT *aRevision)
{
    /* this is const, no need to lock */
    *aRevision = mData.revision;

    return S_OK;
}

HRESULT RemoteUSBDevice::getManufacturer(com::Utf8Str &aManufacturer)
{
    /* this is const, no need to lock */
    aManufacturer = mData.manufacturer;

    return S_OK;
}

HRESULT RemoteUSBDevice::getProduct(com::Utf8Str &aProduct)
{
    /* this is const, no need to lock */
    aProduct = mData.product;

    return S_OK;
}

HRESULT RemoteUSBDevice::getSerialNumber(com::Utf8Str &aSerialNumber)
{
    /* this is const, no need to lock */
    aSerialNumber = mData.serialNumber;

    return S_OK;
}

HRESULT RemoteUSBDevice::getAddress(com::Utf8Str &aAddress)
{
    /* this is const, no need to lock */
    aAddress = mData.address;

    return S_OK;
}

HRESULT RemoteUSBDevice::getPort(USHORT *aPort)
{
    /* this is const, no need to lock */
    *aPort = mData.port;

    return S_OK;
}

HRESULT RemoteUSBDevice::getPortPath(com::Utf8Str &aPortPath)
{
    /* this is const, no need to lock */
    aPortPath = mData.portPath;

    return S_OK;
}

HRESULT RemoteUSBDevice::getVersion(USHORT *aVersion)
{
    /* this is const, no need to lock */
    *aVersion = mData.version;

    return S_OK;
}

HRESULT RemoteUSBDevice::getSpeed(USBConnectionSpeed_T *aSpeed)
{
    /* this is const, no need to lock */
    *aSpeed = mData.speed;

    return S_OK;
}

HRESULT RemoteUSBDevice::getRemote(BOOL *aRemote)
{
    /* RemoteUSBDevice is always remote. */
    /* this is const, no need to lock */
    *aRemote = TRUE;

    return S_OK;
}

HRESULT RemoteUSBDevice::getBackend(com::Utf8Str &aBackend)
{
    /* this is const, no need to lock */
    aBackend = mData.backend;

    return S_OK;
}

HRESULT RemoteUSBDevice::getDeviceInfo(std::vector<com::Utf8Str> &aInfo)
{
    /* this is const, no need to lock */
    aInfo.resize(2);
    aInfo[0] = mData.manufacturer;
    aInfo[1] = mData.product;

    return S_OK;
}

// IHostUSBDevice properties
////////////////////////////////////////////////////////////////////////////////

HRESULT RemoteUSBDevice::getState(USBDeviceState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aState = mData.state;

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
