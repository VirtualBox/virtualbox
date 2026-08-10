; $Id: bs3-apic-1-asm.asm 114752 2026-07-22 16:52:58Z alexander.eichner@oracle.com $
;; @file
; BS3Kit - bs3-apic-1
;

;
; Copyright (C) 2026 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit.mac"


BS3_EXTERN_SYSTEM16 Bs3LgdtDef_Gdt
BS3_EXTERN_SYSTEM16 Bs3Lgdt_Gdt

BS3_EXTERN_DATA16 g_fApLock
BS3_EXTERN_DATA16 g_AddrApInitStack

BS3_BEGIN_RMTEXT16

BS3_PROC_BEGIN NAME(bs3ApicApTrampoline)
        ; Disable interrupts
        cli
        cld

        ;
        ; Load the GDT and enable PE32.
        ;
        mov     ax, BS3SYSTEM16
        mov     ds, ax
        lgdt    [Bs3LgdtDef_Gdt]        ; Will only load 24-bit base!

        mov     eax, cr0
        or      eax, X86_CR0_PE
        mov     cr0, eax
        jmp     BS3_SEL_R0_CS32:dword .ap_thirty_two_bit wrt FLAT
BS3_BEGIN_TEXT32
BS3_GLOBAL_LOCAL_LABEL .ap_thirty_two_bit

        mov     ax, BS3_SEL_R0_DS32
        mov     ds, ax

BS3_GLOBAL_LOCAL_LABEL .ap_lck_busy
        mov     al, 0
        mov     bl, 1
        lock cmpxchg [BS3_DATA16_WRT(g_fApLock)], bl
        jne .ap_lck_busy

        ;
        ; Setup the initial stack for the AP
        ;
        mov     esp, dword [BS3_DATA16_WRT(g_AddrApInitStack)]

        mov     ax, BS3_SEL_R0_SS32
        mov     ss, ax

        ;
        ; Call rountine for doing mode specific setups.
        ;
        extern  NAME(Bs3EnteredMode_pe32)
        call    NAME(Bs3EnteredMode_pe32)

        ; Load full 32-bit GDT base address.
        lgdt    [Bs3Lgdt_Gdt wrt FLAT]

        ;
        ; Call AP startup.
        ;
        extern  NAME(bs3ApicApStartup_pe32)
        call    NAME(bs3ApicApStartup_pe32)

        ;
        ; Set the final stack top returned from the previous call.
        ;
        mov     esp, eax

        ;
        ; Unlock the AP lock so other APs can continue.
        ;
        mov     al, 0
        xchg    [BS3_DATA16_WRT(g_fApLock)], al

        ;
        ; Call the AP runloop.
        ;
        extern  NAME(bs3ApicApRunloop_pe32)
        call    NAME(bs3ApicApRunloop_pe32)

        ; Never reached
        int 3
BS3_PROC_END   NAME(bs3ApicApTrampoline)

