; $Id: zero.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Zero Memory.
;

;
; Copyright (C) 2013-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
; in the VirtualBox distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;
; SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
;

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"

;
; Section to put this in.
;
;       - PE/COFF: Unless we put this in an BSS like section, the linker will
;         write out 64KB of zeros.  (Tried using .rdata$zz and put it at the end
;         of that section, but the linker did not reduce the RawDataSize.)  The
;         assmebler does not let us control the write flag directly, so we emit
;         a linker directive that switches of the write flag for the section.
;
;       - Fallback: Code section.
;
%ifdef ASM_FORMAT_PE
section .drectve info
        db '-section:.zero,!W '
section .zero bss align=4096
%else
BEGINCODE
%endif

;;
; 64KB of zero memory with various sized labels.
;
EXPORTEDNAME_EX g_abRTZeroPage, object
EXPORTEDNAME_EX g_abRTZero4K, object
EXPORTEDNAME_EX g_abRTZero8K, object
EXPORTEDNAME_EX g_abRTZero16K, object
EXPORTEDNAME_EX g_abRTZero32K, object
EXPORTEDNAME_EX g_abRTZero64K, object
%ifdef ASM_FORMAT_PE
        resb  0x10000
%else
        times 0x10000/(16*4) dd 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
%endif
%ifdef ASM_FORMAT_ELF
size g_abRTZeroPage     _4K
size g_abRTZero4K       _4K
size g_abRTZero8K       _8K
size g_abRTZero16K     _16K
size g_abRTZero32K     _32K
size g_abRTZero64K     _64K
%endif

