/* $Id: UICocoaApplication.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICocoaApplication class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UICocoaApplication.h"

/* Other VBox includes: */
#include <iprt/assert.h>

/* External includes: */
#import <AppKit/NSApplication.h>
#import <AppKit/NSButton.h>
#import <AppKit/NSEvent.h>
#import <AppKit/NSWindow.h>
#import <Foundation/NSArray.h>


/** Class for tracking a callback. */
@interface CallbackData : NSObject
{
@public
    /** Holds the mask of events to send to this callback. */
    uint32_t           fMask;
    /** Holds the callback. */
    PFNVBOXCACALLBACK  pfnCallback;
    /** Holds the user argument. */
    void              *pvUser;
}
- (id)initWithMask :(uint32)mask callback :(PFNVBOXCACALLBACK)callback user :(void *)user;
@end /* @interface CallbackData  */

@implementation CallbackData
/** Performs initialization. */
- (id)initWithMask :(uint32)mask callback :(PFNVBOXCACALLBACK)callback user :(void *)user
{
    self = [super init];
    if (self)
    {
        fMask = mask;
        pfnCallback = callback;
        pvUser = user;
    }
    return self;
}
@end /* @implementation CallbackData  */


/** Class for event handling. */
@interface UICocoaApplicationPrivate : NSApplication
{
    /** The event mask for which there currently are callbacks. */
    uint32_t        m_fMask;
    /** Array of callbacks. */
    NSMutableArray *m_pCallbacks;
    /** AppNap preventing activity. */
    id <NSObject>   m_activity;
}
- (id)init;
- (void)sendEvent :(NSEvent *)theEvent;
- (void)setCallback :(uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser;
- (void)unsetCallback :(uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser;

- (void)finishLaunching;
- (void)terminate :(nullable id)sender;

- (void)registerToNotificationOfWorkspace :(NSString *)pstrNotificationName;
- (void)unregisterFromNotificationOfWorkspace :(NSString *)pstrNotificationName;

- (void)registerToNotificationOfWindow :(NSString *)pstrNotificationName :(NSWindow *)pWindow;
- (void)unregisterFromNotificationOfWindow :(NSString *)pstrNotificationName :(NSWindow *)pWindow;

- (void)notificationCallbackOfObject :(NSNotification *)notification;
- (void)notificationCallbackOfWindow :(NSNotification *)notification;

- (void)registerSelectorForStandardWindowButton :(NSWindow *)pWindow :(StandardWindowButtonType)enmButtonType;
- (void)selectorForStandardWindowButton :(NSButton *)pButton;
@end /* @interface UICocoaApplicationPrivate */

@implementation UICocoaApplicationPrivate
/** Performs initialization. */
- (id) init
{
    self = [super init];
    if (self)
        m_pCallbacks = [[NSMutableArray alloc] init];

    // WORKAROUND:
    // Gently disable El Capitan tries to break everything with the Enter Full Screen action.
    // S.a. https://developer.apple.com/library/mac/releasenotes/AppKit/RN-AppKit/ for reference.
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];

    return self;
}

/** Sends an event.
  * @param  pEvent  Brings the event to be sent. */
- (void) sendEvent :(NSEvent *)pEvent
{
    /* Check if the type matches any of the registered callbacks. */
    uint32_t const fMask = m_fMask;
#if 0 /* for debugging */
    ::darwinPrintEvent("sendEvent: ", pEvent);
#endif
    if (fMask != 0)
    {
        NSEventType EvtType = [pEvent type];
        uint32_t fEvtMask = RT_LIKELY(EvtType < 32) ? RT_BIT_32(EvtType) : 0;
        if (fMask & fEvtMask)
        {
            /* Do the callouts in LIFO order. */
            for (CallbackData *pData in [m_pCallbacks reverseObjectEnumerator])
            {
                if (pData->fMask & fEvtMask)
                {
                    if (pData->pfnCallback(pEvent, [pEvent eventRef], pData->pvUser))
                        return;
                }

            }
        }
    }

    /* Get on with it. */
    [super sendEvent:pEvent];
}

/** Registers an event callback.
  * @param  fMask        Brings the event mask for which the callback is to be invoked.
  * @param  pfnCallback  Brings the callback function.
  * @param  pvUser       Brings the user argument. */
- (void) setCallback :(uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser
{
    /* Add the callback data to the array: */
    CallbackData *pData = [[[CallbackData alloc] initWithMask:fMask callback:pfnCallback user:pvUser] autorelease];
    [m_pCallbacks addObject:pData];

    /* Update the global mask: */
    m_fMask |= fMask;
}

/** Deregisters an event callback.
  * @param  fMask        Brings the event mask for which the callback is to be invoked.
  * @param  pfnCallback  Brings the callback function.
  * @param  pvUser       Brings the user argument. */
- (void) unsetCallback: (uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser
{
    /* Loop the event array LIFO fashion searching for a matching callback. */
    for (CallbackData *pData in [m_pCallbacks reverseObjectEnumerator])
    {
        if (   pData->pfnCallback == pfnCallback
            && pData->pvUser      == pvUser
            && pData->fMask       == fMask)
        {
            [m_pCallbacks removeObject:pData];
            break;
        }
    }
    uint32_t fNewMask = 0;
    for (CallbackData *pData in m_pCallbacks)
        fNewMask |= pData->fMask;
    m_fMask = fNewMask;
}

/** Standard handler called right after NSApp finished launching. */
- (void)finishLaunching
{
    /* Call to base-class: */
    [super finishLaunching];

    if (UICocoaApplication::instance()->isPreventAppNap())
    {
        //printf("Start activity preventing AppNap!\n");
        NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep;
        NSString *pstrReason = @"VirtualBox napping is prohibited!";
        m_activity = [[NSProcessInfo processInfo] beginActivityWithOptions:options
                                                                    reason:pstrReason];
        [m_activity retain];
    }
}

/** Standard handler called right before NSApp starting termination. */
- (void)terminate :(nullable id)sender
{
    if (UICocoaApplication::instance()->isPreventAppNap())
    {
        //printf("Finish activity preventing AppNap!\n");
        [[NSProcessInfo processInfo] endActivity:m_activity];
        [m_activity release];
        m_activity = Nil;
    }

    /* Call to base-class: */
    [super terminate: sender];
}

/** Registers to cocoa notification @a pstrNotificationName. */
- (void) registerToNotificationOfWorkspace :(NSString *)pstrNotificationName
{
    /* Register notification observer: */
    NSNotificationCenter *pNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    [pNotificationCenter addObserver:self
                            selector:@selector(notificationCallbackOfObject:)
                                name:pstrNotificationName
                              object:nil];
}

/** Unregister @a pWindow from cocoa notification @a pstrNotificationName. */
- (void) unregisterFromNotificationOfWorkspace :(NSString *)pstrNotificationName
{
    /* Uninstall notification observer: */
    NSNotificationCenter *pNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    [pNotificationCenter removeObserver:self
                                   name:pstrNotificationName
                                 object:nil];
}

/** Register @a pWindow to cocoa notification @a pstrNotificationName. */
- (void) registerToNotificationOfWindow :(NSString *)pstrNotificationName :(NSWindow *)pWindow
{
    /* Register notification observer: */
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(notificationCallbackOfWindow:)
                                                 name:pstrNotificationName
                                               object:pWindow];
}

/** Unregister @a pWindow from cocoa notification @a pstrNotificationName. */
- (void) unregisterFromNotificationOfWindow :(NSString *)pstrNotificationName :(NSWindow *)pWindow
{
    /* Uninstall notification observer: */
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:pstrNotificationName
                                                  object:pWindow];
}

/** Redirects cocoa @a notification to UICocoaApplication instance. */
- (void) notificationCallbackOfObject :(NSNotification *)notification
{
    /* Get current notification name: */
    NSString *pstrName = [notification name];

    /* Prepare user-info: */
    QMap<QString, QString> userInfo;

    /* Process known notifications: */
    if (   [pstrName isEqualToString :@"NSWorkspaceDidActivateApplicationNotification"]
        || [pstrName isEqualToString :@"NSWorkspaceDidDeactivateApplicationNotification"])
    {
        NSDictionary *pUserInfo = [notification userInfo];
        NSRunningApplication *pApplication = [pUserInfo valueForKey :@"NSWorkspaceApplicationKey"];
        NSString *pstrBundleIndentifier = [pApplication bundleIdentifier];
        userInfo.insert("BundleIdentifier", darwinFromNativeString((NativeNSStringRef)pstrBundleIndentifier));
    }

    /* Redirect known notifications to objects: */
    UICocoaApplication::instance()->nativeNotificationProxyForObject(pstrName, userInfo);
}

/** Redirects cocoa @a notification to UICocoaApplication instance. */
- (void) notificationCallbackOfWindow :(NSNotification *)notification
{
    /* Get current notification name: */
    NSString *pstrName = [notification name];

    /* Redirect known notifications to widgets: */
    UICocoaApplication::instance()->nativeNotificationProxyForWidget(pstrName, [notification object]);
}

/** Registers selector for standard window @a enmButtonType of the passed @a pWindow. */
- (void)registerSelectorForStandardWindowButton :(NSWindow *)pWindow :(StandardWindowButtonType)enmButtonType
{
    /* Retrieve corresponding button: */
    NSButton *pButton = Nil;
    switch (enmButtonType)
    {
        case StandardWindowButtonType_Close:            pButton = [pWindow standardWindowButton:NSWindowCloseButton]; break;
        case StandardWindowButtonType_Miniaturize:      pButton = [pWindow standardWindowButton:NSWindowMiniaturizeButton]; break;
        case StandardWindowButtonType_Zoom:             pButton = [pWindow standardWindowButton:NSWindowZoomButton]; break;
        case StandardWindowButtonType_Toolbar:          pButton = [pWindow standardWindowButton:NSWindowToolbarButton]; break;
        case StandardWindowButtonType_DocumentIcon:     pButton = [pWindow standardWindowButton:NSWindowDocumentIconButton]; break;
        case StandardWindowButtonType_DocumentVersions: /*pButton = [pWindow standardWindowButton:NSWindowDocumentVersionsButton];*/ break;
        case StandardWindowButtonType_FullScreen:       /*pButton = [pWindow standardWindowButton:NSWindowFullScreenButton];*/ break;
    }

    /* Register selector if button exists: */
    if (pButton != Nil)
    {
        [pButton setTarget:self];
        [pButton setAction:@selector(selectorForStandardWindowButton:)];
    }
}

/** Redirects selector of the standard window @a pButton to UICocoaApplication instance callback. */
- (void)selectorForStandardWindowButton :(NSButton *)pButton
{
    /* Check if Option key is currently held: */
    const bool fWithOptionKey = [NSEvent modifierFlags] & NSAlternateKeyMask;

    /* Redirect selector to callback: */
    UICocoaApplication::instance()->nativeCallbackProxyForStandardWindowButton(pButton, fWithOptionKey);
}
@end /* @implementation UICocoaApplicationPrivate */


/*********************************************************************************************************************************
*   Class UICocoaApplication implementation.                                                                                     *
*********************************************************************************************************************************/

/* static */
UICocoaApplication *UICocoaApplication::s_pInstance = 0;

/* static */
void UICocoaApplication::create(bool fPreventAppNap)
{
    if (!s_pInstance)
        s_pInstance = new UICocoaApplication(fPreventAppNap);
}

/* static */
UICocoaApplication *UICocoaApplication::instance()
{
    return s_pInstance;
}

UICocoaApplication::UICocoaApplication(bool fPreventAppNap)
    : m_fPreventAppNap(fPreventAppNap)
{
    /* Make sure our private NSApplication object is created: */
    m_pNative = (UICocoaApplicationPrivate*)[UICocoaApplicationPrivate sharedApplication];
    // WORKAROUND":
    // Create one auto release pool which is in place for all the
    // initialization and deinitialization stuff. That is when the
    // NSApplication is not running the run loop (there is a separate
    // auto release pool defined).
    m_pPool = [[NSAutoreleasePool alloc] init];
}

UICocoaApplication::~UICocoaApplication()
{
    [m_pNative release];
    [m_pPool release];
}

bool UICocoaApplication::isActive() const
{
    return [m_pNative isActive];
}

bool UICocoaApplication::isDarkMode() const
{
    NativeNSStringRef pstrInterfaceMode = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return [pstrInterfaceMode isEqualToString :@"Dark"];
}

void UICocoaApplication::hide()
{
    [m_pNative hide:m_pNative];
}

void UICocoaApplication::hideUserElements()
{
    [m_pNative setPresentationOptions:NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock];
}

void UICocoaApplication::registerForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser)
{
    [m_pNative setCallback:fMask :pfnCallback :pvUser];
}

