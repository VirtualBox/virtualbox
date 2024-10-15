; $Id: bs3-mode-NameShortLower.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - g_szBs3ModeName_xxx
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


BS3_BEGIN_DATA16
%undef  MY_MODE_NAME_STR
%defstr MY_MODE_NAME_STR TMPL_MODE_LNAME
BS3_GLOBAL_NAME_EX RT_CONCAT3(g_szBs3ModeNameShortLower, _, TMPL_MODE_LNAME), , %strlen(MY_MODE_NAME_STR)
BS3_GLOBAL_NAME_EX RT_CONCAT3(_g_szBs3ModeNameShortLower, _, TMPL_MODE_LNAME), , %strlen(MY_MODE_NAME_STR)
        db MY_MODE_NAME_STR, 0

