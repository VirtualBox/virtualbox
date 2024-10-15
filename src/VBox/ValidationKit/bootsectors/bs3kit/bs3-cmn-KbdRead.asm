; $Id: bs3-cmn-KbdRead.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3KbdRead.
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


;;
; Sends a read command to the keyboard controller and gets the result.
;
; The caller is responsible for making sure the keyboard controller is ready
; for a command (call Bs3KbdWait if unsure).
;
; @returns      The value read is returned (in al).
; @param        bCmd            The read command.
; @uses         al (obviously)
;
; @cproto   BS3_DECL(uint8_t) Bs3KbdRead_c16(uint8_t bCmd);
;
BS3_PROC_BEGIN_CMN Bs3KbdRead, BS3_PBC_NEAR
        push    xBP
        mov     xBP, xSP

        mov     al, [xBP + xCB*2]
        out     64h, al                 ; Write the command.

.check_status:
        in      al, 64h
        test    al, 1                   ; KBD_STAT_OBF
        jz      .check_status

        in      al, 60h                 ; Read the data.

        pop     xBP
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3KbdRead

;
; We may be using the near code in some critical code paths, so don't
; penalize it.
;
BS3_CMN_FAR_STUB   Bs3KbdRead, 2

