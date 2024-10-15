/* $Id: keyboard.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - ???
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 *  ROM BIOS for use with Bochs/Plex86/QEMU emulation environment
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */


#include <stdint.h>
#include "inlines.h"
#include "biosint.h"

#if DEBUG_INT16
#  define BX_DEBUG_INT16(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_INT16(...)
#endif

extern  void    post(void);
#pragma aux post "*";

void jmp_post(void);
#pragma aux jmp_post = "jmp far ptr post" aborts;

extern void eoi_master_pic(void);    /* in assembly code */
#pragma aux eoi_master_pic "*";

/* Manually save/restore BP around invoking user Ctrl-Break handler.
 * The handler could conceivably clobber BP and the compiler does not
 * believe us when we say 'modify [bp]' (BP is considered unalterable).
 */
void int_1b(void);
#pragma aux int_1b =    \
    "push bp"           \
    "int 1Bh"           \
    "pop bp"            \
    value [bp] modify [bp];


#define none 0
#define MAX_SCAN_CODE 0x58

struct {
    uint16_t    normal;
    uint16_t    shift;
    uint16_t    control;
    uint16_t    alt;
    uint8_t     lock_flags;
} static const scan_to_scanascii[MAX_SCAN_CODE + 1] = {
    {   none,   none,   none,   none, none },
    { 0x011b, 0x011b, 0x011b, 0x0100, none }, /* escape */
    { 0x0231, 0x0221,   none, 0x7800, none }, /* 1! */
    { 0x0332, 0x0340, 0x0300, 0x7900, none }, /* 2@ */
    { 0x0433, 0x0423,   none, 0x7a00, none }, /* 3# */
    { 0x0534, 0x0524,   none, 0x7b00, none }, /* 4$ */
    { 0x0635, 0x0625,   none, 0x7c00, none }, /* 5% */
    { 0x0736, 0x075e, 0x071e, 0x7d00, none }, /* 6^ */
    { 0x0837, 0x0826,   none, 0x7e00, none }, /* 7& */
    { 0x0938, 0x092a,   none, 0x7f00, none }, /* 8* */
    { 0x0a39, 0x0a28,   none, 0x8000, none }, /* 9( */
    { 0x0b30, 0x0b29,   none, 0x8100, none }, /* 0) */
    { 0x0c2d, 0x0c5f, 0x0c1f, 0x8200, none }, /* -_ */
    { 0x0d3d, 0x0d2b,   none, 0x8300, none }, /* =+ */
    { 0x0e08, 0x0e08, 0x0e7f,   none, none }, /* backspace */
    { 0x0f09, 0x0f00,   none,   none, none }, /* tab */
    { 0x1071, 0x1051, 0x1011, 0x1000, 0x40 }, /* Q */
    { 0x1177, 0x1157, 0x1117, 0x1100, 0x40 }, /* W */
    { 0x1265, 0x1245, 0x1205, 0x1200, 0x40 }, /* E */
    { 0x1372, 0x1352, 0x1312, 0x1300, 0x40 }, /* R */
    { 0x1474, 0x1454, 0x1414, 0x1400, 0x40 }, /* T */
    { 0x1579, 0x1559, 0x1519, 0x1500, 0x40 }, /* Y */
    { 0x1675, 0x1655, 0x1615, 0x1600, 0x40 }, /* U */
    { 0x1769, 0x1749, 0x1709, 0x1700, 0x40 }, /* I */
    { 0x186f, 0x184f, 0x180f, 0x1800, 0x40 }, /* O */
    { 0x1970, 0x1950, 0x1910, 0x1900, 0x40 }, /* P */
    { 0x1a5b, 0x1a7b, 0x1a1b,   none, none }, /* [{ */
    { 0x1b5d, 0x1b7d, 0x1b1d,   none, none }, /* ]} */
    { 0x1c0d, 0x1c0d, 0x1c0a,   none, none }, /* Enter */
    {   none,   none,   none,   none, none }, /* L Ctrl */
    { 0x1e61, 0x1e41, 0x1e01, 0x1e00, 0x40 }, /* A */
    { 0x1f73, 0x1f53, 0x1f13, 0x1f00, 0x40 }, /* S */
    { 0x2064, 0x2044, 0x2004, 0x2000, 0x40 }, /* D */
    { 0x2166, 0x2146, 0x2106, 0x2100, 0x40 }, /* F */
    { 0x2267, 0x2247, 0x2207, 0x2200, 0x40 }, /* G */
    { 0x2368, 0x2348, 0x2308, 0x2300, 0x40 }, /* H */
    { 0x246a, 0x244a, 0x240a, 0x2400, 0x40 }, /* J */
    { 0x256b, 0x254b, 0x250b, 0x2500, 0x40 }, /* K */
    { 0x266c, 0x264c, 0x260c, 0x2600, 0x40 }, /* L */
    { 0x273b, 0x273a,   none,   none, none }, /* ;: */
    { 0x2827, 0x2822,   none,   none, none }, /* '" */
    { 0x2960, 0x297e,   none,   none, none }, /* `~ */
    {   none,   none,   none,   none, none }, /* L shift */
    { 0x2b5c, 0x2b7c, 0x2b1c,   none, none }, /* |\ */
    { 0x2c7a, 0x2c5a, 0x2c1a, 0x2c00, 0x40 }, /* Z */
    { 0x2d78, 0x2d58, 0x2d18, 0x2d00, 0x40 }, /* X */
    { 0x2e63, 0x2e43, 0x2e03, 0x2e00, 0x40 }, /* C */
    { 0x2f76, 0x2f56, 0x2f16, 0x2f00, 0x40 }, /* V */
    { 0x3062, 0x3042, 0x3002, 0x3000, 0x40 }, /* B */
    { 0x316e, 0x314e, 0x310e, 0x3100, 0x40 }, /* N */
    { 0x326d, 0x324d, 0x320d, 0x3200, 0x40 }, /* M */
    { 0x332c, 0x333c,   none,   none, none }, /* ,< */
    { 0x342e, 0x343e,   none,   none, none }, /* .> */
    { 0x352f, 0x353f,   none,   none, none }, /* /? */
    {   none,   none,   none,   none, none }, /* R Shift */
    { 0x372a, 0x372a,   none,   none, none }, /* * */
    {   none,   none,   none,   none, none }, /* L Alt */
    { 0x3920, 0x3920, 0x3920, 0x3920, none }, /* space */
    {   none,   none,   none,   none, none }, /* caps lock */
    { 0x3b00, 0x5400, 0x5e00, 0x6800, none }, /* F1 */
    { 0x3c00, 0x5500, 0x5f00, 0x6900, none }, /* F2 */
    { 0x3d00, 0x5600, 0x6000, 0x6a00, none }, /* F3 */
    { 0x3e00, 0x5700, 0x6100, 0x6b00, none }, /* F4 */
    { 0x3f00, 0x5800, 0x6200, 0x6c00, none }, /* F5 */
    { 0x4000, 0x5900, 0x6300, 0x6d00, none }, /* F6 */
    { 0x4100, 0x5a00, 0x6400, 0x6e00, none }, /* F7 */
    { 0x4200, 0x5b00, 0x6500, 0x6f00, none }, /* F8 */
    { 0x4300, 0x5c00, 0x6600, 0x7000, none }, /* F9 */
    { 0x4400, 0x5d00, 0x6700, 0x7100, none }, /* F10 */
    {   none,   none,   none,   none, none }, /* Num Lock */
    {   none,   none,   none,   none, none }, /* Scroll Lock */
    { 0x4700, 0x4737, 0x7700,   none, 0x20 }, /* 7 Home */
    { 0x4800, 0x4838,   none,   none, 0x20 }, /* 8 UP */
    { 0x4900, 0x4939, 0x8400,   none, 0x20 }, /* 9 PgUp */
    { 0x4a2d, 0x4a2d,   none,   none, none }, /* - */
    { 0x4b00, 0x4b34, 0x7300,   none, 0x20 }, /* 4 Left */
    { 0x4c00, 0x4c35,   none,   none, 0x20 }, /* 5 */
    { 0x4d00, 0x4d36, 0x7400,   none, 0x20 }, /* 6 Right */
    { 0x4e2b, 0x4e2b,   none,   none, none }, /* + */
    { 0x4f00, 0x4f31, 0x7500,   none, 0x20 }, /* 1 End */
    { 0x5000, 0x5032,   none,   none, 0x20 }, /* 2 Down */
    { 0x5100, 0x5133, 0x7600,   none, 0x20 }, /* 3 PgDn */
    { 0x5200, 0x5230,   none,   none, 0x20 }, /* 0 Ins */
    { 0x5300, 0x532e,   none,   none, 0x20 }, /* Del */
    {   none,   none,   none,   none, none },
    {   none,   none,   none,   none, none },
    { 0x565c, 0x567c,   none,   none, none }, /* \| */
    { 0x8500, 0x8700, 0x8900, 0x8b00, none }, /* F11 */
    { 0x8600, 0x8800, 0x8a00, 0x8c00, none }  /* F12 */
};


