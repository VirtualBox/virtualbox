; $Id: bs3-bootsector.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Generic bootsector for BS3.
;
; This sets up stack at %fff0 and loads the next sectors from the floppy at
; %10000 (1000:0000 in real mode), then starts executing at cs:ip=1000:0000.
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
;



;*********************************************************************************************************************************
;*      Header Files                                                                                                             *
;*********************************************************************************************************************************
%include "bs3kit.mac"
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


;*********************************************************************************************************************************
;*      Defined Constants And Macros                                                                                             *
;*********************************************************************************************************************************
;; Enables faster loading.
%define BS3KIT_BOOTSECTOR_FASTER_LOAD
;; Enables load progress dots.
%define BS3KIT_BOOTSECTOR_LOAD_DOTS
;; Enables support for fake 63.5 MB floppies with 255 sectors, 2 heads and 255 tracks.
;; @note This is typically enabled by the makefile.
;%define BS3KIT_BOOTSECTOR_SUPPORT_63_5MB_FLOPPIES
;; Enables code to avoid trying to read across 64kB DMA boundaries.
%define BS3KIT_BOOTSECTOR_DODGE_BOUNDARIES

;; Halts on failure location. For debugging.
;%define HLT_ON_FAILURE 1

;; Enables saving of initial register state.
;; Dropping this is useful for making more room for debugging.
%define BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE


%ifdef __YASM__
[map all]
%endif

;
; Start with a jump just to follow the convention.
; Also declare all segments/sections to establish them and their order.
;
        ORG 07c00h

BITS 16
CPU 8086
start:
        jmp short bs3InitCode
        db 0ah                          ; Should be nop, but this looks better.
g_OemId:                                ; 003h
        db 'BS3Kit', 0ah, 0ah

;
; DOS 4.0 Extended Bios Parameter Block:
;
g_cBytesPerSector:                      ; 00bh
        dw 512
g_cSectorsPerCluster:                   ; 00dh
        db 1
g_cReservedSectors:                     ; 00eh
        dw 1
g_cFATs:                                ; 010h
        db 0
g_cRootDirEntries:                      ; 011h
        dw 0
g_cTotalSectors:                        ; 013h - We (ab)use this for the number of checksumed sectors.
        dw 0
g_bMediaDescriptor:                     ; 015h
        db 0
g_cSectorsPerFAT:                       ; 016h
        dw 0
g_cPhysSectorsPerTrack:                 ; 018h
        dw 18
g_cHeads:                               ; 01ah
        dw 2
g_cHiddentSectors:                      ; 01ch
        dd 1
g_cLargeTotalSectors:                   ; 020h - We (ab)use this to indicate the number of sectors to load.
        dd 0
g_bBootDrv:                             ; 024h
        db 80h
g_bFlagsEtc:                            ; 025h
        db 0
g_bExtendedSignature:                   ; 026h
        db 0x29
g_dwSerialNumber:                       ; 027h - We (ab)use this for the base image checksum.
        dd 0x0a458634
g_abLabel:                              ; 02bh
        db 'VirtualBox', 0ah
g_abFSType:                             ; 036h
        db 'RawCode', 0ah
g_BpbEnd:                               ; 03ch


;
; Where the real init code starts.
;
bs3InitCode:
        cli

%if 0 ; This does not work, we clear it wholesale below
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        ; save the registers.
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGCTX.rax], ax
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGCTX.ds], ds
%endif
%endif

        ; set up the DS segment register so we can skip the CS prefix when saving more prefixes..
        mov     ax, 0                   ; no xor ax,ax as that messes with the eflags
        mov     ds, ax

%if 0 ; This does not work, we clear it wholesale below
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        mov     [BS3_ADDR_REG_SAVE + BS3REGCTX.rdi], di
        mov     di, BS3_ADDR_REG_SAVE
        mov     [di + BS3REGCTX.rsp], sp
        mov     [di + BS3REGCTX.ss], ss
        mov     [di + BS3REGCTX.rcx], cx
        mov     [di + BS3REGCTX.es], es
        mov     [di + BS3REGCTX.rbp], bp
