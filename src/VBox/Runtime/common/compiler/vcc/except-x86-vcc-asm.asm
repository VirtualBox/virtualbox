; $Id: except-x86-vcc-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Visual C++ Compiler - x86 Exception Handler Support Code.
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
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "iprt/asmdefs.mac"


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************

;; @def WITH_NLG_STUFF
; Enabled NLG debugger hooks/whatever - no idea how it's used.
;
; So far vsdebugeng.manimpl.dll in the VS2019 remote debugger directory is the
; only component with any _NLG_ strings in it.  Could not find any references
; to _NLG_ in windbg.
%define WITH_NLG_STUFF 1


;*********************************************************************************************************************************
;*  Structures and Typedefs                                                                                                      *
;*********************************************************************************************************************************

struc EH4_XCPT_REG_REC_T
    .uSavedEsp                  resd 1      ;   0 / 0x00
    .pXctpPtrs                  resd 1      ;   4 / 0x04
    .XcptRec                    resd 2      ;   8 / 0x08
    .uEncodedScopeTable         resd 1      ;  16 / 0x10
    .uTryLevel                  resd 1      ;  20 / 0x14
endstruc                                    ;  24 / 0x18


;;
; Scope table record describing  __try / __except / __finally blocks (aka
; EH4_SCOPETABLE_RECORD).
;
struc EH4_SCOPE_TAB_REC_T
    .uEnclosingLevel            resd 1
    .pfnFilter                  resd 1
    .pfnHandlerOrFinally        resd 1
endstruc

;; Special EH4_SCOPE_TAB_REC_T::uEnclosingLevel used to terminate the chain.
%define EH4_TOPMOST_TRY_LEVEL   0xfffffffe

;;
; Scope table used by _except_handler4 (aka EH4_SCOPETABLE).
;
struc EH4_SCOPE_TAB_T
    .offGSCookie                resd 1
    .offGSCookieXor             resd 1
    .offEHCookie                resd 1
    .offEHCookieXor             resd 1
    .aScopeRecords              resb 12
endstruc


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BEGINCODE
extern IMPNAME(RtlUnwind@16)
extern _rtVccEh4DoLocalUnwindHandler@16


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************

;; Delcare rtVccEh4DoLocalUnwindHandler() in except-x86.cpp as a save exception handler.
; This adds the symbol table number of the exception handler to the special .sxdata section.
safeseh _rtVccEh4DoLocalUnwindHandler@16

%ifdef WITH_NLG_STUFF
BEGINDATA
GLOBALNAME_RAW __NLG_Destination, data, hidden
        dd      019930520h
        dd      0
        dd      0
        dd      0
%endif


BEGINCODE

%ifdef WITH_NLG_STUFF

;;
; Some VS debugger interface, I think.
;
; @param        EDX     Notification code.
; @uses         EBP, EAX & ECX are preserved.  This differs from other implementation,
;               but simplifies the calling code.
;
ALIGNCODE(32)
GLOBALNAME_RAW __NLG_Notify, function, hidden
        ;
        ; Save registers.
        ;
        push    eax
        push    ecx
        push    ebp
        push    ebx


        ;
        ; ECX = notification code.
        ; EBX = __NLG_Destination
        ;
        mov     ecx, edx                    ; Originally held in ECX, so moving it there for now.
        mov     ebx, __NLG_Destination

__NLG_Go:
        ;
        ; Save info to __NLG_Destination and the stack (same layout).
        ;
        mov     [ebx + 0ch], ebp
        push    ebp
        mov     [ebx + 08h], ecx
        push    ecx
        mov     [ebx + 04h], eax
        push    eax

        ;
        ; This is presumably the symbol the debugger hooks on to as the string
        ; "__NLG_Dispatch" was found in vsdebugeng.manimpl.dll in VS2019.
        ;
global __NLG_Dispatch
__NLG_Dispatch:

        ; Drop the info structure from the stack.
        add     esp, 4*3

        ;
        ; Restore registers and drop the parameter as we return.
        ;
        ; Note! This may sabotage attempts by the debugger to modify the state...
        ;       But that can be fixed once we know this is a requirement. Just
        ;       keep things simple for the caller for now.
        ;
        pop     ebx
        pop     ebp
        pop     ecx
        pop     eax
        ret

