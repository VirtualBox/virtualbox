/* $Id: DevPS2K.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PS2K - PS/2 keyboard emulation.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

/*
 * References:
 *
 * IBM PS/2 Technical Reference, Keyboards (101- and 102-Key), 1990
 * Keyboard Scan Code Specification, Microsoft, 2000
 *
 * Notes:
 *  - The keyboard never sends partial scan-code sequences; if there isn't enough
 *    room left in the buffer for the entire sequence, the keystroke is discarded
 *    and an overrun code is sent instead.
 *  - Command responses do not disturb stored keystrokes and always have priority.
 *  - Caps Lock and Scroll Lock are normal keys from the keyboard's point of view.
 *    However, Num Lock is not and the keyboard internally tracks its state.
 *  - The way Print Screen works in scan set 1/2 is totally insane.
 *  - A PS/2 keyboard can send at most 1,000 to 1,500 bytes per second. There is
 *    software which relies on that fact and assumes that a scan code can be
 *    read twice before the next scan code comes in.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_DEV_KBD
#include <VBox/vmm/pdmdev.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include "VBoxDD.h"
#define IN_PS2K
#include "DevPS2.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name Keyboard commands sent by the system.
 * @{ */
#define KCMD_LEDS           0xED
#define KCMD_ECHO           0xEE
#define KCMD_INVALID_1      0xEF
#define KCMD_SCANSET        0xF0
#define KCMD_INVALID_2      0xF1
#define KCMD_READ_ID        0xF2
#define KCMD_RATE_DELAY     0xF3
#define KCMD_ENABLE         0xF4
#define KCMD_DFLT_DISABLE   0xF5
#define KCMD_SET_DEFAULT    0xF6
#define KCMD_ALL_TYPEMATIC  0xF7
#define KCMD_ALL_MK_BRK     0xF8
#define KCMD_ALL_MAKE       0xF9
#define KCMD_ALL_TMB        0xFA
#define KCMD_TYPE_MATIC     0xFB
#define KCMD_TYPE_MK_BRK    0xFC
#define KCMD_TYPE_MAKE      0xFD
#define KCMD_RESEND         0xFE
#define KCMD_RESET          0xFF
/** @} */

/** @name Keyboard responses sent to the system.
 * @{ */
#define KRSP_ID1            0xAB
#define KRSP_ID2            0x83
#define KRSP_BAT_OK         0xAA
#define KRSP_BAT_FAIL       0xFC    /* Also a 'release keys' signal. */
#define KRSP_ECHO           0xEE
#define KRSP_ACK            0xFA
#define KRSP_RESEND         0xFE
/** @} */

/** @name Modifier key states. Sorted in USB HID code order.
 * @{ */
#define MOD_LCTRL           0x01
#define MOD_LSHIFT          0x02
#define MOD_LALT            0x04
#define MOD_LGUI            0x08
#define MOD_RCTRL           0x10
#define MOD_RSHIFT          0x20
#define MOD_RALT            0x40
#define MOD_RGUI            0x80
/** @} */

/* Default typematic value. */
#define KBD_DFL_RATE_DELAY  0x2B

/* Input throttling delay in milliseconds. */
#define KBD_THROTTLE_DELAY  1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* Key type flags. */
#define KF_E0        0x01    /* E0 prefix. */
#define KF_NB        0x02    /* No break code. */
#define KF_GK        0x04    /* Gray navigation key. */
#define KF_PS        0x08    /* Print Screen key. */
#define KF_PB        0x10    /* Pause/Break key. */
#define KF_NL        0x20    /* Num Lock key. */
#define KF_NS        0x40    /* NumPad '/' key. */

/* Scan Set 3 typematic defaults. */
#define T_U          0x00    /* Unknown value. */
#define T_T          0x01    /* Key is typematic. */
#define T_M          0x02    /* Key is make only. */
#define T_B          0x04    /* Key is make/break. */

/* Special key values. */
#define NONE         0x93    /* No PS/2 scan code returned. */
#define UNAS         0x94    /* No PS/2 scan assigned to key. */
#define RSVD         0x95    /* Reserved, do not use. */
#define UNKN         0x96    /* Translation unknown. */

