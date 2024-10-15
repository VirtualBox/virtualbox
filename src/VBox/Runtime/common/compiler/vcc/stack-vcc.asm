; $Id: stack-vcc.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
%ifdef RT_ARCH_AMD64
 %include "iprt/win/context-amd64.mac"
%else
 %include "iprt/win/context-x86.mac"
%endif


;*********************************************************************************************************************************
;*      Structures and Typedefs                                                                                                  *
;*********************************************************************************************************************************

;; Variable descriptor.
struc RTC_VAR_DESC_T
        .offFrame               resd 1
        .cbVar                  resd 1
        alignb                  RTCCPTR_CB
        .pszName                RTCCPTR_RES 1
endstruc

;; Frame descriptor.
struc RTC_FRAME_DESC_T
        .cVars                  resd 1
        alignb                  RTCCPTR_CB
        .paVars                 RTCCPTR_RES 1   ; Array of RTC_VAR_DESC_T.
endstruc

;; An alloca allocation.
struc RTC_ALLOCA_ENTRY_T
        .uGuard1                resd 1
        .pNext                  RTCCPTR_RES 1   ; Misaligned.
%if ARCH_BITS == 32
        .pNextPad               resd 1
%endif
        .cb                     RTCCPTR_RES 1   ; Misaligned.
%if ARCH_BITS == 32
        .cbPad                  resd 1
%endif
        .auGuard2               resd 3
endstruc

%ifdef RT_ARCH_X86
 %define FASTCALL_NAME(a_Name, a_cbArgs)        $@ %+ a_Name %+ @ %+ a_cbArgs
%else
 %define FASTCALL_NAME(a_Name, a_cbArgs)        NAME(a_Name)
%endif


;*********************************************************************************************************************************
;*      Defined Constants And Macros                                                                                             *
;*********************************************************************************************************************************
%define VARIABLE_MARKER_PRE     0xcccccccc
%define VARIABLE_MARKER_POST    0xcccccccc

%define ALLOCA_FILLER_BYTE      0xcc
%define ALLOCA_FILLER_32        0xcccccccc


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BEGINDATA
GLOBALNAME __security_cookie
        dd  0xdeadbeef
        dd  0x0c00ffe0


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BEGINCODE
extern NAME(rtVccStackVarCorrupted)
extern NAME(rtVccSecurityCookieMismatch)
extern NAME(rtVccRangeCheckFailed)
%ifdef RT_ARCH_X86
extern NAME(rtVccCheckEspFailed)
%endif



;;
; This just initializes a global and calls _RTC_SetErrorFuncW to NULL, and
; since we don't have either of those we have nothing to do here.
BEGINPROC _RTC_InitBase
        SEH64_END_PROLOGUE
        ret
ENDPROC   _RTC_InitBase


;;
; Nothing to do here.
BEGINPROC _RTC_Shutdown
        SEH64_END_PROLOGUE
        ret
ENDPROC   _RTC_Shutdown




;;
; Checks stack variable markers.
;
; This seems to be a regular C function in the CRT, but x86 is conveniently
; using the fastcall convention which makes it very similar to amd64.
;
; We try make this as sleek as possible, leaving all the trouble for when we
; find a corrupted stack variable and need to call a C function to complain.
;
; @param        pStackFrame     The caller RSP/ESP.  [RCX/ECX]
; @param        pFrameDesc      Frame descriptor.    [RDX/EDX]
;
ALIGNCODE(64)
BEGINPROC_RAW   FASTCALL_NAME(_RTC_CheckStackVars, 8)
        push    xBP
        SEH64_PUSH_xBP
        SEH64_END_PROLOGUE

        ;
        ; Load the variable count into eax and check that it's not zero.
        ;
        mov     eax, [xDX + RTC_FRAME_DESC_T.cVars]
        test    eax, eax
        jz      .return

        ;
        ; Make edx/rdx point to the current variable and xBP be the frame pointer.
        ; The latter frees up xCX for scratch use and incidentally make stack access
        ; go via SS instead of DS (mostly irrlevant in 64-bit and 32-bit mode).
        ;
        mov     xDX, [xDX + RTC_FRAME_DESC_T.paVars]
        mov     xBP, xCX

        ;
        ; Loop thru the variables and check that their markers/fences haven't be
        ; trampled over.
        ;
