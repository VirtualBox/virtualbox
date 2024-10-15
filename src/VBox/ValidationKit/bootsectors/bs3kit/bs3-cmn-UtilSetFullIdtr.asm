; $Id: bs3-cmn-UtilSetFullIdtr.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3UtilSetFullIdtr
;

;
; Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
%include "bs3kit-template-header.mac"


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
%if TMPL_BITS == 16
BS3_EXTERN_DATA16 g_uBs3CpuDetected
%endif
%if TMPL_BITS != 64
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%endif
TMPL_BEGIN_TEXT



;;
; @cproto   BS3_CMN_PROTO_NOSB(void, Bs3UtilSetFullIdtr,(uint16_t cbLimit, uint64_t uBase));
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
; @uses     eax/rax; cbLimit on stack in 32-bit mode.
;
BS3_PROC_BEGIN_CMN Bs3UtilSetFullIdtr, BS3_PBC_HYBRID
TONLY16 inc     xBP
        push    xBP
        mov     xBP, xSP

%if TMPL_BITS == 64
        ;
        ; It doesn't (currently) get any better than 64-bit mode.
        ;
        push    rdx
        mov     rax, rcx
        shl     rax, 48
        push    rax
        lidt    [rsp + 6]
        add     rsp, 10h


%elif TMPL_BITS == 32
        ;
        ; Move the limit up two bytes so we can use it directly.
        ;
        shl     dword [xBP + xCB + cbCurRetAddr], 16

        ;
        ; If the system is currently running in long mode, we have to switch to
        ; it in order to do the job with the highest precision.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        and     al, BS3_MODE_SYS_MASK
        cmp     al, BS3_MODE_SYS_LM
        je      .do_64bit

        ; 32-bit is the best we can do.
.do_32bit:
        lidt    [xBP + xCB + cbCurRetAddr + 2]
        jmp     .return

        ; Must switch to long mode and do it there.
.do_64bit:
        jmp     BS3_SEL_R0_CS64:.in_64bit wrt FLAT
.in_64bit:
        BS3_SET_BITS 64
        lidt    [xSP + 4 + cbCurRetAddr + 2]
        push    BS3_SEL_R0_CS32
        push    .return wrt FLAT
        o64 retf
        BS3_SET_BITS 32


%elif TMPL_BITS == 16
        ;
        ; All options are open here, we can be in any 16-bit mode,
        ; including real mode.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        test    al, BS3_MODE_CODE_V86
        jnz     .do_v8086
        and     al, BS3_MODE_SYS_MASK
        cmp     al, BS3_MODE_SYS_LM
        je      .do_64bit
        cmp     al, BS3_MODE_SYS_RM
        je      .do_16bit
        cmp     byte [ BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80386
        jae     .do_32bit

        ;
        ; We're in real mode or in 16-bit protected mode on a 286.
        ;
.do_16bit: ;ba x  1 127f5
        lidt    [xBP + xCB + cbCurRetAddr]
        jmp     .return

        ;
        ; We're in some kind of protected mode on a 386 or better.
        ;
.do_32bit:
        jmp     dword BS3_SEL_R0_CS32:.in_32bit wrt FLAT
.in_32bit:
        BS3_SET_BITS 32
        lidt    [bp + 2 + cbCurRetAddr]
        jmp     BS3_SEL_R0_CS16:.return wrt CGROUP16
        BS3_SET_BITS 16

        ;
        ; V8086 mode - need to switch to 32-bit kernel code to do stuff here.
        ;
.do_v8086:
        BS3_EXTERN_CMN Bs3SwitchTo32Bit
        call    Bs3SwitchTo32Bit
        BS3_SET_BITS 32

        lidt    [xSP + 2 + cbCurRetAddr]

        extern  _Bs3SwitchTo16BitV86_c32
        call    _Bs3SwitchTo16BitV86_c32
        BS3_SET_BITS 16
        jmp     .return

        ;
        ; System is in long mode, so we can switch to 64-bit mode and do the job there.
        ;
.do_64bit:
        push    edx                     ; save
        push    ss
        push    0
        push    bp
        BS3_EXTERN_CMN Bs3SelFar32ToFlat32NoClobber
        call    Bs3SelFar32ToFlat32NoClobber
        add     sp, 6
        shl     edx, 16
        mov     dx, ax
        mov     eax, edx                ; eax = flattened ss:bp
        pop     edx                     ; restore
        jmp     dword BS3_SEL_R0_CS64:.in_64bit wrt FLAT
.in_64bit:
        BS3_SET_BITS 64
        lidt    [rax + 2 + cbCurRetAddr]

        push    BS3_SEL_R0_CS16
        push    .return wrt CGROUP16
        o64 retf
        BS3_SET_BITS 16

%else
 %error "TMPL_BITS!"
%endif

.return:
        pop     xBP
TONLY16 dec     xBP
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3UtilSetFullIdtr