/* Key definition structure. */
typedef struct {
    uint8_t makeS1;      /* Set 1 make code. */
    uint8_t makeS2;      /* Set 2 make code. */
    uint8_t makeS3;      /* Set 3 make code. */
    uint8_t keyFlags;    /* Key flags. */
    uint8_t keyMatic;    /* Set 3 typematic default. */
} key_def;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/* USB to PS/2 conversion table for regular keys (HID Usage Page 7). */
static const   key_def   aPS2Keys[] = {
    /* 00 */ {NONE, NONE, NONE, KF_NB, T_U }, /* Key N/A: No Event */
    /* 01 */ {0xFF, 0x00, 0x00, KF_NB, T_U }, /* Key N/A: Overrun Error */
    /* 02 */ {0xFC, 0xFC, 0xFC, KF_NB, T_U }, /* Key N/A: POST Fail */
    /* 03 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key N/A: ErrorUndefined */
    /* 04 */ {0x1E, 0x1C, 0x1C,     0, T_T }, /* Key  31: a A */
    /* 05 */ {0x30, 0x32, 0x32,     0, T_T }, /* Key  50: b B */
    /* 06 */ {0x2E, 0x21, 0x21,     0, T_T }, /* Key  48: c C */
    /* 07 */ {0x20, 0x23, 0x23,     0, T_T }, /* Key  33: d D */
    /* 08 */ {0x12, 0x24, 0x24,     0, T_T }, /* Key  19: e E */
    /* 09 */ {0x21, 0x2B, 0x2B,     0, T_T }, /* Key  34: f F */
    /* 0A */ {0x22, 0x34, 0x34,     0, T_T }, /* Key  35: g G */
    /* 0B */ {0x23, 0x33, 0x33,     0, T_T }, /* Key  36: h H */
    /* 0C */ {0x17, 0x43, 0x43,     0, T_T }, /* Key  24: i I */
    /* 0D */ {0x24, 0x3B, 0x3B,     0, T_T }, /* Key  37: j J */
    /* 0E */ {0x25, 0x42, 0x42,     0, T_T }, /* Key  38: k K */
    /* 0F */ {0x26, 0x4B, 0x4B,     0, T_T }, /* Key  39: l L */
    /* 10 */ {0x32, 0x3A, 0x3A,     0, T_T }, /* Key  52: m M */
    /* 11 */ {0x31, 0x31, 0x31,     0, T_T }, /* Key  51: n N */
    /* 12 */ {0x18, 0x44, 0x44,     0, T_T }, /* Key  25: o O */
    /* 13 */ {0x19, 0x4D, 0x4D,     0, T_T }, /* Key  26: p P */
    /* 14 */ {0x10, 0x15, 0x15,     0, T_T }, /* Key  17: q Q */
    /* 15 */ {0x13, 0x2D, 0x2D,     0, T_T }, /* Key  20: r R */
    /* 16 */ {0x1F, 0x1B, 0x1B,     0, T_T }, /* Key  32: s S */
    /* 17 */ {0x14, 0x2C, 0x2C,     0, T_T }, /* Key  21: t T */
    /* 18 */ {0x16, 0x3C, 0x3C,     0, T_T }, /* Key  23: u U */
    /* 19 */ {0x2F, 0x2A, 0x2A,     0, T_T }, /* Key  49: v V */
    /* 1A */ {0x11, 0x1D, 0x1D,     0, T_T }, /* Key  18: w W */
    /* 1B */ {0x2D, 0x22, 0x22,     0, T_T }, /* Key  47: x X */
    /* 1C */ {0x15, 0x35, 0x35,     0, T_T }, /* Key  22: y Y */
    /* 1D */ {0x2C, 0x1A, 0x1A,     0, T_T }, /* Key  46: z Z */
    /* 1E */ {0x02, 0x16, 0x16,     0, T_T }, /* Key   2: 1 ! */
    /* 1F */ {0x03, 0x1E, 0x1E,     0, T_T }, /* Key   3: 2 @ */
    /* 20 */ {0x04, 0x26, 0x26,     0, T_T }, /* Key   4: 3 # */
    /* 21 */ {0x05, 0x25, 0x25,     0, T_T }, /* Key   5: 4 $ */
    /* 22 */ {0x06, 0x2E, 0x2E,     0, T_T }, /* Key   6: 5 % */
    /* 23 */ {0x07, 0x36, 0x36,     0, T_T }, /* Key   7: 6 ^ */
    /* 24 */ {0x08, 0x3D, 0x3D,     0, T_T }, /* Key   8: 7 & */
    /* 25 */ {0x09, 0x3E, 0x3E,     0, T_T }, /* Key   9: 8 * */
    /* 26 */ {0x0A, 0x46, 0x46,     0, T_T }, /* Key  10: 9 ( */
    /* 27 */ {0x0B, 0x45, 0x45,     0, T_T }, /* Key  11: 0 ) */
    /* 28 */ {0x1C, 0x5A, 0x5A,     0, T_T }, /* Key  43: Return */
    /* 29 */ {0x01, 0x76, 0x08,     0, T_M }, /* Key 110: Escape */
    /* 2A */ {0x0E, 0x66, 0x66,     0, T_T }, /* Key  15: Backspace */
    /* 2B */ {0x0F, 0x0D, 0x0D,     0, T_T }, /* Key  16: Tab */
    /* 2C */ {0x39, 0x29, 0x29,     0, T_T }, /* Key  61: Space */
    /* 2D */ {0x0C, 0x4E, 0x4E,     0, T_T }, /* Key  12: - _ */
    /* 2E */ {0x0D, 0x55, 0x55,     0, T_T }, /* Key  13: = + */
    /* 2F */ {0x1A, 0x54, 0x54,     0, T_T }, /* Key  27: [ { */
    /* 30 */ {0x1B, 0x5B, 0x5B,     0, T_T }, /* Key  28: ] } */
    /* 31 */ {0x2B, 0x5D, 0x5C,     0, T_T }, /* Key  29: \ | */
    /* 32 */ {0x2B, 0x5D, 0x5D,     0, T_T }, /* Key  42: Europe 1 (Note 2) */
    /* 33 */ {0x27, 0x4C, 0x4C,     0, T_T }, /* Key  40: ; : */
    /* 34 */ {0x28, 0x52, 0x52,     0, T_T }, /* Key  41: ' " */
    /* 35 */ {0x29, 0x0E, 0x0E,     0, T_T }, /* Key   1: ` ~ */
    /* 36 */ {0x33, 0x41, 0x41,     0, T_T }, /* Key  53: , < */
    /* 37 */ {0x34, 0x49, 0x49,     0, T_T }, /* Key  54: . > */
    /* 38 */ {0x35, 0x4A, 0x4A,     0, T_T }, /* Key  55: / ? */
    /* 39 */ {0x3A, 0x58, 0x14,     0, T_B }, /* Key  30: Caps Lock */
    /* 3A */ {0x3B, 0x05, 0x07,     0, T_M }, /* Key 112: F1 */
    /* 3B */ {0x3C, 0x06, 0x0F,     0, T_M }, /* Key 113: F2 */
    /* 3C */ {0x3D, 0x04, 0x17,     0, T_M }, /* Key 114: F3 */
    /* 3D */ {0x3E, 0x0C, 0x1F,     0, T_M }, /* Key 115: F4 */
    /* 3E */ {0x3F, 0x03, 0x27,     0, T_M }, /* Key 116: F5 */
    /* 3F */ {0x40, 0x0B, 0x2F,     0, T_M }, /* Key 117: F6 */
    /* 40 */ {0x41, 0x83, 0x37,     0, T_M }, /* Key 118: F7 */
    /* 41 */ {0x42, 0x0A, 0x3F,     0, T_M }, /* Key 119: F8 */
    /* 42 */ {0x43, 0x01, 0x47,     0, T_M }, /* Key 120: F9 */
    /* 43 */ {0x44, 0x09, 0x4F,     0, T_M }, /* Key 121: F10 */
    /* 44 */ {0x57, 0x78, 0x56,     0, T_M }, /* Key 122: F11 */
    /* 45 */ {0x58, 0x07, 0x5E,     0, T_M }, /* Key 123: F12 */
    /* 46 */ {0x37, 0x7C, 0x57, KF_PS, T_M }, /* Key 124: Print Screen (Note 1) */
    /* 47 */ {0x46, 0x7E, 0x5F,     0, T_M }, /* Key 125: Scroll Lock */
    /* 48 */ {RSVD, RSVD, RSVD, KF_PB, T_M }, /* Key 126: Break (Ctrl-Pause) */
    /* 49 */ {0x52, 0x70, 0x67, KF_GK, T_M }, /* Key  75: Insert (Note 1) */
    /* 4A */ {0x47, 0x6C, 0x6E, KF_GK, T_M }, /* Key  80: Home (Note 1) */
    /* 4B */ {0x49, 0x7D, 0x6F, KF_GK, T_M }, /* Key  85: Page Up (Note 1) */
    /* 4C */ {0x53, 0x71, 0x64, KF_GK, T_T }, /* Key  76: Delete (Note 1) */
    /* 4D */ {0x4F, 0x69, 0x65, KF_GK, T_M }, /* Key  81: End (Note 1) */
    /* 4E */ {0x51, 0x7A, 0x6D, KF_GK, T_M }, /* Key  86: Page Down (Note 1) */
    /* 4F */ {0x4D, 0x74, 0x6A, KF_GK, T_T }, /* Key  89: Right Arrow (Note 1) */
    /* 50 */ {0x4B, 0x6B, 0x61, KF_GK, T_T }, /* Key  79: Left Arrow (Note 1) */
    /* 51 */ {0x50, 0x72, 0x60, KF_GK, T_T }, /* Key  84: Down Arrow (Note 1) */
    /* 52 */ {0x48, 0x75, 0x63, KF_GK, T_T }, /* Key  83: Up Arrow (Note 1) */
    /* 53 */ {0x45, 0x77, 0x76, KF_NL, T_M }, /* Key  90: Num Lock */
    /* 54 */ {0x35, 0x4A, 0x77, KF_NS, T_M }, /* Key  95: Keypad / (Note 1) */
    /* 55 */ {0x37, 0x7C, 0x7E,     0, T_M }, /* Key 100: Keypad * */
    /* 56 */ {0x4A, 0x7B, 0x84,     0, T_M }, /* Key 105: Keypad - */
    /* 57 */ {0x4E, 0x79, 0x7C,     0, T_T }, /* Key 106: Keypad + */
    /* 58 */ {0x1C, 0x5A, 0x79, KF_E0, T_M }, /* Key 108: Keypad Enter */
    /* 59 */ {0x4F, 0x69, 0x69,     0, T_M }, /* Key  93: Keypad 1 End */
    /* 5A */ {0x50, 0x72, 0x72,     0, T_M }, /* Key  98: Keypad 2 Down */
    /* 5B */ {0x51, 0x7A, 0x7A,     0, T_M }, /* Key 103: Keypad 3 PageDn */
    /* 5C */ {0x4B, 0x6B, 0x6B,     0, T_M }, /* Key  92: Keypad 4 Left */
    /* 5D */ {0x4C, 0x73, 0x73,     0, T_M }, /* Key  97: Keypad 5 */
    /* 5E */ {0x4D, 0x74, 0x74,     0, T_M }, /* Key 102: Keypad 6 Right */
    /* 5F */ {0x47, 0x6C, 0x6C,     0, T_M }, /* Key  91: Keypad 7 Home */
    /* 60 */ {0x48, 0x75, 0x75,     0, T_M }, /* Key  96: Keypad 8 Up */
    /* 61 */ {0x49, 0x7D, 0x7D,     0, T_M }, /* Key 101: Keypad 9 PageUp */
    /* 62 */ {0x52, 0x70, 0x70,     0, T_M }, /* Key  99: Keypad 0 Insert */
    /* 63 */ {0x53, 0x71, 0x71,     0, T_M }, /* Key 104: Keypad . Delete */
    /* 64 */ {0x56, 0x61, 0x13,     0, T_T }, /* Key  45: Europe 2 (Note 2) */
    /* 65 */ {0x5D, 0x2F, UNKN, KF_E0, T_U }, /* Key 129: App */
    /* 66 */ {0x5E, 0x37, UNKN, KF_E0, T_U }, /* Key Unk: Keyboard Power */
    /* 67 */ {0x59, 0x0F, UNKN,     0, T_U }, /* Key Unk: Keypad = */
    /* 68 */ {0x64, 0x08, UNKN,     0, T_U }, /* Key Unk: F13 */
    /* 69 */ {0x65, 0x10, UNKN,     0, T_U }, /* Key Unk: F14 */
    /* 6A */ {0x66, 0x18, UNKN,     0, T_U }, /* Key Unk: F15 */
    /* 6B */ {0x67, 0x20, UNKN,     0, T_U }, /* Key Unk: F16 */
    /* 6C */ {0x68, 0x28, UNKN,     0, T_U }, /* Key Unk: F17 */
    /* 6D */ {0x69, 0x30, UNKN,     0, T_U }, /* Key Unk: F18 */
    /* 6E */ {0x6A, 0x38, UNKN,     0, T_U }, /* Key Unk: F19 */
    /* 6F */ {0x6B, 0x40, UNKN,     0, T_U }, /* Key Unk: F20 */
    /* 70 */ {0x6C, 0x48, UNKN,     0, T_U }, /* Key Unk: F21 */
    /* 71 */ {0x6D, 0x50, UNKN,     0, T_U }, /* Key Unk: F22 */
    /* 72 */ {0x6E, 0x57, UNKN,     0, T_U }, /* Key Unk: F23 */
    /* 73 */ {0x76, 0x5F, UNKN,     0, T_U }, /* Key Unk: F24 */
    /* 74 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Execute */
    /* 75 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Help */
    /* 76 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Menu */
    /* 77 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Select */
    /* 78 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Stop */
    /* 79 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Again */
    /* 7A */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Undo */
    /* 7B */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Cut */
    /* 7C */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Copy */
    /* 7D */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Paste */
    /* 7E */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Find */
    /* 7F */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Mute */
    /* 80 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Volume Up */
    /* 81 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Volume Dn */
    /* 82 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Locking Caps Lock */
    /* 83 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Locking Num Lock */
    /* 84 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Locking Scroll Lock */
    /* 85 */ {0x7E, 0x6D, UNKN,     0, T_U }, /* Key Unk: Keypad , (Brazilian Keypad .) */
    /* 86 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Equal Sign */
    /* 87 */ {0x73, 0x51, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl 1 (Ro) */
    /* 88 */ {0x70, 0x13, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl2 (K'kana/H'gana) */
    /* 89 */ {0x7D, 0x6A, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl 2 (Yen) */
    /* 8A */ {0x79, 0x64, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl 4 (Henkan) */
    /* 8B */ {0x7B, 0x67, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl 5 (Muhenkan) */
    /* 8C */ {0x5C, 0x27, UNKN,     0, T_U }, /* Key Unk: Keyboard Intl 6 (PC9800 Pad ,) */
    /* 8D */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Intl 7 */
    /* 8E */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Intl 8 */
    /* 8F */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Intl 9 */
    /* 90 */ {0xF2, 0xF2, UNKN, KF_NB, T_U }, /* Key Unk: Keyboard Lang 1 (Hang'l/Engl) */
    /* 91 */ {0xF1, 0xF1, UNKN, KF_NB, T_U }, /* Key Unk: Keyboard Lang 2 (Hanja) */
    /* 92 */ {0x78, 0x63, UNKN,     0, T_U }, /* Key Unk: Keyboard Lang 3 (Katakana) */
    /* 93 */ {0x77, 0x62, UNKN,     0, T_U }, /* Key Unk: Keyboard Lang 4 (Hiragana) */
    /* 94 */ {0x76, 0x5F, UNKN,     0, T_U }, /* Key Unk: Keyboard Lang 5 (Zen/Han) */
    /* 95 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Lang 6 */
    /* 96 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Lang 7 */
    /* 97 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Lang 8 */
    /* 98 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Lang 9 */
    /* 99 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Alternate Erase */
    /* 9A */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard SysReq/Attention (Note 3) */
    /* 9B */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Cancel */
    /* 9C */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Clear */
    /* 9D */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Prior */
    /* 9E */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Return */
    /* 9F */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Separator */
    /* A0 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Out */
    /* A1 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Oper */
    /* A2 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard Clear/Again */
    /* A3 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard CrSel/Props */
    /* A4 */ {UNAS, UNAS, UNAS,     0, T_U }, /* Key Unk: Keyboard ExSel */
};

/*
 * Note 1: The behavior of these keys depends on the state of modifier keys
 * at the time the key was pressed.
 *
 * Note 2: The key label depends on the national version of the keyboard.
 *
 * Note 3: Certain keys which have their own PS/2 scancodes do not exist on
 * USB keyboards; the SysReq key is an example. The SysReq key scancode needs
 * to be translated to the Print Screen HID usage code. The HID usage to PS/2
 * scancode conversion then generates the correct sequence depending on the
 * keyboard state.
 */

/* USB to PS/2 conversion table for modifier keys (HID Usage Page 7). */
static const   key_def   aPS2ModKeys[] = {
    /* E0 */ {0x1D, 0x14, 0x11,     0, T_B }, /* Key  58: Left Control */
    /* E1 */ {0x2A, 0x12, 0x12,     0, T_B }, /* Key  44: Left Shift */
    /* E2 */ {0x38, 0x11, 0x19,     0, T_B }, /* Key  60: Left Alt */
    /* E3 */ {0x5B, 0x1F, UNKN, KF_E0, T_U }, /* Key 127: Left GUI */
    /* E4 */ {0x1D, 0x14, 0x58, KF_E0, T_M }, /* Key  64: Right Control */
    /* E5 */ {0x36, 0x59, 0x59,     0, T_B }, /* Key  57: Right Shift */
    /* E6 */ {0x38, 0x11, 0x39, KF_E0, T_M }, /* Key  62: Right Alt */
    /* E7 */ {0x5C, 0x27, UNKN, KF_E0, T_U }, /* Key 128: Right GUI */
};

/* Extended key definition for sparse mapping. */
typedef struct {
    uint16_t    usageId;
    key_def     kdef;
} ext_key_def;


/* USB to PS/2 conversion table for consumer control keys (HID Usage Page 12). */
/* This usage page is very sparse so we'll just search through it. */
static const   ext_key_def  aPS2CCKeys[] = {
    {0x00B5, {0x19, 0x4D, UNKN, KF_E0, T_U}},   /* Scan Next Track */
    {0x00B6, {0x10, 0x15, UNKN, KF_E0, T_U}},   /* Scan Previous Track */
    {0x00B7, {0x24, 0x3B, UNKN, KF_E0, T_U}},   /* Stop */
    {0x00CD, {0x22, 0x34, UNKN, KF_E0, T_U}},   /* Play/Pause */
    {0x00E2, {0x20, 0x23, UNKN, KF_E0, T_U}},   /* Mute */
    {0x00E5, {UNAS, UNAS, UNAS,     0, T_U}},   /* Bass Boost */
    {0x00E7, {UNAS, UNAS, UNAS,     0, T_U}},   /* Loudness */
    {0x00E9, {0x30, 0x32, UNKN, KF_E0, T_U}},   /* Volume Up */
    {0x00EA, {0x2E, 0x21, UNKN, KF_E0, T_U}},   /* Volume Down */
    {0x0152, {UNAS, UNAS, UNAS,     0, T_U}},   /* Bass Up */
    {0x0153, {UNAS, UNAS, UNAS,     0, T_U}},   /* Bass Down */
    {0x0154, {UNAS, UNAS, UNAS,     0, T_U}},   /* Treble Up */
    {0x0155, {UNAS, UNAS, UNAS,     0, T_U}},   /* Treble Down */
    {0x0183, {0x6D, 0x50, UNKN, KF_E0, T_U}},   /* Media Select  */
    {0x018A, {0x6C, 0x48, UNKN, KF_E0, T_U}},   /* Mail */
    {0x0192, {0x21, 0x2B, UNKN, KF_E0, T_U}},   /* Calculator */
    {0x0194, {0x6B, 0x40, UNKN, KF_E0, T_U}},   /* My Computer */
    {0x0221, {0x65, 0x10, UNKN, KF_E0, T_U}},   /* WWW Search */
    {0x0223, {0x32, 0x3A, UNKN, KF_E0, T_U}},   /* WWW Home */
    {0x0224, {0x6A, 0x38, UNKN, KF_E0, T_U}},   /* WWW Back */
    {0x0225, {0x69, 0x30, UNKN, KF_E0, T_U}},   /* WWW Forward */
    {0x0226, {0x68, 0x28, UNKN, KF_E0, T_U}},   /* WWW Stop */
    {0x0227, {0x67, 0x20, UNKN, KF_E0, T_U}},   /* WWW Refresh */
    {0x022A, {0x66, 0x18, UNKN, KF_E0, T_U}},   /* WWW Favorites */
};

/* USB to PS/2 conversion table for Generic Desktop Control keys (HID Usage Page 1). */
/* This usage page is tiny. */
static const   ext_key_def  aPS2DCKeys[] = {
    {0x81,   {0x5E, 0x37, UNKN, KF_E0, T_U}},   /* System Power */
    {0x82,   {0x5F, 0x3F, UNKN, KF_E0, T_U}},   /* System Sleep */
    {0x83,   {0x63, 0x5E, UNKN, KF_E0, T_U}},   /* System Wake */
};

/* We somehow need to keep track of depressed keys. To keep the array size
 * under control, and because the number of defined keys isn't massive, we'd
 * like to use an 8-bit index into the array.
 * For the main USB HID usage page 7 (keyboard), we deal with 8-bit HID codes
 * in the range from 0 to 0xE7, and use the HID codes directly.
 * There's a convenient gap in the 0xA5-0xDF range. We use that to stuff the
 * USB HID usage page 12 (consumer control) into the gap starting at 0xC0;
 * the consumer control codes are from 0xB5 to 0x22A, but very sparse, with
 * only 24 codes defined. We use the aPS2CCKeys array to generate our own
 * code in the 0xC0-0xD7 range.
 * For the tiny USB HID usage page 1 (generic desktop system) we use a similar
 * approach, translating these to codes 0xB0 to 0xB2.
 */

#define PS2K_PAGE_DC_START      0xb0
#define PS2K_PAGE_DC_END        (PS2K_PAGE_DC_START + RT_ELEMENTS(aPS2DCKeys) - 1)
#define PS2K_PAGE_CC_START      0xc0
#define PS2K_PAGE_CC_END        (PS2K_PAGE_CC_START + RT_ELEMENTS(aPS2CCKeys) - 1)

AssertCompile(RT_ELEMENTS(aPS2CCKeys) <= 0x20); /* Must fit between 0xC0-0xDF. */
AssertCompile(RT_ELEMENTS(aPS2DCKeys) <= 0x10); /* Must fit between 0xB0-0xBF. */

#endif /* IN_RING3 */

#ifdef IN_RING3

/**
 * Add a null-terminated byte sequence to a queue if there is enough room.
 *
 * @param   pQueue      Pointer to the queue.
 * @param   pStr        Pointer to the bytes to store.
 * @param   cbReserve   Number of bytes that must still remain available in
 *                      queue.
 * @return  VBox status/error code.
 */
static int ps2kR3InsertStrQueue(KbdKeyQ *pQueue, const uint8_t *pStr, uint32_t cbReserve)
{
    /* Check if queue has enough room. */
    size_t const cbStr = (uint32_t)strlen((const char *)pStr);
    uint32_t     cUsed = RT_MIN(pQueue->Hdr.cUsed, RT_ELEMENTS(pQueue->abQueue));
    if (cUsed + cbReserve + cbStr >= RT_ELEMENTS(pQueue->abQueue))
    {
        LogRelFlowFunc(("queue %p (KbdKeyQ) full (%u entries, want room for %u), cannot insert %zu entries\n",
                        pQueue, cUsed, cbReserve, cbStr));
        return VERR_BUFFER_OVERFLOW;
    }

    /* Insert byte sequence and update circular buffer write position. */
    uint32_t wpos = pQueue->Hdr.wpos % RT_ELEMENTS(pQueue->abQueue);
    for (size_t i = 0; i < cbStr; i++)
    {
        pQueue->abQueue[wpos] = pStr[i];
        wpos += 1;
        if (wpos < RT_ELEMENTS(pQueue->abQueue))
        { /* likely */ }
        else
            wpos = 0; /* Roll over. */
    }

    pQueue->Hdr.wpos  = wpos;
    pQueue->Hdr.cUsed = cUsed + (uint32_t)cbStr;

    LogRelFlowFunc(("inserted %u bytes into queue %p (KbdKeyQ)\n", cbStr, pQueue));
    return VINF_SUCCESS;
}

/**
 * Notify listener about LEDs state change.
 *
 * @param   pThisCC         The PS/2 keyboard instance data for ring-3.
 * @param   u8State         Bitfield which reflects LEDs state.
 */
static void ps2kR3NotifyLedsState(PPS2KR3 pThisCC, uint8_t u8State)
{
    PDMKEYBLEDS enmLeds = PDMKEYBLEDS_NONE;

    if (u8State & 0x01)
        enmLeds = (PDMKEYBLEDS)(enmLeds | PDMKEYBLEDS_SCROLLLOCK);
    if (u8State & 0x02)
        enmLeds = (PDMKEYBLEDS)(enmLeds | PDMKEYBLEDS_NUMLOCK);
    if (u8State & 0x04)
        enmLeds = (PDMKEYBLEDS)(enmLeds | PDMKEYBLEDS_CAPSLOCK);

    if (pThisCC->Keyboard.pDrv)
        pThisCC->Keyboard.pDrv->pfnLedStatusChange(pThisCC->Keyboard.pDrv, enmLeds);
}

#endif /* IN_RING3 */

/** Clears the currently active typematic key, if any. */
static void ps2kStopTypematicRepeat(PPDMDEVINS pDevIns, PPS2K pThis)
{
    if (pThis->u32TypematicKey)
    {
        LogFunc(("Typematic key %08X\n", pThis->u32TypematicKey));
        pThis->enmTypematicState = KBD_TMS_IDLE;
        pThis->u32TypematicKey = 0;
        PDMDevHlpTimerStop(pDevIns, pThis->hKbdTypematicTimer);
    }
}

/** Convert encoded typematic value to milliseconds. Note that the values are rated
 * with +/- 20% accuracy, so there's no need for high precision.
 */
static void ps2kSetupTypematic(PPS2K pThis, uint8_t val)
{
    int         A, B;
    unsigned    period;

    pThis->u8TypematicCfg = val;
    /* The delay is easy: (1 + value) * 250 ms */
    pThis->uTypematicDelay = (1 + ((val >> 5) & 3)) * 250;
    /* The rate is more complicated: (8 + A) * 2^B * 4.17 ms */
    A = val & 7;
    B = (val >> 3) & 3;
    period = (8 + A) * (1 << B) * 417 / 100;
    pThis->uTypematicRepeat = period;
    Log(("Typematic delay %u ms, repeat period %u ms\n",
         pThis->uTypematicDelay, pThis->uTypematicRepeat));
}

static void ps2kSetDefaults(PPDMDEVINS pDevIns, PPS2K pThis)
{
    LogFlowFunc(("Set keyboard defaults\n"));
    PS2Q_CLEAR(&pThis->keyQ);
    /* Set default Scan Set 3 typematic values. */
    /* Set default typematic rate/delay. */
    ps2kSetupTypematic(pThis, KBD_DFL_RATE_DELAY);
    /* Clear last typematic key?? */
    ps2kStopTypematicRepeat(pDevIns, pThis);
}

/**
 * The keyboard controller disabled the keyboard serial line.
 *
 * @param   pThis   The keyboard device shared instance data.
 */
void PS2KLineDisable(PPS2K pThis)
{
    LogFlowFunc(("Disabling keyboard serial line\n"));

    pThis->fLineDisabled = true;
}

/**
 * The keyboard controller enabled the keyboard serial line.
 *
 * @param   pThis   The keyboard device shared instance data.
 */
void PS2KLineEnable(PPS2K pThis)
{
    LogFlowFunc(("Enabling keyboard serial line\n"));

    pThis->fLineDisabled = false;

    /* If there was anything in the input queue,
     * consider it lost and throw it away.
     */
    PS2Q_CLEAR(&pThis->keyQ);
}

/**
 * Receive and process a byte sent by the keyboard controller.
 *
 * @param   pDevIns The device instance.
 * @param   pThis   The shared PS/2 keyboard instance data.
 * @param   cmd     The command (or data) byte.
 */
int PS2KByteToKbd(PPDMDEVINS pDevIns, PPS2K pThis, uint8_t cmd)
{
    bool    fHandled = true;

    LogFlowFunc(("new cmd=0x%02X, active cmd=0x%02X\n", cmd, pThis->u8CurrCmd));

    if (pThis->u8CurrCmd == KCMD_RESET)
        /* In reset mode, do not respond at all. */
        return VINF_SUCCESS;

    switch (cmd)
    {
        case KCMD_ECHO:
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ECHO);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_READ_ID:
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ID1);
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ID2);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_ENABLE:
            pThis->fScanning = true;
            PS2Q_CLEAR(&pThis->keyQ);
            ps2kStopTypematicRepeat(pDevIns, pThis);
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_DFLT_DISABLE:
            pThis->fScanning = false;
            ps2kSetDefaults(pDevIns, pThis); /* Also clears buffer/typematic state. */
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_SET_DEFAULT:
            ps2kSetDefaults(pDevIns, pThis);
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_ALL_TYPEMATIC:
        case KCMD_ALL_MK_BRK:
        case KCMD_ALL_MAKE:
        case KCMD_ALL_TMB:
            /// @todo Set the key types here.
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_RESEND:
            pThis->u8CurrCmd = 0;
            break;
        case KCMD_RESET:
            pThis->u8ScanSet = 2;
            ps2kSetDefaults(pDevIns, pThis);
            /// @todo reset more?
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = cmd;
            /* Delay BAT completion; the test may take hundreds of ms. */
            PDMDevHlpTimerSetMillies(pDevIns, pThis->hKbdDelayTimer, 2);
            break;
        /* The following commands need a parameter. */
        case KCMD_LEDS:
        case KCMD_SCANSET:
        case KCMD_RATE_DELAY:
        case KCMD_TYPE_MATIC:
        case KCMD_TYPE_MK_BRK:
        case KCMD_TYPE_MAKE:
            PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
            pThis->u8CurrCmd = cmd;
            break;
        default:
            /* Sending a command instead of a parameter starts the new command. */
            switch (pThis->u8CurrCmd)
            {
                case KCMD_LEDS:
#ifndef IN_RING3
                    return VINF_IOM_R3_IOPORT_WRITE;
#else
                    {
                        PPS2KR3 pThisCC = &PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3)->Kbd;
                        ps2kR3NotifyLedsState(pThisCC, cmd);
                        pThis->fNumLockOn = !!(cmd & 0x02); /* Sync internal Num Lock state. */
                        PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
                        pThis->u8LEDs = cmd;
                        pThis->u8CurrCmd = 0;
                    }
#endif
                    break;
                case KCMD_SCANSET:
                    PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
                    if (cmd == 0)
                        PS2Q_INSERT(&pThis->cmdQ, pThis->u8ScanSet);
                    else if (cmd < 4)
                    {
                        pThis->u8ScanSet = cmd;
                        LogRel(("PS2K: Selected scan set %d\n", cmd));
                    }
                    /* Other values are simply ignored. */
                    pThis->u8CurrCmd = 0;
                    break;
                case KCMD_RATE_DELAY:
                    ps2kSetupTypematic(pThis, cmd);
                    PS2Q_INSERT(&pThis->cmdQ, KRSP_ACK);
                    pThis->u8CurrCmd = 0;
                    break;
                default:
                    fHandled = false;
            }
            /* Fall through only to handle unrecognized commands. */
            if (fHandled)
                break;
            RT_FALL_THRU();

        case KCMD_INVALID_1:
        case KCMD_INVALID_2:
            PS2Q_INSERT(&pThis->cmdQ, KRSP_RESEND);
            pThis->u8CurrCmd = 0;
            break;
    }
    LogFlowFunc(("Active cmd now 0x%02X; updating interrupts\n", pThis->u8CurrCmd));
//    KBCUpdateInterrupts(pDevIns);
    return VINF_SUCCESS;
}

/**
 * Send a byte (keystroke or command response) to the keyboard controller.
 *
 * @returns VINF_SUCCESS or VINF_TRY_AGAIN.
 * @param   pDevIns The device instance.
 * @param   pThis   The shared PS/2 keyboard instance data.
 * @param   pb      Where to return the byte we've read.
 * @remarks Caller must have entered the device critical section.
 */
int PS2KByteFromKbd(PPDMDEVINS pDevIns, PPS2K pThis, uint8_t *pb)
{
    int         rc;

    AssertPtr(pb);

    /* Anything in the command queue has priority over data
     * in the keystroke queue. Additionally, keystrokes are
     * blocked if a command is currently in progress, even if
     * the command queue is empty.
     */
    rc = PS2Q_REMOVE(&pThis->cmdQ, pb);
    if (rc != VINF_SUCCESS && !pThis->u8CurrCmd && pThis->fScanning)
        if (!pThis->fThrottleActive)
        {
            rc = PS2Q_REMOVE(&pThis->keyQ, pb);
            if (pThis->fThrottleEnabled)
            {
                pThis->fThrottleActive = true;
                PDMDevHlpTimerSetMillies(pDevIns, pThis->hThrottleTimer, KBD_THROTTLE_DELAY);
            }
        }

    LogFlowFunc(("keyboard sends 0x%02x (%svalid data)\n", *pb, rc == VINF_SUCCESS ? "" : "not "));
    return rc;
}

#ifdef IN_RING3

static int ps2kR3HidToInternalCode(uint32_t u32HidCode, key_def const **ppKeyDef)
{
    uint8_t         u8HidPage;
    uint16_t        u16HidUsage;
    int             iKeyIndex = -1;
    key_def const   *pKeyDef = &aPS2Keys[0];    /* Dummy no-event key. */;

    u8HidPage   = RT_LOBYTE(RT_HIWORD(u32HidCode));
    u16HidUsage = RT_LOWORD(u32HidCode);

    if (u8HidPage == USB_HID_KB_PAGE)
    {
        if (u16HidUsage <= VBOX_USB_MAX_USAGE_CODE)
        {
            iKeyIndex = u16HidUsage;    /* Direct mapping. */
            pKeyDef   = iKeyIndex >= HID_MODIFIER_FIRST ? &aPS2ModKeys[iKeyIndex - HID_MODIFIER_FIRST] : &aPS2Keys[iKeyIndex];
        }
        else
            AssertMsgFailed(("u16HidUsage out of range! (%04X)\n", u16HidUsage));
    }
    else if (u8HidPage == USB_HID_CC_PAGE)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(aPS2CCKeys); ++i)
            if (aPS2CCKeys[i].usageId == u16HidUsage)
            {
                pKeyDef   = &aPS2CCKeys[i].kdef;
                iKeyIndex = PS2K_PAGE_CC_START + i;
                break;
            }
        AssertMsg(iKeyIndex > -1, ("Unsupported code in USB_HID_CC_PAGE! (%04X)\n", u16HidUsage));
    }
    else if (u8HidPage == USB_HID_DC_PAGE)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(aPS2DCKeys); ++i)
            if (aPS2DCKeys[i].usageId == u16HidUsage)
            {
                pKeyDef   = &aPS2DCKeys[i].kdef;
                iKeyIndex = PS2K_PAGE_DC_START + i;
                break;
            }
        AssertMsg(iKeyIndex > -1, ("Unsupported code in USB_HID_DC_PAGE! (%04X)\n", u16HidUsage));
    }
    else
    {
        AssertMsgFailed(("Unsupported u8HidPage! (%02X)\n", u8HidPage));
    }

    if (ppKeyDef)
        *ppKeyDef = pKeyDef;

    return iKeyIndex;
}

