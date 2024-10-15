/* $Id: HostDriveImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - IHostDrive implementation, VBoxSVC.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_HostDriveImpl_h
#define MAIN_INCLUDED_HostDriveImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HostDriveWrap.h"

class ATL_NO_VTABLE HostDrive
    : public HostDriveWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(HostDrive)

    HRESULT FinalConstruct();
    void FinalRelease();

    /** @name Public initializer/uninitializer for internal purposes only.
     * @{ */
    HRESULT initFromPathAndModel(const com::Utf8Str &drivePath, const com::Utf8Str &driveModel);
    void uninit() RT_OVERRIDE;
    /** @} */

    com::Utf8Str i_getDrivePath() { return m.drivePath; }

private:
    /** @name wrapped IHostDrive properties
     * @{ */
    virtual HRESULT getPartitioningType(PartitioningType_T *aPartitioningType) RT_OVERRIDE;
    virtual HRESULT getDrivePath(com::Utf8Str &aDrivePath) RT_OVERRIDE;
    virtual HRESULT getUuid(com::Guid &aUuid) RT_OVERRIDE;
    virtual HRESULT getSectorSize(ULONG *aSectorSize) RT_OVERRIDE;
    virtual HRESULT getSize(LONG64 *aSize) RT_OVERRIDE;
    virtual HRESULT getModel(com::Utf8Str &aModel) RT_OVERRIDE;
    virtual HRESULT getPartitions(std::vector<ComPtr<IHostDrivePartition> > &aPartitions) RT_OVERRIDE;
    /** @} */

    /** Data. */
    struct Data
    {
        Data() : cbSector(0), cbDisk(0)
        {
        }

        PartitioningType_T partitioningType;
        com::Utf8Str drivePath;
        com::Guid uuid;
        uint32_t cbSector;
        uint64_t cbDisk;
        com::Utf8Str model;
        std::vector<ComPtr<IHostDrivePartition> > partitions;
    };

    Data m;
};

#endif /* !MAIN_INCLUDED_HostDriveImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
