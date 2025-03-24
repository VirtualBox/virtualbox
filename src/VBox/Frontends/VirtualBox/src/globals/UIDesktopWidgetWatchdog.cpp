/* $Id: UIDesktopWidgetWatchdog.cpp 108917 2025-03-24 15:53:56Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDesktopWidgetWatchdog class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QWidget>
#include <QScreen>
#ifdef VBOX_WS_WIN
# include <QLibrary>
#endif
#ifdef VBOX_WS_NIX
# include <QTimer>
#endif

/* GUI includes: */
#include "UIDesktopWidgetWatchdog.h"
#include "UILoggingDefs.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif
#ifdef VBOX_WS_WIN
# include "VBoxUtils-win.h"
#endif
#ifdef VBOX_WS_NIX
# include "UICommon.h"
# include "VBoxUtils-nix.h"
# ifndef VBOX_GUI_WITH_CUSTOMIZATIONS1
#  include "UIConverter.h"
# endif
#endif

/* Other VBox includes: */
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ldr.h>
#ifdef VBOX_WS_WIN
# include <iprt/win/windows.h>
#endif

/* External includes: */
#include <math.h>
#ifdef VBOX_WS_NIX
# include <xcb/xcb.h>
#endif


#ifdef VBOX_WS_WIN

# ifndef DPI_ENUMS_DECLARED
typedef enum _MONITOR_DPI_TYPE // gently stolen from MSDN
{
    MDT_EFFECTIVE_DPI  = 0,
    MDT_ANGULAR_DPI    = 1,
    MDT_RAW_DPI        = 2,
    MDT_DEFAULT        = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
# endif
typedef void (WINAPI *PFN_GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT *, UINT *);

/** Set when dynamic API import is reoslved. */
static bool volatile        g_fResolved;
/** Pointer to Shcore.dll!GetDpiForMonitor, introduced in windows 8.1. */
static PFN_GetDpiForMonitor g_pfnGetDpiForMonitor = NULL;

/** @returns true if all APIs found, false if missing APIs  */
static bool ResolveDynamicImports(void)
{
    if (!g_fResolved)
    {
        PFN_GetDpiForMonitor pfn = (decltype(pfn))RTLdrGetSystemSymbol("Shcore.dll", "GetDpiForMonitor");
        g_pfnGetDpiForMonitor = pfn;
        ASMCompilerBarrier();

        g_fResolved = true;
    }
    return g_pfnGetDpiForMonitor != NULL;
}

static BOOL CALLBACK MonitorEnumProcF(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lpClipRect, LPARAM dwData) RT_NOTHROW_DEF
{
    /* These required for clipped screens only: */
    RT_NOREF(hdcMonitor, lpClipRect);

    /* Acquire effective DPI (available since Windows 8.1): */
    AssertReturn(g_pfnGetDpiForMonitor, false);
    UINT uOutX = 0;
    UINT uOutY = 0;
    g_pfnGetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &uOutX, &uOutY);
    reinterpret_cast<QList<QPair<int, int> >*>(dwData)->append(qMakePair(uOutX, uOutY));

    return TRUE;
}

#endif /* VBOX_WS_WIN */


#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)

/* static */
const QString
UIDesktopWidgetWatchdog::s_strVBoxDesktopWatchdogPolicySynthTest = "VBOX_DESKTOPWATCHDOGPOLICY_SYNTHTEST";

/** QWidget extension used as
  * an invisible window on the basis of which we
  * can calculate available host-screen geometry. */
class UIInvisibleWindow : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about host-screen available-geometry was calulated.
      * @param iHostScreenIndex  holds the index of the host-screen this window created for.
      * @param availableGeometry holds the available-geometry of the host-screen this window created for. */
    void sigHostScreenAvailableGeometryCalculated(int iHostScreenIndex, QRect availableGeometry);

public:

    /** Constructs invisible window for the host-screen with @a iHostScreenIndex. */
    UIInvisibleWindow(int iHostScreenIndex);

private slots:

    /** Performs fallback drop. */
    void sltFallback();

private:

    /** Move @a pEvent handler. */
    void moveEvent(QMoveEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Resize @a pEvent handler. */
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Holds the index of the host-screen this window created for. */
    const int m_iHostScreenIndex;

    /** Holds whether the move event came. */
    bool m_fMoveCame;
    /** Holds whether the resize event came. */
    bool m_fResizeCame;
};


/*********************************************************************************************************************************
*   Class UIInvisibleWindow implementation.                                                                                      *
*********************************************************************************************************************************/

UIInvisibleWindow::UIInvisibleWindow(int iHostScreenIndex)
    : QWidget(0, Qt::Window | Qt::FramelessWindowHint)
    , m_iHostScreenIndex(iHostScreenIndex)
    , m_fMoveCame(false)
    , m_fResizeCame(false)
{
    /* Resize to minimum size of 1 pixel: */
    resize(1, 1);
    /* Apply visual and mouse-event mask for that 1 pixel: */
    setMask(QRect(0, 0, 1, 1));
    /* For composite WMs make this 1 pixel transparent: */
    if (uiCommon().isCompositingManagerRunning())
        setAttribute(Qt::WA_TranslucentBackground);
    /* Install fallback handler: */
    QTimer::singleShot(5000, this, SLOT(sltFallback()));
}

