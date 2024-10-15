/* $Id: USBDeviceFiltersImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of IUSBDeviceFilters.
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_USBDEVICEFILTERS
#include "USBDeviceFiltersImpl.h"

#include "Global.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"
#include "HostImpl.h"
#ifdef VBOX_WITH_USB
# include "USBDeviceImpl.h"
# include "HostUSBDeviceImpl.h"
# include "USBProxyService.h"
# include "USBDeviceFilterImpl.h"
#endif

#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <iprt/errcore.h>
#include <VBox/settings.h>
#include <VBox/com/array.h>

#include <algorithm>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

// defines
/////////////////////////////////////////////////////////////////////////////

typedef std::list< ComObjPtr<USBDeviceFilter> > DeviceFilterList;

struct USBDeviceFilters::Data
{
    Data(Machine *pMachine)
        : pParent(pMachine),
          pHost(pMachine->i_getVirtualBox()->i_host())
    { }

    ~Data()
    {};

    Machine * const                 pParent;
    Host * const                    pHost;

    // peer machine's USB device filters list
    const ComObjPtr<USBDeviceFilters>  pPeer;

#ifdef VBOX_WITH_USB
    // List of device filters.
    Backupable<DeviceFilterList>    llDeviceFilters;
#endif
};



// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(USBDeviceFilters)

HRESULT USBDeviceFilters::FinalConstruct()
{
    return BaseFinalConstruct();
}

void USBDeviceFilters::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the USB controller object.
 *
 * @returns COM result indicator.
 * @param aParent       Pointer to our parent object.
 */