static uint32_t ps2kR3InternalCodeToHid(unsigned uKeyCode)
{
    uint16_t    u16HidUsage;
    uint32_t    u32HidCode = 0;

    if ((uKeyCode >= PS2K_PAGE_DC_START) && (uKeyCode <= PS2K_PAGE_DC_END))
    {
        u16HidUsage = aPS2DCKeys[uKeyCode - PS2K_PAGE_DC_START].usageId;
        u32HidCode  = RT_MAKE_U32(u16HidUsage, USB_HID_DC_PAGE);
    }
    else if ((uKeyCode >= PS2K_PAGE_CC_START) && (uKeyCode <= PS2K_PAGE_CC_END))
    {
        u16HidUsage = aPS2CCKeys[uKeyCode - PS2K_PAGE_CC_START].usageId;
        u32HidCode  = RT_MAKE_U32(u16HidUsage, USB_HID_CC_PAGE);
    }
    else    /* Must be the keyboard usage page. */
    {
        if (uKeyCode <= VBOX_USB_MAX_USAGE_CODE)
            u32HidCode = RT_MAKE_U32(uKeyCode, USB_HID_KB_PAGE);
        else
            AssertMsgFailed(("uKeyCode out of range! (%u)\n", uKeyCode));
    }

    return u32HidCode;
}

