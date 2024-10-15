/* $Id: VBoxManageGuestCtrl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManageGuestCtrl.h - Definitions for guest control.
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

#ifndef VBOX_INCLUDED_SRC_VBoxManage_VBoxManageGuestCtrl_h
#define VBOX_INCLUDED_SRC_VBoxManage_VBoxManageGuestCtrl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/com.h>
#include <VBox/com/listeners.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/semaphore.h>
#include <iprt/time.h>

#include <map>

const char *gctlFileStatusToText(FileStatus_T enmStatus);
const char *gctlProcessStatusToText(ProcessStatus_T enmStatus);
const char *gctlGuestSessionStatusToText(GuestSessionStatus_T enmStatus);

using namespace com;

class GuestFileEventListener;
typedef ListenerImpl<GuestFileEventListener> GuestFileEventListenerImpl;

class GuestProcessEventListener;
typedef ListenerImpl<GuestProcessEventListener> GuestProcessEventListenerImpl;

class GuestSessionEventListener;
typedef ListenerImpl<GuestSessionEventListener> GuestSessionEventListenerImpl;

class GuestEventListener;
typedef ListenerImpl<GuestEventListener> GuestEventListenerImpl;

class GuestAdditionsRunlevelListener;
typedef ListenerImpl<GuestAdditionsRunlevelListener> GuestAdditionsRunlevelListenerImpl;

/** Simple statistics class for binding locally
 *  held data to a specific guest object. */
class GuestEventStats
{

public:

    GuestEventStats(void)
        : uLastUpdatedMS(RTTimeMilliTS())
    {
    }

    /** @todo Make this more a class than a structure. */
public:

    uint64_t uLastUpdatedMS;
};

class GuestFileStats : public GuestEventStats
{

public:

    GuestFileStats(void) { }

    GuestFileStats(ComObjPtr<GuestFileEventListenerImpl> pListenerImpl)
        : mListener(pListenerImpl)
    {
    }

public: /** @todo */

    ComObjPtr<GuestFileEventListenerImpl> mListener;
};

class GuestProcStats : public GuestEventStats
{

public:

    GuestProcStats(void) { }

    GuestProcStats(ComObjPtr<GuestProcessEventListenerImpl> pListenerImpl)
        : mListener(pListenerImpl)
    {
    }

public: /** @todo */

    ComObjPtr<GuestProcessEventListenerImpl> mListener;
};

class GuestSessionStats : public GuestEventStats
{

public:

    GuestSessionStats(void) { }

    GuestSessionStats(ComObjPtr<GuestSessionEventListenerImpl> pListenerImpl)
        : mListener(pListenerImpl)
    {
    }

public: /** @todo */

    ComObjPtr<GuestSessionEventListenerImpl> mListener;
};

/** Map containing all watched guest files. */
typedef std::map< ComPtr<IGuestFile>, GuestFileStats > GuestEventFiles;
/** Map containing all watched guest processes. */
typedef std::map< ComPtr<IGuestProcess>, GuestProcStats > GuestEventProcs;
/** Map containing all watched guest sessions. */
typedef std::map< ComPtr<IGuestSession>, GuestSessionStats > GuestEventSessions;

class GuestListenerBase
{
public:

    GuestListenerBase(void);

    virtual ~GuestListenerBase(void);

public:

    HRESULT init(bool fVerbose = false);

protected:

    /** Verbose flag. */
    bool mfVerbose;
};

/**
 *  Handler for guest process events.
 */
class GuestFileEventListener : public GuestListenerBase
{
public:

    GuestFileEventListener(void);

    virtual ~GuestFileEventListener(void);

public:

    void uninit(void);

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent);

protected:

};

/**
 *  Handler for guest process events.
 */
class GuestProcessEventListener : public GuestListenerBase
{
public:

    GuestProcessEventListener(void);

    virtual ~GuestProcessEventListener(void);

public:

    void uninit(void);

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent);

protected:

};

/**
 *  Handler for guest session events.
 */
class GuestSessionEventListener : public GuestListenerBase
{
public:

    GuestSessionEventListener(void);

    virtual ~GuestSessionEventListener(void);

public:

    void uninit(void);

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent);

protected:

    GuestEventFiles mFiles;
    GuestEventProcs mProcs;
};

/**
 *  Handler for guest events.
 */
class GuestEventListener : public GuestListenerBase
{

public:

    GuestEventListener(void);

    virtual ~GuestEventListener(void);

public:

    void uninit(void);

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent);

protected:

    GuestEventSessions mSessions;
};

/**
 *  Handler for Guest Additions runlevel change events.
 */
class GuestAdditionsRunlevelListener : public GuestListenerBase
{

public:

    GuestAdditionsRunlevelListener(AdditionsRunLevelType_T enmRunLevel);

    virtual ~GuestAdditionsRunlevelListener(void);

public:

    void uninit(void);

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent);

protected:

    /** The run level target we're waiting for. */
    AdditionsRunLevelType_T mRunLevelTarget;
};

#endif /* !VBOX_INCLUDED_SRC_VBoxManage_VBoxManageGuestCtrl_h */