void UIInvisibleWindow::sltFallback()
{
    /* Sanity check for fallback geometry: */
    QRect fallbackGeometry(x(), y(), width(), height());
    if (   fallbackGeometry.width() <= 1
        || fallbackGeometry.height() <= 1)
        fallbackGeometry = gpDesktop->screenGeometry(m_iHostScreenIndex);
    LogRel(("GUI: UIInvisibleWindow::sltFallback: %s event missing. "
            "Screen: %d, work area: %dx%d x %dx%d\n",
            !m_fMoveCame ? "Move" : !m_fResizeCame ? "Resize" : "Some",
            m_iHostScreenIndex, fallbackGeometry.x(), fallbackGeometry.y(), fallbackGeometry.width(), fallbackGeometry.height()));
    emit sigHostScreenAvailableGeometryCalculated(m_iHostScreenIndex, fallbackGeometry);
}

void UIInvisibleWindow::moveEvent(QMoveEvent *pEvent)
{
    /* We do have both move and resize events,
     * with no idea who will come first, but we need
     * to send a final signal after last of events arrived. */

    /* Call to base-class: */
    QWidget::moveEvent(pEvent);

    /* Ignore 'not-yet-shown' case: */
    if (!isVisible())
        return;

    /* Mark move event as received: */
    m_fMoveCame = true;

    /* If the resize event already came: */
    if (m_fResizeCame)
    {
        /* Notify listeners about host-screen available-geometry was calulated: */
        LogRel2(("GUI: UIInvisibleWindow::moveEvent: Screen: %d, work area: %dx%d x %dx%d\n", m_iHostScreenIndex,
                 x(), y(), width(), height()));
        emit sigHostScreenAvailableGeometryCalculated(m_iHostScreenIndex, QRect(x(), y(), width(), height()));
    }
}

void UIInvisibleWindow::resizeEvent(QResizeEvent *pEvent)
{
    /* We do have both move and resize events,
     * with no idea who will come first, but we need
     * to send a final signal after last of events arrived. */

    /* Call to base-class: */
    QWidget::resizeEvent(pEvent);

    /* Ignore 'not-yet-shown' case: */
    if (!isVisible())
        return;

    /* Mark resize event as received: */
    m_fResizeCame = true;

    /* If the move event already came: */
    if (m_fMoveCame)
    {
        /* Notify listeners about host-screen available-geometry was calulated: */
        LogRel2(("GUI: UIInvisibleWindow::resizeEvent: Screen: %d, work area: %dx%d x %dx%d\n", m_iHostScreenIndex,
                 x(), y(), width(), height()));
        emit sigHostScreenAvailableGeometryCalculated(m_iHostScreenIndex, QRect(x(), y(), width(), height()));
    }
}

#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */


/*********************************************************************************************************************************
*   Class UIDesktopWidgetWatchdog implementation.                                                                                *
*********************************************************************************************************************************/

/* static */
UIDesktopWidgetWatchdog *UIDesktopWidgetWatchdog::s_pInstance = 0;

/* static */
void UIDesktopWidgetWatchdog::create()
{
    /* Make sure instance isn't created: */
    AssertReturnVoid(!s_pInstance);

    /* Create/prepare instance: */
    new UIDesktopWidgetWatchdog;
    AssertReturnVoid(s_pInstance);
    s_pInstance->prepare();
}

/* static */
void UIDesktopWidgetWatchdog::destroy()
{
    /* Make sure instance is created: */
    AssertReturnVoid(s_pInstance);

    /* Cleanup/destroy instance: */
    s_pInstance->cleanup();
    delete s_pInstance;
    AssertReturnVoid(!s_pInstance);
}

UIDesktopWidgetWatchdog::UIDesktopWidgetWatchdog()
#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    : m_enmSynthTestPolicy(DesktopWatchdogPolicy_SynthTest_Both)
#endif
{
    /* Initialize instance: */
    s_pInstance = this;
}

UIDesktopWidgetWatchdog::~UIDesktopWidgetWatchdog()
{
    /* Deinitialize instance: */
    s_pInstance = 0;
}

/* static */
int UIDesktopWidgetWatchdog::screenCount()
{
    return QGuiApplication::screens().size();
}

/* static */
int UIDesktopWidgetWatchdog::primaryScreenNumber()
{
    return screenToIndex(QGuiApplication::primaryScreen());
}

/* static */
int UIDesktopWidgetWatchdog::screenNumber(const QWidget *pWidget)
{
    QScreen *pScreen = 0;
    if (pWidget)
        if (QWindow *pWindow = pWidget->windowHandle())
            pScreen = pWindow->screen();

    return screenToIndex(pScreen);
}

/* static */
int UIDesktopWidgetWatchdog::screenNumber(const QPoint &point)
{
    return screenToIndex(QGuiApplication::screenAt(point));
}

QRect UIDesktopWidgetWatchdog::screenGeometry(QScreen *pScreen) const
{
    /* Just return screen geometry: */
    return pScreen->geometry();
}

QRect UIDesktopWidgetWatchdog::screenGeometry(int iHostScreenIndex /* = -1 */) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::screens().value(iHostScreenIndex, QGuiApplication::primaryScreen());

    /* Redirect call to wrapper above: */
    return screenGeometry(pScreen);
}

