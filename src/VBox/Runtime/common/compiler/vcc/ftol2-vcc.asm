; $Id: ftol2-vcc.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Floating Point to Integer related Visual C++ support routines.
;

;
; Copyright (C) 2022-2024 Oracle and/or its affiliates.
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


%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


;;
; Convert st0 to integer returning it in eax, popping st0.
;
; @returns  value in eax
; @param    st0     The value to convet.  Will be popped.
; @uses     eax, st0, FSW.TOP
;
GLOBALNAME_RAW  __ftol2_sse_excpt, function, RT_NOTHING
GLOBALNAME_RAW  __ftol2_sse, function, RT_NOTHING ;; @todo kind of expect __ftol2_sse to take input in xmm0 and return in edx:eax.
BEGINPROC_RAW   __ftoi2
        push    ebp
        mov     ebp, esp
        sub     esp, 8h
        fisttp  dword [esp]
        mov     eax, [esp]
        leave
        ret
ENDPROC_RAW     __ftoi2


;;
; Convert st0 to integer returning it in edx:eax, popping st0.
;
; @returns  value in edx:eax
; @param    st0     The value to convet.  Will be popped.
; @uses     eax, edx, st0, FSW.TOP
;
BEGINPROC_RAW   __ftol2
        push    ebp
        mov     ebp, esp
        sub     esp, 8h
        and     esp, 0fffffff8h             ; proper alignment.
        fisttp  qword [esp]
        mov     eax, [esp]
        mov     edx, [esp + 4]
        leave
        ret
ENDPROC_RAW     __ftol2

