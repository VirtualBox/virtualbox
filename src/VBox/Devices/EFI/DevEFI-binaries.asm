; $Id: DevEFI-binaries.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; DevEFI - firmware binaries.
;

;
; Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "VBox/asmdefs.mac"
%include "VBox/err.mac"
%include "iprt/x86.mac"


BEGINCONST
EXPORTEDNAME g_abEfiFirmware32
        incbin "VBoxEFI32.fd"
end_32_firmware:
EXPORTEDNAME g_cbEfiFirmware32
        dd  end_32_firmware - NAME(g_abEfiFirmware32)

ALIGNDATA(64)
EXPORTEDNAME g_abEfiFirmware64
        incbin "VBoxEFI64.fd"
end_64_firmware:
EXPORTEDNAME g_cbEfiFirmware64
        dd  end_64_firmware - NAME(g_abEfiFirmware64)

%ifdef ASM_FORMAT_ELF
size g_abEfiFirmware32 end_32_firmware - NAME(g_abEfiFirmware32)
type g_abEfiFirmware32 object
size g_cbEfiFirmware32 4
type g_cbEfiFirmware32 object

size g_abEfiFirmware64 end_64_firmware - NAME(g_abEfiFirmware64)
type g_abEfiFirmware64 object
size g_cbEfiFirmware64 4
type g_cbEfiFirmware64 object
%endif
