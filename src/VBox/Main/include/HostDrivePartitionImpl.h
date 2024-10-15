/* $Id: HostDrivePartitionImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - IHostDrivePartition implementation, VBoxSVC.
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

#ifndef MAIN_INCLUDED_HostDrivePartitionImpl_h
#define MAIN_INCLUDED_HostDrivePartitionImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HostDrivePartitionWrap.h"

#include <iprt/dvm.h>

class ATL_NO_VTABLE HostDrivePartition
    : public HostDrivePartitionWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(HostDrivePartition)

    HRESULT FinalConstruct();
    void FinalRelease();

    /** @name Public initializer/uninitializer for internal purposes only.
     * @{ */
    HRESULT initFromDvmVol(RTDVMVOLUME hVol);
    void uninit() RT_OVERRIDE;
    /** @} */

private:
    /** @name wrapped IHostDrivePartition properties
     * @{  */
    /* Common: */
    virtual HRESULT getNumber(ULONG *aNumber) RT_OVERRIDE               { *aNumber        = m.number;       return S_OK; }
    virtual HRESULT getSize(LONG64 *aSize) RT_OVERRIDE                  { *aSize          = m.cbVol;        return S_OK; }
    virtual HRESULT getStart(LONG64 *aStart) RT_OVERRIDE                { *aStart         = m.offStart;     return S_OK; }
    virtual HRESULT getType(PartitionType_T *aType) RT_OVERRIDE         { *aType          = m.enmType;      return S_OK; }
    virtual HRESULT getActive(BOOL *aActive) RT_OVERRIDE                { *aActive        = m.active;       return S_OK; }
    /* MBR: */
    virtual HRESULT getTypeMBR(ULONG *aTypeMBR) RT_OVERRIDE             { *aTypeMBR       = m.bMBRType;     return S_OK; }
    virtual HRESULT getStartCylinder(ULONG *aStartCylinder) RT_OVERRIDE { *aStartCylinder = m.firstCylinder; return S_OK; }
    virtual HRESULT getStartHead(ULONG *aStartHead) RT_OVERRIDE         { *aStartHead     = m.firstHead;    return S_OK; }
    virtual HRESULT getStartSector(ULONG *aStartSector) RT_OVERRIDE     { *aStartSector   = m.firstSector;  return S_OK; }
    virtual HRESULT getEndCylinder(ULONG *aEndCylinder) RT_OVERRIDE     { *aEndCylinder   = m.lastCylinder; return S_OK; }
    virtual HRESULT getEndHead(ULONG *aEndHead) RT_OVERRIDE             { *aEndHead       = m.lastHead;     return S_OK; }
    virtual HRESULT getEndSector(ULONG *aEndSector) RT_OVERRIDE         { *aEndSector     = m.lastSector;   return S_OK; }
    /* GPT: */
    virtual HRESULT getTypeUuid(com::Guid &aTypeUuid) RT_OVERRIDE       { aTypeUuid       = m.typeUuid;     return S_OK; }
    virtual HRESULT getUuid(com::Guid &aUuid) RT_OVERRIDE               { aUuid           = m.uuid;         return S_OK; }
    virtual HRESULT getName(com::Utf8Str &aName) RT_OVERRIDE            { return aName.assignEx(m.name); };
    /** @} */

    /** Data. */
    struct Data
    {
        Data()
            : number(0)
            , cbVol(0)
            , offStart(0)
            , enmType(PartitionType_Unknown)
            , active(FALSE)
            , bMBRType(0)
            , firstCylinder(0)
            , firstHead(0)
            , firstSector(0)
            , lastCylinder(0)
            , lastHead(0)
            , lastSector(0)
            , typeUuid()
            , uuid()
            , name()
        {
        }

        ULONG number;
        LONG64 cbVol;
        LONG64 offStart;
        PartitionType_T enmType;
        BOOL active;
        /** @name MBR specifics
         * @{ */
        uint8_t bMBRType;
        uint16_t firstCylinder;
        uint8_t firstHead;
        uint8_t firstSector;
        uint16_t lastCylinder;
        uint8_t lastHead;
        uint8_t lastSector;
        /** @} */
        /** @name GPT specifics
         * @{ */
        com::Guid typeUuid;
        com::Guid uuid;
        com::Utf8Str name;
        /** @} */
    };

    Data m;
};

#endif /* !MAIN_INCLUDED_HostDrivePartitionImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
