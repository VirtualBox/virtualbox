/* $Id: HostDnsServiceDarwin.cpp 115039 2026-08-14 14:46:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Darwin specific DNS information fetching.
 */

/*
 * Copyright (C) 2004-2026 Oracle and/or its affiliates.
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

#include <VBox/com/string.h>
#include <VBox/com/ptr.h>


#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/net.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>

#include <VBox/log.h>

#include <vector>
#include "../HostDnsService.h"


struct HostDnsServiceDarwin::Data
{
    Data()
        : m_store(NULL)
        , m_DnsWatcher(NULL)
        , m_RunLoopRef(NULL)
        , m_SourceStop(NULL)
        , m_fStop(false)
        , m_evtStop(NIL_RTSEMEVENT) { }

    SCDynamicStoreRef m_store;
    CFRunLoopSourceRef m_DnsWatcher;
    CFRunLoopRef m_RunLoopRef;
    CFRunLoopSourceRef m_SourceStop;
    volatile bool m_fStop;
    RTSEMEVENT m_evtStop;
    static void performShutdownCallback(void *);
};


static const CFStringRef kStateNetworkGlobalDNSKey = CFSTR("State:/Network/Global/DNS");


HostDnsServiceDarwin::HostDnsServiceDarwin()
    : HostDnsServiceBase(true /* fThreaded */)
    , m(NULL)
{
    m = new HostDnsServiceDarwin::Data();
}

HostDnsServiceDarwin::~HostDnsServiceDarwin()
{
    if (m != NULL)
        delete m;
}

HRESULT HostDnsServiceDarwin::init(HostDnsMonitorProxy *pProxy)
{
    SCDynamicStoreContext ctx;
    RT_ZERO(ctx);

    ctx.info = this;

    m->m_store = SCDynamicStoreCreate(NULL, CFSTR("org.virtualbox.VBoxSVC.HostDNS"),
                                      (SCDynamicStoreCallBack)HostDnsServiceDarwin::hostDnsServiceStoreCallback,
                                      &ctx);
    AssertReturn(m->m_store, E_FAIL);

    m->m_DnsWatcher = SCDynamicStoreCreateRunLoopSource(NULL, m->m_store, 0);
    if (!m->m_DnsWatcher)
        return E_OUTOFMEMORY;

    int vrc = RTSemEventCreate(&m->m_evtStop);
    AssertRCReturn(vrc, E_FAIL);

    CFRunLoopSourceContext sctx;
    RT_ZERO(sctx);
    sctx.info    = this;
    sctx.perform = HostDnsServiceDarwin::Data::performShutdownCallback;

    m->m_SourceStop = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &sctx);
    AssertReturn(m->m_SourceStop, E_FAIL);

    HRESULT hrc = HostDnsServiceBase::init(pProxy);
    return hrc;
}

void HostDnsServiceDarwin::uninit(void)
{
    HostDnsServiceBase::uninit();

    CFRelease(m->m_SourceStop);
    m->m_SourceStop = NULL;
    CFRelease(m->m_RunLoopRef);
    m->m_RunLoopRef = NULL;
    CFRelease(m->m_DnsWatcher);
    m->m_DnsWatcher = NULL;
    CFRelease(m->m_store);
    m->m_store = NULL;

    RTSemEventDestroy(m->m_evtStop);
    m->m_evtStop = NIL_RTSEMEVENT;
}

int HostDnsServiceDarwin::monitorThreadShutdown(RTMSINTERVAL uTimeoutMs)
{
    RTCLock grab(m_LockMtx);
    if (!m->m_fStop)
    {
        ASMAtomicXchgBool(&m->m_fStop, true);
        CFRunLoopSourceSignal(m->m_SourceStop);
        CFRunLoopStop(m->m_RunLoopRef);

        grab.release(); /* bird 2025-01-31: May deadlock otherwise since hostDnsServiceStoreCallback takes the lock. */
        RTSemEventWait(m->m_evtStop, uTimeoutMs);
    }

    return VINF_SUCCESS;
}

int HostDnsServiceDarwin::monitorThreadProc(void)
{
    m->m_RunLoopRef = CFRunLoopGetCurrent();
    AssertReturn(m->m_RunLoopRef, VERR_INTERNAL_ERROR);

    CFRetain(m->m_RunLoopRef);

    CFRunLoopAddSource(m->m_RunLoopRef, m->m_SourceStop, kCFRunLoopCommonModes);

    CFArrayRef watchingArrayRef = CFArrayCreate(NULL,
                                                (const void **)&kStateNetworkGlobalDNSKey,
                                                1, &kCFTypeArrayCallBacks);
    if (!watchingArrayRef)
    {
        CFRelease(m->m_DnsWatcher);
        return VERR_NO_MEMORY;
    }

    if (SCDynamicStoreSetNotificationKeys(m->m_store, watchingArrayRef, NULL))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), m->m_DnsWatcher, kCFRunLoopCommonModes);

    CFRelease(watchingArrayRef);

    onMonitorThreadInitDone();

    /* Trigger initial update. */
    int vrc = updateInfo(); /** @todo r=bird: Not holding the lock here, unlike what hostDnsServiceStoreCallback does... */
    AssertRC(vrc); /* Not fatal in release builds. */  /** @todo r=bird: The function always returns VINF_SUCCESS. */

    while (!ASMAtomicReadBool(&m->m_fStop))
    {
        CFRunLoopRun();
    }

    CFRunLoopRemoveSource(m->m_RunLoopRef, m->m_SourceStop, kCFRunLoopCommonModes);

    /* We're notifying stopper thread. */
    RTSemEventSignal(m->m_evtStop);

    return VINF_SUCCESS;
}

