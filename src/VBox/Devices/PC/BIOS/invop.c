/* $Id: invop.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Real mode invalid opcode handler.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

//#define EMU_386_LOADALL

/* The layout of 286 LOADALL descriptors. */
typedef struct tag_ldall_desc {
    uint16_t    base_lo;        /* Bits 0-15 of segment base. */
    uint8_t     base_hi;        /* Bits 16-13 of segment base. */
    uint8_t     attr;           /* Segment attributes. */
    uint16_t    limit;          /* Segment limit. */
} ldall_desc;

/* The 286 LOADALL memory buffer at physical address 800h. From
 * The Undocumented PC.
 */
typedef struct tag_ldall_286 {
    uint16_t    unused1[3];
    uint16_t    msw;            /* 806h */
    uint16_t    unused2[7];
    uint16_t    tr;             /* 816h */
    uint16_t    flags;          /* 818h */
    uint16_t    ip;             /* 81Ah */
    uint16_t    ldt;            /* 81Ch */
    uint16_t    ds;             /* 81Eh */
    uint16_t    ss;             /* 820h */
    uint16_t    cs;             /* 822h */
    uint16_t    es;             /* 824h */
    uint16_t    di;             /* 826h */
    uint16_t    si;             /* 828h */
    uint16_t    bp;             /* 82Ah */
    uint16_t    sp;             /* 82Ch */
    uint16_t    bx;             /* 82Eh */
    uint16_t    dx;             /* 830h */
    uint16_t    cx;             /* 832h */
    uint16_t    ax;             /* 834h */
    ldall_desc  es_desc;        /* 836h */
    ldall_desc  cs_desc;        /* 83Ch */
    ldall_desc  ss_desc;        /* 842h */
    ldall_desc  ds_desc;        /* 848h */
    ldall_desc  gdt_desc;       /* 84Eh */
    ldall_desc  ldt_desc;       /* 854h */
    ldall_desc  idt_desc;       /* 85Ah */
    ldall_desc  tss_desc;       /* 860h */
} ldall_286_s;
ct_assert(sizeof(ldall_286_s) == 0x66);

#ifdef EMU_386_LOADALL

/* The layout of 386 LOADALL descriptors. */
typedef struct tag_ldal3_desc {
    uint32_t    attr;           /* Segment attributes. */
    uint32_t    base;           /* Expanded segment base. */
    uint32_t    limit;          /* Expanded segment limit. */
} ldal3_desc;

/* The 386 LOADALL memory buffer pointed to by ES:EDI.
 */
typedef struct tag_ldall_386 {
    uint32_t    cr0;            /* 00h */
    uint32_t    eflags;         /* 04h */
    uint32_t    eip;            /* 08h */
    uint32_t    edi;            /* 0Ch */
    uint32_t    esi;            /* 10h */
    uint32_t    ebp;            /* 14h */
    uint32_t    esp;            /* 18h */
    uint32_t    ebx;            /* 1Ch */
    uint32_t    edx;            /* 20h */
    uint32_t    ecx;            /* 24h */
    uint32_t    eax;            /* 28h */
    uint32_t    dr6;            /* 2Ch */
    uint32_t    dr7;            /* 30h */
    uint32_t    tr;             /* 34h */
    uint32_t    ldt;            /* 38h */
    uint32_t    gs;             /* 3Ch */
    uint32_t    fs;             /* 40h */
    uint32_t    ds;             /* 44h */
    uint32_t    ss;             /* 4Ch */
    uint32_t    cs;             /* 48h */
    uint32_t    es;             /* 50h */
    ldal3_desc  tss_desc;       /* 54h */
    ldal3_desc  idt_desc;       /* 60h */
    ldal3_desc  gdt_desc;       /* 6Ch */
    ldal3_desc  ldt_desc;       /* 78h */
    ldal3_desc  gs_desc;        /* 84h */
    ldal3_desc  fs_desc;        /* 90h */
    ldal3_desc  ds_desc;        /* 9Ch */
    ldal3_desc  ss_desc;        /* A8h */
    ldal3_desc  cs_desc;        /* B4h */
    ldal3_desc  es_desc;        /* C0h */
} ldall_386_s;
ct_assert(sizeof(ldall_386_s) == 0xCC);

#endif

/*
 * LOADALL emulation assumptions:
 *  - MSW indicates real mode
 *  - Standard real mode CS and SS is to be used
 *  - Segment values of non-RM segments (if any) do not matter
 *  - Standard segment attributes are used
 */

/* A wrapper for LIDT. */
void load_idtr(uint32_t base, uint16_t limit);
#pragma aux load_idtr =     \
    ".286p"                 \
    "mov bx, sp"            \
    "lidt fword ptr ss:[bx]"\
    parm caller reverse [] modify [bx] exact;

