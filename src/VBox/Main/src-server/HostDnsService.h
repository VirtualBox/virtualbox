/* $Id: HostDnsService.h 108156 2025-02-01 02:19:11Z knut.osmundsen@oracle.com $ */
/** @file
 * Host DNS listener.
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

#ifndef MAIN_INCLUDED_SRC_src_server_HostDnsService_h
#define MAIN_INCLUDED_SRC_src_server_HostDnsService_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
#include "VirtualBoxBase.h"

#include <iprt/err.h> /* VERR_IGNORED */
#include <iprt/cpp/lock.h>

#include <list>
#include <vector>

typedef std::list<com::Utf8Str> Utf8StrList;
typedef Utf8StrList::iterator Utf8StrListIterator;

class HostDnsMonitorProxy;
typedef const HostDnsMonitorProxy *PCHostDnsMonitorProxy;

class HostDnsInformation
{
public:
    static const uint32_t IGNORE_SERVER_ORDER = RT_BIT_32(0);
    static const uint32_t IGNORE_SUFFIXES     = RT_BIT_32(1);

public:
    std::vector<com::Utf8Str> servers;
    com::Utf8Str domain;
    std::vector<com::Utf8Str> searchList;
    bool equals(const HostDnsInformation &, uint32_t fLaxComparison = 0) const;
};

/**
 * Base class for host DNS service implementations.
 *
 * This class supposed to be a real DNS monitor object as a singleton,
 * so it lifecycle starts and ends together with VBoxSVC.
 */
class HostDnsServiceBase
{
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(HostDnsServiceBase);

public:

    static HostDnsServiceBase *createHostDnsMonitor(void);

public:

    /* @note: method will wait till client call
       HostDnsService::monitorThreadInitializationDone() */
    virtual HRESULT init(HostDnsMonitorProxy *pProxy);
    virtual void uninit(void);

    virtual ~HostDnsServiceBase();

protected:

    explicit HostDnsServiceBase(bool fThreaded = false);

    void setInfo(const HostDnsInformation &);

    /* this function used only if HostDnsMonitor::HostDnsMonitor(true) */
    void onMonitorThreadInitDone();

public:

    virtual int monitorThreadShutdown(RTMSINTERVAL uTimeoutMs)
    {
        RT_NOREF(uTimeoutMs); AssertFailed(); return VERR_NOT_IMPLEMENTED;
    }

    virtual int monitorThreadProc(void) { AssertFailed(); return VERR_NOT_IMPLEMENTED; }

private:

    static DECLCALLBACK(int) threadMonitorProc(RTTHREAD, void *);

protected:

    mutable RTCLockMtx m_LockMtx;

//public: /** @todo r=andy Why is this public? */

    struct Data;
    Data *m;
};

/**
 * This class supposed to be a proxy for events on changing Host Name Resolving configurations.
 */
class HostDnsMonitorProxy
    : public Lockable
{
public:

    HostDnsMonitorProxy();
    virtual ~HostDnsMonitorProxy();

public:

    HRESULT init(VirtualBox *virtualbox);
    void uninit(void);
    void notify(const HostDnsInformation &info);

    HRESULT GetNameServers(std::vector<com::Utf8Str> &aNameServers);
    HRESULT GetDomainName(com::Utf8Str *pDomainName);
    HRESULT GetSearchStrings(std::vector<com::Utf8Str> &aSearchStrings);

    LockHandle *lockHandle() const RT_OVERRIDE;

private:

    uint32_t pollGlobalExtraData(AutoWriteLock &aLock);
    bool updateInfo(const HostDnsInformation &info);

private:
    struct Data;
    Data *m;
    /** Object lock object. */
    mutable util::RWLockHandle m_ObjectLock;
};

# if defined(RT_OS_DARWIN) || defined(DOXYGEN_RUNNING)
class HostDnsServiceDarwin : public HostDnsServiceBase
{
public:

    HostDnsServiceDarwin();
    virtual ~HostDnsServiceDarwin();

public:

    HRESULT init(HostDnsMonitorProxy *pProxy) RT_OVERRIDE;
    void uninit(void) RT_OVERRIDE;

protected:

