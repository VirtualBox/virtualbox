/* $Id: PCIDeviceAttachmentImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
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

#define LOG_GROUP LOG_GROUP_MAIN_PCIDEVICEATTACHMENT
#include "PCIDeviceAttachmentImpl.h"
#include "AutoCaller.h"
#include "Global.h"
#include "LoggingNew.h"

#include <VBox/settings.h>

struct PCIDeviceAttachment::Data
{
    Data(const Utf8Str &aDevName,
         LONG          aHostAddress,
         LONG          aGuestAddress,
         BOOL          afPhysical) :
        DevName(aDevName),
        HostAddress(aHostAddress),
        GuestAddress(aGuestAddress),
        fPhysical(afPhysical)
    {
    }

    Utf8Str          DevName;
    LONG             HostAddress;
    LONG             GuestAddress;
    BOOL             fPhysical;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
DEFINE_EMPTY_CTOR_DTOR(PCIDeviceAttachment)

HRESULT PCIDeviceAttachment::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void PCIDeviceAttachment::FinalRelease()
{
    LogFlowThisFunc(("\n"));
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////
HRESULT PCIDeviceAttachment::init(IMachine      *aParent,
                                  const Utf8Str &aDevName,
                                  LONG          aHostAddress,
                                  LONG          aGuestAddress,
                                  BOOL          fPhysical)
{
    NOREF(aParent);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aDevName, aHostAddress, aGuestAddress, fPhysical);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

HRESULT PCIDeviceAttachment::initCopy(IMachine *aParent, PCIDeviceAttachment *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    return init(aParent, aThat->m->DevName, aThat->m->HostAddress, aThat->m->GuestAddress, aThat->m->fPhysical);
}

HRESULT PCIDeviceAttachment::i_loadSettings(IMachine *aParent,
                                            const settings::HostPCIDeviceAttachment &hpda)
{
    /** @todo r=bird: Inconsistent signed/unsigned crap. */
    return init(aParent, hpda.strDeviceName, (LONG)hpda.uHostAddress, (LONG)hpda.uGuestAddress, TRUE);
}


HRESULT PCIDeviceAttachment::i_saveSettings(settings::HostPCIDeviceAttachment &data)
{
    Assert(m);
    /** @todo r=bird: Inconsistent signed/unsigned crap. */
    data.uHostAddress  = (uint32_t)m->HostAddress;
    data.uGuestAddress = (uint32_t)m->GuestAddress;
    data.strDeviceName = m->DevName;

    return S_OK;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void PCIDeviceAttachment::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    delete m;
    m = NULL;
}

// IPCIDeviceAttachment properties
/////////////////////////////////////////////////////////////////////////////
HRESULT PCIDeviceAttachment::getName(com::Utf8Str &aName)
{
    aName = m->DevName;
    return S_OK;
}

HRESULT PCIDeviceAttachment::getIsPhysicalDevice(BOOL *aIsPhysicalDevice)
{
    *aIsPhysicalDevice = m->fPhysical;
    return S_OK;
}

HRESULT PCIDeviceAttachment::getHostAddress(LONG *aHostAddress)
{
    *aHostAddress = m->HostAddress;
    return S_OK;
}
HRESULT PCIDeviceAttachment::getGuestAddress(LONG *aGuestAddress)
{
    *aGuestAddress = m->GuestAddress;
    return S_OK;
}