.next_var:
        ; Marker before the variable.
%if ARCH_BITS == 64
        movsxd  rcx, dword [xDX + RTC_VAR_DESC_T.offFrame]
%else
        mov     xCX, dword [xDX + RTC_VAR_DESC_T.offFrame]
%endif
        cmp     dword [xBP + xCX - 4], VARIABLE_MARKER_PRE
        jne     rtVccCheckStackVarsFailed

        ; Marker after the variable.
        add     ecx, dword [xDX + RTC_VAR_DESC_T.cbVar]
%if ARCH_BITS == 64
        movsxd  rcx, ecx
%endif
        cmp     dword [xBP + xCX], VARIABLE_MARKER_POST
        jne     rtVccCheckStackVarsFailed

        ;
        ; Advance to the next variable.
        ;
.advance:
        add     xDX, RTC_VAR_DESC_T_size
        dec     eax
        jnz     .next_var

        ;
        ; Return.
        ;
.return:
        pop     xBP
        ret
ENDPROC_RAW     FASTCALL_NAME(_RTC_CheckStackVars, 8)

;
; Sub-function for _RTC_CheckStackVars, for purposes of SEH64 unwinding.
;
; Note! While we consider this fatal and will terminate the application, the
;       compiler guys do not seem to think it is all that horrible and will
;       report failure, maybe do an int3, and then try continue execution.
;
BEGINPROC_RAW   rtVccCheckStackVarsFailed
        nop     ;push    xBP - done in parent function
        SEH64_PUSH_xBP
        mov     xCX, xBP                    ; xCX = caller pStackFrame. xBP free to become frame pointer.
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        push    xAX
        SEH64_PUSH_GREG xAX
        sub     xSP, CONTEXT_SIZE + 20h
        SEH64_ALLOCATE_STACK (CONTEXT_SIZE + 20h)
        SEH64_END_PROLOGUE

        lea     xAX, [xBP - CONTEXT_SIZE]
        call    NAME(rtVccCaptureContext)

        ; rtVccStackVarCorrupted(uint8_t *pbFrame, RTC_VAR_DESC_T const *pVar, PCONTEXT)
.again:
%ifdef RT_ARCH_AMD64
        lea     r8, [xBP - CONTEXT_SIZE]
%else
        lea     xAX, [xBP - CONTEXT_SIZE]
        mov     [xSP + 8], xAX
        mov     [xSP + 4], xDX
        mov     [xSP], xCX
%endif
        call    NAME(rtVccStackVarCorrupted)
        jmp     .again
ENDPROC_RAW     rtVccCheckStackVarsFailed


%ifdef RT_ARCH_X86
;;
; Called to follow up on a 'CMP ESP, EBP' kind of instruction,
; expected to report failure if the compare failed.
;
; Note! While we consider this fatal and will terminate the application, the
;       compiler guys do not seem to think it is all that horrible and will
;       report failure, maybe do an int3, and then try continue execution.
;
ALIGNCODE(16)
BEGINPROC _RTC_CheckEsp
        jne     .unexpected_esp
        ret

.unexpected_esp:
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        push    xAX
        SEH64_PUSH_GREG xAX
        sub     xSP, CONTEXT_SIZE + 20h
        SEH64_ALLOCATE_STACK (CONTEXT_SIZE + 20h)
        SEH64_END_PROLOGUE

        lea     xAX, [xBP - CONTEXT_SIZE]
        call    NAME(rtVccCaptureContext)

        ; rtVccCheckEspFailed(PCONTEXT)
.again:
        lea     xAX, [xBP - CONTEXT_SIZE]
%ifdef RT_ARCH_AMD64
        mov     xCX, xAX
%else
        mov     [xSP], xAX
%endif
        call    NAME(rtVccCheckEspFailed)
        jmp     .again

ENDPROC   _RTC_CheckEsp
%endif ; RT_ARCH_X86



