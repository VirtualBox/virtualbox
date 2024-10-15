/* $Id: CocoaEventHelper.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility functions for handling Darwin Cocoa specific event handling tasks.
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

/* GUI includes: */
#include "CocoaEventHelper.h"
#include "DarwinKeyboard.h"

/* External includes: */
#import <Cocoa/Cocoa.h>
#import <AppKit/NSEvent.h>
#include <Carbon/Carbon.h>

/* They just had to rename a whole load of constants in 10.12. Just wrap the carp up
   in some defines for now as we need to keep building with both 10.9 and 10.13+: */
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
# define VBOX_NSAlphaShiftKeyMask    NSEventModifierFlagCapsLock
# define VBOX_NSAlternateKeyMask     NSEventModifierFlagOption
# define VBOX_NSAppKitDefined        NSEventTypeAppKitDefined
# define VBOX_NSApplicationDefined   NSEventTypeApplicationDefined
# define VBOX_NSCommandKeyMask       NSEventModifierFlagCommand
# define VBOX_NSControlKeyMask       NSEventModifierFlagControl
# define VBOX_NSCursorUpdate         NSEventTypeCursorUpdate
# define VBOX_NSFlagsChanged         NSEventTypeFlagsChanged
# define VBOX_NSFunctionKeyMask      NSEventModifierFlagFunction
# define VBOX_NSHelpKeyMask          NSEventModifierFlagHelp
# define VBOX_NSKeyDown              NSEventTypeKeyDown
# define VBOX_NSKeyUp                NSEventTypeKeyUp
# define VBOX_NSLeftMouseDown        NSEventTypeLeftMouseDown
# define VBOX_NSLeftMouseDragged     NSEventTypeLeftMouseDragged
# define VBOX_NSLeftMouseUp          NSEventTypeLeftMouseUp
# define VBOX_NSMouseEntered         NSEventTypeMouseEntered
# define VBOX_NSMouseExited          NSEventTypeMouseExited
# define VBOX_NSMouseMoved           NSEventTypeMouseMoved
# define VBOX_NSNumericPadKeyMask    NSEventModifierFlagNumericPad
# define VBOX_NSOtherMouseDown       NSEventTypeOtherMouseDown
# define VBOX_NSOtherMouseDragged    NSEventTypeOtherMouseDragged
# define VBOX_NSOtherMouseUp         NSEventTypeOtherMouseUp
# define VBOX_NSPeriodic             NSEventTypePeriodic
# define VBOX_NSRightMouseDown       NSEventTypeRightMouseDown
# define VBOX_NSRightMouseDragged    NSEventTypeRightMouseDragged
# define VBOX_NSRightMouseUp         NSEventTypeRightMouseUp
# define VBOX_NSScrollWheel          NSEventTypeScrollWheel
# define VBOX_NSShiftKeyMask         NSEventModifierFlagShift
# define VBOX_NSSystemDefined        NSEventTypeSystemDefined
# define VBOX_NSTabletPoint          NSEventTypeTabletPoint
# define VBOX_NSTabletProximity      NSEventTypeTabletProximity
#else
# define VBOX_NSAlphaShiftKeyMask    NSAlphaShiftKeyMask
# define VBOX_NSAlternateKeyMask     NSAlternateKeyMask
# define VBOX_NSAppKitDefined        NSAppKitDefined
# define VBOX_NSApplicationDefined   NSApplicationDefined
# define VBOX_NSCommandKeyMask       NSCommandKeyMask
# define VBOX_NSControlKeyMask       NSControlKeyMask
# define VBOX_NSCursorUpdate         NSCursorUpdate
# define VBOX_NSFlagsChanged         NSFlagsChanged
# define VBOX_NSFunctionKeyMask      NSFunctionKeyMask
# define VBOX_NSHelpKeyMask          NSHelpKeyMask
# define VBOX_NSKeyDown              NSKeyDown
# define VBOX_NSKeyUp                NSKeyUp
# define VBOX_NSLeftMouseDown        NSLeftMouseDown
# define VBOX_NSLeftMouseDragged     NSLeftMouseDragged
# define VBOX_NSLeftMouseUp          NSLeftMouseUp
# define VBOX_NSMouseEntered         NSMouseEntered
# define VBOX_NSMouseExited          NSMouseExited
# define VBOX_NSMouseMoved           NSMouseMoved
# define VBOX_NSNumericPadKeyMask    NSNumericPadKeyMask
# define VBOX_NSOtherMouseDown       NSOtherMouseDown
# define VBOX_NSOtherMouseDragged    NSOtherMouseDragged
# define VBOX_NSOtherMouseUp         NSOtherMouseUp
# define VBOX_NSPeriodic             NSPeriodic
# define VBOX_NSRightMouseDown       NSRightMouseDown
# define VBOX_NSRightMouseDragged    NSRightMouseDragged
# define VBOX_NSRightMouseUp         NSRightMouseUp
# define VBOX_NSScrollWheel          NSScrollWheel
# define VBOX_NSShiftKeyMask         NSShiftKeyMask
# define VBOX_NSSystemDefined        NSSystemDefined
# define VBOX_NSTabletPoint          NSTabletPoint
# define VBOX_NSTabletProximity      NSTabletProximity
#endif

