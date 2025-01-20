/* $Id: XKeyboard-new.cpp 107972 2025-01-20 23:46:28Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Implementation of Linux-specific keyboard functions.
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
#include <QString>
#include <QStringList>

/* GUI includes: */
#include "UILoggingDefs.h"
#include "XKeyboard.h"

/* Other VBox includes: */
#include "VBox/VBoxKeyboard.h"

/* External includes: */
#include <X11/XKBlib.h>
#include <X11/keysym.h>


/* VBoxKeyboard uses the deprecated XKeycodeToKeysym(3) API, but uses it safely. */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static unsigned gfByLayoutOK = 1;
static unsigned gfByTypeOK = 1;
static unsigned gfByXkbOK = 1;

/** Prints a key to the release log in the format needed for the Wine layout tables. */
static void printKey(Display *pDisplay, int cKeys)
{
    bool fWasEscape = false;

    for (int i = 0; i < 2; ++i)
    {
        int iKeySym = XKeycodeToKeysym(pDisplay, cKeys, i);

        int iValue = iKeySym & 0xff;
        if ('\\' == iValue)
        {
            LogRel(("\\\\"));
        }
        else if ('"' == iValue)
        {
            LogRel(("\\\""));
        }
        else if ((iValue > 32) && (iValue < 127))
        {
            if (   fWasEscape
                && (   ((iValue >= '0') && (iValue <= '9'))
                    || ((iValue >= 'A') && (iValue <= 'F'))
                    || ((iValue >= 'a') && (iValue <= 'f'))))
            {
                LogRel(("\"\""));
            }
            LogRel(("%c", (char)iValue));
        }
        else
        {
            LogRel(("\\x%x", iValue));
            fWasEscape = true;
        }
    }
}

/** Dumps the keyboard layout to the release log. */
static void dumpLayout(Display *pDisplay)
{
    LogRel(("Your keyboard layout does not appear to be fully supported by\n"
            "VirtualBox. If you are experiencing keyboard problems this\n"
            "information may help us to resolve them.\n"
            "(Note: please tell us if you are using a custom layout.)\n\n"
            "The correct table for your layout is:\n"));
    /* First, build up a table of scan-to-key code mappings */
    unsigned scanToKeycode[512] = { 0 };
    int iMinKey, iMaxKey;
    XDisplayKeycodes(pDisplay, &iMinKey, &iMaxKey);
    for (int i = iMinKey; i < iMaxKey; ++i)
        scanToKeycode[X11DRV_KeyEvent(pDisplay, i)] = i;
    LogRel(("\""));
    printKey(pDisplay, scanToKeycode[0x29]); /* `~ */
    for (int i = 2; i <= 0xd; ++i) /* 1! - =+ */
    {
        LogRel(("\",\""));
        printKey(pDisplay, scanToKeycode[i]);
    }
    LogRel(("\",\n"));
    LogRel(("\""));
    printKey(pDisplay, scanToKeycode[0x10]); /* qQ */
    for (int i = 0x11; i <= 0x1b; ++i) /* wW - ]} */
    {
        LogRel(("\",\""));
        printKey(pDisplay, scanToKeycode[i]);
    }
    LogRel(("\",\n"));
    LogRel(("\""));
    printKey(pDisplay, scanToKeycode[0x1e]); /* aA */
    for (int i = 0x1f; i <= 0x28; ++i) /* sS - '" */
    {
        LogRel(("\",\""));
        printKey(pDisplay, scanToKeycode[i]);
    }
    LogRel(("\",\""));
    printKey(pDisplay, scanToKeycode[0x2b]); /* \| */
    LogRel(("\",\n"));
    LogRel(("\""));
    printKey(pDisplay, scanToKeycode[0x2c]); /* zZ */
    for (int i = 0x2d; i <= 0x35; ++i) /* xX - /? */
    {
        LogRel(("\",\""));
        printKey(pDisplay, scanToKeycode[i]);
    }
    LogRel(("\",\""));
    printKey(pDisplay, scanToKeycode[0x56]); /* The 102nd key */
    LogRel(("\",\""));
    printKey(pDisplay, scanToKeycode[0x73]); /* The Brazilian key */
    LogRel(("\",\""));
    printKey(pDisplay, scanToKeycode[0x7d]); /* The Yen key */
    LogRel(("\"\n\n"));
}

/** Dumps the keyboard type tables to the release log. */
static void dumpType(Display *pDisplay)
{
    LogRel(("Your keyboard type does not appear to be known to VirtualBox. If\n"
            "you are experiencing keyboard problems this information may help us\n"
            "to resolve them.  Please also provide information about what type\n"
            "of keyboard you have and whether you are using a remote X server or\n"
            "something similar.\n\n"
            "The tables for your keyboard are:\n"));
    for (unsigned i = 0; i < 256; ++i)
    {
        LogRel(("0x%x", X11DRV_KeyEvent(pDisplay, i)));
        if (i < 255)
            LogRel((", "));
        if (15 == (i % 16))
            LogRel(("\n"));
    }
    LogRel(("and\n"));
    LogRel(("NULL, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,\n0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
            XKeysymToKeycode(pDisplay, XK_Control_L),
            XKeysymToKeycode(pDisplay, XK_Shift_L),
            XKeysymToKeycode(pDisplay, XK_Caps_Lock),
            XKeysymToKeycode(pDisplay, XK_Tab),
            XKeysymToKeycode(pDisplay, XK_Escape),
            XKeysymToKeycode(pDisplay, XK_Return),
            XKeysymToKeycode(pDisplay, XK_Up),
            XKeysymToKeycode(pDisplay, XK_Down),
            XKeysymToKeycode(pDisplay, XK_Left),
            XKeysymToKeycode(pDisplay, XK_Right),
            XKeysymToKeycode(pDisplay, XK_F1),
            XKeysymToKeycode(pDisplay, XK_F2),
            XKeysymToKeycode(pDisplay, XK_F3),
            XKeysymToKeycode(pDisplay, XK_F4),
            XKeysymToKeycode(pDisplay, XK_F5),
            XKeysymToKeycode(pDisplay, XK_F6),
            XKeysymToKeycode(pDisplay, XK_F7),
            XKeysymToKeycode(pDisplay, XK_F8)));
}

