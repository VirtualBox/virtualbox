/* $Id: BandwidthControlImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_BandwidthControlImpl_h
#define MAIN_INCLUDED_BandwidthControlImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "BandwidthControlWrap.h"

class BandwidthGroup;

namespace settings
{
    struct IOSettings;
}

class ATL_NO_VTABLE BandwidthControl :
    public BandwidthControlWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(BandwidthControl)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, BandwidthControl *aThat);
    HRESULT initCopy(Machine *aParent, BandwidthControl *aThat);
    void uninit();

    // public internal methods
    HRESULT i_loadSettings(const settings::IOSettings &data);
    HRESULT i_saveSettings(settings::IOSettings &data);
    void i_rollback();
    void i_commit();
    void i_copyFrom(BandwidthControl *aThat);
    Machine *i_getMachine() const;
    HRESULT i_getBandwidthGroupByName(const Utf8Str &aName,
                                      ComObjPtr<BandwidthGroup> &aBandwidthGroup,
                                      bool aSetError /* = false */);

private:

    // wrapped IBandwidthControl properties
    HRESULT getNumGroups(ULONG *aNumGroups);

    // wrapped IBandwidthControl methods
    HRESULT createBandwidthGroup(const com::Utf8Str &aName,
                                       BandwidthGroupType_T aType,
                                       LONG64 aMaxBytesPerSec);
    HRESULT deleteBandwidthGroup(const com::Utf8Str &aName);
    HRESULT getBandwidthGroup(const com::Utf8Str &aName,
                                    ComPtr<IBandwidthGroup> &aBandwidthGroup);
    HRESULT getAllBandwidthGroups(std::vector<ComPtr<IBandwidthGroup> > &aBandwidthGroups);

    // Data
    typedef std::list< ComObjPtr<BandwidthGroup> > BandwidthGroupList;

    struct Data
    {
        Data(Machine *pMachine)
        : pParent(pMachine)
        { }

        ~Data()
        {};

        Machine * const                 pParent;

        // peer machine's bandwidth control
        const ComObjPtr<BandwidthControl>  pPeer;

        // the following fields need special backup/rollback/commit handling,
        // so they cannot be a part of BackupableData
        Backupable<BandwidthGroupList>    llBandwidthGroups;
    };

    Data *m;
};

#endif /* !MAIN_INCLUDED_BandwidthControlImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