HRESULT USBDeviceFilters::init(Machine *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* mPeer is left null */
#ifdef VBOX_WITH_USB
    m->llDeviceFilters.allocate();
#endif

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the USB devic filters object given another USB filters object
 * (a kind of copy constructor). This object shares data with
 * the object passed as an argument.
 *
 * @returns COM result indicator.
 * @param aParent       Pointer to our parent object.
 * @param aPeer         The object to share.
 *
 * @note This object must be destroyed before the original object
 * it shares data with is destroyed.
 */
HRESULT USBDeviceFilters::init(Machine *aParent, USBDeviceFilters *aPeer)
{
    LogFlowThisFunc(("aParent=%p, aPeer=%p\n", aParent, aPeer));

    ComAssertRet(aParent && aPeer, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    unconst(m->pPeer) = aPeer;

    AutoWriteLock thatlock(aPeer COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB
    /* create copies of all filters */
    m->llDeviceFilters.allocate();
    DeviceFilterList::const_iterator it = aPeer->m->llDeviceFilters->begin();
    while (it != aPeer->m->llDeviceFilters->end())
    {
        ComObjPtr<USBDeviceFilter> pFilter;
        pFilter.createObject();
        pFilter->init(this, *it);
        m->llDeviceFilters->push_back(pFilter);
        ++it;
    }
#endif /* VBOX_WITH_USB */

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


/**
 *  Initializes the USB controller object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT USBDeviceFilters::initCopy(Machine *aParent, USBDeviceFilters *aPeer)
{
    LogFlowThisFunc(("aParent=%p, aPeer=%p\n", aParent, aPeer));

    ComAssertRet(aParent && aPeer, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data(aParent);

    /* mPeer is left null */

    AutoWriteLock thatlock(aPeer COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB
    /* create private copies of all filters */
    m->llDeviceFilters.allocate();
    DeviceFilterList::const_iterator it = aPeer->m->llDeviceFilters->begin();
    while (it != aPeer->m->llDeviceFilters->end())
    {
        ComObjPtr<USBDeviceFilter> pFilter;
        pFilter.createObject();
        pFilter->initCopy(this, *it);
        m->llDeviceFilters->push_back(pFilter);
        ++it;
    }
#endif /* VBOX_WITH_USB */

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void USBDeviceFilters::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_USB
    // uninit all device filters on the list (it's a standard std::list not an ObjectsList
    // so we must uninit() manually)
    for (DeviceFilterList::iterator it = m->llDeviceFilters->begin();
         it != m->llDeviceFilters->end();
         ++it)
        (*it)->uninit();

    m->llDeviceFilters.free();
#endif

    unconst(m->pPeer) = NULL;
    unconst(m->pParent) = NULL;

    delete m;
    m = NULL;
}


// IUSBDeviceFilters properties
/////////////////////////////////////////////////////////////////////////////

#ifndef VBOX_WITH_USB
/**
 * Fake class for build without USB.
 * We need an empty collection & enum for deviceFilters, that's all.
 */
class ATL_NO_VTABLE USBDeviceFilter :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IUSBDeviceFilter)
{
public:
    DECLARE_NOT_AGGREGATABLE(USBDeviceFilter)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(USBDeviceFilter)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IUSBDeviceFilter)
        COM_INTERFACE_ENTRY2(IDispatch, IUSBDeviceFilter)
        VBOX_TWEAK_INTERFACE_ENTRY(IUSBDeviceFilter)
    END_COM_MAP()

    DECLARE_COMMON_CLASS_METHODS(USBDeviceFilter)

    // IUSBDeviceFilter properties
    STDMETHOD(COMGETTER(Name))(BSTR *aName);
    STDMETHOD(COMSETTER(Name))(IN_BSTR aName);
    STDMETHOD(COMGETTER(Active))(BOOL *aActive);
    STDMETHOD(COMSETTER(Active))(BOOL aActive);
    STDMETHOD(COMGETTER(VendorId))(BSTR *aVendorId);
    STDMETHOD(COMSETTER(VendorId))(IN_BSTR aVendorId);
    STDMETHOD(COMGETTER(ProductId))(BSTR *aProductId);
    STDMETHOD(COMSETTER(ProductId))(IN_BSTR aProductId);
    STDMETHOD(COMGETTER(Revision))(BSTR *aRevision);
    STDMETHOD(COMSETTER(Revision))(IN_BSTR aRevision);
    STDMETHOD(COMGETTER(Manufacturer))(BSTR *aManufacturer);
    STDMETHOD(COMSETTER(Manufacturer))(IN_BSTR aManufacturer);
    STDMETHOD(COMGETTER(Product))(BSTR *aProduct);
    STDMETHOD(COMSETTER(Product))(IN_BSTR aProduct);
    STDMETHOD(COMGETTER(SerialNumber))(BSTR *aSerialNumber);
    STDMETHOD(COMSETTER(SerialNumber))(IN_BSTR aSerialNumber);
    STDMETHOD(COMGETTER(Port))(BSTR *aPort);
    STDMETHOD(COMSETTER(Port))(IN_BSTR aPort);
    STDMETHOD(COMGETTER(Remote))(BSTR *aRemote);
    STDMETHOD(COMSETTER(Remote))(IN_BSTR aRemote);
    STDMETHOD(COMGETTER(MaskedInterfaces))(ULONG *aMaskedIfs);
    STDMETHOD(COMSETTER(MaskedInterfaces))(ULONG aMaskedIfs);
};
#endif /* !VBOX_WITH_USB */


HRESULT USBDeviceFilters::getDeviceFilters(std::vector<ComPtr<IUSBDeviceFilter> > &aDeviceFilters)
{
#ifdef VBOX_WITH_USB
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDeviceFilters.resize(m->llDeviceFilters.data()->size());
    std::copy(m->llDeviceFilters.data()->begin(), m->llDeviceFilters.data()->end(), aDeviceFilters.begin());

    return S_OK;
#else
    NOREF(aDeviceFilters);
# ifndef RT_OS_WINDOWS
    NOREF(aDeviceFilters);
# endif
    ReturnComNotImplemented();
#endif
}

// wrapped IUSBDeviceFilters methods
/////////////////////////////////////////////////////////////////////////////

HRESULT USBDeviceFilters::createDeviceFilter(const com::Utf8Str &aName,
                                             ComPtr<IUSBDeviceFilter> &aFilter)

{
#ifdef VBOX_WITH_USB

    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<USBDeviceFilter> pFilter;
    pFilter.createObject();
    HRESULT hrc = pFilter->init(this, Bstr(aName).raw());
    ComAssertComRCRetRC(hrc);
    hrc = pFilter.queryInterfaceTo(aFilter.asOutParam());
    AssertComRCReturnRC(hrc);

    return S_OK;
#else
    NOREF(aName);
    NOREF(aFilter);
    ReturnComNotImplemented();
#endif
}


HRESULT USBDeviceFilters::insertDeviceFilter(ULONG aPosition,
                                             const ComPtr<IUSBDeviceFilter> &aFilter)
{
#ifdef VBOX_WITH_USB

    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    IUSBDeviceFilter *iFilter = aFilter;
    ComObjPtr<USBDeviceFilter> pFilter = static_cast<USBDeviceFilter*>(iFilter);

    if (pFilter->mInList)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("The given USB device pFilter is already in the list"));

    /* backup the list before modification */
    m->llDeviceFilters.backup();

    /* iterate to the position... */
    DeviceFilterList::iterator it;
    if (aPosition < m->llDeviceFilters->size())
    {
        it = m->llDeviceFilters->begin();
        std::advance(it, aPosition);
    }
    else
        it = m->llDeviceFilters->end();
    /* ...and insert */
    m->llDeviceFilters->insert(it, pFilter);
    pFilter->mInList = true;

    /* notify the proxy (only when it makes sense) */
    if (pFilter->i_getData().mData.fActive && Global::IsOnline(adep.machineState())
        && pFilter->i_getData().mRemote.isMatch(false))
    {
        USBProxyService *pProxySvc = m->pHost->i_usbProxyService();
        ComAssertRet(pProxySvc, E_FAIL);

        ComAssertRet(pFilter->i_getId() == NULL, E_FAIL);
        pFilter->i_getId() = pProxySvc->insertFilter(&pFilter->i_getData().mUSBFilter);
    }

    alock.release();
    AutoWriteLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    m->pParent->i_setModified(Machine::IsModified_USB);
    mlock.release();

    return S_OK;

#else /* VBOX_WITH_USB */

    NOREF(aPosition);
    NOREF(aFilter);
    ReturnComNotImplemented();

#endif /* VBOX_WITH_USB */
}

HRESULT USBDeviceFilters::removeDeviceFilter(ULONG aPosition,
                                             ComPtr<IUSBDeviceFilter> &aFilter)
{
#ifdef VBOX_WITH_USB
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!m->llDeviceFilters->size())
        return setError(E_INVALIDARG,
                        tr("The USB device pFilter list is empty"));

    if (aPosition >= m->llDeviceFilters->size())
        return setError(E_INVALIDARG,
                        tr("Invalid position: %lu (must be in range [0, %lu])"),
                        aPosition, m->llDeviceFilters->size() - 1);

    /* backup the list before modification */
    m->llDeviceFilters.backup();

    ComObjPtr<USBDeviceFilter> pFilter;
    {
        /* iterate to the position... */
        DeviceFilterList::iterator it = m->llDeviceFilters->begin();
        std::advance(it, aPosition);
        /* ...get an element from there... */
        pFilter = *it;
        /* ...and remove */
        pFilter->mInList = false;
        m->llDeviceFilters->erase(it);
    }

    /* cancel sharing (make an independent copy of data) */
    pFilter->unshare();
    pFilter.queryInterfaceTo(aFilter.asOutParam());


    /* notify the proxy (only when it makes sense) */
    if (pFilter->i_getData().mData.fActive && Global::IsOnline(adep.machineState())
        && pFilter->i_getData().mRemote.isMatch(false))
    {
        USBProxyService *pProxySvc = m->pHost->i_usbProxyService();
        ComAssertRet(pProxySvc, E_FAIL);

        ComAssertRet(pFilter->i_getId() != NULL, E_FAIL);
        pProxySvc->removeFilter(pFilter->i_getId());
        pFilter->i_getId() = NULL;
    }

    alock.release();
    AutoWriteLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    m->pParent->i_setModified(Machine::IsModified_USB);
    mlock.release();

    return S_OK;

#else /* VBOX_WITH_USB */

    NOREF(aPosition);
    NOREF(aFilter);
    ReturnComNotImplemented();

#endif /* VBOX_WITH_USB */
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given machine node.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Does not lock "this" as Machine::loadHardware, which calls this, does not lock either.
 */
HRESULT USBDeviceFilters::i_loadSettings(const settings::USB &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* Note: we assume that the default values for attributes of optional
     * nodes are assigned in the Data::Data() constructor and don't do it
     * here. It implies that this method may only be called after constructing
     * a new USBDeviceFilters object while all its data fields are in the default
     * values. Exceptions are fields whose creation time defaults don't match
     * values that should be applied when these fields are not explicitly set
     * in the settings file (for backwards compatibility reasons). This takes
     * place when a setting of a newly created object must default to A while
     * the same setting of an object loaded from the old settings file must
     * default to B. */

#ifdef VBOX_WITH_USB
    for (settings::USBDeviceFiltersList::const_iterator it = data.llDeviceFilters.begin();
         it != data.llDeviceFilters.end();
         ++it)
    {
        const settings::USBDeviceFilter &f = *it;
        ComObjPtr<USBDeviceFilter> pFilter;
        pFilter.createObject();
        HRESULT hrc = pFilter->init(this /*aParent*/, f);
        if (FAILED(hrc)) return hrc;

        m->llDeviceFilters->push_back(pFilter);
        pFilter->mInList = true;
    }
#else
    RT_NOREF(data);
#endif /* VBOX_WITH_USB */

    return S_OK;
}

/**
 *  Saves settings to the given machine node.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT USBDeviceFilters::i_saveSettings(settings::USB &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB
    data.llDeviceFilters.clear();

    for (DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
         it != m->llDeviceFilters->end();
         ++it)
    {
        AutoWriteLock filterLock(*it COMMA_LOCKVAL_SRC_POS);
        const USBDeviceFilter::BackupableUSBDeviceFilterData &filterData = (*it)->i_getData();

        Bstr str;

        settings::USBDeviceFilter f;
        f.strName = filterData.mData.strName;
        f.fActive = !!filterData.mData.fActive;
        (*it)->COMGETTER(VendorId)(str.asOutParam());
        f.strVendorId = str;
        (*it)->COMGETTER(ProductId)(str.asOutParam());
        f.strProductId = str;
        (*it)->COMGETTER(Revision)(str.asOutParam());
        f.strRevision = str;
        (*it)->COMGETTER(Manufacturer)(str.asOutParam());
        f.strManufacturer = str;
        (*it)->COMGETTER(Product)(str.asOutParam());
        f.strProduct = str;
        (*it)->COMGETTER(SerialNumber)(str.asOutParam());
        f.strSerialNumber = str;
        (*it)->COMGETTER(Port)(str.asOutParam());
        f.strPort = str;
        f.strRemote = filterData.mRemote.string();
        f.ulMaskedInterfaces = filterData.mData.ulMaskedInterfaces;

        data.llDeviceFilters.push_back(f);
    }
#else
    RT_NOREF(data);
#endif /* VBOX_WITH_USB */

    return S_OK;
}

