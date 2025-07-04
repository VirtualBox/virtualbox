/* $Id: HostDnsService.cpp 108156 2025-02-01 02:19:11Z knut.osmundsen@oracle.com $ */
/** @file
 * Base class for Host DNS & Co services.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOST
#include <VBox/com/array.h>
#include <VBox/com/ptr.h>
#include <VBox/com/string.h>

#include <iprt/cpp/utils.h>

#include "LoggingNew.h"
#include "VirtualBoxImpl.h"
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>

#include <algorithm>
#include <set>
#include "HostDnsService.h"



static void dumpHostDnsStrVector(const char *prefix, const std::vector<com::Utf8Str> &v)
{
    int i = 1;
    for (std::vector<com::Utf8Str>::const_iterator it = v.begin(); it != v.end(); ++it, ++i)
        LogRel(("  %s %d: %s\n", prefix, i, it->c_str()));
    if (v.empty())
        LogRel(("  no %s entries\n", prefix));
}

static void dumpHostDnsInformation(const HostDnsInformation &info)
{
    dumpHostDnsStrVector("server", info.servers);

    if (info.domain.isNotEmpty())
        LogRel(("  domain: %s\n", info.domain.c_str()));
    else
        LogRel(("  no domain set\n"));

    dumpHostDnsStrVector("search string", info.searchList);
}

bool HostDnsInformation::equals(const HostDnsInformation &info, uint32_t fLaxComparison) const
{
    bool fSameServers;
    if ((fLaxComparison & IGNORE_SERVER_ORDER) == 0)
        fSameServers = (servers == info.servers);
    else
    {
        std::set<com::Utf8Str> l(servers.begin(), servers.end());
        std::set<com::Utf8Str> r(info.servers.begin(), info.servers.end());

        fSameServers = (l == r);
    }

    bool fSameDomain, fSameSearchList;
    if ((fLaxComparison & IGNORE_SUFFIXES) == 0)
    {
        fSameDomain = (domain == info.domain);
        fSameSearchList = (searchList == info.searchList);
    }
    else
        fSameDomain = fSameSearchList = true;

    return fSameServers && fSameDomain && fSameSearchList;
}

struct HostDnsServiceBase::Data
{
    Data(bool aThreaded)
        : pProxy(NULL)
        , fThreaded(aThreaded)
        , hMonitorThreadEvent(NIL_RTSEMEVENT)
        , hMonitorThread(NIL_RTTHREAD)
    {}

    /** Weak pointer to parent proxy object. */
    HostDnsMonitorProxy *pProxy;
    /** Whether the DNS monitor implementation has a dedicated monitoring thread. Optional. */
    const bool           fThreaded;
    /** Event for the monitor thread, if any. */
    RTSEMEVENT           hMonitorThreadEvent;
    /** Handle of the monitor thread, if any. */
    RTTHREAD             hMonitorThread;
    /** Generic host DNS information. */
    HostDnsInformation   info;
};

struct HostDnsMonitorProxy::Data
{
    Data(HostDnsServiceBase *aMonitor, VirtualBox *aParent)
        : pVirtualBox(aParent)
        , pMonitorImpl(aMonitor)
        , uLastExtraDataPoll(0)
        , fLaxComparison(0)
        , info()
    {}

    VirtualBox *pVirtualBox;
    HostDnsServiceBase *pMonitorImpl;

    uint64_t uLastExtraDataPoll;
    uint32_t fLaxComparison;
    HostDnsInformation info;
};


HostDnsServiceBase::HostDnsServiceBase(bool fThreaded)
    : m(NULL)
{
    m = new HostDnsServiceBase::Data(fThreaded);
}

HostDnsServiceBase::~HostDnsServiceBase()
{
    if (m)
    {
        delete m;
        m = NULL;
    }
}

/* static */
HostDnsServiceBase *HostDnsServiceBase::createHostDnsMonitor(void)
{
    HostDnsServiceBase *pMonitor = NULL;

#if defined (RT_OS_DARWIN)
    pMonitor = new HostDnsServiceDarwin();
#elif defined(RT_OS_WINDOWS)
    pMonitor = new HostDnsServiceWin();
#elif defined(RT_OS_LINUX)
    pMonitor = new HostDnsServiceLinux();
#elif defined(RT_OS_SOLARIS)
    pMonitor =  new HostDnsServiceSolaris();
#elif defined(RT_OS_FREEBSD)
    pMonitor = new HostDnsServiceFreebsd();
#elif defined(RT_OS_OS2)
    pMonitor = new HostDnsServiceOs2();
#else
    pMonitor = new HostDnsServiceBase();
#endif

    return pMonitor;
}

