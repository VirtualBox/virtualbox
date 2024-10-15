/* $Id: HostPowerLinux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox interface to host's power notification service
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include "HostPower.h"
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/power.h>
#include <iprt/time.h>

static bool checkDBusError(DBusError *pError, DBusConnection **pConnection)
{
    if (dbus_error_is_set(pError))
    {
        LogRel(("HostPowerServiceLinux: DBus connection Error (%s)\n", pError->message));
        dbus_error_free(pError);
        if (*pConnection)
        {
            /* Close the socket or whatever underlying the connection. */
            dbus_connection_close(*pConnection);
            /* Free in-process resources used for the now-closed connection. */
            dbus_connection_unref(*pConnection);
            *pConnection = NULL;
        }
        return true;
    }
    return false;
}

HostPowerServiceLinux::HostPowerServiceLinux(VirtualBox *aVirtualBox)
  : HostPowerService(aVirtualBox)
  , mThread(NIL_RTTHREAD)
  , mpConnection(NULL)
{
    DBusError error;

    int vrc = RTDBusLoadLib();
    if (RT_FAILURE(vrc))
    {
        LogRel(("HostPowerServiceLinux: DBus library not found.  Service not available.\n"));
        return;
    }
    dbus_error_init(&error);
    /* Connect to the DBus.  The connection will be not shared with any other
     * in-process callers of dbus_bus_get().  This is considered wasteful (see
     * API documentation) but simplifies our code, specifically shutting down.
     * The session bus allows up to 100000 connections per user as it "is just
     * running as the user anyway" (see session.conf.in in the DBus sources). */
    mpConnection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
    if (checkDBusError(&error, &mpConnection))
        return;
    /* We do not want to exit(1) if the connection is broken. */
    dbus_connection_set_exit_on_disconnect(mpConnection, FALSE);
    /* Tell the bus to wait for the sleep signal(s). */
    /* The current systemd-logind interface. */
    dbus_bus_add_match(mpConnection, "type='signal',interface='org.freedesktop.login1.Manager'", &error);
    /* The previous UPower interfaces (2010 - ca 2013). */
    dbus_bus_add_match(mpConnection, "type='signal',interface='org.freedesktop.UPower'", &error);
    dbus_connection_flush(mpConnection);
    if (checkDBusError(&error, &mpConnection))
        return;

    /* Grab another reference so that both the destruct and thread each has one: */
    DBusConnection *pForAssert = dbus_connection_ref(mpConnection);
    Assert(pForAssert == mpConnection); RT_NOREF(pForAssert);

    /* Create the new worker thread. */
    vrc = RTThreadCreate(&mThread, HostPowerServiceLinux::powerChangeNotificationThread, this, 0 /* cbStack */,
                         RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE, "MainPower");
    if (RT_FAILURE(vrc))
    {
        LogRel(("HostPowerServiceLinux: RTThreadCreate failed with %Rrc\n", vrc));
        dbus_connection_unref(mpConnection);
    }
}


HostPowerServiceLinux::~HostPowerServiceLinux()
{
    /* Closing the connection should cause the event loop to exit. */
    LogFunc((": Stopping thread\n"));
    if (mpConnection)
    {
        dbus_connection_close(mpConnection);
        dbus_connection_unref(mpConnection);
        mpConnection = NULL;
    }

    if (mThread != NIL_RTTHREAD)
    {
        /* HACK ALERT! This poke call should _not_ be necessary as dbus_connection_close()
                       should close the socket and force the poll/dbus_connection_read_write
                       call to return with POLLHUP/FALSE.  It does so when stepping it in the
                       debugger, but not in real life (asan build; dbus-1.12.20-1.fc32; linux 5.8).

                       Poking the thread is a crude crude way to wake it up from whatever
                       stuff it's actually blocked on and realize that the connection has
                       been dropped. */

        uint64_t msElapsed = RTTimeMilliTS();
        int vrc = RTThreadWait(mThread, 10 /*ms*/, NULL);
        if (RT_FAILURE(vrc))
        {
            RTThreadPoke(mThread);
            vrc = RTThreadWait(mThread, RT_MS_5SEC, NULL);
        }
        msElapsed = RTTimeMilliTS() - msElapsed;
        if (vrc != VINF_SUCCESS)
            LogRelThisFunc(("RTThreadWait() failed after %llu ms: %Rrc\n", msElapsed, vrc));
        mThread = NIL_RTTHREAD;
    }
}


DECLCALLBACK(int) HostPowerServiceLinux::powerChangeNotificationThread(RTTHREAD hThreadSelf, void *pInstance)
{
    NOREF(hThreadSelf);
    HostPowerServiceLinux *pPowerObj = static_cast<HostPowerServiceLinux *>(pInstance);
    DBusConnection *pConnection = pPowerObj->mpConnection;

    Log(("HostPowerServiceLinux: Thread started\n"));
    while (dbus_connection_read_write(pConnection, -1))
    {
        DBusMessage *pMessage = NULL;

        for (;;)
        {
            pMessage = dbus_connection_pop_message(pConnection);
            if (pMessage == NULL)
                break;

            /* The systemd-logind interface notification. */
            DBusMessageIter args;
            if (   dbus_message_is_signal(pMessage, "org.freedesktop.login1.Manager", "PrepareForSleep")
                && dbus_message_iter_init(pMessage, &args)
                && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_BOOLEAN)
            {
                dbus_bool_t fSuspend;
                dbus_message_iter_get_basic(&args, &fSuspend);

                /* Trinary operator does not work here as Reason_... is an
                 * anonymous enum. */
                if (fSuspend)
                    pPowerObj->notify(Reason_HostSuspend);
                else
                    pPowerObj->notify(Reason_HostResume);
            }

            /* The UPowerd interface notifications.  Sleeping is the older one,
             * NotifySleep the newer.  This gives us one second grace before the
             * suspend triggers. */
            if (   dbus_message_is_signal(pMessage, "org.freedesktop.UPower", "Sleeping")
                || dbus_message_is_signal(pMessage, "org.freedesktop.UPower", "NotifySleep"))
                pPowerObj->notify(Reason_HostSuspend);
            if (   dbus_message_is_signal(pMessage, "org.freedesktop.UPower", "Resuming")
                || dbus_message_is_signal(pMessage, "org.freedesktop.UPower", "NotifyResume"))
                pPowerObj->notify(Reason_HostResume);

            /* Free local resources held for the message. */
            dbus_message_unref(pMessage);
        }
    }

    /* Close the socket or whatever underlying the connection. */
    dbus_connection_close(pConnection);

    /* Free in-process resources used for the now-closed connection. */
    dbus_connection_unref(pConnection);

    Log(("HostPowerServiceLinux: Exiting thread\n"));
    return VINF_SUCCESS;
}

