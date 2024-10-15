; $Id: tstAsm3DNow-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - 3DNow!
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

%include "tstAsm.mac"

    BITS TEST_BITS

        pavgusb     mm1, mm0
        pf2id       mm5, mm4
        pf2iw       mm6, mm3
        pfacc       mm7, mm2
        pfadd       mm5, mm4
        pfcmpeq     mm6, mm3
        pfcmpge     mm2, mm7
        pfcmpgt     mm4, mm5
        pfmax       mm3, mm6
        pfmin       mm1, mm0
        pfmul       mm5, mm4
        pmulhrwa    mm3, mm6
        pfnacc      mm4, mm5
        pfpnacc     mm3, mm6
        pfrcp       mm0, mm1
        pfrcpit1    mm2, mm7
        pfrcpit2    mm4, mm5
        pfrsqrt     mm7, mm2
        pfrsqit1    mm1, mm0
        pfsub       mm6, mm3
        pfsubr      mm0, mm1
        pi2fd       mm7, mm2
        pi2fw       mm0, mm1
        pswapd      mm2, mm7

%if  TEST_BITS == 16
 %define SIB(extra)
 %define SIB(extra)
%else
 %define SIB(extra) extra
%endif
        pf2id       mm5, qword [ds:xSI SIB(+000101010h)]
        pf2iw       mm6, qword [fs:xSI SIB(+000101010h)]
        pfacc       mm7, qword [gs:xSI SIB(+000101010h)]
        pfadd       mm5, qword [   xSI SIB(+000101010h)]
        pfcmpeq     mm6, qword [   xDI SIB(*8+000101010h)]
        pfcmpge     mm2, qword [es:xSI SIB(+000100010h)]
        pfcmpgt     mm4, qword [es:xSI SIB(+000101010h)]
        pfmax       mm3, qword [es:xSI SIB(+000101010h)]
        pfmin       mm1, qword [es:xSI SIB(+000101010h)]
        pfmul       mm5, qword [es:xSI SIB(+000101000h)]
        pfrcpit1    mm2, qword [es:xBP SIB(+000101510h)]
%if TEST_BITS != 16
        pavgusb     mm1, qword [es:xAX SIB(+000000010h)]
        pfpnacc     mm3, qword [es:xDX SIB(+000102900h)]
        pfrcp       mm0, qword [es:xCX SIB(+000101020h)]
        pmulhrwa    mm3, qword [es:xAX SIB(+0ffffffffh)]
        pfrcpit2    mm4, qword [es:xSP SIB(+000101310h)]
%endif
        pfnacc      mm4, qword [es:xBX SIB(+000101010h)]
        pfrsqrt     mm7, qword [es:xSI SIB(+0f0106010h)]
        pfrsqit1    mm1, qword [es:xDI SIB(+0001f1010h)]
        pfsub       mm6, qword [es:xSI SIB(*2)]
        pfsubr      mm0, qword [es:xSI SIB(*3)]
        pi2fd       mm7, qword [es:xSI SIB(*4)]
        pi2fw       mm0, qword [es:xSI SIB(*5)]
        pswapd      mm2, qword [es:xSI SIB(*8)]

        pmulhrwa mm0, qword [ds:xBP SIB(+xDI*8+00f000001h)]