HRESULT HostDnsServiceBase::init(HostDnsMonitorProxy *pProxy)
{
    LogRel(("HostDnsMonitor: initializing\n"));

    AssertPtrReturn(pProxy, E_POINTER);
    m->pProxy = pProxy;

    if (m->fThreaded)
    {
        LogRel2(("HostDnsMonitor: starting thread ...\n"));

        int vrc = RTSemEventCreate(&m->hMonitorThreadEvent);
        AssertRCReturn(vrc, E_FAIL);

        vrc = RTThreadCreate(&m->hMonitorThread,
                             HostDnsServiceBase::threadMonitorProc,
                             this, 128 * _1K, RTTHREADTYPE_IO,
                             RTTHREADFLAGS_WAITABLE, "dns-monitor");
        AssertRCReturn(vrc, E_FAIL);

        RTSemEventWait(m->hMonitorThreadEvent, RT_INDEFINITE_WAIT);

        LogRel2(("HostDnsMonitor: thread started\n"));
    }

    return S_OK;
}

void HostDnsServiceBase::uninit(void)
{
    LogRel(("HostDnsMonitor: shutting down ...\n"));

    if (m->fThreaded)
    {
        LogRel2(("HostDnsMonitor: waiting for thread ...\n"));

        const RTMSINTERVAL uTimeoutMs = 30 * 1000; /* 30s */

        monitorThreadShutdown(uTimeoutMs);

        int vrc = RTThreadWait(m->hMonitorThread, uTimeoutMs, NULL);
        if (RT_FAILURE(vrc))
            LogRel(("HostDnsMonitor: waiting for thread failed with vrc=%Rrc\n", vrc));

        if (m->hMonitorThreadEvent != NIL_RTSEMEVENT)
        {
            RTSemEventDestroy(m->hMonitorThreadEvent);
            m->hMonitorThreadEvent = NIL_RTSEMEVENT;
        }
    }

    LogRel(("HostDnsMonitor: shut down\n"));
}

void HostDnsServiceBase::setInfo(const HostDnsInformation &info)
{
    if (m->pProxy != NULL)
        m->pProxy->notify(info);
}

/**
 * Updates HostDnsMonitorProxy::Data::fLaxComparison every 30 seconds, returning
 * the new value.
 *
 * @note This will leave the lock while calling IVirtualBox::GetExtraData.
 */
uint32_t HostDnsMonitorProxy::pollGlobalExtraData(AutoWriteLock &aLock)
{
    uint32_t    fLaxComparison = m->fLaxComparison;
    VirtualBox *pVirtualBox = m->pVirtualBox;
    if (pVirtualBox)
    {
        uint64_t uNow = RTTimeNanoTS();
        if (uNow - m->uLastExtraDataPoll >= RT_NS_30SEC || m->uLastExtraDataPoll == 0)
        {
            m->uLastExtraDataPoll = uNow;

            /* We cannot do GetExtraData holding this lock, so temporarily release it. */
            aLock.release();

            /*
             * Should we ignore the order of DNS servers?
             */
            const com::Bstr bstrHostDNSOrderIgnoreKey("VBoxInternal2/HostDNSOrderIgnore");
            com::Bstr bstrHostDNSOrderIgnore;
            pVirtualBox->GetExtraData(bstrHostDNSOrderIgnoreKey.raw(), bstrHostDNSOrderIgnore.asOutParam());
            uint32_t fDNSOrderIgnore = 0;
            if (bstrHostDNSOrderIgnore.isNotEmpty() && bstrHostDNSOrderIgnore != "0")
                fDNSOrderIgnore = HostDnsInformation::IGNORE_SERVER_ORDER;

            if (fDNSOrderIgnore != (fLaxComparison & HostDnsInformation::IGNORE_SERVER_ORDER))
            {
                fLaxComparison ^= HostDnsInformation::IGNORE_SERVER_ORDER;
                LogRel(("HostDnsMonitor: %ls=%ls\n", bstrHostDNSOrderIgnoreKey.raw(), bstrHostDNSOrderIgnore.raw()));
            }

            /*
             * Should we ignore changes to the domain name or the search list?
             */
            const com::Bstr bstrHostDNSSuffixesIgnoreKey("VBoxInternal2/HostDNSSuffixesIgnore");
            com::Bstr bstrHostDNSSuffixesIgnore;
            pVirtualBox->GetExtraData(bstrHostDNSSuffixesIgnoreKey.raw(), bstrHostDNSSuffixesIgnore.asOutParam());
            uint32_t fDNSSuffixesIgnore = 0;
            if (bstrHostDNSSuffixesIgnore.isNotEmpty() && bstrHostDNSSuffixesIgnore != "0")
                fDNSSuffixesIgnore = HostDnsInformation::IGNORE_SUFFIXES;

            if (fDNSSuffixesIgnore != (fLaxComparison & HostDnsInformation::IGNORE_SUFFIXES))
            {
                fLaxComparison ^= HostDnsInformation::IGNORE_SUFFIXES;
                LogRel(("HostDnsMonitor: %ls=%ls\n", bstrHostDNSSuffixesIgnoreKey.raw(), bstrHostDNSSuffixesIgnore.raw()));
            }

            aLock.acquire();
            m->fLaxComparison = fLaxComparison;
        }
    }
    return fLaxComparison;
}

