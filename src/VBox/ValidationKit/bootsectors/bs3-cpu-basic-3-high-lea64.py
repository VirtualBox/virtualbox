#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: bs3-cpu-basic-3-high-lea64.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=invalid-name

"""
Generates the assembly body of the 64-bit lea testcase.

Nasm consumes too much memory (>19GB) doing this via the preprocessor, it also
takes its good time about it.  This script takes 2-3 seconds and nasm only
consumes a few hundred MB of memory processing it (takes some 40 seconds, though).
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2023-2024 Oracle and/or its affiliates.

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

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"

# Standard python imports.
import random;
import sys;

g_kasRegNames = [
    'rax',
    'rcx',
    'rdx',
    'rbx',
    'rsp',
    'rbp',
    'rsi',
    'rdi',
    'r8',
    'r9',
    'r10',
    'r11',
    'r12',
    'r13',
    'r14',
    'r15',
];

g_kaiRegValues = [
    0x1111111111111110,
    0x2222222222222202,
    0x3333333333333033,
    0x4444444444440444,
    0x58595a5d51525356,
    0x5555555555555551,
    0x6666666666666616,
    0x7777777777777177,
    0x8888888888881888,
    0x9999999999999992,
    0xaaaaaaaaaaaaaa2a,
    0xbbbbbbbbbbbbb2bb,
    0xcccccccccccc2ccc,
    0xddddddddddddddd3,
    0xeeeeeeeeeeeeee3e,
    0xfffffffffffff3ff,
];

def x86ModRmMake(iMod, iReg, iRm):
    """ See X86_MODRM_MAKE. """
    return (iMod << 6) | ((iReg & 7) << 3) | (iRm & 7);

def x86SibMake(iBase, iIndex, cShift):
    """ X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift) """
    return (cShift << 6) | ((iIndex & 7) << 3) | (iBase & 7);

def x86RexW3(iBase, iIndex, iDstReg):
    # db      X86_OP_REX_W | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
    return 0x48 | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1);

def x86Rex3(iBase, iIndex, iDstReg):
    # db      X86_OP_REX | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
    return 0x40 | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1);

def generateTraceAndLoad(fLoadRsp, fTracing):
    """ Loads registers and traces current position if requested. """
    if fTracing:
        asRet = ['mov     dword [rel BS3_DATA_NM(g_bs3CpuBasic3_lea_trace)], $',];
    else:
        asRet = [];
    asRet.append('call    .load_regs');
    if fLoadRsp:
        asRet.append('mov     rsp, %#x' % (g_kaiRegValues[4], ));
    return asRet;

def cenerateCompareAndCheckSib(sDstRegName, iValue, fRestoreRsp):
    """ Checks that sDstRegName contains iValue, executing int3 if it doesn't and restoring RSP first if requested. """
    if  -0x80000000 <= iValue <= 0x7fffffff:
        asRet = [
            'cmp     %s, %#x' % (sDstRegName, iValue),
        ];
    elif sDstRegName != 'rax':
        asRet = [
            'mov     rax, %#x' % (iValue,),
            'cmp     %s, rax' % (sDstRegName,),
        ];
    else:
        asRet = [
            'mov     rcx, %#x' % (iValue),
            'cmp     rax, rcx',
        ];
    if fRestoreRsp:
        asRet += [
            'mov     rdx, rsp',
            'mov     rsp, [rel BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp)]',
        ];
    asRet += [
        'jz      $+3',
        'int3'
    ];
    return asRet;

def generateCompareAndCheckModRm(sDstRegName, iValue, sValue, fRestoreRsp):
    """
    Checks that sDstRegName contains iValue or sValue if not None,
    executing int3 if it doesn't and restoring RSP first if requested.
    """
    if sValue:
        asRet = [
            'cmp     %s, %s' % (sDstRegName, sValue),
        ];
    elif -0x80000000 <= iValue <= 0x7fffffff:
        asRet = [
            'cmp     %s, %#x' % (sDstRegName, iValue),
        ];
    elif sDstRegName != 'rax':
        asRet = [
            'mov     rax, %#x' % (iValue,),
            'cmp     %s, rax' % (sDstRegName,),
        ];
    else:
        asRet = [
            'mov     rcx, %#x' % (iValue),
            'cmp     rax, rcx',
        ];
    if fRestoreRsp:
        asRet += [
            'mov     rdx, rsp',
            'mov     rsp, [rel BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp)]',
        ];
    asRet += [
        'jz      $+3',
        'int3'
    ];
    return asRet;

