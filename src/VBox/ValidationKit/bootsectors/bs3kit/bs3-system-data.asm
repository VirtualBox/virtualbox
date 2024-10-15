; $Id: bs3-system-data.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - GDT
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

%include "bs3kit.mac"

%define BS3_SYSTEM16_BASE_16_23             ((BS3_ADDR_BS3SYSTEM16 >> 16) & 0xff)
%define BS3_SYSTEM16_BASE_LOW(a_DataSym)    ((BS3_DATA_NM(a_DataSym) - StartSystem16) & 0xffff)

;;
; The GDT (X86DESCGENERIC).
;
BS3_BEGIN_SYSTEM16
StartSystem16:
        db  10, 13, 'eye-catcher: SYSTEM16.......', 10, 13 ; 32 bytes long
BS3_GLOBAL_DATA Bs3Gdt, 4000h - 20h

;; Macro for checking GDT offsets as we go along.
;; @param       %1      The expected current offset.
%macro BS3GdtAssertOffset 1
 %ifndef KBUILD_GENERATING_MAKEFILE_DEPENDENCIES
  %if ($ - BS3_DATA_NM(Bs3Gdt)) != %1
   %assign offActual ($ - BS3_DATA_NM(Bs3Gdt))
   %error "BS3GdtAssertOffset: Bad offset: " %+ offActual %+ ", expected " %+ %1
  %endif
 %endif
%endmacro

        dw  00000h, 00000h, 00000h, 00000h      ; null selector
BS3GdtAssertOffset 8

        ;
        ; 008h..0f8h - System selectors and other stuff
        ;
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 008h - currently unused

BS3_GLOBAL_DATA Bs3Gdte_Ldt, 16                 ; Entry 010h
        dw  BS3_DATA_NM(Bs3LdtEnd) - BS3_DATA_NM(Bs3Ldt) - 1
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Ldt)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_LDT | 0x80
        dw  00000h
        dw  00000h, 00000h, 00000h, 00000h      ; zero for 64-bit mode.

BS3_GLOBAL_DATA Bs3Gdte_Tss16, 8                ; Entry 020h
        dw  0002bh                              ; 16-bit TSS.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss16)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_286_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss16DoubleFault, 8     ; Entry 028h
        dw  0002bh                              ; 16-bit TSS, double fault.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss16DoubleFault)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_286_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss16Spare0, 8          ; Entry 030h
        dw  0002bh                              ; 16-bit TSS, spare 0.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss16Spare0)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_286_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss16Spare1, 8          ; Entry 038h
        dw  0002bh                              ; 16-bit TSS, spare 0.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss16Spare1)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_286_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32, 8                ; Entry 040h
        dw  00067h                              ; 32-bit TSS.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32DoubleFault, 8     ; Entry 048h
        dw  00067h                              ; 32-bit TSS, double fault.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32DoubleFault)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32Spare0, 8          ; Entry 050h
        dw  00067h                              ; 32-bit TSS, spare 0.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32Spare0)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32Spare1, 8          ; Entry 058h
        dw  00067h                              ; 32-bit TSS, spare 1.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32Spare1)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32IobpIntRedirBm, 8  ; Entry 060h
                                                ; 32-bit TSS, with I/O permission & interrupt redirection bitmaps.
        dw  BS3_DATA_NM(Bs3SharedIobpEnd) - BS3_DATA_NM(Bs3Tss32WithIopb) - 1
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32WithIopb)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss32IntRedirBm, 8      ; Entry 068h
                                                ; 32-bit TSS, with interrupt redirection bitmap (IOBP stripped by limit).
        dw  BS3_DATA_NM(Bs3SharedIobp) - BS3_DATA_NM(Bs3Tss32WithIopb) - 1
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss32WithIopb)
        db  BS3_SYSTEM16_BASE_16_23
        db  X86_SEL_TYPE_SYS_386_TSS_AVAIL | 0x80
        dw  0

BS3_GLOBAL_DATA Bs3Gdte_Tss64, 8                ; Entry 070h
        dw  00067h                              ; 64-bit TSS.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss64)
        db  BS3_SYSTEM16_BASE_16_23
        db  AMD64_SEL_TYPE_SYS_TSS_AVAIL | 0x80
        dw  0
        dw  00000h, 00000h, 00000h, 00000h

BS3_GLOBAL_DATA Bs3Gdte_Tss64Spare0, 8          ; Entry 080h
        dw  00067h                              ; 64-bit TSS, spare 0.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss64Spare0)
        db  BS3_SYSTEM16_BASE_16_23
        db  AMD64_SEL_TYPE_SYS_TSS_AVAIL | 0x80
        dw  0
        dw  00000h, 00000h, 00000h, 00000h

BS3_GLOBAL_DATA Bs3Gdte_Tss64Spare1, 8          ; Entry 090h
        dw  00067h                              ; 64-bit TSS, spare 1.
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss64Spare1)
        db  BS3_SYSTEM16_BASE_16_23
        db  AMD64_SEL_TYPE_SYS_TSS_AVAIL | 0x80
        dw  0
        dw  00000h, 00000h, 00000h, 00000h

BS3_GLOBAL_DATA Bs3Gdte_Tss64Iobp, 8            ; Entry 0a0h
                                                ; 64-bit TSS, with I/O permission bitmap
        dw  BS3_DATA_NM(Bs3SharedIobp) - BS3_DATA_NM(Bs3Tss64WithIopb) - 1
        dw  BS3_SYSTEM16_BASE_LOW(Bs3Tss64WithIopb)
        db  BS3_SYSTEM16_BASE_16_23
        db  AMD64_SEL_TYPE_SYS_TSS_AVAIL | 0x80
        dw  0
        dw  00000h, 00000h, 00000h, 00000h