void HostDnsServiceBase::onMonitorThreadInitDone(void)
{
    if (!m->fThreaded) /* If non-threaded, bail out, nothing to do here. */
        return;

    RTSemEventSignal(m->hMonitorThreadEvent);
}

DECLCALLBACK(int) HostDnsServiceBase::threadMonitorProc(RTTHREAD, void *pvUser)
{
    HostDnsServiceBase *pThis = static_cast<HostDnsServiceBase *>(pvUser);
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    return pThis->monitorThreadProc();
}

/* HostDnsMonitorProxy */
HostDnsMonitorProxy::HostDnsMonitorProxy()
    : m(NULL)
    , m_ObjectLock(LOCKCLASS_OTHEROBJECT, "HostDnsMonitorProxy")
{
}

HostDnsMonitorProxy::~HostDnsMonitorProxy()
{
    uninit();
}

HRESULT HostDnsMonitorProxy::init(VirtualBox *aParent)
{
    AssertMsgReturn(m == NULL, ("DNS monitor proxy already initialized\n"), E_FAIL);

    HostDnsServiceBase *pMonitorImpl = HostDnsServiceBase::createHostDnsMonitor();
    AssertPtrReturn(pMonitorImpl, E_OUTOFMEMORY);

    Assert(m == NULL); /* Paranoia. */
    m = new HostDnsMonitorProxy::Data(pMonitorImpl, aParent);
    AssertPtrReturn(m, E_OUTOFMEMORY);

    return m->pMonitorImpl->init(this);
}

void HostDnsMonitorProxy::uninit(void)
{
    if (m)
    {
        if (m->pMonitorImpl)
        {
            m->pMonitorImpl->uninit();

            delete m->pMonitorImpl;
            m->pMonitorImpl = NULL;
        }

        delete m;
        m = NULL;
    }
}

util::LockHandle *HostDnsMonitorProxy::lockHandle() const
{
    return &m_ObjectLock;
}

void HostDnsMonitorProxy::notify(const HostDnsInformation &info)
{
    const bool fNotify = updateInfo(info);
    if (fNotify)
        m->pVirtualBox->i_onHostNameResolutionConfigurationChange();
}

HRESULT HostDnsMonitorProxy::GetNameServers(std::vector<com::Utf8Str> &aNameServers)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(m != NULL, E_FAIL);

    LogRel(("HostDnsMonitorProxy::GetNameServers:\n"));
    dumpHostDnsStrVector("name server", m->info.servers);

    aNameServers = m->info.servers;

    return S_OK;
}

HRESULT HostDnsMonitorProxy::GetDomainName(com::Utf8Str *pDomainName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(m != NULL, E_FAIL);

    LogRel(("HostDnsMonitorProxy::GetDomainName: %s\n", m->info.domain.isEmpty() ? "no domain set" : m->info.domain.c_str()));
    *pDomainName = m->info.domain.c_str();

    return S_OK;
}

HRESULT HostDnsMonitorProxy::GetSearchStrings(std::vector<com::Utf8Str> &aSearchStrings)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(m != NULL, E_FAIL);

    LogRel(("HostDnsMonitorProxy::GetSearchStrings:\n"));
    dumpHostDnsStrVector("search string", m->info.searchList);

    aSearchStrings = m->info.searchList;

    return S_OK;
}

bool HostDnsMonitorProxy::updateInfo(const HostDnsInformation &info)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    LogRel(("HostDnsMonitor: updating information\n"));

    if (info.equals(m->info))
    {
        LogRel(("HostDnsMonitor: unchanged\n"));
        return false;
    }

    uint32_t const fLaxComparison = pollGlobalExtraData(alock);

    LogRel(("HostDnsMonitor: old information\n"));
    dumpHostDnsInformation(m->info);
    LogRel(("HostDnsMonitor: new information\n"));
    dumpHostDnsInformation(info);

    bool fIgnore = fLaxComparison != 0 && info.equals(m->info, fLaxComparison);
    m->info = info;

    if (fIgnore)
    {
        LogRel(("HostDnsMonitor: lax comparison %#x, not notifying\n", m->fLaxComparison));
        return false;
    }

    return true;
}

