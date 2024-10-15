/* $Id: seamless.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * X11 Guest client - seamless mode, missing proper description while using the
 * potentially confusing word 'host'.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_seamless_h
#define GA_INCLUDED_SRC_x11_VBoxClient_seamless_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/thread.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>      /* for the R3 guest library functions  */

#include "seamless-x11.h"

/** @name Generic seamless functions
 * @{ */
void VBClSeamlessSendRegionUpdate(RTRECT *pRects, size_t cRects);
/** @} */

/**
 * Interface class for seamless mode service implementation.
 */
class VBClSeamlessSvc
{
protected:

    /* Note: Constructor must not throw, as we don't have exception handling on the guest side. */
    VBClSeamlessSvc(void) { }

public:

    virtual ~VBClSeamlessSvc(void) { }

public:

    virtual int  init(void) { return VINF_SUCCESS; }
    virtual int  worker(bool volatile *pfShutdown) { RT_NOREF(pfShutdown); return VINF_SUCCESS; }
    virtual void reset(void) { }
    virtual void stop(void) { }
    virtual int  term(void) { return VINF_SUCCESS; }

#ifdef IN_GUEST
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
};

/**
 * Service class which implements seamless mode for X11.
 */
class VBClX11SeamlessSvc : public VBClSeamlessSvc
{
public:
    VBClX11SeamlessSvc(void);

    virtual ~VBClX11SeamlessSvc();

public:

    /** @copydoc VBCLSERVICE::pfnInit */
    virtual int init(void);

    /** @copydoc VBCLSERVICE::pfnWorker */
    virtual int worker(bool volatile *pfShutdown);

    /** @copydoc VBCLSERVICE::pfnStop */
    virtual void stop(void);

    /** @copydoc VBCLSERVICE::pfnTerm */
    virtual int term(void);

private:
    /** Note: We don't want a copy constructor or assignment operator. */
    VBClX11SeamlessSvc(const VBClX11SeamlessSvc&);
    VBClX11SeamlessSvc& operator=(const VBClX11SeamlessSvc&);

    /** X11 event monitor object. */
    VBClX11SeamlessMonitor mX11Monitor;

    /** Thread to start and stop when we enter and leave seamless mode which
     *  monitors X11 windows in the guest. */
    RTTHREAD mX11MonitorThread;
    /** Should the X11 monitor thread be stopping? */
    volatile bool mX11MonitorThreadStopping;

    /** The current seamless mode we are in. */
    VMMDevSeamlessMode mMode;
    /** Is the service currently paused? */
    volatile bool mfPaused;

    /**
     * Waits for a seamless state change events from the host and dispatch it.  This is
     * meant to be called by the host event monitor thread exclusively.
     *
     * @returns        IRPT return code.
     */
    int nextStateChangeEvent(void);

    /** Thread function to monitor X11 window configuration changes. */
    static DECLCALLBACK(int) x11MonitorThread(RTTHREAD self, void *pvUser);

    /** Helper to start the X11 monitor thread. */
    int startX11MonitorThread(void);

    /** Helper to stop the X11 monitor thread again. */
    int stopX11MonitorThread(void);

    /** Is the service currently actively monitoring X11 windows? */
    bool isX11MonitorThreadRunning()
    {
        return mX11MonitorThread != NIL_RTTHREAD;
    }
};

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_seamless_h */