void UICocoaApplication::unregisterForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser)
{
    [m_pNative unsetCallback:fMask :pfnCallback :pvUser];
}

void UICocoaApplication::registerToNotificationOfWorkspace(const QString &strNativeNotificationName, QObject *pObject,
                                                           PfnNativeNotificationCallbackForQObject pCallback)
{
    /* Make sure it is not registered yet: */
    AssertReturnVoid(!m_objectCallbacks.contains(pObject) || !m_objectCallbacks[pObject].contains(strNativeNotificationName));

    /* Remember callback: */
    m_objectCallbacks[pObject][strNativeNotificationName] = pCallback;

    /* Register observer: */
    NativeNSStringRef pstrNativeNotificationName = darwinToNativeString(strNativeNotificationName.toLatin1().constData());
    [m_pNative registerToNotificationOfWorkspace :pstrNativeNotificationName];
}

void UICocoaApplication::unregisterFromNotificationOfWorkspace(const QString &strNativeNotificationName, QObject *pObject)
{
    /* Make sure it is registered yet: */
    AssertReturnVoid(m_objectCallbacks.contains(pObject) && m_objectCallbacks[pObject].contains(strNativeNotificationName));

    /* Forget callback: */
    m_objectCallbacks[pObject].remove(strNativeNotificationName);
    if (m_objectCallbacks[pObject].isEmpty())
        m_objectCallbacks.remove(pObject);

    /* Unregister observer: */
    NativeNSStringRef pstrNativeNotificationName = darwinToNativeString(strNativeNotificationName.toLatin1().constData());
    [m_pNative unregisterFromNotificationOfWorkspace :pstrNativeNotificationName];
}

