; $Id: SUPR3HardenedMainA-win.asm 115013 2026-08-12 23:38:23Z knut.osmundsen@oracle.com $
;; @file
; VirtualBox Support Library - Hardened main(), Windows assembly bits.
;

;
; Copyright (C) 2012-2026 Oracle and/or its affiliates.
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
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


; External code.
extern NAME(supR3HardenedEarlyProcessInit)
extern NAME(supR3HardenedMonitor_KiUserApcDispatcher_C)
%ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
extern NAME(supR3HardenedMonitor_KiUserExceptionDispatcher_C)
%endif
%ifdef RT_ARCH_AMD64
extern NAME(RTSha256)
%endif


BEGINCODE


;;
; Alternative code for LdrInitializeThunk that performs the early process startup
; for the Stub and VM processes.
;
; This does not concern itself with any arguments on stack or in registers that
; may be passed to the LdrIntializeThunk routine as we just save and restore
; them all before we restart the restored LdrInitializeThunk routine.
;
; @sa supR3HardenedEarlyProcessInit
;
BEGINPROC supR3HardenedEarlyProcessInitThunk
        ;
        ; Prologue.
        ;

        ; Reserve space for the "return" address.
        push    0

        ; Create a stack frame, saving xBP.
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0 ; probably wrong...

        ; Save all volatile registers.
        push    xAX
        push    xCX
        push    xDX
%ifdef RT_ARCH_AMD64
        push    r8
        push    r9
        push    r10
        push    r11
%endif

        ; Reserve spill space and align the stack.
        sub     xSP, 20h
        and     xSP, ~0fh
        SEH64_END_PROLOGUE

        ;
        ; Call the C/C++ code that does the actual work.  This returns the
        ; resume address in xAX, which we put in the "return" stack position.
        ;
        call    NAME(supR3HardenedEarlyProcessInit)
        mov     [xBP + xCB], xAX

        ;
        ; Restore volatile registers.
        ;
        mov     xAX, [xBP - xCB*1]
        mov     xCX, [xBP - xCB*2]
        mov     xDX, [xBP - xCB*3]
%ifdef RT_ARCH_AMD64
        mov     r8,  [xBP - xCB*4]
        mov     r9,  [xBP - xCB*5]
        mov     r10, [xBP - xCB*6]
        mov     r11, [xBP - xCB*7]
%endif
        ;
        ; Use the leave instruction to restore xBP and set up xSP to point at
        ; the resume address. Then use the 'ret' instruction to resume process
        ; initializaton.
        ;
        ; Update 2026-04-13: With shadow stacks enabled, this RET doesn't work
        ; so, we're sacrifying r11 for linking here.
        ;
        leave
%ifndef RT_ARCH_AMD64
        ret
%else
        pop     r11
        db      0x3e                        ; paranoia: notrack (DS prefix)
        jmp     r11
%endif
ENDPROC   supR3HardenedEarlyProcessInitThunk


;;
; Hook for KiUserApcDispatcher that validates user APC calls during early process
; init to prevent calls going to or referring to executable memory we've freed
; already.
;
; We just call C code here, just like supR3HardenedEarlyProcessInitThunk does.
;
; @sa supR3HardenedMonitor_KiUserApcDispatcher_C
;
BEGINPROC supR3HardenedMonitor_KiUserApcDispatcher
        ;
        ; Prologue.
        ;

        ; Reserve space for the "return" address.
        push    0

        ; Create a stack frame, saving xBP.
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0 ; probably wrong...

        ; Save all volatile registers.
        push    xAX
        push    xCX
        push    xDX
%ifdef RT_ARCH_AMD64
        push    r8
        push    r9
        push    r10
        push    r11
%endif

        ; Reserve spill space and align the stack.
        sub     xSP, 20h
        and     xSP, ~0fh
        SEH64_END_PROLOGUE

        ;
        ; Call the C/C++ code that does the actual work.  This returns the
        ; resume address in xAX, which we put in the "return" stack position.
        ;
        ; On AMD64, a CONTEXT structure is found at our RSP address when we're called.
        ; On x86, there a 16 byte structure containing the two routines and their
        ; arguments followed by a CONTEXT structure.
        ;
        lea     xCX, [xBP + xCB + xCB]
