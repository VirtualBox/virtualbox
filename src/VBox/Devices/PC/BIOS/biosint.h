/* $Id: biosint.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - BIOS internal definitions.
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_biosint_h
#define VBOX_INCLUDED_SRC_PC_BIOS_biosint_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Compile-time assertion macro. */
#define ct_assert(a)    extern int ct_ass_arr[!!(a) == 1];

/* For functions called from assembly code. */
#define BIOSCALL    __cdecl

#define BX_ELTORITO_BOOT    1
#define BX_PCIBIOS          1
#define BX_USE_PS2_MOUSE    1
#define BX_APM              1

#ifndef DEBUG_ATA
# define DEBUG_ATA       0
#endif
#ifdef DEBUG_AHCI
# define DEBUG_AHCI      0
#endif
#ifndef DEBUG_SCSI
# define DEBUG_SCSI      0
#endif
#ifndef DEBUG_CD_BOOT
# define DEBUG_CD_BOOT   0
#endif
#ifndef DEBUG_ELTORITO
# define DEBUG_ELTORITO  0
#endif
#ifndef DEBUG_INT13_HD
# define DEBUG_INT13_HD  0
#endif
#ifndef DEBUG_INT13_FL
# define DEBUG_INT13_FL  0
#endif
#ifndef DEBUG_INT13_CD
# define DEBUG_INT13_CD  0
#endif
#ifndef DEBUG_INT15
# define DEBUG_INT15     0
#endif
#ifndef DEBUG_INT15_MS
# define DEBUG_INT15_MS  0
#endif
#ifndef DEBUG_INT16
# define DEBUG_INT16     0
#endif
#ifndef DEBUG_INT1A
# define DEBUG_INT1A     0
#endif
#ifndef DEBUG_INT74
# define DEBUG_INT74     0
#endif
#ifndef DEBUG_PCI
# define DEBUG_PCI       0
#endif
#ifndef DEBUG_APM
# define DEBUG_APM       0
#endif
#ifndef DEBUG_POST
# define DEBUG_POST      0
#endif

#define FP_OFF(p)   ((unsigned)(p))
#define FP_SEG(p)   ((unsigned)((unsigned long)(void __far*)(p) >> 16))
#define MK_FP(s,o)  (((unsigned short)(s)):>((void __near *)(o)))

typedef struct {
    union {
        struct {
            uint16_t    di, si, bp, sp;
            uint16_t    bx, dx, cx, ax;
        } r16;
        struct {
            uint16_t    filler[4];
            uint8_t     bl, bh, dl, dh, cl, ch, al, ah;
        } r8;
    } u;
} pusha_regs_t;

typedef struct {
    union {
        struct {
            uint32_t    edi, esi, ebp, esp;
            uint32_t    ebx, edx, ecx, eax;
        } r32;
        struct {
            uint16_t    di, filler1, si, filler2, bp, filler3, sp, filler4;
            uint16_t    bx, filler5, dx, filler6, cx, filler7, ax, filler8;
        } r16;
        struct {
            uint32_t    filler[4];
            uint8_t     bl, bh;
            uint16_t    filler1;
            uint8_t     dl, dh;
            uint16_t    filler2;
            uint8_t     cl, ch;
            uint16_t    filler3;
            uint8_t     al, ah;
            uint16_t    filler4;
        } r8;
    } u;
} pushad_regs_t;

typedef struct {
    union {
        struct {
            uint16_t    flags;
        } r16;
        struct {
            uint8_t     flagsl;
            uint8_t     flagsh;
        } r8;
    } u;
} flags_t;

typedef struct {
    uint16_t    ip;
    uint16_t    cs;
    flags_t     flags;
} iret_addr_t;

typedef struct {
    uint16_t        ds;
    uint16_t        es;
    pusha_regs_t    gr;
    iret_addr_t     ra;
} disk_regs_t;

typedef struct {
    pusha_regs_t    gr;
    uint16_t        es;
    uint16_t        ds;
    uint16_t        ifl;
    iret_addr_t     ra;
} kbd_regs_t;

typedef struct {
    pusha_regs_t    gr;
    uint16_t        es;
    uint16_t        ds;
    flags_t         fl;
} sys_regs_t;

typedef struct {
    pushad_regs_t   gr;
    uint16_t        es;
    uint16_t        ds;
    flags_t         fl;
} sys32_regs_t;

typedef struct {
    pusha_regs_t    gr;
    iret_addr_t     ra;
} i1apci_regs_t;


#define SetCF(x)   x.u.r8.flagsl |= 0x01
#define SetZF(x)   x.u.r8.flagsl |= 0x40
#define ClearCF(x) x.u.r8.flagsl &= 0xfe
#define ClearZF(x) x.u.r8.flagsl &= 0xbf
#define GetCF(x)   (x.u.r8.flagsl & 0x01)