def generateDispSib(iValue, iMod, iBase):
    """ Generates the displacement part of the LEA instruction for the SIB variant. """
    if iMod == 1: #X86_MOD_MEM1:
        iDisp = random.randint(-128, 127);
        return iValue + iDisp, ['db      %d' % (iDisp,),];
    if iMod == 2 or (iMod == 0 and (iBase & 7) == 5):
        iDisp = random.randint(-0x80000000, 0x7fffffff);
        return iValue + iDisp, ['dd      %d' % (iDisp,),];
    return iValue, [];

def generateDispModRm(iValue, iMod, iMemReg, iLabelVar, f16Bit = False):
    """ Generates the displacement part of the LEA instruction for the non-SIB variant. """
    if iMod == 1: #X86_MOD_MEM1
        iDisp = random.randint(-128, 127);
        return iValue + iDisp, None, ['db      %d' % (iDisp,),];
    if iMod == 0 and (iMemReg & 7) == 5:
        if f16Bit:
            # Generate a known 16-bit value by trickery as we the iDstReg value is mixed.
            # ASSUMES Bs3Text16 is 64KB aligned in the high DLL...
            iValue = random.randint(0, 0x7fff);
            return iValue, None, ['dd      _Bs3Text16_StartOfSegment - $ - 4 + %d wrt BS3FLAT' % (iValue,),];
        if iLabelVar & 1:
            return iValue, '.test_label wrt BS3FLAT', ['dd      .test_label - $ - 4',];
        return     iValue, '.load_regs  wrt BS3FLAT', ['dd      .load_regs  - $ - 4',];
    if iMod == 2: #X86_MOD_MEM4
        iDisp = random.randint(-0x80000000, 0x7fffffff);
        return iValue + iDisp, None, ['dd      %d' % (iDisp,),];
    return iValue, None, [];

