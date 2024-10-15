/* $Id: ps2mouse.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include "biosint.h"
#include "inlines.h"


#if DEBUG_INT15_MS
#  define BX_DEBUG_INT15_MS(...)    BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_INT15_MS(...)
#endif

#if DEBUG_INT74
#  define BX_DEBUG_INT74(...)       BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_INT74(...)
#endif

#if BX_USE_PS2_MOUSE

static  const char  panic_msg_keyb_buffer_full[] = "%s: keyboard input buffer full\n";

uint8_t send_to_mouse_ctrl(uint8_t sendbyte)
{
    BX_DEBUG_INT15_MS("send %02x to mouse:\n", sendbyte);
    // wait for chance to write to ctrl
    if (inb(0x64) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full,"sendmouse");
    outb(0x64, 0xD4);
    outb(0x60, sendbyte);
    return(0);
}


uint8_t get_mouse_data(uint8_t __far *data)
{
    int         retries = 10000;    /* ~150ms timeout */
    uint8_t     response;

    while ((inb(0x64) & 0x21) != 0x21 && retries)
    {
        /* Wait until the 15us refresh counter toggles. */
        response = inb(0x61) & 0x10;
        while((inb(0x61) & 0x10) == response)
            ;
        --retries;
    }

    if (!retries)
        return(1);

    response = inb(0x60);
    *data = response;
    return(0);
}

void set_kbd_command_byte(uint8_t command_byte)
{
    if (inb(0x64) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full,"setkbdcomm");

    outb(0x64, 0x60); // write command byte
    outb(0x60, command_byte);
}


void BIOSCALL int74_function(volatile uint16_t make_farcall, volatile uint16_t Z,
                             volatile uint16_t Y, volatile uint16_t X, volatile uint16_t status)
{
    uint16_t    ebda_seg=read_word(0x0040,0x000E);
    uint8_t     in_byte, index, package_count;
    uint8_t     mouse_flags_1, mouse_flags_2;

    BX_DEBUG_INT74("entering int74_function\n");
    make_farcall = 0;

    in_byte = inb(0x64);
    if ( (in_byte & 0x21) != 0x21 ) {
        return;
    }
    in_byte = inb(0x60);
    BX_DEBUG_INT74("int74: read byte %02x\n", in_byte);

    mouse_flags_1 = read_byte(ebda_seg, 0x0026);
    mouse_flags_2 = read_byte(ebda_seg, 0x0027);

    if ( (mouse_flags_2 & 0x80) != 0x80 ) {
        return;
    }

    package_count = mouse_flags_2 & 0x07;
    index = mouse_flags_1 & 0x07;
    write_byte(ebda_seg, 0x28 + index, in_byte);

    if ( index >= package_count ) {
        BX_DEBUG_INT74("int74_function: make_farcall=1\n");
        status = read_byte(ebda_seg, 0x0028 + 0);
        X      = read_byte(ebda_seg, 0x0028 + 1);
        Y      = read_byte(ebda_seg, 0x0028 + 2);
        Z      = 0;
        mouse_flags_1 = 0;
        // check if far call handler installed
        if (mouse_flags_2 & 0x80)
            make_farcall = 1;
    }
    else {
        mouse_flags_1++;
    }
    write_byte(ebda_seg, 0x0026, mouse_flags_1);
}

