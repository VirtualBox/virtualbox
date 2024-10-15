/* $Id: keyboard.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox/Frontends/Common - X11 keyboard handler library.
 */

/* This code is originally from the Wine project. */

/*
 * X11 keyboard driver
 *
 * Copyright 1993 Bob Amstadt
 * Copyright 1996 Albrecht Kleine
 * Copyright 1997 David Faure
 * Copyright 1998 Morten Welinder
 * Copyright 1998 Ulrich Weigand
 * Copyright 1999 Ove K�ven
 *
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

#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <VBox/VBoxKeyboard.h>

/* VBoxKeyboard uses the deprecated XKeycodeToKeysym(3) API, but uses it safely.
 */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define KEYC2SCAN_SIZE 256

/**
 * Array containing the current mapping of keycodes to scan codes, detected
 * using the keyboard layout algorithm in X11DRV_InitKeyboardByLayout.
 */
static unsigned keyc2scan[KEYC2SCAN_SIZE];
/** Whether to output basic debugging information to standard output */
static int log_kb_1 = 0;
/** Whether to output verbose debugging information to standard output */
static int log_kb_2 = 0;

/** Output basic debugging information if wished */
#define LOG_KB_1(a) \
do { \
    if (log_kb_1) { \
        printf a; \
    } \
} while (0)

/** Output verbose debugging information if wished */
#define LOG_KB_2(a) \
do { \
    if (log_kb_2) { \
        printf a; \
    } \
} while (0)

/** Keyboard layout tables for guessing the current keyboard layout. */
#include "keyboard-tables.h"

/** Tables of keycode to scan code mappings for well-known keyboard types. */
#include "keyboard-types.h"

/**
  * Translate a keycode in a key event to a scan code.  If the keycode maps
  * to a key symbol which is in the same place on all PC keyboards, look it
  * up by symbol in one of our hard-coded translation tables.  It it maps to
  * a symbol which can be in a different place on different PC keyboards, look
  * it up by keycode using either the lookup table which we constructed
  * earlier, or using a hard-coded table if we know what type of keyboard is
  * in use.
  *
  * @returns the scan code number, with 0x100 added for extended scan codes
  * @param code the X11 key code to be looked up
  */

unsigned X11DRV_KeyEvent(Display *display, KeyCode code)
{
    unsigned scan;
    KeySym keysym = XKeycodeToKeysym(display, code, 0);
    scan = 0;
    if (keyc2scan[code] == 0 && keysym != 0)
    {
        if ((keysym >> 8) == 0xFF)          /* non-character key */
            scan = nonchar_key_scan[keysym & 0xff];
        else if ((keysym >> 8) == 0x1008FF) /* XFree86 vendor keys */
            scan = xfree86_vendor_key_scan[keysym & 0xff];
        else if ((keysym >> 8) == 0x1005FF) /* Sun keys */
            scan = sun_key_scan[keysym & 0xff];
        else if (keysym == 0x20)            /* Spacebar */
            scan = 0x39;
        else if (keysym == 0xFE03)          /* ISO level3 shift, aka AltGr */
            scan = 0x138;
        else if (keysym == 0xFE11)          /* ISO level5 shift, R-Ctrl on */
            scan = 0x11d;                   /* Canadian multilingual layout */
    }
    if (keyc2scan[code])
        scan = keyc2scan[code];

    return scan;
}

/**
 * Called from X11DRV_InitKeyboardByLayout
 *  See the comments for that function for a description what this function
 *  does.
 *
 * @returns an index into the table of keyboard layouts, or 0 if absolutely
 *          nothing fits
 * @param   display     pointer to the X11 display handle
 * @param   min_keycode the lowest value in use as a keycode on this server
 * @param   max_keycode the highest value in use as a keycode on this server
 */
