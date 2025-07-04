; $Id: bs3-cmn-TestQueryCfgU8.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; BS3Kit - Bs3TestQueryCfgU8, Bs3TestQueryCfgBool.
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
%include "VBox/VMMDevTesting.mac"

BS3_EXTERN_DATA16 g_fbBs3VMMDevTesting
TMPL_BEGIN_TEXT

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestQueryCfgU8(uint16_t uCfg, uint8_t bDefault);
; @cproto   BS3_DECL(bool)    Bs3TestQueryCfgBool(uint16_t uCfg, bool fDefault);
;
BS3_GLOBAL_NAME_EX BS3_CMN_NM(Bs3TestQueryCfgBool), function, 3
BS3_PROC_BEGIN_CMN Bs3TestQueryCfgU8, BS3_PBC_HYBRID
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        push    xDX
        push    xCX

        cmp     byte [BS3_DATA16_WRT(g_fbBs3VMMDevTesting)], 0
        je      .no_vmmdev

        ; Issue the query command.
        mov     dx, VMMDEV_TESTING_IOPORT_CMD
%if TMPL_BITS == 16
        mov     ax, VMMDEV_TESTING_CMD_QUERY_CFG - VMMDEV_TESTING_CMD_MAGIC_HI_WORD
        out     dx, ax
%else
        mov     eax, VMMDEV_TESTING_CMD_QUERY_CFG
        out     dx, eax
%endif

        ; Write what we wish to query.
        mov     ax, [xBP + xCB + cbCurRetAddr]
        mov     dx, VMMDEV_TESTING_IOPORT_DATA
        out     dx, ax

        ; Read back the result.
        in      al, dx
%if TMPL_BITS == 16
        mov     cl, al
        in      ax, dx                      ; check the 'okay'
        cmp     ax, VMMDEV_TESTING_QUERY_CFG_OKAY_TAIL & 0xffff
        mov     al, cl
        mov     ah, 0
%else
        movzx   ecx, al
        in      eax, dx                     ; check the 'okay'
        cmp     eax, VMMDEV_TESTING_QUERY_CFG_OKAY_TAIL
        mov     eax, ecx
%endif
        jne     .no_vmmdev

.return:
        pop     xCX
        pop     xDX
        pop     xBP
        BS3_CALL_CONV_EPILOG 2
        BS3_HYBRID_RET

.no_vmmdev:
%if TMPL_BITS == 16
        mov     al, [xBP + xCB + cbCurRetAddr + xCB]
%else
        movzx   eax, byte [xBP + xCB + cbCurRetAddr + xCB]
%endif
        jmp     .return
BS3_PROC_END_CMN   Bs3TestQueryCfgU8

%if TMPL_BITS == 16
BS3_GLOBAL_NAME_EX BS3_CMN_NM_FAR(Bs3TestQueryCfgBool), function, 3
        jmp     BS3_CMN_NM_FAR(Bs3TestQueryCfgU8)
%endif
