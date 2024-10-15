/* $Id: keyboard-tables.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox/Frontends/Common - X11 keyboard driver translation tables.
 */

/* This code is originally from the Wine project. It was split off from
   keyboard-new.c.  (See the copyrights in that file). */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef ___VBox_keyboard_tables_h
# define ___VBox_keyboard_tables_h

#define MAIN_LEN 50
static const unsigned main_key_scan[MAIN_LEN] =
{
 /* `    1    2    3    4    5    6    7    8    9    0    -    = */
   0x29,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,
 /* q    w    e    r    t    y    u    i    o    p    [    ] */
   0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,
 /* a    s    d    f    g    h    j    k    l    ;    '    \ */
   0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x2B,
 /* z    x    c    v    b    n    m    ,    .    / */
   0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,
 /* 102nd key Brazilian key Yen */
   0x56,      0x73,         0x7d
};

/*** DEFINE YOUR NEW LANGUAGE-SPECIFIC MAPPINGS BELOW, SEE EXISTING TABLES */

/* order: Normal, Shift */
/* We recommend you write just what is guaranteed to be correct (i.e. what's
   written on the keycaps), not the bunch of special characters behind AltGr
   and Shift-AltGr if it can vary among different X servers */
/* Remember to also add your new table to the layout index table below! */

#include "keyboard-layouts.h"

/*** Layout table. Add your keyboard mappings to this list */
static const struct {
    const char *comment;
    const char (*key)[MAIN_LEN][2];
} main_key_tab[]={
#include "keyboard-list.h"
 {NULL, NULL} /* sentinel */
};

/** @note On the whole we use Microsoft's "USB HID to PS/2 Scan Code
 *    Translation Table" and
 *      http://www.win.tue.nl/~aeb/linux/kbd/scancodes-6.html
 *    as a reference for scan code numbers.
 *    Sun keyboards have eleven additional keys on the left-hand side.
 *    These keys never had PC scan codes assigned to them.  We map all X11
 *    keycodes which can correspond to these keys to the PC scan codes for
 *    F13 to F23 (as per Microsoft's translation table) and the USB keyboard
 *    code translates them back to the correct usage codes. */

/* Scan code table for non-character keys */
static const unsigned nonchar_key_scan[256] =
{
    /* unused */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF00 */
    /* special keys */
    0x0E, 0x0F, 0x00, /*?*/ 0, 0x00, 0x1C, 0x00, 0x00,           /* FF08 */
    0x00, 0x00, 0x00, 0x145, 0x46, 0x00, 0x00, 0x00,             /* FF10 */
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,              /* FF18 */
    /* Sun Menu, additional Japanese keys */
#ifdef sun
    0x15D, 0x79, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00,             /* FF20 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00,              /* FF28 */
#else
    0x15D, 0x00, 0x7B, 0x79, 0x00, 0x00, 0x00, 0x70,             /* FF20 */
    0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF28 */
#endif /* sun */
    /* additional Korean keys */
    0x00, 0xF2, 0x00, 0x00, 0xF1, 0x00, 0x00, 0x00,              /* FF30 */
    /* unused */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF38 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF40 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF48 */
    /* cursor keys */
    0x147, 0x14B, 0x148, 0x14D, 0x150, 0x149, 0x151, 0x14F,      /* FF50 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF58 */
    /* misc keys */
          /* Print Open  Insert       Undo  Again Menu */
                /* ->F17              ->F14 ->F22 */
    0x00, 0x137,   0x68, 0x152, 0x00, 0x65, 0x6d, 0x15D,         /* FF60 */
 /* Find  Stop  Help  Break */
 /* ->F19 ->F21 ->F23 */
    0x6a, 0x6c, 0x6e, 0x146, 0x00, 0x00, 0x00, 0x00,             /* FF68 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF70 */
    /* keypad keys */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x138, 0x45,             /* FF78 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FF80 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x11C, 0x00, 0x00,             /* FF88 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x4B, 0x48,              /* FF90 */
    0x4D, 0x50, 0x49, 0x51, 0x4F, 0x4C, 0x52, 0x53,              /* FF98 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FFA0 */
    0x00, 0x00, 0x37, 0x4E, 0x53, 0x4A, 0x7e, 0x135,             /* FFA8 */
    0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47,              /* FFB0 */
    0x48, 0x49, 0x00, 0x00, 0x00, 0x00,                          /* FFB8 */
    /* function keys (F1 to F12) */
                                        0x3B, 0x3C,
    0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,              /* FFC0 */
#ifdef sun
 /* Stop  Again F13   F14   F15   F16   F17   F18 */
 /* ->F21 ->F22 */
    0x6c, 0x6d, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,              /* FFC8 */
#else
 /* F11    F12 */
    0x57,  0x58,  0x64, 0x65, 0x66, 0x67, 0x68, 0x69,            /* FFC8 */
#endif
 /* F19   F20   F21   F22   F23   F24 */
    0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x76, 0x00, 0x00,              /* FFD0 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FFD8 */
    /* modifier keys */
    0x00, 0x2A, 0x36, 0x1D, 0x11D, 0x3A, 0x00, 0x15B,            /* FFE0 */
    0x15C, 0x38, 0x138, 0x15B, 0x15C, 0x00, 0x00, 0x00,          /* FFE8 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* FFF0 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x153              /* FFF8 */
};