;;
; Initialize an alloca allocation list entry and add it to it.
;
; When this is call, presumably _RTC_CheckStackVars2 is used to verify the frame.
;
; @param        pNewEntry       Pointer to the new entry.               [RCX/ECX]
; @param        cbEntry         The entry size, including header.       [RDX/EDX]
; @param        ppHead          Pointer to the list head pointer.       [R8/stack]
;
ALIGNCODE(64)
BEGINPROC_RAW   FASTCALL_NAME(_RTC_AllocaHelper, 12)
        SEH64_END_PROLOGUE

        ;
        ; Check that input isn't NULL or the size isn't zero.
        ;
        test    xCX, xCX
        jz      .return
        test    xDX, xDX
        jz      .return
%if ARCH_BITS == 64
        test    r8, r8
%else
        cmp     dword [xSP + xCB], 0
%endif
        jz      .return

        ;
        ; Memset the memory to ALLOCA_FILLER
        ;
%if ARCH_BITS == 64
        mov     r10, rdi                ; save rdi
        mov     r11, rcx                ; save pNewEntry
%else
        push    xDI
        push    xCX
        cld                             ; paranoia
%endif

        mov     al, ALLOCA_FILLER_BYTE
        mov     xDI, xCX                ; entry pointer
        mov     xCX, xDX                ; entry size (in bytes)
        rep stosb

%if ARCH_BITS == 64
        mov     rdi, r10
%else
        pop     xCX
        pop     xDI
%endif

        ;
        ; Fill in the entry and link it as onto the head of the chain.
        ;
%if ARCH_BITS == 64
        mov     [r11 + RTC_ALLOCA_ENTRY_T.cb], xDX
        mov     xAX, [r8]
        mov     [r11 + RTC_ALLOCA_ENTRY_T.pNext], xAX
        mov     [r8], r11
%else
        mov     [xCX + RTC_ALLOCA_ENTRY_T.cb], xDX
        mov     xAX, [xSP + xCB]        ; ppHead
        mov     xDX, [xAX]
        mov     [xCX + RTC_ALLOCA_ENTRY_T.pNext], xDX
        mov     [xAX], xCX
%endif

.return:
%if ARCH_BITS == 64
        ret
%else
        ret     4
%endif
ENDPROC_RAW     FASTCALL_NAME(_RTC_AllocaHelper, 12)


;;
; Checks if the security cookie ok, complaining and terminating if it isn't.
;
ALIGNCODE(16)
BEGINPROC_RAW   FASTCALL_NAME(__security_check_cookie, 4)
        SEH64_END_PROLOGUE
        cmp     xCX, [NAME(__security_cookie) xWrtRIP]
        jne     rtVccSecurityCookieFailed
        ;; amd64 version checks if the top 16 bits are zero, we skip that for now.
        ret
ENDPROC_RAW     FASTCALL_NAME(__security_check_cookie, 4)

; Sub-function for __security_check_cookie, for purposes of SEH64 unwinding.
BEGINPROC_RAW   rtVccSecurityCookieFailed
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        push    xAX
        SEH64_PUSH_GREG xAX
        sub     xSP, CONTEXT_SIZE + 20h
        SEH64_ALLOCATE_STACK (CONTEXT_SIZE + 20h)
        SEH64_END_PROLOGUE

        lea     xAX, [xBP - CONTEXT_SIZE]
        call    NAME(rtVccCaptureContext)

        ; rtVccSecurityCookieMismatch(uCookie, PCONTEXT)
.again:
%ifdef RT_ARCH_AMD64
        lea     xDX, [xBP - CONTEXT_SIZE]
%else
        lea     xAX, [xBP - CONTEXT_SIZE]
        mov     [xSP + 4], xAX
        mov     [xSP], xCX
%endif
        call    NAME(rtVccSecurityCookieMismatch)
        jmp     .again
ENDPROC_RAW     rtVccSecurityCookieFailed


;;
; Generated when using /GS - buffer security checks - so, fatal.
;
; Doesn't seem to take any parameters.
;
BEGINPROC __report_rangecheckfailure
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        push    xAX
        SEH64_PUSH_GREG xAX
        sub     xSP, CONTEXT_SIZE + 20h
        SEH64_ALLOCATE_STACK (CONTEXT_SIZE + 20h)
        SEH64_END_PROLOGUE

        lea     xAX, [xBP - CONTEXT_SIZE]
        call    NAME(rtVccCaptureContext)

        ; rtVccRangeCheckFailed(PCONTEXT)
