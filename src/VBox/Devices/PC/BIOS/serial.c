/* $Id: serial.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

void BIOSCALL int14_function(pusha_regs_t regs, uint16_t es, uint16_t ds, volatile iret_addr_t iret_addr)
{
    uint16_t    addr, timer, val16;
    uint8_t     timeout;

    int_enable();

    addr = read_word(0x0040, (regs.u.r16.dx << 1));
    timeout = read_byte(0x0040, 0x007C + regs.u.r16.dx);
    if ((regs.u.r16.dx < 4) && (addr > 0)) {
        switch (regs.u.r8.ah) {
        case 0:
            outb(addr+3, inb(addr+3) | 0x80);
            if ((regs.u.r8.al & 0xE0) == 0) {
                outb(addr, 0x17);
                outb(addr+1, 0x04);
            } else {
                val16 = 0x600 >> ((regs.u.r8.al & 0xE0) >> 5);
                outb(addr, val16 & 0xFF);
                outb(addr+1, val16 >> 8);
            }
            outb(addr+3, regs.u.r8.al & 0x1F);
            regs.u.r8.ah = inb(addr+5);
            regs.u.r8.al = inb(addr+6);
            ClearCF(iret_addr.flags);
            break;
        case 1:
            timer = read_word(0x0040, 0x006C);
            while (((inb(addr+5) & 0x60) != 0x60) && (timeout)) {
                val16 = read_word(0x0040, 0x006C);
                if (val16 != timer) {
                    timer = val16;
                    timeout--;
                }
            }
            if (timeout) outb(addr, regs.u.r8.al);
            regs.u.r8.ah = inb(addr+5);
            if (!timeout) regs.u.r8.ah |= 0x80;
            ClearCF(iret_addr.flags);
            break;
        case 2:
            timer = read_word(0x0040, 0x006C);
            while (((inb(addr+5) & 0x01) == 0) && (timeout)) {
                val16 = read_word(0x0040, 0x006C);
                if (val16 != timer) {
                    timer = val16;
                    timeout--;
                }
            }
            if (timeout) {
                regs.u.r8.ah = 0;
                regs.u.r8.al = inb(addr);
            } else {
                regs.u.r8.ah = inb(addr+5);
            }
            ClearCF(iret_addr.flags);
            break;
        case 3:
            regs.u.r8.ah = inb(addr+5);
            regs.u.r8.al = inb(addr+6);
            ClearCF(iret_addr.flags);
            break;
        default:
            SetCF(iret_addr.flags); // Unsupported
        }
    } else {
        SetCF(iret_addr.flags); // Unsupported
    }
}
