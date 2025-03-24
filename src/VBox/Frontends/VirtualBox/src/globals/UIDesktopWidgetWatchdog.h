/* $Id: UIDesktopWidgetWatchdog.h 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDesktopWidgetWatchdog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIDesktopWidgetWatchdog_h
#define FEQT_INCLUDED_SRC_globals_UIDesktopWidgetWatchdog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QWindow>
#ifdef VBOX_WS_NIX
# include <QRect>
# include <QVector>
#endif

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QScreen;

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
/** Desktop Watchdog / Synthetic Test policy type. */
enum DesktopWatchdogPolicy_SynthTest
{
    DesktopWatchdogPolicy_SynthTest_Disabled,
    DesktopWatchdogPolicy_SynthTest_ManagerOnly,
    DesktopWatchdogPolicy_SynthTest_MachineOnly,
    DesktopWatchdogPolicy_SynthTest_Both
};
Q_DECLARE_METATYPE(DesktopWatchdogPolicy_SynthTest);
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

/** Singleton QObject extension used as desktop-widget
  * watchdog aware of the host-screen geometry changes. */
class SHARED_LIBRARY_STUFF UIDesktopWidgetWatchdog : public QObject
{
    Q_OBJECT;

    /** Constructs desktop-widget watchdog. */
    UIDesktopWidgetWatchdog();
    /** Destructs desktop-widget watchdog. */
    virtual ~UIDesktopWidgetWatchdog() /* override final */;

signals:

    /** Notifies about host-screen count change to @a cHostScreenCount. */
    void sigHostScreenCountChanged(int cHostScreenCount);

    /** Notifies about resize for the host-screen with @a iHostScreenIndex. */
    void sigHostScreenResized(int iHostScreenIndex);

    /** Notifies about work-area resize for the host-screen with @a iHostScreenIndex. */
    void sigHostScreenWorkAreaResized(int iHostScreenIndex);

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /** Notifies about work-area recalculated for the host-screen with @a iHostScreenIndex. */
    void sigHostScreenWorkAreaRecalculated(int iHostScreenIndex);
#endif

public:

    /** Returns the static instance of the desktop-widget watchdog. */
    static UIDesktopWidgetWatchdog *instance() { return s_pInstance; }

    /** Creates the static instance of the desktop-widget watchdog. */
    static void create();
    /** Destroys the static instance of the desktop-widget watchdog. */
    static void destroy();

    /** Returns the number of host-screens currently available on the system. */
    static int screenCount();

    /** Returns primary screen index. */
    static int primaryScreenNumber();
    /** Returns the index of the screen which contains contains @a pWidget. */
    static int screenNumber(const QWidget *pWidget);
    /** Returns the index of the screen which contains contains @a point. */
    static int screenNumber(const QPoint &point);

    /** Returns the geometry of the host @a pScreen. */
    QRect screenGeometry(QScreen *pScreen) const;
    /** Returns the geometry of the host-screen with @a iHostScreenIndex.
      * @note The default screen is used if @a iHostScreenIndex is -1. */
    QRect screenGeometry(int iHostScreenIndex = -1) const;
    /** Returns the geometry of the host-screen which contains @a pWidget. */
    QRect screenGeometry(const QWidget *pWidget) const;
    /** Returns the geometry of the host-screen which contains @a point. */
    QRect screenGeometry(const QPoint &point) const;

    /** Returns the available-geometry of the host @a pScreen. */
    QRect availableGeometry(QScreen *pScreen) const;
    /** Returns the available-geometry of the host-screen with @a iHostScreenIndex.
      * @note The default screen is used if @a iHostScreenIndex is -1. */
    QRect availableGeometry(int iHostScreenIndex = -1) const;
    /** Returns the available-geometry of the host-screen which contains @a pWidget. */
    QRect availableGeometry(const QWidget *pWidget) const;
    /** Returns the available-geometry of the host-screen which contains @a point. */
    QRect availableGeometry(const QPoint &point) const;

    /** Returns overall region unifying all the host-screen geometries. */
    static QRegion overallScreenRegion();
    /** Returns overall region unifying all the host-screen available-geometries. */
    static QRegion overallAvailableRegion();

#ifdef VBOX_WS_NIX
    /** Qt5: X11: Returns whether no or fake screen detected. */
    static bool isFakeScreenDetected();
#endif

