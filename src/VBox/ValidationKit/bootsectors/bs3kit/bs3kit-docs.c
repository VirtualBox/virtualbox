/* $Id: bs3kit-docs.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Documentation.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */



/** @page pg_bs3kit BS3Kit - Boot Sector Kit \#3
 *
 * The BS3Kit is a framework for bare metal floppy/usb image tests.
 *
 * The 3rd iteration of the framework includes support for 16-bit and 32-bit
 * C/C++ code, with provisions for 64-bit C code to possibly be added later.
 * The C code have to do without a runtime library, otherwhat what we can share
 * possibly with IPRT.
 *
 * This iteration also adds a real linker into the picture, which is an
 * improvment over early when all had to done in a single assembler run with
 * lots of includes and macros controlling what we needed.  The functions are no
 * in separate files and compiled/assembled into libraries, so the linker will
 * only include exactly what is needed.  The current linker is the OpenWatcom
 * one, wlink, that we're already using when building the BIOSes.  If it wasn't
 * for the segment/selector fixups in 16-bit code (mostly), maybe we could
 * convince the ELF linker from GNU binutils to do the job too (with help from
 * the ).
 *
 *
 * @sa grp_bs3kit, grp_bs3kit_tmpl, grp_bs3kit_cmn, grp_bs3kit_mode,
 *     grp_bs3kit_system
 *
 * @section sec_calling_convention      Calling convention
 *
 * Because we're not mixing with C code, we will use __cdecl for 16-bit and
 * 32-bit code, where as 64-bit code will use the microsoft calling AMD64
 * convention.  To avoid unnecessary %ifdef'ing in assembly code, we will use a
 * macro to load the RCX, RDX, R8 and R9 registers off the stack in 64-bit
 * assembly code.
 *
 * Register treatment in 16-bit __cdecl, 32-bit __cdecl and 64-bit msabi:
 *
 * | Register     | 16-bit      | 32-bit     | 64-bit          | ASM template |
 * | ------------ | ----------- | ---------- | --------------- | ------------ |
 * | EAX, RAX     | volatile    | volatile   | volatile        | volatile     |
 * | EBX, RBX     | volatile    | preserved  | preserved       | both         |
 * | ECX, RCX     | volatile    | volatile   | volatile, arg 0 | volatile     |
 * | EDX, RDX     | volatile    | volatile   | volatile, arg 1 | volatile     |
 * | ESP, RSP     | preserved   | preserved  | preserved       | preserved    |
 * | EBP, RBP     | preserved   | preserved  | preserved       | preserved    |
 * | EDI, RDI     | preserved   | preserved  | preserved       | preserved    |
 * | ESI, RSI     | preserved   | preserved  | preserved       | preserved    |
 * | R8           | volatile    | volatile   | volatile, arg 2 | volatile     |
 * | R9           | volatile    | volatile   | volatile, arg 3 | volatile     |
 * | R10          | volatile    | volatile   | volatile        | volatile     |
 * | R11          | volatile    | volatile   | volatile        | volatile     |
 * | R12          | volatile    | volatile   | preserved       | preserved(*) |
 * | R13          | volatile    | volatile   | preserved       | preserved(*) |
 * | R14          | volatile    | volatile   | preserved       | preserved(*) |
 * | R15          | volatile    | volatile   | preserved       | preserved(*) |
 * | RFLAGS.DF    | =0          | =0         | =0              | =0           |
 * | CS           | preserved   | preserved  | preserved       | preserved    |
 * | DS           | preserved!  | preserved? | preserved       | both         |
 * | ES           | volatile    | volatile   | preserved       | volatile     |
 * | FS           | preserved   | preserved  | preserved       | preserved    |
 * | GS           | preserved   | volatile   | preserved       | both         |
 * | SS           | preserved   | preserved  | preserved       | preserved    |
 *
 * The 'both' here means that we preserve it wrt to our caller, while at the
 * same time assuming anything we call will clobber it.
 *
 * The 'preserved(*)' marking of R12-R15 indicates that they'll be preserved in
 * 64-bit mode, but may be changed in certain cases when running 32-bit or
 * 16-bit code.  This is especially true if switching CPU mode, e.g. from 32-bit
 * protected mode to 32-bit long mode.
 *
 * Return values are returned in the xAX register, but with the following
 * caveats for values larger than ARCH_BITS:
 *      - 16-bit code:
 *          - 32-bit values are returned in AX:DX, where AX holds bits 15:0 and
 *            DX bits 31:16.
 *          - 64-bit values are returned in DX:CX:BX:AX, where DX holds bits
 *            15:0, CX bits 31:16, BX bits 47:32, and AX bits 63:48.
 *      - 32-bit code:
 *          - 64-bit values are returned in EAX:EDX, where eax holds the least
 *            significant bits.
 *
 * The DS segment register is pegged to BS3DATA16_GROUP in 16-bit code so that
 * we don't need to reload it all the time.  This allows us to modify it in
 * ring-0 and mode switching code without ending up in any serious RPL or DPL
 * trouble.  In 32-bit and 64-bit mode the DS register is a flat, unlimited,
 * writable selector.
 *
 * In 16-bit and 32-bit code we do not assume anything about ES, FS, and GS.
 *
 *
 * For an in depth coverage of x86 and AMD64 calling convensions, see
 * http://homepage.ntlworld.com/jonathan.deboynepollard/FGA/function-calling-conventions.html
 *
 *
 *
 * @section sec_modes               Execution Modes
 *
 * BS3Kit defines a number of execution modes in order to be able to test the
 * full CPU capabilities (that VirtualBox care about anyways).  It currently
 * omits system management mode, hardware virtualization modes, and security
 * modes as those aren't supported by VirtualBox or are difficult to handle.
 *
 * The modes are categorized into normal and weird ones.
 *
 * The normal ones are:
 *    + RM     - Real mode.
 *    + PE16   - Protected  mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PE32   - Protected  mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *    + PEV86  - Protected  mode running  v8086 code, 32-bit TSS and 32-bit handlers.
 *    + PP16   - 386  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PP32   - 386  paged mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *    + PPV86  - 386  paged mode running  v8086 code, 32-bit TSS and 32-bit handlers.
 *    + PAE16  - PAE  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PAE32  - PAE  paged mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *    + PAEV86 - PAE  paged mode running  v8086 code, 32-bit TSS and 32-bit handlers.
 *    + LM16   - AMD64 long mode running 16-bit code, 64-bit TSS and 64-bit handlers.
 *    + LM32   - AMD64 long mode running 32-bit code, 64-bit TSS and 64-bit handlers.
 *    + LM64   - AMD64 long mode running 64-bit code, 64-bit TSS and 64-bit handlers.
 *
 * The weird ones:
 *    + PE16_32   - Protected  mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PE16_V86  - Protected  mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PE32_16   - Protected  mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *    + PP16_32   - 386  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PP16_V86  - 386  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PP32_16   - 386  paged mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *    + PAE16_32  - PAE  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PAE16_V86 - PAE  paged mode running 16-bit code, 16-bit TSS and 16-bit handlers.
 *    + PAE32_16  - PAE  paged mode running 32-bit code, 32-bit TSS and 32-bit handlers.
 *
 * Actually, the PE32_16, PP32_16 and PAE32_16 modes aren't all that weird and fits in
 * right next to LM16 and LM32, but this is the way it ended up. :-)
 *
 */

