/* $Id: VBoxCredProvProvider.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCredProvProvider - The actual credential provider class.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <new> /* For bad_alloc. */

#include <iprt/win/windows.h>
#include <iprt/win/credentialprovider.h>

#include <iprt/errcore.h>
#include <VBox/VBoxGuestLib.h>

#include "VBoxCredentialProvider.h"
#include "VBoxCredProvProvider.h"
#include "VBoxCredProvCredential.h"



VBoxCredProvProvider::VBoxCredProvProvider(void)
    : m_cRefs(1)
    , m_pCred(NULL)
    , m_pPoller(NULL)
    , m_pEvents(NULL)
    , m_fHandleRemoteSessions(false)
{
    VBoxCredentialProviderAcquire();

    VBoxCredProvReportStatus(VBoxGuestFacilityStatus_Init);
}


VBoxCredProvProvider::~VBoxCredProvProvider(void)
{
    VBoxCredProvVerbose(0, "VBoxCredProv: Destroying\n");

    if (m_pCred)
    {
        m_pCred->Release();
        m_pCred = NULL;
    }

    if (m_pPoller)
    {
        m_pPoller->Shutdown();
        delete m_pPoller;
        m_pPoller = NULL;
    }

    VBoxCredProvReportStatus(VBoxGuestFacilityStatus_Terminated);

    VBoxCredentialProviderRelease();
}


/* IUnknown overrides. */
ULONG
VBoxCredProvProvider::AddRef(void)
{
    LONG cRefs = InterlockedIncrement(&m_cRefs);
    VBoxCredProvVerbose(0, "VBoxCredProv: AddRef: Returning refcount=%ld\n",
                        cRefs);
    return cRefs;
}


ULONG
VBoxCredProvProvider::Release(void)
{
    LONG cRefs = InterlockedDecrement(&m_cRefs);
    VBoxCredProvVerbose(0, "VBoxCredProv: Release: Returning refcount=%ld\n",
                        cRefs);
    if (!cRefs)
    {
        VBoxCredProvVerbose(0, "VBoxCredProv: Calling destructor\n");
        delete this;
    }
    return cRefs;
}


HRESULT
VBoxCredProvProvider::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    HRESULT hr = S_OK;
    if (ppvInterface)
    {
        if (   IID_IUnknown            == interfaceID
            || IID_ICredentialProvider == interfaceID)
        {
            *ppvInterface = static_cast<IUnknown*>(this);
            reinterpret_cast<IUnknown*>(*ppvInterface)->AddRef();
        }
        else
        {
            *ppvInterface = NULL;
            hr = E_NOINTERFACE;
        }
    }
    else
        hr = E_INVALIDARG;

    return hr;
}


/**
 * Loads the global configuration from registry.
 *
 * @return  DWORD       Windows error code.
 */
