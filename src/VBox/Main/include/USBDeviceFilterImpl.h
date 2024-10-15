/* $Id: USBDeviceFilterImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Declaration of USBDeviceFilter and HostUSBDeviceFilter.
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

#ifndef MAIN_INCLUDED_USBDeviceFilterImpl_h
#define MAIN_INCLUDED_USBDeviceFilterImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/settings.h>
#include "Matching.h"
#include <VBox/usbfilter.h>
#include "USBDeviceFilterWrap.h"

class USBDeviceFilters;
class Host;
namespace settings
{
    struct USBDeviceFilter;
}

// USBDeviceFilter
////////////////////////////////////////////////////////////////////////////////

class ATL_NO_VTABLE USBDeviceFilter :
    public USBDeviceFilterWrap
{
public:

    struct BackupableUSBDeviceFilterData
    {
        typedef matching::Matchable <matching::ParsedBoolFilter> BOOLFilter;

        BackupableUSBDeviceFilterData() : mId (NULL) {}
        BackupableUSBDeviceFilterData(const BackupableUSBDeviceFilterData &aThat) :
            mRemote(aThat.mRemote),  mId(aThat.mId)
        {
            mData.strName = aThat.mData.strName;
            mData.fActive = aThat.mData.fActive;
            mData.ulMaskedInterfaces = aThat.mData.ulMaskedInterfaces;
            USBFilterClone(&mUSBFilter, &aThat.mUSBFilter);
        }

        /** Remote or local matching criterion. */
        BOOLFilter mRemote;

        /** The filter data blob. */
        USBFILTER mUSBFilter;

        /** Arbitrary ID field (not used by the class itself) */
        void *mId;

        settings::USBDeviceFilter mData;
    };

    DECLARE_COMMON_CLASS_METHODS(USBDeviceFilter)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(USBDeviceFilters *aParent,
                 const settings::USBDeviceFilter &data);
    HRESULT init(USBDeviceFilters *aParent, IN_BSTR aName);
    HRESULT init(USBDeviceFilters *aParent, USBDeviceFilter *aThat,
                 bool aReshare = false);
    HRESULT initCopy(USBDeviceFilters *aParent, USBDeviceFilter *aThat);
    void uninit();

    // public methods only for internal purposes
    bool i_isModified();
    void i_rollback();
    void i_commit();

    void unshare();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)
    void *& i_getId() { return bd->mId; }
    const BackupableUSBDeviceFilterData& i_getData() { return *bd.data(); }
    ComObjPtr<USBDeviceFilter> i_peer() { return mPeer; }

    // tr() wants to belong to a class it seems, thus this one here.
    static HRESULT i_usbFilterFieldFromString(PUSBFILTER aFilter,
                                              USBFILTERIDX aIdx,
                                              const Utf8Str &aValue,
                                              Utf8Str &aErrStr);

    static const char* i_describeUSBFilterIdx(USBFILTERIDX aIdx);

private:

    // wrapped IUSBDeviceFilter properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT setName(const com::Utf8Str &aName);
    HRESULT getActive(BOOL *aActive);
    HRESULT setActive(BOOL aActive);
    HRESULT getVendorId(com::Utf8Str &aVendorId);
    HRESULT setVendorId(const com::Utf8Str &aVendorId);
    HRESULT getProductId(com::Utf8Str &aProductId);
    HRESULT setProductId(const com::Utf8Str &aProductId);
    HRESULT getRevision(com::Utf8Str &aRevision);
    HRESULT setRevision(const com::Utf8Str &aRevision);
    HRESULT getManufacturer(com::Utf8Str &aManufacturer);
    HRESULT setManufacturer(const com::Utf8Str &aManufacturer);
    HRESULT getProduct(com::Utf8Str &aProduct);
    HRESULT setProduct(const com::Utf8Str &aProduct);
    HRESULT getSerialNumber(com::Utf8Str &aSerialNumber);
    HRESULT setSerialNumber(const com::Utf8Str &aSerialNumber);
    HRESULT getPort(com::Utf8Str &aPort);
    HRESULT setPort(const com::Utf8Str &aPort);
    HRESULT getRemote(com::Utf8Str &aRemote);
    HRESULT setRemote(const com::Utf8Str &aRemote);
    HRESULT getMaskedInterfaces(ULONG *aMaskedInterfaces);
    HRESULT setMaskedInterfaces(ULONG aMaskedInterfaces);

    // wrapped IUSBDeviceFilter methods
    HRESULT i_usbFilterFieldGetter(USBFILTERIDX aIdx, com::Utf8Str &aStr);
    HRESULT i_usbFilterFieldSetter(USBFILTERIDX aIdx, const com::Utf8Str &strNew);

    USBDeviceFilters * const     mParent;
    USBDeviceFilter  * const     mPeer;

    Backupable<BackupableUSBDeviceFilterData> bd;

    bool m_fModified;

    /** Used externally to indicate this filter is in the list
        (not touched by the class itself except that in init()/uninit()) */
    bool mInList;

    friend class USBDeviceFilters;
};
#include "HostUSBDeviceFilterWrap.h"

