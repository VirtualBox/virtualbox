; $Id: pmode.inc 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Enter and exit a minimal protected-mode environment.
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

;; Caveats: May only be called from the F000 segment (16-bit). Does not
;; switch stacks. Must be run with disabled interrupts(!), any exceptions
;; will cause a crash. On return from pmode_enter, DS contains a selector
;; which can address the entire 4GB address space.

public		pmode_enter
public		pmode_exit
public		pmbios_gdt_desc
public		pmbios_gdt

pmode_enter	proc	near

		push	cs
		pop 	ds
		.386p
		lgdt	fword ptr [pmbios_gdt_desc]
		mov	eax, cr0
		or	al, 1
		mov	cr0, eax
;		jmp	far ptr 20h:really_enter_pm
		db	0EAh
		dw	really_enter_pm
		dw	20h
really_enter_pm:
		mov	ax, 18h
		mov	ds, ax
		ret

pmode_enter	endp


pmode_exit	proc	near

		mov	ax, 40h		; Ensure RM limit/attributes
		mov	ds, ax		; (where base = selector << 4)

		mov	eax, cr0
		and	al, 0FEh
		mov	cr0, eax
		SET_DEFAULT_CPU_286
		jmp	far ptr really_exit_pm
really_exit_pm:
		ret

pmode_exit	endp



pmbios_gdt_desc:
		dw	40h + 7                 ; last selector plus 8 - 1
		dw	pmbios_gdt
		dw	000Fh

pmbios_gdt:
		dw 0, 0, 0, 0
		dw 0, 0, 0, 0
		dw 0ffffh, 0, 9b00h, 00cfh	; 32-bit code (0x10)
		dw 0ffffh, 0, 9300h, 00cfh	; 32-bit data (0x18)
		dw 0ffffh, 0, 9b0fh, 0000h	; 16-bit code, base=0xf0000
		dw 0ffffh, 0, 9300h, 0000h	; 16-bit data, base=0x0
		dw 0, 0, 0, 0
		dw 0, 0, 0, 0
		dw 0ffffh, 400h, 9300h, 0000h   ; 16-bit data, base=0x40

