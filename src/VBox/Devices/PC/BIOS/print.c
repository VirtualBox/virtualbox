/* $Id: print.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <stdarg.h>
#include "inlines.h"
#include "biosint.h"

// Debug printf support

/* Redirect INFO output to backdoor logging port. */
#define INFO_PORT   0x504
#define DEBUG_PORT  0x403

const char bios_prefix_string[] = "BIOS: ";

void wrch(uint8_t c);
#pragma aux wrch = "mov ah, 0eh" "int 10h" parm [al] modify exact [ax bx];

void send(uint16_t action, uint8_t c)
{
#if BX_DEBUG_SERIAL
    if (c == '\n')
        uart_tx_byte(BX_DEBUG_PORT, '\r');
    uart_tx_byte(BX_DEBUG_PORT, c);
#endif
#if BX_VIRTUAL_PORTS
    if (action & BIOS_PRINTF_DEBUG)
        outb(DEBUG_PORT, c);
    if (action & BIOS_PRINTF_INFO)
        outb(INFO_PORT, c);
#endif
    if (action & BIOS_PRINTF_SCREEN) {
        if (c == '\n')
            wrch('\r');
        wrch(c);
    }
}

void put_int(uint16_t action, short val, short width, bx_bool neg)
{
    short   nval = val / 10;
    if (nval)
        put_int(action, nval, width - 1, neg);
    else {
        while (--width > 0)
            send(action, ' ');
        if (neg)
            send(action, '-');
    }
    send(action, val - (nval * 10) + '0');
}

void put_uint(uint16_t action, unsigned short val, short width, bx_bool neg)
{
    unsigned short nval = val / 10;
    if (nval)
        put_uint(action, nval, width - 1, neg);
    else {
        while (--width > 0)
            send(action, ' ');
        if (neg)
            send(action, '-');
    }
    send(action, val - (nval * 10) + '0');
}

void put_luint(uint16_t action, unsigned long val, short width, bx_bool neg)
{
    unsigned long   nval = val / 10;
    if (nval)
        put_luint(action, nval, width - 1, neg);
    else {
        while (--width > 0)
            send(action, ' ');
        if (neg)
            send(action, '-');
    }
    send(action, val - (nval * 10) + '0');
}

void put_str(uint16_t action, const char __far *s)
{
    uint8_t c;

    while (c = *s) {
        send(action, c);
        s++;
    }
}

void put_str_near(uint16_t action, const char __near *s)
{
    uint8_t c;

    while (c = *s) {
        send(action, c);
        s++;
    }
}


//--------------------------------------------------------------------------
// bios_printf()
//   A compact variable argument printf function.
//
//   Supports %[format_width][length]format
//   where format can be x,X,u,d,s,S,c
//   and the optional length modifier is l (ell, long 32-bit) or ll
//   (long long, 64-bit).
//   Only x,X work with ll
//--------------------------------------------------------------------------
void bios_printf(uint16_t action, const char *s, ...)
{
    uint8_t     c;
    bx_bool     in_format;
    int         i;
    uint16_t    arg, nibble, hibyte, format_width, hexadd;
    va_list     args;

    va_start( args, s );

    in_format = 0;
    format_width = 0;

    if ((action & BIOS_PRINTF_DEBHALT) == BIOS_PRINTF_DEBHALT) {
        bios_printf (BIOS_PRINTF_SCREEN, "FATAL: ");
    }

    while (c = *s) {
        if ( c == '%' ) {
            in_format = 1;
            format_width = 0;
        }
        else if (in_format) {
            if ( (c>='0') && (c<='9') ) {
                format_width = (format_width * 10) + (c - '0');
            }
            else {
                arg = va_arg( args, uint16_t );
                if (c == 'x' || c == 'X') {
                    if (format_width == 0)
                        format_width = 4;
                    if (c == 'x')
                        hexadd = 'a';
                    else
                        hexadd = 'A';
                    for (i=format_width-1; i>=0; i--) {
                        nibble = (arg >> (4 * i)) & 0x000f;
                        send (action, (nibble<=9)? (nibble+'0') : (nibble-10+hexadd));
                    }
                }
                else if (c == 'u') {
                    put_uint(action, arg, format_width, 0);
                }
                else if (c == 'l' && s[1] == 'l') {
                    uint64_t llval;
                    uint16_t __far *cp16;

                    s += 2;
                    c = *s;
                    cp16 = (uint16_t __far *)&llval;
                    cp16[0] = arg;
                    cp16[1] = va_arg( args, uint16_t );
                    cp16[2] = va_arg( args, uint16_t );
                    cp16[3] = va_arg( args, uint16_t );
                    if (c == 'x' || c == 'X') {
                        if (format_width == 0)
                            format_width = 16;
                        if (c == 'x')
                            hexadd = 'a';
                        else
                            hexadd = 'A';
                        for (i=format_width-1; i>=0; i--) {
                            nibble =  (llval >> (i * 4)) & 0x000f;
                            send (action, (nibble<=9)? (nibble+'0') : (nibble-10+hexadd));
                        }
                    } else {
                        BX_PANIC("bios_printf: unknown %ll format\n");
                    }
                }
                else if (c == 'l') {
                    s++;
                    c = *s; /* is it ld,lx,lu? */
                    hibyte = va_arg( args, uint16_t );
                    if (c == 'd') {
                        if (hibyte & 0x8000)
                            put_luint(action, 0L-(((uint32_t) hibyte << 16) | arg), format_width-1, 1);
                        else
                            put_luint(action, ((uint32_t) hibyte << 16) | arg, format_width, 0);
                    }
                    else if (c == 'u') {
                        put_luint(action, ((uint32_t) hibyte << 16) | arg, format_width, 0);
                    }
                    else if (c == 'x' || c == 'X')
                    {
                        if (format_width == 0)
                            format_width = 8;
                        if (c == 'x')
                            hexadd = 'a';
                        else
                            hexadd = 'A';
                        for (i=format_width-1; i>=0; i--) {
                            nibble = ((((uint32_t)hibyte << 16) | arg) >> (4 * i)) & 0x000f;
                            send (action, (nibble<=9)? (nibble+'0') : (nibble-10+hexadd));
                        }
                    }
                }
                else if (c == 'd') {
                    if (arg & 0x8000)
                        put_int(action, -arg, format_width - 1, 1);
                    else
                        put_int(action, arg, format_width, 0);
                }
                else if (c == 's') {
                    put_str(action, (char *)arg);
                }
                else if (c == 'S') {
                    hibyte = arg;
                    arg = va_arg( args, uint16_t );
                    put_str(action, hibyte :> (char *)arg);
                }
                else if (c == 'c') {
                    send(action, arg);
                }
                else
                    BX_PANIC("bios_printf: unknown format\n");
                in_format = 0;
            }
        }
        else {
            send(action, c);
        }
        ++s;
    }
    va_end( args );
    if (action & BIOS_PRINTF_HALT) {
        // freeze in a busy loop.
        int_disable();
        halt_forever();
    }
}

// End of printf support