void UICocoaApplication::registerToNotificationOfWindow(const QString &strNativeNotificationName, QWidget *pWidget,
                                                        PfnNativeNotificationCallbackForQWidget pCallback)
{
    /* Make sure it is not registered yet: */
    AssertReturnVoid(!m_widgetCallbacks.contains(pWidget) || !m_widgetCallbacks[pWidget].contains(strNativeNotificationName));

    /* Remember callback: */
    m_widgetCallbacks[pWidget][strNativeNotificationName] = pCallback;

    /* Register observer: */
    NativeNSStringRef pstrNativeNotificationName = darwinToNativeString(strNativeNotificationName.toLatin1().constData());
    NativeNSWindowRef pWindow = darwinToNativeWindow(pWidget);
    [m_pNative registerToNotificationOfWindow :pstrNativeNotificationName :pWindow];
}

void UICocoaApplication::unregisterFromNotificationOfWindow(const QString &strNativeNotificationName, QWidget *pWidget)
{
    /* Make sure it is registered yet: */
    AssertReturnVoid(m_widgetCallbacks.contains(pWidget) && m_widgetCallbacks[pWidget].contains(strNativeNotificationName));

    /* Forget callback: */
    m_widgetCallbacks[pWidget].remove(strNativeNotificationName);
    if (m_widgetCallbacks[pWidget].isEmpty())
        m_widgetCallbacks.remove(pWidget);

    /* Unregister observer: */
    NativeNSStringRef pstrNativeNotificationName = darwinToNativeString(strNativeNotificationName.toLatin1().constData());
    NativeNSWindowRef pWindow = darwinToNativeWindow(pWidget);
    [m_pNative unregisterFromNotificationOfWindow :pstrNativeNotificationName :pWindow];
}

