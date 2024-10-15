; $Id: bs3-mode-TestDoModesByOneStub.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TestDoModesByOne near stub.
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
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"

;
; Near stub for the API call (16-bit only).
;
%if TMPL_BITS == 16
 %if TMPL_MODE == BS3_MODE_RM
BS3_BEGIN_RMTEXT16
 %endif
BS3_BEGIN_TEXT16_NEARSTUBS
BS3_PROC_BEGIN_MODE Bs3TestDoModesByOne, BS3_PBC_NEAR
        pop     ax
        push    cs
        push    ax
 %if TMPL_MODE == BS3_MODE_RM
        extern TMPL_FAR_NM(Bs3TestDoModesByOne):wrt BS3GROUPRMTEXT16
        jmp far TMPL_FAR_NM(Bs3TestDoModesByOne)
 %else
        extern TMPL_FAR_NM(Bs3TestDoModesByOne):wrt CGROUP16
        jmp     TMPL_NM(Bs3TestDoModesByOne)
 %endif
BS3_PROC_END_MODE   Bs3TestDoModesByOne
%endif

