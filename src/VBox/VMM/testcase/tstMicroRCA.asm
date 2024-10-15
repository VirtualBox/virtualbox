; $Id: tstMicroRCA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; tstMicroRCA
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
; SPDX-License-Identifier: GPL-3.0-only
;

;*******************************************************************************
;*      Header Files                                                           *
;*******************************************************************************
%include "VBox/asmdefs.mac"
%include "iprt/x86.mac"
%include "VBox/vmm/cpum.mac"
%include "VBox/err.mac"
%include "VBox/vmm/vm.mac"
%include "tstMicro.mac"


;*******************************************************************************
;*      Defined Constants And Macros                                           *
;*******************************************************************************
;;
; Function prolog which saves everything and loads the first parameter into ebx.
%macro PROLOG 0
    push    ebp
    mov     ebp, esp
    push    ebx
    push    esi
    push    edi
    mov     ebx, [ebp + 8]                      ; pTst
%endm

;;
; Function epilog which saves everything and loads the first parameter into ebx.
%macro EPILOG 0
    pop     edi
    pop     esi
    pop     ebx
    leave
%endm

;;
; Does an rdtsc (trashing edx:eax) and move the result to edi:esi.
%macro RDTSC_EDI_ESI 0
    rdtsc
    xchg    eax, esi
    xchg    edx, edi
%endm

;;
; Does an rdtsc (trashing edx:eax) and move the result to ecx:ebp.
%macro RDTSC_ECX_EBP 0
    rdtsc
    xchg    eax, ebp
    xchg    edx, ecx
%endm

;;
; Saves the result of an instruction profiling operation.
;
; Input is in edi:esi (start) and [ebp + 8] points to TSTMICRO.
; Trashes ebx.
%macro STORE_INSTR_PRF_RESULT 0
    mov     ebx, [ebp + 8]
    mov     [ebx + TSTMICRO.u64TSCR0Start    ], esi
    mov     [ebx + TSTMICRO.u64TSCR0Start + 4], edi
    mov     [ebx + TSTMICRO.u64TSCR0End      ], eax
    mov     [ebx + TSTMICRO.u64TSCR0End   + 4], edx
%endm

;;
; Samples the end time of an instruction profiling operation and
; Saves the result of an instruction profiling operation.
;
; Input is in edi:esi (start) and [ebp + 8] points to TSTMICRO.
; Trashes ebx.
%macro RDTSC_STORE_INSTR_PRF_RESULT 0
    rdtsc
    STORE_INSTR_PRF_RESULT
%endm


;;
; copies the stack to gabStackCopy and saves ESP and EBP in gpESP and gpEBP.
;
; @param    %1      The resume label.
; @param    ebx     TSTMICRO pointer.
; @uses     ecx, edi, esi, flags.
%macro COPY_STACK_ESP_EBP_RESUME 1
    mov     [gpTst], ebx
    mov     [gESPResume], esp
    mov     [gEBPResume], ebp
    mov     dword [gEIPResume], %1

    mov     esi, esp
    and     esi, ~0fffh
    mov     edi, gabStackCopy
    mov     ecx, 01000h / 4
    rep     movsd

%endm


;*******************************************************************************
;*  Global Variables                                                           *
;*******************************************************************************
BEGINDATA
gpTst       dd 0

gESPResume  dd 0
gEBPResume  dd 0
gEIPResume  dd 0

BEGINBSS
gabStackCopy resb  4096

extern NAME(idtOnly42)
extern IMPNAME(g_VM)

BEGINCODE
EXPORTEDNAME tstMicroRCAsmStart


;;
; Check the overhead of doing rdtsc + two xchg operations.
;
BEGINPROC tstOverhead
    PROLOG

    RDTSC_EDI_ESI
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstOverhead


;;
; Invalidate page 0.
;
BEGINPROC tstInvlpg0
    PROLOG

    RDTSC_EDI_ESI
    invlpg  [0]
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstInvlpg0

;;
; Invalidate the current code page.
;
BEGINPROC tstInvlpgEIP
    PROLOG

    RDTSC_EDI_ESI
    invlpg  [NAME(tstInvlpgEIP)]
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstInvlpgEIP


;;
; Invalidate page 0.
;
BEGINPROC tstInvlpgESP
    PROLOG

    RDTSC_EDI_ESI
    invlpg  [esp]
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstInvlpgESP


;;
; cr3 reload sequence.
;
BEGINPROC tstCR3Reload
    PROLOG

    RDTSC_EDI_ESI
    mov     ebx, cr3
    mov     cr3, ebx
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstCR3Reload


;;
; Enable WP sequence.
;
BEGINPROC tstWPEnable
    PROLOG

    RDTSC_EDI_ESI
    mov     ebx, cr0
    or      ebx, X86_CR0_WRITE_PROTECT
    mov     cr0, ebx
    rdtsc
    ; disabled it now or we'll die...
    and     ebx, ~X86_CR0_WRITE_PROTECT
    mov     cr0, ebx
    STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstWPEnable


;;
; Disable WP sequence.
;
BEGINPROC tstWPDisable
    PROLOG

    ;
    mov     ebx, cr0
    or      ebx, X86_CR0_WRITE_PROTECT
    mov     cr0, ebx
    ; just wast a bit of space and time to try avoid the enable bit tainting the results of the disable.
    xor     ebx, ebx
    rdtsc
    add     ebx, eax
    rdtsc
    add     ebx, edx
    rdtsc
    sub     ebx, eax

    RDTSC_EDI_ESI
    mov     ebx, cr0
    and     ebx, ~X86_CR0_WRITE_PROTECT
    mov     cr0, ebx
    RDTSC_STORE_INSTR_PRF_RESULT

    EPILOG
    ret
ENDPROC tstWPDisable




;;
; Generate a #PF accessing page 0 in
;
BEGINPROC tstPFR0
    PROLOG

    COPY_STACK_ESP_EBP_RESUME tstPFR0_Resume

    rdtsc
    mov     [ebx + TSTMICRO.u64TSCR0Start    ], eax
    mov     [ebx + TSTMICRO.u64TSCR0Start + 4], edx
    xor     ebx, ebx                    ; The NULL pointer.
    xor     ecx, ecx
    xor     ebp, ebp                    ; ebp:ecx - Rx enter time (0:0).
    RDTSC_EDI_ESI                       ; edi:esi - Before trap.
    mov     [ebx], ebx                  ; traps - 2 bytes

    RDTSC_EDI_ESI                       ; edi:esi - Rx entry time.
    int     42h                         ; we're done.

tstPFR0_Resume:
    EPILOG
    ret
ENDPROC tstPFR0



;;
; Generate a #PF accessing page 0 in ring-1
;
BEGINPROC_EXPORTED tstPFR1
    PROLOG

    COPY_STACK_ESP_EBP_RESUME tstPFR1_Resume

    ; Setup iret to execute r1 code.
    mov     eax, 02069h                 ; load ds and es with R1 selectors.
    mov     es, eax
    mov     ds, eax
    push    dword 01069h                ; ss
    push    dword [ebx + TSTMICRO.RCPtrStack] ; esp
    push    dword 0000h                 ; eflags
    push    dword 01061h                ; cs
    push    tstPTR1_R1                  ; eip

    rdtsc
    mov     [ebx + TSTMICRO.u64TSCR0Start    ], eax
    mov     [ebx + TSTMICRO.u64TSCR0Start + 4], edx
    iret

    ; R1 code
tstPTR1_R1:
    RDTSC_ECX_EBP                       ; ebp:ecx - Rx enter time (0:0).
    xor     ebx, ebx
    RDTSC_EDI_ESI                       ; edi:esi - Before trap.
    mov     [ebx], ebx                  ; traps - 2 bytes

    RDTSC_EDI_ESI                       ; edi:esi - Rx entry time.
    int     42h                         ; we're done.

    ; Resume in R0
tstPFR1_Resume:
    EPILOG
    ret
ENDPROC tstPFR1


