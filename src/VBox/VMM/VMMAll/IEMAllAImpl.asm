; $Id: IEMAllAImpl.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IEM - Instruction Implementation in Assembly.
;

;
; Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "VBox/asmdefs.mac"
%include "VBox/err.mac"
%include "iprt/x86.mac"


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************

;;
; This is handy for generating absolutly correct EFLAGS.
;%define IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS


;;
; RET XX / RET wrapper for fastcall.
;
%macro RET_FASTCALL 1
%ifdef RT_ARCH_X86
 %ifdef RT_OS_WINDOWS
    ret %1
 %else
    ret
 %endif
%else
    ret
%endif
%endmacro

;;
; NAME for fastcall functions.
;
;; @todo 'global @fastcall@12' is still broken in yasm and requires dollar
;         escaping (or whatever the dollar is good for here).  Thus the ugly
;         prefix argument.
;
%define NAME_FASTCALL(a_Name, a_cbArgs, a_Prefix)   NAME(a_Name)
%ifdef RT_ARCH_X86
 %ifdef RT_OS_WINDOWS
  %undef NAME_FASTCALL
  %define NAME_FASTCALL(a_Name, a_cbArgs, a_Prefix) a_Prefix %+ a_Name %+ @ %+ a_cbArgs
 %endif
%endif

;;
; BEGINPROC for fastcall functions.
;
; @param        1       The function name (C).
; @param        2       The argument size on x86.
;
%macro BEGINPROC_FASTCALL 2
GLOBALNAME_RAW NAME_FASTCALL(%1,%2,@), function, hidden
        IBT_ENDBRxx
%endmacro


;
; We employ some macro assembly here to hid the calling convention differences.
;
%ifdef RT_ARCH_AMD64
 %macro PROLOGUE_1_ARGS 0
 %endmacro
 %macro EPILOGUE_1_ARGS 0
        ret
 %endmacro
 %macro EPILOGUE_1_ARGS_EX 0
        ret
 %endmacro

 %macro PROLOGUE_2_ARGS 0
 %endmacro
 %macro EPILOGUE_2_ARGS 0
        ret
 %endmacro
 %macro EPILOGUE_2_ARGS_EX 1
        ret
 %endmacro

 %macro PROLOGUE_3_ARGS 0
 %endmacro
 %macro EPILOGUE_3_ARGS 0
        ret
 %endmacro
 %macro EPILOGUE_3_ARGS_EX 1
        ret
 %endmacro

 %macro PROLOGUE_4_ARGS 0
 %endmacro
 %macro EPILOGUE_4_ARGS 0
        ret
 %endmacro
 %macro EPILOGUE_4_ARGS_EX 1
        ret
 %endmacro

 %ifdef ASM_CALL64_GCC
  %define A0        rdi
  %define A0_32     edi
  %define A0_16     di
  %define A0_8      dil

  %define A1        rsi
  %define A1_32     esi
  %define A1_16     si
  %define A1_8      sil

  %define A2        rdx
  %define A2_32     edx
  %define A2_16     dx
  %define A2_8      dl

  %define A3        rcx
  %define A3_32     ecx
  %define A3_16     cx
  %define A3_8      cl
 %endif

 %ifdef ASM_CALL64_MSC
  %define A0        rcx
  %define A0_32     ecx
  %define A0_16     cx
  %define A0_8      cl

  %define A1        rdx
  %define A1_32     edx
  %define A1_16     dx
  %define A1_8      dl

  %define A2        r8
  %define A2_32     r8d
  %define A2_16     r8w
  %define A2_8      r8b

  %define A3        r9
  %define A3_32     r9d
  %define A3_16     r9w
  %define A3_8      r9b
 %endif

 %define T0         rax
 %define T0_32      eax
 %define T0_16      ax
 %define T0_8       al

 %define T1         r11
 %define T1_32      r11d
 %define T1_16      r11w
 %define T1_8       r11b

 %define T2         r10                 ; only AMD64
 %define T2_32      r10d
 %define T2_16      r10w
 %define T2_8       r10b

 ;
 ; Return value, same as T0 but to make it more obvious
 ; that this is a return value.
 ;
 %define R0         rax
 %define R0_32      eax
 %define R0_16      ax
 %define R0_8       al

%else
 ; x86
 %macro PROLOGUE_1_ARGS 0
        push    edi
 %endmacro
 %macro EPILOGUE_1_ARGS 0
        pop     edi
        ret     0
 %endmacro
 %macro EPILOGUE_1_ARGS_EX 1
        pop     edi
        ret     %1
 %endmacro

 %macro PROLOGUE_2_ARGS 0
        push    edi
 %endmacro
 %macro EPILOGUE_2_ARGS 0
        pop     edi
        ret     0
 %endmacro
 %macro EPILOGUE_2_ARGS_EX 1
        pop     edi
        ret     %1
 %endmacro

 %macro PROLOGUE_3_ARGS 0
        push    ebx
        mov     ebx, [esp + 4 + 4]
        push    edi
 %endmacro
 %macro EPILOGUE_3_ARGS_EX 1
  %if (%1) < 4
   %error "With three args, at least 4 bytes must be remove from the stack upon return (32-bit)."
  %endif
        pop     edi
        pop     ebx
        ret     %1
 %endmacro
 %macro EPILOGUE_3_ARGS 0
        EPILOGUE_3_ARGS_EX 4
 %endmacro

 %macro PROLOGUE_4_ARGS 0
        push    ebx
        push    edi
        push    esi
        mov     ebx, [esp + 12 + 4 + 0]
        mov     esi, [esp + 12 + 4 + 4]
 %endmacro
 %macro EPILOGUE_4_ARGS_EX 1
  %if (%1) < 8
   %error "With four args, at least 8 bytes must be remove from the stack upon return (32-bit)."
  %endif
        pop     esi
        pop     edi
        pop     ebx
        ret     %1
 %endmacro
 %macro EPILOGUE_4_ARGS 0
        EPILOGUE_4_ARGS_EX 8
 %endmacro

 %define A0         ecx
 %define A0_32      ecx
 %define A0_16       cx
 %define A0_8        cl

 %define A1         edx
 %define A1_32      edx
 %define A1_16      dx
 %define A1_8       dl

 %define A2         ebx
 %define A2_32      ebx
 %define A2_16      bx
 %define A2_8       bl

 %define A3         esi
 %define A3_32      esi
 %define A3_16      si

 %define T0         eax
 %define T0_32      eax
 %define T0_16      ax
 %define T0_8       al

 %define T1         edi
 %define T1_32      edi
 %define T1_16      di
%endif


;;
; Load the relevant flags from [%1] if there are undefined flags (%3).
;
; @remarks      Clobbers T0, stack. Changes EFLAGS.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       The set of modified flags.
; @param        3       The set of undefined flags.
; @param        4       The flags that must be loaded.
;
%macro IEM_MAYBE_LOAD_FLAGS 4
 %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf                                                   ; store current flags
        mov     T0_32, %1                                       ; load the guest flags
        and     dword [xSP], ~(%2 | %3 | X86_EFL_STATUS_BITS)   ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified and undefined flags.
        or      [xSP], T0                                       ; merge guest flags with host flags.
        popf                                                    ; load the mixed flags.

 %elif (%3 + %4) != 0
  %if 1        ; This approach seems faster on intel 10980XE
   %if (%3 | %4) == X86_EFL_CF
        ; Use bt to load bit into CF
        bt      %1, X86_EFL_CF_BIT
   %else
        ; Use ADD to set OF and SHAF for the rest. ASSUMES T0_32 is eax!
        mov     eax, %1
    %if (%3 | %4) == X86_EFL_OF
        ; Use ADD to set OF.
        shl     eax, 31 - X86_EFL_OF_BIT
        add     eax, 80000000h
    %elif ((%3 | %4) & X86_EFL_OF) != 0
        ; Use ADD to set OF.
        xchg    al, ah
        shl     al, 15 - X86_EFL_OF_BIT
        add     al, 80h
        ; Use SAHF to set the other status flags.
        sahf
    %else ; OF not needed; so al -> ah and load ah into eflags.
     %if 1 ; Pretty similar on 10980XE, but shl seems faster on average.
        shl     eax, 8
     %else
        xchg    al, ah
     %endif
        sahf
    %endif
   %endif

  %else
        pushf                           ; store current flags
        mov     T0_32, %1               ; load the guest flags
        and     dword [xSP], ~(%2 | %3) ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
        or      [xSP], T0               ; merge guest flags with host flags.
        popf                            ; load the mixed flags.
  %endif
 %endif
%endmacro

;;
; Load the relevant flags from [%1].
;
; @remarks      Clobbers T0, stack. Changes EFLAGS.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       The set of flags to load.
; @param        3       The set of undefined flags.
;
%macro IEM_LOAD_FLAGS 3
 %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf                                                   ; store current flags
        mov     T0_32, %1                                       ; load the guest flags
        and     dword [xSP], ~(%2 | %3 | X86_EFL_STATUS_BITS)   ; mask out the modified, undefined and status flags
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified, undefined and status flags.
        or      [xSP], T0                                       ; merge guest flags with host flags.
        popf                                                    ; load the mixed flags.

 %elif 1        ; This approach seems faster on intel 10980XE
  %if (%3 | %2) == X86_EFL_CF
        ; Use bt to load bit into CF
        bt      %1, X86_EFL_CF_BIT
  %else
        mov     eax, %1                   ; ASSUMES T0_32 is eax!!
   %if (%3 | %2) == X86_EFL_OF
        ; Use ADD to set OF.
        shl     eax, 31 - X86_EFL_OF_BIT
        add     eax, 80000000h
   %elif ((%3 | %2) & X86_EFL_OF) != 0
        ; Use ADD to set OF.
        xchg    al, ah
        shl     al, 15 - X86_EFL_OF_BIT
        add     al, 80h
        ; Use SAHF to set the other status flags.
        sahf
   %else ; OF not needed; so al -> ah and load ah into eflags.
    %if 1 ; Pretty similar on 10980XE, but shl seems faster on average.
        shl     eax, 8
    %else
        xchg    al, ah
    %endif
        sahf
   %endif
  %endif ; (%3 | %2) != X86_EFL_CF

 %else
        pushf                           ; store current flags
        mov     T0_32, %1               ; load the guest flags
        and     dword [xSP], ~(%2 | %3) ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
        or      [xSP], T0               ; merge guest flags with host flags.
        popf                            ; load the mixed flags.
 %endif
%endmacro

