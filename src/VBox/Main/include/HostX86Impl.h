/* $Id: HostX86Impl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - x86 host specific IHost methods / attributes.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_HostX86Impl_h
#define MAIN_INCLUDED_HostX86Impl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HostX86Wrap.h"

class ATL_NO_VTABLE HostX86 :
    public HostX86Wrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(HostX86)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(void);
    void uninit();

    // public internal methods
    //HRESULT i_loadSettings(const settings::HostX86 &data);
    //HRESULT i_saveSettings(settings::HostX86 &data);
    //void i_rollback();
    //void i_commit();
    //void i_copyFrom(HostX86 *aThat);

private:

    // wrapped IHostX86 properties
    HRESULT getNumGroups(ULONG *aNumGroups);

    // wrapped IHostX86 methods
    HRESULT getProcessorCPUIDLeaf(ULONG aCpuId,
                                  ULONG aLeaf,
                                  ULONG aSubLeaf,
                                  ULONG *aValEax,
                                  ULONG *aValEbx,
                                  ULONG *aValEcx,
                                  ULONG *aValEdx);

    // Data

    struct Data
    {
        Data(Machine *pMachine)
        : pParent(pMachine)
        { }

        ~Data()
        {};

        Machine * const                 pParent;

        // peer
        const ComObjPtr<HostX86>  pPeer;
    };

    Data *m;
};

#endif /* !MAIN_INCLUDED_HostX86Impl_h */

