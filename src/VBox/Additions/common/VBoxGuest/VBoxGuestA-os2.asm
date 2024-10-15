; $Id: VBoxGuestA-os2.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuest - OS/2 assembly file, the first file in the link.
;

;
; Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
;-----------------------------------------------------------------------------
; This code is based on:
;
; VBoxDrv - OS/2 assembly file, the first file in the link.
;
; Copyright (c) 2007-2010 knut st. osmundsen <bird-src-spam@anduin.net>
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


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_INCL_16BIT_SEGMENTS
%include "iprt/asmdefs.mac"
%include "iprt/err.mac"
%include "VBox/VBoxGuest.mac"


;*******************************************************************************
;* Structures and Typedefs                                                     *
;*******************************************************************************
;;
; Request packet header.
struc PKTHDR
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1
endstruc


;;
; Init request packet - input.
struc PKTINITIN
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1

    .data_1 resb 1
    .fpfnDevHlp resd 1
    .fpszArgs   resd 1
    .data_2 resb 1
endstruc

;;
; Init request packet - output.
struc PKTINITOUT
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1

    .cUnits resb 1                      ; block devs only.
    .cbCode16 resw 1
    .cbData16 resw 1
    .fpaBPBs  resd 1                    ; block devs only.
    .data_2 resb 1
endstruc

;;
; Open request packet.
struc PKTOPEN
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1
    .sfn    resw 1
endstruc

;;
; Close request packet.
struc PKTCLOSE
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1
    .sfn    resw 1
endstruc

;;
; IOCtl request packet.
struc PKTIOCTL
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1

    .cat    resb 1
    .fun    resb 1
    .pParm  resd 1
    .pData  resd 1
    .sfn    resw 1
    .cbParm resw 1
    .cbData resw 1
endstruc

;;
; Read/Write request packet
struc PKTRW
    .cb     resb 1
    .unit   resb 1
    .cmd    resb 1
    .status resw 1
    .res1   resd 1
    .link   resd 1

    .media  resb 1
    .PhysTrans resd 1
    .cbTrans resw 1
    .start  resd 1
    .sfn    resw 1
endstruc


;*******************************************************************************
;*  Defined Constants And Macros                                               *
;*******************************************************************************
; Some devhdr.inc stuff.
%define DEVLEV_3                0180h
%define DEV_30                  0800h
%define DEV_IOCTL               4000h
%define DEV_CHAR_DEV            8000h

%define DEV_16MB                0002h
%define DEV_IOCTL2              0001h

; Some dhcalls.h stuff.
%define DevHlp_VirtToLin        05bh
%define DevHlp_SAVE_MESSAGE     03dh
%define DevHlp_EOI              031h
%define DevHlp_SetIRQ           01bh
%define DevHlp_PhysToVirt       015h

; Fast IOCtl category, also defined in VBoxGuest.h
%define VBGL_IOCTL_CATEGORY_FAST    0c3h

;;
; Got some nasm/link trouble, so emit the stuff ourselves.
; @param    %1      Must be a GLOBALNAME.
%macro JMP32TO16 1
    ;jmp far dword NAME(%1) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(%1) wrt CODE16
    dw      CODE16
%endmacro

;;
; Got some nasm/link trouble, so emit the stuff ourselves.
; @param    %1      Must be a GLOBALNAME.
%macro JMP16TO32 1
    ;jmp far dword NAME(%1) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(%1) ;wrt FLAT
    dw      TEXT32 wrt FLAT
%endmacro


;*******************************************************************************
;* External Symbols                                                            *
;*******************************************************************************
segment CODE16
extern DOS16OPEN
extern DOS16CLOSE
extern DOS16WRITE
extern DOS16DEVIOCTL2
segment TEXT32
extern KernThunkStackTo32
extern KernThunkStackTo16

extern NAME(vgdrvOS2Init)
extern NAME(vgdrvOS2Open)
extern NAME(vgdrvOS2Close)
extern NAME(vgdrvOS2IOCtl)
extern NAME(vgdrvOS2IOCtlFast)
extern NAME(vgdrvOS2IDCConnect)
extern NAME(VGDrvOS2IDCService)
extern NAME(vgdrvOS2ISR)


segment DATA16

;;
; Device headers. The first one is the one we'll be opening and the
; latter is only used for 32-bit initialization.
GLOBALNAME g_VBoxGuestHdr1
    dw  NAME(g_VBoxGuestHdr2) wrt DATA16            ; NextHeader.off
    dw  DATA16                                      ; NextHeader.sel
    dw  DEVLEV_3 | DEV_30 | DEV_CHAR_DEV | DEV_IOCTL; SDevAtt
    dw  NAME(VGDrvOS2Entrypoint) wrt CODE16         ; StrategyEP
    dw  NAME(VGDrvOS2IDC) wrt CODE16                ; IDCEP
    db  'vboxgst$'                                  ; DevName
    dw  0                                           ; SDevProtCS
    dw  0                                           ; SDevProtDS
    dw  0                                           ; SDevRealCS
    dw  0                                           ; SDevRealDS
    dd  DEV_16MB | DEV_IOCTL2                       ; SDevCaps

align 4
GLOBALNAME g_VBoxGuestHdr2
    dd  0ffffffffh                                  ; NextHeader (NIL)
    dw  DEVLEV_3 | DEV_30 | DEV_CHAR_DEV            ; SDevAtt
    dw  NAME(vgdrvOS2InitEntrypoint) wrt CODE16     ; StrategyEP
    dw  0                                           ; IDCEP
    db  'vboxgs1$'                                  ; DevName
    dw  0                                           ; SDevProtCS
    dw  0                                           ; SDevProtDS
    dw  0                                           ; SDevRealCS
    dw  0                                           ; SDevRealDS
    dd  DEV_16MB | DEV_IOCTL2                       ; SDevCaps


;; Tristate 32-bit initialization indicator [0 = need init, -1 = init failed, 1 init succeeded].
; Check in the open path of the primary driver. The secondary driver will
; open the primary one during it's init and thereby trigger the 32-bit init.
GLOBALNAME g_fInitialized
    db 0

