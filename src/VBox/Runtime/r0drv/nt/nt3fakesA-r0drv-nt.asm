; $Id: nt3fakesA-r0drv-nt.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Companion to nt3fakes-r0drv-nt.cpp that provides import stuff to satisfy the linker.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

%undef NAME
%define NAME(name) NAME_OVERLOAD(name)

BEGINCODE

;;
; Called from rtR0Nt3InitSymbols after symbols have been resolved.
BEGINPROC _rtNt3InitSymbolsAssembly
        push    ebp
        mov     ebp, esp

;;
; @param 1  The fastcall name.
; @param 2  Byte size of arguments.
%macro DefineImportDataAndInitCode 3
extern          $%1 %+ Nt3Fb_ %+ %2 %+ @ %+ %3
BEGINDATA
extern          _g_pfnrt %+ %2
GLOBALNAME __imp_ %+ %1 %+ %2 %+ @ %+ %3
        dd      $%1 %+ Nt3Fb_ %+ %2 %+ @ %+ %3
BEGINCODE
        mov     eax, [_g_pfnrt %+ %2]
        test    eax, eax
        jz      %%next
        mov     [__imp_ %+ %1 %+ %2 %+ @ %+ %3], eax
%%next:
%endmacro

        DefineImportDataAndInitCode _,PsGetVersion, 16
        DefineImportDataAndInitCode _,ZwQuerySystemInformation, 16
        DefineImportDataAndInitCode _,KeSetTimerEx, 20
        DefineImportDataAndInitCode _,IoAttachDeviceToDeviceStack, 8
        DefineImportDataAndInitCode _,PsGetCurrentProcessId, 0
        DefineImportDataAndInitCode _,ZwYieldExecution, 0
        DefineImportDataAndInitCode @,ExAcquireFastMutex, 4
        DefineImportDataAndInitCode @,ExReleaseFastMutex, 4

        xor     eax, eax
        leave
        ret
ENDPROC _rtNt3InitSymbolsAssembly


;;
; @param 1  The fastcall name.
; @param 2  The stdcall name.
; @param 3  Byte size of arguments.
; @param 4  Zero if 1:1 mapping;
;           One if 2nd parameter is a byte pointer that the farcall version
;           instead returns in al.
%macro FastOrStdCallWrapper 4
BEGINCODE
extern _g_pfnrt %+ %1
extern _g_pfnrt %+ %2
BEGINPROC_EXPORTED $@ %+ %1 %+ @ %+ %3
        mov     eax, [_g_pfnrt %+ %1]
        cmp     eax, 0
        jnz     .got_fast_call
        mov     eax, .stdcall_wrapper
        mov     [__imp_@ %+ %1 %+ @ %+ %3], eax

.stdcall_wrapper:
        push    ebp
        mov     ebp, esp
%if %4 == 1
        push    dword 0
        push    esp
%else
        push    edx
%endif
        push    ecx
        call    [_g_pfnrt %+ %2]
%if %4 == 1
        movzx   eax, byte [ebp - 4]
%endif
        leave
        ret

.got_fast_call:
        mov     [__imp_@ %+ %1 %+ @ %+ %3], eax
        jmp     eax
ENDPROC $@ %+ %1 %+ @ %+ %3

BEGINDATA
GLOBALNAME __imp_@ %+ %1 %+ @ %+ %3
        dd       $@ %+ %1 %+ @ %+ %3
%endmacro

FastOrStdCallWrapper IofCompleteRequest,            IoCompleteRequest,              8, 0
FastOrStdCallWrapper IofCallDriver,                 IoCallDriver,                   8, 0
FastOrStdCallWrapper ObfDereferenceObject,          ObDereferenceObject,            4, 0
FastOrStdCallWrapper KfAcquireSpinLock,             KeAcquireSpinLock,              4, 1
FastOrStdCallWrapper KfReleaseSpinLock,             KeReleaseSpinLock,              8, 0
FastOrStdCallWrapper KfRaiseIrql,                   KeRaiseIrql,                    4, 1
FastOrStdCallWrapper KfLowerIrql,                   KeLowerIrql,                    4, 0
FastOrStdCallWrapper KefAcquireSpinLockAtDpcLevel,  KeAcquireSpinLockAtDpcLevel,    4, 0
FastOrStdCallWrapper KefReleaseSpinLockFromDpcLevel,KeReleaseSpinLockFromDpcLevel,  4, 0


BEGINCODE
; LONG FASTCALL InterlockedExchange(LONG volatile *,LONG );
BEGINPROC_EXPORTED $@InterlockedExchange@8
        mov     eax, edx
        xchg    [ecx], eax
        ret

BEGINDATA
GLOBALNAME __imp_@InterlockedExchange@8
        dd      $@InterlockedExchange@8


BEGINDATA
GLOBALNAME __imp__KeTickCount
GLOBALNAME _KeTickCount
        dd      0

