; $Id: ftol2-vcc.asm 115009 2026-08-12 23:36:02Z knut.osmundsen@oracle.com $
;; @file
; IPRT - Floating Point to Integer related Visual C++ support routines.
;

;
; Copyright (C) 2022-2026 Oracle and/or its affiliates.
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


%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


%define RTFLOAT80U_EXP_BIAS                    (16383)


;;
; Convert st0 to integer returning it in eax, popping st0.
;
; @returns  value in eax
; @param    st0     The value to convert.  Will be popped.
; @uses     eax, st0, FSW.TOP, FSW.exception
;
GLOBALNAME_RAW  __ftol2_sse_excpt, function, default, CALC_PROC_SIZE(__ftoi2)
GLOBALNAME_RAW  __ftol2_sse, function, default, CALC_PROC_SIZE(__ftoi2) ;; @todo kind of expect __ftol2_sse to take input in xmm0 and return in edx:eax.
BEGINPROC_RAW   __ftoi2
        push    ebp
        mov     ebp, esp
        sub     esp, 8h
        fisttp  dword [esp]
        mov     eax, [esp]
        leave
        ret
ENDPROC_RAW     __ftoi2


;;
; Convert st0 to integer returning it in edx:eax, popping st0.
;
; @returns  value in edx:eax
; @param    st0     The value to convert.  Will be popped.
; @uses     eax, edx, st0, FSW.TOP, FSW.exception
;
BEGINPROC_RAW   __ftol2
        push    ebp
        mov     ebp, esp
        sub     esp, 8h
        and     esp, 0fffffff8h             ; proper alignment.
        fisttp  qword [esp]
        mov     eax, [esp]
        mov     edx, [esp + 4]
        leave
        ret
ENDPROC_RAW     __ftol2


;;
; Convert st0 to an unsigned integer returning it in edx:eax, popping st0.
;
; Used when /fpcvt:IA is given together with /ARCH:IA32 or /ARCH:SSE.
;
; @returns  value in edx:eax
; @param    st0     The value to convert.  Will be popped.
; @uses     eax, edx, st0, FSW.TOP
;
BEGINPROC_RAW   __ftoui2
        push    ebp
        mov     ebp, esp

        ; Proper stack alignment for a qword store (see note below).
        sub     esp, 8h
        and     esp, 0fffffff8h             ; proper alignment.

        ; Check if the value is unordered or negative.
        fldz
        fucomip st0, st1
        jp      .unordered                  ; PF=1 only when unordered.
        ja      .negative                   ; jumps if ZF=0 & CF=0, i.e. if zero > value.

        ; Check if the value is too large for uint32_t.
        fld     dword [g_r32TwoToThePowerOf32]
        fucomip st0, st1                    ; if 1*2^32 <= value
        jbe     .too_large                  ; then jmp

        ;
        ; The value is unproblematic, so just convert and return it.
        ;
        ; Note! We do a 64-bit conversion here, not a 32-bit. This helps with
        ;       values at or above 2**31.
        ;
        fisttp  qword [esp]                 ; Raise exceptions as appropriate, pop ST0.
        mov     eax, [esp]
.return:
        leave
        ret
        int3

        ;
        ; Negative value.
        ;
.negative:
        ; If the value is -1.0 or smaller, treat it as unordered.
        fabs
        fld1
        fucomip st0, st1                    ; if 1.0 <= abs(value)
        jbe     .unordered                  ; then jmp  (jbe = jmp if ZF or/and CF set)

        ; Values between -1.0 and 0 (both exclusively) are truncated to zero.
        fisttp  dword [esp]                 ; Raise exceptions as appropriate, pop ST0.
        xor     eax, eax
        jmp     .return
        int3

        ; Return MAX after maybe raising an exception.
.unordered:
.too_large:
        fcomp   dword [g_r32QNaN]           ; Set C0-3, raise exceptions as appropriate, pop ST0.
        mov     eax, -1
        jmp     .return
ENDPROC_RAW     __ftoui2