.again:
        lea     xAX, [xBP - CONTEXT_SIZE]
%ifdef RT_ARCH_AMD64
        mov     xCX, xAX
%else
        mov     [xSP], xAX
%endif
        call    NAME(rtVccRangeCheckFailed)
        jmp     .again
ENDPROC   __report_rangecheckfailure


%if 0 ; Currently not treating these as completely fatal, just like the
      ; compiler guys do.  I'm sure the compiler only generate these calls
      ; if it thinks a variable could be used uninitialized, however I'm not
      ; really sure if there is a runtime check in addition or if it's an
      ; action that always will be taken in a code path deemed to be bad.
      ; Judging from the warnings, the compile time analysis leave lots to be
      ; desired (lots of false positives).
;;
; Not entirely sure how and when the compiler generates these.
; extern "C" void __cdecl _RTC_UninitUse(const char *pszVar)
BEGINPROC   _RTC_UninitUse
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        push    xAX
        SEH64_PUSH_GREG xAX
        sub     xSP, CONTEXT_SIZE + 20h
        SEH64_ALLOCATE_STACK (CONTEXT_SIZE + 20h)
        SEH64_END_PROLOGUE

        lea     xAX, [xBP - CONTEXT_SIZE]
        call    NAME(rtVccCaptureContext)

        extern NAME(rtVccUninitializedVariableUse)
        ; rtVccUninitializedVariableUse(const char *pszVar, PCONTEXT)
.again:
%ifdef RT_ARCH_AMD64
        lea     xDX, [xBP - CONTEXT_SIZE]
%else
        lea     xAX, [xBP - CONTEXT_SIZE]
        mov     [xSP + xCB], xAX
        mov     xAX, [xBP + xCB * 2]
        mov     [xSP], xAX
%endif
        call    NAME(rtVccUninitializedVariableUse)
        jmp     .again
ENDPROC     _RTC_UninitUse
%endif

;;
; Internal worker that creates a CONTEXT record for the caller.
;
; This expects a old-style stack frame setup, with xBP as base, such that:
;       xBP+xCB*1:  Return address  -> Rip/Eip
;       xBP+xCB*0:  Return xBP      -> Rbp/Ebp
;       xBP-xCB*1:  EFLAGS          -> EFlags
;       xBP-xCB*2:  Saved xAX       -> Rax/Eax
;
; @param    pCtx        xAX     Pointer to a CONTEXT structure.
;
BEGINPROC rtVccCaptureContext
        SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
        mov     [xAX + CONTEXT.Rcx], rcx
        mov     [xAX + CONTEXT.Rdx], rdx
        mov     rcx, [xBP - xCB*2]
        mov     [xAX + CONTEXT.Rax], ecx
        mov     [xAX + CONTEXT.Rbx], rbx
        lea     rcx, [xBP + xCB*2]
        mov     [xAX + CONTEXT.Rsp], rcx
        mov     rdx, [xBP]
        mov     [xAX + CONTEXT.Rbp], rdx
        mov     [xAX + CONTEXT.Rdi], rdi
        mov     [xAX + CONTEXT.Rsi], rsi
        mov     [xAX + CONTEXT.R8], r8
        mov     [xAX + CONTEXT.R9], r9
        mov     [xAX + CONTEXT.R10], r10
        mov     [xAX + CONTEXT.R11], r11
        mov     [xAX + CONTEXT.R12], r12
        mov     [xAX + CONTEXT.R13], r13
        mov     [xAX + CONTEXT.R14], r14
        mov     [xAX + CONTEXT.R15], r15

        mov     rcx, [xBP + xCB*1]
        mov     [xAX + CONTEXT.Rip], rcx
        mov     edx, [xBP - xCB*1]
        mov     [xAX + CONTEXT.EFlags], edx

        mov     dx, ss
        mov     [xAX + CONTEXT.SegSs], dx
        mov     cx, cs
        mov     [xAX + CONTEXT.SegCs], cx
        mov     dx, ds
        mov     [xAX + CONTEXT.SegDs], dx
        mov     cx, es
        mov     [xAX + CONTEXT.SegEs], cx
        mov     dx, fs
        mov     [xAX + CONTEXT.SegFs], dx
        mov     cx, gs
        mov     [xAX + CONTEXT.SegGs], cx

        mov     dword [xAX + CONTEXT.ContextFlags], CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS

        ; Clear stuff we didn't set.
        xor     edx, edx
        mov     [xAX + CONTEXT.P1Home], rdx
        mov     [xAX + CONTEXT.P2Home], rdx
        mov     [xAX + CONTEXT.P3Home], rdx
        mov     [xAX + CONTEXT.P4Home], rdx
        mov     [xAX + CONTEXT.P5Home], rdx
        mov     [xAX + CONTEXT.P6Home], rdx
        mov     [xAX + CONTEXT.MxCsr], edx
        mov     [xAX + CONTEXT.Dr0], rdx
        mov     [xAX + CONTEXT.Dr1], rdx
        mov     [xAX + CONTEXT.Dr2], rdx
        mov     [xAX + CONTEXT.Dr3], rdx
        mov     [xAX + CONTEXT.Dr6], rdx
        mov     [xAX + CONTEXT.Dr7], rdx

        mov     ecx, CONTEXT_size - CONTEXT.FltSave
        AssertCompile(((CONTEXT_size - CONTEXT.FltSave) % 8) == 0)