;;
; NLG call + return2.
;
; The "__NLG_Return2" symbol was observed in several vsdebugeng.manimpl.dll
; strings in VS2019.
;
ALIGNCODE(4)
GLOBALNAME_RAW __NLG_Call, function, hidden
        call    eax

global __NLG_Return2
__NLG_Return2:
        ret

%endif


;;
; Calls the filter sub-function for a __finally statement.
;
; This sets all GRPs to zero, except for ESP, EBP and EAX.
;
; @param    pfnFilter   [ebp + 08h]
; @param    fAbend      [ebp + 0ch]
; @param    pbFrame     [ebp + 10h]
;
ALIGNCODE(64)
BEGINPROC   rtVccEh4DoFinally
        push    ebp
        mov     ebp, esp
        push    ebx
        push    edi
        push    esi

        xor     edi, edi
        xor     esi, esi
%ifndef WITH_NLG_STUFF
        xor     edx, edx
%endif
        xor     ebx, ebx

        mov     eax, [ebp + 08h]            ; pfnFilter
        movzx   ecx, byte [ebp + 0ch]       ; fAbend
        mov     ebp, [ebp + 10h]            ; pbFrame

%ifdef WITH_NLG_STUFF
        mov     edx, 101h
        call    __NLG_Notify
        xor     edx, edx

        call    __NLG_Call
%else
        call    eax
%endif

        pop     esi
        pop     edi
        pop     ebx
        pop     ebp
        ret
ENDPROC   rtVccEh4DoFinally


;;
; Calls the filter sub-function for an __except statement.
;
; This sets all GRPs to zero, except for ESP, EBP and ECX.
;
; @param    pfnFilter   [ebp + 08h]
; @param    pbFrame     [ebp + 0ch]
;
ALIGNCODE(32)
BEGINPROC   rtVccEh4DoFiltering
        push    ebp
        push    ebx
        push    edi
        push    esi

        mov     ecx, [esp + 5 * 4 + 0]      ; pfnFilter
        mov     ebp, [esp + 5 * 4 + 4]      ; pbFrame

        xor     edi, edi
        xor     esi, esi
        xor     edx, edx
        xor     ebx, ebx
        xor     eax, eax

        call    ecx

        pop     esi
        pop     edi
        pop     ebx
        pop     ebp
        ret
ENDPROC   rtVccEh4DoFiltering


;;
; Resumes executing in an __except block (never returns).
;
; @param    pfnHandler  [ebp + 08h]
; @param    pbFrame     [ebp + 0ch]
;
ALIGNCODE(32)
BEGINPROC   rtVccEh4JumpToHandler
        ;
        ; Since we're never returning there is no need to save anything here.  So,
        ; just start by loading parameters into registers.
        ;
        mov     esi, [esp + 1 * 4 + 0]      ; pfnFilter
        mov     ebp, [esp + 1 * 4 + 4]      ; pbFrame

%ifdef WITH_NLG_STUFF
        ;
        ; Notify VS debugger/whatever.
        ;
        mov     eax, esi
        mov     edx, 1
        call    __NLG_Notify
%endif

        ;
        ; Zero all GPRs except for ESP, EBP and ESI.
        ;
        xor     edi, edi
        xor     edx, edx
        xor     ecx, ecx
        xor     ebx, ebx
        xor     eax, eax

        jmp     esi
ENDPROC   rtVccEh4JumpToHandler


;;
; This does global unwinding via RtlUnwind.
;
; The interface kind of requires us to do this from assembly.
;
; @param    pXcptRec    [ebp + 08h]
; @param    pXcptRegRec [ebp + 0ch]
;
ALIGNCODE(32)
BEGINPROC   rtVccEh4DoGlobalUnwind
        push    ebp
        mov     ebp, esp

        ; Save all non-volatile registers.
        push    edi
        push    esi
        push    ebx

        ;
        ; Call unwind function.
        ;
        push    0                   ; ReturnValue
        push    dword [ebp + 08h]   ; ExceptionRecord   - pXcptRec
        push    .return             ; TargetIp
        push    dword [ebp + 0ch]   ; TargetFrame       - pXcptRegRec
        call    IMP(RtlUnwind@16)

        ;
        ; The RtlUnwind code will place us here if it doesn't return the regular way.
        ;
.return:
        ; Restore non-volatile registers.
        pop     ebx
        pop     esi
        pop     edi

        leave
        ret
ENDPROC   rtVccEh4DoGlobalUnwind