static int ps2kR3ProcessKeyEvent(PPDMDEVINS pDevIns, PPS2K pThis, uint32_t u32HidCode, bool fKeyDown)
{
    key_def const   *pKeyDef;
    uint8_t         abCodes[16];
    char            *pCodes;
    size_t          cbLeft;
    uint8_t         abScan[2];
    uint8_t         u8HidPage;
    uint16_t        u16HidUsage;

    u8HidPage   = RT_LOBYTE(RT_HIWORD(u32HidCode));
    u16HidUsage = RT_LOWORD(u32HidCode);

    LogFlowFunc(("key %s: page 0x%02x ID 0x%04x (set %d)\n", fKeyDown ? "down" : "up", u8HidPage, u16HidUsage, pThis->u8ScanSet));

    ps2kR3HidToInternalCode(u32HidCode, &pKeyDef);

    /* Some keys are not processed at all; early return. */
    if (pKeyDef->makeS1 == NONE)
    {
        LogFlow(("Skipping key processing.\n"));
        return VINF_SUCCESS;
    }

    /* Handle modifier keys (Ctrl/Alt/Shift/GUI). We need to keep track
     * of their state in addition to sending the scan code.
     */
    if ((u8HidPage == USB_HID_KB_PAGE) && (u16HidUsage >= HID_MODIFIER_FIRST))
    {
        unsigned    mod_bit = 1 << (u16HidUsage - HID_MODIFIER_FIRST);

        Assert((u16HidUsage <= HID_MODIFIER_LAST));
        if (fKeyDown)
            pThis->u8Modifiers |= mod_bit;
        else
            pThis->u8Modifiers &= ~mod_bit;
    }

    /* Toggle NumLock state. */
    if ((pKeyDef->keyFlags & KF_NL) && fKeyDown)
        pThis->fNumLockOn ^= true;

    abCodes[0] = 0;
    pCodes = (char *)abCodes;
    cbLeft = sizeof(abCodes);

    if (pThis->u8ScanSet == 1 || pThis->u8ScanSet == 2)
    {
        /* The basic scan set 1 and 2 logic is the same, only the scan codes differ.
         * Since scan set 2 is used almost all the time, that case is handled first.
         */
        if (fKeyDown)
        {
            /* Process key down event. */
            if (pKeyDef->keyFlags & KF_PB)
            {
                /* Pause/Break sends different data if either Ctrl is held. */
                if (pThis->u8Modifiers & (MOD_LCTRL | MOD_RCTRL))
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE0\x7E\xE0\xF0\x7E" : "\xE0\x46\xE0\xC6");
                else
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE1\x14\x77\xE1\xF0\x14\xF0\x77" : "\xE1\x1D\x45\xE1\x9D\xC5");
            }
            else if (pKeyDef->keyFlags & KF_PS)
            {
                /* Print Screen depends on all of Ctrl, Shift, *and* Alt! */
                if (pThis->u8Modifiers & (MOD_LALT | MOD_RALT))
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\x84" : "\x54");
                else if (pThis->u8Modifiers & (MOD_LSHIFT | MOD_RSHIFT))
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE0\x7C" : "\xE0\x37");
                else
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE0\x12\xE0\x7C" : "\xE0\x2A\xE0\x37");
            }
            else if (pKeyDef->keyFlags & (KF_GK | KF_NS))
            {
                /* The numeric pad keys fake Shift presses or releases
                 * depending on Num Lock and Shift key state. The '/'
                 * key behaves in a similar manner but does not depend on
                 * the Num Lock state.
                 */
                if (!pThis->fNumLockOn || (pKeyDef->keyFlags & KF_NS))
                {
                    if (pThis->u8Modifiers & MOD_LSHIFT)
                        RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                  "\xE0\xF0\x12" : "\xE0\xAA");
                    if (pThis->u8Modifiers & MOD_RSHIFT)
                        RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                  "\xE0\xF0\x59" : "\xE0\xB6");
                }
                else
                {
                    Assert(pThis->fNumLockOn);  /* Not for KF_NS! */
                    if ((pThis->u8Modifiers & (MOD_LSHIFT | MOD_RSHIFT)) == 0)
                        RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                  "\xE0\x12" : "\xE0\x2A");
                    /* Else Shift cancels NumLock, so no prefix! */
                }
            }

            /* Standard processing for regular keys only. */
            abScan[0] = pThis->u8ScanSet == 2 ? pKeyDef->makeS2 : pKeyDef->makeS1;
            abScan[1] = '\0';
            if (!(pKeyDef->keyFlags & (KF_PB | KF_PS)))
            {
                if (pKeyDef->keyFlags & (KF_E0 | KF_GK | KF_NS))
                    RTStrCatP(&pCodes, &cbLeft, "\xE0");
                RTStrCatP(&pCodes, &cbLeft, (const char *)abScan);
            }

            /* Feed the bytes to the queue if there is room. */
            /// @todo Send overrun code if sequence won't fit?
            ps2kR3InsertStrQueue(&pThis->keyQ, abCodes, 0);
        }
        else if (!(pKeyDef->keyFlags & (KF_NB | KF_PB)))
        {
            /* Process key up event except for keys which produce none. */

            /* Handle Print Screen release. */
            if (pKeyDef->keyFlags & KF_PS)
            {
                /* Undo faked Print Screen state as needed. */
                if (pThis->u8Modifiers & (MOD_LALT | MOD_RALT))
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xF0\x84" : "\xD4");
                else if (pThis->u8Modifiers & (MOD_LSHIFT | MOD_RSHIFT))
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE0\xF0\x7C" : "\xE0\xB7");
                else
                    RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                              "\xE0\xF0\x7C\xE0\xF0\x12" : "\xE0\xB7\xE0\xAA");
            }
            else
            {
                /* Process base scan code for less unusual keys. */
                abScan[0] = pThis->u8ScanSet == 2 ? pKeyDef->makeS2 : pKeyDef->makeS1 | 0x80;
                abScan[1] = '\0';
                if (pKeyDef->keyFlags & (KF_E0 | KF_GK | KF_NS))
                    RTStrCatP(&pCodes, &cbLeft, "\xE0");
                if (pThis->u8ScanSet == 2)
                    RTStrCatP(&pCodes, &cbLeft, "\xF0");
                RTStrCatP(&pCodes, &cbLeft, (const char *)abScan);

                /* Restore shift state for gray keys. */
                if (pKeyDef->keyFlags & (KF_GK | KF_NS))
                {
                    if (!pThis->fNumLockOn || (pKeyDef->keyFlags & KF_NS))
                    {
                        if (pThis->u8Modifiers & MOD_LSHIFT)
                            RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                      "\xE0\x12" : "\xE0\x2A");
                        if (pThis->u8Modifiers & MOD_RSHIFT)
                            RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                      "\xE0\x59" : "\xE0\x36");
                    }
                    else
                    {
                        Assert(pThis->fNumLockOn);  /* Not for KF_NS! */
                        if ((pThis->u8Modifiers & (MOD_LSHIFT | MOD_RSHIFT)) == 0)
                            RTStrCatP(&pCodes, &cbLeft, pThis->u8ScanSet == 2 ?
                                      "\xE0\xF0\x12" : "\xE0\xAA");
                    }
                }
            }

            /* Feed the bytes to the queue if there is room. */
            /// @todo Send overrun code if sequence won't fit?
            ps2kR3InsertStrQueue(&pThis->keyQ, abCodes, 0);
        }
    }
    else
    {
        /* Handle Scan Set 3 - very straightforward. */
        Assert(pThis->u8ScanSet == 3);
        abScan[0] = pKeyDef->makeS3;
        abScan[1] = '\0';
        if (fKeyDown)
        {
            RTStrCatP(&pCodes, &cbLeft, (const char *)abScan);
        }
        else
        {
            /* Send a key release code unless it's a make only key. */
            /// @todo Look up the current typematic setting, not the default!
            if (pKeyDef->keyMatic != T_M)
            {
                RTStrCatP(&pCodes, &cbLeft, "\xF0");
                RTStrCatP(&pCodes, &cbLeft, (const char *)abScan);
            }
        }
        /* Feed the bytes to the queue if there is room. */
        /// @todo Send overrun code if sequence won't fit?
        ps2kR3InsertStrQueue(&pThis->keyQ, abCodes, 0);
    }

    /* Set up or cancel typematic key repeat. For keyboard usage page only. */
    if (u8HidPage == USB_HID_KB_PAGE)
    {
        if (fKeyDown)
        {
            if (pThis->u32TypematicKey != u32HidCode)
            {
                pThis->enmTypematicState = KBD_TMS_DELAY;
                pThis->u32TypematicKey   = u32HidCode;
                PDMDevHlpTimerSetMillies(pDevIns, pThis->hKbdTypematicTimer, pThis->uTypematicDelay);
                Log(("Typematic delay %u ms, key %08X\n", pThis->uTypematicDelay, u32HidCode));
            }
        }
        else
        {
            /* "Typematic operation stops when the last key pressed is released, even
             * if other keys are still held down." (IBM PS/2 Tech Ref). The last key pressed
             * is the one that's being repeated.
             */
            if (pThis->u32TypematicKey == u32HidCode)
            {
                /* This disables the typematic repeat. */
                pThis->u32TypematicKey   = 0;
                pThis->enmTypematicState = KBD_TMS_IDLE;
                /* For good measure, we cancel the timer, too. */
                PDMDevHlpTimerStop(pDevIns, pThis->hKbdTypematicTimer);
                Log(("Typematic action cleared for key %08X\n", u32HidCode));
            }
        }
    }

    /* Poke the KBC to update its state. */
    KBCUpdateInterrupts(pDevIns);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNTMTIMERDEV}
 *
 * Throttling timer to emulate the finite keyboard communication speed. A PS/2 keyboard is
 * limited by the serial link speed and cannot send much more than 1,000 bytes per second.
 * Some software (notably Borland Pascal and programs built with its run-time) relies on
 * being able to read an incoming scan-code twice. Throttling the data rate enables such
 * software to function, while human typists cannot tell any difference.
 *
 * Note: The throttling is currently only done for keyboard data, not command responses.
 * The throttling could and perhaps should be done for any data (including command
 * responses) coming from PS/2 devices, both keyboard and auxiliary. That is not currently
 * done because it would needlessly slow things down.
 */