/* A wrapper for LGDT. */
void load_gdtr(uint32_t base, uint16_t limit);
#pragma aux load_gdtr =     \
    ".286p"                 \
    "mov bx, sp"            \
    "lgdt fword ptr ss:[bx]"\
    parm caller reverse [] modify [bx] exact;

/* Load DS/ES as real-mode segments. May be overwritten later.
 * NB: Loads SS with 80h to address the LOADALL buffer. Must
 * not touch CX!
 */
void load_rm_segs(int seg_flags);
#pragma aux load_rm_segs =  \
    "mov ax, 80h"           \
    "mov ss, ax"            \
    "mov ax, ss:[1Eh]"      \
    "mov ds, ax"            \
    "mov ax, ss:[24h]"      \
    "mov es, ax"            \
    parm [cx] nomemory modify nomemory;

/* Briefly switch to protected mode and load ES and/or DS if necessary.
 * NB: Trashes high bits of EAX, but that should be safe. Expects flags
 * in CX.
 */
void load_pm_segs(void);
#pragma aux load_pm_segs =  \
    ".386p"                 \
    "smsw ax"               \
    "inc  ax"               \
    "lmsw ax"               \
    "mov ax, 8"             \
    "test cx, 1"            \
    "jz  skip_es"           \
    "mov es, ax"            \
    "skip_es:"              \
    "test cx, 2"            \
    "jz  skip_ds"           \
    "mov bx,ss:[00h]"       \
    "mov ss:[08h], bx"      \
    "mov bx,ss:[02h]"       \
    "mov ss:[0Ah], bx"      \
    "mov bx,ss:[04h]"       \
    "mov ss:[0Ch], bx"      \
    "mov ds, ax"            \
    "skip_ds:"              \
    "mov eax, cr0"          \
    "dec ax"                \
    "mov cr0, eax"          \
    parm nomemory modify nomemory;

/* Complete LOADALL emulation: Restore general-purpose registers, stack
 * pointer, and CS:IP. NB: The LOADALL instruction stores registers in
 * the same order as PUSHA. Surprise, surprise!
 */
void ldall_finish(void);
#pragma aux ldall_finish =  \
    ".286"                  \
    "mov sp, 26h"           \
    "popa"                  \
    "mov sp, ss:[2Ch]"      \
    "sub sp, 6"             \
    "mov ss, ss:[20h]"      \
    "iret"                  \
    parm nomemory modify nomemory aborts;

#ifdef EMU_386_LOADALL

/* 386 version of the above. */
void ldal3_finish(void);
#pragma aux ldal3_finish =  \
    ".386"                  \
    "mov sp, 28h"           \
    "popad"                 \
    "mov sp, ss:[18h]"      \
    "sub sp, 6"             \
    "mov ss, ss:[48h]"      \
    "iret"                  \
    parm nomemory modify nomemory aborts;

/* 386 version of load_rm_segs.
 * NB: Must not touch CX!
 */
void load_rm_seg3(int seg_flags, uint16_t ss_base);
#pragma aux load_rm_seg3 =  \
    "mov ss, ax"            \
    "mov ax, ss:[44h]"      \
    "mov ds, ax"            \
    "mov ax, ss:[50h]"      \
    "mov es, ax"            \
    parm [ax] [cx] nomemory modify nomemory;

#endif

#define LOAD_ES     0x01    /* ES needs to be loaded in protected mode. */
#define LOAD_DS     0x02    /* DS needs to be loaded in protected mode. */

/*
 * The invalid opcode handler exists to work around fishy application
 * code and paper over CPU generation differences:
 *
 * - Skip redundant LOCK prefixes (allowed on 8086, #UD on 286+).
 * - Emulate just enough of 286 LOADALL.
 *
 */