QRect UIDesktopWidgetWatchdog::screenGeometry(const QWidget *pWidget) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::primaryScreen();
    if (pWidget)
        if (QWindow *pWindow = pWidget->windowHandle())
            pScreen = pWindow->screen();

    /* Redirect call to wrapper above: */
    return screenGeometry(pScreen);
}

QRect UIDesktopWidgetWatchdog::screenGeometry(const QPoint &point) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::screenAt(point);
    if (!pScreen)
        pScreen = QGuiApplication::primaryScreen();

    /* Redirect call to wrapper above: */
    return screenGeometry(pScreen);
}

QRect UIDesktopWidgetWatchdog::availableGeometry(QScreen *pScreen) const
{
#ifdef VBOX_WS_NIX
# ifdef VBOX_GUI_WITH_CUSTOMIZATIONS1
    // WORKAROUND:
    // For customer WM we don't want Qt to return wrong available geometry,
    // so we are returning fallback screen geometry in any case..
    return screenGeometry(pScreen);
# else /* !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
    /* Get cached available-geometry: */
    const QRect availableGeometry = m_availableGeometryData.value(screenToIndex(pScreen));
    /* Return cached available-geometry if it's valid or screen-geometry otherwise: */
    return availableGeometry.isValid() ? availableGeometry : screenGeometry(pScreen);
# endif /* !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
#else /* !VBOX_WS_NIX */
    /* Just return screen available-geometry: */
    return pScreen->availableGeometry();
#endif /* !VBOX_WS_NIX */
}

QRect UIDesktopWidgetWatchdog::availableGeometry(int iHostScreenIndex /* = -1 */) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::screens().value(iHostScreenIndex, QGuiApplication::primaryScreen());

    /* Redirect call to wrapper above: */
    return availableGeometry(pScreen);
}

QRect UIDesktopWidgetWatchdog::availableGeometry(const QWidget *pWidget) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::primaryScreen();
    if (pWidget)
        if (QWindow *pWindow = pWidget->windowHandle())
            pScreen = pWindow->screen();

    /* Redirect call to wrapper above: */
    return availableGeometry(pScreen);
}

QRect UIDesktopWidgetWatchdog::availableGeometry(const QPoint &point) const
{
    /* Gather suitable screen, use primary if failed: */
    QScreen *pScreen = QGuiApplication::screenAt(point);
    if (!pScreen)
        pScreen = QGuiApplication::primaryScreen();

    /* Redirect call to wrapper above: */
    return availableGeometry(pScreen);
}

/* static */
QRegion UIDesktopWidgetWatchdog::overallScreenRegion()
{
    /* Calculate region: */
    QRegion region;
    foreach (QScreen *pScreen, QGuiApplication::screens())
        region += gpDesktop->screenGeometry(pScreen);
    return region;
}

/* static */
QRegion UIDesktopWidgetWatchdog::overallAvailableRegion()
{
    /* Calculate region: */
    QRegion region;
    foreach (QScreen *pScreen, QGuiApplication::screens())
    {
        /* Get enumerated screen's available area: */
        QRect rect = gpDesktop->availableGeometry(pScreen);
#ifdef VBOX_WS_WIN
        /* On Windows host window can exceed the available
         * area in maximized/sticky-borders state: */
        rect.adjust(-10, -10, 10, 10);
#endif /* VBOX_WS_WIN */
        /* Append rectangle: */
        region += rect;
    }
    /* Return region: */
    return region;
}

#ifdef VBOX_WS_NIX
/* static */
bool UIDesktopWidgetWatchdog::isFakeScreenDetected()
{
    // WORKAROUND:
    // In 5.6.1 Qt devs taught the XCB plugin to silently swap last detached screen
    // with a fake one, and there is no API-way to distinguish fake from real one
    // because all they do is erasing output for the last real screen, keeping
    // all other screen attributes stale. Gladly output influencing screen name
    // so we can use that horrible workaround to detect a fake XCB screen.
    return    qApp->screens().size() == 0 /* zero-screen case is impossible after 5.6.1 */
           || (qApp->screens().size() == 1 && qApp->screens().first()->name() == ":0.0");
}
#endif /* VBOX_WS_NIX */

/* static */
double UIDesktopWidgetWatchdog::devicePixelRatio(int iHostScreenIndex /* = -1 */)
{
    /* First, we should check whether the screen is valid: */
    QScreen *pScreen = iHostScreenIndex == -1
                     ? QGuiApplication::primaryScreen()
                     : QGuiApplication::screens().value(iHostScreenIndex);
    AssertPtrReturn(pScreen, 1.0);

    /* Then acquire device-pixel-ratio: */
    return pScreen->devicePixelRatio();
}

/* static */
double UIDesktopWidgetWatchdog::devicePixelRatio(QWidget *pWidget)
{
    /* Redirect call to wrapper above: */
    return devicePixelRatio(screenNumber(pWidget));
}