;;
; Merge incoming guest EFLAGS (%1) with host EFLAGS into EAX (T0).
;
; @remarks  Clobbers T0, T1, %1, stack.
; @param        1       The parameter (A0..A3) holding the OLD eflags value. Clobbered.
; @param        2       The mask of modified flags to save.
; @param        3       The mask of undefined flags to (maybe) save.
; @param        4       The mask of flags that are zeroed (and thus doesn't require loading, just clearing)
;
%macro IEM_SAVE_FLAGS_RETVAL 4 0
 %if (%2 | %3 | %4) != 0
        mov     T1_32, %1                                       ; flags
  %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf
        pop     T0
        and     %1, ~(%2 | %3 | %4 | X86_EFL_STATUS_BITS)       ; clear the modified & undefined & zeroed & status flags.
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified, undefined and status flags.
  %else
   %if (%2 | %3 | %4) == X86_EFL_CF
        setc    T0_8
   %elif (%2 | %3) == X86_EFL_OF
        seto    T0_8
        shl     T0_32, X86_EFL_OF_BIT
   %elif (%2 | %3) == X86_EFL_ZF
        setz    T0_8                    ; On 10980XE this is faster than the next option 5596 vs 5936 ps/call (cmpxchg8b-positive).
        shl     T0_32, X86_EFL_ZF_BIT
   %elif (%2 | %3) <= 0xff
        lahf
        movzx   eax, ah                 ; ASSUMES T0_32 is eax!
   %elif 1                              ; The locked functions are generally faster on 10980XE with this approach
        lahf                            ; while there seems only to be a tiny advantage in most other test.
        movzx   eax, ah                 ; ASSUMES T0_32 is eax!
        jno     .of_is_clear
        or      eax, X86_EFL_OF
.of_is_clear:
   %else
        pushf                           ; this is a bit slow
        pop     T0
   %endif
        and     %1, ~(%2 | %3 | %4)     ; clear the modified & undefined & zeroed flags.
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
  %endif
        or      T0_32, %1               ; combine the flags. ASSUMES T0 = eax!
        ;mov     %1, T0_32               ; save the flags.
 %else
        mov     T0_32, %1
 %endif
%endmacro

;;
; Calculates the new EFLAGS based on the CPU EFLAGS and fixed clear and set bit masks.
;
; @remarks  Clobbers T0, T1, stack.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       The mask of modified flags to save.
; @param        3       Mask of additional flags to always clear
; @param        4       Mask of additional flags to always set.
;
;; @todo make it stuff the result into EAX?
%macro IEM_SAVE_AND_ADJUST_FLAGS 4
 %if (%2 | %3 | %4) != 0
        pushf
        pop     T1
        mov     T0_32, %1               ; load flags.
        and     T0_32, ~(%2 | %3)       ; clear the modified and always cleared flags.
        and     T1_32, (%2)             ; select the modified flags.
        or      T0_32, T1_32            ; combine the flags.
  %if (%4) != 0
        or      T0_32, %4               ; add the always set flags.
  %endif
        mov     %1, T0_32               ; save the result.
 %endif
%endmacro

;;
; Calculates the new EFLAGS based on the CPU EFLAGS (%2), a clear mask (%3),
; signed input (%4[%5]) and parity index (%6), storing the result into EAX (T0).
;
; @note     %4 & %6 must not be RAX, EAX, or AX! So, don't use with full MUL/IMUL.

; @remarks  Clobbers T0, T1, stack, %6, EFLAGS, %1.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       The mask of modified flags to save.
; @param        3       Mask of additional flags to always clear
; @param        4       The result register to set SF by.
; @param        5       The width of the %4 register in bits (8, 16, 32, or 64).
; @param        6       The (full) register containing the parity table index. Will be modified!
%macro IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL 6
        pushf
        pop     T0
        and     %1, ~(%2 | %3 | X86_EFL_PF | X86_EFL_SF)  ; clear the modified, always cleared flags and the two flags we calc.
        and     T0_32, (%2)             ; select the modified flags.
        or      T0_32, %1               ; combine the flags.

        ; First calculate SF as it is the same register as %6 (only %6 is always full width).
        bt      %4, %5 - 1
        jnc     %%sf_clear
        or      T0_32, X86_EFL_SF
 %%sf_clear:

        ; Parity last.
        and     %6, 0xff
 %ifdef RT_ARCH_AMD64
        lea     T1, [NAME(g_afParity) xWrtRIP]
        or      T0_8, [T1 + %6]
 %else
        or      T0_8, [NAME(g_afParity) + %6]
 %endif

        ;mov     %1, T0_32               ; save the result.
        ; ASSUMES T0 = eax!
%endmacro

;;
; Calculates the new EFLAGS using fixed clear and set bit masks.
;
; @remarks  Clobbers/returns T0.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       Mask of additional flags to always clear
; @param        3       Mask of additional flags to always set.
;
%macro IEM_ADJUST_FLAGS_RETVAL 3
        mov     T0_32, %1               ; Load flags. ASSUMES T0 is EAX!
 %if (%2 | %3) != 0
  %if (%2) != 0
        and     T0_32, ~(%2)            ; Remove the always cleared flags.
  %endif
  %if (%3) != 0
        or      T0_32, %3               ; Add the always set flags.
  %endif
 %endif
%endmacro

;;
; Calculates the new EFLAGS using fixed clear and set bit masks.
;
; @remarks  Clobbers T0, %4, EFLAGS.
; @param        1       The parameter (A0..A3) holding the eflags value.
; @param        2       Mask of additional flags to always clear
; @param        3       Mask of additional flags to always set.
; @param        4       The (full) register containing the parity table index. Will be modified!
;
%macro IEM_ADJUST_FLAGS_WITH_PARITY 4
        mov     T0_32, %1               ; Load flags.
        and     T0_32, ~(%2 | X86_EFL_PF) ; Remove PF and the always cleared flags.
 %if (%3) != 0
        or      T0_32, %3               ; Add the always set flags.
 %endif
        and     %4, 0xff
 %ifdef RT_ARCH_AMD64
        lea     T2, [NAME(g_afParity) xWrtRIP]
        or      T0_8, [T2 + %4]
 %else
        or      T0_8, [NAME(g_afParity) + %4]
 %endif
        mov     %1, T0_32               ; Save the result.
%endmacro


;;;; OLD EFLAGS macros.
;;;; OLD EFLAGS macros.
;;;; OLD EFLAGS macros.
;;;; OLD EFLAGS macros.
;;;; OLD EFLAGS macros.

;;
; Load the relevant flags from [%1] if there are undefined flags (%3).
;
; @remarks      Clobbers T0, stack. Changes EFLAGS.
; @param        1       The parameter (A0..A3) pointing to the eflags.
; @param        2       The set of modified flags.
; @param        3       The set of undefined flags.
; @param        4       The flags that must be loaded.
;
%macro IEM_MAYBE_LOAD_FLAGS_OLD 4
 %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf                                                   ; store current flags
        mov     T0_32, [%1]                                     ; load the guest flags
        and     dword [xSP], ~(%2 | %3 | X86_EFL_STATUS_BITS)   ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified and undefined flags.
        or      [xSP], T0                                       ; merge guest flags with host flags.
        popf                                                    ; load the mixed flags.

 %elif (%3 + %4) != 0
  %if 1        ; This approach seems faster on intel 10980XE
   %if (%3 | %4) == X86_EFL_CF
        ; Use bt to load bit into CF
        bt      dword [%1], X86_EFL_CF_BIT
   %else
        ; Use ADD to set OF and SHAF for the rest. ASSUMES T0_32 is eax!
        mov     eax, [%1]
    %if (%3 | %4) == X86_EFL_OF
        ; Use ADD to set OF.
        shl     eax, 31 - X86_EFL_OF_BIT
        add     eax, 80000000h
    %elif ((%3 | %4) & X86_EFL_OF) != 0
        ; Use ADD to set OF.
        xchg    al, ah
        shl     al, 15 - X86_EFL_OF_BIT
        add     al, 80h
        ; Use SAHF to set the other status flags.
        sahf
    %else ; OF not needed; so al -> ah and load ah into eflags.
     %if 1 ; Pretty similar on 10980XE, but shl seems faster on average.
        shl     eax, 8
     %else
        xchg    al, ah
     %endif
        sahf
    %endif
   %endif

  %else
        pushf                           ; store current flags
        mov     T0_32, [%1]             ; load the guest flags
        and     dword [xSP], ~(%2 | %3) ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
        or      [xSP], T0               ; merge guest flags with host flags.
        popf                            ; load the mixed flags.
  %endif
 %endif
%endmacro

;;
; Load the relevant flags from [%1].
;
; @remarks      Clobbers T0, stack. Changes EFLAGS.
; @param        1       The parameter (A0..A3) pointing to the eflags.
; @param        2       The set of flags to load.
; @param        3       The set of undefined flags.
;
%macro IEM_LOAD_FLAGS_OLD 3
 %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf                                                   ; store current flags
        mov     T0_32, [%1]                                     ; load the guest flags
        and     dword [xSP], ~(%2 | %3 | X86_EFL_STATUS_BITS)   ; mask out the modified, undefined and status flags
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified, undefined and status flags.
        or      [xSP], T0                                       ; merge guest flags with host flags.
        popf                                                    ; load the mixed flags.

 %elif 1        ; This approach seems faster on intel 10980XE
  %if (%3 | %2) == X86_EFL_CF
        ; Use bt to load bit into CF
        bt      dword [%1], X86_EFL_CF_BIT
  %else
        mov     eax, [%1]                 ; ASSUMES T0_32 is eax!!
   %if (%3 | %2) == X86_EFL_OF
        ; Use ADD to set OF.
        shl     eax, 31 - X86_EFL_OF_BIT
        add     eax, 80000000h
   %elif ((%3 | %2) & X86_EFL_OF) != 0
        ; Use ADD to set OF.
        xchg    al, ah
        shl     al, 15 - X86_EFL_OF_BIT
        add     al, 80h
        ; Use SAHF to set the other status flags.
        sahf
   %else ; OF not needed; so al -> ah and load ah into eflags.
    %if 1 ; Pretty similar on 10980XE, but shl seems faster on average.
        shl     eax, 8
    %else
        xchg    al, ah
    %endif
        sahf
   %endif
  %endif ; (%3 | %2) != X86_EFL_CF

 %else
        pushf                           ; store current flags
        mov     T0_32, [%1]             ; load the guest flags
        and     dword [xSP], ~(%2 | %3) ; mask out the modified and undefined flags
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
        or      [xSP], T0               ; merge guest flags with host flags.
        popf                            ; load the mixed flags.
 %endif
%endmacro

;;
; Update the flag.
;
; @remarks  Clobbers T0, T1, stack.
; @param        1       The register pointing to the EFLAGS.
; @param        2       The mask of modified flags to save.
; @param        3       The mask of undefined flags to (maybe) save.
; @param        4       The mask of flags that are zeroed (and thus doesn't require loading, just clearing)
;
%macro IEM_SAVE_FLAGS_OLD 4 0
 %if (%2 | %3 | %4) != 0
        mov     T1_32, [%1]                                     ; flags
  %ifdef IEM_AIMPL_WITH_LOAD_AND_SAVE_ALL_STATUS_FLAGS
        pushf
        pop     T0
        and     T1_32, ~(%2 | %3 | %4 | X86_EFL_STATUS_BITS)    ; clear the modified & undefined & zeroed & status flags.
        and     T0_32, (%2 | %3 | X86_EFL_STATUS_BITS)          ; select the modified, undefined and status flags.
  %else
   %if (%2 | %3 | %4) == X86_EFL_CF
        setc    T0_8
   %elif (%2 | %3) == X86_EFL_OF
        seto    T0_8
        shl     T0_32, X86_EFL_OF_BIT
   %elif (%2 | %3) == X86_EFL_ZF
        setz    T0_8                    ; On 10980XE this is faster than the next option 5596 vs 5936 ps/call (cmpxchg8b-positive).
        shl     T0_32, X86_EFL_ZF_BIT
   %elif (%2 | %3) <= 0xff
        lahf
        movzx   eax, ah                 ; ASSUMES T0_32 is eax!
   %elif 1                              ; The locked functions are generally faster on 10980XE with this approach
        lahf                            ; while there seems only to be a tiny advantage in most other test.
        movzx   eax, ah                 ; ASSUMES T0_32 is eax!
        jno     .of_is_clear
        or      eax, X86_EFL_OF
.of_is_clear:
   %else
        pushf                           ; this is a bit slow
        pop     T0
   %endif
        and     T1_32, ~(%2 | %3 | %4)  ; clear the modified & undefined & zeroed flags.
        and     T0_32, (%2 | %3)        ; select the modified and undefined flags.
  %endif
        or      T0_32, T1_32            ; combine the flags.
        mov     [%1], T0_32             ; save the flags.
 %endif
%endmacro

;;
; Calculates the new EFLAGS based on the CPU EFLAGS and fixed clear and set bit masks.
;
; @remarks  Clobbers T0, T1, stack.
; @param        1       The register pointing to the EFLAGS.
; @param        2       The mask of modified flags to save.
; @param        3       Mask of additional flags to always clear
; @param        4       Mask of additional flags to always set.
;
%macro IEM_SAVE_AND_ADJUST_FLAGS_OLD 4
 %if (%2 | %3 | %4) != 0
        pushf
        pop     T1
        mov     T0_32, [%1]             ; load flags.
        and     T0_32, ~(%2 | %3)       ; clear the modified and always cleared flags.
        and     T1_32, (%2)             ; select the modified flags.
        or      T0_32, T1_32            ; combine the flags.
  %if (%4) != 0
        or      T0_32, %4               ; add the always set flags.
  %endif
        mov     [%1], T0_32             ; save the result.
 %endif
%endmacro

;;
; Calculates the new EFLAGS based on the CPU EFLAGS (%2), a clear mask (%3),
; signed input (%4[%5]) and parity index (%6).
;
; This is used by MUL and IMUL, where we got result (%4 & %6) in xAX which is
; also T0. So, we have to use T1 for the EFLAGS calculation and save T0/xAX
; while we extract the %2 flags from the CPU EFLAGS or use T2 (only AMD64).
;
; @remarks  Clobbers T0, T1, stack, %6, EFLAGS.
; @param        1       The register pointing to the EFLAGS.
; @param        2       The mask of modified flags to save.
; @param        3       Mask of additional flags to always clear
; @param        4       The result register to set SF by.
; @param        5       The width of the %4 register in bits (8, 16, 32, or 64).
; @param        6       The (full) register containing the parity table index. Will be modified!

%macro IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_OLD 6
 %ifdef RT_ARCH_AMD64
        pushf
        pop     T2
 %else
        push    T0
        pushf
        pop     T0
 %endif
        mov     T1_32, [%1]             ; load flags.
        and     T1_32, ~(%2 | %3 | X86_EFL_PF | X86_EFL_SF)  ; clear the modified, always cleared flags and the two flags we calc.
 %ifdef RT_ARCH_AMD64
        and     T2_32, (%2)             ; select the modified flags.
        or      T1_32, T2_32            ; combine the flags.
 %else
        and     T0_32, (%2)             ; select the modified flags.
        or      T1_32, T0_32            ; combine the flags.
        pop     T0
 %endif

        ; First calculate SF as it's likely to be refereing to the same register as %6 does.
        bt      %4, %5 - 1
        jnc     %%sf_clear
        or      T1_32, X86_EFL_SF
 %%sf_clear:

        ; Parity last.
        and     %6, 0xff
 %ifdef RT_ARCH_AMD64
        lea     T2, [NAME(g_afParity) xWrtRIP]
        or      T1_8, [T2 + %6]
 %else
        or      T1_8, [NAME(g_afParity) + %6]
 %endif

        mov     [%1], T1_32             ; save the result.
%endmacro

;;
; Calculates the new EFLAGS using fixed clear and set bit masks.
;
; @remarks  Clobbers T0.
; @param        1       The register pointing to the EFLAGS.
; @param        2       Mask of additional flags to always clear
; @param        3       Mask of additional flags to always set.
;
%macro IEM_ADJUST_FLAGS_OLD 3
 %if (%2 | %3) != 0
        mov     T0_32, [%1]             ; Load flags.
  %if (%2) != 0
        and     T0_32, ~(%2)            ; Remove the always cleared flags.
  %endif
  %if (%3) != 0
        or      T0_32, %3               ; Add the always set flags.
  %endif
        mov     [%1], T0_32             ; Save the result.
 %endif
%endmacro

;;
; Calculates the new EFLAGS using fixed clear and set bit masks.
;
; @remarks  Clobbers T0, %4, EFLAGS.
; @param        1       The register pointing to the EFLAGS.
; @param        2       Mask of additional flags to always clear
; @param        3       Mask of additional flags to always set.
; @param        4       The (full) register containing the parity table index. Will be modified!
;
%macro IEM_ADJUST_FLAGS_WITH_PARITY_OLD 4
        mov     T0_32, [%1]                 ; Load flags.
        and     T0_32, ~(%2 | X86_EFL_PF)   ; Remove PF and the always cleared flags.
 %if (%3) != 0
        or      T0_32, %3                   ; Add the always set flags.
 %endif
        and     %4, 0xff
 %ifdef RT_ARCH_AMD64
        lea     T2, [NAME(g_afParity) xWrtRIP]
        or      T0_8, [T2 + %4]
 %else
        or      T0_8, [NAME(g_afParity) + %4]
 %endif
        mov     [%1], T0_32             ; Save the result.
%endmacro



;;
; Loads register with offset of imm8 instruction -- used by all of the instruction
; implementations which lay out jump tables of 256x immediate byte variants.
; Also checks that the instruction size matches the offsets in the table.
;
; @param    1       The register to receive the jump target address (T1).
; @param    2       The register containing the imm8 index (A1 / A2 / A3).
; @param    3       Byte size of one instruction + ret (+ ?int3) in the table
; @note             Implicitly uses local symbols .imm0, .imm1, and .immEmd
;                   (implementation artifacts of each instruction jump table).
;
; Emits the equivalent (in actual code) of `lea %1, [.imm0 + %2 * %3]`.
;
%macro IEMIMPL_JUMP_TABLE_TARGET_INT 3
        lea     %1, [.imm0 xWrtRIP]
  %if   %3 == 5
        lea     T0, [%2 + %2*4]  ; *5
        lea     %1, [%1 + T0]    ; *5 + .imm0
  %elif %3 == 6
        lea     T0, [%2 + %2*2]  ; *3
        lea     %1, [%1 + T0*2]  ; *6 + .imm0
  %elif %3 == 7
        lea     T0, [%2 + %2*2]  ; *3
        lea     T0, [T0 + %2*4]  ; *7
        lea     %1, [%1 + T0]    ; *7 + .imm0
  %elif %3 == 8
        lea     %1, [%1 + %2*8]  ; *8 + .imm0
  %elif %3 == 9
        lea     T0, [%2 + %2*8]  ; *9
        lea     %1, [%1 + T0]    ; *9 + .imm0
  %elif %3 == 10
        lea     T0, [%2 + %2*4]  ; *5
        lea     %1, [%1 + T0*2]  ; *10 + .imm0
  %elif %3 == 11
        lea     T0, [%2 + %2*4]  ; *5
        lea     T0, [%2 + T0*2]  ; *11
        lea     %1, [%1 + T0]    ; *11 + .imm0
  %elif %3 == 12
        lea     T0, [%2 + %2*2]  ; *3
        lea     %1, [%1 + T0*4]  ; *12 + .imm0
  %else
   %error Unexpected instruction byte count in IEMIMPL_JUMP_TABLE_TARGET_INT
  %endif
        ; check size: 'warning: value does not fit in 8 bit field' if bad
        times (.imm1 - .imm0 + %3) %% %3 db 999 * \
              (.imm1 - .imm0 + %3)
        ; check alignment: 'warning: value does not fit in 8 bit field' if bad
        times ((.immEnd - .imm0) - 256 * %3) db 999 * \
              ((.immEnd - .imm0) - 256 * %3)
%endmacro

%macro IEMIMPL_JUMP_TABLE_TARGET 3
 %ifdef RT_WITH_IBT_BRANCH_PROTECTION_WITHOUT_NOTRACK
        IEMIMPL_JUMP_TABLE_TARGET_INT %1, %2, (%3 + 4)
 %else
        IEMIMPL_JUMP_TABLE_TARGET_INT %1, %2, %3
 %endif
%endmacro


;;
; Calls the given imm8 instruction -- used by all of the instruction
; implementations which lay out jump tables of 256x immediate byte variants.
;
; @param    1       The register to receive the jump target address (T1).
; @param    2       The register containing the imm8 index (A1 / A2 / A3).
; @param    3       Byte size of one instruction + ret (+ ?int3) in the table
;
; Emits the equivalent (in actual code) of `lea %1, [.imm0 + %2 * %3]` +
; `IBT_NOTRACK, call %1`.
;
%macro IEMIMPL_CALL_JUMP_TABLE_TARGET 3
        IEMIMPL_JUMP_TABLE_TARGET %1, %2, %3
        IBT_NOTRACK
        call    %1
%endmacro


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
extern NAME(g_afParity)


;;
; Macro for implementing a binary operator.
;
; This will generate code for the 8, 16, 32 and 64 bit accesses with locked
; variants, except on 32-bit system where the 64-bit accesses requires hand
; coding.
;
; All the functions takes a pointer to the destination memory operand in A0,
; the source register operand in A1 and a pointer to eflags in A2.
;
; @param        1       The instruction mnemonic.
; @param        2       Non-zero if there should be a locked version.
; @param        3       The modified flags.
; @param        4       The undefined flags.
; @param        5       The flags that must be loaded (ADC, SBC).
; @param        6       The flags that will be zeroed by the operation.
;
%macro IEMIMPL_BIN_OP 6
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        %1      byte [A1], A2_8
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        %1      word [A1], A2_16
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        %1      dword [A1], A2_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        %1      qword [A1], A2
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64
  %endif ; RT_ARCH_AMD64

 %if %2 != 0 ; locked versions requested?

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        lock %1 byte [A1], A2_8
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8_locked

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        lock %1 word [A1], A2_16
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16_locked

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        lock %1 dword [A1], A2_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32_locked

  %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64_locked, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, %5
        lock %1 qword [A1], A2
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, %6
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64_locked
  %endif ; RT_ARCH_AMD64
 %endif ; locked
%endmacro

;            instr,lock, modified-flags,                                                               undefined flags, must be loaded, zeroed flags
IEMIMPL_BIN_OP add,  1, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,               0,              0
IEMIMPL_BIN_OP adc,  1, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,               X86_EFL_CF,     0
IEMIMPL_BIN_OP sub,  1, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,               0,              0
IEMIMPL_BIN_OP sbb,  1, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,               X86_EFL_CF,     0
IEMIMPL_BIN_OP cmp,  0, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,               0,              0
IEMIMPL_BIN_OP or,   1, (X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF),                                        X86_EFL_AF,      0,              X86_EFL_OF | X86_EFL_CF
IEMIMPL_BIN_OP xor,  1, (X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF),                                        X86_EFL_AF,      0,              X86_EFL_OF | X86_EFL_CF
IEMIMPL_BIN_OP and,  1, (X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF),                                        X86_EFL_AF,      0,              X86_EFL_OF | X86_EFL_CF
IEMIMPL_BIN_OP test, 0, (X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF),                                        X86_EFL_AF,      0,              X86_EFL_OF | X86_EFL_CF


;;
; Macro for implementing a binary operator, VEX variant with separate input/output.
;
; This will generate code for the 32 and 64 bit accesses, except on 32-bit system
; where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A0,
; the first source register operand in A1, the second source register operand
; in A2 and a pointer to eflags in A3.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       The zeroed flags.
;
%macro IEMIMPL_VEX_BIN_OP 4
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD           A3, %2, %3, 0 ;; @todo do we need to load undefined flags for any platform?
        %1      T0_32, A1_32, A2_32
        mov     [A0], T0_32
        IEM_SAVE_FLAGS_OLD                 A3, %2, %3, %4
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD           A3, %2, %3, 0
        %1      T0, A1, A2
        mov     [A0], T0
        IEM_SAVE_FLAGS_OLD                 A3, %2, %3, %4
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif ; RT_ARCH_AMD64
%endmacro

;                 instr,  modified-flags,                       undefined-flags,                        zeroed-flags
IEMIMPL_VEX_BIN_OP andn,  X86_EFL_SF | X86_EFL_ZF,              X86_EFL_AF | X86_EFL_PF,                X86_EFL_OF | X86_EFL_CF
IEMIMPL_VEX_BIN_OP bextr, X86_EFL_ZF,                           X86_EFL_SF | X86_EFL_AF | X86_EFL_PF,   X86_EFL_OF | X86_EFL_CF
IEMIMPL_VEX_BIN_OP bzhi,  X86_EFL_SF | X86_EFL_ZF | X86_EFL_CF, X86_EFL_AF | X86_EFL_PF,                X86_EFL_OF

;;
; Macro for implementing BLSR, BLCMSK and BLSI (fallbacks implemented in C).
;
; This will generate code for the 32 and 64 bit accesses, except on 32-bit system
; where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A1,
; the source register operand in A2 and incoming EFLAGS in A0.  Updated EFLAGS
; are returned in EAX.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       The zeroed flags.
;
%macro IEMIMPL_VEX_BIN_OP_2 4
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, 0 ;; @todo check if any undefined flags are passed thru
        mov     T0_32, [A1]
        %1      T0_32, A2_32
        mov     [A1], T0_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, %4
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 12
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, 0
        mov     T0, [A1]
        %1      T0, A2
        mov     [A1], T0
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, %4
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif ; RT_ARCH_AMD64
%endmacro

;                    instr,     modified-flags,                         undefined-flags             zeroed-flags
IEMIMPL_VEX_BIN_OP_2 blsr,      (X86_EFL_SF | X86_EFL_ZF | X86_EFL_CF), (X86_EFL_AF | X86_EFL_PF),  X86_EFL_OF
IEMIMPL_VEX_BIN_OP_2 blsmsk,    (X86_EFL_SF | X86_EFL_ZF | X86_EFL_CF), (X86_EFL_AF | X86_EFL_PF),  X86_EFL_OF
IEMIMPL_VEX_BIN_OP_2 blsi,      (X86_EFL_SF | X86_EFL_ZF | X86_EFL_CF), (X86_EFL_AF | X86_EFL_PF),  X86_EFL_OF


;;
; Macro for implementing a binary operator w/o flags, VEX variant with separate input/output.
;
; This will generate code for the 32 and 64 bit accesses, except on 32-bit system
; where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A0,
; the first source register operand in A1, the second source register operand
; in A2 and a pointer to eflags in A3.
;
; @param        1       The instruction mnemonic.
; @param        2       Fallback instruction if applicable.
; @param        3       Whether to emit fallback or not.
;
%macro IEMIMPL_VEX_BIN_OP_NOEFL 3
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
        %1      T0_32, A1_32, A2_32
        mov     [A0], T0_32
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %if %3
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32_fallback, 12
        PROLOGUE_3_ARGS
  %ifdef ASM_CALL64_GCC
        mov     cl, A2_8
        %2      A1_32, cl
        mov     [A0], A1_32
  %else
        xchg    A2, A0
        %2      A1_32, cl
        mov     [A2], A1_32
  %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32_fallback
 %endif

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 12
        PROLOGUE_3_ARGS
        %1      T0, A1, A2
        mov     [A0], T0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64

 %if %3
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64_fallback, 12
        PROLOGUE_3_ARGS
  %ifdef ASM_CALL64_GCC
        mov     cl, A2_8
        %2      A1, cl
        mov     [A0], A1_32
  %else
        xchg    A2, A0
        %2      A1, cl
        mov     [A2], A1_32
  %endif
        mov     [A0], A1
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64_fallback
  %endif
 %endif ; RT_ARCH_AMD64
%endmacro

;                           instr, fallback instr, emit fallback
IEMIMPL_VEX_BIN_OP_NOEFL    sarx,  sar,            1
IEMIMPL_VEX_BIN_OP_NOEFL    shlx,  shl,            1
IEMIMPL_VEX_BIN_OP_NOEFL    shrx,  shr,            1
IEMIMPL_VEX_BIN_OP_NOEFL    pdep,  nop,            0
IEMIMPL_VEX_BIN_OP_NOEFL    pext,  nop,            0


;
; RORX uses a immediate byte for the shift count, so we only do
; fallback implementation of that one.
;
BEGINPROC_FASTCALL iemAImpl_rorx_u32, 12
        PROLOGUE_3_ARGS
 %ifdef ASM_CALL64_GCC
        mov     cl, A2_8
        ror     A1_32, cl
        mov     [A0], A1_32
 %else
        xchg    A2, A0
        ror     A1_32, cl
        mov     [A2], A1_32
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_rorx_u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_rorx_u64, 12
        PROLOGUE_3_ARGS
 %ifdef ASM_CALL64_GCC
        mov     cl, A2_8
        ror     A1, cl
        mov     [A0], A1
 %else
        xchg    A2, A0
        ror     A1, cl
        mov     [A2], A1
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_rorx_u64
 %endif ; RT_ARCH_AMD64


;
; MULX
;
BEGINPROC_FASTCALL iemAImpl_mulx_u32, 16
        PROLOGUE_4_ARGS
%ifdef ASM_CALL64_GCC
        ; A2_32 is EDX - prefect
        mulx    T0_32, T1_32, A3_32
        mov     [A1], T1_32 ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], T0_32
%else
        ; A1 is xDX - must switch A1 and A2, so EDX=uSrc1
        xchg    A1, A2
        mulx    T0_32, T1_32, A3_32
        mov     [A2], T1_32 ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], T0_32
%endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_mulx_u32


BEGINPROC_FASTCALL iemAImpl_mulx_u32_fallback, 16
        PROLOGUE_4_ARGS
%ifdef ASM_CALL64_GCC
        ; A2_32 is EDX, T0_32 is EAX
        mov     eax, A3_32
        mul     A2_32
        mov     [A1], eax ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], edx
%else
        ; A1 is xDX, T0_32 is EAX - must switch A1 and A2, so EDX=uSrc1
        xchg    A1, A2
        mov     eax, A3_32
        mul     A2_32
        mov     [A2], eax ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], edx
%endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_mulx_u32_fallback

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_mulx_u64, 16
        PROLOGUE_4_ARGS
%ifdef ASM_CALL64_GCC
        ; A2 is RDX - prefect
        mulx    T0, T1, A3
        mov     [A1], T1 ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], T0
%else
        ; A1 is xDX - must switch A1 and A2, so RDX=uSrc1
        xchg    A1, A2
        mulx    T0, T1, A3
        mov     [A2], T1 ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], T0
%endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_mulx_u64


BEGINPROC_FASTCALL iemAImpl_mulx_u64_fallback, 16
        PROLOGUE_4_ARGS
%ifdef ASM_CALL64_GCC
        ; A2 is RDX, T0 is RAX
        mov     rax, A3
        mul     A2
        mov     [A1], rax ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], rdx
%else
        ; A1 is xDX, T0 is RAX - must switch A1 and A2, so RDX=uSrc1
        xchg    A1, A2
        mov     rax, A3
        mul     A2
        mov     [A2], rax ; Low value first, as we should return the high part if same destination registers.
        mov     [A0], rdx
%endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_mulx_u64_fallback

%endif


;;
; Macro for implementing a bit operator.
;
; This will generate code for the 16, 32 and 64 bit accesses with locked
; variants, except on 32-bit system where the 64-bit accesses requires hand
; coding.
;
; All the functions takes a pointer to the destination memory operand in A1,
; the source register operand in A2 and incoming eflags in A0.
;
; @param        1       The instruction mnemonic.
; @param        2       Non-zero if there should be a locked version.
; @param        3       The modified flags.
; @param        4       The undefined flags.
;
%macro IEMIMPL_BIT_OP 4
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        %1      word [A1], A2_16
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        %1      dword [A1], A2_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        %1      qword [A1], A2
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64
  %endif ; RT_ARCH_AMD64

 %if %2 != 0 ; locked versions requested?

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        lock %1 word [A1], A2_16
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16_locked

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        lock %1 dword [A1], A2_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32_locked

  %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64_locked, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %3, %4, 0
        lock %1 qword [A1], A2
        IEM_SAVE_FLAGS_RETVAL              A0_32, %3, %4, 0
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64_locked
  %endif ; RT_ARCH_AMD64
 %endif ; locked
%endmacro

;  Undefined flags are passed thru here by the intel and amd CPUs we have.
;                      modified efl, undefined eflags
IEMIMPL_BIT_OP bt,  0, (X86_EFL_CF), 0 ;passed-thru (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)
IEMIMPL_BIT_OP btc, 1, (X86_EFL_CF), 0 ;passed-thru (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)
IEMIMPL_BIT_OP bts, 1, (X86_EFL_CF), 0 ;passed-thru (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)
IEMIMPL_BIT_OP btr, 1, (X86_EFL_CF), 0 ;passed-thru (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)

;;
; Macro for implementing a bit search operator.
;
; This will generate code for the 16, 32 and 64 bit accesses, except on 32-bit
; system where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A1,
; the source register operand in A2 and the incoming eflags in A0.
;
; In the ZF case the destination register is 'undefined', however it seems that
; both AMD and Intel just leaves it as is.  The undefined EFLAGS differs between
; AMD and Intel and according to https://www.sandpile.org/x86/flags.htm between
; Intel microarchitectures.  We only implement 'intel' and 'amd' variation with
; the behaviour of more recent CPUs (Intel 10980XE and AMD 3990X).
;
; Intel: Clear all and calculate PF in addition to ZF.
; AMD:   Passthru all flags other than ZF.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       Non-zero if destination isn't written when ZF=1.  Zero if always written.
;
%macro IEMIMPL_BIT_OP2 4
BEGINCODE
;   16-bit

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, %3 ; Must load undefined flags since AMD passes them thru
        %1      T0_16, A2_16
%if %4 != 0
        jz      .unchanged_dst
%endif
        mov     [A1], T0_16
.unchanged_dst:
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16 %+ _intel, 12
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T1_16, A1_16
;bad;        jz      .unchanged_dst
;bad;        mov     [A0], T1_16
;bad;        IEM_ADJUST_FLAGS_WITH_PARITY_OLD    A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF | X86_EFL_ZF, 0, T1
;bad;        EPILOGUE_3_ARGS
;bad;.unchanged_dst:
;bad;%if %4 != 0
;bad;        mov     [A0], T1_16
;bad;%endif
;bad;        IEM_ADJUST_FLAGS_OLD                A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF, X86_EFL_ZF | X86_EFL_PF
;bad;        EPILOGUE_3_ARGS
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u16_intel
;bad;
;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16 %+ _amd, 12
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T0_16, A1_16
;bad;%if %4 != 0
;bad;        jz      .unchanged_dst
;bad;%endif
;bad;        mov     [A0], T0_16
;bad;.unchanged_dst:
;bad;        IEM_SAVE_AND_ADJUST_FLAGS_OLD       A2, %2, 0, 0    ; Only the ZF flag is modified on AMD Zen 2.
;bad;        EPILOGUE_3_ARGS
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u16_amd

;   32-bit

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, %3 ; Must load undefined flags since AMD passes them thru
        %1      T0_32, A2_32
%if %4 != 0
        jz      .unchanged_dst
%endif
        mov     [A1], T0_32
