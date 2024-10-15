; $Id: bs3-cpu-basic-2-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-cpu-basic-2
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
%include "bs3kit.mac"


;*********************************************************************************************************************************
;*      Global Variables                                                                                                         *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
BS3_GLOBAL_DATA g_bs3CpuBasic2_ud2_FlatAddr, 4
        dd  _bs3CpuBasic2_ud2 wrt FLAT


;
; CPU mode agnostic test code snippets.
;
BS3_BEGIN_TEXT16

BS3_PROC_BEGIN _bs3CpuBasic2_ud2
.again:
        ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_ud2


BS3_PROC_BEGIN _bs3CpuBasic2_salc_ud2
        salc                            ; #UD in 64-bit mode
.again:
        ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_salc_ud2

BS3_PROC_BEGIN _bs3CpuBasic2_swapgs
.again:
        db      00fh, 001h, 0f8h        ; swapgs - #UD when not in 64-bit mode.
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_swapgs


BS3_PROC_BEGIN _bs3CpuBasic2_Int80
        int     80h
.again: ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_Int80


BS3_PROC_BEGIN _bs3CpuBasic2_Int81
        int     81h
.again: ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_Int81


BS3_PROC_BEGIN _bs3CpuBasic2_Int82
        int     82h
.again: ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_Int82


BS3_PROC_BEGIN _bs3CpuBasic2_Int83
        int     83h
.again: ud2
        jmp     .again
BS3_PROC_END   _bs3CpuBasic2_Int83


BS3_PROC_BEGIN _bs3CpuBasic2_iret
        iret
BS3_PROC_END   _bs3CpuBasic2_iret
AssertCompile(_bs3CpuBasic2_iret_EndProc - _bs3CpuBasic2_iret == 1)


BS3_PROC_BEGIN _bs3CpuBasic2_iret_opsize
        iretd
BS3_PROC_END   _bs3CpuBasic2_iret_opsize
AssertCompile(_bs3CpuBasic2_iret_opsize_EndProc - _bs3CpuBasic2_iret_opsize == 2)


BS3_PROC_BEGIN _bs3CpuBasic2_iret_rexw
        BS3_SET_BITS 64
        iretq
        BS3_SET_BITS 16
BS3_PROC_END   _bs3CpuBasic2_iret_rexw
AssertCompile(_bs3CpuBasic2_iret_rexw_EndProc - _bs3CpuBasic2_iret_rexw == 2)


;
; CPU mode agnostic test code snippets.
;
BS3_BEGIN_TEXT32

;;
; @param    [xBP + xCB*2]   puDst
; @param    [xBP + xCB*3]   uNewValue
BS3_PROC_BEGIN_CMN bs3CpuBasic2_Store_mov, BS3_PBC_NEAR
        push    xBP
        mov     xBP, xSP
        mov     xCX, [xBP + xCB*2]
        mov     xAX, [xBP + xCB*3]
        mov     [xCX], xAX
        leave
        ret
BS3_PROC_END_CMN   bs3CpuBasic2_Store_mov

;;
; @param    [xBP + xCB*2]   puDst
; @param    [xBP + xCB*3]   uNewValue
BS3_PROC_BEGIN_CMN bs3CpuBasic2_Store_xchg, BS3_PBC_NEAR
        push    xBP
        mov     xBP, xSP
        mov     xCX, [xBP + xCB*2]
        mov     xAX, [xBP + xCB*3]
        xchg    [xCX], xAX
        leave
        ret
BS3_PROC_END_CMN   bs3CpuBasic2_Store_xchg

;;
; @param    [xBP + xCB*2]   puDst
; @param    [xBP + xCB*3]   uNewValue
; @param    [xBP + xCB*4]   uOldValue
BS3_PROC_BEGIN_CMN bs3CpuBasic2_Store_cmpxchg, BS3_PBC_NEAR
        push    xBP
        mov     xBP, xSP
        mov     xCX, [xBP + xCB*2]
        mov     xDX, [xBP + xCB*3]
        mov     xAX, [xBP + xCB*4]
