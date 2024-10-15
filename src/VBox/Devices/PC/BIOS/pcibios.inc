; $Id: pcibios.inc 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; ???
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
;
; This code is based on:
;
;  ROM BIOS for use with Bochs/Plex86/QEMU emulation environment
;
;  Copyright (C) 2002  MandrakeSoft S.A.
;
;    MandrakeSoft S.A.
;    43, rue d'Aboukir
;    75002 Paris - France
;    http://www.linux-mandrake.com/
;    http://www.mandrakesoft.com/
;
;  This library is free software; you can redistribute it and/or
;  modify it under the terms of the GNU Lesser General Public
;  License as published by the Free Software Foundation; either
;  version 2 of the License, or (at your option) any later version.
;
;  This library is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;  Lesser General Public License for more details.
;
;  You should have received a copy of the GNU Lesser General Public
;  License along with this library; if not, write to the Free Software
;  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
;

; Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
; other than GPL or LGPL is available it will apply instead, Oracle elects to use only
; the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
; a choice of LGPL license versions is made available with the language indicating
; that LGPLv2 or any later version may be used, or where a choice of which version
; of the LGPL is applied is otherwise unspecified.


include	pcicfg.inc

if BX_PCIBIOS

ifdef DEBUG

; Publics for easier debugging and disassembly

public		pcibios_init_iomem_bases
public		pci_init_io_loop1
public		pci_init_io_loop2
public		init_io_base
public		next_pci_base
public		enable_iomem_space
public		next_pci_dev
public		pcibios_init_set_elcr
public		is_master_pic
public		pcibios_init_irqs
public		pci_init_irq_loop1
public		pci_init_irq_loop2
public		pci_test_int_pin
public		pirq_found
public		next_pci_func
public		next_pir_entry
public		pci_init_end

endif

.386

if not BX_ROMBIOS32
pci_irq_list:
		db 11, 10, 9, 11

pcibios_init_sel_reg:
		push	eax
		mov	eax, 800000h
		mov	ax,  bx
		shl	eax, 8
		and	dl,  0FCh
		or	al,  dl
		mov	dx,  PCI_CFG1
		out	dx,  eax
		pop	eax
		ret

pcibios_init_iomem_bases:
		push	bp
		mov	bp, sp
ifdef VBOX
		mov	eax,19200509
		mov	dx,410h
		out	dx,  eax
else
; This incomplete PCI resource setup code is less functional than the PCI
; resource assignment created by the fake PCI BIOS and is therefore disabled.
; Blindly enabling everything on the root bus (including bus mastering!) can
; only be called buggy. It causes the trouble with AMD PCNet which it then
; tries to work around, but that still contains a race.
		mov	eax, 0E0000000h	; base for memory init
		push	eax
		mov	ax, 0D000h	; base for i/o init
		push	ax
		mov	ax, 010h	; start at base address #0
		push	ax
		mov	bx, 8
pci_init_io_loop1:
		mov	dl, 0
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	ax, dx
		cmp	ax, 0FFFFh
		jz	next_pci_dev

ifndef VBOX ; This currently breaks restoring a previously saved state.
		mov	dl, 4	; disable i/o and memory space access
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	al, dx
		and	al, 0FCh
		out	dx, al
pci_init_io_loop2:
		mov	dl, [bp-8]
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	eax, dx
		test	al, 1
		jnz	init_io_base

		mov	ecx, eax
		mov	eax, 0FFFFFFFFh
		out	dx, eax
		in	eax, dx
		cmp	eax, ecx
		je	next_pci_base
		xor	eax, 0FFFFFFFFh
		mov	ecx, eax
		mov	eax, [bp-4]
		out	dx, eax
		add	eax, ecx	; calculate next free mem base
		add	eax, 01000000h
		and	eax, 0FF000000h
		mov	[bp-4], eax
		jmp	next_pci_base

init_io_base:
		mov	cx, ax
		mov	ax, 0FFFFh
		out	dx, eax
		in	eax, dx
		cmp	ax, cx
		je	next_pci_base

		xor	ax, 0FFFEh
		mov	cx, ax
		mov	ax, [bp-6]
		out	dx, eax
		add	ax, cx	; calculate next free i/o base
		add	ax, 00100h
		and	ax, 0FF00h
		mov	[bp-6], ax
next_pci_base:
		mov	al, [bp-8]
		add	al, 4
		cmp	al, 28h
		je	enable_iomem_space

		mov	byte ptr[bp-8], al
		jmp	pci_init_io_loop2
endif ; !VBOX