BS3GdtAssertOffset 0b0h
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0b0h - currently unused
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0b8h - currently unused
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0c0h - currently unused
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0c8h - currently unused
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0d0h - currently unused
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 0d8h - currently unused

        ; Misc selectors.
BS3_GLOBAL_DATA Bs3Gdte_RMTEXT16_CS, 8          ; Entry 0e0h
        dw  0fffeh, 00000h                      ; 16-bit conforming code (read+exec) segment, accessed. Will be finalized at startup.
        dw  09f00h, 00000h
BS3_GLOBAL_DATA Bs3Gdte_X0TEXT16_CS, 8          ; Entry 0e8h
        dw  0fffeh, 00000h                      ; 16-bit conforming code (read+exec) segment, accessed. Will be finalized at startup.
        dw  09f00h, 00000h
BS3_GLOBAL_DATA Bs3Gdte_X1TEXT16_CS, 8          ; Entry 0f0h
        dw  0fffeh, 00000h                      ; 16-bit conforming code (read+exec) segment, accessed. Will be finalized at startup.
        dw  09f00h, 00000h
BS3_GLOBAL_DATA Bs3Gdte_R0_MMIO16, 8            ; Entry 0f8h
        dw  0ffffh, 0f000h, 0930dh, 00000h      ; 16-bit VMMDev MMIO segment with base 0df000h.
BS3GdtAssertOffset 0100h


;;
; Macro that defines the selectors for ring-%1.
;
%macro BS3_GDT_RING_X_SELECTORS 1
BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _First, 80h
BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS16, 8     ; Entry 100h
        dw  0ffffh, (0xffff & BS3_ADDR_BS3TEXT16) ; 16-bit code segment with base 010000h.
        dw  09b01h | (%1 << 0dh) | (0xff & (BS3_ADDR_BS3TEXT16 >> 16)), 00000h | (0xff00 & (BS3_ADDR_BS3TEXT16 >> 16))

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _DS16, 8     ; Entry 108h
        dw  0ffffh, (0xffff & BS3_ADDR_BS3DATA16) ; 16-bit data segment with base 029000h.
        dw  09300h | (%1 << 0dh) | (0xff & (BS3_ADDR_BS3DATA16 >> 16)), 00000h | (0xff00 & (BS3_ADDR_BS3DATA16 >> 16))

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _SS16, 8     ; Entry 110h
        dw  0ffffh, 00000h                      ; 16-bit stack segment with base 0.
        dw  09300h | (%1 << 0dh), 00000h

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS32, 8     ; Entry 118h
        dw  0ffffh, 00000h                      ; 32-bit flat code segment.
        dw  09b00h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _DS32, 8     ; Entry 120h
        dw  0ffffh, 00000h                      ; 32-bit flat data segment.
        dw  09300h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _SS32, 8     ; Entry 128h
        dw  0ffffh, 00000h                      ; 32-bit flat stack segment.
        dw  09300h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS64, 8     ; Entry 130h
        dw  0ffffh, 00000h                      ; 64-bit code segment.
        dw  09a00h | (%1 << 0dh), 000afh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _DS64, 8     ; Entry 138h (also SS64)
        dw  0ffffh, 00000h                      ; 64-bit stack and data segment.
        dw  09300h | (%1 << 0dh), 000afh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS16_EO, 8  ; Entry 140h
        dw  0ffffh, (0xffff & BS3_ADDR_BS3TEXT16) ; 16-bit code segment with base 01000h, not accessed, execute only, short limit.
        dw  09800h | (%1 << 0dh) | (0xff & (BS3_ADDR_BS3TEXT16 >> 16)), 00000h | (0xff00 & (BS3_ADDR_BS3TEXT16 >> 16))

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS16_CNF, 8 ; Entry 148h
        dw  0ffffh, (0xffff & BS3_ADDR_BS3TEXT16) ; 16-bit code segment with base 01000h, not accessed, execute only, short limit.
        dw  09e00h | (%1 << 0dh) | (0xff & (BS3_ADDR_BS3TEXT16 >> 16)), 00000h | (0xff00 & (BS3_ADDR_BS3TEXT16 >> 16))

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS16_CND_EO, 8 ; Entry 150h
        dw  0fffeh, 00000h                      ; 16-bit conforming code segment with base 0, not accessed, execute only, short limit.
        dw  09c00h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS32_EO, 8  ; Entry 158h
        dw  0ffffh, 00000h                      ; 32-bit flat code segment, not accessed, execute only.
        dw  09800h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS32_CNF, 8  ; Entry 160h
        dw  0ffffh, 00000h                      ; 32-bit flat conforming code segment, not accessed.
        dw  09e00h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS32_CNF_EO, 8  ; Entry 168h
        dw  0ffffh, 00000h                      ; 32-bit flat conforming code segment, not accessed, execute only.
        dw  09c00h | (%1 << 0dh), 000cfh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS64_EO, 8  ; Entry 170h
        dw  0ffffh, 00000h                      ; 64-bit code segment, not accessed, execute only.
        dw  09800h | (%1 << 0dh), 000afh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS64_CNF, 8 ; Entry 178h
        dw  0ffffh, 00000h                      ; 64-bit conforming code segment, not accessed.
        dw  09e00h | (%1 << 0dh), 000afh

BS3_GLOBAL_DATA Bs3Gdte_R %+ %1 %+ _CS64_CNF_EO, 8 ; Entry 180h
        dw  0ffffh, 00000h                      ; 64-bit conforming code segment, execute only, not accessed.
        dw  09c00h | (%1 << 0dh), 000afh

;; @todo expand down segments.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 188h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 190h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 198h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1a0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1a8h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1b0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1b8h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1c0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1c8h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1d0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1d8h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1e0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1e8h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1f0h - unused.
        dw  00000h, 00000h, 00000h, 00000h      ; Entry 1f8h - unused.
