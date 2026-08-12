; $Id: VBoxDTraceR0A.asm 115014 2026-08-12 23:38:48Z knut.osmundsen@oracle.com $
;; @file
; VBoxDTraceR0 - Assembly Hacks.
;
; Contributed by: bird
;

;
; Copyright (C) 2012-2026 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from http://www.virtualbox.org.
;
; The contents of this file are subject to the terms of the Common
; Development and Distribution License Version 1.0 (CDDL) only, as it
; comes in the "COPYING.CDDL" file of the VirtualBox distribution.
;
; SPDX-License-Identifier: CDDL-1.0
;



%include "iprt/asmdefs.mac"

BEGINCODE


extern NAME(dtrace_probe)

GLOBALNAME dtrace_probe6
    jmp     NAME(dtrace_probe)

MARK_OBJECT_RETPOLINE_SAFE
