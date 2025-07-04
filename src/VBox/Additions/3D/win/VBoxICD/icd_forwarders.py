#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: icd_forwarders.py 106683 2024-10-24 21:31:30Z knut.osmundsen@oracle.com $

"""
Generates forwards from a .def file.
"""

__copyright__ = \
"""
Copyright (C) 2018-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision: 106683 $"

# Standard python imports"""
import sys


def GenerateForwarders(asArgs):

    if len(asArgs) != 4:
        raise Exception('Expected 3 arguments, not %u!' % (len(asArgs) - 1,));

    # Get list of functions.
    asNames      = []
    asParamSizes = []
    with open(asArgs[1], "r") as oInFile:
        for line in oInFile.readlines():
            line = line.strip()
            if len(line) > 0 and line[0] != ';' and line != 'EXPORTS':
                # Parse 'glAccum = glAccum@8'
                words = line.split('=', 1)

                # Function name
                asNames.append(words[0].strip())

                # Size of arguments in bytes
                words = words[1].split('@')
                asParamSizes.append(words[1].strip())


    #
    # Assembler forwarders
    #
    if asArgs[3] in ('x86', 'amd64',):
        asLines = [
            '; AUTOGENERATED - DO NOT EDIT!',
            '%include "iprt/asmdefs.mac"',
            '',
            ';;;; Enable ICD_LAZY_LOAD to lazy load the ICD DLL - bird 2024-10-24: it should work again now',
            '; %define ICD_LAZY_LOAD 1',
            '',
            '%ifdef ICD_LAZY_LOAD',
            'extern NAME(g_hmodICD)',
            'extern NAME(VBoxLoadICD)',
            '',
            'BEGINPROC VBoxLoadICDWrapper',
            '    ; Check if loaded',
            '    mov     xAX, [NAME(g_hmodICD) xWrtRIP]',
            '    test    xAX, xAX',
            '    jz      .needs_loading',
            '    ret',
            '    ; Save parameter registers',
            '.needs_loading:',
            ' %ifdef RT_ARCH_AMD64',
            '    push    rcx',
            '    push    rdx',
            '    push    r8',
            '    push    r9',
            '  %ifdef ASM_CALL64_GCC',
            '    push    rsi',
            '    push    rdi',
            '    sub     rsp, 8h',
            '  %else',
            '    sub     rsp, 28h',
            '  %endif',
            ' %endif',
            '    ; Make the call',
            '    call  NAME(VBoxLoadICD)',
            '    ; Restore parameter registers',
            ' %ifdef RT_ARCH_AMD64',
            '  %ifdef ASM_CALL64_GCC',
            '    add     rsp, 8h',
            '    pop     rdi',
            '    pop     rsi',
            '  %else',
            '    add     rsp, 28h',
            '  %endif',
            '    pop     r9',
            '    pop     r8',
            '    pop     rdx',
            '    pop     rcx',
            ' %endif',
            '    ret',
            'ENDPROC   VBoxLoadICDWrapper',
            '%endif ; ICD_LAZY_LOAD',
        ];

        for iFn, sFnNm in enumerate(asNames):
            cbRet = asParamSizes[iFn]
            asLines.extend([
                '',
                'BEGINPROC_EXPORTED %s' % sFnNm,
                '    extern NAME(g_pfn_%s)' % sFnNm,
                ';    int3',
                '%ifdef ICD_LAZY_LOAD',
                '    call    VBoxLoadICDWrapper',
                '%endif',
                '    mov     xAX, [NAME(g_pfn_%s) xWrtRIP]' % sFnNm,
                '    test    xAX, xAX',
                '    jz      .return',
                '    jmp     xAX',
                '.return:',
                '%ifdef RT_ARCH_AMD64',
                '    ret',
                '%else ; X86',
                '    ret     %s' % cbRet,
                '%endif',
                'ENDPROC %s' % sFnNm,
            ]);

    elif asArgs[3] == 'arm64':
        asLines = [
            '/* AUTOGENERATED - DO NOT EDIT!*/',
            '#include "iprt/asmdefs-arm.h"',
            '',
            '/** Enable ICD_LAZY_LOAD to lazy load the ICD DLL */',
            '/* #define ICD_LAZY_LOAD 1*/',
            '',
            '#ifdef ICD_LAZY_LOAD',
            '    .p2align 4',
            'BEGINPROC VBoxLoadICDWrapper',
            '    ; Check if loaded',
            '    adrp    x8, PAGE(NAME(g_hmodICD))',
            '    ldr     x8, [x8, PAGEOFF(NAME(g_hmodICD))]',
            '    cbz     x8, Lneeds_loading',
            '    ret',
            'Lneeds_loading:',
            '    /* Save parameter registers */',
            '    stp     x0, x1,   [sp, #-0x50]!',
            '    stp     x2, x3,   [sp, #0x10]',
            '    stp     x4, x5,   [sp, #0x20]',
            '    stp     x6, x7,   [sp, #0x30]',
            '    stp     x29, x30, [sp, #0x40]',
            '    /* Make the call */',
            '    bl      NAME(VBoxLoadICD)',
            '    /* Restore parameter registers */',
            '    ldp     x0, x1,   [sp, #0x00]',
            '    ldp     x2, x3,   [sp, #x10]',
            '    ldp     x4, x5,   [sp, #x20]',
            '    ldp     x6, x7,   [sp, #x30]',
            '    ldp     x29, x30, [sp, #x40]',
            '    add     sp, sp #0x50',
            '    ret',
            'ENDPROC   VBoxLoadICDWrapper',
            '#endif /* ICD_LAZY_LOAD */',
        ];

        for sFnNm in asNames:
            asLines.extend([
                '',
                '     .p2align 3',
                'BEGINPROC_EXPORTED %s' % (sFnNm,),
                ';    brk  #0x8888',
                '#ifdef ICD_LAZY_LOAD',
                '    bl     VBoxLoadICDWrapper',
                '#endif',
                '    adrp    x8, PAGE(NAME(g_pfn_%s))' % (sFnNm,),
                '    ldr     x8, [x8, PAGEOFF(NAME(g_pfn_%s))]' % (sFnNm,),
                '    cbz     x8, Lreturn_%s' % (sFnNm,),
                '    br      x8',
                'Lreturn_%s:' % (sFnNm,),
                '    ret',
                'ENDPROC %s' % (sFnNm,),
            ]);

    else:
        raise Exception('Unsupported target architecture: %s' % (asArgs[3],));

    #
    # Write it out.
    #
    with open(asArgs[2], "w") as oOutFile:
        oOutFile.write('\n'.join(asLines));

    return 0;

if __name__ == '__main__':
    sys.exit(GenerateForwarders(sys.argv));