;;
; Convert st0 to an unsigned long integer returning it in edx:eax, popping st0.
;
; Used when /fpcvt:IA is given together with /ARCH:IA32 or /ARCH:SSE.
;
; @returns  value in edx:eax
; @param    st0     The value to convert.  Will be popped.
; @uses     eax, edx, st0, FSW.TOP
;
BEGINPROC_RAW   __ftoul2
        push    ebp
        mov     ebp, esp

        ; We may need to store a RTFLOAT80 and uint64_t, so make sure the stack is 16 byte aligned.
        sub     esp, 16h
        and     esp, 0fffffff0h

        ; Check if the value is unordered or negative.
        fldz
        fucomip st0, st1
        jp      .unordered                  ; PF=1 only when unordered.
        ja      .negative                   ; jumps if ZF=0 & CF=0, i.e. if zero > value.

        ; Check if the value is in signed 64-bit integer range, i.e. less than 1.0*2^63.
        fld     dword [g_r32TwoToThePowerOf63]
        fucomip st0, st1                                        ; if 1*2^63 <= value
        jbe     .larger_than_or_equal_to_2_to_the_power_of_63   ; then jmp;

        ;
        ; The value is unproblematic for 64-bit signed conversion, so just convert and return it.
        ;
        fisttp  qword [esp]                 ; Raise exceptions as appropriate, pop ST0.
        mov     edx, [esp + 4]
        mov     eax, [esp]
.return:
        leave
        ret
        int3

        ;
        ; We've got a value that so large that fisttp can't handle it, however
        ; it may still be covertable to uint64_t, iff the exponent is 63.
        ;
.larger_than_or_equal_to_2_to_the_power_of_63:

        ; Save the value on the stack so we can examine it in it's full 80-bit format.
        fld     st0
        fstp    tword [esp]                 ; RTFLOAT80U
        movzx   eax, word [esp + 8]         ; The exponent and (zero) sign value.

%ifdef RT_STRICT
        ; Negative numbers shall not end up here, we checked for that in the zero compare above!
        bt      eax, 15
        jnc     .sign_clear_as_expected
        int3
.sign_clear_as_expected:
        ; The exponent shall not be less than 63, we checked for that in the g_r32TwoToThePowerOf63 compare above!
        cmp     eax, RTFLOAT80U_EXP_BIAS+63
        jae     .exp_not_below_63_as_expected
        int3
.exp_not_below_63_as_expected:
%endif

        ; Check that the exponent is 63, because if it isn't it must be higher
        ; and out of range for a conversion to uint64_t.
        cmp     eax, RTFLOAT80U_EXP_BIAS+63
        jne     .too_large

        ; Check for unnormal values just to be on the safe side.
        mov     edx, [esp + 4]
        bt      edx, 31                     ; Check if the most significant bit in the mantissa is zero.
        jnc     .unnormal

        ; Load the rest of the value.
        mov     eax, [esp]
        frndint                             ; Clear C1 & raising exceptions as appropriate.
        ffreep  st0
        jmp     .return
        int3

        ;
        ; Negative value.
        ;
.negative:
        ; If the value is -1.0 or smaller, treat it as unordered.
        fabs
        fld1
        fucomip st0, st1                    ; if 1.0 <= abs(value)
        jbe     .unordered                  ; then jmp  (jbe = jmp if ZF or/and CF set)

        ; Values between -1.0 and 0 (both exclusively) are truncated to zero.
        fisttp  qword [esp]                 ; Raise exceptions as appropriate, pop ST0.
        xor     edx, edx
        xor     eax, eax
        jmp     .return
        int3

        ;
        ; Unordered or a value in the (-1.0, 0) range.
        ; Return -1 after popping ST0.
        ;
.unordered:
.unnormal:
.too_large:
        fcomp   dword [g_r32QNaN]           ; Set C0-3, raise exceptions as appropriate, pop ST0.
        mov     edx, -1
        mov     eax, -1
        jmp     .return
ENDPROC_RAW     __ftoul2


;;
; Convert st0 to unsigned integer returning it in edx:eax, popping st0.
;
; This is new with VC 14.3 / 2022 and changes how extreme values are handled.
;
; This is used for /ARCH:IA32 & /ARCH:SSE, the compiler generates inline conversion
; code for /ARCH:SSE2 it seems.  If either of those two are used with /fpcvt:IA, the
; __ftoul2 is used instead.
;
; @returns  value in edx:eax
; @param    st0     The value to convert.  Will be popped.
; @uses     eax, edx, st0, FSW.TOP
;
BEGINPROC_RAW   __ftoul2_legacy
        sub     esp, 4
        fst     dword [esp]
        pop     edx
        cmp     edx, 0x5f800000             ; RTFLOAT32U
        jae     __ftol2                     ; Jump if exponent >= 64 or the value is signed.
        jmp     __ftoul2
ENDPROC_RAW     __ftoul2_legacy


;
; Constants.
;
ALIGNCODE(4)
g_r32TwoToThePowerOf32:
        dd      0x4f800000                  ; 1.0*2^32
g_r32TwoToThePowerOf63:
        dd      0x5f000000                  ; 1.0*2^63
g_r32QNaN:
        dd      0xffc00000                  ; Quite negative NaN (RTFLOAT32U_INIT_QNAN)

MARK_OBJECT_RETPOLINE_SAFE