enable_iomem_space:
		mov	dl, 4 ;; enable i/o and memory space access if available
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	al, dx
		or	al, 7
		out	dx, al
ifdef VBOX
		mov	dl, 0	; check if PCI device is AMD PCNet
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	eax, dx
		cmp	eax, 020001022h
		jne	next_pci_dev

		mov	dl, 10h	; get I/O address
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	ax, dx
		and	ax, 0FFFCh
		mov	cx, ax
		mov	dx, cx
		add	dx, 14h	; reset register if PCNet is in word I/O mode
		in	ax, dx	; reset is performed by reading the reset register
		mov	dx, cx
		add	dx, 18h	; reset register if PCNet is in word I/O mode
		in	eax, dx	; reset is performed by reading the reset register
endif ; VBOX
next_pci_dev:
		mov	byte ptr[bp-8], 10h
		inc	bx
		cmp	bx, 0100h
		jne	pci_init_io_loop1
endif ; !VBOX
		mov	sp, bp
		pop	bp
		ret

pcibios_init_set_elcr:
		push	ax
		push	cx
		mov	dx, 04D0h
		test	al, 8
		jz	is_master_pic

		inc	dx
		and	al, 7
is_master_pic:
		mov	cl, al
		mov	bl, 1
		shl	bl, cl
		in	al, dx
		or	al, bl
		out	dx, al
		pop	cx
		pop	ax
		ret

pcibios_init_irqs:
		push	ds
		push	bp
		mov	ax, 0F000h
		mov	ds, ax
ifndef VBOX
; this code works OK, but it's unnecessary effort since the fake PCI BIOS
; already configured the IRQ lines and the ELCR correctly
		mov	dx, 04D0h ;; reset ELCR1 + ELCR2
		mov	al, 0
		out	dx, al
		inc	dx
		out	dx, al
		mov	si, pci_routing_table_structure
		mov	bh, [si+8]
		mov	bl, [si+9]
		mov	dl, 0
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	eax, dx
		cmp	eax, [si+12] ;; check irq router
		jne	pci_init_end

		mov	dl, [si+34]
		call	pcibios_init_sel_reg
		push	bx ;; save irq router bus + devfunc
		mov	dx, PCI_CFG2
		mov	ax, 8080h
		out	dx, ax ;; reset PIRQ route control
		add	dx, 2
		out	dx, ax
		mov	ax, [si+6]
		sub	ax, 20h
		shr	ax, 4
		mov	cx, ax
		add	si, 20h ;; set pointer to 1st entry
		mov	bp, sp
		mov	ax, pci_irq_list
		push	ax
		xor	ax, ax
		push	ax
pci_init_irq_loop1:
		mov	bh, [si]
		mov	bl, [si+1]
pci_init_irq_loop2:
		mov	dl, 0
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		in	ax, dx
		cmp	ax, 0FFFFh
		jnz	pci_test_int_pin

		test	bl, 7
		jz	next_pir_entry

		jmp	next_pci_func

pci_test_int_pin:
		mov	dl, 3Ch
		call pcibios_init_sel_reg
		mov	dx, PCI_CFG2 + 1 ; access config space at 3Dh
		in	al, dx
		and	al, 7
		jz	next_pci_func

		dec	al ;; determine pirq reg
		mov	dl, 3
		mul	dl
		add	al, 2
		xor	ah, ah
		mov	bx, ax
		mov	al, [si+bx]
		mov	dl, al
		mov	bx, [bp]
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		and	al, 3
		add	dl, al
		in	al, dx
		cmp	al, 80h
		jb	pirq_found

		mov	bx, [bp-2] ;; pci irq list pointer
		mov	al, [bx]
		out	dx, al
		inc	bx
		mov	[bp-2], bx
		call	pcibios_init_set_elcr
pirq_found:
		mov	bh, [si]
		mov	bl, [si+1]
		add	bl, [bp-3] ;; pci function number
		mov	dl, 3Ch
		call	pcibios_init_sel_reg
		mov	dx, PCI_CFG2
		out	dx, al
next_pci_func:
		inc	byte ptr[bp-3]
		inc	bl
		test	bl, 7
		jnz	pci_init_irq_loop2

next_pir_entry:
		add	si, 10h
		mov	byte ptr[bp-3], 0
		loop	pci_init_irq_loop1

		mov	sp, bp
		pop	bx
pci_init_end:
endif
		pop		bp
		pop		ds
		ret

endif		 ; !BX_ROMBIOS32

endif		 ; BX_PCIBIOS

SET_DEFAULT_CPU_286

