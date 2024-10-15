; $Id: bootsector2-vbinstst-kernel.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; bootsector #2 kernel for big instruction testcases.
;   VBoxManage setextradata bs-vbinstst-64-1 VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled  1
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


;
; This is always the first include file.
;
%include "bootsector2-first.mac"

;
; Include and execute the init code.
;
        %define BS2_INIT_RM
        %define BS2_WITH_TRAPS
        %define BS2_WITHOUT_RAW_MODE ; causes troubles with PIC/floppy.

        %define BS2_INC_RM
        %define BS2_INC_PE16
        %define BS2_INC_PE32
        %define BS2_INC_PP16
        %define BS2_INC_PP32
        %define BS2_INC_PAE16
        %define BS2_INC_PAE32
        %define BS2_INC_LM16
        %define BS2_INC_LM32
        %define BS2_INC_LM64
        %include "bootsector2-common-init-code.mac"
        %include "bootsector2-api.mac"
        %include "iprt/formats/pe.mac"
        %include "iprt/formats/mz.mac"


BEGINCODE
BEGINPROC main
;
; Set up the runtime environment.
;
        call    Bs2EnableA20_r86

        ; 16-bit real mode.
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov dword [NAME(g_pfn %+ a_Name %+ _r86)], dword NAME(a_Name %+ _r86)
        BS2_API_TEMPLATE

        ; 16-bit protected mode.
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov word [NAME(g_pfn %+ a_Name %+ _p16)], word NAME(a_Name %+ _p16)
        BS2_API_TEMPLATE
        mov     eax, BS2_SEL_CS16
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov [NAME(g_pfn %+ a_Name %+ _p16) + 2], ax
        BS2_API_TEMPLATE

        ; 32-bit
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov dword [NAME(g_pfn %+ a_Name %+ _p32)], dword NAME(a_Name %+ _p32)
        BS2_API_TEMPLATE

        ; 64-bit
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov dword [NAME(g_pfn %+ a_Name %+ _p64)], dword NAME(a_Name %+ _p64)
        BS2_API_TEMPLATE
        xor eax, eax
%undef BS2_API_TEMPLATE_ACTION
%define BS2_API_TEMPLATE_ACTION(a_Name) mov dword [NAME(g_pfn %+ a_Name %+ _p64) + 4], eax
        BS2_API_TEMPLATE

        ; The magic markers and version number.
        mov     dword [g_u32Bs2ApiMagic],    BS2_API_MAGIC
        mov     dword [g_u32Bs2ApiEndMagic], BS2_API_MAGIC
        mov     dword [g_u32Bs2ApiVersion],  BS2_API_VERSION

;
; Load the extended image into high memory.
;
        mov     dl, [g_bBootDrv]
        call    NAME(bs2LoadBigImage)

;
; Hand control over to the extended image.
;
%ifdef BS2_BIG_IMAGE_LM64
        call    Bs2EnterMode_rm_lm64
BITS 64
        mov     eax, BS2_BIG_LOAD_ADDR
        call    rax
        call    Bs2ExitMode_lm64
BITS 16

%elifdef BS2_BIG_IMAGE_PP32
        call    Bs2EnterMode_rm_pp32
BITS 32
        mov     eax, BS2_BIG_LOAD_ADDR
        call    eax
        call    Bs2ExitMode_pp32
BITS 16

%elifdef BS2_BIG_IMAGE_PAE32
        call    Bs2EnterMode_rm_pae32
BITS 32
        mov     eax, BS2_BIG_LOAD_ADDR
        call    eax
        call    Bs2ExitMode_pae32
BITS 16

%else
        ;
        ; Probe the image, looking for an executable format we can deal with.
        ; Not doing a lot of checking here, but who cares right now...
        ;
        call    Bs2EnterMode_rm_pp32
BITS 32
        mov     eax, BS2_BIG_LOAD_ADDR
        cmp     word [eax], IMAGE_DOS_SIGNATURE
        jne     .not_dos
        add     eax, [eax + IMAGE_DOS_HEADER.e_lfanew]
.not_dos:
        cmp     dword [eax], IMAGE_NT_SIGNATURE
        je      .is_pe
        mov     eax, BS2_BIG_LOAD_ADDR
        jmp     .start_32

.is_pe:
        lea     edx, [eax + IMAGE_NT_HEADERS32.FileHeader]
        cmp     word [edx + IMAGE_FILE_HEADER.Machine], IMAGE_FILE_MACHINE_I386
        je      .is_pe32
        cmp     word [edx + IMAGE_FILE_HEADER.Machine], IMAGE_FILE_MACHINE_AMD64
        je      .is_pe64
        jmp     .panic_32

.is_pe32:
        add     edx, IMAGE_FILE_HEADER_size
        mov     eax, [edx + IMAGE_OPTIONAL_HEADER32.AddressOfEntryPoint]
        add     eax, BS2_BIG_LOAD_ADDR
        jmp     .start_32