align 4
;; Pointer to the device helper service routine
; This is set during the initialization of the 2nd device driver.
GLOBALNAME g_fpfnDevHlp
    dd 0


;; vgdrvFindAdapter Output
; @{

;; The MMIO base of the VMMDev.
GLOBALNAME g_PhysMMIOBase
    dd 0
;; The size of the MMIO memory of the VMMDev.
GLOBALNAME g_cbMMIO
    dd 0
;; The I/O port base of the VMMDev.
GLOBALNAME g_IOPortBase
    dw 0
;; The VMMDev Interrupt Line.
GLOBALNAME g_bInterruptLine
    db 0
;; The PCI bus number returned by Find PCI Device.
GLOBALNAME g_bPciBusNo
    db 0
;; The PCI Device No / Function Number returned by Find PCI Device.
; (The upper 5 bits is the number, and the lower 3 the function.)
GLOBALNAME g_bPciDevFunNo
    db 0
;; Flag that is set by the vboxgst$ init routine if VMMDev was found.
; Both init routines must refuse loading the driver if the
; device cannot be located.
GLOBALNAME g_fFoundAdapter
    db 0
;; @}


%ifdef DEBUG_READ
;; Where we write to the log.
GLOBALNAME g_offLogHead
    dw 0
;; Where we read from the log.
GLOBALNAME g_offLogTail
    dw 0
;; The size of the log. (power of two!)
%define LOG_SIZE 16384
GLOBALNAME g_cchLogMax
    dw LOG_SIZE
;; The log buffer.
GLOBALNAME g_szLog
    times LOG_SIZE db 0
%endif ; DEBUG_READ


;
; The init data.
;
segment DATA16_INIT
GLOBALNAME g_InitDataStart

;; Far pointer to the device argument.
g_fpszArgs:
    dd      0

%if 0
;; Message table for the Save_Message device helper.
GLOBALNAME g_MsgTab
    dw  1178                                        ; MsgId - 'MSG_REPLACEMENT_STRING'.
    dw  1                                           ; cMsgStrings
    dw  NAME(g_szInitText)                          ; MsgStrings[0]
    dw  seg NAME(g_szInitText)
%else
;; Far pointer to DOS16WRITE (corrected set before called).
; Just a temporary hack to work around a wlink issue.
GLOBALNAME g_fpfnDos16Write
    dw  DOS16WRITE
    dw  seg DOS16WRITE
%endif

;; Size of the text currently in the g_szInitText buffer.
GLOBALNAME g_cchInitText
    dw      0
;; The max size of text that can fit into the g_szInitText buffer.
GLOBALNAME g_cchInitTextMax
    dw      512
;; The init text buffer.
GLOBALNAME g_szInitText
    times 512 db 0

;; Message string that's written on failure.
g_achLoadFailureMsg1:
    db      0dh,0ah,'VBoxGuest: load failure no. '
g_cchLoadFailureMsg1 EQU $ - g_achLoadFailureMsg1
g_achLoadFailureMsg2:
    db      '!',0dh,0ah
g_cchLoadFailureMsg2 EQU $ - g_achLoadFailureMsg2


;
; The 16-bit code segment.
;
segment CODE16


;;
; The strategy entry point (vboxdrv$).
;
; ss:bx -> request packet
; ds:si -> device header
;
; Can clobber any registers it likes except SP.
;
BEGINPROC VGDrvOS2Entrypoint
    push    ebp
    mov     ebp, esp
    push    es                                      ; bp - 2
    push    bx                                      ; bp - 4
    and     sp, 0fffch

    ;
    ; Check for the most frequent first.
    ;
    cmp     byte [es:bx + PKTHDR.cmd], 10h          ; Generic IOCtl
    jne near vgdrvOS2EP_NotGenIOCtl


    ;
    ; Generic I/O Control Request.
    ;
vgdrvOS2EP_GenIOCtl:

    ; Fast IOCtl?
    cmp     byte [es:bx + PKTIOCTL.cat], VBGL_IOCTL_CATEGORY_FAST
    jne     vgdrvOS2EP_GenIOCtl_Other

    ;
    ; Fast IOCtl.
    ;   DECLASM(int) vgdrvOS2IOCtlFast(uint16_t sfn, uint8_t iFunction, uint16_t *pcbParm)
    ;
vgdrvOS2EP_GenIOCtl_Fast:
    mov     ax, [es:bx + PKTIOCTL.pData + 2]        ; LDT selector to flat address.
    shr     ax, 3
    shl     eax, 16
    mov     ax, [es:bx + PKTIOCTL.pData]
    push    eax                                     ; 08h - pointer to the rc buffer.

    ; function.
    movzx   edx, byte [es:bx + PKTIOCTL.fun]
    push    edx                                     ; 04h

    ; system file number.
    movzx   eax, word [es:bx + PKTIOCTL.sfn]
    push    eax                                     ; 00h

    JMP16TO32 vgdrvOS2EP_GenIOCtl_Fast_32
