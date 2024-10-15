; $Id: stack-probe-vcc.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Stack related Visual C++ support routines.
;

;
; Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
;*      Header Files                                                                                                             *
;*********************************************************************************************************************************
%if 0 ; YASM's builtin SEH64 support doesn't cope well with code alignment, so use our own.
 %define RT_ASM_WITH_SEH64
%else
 %define RT_ASM_WITH_SEH64_ALT
%endif
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


;;
; Probe stack to trigger guard faults, and for x86 to allocate stack space.
;
; @param    xAX     Frame size.
; @uses     AMD64:  Probably nothing. EAX is certainly not supposed to change.
;           x86:    ESP = ESP - EAX; EFLAGS, nothing else
;
ALIGNCODE(64)
GLOBALNAME_RAW  __alloca_probe, __alloca_probe, function
BEGINPROC_RAW   __chkstk
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        push    xAX
        SEH64_PUSH_GREG xAX
        push    xBX
        SEH64_PUSH_GREG xBX
        SEH64_END_PROLOGUE

        ;
        ; Adjust eax so we're relative to [xBP - xCB*2].
        ;
        sub     xAX, xCB * 4
        jle     .touch_loop_done            ; jump if rax < xCB*4, very unlikely

        ;
        ; Subtract what's left of the current page from eax and only engage
        ; the touch loop if (int)xAX > 0.
        ;
        lea     ebx, [ebp - xCB * 2]
        and     ebx, PAGE_SIZE - 1
        sub     xAX, xBX
        jnl     .touch_loop                 ; jump if pages to touch.

.touch_loop_done:
        pop     xBX
        pop     xAX
        leave
%ifndef RT_ARCH_X86
        ret
%else
        ;
        ; Do the stack space allocation and jump to the return location.
        ;
        sub     esp, eax
        add     esp, 4
        jmp     dword [esp + eax - 4]
%endif

        ;
        ; The touch loop.
        ;
.touch_loop:
        sub     xBX, PAGE_SIZE
%if 1
        mov     [xBP + xBX - xCB * 2], bl
%else
        or      byte [xBP + xBX - xCB * 2], 0   ; non-destructive variant...
%endif
        sub     xAX, PAGE_SIZE
        jnl     .touch_loop
        jmp     .touch_loop_done
ENDPROC_RAW     __chkstk


%ifdef RT_ARCH_X86
;;
; 8 and 16 byte aligned alloca w/ probing.
;
; This routine adjusts the allocation size so __chkstk will return a
; correctly aligned allocation.
;
; @param    xAX     Unaligned allocation size.
;
%macro __alloc_probe_xxx 1
ALIGNCODE(16)
BEGINPROC_RAW   __alloca_probe_ %+ %1
        push    ecx

        ;
        ; Calc the ESP address after the allocation and adjust EAX so that it
        ; will be aligned as desired.
        ;
        lea     ecx, [esp + 8]
        sub     ecx, eax
        and     ecx, %1 - 1
        add     eax, ecx
        jc      .bad_alloc_size
.continue:

        pop     ecx
        jmp     __alloca_probe

.bad_alloc_size:
  %ifdef RT_STRICT
        int3
  %endif
        or      eax, 0xfffffff0
        jmp     .continue
ENDPROC_RAW     __alloca_probe_ %+ %1
%endmacro

__alloc_probe_xxx 16
__alloc_probe_xxx 8
%endif ; RT_ARCH_X86