%ifdef RT_ARCH_X86
        mov     [xSP], xCX
%endif
        call    NAME(supR3HardenedMonitor_KiUserApcDispatcher_C)
        mov     [xBP + xCB], xAX

        ;
        ; Restore volatile registers.
        ;
        mov     xAX, [xBP - xCB*1]
        mov     xCX, [xBP - xCB*2]
        mov     xDX, [xBP - xCB*3]
%ifdef RT_ARCH_AMD64
        mov     r8,  [xBP - xCB*4]
        mov     r9,  [xBP - xCB*5]
        mov     r10, [xBP - xCB*6]
        mov     r11, [xBP - xCB*7]
%endif
        ;
        ; Use the leave instruction to restore xBP and set up xSP to point at
        ; the resume address. Then use the 'ret' instruction to execute the
        ; original KiUserApcDispatcher code as if we've never been here...
        ;
        ; Update 2026-04-13: With shadow stacks enabled, this RET doesn't work
        ; so, we're sacrifying r11 for linking here.
        ;
        leave
%ifndef RT_ARCH_AMD64
        ret
%else
        pop     r11
        db      0x3e                        ; paranoia: notrack (DS prefix)
        jmp     r11
%endif
ENDPROC   supR3HardenedMonitor_KiUserApcDispatcher


%ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
;;
; Hook for KiUserExceptionDispatcher that logs exceptions.
;
; For the AMD64 variant, we're not directly intercepting the function itself, but
; patching into a Wow64 callout that's done at the very start of the routine.  RCX
; and RDX are set to PEXCEPTION_RECORD and PCONTEXT respectively and there is a
; return address.  Also, we don't need to do any return-via-copied-out-code stuff.
;
; For X86 we hook the function and have PEXCEPTION_RECORD and PCONTEXT pointers on
; the stack, but no return address.

; We just call C code here, just like supR3HardenedEarlyProcessInitThunk and
; supR3HardenedMonitor_KiUserApcDispatcher does.
;
; @sa supR3HardenedMonitor_KiUserExceptionDispatcher_C
;
BEGINPROC supR3HardenedMonitor_KiUserExceptionDispatcher
        ;
        ; Prologue.
        ;

 %ifndef RT_ARCH_AMD64
        ; Reserve space for the "return" address.
        push    0
 %endif

        ; Create a stack frame, saving xBP.
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0 ; probably wrong...

        ; Save all volatile registers.
        push    xAX
        push    xCX
        push    xDX
 %ifdef RT_ARCH_AMD64
        push    r8
        push    r9
        push    r10
        push    r11
 %endif

        ; Reserve spill space and align the stack.
        sub     xSP, 20h
        and     xSP, ~0fh
        SEH64_END_PROLOGUE

        ;
        ; Call the C/C++ code that does the actual work.  For x86 this returns
        ; the resume address in xAX, which we put in the "return" stack position.
        ;
        ; On both AMD64 and X86 we have two parameters on the stack that we
        ; passes along to the C code (see function description for details).
        ;
 %ifdef RT_ARCH_X86
        mov     xCX, [xBP + xCB*2]
        mov     xDX, [xBP + xCB*3]
        mov     [xSP], xCX
        mov     [xSP+4], xDX
 %endif
        call    NAME(supR3HardenedMonitor_KiUserExceptionDispatcher_C)
 %ifdef RT_ARCH_X86
        mov     [xBP + xCB], xAX
 %endif

        ;
        ; Restore volatile registers.
        ;
        mov     xAX, [xBP - xCB*1]
        mov     xCX, [xBP - xCB*2]
        mov     xDX, [xBP - xCB*3]
 %ifdef RT_ARCH_AMD64
        mov     r8,  [xBP - xCB*4]
        mov     r9,  [xBP - xCB*5]
        mov     r10, [xBP - xCB*6]
        mov     r11, [xBP - xCB*7]
 %endif
        ;
        ; Use the leave instruction to restore xBP and set up xSP to point at
        ; the resume address. Then use the 'ret' instruction to execute the
        ; original KiUserExceptionDispatcher code as if we've never been here...
        ;
        leave
        ret