/* static */
double UIDesktopWidgetWatchdog::devicePixelRatioActual(int iHostScreenIndex /* = -1 */)
{
    /* First, we should check whether the screen is valid: */
    QScreen *pScreen = 0;
    if (iHostScreenIndex == -1)
    {
        pScreen = QGuiApplication::primaryScreen();
        iHostScreenIndex = QGuiApplication::screens().indexOf(pScreen);
    }
    else
        pScreen = QGuiApplication::screens().value(iHostScreenIndex);
    AssertPtrReturn(pScreen, 1.0);

#ifdef VBOX_WS_WIN
    /* Enumerate available monitors through EnumDisplayMonitors if GetDpiForMonitor is available: */
    if (ResolveDynamicImports())
    {
        QList<QPair<int, int> > listOfScreenDPI;
        EnumDisplayMonitors(0, 0, MonitorEnumProcF, (LPARAM)&listOfScreenDPI);
        if (iHostScreenIndex >= 0 && iHostScreenIndex < listOfScreenDPI.size())
        {
            const QPair<int, int> dpiPair = listOfScreenDPI.at(iHostScreenIndex);
            if (dpiPair.first > 0)
                return (double)dpiPair.first / 96 /* dpi unawarness value */;
        }
    }
#else /* !VBOX_WS_WIN */
    Q_UNUSED(iHostScreenIndex);
#endif /* !VBOX_WS_WIN */

    /* Then acquire device-pixel-ratio: */
    return pScreen->devicePixelRatio();
}

/* static */
double UIDesktopWidgetWatchdog::devicePixelRatioActual(QWidget *pWidget)
{
    /* Redirect call to wrapper above: */
    return devicePixelRatioActual(screenNumber(pWidget));
}

/* static */
QRect UIDesktopWidgetWatchdog::normalizeGeometry(const QRect &rectangle,
                                                 const QRegion &boundRegion,
                                                 bool fCanResize /* = true */)
{
    /* Perform direct and flipped search of position for @a rectangle to make sure it is fully contained
     * inside @a boundRegion region by moving & resizing (if @a fCanResize is specified) @a rectangle if
     * necessary. Selects the minimum shifted result between direct and flipped variants. */

    /* Direct search for normalized rectangle: */
    QRect var1(getNormalized(rectangle, boundRegion, fCanResize));

    /* Flipped search for normalized rectangle: */
    QRect var2(flip(getNormalized(flip(rectangle).boundingRect(),
                                  flip(boundRegion), fCanResize)).boundingRect());

    /* Calculate shift from starting position for both variants: */
    double dLength1 = sqrt(pow((double)(var1.x() - rectangle.x()), (double)2) +
                           pow((double)(var1.y() - rectangle.y()), (double)2));
    double dLength2 = sqrt(pow((double)(var2.x() - rectangle.x()), (double)2) +
                           pow((double)(var2.y() - rectangle.y()), (double)2));

    /* Return minimum shifted variant: */
    return dLength1 > dLength2 ? var2 : var1;
}

/* static */
QRect UIDesktopWidgetWatchdog::getNormalized(const QRect &rectangle,
                                             const QRegion &boundRegion,
                                             bool /* fCanResize = true */)
{
    /* Ensures that the given rectangle @a rectangle is fully contained within the region @a boundRegion
     * by moving @a rectangle if necessary. If @a rectangle is larger than @a boundRegion, top left
     * corner of @a rectangle is aligned with the top left corner of maximum available rectangle and,
     * if @a fCanResize is true, @a rectangle is shrinked to become fully visible. */

    /* Storing available horizontal sub-rectangles & vertical shifts: */
    const int iWindowVertical = rectangle.center().y();
    QList<QRect> rectanglesList;
    QList<int> shiftsList;
    for (QRegion::const_iterator it = boundRegion.begin(); it != boundRegion.end(); ++it)
    {
        QRect currentItem = *it;
        const int iCurrentDelta = qAbs(iWindowVertical - currentItem.center().y());
        const int iShift2Top = currentItem.top() - rectangle.top();
        const int iShift2Bot = currentItem.bottom() - rectangle.bottom();

        int iTtemPosition = 0;
        foreach (QRect item, rectanglesList)
        {
            const int iDelta = qAbs(iWindowVertical - item.center().y());
            if (iDelta > iCurrentDelta)
                break;
            else
                ++iTtemPosition;
        }
        rectanglesList.insert(iTtemPosition, currentItem);

        int iShift2TopPos = 0;
        foreach (int iShift, shiftsList)
            if (qAbs(iShift) > qAbs(iShift2Top))
                break;
            else
                ++iShift2TopPos;
        shiftsList.insert(iShift2TopPos, iShift2Top);

        int iShift2BotPos = 0;
        foreach (int iShift, shiftsList)
            if (qAbs(iShift) > qAbs(iShift2Bot))
                break;
            else
                ++iShift2BotPos;
        shiftsList.insert(iShift2BotPos, iShift2Bot);
    }

    /* Trying to find the appropriate place for window: */
    QRect result;
    for (int i = -1; i < shiftsList.size(); ++i)
    {
        /* Move to appropriate vertical: */
        QRect newRectangle(rectangle);
        if (i >= 0)
            newRectangle.translate(0, shiftsList[i]);

        /* Search horizontal shift: */
        int iMaxShift = 0;
        foreach (QRect item, rectanglesList)
        {
            QRect trectangle(newRectangle.translated(item.left() - newRectangle.left(), 0));
            if (!item.intersects(trectangle))
                continue;

            if (newRectangle.left() < item.left())
            {
                const int iShift = item.left() - newRectangle.left();
                iMaxShift = qAbs(iShift) > qAbs(iMaxShift) ? iShift : iMaxShift;
            }
            else if (newRectangle.right() > item.right())
            {
                const int iShift = item.right() - newRectangle.right();
                iMaxShift = qAbs(iShift) > qAbs(iMaxShift) ? iShift : iMaxShift;
            }
        }

        /* Shift across the horizontal direction: */
        newRectangle.translate(iMaxShift, 0);

        /* Check the translated rectangle to feat the rules: */
        if (boundRegion.united(newRectangle) == boundRegion)
            result = newRectangle;

        if (!result.isNull())
            break;
    }

    if (result.isNull())
    {
        /* Resize window to feat desirable size
         * using max of available rectangles: */
        QRect maxRectangle;
        quint64 uMaxSquare = 0;
        foreach (QRect item, rectanglesList)
        {
            const quint64 uSquare = item.width() * item.height();
            if (uSquare > uMaxSquare)
            {
                uMaxSquare = uSquare;
                maxRectangle = item;
            }
        }

        result = rectangle;
        result.moveTo(maxRectangle.x(), maxRectangle.y());
        if (maxRectangle.right() < result.right())
            result.setRight(maxRectangle.right());
        if (maxRectangle.bottom() < result.bottom())
            result.setBottom(maxRectangle.bottom());
    }

    return result;
}

