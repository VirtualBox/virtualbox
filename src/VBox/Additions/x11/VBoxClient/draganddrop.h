/** $Id: draganddrop.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - VBoxClient drag'n drop - Main header.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_draganddrop_h
#define GA_INCLUDED_SRC_x11_VBoxClient_draganddrop_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <X11/Xlib.h>

#include <iprt/cpp/mtlist.h>

#include <VBox/VBoxGuestLib.h>

class VBClX11DnDInst;

/**
 * Structure for storing new drag'n drop events and HGCM messages
 * into a single event queue.
 */
typedef struct VBCLDNDEVENT
{
    enum DnDEventType
    {
        /** Unknown event, do not use. */
        DnDEventType_Unknown = 0,
        /** VBGLR3DNDEVENT event. */
        DnDEventType_HGCM,
        /** X11 event. */
        DnDEventType_X11,
        /** Blow the type up to 32-bit. */
        DnDEventType_32BIT_HACK = 0x7fffffff
    };
    /** Event type. */
    DnDEventType enmType;
    union
    {
        PVBGLR3DNDEVENT hgcm;
        XEvent x11;
    };
#ifdef IN_GUEST
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
} VBCLDNDEVENT;
/** Pointer to a drag'n drop event. */
typedef VBCLDNDEVENT *PDNDEVENT;

/**
 * Interface class for a drag'n drop service implementation.
 */
class VBClDnDSvc
{
protected:

    /* Note: Constructor must not throw, as we don't have exception handling on the guest side. */
    VBClDnDSvc(void) { }

public:

    virtual ~VBClDnDSvc(void) { }

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
 * Service class which implements drag'n drop for X11.
 */
class VBClX11DnDSvc : public VBClDnDSvc
{
public:
    VBClX11DnDSvc(void)
      : m_pDisplay(NULL)
      , m_hHGCMThread(NIL_RTTHREAD)
      , m_hX11Thread(NIL_RTTHREAD)
      , m_hEventSem(NIL_RTSEMEVENT)
      , m_pCurDnD(NULL)
      , m_fStop(false)
    {
        RT_ZERO(m_dndCtx);
    }

    virtual int  init(void);
    virtual int  worker(bool volatile *pfShutdown);
    virtual void reset(void);
    virtual void stop(void);
    virtual int  term(void);

private:

    static DECLCALLBACK(int) hgcmEventThread(RTTHREAD hThread, void *pvUser);
    static DECLCALLBACK(int) x11EventThread(RTTHREAD hThread, void *pvUser);

    /* Private member vars */
    Display                   *m_pDisplay;
    /** Our (thread-safe) event queue with mixed events (DnD HGCM / X11). */
    RTCMTList<VBCLDNDEVENT>    m_eventQueue;
    /** Critical section for providing serialized access to list
     *  event queue's contents. */
    RTCRITSECT                 m_eventQueueCS;
    /** Thread handle for the HGCM message pumping thread. */
    RTTHREAD                   m_hHGCMThread;
    /** Thread handle for the X11 message pumping thread. */
    RTTHREAD                   m_hX11Thread;
    /** This service' DnD command context. */
    VBGLR3GUESTDNDCMDCTX       m_dndCtx;
    /** Event semaphore for new DnD events. */
    RTSEMEVENT                 m_hEventSem;
    /** Pointer to the allocated DnD instance.
        Currently we only support and handle one instance at a time. */
    VBClX11DnDInst            *m_pCurDnD;
    /** Stop indicator flag to signal the thread that it should shut down. */
    bool                       m_fStop;

    friend class VBClX11DnDInst;
};
#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_draganddrop_h */