ENDPROC   supR3HardenedMonitor_KiUserExceptionDispatcher
%endif ; !VBOX_WITHOUT_HARDENDED_XCPT_LOGGING

;;
; Composes a standard call name.
%ifdef RT_ARCH_X86
 %define SUPHNTIMP_STDCALL_NAME(a,b) _ %+ a %+ @ %+ b
%else
 %define SUPHNTIMP_STDCALL_NAME(a,b) NAME(a)
%endif

;; Concats two litterals.
%define SUPHNTIMP_CONCAT(a,b) a %+ b

BEGINCODE
%ifdef RT_ARCH_AMD64

;;
; Helper for HardenedSyscallHashStackPre and HardenedSyscallHashStackPostCheck.
;
; @clobbers rax, rcx, rdx, r8, r9, r10, r11
; @returns  r9:r8:rdx:rax   256-bit hash value.
;
BEGINPROC HardenedSyscallHashStack
        ;
        ; Stack frame.
        ;   - 8 byte alignment padding.
        ;   - 32 bytes for digest.
        ;   - 4*8 bytes for call spill area.
        ;
 %define MY_STACK_FRAME         (4*8 + 32 + 8)
        sub     rsp, MY_STACK_FRAME
        SEH64_ALLOCATE_STACK  MY_STACK_FRAME
        SEH64_END_PROLOGUE

        ;
        ; Determine the stack range we'd like to hash.  The hashing includes
        ; the syscall's return address and the 4 saved non-volatile registers.
        ;
        ; ASSUMES the stackframes are identical.
        ;

        ; rcx=start (rsp + frame size + our ret addr + caller pushes + caller's ret address)
        lea     rcx, [rsp + MY_STACK_FRAME + 8 + 7*8 + 8]

        ; Load the stack limits.
        mov     rdx, [gs:8]                 ; NT_TIB::StackBase
        mov     rax, [gs:16]                ; NT_TIB::StackLimit
        cmp     rdx, rax                    ; The limit shall be lower than the base address.
        jle     .alt_stack
        ; Check that rcx is within the stack limits.
        cmp     rcx, rdx                    ; rcx vs NT_TIB::StackBase
        jge     .alt_stack
        cmp     rcx, rax                    ; rcx vs NT_TIB::StackLimit
        jle     .alt_stack
        ; Calculate the number of bytes to check.
        sub     rdx, rcx                    ; rdx=number of bytes to check.
.alt_stack_jumpback:
        cmp     rdx, 168                    ; Stack size sanity check.
        jg      .min_size_checked
        add     edx, X86_PAGE_SIZE
.min_size_checked:

        ;
        ; Call hash function.
        ;
        lea     r8, [rsp + 4*8]             ; digest buffer address
        call    NAME(RTSha256)

        ; Load digest into the return registers.
        mov     rax, [rsp + 4*8]
        mov     rdx, [rsp + 4*8 + 8]
        mov     r8,  [rsp + 4*8 + 16]
        mov     r9,  [rsp + 4*8 + 24]

        ; Mix in the stack address.
        xor     rdx, rsp

        ; Cleanup the call and return.
        add     rsp, MY_STACK_FRAME
        ret
        int3

.alt_stack:
        ; Check up to the next page.
        mov     rdx, X86_PAGE_SIZE
        sub     rdx, rcx
        and     rdx, X86_PAGE_OFFSET_MASK
        jmp     .alt_stack_jumpback

 %undef MY_STACK_FRAME
ENDPROC HardenedSyscallHashStack


;;
; Pre-syscall hardening helper that hashes up the stack.
BEGINPROC HardenedSyscallHashStackPreSetup
        ;
        ; Save a set of non-volatile registes in the 4 unused register-argument slots.
        ;
        mov     [rsp +  8 + 8], rbx
        mov     [rsp + 16 + 8], r12
        mov     [rsp + 24 + 8], r13
        mov     [rsp + 32 + 8], r14

        ;
        ; Save all volatile registers, hash the stack and move the result
        ; into the non-volatile registers we just saved above.
        ;
        push    rax
        SEH64_PUSH_GREG rax
        push    rcx
        SEH64_PUSH_GREG rcx
        push    rdx
        SEH64_PUSH_GREG rdx
        push    r8
        SEH64_PUSH_GREG r8
        push    r9
        SEH64_PUSH_GREG r9
        push    r10
        SEH64_PUSH_GREG r10
        push    r11
        SEH64_PUSH_GREG r11
        SEH64_END_PROLOGUE

        call    NAME(HardenedSyscallHashStack)
        mov     rbx, rax
        mov     r12, rdx
        mov     r13, r8
        mov     r14, r9

        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdx
        pop     rcx
        pop     rax
        ret
ENDPROC HardenedSyscallHashStackPreSetup

;;
; Post-syscall hardening helper that checks that the stack is unchanged since before the call.
BEGINPROC HardenedSyscallHashStackPostCheck
        ;
        ; Save all volatile registers, hash the stack and check if it matches
        ; the pre-call hash values from the non-volatile registers.
        ;
        push    rax
        SEH64_PUSH_GREG rax
        push    rcx
        SEH64_PUSH_GREG rcx
        push    rdx
        SEH64_PUSH_GREG rdx
        push    r8
        SEH64_PUSH_GREG r8
        push    r9
        SEH64_PUSH_GREG r9
        push    r10
        SEH64_PUSH_GREG r10
        push    r11
        SEH64_PUSH_GREG r11
        SEH64_END_PROLOGUE

        call    NAME(HardenedSyscallHashStack)
        cmp     rbx, rax
        jne     .stack_check_failed
        cmp     r12, rdx
        jne     .stack_check_failed
        cmp     r13, r8
        jne     .stack_check_failed
        cmp     r14, r9
        jne     .stack_check_failed

        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdx
        pop     rcx
        pop     rax

        ;
        ; Restore the 4 non-volatile registers saved at the beginning of HardenedSyscallHashStackPre.
        ;
        mov     rbx, [rsp +  8 + 8]
        mov     r12, [rsp + 16 + 8]
        mov     r13, [rsp + 24 + 8]
        mov     r14, [rsp + 32 + 8]
        ret
        int3

.stack_check_failed:
        mov     rbp, 99h
        mov     dword [rbp], 99h
        jmp     .stack_check_failed
ENDPROC HardenedSyscallHashStackPostCheck

%endif ; RT_ARCH_AMD64


;;
; Import data and code for an API call.
;
; @param 1  The plain API name.
; @param 2  The parameter frame size on x86. Multiple of dword.
; @param 3  Non-zero expression if system call.
; @param 4  Non-zero expression if early available call
; @param 5  Non-zero expression for stack checking.
;
%define SUPHNTIMP_SYSCALL 1
%macro SupHardNtImport 5
        ;
        ; The data.
        ;
BEGINDATA
global __imp_ %+ SUPHNTIMP_STDCALL_NAME(%1,%2)  ; The import name used via dllimport.
__imp_ %+ SUPHNTIMP_STDCALL_NAME(%1,%2):
GLOBALNAME g_pfn %+ %1                          ; The name we like to refer to.
        RTCCPTR_DEF 0
 %if %3
GLOBALNAME g_uApiNo %+ %1
        RTCCPTR_DEF 0
 %endif

        ;
        ; The code: First a call stub.
        ;
BEGINCODE
global SUPHNTIMP_STDCALL_NAME(%1, %2)
SUPHNTIMP_STDCALL_NAME(%1, %2):
        jmp     RTCCPTR_PRE [RT_WRT_RIP(NAME(g_pfn %+ %1))]
        int3

 %if %3
        ;
        ; Make system calls.
        ;
  %ifdef RT_ARCH_AMD64
BEGINPROC %1 %+ _SyscallType1
        SEH64_END_PROLOGUE
   %if %5
        call    NAME(HardenedSyscallHashStackPreSetup)
   %endif
        mov     eax, [RT_WRT_RIP(NAME(g_uApiNo %+ %1))]
        mov     r10, rcx
        syscall
   %if %5
        call    NAME(HardenedSyscallHashStackPostCheck)
   %endif
        ret
ENDPROC %1 %+ _SyscallType1

BEGINPROC %1 %+ _SyscallType2 ; Introduced with build 10525
        SEH64_END_PROLOGUE
   %if %5
        call    NAME(HardenedSyscallHashStackPreSetup)
   %endif
        mov     eax, [RT_WRT_RIP(NAME(g_uApiNo %+ %1))]
        test    byte [07ffe0308h], 1    ; SharedUserData!Something
        mov     r10, rcx
        jnz     .int_alternative
        syscall
   %if %5
        call    NAME(HardenedSyscallHashStackPostCheck)
   %endif
        ret
        int3
.int_alternative:
        int     2eh
   %if %5
        call    NAME(HardenedSyscallHashStackPostCheck)
   %endif
        ret
ENDPROC %1 %+ _SyscallType2

  %else  ; !RT_ARCH_AMD64
BEGINPROC %1 %+ _SyscallType1
        mov     edx, 07ffe0300h         ; SharedUserData!SystemCallStub
        mov     eax, [RT_WRT_RIP(NAME(g_uApiNo %+ %1))]
        call    dword [edx]
        ret     %2
ENDPROC %1 %+ _SyscallType1

BEGINPROC %1 %+ _SyscallType2
        push    .return
        mov     edx, esp
        mov     eax, [RT_WRT_RIP(NAME(g_uApiNo %+ %1))]
        sysenter
        add     esp, 4
.return:
        ret     %2
ENDPROC %1 %+ _SyscallType2
  %endif ; !RT_ARCH_AMD64
 %endif

 %if %4 == 0
global NAME(SUPHNTIMP_CONCAT(%1,_Early))
NAME(SUPHNTIMP_CONCAT(%1,_Early)):
        int3
  %ifdef RT_ARCH_AMD64
        ret
  %else
        ret     %2
  %endif
 %endif
%endmacro

%define SUPHARNT_COMMENT(a_Comment)
%define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86)                SupHardNtImport a_Name, a_cbParamsX86, SUPHNTIMP_SYSCALL, 1, 0
%define SUPHARNT_IMPORT_SYSCALL_HASH_STACK(a_Name, a_cbParamsX86)     SupHardNtImport a_Name, a_cbParamsX86, SUPHNTIMP_SYSCALL, 1, 1
%define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)                SupHardNtImport a_Name, a_cbParamsX86, 0, 0, 0
%define SUPHARNT_IMPORT_STDCALL_OPTIONAL(a_Name, a_cbParamsX86)       SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)
%define SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)          SupHardNtImport a_Name, a_cbParamsX86, 0, 1, 0
%define SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL(a_Name, a_cbParamsX86) SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)
%include "import-template-ntdll.h"
%include "import-template-kernel32.h"


;
; For simplified LdrLoadDll patching we define a special writable, readable and
; exectuable section of 4KB where we can put jump back code.
;
%ifdef __NASM__
section .rwxpg bss align=4096 ;; @todo NASM: add way to create RXW sections.
%else
section .rwxpg bss execute read write align=4096
%endif
GLOBALNAME g_abSupHardReadWriteExecPage
        resb    4096

MARK_OBJECT_RETPOLINE_SAFE  ;; @todo retpoline: There are of course indirect calls/jmps here.
