; $Id: timerA-r0drv-os2.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - DevHelp_VMGlobalToProcess, Ring-0 Driver, OS/2.
;

;
; Contributed by knut st. osmundsen.
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
; --------------------------------------------------------------------
;
; This code is based on:
;
; Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
;
; Permission is hereby granted, free of charge, to any person
; obtaining a copy of this software and associated documentation
; files (the "Software"), to deal in the Software without
; restriction, including without limitation the rights to use,
; copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the
; Software is furnished to do so, subject to the following
; conditions:
;
; The above copyright notice and this permission notice shall be
; included in all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
; OTHER DEALINGS IN THE SOFTWARE.
;


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_INCL_16BIT_SEGMENTS
%include "iprt/asmdefs.mac"
%include "iprt/err.mac"


;*******************************************************************************
;* External Symbols                                                            *
;*******************************************************************************
extern KernThunkStackTo32
extern KernThunkStackTo16
extern NAME(rtTimerOs2Tick)
extern NAME(RTErrConvertFromOS2)
BEGINDATA16
extern NAME(g_fpfnDevHlp)


;*******************************************************************************
;* Defined Constants And Macros                                                *
;*******************************************************************************
%define DevHlp_SetTimer     01dh
%define DevHlp_ResetTimer   01eh


BEGINCODE

;;
; Arms the our OS/2 timer.
;
; @returns IPRT status code.
;
BEGINPROC_EXPORTED rtTimerOs2Arm
    call    KernThunkStackTo16
    push    ebp
    mov     ebp, esp

    ; jump to the 16-bit code.
    ;jmp far dword NAME(rtTimerOs2Arm_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(rtTimerOs2Arm_16) wrt CODE16
    dw      CODE16
BEGINCODE16
GLOBALNAME rtTimerOs2Arm_16

    ; setup and do the call
    push    ds
    push    es
    mov     dx, DATA16
    mov     ds, dx
    mov     es, dx

    mov     ax, NAME(rtTimerOs2TickAsm) wrt CODE16
    mov     dl, DevHlp_SetTimer
    call far [NAME(g_fpfnDevHlp)]

    pop     es
    pop     ds

    ;jmp far dword NAME(rtTimerOs2Arm_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(rtTimerOs2Arm_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
BEGINCODE
GLOBALNAME rtTimerOs2Arm_32
    jc      .error
    xor     eax, eax
.done:

    leave
    push    eax
    call    KernThunkStackTo32
    pop     eax
    ret

    ; convert the error code.
.error:
    and     eax, 0ffffh
    call    NAME(RTErrConvertFromOS2)
    jmp     .done
ENDPROC rtTimerOs2Arm


;;
; Dearms the our OS/2 timer.
;
; @returns IPRT status code.
;
BEGINPROC_EXPORTED rtTimerOs2Dearm
    call    KernThunkStackTo16
    push    ebp
    mov     ebp, esp

    ; jump to the 16-bit code.
    ;jmp far dword NAME(rtTimerOs2Dearm_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(rtTimerOs2Dearm_16) wrt CODE16
    dw      CODE16
BEGINCODE16
GLOBALNAME rtTimerOs2Dearm_16

    ; setup and do the call
    push    ds
    push    es
    mov     dx, DATA16
    mov     ds, dx
    mov     es, dx

    mov     ax, NAME(rtTimerOs2TickAsm) wrt CODE16
    mov     dl, DevHlp_ResetTimer
    call far [NAME(g_fpfnDevHlp)]

    pop     es
    pop     ds

    ;jmp far dword NAME(rtTimerOs2Dearm_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(rtTimerOs2Dearm_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
BEGINCODE
GLOBALNAME rtTimerOs2Dearm_32
    jc      .error
    xor     eax, eax
.done:

    ; epilogue
    leave
    push    eax
    call    KernThunkStackTo32
    pop     eax
    ret

    ; convert the error code.
.error:
    and     eax, 0ffffh
    call    NAME(RTErrConvertFromOS2)
    jmp     .done
ENDPROC rtTimerOs2Dearm


BEGINCODE16

;;
; OS/2 timer tick callback.
;
BEGINPROC rtTimerOs2TickAsm
    push    ds
    push    es
    push    ecx
    push    edx
    push    eax

    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax

    ;jmp far dword NAME(rtTimerOs2TickAsm_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(rtTimerOs2TickAsm_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
BEGINCODE
GLOBALNAME rtTimerOs2TickAsm_32
    call    KernThunkStackTo32
    call    NAME(rtTimerOs2Tick)
    call    KernThunkStackTo16

    ;jmp far dword NAME(rtTimerOs2TickAsm_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(rtTimerOs2TickAsm_16) wrt CODE16
    dw      CODE16
BEGINCODE16
GLOBALNAME rtTimerOs2TickAsm_16

    pop     eax
    pop     edx
    pop     ecx
    pop     es
    pop     ds
    retf
ENDPROC rtTimerOs2TickAsm
