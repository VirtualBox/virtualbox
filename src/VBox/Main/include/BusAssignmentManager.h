/* $Id: BusAssignmentManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox bus slots assignment manager
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_BusAssignmentManager_h
#define MAIN_INCLUDED_BusAssignmentManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBox/types.h"
#include "VBox/pci.h"
#include "VirtualBoxBase.h"
#include <vector>

class BusAssignmentManager
{
private:
    struct State;
    State *pState;

    BusAssignmentManager();
    virtual ~BusAssignmentManager();

    HRESULT assignPCIDeviceImpl(const char *pszDevName, PCFGMNODE pCfg, PCIBusAddress& GuestAddress,
                                PCIBusAddress HostAddress, bool fGuestAddressRequired = false);

public:
    struct PCIDeviceInfo
    {
        com::Utf8Str strDeviceName;
        PCIBusAddress guestAddress;
        PCIBusAddress hostAddress;
    };

    static BusAssignmentManager *createInstance(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType);
    virtual void AddRef();
    virtual void Release();

    virtual HRESULT assignHostPCIDevice(const char *pszDevName, PCFGMNODE pCfg, PCIBusAddress HostAddress,
                                        PCIBusAddress& GuestAddress, bool fAddressRequired = false)
    {
        return assignPCIDeviceImpl(pszDevName, pCfg, GuestAddress, HostAddress, fAddressRequired);
    }

    virtual HRESULT assignPCIDevice(const char *pszDevName, PCFGMNODE pCfg, PCIBusAddress& Address, bool fAddressRequired = false)
    {
        PCIBusAddress HostAddress;
        return assignPCIDeviceImpl(pszDevName, pCfg, Address, HostAddress, fAddressRequired);
    }

    virtual HRESULT assignPCIDevice(const char *pszDevName, PCFGMNODE pCfg)
    {
        PCIBusAddress GuestAddress;
        PCIBusAddress HostAddress;
        return assignPCIDeviceImpl(pszDevName, pCfg, GuestAddress, HostAddress, false);
    }
    virtual bool findPCIAddress(const char *pszDevName, int iInstance, PCIBusAddress& Address);
    virtual bool hasPCIDevice(const char *pszDevName, int iInstance)
    {
        PCIBusAddress Address;
        return findPCIAddress(pszDevName, iInstance, Address);
    }
    virtual void listAttachedPCIDevices(std::vector<PCIDeviceInfo> &aAttached);
};

#endif /* !MAIN_INCLUDED_BusAssignmentManager_h */
