/* $Id: UIDesktopServices_darwin_cocoa.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt GUI - Utility Classes and Functions specific to darwin.
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

/* VBox includes */
#include "UIDesktopServices_darwin_p.h"

/* System includes */
#include <Carbon/Carbon.h>
#import <AppKit/NSWorkspace.h>

/* Create desktop alias using a bookmark stuff. */
bool darwinCreateMachineShortcut(NativeNSStringRef pstrSrcFile, NativeNSStringRef pstrDstPath, NativeNSStringRef pstrName, NativeNSStringRef /* pstrUuid */)
{
    RT_NOREF(pstrName);
    if (!pstrSrcFile || !pstrDstPath)
        return false;

    NSError  *pErr        = nil;
    NSURL    *pSrcUrl     = [NSURL fileURLWithPath:pstrSrcFile];

    NSString *pVmFileName = [pSrcUrl lastPathComponent];
    NSString *pSrcPath    = [NSString stringWithFormat:@"%@/%@", pstrDstPath, [pVmFileName stringByDeletingPathExtension]];
    NSURL    *pDstUrl     = [NSURL fileURLWithPath:pSrcPath];

    bool rc = false;

    if (!pSrcUrl || !pDstUrl)
        return false;

    NSData *pBookmark = [pSrcUrl bookmarkDataWithOptions:NSURLBookmarkCreationSuitableForBookmarkFile
                                 includingResourceValuesForKeys:nil
                                 relativeToURL:nil
                                 error:&pErr];

    if (pBookmark)
    {
        rc = [NSURL writeBookmarkData:pBookmark
                    toURL:pDstUrl
                    options:0
                    error:&pErr];
    }

    return rc;
}

bool darwinOpenInFileManager(NativeNSStringRef pstrFile)
{
    return [[NSWorkspace sharedWorkspace] selectFile:pstrFile inFileViewerRootedAtPath:@""];
}