/** @note Locks objects for writing! */
void USBDeviceFilters::i_rollback()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* we need the machine state */
    AutoAnyStateDependency adep(m->pParent);
    AssertComRCReturnVoid(adep.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB

    if (m->llDeviceFilters.isBackedUp())
    {
        USBProxyService *pProxySvc = m->pHost->i_usbProxyService();
        Assert(pProxySvc);

        /* uninitialize all new filters (absent in the backed up list) */
        DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
        DeviceFilterList *backedList = m->llDeviceFilters.backedUpData();
        while (it != m->llDeviceFilters->end())
        {
            if (std::find(backedList->begin(), backedList->end(), *it) ==
                backedList->end())
            {
                /* notify the proxy (only when it makes sense) */
                if ((*it)->i_getData().mData.fActive &&
                    Global::IsOnline(adep.machineState())
                    && (*it)->i_getData().mRemote.isMatch(false))
                {
                    USBDeviceFilter *pFilter = *it;
                    Assert(pFilter->i_getId() != NULL);
                    pProxySvc->removeFilter(pFilter->i_getId());
                    pFilter->i_getId() = NULL;
                }

                (*it)->uninit();
            }
            ++it;
        }

        if (Global::IsOnline(adep.machineState()))
        {
            /* find all removed old filters (absent in the new list)
             * and insert them back to the USB proxy */
            it = backedList->begin();
            while (it != backedList->end())
            {
                if (std::find(m->llDeviceFilters->begin(), m->llDeviceFilters->end(), *it) ==
                    m->llDeviceFilters->end())
                {
                    /* notify the proxy (only when necessary) */
                    if ((*it)->i_getData().mData.fActive
                            && (*it)->i_getData().mRemote.isMatch(false))
                    {
                        USBDeviceFilter *pFilter = *it; /* resolve ambiguity */
                        Assert(pFilter->i_getId() == NULL);
                        pFilter->i_getId() = pProxySvc->insertFilter(&pFilter->i_getData().mUSBFilter);
                    }
                }
                ++it;
            }
        }

        /* restore the list */
        m->llDeviceFilters.rollback();
    }

    /* here we don't depend on the machine state any more */
    adep.release();

    /* rollback any changes to filters after restoring the list */
    DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
    while (it != m->llDeviceFilters->end())
    {
        if ((*it)->i_isModified())
        {
            (*it)->i_rollback();
            /* call this to notify the USB proxy about changes */
            i_onDeviceFilterChange(*it);
        }
        ++it;
    }

#endif /* VBOX_WITH_USB */
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void USBDeviceFilters::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB
    bool commitFilters = false;

    if (m->llDeviceFilters.isBackedUp())
    {
        m->llDeviceFilters.commit();

        /* apply changes to peer */
        if (m->pPeer)
        {
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);

            /* commit all changes to new filters (this will reshare data with
             * peers for those who have peers) */
            DeviceFilterList *newList = new DeviceFilterList();
            DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
            while (it != m->llDeviceFilters->end())
            {
                (*it)->i_commit();

                /* look if this filter has a peer filter */
                ComObjPtr<USBDeviceFilter> peer = (*it)->i_peer();
                if (!peer)
                {
                    /* no peer means the filter is a newly created one;
                     * create a peer owning data this filter share it with */
                    peer.createObject();
                    peer->init(m->pPeer, *it, true /* aReshare */);
                }
                else
                {
                    /* remove peer from the old list */
                    m->pPeer->m->llDeviceFilters->remove(peer);
                }
                /* and add it to the new list */
                newList->push_back(peer);

                ++it;
            }

            /* uninit old peer's filters that are left */
            it = m->pPeer->m->llDeviceFilters->begin();
            while (it != m->pPeer->m->llDeviceFilters->end())
            {
                (*it)->uninit();
                ++it;
            }

            /* attach new list of filters to our peer */
            m->pPeer->m->llDeviceFilters.attach(newList);
        }
        else
        {
            /* we have no peer (our parent is the newly created machine);
             * just commit changes to filters */
            commitFilters = true;
        }
    }
    else
    {
        /* the list of filters itself is not changed,
         * just commit changes to filters themselves */
        commitFilters = true;
    }

    if (commitFilters)
    {
        DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
        while (it != m->llDeviceFilters->end())
        {
            (*it)->i_commit();
            ++it;
        }
    }
