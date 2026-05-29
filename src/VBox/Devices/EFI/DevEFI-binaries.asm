; $Id: DevEFI-binaries.asm 114226 2026-05-29 22:21:51Z knut.osmundsen@oracle.com $
;; @file
; DevEFI - firmware binaries.
;

;
; Copyright (C) 2011-2025 Oracle and/or its affiliates.
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
EXPORTEDNAME_EX g_abEfiFirmwareX86, object, end_x86_firmware - NAME(g_abEfiFirmwareX86)
        incbin "VBoxEFI-x86.fd"
end_x86_firmware:
EXPORTEDNAME_EX g_cbEfiFirmwareX86, object, 4
        dd  end_x86_firmware - NAME(g_abEfiFirmwareX86)

ALIGNDATA(64)
EXPORTEDNAME_EX g_abEfiFirmwareAmd64, object, end_amd64_firmware - NAME(g_abEfiFirmwareAmd64)
        incbin "VBoxEFI-amd64.fd"
end_amd64_firmware:
EXPORTEDNAME_EX g_cbEfiFirmwareAmd64, object, 4
        dd  end_amd64_firmware - NAME(g_abEfiFirmwareAmd64)

%ifdef VBOX_WITH_VIRT_ARMV8
;
; The ARMv8 bits
;

; 32-bit firmware:
ALIGNDATA(64)

EXPORTEDNAME_EX g_abEfiFirmwareArm32, object, end_arm32_firmware - NAME(g_abEfiFirmwareArm32)
        incbin "VBoxEFI-arm32.fd"
end_arm32_firmware:
EXPORTEDNAME_EX g_cbEfiFirmwareArm32, object, 4
        dd  end_arm32_firmware - NAME(g_abEfiFirmwareArm32)

ALIGNDATA(64)
EXPORTEDNAME_EX g_abEfiFirmwareArm64, object, end_arm64_firmware - NAME(g_abEfiFirmwareArm64)
        incbin "VBoxEFI-arm64.fd"
end_arm64_firmware:
EXPORTEDNAME_EX g_cbEfiFirmwareArm64, object, 4
        dd  end_arm64_firmware - NAME(g_abEfiFirmwareArm64)

%endif ; VBOX_WITH_VIRT_ARMV8