static DECLCALLBACK(void) ps2kR3ThrottleTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPS2K       pThis = (PS2K *)pvUser;
    unsigned    uHaveData;
    RT_NOREF(hTimer);

    /* Grab the lock to avoid races with event delivery or EMTs. */
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /* If data is available, poke the KBC. Once the data
     * is actually read, the timer may be re-triggered.
     */
    pThis->fThrottleActive = false;
    uHaveData = PS2Q_COUNT(&pThis->keyQ);
    LogFlowFunc(("Have%s bytes\n", uHaveData ? "" : " no"));
    if (uHaveData)
        KBCUpdateInterrupts(pDevIns);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}

/**
 * @callback_method_impl{FNTMTIMERDEV,
 * Timer handler for emulating typematic keys.}
 *
 * @note    Note that only the last key held down repeats (if typematic).
 */
static DECLCALLBACK(void) ps2kR3TypematicTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPS2K pThis = (PS2K *)pvUser;
    Assert(hTimer == pThis->hKbdTypematicTimer);
    LogFlowFunc(("Typematic state=%d, key %08X\n", pThis->enmTypematicState, pThis->u32TypematicKey));

    /* If the current typematic key is zero, the repeat was canceled just when
     * the timer was about to run. In that case, do nothing.
     */
    if (pThis->u32TypematicKey)
    {
        if (pThis->enmTypematicState == KBD_TMS_DELAY)
            pThis->enmTypematicState = KBD_TMS_REPEAT;

        if (pThis->enmTypematicState == KBD_TMS_REPEAT)
        {
            ps2kR3ProcessKeyEvent(pDevIns, pThis, pThis->u32TypematicKey, true /* Key down */ );
            PDMDevHlpTimerSetMillies(pDevIns, hTimer, pThis->uTypematicRepeat);
        }
    }
}

