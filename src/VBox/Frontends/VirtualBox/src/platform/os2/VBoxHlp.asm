; $Id: VBoxHlp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBox Qt GUI - Implementation of OS/2-specific helpers that require to reside in a DLL.
;
; This stub is used to avoid linking the helper DLL to the C runtime.
;

;
; Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

;; @todo BEGINCODE gives us this:
;
;   02-03-2008  22:19:37  SYS3175  PID 4383  TID 0001  Slot 0076
;   D:\CODING\INNOTEK\VBOX\OUT\OS2.X86\RELEASE\BIN\VIRTUALBOX.EXE
;   c0000005
;   17d40000
;   P1=00000008  P2=0000bea4  P3=XXXXXXXX  P4=XXXXXXXX
;   EAX=00001489  EBX=00000000  ECX=00000000  EDX=00000000
;   ESI=00000000  EDI=00001489
;   DS=be7f  DSACC=00f3  DSLIM=0000003f
;   ES=0053  ESACC=f0f3  ESLIM=ffffffff
;   FS=150b  FSACC=00f3  FSLIM=00000030
;   GS=0000  GSACC=****  GSLIM=********
;   CS:EIP=bea7:00000000  CSACC=00f2  CSLIM=00000002
;   SS:ESP=01d7:0000ffe8  SSACC=00f3  SSLIM=0000ffff
;   EBP=00000000  FLG=00012202
;
;   VBOXHLP.DLL 0003:00000000
;
; Looks like the previous 'segment TEXT32 ...' definition in asmdefs.mac
; is ignored and the segment is redefined as if it had no 'CLASS=CODE...'
; attributes...

;%include "iprt/asmdefs.mac"
;
;BEGINCODE

segment TEXT32 public CLASS=CODE align=16 use32 flat

extern _DLL_InitTerm

; Low-level DLL entry point - Forward to the C code.
..start:
    jmp _DLL_InitTerm


; emxomfld may generate references to this for weak symbols. It is usually
; found in in libend.lib.
ABSOLUTE 0
global WEAK$ZERO
WEAK$ZERO:

