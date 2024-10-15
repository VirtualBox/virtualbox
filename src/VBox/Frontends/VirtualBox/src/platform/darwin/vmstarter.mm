/* $Id: vmstarter.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI -  Helper application for starting vbox the right way when the user double clicks on a file type association.
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

#import <Cocoa/Cocoa.h>
#include <iprt/cdefs.h>

@interface AppDelegate: NSObject
{
NSString *m_strVBoxPath;
}
@end

@implementation AppDelegate
-(id) init
{
    self = [super init];
    if (self)
    {
        /* Get the path of VBox by looking where our bundle is located. */
        m_strVBoxPath = [[[[NSBundle mainBundle] bundlePath]
                          stringByAppendingPathComponent:@"/../../../../VirtualBox.app"]
                         stringByStandardizingPath];
        /* We kill ourself after 1 seconds */
        [NSTimer scheduledTimerWithTimeInterval:1.0
            target:NSApp
            selector:@selector(terminate:)
            userInfo:nil
            repeats:NO];
    }

    return self;
}

- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames
{
    RT_NOREF(sender);

    BOOL fResult = FALSE;
    NSWorkspace *pWS = [NSWorkspace sharedWorkspace];
    /* We need to check if vbox is running already. If so we sent an open
       event. If not we start a new process with the file as parameter. */
    NSArray *pApps = [pWS runningApplications];
    bool fVBoxRuns = false;
    for (NSRunningApplication *pApp in pApps)
    {
        if ([pApp.bundleIdentifier isEqualToString:@"org.virtualbox.app.VirtualBox"])
        {
            fVBoxRuns = true;
            break;
        }
    }
    if (fVBoxRuns)
    {
        /* Send the open event.
         * Todo: check for an method which take a list of files. */
        for (NSString *filename in filenames)
            fResult = [pWS openFile:filename withApplication:m_strVBoxPath andDeactivate:TRUE];
    }
    else
    {
        /* Fire up a new instance of VBox. We prefer LSOpenApplication over
           NSTask, cause it makes sure that VBox will become the front most
           process after starting up. */
/** @todo should replace all this with -[NSWorkspace
 *  launchApplicationAtURL:options:configuration:error:] because LSOpenApplication is deprecated in
 * 10.10 while, FSPathMakeRef is deprecated since 10.8. */
        /* The state horror show starts right here: */
        OSStatus err = noErr;
        Boolean fDir;
        void *asyncLaunchRefCon = NULL;
        FSRef fileRef;
        CFStringRef file = NULL;
        CFArrayRef args = NULL;
        void **list = (void**)malloc(sizeof(void*) * [filenames count]);
        for (size_t i = 0; i < [filenames count]; ++i)
            list[i] = [filenames objectAtIndex:i];
        do
        {
            NSString *strVBoxExe = [m_strVBoxPath stringByAppendingPathComponent:@"Contents/MacOS/VirtualBox"];
            err = FSPathMakeRef((const UInt8*)[strVBoxExe UTF8String], &fileRef, &fDir);
            if (err != noErr)
                break;
            args = CFArrayCreate(NULL, (const void **)list, [filenames count], &kCFTypeArrayCallBacks);
            if (args == NULL)
                break;
            LSApplicationParameters par = { 0, 0, &fileRef, asyncLaunchRefCon, 0, args, 0 };
            err = LSOpenApplication(&par, NULL);
            if (err != noErr)
                break;
            fResult = TRUE;
        }while(0);
        if (list)  /* Why bother checking, because you've crashed already if it's NULL! */
            free(list);
        if (file)
            CFRelease(file);
        if (args)
            CFRelease(args);
    }
}
@end

int main()
{
    /* Global auto release pool. */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    /* Create our own delegate for the application. */
    AppDelegate *pAppDelegate = [[AppDelegate alloc] init];
    [[NSApplication sharedApplication] setDelegate: (id<NSApplicationDelegate>)pAppDelegate]; /** @todo check out ugly cast */
    pAppDelegate = nil;
    /* Start the event loop. */
    [NSApp run];
    /* Cleanup */
    [pool release];
    return 0;
}