;;
; Generate a #PF accessing page 0 in ring-2
;
BEGINPROC_EXPORTED tstPFR2
    PROLOG

    COPY_STACK_ESP_EBP_RESUME tstPFR2_Resume

    ; Setup iret to execute r2 code.
    mov     eax, 0206ah                 ; load ds and es with R2 selectors.
    mov     es, eax
    mov     ds, eax
    push    0206ah                      ; ss
    push    dword [ebx + TSTMICRO.RCPtrStack] ; esp
    push    dword 0000h                 ; eflags
    push    02062h                      ; cs
    push    tstPTR2_R2                  ; eip

    rdtsc
    mov     [ebx + TSTMICRO.u64TSCR0Start    ], eax
    mov     [ebx + TSTMICRO.u64TSCR0Start + 4], edx
    iret

    ; R2 code
tstPTR2_R2:
    RDTSC_ECX_EBP                       ; ebp:ecx - Rx enter time (0:0).
    xor     ebx, ebx
    RDTSC_EDI_ESI                       ; edi:esi - Before trap.
    mov     [ebx], ebx                  ; traps - 2 bytes

    RDTSC_EDI_ESI                       ; edi:esi - Rx entry time.
    int     42h                         ; we're done.

    ; Resume in R0
tstPFR2_Resume:
    EPILOG
    ret
ENDPROC tstPFR2


;;
; Generate a #PF accessing page 0 in ring-3
;
BEGINPROC_EXPORTED tstPFR3
    PROLOG

    COPY_STACK_ESP_EBP_RESUME tstPFR3_Resume

    ; Setup iret to execute r3 code.
    mov     eax, 0306bh                 ; load ds and es with R3 selectors.
    mov     es, eax
    mov     ds, eax
    push    0306bh                      ; ss
    push    dword [ebx + TSTMICRO.RCPtrStack] ; esp
    push    dword 0000h                 ; eflags
    push    03063h                      ; cs
    push    tstPTR3_R3                  ; eip

    rdtsc
    mov     [ebx + TSTMICRO.u64TSCR0Start    ], eax
    mov     [ebx + TSTMICRO.u64TSCR0Start + 4], edx
    iret

    ; R3 code
tstPTR3_R3:
    RDTSC_ECX_EBP                       ; ebp:ecx - Rx enter time (0:0).
    xor     ebx, ebx
    RDTSC_EDI_ESI                       ; edi:esi - Before trap.
    mov     [ebx], ebx                  ; traps - 2 bytes

    RDTSC_EDI_ESI                       ; edi:esi - Rx entry time.
    int     42h                         ; we're done.

    ; Resume in R0
tstPFR3_Resume:
    EPILOG
    ret
ENDPROC tstPFR3



;;
; Trap handler with error code - share code with tstTrapHandler.
align 8
BEGINPROC_EXPORTED tstTrapHandlerNoErr
    rdtsc
    push    0ffffffffh
    jmp     tstTrapHandler_Common

