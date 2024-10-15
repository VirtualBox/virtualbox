/* $Id: HostDnsServiceDarwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Darwin specific DNS information fetching.
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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
#include <iprt/thread.h>
#include <iprt/semaphore.h>

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>

#include <iprt/sanitized/string>
#include <vector>
#include "../HostDnsService.h"


struct HostDnsServiceDarwin::Data
{
    Data()
        : m_fStop(false) { }

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
    CFRelease(m->m_RunLoopRef);
    CFRelease(m->m_DnsWatcher);
    CFRelease(m->m_store);

    RTSemEventDestroy(m->m_evtStop);
}

int HostDnsServiceDarwin::monitorThreadShutdown(RTMSINTERVAL uTimeoutMs)
{
    RTCLock grab(m_LockMtx);
    if (!m->m_fStop)
    {
        ASMAtomicXchgBool(&m->m_fStop, true);
        CFRunLoopSourceSignal(m->m_SourceStop);
        CFRunLoopStop(m->m_RunLoopRef);

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
    int vrc = updateInfo();
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

int HostDnsServiceDarwin::updateInfo(void)
{
    CFPropertyListRef propertyRef = SCDynamicStoreCopyValue(m->m_store, kStateNetworkGlobalDNSKey);
    /**
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
     * 0 : 192.168.1.4
     * 1 : 192.168.1.1
     * 2 : 8.8.4.4
     *   }
     * }
     */

    if (!propertyRef)
        return VINF_SUCCESS;

    HostDnsInformation info;
    CFStringRef domainNameRef = (CFStringRef)CFDictionaryGetValue(static_cast<CFDictionaryRef>(propertyRef), CFSTR("DomainName"));
    if (domainNameRef)
    {
        const char *pszDomainName = CFStringGetCStringPtr(domainNameRef, CFStringGetSystemEncoding());
        if (pszDomainName)
            info.domain = pszDomainName;
    }

    CFArrayRef serverArrayRef = (CFArrayRef)CFDictionaryGetValue(static_cast<CFDictionaryRef>(propertyRef),
                                                                 CFSTR("ServerAddresses"));
    if (serverArrayRef)
    {
        CFIndex const cItems = CFArrayGetCount(serverArrayRef);
        for (CFIndex i = 0; i < cItems; ++i)
        {
            CFStringRef serverAddressRef = (CFStringRef)CFArrayGetValueAtIndex(serverArrayRef, i);
            if (!serverArrayRef)
                continue;

            /** @todo r=bird: This code is messed up as CFStringGetCStringPtr is documented
             *  to return NULL even if the string is valid.   Furthermore, we must have
             *  UTF-8 - some joker might decide latin-1 is better here for all we know
             *  and we'll end up with evil invalid UTF-8 sequences. */
            const char *pszServerAddress = CFStringGetCStringPtr(serverAddressRef, CFStringGetSystemEncoding());
            if (!pszServerAddress)
                continue;

            /** @todo r=bird: Why on earth are we using std::string and not Utf8Str?   */
            info.servers.push_back(std::string(pszServerAddress));
        }
    }

    CFArrayRef searchArrayRef = (CFArrayRef)CFDictionaryGetValue(static_cast<CFDictionaryRef>(propertyRef),
                                                                 CFSTR("SearchDomains"));
    if (searchArrayRef)
    {
        CFIndex const cItems = CFArrayGetCount(searchArrayRef);
        for (CFIndex i = 0; i < cItems; ++i)
        {
            CFStringRef searchStringRef = (CFStringRef)CFArrayGetValueAtIndex(searchArrayRef, i);
            if (!searchArrayRef)
                continue;

            /** @todo r=bird: This code is messed up as CFStringGetCStringPtr is documented
             *  to return NULL even if the string is valid.   Furthermore, we must have
             *  UTF-8 - some joker might decide latin-1 is better here for all we know
             *  and we'll end up with evil invalid UTF-8 sequences. */
            const char *pszSearchString = CFStringGetCStringPtr(searchStringRef, CFStringGetSystemEncoding());
            if (!pszSearchString)
                continue;

            /** @todo r=bird: Why on earth are we using std::string and not Utf8Str?   */
            info.searchList.push_back(std::string(pszSearchString));
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