void UIDesktopWidgetWatchdog::centerWidget(QWidget *pWidget,
                                           QWidget *pRelative,
                                           bool fCanResize /* = true */) const
{
    /* If necessary, pWidget's position is adjusted to make it fully visible within
     * the available desktop area. If pWidget is bigger then this area, it will also
     * be resized unless fCanResize is false or there is an inappropriate minimum
     * size limit (in which case the top left corner will be simply aligned with the top
     * left corner of the available desktop area). pWidget must be a top-level widget.
     * pRelative may be any widget, but if it's not top-level itself, its top-level
     * widget will be used for calculations. pRelative can also be NULL, in which case
     * pWidget will be centered relative to the available desktop area. */

    AssertReturnVoid(pWidget);
    AssertReturnVoid(pWidget->isWindow());

    QRect deskGeo, parentGeo;
    if (pRelative)
    {
        pRelative = pRelative->window();
        deskGeo = availableGeometry(pRelative);
        parentGeo = pRelative->frameGeometry();
        // WORKAROUND:
        // On X11/Gnome, geo/frameGeo.x() and y() are always 0 for top level
        // widgets with parents, what a shame. Use mapToGlobal() to workaround.
        QPoint d = pRelative->mapToGlobal(QPoint(0, 0));
        d.rx() -= pRelative->geometry().x() - pRelative->x();
        d.ry() -= pRelative->geometry().y() - pRelative->y();
        parentGeo.moveTopLeft(d);
    }
    else
    {
        deskGeo = availableGeometry();
        parentGeo = deskGeo;
    }

    // WORKAROUND:
    // On X11, there is no way to determine frame geometry (including WM
    // decorations) before the widget is shown for the first time. Stupidly
    // enumerate other top level widgets to find the thickest frame. The code
    // is based on the idea taken from QDialog::adjustPositionInternal().

    int iExtraW = 0;
    int iExtraH = 0;

    QWidgetList list = QApplication::topLevelWidgets();
    QListIterator<QWidget*> it(list);
    while ((iExtraW == 0 || iExtraH == 0) && it.hasNext())
    {
        int iFrameW, iFrameH;
        QWidget *pCurrent = it.next();
        if (!pCurrent->isVisible())
            continue;

        iFrameW = pCurrent->frameGeometry().width() - pCurrent->width();
        iFrameH = pCurrent->frameGeometry().height() - pCurrent->height();

        iExtraW = qMax(iExtraW, iFrameW);
        iExtraH = qMax(iExtraH, iFrameH);
    }

    /* On non-X11 platforms, the following would be enough instead of the above workaround: */
    // QRect geo = frameGeometry();
    QRect geo = QRect(0, 0, pWidget->width() + iExtraW,
                            pWidget->height() + iExtraH);

    geo.moveCenter(QPoint(parentGeo.x() + (parentGeo.width() - 1) / 2,
                          parentGeo.y() + (parentGeo.height() - 1) / 2));

    /* Ensure the widget is within the available desktop area: */
    QRect newGeo = normalizeGeometry(geo, deskGeo, fCanResize);
#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // No idea why, but Qt doesn't respect if there is a unified toolbar on the
    // ::move call. So manually add the height of the toolbar before setting
    // the position.
    if (pRelative)
        newGeo.translate(0, ::darwinWindowToolBarHeight(pWidget));
#endif /* VBOX_WS_MAC */

    pWidget->move(newGeo.topLeft());

    if (   fCanResize
        && (geo.width() != newGeo.width() || geo.height() != newGeo.height()))
        pWidget->resize(newGeo.width() - iExtraW, newGeo.height() - iExtraH);
}

