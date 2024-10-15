/* $Id: VBoxUtils-darwin-cocoa.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI -  Declarations of utility classes and functions for handling Darwin Cocoa specific tasks.
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

#include "VBoxUtils-darwin.h"
#include "VBoxCocoaHelper.h"

#include <QMenu>

#import <AppKit/NSEvent.h>
#import <AppKit/NSColor.h>
#import <AppKit/NSFont.h>
#import <AppKit/NSScreen.h>
#import <AppKit/NSScroller.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSImageView.h>

#import <objc/objc-class.h>

/* For the keyboard stuff */
#include <Carbon/Carbon.h>
#include "DarwinKeyboard.h"

/** Easy way of dynamical call for 10.7 AppKit functionality we do not yet support. */
#define NSWindowCollectionBehaviorFullScreenPrimary (1 << 7)
#define NSFullScreenWindowMask (1 << 14)

NativeNSWindowRef darwinToNativeWindowImpl(NativeNSViewRef pView)
{
    NativeNSWindowRef window = NULL;
    if (pView)
        window = [pView window];

    return window;
}

NativeNSViewRef darwinToNativeViewImpl(NativeNSWindowRef pWindow)
{
    NativeNSViewRef view = NULL;
    if (pWindow)
        view = [pWindow contentView];

    return view;
}

NativeNSButtonRef darwinNativeButtonOfWindowImpl(NativeNSWindowRef pWindow, StandardWindowButtonType enmButtonType)
{
    /* Return corresponding button: */
    switch (enmButtonType)
    {
        case StandardWindowButtonType_Close:            return [pWindow standardWindowButton:NSWindowCloseButton];
        case StandardWindowButtonType_Miniaturize:      return [pWindow standardWindowButton:NSWindowMiniaturizeButton];
        case StandardWindowButtonType_Zoom:             return [pWindow standardWindowButton:NSWindowZoomButton];
        case StandardWindowButtonType_Toolbar:          return [pWindow standardWindowButton:NSWindowToolbarButton];
        case StandardWindowButtonType_DocumentIcon:     return [pWindow standardWindowButton:NSWindowDocumentIconButton];
        case StandardWindowButtonType_DocumentVersions: /*return [pWindow standardWindowButton:NSWindowDocumentVersionsButton];*/ break;
        case StandardWindowButtonType_FullScreen:       /*return [pWindow standardWindowButton:NSWindowFullScreenButton];*/ break;
    }
    /* Return Nul by default: */
    return Nil;
}

NativeNSImageRef darwinToNSImageRef(const CGImageRef pImage)
{
    /* Create a bitmap rep from the image. */
    NSBitmapImageRep *bitmapRep = [[[NSBitmapImageRep alloc] initWithCGImage:pImage] autorelease];
    /* Create an NSImage and add the bitmap rep to it */
    NSImage *image = [[NSImage alloc] init];
    [image addRepresentation:bitmapRep];
    return image;
}

NativeNSImageRef darwinToNSImageRef(const QImage *pImage)
{
    /* Create CGImage on the basis of passed QImage: */
    CGImageRef pCGImage = ::darwinToCGImageRef(pImage);
    NativeNSImageRef pNSImage = ::darwinToNSImageRef(pCGImage);
    CGImageRelease(pCGImage);
    /* Apply device pixel ratio: */
    double dScaleFactor = pImage->devicePixelRatio();
    NSSize imageSize = { (CGFloat)pImage->width() / dScaleFactor,
                         (CGFloat)pImage->height() / dScaleFactor };
    [pNSImage setSize:imageSize];
    /* Return result: */
    return pNSImage;
}

NativeNSImageRef darwinToNSImageRef(const QPixmap *pPixmap)
{
   CGImageRef pCGImage = ::darwinToCGImageRef(pPixmap);
   NativeNSImageRef pNSImage = ::darwinToNSImageRef(pCGImage);
   CGImageRelease(pCGImage);
   return pNSImage;
}

NativeNSImageRef darwinToNSImageRef(const char *pczSource)
{
   CGImageRef pCGImage = ::darwinToCGImageRef(pczSource);
   NativeNSImageRef pNSImage = ::darwinToNSImageRef(pCGImage);
   CGImageRelease(pCGImage);
   return pNSImage;
}

