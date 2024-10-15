; $Id: SUPDrvA-os2.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxDrv - OS/2 assembly file, the first file in the link.
;

;
; Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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



;;
; The two device headers.
segment DATA16

; Some devhdr.inc stuff.
%define DEVLEV_3                0180h
%define DEV_30                  0800h
%define DEV_CHAR_DEV            8000h
%define DEV_16MB                0002h
%define DEV_IOCTL2              0001h

; Some dhcalls.h stuff.
%define DevHlp_VirtToLin        05bh
%define DevHlp_SAVE_MESSAGE     03dh
%define DevHlp_PhysToVirt       015h

; Fast IOCtl category, also defined in SUPDrvIOC.h
%define SUP_CTL_CATEGORY_FAST   0c1h


;*******************************************************************************
;* External Symbols                                                            *
;*******************************************************************************
extern KernThunkStackTo32
extern KernThunkStackTo16
extern DOS16OPEN
extern DOS16CLOSE
extern DOS16WRITE
extern NAME(VBoxDrvInit)
extern NAME(VBoxDrvOpen)
extern NAME(VBoxDrvClose)
extern NAME(VBoxDrvIOCtl)
extern NAME(VBoxDrvIOCtlFast)


;;
; Device headers. The first one is the one we'll be opening and the
; latter is only used for 32-bit initialization.
GLOBALNAME g_VBoxDrvHdr1
    dw  NAME(g_VBoxDrvHdr2) wrt DATA16  ; NextHeader.off
    dw  DATA16                          ; NextHeader.sel
    dw  DEVLEV_3 | DEV_30 | DEV_CHAR_DEV; SDevAtt
    dw  NAME(VBoxDrvEP) wrt CODE16      ; StrategyEP
    dw  0                               ; InterruptEP
    db  'vboxdrv$'                      ; DevName
    dw  0                               ; SDevProtCS
    dw  0                               ; SDevProtDS
    dw  0                               ; SDevRealCS
    dw  0                               ; SDevRealDS
    dd  DEV_16MB | DEV_IOCTL2           ; SDevCaps

align 4
GLOBALNAME g_VBoxDrvHdr2
    dd  0ffffffffh                      ; NextHeader (NIL)
    dw  DEVLEV_3 | DEV_30 | DEV_CHAR_DEV; SDevAtt
    dw  NAME(VBoxDrvInitEP) wrt CODE16  ; StrategyEP
    dw  0                               ; InterruptEP
    db  'vboxdr1$'                      ; DevName
    dw  0                               ; SDevProtCS
    dw  0                               ; SDevProtDS
    dw  0                               ; SDevRealCS
    dw  0                               ; SDevRealDS
    dd  DEV_16MB | DEV_IOCTL2           ; SDevCaps


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
BEGINPROC VBoxDrvEP
    push    ebp
    mov     ebp, esp
    push    es                                      ; bp - 2
    push    bx                                      ; bp - 4
    and     sp, 0fffch

    ;
    ; Check for the most frequent first.
    ;
    cmp     byte [es:bx + PKTHDR.cmd], 10h          ; Generic IOCtl
    jne near VBoxDrvEP_NotGenIOCtl


    ;
    ; Generic I/O Control Request.
    ;
VBoxDrvEP_GenIOCtl:

    ; Fast IOCtl?
    cmp     byte [es:bx + PKTIOCTL.cat], SUP_CTL_CATEGORY_FAST
    jne     VBoxDrvEP_GenIOCtl_Other

    ;
    ; Fast IOCtl.
    ;   DECLASM(int) VBoxDrvIOCtlFast(uint16_t sfn, uint8_t iFunction)
    ;