%endmacro

        ;
        ; 100h..1f8h - Ring-0 selectors.
        ;
        BS3_GDT_RING_X_SELECTORS 0

        ;
        ; 200h..2f8h - Ring-1 selectors.
        ;
        BS3_GDT_RING_X_SELECTORS 1

        ;
        ; 300h..3f8h - Ring-2 selectors.
        ;
        BS3_GDT_RING_X_SELECTORS 2

        ;
        ; 400h..4f8h - Ring-3 selectors.
        ;
        BS3_GDT_RING_X_SELECTORS 3

        ;
        ; 500..5f8h - Named spare GDT entries.
        ;
BS3GdtAssertOffset 0500h
BS3_GLOBAL_DATA Bs3GdteSpare00, 8               ; Entry 500h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare01, 8               ; Entry 508h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare02, 8               ; Entry 510h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare03, 8               ; Entry 518h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare04, 8               ; Entry 520h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare05, 8               ; Entry 528h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare06, 8               ; Entry 530h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare07, 8               ; Entry 538h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare08, 8               ; Entry 540h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare09, 8               ; Entry 548h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0a, 8               ; Entry 550h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0b, 8               ; Entry 558h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0c, 8               ; Entry 560h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0d, 8               ; Entry 568h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0e, 8               ; Entry 570h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare0f, 8               ; Entry 578h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare10, 8               ; Entry 580h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare11, 8               ; Entry 588h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare12, 8               ; Entry 590h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare13, 8               ; Entry 598h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare14, 8               ; Entry 5a0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare15, 8               ; Entry 5a8h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare16, 8               ; Entry 5b0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare17, 8               ; Entry 5b8h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare18, 8               ; Entry 5c0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare19, 8               ; Entry 5c8h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1a, 8               ; Entry 5d0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1b, 8               ; Entry 5d8h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1c, 8               ; Entry 5e0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1d, 8               ; Entry 5e8h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1e, 8               ; Entry 5f0h
        dq 0
BS3_GLOBAL_DATA Bs3GdteSpare1f, 8               ; Entry 5f8h
        dq 0

        ;
        ; 600..df8h - 16-bit DPL=3 data segments covering the first 16MB of memory.
        ;
BS3_GLOBAL_DATA Bs3GdteTiled, 8                 ; Entry 600h
%assign u8HighBase 0
%rep 256
        dw  0ffffh, 00000h, 0f300h | u8HighBase, 00000h
%assign u8HighBase u8HighBase + 1
%endrep
        ;
        ; e00..ff8h - Free GDTEs.
        ;
BS3GdtAssertOffset 0e00h
BS3_GLOBAL_DATA Bs3GdteFreePart1, 200h
        times 200h db 0

        ;
        ; 1000h - the real mode segment number for BS3TEXT16. DPL=0, BASE=0x10000h, conforming, exec, read.
        ;
BS3GdtAssertOffset 01000h
BS3_GLOBAL_DATA Bs3Gdte_CODE16, 8h
        dw  0ffffh, 00000h, 09f01h, 00000h

        ;
        ; 1008..17f8h - Free GDTEs.
        ;
BS3GdtAssertOffset 01008h
BS3_GLOBAL_DATA Bs3GdteFreePart2, 07f8h
        times 07f8h db 0

        ;
        ; 1800..1ff8h - 16-bit DPL=0 data/stack segments covering the first 16MB of memory.
        ;
BS3GdtAssertOffset 01800h
BS3_GLOBAL_DATA Bs3GdteTiledR0, 8                 ; Entry 1800h
%assign u8HighBase 0
%rep 256
        dw  0ffffh, 00000h, 09300h | u8HighBase, 00000h
%assign u8HighBase u8HighBase + 1
%endrep

        ;
        ; 2000h - the real mode segment number for BS3SYSTEM. DPL=3. BASE=0x20000h
        ;
BS3GdtAssertOffset 02000h
BS3_GLOBAL_DATA Bs3Gdte_SYSTEM16, 8h
        dw  0ffffh, 00000h, 0f302h, 00000h

        ;
        ; 2008..28f8h - Free GDTEs.
        ;
BS3_GLOBAL_DATA Bs3GdteFreePart3, 08f8h
        times 08f8h db 0

        ;
        ; 2900h - the real mode segment number for BS3KIT_GRPNM_DATA16. DPL=3. BASE=0x29000h
        ;
BS3GdtAssertOffset 02900h
BS3_GLOBAL_DATA Bs3Gdte_DATA16, 8h
        dw  0ffffh, 09000h, 0f302h, 00000h

        ;
        ; 2908..29b0h - High DLL CSes.
        ;
BS3GdtAssertOffset 02908h
BS3_GLOBAL_DATA Bs3GdteHighDllCSes, 0c0h
        times 0c0h db 0

        ;
        ; 29b8..29f0h - High DLL DSes.
        ;
BS3GdtAssertOffset 029c8h
BS3_GLOBAL_DATA Bs3GdteHighDllDSes, 40h
        times 40h db 0

        ;
        ; 2908..2f98h - Free GDTEs.
        ;
BS3GdtAssertOffset 02a08h
BS3_GLOBAL_DATA Bs3GdteFreePart4, 598h
        times 598h db 0

        ;
        ; 2be0..2fe0h - 8 spare entries preceeding the test page which we're free
        ;               to mess with page table protection.
        ;
BS3GdtAssertOffset 02fa0h
BS3_GLOBAL_DATA Bs3GdtePreTestPage08, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage07, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage06, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage05, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage04, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage03, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage02, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdtePreTestPage01, 8
        dq 0

        ;
        ; 2fe0..3fd8h - 16 Test entries at the start of the page where we're free
        ;               to mess with page table protection.
        ;
