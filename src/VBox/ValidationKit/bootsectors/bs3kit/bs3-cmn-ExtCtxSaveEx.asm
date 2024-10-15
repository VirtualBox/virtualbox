; $Id: bs3-cmn-ExtCtxSaveEx.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3ExtCtxSaveEx.
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

%include "bs3kit-template-header.mac"


%if ARCH_BITS != 64
BS3_EXTERN_DATA16 g_bBs3CurrentMode

BS3_BEGIN_TEXT64
extern  _Bs3ExtCtxSave_c64
 %if ARCH_BITS == 16
extern  BS3_CMN_NM(Bs3SelProtFar16DataToFlat)
extern  _Bs3SwitchTo16Bit_c64
 %else
extern  _Bs3SwitchTo32Bit_c64
 %endif

TMPL_BEGIN_TEXT
extern  BS3_CMN_NM(Bs3SwitchTo64Bit)
 %if ARCH_BITS == 16
extern  BS3_CMN_NM(Bs3SelProtFar16DataToFlat)
 %endif
%endif

extern BS3_CMN_NM(Bs3ExtCtxSave)



;;
; Saves the extended CPU context (FPU, SSE, AVX, ++), full 64-bit
; when in long mode.
;
; @param    pExtCtx
;
BS3_PROC_BEGIN_CMN Bs3ExtCtxSaveEx, BS3_PBC_NEAR
%if ARCH_BITS == 64
        jmp     BS3_CMN_NM(Bs3ExtCtxSave)
%else
        push    xBP
        mov     xBP, xSP
        push    sAX

        ;
        ; Check if we're in long mode.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        and     al, BS3_MODE_SYS_MASK
        cmp     al, BS3_MODE_SYS_LM
        je      .in_long_mode

        ;
        ; Not in long mode, so do normal save.
        ;
        pop     sAX
        leave
        jmp     BS3_CMN_NM(Bs3ExtCtxSave)

        ;
        ; Switch to 64-bit to do the restoring so we can save the 64-bit only state.
        ;
.in_long_mode:
        push    sCX
BONLY16 push    sDX

        ; Load ecx with the flat pExtCtx address.
        mov     ecx, [xBP + xCB + cbCurRetAddr]

 %if ARCH_BITS == 16
        push    ecx
        call    BS3_CMN_NM(Bs3SelProtFar16DataToFlat)
        mov     ecx, edx
        shl     ecx, 16
        mov     cx, ax
 %endif

        ; Switch to 64-bit mode.
        call    BS3_CMN_NM(Bs3SwitchTo64Bit)
        BITS    64

        ; Do the save.
        sub     rsp, 20h
        call    _Bs3ExtCtxSave_c64
        add     rsp, 20h

        ; Switch back to the original mode.
 %if ARCH_BITS == 16
        call    _Bs3SwitchTo16Bit_c64
 %else
        call    _Bs3SwitchTo32Bit_c64
 %endif
        BITS    ARCH_BITS

        ; Save context and return.
BONLY16 pop     sDX
        pop     sCX

        pop     sAX
        leave
        ret
%endif
BS3_PROC_END_CMN   Bs3ExtCtxSaveEx

