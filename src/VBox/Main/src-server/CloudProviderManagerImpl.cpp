/* $Id: CloudProviderManagerImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ICloudProviderManager  COM class implementations.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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


#define LOG_GROUP LOG_GROUP_MAIN_CLOUDPROVIDERMANAGER
#include <iprt/cpp/utils.h>
#include <VBox/com/array.h>

#include "VirtualBoxImpl.h"
#include "CloudProviderManagerImpl.h"
#include "ExtPackManagerImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"


////////////////////////////////////////////////////////////////////////////////
//
// CloudProviderManager constructor / destructor
//
// ////////////////////////////////////////////////////////////////////////////////
CloudProviderManager::CloudProviderManager()
  : m_pVirtualBox(NULL)
{
}

CloudProviderManager::~CloudProviderManager()
{
}


HRESULT CloudProviderManager::FinalConstruct()
{
    return BaseFinalConstruct();
}

void CloudProviderManager::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT CloudProviderManager::init(VirtualBox *aVirtualBox)
{
    // Enclose the state transition NotReady->InInit->Ready.
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m_apCloudProviders.clear();
    unconst(m_pVirtualBox) = aVirtualBox;

    autoInitSpan.setSucceeded();
    return S_OK;
}

void CloudProviderManager::uninit()
{
    // Enclose the state transition Ready->InUninit->NotReady.
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_EXTPACK
    m_mapCloudProviderManagers.clear();
#endif
    m_apCloudProviders.clear();

    unconst(m_pVirtualBox) = NULL; // not a ComPtr, but be pedantic
}


#ifdef VBOX_WITH_EXTPACK

bool CloudProviderManager::i_canRemoveExtPack(IExtPack *aExtPack)
{
    AssertReturn(aExtPack, false);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // If any cloud provider in this extension pack fails to prepare the
    // uninstall it and the cloud provider will be kept, so that the user
    // can retry safely later. All other cloud providers in this extpack
    // will be done as usual. No attempt is made to bring back the other
    // cloud providers into working shape.

    bool fRes = true;

    Bstr bstrExtPackName;
    aExtPack->COMGETTER(Name)(bstrExtPackName.asOutParam());
    Utf8Str strExtPackName(bstrExtPackName);

    /* is there a cloud provider in this extpack? */
    ExtPackNameCloudProviderManagerMap::iterator it
        = m_mapCloudProviderManagers.find(strExtPackName);
    if (it != m_mapCloudProviderManagers.end())
    {
        // const ComPtr<ICloudProviderManager> pManager(it->second); /* unused */

        /* loop over all providers checking for those from the aExtPack */
        Assert(m_astrExtPackNames.size() == m_apCloudProviders.size());
        for (size_t i = 0; i < m_astrExtPackNames.size(); )
        {
            /* the horse it rode in on? */
            if (m_astrExtPackNames[i] != strExtPackName)
            {
                i++;
                continue;     /* not the extpack we are looking for */
            }

            /* note the id of this provider to send an event later */
            Bstr bstrProviderId;

            /*
             * pTmpProvider will point to an object with refcount > 0
             * until the ComPtr is removed from m_apCloudProviders.
             * PrepareUninstall checks that that is the only reference
             */
            HRESULT hrc = S_OK;
            ULONG uRefCnt = 1;
            ICloudProvider *pTmpProvider(m_apCloudProviders[i]);
            if (pTmpProvider)
            {
                /* do this before the provider goes over the rainbow bridge */
                hrc = pTmpProvider->COMGETTER(Id)(bstrProviderId.asOutParam());

                /*
                 * We send this event @em before we try to uninstall
                 * the provider.  The GUI can get the event and get
                 * rid of any references to the objects related to
                 * this provider that it still has.
                 */
                if (bstrProviderId.isNotEmpty())
                    m_pVirtualBox->i_onCloudProviderUninstall(bstrProviderId);

                hrc = pTmpProvider->PrepareUninstall();
                pTmpProvider->AddRef();
                uRefCnt = pTmpProvider->Release();
            }

            /* has PrepareUninstall uninited the provider? */
            if (SUCCEEDED(hrc) && uRefCnt == 1)
            {
                m_astrExtPackNames.erase(m_astrExtPackNames.begin() + (ssize_t)i);
                m_apCloudProviders.erase(m_apCloudProviders.begin() + (ssize_t)i);

                if (bstrProviderId.isNotEmpty())
                    m_pVirtualBox->i_onCloudProviderRegistered(bstrProviderId, FALSE);

                /* NB: not advancing loop index */
            }
            else
            {
                LogRel(("CloudProviderManager: provider '%s' blocks extpack uninstall, result=%Rhrc, refcount=%u\n",
                        strExtPackName.c_str(), hrc, uRefCnt));
                fRes = false;
                i++;
            }
        }

        if (fRes)
            m_mapCloudProviderManagers.erase(it);

        /**
         * Tell listeners we are done and they can re-read the new
         * list of providers.
         */
        m_pVirtualBox->i_onCloudProviderListChanged(FALSE);
    }

    return fRes;
}