/**
 * @callback_method_impl{FNTMTIMERDEV}
 *
 * The keyboard BAT is specified to take several hundred milliseconds. We need
 * to delay sending the result to the host for at least a tiny little while.
 */
static DECLCALLBACK(void) ps2kR3DelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPS2K pThis = (PS2K *)pvUser;
    RT_NOREF(hTimer);

    LogFlowFunc(("Delay timer: cmd %02X\n", pThis->u8CurrCmd));

    AssertMsg(pThis->u8CurrCmd == KCMD_RESET, ("u8CurrCmd=%02x\n", pThis->u8CurrCmd));
    PS2Q_INSERT(&pThis->cmdQ, KRSP_BAT_OK);
    pThis->fScanning = true;    /* BAT completion enables scanning! */
    pThis->u8CurrCmd = 0;

    /// @todo Might want a PS2KCompleteCommand() to push last response, clear command, and kick the KBC...
    /* Give the KBC a kick. */
    KBCUpdateInterrupts(pDevIns);
}

/* Release any and all currently depressed keys. Used whenever the guest keyboard
 * is likely to be out of sync with the host, such as when loading a saved state
 * or resuming a suspended host.
 */
static void ps2kR3ReleaseKeys(PPDMDEVINS pDevIns, PPS2K pThis)
{
    LogFlowFunc(("Releasing keys...\n"));

    for (unsigned uKey = 0; uKey < RT_ELEMENTS(pThis->abDepressedKeys); ++uKey)
        if (pThis->abDepressedKeys[uKey])
        {
            ps2kR3ProcessKeyEvent(pDevIns, pThis, ps2kR3InternalCodeToHid(uKey), false /* key up */);
            pThis->abDepressedKeys[uKey] = 0;
        }
    LogFlowFunc(("Done releasing keys\n"));
}