void BIOSCALL int15_function_mouse(pusha_regs_t regs, uint16_t ES, uint16_t DS, volatile uint16_t FLAGS)
{
    uint16_t    ebda_seg=read_word(0x0040,0x000E);
    uint8_t     mouse_flags_1, mouse_flags_2;
    uint16_t    mouse_driver_seg;
    uint16_t    mouse_driver_offset;
    uint8_t     mouse_cmd;
    uint8_t     ret, mouse_data1, mouse_data2, mouse_data3;

    BX_DEBUG_INT15_MS("int15 AX=%04x\n",regs.u.r16.ax);

    // Return Codes status in AH
    // =========================
    // 00: success
    // 01: invalid subfunction (AL > 7)
    // 02: invalid input value (out of allowable range)
    // 03: interface error
    // 04: resend command received from mouse controller,
    //     device driver should attempt command again
    // 05: cannot enable mouse, since no far call has been installed
    // 80/86: mouse service not implemented

    if (regs.u.r8.al > 7) {
        BX_DEBUG_INT15_MS("unsupported subfn\n");
        // invalid function
        SET_CF();
        regs.u.r8.ah = 1;
        return;
    }

    // Valid subfn; disable AUX input and IRQ12, assume no error
    set_kbd_command_byte(0x65);
    CLEAR_CF();
    regs.u.r8.ah = 0;

    switch (regs.u.r8.al) {
    case 0: // Disable/Enable Mouse
        BX_DEBUG_INT15_MS("case 0: ");
        if (regs.u.r8.bh > 1) {
            BX_DEBUG_INT15_MS("INT 15h C2 AL=0, BH=%02x\n", (unsigned) regs.u.r8.bh);
            // invalid subfunction
            SET_CF();
            regs.u.r8.ah = 1;
            break;
        }
        mouse_flags_2 = read_byte(ebda_seg, 0x0027);
        if ( (mouse_flags_2 & 0x80) == 0 ) {
            BX_DEBUG_INT15_MS("INT 15h C2 Enable/Disable Mouse, no far call handler\n");
            SET_CF();
            regs.u.r8.ah = 5; // no far call installed
            break;
        }
        if (regs.u.r8.bh == 0) {
            BX_DEBUG_INT15_MS("Disable Mouse\n");
            mouse_cmd = 0xF5;   // disable mouse command
        } else {
            BX_DEBUG_INT15_MS("Enable Mouse\n");
            mouse_cmd = 0xF4;   // enable mouse command
        }

        ret = send_to_mouse_ctrl(mouse_cmd);  // disable mouse command
        if (ret == 0) {
            ret = get_mouse_data(&mouse_data1);
            if ( (ret == 0) || (mouse_data1 == 0xFA) ) {
                // success
                break;
            }
        }

        // interface error
        SET_CF();
        regs.u.r8.ah = 3;
        break;

    case 5: // Initialize Mouse
        // Valid package sizes are 1 to 8
        if ( (regs.u.r8.bh < 1) || (regs.u.r8.bh > 8) ) {
            SET_CF();
            regs.u.r8.ah = 2; // invalid input
            break;
        }
        mouse_flags_2 = read_byte(ebda_seg, 0x0027);
        mouse_flags_2 = (mouse_flags_2 & 0xf8) | (regs.u.r8.bh - 1);
        write_byte(ebda_seg, 0x0027, mouse_flags_2);
        // fall through!

    case 1: // Reset Mouse
        BX_DEBUG_INT15_MS("case 1 or 5:\n");
        // clear current package byte index
        mouse_flags_1 = read_byte(ebda_seg, 0x0026);
        mouse_flags_1 = mouse_flags_1 & 0xf8;
        write_byte(ebda_seg, 0x0026, mouse_flags_1);
        ret = send_to_mouse_ctrl(0xFF); // reset mouse command
        if (ret == 0) {
            ret = get_mouse_data(&mouse_data3);
            // if no mouse attached, it will return RESEND
            if (mouse_data3 == 0xfe) {
                SET_CF();
                regs.u.r8.ah = 4; // resend
                break;
            }
            if (mouse_data3 != 0xfa)
                BX_PANIC("Mouse reset returned %02x (should be ack)\n", (unsigned)mouse_data3);
            if ( ret == 0 ) {
                ret = get_mouse_data(&mouse_data1);
                if ( ret == 0 ) {
                    ret = get_mouse_data(&mouse_data2);
                    if ( ret == 0 ) {
                        // success
                        regs.u.r8.bl = mouse_data1;
                        regs.u.r8.bh = mouse_data2;
                        break;
                    }
                }
            }
        }

        // interface error
        SET_CF();
        regs.u.r8.ah = 3;
        break;

    case 2: // Set Sample Rate
        BX_DEBUG_INT15_MS("case 2:\n");
        switch (regs.u.r8.bh) {
        case 0: mouse_data1 = 10; break; //  10 reports/sec
        case 1: mouse_data1 = 20; break; //  20 reports/sec
        case 2: mouse_data1 = 40; break; //  40 reports/sec
        case 3: mouse_data1 = 60; break; //  60 reports/sec
        case 4: mouse_data1 = 80; break; //  80 reports/sec
        case 5: mouse_data1 = 100; break; // 100 reports/sec (default)
        case 6: mouse_data1 = 200; break; // 200 reports/sec
        default: mouse_data1 = 0;
        }
        if (mouse_data1 > 0) {
            ret = send_to_mouse_ctrl(0xF3); // set sample rate command
            if (ret == 0) {
                ret = get_mouse_data(&mouse_data2);
                ret = send_to_mouse_ctrl(mouse_data1);
                ret = get_mouse_data(&mouse_data2);
                // success
            } else {
                // interface error
                SET_CF();
                regs.u.r8.ah = 3;
            }
        } else {
            // invalid input
            SET_CF();
            regs.u.r8.ah = 2;
        }
        break;

    case 3: // Set Resolution
        BX_DEBUG_INT15_MS("case 3:\n");
        // BX:
        //      0 =  25 dpi, 1 count  per millimeter
        //      1 =  50 dpi, 2 counts per millimeter
        //      2 = 100 dpi, 4 counts per millimeter
        //      3 = 200 dpi, 8 counts per millimeter
        if (regs.u.r8.bh < 4) {
            ret = send_to_mouse_ctrl(0xE8); // set resolution command
            if (ret == 0) {
                ret = get_mouse_data(&mouse_data1);
                if (mouse_data1 != 0xfa)
                    BX_PANIC("Mouse status returned %02x (should be ack)\n", (unsigned)mouse_data1);
                ret = send_to_mouse_ctrl(regs.u.r8.bh);
                ret = get_mouse_data(&mouse_data1);
                if (mouse_data1 != 0xfa)
                    BX_PANIC("Mouse status returned %02x (should be ack)\n", (unsigned)mouse_data1);
                // success
            } else {
                // interface error
                SET_CF();
                regs.u.r8.ah = 3;
            }
        } else {
            // invalid input
            SET_CF();
            regs.u.r8.ah = 2;
        }
        break;

    case 4: // Get Device ID
        BX_DEBUG_INT15_MS("case 4:\n");
        ret = send_to_mouse_ctrl(0xF2); // get mouse ID command
        if (ret == 0) {
            ret = get_mouse_data(&mouse_data1);
            ret = get_mouse_data(&mouse_data2);
            regs.u.r8.bh = mouse_data2;
            // success
        } else {
            // interface error
            SET_CF();
            regs.u.r8.ah = 3;
        }
        break;

    case 6: // Return Status & Set Scaling Factor...
        BX_DEBUG_INT15_MS("case 6:\n");
        switch (regs.u.r8.bh) {
        case 0: // Return Status
            ret = send_to_mouse_ctrl(0xE9); // get mouse info command
            if (ret == 0) {
                ret = get_mouse_data(&mouse_data1);
                if (mouse_data1 != 0xfa)
                    BX_PANIC("Mouse status returned %02x (should be ack)\n", (unsigned)mouse_data1);
                if (ret == 0) {
                    ret = get_mouse_data(&mouse_data1);
                    if ( ret == 0 ) {
                        ret = get_mouse_data(&mouse_data2);
                        if ( ret == 0 ) {
                            ret = get_mouse_data(&mouse_data3);
                            if ( ret == 0 ) {
                                regs.u.r8.bl = mouse_data1;
                                regs.u.r8.cl = mouse_data2;
                                regs.u.r8.dl = mouse_data3;
                                // success
                                break;
                            }
                        }
                    }
                }
            }

            // interface error
            SET_CF();
            regs.u.r8.ah = 3;
            break;

        case 1: // Set Scaling Factor to 1:1
        case 2: // Set Scaling Factor to 2:1
            if (regs.u.r8.bh == 1) {
                ret = send_to_mouse_ctrl(0xE6);
            } else {
                ret = send_to_mouse_ctrl(0xE7);
            }
            if (ret == 0) {
                get_mouse_data(&mouse_data1);
                ret = (mouse_data1 != 0xFA);
            }
            if (ret != 0) {
                // interface error
                SET_CF();
                regs.u.r8.ah = 3;
            }
            break;

        default:
            BX_PANIC("INT 15h C2 AL=6, BH=%02x\n", (unsigned) regs.u.r8.bh);
            // invalid subfunction
            SET_CF();
            regs.u.r8.ah = 1;
        }
        break;

    case 7: // Set Mouse Handler Address
        BX_DEBUG_INT15_MS("case 7:\n");
        mouse_driver_seg = ES;
        mouse_driver_offset = regs.u.r16.bx;
        write_word(ebda_seg, 0x0022, mouse_driver_offset);
        write_word(ebda_seg, 0x0024, mouse_driver_seg);
        mouse_flags_2 = read_byte(ebda_seg, 0x0027);
        if (mouse_driver_offset == 0 && mouse_driver_seg == 0) {
            /* remove handler */
            if ( (mouse_flags_2 & 0x80) != 0 ) {
                mouse_flags_2 &= ~0x80;
            }
        }
        else {
            /* install handler */
            mouse_flags_2 |= 0x80;
        }
        write_byte(ebda_seg, 0x0027, mouse_flags_2);
        break;

    default:
        BX_PANIC("INT 15h C2 default case entered\n");
        // invalid subfunction
        SET_CF();
        regs.u.r8.ah = 1;
    }
    BX_DEBUG_INT15_MS("returning cf = %u, ah = %02x\n", (unsigned)GET_CF(), (unsigned)regs.u.r8.ah);
    // Re-enable AUX input and IRQ12
    set_kbd_command_byte(0x47);
}
#endif // BX_USE_PS2_MOUSE
