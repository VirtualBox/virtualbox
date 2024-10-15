/* $Id: UICocoaDockIconPreview.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Cocoa helper for the dock icon preview.
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

/* VBox includes */
#include "UICocoaDockIconPreview.h"
#include "VBoxCocoaHelper.h"

/* System includes */
#import <Cocoa/Cocoa.h>

@interface UIDockTileMonitor: NSView
{
    UICocoaDockIconPreviewPrivate *p;

    NSImageView *mScreenContent;
    NSImageView *mMonitorGlossy;
}
- (id)initWithFrame:(NSRect)frame parent:(UICocoaDockIconPreviewPrivate*)parent;
- (NSImageView*)screenContent;
- (void)resize:(NSSize)size;
@end

@interface UIDockTileOverlay: NSView
{
    UICocoaDockIconPreviewPrivate *p;
}
- (id)initWithFrame:(NSRect)frame parent:(UICocoaDockIconPreviewPrivate*)parent;
@end

@interface UIDockTile: NSView
{
    UICocoaDockIconPreviewPrivate *p;

    UIDockTileMonitor *mMonitor;
    NSImageView       *mAppIcon;

    UIDockTileOverlay *mOverlay;
}
- (id)initWithParent:(UICocoaDockIconPreviewPrivate*)parent;
- (void)destroy;
- (NSView*)screenContentWithParentView:(NSView*)parentView;
- (void)cleanup;
- (void)restoreAppIcon;
- (void)updateAppIcon;
- (void)restoreMonitor;
- (void)updateMonitorWithImage:(CGImageRef)image;
- (void)resizeMonitor:(NSSize)size;
@end

/*
 * Helper class which allow us to access all members/methods of AbstractDockIconPreviewHelper
 * from any Cocoa class.
 */
class UICocoaDockIconPreviewPrivate: public UIAbstractDockIconPreviewHelper
{
public:
    inline UICocoaDockIconPreviewPrivate(UIMachine *pMachine, const QPixmap& overlayImage)
        : UIAbstractDockIconPreviewHelper(pMachine, overlayImage)
    {
        mUIDockTile = [[UIDockTile alloc] initWithParent:this];
    }

    inline ~UICocoaDockIconPreviewPrivate()
    {

        [mUIDockTile destroy];
        [mUIDockTile release];
    }

    UIDockTile *mUIDockTile;
};

/*
 * Cocoa wrapper for the abstract dock icon preview class
 */
UICocoaDockIconPreview::UICocoaDockIconPreview(UIMachine *pMachine, const QPixmap& overlayImage)
    : UIAbstractDockIconPreview(pMachine, overlayImage)
{
    CocoaAutoreleasePool pool;

    d = new UICocoaDockIconPreviewPrivate(pMachine, overlayImage);
}

UICocoaDockIconPreview::~UICocoaDockIconPreview()
{
    CocoaAutoreleasePool pool;

    delete d;
}

void UICocoaDockIconPreview::updateDockOverlay()
{
    CocoaAutoreleasePool pool;

    [d->mUIDockTile updateAppIcon];
}

void UICocoaDockIconPreview::updateDockPreview(CGImageRef VMImage)
{
    CocoaAutoreleasePool pool;

    [d->mUIDockTile updateMonitorWithImage:VMImage];
}

void UICocoaDockIconPreview::updateDockPreview(UIFrameBuffer *pFrameBuffer)
{
    CocoaAutoreleasePool pool;

    UIAbstractDockIconPreview::updateDockPreview(pFrameBuffer);
}

void UICocoaDockIconPreview::setOriginalSize(int width, int height)
{
    CocoaAutoreleasePool pool;

    [d->mUIDockTile resizeMonitor:NSMakeSize(width, height)];
}

/*
 * Class for arranging/updating the layers for the glossy monitor preview.
 */
@implementation UIDockTileMonitor
- (id)initWithFrame:(NSRect)frame parent:(UICocoaDockIconPreviewPrivate*)parent
{
    self = [super initWithFrame:frame];

    if (self != nil)
    {
        p = parent;
        /* The screen content view */
        mScreenContent = [[NSImageView alloc] initWithFrame:NSRectFromCGRect(p->flipRect(p->m_updateRect))];
//        [mScreenContent setImageAlignment: NSImageAlignCenter];
        [mScreenContent setImageAlignment: NSImageAlignTopLeft];
        [mScreenContent setImageScaling: NSImageScaleAxesIndependently];
        [self addSubview: mScreenContent];
        /* The state view */
        mMonitorGlossy = [[NSImageView alloc] initWithFrame:NSRectFromCGRect(p->flipRect(p->m_monitorRect))];
        [mMonitorGlossy setImage: ::darwinToNSImageRef(p->m_dockMonitorGlossy)];
        [self addSubview: mMonitorGlossy];
    }

    return self;
}

- (void)drawRect:(NSRect)aRect
{
    NSImage *dockMonitor = ::darwinToNSImageRef(p->m_dockMonitor);
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
    [dockMonitor drawInRect:NSRectFromCGRect(p->flipRect(p->m_monitorRect)) fromRect:aRect operation:NSCompositingOperationSourceOver fraction:1.0];
#else
    [dockMonitor drawInRect:NSRectFromCGRect(p->flipRect(p->m_monitorRect)) fromRect:aRect operation:NSCompositeSourceOver fraction:1.0];
#endif
    [dockMonitor release];
}

