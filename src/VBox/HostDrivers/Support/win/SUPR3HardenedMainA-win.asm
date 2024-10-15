; $Id: SUPR3HardenedMainA-win.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VirtualBox Support Library - Hardened main(), Windows assembly bits.
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
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"


; External code.
extern NAME(supR3HardenedEarlyProcessInit)
extern NAME(supR3HardenedMonitor_KiUserApcDispatcher_C)
%ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
extern NAME(supR3HardenedMonitor_KiUserExceptionDispatcher_C)
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
        leave
        ret
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
        leave
        ret
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


;;
; Import data and code for an API call.
;
; @param 1  The plain API name.
; @param 2  The parameter frame size on x86. Multiple of dword.
; @param 3  Non-zero expression if system call.
; @param 4  Non-zero expression if early available call
;
%define SUPHNTIMP_SYSCALL 1
%macro SupHardNtImport 4
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
        jmp     RTCCPTR_PRE [NAME(g_pfn %+ %1) xWrtRIP]

%if %3
        ;
        ; Make system calls.
        ;
 %ifdef RT_ARCH_AMD64
BEGINPROC %1 %+ _SyscallType1
        SEH64_END_PROLOGUE
        mov     eax, [NAME(g_uApiNo %+ %1) xWrtRIP]
        mov     r10, rcx
        syscall
        ret
ENDPROC %1 %+ _SyscallType1
BEGINPROC %1 %+ _SyscallType2 ; Introduced with build 10525
        SEH64_END_PROLOGUE
        mov     eax, [NAME(g_uApiNo %+ %1) xWrtRIP]
        test    byte [07ffe0308h], 1    ; SharedUserData!Something
        mov     r10, rcx
        jnz     .int_alternative
        syscall
        ret
.int_alternative:
        int     2eh
        ret
ENDPROC %1 %+ _SyscallType2
 %else
BEGINPROC %1 %+ _SyscallType1
        mov     edx, 07ffe0300h         ; SharedUserData!SystemCallStub
        mov     eax, [NAME(g_uApiNo %+ %1) xWrtRIP]
        call    dword [edx]
        ret     %2
ENDPROC %1 %+ _SyscallType1
BEGINPROC %1 %+ _SyscallType2
        push    .return
        mov     edx, esp
        mov     eax, [NAME(g_uApiNo %+ %1) xWrtRIP]
        sysenter
        add     esp, 4
.return:
        ret     %2
ENDPROC %1 %+ _SyscallType2
  %endif
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
%define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86)          SupHardNtImport a_Name, a_cbParamsX86, SUPHNTIMP_SYSCALL, 1
%define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)          SupHardNtImport a_Name, a_cbParamsX86, 0, 0
%define SUPHARNT_IMPORT_STDCALL_OPTIONAL(a_Name, a_cbParamsX86) SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)
%define SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)    SupHardNtImport a_Name, a_cbParamsX86, 0, 1
%define SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL(a_Name, a_cbParamsX86) SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)
%include "import-template-ntdll.h"
%include "import-template-kernel32.h"


;
; For simplified LdrLoadDll patching we define a special writable, readable and
; exectuable section of 4KB where we can put jump back code.
;
section .rwxpg bss execute read write align=4096
GLOBALNAME g_abSupHardReadWriteExecPage
        resb    4096

