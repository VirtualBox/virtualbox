; $Id: bs3-mode-MemCopyFlat.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3MemCopyFlat
;

;
; Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"


;*********************************************************************************************************************************
;*  External symbols                                                                                                             *
;*********************************************************************************************************************************
TMPL_BEGIN_TEXT
extern TMPL_NM(Bs3SwitchToRM)
BS3_BEGIN_TEXT16
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)

BS3_EXTERN_DATA16   g_aBs3RmIvtOriginal
%ifdef TMPL_16BIT
extern              NAME(Bs3SwitchTo32BitAndCallC_rm_far)
%else
BS3_EXTERN_CMN      Bs3MemCpy
%endif



;;
; Copies memory from/to real mode.
;
; ASSUMES 386+.
;
; @cproto   BS3_MODE_PROTO_STUB(void BS3_FAR *, Bs3MemCopyFlat,(uint32_t uFlatDst, uint32_t uFlatSrc, uint32_t cbToCopy));
;
TMPL_BEGIN_TEXT
BS3_PROC_BEGIN_MODE Bs3MemCopyFlat, BS3_PBC_HYBRID
%ifdef TMPL_64BIT
        mov     ecx, ecx
        mov     edx, edx
        mov     r8d, r8d
        jmp     Bs3MemCpy
%elifdef TMPL_32BIT
        jmp     Bs3MemCpy
%else
        push    xBP
        mov     xBP, xSP

        ; Just call Bs3SwitchTo32BitAndCallC w/ Bs3MemCpy_c32
        push    dword [xBP + xCB + cbCurRetAddr + 4 + 4]
        push    dword [xBP + xCB + cbCurRetAddr + 4]
        push    dword [xBP + xCB + cbCurRetAddr]
        push    word 4 * 3
        push    seg  .copy32_bit_worker
        push    word .copy32_bit_worker
        call far NAME(Bs3SwitchTo32BitAndCallC_rm_far)

        leave
        BS3_HYBRID_RET

        BITS    32
.copy32_bit_worker:
        push    ebp
        mov     ebp, esp
        push    edi
        push    esi
        push    ecx

        mov     edi, [ebp + 8]
        mov     esi, [ebp + 12]
        mov     ecx, [ebp + 16]
        cld
        rep movsb

        pop     ecx
        pop     esi
        pop     edi
        leave
        ret
        BITS    16
%endif
BS3_PROC_END_MODE   Bs3MemCopyFlat