void BIOSCALL inv_op_handler(uint16_t ds, uint16_t es, pusha_regs_t gr, volatile iret_addr_t ra)
{
    void __far  *ins = ra.cs :> ra.ip;

    if (*(uint8_t __far *)ins == 0xF0) {
         /* LOCK prefix - skip over it and try again. */
        ++ra.ip;
#if VBOX_BIOS_CPU >= 80386
    } else if (*(uint16_t __far *)ins == 0x050F) {
        /* 286 LOADALL. NB: Same opcode as SYSCALL. */
        ldall_286_s __far   *ldbuf = 0 :> 0x800;
        iret_addr_t __far   *ret_addr;
        uint32_t            seg_base;
        int                 seg_flags = 0;

        /* One of the challenges is that we must restore SS:SP as well
         * as CS:IP and FLAGS from the LOADALL buffer. We copy CS/IP/FLAGS
         * from the buffer just below the SS:SP values from the buffer so
         * that we can eventually IRET to the desired CS/IP/FLAGS/SS/SP
         * values in one go.
         */
        ret_addr = ldbuf->ss :> (ldbuf->sp - sizeof(iret_addr_t));
        ret_addr->ip = ldbuf->ip;
        ret_addr->cs = ldbuf->cs;
        ret_addr->flags.u.r16.flags = ldbuf->flags;

        /* Examine ES/DS. */
        seg_base = ldbuf->es_desc.base_lo | (uint32_t)ldbuf->es_desc.base_hi << 16;
        if (seg_base != (uint32_t)ldbuf->es << 4)
            seg_flags |= LOAD_ES;
        seg_base = ldbuf->ds_desc.base_lo | (uint32_t)ldbuf->ds_desc.base_hi << 16;
        if (seg_base != (uint32_t)ldbuf->ds << 4)
            seg_flags |= LOAD_DS;

        /* The LOADALL buffer doubles as a tiny GDT. */
        load_gdtr(0x800, 4 * 8 - 1);

        /* Store the ES base/limit/attributes in the unused words (GDT selector 8). */
        ldbuf->unused2[0] = ldbuf->es_desc.limit;
        ldbuf->unused2[1] = ldbuf->es_desc.base_lo;
        ldbuf->unused2[2] = (ldbuf->es_desc.attr << 8) | ldbuf->es_desc.base_hi;
        ldbuf->unused2[3] = 0;

        /* Store the DS base/limit/attributes in other unused words. */
        ldbuf->unused1[0] = ldbuf->ds_desc.limit;
        ldbuf->unused1[1] = ldbuf->ds_desc.base_lo;
        ldbuf->unused1[2] = (ldbuf->ds_desc.attr << 8) | ldbuf->ds_desc.base_hi;

        /* Load the IDTR as specified. */
        seg_base = ldbuf->idt_desc.base_lo | (uint32_t)ldbuf->idt_desc.base_hi << 16;
        load_idtr(seg_base, ldbuf->idt_desc.limit);

        /* Do the tricky bits now. */
        load_rm_segs(seg_flags);
        load_pm_segs();
        ldall_finish();
#ifdef EMU_386_LOADALL
    } else if (*(uint16_t __far *)ins == 0x070F) {
        /* 386 LOADALL. NB: Same opcode as SYSRET. */
        ldall_386_s __far   *ldbuf = (void __far *)es :> gr.u.r16.di;   /* Assume 16-bit value in EDI. */
        ldall_286_s __far   *ldbuf2 = 0 :> 0x800;
        iret_addr_t __far   *ret_addr;
        uint32_t            seg_base;
        int                 seg_flags = 0;

        /* NB: BIG FAT ASSUMPTION! Users of 386 LOADALL are assumed to also
         * have a 286 LOADALL buffer at physical address 800h. We use unused fields
         * in that buffer for temporary storage.
         */

        /* Set up return stack. */
        ret_addr = ldbuf->ss :> (ldbuf->esp - sizeof(iret_addr_t));
        ret_addr->ip = ldbuf->eip;
        ret_addr->cs = ldbuf->cs;
        ret_addr->flags.u.r16.flags = ldbuf->eflags;

        /* Examine ES/DS. */
        seg_base = ldbuf->es_desc.base;
        if (seg_base != (uint32_t)ldbuf->es << 4)
            seg_flags |= LOAD_ES;
        seg_base = ldbuf->ds_desc.base;
        if (seg_base != (uint32_t)ldbuf->ds << 4)
            seg_flags |= LOAD_DS;

        /* The LOADALL buffer doubles as a tiny GDT. */
        load_gdtr(0x800, 4 * 8 - 1);

        /* Store the ES base/limit/attributes in the unused words (GDT selector 8). */
        ldbuf2->unused2[0] = ldbuf->es_desc.limit;
        ldbuf2->unused2[1] = (uint16_t)ldbuf->es_desc.base;
        ldbuf2->unused2[2] = (ldbuf->es_desc.attr & 0xFF00) | (ldbuf->es_desc.base >> 16);
        ldbuf2->unused2[3] = 0;

        /* Store the DS base/limit/attributes in other unused words. */
        ldbuf2->unused1[0] = ldbuf->ds_desc.limit;
        ldbuf2->unused1[1] = (uint16_t)ldbuf->ds_desc.base;
        ldbuf2->unused1[2] = (ldbuf->ds_desc.attr & 0xFF00) | (ldbuf->ds_desc.base >> 16);

        /* Load the IDTR as specified. */
        seg_base = ldbuf->idt_desc.base;
        load_idtr(seg_base, ldbuf->idt_desc.limit);

        /* Do the tricky bits now. */
        load_rm_seg3(es, seg_flags);
        load_pm_segs();
        ldal3_finish();
#endif
#endif
    } else {
        /* There isn't much point in executing the invalid opcode handler
         * in an endless loop, so halt right here.
         */
        int_enable();
        halt_forever();
    }
}