.is_pe64:
        add     edx, IMAGE_FILE_HEADER_size
        mov     eax, [edx + IMAGE_OPTIONAL_HEADER64.AddressOfEntryPoint]
        add     eax, BS2_BIG_LOAD_ADDR
        jmp     .start_64

        ; Start executing at eax in 32-bit mode (current).
.start_32:
        call    eax
.panic_32:
        call    Bs2ExitMode_pp32
BITS 16
        jmp     .panic

        ; Start executing at eax in 64-bit mode.
BITS 32
.start_64:
        call    Bs2ExitMode_pp32
BITS 16
        call    Bs2EnterMode_rm_lm64
BITS 64
        mov     eax, eax
        call    rax
        call    Bs2ExitMode_lm64
BITS 16
        jmp     .panic

.panic:
%endif
        call    Bs2Panic
ENDPROC main




;;
; Loads the big image off the floppy.
;
; This uses the the_end label to figure out the starting offset.
; The length is assumed to be the whole floppy.
;
; Clobbers nothing, except for 68KB of memory beyond the_end.
;
; @param    dl          The boot drive number (from BIOS).
;
BITS 16
BEGINPROC bs2LoadBigImage
        push    ebp
        movzx   ebp, sp

%define bSavedDiskNo    byte [bp - 02h]
        push    dx
%define bMaxSector      byte [bp - 04h]
        push    0
%define bMaxHead        byte [bp - 06h]
        push    0
%define bMaxCylinder    byte [bp - 08h]
        push    0
%define pbHighDst       dword [bp - 0ch]
        push    dword BS2_BIG_LOAD_ADDR
%define SegTemp         word [bp - 0eh]
        push    0
%define fStatus         byte [bp - 10h]
        push    0

        push    es
        push    ds
        push    eax
        push    edx
        push    ecx
        push    ebx
        push    edi
        push    esi
        push    ebp

        ; Display message.
        push    cs
        push    .s_szLoadingBigImage
        call    PrintF_r86
        add     sp, 4


        ;
        ; Try figure the geometry.  This defines how much we'll read.
        ;
        mov     ah, 08h
        xor     di, di                  ; (es:di = 0000:0000 works around some buggy bioses, says wikipedia.)
        mov     es, di
        int     13h
        jc      .param_error
        mov     bMaxSector, cl          ; Do the cl[7:6]+ch stuff so we can address 255 sectors on the fake 63MB floppy.
        mov     bMaxHead, dh
        mov     bMaxCylinder, ch        ; See above.
        mov     dl, bSavedDiskNo
%if 0
        movzx   ax, bMaxCylinder
        push    ax
        movzx   cx, bMaxHead
        push    cx
        movzx   ax, bMaxSector
        push    ax
        push    ds
        push    .s_szDbgParam
        call    PrintF_r86
        jmp     .dprintf_param_done
.s_szDbgParam:
        db 13, 10, 'Floppy params max: sectors=%RX16 heads=%RX16 cylinders=%RX16', 13, 10, 0
.dprintf_param_done:
%endif

        ;
        ; Skip the kernel image (this could be done more efficiently, but this
        ; also does the trick).
        ;
        lea     eax, [dword the_end]
        sub     eax, start
        shr     eax, 9                  ; sectors to skip
        mov     cx, 0001h               ; sector (1-based), cylinder (0-based).
        xor     dh, dh                  ; head (0-based).
.skip_one_more:
        inc     cl
        cmp     cl, bMaxSector
        jbe     .decrement_sector_count

        mov     cl, 1
        inc     dh
        cmp     dh, bMaxHead            ; ASSUMES bMaxHead < 255.
        jbe     .decrement_sector_count

        mov     dh, 0
        inc     ch

.decrement_sector_count:
        dec     ax
        jnz     .skip_one_more



        ;
        ; Load loop. We load and copy 64 KB at the time into the high location.
        ; Fixed registers (above): dl=drive, cl[7:6]:ch=cylinder, dh=head, cl[5:0]=sector.
        ;
        lea     eax, [dword the_end + 0ffffh]
        and     eax, 0ffff0000h
        shr     eax, 4
        mov     SegTemp, ax             ; the 64KB segment we use for temporary storage.