/* Keyboard initialization. */

//--------------------------------------------------------------------------
// keyboard_panic
//--------------------------------------------------------------------------
void keyboard_panic(uint16_t status)
{
    // If you're getting a 993 keyboard panic here,
    // please see the comment in keyboard_init

    BX_PANIC("Keyboard error:%u\n",status);
}


//--------------------------------------------------------------------------
// keyboard_init
//--------------------------------------------------------------------------
// this file is based on LinuxBIOS implementation of keyboard.c
// could convert to #asm to gain space
void BIOSCALL keyboard_init(void)
{
    uint16_t    max;

    /* ------------------- controller side ----------------------*/
    /* send cmd = 0xAA, self test 8042 */
    outb(0x64, 0xaa);

    /* Wait until buffer is empty */
    max=0xffff;
    while ( (inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x00);
    if (max==0x0) keyboard_panic(00);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x01);
    if (max==0x0) keyboard_panic(01);

    /* read self-test result, 0x55 should be returned from 0x60 */
    if ((inb(0x60) != 0x55)){
        keyboard_panic(991);
    }

    /* send cmd = 0xAB, keyboard interface test */
    outb(0x64,0xab);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x10);
    if (max==0x0) keyboard_panic(10);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x11);
    if (max==0x0) keyboard_panic(11);

    /* read keyboard interface test result, */
    /* 0x00 should be returned form 0x60 */
    if ((inb(0x60) != 0x00)) {
        keyboard_panic(992);
    }

    /* ------------------- keyboard side ------------------------*/
    /* reset keyboard and self test  (keyboard side) */
    /* also enables the keyboard interface */
    outb(0x60, 0xff);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x20);
    if (max==0x0) keyboard_panic(20);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x21);
    if (max==0x0) keyboard_panic(21);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(993);
    }

    /* Wait for reset to complete */
    while ( (inb(0x64) & 0x01) == 0 ) outb(0x80, 0x31);

    if ((inb(0x60) != 0xaa && inb(0x60) != 0xaa)) {
        keyboard_panic(994);
    }

    /* Disable keyboard */
    outb(0x60, 0xf5);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x40);
    if (max==0x0) keyboard_panic(40);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x41);
    if (max==0x0) keyboard_panic(41);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(995);
    }

    /* Write Keyboard Mode */
    outb(0x64, 0x60);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x50);
    if (max==0x0) keyboard_panic(50);

    /* send cmd: scan code convert, disable mouse, enable IRQ 1 */
    outb(0x60, 0x65);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x60);
    if (max==0x0) keyboard_panic(60);

    /* Enable keyboard */
    outb(0x60, 0xf4);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x70);
    if (max==0x0) keyboard_panic(70);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x71);
    if (max==0x0) keyboard_panic(70);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(996);
    }

    /* Enable aux interface */
    outb(0x64,0xa8);

    /* While we're here, disable the A20 gate. Required for
     * compatibility with the IBM PC and DOS.
     */
    set_enable_a20(0);
}