static int
X11DRV_KEYBOARD_DetectLayout (Display *display, unsigned min_keycode,
                              unsigned max_keycode)
{
  /** Counter variable for iterating through the keyboard layout tables. */
  unsigned current;
  /** The best candidate so far for the layout. */
  unsigned kbd_layout = 0;
  /** The number of matching keys in the current best candidate layout. */
  unsigned max_score = 0;
  /** The number of changes of scan-code direction in the current
      best candidate. */
  unsigned max_seq = 0;
  /** Table for the current keycode to keysym mapping. */
  char ckey[256][2];
  /** Counter variable representing a keycode */
  unsigned keyc;

  /* Fill in our keycode to keysym mapping table. */
  memset( ckey, 0, sizeof(ckey) );
  for (keyc = min_keycode; keyc <= max_keycode; keyc++) {
      /* get data for keycodes from X server */
      KeySym keysym = XKeycodeToKeysym (display, keyc, 0);
      /* We leave keycodes which will definitely not be in the lookup tables
         marked with 0 so that we know that we know not to look them up when
         we scan the tables. */
      if (   (0xFF != (keysym >> 8))     /* Non-character key */
          && (0x1008FF != (keysym >> 8)) /* XFree86 vendor keys */
          && (0x1005FF != (keysym >> 8)) /* Sun keys */
          && (0x20 != keysym)            /* Spacebar */
          && (0xFE03 != keysym)          /* ISO level3 shift, aka AltGr */
         ) {
          ckey[keyc][0] = keysym & 0xFF;
          ckey[keyc][1] = XKeycodeToKeysym(display, keyc, 1) & 0xFF;
      }
  }

  /* Now scan the lookup tables, looking for one that is as close as
     possible to our current keycode to keysym mapping. */
  for (current = 0; main_key_tab[current].comment; current++) {
    /** How many keys have matched so far in this layout? */
    unsigned match = 0;
    /** How many keys have not changed the direction? */
    unsigned seq = 0;
    /** Pointer to the layout we are currently comparing against. */
    const char (*lkey)[MAIN_LEN][2] = main_key_tab[current].key;
    /** For detecting dvorak layouts - in which direction do the server's
        keycodes seem to be running?  We count the number of times that
        this direction changes as an additional hint as to how likely this
        layout is to be the right one. */
    int direction = 1;
    /** The keycode of the last key that we matched.  This is used to
        determine the direction that the keycodes are running in. */
    int pkey = -1;
    LOG_KB_2(("Attempting to match against \"%s\"\n", main_key_tab[current].comment));
    for (keyc = min_keycode; keyc <= max_keycode; keyc++) {
      if (0 != ckey[keyc][0]) {
        /** The candidate key in the current layout for this keycode. */
        int key;
        /** Does this key match? */
        int ok = 0;
        /* search for a match in layout table */
        for (key = 0; (key < MAIN_LEN) && (0 == ok); key++) {
          if (   ((*lkey)[key][0] == ckey[keyc][0])
              && ((*lkey)[key][1] == ckey[keyc][1])
             ) {
              ok = 1;
          }
        }
        /* count the matches and mismatches */
        if (0 != ok) {
          match++;
          /* How well in sequence are the keys?  For dvorak layouts. */
          if (key > pkey) {
            if (1 == direction) {
              ++seq;
            } else {
              direction = -1;
            }
          }
          if (key < pkey) {
            if (1 != direction) {
              ++seq;
            } else {
              direction = 1;
            }
          }
          pkey = key;
        } else {
#ifdef DEBUG
          /* print spaces instead of \0's */
          char str[3] = "  ";
          if ((ckey[keyc][0] > 32) && (ckey[keyc][0] < 127)) {
              str[0] = ckey[keyc][0];
          }
          if ((ckey[keyc][0] > 32) && (ckey[keyc][0] < 127)) {
              str[0] = ckey[keyc][0];
          }
          LOG_KB_2(("Mismatch for keycode %u, keysym \"%s\" (0x%.2hx 0x%.2hx)\n",
                       keyc, str, ckey[keyc][0], ckey[keyc][1]));
#endif /* DEBUG defined */
        }
      }
    }
    LOG_KB_2(("Matches=%u, seq=%u\n", match, seq));
    if (   (match > max_score)
        || ((match == max_score) && (seq > max_seq))
       ) {
      /* best match so far */
      kbd_layout = current;
      max_score = match;
      max_seq = seq;
    }
  }
  /* we're done, report results if necessary */
  LOG_KB_1(("Detected layout is \"%s\", matches=%u, seq=%u\n",
        main_key_tab[kbd_layout].comment, max_score, max_seq));
  return kbd_layout;
}

