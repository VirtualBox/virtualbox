/* $Id: UIConsoleEventHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIConsoleEventHandler class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIConsoleEventHandler_h
#define FEQT_INCLUDED_SRC_runtime_UIConsoleEventHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QRect>

/* COM includes: */
#include "CMediumAttachment.h"
#include "CNetworkAdapter.h"
#include "CUSBDevice.h"
#include "CVirtualBoxErrorInfo.h"
#include "KClipboardMode.h"
#include "KDnDMode.h"
#include "KGuestMonitorChangedEventType.h"
#include "KMachineState.h"

/* Forward declarations: */
class UIConsoleEventHandlerProxy;
class UIMousePointerShapeData;
class UISession;


/** QObject subclass
  * providing GUI with the CConsole event-source. */
class UIConsoleEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about mouse pointer @a shapeData change. */
    void sigMousePointerShapeChange(const UIMousePointerShapeData &shapeData);
    /** Notifies about mouse capability change to @a fSupportsAbsolute, @a fSupportsRelative,
      * @a fSupportsTouchScreen, @a fSupportsTouchPad and @a fNeedsHostCursor. */
    void sigMouseCapabilityChange(bool fSupportsAbsolute, bool fSupportsRelative,
                                  bool fSupportsTouchScreen, bool fSupportsTouchPad,
                                  bool fNeedsHostCursor);
    /** Notifies about guest request to change the cursor position to @a uX * @a uY.
      * @param  fContainsData  Brings whether the @a uX and @a uY values are valid and could be used by the GUI now. */
    void sigCursorPositionChange(bool fContainsData, unsigned long uX, unsigned long uY);
    /** Notifies about keyboard LEDs change for @a fNumLock, @a fCapsLock and @a fScrollLock. */
    void sigKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock);
    /** Notifies about machine @a state change. */
    void sigStateChange(KMachineState state);
    /** Notifies about guest additions state change. */
    void sigAdditionsChange();
    /** Notifies about network @a adapter state change. */
    void sigNetworkAdapterChange(CNetworkAdapter adapter);
    /** Notifies about storage device change for @a attachment, which was @a fRemoved and it was @a fSilent for guest. */
    void sigStorageDeviceChange(CMediumAttachment attachment, bool fRemoved, bool fSilent);
    /** Notifies about storage medium @a attachment state change. */
    void sigMediumChange(CMediumAttachment attachment);
    /** Notifies about VRDE device state change. */
    void sigVRDEChange();
    /** Notifies about recording state change. */
    void sigRecordingChange();
    /** Notifies about USB controller state change. */
    void sigUSBControllerChange();
    /** Notifies about USB @a device state change to @a fAttached, holding additional @a error information. */
    void sigUSBDeviceStateChange(CUSBDevice device, bool fAttached, CVirtualBoxErrorInfo error);
    /** Notifies about shared folder state change. */
    void sigSharedFolderChange();
    /** Notifies about CPU execution-cap change. */
    void sigCPUExecutionCapChange();
    /** Notifies about guest-screen configuration change of @a type for @a uScreenId with @a screenGeo. */
    void sigGuestMonitorChange(KGuestMonitorChangedEventType type, ulong uScreenId, QRect screenGeo);
    /** Notifies about Runtime error with @a strErrorId which is @a fFatal and have @a strMessage. */
    void sigRuntimeError(bool fFatal, QString strErrorId, QString strMessage);
#ifdef VBOX_WS_MAC
    /** Notifies about VM window should be shown. */
    void sigShowWindow();
#endif /* VBOX_WS_MAC */
    /** Notifies about audio adapter state change. */
    void sigAudioAdapterChange();
    /** Notifies clipboard mode change. */
    void sigClipboardModeChange(KClipboardMode enmMode);
    /** Notifies about a clipboard error. */
    void sigClipboardError(const QString &strMsg);
    /** Notifies drag and drop mode change. */
    void sigDnDModeChange(KDnDMode enmMode);

public:

    /** Constructs console event handler for passed @a pSession. */
    UIConsoleEventHandler(UISession *pSession);

private:

    /** Prepares all. */
    void prepare();

    /** Holds the console event proxy instance. */
    UIConsoleEventHandlerProxy *m_pProxy;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIConsoleEventHandler_h */