%endif
%endif

        ; set up the stack.
        mov     ss, ax
        mov     sp, BS3_ADDR_STACK

        ; Load es and setup bp frame.
        mov     es, ax
        mov     bp, sp
%if 0
        mov     [bp], ax                ; clear the first 8 bytes (terminates the ebp chain)
        mov     [bp + 02h], ax
        mov     [bp + 04h], ax
        mov     [bp + 06h], ax
%else
        mov     di, sp                  ; Combine clearing the rbp chain and register save area.
%endif

        ; Save flags now that we know that there's a valid stack.
        pushf

        ;
        ; Clear the register area.
        ;
%if 0
        mov     di, BS3_ADDR_REG_SAVE
        mov     cx, BS3REGCTX_size/2
%else
        mov     cx, (BS3_ADDR_LOAD - BS3_ADDR_STACK) / 2
%endif
        cld
        rep stosw

        ;
        ; Do basic CPU detection.
        ;

        ; 0. Load the register save area address into DI to avoid absolute addressing
        ;    when saving additional state.  To avoid disp16, offset the address.
        mov     di, BS3_ADDR_REG_SAVE + 0x70

        ; 1. bit 15-bit was fixed to 1 in pre-286 CPUs, and fixed to 0 in 286+.
%if 0 ; 3 vs 2 bytes
        mov     ax, [bp - 2]
%else
        pop     ax
        push    ax
%endif
        test    ah, 080h                ; always set on pre 286, clear on 286 and later
        jnz     .pre_80286

        ; 2. On a 286 you cannot popf IOPL and NT from real mode.
.detect_286_or_386plus:
CPU 286
        mov     ah, (X86_EFL_IOPL | X86_EFL_NT) >> 8 ; OR'ing would be safer, but costs a byte and nothing should be set anyway.
        push    ax
        popf
        pushf
        cmp     ah, [bp - 3]
        pop     ax
        je      .is_386plus
.is_80286:
CPU 286
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        smsw    [di + BS3REGCTX.cr0 - 0x70]
%endif
.pre_80286:
CPU 8086
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        mov     [di - 0x70 + BS3REGCTX.rflags], ax
        mov     [di - 0x70 + BS3REGCTX.rbx], bx
        mov     [di - 0x70 + BS3REGCTX.rdx], dx
        mov     [di - 0x70 + BS3REGCTX.rsi], si
%endif
        jmp     .do_load

        ; Save 386 registers. We can now skip the CS prefix as DS is flat.
CPU 386
.is_386plus:
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        shr     eax, 16
        mov     [di - 0x70 + BS3REGCTX.rax+2], ax
        mov     eax, esp
        shr     eax, 16
        mov     [di - 0x70 + BS3REGCTX.rsp+2], ax
        mov     eax, ebp
        shr     eax, 16
        mov     [di - 0x70 + BS3REGCTX.rbp+2], ax
        mov     eax, edi
        shr     eax, 16
        mov     [di - 0x70 + BS3REGCTX.rdi+2], ax
        shr     ecx, 16
        mov     [di - 0x70 + BS3REGCTX.rcx+2], cx
        mov     [di - 0x70 + BS3REGCTX.fs], fs
        mov     [di - 0x70 + BS3REGCTX.gs], gs
        mov     [di - 0x70 + BS3REGCTX.rbx], ebx
        mov     [di - 0x70 + BS3REGCTX.rdx], edx
        mov     [di - 0x70 + BS3REGCTX.rsi], esi
        mov     eax, cr2
        mov     [di - 0x70 + BS3REGCTX.cr2], eax
        mov     eax, cr3
        mov     [di - 0x70 + BS3REGCTX.cr3], eax
        mov     byte [di - 0x70 + BS3REGCTX.bMode], BS3_MODE_RM
        mov     [di - 0x70 + BS3REGCTX.cs], cs
        xor     eax, eax
        mov     ax, start
        mov     [di - 0x70 + BS3REGCTX.rip], eax

        ; Pentium/486+: CR4 requires VME/CPUID, so we need to detect that before accessing it.
        mov     [di - 0x70 + BS3REGCTX.cr4], eax
        popf                            ; (restores IOPL+NT)
        pushfd
        pop     eax
        mov     [di - 0x70 + BS3REGCTX.rflags], eax
        xor     eax, X86_EFL_ID
        push    eax
        popfd
        pushfd
        pop     ebx
        cmp     ebx, eax
        jne     .no_cr4
        mov     eax, cr4
        mov     [di - 0x70 + BS3REGCTX.cr4], eax