/**
 * Initialise the X11 keyboard driver by building up a table to convert X11
 * keycodes to scan codes using a heuristic based on comparing the current
 * keyboard map to known international keyboard layouts.
 * The basic idea is to examine each key in the current layout to see which
 * characters it produces in its normal and its "shifted" state, and to look
 * for known keyboard layouts which it could belong to.  We then guess the
 * current layout based on the number of matches we find.
 * One difficulty with this approach is so-called Dvorak layouts, which are
 * identical to non-Dvorak layouts, but with the keys in a different order.
 * To deal with this, we compare the different candidate layouts to see in
 * which one the X11 keycodes would be most sequential and hope that they
 * really are arranged more or less sequentially.
 *
 * The actual detection of the current layout is done in the sub-function
 * X11DRV_KEYBOARD_DetectLayout.  Once we have determined the layout, since we
 * know which PC scan code corresponds to each key in the layout, we can use
 * this information to associate the scan code with an X11 keycode, which is
 * what the rest of this function does.
 *
 * @warning not re-entrant
 * @returns 1 if the layout found was optimal, 0 if it was not.  This is
 *          for diagnostic purposes
 * @param   display a pointer to the X11 display
 */
static unsigned
X11DRV_InitKeyboardByLayout(Display *display)
{
    KeySym keysym;
    unsigned scan;
    int keyc, keyn;
    const char (*lkey)[MAIN_LEN][2];
    int min_keycode, max_keycode;
    int kbd_layout;
    unsigned matches = 0, entries = 0;

    /* Should we log to standard output? */
    if (NULL != getenv("LOG_KB_PRIMARY")) {
        log_kb_1 = 1;
    }
    if (NULL != getenv("LOG_KB_SECONDARY")) {
        log_kb_1 = 1;
        log_kb_2 = 1;
    }
    XDisplayKeycodes(display, &min_keycode, &max_keycode);

    /* according to the space this function is guaranteed to never return
     * values for min_keycode < 8 and values for max_keycode > 255 */
    if (min_keycode < 0)
        min_keycode = 0;
    if (max_keycode > 255)
        max_keycode = 255;

    /* Detect the keyboard layout */
    kbd_layout = X11DRV_KEYBOARD_DetectLayout(display, min_keycode,
                                              max_keycode);
    lkey = main_key_tab[kbd_layout].key;

    /* Now build a conversion array :
     * keycode -> scancode + extended */

    for (keyc = min_keycode; keyc <= max_keycode; keyc++)
    {
        keysym = XKeycodeToKeysym(display, keyc, 0);
        scan = 0;
        if (keysym)  /* otherwise, keycode not used */
        {
          /* Skip over keysyms which we look up on the fly */
          if (   (0xFF != (keysym >> 8))     /* Non-character key */
              && (0x1008FF != (keysym >> 8)) /* XFree86 vendor keys */
              && (0x1005FF != (keysym >> 8)) /* Sun keys */
              && (0x20 != keysym)            /* Spacebar */
              && (0xFE03 != keysym)          /* ISO level3 shift, aka AltGr */
             ) {
              unsigned found = 0;

              /* we seem to need to search the layout-dependent scancodes */
              char unshifted = keysym & 0xFF;
              char shifted = XKeycodeToKeysym(display, keyc, 1) & 0xFF;
              /* find a key which matches */
              for (keyn = 0; (0 == found) && (keyn<MAIN_LEN); keyn++) {
                if (   ((*lkey)[keyn][0] == unshifted)
                    && ((*lkey)[keyn][1] == shifted)
                   ) {
                   found = 1;
                }
              }
              if (0 != found) {
                /* got it */
                scan = main_key_scan[keyn - 1];
                /* We keep track of the number of keys that we found a
                 * match for to see if the layout is optimal or not.
                 * We ignore the 102nd key though (key number 48), since
                 * not all keyboards have it. */
                if (keyn != 48)
                    ++matches;
              }
              if (0 == scan) {
                /* print spaces instead of \0's */
                char str[3] = "  ";
                if ((unshifted > 32) && (unshifted < 127)) {
                    str[0] = unshifted;
                }
                if ((shifted > 32) && (shifted < 127)) {
                    str[1] = shifted;
                }
                LOG_KB_1(("No match found for keycode %d, keysym \"%s\" (0x%x 0x%x)\n",
                             keyc, str, unshifted, shifted));
              } else if ((keyc > 8) && (keyc < 97) && (keyc - scan != 8)) {
                /* print spaces instead of \0's */
                char str[3] = "  ";
                if ((unshifted > 32) && (unshifted < 127)) {
                    str[0] = unshifted;
                }
                if ((shifted > 32) && (shifted < 127)) {
                    str[1] = shifted;
                }
                LOG_KB_1(("Warning - keycode %d, keysym \"%s\" (0x%x 0x%x) was matched to scancode %u\n",
                             keyc, str, unshifted, shifted, scan));
              }
            }
        }
        keyc2scan[keyc] = scan;
    } /* for */
    /* Did we find a match for all keys in the layout?  Count them first.
     * Note that we skip the 102nd key, so that owners of 101 key keyboards
     * don't get bogus messages about bad matches. */
    for (entries = 0, keyn = 0; keyn < MAIN_LEN; ++keyn) {
        if (   (0 != (*lkey)[keyn][0])
            && (0 != (*lkey)[keyn][1])
            && (keyn != 47) /* don't count the 102nd key */
           ) {
            ++entries;
        }
    }
    LOG_KB_1(("Finished mapping keyboard, matches=%u, entries=%u (excluding 102nd key)\n", matches, entries));
    if (matches != entries)
    {
        return 0;
    }
    return 1;
}

