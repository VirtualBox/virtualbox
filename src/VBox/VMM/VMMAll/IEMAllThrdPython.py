#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: IEMAllThrdPython.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=invalid-name

"""
Annotates and generates threaded functions from IEMAllInst*.cpp.h.
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

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision: 106320 $"

# Standard python imports.
import copy;
import datetime;
import os;
import re;
import sys;
import argparse;
from typing import Dict, List;

import IEMAllInstPython as iai;
import IEMAllN8vePython as ian;


# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name

## Number of generic parameters for the thread functions.
g_kcThreadedParams = 3;

g_kdTypeInfo = {
    # type name:        (cBits, fSigned, C-type      )
    'int8_t':           (    8,    True, 'int8_t',   ),
    'int16_t':          (   16,    True, 'int16_t',  ),
    'int32_t':          (   32,    True, 'int32_t',  ),
    'int64_t':          (   64,    True, 'int64_t',  ),
    'uint4_t':          (    4,   False, 'uint8_t',  ),
    'uint8_t':          (    8,   False, 'uint8_t',  ),
    'uint16_t':         (   16,   False, 'uint16_t', ),
    'uint32_t':         (   32,   False, 'uint32_t', ),
    'uint64_t':         (   64,   False, 'uint64_t', ),
    'uintptr_t':        (   64,   False, 'uintptr_t',), # ASSUMES 64-bit host pointer size.
    'bool':             (    1,   False, 'bool',     ),
    'IEMMODE':          (    2,   False, 'IEMMODE',  ),
};

# Only for getTypeBitCount/variables.
g_kdTypeInfo2 = {
    'RTFLOAT32U':       (   32,   False, 'RTFLOAT32U',      ),
    'RTFLOAT64U':       (   64,   False, 'RTFLOAT64U',      ),
    'RTUINT64U':        (   64,   False, 'RTUINT64U',       ),
    'RTGCPTR':          (   64,   False, 'RTGCPTR',         ),
    'RTPBCD80U':        (   80,   False, 'RTPBCD80U',       ),
    'RTFLOAT80U':       (   80,   False, 'RTFLOAT80U',      ),
    'IEMFPURESULT':     (80+16,   False, 'IEMFPURESULT',    ),
    'IEMFPURESULTTWO':  (80+16+80,False, 'IEMFPURESULTTWO', ),
    'RTUINT128U':       (  128,   False, 'RTUINT128U',      ),
    'X86XMMREG':        (  128,   False, 'X86XMMREG',       ),
    'X86YMMREG':        (  256,   False, 'X86YMMREG',       ),
    'IEMMEDIAF2XMMSRC': (  256,   False, 'IEMMEDIAF2XMMSRC',),
    'IEMMEDIAF2YMMSRC': (  512,   False, 'IEMMEDIAF2YMMSRC',),
    'RTUINT256U':       (  256,   False, 'RTUINT256U',      ),
    'IEMPCMPISTRXSRC':  (  256,   False, 'IEMPCMPISTRXSRC', ),
    'IEMPCMPESTRXSRC':  (  384,   False, 'IEMPCMPESTRXSRC', ),
}; #| g_kdTypeInfo; - requires 3.9
g_kdTypeInfo2.update(g_kdTypeInfo);

def getTypeBitCount(sType):
    """
    Translate a type to size in bits
    """
    if sType in g_kdTypeInfo2:
        return g_kdTypeInfo2[sType][0];
    if '*' in sType or sType[0] == 'P':
        return 64;
    #raise Exception('Unknown type: %s' % (sType,));
    print('error: Unknown type: %s' % (sType,));
    return 64;

g_kdIemFieldToType = {
    # Illegal ones:
    'offInstrNextByte':     ( None, ),
    'cbInstrBuf':           ( None, ),
    'pbInstrBuf':           ( None, ),
    'uInstrBufPc':          ( None, ),
    'cbInstrBufTotal':      ( None, ),
    'offCurInstrStart':     ( None, ),
    'cbOpcode':             ( None, ),
    'offOpcode':            ( None, ),
    'offModRm':             ( None, ),
    # Okay ones.
    'fPrefixes':            ( 'uint32_t', ),
    'uRexReg':              ( 'uint8_t', ),
    'uRexB':                ( 'uint8_t', ),
    'uRexIndex':            ( 'uint8_t', ),
    'iEffSeg':              ( 'uint8_t', ),
    'enmEffOpSize':         ( 'IEMMODE', ),
    'enmDefAddrMode':       ( 'IEMMODE', ),
    'enmEffAddrMode':       ( 'IEMMODE', ),
    'enmDefOpSize':         ( 'IEMMODE', ),
    'idxPrefix':            ( 'uint8_t', ),
    'uVex3rdReg':           ( 'uint8_t', ),
    'uVexLength':           ( 'uint8_t', ),
    'fEvexStuff':           ( 'uint8_t', ),
    'uFpuOpcode':           ( 'uint16_t', ),
};

## @name McStmtCond.oIfBranchAnnotation/McStmtCond.oElseBranchAnnotation values
## @{
g_ksFinishAnnotation_Advance            = 'Advance';
g_ksFinishAnnotation_RelJmp             = 'RelJmp';
g_ksFinishAnnotation_SetJmp             = 'SetJmp';
g_ksFinishAnnotation_RelCall            = 'RelCall';
g_ksFinishAnnotation_IndCall            = 'IndCall';
g_ksFinishAnnotation_DeferToCImpl       = 'DeferToCImpl';
## @}


class ThreadedParamRef(object):
    """
    A parameter reference for a threaded function.
    """

    def __init__(self, sOrgRef, sType, oStmt, iParam = None, offParam = 0, sStdRef = None):
        ## The name / reference in the original code.
        self.sOrgRef    = sOrgRef;
        ## Normalized name to deal with spaces in macro invocations and such.
        self.sStdRef    = sStdRef if sStdRef else ''.join(sOrgRef.split());
        ## Indicates that sOrgRef may not match the parameter.
        self.fCustomRef = sStdRef is not None;
        ## The type (typically derived).
        self.sType      = sType;
        ## The statement making the reference.
        self.oStmt      = oStmt;
        ## The parameter containing the references. None if implicit.
        self.iParam     = iParam;
        ## The offset in the parameter of the reference.
        self.offParam   = offParam;

        ## The variable name in the threaded function.
        self.sNewName     = 'x';
        ## The this is packed into.
        self.iNewParam    = 99;
        ## The bit offset in iNewParam.
        self.offNewParam  = 1024


class ThreadedFunctionVariation(object):
    """ Threaded function variation. """

    ## @name Variations.
    ## These variations will match translation block selection/distinctions as well.
    ## @{
    # pylint: disable=line-too-long
    ksVariation_Default           = '';                  ##< No variations - only used by IEM_MC_DEFER_TO_CIMPL_X_RET.
    ksVariation_16                = '_16';               ##< 16-bit mode code (386+).
    ksVariation_16f               = '_16f';              ##< 16-bit mode code (386+), check+clear eflags.
    ksVariation_16_Jmp            = '_16_Jmp';           ##< 16-bit mode code (386+), conditional jump taken.
    ksVariation_16f_Jmp           = '_16f_Jmp';          ##< 16-bit mode code (386+), check+clear eflags, conditional jump taken.
    ksVariation_16_NoJmp          = '_16_NoJmp';         ##< 16-bit mode code (386+), conditional jump not taken.
    ksVariation_16f_NoJmp         = '_16f_NoJmp';        ##< 16-bit mode code (386+), check+clear eflags, conditional jump not taken.
    ksVariation_16_Addr32         = '_16_Addr32';        ##< 16-bit mode code (386+), address size prefixed to 32-bit addressing.
    ksVariation_16f_Addr32        = '_16f_Addr32';       ##< 16-bit mode code (386+), address size prefixed to 32-bit addressing, eflags.
    ksVariation_16_Pre386         = '_16_Pre386';        ##< 16-bit mode code, pre-386 CPU target.
    ksVariation_16f_Pre386        = '_16f_Pre386';       ##< 16-bit mode code, pre-386 CPU target, check+clear eflags.
    ksVariation_16_Pre386_Jmp     = '_16_Pre386_Jmp';    ##< 16-bit mode code, pre-386 CPU target, conditional jump taken.
    ksVariation_16f_Pre386_Jmp    = '_16f_Pre386_Jmp';   ##< 16-bit mode code, pre-386 CPU target, check+clear eflags, conditional jump taken.
    ksVariation_16_Pre386_NoJmp   = '_16_Pre386_NoJmp';  ##< 16-bit mode code, pre-386 CPU target, conditional jump not taken.
    ksVariation_16f_Pre386_NoJmp  = '_16f_Pre386_NoJmp'; ##< 16-bit mode code, pre-386 CPU target, check+clear eflags, conditional jump not taken.
    ksVariation_32                = '_32';               ##< 32-bit mode code (386+).
    ksVariation_32f               = '_32f';              ##< 32-bit mode code (386+), check+clear eflags.
    ksVariation_32_Jmp            = '_32_Jmp';           ##< 32-bit mode code (386+), conditional jump taken.
    ksVariation_32f_Jmp           = '_32f_Jmp';          ##< 32-bit mode code (386+), check+clear eflags, conditional jump taken.
    ksVariation_32_NoJmp          = '_32_NoJmp';         ##< 32-bit mode code (386+), conditional jump not taken.
    ksVariation_32f_NoJmp         = '_32f_NoJmp';        ##< 32-bit mode code (386+), check+clear eflags, conditional jump not taken.
    ksVariation_32_Flat_Jmp       = '_32_Flat_Jmp';      ##< 32-bit mode code (386+) with flat CS, SS, DS and ES, conditional jump taken.
    ksVariation_32f_Flat_Jmp      = '_32f_Flat_Jmp';     ##< 32-bit mode code (386+) with flat CS, SS, DS and ES, check+clear eflags, conditional jump taken.
    ksVariation_32_Flat_NoJmp     = '_32_Flat_NoJmp';    ##< 32-bit mode code (386+) with flat CS, SS, DS and ES, conditional jump not taken.
    ksVariation_32f_Flat_NoJmp    = '_32f_Flat_NoJmp';   ##< 32-bit mode code (386+) with flat CS, SS, DS and ES, check+clear eflags, conditional jump not taken.
    ksVariation_32_Flat           = '_32_Flat';          ##< 32-bit mode code (386+) with CS, DS, ES and SS flat and 4GB wide.
    ksVariation_32f_Flat          = '_32f_Flat';         ##< 32-bit mode code (386+) with CS, DS, ES and SS flat and 4GB wide, eflags.
    ksVariation_32_Addr16         = '_32_Addr16';        ##< 32-bit mode code (386+), address size prefixed to 16-bit addressing.
    ksVariation_32f_Addr16        = '_32f_Addr16';       ##< 32-bit mode code (386+), address size prefixed to 16-bit addressing, eflags.
    ksVariation_64                = '_64';               ##< 64-bit mode code.
    ksVariation_64f               = '_64f';              ##< 64-bit mode code, check+clear eflags.
    ksVariation_64_Jmp            = '_64_Jmp';           ##< 64-bit mode code, conditional jump taken.
    ksVariation_64f_Jmp           = '_64f_Jmp';          ##< 64-bit mode code, check+clear eflags, conditional jump taken.
    ksVariation_64_NoJmp          = '_64_NoJmp';         ##< 64-bit mode code, conditional jump not taken.
    ksVariation_64f_NoJmp         = '_64f_NoJmp';        ##< 64-bit mode code, check+clear eflags, conditional jump within page not taken.
    ksVariation_64_SamePg_Jmp     = '_64_SamePg_Jmp';    ##< 64-bit mode code, conditional jump within page taken.
    ksVariation_64f_SamePg_Jmp    = '_64f_SamePg_Jmp';   ##< 64-bit mode code, check+clear eflags, conditional jump taken.
    ksVariation_64_SamePg_NoJmp   = '_64_SamePg_NoJmp';  ##< 64-bit mode code, conditional jump within page not taken.
    ksVariation_64f_SamePg_NoJmp  = '_64f_SamePg_NoJmp'; ##< 64-bit mode code, check+clear eflags, conditional jump within page not taken.
    ksVariation_64_FsGs           = '_64_FsGs';          ##< 64-bit mode code, with memory accesses via FS or GS.
    ksVariation_64f_FsGs          = '_64f_FsGs';         ##< 64-bit mode code, with memory accesses via FS or GS, check+clear eflags.
    ksVariation_64_Addr32         = '_64_Addr32';        ##< 64-bit mode code, address size prefixed to 32-bit addressing.
    ksVariation_64f_Addr32        = '_64f_Addr32';       ##< 64-bit mode code, address size prefixed to 32-bit addressing, c+c eflags.
    # pylint: enable=line-too-long
    kasVariations           = (
        ksVariation_Default,
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Jmp,
        ksVariation_16f_Jmp,
        ksVariation_16_NoJmp,
        ksVariation_16f_NoJmp,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_16_Pre386_Jmp,
        ksVariation_16f_Pre386_Jmp,
        ksVariation_16_Pre386_NoJmp,
        ksVariation_16f_Pre386_NoJmp,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Jmp,
        ksVariation_32f_Jmp,
        ksVariation_32_NoJmp,
        ksVariation_32f_NoJmp,
        ksVariation_32_Flat_Jmp,
        ksVariation_32f_Flat_Jmp,
        ksVariation_32_Flat_NoJmp,
        ksVariation_32f_Flat_NoJmp,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
        ksVariation_64,
        ksVariation_64f,
        ksVariation_64_Jmp,
        ksVariation_64f_Jmp,
        ksVariation_64_NoJmp,
        ksVariation_64f_NoJmp,
        ksVariation_64_SamePg_Jmp,
        ksVariation_64f_SamePg_Jmp,
        ksVariation_64_SamePg_NoJmp,
        ksVariation_64f_SamePg_NoJmp,
        ksVariation_64_FsGs,
        ksVariation_64f_FsGs,
        ksVariation_64_Addr32,
        ksVariation_64f_Addr32,
    );
    kasVariationsWithoutAddress = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_64,
        ksVariation_64f,
    );
    kasVariationsWithoutAddressNot286 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_64,
        ksVariation_64f,
    );
    kasVariationsWithoutAddressNot286Not64 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_32,
        ksVariation_32f,
    );
    kasVariationsWithoutAddressNot64 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_32,
        ksVariation_32f,
    );
    kasVariationsWithoutAddressOnly64 = (
        ksVariation_64,
        ksVariation_64f,
    );
    kasVariationsWithAddress = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
        ksVariation_64,
        ksVariation_64f,
        ksVariation_64_FsGs,
        ksVariation_64f_FsGs,
        ksVariation_64_Addr32,
        ksVariation_64f_Addr32,
    );
    kasVariationsWithAddressNot286 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
        ksVariation_64,
        ksVariation_64f,
        ksVariation_64_FsGs,
        ksVariation_64f_FsGs,
        ksVariation_64_Addr32,
        ksVariation_64f_Addr32,
    );
    kasVariationsWithAddressNot286Not64 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
    );
    kasVariationsWithAddressNot64 = (
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
    );
    kasVariationsWithAddressOnly64 = (
        ksVariation_64,
        ksVariation_64f,
        ksVariation_64_FsGs,
        ksVariation_64f_FsGs,
        ksVariation_64_Addr32,
        ksVariation_64f_Addr32,
    );
    kasVariationsOnlyPre386 = (
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
    );
    kasVariationsEmitOrder = (
        ksVariation_Default,
        ksVariation_64,
        ksVariation_64f,
        ksVariation_64_Jmp,
        ksVariation_64f_Jmp,
        ksVariation_64_SamePg_Jmp,
        ksVariation_64f_SamePg_Jmp,
        ksVariation_64_NoJmp,
        ksVariation_64f_NoJmp,
        ksVariation_64_SamePg_NoJmp,
        ksVariation_64f_SamePg_NoJmp,
        ksVariation_64_FsGs,
        ksVariation_64f_FsGs,
        ksVariation_32_Flat,
        ksVariation_32f_Flat,
        ksVariation_32_Flat_Jmp,
        ksVariation_32f_Flat_Jmp,
        ksVariation_32_Flat_NoJmp,
        ksVariation_32f_Flat_NoJmp,
        ksVariation_32,
        ksVariation_32f,
        ksVariation_32_Jmp,
        ksVariation_32f_Jmp,
        ksVariation_32_NoJmp,
        ksVariation_32f_NoJmp,
        ksVariation_32_Addr16,
        ksVariation_32f_Addr16,
        ksVariation_16,
        ksVariation_16f,
        ksVariation_16_Jmp,
        ksVariation_16f_Jmp,
        ksVariation_16_NoJmp,
        ksVariation_16f_NoJmp,
        ksVariation_16_Addr32,
        ksVariation_16f_Addr32,
        ksVariation_16_Pre386,
        ksVariation_16f_Pre386,
        ksVariation_16_Pre386_Jmp,
        ksVariation_16f_Pre386_Jmp,
        ksVariation_16_Pre386_NoJmp,
        ksVariation_16f_Pre386_NoJmp,
        ksVariation_64_Addr32,
        ksVariation_64f_Addr32,
    );
    kdVariationNames = {
        ksVariation_Default:          'defer-to-cimpl',
        ksVariation_16:               '16-bit',
        ksVariation_16f:              '16-bit w/ eflag checking and clearing',
        ksVariation_16_Jmp:           '16-bit w/ conditional jump taken',
        ksVariation_16f_Jmp:          '16-bit w/ eflag checking and clearing and conditional jump taken',
        ksVariation_16_NoJmp:         '16-bit w/ conditional jump not taken',
        ksVariation_16f_NoJmp:        '16-bit w/ eflag checking and clearing and conditional jump not taken',
        ksVariation_16_Addr32:        '16-bit w/ address prefix (Addr32)',
        ksVariation_16f_Addr32:       '16-bit w/ address prefix (Addr32) and eflag checking and clearing',
        ksVariation_16_Pre386:        '16-bit on pre-386 CPU',
        ksVariation_16f_Pre386:       '16-bit on pre-386 CPU w/ eflag checking and clearing',
        ksVariation_16_Pre386_Jmp:    '16-bit on pre-386 CPU w/ conditional jump taken',
        ksVariation_16f_Pre386_Jmp:   '16-bit on pre-386 CPU w/ eflag checking and clearing and conditional jump taken',
        ksVariation_16_Pre386_NoJmp:  '16-bit on pre-386 CPU w/ conditional jump taken',
        ksVariation_16f_Pre386_NoJmp: '16-bit on pre-386 CPU w/ eflag checking and clearing and conditional jump taken',
        ksVariation_32:               '32-bit',
        ksVariation_32f:              '32-bit w/ eflag checking and clearing',
        ksVariation_32_Jmp:           '32-bit w/ conditional jump taken',
        ksVariation_32f_Jmp:          '32-bit w/ eflag checking and clearing and conditional jump taken',
        ksVariation_32_NoJmp:         '32-bit w/ conditional jump not taken',
        ksVariation_32f_NoJmp:        '32-bit w/ eflag checking and clearing and conditional jump not taken',
        ksVariation_32_Flat_Jmp:      '32-bit flat+wide CS, ++ w/ conditional jump taken',
        ksVariation_32f_Flat_Jmp:     '32-bit flat+wide CS, ++ w/ eflag checking and clearing and conditional jump taken',
        ksVariation_32_Flat_NoJmp:    '32-bit flat+wide CS, ++ w/ conditional jump not taken',
        ksVariation_32f_Flat_NoJmp:   '32-bit flat+wide CS, ++ w/ eflag checking and clearing and conditional jump not taken',
        ksVariation_32_Flat:          '32-bit flat and wide open CS, SS, DS and ES',
        ksVariation_32f_Flat:         '32-bit flat and wide open CS, SS, DS and ES w/ eflag checking and clearing',
        ksVariation_32_Addr16:        '32-bit w/ address prefix (Addr16)',
        ksVariation_32f_Addr16:       '32-bit w/ address prefix (Addr16) and eflag checking and clearing',
        ksVariation_64:               '64-bit',
        ksVariation_64f:              '64-bit w/ eflag checking and clearing',
        ksVariation_64_Jmp:           '64-bit w/ conditional jump taken',
        ksVariation_64f_Jmp:          '64-bit w/ eflag checking and clearing and conditional jump taken',
        ksVariation_64_NoJmp:         '64-bit w/ conditional jump not taken',
        ksVariation_64f_NoJmp:        '64-bit w/ eflag checking and clearing and conditional jump not taken',
        ksVariation_64_SamePg_Jmp:    '64-bit w/ conditional jump within page taken',
        ksVariation_64f_SamePg_Jmp:   '64-bit w/ eflag checking and clearing and conditional jumpwithin page taken',
        ksVariation_64_SamePg_NoJmp:  '64-bit w/ conditional jump within page not taken',
        ksVariation_64f_SamePg_NoJmp: '64-bit w/ eflag checking and clearing and conditional jump within page not taken',
        ksVariation_64_FsGs:          '64-bit with memory accessed via FS or GS',
        ksVariation_64f_FsGs:         '64-bit with memory accessed via FS or GS and eflag checking and clearing',
        ksVariation_64_Addr32:        '64-bit w/ address prefix (Addr32)',
        ksVariation_64f_Addr32:       '64-bit w/ address prefix (Addr32) and eflag checking and clearing',
    };
    kdVariationsWithEflagsCheckingAndClearing = {
        ksVariation_16f: True,
        ksVariation_16f_Jmp: True,
        ksVariation_16f_NoJmp: True,
        ksVariation_16f_Addr32: True,
        ksVariation_16f_Pre386: True,
        ksVariation_16f_Pre386_Jmp: True,
        ksVariation_16f_Pre386_NoJmp: True,
        ksVariation_32f: True,
        ksVariation_32f_Jmp: True,
        ksVariation_32f_NoJmp: True,
        ksVariation_32f_Flat: True,
        ksVariation_32f_Flat_Jmp: True,
        ksVariation_32f_Flat_NoJmp: True,
        ksVariation_32f_Addr16: True,
        ksVariation_64f: True,
        ksVariation_64f_Jmp: True,
        ksVariation_64f_NoJmp: True,
        ksVariation_64f_SamePg_Jmp: True,
        ksVariation_64f_SamePg_NoJmp: True,
        ksVariation_64f_FsGs: True,
        ksVariation_64f_Addr32: True,
    };
    kdVariationsOnly64NoFlags = {
        ksVariation_64:                 True,
        ksVariation_64_Jmp:             True,
        ksVariation_64_NoJmp:           True,
        ksVariation_64_SamePg_Jmp:      True,
        ksVariation_64_SamePg_NoJmp:    True,
        ksVariation_64_FsGs:            True,
        ksVariation_64_Addr32:          True,
    };
    kdVariationsOnly64WithFlags = {
        ksVariation_64f:                True,
        ksVariation_64f_Jmp:            True,
        ksVariation_64f_NoJmp:          True,
        ksVariation_64f_SamePg_Jmp:     True,
        ksVariation_64f_SamePg_NoJmp:   True,
        ksVariation_64f_FsGs:           True,
        ksVariation_64f_Addr32:         True,
    };
    kdVariationsOnlyPre386NoFlags = {
        ksVariation_16_Pre386:       True,
        ksVariation_16_Pre386_Jmp:   True,
        ksVariation_16_Pre386_NoJmp: True,
    };
    kdVariationsOnlyPre386WithFlags = {
        ksVariation_16f_Pre386:       True,
        ksVariation_16f_Pre386_Jmp:   True,
        ksVariation_16f_Pre386_NoJmp: True,
    };
    kdVariationsWithFlatAddress = {
        ksVariation_32_Flat: True,
        ksVariation_32f_Flat: True,
        ksVariation_64: True,
        ksVariation_64f: True,
        ksVariation_64_Addr32: True,
        ksVariation_64f_Addr32: True,
    };
    kdVariationsWithFlatStackAddress = {
        ksVariation_32_Flat: True,
        ksVariation_32f_Flat: True,
        ksVariation_64: True,
        ksVariation_64f: True,
        ksVariation_64_FsGs: True,
        ksVariation_64f_FsGs: True,
        ksVariation_64_Addr32: True,
        ksVariation_64f_Addr32: True,
    };
    kdVariationsWithFlat64StackAddress = {
        ksVariation_64: True,
        ksVariation_64f: True,
        ksVariation_64_FsGs: True,
        ksVariation_64f_FsGs: True,
        ksVariation_64_Addr32: True,
        ksVariation_64f_Addr32: True,
    };
    kdVariationsWithFlatAddr16 = {
        ksVariation_16: True,
        ksVariation_16f: True,
        ksVariation_16_Pre386: True,
        ksVariation_16f_Pre386: True,
        ksVariation_32_Addr16: True,
        ksVariation_32f_Addr16: True,
    };
    kdVariationsWithFlatAddr32No64 = {
        ksVariation_16_Addr32:  True,
        ksVariation_16f_Addr32: True,
        ksVariation_32:         True,
        ksVariation_32f:        True,
        ksVariation_32_Flat:    True,
        ksVariation_32f_Flat:   True,
    };
    kdVariationsWithAddressOnly64 = {
        ksVariation_64:         True,
        ksVariation_64f:        True,
        ksVariation_64_FsGs:    True,
        ksVariation_64f_FsGs:   True,
        ksVariation_64_Addr32:  True,
        ksVariation_64f_Addr32: True,
    };
    kdVariationsWithConditional = {
        ksVariation_16_Jmp:             True,
        ksVariation_16_NoJmp:           True,
        ksVariation_16_Pre386_Jmp:      True,
        ksVariation_16_Pre386_NoJmp:    True,
        ksVariation_32_Jmp:             True,
        ksVariation_32_NoJmp:           True,
        ksVariation_32_Flat_Jmp:        True,
        ksVariation_32_Flat_NoJmp:      True,
        ksVariation_64_Jmp:             True,
        ksVariation_64_NoJmp:           True,
        ksVariation_64_SamePg_Jmp:      True,
        ksVariation_64_SamePg_NoJmp:    True,
        ksVariation_16f_Jmp:            True,
        ksVariation_16f_NoJmp:          True,
        ksVariation_16f_Pre386_Jmp:     True,
        ksVariation_16f_Pre386_NoJmp:   True,
        ksVariation_32f_Jmp:            True,
        ksVariation_32f_NoJmp:          True,
        ksVariation_32f_Flat_Jmp:       True,
        ksVariation_32f_Flat_NoJmp:     True,
        ksVariation_64f_Jmp:            True,
        ksVariation_64f_NoJmp:          True,
        ksVariation_64f_SamePg_Jmp:     True,
        ksVariation_64f_SamePg_NoJmp:   True,
    };
    kdVariationsWithConditionalNoJmp = {
        ksVariation_16_NoJmp:           True,
        ksVariation_16_Pre386_NoJmp:    True,
        ksVariation_32_NoJmp:           True,
        ksVariation_32_Flat_NoJmp:      True,
        ksVariation_64_NoJmp:           True,
        ksVariation_64_SamePg_NoJmp:    True,
        ksVariation_16f_NoJmp:          True,
        ksVariation_16f_Pre386_NoJmp:   True,
        ksVariation_32f_NoJmp:          True,
        ksVariation_32f_Flat_NoJmp:     True,
        ksVariation_64f_NoJmp:          True,
        ksVariation_64f_SamePg_NoJmp:   True,
    };
    kdVariationsWithFlat32Conditional = {
        ksVariation_32_Flat_Jmp:        True,
        ksVariation_32_Flat_NoJmp:      True,
        ksVariation_32f_Flat_Jmp:       True,
        ksVariation_32f_Flat_NoJmp:     True,
    };
    kdVariationsWithSamePgConditional = {
        ksVariation_64_SamePg_Jmp:      True,
        ksVariation_64_SamePg_NoJmp:    True,
        ksVariation_64f_SamePg_Jmp:     True,
        ksVariation_64f_SamePg_NoJmp:   True,
    };
    kdVariationsOnlyPre386 = {
        ksVariation_16_Pre386:          True,
        ksVariation_16f_Pre386:         True,
        ksVariation_16_Pre386_Jmp:      True,
        ksVariation_16f_Pre386_Jmp:     True,
        ksVariation_16_Pre386_NoJmp:    True,
        ksVariation_16f_Pre386_NoJmp:   True,
    };
    ## @}

    ## IEM_CIMPL_F_XXX flags that we know.
    ## The value indicates whether it terminates the TB or not. The goal is to
    ## improve the recompiler so all but END_TB will be False.
    ##
    ## @note iemThreadedRecompilerMcDeferToCImpl0 duplicates info found here.
    kdCImplFlags = {
        'IEM_CIMPL_F_MODE':                         False,
        'IEM_CIMPL_F_BRANCH_DIRECT':                False,
        'IEM_CIMPL_F_BRANCH_INDIRECT':              False,
        'IEM_CIMPL_F_BRANCH_RELATIVE':              False,
        'IEM_CIMPL_F_BRANCH_FAR':                   True,
        'IEM_CIMPL_F_BRANCH_CONDITIONAL':           False,
        # IEM_CIMPL_F_BRANCH_ANY should only be used for testing, so not included here.
        'IEM_CIMPL_F_BRANCH_STACK':                 False,
        'IEM_CIMPL_F_BRANCH_STACK_FAR':             False,
        'IEM_CIMPL_F_RFLAGS':                       False,
        'IEM_CIMPL_F_INHIBIT_SHADOW':               False,
        'IEM_CIMPL_F_CHECK_IRQ_AFTER':              False,
        'IEM_CIMPL_F_CHECK_IRQ_BEFORE':             False,
        'IEM_CIMPL_F_CHECK_IRQ_BEFORE_AND_AFTER':   False, # (ignore)
        'IEM_CIMPL_F_STATUS_FLAGS':                 False,
        'IEM_CIMPL_F_VMEXIT':                       False,
        'IEM_CIMPL_F_FPU':                          False,
        'IEM_CIMPL_F_REP':                          False,
        'IEM_CIMPL_F_IO':                           False,
        'IEM_CIMPL_F_END_TB':                       True,
        'IEM_CIMPL_F_XCPT':                         True,
        'IEM_CIMPL_F_CALLS_CIMPL':                  False,
        'IEM_CIMPL_F_CALLS_AIMPL':                  False,
        'IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE':     False,
        'IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE':      False,
    };

    def __init__(self, oThreadedFunction, sVariation = ksVariation_Default):
        self.oParent        = oThreadedFunction # type: ThreadedFunction
        ##< ksVariation_Xxxx.
        self.sVariation     = sVariation

        ## Threaded function parameter references.
        self.aoParamRefs    = []            # type: List[ThreadedParamRef]
        ## Unique parameter references.
        self.dParamRefs     = {}            # type: Dict[str, List[ThreadedParamRef]]
        ## Minimum number of parameters to the threaded function.
        self.cMinParams     = 0;

        ## List/tree of statements for the threaded function.
        self.aoStmtsForThreadedFunction = [] # type: List[McStmt]

        ## Function enum number, for verification. Set by generateThreadedFunctionsHeader.
        self.iEnumValue     = -1;

        ## Native recompilation details for this variation.
        self.oNativeRecomp  = None;

    def getIndexName(self):
        sName = self.oParent.oMcBlock.sFunction;
        if sName.startswith('iemOp_'):
            sName = sName[len('iemOp_'):];
        return 'kIemThreadedFunc_%s%s%s' % ( sName, self.oParent.sSubName, self.sVariation, );

    def getThreadedFunctionName(self):
        sName = self.oParent.oMcBlock.sFunction;
        if sName.startswith('iemOp_'):
            sName = sName[len('iemOp_'):];
        return 'iemThreadedFunc_%s%s%s' % ( sName, self.oParent.sSubName, self.sVariation, );

    def getNativeFunctionName(self):
        return 'iemNativeRecompFunc_' + self.getThreadedFunctionName()[len('iemThreadedFunc_'):];

    def getLivenessFunctionName(self):
        return 'iemNativeLivenessFunc_' + self.getThreadedFunctionName()[len('iemThreadedFunc_'):];

    def getShortName(self):
        sName = self.oParent.oMcBlock.sFunction;
        if sName.startswith('iemOp_'):
            sName = sName[len('iemOp_'):];
        return '%s%s%s' % ( sName, self.oParent.sSubName, self.sVariation, );

    def getThreadedFunctionStatisticsName(self):
        sName = self.oParent.oMcBlock.sFunction;
        if sName.startswith('iemOp_'):
            sName = sName[len('iemOp_'):];

        sVarNm = self.sVariation;
        if sVarNm:
            if sVarNm.startswith('_'):
                sVarNm = sVarNm[1:];
            if sVarNm.endswith('_Jmp'):
                sVarNm = sVarNm[:-4];
                sName += '_Jmp';
            elif sVarNm.endswith('_NoJmp'):
                sVarNm = sVarNm[:-6];
                sName += '_NoJmp';
        else:
            sVarNm = 'DeferToCImpl';

        return '%s/%s%s' % ( sVarNm, sName, self.oParent.sSubName );

    def isWithFlagsCheckingAndClearingVariation(self):
        """
        Checks if this is a variation that checks and clears EFLAGS.
        """
        return self.sVariation in ThreadedFunctionVariation.kdVariationsWithEflagsCheckingAndClearing;

    #
    # Analysis and code morphing.
    #

    def raiseProblem(self, sMessage):
        """ Raises a problem. """
        self.oParent.raiseProblem(sMessage);

    def warning(self, sMessage):
        """ Emits a warning. """
        self.oParent.warning(sMessage);

    def analyzeReferenceToType(self, sRef):
        """
        Translates a variable or structure reference to a type.
        Returns type name.
        Raises exception if unable to figure it out.
        """
        ch0 = sRef[0];
        if ch0 == 'u':
            if sRef.startswith('u32'):
                return 'uint32_t';
            if sRef.startswith('u8') or sRef == 'uReg':
                return 'uint8_t';
            if sRef.startswith('u64'):
                return 'uint64_t';
            if sRef.startswith('u16'):
                return 'uint16_t';
        elif ch0 == 'b':
            return 'uint8_t';
        elif ch0 == 'f':
            return 'bool';
        elif ch0 == 'i':
            if sRef.startswith('i8'):
                return 'int8_t';
            if sRef.startswith('i16'):
                return 'int16_t';
            if sRef.startswith('i32'):
                return 'int32_t';
            if sRef.startswith('i64'):
                return 'int64_t';
            if sRef in ('iReg', 'iFixedReg', 'iGReg', 'iSegReg', 'iSrcReg', 'iDstReg', 'iCrReg'):
                return 'uint8_t';
        elif ch0 == 'p':
            if sRef.find('-') < 0:
                return 'uintptr_t';
            if sRef.startswith('pVCpu->iem.s.'):
                sField = sRef[len('pVCpu->iem.s.') : ];
                if sField in g_kdIemFieldToType:
                    if g_kdIemFieldToType[sField][0]:
                        return g_kdIemFieldToType[sField][0];
        elif ch0 == 'G' and sRef.startswith('GCPtr'):
            return 'uint64_t';
        elif ch0 == 'e':
            if sRef == 'enmEffOpSize':
                return 'IEMMODE';
        elif ch0 == 'o':
            if sRef.startswith('off32'):
                return 'uint32_t';
        elif sRef == 'cbFrame':  # enter
            return 'uint16_t';
        elif sRef == 'cShift': ## @todo risky
            return 'uint8_t';

        self.raiseProblem('Unknown reference: %s' % (sRef,));
        return None; # Shut up pylint 2.16.2.

    def analyzeCallToType(self, sFnRef):
        """
        Determins the type of an indirect function call.
        """
        assert sFnRef[0] == 'p';

        #
        # Simple?
        #
        if sFnRef.find('-') < 0:
            oDecoderFunction = self.oParent.oMcBlock.oFunction;

            # Try the argument list of the function defintion macro invocation first.
            iArg = 2;
            while iArg < len(oDecoderFunction.asDefArgs):
                if sFnRef == oDecoderFunction.asDefArgs[iArg]:
                    return oDecoderFunction.asDefArgs[iArg - 1];
                iArg += 1;

            # Then check out line that includes the word and looks like a variable declaration.
            oRe = re.compile(' +(P[A-Z0-9_]+|const +IEMOP[A-Z0-9_]+ *[*]) +(const |) *' + sFnRef + ' *(;|=)');
            for sLine in oDecoderFunction.asLines:
                oMatch = oRe.match(sLine);
                if oMatch:
                    if not oMatch.group(1).startswith('const'):
                        return oMatch.group(1);
                    return 'PC' + oMatch.group(1)[len('const ') : -1].strip();

        #
        # Deal with the pImpl->pfnXxx:
        #
        elif sFnRef.startswith('pImpl->pfn'):
            sMember   = sFnRef[len('pImpl->') : ];
            sBaseType = self.analyzeCallToType('pImpl');
            offBits   = sMember.rfind('U') + 1;
            if sBaseType == 'PCIEMOPBINSIZES':          return 'PFNIEMAIMPLBINU'        + sMember[offBits:];
            if sBaseType == 'PCIEMOPBINTODOSIZES':      return 'PFNIEMAIMPLBINTODOU'    + sMember[offBits:];
            if sBaseType == 'PCIEMOPUNARYSIZES':        return 'PFNIEMAIMPLUNARYU'      + sMember[offBits:];
            if sBaseType == 'PCIEMOPSHIFTSIZES':        return 'PFNIEMAIMPLSHIFTU'      + sMember[offBits:];
            if sBaseType == 'PCIEMOPSHIFTDBLSIZES':     return 'PFNIEMAIMPLSHIFTDBLU'   + sMember[offBits:];
            if sBaseType == 'PCIEMOPMULDIVSIZES':       return 'PFNIEMAIMPLMULDIVU'     + sMember[offBits:];
            if sBaseType == 'PCIEMOPMEDIAF2':           return 'PFNIEMAIMPLMEDIAF2U'    + sMember[offBits:];
            if sBaseType == 'PCIEMOPMEDIAF2IMM8':       return 'PFNIEMAIMPLMEDIAF2U'    + sMember[offBits:] + 'IMM8';
            if sBaseType == 'PCIEMOPMEDIAF3':           return 'PFNIEMAIMPLMEDIAF3U'    + sMember[offBits:];
            if sBaseType == 'PCIEMOPMEDIAOPTF2':        return 'PFNIEMAIMPLMEDIAOPTF2U' + sMember[offBits:];
            if sBaseType == 'PCIEMOPMEDIAOPTF2IMM8':    return 'PFNIEMAIMPLMEDIAOPTF2U' + sMember[offBits:] + 'IMM8';
            if sBaseType == 'PCIEMOPMEDIAOPTF3':        return 'PFNIEMAIMPLMEDIAOPTF3U' + sMember[offBits:];
            if sBaseType == 'PCIEMOPMEDIAOPTF3IMM8':    return 'PFNIEMAIMPLMEDIAOPTF3U' + sMember[offBits:] + 'IMM8';
            if sBaseType == 'PCIEMOPBLENDOP':           return 'PFNIEMAIMPLAVXBLENDU'   + sMember[offBits:];

            self.raiseProblem('Unknown call reference: %s::%s (%s)' % (sBaseType, sMember, sFnRef,));

        self.raiseProblem('Unknown call reference: %s' % (sFnRef,));
        return None; # Shut up pylint 2.16.2.

    def analyze8BitGRegStmt(self, oStmt):
        """
        Gets the 8-bit general purpose register access details of the given statement.
        ASSUMES the statement is one accessing an 8-bit GREG.
        """
        idxReg = 0;
        if (   oStmt.sName.find('_FETCH_') > 0
            or oStmt.sName.find('_REF_') > 0
            or oStmt.sName.find('_TO_LOCAL') > 0):
            idxReg = 1;

        sRegRef = oStmt.asParams[idxReg];
        if sRegRef.startswith('IEM_GET_MODRM_RM') or sRegRef.startswith('IEM_GET_MODRM_REG'):
            asBits = [sBit.strip() for sBit in sRegRef.replace('(', ',').replace(')', '').split(',')];
            if len(asBits) != 3 or asBits[1] != 'pVCpu' or (asBits[0] != 'IEM_GET_MODRM_RM' and asBits[0] != 'IEM_GET_MODRM_REG'):
                self.raiseProblem('Unexpected reference: %s (asBits=%s)' % (sRegRef, asBits));
            sOrgExpr = asBits[0] + '_EX8(pVCpu, ' + asBits[2] + ')';
        else:
            sOrgExpr = '((%s) < 4 || (pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_REX | IEM_OP_PRF_VEX)) ? (%s) : (%s) + 12)' \
                     % (sRegRef, sRegRef, sRegRef,);

        if sRegRef.find('IEM_GET_MODRM_RM') >= 0:    sStdRef = 'bRmRm8Ex';
        elif sRegRef.find('IEM_GET_MODRM_REG') >= 0: sStdRef = 'bRmReg8Ex';
        elif sRegRef == 'X86_GREG_xAX':              sStdRef = 'bGregXAx8Ex';
        elif sRegRef == 'X86_GREG_xCX':              sStdRef = 'bGregXCx8Ex';
        elif sRegRef == 'X86_GREG_xSP':              sStdRef = 'bGregXSp8Ex';
        elif sRegRef == 'iFixedReg':                 sStdRef = 'bFixedReg8Ex';
        else:
            self.warning('analyze8BitGRegStmt: sRegRef=%s -> bOther8Ex; %s %s; sOrgExpr=%s'
                         % (sRegRef, oStmt.sName, oStmt.asParams, sOrgExpr,));
            sStdRef = 'bOther8Ex';

        #print('analyze8BitGRegStmt: %s %s; sRegRef=%s\n -> idxReg=%s sOrgExpr=%s sStdRef=%s'
        #      % (oStmt.sName, oStmt.asParams, sRegRef, idxReg, sOrgExpr, sStdRef));
        return (idxReg, sOrgExpr, sStdRef);


    ## Maps memory related MCs to info for FLAT conversion.
    ## This is used in 64-bit and flat 32-bit variants to skip the unnecessary
    ## segmentation checking for every memory access.  Only applied to access
    ## via ES, DS and SS.  FS, GS and CS gets the full segmentation threatment,
    ## the latter (CS) is just to keep things simple (we could safely fetch via
    ## it, but only in 64-bit mode could we safely write via it, IIRC).
    kdMemMcToFlatInfo = {
        'IEM_MC_FETCH_MEM_U8':                    (  1, 'IEM_MC_FETCH_MEM_FLAT_U8' ),
        'IEM_MC_FETCH_MEM16_U8':                  (  1, 'IEM_MC_FETCH_MEM16_FLAT_U8' ),
        'IEM_MC_FETCH_MEM32_U8':                  (  1, 'IEM_MC_FETCH_MEM32_FLAT_U8' ),
        'IEM_MC_FETCH_MEM_U16':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_U16' ),
        'IEM_MC_FETCH_MEM_U16_DISP':              (  1, 'IEM_MC_FETCH_MEM_FLAT_U16_DISP' ),
        'IEM_MC_FETCH_MEM_I16':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_I16' ),
        'IEM_MC_FETCH_MEM_I16_DISP':              (  1, 'IEM_MC_FETCH_MEM_FLAT_I16_DISP' ),
        'IEM_MC_FETCH_MEM_U32':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_U32' ),
        'IEM_MC_FETCH_MEM_U32_DISP':              (  1, 'IEM_MC_FETCH_MEM_FLAT_U32_DISP' ),
        'IEM_MC_FETCH_MEM_I32':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_I32' ),
        'IEM_MC_FETCH_MEM_I32_DISP':              (  1, 'IEM_MC_FETCH_MEM_FLAT_I32_DISP' ),
        'IEM_MC_FETCH_MEM_U64':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_U64' ),
        'IEM_MC_FETCH_MEM_U64_DISP':              (  1, 'IEM_MC_FETCH_MEM_FLAT_U64_DISP' ),
        'IEM_MC_FETCH_MEM_U64_ALIGN_U128':        (  1, 'IEM_MC_FETCH_MEM_FLAT_U64_ALIGN_U128' ),
        'IEM_MC_FETCH_MEM_I64':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_I64' ),
        'IEM_MC_FETCH_MEM_R32':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_R32' ),
        'IEM_MC_FETCH_MEM_R64':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_R64' ),
        'IEM_MC_FETCH_MEM_R80':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_R80' ),
        'IEM_MC_FETCH_MEM_D80':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_D80' ),
        'IEM_MC_FETCH_MEM_U128':                  (  1, 'IEM_MC_FETCH_MEM_FLAT_U128' ),
        'IEM_MC_FETCH_MEM_U128_NO_AC':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U128_NO_AC' ),
        'IEM_MC_FETCH_MEM_U128_ALIGN_SSE':        (  1, 'IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE' ),
        'IEM_MC_FETCH_MEM_XMM':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_XMM' ),
        'IEM_MC_FETCH_MEM_XMM_NO_AC':             (  1, 'IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC' ),
        'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE':         (  1, 'IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE' ),
        'IEM_MC_FETCH_MEM_XMM_U32':               (  2, 'IEM_MC_FETCH_MEM_FLAT_XMM_U32' ),
        'IEM_MC_FETCH_MEM_XMM_U64':               (  2, 'IEM_MC_FETCH_MEM_FLAT_XMM_U64' ),
        'IEM_MC_FETCH_MEM_U256':                  (  1, 'IEM_MC_FETCH_MEM_FLAT_U256' ),
        'IEM_MC_FETCH_MEM_U256_NO_AC':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U256_NO_AC' ),
        'IEM_MC_FETCH_MEM_U256_ALIGN_AVX':        (  1, 'IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX' ),
        'IEM_MC_FETCH_MEM_YMM':                   (  1, 'IEM_MC_FETCH_MEM_FLAT_YMM' ),
        'IEM_MC_FETCH_MEM_YMM_NO_AC':             (  1, 'IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC' ),
        'IEM_MC_FETCH_MEM_YMM_ALIGN_AVX':         (  1, 'IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX' ),
        'IEM_MC_FETCH_MEM_U8_ZX_U16':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16' ),
        'IEM_MC_FETCH_MEM_U8_ZX_U32':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32' ),
        'IEM_MC_FETCH_MEM_U8_ZX_U64':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64' ),
        'IEM_MC_FETCH_MEM_U16_ZX_U32':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32' ),
        'IEM_MC_FETCH_MEM_U16_ZX_U64':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64' ),
        'IEM_MC_FETCH_MEM_U32_ZX_U64':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64' ),
        'IEM_MC_FETCH_MEM_U8_SX_U16':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_SX_U16' ),
        'IEM_MC_FETCH_MEM_U8_SX_U32':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_SX_U32' ),
        'IEM_MC_FETCH_MEM_U8_SX_U64':             (  1, 'IEM_MC_FETCH_MEM_FLAT_U8_SX_U64' ),
        'IEM_MC_FETCH_MEM_U16_SX_U32':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U16_SX_U32' ),
        'IEM_MC_FETCH_MEM_U16_SX_U64':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U16_SX_U64' ),
        'IEM_MC_FETCH_MEM_U32_SX_U64':            (  1, 'IEM_MC_FETCH_MEM_FLAT_U32_SX_U64' ),
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128':          (  2, 'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128' ),
        'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM':  (  2, 'IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE_AND_XREG_XMM' ),
        'IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM':        (  3, 'IEM_MC_FETCH_MEM_FLAT_XMM_U32_AND_XREG_XMM' ),
        'IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM':        (  3, 'IEM_MC_FETCH_MEM_FLAT_XMM_U64_AND_XREG_XMM' ),
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64':
            (  2, 'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_RAX_RDX_U64' ),
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64':
            (  2, 'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64' ),
        'IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM':  (  2, 'IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX_AND_YREG_YMM' ),
        'IEM_MC_STORE_MEM_U8':                    (  0, 'IEM_MC_STORE_MEM_FLAT_U8' ),
        'IEM_MC_STORE_MEM_U16':                   (  0, 'IEM_MC_STORE_MEM_FLAT_U16' ),
        'IEM_MC_STORE_MEM_U32':                   (  0, 'IEM_MC_STORE_MEM_FLAT_U32' ),
        'IEM_MC_STORE_MEM_U64':                   (  0, 'IEM_MC_STORE_MEM_FLAT_U64' ),
        'IEM_MC_STORE_MEM_U8_CONST':              (  0, 'IEM_MC_STORE_MEM_FLAT_U8_CONST' ),
        'IEM_MC_STORE_MEM_U16_CONST':             (  0, 'IEM_MC_STORE_MEM_FLAT_U16_CONST' ),
        'IEM_MC_STORE_MEM_U32_CONST':             (  0, 'IEM_MC_STORE_MEM_FLAT_U32_CONST' ),
        'IEM_MC_STORE_MEM_U64_CONST':             (  0, 'IEM_MC_STORE_MEM_FLAT_U64_CONST' ),
        'IEM_MC_STORE_MEM_U128':                  (  0, 'IEM_MC_STORE_MEM_FLAT_U128' ),
        'IEM_MC_STORE_MEM_U128_NO_AC':            (  0, 'IEM_MC_STORE_MEM_FLAT_U128_NO_AC' ),
        'IEM_MC_STORE_MEM_U128_ALIGN_SSE':        (  0, 'IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE' ),
        'IEM_MC_STORE_MEM_U256':                  (  0, 'IEM_MC_STORE_MEM_FLAT_U256' ),
        'IEM_MC_STORE_MEM_U256_NO_AC':            (  0, 'IEM_MC_STORE_MEM_FLAT_U256_NO_AC' ),
        'IEM_MC_STORE_MEM_U256_ALIGN_AVX':        (  0, 'IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX' ),
        'IEM_MC_MEM_MAP_D80_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_D80_WO' ),
        'IEM_MC_MEM_MAP_I16_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_I16_WO' ),
        'IEM_MC_MEM_MAP_I32_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_I32_WO' ),
        'IEM_MC_MEM_MAP_I64_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_I64_WO' ),
        'IEM_MC_MEM_MAP_R32_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_R32_WO' ),
        'IEM_MC_MEM_MAP_R64_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_R64_WO' ),
        'IEM_MC_MEM_MAP_R80_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_R80_WO' ),
        'IEM_MC_MEM_MAP_U8_ATOMIC':               (  2, 'IEM_MC_MEM_FLAT_MAP_U8_ATOMIC' ),
        'IEM_MC_MEM_MAP_U8_RW':                   (  2, 'IEM_MC_MEM_FLAT_MAP_U8_RW' ),
        'IEM_MC_MEM_MAP_U8_RO':                   (  2, 'IEM_MC_MEM_FLAT_MAP_U8_RO' ),
        'IEM_MC_MEM_MAP_U8_WO':                   (  2, 'IEM_MC_MEM_FLAT_MAP_U8_WO' ),
        'IEM_MC_MEM_MAP_U16_ATOMIC':              (  2, 'IEM_MC_MEM_FLAT_MAP_U16_ATOMIC' ),
        'IEM_MC_MEM_MAP_U16_RW':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U16_RW' ),
        'IEM_MC_MEM_MAP_U16_RO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U16_RO' ),
        'IEM_MC_MEM_MAP_U16_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U16_WO' ),
        'IEM_MC_MEM_MAP_U32_ATOMIC':              (  2, 'IEM_MC_MEM_FLAT_MAP_U32_ATOMIC' ),
        'IEM_MC_MEM_MAP_U32_RW':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U32_RW' ),
        'IEM_MC_MEM_MAP_U32_RO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U32_RO' ),
        'IEM_MC_MEM_MAP_U32_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U32_WO' ),
        'IEM_MC_MEM_MAP_U64_ATOMIC':              (  2, 'IEM_MC_MEM_FLAT_MAP_U64_ATOMIC' ),
        'IEM_MC_MEM_MAP_U64_RW':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U64_RW' ),
        'IEM_MC_MEM_MAP_U64_RO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U64_RO' ),
        'IEM_MC_MEM_MAP_U64_WO':                  (  2, 'IEM_MC_MEM_FLAT_MAP_U64_WO' ),
        'IEM_MC_MEM_MAP_U128_ATOMIC':             (  2, 'IEM_MC_MEM_FLAT_MAP_U128_ATOMIC' ),
        'IEM_MC_MEM_MAP_U128_RW':                 (  2, 'IEM_MC_MEM_FLAT_MAP_U128_RW' ),
        'IEM_MC_MEM_MAP_U128_RO':                 (  2, 'IEM_MC_MEM_FLAT_MAP_U128_RO' ),
        'IEM_MC_MEM_MAP_U128_WO':                 (  2, 'IEM_MC_MEM_FLAT_MAP_U128_WO' ),
        'IEM_MC_MEM_MAP_EX':                      (  3, 'IEM_MC_MEM_FLAT_MAP_EX' ),
    };

    kdMemMcToFlatInfoStack = {
        'IEM_MC_PUSH_U16':                        (  'IEM_MC_FLAT32_PUSH_U16',      'IEM_MC_FLAT64_PUSH_U16', ),
        'IEM_MC_PUSH_U32':                        (  'IEM_MC_FLAT32_PUSH_U32',      'IEM_MC_PUSH_U32', ),
        'IEM_MC_PUSH_U64':                        (  'IEM_MC_PUSH_U64',             'IEM_MC_FLAT64_PUSH_U64', ),
        'IEM_MC_PUSH_U32_SREG':                   (  'IEM_MC_FLAT32_PUSH_U32_SREG', 'IEM_MC_PUSH_U32_SREG' ),
        'IEM_MC_POP_GREG_U16':                    (  'IEM_MC_FLAT32_POP_GREG_U16',  'IEM_MC_FLAT64_POP_GREG_U16', ),
        'IEM_MC_POP_GREG_U32':                    (  'IEM_MC_FLAT32_POP_GREG_U32',  'IEM_MC_POP_GREG_U32', ),
        'IEM_MC_POP_GREG_U64':                    (  'IEM_MC_POP_GREG_U64',         'IEM_MC_FLAT64_POP_GREG_U64', ),
    };

    kdThreadedCalcRmEffAddrMcByVariation = {
        ksVariation_16:             'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_16f:            'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_16_Pre386:      'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_16f_Pre386:     'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_32_Addr16:      'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_32f_Addr16:     'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16',
        ksVariation_16_Addr32:      'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_16f_Addr32:     'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_32:             'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_32f:            'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_32_Flat:        'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_32f_Flat:       'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32',
        ksVariation_64:             'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64',
        ksVariation_64f:            'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64',
        ksVariation_64_FsGs:        'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS',
        ksVariation_64f_FsGs:       'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS',
        ksVariation_64_Addr32:      'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32', ## @todo How did this work again...
        ksVariation_64f_Addr32:     'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32',
    };

    kdRelJmpMcWithFlatOrSamePageVariations = {
        'IEM_MC_REL_JMP_S8_AND_FINISH':  True,
        'IEM_MC_REL_JMP_S16_AND_FINISH': True,
        'IEM_MC_REL_JMP_S32_AND_FINISH': True,
    };

    def analyzeMorphStmtForThreaded(self, aoStmts, dState, iParamRef = 0, iLevel = 0):
        """
        Transforms (copy) the statements into those for the threaded function.

        Returns list/tree of statements (aoStmts is not modified) and the new
        iParamRef value.
        """
        #
        # We'll be traversing aoParamRefs in parallel to the statements, so we
        # must match the traversal in analyzeFindThreadedParamRefs exactly.
        #
        #print('McBlock at %s:%s' % (os.path.split(self.oMcBlock.sSrcFile)[1], self.oMcBlock.iBeginLine,));
        aoThreadedStmts = [];
        for oStmt in aoStmts:
            # Skip C++ statements that is purely related to decoding.
            if not oStmt.isCppStmt() or not oStmt.fDecode:
                # Copy the statement. Make a deep copy to make sure we've got our own
                # copies of all instance variables, even if a bit overkill at the moment.
                oNewStmt = copy.deepcopy(oStmt);
                aoThreadedStmts.append(oNewStmt);
                #print('oNewStmt %s %s' % (oNewStmt.sName, len(oNewStmt.asParams),));

                # If the statement has parameter references, process the relevant parameters.
                # We grab the references relevant to this statement and apply them in reserve order.
                if iParamRef < len(self.aoParamRefs) and self.aoParamRefs[iParamRef].oStmt == oStmt:
                    iParamRefFirst = iParamRef;
                    while True:
                        iParamRef += 1;
                        if iParamRef >= len(self.aoParamRefs) or self.aoParamRefs[iParamRef].oStmt != oStmt:
                            break;

                    #print('iParamRefFirst=%s iParamRef=%s' % (iParamRefFirst, iParamRef));
                    for iCurRef in range(iParamRef - 1, iParamRefFirst - 1, -1):
                        oCurRef = self.aoParamRefs[iCurRef];
                        if oCurRef.iParam is not None:
                            assert oCurRef.oStmt == oStmt;
                            #print('iCurRef=%s iParam=%s sOrgRef=%s' % (iCurRef, oCurRef.iParam, oCurRef.sOrgRef));
                            sSrcParam = oNewStmt.asParams[oCurRef.iParam];
                            assert (   sSrcParam[oCurRef.offParam : oCurRef.offParam + len(oCurRef.sOrgRef)] == oCurRef.sOrgRef
                                    or oCurRef.fCustomRef), \
                                   'offParam=%s sOrgRef=%s iParam=%s oStmt.sName=%s sSrcParam=%s<eos>' \
                                   % (oCurRef.offParam, oCurRef.sOrgRef, oCurRef.iParam, oStmt.sName, sSrcParam);
                            oNewStmt.asParams[oCurRef.iParam] = sSrcParam[0 : oCurRef.offParam] \
                                                              + oCurRef.sNewName \
                                                              + sSrcParam[oCurRef.offParam + len(oCurRef.sOrgRef) : ];

                # Morph IEM_MC_CALC_RM_EFF_ADDR into IEM_MC_CALC_RM_EFF_ADDR_THREADED ...
                if oNewStmt.sName == 'IEM_MC_CALC_RM_EFF_ADDR':
                    oNewStmt.sName = self.kdThreadedCalcRmEffAddrMcByVariation[self.sVariation];
                    assert len(oNewStmt.asParams) == 3;

                    if self.sVariation in self.kdVariationsWithFlatAddr16:
                        oNewStmt.asParams = [
                            oNewStmt.asParams[0], oNewStmt.asParams[1], self.dParamRefs['u16Disp'][0].sNewName,
                        ];
                    else:
                        sSibAndMore = self.dParamRefs['bSib'][0].sNewName; # Merge bSib and 2nd part of cbImmAndRspOffset.
                        if oStmt.asParams[2] not in ('0', '1', '2', '4'):
                            sSibAndMore = '(%s) | ((%s) & 0x0f00)' % (self.dParamRefs['bSib'][0].sNewName, oStmt.asParams[2]);

                        if self.sVariation in self.kdVariationsWithFlatAddr32No64:
                            oNewStmt.asParams = [
                                oNewStmt.asParams[0], oNewStmt.asParams[1], sSibAndMore, self.dParamRefs['u32Disp'][0].sNewName,
                            ];
                        else:
                            oNewStmt.asParams = [
                                oNewStmt.asParams[0], self.dParamRefs['bRmEx'][0].sNewName, sSibAndMore,
                                self.dParamRefs['u32Disp'][0].sNewName, self.dParamRefs['cbInstr'][0].sNewName,
                            ];
                # ... and IEM_MC_ADVANCE_RIP_AND_FINISH into *_THREADED_PCxx[_WITH_FLAGS] ...
                elif (   oNewStmt.sName
                      in ('IEM_MC_ADVANCE_RIP_AND_FINISH',
                          'IEM_MC_REL_JMP_S8_AND_FINISH',  'IEM_MC_REL_JMP_S16_AND_FINISH', 'IEM_MC_REL_JMP_S32_AND_FINISH',
                          'IEM_MC_SET_RIP_U16_AND_FINISH', 'IEM_MC_SET_RIP_U32_AND_FINISH', 'IEM_MC_SET_RIP_U64_AND_FINISH',
                          'IEM_MC_REL_CALL_S16_AND_FINISH', 'IEM_MC_REL_CALL_S32_AND_FINISH', 'IEM_MC_REL_CALL_S64_AND_FINISH',
                          'IEM_MC_IND_CALL_U16_AND_FINISH', 'IEM_MC_IND_CALL_U32_AND_FINISH', 'IEM_MC_IND_CALL_U64_AND_FINISH',
                          'IEM_MC_RETN_AND_FINISH',)):
                    if oNewStmt.sName not in ('IEM_MC_SET_RIP_U16_AND_FINISH', 'IEM_MC_SET_RIP_U32_AND_FINISH',
                                              'IEM_MC_SET_RIP_U64_AND_FINISH', ):
                        oNewStmt.asParams.append(self.dParamRefs['cbInstr'][0].sNewName);
                    if (    oNewStmt.sName in ('IEM_MC_REL_JMP_S8_AND_FINISH', 'IEM_MC_RETN_AND_FINISH', )
                        and self.sVariation not in self.kdVariationsOnlyPre386):
                        oNewStmt.asParams.append(self.dParamRefs['pVCpu->iem.s.enmEffOpSize'][0].sNewName);
                    if   self.sVariation in self.kdVariationsOnly64NoFlags:
                        if (  self.sVariation not in self.kdVariationsWithSamePgConditional
                            or oNewStmt.sName not in self.kdRelJmpMcWithFlatOrSamePageVariations):
                            oNewStmt.sName += '_THREADED_PC64';
                        else:
                            oNewStmt.sName += '_THREADED_PC64_INTRAPG';
                    elif self.sVariation in self.kdVariationsOnly64WithFlags:
                        if (  self.sVariation not in self.kdVariationsWithSamePgConditional
                            or oNewStmt.sName not in self.kdRelJmpMcWithFlatOrSamePageVariations):
                            oNewStmt.sName += '_THREADED_PC64_WITH_FLAGS';
                        else:
                            oNewStmt.sName += '_THREADED_PC64_INTRAPG_WITH_FLAGS';
                    elif self.sVariation in self.kdVariationsOnlyPre386NoFlags:
                        oNewStmt.sName += '_THREADED_PC16';
                    elif self.sVariation in self.kdVariationsOnlyPre386WithFlags:
                        oNewStmt.sName += '_THREADED_PC16_WITH_FLAGS';
                    elif oNewStmt.sName not in self.kdRelJmpMcWithFlatOrSamePageVariations:
                        if self.sVariation not in self.kdVariationsWithEflagsCheckingAndClearing:
                            assert self.sVariation != self.ksVariation_Default;
                            oNewStmt.sName += '_THREADED_PC32';
                        else:
                            oNewStmt.sName += '_THREADED_PC32_WITH_FLAGS';
                    else:
                        if self.sVariation not in self.kdVariationsWithEflagsCheckingAndClearing:
                            assert self.sVariation != self.ksVariation_Default;
                            oNewStmt.sName += '_THREADED_PC32_FLAT';
                        else:
                            oNewStmt.sName += '_THREADED_PC32_FLAT_WITH_FLAGS';

                    # This is making the wrong branch of conditionals break out of the TB.
                    if (oStmt.sName in ('IEM_MC_ADVANCE_RIP_AND_FINISH', 'IEM_MC_REL_JMP_S8_AND_FINISH',
                                        'IEM_MC_REL_JMP_S16_AND_FINISH', 'IEM_MC_REL_JMP_S32_AND_FINISH')):
                        sExitTbStatus = 'VINF_SUCCESS';
                        if self.sVariation in self.kdVariationsWithConditional:
                            if self.sVariation in self.kdVariationsWithConditionalNoJmp:
                                if oStmt.sName != 'IEM_MC_ADVANCE_RIP_AND_FINISH':
                                    sExitTbStatus = 'VINF_IEM_REEXEC_BREAK';
                            elif oStmt.sName == 'IEM_MC_ADVANCE_RIP_AND_FINISH':
                                sExitTbStatus = 'VINF_IEM_REEXEC_BREAK';
                        oNewStmt.asParams.append(sExitTbStatus);

                    # Insert an MC so we can assert the correctioness of modified flags annotations on IEM_MC_REF_EFLAGS.
                    if 'IEM_MC_ASSERT_EFLAGS' in dState:
                        aoThreadedStmts.insert(len(aoThreadedStmts) - 1,
                                               iai.McStmtAssertEFlags(self.oParent.oMcBlock.oInstruction));
                        del dState['IEM_MC_ASSERT_EFLAGS'];

                # ... and IEM_MC_*_GREG_U8 into *_THREADED w/ reworked index taking REX into account
                elif oNewStmt.sName.startswith('IEM_MC_') and oNewStmt.sName.find('_GREG_U8') > 0:
                    (idxReg, _, sStdRef) = self.analyze8BitGRegStmt(oStmt); # Don't use oNewStmt as it has been modified!
                    oNewStmt.asParams[idxReg] = self.dParamRefs[sStdRef][0].sNewName;
                    oNewStmt.sName += '_THREADED';

                # ... and IEM_MC_CALL_CIMPL_[0-5] and IEM_MC_DEFER_TO_CIMPL_[0-5]_RET into *_THREADED ...
                elif oNewStmt.sName.startswith('IEM_MC_CALL_CIMPL_') or oNewStmt.sName.startswith('IEM_MC_DEFER_TO_CIMPL_'):
                    oNewStmt.sName     += '_THREADED';
                    oNewStmt.idxFn     += 1;
                    oNewStmt.idxParams += 1;
                    oNewStmt.asParams.insert(0, self.dParamRefs['cbInstr'][0].sNewName);

                # ... and in FLAT modes we must morph memory access into FLAT accesses ...
                elif (    self.sVariation in self.kdVariationsWithFlatAddress
                      and (   oNewStmt.sName.startswith('IEM_MC_FETCH_MEM')
                           or (oNewStmt.sName.startswith('IEM_MC_STORE_MEM_') and oNewStmt.sName.find('_BY_REF') < 0)
                           or oNewStmt.sName.startswith('IEM_MC_MEM_MAP') )):
                    idxEffSeg = self.kdMemMcToFlatInfo[oNewStmt.sName][0];
                    if idxEffSeg != -1:
                        if (    oNewStmt.asParams[idxEffSeg].find('iEffSeg') < 0
                            and oNewStmt.asParams[idxEffSeg] not in ('X86_SREG_ES', ) ):
                            self.raiseProblem('Expected iEffSeg as param #%d to %s: %s'
                                              % (idxEffSeg + 1, oNewStmt.sName, oNewStmt.asParams[idxEffSeg],));
                        oNewStmt.asParams.pop(idxEffSeg);
                    oNewStmt.sName = self.kdMemMcToFlatInfo[oNewStmt.sName][1];

                # ... PUSH and POP also needs flat variants, but these differ a little.
                elif (    self.sVariation in self.kdVariationsWithFlatStackAddress
                      and (   (oNewStmt.sName.startswith('IEM_MC_PUSH') and oNewStmt.sName.find('_FPU') < 0)
                           or oNewStmt.sName.startswith('IEM_MC_POP'))):
                    oNewStmt.sName = self.kdMemMcToFlatInfoStack[oNewStmt.sName][int(self.sVariation in
                                                                                     self.kdVariationsWithFlat64StackAddress)];

                # Add EFLAGS usage annotations to relevant MCs.
                elif oNewStmt.sName in ('IEM_MC_COMMIT_EFLAGS', 'IEM_MC_COMMIT_EFLAGS_OPT', 'IEM_MC_REF_EFLAGS',
                                        'IEM_MC_FETCH_EFLAGS'):
                    oInstruction = self.oParent.oMcBlock.oInstruction;
                    oNewStmt.sName += '_EX';
                    oNewStmt.asParams.append(oInstruction.getTestedFlagsCStyle());   # Shall crash and burn if oInstruction is
                    oNewStmt.asParams.append(oInstruction.getModifiedFlagsCStyle()); # None.  Fix the IEM decoder code.

                    # For IEM_MC_REF_EFLAGS we to emit an MC before the ..._FINISH
                    if oNewStmt.sName == 'IEM_MC_REF_EFLAGS_EX':
                        dState['IEM_MC_ASSERT_EFLAGS'] = iLevel;

                # Process branches of conditionals recursively.
                if isinstance(oStmt, iai.McStmtCond):
                    (oNewStmt.aoIfBranch, iParamRef) = self.analyzeMorphStmtForThreaded(oStmt.aoIfBranch, dState,
                                                                                        iParamRef, iLevel + 1);
                    if oStmt.aoElseBranch:
                        (oNewStmt.aoElseBranch, iParamRef) = self.analyzeMorphStmtForThreaded(oStmt.aoElseBranch,
                                                                                              dState, iParamRef, iLevel + 1);

        # Insert an MC so we can assert the correctioness of modified flags annotations
        # on IEM_MC_REF_EFLAGS if it goes out of scope.
        if dState.get('IEM_MC_ASSERT_EFLAGS', -1) == iLevel:
            aoThreadedStmts.append(iai.McStmtAssertEFlags(self.oParent.oMcBlock.oInstruction));
            del dState['IEM_MC_ASSERT_EFLAGS'];

        return (aoThreadedStmts, iParamRef);


    def analyzeConsolidateThreadedParamRefs(self):
        """
        Consolidate threaded function parameter references into a dictionary
        with lists of the references to each variable/field.
        """
        # Gather unique parameters.
        self.dParamRefs = {};
        for oRef in self.aoParamRefs:
            if oRef.sStdRef not in self.dParamRefs:
                self.dParamRefs[oRef.sStdRef] = [oRef,];
            else:
                self.dParamRefs[oRef.sStdRef].append(oRef);

        # Generate names for them for use in the threaded function.
        dParamNames = {};
        for sName, aoRefs in self.dParamRefs.items():
            # Morph the reference expression into a name.
            if sName.startswith('IEM_GET_MODRM_REG'):           sName = 'bModRmRegP';
            elif sName.startswith('IEM_GET_MODRM_RM'):          sName = 'bModRmRmP';
            elif sName.startswith('IEM_GET_MODRM_REG_8'):       sName = 'bModRmReg8P';
            elif sName.startswith('IEM_GET_MODRM_RM_8'):        sName = 'bModRmRm8P';
            elif sName.startswith('IEM_GET_EFFECTIVE_VVVV'):    sName = 'bEffVvvvP';
            elif sName.startswith('IEM_GET_IMM8_REG'):          sName = 'bImm8Reg';
            elif sName.find('.') >= 0 or sName.find('->') >= 0:
                sName = sName[max(sName.rfind('.'), sName.rfind('>')) + 1 : ] + 'P';
            else:
                sName += 'P';

            # Ensure it's unique.
            if sName in dParamNames:
                for i in range(10):
                    if sName + str(i) not in dParamNames:
                        sName += str(i);
                        break;
            dParamNames[sName] = True;

            # Update all the references.
            for oRef in aoRefs:
                oRef.sNewName = sName;

        # Organize them by size too for the purpose of optimize them.
        dBySize = {}        # type: Dict[str, str]
        for sStdRef, aoRefs in self.dParamRefs.items():
            if aoRefs[0].sType[0] != 'P':
                cBits = g_kdTypeInfo[aoRefs[0].sType][0];
                assert(cBits <= 64);
            else:
                cBits = 64;

            if cBits not in dBySize:
                dBySize[cBits] = [sStdRef,]
            else:
                dBySize[cBits].append(sStdRef);

        # Pack the parameters as best as we can, starting with the largest ones
        # and ASSUMING a 64-bit parameter size.
        self.cMinParams = 0;
        offNewParam     = 0;
        for cBits in sorted(dBySize.keys(), reverse = True):
            for sStdRef in dBySize[cBits]:
                if offNewParam == 0 or offNewParam + cBits > 64:
                    self.cMinParams += 1;
                    offNewParam      = cBits;
                else:
                    offNewParam     += cBits;
                assert(offNewParam <= 64);

                for oRef in self.dParamRefs[sStdRef]:
                    oRef.iNewParam   = self.cMinParams - 1;
                    oRef.offNewParam = offNewParam - cBits;

        # Currently there are a few that requires 4 parameters, list these so we can figure out why:
        if self.cMinParams >= 4:
            print('debug: cMinParams=%s cRawParams=%s - %s:%d'
                  % (self.cMinParams, len(self.dParamRefs), self.oParent.oMcBlock.sSrcFile, self.oParent.oMcBlock.iBeginLine,));

        return True;

    ksHexDigits = '0123456789abcdefABCDEF';

    def analyzeFindThreadedParamRefs(self, aoStmts): # pylint: disable=too-many-statements
        """
        Scans the statements for things that have to passed on to the threaded
        function (populates self.aoParamRefs).
        """
        for oStmt in aoStmts:
            # Some statements we can skip alltogether.
            if isinstance(oStmt, iai.McCppPreProc):
                continue;
            if oStmt.isCppStmt() and oStmt.fDecode:
                continue;
            if oStmt.sName in ('IEM_MC_BEGIN',):
                continue;

            if isinstance(oStmt, iai.McStmtVar):
                if oStmt.sValue is None:
                    continue;
                aiSkipParams = { 0: True, 1: True, 3: True };
            else:
                aiSkipParams = {};

            # Several statements have implicit parameters and some have different parameters.
            if oStmt.sName in ('IEM_MC_ADVANCE_RIP_AND_FINISH', 'IEM_MC_REL_JMP_S8_AND_FINISH', 'IEM_MC_REL_JMP_S16_AND_FINISH',
                               'IEM_MC_REL_JMP_S32_AND_FINISH',
                               'IEM_MC_REL_CALL_S16_AND_FINISH', 'IEM_MC_REL_CALL_S32_AND_FINISH',
                               'IEM_MC_REL_CALL_S64_AND_FINISH',
                               'IEM_MC_IND_CALL_U16_AND_FINISH', 'IEM_MC_IND_CALL_U32_AND_FINISH',
                               'IEM_MC_IND_CALL_U64_AND_FINISH',
                               'IEM_MC_RETN_AND_FINISH',
                               'IEM_MC_CALL_CIMPL_0', 'IEM_MC_CALL_CIMPL_1', 'IEM_MC_CALL_CIMPL_2', 'IEM_MC_CALL_CIMPL_3',
                               'IEM_MC_CALL_CIMPL_4', 'IEM_MC_CALL_CIMPL_5',
                               'IEM_MC_DEFER_TO_CIMPL_0_RET', 'IEM_MC_DEFER_TO_CIMPL_1_RET', 'IEM_MC_DEFER_TO_CIMPL_2_RET',
                               'IEM_MC_DEFER_TO_CIMPL_3_RET', 'IEM_MC_DEFER_TO_CIMPL_4_RET', 'IEM_MC_DEFER_TO_CIMPL_5_RET', ):
                self.aoParamRefs.append(ThreadedParamRef('IEM_GET_INSTR_LEN(pVCpu)', 'uint4_t', oStmt, sStdRef = 'cbInstr'));

            if (    oStmt.sName in ('IEM_MC_REL_JMP_S8_AND_FINISH', 'IEM_MC_RETN_AND_FINISH', )
                and self.sVariation not in self.kdVariationsOnlyPre386):
                self.aoParamRefs.append(ThreadedParamRef('pVCpu->iem.s.enmEffOpSize', 'IEMMODE', oStmt));

            if oStmt.sName == 'IEM_MC_CALC_RM_EFF_ADDR':
                # This is being pretty presumptive about bRm always being the RM byte...
                assert len(oStmt.asParams) == 3;
                assert oStmt.asParams[1] == 'bRm';

                if self.sVariation in self.kdVariationsWithFlatAddr16:
                    self.aoParamRefs.append(ThreadedParamRef('bRm',     'uint8_t',  oStmt));
                    self.aoParamRefs.append(ThreadedParamRef('(uint16_t)uEffAddrInfo' ,
                                                             'uint16_t', oStmt, sStdRef = 'u16Disp'));
                elif self.sVariation in self.kdVariationsWithFlatAddr32No64:
                    self.aoParamRefs.append(ThreadedParamRef('bRm',     'uint8_t',  oStmt));
                    self.aoParamRefs.append(ThreadedParamRef('(uint8_t)(uEffAddrInfo >> 32)',
                                                             'uint8_t',  oStmt, sStdRef = 'bSib'));
                    self.aoParamRefs.append(ThreadedParamRef('(uint32_t)uEffAddrInfo',
                                                             'uint32_t', oStmt, sStdRef = 'u32Disp'));
                else:
                    assert self.sVariation in self.kdVariationsWithAddressOnly64;
                    self.aoParamRefs.append(ThreadedParamRef('IEM_GET_MODRM_EX(pVCpu, bRm)',
                                                             'uint8_t',  oStmt, sStdRef = 'bRmEx'));
                    self.aoParamRefs.append(ThreadedParamRef('(uint8_t)(uEffAddrInfo >> 32)',
                                                             'uint8_t',  oStmt, sStdRef = 'bSib'));
                    self.aoParamRefs.append(ThreadedParamRef('(uint32_t)uEffAddrInfo',
                                                             'uint32_t', oStmt, sStdRef = 'u32Disp'));
                    self.aoParamRefs.append(ThreadedParamRef('IEM_GET_INSTR_LEN(pVCpu)',
                                                             'uint4_t',  oStmt, sStdRef = 'cbInstr'));
                    aiSkipParams[1] = True; # Skip the bRm parameter as it is being replaced by bRmEx.

            # 8-bit register accesses needs to have their index argument reworked to take REX into account.
            if oStmt.sName.startswith('IEM_MC_') and oStmt.sName.find('_GREG_U8') > 0:
                (idxReg, sOrgRef, sStdRef) = self.analyze8BitGRegStmt(oStmt);
                self.aoParamRefs.append(ThreadedParamRef(sOrgRef, 'uint8_t', oStmt, idxReg, sStdRef = sStdRef));
                aiSkipParams[idxReg] = True; # Skip the parameter below.

            # If in flat mode variation, ignore the effective segment parameter to memory MCs.
            if (    self.sVariation in self.kdVariationsWithFlatAddress
                and oStmt.sName in self.kdMemMcToFlatInfo
                and self.kdMemMcToFlatInfo[oStmt.sName][0] != -1):
                aiSkipParams[self.kdMemMcToFlatInfo[oStmt.sName][0]] = True;

            # Inspect the target of calls to see if we need to pass down a
            # function pointer or function table pointer for it to work.
            if isinstance(oStmt, iai.McStmtCall):
                if oStmt.sFn[0] == 'p':
                    self.aoParamRefs.append(ThreadedParamRef(oStmt.sFn, self.analyzeCallToType(oStmt.sFn), oStmt, oStmt.idxFn));
                elif (    oStmt.sFn[0] != 'i'
                      and not oStmt.sFn.startswith('RT_CONCAT3')
                      and not oStmt.sFn.startswith('IEMTARGETCPU_EFL_BEHAVIOR_SELECT')
                      and not oStmt.sFn.startswith('IEM_SELECT_HOST_OR_FALLBACK') ):
                    self.raiseProblem('Bogus function name in %s: %s' % (oStmt.sName, oStmt.sFn,));
                aiSkipParams[oStmt.idxFn] = True;

                # Skip the hint parameter (first) for IEM_MC_CALL_CIMPL_X.
                if oStmt.sName.startswith('IEM_MC_CALL_CIMPL_'):
                    assert oStmt.idxFn == 2;
                    aiSkipParams[0] = True;

            # Skip the function parameter (first) for IEM_MC_NATIVE_EMIT_X.
            if oStmt.sName.startswith('IEM_MC_NATIVE_EMIT_'):
                aiSkipParams[0] = True;


            # Check all the parameters for bogus references.
            for iParam, sParam in enumerate(oStmt.asParams):
                if iParam not in aiSkipParams  and  sParam not in self.oParent.dVariables:
                    # The parameter may contain a C expression, so we have to try
                    # extract the relevant bits, i.e. variables and fields while
                    # ignoring operators and parentheses.
                    offParam = 0;
                    while offParam < len(sParam):
                        # Is it the start of an C identifier? If so, find the end, but don't stop on field separators (->, .).
                        ch = sParam[offParam];
                        if ch.isalpha() or ch == '_':
                            offStart = offParam;
                            offParam += 1;
                            while offParam < len(sParam):
                                ch = sParam[offParam];
                                if not ch.isalnum() and ch != '_' and ch != '.':
                                    if ch != '-' or sParam[offParam + 1] != '>':
                                        # Special hack for the 'CTX_SUFF(pVM)' bit in pVCpu->CTX_SUFF(pVM)->xxxx:
                                        if (    ch == '('
                                            and sParam[offStart : offParam + len('(pVM)->')] == 'pVCpu->CTX_SUFF(pVM)->'):
                                            offParam += len('(pVM)->') - 1;
                                        else:
                                            break;
                                    offParam += 1;
                                offParam += 1;
                            sRef = sParam[offStart : offParam];

                            # For register references, we pass the full register indexes instead as macros
                            # like IEM_GET_MODRM_REG implicitly references pVCpu->iem.s.uRexReg and the
                            # threaded function will be more efficient if we just pass the register index
                            # as a 4-bit param.
                            if (   sRef.startswith('IEM_GET_MODRM')
                                or sRef.startswith('IEM_GET_EFFECTIVE_VVVV')
                                or sRef.startswith('IEM_GET_IMM8_REG') ):
                                offParam = iai.McBlock.skipSpacesAt(sParam, offParam, len(sParam));
                                if sParam[offParam] != '(':
                                    self.raiseProblem('Expected "(" following %s in "%s"' % (sRef, oStmt.renderCode(),));
                                (asMacroParams, offCloseParam) = iai.McBlock.extractParams(sParam, offParam);
                                if asMacroParams is None:
                                    self.raiseProblem('Unable to find ")" for %s in "%s"' % (sRef, oStmt.renderCode(),));
                                offParam = offCloseParam + 1;
                                self.aoParamRefs.append(ThreadedParamRef(sParam[offStart : offParam], 'uint8_t',
                                                                         oStmt, iParam, offStart));

                            # We can skip known variables.
                            elif sRef in self.oParent.dVariables:
                                pass;

                            # Skip certain macro invocations.
                            elif sRef in ('IEM_GET_HOST_CPU_FEATURES',
                                          'IEM_GET_GUEST_CPU_FEATURES',
                                          'IEM_IS_GUEST_CPU_AMD',
                                          'IEM_IS_16BIT_CODE',
                                          'IEM_IS_32BIT_CODE',
                                          'IEM_IS_64BIT_CODE',
                                          ):
                                offParam = iai.McBlock.skipSpacesAt(sParam, offParam, len(sParam));
                                if sParam[offParam] != '(':
                                    self.raiseProblem('Expected "(" following %s in "%s"' % (sRef, oStmt.renderCode(),));
                                (asMacroParams, offCloseParam) = iai.McBlock.extractParams(sParam, offParam);
                                if asMacroParams is None:
                                    self.raiseProblem('Unable to find ")" for %s in "%s"' % (sRef, oStmt.renderCode(),));
                                offParam = offCloseParam + 1;

                                # Skip any dereference following it, unless it's a predicate like IEM_IS_GUEST_CPU_AMD.
                                if sRef not in ('IEM_IS_GUEST_CPU_AMD',
                                                'IEM_IS_16BIT_CODE',
                                                'IEM_IS_32BIT_CODE',
                                                'IEM_IS_64BIT_CODE',
                                                ):
                                    offParam = iai.McBlock.skipSpacesAt(sParam, offParam, len(sParam));
                                    if offParam + 2 <= len(sParam) and sParam[offParam : offParam + 2] == '->':
                                        offParam = iai.McBlock.skipSpacesAt(sParam, offParam + 2, len(sParam));
                                        while offParam < len(sParam) and (sParam[offParam].isalnum() or sParam[offParam] in '_.'):
                                            offParam += 1;

                            # Skip constants, globals, types (casts), sizeof and macros.
                            elif (   sRef.startswith('IEM_OP_PRF_')
                                  or sRef.startswith('IEM_ACCESS_')
                                  or sRef.startswith('IEMINT_')
                                  or sRef.startswith('X86_GREG_')
                                  or sRef.startswith('X86_SREG_')
                                  or sRef.startswith('X86_EFL_')
                                  or sRef.startswith('X86_FSW_')
                                  or sRef.startswith('X86_FCW_')
                                  or sRef.startswith('X86_XCPT_')
                                  or sRef.startswith('IEMMODE_')
                                  or sRef.startswith('IEM_F_')
                                  or sRef.startswith('IEM_CIMPL_F_')
                                  or sRef.startswith('g_')
                                  or sRef.startswith('iemAImpl_')
                                  or sRef.startswith('kIemNativeGstReg_')
                                  or sRef.startswith('RT_ARCH_VAL_')
                                  or sRef in ( 'int8_t',    'int16_t',    'int32_t',    'int64_t',
                                               'INT8_C',    'INT16_C',    'INT32_C',    'INT64_C',
                                               'uint8_t',   'uint16_t',   'uint32_t',   'uint64_t',
                                               'UINT8_C',   'UINT16_C',   'UINT32_C',   'UINT64_C',
                                               'UINT8_MAX', 'UINT16_MAX', 'UINT32_MAX', 'UINT64_MAX',
                                               'INT8_MAX',  'INT16_MAX',  'INT32_MAX',  'INT64_MAX',
                                               'INT8_MIN',  'INT16_MIN',  'INT32_MIN',  'INT64_MIN',
                                               'sizeof',    'NOREF',      'RT_NOREF',   'IEMMODE_64BIT',
                                               'RT_BIT_32', 'RT_BIT_64',  'true',       'false',
                                               'NIL_RTGCPTR',) ):
                                pass;

                            # Skip certain macro invocations.
                            # Any variable (non-field) and decoder fields in IEMCPU will need to be parameterized.
                            elif (   (    '.' not in sRef
                                      and '-' not in sRef
                                      and sRef not in ('pVCpu', ) )
                                  or iai.McBlock.koReIemDecoderVars.search(sRef) is not None):
                                self.aoParamRefs.append(ThreadedParamRef(sRef, self.analyzeReferenceToType(sRef),
                                                                         oStmt, iParam, offStart));
                        # Number.
                        elif ch.isdigit():
                            if (    ch == '0'
                                and offParam + 2 <= len(sParam)
                                and sParam[offParam + 1] in 'xX'
                                and sParam[offParam + 2] in self.ksHexDigits ):
                                offParam += 2;
                                while offParam < len(sParam) and sParam[offParam] in self.ksHexDigits:
                                    offParam += 1;
                            else:
                                while offParam < len(sParam) and sParam[offParam].isdigit():
                                    offParam += 1;
                        # Comment?
                        elif (    ch == '/'
                              and offParam + 4 <= len(sParam)
                              and sParam[offParam + 1] == '*'):
                            offParam += 2;
                            offNext = sParam.find('*/', offParam);
                            if offNext < offParam:
                                self.raiseProblem('Unable to find "*/" in "%s" ("%s")' % (sRef, oStmt.renderCode(),));
                            offParam = offNext + 2;
                        # Whatever else.
                        else:
                            offParam += 1;

            # Traverse the branches of conditionals.
            if isinstance(oStmt, iai.McStmtCond):
                self.analyzeFindThreadedParamRefs(oStmt.aoIfBranch);
                self.analyzeFindThreadedParamRefs(oStmt.aoElseBranch);
        return True;

    def analyzeVariation(self, aoStmts):
        """
        2nd part of the analysis, done on each variation.

        The variations may differ in parameter requirements and will end up with
        slightly different MC sequences. Thus this is done on each individually.

        Returns dummy True - raises exception on trouble.
        """
        # Now scan the code for variables and field references that needs to
        # be passed to the threaded function because they are related to the
        # instruction decoding.
        self.analyzeFindThreadedParamRefs(aoStmts);
        self.analyzeConsolidateThreadedParamRefs();

        # Morph the statement stream for the block into what we'll be using in the threaded function.
        (self.aoStmtsForThreadedFunction, iParamRef) = self.analyzeMorphStmtForThreaded(aoStmts, {});
        if iParamRef != len(self.aoParamRefs):
            raise Exception('iParamRef=%s, expected %s!' % (iParamRef, len(self.aoParamRefs),));

        return True;

    def emitThreadedCallStmtsForVariant(self, cchIndent, fTbLookupTable = False, sCallVarNm = None):
        """
        Produces generic C++ statments that emits a call to the thread function
        variation and any subsequent checks that may be necessary after that.

        The sCallVarNm is the name of the variable with the threaded function
        to call.  This is for the case where all the variations have the same
        parameters and only the threaded function number differs.

        The fTbLookupTable parameter can either be False, True or whatever else
        (like 2) - in the latte case this means a large lookup table.
        """
        aoStmts = [
            iai.McCppCall('IEM_MC2_BEGIN_EMIT_CALLS',
                          ['1' if 'IEM_CIMPL_F_CHECK_IRQ_BEFORE' in self.oParent.dsCImplFlags else '0'],
                          cchIndent = cchIndent), # Scope and a hook for various stuff.
        ];

        # The call to the threaded function.
        asCallArgs = [ self.getIndexName() if not sCallVarNm else sCallVarNm, ];
        for iParam in range(self.cMinParams):
            asFrags = [];
            for aoRefs in self.dParamRefs.values():
                oRef = aoRefs[0];
                if oRef.iNewParam == iParam:
                    sCast = '(uint64_t)'
                    if oRef.sType in ('int8_t', 'int16_t', 'int32_t'): # Make sure these doesn't get sign-extended.
                        sCast = '(uint64_t)(u' + oRef.sType + ')';
                    if oRef.offNewParam == 0:
                        asFrags.append(sCast + '(' + oRef.sOrgRef + ')');
                    else:
                        asFrags.append('(%s(%s) << %s)' % (sCast, oRef.sOrgRef, oRef.offNewParam));
            assert asFrags;
            asCallArgs.append(' | '.join(asFrags));

        if fTbLookupTable is False:
            aoStmts.append(iai.McCppCall('IEM_MC2_EMIT_CALL_%s' % (len(asCallArgs) - 1,),
                                         asCallArgs, cchIndent = cchIndent));
        else:
            aoStmts.append(iai.McCppCall('IEM_MC2_EMIT_CALL_WITH_TB_LOOKUP_%s' % (len(asCallArgs) - 1,),
                                         ['0' if fTbLookupTable is True else '1',] + asCallArgs, cchIndent = cchIndent));

        # 2023-11-28: This has to be done AFTER the CIMPL call, so we have to
        #             emit this mode check from the compilation loop.  On the
        #             plus side, this means we eliminate unnecessary call at
        #             end of the TB. :-)
        ## For CIMPL stuff, we need to consult the associated IEM_CIMPL_F_XXX
        ## mask and maybe emit additional checks.
        #if (   'IEM_CIMPL_F_MODE'   in self.oParent.dsCImplFlags
        #    or 'IEM_CIMPL_F_XCPT'   in self.oParent.dsCImplFlags
        #    or 'IEM_CIMPL_F_VMEXIT' in self.oParent.dsCImplFlags):
        #    aoStmts.append(iai.McCppCall('IEM_MC2_EMIT_CALL_1', ( 'kIemThreadedFunc_BltIn_CheckMode', 'pVCpu->iem.s.fExec', ),
        #                                 cchIndent = cchIndent));

        sCImplFlags = ' | '.join(self.oParent.dsCImplFlags.keys());
        if not sCImplFlags:
            sCImplFlags = '0'
        aoStmts.append(iai.McCppCall('IEM_MC2_END_EMIT_CALLS', ( sCImplFlags, ), cchIndent = cchIndent)); # For closing the scope.

        # Emit fEndTb = true or fTbBranched = true if any of the CIMPL flags
        # indicates we should do so.
        # Note! iemThreadedRecompilerMcDeferToCImpl0 duplicates work done here.
        asEndTbFlags      = [];
        asTbBranchedFlags = [];
        for sFlag in self.oParent.dsCImplFlags:
            if self.kdCImplFlags[sFlag] is True:
                asEndTbFlags.append(sFlag);
            elif sFlag.startswith('IEM_CIMPL_F_BRANCH_'):
                asTbBranchedFlags.append(sFlag);
        if (    asTbBranchedFlags
            and (   'IEM_CIMPL_F_BRANCH_CONDITIONAL' not in asTbBranchedFlags
                 or self.sVariation not in self.kdVariationsWithConditionalNoJmp)):
            aoStmts.append(iai.McCppGeneric('iemThreadedSetBranched(pVCpu, %s);'
                                            % ((' | '.join(asTbBranchedFlags)).replace('IEM_CIMPL_F_BRANCH', 'IEMBRANCHED_F'),),
                                            cchIndent = cchIndent)); # Inline fn saves ~2 seconds for gcc 13/dbg (1m13s vs 1m15s).
        if asEndTbFlags:
            aoStmts.append(iai.McCppGeneric('pVCpu->iem.s.fEndTb = true; /* %s */' % (','.join(asEndTbFlags),),
                                            cchIndent = cchIndent));

        if 'IEM_CIMPL_F_CHECK_IRQ_AFTER' in self.oParent.dsCImplFlags:
            aoStmts.append(iai.McCppGeneric('pVCpu->iem.s.cInstrTillIrqCheck = 0;', cchIndent = cchIndent));

        return aoStmts;


class ThreadedFunction(object):
    """
    A threaded function.
    """

    def __init__(self, oMcBlock: iai.McBlock) -> None:
        self.oMcBlock       = oMcBlock      # type: iai.McBlock
        # The remaining fields are only useful after analyze() has been called:
        ## Variations for this block. There is at least one.
        self.aoVariations   = []            # type: List[ThreadedFunctionVariation]
        ## Variation dictionary containing the same as aoVariations.
        self.dVariations    = {}            # type: Dict[str, ThreadedFunctionVariation]
        ## Dictionary of local variables (IEM_MC_LOCAL[_CONST]) and call arguments (IEM_MC_ARG*).
        self.dVariables     = {}            # type: Dict[str, iai.McStmtVar]
        ## Dictionary with any IEM_CIMPL_F_XXX flags explicitly advertised in the code block
        ## and those determined by analyzeCodeOperation().
        self.dsCImplFlags   = {}            # type: Dict[str, bool]
        ## The unique sub-name for this threaded function.
        self.sSubName       = '';
        #if oMcBlock.iInFunction > 0 or (oMcBlock.oInstruction and len(oMcBlock.oInstruction.aoMcBlocks) > 1):
        #    self.sSubName  = '_%s' % (oMcBlock.iInFunction);

    @staticmethod
    def dummyInstance():
        """ Gets a dummy instance. """
        return ThreadedFunction(iai.McBlock('null', 999999999, 999999999,
                                            iai.DecoderFunction('null', 999999999, 'nil', ('','')), 999999999));

    def hasWithFlagsCheckingAndClearingVariation(self):
        """
        Check if there is one or more with flags checking and clearing
        variations for this threaded function.
        """
        for sVarWithFlags in ThreadedFunctionVariation.kdVariationsWithEflagsCheckingAndClearing:
            if sVarWithFlags in self.dVariations:
                return True;
        return False;

    #
    # Analysis and code morphing.
    #

    def raiseProblem(self, sMessage):
        """ Raises a problem. """
        raise Exception('%s:%s: error: %s' % (self.oMcBlock.sSrcFile, self.oMcBlock.iBeginLine, sMessage, ));

    def error(self, sMessage, oGenerator):
        """ Emits an error via the generator object, causing it to fail. """
        oGenerator.rawError('%s:%s: error: %s' % (self.oMcBlock.sSrcFile, self.oMcBlock.iBeginLine, sMessage, ));

    def warning(self, sMessage):
        """ Emits a warning. """
        print('%s:%s: warning: %s' % (self.oMcBlock.sSrcFile, self.oMcBlock.iBeginLine, sMessage, ));

    ## Used by analyzeAndAnnotateName for memory MC blocks.
    kdAnnotateNameMemStmts = {
        'IEM_MC_FETCH_MEM16_U8':                    '__mem8',
        'IEM_MC_FETCH_MEM32_U8':                    '__mem8',
        'IEM_MC_FETCH_MEM_D80':                     '__mem80',
        'IEM_MC_FETCH_MEM_I16':                     '__mem16',
        'IEM_MC_FETCH_MEM_I32':                     '__mem32',
        'IEM_MC_FETCH_MEM_I64':                     '__mem64',
        'IEM_MC_FETCH_MEM_R32':                     '__mem32',
        'IEM_MC_FETCH_MEM_R64':                     '__mem64',
        'IEM_MC_FETCH_MEM_R80':                     '__mem80',
        'IEM_MC_FETCH_MEM_U128':                    '__mem128',
        'IEM_MC_FETCH_MEM_U128_ALIGN_SSE':          '__mem128',
        'IEM_MC_FETCH_MEM_U128_NO_AC':              '__mem128',
        'IEM_MC_FETCH_MEM_U16':                     '__mem16',
        'IEM_MC_FETCH_MEM_U16_DISP':                '__mem16',
        'IEM_MC_FETCH_MEM_U16_SX_U32':              '__mem16sx32',
        'IEM_MC_FETCH_MEM_U16_SX_U64':              '__mem16sx64',
        'IEM_MC_FETCH_MEM_U16_ZX_U32':              '__mem16zx32',
        'IEM_MC_FETCH_MEM_U16_ZX_U64':              '__mem16zx64',
        'IEM_MC_FETCH_MEM_U256':                    '__mem256',
        'IEM_MC_FETCH_MEM_U256_ALIGN_AVX':          '__mem256',
        'IEM_MC_FETCH_MEM_U256_NO_AC':              '__mem256',
        'IEM_MC_FETCH_MEM_U32':                     '__mem32',
        'IEM_MC_FETCH_MEM_U32_DISP':                '__mem32',
        'IEM_MC_FETCH_MEM_U32_SX_U64':              '__mem32sx64',
        'IEM_MC_FETCH_MEM_U32_ZX_U64':              '__mem32zx64',
        'IEM_MC_FETCH_MEM_U64':                     '__mem64',
        'IEM_MC_FETCH_MEM_U64_ALIGN_U128':          '__mem64',
        'IEM_MC_FETCH_MEM_U64_DISP':                '__mem64',
        'IEM_MC_FETCH_MEM_U8':                      '__mem8',
        'IEM_MC_FETCH_MEM_U8_DISP':                 '__mem8',
        'IEM_MC_FETCH_MEM_U8_SX_U16':               '__mem8sx16',
        'IEM_MC_FETCH_MEM_U8_SX_U32':               '__mem8sx32',
        'IEM_MC_FETCH_MEM_U8_SX_U64':               '__mem8sx64',
        'IEM_MC_FETCH_MEM_U8_ZX_U16':               '__mem8zx16',
        'IEM_MC_FETCH_MEM_U8_ZX_U32':               '__mem8zx32',
        'IEM_MC_FETCH_MEM_U8_ZX_U64':               '__mem8zx64',
        'IEM_MC_FETCH_MEM_XMM':                     '__mem128',
        'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE':           '__mem128',
        'IEM_MC_FETCH_MEM_XMM_NO_AC':               '__mem128',
        'IEM_MC_FETCH_MEM_XMM_U32':                 '__mem32',
        'IEM_MC_FETCH_MEM_XMM_U64':                 '__mem64',
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128':      '__mem128',
        'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM': '__mem128',
        'IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM':    '__mem32',
        'IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM':    '__mem64',
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64': '__mem128',
        'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64': '__mem128',

        'IEM_MC_STORE_MEM_I16_CONST_BY_REF':        '__mem16',
        'IEM_MC_STORE_MEM_I32_CONST_BY_REF':        '__mem32',
        'IEM_MC_STORE_MEM_I64_CONST_BY_REF':        '__mem64',
        'IEM_MC_STORE_MEM_I8_CONST_BY_REF':         '__mem8',
        'IEM_MC_STORE_MEM_INDEF_D80_BY_REF':        '__mem80',
        'IEM_MC_STORE_MEM_NEG_QNAN_R32_BY_REF':     '__mem32',
        'IEM_MC_STORE_MEM_NEG_QNAN_R64_BY_REF':     '__mem64',
        'IEM_MC_STORE_MEM_NEG_QNAN_R80_BY_REF':     '__mem80',
        'IEM_MC_STORE_MEM_U128':                    '__mem128',
        'IEM_MC_STORE_MEM_U128_ALIGN_SSE':          '__mem128',
        'IEM_MC_STORE_MEM_U128_NO_AC':              '__mem128',
        'IEM_MC_STORE_MEM_U16':                     '__mem16',
        'IEM_MC_STORE_MEM_U16_CONST':               '__mem16c',
        'IEM_MC_STORE_MEM_U256':                    '__mem256',
        'IEM_MC_STORE_MEM_U256_ALIGN_AVX':          '__mem256',
        'IEM_MC_STORE_MEM_U256_NO_AC':              '__mem256',
        'IEM_MC_STORE_MEM_U32':                     '__mem32',
        'IEM_MC_STORE_MEM_U32_CONST':               '__mem32c',
        'IEM_MC_STORE_MEM_U64':                     '__mem64',
        'IEM_MC_STORE_MEM_U64_CONST':               '__mem64c',
        'IEM_MC_STORE_MEM_U8':                      '__mem8',
        'IEM_MC_STORE_MEM_U8_CONST':                '__mem8c',

        'IEM_MC_MEM_MAP_D80_WO':                    '__mem80',
        'IEM_MC_MEM_MAP_I16_WO':                    '__mem16',
        'IEM_MC_MEM_MAP_I32_WO':                    '__mem32',
        'IEM_MC_MEM_MAP_I64_WO':                    '__mem64',
        'IEM_MC_MEM_MAP_R32_WO':                    '__mem32',
        'IEM_MC_MEM_MAP_R64_WO':                    '__mem64',
        'IEM_MC_MEM_MAP_R80_WO':                    '__mem80',
        'IEM_MC_MEM_MAP_U128_ATOMIC':               '__mem128a',
        'IEM_MC_MEM_MAP_U128_RO':                   '__mem128',
        'IEM_MC_MEM_MAP_U128_RW':                   '__mem128',
        'IEM_MC_MEM_MAP_U128_WO':                   '__mem128',
        'IEM_MC_MEM_MAP_U16_ATOMIC':                '__mem16a',
        'IEM_MC_MEM_MAP_U16_RO':                    '__mem16',
        'IEM_MC_MEM_MAP_U16_RW':                    '__mem16',
        'IEM_MC_MEM_MAP_U16_WO':                    '__mem16',
        'IEM_MC_MEM_MAP_U32_ATOMIC':                '__mem32a',
        'IEM_MC_MEM_MAP_U32_RO':                    '__mem32',
        'IEM_MC_MEM_MAP_U32_RW':                    '__mem32',
        'IEM_MC_MEM_MAP_U32_WO':                    '__mem32',
        'IEM_MC_MEM_MAP_U64_ATOMIC':                '__mem64a',
        'IEM_MC_MEM_MAP_U64_RO':                    '__mem64',
        'IEM_MC_MEM_MAP_U64_RW':                    '__mem64',
        'IEM_MC_MEM_MAP_U64_WO':                    '__mem64',
        'IEM_MC_MEM_MAP_U8_ATOMIC':                 '__mem8a',
        'IEM_MC_MEM_MAP_U8_RO':                     '__mem8',
        'IEM_MC_MEM_MAP_U8_RW':                     '__mem8',
        'IEM_MC_MEM_MAP_U8_WO':                     '__mem8',
    };
    ## Used by analyzeAndAnnotateName for non-memory MC blocks.
    kdAnnotateNameRegStmts = {
        'IEM_MC_FETCH_GREG_U8':                     '__greg8',
        'IEM_MC_FETCH_GREG_U8_ZX_U16':              '__greg8zx16',
        'IEM_MC_FETCH_GREG_U8_ZX_U32':              '__greg8zx32',
        'IEM_MC_FETCH_GREG_U8_ZX_U64':              '__greg8zx64',
        'IEM_MC_FETCH_GREG_U8_SX_U16':              '__greg8sx16',
        'IEM_MC_FETCH_GREG_U8_SX_U32':              '__greg8sx32',
        'IEM_MC_FETCH_GREG_U8_SX_U64':              '__greg8sx64',
        'IEM_MC_FETCH_GREG_U16':                    '__greg16',
        'IEM_MC_FETCH_GREG_U16_ZX_U32':             '__greg16zx32',
        'IEM_MC_FETCH_GREG_U16_ZX_U64':             '__greg16zx64',
        'IEM_MC_FETCH_GREG_U16_SX_U32':             '__greg16sx32',
        'IEM_MC_FETCH_GREG_U16_SX_U64':             '__greg16sx64',
        'IEM_MC_FETCH_GREG_U32':                    '__greg32',
        'IEM_MC_FETCH_GREG_U32_ZX_U64':             '__greg32zx64',
        'IEM_MC_FETCH_GREG_U32_SX_U64':             '__greg32sx64',
        'IEM_MC_FETCH_GREG_U64':                    '__greg64',
        'IEM_MC_FETCH_GREG_U64_ZX_U64':             '__greg64zx64',
        'IEM_MC_FETCH_GREG_PAIR_U32':               '__greg32',
        'IEM_MC_FETCH_GREG_PAIR_U64':               '__greg64',

        'IEM_MC_STORE_GREG_U8':                     '__greg8',
        'IEM_MC_STORE_GREG_U16':                    '__greg16',
        'IEM_MC_STORE_GREG_U32':                    '__greg32',
        'IEM_MC_STORE_GREG_U64':                    '__greg64',
        'IEM_MC_STORE_GREG_I64':                    '__greg64',
        'IEM_MC_STORE_GREG_U8_CONST':               '__greg8c',
        'IEM_MC_STORE_GREG_U16_CONST':              '__greg16c',
        'IEM_MC_STORE_GREG_U32_CONST':              '__greg32c',
        'IEM_MC_STORE_GREG_U64_CONST':              '__greg64c',
        'IEM_MC_STORE_GREG_PAIR_U32':               '__greg32',
        'IEM_MC_STORE_GREG_PAIR_U64':               '__greg64',

        'IEM_MC_FETCH_SREG_U16':                    '__sreg16',
        'IEM_MC_FETCH_SREG_ZX_U32':                 '__sreg32',
        'IEM_MC_FETCH_SREG_ZX_U64':                 '__sreg64',
        'IEM_MC_FETCH_SREG_BASE_U64':               '__sbase64',
        'IEM_MC_FETCH_SREG_BASE_U32':               '__sbase32',
        'IEM_MC_STORE_SREG_BASE_U64':               '__sbase64',
        'IEM_MC_STORE_SREG_BASE_U32':               '__sbase32',

        'IEM_MC_REF_GREG_U8':                       '__greg8',
        'IEM_MC_REF_GREG_U16':                      '__greg16',
        'IEM_MC_REF_GREG_U32':                      '__greg32',
        'IEM_MC_REF_GREG_U64':                      '__greg64',
        'IEM_MC_REF_GREG_U8_CONST':                 '__greg8',
        'IEM_MC_REF_GREG_U16_CONST':                '__greg16',
        'IEM_MC_REF_GREG_U32_CONST':                '__greg32',
        'IEM_MC_REF_GREG_U64_CONST':                '__greg64',
        'IEM_MC_REF_GREG_I32':                      '__greg32',
        'IEM_MC_REF_GREG_I64':                      '__greg64',
        'IEM_MC_REF_GREG_I32_CONST':                '__greg32',
        'IEM_MC_REF_GREG_I64_CONST':                '__greg64',

        'IEM_MC_STORE_FPUREG_R80_SRC_REF':          '__fpu',
        'IEM_MC_REF_FPUREG':                        '__fpu',

        'IEM_MC_FETCH_MREG_U64':                    '__mreg64',
        'IEM_MC_FETCH_MREG_U32':                    '__mreg32',
        'IEM_MC_FETCH_MREG_U16':                    '__mreg16',
        'IEM_MC_FETCH_MREG_U8':                     '__mreg8',
        'IEM_MC_STORE_MREG_U64':                    '__mreg64',
        'IEM_MC_STORE_MREG_U32':                    '__mreg32',
        'IEM_MC_STORE_MREG_U16':                    '__mreg16',
        'IEM_MC_STORE_MREG_U8':                     '__mreg8',
        'IEM_MC_STORE_MREG_U32_ZX_U64':             '__mreg32zx64',
        'IEM_MC_REF_MREG_U64':                      '__mreg64',
        'IEM_MC_REF_MREG_U64_CONST':                '__mreg64',
        'IEM_MC_REF_MREG_U32_CONST':                '__mreg32',

        'IEM_MC_CLEAR_XREG_U32_MASK':               '__xreg32x4',
        'IEM_MC_FETCH_XREG_U128':                   '__xreg128',
        'IEM_MC_FETCH_XREG_XMM':                    '__xreg128',
        'IEM_MC_FETCH_XREG_U64':                    '__xreg64',
        'IEM_MC_FETCH_XREG_U32':                    '__xreg32',
        'IEM_MC_FETCH_XREG_U16':                    '__xreg16',
        'IEM_MC_FETCH_XREG_U8':                     '__xreg8',
        'IEM_MC_FETCH_XREG_PAIR_U128':              '__xreg128p',
        'IEM_MC_FETCH_XREG_PAIR_XMM':               '__xreg128p',
        'IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64': '__xreg128p',
        'IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64': '__xreg128p',

        'IEM_MC_STORE_XREG_U32_U128':               '__xreg32',
        'IEM_MC_STORE_XREG_U128':                   '__xreg128',
        'IEM_MC_STORE_XREG_XMM':                    '__xreg128',
        'IEM_MC_STORE_XREG_XMM_U32':                '__xreg32',
        'IEM_MC_STORE_XREG_XMM_U64':                '__xreg64',
        'IEM_MC_STORE_XREG_U64':                    '__xreg64',
        'IEM_MC_STORE_XREG_U64_ZX_U128':            '__xreg64zx128',
        'IEM_MC_STORE_XREG_U32':                    '__xreg32',
        'IEM_MC_STORE_XREG_U16':                    '__xreg16',
        'IEM_MC_STORE_XREG_U8':                     '__xreg8',
        'IEM_MC_STORE_XREG_U32_ZX_U128':            '__xreg32zx128',
        'IEM_MC_STORE_XREG_R32':                    '__xreg32',
        'IEM_MC_STORE_XREG_R64':                    '__xreg64',
        'IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX':        '__xreg8zx',
        'IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX':       '__xreg16zx',
        'IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX':       '__xreg32zx',
        'IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX':       '__xreg64zx',
        'IEM_MC_BROADCAST_XREG_U128_ZX_VLMAX':      '__xreg128zx',
        'IEM_MC_REF_XREG_U128':                     '__xreg128',
        'IEM_MC_REF_XREG_U128_CONST':               '__xreg128',
        'IEM_MC_REF_XREG_U32_CONST':                '__xreg32',
        'IEM_MC_REF_XREG_U64_CONST':                '__xreg64',
        'IEM_MC_REF_XREG_R32_CONST':                '__xreg32',
        'IEM_MC_REF_XREG_R64_CONST':                '__xreg64',
        'IEM_MC_REF_XREG_XMM_CONST':                '__xreg128',
        'IEM_MC_COPY_XREG_U128':                    '__xreg128',

        'IEM_MC_FETCH_YREG_U256':                   '__yreg256',
        'IEM_MC_FETCH_YREG_YMM':                    '__yreg256',
        'IEM_MC_FETCH_YREG_U128':                   '__yreg128',
        'IEM_MC_FETCH_YREG_U64':                    '__yreg64',
        'IEM_MC_FETCH_YREG_U32':                    '__yreg32',
        'IEM_MC_STORE_YREG_U128':                   '__yreg128',
        'IEM_MC_STORE_YREG_U32_ZX_VLMAX':           '__yreg32zx',
        'IEM_MC_STORE_YREG_U64_ZX_VLMAX':           '__yreg64zx',
        'IEM_MC_STORE_YREG_U128_ZX_VLMAX':          '__yreg128zx',
        'IEM_MC_STORE_YREG_U256_ZX_VLMAX':          '__yreg256zx',
        'IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX':        '__yreg8',
        'IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX':       '__yreg16',
        'IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX':       '__yreg32',
        'IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX':       '__yreg64',
        'IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX':      '__yreg128',
        'IEM_MC_REF_YREG_U128':                     '__yreg128',
        'IEM_MC_REF_YREG_U128_CONST':               '__yreg128',
        'IEM_MC_REF_YREG_U64_CONST':                '__yreg64',
        'IEM_MC_COPY_YREG_U256_ZX_VLMAX':           '__yreg256zx',
        'IEM_MC_COPY_YREG_U128_ZX_VLMAX':           '__yreg128zx',
        'IEM_MC_COPY_YREG_U64_ZX_VLMAX':            '__yreg64zx',
        'IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX':       '__yreg3296',
        'IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX':       '__yreg6464',
        'IEM_MC_MERGE_YREG_U64HI_U64HI_ZX_VLMAX':   '__yreg64hi64hi',
        'IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX':   '__yreg64lo64lo',
        'IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX':'__yreg64',
        'IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX':'__yreg64',
    };
    kdAnnotateNameCallStmts = {
        'IEM_MC_CALL_CIMPL_0':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_1':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_2':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_3':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_4':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_5':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_6':                      '__cimpl',
        'IEM_MC_CALL_CIMPL_7':                      '__cimpl',
        'IEM_MC_DEFER_TO_CIMPL_0_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_1_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_2_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_3_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_4_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_5_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_6_RET':              '__cimpl_defer',
        'IEM_MC_DEFER_TO_CIMPL_7_RET':              '__cimpl_defer',
        'IEM_MC_CALL_VOID_AIMPL_0':                 '__aimpl',
        'IEM_MC_CALL_VOID_AIMPL_1':                 '__aimpl',
        'IEM_MC_CALL_VOID_AIMPL_2':                 '__aimpl',
        'IEM_MC_CALL_VOID_AIMPL_3':                 '__aimpl',
        'IEM_MC_CALL_VOID_AIMPL_4':                 '__aimpl',
        'IEM_MC_CALL_VOID_AIMPL_5':                 '__aimpl',
        'IEM_MC_CALL_AIMPL_0':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_1':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_2':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_3':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_4':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_5':                      '__aimpl_ret',
        'IEM_MC_CALL_AIMPL_6':                      '__aimpl_ret',
        'IEM_MC_CALL_VOID_AIMPL_6':                 '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_0':                  '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_1':                  '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_2':                  '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_3':                  '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_4':                  '__aimpl_fpu',
        'IEM_MC_CALL_FPU_AIMPL_5':                  '__aimpl_fpu',
        'IEM_MC_CALL_MMX_AIMPL_0':                  '__aimpl_mmx',
        'IEM_MC_CALL_MMX_AIMPL_1':                  '__aimpl_mmx',
        'IEM_MC_CALL_MMX_AIMPL_2':                  '__aimpl_mmx',
        'IEM_MC_CALL_MMX_AIMPL_3':                  '__aimpl_mmx',
        'IEM_MC_CALL_MMX_AIMPL_4':                  '__aimpl_mmx',
        'IEM_MC_CALL_MMX_AIMPL_5':                  '__aimpl_mmx',
        'IEM_MC_CALL_SSE_AIMPL_0':                  '__aimpl_sse',
        'IEM_MC_CALL_SSE_AIMPL_1':                  '__aimpl_sse',
        'IEM_MC_CALL_SSE_AIMPL_2':                  '__aimpl_sse',
        'IEM_MC_CALL_SSE_AIMPL_3':                  '__aimpl_sse',
        'IEM_MC_CALL_SSE_AIMPL_4':                  '__aimpl_sse',
        'IEM_MC_CALL_SSE_AIMPL_5':                  '__aimpl_sse',
        'IEM_MC_CALL_AVX_AIMPL_0':                  '__aimpl_avx',
        'IEM_MC_CALL_AVX_AIMPL_1':                  '__aimpl_avx',
        'IEM_MC_CALL_AVX_AIMPL_2':                  '__aimpl_avx',
        'IEM_MC_CALL_AVX_AIMPL_3':                  '__aimpl_avx',
        'IEM_MC_CALL_AVX_AIMPL_4':                  '__aimpl_avx',
        'IEM_MC_CALL_AVX_AIMPL_5':                  '__aimpl_avx',
    };
    def analyzeAndAnnotateName(self, aoStmts: List[iai.McStmt]):
        """
        Scans the statements and variation lists for clues about the threaded function,
        and sets self.sSubName if successfull.
        """
        # Operand base naming:
        dHits = {};
        cHits = iai.McStmt.countStmtsByName(aoStmts, self.kdAnnotateNameMemStmts, dHits);
        if cHits > 0:
            sStmtNm = sorted(dHits.keys())[-1]; # priority: STORE, MEM_MAP, FETCH.
            sName = self.kdAnnotateNameMemStmts[sStmtNm];
        else:
            cHits = iai.McStmt.countStmtsByName(aoStmts, self.kdAnnotateNameRegStmts, dHits);
            if cHits > 0:
                sStmtNm = sorted(dHits.keys())[-1]; # priority: STORE, MEM_MAP, FETCH.
                sName = self.kdAnnotateNameRegStmts[sStmtNm];
            else:
                # No op details, try name it by call type...
                cHits = iai.McStmt.countStmtsByName(aoStmts, self.kdAnnotateNameCallStmts, dHits);
                if cHits > 0:
                    sStmtNm = sorted(dHits.keys())[-1]; # Not really necessary to sort, but simple this way.
                    self.sSubName = self.kdAnnotateNameCallStmts[sStmtNm];
                return;

        # Add call info if any:
        dHits = {};
        cHits = iai.McStmt.countStmtsByName(aoStmts, self.kdAnnotateNameCallStmts, dHits);
        if cHits > 0:
            sStmtNm = sorted(dHits.keys())[-1]; # Not really necessary to sort, but simple this way.
            sName += self.kdAnnotateNameCallStmts[sStmtNm][1:];

        self.sSubName = sName;
        return;

    def analyzeFindVariablesAndCallArgs(self, aoStmts: List[iai.McStmt]) -> bool:
        """ Scans the statements for MC variables and call arguments. """
        for oStmt in aoStmts:
            if isinstance(oStmt, iai.McStmtVar):
                if oStmt.sVarName in self.dVariables:
                    raise Exception('Variable %s is defined more than once!' % (oStmt.sVarName,));
                self.dVariables[oStmt.sVarName] = oStmt.sVarName;
            elif isinstance(oStmt, iai.McStmtCall) and oStmt.sName.startswith('IEM_MC_CALL_AIMPL_'):
                if oStmt.asParams[1] in self.dVariables:
                    raise Exception('Variable %s is defined more than once!' % (oStmt.asParams[1],));
                self.dVariables[oStmt.asParams[1]] = iai.McStmtVar('IEM_MC_LOCAL', oStmt.asParams[0:2],
                                                                   oStmt.asParams[0], oStmt.asParams[1]);

            # There shouldn't be any variables or arguments declared inside if/
            # else blocks, but scan them too to be on the safe side.
            if isinstance(oStmt, iai.McStmtCond):
                #cBefore = len(self.dVariables);
                self.analyzeFindVariablesAndCallArgs(oStmt.aoIfBranch);
                self.analyzeFindVariablesAndCallArgs(oStmt.aoElseBranch);
                #if len(self.dVariables) != cBefore:
                #    raise Exception('Variables/arguments defined in conditional branches!');
        return True;

    kdReturnStmtAnnotations = {
        'IEM_MC_ADVANCE_RIP_AND_FINISH':    g_ksFinishAnnotation_Advance,
        'IEM_MC_REL_JMP_S8_AND_FINISH':     g_ksFinishAnnotation_RelJmp,
        'IEM_MC_REL_JMP_S16_AND_FINISH':    g_ksFinishAnnotation_RelJmp,
        'IEM_MC_REL_JMP_S32_AND_FINISH':    g_ksFinishAnnotation_RelJmp,
        'IEM_MC_SET_RIP_U16_AND_FINISH':    g_ksFinishAnnotation_SetJmp,
        'IEM_MC_SET_RIP_U32_AND_FINISH':    g_ksFinishAnnotation_SetJmp,
        'IEM_MC_SET_RIP_U64_AND_FINISH':    g_ksFinishAnnotation_SetJmp,
        'IEM_MC_REL_CALL_S16_AND_FINISH':   g_ksFinishAnnotation_RelCall,
        'IEM_MC_REL_CALL_S32_AND_FINISH':   g_ksFinishAnnotation_RelCall,
        'IEM_MC_REL_CALL_S64_AND_FINISH':   g_ksFinishAnnotation_RelCall,
        'IEM_MC_IND_CALL_U16_AND_FINISH':   g_ksFinishAnnotation_IndCall,
        'IEM_MC_IND_CALL_U32_AND_FINISH':   g_ksFinishAnnotation_IndCall,
        'IEM_MC_IND_CALL_U64_AND_FINISH':   g_ksFinishAnnotation_IndCall,
        'IEM_MC_DEFER_TO_CIMPL_0_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_1_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_2_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_3_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_4_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_5_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_6_RET':      g_ksFinishAnnotation_DeferToCImpl,
        'IEM_MC_DEFER_TO_CIMPL_7_RET':      g_ksFinishAnnotation_DeferToCImpl,
    };
    def analyzeCodeOperation(self, aoStmts: List[iai.McStmt], dEflStmts, fSeenConditional = False) -> bool:
        """
        Analyzes the code looking clues as to additional side-effects.

        Currently this is simply looking for branching and adding the relevant
        branch flags to dsCImplFlags.  ASSUMES the caller pre-populates the
        dictionary with a copy of self.oMcBlock.dsCImplFlags.

        This also sets McStmtCond.oIfBranchAnnotation & McStmtCond.oElseBranchAnnotation.

        Returns annotation on return style.
        """
        sAnnotation = None;
        for oStmt in aoStmts:
            # Set IEM_IMPL_C_F_BRANCH_XXXX flags if we see any branching MCs.
            if oStmt.sName.startswith('IEM_MC_SET_RIP'):
                assert not fSeenConditional;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_INDIRECT'] = True;
            elif oStmt.sName.startswith('IEM_MC_REL_JMP'):
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_RELATIVE'] = True;
                if fSeenConditional:
                    self.dsCImplFlags['IEM_CIMPL_F_BRANCH_CONDITIONAL'] = True;
            elif oStmt.sName.startswith('IEM_MC_IND_CALL'):
                assert not fSeenConditional;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_INDIRECT'] = True;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_STACK']    = True;
                self.dsCImplFlags['IEM_CIMPL_F_END_TB']          = True;
            elif oStmt.sName.startswith('IEM_MC_REL_CALL'):
                assert not fSeenConditional;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_RELATIVE'] = True;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_STACK']    = True;
                self.dsCImplFlags['IEM_CIMPL_F_END_TB']          = True;
            elif oStmt.sName.startswith('IEM_MC_RETN'):
                assert not fSeenConditional;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_INDIRECT'] = True;
                self.dsCImplFlags['IEM_CIMPL_F_BRANCH_STACK']    = True;
                self.dsCImplFlags['IEM_CIMPL_F_END_TB']          = True;

            # Check for CIMPL and AIMPL calls.
            if oStmt.sName.startswith('IEM_MC_CALL_'):
                if oStmt.sName.startswith('IEM_MC_CALL_CIMPL_'):
                    self.dsCImplFlags['IEM_CIMPL_F_CALLS_CIMPL'] = True;
                elif (   oStmt.sName.startswith('IEM_MC_CALL_VOID_AIMPL_')
                      or oStmt.sName.startswith('IEM_MC_CALL_AIMPL_')):
                    self.dsCImplFlags['IEM_CIMPL_F_CALLS_AIMPL'] = True;
                elif (   oStmt.sName.startswith('IEM_MC_CALL_SSE_AIMPL_')
                      or oStmt.sName.startswith('IEM_MC_CALL_MMX_AIMPL_')
                      or oStmt.sName.startswith('IEM_MC_CALL_FPU_AIMPL_')):
                    self.dsCImplFlags['IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE'] = True;
                elif oStmt.sName.startswith('IEM_MC_CALL_AVX_AIMPL_'):
                    self.dsCImplFlags['IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE'] = True;
                else:
                    raise Exception('Unknown IEM_MC_CALL_* statement: %s' % (oStmt.sName,));

            # Check for return statements.
            if oStmt.sName in self.kdReturnStmtAnnotations:
                assert sAnnotation is None;
                sAnnotation = self.kdReturnStmtAnnotations[oStmt.sName];

            # Collect MCs working on EFLAGS.  Caller will check this.
            if oStmt.sName in ('IEM_MC_FETCH_EFLAGS', 'IEM_MC_FETCH_EFLAGS_U8', 'IEM_MC_COMMIT_EFLAGS',
                               'IEM_MC_COMMIT_EFLAGS_OPT', 'IEM_MC_REF_EFLAGS', 'IEM_MC_ARG_LOCAL_EFLAGS', ):
                dEflStmts[oStmt.sName] = oStmt;
            elif isinstance(oStmt, iai.McStmtCall):
                if oStmt.sName in ('IEM_MC_CALL_CIMPL_0', 'IEM_MC_CALL_CIMPL_1', 'IEM_MC_CALL_CIMPL_2',
                                   'IEM_MC_CALL_CIMPL_3', 'IEM_MC_CALL_CIMPL_4', 'IEM_MC_CALL_CIMPL_5',):
                    if (   oStmt.asParams[0].find('IEM_CIMPL_F_RFLAGS')       >= 0
                        or oStmt.asParams[0].find('IEM_CIMPL_F_STATUS_FLAGS') >= 0):
                        dEflStmts[oStmt.sName] = oStmt;

            # Process branches of conditionals recursively.
            if isinstance(oStmt, iai.McStmtCond):
                oStmt.oIfBranchAnnotation = self.analyzeCodeOperation(oStmt.aoIfBranch, dEflStmts, True);
                if oStmt.aoElseBranch:
                    oStmt.oElseBranchAnnotation = self.analyzeCodeOperation(oStmt.aoElseBranch, dEflStmts, True);

        return sAnnotation;

    def analyzeThreadedFunction(self, oGenerator):
        """
        Analyzes the code, identifying the number of parameters it requires and such.

        Returns dummy True - raises exception on trouble.
        """

        #
        # Decode the block into a list/tree of McStmt objects.
        #
        aoStmts = self.oMcBlock.decode();

        #
        # Check the block for errors before we proceed (will decode it).
        #
        asErrors = self.oMcBlock.check();
        if asErrors:
            raise Exception('\n'.join(['%s:%s: error: %s' % (self.oMcBlock.sSrcFile, self.oMcBlock.iBeginLine, sError, )
                                       for sError in asErrors]));

        #
        # Scan the statements for local variables and call arguments (self.dVariables).
        #
        self.analyzeFindVariablesAndCallArgs(aoStmts);

        #
        # Scan the code for IEM_CIMPL_F_ and other clues.
        #
        self.dsCImplFlags = self.oMcBlock.dsCImplFlags.copy();
        dEflStmts         = {};
        self.analyzeCodeOperation(aoStmts, dEflStmts);
        if (   ('IEM_CIMPL_F_CALLS_CIMPL' in self.dsCImplFlags)
             + ('IEM_CIMPL_F_CALLS_AIMPL' in self.dsCImplFlags)
             + ('IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE' in self.dsCImplFlags)
             + ('IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE' in self.dsCImplFlags) > 1):
            self.error('Mixing CIMPL/AIMPL/AIMPL_WITH_FXSTATE/AIMPL_WITH_XSTATE calls', oGenerator);

        #
        # Analyse EFLAGS related MCs and @opflmodify and friends.
        #
        if dEflStmts:
            oInstruction = self.oMcBlock.oInstruction; # iai.Instruction
            if (   oInstruction is None
                or (oInstruction.asFlTest is None and oInstruction.asFlModify is None)):
                sMcNames = '+'.join(dEflStmts.keys());
                if len(dEflStmts) != 1 or not sMcNames.startswith('IEM_MC_CALL_CIMPL_'): # Hack for far calls
                    self.error('Uses %s but has no @opflmodify, @opfltest or @opflclass with details!' % (sMcNames,), oGenerator);
            elif 'IEM_MC_COMMIT_EFLAGS' in dEflStmts  or  'IEM_MC_COMMIT_EFLAGS_OPT' in dEflStmts:
                if not oInstruction.asFlModify:
                    if oInstruction.sMnemonic not in [ 'not', ]:
                        self.error('Uses IEM_MC_COMMIT_EFLAGS[_OPT] but has no flags in @opflmodify!', oGenerator);
            elif (   'IEM_MC_CALL_CIMPL_0' in dEflStmts
                  or 'IEM_MC_CALL_CIMPL_1' in dEflStmts
                  or 'IEM_MC_CALL_CIMPL_2' in dEflStmts
                  or 'IEM_MC_CALL_CIMPL_3' in dEflStmts
                  or 'IEM_MC_CALL_CIMPL_4' in dEflStmts
                  or 'IEM_MC_CALL_CIMPL_5' in dEflStmts ):
                if not oInstruction.asFlModify:
                    self.error('Uses IEM_MC_CALL_CIMPL_x or IEM_MC_DEFER_TO_CIMPL_5_RET with IEM_CIMPL_F_STATUS_FLAGS '
                               'or IEM_CIMPL_F_RFLAGS but has no flags in @opflmodify!', oGenerator);
            elif 'IEM_MC_REF_EFLAGS' not in dEflStmts:
                if not oInstruction.asFlTest:
                    if oInstruction.sMnemonic not in [ 'not', ]:
                        self.error('Expected @opfltest!', oGenerator);
            if oInstruction and oInstruction.asFlSet:
                for sFlag in oInstruction.asFlSet:
                    if sFlag not in oInstruction.asFlModify:
                        self.error('"%s" in  @opflset but missing from @opflmodify (%s)!'
                                   % (sFlag, ', '.join(oInstruction.asFlModify)), oGenerator);
            if oInstruction and oInstruction.asFlClear:
                for sFlag in oInstruction.asFlClear:
                    if sFlag not in oInstruction.asFlModify:
                        self.error('"%s" in  @opflclear but missing from @opflmodify (%s)!'
                                   % (sFlag, ', '.join(oInstruction.asFlModify)), oGenerator);

        #
        # Create variations as needed.
        #
        if iai.McStmt.findStmtByNames(aoStmts,
                                      { 'IEM_MC_DEFER_TO_CIMPL_0_RET': True,
                                        'IEM_MC_DEFER_TO_CIMPL_1_RET': True,
                                        'IEM_MC_DEFER_TO_CIMPL_2_RET': True,
                                        'IEM_MC_DEFER_TO_CIMPL_3_RET': True, }):
            asVariations = (ThreadedFunctionVariation.ksVariation_Default,);

        elif iai.McStmt.findStmtByNames(aoStmts, { 'IEM_MC_CALC_RM_EFF_ADDR' : True,
                                                   'IEM_MC_FETCH_MEM_U8'  : True,  # mov_AL_Ob ++
                                                   'IEM_MC_FETCH_MEM_U16' : True,  # mov_rAX_Ov ++
                                                   'IEM_MC_FETCH_MEM_U32' : True,
                                                   'IEM_MC_FETCH_MEM_U64' : True,
                                                   'IEM_MC_STORE_MEM_U8'  : True,  # mov_Ob_AL ++
                                                   'IEM_MC_STORE_MEM_U16' : True,  # mov_Ov_rAX ++
                                                   'IEM_MC_STORE_MEM_U32' : True,
                                                   'IEM_MC_STORE_MEM_U64' : True, }):
            if 'IEM_MC_F_64BIT' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithAddressOnly64;
            elif 'IEM_MC_F_NOT_64BIT' in self.oMcBlock.dsMcFlags and 'IEM_MC_F_NOT_286_OR_OLDER' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithAddressNot286Not64;
            elif 'IEM_MC_F_NOT_286_OR_OLDER' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithAddressNot286;
            elif 'IEM_MC_F_NOT_64BIT' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithAddressNot64;
            elif 'IEM_MC_F_ONLY_8086' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsOnlyPre386;
            else:
                asVariations = ThreadedFunctionVariation.kasVariationsWithAddress;
        else:
            if 'IEM_MC_F_64BIT' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithoutAddressOnly64;
            elif 'IEM_MC_F_NOT_64BIT' in self.oMcBlock.dsMcFlags and 'IEM_MC_F_NOT_286_OR_OLDER' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithoutAddressNot286Not64;
            elif 'IEM_MC_F_NOT_286_OR_OLDER' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithoutAddressNot286;
            elif 'IEM_MC_F_NOT_64BIT' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsWithoutAddressNot64;
            elif 'IEM_MC_F_ONLY_8086' in self.oMcBlock.dsMcFlags:
                asVariations = ThreadedFunctionVariation.kasVariationsOnlyPre386;
            else:
                asVariations = ThreadedFunctionVariation.kasVariationsWithoutAddress;

            if (    'IEM_CIMPL_F_BRANCH_CONDITIONAL' in self.dsCImplFlags
                and 'IEM_CIMPL_F_BRANCH_RELATIVE'    in self.dsCImplFlags): # (latter to avoid iemOp_into)
                assert set(asVariations).issubset(ThreadedFunctionVariation.kasVariationsWithoutAddress), \
                    '%s: vars=%s McFlags=%s' % (self.oMcBlock.oFunction.sName, asVariations, self.oMcBlock.dsMcFlags);
                asVariationsBase = asVariations;
                asVariations     = [];
                for sVariation in asVariationsBase:
                    asVariations.extend([sVariation + '_Jmp', sVariation + '_NoJmp']);
                assert set(asVariations).issubset(ThreadedFunctionVariation.kdVariationsWithConditional);

                # We've got some Flat variations we need to add manually to avoid unnecessary CS.LIM checks.
                if ThrdFnVar.ksVariation_32 in asVariationsBase:
                    assert ThrdFnVar.ksVariation_32f in asVariationsBase;
                    asVariations.extend([
                        ThrdFnVar.ksVariation_32_Flat_Jmp,
                        ThrdFnVar.ksVariation_32_Flat_NoJmp,
                        ThrdFnVar.ksVariation_32f_Flat_Jmp,
                        ThrdFnVar.ksVariation_32f_Flat_NoJmp,
                    ]);

                # Similarly, if there are 64-bit variants, we need the within same page variations.
                # We skip this when the operand size prefix forces is used because it cuts RIP down
                # to 16-bit only and the same-page assumptions are most likely wrong then.
                if (    ThrdFnVar.ksVariation_64 in asVariationsBase
                    and not iai.McStmt.findStmtByNames(aoStmts, { 'IEM_MC_REL_JMP_S16_AND_FINISH': True })):
                    assert ThrdFnVar.ksVariation_64f in asVariationsBase;
                    asVariations.extend([
                        ThrdFnVar.ksVariation_64_SamePg_Jmp,
                        ThrdFnVar.ksVariation_64_SamePg_NoJmp,
                        ThrdFnVar.ksVariation_64f_SamePg_Jmp,
                        ThrdFnVar.ksVariation_64f_SamePg_NoJmp,
                    ]);

        if not iai.McStmt.findStmtByNames(aoStmts,
                                          { 'IEM_MC_ADVANCE_RIP_AND_FINISH':  True,
                                            'IEM_MC_REL_JMP_S8_AND_FINISH':   True,
                                            'IEM_MC_REL_JMP_S16_AND_FINISH':  True,
                                            'IEM_MC_REL_JMP_S32_AND_FINISH':  True,
                                            'IEM_MC_SET_RIP_U16_AND_FINISH':  True,
                                            'IEM_MC_SET_RIP_U32_AND_FINISH':  True,
                                            'IEM_MC_SET_RIP_U64_AND_FINISH':  True,
                                            'IEM_MC_REL_CALL_S16_AND_FINISH': True,
                                            'IEM_MC_REL_CALL_S32_AND_FINISH': True,
                                            'IEM_MC_REL_CALL_S64_AND_FINISH': True,
                                            'IEM_MC_IND_CALL_U16_AND_FINISH': True,
                                            'IEM_MC_IND_CALL_U32_AND_FINISH': True,
                                            'IEM_MC_IND_CALL_U64_AND_FINISH': True,
                                            'IEM_MC_RETN_AND_FINISH':         True,
                                           }):
            asVariations = [sVariation for sVariation in asVariations
                            if sVariation not in ThreadedFunctionVariation.kdVariationsWithEflagsCheckingAndClearing];

        self.aoVariations = [ThreadedFunctionVariation(self, sVar) for sVar in asVariations];

        # Dictionary variant of the list.
        self.dVariations = { oVar.sVariation: oVar for oVar in self.aoVariations };

        #
        # Try annotate the threaded function name.
        #
        self.analyzeAndAnnotateName(aoStmts);

        #
        # Continue the analysis on each variation.
        #
        for oVariation in self.aoVariations:
            oVariation.analyzeVariation(aoStmts);

        return True;

    ## Used by emitThreadedCallStmts.
    kdVariationsWithNeedForPrefixCheck = {
        ThreadedFunctionVariation.ksVariation_64_Addr32:  True,
        ThreadedFunctionVariation.ksVariation_64f_Addr32: True,
        ThreadedFunctionVariation.ksVariation_64_FsGs:    True,
        ThreadedFunctionVariation.ksVariation_64f_FsGs:   True,
        ThreadedFunctionVariation.ksVariation_32_Addr16:  True,
        ThreadedFunctionVariation.ksVariation_32f_Addr16: True,
        ThreadedFunctionVariation.ksVariation_32_Flat:    True,
        ThreadedFunctionVariation.ksVariation_32f_Flat:   True,
        ThreadedFunctionVariation.ksVariation_16_Addr32:  True,
        ThreadedFunctionVariation.ksVariation_16f_Addr32: True,
    };

    def emitThreadedCallStmts(self, sBranch = None, fTbLookupTable = False): # pylint: disable=too-many-statements
        """
        Worker for morphInputCode that returns a list of statements that emits
        the call to the threaded functions for the block.

        The sBranch parameter is used with conditional branches where we'll emit
        different threaded calls depending on whether we're in the jump-taken or
        no-jump code path.  Values are either None, 'Jmp' or 'NoJmp'.

        The fTbLookupTable parameter can either be False, True or whatever else
        (like 2) - in the latter case this means a large lookup table.
        """
        # Special case for only default variation:
        if len(self.aoVariations) == 1  and  self.aoVariations[0].sVariation == ThreadedFunctionVariation.ksVariation_Default:
            assert not sBranch;
            return self.aoVariations[0].emitThreadedCallStmtsForVariant(0, fTbLookupTable);

        #
        # Case statement sub-class.
        #
        dByVari = self.dVariations;
        #fDbg   = self.oMcBlock.sFunction == 'iemOp_jnl_Jv';
        class Case:
            def __init__(self, sCond, sVarNm = None, sIntraPgVarNm = None, sIntraPgDispVariable = None):
                self.sCond  = sCond;
                self.sVarNm = sVarNm;
                self.oVar   = dByVari[sVarNm] if sVarNm else None;
                self.aoBody = self.oVar.emitThreadedCallStmtsForVariant(8, fTbLookupTable) if sVarNm else None;
                # Some annoying complications just to skip canonical jump target checks for intrapage jumps.
                self.sIntraPgDispVariable = sIntraPgDispVariable;
                self.oIntraPgVar   = dByVari[sIntraPgVarNm] if sIntraPgVarNm else None;
                self.aoIntraPgBody = self.oIntraPgVar.emitThreadedCallStmtsForVariant(8, fTbLookupTable) if sIntraPgVarNm \
                                     else None;

            def toCode(self):
                aoStmts = [ iai.McCppGeneric('case %s:' % (self.sCond), cchIndent = 4), ];
                if self.aoBody:
                    if not self.aoIntraPgBody:
                        aoStmts.extend(self.aoBody);
                        aoStmts.append(iai.McCppGeneric('break;', cchIndent = 8));
                    else:
                        aoStmts.extend([
                            iai.McCppCond('!IEMOP_HLP_PC64_IS_JMP_REL_WITHIN_PAGE(%s)' % (self.sIntraPgDispVariable,),
                                          True, self.aoBody, self.aoIntraPgBody, cchIndent = 8),
                            iai.McCppGeneric('break;', cchIndent = 8),
                        ]);
                return aoStmts;

            def toFunctionAssignment(self):
                aoStmts = [ iai.McCppGeneric('case %s:' % (self.sCond), cchIndent = 4), ];
                if self.aoBody:
                    if not self.aoIntraPgBody:
                        aoStmts.extend([
                            iai.McCppGeneric('enmFunction = %s;' % (self.oVar.getIndexName(),), cchIndent = 8),
                            iai.McCppGeneric('break;', cchIndent = 8),
                        ]);
                    else:
                        aoStmts.extend([
                            iai.McCppGeneric('enmFunction = !IEMOP_HLP_PC64_IS_JMP_REL_WITHIN_PAGE(%s) ? %s : %s;'
                                             % (self.sIntraPgDispVariable, self.oVar.getIndexName(),
                                                self.oIntraPgVar.getIndexName(),), cchIndent = 8),
                            iai.McCppGeneric('break;', cchIndent = 8),
                        ]);
                return aoStmts;

            @staticmethod
            def isSameBody(aoThisBody, sThisIndexName, aoThatBody, sThatIndexName, sBody = ''):
                if len(aoThisBody) != len(aoThatBody):
                    #if fDbg: print('dbg: %sbody len diff: %s vs %s' % (sBody, len(aoThisBody), len(aoThatBody),));
                    return False;
                for iStmt, oStmt in enumerate(aoThisBody):
                    oThatStmt = aoThatBody[iStmt] # type: iai.McStmt
                    assert isinstance(oStmt, iai.McCppGeneric);
                    assert not isinstance(oStmt, iai.McStmtCond);
                    if isinstance(oStmt, iai.McStmtCond):
                        return False;
                    if oStmt.sName != oThatStmt.sName:
                        #if fDbg: print('dbg: %sstmt #%s name: %s vs %s' % (sBody, iStmt, oStmt.sName, oThatStmt.sName,));
                        return False;
                    if len(oStmt.asParams) != len(oThatStmt.asParams):
                        #if fDbg: print('dbg: %sstmt #%s param count: %s vs %s'
                        #               % (sBody, iStmt, len(oStmt.asParams), len(oThatStmt.asParams),));
                        return False;
                    for iParam, sParam in enumerate(oStmt.asParams):
                        if (    sParam != oThatStmt.asParams[iParam]
                            and (   iParam not in (1, 2)
                                 or not isinstance(oStmt, iai.McCppCall)
                                 or not oStmt.asParams[0].startswith('IEM_MC2_EMIT_CALL_')
                                 or sParam != sThisIndexName
                                 or oThatStmt.asParams[iParam] != sThatIndexName )):
                            #if fDbg: print('dbg: %sstmt #%s, param #%s: %s vs %s'
                            #               % (sBody, iStmt, iParam, sParam, oThatStmt.asParams[iParam],));
                            return False;
                return True;

            def isSame(self, oThat):
                if self.aoBody:  # no body == fall thru - that always matches.
                    if not self.isSameBody(self.aoBody,  self.oVar.getIndexName(),
                                           oThat.aoBody, oThat.oVar.getIndexName()):
                        return False;
                    if self.aoIntraPgBody and not self.isSameBody(self.aoIntraPgBody,   self.oIntraPgVar.getIndexName(),
                                                                  oThat.aoBody,         oThat.oVar.getIndexName(),
                                                                  'intrapg/left '):
                        return False;
                    if oThat.aoIntraPgBody and not self.isSameBody(self.aoBody,         self.oVar.getIndexName(),
                                                                   oThat.aoIntraPgBody, oThat.oIntraPgVar.getIndexName(),
                                                                   'intrapg/right '):
                        return False;
                return True;

        #
        # Determine what we're switch on.
        # This ASSUMES that (IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | IEM_F_MODE_CPUMODE_MASK) == 7!
        #
        fSimple = True;
        sSwitchValue  = '(pVCpu->iem.s.fExec & (IEM_F_MODE_CPUMODE_MASK | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK))';
        if dByVari.keys() & self.kdVariationsWithNeedForPrefixCheck.keys():
            sSwitchValue += ' | (pVCpu->iem.s.enmEffAddrMode == (pVCpu->iem.s.fExec & IEM_F_MODE_CPUMODE_MASK) ? 0 : 8)';
            # Accesses via FS and GS and CS goes thru non-FLAT functions. (CS
            # is not writable in 32-bit mode (at least), thus the penalty mode
            # for any accesses via it (simpler this way).)
            sSwitchValue += ' | (pVCpu->iem.s.iEffSeg < X86_SREG_FS && pVCpu->iem.s.iEffSeg != X86_SREG_CS ? 0 : 16)';
            fSimple       = False;                                              # threaded functions.
        if dByVari.keys() & ThreadedFunctionVariation.kdVariationsWithEflagsCheckingAndClearing:
            sSwitchValue += ' | ((pVCpu->iem.s.fTbPrevInstr & (IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_INHIBIT_SHADOW)) || ' \
                          + '(pVCpu->iem.s.fExec & IEM_F_PENDING_BRK_MASK) ? 32 : 0)';

        #
        # Generate the case statements.
        #
        # pylintx: disable=x
        aoCases = [];
        if ThreadedFunctionVariation.ksVariation_64_Addr32 in dByVari:
            assert not fSimple and not sBranch;
            aoCases.extend([
                Case('IEMMODE_64BIT',       ThrdFnVar.ksVariation_64),
                Case('IEMMODE_64BIT | 16',  ThrdFnVar.ksVariation_64_FsGs),
                Case('IEMMODE_64BIT | 8 | 16', None), # fall thru
                Case('IEMMODE_64BIT | 8',   ThrdFnVar.ksVariation_64_Addr32),
            ]);
            if ThreadedFunctionVariation.ksVariation_64f_Addr32 in dByVari:
                aoCases.extend([
                    Case('IEMMODE_64BIT | 32',       ThrdFnVar.ksVariation_64f),
                    Case('IEMMODE_64BIT | 32 | 16',  ThrdFnVar.ksVariation_64f_FsGs),
                    Case('IEMMODE_64BIT | 32 | 8 | 16', None), # fall thru
                    Case('IEMMODE_64BIT | 32 | 8',   ThrdFnVar.ksVariation_64f_Addr32),
                ]);
        elif ThrdFnVar.ksVariation_64 in dByVari:
            assert fSimple and not sBranch;
            aoCases.append(Case('IEMMODE_64BIT', ThrdFnVar.ksVariation_64));
            if ThreadedFunctionVariation.ksVariation_64f in dByVari:
                aoCases.append(Case('IEMMODE_64BIT | 32', ThrdFnVar.ksVariation_64f));
        elif ThrdFnVar.ksVariation_64_Jmp in dByVari:
            assert fSimple and sBranch;
            if ThrdFnVar.ksVariation_64_SamePg_Jmp not in dByVari:
                assert ThrdFnVar.ksVariation_64f_Jmp in dByVari;
                aoCases.extend([
                    Case('IEMMODE_64BIT',
                         ThrdFnVar.ksVariation_64_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64_NoJmp),
                    Case('IEMMODE_64BIT | 32',
                         ThrdFnVar.ksVariation_64f_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64f_NoJmp),
                ]);
            else:
                assert ThrdFnVar.ksVariation_64f_SamePg_Jmp in dByVari;
                oStmtRelJmp = iai.McStmt.findStmtByNames(self.oMcBlock.decode(),
                                                         { 'IEM_MC_REL_JMP_S8_AND_FINISH': True,
                                                           'IEM_MC_REL_JMP_S16_AND_FINISH': True,
                                                           'IEM_MC_REL_JMP_S32_AND_FINISH': True,});
                sIntraPgDispVariable = oStmtRelJmp.asParams[0];
                aoCases.extend([
                    Case('IEMMODE_64BIT',
                         ThrdFnVar.ksVariation_64_Jmp        if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64_NoJmp,
                         ThrdFnVar.ksVariation_64_SamePg_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64_SamePg_NoJmp,
                         sIntraPgDispVariable),
                    Case('IEMMODE_64BIT | 32',
                         ThrdFnVar.ksVariation_64f_Jmp        if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64f_NoJmp,
                         ThrdFnVar.ksVariation_64f_SamePg_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_64f_SamePg_NoJmp,
                         sIntraPgDispVariable),
                ]);


        if ThrdFnVar.ksVariation_32_Addr16 in dByVari:
            assert not fSimple and not sBranch;
            aoCases.extend([
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK',         ThrdFnVar.ksVariation_32_Flat),
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 16',    None), # fall thru
                Case('IEMMODE_32BIT                                       | 16',    None), # fall thru
                Case('IEMMODE_32BIT',                                               ThrdFnVar.ksVariation_32),
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 8',     None), # fall thru
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 8 | 16',None), # fall thru
                Case('IEMMODE_32BIT                                       | 8 | 16',None), # fall thru
                Case('IEMMODE_32BIT                                       | 8',     ThrdFnVar.ksVariation_32_Addr16),
            ]);
            if ThrdFnVar.ksVariation_32f_Addr16 in dByVari:
                aoCases.extend([
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32',         ThrdFnVar.ksVariation_32f_Flat),
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32 | 16',    None), # fall thru
                    Case('IEMMODE_32BIT                                       | 32 | 16',    None), # fall thru
                    Case('IEMMODE_32BIT                                       | 32',         ThrdFnVar.ksVariation_32f),
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32 | 8',     None), # fall thru
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32 | 8 | 16',None), # fall thru
                    Case('IEMMODE_32BIT                                       | 32 | 8 | 16',None), # fall thru
                    Case('IEMMODE_32BIT                                       | 32 | 8',     ThrdFnVar.ksVariation_32f_Addr16),
                ]);
        elif ThrdFnVar.ksVariation_32 in dByVari:
            assert fSimple and not sBranch;
            aoCases.extend([
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK', None), # fall thru
                Case('IEMMODE_32BIT',                                       ThrdFnVar.ksVariation_32),
            ]);
            if ThrdFnVar.ksVariation_32f in dByVari:
                aoCases.extend([
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32', None), # fall thru
                    Case('IEMMODE_32BIT                                       | 32', ThrdFnVar.ksVariation_32f),
                ]);
        elif ThrdFnVar.ksVariation_32_Jmp in dByVari:
            assert fSimple and sBranch;
            aoCases.extend([
                Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK',
                     ThrdFnVar.ksVariation_32_Flat_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_32_Flat_NoJmp),
                Case('IEMMODE_32BIT',
                     ThrdFnVar.ksVariation_32_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_32_NoJmp),
            ]);
            if ThrdFnVar.ksVariation_32f_Jmp in dByVari:
                aoCases.extend([
                    Case('IEMMODE_32BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32',
                         ThrdFnVar.ksVariation_32f_Flat_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_32f_Flat_NoJmp),
                    Case('IEMMODE_32BIT                                       | 32',
                         ThrdFnVar.ksVariation_32f_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_32f_NoJmp),
                ]);

        if ThrdFnVar.ksVariation_16_Addr32 in dByVari:
            assert not fSimple and not sBranch;
            aoCases.extend([
                Case('IEMMODE_16BIT | 16',      None), # fall thru
                Case('IEMMODE_16BIT',           ThrdFnVar.ksVariation_16),
                Case('IEMMODE_16BIT | 8 | 16',  None), # fall thru
                Case('IEMMODE_16BIT | 8',       ThrdFnVar.ksVariation_16_Addr32),
            ]);
            if ThrdFnVar.ksVariation_16f_Addr32 in dByVari:
                aoCases.extend([
                    Case('IEMMODE_16BIT | 32 | 16',      None), # fall thru
                    Case('IEMMODE_16BIT | 32',           ThrdFnVar.ksVariation_16f),
                    Case('IEMMODE_16BIT | 32 | 8 | 16',  None), # fall thru
                    Case('IEMMODE_16BIT | 32 | 8',       ThrdFnVar.ksVariation_16f_Addr32),
                ]);
        elif ThrdFnVar.ksVariation_16 in dByVari:
            assert fSimple and not sBranch;
            aoCases.append(Case('IEMMODE_16BIT', ThrdFnVar.ksVariation_16));
            if ThrdFnVar.ksVariation_16f in dByVari:
                aoCases.append(Case('IEMMODE_16BIT | 32', ThrdFnVar.ksVariation_16f));
        elif ThrdFnVar.ksVariation_16_Jmp in dByVari:
            assert fSimple and sBranch;
            aoCases.append(Case('IEMMODE_16BIT',
                                ThrdFnVar.ksVariation_16_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_16_NoJmp));
            if ThrdFnVar.ksVariation_16f_Jmp in dByVari:
                aoCases.append(Case('IEMMODE_16BIT | 32',
                                    ThrdFnVar.ksVariation_16f_Jmp if sBranch == 'Jmp' else ThrdFnVar.ksVariation_16f_NoJmp));


        if ThrdFnVar.ksVariation_16_Pre386 in dByVari:
            if not fSimple:
                aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 16', None)); # fall thru
            aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK', ThrdFnVar.ksVariation_16_Pre386));
        if ThrdFnVar.ksVariation_16f_Pre386 in dByVari:  # should be nested under previous if, but line too long.
            if not fSimple:
                aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32 | 16', None)); # fall thru
            aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32', ThrdFnVar.ksVariation_16f_Pre386));

        if ThrdFnVar.ksVariation_16_Pre386_Jmp in dByVari:
            assert fSimple and sBranch;
            aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK',
                                ThrdFnVar.ksVariation_16_Pre386_Jmp if sBranch == 'Jmp'
                                else ThrdFnVar.ksVariation_16_Pre386_NoJmp));
        if ThrdFnVar.ksVariation_16f_Pre386_Jmp in dByVari:
            assert fSimple and sBranch;
            aoCases.append(Case('IEMMODE_16BIT | IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | 32',
                                ThrdFnVar.ksVariation_16f_Pre386_Jmp if sBranch == 'Jmp'
                                else ThrdFnVar.ksVariation_16f_Pre386_NoJmp));

        #
        # If the case bodies are all the same, except for the function called,
        # we can reduce the code size and hopefully compile time.
        #
        iFirstCaseWithBody = 0;
        while not aoCases[iFirstCaseWithBody].aoBody:
            iFirstCaseWithBody += 1
        fAllSameCases = True
        for iCase in range(iFirstCaseWithBody + 1, len(aoCases)):
            fAllSameCases = fAllSameCases and aoCases[iCase].isSame(aoCases[iFirstCaseWithBody]);
        #if fDbg: print('fAllSameCases=%s %s' % (fAllSameCases, self.oMcBlock.sFunction,));
        if fAllSameCases:
            aoStmts = [
                iai.McCppGeneric('IEMTHREADEDFUNCS enmFunction;'),
                iai.McCppGeneric('switch (%s)' % (sSwitchValue,)),
                iai.McCppGeneric('{'),
            ];
            for oCase in aoCases:
                aoStmts.extend(oCase.toFunctionAssignment());
            aoStmts.extend([
                iai.McCppGeneric('IEM_NOT_REACHED_DEFAULT_CASE_RET();', cchIndent = 4),
                iai.McCppGeneric('}'),
            ]);
            aoStmts.extend(dByVari[aoCases[iFirstCaseWithBody].sVarNm].emitThreadedCallStmtsForVariant(0, fTbLookupTable,
                                                                                                       'enmFunction'));

        else:
            #
            # Generate the generic switch statement.
            #
            aoStmts = [
                iai.McCppGeneric('switch (%s)' % (sSwitchValue,)),
                iai.McCppGeneric('{'),
            ];
            for oCase in aoCases:
                aoStmts.extend(oCase.toCode());
            aoStmts.extend([
                iai.McCppGeneric('IEM_NOT_REACHED_DEFAULT_CASE_RET();', cchIndent = 4),
                iai.McCppGeneric('}'),
            ]);

        return aoStmts;

    def morphInputCode(self, aoStmts, fIsConditional = False, fCallEmitted = False, cDepth = 0, sBranchAnnotation = None):
        """
        Adjusts (& copies) the statements for the input/decoder so it will emit
        calls to the right threaded functions for each block.

        Returns list/tree of statements (aoStmts is not modified) and updated
        fCallEmitted status.
        """
        #print('McBlock at %s:%s' % (os.path.split(self.oMcBlock.sSrcFile)[1], self.oMcBlock.iBeginLine,));
        aoDecoderStmts = [];

        for iStmt, oStmt in enumerate(aoStmts):
            # Copy the statement. Make a deep copy to make sure we've got our own
            # copies of all instance variables, even if a bit overkill at the moment.
            oNewStmt = copy.deepcopy(oStmt);
            aoDecoderStmts.append(oNewStmt);
            #print('oNewStmt %s %s' % (oNewStmt.sName, len(oNewStmt.asParams),));
            if oNewStmt.sName == 'IEM_MC_BEGIN' and self.dsCImplFlags:
                oNewStmt.asParams[1] = ' | '.join(sorted(self.dsCImplFlags.keys()));

            # If we haven't emitted the threaded function call yet, look for
            # statements which it would naturally follow or preceed.
            if not fCallEmitted:
                if not oStmt.isCppStmt():
                    if (   oStmt.sName.startswith('IEM_MC_MAYBE_RAISE_') \
                        or (oStmt.sName.endswith('_AND_FINISH') and oStmt.sName.startswith('IEM_MC_'))
                        or oStmt.sName.startswith('IEM_MC_CALL_CIMPL_')
                        or oStmt.sName.startswith('IEM_MC_DEFER_TO_CIMPL_')
                        or oStmt.sName in ('IEM_MC_RAISE_DIVIDE_ERROR_IF_LOCAL_IS_ZERO',)):
                        aoDecoderStmts.pop();
                        if not fIsConditional:
                            aoDecoderStmts.extend(self.emitThreadedCallStmts());
                        elif oStmt.sName == 'IEM_MC_ADVANCE_RIP_AND_FINISH':
                            aoDecoderStmts.extend(self.emitThreadedCallStmts('NoJmp', True));
                        else:
                            assert oStmt.sName in { 'IEM_MC_REL_JMP_S8_AND_FINISH':  True,
                                                    'IEM_MC_REL_JMP_S16_AND_FINISH': True,
                                                    'IEM_MC_REL_JMP_S32_AND_FINISH': True, };
                            aoDecoderStmts.extend(self.emitThreadedCallStmts('Jmp', True));
                        aoDecoderStmts.append(oNewStmt);
                        fCallEmitted = True;

                    elif iai.g_dMcStmtParsers[oStmt.sName][2]:
                        # This is for Jmp/NoJmp with loopne and friends which modifies state other than RIP.
                        if not sBranchAnnotation:
                            self.raiseProblem('Modifying state before emitting calls! %s' % (oStmt.sName,));
                        assert fIsConditional;
                        aoDecoderStmts.pop();
                        if sBranchAnnotation == g_ksFinishAnnotation_Advance:
                            assert iai.McStmt.findStmtByNames(aoStmts[iStmt:], {'IEM_MC_ADVANCE_RIP_AND_FINISH':1,})
                            aoDecoderStmts.extend(self.emitThreadedCallStmts('NoJmp', True));
                        elif sBranchAnnotation == g_ksFinishAnnotation_RelJmp:
                            assert iai.McStmt.findStmtByNames(aoStmts[iStmt:],
                                                              { 'IEM_MC_REL_JMP_S8_AND_FINISH':  1,
                                                                'IEM_MC_REL_JMP_S16_AND_FINISH': 1,
                                                                'IEM_MC_REL_JMP_S32_AND_FINISH': 1, });
                            aoDecoderStmts.extend(self.emitThreadedCallStmts('Jmp', True));
                        else:
                            self.raiseProblem('Modifying state before emitting calls! %s' % (oStmt.sName,));
                        aoDecoderStmts.append(oNewStmt);
                        fCallEmitted = True;

                elif (    not fIsConditional
                      and oStmt.fDecode
                      and (   oStmt.asParams[0].find('IEMOP_HLP_DONE_') >= 0
                           or oStmt.asParams[0].find('IEMOP_HLP_DECODED_') >= 0)):
                    aoDecoderStmts.extend(self.emitThreadedCallStmts());
                    fCallEmitted = True;

            # Process branches of conditionals recursively.
            if isinstance(oStmt, iai.McStmtCond):
                (oNewStmt.aoIfBranch, fCallEmitted1) = self.morphInputCode(oStmt.aoIfBranch, fIsConditional,
                                                                           fCallEmitted, cDepth + 1, oStmt.oIfBranchAnnotation);
                if oStmt.aoElseBranch:
                    (oNewStmt.aoElseBranch, fCallEmitted2) = self.morphInputCode(oStmt.aoElseBranch, fIsConditional,
                                                                                 fCallEmitted, cDepth + 1,
                                                                                 oStmt.oElseBranchAnnotation);
                else:
                    fCallEmitted2 = False;
                fCallEmitted = fCallEmitted or (fCallEmitted1 and fCallEmitted2);

        if not fCallEmitted and cDepth == 0:
            self.raiseProblem('Unable to insert call to threaded function.');

        return (aoDecoderStmts, fCallEmitted);


    def generateInputCode(self):
        """
        Modifies the input code.
        """
        cchIndent = (self.oMcBlock.cchIndent + 3) // 4 * 4;

        if len(self.oMcBlock.aoStmts) == 1:
            # IEM_MC_DEFER_TO_CIMPL_X_RET - need to wrap in {} to make it safe to insert into random code.
            sCode = ' ' * cchIndent + 'pVCpu->iem.s.fTbCurInstr = ';
            if self.dsCImplFlags:
                sCode += ' | '.join(sorted(self.dsCImplFlags.keys())) + ';\n';
            else:
                sCode += '0;\n';
            sCode += iai.McStmt.renderCodeForList(self.morphInputCode(self.oMcBlock.aoStmts)[0],
                                                  cchIndent = cchIndent).replace('\n', ' /* gen */\n', 1);
            sIndent = ' ' * (min(cchIndent, 2) - 2);
            sCode = sIndent + '{\n' + sCode + sIndent + '}\n';
            return sCode;

        # IEM_MC_BEGIN/END block
        assert len(self.oMcBlock.asLines) > 2, "asLines=%s" % (self.oMcBlock.asLines,);
        fIsConditional = (    'IEM_CIMPL_F_BRANCH_CONDITIONAL' in self.dsCImplFlags
                          and 'IEM_CIMPL_F_BRANCH_RELATIVE'    in self.dsCImplFlags); # (latter to avoid iemOp_into)
        return iai.McStmt.renderCodeForList(self.morphInputCode(self.oMcBlock.aoStmts, fIsConditional)[0],
                                            cchIndent = cchIndent).replace('\n', ' /* gen */\n', 1);

# Short alias for ThreadedFunctionVariation.
ThrdFnVar = ThreadedFunctionVariation;


class IEMThreadedGenerator(object):
    """
    The threaded code generator & annotator.
    """

    def __init__(self):
        self.aoThreadedFuncs = []       # type: List[ThreadedFunction]
        self.oOptions        = None     # type: argparse.Namespace
        self.aoParsers       = []       # type: List[IEMAllInstPython.SimpleParser]
        self.aidxFirstFunctions = []    # type: List[int] ##< Runs parallel to aoParser giving the index of the first function.
        self.cErrors         = 0;

    #
    # Error reporting.
    #

    def rawError(self, sCompleteMessage):
        """ Output a raw error and increment the error counter. """
        print(sCompleteMessage, file = sys.stderr);
        self.cErrors += 1;
        return False;

    #
    # Processing.
    #

    def processInputFiles(self, sHostArch, fNativeRecompilerEnabled):
        """
        Process the input files.
        """

        # Parse the files.
        self.aoParsers = iai.parseFiles(self.oOptions.asInFiles, sHostArch);

        # Create threaded functions for the MC blocks.
        self.aoThreadedFuncs = [ThreadedFunction(oMcBlock) for oMcBlock in iai.g_aoMcBlocks];

        # Analyze the threaded functions.
        dRawParamCounts = {};
        dMinParamCounts = {};
        for oThreadedFunction in self.aoThreadedFuncs:
            oThreadedFunction.analyzeThreadedFunction(self);
            for oVariation in oThreadedFunction.aoVariations:
                dRawParamCounts[len(oVariation.dParamRefs)] = dRawParamCounts.get(len(oVariation.dParamRefs), 0) + 1;
                dMinParamCounts[oVariation.cMinParams]      = dMinParamCounts.get(oVariation.cMinParams,      0) + 1;
        print('debug: param count distribution, raw and optimized:', file = sys.stderr);
        for cCount in sorted({cBits: True for cBits in list(dRawParamCounts.keys()) + list(dMinParamCounts.keys())}.keys()):
            print('debug:     %s params: %4s raw, %4s min'
                  % (cCount, dRawParamCounts.get(cCount, 0), dMinParamCounts.get(cCount, 0)),
                  file = sys.stderr);

        # Do another pass over the threaded functions to settle the name suffix.
        iThreadedFn = 0;
        while iThreadedFn < len(self.aoThreadedFuncs):
            oFunction = self.aoThreadedFuncs[iThreadedFn].oMcBlock.oFunction;
            assert oFunction;
            iThreadedFnNext = iThreadedFn + 1;
            dSubNames = { self.aoThreadedFuncs[iThreadedFn].sSubName: 1 };
            while (    iThreadedFnNext < len(self.aoThreadedFuncs)
                   and self.aoThreadedFuncs[iThreadedFnNext].oMcBlock.oFunction == oFunction):
                dSubNames[self.aoThreadedFuncs[iThreadedFnNext].sSubName] = 1;
                iThreadedFnNext += 1;
            if iThreadedFnNext - iThreadedFn > len(dSubNames):
                iSubName = 0;
                while iThreadedFn + iSubName < iThreadedFnNext:
                    self.aoThreadedFuncs[iThreadedFn + iSubName].sSubName += '_%s' % (iSubName,);
                    iSubName += 1;
            iThreadedFn = iThreadedFnNext;

        # Populate aidxFirstFunctions.  This is ASSUMING that
        # g_aoMcBlocks/self.aoThreadedFuncs are in self.aoParsers order.
        iThreadedFunction       = 0;
        oThreadedFunction       = self.getThreadedFunctionByIndex(0);
        self.aidxFirstFunctions = [];
        for oParser in self.aoParsers:
            self.aidxFirstFunctions.append(iThreadedFunction);

            while oThreadedFunction.oMcBlock.sSrcFile == oParser.sSrcFile:
                iThreadedFunction += 1;
                oThreadedFunction  = self.getThreadedFunctionByIndex(iThreadedFunction);

        # Analyze the threaded functions and their variations for native recompilation.
        if fNativeRecompilerEnabled:
            ian.analyzeThreadedFunctionsForNativeRecomp(self.aoThreadedFuncs, sHostArch);

        # Gather arguments + variable statistics for the MC blocks.
        cMaxArgs         = 0;
        cMaxVars         = 0;
        cMaxVarsAndArgs  = 0;
        cbMaxArgs        = 0;
        cbMaxVars        = 0;
        cbMaxVarsAndArgs = 0;
        for oThreadedFunction in self.aoThreadedFuncs:
            if oThreadedFunction.oMcBlock.aoLocals or oThreadedFunction.oMcBlock.aoArgs:
                # Counts.
                cMaxVars        = max(cMaxVars, len(oThreadedFunction.oMcBlock.aoLocals));
                cMaxArgs        = max(cMaxArgs, len(oThreadedFunction.oMcBlock.aoArgs));
                cMaxVarsAndArgs = max(cMaxVarsAndArgs,
                                      len(oThreadedFunction.oMcBlock.aoLocals) + len(oThreadedFunction.oMcBlock.aoArgs));
                if cMaxVarsAndArgs > 9:
                    raise Exception('%s potentially uses too many variables / args: %u, max 10 - %u vars and %u args'
                                    % (oThreadedFunction.oMcBlock.oFunction.sName, cMaxVarsAndArgs,
                                       len(oThreadedFunction.oMcBlock.aoLocals), len(oThreadedFunction.oMcBlock.aoArgs),));
                # Calc stack allocation size:
                cbArgs = 0;
                for oArg in oThreadedFunction.oMcBlock.aoArgs:
                    cbArgs += (getTypeBitCount(oArg.sType) + 63) // 64 * 8;
                cbVars = 0;
                for oVar in oThreadedFunction.oMcBlock.aoLocals:
                    cbVars += (getTypeBitCount(oVar.sType) + 63) // 64 * 8;
                cbMaxVars        = max(cbMaxVars, cbVars);
                cbMaxArgs        = max(cbMaxArgs, cbArgs);
                cbMaxVarsAndArgs = max(cbMaxVarsAndArgs, cbVars + cbArgs);
                if cbMaxVarsAndArgs >= 0xc0:
                    raise Exception('%s potentially uses too much stack: cbMaxVars=%#x cbMaxArgs=%#x'
                                    % (oThreadedFunction.oMcBlock.oFunction.sName, cbMaxVars, cbMaxArgs,));

        print('debug: max vars+args: %u bytes / %u; max vars: %u bytes / %u; max args: %u bytes / %u'
              % (cbMaxVarsAndArgs, cMaxVarsAndArgs, cbMaxVars, cMaxVars, cbMaxArgs, cMaxArgs,), file = sys.stderr);

        if self.cErrors > 0:
            print('fatal error: %u error%s during processing. Details above.'
                  % (self.cErrors, 's' if self.cErrors > 1 else '',), file = sys.stderr);
            return False;
        return True;

    #
    # Output
    #

    def generateLicenseHeader(self):
        """
        Returns the lines for a license header.
        """
        return [
            '/*',
            ' * Autogenerated by $Id: IEMAllThrdPython.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ ',
            ' * Do not edit!',
            ' */',
            '',
            '/*',
            ' * Copyright (C) 2023-' + str(datetime.date.today().year) + ' Oracle and/or its affiliates.',
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
            '',
        ];

    ## List of built-in threaded functions with user argument counts and
    ## whether it has a native recompiler implementation.
    katBltIns = (
        ( 'Nop',                                                0, True  ),
        ( 'LogCpuState',                                        0, True  ),

        ( 'DeferToCImpl0',                                      2, True  ),
        ( 'CheckIrq',                                           0, True  ),
        ( 'CheckTimers',                                        0, True  ),
        ( 'CheckTimersAndIrq',                                  0, True  ),
        ( 'CheckMode',                                          1, True  ),
        ( 'CheckHwInstrBps',                                    0, False ),
        ( 'CheckCsLim',                                         1, True  ),

        ( 'CheckCsLimAndOpcodes',                               3, True  ),
        ( 'CheckOpcodes',                                       3, True  ),
        ( 'CheckOpcodesConsiderCsLim',                          3, True  ),

        ( 'CheckCsLimAndPcAndOpcodes',                          3, True  ),
        ( 'CheckPcAndOpcodes',                                  3, True  ),
        ( 'CheckPcAndOpcodesConsiderCsLim',                     3, True  ),

        ( 'CheckCsLimAndOpcodesAcrossPageLoadingTlb',           3, True  ),
        ( 'CheckOpcodesAcrossPageLoadingTlb',                   3, True  ),
        ( 'CheckOpcodesAcrossPageLoadingTlbConsiderCsLim',      2, True  ),

        ( 'CheckCsLimAndOpcodesLoadingTlb',                     3, True  ),
        ( 'CheckOpcodesLoadingTlb',                             3, True  ),
        ( 'CheckOpcodesLoadingTlbConsiderCsLim',                3, True  ),

        ( 'CheckCsLimAndOpcodesOnNextPageLoadingTlb',           2, True  ),
        ( 'CheckOpcodesOnNextPageLoadingTlb',                   2, True  ),
        ( 'CheckOpcodesOnNextPageLoadingTlbConsiderCsLim',      2, True  ),

        ( 'CheckCsLimAndOpcodesOnNewPageLoadingTlb',            2, True  ),
        ( 'CheckOpcodesOnNewPageLoadingTlb',                    2, True  ),
        ( 'CheckOpcodesOnNewPageLoadingTlbConsiderCsLim',       2, True  ),

        ( 'Jump',                                               1, True  ),
    );

    def generateThreadedFunctionsHeader(self, oOut, _):
        """
        Generates the threaded functions header file.
        Returns success indicator.
        """

        asLines = self.generateLicenseHeader();

        # Generate the threaded function table indexes.
        asLines += [
            'typedef enum IEMTHREADEDFUNCS',
            '{',
            '    kIemThreadedFunc_Invalid = 0,',
            '',
            '    /*',
            '     * Predefined',
            '     */',
        ];
        asLines += ['    kIemThreadedFunc_BltIn_%s,' % (sFuncNm,) for sFuncNm, _, _ in self.katBltIns];

        iThreadedFunction = 1 + len(self.katBltIns);
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
            asLines += [
                    '',
                    '    /*',
                    '     * Variation: ' + ThreadedFunctionVariation.kdVariationNames[sVariation] + '',
                    '     */',
            ];
            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None);
                if oVariation:
                    iThreadedFunction += 1;
                    oVariation.iEnumValue = iThreadedFunction;
                    asLines.append('    ' + oVariation.getIndexName() + ',');
        asLines += [
            '    kIemThreadedFunc_End',
            '} IEMTHREADEDFUNCS;',
            '',
        ];

        # Prototype the function table.
        asLines += [
            'extern const PFNIEMTHREADEDFUNC g_apfnIemThreadedFunctions[kIemThreadedFunc_End];',
            'extern uint8_t const            g_acIemThreadedFunctionUsedArgs[kIemThreadedFunc_End];',
            '#if defined(IN_RING3) || defined(LOG_ENABLED)',
            'extern const char * const       g_apszIemThreadedFunctions[kIemThreadedFunc_End];',
            '#endif',
            '#if defined(IN_RING3)',
            'extern const char * const       g_apszIemThreadedFunctionStats[kIemThreadedFunc_End];',
            '#endif',
        ];

        oOut.write('\n'.join(asLines));
        return True;

    ksBitsToIntMask = {
        1:  "UINT64_C(0x1)",
        2:  "UINT64_C(0x3)",
        4:  "UINT64_C(0xf)",
        8:  "UINT64_C(0xff)",
        16: "UINT64_C(0xffff)",
        32: "UINT64_C(0xffffffff)",
    };

    def generateFunctionParameterUnpacking(self, oVariation, oOut, asParams, uNoRefLevel = 0):
        """
        Outputs code for unpacking parameters.
        This is shared by the threaded and native code generators.
        """
        aasVars = [];
        for aoRefs in oVariation.dParamRefs.values():
            oRef  = aoRefs[0];
            if oRef.sType[0] != 'P':
                cBits = g_kdTypeInfo[oRef.sType][0];
                sType = g_kdTypeInfo[oRef.sType][2];
            else:
                cBits = 64;
                sType = oRef.sType;

            sTypeDecl = sType + ' const';

            if cBits == 64:
                assert oRef.offNewParam == 0;
                if sType == 'uint64_t':
                    sUnpack = '%s;' % (asParams[oRef.iNewParam],);
                else:
                    sUnpack = '(%s)%s;' % (sType, asParams[oRef.iNewParam],);
            elif oRef.offNewParam == 0:
                sUnpack = '(%s)(%s & %s);' % (sType, asParams[oRef.iNewParam], self.ksBitsToIntMask[cBits]);
            else:
                sUnpack = '(%s)((%s >> %s) & %s);' \
                        % (sType, asParams[oRef.iNewParam], oRef.offNewParam, self.ksBitsToIntMask[cBits]);

            sComment = '/* %s - %s ref%s */' % (oRef.sOrgRef, len(aoRefs), 's' if len(aoRefs) != 1 else '',);

            aasVars.append([ '%s:%02u' % (oRef.iNewParam, oRef.offNewParam),
                             sTypeDecl, oRef.sNewName, sUnpack, sComment ]);
        acchVars = [0, 0, 0, 0, 0];
        for asVar in aasVars:
            for iCol, sStr in enumerate(asVar):
                acchVars[iCol] = max(acchVars[iCol], len(sStr));
        sFmt = '    %%-%ss %%-%ss = %%-%ss %%s\n' % (acchVars[1], acchVars[2], acchVars[3]);
        for asVar in sorted(aasVars):
            oOut.write(sFmt % (asVar[1], asVar[2], asVar[3], asVar[4],));

        if uNoRefLevel > 0 and aasVars:
            if uNoRefLevel > 1:
                # level 2: Everything. This is used by liveness.
                oOut.write('   ');
                for asVar in sorted(aasVars):
                    oOut.write(' RT_NOREF_PV(%s);' % (asVar[2],));
                oOut.write('\n');
            else:
                # level 1: Only pfnXxxx variables.  This is used by native.
                for asVar in sorted(aasVars):
                    if asVar[2].startswith('pfn'):
                        oOut.write('    RT_NOREF_PV(%s);\n' % (asVar[2],));
        return True;

    kasThreadedParamNames = ('uParam0', 'uParam1', 'uParam2');
    def generateThreadedFunctionsSource(self, oOut, _):
        """
        Generates the threaded functions source file.
        Returns success indicator.
        """

        asLines = self.generateLicenseHeader();
        oOut.write('\n'.join(asLines));

        #
        # Emit the function definitions.
        #
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
            sVarName = ThreadedFunctionVariation.kdVariationNames[sVariation];
            oOut.write(  '\n'
                       + '\n'
                       + '\n'
                       + '\n'
                       + '/*' + '*' * 128 + '\n'
                       + '*   Variation: ' + sVarName + ' ' * (129 - len(sVarName) - 15) + '*\n'
                       + '*' * 128 + '*/\n');

            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None);
                if oVariation:
                    oMcBlock = oThreadedFunction.oMcBlock;

                    # Function header
                    oOut.write(  '\n'
                               + '\n'
                               + '/**\n'
                               + ' * #%u: %s at line %s offset %s in %s%s\n'
                                  % (oVariation.iEnumValue, oMcBlock.sFunction, oMcBlock.iBeginLine, oMcBlock.offBeginLine,
                                     os.path.split(oMcBlock.sSrcFile)[1],
                                     ' (macro expansion)' if oMcBlock.iBeginLine == oMcBlock.iEndLine else '')
                               + ' */\n'
                               + 'static IEM_DECL_IEMTHREADEDFUNC_DEF(' + oVariation.getThreadedFunctionName() + ')\n'
                               + '{\n');

                    # Unpack parameters.
                    self.generateFunctionParameterUnpacking(oVariation, oOut, self.kasThreadedParamNames);

                    # RT_NOREF for unused parameters.
                    if oVariation.cMinParams < g_kcThreadedParams:
                        oOut.write('    RT_NOREF(' + ', '.join(self.kasThreadedParamNames[oVariation.cMinParams:]) + ');\n');

                    # Now for the actual statements.
                    oOut.write(iai.McStmt.renderCodeForList(oVariation.aoStmtsForThreadedFunction, cchIndent = 4));

                    oOut.write('}\n');


        #
        # Generate the output tables in parallel.
        #
        asFuncTable = [
            '/**',
            ' * Function pointer table.',
            ' */',
            'PFNIEMTHREADEDFUNC const g_apfnIemThreadedFunctions[kIemThreadedFunc_End] =',
            '{',
            '    /*Invalid*/ NULL,',
        ];
        asArgCntTab = [
            '/**',
            ' * Argument count table.',
            ' */',
            'uint8_t const g_acIemThreadedFunctionUsedArgs[kIemThreadedFunc_End] =',
            '{',
            '    0, /*Invalid*/',
        ];
        asNameTable = [
            '/**',
            ' * Function name table.',
            ' */',
            'const char * const g_apszIemThreadedFunctions[kIemThreadedFunc_End] =',
            '{',
            '    "Invalid",',
        ];
        asStatTable = [
            '/**',
            ' * Function statistics name table.',
            ' */',
            'const char * const g_apszIemThreadedFunctionStats[kIemThreadedFunc_End] =',
            '{',
            '    NULL,',
        ];
        aasTables = (asFuncTable, asArgCntTab, asNameTable, asStatTable,);

        for asTable in aasTables:
            asTable.extend((
                '',
                '    /*',
                '     * Predefined.',
                '     */',
            ));
        for sFuncNm, cArgs, _ in self.katBltIns:
            asFuncTable.append('    iemThreadedFunc_BltIn_%s,' % (sFuncNm,));
            asArgCntTab.append('    %d, /*BltIn_%s*/' % (cArgs, sFuncNm,));
            asNameTable.append('    "BltIn_%s",' % (sFuncNm,));
            asStatTable.append('    "BltIn/%s",' % (sFuncNm,));

        iThreadedFunction = 1 + len(self.katBltIns);
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
            for asTable in aasTables:
                asTable.extend((
                    '',
                    '    /*',
                    '     * Variation: ' + ThreadedFunctionVariation.kdVariationNames[sVariation],
                    '     */',
                ));
            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None);
                if oVariation:
                    iThreadedFunction += 1;
                    assert oVariation.iEnumValue == iThreadedFunction;
                    sName = oVariation.getThreadedFunctionName();
                    asFuncTable.append('    /*%4u*/ %s,' % (iThreadedFunction, sName,));
                    asNameTable.append('    /*%4u*/ "%s",' % (iThreadedFunction, sName,));
                    asArgCntTab.append('    /*%4u*/ %d, /*%s*/' % (iThreadedFunction, oVariation.cMinParams, sName,));
                    asStatTable.append('    "%s",' % (oVariation.getThreadedFunctionStatisticsName(),));

        for asTable in aasTables:
            asTable.append('};');

        #
        # Output the tables.
        #
        oOut.write(  '\n'
                   + '\n');
        oOut.write('\n'.join(asFuncTable));
        oOut.write(  '\n'
                   + '\n'
                   + '\n');
        oOut.write('\n'.join(asArgCntTab));
        oOut.write(  '\n'
                   + '\n'
                   + '#if defined(IN_RING3) || defined(LOG_ENABLED)\n');
        oOut.write('\n'.join(asNameTable));
        oOut.write(  '\n'
                   + '#endif /* IN_RING3 || LOG_ENABLED */\n'
                   + '\n'
                   + '\n'
                   + '#if defined(IN_RING3)\n');
        oOut.write('\n'.join(asStatTable));
        oOut.write(  '\n'
                   + '#endif /* IN_RING3 */\n');

        return True;

    def generateNativeFunctionsHeader(self, oOut, _):
        """
        Generates the native recompiler functions header file.
        Returns success indicator.
        """
        if not self.oOptions.fNativeRecompilerEnabled:
            return True;

        asLines = self.generateLicenseHeader();

        # Prototype the function table.
        asLines += [
            'extern const PFNIEMNATIVERECOMPFUNC   g_apfnIemNativeRecompileFunctions[kIemThreadedFunc_End];',
            'extern const PFNIEMNATIVELIVENESSFUNC g_apfnIemNativeLivenessFunctions[kIemThreadedFunc_End];',
            '',
        ];

        # Emit indicators as to which of the builtin functions have a native
        # recompiler function and which not.  (We only really need this for
        # kIemThreadedFunc_BltIn_CheckMode, but do all just for simplicity.)
        for atBltIn in self.katBltIns:
            if atBltIn[1]:
                asLines.append('#define IEMNATIVE_WITH_BLTIN_' + atBltIn[0].upper())
            else:
                asLines.append('#define IEMNATIVE_WITHOUT_BLTIN_' + atBltIn[0].upper())

        # Emit prototypes for the builtin functions we use in tables.
        asLines += [
            '',
            '/* Prototypes for built-in functions used in the above tables. */',
        ];
        for sFuncNm, _, fHaveRecompFunc in self.katBltIns:
            if fHaveRecompFunc:
                asLines += [
                    'IEM_DECL_IEMNATIVERECOMPFUNC_PROTO(    iemNativeRecompFunc_BltIn_%s);' % (sFuncNm,),
                    'IEM_DECL_IEMNATIVELIVENESSFUNC_PROTO(iemNativeLivenessFunc_BltIn_%s);' % (sFuncNm,),
                ];

        # Emit prototypes for table function.
        asLines += [
            '',
            '#ifdef IEMNATIVE_INCL_TABLE_FUNCTION_PROTOTYPES'
        ]
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
            sVarName = ThreadedFunctionVariation.kdVariationNames[sVariation];
            asLines += [
                '',
                '/* Variation: ' + sVarName + ' */',
            ];
            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None) # type: ThreadedFunctionVariation
                if oVariation and oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                    asLines.append('IEM_DECL_IEMNATIVERECOMPFUNC_PROTO(' + oVariation.getNativeFunctionName() + ');');
        asLines += [
            '',
            '#endif /* IEMNATIVE_INCL_TABLE_FUNCTION_PROTOTYPES */',
        ]

        oOut.write('\n'.join(asLines));
        return True;

    # This applies to both generateNativeFunctionsSource and generateNativeLivenessSource.
    kcNativeSourceParts = 6;

    def generateNativeFunctionsSource(self, oOut, idxPart):
        """
        Generates the native recompiler functions source file.
        Returns success indicator.
        """
        assert(idxPart in range(self.kcNativeSourceParts));
        if not self.oOptions.fNativeRecompilerEnabled:
            return True;

        #
        # The file header.
        #
        oOut.write('\n'.join(self.generateLicenseHeader()));

        #
        # Emit the functions.
        #
        # The files are split up by threaded variation as that's the simplest way to
        # do it, even if the distribution isn't entirely even (ksVariation_Default
        # only has the defer to cimpl bits and the pre-386 variants will naturally
        # have fewer instructions).
        #
        cVariationsPerFile = len(ThreadedFunctionVariation.kasVariationsEmitOrder) // self.kcNativeSourceParts;
        idxFirstVar        = idxPart * cVariationsPerFile;
        idxEndVar          = idxFirstVar + cVariationsPerFile;
        if idxPart + 1 >= self.kcNativeSourceParts:
            idxEndVar      = len(ThreadedFunctionVariation.kasVariationsEmitOrder);
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder[idxFirstVar:idxEndVar]:
            sVarName = ThreadedFunctionVariation.kdVariationNames[sVariation];
            oOut.write(  '\n'
                       + '\n'
                       + '\n'
                       + '\n'
                       + '/*' + '*' * 128 + '\n'
                       + '*   Variation: ' + sVarName + ' ' * (129 - len(sVarName) - 15) + '*\n'
                       + '*' * 128 + '*/\n');

            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None) # type: ThreadedFunctionVariation
                if oVariation and oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                    oMcBlock = oThreadedFunction.oMcBlock;

                    # Function header
                    oOut.write(  '\n'
                               + '\n'
                               + '/**\n'
                               + ' * #%u: %s at line %s offset %s in %s%s\n'
                                  % (oVariation.iEnumValue, oMcBlock.sFunction, oMcBlock.iBeginLine, oMcBlock.offBeginLine,
                                     os.path.split(oMcBlock.sSrcFile)[1],
                                     ' (macro expansion)' if oMcBlock.iBeginLine == oMcBlock.iEndLine else '')
                               + ' */\n'
                               + 'IEM_DECL_IEMNATIVERECOMPFUNC_DEF(' + oVariation.getNativeFunctionName() + ')\n'
                               + '{\n');

                    # Unpack parameters.
                    self.generateFunctionParameterUnpacking(oVariation, oOut,
                                                            ('pCallEntry->auParams[0]',
                                                             'pCallEntry->auParams[1]',
                                                             'pCallEntry->auParams[2]',),
                                                            uNoRefLevel = 1);

                    # Now for the actual statements.
                    oOut.write(oVariation.oNativeRecomp.renderCode(cchIndent = 4));

                    oOut.write('}\n');

        #
        # Output the function table in the smallest file (currently the last).
        #
        if idxPart + 1 == self.kcNativeSourceParts:
            oOut.write(   '\n'
                        + '\n'
                        + '/*\n'
                        + ' * Function table running parallel to g_apfnIemThreadedFunctions and friends.\n'
                        + ' */\n'
                        + 'const PFNIEMNATIVERECOMPFUNC g_apfnIemNativeRecompileFunctions[kIemThreadedFunc_End] =\n'
                        + '{\n'
                        + '    /*Invalid*/ NULL,'
                        + '\n'
                        + '    /*\n'
                        + '     * Predefined.\n'
                        + '     */\n'
                        );
            for sFuncNm, _, fHaveRecompFunc in self.katBltIns:
                if fHaveRecompFunc:
                    oOut.write('    iemNativeRecompFunc_BltIn_%s,\n' % (sFuncNm,))
                else:
                    oOut.write('    NULL, /*BltIn_%s*/\n' % (sFuncNm,))

            iThreadedFunction = 1 + len(self.katBltIns);
            for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
                oOut.write(  '    /*\n'
                           + '     * Variation: ' + ThreadedFunctionVariation.kdVariationNames[sVariation] + '\n'
                           + '     */\n');
                for oThreadedFunction in self.aoThreadedFuncs:
                    oVariation = oThreadedFunction.dVariations.get(sVariation, None);
                    if oVariation:
                        iThreadedFunction += 1;
                        assert oVariation.iEnumValue == iThreadedFunction;
                        sName = oVariation.getNativeFunctionName();
                        if oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                            oOut.write('    /*%4u*/ %s,\n' % (iThreadedFunction, sName,));
                        else:
                            oOut.write('    /*%4u*/ NULL /*%s*/,\n' % (iThreadedFunction, sName,));

            oOut.write(  '};\n');

        oOut.write('\n');
        return True;

    def generateNativeLivenessHeader(self, oOut, _):
        """
        Generates the internal native recompiler liveness header file.
        Returns success indicator.
        """
        if not self.oOptions.fNativeRecompilerEnabled:
            return True;

        oOut.write('\n'.join(self.generateLicenseHeader()));
        oOut.write(   '\n'
                   + '/*\n'
                   + ' * Liveness analysis function prototypes.\n'
                   + ' */\n');

        # Emit prototypes for the liveness table functions.
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
            sVarName = ThreadedFunctionVariation.kdVariationNames[sVariation];
            oOut.write('/*   Variation: ' + sVarName + ' */\n');
            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None) # type: ThreadedFunctionVariation
                if oVariation and oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                    oOut.write('IEM_DECL_IEMNATIVELIVENESSFUNC_PROTO(' + oVariation.getLivenessFunctionName() + ');\n');

        oOut.write('\n');
        return True;


    def generateNativeLivenessSource(self, oOut, idxPart):
        """
        Generates the native recompiler liveness analysis functions source file.
        Returns success indicator.
        """
        assert(idxPart in range(self.kcNativeSourceParts));
        if not self.oOptions.fNativeRecompilerEnabled:
            return True;

        #
        # The file header.
        #
        oOut.write('\n'.join(self.generateLicenseHeader()));

        #
        # Emit the functions.
        #
        # The files are split up by threaded variation as that's the simplest way to
        # do it, even if the distribution isn't entirely even (ksVariation_Default
        # only has the defer to cimpl bits and the pre-386 variants will naturally
        # have fewer instructions).
        #
        cVariationsPerFile = len(ThreadedFunctionVariation.kasVariationsEmitOrder) // self.kcNativeSourceParts;
        idxFirstVar        = idxPart * cVariationsPerFile;
        idxEndVar          = idxFirstVar + cVariationsPerFile;
        if idxPart + 1 >= self.kcNativeSourceParts:
            idxEndVar      = len(ThreadedFunctionVariation.kasVariationsEmitOrder);
        for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder[idxFirstVar:idxEndVar]:
            sVarName = ThreadedFunctionVariation.kdVariationNames[sVariation];
            oOut.write(  '\n'
                       + '\n'
                       + '\n'
                       + '\n'
                       + '/*' + '*' * 128 + '\n'
                       + '*   Variation: ' + sVarName + ' ' * (129 - len(sVarName) - 15) + '*\n'
                       + '*' * 128 + '*/\n');

            for oThreadedFunction in self.aoThreadedFuncs:
                oVariation = oThreadedFunction.dVariations.get(sVariation, None) # type: ThreadedFunctionVariation
                if oVariation and oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                    oMcBlock = oThreadedFunction.oMcBlock;

                    # Function header
                    oOut.write(  '\n'
                               + '\n'
                               + '/**\n'
                               + ' * #%u: %s at line %s offset %s in %s%s\n'
                                  % (oVariation.iEnumValue, oMcBlock.sFunction, oMcBlock.iBeginLine, oMcBlock.offBeginLine,
                                     os.path.split(oMcBlock.sSrcFile)[1],
                                     ' (macro expansion)' if oMcBlock.iBeginLine == oMcBlock.iEndLine else '')
                               + ' */\n'
                               + 'IEM_DECL_IEMNATIVELIVENESSFUNC_DEF(' + oVariation.getLivenessFunctionName() + ')\n'
                               + '{\n');

                    # Unpack parameters.
                    self.generateFunctionParameterUnpacking(oVariation, oOut,
                                                            ('pCallEntry->auParams[0]',
                                                             'pCallEntry->auParams[1]',
                                                             'pCallEntry->auParams[2]',),
                                                            uNoRefLevel = 2);

                    # Now for the actual statements.
                    oOut.write(oVariation.oNativeRecomp.renderCode(cchIndent = 4));

                    oOut.write('}\n');

        #
        # Output the function table in the smallest file (currently the last).
        #
        if idxPart + 1 == self.kcNativeSourceParts:
            oOut.write(   '\n'
                        + '\n'
                        + '\n'
                        + '/*\n'
                        + ' * Liveness analysis function table running parallel to g_apfnIemThreadedFunctions and friends.\n'
                        + ' */\n'
                        + 'const PFNIEMNATIVELIVENESSFUNC g_apfnIemNativeLivenessFunctions[kIemThreadedFunc_End] =\n'
                        + '{\n'
                        + '    /*Invalid*/ NULL,'
                        + '\n'
                        + '    /*\n'
                        + '     * Predefined.\n'
                        + '     */\n'
                        );
            for sFuncNm, _, fHaveRecompFunc in self.katBltIns:
                if fHaveRecompFunc:
                    oOut.write('    iemNativeLivenessFunc_BltIn_%s,\n' % (sFuncNm,))
                else:
                    oOut.write('    iemNativeLivenessFunc_ThreadedCall, /*BltIn_%s*/\n' % (sFuncNm,))

            iThreadedFunction = 1 + len(self.katBltIns);
            for sVariation in ThreadedFunctionVariation.kasVariationsEmitOrder:
                oOut.write(  '    /*\n'
                           + '     * Variation: ' + ThreadedFunctionVariation.kdVariationNames[sVariation] + '\n'
                           + '     */\n');
                for oThreadedFunction in self.aoThreadedFuncs:
                    oVariation = oThreadedFunction.dVariations.get(sVariation, None);
                    if oVariation:
                        iThreadedFunction += 1;
                        assert oVariation.iEnumValue == iThreadedFunction;
                        sName = oVariation.getLivenessFunctionName();
                        if oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                            oOut.write('    /*%4u*/ %s,\n' % (iThreadedFunction, sName,));
                        else:
                            oOut.write('    /*%4u*/ iemNativeLivenessFunc_ThreadedCall /*%s*/,\n' % (iThreadedFunction, sName,));

            oOut.write(  '};\n'
                       + '\n');
        return True;


    def getThreadedFunctionByIndex(self, idx):
        """
        Returns a ThreadedFunction object for the given index.  If the index is
        out of bounds, a dummy is returned.
        """
        if idx < len(self.aoThreadedFuncs):
            return self.aoThreadedFuncs[idx];
        return ThreadedFunction.dummyInstance();

    def generateModifiedInput(self, oOut, idxFile):
        """
        Generates the combined modified input source/header file.
        Returns success indicator.
        """
        #
        # File header and assert assumptions.
        #
        oOut.write('\n'.join(self.generateLicenseHeader()));
        oOut.write('AssertCompile((IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK | IEM_F_MODE_CPUMODE_MASK) == 7);\n');

        #
        # Iterate all parsers (input files) and output the ones related to the
        # file set given by idxFile.
        #
        for idxParser, oParser in enumerate(self.aoParsers): # type: int, IEMAllInstPython.SimpleParser
            # Is this included in the file set?
            sSrcBaseFile = os.path.basename(oParser.sSrcFile).lower();
            fInclude     = -1;
            for aoInfo in iai.g_aaoAllInstrFilesAndDefaultMapAndSet:
                if sSrcBaseFile == aoInfo[0].lower():
                    fInclude = aoInfo[2] in (-1, idxFile);
                    break;
            if fInclude is not True:
                assert fInclude is False;
                continue;

            # Output it.
            oOut.write("\n\n/* ****** BEGIN %s ******* */\n" % (oParser.sSrcFile,));

            iThreadedFunction = self.aidxFirstFunctions[idxParser];
            oThreadedFunction = self.getThreadedFunctionByIndex(iThreadedFunction);
            iLine             = 0;
            while iLine < len(oParser.asLines):
                sLine = oParser.asLines[iLine];
                iLine += 1;                 # iBeginLine and iEndLine are 1-based.

                # Can we pass it thru?
                if (   iLine not in [oThreadedFunction.oMcBlock.iBeginLine, oThreadedFunction.oMcBlock.iEndLine]
                    or oThreadedFunction.oMcBlock.sSrcFile != oParser.sSrcFile):
                    oOut.write(sLine);
                #
                # Single MC block.  Just extract it and insert the replacement.
                #
                elif oThreadedFunction.oMcBlock.iBeginLine != oThreadedFunction.oMcBlock.iEndLine:
                    assert (   (sLine.count('IEM_MC_') - sLine.count('IEM_MC_F_') == 1)
                            or oThreadedFunction.oMcBlock.iMacroExp == iai.McBlock.kiMacroExp_Partial), 'sLine="%s"' % (sLine,);
                    oOut.write(sLine[:oThreadedFunction.oMcBlock.offBeginLine]);
                    sModified = oThreadedFunction.generateInputCode().strip();
                    oOut.write(sModified);

                    iLine = oThreadedFunction.oMcBlock.iEndLine;
                    sLine = oParser.asLines[iLine - 1];
                    assert (   sLine.count('IEM_MC_') - sLine.count('IEM_MC_F_') == 1
                            or len(oThreadedFunction.oMcBlock.aoStmts) == 1
                            or oThreadedFunction.oMcBlock.iMacroExp == iai.McBlock.kiMacroExp_Partial);
                    oOut.write(sLine[oThreadedFunction.oMcBlock.offAfterEnd : ]);

                    # Advance
                    iThreadedFunction += 1;
                    oThreadedFunction  = self.getThreadedFunctionByIndex(iThreadedFunction);
                #
                # Macro expansion line that have sublines and may contain multiple MC blocks.
                #
                else:
                    offLine = 0;
                    while iLine == oThreadedFunction.oMcBlock.iBeginLine:
                        oOut.write(sLine[offLine : oThreadedFunction.oMcBlock.offBeginLine]);

                        sModified = oThreadedFunction.generateInputCode().strip();
                        assert (   sModified.startswith('IEM_MC_BEGIN')
                                or (sModified.find('IEM_MC_DEFER_TO_CIMPL_') > 0 and sModified.strip().startswith('{\n'))
                                or sModified.startswith('pVCpu->iem.s.fEndTb = true')
                                or sModified.startswith('pVCpu->iem.s.fTbCurInstr = ')
                                ), 'sModified="%s"' % (sModified,);
                        oOut.write(sModified);

                        offLine = oThreadedFunction.oMcBlock.offAfterEnd;

                        # Advance
                        iThreadedFunction += 1;
                        oThreadedFunction  = self.getThreadedFunctionByIndex(iThreadedFunction);

                    # Last line segment.
                    if offLine < len(sLine):
                        oOut.write(sLine[offLine : ]);

            oOut.write("/* ****** END %s ******* */\n" % (oParser.sSrcFile,));

        return True;


    #
    # Main
    #

    def main(self, asArgs):
        """
        C-like main function.
        Returns exit code.
        """

        #
        # Parse arguments
        #
        sScriptDir = os.path.dirname(__file__);
        oParser = argparse.ArgumentParser(add_help = False);
        oParser.add_argument('asInFiles',
                             metavar = 'input.cpp.h',
                             nargs   = '*',
                             default = [os.path.join(sScriptDir, aoInfo[0])
                                        for aoInfo in iai.g_aaoAllInstrFilesAndDefaultMapAndSet],
                             help    = "Selection of VMMAll/IEMAllInst*.cpp.h files to use as input.");
        oParser.add_argument('--host-arch',
                             metavar = 'arch',
                             dest    = 'sHostArch',
                             action  = 'store',
                             default = None,
                             help    = 'The host architecture.');

        oParser.add_argument('--out-thrd-funcs-hdr',
                             metavar = 'file-thrd-funcs.h',
                             dest    = 'sOutFileThrdFuncsHdr',
                             action  = 'store',
                             default = '-',
                             help    = 'The output header file for the threaded functions.');
        oParser.add_argument('--out-thrd-funcs-cpp',
                             metavar = 'file-thrd-funcs.cpp',
                             dest    = 'sOutFileThrdFuncsCpp',
                             action  = 'store',
                             default = '-',
                             help    = 'The output C++ file for the threaded functions.');
        oParser.add_argument('--out-n8ve-funcs-hdr',
                             metavar = 'file-n8tv-funcs.h',
                             dest    = 'sOutFileN8veFuncsHdr',
                             action  = 'store',
                             default = '-',
                             help    = 'The output header file for the native recompiler functions.');
        for iFile in range(1, self.kcNativeSourceParts + 1):
            oParser.add_argument('--out-n8ve-funcs-cpp%u' % (iFile,),
                                 metavar = 'file-n8tv-funcs%u.cpp' % (iFile,),
                                 dest    = 'sOutFileN8veFuncsCpp%u' % (iFile,),
                                 action  = 'store',
                                 default = '-',
                                 help    = 'The output C++ file for the native recompiler functions part %u.' % (iFile,));
        oParser.add_argument('--out-n8ve-liveness-hdr',
                             metavar = 'file-n8ve-liveness.h',
                             dest    = 'sOutFileN8veLivenessHdr',
                             action  = 'store',
                             default = '-',
                             help    = 'The output header file for the native recompiler liveness analysis functions.');
        for iFile in range(1, self.kcNativeSourceParts + 1):
            oParser.add_argument('--out-n8ve-liveness-cpp%u' % (iFile,),
                                 metavar = 'file-n8ve-liveness%u.cpp' % (iFile,),
                                 dest    = 'sOutFileN8veLivenessCpp%u' % (iFile,),
                                 action  = 'store',
                                 default = '-',
                                 help    = 'The output C++ file for the native recompiler liveness analysis functions part %u.'
                                            % (iFile,));
        oParser.add_argument('--native',
                             dest    = 'fNativeRecompilerEnabled',
                             action  = 'store_true',
                             default = False,
                             help    = 'Enables generating the files related to native recompilation.');
        oParser.add_argument('--out-mod-input1',
                             metavar = 'file-instr.cpp.h',
                             dest    = 'sOutFileModInput1',
                             action  = 'store',
                             default = '-',
                             help    = 'The output C++/header file for modified input instruction files part 1.');
        oParser.add_argument('--out-mod-input2',
                             metavar = 'file-instr.cpp.h',
                             dest    = 'sOutFileModInput2',
                             action  = 'store',
                             default = '-',
                             help    = 'The output C++/header file for modified input instruction files part 2.');
        oParser.add_argument('--out-mod-input3',
                             metavar = 'file-instr.cpp.h',
                             dest    = 'sOutFileModInput3',
                             action  = 'store',
                             default = '-',
                             help    = 'The output C++/header file for modified input instruction files part 3.');
        oParser.add_argument('--out-mod-input4',
                             metavar = 'file-instr.cpp.h',
                             dest    = 'sOutFileModInput4',
                             action  = 'store',
                             default = '-',
                             help    = 'The output C++/header file for modified input instruction files part 4.');
        oParser.add_argument('--help', '-h', '-?',
                             action  = 'help',
                             help    = 'Display help and exit.');
        oParser.add_argument('--version', '-V',
                             action  = 'version',
                             version = 'r%s (IEMAllThreadedPython.py), r%s (IEMAllInstPython.py)'
                                     % (__version__.split()[1], iai.__version__.split()[1],),
                             help    = 'Displays the version/revision of the script and exit.');
        self.oOptions = oParser.parse_args(asArgs[1:]);
        print("oOptions=%s" % (self.oOptions,), file = sys.stderr);

        if self.oOptions.sHostArch not in ('amd64', 'arm64'):
            print('error! Unsupported (or missing) host architecture: %s' % (self.oOptions.sHostArch,), file = sys.stderr);
            return 1;

        #
        # Process the instructions specified in the IEM sources.
        #
        if self.processInputFiles(self.oOptions.sHostArch, self.oOptions.fNativeRecompilerEnabled):
            #
            # Generate the output files.
            #
            aaoOutputFiles = [
                 ( self.oOptions.sOutFileThrdFuncsHdr,      self.generateThreadedFunctionsHeader, 0, ),
                 ( self.oOptions.sOutFileThrdFuncsCpp,      self.generateThreadedFunctionsSource, 0, ),
                 ( self.oOptions.sOutFileN8veFuncsHdr,      self.generateNativeFunctionsHeader,   0, ),
                 ( self.oOptions.sOutFileN8veLivenessHdr,   self.generateNativeLivenessHeader,    0, ),
                 ( self.oOptions.sOutFileModInput1,         self.generateModifiedInput,           1, ),
                 ( self.oOptions.sOutFileModInput2,         self.generateModifiedInput,           2, ),
                 ( self.oOptions.sOutFileModInput3,         self.generateModifiedInput,           3, ),
                 ( self.oOptions.sOutFileModInput4,         self.generateModifiedInput,           4, ),
            ];
            for iFile in range(self.kcNativeSourceParts):
                aaoOutputFiles.extend([
                    ( getattr(self.oOptions, 'sOutFileN8veFuncsCpp%u' % (iFile + 1)),
                      self.generateNativeFunctionsSource, iFile, ),
                    ( getattr(self.oOptions, 'sOutFileN8veLivenessCpp%u' % (iFile + 1)),
                      self.generateNativeLivenessSource, iFile, ),
                ]);
            fRc = True;
            for sOutFile, fnGenMethod, iPartNo in aaoOutputFiles:
                if sOutFile == '-':
                    fRc = fnGenMethod(sys.stdout, iPartNo) and fRc;
                else:
                    try:
                        oOut = open(sOutFile, 'w');                 # pylint: disable=consider-using-with,unspecified-encoding
                    except Exception as oXcpt:
                        print('error! Failed open "%s" for writing: %s' % (sOutFile, oXcpt,), file = sys.stderr);
                        return 1;
                    fRc = fnGenMethod(oOut, iPartNo) and fRc;
                    oOut.close();
            if fRc:
                return 0;

        return 1;


if __name__ == '__main__':
    sys.exit(IEMThreadedGenerator().main(sys.argv));