DWORD
VBoxCredProvProvider::LoadConfiguration(void)
{
    HKEY hKey;
    /** @todo Add some registry wrapper function(s) as soon as we got more values to retrieve. */
    DWORD dwRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox Guest Additions\\AutoLogon",
                               0L, KEY_QUERY_VALUE, &hKey);
    if (dwRet == ERROR_SUCCESS)
    {
        DWORD dwValue;
        DWORD dwType = REG_DWORD;
        DWORD dwSize = sizeof(DWORD);

        dwRet = RegQueryValueEx(hKey, L"HandleRemoteSessions", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
        if (   dwRet  == ERROR_SUCCESS
            && dwType == REG_DWORD
            && dwSize == sizeof(DWORD))
        {
            m_fHandleRemoteSessions = RT_BOOL(dwValue);
        }

        dwRet = RegQueryValueEx(hKey, L"LoggingEnabled", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
        if (   dwRet  == ERROR_SUCCESS
            && dwType == REG_DWORD
            && dwSize == sizeof(DWORD))
        {
            g_dwVerbosity = 1; /* Default logging level. */
        }

        if (g_dwVerbosity) /* Do we want logging at all? */
        {
            dwRet = RegQueryValueEx(hKey, L"LoggingLevel", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
            if (   dwRet  == ERROR_SUCCESS
                && dwType == REG_DWORD
                && dwSize == sizeof(DWORD))
            {
                g_dwVerbosity = dwValue;
            }
        }

        RegCloseKey(hKey);
    }
    /* Do not report back an error here yet. */
    return ERROR_SUCCESS;
}


/**
 * Determines whether we should handle the current session or not.
 *
 * @return  bool        true if we should handle this session, false if not.
 */
bool
VBoxCredProvProvider::HandleCurrentSession(void)
{
    /* Load global configuration from registry. */
    int rc = LoadConfiguration();
    if (RT_FAILURE(rc))
        VBoxCredProvVerbose(0, "VBoxCredProv: Error loading global configuration, rc=%Rrc\n",
                            rc);

    bool fHandle = false;
    if (VbglR3AutoLogonIsRemoteSession())
    {
        if (m_fHandleRemoteSessions) /* Force remote session handling. */
            fHandle = true;
    }
    else /* No remote session. */
        fHandle = true;

    VBoxCredProvVerbose(3, "VBoxCredProv: Handling current session=%RTbool\n", fHandle);
    return fHandle;
}


/**
 * Tells this provider the current usage scenario.
 *
 * @return  HRESULT
 * @param   enmUsageScenario    Current usage scenario this provider will be
 *                              used in.
 * @param   dwFlags             Optional flags for the usage scenario.
 */
HRESULT
VBoxCredProvProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO enmUsageScenario, DWORD dwFlags)
{
    HRESULT hr = S_OK;

    VBoxCredProvVerbose(0, "VBoxCredProv::SetUsageScenario: enmUsageScenario=%d, dwFlags=%ld\n",
                        enmUsageScenario, dwFlags);

    m_enmUsageScenario = enmUsageScenario;

    switch (m_enmUsageScenario)
    {
        case CPUS_LOGON:
        case CPUS_UNLOCK_WORKSTATION:
        {
            VBoxCredProvReportStatus(VBoxGuestFacilityStatus_Active);

            DWORD dwErr = LoadConfiguration();
            if (dwErr != ERROR_SUCCESS)
                VBoxCredProvVerbose(0, "VBoxCredProv: Error while loading configuration, error=%ld\n", dwErr);
            /* Do not stop running on a misconfigured system. */

            /*
             * If we're told to not handle the current session just bail out and let the
             * user know.
             */
            if (!HandleCurrentSession())
                break;

            hr = S_OK;
            if (!m_pPoller)
            {
#ifdef RT_EXCEPTIONS_ENABLED
                try { m_pPoller = new VBoxCredProvPoller(); }
                catch (std::bad_alloc &) { hr = E_OUTOFMEMORY; }
#else
                m_pPoller = new VBoxCredProvPoller();
                AssertStmt(m_pPoller, hr = E_OUTOFMEMORY);
#endif
                if (SUCCEEDED(hr))
                {
                    int rc = m_pPoller->Initialize(this);
                    if (RT_FAILURE(rc))
                        VBoxCredProvVerbose(0, "VBoxCredProv::SetUsageScenario: Error initializing poller thread, rc=%Rrc\n", rc);
/** @todo r=bird: Why is the initialize failure ignored here? */
                }
            }

            if (   SUCCEEDED(hr)
                && !m_pCred)
            {
#ifdef RT_EXCEPTIONS_ENABLED
                try { m_pCred = new VBoxCredProvCredential(); }
                catch (std::bad_alloc &) { hr = E_OUTOFMEMORY; }
#else
                m_pCred = new VBoxCredProvCredential();
                AssertStmt(m_pCred, hr = E_OUTOFMEMORY);
#endif
                if (SUCCEEDED(hr))
                    hr = m_pCred->Initialize(m_enmUsageScenario);
            }
            else
            {
                /* All set up already! Nothing to do here right now. */
            }

            /* If we failed, do some cleanup. */
/** @todo r=bird: Why aren't we cleaning up m_pPoller too? Very confusing given
 * that m_pCred wasn't necessarily even created above.  Always explain the WHY
 * when doing something that isn't logical like here! */
            if (FAILED(hr))
            {
                if (m_pCred != NULL)
                {
                    m_pCred->Release();
                    m_pCred = NULL;
                }
            }
            break;
        }

        case CPUS_CHANGE_PASSWORD: /* Asks us to provide a way to change the password. */
        case CPUS_CREDUI:          /* Displays an own UI. We don't need that. */
        case CPUS_PLAP:            /* See Pre-Logon-Access Provider. Not needed (yet). */
            hr = E_NOTIMPL;
            break;

        default:

            hr = E_INVALIDARG;
            break;
    }

    VBoxCredProvVerbose(0, "VBoxCredProv::SetUsageScenario returned hr=0x%08x\n", hr);
    return hr;
}


/**
 * Tells this provider how the serialization will be handled. Currently not used.
 *
 * @return  STDMETHODIMP
 * @param   pcpCredentialSerialization      Credentials serialization.
 */
STDMETHODIMP
VBoxCredProvProvider::SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION *pcpCredentialSerialization)
{
    NOREF(pcpCredentialSerialization);
    return E_NOTIMPL;
}