static int checkHostKeycode(unsigned hostCode, unsigned targetCode)
{
    if (!targetCode)
        return 0;
    if (hostCode && hostCode != targetCode)
        return 0;
    return 1;
}

static int compKBMaps(const keyboard_type *pHost, const keyboard_type *pTarget)
{
    if (   !pHost->lctrl && !pHost->capslock && !pHost->lshift && !pHost->tab
        && !pHost->esc && !pHost->enter && !pHost->up && !pHost->down
        && !pHost->left && !pHost->right && !pHost->f1 && !pHost->f2
        && !pHost->f3 && !pHost->f4 && !pHost->f5 && !pHost->f6 && !pHost->f7
        && !pHost->f8)
        return 0;
    /* This test is for the people who like to swap control and caps lock */
    if (   (   !checkHostKeycode(pHost->lctrl, pTarget->lctrl)
            || !checkHostKeycode(pHost->capslock, pTarget->capslock))
        && (   !checkHostKeycode(pHost->lctrl, pTarget->capslock)
            || !checkHostKeycode(pHost->capslock, pTarget->lctrl)))
        return 0;
    if (   !checkHostKeycode(pHost->lshift, pTarget->lshift)
        || !checkHostKeycode(pHost->tab, pTarget->tab)
        || !checkHostKeycode(pHost->esc, pTarget->esc)
        || !checkHostKeycode(pHost->enter, pTarget->enter)
        || !checkHostKeycode(pHost->up, pTarget->up)
        || !checkHostKeycode(pHost->down, pTarget->down)
        || !checkHostKeycode(pHost->left, pTarget->left)
        || !checkHostKeycode(pHost->right, pTarget->right)
        || !checkHostKeycode(pHost->f1, pTarget->f1)
        || !checkHostKeycode(pHost->f2, pTarget->f2)
        || !checkHostKeycode(pHost->f3, pTarget->f3)
        || !checkHostKeycode(pHost->f4, pTarget->f4)
        || !checkHostKeycode(pHost->f5, pTarget->f5)
        || !checkHostKeycode(pHost->f6, pTarget->f6)
        || !checkHostKeycode(pHost->f7, pTarget->f7)
        || !checkHostKeycode(pHost->f8, pTarget->f8))
        return 0;
    return 1;
}

static int findHostKBInList(const keyboard_type *pHost,
                            const keyboard_type *pList, int cList)
{
    int i = 0;
    for (; i < cList; ++i)
        if (compKBMaps(pHost, &pList[i]))
            return i;
    return -1;
}