BS3GdtAssertOffset 02fe0h
AssertCompile(($ - $$) == 0x3000)
BS3_GLOBAL_DATA Bs3GdteTestPage, 0
BS3_GLOBAL_DATA Bs3GdteTestPage00, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage01, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage02, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage03, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage04, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage05, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage06, 8
        dq 0
BS3_GLOBAL_DATA Bs3GdteTestPage07, 8
        dq 0
BS3GdtAssertOffset 3020h
        times 0fb8h db 0
BS3GdtAssertOffset 3fd8h
BS3_GLOBAL_DATA Bs3GdtEnd, 0
        db  10, 13, 'GDTE', 10, 13      ; alignment padding (next address on 16 byte boundrary).
BS3GdtAssertOffset 4000h - 20h ; We're at a page boundrary here! Only GDT and eyecatchers on page starting at 3000h!
AssertCompile(($ - $$) == 0x4000)



;;
; The 16-bit TSS.
;
BS3_GLOBAL_DATA Bs3Tss16, X86TSS16_size
istruc X86TSS16
        at X86TSS16.selPrev,    dw 0
        at X86TSS16.sp0,        dw BS3_ADDR_STACK_R0
        at X86TSS16.ss0,        dw BS3_SEL_R0_SS16
        at X86TSS16.sp1,        dw BS3_ADDR_STACK_R1
        at X86TSS16.ss1,        dw BS3_SEL_R1_SS16
        at X86TSS16.sp2,        dw BS3_ADDR_STACK_R2
        at X86TSS16.ss2,        dw BS3_SEL_R2_SS16
        at X86TSS16.ip,         dw 0
        at X86TSS16.flags,      dw 0
        at X86TSS16.ax,         dw 0
        at X86TSS16.cx,         dw 0
        at X86TSS16.dx,         dw 0
        at X86TSS16.bx,         dw 0
        at X86TSS16.sp,         dw 0
        at X86TSS16.bp,         dw 0
        at X86TSS16.si,         dw 0
        at X86TSS16.di,         dw 0
        at X86TSS16.es,         dw 0
        at X86TSS16.cs,         dw 0
        at X86TSS16.ss,         dw 0
        at X86TSS16.ds,         dw 0
        at X86TSS16.selLdt,     dw 0
iend

;;
; 16-bit TSS for (trying to) handle double faults.
BS3_GLOBAL_DATA Bs3Tss16DoubleFault, X86TSS16_size
istruc X86TSS16
        at X86TSS16.selPrev,    dw 0
        at X86TSS16.sp0,        dw BS3_ADDR_STACK_R0
        at X86TSS16.ss0,        dw BS3_SEL_R0_SS16
        at X86TSS16.sp1,        dw BS3_ADDR_STACK_R1
        at X86TSS16.ss1,        dw BS3_SEL_R1_SS16
        at X86TSS16.sp2,        dw BS3_ADDR_STACK_R2
        at X86TSS16.ss2,        dw BS3_SEL_R2_SS16
        at X86TSS16.ip,         dw 0 ; Will be filled in by routine setting up 16-bit mode w/ traps++.
        at X86TSS16.flags,      dw X86_EFL_1
        at X86TSS16.ax,         dw 0
        at X86TSS16.cx,         dw 0
        at X86TSS16.dx,         dw 0
        at X86TSS16.bx,         dw 0
        at X86TSS16.sp,         dw BS3_ADDR_STACK_R0_IST1
        at X86TSS16.bp,         dw 0
        at X86TSS16.si,         dw 0
        at X86TSS16.di,         dw 0
        at X86TSS16.es,         dw BS3_SEL_R0_DS16
        at X86TSS16.cs,         dw BS3_SEL_R0_CS16
        at X86TSS16.ss,         dw BS3_SEL_R0_SS16
        at X86TSS16.ds,         dw BS3_SEL_R0_DS16
        at X86TSS16.selLdt,     dw 0
iend

;;
; A spare 16-bit TSS for testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss16Spare0, X86TSS16_size
istruc X86TSS16
        at X86TSS16.selPrev,    dw 0
        at X86TSS16.sp0,        dw BS3_ADDR_STACK_R0
        at X86TSS16.ss0,        dw BS3_SEL_R0_SS16
        at X86TSS16.sp1,        dw BS3_ADDR_STACK_R1
        at X86TSS16.ss1,        dw BS3_SEL_R1_SS16
        at X86TSS16.sp2,        dw BS3_ADDR_STACK_R2
        at X86TSS16.ss2,        dw BS3_SEL_R2_SS16
        at X86TSS16.ip,         dw 0 ; Will be filled in by routine setting up 16-bit mode w/ traps++.
        at X86TSS16.flags,      dw X86_EFL_1
        at X86TSS16.ax,         dw 0
        at X86TSS16.cx,         dw 0
        at X86TSS16.dx,         dw 0
        at X86TSS16.bx,         dw 0
        at X86TSS16.sp,         dw BS3_ADDR_STACK_R0_IST2
        at X86TSS16.bp,         dw 0
        at X86TSS16.si,         dw 0
        at X86TSS16.di,         dw 0
        at X86TSS16.es,         dw BS3_SEL_R0_DS16
        at X86TSS16.cs,         dw BS3_SEL_R0_CS16
        at X86TSS16.ss,         dw BS3_SEL_R0_SS16
        at X86TSS16.ds,         dw BS3_SEL_R0_DS16
        at X86TSS16.selLdt,     dw 0
iend

