; $Id: security-cookie-vcc.asm 115003 2026-08-12 23:16:08Z knut.osmundsen@oracle.com $
;; @file
; IPRT - Stack related Visual C++ support routines, ring-0.
;

;
; Copyright (C) 2022-2026 Oracle and/or its affiliates.
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
%define RT_ASM_WITH_SEH64_ALT
%include "iprt/asmdefs.mac"


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
;; The default security cookie.
; Can be re-defined via ASDEFS.
; @note The top 16-bit must be zero because __security_check_cookie in
;       BufferOverflowK.lib expects them to be all 1s when xor'ed with rsp.
;       See gh-590.  (32-bit code doesn't care about the top half.)
%ifndef  RT_VCC_SECURITY_COOKIE_DEFAULT_LOW
 %define RT_VCC_SECURITY_COOKIE_DEFAULT_LOW     0xdeadbeef
%endif
%ifndef  RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH
 %define RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH    0x0000c0de
%endif
%if RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH & 0xffff0000
 %error "The top 16-bits of RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH must be zero!"
%endif

;; The what we XOR the RDTSC output with when initializing the cookie.
; Can be re-defined via ASDEFS.
%ifndef  RT_VCC_SECURITY_COOKIE_XOR_LOW
 %define RT_VCC_SECURITY_COOKIE_XOR_LOW         0xc22f3ec7
%endif
%ifndef  RT_VCC_SECURITY_COOKIE_XOR_HIGH
 %define RT_VCC_SECURITY_COOKIE_XOR_HIGH        0x4ab98ec4
%endif


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BEGINDATA
GLOBALNAME __security_cookie
        dd  RT_VCC_SECURITY_COOKIE_DEFAULT_LOW
        dd  RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH
;;
; The complement security cookie seems only to be used in the BugCheck, in
; order to verify the validity of __security_cookie.
GLOBALNAME __security_cookie_complement
        dd  ~(RT_VCC_SECURITY_COOKIE_DEFAULT_LOW)  & 0xffffffff
        dd  ~(RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH) & 0xffffffff


;;
; Initializes the security cookie.
;
; @cproto void __cdecl __security_init_cookie(void);
;
BEGINPROC __security_init_cookie
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        SEH64_END_PROLOGUE

        ; Don't initialize it again if already done.
        cmp     dword [RT_WRT_RIP(NAME(__security_cookie))], RT_VCC_SECURITY_COOKIE_DEFAULT_LOW
        je      .need_init
        cmp     dword [RT_WRT_RIP(NAME(__security_cookie) + 4)], RT_VCC_SECURITY_COOKIE_DEFAULT_HIGH
        jne     .already_initialized

        ; Use TSC to get a random-ish number.
.need_init:
        rdtsc

        ; XOR with random number.
        xor     eax, RT_VCC_SECURITY_COOKIE_XOR_LOW
        xor     edx, RT_VCC_SECURITY_COOKIE_XOR_HIGH

        ; Let KASLR do some work.
        lea     xCX, [RT_WRT_RIP(NAME(__security_cookie) + 4)]
        xor     eax, ecx
        xor     edx, esp

%ifdef RT_ARCH_AMD64
        ; Bits 63:48 must be zero, see @note above.
        ; Note! We drop the cleared bits and does not carry them forwared in
        ;       the __security_cookie_complement value (what's the point).
        and     edx, 0xffff
%endif

        ; Store the result.
.store_result:
        mov     [RT_WRT_RIP(NAME(__security_cookie))], eax
        mov     [RT_WRT_RIP(NAME(__security_cookie) + 4)], edx

        not     eax
        not     edx
        mov     [RT_WRT_RIP(NAME(__security_cookie_complement))], eax
        mov     [RT_WRT_RIP(NAME(__security_cookie_complement) + 4)], edx

.already_initialized:
        leave
        ret
ENDPROC   __security_init_cookie

MARK_OBJECT_RETPOLINE_SAFE