#ifdef DEBUG
static void testFindHostKB(void)
{
    keyboard_type hostBasic =
    { NULL, 1 /* lctrl */, 2, 3, 4, 5, 6, 7 /* up */, 8, 9, 10, 11 /* F1 */,
      12, 13, 14, 15, 16, 17, 18 };
    keyboard_type hostSwapCtrlCaps =
    { NULL, 3 /* lctrl */, 2, 1, 4, 5, 6, 7 /* up */, 8, 9, 10, 11 /* F1 */,
      12, 13, 14, 15, 16, 17, 18 };
    keyboard_type hostEmpty =
    { NULL, 0 /* lctrl */, 0, 0, 0, 0, 0, 0 /* up */, 0, 0, 0, 0 /* F1 */,
      0, 0, 0, 0, 0, 0, 0 };
    keyboard_type hostNearlyEmpty =
    { NULL, 1 /* lctrl */, 0, 0, 0, 0, 0, 0 /* up */, 0, 0, 0, 0 /* F1 */,
      0, 0, 0, 0, 0, 0, 18 };
    keyboard_type hostNearlyRight =
    { NULL, 20 /* lctrl */, 2, 3, 4, 5, 6, 7 /* up */, 8, 9, 10, 11 /* F1 */,
      12, 13, 14, 15, 16, 17, 18 };
    keyboard_type targetList[] = {
        { NULL, 18 /* lctrl */, 17, 16, 15, 14, 13, 12 /* up */, 11, 10, 9,
          8 /* F1 */, 7, 6, 5, 4, 3, 2, 1 },
        { NULL, 1 /* lctrl */, 2, 3, 4, 5, 6, 7 /* up */, 8, 9, 10,
          11 /* F1 */, 12, 13, 14, 15, 16, 17, 18 }
    };

    /* As we don't have assertions here, just printf.  This should *really*
     * never happen. */
    if (   hostBasic.f8 != 18 || hostSwapCtrlCaps.f8 != 18
        || hostNearlyEmpty.f8 != 18 || hostNearlyRight.f8 != 18
        || targetList[0].f8 != 1 || targetList[1].f8 != 18)
        printf("ERROR: testFindHostKB: bad structures\n");
    if (findHostKBInList(&hostBasic, targetList, 2) != 1)
        printf("ERROR: findHostKBInList failed to find a target in a list\n");
    if (findHostKBInList(&hostSwapCtrlCaps, targetList, 2) != 1)
        printf("ERROR: findHostKBInList failed on a ctrl-caps swapped map\n");
    if (findHostKBInList(&hostEmpty, targetList, 2) != -1)
        printf("ERROR: findHostKBInList accepted an empty host map\n");
    if (findHostKBInList(&hostNearlyEmpty, targetList, 2) != 1)
        printf("ERROR: findHostKBInList failed on a partly empty host map\n");
    if (findHostKBInList(&hostNearlyRight, targetList, 2) != -1)
        printf("ERROR: findHostKBInList failed to fail a wrong host map\n");
}
#endif

static unsigned
X11DRV_InitKeyboardByType(Display *display)
{
    keyboard_type hostKB;
    int cMap;

    hostKB.lctrl    = XKeysymToKeycode(display, XK_Control_L);
    hostKB.capslock = XKeysymToKeycode(display, XK_Caps_Lock);
    hostKB.lshift   = XKeysymToKeycode(display, XK_Shift_L);
    hostKB.tab      = XKeysymToKeycode(display, XK_Tab);
    hostKB.esc      = XKeysymToKeycode(display, XK_Escape);
    hostKB.enter    = XKeysymToKeycode(display, XK_Return);
    hostKB.up       = XKeysymToKeycode(display, XK_Up);
    hostKB.down     = XKeysymToKeycode(display, XK_Down);
    hostKB.left     = XKeysymToKeycode(display, XK_Left);
    hostKB.right    = XKeysymToKeycode(display, XK_Right);
    hostKB.f1       = XKeysymToKeycode(display, XK_F1);
    hostKB.f2       = XKeysymToKeycode(display, XK_F2);
    hostKB.f3       = XKeysymToKeycode(display, XK_F3);
    hostKB.f4       = XKeysymToKeycode(display, XK_F4);
    hostKB.f5       = XKeysymToKeycode(display, XK_F5);
    hostKB.f6       = XKeysymToKeycode(display, XK_F6);
    hostKB.f7       = XKeysymToKeycode(display, XK_F7);
    hostKB.f8       = XKeysymToKeycode(display, XK_F8);

#ifdef DEBUG
    testFindHostKB();
#endif
    cMap = findHostKBInList(&hostKB, main_keyboard_type_list,
                                  sizeof(main_keyboard_type_list)
                                / sizeof(main_keyboard_type_list[0]));
#ifdef DEBUG
    /* Assertion */
    if (sizeof(keyc2scan) != sizeof(main_keyboard_type_scans[cMap]))
    {
        printf("ERROR: keyc2scan array size doesn't match main_keyboard_type_scans[]!\n");
        return 0;
    }
#endif
    if (cMap >= 0)
    {
        memcpy(keyc2scan, main_keyboard_type_scans[cMap], sizeof(keyc2scan));
        return 1;
    }
    return 0;
}

/**
 * Checks for the XKB extension, and if it is found initialises the X11 keycode
 * to XT scan code mapping by looking at the XKB names for each keycode.  As it
 * turns out that XKB can return an empty list we make sure that the list holds
 * enough data to be useful to us.
 */