;;
; A spare 16-bit TSS for testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss16Spare1, X86TSS16_size
istruc X86TSS16
        at X86TSS16.selPrev,    dw 0
        at X86TSS16.sp0,        dw BS3_ADDR_STACK_R0
        at X86TSS16.ss0,        dw BS3_SEL_R0_SS16
        at X86TSS16.sp1,        dw BS3_ADDR_STACK_R1
        at X86TSS16.ss1,        dw BS3_SEL_R1_SS16
        at X86TSS16.sp2,        dw BS3_ADDR_STACK_R2
        at X86TSS16.ss2,        dw BS3_SEL_R2_SS16
        at X86TSS16.ip,         dw 0 ; Will be filled in by routine setting up 16-bit mode w/ traps++.
        at X86TSS16.flags,      dw X86_EFL_1
        at X86TSS16.ax,         dw 0
        at X86TSS16.cx,         dw 0
        at X86TSS16.dx,         dw 0
        at X86TSS16.bx,         dw 0
        at X86TSS16.sp,         dw BS3_ADDR_STACK_R0_IST4
        at X86TSS16.bp,         dw 0
        at X86TSS16.si,         dw 0
        at X86TSS16.di,         dw 0
        at X86TSS16.es,         dw BS3_SEL_R0_DS16
        at X86TSS16.cs,         dw BS3_SEL_R0_CS16
        at X86TSS16.ss,         dw BS3_SEL_R0_SS16
        at X86TSS16.ds,         dw BS3_SEL_R0_DS16
        at X86TSS16.selLdt,     dw 0
iend


;;
; The 32-bit TSS.
;
BS3_GLOBAL_DATA Bs3Tss32, X86TSS32_size
istruc X86TSS32
        at X86TSS32.selPrev,        dw 0
        at X86TSS32.padding1,       dw 0
        at X86TSS32.esp0,           dd BS3_ADDR_STACK_R0
        at X86TSS32.ss0,            dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss0,    dw 1
        at X86TSS32.esp1,           dd 1
        at X86TSS32.ss1,            dw BS3_SEL_R1_SS32
        at X86TSS32.padding_ss1,    dw 1
        at X86TSS32.esp2,           dd 1
        at X86TSS32.ss2,            dw BS3_SEL_R2_SS32
        at X86TSS32.padding_ss2,    dw 1
        at X86TSS32.cr3,            dd 0
        at X86TSS32.eip,            dd 0
        at X86TSS32.eflags,         dd X86_EFL_1
        at X86TSS32.eax,            dd 0
        at X86TSS32.ecx,            dd 0
        at X86TSS32.edx,            dd 0
        at X86TSS32.ebx,            dd 0
        at X86TSS32.esp,            dd 0
        at X86TSS32.ebp,            dd 0
        at X86TSS32.esi,            dd 0
        at X86TSS32.edi,            dd 0
        at X86TSS32.es,             dw 0
        at X86TSS32.padding_es,     dw 0
        at X86TSS32.cs,             dw 0
        at X86TSS32.padding_cs,     dw 0
        at X86TSS32.ss,             dw 0
        at X86TSS32.padding_ss,     dw 0
        at X86TSS32.ds,             dw 0
        at X86TSS32.padding_ds,     dw 0
        at X86TSS32.fs,             dw 0
        at X86TSS32.padding_fs,     dw 0
        at X86TSS32.gs,             dw 0
        at X86TSS32.padding_gs,     dw 0
        at X86TSS32.selLdt,         dw 0
        at X86TSS32.padding_ldt,    dw 0
        at X86TSS32.fDebugTrap,     dw 0
        at X86TSS32.offIoBitmap,    dw (BS3_DATA_NM(Bs3SharedIobp) - BS3_DATA_NM(Bs3Tss32WithIopb))
iend

;;
; The 32-bit TSS for handling double faults.
BS3_GLOBAL_DATA Bs3Tss32DoubleFault, X86TSS32_size
istruc X86TSS32
        at X86TSS32.selPrev,        dw 0
        at X86TSS32.padding1,       dw 0
        at X86TSS32.esp0,           dd BS3_ADDR_STACK_R0
        at X86TSS32.ss0,            dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss0,    dw 1
        at X86TSS32.esp1,           dd 1
        at X86TSS32.ss1,            dw BS3_SEL_R1_SS32
        at X86TSS32.padding_ss1,    dw 1
        at X86TSS32.esp2,           dd 1
        at X86TSS32.ss2,            dw BS3_SEL_R2_SS32
        at X86TSS32.padding_ss2,    dw 1
        at X86TSS32.cr3,            dd 0 ; Will be filled in by routine setting up paged 32-bit mode w/ traps++.
        at X86TSS32.eip,            dd 0 ; Will be filled in by routine setting up 32-bit mode w/ traps++.
        at X86TSS32.eflags,         dd X86_EFL_1
        at X86TSS32.eax,            dd 0
        at X86TSS32.ecx,            dd 0
        at X86TSS32.edx,            dd 0
        at X86TSS32.ebx,            dd 0
        at X86TSS32.esp,            dd BS3_ADDR_STACK_R0_IST1
        at X86TSS32.ebp,            dd 0
        at X86TSS32.esi,            dd 0
        at X86TSS32.edi,            dd 0
        at X86TSS32.es,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_es,     dw 0
        at X86TSS32.cs,             dw BS3_SEL_R0_CS32
        at X86TSS32.padding_cs,     dw 0
        at X86TSS32.ss,             dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss,     dw 0
        at X86TSS32.ds,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_ds,     dw 0
        at X86TSS32.fs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_fs,     dw 0
        at X86TSS32.gs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_gs,     dw 0
        at X86TSS32.selLdt,         dw 0
        at X86TSS32.padding_ldt,    dw 0
        at X86TSS32.fDebugTrap,     dw 0
        at X86TSS32.offIoBitmap,    dw 0
iend

