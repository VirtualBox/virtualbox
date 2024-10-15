/* $Id: parallel.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

void BIOSCALL int17_function(pusha_regs_t regs, uint16_t es, uint16_t ds, volatile iret_addr_t iret_addr)
{
    uint16_t    addr,timeout;
    uint8_t     val8;

    int_enable();

    addr = read_word(0x0040, (regs.u.r16.dx << 1) + 8);
    if ((regs.u.r8.ah < 3) && (regs.u.r16.dx < 3) && (addr > 0)) {
        timeout = read_byte(0x0040, 0x0078 + regs.u.r16.dx) << 8;
        if (regs.u.r8.ah == 0) {
            outb(addr, regs.u.r8.al);
            val8 = inb(addr+2);
            outb(addr+2, val8 | 0x01); // send strobe
            outb(addr+2, val8 & ~0x01);
            while (((inb(addr+1) & 0x40) == 0x40) && (timeout)) {
                timeout--;
            }
        }
        if (regs.u.r8.ah == 1) {
            val8 = inb(addr+2);
            outb(addr+2, val8 & ~0x04); // send init
            outb(addr+2, val8 | 0x04);
        }
        val8 = inb(addr+1);
        regs.u.r8.ah = (val8 ^ 0x48);
        if (!timeout) regs.u.r8.ah |= 0x01;
        ClearCF(iret_addr.flags);
    } else {
        SetCF(iret_addr.flags); // Unsupported
    }
}