.no_cr4:
%else
        popf                            ; (restores IOPL+NT)
%endif
        ; Make sure caching is enabled and alignment is off.
        mov     eax, cr0
%ifdef BS3KIT_BOOTSECTOR_SAVE_INITIAL_STATE
        mov     [di - 0x70 + BS3REGCTX.cr0], eax
%endif
        and     eax, ~(X86_CR0_NW | X86_CR0_CD | X86_CR0_AM)
        mov     cr0, eax

        ; Load all the code.
.do_load
        mov     [g_bBootDrv], dl
        call    NAME(bs3InitLoadImage)
%if 0
        mov     al, '='
        call    bs3PrintChrInAl
%endif

        ;
        ; Call the user 'main' procedure (shouldn't return).
        ;
        cld
        call    BS3_SEL_TEXT16:0000h

        ; Panic/hang.
Bs3Panic:
        cli
        jmp     Bs3Panic


;; For debug and error handling.
; @uses ax
bs3PrintHexInAl:
CPU 286
        push    ax
        shr     al, 4
        call    bs3PrintHexDigitInAl
        pop     ax
bs3PrintHexDigitInAl:
        and     al, 0fh
        cmp     al, 10
        jb      .decimal
        add     al, 'a' - '0' - 10
.decimal:
        add     al, '0'
bs3PrintChrInAl:
        push    bx
        mov     ah, 0eh
        mov     bx, 0ff00h
        int     10h
        pop     bx
        ret


;;
; Loads the image off the floppy.
;
; This uses g_cLargeTotalSectors to figure out how much to load.
;
; Clobbers everything except ebp and esp.  Panics on failure.
;
; @param    dl          The boot drive number (from BIOS).
; @uses     ax, cx, bx, esi, di
;
BEGINPROC bs3InitLoadImage
        push    bp
        mov     bp, sp
        push    es
%define bSavedDiskNo    byte [bp - 04h]
        push    dx
%define bMaxSector      byte [bp - 06h]
%define wMaxSector      word [bp - 06h]
        xor     ax, ax
        push    ax
%define bMaxHead        byte [bp - 08h]
        push    ax

        ;
        ; Try figure the geometry.
        ;
        mov     ah, 08h
        int     13h
%ifndef HLT_ON_FAILURE
        jc      .failure
%else
        jnc     .ok_geometry_call
        cli
        hlt
.ok_geometry_call:
%endif
%ifndef BS3KIT_BOOTSECTOR_SUPPORT_63_5MB_FLOPPIES
        and     cl, 63                  ; only the sector count. (63.5MB has 255 sectors, so don't do this!)
%endif
        mov     bMaxSector, cl
        mov     bMaxHead, dh
        mov     dl, bSavedDiskNo
                                  ;63*2*0xff*512 = 0xFB0400 (16 450 560)
%if 0 ; DEBUG ; bMaxSector=0x12 (18); bMaxHead=0x01; bMaxCylinder=0x4f (79)
        mov al, 'S'
        call bs3PrintChrInAl
        mov al, bMaxSector
        call bs3PrintHexInAl
        mov al, 'H'
        call bs3PrintChrInAl
        mov al, bMaxHead
        call bs3PrintHexInAl
        mov al, 'C'
        call bs3PrintChrInAl
        mov al, ch                      ; first 8-bit of cylinder count.
        call bs3PrintHexInAl
        mov al, ';'
        call bs3PrintChrInAl
%endif

%ifndef BS3KIT_BOOTSECTOR_FASTER_LOAD
        ;
        ; Load the sectors following the boot sector one at a time (avoids problems).
        ;
        mov     si, [g_cLargeTotalSectors] ; 16-bit sector count ==> max 512 * 65 535 = 33 553 920 bytes.
        dec     si                       ; Practically max: ca 575 KB, or 1150 sectors.  Linker set BS3_MAX_SIZE to 480KB.

        mov     di, BS3_ADDR_LOAD / 16  ; The current load segment.
        mov     cx, 0002h               ; ch/cylinder=0 (0-based); cl/sector=2 (1-based)
        xor     dh, dh                  ; dh/head=0
.the_load_loop:
 %if 0 ; DEBUG
        mov al, 'c'
        call bs3PrintChrInAl
        mov al, ch
        call bs3PrintHexInAl
        mov al, 's'
        call bs3PrintChrInAl
        mov al, cl
        call bs3PrintHexInAl
        mov al, 'h'
        call bs3PrintChrInAl
        mov al, dh
        call bs3PrintHexInAl
        mov al, ';'
        call bs3PrintChrInAl
 %elifdef BS3KIT_BOOTSECTOR_LOAD_DOTS
        mov     al, '.'
        call bs3PrintChrInAl
 %endif
        xor     bx, bx
        mov     es, di                  ; es:bx -> buffer
        mov     ax, 0201h               ; al=1 sector; ah=read function
        int     13h
 %ifndef HLT_ON_FAILURE
        jc      .failure
 %else
        jnc     .read_ok
        cli
        hlt
.read_ok:
 %endif

        ; advance to the next sector/head/cylinder.
        inc     cl
        cmp     cl, bMaxSector
        jbe     .adv_addr

        mov     cl, 1
        inc     dh
        cmp     dh, bMaxHead
        jbe     .adv_addr

        mov     dh, 0
        inc     ch

.adv_addr:
        add     di, 512 / 16
        dec     si
        jnz     .the_load_loop

%else ; BS3KIT_BOOTSECTOR_FASTER_LOAD
        ;
        ; Load the sectors following the boot sector, trying to load a whole
        ; side in each bios call, falling back on single sector reads if we
        ; run into DMA 64KB boundrary issues (BIOS must tell us).
        ;
        mov     si, [g_cLargeTotalSectors] ; 16-bit sector count ==> max 512 * 65 535 = 33 553 920 bytes.
        dec     si                      ; Skip the boot sector, it's not part of the test image we execute.
        mov     di, BS3_ADDR_LOAD / 16  ; The current load segment.
        mov     cx, 0002h               ; ch/cylinder=0 (0-based); cl/sector=0 (1-based)
        xor     dh, dh                  ; dh/head=0
.the_load_loop:
 %if 0 ; DEBUG
        mov al, 'c'
        call bs3PrintChrInAl
        mov al, ch
        call bs3PrintHexInAl
        mov al, 's'
        call bs3PrintChrInAl
        mov al, cl
        call bs3PrintHexInAl
        mov al, 'h'
        call bs3PrintChrInAl
        mov al, dh
        call bs3PrintHexInAl
        mov al, '#'
        call bs3PrintChrInAl
 %elifdef BS3KIT_BOOTSECTOR_LOAD_DOTS
        mov     al, '.'
        call bs3PrintChrInAl
 %endif
        mov     ax, wMaxSector          ; read to the end of the side by default.
        sub     al, cl
        inc     al
 %ifdef BS3KIT_BOOTSECTOR_SUPPORT_63_5MB_FLOPPIES
        cmp     al, 72                  ; The BIOS dislikes reading too many sectors at a time.
        jbe     .read_again             ; Look for 'num_sectors > 72' in PC/BIOS/floppy.c.
        mov     al, 72
 %endif
.read_again:
        cmp     si, ax
        jae     .do_read
        mov     ax, si
.do_read:

 %ifdef BS3KIT_BOOTSECTOR_DODGE_BOUNDARIES
                                        ; Dodge around 64kB DMA boundaries
        push    di                      ; di is 'paragraph' address of buffer
        push    ax                      ; Example: di = 1f60h, al = 12h = 18-sector request;
        shr     di, 5                   ; 0fbh   addr = 1f600h (can fit 5x 200h below 128kB)
        and     di, 7fh                 ;  7bh
        add     di, ax                  ;  8dh
        sub     di, 80h                 ;  0dh
        pop     ax                      ;  12h
        js      .non_boundary
        sub     ax, di                  ;  05h
.non_boundary:
        pop     di
 %endif

 %if 0 ; DEBUG
        push    ax
        call bs3PrintHexInAl
        mov al, ';'
        call bs3PrintChrInAl
        pop     ax
 %endif

        mov     ah, 02h                 ; ah=read function
        xor     bx, bx
        mov     es, di                  ; es:bx -> buffer
        int     13h
 %ifdef BS3KIT_BOOTSECTOR_DODGE_BOUNDARIES
        jc      .failure
 %else
        jnc     .advance_sector

        cmp     ah, 9                   ; DMA 64KB crossing error
        jne     .failure
        mov     ax, 1                   ; Retry reading a single sector.
        jmp     .read_again
 %endif ; !BS3KIT_BOOTSECTOR_DODGE_BOUNDARIES

        ; advance to the next sector/head/cylinder and address.
.advance_sector:
        inc     cl
 %ifdef BS3KIT_BOOTSECTOR_SUPPORT_63_5MB_FLOPPIES
        jz      .adv_sector             ; Wraparound is a 63.5 MB problem.
 %endif
        cmp     cl, bMaxSector
        jbe     .adv_addr

.adv_sector:
        mov     cl, 1
        inc     dh
        cmp     dh, bMaxHead
        jbe     .adv_addr

        mov     dh, 0
        inc     ch

.adv_addr:
        dec     si
        jz      .done_reading
        add     di, 512 / 16
        dec     al
        jnz     .advance_sector
        jmp     .the_load_loop

.done_reading:
%endif ; BS3KIT_BOOTSECTOR_FASTER_LOAD
%if 0 ; DEBUG
        mov     al, 'D'
        call bs3PrintChrInAl
%elifdef BS3KIT_BOOTSECTOR_LOAD_DOTS
        mov     al, 13
        call bs3PrintChrInAl
        mov     al, 10
        call bs3PrintChrInAl
%endif

%if 0 ; 3 vs 2 bytes
        add     sp, 2*2
%else
        pop     dx
        pop     dx
%endif
        pop     dx
        pop     es
        pop     bp
        ret

%ifndef HLT_ON_FAILURE
        ;
        ; Something went wrong, display a message.
        ;
.str_sErrMsgBang:
        db ' rd err!'

.failure:
 %if 1 ; Disable to save space for debugging.
        mov     al, ah
        call    bs3PrintHexInAl
        ; print message
        mov     si, .str_sErrMsgBang
.failure_next_char:
        lodsb
        call    bs3PrintChrInAl
        cmp     byte [si], '!'          ; Don't trust that int 10h/0eh preserves al
        jne     .failure_next_char
 %endif
%else
.failure:
%endif
        jmp     Bs3Panic
;ENDPROC bs3InitLoadImage - don't want the padding.


;
; Pad the remainder of the sector with int3's and end it with the DOS signature.
;
bs3Padding:
        times ( 510 - ( (bs3Padding - start) % 512 ) ) db 0cch
        db      055h, 0aah