.unchanged_dst:
        IEM_SAVE_FLAGS_RETVAL             A0_32, %2, %3, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32 %+ _intel, 12
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T1_32, A1_32
;bad;%if %4 != 0
;bad;        jz      .unchanged_dst
;bad;%endif
;bad;        mov     [A0], T1_32
;bad;        IEM_ADJUST_FLAGS_WITH_PARITY_OLD    A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF | X86_EFL_ZF, 0, T1
;bad;        EPILOGUE_3_ARGS
;bad;.unchanged_dst:
;bad;        IEM_ADJUST_FLAGS_OLD                A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF, X86_EFL_ZF | X86_EFL_PF
;bad;        EPILOGUE_3_ARGS
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u32_intel
;bad;
;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32 %+ _amd, 12
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T0_32, A1_32
;bad;%if %4 != 0
;bad;        jz      .unchanged_dst
;bad;%endif
;bad;        mov     [A0], T0_32
;bad;.unchanged_dst:
;bad;        IEM_SAVE_AND_ADJUST_FLAGS_OLD       A2, %2, 0, 0    ; Only the ZF flag is modified on AMD Zen 2.
;bad;        EPILOGUE_3_ARGS
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u32_amd


 %ifdef RT_ARCH_AMD64
;   64-bit

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, %3 ; Must load undefined flags since AMD passes them thru
        %1      T0, A2
%if %4 != 0
        jz      .unchanged_dst
%endif
        mov     [A1], T0
.unchanged_dst:
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, 0
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64

;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64 %+ _intel, 16
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T1, A1
;bad;%if %4 != 0
;bad;        jz      .unchanged_dst
;bad;%endif
;bad;        mov     [A0], T1
;bad;        IEM_ADJUST_FLAGS_WITH_PARITY_OLD    A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF | X86_EFL_ZF, 0, T1
;bad;        EPILOGUE_3_ARGS
;bad;.unchanged_dst:
;bad;        IEM_ADJUST_FLAGS_OLD                A2, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_CF, X86_EFL_ZF | X86_EFL_PF
;bad;        EPILOGUE_3_ARGS
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u64_intel
;bad;
;bad;BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64 %+ _amd, 16
;bad;        PROLOGUE_3_ARGS
;bad;        %1      T0, A1
;bad;%if %4 != 0
;bad;        jz      .unchanged_dst
;bad;%endif
;bad;        mov     [A0], T0
;bad;.unchanged_dst:
;bad;        IEM_SAVE_AND_ADJUST_FLAGS_OLD       A2, %2, 0, 0    ; Only the ZF flag is modified on AMD Zen 2.
;bad;        EPILOGUE_3_ARGS_EX 8
;bad;ENDPROC iemAImpl_ %+ %1 %+ _u64_amd

 %endif ; RT_ARCH_AMD64
%endmacro