NativeNSStringRef darwinToNativeString(const char* pcszString)
{
    return [NSString stringWithUTF8String: pcszString];
}

QString darwinFromNativeString(NativeNSStringRef pString)
{
    return [pString cStringUsingEncoding :NSASCIIStringEncoding];
}

void darwinSetShowsToolbarButtonImpl(NativeNSWindowRef pWindow, bool fEnabled)
{
    [pWindow setShowsToolbarButton:fEnabled];
}

void darwinLabelWindow(NativeNSWindowRef pWindow, NativeNSImageRef pImage, double dDpr)
{
    /* Get the parent view of the close button. */
    NSView *wv = [[pWindow standardWindowButton:NSWindowCloseButton] superview];
    if (wv)
    {
        /* We have to calculate the size of the title bar for the center case. */
        NSSize s = [pImage size];
        NSSize s1 = [wv frame].size;
        /* Correctly position the label. */
        NSImageView *iv = [[NSImageView alloc] initWithFrame:NSMakeRect(s1.width - s.width / dDpr,
                                                                        s1.height - s.height / dDpr - 1,
                                                                        s.width / dDpr, s.height / dDpr)];
        /* Configure the NSImageView for auto moving. */
        [iv setImage:pImage];
        [iv setAutoresizesSubviews:true];
        [iv setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
        /* Add it to the parent of the close button. */
        [wv addSubview:iv positioned:NSWindowBelow relativeTo:nil];
    }
}

void darwinSetShowsResizeIndicatorImpl(NativeNSWindowRef pWindow, bool fEnabled)
{
    [pWindow setShowsResizeIndicator:fEnabled];
}

void darwinSetHidesAllTitleButtonsImpl(NativeNSWindowRef pWindow)
{
    /* Remove all title buttons by changing the style mask. This method is
       available from 10.6 on only. */
    if ([pWindow respondsToSelector: @selector(setStyleMask:)])
        [pWindow performSelector: @selector(setStyleMask:) withObject: (id)NSTitledWindowMask];
    else
    {
        /* On pre 10.6 disable all the buttons currently displayed. Don't use
           setHidden cause this remove the buttons, but didn't release the
           place used for the buttons. */
        NSButton *pButton = [pWindow standardWindowButton:NSWindowCloseButton];
        if (pButton != Nil)
            [pButton setEnabled: NO];
        pButton = [pWindow standardWindowButton:NSWindowMiniaturizeButton];
        if (pButton != Nil)
            [pButton setEnabled: NO];
        pButton = [pWindow standardWindowButton:NSWindowZoomButton];
        if (pButton != Nil)
            [pButton setEnabled: NO];
        pButton = [pWindow standardWindowButton:NSWindowDocumentIconButton];
        if (pButton != Nil)
            [pButton setEnabled: NO];
    }
}

void darwinSetShowsWindowTransparentImpl(NativeNSWindowRef pWindow, bool fEnabled)
{
    if (fEnabled)
    {
        [pWindow setOpaque:NO];
        [pWindow setBackgroundColor:[NSColor clearColor]];
        [pWindow setHasShadow:NO];
    }
    else
    {
        [pWindow setOpaque:YES];
        [pWindow setBackgroundColor:[NSColor windowBackgroundColor]];
        [pWindow setHasShadow:YES];
    }
}

void darwinSetWindowHasShadow(NativeNSWindowRef pWindow, bool fEnabled)
{
    if (fEnabled)
        [pWindow setHasShadow :YES];
    else
        [pWindow setHasShadow :NO];
}

void darwinMinaturizeWindow(NativeNSWindowRef pWindow)
{
    RT_NOREF(pWindow);
//    [[NSApplication sharedApplication] miniaturizeAll];
//    printf("bla\n");
//    [pWindow miniaturize:pWindow];
//    [[NSApplication sharedApplication] deactivate];
//    [pWindow performMiniaturize:nil];
}

void darwinEnableFullscreenSupport(NativeNSWindowRef pWindow)
{
    [pWindow setCollectionBehavior :NSWindowCollectionBehaviorFullScreenPrimary];
}

void darwinEnableTransienceSupport(NativeNSWindowRef pWindow)
{
    [pWindow setCollectionBehavior :NSWindowCollectionBehaviorTransient];
}

void darwinToggleFullscreenMode(NativeNSWindowRef pWindow)
{
    /* Toggle native fullscreen mode for passed pWindow. This method is available since 10.7 only. */
    if ([pWindow respondsToSelector: @selector(toggleFullScreen:)])
        [pWindow performSelector: @selector(toggleFullScreen:) withObject: (id)nil];
}

void darwinToggleWindowZoom(NativeNSWindowRef pWindow)
{
    /* Toggle native window zoom for passed pWindow. This method is available since 10.0. */
    if ([pWindow respondsToSelector: @selector(zoom:)])
        [pWindow performSelector: @selector(zoom:)];
}

bool darwinIsInFullscreenMode(NativeNSWindowRef pWindow)
{
    /* Check whether passed pWindow is in native fullscreen mode. */
    return [pWindow styleMask] & NSFullScreenWindowMask;
}

bool darwinIsOnActiveSpace(NativeNSWindowRef pWindow)
{
    /* Check whether passed pWindow is on active space. */
    return [pWindow isOnActiveSpace];
}

bool darwinScreensHaveSeparateSpaces()
{
    /* Check whether screens have separate spaces.
     * This method is available since 10.9 only. */
    if ([NSScreen respondsToSelector: @selector(screensHaveSeparateSpaces)])
        return [NSScreen performSelector: @selector(screensHaveSeparateSpaces)];
    else
        return false;
}

bool darwinIsScrollerStyleOverlay()
{
    /* Check whether scrollers by default have legacy style.
     * This method is available since 10.7 only. */
    if ([NSScroller respondsToSelector: @selector(preferredScrollerStyle)])
    {
        const int enmType = (int)(intptr_t)[NSScroller performSelector: @selector(preferredScrollerStyle)];
        return enmType == NSScrollerStyleOverlay;
    }
    else
        return false;
}

/**
 * Calls the + (void)setMouseCoalescingEnabled:(BOOL)flag class method.
 *
 * @param   fEnabled    Whether to enable or disable coalescing.
 */
void darwinSetMouseCoalescingEnabled(bool fEnabled)
{
    [NSEvent setMouseCoalescingEnabled:fEnabled];
}

void darwinWindowAnimateResizeImpl(NativeNSWindowRef pWindow, int x, int y, int width, int height)
{
    RT_NOREF(x, y, width);

    /* It seems that Qt doesn't return the height of the window with the
     * toolbar height included. So add this size manually. Could easily be that
     * the Trolls fix this in the final release. */
    NSToolbar *toolbar = [pWindow toolbar];
    NSRect windowFrame = [pWindow frame];
    int toolbarHeight = 0;
    if(toolbar && [toolbar isVisible])
        toolbarHeight = NSHeight(windowFrame) - NSHeight([[pWindow contentView] frame]);
    int h = height + toolbarHeight;
    int h1 = h - NSHeight(windowFrame);
    windowFrame.size.height = h;
    windowFrame.origin.y -= h1;

    [pWindow setFrame:windowFrame display:YES animate: YES];
}

void darwinWindowAnimateResizeNewImpl(NativeNSWindowRef pWindow, int height, bool fAnimate)
{
    /* It seems that Qt doesn't return the height of the window with the
     * toolbar height included. So add this size manually. Could easily be that
     * the Trolls fix this in the final release. */
    NSToolbar *toolbar = [pWindow toolbar];
    NSRect windowFrame = [pWindow frame];
    int toolbarHeight = 0;
    if(toolbar && [toolbar isVisible])
        toolbarHeight = NSHeight(windowFrame) - NSHeight([[pWindow contentView] frame]);
    int h = height + toolbarHeight;
    int h1 = h - NSHeight(windowFrame);
    windowFrame.size.height = h;
    windowFrame.origin.y -= h1;

    [pWindow setFrame:windowFrame display:YES animate: fAnimate ? YES : NO];
}

void darwinTest(NativeNSViewRef pViewOld, NativeNSViewRef pViewNew, int h)
{
    NSMutableDictionary *pDicts[3] = { nil, nil, nil };
    int c = 0;

    /* Scaling necessary? */
    if (h != -1)
    {
        NSWindow *pWindow  = [(pViewOld ? pViewOld : pViewNew) window];
        NSToolbar *toolbar = [pWindow toolbar];
        NSRect windowFrame = [pWindow frame];
        /* Dictionary containing all animation parameters. */
        pDicts[c] = [NSMutableDictionary dictionaryWithCapacity:2];
        /* Specify the animation target. */
        [pDicts[c] setObject:pWindow forKey:NSViewAnimationTargetKey];
        /* Scaling effect. */
        [pDicts[c] setObject:[NSValue valueWithRect:windowFrame] forKey:NSViewAnimationStartFrameKey];
        int toolbarHeight = 0;
        if(toolbar && [toolbar isVisible])
            toolbarHeight = NSHeight(windowFrame) - NSHeight([[pWindow contentView] frame]);
        int h1 = h + toolbarHeight;
        int h2 = h1 - NSHeight(windowFrame);
        windowFrame.size.height = h1;
        windowFrame.origin.y -= h2;
        [pDicts[c] setObject:[NSValue valueWithRect:windowFrame] forKey:NSViewAnimationEndFrameKey];
        ++c;
    }
    /* Fade out effect. */
    if (pViewOld)
    {
        /* Dictionary containing all animation parameters. */
        pDicts[c] = [NSMutableDictionary dictionaryWithCapacity:2];
        /* Specify the animation target. */
        [pDicts[c] setObject:pViewOld forKey:NSViewAnimationTargetKey];
        /* Fade out effect. */
        [pDicts[c] setObject:NSViewAnimationFadeOutEffect forKey:NSViewAnimationEffectKey];
        ++c;
    }
    /* Fade in effect. */
    if (pViewNew)
    {
        /* Dictionary containing all animation parameters. */
        pDicts[c] = [NSMutableDictionary dictionaryWithCapacity:2];
        /* Specify the animation target. */
        [pDicts[c] setObject:pViewNew forKey:NSViewAnimationTargetKey];
        /* Fade in effect. */
        [pDicts[c] setObject:NSViewAnimationFadeInEffect forKey:NSViewAnimationEffectKey];
        ++c;
    }
    /* Create our animation object. */
    NSViewAnimation *pAni = [[NSViewAnimation alloc] initWithViewAnimations:[NSArray arrayWithObjects:pDicts count:c]];
    [pAni setDuration:.15];
    [pAni setAnimationCurve:NSAnimationEaseIn];
    [pAni setAnimationBlockingMode:NSAnimationBlocking];
//    [pAni setAnimationBlockingMode:NSAnimationNonblockingThreaded];

    /* Run the animation. */
    [pAni startAnimation];
    /* Cleanup */
    [pAni release];
}

void darwinWindowInvalidateShadowImpl(NativeNSWindowRef pWindow)
{
    [pWindow invalidateShadow];
}

int darwinWindowToolBarHeight(NativeNSWindowRef pWindow)
{
    NSToolbar *toolbar = [pWindow toolbar];
    NSRect windowFrame = [pWindow frame];
    int toolbarHeight = 0;
    int theight = (NSHeight([NSWindow contentRectForFrameRect:[pWindow frame] styleMask:[pWindow styleMask]]) - NSHeight([[pWindow contentView] frame]));
    /* toolbar height: */
    if(toolbar && [toolbar isVisible])
        /* title bar height: */
        toolbarHeight = NSHeight(windowFrame) - NSHeight([[pWindow contentView] frame]) - theight;

    return toolbarHeight;
}

int darwinWindowTitleHeight(NativeNSWindowRef pWindow)
{
    NSView *pSuperview = [[pWindow standardWindowButton:NSWindowCloseButton] superview];
    NSSize sz = [pSuperview frame].size;
    return sz.height;
}

bool darwinIsToolbarVisible(NativeNSWindowRef pWindow)
{
    NSToolbar *pToolbar = [pWindow toolbar];

    return [pToolbar isVisible] == YES;
}

bool darwinIsWindowMaximized(NativeNSWindowRef pWindow)
{
    /* Mac OS X API NSWindow isZoomed returns true even for almost maximized windows,
     * So implementing this by ourseleves by comparing visible screen-frame & window-frame: */
    NSRect windowFrame = [pWindow frame];
    NSRect screenFrame = [[NSScreen mainScreen] visibleFrame];

    return (windowFrame.origin.x == screenFrame.origin.x) &&
           (windowFrame.origin.y == screenFrame.origin.y) &&
           (windowFrame.size.width == screenFrame.size.width) &&
           (windowFrame.size.height == screenFrame.size.height);
}

bool darwinOpenFile(NativeNSStringRef pstrFile)
{
    return [[NSWorkspace sharedWorkspace] openFile:pstrFile];
}

float darwinSmallFontSize()
{
    float size = [NSFont systemFontSizeForControlSize: NSSmallControlSize];

    return size;
}

bool darwinMouseGrabEvents(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser)
{
    NSEvent *pEvent = (NSEvent*)pvCocoaEvent;
    NSEventType EvtType = [pEvent type];
    NSWindow *pWin = ::darwinToNativeWindow((QWidget*)pvUser);
    if (   pWin == [pEvent window]
        && (   EvtType == NSLeftMouseDown
            || EvtType == NSLeftMouseUp
            || EvtType == NSRightMouseDown
            || EvtType == NSRightMouseUp
            || EvtType == NSOtherMouseDown
            || EvtType == NSOtherMouseUp
            || EvtType == NSLeftMouseDragged
            || EvtType == NSRightMouseDragged
            || EvtType == NSOtherMouseDragged
            || EvtType == NSMouseMoved
            || EvtType == NSScrollWheel))
    {
        /* When the mouse position is not associated to the mouse cursor, the x
           and y values are reported as delta values. */
        float x = [pEvent deltaX];
        float y = [pEvent deltaY];
        if (EvtType == NSScrollWheel)
        {
            /* In the scroll wheel case we have to do some magic, cause a
               normal scroll wheel on a mouse behaves different to a trackpad.
               The following is used within Qt. We use the same to get a
               similar behavior. */
            if ([pEvent respondsToSelector:@selector(deviceDeltaX:)])
                x = (float)(intptr_t)[pEvent performSelector:@selector(deviceDeltaX)] * 2;
            else
                x = qBound(-120, (int)(x * 10000), 120);
            if ([pEvent respondsToSelector:@selector(deviceDeltaY:)])
                y = (float)(intptr_t)[pEvent performSelector:@selector(deviceDeltaY)] * 2;
            else
                y = qBound(-120, (int)(y * 10000), 120);
        }
        /* Get the buttons which where pressed when this event occurs. We have
           to use Carbon here, cause the Cocoa method [NSEvent pressedMouseButtons]
           is >= 10.6. */
        uint32 buttonMask = 0;
        GetEventParameter((EventRef)pvCarbonEvent, kEventParamMouseChord, typeUInt32, 0,
                          sizeof(buttonMask), 0, &buttonMask);
        /* Produce a Qt event out of our info. */
        ::darwinSendMouseGrabEvents((QWidget*)pvUser, EvtType, [pEvent buttonNumber], buttonMask, x, y);
        return true;
    }
    return false;
}

/* Cocoa event handler which checks if the user right clicked at the unified
   toolbar or the title area. */
bool darwinUnifiedToolbarEvents(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser)
{
    RT_NOREF(pvCarbonEvent);

    NSEvent *pEvent = (NSEvent*)pvCocoaEvent;
    NSEventType EvtType = [pEvent type];
    NSWindow *pWin = ::darwinToNativeWindow((QWidget*)pvUser);
    /* First check for the right event type and that we are processing events
       from the window which was registered by the user. */
    if (   EvtType == NSRightMouseDown
        && pWin == [pEvent window])
    {
        /* Get the mouse position of the event (screen coordinates) */
        NSPoint point = [NSEvent mouseLocation];
        /* Get the frame rectangle of the window (screen coordinates) */
        NSRect winFrame = [pWin frame];
        /* Calculate the height of the title and the toolbar */
        int i = NSHeight(winFrame) - NSHeight([[pWin contentView] frame]);
        /* Based on that height create a rectangle of the unified toolbar + title */
        winFrame.origin.y += winFrame.size.height - i;
        winFrame.size.height = i;
        /* Check if the mouse press event was on the unified toolbar or title */
        if (NSMouseInRect(point, winFrame, NO))
            /* Create a Qt context menu event, with flipped screen coordinates */
            ::darwinCreateContextMenuEvent(pvUser, point.x, NSHeight([[pWin screen] frame]) - point.y);
    }
    return false;
}

/**
 * Check for some default application key combinations a Mac user expect, like
 * CMD+Q or CMD+H.
 *
 * @returns true if such a key combo was hit, false otherwise.
 * @param   pEvent          The Cocoa event.
 */
bool darwinIsApplicationCommand(ConstNativeNSEventRef pEvent)
{
    NSEventType  eEvtType = [pEvent type];
    bool         fGlobalHotkey = false;
//
//    if (   (eEvtType == NSKeyDown || eEvtType == NSKeyUp)
//        && [[NSApp mainMenu] performKeyEquivalent:pEvent])
//        return true;
//    return false;
//        && [[[NSApp mainMenu] delegate] menuHasKeyEquivalent:[NSApp mainMenu] forEvent:pEvent target:b action:a])

    switch (eEvtType)
    {
        case NSKeyDown:
        case NSKeyUp:
        {
            NSUInteger fEvtMask = [pEvent modifierFlags];
            unsigned short KeyCode = [pEvent keyCode];
            if (   ((fEvtMask & (NX_NONCOALSESCEDMASK | NX_COMMANDMASK | NX_DEVICELCMDKEYMASK)) == (NX_NONCOALSESCEDMASK | NX_COMMANDMASK | NX_DEVICELCMDKEYMASK))  /* L+CMD */
                || ((fEvtMask & (NX_NONCOALSESCEDMASK | NX_COMMANDMASK | NX_DEVICERCMDKEYMASK)) == (NX_NONCOALSESCEDMASK | NX_COMMANDMASK | NX_DEVICERCMDKEYMASK))) /* R+CMD */
            {
                if (   KeyCode == 0x0c  /* CMD+Q (Quit) */
                    || KeyCode == 0x04) /* CMD+H (Hide) */
                    fGlobalHotkey = true;
            }
            else if (   ((fEvtMask & (NX_NONCOALSESCEDMASK | NX_ALTERNATEMASK | NX_DEVICELALTKEYMASK | NX_COMMANDMASK | NX_DEVICELCMDKEYMASK)) == (NX_NONCOALSESCEDMASK | NX_ALTERNATEMASK | NX_DEVICELALTKEYMASK | NX_COMMANDMASK | NX_DEVICELCMDKEYMASK)) /* L+ALT+CMD */
                     || ((fEvtMask & (NX_NONCOALSESCEDMASK | NX_ALTERNATEMASK | NX_DEVICERCMDKEYMASK | NX_COMMANDMASK | NX_DEVICERCMDKEYMASK)) == (NX_NONCOALSESCEDMASK | NX_ALTERNATEMASK | NX_DEVICERCMDKEYMASK | NX_COMMANDMASK | NX_DEVICERCMDKEYMASK))) /* R+ALT+CMD */
            {
                if (KeyCode == 0x04)    /* ALT+CMD+H (Hide-Others) */
                    fGlobalHotkey = true;
            }
            break;
        }
        default: break;
    }
    return fGlobalHotkey;
}

void darwinRetranslateAppMenu()
{
    /* This is purely Qt internal. If the Trolls change something here, it will
       not work anymore, but at least it will not be a burning man. */
    if ([NSApp respondsToSelector:@selector(qt_qcocoamenuLoader)])
    {
        id loader = [NSApp performSelector:@selector(qt_qcocoamenuLoader)];
        if ([loader respondsToSelector:@selector(qtTranslateApplicationMenu)])
            [loader performSelector:@selector(qtTranslateApplicationMenu)];
    }
}

/* Our resize proxy singleton. This class has two major tasks. First it is used
   to proxy the windowWillResize selector of the Qt delegate. As this is class
   global and therewith done for all Qt window instances, we have to track the
   windows we are interested in. This is the second task. */
@interface UIResizeProxy: NSObject
{
    NSMutableArray *m_pArray;
    bool m_fInit;
}
+(UIResizeProxy*)sharedResizeProxy;
-(void)addWindow:(NSWindow*)pWindow;
-(void)removeWindow:(NSWindow*)pWindow;
-(BOOL)containsWindow:(NSWindow*)pWindow;
@end

static UIResizeProxy *gSharedResizeProxy = nil;

@implementation UIResizeProxy
+(UIResizeProxy*)sharedResizeProxy
{
    if (gSharedResizeProxy == nil)
        gSharedResizeProxy = [[super allocWithZone:NULL] init];
    return gSharedResizeProxy;
}
-(id)init
{
    self = [super init];

    m_fInit = false;

    return self;
}
- (void)addWindow:(NSWindow*)pWindow
{
    if (!m_fInit)
    {
        /* Create an array which contains the registered windows. */
        m_pArray = [[NSMutableArray alloc] init];
        /* Swizzle the windowWillResize method. This means replacing the
           original method with our own one and reroute the original one to
           another name. */
        Class oriClass = [[pWindow delegate] class];
        Class myClass = [UIResizeProxy class];
        SEL oriSel = @selector(windowWillResize:toSize:);
        SEL qtSel  = @selector(qtWindowWillResize:toSize:);
        Method m1 = class_getInstanceMethod(oriClass, oriSel);
        Method m2 = class_getInstanceMethod(myClass, oriSel);
        Method m3 = class_getInstanceMethod(myClass, qtSel);
        /* Overwrite the original implementation with our own one. old contains
           the old implementation. */
        IMP old = method_setImplementation(m1, method_getImplementation(m2));
        /* Add a new method to our class with the old implementation. */
        class_addMethod(oriClass, qtSel, old, method_getTypeEncoding(m3));
        m_fInit = true;
    }
    [m_pArray addObject:pWindow];
}
- (void)removeWindow:(NSWindow*)pWindow
{
    [m_pArray removeObject:pWindow];
}
- (BOOL)containsWindow:(NSWindow*)pWindow
{
    return [m_pArray containsObject:pWindow];
}
- (NSSize)qtWindowWillResize:(NSWindow *)pWindow toSize:(NSSize)newFrameSize
{
    RT_NOREF(pWindow);
    /* This is a fake implementation. It will be replaced by the original Qt
       method. */
    return newFrameSize;
}
- (NSSize)windowWillResize:(NSWindow *)pWindow toSize:(NSSize)newFrameSize
{
    /* Call the original implementation for newFrameSize. */
    NSSize qtSize = [self qtWindowWillResize:pWindow toSize:newFrameSize];
    /* Check if we are responsible for this window. */
    if (![[UIResizeProxy sharedResizeProxy] containsWindow:pWindow])
        return qtSize;
    /* The static modifier method in NSEvent is >= 10.6. It allows us to check
       the shift modifier state during the resize. If it is not available the
       user have to press shift before he start to resize. */
    if ([NSEvent respondsToSelector:@selector(modifierFlags)])
    {
        if (((int)(intptr_t)[NSEvent performSelector:@selector(modifierFlags)] & NSShiftKeyMask) == NSShiftKeyMask)
            return qtSize;
    }
    else
    {
        /* Shift key pressed when this resize event was initiated? */
        if (([pWindow resizeFlags] & NSShiftKeyMask) == NSShiftKeyMask)
            return qtSize;
    }
    /* The default case is to calculate the aspect radio of the old size and
       used it for the new size. */
    NSSize s = [pWindow frame].size;
    double a = (double)s.width / s.height;
    NSSize newSize = NSMakeSize(newFrameSize.width, newFrameSize.width / a);
    /* We have to make sure the new rectangle meets the minimum requirements. */
    NSSize testSize = [self qtWindowWillResize:pWindow toSize:newSize];
    if (   testSize.width > newSize.width
        || testSize.height > newSize.height)
    {
        double w1 = testSize.width / newSize.width;
        double h1 = testSize.height / newSize.height;
        if (   w1 > 1
            && w1 > h1)
        {
            newSize.width = testSize.width;
            newSize.height = testSize.width * a;
        }else if (h1 > 1)
        {
            newSize.width = testSize.height * a;
            newSize.height = testSize.height;
        }
    }
    return newSize;
}
@end

void darwinInstallResizeDelegate(NativeNSWindowRef pWindow)
{
    [[UIResizeProxy sharedResizeProxy] addWindow:pWindow];
}

void darwinUninstallResizeDelegate(NativeNSWindowRef pWindow)
{
    [[UIResizeProxy sharedResizeProxy] removeWindow:pWindow];
}

void *darwinCocoaToCarbonEvent(void *pvCocoaEvent)
{
    NSEvent *pEvent = (NSEvent*)pvCocoaEvent;
    return (void*)[pEvent eventRef];
}