uint32_t darwinEventModifierFlagsXlated(ConstNativeNSEventRef pEvent)
{
    NSUInteger fCocoa = [pEvent modifierFlags];
    uint32_t fCarbon = 0;
    if (fCocoa)
    {
        if (fCocoa & VBOX_NSAlphaShiftKeyMask)
            fCarbon |= alphaLock;
        if (fCocoa & (VBOX_NSShiftKeyMask | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK))
        {
            if (fCocoa & (NX_DEVICERSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK))
            {
                if (fCocoa & NX_DEVICERSHIFTKEYMASK)
                    fCarbon |= rightShiftKey;
                if (fCocoa & NX_DEVICELSHIFTKEYMASK)
                    fCarbon |= shiftKey;
            }
            else
                fCarbon |= shiftKey;
        }

        if (fCocoa & (VBOX_NSControlKeyMask | NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK))
        {
            if (fCocoa & (NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK))
            {
                if (fCocoa & NX_DEVICERCTLKEYMASK)
                    fCarbon |= rightControlKey;
                if (fCocoa & NX_DEVICELCTLKEYMASK)
                    fCarbon |= controlKey;
            }
            else
                fCarbon |= controlKey;
        }

        if (fCocoa & (VBOX_NSAlternateKeyMask | NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK))
        {
            if (fCocoa & (NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK))
            {
                if (fCocoa & NX_DEVICERALTKEYMASK)
                    fCarbon |= rightOptionKey;
                if (fCocoa & NX_DEVICELALTKEYMASK)
                    fCarbon |= optionKey;
            }
            else
                fCarbon |= optionKey;
        }

        if (fCocoa & (VBOX_NSCommandKeyMask | NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK))
        {
            if (fCocoa & (NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK))
            {
                if (fCocoa & NX_DEVICERCMDKEYMASK)
                    fCarbon |= kEventKeyModifierRightCmdKeyMask;
                if (fCocoa & NX_DEVICELCMDKEYMASK)
                    fCarbon |= cmdKey;
            }
            else
                fCarbon |= cmdKey;
        }

        /*
        if (fCocoa & VBOX_NSNumericPadKeyMask)
            fCarbon |= ???;

        if (fCocoa & VBOX_NSHelpKeyMask)
            fCarbon |= ???;

        if (fCocoa & VBOX_NSFunctionKeyMask)
            fCarbon |= ???;
        */
    }

    return fCarbon;
}