.again:
        cmpxchg [xCX], xDX
        jnz     .again
        leave
        ret
BS3_PROC_END_CMN   bs3CpuBasic2_Store_cmpxchg


;
; Jump code segment 64KB.
;
; There is no ORG directive in OMF mode of course. :-(
;
section BS3JMPTEXT16 align=16 CLASS=BS3CLASS16JMPCODE PRIVATE USE16
        GROUP BS3GROUPJMPTEXT16 BS3JMPTEXT16
        BS3_SET_BITS 16

; 0000: Start with two int3 filler instructions.
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmptext16_start), function, 2
        int3
        int3

; 0002: This is the target for forward wrap around jumps, should they succeed.
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_target_wrap_forward), function, 2
        ud2
        align 8, int3

; 0008
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jb_wrap_backward__ud2), function, 2
        db      0ebh, -012h             ; jmp (0x0008 + 2 - 0x12 = 0xFFFFFFF8 (-8))
        int3

; 000b
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jb_opsize_wrap_backward__ud2), function, 3
        db      066h, 0ebh, -016h       ; jmp (0x000b + 3 - 0x16 = 0xFFFFFFF8 (-8))
        int3

        align   0x80, int3
; 0080
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jv16_wrap_backward__ud2), function, 3
        db      0e9h                    ; jmp (0x0080 + 3 - 0x8b = 0xFFFFFFF8 (-8))
        dw      -08bh
        int3

; 0084
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jv16_opsize_wrap_backward__ud2), function, 6
        db      066h, 0e9h              ; jmp (0x0084 + 6 - 0x92 = 0xFFFFFFF8 (-8))
        dd      -092h
        int3

; 008b
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_call_jv16_wrap_backward__ud2), function, 3
        db      0e8h                    ; call (0x008b + 3 - 0x96)
        dw      -096h
        int3

; 008f
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_call_jv16_opsize_wrap_backward__ud2), function, 6
        db      066h, 0e8h              ; call (0x008f + 6 - 0x9d = 0xFFFFFFF8 (-8))
        dd      -09dh
        int3


        align   0x100, int3             ; Note! Doesn't work correctly for higher values.
        times   (0xff6b - 0x100) int3

; ff6b
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_call_jv16_wrap_forward__ud2), function, 4
        db      0e8h                    ; call (0xff6b+3 + 0x94 = 0x10002 (65538))
        dw      094h
        int3

; ff6f
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_call_jv16_opsize_wrap_forward__ud2), function, 7
        db      066h, 0e8h              ; o32 call (0xff6f+6 + 0x8d = 0x10002 (65538))
        dd      08dh
        int3

; ff76
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jv16_wrap_forward__ud2), function, 5
        db      0e9h                    ; jmp (0xff76+4 + 0x88 = 0x10002 (65538))
        dw      089h
        int3

; ff7a
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jv16_opsize_wrap_forward__ud2), function, 7
        db      066h, 0e9h              ; o32 jmp (0xff7a+6 + 0x82 = 0x10002 (65538))
        dd      082h
        int3

; ff81
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jb_wrap_forward__ud2), function, 2
        db      0ebh, 07fh              ; jmp (0xff81+2 + 0x7f = 0x10002 (65538))
        int3

; ff84
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_jb_opsize_wrap_forward__ud2), function, 3
        db      066h, 0ebh, 07bh        ; o32 jmp (0xff84+3 + 0x7b = 0x10002 (65538))
; ff87

        times   (0xfff8 - 0xff87) int3

; fff8: This is the target for backward wrap around jumps, should they succeed.
BS3_GLOBAL_NAME_EX NAME(bs3CpuBasic2_jmp_target_wrap_backward), function, 2
        ud2
        times   6 int3
; End of segment.

BS3_BEGIN_TEXT16

;
; Instantiate code templates.
;
BS3_INSTANTIATE_COMMON_TEMPLATE          "bs3-cpu-basic-2-template.mac"
BS3_INSTANTIATE_TEMPLATE_WITH_WEIRD_ONES "bs3-cpu-basic-2-template.mac"