/* static */
void UIDesktopWidgetWatchdog::restoreWidget(QWidget *pWidget)
{
    pWidget->show();
    pWidget->setWindowState(pWidget->windowState() & ~Qt::WindowMinimized);
    pWidget->activateWindow();
    pWidget->raise();
}

/* static */
void UIDesktopWidgetWatchdog::setTopLevelGeometry(QWidget *pWidget, int x, int y, int w, int h)
{
    AssertPtrReturnVoid(pWidget);
#ifdef VBOX_WS_NIX
# define QWINDOWSIZE_MAX ((1<<24)-1)
    if (pWidget->isWindow() && pWidget->isVisible() && uiCommon().X11ServerAvailable())
    {
        // WORKAROUND:
        // X11 window managers are not required to accept geometry changes on
        // the top-level window.  Unfortunately, current at Qt 5.6 and 5.7, Qt
        // assumes that the change will succeed, and resizes all sub-windows
        // unconditionally.  By calling ConfigureWindow directly, Qt will see
        // our change request as an externally triggered one on success and not
        // at all if it is rejected.
        const double dDPR = devicePixelRatio(pWidget);
        uint16_t fMask =   XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
                         | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t values[] = { (uint32_t)(x * dDPR), (uint32_t)(y * dDPR), (uint32_t)(w * dDPR), (uint32_t)(h * dDPR) };
        xcb_configure_window(NativeWindowSubsystem::X11GetConnection(), (xcb_window_t)pWidget->winId(),
                             fMask, values);
        xcb_size_hints_t hints;
        hints.flags =   1 /* XCB_ICCCM_SIZE_HINT_US_POSITION */
                      | 2 /* XCB_ICCCM_SIZE_HINT_US_SIZE */
                      | 512 /* XCB_ICCCM_SIZE_P_WIN_GRAVITY */;
        hints.x           = x * dDPR;
        hints.y           = y * dDPR;
        hints.width       = w * dDPR;
        hints.height      = h * dDPR;
        hints.min_width   = pWidget->minimumSize().width() * dDPR;
        hints.min_height  = pWidget->minimumSize().height() * dDPR;
        hints.max_width   = pWidget->maximumSize().width() * dDPR;
        hints.max_height  = pWidget->maximumSize().height() * dDPR;
        hints.width_inc   = pWidget->sizeIncrement().width() * dDPR;
        hints.height_inc  = pWidget->sizeIncrement().height() * dDPR;
        hints.base_width  = pWidget->baseSize().width() * dDPR;
        hints.base_height = pWidget->baseSize().height() * dDPR;
        hints.win_gravity = XCB_GRAVITY_STATIC;
        if (hints.min_width > 0 || hints.min_height > 0)
            hints.flags |= 16 /* XCB_ICCCM_SIZE_HINT_P_MIN_SIZE */;
        if (hints.max_width < QWINDOWSIZE_MAX || hints.max_height < QWINDOWSIZE_MAX)
            hints.flags |= 32 /* XCB_ICCCM_SIZE_HINT_P_MAX_SIZE */;
        if (hints.width_inc > 0 || hints.height_inc)
            hints.flags |=   64 /* XCB_ICCCM_SIZE_HINT_P_MIN_SIZE */
                           | 256 /* XCB_ICCCM_SIZE_HINT_BASE_SIZE */;
        xcb_change_property(NativeWindowSubsystem::X11GetConnection(), XCB_PROP_MODE_REPLACE,
                            (xcb_window_t)pWidget->winId(), XCB_ATOM_WM_NORMAL_HINTS,
                            XCB_ATOM_WM_SIZE_HINTS, 32, sizeof(hints) >> 2, &hints);
        xcb_flush(NativeWindowSubsystem::X11GetConnection());
    }
    else
        // WORKAROUND:
        // Call the Qt method if the window is not visible as otherwise no
        // Configure event will arrive to tell Qt what geometry we want.
        pWidget->setGeometry(x, y, w, h);
# else /* !VBOX_WS_NIX */
    pWidget->setGeometry(x, y, w, h);
# endif /* !VBOX_WS_NIX */
}

/* static */
void UIDesktopWidgetWatchdog::setTopLevelGeometry(QWidget *pWidget, const QRect &rect)
{
    UIDesktopWidgetWatchdog::setTopLevelGeometry(pWidget, rect.x(), rect.y(), rect.width(), rect.height());
}

/* static */
bool UIDesktopWidgetWatchdog::activateWindow(WId wId, bool fSwitchDesktop /* = true */)
{
    Q_UNUSED(fSwitchDesktop);
    bool fResult = true;

#if defined(VBOX_WS_WIN)

    fResult &= NativeWindowSubsystem::WinActivateWindow(wId, fSwitchDesktop);

#elif defined(VBOX_WS_NIX)

    fResult &= NativeWindowSubsystem::activateWindow(uiCommon().X11ServerAvailable(), wId, fSwitchDesktop);

#else

    NOREF(wId);
    NOREF(fSwitchDesktop);
    AssertFailed();
    fResult = false;

#endif

    if (!fResult)
        Log1WarningFunc(("Couldn't activate wId=%08X\n", wId));

    return fResult;
}

