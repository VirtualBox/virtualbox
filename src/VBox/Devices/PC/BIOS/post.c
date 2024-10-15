/* $Id: post.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BIOS POST routines. Used only during initialization.
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

#include <stdint.h>
#include <string.h>
#include "biosint.h"
#include "inlines.h"

#if DEBUG_POST  || 0
#  define DPRINT(...) BX_DEBUG(__VA_ARGS__)
#else
#  define DPRINT(...)
#endif

/* In general, checksumming ROMs in a VM just wastes time. */
//#define CHECKSUM_ROMS

/* The format of a ROM is as follows:
 *
 *     ------------------------------
 *   0 | AA55h signature (word)     |
 *     ------------------------------
 *   2 | Size in 512B blocks (byte) |
 *     ------------------------------
 *   3 | Start of executable code   |
 *     |          .......           |
 * end |                            |
 *     ------------------------------
 */

typedef struct rom_hdr_tag {
    uint16_t    signature;
    uint8_t     num_blks;
    uint8_t     code;
} rom_hdr;


/* Calculate the checksum of a ROM. Note that the ROM might be
 * larger than 64K.
 */
static inline uint8_t rom_checksum(uint8_t __far *rom, uint8_t blocks)
{
    uint8_t     sum = 0;

#ifdef CHECKSUM_ROMS
    while (blocks--) {
        int     i;

        for (i = 0; i < 512; ++i)
            sum += rom[i];
        /* Add 512 bytes (32 paragraphs) to segment. */
        rom = MK_FP(FP_SEG(rom) + (512 >> 4), 0);
    }
#endif
    return sum;
}

/* The ROM init routine might trash register. Give the compiler a heads-up. */
typedef void __far (rom_init_rtn)(void);
#pragma aux rom_init_rtn modify [ax bx cx dx si di es /*ignored:*/ bp] /*ignored:*/ loadds;

/* The loadds bit is ignored for rom_init_rtn, the impression from the code
   generator is that this is due to using a memory model where DS is fixed.
   If we add DS as a modified register, we'll get run into compiler error
   E1122 (BAD_REG) because FixedRegs() in cg/intel/386/c/386rgtbl.c returns
   HW_DS as part of the fixed register set that cannot be modified.

   The problem of the vga bios trashing DS isn't a biggie, except when
   something goes sideways before we can reload it.  Setting a I/O port
   breakpoint on port 80h and wait a while before resuming execution
   (ba i 1 80 "sleep 400 ; g") will usually trigger a keyboard init panic and
   showcase the issue.  The panic message is garbage because it's read from
   segment 0c000h instead of 0f000h. */
static void restore_ds_as_dgroup(void);
#pragma aux restore_ds_as_dgroup = \
    "mov ax, 0f000h" \
    "mov ds, ax" \
    modify exact [ax];


/* Scan for ROMs in the given range and execute their POST code. */
void rom_scan(uint16_t start_seg, uint16_t end_seg)
{
    rom_hdr __far   *rom;
    uint8_t         rom_blks;

    DPRINT("Scanning for ROMs in %04X-%04X range\n", start_seg, end_seg);

    while (start_seg < end_seg) {
        rom = MK_FP(start_seg, 0);
        /* Check for the ROM signature. */
        if (rom->signature == 0xAA55) {
            DPRINT("Found ROM at segment %04X\n", start_seg);
            if (!rom_checksum((void __far *)rom, rom->num_blks)) {
                rom_init_rtn *rom_init;

                /* Checksum good, initialize ROM. */
                rom_init = (rom_init_rtn *)&rom->code;
                rom_init();
                int_disable();
                restore_ds_as_dgroup();
                /** @todo BP is not restored. */
                DPRINT("ROM initialized\n");

                /* Continue scanning past the end of this ROM. */
                rom_blks   = (rom->num_blks + 3) & ~3;  /* 4 blocks = 2K */
                start_seg += rom_blks / 4;
            }
        } else {
            /* Scanning is done in 2K steps. */
            start_seg += 2048 >> 4;
        }
    }
}

#if VBOX_BIOS_CPU >= 80386

/* NB: The CPUID detection is generic but currently not used elsewhere. */

/* Check CPUID availability. */
int is_cpuid_supported( void )
{
    uint32_t    old_flags, new_flags;

    old_flags = eflags_read();
    new_flags = old_flags ^ (1L << 21); /* Toggle CPUID bit. */
    eflags_write( new_flags );
    new_flags = eflags_read();
    return( old_flags != new_flags );   /* Supported if bit changed. */
}

#define APICMODE_DISABLED   0
#define APICMODE_APIC       1
#define APICMODE_X2APIC     2

#define APIC_BASE_MSR       0x1B
#define APICBASE_X2APIC     0x400   /* bit 10 */
#define APICBASE_ENABLE     0x800   /* bit 11 */