VBoxDrvEP_GenIOCtl_Fast:
    ; function.
    movzx   edx, byte [es:bx + PKTIOCTL.fun]
    push    edx                                     ; 04h

    ; system file number.
    movzx   eax, word [es:bx + PKTIOCTL.sfn]
    push    eax                                     ; 00h

    ; go to the 32-bit code
    ;jmp far dword NAME(VBoxDrvEP_GenIOCtl_Fast_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxDrvEP_GenIOCtl_Fast_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxDrvEP_GenIOCtl_Fast_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code (don't cleanup the stack).
    call    NAME(VBoxDrvIOCtlFast)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    ; jump back to the 16-bit code.
    ;jmp far dword NAME(VBoxDrvEP_GenIOCtl_Fast_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxDrvEP_GenIOCtl_Fast_16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME VBoxDrvEP_GenIOCtl_Fast_16
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near VBoxDrvEP_GeneralFailure

    ; setup output stuff.
    xor     eax, eax
    mov     [es:bx + PKTIOCTL.cbParm], eax          ; update cbParm and cbData.
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.

    mov     sp, bp
    pop     ebp
    retf

    ;
    ; Other IOCtl (slow)
    ;
VBoxDrvEP_GenIOCtl_Other:
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
    jc near VBoxDrvEP_GeneralFailure
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
    jc near VBoxDrvEP_GeneralFailure
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

    ; go to the 32-bit code
    ;jmp far dword NAME(VBoxDrvEP_GenIOCtl_Other_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxDrvEP_GenIOCtl_Other_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxDrvEP_GenIOCtl_Other_32

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
    call    NAME(VBoxDrvIOCtl)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    ; jump back to the 16-bit code.
    ;jmp far dword NAME(VBoxDrvEP_GenIOCtl_Other_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxDrvEP_GenIOCtl_Other_16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME VBoxDrvEP_GenIOCtl_Other_16
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near VBoxDrvEP_GeneralFailure

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
VBoxDrvEP_NotGenIOCtl:
    cmp     byte [es:bx + PKTHDR.cmd], 0dh          ; Open
    je      VBoxDrvEP_Open
    cmp     byte [es:bx + PKTHDR.cmd], 0eh          ; Close
    je      VBoxDrvEP_Close
    cmp     byte [es:bx + PKTHDR.cmd], 00h          ; Init
    je      VBoxDrvEP_Init
    cmp     byte [es:bx + PKTHDR.cmd], 04h          ; Read
    je near VBoxDrvEP_Read
    jmp near VBoxDrvEP_NotSupported


    ;
    ; Open Request. w/ ring-0 init.
    ;
VBoxDrvEP_Open:
    cmp     byte [NAME(g_fInitialized)], 1
    jne     VBoxDrvEP_OpenOther

    ; First argument, the system file number.
    movzx   eax, word [es:bx + PKTOPEN.sfn]
    push    eax

    ; go to the 32-bit code
    ;jmp far dword NAME(VBoxDrvEP_Open_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxDrvEP_Open_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxDrvEP_Open_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(VBoxDrvOpen)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    ; jump back to the 16-bit code.
    ;jmp far dword NAME(VBoxDrvEP_Open_32) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxDrvEP_Open_16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME VBoxDrvEP_Open_16
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near VBoxDrvEP_GeneralFailure
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    jmp near VBoxDrvEP_Done

    ; Initializing or failed init?
VBoxDrvEP_OpenOther:
    cmp     byte [NAME(g_fInitialized)], 0
    jne     VBoxDrvEP_OpenFailed

    mov     byte [NAME(g_fInitialized)], -1
    call    NAME(VBoxDrvRing0Init)
    cmp     byte [NAME(g_fInitialized)], 1
    je      VBoxDrvEP_Open

VBoxDrvEP_OpenFailed:
    mov     word [es:bx + PKTHDR.status], 0810fh    ; error, done, init failed.
    jmp near VBoxDrvEP_Done


    ;
    ; Close Request.
    ;
VBoxDrvEP_Close:
    ; First argument, the system file number.
    movzx   eax, word [es:bx + PKTOPEN.sfn]
    push    eax

    ; go to the 32-bit code
    ;jmp far dword NAME(VBoxDrvEP_Close_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxDrvEP_Close_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxDrvEP_Close_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(VBoxDrvClose)

    ; switch back the stack.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    ; jump back to the 16-bit code.
    ;jmp far dword NAME(VBoxDrvEP_Close_32) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxDrvEP_Close_16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME VBoxDrvEP_Close_16
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    or      eax, eax
    jnz near VBoxDrvEP_GeneralFailure
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    jmp near VBoxDrvEP_Done


    ;
    ; Init Request.
    ; The other driver header will do this.
    ;
VBoxDrvEP_Init:
    mov     word [es:bx + PKTHDR.status], 00100h    ; done, ok.
    mov     byte [es:bx + PKTINITOUT.cUnits], 0
    mov     word [es:bx + PKTINITOUT.cbCode16], NAME(g_InitCodeStart) wrt CODE16
    mov     word [es:bx + PKTINITOUT.cbData16], NAME(g_InitDataStart) wrt DATA16
    mov     dword [es:bx + PKTINITOUT.fpaBPBs], 0
    jmp near VBoxDrvEP_Done


    ;
    ; Read Request.
    ; Return log data.
    ;
VBoxDrvEP_Read:
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
    jmp near VBoxDrvEP_Done

.log_phystovirt_failed:
    les     bx, [bp - 4]                            ; Reload the packet pointer.
    jmp     VBoxDrvEP_GeneralFailure


    ;
    ; Return 'unknown command' error.
    ;
VBoxDrvEP_NotSupported:
    mov     word [es:bx + PKTHDR.status], 08103h    ; error, done, unknown command.
    jmp     VBoxDrvEP_Done

    ;
    ; Return 'general failure' error.
    ;
VBoxDrvEP_GeneralFailure:
    mov     word [es:bx + PKTHDR.status], 0810ch    ; error, done, general failure.
    jmp     VBoxDrvEP_Done

    ;
    ; Non-optimized return path.
    ;
VBoxDrvEP_Done:
    mov     sp, bp
    pop     ebp
    retf
ENDPROC VBoxDrvEP


;;
; The helper device entry point.
;
; This is only used to do the DosOpen on the main driver so we can
; do ring-3 init and report failures.
;
GLOBALNAME VBoxDrvInitEP
    ; The only request we're servicing is the 'init' one.
    cmp     word [es:bx + PKTHDR.cmd], 0
    je near NAME(VBoxDrvInitEPServiceInitReq)

    ; Ok, it's not the init request, just fail it.
    mov     word [es:bx + PKTHDR.status], 08103h    ; error, done, unknown command.
    retf


;
; The 16-bit init code.
;
segment CODE16_INIT
GLOBALNAME g_InitCodeStart

;; The device name for DosOpen.
g_szDeviceName:
    db  '\DEV\vboxdrv$', 0

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
BEGINPROC VBoxDrvInitEPServiceInitReq
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
    call    NAME(VBoxDrvInitFlushText)
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
ENDPROC VBoxDrvInitEPServiceInitReq


;;
; The Ring-0 init code.
;
BEGINPROC VBoxDrvRing0Init
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
    jc near VBoxDrvRing0Init_done                   ; eax is non-zero on failure (can't happen)
    push    eax                                     ; 00h - pszArgs (for VBoxDrvInit).

    ;
    ; Do 16-bit init?
    ;


    ;
    ; Do 32-bit init
    ;
    ;jmp far dword NAME(VBoxDrvRing0Init_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxDrvRing0Init_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxDrvRing0Init_32

    ; switch stack to 32-bit.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax
    call    KernThunkStackTo32

    ; call the C code.
    call    NAME(VBoxDrvInit)

    ; switch back the stack and reload ds.
    push    eax
    call    KernThunkStackTo16
    pop     eax

    mov     dx, seg NAME(g_fInitialized)
    mov     ds, dx

    ; jump back to the 16-bit code.
    ;jmp far dword NAME(VBoxDrvRing0Init_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxDrvRing0Init_16) wrt CODE16
    dw      CODE16_INIT
segment CODE16_INIT
GLOBALNAME VBoxDrvRing0Init_16

    ; check the result and set g_fInitialized on success.
    or      eax, eax
    jnz     VBoxDrvRing0Init_done
    mov     byte [NAME(g_fInitialized)], 1

VBoxDrvRing0Init_done:
    mov     sp, bp
    pop     ebp
    pop     esi
    pop     es
    ret
ENDPROC VBoxDrvRing0Init


;;
; Flush any text in the text buffer.
;
BEGINPROC VBoxDrvInitFlushText
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
ENDPROC VBoxDrvInitFlushText



;;
; This must be present
segment DATA32
g_pfnDos16Write:
    dd  DOS16WRITE  ; flat