segment TEXT32
GLOBALNAME vgdrvOS2EP_GenIOCtl_Fast_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code (don't cleanup the stack).
    call    NAME(vgdrvOS2IOCtlFast)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    JMP32TO16 vgdrvOS2EP_GenIOCtl_Fast_32
segment CODE16
GLOBALNAME vgdrvOS2EP_GenIOCtl_Fast_16

    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near vgdrvOS2EP_GeneralFailure

    ; setup output stuff.
    mov     edx, esp
    mov     eax, [ss:edx + 0ch]                     ; output sizes.
    mov     [es:bx + PKTIOCTL.cbParm], eax          ; update cbParm and cbData.
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.

    mov     sp, bp
    pop     ebp
    retf

    ;
    ; Other IOCtl (slow)
    ;
vgdrvOS2EP_GenIOCtl_Other:
    mov     eax, [es:bx +  PKTIOCTL.cbParm]         ; Load cbParm and cbData
    push    eax                                     ; 1eh - in/out data size.
                                                    ; 1ch - in/out parameter size.
    push    edx                                     ; 18h - pointer to data size (filled in later).
    push    ecx                                     ; 14h - pointer to param size (filled in later).

    ; pData (convert to flat 32-bit)
    mov     ax,  word [es:bx + PKTIOCTL.pData + 2]  ; selector
    cmp     ax, 3                                   ; <= 3 -> nil selector...
    jbe     .no_data
    movzx   esi, word [es:bx + PKTIOCTL.pData]      ; offset
    mov     dl, DevHlp_VirtToLin
    call far [NAME(g_fpfnDevHlp)]
    jc near vgdrvOS2EP_GeneralFailure
    jmp     .finish_data
.no_data:
    xor     eax, eax
.finish_data:
    push    eax                                     ; 10h

    ; pParm (convert to flat 32-bit)
    mov     ax,  word [es:bx + PKTIOCTL.pParm + 2]  ; selector
    cmp     ax, 3                                   ; <= 3 -> nil selector...
    jbe     .no_parm
    movzx   esi, word [es:bx + PKTIOCTL.pParm]      ; offset
    mov     dl, DevHlp_VirtToLin
    call far [NAME(g_fpfnDevHlp)]
    jc near vgdrvOS2EP_GeneralFailure
    jmp     .finish_parm
.no_parm:
    xor     eax, eax
.finish_parm:
    push    eax                                     ; 0ch

    ; function.
    movzx   edx, byte [es:bx + PKTIOCTL.fun]
    push    edx                                     ; 08h

    ; category.
    movzx   ecx, byte [es:bx + PKTIOCTL.cat]
    push    ecx                                     ; 04h

    ; system file number.
    movzx   eax, word [es:bx + PKTIOCTL.sfn]
    push    eax                                     ; 00h

    JMP16TO32 vgdrvOS2EP_GenIOCtl_Other_32
segment TEXT32
GLOBALNAME vgdrvOS2EP_GenIOCtl_Other_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; update in/out parameter pointers
    lea     eax, [esp + 1ch]
    mov     [esp + 14h], eax
    lea     edx, [esp + 1eh]
    mov     [esp + 18h], edx

    ; call the C code (don't cleanup the stack).
    call    NAME(vgdrvOS2IOCtl)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    JMP32TO16 vgdrvOS2EP_GenIOCtl_Other_16
segment CODE16
GLOBALNAME vgdrvOS2EP_GenIOCtl_Other_16

    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near vgdrvOS2EP_GeneralFailure

    ; setup output stuff.
    mov     edx, esp
    mov     eax, [ss:edx + 1ch]                     ; output sizes.
    mov     [es:bx + PKTIOCTL.cbParm], eax          ; update cbParm and cbData.
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.

    mov     sp, bp
    pop     ebp
    retf


    ;
    ; Less Performance Critical Requests.
    ;
vgdrvOS2EP_NotGenIOCtl:
    cmp     byte [es:bx + PKTHDR.cmd], 0dh          ; Open
    je      vgdrvOS2EP_Open
    cmp     byte [es:bx + PKTHDR.cmd], 0eh          ; Close
    je      vgdrvOS2EP_Close
    cmp     byte [es:bx + PKTHDR.cmd], 00h          ; Init
    je      vgdrvOS2EP_Init
%ifdef DEBUG_READ
    cmp     byte [es:bx + PKTHDR.cmd], 04h          ; Read
    je near vgdrvOS2EP_Read
%endif
    jmp near vgdrvOS2EP_NotSupported


    ;
    ; Open Request. w/ ring-0 init.
    ;
vgdrvOS2EP_Open:
    cmp     byte [NAME(g_fInitialized)], 1
    jne     vgdrvOS2EP_OpenOther

    ; First argument, the system file number.
    movzx   eax, word [es:bx + PKTOPEN.sfn]
    push    eax

    JMP16TO32 vgdrvOS2EP_Open_32
segment TEXT32
GLOBALNAME vgdrvOS2EP_Open_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(vgdrvOS2Open)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    JMP32TO16 vgdrvOS2EP_Open_16
segment CODE16
GLOBALNAME vgdrvOS2EP_Open_16

    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near vgdrvOS2EP_GeneralFailure
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    jmp near vgdrvOS2EP_Done

    ; Initializing or failed init?
vgdrvOS2EP_OpenOther:
    cmp     byte [NAME(g_fInitialized)], 0
    jne     vgdrvOS2EP_OpenFailed

    mov     byte [NAME(g_fInitialized)], -1
    call    NAME(vgdrvRing0Init)
    cmp     byte [NAME(g_fInitialized)], 1
    je      vgdrvOS2EP_Open

vgdrvOS2EP_OpenFailed:
    mov     word [es:bx + PKTHDR.status], 0810fh    ; error, done, init failed.
    jmp near vgdrvOS2EP_Done


    ;
    ; Close Request.
    ;
vgdrvOS2EP_Close:
    ; First argument, the system file number.
    movzx   eax, word [es:bx + PKTOPEN.sfn]
    push    eax

    JMP16TO32 vgdrvOS2EP_Close_32
segment TEXT32
GLOBALNAME vgdrvOS2EP_Close_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(vgdrvOS2Close)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    JMP32TO16 vgdrvOS2EP_Close_16
segment CODE16
GLOBALNAME vgdrvOS2EP_Close_16

    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near vgdrvOS2EP_GeneralFailure
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    jmp near vgdrvOS2EP_Done


    ;
    ; Init Request.
    ; Find the VMMDev adapter so we can unload the driver (and avoid trouble) if not found.
    ;
vgdrvOS2EP_Init:
    call    NAME(vgdrvFindAdapter)
    test    ax, ax
    jz      .ok
    mov     word [es:bx + PKTHDR.status], 0810fh    ; error, done, init failed.
    call    NAME(vgdrvOS2InitFlushText)
    jmp     .next
.ok:
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
.next:
    mov     byte [es:bx + PKTINITOUT.cUnits], 0
    mov     word [es:bx + PKTINITOUT.cbCode16], NAME(g_InitCodeStart) wrt CODE16
    mov     word [es:bx + PKTINITOUT.cbData16], NAME(g_InitDataStart) wrt DATA16
    mov     dword [es:bx + PKTINITOUT.fpaBPBs], 0
    jmp near vgdrvOS2EP_Done


%ifdef DEBUG_READ
    ;
    ; Read Request.
    ; Return log data.
    ;
vgdrvOS2EP_Read:
    ; Any log data available?
    xor     dx, dx
    mov     ax, [NAME(g_offLogTail)]
    cmp     ax, [NAME(g_offLogHead)]
    jz near .log_done

    ; create a temporary mapping of the physical buffer. Docs claims it trashes nearly everything...
    push    ebp
    mov     cx, [es:bx + PKTRW.cbTrans]
    push    cx
    mov     ax, [es:bx + PKTRW.PhysTrans + 2]
    mov     bx, [es:bx + PKTRW.PhysTrans]
    mov     dh, 1
    mov     dl, DevHlp_PhysToVirt
    call far [NAME(g_fpfnDevHlp)]
    pop     bx                                      ; bx = cbTrans
    pop     ebp
    jc near .log_phystovirt_failed
                                                    ; es:di -> the output buffer.

    ; setup the copy operation.
    mov     ax, [NAME(g_offLogTail)]
    xor     dx, dx                                  ; dx tracks the number of bytes copied.
.log_loop:
    mov     cx, [NAME(g_offLogHead)]
    cmp     ax, cx
    je      .log_done
    jb      .log_loop_before
    mov     cx, LOG_SIZE
.log_loop_before:                                   ; cx = end offset
    sub     cx, ax                                  ; cx = sequential bytes to copy.
    cmp     cx, bx
    jbe     .log_loop_min
    mov     cx, bx                                  ; output buffer is smaller than available data.
.log_loop_min:
    mov     si, NAME(g_szLog)
    add     si, ax                                  ; ds:si -> the log buffer.
    add     dx, cx                                  ; update output counter
    add     ax, cx                                  ; calc new offLogTail
    and     ax, LOG_SIZE - 1
    rep movsb                                       ; do the copy
    mov     [NAME(g_offLogTail)], ax                ; commit the read.
    jmp     .log_loop

.log_done:
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    mov     word [es:bx + PKTRW.cbTrans], dx
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    jmp near vgdrvOS2EP_Done

.log_phystovirt_failed:
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    jmp     vgdrvOS2EP_GeneralFailure
%endif ; DEBUG_READ


    ;
    ; Return 'unknown command' error.
    ;
vgdrvOS2EP_NotSupported:
    mov     word [es:bx + PKTHDR.status], 08103h    ; error, done, unknown command.
    jmp     vgdrvOS2EP_Done

    ;
    ; Return 'general failure' error.
    ;
vgdrvOS2EP_GeneralFailure:
    mov     word [es:bx + PKTHDR.status], 0810ch    ; error, done, general failure.
    jmp     vgdrvOS2EP_Done

    ;
    ; Non-optimized return path.
    ;
vgdrvOS2EP_Done:
    mov     sp, bp
    pop     ebp
    retf
ENDPROC VGDrvOS2Entrypoint


;;
; The helper device entry point.
;
; This is only used to do the DosOpen on the main driver so we can
; do ring-3 init and report failures.
;
GLOBALNAME vgdrvOS2InitEntrypoint
    ; The only request we're servicing is the 'init' one.
    cmp     word [es:bx + PKTHDR.cmd], 0
    je near NAME(vgdrvOS2InitEntrypointServiceInitReq)

    ; Ok, it's not the init request, just fail it.
    mov     word [es:bx + PKTHDR.status], 08103h    ; error, done, unknown command.
    retf


;;
; The OS/2 IDC entry point.
;
; This is only used to setup connection, the returned structure
; will provide the entry points that we'll be using.
;
; @cproto  void far __cdecl VGDrvOS2IDC(VBOXGUESTOS2IDCCONNECT far *fpConnectInfo);
;
; @param   fpConnectInfo   [bp + 8]     Pointer to an VBOXGUESTOS2IDCCONNECT structure.
;
GLOBALNAME VGDrvOS2IDC
    push    ebp                         ; bp -  0h
    mov     ebp, esp
    ; save everything we might touch.
    push    es                          ; bp -  2h
    push    ds                          ; bp -  4h
    push    eax                         ; bp -  8h
    push    ebx                         ; bp - 0ch
    push    ecx                         ; bp - 10h
    push    edx                         ; bp - 14h
    and     sp, 0fffch

    JMP16TO32 VGDrvOS2IDC_32
segment TEXT32
GLOBALNAME VGDrvOS2IDC_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(vgdrvOS2IDCConnect)

    ;
    ; Load the return buffer address into ds:ebx and setup the buffer.
    ; (eax == u32Session)
    ;
    mov     cx, [ebp + 08h + 2]
    mov     ds, cx
    movzx   ebx, word [ebp + 08h]

    mov     dword [ebx + VBGLOS2ATTACHDD.u32Version       ], VBGL_IOC_VERSION
    mov     dword [ebx + VBGLOS2ATTACHDD.u32Session       ], eax
    mov     dword [ebx + VBGLOS2ATTACHDD.pfnServiceEP     ], NAME(VGDrvOS2IDCService)
    mov     word  [ebx + VBGLOS2ATTACHDD.fpfnServiceEP    ], NAME(VGDrvOS2IDCService16) wrt CODE16
    mov     word  [ebx + VBGLOS2ATTACHDD.fpfnServiceEP + 2], CODE16
    mov     word  [ebx + VBGLOS2ATTACHDD.fpfnServiceAsmEP ], NAME(VGDrvOS2IDCService16Asm) wrt CODE16
    mov     word  [ebx + VBGLOS2ATTACHDD.fpfnServiceAsmEP+2],CODE16

    mov     ax, DATA32 wrt FLAT
    mov     ds, ax

    ; switch back the stack.
    call    KernThunkStackTo16

    JMP32TO16 VGDrvOS2IDC_16
segment CODE16
GLOBALNAME VGDrvOS2IDC_16

    ; restore.
    lea     sp, [bp - 14h]
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     ds
    pop     es
    pop     ebp
    retf
ENDPROC VGDrvOS2IDC


;;
; The 16-bit IDC entry point, cdecl.
;
; All this does is thunking the request into something that fits
; the 32-bit IDC service routine.
;
;
; @returns VBox status code.
; @param   u32Session          bp +  8h - The above session handle.
; @param   iFunction           bp + 0ch - The requested function.
; @param   fpReqHdr            bp + 0eh - The input/output data buffer. The caller ensures that this
;                                         cannot be swapped out, or that it's acceptable to take a
;                                         page in fault in the current context. If the request doesn't
;                                         take input or produces output, passing NULL is okay.
; @param   cbReq               bp + 12h - The size of the data buffer.
;
; @cproto long far __cdecl VGDrvOS2IDCService16(uint32_t u32Session, uint16_t iFunction, void far *fpReqHdr, uint16_t cbReq);
;
GLOBALNAME VGDrvOS2IDCService16
    push    ebp                         ; bp -  0h
    mov     ebp, esp
    push    es                          ; bp -  2h
    push    ds                          ; bp -  4h
    push    ecx                         ; bp -  8h
    push    edx                         ; bp - 0ch
    push    esi                         ; bp - 10h
    and     sp, 0fffch                  ; align the stack.

    ; locals
    push    dword 0                     ; esp + 18h (dd): cbDataReturned

    ; load our ds (for g_fpfnDevHlp).
    mov     ax, DATA16
    mov     ds, ax

    ;
    ; Create the call frame before switching.
    ;
    movzx   ecx, word [bp + 12h]
    push    ecx                         ; esp + 10h:    cbData

    ; thunk data argument if present.
    mov     ax,  [bp + 0eh + 2]         ; selector
    cmp     ax, 3                       ; <= 3 -> nil selector...
    jbe     .no_data
    movzx   esi, word [bp + 0eh]        ; offset
    mov     dl, DevHlp_VirtToLin
    call far [NAME(g_fpfnDevHlp)]
    jc near VGDrvOS2IDCService16_InvalidPointer
    jmp     .finish_data
.no_data:
    xor     eax, eax
.finish_data:
    push    eax                         ; esp + 08h:    pvData
    movzx   edx, word [bp + 0ch]
    push    edx                         ; esp + 04h:    iFunction
    mov     ecx, [bp + 08h]
    push    ecx                         ; esp + 00h:    u32Session

    JMP16TO32 VGDrvOS2IDCService16_32
segment TEXT32
GLOBALNAME VGDrvOS2IDCService16_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code (don't cleanup the stack).
    call    NAME(VGDrvOS2IDCService)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    JMP32TO16 VGDrvOS2IDCService16_16
segment CODE16
GLOBALNAME VGDrvOS2IDCService16_16

VGDrvOS2IDCService16_Done:
    lea     sp, [bp - 10h]
    pop     esi
    pop     edx
    pop     ecx
    pop     ds
    pop     es
    pop     ebp
    retf

VGDrvOS2IDCService16_InvalidPointer:
    mov     ax, VERR_INVALID_POINTER
    jmp     VGDrvOS2IDCService16_Done
ENDPROC VGDrvOS2IDCService16


;;
; The 16-bit IDC entry point, register based.
;
; This is just a wrapper around VGDrvOS2IDCService16 to simplify
; calls from 16-bit assembly code.
;
; @returns ax: VBox status code; cx: The amount of data returned.
;
; @param   u32Session          eax   - The above session handle.
; @param   iFunction           dl    - The requested function.
; @param   pvData              es:bx - The input/output data buffer.
; @param   cbData              cx    - The size of the data buffer.
;
GLOBALNAME VGDrvOS2IDCService16Asm
    push    ebp                         ; bp - 0h
    mov     ebp, esp
    push    edx                         ; bp - 4h

    push    cx                          ; cbData
    push    es
    xor     dh, dh
    push    dx
    push    eax
    call    NAME(VGDrvOS2IDCService16)

    mov     cx, [es:bx + VBGLREQHDR.cbOut]

    mov     edx, [bp - 4]
    mov     esp, ebp
    pop     ebp
    retf
ENDPROC VGDrvOS2IDCService16Asm



;;
; The 16-bit interrupt service routine.
;
; OS/2 saves all registers according to the docs, although it doesn't say whether
; this includes the 32-bit parts. Since it doesn't cost much to be careful, save
; everything.
;
; @returns  CF=0 if it's our interrupt, CF=1 it it isn't.
;
;
GLOBALNAME vgdrvOS2ISR16
    push    ebp
    mov     ebp, esp
    pushf                               ; bp - 02h
    cli
    push    eax                         ; bp - 06h
    push    edx                         ; bp - 0ah
    push    ebx                         ; bp - 0eh
    push    ds                          ; bp - 10h
    push    es                          ; bp - 12h
    push    ecx                         ; bp - 16h
    push    esi                         ; bp - 1ah
    push    edi                         ; bp - 1eh

    and     sp, 0fff0h                  ; align the stack (16-bytes make GCC extremely happy).

    JMP16TO32 vgdrvOS2ISR16_32
segment TEXT32
GLOBALNAME vgdrvOS2ISR16_32

    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax

    call    KernThunkStackTo32

    call    NAME(vgdrvOS2ISR)
    mov     ebx, eax

    call    KernThunkStackTo16

    JMP32TO16 vgdrvOS2ISR16_16
segment CODE16
GLOBALNAME vgdrvOS2ISR16_16

    lea     sp, [bp - 1eh]
    pop     edi
    pop     esi
    pop     ecx
    pop     es
    pop     ds
    test    bl, 0ffh
    jnz     .our
    pop     ebx
    pop     edx
    pop     eax
    popf
    pop     ebp
    stc
    retf

    ;
    ; Do EIO.
    ;
.our:
    mov     al, [NAME(g_bInterruptLine)]
    mov     dl, DevHlp_EOI
    call far [NAME(g_fpfnDevHlp)]

    pop     ebx
    pop     edx
    pop     eax
    popf
    pop     ebp
    clc
    retf
ENDPROC vgdrvOS2ISR16






;
; The 32-bit text segment.
;
segment TEXT32
;;
; 32-bit worker for registering the ISR.
;
; @returns  0 on success, some non-zero OS/2 error code on failure.
; @param    bIrq        [ebp + 8]       The IRQ number. (uint8_t)
;
GLOBALNAME vgdrvOS2DevHlpSetIRQ
    push    ebp
    mov     ebp, esp
    push    ebx
    push    ds

    call    KernThunkStackTo16

    movzx   ebx, byte [ebp + 8]         ; load bIrq into BX.

    JMP32TO16 vgdrvOS2DevHlpSetIRQ_16
segment CODE16
GLOBALNAME vgdrvOS2DevHlpSetIRQ_16

    mov     ax, DATA16                  ; for g_fpfnDevHlp.
    mov     ds, ax
    mov     ax, NAME(vgdrvOS2ISR16)     ; The devhlp assume it's relative to DS.
    mov     dh, 1                       ; 1 = shared
    mov     dl, DevHlp_SetIRQ
    call far [NAME(g_fpfnDevHlp)]
    jnc     .ok
    movzx   eax, ax
    or      eax, eax
    jnz     .go_back
    or      eax, 6
    jmp     .go_back
.ok:
    xor     eax, eax

.go_back:
    JMP16TO32 vgdrvOS2DevHlpSetIRQ_32
segment TEXT32
GLOBALNAME vgdrvOS2DevHlpSetIRQ_32

    pop     ds                          ; KernThunkStackTo32 ASSUMES flat DS and ES.

    mov     ebx, eax
    call    KernThunkStackTo32
    mov     eax, ebx

    pop     ebx
    pop     ebp
    ret
ENDPROC vgdrvOS2DevHlpSetIRQ




;
; The 16-bit init code.
;
segment CODE16_INIT
GLOBALNAME g_InitCodeStart

;; The device name for DosOpen.
g_szDeviceName:
    db  '\DEV\vboxgst$', 0

; icsdebug can't see where stuff starts otherwise. (kDevTest)
int3
int3
int3
int3
int3
int3

;;
; The Ring-3 init code.
;
BEGINPROC vgdrvOS2InitEntrypointServiceInitReq
    push    ebp
    mov     ebp, esp
    push    es                                      ; bp - 2
    push    sp                                      ; bp - 4
    push    -1                                      ; bp - 6: hfOpen
    push    0                                       ; bp - 8: usAction
    and     sp, 0fffch

    ; check for the init package.
    cmp     word [es:bx + PKTHDR.cmd], 0
    jne near .not_init

    ; check that we found the VMMDev.
    test    byte [NAME(g_fFoundAdapter)], 1
    jz near .done_err

    ;
    ; Copy the data out of the init packet.
    ;
    mov     eax, [es:bx + PKTINITIN.fpfnDevHlp]
    mov     [NAME(g_fpfnDevHlp)], eax
    mov     edx, [es:bx + PKTINITIN.fpszArgs]
    mov     [g_fpszArgs], edx

    ;
    ; Open the first driver, close it, and check status.
    ;

    ; APIRET _Pascal DosOpen(PSZ pszFname, PHFILE phfOpen, PUSHORT pusAction,
    ;                        ULONG ulFSize, USHORT usAttr, USHORT fsOpenFlags,
    ;                        USHORT fsOpenMode, ULONG ulReserved);
    push    seg g_szDeviceName                      ; pszFname
    push    g_szDeviceName
    push    ss                                      ; phfOpen
    lea     dx, [bp - 6]
    push    dx
    push    ss                                      ; pusAction
    lea     dx, [bp - 8]
    push    dx
    push    dword 0                                 ; ulFSize
    push    0                                       ; usAttr = FILE_NORMAL
    push    1                                       ; fsOpenFlags = FILE_OPEN
    push    00040h                                  ; fsOpenMode = OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY
    push    dword 0                                 ; ulReserved
    call far DOS16OPEN

    push    ax                                      ; Quickly flush any text.
    call    NAME(vgdrvOS2InitFlushText)
    pop     ax

    or      ax, ax
    jnz     .done_err

    ; APIRET  APIENTRY DosClose(HFILE hf);
    mov     cx, [bp - 6]
    push    cx
    call far DOS16CLOSE
    or      ax, ax
    jnz     .done_err                               ; This can't happen (I hope).

    ;
    ; Ok, we're good.
    ;
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    mov     byte [es:bx + PKTINITOUT.cUnits], 0
    mov     word [es:bx + PKTINITOUT.cbCode16], NAME(g_InitCodeStart) wrt CODE16
    mov     word [es:bx + PKTINITOUT.cbData16], NAME(g_InitDataStart) wrt DATA16
    mov     dword [es:bx + PKTINITOUT.fpaBPBs], 0
    jmp     .done

    ;
    ; Init failure.
    ;
.done_err:
    mov     word [es:bx + PKTHDR.status], 0810fh    ; error, done, init failed.
    mov     byte [es:bx + PKTINITOUT.cUnits], 0
    mov     word [es:bx + PKTINITOUT.cbCode16], 0
    mov     word [es:bx + PKTINITOUT.cbData16], 0
    mov     dword [es:bx + PKTINITOUT.fpaBPBs], 0
    jmp     .done

    ;
    ; Not init, return 'unknown command'.
    ;
.not_init:
    mov     word [es:bx + PKTHDR.status], 08103h    ; error, done, unknown command.
    jmp     .done

    ;
    ; Request done.
    ;
.done:
    mov     sp, bp
    pop     ebp
    retf
ENDPROC vgdrvOS2InitEntrypointServiceInitReq


;;
; The Ring-0 init code.
;
BEGINPROC vgdrvRing0Init
    push    es
    push    esi
    push    ebp
    mov     ebp, esp
    and     sp, 0fffch

    ;
    ; Thunk the argument string pointer first.
    ;
    movzx   esi, word [g_fpszArgs]                  ; offset
    mov     ax, [g_fpszArgs + 2]                    ; selector
    mov     dl, DevHlp_VirtToLin
    call far [NAME(g_fpfnDevHlp)]
    jc near vgdrvRing0Init_done                     ; eax is non-zero on failure (can't happen)
    push    eax                                     ; 00h - pszArgs (for vgdrvOS2Init).

    ;
    ; Do 16-bit init?
    ;


    ;
    ; Do 32-bit init
    ;
    JMP16TO32 vgdrvRing0Init_32
segment TEXT32
GLOBALNAME vgdrvRing0Init_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(vgdrvOS2Init)

    ; switch back the stack and reload ds.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    mov     dx, seg NAME(g_fInitialized)
    mov     ds, dx

    JMP32TO16 vgdrvRing0Init_16
segment CODE16_INIT
GLOBALNAME vgdrvRing0Init_16

    ; check the result and set g_fInitialized on success.
    or      eax, eax
    jnz     vgdrvRing0Init_done
    mov     byte [NAME(g_fInitialized)], 1

vgdrvRing0Init_done:
    mov     sp, bp
    pop     ebp
    pop     esi
    pop     es
    ret
ENDPROC vgdrvRing0Init


;;
; Flush any text in the text buffer.
;
BEGINPROC vgdrvOS2InitFlushText
    push    bp
    mov     bp, sp

    ; Anything in the buffer?
    mov     ax, [NAME(g_cchInitText)]
    or      ax, ax
    jz      .done

%if 1
    ; Write it to STDOUT.
    ; APIRET  _Pascal DosWrite(HFILE hf, PVOID pvBuf, USHORT cbBuf, PUSHORT pcbBytesWritten);
    push    ax                                      ; bp - 2 : cbBytesWritten
    mov     cx, sp
    push    1                                       ; STDOUT
    push    seg NAME(g_szInitText)                  ; pvBuf
    push    NAME(g_szInitText)
    push    ax                                      ; cbBuf
    push    ss                                      ; pcbBytesWritten
    push    cx
%if 0 ; wlink generates a non-aliased fixup here which results in 16-bit offset with the flat 32-bit selector.
    call far DOS16WRITE
%else
    ; convert flat pointer to a far pointer using the tiled algorithm.
    push    ds
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, g_pfnDos16Write wrt FLAT
    movzx   eax, word [eax + 2]                     ; High word of the flat address (in DATA32).
    shl     ax, 3
    or      ax, 0007h
    pop     ds
    mov     [NAME(g_fpfnDos16Write) + 2], ax        ; Update the selector (in DATA16_INIT).
    ; do the call
    call far [NAME(g_fpfnDos16Write)]
%endif

%else ; alternative workaround for the wlink issue.
    ; Use the save message devhlp.
    push    esi
    push    ebx
    xor     bx, bx
    mov     si, NAME(g_MsgTab)
    mov     dx, seg NAME(g_MsgTab)
    mov     ds, dx
    mov     dl, DevHlp_SAVE_MESSAGE
    call far [NAME(g_fpfnDevHlp)]
    pop     ebx
    pop     esi
%endif

    ; Empty the buffer.
    mov     word [NAME(g_cchInitText)], 0
    mov     byte [NAME(g_szInitText)], 0

.done:
    mov     sp, bp
    pop     bp
    ret
ENDPROC vgdrvOS2InitFlushText


;; The device name for DosOpen.
g_szOemHlpDevName:
    db  '\DEV\OEMHLP$', 0


;;
; Talks to OEMHLP$ about finding the VMMDev PCI adapter.
;
; On success g_fFoundAdapter is set to 1, and g_cbMMIO,
; g_PhysMMIOBase and g_IOPortBase are initialized with
; the PCI data.
;
; @returns  0 on success, non-zero on failure. (eax)
;
; @remark   ASSUMES DS:DATA16.
; @uses     nothing.
;
BEGINPROC vgdrvFindAdapter
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi
    push    ebp
    mov     ebp, esp
    push    -1                                      ; bp - 2: hfOpen
%define hfOpen      bp - 2
    push    0                                       ; bp - 4: usAction
%define usAction    bp - 4
    sub     sp, 20h                                 ; bp - 44: 32 byte parameter buffer.
%define abParm      bp - 24h
    sub     sp, 40h                                 ; bp - c4: 32 byte data buffer.
%define abData      bp - 44h

;; VBox stuff
%define VBOX_PCI_VENDORID       080eeh
%define VMMDEV_DEVICEID         0cafeh

;; OEMHLP$ stuff.
%define IOCTL_OEMHLP            80h
%define OEMHLP_PCI              0bh
%define PCI_FIND_DEVICE         1
%define PCI_READ_CONFIG         3

;; PCI stuff
%define PCI_INTERRUPT_LINE      03ch                ;;< 8-bit RW - Interrupt line.
%define PCI_BASE_ADDRESS_0      010h                ;;< 32-bit RW */
%define PCI_BASE_ADDRESS_1      014h                ;;< 32-bit RW */


%macro CallIOCtl 2
    ; APIRET _Pascal DosDevIOCtl2(PVOID pData, USHORT cbData, PVOID pParm,
    ;                             USHORT cbParm, USHORT usFun, USHORT usCategory,
    ;                             HFILE hDev);
    push    ss                                      ; pData
    lea     dx, [abData]
    push    dx
    push    %2                                      ; cbData

    push    ss                                      ; pParm
    lea     dx, [abParm]
    push    dx
    push    %1                                      ; cbParm
    push    OEMHLP_PCI                              ; usFun
    push    IOCTL_OEMHLP                            ; usCategory

    mov     ax, [hfOpen]                            ; hDev
    push    ax
    call far DOS16DEVIOCTL2

    ; check for error.
    test    ax, ax
    jnz near .done_err_close
    cmp     [abData + 0], byte 0
    jne near .done_err_close
%endmacro


    ;
    ; Open the OEMHLP$ driver.
    ;

    ; APIRET _Pascal DosOpen(PSZ pszFname, PHFILE phfOpen, PUSHORT pusAction,
    ;                        ULONG ulFSize, USHORT usAttr, USHORT fsOpenFlags,
    ;                        USHORT fsOpenMode, ULONG ulReserved);
    mov     di, '0'
    push    seg g_szOemHlpDevName                   ; pszFname
    push    g_szOemHlpDevName
    push    ss                                      ; phfOpen
    lea     dx, [hfOpen]
    push    dx
    push    ss                                      ; pusAction
    lea     dx, [usAction]
    push    dx
    push    dword 0                                 ; ulFSize
    push    0                                       ; usAttr = FILE_NORMAL
    push    1                                       ; fsOpenFlags = FILE_OPEN
    push    00040h                                  ; fsOpenMode = OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY
    push    dword 0                                 ; ulReserved
    call far DOS16OPEN
    or      ax, ax
    jnz near .done


    ;
    ; Send a PCI_FIND_DEVICE request.
    ;

    ; Initialize the parameter packet.
    mov     [abParm + 0], byte PCI_FIND_DEVICE      ; 0 - db - SubFunction Number
    mov     [abParm + 1], word VMMDEV_DEVICEID      ; 1 - dw - Device ID
    mov     [abParm + 3], word VBOX_PCI_VENDORID    ; 3 - dw - Vendor ID
    mov     [abParm + 5], byte 0                    ; 5 - db - (Device) Index

    ; Zero padd the data packet.
    mov     [abData + 0], dword 0

    mov     di, '1'
    CallIOCtl 6, 3

    mov     al, [abData + 1]                        ; 1 - db - Bus Number.
    mov     [NAME(g_bPciBusNo)], al
    mov     al, [abData + 2]                        ; 2 - db - DevFunc Number.
    mov     [NAME(g_bPciDevFunNo)], al

    ;
    ; Read the interrupt register (byte).
    ;
    mov     di, '2'
    mov     ax, PCI_INTERRUPT_LINE | 0100h
    call .NestedReadReg
    mov     [NAME(g_bInterruptLine)], al

    ;
    ; Read the first base address (dword), this shall must be in I/O space.
    ;
    mov     di, '3'
    mov     ax, PCI_BASE_ADDRESS_0 | 0400h
    call .NestedReadReg
    mov     di, '4'
    test    al, 1h                                  ; Test that it's an I/O space address.
    jz      .done_err_close
    mov     di, '5'
    test    eax, 0ffff0002h                         ; These shall all be 0 according to the specs.
    jnz     .done_err_close
    and     ax, 0fffeh
    mov     [NAME(g_IOPortBase)], ax

    ;
    ; Read the second base address (dword), this shall be in memory space if present.
    ;
    mov     di, '6'
    mov     ax, PCI_BASE_ADDRESS_1 | 0400h
    call .NestedReadReg
    mov     di, '7'
    test    al, 1h                                  ; Test that it's a memory space address.
    jnz     .done_err_close
    and     eax, 0fffffff0h
    mov     [NAME(g_PhysMMIOBase)], eax

    ;or      eax, eax
    ;jz      .done_success                             ; No memory region.
    ;; @todo If there is a simple way of determining the size do that, if
    ;        not we can easily handle it the code that does the actual mapping.


    ;
    ; Ok, we're good!
    ;
.done_success:
    or      [NAME(g_fFoundAdapter)], byte 1
    jmp     .done_close

    ;
    ; Close the OEMHLP$ driver.
    ;
.done_err_close:
    or      ax, 80h
.done_close:
    ; APIRET  APIENTRY DosClose(HFILE hf);
    push    ax                                      ; Save result
    mov     cx, [hfOpen]
    push    cx
    call far DOS16CLOSE
    or      ax, ax
    jnz     .bitch                                  ; This can't happen (I hope).
    pop     ax
    or      ax, ax
    jnz     .bitch

    ;
    ; Return to vgdrvOS2EP_Init.
    ;
.done:
    mov     esp, ebp
    pop     ebp
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret


    ;
    ; Puts the reason for failure in message buffer.
    ; The caller will flush this.
    ;
.bitch:
    push    es

    mov     ax, ds
    mov     es, ax
    mov     ax, di                                  ; save the reason.
    mov     di, NAME(g_szInitText)
    add     di, [NAME(g_cchInitText)]

    mov     si, g_achLoadFailureMsg1
    mov     cx, g_cchLoadFailureMsg1
    rep movsb

    stosb

    mov     si, g_achLoadFailureMsg2
    mov     cx, g_cchLoadFailureMsg2
    rep movsb

    mov     [di], byte 0
    sub     di, NAME(g_szInitText)
    mov     [NAME(g_cchInitText)], di

    pop     es
    jmp     .done


    ;
    ; Nested function which reads a PCI config register.
    ; (This operates on the vgdrvFindAdapter stack frame.)
    ;
    ; Input:
    ;   al - register to read
    ;   ah - register size.
    ;
    ; Output:
    ;   eax - the value.
    ;
    ; Uses:
    ;   dx
    ;
.NestedReadReg:
    ; Fill in the request packet.
    mov     [abParm + 0], byte PCI_READ_CONFIG      ; 0 - db - SubFunction Number
    mov     dl, [NAME(g_bPciBusNo)]
    mov     [abParm + 1], dl                        ; 1 - db - Bus Number
    mov     dl, [NAME(g_bPciDevFunNo)]
    mov     [abParm + 2], dl                        ; 2 - db - DevFunc Number
    mov     [abParm + 3], al                        ; 3 - db - Configuration Register
    mov     [abParm + 4], ah                        ; 4 - db - (Register) Size

    ; Pad the data packet.
    mov     [abData + 0], dword 0
    mov     [abData + 4], dword 0

    CallIOCtl 5, 5

    mov     eax, [abData + 1]                       ; 1 - dd - Data

    ret

ENDPROC vgdrvFindAdapter



;;
; This must be present
segment DATA32
g_pfnDos16Write:
    dd  DOS16WRITE  ; flat