/**
 * Debug device info handler. Prints basic keyboard state.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) ps2kR3InfoState(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PKBDSTATE   pParent = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PPS2K       pThis   = &pParent->Kbd;
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "PS/2 Keyboard: scan set %d, scanning %s, serial line %s\n",
                    pThis->u8ScanSet, pThis->fScanning ? "enabled" : "disabled",
                    pThis->fLineDisabled ? "disabled" : "enabled");
    pHlp->pfnPrintf(pHlp, "Active command %02X\n", pThis->u8CurrCmd);
    pHlp->pfnPrintf(pHlp, "LED state %02X, Num Lock %s\n", pThis->u8LEDs,
                    pThis->fNumLockOn ? "on" : "off");
    pHlp->pfnPrintf(pHlp, "Typematic delay %ums, repeat period %ums\n",
                    pThis->uTypematicDelay, pThis->uTypematicRepeat);
    pHlp->pfnPrintf(pHlp, "Command queue: %d items (%d max)\n",
                    PS2Q_COUNT(&pThis->cmdQ), PS2Q_SIZE(&pThis->cmdQ));
    pHlp->pfnPrintf(pHlp, "Input queue  : %d items (%d max)\n",
                    PS2Q_COUNT(&pThis->keyQ), PS2Q_SIZE(&pThis->keyQ));
    if (pThis->enmTypematicState != KBD_TMS_IDLE)
        pHlp->pfnPrintf(pHlp, "Active typematic key %08X (%s)\n", pThis->u32TypematicKey,
                        pThis->enmTypematicState == KBD_TMS_DELAY ? "delay" : "repeat");
}


/* -=-=-=-=-=- Keyboard: IKeyboardPort  -=-=-=-=-=- */

/**
 * Keyboard event handler.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The PS2 keyboard instance data.
 * @param   idUsage     USB HID usage code with key press/release flag.
 */
static int ps2kR3PutEventWorker(PPDMDEVINS pDevIns, PPS2K pThis, uint32_t idUsage)
{
    uint32_t        u32HidCode;
    bool            fKeyDown;
    bool            fHaveEvent = true;
    int             iKeyCode;
    int             rc = VINF_SUCCESS;

    /* Extract the usage page and ID and ensure it's valid. */
    fKeyDown   = !(idUsage & PDMIKBDPORT_KEY_UP);
    u32HidCode = idUsage & 0xFFFFFF;

    iKeyCode = ps2kR3HidToInternalCode(u32HidCode, NULL);
    AssertMsgReturn(iKeyCode > 0 && iKeyCode <= VBOX_USB_MAX_USAGE_CODE, ("iKeyCode=%#x idUsage=%#x\n", iKeyCode, idUsage),
                    VERR_INTERNAL_ERROR);

    if (fKeyDown)
    {
        /* Due to host key repeat, we can get key events for keys which are
         * already depressed. We need to ignore those. */
        if (pThis->abDepressedKeys[iKeyCode])
            fHaveEvent = false;
        pThis->abDepressedKeys[iKeyCode] = 1;
    }
    else
    {
        /* NB: We allow key release events for keys which aren't depressed.
         * That is unlikely to happen and should not cause trouble.
         */
        pThis->abDepressedKeys[iKeyCode] = 0;
    }

    /* Unless this is a new key press/release, don't even bother. */
    if (fHaveEvent)
    {
        Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->pCritSectRoR3));
        rc = ps2kR3ProcessKeyEvent(pDevIns, pThis, u32HidCode, fKeyDown);
    }

    return rc;
}


/**
 * @interface_method_impl{PDMIKEYBOARDPORT,pfnPutEventHid}
 */
