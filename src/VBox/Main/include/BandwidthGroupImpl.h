/* $Id: BandwidthGroupImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_BandwidthGroupImpl_h
#define MAIN_INCLUDED_BandwidthGroupImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/settings.h>
#include "BandwidthControlImpl.h"
#include "BandwidthGroupWrap.h"


class ATL_NO_VTABLE BandwidthGroup :
    public BandwidthGroupWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(BandwidthGroup)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(BandwidthControl *aParent,
                 const com::Utf8Str &aName,
                 BandwidthGroupType_T aType,
                 LONG64 aMaxBytesPerSec);
    HRESULT init(BandwidthControl *aParent, BandwidthGroup *aThat, bool aReshare = false);
    HRESULT initCopy(BandwidthControl *aParent, BandwidthGroup *aThat);
    void uninit();

    // public methods only for internal purposes
    void i_rollback();
    void i_commit();
    void i_unshare();
    void i_reference();
    void i_release();

    ComObjPtr<BandwidthGroup> i_getPeer() { return m->pPeer; }
    const Utf8Str &i_getName() const { return m->bd->mData.strName; }
    BandwidthGroupType_T i_getType() const { return m->bd->mData.enmType; }
    LONG64 i_getMaxBytesPerSec() const { return (LONG64)m->bd->mData.cMaxBytesPerSec; }
    ULONG i_getReferences() const { return m->bd->cReferences; }

private:

    // wrapped IBandwidthGroup properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getType(BandwidthGroupType_T *aType);
    HRESULT getReference(ULONG *aReferences);
    HRESULT getMaxBytesPerSec(LONG64 *aMaxBytesPerSec);
    HRESULT setMaxBytesPerSec(LONG64 MaxBytesPerSec);

    ////////////////////////////////////////////////////////////////////////////////
    ////
    //// private member data definition
    ////
    //////////////////////////////////////////////////////////////////////////////////
    //
    struct BackupableBandwidthGroupData
    {
       BackupableBandwidthGroupData()
           : cReferences(0)
       { }

       settings::BandwidthGroup mData;
       ULONG                    cReferences;
    };

    struct Data
    {
        Data(BandwidthControl * const aBandwidthControl)
            : pParent(aBandwidthControl),
              pPeer(NULL)
        { }

       BandwidthControl * const    pParent;
       ComObjPtr<BandwidthGroup>   pPeer;

       // use the XML settings structure in the members for simplicity
       Backupable<BackupableBandwidthGroupData> bd;
    };

    Data *m;
};

#endif /* !MAIN_INCLUDED_BandwidthGroupImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