void UICocoaApplication::nativeNotificationProxyForObject(NativeNSStringRef pstrNotificationName,
                                                          const QMap<QString,
                                                          QString> &userInfo)
{
    /* Get notification name: */
    QString strNotificationName = darwinFromNativeString(pstrNotificationName);

    /* Check if existing object(s) have corresponding notification handler: */
    foreach (QObject *pObject, m_objectCallbacks.keys())
    {
        const QMap<QString, PfnNativeNotificationCallbackForQObject> &callbacks = m_objectCallbacks[pObject];
        if (callbacks.contains(strNotificationName))
            callbacks[strNotificationName](pObject, userInfo);
    }
}

void UICocoaApplication::nativeNotificationProxyForWidget(NativeNSStringRef pstrNotificationName, NativeNSWindowRef pWindow)
{
    /* Get notification name: */
    QString strNotificationName = darwinFromNativeString(pstrNotificationName);

    /* Check if existing widget(s) have corresponding notification handler: */
    foreach (QWidget *pWidget, m_widgetCallbacks.keys())
    {
        if (darwinToNativeWindow(pWidget) == pWindow)
        {
            const QMap<QString, PfnNativeNotificationCallbackForQWidget> &callbacks = m_widgetCallbacks[pWidget];
            if (callbacks.contains(strNotificationName))
                callbacks[strNotificationName](strNotificationName, pWidget);
        }
    }
}

