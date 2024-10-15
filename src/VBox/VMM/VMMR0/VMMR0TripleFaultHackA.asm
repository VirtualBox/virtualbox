; $Id: VMMR0TripleFaultHackA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VMM - Host Context Ring 0, Assembly Code for The Triple Fault Debugging Hack.
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

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "VBox/asmdefs.mac"


BEGINCODE
GLOBALNAME vmmR0TripleFaultHackStart
%define CALC_ADDR(a_Addr) ( (a_Addr) - NAME(vmmR0TripleFaultHackStart) + 07000h )


BITS 16
BEGINPROC vmmR0TripleFaultHack
    ; Set up stack.
    cli                                 ; paranoia
    mov     sp, 0ffffh
    mov     ax, cs
    mov     ss, ax
    mov     ds, ax
    mov     es, ax
    cld                                 ; paranoia

    COM_INIT

    ; Beep and say hello to the post-reset world.
    call    NAME(vmmR0TripleFaultHackBeep)
    mov     si, CALC_ADDR(.s_szHello)
    call    NAME(vmmR0TripleFaultHackPrint)

.forever:
    hlt
    jmp     .forever

.s_szHello:
    db      'Hello post-reset world', 0ah, 0dh, 0
ENDPROC   vmmR0TripleFaultHack

;; ds:si = zero terminated string.
BEGINPROC   vmmR0TripleFaultHackPrint
    push    eax
    push    esi

.outer_loop:
    lodsb
    cmp     al, 0
    je      .done
    call    NAME(vmmR0TripleFaultHackPrintCh)
    jmp     .outer_loop

.done:
    pop     esi
    pop     eax
    ret
ENDPROC     vmmR0TripleFaultHackPrint


;; al = char to print
BEGINPROC   vmmR0TripleFaultHackPrintCh
    push    eax
    push    edx
    push    ecx
    mov     ah, al                      ; save char.

    ; Wait for status.
    mov     ecx, _1G
    mov     dx, VBOX_UART_BASE + 5
.pre_status:
    in      al, dx
    test    al, 20h
    jnz     .put_char
    dec     ecx
    jnz     .pre_status

    ; Write the character.
.put_char:
    mov     al, ah
    mov     dx, VBOX_UART_BASE
    out     dx, al

    ; Wait for status.
    mov     ecx, _1G
    mov     dx, VBOX_UART_BASE + 5
.post_status:
    in      al, dx
    test    al, 20h
    jnz     .done
    dec     ecx
    jnz     .post_status

.done:
    pop     ecx
    pop     edx
    pop     eax
    ret
ENDPROC     vmmR0TripleFaultHackPrintCh

;;
; make a 440 BEEP.
BEGINPROC   vmmR0TripleFaultHackBeep
    push    eax
    push    edx
    push    ecx

    ; program PIT(1) and stuff.
    mov     al, 10110110b
    out     43h, al
    mov     ax, 0a79h                   ; A = 440
    out     42h, al
    shr     ax, 8
    out     42h, al

    in      al, 61h
    or      al, 3
    out     61h, al

    ; delay
    mov     ecx, _1G
.delay:
    inc     ecx
    dec     ecx
    dec     ecx
    jnz     .delay

    ; shut up speaker.
    in      al, 61h
    and     al, 11111100b
    out     61h, al

.done:
    pop     ecx
    pop     edx
    pop     eax
    ret
ENDPROC     vmmR0TripleFaultHackBeep


GLOBALNAME vmmR0TripleFaultHackEnd




;;;
;;;
;;;
;;;
;;;



BITS ARCH_BITS

BEGINPROC vmmR0TripleFaultHackKbdWait
        push    xAX

.check_status:
        in      al, 64h
        test    al, 1                   ; KBD_STAT_OBF
        jnz     .read_data_and_status
        test    al, 2                   ; KBD_STAT_IBF
        jnz     .check_status

        pop     xAX
        ret

.read_data_and_status:
        in      al, 60h
        jmp     .check_status
ENDPROC vmmR0TripleFaultHackKbdWait


BEGINPROC vmmR0TripleFaultHackKbdRead
        out     64h, al                 ; Write the command.

.check_status:
        in      al, 64h
        test    al, 1                   ; KBD_STAT_OBF
        jz      .check_status

        in      al, 60h                 ; Read the data.
        ret
ENDPROC   vmmR0TripleFaultHackKbdRead


BEGINPROC vmmR0TripleFaultHackKbdWrite
        out     64h, al                 ; Write the command.
        call    NAME(vmmR0TripleFaultHackKbdWait)

        xchg    al, ah
        out     60h, al                 ; Write the data.
        call    NAME(vmmR0TripleFaultHackKbdWait)
        xchg    al, ah

        ret
ENDPROC   vmmR0TripleFaultHackKbdWrite



BEGINPROC vmmR0TripleFaultHackTripleFault
    push    xAX
    push    xSI

    xor     eax, eax
    push    xAX
    push    xAX
    push    xAX
    push    xAX

    COM_CHAR 'B'
    COM_CHAR 'y'
    COM_CHAR 'e'
    COM_CHAR '!'
    COM_CHAR 0ah
    COM_CHAR 0dh


    ;call    NAME(vmmR0TripleFaultHackBeep32)
%if 1
    lidt    [xSP]
%elif 0
    in      al, 92h
    or      al, 1
    out     92h, al
    in      al, 92h
    cli
    hlt
%else
    mov     al, 0d0h                ; KBD_CCMD_READ_OUTPORT
    call    NAME(vmmR0TripleFaultHackKbdRead)
    mov     ah, 0feh
    and     ah, al
    mov     al, 0d1h                ; KBD_CCMD_WRITE_OUTPORT
    call    NAME(vmmR0TripleFaultHackKbdWrite)
    cli
    hlt
%endif
    int3

    pop     xAX
    pop     xAX
    pop     xAX
    pop     xAX

    pop     xSI
    pop     xAX
    ret
ENDPROC   vmmR0TripleFaultHackTripleFault

