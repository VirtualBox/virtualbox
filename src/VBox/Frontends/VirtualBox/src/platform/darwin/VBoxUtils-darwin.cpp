/* $Id: VBoxUtils-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Utility Classes and Functions specific to Darwin.
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

/* Qt includes: */
#include <QMainWindow>
#include <QApplication>
#include <QWidget>
#include <QToolBar>
#include <QPainter>
#include <QPixmap>
#include <QContextMenuEvent>

/* GUI includes: */
#include "VBoxUtils-darwin.h"
#include "VBoxCocoaHelper.h"
#include "UICocoaApplication.h"

/* Other VBox includes: */
#include <iprt/mem.h>
#include <iprt/assert.h>

/* System includes: */
#include <Carbon/Carbon.h>

NativeNSViewRef darwinToNativeView(QWidget *pWidget)
{
    if (pWidget)
        return reinterpret_cast<NativeNSViewRef>(pWidget->winId());
    return nil;
}

NativeNSWindowRef darwinToNativeWindow(QWidget *pWidget)
{
    if (pWidget)
        return ::darwinToNativeWindowImpl(::darwinToNativeView(pWidget));
    return nil;
}

NativeNSWindowRef darwinToNativeWindow(NativeNSViewRef aView)
{
    return ::darwinToNativeWindowImpl(aView);
}

NativeNSViewRef darwinToNativeView(NativeNSWindowRef aWindow)
{
    return ::darwinToNativeViewImpl(aWindow);
}

NativeNSWindowRef darwinNativeButtonOfWindow(QWidget *pWidget, StandardWindowButtonType enmButtonType)
{
    return ::darwinNativeButtonOfWindowImpl(::darwinToNativeWindow(pWidget), enmButtonType);
}

void darwinSetShowsToolbarButton(QToolBar *aToolBar, bool fEnabled)
{
    QWidget *parent = aToolBar->parentWidget();
    if (parent)
        ::darwinSetShowsToolbarButtonImpl(::darwinToNativeWindow(parent), fEnabled);
}

void darwinLabelWindow(QWidget *pWidget, QPixmap *pPixmap)
{
    ::darwinLabelWindow(::darwinToNativeWindow(pWidget), ::darwinToNSImageRef(pPixmap), pPixmap->devicePixelRatio());
}

void darwinSetHidesAllTitleButtons(QWidget *pWidget)
{
    /* Currently only necessary in the Cocoa version */
    ::darwinSetHidesAllTitleButtonsImpl(::darwinToNativeWindow(pWidget));
}

void darwinSetShowsWindowTransparent(QWidget *pWidget, bool fEnabled)
{
    ::darwinSetShowsWindowTransparentImpl(::darwinToNativeWindow(pWidget), fEnabled);
}

void darwinSetWindowHasShadow(QWidget *pWidget, bool fEnabled)
{
    ::darwinSetWindowHasShadow(::darwinToNativeWindow(pWidget), fEnabled);
}

void darwinWindowAnimateResize(QWidget *pWidget, const QRect &aTarget)
{
    ::darwinWindowAnimateResizeImpl(::darwinToNativeWindow(pWidget), aTarget.x(), aTarget.y(), aTarget.width(), aTarget.height());
}

void darwinWindowAnimateResizeNew(QWidget *pWidget, int h, bool fAnimate)
{
    ::darwinWindowAnimateResizeNewImpl(::darwinToNativeWindow(pWidget), h, fAnimate);
}

void darwinTest(QWidget *pWidget1, QWidget *pWidget2, int h)
{
    ::darwinTest(::darwinToNativeView(pWidget1), ::darwinToNativeView(pWidget2), h);
}

void darwinWindowInvalidateShape(QWidget *pWidget)
{
    /* Here a simple update is enough! */
    pWidget->update();
}

void darwinWindowInvalidateShadow(QWidget *pWidget)
{
    ::darwinWindowInvalidateShadowImpl(::darwinToNativeWindow(pWidget));
}

