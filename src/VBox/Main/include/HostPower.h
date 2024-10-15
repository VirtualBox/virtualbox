/* $Id: HostPower.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox interface to host's power notification service
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_HostPower_h
#define MAIN_INCLUDED_HostPower_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef RT_OS_DARWIN /* first, so we can undef pVM in iprt/cdefs.h */
# include <IOKit/pwr_mgt/IOPMLib.h>
# include <Carbon/Carbon.h>
#endif

#include "VirtualBoxBase.h"

#include <vector>

#ifdef RT_OS_LINUX
# include <VBox/dbus.h>
#endif

class HostPowerService
{
  public:
    HostPowerService(VirtualBox *aVirtualBox);
    virtual ~HostPowerService();
    void notify(Reason_T aReason);

  protected:
    VirtualBox *mVirtualBox;
    std::vector<ComPtr<IInternalSessionControl> > mSessionControls;
};

# if defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
/**
 * The Windows hosted Power Service.
 */
class HostPowerServiceWin : public HostPowerService
{
public:

    HostPowerServiceWin(VirtualBox *aVirtualBox);
    virtual ~HostPowerServiceWin();

private:

    static DECLCALLBACK(int) NotificationThread(RTTHREAD ThreadSelf, void *pInstance);
    static LRESULT CALLBACK  WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND        mHwnd;
    RTTHREAD    mThread;
};
# endif
# if defined(RT_OS_LINUX) || defined(DOXYGEN_RUNNING)
/**
 * The Linux hosted Power Service.
 */
class HostPowerServiceLinux : public HostPowerService
{
public:

    HostPowerServiceLinux(VirtualBox *aVirtualBox);
    virtual ~HostPowerServiceLinux();

private:

    static DECLCALLBACK(int) powerChangeNotificationThread(RTTHREAD ThreadSelf, void *pInstance);

    /* Private member vars */
    /** Our message thread. */
    RTTHREAD mThread;
    /** Our (private) connection to the DBus.  Closing this will cause the
     * message thread to exit. */
    DBusConnection *mpConnection;
};

# endif
# if defined(RT_OS_DARWIN) || defined(DOXYGEN_RUNNING)
/**
 * The Darwin hosted Power Service.
 */
class HostPowerServiceDarwin : public HostPowerService
{
public:

    HostPowerServiceDarwin(VirtualBox *aVirtualBox);
    virtual ~HostPowerServiceDarwin();

private:

    static DECLCALLBACK(int) powerChangeNotificationThread(RTTHREAD ThreadSelf, void *pInstance);
    static void powerChangeNotificationHandler(void *pvData, io_service_t service, natural_t messageType, void *pMessageArgument);
    static void lowPowerHandler(void *pvData);

    void checkBatteryCriticalLevel(bool *pfCriticalChanged = NULL);

    /* Private member vars */
    RTTHREAD mThread; /* Our message thread. */

    io_connect_t mRootPort; /* A reference to the Root Power Domain IOService */
    IONotificationPortRef mNotifyPort; /* Notification port allocated by IORegisterForSystemPower */
    io_object_t mNotifierObject; /* Notifier object, used to deregister later */
    CFRunLoopRef mRunLoop; /* A reference to the local thread run loop */

    bool mCritical; /* Indicate if the battery was in the critical state last checked */
};
# endif

#endif /* !MAIN_INCLUDED_HostPower_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
