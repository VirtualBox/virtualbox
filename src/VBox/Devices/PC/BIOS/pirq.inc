; $Id: pirq.inc 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
;

; Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
; other than GPL or LGPL is available it will apply instead, Oracle elects to use only
; the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
; a choice of LGPL license versions is made available with the language indicating
; that LGPLv2 or any later version may be used, or where a choice of which version
; of the LGPL is applied is otherwise unspecified.


if BX_PCIBIOS
public _pci_routing_table
public _pci_routing_table_size

align 16
pci_routing_table_structure:
  db '$PIR'  ;; "$PIR" signature
  db 0, 1 ;; version
ifdef VBOX
  dw 32 + (30 * 16) ;; table size
else ; !VBOX
  dw 32 + (6 * 16) ;; table size
endif ; !VBOX
  db 0 ;; PCI interrupt router bus
  db 8 ;; PCI interrupt router DevFunc
  dw 0000h ;; PCI exclusive IRQs
  dw 8086h ;; compatible PCI interrupt router vendor ID
  dw 7000h ;; compatible PCI interrupt router device ID
  dw 0,0 ;; Miniport data
  db 0,0,0,0,0,0,0,0,0,0,0 ;; reserved
ifdef VBOX
  db 0 ;; checksum (set by biossums)
else ; !VBOX
  db 7 ;; checksum
endif ; !VBOX
_pci_routing_table:
pci_routing_table:
  ;; first slot entry PCI-to-ISA (embedded)
  db 0 ;; pci bus number
  db 008h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#: pointer into PCI2ISA config space
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 0 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; second slot entry: 1st PCI slot
  db 0 ;; pci bus number
  db 010h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 1 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; third slot entry: 2nd PCI slot
  db 0 ;; pci bus number
  db 018h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 2 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 4th slot entry: 3rd PCI slot
  db 0 ;; pci bus number
  db 020h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 3 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 5th slot entry: 4th PCI slot
  db 0 ;; pci bus number
  db 028h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 4 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 6th slot entry: 5th PCI slot
  db 0 ;; pci bus number
  db 030h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 5 ;; physical slot (0 = embedded)
  db 0 ;; reserved
ifdef VBOX
  ;; 7th slot entry: 6th PCI slot
  db 0 ;; pci bus number
  db 038h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 6 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 8th slot entry: 7th PCI slot
  db 0 ;; pci bus number
  db 040h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 7 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 9th slot entry: 8th PCI slot
  db 0 ;; pci bus number
  db 048h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 8 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 10th slot entry: 9th PCI slot
  db 0 ;; pci bus number
  db 050h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 9 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 11th slot entry: 10th PCI slot
  db 0 ;; pci bus number
  db 058h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 10 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 12th slot entry: 11th PCI slot
  db 0 ;; pci bus number
  db 60h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 11 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 13th slot entry: 12th PCI slot
  db 0 ;; pci bus number
  db 068h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 12 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 14th slot entry: 13th PCI slot
  db 0 ;; pci bus number
  db 070h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 13 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 15th slot entry: 14th PCI slot
  db 0 ;; pci bus number
  db 078h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 14 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 16th slot entry: 15th PCI slot
  db 0 ;; pci bus number
  db 080h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 15 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 17th slot entry: 16th PCI slot
  db 0 ;; pci bus number
  db 088h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 16 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 18th slot entry: 17th PCI slot
  db 0 ;; pci bus number
  db 090h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 17 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 19th slot entry: 18th PCI slot
  db 0 ;; pci bus number
  db 098h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 18 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 20th slot entry: 19th PCI slot
  db 0 ;; pci bus number
  db 0A0h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 19 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 21st slot entry: 20th PCI slot
  db 0 ;; pci bus number
  db 0A8h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 20 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 22nd slot entry: 21st PCI slot
  db 0 ;; pci bus number
  db 0B0h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 21 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 23rd slot entry: 22nd PCI slot
  db 0 ;; pci bus number
  db 0B8h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 22 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 24th slot entry: 23rd PCI slot
  db 0 ;; pci bus number
  db 0C0h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 23 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 25th slot entry: 24th PCI slot
  db 0 ;; pci bus number
  db 0C8h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 24 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 26th slot entry: 25th PCI slot
  db 0 ;; pci bus number
  db 0D0h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 25 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 27th slot entry: 26th PCI slot
  db 0 ;; pci bus number
  db 0D8h ;; pci device number (bit 7-3)
  db 62h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 63h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 60h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 61h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 26 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 28th slot entry: 27th PCI slot
  db 0 ;; pci bus number
  db 0E0h ;; pci device number (bit 7-3)
  db 63h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 60h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 61h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 62h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 27 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 29th slot entry: 28th PCI slot
  db 0 ;; pci bus number
  db 0E8h ;; pci device number (bit 7-3)
  db 60h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 61h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 62h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 63h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 28 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 30th slot entry: 29th PCI slot
  db 0 ;; pci bus number
  db 0F0h ;; pci device number (bit 7-3)
  db 61h ;; link value INTA#
  dw 0DEF8h ;; IRQ bitmap INTA#
  db 62h ;; link value INTB#
  dw 0DEF8h ;; IRQ bitmap INTB#
  db 63h ;; link value INTC#
  dw 0DEF8h ;; IRQ bitmap INTC#
  db 60h ;; link value INTD#
  dw 0DEF8h ;; IRQ bitmap INTD#
  db 29 ;; physical slot (0 = embedded)
  db 0 ;; reserved
endif ; VBOX
pci_routing_table_end:
;; Size of the table
_pci_routing_table_size dw	pci_routing_table_end - pci_routing_table

endif   ; BX_PCIBIOS