void darwinSetShowsResizeIndicator(QWidget *pWidget, bool fEnabled)
{
    ::darwinSetShowsResizeIndicatorImpl(::darwinToNativeWindow(pWidget), fEnabled);
}

bool darwinIsWindowMaximized(QWidget *pWidget)
{
    /* Currently only necessary in the Cocoa version */
    return ::darwinIsWindowMaximized(::darwinToNativeWindow(pWidget));
}

void darwinMinaturizeWindow(QWidget *pWidget)
{
    return ::darwinMinaturizeWindow(::darwinToNativeWindow(pWidget));
}

void darwinEnableFullscreenSupport(QWidget *pWidget)
{
    return ::darwinEnableFullscreenSupport(::darwinToNativeWindow(pWidget));
}

void darwinEnableTransienceSupport(QWidget *pWidget)
{
    return ::darwinEnableTransienceSupport(::darwinToNativeWindow(pWidget));
}

void darwinToggleFullscreenMode(QWidget *pWidget)
{
    return ::darwinToggleFullscreenMode(::darwinToNativeWindow(pWidget));
}

void darwinToggleWindowZoom(QWidget *pWidget)
{
    return ::darwinToggleWindowZoom(::darwinToNativeWindow(pWidget));
}

bool darwinIsInFullscreenMode(QWidget *pWidget)
{
    return ::darwinIsInFullscreenMode(::darwinToNativeWindow(pWidget));
}

bool darwinIsOnActiveSpace(QWidget *pWidget)
{
    return ::darwinIsOnActiveSpace(::darwinToNativeWindow(pWidget));
}

void darwinInstallResizeDelegate(QWidget *pWidget)
{
    ::darwinInstallResizeDelegate(::darwinToNativeWindow(pWidget));
}

void darwinUninstallResizeDelegate(QWidget *pWidget)
{
    ::darwinUninstallResizeDelegate(::darwinToNativeWindow(pWidget));
}

bool darwinOpenFile(const QString& strFile)
{
    return ::darwinOpenFile(darwinToNativeString(strFile.toUtf8().constData()));
}

QString darwinSystemLanguage(void)
{
    /* Get the locales supported by our bundle */
    CFArrayRef supportedLocales = ::CFBundleCopyBundleLocalizations(::CFBundleGetMainBundle());
    /* Check them against the languages currently selected by the user */
    CFArrayRef preferredLocales = ::CFBundleCopyPreferredLocalizationsFromArray(supportedLocales);
    /* Get the one which is on top */
    CFStringRef localeId = (CFStringRef)::CFArrayGetValueAtIndex(preferredLocales, 0);
    /* Convert them to a C-string */
    char localeName[20];
    ::CFStringGetCString(localeId, localeName, sizeof(localeName), kCFStringEncodingUTF8);
    /* Some cleanup */
    ::CFRelease(supportedLocales);
    ::CFRelease(preferredLocales);
    QString id(localeName);
    /* Check for some misbehavior */
    if (id.isEmpty() ||
        id.toLower() == "english")
        id = "en";
    return id;
}

void darwinDisableIconsInMenus(void)
{
    /* No icons in the menu of a mac application. */
    QApplication::instance()->setAttribute(Qt::AA_DontShowIconsInMenus, true);
}

int darwinWindowToolBarHeight(QWidget *pWidget)
{
    NOREF(pWidget);
    return 0;
}

int darwinWindowTitleHeight(QWidget *pWidget)
{
    return ::darwinWindowTitleHeight(::darwinToNativeWindow(pWidget));
}

bool darwinIsToolbarVisible(QToolBar *pToolBar)
{
    bool fResult = false;
    QWidget *pParent = pToolBar->parentWidget();
    if (pParent)
        fResult = ::darwinIsToolbarVisible(::darwinToNativeWindow(pParent));
    return fResult;
}