#endif /* VBOX_WITH_USB */
}

/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void USBDeviceFilters::i_copyFrom(USBDeviceFilters *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* even more sanity */
    AutoAnyStateDependency adep(m->pParent);
    AssertComRCReturnVoid(adep.hrc());
    /* Machine::copyFrom() may not be called when the VM is running */
    AssertReturnVoid(!Global::IsOnline(adep.machineState()));

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_USB

    /* Note that we won't inform the USB proxy about new filters since the VM is
     * not running when we are here and therefore no need to do so */

    /* create private copies of all filters */
    m->llDeviceFilters.backup();
    m->llDeviceFilters->clear();
    for (DeviceFilterList::const_iterator it = aThat->m->llDeviceFilters->begin();
        it != aThat->m->llDeviceFilters->end();
        ++it)
    {
        ComObjPtr<USBDeviceFilter> pFilter;
        pFilter.createObject();
        pFilter->initCopy(this, *it);
        m->llDeviceFilters->push_back(pFilter);
    }

#endif /* VBOX_WITH_USB */
}

#ifdef VBOX_WITH_USB

/**
 *  Called by setter methods of all USB device filters.
 *
 *  @note Locks nothing.
 */
HRESULT USBDeviceFilters::i_onDeviceFilterChange(USBDeviceFilter *aFilter,
                                                 BOOL aActiveChanged /* = FALSE */)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* we need the machine state */
    AutoAnyStateDependency adep(m->pParent);
    AssertComRCReturnRC(adep.hrc());

    /* nothing to do if the machine isn't running */
    if (!Global::IsOnline(adep.machineState()))
        return S_OK;

    /* we don't modify our data fields -- no need to lock */

    if (    aFilter->mInList
         && m->pParent->i_isRegistered())
    {
        USBProxyService *pProxySvc = m->pHost->i_usbProxyService();
        ComAssertRet(pProxySvc, E_FAIL);

        if (aActiveChanged)
        {
            if (aFilter->i_getData().mRemote.isMatch(false))
            {
                /* insert/remove the filter from the proxy */
                if (aFilter->i_getData().mData.fActive)
                {
                    ComAssertRet(aFilter->i_getId() == NULL, E_FAIL);
                    aFilter->i_getId() = pProxySvc->insertFilter(&aFilter->i_getData().mUSBFilter);
                }
                else
                {
                    ComAssertRet(aFilter->i_getId() != NULL, E_FAIL);
                    pProxySvc->removeFilter(aFilter->i_getId());
                    aFilter->i_getId() = NULL;
                }
            }
        }
        else
        {
            if (aFilter->i_getData().mData.fActive)
            {
                /* update the filter in the proxy */
                ComAssertRet(aFilter->i_getId() != NULL, E_FAIL);
                pProxySvc->removeFilter(aFilter->i_getId());
                if (aFilter->i_getData().mRemote.isMatch(false))
                {
                    aFilter->i_getId() = pProxySvc->insertFilter(&aFilter->i_getData().mUSBFilter);
                }
            }
        }
    }

    return S_OK;
}