- (NSImageView*)screenContent
{
    return mScreenContent;
}

- (void)resize:(NSSize)size
{
    /* Calculate the new size based on the aspect ratio of the original screen
       size */
    float w, h;
    if (size.width > size.height)
    {
        w = p->m_updateRect.size.width;
        h = ((float)size.height / size.width * p->m_updateRect.size.height);
    }
    else
    {
        w = ((float)size.width / size.height * p->m_updateRect.size.width);
        h = p->m_updateRect.size.height;
    }
    CGRect r = (p->flipRect (p->centerRectTo (CGRectMake (0, 0, (int)w, (int)h), p->m_updateRect)));
    r.origin.x = (int)r.origin.x;
    r.origin.y = (int)r.origin.y;
    r.size.width = (int)r.size.width;
    r.size.height = (int)r.size.height;
//    printf("gui %f %f %f %f\n", r.origin.x, r.origin.y, r.size.width, r.size.height);
    /* Center within the update rect */
    [mScreenContent setFrame:NSRectFromCGRect (r)];
}
@end

/*
 * Simple implementation for the overlay of the OS & the state icon. Is used both
 * in the application icon & preview mode.
 */
@implementation UIDockTileOverlay
- (id)initWithFrame:(NSRect)frame parent:(UICocoaDockIconPreviewPrivate*)parent
{
    self = [super initWithFrame:frame];

    if (self != nil)
        p = parent;

    return self;
}

- (void)drawRect:(NSRect)aRect
{
    RT_NOREF(aRect);
    NSGraphicsContext *nsContext = [NSGraphicsContext currentContext];
    CGContextRef pCGContext = (CGContextRef)[nsContext graphicsPort];
    p->drawOverlayIcons (pCGContext);
}
@end

/*
 * VirtualBox Dock Tile implementation. Manage the switching between the icon
 * and preview mode & forwards all update request to the appropriate methods.
 */
@implementation UIDockTile
- (id)initWithParent:(UICocoaDockIconPreviewPrivate*)parent
{
    self = [super init];

    if (self != nil)
    {
        p = parent;
        /* Add self as the content view of the dock tile */
        NSDockTile *dock = [[NSApplication sharedApplication] dockTile];
        [dock setContentView: self];
        /* App icon is default */
        [self restoreAppIcon];
        /* The overlay */
        mOverlay = [[UIDockTileOverlay alloc] initWithFrame:NSRectFromCGRect(p->flipRect (p->m_dockIconRect)) parent:p];
        [self addSubview: mOverlay];
    }

    return self;
}

- (void)destroy
{
    /* Remove all content from the application dock tile. */
    [mOverlay removeFromSuperview];
    [mOverlay release];
    mOverlay = nil;
    NSDockTile *dock = [[NSApplication sharedApplication] dockTile];
    [dock setContentView: nil];
    /* Cleanup all other resources */
    [self cleanup];
}

- (NSView*)screenContentWithParentView:(NSView*)parentView
{
    if (mMonitor != nil)
    {
        void *pId = p->currentPreviewWindowId();
        if (parentView == pId)
            return [mMonitor screenContent];
    }
    return nil;
}

- (void)cleanup
{
    if (mAppIcon != nil)
    {
        [mAppIcon removeFromSuperview];
        [mAppIcon release];
        mAppIcon = nil;
    }
    if (mMonitor != nil)
    {
        [mMonitor removeFromSuperview];
        [mMonitor release];
        mMonitor = nil;
    }
}

- (void)restoreAppIcon
{
    if (mAppIcon == nil)
    {
        [self cleanup];
        mAppIcon = [[NSImageView alloc] initWithFrame:NSRectFromCGRect (p->flipRect (p->m_dockIconRect))];
        [mAppIcon setImage: [NSImage imageNamed:@"NSApplicationIcon"]];
        [self addSubview: mAppIcon positioned:NSWindowBelow relativeTo:mOverlay];
    }
}

- (void)updateAppIcon
{
    [self restoreAppIcon];
    [[[NSApplication sharedApplication] dockTile] display];
}

- (void)restoreMonitor
{
    if (mMonitor == nil)
    {
        p->initPreviewImages();
        [self cleanup];
        mMonitor = [[UIDockTileMonitor alloc] initWithFrame:NSRectFromCGRect (p->flipRect (p->m_dockIconRect)) parent:p];
        [self addSubview: mMonitor positioned:NSWindowBelow relativeTo:mOverlay];
    }
}

- (void)updateMonitorWithImage:(CGImageRef)image
{
    [self restoreMonitor];
    NSImage *nsimage = ::darwinToNSImageRef(image);
    [[mMonitor screenContent] setImage: nsimage];
    [nsimage release];
    [[[NSApplication sharedApplication] dockTile] display];
}

- (void)resizeMonitor:(NSSize)size
{
    [self restoreMonitor];
    [mMonitor resize:size];
}
@end

