; $Id: pmsetup.inc 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Initial system setup which needs to run in protected mode.
;

;
; Copyright (C) 2004-2024 Oracle and/or its affiliates.
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
; SPDX-License-Identifier: GPL-3.0-only
;

SVR		equ	0FEE000F0h
LVT_LINT0	equ	0FEE00350h
LVT_LINT1	equ	0FEE00360h

public		pmode_setup

;; Enable the local APIC and program LINT0/LINT1 entries. Without that,
;; virtual wire interrupts could not be delivered. Note that the APIC must
;; be enabled first because when disabled, all LVTs are forced masked.

pmode_setup	proc	near

		.386
		push	eax
		push	esi
		pushf
		cli			; Interrupts would kill us!
		call	pmode_enter

		mov	eax, cr0	; Clear CR0.CD and CR0.NW
		and	eax, 09FFFFFFFh
		mov	cr0, eax

		mov	esi, SVR	; Program the SVR -- enable the APIC,
		mov	eax, 010Fh	; set spurious interrupt vector to 15
		mov	[esi], eax

		mov	esi, LVT_LINT0	; Program LINT0 to ExtINT and unmask
		mov	eax, [esi]
		and	eax, 0FFFE00FFh
		or	ah,  7
		mov	[esi], eax

		mov	esi, LVT_LINT1	; Program LINT1 to NMI and unmask
		mov	eax, [esi]
		and	eax, 0FFFE00FFh
		or	ah,  4
		mov	[esi], eax

		call	pmode_exit
		popf
		pop	esi
		pop	eax
		SET_DEFAULT_CPU_286
		ret

pmode_setup	endp