/**
 *  Returns true if the given USB device matches to at least one of
 *  this controller's USB device filters.
 *
 *  A HostUSBDevice specific version.
 *
 *  @note Locks this object for reading.
 */
bool USBDeviceFilters::i_hasMatchingFilter(const ComObjPtr<HostUSBDevice> &aDevice, ULONG *aMaskedIfs)
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), false);

    /* It is not possible to work with USB device if there is no USB controller present. */
    if (!m->pParent->i_isUSBControllerPresent())
        return false;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* apply self filters */
    for (DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
         it != m->llDeviceFilters->end();
         ++it)
    {
        AutoWriteLock filterLock(*it COMMA_LOCKVAL_SRC_POS);
        if (aDevice->i_isMatch((*it)->i_getData()))
        {
            *aMaskedIfs = (*it)->i_getData().mData.ulMaskedInterfaces;
            return true;
        }
    }

    return false;
}

/**
 *  Returns true if the given USB device matches to at least one of
 *  this controller's USB device filters.
 *
 *  A generic version that accepts any IUSBDevice on input.
 *
 *  @note
 *      This method MUST correlate with HostUSBDevice::isMatch()
 *      in the sense of the device matching logic.
 *
 *  @note Locks this object for reading.
 */
bool USBDeviceFilters::i_hasMatchingFilter(IUSBDevice *aUSBDevice, ULONG *aMaskedIfs)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), false);

    /* It is not possible to work with USB device if there is no USB controller present. */
    if (!m->pParent->i_isUSBControllerPresent())
        return false;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* query fields */
    USBFILTER dev;
    USBFilterInit(&dev, USBFILTERTYPE_CAPTURE);

    USHORT vendorId = 0;
    HRESULT hrc = aUSBDevice->COMGETTER(VendorId)(&vendorId);
    ComAssertComRCRet(hrc, false);
    ComAssertRet(vendorId, false);
    int vrc = USBFilterSetNumExact(&dev, USBFILTERIDX_VENDOR_ID, vendorId, true); AssertRC(vrc);

    USHORT productId = 0;
    hrc = aUSBDevice->COMGETTER(ProductId)(&productId);
    ComAssertComRCRet(hrc, false);
    vrc = USBFilterSetNumExact(&dev, USBFILTERIDX_PRODUCT_ID, productId, true); AssertRC(vrc);

    USHORT revision;
    hrc = aUSBDevice->COMGETTER(Revision)(&revision);
    ComAssertComRCRet(hrc, false);
    vrc = USBFilterSetNumExact(&dev, USBFILTERIDX_DEVICE, revision, true); AssertRC(vrc);

    Bstr manufacturer;
    hrc = aUSBDevice->COMGETTER(Manufacturer)(manufacturer.asOutParam());
    ComAssertComRCRet(hrc, false);
    if (!manufacturer.isEmpty())
        USBFilterSetStringExact(&dev, USBFILTERIDX_MANUFACTURER_STR, Utf8Str(manufacturer).c_str(),
                                true /*fMustBePresent*/, false /*fPurge*/);

    Bstr product;
    hrc = aUSBDevice->COMGETTER(Product)(product.asOutParam());
    ComAssertComRCRet(hrc, false);
    if (!product.isEmpty())
        USBFilterSetStringExact(&dev, USBFILTERIDX_PRODUCT_STR, Utf8Str(product).c_str(),
                                true /*fMustBePresent*/, false /*fPurge*/);

    Bstr serialNumber;
    hrc = aUSBDevice->COMGETTER(SerialNumber)(serialNumber.asOutParam());
    ComAssertComRCRet(hrc, false);
    if (!serialNumber.isEmpty())
        USBFilterSetStringExact(&dev, USBFILTERIDX_SERIAL_NUMBER_STR, Utf8Str(serialNumber).c_str(),
                                true /*fMustBePresent*/, false /*fPurge*/);

    Bstr address;
    hrc = aUSBDevice->COMGETTER(Address)(address.asOutParam());
    ComAssertComRCRet(hrc, false);

    USHORT port = 0;
    hrc = aUSBDevice->COMGETTER(Port)(&port);
    ComAssertComRCRet(hrc, false);
    USBFilterSetNumExact(&dev, USBFILTERIDX_PORT, port, true);

    BOOL remote = FALSE;
    hrc = aUSBDevice->COMGETTER(Remote)(&remote);
    ComAssertComRCRet(hrc, false);
    ComAssertRet(remote == TRUE, false);

    bool match = false;

    /* apply self filters */
    for (DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
         it != m->llDeviceFilters->end();
         ++it)
    {
        AutoWriteLock filterLock(*it COMMA_LOCKVAL_SRC_POS);
        const USBDeviceFilter::BackupableUSBDeviceFilterData &aData = (*it)->i_getData();

        if (!aData.mData.fActive)
            continue;
        if (!aData.mRemote.isMatch(remote))
            continue;
        if (!USBFilterMatch(&aData.mUSBFilter, &dev))
            continue;

        match = true;
        *aMaskedIfs = aData.mData.ulMaskedInterfaces;
        break;
    }

    LogFlowThisFunc(("returns: %d\n", match));
    LogFlowThisFuncLeave();

    return match;
}