/**
 * Initializes the communication with LogonUI through callbacks events which we can later
 * use to start re-enumeration of credentials.
 *
 * @return  HRESULT
 * @param   pcpEvents               Pointer to event interface.
 * @param   upAdviseContext         The current advise context.
 */
HRESULT
VBoxCredProvProvider::Advise(ICredentialProviderEvents *pcpEvents, UINT_PTR upAdviseContext)
{
    VBoxCredProvVerbose(0, "VBoxCredProv::Advise, pcpEvents=0x%p, upAdviseContext=%u\n",
                        pcpEvents, upAdviseContext);
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = NULL;
    }

    m_pEvents = pcpEvents;
    if (m_pEvents)
        m_pEvents->AddRef();

    /*
     * Save advice context for later use when binding to
     * certain ICredentialProviderEvents events.
     */
    m_upAdviseContext = upAdviseContext;
    return S_OK;
}


/**
 * Uninitializes the callback events so that they're no longer valid.
 *
 * @return  HRESULT
 */
HRESULT
VBoxCredProvProvider::UnAdvise(void)
{
    VBoxCredProvVerbose(0, "VBoxCredProv::UnAdvise: pEvents=0x%p\n",
                        m_pEvents);
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = NULL;
    }

    return S_OK;
}


/**
 * Retrieves the total count of fields we're handling (needed for field enumeration
 * through LogonUI).
 *
 * @return  HRESULT
 * @param   pdwCount        Receives total count of fields.
 */
HRESULT
VBoxCredProvProvider::GetFieldDescriptorCount(DWORD *pdwCount)
{
    if (pdwCount)
    {
        *pdwCount = VBOXCREDPROV_NUM_FIELDS;
        VBoxCredProvVerbose(0, "VBoxCredProv::GetFieldDescriptorCount: %ld\n", *pdwCount);
    }
    return S_OK;
}


/**
 * Retrieves a descriptor of a specified field.
 *
 * @return  HRESULT
 * @param   dwIndex                 ID of field to retrieve descriptor for.
 * @param   ppFieldDescriptor       Pointer which receives the allocated field
 *                                  descriptor.
 */
HRESULT
VBoxCredProvProvider::GetFieldDescriptorAt(DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR **ppFieldDescriptor)
{
    HRESULT hr = S_OK;
    if (   dwIndex < VBOXCREDPROV_NUM_FIELDS
        && ppFieldDescriptor)
    {
        PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR pcpFieldDesc =
            (PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));

        if (pcpFieldDesc)
        {
            const VBOXCREDPROV_FIELD &field = s_VBoxCredProvDefaultFields[dwIndex];

            RT_BZERO(pcpFieldDesc, sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));

            pcpFieldDesc->dwFieldID     = field.desc.dwFieldID;
            pcpFieldDesc->cpft          = field.desc.cpft;

            PCRTUTF16 pcwszField = NULL;

            if (dwIndex != VBOXCREDPROV_FIELDID_PASSWORD) /* Don't ever get any password. Never ever, ever. */
            {
                if (m_pCred) /* If we have retrieved credentials, get the actual (current) value. */
                    pcwszField = m_pCred->getField(dwIndex);
                else /* Otherwise get the default value. */
                    pcwszField = field.desc.pszLabel;
            }

            hr = SHStrDupW(pcwszField ? pcwszField : L"", &pcpFieldDesc->pszLabel);

            VBoxCredProvVerbose(0, "VBoxCredProv::GetFieldDescriptorAt: dwIndex=%ld, pszLabel=%ls, hr=0x%08x\n",
                                dwIndex,
#ifdef DEBUG /* Don't show any (sensitive data) in release mode. */
                                pcwszField ? pcwszField : L"",
#else
                                L"XXX",
#endif
                                hr);

            pcpFieldDesc->guidFieldType = field.desc.guidFieldType;
        }
        else
            hr = E_OUTOFMEMORY;

        if (SUCCEEDED(hr))
        {
            *ppFieldDescriptor = pcpFieldDesc;
        }
        else if (pcpFieldDesc)
        {
            if (pcpFieldDesc->pszLabel)
            {
                CoTaskMemFree(pcpFieldDesc->pszLabel);
                pcpFieldDesc->pszLabel = NULL;
            }

            CoTaskMemFree(pcpFieldDesc);
        }
    }
    else
        hr = E_INVALIDARG;

    VBoxCredProvVerbose(0, "VBoxCredProv::GetFieldDescriptorAt: dwIndex=%ld, ppDesc=0x%p, hr=0x%08x\n",
                        dwIndex, ppFieldDescriptor, hr);
    return hr;
}


