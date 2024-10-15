; $Id: SUPR3HardenedMainA-posix.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VirtualBox Support Library - Hardened main(), Posix assembly bits.
;

;
; Copyright (C) 2017-2024 Oracle and/or its affiliates.
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


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
; External code.
BEGINCODE
extern NAME(supR3HardenedPosixMonitor_VerifyLibrary)

; External data
BEGINDATA
extern NAME(g_pfnDlopenReal)
%ifdef SUP_HARDENED_WITH_DLMOPEN
extern NAME(g_pfnDlmopenReal)
%endif



BEGINCODE

;;
; Wrapper for dlopen() handing the call over to the file verification code
; and resuming the call if we get a green light to load the library.
;
align 16
BEGINPROC supR3HardenedPosixMonitor_Dlopen
        push    xBP
        mov     xBP, xSP

%ifdef RT_ARCH_AMD64
        ; Save parameters on the stack
        push    rdi
        push    rsi
%else
        sub     esp, 4                  ; 16-byte stack alignment before call.
        push    dword [xBP + 08h]       ; first parameter.
%endif

        ;
        ; Call the verification method.
        ;
        call    NAME(supR3HardenedPosixMonitor_VerifyLibrary)

        ;
        ; Restore parameters for the next call and get the stack back to the
        ; original state.
        ;
%ifdef RT_ARCH_AMD64
        pop     rsi
        pop     rdi
%endif
        leave

        ; Check the result and resume the call if the result is positive,
        ; otherwise clean up and return NULL
        test    al, al
        je short .failed

        ; Resume the original dlopen call by jumping into the saved code.
        jmp     [NAME(g_pfnDlopenReal) xWrtRIP]

.failed:
        ;
        ; Don't use leave here as we didn't use the enter instruction. Just clear
        ; xAX and return
        ;
        xor     xAX, xAX
        ret
ENDPROC   supR3HardenedPosixMonitor_Dlopen


%ifdef SUP_HARDENED_WITH_DLMOPEN
;;
; Wrapper for dlmopen() handing the call over to the file verification code
; and resuming the call if we get a green light to load the library.
;
align 16
BEGINPROC supR3HardenedPosixMonitor_Dlmopen
        push    xBP
        mov     xBP, xSP

%ifdef RT_ARCH_AMD64
        sub     rsp, 8                  ; 16-byte stack alignment before call.

        ; Save parameters on the stack
        push    rdi
        push    rsi
        push    rdx

        mov     rdi, rsi                ; Move the second parameter to the front
%else
        sub     esp, 4                  ; 16-byte stack alignment before call.
        push    dword [xBP + 0ch]       ; Move the second parameter to the front
%endif

        ;
        ; Call the verification method.
        ;
        call    NAME(supR3HardenedPosixMonitor_VerifyLibrary)

        ;
        ; Restore parameters for the next call and get the stack back to the
        ; original state.
        ;
%ifdef RT_ARCH_AMD64
        pop     rdx
        pop     rsi
        pop     rdi
%endif
        leave

        ; Check the result and resume the call if the result is positive,
        ; otherwise clean up and return NULL
        test    al, al
        je short .failed

        ; Resume the original dlopen call by jumping into the saved code.
        jmp     [NAME(g_pfnDlmopenReal) xWrtRIP]

.failed:
        ;
        ; Don't use leave here as we didn't use the enter instruction. Just clear
        ; xAX and return
        ;
        xor     xAX, xAX
        ret
ENDPROC   supR3HardenedPosixMonitor_Dlmopen
%endif