bool darwinSetFrontMostProcess()
{
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    return ::SetFrontProcess(&psn) == 0;
}

uint64_t darwinGetCurrentProcessId()
{
    uint64_t processId = 0;
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    if (::GetCurrentProcess(&psn) == 0)
        processId = RT_MAKE_U64(psn.lowLongOfPSN, psn.highLongOfPSN);
    return processId;
}

/* Proxy icon creation */
QPixmap darwinCreateDragPixmap(const QPixmap& aPixmap, const QString &aText)
{
    QFontMetrics fm(qApp->font());
    QRect tbRect = fm.boundingRect(aText);
    const int h = qMax(aPixmap.height(), fm.ascent() + 1);
    const int m = 2;
    QPixmap dragPixmap(aPixmap.width() + tbRect.width() + m, h);
    dragPixmap.fill(Qt::transparent);
    QPainter painter(&dragPixmap);
    painter.drawPixmap(0, qAbs(h - aPixmap.height()) / 2.0, aPixmap);
    painter.setPen(Qt::white);
    painter.drawText(QRect(aPixmap.width() + m, 1, tbRect.width(), h - 1), Qt::AlignLeft | Qt::AlignVCenter, aText);
    painter.setPen(Qt::black);
    painter.drawText(QRect(aPixmap.width() + m, 0, tbRect.width(), h - 1), Qt::AlignLeft | Qt::AlignVCenter, aText);
    painter.end();
    return dragPixmap;
}

/**
 * Callback for deleting the QImage object when CGImageCreate is done
 * with it (which is probably not until the returned CFGImageRef is released).
 *
 * @param   info        Pointer to the QImage.
 */
static void darwinDataProviderReleaseQImage(void *info, const void *, size_t)
{
    QImage *qimg = (QImage *)info;
    delete qimg;
}

/**
 * Converts a QPixmap to a CGImage.
 *
 * @returns CGImageRef for the new image. (Remember to release it when finished with it.)
 * @param   aPixmap     Pointer to the QPixmap instance to convert.
 */