;;
; A spare 32-bit TSS testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss32Spare0, X86TSS32_size
istruc X86TSS32
        at X86TSS32.selPrev,        dw 0
        at X86TSS32.padding1,       dw 0
        at X86TSS32.esp0,           dd BS3_ADDR_STACK_R0
        at X86TSS32.ss0,            dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss0,    dw 1
        at X86TSS32.esp1,           dd 1
        at X86TSS32.ss1,            dw BS3_SEL_R1_SS32
        at X86TSS32.padding_ss1,    dw 1
        at X86TSS32.esp2,           dd 1
        at X86TSS32.ss2,            dw BS3_SEL_R2_SS32
        at X86TSS32.padding_ss2,    dw 1
        at X86TSS32.cr3,            dd 0 ; Will be filled in by routine setting up paged 32-bit mode w/ traps++.
        at X86TSS32.eip,            dd 0 ; Will be filled in by routine setting up 32-bit mode w/ traps++.
        at X86TSS32.eflags,         dd X86_EFL_1
        at X86TSS32.eax,            dd 0
        at X86TSS32.ecx,            dd 0
        at X86TSS32.edx,            dd 0
        at X86TSS32.ebx,            dd 0
        at X86TSS32.esp,            dd BS3_ADDR_STACK_R0_IST2
        at X86TSS32.ebp,            dd 0
        at X86TSS32.esi,            dd 0
        at X86TSS32.edi,            dd 0
        at X86TSS32.es,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_es,     dw 0
        at X86TSS32.cs,             dw BS3_SEL_R0_CS32
        at X86TSS32.padding_cs,     dw 0
        at X86TSS32.ss,             dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss,     dw 0
        at X86TSS32.ds,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_ds,     dw 0
        at X86TSS32.fs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_fs,     dw 0
        at X86TSS32.gs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_gs,     dw 0
        at X86TSS32.selLdt,         dw 0
        at X86TSS32.padding_ldt,    dw 0
        at X86TSS32.fDebugTrap,     dw 0
        at X86TSS32.offIoBitmap,    dw 0
iend

;;
; A spare 32-bit TSS testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss32Spare1, X86TSS32_size
istruc X86TSS32
        at X86TSS32.selPrev,        dw 0
        at X86TSS32.padding1,       dw 0
        at X86TSS32.esp0,           dd BS3_ADDR_STACK_R0
        at X86TSS32.ss0,            dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss0,    dw 1
        at X86TSS32.esp1,           dd 1
        at X86TSS32.ss1,            dw BS3_SEL_R1_SS32
        at X86TSS32.padding_ss1,    dw 1
        at X86TSS32.esp2,           dd 1
        at X86TSS32.ss2,            dw BS3_SEL_R2_SS32
        at X86TSS32.padding_ss2,    dw 1
        at X86TSS32.cr3,            dd 0 ; Will be filled in by routine setting up paged 32-bit mode w/ traps++.
        at X86TSS32.eip,            dd 0 ; Will be filled in by routine setting up 32-bit mode w/ traps++.
        at X86TSS32.eflags,         dd X86_EFL_1
        at X86TSS32.eax,            dd 0
        at X86TSS32.ecx,            dd 0
        at X86TSS32.edx,            dd 0
        at X86TSS32.ebx,            dd 0
        at X86TSS32.esp,            dd BS3_ADDR_STACK_R0_IST4
        at X86TSS32.ebp,            dd 0
        at X86TSS32.esi,            dd 0
        at X86TSS32.edi,            dd 0
        at X86TSS32.es,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_es,     dw 0
        at X86TSS32.cs,             dw BS3_SEL_R0_CS32
        at X86TSS32.padding_cs,     dw 0
        at X86TSS32.ss,             dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss,     dw 0
        at X86TSS32.ds,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_ds,     dw 0
        at X86TSS32.fs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_fs,     dw 0
        at X86TSS32.gs,             dw BS3_SEL_R0_DS32
        at X86TSS32.padding_gs,     dw 0
        at X86TSS32.selLdt,         dw 0
        at X86TSS32.padding_ldt,    dw 0
        at X86TSS32.fDebugTrap,     dw 0
        at X86TSS32.offIoBitmap,    dw 0
iend



;;
; 64-bit TSS
BS3_GLOBAL_DATA Bs3Tss64, X86TSS64_size
istruc X86TSS64
        at X86TSS64.u32Reserved,    dd 0
        at X86TSS64.rsp0,           dq BS3_ADDR_STACK_R0
        at X86TSS64.rsp1,           dq BS3_ADDR_STACK_R1
        at X86TSS64.rsp2,           dq BS3_ADDR_STACK_R2
        at X86TSS64.u32Reserved2,   dd 0
        at X86TSS64.ist1,           dq BS3_ADDR_STACK_R0_IST1
        at X86TSS64.ist2,           dq BS3_ADDR_STACK_R0_IST2
        at X86TSS64.ist3,           dq BS3_ADDR_STACK_R0_IST3
        at X86TSS64.ist4,           dq BS3_ADDR_STACK_R0_IST4
        at X86TSS64.ist5,           dq BS3_ADDR_STACK_R0_IST5
        at X86TSS64.ist6,           dq BS3_ADDR_STACK_R0_IST6
        at X86TSS64.ist7,           dq BS3_ADDR_STACK_R0_IST7
        at X86TSS64.u16Reserved,    dw 0
        at X86TSS64.offIoBitmap,    dw 0
iend