/**
 * Retrieves the total number of credentials this provider can offer at the current time and
 * if a logon attempt should be made.
 *
 * @return  HRESULT
 * @param   pdwCount                    Receives number of credentials to serve.
 * @param   pdwDefault                  Receives the credentials index to try
 *                                      logging on if there is more than one
 *                                      credential provided. 0 is default.
 * @param   pfAutoLogonWithDefault      Receives a flag indicating whether a
 *                                      logon attempt using the default
 *                                      credential should be made or not.
 */
HRESULT
VBoxCredProvProvider::GetCredentialCount(DWORD *pdwCount, DWORD *pdwDefault, BOOL *pfAutoLogonWithDefault)
{
    AssertPtr(pdwCount);
    AssertPtr(pdwDefault);
    AssertPtr(pfAutoLogonWithDefault);

    bool fHasCredentials = false;

    /* Do we have credentials? */
    if (m_pCred)
    {
        int rc = m_pCred->RetrieveCredentials();
        fHasCredentials = rc == VINF_SUCCESS;
    }

    if (fHasCredentials)
    {
        *pdwCount = 1;                   /* This provider always has the same number of credentials (1). */
        *pdwDefault = 0;                 /* The credential we provide is *always* at index 0! */
        *pfAutoLogonWithDefault = TRUE;  /* We always at least try to auto-login (if password is correct). */
    }
    else
    {
        *pdwCount = 0;
        *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
        *pfAutoLogonWithDefault = FALSE;
    }

    VBoxCredProvVerbose(0, "VBoxCredProv::GetCredentialCount: *pdwCount=%ld, *pdwDefault=%ld, *pfAutoLogonWithDefault=%s\n",
                        *pdwCount, *pdwDefault, *pfAutoLogonWithDefault ? "true" : "false");
    return S_OK;
}


/**
 * Called by Winlogon to retrieve the interface of our current ICredentialProviderCredential interface.
 *
 * @return  HRESULT
 * @param   dwIndex                     Index of credential (in case there is more than one credential at a time) to
 *                                      retrieve the interface for.
 * @param   ppCredProvCredential        Pointer that receives the credential interface.
 */
HRESULT
VBoxCredProvProvider::GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential **ppCredProvCredential)
{
    VBoxCredProvVerbose(0, "VBoxCredProv::GetCredentialAt: Index=%ld, ppCredProvCredential=0x%p\n",
                        dwIndex, ppCredProvCredential);
    if (!m_pCred)
    {
        VBoxCredProvVerbose(0, "VBoxCredProv::GetCredentialAt: No credentials available\n");
        return E_INVALIDARG;
    }

    HRESULT hr;
    if (   dwIndex == 0
        && ppCredProvCredential)
    {
        hr = m_pCred->QueryInterface(IID_ICredentialProviderCredential,
                                     reinterpret_cast<void**>(ppCredProvCredential));
    }
    else
    {
        VBoxCredProvVerbose(0, "VBoxCredProv::GetCredentialAt: More than one credential not supported!\n");
        hr = E_INVALIDARG;
    }
    return hr;
}


/**
 * Triggers a credential re-enumeration -- will be called by our poller thread. This then invokes
 * GetCredentialCount() and GetCredentialAt() called by Winlogon.
 */
void
VBoxCredProvProvider::OnCredentialsProvided(void)
{
    VBoxCredProvVerbose(0, "VBoxCredProv::OnCredentialsProvided\n");

    if (m_pEvents)
        m_pEvents->CredentialsChanged(m_upAdviseContext);
}


/**
 * Creates our provider. This happens *before* CTRL-ALT-DEL was pressed!
 */
HRESULT
VBoxCredProvProviderCreate(REFIID interfaceID, void **ppvInterface)
{
    VBoxCredProvProvider *pProvider;
#ifdef RT_EXCEPTIONS_ENABLED
    try { pProvider = new VBoxCredProvProvider(); }
    catch (std::bad_alloc &) { AssertFailedReturn(E_OUTOFMEMORY); }
#else
    pProvider = new VBoxCredProvProvider();
    AssertReturn(pProvider, E_OUTOFMEMORY);
#endif

    HRESULT hr = pProvider->QueryInterface(interfaceID, ppvInterface);
    pProvider->Release();

    return hr;
}