;;
; Trap handler with error code.
;           14  SS          (only if ring transition.)
;           10  ESP         (only if ring transition.)
;            c  EFLAGS
;            8  CS
;            4  EIP
;            0  Error code. (~0 for vectors which don't take an error code.)
;; @todo This is a bit of a mess - clean up!
align 8
BEGINPROC tstTrapHandler
    ; get the time
    rdtsc

tstTrapHandler_Common:
    xchg    ecx, eax
    mov     eax, -1                         ; return code

    ; disable WP
    mov     ebx, cr0
    and     ebx, ~X86_CR0_WRITE_PROTECT
    mov     cr0, ebx

    ; first hit, or final hit?
    mov     ebx, [gpTst]
    inc     dword [ebx + TSTMICRO.cHits]
    cmp     dword [ebx + TSTMICRO.cHits], byte 1
    jne     near tstTrapHandler_Fault

    ; save the results - edx:ecx == r0 enter time, edi:esi == before trap, ecx:ebp == Rx enter time.

    mov     [ebx + TSTMICRO.u64TSCR0Enter    ], ecx
    mov     [ebx + TSTMICRO.u64TSCR0Enter + 4], edx

    ;mov     [ebx + TSTMICRO.u64TSCRxStart    ], ecx
    ;mov     [ebx + TSTMICRO.u64TSCRxStart + 4], ebp

    mov     [ebx + TSTMICRO.u64TSCRxStart    ], esi
    mov     [ebx + TSTMICRO.u64TSCRxStart + 4], edi

    mov     eax, cr2
    mov     [ebx + TSTMICRO.u32CR2], eax
    mov     eax, [esp + 0]
    mov     [ebx + TSTMICRO.u32ErrCd], eax
    mov     eax, [esp + 4]
    mov     [ebx + TSTMICRO.u32EIP], eax

    ;
    ; Advance the EIP and resume.
    ;
    mov     ecx, [ebx + TSTMICRO.offEIPAdd]
    add     [esp + 4], ecx              ; return eip + offEIPAdd

    add     esp, byte 4                 ; skip the err code

    ; take the timestamp before resuming.
    rdtsc
    mov     [ebx + TSTMICRO.u64TSCR0Exit    ], eax
    mov     [ebx + TSTMICRO.u64TSCR0Exit + 4], edx
    iret


tstTrapHandler_Fault:
    cld

%if 0 ; this has been broken for quite some time
    ;
    ; Setup CPUMCTXCORE frame
    ;
    push    dword [esp +  4h +  0h]     ; 3ch - eip
    push    dword [esp + 0ch +  4h]     ; 38h - eflags
    ;;;;push    dword [esp + 08h +  8h]     ; 34h - cs
    push    cs;want disasm
    push    ds                ; c       ; 30h
    push    es                ;10       ; 2ch
    push    fs                ;14       ; 28h
    push    gs                ;18       ; 24h
    push    dword [esp + 14h + 1ch]     ; 20h - ss
    push    dword [esp + 10h + 20h]     ; 1ch - esp
    push    ecx               ;24       ; 18h
    push    edx               ;28       ; 14h
    push    ebx               ;2c       ; 10h
    push    eax               ;30       ;  ch
    push    ebp               ;34       ;  8h
    push    esi               ;38       ;  4h
    push    edi               ;3c       ;  0h
                              ;40
%endif

    test    byte [esp + 0ch +  4h], 3h ; check CPL of the cs selector
    jmp short tstTrapHandler_Fault_Hyper ;; @todo
    jz short tstTrapHandler_Fault_Hyper
tstTrapHandler_Fault_Guest:
    mov     ecx, esp
    mov     edx, IMP(g_VM)
    mov     eax, VERR_TRPM_DONT_PANIC
    call    [edx + VM.pfnVMMRCToHostAsm]
    jmp short tstTrapHandler_Fault_Guest

tstTrapHandler_Fault_Hyper:
    ; fix ss:esp.
    lea     ebx, [esp + 14h + 040h]     ; calc esp at trap
    mov     [esp + CPUMCTXCORE.esp], ebx; update esp in register frame
    mov     [esp + CPUMCTXCORE.ss.Sel], ss  ; update ss in register frame

    mov     ecx, esp
    mov     edx, IMP(g_VM)
    mov     eax, VERR_TRPM_DONT_PANIC
    call    [edx + VM.pfnVMMRCToHostAsm]
    jmp short tstTrapHandler_Fault_Hyper

BEGINPROC tstInterrupt42
    rdtsc
    push    byte 0
    mov     ecx, eax                    ; low ts
    xor     eax, eax                    ; return code.

    ; save the results - edx:ecx == r0 end time, edi:esi == Rx end time.
    mov     [ebx + TSTMICRO.u64TSCR0End    ], ecx
    mov     [ebx + TSTMICRO.u64TSCR0End + 4], edx

    mov     [ebx + TSTMICRO.u64TSCRxEnd    ], esi
    mov     [ebx + TSTMICRO.u64TSCRxEnd + 4], edi

    ;
    ; Restore the IDT and stack, and resume the testcase code.
    ;
    lidt    [ebx + TSTMICRO.OriginalIDTR]

    mov     edi, esp
    and     edi, ~0fffh
    mov     esi, gabStackCopy
    mov     ecx, 01000h / 4
    mov     esp, [gESPResume]
    mov     ebp, [gEBPResume]
    rep movsd

    jmp     [gEIPResume]

ENDPROC tstTrapHandler

EXPORTEDNAME tstMicroRCAsmEnd