IEMIMPL_BIT_OP2 bsf,   (X86_EFL_ZF), (X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 1
IEMIMPL_BIT_OP2 bsr,   (X86_EFL_ZF), (X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 1
IEMIMPL_BIT_OP2 tzcnt, (X86_EFL_ZF | X86_EFL_CF), (X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF), 0
IEMIMPL_BIT_OP2 lzcnt, (X86_EFL_ZF | X86_EFL_CF), (X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF), 0


;;
; Macro for implementing POPCNT.
;
; This will generate code for the 16, 32 and 64 bit accesses, except on 32-bit
; system where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A1,
; the source register operand in A2 and eflags in A0.
;
; ASSUMES Intel and AMD set EFLAGS the same way.
;
; ASSUMES the instruction does not support memory destination.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       The zeroed flags.
;
%macro IEMIMPL_BIT_OP3 4
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, 0
        %1      T0_16, A2_16
        mov     [A1], T0_16
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, %4
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, 0
        %1      T0_32, A2_32
        mov     [A1], T0_32
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, %4
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS               A0_32, %2, %3, 0
        %1      T0, A2
        mov     [A1], T0
        IEM_SAVE_FLAGS_RETVAL              A0_32, %2, %3, %4
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif ; RT_ARCH_AMD64
%endmacro
IEMIMPL_BIT_OP3 popcnt, X86_EFL_ZF, 0, X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF


;
; IMUL is also a similar but yet different case (no lock, no mem dst).
; The rDX:rAX variant of imul is handled together with mul further down.
;
BEGINCODE
; @param        1       EFLAGS that are modified.
; @param        2       Undefined EFLAGS.
; @param        3       Function suffix.
; @param        4       EFLAGS variation: 0 for native, 1 for intel,
;                       2 for AMD (set AF, clear PF, ZF and SF).
%macro IEMIMPL_IMUL_TWO 4
BEGINPROC_FASTCALL iemAImpl_imul_two_u16 %+ %3, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS                        A0_32, %1, %2, %2 ; Undefined flags may be passed thru (AMD)
        imul    A2_16, word [A1]
        mov     [A1], A2_16
 %if %4 != 1
        IEM_SAVE_FLAGS_RETVAL                       A0_32, %1, %2, 0
 %else
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL A0_32, %1, X86_EFL_AF | X86_EFL_ZF, A2_16, 16, A2 ; intel
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_imul_two_u16 %+ %3

BEGINPROC_FASTCALL iemAImpl_imul_two_u32 %+ %3, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS                        A0_32, %1, %2, %2 ; Undefined flags may be passed thru (AMD)
        imul    A2_32, dword [A1]
        mov     [A1], A2_32
 %if %4 != 1
        IEM_SAVE_FLAGS_RETVAL                       A0_32, %1, %2, 0
 %else
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL A0_32, %1, X86_EFL_AF | X86_EFL_ZF, A2_32, 32, A2 ; intel
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_imul_two_u32 %+ %3

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_imul_two_u64 %+ %3, 16
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS                        A0_32, %1, %2, %2 ; Undefined flags may be passed thru (AMD)
        imul    A2, qword [A1]
        mov     [A1], A2
 %if %4 != 1
        IEM_SAVE_FLAGS_RETVAL                       A0_32, %1, %2, 0
 %else
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL A0_32, %1, X86_EFL_AF | X86_EFL_ZF, A2, 64, A2 ; intel
 %endif
        EPILOGUE_3_ARGS_EX 8
ENDPROC iemAImpl_imul_two_u64 %+ %3
 %endif ; RT_ARCH_AMD64
%endmacro
; The SF, ZF, AF and PF flags are "undefined". AMD (3990x) leaves these
; flags as is.  Whereas Intel skylake (6700K and 10980XE (Cascade Lake)) always
; clear AF and ZF and calculates SF and PF as per the lower half of the result.
IEMIMPL_IMUL_TWO X86_EFL_OF | X86_EFL_CF, X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF,       , 0
IEMIMPL_IMUL_TWO X86_EFL_OF | X86_EFL_CF, 0,                                                 _intel, 1
IEMIMPL_IMUL_TWO X86_EFL_OF | X86_EFL_CF, 0,                                                 _amd,   2


;
; XCHG for memory operands.  This implies locking.  No flag changes.
;
; Each function takes two arguments, first the pointer to the memory,
; then the pointer to the register.  They all return void.
;
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_xchg_u8_locked, 8
        PROLOGUE_2_ARGS
        mov     T0_8, [A1]
        xchg    [A0], T0_8
        mov     [A1], T0_8
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u8_locked

BEGINPROC_FASTCALL iemAImpl_xchg_u16_locked, 8
        PROLOGUE_2_ARGS
        mov     T0_16, [A1]
        xchg    [A0], T0_16
        mov     [A1], T0_16
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u16_locked

BEGINPROC_FASTCALL iemAImpl_xchg_u32_locked, 8
        PROLOGUE_2_ARGS
        mov     T0_32, [A1]
        xchg    [A0], T0_32
        mov     [A1], T0_32
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u32_locked

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_xchg_u64_locked, 8
        PROLOGUE_2_ARGS
        mov     T0, [A1]
        xchg    [A0], T0
        mov     [A1], T0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u64_locked
%endif

; Unlocked variants for fDisregardLock mode.

BEGINPROC_FASTCALL iemAImpl_xchg_u8_unlocked, 8
        PROLOGUE_2_ARGS
        mov     T0_8, [A1]
        mov     T1_8, [A0]
        mov     [A0], T0_8
        mov     [A1], T1_8
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u8_unlocked

BEGINPROC_FASTCALL iemAImpl_xchg_u16_unlocked, 8
        PROLOGUE_2_ARGS
        mov     T0_16, [A1]
        mov     T1_16, [A0]
        mov     [A0], T0_16
        mov     [A1], T1_16
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u16_unlocked

BEGINPROC_FASTCALL iemAImpl_xchg_u32_unlocked, 8
        PROLOGUE_2_ARGS
        mov     T0_32, [A1]
        mov     T1_32, [A0]
        mov     [A0], T0_32
        mov     [A1], T1_32
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u32_unlocked

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_xchg_u64_unlocked, 8
        PROLOGUE_2_ARGS
        mov     T0, [A1]
        mov     T1, [A0]
        mov     [A0], T0
        mov     [A1], T1
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_xchg_u64_unlocked
%endif


;
; XADD for memory operands.
;
; Each function takes three arguments, first the pointer to the
; memory/register, then the pointer to the register, and finally a pointer to
; eflags.  They all return void.
;
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_xadd_u8, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_8, [A1]
        xadd    [A0], T0_8
        mov     [A1], T0_8
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u8

BEGINPROC_FASTCALL iemAImpl_xadd_u16, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_16, [A1]
        xadd    [A0], T0_16
        mov     [A1], T0_16
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u16

BEGINPROC_FASTCALL iemAImpl_xadd_u32, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_32, [A1]
        xadd    [A0], T0_32
        mov     [A1], T0_32
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u32

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_xadd_u64, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0, [A1]
        xadd    [A0], T0
        mov     [A1], T0
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u64
%endif ; RT_ARCH_AMD64

BEGINPROC_FASTCALL iemAImpl_xadd_u8_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_8, [A1]
        lock xadd [A0], T0_8
        mov     [A1], T0_8
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u8_locked

BEGINPROC_FASTCALL iemAImpl_xadd_u16_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_16, [A1]
        lock xadd [A0], T0_16
        mov     [A1], T0_16
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u16_locked

BEGINPROC_FASTCALL iemAImpl_xadd_u32_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0_32, [A1]
        lock xadd [A0], T0_32
        mov     [A1], T0_32
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u32_locked

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_xadd_u64_locked, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        mov     T0, [A1]
        lock xadd [A0], T0
        mov     [A1], T0
        IEM_SAVE_FLAGS_OLD       A2, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0, 0
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_xadd_u64_locked
%endif ; RT_ARCH_AMD64


;
; CMPXCHG8B.
;
; These are tricky register wise, so the code is duplicated for each calling
; convention.
;
; WARNING! This code make ASSUMPTIONS about which registers T1 and T0 are mapped to!
;
; C-proto:
; IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg8b,(uint64_t *pu64Dst, PRTUINT64U pu64EaxEdx, PRTUINT64U pu64EbxEcx,
;                                             uint32_t *pEFlags));
;
; Note! Identical to iemAImpl_cmpxchg16b.
;
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_cmpxchg8b, 16
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        push    rbx

        mov     r11, rdx                ; pu64EaxEdx (is also T1)
        mov     r10, rcx                ; pu64Dst

        mov     ebx, [r8]
        mov     ecx, [r8 + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD r9, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [r11]
        mov     edx, [r11 + 4]

        cmpxchg8b [r10]

        mov     [r11], eax
        mov     [r11 + 4], edx
        IEM_SAVE_FLAGS_OLD       r9, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret
 %else
        push    rbx

        mov     r10, rcx                ; pEFlags
        mov     r11, rdx                ; pu64EbxEcx (is also T1)

        mov     ebx, [r11]
        mov     ecx, [r11 + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD r10, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [rsi]
        mov     edx, [rsi + 4]

        cmpxchg8b [rdi]

        mov     [rsi], eax
        mov     [rsi + 4], edx
        IEM_SAVE_FLAGS_OLD       r10, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret

 %endif
%else
        push    esi
        push    edi
        push    ebx
        push    ebp

        mov     edi, ecx                ; pu64Dst
        mov     esi, edx                ; pu64EaxEdx
        mov     ecx, [esp + 16 + 4 + 0] ; pu64EbxEcx
        mov     ebp, [esp + 16 + 4 + 4] ; pEFlags

        mov     ebx, [ecx]
        mov     ecx, [ecx + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD ebp, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [esi]
        mov     edx, [esi + 4]

        cmpxchg8b [edi]

        mov     [esi], eax
        mov     [esi + 4], edx
        IEM_SAVE_FLAGS_OLD       ebp, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, edi)

        pop     ebp
        pop     ebx
        pop     edi
        pop     esi
        ret     8
%endif
ENDPROC iemAImpl_cmpxchg8b

BEGINPROC_FASTCALL iemAImpl_cmpxchg8b_locked, 16
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        push    rbx

        mov     r11, rdx                ; pu64EaxEdx (is also T1)
        mov     r10, rcx                ; pu64Dst

        mov     ebx, [r8]
        mov     ecx, [r8 + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD r9, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [r11]
        mov     edx, [r11 + 4]

        lock cmpxchg8b [r10]

        mov     [r11], eax
        mov     [r11 + 4], edx
        IEM_SAVE_FLAGS_OLD       r9, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret
 %else
        push    rbx

        mov     r10, rcx                ; pEFlags
        mov     r11, rdx                ; pu64EbxEcx (is also T1)

        mov     ebx, [r11]
        mov     ecx, [r11 + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD r10, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [rsi]
        mov     edx, [rsi + 4]

        lock cmpxchg8b [rdi]

        mov     [rsi], eax
        mov     [rsi + 4], edx
        IEM_SAVE_FLAGS_OLD       r10, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret

 %endif
%else
        push    esi
        push    edi
        push    ebx
        push    ebp

        mov     edi, ecx                ; pu64Dst
        mov     esi, edx                ; pu64EaxEdx
        mov     ecx, [esp + 16 + 4 + 0] ; pu64EbxEcx
        mov     ebp, [esp + 16 + 4 + 4] ; pEFlags

        mov     ebx, [ecx]
        mov     ecx, [ecx + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD ebp, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [esi]
        mov     edx, [esi + 4]

        lock cmpxchg8b [edi]

        mov     [esi], eax
        mov     [esi + 4], edx
        IEM_SAVE_FLAGS_OLD       ebp, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, edi)

        pop     ebp
        pop     ebx
        pop     edi
        pop     esi
        ret     8
%endif
ENDPROC iemAImpl_cmpxchg8b_locked

%ifdef RT_ARCH_AMD64

;
; CMPXCHG16B.
;
; These are tricky register wise, so the code is duplicated for each calling
; convention.
;
; WARNING! This code make ASSUMPTIONS about which registers T1 and T0 are mapped to!
;
; C-proto:
; IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b,(PRTUINT128U pu128Dst, PRTUINT128U pu1284RaxRdx, PRTUINT128U pu128RbxRcx,
;                                              uint32_t *pEFlags));
;
; Note! Identical to iemAImpl_cmpxchg8b.
;
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_cmpxchg16b, 16
 %ifdef ASM_CALL64_MSC
        push    rbx

        mov     r11, rdx                ; pu64RaxRdx (is also T1)
        mov     r10, rcx                ; pu64Dst

        mov     rbx, [r8]
        mov     rcx, [r8 + 8]
        IEM_MAYBE_LOAD_FLAGS_OLD r9, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     rax, [r11]
        mov     rdx, [r11 + 8]

        cmpxchg16b [r10]

        mov     [r11], rax
        mov     [r11 + 8], rdx
        IEM_SAVE_FLAGS_OLD       r9, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret
 %else
        push    rbx

        mov     r10, rcx                ; pEFlags
        mov     r11, rdx                ; pu64RbxRcx (is also T1)

        mov     rbx, [r11]
        mov     rcx, [r11 + 8]
        IEM_MAYBE_LOAD_FLAGS_OLD r10, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     rax, [rsi]
        mov     rdx, [rsi + 8]

        cmpxchg16b [rdi]

        mov     [rsi], rax
        mov     [rsi + 8], rdx
        IEM_SAVE_FLAGS_OLD       r10, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret

 %endif
ENDPROC iemAImpl_cmpxchg16b

BEGINPROC_FASTCALL iemAImpl_cmpxchg16b_locked, 16
 %ifdef ASM_CALL64_MSC
        push    rbx

        mov     r11, rdx                ; pu64RaxRdx (is also T1)
        mov     r10, rcx                ; pu64Dst

        mov     rbx, [r8]
        mov     rcx, [r8 + 8]
        IEM_MAYBE_LOAD_FLAGS_OLD r9, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     rax, [r11]
        mov     rdx, [r11 + 8]

        lock cmpxchg16b [r10]

        mov     [r11], rax
        mov     [r11 + 8], rdx
        IEM_SAVE_FLAGS_OLD       r9, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret
 %else
        push    rbx

        mov     r10, rcx                ; pEFlags
        mov     r11, rdx                ; pu64RbxRcx (is also T1)

        mov     rbx, [r11]
        mov     rcx, [r11 + 8]
        IEM_MAYBE_LOAD_FLAGS_OLD r10, (X86_EFL_ZF), 0, 0 ; clobbers T0 (eax)
        mov     rax, [rsi]
        mov     rdx, [rsi + 8]

        lock cmpxchg16b [rdi]

        mov     [rsi], rax
        mov     [rsi + 8], rdx
        IEM_SAVE_FLAGS_OLD       r10, (X86_EFL_ZF), 0, 0 ; clobbers T0+T1 (eax, r11)

        pop     rbx
        ret

 %endif
ENDPROC iemAImpl_cmpxchg16b_locked

%endif ; RT_ARCH_AMD64


;
; CMPXCHG.
;
; WARNING! This code make ASSUMPTIONS about which registers T1 and T0 are mapped to!
;
; C-proto:
; IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg,(uintX_t *puXDst, uintX_t puEax, uintX_t uReg, uint32_t *pEFlags));
;
BEGINCODE
%macro IEMIMPL_CMPXCHG 2
BEGINPROC_FASTCALL iemAImpl_cmpxchg_u8 %+ %2, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0 (eax)
        mov     al, [A1]
        %1 cmpxchg [A0], A2_8
        mov     [A1], al
        IEM_SAVE_FLAGS_OLD       A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0+T1 (eax, r11/edi)
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_cmpxchg_u8 %+ %2

BEGINPROC_FASTCALL iemAImpl_cmpxchg_u16 %+ %2, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0 (eax)
        mov     ax, [A1]
        %1 cmpxchg [A0], A2_16
        mov     [A1], ax
        IEM_SAVE_FLAGS_OLD       A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0+T1 (eax, r11/edi)
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_cmpxchg_u16 %+ %2

BEGINPROC_FASTCALL iemAImpl_cmpxchg_u32 %+ %2, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0 (eax)
        mov     eax, [A1]
        %1 cmpxchg [A0], A2_32
        mov     [A1], eax
        IEM_SAVE_FLAGS_OLD       A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0+T1 (eax, r11/edi)
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_cmpxchg_u32 %+ %2

BEGINPROC_FASTCALL iemAImpl_cmpxchg_u64 %+ %2, 16
%ifdef RT_ARCH_AMD64
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0 (eax)
        mov     rax, [A1]
        %1 cmpxchg [A0], A2
        mov     [A1], rax
        IEM_SAVE_FLAGS_OLD       A3, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0+T1 (eax, r11/edi)
        EPILOGUE_4_ARGS
%else
        ;
        ; Must use cmpxchg8b here. See also iemAImpl_cmpxchg8b.
        ;
        push    esi
        push    edi
        push    ebx
        push    ebp

        mov     edi, ecx                ; pu64Dst
        mov     esi, edx                ; pu64Rax
        mov     ecx, [esp + 16 + 4 + 0] ; pu64Reg - Note! Pointer on 32-bit hosts!
        mov     ebp, [esp + 16 + 4 + 4] ; pEFlags

        mov     ebx, [ecx]
        mov     ecx, [ecx + 4]
        IEM_MAYBE_LOAD_FLAGS_OLD ebp, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0  ; clobbers T0 (eax)
        mov     eax, [esi]
        mov     edx, [esi + 4]

        lock cmpxchg8b [edi]

        ; cmpxchg8b doesn't set CF, PF, AF, SF and OF, so we have to do that.
        jz      .cmpxchg8b_not_equal
;; @todo this isn't correct. Need to do a 64-bit compare, not just the lower 32-bit.
        cmp     eax, eax                ; just set the other flags.
.store:
        mov     [esi], eax
        mov     [esi + 4], edx
        IEM_SAVE_FLAGS_OLD       ebp, (X86_EFL_ZF | X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF), 0, 0 ; clobbers T0+T1 (eax, edi)

        pop     ebp
        pop     ebx
        pop     edi
        pop     esi
        ret     8

.cmpxchg8b_not_equal:
        cmp     [esi + 4], edx          ;; @todo FIXME - verify 64-bit compare implementation
        jne     .store
        cmp     [esi], eax
        jmp     .store

%endif
ENDPROC iemAImpl_cmpxchg_u64 %+ %2
%endmacro ; IEMIMPL_CMPXCHG

IEMIMPL_CMPXCHG , ,
IEMIMPL_CMPXCHG lock, _locked



;;
; Macro for implementing a unary operator.
;
; This will generate code for the 8, 16, 32 and 64 bit accesses with locked
; variants, except on 32-bit system where the 64-bit accesses requires hand
; coding.
;
; All the functions takes a pointer to the destination memory operand in A0,
; the source register operand in A1 and a pointer to eflags in A2.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
;
%macro IEMIMPL_UNARY_OP 3
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        %1      byte [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8_locked, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        lock %1 byte [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8_locked

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        %1      word [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16_locked, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        lock %1 word [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16_locked

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        %1      dword [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32_locked, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        lock %1 dword [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32_locked

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        %1      qword [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64_locked, 8
        PROLOGUE_2_ARGS
        IEM_MAYBE_LOAD_FLAGS_OLD A1, %2, %3, 0
        lock %1 qword [A0]
        IEM_SAVE_FLAGS_OLD       A1, %2, %3, 0
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64_locked
 %endif ; RT_ARCH_AMD64

%endmacro

IEMIMPL_UNARY_OP inc, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF), 0
IEMIMPL_UNARY_OP dec, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF), 0
IEMIMPL_UNARY_OP neg, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0
IEMIMPL_UNARY_OP not, 0, 0


;
; BSWAP. No flag changes.
;
; Each function takes one argument, pointer to the value to bswap
; (input/output). They all return void.
;
BEGINPROC_FASTCALL iemAImpl_bswap_u16, 4
        PROLOGUE_1_ARGS
        mov     T0_32, [A0]             ; just in case any of the upper bits are used.
        db 66h
        bswap   T0_32
        mov     [A0], T0_32
        EPILOGUE_1_ARGS
ENDPROC iemAImpl_bswap_u16

BEGINPROC_FASTCALL iemAImpl_bswap_u32, 4
        PROLOGUE_1_ARGS
        mov     T0_32, [A0]
        bswap   T0_32
        mov     [A0], T0_32
        EPILOGUE_1_ARGS
ENDPROC iemAImpl_bswap_u32

BEGINPROC_FASTCALL iemAImpl_bswap_u64, 4
%ifdef RT_ARCH_AMD64
        PROLOGUE_1_ARGS
        mov     T0, [A0]
        bswap   T0
        mov     [A0], T0
        EPILOGUE_1_ARGS
%else
        PROLOGUE_1_ARGS
        mov     T0, [A0]
        mov     T1, [A0 + 4]
        bswap   T0
        bswap   T1
        mov     [A0 + 4], T0
        mov     [A0], T1
        EPILOGUE_1_ARGS
%endif
ENDPROC iemAImpl_bswap_u64


;;
; Macro for implementing a shift operation.
;
; This will generate code for the 8, 16, 32 and 64 bit accesses, except on
; 32-bit system where the 64-bit accesses requires hand coding.
;
; All the functions takes a pointer to the destination memory operand in A0,
; the shift count in A1 and a pointer to eflags in A2.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       Force load flags.
;
; Makes ASSUMPTIONS about A0, A1 and A2 assignments.  Specifically, that with
; GCC/64 we're free to use RCX/CL as it isn't used for any arguments.  While
; MSC/64 & 32-bit fastcall are using ECX for the first argument (fEFlagsIn),
; so we have to switch it around with the shift count parameter registers.
;
; @note the _intel and _amd variants are implemented in C.
;
%macro IEMIMPL_SHIFT_OP 4
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8, 12
        PROLOGUE_3_ARGS
 %ifdef ASM_CALL64_GCC
        IEM_MAYBE_LOAD_FLAGS     A0_32, %2, %3, %4
        mov     cl, A2_8
        %1      byte [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A0_32, %2, %3, 0
 %else
        xchg    A2, A0
        IEM_MAYBE_LOAD_FLAGS     A2_32, %2, %3, %4
        %1      byte [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A2_32, %2, %3, 0
 %endif
.zero_shift:
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 12
        PROLOGUE_3_ARGS
 %ifdef ASM_CALL64_GCC
        IEM_MAYBE_LOAD_FLAGS     A0_32, %2, %3, %4
        mov     cl, A2_8
        %1      word [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A0_32, %2, %3, 0
 %else
        xchg    A2, A0
        IEM_MAYBE_LOAD_FLAGS     A2_32, %2, %3, %4
        %1      word [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A2_32, %2, %3, 0
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 12
        PROLOGUE_3_ARGS
 %ifdef ASM_CALL64_GCC
        IEM_MAYBE_LOAD_FLAGS     A0_32, %2, %3, %4
        mov     cl, A2_8
        %1      dword [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A0_32, %2, %3, 0
 %else
        xchg    A2, A0
        IEM_MAYBE_LOAD_FLAGS     A2_32, %2, %3, %4
        %1      dword [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A2_32, %2, %3, 0
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 12
        PROLOGUE_3_ARGS
  %ifdef ASM_CALL64_GCC
        IEM_MAYBE_LOAD_FLAGS     A0_32, %2, %3, %4
        mov     cl, A2_8
        %1      qword [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A0_32, %2, %3, 0
  %else
        xchg    A2, A0
        IEM_MAYBE_LOAD_FLAGS     A2_32, %2, %3, %4
        %1      qword [A1], cl
        IEM_SAVE_FLAGS_RETVAL    A2_32, %2, %3, 0
  %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif ; RT_ARCH_AMD64

%endmacro

; These instructions will NOT modify flags if the masked shift count is zero
; (the mask is 0x3f for 64-bit instructions and 0x1f for the others).  Thus,
; we have to force load all modified and undefined.
IEMIMPL_SHIFT_OP rol, (X86_EFL_OF | X86_EFL_CF), 0,                                                   X86_EFL_CF | X86_EFL_OF
IEMIMPL_SHIFT_OP ror, (X86_EFL_OF | X86_EFL_CF), 0,                                                   X86_EFL_CF | X86_EFL_OF
IEMIMPL_SHIFT_OP rcl, (X86_EFL_OF | X86_EFL_CF), 0,                                                   X86_EFL_CF | X86_EFL_OF
IEMIMPL_SHIFT_OP rcr, (X86_EFL_OF | X86_EFL_CF), 0,                                                   X86_EFL_CF | X86_EFL_OF
IEMIMPL_SHIFT_OP shl, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF), (X86_EFL_AF), X86_EFL_STATUS_BITS
IEMIMPL_SHIFT_OP shr, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF), (X86_EFL_AF), X86_EFL_STATUS_BITS
IEMIMPL_SHIFT_OP sar, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF), (X86_EFL_AF), X86_EFL_STATUS_BITS


;;
; Macro for implementing a double precision shift operation.
;
; This will generate code for the 16, 32 and 64 bit accesses, except on
; 32-bit system where the 64-bit accesses requires hand coding.
;
; The functions takes the destination operand (r/m) in A0, the source (reg) in
; A1, the shift count in A2 and a pointer to the eflags variable/register in A3.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       The force loaded flags.
;
; Makes ASSUMPTIONS about A0, A1, A2 and A3 assignments.
;
; @note the _intel and _amd variants are implemented in C.
;
%macro IEMIMPL_SHIFT_DBL_OP 4
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16, 16
        PROLOGUE_4_ARGS
        ;IEM_LOAD_FLAGS_OLD      A3, %4, %3
        IEM_MAYBE_LOAD_FLAGS_OLD A3, %2, %3, %4
 %ifdef ASM_CALL64_GCC
        xchg    A3, A2
        %1      [A0], A1_16, cl
        xchg    A3, A2
 %else
        xchg    A0, A2
        %1      [A2], A1_16, cl
 %endif
        IEM_SAVE_FLAGS_OLD      A3, %2, %3, 0
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32, 16
        PROLOGUE_4_ARGS
        ;IEM_LOAD_FLAGS_OLD      A3, %4, %3
        IEM_MAYBE_LOAD_FLAGS_OLD A3, %2, %3, %4
 %ifdef ASM_CALL64_GCC
        xchg    A3, A2
        %1      [A0], A1_32, cl
        xchg    A3, A2
 %else
        xchg    A0, A2
        %1      [A2], A1_32, cl
 %endif
        IEM_SAVE_FLAGS_OLD      A3, %2, %3, 0
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32

 %ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 20
        PROLOGUE_4_ARGS
        ;IEM_LOAD_FLAGS_OLD      A3, %4, %3
        IEM_MAYBE_LOAD_FLAGS_OLD A3, %2, %3, %4
 %ifdef ASM_CALL64_GCC
        xchg    A3, A2
        %1      [A0], A1, cl
        xchg    A3, A2
 %else
        xchg    A0, A2
        %1      [A2], A1, cl
 %endif
        IEM_SAVE_FLAGS_OLD      A3, %2, %3, 0
        EPILOGUE_4_ARGS_EX 12
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif ; RT_ARCH_AMD64

%endmacro

; These instructions will NOT modify flags if the masked shift count is zero
; (the mask is 0x3f for 64-bit instructions and 0x1f for the others).  Thus,
; we have to force load all modified and undefined.
IEMIMPL_SHIFT_DBL_OP shld, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF), (X86_EFL_AF), X86_EFL_STATUS_BITS
IEMIMPL_SHIFT_DBL_OP shrd, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF), (X86_EFL_AF), X86_EFL_STATUS_BITS


;;
; Macro for implementing a multiplication operations.
;
; This will generate code for the 8, 16, 32 and 64 bit accesses, except on
; 32-bit system where the 64-bit accesses requires hand coding.
;
; The 8-bit function only operates on AX, so it takes no DX pointer.  The other
; functions takes a pointer to rAX in A0, rDX in A1, the operand in A2 and a
; incoming eflags in A3.
;
; The functions all return eflags. Since valid eflags can't ever be zero, we can
; use the same macros/tests framework as div/idiv.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       Name suffix.
; @param        5       EFLAGS behaviour: 0 for native, 1 for intel and 2 for AMD.
;
; Makes ASSUMPTIONS about A0, A1, A2, A3, T0 and T1 assignments.
;
%macro IEMIMPL_MUL_OP 5
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8 %+ %4, 12
        PROLOGUE_3_ARGS
        IEM_MAYBE_LOAD_FLAGS                         A2_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     al, [A0]
        %1      A1_8
        mov     [A0], ax
 %if %5 != 1
        IEM_SAVE_FLAGS_RETVAL                        A2_32, %2, %3, 0
 %else
        movzx   edx, ax
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL  A2_32, %2, X86_EFL_AF | X86_EFL_ZF, dx, 8, xDX ; intel
 %endif
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u8 %+ %4

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16 %+ %4, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS                         A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     ax, [A0]
 %ifdef ASM_CALL64_GCC
        %1      A2_16
        mov     [A0], ax
        mov     [A1], dx
 %else
        mov     T1, A1
        %1      A2_16
        mov     [A0], ax
        mov     [T1], dx
 %endif
 %if %5 != 1
        IEM_SAVE_FLAGS_RETVAL                        A3_32, %2, %3, 0
 %else
        movzx   edx, ax
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL  A3_32, %2, X86_EFL_AF | X86_EFL_ZF, dx, 16, xDX ; intel
 %endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u16 %+ %4

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32 %+ %4, 16
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS                         A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     eax, [A0]
 %ifdef ASM_CALL64_GCC
        %1      A2_32
        mov     [A0], eax
        mov     [A1], edx
 %else
        mov     T1, A1
        %1      A2_32
        mov     [A0], eax
        mov     [T1], edx
 %endif
 %if %5 != 1
        IEM_SAVE_FLAGS_RETVAL                        A3_32, %2, %3, 0
 %else
        mov     edx, eax
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL  A3_32, %2, X86_EFL_AF | X86_EFL_ZF, edx, 32, xDX ; intel
 %endif
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u32 %+ %4

 %ifdef RT_ARCH_AMD64 ; The 32-bit host version lives in IEMAllAImplC.cpp.
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64 %+ %4, 20
        PROLOGUE_4_ARGS
        IEM_MAYBE_LOAD_FLAGS                         A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     rax, [A0]
  %ifdef ASM_CALL64_GCC
        %1      A2
        mov     [A0], rax
        mov     [A1], rdx
  %else
        mov     T1, A1
        %1      A2
        mov     [A0], rax
        mov     [T1], rdx
  %endif
  %if %5 != 1
        IEM_SAVE_FLAGS_RETVAL                        A3_32, %2, %3, 0
  %else
        mov     T2, rax
        IEM_SAVE_FLAGS_ADJUST_AND_CALC_SF_PF_RETVAL  A3_32, %2, X86_EFL_AF | X86_EFL_ZF, T2, 64, T2 ; intel
  %endif
        EPILOGUE_4_ARGS_EX 12
ENDPROC iemAImpl_ %+ %1 %+ _u64 %+ %4
 %endif ; !RT_ARCH_AMD64
%endmacro

IEMIMPL_MUL_OP mul,  (X86_EFL_OF | X86_EFL_CF), (X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF),       , 0
IEMIMPL_MUL_OP mul,  (X86_EFL_OF | X86_EFL_CF), 0,                                                   _intel, 1
IEMIMPL_MUL_OP mul,  (X86_EFL_OF | X86_EFL_CF), 0,                                                   _amd,   2
IEMIMPL_MUL_OP imul, (X86_EFL_OF | X86_EFL_CF), (X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF),       , 0
IEMIMPL_MUL_OP imul, (X86_EFL_OF | X86_EFL_CF), 0,                                                   _intel, 1
IEMIMPL_MUL_OP imul, (X86_EFL_OF | X86_EFL_CF), 0,                                                   _amd,   2


BEGINCODE
;;
; Worker function for negating a 32-bit number in T1:T0
; @uses None (T0,T1)
BEGINPROC   iemAImpl_negate_T0_T1_u32
        push    0
        push    0
        xchg    T0_32, [xSP]
        xchg    T1_32, [xSP + xCB]
        sub     T0_32, [xSP]
        sbb     T1_32, [xSP + xCB]
        add     xSP, xCB*2
        ret
ENDPROC     iemAImpl_negate_T0_T1_u32

%ifdef RT_ARCH_AMD64
;;
; Worker function for negating a 64-bit number in T1:T0
; @uses None (T0,T1)
BEGINPROC   iemAImpl_negate_T0_T1_u64
        push    0
        push    0
        xchg    T0, [xSP]
        xchg    T1, [xSP + xCB]
        sub     T0, [xSP]
        sbb     T1, [xSP + xCB]
        add     xSP, xCB*2
        ret
ENDPROC     iemAImpl_negate_T0_T1_u64
%endif


;;
; Macro for implementing a division operations.
;
; This will generate code for the 8, 16, 32 and 64 bit accesses, except on
; 32-bit system where the 64-bit accesses requires hand coding.
;
; The 8-bit function only operates on AX, so it takes no DX pointer.  The other
; functions takes a pointer to rAX in A0, rDX in A1, the operand in A2 and
; incoming eflags in A3.
;
; The functions returns the new EFLAGS on success and zero on divide error.
; The new EFLAGS value can never be zero, given that bit 1 always set.
;
; @param        1       The instruction mnemonic.
; @param        2       The modified flags.
; @param        3       The undefined flags.
; @param        4       1 if signed, 0 if unsigned.
; @param        5       Function suffix.
; @param        6       EFLAGS variation: 0 for native, 1 for intel (ignored),
;                       2 for AMD (set AF, clear PF, ZF and SF).
;
; Makes ASSUMPTIONS about A0, A1, A2, A3, T0 and T1 assignments.
;
%macro IEMIMPL_DIV_OP 6
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u8 %+ %5, 12
        PROLOGUE_3_ARGS

        ; div by chainsaw check.
        and     A1_32, 0xff             ; Ensure it's zero extended to 16-bits for the idiv range check.
        jz      .div_zero

        ; Overflow check - unsigned division is simple to verify, haven't
        ; found a simple way to check signed division yet unfortunately.
 %if %4 == 0
        cmp     [A0 + 1], A1_8
        jae     .div_overflow
 %else
        movzx   T0_32, word [A0]        ; T0 = dividend (zero extending to full register to simplify register aliasing)
        mov     T1, A1                  ; T1 = saved divisor (because of missing T1_8 in 32-bit)
        test    A1_8, A1_8
        js      .divisor_negative
        test    T0_16, T0_16
        jns     .both_positive
        neg     T0_16
.one_of_each:                           ; OK range is 2^(result-width - 1) + (divisor - 1).
        push    T0                      ; Start off like unsigned below.
        shr     T0_16, 7
        cmp     T0_16, A1_16            ; 16-bit compare, since T0_16=0x8000 >> 7 --> T0_16=0x0100. (neg 0x8000 = 0x8000)
        pop     T0
        jb      .div_no_overflow
        ja      .div_overflow
        and     T0_8, 0x7f              ; Special case for covering (divisor - 1).
        cmp     T0_8, A1_8
        jae     .div_overflow
        jmp     .div_no_overflow

.divisor_negative:
        neg     A1_8
        test    T0_16, T0_16
        jns     .one_of_each
        neg     T0_16
.both_positive:                         ; Same as unsigned shifted by sign indicator bit.
        shr     T0_16, 7
        cmp     T0_16, A1_16            ; 16-bit compare, since T0_16=0x8000 >> 7 --> T0_16=0x0100. (neg 0x8000 = 0x8000)
        jae     .div_overflow
.div_no_overflow:
        mov     A1, T1                  ; restore divisor
 %endif

        IEM_MAYBE_LOAD_FLAGS    A2_32, %2, %3, %3 ; Undefined flags may be passed thru (Intel)
        mov     ax, [A0]
        %1      A1_8
        mov     [A0], ax
 %if %6 == 2 ; AMD64 3990X: Set AF and clear PF, ZF and SF.
        IEM_ADJUST_FLAGS_RETVAL A2_32, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, X86_EFL_AF
 %else
        IEM_SAVE_FLAGS_RETVAL   A2_32, %2, %3, 0
 %endif
.return:
        EPILOGUE_3_ARGS

.div_zero:
.div_overflow:
        xor     eax, eax
        jmp     .return
ENDPROC iemAImpl_ %+ %1 %+ _u8 %+ %5

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u16 %+ %5, 16
        PROLOGUE_4_ARGS

        ; div by chainsaw check.
        and     A2_16, 0xffff            ; Zero extend it for simpler sign overflow checks (see below).
        jz      .div_zero

        ; Overflow check - unsigned division is simple to verify, haven't
        ; found a simple way to check signed division yet unfortunately.
 %if %4 == 0
        cmp     [A1], A2_16
        jae     .div_overflow
 %else
        movzx   T0_32, word [A1]         ; Zero extend to simplify register aliasing by clobbing the whole register.
        shl     T0_32, 16
        mov     T0_16, [A0]              ; T0 = dividend
        mov     T1, A2                   ; T1 = divisor
        test    T1_16, T1_16
        js      .divisor_negative
        test    T0_32, T0_32
        jns     .both_positive
        neg     T0_32
.one_of_each:                           ; OK range is 2^(result-width - 1) + (divisor - 1).
        push    T0                      ; Start off like unsigned below.
        shr     T0_32, 15
        cmp     T0_32, T1_32            ; 32-bit compares, because 0x80000000 >> 15 = 0x10000 (65536) which doesn't fit in 16 bits.
        pop     T0
        jb      .div_no_overflow
        ja      .div_overflow
        and     T0_16, 0x7fff           ; Special case for covering (divisor - 1).
        cmp     T0_16, T1_16
        jae     .div_overflow
        jmp     .div_no_overflow

.divisor_negative:
        neg     T1_16
        test    T0_32, T0_32
        jns     .one_of_each
        neg     T0_32
.both_positive:                         ; Same as unsigned shifted by sign indicator bit.
        shr     T0_32, 15
        cmp     T0_32, T1_32            ; 32-bit compares, because 0x80000000 >> 15 = 0x10000 (65536) which doesn't fit in 16 bits.
        jae     .div_overflow
.div_no_overflow:
 %endif

        IEM_MAYBE_LOAD_FLAGS     A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
 %ifdef ASM_CALL64_GCC
        mov     T1, A2
        mov     ax, [A0]
        mov     dx, [A1]
        %1      T1_16
        mov     [A0], ax
        mov     [A1], dx
 %else
        mov     T1, A1
        mov     ax, [A0]
        mov     dx, [T1]
        %1      A2_16
        mov     [A0], ax
        mov     [T1], dx
 %endif
 %if %6 == 2 ; AMD64 3990X: Set AF and clear PF, ZF and SF.
        IEM_ADJUST_FLAGS_RETVAL A3_32, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, X86_EFL_AF
 %else
        IEM_SAVE_FLAGS_RETVAL   A3_32, %2, %3, 0
 %endif

.return:
        EPILOGUE_4_ARGS

.div_zero:
.div_overflow:
        xor     eax, eax
        jmp     .return
ENDPROC iemAImpl_ %+ %1 %+ _u16 %+ %5

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u32 %+ %5, 16
        PROLOGUE_4_ARGS

        ; div by chainsaw check.
        test    A2_32, A2_32
        jz      .div_zero

        ; Overflow check - unsigned division is simple to verify, haven't
        ; found a simple way to check signed division yet unfortunately.
 %if %4 == 0
        cmp     [A1], A2_32
        jae     .div_overflow
 %else
        push    A2                      ; save A2 so we modify it (we out of regs on x86).
        mov     T0_32, [A0]             ; T0 = dividend low
        mov     T1_32, [A1]             ; T1 = dividend high
        ;test    A2_32, A2_32 - we did this 5 instructions ago.
        js      .divisor_negative
        test    T1_32, T1_32
        jns     .both_positive
        call    NAME(iemAImpl_negate_T0_T1_u32)
.one_of_each:                           ; OK range is 2^(result-width - 1) + (divisor - 1).
        test    T1_32, 0x80000000       ; neg 0x8000000000000000 = 0x8000000000000000
        jnz     .div_overflow
        push    T0                      ; Start off like unsigned below.
        shl     T1_32, 1
        shr     T0_32, 31
        or      T1_32, T0_32
        cmp     T1_32, A2_32
        pop     T0
        jb      .div_no_overflow
        ja      .div_overflow
        and     T0_32, 0x7fffffff       ; Special case for covering (divisor - 1).
        cmp     T0_32, A2_32
        jae     .div_overflow
        jmp     .div_no_overflow

.divisor_negative:
        neg     A2_32
        test    T1_32, T1_32
        jns     .one_of_each
        call    NAME(iemAImpl_negate_T0_T1_u32)
.both_positive:                         ; Same as unsigned shifted by sign indicator bit.
        test    T1_32, 0x80000000       ; neg 0x8000000000000000 = 0x8000000000000000
        jnz     .div_overflow
        shl     T1_32, 1
        shr     T0_32, 31
        or      T1_32, T0_32
        cmp     T1_32, A2_32
        jae     .div_overflow
.div_no_overflow:
        pop     A2
 %endif

        IEM_MAYBE_LOAD_FLAGS    A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     eax, [A0]
 %ifdef ASM_CALL64_GCC
        mov     T1, A2
        mov     eax, [A0]
        mov     edx, [A1]
        %1      T1_32
        mov     [A0], eax
        mov     [A1], edx
 %else
        mov     T1, A1
        mov     eax, [A0]
        mov     edx, [T1]
        %1      A2_32
        mov     [A0], eax
        mov     [T1], edx
 %endif
 %if %6 == 2 ; AMD64 3990X: Set AF and clear PF, ZF and SF.
        IEM_ADJUST_FLAGS_RETVAL A3_32, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, X86_EFL_AF
 %else
        IEM_SAVE_FLAGS_RETVAL   A3_32, %2, %3, 0
 %endif

.return:
        EPILOGUE_4_ARGS

.div_overflow:
 %if %4 != 0
        pop     A2
 %endif
.div_zero:
        xor     eax, eax
        jmp     .return
ENDPROC iemAImpl_ %+ %1 %+ _u32 %+ %5

 %ifdef RT_ARCH_AMD64 ; The 32-bit host version lives in IEMAllAImplC.cpp.
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64 %+ %5, 20
        PROLOGUE_4_ARGS

        test    A2, A2
        jz      .div_zero
  %if %4 == 0
        cmp     [A1], A2
        jae     .div_overflow
  %else
        push    A2                      ; save A2 so we modify it (we out of regs on x86).
        mov     T0, [A0]                ; T0 = dividend low
        mov     T1, [A1]                ; T1 = dividend high
        ;test    A2, A2 - we did this five instructions above.
        js      .divisor_negative
        test    T1, T1
        jns     .both_positive
        call    NAME(iemAImpl_negate_T0_T1_u64)
.one_of_each:                           ; OK range is 2^(result-width - 1) + (divisor - 1).
        bt      T1, 63                  ; neg 0x8000000000000000'0000000000000000 = same
        jc      .div_overflow
        push    T0                      ; Start off like unsigned below.
        shl     T1, 1
        shr     T0, 63
        or      T1, T0
        cmp     T1, A2
        pop     T0
        jb      .div_no_overflow
        ja      .div_overflow
        mov     T1, 0x7fffffffffffffff
        and     T0, T1                  ; Special case for covering (divisor - 1).
        cmp     T0, A2
        jae     .div_overflow
        jmp     .div_no_overflow

.divisor_negative:
        neg     A2
        test    T1, T1
        jns     .one_of_each
        call    NAME(iemAImpl_negate_T0_T1_u64)
.both_positive:                         ; Same as unsigned shifted by sign indicator bit.
        bt      T1, 63                  ; neg 0x8000000000000000'0000000000000000 = same
        jc      .div_overflow
        shl     T1, 1
        shr     T0, 63
        or      T1, T0
        cmp     T1, A2
        jae     .div_overflow
.div_no_overflow:
        pop     A2
  %endif

        IEM_MAYBE_LOAD_FLAGS    A3_32, %2, %3, %3 ; Undefined flags may be passed thru (AMD)
        mov     rax, [A0]
  %ifdef ASM_CALL64_GCC
        mov     T1, A2
        mov     rax, [A0]
        mov     rdx, [A1]
        %1      T1
        mov     [A0], rax
        mov     [A1], rdx
  %else
        mov     T1, A1
        mov     rax, [A0]
        mov     rdx, [T1]
        %1      A2
        mov     [A0], rax
        mov     [T1], rdx
  %endif
  %if %6 == 2 ; AMD64 3990X: Set AF and clear PF, ZF and SF.
        IEM_ADJUST_FLAGS_RETVAL A3_32, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, X86_EFL_AF
  %else
        IEM_SAVE_FLAGS_RETVAL   A3_32, %2, %3, 0
  %endif

.return:
        EPILOGUE_4_ARGS_EX 12

.div_overflow:
  %if %4 != 0
        pop     A2
  %endif
.div_zero:
        xor     eax, eax
        jmp     .return
ENDPROC iemAImpl_ %+ %1 %+ _u64 %+ %5
 %endif ; !RT_ARCH_AMD64

%endmacro

IEMIMPL_DIV_OP div,  0, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 0,       , 0
IEMIMPL_DIV_OP div,  0, 0,                                                                             0, _intel, 1
IEMIMPL_DIV_OP div,  0, 0,                                                                             0, _amd,   2
;; @todo overflows with AX=0x8000 DL=0xc7 IDIV DL
IEMIMPL_DIV_OP idiv, 0, (X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF), 1,       , 0
IEMIMPL_DIV_OP idiv, 0, 0,                                                                             1, _intel, 1
IEMIMPL_DIV_OP idiv, 0, 0,                                                                             1, _amd,   2


;;
; Macro for implementing memory fence operation.
;
; No return value, no operands or anything.
;
; @param        1      The instruction.
;
%macro IEMIMPL_MEM_FENCE 1
BEGINCODE
BEGINPROC_FASTCALL iemAImpl_ %+ %1, 0
        %1
        ret
ENDPROC iemAImpl_ %+ %1
%endmacro

IEMIMPL_MEM_FENCE lfence
IEMIMPL_MEM_FENCE sfence
IEMIMPL_MEM_FENCE mfence

;;
; Alternative for non-SSE2 host.
;
BEGINPROC_FASTCALL iemAImpl_alt_mem_fence, 0
        push    xAX
        xchg    xAX, [xSP]
        add     xSP, xCB
        ret
ENDPROC iemAImpl_alt_mem_fence


;;
; Initialize the FPU for the actual instruction being emulated, this means
; loading parts of the guest's control word and status word.
;
; @uses     24 bytes of stack. T0, T1
; @param    1       Expression giving the address of the FXSTATE of the guest.
;
%macro FPU_LD_FXSTATE_FCW_AND_SAFE_FSW 1
        fnstenv [xSP]

        ; FCW - for exception, precision and rounding control.
        movzx   T0, word [%1 + X86FXSTATE.FCW]
        and     T0, X86_FCW_MASK_ALL | X86_FCW_PC_MASK | X86_FCW_RC_MASK
        mov     [xSP + X86FSTENV32P.FCW], T0_16

        ; FSW - for undefined C0, C1, C2, and C3.
        movzx   T1, word [%1 + X86FXSTATE.FSW]
        and     T1, X86_FSW_C_MASK
        movzx   T0, word [xSP + X86FSTENV32P.FSW]
        and     T0, X86_FSW_TOP_MASK
        or      T0, T1
        mov     [xSP + X86FSTENV32P.FSW], T0_16

        fldenv  [xSP]
%endmacro


;;
; Initialize the FPU for the actual instruction being emulated, this means
; loading parts of the guest's control word, status word, and update the
; tag word for the top register if it's empty.
;
; ASSUMES actual TOP=7
;
; @uses     24 bytes of stack.  T0, T1
; @param    1       Expression giving the address of the FXSTATE of the guest.
;
%macro FPU_LD_FXSTATE_FCW_AND_SAFE_FSW_AND_FTW_0 1
        fnstenv [xSP]

        ; FCW - for exception, precision and rounding control.
        movzx   T0_32, word [%1 + X86FXSTATE.FCW]
        and     T0_32, X86_FCW_MASK_ALL | X86_FCW_PC_MASK | X86_FCW_RC_MASK
        mov     [xSP + X86FSTENV32P.FCW], T0_16

        ; FSW - for undefined C0, C1, C2, and C3.
        movzx   T1_32, word [%1 + X86FXSTATE.FSW]
        and     T1_32, X86_FSW_C_MASK
        movzx   T0_32, word [xSP + X86FSTENV32P.FSW]
        and     T0_32, X86_FSW_TOP_MASK
        or      T0_32, T1_32
        mov     [xSP + X86FSTENV32P.FSW], T0_16

        ; FTW - Only for ST0 (in/out).
        movzx   T1_32, word [%1 + X86FXSTATE.FSW]
        shr     T1_32, X86_FSW_TOP_SHIFT
        and     T1_32, X86_FSW_TOP_SMASK
        bt      [%1 + X86FXSTATE.FTW], T1_16     ; Empty if FTW bit is clear.  Fixed register order.
        jc      %%st0_not_empty
        or      word [xSP + X86FSTENV32P.FTW], 0c000h ; TOP=7, so set TAG(7)=3
%%st0_not_empty:

        fldenv  [xSP]
%endmacro


;;
; Need to move this as well somewhere better?
;
struc IEMFPURESULT
    .r80Result  resw 5
    .FSW        resw 1
endstruc


;;
; Need to move this as well somewhere better?
;
struc IEMFPURESULTTWO
    .r80Result1 resw 5
    .FSW        resw 1
    .r80Result2 resw 5
endstruc


;
;---------------------- 16-bit signed integer operations ----------------------
;


;;
; Converts a 16-bit floating point value to a 80-bit one (fpu register).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 16-bit floating point value to convert.
;
BEGINPROC_FASTCALL iemAImpl_fild_r80_from_i16, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fild    word [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fild_r80_from_i16


;;
; Store a 80-bit floating point value (register) as a 16-bit signed integer (memory).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 16-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fist_r80_to_i16, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fistp   word [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fist_r80_to_i16


;;
; Store a 80-bit floating point value (register) as a 16-bit signed integer
; (memory) with truncation.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 16-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fistt_r80_to_i16, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fisttp  word [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fistt_r80_to_i16


;;
; FPU instruction working on one 80-bit and one 16-bit signed integer value.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 16-bit value.
;
%macro IEMIMPL_FPU_R80_BY_I16 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_i16, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      word [A3]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_i16
%endmacro

IEMIMPL_FPU_R80_BY_I16 fiadd
IEMIMPL_FPU_R80_BY_I16 fimul
IEMIMPL_FPU_R80_BY_I16 fisub
IEMIMPL_FPU_R80_BY_I16 fisubr
IEMIMPL_FPU_R80_BY_I16 fidiv
IEMIMPL_FPU_R80_BY_I16 fidivr


;;
; FPU instruction working on one 80-bit and one 16-bit signed integer value,
; only returning FSW.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to store the output FSW.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 64-bit value.
;
%macro IEMIMPL_FPU_R80_BY_I16_FSW 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_i16, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      word [A3]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_i16
%endmacro

IEMIMPL_FPU_R80_BY_I16_FSW ficom



;
;---------------------- 32-bit signed integer operations ----------------------
;


;;
; Converts a 32-bit floating point value to a 80-bit one (fpu register).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 32-bit floating point value to convert.
;
BEGINPROC_FASTCALL iemAImpl_fild_r80_from_i32, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fild    dword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fild_r80_from_i32


;;
; Store a 80-bit floating point value (register) as a 32-bit signed integer (memory).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 32-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fist_r80_to_i32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fistp   dword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fist_r80_to_i32


;;
; Store a 80-bit floating point value (register) as a 32-bit signed integer
; (memory) with truncation.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 32-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fistt_r80_to_i32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fisttp  dword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fistt_r80_to_i32


;;
; FPU instruction working on one 80-bit and one 32-bit signed integer value.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 32-bit value.
;
%macro IEMIMPL_FPU_R80_BY_I32 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_i32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      dword [A3]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_i32
%endmacro

IEMIMPL_FPU_R80_BY_I32 fiadd
IEMIMPL_FPU_R80_BY_I32 fimul
IEMIMPL_FPU_R80_BY_I32 fisub
IEMIMPL_FPU_R80_BY_I32 fisubr
IEMIMPL_FPU_R80_BY_I32 fidiv
IEMIMPL_FPU_R80_BY_I32 fidivr


;;
; FPU instruction working on one 80-bit and one 32-bit signed integer value,
; only returning FSW.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to store the output FSW.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 64-bit value.
;
%macro IEMIMPL_FPU_R80_BY_I32_FSW 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_i32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      dword [A3]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_i32
%endmacro

IEMIMPL_FPU_R80_BY_I32_FSW ficom



;
;---------------------- 64-bit signed integer operations ----------------------
;


;;
; Converts a 64-bit floating point value to a 80-bit one (fpu register).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 64-bit floating point value to convert.
;
BEGINPROC_FASTCALL iemAImpl_fild_r80_from_i64, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fild    qword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fild_r80_from_i64


;;
; Store a 80-bit floating point value (register) as a 64-bit signed integer (memory).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 64-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fist_r80_to_i64, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fistp   qword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fist_r80_to_i64


;;
; Store a 80-bit floating point value (register) as a 64-bit signed integer
; (memory) with truncation.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 64-bit signed integer value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fistt_r80_to_i64, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fisttp  qword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fistt_r80_to_i64



;
;---------------------- 32-bit floating point operations ----------------------
;

;;
; Converts a 32-bit floating point value to a 80-bit one (fpu register).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 32-bit floating point value to convert.
;
BEGINPROC_FASTCALL iemAImpl_fld_r80_from_r32, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fld     dword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fld_r80_from_r32


;;
; Store a 80-bit floating point value (register) as a 32-bit one (memory).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 32-bit value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fst_r80_to_r32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fst     dword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fst_r80_to_r32


;;
; FPU instruction working on one 80-bit and one 32-bit floating point value.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 32-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R32 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      dword [A3]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r32
%endmacro

IEMIMPL_FPU_R80_BY_R32 fadd
IEMIMPL_FPU_R80_BY_R32 fmul
IEMIMPL_FPU_R80_BY_R32 fsub
IEMIMPL_FPU_R80_BY_R32 fsubr
IEMIMPL_FPU_R80_BY_R32 fdiv
IEMIMPL_FPU_R80_BY_R32 fdivr


;;
; FPU instruction working on one 80-bit and one 32-bit floating point value,
; only returning FSW.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to store the output FSW.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 64-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R32_FSW 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r32, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      dword [A3]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r32
%endmacro

IEMIMPL_FPU_R80_BY_R32_FSW fcom



;
;---------------------- 64-bit floating point operations ----------------------
;

;;
; Converts a 64-bit floating point value to a 80-bit one (fpu register).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 64-bit floating point value to convert.
;
BEGINPROC_FASTCALL iemAImpl_fld_r80_from_r64, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fld     qword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fld_r80_from_r64


;;
; Store a 80-bit floating point value (register) as a 64-bit one (memory).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 64-bit value.
; @param    A3      Pointer to the 80-bit value.
;
BEGINPROC_FASTCALL iemAImpl_fst_r80_to_r64, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fst     qword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fst_r80_to_r64


;;
; FPU instruction working on one 80-bit and one 64-bit floating point value.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 64-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R64 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r64, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      qword [A3]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r64
%endmacro

IEMIMPL_FPU_R80_BY_R64 fadd
IEMIMPL_FPU_R80_BY_R64 fmul
IEMIMPL_FPU_R80_BY_R64 fsub
IEMIMPL_FPU_R80_BY_R64 fsubr
IEMIMPL_FPU_R80_BY_R64 fdiv
IEMIMPL_FPU_R80_BY_R64 fdivr

;;
; FPU instruction working on one 80-bit and one 64-bit floating point value,
; only returning FSW.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to store the output FSW.
; @param    A2      Pointer to the 80-bit value.
; @param    A3      Pointer to the 64-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R64_FSW 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r64, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      qword [A3]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r64
%endmacro

IEMIMPL_FPU_R80_BY_R64_FSW fcom



;
;---------------------- 80-bit floating point operations ----------------------
;

;;
; Loads a 80-bit floating point register value from memory.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit floating point value to load.
;
BEGINPROC_FASTCALL iemAImpl_fld_r80_from_r80, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fld     tword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fld_r80_from_r80


;;
; Store a 80-bit floating point register to memory
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 80-bit value.
; @param    A3      Pointer to the 80-bit register value.
;
BEGINPROC_FASTCALL iemAImpl_fst_r80_to_r80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fstp    tword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fst_r80_to_r80


;;
; Loads an 80-bit floating point register value in BCD format from memory.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit BCD value to load.
;
BEGINPROC_FASTCALL iemAImpl_fld_r80_from_d80, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fbld    tword [A2]

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_fld_r80_from_d80


;;
; Store a 80-bit floating point register to memory as BCD
;
; @param    A0      FPU context (fxsave).
; @param    A1      Where to return the output FSW.
; @param    A2      Where to store the 80-bit BCD value.
; @param    A3      Pointer to the 80-bit register value.
;
BEGINPROC_FASTCALL iemAImpl_fst_r80_to_d80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        fbstp   tword [A2]

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_fst_r80_to_d80


;;
; FPU instruction working on two 80-bit floating point values.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the first 80-bit value (ST0)
; @param    A3      Pointer to the second 80-bit value (STn).
;
%macro IEMIMPL_FPU_R80_BY_R80 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      %2

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r80
%endmacro

IEMIMPL_FPU_R80_BY_R80 fadd,   {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fmul,   {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fsub,   {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fsubr,  {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fdiv,   {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fdivr,  {st0, st1}
IEMIMPL_FPU_R80_BY_R80 fprem,  {}
IEMIMPL_FPU_R80_BY_R80 fprem1, {}
IEMIMPL_FPU_R80_BY_R80 fscale, {}


;;
; FPU instruction working on two 80-bit floating point values, ST1 and ST0,
; storing the result in ST1 and popping the stack.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the first 80-bit value (ST1).
; @param    A3      Pointer to the second 80-bit value (ST0).
;
%macro IEMIMPL_FPU_R80_BY_R80_ST1_ST0_POP 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        fld     tword [A3]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r80
%endmacro

IEMIMPL_FPU_R80_BY_R80_ST1_ST0_POP fpatan
IEMIMPL_FPU_R80_BY_R80_ST1_ST0_POP fyl2x
IEMIMPL_FPU_R80_BY_R80_ST1_ST0_POP fyl2xp1


;;
; FPU instruction working on two 80-bit floating point values, only
; returning FSW.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a uint16_t for the resulting FSW.
; @param    A2      Pointer to the first 80-bit value.
; @param    A3      Pointer to the second 80-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R80_FSW 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      st0, st1

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r80
%endmacro

IEMIMPL_FPU_R80_BY_R80_FSW fcom
IEMIMPL_FPU_R80_BY_R80_FSW fucom


;;
; FPU instruction working on two 80-bit floating point values,
; returning FSW and EFLAGS (eax).
;
; @param    1       The instruction
;
; @returns  EFLAGS in EAX.
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a uint16_t for the resulting FSW.
; @param    A2      Pointer to the first 80-bit value.
; @param    A3      Pointer to the second 80-bit value.
;
%macro IEMIMPL_FPU_R80_BY_R80_EFL 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_by_r80, 16
        PROLOGUE_4_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A3]
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1      st1

        fnstsw  word  [A1]
        pushf
        pop     xAX

        fninit
        add     xSP, 20h
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_by_r80
%endmacro

IEMIMPL_FPU_R80_BY_R80_EFL fcomi
IEMIMPL_FPU_R80_BY_R80_EFL fucomi


;;
; FPU instruction working on one 80-bit floating point value.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
; @param    A2      Pointer to the 80-bit value.
;
%macro IEMIMPL_FPU_R80 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80
%endmacro

IEMIMPL_FPU_R80 fchs
IEMIMPL_FPU_R80 fabs
IEMIMPL_FPU_R80 f2xm1
IEMIMPL_FPU_R80 fsqrt
IEMIMPL_FPU_R80 frndint
IEMIMPL_FPU_R80 fsin
IEMIMPL_FPU_R80 fcos


;;
; FPU instruction working on one 80-bit floating point value, only
; returning FSW.
;
; @param    1       The instruction
; @param    2       Non-zero to also restore FTW.
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a uint16_t for the resulting FSW.
; @param    A2      Pointer to the 80-bit value.
;
%macro IEMIMPL_FPU_R80_FSW 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
%if %2 != 0
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW_AND_FTW_0 A0
%else
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
%endif
        %1

        fnstsw  word  [A1]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80
%endmacro

IEMIMPL_FPU_R80_FSW ftst, 0
IEMIMPL_FPU_R80_FSW fxam, 1 ; No #IS or any other FP exceptions.



;;
; FPU instruction loading a 80-bit floating point constant.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULT for the output.
;
%macro IEMIMPL_FPU_R80_CONST 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1, 8
        PROLOGUE_2_ARGS
        sub     xSP, 20h

        fninit
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1

        fnstsw  word  [A1 + IEMFPURESULT.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULT.r80Result]

        fninit
        add     xSP, 20h
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+
%endmacro

IEMIMPL_FPU_R80_CONST fld1
IEMIMPL_FPU_R80_CONST fldl2t
IEMIMPL_FPU_R80_CONST fldl2e
IEMIMPL_FPU_R80_CONST fldpi
IEMIMPL_FPU_R80_CONST fldlg2
IEMIMPL_FPU_R80_CONST fldln2
IEMIMPL_FPU_R80_CONST fldz


;;
; FPU instruction working on one 80-bit floating point value, outputing two.
;
; @param    1       The instruction
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to a IEMFPURESULTTWO for the output.
; @param    A2      Pointer to the 80-bit value.
;
%macro IEMIMPL_FPU_R80_R80 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _r80_r80, 12
        PROLOGUE_3_ARGS
        sub     xSP, 20h

        fninit
        fld     tword [A2]
        FPU_LD_FXSTATE_FCW_AND_SAFE_FSW A0
        %1

        fnstsw  word  [A1 + IEMFPURESULTTWO.FSW]
        fnclex
        fstp    tword [A1 + IEMFPURESULTTWO.r80Result2]
        fnclex
        fstp    tword [A1 + IEMFPURESULTTWO.r80Result1]

        fninit
        add     xSP, 20h
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _r80_r80
%endmacro

IEMIMPL_FPU_R80_R80 fptan
IEMIMPL_FPU_R80_R80 fxtract
IEMIMPL_FPU_R80_R80 fsincos




;---------------------- SSE and MMX Operations ----------------------

;; @todo what do we need to do for MMX?
%macro IEMIMPL_MMX_PROLOGUE 0
%endmacro
%macro IEMIMPL_MMX_EPILOGUE 0
%endmacro

;; @todo what do we need to do for SSE?
%macro IEMIMPL_SSE_PROLOGUE 0
%endmacro
%macro IEMIMPL_SSE_EPILOGUE 0
%endmacro

;; @todo what do we need to do for AVX?
%macro IEMIMPL_AVX_PROLOGUE 0
%endmacro
%macro IEMIMPL_AVX_EPILOGUE 0
%endmacro


;;
; Media instruction working on two full sized registers.
;
; @param    1       The instruction
; @param    2       Whether there is an MMX variant (1) or not (0).
;
; @param    A0      FPU context (fxsave).
; @param    A1      Pointer to the first media register size operand (input/output).
; @param    A2      Pointer to the second media register size operand (input).
;
; @todo r=aeichner Currently unused, can probably be removed.
;
%macro IEMIMPL_MEDIA_F2 2
%if %2 != 0
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 12
        PROLOGUE_3_ARGS
        IEMIMPL_MMX_PROLOGUE

        movq    mm0, [A1]
        movq    mm1, [A2]
        %1      mm0, mm1
        movq    [A1], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
%endif

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu   xmm0, [A1]
        movdqu   xmm1, [A2]
        %1       xmm0, xmm1
        movdqu   [A1], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

;;
; Media instruction working on two full sized registers, but no FXSAVE state argument.
;
; @param    1       The instruction
; @param    2       Whether there is an MMX variant (1) or not (0).
;
; @param    A0      Pointer to the first media register size operand (input/output).
; @param    A1      Pointer to the second media register size operand (input).
;
%macro IEMIMPL_MEDIA_OPT_F2 2
%if %2 != 0
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 8
        PROLOGUE_2_ARGS
        IEMIMPL_MMX_PROLOGUE

        movq    mm0, [A0]
        movq    mm1, [A1]
        %1      mm0, mm1
        movq    [A0], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
%endif

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 8
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu   xmm0, [A0]
        movdqu   xmm1, [A1]
        %1       xmm0, xmm1
        movdqu   [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_OPT_F2 pshufb,  1
IEMIMPL_MEDIA_OPT_F2 pand,    1
IEMIMPL_MEDIA_OPT_F2 pandn,   1
IEMIMPL_MEDIA_OPT_F2 por,     1
IEMIMPL_MEDIA_OPT_F2 pxor,    1
IEMIMPL_MEDIA_OPT_F2 pcmpeqb, 1
IEMIMPL_MEDIA_OPT_F2 pcmpeqw, 1
IEMIMPL_MEDIA_OPT_F2 pcmpeqd, 1
IEMIMPL_MEDIA_OPT_F2 pcmpeqq, 0
IEMIMPL_MEDIA_OPT_F2 pcmpgtb, 1
IEMIMPL_MEDIA_OPT_F2 pcmpgtw, 1
IEMIMPL_MEDIA_OPT_F2 pcmpgtd, 1
IEMIMPL_MEDIA_OPT_F2 pcmpgtq, 0
IEMIMPL_MEDIA_OPT_F2 paddb,   1
IEMIMPL_MEDIA_OPT_F2 paddw,   1
IEMIMPL_MEDIA_OPT_F2 paddd,   1
IEMIMPL_MEDIA_OPT_F2 paddq,   1
IEMIMPL_MEDIA_OPT_F2 paddsb,  1
IEMIMPL_MEDIA_OPT_F2 paddsw,  1
IEMIMPL_MEDIA_OPT_F2 paddusb, 1
IEMIMPL_MEDIA_OPT_F2 paddusw, 1
IEMIMPL_MEDIA_OPT_F2 psubb,   1
IEMIMPL_MEDIA_OPT_F2 psubw,   1
IEMIMPL_MEDIA_OPT_F2 psubd,   1
IEMIMPL_MEDIA_OPT_F2 psubq,   1
IEMIMPL_MEDIA_OPT_F2 psubsb,  1
IEMIMPL_MEDIA_OPT_F2 psubsw,  1
IEMIMPL_MEDIA_OPT_F2 psubusb, 1
IEMIMPL_MEDIA_OPT_F2 psubusw, 1
IEMIMPL_MEDIA_OPT_F2 pmullw,  1
IEMIMPL_MEDIA_OPT_F2 pmulld,  0
IEMIMPL_MEDIA_OPT_F2 pmulhw,  1
IEMIMPL_MEDIA_OPT_F2 pmaddwd, 1
IEMIMPL_MEDIA_OPT_F2 pminub,  1
IEMIMPL_MEDIA_OPT_F2 pminuw,  0
IEMIMPL_MEDIA_OPT_F2 pminud,  0
IEMIMPL_MEDIA_OPT_F2 pminsb,  0
IEMIMPL_MEDIA_OPT_F2 pminsw,  1
IEMIMPL_MEDIA_OPT_F2 pminsd,  0
IEMIMPL_MEDIA_OPT_F2 pmaxub,  1
IEMIMPL_MEDIA_OPT_F2 pmaxuw,  0
IEMIMPL_MEDIA_OPT_F2 pmaxud,  0
IEMIMPL_MEDIA_OPT_F2 pmaxsb,  0
IEMIMPL_MEDIA_OPT_F2 pmaxsw,  1
IEMIMPL_MEDIA_OPT_F2 pmaxsd,  0
IEMIMPL_MEDIA_OPT_F2 pabsb,   1
IEMIMPL_MEDIA_OPT_F2 pabsw,   1
IEMIMPL_MEDIA_OPT_F2 pabsd,   1
IEMIMPL_MEDIA_OPT_F2 psignb,  1
IEMIMPL_MEDIA_OPT_F2 psignw,  1
IEMIMPL_MEDIA_OPT_F2 psignd,  1
IEMIMPL_MEDIA_OPT_F2 phaddw,  1
IEMIMPL_MEDIA_OPT_F2 phaddd,  1
IEMIMPL_MEDIA_OPT_F2 phsubw,  1
IEMIMPL_MEDIA_OPT_F2 phsubd,  1
IEMIMPL_MEDIA_OPT_F2 phaddsw, 1
IEMIMPL_MEDIA_OPT_F2 phsubsw, 1
IEMIMPL_MEDIA_OPT_F2 pmaddubsw, 1
IEMIMPL_MEDIA_OPT_F2 pmulhrsw,  1
IEMIMPL_MEDIA_OPT_F2 pmuludq,   1
IEMIMPL_MEDIA_OPT_F2 packsswb, 1
IEMIMPL_MEDIA_OPT_F2 packssdw, 1
IEMIMPL_MEDIA_OPT_F2 packuswb, 1
IEMIMPL_MEDIA_OPT_F2 packusdw, 0
IEMIMPL_MEDIA_OPT_F2 psllw,    1
IEMIMPL_MEDIA_OPT_F2 pslld,    1
IEMIMPL_MEDIA_OPT_F2 psllq,    1
IEMIMPL_MEDIA_OPT_F2 psrlw,    1
IEMIMPL_MEDIA_OPT_F2 psrld,    1
IEMIMPL_MEDIA_OPT_F2 psrlq,    1
IEMIMPL_MEDIA_OPT_F2 psraw,    1
IEMIMPL_MEDIA_OPT_F2 psrad,    1
IEMIMPL_MEDIA_OPT_F2 pmulhuw,  1
IEMIMPL_MEDIA_OPT_F2 pavgb,    1
IEMIMPL_MEDIA_OPT_F2 pavgw,    1
IEMIMPL_MEDIA_OPT_F2 psadbw,   1
IEMIMPL_MEDIA_OPT_F2 pmuldq,   0
IEMIMPL_MEDIA_OPT_F2 unpcklps, 0
IEMIMPL_MEDIA_OPT_F2 unpcklpd, 0
IEMIMPL_MEDIA_OPT_F2 unpckhps, 0
IEMIMPL_MEDIA_OPT_F2 unpckhpd, 0
IEMIMPL_MEDIA_OPT_F2 phminposuw, 0
IEMIMPL_MEDIA_OPT_F2 aesimc,   0
IEMIMPL_MEDIA_OPT_F2 aesenc,   0
IEMIMPL_MEDIA_OPT_F2 aesdec,   0
IEMIMPL_MEDIA_OPT_F2 aesenclast, 0
IEMIMPL_MEDIA_OPT_F2 aesdeclast, 0
IEMIMPL_MEDIA_OPT_F2 sha1nexte,  0
IEMIMPL_MEDIA_OPT_F2 sha1msg1,   0
IEMIMPL_MEDIA_OPT_F2 sha1msg2,   0
IEMIMPL_MEDIA_OPT_F2 sha256msg1, 0
IEMIMPL_MEDIA_OPT_F2 sha256msg2, 0


;;
; Media instruction working on one full sized and one half sized register (lower half).
;
; @param    1       The instruction
; @param    2       1 if MMX is included, 0 if not.
;
; @param    A0      Pointer to the first full sized media register operand (input/output).
; @param    A1      Pointer to the second half sized media register operand (input).
;
%macro IEMIMPL_MEDIA_F1L1 2
 %if %2 != 0
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u64, 8
        PROLOGUE_2_ARGS
        IEMIMPL_MMX_PROLOGUE

        movq    mm0, [A0]
        movq    mm1, [A1]
        %1      mm0, mm1
        movq    [A0], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u64
 %endif

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 8
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu   xmm0, [A0]
        movdqu   xmm1, [A1]
        %1       xmm0, xmm1
        movdqu   [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_F1L1 punpcklbw,  1
IEMIMPL_MEDIA_F1L1 punpcklwd,  1
IEMIMPL_MEDIA_F1L1 punpckldq,  1
IEMIMPL_MEDIA_F1L1 punpcklqdq, 0


;;
; Media instruction working two half sized input registers (lower half) and a full sized
; destination register (vpunpckh*).
;
; @param    1       The instruction
;
; @param    A0      Pointer to the destination register (full sized, output only).
; @param    A1      Pointer to the first full sized media source register operand, where we
;                   will only use the lower half as input - but we'll be loading it in full.
; @param    A2      Pointer to the second full sized media source register operand, where we
;                   will only use the lower half as input - but we'll be loading it in full.
;
%macro IEMIMPL_MEDIA_F1L1L1 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu xmm0, [A1]
        vmovdqu xmm1, [A2]
        %1      xmm0, xmm0, xmm1
        vmovdqu [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        vmovdqu  ymm1, [A2]
        %1       ymm0, ymm0, ymm1
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_MEDIA_F1L1L1 vpunpcklbw
IEMIMPL_MEDIA_F1L1L1 vpunpcklwd
IEMIMPL_MEDIA_F1L1L1 vpunpckldq
IEMIMPL_MEDIA_F1L1L1 vpunpcklqdq


;;
; Media instruction working on one full sized and one half sized register (high half).
;
; @param    1       The instruction
; @param    2       1 if MMX is included, 0 if not.
;
; @param    A0      Pointer to the first full sized media register operand (input/output).
; @param    A1      Pointer to the second full sized media register operand, where we
;                   will only use the upper half as input - but we'll load it in full.
;
%macro IEMIMPL_MEDIA_F1H1 2
IEMIMPL_MEDIA_F1L1 %1, %2
%endmacro

IEMIMPL_MEDIA_F1L1 punpckhbw,  1
IEMIMPL_MEDIA_F1L1 punpckhwd,  1
IEMIMPL_MEDIA_F1L1 punpckhdq,  1
IEMIMPL_MEDIA_F1L1 punpckhqdq, 0


;;
; Media instruction working two half sized input registers (high half) and a full sized
; destination register (vpunpckh*).
;
; @param    1       The instruction
;
; @param    A0      Pointer to the destination register (full sized, output only).
; @param    A1      Pointer to the first full sized media source register operand, where we
;                   will only use the upper half as input - but we'll be loading it in full.
; @param    A2      Pointer to the second full sized media source register operand, where we
;                   will only use the upper half as input - but we'll be loading it in full.
;
%macro IEMIMPL_MEDIA_F1H1H1 1
IEMIMPL_MEDIA_F1L1L1 %1
%endmacro

IEMIMPL_MEDIA_F1H1H1 vpunpckhbw
IEMIMPL_MEDIA_F1H1H1 vpunpckhwd
IEMIMPL_MEDIA_F1H1H1 vpunpckhdq
IEMIMPL_MEDIA_F1H1H1 vpunpckhqdq


;
; Shufflers with evil 8-bit immediates.
;

BEGINPROC_FASTCALL iemAImpl_pshufw_u64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_MMX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movq    mm1, [A1]
        movq    mm0, mm0                ; paranoia!
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 5
        movq    [A0], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_3_ARGS
%assign bImm 0
%rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        pshufw  mm0, mm1, bImm
        ret
 %assign bImm bImm + 1
%endrep
.immEnd:
ENDPROC iemAImpl_pshufw_u64


%macro IEMIMPL_MEDIA_SSE_PSHUFXX 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm1, [A1]
        movdqu  xmm0, xmm1              ; paranoia!
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS

 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_SSE_PSHUFXX pshufhw
IEMIMPL_MEDIA_SSE_PSHUFXX pshuflw
IEMIMPL_MEDIA_SSE_PSHUFXX pshufd


%macro IEMIMPL_MEDIA_AVX_VPSHUFXX 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        vmovdqu ymm1, [A1]
        vmovdqu ymm0, ymm1              ; paranoia!
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        vmovdqu  [A0], ymm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      ymm0, ymm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_MEDIA_AVX_VPSHUFXX vpshufhw
IEMIMPL_MEDIA_AVX_VPSHUFXX vpshuflw
IEMIMPL_MEDIA_AVX_VPSHUFXX vpshufd


;
; Shifts with evil 8-bit immediates.
;

%macro IEMIMPL_MEDIA_MMX_PSHIFTXX 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _imm_u64, 16
        PROLOGUE_2_ARGS
        IEMIMPL_MMX_PROLOGUE

        movzx   A1, A1_8                ; must clear top bits
        movq    mm0, [A0]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A1, 5
        movq    [A0], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_2_ARGS
%assign bImm 0
%rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      mm0, bImm
        ret
 %assign bImm bImm + 1
%endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _imm_u64
%endmacro

IEMIMPL_MEDIA_MMX_PSHIFTXX psllw
IEMIMPL_MEDIA_MMX_PSHIFTXX pslld
IEMIMPL_MEDIA_MMX_PSHIFTXX psllq
IEMIMPL_MEDIA_MMX_PSHIFTXX psrlw
IEMIMPL_MEDIA_MMX_PSHIFTXX psrld
IEMIMPL_MEDIA_MMX_PSHIFTXX psrlq
IEMIMPL_MEDIA_MMX_PSHIFTXX psraw
IEMIMPL_MEDIA_MMX_PSHIFTXX psrad


%macro IEMIMPL_MEDIA_SSE_PSHIFTXX 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _imm_u128, 16
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A1, A1_8                ; must clear top bits
        movdqu  xmm0, [A0]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A1, 6
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _imm_u128
%endmacro

IEMIMPL_MEDIA_SSE_PSHIFTXX psllw
IEMIMPL_MEDIA_SSE_PSHIFTXX pslld
IEMIMPL_MEDIA_SSE_PSHIFTXX psllq
IEMIMPL_MEDIA_SSE_PSHIFTXX psrlw
IEMIMPL_MEDIA_SSE_PSHIFTXX psrld
IEMIMPL_MEDIA_SSE_PSHIFTXX psrlq
IEMIMPL_MEDIA_SSE_PSHIFTXX psraw
IEMIMPL_MEDIA_SSE_PSHIFTXX psrad
IEMIMPL_MEDIA_SSE_PSHIFTXX pslldq
IEMIMPL_MEDIA_SSE_PSHIFTXX psrldq


;
; Move byte mask.
;

BEGINPROC_FASTCALL iemAImpl_pmovmskb_u64, 8
        PROLOGUE_2_ARGS
        IEMIMPL_MMX_PROLOGUE

        movq    mm1, [A1]
        pmovmskb T0, mm1
        mov     [A0], T0
%ifdef RT_ARCH_X86
        mov     dword [A0 + 4], 0
%endif
        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_pmovmskb_u64

BEGINPROC_FASTCALL iemAImpl_pmovmskb_u128, 8
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu  xmm1, [A1]
        pmovmskb T0, xmm1
        mov     [A0], T0
%ifdef RT_ARCH_X86
        mov     dword [A0 + 4], 0
%endif
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_pmovmskb_u128

BEGINPROC_FASTCALL iemAImpl_vpmovmskb_u256, 8
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm1, [A1]
        vpmovmskb T0, ymm1
        mov     [A0], T0
%ifdef RT_ARCH_X86
        mov     dword [A0 + 4], 0
%endif
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_vpmovmskb_u256


;;
; Media instruction working on two full sized source registers and one destination (AVX).
;
; @param    1       The instruction
;
; @param    A0      Pointer to the extended CPU/FPU state (X86XSAVEAREA).
; @param    A1      Pointer to the destination media register size operand (output).
; @param    A2      Pointer to the first source media register size operand (input).
; @param    A3      Pointer to the second source media register size operand (input).
;
; @todo r=aeichner Not used right now
;
%macro IEMIMPL_MEDIA_F3 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  xmm0, [A2]
        vmovdqu  xmm1, [A3]
        %1       xmm0, xmm0, xmm1
        vmovdqu  [A1], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A2]
        vmovdqu  ymm1, [A3]
        %1       ymm0, ymm0, ymm1
        vmovdqu  [A1], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

;;
; Media instruction working on two full sized source registers and one destination (AVX),
; but no XSAVE state pointer argument.
;
; @param    1       The instruction
; @param    2       Flag whether to add a 256-bit variant (1) or not (0).
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      Pointer to the second source media register size operand (input).
;
%macro IEMIMPL_MEDIA_OPT_F3 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  xmm0, [A1]
        vmovdqu  xmm1, [A2]
        %1       xmm0, xmm0, xmm1
        vmovdqu  [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        vmovdqu  ymm1, [A2]
        %1       ymm0, ymm0, ymm1
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_MEDIA_OPT_F3 vpshufb,    1
IEMIMPL_MEDIA_OPT_F3 vpand,      1
IEMIMPL_MEDIA_OPT_F3 vpminub,    1
IEMIMPL_MEDIA_OPT_F3 vpminuw,    1
IEMIMPL_MEDIA_OPT_F3 vpminud,    1
IEMIMPL_MEDIA_OPT_F3 vpminsb,    1
IEMIMPL_MEDIA_OPT_F3 vpminsw,    1
IEMIMPL_MEDIA_OPT_F3 vpminsd,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxub,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxuw,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxud,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxsb,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxsw,    1
IEMIMPL_MEDIA_OPT_F3 vpmaxsd,    1
IEMIMPL_MEDIA_OPT_F3 vpandn,     1
IEMIMPL_MEDIA_OPT_F3 vpor,       1
IEMIMPL_MEDIA_OPT_F3 vpxor,      1
IEMIMPL_MEDIA_OPT_F3 vpcmpeqb,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpeqw,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpeqd,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpeqq,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpgtb,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpgtw,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpgtd,   1
IEMIMPL_MEDIA_OPT_F3 vpcmpgtq,   1
IEMIMPL_MEDIA_OPT_F3 vpaddb,     1
IEMIMPL_MEDIA_OPT_F3 vpaddw,     1
IEMIMPL_MEDIA_OPT_F3 vpaddd,     1
IEMIMPL_MEDIA_OPT_F3 vpaddq,     1
IEMIMPL_MEDIA_OPT_F3 vpsubb,     1
IEMIMPL_MEDIA_OPT_F3 vpsubw,     1
IEMIMPL_MEDIA_OPT_F3 vpsubd,     1
IEMIMPL_MEDIA_OPT_F3 vpsubq,     1
IEMIMPL_MEDIA_OPT_F3 vpacksswb,  1
IEMIMPL_MEDIA_OPT_F3 vpackssdw,  1
IEMIMPL_MEDIA_OPT_F3 vpackuswb,  1
IEMIMPL_MEDIA_OPT_F3 vpackusdw,  1
IEMIMPL_MEDIA_OPT_F3 vpmullw,    1
IEMIMPL_MEDIA_OPT_F3 vpmulld,    1
IEMIMPL_MEDIA_OPT_F3 vpmulhw,    1
IEMIMPL_MEDIA_OPT_F3 vpmulhuw,   1
IEMIMPL_MEDIA_OPT_F3 vpavgb,     1
IEMIMPL_MEDIA_OPT_F3 vpavgw,     1
IEMIMPL_MEDIA_OPT_F3 vpsignb,    1
IEMIMPL_MEDIA_OPT_F3 vpsignw,    1
IEMIMPL_MEDIA_OPT_F3 vpsignd,    1
IEMIMPL_MEDIA_OPT_F3 vphaddw,    1
IEMIMPL_MEDIA_OPT_F3 vphaddd,    1
IEMIMPL_MEDIA_OPT_F3 vphsubw,    1
IEMIMPL_MEDIA_OPT_F3 vphsubd,    1
IEMIMPL_MEDIA_OPT_F3 vphaddsw,   1
IEMIMPL_MEDIA_OPT_F3 vphsubsw,   1
IEMIMPL_MEDIA_OPT_F3 vpmaddubsw, 1
IEMIMPL_MEDIA_OPT_F3 vpmulhrsw,  1
IEMIMPL_MEDIA_OPT_F3 vpsadbw,    1
IEMIMPL_MEDIA_OPT_F3 vpmuldq,    1
IEMIMPL_MEDIA_OPT_F3 vpmuludq,   1
IEMIMPL_MEDIA_OPT_F3 vunpcklps,  1
IEMIMPL_MEDIA_OPT_F3 vunpcklpd,  1
IEMIMPL_MEDIA_OPT_F3 vunpckhps,  1
IEMIMPL_MEDIA_OPT_F3 vunpckhpd,  1
IEMIMPL_MEDIA_OPT_F3 vpsubsb,    1
IEMIMPL_MEDIA_OPT_F3 vpsubsw,    1
IEMIMPL_MEDIA_OPT_F3 vpsubusb,   1
IEMIMPL_MEDIA_OPT_F3 vpsubusw,   1
IEMIMPL_MEDIA_OPT_F3 vpaddusb,   1
IEMIMPL_MEDIA_OPT_F3 vpaddusw,   1
IEMIMPL_MEDIA_OPT_F3 vpaddsb,    1
IEMIMPL_MEDIA_OPT_F3 vpaddsw,    1
IEMIMPL_MEDIA_OPT_F3 vpermilps,  1
IEMIMPL_MEDIA_OPT_F3 vpermilpd,  1
IEMIMPL_MEDIA_OPT_F3 vpmaddwd,   1
IEMIMPL_MEDIA_OPT_F3 vpsrlvd,    1
IEMIMPL_MEDIA_OPT_F3 vpsrlvq,    1
IEMIMPL_MEDIA_OPT_F3 vpsravd,    1
IEMIMPL_MEDIA_OPT_F3 vpsllvd,    1
IEMIMPL_MEDIA_OPT_F3 vpsllvq,    1

IEMIMPL_MEDIA_OPT_F3 vaesenc,     0
IEMIMPL_MEDIA_OPT_F3 vaesenclast, 0
IEMIMPL_MEDIA_OPT_F3 vaesdec,     0
IEMIMPL_MEDIA_OPT_F3 vaesdeclast, 0


;;
; VAESIMC instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vaesimc_u128, 8
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu   xmm0, [A0]
        movdqu   xmm1, [A1]
        vaesimc  xmm0, xmm1
        movdqu   [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_vaesimc_u128


;;
; VAESKEYGENASSIST instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
; @param    A2      8-bit immediate for the round constant.
;
BEGINPROC_FASTCALL iemAImpl_vaeskeygenassist_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 8
        movdqu  [A0], xmm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        vaeskeygenassist xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_vaeskeygenassist_u128


;;
; VPERMQ instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
; @param    A2      8-bit immediate for the round constant.
;
BEGINPROC_FASTCALL iemAImpl_vpermq_u256, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        vmovdqu ymm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 8
        vmovdqu [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        vpermq ymm0, ymm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_vpermq_u256


;;
; VPERMPD instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
; @param    A2      8-bit immediate for the round constant.
;
BEGINPROC_FASTCALL iemAImpl_vpermpd_u256, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        vmovdqu ymm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 8
        vmovdqu [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        vpermpd ymm0, ymm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_vpermpd_u256


;;
; VPERMPS instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
; @param    A2      Pointer to the third media register size operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vpermps_u256, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        vmovdqu  ymm1, [A2]
        vpermps  ymm0, ymm0, ymm1
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vpermps_u256


;;
; VPERMD instruction.
;
; @param    A0      Pointer to the first media register size operand (output).
; @param    A1      Pointer to the second media register size operand (input).
; @param    A2      Pointer to the third media register size operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vpermd_u256, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        vmovdqu  ymm1, [A2]
        vpermd   ymm0, ymm0, ymm1
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vpermd_u256


;;
; Media instruction working on one full sized source register, one full sized destination
; register, and one no-larger-than-XMM register (in the vps{ll,ra,rl}[dwq] instructions,
; this is actually used to retrieve a 128-bit load, from which a 64-bit shift length is
; extracted; if the 64-bit unsigned value is larger than the permissible max shift size
; of either 16, 32, or 64, it acts like the max shift size)
;
; @param    1       The instruction
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      Pointer to the second source media register size operand (input).
;
%macro IEMIMPL_SHIFT_OPT_F3 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  xmm0, [A1]
        vmovdqu  xmm1, [A2]
        %1       xmm0, xmm0, xmm1
        vmovdqu  [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        vmovdqu  xmm1, [A2]
        %1       ymm0, ymm0, xmm1
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_SHIFT_OPT_F3 vpsllw
IEMIMPL_SHIFT_OPT_F3 vpslld
IEMIMPL_SHIFT_OPT_F3 vpsllq
IEMIMPL_SHIFT_OPT_F3 vpsraw
IEMIMPL_SHIFT_OPT_F3 vpsrad
IEMIMPL_SHIFT_OPT_F3 vpsrlw
IEMIMPL_SHIFT_OPT_F3 vpsrld
IEMIMPL_SHIFT_OPT_F3 vpsrlq


;;
; Media instruction working on one full sized source registers and one destination (AVX),
; but no XSAVE state pointer argument.
;
; @param    1       The instruction
; @param    2       Flag whether the isntruction has a 256-bit (AVX2) variant (1) or not (0).
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the source media register size operand (input).
;
%macro IEMIMPL_MEDIA_OPT_F2_AVX 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  xmm0, [A1]
        %1       xmm0, xmm0
        vmovdqu  [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 12
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu  ymm0, [A1]
        %1       ymm0, ymm0
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_MEDIA_OPT_F2_AVX vpabsb, 1
IEMIMPL_MEDIA_OPT_F2_AVX vpabsw, 1
IEMIMPL_MEDIA_OPT_F2_AVX vpabsd, 1
IEMIMPL_MEDIA_OPT_F2_AVX vphminposuw, 0


;
; The SSE 4.2 crc32
;
; @param    A1      Pointer to the 32-bit destination.
; @param    A2      The source operand, sized according to the suffix.
;
BEGINPROC_FASTCALL iemAImpl_crc32_u8, 8
        PROLOGUE_2_ARGS

        mov     T0_32, [A0]
        crc32   T0_32, A1_8
        mov     [A0], T0_32

        EPILOGUE_2_ARGS
ENDPROC iemAImpl_crc32_u8

BEGINPROC_FASTCALL iemAImpl_crc32_u16, 8
        PROLOGUE_2_ARGS

        mov     T0_32, [A0]
        crc32   T0_32, A1_16
        mov     [A0], T0_32

        EPILOGUE_2_ARGS
ENDPROC iemAImpl_crc32_u16

BEGINPROC_FASTCALL iemAImpl_crc32_u32, 8
        PROLOGUE_2_ARGS

        mov     T0_32, [A0]
        crc32   T0_32, A1_32
        mov     [A0], T0_32

        EPILOGUE_2_ARGS
ENDPROC iemAImpl_crc32_u32

%ifdef RT_ARCH_AMD64
BEGINPROC_FASTCALL iemAImpl_crc32_u64, 8
        PROLOGUE_2_ARGS

        mov     T0_32, [A0]
        crc32   T0, A1
        mov     [A0], T0_32

        EPILOGUE_2_ARGS
ENDPROC iemAImpl_crc32_u64
%endif


;
; PTEST (SSE 4.1)
;
; @param    A0      Pointer to the first source operand (aka readonly destination).
; @param    A1      Pointer to the second source operand.
; @param    A2      Pointer to the EFLAGS register.
;
BEGINPROC_FASTCALL  iemAImpl_ptest_u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        ptest   xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A2, X86_EFL_ZF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_AF | X86_EFL_PF | X86_EFL_SF

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC             iemAImpl_ptest_u128

BEGINPROC_FASTCALL  iemAImpl_vptest_u256, 12
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        vmovdqu ymm0, [A0]
        vmovdqu ymm1, [A1]
        vptest  ymm0, ymm1
        IEM_SAVE_FLAGS_OLD A2, X86_EFL_ZF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_AF | X86_EFL_PF | X86_EFL_SF

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC             iemAImpl_vptest_u256


;; Template for the vtestp{s,d} instructions
;
; @param    1       The instruction
;
; @param    A0      Pointer to the first source operand (aka readonly destination).
; @param    A1      Pointer to the second source operand.
; @param    A2      Pointer to the EFLAGS register.
;
%macro IEMIMPL_VTESTP_S_D 1
BEGINPROC_FASTCALL  iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu xmm0, [A0]
        vmovdqu xmm1, [A1]
        %1 xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A2, X86_EFL_ZF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_AF | X86_EFL_PF | X86_EFL_SF

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC             iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL  iemAImpl_ %+ %1 %+ _u256, 12
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu ymm0, [A0]
        vmovdqu ymm1, [A1]
        %1 ymm0, ymm1
        IEM_SAVE_FLAGS_OLD A2, X86_EFL_ZF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_AF | X86_EFL_PF | X86_EFL_SF

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC             iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_VTESTP_S_D vtestps
IEMIMPL_VTESTP_S_D vtestpd


;;
; Template for the [v]pmov{s,z}x* instructions
;
; @param    1       The instruction
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      The source operand value (input).
;
%macro IEMIMPL_V_PMOV_SZ_X 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movd     xmm0, A1
        %1       xmm0, xmm0
        vmovdqu  [A0], xmm0

        IEMIMPL_SSE_PROLOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 12
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        movd     xmm0, A1
        v %+ %1  xmm0, xmm0
        vmovdqu  [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 12
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        movdqu   xmm0, [A1]
        v %+ %1  ymm0, xmm0
        vmovdqu  [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u256
%endmacro

IEMIMPL_V_PMOV_SZ_X pmovsxbw
IEMIMPL_V_PMOV_SZ_X pmovsxbd
IEMIMPL_V_PMOV_SZ_X pmovsxbq
IEMIMPL_V_PMOV_SZ_X pmovsxwd
IEMIMPL_V_PMOV_SZ_X pmovsxwq
IEMIMPL_V_PMOV_SZ_X pmovsxdq

IEMIMPL_V_PMOV_SZ_X pmovzxbw
IEMIMPL_V_PMOV_SZ_X pmovzxbd
IEMIMPL_V_PMOV_SZ_X pmovzxbq
IEMIMPL_V_PMOV_SZ_X pmovzxwd
IEMIMPL_V_PMOV_SZ_X pmovzxwq
IEMIMPL_V_PMOV_SZ_X pmovzxdq


;;
; Initialize the SSE MXCSR register using the guest value partially to
; account for rounding mode, load the value from the given register.
;
; @uses     4 bytes of stack to save the original value, T0.
; @param    1       Expression giving the register holding the guest's MXCSR.
;
%macro SSE_AVX_LD_MXCSR 1
        sub     xSP, 4

        stmxcsr [xSP]
        mov     T0_32, %1
        and     T0_32, X86_MXCSR_FZ | X86_MXCSR_RC_MASK | X86_MXCSR_DAZ
        or      T0_32, X86_MXCSR_XCPT_MASK
        sub     xSP, 4
        mov     [xSP], T0_32
        ldmxcsr [xSP]
        add     xSP, 4
%endmacro


;;
; Restores the SSE MXCSR register with the original value.
;
; @uses     4 bytes of stack to save the content of MXCSR value, T0, T1.
; @param    1       Expression giving the register to return the new guest's MXCSR value.
; @param    2       Expression giving the register holding original guest's MXCSR value.
;
; @note Restores the stack pointer.
;
%macro SSE_AVX_ST_MXCSR 2
        sub     xSP, 4
        stmxcsr [xSP]
        mov     %1, [xSP]
        add     xSP, 4
        ; Merge the status bits into the original MXCSR value.
        and     %1, X86_MXCSR_XCPT_FLAGS
        ;
        ; If PE is set together with OE/UE and neither are masked
        ; PE needs to be cleared because on real hardware
        ; an exception is generated with only OE/UE being set,
        ; but because we mask all exceptions PE will get set as well.
        ;
        mov     T2_32, %1
        and     T2_32, X86_MXCSR_OE | X86_MXCSR_UE
        mov     T1_32, %2
        and     T1_32, X86_MXCSR_OM | X86_MXCSR_UM
        shr     T1_32, X86_MXCSR_XCPT_MASK_SHIFT
        not     T1_32
        and     T2_32, T1_32
        test    T2_32, X86_MXCSR_OE | X86_MXCSR_UE
        jz      .excp_masked
        btr     %1, X86_MXCSR_PE_BIT
.excp_masked:
        or      %1, %2

        ldmxcsr [xSP]
        add     xSP, 4
%endmacro


;;
; Floating point instruction working on two full sized registers.
;
; @param    1       The instruction
; @param    2       Flag whether the AVX variant of the instruction takes two or three operands, 0 to disable AVX variants
;
; @returns  R0_32   The new MXCSR value of the guest.
; @param    A0      The guest's MXCSR register value to use.
; @param    A1      Where to return the result.
; @param    A2      Pointer to the first media register size operand (input/output).
; @param    A3      Pointer to the second media register size operand (input).
;
%macro IEMIMPL_FP_F2 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 12
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu   xmm0, [A2]
        movdqu   xmm1, [A3]
        %1       xmm0, xmm1
        movdqu   [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

 %if %2 == 3
BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 12
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  xmm0, [A2]
        vmovdqu  xmm1, [A3]
        v %+ %1  xmm0, xmm0, xmm1
        vmovdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 12
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  ymm0, [A2]
        vmovdqu  ymm1, [A3]
        v %+ %1  ymm0, ymm0, ymm1
        vmovdqu  [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u256
 %elif %2 == 2
BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 12
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  xmm0, [A2]
        vmovdqu  xmm1, [A3]
        v %+ %1  xmm0, xmm1
        vmovdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 12
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  ymm0, [A2]
        vmovdqu  ymm1, [A3]
        v %+ %1  ymm0, ymm1
        vmovdqu  [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_FP_F2 addps, 3
IEMIMPL_FP_F2 addpd, 3
IEMIMPL_FP_F2 mulps, 3
IEMIMPL_FP_F2 mulpd, 3
IEMIMPL_FP_F2 subps, 3
IEMIMPL_FP_F2 subpd, 3
IEMIMPL_FP_F2 minps, 3
IEMIMPL_FP_F2 minpd, 3
IEMIMPL_FP_F2 divps, 3
IEMIMPL_FP_F2 divpd, 3
IEMIMPL_FP_F2 maxps, 3
IEMIMPL_FP_F2 maxpd, 3
IEMIMPL_FP_F2 haddps, 3
IEMIMPL_FP_F2 haddpd, 3
IEMIMPL_FP_F2 hsubps, 3
IEMIMPL_FP_F2 hsubpd, 3
IEMIMPL_FP_F2 addsubps, 3
IEMIMPL_FP_F2 addsubpd, 3


;;
; These are actually unary operations but to keep it simple
; we treat them as binary for now, so the output result is
; always in sync with the register where the result might get written
; to.
IEMIMPL_FP_F2 sqrtps,    2
IEMIMPL_FP_F2 rsqrtps,   2
IEMIMPL_FP_F2 sqrtpd,    2
IEMIMPL_FP_F2 rcpps,     2
IEMIMPL_FP_F2 cvtdq2ps,  2
IEMIMPL_FP_F2 cvtps2dq,  2
IEMIMPL_FP_F2 cvttps2dq, 2
IEMIMPL_FP_F2 cvtdq2pd,  0 ; @todo AVX variants due to register size differences missing right now


;;
; Floating point instruction working on a full sized register and a single precision operand.
;
; @param    1       The instruction
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0      The guest's MXCSR register value to use.
; @param    A1      Where to return the result.
; @param    A2      Pointer to the first media register size operand (input/output).
; @param    A3      Pointer to the second single precision floating point value (input).
;
%macro IEMIMPL_FP_F2_R32 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128_r32, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu   xmm0, [A2]
        movd     xmm1, [A3]
        %1       xmm0, xmm1
        movdqu   [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128_r32

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128_r32, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  xmm0, [A2]
        vmovd    xmm1, [A3]
        v %+ %1  xmm0, xmm0, xmm1
        vmovdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128_r32
%endmacro

IEMIMPL_FP_F2_R32 addss
IEMIMPL_FP_F2_R32 mulss
IEMIMPL_FP_F2_R32 subss
IEMIMPL_FP_F2_R32 minss
IEMIMPL_FP_F2_R32 divss
IEMIMPL_FP_F2_R32 maxss
IEMIMPL_FP_F2_R32 cvtss2sd
IEMIMPL_FP_F2_R32 sqrtss
IEMIMPL_FP_F2_R32 rsqrtss
IEMIMPL_FP_F2_R32 rcpss


;;
; Floating point instruction working on a full sized register and a double precision operand.
;
; @param    1       The instruction
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0      The guest's MXCSR register value to use.
; @param    A1      Where to return the result.
; @param    A2      Pointer to the first media register size operand (input/output).
; @param    A3      Pointer to the second double precision floating point value (input).
;
%macro IEMIMPL_FP_F2_R64 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128_r64, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu   xmm0, [A2]
        movq     xmm1, [A3]
        %1       xmm0, xmm1
        movdqu   [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128_r64

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128_r64, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu  xmm0, [A2]
        vmovq    xmm1, [A3]
        v %+ %1  xmm0, xmm0, xmm1
        vmovdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128_r64
%endmacro

IEMIMPL_FP_F2_R64 addsd
IEMIMPL_FP_F2_R64 mulsd
IEMIMPL_FP_F2_R64 subsd
IEMIMPL_FP_F2_R64 minsd
IEMIMPL_FP_F2_R64 divsd
IEMIMPL_FP_F2_R64 maxsd
IEMIMPL_FP_F2_R64 cvtsd2ss
IEMIMPL_FP_F2_R64 sqrtsd


;;
; Macro for the cvtpd2ps/cvtps2pd instructions.
;
;           1       The instruction name.
;           2       Whether the AVX256 result is 128-bit (0) or 256-bit (1).
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Where to return the result.
; @param    A2      Pointer to the first media register size operand (input/output).
; @param    A3      Pointer to the second media register size operand (input).
;
%macro IEMIMPL_CVT_F2 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu  xmm0, [A2]
        movdqu  xmm1, [A3]
        %1      xmm0, xmm1
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu xmm1, [A2]
        v %+ %1 xmm0, xmm1
        vmovdqu [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128_u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128_u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vmovdqu xmm1, [A2]
 %if %2 == 0
        v %+ %1 xmm0, xmm1
 %else
        v %+ %1 ymm0, xmm1
 %endif
        vmovdqu [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_v %+ %1 %+ _u128_u256
%endmacro

IEMIMPL_CVT_F2 cvtpd2ps,  0
IEMIMPL_CVT_F2 cvttpd2dq, 0
IEMIMPL_CVT_F2 cvtpd2dq,  0

;IEMIMPL_CVT_F2 cvtps2pd, 1 - inefficient.

BEGINPROC_FASTCALL iemAImpl_cvtps2pd_u128, 12
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        cvtps2pd xmm0, [A2]
        movdqu   [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_cvtps2pd_u128


;;
; vcvtps2pd instruction - 128-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtps2pd_u128_u64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vcvtps2pd xmm0, qword [A2]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtps2pd_u128_u64


;;
; vcvtps2pd instruction - 256-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtps2pd_u256_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtps2pd ymm0, xmm1
        vmovdqu   [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtps2pd_u256_u128


;;
; vcvtdq2pd instruction - 128-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtdq2pd_u128_u64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        vcvtdq2pd xmm0, qword [A2]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtdq2pd_u128_u64


;;
; vcvtdq2pd instruction - 256-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtdq2pd_u256_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtdq2pd ymm0, xmm1
        vmovdqu   [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtdq2pd_u256_u128


;;
; shufps instructions with 8-bit immediates.
;
; @param    A0      Pointer to the destination media register size operand (input/output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_shufps_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        shufps  xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_shufps_u128


;;
; shufpd instruction with 8-bit immediates.
;
; @param    A0      Pointer to the destination media register size operand (input/output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_shufpd_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        shufpd  xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_shufpd_u128


;;
; vshufp{s,d} instructions with 8-bit immediates.
;
; @param    1       The instruction name.
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      Pointer to the second source media register size operand (input).
; @param    A3      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_AVX_VSHUFPX 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A1]
        movdqu  xmm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 6
        movdqu  [A0], xmm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        vmovdqu ymm0, [A1]
        vmovdqu ymm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 6
        vmovdqu [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      ymm0, ymm0, ymm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_MEDIA_AVX_VSHUFPX vshufps
IEMIMPL_MEDIA_AVX_VSHUFPX vshufpd


;;
; One of the [p]blendv{b,ps,pd} variants
;
; @param    1       The instruction
;
; @param    A0      Pointer to the first media register sized operand (input/output).
; @param    A1      Pointer to the second media sized value (input).
; @param    A2      Pointer to the media register sized mask value (input).
;
%macro IEMIMPL_P_BLEND 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu   xmm0, [A2] ; This is implicit
        movdqu   xmm1, [A0]
        movdqu   xmm2, [A1] ; @todo Do I need to save the original value here first?
        %1       xmm1, xmm2
        movdqu   [A0], xmm1

        IEMIMPL_SSE_PROLOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_P_BLEND pblendvb
IEMIMPL_P_BLEND blendvps
IEMIMPL_P_BLEND blendvpd


;;
; One of the v[p]blendv{b,ps,pd} variants
;
; @param    1       The instruction
;
; @param    A0      Pointer to the first media register sized operand (output).
; @param    A1      Pointer to the first media register sized operand (input).
; @param    A2      Pointer to the second media register sized operand (input).
; @param    A3      Pointer to the media register sized mask value (input).
%macro IEMIMPL_AVX_P_BLEND 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu   xmm0, [A1]
        vmovdqu   xmm1, [A2]
        vmovdqu   xmm2, [A3]
        %1        xmm0, xmm0, xmm1, xmm2
        vmovdqu   [A0], xmm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu   ymm0, [A1]
        vmovdqu   ymm1, [A2]
        vmovdqu   ymm2, [A3]
        %1        ymm0, ymm0, ymm1, ymm2
        vmovdqu   [A0], ymm0

        IEMIMPL_AVX_PROLOGUE
        EPILOGUE_4_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u256
%endmacro

IEMIMPL_AVX_P_BLEND vpblendvb
IEMIMPL_AVX_P_BLEND vblendvps
IEMIMPL_AVX_P_BLEND vblendvpd


;;
; palignr mm1, mm2/m64 instruction.
;
; @param    A0      Pointer to the first media register sized operand (output).
; @param    A1      The second register sized operand (input).
; @param    A2      The 8-bit immediate.
BEGINPROC_FASTCALL iemAImpl_palignr_u64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_MMX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movq    mm0, [A0]
        movq    mm1, A1
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        movq    [A0], mm0

        IEMIMPL_MMX_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        palignr mm0, mm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_palignr_u64


;;
; SSE instructions with 8-bit immediates of the form
;    xxx xmm1, xmm2, imm8.
; where the instruction encoding takes up 6 bytes.
;
; @param    1       The instruction name.
;
; @param    A0      Pointer to the first media register size operand (input/output).
; @param    A1      Pointer to the second source media register size operand (input).
; @param    A2      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_SSE_INSN_IMM8_6 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 8
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_SSE_INSN_IMM8_6 blendps
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 blendpd
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 pblendw
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 palignr
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 pclmulqdq
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 aeskeygenassist
IEMIMPL_MEDIA_SSE_INSN_IMM8_6 mpsadbw


;;
; AVX instructions with 8-bit immediates of the form
;    xxx {x,y}mm1, {x,y}mm2, {x,y}mm3, imm8.
; where the instruction encoding takes up 6 bytes.
;
; @param    1       The instruction name.
; @param    2       Whether the instruction has a 128-bit variant (1) or not (0).
; @param    3       Whether the instruction has a 256-bit variant (1) or not (0).
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      Pointer to the second source media register size operand (input).
; @param    A3      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_AVX_INSN_IMM8_6 3
 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A1]
        movdqu  xmm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        movdqu  [A0], xmm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128
 %endif

 %if %3 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        vmovdqu ymm0, [A1]
        vmovdqu ymm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        vmovdqu [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      ymm0, ymm0, ymm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vblendps,   1, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vblendpd,   1, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vpblendw,   1, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vpblendd,   1, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vpalignr,   1, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vpclmulqdq, 1, 0
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vperm2i128, 0, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vperm2f128, 0, 1
IEMIMPL_MEDIA_AVX_INSN_IMM8_6 vmpsadbw,   1, 1


;;
; AVX instructions with 8-bit immediates of the form
;    xxx {x,y}mm1, {x,y}mm2, imm8.
; where the instruction encoding takes up 6 bytes.
;
; @param    1       The instruction name.
; @param    2       Whether the instruction has a 128-bit variant (1) or not (0).
; @param    3       Whether the instruction has a 256-bit variant (1) or not (0).
; @param    4       The number of bytes taken up by a single instance of the instruction.
;
; @param    A0      Pointer to the destination media register size operand (output).
; @param    A1      Pointer to the first source media register size operand (input).
; @param    A2      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_AVX_INSN_IMM8_2OP 4
 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _imm_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, %4
        movdqu  [A0], xmm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _imm_u128
 %endif

 %if %3 == 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _imm_u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_AVX_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        vmovdqu ymm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, %4
        vmovdqu [A0], ymm0

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      ymm0, ymm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _imm_u256
 %endif
%endmacro

IEMIMPL_MEDIA_AVX_INSN_IMM8_2OP vpermilps, 1, 1, 8
IEMIMPL_MEDIA_AVX_INSN_IMM8_2OP vpermilpd, 1, 1, 8
IEMIMPL_MEDIA_AVX_INSN_IMM8_2OP vpslldq,   1, 1, 7
IEMIMPL_MEDIA_AVX_INSN_IMM8_2OP vpsrldq,   1, 1, 7


;;
; Need to move this as well somewhere better?
;
struc IEMPCMPISTRXSRC
    .uSrc1        resd 4
    .uSrc2        resd 4
endstruc

struc IEMPCMPESTRXSRC
    .uSrc1        resd 4
    .uSrc2        resd 4
    .u64Rax       resd 2
    .u64Rdx       resd 2
endstruc

;;
; The pcmpistri/vcmpistri instruction.
;
; @param    1       The instruction name
;
; @return   R0_32   The new ECX value.
; @param    A0      Pointer to the EFLAGS register.
; @param    A1      Pointer to the first operand (input).
; @param    A2      Pointer to the second operand (input).
; @param    A3      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_V_CMPISTRI 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A1]
        movdqu  xmm1, [A2]
        mov     T2, A0                  ; A0 can be ecx/rcx in some calling conventions which gets overwritten later (T2 only available on AMD64)
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8

        IEM_SAVE_FLAGS_OLD T2, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        mov    R0_32, ecx

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1 xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_V_CMPISTRI pcmpistri
IEMIMPL_MEDIA_V_CMPISTRI vpcmpistri


;;
; The pcmpestri instruction.
;
; @param    1       The instruction name
;
; @param    A0      Pointer to the ECX register to store the result to (output).
; @param    A1      Pointer to the EFLAGS register.
; @param    A2      Pointer to the structure containing the source operands (input).
; @param    A3      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_pcmpestri_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A2 + IEMPCMPESTRXSRC.uSrc1]
        movdqu  xmm1, [A2 + IEMPCMPESTRXSRC.uSrc2]
        mov     T2, A0                  ; A0 can be ecx/rcx in some calling conventions which gets overwritten later (T2 only available on AMD64)
        IEMIMPL_JUMP_TABLE_TARGET T1, A3, 8
        push    xDX                                 ; xDX can be A1 or A2 depending on the calling convention
        mov     xAX, [A2 + IEMPCMPESTRXSRC.u64Rax]  ; T0 is rax, so only overwrite it after we're done using it
        mov     xDX, [A2 + IEMPCMPESTRXSRC.u64Rdx]
        IBT_NOTRACK
        call    T1

        pop     xDX
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        mov    [T2], ecx

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        db      0x48                    ; Use the REX.W prefix to make pcmpestr{i,m} use full RAX/RDX (would use EAX/EDX only otherwise.)
        pcmpestri xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_pcmpestri_u128


;;
; The vpcmpestri instruction.
;
; @param    1       The instruction name
;
; @param    A0      Pointer to the ECX register to store the result to (output).
; @param    A1      Pointer to the EFLAGS register.
; @param    A2      Pointer to the structure containing the source operands (input).
; @param    A3      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_vpcmpestri_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A2 + IEMPCMPESTRXSRC.uSrc1]
        movdqu  xmm1, [A2 + IEMPCMPESTRXSRC.uSrc2]
        mov     T2, A0                  ; A0 can be ecx/rcx in some calling conventions which gets overwritten later (T2 only available on AMD64)
        IEMIMPL_JUMP_TABLE_TARGET T1, A3, 8
        push    xDX                                 ; xDX can be A1 or A2 depending on the calling convention
        mov     xAX, [A2 + IEMPCMPESTRXSRC.u64Rax]  ; T0 is rax, so only overwrite it after we're done using it
        mov     xDX, [A2 + IEMPCMPESTRXSRC.u64Rdx]
        IBT_NOTRACK
        call    T1

        pop     xDX
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        mov    [T2], ecx

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        db 0xc4, 0xe3, 0xf9, 0x61, 0xc1, bImm ; vpcmpestri xmm0,xmm1,0x1 with VEX.W set
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_vpcmpestri_u128


;;
; The pcmpistrm/vpcmpistrm instruction template.
;
; @param    1       The instruction name
;
; @param    A0      Pointer to the XMM0 register to store the result to (output).
; @param    A1      Pointer to the EFLAGS register.
; @param    A2      Pointer to the structure containing the source operands (input).
; @param    A3      The 8-bit immediate
;
%macro IEMIMPL_MEDIA_V_CMPISTRM 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2 + IEMPCMPISTRXSRC.uSrc1]
        movdqu  xmm2, [A2 + IEMPCMPISTRXSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8

        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1 xmm1, xmm2, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_V_CMPISTRM pcmpistrm
IEMIMPL_MEDIA_V_CMPISTRM vpcmpistrm


;;
; The pcmpestrm instruction.
;
; @param    A0      Pointer to the XMM0 register to store the result to (output).
; @param    A1      Pointer to the EFLAGS register.
; @param    A2      Pointer to the structure containing the source operands (input).
; @param    A3      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_pcmpestrm_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2 + IEMPCMPESTRXSRC.uSrc1]
        movdqu  xmm2, [A2 + IEMPCMPESTRXSRC.uSrc2]
        IEMIMPL_JUMP_TABLE_TARGET T1, A3, 8
        push    xDX                                 ; xDX can be A1 or A2 depending on the calling convention
        mov     xAX, [A2 + IEMPCMPESTRXSRC.u64Rax]  ; T0 is rax, so only overwrite it after we're done using it
        mov     xDX, [A2 + IEMPCMPESTRXSRC.u64Rdx]
        IBT_NOTRACK
        call    T1

        pop     xDX
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        db      0x48                    ; Use the REX.W prefix to make pcmpestr{i,m} use full RAX/RDX (would use EAX/EDX only otherwise.)
        pcmpestrm xmm1, xmm2, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_pcmpestrm_u128


;;
; The vpcmpestrm instruction.
;
; @param    A0      Pointer to the XMM0 register to store the result to (output).
; @param    A1      Pointer to the EFLAGS register.
; @param    A2      Pointer to the structure containing the source operands (input).
; @param    A3      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_vpcmpestrm_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2 + IEMPCMPESTRXSRC.uSrc1]
        movdqu  xmm2, [A2 + IEMPCMPESTRXSRC.uSrc2]
        IEMIMPL_JUMP_TABLE_TARGET T1, A3, 8
        push    xDX                                 ; xDX can be A1 or A2 depending on the calling convention
        mov     xAX, [A2 + IEMPCMPESTRXSRC.u64Rax]  ; T0 is rax, so only overwrite it after we're done using it
        mov     xDX, [A2 + IEMPCMPESTRXSRC.u64Rdx]
        IBT_NOTRACK
        call    T1

        pop     xDX
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF, 0, X86_EFL_AF | X86_EFL_PF
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        db 0xc4, 0xe3, 0xf9, 0x60, 0xca, bImm ; vpcmpestrm xmm1, xmm2, bImm with VEX.W set
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_vpcmpestrm_u128


;;
; movmskp{s,d} SSE instruction template
;
; @param    1       The SSE instruction name.
; @param    2       The AVX instruction name.
;
; @param    A0      Pointer to the output register (output/byte sized).
; @param    A1      Pointer to the source media register size operand (input).
;
%macro IEMIMPL_MEDIA_MOVMSK_P 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_2_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu  xmm0, [A1]
        %1      T0, xmm0
        mov     byte [A0], T0_8

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %2 %+ _u128, 16
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        movdqu  xmm0, [A1]
        %2      T0, xmm0
        mov     byte [A0], T0_8

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %2 %+ _u128

BEGINPROC_FASTCALL iemAImpl_ %+ %2 %+ _u256, 16
        PROLOGUE_2_ARGS
        IEMIMPL_AVX_PROLOGUE

        vmovdqu ymm0, [A1]
        %2      T0, ymm0
        mov     byte [A0], T0_8

        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_2_ARGS
ENDPROC iemAImpl_ %+ %2 %+ _u256
%endmacro

IEMIMPL_MEDIA_MOVMSK_P movmskps, vmovmskps
IEMIMPL_MEDIA_MOVMSK_P movmskpd, vmovmskpd


;;
; Template for [v]cvttss2si/[v]cvtss2si instructions.
;
; @param    1       Instruction name.
; @param    2       AVX or SSE
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
%macro IEMIMPL_MEDIA_V_CVTXSS2SI 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _i32_r32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_ %+ %2 %+ _PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        %1  T0_32, [A2]
        mov dword [A1], T0_32

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_ %+ %2 %+ _EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _i32_r32


BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _i64_r32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_ %+ %2 %+ _PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        %1  T0, [A2]
        mov qword [A1], T0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_ %+ %2 %+ _EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _i64_r32
%endmacro

IEMIMPL_MEDIA_V_CVTXSS2SI cvttss2si,  SSE
IEMIMPL_MEDIA_V_CVTXSS2SI vcvttss2si, AVX
IEMIMPL_MEDIA_V_CVTXSS2SI cvtss2si,   SSE
IEMIMPL_MEDIA_V_CVTXSS2SI vcvtss2si,  AVX


;;
; Template for [v]cvttsd2si/[v]cvtsd2si instructions.
;
; @param    1       Instruction name.
; @param    2       AVX or SSE
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
%macro IEMIMPL_MEDIA_V_CVTXSD2SI 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _i32_r64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_ %+ %2 %+ _PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        %1  T0_32, [A2]
        mov dword [A1], T0_32

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_ %+ %2 %+ _EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _i32_r64


BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _i64_r64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_ %+ %2 %+ _PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        %1  T0, [A2]
        mov qword [A1], T0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_ %+ %2 %+ _EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _i64_r64
%endmacro

IEMIMPL_MEDIA_V_CVTXSD2SI cvttsd2si,  SSE
IEMIMPL_MEDIA_V_CVTXSD2SI vcvttsd2si, AVX
IEMIMPL_MEDIA_V_CVTXSD2SI cvtsd2si,   SSE
IEMIMPL_MEDIA_V_CVTXSD2SI vcvtsd2si,  AVX


;;
; cvtsi2ss instruction - 32-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_cvtsi2ss_r32_i32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        cvtsi2ss  xmm0, dword [A2]
        movd      dword [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_cvtsi2ss_r32_i32


;;
; vcvtsi2ss instruction - 32-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
; @param    A3      Pointer to the third operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtsi2ss_u128_i32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtsi2ss xmm0, xmm0, dword [A3]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtsi2ss_u128_i32


;;
; cvtsi2ss instruction - 64-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_cvtsi2ss_r32_i64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        cvtsi2ss  xmm0, qword [A2]
        movd      dword [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_cvtsi2ss_r32_i64


;;
; vcvtsi2ss instruction - 64-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
; @param    A3      Pointer to the third operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtsi2ss_u128_i64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtsi2ss xmm0, xmm0, qword [A3]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtsi2ss_u128_i64


;;
; cvtsi2sd instruction - 32-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_cvtsi2sd_r64_i32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        cvtsi2sd  xmm0, dword [A2]
        movq      [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_cvtsi2sd_r64_i32


;;
; vcvtsi2sd instruction - 32-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
; @param    A3      Pointer to the third operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtsi2sd_u128_i32, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtsi2sd xmm0, xmm0, dword [A3]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtsi2sd_u128_i32


;;
; cvtsi2sd instruction - 64-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
;
BEGINPROC_FASTCALL iemAImpl_cvtsi2sd_r64_i64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        cvtsi2sd  xmm0, qword [A2]
        movq      [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_cvtsi2sd_r64_i64


;;
; vcvtsi2sd instruction - 64-bit variant.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use.
; @param    A1      Pointer to the result operand (output).
; @param    A2      Pointer to the second operand (input).
; @param    A3      Pointer to the third operand (input).
;
BEGINPROC_FASTCALL iemAImpl_vcvtsi2sd_u128_i64, 16
        PROLOGUE_3_ARGS
        IEMIMPL_AVX_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu    xmm0, [A2]
        vcvtsi2sd xmm0, xmm0, qword [A3]
        movdqu    [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_AVX_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_vcvtsi2sd_u128_i64


;
; UCOMISS (SSE)
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the EFLAGS value (input/output).
; @param    A2_32   The first source operand.
; @param    A3_32   The second source operand.
;
BEGINPROC_FASTCALL  iemAImpl_ucomiss_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movd    xmm0, A2_32
        movd    xmm1, A3_32
        ucomiss xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_ucomiss_u128

BEGINPROC_FASTCALL  iemAImpl_vucomiss_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movd    xmm0, A2_32
        movd    xmm1, A3_32
        vucomiss xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC             iemAImpl_vucomiss_u128


;
; UCOMISD (SSE)
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the EFLAGS value (input/output).
; @param    A2      The first source operand.
; @param    A3      The second source operand.
;
BEGINPROC_FASTCALL  iemAImpl_ucomisd_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movq    xmm0, A2
        movq    xmm1, A3
        ucomisd xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_ucomisd_u128

BEGINPROC_FASTCALL  iemAImpl_vucomisd_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movq    xmm0, A2
        movq    xmm1, A3
        vucomisd xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_vucomisd_u128

;
; COMISS (SSE)
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the EFLAGS value (input/output).
; @param    A2_32   The first source operand.
; @param    A3_32   The second source operand.
;
BEGINPROC_FASTCALL  iemAImpl_comiss_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movd    xmm0, A2_32
        movd    xmm1, A3_32
        comiss xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_comiss_u128

BEGINPROC_FASTCALL  iemAImpl_vcomiss_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movd    xmm0, A2_32
        movd    xmm1, A3_32
        vcomiss xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_vcomiss_u128


;
; COMISD (SSE)
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the EFLAGS value (input/output).
; @param    A2      The first source operand.
; @param    A3      The second source operand.
;
BEGINPROC_FASTCALL  iemAImpl_comisd_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movq    xmm0, A2
        movq    xmm1, A3
        comisd xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_comisd_u128

BEGINPROC_FASTCALL  iemAImpl_vcomisd_u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movq    xmm0, A2
        movq    xmm1, A3
        vcomisd xmm0, xmm1
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_AF

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_vcomisd_u128


;;
; Need to move this as well somewhere better?
;
struc IEMMEDIAF2XMMSRC
    .uSrc1        resd 4
    .uSrc2        resd 4
endstruc


struc IEMMEDIAF2YMMSRC
    .uSrc1        resd 8
    .uSrc2        resd 8
endstruc


;;
; SSE/AVX instructions with 8-bit immediates of the form
;    xxx xmm1, xmm2, imm8.
;   vxxx xmm1, xmm2, xmm3, imm8.
; and we need to load and save the MXCSR register.
;
; @param    1       The instruction name.
; @param    2       Flag whether this instruction has a 256-bit AVX variant (1) or not (0).
; @param    3       Number of bytes for the encoding of the SSE variant + ret instruction (AVX is fixed to 6).
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first media register size operand (output).
; @param    A2      Pointer to the two media register sized inputs - IEMMEDIAF2XMMSRC (input).
; @param    A3      The 8-bit immediate (input).
;
%macro IEMIMPL_MEDIA_SSE_INSN_IMM8_MXCSR 3
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A2 + IEMMEDIAF2XMMSRC.uSrc1]
        movdqu  xmm1, [A2 + IEMMEDIAF2XMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, %3
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128


BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A2 + IEMMEDIAF2XMMSRC.uSrc1]
        movdqu  xmm1, [A2 + IEMMEDIAF2XMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 6
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v %+ %1     xmm0, xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u128

 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        vmovdqu ymm0, [A2 + IEMMEDIAF2YMMSRC.uSrc1]
        vmovdqu ymm1, [A2 + IEMMEDIAF2YMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 6
        vmovdqu [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v %+ %1     ymm0, ymm0, ymm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_MEDIA_SSE_INSN_IMM8_MXCSR cmpps, 1, 5
IEMIMPL_MEDIA_SSE_INSN_IMM8_MXCSR cmppd, 1, 6
IEMIMPL_MEDIA_SSE_INSN_IMM8_MXCSR cmpss, 0, 6
IEMIMPL_MEDIA_SSE_INSN_IMM8_MXCSR cmpsd, 0, 6


;;
; SSE/AVX instructions with 2 full sized perands and an 8-bit immediate of the form
;    xxx xmm1, xmm2, imm8.
;   vxxx xmm1, xmm2, imm8
; where the instruction encoding takes up 6 bytes and we need to load and save the MXCSR
; register.
;
; @param    1       The instruction name.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first media register size operand (output).
; @param    A2      Pointer to the second media register size operand (input).
; @param    A3      The 8-bit immediate (input).
;
%macro IEMIMPL_MEDIA_SSE_AVX_INSN_F2_IMM8_MXCSR_6 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v%1     xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u128

BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        vmovdqu ymm1, [A2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        vmovdqu [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v%1     ymm0, ymm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u256
%endmacro

IEMIMPL_MEDIA_SSE_AVX_INSN_F2_IMM8_MXCSR_6 roundps
IEMIMPL_MEDIA_SSE_AVX_INSN_F2_IMM8_MXCSR_6 roundpd


;;
; SSE/AVX instructions with 3 full sized perands and an 8-bit immediate of the form
;    xxx xmm1, xmm2, imm8.
;   vxxx xmm1, xmm2, xmm3, imm8
; where the instruction encoding takes up 6 bytes and we need to load and save the MXCSR
; register.
;
; @param    1       The instruction name.
; @param    2       Flag whether to emit a 256-bit AVX variant (1) or not (0).
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first media register size operand (output).
; @param    A2      Pointer to the two media register sized inputs - IEMMEDIAF2XMMSRC/IEMMEDIAF2YMMSRC (input).
; @param    A3      The 8-bit immediate (input).
;
%macro IEMIMPL_MEDIA_SSE_AVX_INSN_F3_IMM8_MXCSR_6 2
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm0, [A2 + IEMMEDIAF2XMMSRC.uSrc1]
        movdqu  xmm1, [A2 + IEMMEDIAF2XMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        %1      xmm0, xmm1, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_ %+ %1 %+ _u128


BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u128, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        movdqu  xmm1, [A2 + IEMMEDIAF2XMMSRC.uSrc1]
        movdqu  xmm2, [A2 + IEMMEDIAF2XMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v %+ %1 xmm0, xmm1, xmm2, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u128


 %if %2 == 1
BEGINPROC_FASTCALL iemAImpl_v %+ %1 %+ _u256, 16
        PROLOGUE_4_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movzx   A3, A3_8                ; must clear top bits
        vmovdqu ymm1, [A2 + IEMMEDIAF2YMMSRC.uSrc1]
        vmovdqu ymm2, [A2 + IEMMEDIAF2YMMSRC.uSrc2]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A3, 8
        vmovdqu [A1], ymm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_4_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        v %+ %1 ymm0, ymm1, ymm2, bImm
        ret
        int3
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_v %+ %1 %+ _u256
 %endif
%endmacro

IEMIMPL_MEDIA_SSE_AVX_INSN_F3_IMM8_MXCSR_6 roundss, 0
IEMIMPL_MEDIA_SSE_AVX_INSN_F3_IMM8_MXCSR_6 roundsd, 0
IEMIMPL_MEDIA_SSE_AVX_INSN_F3_IMM8_MXCSR_6 dpps,    1
IEMIMPL_MEDIA_SSE_AVX_INSN_F3_IMM8_MXCSR_6 dppd,    0


;;
; SSE instructions of the form
;    xxx mm, xmm.
; and we need to load and save the MXCSR register.
;
; @param    1       The instruction name.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first MMX register sized operand (output).
; @param    A2      Pointer to the media register sized operand (input).
;
%macro IEMIMPL_MEDIA_SSE_MXCSR_I64_U128 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu  xmm0, [A2]
        %1      mm0, xmm0
        movq    [A1], mm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_SSE_MXCSR_I64_U128 cvtpd2pi
IEMIMPL_MEDIA_SSE_MXCSR_I64_U128 cvttpd2pi

;;
; SSE instructions of the form
;    xxx xmm, xmm/m64.
; and we need to load and save the MXCSR register.
;
; @param    1       The instruction name.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first media register sized operand (input/output).
; @param    A2      The 64bit source value from a MMX media register (input)
;
%macro IEMIMPL_MEDIA_SSE_MXCSR_U128_U64 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movdqu  xmm0, [A1]
        movq    mm0, A2
        %1      xmm0, mm0
        movdqu  [A1], xmm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_SSE_MXCSR_U128_U64 cvtpi2ps
IEMIMPL_MEDIA_SSE_MXCSR_U128_U64 cvtpi2pd

;;
; SSE instructions of the form
;    xxx mm, xmm/m64.
; and we need to load and save the MXCSR register.
;
; @param    1       The instruction name.
;
; @return   R0_32   The new MXCSR value of the guest.
; @param    A0_32   The guest's MXCSR register value to use (input).
; @param    A1      Pointer to the first MMX media register sized operand (output).
; @param    A2      The 64bit source value (input).
;
%macro IEMIMPL_MEDIA_SSE_MXCSR_U64_U64 1
BEGINPROC_FASTCALL iemAImpl_ %+ %1 %+ _u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE
        SSE_AVX_LD_MXCSR A0_32

        movq    xmm0, A2
        %1      mm0, xmm0
        movq    [A1], mm0

        SSE_AVX_ST_MXCSR R0_32, A0_32
        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_ %+ %1 %+ _u128
%endmacro

IEMIMPL_MEDIA_SSE_MXCSR_U64_U64 cvtps2pi
IEMIMPL_MEDIA_SSE_MXCSR_U64_U64 cvttps2pi

;
; All forms of RDRAND and RDSEED
;
; @param    A0      Pointer to the destination operand.
; @param    A1      Pointer to the EFLAGS value (input/output).
;
%macro IEMIMPL_RDRAND_RDSEED 3
BEGINPROC_FASTCALL  iemAImpl_ %+ %1 %+ _u %+ %3, 8
        PROLOGUE_2_ARGS

        %1      %2
        mov     [A0], %2
        IEM_SAVE_FLAGS_OLD A1, X86_EFL_CF, 0, X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF

        EPILOGUE_2_ARGS
ENDPROC             iemAImpl_ %+ %1 %+ _u %+ %3
%endmacro

IEMIMPL_RDRAND_RDSEED rdrand, ax,  16
IEMIMPL_RDRAND_RDSEED rdrand, eax, 32
IEMIMPL_RDRAND_RDSEED rdrand, rax, 64
IEMIMPL_RDRAND_RDSEED rdseed, ax,  16
IEMIMPL_RDRAND_RDSEED rdseed, eax, 32
IEMIMPL_RDRAND_RDSEED rdseed, rax, 64


;;
; sha1rnds4 xmm1, xmm2, imm8.
;
; @param    1       The instruction name.
;
; @param    A0      Pointer to the first media register size operand (input/output).
; @param    A1      Pointer to the second source media register size operand (input).
; @param    A2      The 8-bit immediate
;
BEGINPROC_FASTCALL iemAImpl_sha1rnds4_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movzx   A2, A2_8                ; must clear top bits
        movdqu  xmm0, [A0]
        movdqu  xmm1, [A1]
        IEMIMPL_CALL_JUMP_TABLE_TARGET T1, A2, 6
        movdqu  [A0], xmm0

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
 %assign bImm 0
 %rep 256
.imm %+ bImm:
        IBT_ENDBRxx_WITHOUT_NOTRACK
        sha1rnds4 xmm0, xmm1, bImm
        ret
  %assign bImm bImm + 1
 %endrep
.immEnd:
ENDPROC iemAImpl_sha1rnds4_u128


;;
; sha256rnds2 xmm1, xmm2, <XMM0>.
;
; @param    1       The instruction name.
;
; @param    A0      Pointer to the first media register size operand (input/output).
; @param    A1      Pointer to the second source media register size operand (input).
; @param    A2      Pointer to the implicit XMM0 constants (input).
;
BEGINPROC_FASTCALL iemAImpl_sha256rnds2_u128, 16
        PROLOGUE_3_ARGS
        IEMIMPL_SSE_PROLOGUE

        movdqu       xmm0, [A2]
        movdqu       xmm1, [A0]
        movdqu       xmm2, [A1]
        sha256rnds2  xmm1, xmm2
        movdqu       [A0], xmm1

        IEMIMPL_SSE_EPILOGUE
        EPILOGUE_3_ARGS
ENDPROC iemAImpl_sha256rnds2_u128


;
; 32-bit forms of ADCX and ADOX
;
; @returns  Updated EFLAGS.
; @param    A0      Incoming EFLAGS value (input).
; @param    A1      Pointer to the destination operand (input/output).
; @param    A2      32-bit source operand 1 (input).
;
%macro IEMIMPL_ADX_32 2
BEGINPROC_FASTCALL  iemAImpl_ %+ %1 %+ _u32, 8
        PROLOGUE_4_ARGS

        IEM_LOAD_FLAGS          A0_32, %2, 0
        %1      A2_32, [A1]
        mov     [A1], A2_32
        IEM_SAVE_FLAGS_RETVAL   A0_32, %2, 0, 0

        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_ %+ %1 %+ _u32
%endmacro

;
; 64-bit forms of ADCX and ADOX
;
; @returns  Updated EFLAGS.
; @param    A0      Incoming EFLAGS value (input).
; @param    A1      Pointer to the destination operand (input/output).
; @param    A2      64-bit source operand 1 (input).
;
%macro IEMIMPL_ADX_64 2
BEGINPROC_FASTCALL  iemAImpl_ %+ %1 %+ _u64, 8
        PROLOGUE_4_ARGS

        IEM_LOAD_FLAGS          A0_32, %2, 0
        %1      A2, [A1]
        mov     [A1], A2
        IEM_SAVE_FLAGS_RETVAL   A0_32, %2, 0, 0

        EPILOGUE_4_ARGS
ENDPROC             iemAImpl_ %+ %1 %+ _u64
%endmacro

IEMIMPL_ADX_32 adcx, X86_EFL_CF
IEMIMPL_ADX_64 adcx, X86_EFL_CF

IEMIMPL_ADX_32 adox, X86_EFL_OF
IEMIMPL_ADX_64 adox, X86_EFL_OF

