; $Id: SUPLibTracerA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VirtualBox Support Library - Tracer Interface, Assembly bits.
;

;
; Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
%include "VBox/sup.mac"

; This should go into asmdefs.mac
%ifdef PIC
 %ifdef ASM_FORMAT_ELF
  %define RT_ASM_USE_GOT
  %define RT_ASM_USE_PLT
 %endif
%endif


;*******************************************************************************
;*  Structures and Typedefs                                                    *
;*******************************************************************************
struc SUPREQHDR
        .u32Cookie              resd 1
        .u32SessionCookie       resd 1
        .cbIn                   resd 1
        .cbOut                  resd 1
        .fFlags                 resd 1
        .rc                     resd 1
endstruc

struc SUPTRACERUMODFIREPROBE
        .Hdr                    resb SUPREQHDR_size
        .In                     resb SUPDRVTRACERUSRCTX64_size
endstruc


extern NAME(suplibTracerFireProbe)


BEGINCODE


;;
; Set up a SUPTRACERUMODFIREPROBE request package on the stack and a C helper
; function in SUPLib.cpp to do the rest.
;
EXPORTEDNAME SUPTracerFireProbe
        push    xBP
        mov     xBP, xSP

        ;
        ; Allocate package and set the sizes (the helper does the rest of
        ; the header).  Setting the sizes here allows the helper to verify our
        ; idea of the request sizes.
        ;
        lea     xSP, [xBP - SUPTRACERUMODFIREPROBE_size - 8]

        mov     dword [xSP + SUPTRACERUMODFIREPROBE.Hdr + SUPREQHDR.cbIn],  SUPTRACERUMODFIREPROBE_size
        mov     dword [xSP + SUPTRACERUMODFIREPROBE.Hdr + SUPREQHDR.cbOut], SUPREQHDR_size

%ifdef RT_ARCH_AMD64
        ;
        ; Save the AMD64 context.
        ;
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rax], rax
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rcx], rcx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rdx], rdx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rbx], rbx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rsi], rsi
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rdi], rdi
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r8 ], r8
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r9 ], r9
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r10], r10
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r11], r11
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r12], r12
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r13], r13
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r14], r14
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.r15], r15
        pushf
        pop     xAX
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rflags], xAX
        mov     xAX, [xBP + xCB]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rip], xAX
        mov     xAX, [xBP]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rbp], xAX
        lea     xAX, [xBP + xCB*2]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.rsp], xAX
 %ifdef ASM_CALL64_MSC
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.uVtgProbeLoc], rcx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*0], rdx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*1], r8
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*2], r9
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*0]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*3], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*1]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*4], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*2]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*5], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*3]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*6], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*4]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*7], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*5]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*8], xAX
        mov     xAX, [xBP + xCB*2 + 0x20 + xCB*6]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*9], xAX
        mov     eax, [xCX + 4]          ; VTGPROBELOC::idProbe.
 %else
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.uVtgProbeLoc], rdi
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*0], rsi
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*1], rdx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*2], rcx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*3], r8
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*4], r9
        mov     xAX, [xBP + xCB*2 + xCB*0]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*5], xAX
        mov     xAX, [xBP + xCB*2 + xCB*1]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*6], xAX
        mov     xAX, [xBP + xCB*2 + xCB*2]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*7], xAX
        mov     xAX, [xBP + xCB*2 + xCB*3]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*8], xAX
        mov     xAX, [xBP + xCB*2 + xCB*4]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.u.Amd64.aArgs + xCB*9], xAX
        mov     eax, [xDI + 4]          ; VTGPROBELOC::idProbe.
 %endif
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.idProbe], eax
        mov     dword [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX64.cBits], 64

        ;
        ; Call the helper.
        ;
 %ifdef ASM_CALL64_MSC
        mov     xDX, xSP
        sub     xSP, 0x20
        call    NAME(suplibTracerFireProbe)
 %else
        mov     xSI, xSP
  %ifdef RT_ASM_USE_PLT
        call    NAME(suplibTracerFireProbe) wrt ..plt
  %else
        call    NAME(suplibTracerFireProbe)
  %endif
 %endif

%elifdef RT_ARCH_X86
        ;
        ; Save the X86 context.
        ;
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.eax], eax
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.ecx], ecx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.edx], edx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.ebx], ebx
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.esi], esi
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.edi], edi
        pushf
        pop     xAX
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.eflags], xAX
        mov     xAX, [xBP + xCB]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.eip], xAX
        mov     xAX, [xBP]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.ebp], xAX
        lea     xAX, [xBP + xCB*2]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.esp], xAX

        mov     xCX, [xBP + xCB*2 + xCB*0]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.uVtgProbeLoc], xCX ; keep, used below.

        mov     edx, 20
.more:
        dec     edx
        mov     xAX, [xBP + xCB*2 + xCB*xDX]
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.u.X86.aArgs + xCB*xDX], xAX
        jnz     .more

        mov     eax, [xCX + 4]          ; VTGPROBELOC::idProbe.
        mov     [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.idProbe], eax
        mov     dword [xSP + SUPTRACERUMODFIREPROBE.In + SUPDRVTRACERUSRCTX32.cBits], 32

        ;
        ; Call the helper.
        ;
        mov     xDX, xSP
        push    xDX
        push    xCX
 %ifdef RT_ASM_USE_PLT
        call    NAME(suplibTracerFireProbe) wrt ..plt
 %else
        call    NAME(suplibTracerFireProbe)
 %endif
%else
 %error "Arch not supported (or correctly defined)."
%endif


        leave
        ret
ENDPROC SUPTracerFireProbe

