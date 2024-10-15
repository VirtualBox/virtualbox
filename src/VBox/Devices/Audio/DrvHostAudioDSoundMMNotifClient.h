/* $Id: DrvHostAudioDSoundMMNotifClient.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - DSound - Implementation of the IMMNotificationClient interface to detect audio endpoint changes.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Audio_DrvHostAudioDSoundMMNotifClient_h
#define VBOX_INCLUDED_SRC_Audio_DrvHostAudioDSoundMMNotifClient_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>
#include <iprt/win/windows.h>
#include <mmdeviceapi.h>

#include <VBox/vmm/pdmaudioifs.h>


class DrvHostAudioDSoundMMNotifClient : public IMMNotificationClient
{
public:

    DrvHostAudioDSoundMMNotifClient(PPDMIHOSTAUDIOPORT pInterface, bool fDefaultIn, bool fDefaultOut);
    virtual ~DrvHostAudioDSoundMMNotifClient();

    HRESULT Initialize();

    HRESULT Register(void);
    void    Unregister(void);

    /** @name IUnknown interface
     * @{ */
    IFACEMETHODIMP_(ULONG) Release();
    /** @} */

private:

    bool                        m_fDefaultIn;
    bool                        m_fDefaultOut;
    bool                        m_fRegisteredClient;
    IMMDeviceEnumerator        *m_pEnum;
    IMMDevice                  *m_pEndpoint;

    long                        m_cRef;

    PPDMIHOSTAUDIOPORT          m_pIAudioNotifyFromHost;

    /** @name IMMNotificationClient interface
     * @{ */
    IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);
    IFACEMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId);
    IFACEMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId);
    IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId);
    IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, const PROPERTYKEY /*key*/) { return S_OK; }
    /** @} */

    /** @name IUnknown interface
     * @{ */
    IFACEMETHODIMP QueryInterface(const IID& iid, void** ppUnk);
    IFACEMETHODIMP_(ULONG) AddRef();
    /** @} */
};

#endif /* !VBOX_INCLUDED_SRC_Audio_DrvHostAudioDSoundMMNotifClient_h */