;;
; A spare TSS for testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss64Spare0, X86TSS64_size
istruc X86TSS64
        at X86TSS64.u32Reserved,    dd 0
        at X86TSS64.rsp0,           dq BS3_ADDR_STACK_R0
        at X86TSS64.rsp1,           dq BS3_ADDR_STACK_R1
        at X86TSS64.rsp2,           dq BS3_ADDR_STACK_R2
        at X86TSS64.u32Reserved2,   dd 0
        at X86TSS64.ist1,           dq BS3_ADDR_STACK_R0_IST1
        at X86TSS64.ist2,           dq BS3_ADDR_STACK_R0_IST2
        at X86TSS64.ist3,           dq BS3_ADDR_STACK_R0_IST3
        at X86TSS64.ist4,           dq BS3_ADDR_STACK_R0_IST4
        at X86TSS64.ist5,           dq BS3_ADDR_STACK_R0_IST5
        at X86TSS64.ist6,           dq BS3_ADDR_STACK_R0_IST6
        at X86TSS64.ist7,           dq BS3_ADDR_STACK_R0_IST7
        at X86TSS64.u16Reserved,    dw 0
        at X86TSS64.offIoBitmap,    dw 0
iend

;;
; A spare TSS for testcases to play around with.
BS3_GLOBAL_DATA Bs3Tss64Spare1, X86TSS64_size
istruc X86TSS64
        at X86TSS64.u32Reserved,    dd 0
        at X86TSS64.rsp0,           dq BS3_ADDR_STACK_R0
        at X86TSS64.rsp1,           dq BS3_ADDR_STACK_R1
        at X86TSS64.rsp2,           dq BS3_ADDR_STACK_R2
        at X86TSS64.u32Reserved2,   dd 0
        at X86TSS64.ist1,           dq BS3_ADDR_STACK_R0_IST1
        at X86TSS64.ist2,           dq BS3_ADDR_STACK_R0_IST2
        at X86TSS64.ist3,           dq BS3_ADDR_STACK_R0_IST3
        at X86TSS64.ist4,           dq BS3_ADDR_STACK_R0_IST4
        at X86TSS64.ist5,           dq BS3_ADDR_STACK_R0_IST5
        at X86TSS64.ist6,           dq BS3_ADDR_STACK_R0_IST6
        at X86TSS64.ist7,           dq BS3_ADDR_STACK_R0_IST7
        at X86TSS64.u16Reserved,    dw 0
        at X86TSS64.offIoBitmap,    dw 0
iend



;;
; 64-bit TSS sharing an I/O permission bitmap (Bs3SharedIobp) with a 32-bit TSS.
;
BS3_GLOBAL_DATA Bs3Tss64WithIopb, X86TSS64_size
istruc X86TSS64
        at X86TSS64.u32Reserved,    dd 0
        at X86TSS64.rsp0,           dq BS3_ADDR_STACK_R0
        at X86TSS64.rsp1,           dq BS3_ADDR_STACK_R1
        at X86TSS64.rsp2,           dq BS3_ADDR_STACK_R2
        at X86TSS64.u32Reserved2,   dd 0
        at X86TSS64.ist1,           dq BS3_ADDR_STACK_R0_IST1
        at X86TSS64.ist2,           dq BS3_ADDR_STACK_R0_IST2
        at X86TSS64.ist3,           dq BS3_ADDR_STACK_R0_IST3
        at X86TSS64.ist4,           dq BS3_ADDR_STACK_R0_IST4
        at X86TSS64.ist5,           dq BS3_ADDR_STACK_R0_IST5
        at X86TSS64.ist6,           dq BS3_ADDR_STACK_R0_IST6
        at X86TSS64.ist7,           dq BS3_ADDR_STACK_R0_IST7
        at X86TSS64.u16Reserved,    dw 0
        at X86TSS64.offIoBitmap,    dw (BS3_DATA_NM(Bs3SharedIobp) - BS3_DATA_NM(Bs3Tss64WithIopb))
iend

;;
; 32-bit TSS sharing an I/O permission bitmap (Bs3SharedIobp) with a 64-bit TSS,
; and sporting an interrupt redirection bitmap (Bs3SharedIntRedirBm).
BS3_GLOBAL_DATA Bs3Tss32WithIopb, X86TSS32_size
istruc X86TSS32
        at X86TSS32.selPrev,        dw 0
        at X86TSS32.padding1,       dw 0
        at X86TSS32.esp0,           dd BS3_ADDR_STACK_R0
        at X86TSS32.ss0,            dw BS3_SEL_R0_SS32
        at X86TSS32.padding_ss0,    dw 1
        at X86TSS32.esp1,           dd 1
        at X86TSS32.ss1,            dw BS3_SEL_R1_SS32
        at X86TSS32.padding_ss1,    dw 1
        at X86TSS32.esp2,           dd 1
        at X86TSS32.ss2,            dw BS3_SEL_R2_SS32
        at X86TSS32.padding_ss2,    dw 1
        at X86TSS32.cr3,            dd 0 ; Will be filled in by routine setting up paged 32-bit mode w/ traps++.
        at X86TSS32.eip,            dd 0 ; Will be filled in by routine setting up 32-bit mode w/ traps++.
        at X86TSS32.eflags,         dd X86_EFL_1
        at X86TSS32.eax,            dd 0
        at X86TSS32.ecx,            dd 0
        at X86TSS32.edx,            dd 0
        at X86TSS32.ebx,            dd 0
        at X86TSS32.esp,            dd 0
        at X86TSS32.ebp,            dd 0
        at X86TSS32.esi,            dd 0
        at X86TSS32.edi,            dd 0
        at X86TSS32.es,             dw 0
        at X86TSS32.padding_es,     dw 0
        at X86TSS32.cs,             dw 0
        at X86TSS32.padding_cs,     dw 0
        at X86TSS32.ss,             dw 0
        at X86TSS32.padding_ss,     dw 0
        at X86TSS32.ds,             dw 0
        at X86TSS32.padding_ds,     dw 0
        at X86TSS32.fs,             dw 0
        at X86TSS32.padding_fs,     dw 0
        at X86TSS32.gs,             dw 0
        at X86TSS32.padding_gs,     dw 0
        at X86TSS32.selLdt,         dw 0
        at X86TSS32.padding_ldt,    dw 0
        at X86TSS32.fDebugTrap,     dw 0
        at X86TSS32.offIoBitmap,    dw (BS3_DATA_NM(Bs3SharedIobp) - BS3_DATA_NM(Bs3Tss32WithIopb))