void UICocoaApplication::registerCallbackForStandardWindowButton(QWidget *pWidget, StandardWindowButtonType enmButtonType,
                                                                 PfnStandardWindowButtonCallbackForQWidget pCallback)
{
    /* Make sure it is not registered yet: */
    AssertReturnVoid(   !m_stdWindowButtonCallbacks.contains(pWidget)
                     || !m_stdWindowButtonCallbacks.value(pWidget).contains(enmButtonType));

    /* Remember callback: */
    m_stdWindowButtonCallbacks[pWidget][enmButtonType] = pCallback;

    /* Register selector: */
    NativeNSWindowRef pWindow = darwinToNativeWindow(pWidget);
    [m_pNative registerSelectorForStandardWindowButton :pWindow :enmButtonType];
}

void UICocoaApplication::unregisterCallbackForStandardWindowButton(QWidget *pWidget, StandardWindowButtonType enmButtonType)
{
    /* Make sure it is registered yet: */
    AssertReturnVoid(   m_stdWindowButtonCallbacks.contains(pWidget)
                     && m_stdWindowButtonCallbacks.value(pWidget).contains(enmButtonType));

    /* Forget callback: */
    m_stdWindowButtonCallbacks[pWidget].remove(enmButtonType);
    if (m_stdWindowButtonCallbacks.value(pWidget).isEmpty())
        m_stdWindowButtonCallbacks.remove(pWidget);
}

void UICocoaApplication::nativeCallbackProxyForStandardWindowButton(NativeNSButtonRef pButton, bool fWithOptionKey)
{
    // WORKAROUND:
    // Why not using nested foreach, will you ask?
    // It's because Qt 4.x has shadowing issue in Q_FOREACH macro.
    // Bug record QTBUG-33585 opened for Qt 4.8.4 and closed as _won't fix_ by one of Qt devs.

    /* Check if passed button is one of the buttons of the registered widget(s): */
    const QList<QWidget*> widgets = m_stdWindowButtonCallbacks.keys();
    for (int iWidgetIndex = 0; iWidgetIndex < widgets.size(); ++iWidgetIndex)
    {
        QWidget *pWidget = widgets.at(iWidgetIndex);
        const QMap<StandardWindowButtonType, PfnStandardWindowButtonCallbackForQWidget> callbacks
            = m_stdWindowButtonCallbacks.value(pWidget);
        const QList<StandardWindowButtonType> buttonTypes = callbacks.keys();
        for (int iButtonTypeIndex = 0; iButtonTypeIndex < buttonTypes.size(); ++iButtonTypeIndex)
        {
            StandardWindowButtonType enmButtonType = buttonTypes.at(iButtonTypeIndex);
            if (darwinNativeButtonOfWindow(pWidget, enmButtonType) == pButton)
                return callbacks.value(enmButtonType)(enmButtonType, fWithOptionKey, pWidget);
        }
    }
}

