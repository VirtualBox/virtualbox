; $Id: RTR0Os2DHVMGlobalToProcess.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
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
extern NAME(g_fpfnDevHlp)


;*******************************************************************************
;* Defined Constants And Macros                                                *
;*******************************************************************************
%define DevHlp_VMGlobalToProcess  05ah


BEGINCODE

;;
; VMGlobalToProcess wrapper.
;
; @param    fFlags  [ebp + 08h]     Flags
; @param    pvR0    [ebp + 0ch]     Ring-0 memory.
; @param    cb      [ebp + 10h]     Size of memory object to map.
; @param    ppR3    [ebp + 14h]     Where to store the address of the ring-3 mapping.
;
BEGINPROC_EXPORTED RTR0Os2DHVMGlobalToProcess
    ; switch stack first.
    call    KernThunkStackTo16

    ; normal prolog.
    push    ebp
    mov     ebp, esp
    push    dword [NAME(g_fpfnDevHlp)]  ; ebp - 4
    push    ebx                         ; save ebx

    ; setup the devhelp call
    mov     eax, [ebp + 08h]            ; fFlags
    mov     ebx, [ebp + 0ch]            ; pvR0
    mov     ecx, [ebp + 10h]            ; cb
    mov     dl, DevHlp_VMGlobalToProcess

    ; jump to the 16-bit code.
    ;jmp far dword NAME(RTR0Os2DHQueryDOSVar_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(RTR0Os2DHVMGlobalToProcess_16) wrt CODE16
    dw      CODE16
BEGINCODE16
GLOBALNAME RTR0Os2DHVMGlobalToProcess_16
    call far [ss:ebp - 4]

    ;jmp far dword NAME(RTR0Os2DHVMGlobalToProcess_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(RTR0Os2DHVMGlobalToProcess_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
BEGINCODE
GLOBALNAME RTR0Os2DHVMGlobalToProcess_32
    jc      .done

    ; save the result.
    mov     edx, [ebp + 14h]            ; ppvR3
    mov     [edx], eax
    xor     eax, eax

.done:
    pop     ebx
    leave

    ; switch stack back and return.
    push    eax
    call    KernThunkStackTo32
    pop     eax
    ret
ENDPROC RTR0Os2DHVMGlobalToProcess

