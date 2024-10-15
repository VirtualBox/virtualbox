#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: bs3-cpu-generated-1-data.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=invalid-name

"""
Generates testcases from @optest specifications in IEM.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2017-2024 Oracle and/or its affiliates.

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
import datetime;
import os;
import sys;

# Only the main script needs to modify the path.
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));
g_ksVmmAllDir = os.path.join(os.path.dirname(g_ksValidationKitDir), 'VMM', 'VMMAll')
sys.path.append(g_ksVmmAllDir);

import IEMAllInstPython as iai; # pylint: disable=import-error


# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class Bs3Cg1TestEncoder(object):
    """
    Does the encoding of a single test.
    """

    def __init__(self, fLast):
        self.fLast      = fLast;
        # Each list member (in all lists) are C expression of a byte.
        self.asHdr       = [];
        self.asSelectors = [];
        self.asInputs    = [];
        self.asOutputs   = [];

    @staticmethod
    def _compileSelectors(aoSelectors): # (list(iai.TestSelector)) -> list(str)
        """
        Compiles a list of iai.TestSelector predicate checks.
        Returns C byte expression strings.
        """
        asRet = [];
        for oSelector in aoSelectors:
            sConstant = oSelector.kdVariables[oSelector.sVariable][oSelector.sValue];
            sConstant = sConstant.upper().replace('.', '_');
            if oSelector.sOp == '==':
                sByte = '(BS3CG1PRED_%s << BS3CG1SEL_OP_PRED_SHIFT) | BS3CG1SEL_OP_IS_TRUE' % (sConstant,);
            elif oSelector.sOp == '!=':
                sByte = '(BS3CG1PRED_%s << BS3CG1SEL_OP_PRED_SHIFT) | BS3CG1SEL_OP_IS_FALSE' % (sConstant,);
            else:
                raise Exception('Unknown selector operator: %s' % (oSelector.sOp,));
            asRet.append(sByte);
        return asRet;

    kdSmallFields = {
        'op1':  'BS3CG1_CTXOP_OP1',
        'op2':  'BS3CG1_CTXOP_OP2',
        'efl':  'BS3CG1_CTXOP_EFL',
    };
    kdOperators = {
        '=':    'BS3CG1_CTXOP_ASSIGN',
        '|=':   'BS3CG1_CTXOP_OR',
        '&=':   'BS3CG1_CTXOP_AND',
        '&~=':  'BS3CG1_CTXOP_AND_INV',
    };
    kdSmallSizes = {
        1:      'BS3CG1_CTXOP_1_BYTE',
        2:      'BS3CG1_CTXOP_2_BYTES',
        4:      'BS3CG1_CTXOP_4_BYTES',
        8:      'BS3CG1_CTXOP_8_BYTES',
        16:     'BS3CG1_CTXOP_16_BYTES',
        32:     'BS3CG1_CTXOP_32_BYTES',
        12:     'BS3CG1_CTXOP_12_BYTES',
    };

    @staticmethod
    def _amendOutputs(aoOutputs, oInstr): # type: (list(iai.TestInOut), iai.Instruction) -> list(iai.TestInOut)
        """
        Amends aoOutputs for instructions with special flag behaviour (undefined,
        always set, always clear).

        Undefined flags are copied from the result context as the very first
        operation so they can be set to CPU vendor specific values later if
        desired.

        Always set or cleared flags are applied at the very end of the
        modification operations so that we spot incorrect specifications.
        """
        if oInstr.asFlUndefined or oInstr.asFlClear or oInstr.asFlSet:
            aoOutputs = list(aoOutputs);

            if oInstr.asFlUndefined:
                fFlags = oInstr.getUndefinedFlagsMask();
                assert fFlags != 0;
                aoOutputs.insert(0, iai.TestInOut('efl_undef', '=', str(fFlags), 'uint'));

            if oInstr.asFlClear:
                fFlags = oInstr.getClearedFlagsMask();
                assert fFlags != 0;
                aoOutputs.append(iai.TestInOut('efl', '&~=', str(fFlags), 'uint'));

            if oInstr.asFlSet:
                fFlags = oInstr.getSetFlagsMask();
                assert fFlags != 0;
                aoOutputs.append(iai.TestInOut('efl', '|=', str(fFlags), 'uint'));

        return aoOutputs;

    @staticmethod
    def _compileContextModifers(aoOperations): # (list(iai.TestInOut))
        """
        Compile a list of iai.TestInOut context modifiers.
        """
        asRet = [];
        for oOperation in aoOperations:
            oType = iai.TestInOut.kdTypes[oOperation.sType];
            aaoValues = oType.get(oOperation.sValue);
            assert len(aaoValues) == 1 or len(aaoValues) == 2;

            sOp = oOperation.sOp;
            if sOp == '&|=':
                sOp = '|=' if len(aaoValues) == 1 else '&~=';

            for fSignExtend, abValue in aaoValues:
                cbValue = len(abValue);

                # The opcode byte.
                sOpcode = Bs3Cg1TestEncoder.kdOperators[sOp];
                sOpcode += ' | ';
                if oOperation.sField in Bs3Cg1TestEncoder.kdSmallFields:
                    sOpcode += Bs3Cg1TestEncoder.kdSmallFields[oOperation.sField];
                else:
                    sOpcode += 'BS3CG1_CTXOP_DST_ESC';
                sOpcode += ' | ';
                if cbValue in Bs3Cg1TestEncoder.kdSmallSizes:
                    sOpcode += Bs3Cg1TestEncoder.kdSmallSizes[cbValue];
                else:
                    sOpcode += 'BS3CG1_CTXOP_SIZE_ESC';
                if fSignExtend:
                    sOpcode += ' | BS3CG1_CTXOP_SIGN_EXT';
                asRet.append(sOpcode);

                # Escaped field identifier.
                if oOperation.sField not in Bs3Cg1TestEncoder.kdSmallFields:
                    asRet.append('BS3CG1DST_%s' % (oOperation.sField.upper().replace('.', '_'),));

                # Escaped size byte?
                if cbValue not in Bs3Cg1TestEncoder.kdSmallSizes:
                    if cbValue >= 256 or cbValue not in [ 1, 2, 4, 6, 8, 12, 16, 32, 64, 128, ]:
                        raise Exception('Invalid value size: %s' % (cbValue,));
                    asRet.append('0x%02x' % (cbValue,));

                # The value bytes.
                for b in abValue:
                    asRet.append('0x%02x' % (b,));

                sOp = '|=';

        return asRet;

    def _constructHeader(self):
        """
        Returns C byte expression strings for BS3CG1TESTHDR.
        """
        cbSelectors = len(self.asSelectors);
        if cbSelectors >=  256:
            raise Exception('Too many selectors: %s bytes, max 255 bytes' % (cbSelectors,))

        cbInputs = len(self.asInputs);
        if cbInputs >= 4096:
            raise Exception('Too many input context modifiers: %s bytes, max 4095 bytes' % (cbInputs,))

        cbOutputs = len(self.asOutputs);
        if cbOutputs >= 2048:
            raise Exception('Too many output context modifiers: %s bytes, max 2047 bytes' % (cbOutputs,))

        return [
            '%#04x' % (cbSelectors,),                                     # 8-bit
            '%#05x & 0xff' % (cbInputs,),                                 # first 8 bits of cbInputs
            '(%#05x >> 8) | ((%#05x & 0xf) << 4)' % (cbInputs, cbOutputs,),  # last 4 bits of cbInputs, lower 4 bits of cbOutputs.
            '(%#05x >> 4) | (%#05x << 7)' % (cbOutputs, self.fLast),         # last 7 bits of cbOutputs and 1 bit fLast.
        ];

    def encodeTest(self, oTest): # type: (iai.InstructionTest)
        """
        Does the encoding.
        """
        self.asSelectors = self._compileSelectors(oTest.aoSelectors);
        self.asInputs    = self._compileContextModifers(oTest.aoInputs);
        self.asOutputs   = self._compileContextModifers(self._amendOutputs(oTest.aoOutputs, oTest.oInstr));
        self.asHdr       = self._constructHeader();


class Bs3Cg1EncodedTests(object):
    """
    Encodes the tests for an instruction.
    """

    def __init__(self, oInstr):
        self.offTests       = -1;
        self.cbTests        = 0;
        self.asLines        = []        # type: list(str)
        self.aoInstructions = []        # type: list(iai.Instruction)

        # Encode the tests.
        for iTest, oTest in enumerate(oInstr.aoTests):
            oEncodedTest = Bs3Cg1TestEncoder(iTest + 1 == len(oInstr.aoTests));
            oEncodedTest.encodeTest(oTest);

            self.cbTests += len(oEncodedTest.asHdr) + len(oEncodedTest.asSelectors) \
                          + len(oEncodedTest.asInputs) + len(oEncodedTest.asOutputs);

            self.asLines.append('    /* test #%s: %s */' % (iTest, oTest,));
            self.asLines += self.bytesToLines('             ', oEncodedTest.asHdr);
            if oEncodedTest.asSelectors:
                self.asLines += self.bytesToLines('    /*sel:*/ ', oEncodedTest.asSelectors);
            if oEncodedTest.asInputs:
                self.asLines += self.bytesToLines('    /* in:*/ ', oEncodedTest.asInputs);
            if oEncodedTest.asOutputs:
                self.asLines += self.bytesToLines('    /*out:*/ ', oEncodedTest.asOutputs);

    @staticmethod
    def bytesToLines(sPrefix, asBytes):
        """
        Formats a series of bytes into one or more lines.
        A byte ending with a newline indicates that we should start a new line,
        and prefix it by len(sPrefix) spaces.

        Returns list of lines.
        """
        asRet = [];
        sLine = sPrefix;
        for sByte in asBytes:
            if sByte[-1] == '\n':
                sLine += sByte[:-1] + ',';
                asRet.append(sLine);
                sLine = ' ' * len(sPrefix);
            else:
                if len(sLine) + 2 + len(sByte) > 132 and len(sLine) > len(sPrefix):
                    asRet.append(sLine[:-1]);
                    sLine = ' ' * len(sPrefix);
                sLine += sByte + ', ';


        if len(sLine) > len(sPrefix):
            asRet.append(sLine);
        return asRet;


    def isEqual(self, oOther):
        """ Compares two encoded tests. """
        if self.cbTests != oOther.cbTests:
            return False;
        if len(self.asLines) != len(oOther.asLines):
            return False;
        for iLine, sLines in enumerate(self.asLines):
            if sLines != oOther.asLines[iLine]:
                return False;
        return True;



class Bs3Cg1Instruction(object):
    """
    An instruction with tests.
    """

    def __init__(self, oMap, oInstr, oTests):
        self.oMap   = oMap              # type: iai.InstructionMap
        self.oInstr = oInstr            # type: iai.Instruction
        self.oTests = oTests            # type: Bs3Cg1EncodedTests

        self.asOpcodes          = oMap.asLeadOpcodes + [ '0x%02x' % (oInstr.getOpcodeByte(),) ];
        self.sEncoding          = iai.g_kdEncodings[oInstr.sEncoding][0];

        for oOp in oInstr.aoOperands:
            self.sEncoding     += '_' + oOp.sType;
        if oInstr.sSubOpcode and iai.g_kdSubOpcodes[oInstr.sSubOpcode][1]:
            self.sEncoding     += '_' + iai.g_kdSubOpcodes[oInstr.sSubOpcode][1];

        if oInstr.fUnused:
            if oInstr.sInvalidStyle == 'immediate' and oInstr.sSubOpcode:
                self.sEncoding += '_MOD_EQ_3' if oInstr.sSubOpcode == '11 mr/reg' else '_MOD_NE_3';
            elif oInstr.sInvalidStyle == 'intel-modrm':
                if oInstr.sSubOpcode is None:
                    self.sEncoding = 'BS3CG1ENC_MODRM_Gv_Ev';
                elif oInstr.sSubOpcode == '11 mr/reg':
                    self.sEncoding = 'BS3CG1ENC_MODRM_MOD_EQ_3';
                elif oInstr.sSubOpcode == '!11 mr/reg':
                    self.sEncoding = 'BS3CG1ENC_MODRM_MOD_NE_3';
                else:
                    raise Exception('Unhandled sSubOpcode=%s for sInvalidStyle=%s' % (oInstr.sSubOpcode, oInstr.sInvalidStyle));
            elif oInstr.sInvalidStyle == 'vex.modrm':
                self.sEncoding = 'BS3CG1ENC_VEX_MODRM';

        self.asFlags            = [];
        if 'x86_invalid_64' in oInstr.dHints:
            self.asFlags.append('BS3CG1INSTR_F_INVALID_64BIT');
        if oInstr.fUnused:
            self.asFlags.append('BS3CG1INSTR_F_UNUSED');
        elif oInstr.fInvalid:
            self.asFlags.append('BS3CG1INSTR_F_INVALID');
        if oInstr.sInvalidStyle and oInstr.sInvalidStyle.startswith('intel-'):
            self.asFlags.append('BS3CG1INSTR_F_INTEL_DECODES_INVALID');
        if 'vex_l_zero' in oInstr.dHints:
            self.asFlags.append('BS3CG1INSTR_F_VEX_L_ZERO');
        if 'vex_l_ignored' in oInstr.dHints:
            self.asFlags.append('BS3CG1INSTR_F_VEX_L_IGNORED');

        self.fAdvanceMnemonic   = True; ##< Set by the caller.
        if oInstr.sPrefix:
            if oInstr.sPrefix == 'none':
                self.sPfxKind = 'BS3CG1PFXKIND_NO_F2_F3_66';
            else:
                self.sPfxKind = 'BS3CG1PFXKIND_REQ_' + oInstr.sPrefix[-2:].upper();
        elif oInstr.sEncoding == 'ModR/M':
            if 'ignores_op_size' not in oInstr.dHints:
                self.sPfxKind   = 'BS3CG1PFXKIND_MODRM';
            else:
                self.sPfxKind   = 'BS3CG1PFXKIND_MODRM_NO_OP_SIZES';
        else:
            self.sPfxKind       = '0';

        self.sCpu = 'BS3CG1CPU_';
        assert len(oInstr.asCpuIds) in [0, 1], str(oInstr);
        if oInstr.asCpuIds:
            self.sCpu += oInstr.asCpuIds[0].upper().replace('.', '_');
        elif oInstr.sMinCpu:
            self.sCpu += 'GE_' + oInstr.sMinCpu;
        else:
            self.sCpu += 'ANY';

        if oInstr.sXcptType:
            self.sXcptType = 'BS3CG1XCPTTYPE_' + oInstr.sXcptType.upper();
        else:
            self.sXcptType = 'BS3CG1XCPTTYPE_NONE';

    def getOperands(self):
        """ Returns comma separated string of operand values for g_abBs3Cg1Operands. """
        return ', '.join(['(uint8_t)BS3CG1OP_%s' % (oOp.sType,) for oOp in self.oInstr.aoOperands]);

    def getOpcodeMap(self):
        """ Returns the opcode map number for the BS3CG1INSTR structure. """
        sEncoding = self.oInstr.aoMaps[0].sEncoding;
        if sEncoding == 'legacy':   return 0;
        if sEncoding == 'vex1':     return 1;
        if sEncoding == 'vex2':     return 2;
        if sEncoding == 'vex3':     return 3;
        if sEncoding == 'xop8':     return 8;
        if sEncoding == 'xop9':     return 9;
        if sEncoding == 'xop10':    return 10;
        assert False, sEncoding;
        return 3;

    def getInstructionEntry(self):
        """ Returns an array of BS3CG1INSTR member initializers. """
        assert len(self.oInstr.sMnemonic) < 16;
        sOperands = ', '.join([oOp.sType for oOp in self.oInstr.aoOperands]);
        if sOperands:
            sOperands = ' /* ' + sOperands + ' */';
        return [
            '        /* cbOpcodes = */        %s, /* %s */' % (len(self.asOpcodes), ' '.join(self.asOpcodes),),
            '        /* cOperands = */        %s,%s' % (len(self.oInstr.aoOperands), sOperands,),
            '        /* cchMnemonic = */      %s, /* %s */' % (len(self.oInstr.sMnemonic), self.oInstr.sMnemonic,),
            '        /* fAdvanceMnemonic = */ %s,' % ('true' if self.fAdvanceMnemonic else 'false',),
            '        /* offTests = */         %s,' % (self.oTests.offTests,),
            '        /* enmEncoding = */      (unsigned)%s,' % (self.sEncoding,),
            '        /* uOpcodeMap = */       (unsigned)%s,' % (self.getOpcodeMap(),),
            '        /* enmPrefixKind = */    (unsigned)%s,' % (self.sPfxKind,),
            '        /* enmCpuTest = */       (unsigned)%s,' % (self.sCpu,),
            '        /* enmXcptType = */      (unsigned)%s,' % (self.sXcptType,),
            '        /* uUnused = */          0,',
            '        /* fFlags = */           %s' % (' | '.join(self.asFlags) if self.asFlags else '0'),
        ];


class Bs3CpuGenerated1Generator(object):
    """
    The generator code for bs3-cpu-generated-1.
    """

    def __init__(self):
        self.aoInstructions = []        # type: Bs3Cg1Instruction
        self.aoTests        = []        # type: Bs3Cg1EncodedTests
        self.cbTests        = 0;

    def addTests(self, oTests, oInstr): # type: (Bs3Cg1EncodedTests, iai.Instruction) -> Bs3Cg1EncodedTests
        """
        Adds oTests to self.aoTests, setting the oTests.offTests member.
        Checks for and eliminates duplicates.
        Returns the tests to use.
        """
        # Check for duplicates.
        for oExisting in self.aoTests:
            if oTests.isEqual(oExisting):
                oExisting.aoInstructions.append(oInstr);
                return oExisting;

        # New test, so add it.
        oTests.offTests = self.cbTests;
        self.aoTests.append(oTests);
        self.cbTests   += oTests.cbTests;

        assert not oTests.aoInstructions;
        oTests.aoInstructions.append(oInstr);

        return oTests;

    def processInstruction(self):
        """
        Processes the IEM specified instructions.
        Returns success indicator.
        """

        #
        # Do the parsing.
        #
        try:
            iai.parseAll();
        except Exception as oXcpt:
            print('error: parseAll failed: %s' % (oXcpt,), file = sys.stderr);
            return False;

        #
        # Group instructions by mnemonic to reduce the number of sub-tests.
        #
        for oInstr in sorted(iai.g_aoAllInstructions,
                             key = lambda oInstr: oInstr.sMnemonic + ''.join([oOp.sType for oOp in oInstr.aoOperands])
                                                                   + (oInstr.sOpcode if oInstr.sOpcode else 'zz')):
            if oInstr.aoTests:
                oTests = Bs3Cg1EncodedTests(oInstr);
                oTests = self.addTests(oTests, oInstr);

                for oMap in oInstr.aoMaps:
                    self.aoInstructions.append(Bs3Cg1Instruction(oMap, oInstr, oTests));

        # Set fAdvanceMnemonic.
        for iInstr, oInstr in enumerate(self.aoInstructions):
            oInstr.fAdvanceMnemonic = iInstr + 1 >= len(self.aoInstructions) \
                                   or oInstr.oInstr.sMnemonic != self.aoInstructions[iInstr + 1].oInstr.sMnemonic;

        return True;

    def generateCode(self, oOut):
        """
        Generates the C code.
        Returns success indicator.
        """

        # First, a file header.
        asLines = [
            '/*',
            ' * Autogenerated by  $Id: bs3-cpu-generated-1-data.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ ',
            ' * Do not edit!',
            ' */',
            '',
            '/*',
            ' * Copyright (C) 2017-' + str(datetime.date.today().year) + ' Oracle and/or its affiliates.',
            ' *',
            ' * This file is part of VirtualBox base platform packages, as',
            ' * available from https://www.virtualbox.org.',
            ' *',
            ' * This program is free software; you can redistribute it and/or',
            ' * modify it under the terms of the GNU General Public License',
            ' * as published by the Free Software Foundation, in version 3 of the',
            ' * License.',
            ' *',
            ' * This program is distributed in the hope that it will be useful, but',
            ' * WITHOUT ANY WARRANTY; without even the implied warranty of',
            ' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU',
            ' * General Public License for more details.',
            ' *',
            ' * You should have received a copy of the GNU General Public License',
            ' * along with this program; if not, see <https://www.gnu.org/licenses>.',
            ' *',
            ' * The contents of this file may alternatively be used under the terms',
            ' * of the Common Development and Distribution License Version 1.0',
            ' * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included',
            ' * in the VirtualBox distribution, in which case the provisions of the',
            ' * CDDL are applicable instead of those of the GPL.',
            ' *',
            ' * You may elect to license modified versions of this file under the',
            ' * terms and conditions of either the GPL or the CDDL or both.',
            ' *',
            ' * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0',
            ' */',
            '',
            '',
            '#include "bs3-cpu-generated-1.h"',
            '',
            '',
            '#pragma data_seg ("BS3DATA16")',
        ];

        # Generate the g_achBs3Cg1Mnemonics array.
        asLines += [
            'const char BS3_FAR_DATA g_achBs3Cg1Mnemonics[] = ',
            '{',
        ];
        fAdvanceMnemonic = True;
        for oInstr in self.aoInstructions:
            if fAdvanceMnemonic:
                asLines.append('    \"%s\"' % (oInstr.oInstr.sMnemonic,));
            fAdvanceMnemonic = oInstr.fAdvanceMnemonic;
        asLines += [
            '};',
            '',
            '',
        ];

        # Generate the g_abBs3Cg1Opcodes array.
        asLines += [
            'const uint8_t BS3_FAR_DATA g_abBs3Cg1Opcodes[] = ',
            '{',
        ];
        for oInstr in self.aoInstructions:
            asLines.append('    ' + ', '.join(oInstr.asOpcodes) + ',');
        asLines += [
            '};',
            '',
            '',
        ];

        # Generate the g_abBs3Cg1Opcodes array.
        asLines += [
            'const uint8_t BS3_FAR_DATA g_abBs3Cg1Operands[] = ',
            '{',
        ];
        cOperands = 0;
        for oInstr in self.aoInstructions:
            if oInstr.oInstr.aoOperands:
                cOperands += len(oInstr.oInstr.aoOperands);
                asLines.append('    ' + oInstr.getOperands() + ', /* %s */' % (oInstr.oInstr.sStats,));
            else:
                asLines.append('    /* none */');
        if not cOperands:
            asLines.append('    0 /* dummy */');
        asLines += [
            '};',
            '',
            '',
        ];

        # Generate the g_abBs3Cg1Operands array.
        asLines += [
            'const BS3CG1INSTR BS3_FAR_DATA g_aBs3Cg1Instructions[] = ',
            '{',
        ];
        for oInstr in self.aoInstructions:
            asLines.append('    {');
            asLines += oInstr.getInstructionEntry();
            asLines.append('    },');
        asLines += [
            '};',
            'const  uint16_t BS3_FAR_DATA g_cBs3Cg1Instructions = RT_ELEMENTS(g_aBs3Cg1Instructions);',
            '',
            '',
        ];

        # Generate the g_abBs3Cg1Tests array.
        asLines += [
            'const uint8_t BS3_FAR_DATA g_abBs3Cg1Tests[] = ',
            '{',
        ];
        for oTests in self.aoTests:
            asLines.append('    /*');
            asLines.append('     * offTests=%s' % (oTests.offTests,));
            asLines.append('     * Instructions: %s' % (', '.join([oInstr.sStats for oInstr in oTests.aoInstructions]),));
            asLines.append('     */');
            asLines += oTests.asLines;
        asLines += [
            '};',
            '',
        ];


        #/** The test data that BS3CG1INSTR.
        # * In order to simplify generating these, we use a byte array. */
        #extern const uint8_t BS3_FAR_DATA   g_abBs3Cg1Tests[];


        oOut.write('\n'.join(asLines));
        return True;


    def usage(self):
        """ Prints usage. """
        print('usage: bs3-cpu-generated-1-data.py [output file|-]');
        return 0;

    def main(self, asArgs):
        """
        C-like main function.
        Returns exit code.
        """

        #
        # Quick argument parsing.
        #
        if len(asArgs) == 1:
            sOutFile = '-';
        elif len(asArgs) != 2:
            print('syntax error! Expected exactly one argument.');
            return 2;
        elif asArgs[1] in [ '-h', '-?', '--help' ]:
            return self.usage();
        else:
            sOutFile = asArgs[1];

        #
        # Process the instructions specified in the IEM sources.
        #
        if self.processInstruction():

            #
            # Open the output file and generate the code.
            #
            if sOutFile == '-':
                oOut = sys.stdout;
            else:
                try:
                    oOut = open(sOutFile, 'w');                 # pylint: disable=consider-using-with,unspecified-encoding
                except Exception as oXcpt:
                    print('error! Failed open "%s" for writing: %s' % (sOutFile, oXcpt,));
                    return 1;
            if self.generateCode(oOut):
                return 0;

        return 1;


if __name__ == '__main__':
    sys.exit(Bs3CpuGenerated1Generator().main(sys.argv));