const char *darwinEventTypeName(unsigned long enmEventType)
{
    switch (enmEventType)
    {
#define EVT_CASE(nm) case nm: return #nm
        EVT_CASE(VBOX_NSLeftMouseDown);
        EVT_CASE(VBOX_NSLeftMouseUp);
        EVT_CASE(VBOX_NSRightMouseDown);
        EVT_CASE(VBOX_NSRightMouseUp);
        EVT_CASE(VBOX_NSMouseMoved);
        EVT_CASE(VBOX_NSLeftMouseDragged);
        EVT_CASE(VBOX_NSRightMouseDragged);
        EVT_CASE(VBOX_NSMouseEntered);
        EVT_CASE(VBOX_NSMouseExited);
        EVT_CASE(VBOX_NSKeyDown);
        EVT_CASE(VBOX_NSKeyUp);
        EVT_CASE(VBOX_NSFlagsChanged);
        EVT_CASE(VBOX_NSAppKitDefined);
        EVT_CASE(VBOX_NSSystemDefined);
        EVT_CASE(VBOX_NSApplicationDefined);
        EVT_CASE(VBOX_NSPeriodic);
        EVT_CASE(VBOX_NSCursorUpdate);
        EVT_CASE(VBOX_NSScrollWheel);
        EVT_CASE(VBOX_NSTabletPoint);
        EVT_CASE(VBOX_NSTabletProximity);
        EVT_CASE(VBOX_NSOtherMouseDown);
        EVT_CASE(VBOX_NSOtherMouseUp);
        EVT_CASE(VBOX_NSOtherMouseDragged);
#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_5
        EVT_CASE(NSEventTypeGesture);
        EVT_CASE(NSEventTypeMagnify);
        EVT_CASE(NSEventTypeSwipe);
        EVT_CASE(NSEventTypeRotate);
        EVT_CASE(NSEventTypeBeginGesture);
        EVT_CASE(NSEventTypeEndGesture);
#endif
#undef EVT_CASE
        default:
            return "Unknown!";
    }
}