unsigned int enqueue_key(uint8_t scan_code, uint8_t ascii_code)
{
    uint16_t    buffer_start, buffer_end, buffer_head, buffer_tail, temp_tail;

#if VBOX_BIOS_CPU >= 80286
    buffer_start = read_word(0x0040, 0x0080);
    buffer_end   = read_word(0x0040, 0x0082);
#else
    buffer_start = 0x001E;
    buffer_end   = 0x003E;
#endif

    buffer_head = read_word(0x0040, 0x001A);
    buffer_tail = read_word(0x0040, 0x001C);

    temp_tail = buffer_tail;
    buffer_tail += 2;
    if (buffer_tail >= buffer_end)
        buffer_tail = buffer_start;

    if (buffer_tail == buffer_head)
        return(0);

    write_byte(0x0040, temp_tail, ascii_code);
    write_byte(0x0040, temp_tail+1, scan_code);
    write_word(0x0040, 0x001C, buffer_tail);
    return(1);
}


/* Keyboard hardware interrupt handler. */
/// @todo should this be declared as taking arguments at all?
void BIOSCALL int09_function(uint16_t ES, uint16_t DI, uint16_t SI, uint16_t BP, uint16_t SP,
                             uint16_t BX, uint16_t DX, uint16_t CX, uint16_t AX)
{
    uint8_t scancode, asciicode, shift_flags;
    uint8_t mf2_flags, mf2_state, flag;

    //
    // DS has been set to F000 before call
    //


    scancode = GET_AL();

    if (scancode == 0) {
        BX_INFO("KBD: int09 handler: AL=0\n");
        return;
    }

    mf2_flags = read_byte(0x0040, 0x18);
    mf2_state = read_byte(0x0040, 0x96);
    shift_flags = read_byte(0x0040, 0x17);
    asciicode = 0;

    switch (scancode) {
    case 0x3a: /* Caps Lock press */
        shift_flags ^= 0x40;
        write_byte(0x0040, 0x17, shift_flags);
        mf2_flags |= 0x40;
        write_byte(0x0040, 0x18, mf2_flags);
        break;
    case 0xba: /* Caps Lock release */
        mf2_flags &= ~0x40;
        write_byte(0x0040, 0x18, mf2_flags);
        break;

    case 0x2a: /* L Shift press */
    case 0xaa: /* L Shift release */
    case 0x36: /* R Shift press */
    case 0xb6: /* R Shift release */
        /* If this was an extended (i.e. faked) key, leave flags alone. */
        if (!(mf2_state & 0x02)) {
            flag = (scancode & 0x7f) == 0x2a ? 0x02 : 0x01;
            if (scancode & 0x80)
                shift_flags &= ~flag;
            else
                shift_flags |= flag;
            write_byte(0x0040, 0x17, shift_flags);
        }
        break;

    case 0x1d: /* Ctrl press */
        if ((mf2_state & 0x01) == 0) {
            shift_flags |= 0x04;
            write_byte(0x0040, 0x17, shift_flags);
            if (mf2_state & 0x02) {
                mf2_state |= 0x04;
                write_byte(0x0040, 0x96, mf2_state);
            } else {
                mf2_flags |= 0x01;
                write_byte(0x0040, 0x18, mf2_flags);
            }
        }
        break;
    case 0x9d: /* Ctrl release */
        if ((mf2_state & 0x01) == 0) {
            shift_flags &= ~0x04;
            write_byte(0x0040, 0x17, shift_flags);
            if (mf2_state & 0x02) {
                mf2_state &= ~0x04;
                write_byte(0x0040, 0x96, mf2_state);
            } else {
                mf2_flags &= ~0x01;
                write_byte(0x0040, 0x18, mf2_flags);
            }
        }
        break;

    case 0x38: /* Alt press */
        shift_flags |= 0x08;
        write_byte(0x0040, 0x17, shift_flags);
        if (mf2_state & 0x02) {
            mf2_state |= 0x08;
            write_byte(0x0040, 0x96, mf2_state);
        } else {
            mf2_flags |= 0x02;
            write_byte(0x0040, 0x18, mf2_flags);
        }
        break;
    case 0xb8: /* Alt release */
        shift_flags &= ~0x08;
        write_byte(0x0040, 0x17, shift_flags);
        if (mf2_state & 0x02) {
            mf2_state &= ~0x08;
            write_byte(0x0040, 0x96, mf2_state);
        } else {
            mf2_flags &= ~0x02;
            write_byte(0x0040, 0x18, mf2_flags);
        }
        break;

    case 0x45: /* Num Lock/Pause press */
        if ((mf2_state & 0x03) == 0) {
            /* Num Lock */
            mf2_flags |= 0x20;
            write_byte(0x0040, 0x18, mf2_flags);
            shift_flags ^= 0x20;
            write_byte(0x0040, 0x17, shift_flags);
        } else {
            /* Pause */
            mf2_flags |= 0x08;  /* Set the suspend flag */
            write_byte(0x0040, 0x18, mf2_flags);

            /* Enable keyboard and send EOI. */
            outp(0x64, 0xae);
            eoi_master_pic();

            while (read_byte(0x0040, 0x18) & 0x08)
                ;   /* Hold on and wait... */

            /// @todo We will send EOI again (and enable keyboard) on the way out; we shouldn't
        }
        break;
    case 0xc5: /* Num Lock/Pause release */
        if ((mf2_state & 0x03) == 0) {
            mf2_flags &= ~0x20;
            write_byte(0x0040, 0x18, mf2_flags);
        }
        break;

    case 0x46: /* Scroll Lock/Break press */
        if (mf2_state & 0x02) { /* E0 prefix? */
            /* Zap the keyboard buffer. */
            write_word(0x0040, 0x001c, read_word(0x0040, 0x001a));

            write_byte(0x0040, 0x71, 0x80); /* Set break flag */
            outp(0x64, 0xae);               /* Enable keyboard */
            int_1b();                       /* Invoke user handler */
            enqueue_key(0, 0);              /* Dummy key press*/
        } else {
            mf2_flags |= 0x10;
            write_byte(0x0040, 0x18, mf2_flags);
            shift_flags ^= 0x10;
            write_byte(0x0040, 0x17, shift_flags);
        }
        break;

    case 0xc6: /* Scroll Lock/Break release */
        if (!(mf2_state & 0x02)) {  /* Only if no E0 prefix */
            mf2_flags &= ~0x10;
            write_byte(0x0040, 0x18, mf2_flags);
        }
        break;

    case 0x54: /* SysRq press */
        if (!(mf2_flags & 0x04)) {  /* If not already down */
            mf2_flags |= 0x04;
            write_byte(0x0040, 0x18, mf2_flags);
            /// @todo EOI/enable kbd/enable interrupts/call INT 15h/8500h
        }
        break;

    case 0xd4: /* SysRq release */
        mf2_flags &= ~0x04;
        write_byte(0x0040, 0x18, mf2_flags);
        /// @todo EOI/enable kbd/enable interrupts/call INT 15h/8501h
        break;

    case 0x53: /* Del press */
        if ((shift_flags & 0x0c) == 0x0c) {
            /* Indicate a warm boot. */
            write_word(0x0040,0x0072, 0x1234);
            jmp_post();
        }
        /* fall through */

    default:
        /* Check if suspend flag set. */
        if (mf2_flags & 0x08) {
            /* Pause had been pressed. Clear suspend flag and do nothing. */
            mf2_flags &= ~0x08;
            write_byte(0x0040, 0x18, mf2_flags);
            return;
        }

        if (scancode & 0x80) {
            /* Set ack/resend flags if appropriate. */
            if (scancode == 0xFA) {
                flag = read_byte(0x0040, 0x97) | 0x10;
                write_byte(0x0040, 0x97, flag);
            } else if (scancode == 0xFE) {
                flag = read_byte(0x0040, 0x97) | 0x20;
                write_byte(0x0040, 0x97, flag);
            }
            break; /* toss key releases ... */
        }
        if (scancode > MAX_SCAN_CODE) {
            BX_INFO("KBD: int09h_handler(): unknown scancode read: 0x%02x!\n", scancode);
            return;
        }
        if (shift_flags & 0x08) { /* ALT */
            asciicode = scan_to_scanascii[scancode].alt;
            scancode = scan_to_scanascii[scancode].alt >> 8;
        } else if (shift_flags & 0x04) { /* CONTROL */
            asciicode = scan_to_scanascii[scancode].control;
            scancode = scan_to_scanascii[scancode].control >> 8;
        } else if (((mf2_state & 0x02) > 0) && ((scancode >= 0x47) && (scancode <= 0x53))) {
            /* extended keys handling */
            asciicode = 0xe0;
            scancode = scan_to_scanascii[scancode].normal >> 8;
        } else if (shift_flags & 0x03) { /* LSHIFT + RSHIFT */
            /* check if lock state should be ignored
             * because a SHIFT key are pressed */

            if (shift_flags & scan_to_scanascii[scancode].lock_flags) {
                asciicode = scan_to_scanascii[scancode].normal;
                scancode  = scan_to_scanascii[scancode].normal >> 8;
            } else {
                asciicode = scan_to_scanascii[scancode].shift;
                scancode  = scan_to_scanascii[scancode].shift >> 8;
            }
        } else {
            /* check if lock is on */
            if (shift_flags & scan_to_scanascii[scancode].lock_flags) {
                asciicode = scan_to_scanascii[scancode].shift;
                scancode = scan_to_scanascii[scancode].shift >> 8;
            } else {
                asciicode = scan_to_scanascii[scancode].normal;
                scancode = scan_to_scanascii[scancode].normal >> 8;
            }
        }
        if (scancode==0 && asciicode==0) {
            BX_INFO("KBD: int09h_handler(): scancode & asciicode are zero?\n");
        }
        enqueue_key(scancode, asciicode);
        break;
    }
    if ((scancode & 0x7f) != 0x1d) {
        mf2_state &= ~0x01;
    }
    mf2_state &= ~0x02;
    write_byte(0x0040, 0x96, mf2_state);
}

