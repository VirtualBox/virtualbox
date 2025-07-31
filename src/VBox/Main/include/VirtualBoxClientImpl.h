/* $Id: VirtualBoxClientImpl.h 110372 2025-07-23 10:03:33Z klaus.espenlaub@oracle.com $ */
/** @file
 * Header file for the VirtualBoxClient (IVirtualBoxClient) class, VBoxC.
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

#ifndef MAIN_INCLUDED_VirtualBoxClientImpl_h
#define MAIN_INCLUDED_VirtualBoxClientImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxClientWrap.h"
#include "EventImpl.h"
#include "VirtualBoxTranslator.h"
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
# include "ObjectsTracker.h"
#endif

#ifdef RT_OS_WINDOWS
# include "win/resource.h"
#endif

class ATL_NO_VTABLE VirtualBoxClient :
    public VirtualBoxClientWrap
#ifdef RT_OS_WINDOWS
    , public ATL::CComCoClass<VirtualBoxClient, &CLSID_VirtualBoxClient>
#endif
{
public:
    DECLARE_CLASSFACTORY_SINGLETON(VirtualBoxClient)

    // Do not use any ATL registry support.
    //DECLARE_REGISTRY_RESOURCEID(IDR_VIRTUALBOX)

    DECLARE_NOT_AGGREGATABLE(VirtualBoxClient)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init();
    void uninit();

#ifdef RT_OS_WINDOWS
    /* HACK ALERT! Implemented in dllmain.cpp. */
    ULONG InternalRelease();
#endif

private:
    // wrapped IVirtualBoxClient properties
    virtual HRESULT getVirtualBox(ComPtr<IVirtualBox> &aVirtualBox);
    virtual HRESULT getSession(ComPtr<ISession> &aSession);
    virtual HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);

    // wrapped IVirtualBoxClient methods
    virtual HRESULT checkMachineError(const ComPtr<IMachine> &aMachine);

    /** Instance counter for simulating something similar to a singleton.
     * Only the first instance will be a usable object, all additional
     * instances will return a failure at creation time and will not work. */
    static uint32_t g_cInstances;

#ifdef RT_OS_WINDOWS
    virtual HRESULT i_investigateVirtualBoxObjectCreationFailure(HRESULT hrc);
#endif

#ifdef VBOX_WITH_SDS
    int     i_getServiceAccountAndStartType(const wchar_t *pwszServiceName,
                                            wchar_t *pwszAccountName, size_t cwcAccountName, uint32_t *puStartType);
#endif

    static DECLCALLBACK(int) SVCWatcherThread(RTTHREAD ThreadSelf, void *pvUser);

    struct Data
    {
        Data()
            : m_ThreadWatcher(NIL_RTTHREAD)
            , m_SemEvWatcher(NIL_RTSEMEVENT)
#ifdef VBOX_WITH_MAIN_NLS
            , m_pVBoxTranslator(NULL)
            , m_pTrComponent(NULL)
#endif
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
            , m_objectTrackerTask(NULL)
#endif
        {}

        ~Data()
        {
            /* HACK ALERT! This is for DllCanUnloadNow(). */
            if (m_pEventSource.isNotNull())
            {
                s_cUnnecessaryAtlModuleLocks--;
                AssertMsg(s_cUnnecessaryAtlModuleLocks == 0, ("%d\n", s_cUnnecessaryAtlModuleLocks));
            }
        }

        ComPtr<IVirtualBox> m_pVirtualBox;
        const ComObjPtr<EventSource> m_pEventSource;
        ComPtr<IEventSource> m_pVBoxEventSource;
        ComPtr<IEventListener> m_pVBoxEventListener;

        RTTHREAD m_ThreadWatcher;
        RTSEMEVENT m_SemEvWatcher;
#ifdef VBOX_WITH_MAIN_NLS
        VirtualBoxTranslator *m_pVBoxTranslator;
        PTRCOMPONENT          m_pTrComponent;
#endif

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
        ObjectTracker *m_objectTrackerTask;
#endif
    };

    Data mData;

public:
    /** Hack for discounting the AtlModule lock held by Data::m_pEventSource during
     * DllCanUnloadNow().  This is incremented to 1 when init() initialized
     * m_pEventSource and is decremented by the Data destructor (above). */
    static LONG s_cUnnecessaryAtlModuleLocks;

#ifdef VBOX_WITH_MAIN_NLS
    HRESULT i_reloadApiLanguage();
    HRESULT i_registerEventListener();
    void    i_unregisterEventListener();
#endif
};

#endif /* !MAIN_INCLUDED_VirtualBoxClientImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