#define SET_AL(val8) AX = ((AX & 0xff00) | (val8))
#define SET_BL(val8) BX = ((BX & 0xff00) | (val8))
#define SET_CL(val8) CX = ((CX & 0xff00) | (val8))
#define SET_DL(val8) DX = ((DX & 0xff00) | (val8))
#define SET_AH(val8) AX = ((AX & 0x00ff) | ((val8) << 8))
#define SET_BH(val8) BX = ((BX & 0x00ff) | ((val8) << 8))
#define SET_CH(val8) CX = ((CX & 0x00ff) | ((val8) << 8))
#define SET_DH(val8) DX = ((DX & 0x00ff) | ((val8) << 8))

#define GET_AL() ( AX & 0x00ff )
#define GET_BL() ( BX & 0x00ff )
#define GET_CL() ( CX & 0x00ff )
#define GET_DL() ( DX & 0x00ff )
#define GET_AH() ( AX >> 8 )
#define GET_BH() ( BX >> 8 )
#define GET_CH() ( CX >> 8 )
#define GET_DH() ( DX >> 8 )

#define GET_ELDL() ( ELDX & 0x00ff )
#define GET_ELDH() ( ELDX >> 8 )

#define SET_CF()     FLAGS |= 0x0001
#define CLEAR_CF()   FLAGS &= 0xfffe
#define GET_CF()     (FLAGS & 0x0001)

#define SET_ZF()     FLAGS |= 0x0040
#define CLEAR_ZF()   FLAGS &= 0xffbf
#define GET_ZF()     (FLAGS & 0x0040)

#define SET_IF()     FLAGS |= 0x0200

typedef unsigned short  bx_bool;

#define BX_VIRTUAL_PORTS 1 /* normal output to Bochs ports */
#define BX_DEBUG_SERIAL  0 /* output to COM1 */

#define BIOS_PRINTF_HALT     1
#define BIOS_PRINTF_SCREEN   2
#define BIOS_PRINTF_INFO     4
#define BIOS_PRINTF_DEBUG    8
#define BIOS_PRINTF_ALL      (BIOS_PRINTF_SCREEN | BIOS_PRINTF_INFO)
#define BIOS_PRINTF_DEBHALT  (BIOS_PRINTF_SCREEN | BIOS_PRINTF_INFO | BIOS_PRINTF_HALT)

extern  const char  bios_prefix_string[];
extern  void        bios_printf(uint16_t action, const char *s, ...);
extern  void        put_str(uint16_t action, const char __far *s);
extern  void        put_str_near(uint16_t action, const char __near *s);
extern  uint8_t     inb_cmos(uint8_t cmos_reg);
extern  void        outb_cmos(uint8_t cmos_reg, uint8_t val);
extern  uint16_t    get_cmos_word(uint8_t idxFirst);
extern  uint16_t    cdrom_boot(void);
extern  void        show_logo(void);
extern  void        delay_boot(uint16_t secs);
extern  bx_bool     set_enable_a20(bx_bool val);
extern uint16_t     conv_mem_alloc(int n_kb, int in_ebda);

#define printf(...)  bios_printf(BIOS_PRINTF_SCREEN, __VA_ARGS__)

// Defines the output macros.
// BX_DEBUG goes to INFO port until we can easily choose debug info on a
// per-device basis. Debug info are sent only in debug mode
#define DEBUG_ROMBIOS   1
#if DEBUG_ROMBIOS
#  define BX_DEBUG(...) bios_printf(BIOS_PRINTF_INFO, __VA_ARGS__)
#else
#  define BX_DEBUG(...)
#endif
#ifdef VBOX
#define BX_INFO(...)    do { put_str(BIOS_PRINTF_INFO, bios_prefix_string); bios_printf(BIOS_PRINTF_INFO, __VA_ARGS__); } while (0)
#define BX_INFO_CON(...)do { put_str(BIOS_PRINTF_INFO, bios_prefix_string); bios_printf(BIOS_PRINTF_ALL, __VA_ARGS__); } while (0)
#else /* !VBOX */
#define BX_INFO(...)    bios_printf(BIOS_PRINTF_INFO, __VA_ARGS__)
#endif /* !VBOX */
#define BX_PANIC(...)   bios_printf(BIOS_PRINTF_DEBHALT, __VA_ARGS__)

uint16_t pci16_find_device(uint32_t search_item, uint16_t index, int search_class, int ignore_if);

/* Random hardware-related definitions. */

#define PIC_MASTER          0x20
#define PIC_MASTER_MASK     0x21
#define PIC_SLAVE           0xA0
#define PIC_SLAVE_MASK      0xA1
#define PIC_CMD_EOI         0x20
#define PIC_CMD_RD_ISR      0x0B
#define PIC_CMD_INIT        0x11

#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_biosint_h */