void darwinPrintEvent(const char *pszPrefix, ConstNativeNSEventRef pEvent)
{
    NSEventType enmEventType = [pEvent type];
    NSUInteger fEventMask = [pEvent modifierFlags];
    NSWindow *pEventWindow = [pEvent window];
    NSInteger iEventWindow = [pEvent windowNumber];
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
    NSGraphicsContext *pEventGraphicsContext = nil; /* NSEvent::context is deprecated and said to always return nil. */
#else
    NSGraphicsContext *pEventGraphicsContext = [pEvent context];
#endif

    printf("%s%p: Type=%lu Modifiers=%08lx pWindow=%p #Wnd=%ld pGraphCtx=%p %s\n",
           pszPrefix, (void*)pEvent, (unsigned long)enmEventType, (unsigned long)fEventMask, (void*)pEventWindow,
           (long)iEventWindow, (void*)pEventGraphicsContext, darwinEventTypeName(enmEventType));

    /* Dump type specific info: */
    switch (enmEventType)
    {
        case VBOX_NSLeftMouseDown:
        case VBOX_NSLeftMouseUp:
        case VBOX_NSRightMouseDown:
        case VBOX_NSRightMouseUp:
        case VBOX_NSMouseMoved:

        case VBOX_NSLeftMouseDragged:
        case VBOX_NSRightMouseDragged:
        case VBOX_NSMouseEntered:
        case VBOX_NSMouseExited:
            break;

        case VBOX_NSKeyDown:
        case VBOX_NSKeyUp:
        {
            NSUInteger i;
            NSUInteger cch;
            NSString *pChars = [pEvent characters];
            NSString *pCharsIgnMod = [pEvent charactersIgnoringModifiers];
            BOOL fIsARepeat = [pEvent isARepeat];
            unsigned short KeyCode = [pEvent keyCode];

            printf("    KeyCode=%04x isARepeat=%d", KeyCode, fIsARepeat);
            if (pChars)
            {
                cch = [pChars length];
                printf(" characters={");
                for (i = 0; i < cch; i++)
                    printf(i == 0 ? "%02x" : ",%02x", [pChars characterAtIndex: i]);
                printf("}");
            }

            if (pCharsIgnMod)
            {
                cch = [pCharsIgnMod length];
                printf(" charactersIgnoringModifiers={");
                for (i = 0; i < cch; i++)
                    printf(i == 0 ? "%02x" : ",%02x", [pCharsIgnMod characterAtIndex: i]);
                printf("}");
            }
            printf("\n");
            break;
        }

        case VBOX_NSFlagsChanged:
        {
            NSUInteger fOddBits = VBOX_NSAlphaShiftKeyMask | VBOX_NSShiftKeyMask | VBOX_NSControlKeyMask | VBOX_NSAlternateKeyMask
                                | VBOX_NSCommandKeyMask | VBOX_NSNumericPadKeyMask | VBOX_NSHelpKeyMask | VBOX_NSFunctionKeyMask
                                | NX_DEVICELCTLKEYMASK | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK
                                | NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK | NX_DEVICELALTKEYMASK
                                | NX_DEVICERALTKEYMASK | NX_DEVICERCTLKEYMASK;

            printf("    KeyCode=%04x", (int)[pEvent keyCode]);
#define PRINT_MOD(cnst, nm) do { if (fEventMask & (cnst)) printf(" %s", #nm); } while (0)
            /* device-independent: */
            PRINT_MOD(VBOX_NSAlphaShiftKeyMask, "AlphaShift");
            PRINT_MOD(VBOX_NSShiftKeyMask, "Shift");
            PRINT_MOD(VBOX_NSControlKeyMask, "Ctrl");
            PRINT_MOD(VBOX_NSAlternateKeyMask, "Alt");
            PRINT_MOD(VBOX_NSCommandKeyMask, "Cmd");
            PRINT_MOD(VBOX_NSNumericPadKeyMask, "NumLock");
            PRINT_MOD(VBOX_NSHelpKeyMask, "Help");
            PRINT_MOD(VBOX_NSFunctionKeyMask, "Fn");
            /* device-dependent (sort of): */
            PRINT_MOD(NX_DEVICELCTLKEYMASK,   "$L-Ctrl");
            PRINT_MOD(NX_DEVICELSHIFTKEYMASK, "$L-Shift");
            PRINT_MOD(NX_DEVICERSHIFTKEYMASK, "$R-Shift");
            PRINT_MOD(NX_DEVICELCMDKEYMASK,   "$L-Cmd");
            PRINT_MOD(NX_DEVICERCMDKEYMASK,   "$R-Cmd");
            PRINT_MOD(NX_DEVICELALTKEYMASK,   "$L-Alt");
            PRINT_MOD(NX_DEVICERALTKEYMASK,   "$R-Alt");
            PRINT_MOD(NX_DEVICERCTLKEYMASK,   "$R-Ctrl");
#undef  PRINT_MOD

            fOddBits = fEventMask & ~fOddBits;
            if (fOddBits)
                printf(" fOddBits=%#08lx", (unsigned long)fOddBits);
#undef  KNOWN_BITS
            printf("\n");
            break;
        }

        case VBOX_NSAppKitDefined:
        case VBOX_NSSystemDefined:
        case VBOX_NSApplicationDefined:
        case VBOX_NSPeriodic:
        case VBOX_NSCursorUpdate:
        case VBOX_NSScrollWheel:
        case VBOX_NSTabletPoint:
        case VBOX_NSTabletProximity:
        case VBOX_NSOtherMouseDown:
        case VBOX_NSOtherMouseUp:
        case VBOX_NSOtherMouseDragged:
#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_5
        case NSEventTypeGesture:
        case NSEventTypeMagnify:
        case NSEventTypeSwipe:
        case NSEventTypeRotate:
        case NSEventTypeBeginGesture:
        case NSEventTypeEndGesture:
#endif
        default:
            printf(" Unknown!\n");
            break;
    }
}

void darwinPostStrippedMouseEvent(ConstNativeNSEventRef pEvent)
{
    /* Create and post new stripped event: */
    NSEvent *pNewEvent = [NSEvent mouseEventWithType:[pEvent type]
                                            location:[pEvent locationInWindow]
                                       modifierFlags:0
                                           timestamp:[pEvent timestamp] // [NSDate timeIntervalSinceReferenceDate] ?
                                        windowNumber:[pEvent windowNumber]
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
                                             context:nil /* NSEvent::context is deprecated and said to always return nil. */
#else
                                             context:[pEvent context]
#endif
                                         eventNumber:[pEvent eventNumber]
                                          clickCount:[pEvent clickCount]
                                            pressure:[pEvent pressure]];
    [NSApp postEvent:pNewEvent atStart:YES];
}

