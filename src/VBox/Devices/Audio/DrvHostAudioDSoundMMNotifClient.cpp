/* $Id: DrvHostAudioDSoundMMNotifClient.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include "DrvHostAudioDSoundMMNotifClient.h"

#include <iprt/win/windows.h>
#include <mmdeviceapi.h>
#include <iprt/win/endpointvolume.h>
#include <iprt/errcore.h>

#ifdef LOG_GROUP  /** @todo r=bird: wtf? Put it before all other includes like you're supposed to. */
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>


DrvHostAudioDSoundMMNotifClient::DrvHostAudioDSoundMMNotifClient(PPDMIHOSTAUDIOPORT pInterface, bool fDefaultIn, bool fDefaultOut)
    : m_fDefaultIn(fDefaultIn)
    , m_fDefaultOut(fDefaultOut)
    , m_fRegisteredClient(false)
    , m_cRef(1)
    , m_pIAudioNotifyFromHost(pInterface)
{
}

DrvHostAudioDSoundMMNotifClient::~DrvHostAudioDSoundMMNotifClient(void)
{
}

/**
 * Registers the mulitmedia notification client implementation.
 */
HRESULT DrvHostAudioDSoundMMNotifClient::Register(void)
{
    HRESULT hr = m_pEnum->RegisterEndpointNotificationCallback(this);
    if (SUCCEEDED(hr))
        m_fRegisteredClient = true;

    return hr;
}

/**
 * Unregisters the mulitmedia notification client implementation.
 */
void DrvHostAudioDSoundMMNotifClient::Unregister(void)
{
    if (m_fRegisteredClient)
    {
        m_pEnum->UnregisterEndpointNotificationCallback(this);

        m_fRegisteredClient = false;
    }
}

/**
 * Initializes the mulitmedia notification client implementation.
 *
 * @return  HRESULT
 */
HRESULT DrvHostAudioDSoundMMNotifClient::Initialize(void)
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void **)&m_pEnum);

    LogFunc(("Returning %Rhrc\n",  hr));
    return hr;
}

/**
 * Handler implementation which is called when an audio device state
 * has been changed.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID the state is announced for.
 * @param   dwNewState          New state the device is now in.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
    const char *pszState = "unknown";

    switch (dwNewState)
    {
        case DEVICE_STATE_ACTIVE:
            pszState = "active";
            break;
        case DEVICE_STATE_DISABLED:
            pszState = "disabled";
            break;
        case DEVICE_STATE_NOTPRESENT:
            pszState = "not present";
            break;
        case DEVICE_STATE_UNPLUGGED:
            pszState = "unplugged";
            break;
        default:
            break;
    }

    LogRel(("Audio: Device '%ls' has changed state to '%s'\n", pwstrDeviceId, pszState));

    if (m_pIAudioNotifyFromHost)
        m_pIAudioNotifyFromHost->pfnNotifyDevicesChanged(m_pIAudioNotifyFromHost);

    return S_OK;
}

/**
 * Handler implementation which is called when a new audio device has been added.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID which has been added.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
    LogRel(("Audio: Device '%ls' has been added\n", pwstrDeviceId));
    /* Note! It is hard to properly support non-default devices when the backend is DSound,
             as DSound talks GUID where-as the pwszDeviceId string we get here is something
             completely different.  So, ignorining that edge case here.  The WasApi backend
             supports this, though. */
    if (m_pIAudioNotifyFromHost)
        m_pIAudioNotifyFromHost->pfnNotifyDevicesChanged(m_pIAudioNotifyFromHost);
    return S_OK;
}

/**
 * Handler implementation which is called when an audio device has been removed.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID which has been removed.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
    LogRel(("Audio: Device '%ls' has been removed\n", pwstrDeviceId));
    if (m_pIAudioNotifyFromHost)
        m_pIAudioNotifyFromHost->pfnNotifyDevicesChanged(m_pIAudioNotifyFromHost);
    return S_OK;
}

/**
 * Handler implementation which is called when the device audio device has been
 * changed.
 *
 * @return  HRESULT
 * @param   eFlow                     Flow direction of the new default device.
 * @param   eRole                     Role of the new default device.
 * @param   pwstrDefaultDeviceId      ID of the new default device.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDefaultDeviceChanged(EDataFlow eFlow, ERole eRole, LPCWSTR pwstrDefaultDeviceId)
{
    /* When the user triggers a default device change, we'll typically get two or
       three notifications. Just pick up the one for the multimedia role for now
       (dunno if DSound default equals eMultimedia or eConsole, and whether it make
       any actual difference). */
    if (eRole == eMultimedia)
    {
        PDMAUDIODIR enmDir  = PDMAUDIODIR_INVALID;
        const char *pszRole = "unknown";
        if (eFlow == eRender)
        {
            pszRole = "output";
            if (m_fDefaultOut)
                enmDir = PDMAUDIODIR_OUT;
        }
        else if (eFlow == eCapture)
        {
            pszRole = "input";
            if (m_fDefaultIn)
                enmDir = PDMAUDIODIR_IN;
        }

        LogRel(("Audio: Default %s device has been changed to '%ls'\n", pszRole, pwstrDefaultDeviceId));

        if (m_pIAudioNotifyFromHost)
        {
            if (enmDir != PDMAUDIODIR_INVALID)
                m_pIAudioNotifyFromHost->pfnNotifyDeviceChanged(m_pIAudioNotifyFromHost, enmDir, NULL);
            m_pIAudioNotifyFromHost->pfnNotifyDevicesChanged(m_pIAudioNotifyFromHost);
        }
    }
    return S_OK;
}

STDMETHODIMP DrvHostAudioDSoundMMNotifClient::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    const IID MY_IID_IMMNotificationClient = __uuidof(IMMNotificationClient);

    if (   IsEqualIID(interfaceID, IID_IUnknown)
        || IsEqualIID(interfaceID, MY_IID_IMMNotificationClient))
    {
        *ppvInterface = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }

    *ppvInterface = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DrvHostAudioDSoundMMNotifClient::AddRef(void)
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) DrvHostAudioDSoundMMNotifClient::Release(void)
{
    long lRef = InterlockedDecrement(&m_cRef);
    if (lRef == 0)
        delete this;

    return lRef;
}

