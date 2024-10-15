/* $Id: UIKeyboardHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIKeyboardHandler class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIKeyboardHandler_h
#define FEQT_INCLUDED_SRC_runtime_UIKeyboardHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Other VBox includes: */
#include <VBox/com/defs.h>

/* External includes: */
#ifdef VBOX_WS_MAC
# include <Carbon/Carbon.h>
# include <CoreFoundation/CFBase.h>
#endif /* VBOX_WS_MAC */

/* Forward declarations: */
class QWidget;
class UIActionPool;
class UIMachine;
class UIMachineLogic;
class UIMachineView;
class UIMachineWindow;
#ifdef VBOX_WS_WIN
class WinAltGrMonitor;
#endif
#ifdef VBOX_WS_NIX
#  include <xcb/xcb.h>
#endif


/* Delegate to control VM keyboard functionality: */
class UIKeyboardHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about state-change. */
    void sigStateChange(int iState);

public:

    /* Factory functions to create/destroy keyboard-handler: */
    static UIKeyboardHandler* create(UIMachineLogic *pMachineLogic, UIVisualStateType visualStateType);
    static void destroy(UIKeyboardHandler *pKeyboardHandler);

    /* Prepare/cleanup listeners: */
    void prepareListener(ulong uScreenId, UIMachineWindow *pMachineWindow);
    void cleanupListener(ulong uScreenId);

    /** Captures the keyboard for @a uScreenId. */
    void captureKeyboard(ulong uScreenId);
    /** Finalises keyboard capture. */
    bool finaliseCaptureKeyboard();
    /** Releases the keyboard. */
    void releaseKeyboard();

    void releaseAllPressedKeys(bool aReleaseHostKey = true);

    /* Current keyboard state: */
    int state() const;

    /* Some getters required by side-code: */
    bool isHostKeyPressed() const { return m_fHostComboPressed; }
#ifdef VBOX_WS_MAC
    bool isHostKeyAlone() const { return m_bIsHostComboAlone; }
    bool isKeyboardGrabbed() const { return m_iKeyboardHookViewIndex != -1; }
#endif /* VBOX_WS_MAC */

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* For the debugger. */
    void setDebuggerActive(bool aActive = true);
#endif

#ifdef VBOX_WS_WIN
    /** Tells the keyboard event handler to skip host keyboard events.
      * Used for HID LEDs sync when on Windows host a keyboard event
      * is generated in order to change corresponding LED. */
    void winSkipKeyboardEvents(bool fSkip);
#endif /* VBOX_WS_WIN */

    /** Qt5: Performs pre-processing of all the native events. */
    bool nativeEventFilter(void *pMessage, ulong uScreenId);

    /** Called whenever host key press/release scan codes are inserted to the guest.
      * @a bPressed is true for press and false for release inserts. */
    void setHostKeyComboPressedFlag(bool bPressed);

protected slots:

    /* Machine state-change handler: */
    virtual void sltMachineStateChanged();

    /** Finalises keyboard capture. */
    void sltFinaliseCaptureKeyboard();

protected:

    /* Keyboard-handler constructor/destructor: */
    UIKeyboardHandler(UIMachineLogic *pMachineLogic);
    virtual ~UIKeyboardHandler();

    /* Prepare helpers: */
    virtual void prepareCommon();
    virtual void loadSettings();

    /* Cleanup helpers: */
    //virtual void saveSettings() {}
    virtual void cleanupCommon();

    /* Common getters: */
    UIMachineLogic *machineLogic() const { return m_pMachineLogic; }
    UIActionPool *actionPool() const;
    UIMachine *uimachine() const;

    /* Event handler for registered machine-view(s): */
    bool eventFilter(QObject *pWatchedObject, QEvent *pEvent) RT_OVERRIDE;

#if defined(VBOX_WS_MAC)
    /** Mac: Performs initial pre-processing of all the native keyboard events. */
    static bool macKeyboardProc(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);
    /** Mac: Performs initial pre-processing of all the native keyboard events. */
    bool macKeyboardEvent(const void *pvCocoaEvent, EventRef inEvent);
