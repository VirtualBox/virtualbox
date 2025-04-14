/* $Id: old-solaris-asm-trick.h 109212 2025-04-14 17:29:48Z aleksey.ilyushin@oracle.com $ */
/** @file
 * OpenSSL - Hack for older binutils/gas on solaris (10).
 *
 * Disable the .cfi_xxxx directives for Solaris 10/amd64 with old gas, as gas will 
 * be creating .eh_frame with the wrong kind of of section type, causing the 
 * linker to stop with an fatal error.  Looks like this was addressed between
 * binutils 2.18 and 2.20.1. 
 * 
 * The .xref directive is ignored by gas v2.15, so we map the .cfi_xxxx stuff 
 * onto it.
 */

/*
 * Copyright (C) 2022 Oracle and/or its affiliates.
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

#define cfi_adjust_cfa_offset   xref
#define cfi_def_cfa             xref
#define cfi_def_cfa_register    xref
#define cfi_endproc             xref
#define cfi_escape              xref
#define cfi_offset              xref
#define cfi_register            xref
#define cfi_rel_offset          xref
#define cfi_remember_state      xref
#define cfi_restore             xref
#define cfi_restore_state       xref
#define cfi_startproc           xref