CGImageRef darwinToCGImageRef(const QImage *pImage)
{
    QImage *imageCopy = new QImage(*pImage);
    /** @todo this code assumes 32-bit image input, the lazy bird convert image to 32-bit method is anything but optimal... */
    if (imageCopy->format() != QImage::Format_ARGB32)
        *imageCopy = imageCopy->convertToFormat(QImage::Format_ARGB32);
    Assert(!imageCopy->isNull());

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(imageCopy, pImage->bits(), pImage->sizeInBytes(),
                                                        darwinDataProviderReleaseQImage);

    CGBitmapInfo bmpInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Host;
    CGImageRef ir = CGImageCreate(imageCopy->width(), imageCopy->height(), 8, 32, imageCopy->bytesPerLine(), cs,
                                   bmpInfo, dp, 0 /*decode */, 0 /* shouldInterpolate */,
                                   kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(dp);

    Assert(ir);
    return ir;
}

/**
 * Converts a QPixmap to a CGImage.
 *
 * @returns CGImageRef for the new image. (Remember to release it when finished with it.)
 * @param   aPixmap     Pointer to the QPixmap instance to convert.
 */
CGImageRef darwinToCGImageRef(const QPixmap *pPixmap)
{
    /* It seems Qt releases the memory to an returned CGImageRef when the
     * associated QPixmap is destroyed. This shouldn't happen as long a
     * CGImageRef has a retrain count. As a workaround we make a real copy. */
    int bitmapBytesPerRow = pPixmap->width() * 4;
    int bitmapByteCount = (bitmapBytesPerRow * pPixmap->height());
    /* Create a memory block for the temporary image. It is initialized by zero
     * which means black & zero alpha. */
    void *pBitmapData = RTMemAllocZ(bitmapByteCount);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    /* Create a context to paint on */
    CGContextRef ctx = CGBitmapContextCreate(pBitmapData,
                                              pPixmap->width(),
                                              pPixmap->height(),
                                              8,
                                              bitmapBytesPerRow,
                                              cs,
                                              kCGImageAlphaPremultipliedFirst);
    /* Get the CGImageRef from Qt */
    CGImageRef qtPixmap = pPixmap->toImage().toCGImage();
    /* Draw the image from Qt & convert the context back to a new CGImageRef. */
    CGContextDrawImage(ctx, CGRectMake(0, 0, pPixmap->width(), pPixmap->height()), qtPixmap);
    CGImageRef newImage = CGBitmapContextCreateImage(ctx);
    /* Now release all used resources */
    CGImageRelease(qtPixmap);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    RTMemFree(pBitmapData);

    /* Return the new CGImageRef */
    return newImage;
}

/**
 * Loads an image using Qt and converts it to a CGImage.
 *
 * @returns CGImageRef for the new image. (Remember to release it when finished with it.)
 * @param   aSource     The source name.
 */
CGImageRef darwinToCGImageRef(const char *pczSource)
{
    QPixmap qpm(QString(":/") + pczSource);
    Assert(!qpm.isNull());
    return ::darwinToCGImageRef(&qpm);
}

void darwinRegisterForUnifiedToolbarContextMenuEvents(QMainWindow *pWindow)
{
    UICocoaApplication::instance()->registerForNativeEvents(RT_BIT_32(3) /* NSRightMouseDown */, ::darwinUnifiedToolbarEvents, pWindow);
}

void darwinUnregisterForUnifiedToolbarContextMenuEvents(QMainWindow *pWindow)
{
    UICocoaApplication::instance()->unregisterForNativeEvents(RT_BIT_32(3) /* NSRightMouseDown */, ::darwinUnifiedToolbarEvents, pWindow);
}

void darwinMouseGrab(QWidget *pWidget)
{
    CGAssociateMouseAndMouseCursorPosition(false);
    UICocoaApplication::instance()->registerForNativeEvents(RT_BIT_32(1)  | /* NSLeftMouseDown */
                                                            RT_BIT_32(2)  | /* NSLeftMouseUp */
                                                            RT_BIT_32(3)  | /* NSRightMouseDown */
                                                            RT_BIT_32(4)  | /* NSRightMouseUp */
                                                            RT_BIT_32(5)  | /* NSMouseMoved */
                                                            RT_BIT_32(6)  | /* NSLeftMouseDragged */
                                                            RT_BIT_32(7)  | /* NSRightMouseDragged */
                                                            RT_BIT_32(25) | /* NSOtherMouseDown */
                                                            RT_BIT_32(26) | /* NSOtherMouseUp */
                                                            RT_BIT_32(27) | /* NSOtherMouseDragged */
                                                            RT_BIT_32(22),  /* NSScrollWheel */
                                                            ::darwinMouseGrabEvents, pWidget);
}

void darwinMouseRelease(QWidget *pWidget)
{
    UICocoaApplication::instance()->unregisterForNativeEvents(RT_BIT_32(1)  | /* NSLeftMouseDown */
                                                              RT_BIT_32(2)  | /* NSLeftMouseUp */
                                                              RT_BIT_32(3)  | /* NSRightMouseDown */
                                                              RT_BIT_32(4)  | /* NSRightMouseUp */
                                                              RT_BIT_32(5)  | /* NSMouseMoved */
                                                              RT_BIT_32(6)  | /* NSLeftMouseDragged */
                                                              RT_BIT_32(7)  | /* NSRightMouseDragged */
                                                              RT_BIT_32(25) | /* NSOtherMouseDown */
                                                              RT_BIT_32(26) | /* NSOtherMouseUp */
                                                              RT_BIT_32(27) | /* NSOtherMouseDragged */
                                                              RT_BIT_32(22),  /* NSScrollWheel */
                                                              ::darwinMouseGrabEvents, pWidget);
    CGAssociateMouseAndMouseCursorPosition(true);
}

void darwinSendMouseGrabEvents(QWidget *pWidget, int type, int button, int buttons, int x, int y)
{
    QEvent::Type qtType = QEvent::None;
    Qt::MouseButtons qtButtons = Qt::NoButton;
    Qt::MouseButton qtButton = Qt::NoButton;
    Qt::MouseButton qtExtraButton = Qt::NoButton;
    Qt::Orientation qtOrientation = Qt::Horizontal;
    int wheelDelta = 0;
    /* Which button is used in the NSOtherMouse... cases? */
    if (button == 0)
        qtExtraButton = Qt::LeftButton;
    else if (button == 1)
        qtExtraButton = Qt::RightButton;
    else if (button == 2)
        qtExtraButton = Qt::MiddleButton;
    else if (button == 3)
        qtExtraButton = Qt::XButton1;
    else if (button == 4)
        qtExtraButton = Qt::XButton2;
    /* Map the NSEvent to a QEvent and define the Qt::Buttons when necessary. */
    switch(type)
    {
        case 1: /* NSLeftMouseDown */
        {
            qtType = QEvent::MouseButtonPress;
            qtButton = Qt::LeftButton;
            break;
        }
        case 2: /* NSLeftMouseUp */
        {
            qtType = QEvent::MouseButtonRelease;
            qtButton = Qt::LeftButton;
            break;
        }
        case 3: /* NSRightMouseDown */
        {
            qtType = QEvent::MouseButtonPress;
            qtButton = Qt::RightButton;
            break;
        }
        case 4: /* NSRightMouseUp */
        {
            qtType = QEvent::MouseButtonRelease;
            qtButton = Qt::RightButton;
            break;
        }
        case 5: /* NSMouseMoved */
        {
            qtType = QEvent::MouseMove;
            break;
        }
        case 6: /* NSLeftMouseDragged */
        {
            qtType = QEvent::MouseMove;
            qtButton = Qt::LeftButton;
            break;
        }
        case 7: /* NSRightMouseDragged */
        {
            qtType = QEvent::MouseMove;
            qtButton = Qt::RightButton;
            break;
        }
        case 22: /* NSScrollWheel */
        {
            qtType = QEvent::Wheel;
            if (y != 0)
            {
                wheelDelta = y;
                qtOrientation = Qt::Vertical;
            }
            else if (x != 0)
            {
                wheelDelta = x;
                qtOrientation = Qt::Horizontal;
            }
            x = y = 0;
            break;
        }
        case 25: /* NSOtherMouseDown */
        {
            qtType = QEvent::MouseButtonPress;
            qtButton = qtExtraButton;
            break;
        }
        case 26: /* NSOtherMouseUp */
        {
            qtType = QEvent::MouseButtonRelease;
            qtButton = qtExtraButton;
            break;
        }
        case 27: /* NSOtherMouseDragged */
        {
            qtType = QEvent::MouseMove;
            qtButton = qtExtraButton;
            break;
        }
        default: return;
    }
    /* Create a Qt::MouseButtons Mask. */
    if ((buttons & RT_BIT_32(0)) == RT_BIT_32(0))
        qtButtons |= Qt::LeftButton;
    if ((buttons & RT_BIT_32(1)) == RT_BIT_32(1))
        qtButtons |= Qt::RightButton;
    if ((buttons & RT_BIT_32(2)) == RT_BIT_32(2))
        qtButtons |= Qt::MiddleButton;
    if ((buttons & RT_BIT_32(3)) == RT_BIT_32(3))
        qtButtons |= Qt::XButton1;
    if ((buttons & RT_BIT_32(4)) == RT_BIT_32(4))
        qtButtons |= Qt::XButton2;
    /* Create a new mouse delta event and send it to the widget. */
    UIGrabMouseEvent *pEvent = new UIGrabMouseEvent(qtType, qtButton, qtButtons, x, y, wheelDelta, qtOrientation);
    qApp->sendEvent(pWidget, pEvent);
}

void darwinCreateContextMenuEvent(void *pvUser, int x, int y)
{
    QWidget *pWin = static_cast<QWidget*>(pvUser);
    QPoint global(x, y);
    QPoint local = pWin->mapFromGlobal(global);
    qApp->postEvent(pWin, new QContextMenuEvent(QContextMenuEvent::Mouse, local, global));
}

QString darwinResolveAlias(const QString &strFile)
{
    OSErr err = noErr;
    FSRef fileRef;
    QString strTarget;
    do
    {
        Boolean fDir;
        if ((err = FSPathMakeRef((const UInt8*)strFile.toUtf8().constData(), &fileRef, &fDir)) != noErr)
            break;
        Boolean fAlias = FALSE;
        if ((err = FSIsAliasFile(&fileRef, &fAlias, &fDir)) != noErr)
            break;
        if (fAlias == TRUE)
        {
            if ((err = FSResolveAliasFile(&fileRef, TRUE, &fAlias, &fDir)) != noErr)
                break;
            char pszPath[1024];
            if ((err = FSRefMakePath(&fileRef, (UInt8*)pszPath, 1024)) != noErr)
                break;
            strTarget = QString::fromUtf8(pszPath);
        }
        else
            strTarget = strFile;
    }while(0);

    return strTarget;
}


/********************************************************************************
 *
 * Old carbon stuff. Have to convert soon!
 *
 ********************************************************************************/

/* Event debugging stuff. Borrowed from Knuts Qt patch. */
#if defined (DEBUG)

# define MY_CASE(a) case a: return #a
const char * DarwinDebugEventName(UInt32 ekind)
{
    switch (ekind)
    {
# if !__LP64__
        MY_CASE(kEventWindowUpdate                );
        MY_CASE(kEventWindowDrawContent           );
# endif
        MY_CASE(kEventWindowActivated             );
        MY_CASE(kEventWindowDeactivated           );
        MY_CASE(kEventWindowHandleActivate        );
        MY_CASE(kEventWindowHandleDeactivate      );
        MY_CASE(kEventWindowGetClickActivation    );
        MY_CASE(kEventWindowGetClickModality      );
        MY_CASE(kEventWindowShowing               );
        MY_CASE(kEventWindowHiding                );
        MY_CASE(kEventWindowShown                 );
        MY_CASE(kEventWindowHidden                );
        MY_CASE(kEventWindowCollapsing            );
        MY_CASE(kEventWindowCollapsed             );
        MY_CASE(kEventWindowExpanding             );
        MY_CASE(kEventWindowExpanded              );
        MY_CASE(kEventWindowZoomed                );
        MY_CASE(kEventWindowBoundsChanging        );
        MY_CASE(kEventWindowBoundsChanged         );
        MY_CASE(kEventWindowResizeStarted         );
        MY_CASE(kEventWindowResizeCompleted       );
        MY_CASE(kEventWindowDragStarted           );
        MY_CASE(kEventWindowDragCompleted         );
        MY_CASE(kEventWindowClosed                );
        MY_CASE(kEventWindowTransitionStarted     );
        MY_CASE(kEventWindowTransitionCompleted   );
# if !__LP64__
        MY_CASE(kEventWindowClickDragRgn          );
        MY_CASE(kEventWindowClickResizeRgn        );
        MY_CASE(kEventWindowClickCollapseRgn      );
        MY_CASE(kEventWindowClickCloseRgn         );
        MY_CASE(kEventWindowClickZoomRgn          );
        MY_CASE(kEventWindowClickContentRgn       );
        MY_CASE(kEventWindowClickProxyIconRgn     );
        MY_CASE(kEventWindowClickToolbarButtonRgn );
        MY_CASE(kEventWindowClickStructureRgn     );
# endif
        MY_CASE(kEventWindowCursorChange          );
        MY_CASE(kEventWindowCollapse              );
        MY_CASE(kEventWindowCollapseAll           );
        MY_CASE(kEventWindowExpand                );
        MY_CASE(kEventWindowExpandAll             );
        MY_CASE(kEventWindowClose                 );
        MY_CASE(kEventWindowCloseAll              );
        MY_CASE(kEventWindowZoom                  );
        MY_CASE(kEventWindowZoomAll               );
        MY_CASE(kEventWindowContextualMenuSelect  );
        MY_CASE(kEventWindowPathSelect            );
        MY_CASE(kEventWindowGetIdealSize          );
        MY_CASE(kEventWindowGetMinimumSize        );
        MY_CASE(kEventWindowGetMaximumSize        );
        MY_CASE(kEventWindowConstrain             );
# if !__LP64__
        MY_CASE(kEventWindowHandleContentClick    );
# endif
        MY_CASE(kEventWindowGetDockTileMenu       );
        MY_CASE(kEventWindowProxyBeginDrag        );
        MY_CASE(kEventWindowProxyEndDrag          );
        MY_CASE(kEventWindowToolbarSwitchMode     );
        MY_CASE(kEventWindowFocusAcquired         );
        MY_CASE(kEventWindowFocusRelinquish       );
        MY_CASE(kEventWindowFocusContent          );
        MY_CASE(kEventWindowFocusToolbar          );
        MY_CASE(kEventWindowFocusDrawer           );
        MY_CASE(kEventWindowSheetOpening          );
        MY_CASE(kEventWindowSheetOpened           );
        MY_CASE(kEventWindowSheetClosing          );
        MY_CASE(kEventWindowSheetClosed           );
        MY_CASE(kEventWindowDrawerOpening         );
        MY_CASE(kEventWindowDrawerOpened          );
        MY_CASE(kEventWindowDrawerClosing         );
        MY_CASE(kEventWindowDrawerClosed          );
        MY_CASE(kEventWindowDrawFrame             );
        MY_CASE(kEventWindowDrawPart              );
        MY_CASE(kEventWindowGetRegion             );
        MY_CASE(kEventWindowHitTest               );
        MY_CASE(kEventWindowInit                  );
        MY_CASE(kEventWindowDispose               );
        MY_CASE(kEventWindowDragHilite            );
        MY_CASE(kEventWindowModified              );
        MY_CASE(kEventWindowSetupProxyDragImage   );
        MY_CASE(kEventWindowStateChanged          );
        MY_CASE(kEventWindowMeasureTitle          );
        MY_CASE(kEventWindowDrawGrowBox           );
        MY_CASE(kEventWindowGetGrowImageRegion    );
        MY_CASE(kEventWindowPaint                 );
    }
    static char s_sz[64];
    sprintf(s_sz, "kind=%u", (uint)ekind);
    return s_sz;
}
# undef MY_CASE

/* Convert a class into the 4 char code defined in
 * 'Developer/Headers/CFMCarbon/CarbonEvents.h' to
 * identify the event. */
const char * darwinDebugClassName(UInt32 eclass)
{
    char *pclass = (char*)&eclass;
    static char s_sz[11];
    sprintf(s_sz, "class=%c%c%c%c", pclass[3],
                                    pclass[2],
                                    pclass[1],
                                    pclass[0]);
    return s_sz;
}

void darwinDebugPrintEvent(const char *psz, EventRef evtRef)
{
  if (!evtRef)
      return;
  UInt32 ekind = GetEventKind(evtRef), eclass = GetEventClass(evtRef);
  if (eclass == kEventClassWindow)
  {
      switch (ekind)
      {
# if !__LP64__
          case kEventWindowDrawContent:
          case kEventWindowUpdate:
# endif
          case kEventWindowBoundsChanged:
              break;
          default:
          {
              WindowRef wid = NULL;
              GetEventParameter(evtRef, kEventParamDirectObject, typeWindowRef, NULL, sizeof(WindowRef), NULL, &wid);
              QWidget *widget = QWidget::find((WId)wid);
              printf("%d %s: (%s) %#x win=%p wid=%p (%s)\n", (int)time(NULL), psz, darwinDebugClassName(eclass), (uint)ekind, wid, widget, DarwinDebugEventName(ekind));
              break;
          }
      }
  }
  else if (eclass == kEventClassCommand)
  {
      WindowRef wid = NULL;
      GetEventParameter(evtRef, kEventParamDirectObject, typeWindowRef, NULL, sizeof(WindowRef), NULL, &wid);
      QWidget *widget = QWidget::find((WId)wid);
      const char *name = "Unknown";
      switch (ekind)
      {
          case kEventCommandProcess:
              name = "kEventCommandProcess";
              break;
          case kEventCommandUpdateStatus:
              name = "kEventCommandUpdateStatus";
              break;
      }
      printf("%d %s: (%s) %#x win=%p wid=%p (%s)\n", (int)time(NULL), psz, darwinDebugClassName(eclass), (uint)ekind, wid, widget, name);
  }
  else if (eclass == kEventClassKeyboard)
  {
      printf("%d %s: %#x(%s) %#x (kEventClassKeyboard)", (int)time(NULL), psz, (uint)eclass, darwinDebugClassName(eclass), (uint)ekind);

      UInt32 keyCode = 0;
      ::GetEventParameter(evtRef, kEventParamKeyCode, typeUInt32, NULL,
                           sizeof(keyCode), NULL, &keyCode);
      printf(" keyCode=%d (%#x) ", (int)keyCode, (unsigned)keyCode);

      char macCharCodes[8] = {0,0,0,0, 0,0,0,0};
      ::GetEventParameter(evtRef, kEventParamKeyCode, typeChar, NULL,
                           sizeof(macCharCodes), NULL, &macCharCodes[0]);
      printf(" macCharCodes={");
      for (unsigned i =0; i < 8 && macCharCodes[i]; i++)
          printf( i == 0 ? "%02x" : ",%02x", macCharCodes[i]);
      printf("}");

      UInt32 modifierMask = 0;
      ::GetEventParameter(evtRef, kEventParamKeyModifiers, typeUInt32, NULL,
                           sizeof(modifierMask), NULL, &modifierMask);
      printf(" modifierMask=%08x", (unsigned)modifierMask);

      UniChar keyUnicodes[8] = {0,0,0,0, 0,0,0,0};
      ::GetEventParameter(evtRef, kEventParamKeyUnicodes, typeUnicodeText, NULL,
                           sizeof(keyUnicodes), NULL, &keyUnicodes[0]);
      printf(" keyUnicodes={");
      for (unsigned i =0; i < 8 && keyUnicodes[i]; i++)
          printf( i == 0 ? "%02x" : ",%02x", keyUnicodes[i]);
      printf("}");

      UInt32 keyboardType = 0;
      ::GetEventParameter(evtRef, kEventParamKeyboardType, typeUInt32, NULL,
                           sizeof(keyboardType), NULL, &keyboardType);
      printf(" keyboardType=%08x", (unsigned)keyboardType);

      EventHotKeyID evtHotKeyId = {0,0};
      ::GetEventParameter(evtRef, typeEventHotKeyID, typeEventHotKeyID, NULL,
                           sizeof(evtHotKeyId), NULL, &evtHotKeyId);
      printf(" evtHotKeyId={signature=%08x, .id=%08x}", (unsigned)evtHotKeyId.signature, (unsigned)evtHotKeyId.id);
      printf("\n");
  }
  else
      printf("%d %s: %#x(%s) %#x\n", (int)time(NULL), psz, (uint)eclass, darwinDebugClassName(eclass), (uint)ekind);
}

#endif /* DEBUG */