.the_load_loop:
        mov     al, '.'
        call    PrintChr_r86

        ; Fill the segment with int3s (in case we don't read a full 64KB).
        mov     eax, 0cccccccch
        mov     di, SegTemp
        mov     es, di
        xor     edi, edi
        push    ecx
        cld
        mov     cx, 4000h
        rep stosd
        pop     ecx

        ;
        ; Load a bunch of sectors into the temp segment.
        ;
        xor     ebx, ebx
.the_sector_load_loop:
        ; Figure how many sectors we can read without switching track or side.
        movzx   ax, bMaxSector
        sub     al, cl
        inc     al                      ; al = sectors left to read in the current track on the current side.
        mov     di, bx
        shr     di, 9                   ; bx/512 = current sector offset.
        neg     di
        add     di, 10000h / 512        ; di = sectors left to read in the 64KB buffer.
        cmp     ax, di                  ; ax = min(ax, di)
        jbe     .use_ax_sector_count1
        mov     ax, di
.use_ax_sector_count1:
        cmp     ax, 64                  ; ax = min(ax,64) - Our BIOS limitation is 72, play safe.
        jbe     .use_ax_sector_count2
        mov     ax, 64
.use_ax_sector_count2:
        mov     di, ax                  ; save the number of sectors we read

        ; Do the reading.
%if 0
        push    bx
        push    ax
        push    dx
        push    cx
        push    cs
        push    .s_szDbgRead
        call    PrintF_r86
        jmp     .after_read_dprintf
.s_szDbgRead: db 'Reading CX=%RX16 DX=%RX16 AX=%RX16 BX=%RX16', 13, 10, 0
.after_read_dprintf:
%endif
        push    bx
        mov     ah, 02h                 ; ah=read function
        int     13h
        pop     bx
        jc      .read_error

        ; advance to the next sector/head/cylinder and address (lazy impl).
.advance_another_sector:
        cmp     cl, bMaxSector
        je      .next_head
        inc     cl
        jmp     .adv_addr

.next_head:
        mov     cl, 1
        cmp     dh, bMaxHead
        je      .next_cylinder
        inc     dh
        jmp     .adv_addr

.next_cylinder:
        mov     dh, 0
        cmp     ch, bMaxCylinder        ; No the cl[7:6]+ch stuff so we can address 255 sectors on the fake 63MB floppy.
        jb      .update_ch
        mov     fStatus, 1
        jmp     .move_block
.update_ch:
        inc     ch

.adv_addr:
        add     bx, 512
        dec     di
        jnz     .advance_another_sector

        test    bx, bx
        jnz     .the_sector_load_loop

.move_block:
        ;
        ; Copy the memory into high mem.
        ;
%if 0
        mov     edi, pbHighDst
        push    edi
        push    cs
        push    .s_szDbgMove
        call    PrintF_r86
        jmp     .after_move_dprintf
.s_szDbgMove: db 'Moving memory to EDI=%RX32', 13, 10, 0
.after_move_dprintf:
%endif

        push    ecx
        push    edx
        push    ds
        push    es
        call    Bs2EnterMode_rm_pp32
BITS 32
        ; Copy
        mov     edi, pbHighDst
        movzx   esi, SegTemp
        shl     esi, 4
        mov     ecx, 10000h / 4
        cld
        rep movsd

        ; Verify
        mov     edi, pbHighDst
        movzx   esi, SegTemp
        shl     esi, 4
        mov     ecx, 10000h / 4
        cld
        repe cmpsd
        je      .mem_verified_ok
        mov     fStatus, 2

.mem_verified_ok:
        mov     pbHighDst, edi

        call    Bs2ExitMode_pp32
BITS 16
        pop     es
        pop     ds
        pop     edx
        pop     ecx

        ; Continue reading and copying?
        cmp     fStatus, 0
        je      .the_load_loop

        ; Do we quit the loop on a failure?
        cmp     fStatus, 2
        je      .verify_failed_msg

        ;
        ; Done, so end the current message line.
        ;
        mov     al, 13
        call    PrintChr_r86
        mov     al, 10
        call    PrintChr_r86


        pop     esi
        pop     edi
        pop     ebx
        pop     ecx
        pop     edx
        pop     eax
        pop     ds
        pop     es
        mov     sp, bp
        pop     ebp
        ret


        ;
        ; Something went wrong, display a message.
        ;
.verify_failed_msg:
        mov     edi, pbHighDst
        push    edi
        push    cs
        push    .s_szVerifyFailed
        jmp     .print_message_and_panic

.param_error:
        push    ax
        push    cs
        push    .s_szParamError
        jmp     .print_message_and_panic

.read_error:
        push    ax
        push    cs
        push    .s_szReadError
        jmp     .print_message_and_panic

.print_message_and_panic:
        call    PrintF_r86
        call    Bs2Panic
        jmp     .print_message_and_panic

.s_szReadError:
        db 13, 10, 'Error reading: %RX8', 13, 10, 0
.s_szParamError:
        db 13, 10, 'Error getting params: %RX8', 13, 10, 0
.s_szVerifyFailed:
        db 13, 10, 'Failed to move block high... (%RX32) Got enough memory configured?', 13, 10, 0
.s_szLoadingBigImage:
        db 'Loading 2nd image.', 0
ENDPROC bs2LoadBigImage


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

