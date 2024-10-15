/* $Id: VBoxCredProvFactory.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredProvFactory_h
#define GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredProvFactory_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>

class VBoxCredProvFactory : public IClassFactory
{
private:
    /*
     * Make the constructors / destructors private so that
     * this class cannot be instanciated directly by non-friends.
     */
    VBoxCredProvFactory(void);
    virtual ~VBoxCredProvFactory(void);

public:
    /** @name IUnknown methods.
     * @{ */
    IFACEMETHODIMP_(ULONG) AddRef(void);
    IFACEMETHODIMP_(ULONG) Release(void);
    IFACEMETHODIMP         QueryInterface(REFIID interfaceID, void **ppvInterface);
    /** @} */

    /** @name IClassFactory methods.
     * @{ */
    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID interfaceID, void **ppvInterface);
    IFACEMETHODIMP LockServer(BOOL fLock);
    /** @} */

private:
    LONG m_cRefs;
    friend HRESULT VBoxCredentialProviderCreate(REFCLSID classID, REFIID interfaceID, void **ppvInterface);
};
#endif /* !GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredProvFactory_h */