void UIDesktopWidgetWatchdog::sltHostScreenAdded(QScreen *pHostScreen)
{
//    printf("UIDesktopWidgetWatchdog::sltHostScreenAdded(%d)\n", screenCount());

    /* Listen for screen signals: */
    connect(pHostScreen, &QScreen::geometryChanged,
            this, &UIDesktopWidgetWatchdog::sltHandleHostScreenResized);
    connect(pHostScreen, &QScreen::availableGeometryChanged,
            this, &UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized);

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Update host-screen configuration: */
    updateHostScreenConfiguration();
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /* Notify listeners: */
    emit sigHostScreenCountChanged(screenCount());
}

void UIDesktopWidgetWatchdog::sltHostScreenRemoved(QScreen *pHostScreen)
{
//    printf("UIDesktopWidgetWatchdog::sltHostScreenRemoved(%d)\n", screenCount());

    /* Forget about screen signals: */
    disconnect(pHostScreen, &QScreen::geometryChanged,
               this, &UIDesktopWidgetWatchdog::sltHandleHostScreenResized);
    disconnect(pHostScreen, &QScreen::availableGeometryChanged,
               this, &UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized);

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Update host-screen configuration: */
    updateHostScreenConfiguration();
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /* Notify listeners: */
    emit sigHostScreenCountChanged(screenCount());
}

void UIDesktopWidgetWatchdog::sltHandleHostScreenResized(const QRect &geometry)
{
    /* Get the screen: */
    QScreen *pScreen = sender() ? qobject_cast<QScreen*>(sender()) : 0;
    AssertPtrReturnVoid(pScreen);

    /* Determine screen index: */
    const int iHostScreenIndex = qApp->screens().indexOf(pScreen);
    AssertReturnVoid(iHostScreenIndex != -1);
    LogRel(("GUI: UIDesktopWidgetWatchdog::sltHandleHostScreenResized: "
            "Screen %d is formally resized to: %dx%d x %dx%d\n",
            iHostScreenIndex, geometry.x(), geometry.y(),
            geometry.width(), geometry.height()));

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Update host-screen available-geometry: */
    updateHostScreenAvailableGeometry(iHostScreenIndex);
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /* Notify listeners: */
    emit sigHostScreenResized(iHostScreenIndex);
}

void UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized(const QRect &availableGeometry)
{
    /* Get the screen: */
    QScreen *pScreen = sender() ? qobject_cast<QScreen*>(sender()) : 0;
    AssertPtrReturnVoid(pScreen);

    /* Determine screen index: */
    const int iHostScreenIndex = qApp->screens().indexOf(pScreen);
    AssertReturnVoid(iHostScreenIndex != -1);
    LogRel(("GUI: UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized: "
            "Screen %d work area is formally resized to: %dx%d x %dx%d\n",
            iHostScreenIndex, availableGeometry.x(), availableGeometry.y(),
            availableGeometry.width(), availableGeometry.height()));

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Update host-screen available-geometry: */
    updateHostScreenAvailableGeometry(iHostScreenIndex);
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /* Notify listeners: */
    emit sigHostScreenWorkAreaResized(iHostScreenIndex);
}

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
void UIDesktopWidgetWatchdog::sltHandleHostScreenAvailableGeometryCalculated(int iHostScreenIndex, QRect availableGeometry)
{
    LogRel(("GUI: UIDesktopWidgetWatchdog::sltHandleHostScreenAvailableGeometryCalculated: "
            "Screen %d work area is actually resized to: %dx%d x %dx%d\n",
            iHostScreenIndex, availableGeometry.x(), availableGeometry.y(),
            availableGeometry.width(), availableGeometry.height()));

    /* Apply received data: */
    const bool fSendSignal = m_availableGeometryData.value(iHostScreenIndex).isValid();
    m_availableGeometryData[iHostScreenIndex] = availableGeometry;
    /* Forget finished worker: */
    AssertPtrReturnVoid(m_availableGeometryWorkers.value(iHostScreenIndex));
    m_availableGeometryWorkers.value(iHostScreenIndex)->disconnect();
    m_availableGeometryWorkers.value(iHostScreenIndex)->deleteLater();
    m_availableGeometryWorkers[iHostScreenIndex] = 0;

    /* Notify listeners: */
    if (fSendSignal)
        emit sigHostScreenWorkAreaRecalculated(iHostScreenIndex);
}
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

void UIDesktopWidgetWatchdog::prepare()
{
    /* Prepare connections: */
    connect(qApp, &QGuiApplication::screenAdded,
            this, &UIDesktopWidgetWatchdog::sltHostScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved,
            this, &UIDesktopWidgetWatchdog::sltHostScreenRemoved);
    foreach (QScreen *pHostScreen, qApp->screens())
    {
        connect(pHostScreen, &QScreen::geometryChanged,
                this, &UIDesktopWidgetWatchdog::sltHandleHostScreenResized);
        connect(pHostScreen, &QScreen::availableGeometryChanged,
                this, &UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized);
    }

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Load Synthetic Test policy: */
    const QString strSynthTestPolicy =
        QString::fromLocal8Bit(qgetenv(s_strVBoxDesktopWatchdogPolicySynthTest.toLatin1().constData()));
    m_enmSynthTestPolicy = gpConverter->fromInternalString<DesktopWatchdogPolicy_SynthTest>(strSynthTestPolicy);

    /* Update host-screen configuration: */
    updateHostScreenConfiguration();
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
}

