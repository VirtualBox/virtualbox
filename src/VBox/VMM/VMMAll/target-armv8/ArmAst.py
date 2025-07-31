#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: ArmAst.py 110124 2025-07-05 01:55:01Z knut.osmundsen@oracle.com $

"""
ARM BSD / OpenSource specification reader - AST related bits.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2025 Oracle and/or its affiliates.

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
__version__ = "$Revision: 110124 $"

# Standard python imports.
import re;



#
# Misc Helpers for decoding ARM specs and JSON.
#
def assertJsonAttribsInSet(dJson, oAttribSet):
    """ Checks that the JSON element has all the attributes in the set and nothing else. """
    #assert set(dJson) == oAttribSet, '%s - %s' % (set(dJson) ^ oAttribSet, dJson,);
    assert len(dJson) == len(oAttribSet) and sum(sKey in oAttribSet for sKey in dJson), \
           '%s - %s' % (set(dJson) ^ oAttribSet, dJson,);


#
# The ARM instruction AST stuff.
#

class ArmAstBase(object):
    """
    ARM instruction AST base class.
    """

    ksTypeAssignment    = 'AST.Assignment';
    ksTypeBinaryOp      = 'AST.BinaryOp';
    ksTypeBool          = 'AST.Bool';
    ksTypeConcat        = 'AST.Concat';
    ksTypeDotAtom       = 'AST.DotAtom';
    ksTypeFunction      = 'AST.Function';
    ksTypeIdentifier    = 'AST.Identifier';
    ksTypeInteger       = 'AST.Integer';
    ksTypeReturn        = 'AST.Return';
    ksTypeSet           = 'AST.Set';
    ksTypeSlice         = 'AST.Slice';
    ksTypeSquareOp      = 'AST.SquareOp';
    ksTypeType          = 'AST.Type';
    ksTypeTypeAnnotation= 'AST.TypeAnnotation';
    ksTypeTuple         = 'AST.Tuple';
    ksTypeUnaryOp       = 'AST.UnaryOp';
    ksTypeValue         = 'Values.Value';
    ksTypeEquationValue = 'Values.EquationValue';
    ksTypeValuesGroup   = 'Values.Group';
    ksTypeField         = 'Types.Field';
    ksTypeString        = 'Types.String';
    ksTypeRegisterType  = 'Types.RegisterType';

    ksModeAccessor      = 'accessor';
    ksModeAccessorCond  = 'accessor-cond';
    ksModeConditions    = 'condition';
    ksModeConstraints   = 'contraints';
    ksModeValuesOnly    = 'values-only'; # Value and EquationValue

    def __init__(self, sType):
        self.sType = sType;

    khAttribSetBinaryOp = frozenset(['_type', 'left', 'op', 'right']);
    @staticmethod
    def fromJsonBinaryOp(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetBinaryOp);
        return ArmAstBinaryOp(ArmAstBase.fromJson(oJson['left'], sMode),
                              oJson['op'],
                              ArmAstBase.fromJson(oJson['right'], sMode),
                              sMode == ArmAstBase.ksModeConstraints);

    khAttribSetUnaryOp = frozenset(['_type', 'op', 'expr']);
    @staticmethod
    def fromJsonUnaryOp(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetUnaryOp);
        return ArmAstUnaryOp(oJson['op'], ArmAstBase.fromJson(oJson['expr'], sMode));

    khAttribSetSlice = frozenset(['_type', 'left', 'right']);
    @staticmethod
    def fromJsonSlice(oJson, sMode):
        assert sMode in (ArmAstBase.ksModeAccessor, ArmAstBase.ksModeAccessorCond);
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetSlice);
        return ArmAstSlice(ArmAstBase.fromJson(oJson['left'], sMode), ArmAstBase.fromJson(oJson['right'], sMode));

    khAttribSetSquareOp = frozenset(['_type', 'var', 'arguments']);
    @staticmethod
    def fromJsonSquareOp(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetSquareOp);
        return ArmAstSquareOp(ArmAstBase.fromJson(oJson['var'], sMode),
                              [ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['arguments']]);

    khAttribSetTuple = frozenset(['_type', 'values']);
    @staticmethod
    def fromJsonTuple(oJson, sMode):
        assert sMode == ArmAstBase.ksModeAccessor;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetTuple);
        return ArmAstTuple([ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['values']]);

    khAttribSetDotAtom = frozenset(['_type', 'values']);
    @staticmethod
    def fromJsonDotAtom(oJson, sMode):
        assert sMode in (ArmAstBase.ksModeConstraints, ArmAstBase.ksModeAccessor, ArmAstBase.ksModeAccessorCond);
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetDotAtom);
        return ArmAstDotAtom([ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['values']]);

    khAttribSetConcat = frozenset(['_type', 'values']);
    @staticmethod
    def fromJsonConcat(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetConcat);
        return ArmAstConcat([ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['values']]);

    khAttribSetFunction = frozenset(['_type', 'name', 'arguments']);
    @staticmethod
    def fromJsonFunction(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetFunction);
        return ArmAstFunction(oJson['name'], [ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['arguments']]);

    khAttribSetIdentifier = frozenset(['_type', 'value']);
    @staticmethod
    def fromJsonIdentifier(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetIdentifier);
        return ArmAstIdentifier(oJson['value'], sMode == ArmAstBase.ksModeConstraints);

    khAttribSetBool = frozenset(['_type', 'value']);
    @staticmethod
    def fromJsonBool(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetBool);
        return ArmAstBool(oJson['value']);

    khAttribSetInteger = frozenset(['_type', 'value']);
    @staticmethod
    def fromJsonInteger(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetInteger);
        return ArmAstInteger(oJson['value']);

    khAttribSetSet = frozenset(['_type', 'values']);
    @staticmethod
    def fromJsonSet(oJson, sMode):
        assert sMode != ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetSet);
        return ArmAstSet([ArmAstBase.fromJson(oArg, sMode) for oArg in oJson['values']]);

    khAttribSetValue = frozenset(['_type', 'value', 'meaning']);
    @staticmethod
    def fromJsonValue(oJson, sMode):
        _ = sMode;
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetValue);
        return ArmAstValue(oJson['value']);

    khAttribSetEquationValue      = frozenset(['_type', 'value', 'meaning', 'slice']);
    khAttribSetEquationValueRange = frozenset(['_type', 'start', 'width']);
    @staticmethod
    def fromJsonEquationValue(dJson, sMode):
        assert sMode == ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetEquationValue);
        assert len(dJson['slice']) == 1;
        dSlice = dJson['slice'][0];
        assert dSlice['_type'] == 'Range';
        assertJsonAttribsInSet(dSlice, ArmAstBase.khAttribSetEquationValueRange);
        return ArmAstEquationValue(dJson['value'], int(dSlice['start']), int(dSlice['width']));

    khAttribSetValuesGroup = frozenset(['_type', 'value', 'meaning', 'values']);
    @staticmethod
    def fromJsonValuesGroup(dJson, sMode):
        assert sMode == ArmAstBase.ksModeValuesOnly;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetValuesGroup);
        assert dJson['values']['_type'] == 'Valuesets.Values';
        assert len(dJson['values']['values']) == 0;
        return ArmAstValuesGroup(dJson['value']);

    khAttribSetField = frozenset(['_type', 'value']);
    @staticmethod
    def fromJsonString(oJson, sMode):
        assert sMode in (ArmAstBase.ksModeConstraints, # Seen in register as 'input' to ImpDefBool.
                         ArmAstBase.ksModeAccessorCond);
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetField);
        return ArmAstString(oJson['value']);

    khAttribSetField = frozenset(['_type', 'value']);
    khAttribSetFieldValue = frozenset(['field', 'name', 'state', 'instance', 'slices']);
    @staticmethod
    def fromJsonField(oJson, sMode):
        assert sMode in (ArmAstBase.ksModeConstraints, ArmAstBase.ksModeAccessor, ArmAstBase.ksModeAccessorCond);
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetField);
        dJsonValue = oJson['value'];
        assertJsonAttribsInSet(dJsonValue, ArmAstBase.khAttribSetFieldValue);
        return ArmAstField(dJsonValue['field'], dJsonValue['name'], dJsonValue['state'],
                           dJsonValue['slices'], dJsonValue['instance']);

    khAttribSetRegisterType = frozenset(['_type', 'value']);
    khAttribSetRegisterTypeValue = frozenset(['name', 'state', 'instance', 'slices']);
    @staticmethod
    def fromJsonRegisterType(oJson, sMode):
        assert sMode in (ArmAstBase.ksModeConstraints, ArmAstBase.ksModeAccessorCond);
        assertJsonAttribsInSet(oJson, ArmAstBase.khAttribSetRegisterType);
        dJsonValue = oJson['value'];
        assertJsonAttribsInSet(dJsonValue, ArmAstBase.khAttribSetRegisterTypeValue);
        return ArmAstRegisterType(dJsonValue['name'], dJsonValue['state'], dJsonValue['slices'], dJsonValue['instance']);

    khAttribSetType = frozenset(['_type', 'name']);
    @staticmethod
    def fromJsonType(dJson, sMode):
        assert sMode == ArmAstBase.ksModeAccessor;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetType);
        return ArmAstType(ArmAstBase.fromJson(dJson['name'], sMode));

    khAttribSetTypeAnnotation = frozenset(['_type', 'type', 'var']);
    @staticmethod
    def fromJsonTypeAnnotation(dJson, sMode):
        assert sMode == ArmAstBase.ksModeAccessor;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetTypeAnnotation);
        return ArmAstTypeAnnotation(ArmAstBase.fromJson(dJson['var'], sMode), ArmAstBase.fromJson(dJson['type'], sMode));

    khAttribSetAssignment = frozenset(['_type', 'val', 'var']);
    @staticmethod
    def fromJsonAssignment(dJson, sMode):
        assert sMode == ArmAstBase.ksModeAccessor;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetAssignment);
        return ArmAstAssignment(ArmAstBase.fromJson(dJson['var'], sMode), ArmAstBase.fromJson(dJson['val'], sMode));

    khAttribSetReturn = frozenset(['_type', 'val']);
    @staticmethod
    def fromJsonReturn(dJson, sMode):
        assert sMode == ArmAstBase.ksModeAccessor;
        assertJsonAttribsInSet(dJson, ArmAstBase.khAttribSetReturn);
        return ArmAstReturn(ArmAstBase.fromJson(dJson['val'], sMode) if dJson['val'] else None);

    kfnTypeMap = {
        ksTypeBinaryOp:         fromJsonBinaryOp,
        ksTypeUnaryOp:          fromJsonUnaryOp,
        ksTypeSlice:            fromJsonSlice,
        ksTypeSquareOp:         fromJsonSquareOp,
        ksTypeTuple:            fromJsonTuple,
        ksTypeDotAtom:          fromJsonDotAtom,
        ksTypeConcat:           fromJsonConcat,
        ksTypeFunction:         fromJsonFunction,
        ksTypeIdentifier:       fromJsonIdentifier,
        ksTypeBool:             fromJsonBool,
        ksTypeInteger:          fromJsonInteger,
        ksTypeSet:              fromJsonSet,
        ksTypeValue:            fromJsonValue,
        ksTypeEquationValue:    fromJsonEquationValue,
        ksTypeValuesGroup:      fromJsonValuesGroup,
        ksTypeString:           fromJsonString,
        ksTypeField:            fromJsonField,
        ksTypeRegisterType:     fromJsonRegisterType,
        ksTypeType:             fromJsonType,
        ksTypeTypeAnnotation:   fromJsonTypeAnnotation,
        ksTypeAssignment:       fromJsonAssignment,
        ksTypeReturn:           fromJsonReturn,
    };

    @staticmethod
    def fromJson(dJson, sMode = ksModeConditions):
        """ Decodes an AST/Values expression. """
        #print('debug ast: %s' % dJson['_type'])
        return ArmAstBase.kfnTypeMap[dJson['_type']](dJson, sMode);

    def toString(self):
        return self.toStringEx();

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        """ Extended version of toString() that can split the expression up into multiple lines (newline sep). """
        _ = cchMaxWidth; _ = sLang;
        #raise Exception('Child did not implement toStringEx: %s' % (type(self),));
        return '{todo: implement toStringEx for %s!}' % (type(self).__name__,);

    def toDebugString(self, sLang = None, cchMaxWidth = 120):
        """ For debugging the tree structure... """
        return '{%s: %s}' % (type(self).__name__, self.toStringEx(sLang, cchMaxWidth),);

    def __str__(self):
        return self.toString();

    def __repr__(self):
        return self.toString();

    def _walker(self, fnCallback, oCallbackArg, fDepthFirst, *aoChildren):
        """ Helper for walking. """
        if fDepthFirst:
            for oChild in aoChildren:
                oChild.walk(fnCallback, oCallbackArg, fDepthFirst);
            fnCallback(self, oCallbackArg);
        else:
            fnCallback(self, oCallbackArg);
            for oChild in aoChildren:
                oChild.walk(fnCallback, oCallbackArg, fDepthFirst);
        return True;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        """ Walker. """
        _ = fnCallback; _ = oCallbackArg; _ = fDepthFirst;
        raise Exception('Not implemented by %s' % (type(self).__name__,));

    def isLeaf(self):
        """ Checks if this is a leaf node or not. """
        return False;

    def containsNode(self, oNodeToFind):
        """ Checks if oNodeToFind is part of this tree. """
        dResult = {'ret': False,};
        def callback(oNode, dResult):
            if oNode == oNodeToFind:
                dResult['ret'] = True;
        self.walk(callback, dResult);
        return dResult['ret'];

    #
    # Convenience matching routines, matching node type and type specific value.
    #

    def isBoolAndTrue(self):
        """ Checks if this is a boolean with the value True. """
        # This is overridden by ArmAstBool.
        return False;

    def isBoolAndFalse(self):
        """ Checks if this is a boolean with the value False. """
        # This is overridden by ArmAstBool.
        return False;

    def isMatchingIdentifier(self, sName):
        """ Checks if this is an identifier with the given name. """
        # This is overridden by ArmAstIdentifier.
        _ = sName;
        return False;

    def getIdentifierName(self):
        """ If this is an identifier, its name is return, otherwise None. """
        # This is overridden by ArmAstIdentifier.
        return None;

    def isMatchingDotAtom(self, *asElements):
        """ Checks if this is a dot atom node with the given list of string identifiers. """
        # This is overridden by ArmAstDotAtom.
        _ = asElements;
        return False;

    def isMatchingInteger(self, iValue):
        """ Checks if this is an integer node with the given value. """
        # This is overridden by ArmAstInteger.
        _ = iValue;
        return False;

    def getIntegerValue(self):
        """ Return the value of an integer node, otherwise return None """
        # This is overridden by ArmAstInteger.
        return None;

    def isMatchingIntegerOrValue(self, iValue):
        """ Checks if this is an integer or a fixed value node with the given value. """
        # This is overridden by ArmAstInteger and ArmAstValue.
        _ = iValue;
        return False;

    def getIntegerOrValue(self):
        """
        Return the value of an integer or value node, otherwise return None
        In the ArmAstValue case, None is returned if any wildcard is used.
        """
        # This is overridden by ArmAstInteger and ArmAstValue.
        return None;

    def isMatchingSquareOp(self, sVar, *aoValueMatches):
        """
        Checks if this is a square op node with the given variable and values.
        Values are mapped as following:
            - int value to ArmAstInteger.
            - str value to ArmAstIdentifier.
            - None matches anything..
        """
        # This is overridden by ArmAstSquareOp.
        _ = sVar; _ = aoValueMatches;
        return False;

    def isMatchingFunctionCall(self, sFunctionName, *aoArgMatches):
        """
        Checks if this is a function (call) node with the given name and arguments.
        Values are mapped as following:
            - int value to ArmAstInteger.
            - str value to the toString result.
            - None matches anything..
        """
        # This is overridden by ArmAstFunction.
        _ = sFunctionName; _ = aoArgMatches;
        return False;

    def isMatchingField(self, sField, sName, sState = 'AArch64'):
        """
        Checks if this is a field access node for the given field and register.
        A sField of None will match any field.  The other two arguments must be strings.
        """
        _ = sField; _ = sName; _ = sState;
        return False;


class ArmAstLeafBase(ArmAstBase):
    """
    Base class for simple leaf nodes.

    This implements the transformation and walking methods.
    """
    def __init__(self, sType):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeField);

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        _ = fDepthFirst;
        fnCallback(self, oCallbackArg);
        return True;

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def isLeaf(self):
        return True;


class ArmAstValuesBase(ArmAstBase):
    """ Base class for a node with a value list (aoValues). """
    def __init__(self, sType, aoValues):
        ArmAstBase.__init__(self, sType);
        self.aoValues = aoValues;

    def isSame(self, oOther):
        assert isinstance(self, type(self));
        if isinstance(oOther, type(self)):
            if len(self.aoValues) == len(oOther.aoValues):
                for idx, oMyValue in enumerate(self.aoValues):
                    if not oMyValue.isSame(oOther.aoValues[idx]):
                        return False;
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, *self.aoValues);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.aoValues = [oValue.transform(fnCallback, False, oCallbackArg, aoStack) for oValue in self.aoValues];
        aoStack.pop();
        for oValue in self.aoValues:
            assert oValue;
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);


class ArmAstBinaryOp(ArmAstBase):
    ksOpTypeCompare      = 'cmp';
    ksOpTypeLogical      = 'log';
    ksOpTypeArithmetical = 'arit';
    ksOpTypeSet          = 'set';
    ksOpTypeConstraints  = 'constraints';
    ksOpTypeBitwise      = 'bitwise';
    kdOps = {
        '||':  ksOpTypeLogical,
        '&&':  ksOpTypeLogical,
        '==':  ksOpTypeCompare,
        '!=':  ksOpTypeCompare,
        '>':   ksOpTypeCompare,
        '<':   ksOpTypeCompare,
        '>=':  ksOpTypeCompare,
        '<=':  ksOpTypeCompare,
        'IN':  ksOpTypeSet,
        '+':   ksOpTypeArithmetical,
        '-':   ksOpTypeArithmetical,
        'MOD': ksOpTypeArithmetical,
        '*':   ksOpTypeArithmetical,
        'AND': ksOpTypeBitwise,
        'OR':  ksOpTypeBitwise,
        '-->': ksOpTypeConstraints,    # implies that the right hand side is true when left hand side is.
        '<->': ksOpTypeConstraints,    # bidirectional version of -->, i.e. it follows strictly in both directions.
    };
    kdOpsToC = {
        '||':  '||',
        '&&':  '&&',
        '==':  '==',
        '!=':  '!=',
        '>':   '>',
        '<':   '<',
        '>=':  '>=',
        '<=':  '<=',
        #'IN':  'IN',
        '+':   '+',
        '-':   '-',
        'MOD': '%',
        '*':   '*',
        'AND': '&',
        'OR':  '|',
        #'-->': ksOpTypeConstraints,
        #'<->': ksOpTypeConstraints,
    };

    ## This is operators that can be grouped by toStringEx.
    # It's operators with the same C++ precedency, more or less.
    kdOpGroupings = {
        '||':  { '||', },
        '&&':  { '&&', },
        '==':  { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '!=':  { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '>':   { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '<':   { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '>=':  { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '<=':  { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        'IN':  { '==', '!=', '>', '<', '>=', '<=', 'IN', },
        '+':   { '+', '-', },
        '-':   { '+', '-', },
        'MOD': { '*', 'MOD', },
        '*':   { '*', 'MOD', },
        'AND': { 'AND', },
        'OR':  { 'OR', },
        '-->': { '-->', },
        '<->': { '<->', },
    };

    ## Operator precedency, lower means higher importance.
    kdOpPrecedence = {
        '||':   15,
        '&&':   14,
        '==':   10,
        '!=':   10,
        '>':    9,
        '<':    9,
        '>=':   9,
        '<=':   9,
        'IN':   9,
        '+':    6,
        '-':    6,
        'MOD':  5,
        '*':    5,
        'AND':  11,
        'OR':   13,
        '-->':  17,
        '<->':  17,
    };

    ## Boolean negation transformation rules: (sNewOperator, fNegateOperands)
    kdOpBoolNegation = {
        '||':  ('&&', True),
        '&&':  ('||', True),
        '==':  ('!=', False),
        '!=':  ('==', False),
        '>':   ('<=', False),
        '<':   ('>=', False),
        '>=':  ('<',  False),
        '<=':  ('>',  False),
    };


    def __init__(self, oLeft, sOp, oRight, fConstraints = False):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeBinaryOp);
        assert sOp in ArmAstBinaryOp.kdOps and (fConstraints or ArmAstBinaryOp.kdOps[sOp] != ArmAstBinaryOp.ksOpTypeConstraints),\
               'sOp="%s"' % (sOp,);
        self.oLeft  = oLeft;
        self.sOp    = sOp;
        self.oRight = oRight;

        # Switch value == field non-sense (simplifies transferConditionsToEncoding and such):
        if (    isinstance(oRight, ArmAstIdentifier)
            and isinstance(oLeft, (ArmAstValue, ArmAstInteger))
            and sOp in ['==', '!=']):
            self.oLeft  = oRight;
            self.oRight = oLeft;

    def clone(self):
        return ArmAstBinaryOp(self.oLeft.clone(), self.sOp, self.oRight.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstBinaryOp):
            if self.sOp == oOther.sOp:
                if self.oLeft.isSame(oOther.oLeft):
                    if self.oRight.isSame(oOther.oRight):
                        return True;
            ## @todo switch sides and whatnot.
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oLeft, self.oRight);

    @staticmethod
    def _transformEnsureBool(oNode):
        """
        Tries to make sure the reduction of a logical expression to just one of
        the operands (oNode) still results in a boolean.
        """
        if isinstance(oNode, ArmAstBool):
            return oNode;
        if isinstance(oNode, ArmAstBinaryOp):
            if ArmAstBinaryOp.kdOps[oNode.sOp] in (ArmAstBinaryOp.ksOpTypeCompare, ArmAstBinaryOp.ksOpTypeLogical,
                                                   ArmAstBinaryOp.ksOpTypeSet):
                return oNode;
        elif isinstance(oNode, ArmAstUnaryOp):
            if ArmAstUnaryOp.kdOps[oNode.sOp] == ArmAstBinaryOp.ksOpTypeLogical:
                return oNode;
        elif isinstance(oNode, ArmAstFunction):
            if (   oNode.sName.startswith('Is')   # ASSUMES 'Is' means predicates that return bool.
                or oNode.sName.startswith('Have') # ASSUMES 'Have' means predicates that return bool.
                or oNode.sName in ('ELIsInHost', 'EL2Enabled',) ):
                return oNode;
        elif isinstance(oNode, ArmAstCppExpr):
            if oNode.cBitsWidth == 1:
                return oNode;
        else:
            iIntValue = oNode.getIntegerOrValue();
            if iIntValue is not None:
                return ArmAstBool(iIntValue != 0);

        # Just wrap it in double boolean negation for now.
        return ArmAstUnaryOp('!', ArmAstUnaryOp('!', oNode));


    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        # Recurse first.
        aoStack.append(self);
        fChildElimination = ArmAstBinaryOp.kdOps[self.sOp] in (ArmAstBinaryOp.ksOpTypeLogical,);
        self.oLeft  = self.oLeft.transform(fnCallback, fChildElimination, oCallbackArg, aoStack);
        self.oRight = self.oRight.transform(fnCallback, fChildElimination, oCallbackArg, aoStack);
        assert (self.oLeft and self.oRight) or fChildElimination;
        aoStack.pop();

        if self.oLeft and self.oRight:
            if self.sOp == '||':
                # Simplify: 'true || x'; 'x || true'; '1 || x'; 'x || 34'
                if self.oLeft.isBoolAndTrue() or self.oLeft.getIntegerOrValue() not in (None, 0):
                    return ArmAstBool(True);
                if self.oRight.isBoolAndTrue() or self.oRight.getIntegerOrValue() not in (None, 0):
                    return ArmAstBool(True); # This is ASSUMING no function call sideeffects!
                # Simplify: 'false || x; '0 || x; 'x || false'; '0 || false'; 'false || false'; '0 || 0'
                if self.oLeft.isBoolAndFalse() or self.oLeft.isMatchingIntegerOrValue(0):
                    if self.oRight.isBoolAndFalse() or self.oRight.isMatchingIntegerOrValue(0):
                        return ArmAstBool(False);
                    return fnCallback(self._transformEnsureBool(self.oRight), fEliminationAllowed, oCallbackArg, aoStack);
                if self.oRight.isBoolAndFalse() or self.oRight.isMatchingIntegerOrValue(0):
                    return fnCallback(self._transformEnsureBool(self.oLeft), fEliminationAllowed, oCallbackArg, aoStack);

            elif self.sOp == '&&':
                if (   self.oLeft.isBoolAndFalse()
                    or self.oLeft.isMatchingIntegerOrValue(0)
                    or self.oRight.isBoolAndFalse()
                    or self.oRight.isMatchingIntegerOrValue(0)):
                    if not fEliminationAllowed:
                        return fnCallback(ArmAstBool(False), fEliminationAllowed, oCallbackArg, aoStack);
                    return None;
                if self.oLeft.isBoolAndTrue() or self.oLeft.getIntegerOrValue() not in (None, 0):
                    return fnCallback(self._transformEnsureBool(self.oRight), fEliminationAllowed, oCallbackArg, aoStack);
                if self.oRight.isBoolAndTrue() or self.oRight.getIntegerOrValue() not in (None, 0):
                    return fnCallback(self._transformEnsureBool(self.oLeft), fEliminationAllowed, oCallbackArg, aoStack);

            else:
                iLeft = self.oLeft.getIntegerOrValue();
                if iLeft is None and isinstance(self.oLeft, ArmAstBool):
                    iLeft = 1 if self.oLeft.fValue else 0;
                if iLeft is not None:

                    iRight = self.oRight.getIntegerOrValue();
                    if iRight is None and isinstance(self.oRight, ArmAstBool):
                        iRight = 1 if self.oRight.fValue else 0;
                    if iRight is not None:
                        if self.sOp == '==':
                            return fnCallback(ArmAstBool(iLeft == iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        if self.sOp == '!=':
                            return fnCallback(ArmAstBool(iLeft != iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        if self.sOp == '>':
                            return fnCallback(ArmAstBool(iLeft >  iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        if self.sOp == '>=':
                            return fnCallback(ArmAstBool(iLeft >= iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        if self.sOp == '<':
                            return fnCallback(ArmAstBool(iLeft <  iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        if self.sOp == '<=':
                            return fnCallback(ArmAstBool(iLeft <= iRight), fEliminationAllowed, oCallbackArg, aoStack);
                        ## @todo we could do +, -, div, mod, mult here, but need to consider the width of the result...

            return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

        if self.sOp == '||':
            if self.oLeft and not self.oLeft.isMatchingIntegerOrValue(0):
                return fnCallback(self._transformEnsureBool(self.oLeft), fEliminationAllowed, oCallbackArg, aoStack);
            if self.oRight and not self.oRight.isMatchingIntegerOrValue(0):
                return fnCallback(self._transformEnsureBool(self.oRight), fEliminationAllowed, oCallbackArg, aoStack);
        else:
            assert self.sOp == '&&';
        return fnCallback(ArmAstBool(False), fEliminationAllowed, oCallbackArg, aoStack) if not fEliminationAllowed else None;

    @staticmethod
    def needParentheses(oNode, sOp = '&&'):
        if isinstance(oNode, ArmAstBinaryOp):
            if sOp != oNode.sOp or sOp not in ('||', '&&', '+', '-', '*'):
                return True;
        return False;

    @staticmethod
    def getOpForLang(sOp, sLang):
        if sLang == 'C':
            return ArmAstBinaryOp.kdOpsToC.get(sOp, sOp);
        return sOp;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        """ Extended version of toString() that can split the expression up into multiple lines (newline sep). """
        #
        # Create a list of binary ops that can be grouped together with self.sOp.
        #
        cchIndent   = len(self.getOpForLang(self.sOp, sLang)) + 1;
        dOpGrouping = self.kdOpGroupings[self.sOp];
        aoList      = [ self.oLeft, self.sOp, self.oRight ];
        idx = 0;
        while idx < len(aoList):
            oEntry = aoList[idx];
            if isinstance(oEntry, str):
                cchIndent = max(cchIndent, len(self.getOpForLang(oEntry, sLang)) + 1);
            elif isinstance(oEntry, ArmAstBinaryOp) and oEntry.sOp in dOpGrouping:
                aoList = aoList[:idx] + [ oEntry.oLeft, oEntry.sOp, oEntry.oRight ] + aoList[idx + 1:];
                idx -= 1;
            idx += 1;

        #
        # Now do the formatting.  This is constructing two alternative strings,
        # one where everything is potentially on a single line and a multiline one.
        #
        cchNewMaxWidth = max(cchMaxWidth - cchIndent, 16);
        sRetSameLine   = '';
        sRetMultiLine  = '';
        idx            = 0;
        sCurOp         = ' ' * (cchIndent - 1);
        iCurOpPrio     = self.kdOpPrecedence[self.sOp];
        while idx < len(aoList):
            oNode       = aoList[idx];
            sNextOp     = aoList[idx + 1] if idx + 1 < len(aoList) else sCurOp;
            iNextOpPrio = self.kdOpPrecedence[sNextOp] if idx + 1 < len(aoList) else iCurOpPrio;
            sNodeExpr   = oNode.toStringEx(sLang, cchNewMaxWidth);
            if isinstance(oNode, ArmAstBinaryOp):
                tOps = (sNextOp, sCurOp);
                if (   self.kdOpPrecedence[oNode.sOp] > min(iNextOpPrio, iCurOpPrio)
                    or (oNode.sOp == '&&'  and '||' in tOps) # Compilers may warn when mixing && and ||.  Helps readability too.
                    or (oNode.sOp == 'AND' and 'OR' in tOps) # Compilers may warn when mixing & and |. Helps readability too.
                   ):
                    sNodeExpr = '(' + sNodeExpr.replace('\n', '\n ') + ')';
            if idx == 0:
                sRetSameLine = sNodeExpr;
            else:
                sRetSameLine  += ' %s %s' % (self.getOpForLang(sCurOp, sLang).strip(), sNodeExpr);
                sRetMultiLine += '\n';
            sRetMultiLine += '%-*s%s' % (cchIndent,
                                         self.getOpForLang(sCurOp, sLang), sNodeExpr.replace('\n', '\n' + ' ' * cchIndent),);

            # next;
            sCurOp     = sNextOp;
            iCurOpPrio = iNextOpPrio;
            idx       += 2;

        # Pick which string to return.
        if len(sRetSameLine) <= cchMaxWidth and '\n' not in sRetSameLine:
            return sRetSameLine;
        return sRetMultiLine;

    def toCExpr(self, oHelper):
        # Logical, compare, arithmetical & bitwise operations are straight forward.
        if ArmAstBinaryOp.kdOps[self.sOp] in (ArmAstBinaryOp.ksOpTypeLogical,
                                              ArmAstBinaryOp.ksOpTypeCompare,
                                              ArmAstBinaryOp.ksOpTypeArithmetical,
                                              ArmAstBinaryOp.ksOpTypeBitwise):
            sLeft = self.oLeft.toCExpr(oHelper);
            if ArmAstBinaryOp.needParentheses(self.oLeft, self.sOp):
                sLeft = '(%s)' % (sLeft);

            sRight = self.oRight.toCExpr(oHelper);
            if ArmAstBinaryOp.needParentheses(self.oRight, self.sOp):
                sRight = '(%s)' % (sRight);
            return '%s %s %s' % (sLeft, ArmAstBinaryOp.kdOpsToC[self.sOp], sRight);

        # 'x IN (y,z,...)' needs rewriting.
        if self.sOp == 'IN':
            if not isinstance(self.oRight, ArmAstSet):
                raise Exception('Unsupported right operand to IN operator: %s' % (self.toString(),));

            if isinstance(self.oLeft, ArmAstIdentifier):
                (sCName, cBitsWidth) = oHelper.getFieldInfo(self.oLeft.sName);
            elif isinstance(self.oLeft, ArmAstField):
                (sCName, cBitsWidth) = oHelper.getFieldInfo(self.oLeft.sField, self.oLeft.sName, self.oLeft.sState);
            else:
                raise Exception('Unsupported left operand to IN operator: %s' % (self.toString(),));

            asTests = [];
            for oValue in self.oRight.aoValues:
                if isinstance(oValue, ArmAstValue):
                    (fValue, fFixed, fWildcard, _) = ArmAstValue.parseValue(oValue.sValue, cBitsWidth);
                    fCombined = fValue | fFixed | fWildcard;
                    if fCombined < 0 or fCombined >= (1 << cBitsWidth):
                        raise Exception('Set value out of range: %s, width %u bits (expr: %s)'
                                        % (oValue.toString(), cBitsWidth, self.toString(),));
                    if fFixed == ((1 << cBitsWidth) - 1):
                        if fValue < 10:
                            asTests.append('%s == %u' % (sCName, fValue,));
                        elif fValue < (1 << 31):
                            asTests.append('%s == %#x' % (sCName, fValue,));
                        else:
                            asTests.append('%s == UINT32_C(%#010x)' % (sCName, fValue,));
                    else:
                        if fFixed < 10:
                            asTests.append('(%s & %u) == %u' % (sCName, fFixed, fValue,));
                        elif fFixed < (1 << 31):
                            asTests.append('(%s & %#x) == %#x' % (sCName, fFixed, fValue,));
                        else:
                            asTests.append('(%s & %#010x) == UINT32_C(%#010x)' % (sCName, fFixed, fValue,));
                elif isinstance(oValue, ArmAstInteger):
                    if oValue.iValue < 0 or oValue.iValue >= (1 << cBitsWidth):
                        raise Exception('Set value out of range: %s, width %u bits (expr: %s)'
                                        % (oValue.toString(), cBitsWidth, self.toString(),));
                    asTests.append('(%s == %s)' % (sCName, oValue.toCExpr(oHelper),));
                else:
                    raise Exception('Unsupported value in set: %s (expr: %s)' % (oValue.toString(), self.toString(),));

            if len(asTests) == 1:
                return asTests[0];
            return '(%s)' % (' || '.join(asTests),);

        raise Exception('Unsupported binary operator: %s (%s)' % (self.sOp, self.toString(),));

    def getWidth(self, oHelper):
        _ = oHelper;
        sOpType = self.kdOps[self.sOp];
        if sOpType in (self.ksOpTypeCompare, self.ksOpTypeLogical, self.ksOpTypeSet):
            return 1; # boolean result.
        return -1;

    @staticmethod
    def andListToTree(aoAndConditions):
        """ Creates AST tree of AND binary checks from aoAndConditions. """
        if len(aoAndConditions) <= 1:
            return aoAndConditions[0].clone();
        return ArmAstBinaryOp(aoAndConditions[0].clone(), '&&', ArmAstBinaryOp.andListToTree(aoAndConditions[1:]));

    @staticmethod
    def orListToTree(aoOrConditions):
        """ Creates AST tree of OR binary checks from aoAndConditions. """
        if len(aoOrConditions) <= 1:
            return aoOrConditions[0].clone();
        return ArmAstBinaryOp(aoOrConditions[0].clone(), '||', ArmAstBinaryOp.orListToTree(aoOrConditions[1:]));


class ArmAstUnaryOp(ArmAstBase):
    ksOpTypeLogical      = 'log';
    ksOpTypeBitwise      = 'bitwise';
    kdOps = {
        '!':   ksOpTypeLogical,
        'NOT': ksOpTypeBitwise,
    };
    kdOpsToC = {
        '!':   '!',
        'NOT': '~',
    };

    def __init__(self, sOp, oExpr):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeUnaryOp);
        assert sOp in ArmAstUnaryOp.kdOps, 'sOp=%s' % (sOp,);
        self.sOp   = sOp;
        self.oExpr = oExpr;

    def clone(self):
        return ArmAstUnaryOp(self.sOp, self.oExpr.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstUnaryOp):
            if self.sOp == oOther.sOp:
                if self.oExpr.isSame(oOther.oExpr):
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oExpr);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        # If the expression is a logical or comparative binary op, push the
        # negation down one level to try make it go away.
        if isinstance(self.oExpr, ArmAstBinaryOp):
            if self.sOp == '!':
                tNegInfo = ArmAstBinaryOp.kdOpBoolNegation.get(self.oExpr.sOp);
                if tNegInfo:
                    self.oExpr.sOp = tNegInfo[0];
                    if tNegInfo[1]:
                        self.oExpr.oLeft  = ArmAstUnaryOp('!', self.oExpr.oLeft);
                        self.oExpr.oRight = ArmAstUnaryOp('!', self.oExpr.oRight);
                    return self.oExpr.transform(fnCallback, fEliminationAllowed, oCallbackArg, aoStack);
        # Eliminate double unary expressions.
        elif isinstance(self.oExpr, ArmAstUnaryOp):
            if self.oExpr.sOp == self.sOp: ## @todo For '!', we ASSUME the inner expression is also boolean.
                return self.oExpr.oExpr.transform(fnCallback, fEliminationAllowed, oCallbackArg, aoStack);

        # Regular transforming.
        aoStack.append(self);
        fChildElimination = ArmAstUnaryOp.kdOps[self.sOp] in (ArmAstUnaryOp.ksOpTypeLogical,);
        self.oExpr = self.oExpr.transform(fnCallback, fChildElimination, oCallbackArg, aoStack);
        aoStack.pop();
        if self.oExpr:
            if self.sOp == '!' and isinstance(self.oExpr, ArmAstBool):
                return fnCallback(ArmAstBool(not self.oExpr.fValue), fEliminationAllowed, oCallbackArg, aoStack);

            oRet = fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);
            if oRet is not None:
                if self.sOp == '!' and isinstance(oRet, ArmAstBool):
                    return ArmAstBool(not oRet.fValue);
                return oRet;
        else:
            assert fChildElimination;
        if fEliminationAllowed and self.sOp == 'NOT': ## @todo perhaps not quite sensible...
            return None;
        assert self.sOp == '!';
        return fnCallback(ArmAstBool(True), fEliminationAllowed, oCallbackArg, aoStack);

    @staticmethod
    def needParentheses(oNode):
        return isinstance(oNode, ArmAstBinaryOp)

    @staticmethod
    def getOpForLang(sOp, sLang, fAddSpace):
        if sLang == 'C':
            return ArmAstUnaryOp.kdOpsToC.get(sOp, sOp);
        if len(sOp) > 1 and fAddSpace:
            return sOp + ' ';
        return sOp;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        if ArmAstUnaryOp.needParentheses(self.oExpr):
            return '%s(%s)' % (self.getOpForLang(self.sOp, sLang, False), self.oExpr.toStringEx(sLang, cchMaxWidth),);
        return '%s%s' % (self.getOpForLang(self.sOp, sLang, True), self.oExpr.toStringEx(sLang, cchMaxWidth),);

    def toCExpr(self, oHelper):
        if ArmAstUnaryOp.needParentheses(self.oExpr):
            return '%s(%s)' % (self.getOpForLang(self.sOp, 'C', False), self.oExpr.toCExpr(oHelper));
        return '%s%s' % (self.getOpForLang(self.sOp, 'C', True), self.oExpr.toCExpr(oHelper));

    def getWidth(self, oHelper):
        if self.kdOps[self.sOp] == self.ksOpTypeLogical:
            return 1; # boolean result.
        return self.oExpr.getWidth(oHelper);


class ArmAstSlice(ArmAstBase):
    def __init__(self, oFrom, oTo):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeSlice);
        self.oFrom = oFrom; # left
        self.oTo   = oTo;   # right

    def clone(self):
        return ArmAstSlice(self.oFrom.clone(), self.oTo.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstSlice):
            if self.oFrom.isSame(oOther.oFrom):
                if self.oTo.isSame(oOther.oTo):
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oFrom, self.oTo);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.oFrom = self.oFrom.transform(fnCallback, False, oCallbackArg, aoStack);
        self.oTo   = self.oTo.transform(fnCallback, False, oCallbackArg, aoStack);
        assert self.oFrom and self.oTo;
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '[%s:%s]' % (self.oFrom.toStringEx(sLang, cchMaxWidth), self.oTo.toStringEx(sLang, cchMaxWidth));

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');

    def getWidth(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');


class ArmAstSquareOp(ArmAstBase):
    def __init__(self, oVar, aoValues):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeSquareOp);
        assert isinstance(oVar, ArmAstBase);
        self.oVar     = oVar;
        self.aoValues = aoValues;

    def clone(self):
        return ArmAstSquareOp(self.oVar.clone(), [oValue.clone() for oValue in self.aoValues]);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstSquareOp):
            if self.oVar.isSame(oOther.oVar):
                if len(self.aoValues) == len(oOther.aoValues):
                    for idx, oMyValue in enumerate(self.aoValues):
                        if not oMyValue.isSame(oOther.aoValues[idx]):
                            return False;
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oVar, *self.aoValues);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.oVar = self.oVar.transform(fnCallback, fEliminationAllowed, oCallbackArg, aoStack);
        if self.oVar:
            self.aoValues = [oValue.transform(fnCallback, False, oCallbackArg, aoStack) for oValue in self.aoValues];
            for oValue in self.aoValues:
                assert oValue;
            aoStack.pop();
            return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);
        aoStack.pop();
        assert fEliminationAllowed;
        return None;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        sVar  = self.oVar.toStringEx(sLang, cchMaxWidth);
        if not isinstance(self.oVar, (ArmAstIdentifier, ArmAstField,)):
            sVar = '(%s)' % (sVar);
        return '%s<%s>' % (sVar,
                           ','.join([oValue.toStringEx(sLang, cchMaxWidth) for oValue in self.aoValues]),);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('ArmAstSquareOp does not support conversion to C expression: %s' % (self.toString()));

    def getWidth(self, oHelper):
        _ = oHelper;
        return -1;

    def isMatchingSquareOp(self, sVar, *aoValueMatches):
        if self.oVar.isMatchingIdentifier(sVar):
            if len(self.aoValues) == len(aoValueMatches):
                for i, oValue in enumerate(self.aoValues):
                    if isinstance(aoValueMatches[i], int):
                        if not oValue.isMatchingInteger(aoValueMatches[i]):
                            return False;
                    elif isinstance(aoValueMatches[i], str):
                        if not oValue.isMatchingIdentifier(aoValueMatches[i]):
                            return False;
                    elif aoValueMatches[i] is int:
                        if not isinstance(oValue, ArmAstInteger):
                            return False;
                    elif aoValueMatches[i] is str:
                        if not isinstance(oValue, ArmAstIdentifier):
                            return False;
                    elif aoValueMatches[i] is not None:
                        raise Exception('Unexpected #%u: %s' % (i, aoValueMatches[i],));
                return True;
        return False;


class ArmAstTuple(ArmAstValuesBase):
    def __init__(self, aoValues):
        ArmAstValuesBase.__init__(self, ArmAstBase.ksTypeTuple, aoValues);

    def clone(self):
        return ArmAstTuple([oValue.clone() for oValue in self.aoValues]);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '(%s)' % (','.join([oValue.toStringEx(sLang, cchMaxWidth) for oValue in self.aoValues]),);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('ArmAstTuple does not support conversion to C expression: %s' % (self.toString()));

    def getWidth(self, oHelper):
        _ = oHelper;
        return -1;


class ArmAstDotAtom(ArmAstValuesBase):
    def __init__(self, aoValues):
        ArmAstValuesBase.__init__(self, ArmAstBase.ksTypeDotAtom, aoValues);

    def clone(self):
        return ArmAstDotAtom([oValue.clone() for oValue in self.aoValues]);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '.'.join([oValue.toStringEx(sLang, cchMaxWidth) for oValue in self.aoValues]);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('ArmAstDotAtom does not support conversion to C expression: %s' % (self.toString()));

    #@staticmethod
    #def fromString(self, sExpr):
    #    """ Limited to identifiers separated byt dots """
    #    asValues = sExpr.split('.');

    def getWidth(self, oHelper):
        _ = oHelper;
        return -1;

    def isMatchingDotAtom(self, *asElements):
        if len(asElements) == len(self.aoValues):
            for idx, sName in enumerate(asElements):
                if not self.aoValues[idx].isMatchingIdentifier(sName):
                    return False;
            return True;
        return False;


class ArmAstConcat(ArmAstValuesBase):
    def __init__(self, aoValues):
        ArmAstValuesBase.__init__(self, ArmAstBase.ksTypeConcat, aoValues);

    def clone(self):
        return ArmAstConcat([oValue.clone() for oValue in self.aoValues]);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        sRet = '';
        for oValue in self.aoValues:
            if sRet:
                sRet += ':'
            if isinstance(oValue, ArmAstIdentifier):
                sRet += oValue.sName;
            else:
                sRet += '(%s)' % (oValue.toStringEx(sLang, cchMaxWidth));
        return sRet;

    def toCExpr(self, oHelper):
        sConcat = '(';
        iBitPos = 0;
        for oPart in self.aoValues:
            if len(sConcat) > 1:
                sConcat += ' | ';
            if isinstance(oPart, ArmAstIdentifier):
                (sCName, cBitsWidth) = oHelper.getFieldInfo(oPart.sName);
                if iBitPos == 0:
                    sConcat += sCName;
                else:
                    sConcat += '(%s << %u)' % (sCName, iBitPos);
                iBitPos += cBitsWidth;
            else:
                raise Exception('Unexpected value type for concat(): %s' % (oPart.sType,));
        sConcat += ')';
        return sConcat;

    def getWidth(self, oHelper):
        _ = oHelper;
        cBitsWidth = 0;
        for oValue in self.aoValues:
            cBitsThis = oValue.getWidth(oHelper);
            if cBitsThis < 0:
                return -1;
            cBitsWidth += cBitsThis;
        return cBitsWidth;


class ArmAstFunctionCallBase(ArmAstBase):
    """ Common base class for ArmAstFunction and ArmAstCppCall. """
    def __init__(self, sType, sName, aoArgs):
        ArmAstBase.__init__(self, sType);
        self.sName  = sName;
        self.aoArgs = aoArgs;

    def isSame(self, oOther):
        if self.sName == oOther.sName:
            if len(self.aoArgs) == len(oOther.aoArgs):
                for idx, oMyArg in enumerate(self.aoArgs):
                    if not oMyArg.isSame(oOther.aoArgs[idx]):
                        return False;
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, *self.aoArgs);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.aoArgs = [oArgs.transform(fnCallback, False, oCallbackArg, aoStack) for oArgs in self.aoArgs];
        for oArgs in self.aoArgs:
            assert oArgs;
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        asArgs   = [oArg.toStringEx(sLang, max(cchMaxWidth - len(self.sName) - 1, 60)) for oArg in self.aoArgs];
        sArgList = ', '.join(asArgs);
        if '\n' in sArgList or len(self.sName) + len(sArgList) + 2 > cchMaxWidth:
            sNlIndent = '\n' + ' ' * (len(self.sName) + 1);
            sArgList  = '';
            for i, sArg in enumerate(asArgs):
                if i > 0: sArgList += ',' + sNlIndent;
                sArgList += sArg.replace('\n', sNlIndent);
        return '%s(%s)' % (self.sName, sArgList,);

    def toCExpr(self, oHelper):
        return oHelper.convertFunctionCall(self);

    def getWidth(self, oHelper):
        _ = oHelper;
        return -1;


class ArmAstFunction(ArmAstFunctionCallBase):
    """ This is used both as an expression and as a statment... """
    koReValidName = re.compile('^[_A-Za-z][_A-Za-z0-9]+$');

    def __init__(self, sName, aoArgs):
        assert self.koReValidName.match(sName), 'sName=%s' % (sName);
        ArmAstFunctionCallBase.__init__(self, ArmAstBase.ksTypeFunction, sName, aoArgs);

    def clone(self):
        return ArmAstFunction(self.sName, [oArg.clone() for oArg in self.aoArgs]);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstFunction):
            return ArmAstFunctionCallBase.isSame(self, oOther);
        return False;

    def isMatchingFunctionCall(self, sFunctionName, *aoArgMatches):
        if self.sName == sFunctionName:
            if len(self.aoArgs) == len(aoArgMatches):
                for i, oArg in enumerate(self.aoArgs):
                    if isinstance(aoArgMatches[i], int):
                        if not oArg.isMatchingInteger(aoArgMatches[i]):
                            return False;
                    elif isinstance(aoArgMatches[i], str):
                        if not oArg.toString() != aoArgMatches[i]:
                            return False;
                    elif aoArgMatches[i] is int:
                        if not isinstance(oArg, ArmAstInteger):
                            return False;
                    elif aoArgMatches[i] is str:
                        if not isinstance(oArg, ArmAstIdentifier):
                            return False;
                    elif aoArgMatches[i] is not None:
                        raise Exception('Unexpected #%u: %s' % (i, aoArgMatches[i],));
                return True;
        return False;


class ArmAstIdentifier(ArmAstLeafBase):
    koReValidName        = re.compile('^[_A-Za-z][_A-Za-z0-9]*$');
    koReValidNameRelaxed = re.compile('^[_A-Za-z][_A-Za-z0-9<>]*$');
    kaoReValidName       = (koReValidName, koReValidNameRelaxed)

    def __init__(self, sName, fRelaxedName = False):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeIdentifier);
        assert self.kaoReValidName[fRelaxedName].match(sName), 'sName=%s' % (sName);
        self.sName = sName;

    def clone(self):
        return ArmAstIdentifier(self.sName);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstIdentifier):
            if self.sName == oOther.sName:
                return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return self.sName;

    def toCExpr(self, oHelper):
        (sCName, _) = oHelper.getFieldInfo(self.sName);
        return sCName;

    def getWidth(self, oHelper):
        (_, cBitsWidth) = oHelper.getFieldInfo(self.sName);
        return cBitsWidth;

    def isMatchingIdentifier(self, sName):
        return self.sName == sName;

    def getIdentifierName(self):
        return self.sName;


class ArmAstBool(ArmAstLeafBase):
    def __init__(self, fValue):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeBool);
        assert fValue is True or fValue is False, '%s' % (fValue,);
        self.fValue = fValue;

    def clone(self):
        return ArmAstBool(self.fValue);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstBase):
            if self.fValue == oOther.fValue:
                return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return 'true' if self.fValue is True else 'false';

    def toCExpr(self, oHelper):
        _ = oHelper;
        return 'true' if self.fValue is True else 'false';

    def getWidth(self, oHelper):
        _ = oHelper;
        return 1;

    def isBoolAndTrue(self):
        return self.fValue is True;

    def isBoolAndFalse(self):
        return self.fValue is False;


class ArmAstInteger(ArmAstLeafBase):
    def __init__(self, iValue, cBitsWidth = -1):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeInteger);
        self.iValue = int(iValue);
        self.cBitsWidth = cBitsWidth;
        assert cBitsWidth == -1 or (cBitsWidth > 0 and 0 <= iValue < (1 << cBitsWidth));

    def clone(self):
        return ArmAstInteger(self.iValue, self.cBitsWidth);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstInteger):
            if self.iValue == oOther.iValue:
                if self.cBitsWidth == oOther.cBitsWidth:
                    return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        if self.iValue < 10:
            return '%u' % (self.iValue,);
        return '%#x' % (self.iValue,);

    def toCExpr(self, oHelper):
        _ = oHelper;
        if self.iValue < 10:
            return '%u' % (self.iValue,);
        if self.iValue & (1<<31):
            return 'UINT32_C(%#x)' % (self.iValue,);
        return '%#x' % (self.iValue,);

    def getWidth(self, oHelper):
        _ = oHelper;
        if self.cBitsWidth > 0:
            return self.cBitsWidth;
        return self.iValue.bit_length() + (self.iValue < 0)

    def isMatchingInteger(self, iValue):
        return self.iValue == iValue;

    def getIntegerValue(self):
        return self.iValue;

    def isMatchingIntegerOrValue(self, iValue):
        return self.iValue == iValue;

    def getIntegerOrValue(self):
        return self.iValue;

    def getValueDetails(self):
        """
        For compatibility with ArmAstValue.
        Returns (fValue, fFixed, fWildcard, cBitsWidth) tuple on success.
        """
        cBitsWidth = self.getWidth(None);
        return (self.iValue, (1 << cBitsWidth) - 1, 0, cBitsWidth);


class ArmAstSet(ArmAstValuesBase):
    def __init__(self, aoValues):
        ArmAstValuesBase.__init__(self, ArmAstBase.ksTypeSet, aoValues);

    def clone(self):
        return ArmAstSet([oValue.clone() for oValue in self.aoValues]);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '(%s)' % (', '.join([oValue.toStringEx(sLang, cchMaxWidth) for oValue in self.aoValues]),);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('ArmAstSet does not support conversion to C expression: %s' % (self.toString()));

    def getWidth(self, oHelper):
        _ = oHelper;
        if self.aoValues:
            return max(oValue.getWidth() for oValue in self.aoValues);
        return -1;


class ArmAstValue(ArmAstLeafBase):
    def __init__(self, sValue):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeValue);
        assert isinstance(sValue, str);
        self.sValue = sValue;

    def clone(self):
        return ArmAstValue(self.sValue);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstValue):
            if self.sValue == oOther.sValue:
                return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return self.sValue;

    def toCExpr(self, oHelper):
        _ = oHelper;
        (fValue, _, fWildcard, _) = ArmAstValue.parseValue(self.sValue, 0);
        if fWildcard:
            raise Exception('Value contains wildcard elements: %s' % (self.sValue,));
        if fValue < 10:
            return '%u' % (fValue,);
        if fValue & (1<<31):
            return 'UINT32_C(%#x)' % (fValue,);
        return '%#x' % (fValue,);

    def getWidth(self, oHelper):
        _ = oHelper;
        (_, _, _, cBitsWidth) = ArmAstValue.parseValue(self.sValue, 0);
        return cBitsWidth;

    def isMatchingIntegerOrValue(self, iValue):
        (fValue, _, fWildcard, _) = ArmAstValue.parseValue(self.sValue, 0);
        return fValue == iValue and fWildcard == 0;

    def getIntegerOrValue(self):
        (fValue, _, fWildcard, _) = ArmAstValue.parseValue(self.sValue, 0);
        return fValue if fWildcard == 0 else None;

    def getValueDetails(self):
        """
        Returns (fValue, fFixed, fWildcard, cBitsWidth) tuple on success.
        """
        return self.parseValue(self.sValue, 0);

    @staticmethod
    def parseValue(sValue, cBitsWidth = 0):
        """
        Returns (fValue, fFixed, fWildcard, cBitsWidth) tuple on success, raises AssertionError otherwise.
        """
        assert sValue[0] == '\'' and sValue[-1] == '\'', sValue;
        sValue = sValue[1:-1];
        assert not cBitsWidth or len(sValue) == cBitsWidth, 'cBitsWidth=%s sValue=%s' % (cBitsWidth, sValue,);
        cBitsWidth = len(sValue);
        fFixed     = 0;
        fWildcard  = 0;
        fValue     = 0;
        for ch in sValue:
            assert ch in 'x10', 'ch=%s' % ch;
            fFixed    <<= 1;
            fValue    <<= 1;
            fWildcard <<= 1;
            if ch != 'x':
                fFixed |= 1;
                if ch == '1':
                    fValue |= 1;
            else:
                fWildcard |= 1;
        return (fValue, fFixed, fWildcard, cBitsWidth);


class ArmAstEquationValue(ArmAstLeafBase):
    koSimpleName = re.compile('^[_A-Za-z][_A-Za-z0-9]+$');

    def __init__(self, sValue, iFirstBit, cBitsWidth):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeValue);
        self.sValue     = sValue;
        self.iFirstBit  = iFirstBit;
        self.cBitsWidth = cBitsWidth;

    def clone(self):
        return ArmAstEquationValue(self.sValue, self.iFirstBit, self.cBitsWidth);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstEquationValue):
            if self.sValue == oOther.sValue:
                if self.iFirstBit == oOther.iFirstBit:
                    if self.cBitsWidth == oOther.cBitsWidth:
                        return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        if self.koSimpleName.match(self.sValue):
            return '%s[%u:%u]' % (self.sValue, self.iFirstBit, self.iFirstBit + self.cBitsWidth - 1,);
        return '(%s)[%u:%u]' % (self.sValue, self.iFirstBit, self.iFirstBit + self.cBitsWidth - 1,);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('todo');

    def getWidth(self, oHelper):
        _ = oHelper;
        return self.cBitsWidth;


class ArmAstValuesGroup(ArmAstBase):
    def __init__(self, sValue, aoValues = None):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeValue);
        self.sValue     = sValue;
        self.aoValues   = aoValues;
        if aoValues is None:
            ## @todo split up the value string. Optionally handle a specific 'values' list
            ## when present (looks like it's suppressed in the spec generator).
            self.aoValues = [];
            off = 0;
            while off < len(sValue):
                offStart = off;
                fSlice = False;
                while off < len(sValue) and (sValue[off] != ':' or fSlice):
                    if sValue[off] == '[':
                        assert not fSlice;
                        assert off > offStart;
                        fSlice = True;
                    elif sValue[off] == ']':
                        assert fSlice;
                        fSlice = False;
                    off += 1;
                sSubValue = sValue[offStart:off]
                assert off > offStart;
                assert not fSlice;

                if sSubValue[0] == '\'':
                    self.aoValues.append(ArmAstValue(sSubValue));
                elif '[' in sSubValue[0]:
                    assert sSubValue[-1] == ']';
                    offSlice = sSubValue.find('[');
                    asSlice = sSubValue[offSlice + 1:-1].split(':');
                    assert len(asSlice) == 2;
                    self.aoValues.append(ArmAstEquationValue(sSubValue[:offSlice], int(asSlice[0]),
                                                             int(asSlice[1]) - int(asSlice[0]) + 1));
                off += 1;

    def clone(self):
        return ArmAstValuesGroup(self.sValue, [oValue.clone() for oValue in self.aoValues]);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstValuesGroup):
            if self.sValue == oOther.sValue:
                assert len(self.aoValues) == len(oOther.aoValues);
                for iValue, oValue in enumerate(self.aoValues):
                    assert oValue.isSame(oOther.aoValues[iValue]);
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, *self.aoValues);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.aoValues = [oValue.transform(fnCallback, False, oCallbackArg, aoStack) for oValue in self.aoValues];
        for oValue in self.aoValues:
            assert oValue;
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        ## @todo deal width cchMaxWidth.
        return ':'.join([oValue.toStringEx(sLang, cchMaxWidth) for oValue in self.aoValues]);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('todo');

    def getWidth(self, oHelper):
        return sum(oValue.getWidth(oHelper) for oValue in self.aoValues);


class ArmAstString(ArmAstLeafBase):
    def __init__(self, sValue):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeValue);
        self.sValue = sValue;

    def clone(self):
        return ArmAstString(self.sValue);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstString):
            if self.sValue == oOther.sValue:
                return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return '"' + self.sValue + '"';

    def toCExpr(self, oHelper):
        _ = oHelper;
        return '"' + self.sValue.replace('\\', '\\\\').replace('"', '\\"') + '"';

    def getWidth(self, oHelper):
        _ = oHelper;
        return -1;


class ArmAstField(ArmAstLeafBase):
    def __init__(self, sField, sName, sState = 'AArch64', sSlices = None, sInstance = None):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeField);
        self.sField    = sField;
        self.sName     = sName;
        self.sState    = sState;
        self.sSlices   = sSlices;
        self.sInstance = sInstance;
        assert sSlices is None;
        assert sInstance is None;

    def clone(self):
        return ArmAstField(self.sField, self.sName, self.sState, self.sSlices, self.sInstance);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstField):
            if self.sField == oOther.sField:
                if self.sName == oOther.sName:
                    if self.sState == oOther.sState:
                        if self.sSlices == oOther.sSlices:
                            if self.sInstance == oOther.sInstance:
                                return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return '%s.%s.%s' % (self.sState, self.sName, self.sField,);

    def toCExpr(self, oHelper):
        (sCName, _) = oHelper.getFieldInfo(self.sField, self.sName, self.sState);
        return sCName;

    def getWidth(self, oHelper):
        (_, cBitsWidth) = oHelper.getFieldInfo(self.sField, self.sName, self.sState);
        return cBitsWidth;

    def isMatchingField(self, sField, sName, sState = 'AArch64'):
        return (    (   sField is None
                     or self.sField == sField)
                and self.sName  == sName
                and self.sState == sState
                and self.sSlices is None
                and self.sInstance is None);


class ArmAstRegisterType(ArmAstLeafBase):
    def __init__(self, sName, sState = 'AArch64', sSlices = None, sInstance = None):
        ArmAstLeafBase.__init__(self, ArmAstBase.ksTypeRegisterType);
        self.sName     = sName;
        self.sState    = sState;
        self.sSlices   = sSlices;
        self.sInstance = sInstance;
        assert sSlices is None;
        assert sInstance is None;

    def clone(self):
        return ArmAstRegisterType(self.sName, self.sState, self.sSlices, self.sInstance);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstRegisterType):
            if self.sName == oOther.sName:
                if self.sState == oOther.sState:
                    if self.sSlices == oOther.sSlices:
                        if self.sInstance == oOther.sInstance:
                            return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return '%s.%s' % (self.sState, self.sName,);

    def toCExpr(self, oHelper):
        #(sCName, _) = oHelper.getFieldInfo(None, self.sName, self.sState);
        #return sCName;
        raise Exception('not implemented');

    def getWidth(self, oHelper):
        #(_, cBitsWidth) = oHelper.getFieldInfo(None, self.sName, self.sState);
        #return cBitsWidth;
        _ = oHelper;
        return -1;


class ArmAstType(ArmAstBase):
    def __init__(self, oName):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeType);
        self.oName = oName;

    def clone(self):
        return ArmAstType(self.oName.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstType):
            if self.oName.isSame(oOther.oName):
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oName);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.oName = self.oName.transform(fnCallback, False, oCallbackArg, aoStack);
        assert self.oName;
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return self.oName.toStringEx(sLang, cchMaxWidth);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');

    def getWidth(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');


class ArmAstTypeAnnotation(ArmAstBase):
    def __init__(self, oVar, oType):
        ArmAstBase.__init__(self, ArmAstBase.ksTypeTypeAnnotation);
        self.oVar  = oVar;
        self.oType = oType;

    def clone(self):
        return ArmAstTypeAnnotation(self.oVar.clone(), self.oType.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstType):
            if self.oVar.isSame(oOther.oVar):
                if self.oType.isSame(oOther.oType):
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oVar, self.oType);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.oVar = self.oVar.transform(fnCallback, False, oCallbackArg, aoStack);
        assert self.oVar;
        self.oType = self.oType.transform(fnCallback, False, oCallbackArg, aoStack);
        assert self.oType;
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '(%s) %s' % (self.oType.toStringEx(sLang, cchMaxWidth), self.oVar.toStringEx(sLang, cchMaxWidth),);

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');

    def getWidth(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');


#
# Pure statement AST nodes.
#

class ArmAstStatementBase(ArmAstBase):
    """
    Base class for statements.

    This adds the toStringList method and blocks the toCExpr and getWidth methods.
    """
    def __init__(self, sType):
        ArmAstBase.__init__(self, sType);

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        return '\n'.join(self.toStringList(sLang = sLang, cchMaxWidth = cchMaxWidth));

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        _ = sIndent; _ = sLang; _ = cchMaxWidth;
        raise Exception('child class must implement toStringList!');

    def toCExpr(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');

    def getWidth(self, oHelper):
        _ = oHelper;
        raise Exception('not implemented');

    def isNop(self):
        """ Checks if this is a NOP statement. """
        return isinstance(self, ArmAstNop);

    def doAllPathsReturn(self):
        """ Checks if all code paths ends with a return statement. """
        return False;


class ArmAstStatementList(ArmAstStatementBase):
    """
    List of statements.
    """
    def __init__(self, aoStmts):
        ArmAstStatementBase.__init__(self, 'Statement List');
        self.aoStmts = aoStmts;

    def clone(self):
        return ArmAstStatementList([oStmt.clone() for oStmt in self.aoStmts]);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstStatementList):
            if len(self.aoStmts) == len(oOther.aoStmts):
                for i, oStmt in enumerate(self.aoStmts):
                    if not oStmt.isSame(oOther.aoStmts[i]):
                        return False;
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, *self.aoStmts);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        idx = 0;
        while idx < len(self.aoStmts):
            oStmt = self.aoStmts[idx].transform(fnCallback, True, oCallbackArg, aoStack);
            if oStmt:
                self.aoStmts[idx] = oStmt;
                idx += 1;
            else:
                del self.aoStmts[idx];
        ## @todo remove anything but comments following a return statement...
        aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        asLines = [];
        for oStmt in self.aoStmts:
            asLines += oStmt.toStringList(sIndent, sLang, cchMaxWidth);
        return asLines;

    def doAllPathsReturn(self):
        if self.aoStmts:
            for oStmt in reversed(self.aoStmts):
                if isinstance(oStmt, ArmAstStatementBase) and oStmt.doAllPathsReturn():
                    return True;
        return False;


class ArmAstNop(ArmAstStatementBase):
    """
    NOP statement.
    Not part of ARM spec. We need it for transformations.
    """
    def __init__(self):
        ArmAstStatementBase.__init__(self, 'AST.Nop');

    def clone(self):
        return ArmAstNop();

    def isSame(self, oOther):
        return isinstance(oOther, ArmAstNop);

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return [ 'NOP();', ];

    def isLeaf(self):
        return True;


class ArmAstAssignment(ArmAstStatementBase):
    """ We classify assignments as statements. """

    def __init__(self, oVar, oValue):
        ArmAstStatementBase.__init__(self, ArmAstBase.ksTypeAssignment);
        self.oVar      = oVar;
        self.oValue    = oValue;

    def clone(self):
        return ArmAstAssignment(self.oVar.clone(), self.oValue.clone());

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstAssignment):
            if self.oVar.isSame(oOther.oVar):
                if self.oValue.isSame(oOther.oValue):
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oVar, self.oValue);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoStack.append(self);
        self.oVar = self.oVar.transform(fnCallback, fEliminationAllowed, oCallbackArg, aoStack);
        if self.oVar:
            self.oValue = self.oValue.transform(fnCallback, False, oCallbackArg, aoStack);
            assert self.oValue;
            aoStack.pop();
            return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);
        aoStack.pop();
        assert fEliminationAllowed;
        return None;

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        sVar   = self.oVar.toStringEx(sLang, cchMaxWidth);
        cchVar = len(sVar);
        if '\n' in sVar:
            sVar += '\n   ' + sIndent;
            cchVar = 3;
        sValue = self.oValue.toStringEx(sLang, max(cchMaxWidth - cchVar, 60));
        if '\n' in sValue:
            sValue = '(' + sValue.replace('\n', '\n' + ' ' * (cchVar + 4) + sIndent) + ' )';
        return ('%s%s = %s;' % (sIndent, sVar, sValue)).split('\n');


class ArmAstReturn(ArmAstStatementBase):
    """ We classify assignments as statements. """

    def __init__(self, oValue):
        ArmAstStatementBase.__init__(self, ArmAstBase.ksTypeReturn);
        self.oValue = oValue;

    def clone(self):
        return ArmAstReturn(self.oValue.clone() if self.oValue else None);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstReturn):
            if self.oValue and oOther.oValue:
                if self.oValue.isSame(oOther.oValue):
                    return True;
            elif not self.oValue and not oOther.oValue:
                return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        if self.oValue:
            return self._walker(fnCallback, oCallbackArg, fDepthFirst, self.oValue);
        return self._walker(fnCallback, oCallbackArg, fDepthFirst);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        if self.oValue:
            aoStack.append(self);
            self.oValue = self.oValue.transform(fnCallback, False, oCallbackArg, aoStack);
            assert self.oValue;
            aoStack.pop();
        return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        if self.oValue:
            sValue = self.oValue.toStringEx(sLang, max(cchMaxWidth - 7, 60));
            if '\n' not in sValue:
                return [ '%sreturn %s;' % (sIndent, sValue,) ];
            return ('%sreturn %s;' % (sIndent, sValue.replace('\n', '\n' + '       ' + sIndent),)).split('\n');
        return [ '%sreturn;' % (sIndent,) ];

    def doAllPathsReturn(self):
        return True;


class ArmAstIfList(ArmAstStatementBase):
    """
    Accessors.Permission.SystemAccess

    We make this part of the AST.

    A series of conditional actions or nested conditional series:
        if cond1:
            action1;
        elif cond2:
            if cond2a:  # nested series
                action2a;
            else:
                action2b;
        else:
            action3;
    """

    def __init__(self, aoIfConditions, aoIfStatements, oElseStatement):
        ArmAstStatementBase.__init__(self, 'Accessors.Permission.MemoryAccess');
        # The if/elif condition expressions.
        self.aoIfConditions  = aoIfConditions   # type: List[ArmAstBase]
        # The if/elif statements, runs in parallel to aoIfConditions. ArmAstIfList allowed.
        self.aoIfStatements  = aoIfStatements   # type: List[ArmAstBase]
        # The else statement - optional.  ArmAstIfList allowed.
        self.oElseStatement  = oElseStatement   # type: ArmAstBase

        # Assert sanity.
        assert len(aoIfConditions) == len(aoIfStatements);
        assert aoIfStatements or oElseStatement;  # Pretty lax at the moment.
        for oStmt in list(aoIfStatements):
            assert isinstance(oStmt, (ArmAstStatementBase, ArmAstFunction));
        assert oElseStatement is None or isinstance(oElseStatement, (ArmAstStatementBase, ArmAstFunction));

    def clone(self):
        return ArmAstIfList([oIfCond.clone() for oIfCond in self.aoIfConditions],
                            [oIfStmt.clone() for oIfStmt in self.aoIfStatements],
                            self.oElseStatement.clone() if self.oElseStatement else None);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstIfList):
            if len(self.aoIfConditions) == len(oOther.aoIfConditions):
                for i, oIfCond in enumerate(self.aoIfConditions):
                    if not oIfCond.isSame(oOther.aoIfConditions[i]):
                        return False;
                    if not self.aoIfStatements[i].isSame(oOther.aoIfStatements[i]):
                        return False;
                if (self.oElseStatement is None) == (oOther.oElseStatement is None):
                    if self.oElseStatement and not self.oElseStatement.isSame(oOther.oElseStatement):
                        return False;
                    return True;
        return False;

    def walk(self, fnCallback, oCallbackArg = None, fDepthFirst = True):
        aoChildren = [];
        for idxIf, oIfCond in enumerate(self.aoIfConditions):
            aoChildren.extend([oIfCond, self.aoIfStatements[idxIf]]);
        if self.oElseStatement:
            aoChildren.append(self.oElseStatement);
        return self._walker(fnCallback, oCallbackArg, fDepthFirst, *aoChildren);

    def transform(self, fnCallback, fEliminationAllowed, oCallbackArg, aoStack):
        aoNewIfConditions = [];
        aoNewIfStatements = [];
        oNewElseStatement = None;
        aoStack.append(self);
        for idxIf, oIfCond in enumerate(self.aoIfConditions):
            oIfCond = oIfCond.transform(fnCallback, True, oCallbackArg, aoStack);
            if oIfCond and not oIfCond.isBoolAndFalse():
                oIfStmt = self.aoIfStatements[idxIf].transform(fnCallback, True, oCallbackArg, aoStack);
                assert oIfStmt;
                if oIfCond.isBoolAndTrue():
                    oNewElseStatement = oIfStmt;
                    break;
                aoNewIfConditions.append(oIfCond);
                aoNewIfStatements.append(oIfStmt);
        if not oNewElseStatement and self.oElseStatement:
            oNewElseStatement = self.oElseStatement.transform(fnCallback, True, oCallbackArg, aoStack);
        aoStack.pop();

        if aoNewIfConditions:
            self.aoIfConditions = aoNewIfConditions;
            self.aoIfStatements = aoNewIfStatements;
            self.oElseStatement = oNewElseStatement;
            return fnCallback(self, fEliminationAllowed, oCallbackArg, aoStack);
        if oNewElseStatement:
            return fnCallback(oNewElseStatement, fEliminationAllowed, oCallbackArg, aoStack);
        if fEliminationAllowed:
            return None;
        return fnCallback(ArmAstNop(), fEliminationAllowed, oCallbackArg, aoStack);

    def toStringList(self, sIndent = '', sLang = None, cchMaxWidth = 120):
        asLines         = [];
        sNextIndent     = sIndent + '    ';
        cchMaxWidth     = max(cchMaxWidth,     60);
        cchNextMaxWidth = max(cchMaxWidth - 4, 60);
        fNeedElse       = False;
        for i, oIfCond in enumerate(self.aoIfConditions):
            sIfCond = oIfCond.toStringEx(sLang, cchMaxWidth);
            if '\n' in sIfCond:
                sIfCond = sIfCond.replace('\n', '\n         ' + sIndent if fNeedElse else '\n    ' + sIndent);
            asLines.extend(('%s%sif (%s)' % (sIndent, 'else ' if fNeedElse else '', sIfCond,)).split('\n'));

            oIfStmt = self.aoIfStatements[i];
            if isinstance(oIfStmt, ArmAstStatementBase):
                asStmts = oIfStmt.toStringList(sNextIndent, sLang, cchNextMaxWidth);
                if sLang == 'C' and isinstance(oIfStmt, (ArmAstStatementList, ArmAstIfList)):
                    asLines.append(sIndent + '{');
                    asLines.extend(asStmts);
                    asLines.append(sIndent + '}');
                else:
                    asLines.extend(asStmts);
            else:
                sTmp = sNextIndent + oIfStmt.toStringEx(sLang, cchNextMaxWidth);
                asLines.extend(sTmp.replace('\n', '\n' + sNextIndent).split('\n'));
            fNeedElse = fNeedElse or (not isinstance(oIfStmt, ArmAstStatementBase) or not oIfStmt.doAllPathsReturn());

        if self.oElseStatement:
            if self.aoIfConditions and fNeedElse:
                asLines.append(sIndent + 'else');
            else:
                sNextIndent     = sIndent;     # Trick.
                cchNextMaxWidth = cchMaxWidth; # Trick.
            if isinstance(self.oElseStatement, ArmAstStatementBase):
                asStmts = self.oElseStatement.toStringList(sNextIndent, sLang, cchNextMaxWidth);
                if sLang == 'C' and isinstance(self.oElseStatement, (ArmAstStatementList, ArmAstIfList)) and fNeedElse:
                    asLines.append(sIndent + '{');
                    asLines.extend(asStmts);
                    asLines.append(sIndent + '}');
                else:
                    asLines.extend(asStmts);
            else:
                sTmp = sNextIndent + self.oElseStatement.toStringEx(sLang, cchNextMaxWidth);
                asLines.extend(sTmp.replace('\n', '\n' + sNextIndent).split('\n'));
        return asLines;

    def doAllPathsReturn(self):
        # There must be an else statement and it must return.
        if (   not self.oElseStatement
            or not isinstance(self.oElseStatement, ArmAstStatementBase)
            or not self.oElseStatement.doAllPathsReturn()):
            return False;

        # The else-block is good, now check each of the if-blocks.
        for oStmt in self.aoIfStatements:
            if (   not isinstance(oStmt, ArmAstStatementBase)
                or not oStmt.doAllPathsReturn()):
                return False;
        return True;


    khAttribSet = frozenset(['_type', 'access', 'condition',]);
    @staticmethod
    def fromJson(dJson, uDepth = 0): # pylint: disable=arguments-renamed
        #
        # There are two variants of this object.
        #
        if dJson['_type'] != 'Accessors.Permission.SystemAccess': raise Exception('wrong type: %s' % (dJson['_type'],));
        assertJsonAttribsInSet(dJson, ArmAstIfList.khAttribSet);

        oCondition = ArmAstBase.fromJson(dJson['condition'], ArmAstBase.ksModeAccessorCond);

        #
        # 1. 'access' is an AST: This is one if/else + action without nesting.
        #
        if not isinstance(dJson['access'], list):
            oStmt = ArmAstBase.fromJson(dJson['access'], ArmAstBase.ksModeAccessor);
            if oCondition.isBoolAndTrue():
                assert isinstance(oStmt, (ArmAstStatementBase, ArmAstFunction)); ## @todo may need a wrapper.
                #print('debug/IfList/%u:%s 1a. oStmt=%s' % (uDepth, ' '*uDepth, oStmt,));
                return oStmt;
            oRet = ArmAstIfList([oCondition], [oStmt], None);
            #print('debug/IfList/%u:%s 1b. oRet=\n%s' % (uDepth, ' '*uDepth, oRet,));
            return oRet;

        #
        # 2. 'access' is a list of the same type: Typically nested if-list.
        #
        #    This is more complicated because of the nesting and different ways
        #    of expressing the same stuff.  We make it even more complicated by
        #    not mirroring the json representation 1:1.
        #
        aoChildren = [ArmAstIfList.fromJson(dJsonChild, uDepth + 1) for dJsonChild in dJson['access']];
        assert aoChildren;

        # Iff there is only one child, we need to check for some special cases.
        if len(aoChildren) == 1:
            oChild = aoChildren[0];
            if not isinstance(oChild, ArmAstIfList):
                if oCondition.isBoolAndTrue():
                    #print('debug/IfList/%u:%s 2a. oChild=%s' % (uDepth, ' '*uDepth, oChild,));
                    return oChild;
                #print('debug/IfList/%u:%s 2b. oCondition=%s oChild=%s' % (uDepth, ' '*uDepth, oCondition, oChild,));
                return ArmAstIfList([oCondition], aoChildren, None);

            # If our condition is a dummy one, return the child.
            if oCondition.isBoolAndTrue():
                #print('debug/IfList/%u:%s 2c. oChild=%s' % (uDepth, ' '*uDepth, oChild,));
                return oChild;
            assert oChild.aoIfConditions;

        # Generic.
        #print('debug/IfList/%u:%s 2d. #aoChildren=%s' % (uDepth, ' '*uDepth, len(aoChildren),));
        aoIfConds = [];
        aoIfStmts = [];
        oElseStmt = None;
        for i, oChild in enumerate(aoChildren):
            if isinstance(oChild, ArmAstIfList):
                assert not oChild.oElseStatement or i + 1 == len(aoChildren), \
                       'i=%s/%u\noIfConditions=%s\noElseStmts=%s\naoChildren=[\n%s]' \
                        % (i, len(aoChildren), oChild.aoIfConditions, oChild.oElseStatement,
                           '\n'.join(['  #%u: %s' % (j, o.toString()) for j, o in enumerate(aoChildren)])
                           );
                aoIfConds.extend(oChild.aoIfConditions);
                aoIfStmts.extend(oChild.aoIfStatements);
                if oChild.oElseStatement:
                    assert i == len(aoChildren) - 1 and not oElseStmt;
                    oElseStmt = oChild.oElseStatement;
                    #print('debug/IfList/%u:%s 2e. i=%u oChild=%s' % (uDepth, ' '*uDepth, i, oChild,));
                #else: print('debug/IfList/%u:%s 2f. i=%u oChild=%s' % (uDepth, ' '*uDepth, i, oChild,));
            else:
                #print('debug/IfList/%u:%s 2g. i=%u oChild=%s' % (uDepth, ' '*uDepth, i, oChild,));
                assert i == len(aoChildren) - 1 and not oElseStmt;
                oElseStmt = oChild;
        #print('debug/IfList/%u:%s 2x. oElseStmt=%s' % (uDepth, ' '*uDepth, oElseStmt));

        # Twist: Eliminate this level if the current condition is just 'true'.
        if oCondition.isBoolAndTrue():
            oRet = ArmAstIfList(aoIfConds, aoIfStmts, oElseStmt);
            #print('debug/IfList/%u:%s 2y. oRet=\n%s\n' % (uDepth, ' '*uDepth, oRet));
        else:
            oRet = ArmAstIfList([oCondition], [ArmAstIfList(aoIfConds, aoIfStmts, oElseStmt)], None);
            #print('debug/IfList/%u:%s 2z. oRet=\n%s\n' % (uDepth, ' '*uDepth, oRet));
        return oRet;


#
# Some quick C++ AST nodes.
#

class ArmAstCppExprBase(object):
    """ C++ AST expression base class. Mainly for isinstance() purposes. """
    def __init__(self):
        self.fIsCpp = True;


class ArmAstCppExpr(ArmAstLeafBase, ArmAstCppExprBase):
    """ C++ AST node. """
    def __init__(self, sExpr, cBitsWidth = -1):
        ArmAstLeafBase.__init__(self, 'C++ Expression');
        ArmAstCppExprBase.__init__(self);
        self.sExpr      = sExpr;
        self.cBitsWidth = cBitsWidth;

    def clone(self):
        return ArmAstCppExpr(self.sExpr, self.cBitsWidth);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstCppExpr):
            if self.sExpr == oOther.sExpr:
                if self.cBitsWidth == oOther.cBitsWidth:
                    return True;
        return False;

    def toStringEx(self, sLang = None, cchMaxWidth = 120):
        _ = sLang; _ = cchMaxWidth;
        return self.sExpr;

    def toCExpr(self, oHelper):
        _ = oHelper;
        return self.sExpr;

    def getWidth(self, oHelper):
        _ = oHelper;
        return self.cBitsWidth;


class ArmAstCppCall(ArmAstFunctionCallBase, ArmAstCppExprBase):
    """
    C++ AST function call node.
    """
    def __init__(self, sName, aoArgs, cBitsWidth = 32):
        ArmAstFunctionCallBase.__init__(self, 'C++ function call', sName, aoArgs);
        ArmAstCppExprBase.__init__(self);
        self.cBitsWidth = cBitsWidth; # the return value width. 0 for void.

    def clone(self):
        return ArmAstCppCall(self.sName, [oArg.clone() for oArg in self.aoArgs], self.cBitsWidth);

    def isSame(self, oOther):
        if isinstance(oOther, ArmAstCppCall):
            if self.cBitsWidth == oOther.cBitsWidth:
                return ArmAstFunctionCallBase.isSame(self, oOther);
        return False;

    def toCExpr(self, oHelper):
        _ = oHelper;
        return self.toStringEx(sLang = 'C');

    def getWidth(self, oHelper):
        _ = oHelper;
        return self.cBitsWidth;