/*
 * Set up APIC/x2APIC. See also DevPcBios.cpp.
 *
 * NB: Virtual wire compatibility is set up earlier in 32-bit protected
 * mode assembler (because it needs to access MMIO just under 4GB).
 * Switching to x2APIC mode or disabling the APIC is done through an MSR
 * and needs no 32-bit addressing. Going to x2APIC mode does not lose the
 * existing virtual wire setup.
 *
 * NB: This code does not assume that there is a local APIC. It is necessary
 * to check CPUID whether APIC is present; the CPUID instruction might not be
 * available either.
 *
 * NB: Destroys high bits of 32-bit registers.
 */
void BIOSCALL apic_setup(void)
{
    uint64_t    base_msr;
    uint16_t    mask_set;
    uint16_t    mask_clr;
    uint8_t     apic_mode;
    uint32_t    cpu_id[4];

    /* If there's no CPUID, there's certainly no APIC. */
    if (!is_cpuid_supported()) {
        return;
    }

    /* Check EDX bit 9 */
    cpuid(&cpu_id, 1);
    BX_DEBUG("CPUID EDX: 0x%lx\n", cpu_id[3]);
    if ((cpu_id[3] & (1 << 9)) == 0) {
        return; /* No local APIC, nothing to do. */
    }

    /* APIC mode at offset 78h in CMOS NVRAM. */
    apic_mode = inb_cmos(0x78);

    mask_set = mask_clr = 0;
    if (apic_mode == APICMODE_X2APIC)
        mask_set = APICBASE_X2APIC;
    else if (apic_mode == APICMODE_DISABLED)
        mask_clr = APICBASE_ENABLE;
    else
        ;   /* Any other setting leaves things alone. */

    if (mask_set || mask_clr) {
        base_msr = msr_read(APIC_BASE_MSR);
        base_msr &= ~(uint64_t)mask_clr;
        base_msr |= mask_set;
        msr_write(base_msr, APIC_BASE_MSR);
    }
}

#endif

/**
 * Allocate n KB of conventional memory, and either add it to
 * the EBDA or just take it away from memory below 640K.
 *
 * Returns offset (in paragraphs) to the allocated block within
 * the EBDA when adding to the EBDA, or segment address when
 * allocating outside of the EBDA.
 *
 * NB: By default, the EBDA is 1KB in size, located at 639KB
 * into the start of memory (just below the video RAM). Optional
 * BIOS components may need additional real-mode addressable RAM
 * which is added to the EBDA.
 *
 * The EBDA should be relocatable as a whole. All pointers to
 * the EBDA must be relative, actual address is calculated from
 * the value at 40:0E.
 *
 * Items like PS/2 mouse support must be at a fixed offset from
 * the start of the EBDA. Therefore, any additional memory gets
 * allocated at the end and the original EBDA contents are
 * shifted down.
 *
 * To make relocating the EBDA practical, the EBDA must
 * immediately follow the end of conventional memory. Therefore,
 * new
 *
 * WARNING: When successful, this function moves the EBDA!
 */
uint16_t conv_mem_alloc(int n_kb, int in_ebda)
{
    uint16_t        base_mem_kb;
    uint16_t        ebda_kb;
    uint16_t        user_ofs;   /* Offset and size in paragraphs */
    uint16_t        user_size;
    uint16_t        old_ebda_seg;
    uint16_t        new_ebda_seg;
    uint16_t        ret_val;

    base_mem_kb = read_word(0x00, 0x0413);

    DPRINT("BIOS: %dK of base mem, removing %dK\n", base_mem_kb, n_kb);

    if (base_mem_kb == 0)
        return 0;

    /* Reduce conventional memory size and update the BDA. */
    base_mem_kb -= n_kb;
    write_word(0x0040, 0x0013, base_mem_kb);

    /* Figure out what's where. */
    old_ebda_seg = read_word(0x0040, 0x000E);
    ebda_kb      = read_byte(old_ebda_seg, 0);
    user_ofs     = ebda_kb * (1024 / 16);   /* Old EBDA size == new mem offset. */
    user_size    = n_kb * (1024 / 16);

    /* Shift the existing EBDA down. */
    new_ebda_seg = old_ebda_seg - user_size;
    /* This is where we should be using memmove(), but we know how our own memcpy() works. */
    _fmemcpy(MK_FP(new_ebda_seg, 0), MK_FP(old_ebda_seg, 0), user_ofs * 16);
    _fmemset(MK_FP(new_ebda_seg + user_ofs, 0), 0, user_size * 16);

    /* Update the EBDA location and possibly size. */
    write_word(0x0040, 0x000E, new_ebda_seg);
    if (in_ebda) {
        write_byte(new_ebda_seg, 0, ebda_kb + n_kb);
        ret_val = user_ofs;
    } else {
        ret_val = new_ebda_seg + user_ofs;
    }

    DPRINT("BIOS: added %04X paras ofs or seg %04X\n", user_size, ret_val);
    return ret_val;
}

