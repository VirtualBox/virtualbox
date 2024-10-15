/* $Id: UICocoaApplication.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICocoaApplication class declaration.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_UICocoaApplication_h
#define FEQT_INCLUDED_SRC_platform_darwin_UICocoaApplication_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>

/* GUI includes: */
#include "VBoxCocoaHelper.h"
#include "VBoxUtils-darwin.h"
#include "UILibraryDefs.h"

/* Forward declarations: */
class QObject;
class QWidget;

/* Cocoa declarations: */
ADD_COCOA_NATIVE_REF(UICocoaApplicationPrivate);
ADD_COCOA_NATIVE_REF(NSAutoreleasePool);
ADD_COCOA_NATIVE_REF(NSString);
ADD_COCOA_NATIVE_REF(NSWindow);
ADD_COCOA_NATIVE_REF(NSButton);


/** Event handler callback. */
typedef bool (*PFNVBOXCACALLBACK)(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);

/** Native notification callback type for QObject. */
typedef void (*PfnNativeNotificationCallbackForQObject)(QObject *pObject, const QMap<QString, QString> &userInfo);
/** Native notification callback type for QWidget. */
typedef void (*PfnNativeNotificationCallbackForQWidget)(const QString &strNativeNotificationName, QWidget *pWidget);
/** Standard window button callback type for QWidget. */
typedef void (*PfnStandardWindowButtonCallbackForQWidget)(StandardWindowButtonType emnButtonType, bool fWithOptionKey, QWidget *pWidget);


/** Singleton prototype for our private NSApplication object. */
class SHARED_LIBRARY_STUFF UICocoaApplication
{
public:

    /** Creates singleton instance.
      * @param  fPreventAppNap  Brings whether we should prevent AppNap. */
    static void create(bool fPreventAppNap);
    /** Returns singleton instance. */
    static UICocoaApplication *instance();

    /** Destructs cocoa application. */
    virtual ~UICocoaApplication();

    /** Returns whether we should prevent AppNap. */
    bool isPreventAppNap() const { return m_fPreventAppNap; }

    /** Returns whether application is currently active. */
    bool isActive() const;

    /** Returns whether app is in Dark mode. */
    bool isDarkMode() const;

    /** Hides the application. */
    void hide();

    /** Hides user elements such as menu-bar and dock. */
    void hideUserElements();

    /** Register native @a pfnCallback of the @a pvUser taking event @a fMask into account. */
    void registerForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);
    /** Unregister native @a pfnCallback of the @a pvUser taking event @a fMask into account. */
    void unregisterForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);

    /** Register passed @a pObject to native notification @a strNativeNotificationName, using @a pCallback as handler. */
    void registerToNotificationOfWorkspace(const QString &strNativeNotificationName, QObject *pObject, PfnNativeNotificationCallbackForQObject pCallback);
    /** Unregister passed @a pWidget from native notification @a strNativeNotificationName. */
    void unregisterFromNotificationOfWorkspace(const QString &strNativeNotificationName, QObject *pObject);

    /** Register passed @a pWidget to native notification @a strNativeNotificationName, using @a pCallback as handler. */
    void registerToNotificationOfWindow(const QString &strNativeNotificationName, QWidget *pWidget, PfnNativeNotificationCallbackForQWidget pCallback);
    /** Unregister passed @a pWidget from native notification @a strNativeNotificationName. */
    void unregisterFromNotificationOfWindow(const QString &strNativeNotificationName, QWidget *pWidget);

    /** Redirects native notification @a pstrNativeNotificationName for application to registered listener. */
    void nativeNotificationProxyForObject(NativeNSStringRef pstrNativeNotificationName, const QMap<QString, QString> &userInfo);
    /** Redirects native notification @a pstrNativeNotificationName for window @a pWindow to registered listener. */
    void nativeNotificationProxyForWidget(NativeNSStringRef pstrNativeNotificationName, NativeNSWindowRef pWindow);

    /** Register callback for standard window @a buttonType of passed @a pWidget as @a pCallback. */
    void registerCallbackForStandardWindowButton(QWidget *pWidget, StandardWindowButtonType enmButtonType, PfnStandardWindowButtonCallbackForQWidget pCallback);
    /** Unregister callback for standard window @a buttonType of passed @a pWidget. */
    void unregisterCallbackForStandardWindowButton(QWidget *pWidget, StandardWindowButtonType enmButtonType);
    /** Redirects standard window button selector to registered callback. */
    void nativeCallbackProxyForStandardWindowButton(NativeNSButtonRef pButton, bool fWithOptionKey);

private:

    /** Constructs cocoa application.
      * @param  fPreventAppNap  Brings whether we should prevent AppNap. */
    UICocoaApplication(bool fPreventAppNap);

    /** Holds the singleton access instance. */
    static UICocoaApplication *s_pInstance;

    /** Holds whether we should prevent AppNap. */
    const bool  m_fPreventAppNap;

    /** Holds the private NSApplication instance. */
    NativeUICocoaApplicationPrivateRef  m_pNative;
    /** Holds the private NSAutoreleasePool instance. */
    NativeNSAutoreleasePoolRef          m_pPool;

    /** Map of notification callbacks registered for corresponding QObject(s). */
    QMap<QObject*, QMap<QString, PfnNativeNotificationCallbackForQObject> >  m_objectCallbacks;
    /** Map of notification callbacks registered for corresponding QWidget(s). */
    QMap<QWidget*, QMap<QString, PfnNativeNotificationCallbackForQWidget> >  m_widgetCallbacks;

    /** Map of callbacks registered for standard window button(s) of corresponding QWidget(s). */
    QMap<QWidget*, QMap<StandardWindowButtonType, PfnStandardWindowButtonCallbackForQWidget> >  m_stdWindowButtonCallbacks;
};

#endif /* !FEQT_INCLUDED_SRC_platform_darwin_UICocoaApplication_h */