.again:
        mov     [xAX + CONTEXT.FltSave + xCX - 8], rdx
        sub     ecx, 8
        jnz     .again

        ; Restore edx and ecx.
        mov     rcx, [xAX + CONTEXT.Rcx]
        mov     rdx, [xAX + CONTEXT.Rdx]

%elifdef RT_ARCH_X86

        mov     [xAX + CONTEXT.Ecx], ecx
        mov     [xAX + CONTEXT.Edx], edx
        mov     ecx, [xBP - xCB*2]
        mov     [xAX + CONTEXT.Eax], ecx
        mov     [xAX + CONTEXT.Ebx], ebx
        lea     ecx, [xBP + xCB*2]
        mov     [xAX + CONTEXT.Esp], ecx
        mov     edx, [xBP]
        mov     [xAX + CONTEXT.Ebp], edx
        mov     [xAX + CONTEXT.Edi], edi
        mov     [xAX + CONTEXT.Esi], esi

        mov     ecx, [xBP + xCB]
        mov     [xAX + CONTEXT.Eip], ecx
        mov     ecx, [xBP - xCB*1]
        mov     [xAX + CONTEXT.EFlags], ecx

        mov     dx, ss
        movzx   edx, dx
        mov     [xAX + CONTEXT.SegSs], edx
        mov     cx, cs
        movzx   ecx, cx
        mov     [xAX + CONTEXT.SegCs], ecx
        mov     dx, ds
        movzx   edx, dx
        mov     [xAX + CONTEXT.SegDs], edx
        mov     cx, es
        movzx   ecx, cx
        mov     [xAX + CONTEXT.SegEs], ecx
        mov     dx, fs
        movzx   edx, dx
        mov     [xAX + CONTEXT.SegFs], edx
        mov     cx, gs
        movzx   ecx, cx
        mov     [xAX + CONTEXT.SegGs], ecx

        mov     dword [xAX + CONTEXT.ContextFlags], CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS

        ; Clear stuff we didn't set.
        xor     edx, edx
        mov     [xAX + CONTEXT.Dr0], edx
        mov     [xAX + CONTEXT.Dr1], edx
        mov     [xAX + CONTEXT.Dr2], edx
        mov     [xAX + CONTEXT.Dr3], edx
        mov     [xAX + CONTEXT.Dr6], edx
        mov     [xAX + CONTEXT.Dr7], edx

        mov     ecx, CONTEXT_size - CONTEXT.ExtendedRegisters
.again:
        mov     [xAX + CONTEXT.ExtendedRegisters + xCX - 4], edx
        sub     ecx, 4
        jnz     .again

        ; Restore edx and ecx.
        mov     ecx, [xAX + CONTEXT.Ecx]
        mov     edx, [xAX + CONTEXT.Edx]

%else
 %error RT_ARCH
%endif
        ret
ENDPROC   rtVccCaptureContext