iend

;
; We insert 6 bytes before the interrupt redirection bitmap just to make sure
; we've all got the same idea about where it starts (i.e. 32 bytes before IOBP).
;
        times 6 db 0ffh

;;
; Interrupt redirection bitmap (used by 32-bit TSS).
BS3_GLOBAL_DATA Bs3SharedIntRedirBm, 32
        times 32 db 00h

;;
; Shared I/O permission bitmap used both by Bs3Tss64WithIopb and Bs3Tss32WithIopb.
BS3_GLOBAL_DATA Bs3SharedIobp, 8192+2
        times 8192+2 db 0ffh
BS3_GLOBAL_DATA Bs3SharedIobpEnd, 0


align   128

;;
; 16-bit IDT.
; This requires manual setup by code fielding traps, so we'll just reserve the
; memory here.
;
BS3_GLOBAL_DATA Bs3Idt16, 256*8
        times 256 dq 0

;;
; 32-bit IDT.
; This requires manual setup by code fielding traps, so we'll just reserve the
; memory here.
;
BS3_GLOBAL_DATA Bs3Idt32, 256*8
        times 256 dq 0

;;
; 64-bit IDT.
; This requires manual setup by code fielding traps, so we'll just reserve the
; memory here.
;
BS3_GLOBAL_DATA Bs3Idt64, 256*16
        times 256 dq 0, 0


        times 6 db 0                            ; Pad the first LIDT correctly.

;;
; LIDT structure for the 16-bit IDT (8-byte aligned on offset).
BS3_GLOBAL_DATA Bs3Lidt_Idt16, 2+8
        dw      256*8 - 1                       ; limit
        dw      BS3_SYSTEM16_BASE_LOW(Bs3Idt16) ; low offset
        dw      (BS3_ADDR_BS3SYSTEM16 >> 16)    ; high offset
        dd      0                               ; top32 offset

        times 4 db 0                            ; padding the start of the next

;;
; LIDT structure for the 32-bit IDT (8-byte aligned on offset).
BS3_GLOBAL_DATA Bs3Lidt_Idt32, 2+8
        dw      256*8 - 1                       ; limit
        dw      BS3_SYSTEM16_BASE_LOW(Bs3Idt32) ; low offset
        dw      (BS3_ADDR_BS3SYSTEM16 >> 16)    ; high offset
        dd      0                               ; top32 offset

        times 4 db 0                            ; padding the start of the next

;;
; LIDT structure for the 64-bit IDT (8-byte aligned on offset).
BS3_GLOBAL_DATA Bs3Lidt_Idt64, 2+8
        dw      256*16 - 1                      ; limit
        dw      BS3_SYSTEM16_BASE_LOW(Bs3Idt64) ; low offset
        dw      (BS3_ADDR_BS3SYSTEM16 >> 16)    ; high offset
        dd      0                               ; top32 offset

        times 4 db 0                            ; padding the start of the next

;;
; LIDT structure for the real mode IVT at address 0x00000000 (8-byte aligned on offset).
BS3_GLOBAL_DATA Bs3Lidt_Ivt, 2+8
        dw      0ffffh                          ; limit
        dw      0                               ; low offset
        dw      0                               ; high offset
        dd      0                               ; top32 offset

        times 4 db 0                            ; padding the start of the next

;;
; LGDT structure for the current GDT (8-byte aligned on offset).
BS3_GLOBAL_DATA Bs3Lgdt_Gdt, 2+8
        dw      BS3_DATA_NM(Bs3GdtEnd) - BS3_DATA_NM(Bs3Gdt) - 1 ; limit
        dw      BS3_SYSTEM16_BASE_LOW(Bs3Gdt)                    ; low offset
        dw      (BS3_ADDR_BS3SYSTEM16 >> 16)                     ; high offset
        dd      0                                                ; top32 offset

;;
; LGDT structure for the default GDT (8-byte aligned on offset).
; This must not be modified, whereas Bs3Lgdt_Gdt can be modified by the user.
BS3_GLOBAL_DATA Bs3LgdtDef_Gdt, 2+8
        dw      BS3_DATA_NM(Bs3GdtEnd) - BS3_DATA_NM(Bs3Gdt) - 1 ; limit
        dw      BS3_SYSTEM16_BASE_LOW(Bs3Gdt)                    ; low offset
        dw      (BS3_ADDR_BS3SYSTEM16 >> 16)                     ; high offset
        dd      0                                                ; top32 offset



align   16
;;
; LDT filling up the rest of the segment.
;
; Currently this starts at 0x84e0, which leaves us with 0xb20 bytes.  We'll use
; the last 32 of those for an eye catcher.
;
BS3_GLOBAL_DATA Bs3Ldt, 0b20h - 32
        times (0b20h - 32) db 0
BS3_GLOBAL_DATA Bs3LdtEnd, 0
        db  10, 13, 'eye-catcher: SYSTEM16 END', 10, 13, 0, 0, 0 ; 32 bytes long

;
; Check the segment size.
;
%ifndef KBUILD_GENERATING_MAKEFILE_DEPENDENCIES
 %if ($ - $$) != 09000h
  %assign offActual ($ - $$)
  %error "Bad BS3SYSTEM16 segment size: " %+ offActual %+ ", expected 0x9000 (36864)"
 %endif
%endif