void CloudProviderManager::i_addExtPack(IExtPack *aExtPack)
{
    HRESULT hrc;

    AssertReturnVoid(aExtPack);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    Bstr bstrExtPackName;
    aExtPack->COMGETTER(Name)(bstrExtPackName.asOutParam());
    Utf8Str strExtPackName(bstrExtPackName);

    /* get the extpack's cloud provider manager object if present */
    ComPtr<IUnknown> pObj;
    com::Guid idObj(COM_IIDOF(ICloudProviderManager));
    hrc = aExtPack->QueryObject(Bstr(idObj.toString()).raw(), pObj.asOutParam());
    if (FAILED(hrc))
        return;
    const ComPtr<ICloudProviderManager> pManager(pObj);
    if (pManager.isNull())
        return;

    /* get the list of cloud providers */
    SafeIfaceArray<ICloudProvider> apProvidersFromCurrExtPack;
    hrc = pManager->COMGETTER(Providers)(ComSafeArrayAsOutParam(apProvidersFromCurrExtPack));
    if (FAILED(hrc))
        return;
    if (apProvidersFromCurrExtPack.size() == 0)
        return;

    m_mapCloudProviderManagers[strExtPackName] = pManager;

    for (unsigned i = 0; i < apProvidersFromCurrExtPack.size(); i++)
    {
        const ComPtr<ICloudProvider> pProvider(apProvidersFromCurrExtPack[i]);
        if (!pProvider.isNull())
        {
            // Sanity check each cloud provider by forcing a QueryInterface call,
            // making sure that it implements the right interface.
            ComPtr<ICloudProvider> pProviderCheck;
            pProvider.queryInterfaceTo(pProviderCheck.asOutParam());
            if (!pProviderCheck.isNull()) /* ok, seems legit */
            {
                /* save the provider and the name of the extpack it came from */
                Assert(m_astrExtPackNames.size() == m_apCloudProviders.size());
                m_astrExtPackNames.push_back(strExtPackName);
                m_apCloudProviders.push_back(pProvider);

                Bstr bstrProviderId;
                pProvider->COMGETTER(Id)(bstrProviderId.asOutParam());
                if (bstrProviderId.isNotEmpty())
                    m_pVirtualBox->i_onCloudProviderRegistered(bstrProviderId, TRUE);
            }
        }
    }

    /**
     * Tell listeners we are done and they can re-read the new list of
     * providers.
     */
    m_pVirtualBox->i_onCloudProviderListChanged(TRUE);
}

#endif  /* VBOX_WITH_EXTPACK */

HRESULT CloudProviderManager::getProviders(std::vector<ComPtr<ICloudProvider> > &aProviders)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aProviders = m_apCloudProviders;
    return S_OK;
}

HRESULT CloudProviderManager::getProviderById(const com::Guid &aProviderId,
                                              ComPtr<ICloudProvider> &aProvider)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    for (size_t i = 0; i < m_apCloudProviders.size(); i++)
    {
        Bstr bstrId;
        HRESULT hrc = m_apCloudProviders[i]->COMGETTER(Id)(bstrId.asOutParam());
        if (SUCCEEDED(hrc) && aProviderId == bstrId)
        {
            aProvider = m_apCloudProviders[i];
            return S_OK;
        }
    }
    return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a cloud provider with UUID {%RTuuid}"),
                    aProviderId.raw());
}

HRESULT CloudProviderManager::getProviderByShortName(const com::Utf8Str &aProviderName,
                                                     ComPtr<ICloudProvider> &aProvider)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    for (size_t i = 0; i < m_apCloudProviders.size(); i++)
    {
        Bstr bstrName;
        HRESULT hrc = m_apCloudProviders[i]->COMGETTER(ShortName)(bstrName.asOutParam());
        if (SUCCEEDED(hrc) && bstrName.equals(aProviderName))
        {
            aProvider = m_apCloudProviders[i];
            return S_OK;
        }
    }
    return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a cloud provider with short name '%s'"),
                    aProviderName.c_str());
}

HRESULT CloudProviderManager::getProviderByName(const com::Utf8Str &aProviderName,
                                                ComPtr<ICloudProvider> &aProvider)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    for (size_t i = 0; i < m_apCloudProviders.size(); i++)
    {
        Bstr bstrName;
        HRESULT hrc = m_apCloudProviders[i]->COMGETTER(Name)(bstrName.asOutParam());
        if (SUCCEEDED(hrc) && bstrName.equals(aProviderName))
        {
            aProvider = m_apCloudProviders[i];
            return S_OK;
        }
    }
    return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a cloud provider with name '%s'"),
                    aProviderName.c_str());
}