DECLINLINE(bool) queryCFStringAsUtf8Str(CFStringRef hRefSrc, com::Utf8Str &a_rDst, size_t cbMax)
{
    a_rDst.reserve(_1K);
    if (!CFStringGetCString(hRefSrc, a_rDst.mutableRaw(), (CFIndex)a_rDst.capacity(), kCFStringEncodingUTF8))
    {
        a_rDst.reserve(cbMax);
        if (!CFStringGetCString(hRefSrc, a_rDst.mutableRaw(), (CFIndex)a_rDst.capacity(), kCFStringEncodingUTF8))
            return false;
    }
    RTStrPurgeEncoding(a_rDst.mutableRaw()); /* paranoia */
    a_rDst.jolt();
    return true;
}


int HostDnsServiceDarwin::updateInfo(void)
{
    CFPropertyListRef propertyRef = SCDynamicStoreCopyValue(m->m_store, kStateNetworkGlobalDNSKey);
    /*
     * # scutil
     * \> get State:/Network/Global/DNS
     * \> d.show
     * \<dictionary\> {
     * DomainName : vvl-domain
     * SearchDomains : \<array\> {
     * 0 : vvl-domain
     * 1 : de.vvl-domain.com
     * }
     * ServerAddresses : \<array\> {
     * 0 : 2001:4860:4860::8888
     * 1 : 192.168.1.1
     * 2 : 8.8.4.4
     *   }
     * }
     */
    if (!propertyRef)
        return VINF_SUCCESS;
    CFDictionaryRef const propertyAsDictRef = static_cast<CFDictionaryRef>(propertyRef);

    HostDnsInformation info;
    com::Utf8Str       strTmp;

    CFStringRef const domainNameRef = (CFStringRef)CFDictionaryGetValue(propertyAsDictRef, CFSTR("DomainName"));
    if (domainNameRef)
        if (queryCFStringAsUtf8Str(domainNameRef, strTmp, _16K))
            info.domain = strTmp;

    CFArrayRef const serverArrayRef = (CFArrayRef)CFDictionaryGetValue(propertyAsDictRef, CFSTR("ServerAddresses"));
    if (serverArrayRef)
    {
        CFIndex const cItems = CFArrayGetCount(serverArrayRef);
        for (CFIndex i = 0; i < cItems; ++i)
        {
            CFStringRef const serverAddressRef = (CFStringRef)CFArrayGetValueAtIndex(serverArrayRef, i);
            if (serverAddressRef)
            {
                if (!queryCFStringAsUtf8Str(serverAddressRef, strTmp, _16K))
                {
                    LogRel(("HostDnsServiceDarwin: idx: %u: Failed to convert address.\n", i));
                    continue;
                }

                RTNETADDRIPV4 mIPv4Addr = { 0 };
                int vrc = RTNetStrToIPv4AddrEx(strTmp.c_str(), &mIPv4Addr, NULL); /* Trusting macOS is providing not garbage addresses */
                if (RT_SUCCESS(vrc))
                {
                    LogRel(("HostDnsServiceDarwin: idx %u: IPv4 nameserver %RTnaipv4\n", i, mIPv4Addr));
                    info.servers.push_back(strTmp);
                    continue;
                }

                char *pszNext = NULL;
                RTNETADDRIPV6 mIPv6Addr = { { 0, 0 } };
                vrc = RTNetStrToIPv6AddrEx(strTmp.c_str(), &mIPv6Addr, &pszNext);
                if (RT_SUCCESS(vrc))
                {
                    if (*pszNext == '%') /** @todo XXX: TODO: IPv6 zones */
                    {
                        size_t zlen = RTStrOffCharOrTerm(pszNext, '.');
                        LogRel(("HostDnsServiceDarwin: line %u: FIXME: ignoring IPv6 zone %*.*s\n",
                                i, zlen, zlen, pszNext));
                    }

                    LogRel(("HostDnsServiceDarwin: line %u: IPv6 nameserver %RTnaipv6\n", i, &mIPv6Addr));
                    info.serversV6.push_back(strTmp);
                }
                else
                    LogRel(("HostDnsServiceDarwin: line %u: bad nameserver address %s\n", i, strTmp.c_str()));
            }
        }
    }

    CFArrayRef const searchArrayRef = (CFArrayRef)CFDictionaryGetValue(propertyAsDictRef, CFSTR("SearchDomains"));
    if (searchArrayRef)
    {
        CFIndex const cItems = CFArrayGetCount(searchArrayRef);
        for (CFIndex i = 0; i < cItems; ++i)
        {
            CFStringRef searchStringRef = (CFStringRef)CFArrayGetValueAtIndex(searchArrayRef, i);
            if (searchStringRef)
                if (queryCFStringAsUtf8Str(searchStringRef, strTmp, _64K))
                    info.searchList.push_back(strTmp);
        }
    }

    CFRelease(propertyRef);

    setInfo(info);

    return VINF_SUCCESS;
}

void HostDnsServiceDarwin::hostDnsServiceStoreCallback(void *, void *, void *pInfo)
{
    HostDnsServiceDarwin *pThis = (HostDnsServiceDarwin *)pInfo;
    AssertPtrReturnVoid(pThis);

    RTCLock grab(pThis->m_LockMtx);
    pThis->updateInfo();
}

void HostDnsServiceDarwin::Data::performShutdownCallback(void *pInfo)
{
    HostDnsServiceDarwin *pThis = (HostDnsServiceDarwin *)pInfo;
    AssertPtrReturnVoid(pThis);

    AssertPtrReturnVoid(pThis->m);
    ASMAtomicXchgBool(&pThis->m->m_fStop, true);
}