static unsigned
X11DRV_InitKeyboardByXkb(Display *pDisplay)
{
    int major = XkbMajorVersion, minor = XkbMinorVersion;
    XkbDescPtr pKBDesc;
    unsigned cFound = 0;

    if (!XkbLibraryVersion(&major, &minor))
        return 0;
    if (!XkbQueryExtension(pDisplay, NULL, NULL, &major, &minor, NULL))
        return 0;
    pKBDesc = XkbGetKeyboard(pDisplay, XkbAllComponentsMask, XkbUseCoreKbd);
    if (!pKBDesc)
        return 0;
    if (XkbGetNames(pDisplay, XkbKeyNamesMask, pKBDesc) != Success)
        return 0;
    {
        unsigned i, j;

        memset(keyc2scan, 0, sizeof(keyc2scan));
        for (i = pKBDesc->min_key_code; i < pKBDesc->max_key_code; ++i)
            for (j = 0; j < sizeof(xkbMap) / sizeof(xkbMap[0]); ++j)
                if (!memcmp(xkbMap[j].cszName,
                            &pKBDesc->names->keys->name[i * XKB_NAME_SIZE],
                            XKB_NAME_SIZE))
                {
                    keyc2scan[i] = xkbMap[j].uScan;
                    ++cFound;
                    break;
                }
    }
    XkbFreeNames(pKBDesc, XkbKeyNamesMask, True);
    XkbFreeKeyboard(pKBDesc, XkbAllComponentsMask, True);
    return cFound >= 45 ? 1 : 0;
}

/**
 * Initialise the X11 keyboard driver by finding which X11 keycodes correspond
 * to which PC scan codes.  If the keyboard being used is not a PC keyboard,
 * the X11 keycodes will be mapped to the scan codes which the equivalent keys
 * on a PC keyboard would use.
 *
 * We use two algorithms to try to determine the mapping.  See the comments
 * attached to the two algorithm functions (X11DRV_InitKeyboardByLayout and
 * X11DRV_InitKeyboardByType) for descriptions of the algorithms used.  Both
 * functions tell us on return whether they think that they have correctly
 * determined the mapping.  If both functions claim to have determined the
 * mapping correctly, we prefer the second (ByType).  However, if neither does
 * then we prefer the first (ByLayout), as it produces a fuzzy result which is
 * still likely to be partially correct.
 *
 * @warning not re-entrant
 * @returns 1 if the layout found was optimal, 0 if it was not.  This is
 *          for diagnostic purposes
 * @param   display          a pointer to the X11 display
 * @param   byLayoutOK       diagnostic - set to one if detection by layout
 *                           succeeded, and to 0 otherwise
 * @param   byTypeOK         diagnostic - set to one if detection by type
 *                           succeeded, and to 0 otherwise
 * @param   byXkbOK          diagnostic - set to one if detection using XKB
 *                           succeeded, and to 0 otherwise
 * @param   remapScancode    array of tuples that remap the keycode (first
 *                           part) to a scancode (second part)
 * @note  Xkb takes precedence over byType takes precedence over byLayout,
 *        for anyone who wants to log information about which method is in
 *        use.  byLayout is the fallback, as it is likely to be partly usable
 *        even if it doesn't initialise correctly.
 */
unsigned X11DRV_InitKeyboard(Display *display, unsigned *byLayoutOK,
                             unsigned *byTypeOK, unsigned *byXkbOK,
                             int (*remapScancodes)[2])
{
    unsigned byLayout, byType, byXkb;

    byLayout = X11DRV_InitKeyboardByLayout(display);
    if (byLayoutOK)
        *byLayoutOK = byLayout;

    byType = X11DRV_InitKeyboardByType(display);
    if (byTypeOK)
        *byTypeOK = byType;

    byXkb = X11DRV_InitKeyboardByXkb(display);
    if (byXkbOK)
        *byXkbOK = byXkb;

    /* Fall back to the one which did work. */
    if (!byXkb)
    {
        if (byType)
            X11DRV_InitKeyboardByType(display);
        else
            X11DRV_InitKeyboardByLayout(display);
    }

    /* Remap keycodes after initialization. Remapping stops after an
       identity mapping is seen */
    if (remapScancodes != NULL)
        for (; (*remapScancodes)[0] != (*remapScancodes)[1]; remapScancodes++)
            keyc2scan[(*remapScancodes)[0]] = (*remapScancodes)[1];

    return (byLayout || byType || byXkb) ? 1 : 0;
}

/**
 * Returns the keycode to scancode array
 */
unsigned *X11DRV_getKeyc2scan(void)
{
    return keyc2scan;
}