#elif defined(VBOX_WS_WIN)
    /** Win: Performs initial pre-processing of all the native keyboard events. */
    static LRESULT CALLBACK winKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) RT_NOTHROW_PROTO;
    /** Win: Performs initial pre-processing of all the native keyboard events. */
    bool winKeyboardEvent(UINT msg, const KBDLLHOOKSTRUCT &event);
#endif /* VBOX_WS_WIN */

    bool keyEventCADHandled(uint8_t uScan);
    bool keyEventHandleNormal(int iKey, uint8_t uScan, int fFlags, LONG *pCodes, uint *puCodesCount);
    bool keyEventHostComboHandled(int iKey, wchar_t *pUniKey, bool isHostComboStateChanged, bool *pfResult);
    void keyEventHandleHostComboRelease(ulong uScreenId);
    /* Separate function to handle most of existing keyboard-events: */
    bool keyEvent(int iKey, uint8_t uScan, int fFlags, ulong uScreenId, wchar_t *pUniKey = 0);
    bool processHotKey(int iHotKey, wchar_t *pUniKey);

    /* Private helpers: */
    void fixModifierState(LONG *piCodes, uint *puCount);
    void saveKeyStates();
    void sendChangedKeyStates();
    bool isAutoCaptureDisabled();
    void setAutoCaptureDisabled(bool fIsAutoCaptureDisabled);
    bool autoCaptureSetGlobally();
    bool viewHasFocus(ulong uScreenId);
    bool isSessionRunning();
    bool isSessionStuck();

    UIMachineWindow* isItListenedWindow(QObject *pWatchedObject) const;
    UIMachineView* isItListenedView(QObject *pWatchedObject) const;

    /* Machine logic parent: */
    UIMachineLogic *m_pMachineLogic;

    /* Registered machine-window(s): */
    QMap<ulong, UIMachineWindow*> m_windows;
    /* Registered machine-view(s): */
    QMap<ulong, UIMachineView*> m_views;

    /* Other keyboard variables: */
    int m_iKeyboardCaptureViewIndex;

    uint8_t m_pressedKeys[128];
    uint8_t m_pressedKeysCopy[128];

    QMap<int, uint8_t> m_pressedHostComboKeys;

    bool m_fKeyboardCaptured : 1;
    bool m_fHostComboPressed : 1;
    bool m_bIsHostComboAlone : 1;
    bool m_bIsHostComboProcessed : 1;
    bool m_fPassCADtoGuest : 1;
    bool m_fHostKeyComboPressInserted : 1;
    /** Whether the debugger is active.
     * Currently only affects auto capturing. */
    bool m_fDebuggerActive : 1;

    /** Holds the keyboard hook view index. */
    int m_iKeyboardHookViewIndex;

#if defined(VBOX_WS_MAC)
    /** Mac: Holds the current modifiers key mask. */
    UInt32 m_uDarwinKeyModifiers;
#elif defined(VBOX_WS_WIN)
    /** Win: Currently this is used in winKeyboardEvent() only. */
    bool m_fIsHostkeyInCapture;
    /** Win: Holds whether the keyboard event filter should ignore keyboard events. */
    bool m_fSkipKeyboardEvents;
    /** Win: Holds the keyboard hook instance. */
    HHOOK m_keyboardHook;
    /** Win: Holds the object monitoring key event stream for problematic AltGr events. */
    WinAltGrMonitor *m_pAltGrMonitor;
    /** Win: Holds the keyboard handler reference to be accessible from the keyboard hook. */
    static UIKeyboardHandler *m_spKeyboardHandler;
#elif defined(VBOX_WS_NIX)
    /** The root window at the time we grab the mouse buttons. */
    xcb_window_t m_hButtonGrabWindow;
#endif /* VBOX_WS_NIX */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIKeyboardHandler_h */