def generateLea64(oOut): # pylint: disable=too-many-statements
    fTracing = False;

    #
    # Loop thru all the modr/m memory encodings.
    #
    asLines = [];
    for iMod in range(3):
        for iDstReg in range(16):
            sDstReg_Name  = g_kasRegNames[iDstReg];
            iDstReg_Value = g_kaiRegValues[iDstReg];
            for iMemReg in range(16):
                if (iMemReg & 7) == 4:
                    #
                    # SIB.
                    #
                    for iBase in range(16):
                        if (iBase & 7) == 5 and iMod == 0:
                            iBase_Value = 0
                        else:
                            iBase_Value = g_kaiRegValues[iBase];

                        for iIndex in range(16):
                            if iIndex == 4:
                                iIndex_Value = 0;
                            else:
                                iIndex_Value = g_kaiRegValues[iIndex];

                            for cShift in range(4):
                                fLoadRestoreRsp = iBase == 4 or iDstReg == 4;

                                #
                                # LEA+SIB w/ 64-bit operand size and 64-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea
                                iValue = iBase_Value + (iIndex_Value << cShift)
                                asLines.append('db      %#x, 8dh, %#x, %#x'
                                                % (x86RexW3(iBase, iIndex, iDstReg), x86ModRmMake(iMod, iDstReg, iMemReg),
                                                   x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue &= 0xffffffffffffffff;

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));


                                #
                                # LEA+SIB w/ 64-bit operand size and 32-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea w/ X86_OP_PRF_SIZE_ADDR
                                iValue = iBase_Value + (iIndex_Value << cShift)
                                asLines.append('db      067h, %#x, 8dh, %#x, %#x'
                                               % (x86RexW3(iBase, iIndex, iDstReg), x86ModRmMake(iMod, iDstReg, iMemReg),
                                                  x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue &= 0xffffffff;

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));


                                #
                                # LEA+SIB w/ 32-bit operand size and 64-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea
                                iValue = iBase_Value + (iIndex_Value << cShift);
                                if (iBase | iIndex | iDstReg) & 8:
                                    asLines.append('db      %#x' % (x86Rex3(iBase, iIndex, iDstReg),));
                                asLines.append('db      8dh, %#x, %#x'
                                               % (x86ModRmMake(iMod, iDstReg, iMemReg), x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue &= 0xffffffff;

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));


                                #
                                # LEA+SIB w/ 32-bit operand size and 32-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea
                                iValue = iBase_Value + (iIndex_Value << cShift);
                                asLines.append('db      067h');  # X86_OP_PRF_SIZE_ADDR
                                if (iBase | iIndex | iDstReg) & 8:
                                    asLines.append('db      %#x' % (x86Rex3(iBase, iIndex, iDstReg),));
                                asLines.append('db      8dh, %#x, %#x'
                                               % (x86ModRmMake(iMod, iDstReg, iMemReg), x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue &= 0xffffffff;

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));


                                #
                                # LEA+SIB w/ 16-bit operand size and 64-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea
                                iValue = iBase_Value + (iIndex_Value << cShift);
                                asLines.append('db      66h'); # X86_OP_PRF_SIZE_OP
                                if (iBase | iIndex | iDstReg) & 8:
                                    asLines.append('db      %#x' % (x86Rex3(iBase, iIndex, iDstReg),));
                                asLines.append('db      8dh, %#x, %#x'
                                               % (x86ModRmMake(iMod, iDstReg, iMemReg), x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue = (iValue & 0xffff) | (iDstReg_Value & 0xffffffffffff0000);

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));

                                #
                                # LEA+SIB w/ 16-bit operand size and 32-bit address size.
                                #
                                asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                                # lea
                                iValue = iBase_Value + (iIndex_Value << cShift);
                                if cShift & 2:
                                    asLines.append('db      066h, 067h'); # X86_OP_PRF_SIZE_OP, X86_OP_PRF_SIZE_ADDR
                                else:
                                    asLines.append('db      067h, 066h'); # X86_OP_PRF_SIZE_ADDR, X86_OP_PRF_SIZE_OP
                                if (iBase | iIndex | iDstReg) & 8:
                                    asLines.append('db      %#x' % (x86Rex3(iBase, iIndex, iDstReg),));
                                asLines.append('db      8dh, %#x, %#x'
                                               % (x86ModRmMake(iMod, iDstReg, iMemReg), x86SibMake(iBase, iIndex, cShift),));
                                iValue, asAdd = generateDispSib(iValue, iMod, iBase);
                                asLines.extend(asAdd);
                                iValue = (iValue & 0xffff) | (iDstReg_Value & 0xffffffffffff0000);

                                # cmp iDstReg, iValue + jz + int3
                                asLines.extend(cenerateCompareAndCheckSib(sDstReg_Name, iValue, fLoadRestoreRsp));

                else: # !SIB
                    #
                    # Plain lea reg, [reg] with disp according to iMod,
                    # or lea reg, [disp32] if iMemReg == 5 && iMod == 0.
                    #
                    if (iMemReg & 7) == 5 and iMod == 0:
                        iMemReg_Value = 0;
                    else:
                        iMemReg_Value = g_kaiRegValues[iMemReg];

                    fLoadRestoreRsp = iDstReg == 4;

                    #
                    # 64-bit operand and address size first.
                    #
                    asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                    # lea
                    asLines.append('db      %#x, 8dh, %#x'
                                    % (x86RexW3(iMemReg, 0, iDstReg), x86ModRmMake(iMod, iDstReg, iMemReg),));
                    iValue, sValue, asAdd = generateDispModRm(iMemReg_Value, iMod, iMemReg, 0);
                    asLines.extend(asAdd);
                    iValue &= 0xffffffffffffffff;

                    # cmp iDstReg, iValue + jz + int3
                    asLines.extend(generateCompareAndCheckModRm(sDstReg_Name, iValue, sValue, fLoadRestoreRsp));


                    #
                    # 64-bit operand and 32-bit address size.
                    #
                    asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                    # lea w/ X86_OP_PRF_SIZE_ADDR
                    asLines.append('db      067h, %#x, 8dh, %#x'
                                    % (x86RexW3(iMemReg, 0, iDstReg), x86ModRmMake(iMod, iDstReg, iMemReg),));
                    iValue, sValue, asAdd = generateDispModRm(iMemReg_Value, iMod, iMemReg, 1);
                    asLines.extend(asAdd);
                    iValue &= 0x00000000ffffffff;

                    # cmp iDstReg, iValue + jz + int3
                    asLines.extend(generateCompareAndCheckModRm(sDstReg_Name, iValue, sValue, fLoadRestoreRsp));


                    #
                    # 32-bit operand and 64-bit address size.
                    #
                    asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                    # lea iValue = iMemReg_Value;
                    iValue = iMemReg_Value;
                    if iDstReg >= 8 or iMemReg >= 8:
                        asLines.append('db      %#x' % (x86Rex3(iMemReg, 0, iDstReg),));
                    asLines.append('db      8dh, %#x' % (x86ModRmMake(iMod, iDstReg, iMemReg),));
                    iValue, sValue, asAdd = generateDispModRm(iMemReg_Value, iMod, iMemReg, 2);
                    asLines.extend(asAdd);
                    iValue &= 0x00000000ffffffff;

                    # cmp iDstReg, iValue + jz + int3
                    asLines.extend(generateCompareAndCheckModRm(sDstReg_Name, iValue, sValue, fLoadRestoreRsp));


                    #
                    # 16-bit operand and 64-bit address size.
                    #
                    asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                    # lea
                    asLines.append('db      066h'); # X86_OP_PRF_SIZE_OP
                    if iDstReg >= 8 or iMemReg >= 8:
                        asLines.append('db      %#x' % (x86Rex3(iMemReg, 0, iDstReg),));
                    asLines.append('db      8dh, %#x' % (x86ModRmMake(iMod, iDstReg, iMemReg),));
                    iValue, sValue, asAdd = generateDispModRm(iMemReg_Value, iMod, iMemReg, 0, True);
                    asLines.extend(asAdd);
                    iValue = (iValue & 0xffff) | (iDstReg_Value & 0xffffffffffff0000);

                    # cmp iDstReg, iValue
                    asLines.extend(generateCompareAndCheckModRm(sDstReg_Name, iValue, sValue, fLoadRestoreRsp));

                    #
                    # 16-bit operand and 32-bit address size.
                    #
                    asLines.extend(generateTraceAndLoad(fLoadRestoreRsp, fTracing));

                    # lea
                    if random.randint(0, 1):
                        asLines.append('db      066h,067h'); # X86_OP_PRF_SIZE_OP, X86_OP_PRF_SIZE_ADDR
                    else:
                        asLines.append('db      067h,066h'); # X86_OP_PRF_SIZE_ADDR, X86_OP_PRF_SIZE_OP
                    if iDstReg >= 8 or iMemReg >= 8:
                        asLines.append('db      %#x' % (x86Rex3(iMemReg, 0, iDstReg),));
                    asLines.append('db      8dh, %#x' % (x86ModRmMake(iMod, iDstReg, iMemReg),));
                    iValue, sValue, asAdd = generateDispModRm(iMemReg_Value, iMod, iMemReg, 0, True);
                    asLines.extend(asAdd);
                    iValue = (iValue & 0xffff) | (iDstReg_Value & 0xffffffffffff0000);

                    # cmp iDstReg, iValue
                    asLines.extend(generateCompareAndCheckModRm(sDstReg_Name, iValue, sValue, fLoadRestoreRsp));

    oOut.write('\n'.join(asLines));
    return 0;

def main(asArgs):
    if len(asArgs) != 2 or asArgs[1][0] == '-':
        print('bs3-cpu-basic-3-high-lea64.py: syntax error!\n', file = sys.stderr);
        return 2;
    with open(asArgs[1], 'w', encoding = 'utf-8') as oOut:
        generateLea64(oOut);
    return 0;

if __name__ == '__main__':
    sys.exit(main(sys.argv));

