; $Id: commondefs.inc 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Stuff for drawing the BIOS logo.
;

;
; Copyright (C) 2004-2024 Oracle and/or its affiliates.
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


;; Switches back to default CPU mode.
SET_DEFAULT_CPU macro
 if VBOX_BIOS_CPU eq 8086
        .8086
 elseif VBOX_BIOS_CPU eq 80186
        .186
 elseif VBOX_BIOS_CPU eq 80286
        .286
 elseif VBOX_BIOS_CPU eq 80386
        .386
 else
  .errnz 1, Unsupported target CPU value: VBOX_BIOS_CPU
        .386
 endif
endm


;; Switches back to default CPU mode, max 286.
SET_DEFAULT_CPU_286 macro
 if VBOX_BIOS_CPU eq 8086
        .8086
 elseif VBOX_BIOS_CPU eq 80186
        .186
 else
        .286
 endif
endm


;;
; Pretends to do a call by pushing the return address and jumping
; to the function.
;
; This is slightly problematic on 8086 since it doesn't support pushing
; word immediates. OTOH, it support pushing memory, so we'll do that
; instead when targeting 8086.
;
if 0 ;; this crap crashes wasm, makes it call exit(0), or similar weird stuff.
     ;; switched to a simpler approach where I define the one return address variable myself.
DO_JMP_CALL     macro calling, returning
 if VBOX_BIOS_CPU gt 8086
        push    returning
        jmp     calling
 else
        push    word ptr cs:[%jmp_call_addr_&returning]
        jmp     calling
;public jmp_call_addr_&returning
%jmp_call_addr_&returning: dw offset retaddr
 endif
endm

DO_JMP_CALL_AGAIN macro calling, returning
 if VBOX_BIOS_CPU gt 8086
        push    returning
        jmp     calling
 else
        push    word ptr cs:[%jmp_call_addr_&returning]
        jmp     calling
 endif
endm

else  ; Simplified.
DO_JMP_CALL_EX  macro calling, returning, returning_ptr_var
 if VBOX_BIOS_CPU gt 8086
        push    returning
        jmp     calling
  else
        push    word ptr cs:[returning_ptr_var]
        jmp     calling
  endif
endm

endif ; Simplified.


;; For handling the pusha instruction depending on target CPU.
DO_pusha        macro
 if VBOX_BIOS_CPU gt 8086
        pusha
 else
        push    ax
        push    cx
        push    dx
        push    bx
        push    sp
        push    bp
        push    si
        push    di
 endif
endm


;; For handling the popad/popa instruction depending on target CPU.
DO_popa         macro
 if VBOX_BIOS_CPU gt 8086
        popa
 else
        pop     di
        pop     si
        pop     bp
        pop     bx ;pop sp
        pop     bx
        pop     dx
        pop     cx
        pop     ax
 endif
endm


;; For handling the pushad/pusha instruction depending on target CPU.
DO_PUSHAD       macro
 if VBOX_BIOS_CPU ge 80386
        pushad
 elseif VBOX_BIOS_CPU gt 8086
        pusha
 else
        push    ax
        push    cx
        push    dx
        push    bx
        push    sp
        push    bp
        push    si
        push    di
 endif
endm


;; For handling the popad/popa instruction depending on target CPU.
DO_POPAD        macro
 if VBOX_BIOS_CPU ge 80386
        popad
 elseif VBOX_BIOS_CPU gt 8086
        popa
 else
        pop     di
        pop     si
        pop     bp
        pop     bx ;pop sp
        pop     bx
        pop     dx
        pop     cx
        pop     ax
 endif
endm


;; ASSUMES working stack.
DO_shr          macro reg, count
 if VBOX_BIOS_CPU ge 80186
        shr     reg, count
 else
  if count ge 6
        push    cx
        mov     cl, count
        shr     reg, cl
        pop     cx
  else
   if count ge 5
        shr     reg, 1
   endif
   if count ge 4
        shr     reg, 1
   endif
   if count ge 3
        shr     reg, 1
   endif
   if count ge 2
        shr     reg, 1
   endif
   if count ge 1
        shr     reg, 1
   endif
  endif
 endif
endm


;; ASSUMES working stack.
DO_shl          macro reg, count
 if VBOX_BIOS_CPU ge 80186
        shl     reg, count
 else
  if count ge 6
        push    cx
        mov     cl, count
        shl     reg, cl
        pop     cx
  else
   if count ge 5
        shl     reg, 1
   endif
   if count ge 4
        shl     reg, 1
   endif
   if count ge 3
        shl     reg, 1
   endif
   if count ge 2
        shl     reg, 1
   endif
   if count ge 1
        shl     reg, 1
   endif
  endif
 endif
endm




;; Adds a special label that will have its address checked after linking.
BIOSORG_CHECK macro addr
public biosorg_check_at_&addr
biosorg_check_at_&addr:
        endm

;; Adds a special label that will have its address checked after linking.
BIOSORG_CHECK_BEFORE macro addr
public biosorg_check_before_or_at_&addr
biosorg_check_before_or_at_&addr:
        endm