unsigned int dequeue_key(uint8_t __far *scan_code, uint8_t __far *ascii_code, unsigned incr)
{
    uint16_t    buffer_start, buffer_end, buffer_head, buffer_tail;
    uint8_t     acode, scode;

#if VBOX_BIOS_CPU >= 80286
    buffer_start = read_word(0x0040, 0x0080);
    buffer_end   = read_word(0x0040, 0x0082);
#else
    buffer_start = 0x001E;
    buffer_end   = 0x003E;
#endif

    buffer_head = read_word(0x0040, 0x001a);
    buffer_tail = read_word(0x0040, 0x001c);

    if (buffer_head != buffer_tail) {
        acode = read_byte(0x0040, buffer_head);
        scode = read_byte(0x0040, buffer_head+1);
        *ascii_code = acode;
        *scan_code  = scode;
        BX_DEBUG_INT16("dequeue_key: ascii=%02x scan=%02x \n", acode, scode);

        if (incr) {
            buffer_head += 2;
            if (buffer_head >= buffer_end)
                buffer_head = buffer_start;
            write_word(0x0040, 0x001a, buffer_head);
        }
        return(1);
    }
    else {
        return(0);
    }
}


/// @todo move somewhere else?
#define AX      r.gr.u.r16.ax
#define BX      r.gr.u.r16.bx
#define CX      r.gr.u.r16.cx
#define DX      r.gr.u.r16.dx
#define SI      r.gr.u.r16.si
#define DI      r.gr.u.r16.di
#define BP      r.gr.u.r16.bp
#define SP      r.gr.u.r16.sp
#define FLAGS   r.ra.flags.u.r16.flags
#define IFLGS   r.ifl

