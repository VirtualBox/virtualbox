; $Id: bs3-cmn-ExtCtxRestore.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3ExtCtxRestore.
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

extern BS3_CMN_NM(Bs3RegSetXcr0)


;;
; Restores the extended CPU context (FPU, SSE, AVX, ++).
;
; @param    pExtCtx
;
BS3_PROC_BEGIN_CMN Bs3ExtCtxRestore, BS3_PBC_NEAR
        push    xBP
        mov     xBP, xSP
        push    sAX
        push    sCX
        push    sDX
        push    xBX
BONLY16 push    es

%if ARCH_BITS == 16
        les     bx, [xBP + xCB + cbCurRetAddr]
        mov     al, [es:bx + BS3EXTCTX.enmMethod]
        cmp     al, BS3EXTCTXMETHOD_XSAVE
        je      .do_16_xsave
        cmp     al, BS3EXTCTXMETHOD_FXSAVE
        je      .do_16_fxsave
        cmp     al, BS3EXTCTXMETHOD_ANCIENT
        je      .do_16_ancient
        int3

.do_16_ancient:
        frstor  [es:bx + BS3EXTCTX.Ctx]
        jmp     .return

.do_16_fxsave:
        fxrstor  [es:bx + BS3EXTCTX.Ctx]
        jmp     .return

.do_16_xsave:
        push    dword [es:bx + BS3EXTCTX.fXcr0Nominal + 4]
        push    dword [es:bx + BS3EXTCTX.fXcr0Nominal]
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        mov     eax, [es:bx + BS3EXTCTX.fXcr0Nominal]
        mov     edx, [es:bx + BS3EXTCTX.fXcr0Nominal + 4]
        xrstor  [es:bx + BS3EXTCTX.Ctx]

        push    dword [es:bx + BS3EXTCTX.fXcr0Saved + 4]
        push    dword [es:bx + BS3EXTCTX.fXcr0Saved]
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        add     xSP, 4 * 2 * 2          ; clean up both calls
        ;jmp     .return

%else
BONLY32 mov     ebx, [xBP + xCB + cbCurRetAddr]
BONLY64 mov     rbx, rcx

        mov     al, [xBX + BS3EXTCTX.enmMethod]
        cmp     al, BS3EXTCTXMETHOD_XSAVE
        je      .do_xsave
        cmp     al, BS3EXTCTXMETHOD_FXSAVE
        je      .do_fxsave
        cmp     al, BS3EXTCTXMETHOD_ANCIENT
        je      .do_ancient
        int3

.do_ancient:
        frstor  [xBX + BS3EXTCTX.Ctx]
        jmp     .return

.do_fxsave:
BONLY32 fxrstor [xBX + BS3EXTCTX.Ctx]
BONLY64 fxrstor64 [xBX + BS3EXTCTX.Ctx]
        jmp     .return

.do_xsave:
 %if ARCH_BITS == 32
        push    dword [xBX + BS3EXTCTX.fXcr0Nominal + 4]
        push    dword [xBX + BS3EXTCTX.fXcr0Nominal]
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        mov     eax, [xBX + BS3EXTCTX.fXcr0Nominal]
        mov     edx, [xBX + BS3EXTCTX.fXcr0Nominal + 4]
        xrstor  [xBX + BS3EXTCTX.Ctx]

        push    dword [xBX + BS3EXTCTX.fXcr0Saved + 4]
        push    dword [xBX + BS3EXTCTX.fXcr0Saved]
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        add     xSP, 4 * 2 * 2          ; clean up both calls
 %else
        mov     rcx, [xBX + BS3EXTCTX.fXcr0Nominal]
        push    rcx                     ; just for reserving parameter dumping space needed by Bs3RegSetXcr0
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        mov     eax, [xBX + BS3EXTCTX.fXcr0Nominal]
        mov     edx, [xBX + BS3EXTCTX.fXcr0Nominal + 4]
        xrstor64 [xBX + BS3EXTCTX.Ctx]

        mov     rcx, [xBX + BS3EXTCTX.fXcr0Saved]
        call    BS3_CMN_NM(Bs3RegSetXcr0)

        add     xSP, 8                  ; clean up parameter space
        ;jmp     .return
  %endif
%endif

.return:
BONLY16 pop     es
        pop     xBX
        pop     sDX
        pop     sCX
        pop     sAX
        mov     xSP, xBP
        pop     xBP
        ret
BS3_PROC_END_CMN   Bs3ExtCtxRestore

