; $Id: vbdmismi.inc 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; DMI and SMBIOS definitions for VBox.
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
;

;; TODO: make dynamic

VBOX_DMI_TABLE_BASE	equ	0E1000h
VBOX_DMI_TABLE_VER	equ	25h
VBOX_DMI_TABLE_SIZE	equ	352

VBOX_SMBIOS_MAJOR_VER	equ	2
VBOX_SMBIOS_MINOR_VER	equ	5
VBOX_SMBIOS_MAXSS	equ	0FFh

