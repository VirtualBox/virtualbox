/* $Id: HostDnsService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/sanitized/string>
#include "HostDnsService.h"



static void dumpHostDnsStrVector(const std::string &prefix, const std::vector<std::string> &v)
{
    int i = 1;
    for (std::vector<std::string>::const_iterator it = v.begin();
         it != v.end();
         ++it, ++i)
        LogRel(("  %s %d: %s\n", prefix.c_str(), i, it->c_str()));
    if (v.empty())
        LogRel(("  no %s entries\n", prefix.c_str()));
}

static void dumpHostDnsInformation(const HostDnsInformation &info)
{
    dumpHostDnsStrVector("server", info.servers);

    if (!info.domain.empty())
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
        std::set<std::string> l(servers.begin(), servers.end());
        std::set<std::string> r(info.servers.begin(), info.servers.end());

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

DECLINLINE(void) detachVectorOfString(const std::vector<std::string>& v, std::vector<com::Utf8Str> &aArray)
{
    aArray.resize(v.size());
    size_t i = 0;
    for (std::vector<std::string>::const_iterator it = v.begin(); it != v.end(); ++it, ++i)
        aArray[i] = Utf8Str(it->c_str()); /** @todo r=bird: *it isn't necessarily UTF-8 clean!!
                                           * On darwin we do silly shit like using CFStringGetSystemEncoding()
                                           * that may be UTF-8 but doesn't need to be.
                                           *
                                           * Why on earth are we using std::string here anyway?
                                           */
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

void HostDnsMonitorProxy::pollGlobalExtraData(void)
{
    VirtualBox *pVirtualBox = m->pVirtualBox;
    if (RT_UNLIKELY(pVirtualBox == NULL))
        return;

    uint64_t uNow = RTTimeNanoTS();
    if (uNow - m->uLastExtraDataPoll >= RT_NS_30SEC || m->uLastExtraDataPoll == 0)
    {
        m->uLastExtraDataPoll = uNow;

        /*
         * Should we ignore the order of DNS servers?
         */
        const com::Bstr bstrHostDNSOrderIgnoreKey("VBoxInternal2/HostDNSOrderIgnore");
        com::Bstr bstrHostDNSOrderIgnore;
        pVirtualBox->GetExtraData(bstrHostDNSOrderIgnoreKey.raw(),
                                 bstrHostDNSOrderIgnore.asOutParam());
        uint32_t fDNSOrderIgnore = 0;
        if (bstrHostDNSOrderIgnore.isNotEmpty())
        {
            if (bstrHostDNSOrderIgnore != "0")
                fDNSOrderIgnore = HostDnsInformation::IGNORE_SERVER_ORDER;
        }

        if (fDNSOrderIgnore != (m->fLaxComparison & HostDnsInformation::IGNORE_SERVER_ORDER))
        {

            m->fLaxComparison ^= HostDnsInformation::IGNORE_SERVER_ORDER;
            LogRel(("HostDnsMonitor: %ls=%ls\n",
                    bstrHostDNSOrderIgnoreKey.raw(),
                    bstrHostDNSOrderIgnore.raw()));
        }

        /*
         * Should we ignore changes to the domain name or the search list?
         */
        const com::Bstr bstrHostDNSSuffixesIgnoreKey("VBoxInternal2/HostDNSSuffixesIgnore");
        com::Bstr bstrHostDNSSuffixesIgnore;
        pVirtualBox->GetExtraData(bstrHostDNSSuffixesIgnoreKey.raw(),
                                 bstrHostDNSSuffixesIgnore.asOutParam());
        uint32_t fDNSSuffixesIgnore = 0;
        if (bstrHostDNSSuffixesIgnore.isNotEmpty())
        {
            if (bstrHostDNSSuffixesIgnore != "0")
                fDNSSuffixesIgnore = HostDnsInformation::IGNORE_SUFFIXES;
        }

        if (fDNSSuffixesIgnore != (m->fLaxComparison & HostDnsInformation::IGNORE_SUFFIXES))
        {

            m->fLaxComparison ^= HostDnsInformation::IGNORE_SUFFIXES;
            LogRel(("HostDnsMonitor: %ls=%ls\n",
                    bstrHostDNSSuffixesIgnoreKey.raw(),
                    bstrHostDNSSuffixesIgnore.raw()));
        }
    }
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
{
}

HostDnsMonitorProxy::~HostDnsMonitorProxy()
{
    uninit();
}

HRESULT HostDnsMonitorProxy::init(VirtualBox* aParent)
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

void HostDnsMonitorProxy::notify(const HostDnsInformation &info)
{
    const bool fNotify = updateInfo(info);
    if (fNotify)
        m->pVirtualBox->i_onHostNameResolutionConfigurationChange();
}

HRESULT HostDnsMonitorProxy::GetNameServers(std::vector<com::Utf8Str> &aNameServers)
{
    AssertReturn(m != NULL, E_FAIL);
    RTCLock grab(m_LockMtx);

    LogRel(("HostDnsMonitorProxy::GetNameServers:\n"));
    dumpHostDnsStrVector("name server", m->info.servers);

    detachVectorOfString(m->info.servers, aNameServers);

    return S_OK;
}

HRESULT HostDnsMonitorProxy::GetDomainName(com::Utf8Str *pDomainName)
{
    AssertReturn(m != NULL, E_FAIL);
    RTCLock grab(m_LockMtx);

    LogRel(("HostDnsMonitorProxy::GetDomainName: %s\n",
            m->info.domain.empty() ? "no domain set" : m->info.domain.c_str()));

    *pDomainName = m->info.domain.c_str();

    return S_OK;
}

HRESULT HostDnsMonitorProxy::GetSearchStrings(std::vector<com::Utf8Str> &aSearchStrings)
{
    AssertReturn(m != NULL, E_FAIL);
    RTCLock grab(m_LockMtx);

    LogRel(("HostDnsMonitorProxy::GetSearchStrings:\n"));
    dumpHostDnsStrVector("search string", m->info.searchList);

    detachVectorOfString(m->info.searchList, aSearchStrings);

    return S_OK;
}

bool HostDnsMonitorProxy::updateInfo(const HostDnsInformation &info)
{
    LogRel(("HostDnsMonitor: updating information\n"));
    RTCLock grab(m_LockMtx);

    if (info.equals(m->info))
    {
        LogRel(("HostDnsMonitor: unchanged\n"));
        return false;
    }

    pollGlobalExtraData();

    LogRel(("HostDnsMonitor: old information\n"));
    dumpHostDnsInformation(m->info);
    LogRel(("HostDnsMonitor: new information\n"));
    dumpHostDnsInformation(info);

    bool fIgnore = m->fLaxComparison != 0 && info.equals(m->info, m->fLaxComparison);
    m->info = info;

    if (fIgnore)
    {
        LogRel(("HostDnsMonitor: lax comparison %#x, not notifying\n", m->fLaxComparison));
        return false;
    }

    return true;
}