static DECLCALLBACK(int) ps2kR3KeyboardPort_PutEventHid(PPDMIKEYBOARDPORT pInterface, uint32_t idUsage)
{
    PPS2KR3     pThisCC = RT_FROM_MEMBER(pInterface, PS2KR3, Keyboard.IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PPS2K       pThis   = &PDMDEVINS_2_DATA(pDevIns, PKBDSTATE)->Kbd;

    LogRelFlowFunc(("key code %08X\n", idUsage));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /* The 'BAT fail' scancode is reused as a signal to release keys. No actual
     * key is allowed to use this scancode.
     */
    if (RT_LIKELY(!(idUsage & PDMIKBDPORT_RELEASE_KEYS)))
        ps2kR3PutEventWorker(pDevIns, pThis, idUsage);
    else
        ps2kR3ReleaseKeys(pDevIns, pThis);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Keyboard: IBase  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ps2kR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPS2KR3 pThisCC = RT_FROM_MEMBER(pInterface, PS2KR3, Keyboard.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->Keyboard.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIKEYBOARDPORT, &pThisCC->Keyboard.IPort);
    return NULL;
}


/* -=-=-=-=-=- Device management -=-=-=-=-=- */

/**
 * Attach command.
 *
 * This is called to let the device attach to a driver for a
 * specified LUN.
 *
 * This is like plugging in the keyboard after turning on the
 * system.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The PS/2 keyboard instance data for ring-3.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
int PS2KR3Attach(PPDMDEVINS pDevIns, PPS2KR3 pThisCC, unsigned iLUN, uint32_t fFlags)
{
    int         rc;

    /* The LUN must be 0, i.e. keyboard. */
    Assert(iLUN == 0);
    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("PS/2 keyboard does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    LogFlowFunc(("iLUN=%d\n", iLUN));

    rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->Keyboard.IBase, &pThisCC->Keyboard.pDrvBase, "Keyboard Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->Keyboard.pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->Keyboard.pDrvBase, PDMIKEYBOARDCONNECTOR);
        if (!pThisCC->Keyboard.pDrv)
        {
            AssertLogRelMsgFailed(("LUN #0 doesn't have a keyboard interface! rc=%Rrc\n", rc));
            rc = VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        Log(("%s/%d: warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
        rc = VINF_SUCCESS;
    }
    else
        AssertLogRelMsgFailed(("Failed to attach LUN #0! rc=%Rrc\n", rc));

    return rc;
}

void PS2KR3SaveState(PPDMDEVINS pDevIns, PPS2K pThis, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    uint32_t        cPressed = 0;
    uint32_t        cbTMSSize = 0;

    LogFlowFunc(("Saving PS2K state\n"));

    /* Save the basic keyboard state. */
    pHlp->pfnSSMPutU8(pSSM, pThis->u8CurrCmd);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8LEDs);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8TypematicCfg);
    pHlp->pfnSSMPutU8(pSSM, (uint8_t)pThis->u32TypematicKey);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8Modifiers);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8ScanSet);
    pHlp->pfnSSMPutU8(pSSM, pThis->enmTypematicState);
    pHlp->pfnSSMPutBool(pSSM, pThis->fNumLockOn);
    pHlp->pfnSSMPutBool(pSSM, pThis->fScanning);

    /* Save the command and keystroke queues. */
    PS2Q_SAVE(pHlp, pSSM, &pThis->cmdQ);
    PS2Q_SAVE(pHlp, pSSM, &pThis->keyQ);

    /* Save the command delay timer. Note that the typematic repeat
     * timer is *not* saved.
     */
    PDMDevHlpTimerSave(pDevIns, pThis->hKbdDelayTimer, pSSM);

    /* Save any pressed keys. This is necessary to avoid "stuck"
     * keys after a restore. Needs two passes.
     */
    for (unsigned i = 0; i < sizeof(pThis->abDepressedKeys); ++i)
        if (pThis->abDepressedKeys[i])
            ++cPressed;

    pHlp->pfnSSMPutU32(pSSM, cPressed);

    for (unsigned uKey = 0; uKey < sizeof(pThis->abDepressedKeys); ++uKey)
        if (pThis->abDepressedKeys[uKey])
            pHlp->pfnSSMPutU8(pSSM, uKey);

    /* Save the typematic settings for Scan Set 3. */
    pHlp->pfnSSMPutU32(pSSM, cbTMSSize);
    /* Currently not implemented. */
}

int PS2KR3LoadState(PPDMDEVINS pDevIns, PPS2K pThis, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    uint8_t         u8;
    uint32_t        cPressed;
    uint32_t        cbTMSSize;
    int             rc;

    NOREF(uVersion);
    LogFlowFunc(("Loading PS2K state version %u\n", uVersion));

    /* Load the basic keyboard state. */
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8CurrCmd);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8LEDs);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8TypematicCfg);
    pHlp->pfnSSMGetU8(pSSM, &u8);
    /* Reconstruct the 32-bit code from the 8-bit value in saved state. */
    pThis->u32TypematicKey = u8 ? ps2kR3InternalCodeToHid(u8) : 0;
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8Modifiers);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8ScanSet);
    pHlp->pfnSSMGetU8(pSSM, &u8);
    pThis->enmTypematicState = (tmatic_state_t)u8;
    pHlp->pfnSSMGetBool(pSSM, &pThis->fNumLockOn);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fScanning);

    /* Load the command and keystroke queues. */
    rc = PS2Q_LOAD(pHlp, pSSM, &pThis->cmdQ);
    AssertRCReturn(rc, rc);
    rc = PS2Q_LOAD(pHlp, pSSM, &pThis->keyQ);
    AssertRCReturn(rc, rc);

    /* Load the command delay timer, just in case. */
    rc = PDMDevHlpTimerLoad(pDevIns, pThis->hKbdDelayTimer, pSSM);
    AssertRCReturn(rc, rc);

    /* Recalculate the typematic delay/rate. */
    ps2kSetupTypematic(pThis, pThis->u8TypematicCfg);

    /* Fake key up events for keys that were held down at the time the state was saved. */
    rc = pHlp->pfnSSMGetU32(pSSM, &cPressed);
    AssertRCReturn(rc, rc);

    /* If any keys were down, load and then release them. */
    if (cPressed)
    {
        for (unsigned i = 0; i < cPressed; ++i)
        {
            rc = pHlp->pfnSSMGetU8(pSSM, &u8);
            AssertRCReturn(rc, rc);
            pThis->abDepressedKeys[u8] = 1;
        }
    }

    /* Load typematic settings for Scan Set 3. */
    rc = pHlp->pfnSSMGetU32(pSSM, &cbTMSSize);
    AssertRCReturn(rc, rc);

    while (cbTMSSize--)
    {
        rc = pHlp->pfnSSMGetU8(pSSM, &u8);
        AssertRCReturn(rc, rc);
    }

    return rc;
}

int PS2KR3LoadDone(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC)
{
    /* This *must* be done after the inital load because it may trigger
     * interrupts and change the interrupt controller state.
     */
    ps2kR3ReleaseKeys(pDevIns, pThis);
    ps2kR3NotifyLedsState(pThisCC, pThis->u8LEDs);
    return VINF_SUCCESS;
}

void PS2KR3Reset(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC)
{
    LogFlowFunc(("Resetting PS2K\n"));

    pThis->fScanning         = true;
    pThis->fThrottleActive   = false;
    pThis->u8ScanSet         = 2;
    pThis->u8CurrCmd         = 0;
    pThis->u8Modifiers       = 0;
    pThis->u32TypematicKey   = 0;
    pThis->enmTypematicState = KBD_TMS_IDLE;

    /* Clear queues and any pressed keys. */
    memset(pThis->abDepressedKeys, 0, sizeof(pThis->abDepressedKeys));
    PS2Q_CLEAR(&pThis->cmdQ);
    ps2kSetDefaults(pDevIns, pThis);     /* Also clears keystroke queue. */

    /* Activate the PS/2 keyboard by default. */
    if (pThisCC->Keyboard.pDrv)
        pThisCC->Keyboard.pDrv->pfnSetActive(pThisCC->Keyboard.pDrv, true /*fActive*/);
}

int PS2KR3Construct(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC, PCFGMNODE pCfg)
{
    LogFlowFunc(("\n"));
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    /*
     * Read configuration.
     */
    bool fThrottleEnabled;
    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "KbdThrottleEnabled", &fThrottleEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to query \"KbdThrottleEnabled\" from the config"));
    Log(("KbdThrottleEnabled=%RTbool\n", fThrottleEnabled));
    pThis->fThrottleEnabled = fThrottleEnabled;

    /*
     * Initialize state.
     */
    pThisCC->pDevIns                          = pDevIns;
    pThisCC->Keyboard.IBase.pfnQueryInterface = ps2kR3QueryInterface;
    pThisCC->Keyboard.IPort.pfnPutEventHid    = ps2kR3KeyboardPort_PutEventHid;

    pThis->cmdQ.Hdr.pszDescR3 = "Kbd Cmd";
    pThis->keyQ.Hdr.pszDescR3 = "Kbd Key";

    /*
     * Create the input rate throttling timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, ps2kR3ThrottleTimer, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "PS2K Throttle", &pThis->hThrottleTimer);
    AssertRCReturn(rc, rc);

    /*
     * Create the typematic delay/repeat timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, ps2kR3TypematicTimer, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "PS2K Typematic", &pThis->hKbdTypematicTimer);
    AssertRCReturn(rc, rc);

    /*
     * Create the command delay timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, ps2kR3DelayTimer, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "PS2K Delay", &pThis->hKbdDelayTimer);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ps2k", "Display PS/2 keyboard state.", ps2kR3InfoState);

    return rc;
}

#endif /* IN _RING3 */

