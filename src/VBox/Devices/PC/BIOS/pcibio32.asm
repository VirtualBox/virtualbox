; $Id: pcibio32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BIOS32 service directory and 32-bit PCI BIOS entry point
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
; --------------------------------------------------------------------


; Public symbols for debugging only
public		pcibios32_entry
public		bios32_service

; The BIOS32 service directory header must be located in the E0000h-FFFF0h
; range on a paragraph boundary. Note that the actual 32-bit code need not
; be located below 1MB at all.

_DATA		segment public 'DATA'

align   16
bios32_directory:
		db	'_32_'		; ASCII signature
		dw	bios32_service	; Entry point address...
		dw	000Fh 		; ...hardcoded to F000 segment
		db	0		; Revision
		db	1		; Length in paras - must be 1
		db	0		; Checksum calculated later
		db 	5 dup(0)	; Unused, must be zero

_DATA		ends

.386

extrn	_pci32_function:near

BIOS32		segment	public 'CODE' use32

;
; The BIOS32 Service Directory - must be less than 4K in size (easy!).
;
bios32_service	proc	far

		pushfd

		cmp	bl, 0			; Only function 0 supported
		jnz	b32_bad_func

		cmp	eax, 'ICP$'		; "$PCI"
		mov	al, 80h			; Unknown service
		jnz	b32_done

		mov	ebx, 000f0000h		; Base address (linear)
		mov	ecx, 0f000h		; Length of service
		mov	edx, pcibios32_entry	; Entry point offset from base
		xor	al, al			; Indicate success
b32_done:
		popfd
		retf

b32_bad_func:
		mov	al, 81h			; Unsupported function
		jmp	b32_done

bios32_service	endp

;
; The 32-bit PCI BIOS entry point - simply calls into C code.
;
pcibios32_entry	proc	far

		pushfd				; Preserve flags
		cld				; Just in case...

		push	es			; Call into C implementation
		pushad
		call	_pci32_function
		popad
		pop	es

		popfd				; Restore flags and return
		retf

pcibios32_entry	endp


BIOS32		ends

		end

