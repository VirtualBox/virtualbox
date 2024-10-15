/* $Id: VBoxCredProvFactory.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCredentialProvFactory - The VirtualBox Credential Provider Factory.
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
#include "VBoxCredentialProvider.h"
#include "VBoxCredProvFactory.h"
#include "VBoxCredProvProvider.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
extern HRESULT VBoxCredProvProviderCreate(REFIID interfaceID, void **ppvInterface);


VBoxCredProvFactory::VBoxCredProvFactory(void) :
    m_cRefs(1) /* Start with one instance. */
{
}

VBoxCredProvFactory::~VBoxCredProvFactory(void)
{
}

ULONG
VBoxCredProvFactory::AddRef(void)
{
    LONG cRefs = InterlockedIncrement(&m_cRefs);
    VBoxCredProvVerbose(0, "VBoxCredProvFactory: AddRef: Returning refcount=%ld\n",
                        cRefs);
    return cRefs;
}

ULONG
VBoxCredProvFactory::Release(void)
{
    LONG cRefs = InterlockedDecrement(&m_cRefs);
    VBoxCredProvVerbose(0, "VBoxCredProvFactory: Release: Returning refcount=%ld\n",
                        cRefs);
    if (!cRefs)
    {
        VBoxCredProvVerbose(0, "VBoxCredProvFactory: Calling destructor\n");
        delete this;
    }
    return cRefs;
}

HRESULT
VBoxCredProvFactory::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    VBoxCredProvVerbose(0, "VBoxCredProvFactory: QueryInterface\n");

    HRESULT hr = S_OK;
    if (ppvInterface)
    {
        if (   IID_IClassFactory == interfaceID
            || IID_IUnknown      == interfaceID)
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

HRESULT
VBoxCredProvFactory::CreateInstance(IUnknown *pUnkOuter, REFIID interfaceID, void **ppvInterface)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;
    return VBoxCredProvProviderCreate(interfaceID, ppvInterface);
}

HRESULT
VBoxCredProvFactory::LockServer(BOOL fLock)
{
    if (fLock)
        VBoxCredentialProviderAcquire();
    else
        VBoxCredentialProviderRelease();
    return S_OK;
}

