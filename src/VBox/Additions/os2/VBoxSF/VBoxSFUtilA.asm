; $Id: VBoxSFUtilA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxSF - OS/2 Shared Folders Utility, Assembly code for calling 16-bit APIs.
;

;
; Copyright (c) 2007-2017 knut st. osmundsen <bird-src-spam@anduin.net>
;
; Permission is hereby granted, free of charge, to any person
; obtaining a copy of this software and associated documentation
; files (the "Software"), to deal in the Software without
; restriction, including without limitation the rights to use,
; copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the
; Software is furnished to do so, subject to the following
; conditions:
;
; The above copyright notice and this permission notice shall be
; included in all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
; OTHER DEALINGS IN THE SOFTWARE.
;



;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%define RT_INCL_16BIT_SEGMENTS
%include "iprt/asmdefs.mac"


;*********************************************************************************************************************************
;*  External symbols                                                                                                             *
;*********************************************************************************************************************************
BEGINCODE16
extern DOSQFILEMODE
IMPORT DOSQFILEMODE DOSCALLS 75


BEGINCODE
BEGINPROC CallDosQFileMode
        push    ebp
        mov     ebp, esp

        ;
        ; Make a LSS frame to ease switching back the stack.
        ;
        push    ss
        push    esp

        ;
        ; Create the PASCAL stackframe.
        ;

        ; Use tile algorithm to convert pointers.
        mov     eax, [ebp + 08h]        ; PCSZ    pszPath
        ror     eax, 16
        shl     ax, 3
        or      eax, 0007h
        rol     eax, 16
        push    eax

        ; Use the tiled algorithm to convert flat to 16:16 pointer.
        mov     ecx, [ebp + 0ch]        ; PUSHORT pfAttrib
        ror     ecx, 16
        shl     cx, 3
        or      ecx, 0007h
        rol     ecx, 16
        push    ecx

        mov     eax, [ebp + 10h]        ; ULONG   ulReserved
        push    eax

        ;
        ; Convert the stack in the same manner.
        ;
        movzx   edx, sp
        mov     eax, esp
        shr     eax, 16
        shl     eax, 3
        or      eax, 0007h
        push    eax
        push    edx
        lss     esp, [esp]

        ;jmp far dword .thunked_to_16bit wrt CODE16
        db      066h
        db      0eah
        dw      .thunked_to_16bit wrt CODE16
        dw      CODE16
BEGINCODE16
.thunked_to_16bit:
        call far DOSQFILEMODE

        ;jmp far dword NAME(%i %+ _32) wrt FLAT
        db      066h
        db      0eah
        dd      .thunked_back_to_32bit ;wrt FLAT
        dw      TEXT32 wrt FLAT
BEGINCODE
.thunked_back_to_32bit:
        lss     esp, [ds:ebp - 8]
        movzx   eax, ax
        leave
        ret
ENDPROC CallDosQFileMode