    /** Returns device-pixel-ratio of the host-screen with @a iHostScreenIndex. */
    static double devicePixelRatio(int iHostScreenIndex = -1);
    /** Returns device-pixel-ratio of the host-screen which contains @a pWidget. */
    static double devicePixelRatio(QWidget *pWidget);

    /** Search position for @a rectangle to make sure it is fully
      * contained within @a boundRegion, performing resize if allowed. */
    static QRect normalizeGeometry(const QRect &rectangle,
                                   const QRegion &boundRegion,
                                   bool fCanResize = true);
    /** Ensures that the given rectangle @a rectangle is fully
      * contained within the region @a boundRegion, performing resize if allowed. */
    static QRect getNormalized(const QRect &rectangle,
                               const QRegion &boundRegion,
                               bool fCanResize = true);
    /** Aligns the center of @a pWidget with the center
      * of @a pRelative, performing resize if allowed. */
    void centerWidget(QWidget *pWidget,
                      QWidget *pRelative,
                      bool fCanResize = true) const;
    /** Restores and exposes the @a pWidget on desktop. */
    static void restoreWidget(QWidget *pWidget);

    /** Assigns top-level @a pWidget geometry passed as QRect coordinates.
      * @note  Take into account that this request may fail on X11. */
    static void setTopLevelGeometry(QWidget *pWidget, int x, int y, int w, int h);
    /** Assigns top-level @a pWidget geometry passed as @a rect.
      * @note  Take into account that this request may fail on X11. */
    static void setTopLevelGeometry(QWidget *pWidget, const QRect &rect);

    /** Activates the specified window with given @a wId. Can @a fSwitchDesktop if requested. */
    static bool activateWindow(WId wId, bool fSwitchDesktop = true);

private slots:

    /** Handles @a pHostScreen adding. */
    void sltHostScreenAdded(QScreen *pHostScreen);
    /** Handles @a pHostScreen removing. */
    void sltHostScreenRemoved(QScreen *pHostScreen);
    /** Handles host-screen resize to passed @a geometry. */
    void sltHandleHostScreenResized(const QRect &geometry);
    /** Handles host-screen work-area resize to passed @a availableGeometry. */
    void sltHandleHostScreenWorkAreaResized(const QRect &availableGeometry);

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /** Handles @a availableGeometry calculation result for the host-screen with @a iHostScreenIndex. */
    void sltHandleHostScreenAvailableGeometryCalculated(int iHostScreenIndex, QRect availableGeometry);
#endif

private:

    /** Prepare routine. */
    void prepare();
    /** Cleanup routine. */
    void cleanup();

    /** Returns index of passed @a pScreen. */
    static int screenToIndex(QScreen *pScreen);

    /** Returns the flipped (transposed) @a region. */
    static QRegion flip(const QRegion &region);

    /** Holds the static instance of the desktop-widget watchdog. */
    static UIDesktopWidgetWatchdog *s_pInstance;

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /** Returns whether Synthetic Test is restricted according to cached policy. */
    bool isSynchTestRestricted() const;

    /** Updates host-screen configuration according to new @a cHostScreenCount.
      * @note If cHostScreenCount is equal to -1 we have to acquire it ourselves. */
    void updateHostScreenConfiguration(int cHostScreenCount = -1);

    /** Update available-geometry for the host-screen with @a iHostScreenIndex. */
    void updateHostScreenAvailableGeometry(int iHostScreenIndex);

    /** Cleanups existing workers. */
    void cleanupExistingWorkers();

    /** Holds environment variable name for Desktop Watchdog / Synthetic Test policy type. */
    static const QString s_strVBoxDesktopWatchdogPolicySynthTest;

    /** Holds the cached Synthetic Test policy. */
    DesktopWatchdogPolicy_SynthTest  m_enmSynthTestPolicy;

    /** Holds current host-screen available-geometries. */
    QVector<QRect>    m_availableGeometryData;
    /** Holds current workers determining host-screen available-geometries. */
    QVector<QWidget*> m_availableGeometryWorkers;
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
};

/** 'Official' name for the desktop-widget watchdog singleton. */
#define gpDesktop UIDesktopWidgetWatchdog::instance()

#endif /* !FEQT_INCLUDED_SRC_globals_UIDesktopWidgetWatchdog_h */