void UIDesktopWidgetWatchdog::cleanup()
{
    /* Cleanup connections: */
    disconnect(qApp, &QGuiApplication::screenAdded,
               this, &UIDesktopWidgetWatchdog::sltHostScreenAdded);
    disconnect(qApp, &QGuiApplication::screenRemoved,
               this, &UIDesktopWidgetWatchdog::sltHostScreenRemoved);
    foreach (QScreen *pHostScreen, qApp->screens())
    {
        disconnect(pHostScreen, &QScreen::geometryChanged,
                   this, &UIDesktopWidgetWatchdog::sltHandleHostScreenResized);
        disconnect(pHostScreen, &QScreen::availableGeometryChanged,
                   this, &UIDesktopWidgetWatchdog::sltHandleHostScreenWorkAreaResized);
    }

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Cleanup existing workers finally: */
    cleanupExistingWorkers();
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
}

/* static */
int UIDesktopWidgetWatchdog::screenToIndex(QScreen *pScreen)
{
    if (pScreen)
    {
        unsigned iScreen = 0;
        foreach (QScreen *pCurScreen, QGuiApplication::screens())
        {
            if (   pCurScreen == pScreen
                || (   pCurScreen->geometry() == pScreen->geometry()
                    && pCurScreen->serialNumber() == pScreen->serialNumber()))
                return iScreen;
            ++iScreen;
        }
    }
    return -1;
}

/* static */
QRegion UIDesktopWidgetWatchdog::flip(const QRegion &region)
{
    QRegion result;
    for (QRegion::const_iterator it = region.begin(); it != region.end(); ++it)
        result += QRect(it->y(),      it->x(),
                        it->height(), it->width());
    return result;
}

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
bool UIDesktopWidgetWatchdog::isSynchTestRestricted() const
{
    return    m_enmSynthTestPolicy == DesktopWatchdogPolicy_SynthTest_Disabled
           || (   m_enmSynthTestPolicy == DesktopWatchdogPolicy_SynthTest_ManagerOnly
               && uiCommon().uiType() == UIType_RuntimeUI)
           || (   m_enmSynthTestPolicy == DesktopWatchdogPolicy_SynthTest_MachineOnly
               && uiCommon().uiType() == UIType_ManagerUI);
}

void UIDesktopWidgetWatchdog::updateHostScreenConfiguration(int cHostScreenCount /* = -1 */)
{
    /* Check the policy: */
    if (isSynchTestRestricted())
        return;

    /* Acquire new host-screen count: */
    if (cHostScreenCount == -1)
        cHostScreenCount = screenCount();

    /* Cleanup existing workers first: */
    cleanupExistingWorkers();

    /* Resize workers vectors to new host-screen count: */
    m_availableGeometryWorkers.resize(cHostScreenCount);
    m_availableGeometryData.resize(cHostScreenCount);

    /* Update host-screen available-geometry for each particular host-screen: */
    for (int iHostScreenIndex = 0; iHostScreenIndex < cHostScreenCount; ++iHostScreenIndex)
        updateHostScreenAvailableGeometry(iHostScreenIndex);
}

void UIDesktopWidgetWatchdog::updateHostScreenAvailableGeometry(int iHostScreenIndex)
{
    /* Check the policy: */
    if (isSynchTestRestricted())
        return;

    /* Make sure index is valid: */
    if (iHostScreenIndex < 0 || iHostScreenIndex >= screenCount())
    {
        iHostScreenIndex = UIDesktopWidgetWatchdog::primaryScreenNumber();
        AssertReturnVoid(iHostScreenIndex >= 0 && iHostScreenIndex < screenCount());
    }

    /* Create invisible frame-less window worker: */
    UIInvisibleWindow *pWorker = new UIInvisibleWindow(iHostScreenIndex);
    AssertPtrReturnVoid(pWorker);
    {
        /* Remember created worker (replace if necessary): */
        if (m_availableGeometryWorkers.value(iHostScreenIndex))
            delete m_availableGeometryWorkers.value(iHostScreenIndex);
        m_availableGeometryWorkers[iHostScreenIndex] = pWorker;

        /* Get the screen-geometry: */
        const QRect hostScreenGeometry = screenGeometry(iHostScreenIndex);

        /* Connect worker listener: */
        connect(pWorker, &UIInvisibleWindow::sigHostScreenAvailableGeometryCalculated,
                this, &UIDesktopWidgetWatchdog::sltHandleHostScreenAvailableGeometryCalculated);

        /* Place worker to corresponding host-screen: */
        pWorker->move(hostScreenGeometry.center());
        /* And finally, maximize it: */
        pWorker->showMaximized();
    }
}

void UIDesktopWidgetWatchdog::cleanupExistingWorkers()
{
    /* Check the policy: */
    if (isSynchTestRestricted())
        return;

    /* Destroy existing workers: */
    qDeleteAll(m_availableGeometryWorkers);
    /* And clear their vector: */
    m_availableGeometryWorkers.clear();
}

# include "UIDesktopWidgetWatchdog.moc"
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */
