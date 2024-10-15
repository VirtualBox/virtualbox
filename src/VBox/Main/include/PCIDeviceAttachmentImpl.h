/* $Id: PCIDeviceAttachmentImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * PCI attachment information implmentation.
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

#ifndef MAIN_INCLUDED_PCIDeviceAttachmentImpl_h
#define MAIN_INCLUDED_PCIDeviceAttachmentImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "PCIDeviceAttachmentWrap.h"

namespace settings
{
    struct HostPCIDeviceAttachment;
}

class ATL_NO_VTABLE PCIDeviceAttachment :
    public PCIDeviceAttachmentWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(PCIDeviceAttachment)

    // public initializer/uninitializer for internal purposes only
    HRESULT init(IMachine *    aParent,
                 const Utf8Str &aDevName,
                 LONG          aHostAddess,
                 LONG          aGuestAddress,
                 BOOL          fPhysical);
    HRESULT initCopy(IMachine *aParent, PCIDeviceAttachment *aThat);
    void uninit();

    // settings
    HRESULT i_loadSettings(IMachine * aParent,
                           const settings::HostPCIDeviceAttachment& aHpda);
    HRESULT i_saveSettings(settings::HostPCIDeviceAttachment &data);

    HRESULT FinalConstruct();
    void FinalRelease();

private:

    // wrapped IPCIDeviceAttachment properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getIsPhysicalDevice(BOOL *aIsPhysicalDevice);
    HRESULT getHostAddress(LONG *aHostAddress);
    HRESULT getGuestAddress(LONG *aGuestAddress);

    struct Data;
    Data*  m;
};

#endif /* !MAIN_INCLUDED_PCIDeviceAttachmentImpl_h */