    int monitorThreadShutdown(RTMSINTERVAL uTimeoutMs) RT_OVERRIDE;
    int monitorThreadProc(void) RT_OVERRIDE;

private:

    int updateInfo(void);
    static void hostDnsServiceStoreCallback(void *store, void *arrayRef, void *info);
    struct Data;
    Data *m;
};
# endif
# if defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
class HostDnsServiceWin : public HostDnsServiceBase
{
public:
    HostDnsServiceWin();
    virtual ~HostDnsServiceWin();

public:

    HRESULT init(HostDnsMonitorProxy *pProxy) RT_OVERRIDE;
    void uninit(void) RT_OVERRIDE;

protected:

    int monitorThreadShutdown(RTMSINTERVAL uTimeoutMs) RT_OVERRIDE;
    int monitorThreadProc(void) RT_OVERRIDE;

private:

    HRESULT updateInfo(void);

private:

    struct Data;
    Data *m;
};
# endif
# if defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX) || defined(RT_OS_OS2) || defined(RT_OS_FREEBSD) \
    || defined(DOXYGEN_RUNNING)
class HostDnsServiceResolvConf: public HostDnsServiceBase
{
public:

    explicit HostDnsServiceResolvConf(bool fThreaded = false) : HostDnsServiceBase(fThreaded), m(NULL) {}
    virtual ~HostDnsServiceResolvConf();

public:

    HRESULT init(HostDnsMonitorProxy *pProxy, const char *aResolvConfFileName);
    void uninit(void) RT_OVERRIDE;

    const Utf8Str &getResolvConf(void) const;

protected:

    HRESULT readResolvConf(void);

protected:

    struct Data;
    Data *m;
};
#  if defined(RT_OS_SOLARIS) || defined(DOXYGEN_RUNNING)
/**
 * XXX: https://blogs.oracle.com/praks/entry/file_events_notification
 */
class HostDnsServiceSolaris : public HostDnsServiceResolvConf
{
public:

    HostDnsServiceSolaris() {}
    virtual ~HostDnsServiceSolaris() {}

public:

    virtual HRESULT init(HostDnsMonitorProxy *pProxy) RT_OVERRIDE
    {
        return HostDnsServiceResolvConf::init(pProxy, "/etc/resolv.conf");
    }
};

#  endif
#  if defined(RT_OS_LINUX) || defined(DOXYGEN_RUNNING)
class HostDnsServiceLinux : public HostDnsServiceResolvConf
{
public:

    HostDnsServiceLinux() : HostDnsServiceResolvConf(true), m_fdShutdown(-1) {}
    virtual ~HostDnsServiceLinux();

public:

    HRESULT init(HostDnsMonitorProxy *pProxy) RT_OVERRIDE;

protected:

    int monitorThreadShutdown(RTMSINTERVAL uTimeoutMs) RT_OVERRIDE;
    int monitorThreadProc(void) RT_OVERRIDE;

    /** Socket end to write shutdown notification to, so the monitor thread will
     *  wake up and terminate. */
    int m_fdShutdown;
};

#  endif
#  if defined(RT_OS_FREEBSD) || defined(DOXYGEN_RUNNING)
class HostDnsServiceFreebsd: public HostDnsServiceResolvConf
{
public:

    HostDnsServiceFreebsd(){}
    virtual ~HostDnsServiceFreebsd() {}

public:

    virtual HRESULT init(HostDnsMonitorProxy *pProxy)
    {
        return HostDnsServiceResolvConf::init(pProxy, "/etc/resolv.conf");
    }
};

#  endif
#  if defined(RT_OS_OS2) || defined(DOXYGEN_RUNNING)
class HostDnsServiceOs2 : public HostDnsServiceResolvConf
{
public:

    HostDnsServiceOs2() {}
    virtual ~HostDnsServiceOs2() {}

public:

    /* XXX: \\MPTN\\ETC should be taken from environment variable ETC  */
    virtual HRESULT init(HostDnsMonitorProxy *pProxy) RT_OVERRIDE
    {
        return HostDnsServiceResolvConf::init(pProxy, "\\MPTN\\ETC\\RESOLV2");
    }
};

#  endif
# endif

#endif /* !MAIN_INCLUDED_SRC_src_server_HostDnsService_h */
