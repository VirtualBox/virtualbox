# $Id: makefile 114667 2026-07-09 10:16:32Z klaus.espenlaub@oracle.com $
## @file
# wmake makefile for the PC BIOS.
#

#
# Copyright (C) 2011-2025 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

# Compiler flags:
#  -q   no banner
#  -wx  max warnings
#  -0   generate 8086 code
#  -ms  force small model (default)
#  -s   remove runtime stack checks (required)
#  -os  optimize for size
#  -zu  assume that SS != DS (required)
#  -ecc default to __cdecl calling convention (bad idea)
#
!ifdef __UNIX__
Q=\"
!else
Q="
!endif

CPU = 386

CFLAGS   = -q -0 -wx -zu -s -oas -d1+ -ms
CFLAGS32 = -q -wx -zu -s -oas -d1+ -ms -nt=BIOS32 -nd=BIOS32
# -oat seems to prevent ENTER/LEAVE generation
#CFLAGS32 = -q -wx -zu -s -oat -d1+ -ms -nt=BIOS32 -nd=BIOS32

DEFS = -DVBOX -DVBOX_LANBOOT_SEG=0xE200 -DVBOX_VERSION_STRING=$(Q)0.9$(Q) &
 -DVBOX_WITH_SCSI -DVBOX_BIOS_CPU=80386

AFLAGS = -q -0 -wx

INCLS  = -I$(Q)../../../../../include$(Q) -I$(Q) ../../BiosCommonCode$(Q)

OBJS = 	bios.obj post.obj ata.obj floppy.obj floppyt.obj eltorito.obj &
	boot.obj keyboard.obj disk.obj serial.obj system.obj invop.obj &
	timepci.obj logo.obj ps2mouse.obj parallel.obj scsi.obj &
	apm.obj apm_pm.obj pcibios.obj pciutil.obj vds.obj &
	print.obj pcibio32.obj pci32.obj orgs.obj

!if $(CPU) > 286
OBJS += ahci.obj
DEFS += -DVBOX_WITH_AHCI
!endif

.c.obj : .autodepend
	wcc -fo=.obj $(CFLAGS) $(DEFS) $(INCLS) $<

.asm.obj : .autodepend
	wasm -fo=.obj $(AFLAGS) $(DEFS) $(INCLS) $<

vbxbios.rom : vbxbios.bin
	biossums $< $@

vbxbios.bin : $(OBJS) $(__MAKEFILES__)
	wlink name $@ system dos debug all option quiet &
	 option nofarcalls, map, verbose, statics, symfile &
	 output raw offset=0xF0000 order &
	 clname DATA segaddr=0xF000 segment _DATA &
	 clname CODE &
	 segment _TEXT segaddr=0xF000 offset=0x1C00 &
	 segment BIOS32 segaddr=0xF000 offset=0xDB00 &
	 segment BIOSSEG segaddr=0xF000 offset=0xE000 &
	 file { $(OBJS) } &
	 library clibs.lib &
	 disable 1014, 1023, 2120

logo.obj : logo.c .autodepend
	wcc -fo=.obj $(INCLS) $(CFLAGS) $(DEFS) -DVBOX_PC_BIOS $<

pci32.obj : pci32.c .autodepend
	wcc386 -fo=.obj $(INCLS) $(DEFS) $(CFLAGS32) $<

clean : .symbolic
	@rm -f *.obj *.err
	@rm -f vbxbios.bin vbxbios.rom vbxbios.map vbxbios.sym