/** Builds a table mapping the X server's scan codes to PC keyboard scan codes.
  * The logic of the function is that while the X server may be using a different
  * set of scan codes (if for example it is running on a non-PC machine), the
  * keyboard layout should be similar to a PC layout.  So we look at the symbols
  * attached to each key on the X server, find the PC layout which is closest to
  * it and remember the mappings. */
bool initXKeyboard(Display *pDisplay, int (*remapScancodes)[2])
{
    X11DRV_InitKeyboard(pDisplay, &gfByLayoutOK, &gfByTypeOK, &gfByXkbOK, remapScancodes);

    /* It will almost always work to some extent.. */
    return true;
}

void initMappedX11Keyboard(Display *pDisplay, const QString &remapScancodes)
{
    /* Initialize X11 keyboard including the remapping specified in the
     * global property GUI/RemapScancodes. This property is a string of
     * comma-separated x=y pairs, where x is the X11 keycode and y is the
     * keyboard scancode that is emitted when the key attached to the X11
     * keycode is pressed. */

    int (*scancodes)[2] = NULL;
    int (*scancodesTail)[2] = NULL;

    if (remapScancodes != QString())
    {
        const QStringList tuples = remapScancodes.split(",", Qt::SkipEmptyParts);
        scancodes = scancodesTail = new int [tuples.size()+1][2];
        for (int i = 0; i < tuples.size(); ++i)
        {
            QStringList keyc2scan = tuples.at(i).split("=");
            (*scancodesTail)[0] = keyc2scan.at(0).toUInt();
            (*scancodesTail)[1] = keyc2scan.at(1).toUInt();
            /* Do not advance on (ignore) identity mappings as this
             * is the stop signal to initXKeyboard and friends: */
            if ((*scancodesTail)[0] != (*scancodesTail)[1])
                ++scancodesTail;
        }
        (*scancodesTail)[0] = (*scancodesTail)[1] = 0;
    }

    /* Initialize the X keyboard subsystem: */
    initXKeyboard(pDisplay, scancodes);

    if (scancodes)
        delete scancodes;
}

unsigned handleXKeyEvent(Display *pDisplay, unsigned int iDetail)
{
    /* Call the WINE event handler: */
    unsigned iKey = X11DRV_KeyEvent(pDisplay, iDetail);
    LogRel3(("VBoxKeyboard: converting keycode %d to scancode %s0x%x\n",
             iDetail, iKey > 0x100 ? "0xe0 " : "", iKey & 0xff));
    return iKey;
}

void doXKeyboardLogging(Display *pDisplay)
{
    if (((1 == gfByTypeOK) || (1 == gfByXkbOK)) && (gfByLayoutOK != 1))
        dumpLayout(pDisplay);
    if (((1 == gfByLayoutOK) || (1 == gfByXkbOK)) && (gfByTypeOK != 1))
        dumpType(pDisplay);
    if ((gfByLayoutOK != 1) && (gfByTypeOK != 1) && (gfByXkbOK != 1))
    {
        LogRel(("Failed to recognize the keyboard mapping or to guess it based on\n"
                "the keyboard layout.  It is very likely that some keys will not\n"
                "work correctly in the guest.  If this is the case, please submit\n"
                "a bug report, giving us information about your keyboard type,\n"
                "its layout and other relevant information such as whether you\n"
                "are using a remote X server or something similar. \n"));
        unsigned *keyc2scan = X11DRV_getKeyc2scan();

        LogRel(("The keycode-to-scancode table is: %d=%d",0,keyc2scan[0]));
        for (int i = 1; i < 256; i++)
            LogRel((",%d=%d",i,keyc2scan[i]));
        LogRel(("\n"));
    }
    LogRel(("X Server details: vendor: %s, release: %d, protocol version: %d.%d, display string: %s\n",
            ServerVendor(pDisplay), VendorRelease(pDisplay), ProtocolVersion(pDisplay),
            ProtocolRevision(pDisplay), DisplayString(pDisplay)));
    LogRel(("Using %s for keycode to scan code conversion\n",
              gfByXkbOK ? "XKB"
            : gfByTypeOK ? "known keycode mapping"
            : "host keyboard layout detection"));
}

unsigned long wrapXkbKeycodeToKeysym(Display *pDisplay, unsigned char cCode,
                                     unsigned int cGroup, unsigned int cIndex)
{
    KeySym cSym = XkbKeycodeToKeysym(pDisplay, cCode, cGroup, cIndex);
    if (cSym != NoSymbol)
        return cSym;
    return XKeycodeToKeysym(pDisplay, cCode, cGroup * 2 + cIndex % 2);
}

