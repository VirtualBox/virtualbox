/* $Id: inlines.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Inline routines for Watcom C.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_inlines_h
#define VBOX_INCLUDED_SRC_PC_BIOS_inlines_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

extern unsigned inp(unsigned port);
extern unsigned outp(unsigned port, unsigned value);
extern unsigned inpw(unsigned port);
extern unsigned outpw(unsigned port, unsigned value);
#pragma intrinsic(inp,outp,inpw,outpw)
#define inb(p)      inp(p)
#define outb(p, v)  outp(p, v)
#define inw(p)      inpw(p)
#define outw(p, v)  outpw(p, v)

/* Far byte/word/dword access routines. */

inline uint8_t read_byte(uint16_t seg, uint16_t offset)
{
    return( *(seg:>(uint8_t *)offset) );
}

inline void write_byte(uint16_t seg, uint16_t offset, uint8_t data)
{
    *(seg:>(uint8_t *)offset) = data;
}

inline uint16_t read_word(uint16_t seg, uint16_t offset)
{
    return( *(seg:>(uint16_t *)offset) );
}

inline void write_word(uint16_t seg, uint16_t offset, uint16_t data)
{
    *(seg:>(uint16_t *)offset) = data;
}

inline uint32_t read_dword(uint16_t seg, uint16_t offset)
{
    return( *(seg:>(uint32_t *)offset) );
}

inline void write_dword(uint16_t seg, uint16_t offset, uint32_t data)
{
    *(seg:>(uint32_t *)offset) = data;
}


void int_enable(void);
#pragma aux int_enable = "sti" modify exact [] nomemory;

void int_disable(void);
#pragma aux int_disable = "cli" modify exact [] nomemory;

void int_enable_hlt_disable(void);
#pragma aux int_enable_hlt_disable = \
    "sti" \
    "hlt" \
    "cli" \
    modify exact [] nomemory;

uint16_t int_query(void);
#pragma aux int_query =     \
    "pushf"                 \
    "pop    ax"             \
    value [ax] modify exact [ax] nomemory;

void int_restore(uint16_t old_flags);
#pragma aux int_restore =   \
    "push   ax"             \
    "popf"                  \
    parm [ax] modify exact [] nomemory;

void halt(void);
#pragma aux halt = "hlt" modify exact [] nomemory;

void halt_forever(void);
#pragma aux halt_forever =  \
    "forever:"              \
    "hlt"                   \
    "jmp forever"           \
    modify exact [] nomemory aborts;

/* Output a null-terminated string to a specified port, without the
 * terminating null character.
 */
static void out_ctrl_str_asm(uint16_t port, const char *s);
#pragma aux out_ctrl_str_asm =   \
    "mov    al, [bx]"       \
    "next:"                 \
    "out    dx, al"         \
    "inc    bx"             \
    "mov    al, [bx]"       \
    "or     al, al"         \
    "jnz    next"           \
    parm [dx] [bx] modify exact [ax bx] nomemory;

#ifdef __386__

void rep_movsb(void __far *d, void __far *s, int nbytes);
#pragma aux rep_movsb =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsb"          \
    "pop    ds"             \
    parm [es edi] [dx esi] [ecx];

#else

void rep_movsb(void __far *d, void __far *s, int nbytes);
#pragma aux rep_movsb =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsb"          \
    "pop    ds"             \
    parm [es di] [dx si] [cx];

#endif

void rep_movsw(void __far *d, void __far *s, int nwords);
#pragma aux rep_movsw =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsw"          \
    "pop    ds"             \
    parm [es di] [dx si] [cx];

#ifndef __386__

char __far *rep_insb(char __far *buffer, unsigned nbytes, unsigned port);
#pragma aux rep_insb = ".286" "rep insb" parm [es di] [cx] [dx] value [es di] modify exact [cx di];

char __far *rep_insw(char __far *buffer, unsigned nwords, unsigned port);
#if VBOX_BIOS_CPU >= 80286
#pragma aux rep_insw = ".286" "rep insw" parm [es di] [cx] [dx] value [es di] modify exact [cx di];
#else
#pragma aux rep_insw = "next:" "in ax,dx" "stosw" "loop next" parm [es di] [cx] [dx] value [es di] modify exact [cx di];
#endif