/* Interrupt 16h service implementation. */

void BIOSCALL int16_function(volatile kbd_regs_t r)
{
    uint8_t     scan_code, ascii_code, shift_flags, led_flags, count;
    uint16_t    kbd_code, max;

    BX_DEBUG_INT16("int16: AX=%04x BX=%04x CX=%04x DX=%04x \n", AX, BX, CX, DX);

    shift_flags = read_byte(0x0040, 0x17);
    led_flags   = read_byte(0x0040, 0x97);
    if ((((shift_flags >> 4) & 0x07) ^ (led_flags & 0x07)) != 0) {
        int_disable();    /// @todo interrupts should be disabled already??
        outb(0x60, 0xed);
        while ((inb(0x64) & 0x01) == 0) outb(0x80, 0x21);
        if ((inb(0x60) == 0xfa)) {
            led_flags &= 0xc8;
            led_flags |= ((shift_flags >> 4) & 0x07);
            outb(0x60, led_flags & 0x07);
            while ((inb(0x64) & 0x01) == 0)
                outb(0x80, 0x21);
            inb(0x60);
            write_byte(0x0040, 0x97, led_flags);
        }
        int_enable();
    }

    switch (GET_AH()) {
    case 0x00: /* read keyboard input */
        if ( !dequeue_key(&scan_code, &ascii_code, 1) ) {
            BX_PANIC("KBD: int16h: out of keyboard input\n");
        }
        if (scan_code !=0 && ascii_code == 0xF0)
            ascii_code = 0;
        else if (ascii_code == 0xE0)
            ascii_code = 0;
        AX = (scan_code << 8) | ascii_code;
        break;

    case 0x01: /* check keyboard status */
        /* Enable interrupts, preserve most flags. Some callers depend on that! */
        FLAGS = IFLGS;
        if ( !dequeue_key(&scan_code, &ascii_code, 0) ) {
            SET_ZF();
            return;
        }
        if (scan_code !=0 && ascii_code == 0xF0)
            ascii_code = 0;
        else if (ascii_code == 0xE0)
            ascii_code = 0;
        AX = (scan_code << 8) | ascii_code;
        CLEAR_ZF();
        break;

    case 0x02: /* get shift flag status */
        shift_flags = read_byte(0x0040, 0x17);
        SET_AL(shift_flags);
        break;

    case 0x05: /* store key-stroke into buffer */
        if ( !enqueue_key(GET_CH(), GET_CL()) ) {
            SET_AL(1);
        }
        else {
            SET_AL(0);
        }
        break;

    case 0x09: /* GET KEYBOARD FUNCTIONALITY */
        // bit Bochs Description
        //  7    0   reserved
        //  6    0   INT 16/AH=20h-22h supported (122-key keyboard support)
        //  5    1   INT 16/AH=10h-12h supported (enhanced keyboard support)
        //  4    1   INT 16/AH=0Ah supported
        //  3    0   INT 16/AX=0306h supported
        //  2    0   INT 16/AX=0305h supported
        //  1    0   INT 16/AX=0304h supported
        //  0    0   INT 16/AX=0300h supported
        //
        SET_AL(0x30);
        break;

    case 0x0A: /* GET KEYBOARD ID */
        count = 2;
        kbd_code = 0x0;
        /// @todo Might be better to just mask the KB interrupt
        int_disable();
        outb(0x60, 0xf2);
        /* Wait for data */
        max=0xffff;
        while ( ((inb(0x64) & 0x01) == 0) && (--max>0) )
            inb(0x80);
        if (max>0x0) {
            if ((inb(0x60) == 0xfa)) {
                do {
                    max=0xffff;
                    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) )
                        inb(0x80);
                    if (max>0x0) {
                        kbd_code >>= 8;
                        kbd_code |= (inb(0x60) << 8);
                    }
                } while (--count>0);
            }
        }
        BX=kbd_code;
        break;

    case 0x10: /* read MF-II keyboard input */
        if ( !dequeue_key(&scan_code, &ascii_code, 1) ) {
            BX_PANIC("KBD: int16h: out of keyboard input\n");
        }
        if (scan_code !=0 && ascii_code == 0xF0)
            ascii_code = 0;
        AX = (scan_code << 8) | ascii_code;
        break;

    case 0x11: /* check MF-II keyboard status */
        /* Enable interrupts, preserve most flags. Some callers depend on that! */
        FLAGS = IFLGS;
        if ( !dequeue_key(&scan_code, &ascii_code, 0) ) {
            SET_ZF();
            return;
        }
        if (scan_code !=0 && ascii_code == 0xF0)
            ascii_code = 0;
        AX = (scan_code << 8) | ascii_code;
        CLEAR_ZF();
        break;

    case 0x12: /* get extended keyboard status */
        shift_flags = read_byte(0x0040, 0x17);
        SET_AL(shift_flags);
        shift_flags = read_byte(0x0040, 0x18) & 0x73;
        shift_flags |= read_byte(0x0040, 0x96) & 0x0c;
        SET_AH(shift_flags);
        BX_DEBUG_INT16("int16: func 12 sending %04x\n",AX);
        break;

    case 0x92: /* keyboard capability check called by DOS 5.0+ keyb */
        SET_AH(0x80); // function int16 ah=0x10-0x12 supported
        break;

    case 0xA2: /* 122 keys capability check called by DOS 5.0+ keyb */
        // don't change AH : function int16 ah=0x20-0x22 NOT supported
        break;

    /// @todo what's the point of handling this??
#if 0
    case 0x6F:
        if (GET_AL() == 0x08)
        SET_AH(0x02); // unsupported, aka normal keyboard
#endif

    default:
        BX_INFO("KBD: unsupported int 16h function %02x\n", GET_AH());
        BX_INFO("AX=%04x BX=%04x CX=%04x DX=%04x \n", AX, BX, CX, DX);
    }
    BX_DEBUG_INT16("int16ex: AX=%04x BX=%04x CX=%04x DX=%04x \n", AX, BX, CX, DX);
}