/* This list was put together using /usr/include/X11/XF86keysym.h and
   the documents referenced above for scan code numbers.  It has not yet
   been extensively tested.  The scancodes are those used by MicroSoft
   keyboards. */
static const unsigned xfree86_vendor_key_scan[256] =
{
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF00 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF08 */
 /*    Vol-   Mute   Vol+   Play   Stop   Track- Track+ */
    0, 0x12e, 0x120, 0x130, 0x122, 0x124, 0x110, 0x119,         /* 1008FF10 */
 /* Home   E-mail    Search */
    0x132, 0x16c, 0, 0x165, 0, 0, 0, 0,                         /* 1008FF18 */
 /* Calndr PwrDown            Back   Forward */
    0x115, 0x15e, 0, 0, 0, 0, 0x16a, 0x169,                     /* 1008FF20 */
 /* Stop   Refresh Power Wake            Sleep */
    0x168, 0x167, 0x15e, 0x163, 0, 0, 0, 0x15f,                 /* 1008FF28 */
 /* Favrts Pause  Media  MyComp */
    0x166, 0x122, 0x16d, 0x16b, 0, 0, 0, 0,                     /* 1008FF30 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF38 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF40 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF48 */
 /* AppL   AppR         Calc      Close  Copy */
                                      /* ->F16 */
    0x109, 0x11e, 0, 0, 0x121, 0, 0x140, 0x67,                  /* 1008FF50 */
 /* Cut         Docmnts Excel */
 /* ->F20 */
    0x6b, 0, 0, 0x105, 0x114, 0, 0, 0,                         /* 1008FF58 */
 /*    LogOff */
    0, 0x116, 0, 0, 0, 0, 0, 0,                                 /* 1008FF60 */
 /*       OffcHm Open     Paste */
              /* ->F17    ->F18 */
    0, 0, 0x13c, 0x68, 0, 0x69, 0, 0,                           /* 1008FF68 */
 /*       Reply  Refresh         Save */
    0, 0, 0x141, 0x167, 0, 0, 0, 0x157,                         /* 1008FF70 */
 /* ScrlUp ScrlDn    Send   Spell        TaskPane */
    0x10b, 0x18b, 0, 0x143, 0x123, 0, 0, 0x13d,                 /* 1008FF78 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF80 */
 /*    Word */
    0, 0x113, 0, 0, 0, 0, 0, 0,                                 /* 1008FF88 */
 /* MailFwd MyPics MyMusic*/
    0x142, 0x164, 0x13c, 0, 0, 0, 0, 0,                         /* 1008FF90 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FF98 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFA0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFA8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFB0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFB8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFC0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFC8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFD0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFD8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFE0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFE8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1008FFF0 */
    0, 0, 0, 0, 0, 0, 0, 0                                      /* 1008FFF8 */
};

/* This list was put together using /usr/include/X11/Sunkeysym.h and
   comparing the scancodes produced by a Sun type 7 USB keyboard.  Note that
   Sun call F11 and F12 F36 and F37 respectively. */
static const unsigned sun_key_scan[256] =
{
 /* FAGrav, FACirc, FATild, FAAcut, FADiae, FACed */
    0,      0,      0,      0,      0,      0, 0, 0,            /* 1005FF00 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF08 */
 /* SunF36, SunF37 */
    0x57,   0x58, 0, 0, 0, 0, 0, 0,                             /* 1005FF10 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF18 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF20 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF28 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF30 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF38 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF40 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF48 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF50 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF58 */
 /* SysReq */
    0,     0, 0, 0, 0, 0, 0, 0,                                 /* 1005FF60 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF68 */
 /* Props Front Copy  Paste Cut    Power  Vol-   Mute */
 /* ->F13 ->F15 ->F16 ->F18 ->F20 */
    0x64, 0x66, 0x67, 0x69, 0x6b,  0x15e, 0x12e, 0x120,         /* 1005FF70 */
 /* Vol+ */
    0x130, 0, 0, 0, 0, 0, 0, 0,                                 /* 1005FF78 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF80 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF88 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF90 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FF98 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFA0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFA8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFB0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFB8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFC0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFC8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFD0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFD8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFE0 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFE8 */
    0, 0, 0, 0, 0, 0, 0, 0,                                     /* 1005FFF0 */
    0, 0, 0, 0, 0, 0, 0, 0                                      /* 1005FFF8 */
};

#include "xkbtoscan.h"

#endif /* !___VBox_keyboard_tables_h */

