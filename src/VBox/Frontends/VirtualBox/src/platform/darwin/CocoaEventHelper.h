/* $Id: CocoaEventHelper.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility functions for handling Darwin Cocoa specific event-handling tasks.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_CocoaEventHelper_h
#define FEQT_INCLUDED_SRC_platform_darwin_CocoaEventHelper_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"

/* Other VBox includes: */
#include <VBox/VBoxCocoa.h>

/* Cocoa declarations: */
ADD_COCOA_NATIVE_REF(NSEvent);


RT_C_DECLS_BEGIN

/** Calls the -(NSUInteger)modifierFlags method on @a pEvent object and converts the flags to carbon style. */
uint32_t darwinEventModifierFlagsXlated(ConstNativeNSEventRef pEvent);

/** Get the name for a Cocoa @a enmEventType. */
const char *darwinEventTypeName(unsigned long enmEventType);

/** Debug helper function for dumping a Cocoa event to stdout.
  * @param   pszPrefix  Brings the message prefix.
  * @param   pEvent     Brings the Cocoa event. */
void darwinPrintEvent(const char *pszPrefix, ConstNativeNSEventRef pEvent);

/** Posts stripped mouse event based on passed @a pEvent. */
SHARED_LIBRARY_STUFF void darwinPostStrippedMouseEvent(ConstNativeNSEventRef pEvent);

RT_C_DECLS_END


#endif /* !FEQT_INCLUDED_SRC_platform_darwin_CocoaEventHelper_h */