# if VBOX_BIOS_CPU >= 80386
char __far *rep_insd(char __far *buffer, unsigned ndwords, unsigned port);
#  pragma aux rep_insd = ".386" "rep insd" parm [es di] [cx] [dx] value [es di] modify exact [cx di];
# endif

char __far *rep_outsb(char __far *buffer, unsigned nbytes, unsigned port);
#pragma aux rep_outsb = ".286" "rep outs dx,byte ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];

char __far *rep_outsw(char __far *buffer, unsigned nwords, unsigned port);
#if VBOX_BIOS_CPU >= 80286
#pragma aux rep_outsw = ".286" "rep outs dx,word ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];
#else
#pragma aux rep_outsw = "next:" "lods word ptr es:[si]" "out dx,ax" "loop next" parm [es si] [cx] [dx] value [es si] modify exact [cx si];
#endif

# if VBOX_BIOS_CPU >= 80386
char __far *rep_outsd(char __far *buffer, unsigned ndwords, unsigned port);
#  pragma aux rep_outsd = ".386" "rep outs dx,dword ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];
# endif

uint16_t swap_16(uint16_t val);
#pragma aux swap_16 = "xchg ah,al" parm [ax] value [ax] modify exact [ax] nomemory;

uint32_t swap_32(uint32_t val);
#pragma aux swap_32 =   \
    "xchg   ah, al"     \
    "xchg   dh, dl"     \
    "xchg   ax, dx"     \
    parm [dx ax] value [dx ax] modify exact [dx ax] nomemory;

uint64_t swap_64(uint64_t val);
#pragma aux swap_64 =   \
    "xchg   ah, al"     \
    "xchg   bh, bl"     \
    "xchg   ch, cl"     \
    "xchg   dh, dl"     \
    "xchg   ax, dx"     \
    "xchg   bx, cx"     \
    parm [ax bx cx dx] value [ax bx cx dx] modify exact [ax bx cx dx] nomemory;

#endif

#if VBOX_BIOS_CPU >= 80386

/* Warning: msr_read/msr_write destroy high bits of 32-bit registers (EAX, ECX, EDX). */

uint64_t msr_read(uint32_t msr);
#pragma aux msr_read =  \
    ".586"              \
    "shl    ecx, 16"    \
    "mov    cx, ax"     \
    "rdmsr"             \
    "xchg   eax, edx"   \
    "mov    bx, ax"     \
    "shr    eax, 16"    \
    "mov    cx, dx"     \
    "shr    edx, 16"    \
    "xchg   dx, cx"     \
    parm [cx ax] value [ax bx cx dx] modify [] nomemory;

void msr_write(uint64_t val, uint32_t msr);
#pragma aux msr_write =  \
    ".586"              \
    "shl    eax, 16"    \
    "mov    ax, bx"     \
    "xchg   dx, cx"     \
    "shl    edx, 16"    \
    "mov    dx, cx"     \
    "xchg   eax, edx"   \
    "mov    cx, di"     \
    "shl    ecx, 16"    \
    "mov    cx, si"     \
    "wrmsr"             \
    parm [ax bx cx dx] [di si] modify [] nomemory;

/* Warning: eflags_read/eflags_write destroy high bits of 32-bit registers (EDX). */
uint32_t eflags_read( void );
#pragma aux eflags_read =   \
    ".386"                  \
    "pushfd"                \
    "pop  edx"              \
    "mov  ax, dx"           \
    "shr  edx, 16"          \
    value [dx ax] modify [dx ax];

uint32_t eflags_write( uint32_t e_flags );
#pragma aux eflags_write =  \
    ".386"                  \
    "shl  edx, 16"          \
    "mov  dx, ax"           \
    "push edx"              \
    "popfd"                 \
    parm [dx ax] modify [dx ax];

/* Warning cpuid destroys high bits of 32-bit registers (EAX, EBX, ECX, EDX). */
void cpuid( uint32_t __far cpu_id[4], uint32_t leaf );
#pragma aux cpuid =         \
    ".586"                  \
    "shl  edx, 16"          \
    "mov  dx, ax"           \
    "mov  eax, edx"         \
    "cpuid"                 \
    "mov  es:[di+0], eax"   \
    "mov  es:[di+4], ebx"   \
    "mov  es:[di+8], ecx"   \
    "mov  es:[di+12], edx"  \
    parm [es di] [dx ax] modify [bx cx dx]

#endif

#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_inlines_h */

