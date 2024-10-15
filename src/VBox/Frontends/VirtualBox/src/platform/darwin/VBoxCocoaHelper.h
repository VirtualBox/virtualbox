/* $Id: VBoxCocoaHelper.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - VBoxCocoa Helper.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_VBoxCocoaHelper_h
#define FEQT_INCLUDED_SRC_platform_darwin_VBoxCocoaHelper_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Global includes */
#include <VBox/VBoxCocoa.h>

#ifdef __OBJC__

/* System includes */
#import <AppKit/NSImage.h>
#import <Foundation/NSAutoreleasePool.h>
#import <CoreFoundation/CFString.h>

/* Qt includes */
#include <QString>
#include <QVarLengthArray>

inline NSString *darwinQStringToNSString(const QString &aString)
{
    const UniChar *chars = reinterpret_cast<const UniChar *>(aString.unicode());
    CFStringRef str = CFStringCreateWithCharacters(0, chars, aString.length());
    return [(NSString*)CFStringCreateMutableCopy(0, 0, str) autorelease];
}

inline QString darwinNSStringToQString(const NSString *aString)
{
    CFStringRef str = reinterpret_cast<const CFStringRef>(aString);
    if(!str)
        return QString();
    CFIndex length = CFStringGetLength(str);
    const UniChar *chars = CFStringGetCharactersPtr(str);
    if (chars)
        return QString(reinterpret_cast<const QChar *>(chars), length);

    QVarLengthArray<UniChar> buffer(length);
    CFStringGetCharacters(str, CFRangeMake(0, length), buffer.data());
    return QString(reinterpret_cast<const QChar *>(buffer.constData()), length);
}

#endif /* __OBJC__ */

#endif /* !FEQT_INCLUDED_SRC_platform_darwin_VBoxCocoaHelper_h */