/**
 *  Notifies the proxy pProxySvc about all filters as requested by the
 *  @a aInsertFilters argument.
 *
 *  @param aInsertFilters   @c true to insert filters, @c false to remove.
 *
 *  @note Locks this object for reading.
 */
HRESULT USBDeviceFilters::i_notifyProxy(bool aInsertFilters)
{
    LogFlowThisFunc(("aInsertFilters=%RTbool\n", aInsertFilters));

    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), false);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    USBProxyService *pProxySvc = m->pHost->i_usbProxyService();
    AssertReturn(pProxySvc, E_FAIL);

    DeviceFilterList::const_iterator it = m->llDeviceFilters->begin();
    while (it != m->llDeviceFilters->end())
    {
        USBDeviceFilter *pFilter = *it; /* resolve ambiguity (for ComPtr below) */

        /* notify the proxy (only if the filter is active) */
        if (   pFilter->i_getData().mData.fActive
            && pFilter->i_getData().mRemote.isMatch(false) /* and if the filter is NOT remote */
           )
        {
            if (aInsertFilters)
            {
                AssertReturn(pFilter->i_getId() == NULL, E_FAIL);
                pFilter->i_getId() = pProxySvc->insertFilter(&pFilter->i_getData().mUSBFilter);
            }
            else
            {
                /* It's possible that the given filter was not inserted the proxy
                 * when this method gets called (as a result of an early VM
                 * process crash for example. So, don't assert that ID != NULL. */
                if (pFilter->i_getId() != NULL)
                {
                    pProxySvc->removeFilter(pFilter->i_getId());
                    pFilter->i_getId() = NULL;
                }
            }
        }
        ++it;
    }

    return S_OK;
}

Machine* USBDeviceFilters::i_getMachine()
{
    return m->pParent;
}

#endif /* VBOX_WITH_USB */

// private methods
/////////////////////////////////////////////////////////////////////////////
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