// HostUSBDeviceFilter
////////////////////////////////////////////////////////////////////////////////

class ATL_NO_VTABLE HostUSBDeviceFilter :
    public HostUSBDeviceFilterWrap
{
public:

    struct BackupableUSBDeviceFilterData : public USBDeviceFilter::BackupableUSBDeviceFilterData
    {
        BackupableUSBDeviceFilterData() {}
    };

    DECLARE_COMMON_CLASS_METHODS (HostUSBDeviceFilter)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Host *aParent,
                 const settings::USBDeviceFilter &data);
    HRESULT init(Host *aParent, IN_BSTR aName);
    void uninit();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)
    void i_saveSettings(settings::USBDeviceFilter &data);

    void*& i_getId() { return bd.data()->mId; }

    const BackupableUSBDeviceFilterData& i_getData() { return *bd.data(); }

    // util::Lockable interface
    RWLockHandle *lockHandle() const;

private:

    // wrapped IHostUSBDeviceFilter properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT setName(const com::Utf8Str &aName);
    HRESULT getActive(BOOL *aActive);
    HRESULT setActive(BOOL aActive);
    HRESULT getVendorId(com::Utf8Str &aVendorId);
    HRESULT setVendorId(const com::Utf8Str &aVendorId);
    HRESULT getProductId(com::Utf8Str &aProductId);
    HRESULT setProductId(const com::Utf8Str &aProductId);
    HRESULT getRevision(com::Utf8Str &aRevision);
    HRESULT setRevision(const com::Utf8Str &aRevision);
    HRESULT getManufacturer(com::Utf8Str &aManufacturer);
    HRESULT setManufacturer(const com::Utf8Str &aManufacturer);
    HRESULT getProduct(com::Utf8Str &aProduct);
    HRESULT setProduct(const com::Utf8Str &aProduct);
    HRESULT getSerialNumber(com::Utf8Str &aSerialNumber);
    HRESULT setSerialNumber(const com::Utf8Str &aSerialNumber);
    HRESULT getPort(com::Utf8Str &aPort);
    HRESULT setPort(const com::Utf8Str &aPort);
    HRESULT getRemote(com::Utf8Str &aRemote);
    HRESULT setRemote(const com::Utf8Str &aRemote);
    HRESULT getMaskedInterfaces(ULONG *aMaskedInterfaces);
    HRESULT setMaskedInterfaces(ULONG aMaskedInterfaces);

    // wrapped IHostUSBDeviceFilter properties
    HRESULT getAction(USBDeviceFilterAction_T *aAction);
    HRESULT setAction(USBDeviceFilterAction_T aAction);

    HRESULT i_usbFilterFieldGetter(USBFILTERIDX aIdx, com::Utf8Str &aStr);
    HRESULT i_usbFilterFieldSetter(USBFILTERIDX aIdx, const com::Utf8Str &aStr);

    Host * const        mParent;

    Backupable<BackupableUSBDeviceFilterData>    bd;

    /** Used externally to indicate this filter is in the list
        (not touched by the class itself except that in init()/uninit()) */
    bool mInList;

    friend class Host;
};

#endif /* !MAIN_INCLUDED_USBDeviceFilterImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
