#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: IEMAllInstPython.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
IEM instruction extractor.

This script/module parses the IEMAllInstruction*.cpp.h files next to it and
collects information about the instructions.  It can then be used to generate
disassembler tables and tests.
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

# pylint: disable=anomalous-backslash-in-string,too-many-lines

# Standard python imports.
import os;
import re;
import sys;
import traceback;

## Only the main script needs to modify the path.
#g_ksValidationKitDir = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
#                                    'ValidationKit');
#sys.path.append(g_ksValidationKitDir);
#
#from common import utils; - Windows build boxes doesn't have pywin32.

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


g_kdX86EFlagsConstants = {
    'X86_EFL_CF':          0x00000001, # RT_BIT_32(0)
    'X86_EFL_1':           0x00000002, # RT_BIT_32(1)
    'X86_EFL_PF':          0x00000004, # RT_BIT_32(2)
    'X86_EFL_AF':          0x00000010, # RT_BIT_32(4)
    'X86_EFL_ZF':          0x00000040, # RT_BIT_32(6)
    'X86_EFL_SF':          0x00000080, # RT_BIT_32(7)
    'X86_EFL_TF':          0x00000100, # RT_BIT_32(8)
    'X86_EFL_IF':          0x00000200, # RT_BIT_32(9)
    'X86_EFL_DF':          0x00000400, # RT_BIT_32(10)
    'X86_EFL_OF':          0x00000800, # RT_BIT_32(11)
    'X86_EFL_IOPL':        0x00003000, # (RT_BIT_32(12) | RT_BIT_32(13))
    'X86_EFL_NT':          0x00004000, # RT_BIT_32(14)
    'X86_EFL_RF':          0x00010000, # RT_BIT_32(16)
    'X86_EFL_VM':          0x00020000, # RT_BIT_32(17)
    'X86_EFL_AC':          0x00040000, # RT_BIT_32(18)
    'X86_EFL_VIF':         0x00080000, # RT_BIT_32(19)
    'X86_EFL_VIP':         0x00100000, # RT_BIT_32(20)
    'X86_EFL_ID':          0x00200000, # RT_BIT_32(21)
    'X86_EFL_LIVE_MASK':   0x003f7fd5, # UINT32_C(0x003f7fd5)
    'X86_EFL_RA1_MASK':    0x00000002, # RT_BIT_32(1)
};

## EFlags values allowed in \@opfltest, \@opflmodify, \@opflundef, \@opflset, and \@opflclear.
g_kdEFlagsMnemonics = {
    # Debugger flag notation (sorted by value):
    'cf':   'X86_EFL_CF',   ##< Carry Flag.
    'nc':  '!X86_EFL_CF',   ##< No Carry.

    'po':   'X86_EFL_PF',   ##< Parity Pdd.
    'pe':  '!X86_EFL_PF',   ##< Parity Even.

    'af':   'X86_EFL_AF',   ##< Aux Flag.
    'na':  '!X86_EFL_AF',   ##< No Aux.

    'zr':   'X86_EFL_ZF',   ##< ZeRo.
    'nz':  '!X86_EFL_ZF',   ##< No Zero.

    'ng':   'X86_EFL_SF',   ##< NeGative (sign).
    'pl':  '!X86_EFL_SF',   ##< PLuss (sign).

    'tf':   'X86_EFL_TF',   ##< Trap flag.

    'ei':   'X86_EFL_IF',   ##< Enabled Interrupts.
    'di':  '!X86_EFL_IF',   ##< Disabled Interrupts.

    'dn':   'X86_EFL_DF',   ##< DowN (string op direction).
    'up':  '!X86_EFL_DF',   ##< UP (string op direction).

    'ov':   'X86_EFL_OF',   ##< OVerflow.
    'nv':  '!X86_EFL_OF',   ##< No Overflow.

    'nt':   'X86_EFL_NT',   ##< Nested Task.
    'rf':   'X86_EFL_RF',   ##< Resume Flag.
    'vm':   'X86_EFL_VM',   ##< Virtual-8086 Mode.
    'ac':   'X86_EFL_AC',   ##< Alignment Check.
    'vif':  'X86_EFL_VIF',  ##< Virtual Interrupt Flag.
    'vip':  'X86_EFL_VIP',  ##< Virtual Interrupt Pending.

    # Reference manual notation not covered above (sorted by value):
    'pf':   'X86_EFL_PF',
    'zf':   'X86_EFL_ZF',
    'sf':   'X86_EFL_SF',
    'if':   'X86_EFL_IF',
    'df':   'X86_EFL_DF',
    'of':   'X86_EFL_OF',
    'iopl': 'X86_EFL_IOPL',
    'id':   'X86_EFL_ID',
};

## Constants and values for CR0.
g_kdX86Cr0Constants = {
    'X86_CR0_PE':           0x00000001, # RT_BIT_32(0)
    'X86_CR0_MP':           0x00000002, # RT_BIT_32(1)
    'X86_CR0_EM':           0x00000004, # RT_BIT_32(2)
    'X86_CR0_TS':           0x00000008, # RT_BIT_32(3)
    'X86_CR0_ET':           0x00000010, # RT_BIT_32(4)
    'X86_CR0_NE':           0x00000020, # RT_BIT_32(5)
    'X86_CR0_WP':           0x00010000, # RT_BIT_32(16)
    'X86_CR0_AM':           0x00040000, # RT_BIT_32(18)
    'X86_CR0_NW':           0x20000000, # RT_BIT_32(29)
    'X86_CR0_CD':           0x40000000, # RT_BIT_32(30)
    'X86_CR0_PG':           0x80000000, # RT_BIT_32(31)
};

## Constants and values for CR4.
g_kdX86Cr4Constants = {
    'X86_CR4_VME':          0x00000001, # RT_BIT_32(0)
    'X86_CR4_PVI':          0x00000002, # RT_BIT_32(1)
    'X86_CR4_TSD':          0x00000004, # RT_BIT_32(2)
    'X86_CR4_DE':           0x00000008, # RT_BIT_32(3)
    'X86_CR4_PSE':          0x00000010, # RT_BIT_32(4)
    'X86_CR4_PAE':          0x00000020, # RT_BIT_32(5)
    'X86_CR4_MCE':          0x00000040, # RT_BIT_32(6)
    'X86_CR4_PGE':          0x00000080, # RT_BIT_32(7)
    'X86_CR4_PCE':          0x00000100, # RT_BIT_32(8)
    'X86_CR4_OSFXSR':       0x00000200, # RT_BIT_32(9)
    'X86_CR4_OSXMMEEXCPT':  0x00000400, # RT_BIT_32(10)
    'X86_CR4_VMXE':         0x00002000, # RT_BIT_32(13)
    'X86_CR4_SMXE':         0x00004000, # RT_BIT_32(14)
    'X86_CR4_PCIDE':        0x00020000, # RT_BIT_32(17)
    'X86_CR4_OSXSAVE':      0x00040000, # RT_BIT_32(18)
    'X86_CR4_SMEP':         0x00100000, # RT_BIT_32(20)
    'X86_CR4_SMAP':         0x00200000, # RT_BIT_32(21)
    'X86_CR4_PKE':          0x00400000, # RT_BIT_32(22)
};

## XSAVE components (XCR0).
g_kdX86XSaveCConstants = {
    'XSAVE_C_X87':          0x00000001,
    'XSAVE_C_SSE':          0x00000002,
    'XSAVE_C_YMM':          0x00000004,
    'XSAVE_C_BNDREGS':      0x00000008,
    'XSAVE_C_BNDCSR':       0x00000010,
    'XSAVE_C_OPMASK':       0x00000020,
    'XSAVE_C_ZMM_HI256':    0x00000040,
    'XSAVE_C_ZMM_16HI':     0x00000080,
    'XSAVE_C_PKRU':         0x00000200,
    'XSAVE_C_LWP':          0x4000000000000000,
    'XSAVE_C_X':            0x8000000000000000,
    'XSAVE_C_ALL_AVX':      0x000000c4, # For clearing all AVX bits.
    'XSAVE_C_ALL_AVX_SSE':  0x000000c6, # For clearing all AVX and SSE bits.
};


## \@op[1-4] locations
g_kdOpLocations = {
    'reg':      [], ## modrm.reg
    'rm':       [], ## modrm.rm
    'imm':      [], ## immediate instruction data
    'vvvv':     [], ## VEX.vvvv
    'imm8reg':  [], ## byte immediate with register specified in bits 4 thru 7 (vpblendvb, vblendvps, vblendvpd).

    # fixed registers.
    'AL':       [],
    'rAX':      [],
    'rDX':      [],
    'CL':       [],
    'rSI':      [],
    'rDI':      [],
    'rFLAGS':   [],
    'CS':       [],
    'DS':       [],
    'ES':       [],
    'FS':       [],
    'GS':       [],
    'SS':       [],
    'XMM0':     [],

    # fixed valures.
    '1':        [],
};

## \@op[1-4] types
##
## Value fields:
##    - 0: the normal IDX_ParseXXX handler (IDX_UseModRM == IDX_ParseModRM).
##    - 1: the location (g_kdOpLocations).
##    - 2: disassembler format string version of the type.
##    - 3: disassembler OP_PARAM_XXX (XXX only).
##    - 4: IEM form matching instruction.
##
## Note! See the A.2.1 in SDM vol 2 for the type names.
g_kdOpTypes = {
    # Fixed addresses
    'Ap':           ( 'IDX_ParseImmAddrF',  'imm',    '%Ap',  'Ap',      'FIXED', ),

    # ModR/M.rm
    'Eb':           ( 'IDX_UseModRM',       'rm',     '%Eb',  'Eb',      'RM',    ),
    'Ed':           ( 'IDX_UseModRM',       'rm',     '%Ed',  'Ed',      'RM',    ),
    'Ed_WO':        ( 'IDX_UseModRM',       'rm',     '%Ed',  'Ed',      'RM',    ),
    'Eq':           ( 'IDX_UseModRM',       'rm',     '%Eq',  'Eq',      'RM',    ),
    'Eq_WO':        ( 'IDX_UseModRM',       'rm',     '%Eq',  'Eq',      'RM',    ),
    'Ew':           ( 'IDX_UseModRM',       'rm',     '%Ew',  'Ew',      'RM',    ),
    'Ew_WO':        ( 'IDX_UseModRM',       'rm',     '%Ew',  'Ew',      'RM',    ),
    'Ev':           ( 'IDX_UseModRM',       'rm',     '%Ev',  'Ev',      'RM',    ),
    'Ey':           ( 'IDX_UseModRM',       'rm',     '%Ey',  'Ey',      'RM',    ),
    'Ey_WO':        ( 'IDX_UseModRM',       'rm',     '%Ey',  'Ey',      'RM',    ),
    'Qd':           ( 'IDX_UseModRM',       'rm',     '%Qd',  'Qd',      'RM',    ),
    'Qq':           ( 'IDX_UseModRM',       'rm',     '%Qq',  'Qq',      'RM',    ),
    'Qq_WO':        ( 'IDX_UseModRM',       'rm',     '%Qq',  'Qq',      'RM',    ),
    'Wss':          ( 'IDX_UseModRM',       'rm',     '%Wss', 'Wss',     'RM',    ),
    'Wss_WO':       ( 'IDX_UseModRM',       'rm',     '%Wss', 'Wss',     'RM',    ),
    'Wsd':          ( 'IDX_UseModRM',       'rm',     '%Wsd', 'Wsd',     'RM',    ),
    'Wsd_WO':       ( 'IDX_UseModRM',       'rm',     '%Wsd', 'Wsd',     'RM',    ),
    'Wps':          ( 'IDX_UseModRM',       'rm',     '%Wps', 'Wps',     'RM',    ),
    'Wps_WO':       ( 'IDX_UseModRM',       'rm',     '%Wps', 'Wps',     'RM',    ),
    'Wpd':          ( 'IDX_UseModRM',       'rm',     '%Wpd', 'Wpd',     'RM',    ),
    'Wpd_WO':       ( 'IDX_UseModRM',       'rm',     '%Wpd', 'Wpd',     'RM',    ),
    'Wdq':          ( 'IDX_UseModRM',       'rm',     '%Wdq', 'Wdq',     'RM',    ),
    'Wdq_WO':       ( 'IDX_UseModRM',       'rm',     '%Wdq', 'Wdq',     'RM',    ),
    'Wq':           ( 'IDX_UseModRM',       'rm',     '%Wq',  'Wq',      'RM',    ),
    'Wq_WO':        ( 'IDX_UseModRM',       'rm',     '%Wq',  'Wq',      'RM',    ),
    'WqZxReg_WO':   ( 'IDX_UseModRM',       'rm',     '%Wq',  'Wq',      'RM',    ),
    'Wqq':          ( 'IDX_UseModRM',       'rm',     '%Wqq', 'Wqq',     'RM',    ),
    'Wqq_WO':       ( 'IDX_UseModRM',       'rm',     '%Wqq', 'Wqq',     'RM',    ),
    'Wx':           ( 'IDX_UseModRM',       'rm',     '%Wx',  'Wx',      'RM',    ),
    'Wx_WO':        ( 'IDX_UseModRM',       'rm',     '%Wx',  'Wx',      'RM',    ),

    # ModR/M.rm - register only.
    'Uq':           ( 'IDX_UseModRM',       'rm',     '%Uq',  'Uq',      'REG'    ),
    'UqHi':         ( 'IDX_UseModRM',       'rm',     '%Uq',  'UqHi',    'REG'    ),
    'Uqq':          ( 'IDX_UseModRM',       'rm',     '%Uqq', 'Uqq',     'REG'    ),
    'Uss':          ( 'IDX_UseModRM',       'rm',     '%Uss', 'Uss',     'REG'    ),
    'Uss_WO':       ( 'IDX_UseModRM',       'rm',     '%Uss', 'Uss',     'REG'    ),
    'Usd':          ( 'IDX_UseModRM',       'rm',     '%Usd', 'Usd',     'REG'    ),
    'Usd_WO':       ( 'IDX_UseModRM',       'rm',     '%Usd', 'Usd',     'REG'    ),
    'Ux':           ( 'IDX_UseModRM',       'rm',     '%Ux',  'Ux',      'REG'    ),
    'Nq':           ( 'IDX_UseModRM',       'rm',     '%Qq',  'Nq',      'REG'    ),

    # ModR/M.rm - memory only.
    'Ma':           ( 'IDX_UseModRM',       'rm',     '%Ma',  'Ma',      'MEM',   ), ##< Only used by BOUND.
    'Mb_RO':        ( 'IDX_UseModRM',       'rm',     '%Mb',  'Mb',      'MEM',   ),
    'Md':           ( 'IDX_UseModRM',       'rm',     '%Md',  'Md',      'MEM',   ),
    'Md_RO':        ( 'IDX_UseModRM',       'rm',     '%Md',  'Md',      'MEM',   ),
    'Md_WO':        ( 'IDX_UseModRM',       'rm',     '%Md',  'Md',      'MEM',   ),
    'Mdq':          ( 'IDX_UseModRM',       'rm',     '%Mdq', 'Mdq',     'MEM',   ),
    'Mdq_WO':       ( 'IDX_UseModRM',       'rm',     '%Mdq', 'Mdq',     'MEM',   ),
    'Mq':           ( 'IDX_UseModRM',       'rm',     '%Mq',  'Mq',      'MEM',   ),
    'Mq_WO':        ( 'IDX_UseModRM',       'rm',     '%Mq',  'Mq',      'MEM',   ),
    'Mps_WO':       ( 'IDX_UseModRM',       'rm',     '%Mps', 'Mps',     'MEM',   ),
    'Mpd_WO':       ( 'IDX_UseModRM',       'rm',     '%Mpd', 'Mpd',     'MEM',   ),
    'Mx':           ( 'IDX_UseModRM',       'rm',     '%Mx',  'Mx',      'MEM',   ),
    'Mx_WO':        ( 'IDX_UseModRM',       'rm',     '%Mx',  'Mx',      'MEM',   ),
    'MVx':          ( 'IDX_UseModRM',       'rm',     '%MVx', 'MVx',     'MEM',   ), ##< VSIB only. 'V' is '*' in AMD manuals.
    'MVx_RO':       ( 'IDX_UseModRM',       'rm',     '%MVx', 'MVx',     'MEM',   ), ##< VSIB only.
    'MVx_WO':       ( 'IDX_UseModRM',       'rm',     '%MVx', 'MVx',     'MEM',   ), ##< VSIB only.
    'M_RO':         ( 'IDX_UseModRM',       'rm',     '%M',   'M',       'MEM',   ),
    'M_RW':         ( 'IDX_UseModRM',       'rm',     '%M',   'M',       'MEM',   ),

    # ModR/M.reg
    'Gb':           ( 'IDX_UseModRM',       'reg',    '%Gb',  'Gb',      '',      ),
    'Gw':           ( 'IDX_UseModRM',       'reg',    '%Gw',  'Gw',      '',      ),
    'Gd':           ( 'IDX_UseModRM',       'reg',    '%Gd',  'Gd',      '',      ),
    'Gv':           ( 'IDX_UseModRM',       'reg',    '%Gv',  'Gv',      '',      ),
    'Gv_RO':        ( 'IDX_UseModRM',       'reg',    '%Gv',  'Gv',      '',      ),
    'Gy':           ( 'IDX_UseModRM',       'reg',    '%Gy',  'Gy',      '',      ),
    'Pd':           ( 'IDX_UseModRM',       'reg',    '%Pd',  'Pd',      '',      ),
    'PdZx_WO':      ( 'IDX_UseModRM',       'reg',    '%Pd',  'PdZx',    '',      ),
    'Pq':           ( 'IDX_UseModRM',       'reg',    '%Pq',  'Pq',      '',      ),
    'Pq_WO':        ( 'IDX_UseModRM',       'reg',    '%Pq',  'Pq',      '',      ),
    'Vd':           ( 'IDX_UseModRM',       'reg',    '%Vd',  'Vd',      '',      ),
    'Vd_WO':        ( 'IDX_UseModRM',       'reg',    '%Vd',  'Vd',      '',      ),
    'VdZx_WO':      ( 'IDX_UseModRM',       'reg',    '%Vd',  'Vd',      '',      ),
    'Vdq':          ( 'IDX_UseModRM',       'reg',    '%Vdq', 'Vdq',     '',      ),
    'Vss':          ( 'IDX_UseModRM',       'reg',    '%Vss', 'Vss',     '',      ),
    'Vss_WO':       ( 'IDX_UseModRM',       'reg',    '%Vss', 'Vss',     '',      ),
    'VssZx_WO':     ( 'IDX_UseModRM',       'reg',    '%Vss', 'Vss',     '',      ),
    'Vsd':          ( 'IDX_UseModRM',       'reg',    '%Vsd', 'Vsd',     '',      ),
    'Vsd_WO':       ( 'IDX_UseModRM',       'reg',    '%Vsd', 'Vsd',     '',      ),
    'VsdZx_WO':     ( 'IDX_UseModRM',       'reg',    '%Vsd', 'Vsd',     '',      ),
    'Vps':          ( 'IDX_UseModRM',       'reg',    '%Vps', 'Vps',     '',      ),
    'Vps_WO':       ( 'IDX_UseModRM',       'reg',    '%Vps', 'Vps',     '',      ),
    'Vpd':          ( 'IDX_UseModRM',       'reg',    '%Vpd', 'Vpd',     '',      ),
    'Vpd_WO':       ( 'IDX_UseModRM',       'reg',    '%Vpd', 'Vpd',     '',      ),
    'Vq':           ( 'IDX_UseModRM',       'reg',    '%Vq',  'Vq',      '',      ),
    'Vq_WO':        ( 'IDX_UseModRM',       'reg',    '%Vq',  'Vq',      '',      ),
    'Vdq_WO':       ( 'IDX_UseModRM',       'reg',    '%Vdq', 'Vdq',     '',      ),
    'VqHi':         ( 'IDX_UseModRM',       'reg',    '%Vdq', 'VdqHi',   '',      ),
    'VqHi_WO':      ( 'IDX_UseModRM',       'reg',    '%Vdq', 'VdqHi',   '',      ),
    'VqZx_WO':      ( 'IDX_UseModRM',       'reg',    '%Vq',  'VqZx',    '',      ),
    'Vqq':          ( 'IDX_UseModRM',       'reg',    '%Vqq', 'Vqq',     '',      ),
    'Vqq_WO':       ( 'IDX_UseModRM',       'reg',    '%Vqq', 'Vqq',     '',      ),
    'Vx':           ( 'IDX_UseModRM',       'reg',    '%Vx',  'Vx',      '',      ),
    'Vx_WO':        ( 'IDX_UseModRM',       'reg',    '%Vx',  'Vx',      '',      ),

    # VEX.vvvv
    'By':           ( 'IDX_UseModRM',       'vvvv',   '%By',  'By',      'V',     ),
    'Hdq':          ( 'IDX_UseModRM',       'vvvv',   '%Hdq', 'Hdq',     'V',     ),
    'Hps':          ( 'IDX_UseModRM',       'vvvv',   '%Hps', 'Hps',     'V',     ),
    'Hpd':          ( 'IDX_UseModRM',       'vvvv',   '%Hpd', 'Hpd',     'V',     ),
    'HssHi':        ( 'IDX_UseModRM',       'vvvv',   '%Hx',  'HssHi',   'V',     ),
    'HsdHi':        ( 'IDX_UseModRM',       'vvvv',   '%Hx',  'HsdHi',   'V',     ),
    'Hq':           ( 'IDX_UseModRM',       'vvvv',   '%Hq',  'Hq',      'V',     ),
    'HqHi':         ( 'IDX_UseModRM',       'vvvv',   '%Hq',  'HqHi',    'V',     ),
    'Hqq':          ( 'IDX_UseModRM',       'vvvv',   '%Hqq', 'Hqq',     'V',     ),
    'Hx':           ( 'IDX_UseModRM',       'vvvv',   '%Hx',  'Hx',      'V',     ),

    # Immediate values.
    'Ib':           ( 'IDX_ParseImmByte',   'imm',    '%Ib',  'Ib',   '', ), ##< NB! Could be IDX_ParseImmByteSX for some instrs.
    'Iw':           ( 'IDX_ParseImmUshort', 'imm',    '%Iw',  'Iw',      '',      ),
    'Id':           ( 'IDX_ParseImmUlong',  'imm',    '%Id',  'Id',      '',      ),
    'Iq':           ( 'IDX_ParseImmQword',  'imm',    '%Iq',  'Iq',      '',      ),
    'Iv':           ( 'IDX_ParseImmV',      'imm',    '%Iv',  'Iv',      '',      ), ##< o16: word, o32: dword, o64: qword
    'Iz':           ( 'IDX_ParseImmZ',      'imm',    '%Iz',  'Iz',      '',      ), ##< o16: word, o32|o64:dword

    # Registers encoded as immediates.
    'Lx':           ( 'IDX_ParseImmByte',    'imm8reg', '%Lx', 'Lx',     '',      ),

    # Address operands (no ModR/M).
    'Ob':           ( 'IDX_ParseImmAddr',   'imm',    '%Ob',  'Ob',      '',      ),
    'Ov':           ( 'IDX_ParseImmAddr',   'imm',    '%Ov',  'Ov',      '',      ),

    # Relative jump targets
    'Jb':           ( 'IDX_ParseImmBRel',   'imm',    '%Jb',  'Jb',      '',      ),
    'Jv':           ( 'IDX_ParseImmVRel',   'imm',    '%Jv',  'Jv',      '',      ),

    # DS:rSI
    'Xb':           ( 'IDX_ParseXb',        'rSI',    '%eSI', 'Xb',      '',      ),
    'Xv':           ( 'IDX_ParseXv',        'rSI',    '%eSI', 'Xv',      '',      ),
    # ES:rDI
    'Yb':           ( 'IDX_ParseYb',        'rDI',    '%eDI', 'Yb',      '',      ),
    'Yv':           ( 'IDX_ParseYv',        'rDI',    '%eDI', 'Yv',      '',      ),

    'Fv':           ( 'IDX_ParseFixedReg',  'rFLAGS', '%Fv',  'Fv',      '',      ),

    # Fixed registers.
    'AL':           ( 'IDX_ParseFixedReg',  'AL',     'al',   'REG_AL',  '',      ),
    'REG_CL':       ( 'IDX_ParseFixedReg',  'CL',     'cl',   'REG_CL',  '',      ),
    'REG_XMM0':     ( 'IDX_ParseFixedReg',  'XMM0',   'xmm0', 'REG_XMM0','',      ),
    'rAX':          ( 'IDX_ParseFixedReg',  'rAX',    '%eAX', 'REG_EAX', '',      ),
    'rDX':          ( 'IDX_ParseFixedReg',  'rDX',    '%eDX', 'REG_EDX', '',      ),
    'CS':           ( 'IDX_ParseFixedReg',  'CS',     'cs',   'REG_CS',  '',      ), # 8086: push CS
    'DS':           ( 'IDX_ParseFixedReg',  'DS',     'ds',   'REG_DS',  '',      ),
    'ES':           ( 'IDX_ParseFixedReg',  'ES',     'es',   'REG_ES',  '',      ),
    'FS':           ( 'IDX_ParseFixedReg',  'FS',     'fs',   'REG_FS',  '',      ),
    'GS':           ( 'IDX_ParseFixedReg',  'GS',     'gs',   'REG_GS',  '',      ),
    'SS':           ( 'IDX_ParseFixedReg',  'SS',     'ss',   'REG_SS',  '',      ),

    # Fixed values.
    '1':            ( '',                   '1',      '1',    '1',       '',      ),
};

# IDX_ParseFixedReg
# IDX_ParseVexDest


## IEMFORM_XXX mappings.
g_kdIemForms = {     # sEncoding,   [ sWhere1, ... ]                    opcodesub      ),
    'RM':           ( 'ModR/M',     [ 'reg', 'rm' ],                    '',            ),
    'RM_REG':       ( 'ModR/M',     [ 'reg', 'rm' ],                    '11 mr/reg',   ),
    'RM_MEM':       ( 'ModR/M',     [ 'reg', 'rm' ],                    '!11 mr/reg',  ),
    'RMI':          ( 'ModR/M',     [ 'reg', 'rm', 'imm' ],             '',            ),
    'RMI_REG':      ( 'ModR/M',     [ 'reg', 'rm', 'imm' ],             '11 mr/reg',   ),
    'RMI_MEM':      ( 'ModR/M',     [ 'reg', 'rm', 'imm' ],             '!11 mr/reg',  ),
    'RM0':          ( 'ModR/M',     [ 'reg', 'rm', 'XMM0' ],            '',            ),
    'RM0_REG':      ( 'ModR/M',     [ 'reg', 'rm', 'XMM0' ],            '11 mr/reg',   ),
    'RM0_MEM':      ( 'ModR/M',     [ 'reg', 'rm', 'XMM0' ],            '!11 mr/reg',  ),
    'MR':           ( 'ModR/M',     [ 'rm', 'reg' ],                    '',            ),
    'MR_REG':       ( 'ModR/M',     [ 'rm', 'reg' ],                    '11 mr/reg',   ),
    'MR_MEM':       ( 'ModR/M',     [ 'rm', 'reg' ],                    '!11 mr/reg',  ),
    'MRI':          ( 'ModR/M',     [ 'rm', 'reg', 'imm' ],             '',            ),
    'MRI_REG':      ( 'ModR/M',     [ 'rm', 'reg', 'imm' ],             '11 mr/reg',   ),
    'MRI_MEM':      ( 'ModR/M',     [ 'rm', 'reg', 'imm' ],             '!11 mr/reg',  ),
    'M':            ( 'ModR/M',     [ 'rm', ],                          '',            ),
    'M_REG':        ( 'ModR/M',     [ 'rm', ],                          '',            ),
    'M_MEM':        ( 'ModR/M',     [ 'rm', ],                          '',            ),
    'M1':           ( 'ModR/M',     [ 'rm', '1' ],                      '',            ),
    'M_CL':         ( 'ModR/M',     [ 'rm', 'CL' ],                     '',            ), # shl/rcl/ror/++
    'MI':           ( 'ModR/M',     [ 'rm', 'imm' ],                    '',            ),
    'MI_REG':       ( 'ModR/M',     [ 'rm', 'imm' ],                    '11 mr/reg',   ),
    'MI_MEM':       ( 'ModR/M',     [ 'rm', 'imm' ],                    '!11 mr/reg',  ),
    'R':            ( 'ModR/M',     [ 'reg', ],                         '',            ),

    'VEX_RM':       ( 'VEX.ModR/M', [ 'reg', 'rm' ],                    '',            ),
    'VEX_RM_REG':   ( 'VEX.ModR/M', [ 'reg', 'rm' ],                    '11 mr/reg',   ),
    'VEX_RM_MEM':   ( 'VEX.ModR/M', [ 'reg', 'rm' ],                    '!11 mr/reg',  ),
    'VEX_MR':       ( 'VEX.ModR/M', [ 'rm', 'reg' ],                    '',            ),
    'VEX_MR_REG':   ( 'VEX.ModR/M', [ 'rm', 'reg' ],                    '11 mr/reg',   ),
    'VEX_MR_MEM':   ( 'VEX.ModR/M', [ 'rm', 'reg' ],                    '!11 mr/reg',  ),
    'VEX_MRI':      ( 'VEX.ModR/M', [ 'rm', 'reg', 'imm' ],             '',            ),
    'VEX_MRI_REG':  ( 'VEX.ModR/M', [ 'rm', 'reg', 'imm' ],             '11 mr/reg',   ),
    'VEX_MRI_MEM':  ( 'VEX.ModR/M', [ 'rm', 'reg', 'imm' ],             '!11 mr/reg',  ),
    'VEX_M':        ( 'VEX.ModR/M', [ 'rm', ],                          '' ),
    'VEX_M_REG':    ( 'VEX.ModR/M', [ 'rm', ],                          '' ),
    'VEX_M_MEM':    ( 'VEX.ModR/M', [ 'rm', ],                          '' ),
    'VEX_R':        ( 'VEX.ModR/M', [ 'reg', ],                         '' ),
    'VEX_RVM':      ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm' ],            '',            ),
    'VEX_RVM_REG':  ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm' ],            '11 mr/reg',   ),
    'VEX_RVM_MEM':  ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm' ],            '!11 mr/reg',  ),
    'VEX_RVMI':     ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm' ],     '',            ),
    'VEX_RVMI_REG': ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm' ],     '11 mr/reg',   ),
    'VEX_RVMI_MEM': ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm' ],     '!11 mr/reg',  ),
    'VEX_RVMR':     ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm8reg' ], '',            ),
    'VEX_RVMR_REG': ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm8reg' ], '11 mr/reg',   ),
    'VEX_RVMR_MEM': ( 'VEX.ModR/M', [ 'reg', 'vvvv', 'rm', 'imm8reg' ], '!11 mr/reg',  ),
    'VEX_RMV':      ( 'VEX.ModR/M', [ 'reg', 'rm', 'vvvv' ],            '',            ),
    'VEX_RMV_REG':  ( 'VEX.ModR/M', [ 'reg', 'rm', 'vvvv' ],            '11 mr/reg',   ),
    'VEX_RMV_MEM':  ( 'VEX.ModR/M', [ 'reg', 'rm', 'vvvv' ],            '!11 mr/reg',  ),
    'VEX_RMI':      ( 'VEX.ModR/M', [ 'reg', 'rm', 'imm' ],             '',            ),
    'VEX_RMI_REG':  ( 'VEX.ModR/M', [ 'reg', 'rm', 'imm' ],             '11 mr/reg',   ),
    'VEX_RMI_MEM':  ( 'VEX.ModR/M', [ 'reg', 'rm', 'imm' ],             '!11 mr/reg',  ),
    'VEX_MVR':      ( 'VEX.ModR/M', [ 'rm', 'vvvv', 'reg' ],            '',            ),
    'VEX_MVR_REG':  ( 'VEX.ModR/M', [ 'rm', 'vvvv', 'reg' ],            '11 mr/reg',   ),
    'VEX_MVR_MEM':  ( 'VEX.ModR/M', [ 'rm', 'vvvv', 'reg' ],            '!11 mr/reg',  ),

    'VEX_VM':       ( 'VEX.ModR/M', [ 'vvvv', 'rm' ],                   '',            ),
    'VEX_VM_REG':   ( 'VEX.ModR/M', [ 'vvvv', 'rm' ],                   '11 mr/reg',   ),
    'VEX_VM_MEM':   ( 'VEX.ModR/M', [ 'vvvv', 'rm' ],                   '!11 mr/reg',  ),
    'VEX_VMI':      ( 'VEX.ModR/M', [ 'vvvv', 'rm', 'imm' ],            '',            ),
    'VEX_VMI_REG':  ( 'VEX.ModR/M', [ 'vvvv', 'rm', 'imm' ],            '11 mr/reg',   ),
    'VEX_VMI_MEM':  ( 'VEX.ModR/M', [ 'vvvv', 'rm', 'imm' ],            '!11 mr/reg',  ),

    'FIXED':        ( 'fixed',      None,                               '',            ),
};

## \@oppfx values.
g_kdPrefixes = {
    'none':  [],
    '0x66':  [],
    '0xf3':  [],
    '0xf2':  [],
    '!0xf3': [], # special case for bsf/tzcnt
};

## Special \@opcode tag values.
g_kdSpecialOpcodes = {
    '/reg':         [],
    'mr/reg':       [],
    '11 /reg':      [],
    '!11 /reg':     [],
    '11 mr/reg':    [],
    '!11 mr/reg':   [],
};

## Special \@opcodesub tag values.
## The first value is the real value for aliases.
## The second value is for bs3cg1.
g_kdSubOpcodes = {
    'none':                 [ None,                 '',         ],
    '11 mr/reg':            [ '11 mr/reg',          '',         ],
    '11':                   [ '11 mr/reg',          '',         ], ##< alias
    '!11 mr/reg':           [ '!11 mr/reg',         '',         ],
    '!11':                  [ '!11 mr/reg',         '',         ], ##< alias
    'rex.w=0':              [ 'rex.w=0',            'WZ',       ],
    'w=0':                  [ 'rex.w=0',            '',         ], ##< alias
    'rex.w=1':              [ 'rex.w=1',            'WNZ',      ],
    'w=1':                  [ 'rex.w=1',            '',         ], ##< alias
    'vex.l=0':              [ 'vex.l=0',            'L0',       ],
    'vex.l=1':              [ 'vex.l=0',            'L1',       ],
    '11 mr/reg vex.l=0':    [ '11 mr/reg vex.l=0',  'L0',       ],
    '11 mr/reg vex.l=1':    [ '11 mr/reg vex.l=1',  'L1',       ],
    '!11 mr/reg vex.l=0':   [ '!11 mr/reg vex.l=0', 'L0',       ],
    '!11 mr/reg vex.l=1':   [ '!11 mr/reg vex.l=1', 'L1',       ],
    '!11 mr/reg rex.w=0':   [ '!11 mr/reg rex.w=0', '',         ],
    '!11 mr/reg rex.w=1':   [ '!11 mr/reg rex.w=1', '',         ],
};

## Valid values for \@openc
g_kdEncodings = {
    'ModR/M':       [ 'BS3CG1ENC_MODRM', ],     ##< ModR/M
    'VEX.ModR/M':   [ 'BS3CG1ENC_VEX_MODRM', ], ##< VEX...ModR/M
    'fixed':        [ 'BS3CG1ENC_FIXED', ],     ##< Fixed encoding (address, registers, unused, etc).
    'VEX.fixed':    [ 'BS3CG1ENC_VEX_FIXED', ], ##< VEX + fixed encoding (address, registers, unused, etc).
    'prefix':       [ None, ],                  ##< Prefix
};

## \@opunused, \@opinvalid, \@opinvlstyle
g_kdInvalidStyles = {
    'immediate':                [], ##< CPU stops decoding immediately after the opcode.
    'vex.modrm':                [], ##< VEX+ModR/M, everyone.
    'intel-modrm':              [], ##< Intel decodes ModR/M.
    'intel-modrm-imm8':         [], ##< Intel decodes ModR/M and an 8-byte immediate.
    'intel-opcode-modrm':       [], ##< Intel decodes another opcode byte followed by ModR/M. (Unused extension tables.)
    'intel-opcode-modrm-imm8':  [], ##< Intel decodes another opcode byte followed by ModR/M and an 8-byte immediate.
};

g_kdCpuNames = {
    '8086':     (),
    '80186':    (),
    '80286':    (),
    '80386':    (),
    '80486':    (),
};

## \@opcpuid
g_kdCpuIdFlags = {
    'vme':          'X86_CPUID_FEATURE_EDX_VME',
    'tsc':          'X86_CPUID_FEATURE_EDX_TSC',
    'msr':          'X86_CPUID_FEATURE_EDX_MSR',
    'cx8':          'X86_CPUID_FEATURE_EDX_CX8',
    'sep':          'X86_CPUID_FEATURE_EDX_SEP',
    'cmov':         'X86_CPUID_FEATURE_EDX_CMOV',
    'clfsh':        'X86_CPUID_FEATURE_EDX_CLFSH',
    'clflushopt':   'X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT',
    'mmx':          'X86_CPUID_FEATURE_EDX_MMX',
    'fxsr':         'X86_CPUID_FEATURE_EDX_FXSR',
    'sse':          'X86_CPUID_FEATURE_EDX_SSE',
    'sse2':         'X86_CPUID_FEATURE_EDX_SSE2',
    'sse3':         'X86_CPUID_FEATURE_ECX_SSE3',
    'pclmul':       'X86_CPUID_FEATURE_ECX_DTES64',
    'monitor':      'X86_CPUID_FEATURE_ECX_CPLDS',
    'vmx':          'X86_CPUID_FEATURE_ECX_VMX',
    'smx':          'X86_CPUID_FEATURE_ECX_TM2',
    'ssse3':        'X86_CPUID_FEATURE_ECX_SSSE3',
    'fma':          'X86_CPUID_FEATURE_ECX_FMA',
    'cx16':         'X86_CPUID_FEATURE_ECX_CX16',
    'pcid':         'X86_CPUID_FEATURE_ECX_PCID',
    'sse4.1':       'X86_CPUID_FEATURE_ECX_SSE4_1',
    'sse4.2':       'X86_CPUID_FEATURE_ECX_SSE4_2',
    'movbe':        'X86_CPUID_FEATURE_ECX_MOVBE',
    'popcnt':       'X86_CPUID_FEATURE_ECX_POPCNT',
    'aes':          'X86_CPUID_FEATURE_ECX_AES',
    'xsave':        'X86_CPUID_FEATURE_ECX_XSAVE',
    'avx':          'X86_CPUID_FEATURE_ECX_AVX',
    'avx2':         'X86_CPUID_STEXT_FEATURE_EBX_AVX2',
    'f16c':         'X86_CPUID_FEATURE_ECX_F16C',
    'rdrand':       'X86_CPUID_FEATURE_ECX_RDRAND',

    'axmmx':        'X86_CPUID_AMD_FEATURE_EDX_AXMMX',
    '3dnowext':     'X86_CPUID_AMD_FEATURE_EDX_3DNOW_EX',
    '3dnow':        'X86_CPUID_AMD_FEATURE_EDX_3DNOW',
    'svm':          'X86_CPUID_AMD_FEATURE_ECX_SVM',
    'cr8l':         'X86_CPUID_AMD_FEATURE_ECX_CR8L',
    'abm':          'X86_CPUID_AMD_FEATURE_ECX_ABM',
    'sse4a':        'X86_CPUID_AMD_FEATURE_ECX_SSE4A',
    '3dnowprf':     'X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF',
    'xop':          'X86_CPUID_AMD_FEATURE_ECX_XOP',
    'fma4':         'X86_CPUID_AMD_FEATURE_ECX_FMA4',
};

## \@ophints values.
# pylint: disable=line-too-long
g_kdHints = {
    'invalid':                   'DISOPTYPE_INVALID',                   ##<
    'harmless':                  'DISOPTYPE_HARMLESS',                  ##<
    'controlflow':               'DISOPTYPE_CONTROLFLOW',               ##<
    'potentially_dangerous':     'DISOPTYPE_POTENTIALLY_DANGEROUS',     ##<
    'dangerous':                 'DISOPTYPE_DANGEROUS',                 ##<
    'portio':                    'DISOPTYPE_PORTIO',                    ##<
    'privileged':                'DISOPTYPE_PRIVILEGED',                ##<
    'privileged_notrap':         'DISOPTYPE_PRIVILEGED_NOTRAP',         ##<
    'uncond_controlflow':        'DISOPTYPE_UNCOND_CONTROLFLOW',        ##<
    'relative_controlflow':      'DISOPTYPE_RELATIVE_CONTROLFLOW',      ##<
    'cond_controlflow':          'DISOPTYPE_COND_CONTROLFLOW',          ##<
    'interrupt':                 'DISOPTYPE_INTERRUPT',                 ##<
    'illegal':                   'DISOPTYPE_ILLEGAL',                   ##<
    'rrm_dangerous':             'DISOPTYPE_RRM_DANGEROUS',             ##< Some additional dangerous ones when recompiling raw r0.
    'rrm_dangerous_16':          'DISOPTYPE_RRM_DANGEROUS_16',          ##< Some additional dangerous ones when recompiling 16-bit raw r0.
    'inhibit_irqs':              'DISOPTYPE_INHIBIT_IRQS',              ##< Will or can inhibit irqs (sti, pop ss, mov ss) */
    'x86_portio_read':           'DISOPTYPE_X86_PORTIO_READ',           ##<
    'x86_portio_write':          'DISOPTYPE_X86_PORTIO_WRITE',          ##<
    'x86_invalid_64':            'DISOPTYPE_X86_INVALID_64',            ##< Invalid in 64 bits mode
    'x86_only_64':               'DISOPTYPE_X86_ONLY_64',               ##< Only valid in 64 bits mode
    'x86_default_64_op_size':    'DISOPTYPE_X86_DEFAULT_64_OP_SIZE',    ##< Default 64 bits operand size
    'x86_forced_64_op_size':     'DISOPTYPE_X86_FORCED_64_OP_SIZE',     ##< Forced 64 bits operand size; regardless of prefix bytes
    'x86_rexb_extends_opreg':    'DISOPTYPE_X86_REXB_EXTENDS_OPREG',    ##< REX.B extends the register field in the opcode byte
    'x86_mod_fixed_11':          'DISOPTYPE_X86_MOD_FIXED_11',          ##< modrm.mod is always 11b
    'x86_forced_32_op_size_x86': 'DISOPTYPE_X86_FORCED_32_OP_SIZE_X86', ##< Forced 32 bits operand size; regardless of prefix bytes
                                                                        ##  (only in 16 & 32 bits mode!)
    'x86_avx':                   'DISOPTYPE_X86_AVX',                   ##< AVX,AVX2,++ instruction. Not implemented yet!
    'x86_sse':                   'DISOPTYPE_X86_SSE',                   ##< SSE,SSE2,SSE3,++ instruction. Not implemented yet!
    'x86_mmx':                   'DISOPTYPE_X86_MMX',                   ##< MMX,MMXExt,3DNow,++ instruction. Not implemented yet!
    'x86_fpu':                   'DISOPTYPE_X86_FPU',                   ##< FPU instruction. Not implemented yet!
    'ignores_oz_pfx':        '',                                        ##< Ignores operand size prefix 66h.
    'ignores_rexw':          '',                                        ##< Ignores REX.W.
    'ignores_op_sizes':      '',                                        ##< Shorthand for "ignores_oz_pfx | ignores_op_sizes".
    'vex_l_zero':            '',                                        ##< VEX.L must be 0.
    'vex_l_one':             '',                                        ##< VEX.L must be 1.
    'vex_l_ignored':         '',                                        ##< VEX.L is ignored.
    'vex_v_zero':            '',                                        ##< VEX.V must be 0. (generate sub-table?)
    'vex_w_zero':            '',                                        ##< REX.W/VEX.W must be 0.
    'vex_w_one':             '',                                        ##< REX.W/VEX.W must be 1.
    'rex_w_zero':            '',                                        ##< REX.W/VEX.W must be 0.
    'rex_w_one':             '',                                        ##< REX.W/VEX.W must be 1.
    'lock_allowed':          '',                                        ##< Lock prefix allowed.
};
# pylint: enable=line-too-long

## \@opxcpttype values (see SDMv2 2.4, 2.7).
g_kdXcptTypes = {
    'none':     [],
    '1':        [],
    '2':        [],
    '3':        [],
    '4':        [],
    '4UA':      [],
    '5':        [],
    '5LZ':      [], # LZ = VEX.L must be zero.
    '6':        [],
    '7':        [],
    '7LZ':      [],
    '8':        [],
    '11':       [],
    '12':       [],
    'E1':       [],
    'E1NF':     [],
    'E2':       [],
    'E3':       [],
    'E3NF':     [],
    'E4':       [],
    'E4NF':     [],
    'E5':       [],
    'E5NF':     [],
    'E6':       [],
    'E6NF':     [],
    'E7NF':     [],
    'E9':       [],
    'E9NF':     [],
    'E10':      [],
    'E11':      [],
    'E12':      [],
    'E12NF':    [],
};


def _isValidOpcodeByte(sOpcode):
    """
    Checks if sOpcode is a valid lower case opcode byte.
    Returns true/false.
    """
    if len(sOpcode) == 4:
        if sOpcode[:2] == '0x':
            if sOpcode[2] in '0123456789abcdef':
                if sOpcode[3] in '0123456789abcdef':
                    return True;
    return False;


class InstructionMap(object):
    """
    Instruction map.

    The opcode map provides the lead opcode bytes (empty for the one byte
    opcode map).  An instruction can be member of multiple opcode maps as long
    as it uses the same opcode value within the map (because of VEX).
    """

    kdEncodings = {
        'legacy':   [],
        'vex1':     [], ##< VEX or EVEX prefix with vvvvv = 1
        'vex2':     [], ##< VEX or EVEX prefix with vvvvv = 2
        'vex3':     [], ##< VEX or EVEX prefix with vvvvv = 3
        'xop8':     [], ##< XOP prefix with vvvvv = 8
        'xop9':     [], ##< XOP prefix with vvvvv = 9
        'xop10':    [], ##< XOP prefix with vvvvv = 10
    };
    ## Selectors.
    ## 1. The first value is the number of table entries required by a
    ## decoder or disassembler for this type of selector.
    ## 2. The second value is how many entries per opcode byte if applicable.
    kdSelectors = {
        'byte':     [  256, 1, ], ##< next opcode byte selects the instruction (default).
        'byte+pfx': [ 1024, 4, ], ##< next opcode byte selects the instruction together with the 0x66, 0xf2 and 0xf3 prefixes.
        '/r':       [    8, 1, ], ##< modrm.reg selects the instruction.
        'memreg /r':[   16, 1, ], ##< modrm.reg and (modrm.mod == 3) selects the instruction.
        'mod /r':   [   32, 1, ], ##< modrm.reg and modrm.mod selects the instruction.
        '!11 /r':   [    8, 1, ], ##< modrm.reg selects the instruction with modrm.mod != 0y11.
        '11 /r':    [    8, 1, ], ##< modrm.reg select the instruction with modrm.mod == 0y11.
        '11':       [   64, 1, ], ##< modrm.reg and modrm.rm select the instruction with modrm.mod == 0y11.
    };

    ## Define the subentry number according to the Instruction::sPrefix
    ## value for 'byte+pfx' selected tables.
    kiPrefixOrder = {
        'none': 0,
        '0x66': 1,
        '0xf3': 2,
        '0xf2': 3,
    };

    def __init__(self, sName, sIemName = None, asLeadOpcodes = None, sSelector = 'byte+pfx',
                 sEncoding = 'legacy', sDisParse = None):
        assert sSelector in self.kdSelectors;
        assert sEncoding in self.kdEncodings;
        if asLeadOpcodes is None:
            asLeadOpcodes = [];
        else:
            for sOpcode in asLeadOpcodes:
                assert _isValidOpcodeByte(sOpcode);
        assert sDisParse is None or sDisParse.startswith('IDX_Parse');

        self.sName          = sName;
        self.sIemName       = sIemName;
        self.asLeadOpcodes  = asLeadOpcodes;    ##< Lead opcode bytes formatted as hex strings like '0x0f'.
        self.sSelector      = sSelector;        ##< The member selector, see kdSelectors.
        self.sEncoding      = sEncoding;        ##< The encoding, see kdSelectors.
        self.aoInstructions = []                # type: Instruction
        self.sDisParse      = sDisParse;        ##< IDX_ParseXXX.

    def copy(self, sNewName, sPrefixFilter = None):
        """
        Copies the table with filtering instruction by sPrefix if not None.
        """
        oCopy = InstructionMap(sNewName, sIemName = self.sIemName, asLeadOpcodes = self.asLeadOpcodes,
                               sSelector = 'byte' if sPrefixFilter is not None and self.sSelector == 'byte+pfx'
                               else self.sSelector,
                               sEncoding = self.sEncoding, sDisParse = self.sDisParse);
        if sPrefixFilter is None:
            oCopy.aoInstructions = list(self.aoInstructions);
        else:
            oCopy.aoInstructions = [oInstr for oInstr in self.aoInstructions if oInstr.sPrefix == sPrefixFilter];
        return oCopy;

    def getTableSize(self):
        """
        Number of table entries.   This corresponds directly to the selector.
        """
        return self.kdSelectors[self.sSelector][0];

    def getEntriesPerByte(self):
        """
        Number of table entries per opcode bytes.

        This only really makes sense for the 'byte' and 'byte+pfx' selectors, for
        the others it will just return 1.
        """
        return self.kdSelectors[self.sSelector][1];

    def getInstructionIndex(self, oInstr):
        """
        Returns the table index for the instruction.
        """
        bOpcode = oInstr.getOpcodeByte();

        # The byte selectors are simple.  We need a full opcode byte and need just return it.
        if self.sSelector == 'byte':
            assert oInstr.sOpcode[:2] == '0x' and len(oInstr.sOpcode) == 4, str(oInstr);
            return bOpcode;

        # The byte + prefix selector is similarly simple, though requires a prefix as well as the full opcode.
        if self.sSelector  == 'byte+pfx':
            assert oInstr.sOpcode[:2] == '0x' and len(oInstr.sOpcode) == 4, str(oInstr);
            assert self.kiPrefixOrder.get(oInstr.sPrefix, -16384) >= 0;
            return bOpcode * 4 + self.kiPrefixOrder.get(oInstr.sPrefix, -16384);

        # The other selectors needs masking and shifting.
        if self.sSelector == '/r':
            return (bOpcode >> 3) & 0x7;

        if self.sSelector == 'mod /r':
            return (bOpcode >> 3) & 0x1f;

        if self.sSelector == 'memreg /r':
            return ((bOpcode >> 3) & 0x7) | (int((bOpcode >> 6) == 3) << 3);

        if self.sSelector == '!11 /r':
            assert (bOpcode & 0xc0) != 0xc, str(oInstr);
            return (bOpcode >> 3) & 0x7;

        if self.sSelector == '11 /r':
            assert (bOpcode & 0xc0) == 0xc, str(oInstr);
            return (bOpcode >> 3) & 0x7;

        if self.sSelector == '11':
            assert (bOpcode & 0xc0) == 0xc, str(oInstr);
            return bOpcode & 0x3f;

        assert False, self.sSelector;
        return -1;

    def getInstructionsInTableOrder(self):
        """
        Get instructions in table order.

        Returns array of instructions.  Normally there is exactly one
        instruction per entry.  However the entry could also be None if
        not instruction was specified for that opcode value.  Or there
        could be a list of instructions to deal with special encodings
        where for instance prefix (e.g. REX.W) encodes a different
        instruction or different CPUs have different instructions or
        prefixes in the same place.
        """
        # Start with empty table.
        cTable  = self.getTableSize();
        aoTable = [None] * cTable;

        # Insert the instructions.
        for oInstr in self.aoInstructions:
            if oInstr.sOpcode:
                idxOpcode = self.getInstructionIndex(oInstr);
                assert idxOpcode < cTable, str(idxOpcode);

                oExisting = aoTable[idxOpcode];
                if oExisting is None:
                    aoTable[idxOpcode] = oInstr;
                elif not isinstance(oExisting, list):
                    aoTable[idxOpcode] = list([oExisting, oInstr]);
                else:
                    oExisting.append(oInstr);

        return aoTable;


    def getDisasTableName(self):
        """
        Returns the disassembler table name for this map.
        """
        sName = 'g_aDisas';
        for sWord in self.sName.split('_'):
            if sWord == 'm':            # suffix indicating modrm.mod==mem
                sName += '_m';
            elif sWord == 'r':          # suffix indicating modrm.mod==reg
                sName += '_r';
            elif len(sWord) == 2 and re.match('^[a-f0-9][a-f0-9]$', sWord):
                sName += '_' + sWord;
            else:
                sWord  = sWord.replace('grp', 'Grp');
                sWord  = sWord.replace('map', 'Map');
                sName += sWord[0].upper() + sWord[1:];
        return sName;

    def getDisasRangeName(self):
        """
        Returns the disassembler table range name for this map.
        """
        return self.getDisasTableName().replace('g_aDisas', 'g_Disas') + 'Range';

    def isVexMap(self):
        """ Returns True if a VEX map. """
        return self.sEncoding.startswith('vex');


class TestType(object):
    """
    Test value type.

    This base class deals with integer like values.  The fUnsigned constructor
    parameter indicates the default stance on zero vs sign extending.  It is
    possible to override fUnsigned=True by prefixing the value with '+' or '-'.
    """
    def __init__(self, sName, acbSizes = None, fUnsigned = True):
        self.sName = sName;
        self.acbSizes = [1, 2, 4, 8, 16, 32] if acbSizes is None else acbSizes;  # Normal sizes.
        self.fUnsigned = fUnsigned;

    class BadValue(Exception):
        """ Bad value exception. """
        def __init__(self, sMessage):
            Exception.__init__(self, sMessage);
            self.sMessage = sMessage;

    ## For ascii ~ operator.
    kdHexInv = {
        '0': 'f',
        '1': 'e',
        '2': 'd',
        '3': 'c',
        '4': 'b',
        '5': 'a',
        '6': '9',
        '7': '8',
        '8': '7',
        '9': '6',
        'a': '5',
        'b': '4',
        'c': '3',
        'd': '2',
        'e': '1',
        'f': '0',
    };

    def get(self, sValue):
        """
        Get the shortest normal sized byte representation of oValue.

        Returns ((fSignExtend, bytearray), ) or ((fSignExtend, bytearray), (fSignExtend, bytearray), ).
        The latter form is for AND+OR pairs where the first entry is what to
        AND with the field and the second the one or OR with.

        Raises BadValue if invalid value.
        """
        if not sValue:
            raise TestType.BadValue('empty value');

        # Deal with sign and detect hexadecimal or decimal.
        fSignExtend = not self.fUnsigned;
        if sValue[0] == '-' or sValue[0] == '+':
            fSignExtend = True;
            fHex = len(sValue) > 3 and sValue[1:3].lower() == '0x';
        else:
            fHex = len(sValue) > 2 and sValue[0:2].lower() == '0x';

        # try convert it to long integer.
        try:
            iValue = long(sValue, 16 if fHex else 10);
        except Exception as oXcpt:
            raise TestType.BadValue('failed to convert "%s" to integer (%s)' % (sValue, oXcpt));

        # Convert the hex string and pad it to a decent value.  Negative values
        # needs to be manually converted to something non-negative (~-n + 1).
        if iValue >= 0:
            sHex = hex(iValue);
            if sys.version_info[0] < 3:
                assert sHex[-1] == 'L';
                sHex = sHex[:-1];
            assert sHex[:2] == '0x';
            sHex = sHex[2:];
        else:
            sHex = hex(-iValue - 1);
            if sys.version_info[0] < 3:
                assert sHex[-1] == 'L';
                sHex = sHex[:-1];
            assert sHex[:2] == '0x';
            sHex = ''.join([self.kdHexInv[sDigit] for sDigit in sHex[2:]]);
            if fSignExtend and sHex[0] not in [ '8', '9', 'a', 'b', 'c', 'd', 'e', 'f']:
                sHex = 'f' + sHex;

        cDigits = len(sHex);
        if cDigits <= self.acbSizes[-1] * 2:
            for cb in self.acbSizes:
                cNaturalDigits = cb * 2;
                if cDigits <= cNaturalDigits:
                    break;
        else:
            cNaturalDigits = self.acbSizes[-1] * 2;
            cNaturalDigits = int((cDigits + cNaturalDigits - 1) / cNaturalDigits) * cNaturalDigits;
            assert isinstance(cNaturalDigits, int)

        if cNaturalDigits != cDigits:
            cNeeded = cNaturalDigits - cDigits;
            if iValue >= 0:
                sHex = ('0' * cNeeded) + sHex;
            else:
                sHex = ('f' * cNeeded) + sHex;

        # Invert and convert to bytearray and return it.
        abValue = bytearray([int(sHex[offHex - 2 : offHex], 16) for offHex in range(len(sHex), 0, -2)]);

        return ((fSignExtend, abValue),);

    def validate(self, sValue):
        """
        Returns True if value is okay, error message on failure.
        """
        try:
            self.get(sValue);
        except TestType.BadValue as oXcpt:
            return oXcpt.sMessage;
        return True;

    def isAndOrPair(self, sValue):
        """
        Checks if sValue is a pair.
        """
        _ = sValue;
        return False;


class TestTypeEflags(TestType):
    """
    Special value parsing for EFLAGS/RFLAGS/FLAGS.
    """

    kdZeroValueFlags = { 'nv': 0, 'pl': 0, 'nz': 0, 'na': 0, 'pe': 0, 'nc': 0, 'di': 0, 'up': 0 };

    def __init__(self, sName):
        TestType.__init__(self, sName, acbSizes = [1, 2, 4, 8], fUnsigned = True);

    def get(self, sValue):
        fClear = 0;
        fSet   = 0;
        for sFlag in sValue.split(','):
            sConstant = g_kdEFlagsMnemonics.get(sFlag, None);
            if sConstant is None:
                raise self.BadValue('Unknown flag "%s" in "%s"' % (sFlag, sValue))
            if sConstant[0] == '!':
                fClear |= g_kdX86EFlagsConstants[sConstant[1:]];
            else:
                fSet   |= g_kdX86EFlagsConstants[sConstant];

        aoSet = TestType.get(self, '0x%x' % (fSet,));
        if fClear != 0:
            aoClear = TestType.get(self, '%#x' % (fClear,))
            assert self.isAndOrPair(sValue) is True;
            return (aoClear[0], aoSet[0]);
        assert self.isAndOrPair(sValue) is False;
        return aoSet;

    def isAndOrPair(self, sValue):
        for sZeroFlag in self.kdZeroValueFlags:
            if sValue.find(sZeroFlag) >= 0:
                return True;
        return False;

class TestTypeFromDict(TestType):
    """
    Special value parsing for CR0.
    """

    kdZeroValueFlags = { 'nv': 0, 'pl': 0, 'nz': 0, 'na': 0, 'pe': 0, 'nc': 0, 'di': 0, 'up': 0 };

    def __init__(self, sName, kdConstantsAndValues, sConstantPrefix):
        TestType.__init__(self, sName, acbSizes = [1, 2, 4, 8], fUnsigned = True);
        self.kdConstantsAndValues = kdConstantsAndValues;
        self.sConstantPrefix      = sConstantPrefix;

    def get(self, sValue):
        fValue = 0;
        for sFlag in sValue.split(','):
            fFlagValue = self.kdConstantsAndValues.get(self.sConstantPrefix + sFlag.upper(), None);
            if fFlagValue is None:
                raise self.BadValue('Unknown flag "%s" in "%s"' % (sFlag, sValue))
            fValue |= fFlagValue;
        return TestType.get(self, '0x%x' % (fValue,));


class TestInOut(object):
    """
    One input or output state modifier.

    This should be thought as values to modify BS3REGCTX and extended (needs
    to be structured) state.
    """
    ## Assigned operators.
    kasOperators = [
        '&|=',  # Special AND(INV)+OR operator for use with EFLAGS.
        '&~=',
        '&=',
        '|=',
        '='
    ];
    ## Types
    kdTypes = {
        'uint':  TestType('uint', fUnsigned = True),
        'int':   TestType('int'),
        'efl':   TestTypeEflags('efl'),
        'cr0':   TestTypeFromDict('cr0', g_kdX86Cr0Constants, 'X86_CR0_'),
        'cr4':   TestTypeFromDict('cr4', g_kdX86Cr4Constants, 'X86_CR4_'),
        'xcr0':  TestTypeFromDict('xcr0', g_kdX86XSaveCConstants, 'XSAVE_C_'),
    };
    ## CPU context fields.
    kdFields = {
        # name:         ( default type, [both|input|output], )
        # Operands.
        'op1':          ( 'uint', 'both',   ), ## \@op1
        'op2':          ( 'uint', 'both',   ), ## \@op2
        'op3':          ( 'uint', 'both',   ), ## \@op3
        'op4':          ( 'uint', 'both',   ), ## \@op4
        # Flags.
        'efl':          ( 'efl',  'both',   ),
        'efl_undef':    ( 'uint', 'output', ),
        # 8-bit GPRs.
        'al':           ( 'uint', 'both',   ),
        'cl':           ( 'uint', 'both',   ),
        'dl':           ( 'uint', 'both',   ),
        'bl':           ( 'uint', 'both',   ),
        'ah':           ( 'uint', 'both',   ),
        'ch':           ( 'uint', 'both',   ),
        'dh':           ( 'uint', 'both',   ),
        'bh':           ( 'uint', 'both',   ),
        'r8l':          ( 'uint', 'both',   ),
        'r9l':          ( 'uint', 'both',   ),
        'r10l':         ( 'uint', 'both',   ),
        'r11l':         ( 'uint', 'both',   ),
        'r12l':         ( 'uint', 'both',   ),
        'r13l':         ( 'uint', 'both',   ),
        'r14l':         ( 'uint', 'both',   ),
        'r15l':         ( 'uint', 'both',   ),
        # 16-bit GPRs.
        'ax':           ( 'uint', 'both',   ),
        'dx':           ( 'uint', 'both',   ),
        'cx':           ( 'uint', 'both',   ),
        'bx':           ( 'uint', 'both',   ),
        'sp':           ( 'uint', 'both',   ),
        'bp':           ( 'uint', 'both',   ),
        'si':           ( 'uint', 'both',   ),
        'di':           ( 'uint', 'both',   ),
        'r8w':          ( 'uint', 'both',   ),
        'r9w':          ( 'uint', 'both',   ),
        'r10w':         ( 'uint', 'both',   ),
        'r11w':         ( 'uint', 'both',   ),
        'r12w':         ( 'uint', 'both',   ),
        'r13w':         ( 'uint', 'both',   ),
        'r14w':         ( 'uint', 'both',   ),
        'r15w':         ( 'uint', 'both',   ),
        # 32-bit GPRs.
        'eax':          ( 'uint', 'both',   ),
        'edx':          ( 'uint', 'both',   ),
        'ecx':          ( 'uint', 'both',   ),
        'ebx':          ( 'uint', 'both',   ),
        'esp':          ( 'uint', 'both',   ),
        'ebp':          ( 'uint', 'both',   ),
        'esi':          ( 'uint', 'both',   ),
        'edi':          ( 'uint', 'both',   ),
        'r8d':          ( 'uint', 'both',   ),
        'r9d':          ( 'uint', 'both',   ),
        'r10d':         ( 'uint', 'both',   ),
        'r11d':         ( 'uint', 'both',   ),
        'r12d':         ( 'uint', 'both',   ),
        'r13d':         ( 'uint', 'both',   ),
        'r14d':         ( 'uint', 'both',   ),
        'r15d':         ( 'uint', 'both',   ),
        # 64-bit GPRs.
        'rax':          ( 'uint', 'both',   ),
        'rdx':          ( 'uint', 'both',   ),
        'rcx':          ( 'uint', 'both',   ),
        'rbx':          ( 'uint', 'both',   ),
        'rsp':          ( 'uint', 'both',   ),
        'rbp':          ( 'uint', 'both',   ),
        'rsi':          ( 'uint', 'both',   ),
        'rdi':          ( 'uint', 'both',   ),
        'r8':           ( 'uint', 'both',   ),
        'r9':           ( 'uint', 'both',   ),
        'r10':          ( 'uint', 'both',   ),
        'r11':          ( 'uint', 'both',   ),
        'r12':          ( 'uint', 'both',   ),
        'r13':          ( 'uint', 'both',   ),
        'r14':          ( 'uint', 'both',   ),
        'r15':          ( 'uint', 'both',   ),
        # 16-bit, 32-bit or 64-bit registers according to operand size.
        'oz.rax':       ( 'uint', 'both',   ),
        'oz.rdx':       ( 'uint', 'both',   ),
        'oz.rcx':       ( 'uint', 'both',   ),
        'oz.rbx':       ( 'uint', 'both',   ),
        'oz.rsp':       ( 'uint', 'both',   ),
        'oz.rbp':       ( 'uint', 'both',   ),
        'oz.rsi':       ( 'uint', 'both',   ),
        'oz.rdi':       ( 'uint', 'both',   ),
        'oz.r8':        ( 'uint', 'both',   ),
        'oz.r9':        ( 'uint', 'both',   ),
        'oz.r10':       ( 'uint', 'both',   ),
        'oz.r11':       ( 'uint', 'both',   ),
        'oz.r12':       ( 'uint', 'both',   ),
        'oz.r13':       ( 'uint', 'both',   ),
        'oz.r14':       ( 'uint', 'both',   ),
        'oz.r15':       ( 'uint', 'both',   ),
        # Control registers.
        'cr0':          ( 'cr0',  'both',   ),
        'cr4':          ( 'cr4',  'both',   ),
        'xcr0':         ( 'xcr0', 'both',   ),
        # FPU Registers
        'fcw':          ( 'uint', 'both',   ),
        'fsw':          ( 'uint', 'both',   ),
        'ftw':          ( 'uint', 'both',   ),
        'fop':          ( 'uint', 'both',   ),
        'fpuip':        ( 'uint', 'both',   ),
        'fpucs':        ( 'uint', 'both',   ),
        'fpudp':        ( 'uint', 'both',   ),
        'fpuds':        ( 'uint', 'both',   ),
        'mxcsr':        ( 'uint', 'both',   ),
        'st0':          ( 'uint', 'both',   ),
        'st1':          ( 'uint', 'both',   ),
        'st2':          ( 'uint', 'both',   ),
        'st3':          ( 'uint', 'both',   ),
        'st4':          ( 'uint', 'both',   ),
        'st5':          ( 'uint', 'both',   ),
        'st6':          ( 'uint', 'both',   ),
        'st7':          ( 'uint', 'both',   ),
        # MMX registers.
        'mm0':          ( 'uint', 'both',   ),
        'mm1':          ( 'uint', 'both',   ),
        'mm2':          ( 'uint', 'both',   ),
        'mm3':          ( 'uint', 'both',   ),
        'mm4':          ( 'uint', 'both',   ),
        'mm5':          ( 'uint', 'both',   ),
        'mm6':          ( 'uint', 'both',   ),
        'mm7':          ( 'uint', 'both',   ),
        # SSE registers.
        'xmm0':         ( 'uint', 'both',   ),
        'xmm1':         ( 'uint', 'both',   ),
        'xmm2':         ( 'uint', 'both',   ),
        'xmm3':         ( 'uint', 'both',   ),
        'xmm4':         ( 'uint', 'both',   ),
        'xmm5':         ( 'uint', 'both',   ),
        'xmm6':         ( 'uint', 'both',   ),
        'xmm7':         ( 'uint', 'both',   ),
        'xmm8':         ( 'uint', 'both',   ),
        'xmm9':         ( 'uint', 'both',   ),
        'xmm10':        ( 'uint', 'both',   ),
        'xmm11':        ( 'uint', 'both',   ),
        'xmm12':        ( 'uint', 'both',   ),
        'xmm13':        ( 'uint', 'both',   ),
        'xmm14':        ( 'uint', 'both',   ),
        'xmm15':        ( 'uint', 'both',   ),
        'xmm0.lo':      ( 'uint', 'both',   ),
        'xmm1.lo':      ( 'uint', 'both',   ),
        'xmm2.lo':      ( 'uint', 'both',   ),
        'xmm3.lo':      ( 'uint', 'both',   ),
        'xmm4.lo':      ( 'uint', 'both',   ),
        'xmm5.lo':      ( 'uint', 'both',   ),
        'xmm6.lo':      ( 'uint', 'both',   ),
        'xmm7.lo':      ( 'uint', 'both',   ),
        'xmm8.lo':      ( 'uint', 'both',   ),
        'xmm9.lo':      ( 'uint', 'both',   ),
        'xmm10.lo':     ( 'uint', 'both',   ),
        'xmm11.lo':     ( 'uint', 'both',   ),
        'xmm12.lo':     ( 'uint', 'both',   ),
        'xmm13.lo':     ( 'uint', 'both',   ),
        'xmm14.lo':     ( 'uint', 'both',   ),
        'xmm15.lo':     ( 'uint', 'both',   ),
        'xmm0.hi':      ( 'uint', 'both',   ),
        'xmm1.hi':      ( 'uint', 'both',   ),
        'xmm2.hi':      ( 'uint', 'both',   ),
        'xmm3.hi':      ( 'uint', 'both',   ),
        'xmm4.hi':      ( 'uint', 'both',   ),
        'xmm5.hi':      ( 'uint', 'both',   ),
        'xmm6.hi':      ( 'uint', 'both',   ),
        'xmm7.hi':      ( 'uint', 'both',   ),
        'xmm8.hi':      ( 'uint', 'both',   ),
        'xmm9.hi':      ( 'uint', 'both',   ),
        'xmm10.hi':     ( 'uint', 'both',   ),
        'xmm11.hi':     ( 'uint', 'both',   ),
        'xmm12.hi':     ( 'uint', 'both',   ),
        'xmm13.hi':     ( 'uint', 'both',   ),
        'xmm14.hi':     ( 'uint', 'both',   ),
        'xmm15.hi':     ( 'uint', 'both',   ),
        'xmm0.lo.zx':   ( 'uint', 'both',   ),
        'xmm1.lo.zx':   ( 'uint', 'both',   ),
        'xmm2.lo.zx':   ( 'uint', 'both',   ),
        'xmm3.lo.zx':   ( 'uint', 'both',   ),
        'xmm4.lo.zx':   ( 'uint', 'both',   ),
        'xmm5.lo.zx':   ( 'uint', 'both',   ),
        'xmm6.lo.zx':   ( 'uint', 'both',   ),
        'xmm7.lo.zx':   ( 'uint', 'both',   ),
        'xmm8.lo.zx':   ( 'uint', 'both',   ),
        'xmm9.lo.zx':   ( 'uint', 'both',   ),
        'xmm10.lo.zx':  ( 'uint', 'both',   ),
        'xmm11.lo.zx':  ( 'uint', 'both',   ),
        'xmm12.lo.zx':  ( 'uint', 'both',   ),
        'xmm13.lo.zx':  ( 'uint', 'both',   ),
        'xmm14.lo.zx':  ( 'uint', 'both',   ),
        'xmm15.lo.zx':  ( 'uint', 'both',   ),
        'xmm0.dw0':     ( 'uint', 'both',   ),
        'xmm1.dw0':     ( 'uint', 'both',   ),
        'xmm2.dw0':     ( 'uint', 'both',   ),
        'xmm3.dw0':     ( 'uint', 'both',   ),
        'xmm4.dw0':     ( 'uint', 'both',   ),
        'xmm5.dw0':     ( 'uint', 'both',   ),
        'xmm6.dw0':     ( 'uint', 'both',   ),
        'xmm7.dw0':     ( 'uint', 'both',   ),
        'xmm8.dw0':     ( 'uint', 'both',   ),
        'xmm9.dw0':     ( 'uint', 'both',   ),
        'xmm10.dw0':    ( 'uint', 'both',   ),
        'xmm11.dw0':    ( 'uint', 'both',   ),
        'xmm12.dw0':    ( 'uint', 'both',   ),
        'xmm13.dw0':    ( 'uint', 'both',   ),
        'xmm14.dw0':    ( 'uint', 'both',   ),
        'xmm15_dw0':    ( 'uint', 'both',   ),
        # AVX registers.
        'ymm0':         ( 'uint', 'both',   ),
        'ymm1':         ( 'uint', 'both',   ),
        'ymm2':         ( 'uint', 'both',   ),
        'ymm3':         ( 'uint', 'both',   ),
        'ymm4':         ( 'uint', 'both',   ),
        'ymm5':         ( 'uint', 'both',   ),
        'ymm6':         ( 'uint', 'both',   ),
        'ymm7':         ( 'uint', 'both',   ),
        'ymm8':         ( 'uint', 'both',   ),
        'ymm9':         ( 'uint', 'both',   ),
        'ymm10':        ( 'uint', 'both',   ),
        'ymm11':        ( 'uint', 'both',   ),
        'ymm12':        ( 'uint', 'both',   ),
        'ymm13':        ( 'uint', 'both',   ),
        'ymm14':        ( 'uint', 'both',   ),
        'ymm15':        ( 'uint', 'both',   ),

        # Special ones.
        'value.xcpt':   ( 'uint', 'output', ),
    };

    def __init__(self, sField, sOp, sValue, sType):
        assert sField in self.kdFields;
        assert sOp in self.kasOperators;
        self.sField = sField;
        self.sOp    = sOp;
        self.sValue = sValue;
        self.sType  = sType;
        assert isinstance(sField, str);
        assert isinstance(sOp, str);
        assert isinstance(sType, str);
        assert isinstance(sValue, str);


class TestSelector(object):
    """
    One selector for an instruction test.
    """
    ## Selector compare operators.
    kasCompareOps = [ '==', '!=' ];
    ## Selector variables and their valid values.
    kdVariables = {
        # Operand size.
        'size': {
            'o16':  'size_o16',
            'o32':  'size_o32',
            'o64':  'size_o64',
        },
        # VEX.L value.
        'vex.l': {
            '0':    'vexl_0',
            '1':    'vexl_1',
        },
        # Execution ring.
        'ring': {
            '0':    'ring_0',
            '1':    'ring_1',
            '2':    'ring_2',
            '3':    'ring_3',
            '0..2': 'ring_0_thru_2',
            '1..3': 'ring_1_thru_3',
        },
        # Basic code mode.
        'codebits': {
            '64':   'code_64bit',
            '32':   'code_32bit',
            '16':   'code_16bit',
        },
        # cpu modes.
        'mode': {
            'real': 'mode_real',
            'prot': 'mode_prot',
            'long': 'mode_long',
            'v86':  'mode_v86',
            'smm':  'mode_smm',
            'vmx':  'mode_vmx',
            'svm':  'mode_svm',
        },
        # paging on/off
        'paging': {
            'on':       'paging_on',
            'off':      'paging_off',
        },
        # CPU vendor
        'vendor': {
            'amd':      'vendor_amd',
            'intel':    'vendor_intel',
            'via':      'vendor_via',
        },
    };
    ## Selector shorthand predicates.
    ## These translates into variable expressions.
    kdPredicates = {
        'o16':          'size==o16',
        'o32':          'size==o32',
        'o64':          'size==o64',
        'ring0':        'ring==0',
        '!ring0':       'ring==1..3',
        'ring1':        'ring==1',
        'ring2':        'ring==2',
        'ring3':        'ring==3',
        'user':         'ring==3',
        'supervisor':   'ring==0..2',
        '16-bit':       'codebits==16',
        '32-bit':       'codebits==32',
        '64-bit':       'codebits==64',
        'real':         'mode==real',
        'prot':         'mode==prot',
        'long':         'mode==long',
        'v86':          'mode==v86',
        'smm':          'mode==smm',
        'vmx':          'mode==vmx',
        'svm':          'mode==svm',
        'paging':       'paging==on',
        '!paging':      'paging==off',
        'amd':          'vendor==amd',
        '!amd':         'vendor!=amd',
        'intel':        'vendor==intel',
        '!intel':       'vendor!=intel',
        'via':          'vendor==via',
        '!via':         'vendor!=via',
    };

    def __init__(self, sVariable, sOp, sValue):
        assert sVariable in self.kdVariables;
        assert sOp in self.kasCompareOps;
        assert sValue in self.kdVariables[sVariable];
        self.sVariable  = sVariable;
        self.sOp        = sOp;
        self.sValue     = sValue;


class InstructionTest(object):
    """
    Instruction test.
    """

    def __init__(self, oInstr): # type: (InstructionTest, Instruction)
        self.oInstr         = oInstr    # type: InstructionTest
        self.aoInputs       = []        # type: List[TestInOut]
        self.aoOutputs      = []        # type: List[TestInOut]
        self.aoSelectors    = []        # type: List[TestSelector]

    def toString(self, fRepr = False):
        """
        Converts it to string representation.
        """
        asWords = [];
        if self.aoSelectors:
            for oSelector in self.aoSelectors:
                asWords.append('%s%s%s' % (oSelector.sVariable, oSelector.sOp, oSelector.sValue,));
            asWords.append('/');

        for oModifier in self.aoInputs:
            asWords.append('%s%s%s:%s' % (oModifier.sField, oModifier.sOp, oModifier.sValue, oModifier.sType,));

        asWords.append('->');

        for oModifier in self.aoOutputs:
            asWords.append('%s%s%s:%s' % (oModifier.sField, oModifier.sOp, oModifier.sValue, oModifier.sType,));

        if fRepr:
            return '<' + ' '.join(asWords) + '>';
        return '  '.join(asWords);

    def __str__(self):
        """ Provide string represenation. """
        return self.toString(False);

    def __repr__(self):
        """ Provide unambigious string representation. """
        return self.toString(True);

class Operand(object):
    """
    Instruction operand.
    """

    def __init__(self, sWhere, sType):
        assert sWhere in g_kdOpLocations, sWhere;
        assert sType  in g_kdOpTypes, sType;
        self.sWhere = sWhere;           ##< g_kdOpLocations
        self.sType  = sType;            ##< g_kdOpTypes

    def usesModRM(self):
        """ Returns True if using some form of ModR/M encoding. """
        return self.sType[0] in ['E', 'G', 'M'];



class Instruction(object): # pylint: disable=too-many-instance-attributes
    """
    Instruction.
    """

    def __init__(self, sSrcFile, iLine):
        ## @name Core attributes.
        ## @{
        self.oParent        = None      # type: Instruction
        self.sMnemonic      = None;
        self.sBrief         = None;
        self.asDescSections = []        # type: List[str]
        self.aoMaps         = []        # type: List[InstructionMap]
        self.aoOperands     = []        # type: List[Operand]
        self.sPrefix        = None;     ##< Single prefix: None, 'none', 0x66, 0xf3, 0xf2
        self.sOpcode        = None      # type: str
        self.sSubOpcode     = None      # type: str
        self.sEncoding      = None;
        self.asFlTest       = None;
        self.asFlModify     = None;
        self.asFlUndefined  = None;
        self.asFlSet        = None;
        self.asFlClear      = None;
        self.dHints         = {};       ##< Dictionary of instruction hints, flags, whatnot. (Dictionary for speed; dummy value).
        self.sDisEnum       = None;     ##< OP_XXXX value.  Default is based on the uppercased mnemonic.
        self.asCpuIds       = [];       ##< The CPUID feature bit names for this instruction. If multiple, assume AND.
        self.asReqFeatures  = [];       ##< Which features are required to be enabled to run this instruction.
        self.aoTests        = []        # type: List[InstructionTest]
        self.sMinCpu        = None;     ##< Indicates the minimum CPU required for the instruction. Not set when oCpuExpr is.
        self.oCpuExpr       = None;     ##< Some CPU restriction expression...
        self.sGroup         = None;
        self.fUnused        = False;    ##< Unused instruction.
        self.fInvalid       = False;    ##< Invalid instruction (like UD2).
        self.sInvalidStyle  = None;     ##< Invalid behviour style (g_kdInvalidStyles),
        self.sXcptType      = None;     ##< Exception type (g_kdXcptTypes).
        ## @}

        ## @name Implementation attributes.
        ## @{
        self.sStats         = None;
        self.sFunction      = None;
        self.fStub          = False;
        self.fUdStub        = False;
        ## @}

        ## @name Decoding info
        ## @{
        self.sSrcFile       = sSrcFile;
        self.iLineCreated   = iLine;
        self.iLineCompleted = None;
        self.cOpTags        = 0;
        self.iLineFnIemOpMacro  = -1;
        self.iLineMnemonicMacro = -1;
        ## @}

        ## @name Intermediate input fields.
        ## @{
        self.sRawDisOpNo    = None;
        self.asRawDisParams = [];
        self.sRawIemOpFlags = None;
        self.sRawOldOpcodes = None;
        self.asCopyTests    = [];
        ## @}

        ## All the MC blocks associated with this instruction.
        self.aoMcBlocks     = []        # type: List[McBlock]

    def toString(self, fRepr = False):
        """ Turn object into a string. """
        aasFields = [];

        aasFields.append(['opcode',    self.sOpcode]);
        if self.sPrefix:
            aasFields.append(['prefix', self.sPrefix]);
        aasFields.append(['mnemonic',  self.sMnemonic]);
        for iOperand, oOperand in enumerate(self.aoOperands):
            aasFields.append(['op%u' % (iOperand + 1,), '%s:%s' % (oOperand.sWhere, oOperand.sType,)]);
        if self.aoMaps:         aasFields.append(['maps', ','.join([oMap.sName for oMap in self.aoMaps])]);
        aasFields.append(['encoding',  self.sEncoding]);
        if self.dHints:         aasFields.append(['hints', ','.join(self.dHints.keys())]);
        aasFields.append(['disenum',   self.sDisEnum]);
        if self.asCpuIds:       aasFields.append(['cpuid', ','.join(self.asCpuIds)]);
        aasFields.append(['group',     self.sGroup]);
        if self.fUnused:        aasFields.append(['unused', 'True']);
        if self.fInvalid:       aasFields.append(['invalid', 'True']);
        aasFields.append(['invlstyle', self.sInvalidStyle]);
        aasFields.append(['fltest',    self.asFlTest]);
        aasFields.append(['flmodify',  self.asFlModify]);
        aasFields.append(['flundef',   self.asFlUndefined]);
        aasFields.append(['flset',     self.asFlSet]);
        aasFields.append(['flclear',   self.asFlClear]);
        aasFields.append(['mincpu',    self.sMinCpu]);
        aasFields.append(['stats',     self.sStats]);
        aasFields.append(['sFunction', self.sFunction]);
        if self.fStub:          aasFields.append(['fStub', 'True']);
        if self.fUdStub:        aasFields.append(['fUdStub', 'True']);
        if self.cOpTags:        aasFields.append(['optags', str(self.cOpTags)]);
        if self.iLineFnIemOpMacro  != -1: aasFields.append(['FNIEMOP_XXX', str(self.iLineFnIemOpMacro)]);
        if self.iLineMnemonicMacro != -1: aasFields.append(['IEMOP_MNEMMONICn', str(self.iLineMnemonicMacro)]);

        sRet = '<' if fRepr else '';
        for sField, sValue in aasFields:
            if sValue is not None:
                if len(sRet) > 1:
                    sRet += '; ';
                sRet += '%s=%s' % (sField, sValue,);
        if fRepr:
            sRet += '>';

        return sRet;

    def __str__(self):
        """ Provide string represenation. """
        return self.toString(False);

    def __repr__(self):
        """ Provide unambigious string representation. """
        return self.toString(True);

    def copy(self, oMap = None, sOpcode = None, sSubOpcode = None, sPrefix = None):
        """
        Makes a copy of the object for the purpose of putting in a different map
        or a different place in the current map.
        """
        oCopy = Instruction(self.sSrcFile, self.iLineCreated);

        oCopy.oParent           = self;
        oCopy.sMnemonic         = self.sMnemonic;
        oCopy.sBrief            = self.sBrief;
        oCopy.asDescSections    = list(self.asDescSections);
        oCopy.aoMaps            = [oMap,]    if oMap       else list(self.aoMaps);
        oCopy.aoOperands        = list(self.aoOperands); ## Deeper copy?
        oCopy.sPrefix           = sPrefix    if sPrefix    else self.sPrefix;
        oCopy.sOpcode           = sOpcode    if sOpcode    else self.sOpcode;
        oCopy.sSubOpcode        = sSubOpcode if sSubOpcode else self.sSubOpcode;
        oCopy.sEncoding         = self.sEncoding;
        oCopy.asFlTest          = self.asFlTest;
        oCopy.asFlModify        = self.asFlModify;
        oCopy.asFlUndefined     = self.asFlUndefined;
        oCopy.asFlSet           = self.asFlSet;
        oCopy.asFlClear         = self.asFlClear;
        oCopy.dHints            = dict(self.dHints);
        oCopy.sDisEnum          = self.sDisEnum;
        oCopy.asCpuIds          = list(self.asCpuIds);
        oCopy.asReqFeatures     = list(self.asReqFeatures);
        oCopy.aoTests           = list(self.aoTests); ## Deeper copy?
        oCopy.sMinCpu           = self.sMinCpu;
        oCopy.oCpuExpr          = self.oCpuExpr;
        oCopy.sGroup            = self.sGroup;
        oCopy.fUnused           = self.fUnused;
        oCopy.fInvalid          = self.fInvalid;
        oCopy.sInvalidStyle     = self.sInvalidStyle;
        oCopy.sXcptType         = self.sXcptType;

        oCopy.sStats            = self.sStats;
        oCopy.sFunction         = self.sFunction;
        oCopy.fStub             = self.fStub;
        oCopy.fUdStub           = self.fUdStub;

        oCopy.iLineCompleted    = self.iLineCompleted;
        oCopy.cOpTags           = self.cOpTags;
        oCopy.iLineFnIemOpMacro = self.iLineFnIemOpMacro;
        oCopy.iLineMnemonicMacro = self.iLineMnemonicMacro;

        oCopy.sRawDisOpNo       = self.sRawDisOpNo;
        oCopy.asRawDisParams    = list(self.asRawDisParams);
        oCopy.sRawIemOpFlags    = self.sRawIemOpFlags;
        oCopy.sRawOldOpcodes    = self.sRawOldOpcodes;
        oCopy.asCopyTests       = list(self.asCopyTests);

        return oCopy;

    def getOpcodeByte(self):
        """
        Decodes sOpcode into a byte range integer value.
        Raises exception if sOpcode is None or invalid.
        """
        if self.sOpcode is None:
            raise Exception('No opcode byte for %s!' % (self,));
        sOpcode = str(self.sOpcode);    # pylint type confusion workaround.

        # Full hex byte form.
        if sOpcode[:2] == '0x':
            return int(sOpcode, 16);

        # The /r form:
        if len(sOpcode) == 2 and sOpcode[0] == '/' and sOpcode[1].isdigit():
            return int(sOpcode[1:]) << 3;

        # The 11/r form:
        if len(sOpcode) == 4 and sOpcode.startswith('11/') and sOpcode[-1].isdigit():
            return (int(sOpcode[-1:]) << 3) | 0xc0;

        # The !11/r form (returns mod=1):
        ## @todo this doesn't really work...
        if len(sOpcode) == 5 and sOpcode.startswith('!11/') and sOpcode[-1].isdigit():
            return (int(sOpcode[-1:]) << 3) | 0x80;

        raise Exception('unsupported opcode byte spec "%s" for %s' % (sOpcode, self,));

    @staticmethod
    def _flagsToIntegerMask(asFlags):
        """
        Returns the integer mask value for asFlags.
        """
        uRet = 0;
        if asFlags:
            for sFlag in asFlags:
                sConstant = g_kdEFlagsMnemonics[sFlag];
                assert sConstant[0] != '!', sConstant
                uRet |= g_kdX86EFlagsConstants[sConstant];
        return uRet;

    def getTestedFlagsMask(self):
        """ Returns asFlTest into a integer mask value """
        return self._flagsToIntegerMask(self.asFlTest);

    def getModifiedFlagsMask(self):
        """ Returns asFlModify into a integer mask value """
        return self._flagsToIntegerMask(self.asFlModify);

    def getUndefinedFlagsMask(self):
        """ Returns asFlUndefined into a integer mask value """
        return self._flagsToIntegerMask(self.asFlUndefined);

    def getSetFlagsMask(self):
        """ Returns asFlSet into a integer mask value """
        return self._flagsToIntegerMask(self.asFlSet);

    def getClearedFlagsMask(self):
        """ Returns asFlClear into a integer mask value """
        return self._flagsToIntegerMask(self.asFlClear);

    @staticmethod
    def _flagsToC(asFlags):
        """
        Returns asFlags converted to X86_EFL_XXX ored together C-style.
        """
        if asFlags:
            asRet = [];
            for sFlag in asFlags:
                sConstant = g_kdEFlagsMnemonics[sFlag];
                assert sConstant[0] != '!', sConstant
                asRet.append(sConstant);
            return ' | '.join(asRet);
        return '0';

    def getTestedFlagsCStyle(self):
        """ Returns asFlTest as C constants ored together. """
        return self._flagsToC(self.asFlTest);

    def getModifiedFlagsCStyle(self):
        """ Returns asFlModify as C constants ored together. """
        return self._flagsToC(self.asFlModify);

    def getUndefinedFlagsCStyle(self):
        """ Returns asFlUndefined as C constants ored together. """
        return self._flagsToC(self.asFlUndefined);

    def getSetFlagsCStyle(self):
        """ Returns asFlSet as C constants ored together. """
        return self._flagsToC(self.asFlSet);

    def getClearedFlagsCStyle(self):
        """ Returns asFlClear as C constants ored together. """
        return self._flagsToC(self.asFlClear);

    def onlyInVexMaps(self):
        """ Returns True if only in VEX maps, otherwise False.  (No maps -> False) """
        if not self.aoMaps:
            return False;
        for oMap in self.aoMaps:
            if not oMap.isVexMap():
                return False;
        return True;



## All the instructions.
g_aoAllInstructions = []            # type: List[Instruction]

## All the instructions indexed by statistics name (opstat).
g_dAllInstructionsByStat = {}       # type: Dict[Instruction]

## All the instructions indexed by function name (opfunction).
g_dAllInstructionsByFunction = {}   # type: Dict[List[Instruction]]

## Instructions tagged by oponlytest
g_aoOnlyTestInstructions = []       # type: List[Instruction]

## Instruction maps.
g_aoInstructionMaps = [
    InstructionMap('one',        'g_apfnOneByteMap',        sSelector = 'byte'),
    InstructionMap('grp1_80',    asLeadOpcodes = ['0x80',], sSelector = '/r'),
    InstructionMap('grp1_81',    asLeadOpcodes = ['0x81',], sSelector = '/r'),
    InstructionMap('grp1_82',    asLeadOpcodes = ['0x82',], sSelector = '/r'),
    InstructionMap('grp1_83',    asLeadOpcodes = ['0x83',], sSelector = '/r'),
    InstructionMap('grp1a',      asLeadOpcodes = ['0x8f',], sSelector = '/r'),
    InstructionMap('grp2_c0',    asLeadOpcodes = ['0xc0',], sSelector = '/r'),
    InstructionMap('grp2_c1',    asLeadOpcodes = ['0xc1',], sSelector = '/r'),
    InstructionMap('grp2_d0',    asLeadOpcodes = ['0xd0',], sSelector = '/r'),
    InstructionMap('grp2_d1',    asLeadOpcodes = ['0xd1',], sSelector = '/r'),
    InstructionMap('grp2_d2',    asLeadOpcodes = ['0xd2',], sSelector = '/r'),
    InstructionMap('grp2_d3',    asLeadOpcodes = ['0xd3',], sSelector = '/r'),
    ## @todo g_apfnEscF1_E0toFF
    InstructionMap('grp3_f6',    asLeadOpcodes = ['0xf6',], sSelector = '/r'),
    InstructionMap('grp3_f7',    asLeadOpcodes = ['0xf7',], sSelector = '/r'),
    InstructionMap('grp4',       asLeadOpcodes = ['0xfe',], sSelector = '/r'),
    InstructionMap('grp5',       asLeadOpcodes = ['0xff',], sSelector = '/r'),
    InstructionMap('grp11_c6_m', asLeadOpcodes = ['0xc6',], sSelector = '!11 /r'),
    InstructionMap('grp11_c6_r', asLeadOpcodes = ['0xc6',], sSelector = '11'),    # xabort
    InstructionMap('grp11_c7_m', asLeadOpcodes = ['0xc7',], sSelector = '!11 /r'),
    InstructionMap('grp11_c7_r', asLeadOpcodes = ['0xc7',], sSelector = '11'),    # xbegin

    InstructionMap('two0f',      'g_apfnTwoByteMap',    asLeadOpcodes = ['0x0f',], sDisParse = 'IDX_ParseTwoByteEsc'),
    InstructionMap('grp6',       'g_apfnGroup6',        asLeadOpcodes = ['0x0f', '0x00',], sSelector = '/r'),
    InstructionMap('grp7_m',     'g_apfnGroup7Mem',     asLeadOpcodes = ['0x0f', '0x01',], sSelector = '!11 /r'),
    InstructionMap('grp7_r',                            asLeadOpcodes = ['0x0f', '0x01',], sSelector = '11'),
    InstructionMap('grp8',                              asLeadOpcodes = ['0x0f', '0xba',], sSelector = '/r'),
    InstructionMap('grp9',       'g_apfnGroup9RegReg',  asLeadOpcodes = ['0x0f', '0xc7',], sSelector = 'mod /r'),
    ## @todo What about g_apfnGroup9MemReg?
    InstructionMap('grp10',      None,                  asLeadOpcodes = ['0x0f', '0xb9',], sSelector = '/r'), # UD1 /w modr/m
    InstructionMap('grp12',      'g_apfnGroup12RegReg', asLeadOpcodes = ['0x0f', '0x71',], sSelector = 'mod /r'),
    InstructionMap('grp13',      'g_apfnGroup13RegReg', asLeadOpcodes = ['0x0f', '0x72',], sSelector = 'mod /r'),
    InstructionMap('grp14',      'g_apfnGroup14RegReg', asLeadOpcodes = ['0x0f', '0x73',], sSelector = 'mod /r'),
    InstructionMap('grp15',      'g_apfnGroup15MemReg', asLeadOpcodes = ['0x0f', '0xae',], sSelector = 'memreg /r'),
    ## @todo What about g_apfnGroup15RegReg?
    InstructionMap('grp16',       asLeadOpcodes = ['0x0f', '0x18',], sSelector = 'mod /r'),
    InstructionMap('grpA17',      asLeadOpcodes = ['0x0f', '0x78',], sSelector = '/r'), # AMD: EXTRQ weirdness
    InstructionMap('grpP',        asLeadOpcodes = ['0x0f', '0x0d',], sSelector = '/r'), # AMD: prefetch

    InstructionMap('three0f38',  'g_apfnThreeByte0f38',    asLeadOpcodes = ['0x0f', '0x38',]),
    InstructionMap('three0f3a',  'g_apfnThreeByte0f3a',    asLeadOpcodes = ['0x0f', '0x3a',]),

    InstructionMap('vexmap1',  'g_apfnVexMap1',          sEncoding = 'vex1'),
    InstructionMap('vexgrp12', 'g_apfnVexGroup12RegReg', sEncoding = 'vex1', asLeadOpcodes = ['0x71',], sSelector = 'mod /r'),
    InstructionMap('vexgrp13', 'g_apfnVexGroup13RegReg', sEncoding = 'vex1', asLeadOpcodes = ['0x72',], sSelector = 'mod /r'),
    InstructionMap('vexgrp14', 'g_apfnVexGroup14RegReg', sEncoding = 'vex1', asLeadOpcodes = ['0x73',], sSelector = 'mod /r'),
    InstructionMap('vexgrp15', 'g_apfnVexGroup15MemReg', sEncoding = 'vex1', asLeadOpcodes = ['0xae',], sSelector = 'memreg /r'),
    InstructionMap('vexgrp17', 'g_apfnVexGroup17_f3',    sEncoding = 'vex1', asLeadOpcodes = ['0xf3',], sSelector = '/r'),

    InstructionMap('vexmap2',  'g_apfnVexMap2',          sEncoding = 'vex2'),
    InstructionMap('vexmap3',  'g_apfnVexMap3',          sEncoding = 'vex3'),

    InstructionMap('3dnow',    asLeadOpcodes = ['0x0f', '0x0f',]),
    InstructionMap('xopmap8',  sEncoding = 'xop8'),
    InstructionMap('xopmap9',  sEncoding = 'xop9'),
    InstructionMap('xopgrp1',  sEncoding = 'xop9',  asLeadOpcodes = ['0x01'], sSelector = '/r'),
    InstructionMap('xopgrp2',  sEncoding = 'xop9',  asLeadOpcodes = ['0x02'], sSelector = '/r'),
    InstructionMap('xopgrp3',  sEncoding = 'xop9',  asLeadOpcodes = ['0x12'], sSelector = '/r'),
    InstructionMap('xopmap10', sEncoding = 'xop10'),
    InstructionMap('xopgrp4',  sEncoding = 'xop10', asLeadOpcodes = ['0x12'], sSelector = '/r'),
];
g_dInstructionMaps          = { oMap.sName:    oMap for oMap in g_aoInstructionMaps };
g_dInstructionMapsByIemName = { oMap.sIemName: oMap for oMap in g_aoInstructionMaps };


#
# Decoder functions.
#

class DecoderFunction(object):
    """
    Decoder function.

    This is mainly for searching for scoping searches for variables used in
    microcode blocks.
    """
    def __init__(self, sSrcFile, iBeginLine, sName, asDefArgs):
        self.sName        = sName;                  ##< The function name.
        self.asDefArgs    = asDefArgs;              ##< The FNIEMOP*DEF/STUB* macro argument list, 0th element is the macro name.
        self.sSrcFile     = sSrcFile;               ##< The source file the function is defined in.
        self.iBeginLine   = iBeginLine;             ##< The start line.
        self.iEndLine     = -1;                     ##< The line the function (probably) ends on.
        self.asLines      = [] # type: List[str]    ##< The raw lines the function is made up of.

    def complete(self, iEndLine, asLines):
        """
        Completes the function.
        """
        assert self.iEndLine == -1;
        self.iEndLine     = iEndLine;
        self.asLines      = asLines;


#
# "Microcode" statements and blocks
#

class McStmt(object):
    """
    Statement in a microcode block.
    """
    def __init__(self, sName, asParams):
        self.sName    = sName;     ##< 'IEM_MC_XXX' or 'C++'.
        self.asParams = asParams;
        self.oUser    = None;

    def __eq__(self, oOther):
        if self.sName != oOther.sName:
            return False;
        if len(self.asParams) != len(oOther.asParams):
            return False;
        for iParam, sMyParam in enumerate(self.asParams):
            if sMyParam != oOther.asParams[iParam]:
                return False;
        return True;

    def renderCode(self, cchIndent = 0):
        """
        Renders the code for the statement.
        """
        return ' ' * cchIndent + self.sName + '(' + ', '.join(self.asParams) + ');\n';

    @staticmethod
    def renderCodeForList(aoStmts, cchIndent = 0):
        """
        Renders a list of statements.
        """
        return ''.join([oStmt.renderCode(cchIndent) for oStmt in aoStmts]);

    @staticmethod
    def findStmtByNames(aoStmts, dNames):
        """
        Returns first statement with any of the given names in from the list.

        Note! The names are passed as a dictionary for quick lookup, the value
              does not matter.
        """
        for oStmt in aoStmts:
            if oStmt.sName in dNames:
                return oStmt;
            if isinstance(oStmt, McStmtCond):
                oHit = McStmt.findStmtByNames(oStmt.aoIfBranch, dNames);
                if not oHit:
                    oHit = McStmt.findStmtByNames(oStmt.aoElseBranch, dNames);
                if oHit:
                    return oHit;
        return None;

    @staticmethod
    def countStmtsByName(aoStmts, dNames, dRet):
        """
        Searches the given list of statements for the names in the dictionary,
        adding each found to dRet with an occurnece count.

        return total number of hits;
        """
        cHits = 0;
        for oStmt in aoStmts:
            if oStmt.sName in dNames:
                dRet[oStmt.sName] = dRet.get(oStmt.sName, 0) + 1;
                cHits += 1;
            if isinstance(oStmt, McStmtCond):
                cHits += McStmt.countStmtsByName(oStmt.aoIfBranch,   dNames, dRet);
                cHits += McStmt.countStmtsByName(oStmt.aoElseBranch, dNames, dRet);
        return cHits;

    def isCppStmt(self):
        """ Checks if this is a C++ statement. """
        return self.sName.startswith('C++');

class McStmtCond(McStmt):
    """
    Base class for conditional statements (IEM_MC_IF_XXX, IEM_MC_NATIVE_IF).
    """
    def __init__(self, sName, asParams, aoIfBranch = None, aoElseBranch = None):
        McStmt.__init__(self, sName, asParams);
        self.aoIfBranch            = [] if aoIfBranch   is None else list(aoIfBranch);
        self.aoElseBranch          = [] if aoElseBranch is None else list(aoElseBranch);
        self.oIfBranchAnnotation   = None;      ##< User specific IF-branch annotation.
        self.oElseBranchAnnotation = None;      ##< User specific IF-branch annotation.
        self.sNativeInfix          = '' if sName != 'IEM_MC_NATIVE_IF' else '_NATIVE';

    def renderCode(self, cchIndent = 0):
        sRet  = ' ' * cchIndent + self.sName + '(' + ', '.join(self.asParams) + ') {\n';
        sRet += self.renderCodeForList(self.aoIfBranch, cchIndent + 4);
        if self.aoElseBranch:
            sRet += ' ' * cchIndent + '} IEM_MC%s_ELSE() {\n' % (self.sNativeInfix,);
            sRet += self.renderCodeForList(self.aoElseBranch, cchIndent + 4);
        sRet += ' ' * cchIndent + '} IEM_MC%s_ENDIF();\n' % (self.sNativeInfix,);
        return sRet;

class McStmtNativeIf(McStmtCond):
    """ IEM_MC_NATIVE_IF """
    def __init__(self, sName, asArchitectures):
        McStmtCond.__init__(self, sName, ['|'.join(asArchitectures) if asArchitectures else '0',]);
        self.asArchitectures = asArchitectures;

class McStmtVar(McStmt):
    """ IEM_MC_LOCAL, IEM_MC_LOCAL_ASSIGN, IEM_MC_LOCAL_CONST """
    def __init__(self, sName, asParams, sType, sVarName, sValue = None):
        McStmt.__init__(self, sName, asParams);
        self.sType       = sType;
        self.sVarName    = sVarName;
        self.sValue      = sValue;              ##< None if no assigned / const value.

class McStmtArg(McStmtVar):
    """ IEM_MC_ARG, IEM_MC_ARG_CONST, IEM_MC_ARG_LOCAL_REF """
    def __init__(self, sName, asParams, sType, sVarName, iArg, sConstValue = None, sRef = None, sRefType = 'none'):
        McStmtVar.__init__(self, sName, asParams, sType, sVarName, sConstValue);
        self.iArg       = iArg;
        self.sRef       = sRef;       ##< The reference string (local variable, register).
        self.sRefType   = sRefType;   ##< The kind of reference: 'local', 'none'.
        assert sRefType in ('none', 'local');

class McStmtCall(McStmt):
    """ IEM_MC_CALL_* """
    def __init__(self, sName, asParams, iFnParam, iRcNameParam = -1):
        McStmt.__init__(self, sName, asParams);
        self.idxFn       = iFnParam;
        self.idxParams   = iFnParam + 1;
        self.sFn         = asParams[iFnParam];
        self.iRcName     = None if iRcNameParam < 0 else asParams[iRcNameParam];

class McStmtAssertEFlags(McStmt):
    """
    IEM_MC_ASSERT_EFLAGS
    """
    def __init__(self, oInstruction):
        McStmt.__init__(self, 'IEM_MC_ASSERT_EFLAGS',
                        [oInstruction.getTestedFlagsCStyle(), oInstruction.getModifiedFlagsCStyle(),]);


class McCppGeneric(McStmt):
    """
    Generic C++/C statement.
    """
    def __init__(self, sCode, fDecode = True, sName = 'C++', cchIndent = 0):
        McStmt.__init__(self, sName, [sCode,]);
        self.fDecode   = fDecode;
        self.cchIndent = cchIndent;

    def renderCode(self, cchIndent = 0):
        cchIndent += self.cchIndent;
        sRet = ' ' * cchIndent + self.asParams[0] + '\n';
        if self.fDecode:
            sRet = sRet.replace('\n', ' // C++ decode\n');
        else:
            sRet = sRet.replace('\n', ' // C++ normal\n');
        return sRet;

class McCppCall(McCppGeneric):
    """
    A generic C++/C call statement.

    The sName is still 'C++', so the function name is in the first parameter
    and the the arguments in the subsequent ones.
    """
    def __init__(self, sFnName, asArgs, fDecode = True, cchIndent = 0):
        McCppGeneric.__init__(self, sFnName, fDecode = fDecode, cchIndent = cchIndent);
        self.asParams.extend(asArgs);

    def renderCode(self, cchIndent = 0):
        cchIndent += self.cchIndent;
        sRet = ' ' * cchIndent + self.asParams[0] + '(' + ', '.join(self.asParams[1:]) + ');';
        if self.fDecode:
            sRet += ' // C++ decode\n';
        else:
            sRet += ' // C++ normal\n';
        return sRet;

class McCppCond(McStmtCond):
    """
    C++/C 'if' statement.
    """
    def __init__(self, sCode, fDecode = True, aoIfBranch = None, aoElseBranch = None, cchIndent = 0):
        McStmtCond.__init__(self, 'C++/if', [sCode,], aoIfBranch, aoElseBranch);
        self.fDecode   = fDecode;
        self.cchIndent = cchIndent;

    def renderCode(self, cchIndent = 0):
        cchIndent += self.cchIndent;
        sAnnotation = '// C++ decode' if self.fDecode else '// C++ normal';
        sRet  = ' ' * cchIndent + 'if (' + self.asParams[0] + ') ' + sAnnotation + '\n';
        sRet += ' ' * cchIndent + '{\n';
        sRet += self.renderCodeForList(self.aoIfBranch, cchIndent + 4);
        sRet += ' ' * cchIndent + '}\n';
        if self.aoElseBranch:
            sRet += ' ' * cchIndent + 'else ' + sAnnotation + '\n';
            sRet += ' ' * cchIndent + '{\n';
            sRet += self.renderCodeForList(self.aoElseBranch, cchIndent + 4);
            sRet += ' ' * cchIndent + '}\n';
        return sRet;

class McCppPreProc(McCppGeneric):
    """
    C++/C Preprocessor directive.
    """
    def __init__(self, sCode):
        McCppGeneric.__init__(self, sCode, False, sName = 'C++/preproc');

    def renderCode(self, cchIndent = 0):
        return self.asParams[0] + '\n';


## IEM_MC_F_XXX values.
g_kdMcFlags = {
    'IEM_MC_F_ONLY_8086':           (),
    'IEM_MC_F_MIN_186':             (),
    'IEM_MC_F_MIN_286':             (),
    'IEM_MC_F_NOT_286_OR_OLDER':    (),
    'IEM_MC_F_MIN_386':             ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_MIN_486':             ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_MIN_PENTIUM':         ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_MIN_PENTIUM_II':      ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_MIN_CORE':            ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_64BIT':               ('IEM_MC_F_NOT_286_OR_OLDER',),
    'IEM_MC_F_NOT_64BIT':           (),
};
## IEM_MC_F_XXX values.
g_kdCImplFlags = {
    'IEM_CIMPL_F_BRANCH_DIRECT':        (),
    'IEM_CIMPL_F_BRANCH_INDIRECT':      (),
    'IEM_CIMPL_F_BRANCH_RELATIVE':      (),
    'IEM_CIMPL_F_BRANCH_CONDITIONAL':   (),
    'IEM_CIMPL_F_BRANCH_FAR':           (),
    'IEM_CIMPL_F_BRANCH_ANY':           ('IEM_CIMPL_F_BRANCH_DIRECT', 'IEM_CIMPL_F_BRANCH_INDIRECT',
                                         'IEM_CIMPL_F_BRANCH_RELATIVE',),
    'IEM_CIMPL_F_BRANCH_STACK':         (),
    'IEM_CIMPL_F_BRANCH_STACK_FAR':     (),
    'IEM_CIMPL_F_MODE':                 (),
    'IEM_CIMPL_F_RFLAGS':               (),
    'IEM_CIMPL_F_INHIBIT_SHADOW':       (),
    'IEM_CIMPL_F_STATUS_FLAGS':         (),
    'IEM_CIMPL_F_CHECK_IRQ_AFTER':      (),
    'IEM_CIMPL_F_CHECK_IRQ_BEFORE':     (),
    'IEM_CIMPL_F_CHECK_IRQ_BEFORE_AND_AFTER': ('IEM_CIMPL_F_CHECK_IRQ_BEFORE', 'IEM_CIMPL_F_CHECK_IRQ_AFTER',),
    'IEM_CIMPL_F_VMEXIT':               (),
    'IEM_CIMPL_F_FPU':                  (),
    'IEM_CIMPL_F_REP':                  (),
    'IEM_CIMPL_F_IO':                   (),
    'IEM_CIMPL_F_END_TB':               (),
    'IEM_CIMPL_F_XCPT':                 ('IEM_CIMPL_F_BRANCH_INDIRECT', 'IEM_CIMPL_F_BRANCH_FAR',
                                         'IEM_CIMPL_F_MODE', 'IEM_CIMPL_F_RFLAGS', 'IEM_CIMPL_F_VMEXIT', ),
    'IEM_CIMPL_F_CALLS_CIMPL':              (),
    'IEM_CIMPL_F_CALLS_AIMPL':              (),
    'IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE': (),
};
class McBlock(object):
    """
    Microcode block (IEM_MC_BEGIN ... IEM_MC_END, IEM_MC_DEFER_TO_CIMPL_x_RET).
    """

    ## @name Macro expansion types.
    ## @{
    kiMacroExp_None    = 0;
    kiMacroExp_Entire  = 1; ##< Entire block (iBeginLine == iEndLine), original line may contain multiple blocks.
    kiMacroExp_Partial = 2; ##< Partial/mixed (cmpxchg16b), safe to assume single block.
    ## @}

    def __init__(self, sSrcFile, iBeginLine, offBeginLine, oFunction, iInFunction,
                 oInstruction = None, cchIndent = None, fDeferToCImpl = False):
        ## Set if IEM_MC_DEFER_TO_CIMPL_0_RET and friends, clear if IEM_MC_BEGIN/END block.
        self.fDeferToCImpl = fDeferToCImpl;
        ## The source file containing the block.
        self.sSrcFile     = sSrcFile;
        ## The line with the IEM_MC_BEGIN/IEM_MC_DEFER_TO_CIMPL_X_RET statement.
        self.iBeginLine   = iBeginLine;
        ## The offset of the IEM_MC_BEGIN/IEM_MC_DEFER_TO_CIMPL_X_RET statement within the line.
        self.offBeginLine = offBeginLine;
        ## The line with the IEM_MC_END statement / last line of IEM_MC_DEFER_TO_CIMPL_X_RET.
        self.iEndLine     = -1;
        ## The offset of the IEM_MC_END statement within the line / semicolon offset for defer-to.
        self.offEndLine   = 0;
        ## The offset following the IEM_MC_END/IEM_MC_DEFER_TO_CIMPL_X_RET semicolon.
        self.offAfterEnd  = 0;
        ## The function the block resides in.
        self.oFunction    = oFunction;
        ## The name of the function the block resides in. DEPRECATED.
        self.sFunction    = oFunction.sName;
        ## The block number within the function.
        self.iInFunction  = iInFunction;
        ## The instruction this block is associated with - can be None.
        self.oInstruction = oInstruction    # type: Instruction
        ## Indentation level of the block.
        self.cchIndent    = cchIndent if cchIndent else offBeginLine;
        ## The raw lines the block is made up of.
        self.asLines      = []              # type: List[str]
        ## Indicates whether the block includes macro expansion parts (kiMacroExp_None,
        ## kiMacroExp_Entrie, kiMacroExp_Partial).
        self.iMacroExp    = self.kiMacroExp_None;
        ## IEM_MC_ARG, IEM_MC_ARG_CONST, IEM_MC_ARG_LOCAL_REF, IEM_MC_ARG_LOCAL_EFLAGS.
        self.aoArgs       = []              # type: List[McStmtArg]
        ## IEM_MC_LOCAL, IEM_MC_LOCAL_CONST, IEM_MC_ARG_LOCAL_EFLAGS.
        self.aoLocals     = []              # type: List[McStmtVar]
        ## IEM_MC_BEGIN: IEM_MC_F_XXX dictionary
        self.dsMcFlags    = {}              # type: Dict[str, bool]
        ## IEM_MC_[DEFER_TO|CALL]_CIMPL_XXX: IEM_CIMPL_F_XXX dictionary
        self.dsCImplFlags = {}              # type: Dict[str, bool]
        ## Decoded statements in the block.
        self.aoStmts      = []              # type: List[McStmt]

    def complete(self, iEndLine, offEndLine, offAfterEnd, asLines):
        """
        Completes the microcode block.
        """
        assert self.iEndLine == -1;
        self.iEndLine     = iEndLine;
        self.offEndLine   = offEndLine;
        self.offAfterEnd  = offAfterEnd;
        self.asLines      = asLines;

    def raiseDecodeError(self, sRawCode, off, sMessage):
        """ Raises a decoding error. """
        offStartOfLine = sRawCode.rfind('\n', 0, off) + 1;
        iLine          = sRawCode.count('\n', 0, off);
        raise ParserException('%s:%d:%d: parsing error: %s'
                              % (self.sSrcFile, self.iBeginLine + iLine, off - offStartOfLine + 1, sMessage,));

    def raiseStmtError(self, sName, sMessage):
        """ Raises a statement parser error. """
        raise ParserException('%s:%d: %s: parsing error: %s' % (self.sSrcFile, self.iBeginLine, sName, sMessage,));

    def checkStmtParamCount(self, sName, asParams, cParamsExpected):
        """ Check the parameter count, raising an error it doesn't match. """
        if len(asParams) != cParamsExpected:
            raise ParserException('%s:%d: %s: Expected %s parameters, found %s!'
                                  % (self.sSrcFile, self.iBeginLine, sName, cParamsExpected, len(asParams),));
        return True;

    @staticmethod
    def parseMcGeneric(oSelf, sName, asParams):
        """ Generic parser that returns a plain McStmt object. """
        _ = oSelf;
        return McStmt(sName, asParams);

    @staticmethod
    def parseMcGenericCond(oSelf, sName, asParams):
        """ Generic parser that returns a plain McStmtCond object. """
        _ = oSelf;
        return McStmtCond(sName, asParams);

    kdArchVals = {
        'RT_ARCH_VAL_X86':     True,
        'RT_ARCH_VAL_AMD64':   True,
        'RT_ARCH_VAL_ARM32':   True,
        'RT_ARCH_VAL_ARM64':   True,
        'RT_ARCH_VAL_SPARC32': True,
        'RT_ARCH_VAL_SPARC64': True,
    };

    @staticmethod
    def parseMcNativeIf(oSelf, sName, asParams):
        """ IEM_MC_NATIVE_IF """
        oSelf.checkStmtParamCount(sName, asParams, 1);
        if asParams[0].strip() == '0':
            asArchitectures = [];
        else:
            asArchitectures = [sArch.strip() for sArch in asParams[0].split('|')];
            for sArch in asArchitectures:
                if sArch not in oSelf.kdArchVals:
                    oSelf.raiseStmtError(sName, 'Unknown architecture: %s' % (sArch,));
        return McStmtNativeIf(sName, asArchitectures);

    @staticmethod
    def parseMcBegin(oSelf, sName, asParams):
        """ IEM_MC_BEGIN """
        oSelf.checkStmtParamCount(sName, asParams, 2);
        if oSelf.dsMcFlags:
            oSelf.raiseStmtError(sName, 'Used more than once!');

        if asParams[0] != '0':
            for sFlag in asParams[0].split('|'):
                sFlag = sFlag.strip();
                if sFlag not in g_kdMcFlags:
                    oSelf.raiseStmtError(sName, 'Unknown flag: %s' % (sFlag, ));
                oSelf.dsMcFlags[sFlag] = True;
                for sFlag2 in g_kdMcFlags[sFlag]:
                    oSelf.dsMcFlags[sFlag2] = True;

        if asParams[1] != '0':
            oSelf.parseCImplFlags(sName, asParams[1]);

        return McBlock.parseMcGeneric(oSelf, sName, asParams);

    @staticmethod
    def parseMcArg(oSelf, sName, asParams):
        """ IEM_MC_ARG """
        oSelf.checkStmtParamCount(sName, asParams, 3);
        oStmt = McStmtArg(sName, asParams, asParams[0], asParams[1], int(asParams[2]));
        oSelf.aoArgs.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcArgConst(oSelf, sName, asParams):
        """ IEM_MC_ARG_CONST """
        oSelf.checkStmtParamCount(sName, asParams, 4);
        oStmt = McStmtArg(sName, asParams, asParams[0], asParams[1], int(asParams[3]), sConstValue = asParams[2]);
        oSelf.aoArgs.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcArgLocalRef(oSelf, sName, asParams):
        """ IEM_MC_ARG_LOCAL_REF """
        oSelf.checkStmtParamCount(sName, asParams, 4);
        oStmt = McStmtArg(sName, asParams, asParams[0], asParams[1], int(asParams[3]), sRef = asParams[2], sRefType = 'local');
        oSelf.aoArgs.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcArgEFlags(oSelf, sName, asParams):
        """ IEM_MC_ARG_EFLAGS """
        oSelf.checkStmtParamCount(sName, asParams, 2);
        # Note! We split this one up into IEM_MC_ARG and IEM_MC_FETCH_EFLAGS.
        oStmtArg   = McStmtArg('IEM_MC_ARG', ['uint32_t', asParams[0], asParams[1]], 'uint32_t', asParams[0], int(asParams[1]));
        oSelf.aoArgs.append(oStmtArg);
        oStmtFetch = McStmt('IEM_MC_FETCH_EFLAGS', [asParams[0]]);
        return (oStmtArg, oStmtFetch,);

    @staticmethod
    def parseMcArgLocalEFlags(oSelf, sName, asParams):
        """ IEM_MC_ARG_LOCAL_EFLAGS """
        oSelf.checkStmtParamCount(sName, asParams, 3);
        # Note! We split this one up into IEM_MC_LOCAL_VAR, IEM_MC_ARG_LOCAL_REF and IEM_MC_FETCH_EFLAGS.
        oStmtLocal = McStmtVar('IEM_MC_LOCAL', ['uint32_t', asParams[1],], 'uint32_t', asParams[1]);
        oSelf.aoLocals.append(oStmtLocal);
        oStmtArg   = McStmtArg('IEM_MC_ARG_LOCAL_REF', ['uint32_t *', asParams[0], asParams[1], asParams[2]],
                               'uint32_t *', asParams[0], int(asParams[2]), sRef = asParams[1], sRefType = 'local');
        oSelf.aoArgs.append(oStmtArg);
        oStmtFetch = McStmt('IEM_MC_FETCH_EFLAGS', [asParams[1]]);
        return (oStmtLocal, oStmtArg, oStmtFetch,);

    @staticmethod
    def parseMcLocal(oSelf, sName, asParams):
        """ IEM_MC_LOCAL """
        oSelf.checkStmtParamCount(sName, asParams, 2);
        oStmt = McStmtVar(sName, asParams, asParams[0], asParams[1]);
        oSelf.aoLocals.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcLocalAssign(oSelf, sName, asParams):
        """ IEM_MC_LOCAL_ASSIGN """
        oSelf.checkStmtParamCount(sName, asParams, 3);
        oStmt = McStmtVar(sName, asParams, asParams[0], asParams[1], sValue = asParams[2]);
        oSelf.aoLocals.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcLocalConst(oSelf, sName, asParams):
        """ IEM_MC_LOCAL_CONST """
        oSelf.checkStmtParamCount(sName, asParams, 3);
        oStmt = McStmtVar(sName, asParams, asParams[0], asParams[1], sValue = asParams[2]);
        oSelf.aoLocals.append(oStmt);
        return oStmt;

    @staticmethod
    def parseMcLocalEFlags(oSelf, sName, asParams):
        """ IEM_MC_LOCAL_EFLAGS"""
        oSelf.checkStmtParamCount(sName, asParams, 1);
        # Note! We split this one up into IEM_MC_LOCAL_VAR and IEM_MC_FETCH_EFLAGS just like with IEM_MC_ARG_LOCAL_EFLAGS.
        oStmtLocal = McStmtVar('IEM_MC_LOCAL', ['uint32_t', asParams[0]], 'uint32_t', asParams[0]);
        oSelf.aoLocals.append(oStmtLocal);
        oStmtFetch = McStmt('IEM_MC_FETCH_EFLAGS', [asParams[0]]);
        return (oStmtLocal, oStmtFetch,);

    @staticmethod
    def parseMcCallAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_AIMPL_3|4 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 3 + cArgs);
        oSelf.aoLocals.append(McStmtVar('IEM_MC_LOCAL', [asParams[0], asParams[1]], asParams[0], asParams[1]));
        return McStmtCall(sName, asParams, 2, 1);

    @staticmethod
    def parseMcCallVoidAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_VOID_AIMPL_2|3 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 1 + cArgs);
        return McStmtCall(sName, asParams, 0);

    @staticmethod
    def parseMcCallAvxAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_AVX_AIMPL_2|3 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 1 + cArgs);
        return McStmtCall(sName, asParams, 0);

    @staticmethod
    def parseMcCallFpuAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_FPU_AIMPL_1|2|3 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 1 + cArgs);
        return McStmtCall(sName, asParams, 0);

    @staticmethod
    def parseMcCallMmxAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_MMX_AIMPL_2|3 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 1 + cArgs);
        return McStmtCall(sName, asParams, 0);

    @staticmethod
    def parseMcCallSseAImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_SSE_AIMPL_2|3 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 1 + cArgs);
        return McStmtCall(sName, asParams, 0);

    def parseCImplFlags(self, sName, sFlags):
        """
        Helper for parseMcCallCImpl and parseMcDeferToCImpl to validate and
        merge a bunch of IEM_CIMPL_F_XXX value into dsCImplFlags.
        """
        if sFlags != '0':
            sFlags = self.stripComments(sFlags);
            #print('debug: %s: %s' % (self.oFunction.sName,' | '.join(''.join(sFlags.split()).split('|')),));
            for sFlag in sFlags.split('|'):
                sFlag = sFlag.strip();
                if sFlag[0] == '(':  sFlag = sFlag[1:].strip();
                if sFlag[-1] == ')': sFlag = sFlag[:-1].strip();
                #print('debug:   %s' % sFlag)
                if sFlag not in g_kdCImplFlags:
                    if sFlag == '0':
                        continue;
                    self.raiseStmtError(sName, 'Unknown flag: %s' % (sFlag, ));
                self.dsCImplFlags[sFlag] = True;
                for sFlag2 in g_kdCImplFlags[sFlag]:
                    self.dsCImplFlags[sFlag2] = True;
        return None;

    @staticmethod
    def parseMcCallCImpl(oSelf, sName, asParams):
        """ IEM_MC_CALL_CIMPL_0|1|2|3|4|5 """
        cArgs = int(sName[-1]);
        oSelf.checkStmtParamCount(sName, asParams, 3 + cArgs);
        oSelf.parseCImplFlags(sName, asParams[0]);
        return McStmtCall(sName, asParams, 2);

    @staticmethod
    def parseMcDeferToCImpl(oSelf, sName, asParams):
        """ IEM_MC_DEFER_TO_CIMPL_[0|1|2|3]_RET """
        # Note! This code is called by workerIemMcDeferToCImplXRet.
        #print('debug: %s, %s,...' % (sName, asParams[0],));
        cArgs = int(sName[-5]);
        oSelf.checkStmtParamCount(sName, asParams, 3 + cArgs);
        oSelf.parseCImplFlags(sName, asParams[0]);
        return McStmtCall(sName, asParams, 2);

    @staticmethod
    def stripComments(sCode):
        """ Returns sCode with comments removed. """
        off = 0;
        while off < len(sCode):
            off = sCode.find('/', off);
            if off < 0 or off + 1 >= len(sCode):
                break;

            if sCode[off + 1] == '/':
                # C++ comment.
                offEnd = sCode.find('\n', off + 2);
                if offEnd < 0:
                    return sCode[:off].rstrip();
                sCode = sCode[ : off] + sCode[offEnd : ];
                off += 1;

            elif sCode[off + 1] == '*':
                # C comment
                offEnd = sCode.find('*/', off + 2);
                if offEnd < 0:
                    return sCode[:off].rstrip();
                sSep = ' ';
                if (off > 0 and sCode[off - 1].isspace())  or  (offEnd + 2 < len(sCode) and sCode[offEnd + 2].isspace()):
                    sSep = '';
                sCode = sCode[ : off] + sSep + sCode[offEnd + 2 : ];
                off += len(sSep);

            else:
                # Not a comment.
                off += 1;
        return sCode;

    @staticmethod
    def extractParam(sCode, offParam):
        """
        Extracts the parameter value at offParam in sCode.
        Returns stripped value and the end offset of the terminating ',' or ')'.
        """
        # Extract it.
        cNesting = 0;
        offStart = offParam;
        while offParam < len(sCode):
            ch = sCode[offParam];
            if ch == '(':
                cNesting += 1;
            elif ch == ')':
                if cNesting == 0:
                    break;
                cNesting -= 1;
            elif ch == ',' and cNesting == 0:
                break;
            offParam += 1;
        return (sCode[offStart : offParam].strip(), offParam);

    @staticmethod
    def extractParams(sCode, offOpenParen):
        """
        Parses a parameter list.
        Returns the list of parameter values and the offset of the closing parentheses.
        Returns (None, len(sCode)) on if no closing parentheses was found.
        """
        assert sCode[offOpenParen] == '(';
        asParams = [];
        off      = offOpenParen + 1;
        while off < len(sCode):
            ch = sCode[off];
            if ch.isspace():
                off += 1;
            elif ch != ')':
                (sParam, off) = McBlock.extractParam(sCode, off);
                asParams.append(sParam);
                assert off < len(sCode), 'off=%s sCode=%s:"%s"' % (off, len(sCode), sCode,);
                if sCode[off] == ',':
                    off += 1;
            else:
                return (asParams, off);
        return (None, off);

    @staticmethod
    def findClosingBraces(sCode, off, offStop):
        """
        Finds the matching '}' for the '{' at off in sCode.
        Returns offset of the matching '}' on success, otherwise -1.

        Note! Does not take comments into account.
        """
        cDepth = 1;
        off   += 1;
        while off < offStop:
            offClose = sCode.find('}', off, offStop);
            if offClose < 0:
                break;
            cDepth += sCode.count('{', off, offClose);
            cDepth -= 1;
            if cDepth == 0:
                return offClose;
            off = offClose + 1;
        return -1;

    @staticmethod
    def countSpacesAt(sCode, off, offStop):
        """ Returns the number of space characters at off in sCode. """
        offStart = off;
        while off < offStop and sCode[off].isspace():
            off += 1;
        return off - offStart;

    @staticmethod
    def skipSpacesAt(sCode, off, offStop):
        """ Returns first offset at or after off for a non-space character. """
        return off + McBlock.countSpacesAt(sCode, off, offStop);

    @staticmethod
    def isSubstrAt(sStr, off, sSubStr):
        """ Returns true of sSubStr is found at off in sStr. """
        return sStr[off : off + len(sSubStr)] == sSubStr;

    koReCppCtrlStmts   = re.compile(r'\b(if\s*[(]|else\b|while\s*[(]|for\s*[(]|do\b)');
    koReIemDecoderVars = re.compile(  r'iem\.s\.(fPrefixes|uRexReg|uRexB|uRexIndex|iEffSeg|offModRm|cbOpcode|offOpcode'
                                    + r'|enmEffOpSize|enmDefOpSize|enmDefAddrMode|enmEffAddrMode|idxPrefix'
                                    + r'|uVex3rdReg|uVexLength|fEvxStuff|uFpuOpcode|abOpcode'
                                    + r')');

    kaasConditions = (
        ( 'IEM_MC_IF_',       'IEM_MC_ELSE',        'IEM_MC_ENDIF' ),
        ( 'IEM_MC_NATIVE_IF', 'IEM_MC_NATIVE_ELSE', 'IEM_MC_NATIVE_ENDIF' ),
    );
    def decodeCode(self, sRawCode, off = 0, offStop = -1, iLevel = 0): # pylint: disable=too-many-statements,too-many-branches
        """
        Decodes sRawCode[off : offStop].

        Returns list of McStmt instances.
        Raises ParserException on failure.
        """
        if offStop < 0:
            offStop = len(sRawCode);
        aoStmts = [];
        while off < offStop:
            ch = sRawCode[off];

            #
            # Skip spaces and comments.
            #
            if ch.isspace():
                off += 1;

            elif ch == '/':
                ch = sRawCode[off + 1];
                if ch == '/': # C++ comment.
                    off = sRawCode.find('\n', off + 2);
                    if off < 0:
                        break;
                    off += 1;
                elif ch == '*': # C comment.
                    off = sRawCode.find('*/', off + 2);
                    if off < 0:
                        break;
                    off += 2;
                else:
                    self.raiseDecodeError(sRawCode, off, 'Unexpected "/"');

            #
            # Is it a MC statement.
            #
            elif ch == 'I' and sRawCode[off : off + len('IEM_MC_')] == 'IEM_MC_':
                # All MC statements ends with a semicolon, except for conditionals which ends with a '{'.
                # Extract it and strip comments from it.
                if   self.isSubstrAt(sRawCode, off, self.kaasConditions[0][0]): iCond = 0;
                elif self.isSubstrAt(sRawCode, off, self.kaasConditions[1][0]): iCond = 1;
                else:                                                           iCond = -1;
                if iCond < 0:
                    offEnd = sRawCode.find(';', off + len('IEM_MC_'));
                    if offEnd <= off:
                        self.raiseDecodeError(sRawCode, off, 'MC statement without a ";"');
                else:
                    offEnd = sRawCode.find('{', off + len(self.kaasConditions[iCond][0]));
                    if offEnd <= off:
                        self.raiseDecodeError(sRawCode, off, 'MC conditional statement without a "{"');
                    if sRawCode.find(';', off + len(self.kaasConditions[iCond][0]), offEnd) > off:
                        self.raiseDecodeError(sRawCode, off, 'MC conditional statement without an immediate "{"');
                    offEnd -= 1;
                    while offEnd > off and sRawCode[offEnd - 1].isspace():
                        offEnd -= 1;

                sRawStmt = self.stripComments(sRawCode[off : offEnd]);

                # Isolate the statement name.
                offOpenParen = sRawStmt.find('(');
                if offOpenParen < 0:
                    self.raiseDecodeError(sRawCode, off, 'MC statement without a "("');
                sName = sRawStmt[: offOpenParen].strip();

                # Extract the parameters.
                (asParams, offCloseParen) = self.extractParams(sRawStmt, offOpenParen);
                if asParams is None:
                    self.raiseDecodeError(sRawCode, off, 'MC statement without a closing parenthesis');
                if offCloseParen + 1 != len(sRawStmt):
                    self.raiseDecodeError(sRawCode, off,
                                          'Unexpected code following MC statement: %s' % (sRawStmt[offCloseParen + 1:]));

                # Hand it to the handler.
                fnParser = g_dMcStmtParsers.get(sName);
                if not fnParser:
                    self.raiseDecodeError(sRawCode, off, 'Unknown MC statement: %s' % (sName,));
                fnParser = fnParser[0];
                oStmt = fnParser(self, sName, asParams);
                if not isinstance(oStmt, (list, tuple)):
                    aoStmts.append(oStmt);
                else:
                    aoStmts.extend(oStmt);

                #
                # If conditional, we need to parse the whole statement.
                #
                # For reasons of simplicity, we assume the following structure
                # and parse each branch in a recursive call:
                #   IEM_MC_IF_XXX() {
                #        IEM_MC_WHATEVER();
                #   } IEM_MC_ELSE() {
                #        IEM_MC_WHATEVER();
                #   } IEM_MC_ENDIF();
                #
                if iCond >= 0:
                    if iLevel > 1: ## @todo discount IEM_MC_NATIVE_IF.
                        self.raiseDecodeError(sRawCode, off, 'Too deep nesting of conditionals.');

                    # Find start of the IF block:
                    offBlock1 = self.skipSpacesAt(sRawCode, offEnd, offStop);
                    if sRawCode[offBlock1] != '{':
                        self.raiseDecodeError(sRawCode, offBlock1, 'Expected "{" following %s' % (sName,));

                    # Find the end of it.
                    offBlock1End = self.findClosingBraces(sRawCode, offBlock1, offStop);
                    if offBlock1End < 0:
                        self.raiseDecodeError(sRawCode, offBlock1, 'No matching "}" closing IF block of %s' % (sName,));

                    oStmt.aoIfBranch = self.decodeCode(sRawCode, offBlock1 + 1, offBlock1End, iLevel + 1);

                    # Is there an else section?
                    off = self.skipSpacesAt(sRawCode, offBlock1End + 1, offStop);
                    sElseNm = self.kaasConditions[iCond][1];
                    if self.isSubstrAt(sRawCode, off, sElseNm):
                        off = self.skipSpacesAt(sRawCode, off + len(sElseNm), offStop);
                        if sRawCode[off] != '(':
                            self.raiseDecodeError(sRawCode, off, 'Expected "(" following %s"' % (sElseNm,));
                        off = self.skipSpacesAt(sRawCode, off + 1, offStop);
                        if sRawCode[off] != ')':
                            self.raiseDecodeError(sRawCode, off, 'Expected ")" following %s("' % (sElseNm,));

                        # Find start of the ELSE block.
                        offBlock2 = self.skipSpacesAt(sRawCode, off + 1, offStop);
                        if sRawCode[offBlock2] != '{':
                            self.raiseDecodeError(sRawCode, offBlock2, 'Expected "{" following %s()"' % (sElseNm,));

                        # Find the end of it.
                        offBlock2End = self.findClosingBraces(sRawCode, offBlock2, offStop);
                        if offBlock2End < 0:
                            self.raiseDecodeError(sRawCode, offBlock2, 'No matching "}" closing ELSE block of %s' % (sName,));

                        oStmt.aoElseBranch = self.decodeCode(sRawCode, offBlock2 + 1, offBlock2End, iLevel + 1);
                        off = self.skipSpacesAt(sRawCode, offBlock2End + 1, offStop);

                    # Parse past the endif statement.
                    sEndIfNm = self.kaasConditions[iCond][2];
                    if not self.isSubstrAt(sRawCode, off, sEndIfNm):
                        self.raiseDecodeError(sRawCode, off, 'Expected %s for closing %s' % (sEndIfNm, sName,));
                    off = self.skipSpacesAt(sRawCode, off + len(sEndIfNm), offStop);
                    if sRawCode[off] != '(':
                        self.raiseDecodeError(sRawCode, off, 'Expected "(" following %s"' % (sEndIfNm,));
                    off = self.skipSpacesAt(sRawCode, off + 1, offStop);
                    if sRawCode[off] != ')':
                        self.raiseDecodeError(sRawCode, off, 'Expected ")" following %s("' % (sEndIfNm,));
                    off = self.skipSpacesAt(sRawCode, off + 1, offStop);
                    if sRawCode[off] != ';':
                        self.raiseDecodeError(sRawCode, off, 'Expected ";" following %s()"' % (sEndIfNm,));
                    off += 1;

                else:
                    # Advance.
                    off = offEnd + 1;

            #
            # Otherwise it must be a C/C++ statement of sorts.
            #
            else:
                # Find the end of the statement.  if and else requires special handling.
                sCondExpr = None;
                oMatch = self.koReCppCtrlStmts.match(sRawCode, off);
                if oMatch:
                    if oMatch.group(1)[-1] == '(':
                        (sCondExpr, offEnd) = self.extractParam(sRawCode, oMatch.end());
                    else:
                        offEnd = oMatch.end();
                    if not oMatch.group(1).startswith('if') and oMatch.group(1) != 'else':
                        self.raiseDecodeError(sRawCode, off, 'Only if/else control statements allowed: %s' % (oMatch.group(1),));
                elif ch == '#':
                    offEnd = sRawCode.find('\n', off, offStop);
                    if offEnd < 0:
                        offEnd = offStop;
                    offEnd -= 1;
                    while offEnd > off and sRawCode[offEnd - 1].isspace():
                        offEnd -= 1;
                else:
                    offEnd = sRawCode.find(';', off);
                    if offEnd < 0:
                        self.raiseDecodeError(sRawCode, off, 'C++ statement without a ";"');

                # Check this and the following statement whether it might have
                # something to do with decoding.  This is a statement filter
                # criteria when generating the threaded functions blocks.
                offNextEnd = sRawCode.find(';', offEnd + 1);
                fDecode    = (   sRawCode.find('IEM_OPCODE_', off, max(offEnd, offNextEnd)) >= 0
                              or sRawCode.find('IEMOP_HLP_DONE_', off, max(offEnd, offNextEnd)) >= 0
                              or sRawCode.find('IEMOP_HLP_DECODED_', off, offEnd) >= 0
                              or sRawCode.find('IEMOP_HLP_RAISE_UD_IF_MISSING_GUEST_FEATURE', off, offEnd) >= 0
                              or sRawCode.find('IEMOP_HLP_VMX_INSTR', off, offEnd) >= 0
                              or sRawCode.find('IEMOP_HLP_IN_VMX_OPERATION', off, offEnd) >= 0 ## @todo wrong
                           );

                if not oMatch:
                    if ch != '#':
                        aoStmts.append(McCppGeneric(sRawCode[off : offEnd + 1], fDecode));
                    else:
                        aoStmts.append(McCppPreProc(sRawCode[off : offEnd + 1]));
                    off = offEnd + 1;
                elif oMatch.group(1).startswith('if'):
                    #
                    # if () xxx [else yyy] statement.
                    #
                    oStmt = McCppCond(sCondExpr, fDecode);
                    aoStmts.append(oStmt);
                    off = offEnd + 1;

                    # Following the if () we can either have a {} containing zero or more statements
                    # or we have a single statement.
                    offBlock1 = self.skipSpacesAt(sRawCode, offEnd + 1, offStop);
                    if sRawCode[offBlock1] == '{':
                        offBlock1End = self.findClosingBraces(sRawCode, offBlock1, offStop);
                        if offBlock1End < 0:
                            self.raiseDecodeError(sRawCode, offBlock1, 'No matching "}" closing if block');
                        offBlock1 += 1;
                    else:
                        offBlock1End = sRawCode.find(';', offBlock1, offStop);
                        if offBlock1End < 0:
                            self.raiseDecodeError(sRawCode, off, 'Expected ";" terminating one-line if block"');

                    oStmt.aoIfBranch = self.decodeCode(sRawCode, offBlock1, offBlock1End, iLevel + 1);

                    # The else is optional and can likewise be followed by {} or a single statement.
                    off = self.skipSpacesAt(sRawCode, offBlock1End + 1, offStop);
                    if self.isSubstrAt(sRawCode, off, 'else') and sRawCode[off + len('else')].isspace():
                        offBlock2 = self.skipSpacesAt(sRawCode, off + len('else'), offStop);
                        if sRawCode[offBlock2] == '{':
                            offBlock2End = self.findClosingBraces(sRawCode, offBlock2, offStop);
                            if offBlock2End < 0:
                                self.raiseDecodeError(sRawCode, offBlock2, 'No matching "}" closing else block');
                            offBlock2 += 1;
                        else:
                            offBlock2End = sRawCode.find(';', offBlock2, offStop);
                            if offBlock2End < 0:
                                self.raiseDecodeError(sRawCode, off, 'Expected ";" terminating one-line else block"');

                        oStmt.aoElseBranch = self.decodeCode(sRawCode, offBlock2, offBlock2End, iLevel + 1);
                        off = offBlock2End + 1;

                elif oMatch.group(1) == 'else':
                    # Problematic 'else' branch, typically involving #ifdefs.
                    self.raiseDecodeError(sRawCode, off, 'Mixed up else/#ifdef or something confusing us.');

        return aoStmts;

    def decode(self):
        """
        Decodes the block, populating self.aoStmts if necessary.
        Returns the statement list.
        Raises ParserException on failure.
        """
        if not self.aoStmts:
            self.aoStmts = self.decodeCode(''.join(self.asLines));
        return self.aoStmts;


    def checkForTooEarlyEffSegUse(self, aoStmts):
        """
        Checks if iEffSeg is used before the effective address has been decoded.
        Returns None on success, error string on failure.

        See r158454 for an example of this issue.
        """

        # Locate the IEM_MC_CALC_RM_EFF_ADDR statement, if found, scan backwards
        # for IEMCPU::iEffSeg references. No need to check conditional branches,
        # as we're ASSUMING these will not occur before address calculation.
        for iStmt, oStmt in enumerate(aoStmts):
            if oStmt.sName == 'IEM_MC_CALC_RM_EFF_ADDR':
                while iStmt > 0:
                    iStmt -= 1;
                    oStmt  = aoStmts[iStmt];
                    for sArg in oStmt.asParams:
                        if sArg.find('pVCpu->iem.s.iEffSeg') >= 0:
                            return "statement #%u: pVCpu->iem.s.iEffSeg is used prior to IEM_MC_CALC_RM_EFF_ADDR!" % (iStmt + 1,);
                break;
        return None;

    koReCppFirstWord = re.compile(r'^\s*(\w+)[ (;]');
    kdDecodeCppStmtOkayAfterDone = {
        'IEMOP_HLP_IN_VMX_OPERATION': True,
        'IEMOP_HLP_VMX_INSTR':        True,
    };

    def checkForDoneDecoding(self, aoStmts):
        """
        Checks that the block contains a IEMOP_HLP_DONE_*DECODING* macro
        invocation.
        Returns None on success, error string on failure.

        This ensures safe instruction restarting in case the recompiler runs
        out of TB resources during recompilation (e.g. aRanges or aGCPhysPages
        entries).
        """

        # The IEMOP_HLP_DONE_ stuff is not allowed inside conditionals, so we
        # don't need to look.
        cIemOpHlpDone = 0;
        for iStmt, oStmt in enumerate(aoStmts):
            if oStmt.isCppStmt():
                #print('dbg: #%u[%u]: %s %s (%s)'
                #      % (iStmt + 1, cIemOpHlpDone, oStmt.sName, 'd' if oStmt.fDecode else 'r', oStmt.asParams[0],));

                oMatch = self.koReCppFirstWord.match(oStmt.asParams[0]);
                if oMatch:
                    sFirstWord = oMatch.group(1);
                    if (   sFirstWord.startswith('IEMOP_HLP_DONE_')
                        or sFirstWord.startswith('IEMOP_HLP_DECODED_')):
                        cIemOpHlpDone += 1;
                    elif cIemOpHlpDone > 0 and oStmt.fDecode and sFirstWord not in self.kdDecodeCppStmtOkayAfterDone:
                        return "statement #%u: Decoding statement following IEMOP_HLP_DONE_*DECODING*!" % (iStmt + 1,);
                #else: print('dbg: #%u[%u]: %s' % (iStmt + 1, cIemOpHlpDone, oStmt.asParams[0]));
            else:
                #print('dbg: #%u[%u]: %s' % (iStmt + 1, cIemOpHlpDone, oStmt.sName));
                if oStmt.sName.startswith('IEM_MC_DEFER_TO_CIMPL_') and iStmt == 0: # implicit
                    cIemOpHlpDone += 1;
                elif cIemOpHlpDone == 0 and g_dMcStmtParsers.get(oStmt.sName, (None, False))[1]:
                    return "statement #%u: State modifying MC statement before IEMOP_HLP_DONE_*DECODING*!" % (iStmt + 1,);
                elif cIemOpHlpDone > 0  and oStmt.sName in ('IEM_MC_CALC_RM_EFF_ADDR',):
                    return "statement #%u: Decoding statement following IEMOP_HLP_DONE_*DECODING*!" % (iStmt + 1,);
        if cIemOpHlpDone == 1:
            return None;
        if cIemOpHlpDone > 1:
            return "Block has more than one IEMOP_HLP_DONE_*DECODING* invocation!";
        return "Block is missing IEMOP_HLP_DONE_*DECODING* invocation!";

    def checkForFetchAfterRef(self, aoStmts, asRegRefClasses):
        """
        Checks that the register references are placed after register fetches
        from the same register class.
        Returns None on success, error string on failure.

        Example:
                SHL CH, CL

        If the CH reference is created first, the fetching of CL will cause the
        RCX guest register to have an active shadow register when it's being
        updated.  The shadow register will then be stale after the SHL operation
        completes, without us noticing.

        It's easier to ensure we've got correct code than complicating the
        recompiler code with safeguards here.
        """
        for iStmt, oStmt in enumerate(aoStmts):
            if not oStmt.isCppStmt():
                offRef = oStmt.sName.find("_REF_");
                if offRef > 0:
                    if oStmt.sName in ('IEM_MC_IF_FPUREG_NOT_EMPTY_REF_R80',
                                       'IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80',
                                       'IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80_FIRST',):
                        sClass = 'FPUREG';
                    else:
                        offUnderscore = oStmt.sName.find('_', offRef + 5);
                        if offUnderscore > 0:
                            assert offUnderscore > offRef;
                            sClass = oStmt.sName[offRef + 5 : offUnderscore];
                        else:
                            sClass = oStmt.sName[offRef + 5];
                    asRegRefClasses[sClass] = True;
                else:
                    offFetch = oStmt.sName.find("_FETCH_");
                    if offFetch > 0:
                        sClass = oStmt.sName[offFetch + 7 : ];
                        if not sClass.startswith("MEM"):
                            offUnderscore = sClass.find('_');
                            if offUnderscore >= 0:
                                assert offUnderscore > 0;
                                sClass = sClass[:offUnderscore];
                            if sClass in asRegRefClasses:
                                return "statement #%u: %s following REF! That'll mess up guest register shadowing" \
                                     % (iStmt + 1, oStmt.sName,);

            # Go into branches.
            if isinstance(oStmt, McStmtCond):
                sRet = self.checkForFetchAfterRef(oStmt.aoIfBranch, asRegRefClasses);
                if sRet:
                    return sRet;
                sRet = self.checkForFetchAfterRef(oStmt.aoElseBranch, asRegRefClasses);
                if sRet:
                    return sRet;
        return None;

    koReRefEflIllegalMemRough   = re.compile(r'^IEM_MC_.*(MEM|PUSH_U|POP_GREG|RETN|IND_CALL|REL_CALL)');
    koReRefEflIllegalMemExclude = re.compile(r'^IEM_MC_.*(MEM_COMMIT|MEM_OP|FPU_|UPDATE_FSW)');
    koReRefEflIllegalRaise      = re.compile(r'^IEM_MC_(RAISE|MAYBE_RAISE)');

    def checkRefEFlagsUse(self, aoStmts, asState):
        """
        Checks that EFLAGS references comes after memory fetches and that there
        are no memory stores or conditional raises afterwards.

        The problem is postponed EFLAGS calculation management.  This gets a
        lot easier if we can jettison any postponements when EFLAGS are
        referenced.  If we had to deal with potential TB exits / exceptions
        after they are referenced, this means it would have to delay the
        cleanup until the IEM_MC_..._AND_FINISH statement which is kind of
        complicated and not very efficient.
        """
        fSeenIt = asState.get('fSeenIt', False);
        for iStmt, oStmt in enumerate(aoStmts):
            if not oStmt.isCppStmt():
                if oStmt.sName in ('IEM_MC_REF_EFLAGS', 'IEM_MC_REF_EFLAGS_EX',):
                    fSeenIt = True;
                elif (    fSeenIt
                      and (    (    self.koReRefEflIllegalMemRough.match(oStmt.sName)
                                and not self.koReRefEflIllegalMemExclude.match(oStmt.sName))
                           or self.koReRefEflIllegalRaise.match(oStmt.sName) )):
                    return "statement #%u: %s following REF_EFLAGS! That'll mess up EFLAGS calculation postponing" \
                         % (iStmt + 1, oStmt.sName,);

            # Go into branches.
            if isinstance(oStmt, McStmtCond):
                asState['fSeenIt'] = fSeenIt;
                sRet = self.checkRefEFlagsUse(oStmt.aoIfBranch, asState);
                if sRet:
                    return sRet;
                sRet = self.checkRefEFlagsUse(oStmt.aoElseBranch, asState);
                if sRet:
                    return sRet;
                fSeenIt = asState['fSeenIt'];
        return None;

    def check(self):
        """
        Performs some sanity checks on the block.
        Returns error string list, empty if all is fine.
        """
        aoStmts = self.decode();
        asRet   = [];

        sRet = self.checkForTooEarlyEffSegUse(aoStmts);
        if sRet:
            asRet.append(sRet);

        sRet = self.checkForDoneDecoding(aoStmts);
        if sRet:
            asRet.append(sRet);

        sRet = self.checkForFetchAfterRef(aoStmts, {});
        if sRet:
            asRet.append(sRet);

        sRet = self.checkRefEFlagsUse(aoStmts, {});
        if sRet:
            asRet.append(sRet);

        return asRet;


## Temporary flag for enabling / disabling experimental MCs depending on the
## SIMD register allocator.
g_fNativeSimd = True;

## IEM_MC_XXX -> parser + info dictionary.
#
# The info columns:
#   - col 1+0: boolean entry indicating whether the statement modifies state and
#              must not be used before IEMOP_HL_DONE_*.
#   - col 1+1: boolean entry indicating similar to the previous column but is
#              used to decide when to emit calls for conditional jumps (Jmp/NoJmp).
#              The difference is that most IEM_MC_IF_XXX entries are False here.
#   - col 1+2: boolean entry indicating native recompiler support.
#
# The raw table was generated via the following command
#       sed -n -e "s/^# *define *\(IEM_MC_[A-Z_0-9]*\)[ (].*$/        '\1': McBlock.parseMcGeneric,/p" include/IEMMc.h \
#       | sort | uniq | gawk "{printf """    %%-60s (%%s,        True)\n""", $1, $2}"
# pylint: disable=line-too-long
g_dMcStmtParsers = {
    'IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE':                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ':                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE':                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ':                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE':                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ':                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ADD_GREG_U16':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADD_GREG_U16_TO_LOCAL':                              (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_ADD_GREG_U32':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADD_GREG_U32_TO_LOCAL':                              (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_ADD_GREG_U64':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADD_GREG_U64_TO_LOCAL':                              (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_ADD_GREG_U8_TO_LOCAL':                               (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR':                          (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR':                          (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR':                          (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_ADVANCE_RIP_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_AND_2LOCS_U32':                                      (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_AND_ARG_U16':                                        (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_ARG_U32':                                        (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_ARG_U64':                                        (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_GREG_U16':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_AND_GREG_U32':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_AND_GREG_U64':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_AND_GREG_U8':                                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_AND_LOCAL_U16':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_LOCAL_U32':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_LOCAL_U64':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_AND_LOCAL_U8':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_ARG':                                                (McBlock.parseMcArg,               False, False, True,  ),
    'IEM_MC_ARG_CONST':                                          (McBlock.parseMcArgConst,          False, False, True,  ),
    'IEM_MC_ARG_EFLAGS':                                         (McBlock.parseMcArgEFlags,         False, False, True,  ),
    'IEM_MC_ARG_LOCAL_EFLAGS':                                   (McBlock.parseMcArgLocalEFlags,    False, False, True,  ),
    'IEM_MC_ARG_LOCAL_REF':                                      (McBlock.parseMcArgLocalRef,       False, False, True,  ),
    'IEM_MC_ASSIGN_TO_SMALLER':                                  (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_BEGIN':                                              (McBlock.parseMcBegin,             False, False, True,  ),
    'IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX':                         (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX':                       (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX':                         (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_BSWAP_LOCAL_U16':                                    (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_BSWAP_LOCAL_U32':                                    (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_BSWAP_LOCAL_U64':                                    (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_CALC_RM_EFF_ADDR':                                   (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_CALL_AIMPL_3':                                       (McBlock.parseMcCallAImpl,         True,  True,  True,  ),
    'IEM_MC_CALL_AIMPL_4':                                       (McBlock.parseMcCallAImpl,         True,  True,  True,  ),
    'IEM_MC_CALL_AVX_AIMPL_2':                                   (McBlock.parseMcCallAvxAImpl,      True,  True,  g_fNativeSimd),
    'IEM_MC_CALL_AVX_AIMPL_3':                                   (McBlock.parseMcCallAvxAImpl,      True,  True,  g_fNativeSimd),
    'IEM_MC_CALL_CIMPL_0':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_CIMPL_1':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_CIMPL_2':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_CIMPL_3':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_CIMPL_4':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_CIMPL_5':                                       (McBlock.parseMcCallCImpl,         True,  True,  False, ),
    'IEM_MC_CALL_FPU_AIMPL_1':                                   (McBlock.parseMcCallFpuAImpl,      True,  True,  False, ),
    'IEM_MC_CALL_FPU_AIMPL_2':                                   (McBlock.parseMcCallFpuAImpl,      True,  True,  False, ),
    'IEM_MC_CALL_FPU_AIMPL_3':                                   (McBlock.parseMcCallFpuAImpl,      True,  True,  False, ),
    'IEM_MC_CALL_MMX_AIMPL_2':                                   (McBlock.parseMcCallMmxAImpl,      True,  True,  False, ),
    'IEM_MC_CALL_MMX_AIMPL_3':                                   (McBlock.parseMcCallMmxAImpl,      True,  True,  False, ),
    'IEM_MC_CALL_SSE_AIMPL_2':                                   (McBlock.parseMcCallSseAImpl,      True,  True,  g_fNativeSimd),
    'IEM_MC_CALL_SSE_AIMPL_3':                                   (McBlock.parseMcCallSseAImpl,      True,  True,  g_fNativeSimd),
    'IEM_MC_CALL_VOID_AIMPL_0':                                  (McBlock.parseMcCallVoidAImpl,     True,  True,  True,  ),
    'IEM_MC_CALL_VOID_AIMPL_1':                                  (McBlock.parseMcCallVoidAImpl,     True,  True,  True,  ),
    'IEM_MC_CALL_VOID_AIMPL_2':                                  (McBlock.parseMcCallVoidAImpl,     True,  True,  True,  ),
    'IEM_MC_CALL_VOID_AIMPL_3':                                  (McBlock.parseMcCallVoidAImpl,     True,  True,  True,  ),
    'IEM_MC_CALL_VOID_AIMPL_4':                                  (McBlock.parseMcCallVoidAImpl,     True,  True,  True,  ),
    'IEM_MC_CLEAR_EFL_BIT':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_CLEAR_FSW_EX':                                       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_CLEAR_HIGH_GREG_U64':                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_CLEAR_XREG_U32_MASK':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_CLEAR_YREG_128_UP':                                  (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_CLEAR_ZREG_256_UP':                                  (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_COMMIT_EFLAGS':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_COMMIT_EFLAGS_OPT':                                  (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_COPY_XREG_U128':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_COPY_YREG_U128_ZX_VLMAX':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_COPY_YREG_U256_ZX_VLMAX':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_COPY_YREG_U64_ZX_VLMAX':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_DEFER_TO_CIMPL_0_RET':                               (McBlock.parseMcDeferToCImpl,      False, False, False, ),
    'IEM_MC_DEFER_TO_CIMPL_1_RET':                               (McBlock.parseMcDeferToCImpl,      False, False, False, ),
    'IEM_MC_DEFER_TO_CIMPL_2_RET':                               (McBlock.parseMcDeferToCImpl,      False, False, False, ),
    'IEM_MC_DEFER_TO_CIMPL_3_RET':                               (McBlock.parseMcDeferToCImpl,      False, False, False, ),
    'IEM_MC_END':                                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_EFLAGS':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_EFLAGS_U8':                                    (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_FCW':                                          (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_FSW':                                          (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_I16':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U16':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U16_SX_U32':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U16_SX_U64':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U16_ZX_U32':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U16_ZX_U64':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_I32':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U32':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U32_SX_U64':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U32_ZX_U64':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U64':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U64_ZX_U64':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8':                                      (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_SX_U16':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_SX_U32':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_SX_U64':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_ZX_U16':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_ZX_U32':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_U8_ZX_U64':                               (McBlock.parseMcGeneric,           False, False, True,  ), # thrd var
    'IEM_MC_FETCH_GREG_PAIR_U32':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_GREG_PAIR_U64':                                (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_MEM_D80':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_I16':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_I16_DISP':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_I32':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_I32_DISP':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_I64':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_R32':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_R64':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_R80':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_U128':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U128_ALIGN_SSE':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U128_NO_AC':                               (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U128_AND_XREG_U128':                       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64':       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64':(McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_U16':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_U16_DISP':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_U16_SX_U32':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U16_SX_U64':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U16_ZX_U32':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_U16_ZX_U64':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_U256':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U256_ALIGN_AVX':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U256_NO_AC':                               (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_U32':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_U32_DISP':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ), #bounds only
    'IEM_MC_FETCH_MEM_U32_SX_U64':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U32_ZX_U64':                               (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_U64':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_U64_ALIGN_U128':                           (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_U8':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_U8_SX_U16':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U8_SX_U32':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U8_SX_U64':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movsx
    'IEM_MC_FETCH_MEM_U8_ZX_U16':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_U8_ZX_U32':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_U8_ZX_U64':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # movzx
    'IEM_MC_FETCH_MEM_XMM':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_XMM_NO_AC':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM':               (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM':                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM':                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_YMM':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_YMM_ALIGN_AVX':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM_YMM_NO_AC':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM':               (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM16_U8':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MEM32_U8':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FETCH_MREG_U8':                                      (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_MREG_U16':                                     (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_MREG_U32':                                     (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_MREG_U64':                                     (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_SREG_BASE_U32':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_SREG_BASE_U64':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_SREG_U16':                                     (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_SREG_ZX_U32':                                  (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_SREG_ZX_U64':                                  (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_FETCH_XREG_R32':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_R64':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_U128':                                    (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_U16':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_U32':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_U64':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_U8':                                      (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_XREG_XMM':                                     (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_XREG_PAIR_U128':                               (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64':               (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64':        (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_XREG_PAIR_XMM':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FETCH_YREG_U128':                                    (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_YREG_U256':                                    (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_YREG_YMM':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_YREG_U32':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_YREG_U64':                                     (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_FETCH_YREG_PAIR_YMM':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_FLIP_EFL_BIT':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_FPU_FROM_MMX_MODE':                                  (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_DEC_TOP':                                  (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_FREE':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_INC_TOP':                                  (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_PUSH_OVERFLOW':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_PUSH_OVERFLOW_MEM_OP':                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_PUSH_UNDERFLOW':                           (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_PUSH_UNDERFLOW_TWO':                       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_UNDERFLOW':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP':                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP_THEN_POP':                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP':                       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP_POP':                   (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_FPU_TO_MMX_MODE':                                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_HINT_FLUSH_GUEST_SHADOW':                            (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_IF_CX_IS_NZ':                                        (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_CX_IS_NOT_ONE':                                   (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET':               (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_SET':                   (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_ECX_IS_NZ':                                       (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_ECX_IS_NOT_ONE':                                  (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET':              (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_SET':                  (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_ANY_BITS_SET':                                (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BIT_NOT_SET':                                 (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ':                     (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BIT_SET':                                     (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE':                          (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BITS_EQ':                                     (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_BITS_NE':                                     (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_EFL_NO_BITS_SET':                                 (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_FCW_IM':                                          (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IF_FPUREG_IS_EMPTY':                                 (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IF_FPUREG_NOT_EMPTY':                                (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IF_FPUREG_NOT_EMPTY_REF_R80':                        (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IF_GREG_BIT_SET':                                    (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_LOCAL_IS_Z':                                      (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_RCX_IS_NZ':                                       (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_RCX_IS_NOT_ONE':                                  (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET':              (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_SET':                  (McBlock.parseMcGenericCond,       True,  False, True,  ),
    'IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80':                   (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80_FIRST':             (McBlock.parseMcGenericCond,       True,  True,  False, ),
    'IEM_MC_IND_CALL_U16_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_IND_CALL_U32_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_IND_CALL_U64_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_INT_CLEAR_ZMM_256_UP':                               (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_LIVENESS_GREG_INPUT':                                (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_GREG_CLOBBER':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_GREG_MODIFY':                               (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MREG_INPUT':                                (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MREG_CLOBBER':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MREG_MODIFY':                               (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_XREG_INPUT':                                (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_XREG_CLOBBER':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_XREG_MODIFY':                               (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MXCSR_INPUT':                               (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MXCSR_CLOBBER':                             (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LIVENESS_MXCSR_MODIFY':                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_LOCAL':                                              (McBlock.parseMcLocal,             False, False, True,  ),
    'IEM_MC_LOCAL_ASSIGN':                                       (McBlock.parseMcLocalAssign,       False, False, True,  ),
    'IEM_MC_LOCAL_CONST':                                        (McBlock.parseMcLocalConst,        False, False, True,  ),
    'IEM_MC_LOCAL_EFLAGS':                                       (McBlock.parseMcLocalEFlags,       True,  True,  True,  ),
    'IEM_MC_NOREF':                                              (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT':                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE':                   (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MAYBE_RAISE_FPU_XCPT':                               (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT':                          (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT':                       (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MAYBE_RAISE_NON_CANONICAL_ADDR_GP0':                 (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT':                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE':              (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC':                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_COMMIT_AND_UNMAP_RW':                            (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_COMMIT_AND_UNMAP_RO':                            (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_COMMIT_AND_UNMAP_WO':                            (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO':              (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MEM_MAP_D80_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_I16_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_I32_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_I64_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_R32_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_R64_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_R80_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U8_ATOMIC':                                  (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U8_RW':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U8_RO':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U8_WO':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U16_ATOMIC':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U16_RW':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U16_RO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U16_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U32_ATOMIC':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U32_RW':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U32_RO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U32_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U64_ATOMIC':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U64_RW':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U64_RO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U64_WO':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U128_ATOMIC':                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U128_RW':                                    (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U128_RO':                                    (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_MAP_U128_WO':                                    (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO':                          (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX':                        (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MERGE_YREG_U64HI_U64HI_ZX_VLMAX':                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX':                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX':                 (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX':                 (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_MODIFIED_MREG':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_MODIFIED_MREG_BY_REF':                               (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_NATIVE_EMIT_0':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_1':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_2':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_2_EX':                                   (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_3':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_4':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_5':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_6':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_EMIT_7':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_NATIVE_IF':                                          (McBlock.parseMcNativeIf,          False, False, True,  ),
    'IEM_MC_NATIVE_ELSE':                                        (McBlock.parseMcGenericCond,       False, False, True,  ),
    'IEM_MC_NATIVE_ENDIF':                                       (McBlock.parseMcGenericCond,       False, False, True,  ),
    'IEM_MC_NATIVE_SET_AMD64_HOST_REG_FOR_LOCAL':                (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_OR_2LOCS_U32':                                       (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_OR_GREG_U16':                                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_OR_GREG_U32':                                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_OR_GREG_U64':                                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_OR_GREG_U8':                                         (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_OR_LOCAL_U16':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_OR_LOCAL_U32':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_OR_LOCAL_U8':                                        (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_POP_GREG_U16':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_POP_GREG_U32':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_POP_GREG_U64':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_PREPARE_AVX_USAGE':                                  (McBlock.parseMcGeneric,           False, False, True),
    'IEM_MC_PREPARE_FPU_USAGE':                                  (McBlock.parseMcGeneric,           False, False, True),
    'IEM_MC_PREPARE_SSE_USAGE':                                  (McBlock.parseMcGeneric,           False, False, True),
    'IEM_MC_PUSH_FPU_RESULT':                                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_PUSH_FPU_RESULT_MEM_OP':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_PUSH_FPU_RESULT_TWO':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_PUSH_U16':                                           (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_PUSH_U32':                                           (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_PUSH_U32_SREG':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_PUSH_U64':                                           (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_RAISE_DIVIDE_ERROR_IF_LOCAL_IS_ZERO':                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_RAISE_GP0_IF_CPL_NOT_ZERO':                          (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_RAISE_GP0_IF_EFF_ADDR_UNALIGNED':                    (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_REF_EFLAGS':                                         (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_FPUREG':                                         (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_GREG_I32':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_I32_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_I64':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_I64_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U16':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U16_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U32':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U32_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U64':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U64_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_GREG_U8':                                        (McBlock.parseMcGeneric,           False, False, False, ), # threaded
    'IEM_MC_REF_GREG_U8_CONST':                                  (McBlock.parseMcGeneric,           False, False, False, ), # threaded
    'IEM_MC_REF_MREG_U32_CONST':                                 (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_MREG_U64':                                       (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_MREG_U64_CONST':                                 (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_XREG_R32_CONST':                                 (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_REF_XREG_R64_CONST':                                 (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_REF_XREG_U128':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_XREG_XMM':                                       (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_XREG_U128_CONST':                                (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_XREG_U32_CONST':                                 (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_REF_XREG_U64_CONST':                                 (McBlock.parseMcGeneric,           False, False, g_fNativeSimd),
    'IEM_MC_REF_XREG_XMM_CONST':                                 (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_REF_YREG_U128':                                      (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_YREG_U128_CONST':                                (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REF_YREG_U64_CONST':                                 (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_REL_CALL_S16_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_REL_CALL_S32_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_REL_CALL_S64_AND_FINISH':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_REL_JMP_S16_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_REL_JMP_S32_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_REL_JMP_S8_AND_FINISH':                              (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_RETN_AND_FINISH':                                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_RETURN_ON_FAILURE':                                  (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_SAR_LOCAL_S16':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SAR_LOCAL_S32':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SAR_LOCAL_S64':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SET_EFL_BIT':                                        (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_SET_FPU_RESULT':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_SET_RIP_U16_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_SET_RIP_U32_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_SET_RIP_U64_AND_FINISH':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_SHL_LOCAL_S16':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SHL_LOCAL_S32':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SHL_LOCAL_S64':                                      (McBlock.parseMcGeneric,           False, False, True,  ),
    'IEM_MC_SHR_LOCAL_U8':                                       (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_STORE_FPU_RESULT':                                   (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_FPU_RESULT_MEM_OP':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_FPU_RESULT_THEN_POP':                          (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_FPU_RESULT_WITH_MEM_OP_THEN_POP':              (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_FPUREG_R80_SRC_REF':                           (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_GREG_I32':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_I64':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U16':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U16_CONST':                               (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U32':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U32_CONST':                               (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U64':                                     (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U64_CONST':                               (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U8':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ), # thrd var
    'IEM_MC_STORE_GREG_U8_CONST':                                (McBlock.parseMcGeneric,           True,  True,  True,  ), # thrd var
    'IEM_MC_STORE_GREG_PAIR_U32':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_GREG_PAIR_U64':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_I16_CONST_BY_REF':                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_I32_CONST_BY_REF':                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_I64_CONST_BY_REF':                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_I8_CONST_BY_REF':                          (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_INDEF_D80_BY_REF':                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_NEG_QNAN_R32_BY_REF':                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_NEG_QNAN_R64_BY_REF':                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_NEG_QNAN_R80_BY_REF':                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_U128':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_U128_NO_AC':                               (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_U128_ALIGN_SSE':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_U16':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U16_CONST':                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U256':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MEM_U256_NO_AC':                               (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_U256_ALIGN_AVX':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_U32':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U32_CONST':                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U64':                                      (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U64_CONST':                                (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U8':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MEM_U8_CONST':                                 (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_STORE_MREG_U8':                                      (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MREG_U16':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MREG_U32':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MREG_U32_ZX_U64':                              (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_MREG_U64':                                     (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_SREG_BASE_U32':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_SREG_BASE_U64':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_XREG_R32':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_R64':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U128':                                    (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U16':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U32':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U32_U128':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U32_ZX_U128':                             (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U64':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U64_ZX_U128':                             (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_U8':                                      (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_XMM':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_XREG_XMM_U32':                                 (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_XREG_XMM_U64':                                 (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_STORE_YREG_U128':                                    (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U128_ZX_VLMAX':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U256_ZX_VLMAX':                           (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_YMM_ZX_VLMAX':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U32_U256':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U32_ZX_VLMAX':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U64':                                     (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U64_U256':                                (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_YREG_U64_ZX_VLMAX':                            (McBlock.parseMcGeneric,           True,  True,  g_fNativeSimd),
    'IEM_MC_SUB_GREG_U16':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_SUB_GREG_U32':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_SUB_GREG_U64':                                       (McBlock.parseMcGeneric,           True,  True,  True,  ),
    'IEM_MC_SUB_LOCAL_U16':                                      (McBlock.parseMcGeneric,           False, False, False, ),
    'IEM_MC_UPDATE_FPU_OPCODE_IP':                               (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW':                                         (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW_CONST':                                   (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW_THEN_POP':                                (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW_THEN_POP_POP':                            (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW_WITH_MEM_OP':                             (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_UPDATE_FSW_WITH_MEM_OP_THEN_POP':                    (McBlock.parseMcGeneric,           True,  True,  False, ),
    'IEM_MC_NO_NATIVE_RECOMPILE':                                (McBlock.parseMcGeneric,           False, False, False, ),
};
# pylint: enable=line-too-long

## List of microcode blocks.
g_aoMcBlocks = [] # type: List[McBlock]



class ParserException(Exception):
    """ Parser exception """
    def __init__(self, sMessage):
        Exception.__init__(self, sMessage);


class SimpleParser(object): # pylint: disable=too-many-instance-attributes
    """
    Parser of IEMAllInstruction*.cpp.h instruction specifications.
    """

    ## @name Parser state.
    ## @{
    kiCode              = 0;
    kiCommentMulti      = 1;
    ## @}

    class Macro(object):
        """ Macro """
        def __init__(self, sName, asArgs, sBody, iLine):
            self.sName       = sName;            ##< The macro name.
            self.asArgs      = asArgs;           ##< None if simple macro, list of parameters otherwise.
            self.sBody       = sBody;
            self.iLine       = iLine;
            self.oReArgMatch = re.compile(r'(\s*##\s*|\b)(' + '|'.join(asArgs) + r')(\s*##\s*|\b)') if asArgs else None;

        @staticmethod
        def _needSpace(ch):
            """ This is just to make the expanded output a bit prettier. """
            return ch.isspace() and ch != '(';

        def expandMacro(self, oParent, asArgs = None):
            """ Expands the macro body with the given arguments. """
            _ = oParent;
            sBody = self.sBody;

            if self.oReArgMatch:
                assert len(asArgs) == len(self.asArgs);
                #oParent.debug('%s: %s' % (self.sName, self.oReArgMatch.pattern,));

                dArgs = { self.asArgs[iArg]: sValue for iArg, sValue in enumerate(asArgs) };
                oMatch = self.oReArgMatch.search(sBody);
                while oMatch:
                    sName  = oMatch.group(2);
                    #oParent.debug('%s %s..%s (%s)' % (sName, oMatch.start(), oMatch.end(),oMatch.group()));
                    sValue = dArgs[sName];
                    sPre   = '';
                    if not oMatch.group(1) and oMatch.start() > 0 and self._needSpace(sBody[oMatch.start()]):
                        sPre = ' ';
                    sPost  = '';
                    if not oMatch.group(3) and oMatch.end() < len(sBody) and self._needSpace(sBody[oMatch.end()]):
                        sPost = ' ';
                    sBody  = sBody[ : oMatch.start()] + sPre + sValue + sPost + sBody[oMatch.end() : ];
                    oMatch = self.oReArgMatch.search(sBody, oMatch.start() + len(sValue));
            else:
                assert not asArgs;

            return sBody;

    class PreprocessorConditional(object):
        """ Preprocessor conditional (#if/#ifdef/#ifndef/#elif/#else/#endif). """

        ## Known defines.
        #   - A value of 1 indicates that it's always defined.
        #   - A value of 0 if it's always undefined
        #   - A value of -1 if it's an arch and it depends of script parameters.
        #   - A value of -2 if it's not recognized when filtering MC blocks.
        kdKnownDefines = {
            'IEM_WITH_ONE_BYTE_TABLE':          1,
            'IEM_WITH_TWO_BYTE_TABLE':          1,
            'IEM_WITH_THREE_0F_38':             1,
            'IEM_WITH_THREE_0F_3A':             1,
            'IEM_WITH_THREE_BYTE_TABLES':       1,
            'IEM_WITH_3DNOW':                   1,
            'IEM_WITH_3DNOW_TABLE':             1,
            'IEM_WITH_VEX':                     1,
            'IEM_WITH_VEX_TABLES':              1,
            'VBOX_WITH_NESTED_HWVIRT_VMX':      1,
            'VBOX_WITH_NESTED_HWVIRT_VMX_EPT':  1,
            'VBOX_WITH_NESTED_HWVIRT_SVM':      1,
            'LOG_ENABLED':                      1,
            'RT_WITHOUT_PRAGMA_ONCE':           0,
            'TST_IEM_CHECK_MC':                 0,
            'IEM_WITHOUT_ASSEMBLY':            -2, ##< @todo ??
            'RT_ARCH_AMD64':                   -1,
            'RT_ARCH_ARM64':                   -1,
            'RT_ARCH_ARM32':                   -1,
            'RT_ARCH_X86':                     -1,
            'RT_ARCH_SPARC':                   -1,
            'RT_ARCH_SPARC64':                 -1,
        };
        kdBuildArchToIprt = {
            'amd64':   'RT_ARCH_AMD64',
            'arm64':   'RT_ARCH_ARM64',
            'sparc32': 'RT_ARCH_SPARC64',
        };
        ## For parsing the next defined(xxxx).
        koMatchDefined = re.compile(r'\s*defined\s*\(\s*([^ \t)]+)\s*\)\s*');

        def __init__(self, sType, sExpr):
            self.sType   = sType;
            self.sExpr   = sExpr;   ##< Expression without command and no leading or trailing spaces.
            self.aoElif  = []       # type: List[PreprocessorConditional]
            self.fInElse = [];
            if sType in ('if', 'elif'):
                self.checkExpression(sExpr);
            else:
                self.checkSupportedDefine(sExpr)

        @staticmethod
        def checkSupportedDefine(sDefine):
            """ Checks that sDefine is one that we support. Raises exception if unuspported. """
            #print('debug: checkSupportedDefine: %s' % (sDefine,), file = sys.stderr);
            if sDefine in SimpleParser.PreprocessorConditional.kdKnownDefines:
                return True;
            if sDefine.startswith('VMM_INCLUDED_') and sDefine.endswith('_h'):
                return True;
            raise Exception('Unsupported define: %s' % (sDefine,));

        @staticmethod
        def checkExpression(sExpr):
            """ Check that the expression is supported. Raises exception if not. """
            #print('debug: checkExpression: %s' % (sExpr,), file = sys.stderr);
            if sExpr in ('0', '1'):
                return True;

            off = 0;
            cParan = 0;
            while off < len(sExpr):
                ch = sExpr[off];

                # Unary operator or parentheses:
                if ch in ('(', '!'):
                    if ch == '(':
                        cParan += 1;
                    off += 1;
                else:
                    # defined(xxxx)
                    oMatch = SimpleParser.PreprocessorConditional.koMatchDefined.match(sExpr, off);
                    if oMatch:
                        SimpleParser.PreprocessorConditional.checkSupportedDefine(oMatch.group(1));
                    elif sExpr[off:] != '1':
                        raise Exception('Cannot grok: \'%s\' (at %u in: \'%s\')' % (sExpr[off:10], off + 1, sExpr,));
                    off = oMatch.end();

                    # Look for closing parentheses.
                    while off < len(sExpr) and sExpr[off].isspace():
                        off += 1;
                    if cParan > 0:
                        while off < len(sExpr) and sExpr[off] == ')':
                            if cParan <= 0:
                                raise Exception('Unbalanced parentheses at %u in \'%s\'' % (off + 1, sExpr,));
                            cParan -= 1;
                            off    += 1;
                            while off < len(sExpr) and sExpr[off].isspace():
                                off += 1;

                    # Look for binary operator.
                    if off >= len(sExpr):
                        break;
                    if sExpr[off:off + 2] in ('||', '&&'):
                        off += 2;
                    else:
                        raise Exception('Cannot grok operator: \'%s\' (at %u in: \'%s\')' % (sExpr[off:2], off + 1, sExpr,));

                # Skip spaces.
                while off < len(sExpr) and sExpr[off].isspace():
                    off += 1;
            if cParan != 0:
                raise Exception('Unbalanced parentheses at %u in \'%s\'' % (off + 1, sExpr,));
            return True;

        @staticmethod
        def isArchIncludedInExpr(sExpr, sArch):
            """ Checks if sArch is included in the given expression. """
            # We only grok defined() [|| defined()...] and [1|0] at the moment.
            if sExpr == '0':
                return False;
            if sExpr == '1':
                return True;
            off = 0;
            while off < len(sExpr):
                # defined(xxxx)
                oMatch = SimpleParser.PreprocessorConditional.koMatchDefined.match(sExpr, off);
                if not oMatch:
                    if sExpr[off:] == '1':
                        return True;
                    raise Exception('Cannot grok: %s (at %u in: %s)' % (sExpr[off:10], off + 1, sExpr,));
                if SimpleParser.PreprocessorConditional.matchDefined(oMatch.group(1), sArch):
                    return True;
                off = oMatch.end();

                # Look for OR operator.
                while off + 1 < len(sExpr) and sExpr[off + 1].isspace():
                    off += 1;
                if off >= len(sExpr):
                    break;
                if sExpr.startswith('||'):
                    off += 2;
                else:
                    raise Exception('Cannot grok: %s (at %u in: %s)' % (sExpr[off:10], off + 1, sExpr,));

            return False;

        @staticmethod
        def matchArch(sDefine, sArch):
            """ Compares sDefine (RT_ARCH_XXXX) and sArch (x86, amd64, arm64, ++). """
            return SimpleParser.PreprocessorConditional.kdBuildArchToIprt[sArch] == sDefine;

        @staticmethod
        def matchDefined(sExpr, sArch):
            """ Check the result of an ifdef/ifndef expression, given sArch. """
            iDefine = SimpleParser.PreprocessorConditional.kdKnownDefines.get(sExpr, 0);
            if iDefine == -2:
                raise Exception('Unsupported define for MC block filtering: %s' % (sExpr,));
            return iDefine == 1 or (iDefine == -1 and SimpleParser.PreprocessorConditional.matchArch(sExpr, sArch));

        def isArchIncludedInPrimaryBlock(self, sArch):
            """ Checks if sArch is included in the (primary) 'if' block. """
            if self.sType == 'ifdef':
                return self.matchDefined(self.sExpr, sArch);
            if self.sType == 'ifndef':
                return not self.matchDefined(self.sExpr, sArch);
            return self.isArchIncludedInExpr(self.sExpr, sArch);

        @staticmethod
        def isInBlockForArch(aoCppCondStack, sArch, iLine):
            """ Checks if sArch is included in the current conditional block. """
            _ = iLine;
            #print('debug: isInBlockForArch(%s,%s); line %s' % (len(aoCppCondStack), sArch, iLine), file = sys.stderr);
            for oCond in aoCppCondStack:
                if oCond.isArchIncludedInPrimaryBlock(sArch):
                    if oCond.aoElif or oCond.fInElse:
                        #print('debug: isInBlockForArch -> False #1', file = sys.stderr);
                        return False;
                    #print('debug: isInBlockForArch(%s,%s): in IF-block' % (len(aoCppCondStack), sArch), file = sys.stderr);
                else:
                    fFine = False;
                    for oElifCond in oCond.aoElif:
                        if oElifCond.isArchIncludedInPrimaryBlock(sArch):
                            if oElifCond is not oCond.aoElif[-1] or oCond.fInElse:
                                #print('debug: isInBlockForArch -> False #3', file = sys.stderr);
                                return False;
                            fFine = True;
                    if not fFine and not oCond.fInElse:
                        #print('debug: isInBlockForArch -> False #4', file = sys.stderr);
                        return False;
            #print('debug: isInBlockForArch -> True', file = sys.stderr);
            return True;

    def __init__(self, sSrcFile, asLines, sDefaultMap, sHostArch, oInheritMacrosFrom = None):
        self.sSrcFile       = sSrcFile;
        self.asLines        = asLines;
        self.iLine          = 0;
        self.iState         = self.kiCode;
        self.sComment       = '';
        self.iCommentLine   = 0;
        self.aoCurInstrs    = []    # type: List[Instruction]
        self.oCurFunction   = None  # type: DecoderFunction
        self.iMcBlockInFunc = 0;
        self.oCurMcBlock    = None  # type: McBlock
        self.dMacros        = {}    # type: Dict[str, SimpleParser.Macro]
        self.oReMacros      = None  # type: re      ##< Regular expression matching invocations of anything in self.dMacros.
        if oInheritMacrosFrom:
            self.dMacros    = dict(oInheritMacrosFrom.dMacros);
            self.oReMacros  = oInheritMacrosFrom.oReMacros;
        self.aoCppCondStack = []    # type: List[PreprocessorConditional] ##< Preprocessor conditional stack.
        self.sHostArch      = sHostArch;

        assert sDefaultMap in g_dInstructionMaps;
        self.oDefaultMap    = g_dInstructionMaps[sDefaultMap];

        self.cTotalInstr    = 0;
        self.cTotalStubs    = 0;
        self.cTotalTagged   = 0;
        self.cTotalMcBlocks = 0;

        self.oReMacroName   = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$');
        self.oReMnemonic    = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$');
        self.oReStatsName   = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$');
        self.oReFunctionName= re.compile(r'^iemOp_[A-Za-z_][A-Za-z0-9_]*$');
        self.oReGroupName   = re.compile(r'^og_[a-z0-9]+(|_[a-z0-9]+|_[a-z0-9]+_[a-z0-9]+)$');
        self.oReDisEnum     = re.compile(r'^OP_[A-Z0-9_]+$');
        self.oReFunTable    = re.compile(r'^(IEM_STATIC|static) +const +PFNIEMOP +g_apfn[A-Za-z0-9_]+ *\[ *\d* *\] *= *$');
        self.oReComment     = re.compile(r'//.*?$|/\*.*?\*/'); ## Full comments.
        self.oReHashDefine2 = re.compile(r'(?s)\A\s*([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\)\s*(.*)\Z'); ##< With arguments.
        self.oReHashDefine3 = re.compile(r'(?s)\A\s*([A-Za-z_][A-Za-z0-9_]*)[^(]\s*(.*)\Z');        ##< Simple, no arguments.
        self.oReMcBeginEnd  = re.compile(r'\bIEM_MC_(BEGIN|END|DEFER_TO_CIMPL_[1-5]_RET)\s*\('); ##> Not DEFER_TO_CIMPL_0_RET!
        self.fDebug         = True;
        self.fDebugMc       = False;
        self.fDebugPreproc  = False;

        self.dTagHandlers   = {
            '@opbrief':     self.parseTagOpBrief,
            '@opdesc':      self.parseTagOpDesc,
            '@opmnemonic':  self.parseTagOpMnemonic,
            '@op1':         self.parseTagOpOperandN,
            '@op2':         self.parseTagOpOperandN,
            '@op3':         self.parseTagOpOperandN,
            '@op4':         self.parseTagOpOperandN,
            '@oppfx':       self.parseTagOpPfx,
            '@opmaps':      self.parseTagOpMaps,
            '@opcode':      self.parseTagOpcode,
            '@opcodesub':   self.parseTagOpcodeSub,
            '@openc':       self.parseTagOpEnc,
            #@opfltest:     Lists all flags that will be used as input in some way.
            '@opfltest':    self.parseTagOpEFlags,
            #@opflmodify:   Lists all EFLAGS modified. Includes @opflset, @opflcleared and @opflundef (if applicable).
            '@opflmodify':  self.parseTagOpEFlags,
            #@opflclear:    Lists all flags that will be set (set to 1).
            '@opflset':     self.parseTagOpEFlags,
            #@opflclear:    Lists all flags that will be cleared (set to 0).
            '@opflclear':   self.parseTagOpEFlags,
            #@opflundef:    List of flag documented as undefined.
            '@opflundef':   self.parseTagOpEFlags,
            #@opflclass:    Shorthand for defining flag behaviour (@opfltest, @opfmodify, @opflset, @opflclear, @opflundef).
            '@opflclass':   self.parseTagOpEFlagsClass,
            '@ophints':     self.parseTagOpHints,
            '@opdisenum':   self.parseTagOpDisEnum,
            '@opmincpu':    self.parseTagOpMinCpu,
            '@opcpuid':     self.parseTagOpCpuId,
            '@opgroup':     self.parseTagOpGroup,
            '@opunused':    self.parseTagOpUnusedInvalid,
            '@opinvalid':   self.parseTagOpUnusedInvalid,
            '@opinvlstyle': self.parseTagOpUnusedInvalid,
            '@optest':      self.parseTagOpTest,
            '@optestign':   self.parseTagOpTestIgnore,
            '@optestignore': self.parseTagOpTestIgnore,
            '@opcopytests': self.parseTagOpCopyTests,
            '@oponly':      self.parseTagOpOnlyTest,
            '@oponlytest':  self.parseTagOpOnlyTest,
            '@opxcpttype':  self.parseTagOpXcptType,
            '@opstats':     self.parseTagOpStats,
            '@opfunction':  self.parseTagOpFunction,
            '@opdone':      self.parseTagOpDone,
        };
        for i in range(48):
            self.dTagHandlers['@optest%u' % (i,)]   = self.parseTagOpTestNum;
            self.dTagHandlers['@optest[%u]' % (i,)] = self.parseTagOpTestNum;

        self.asErrors = [];

    def raiseError(self, sMessage):
        """
        Raise error prefixed with the source and line number.
        """
        raise ParserException("%s:%d: error: %s" % (self.sSrcFile, self.iLine, sMessage,));

    def raiseCommentError(self, iLineInComment, sMessage):
        """
        Similar to raiseError, but the line number is iLineInComment + self.iCommentLine.
        """
        raise ParserException("%s:%d: error: %s" % (self.sSrcFile, self.iCommentLine + iLineInComment, sMessage,));

    def error(self, sMessage):
        """
        Adds an error.
        returns False;
        """
        self.asErrors.append(u'%s:%d: error: %s\n' % (self.sSrcFile, self.iLine, sMessage,));
        return False;

    def errorOnLine(self, iLine, sMessage):
        """
        Adds an error.
        returns False;
        """
        self.asErrors.append(u'%s:%d: error: %s\n' % (self.sSrcFile, iLine, sMessage,));
        return False;

    def errorComment(self, iLineInComment, sMessage):
        """
        Adds a comment error.
        returns False;
        """
        self.asErrors.append(u'%s:%d: error: %s\n' % (self.sSrcFile, self.iCommentLine + iLineInComment, sMessage,));
        return False;

    def printErrors(self):
        """
        Print the errors to stderr.
        Returns number of errors.
        """
        if self.asErrors:
            sys.stderr.write(u''.join(self.asErrors));
        return len(self.asErrors);

    def debug(self, sMessage):
        """
        For debugging.
        """
        if self.fDebug:
            print('debug: %s' % (sMessage,), file = sys.stderr);

    def stripComments(self, sLine):
        """
        Returns sLine with comments stripped.

        Complains if traces of incomplete multi-line comments are encountered.
        """
        sLine = self.oReComment.sub(" ", sLine);
        if sLine.find('/*') >= 0 or sLine.find('*/') >= 0:
            self.error('Unexpected multi-line comment will not be handled correctly. Please simplify.');
        return sLine;

    def parseFunctionTable(self, sLine):
        """
        Parses a PFNIEMOP table, updating/checking the @oppfx value.

        Note! Updates iLine as it consumes the whole table.
        """

        #
        # Extract the table name.
        #
        sName = re.search(r' *([a-zA-Z_0-9]+) *\[', sLine).group(1);
        oMap  = g_dInstructionMapsByIemName.get(sName);
        if not oMap:
            self.debug('No map for PFNIEMOP table: %s' % (sName,));
            oMap = self.oDefaultMap; # This is wrong wrong wrong.

        #
        # All but the g_apfnOneByteMap & g_apfnEscF1_E0toFF tables uses four
        # entries per byte:
        #       no prefix, 066h prefix, f3h prefix, f2h prefix
        # Those tables has 256 & 32 entries respectively.
        #
        cEntriesPerByte   = 4;
        cValidTableLength = 1024;
        asPrefixes        = ('none', '0x66', '0xf3', '0xf2');

        oEntriesMatch = re.search(r'\[ *(256|32) *\]', sLine);
        if oEntriesMatch:
            cEntriesPerByte   = 1;
            cValidTableLength = int(oEntriesMatch.group(1));
            asPrefixes        = (None,);

        #
        # The next line should be '{' and nothing else.
        #
        if self.iLine >= len(self.asLines) or not re.match('^ *{ *$', self.asLines[self.iLine]):
            return self.errorOnLine(self.iLine + 1, 'Expected lone "{" on line following PFNIEMOP table %s start' % (sName, ));
        self.iLine += 1;

        #
        # Parse till we find the end of the table.
        #
        iEntry = 0;
        while self.iLine < len(self.asLines):
            # Get the next line and strip comments and spaces (assumes no
            # multi-line comments).
            sLine = self.asLines[self.iLine];
            self.iLine += 1;
            sLine = self.stripComments(sLine).strip();

            # Split the line up into entries, expanding IEMOP_X4 usage.
            asEntries = sLine.split(',');
            for i in range(len(asEntries) - 1, -1, -1):
                sEntry = asEntries[i].strip();
                if sEntry.startswith('IEMOP_X4(') and sEntry[-1] == ')':
                    sEntry = (sEntry[len('IEMOP_X4('):-1]).strip();
                    asEntries.insert(i + 1, sEntry);
                    asEntries.insert(i + 1, sEntry);
                    asEntries.insert(i + 1, sEntry);
                if sEntry:
                    asEntries[i] = sEntry;
                else:
                    del asEntries[i];

            # Process the entries.
            for sEntry in asEntries:
                if sEntry in ('};', '}'):
                    if iEntry != cValidTableLength:
                        return self.error('Wrong table length for %s: %#x, expected %#x' % (sName, iEntry, cValidTableLength, ));
                    return True;
                if sEntry.startswith('iemOp_Invalid'):
                    pass; # skip
                else:
                    # Look up matching instruction by function.
                    sPrefix = asPrefixes[iEntry % cEntriesPerByte];
                    sOpcode = '%#04x' % (iEntry // cEntriesPerByte);
                    aoInstr  = g_dAllInstructionsByFunction.get(sEntry);
                    if aoInstr:
                        if not isinstance(aoInstr, list):
                            aoInstr = [aoInstr,];
                        oInstr = None;
                        for oCurInstr in aoInstr:
                            if oCurInstr.sOpcode == sOpcode and oCurInstr.sPrefix == sPrefix:
                                pass;
                            elif oCurInstr.sOpcode == sOpcode and oCurInstr.sPrefix is None:
                                oCurInstr.sPrefix = sPrefix;
                            elif oCurInstr.sOpcode is None and oCurInstr.sPrefix is None:
                                oCurInstr.sOpcode = sOpcode;
                                oCurInstr.sPrefix = sPrefix;
                            else:
                                continue;
                            oInstr = oCurInstr;
                            break;
                        if not oInstr:
                            oInstr = aoInstr[0].copy(oMap = oMap, sOpcode = sOpcode, sPrefix = sPrefix);
                            aoInstr.append(oInstr);
                            g_dAllInstructionsByFunction[sEntry] = aoInstr;
                            g_aoAllInstructions.append(oInstr);
                            oMap.aoInstructions.append(oInstr);
                    else:
                        self.debug('Function "%s", entry %#04x / byte %#04x in %s, is not associated with an instruction.'
                                   % (sEntry, iEntry, iEntry // cEntriesPerByte, sName,));
                iEntry += 1;

        return self.error('Unexpected end of file in PFNIEMOP table');

    def addInstruction(self, iLine = None):
        """
        Adds an instruction.
        """
        oInstr = Instruction(self.sSrcFile, self.iLine if iLine is None else iLine);
        g_aoAllInstructions.append(oInstr);
        self.aoCurInstrs.append(oInstr);
        return oInstr;

    def deriveMnemonicAndOperandsFromStats(self, oInstr, sStats):
        """
        Derives the mnemonic and operands from a IEM stats base name like string.
        """
        if oInstr.sMnemonic is None:
            asWords = sStats.split('_');
            oInstr.sMnemonic = asWords[0].lower();
            if len(asWords) > 1 and not oInstr.aoOperands:
                for sType in asWords[1:]:
                    if sType in g_kdOpTypes:
                        oInstr.aoOperands.append(Operand(g_kdOpTypes[sType][1], sType));
                    else:
                        #return self.error('unknown operand type: %s (instruction: %s)' % (sType, oInstr))
                        return False;
        return True;

    def doneInstructionOne(self, oInstr, iLine):
        """
        Complete the parsing by processing, validating and expanding raw inputs.
        """
        assert oInstr.iLineCompleted is None;
        oInstr.iLineCompleted = iLine;

        #
        # Specified instructions.
        #
        if oInstr.cOpTags > 0:
            if oInstr.sStats is None:
                pass;

        #
        # Unspecified legacy stuff.  We generally only got a few things to go on here.
        #   /** Opcode 0x0f 0x00 /0. */
        #   FNIEMOPRM_DEF(iemOp_Grp6_sldt)
        #
        else:
            #if oInstr.sRawOldOpcodes:
            #
            #if oInstr.sMnemonic:
            pass;

        #
        # Common defaults.
        #

        # Guess mnemonic and operands from stats if the former is missing.
        if oInstr.sMnemonic is None:
            if oInstr.sStats is not None:
                self.deriveMnemonicAndOperandsFromStats(oInstr, oInstr.sStats);
            elif oInstr.sFunction is not None:
                self.deriveMnemonicAndOperandsFromStats(oInstr, oInstr.sFunction.replace('iemOp_', ''));

        # Derive the disassembler op enum constant from the mnemonic.
        if oInstr.sDisEnum is None and oInstr.sMnemonic is not None:
            oInstr.sDisEnum = 'OP_' + oInstr.sMnemonic.upper();

        # Derive the IEM statistics base name from mnemonic and operand types.
        if oInstr.sStats is None:
            if oInstr.sFunction is not None:
                oInstr.sStats = oInstr.sFunction.replace('iemOp_', '');
            elif oInstr.sMnemonic is not None:
                oInstr.sStats = oInstr.sMnemonic;
                for oOperand in oInstr.aoOperands:
                    if oOperand.sType:
                        oInstr.sStats += '_' + oOperand.sType;

        # Derive the IEM function name from mnemonic and operand types.
        if oInstr.sFunction is None:
            if oInstr.sMnemonic is not None:
                oInstr.sFunction = 'iemOp_' + oInstr.sMnemonic;
                for oOperand in oInstr.aoOperands:
                    if oOperand.sType:
                        oInstr.sFunction += '_' + oOperand.sType;
            elif oInstr.sStats:
                oInstr.sFunction = 'iemOp_' + oInstr.sStats;

        #
        # Apply default map and then add the instruction to all it's groups.
        #
        if not oInstr.aoMaps:
            oInstr.aoMaps = [ self.oDefaultMap, ];
        for oMap in oInstr.aoMaps:
            oMap.aoInstructions.append(oInstr);

        #
        # Derive encoding from operands and maps.
        #
        if oInstr.sEncoding is None:
            if not oInstr.aoOperands:
                if oInstr.fUnused and oInstr.sSubOpcode:
                    oInstr.sEncoding = 'VEX.ModR/M' if oInstr.onlyInVexMaps() else 'ModR/M';
                else:
                    oInstr.sEncoding = 'VEX.fixed' if oInstr.onlyInVexMaps() else 'fixed';
            elif oInstr.aoOperands[0].usesModRM():
                if     (len(oInstr.aoOperands) >= 2 and oInstr.aoOperands[1].sWhere == 'vvvv') \
                    or oInstr.onlyInVexMaps():
                    oInstr.sEncoding = 'VEX.ModR/M';
                else:
                    oInstr.sEncoding = 'ModR/M';

        #
        # Check the opstat value and add it to the opstat indexed dictionary.
        #
        if oInstr.sStats:
            if oInstr.sStats not in g_dAllInstructionsByStat:
                g_dAllInstructionsByStat[oInstr.sStats] = oInstr;
            else:
                self.error('Duplicate opstat value "%s"\nnew: %s\nold: %s'
                           % (oInstr.sStats, oInstr, g_dAllInstructionsByStat[oInstr.sStats],));

        #
        # Add to function indexed dictionary.  We allow multiple instructions per function.
        #
        if oInstr.sFunction:
            if oInstr.sFunction not in g_dAllInstructionsByFunction:
                g_dAllInstructionsByFunction[oInstr.sFunction] = [oInstr,];
            else:
                g_dAllInstructionsByFunction[oInstr.sFunction].append(oInstr);

        #self.debug('%d..%d: %s; %d @op tags' % (oInstr.iLineCreated, oInstr.iLineCompleted, oInstr.sFunction, oInstr.cOpTags));
        return True;

    def doneInstructions(self, iLineInComment = None, fEndOfFunction = False):
        """
        Done with current instruction.
        """
        for oInstr in self.aoCurInstrs:
            self.doneInstructionOne(oInstr, self.iLine if iLineInComment is None else self.iCommentLine + iLineInComment);
            if oInstr.fStub:
                self.cTotalStubs += 1;

        self.cTotalInstr += len(self.aoCurInstrs);

        self.sComment     = '';
        self.aoCurInstrs  = [];
        if fEndOfFunction:
            #self.debug('%s: oCurFunction=None' % (self.iLine, ));
            if self.oCurFunction:
                self.oCurFunction.complete(self.iLine, self.asLines[self.oCurFunction.iBeginLine - 1 : self.iLine]);
            self.oCurFunction   = None;
            self.iMcBlockInFunc = 0;
        return True;

    def setInstrunctionAttrib(self, sAttrib, oValue, fOverwrite = False):
        """
        Sets the sAttrib of all current instruction to oValue.  If fOverwrite
        is False, only None values and empty strings are replaced.
        """
        for oInstr in self.aoCurInstrs:
            if fOverwrite is not True:
                oOldValue = getattr(oInstr, sAttrib);
                if oOldValue is not None:
                    continue;
            setattr(oInstr, sAttrib, oValue);

    def setInstrunctionArrayAttrib(self, sAttrib, iEntry, oValue, fOverwrite = False):
        """
        Sets the iEntry of the array sAttrib of all current instruction to oValue.
        If fOverwrite is False, only None values and empty strings are replaced.
        """
        for oInstr in self.aoCurInstrs:
            aoArray = getattr(oInstr, sAttrib);
            while len(aoArray) <= iEntry:
                aoArray.append(None);
            if fOverwrite is True or aoArray[iEntry] is None:
                aoArray[iEntry] = oValue;

    def parseCommentOldOpcode(self, asLines):
        """ Deals with 'Opcode 0xff /4' like comments """
        asWords = asLines[0].split();
        if    len(asWords) >= 2  \
          and asWords[0] == 'Opcode'  \
          and (   asWords[1].startswith('0x')
               or asWords[1].startswith('0X')):
            asWords = asWords[:1];
            for iWord, sWord in enumerate(asWords):
                if sWord.startswith('0X'):
                    sWord = '0x' + sWord[:2];
                    asWords[iWord] = asWords;
            self.setInstrunctionAttrib('sRawOldOpcodes', ' '.join(asWords));

        return False;

    def ensureInstructionForOpTag(self, iTagLine):
        """ Ensure there is an instruction for the op-tag being parsed. """
        if not self.aoCurInstrs:
            self.addInstruction(self.iCommentLine + iTagLine);
        for oInstr in self.aoCurInstrs:
            oInstr.cOpTags += 1;
            if oInstr.cOpTags == 1:
                self.cTotalTagged += 1;
        return self.aoCurInstrs[-1];

    @staticmethod
    def flattenSections(aasSections):
        """
        Flattens multiline sections into stripped single strings.
        Returns list of strings, on section per string.
        """
        asRet = [];
        for asLines in aasSections:
            if asLines:
                asRet.append(' '.join([sLine.strip() for sLine in asLines]));
        return asRet;

    @staticmethod
    def flattenAllSections(aasSections, sLineSep = ' ', sSectionSep = '\n'):
        """
        Flattens sections into a simple stripped string with newlines as
        section breaks.  The final section does not sport a trailing newline.
        """
        # Typical: One section with a single line.
        if len(aasSections) == 1 and len(aasSections[0]) == 1:
            return aasSections[0][0].strip();

        sRet = '';
        for iSection, asLines in enumerate(aasSections):
            if asLines:
                if iSection > 0:
                    sRet += sSectionSep;
                sRet += sLineSep.join([sLine.strip() for sLine in asLines]);
        return sRet;



    ## @name Tag parsers
    ## @{

    def parseTagOpBrief(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opbrief
        Value:  Text description, multiple sections, appended.

        Brief description.  If not given, it's the first sentence from @opdesc.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sBrief = self.flattenAllSections(aasSections);
        if not sBrief:
            return self.errorComment(iTagLine, '%s: value required' % (sTag,));
        if sBrief[-1] != '.':
            sBrief = sBrief + '.';
        if len(sBrief) > 180:
            return self.errorComment(iTagLine, '%s: value too long (max 180 chars): %s' % (sTag, sBrief));
        offDot = sBrief.find('.');
        while 0 <= offDot < len(sBrief) - 1 and sBrief[offDot + 1] != ' ':
            offDot = sBrief.find('.', offDot + 1);
        if offDot >= 0 and offDot != len(sBrief) - 1:
            return self.errorComment(iTagLine, '%s: only one sentence: %s' % (sTag, sBrief));

        # Update the instruction.
        if oInstr.sBrief is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite brief "%s" with "%s"'
                                               % (sTag, oInstr.sBrief, sBrief,));
        _ = iEndLine;
        return True;

    def parseTagOpDesc(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opdesc
        Value:  Text description, multiple sections, appended.

        It is used to describe instructions.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);
        if aasSections:
            oInstr.asDescSections.extend(self.flattenSections(aasSections));
            return True;

        _ = sTag; _ = iEndLine;
        return True;

    def parseTagOpMnemonic(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opmenmonic
        Value:  mnemonic

        The 'mnemonic' value must be a valid C identifier string.  Because of
        prefixes, groups and whatnot, there times when the mnemonic isn't that
        of an actual assembler mnemonic.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sMnemonic = self.flattenAllSections(aasSections);
        if not self.oReMnemonic.match(sMnemonic):
            return self.errorComment(iTagLine, '%s: invalid menmonic name: "%s"' % (sTag, sMnemonic,));
        if oInstr.sMnemonic is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite menmonic "%s" with "%s"'
                                     % (sTag, oInstr.sMnemonic, sMnemonic,));
        oInstr.sMnemonic = sMnemonic

        _ = iEndLine;
        return True;

    def parseTagOpOperandN(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tags:  @op1, @op2, @op3, @op4
        Value: [where:]type

        The 'where' value indicates where the operand is found, like the 'reg'
        part of the ModR/M encoding. See Instruction.kdOperandLocations for
        a list.

        The 'type' value indicates the operand type.  These follow the types
        given in the opcode tables in the CPU reference manuals.
        See Instruction.kdOperandTypes for a list.

        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);
        idxOp = int(sTag[-1]) - 1;
        assert 0 <= idxOp < 4;

        # flatten, split up, and validate the "where:type" value.
        sFlattened = self.flattenAllSections(aasSections);
        asSplit = sFlattened.split(':');
        if len(asSplit) == 1:
            sType  = asSplit[0];
            sWhere = None;
        elif len(asSplit) == 2:
            (sWhere, sType) = asSplit;
        else:
            return self.errorComment(iTagLine, 'expected %s value on format "[<where>:]<type>" not "%s"' % (sTag, sFlattened,));

        if sType not in g_kdOpTypes:
            return self.errorComment(iTagLine, '%s: invalid where value "%s", valid: %s'
                                               % (sTag, sType, ', '.join(g_kdOpTypes.keys()),));
        if sWhere is None:
            sWhere = g_kdOpTypes[sType][1];
        elif sWhere not in g_kdOpLocations:
            return self.errorComment(iTagLine, '%s: invalid where value "%s", valid: %s'
                                               % (sTag, sWhere, ', '.join(g_kdOpLocations.keys()),));

        # Insert the operand, refusing to overwrite an existing one.
        while idxOp >= len(oInstr.aoOperands):
            oInstr.aoOperands.append(None);
        if oInstr.aoOperands[idxOp] is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s:%s" with "%s:%s"'
                                               % ( sTag, oInstr.aoOperands[idxOp].sWhere, oInstr.aoOperands[idxOp].sType,
                                                   sWhere, sType,));
        oInstr.aoOperands[idxOp] = Operand(sWhere, sType);

        _ = iEndLine;
        return True;

    def parseTagOpMaps(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opmaps
        Value:  map[,map2]

        Indicates which maps the instruction is in.  There is a default map
        associated with each input file.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten, split up and validate the value.
        sFlattened = self.flattenAllSections(aasSections, sLineSep = ',', sSectionSep = ',');
        asMaps = sFlattened.split(',');
        if not asMaps:
            return self.errorComment(iTagLine, '%s: value required' % (sTag,));
        for sMap in asMaps:
            if sMap not in g_dInstructionMaps:
                return self.errorComment(iTagLine, '%s: invalid map value: %s  (valid values: %s)'
                                                   % (sTag, sMap, ', '.join(g_dInstructionMaps.keys()),));

        # Add the maps to the current list.  Throw errors on duplicates.
        for oMap in oInstr.aoMaps:
            if oMap.sName in asMaps:
                return self.errorComment(iTagLine, '%s: duplicate map assignment: %s' % (sTag, oMap.sName));

        for sMap in asMaps:
            oMap = g_dInstructionMaps[sMap];
            if oMap not in oInstr.aoMaps:
                oInstr.aoMaps.append(oMap);
            else:
                self.errorComment(iTagLine, '%s: duplicate map assignment (input): %s' % (sTag, sMap));

        _ = iEndLine;
        return True;

    def parseTagOpPfx(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @oppfx
        Value:      n/a|none|0x66|0xf3|0xf2|!0xf3

        Required prefix for the instruction.  (In a (E)VEX context this is the
        value of the 'pp' field rather than an actual prefix.)
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sFlattened = self.flattenAllSections(aasSections);
        asPrefixes = sFlattened.split();
        if len(asPrefixes) > 1:
            return self.errorComment(iTagLine, '%s: max one prefix: %s' % (sTag, asPrefixes,));

        sPrefix = asPrefixes[0].lower();
        if sPrefix == 'none':
            sPrefix = 'none';
        elif sPrefix == 'n/a':
            sPrefix = None;
        else:
            if len(sPrefix) == 2:
                sPrefix = '0x' + sPrefix;
            if not _isValidOpcodeByte(sPrefix):
                if sPrefix != '!0xf3':
                    return self.errorComment(iTagLine, '%s: invalid prefix: %s' % (sTag, sPrefix,));

        if sPrefix is not None and sPrefix not in g_kdPrefixes:
            return self.errorComment(iTagLine, '%s: invalid prefix: %s (valid %s)' % (sTag, sPrefix, g_kdPrefixes,));

        # Set it.
        if oInstr.sPrefix is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"' % ( sTag, oInstr.sPrefix, sPrefix,));
        oInstr.sPrefix = sPrefix;

        _ = iEndLine;
        return True;

    def parseTagOpcode(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opcode
        Value:      0x?? | /reg (TODO: | mr/reg | 11 /reg | !11 /reg | 11 mr/reg | !11 mr/reg)

        The opcode byte or sub-byte for the instruction in the context of a map.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sOpcode = self.flattenAllSections(aasSections);
        if _isValidOpcodeByte(sOpcode):
            pass;
        elif len(sOpcode) == 2 and sOpcode.startswith('/') and sOpcode[-1] in '012345678':
            pass;
        elif len(sOpcode) == 4 and sOpcode.startswith('11/') and sOpcode[-1] in '012345678':
            pass;
        elif len(sOpcode) == 5 and sOpcode.startswith('!11/') and sOpcode[-1] in '012345678':
            pass;
        else:
            return self.errorComment(iTagLine, '%s: invalid opcode: %s' % (sTag, sOpcode,));

        # Set it.
        if oInstr.sOpcode is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"' % ( sTag, oInstr.sOpcode, sOpcode,));
        oInstr.sOpcode = sOpcode;

        _ = iEndLine;
        return True;

    def parseTagOpcodeSub(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opcodesub
        Value:      none | 11 mr/reg | !11 mr/reg | rex.w=0 | rex.w=1 | vex.l=0 | vex.l=1
                    | 11 mr/reg vex.l=0 | 11 mr/reg vex.l=1 | !11 mr/reg vex.l=0 | !11 mr/reg vex.l=1
                    | !11 rex.w=0 | !11 mr/reg rex.w=0
                    | !11 rex.w=1 | !11 mr/reg rex.w=1

        This is a simple way of dealing with encodings where the mod=3 and mod!=3
        represents exactly two different instructions.  The more proper way would
        be to go via maps with two members, but this is faster.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sSubOpcode = self.flattenAllSections(aasSections);
        if sSubOpcode not in g_kdSubOpcodes:
            return self.errorComment(iTagLine, '%s: invalid sub opcode: %s  (valid: %s)'
                                     % (sTag, sSubOpcode, ', '.join(sorted(g_kdSubOpcodes.keys())),));
        sSubOpcode = g_kdSubOpcodes[sSubOpcode][0];

        # Set it.
        if oInstr.sSubOpcode is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"'
                                               % ( sTag, oInstr.sSubOpcode, sSubOpcode,));
        oInstr.sSubOpcode = sSubOpcode;

        _ = iEndLine;
        return True;

    def parseTagOpEnc(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @openc
        Value:      ModR/M|fixed|prefix|<map name>

        The instruction operand encoding style.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sEncoding = self.flattenAllSections(aasSections);
        if sEncoding in g_kdEncodings:
            pass;
        elif sEncoding in g_dInstructionMaps:
            pass;
        elif not _isValidOpcodeByte(sEncoding):
            return self.errorComment(iTagLine, '%s: invalid encoding: %s' % (sTag, sEncoding,));

        # Set it.
        if oInstr.sEncoding is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"'
                                               % ( sTag, oInstr.sEncoding, sEncoding,));
        oInstr.sEncoding = sEncoding;

        _ = iEndLine;
        return True;

    ## EFlags tag to Instruction attribute name.
    kdOpFlagToAttr = {
        '@opfltest':    'asFlTest',
        '@opflmodify':  'asFlModify',
        '@opflundef':   'asFlUndefined',
        '@opflset':     'asFlSet',
        '@opflclear':   'asFlClear',
    };

    def parseTagOpEFlags(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tags:   @opfltest, @opflmodify, @opflundef, @opflset, @opflclear
        Value:  <eflags specifier>

        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten, split up and validate the values.
        asFlags = self.flattenAllSections(aasSections, sLineSep = ',', sSectionSep = ',').split(',');
        if len(asFlags) == 1 and asFlags[0].lower() == 'none':
            asFlags = [];
        else:
            fRc = True;
            for iFlag, sFlag in enumerate(asFlags):
                if sFlag not in g_kdEFlagsMnemonics:
                    if sFlag.strip() in g_kdEFlagsMnemonics:
                        asFlags[iFlag] = sFlag.strip();
                    else:
                        fRc = self.errorComment(iTagLine, '%s: invalid EFLAGS value: %s' % (sTag, sFlag,));
            if not fRc:
                return False;

        # Set them.
        asOld = getattr(oInstr, self.kdOpFlagToAttr[sTag]);
        if asOld is not None and len(asOld) > 0:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"' % ( sTag, asOld, asFlags,));
        setattr(oInstr, self.kdOpFlagToAttr[sTag], asFlags);

        _ = iEndLine;
        return True;

    ## EFLAGS class definitions with their attribute lists.
    kdEFlagsClasses = {
        'arithmetic': { # add, sub, ...
            'asFlTest':         [],
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
        'arithmetic_carry': { # adc, sbb, ...
            'asFlTest':         [ 'cf', ],
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
        'incdec': {
            'asFlTest':         [],
            'asFlModify':       [ 'pf', 'af', 'zf', 'sf', 'of', ], # leaves CF alone
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
        'division': { ## @todo specify intel/amd differences...
            'asFlTest':         [ 'pf', 'af', 'zf', 'sf', ], # Intel leaves all flags unchanged.
            'asFlModify':       [ 'pf', 'af', 'zf', 'sf', ], # While AMD sets AF and clears PF, ZF & SF, leaving CF and OF alone.
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
        },
        'multiply': { ## @todo specify intel/amd differences...
            'asFlTest':         [ 'pf', 'af', 'zf', 'sf', ], # AMD leaves these unchanged, so we have to delcare them as inputs.
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of' ], # Intel always modifies all flags, but how differs
            'asFlClear':        [],                                     # between IMUL and MUL.
            'asFlSet':          [],
            'asFlUndefined':    [ 'pf', 'af', 'zf', 'sf', ],
        },
        'logical': { # and, or, xor, ...
            'asFlTest':         [],
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
            'asFlClear':        [ 'cf', 'af', 'of', ], # 'af' is undefined, but tstIEMAImpl indicates that it is cleared.
            'asFlSet':          [],
            'asFlUndefined':    [ 'af', ],
        },
        'rotate_1': { # rol and ror with fixed 1 shift count
            'asFlTest':         [],
            'asFlModify':       [ 'cf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
        'rotate_count': { # rol and ror w/o fixed 1 shift count
            'asFlTest':         [ 'cf', 'of', ], # If the count is zero, nothing changes.
            'asFlModify':       [ 'cf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'of', ],
        },
        'rotate_carry_1': { # rcl and rcr with fixed 1 shift count
            'asFlTest':         [ 'cf', ],
            'asFlModify':       [ 'cf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
        'rotate_carry_count': { # rcl and rcr w/o fixed 1 shift count
            'asFlTest':         [ 'cf', 'of', ], # If the count is zero, nothing changes, so 'of' is also input.
            'asFlModify':       [ 'cf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'of', ],
        },
        'shift_1': { # shl, shr or sar with fixed 1 count.
            'asFlTest':         [],
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'af', ],
        },
        'shift_count': { # shl, shr or sar w/o fixed 1 shift count
            'asFlTest':         [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ], # If the count is zero, nothing is changed.
            'asFlModify':       [ 'cf', 'pf', 'af', 'zf', 'sf', 'of', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'af', 'of', ],
        },
        'bitmap': { # bt, btc, btr, btc
            'asFlTest':         [],
            'asFlModify':       [ 'cf', ],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [ 'pf', 'af', 'zf', 'sf', 'of', ], # tstIEMAImpl indicates that they aren't modified.
        },
        'unchanged': {
            'asFlTest':         [],
            'asFlModify':       [],
            'asFlClear':        [],
            'asFlSet':          [],
            'asFlUndefined':    [],
        },
    };
    def parseTagOpEFlagsClass(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tags:   @opflclass
        Value:  arithmetic, logical, ...

        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sClass = self.flattenAllSections(aasSections);
        kdAttribs = self.kdEFlagsClasses.get(sClass);
        if not kdAttribs:
            return self.errorComment(iTagLine, '%s: Unknown EFLAGS class: %s (valid: %s)'
                                     % (sTag, sClass, ', '.join(sorted(self.kdEFlagsClasses.keys())),));

        # Set the attributes.
        for sAttrib, asFlags in kdAttribs.items():
            asOld = getattr(oInstr, sAttrib);
            if asOld is not None:
                return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s" for %s'
                                         % (sTag, asOld, asFlags, sAttrib));
            setattr(oInstr, sAttrib, asFlags);

        _ = iEndLine;
        return True;

    def parseTagOpHints(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @ophints
        Value:      Comma or space separated list of flags and hints.

        This covers the disassembler flags table and more.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten as a space separated list, split it up and validate the values.
        asHints = self.flattenAllSections(aasSections, sLineSep = ' ', sSectionSep = ' ').replace(',', ' ').split();
        if len(asHints) == 1 and asHints[0].lower() == 'none':
            asHints = [];
        else:
            fRc = True;
            for iHint, sHint in enumerate(asHints):
                if sHint not in g_kdHints:
                    if sHint.strip() in g_kdHints:
                        sHint[iHint] = sHint.strip();
                    else:
                        fRc = self.errorComment(iTagLine, '%s: invalid hint value: %s' % (sTag, sHint,));
            if not fRc:
                return False;

        # Append them.
        for sHint in asHints:
            if sHint not in oInstr.dHints:
                oInstr.dHints[sHint] = True; # (dummy value, using dictionary for speed)
            else:
                self.errorComment(iTagLine, '%s: duplicate hint: %s' % ( sTag, sHint,));

        _ = iEndLine;
        return True;

    def parseTagOpDisEnum(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opdisenum
        Value:      OP_XXXX

        This is for select a specific (legacy) disassembler enum value for the
        instruction.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and split.
        asWords = self.flattenAllSections(aasSections).split();
        if len(asWords) != 1:
            self.errorComment(iTagLine, '%s: expected exactly one value: %s' % (sTag, asWords,));
            if not asWords:
                return False;
        sDisEnum = asWords[0];
        if not self.oReDisEnum.match(sDisEnum):
            return self.errorComment(iTagLine, '%s: invalid disassembler OP_XXXX enum: %s (pattern: %s)'
                                               % (sTag, sDisEnum, self.oReDisEnum.pattern));

        # Set it.
        if oInstr.sDisEnum is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"' % (sTag, oInstr.sDisEnum, sDisEnum,));
        oInstr.sDisEnum = sDisEnum;

        _ = iEndLine;
        return True;

    def parseTagOpMinCpu(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opmincpu
        Value:      <simple CPU name>

        Indicates when this instruction was introduced.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten the value, split into words, make sure there's just one, valid it.
        asCpus = self.flattenAllSections(aasSections).split();
        if len(asCpus) > 1:
            self.errorComment(iTagLine, '%s: exactly one CPU name, please: %s' % (sTag, ' '.join(asCpus),));

        sMinCpu = asCpus[0];
        if sMinCpu in g_kdCpuNames:
            oInstr.sMinCpu = sMinCpu;
        else:
            return self.errorComment(iTagLine, '%s: invalid CPU name: %s  (names: %s)'
                                               % (sTag, sMinCpu, ','.join(sorted(g_kdCpuNames)),));

        # Set it.
        if oInstr.sMinCpu is None:
            oInstr.sMinCpu = sMinCpu;
        elif oInstr.sMinCpu != sMinCpu:
            self.errorComment(iTagLine, '%s: attemting to overwrite "%s" with "%s"' % (sTag, oInstr.sMinCpu, sMinCpu,));

        _ = iEndLine;
        return True;

    def parseTagOpCpuId(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opcpuid
        Value:      none | <CPUID flag specifier>

        CPUID feature bit which is required for the instruction to be present.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten as a space separated list, split it up and validate the values.
        asCpuIds = self.flattenAllSections(aasSections, sLineSep = ' ', sSectionSep = ' ').replace(',', ' ').split();
        if len(asCpuIds) == 1 and asCpuIds[0].lower() == 'none':
            asCpuIds = [];
        else:
            fRc = True;
            for iCpuId, sCpuId in enumerate(asCpuIds):
                if sCpuId not in g_kdCpuIdFlags:
                    if sCpuId.strip() in g_kdCpuIdFlags:
                        sCpuId[iCpuId] = sCpuId.strip();
                    else:
                        fRc = self.errorComment(iTagLine, '%s: invalid CPUID value: %s' % (sTag, sCpuId,));
            if not fRc:
                return False;

        # Append them.
        for sCpuId in asCpuIds:
            if sCpuId not in oInstr.asCpuIds:
                oInstr.asCpuIds.append(sCpuId);
            else:
                self.errorComment(iTagLine, '%s: duplicate CPUID: %s' % ( sTag, sCpuId,));

        _ = iEndLine;
        return True;

    def parseTagOpGroup(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opgroup
        Value:      op_grp1[_subgrp2[_subsubgrp3]]

        Instruction grouping.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten as a space separated list, split it up and validate the values.
        asGroups = self.flattenAllSections(aasSections).split();
        if len(asGroups) != 1:
            return self.errorComment(iTagLine, '%s: exactly one group, please: %s' % (sTag, asGroups,));
        sGroup = asGroups[0];
        if not self.oReGroupName.match(sGroup):
            return self.errorComment(iTagLine, '%s: invalid group name: %s (valid: %s)'
                                               % (sTag, sGroup, self.oReGroupName.pattern));

        # Set it.
        if oInstr.sGroup is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite "%s" with "%s"' % ( sTag, oInstr.sGroup, sGroup,));
        oInstr.sGroup = sGroup;

        _ = iEndLine;
        return True;

    def parseTagOpUnusedInvalid(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opunused, @opinvalid, @opinvlstyle
        Value:  <invalid opcode behaviour style>

        The @opunused indicates the specification is for a currently unused
        instruction encoding.

        The @opinvalid indicates the specification is for an invalid currently
        instruction encoding (like UD2).

        The @opinvlstyle just indicates how CPUs decode the instruction when
        not supported (@opcpuid, @opmincpu) or disabled.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten as a space separated list, split it up and validate the values.
        asStyles = self.flattenAllSections(aasSections).split();
        if len(asStyles) != 1:
            return self.errorComment(iTagLine, '%s: exactly one invalid behviour style, please: %s' % (sTag, asStyles,));
        sStyle = asStyles[0];
        if sStyle not in g_kdInvalidStyles:
            return self.errorComment(iTagLine, '%s: invalid invalid behaviour style: %s (valid: %s)'
                                               % (sTag, sStyle, g_kdInvalidStyles.keys(),));
        # Set it.
        if oInstr.sInvalidStyle is not None:
            return self.errorComment(iTagLine,
                                     '%s: attempting to overwrite "%s" with "%s" (only one @opunused, @opinvalid, @opinvlstyle)'
                                     % ( sTag, oInstr.sInvalidStyle, sStyle,));
        oInstr.sInvalidStyle = sStyle;
        if sTag == '@opunused':
            oInstr.fUnused = True;
        elif sTag == '@opinvalid':
            oInstr.fInvalid = True;

        _ = iEndLine;
        return True;

    def parseTagOpTest(self, sTag, aasSections, iTagLine, iEndLine): # pylint: disable=too-many-locals
        """
        Tag:        @optest
        Value:      [<selectors>[ ]?] <inputs> -> <outputs>
        Example:    mode==64bit / in1=0xfffffffe:dw in2=1:dw -> out1=0xffffffff:dw outfl=a?,p?

        The main idea here is to generate basic instruction tests.

        The probably simplest way of handling the diverse input, would be to use
        it to produce size optimized byte code for a simple interpreter that
        modifies the register input and output states.

        An alternative to the interpreter would be creating multiple tables,
        but that becomes rather complicated wrt what goes where and then to use
        them in an efficient manner.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        #
        # Do it section by section.
        #
        for asSectionLines in aasSections:
            #
            # Sort the input into outputs, inputs and selector conditions.
            #
            sFlatSection = self.flattenAllSections([asSectionLines,]);
            if not sFlatSection:
                self.errorComment(iTagLine, '%s: missing value (dbg: aasSections=%s)' % ( sTag, aasSections));
                continue;
            oTest = InstructionTest(oInstr);

            asSelectors = [];
            asInputs    = [];
            asOutputs   = [];
            asCur   = asOutputs;
            fRc     = True;
            asWords = sFlatSection.split();
            for iWord in range(len(asWords) - 1, -1, -1):
                sWord = asWords[iWord];
                # Check for array switchers.
                if sWord == '->':
                    if asCur != asOutputs:
                        fRc = self.errorComment(iTagLine, '%s: "->" shall only occure once: %s' % (sTag, sFlatSection,));
                        break;
                    asCur = asInputs;
                elif sWord == '/':
                    if asCur != asInputs:
                        fRc = self.errorComment(iTagLine, '%s: "/" shall only occure once: %s' % (sTag, sFlatSection,));
                        break;
                    asCur = asSelectors;
                else:
                    asCur.insert(0, sWord);

            #
            # Validate and add selectors.
            #
            for sCond in asSelectors:
                sCondExp = TestSelector.kdPredicates.get(sCond, sCond);
                oSelector = None;
                for sOp in TestSelector.kasCompareOps:
                    off = sCondExp.find(sOp);
                    if off >= 0:
                        sVariable = sCondExp[:off];
                        sValue    = sCondExp[off + len(sOp):];
                        if sVariable in TestSelector.kdVariables:
                            if sValue in TestSelector.kdVariables[sVariable]:
                                oSelector = TestSelector(sVariable, sOp, sValue);
                            else:
                                self.errorComment(iTagLine, '%s: invalid condition value "%s" in "%s" (valid: %s)'
                                                             % ( sTag, sValue, sCond,
                                                                 TestSelector.kdVariables[sVariable].keys(),));
                        else:
                            self.errorComment(iTagLine, '%s: invalid condition variable "%s" in "%s" (valid: %s)'
                                                         % ( sTag, sVariable, sCond, TestSelector.kdVariables.keys(),));
                        break;
                if oSelector is not None:
                    for oExisting in oTest.aoSelectors:
                        if oExisting.sVariable == oSelector.sVariable:
                            self.errorComment(iTagLine, '%s: already have a selector for variable "%s" (existing: %s, new: %s)'
                                                         % ( sTag, oSelector.sVariable, oExisting, oSelector,));
                    oTest.aoSelectors.append(oSelector);
                else:
                    fRc = self.errorComment(iTagLine, '%s: failed to parse selector: %s' % ( sTag, sCond,));

            #
            # Validate outputs and inputs, adding them to the test as we go along.
            #
            for asItems, sDesc, aoDst in [ (asInputs, 'input', oTest.aoInputs), (asOutputs, 'output', oTest.aoOutputs)]:
                asValidFieldKinds = [ 'both', sDesc, ];
                for sItem in asItems:
                    oItem = None;
                    for sOp in TestInOut.kasOperators:
                        off = sItem.find(sOp);
                        if off < 0:
                            continue;
                        sField     = sItem[:off];
                        sValueType = sItem[off + len(sOp):];
                        if     sField in TestInOut.kdFields \
                           and TestInOut.kdFields[sField][1] in asValidFieldKinds:
                            asSplit = sValueType.split(':', 1);
                            sValue  = asSplit[0];
                            sType   = asSplit[1] if len(asSplit) > 1 else TestInOut.kdFields[sField][0];
                            if sType in TestInOut.kdTypes:
                                oValid = TestInOut.kdTypes[sType].validate(sValue);
                                if oValid is True:
                                    if not TestInOut.kdTypes[sType].isAndOrPair(sValue) or sOp == '&|=':
                                        oItem = TestInOut(sField, sOp, sValue, sType);
                                    else:
                                        self.errorComment(iTagLine, '%s: and-or %s value "%s" can only be used with "&|="'
                                                                    % ( sTag, sDesc, sItem, ));
                                else:
                                    self.errorComment(iTagLine, '%s: invalid %s value "%s" in "%s" (type: %s): %s'
                                                                % ( sTag, sDesc, sValue, sItem, sType, oValid, ));
                            else:
                                self.errorComment(iTagLine, '%s: invalid %s type "%s" in "%s" (valid types: %s)'
                                                             % ( sTag, sDesc, sType, sItem, TestInOut.kdTypes.keys(),));
                        else:
                            self.errorComment(iTagLine, '%s: invalid %s field "%s" in "%s"\nvalid fields: %s'
                                                         % ( sTag, sDesc, sField, sItem,
                                                             ', '.join([sKey for sKey, asVal in TestInOut.kdFields.items()
                                                                        if asVal[1] in asValidFieldKinds]),));
                        break;
                    if oItem is not None:
                        for oExisting in aoDst:
                            if oExisting.sField == oItem.sField and oExisting.sOp == oItem.sOp:
                                self.errorComment(iTagLine,
                                                  '%s: already have a "%s" assignment for field "%s" (existing: %s, new: %s)'
                                                  % ( sTag, oItem.sOp, oItem.sField, oExisting, oItem,));
                        aoDst.append(oItem);
                    else:
                        fRc = self.errorComment(iTagLine, '%s: failed to parse assignment: %s' % ( sTag, sItem,));

            #
            # .
            #
            if fRc:
                oInstr.aoTests.append(oTest);
            else:
                self.errorComment(iTagLine, '%s: failed to parse test: %s' % (sTag, ' '.join(asWords),));
                self.errorComment(iTagLine, '%s: asSelectors=%s / asInputs=%s -> asOutputs=%s'
                                            % (sTag, asSelectors, asInputs, asOutputs,));

        _ = iEndLine;
        return True;

    def parseTagOpTestNum(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Numbered @optest tag.  Either @optest42 or @optest[42].
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        iTest = 0;
        if sTag[-1] == ']':
            iTest = int(sTag[8:-1]);
        else:
            iTest = int(sTag[7:]);

        if iTest != len(oInstr.aoTests):
            self.errorComment(iTagLine, '%s: incorrect test number: %u, actual %u' % (sTag, iTest, len(oInstr.aoTests),));
        return self.parseTagOpTest(sTag, aasSections, iTagLine, iEndLine);

    def parseTagOpTestIgnore(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @optestign | @optestignore
        Value:      <value is ignored>

        This is a simple trick to ignore a test while debugging another.

        See also @oponlytest.
        """
        _ = sTag; _ = aasSections; _ = iTagLine; _ = iEndLine;
        return True;

    def parseTagOpCopyTests(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opcopytests
        Value:      <opstat | function> [..]
        Example:    @opcopytests add_Eb_Gb

        Trick to avoid duplicating tests for different encodings of the same
        operation.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten, validate and append the copy job to the instruction.  We execute
        # them after parsing all the input so we can handle forward references.
        asToCopy = self.flattenAllSections(aasSections).split();
        if not asToCopy:
            return self.errorComment(iTagLine, '%s: requires at least on reference value' % (sTag,));
        for sToCopy in asToCopy:
            if sToCopy not in oInstr.asCopyTests:
                if self.oReStatsName.match(sToCopy) or self.oReFunctionName.match(sToCopy):
                    oInstr.asCopyTests.append(sToCopy);
                else:
                    self.errorComment(iTagLine, '%s: invalid instruction reference (opstat or function) "%s" (valid: %s or %s)'
                                                % (sTag, sToCopy, self.oReStatsName.pattern, self.oReFunctionName.pattern));
            else:
                self.errorComment(iTagLine, '%s: ignoring duplicate "%s"' % (sTag, sToCopy,));

        _ = iEndLine;
        return True;

    def parseTagOpOnlyTest(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @oponlytest | @oponly
        Value:      none

        Only test instructions with this tag.  This is a trick that is handy
        for singling out one or two new instructions or tests.

        See also @optestignore.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Validate and add instruction to only test dictionary.
        sValue = self.flattenAllSections(aasSections).strip();
        if sValue:
            return self.errorComment(iTagLine, '%s: does not take any value: %s' % (sTag, sValue));

        if oInstr not in g_aoOnlyTestInstructions:
            g_aoOnlyTestInstructions.append(oInstr);

        _ = iEndLine;
        return True;

    def parseTagOpXcptType(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opxcpttype
        Value:      [none|1|2|3|4|4UA|5|6|7|8|11|12|E1|E1NF|E2|E3|E3NF|E4|E4NF|E5|E5NF|E6|E6NF|E7NF|E9|E9NF|E10|E11|E12|E12NF]

        Sets the SSE or AVX exception type (see SDMv2 2.4, 2.7).
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten as a space separated list, split it up and validate the values.
        asTypes = self.flattenAllSections(aasSections).split();
        if len(asTypes) != 1:
            return self.errorComment(iTagLine, '%s: exactly one invalid exception type, please: %s' % (sTag, asTypes,));
        sType = asTypes[0];
        if sType not in g_kdXcptTypes:
            return self.errorComment(iTagLine, '%s: invalid invalid exception type: %s (valid: %s)'
                                               % (sTag, sType, sorted(g_kdXcptTypes.keys()),));
        # Set it.
        if oInstr.sXcptType is not None:
            return self.errorComment(iTagLine,
                                     '%s: attempting to overwrite "%s" with "%s" (only one @opxcpttype)'
                                     % ( sTag, oInstr.sXcptType, sType,));
        oInstr.sXcptType = sType;

        _ = iEndLine;
        return True;

    def parseTagOpFunction(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opfunction
        Value:      <VMM function name>

        This is for explicitly setting the IEM function name.  Normally we pick
        this up from the FNIEMOP_XXX macro invocation after the description, or
        generate it from the mnemonic and operands.

        It it thought it maybe necessary to set it when specifying instructions
        which implementation isn't following immediately or aren't implemented yet.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sFunction = self.flattenAllSections(aasSections);
        if not self.oReFunctionName.match(sFunction):
            return self.errorComment(iTagLine, '%s: invalid VMM function name: "%s" (valid: %s)'
                                               % (sTag, sFunction, self.oReFunctionName.pattern));

        if oInstr.sFunction is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite VMM function name "%s" with "%s"'
                                     % (sTag, oInstr.sFunction, sFunction,));
        oInstr.sFunction = sFunction;

        _ = iEndLine;
        return True;

    def parseTagOpStats(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:        @opstats
        Value:      <VMM statistics base name>

        This is for explicitly setting the statistics name.  Normally we pick
        this up from the IEMOP_MNEMONIC macro invocation, or generate it from
        the mnemonic and operands.

        It it thought it maybe necessary to set it when specifying instructions
        which implementation isn't following immediately or aren't implemented yet.
        """
        oInstr = self.ensureInstructionForOpTag(iTagLine);

        # Flatten and validate the value.
        sStats = self.flattenAllSections(aasSections);
        if not self.oReStatsName.match(sStats):
            return self.errorComment(iTagLine, '%s: invalid VMM statistics name: "%s" (valid: %s)'
                                               % (sTag, sStats, self.oReStatsName.pattern));

        if oInstr.sStats is not None:
            return self.errorComment(iTagLine, '%s: attempting to overwrite VMM statistics base name "%s" with "%s"'
                                     % (sTag, oInstr.sStats, sStats,));
        oInstr.sStats = sStats;

        _ = iEndLine;
        return True;

    def parseTagOpDone(self, sTag, aasSections, iTagLine, iEndLine):
        """
        Tag:    @opdone
        Value:  none

        Used to explictily flush the instructions that have been specified.
        """
        sFlattened = self.flattenAllSections(aasSections);
        if sFlattened != '':
            return self.errorComment(iTagLine, '%s: takes no value, found: "%s"' % (sTag, sFlattened,));
        _ = sTag; _ = iEndLine;
        return self.doneInstructions();

    ## @}


    def parseComment(self):
        """
        Parse the current comment (self.sComment).

        If it's a opcode specifiying comment, we reset the macro stuff.
        """
        #
        # Reject if comment doesn't seem to contain anything interesting.
        #
        if    self.sComment.find('Opcode') < 0 \
          and self.sComment.find('@') < 0:
            return False;

        #
        # Split the comment into lines, removing leading asterisks and spaces.
        # Also remove leading and trailing empty lines.
        #
        asLines = self.sComment.split('\n');
        for iLine, sLine in enumerate(asLines):
            asLines[iLine] = sLine.lstrip().lstrip('*').lstrip();

        while asLines and not asLines[0]:
            self.iCommentLine += 1;
            asLines.pop(0);

        while asLines and not asLines[-1]:
            asLines.pop(len(asLines) - 1);

        #
        # Check for old style: Opcode 0x0f 0x12
        #
        if asLines[0].startswith('Opcode '):
            self.parseCommentOldOpcode(asLines);

        #
        # Look for @op* tagged data.
        #
        cOpTags      = 0;
        sFlatDefault = None;
        sCurTag      = '@default';
        iCurTagLine  = 0;
        asCurSection = [];
        aasSections  = [ asCurSection, ];
        for iLine, sLine in enumerate(asLines):
            if not sLine.startswith('@'):
                if sLine:
                    asCurSection.append(sLine);
                elif asCurSection:
                    asCurSection = [];
                    aasSections.append(asCurSection);
            else:
                #
                # Process the previous tag.
                #
                if not asCurSection and len(aasSections) > 1:
                    aasSections.pop(-1);
                if sCurTag in self.dTagHandlers:
                    self.dTagHandlers[sCurTag](sCurTag, aasSections, iCurTagLine, iLine);
                    cOpTags += 1;
                elif sCurTag.startswith('@op'):
                    self.errorComment(iCurTagLine, 'Unknown tag: %s' % (sCurTag));
                elif sCurTag == '@default':
                    sFlatDefault = self.flattenAllSections(aasSections);
                elif '@op' + sCurTag[1:] in self.dTagHandlers:
                    self.errorComment(iCurTagLine, 'Did you mean "@op%s" rather than "%s"?' % (sCurTag[1:], sCurTag));
                elif sCurTag in ['@encoding', '@opencoding']:
                    self.errorComment(iCurTagLine, 'Did you mean "@openc" rather than "%s"?' % (sCurTag,));

                #
                # New tag.
                #
                asSplit = sLine.split(None, 1);
                sCurTag = asSplit[0].lower();
                if len(asSplit) > 1:
                    asCurSection = [asSplit[1],];
                else:
                    asCurSection = [];
                aasSections = [asCurSection, ];
                iCurTagLine = iLine;

        #
        # Process the final tag.
        #
        if not asCurSection and len(aasSections) > 1:
            aasSections.pop(-1);
        if sCurTag in self.dTagHandlers:
            self.dTagHandlers[sCurTag](sCurTag, aasSections, iCurTagLine, iLine);
            cOpTags += 1;
        elif sCurTag.startswith('@op'):
            self.errorComment(iCurTagLine, 'Unknown tag: %s' % (sCurTag));
        elif sCurTag == '@default':
            sFlatDefault = self.flattenAllSections(aasSections);

        #
        # Don't allow default text in blocks containing @op*.
        #
        if cOpTags > 0 and sFlatDefault:
            self.errorComment(0, 'Untagged comment text is not allowed with @op*: %s' % (sFlatDefault,));

        return True;

    def parseMacroInvocation(self, sInvocation, offStartInvocation = 0):
        """
        Parses a macro invocation.

        Returns three values:
            1. A list of macro arguments, where the zero'th is the macro name.
            2. The offset following the macro invocation, into sInvocation of
               this is on the same line or into the last line if it is on a
               different line.
            3. Number of additional lines the invocation spans (i.e. zero if
               it is all contained within sInvocation).
        """
        # First the name.
        offOpen = sInvocation.find('(', offStartInvocation);
        if offOpen <= offStartInvocation:
            self.raiseError("macro invocation open parenthesis not found");
        sName = sInvocation[offStartInvocation:offOpen].strip();
        if not self.oReMacroName.match(sName):
            self.raiseError("invalid macro name '%s'" % (sName,));
        asRet = [sName, ];

        # Arguments.
        iLine    = self.iLine;
        cDepth   = 1;
        off      = offOpen + 1;
        offStart = off;
        offCurLn = 0;
        chQuote  = None;
        while cDepth > 0:
            if off >= len(sInvocation):
                if iLine >= len(self.asLines):
                    self.error('macro invocation beyond end of file');
                    return (asRet, off - offCurLn, iLine - self.iLine);
                offCurLn     = off;
                sInvocation += self.asLines[iLine];
                iLine       += 1;
            ch = sInvocation[off];

            if chQuote:
                if ch == '\\' and off + 1 < len(sInvocation):
                    off += 1;
                elif ch == chQuote:
                    chQuote = None;
            elif ch in ('"', '\'',):
                chQuote = ch;
            elif ch in (',', ')',):
                if cDepth == 1:
                    asRet.append(sInvocation[offStart:off].strip());
                    offStart = off + 1;
                if ch == ')':
                    cDepth -= 1;
            elif ch == '(':
                cDepth += 1;
            off += 1;

        return (asRet, off - offCurLn, iLine - self.iLine);

    def findAndParseMacroInvocationEx(self, sCode, sMacro, offStart = 0):
        """
        Returns (None, len(sCode), 0) if not found, otherwise the
        parseMacroInvocation() return value.
        """
        offHit = sCode.find(sMacro, offStart);
        if offHit >= 0 and sCode[offHit + len(sMacro):].strip()[0] == '(':
            return self.parseMacroInvocation(sCode, offHit);
        return (None, len(sCode), 0);

    def findAndParseMacroInvocation(self, sCode, sMacro):
        """
        Returns None if not found, arguments as per parseMacroInvocation if found.
        """
        return self.findAndParseMacroInvocationEx(sCode, sMacro)[0];

    def findAndParseFirstMacroInvocation(self, sCode, asMacro):
        """
        Returns same as findAndParseMacroInvocation.
        """
        for sMacro in asMacro:
            asRet = self.findAndParseMacroInvocation(sCode, sMacro);
            if asRet is not None:
                return asRet;
        return None;

    def workerIemOpMnemonicEx(self, sMacro, sStats, sAsm, sForm, sUpper, sLower,  # pylint: disable=too-many-arguments
                              sDisHints, sIemHints, asOperands):
        """
        Processes one of the a IEMOP_MNEMONIC0EX, IEMOP_MNEMONIC1EX, IEMOP_MNEMONIC2EX,
        IEMOP_MNEMONIC3EX, and IEMOP_MNEMONIC4EX macros.
        """
        #
        # Some invocation checks.
        #
        if sUpper != sUpper.upper():
            self.error('%s: bad a_Upper parameter: %s' % (sMacro, sUpper,));
        if sLower != sLower.lower():
            self.error('%s: bad a_Lower parameter: %s' % (sMacro, sLower,));
        if sUpper.lower() != sLower:
            self.error('%s: a_Upper and a_Lower parameters does not match: %s vs %s' % (sMacro, sUpper, sLower,));
        if not self.oReMnemonic.match(sLower):
            self.error('%s: invalid a_Lower: %s  (valid: %s)' % (sMacro, sLower, self.oReMnemonic.pattern,));

        #
        # Check if sIemHints tells us to not consider this macro invocation.
        #
        if sIemHints.find('IEMOPHINT_SKIP_PYTHON') >= 0:
            return True;

        # Apply to the last instruction only for now.
        if not self.aoCurInstrs:
            self.addInstruction();
        oInstr = self.aoCurInstrs[-1];
        if oInstr.iLineMnemonicMacro == -1:
            oInstr.iLineMnemonicMacro = self.iLine;
        else:
            self.error('%s: already saw a IEMOP_MNEMONIC* macro on line %u for this instruction'
                       % (sMacro, oInstr.iLineMnemonicMacro,));

        # Mnemonic
        if oInstr.sMnemonic is None:
            oInstr.sMnemonic = sLower;
        elif oInstr.sMnemonic != sLower:
            self.error('%s: current instruction and a_Lower does not match: %s vs %s' % (sMacro, oInstr.sMnemonic, sLower,));

        # Process operands.
        if len(oInstr.aoOperands) not in [0, len(asOperands)]:
            self.error('%s: number of operands given by @opN does not match macro: %s vs %s'
                       % (sMacro, len(oInstr.aoOperands), len(asOperands),));
        for iOperand, sType in enumerate(asOperands):
            sWhere = g_kdOpTypes.get(sType, [None, None])[1];
            if sWhere is None:
                self.error('%s: unknown a_Op%u value: %s' % (sMacro, iOperand + 1, sType));
                if iOperand < len(oInstr.aoOperands): # error recovery.
                    sWhere = oInstr.aoOperands[iOperand].sWhere;
                    sType  = oInstr.aoOperands[iOperand].sType;
                else:
                    sWhere = 'reg';
                    sType  = 'Gb';
            if iOperand == len(oInstr.aoOperands):
                oInstr.aoOperands.append(Operand(sWhere, sType))
            elif oInstr.aoOperands[iOperand].sWhere != sWhere or oInstr.aoOperands[iOperand].sType != sType:
                self.error('%s: @op%u and a_Op%u mismatch: %s:%s vs %s:%s'
                           % (sMacro, iOperand + 1, iOperand + 1, oInstr.aoOperands[iOperand].sWhere,
                              oInstr.aoOperands[iOperand].sType, sWhere, sType,));

        # Encoding.
        if sForm not in g_kdIemForms:
            self.error('%s: unknown a_Form value: %s' % (sMacro, sForm,));
        else:
            if oInstr.sEncoding is None:
                oInstr.sEncoding = g_kdIemForms[sForm][0];
            elif g_kdIemForms[sForm][0] != oInstr.sEncoding:
                self.error('%s: current instruction @openc and a_Form does not match: %s vs %s (%s)'
                           % (sMacro, oInstr.sEncoding, g_kdIemForms[sForm], sForm));

            # Check the parameter locations for the encoding.
            if g_kdIemForms[sForm][1] is not None:
                if len(g_kdIemForms[sForm][1]) > len(oInstr.aoOperands):
                    self.error('%s: The a_Form=%s has a different operand count: %s (form) vs %s'
                               % (sMacro, sForm, len(g_kdIemForms[sForm][1]), len(oInstr.aoOperands) ));
                else:
                    for iOperand, sWhere in enumerate(g_kdIemForms[sForm][1]):
                        if oInstr.aoOperands[iOperand].sWhere != sWhere:
                            self.error('%s: current instruction @op%u and a_Form location does not match: %s vs %s (%s)'
                                       % (sMacro, iOperand + 1, oInstr.aoOperands[iOperand].sWhere, sWhere, sForm,));
                        sOpFormMatch = g_kdOpTypes[oInstr.aoOperands[iOperand].sType][4];
                        if    (sOpFormMatch in [ 'REG', 'MEM', ] and sForm.find('_' + sOpFormMatch) < 0) \
                           or (sOpFormMatch in [ 'FIXED', ]      and sForm.find(sOpFormMatch) < 0) \
                           or (sOpFormMatch == 'RM' and (sForm.find('_MEM') > 0 or sForm.find('_REG') > 0) ) \
                           or (sOpFormMatch == 'V'  and (   not (sForm.find('VEX') > 0 or sForm.find('XOP')) \
                                                         or sForm.replace('VEX','').find('V') < 0) ):
                            self.error('%s: current instruction @op%u and a_Form type does not match: %s/%s vs %s'
                                       % (sMacro, iOperand + 1, oInstr.aoOperands[iOperand].sType, sOpFormMatch, sForm, ));
                    if len(g_kdIemForms[sForm][1]) < len(oInstr.aoOperands):
                        for iOperand in range(len(g_kdIemForms[sForm][1]), len(oInstr.aoOperands)):
                            if    oInstr.aoOperands[iOperand].sType != 'FIXED' \
                              and g_kdOpTypes[oInstr.aoOperands[iOperand].sType][0] != 'IDX_ParseFixedReg':
                                self.error('%s: Expected FIXED type operand #%u following operands given by a_Form=%s: %s (%s)'
                                           % (sMacro, iOperand, sForm, oInstr.aoOperands[iOperand].sType,
                                              oInstr.aoOperands[iOperand].sWhere));


            # Check @opcodesub
            if oInstr.sSubOpcode \
              and g_kdIemForms[sForm][2] \
              and oInstr.sSubOpcode.find(g_kdIemForms[sForm][2]) < 0:
                self.error('%s: current instruction @opcodesub and a_Form does not match: %s vs %s (%s)'
                            % (sMacro, oInstr.sSubOpcode, g_kdIemForms[sForm][2], sForm,));

        # Stats.
        if not self.oReStatsName.match(sStats):
            self.error('%s: invalid a_Stats value: %s' % (sMacro, sStats,));
        elif oInstr.sStats is None:
            oInstr.sStats = sStats;
        elif oInstr.sStats != sStats:
            self.error('%s: mismatching @opstats and a_Stats value: %s vs %s'
                       % (sMacro, oInstr.sStats, sStats,));

        # Process the hints (simply merge with @ophints w/o checking anything).
        for sHint in sDisHints.split('|'):
            sHint = sHint.strip();
            if sHint.startswith('DISOPTYPE_'):
                sShortHint = sHint[len('DISOPTYPE_'):].lower();
                if sShortHint in g_kdHints:
                    oInstr.dHints[sShortHint] = True; # (dummy value, using dictionary for speed)
                else:
                    self.error('%s: unknown a_fDisHints value: %s' % (sMacro, sHint,));
            elif sHint != '0':
                self.error('%s: expected a_fDisHints value: %s' % (sMacro, sHint,));

        for sHint in sIemHints.split('|'):
            sHint = sHint.strip();
            if sHint.startswith('IEMOPHINT_'):
                sShortHint = sHint[len('IEMOPHINT_'):].lower();
                if sShortHint in g_kdHints:
                    oInstr.dHints[sShortHint] = True; # (dummy value, using dictionary for speed)
                else:
                    self.error('%s: unknown a_fIemHints value: %s' % (sMacro, sHint,));
            elif sHint != '0':
                self.error('%s: expected a_fIemHints value: %s' % (sMacro, sHint,));

        _ = sAsm;
        return True;

    def workerIemOpMnemonic(self, sMacro, sForm, sUpper, sLower, sDisHints, sIemHints, asOperands):
        """
        Processes one of the a IEMOP_MNEMONIC0, IEMOP_MNEMONIC1, IEMOP_MNEMONIC2,
        IEMOP_MNEMONIC3, and IEMOP_MNEMONIC4 macros.
        """
        if not asOperands:
            return self.workerIemOpMnemonicEx(sMacro, sLower, sLower, sForm, sUpper, sLower, sDisHints, sIemHints, asOperands);
        return self.workerIemOpMnemonicEx(sMacro, sLower + '_' + '_'.join(asOperands), sLower + ' ' + ','.join(asOperands),
                                          sForm, sUpper, sLower, sDisHints, sIemHints, asOperands);

    def workerIemMcBegin(self, sCode, offBeginStatementInCodeStr, offBeginStatementInLine):
        """
        Process a IEM_MC_BEGIN macro invocation.
        """
        if self.fDebugMc:
            self.debug('IEM_MC_BEGIN on %s off %s' % (self.iLine, offBeginStatementInLine,));
            #self.debug('%s<eos>' % (sCode,));

        # Check preconditions.
        if not self.oCurFunction:
            self.raiseError('IEM_MC_BEGIN w/o current function (%s)' % (sCode,));
        if self.oCurMcBlock:
            self.raiseError('IEM_MC_BEGIN before IEM_MC_END.  Previous IEM_MC_BEGIN at line %u' % (self.oCurMcBlock.iBeginLine,));

        # Figure out the indent level the block starts at, adjusting for expanded multiline macros.
        cchIndent = offBeginStatementInCodeStr;
        offPrevNewline = sCode.rfind('\n', 0, offBeginStatementInCodeStr);
        if offPrevNewline >= 0:
            cchIndent -= offPrevNewline + 1;
        #self.debug('cchIndent=%s offPrevNewline=%s sFunc=%s' % (cchIndent, offPrevNewline, self.oCurFunction.sName));

        # Start a new block.
        # But don't add it to the list unless the context matches the host architecture.
        self.oCurMcBlock = McBlock(self.sSrcFile, self.iLine, offBeginStatementInLine, self.oCurFunction, self.iMcBlockInFunc,
                                   oInstruction = self.aoCurInstrs[-1] if self.aoCurInstrs else None,
                                   cchIndent = cchIndent);
        try:
            if (   not self.aoCppCondStack
                or not self.sHostArch
                or self.PreprocessorConditional.isInBlockForArch(self.aoCppCondStack, self.sHostArch, self.iLine)):
                g_aoMcBlocks.append(self.oCurMcBlock);
                self.cTotalMcBlocks += 1;
        except Exception as oXcpt:
            self.raiseError(oXcpt.args[0]);

        if self.oCurMcBlock.oInstruction:
            self.oCurMcBlock.oInstruction.aoMcBlocks.append(self.oCurMcBlock);
        self.iMcBlockInFunc += 1;
        return True;

    @staticmethod
    def extractLinesFromMacroExpansionLine(sRawLine, offBegin, offEnd, sBeginStmt = 'IEM_MC_BEGIN'):
        """
        Helper used by workerIemMcEnd and workerIemMcDeferToCImplXRet for
        extracting a statement block from a string that's the result of macro
        expansion and therefore contains multiple "sub-lines" as it were.

        Returns list of lines covering offBegin thru offEnd in sRawLine.
        """

        off = sRawLine.find('\n', offEnd);
        if off > 0:
            sRawLine = sRawLine[:off + 1];

        off = sRawLine.rfind('\n', 0, offBegin) + 1;
        sRawLine = sRawLine[off:];
        if not sRawLine.strip().startswith(sBeginStmt):
            sRawLine = sRawLine[offBegin - off:]

        return [sLine + '\n' for sLine in sRawLine.split('\n')];

    def workerIemMcEnd(self, offEndStatementInLine):
        """
        Process a IEM_MC_END macro invocation.
        """
        if self.fDebugMc:
            self.debug('IEM_MC_END on %s off %s' % (self.iLine, offEndStatementInLine,));

        # Check preconditions.
        if not self.oCurMcBlock:
            self.raiseError('IEM_MC_END w/o IEM_MC_BEGIN.');

        #
        # HACK ALERT! For blocks originating from macro expansion the start and
        #             end line will be the same, but the line has multiple
        #             newlines inside it.  So, we have to do some extra tricks
        #             to get the lines out of there. We ASSUME macros aren't
        #             messy, but keep IEM_MC_BEGIN/END on separate lines.
        #
        if self.iLine > self.oCurMcBlock.iBeginLine:
            asLines = self.asLines[self.oCurMcBlock.iBeginLine - 1 : self.iLine];
            if not asLines[0].strip().startswith('IEM_MC_BEGIN'):
                self.raiseError('IEM_MC_BEGIN is not the first word on the line');

            # Hack alert! Detect mixed tail/head macros a la cmpxchg16b and split up the lines
            #             so we can deal correctly with IEM_MC_END below and everything else.
            for sLine in asLines:
                cNewLines = sLine.count('\n');
                assert cNewLines > 0;
                if cNewLines > 1:
                    asLines = self.extractLinesFromMacroExpansionLine(''.join(asLines),
                                                                      self.oCurMcBlock.offBeginLine,
                                                                        offEndStatementInLine
                                                                      + sum(len(s) for s in asLines)
                                                                      - len(asLines[-1]));
                    self.oCurMcBlock.iMacroExp = McBlock.kiMacroExp_Partial;
                    break;
        else:
            self.oCurMcBlock.iMacroExp = McBlock.kiMacroExp_Entire;
            asLines = self.extractLinesFromMacroExpansionLine(self.asLines[self.iLine - 1],
                                                              self.oCurMcBlock.offBeginLine, offEndStatementInLine);

        #
        # Strip anything following the IEM_MC_END(); statement in the final line,
        # so that we don't carry on any trailing 'break' after macro expansions
        # like for iemOp_movsb_Xb_Yb.
        #
        while asLines[-1].strip() == '':
            asLines.pop();
        sFinal        = asLines[-1];
        offFinalEnd   = sFinal.find('IEM_MC_END');
        offEndInFinal = offFinalEnd;
        if offFinalEnd < 0: self.raiseError('bogus IEM_MC_END: Not in final line: %s' % (sFinal,));
        offFinalEnd += len('IEM_MC_END');

        while sFinal[offFinalEnd].isspace():
            offFinalEnd += 1;
        if sFinal[offFinalEnd] != '(': self.raiseError('bogus IEM_MC_END: Expected "(" at %s: %s' % (offFinalEnd, sFinal,));
        offFinalEnd += 1;

        while sFinal[offFinalEnd].isspace():
            offFinalEnd += 1;
        if sFinal[offFinalEnd] != ')': self.raiseError('bogus IEM_MC_END: Expected ")" at %s: %s' % (offFinalEnd, sFinal,));
        offFinalEnd += 1;

        while sFinal[offFinalEnd].isspace():
            offFinalEnd += 1;
        if sFinal[offFinalEnd] != ';': self.raiseError('bogus IEM_MC_END: Expected ";" at %s: %s' % (offFinalEnd, sFinal,));
        offFinalEnd += 1;

        asLines[-1] = sFinal[: offFinalEnd];

        #
        # Complete and discard the current block.
        #
        self.oCurMcBlock.complete(self.iLine, offEndStatementInLine,
                                  offEndStatementInLine + offFinalEnd - offEndInFinal, asLines);
        self.oCurMcBlock = None;
        return True;

    def workerIemMcDeferToCImplXRet(self, sCode, offBeginStatementInCodeStr, offBeginStatementInLine, cParams):
        """
        Process a IEM_MC_DEFER_TO_CIMPL_[1-5]_RET macro invocation.
        """
        sStmt = 'IEM_MC_DEFER_TO_CIMPL_%d_RET' % (cParams,);
        if self.fDebugMc:
            self.debug('%s on %s off %s' % (sStmt, self.iLine, offBeginStatementInLine,));
            #self.debug('%s<eos>' % (sCode,));

        # Check preconditions.
        if not self.oCurFunction:
            self.raiseError('%s w/o current function (%s)' % (sStmt, sCode,));
        if self.oCurMcBlock:
            self.raiseError('%s inside IEM_MC_BEGIN block starting at line %u' % (sStmt, self.oCurMcBlock.iBeginLine,));

        # Figure out the indent level the block starts at, adjusting for expanded multiline macros.
        cchIndent = offBeginStatementInCodeStr;
        offPrevNewline = sCode.rfind('\n', 0, offBeginStatementInCodeStr);
        if offPrevNewline >= 0:
            cchIndent -= offPrevNewline + 1;
        #self.debug('cchIndent=%s offPrevNewline=%s sFunc=%s' % (cchIndent, offPrevNewline, self.oCurFunction.sName));

        # Start a new block.
        oMcBlock = McBlock(self.sSrcFile, self.iLine, offBeginStatementInLine, self.oCurFunction, self.iMcBlockInFunc,
                           oInstruction = self.aoCurInstrs[-1] if self.aoCurInstrs else None,
                           cchIndent = cchIndent, fDeferToCImpl = True);

        # Parse the statment.
        asArgs, offAfter, cLines = self.findAndParseMacroInvocationEx(sCode, sStmt, offBeginStatementInCodeStr);
        if asArgs is None:
            self.raiseError('%s: Closing parenthesis not found!' % (sStmt,));
        if len(asArgs) != cParams + 4:
            self.raiseError('%s: findAndParseMacroInvocationEx returns %s args, expected %s! (%s)'
                            % (sStmt, len(asArgs), cParams + 4, asArgs));

        oMcBlock.aoStmts = [ McBlock.parseMcDeferToCImpl(oMcBlock, asArgs[0], asArgs[1:]), ];

        # These MCs are not typically part of macro expansions, but let's get
        # it out of the way immediately if it's the case.
        if cLines > 0 or self.asLines[oMcBlock.iBeginLine - 1].count('\n') <= 1:
            asLines = self.asLines[self.iLine - 1 : self.iLine - 1 + cLines + 1];
            assert offAfter < len(asLines[-1]) and asLines[-1][offAfter] == ';', \
                   'iBeginLine=%d iLine=%d offAfter=%s line: "%s"' % (oMcBlock.iBeginLine, self.iLine, offAfter, asLines[-1],);
            asLines[-1] = asLines[-1][:offAfter + 1];
        else:
            asLines = self.extractLinesFromMacroExpansionLine(self.asLines[self.iLine - 1], offBeginStatementInCodeStr,
                                                              offAfter, sStmt);
            assert asLines[-1].find(';') >= 0;
            asLines[-1] = asLines[-1][:asLines[-1].find(';') + 1];

        assert asLines[0].find(sStmt) >= 0;
        #if not asLines[0].strip().startswith(sStmt):
        #    self.raiseError('%s is not the first word on the line: %s' % (sStmt, asLines[0].strip()));

        # Advance to the line with the closing ')'.
        self.iLine += cLines;

        # Complete the block.
        oMcBlock.complete(self.iLine, 0 if cLines > 0 else offBeginStatementInCodeStr, offAfter + 1, asLines);

        g_aoMcBlocks.append(oMcBlock);
        if oMcBlock.oInstruction:
            oMcBlock.oInstruction.aoMcBlocks.append(oMcBlock);
        self.cTotalMcBlocks += 1;
        self.iMcBlockInFunc += 1;

        return True;

    def workerStartFunction(self, asArgs):
        """
        Deals with the start of a decoder function.

        These are all defined using one of the FNIEMOP*_DEF* and FNIEMOP_*STUB*
        macros, so we get a argument list for these where the 0th argument is the
        macro name.
        """
        # Complete any existing function.
        if self.oCurFunction:
            self.oCurFunction.complete(self.iLine - 1, self.asLines[self.oCurFunction.iBeginLine - 1 : self.iLine - 1]);

        # Create the new function.
        self.oCurFunction = DecoderFunction(self.sSrcFile, self.iLine, asArgs[1], asArgs);
        return True;

    def checkCodeForMacro(self, sCode, offLine):
        """
        Checks code for relevant macro invocation.
        """

        #
        # Scan macro invocations.
        #
        if sCode.find('(') > 0:
            # Look for instruction decoder function definitions. ASSUME single line.
            asArgs = self.findAndParseFirstMacroInvocation(sCode,
                                                           [ 'FNIEMOP_DEF',
                                                             'FNIEMOPRM_DEF',
                                                             'FNIEMOP_STUB',
                                                             'FNIEMOP_STUB_1',
                                                             'FNIEMOP_UD_STUB',
                                                             'FNIEMOP_UD_STUB_1' ]);
            if asArgs is not None:
                self.workerStartFunction(asArgs);
                #self.debug('%s: oCurFunction=%s' % (self.iLine, self.oCurFunction.sName,));

                if not self.aoCurInstrs:
                    self.addInstruction();
                for oInstr in self.aoCurInstrs:
                    if oInstr.iLineFnIemOpMacro == -1:
                        oInstr.iLineFnIemOpMacro = self.iLine;
                    else:
                        self.error('%s: already seen a FNIEMOP_XXX macro for %s' % (asArgs[0], oInstr,) );
                self.setInstrunctionAttrib('sFunction', asArgs[1]);
                self.setInstrunctionAttrib('fStub', asArgs[0].find('STUB') > 0, fOverwrite = True);
                self.setInstrunctionAttrib('fUdStub', asArgs[0].find('UD_STUB') > 0, fOverwrite = True);
                if asArgs[0].find('STUB') > 0:
                    self.doneInstructions(fEndOfFunction = True);
                return True;

            # Check for worker function definitions, so we can get a context for MC blocks.
            asArgs = self.findAndParseFirstMacroInvocation(sCode,
                                                           [ 'FNIEMOP_DEF_1',
                                                             'FNIEMOP_DEF_2', ]);
            if asArgs is not None:
                self.workerStartFunction(asArgs);
                #self.debug('%s: oCurFunction=%s (%s)' % (self.iLine, self.oCurFunction.sName, asArgs[0]));
                return True;

            # IEMOP_HLP_DONE_VEX_DECODING_*
            asArgs = self.findAndParseFirstMacroInvocation(sCode,
                                                           [ 'IEMOP_HLP_DONE_VEX_DECODING',
                                                             'IEMOP_HLP_DONE_VEX_DECODING_L0',
                                                             'IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV',
                                                             'IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV',
                                                             ]);
            if asArgs is not None:
                sMacro = asArgs[0];
                if sMacro in ('IEMOP_HLP_DONE_VEX_DECODING_L0', 'IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV', ):
                    for oInstr in self.aoCurInstrs:
                        if 'vex_l_zero' not in oInstr.dHints:
                            if oInstr.iLineMnemonicMacro >= 0:
                                self.errorOnLine(oInstr.iLineMnemonicMacro,
                                                 'Missing IEMOPHINT_VEX_L_ZERO! (%s on line %d)' % (sMacro, self.iLine,));
                            oInstr.dHints['vex_l_zero'] = True;

            #
            # IEMOP_MNEMONIC*
            #
            if sCode.find('IEMOP_MNEMONIC') >= 0:
                # IEMOP_MNEMONIC(a_Stats, a_szMnemonic) IEMOP_INC_STATS(a_Stats)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC');
                if asArgs is not None:
                    if len(self.aoCurInstrs) == 1:
                        oInstr = self.aoCurInstrs[0];
                        if oInstr.sStats is None:
                            oInstr.sStats = asArgs[1];
                        self.deriveMnemonicAndOperandsFromStats(oInstr, asArgs[1]);

                # IEMOP_MNEMONIC0EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC0EX');
                if asArgs is not None:
                    self.workerIemOpMnemonicEx(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], asArgs[6],
                                               asArgs[7], []);
                # IEMOP_MNEMONIC1EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC1EX');
                if asArgs is not None:
                    self.workerIemOpMnemonicEx(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], asArgs[7],
                                               asArgs[8], [asArgs[6],]);
                # IEMOP_MNEMONIC2EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC2EX');
                if asArgs is not None:
                    self.workerIemOpMnemonicEx(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], asArgs[8],
                                               asArgs[9], [asArgs[6], asArgs[7]]);
                # IEMOP_MNEMONIC3EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints,
                #                   a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC3EX');
                if asArgs is not None:
                    self.workerIemOpMnemonicEx(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], asArgs[9],
                                               asArgs[10], [asArgs[6], asArgs[7], asArgs[8],]);
                # IEMOP_MNEMONIC4EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints,
                #                   a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC4EX');
                if asArgs is not None:
                    self.workerIemOpMnemonicEx(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], asArgs[10],
                                               asArgs[11], [asArgs[6], asArgs[7], asArgs[8], asArgs[9],]);

                # IEMOP_MNEMONIC0(a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC0');
                if asArgs is not None:
                    self.workerIemOpMnemonic(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[4], asArgs[5], []);
                # IEMOP_MNEMONIC1(a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC1');
                if asArgs is not None:
                    self.workerIemOpMnemonic(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[5], asArgs[6], [asArgs[4],]);
                # IEMOP_MNEMONIC2(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC2');
                if asArgs is not None:
                    self.workerIemOpMnemonic(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[6], asArgs[7],
                                             [asArgs[4], asArgs[5],]);
                # IEMOP_MNEMONIC3(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC3');
                if asArgs is not None:
                    self.workerIemOpMnemonic(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[7], asArgs[8],
                                             [asArgs[4], asArgs[5], asArgs[6],]);
                # IEMOP_MNEMONIC4(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints, a_fIemHints)
                asArgs = self.findAndParseMacroInvocation(sCode, 'IEMOP_MNEMONIC4');
                if asArgs is not None:
                    self.workerIemOpMnemonic(asArgs[0], asArgs[1], asArgs[2], asArgs[3], asArgs[8], asArgs[9],
                                             [asArgs[4], asArgs[5], asArgs[6], asArgs[7],]);

            #
            # IEM_MC_BEGIN + IEM_MC_END.
            # We must support multiple instances per code snippet.
            #
            offCode = sCode.find('IEM_MC_');
            if offCode >= 0:
                for oMatch in self.oReMcBeginEnd.finditer(sCode, offCode):
                    if oMatch.group(1) == 'END':
                        self.workerIemMcEnd(offLine + oMatch.start());
                    elif oMatch.group(1) == 'BEGIN':
                        self.workerIemMcBegin(sCode, oMatch.start(), offLine + oMatch.start());
                    else:
                        self.workerIemMcDeferToCImplXRet(sCode, oMatch.start(), offLine + oMatch.start(),
                                                         int(oMatch.group(1)[len('DEFER_TO_CIMPL_')]));
                return True;

        return False;

    def workerPreprocessorRecreateMacroRegex(self):
        """
        Recreates self.oReMacros when self.dMacros changes.
        """
        if self.dMacros:
            sRegex = '';
            for sName, oMacro in self.dMacros.items():
                if sRegex:
                    sRegex += r'|' + sName;
                else:
                    sRegex  = r'\b(' + sName;
                if oMacro.asArgs is not None:
                    sRegex += r'\s*\(';
                else:
                    sRegex += r'\b';
            sRegex += ')';
            self.oReMacros = re.compile(sRegex);
        else:
            self.oReMacros = None;
        return True;

    def workerPreprocessorDefine(self, sRest):
        """
        Handles a macro #define, the sRest is what follows after the directive word.
        """
        assert sRest[-1] == '\n';

        #
        # If using line continutation, just concat all the lines together,
        # preserving the newline character but not the escaping.
        #
        iLineStart = self.iLine;
        while sRest.endswith('\\\n') and self.iLine < len(self.asLines):
            sRest = sRest[0:-2].rstrip() + '\n' + self.asLines[self.iLine];
            self.iLine += 1;
        #self.debug('workerPreprocessorDefine: sRest=%s<EOS>' % (sRest,));

        #
        # Use regex to split out the name, argument list and body.
        # If this fails, we assume it's a simple macro.
        #
        oMatch = self.oReHashDefine2.match(sRest);
        if oMatch:
            sAllArgs = oMatch.group(2).strip();
            asArgs = [sParam.strip() for sParam in sAllArgs.split(',')] if sAllArgs else None;
            sBody  = oMatch.group(3);
        else:
            oMatch = self.oReHashDefine3.match(sRest);
            if not oMatch:
                self.debug('workerPreprocessorDefine: wtf? sRest=%s' % (sRest,));
                return self.error('bogus macro definition: %s' % (sRest,));
            asArgs = None;
            sBody  = oMatch.group(2);
        sName = oMatch.group(1);
        assert sName == sName.strip();
        #self.debug('workerPreprocessorDefine: sName=%s asArgs=%s sBody=%s<EOS>' % (sName, asArgs, sBody));

        #
        # Is this of any interest to us?  We do NOT support MC blocks wihtin
        # nested macro expansion, just to avoid lots of extra work.
        #
        # There is only limited support for macros expanding to partial MC blocks.
        #
        # Note! IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX and other macros someone making
        #       use of IEMOP_RAISE_INVALID_LOCK_PREFIX_RET() will be ignored here and
        #       dealt with by overriding IEMOP_RAISE_INVALID_LOCK_PREFIX_RET and its
        #       siblings in the recompiler.  This is a lot simpler than nested macro
        #       expansion and lots of heuristics for locating all the relevant macros.
        #       Also, this way we don't produce lots of unnecessary threaded functions.
        #
        if sBody.find("IEM_MC_BEGIN") < 0 and sBody.find("IEM_MC_END") < 0:
            #self.debug('workerPreprocessorDefine: irrelevant (%s: %s)' % (sName, sBody));
            return True;

        #
        # Add the macro.
        #
        if self.fDebugPreproc:
            self.debug('#define %s on line %u' % (sName, self.iLine,));
        self.dMacros[sName] = SimpleParser.Macro(sName, asArgs, sBody.strip(), iLineStart);
        return self.workerPreprocessorRecreateMacroRegex();

    def workerPreprocessorUndef(self, sRest):
        """
        Handles a macro #undef, the sRest is what follows after the directive word.
        """
        # Quick comment strip and isolate the name.
        offSlash = sRest.find('/');
        if offSlash > 0:
            sRest = sRest[:offSlash];
        sName = sRest.strip();

        # Remove the macro if we're clocking it.
        if sName in self.dMacros:
            if self.fDebugPreproc:
                self.debug('#undef %s on line %u' % (sName, self.iLine,));
            del self.dMacros[sName];
            return self.workerPreprocessorRecreateMacroRegex();

        return True;

    def workerPreprocessorIfOrElif(self, sDirective, sRest):
        """
        Handles an #if, #ifdef, #ifndef or #elif directive.
        """
        #
        # Sanity check #elif.
        #
        if sDirective == 'elif':
            if len(self.aoCppCondStack) == 0:
                self.raiseError('#elif without #if');
            if self.aoCppCondStack[-1].fInElse:
                self.raiseError('#elif after #else');

        #
        # If using line continutation, just concat all the lines together,
        # stripping both the newline and escape characters.
        #
        while sRest.endswith('\\\n') and self.iLine < len(self.asLines):
            sRest = sRest[0:-2].rstrip() + ' ' + self.asLines[self.iLine];
            self.iLine += 1;

        # Strip it of all comments and leading and trailing blanks.
        sRest = self.stripComments(sRest).strip();

        #
        # Stash it.
        #
        try:
            oPreprocCond = self.PreprocessorConditional(sDirective, sRest);
        except Exception as oXcpt:
            self.raiseError(oXcpt.args[0]);

        if sDirective == 'elif':
            self.aoCppCondStack[-1].aoElif.append(oPreprocCond);
        else:
            self.aoCppCondStack.append(oPreprocCond);

        return True;

    def workerPreprocessorElse(self):
        """
        Handles an #else directive.
        """
        if len(self.aoCppCondStack) == 0:
            self.raiseError('#else without #if');
        if self.aoCppCondStack[-1].fInElse:
            self.raiseError('Another #else after #else');

        self.aoCppCondStack[-1].fInElse = True;
        return True;

    def workerPreprocessorEndif(self):
        """
        Handles an #endif directive.
        """
        if len(self.aoCppCondStack) == 0:
            self.raiseError('#endif without #if');

        self.aoCppCondStack.pop();
        return True;

    def checkPreprocessorDirective(self, sLine):
        """
        Handles a preprocessor directive.
        """
        # Skip past the preprocessor hash.
        off = sLine.find('#');
        assert off >= 0;
        off += 1;
        while off < len(sLine) and sLine[off].isspace():
            off += 1;

        # Extract the directive.
        offDirective = off;
        while off < len(sLine) and not sLine[off].isspace():
            off += 1;
        sDirective = sLine[offDirective:off];
        if self.fDebugPreproc:
            self.debug('line %d: #%s...' % (self.iLine, sDirective));

        # Skip spaces following it to where the arguments/whatever starts.
        while off + 1 < len(sLine) and sLine[off + 1].isspace():
            off += 1;
        sTail = sLine[off:];

        # Handle the directive.
        if sDirective == 'define':
            return self.workerPreprocessorDefine(sTail);
        if sDirective == 'undef':
            return self.workerPreprocessorUndef(sTail);
        if sDirective in ('if', 'ifdef', 'ifndef', 'elif',):
            return self.workerPreprocessorIfOrElif(sDirective, sTail);
        if sDirective == 'else':
            return self.workerPreprocessorElse();
        if sDirective == 'endif':
            return self.workerPreprocessorEndif();

        if self.fDebugPreproc:
            self.debug('line %d: Unknown preprocessor directive: %s' % (self.iLine, sDirective));
        return False;

    def expandMacros(self, sLine, oMatch):
        """
        Expands macros we know about in the given line.
        Currently we ASSUME there is only one and that is what oMatch matched.
        """
        #
        # Get our bearings.
        #
        offMatch  = oMatch.start();
        sName     = oMatch.group(1);
        assert sName == sLine[oMatch.start() : oMatch.end()];
        fWithArgs = sName.endswith('(');
        if fWithArgs:
            sName = sName[:-1].strip();
        oMacro = self.dMacros[sName]        # type: SimpleParser.Macro

        #
        # Deal with simple macro invocations w/o parameters.
        #
        if not fWithArgs:
            if self.fDebugPreproc:
                self.debug('expanding simple macro %s on line %u' % (sName, self.iLine,));
            return sLine[:offMatch] + oMacro.expandMacro(self) + sLine[oMatch.end():];

        #
        # Complicated macro with parameters.
        # Start by extracting the parameters. ASSUMES they are all on the same line!
        #
        cLevel        = 1;
        offCur        = oMatch.end();
        offCurArg     = offCur;
        asArgs        = [];
        while True:
            if offCur >= len(sLine):
                self.raiseError('expandMacros: Invocation of macro %s spans multiple lines!' % (sName,));
            ch = sLine[offCur];
            if ch == '(':
                cLevel += 1;
            elif ch == ')':
                cLevel -= 1;
                if cLevel == 0:
                    asArgs.append(sLine[offCurArg:offCur].strip());
                    break;
            elif ch == ',' and cLevel == 1:
                asArgs.append(sLine[offCurArg:offCur].strip());
                offCurArg = offCur + 1;
            offCur += 1;
        if len(oMacro.asArgs) == 0 and len(asArgs) == 1 and asArgs[0] == '': # trick for empty parameter list.
            asArgs = [];
        if len(oMacro.asArgs) != len(asArgs):
            self.raiseError('expandMacros: Argument mismatch in %s invocation' % (oMacro.sName,));

        #
        # Do the expanding.
        #
        if self.fDebugPreproc:
            self.debug('expanding macro %s on line %u with arguments %s' % (sName, self.iLine, asArgs));
        return sLine[:offMatch] + oMacro.expandMacro(self, asArgs) + sLine[offCur + 1 :];

    def parse(self):
        """
        Parses the given file.

        Returns number or errors.
        Raises exception on fatal trouble.
        """
        #self.debug('Parsing %s' % (self.sSrcFile,));

        #
        # Loop thru the lines.
        #
        # Please mind that self.iLine may be updated by checkCodeForMacro and
        # other worker methods.
        #
        while self.iLine < len(self.asLines):
            sLine = self.asLines[self.iLine];
            self.iLine  += 1;
            #self.debug('line %u: %s' % (self.iLine, sLine[:-1]));

            # Expand macros we know about if we're currently in code.
            if self.iState == self.kiCode and self.oReMacros:
                oMatch = self.oReMacros.search(sLine);
                if oMatch:
                    sLine = self.expandMacros(sLine, oMatch);
                    if self.fDebugPreproc:
                        self.debug('line %d: expanded\n%s ==>\n%s' % (self.iLine, self.asLines[self.iLine - 1], sLine[:-1],));
                    self.asLines[self.iLine - 1] = sLine;

            # Check for preprocessor directives before comments and other stuff.
            # ASSUMES preprocessor directives doesn't end with multiline comments.
            if self.iState == self.kiCode and sLine.lstrip().startswith('#'):
                if self.fDebugPreproc:
                    self.debug('line %d: preproc' % (self.iLine,));
                self.checkPreprocessorDirective(sLine);
            else:
                # Look for comments.
                offSlash = sLine.find('/');
                if offSlash >= 0:
                    if offSlash + 1 >= len(sLine)  or  sLine[offSlash + 1] != '/'  or  self.iState != self.kiCode:
                        offLine = 0;
                        while offLine < len(sLine):
                            if self.iState == self.kiCode:
                                # Look for substantial multiline comment so we pass the following MC as a whole line:
                                #   IEM_MC_ARG_CONST(uint8_t, bImmArg, /*=*/ bImm,   2);
                                # Note! We ignore C++ comments here, assuming these aren't used in lines with C-style comments.
                                offHit = sLine.find('/*', offLine);
                                while offHit >= 0:
                                    offEnd = sLine.find('*/', offHit + 2);
                                    if offEnd < 0 or offEnd - offHit >= 16: # 16 chars is a bit random.
                                        break;
                                    offHit = sLine.find('/*', offEnd);

                                if offHit >= 0:
                                    self.checkCodeForMacro(sLine[offLine:offHit], offLine);
                                    self.sComment     = '';
                                    self.iCommentLine = self.iLine;
                                    self.iState       = self.kiCommentMulti;
                                    offLine = offHit + 2;
                                else:
                                    self.checkCodeForMacro(sLine[offLine:], offLine);
                                    offLine = len(sLine);

                            elif self.iState == self.kiCommentMulti:
                                offHit = sLine.find('*/', offLine);
                                if offHit >= 0:
                                    self.sComment += sLine[offLine:offHit];
                                    self.iState    = self.kiCode;
                                    offLine = offHit + 2;
                                    self.parseComment();
                                else:
                                    self.sComment += sLine[offLine:];
                                    offLine = len(sLine);
                            else:
                                assert False;
                    # C++ line comment.
                    elif offSlash > 0:
                        self.checkCodeForMacro(sLine[:offSlash], 0);

                # No slash, but append the line if in multi-line comment.
                elif self.iState == self.kiCommentMulti:
                    #self.debug('line %d: multi' % (self.iLine,));
                    self.sComment += sLine;

                # No slash, but check code line for relevant macro.
                elif (     self.iState == self.kiCode
                      and (sLine.find('IEMOP_') >= 0 or sLine.find('FNIEMOPRM_DEF') >= 0 or sLine.find('IEM_MC') >= 0)):
                    #self.debug('line %d: macro' % (self.iLine,));
                    self.checkCodeForMacro(sLine, 0);

                # If the line is a '}' in the first position, complete the instructions.
                elif self.iState == self.kiCode and sLine[0] == '}':
                    #self.debug('line %d: }' % (self.iLine,));
                    self.doneInstructions(fEndOfFunction = True);

                # Look for instruction table on the form 'IEM_STATIC const PFNIEMOP g_apfnVexMap3'
                # so we can check/add @oppfx info from it.
                elif self.iState == self.kiCode and sLine.find('PFNIEMOP') > 0 and self.oReFunTable.match(sLine):
                    self.parseFunctionTable(sLine);

        self.doneInstructions(fEndOfFunction = True);
        self.debug('%3s%% / %3s stubs out of %4s instructions and %4s MC blocks in %s'
                   % (self.cTotalStubs * 100 // max(self.cTotalInstr, 1), self.cTotalStubs, self.cTotalInstr,
                      self.cTotalMcBlocks, os.path.basename(self.sSrcFile),));
        return self.printErrors();

# Some sanity checking.
def __sanityCheckEFlagsClasses():
    for sClass, dLists in SimpleParser.kdEFlagsClasses.items():
        for sAttrib, asFlags in dLists.items():
            for sFlag in asFlags:
                assert sFlag in g_kdEFlagsMnemonics, 'sClass=%s sAttrib=%s sFlag=%s' % (sClass, sAttrib, sFlag,);
__sanityCheckEFlagsClasses();

## The parsed content of IEMAllInstCommonBodyMacros.h.
g_oParsedCommonBodyMacros = None # type: SimpleParser

def __parseFileByName(sSrcFile, sDefaultMap, sHostArch):
    """
    Parses one source file for instruction specfications.
    """
    #
    # Read sSrcFile into a line array.
    #
    try:
        oFile = open(sSrcFile, "r");    # pylint: disable=consider-using-with,unspecified-encoding
    except Exception as oXcpt:
        raise Exception("failed to open %s for reading: %s" % (sSrcFile, oXcpt,));
    try:
        asLines = oFile.readlines();
    except Exception as oXcpt:
        raise Exception("failed to read %s: %s" % (sSrcFile, oXcpt,));
    finally:
        oFile.close();

    #
    # On the first call, we parse IEMAllInstCommonBodyMacros.h so we
    # can use the macros from it when processing the other files.
    #
    global g_oParsedCommonBodyMacros;
    if g_oParsedCommonBodyMacros is None:
        # Locate the file.
        sCommonBodyMacros = os.path.join(os.path.split(sSrcFile)[0], 'IEMAllInstCommonBodyMacros.h');
        if not os.path.isfile(sCommonBodyMacros):
            sCommonBodyMacros = os.path.join(os.path.split(__file__)[0], 'IEMAllInstCommonBodyMacros.h');

        # Read it.
        try:
            with open(sCommonBodyMacros, "r") as oIncFile: # pylint: disable=unspecified-encoding
                asIncFiles = oIncFile.readlines();
        except Exception as oXcpt:
            raise Exception("failed to open/read %s: %s" % (sCommonBodyMacros, oXcpt,));

        # Parse it.
        try:
            oParser = SimpleParser(sCommonBodyMacros, asIncFiles, 'one', sHostArch);
            if oParser.parse() != 0:
                raise ParserException('%s: errors: See above' % (sCommonBodyMacros, ));
            if oParser.cTotalInstr != 0 or oParser.cTotalStubs != 0 or oParser.cTotalTagged != 0 or oParser.cTotalMcBlocks != 0:
                raise ParserException('%s: error: Unexpectedly found %u instr, %u tags, %u stubs and %u MCs, expecting zero. %s'
                                      % (sCommonBodyMacros, oParser.cTotalInstr, oParser.cTotalStubs, oParser.cTotalTagged,
                                         oParser.cTotalMcBlocks,
                                         ', '.join(sorted(  [str(oMcBlock.iBeginLine) for oMcBlock in g_aoMcBlocks]
                                                          + [str(oInstr.iLineCreated) for oInstr in g_aoAllInstructions])),));
        except ParserException as oXcpt:
            print(str(oXcpt), file = sys.stderr);
            raise;
        g_oParsedCommonBodyMacros = oParser;

    #
    # Do the parsing.
    #
    try:
        oParser = SimpleParser(sSrcFile, asLines, sDefaultMap, sHostArch, g_oParsedCommonBodyMacros);
        return (oParser.parse(), oParser) ;
    except ParserException as oXcpt:
        print(str(oXcpt), file = sys.stderr);
        raise;


def __doTestCopying():
    """
    Executes the asCopyTests instructions.
    """
    asErrors = [];
    for oDstInstr in g_aoAllInstructions:
        if oDstInstr.asCopyTests:
            for sSrcInstr in oDstInstr.asCopyTests:
                oSrcInstr = g_dAllInstructionsByStat.get(sSrcInstr, None);
                if oSrcInstr:
                    aoSrcInstrs = [oSrcInstr,];
                else:
                    aoSrcInstrs = g_dAllInstructionsByFunction.get(sSrcInstr, []);
                if aoSrcInstrs:
                    for oSrcInstr in aoSrcInstrs:
                        if oSrcInstr != oDstInstr:
                            oDstInstr.aoTests.extend(oSrcInstr.aoTests);
                        else:
                            asErrors.append('%s:%s: error: @opcopytests reference "%s" matches the destination\n'
                                            % ( oDstInstr.sSrcFile, oDstInstr.iLineCreated, sSrcInstr));
                else:
                    asErrors.append('%s:%s: error: @opcopytests reference "%s" not found\n'
                                    % ( oDstInstr.sSrcFile, oDstInstr.iLineCreated, sSrcInstr));

    if asErrors:
        sys.stderr.write(u''.join(asErrors));
    return len(asErrors);


def __applyOnlyTest():
    """
    If g_aoOnlyTestInstructions contains any instructions, drop aoTests from
    all other instructions so that only these get tested.
    """
    if g_aoOnlyTestInstructions:
        for oInstr in g_aoAllInstructions:
            if oInstr.aoTests:
                if oInstr not in g_aoOnlyTestInstructions:
                    oInstr.aoTests = [];
    return 0;

## List of all main instruction files, their default maps and file sets (-1 means included it all sets).
g_aaoAllInstrFilesAndDefaultMapAndSet = (
    ( 'IEMAllInstCommon.cpp.h',    'one',       -1, ),
    ( 'IEMAllInstOneByte.cpp.h',   'one',        1, ),
    ( 'IEMAllInst3DNow.cpp.h',     '3dnow',      2, ),
    ( 'IEMAllInstTwoByte0f.cpp.h', 'two0f',      2, ),
    ( 'IEMAllInstThree0f38.cpp.h', 'three0f38',  3, ),
    ( 'IEMAllInstThree0f3a.cpp.h', 'three0f3a',  3, ),
    ( 'IEMAllInstVexMap1.cpp.h',   'vexmap1',    4, ),
    ( 'IEMAllInstVexMap2.cpp.h',   'vexmap2',    4, ),
    ( 'IEMAllInstVexMap3.cpp.h',   'vexmap3',    4, ),
);

def __parseFilesWorker(asFilesAndDefaultMap, sHostArch):
    """
    Parses all the IEMAllInstruction*.cpp.h files.

    Returns a list of the parsers on success.
    Raises exception on failure.
    """
    sSrcDir   = os.path.dirname(os.path.abspath(__file__));
    cErrors   = 0;
    aoParsers = [];
    for sFilename, sDefaultMap in asFilesAndDefaultMap:
        if not os.path.split(sFilename)[0] and not os.path.exists(sFilename):
            sFilename = os.path.join(sSrcDir, sFilename);
        cThisErrors, oParser = __parseFileByName(sFilename, sDefaultMap, sHostArch);
        cErrors += cThisErrors;
        aoParsers.append(oParser);
    cErrors += __doTestCopying();
    cErrors += __applyOnlyTest();

    # Total stub stats:
    cTotalStubs = 0;
    for oInstr in g_aoAllInstructions:
        cTotalStubs += oInstr.fStub;
    print('debug: %3s%% / %3s stubs out of %4s instructions and %4s MC blocks in total'
          % (cTotalStubs * 100 // len(g_aoAllInstructions), cTotalStubs, len(g_aoAllInstructions), len(g_aoMcBlocks),),
          file = sys.stderr);

    if cErrors != 0:
        raise Exception('%d parse errors' % (cErrors,));
    return aoParsers;


def parseFiles(asFiles, sHostArch = None):
    """
    Parses a selection of IEMAllInstruction*.cpp.h files.

    Returns a list of the parsers on success.
    Raises exception on failure.
    """
    # Look up default maps for the files and call __parseFilesWorker to do the job.
    asFilesAndDefaultMap = [];
    for sFilename in asFiles:
        sName = os.path.split(sFilename)[1].lower();
        sMap  = None;
        for aoInfo in g_aaoAllInstrFilesAndDefaultMapAndSet:
            if aoInfo[0].lower() == sName:
                sMap = aoInfo[1];
                break;
        if not sMap:
            raise Exception('Unable to classify file: %s' % (sFilename,));
        asFilesAndDefaultMap.append((sFilename, sMap));

    return __parseFilesWorker(asFilesAndDefaultMap, sHostArch);


def parseAll(sHostArch = None):
    """
    Parses all the IEMAllInstruction*.cpp.h files.

    Returns a list of the parsers on success.
    Raises exception on failure.
    """
    return __parseFilesWorker([aoInfo[0:2] for aoInfo in g_aaoAllInstrFilesAndDefaultMapAndSet], sHostArch);


#
# Generators (may perhaps move later).
#
def __formatDisassemblerTableEntry(oInstr):
    """
    """
    sMacro = 'OP';
    cMaxOperands = 3;
    if len(oInstr.aoOperands) > 3:
        sMacro = 'OPVEX'
        cMaxOperands = 4;
        assert len(oInstr.aoOperands) <= cMaxOperands;

    #
    # Format string.
    #
    sTmp = '%s("%s' % (sMacro, oInstr.sMnemonic,);
    for iOperand, oOperand in enumerate(oInstr.aoOperands):
        sTmp += ' ' if iOperand == 0 else ',';
        if g_kdOpTypes[oOperand.sType][2][0] != '%':        ## @todo remove upper() later.
            sTmp += g_kdOpTypes[oOperand.sType][2].upper(); ## @todo remove upper() later.
        else:
            sTmp += g_kdOpTypes[oOperand.sType][2];
    sTmp += '",';
    asColumns = [ sTmp, ];

    #
    # Decoders.
    #
    iStart = len(asColumns);
    if oInstr.sEncoding is None:
        pass;
    elif oInstr.sEncoding == 'ModR/M':
        # ASSUME the first operand is using the ModR/M encoding
        assert len(oInstr.aoOperands) >= 1 and oInstr.aoOperands[0].usesModRM(), "oInstr=%s" % (oInstr,);
        asColumns.append('IDX_ParseModRM,');
    elif oInstr.sEncoding in [ 'prefix', ]:
        for oOperand in oInstr.aoOperands:
            asColumns.append('0,');
    elif oInstr.sEncoding in [ 'fixed', 'VEX.fixed' ]:
        pass;
    elif oInstr.sEncoding == 'VEX.ModR/M':
        asColumns.append('IDX_ParseModRM,');
    elif oInstr.sEncoding == 'vex2':
        asColumns.append('IDX_ParseVex2b,')
    elif oInstr.sEncoding == 'vex3':
        asColumns.append('IDX_ParseVex3b,')
    elif oInstr.sEncoding in g_dInstructionMaps:
        asColumns.append(g_dInstructionMaps[oInstr.sEncoding].sDisParse + ',');
    else:
        ## @todo
        #IDX_ParseTwoByteEsc,
        #IDX_ParseGrp1,
        #IDX_ParseShiftGrp2,
        #IDX_ParseGrp3,
        #IDX_ParseGrp4,
        #IDX_ParseGrp5,
        #IDX_Parse3DNow,
        #IDX_ParseGrp6,
        #IDX_ParseGrp7,
        #IDX_ParseGrp8,
        #IDX_ParseGrp9,
        #IDX_ParseGrp10,
        #IDX_ParseGrp12,
        #IDX_ParseGrp13,
        #IDX_ParseGrp14,
        #IDX_ParseGrp15,
        #IDX_ParseGrp16,
        #IDX_ParseThreeByteEsc4,
        #IDX_ParseThreeByteEsc5,
        #IDX_ParseModFence,
        #IDX_ParseEscFP,
        #IDX_ParseNopPause,
        #IDX_ParseInvOpModRM,
        assert False, str(oInstr);

    # Check for immediates and stuff in the remaining operands.
    for oOperand in oInstr.aoOperands[len(asColumns) - iStart:]:
        sIdx = g_kdOpTypes[oOperand.sType][0];
        #if sIdx != 'IDX_UseModRM':
        asColumns.append(sIdx + ',');
    asColumns.extend(['0,'] * (cMaxOperands - (len(asColumns) - iStart)));

    #
    # Opcode and operands.
    #
    assert oInstr.sDisEnum, str(oInstr);
    asColumns.append(oInstr.sDisEnum + ',');
    iStart = len(asColumns)
    for oOperand in oInstr.aoOperands:
        asColumns.append('OP_PARM_' + g_kdOpTypes[oOperand.sType][3] + ',');
    asColumns.extend(['OP_PARM_NONE,'] * (cMaxOperands - (len(asColumns) - iStart)));

    #
    # Flags.
    #
    sTmp = '';
    for sHint in sorted(oInstr.dHints.keys()):
        sDefine = g_kdHints[sHint];
        if sDefine.startswith('DISOPTYPE_'):
            if sTmp:
                sTmp += ' | ' + sDefine;
            else:
                sTmp += sDefine;
    if sTmp:
        sTmp += '),';
    else:
        sTmp += '0),';
    asColumns.append(sTmp);

    #
    # Format the columns into a line.
    #
    aoffColumns = [4, 29, 49, 65, 77, 89, 109, 125, 141, 157, 183, 199];
    sLine = '';
    for i, s in enumerate(asColumns):
        if len(sLine) < aoffColumns[i]:
            sLine += ' ' * (aoffColumns[i] - len(sLine));
        else:
            sLine += ' ';
        sLine += s;

    # OP("psrlw %Vdq,%Wdq", IDX_ParseModRM, IDX_UseModRM, 0, OP_PSRLW, OP_PARM_Vdq, OP_PARM_Wdq, OP_PARM_NONE,
    # DISOPTYPE_HARMLESS),
    # define OP(pszOpcode, idxParse1, idxParse2, idxParse3, opcode, param1, param2, param3, optype) \
    # { pszOpcode, idxParse1, idxParse2, idxParse3, 0, opcode, param1, param2, param3, 0, 0, optype }
    return sLine;

def __checkIfShortTable(aoTableOrdered, oMap):
    """
    Returns (iInstr, cInstructions, fShortTable)
    """

    # Determin how much we can trim off.
    cInstructions = len(aoTableOrdered);
    while cInstructions > 0 and aoTableOrdered[cInstructions - 1] is None:
        cInstructions -= 1;

    iInstr = 0;
    while iInstr < cInstructions and aoTableOrdered[iInstr] is None:
        iInstr += 1;

    # If we can save more than 30%, we go for the short table version.
    if iInstr + len(aoTableOrdered) - cInstructions >= len(aoTableOrdered) // 30:
        return (iInstr, cInstructions, True);
    _ = oMap; # Use this for overriding.

    # Output the full table.
    return (0, len(aoTableOrdered), False);

def generateDisassemblerTables(oDstFile = sys.stdout):
    """
    Generates disassembler tables.

    Returns exit code.
    """

    #
    # Parse all.
    #
    try:
        parseAll();
    except Exception as oXcpt:
        print('error: parseAll failed: %s' % (oXcpt,), file = sys.stderr);
        traceback.print_exc(file = sys.stderr);
        return 1;


    #
    # The disassembler uses a slightly different table layout to save space,
    # since several of the prefix varia
    #
    aoDisasmMaps = [];
    for sName, oMap in sorted(iter(g_dInstructionMaps.items()),
                              key = lambda aKV: aKV[1].sEncoding + ''.join(aKV[1].asLeadOpcodes)):
        if oMap.sSelector != 'byte+pfx':
            aoDisasmMaps.append(oMap);
        else:
            # Split the map by prefix.
            aoDisasmMaps.append(oMap.copy(oMap.sName,         'none'));
            aoDisasmMaps.append(oMap.copy(oMap.sName + '_66', '0x66'));
            aoDisasmMaps.append(oMap.copy(oMap.sName + '_F3', '0xf3'));
            aoDisasmMaps.append(oMap.copy(oMap.sName + '_F2', '0xf2'));

    #
    # Dump each map.
    #
    asHeaderLines = [];
    print("debug: maps=%s\n" % (', '.join([oMap.sName for oMap in aoDisasmMaps]),), file = sys.stderr);
    for oMap in aoDisasmMaps:
        sName = oMap.sName;

        if not sName.startswith("vex"): continue; # only looking at the vex maps at the moment.

        #
        # Get the instructions for the map and see if we can do a short version or not.
        #
        aoTableOrder    = oMap.getInstructionsInTableOrder();
        cEntriesPerByte = oMap.getEntriesPerByte();
        (iInstrStart, iInstrEnd, fShortTable) = __checkIfShortTable(aoTableOrder, oMap);

        #
        # Output the table start.
        # Note! Short tables are static and only accessible via the map range record.
        #
        asLines = [];
        asLines.append('/* Generated from: %-11s  Selector: %-7s  Encoding: %-7s  Lead bytes opcodes: %s */'
                       % ( oMap.sName, oMap.sSelector, oMap.sEncoding, ' '.join(oMap.asLeadOpcodes), ));
        if fShortTable:
            asLines.append('%sconst DISOPCODE %s[] =' % ('static ' if fShortTable else '', oMap.getDisasTableName(),));
        else:
            asHeaderLines.append('extern const DISOPCODE %s[%d];'  % (oMap.getDisasTableName(), iInstrEnd - iInstrStart,));
            asLines.append(             'const DISOPCODE %s[%d] =' % (oMap.getDisasTableName(), iInstrEnd - iInstrStart,));
        asLines.append('{');

        if fShortTable and (iInstrStart & ((0x10 * cEntriesPerByte) - 1)) != 0:
            asLines.append('    /* %#04x: */' % (iInstrStart,));

        #
        # Output the instructions.
        #
        iInstr = iInstrStart;
        while iInstr < iInstrEnd:
            oInstr = aoTableOrder[iInstr];
            if (iInstr & ((0x10 * cEntriesPerByte) - 1)) == 0:
                if iInstr != iInstrStart:
                    asLines.append('');
                asLines.append('    /* %x */' % ((iInstr // cEntriesPerByte) >> 4,));

            if oInstr is None:
                # Invalid. Optimize blocks of invalid instructions.
                cInvalidInstrs = 1;
                while iInstr + cInvalidInstrs < len(aoTableOrder) and aoTableOrder[iInstr + cInvalidInstrs] is None:
                    cInvalidInstrs += 1;
                if (iInstr & (0x10 * cEntriesPerByte - 1)) == 0 and cInvalidInstrs >= 0x10 * cEntriesPerByte:
                    asLines.append('    INVALID_OPCODE_BLOCK_%u,' % (0x10 * cEntriesPerByte,));
                    iInstr += 0x10 * cEntriesPerByte - 1;
                elif cEntriesPerByte > 1:
                    if (iInstr & (cEntriesPerByte - 1)) == 0 and cInvalidInstrs >= cEntriesPerByte:
                        asLines.append('    INVALID_OPCODE_BLOCK_%u,' % (cEntriesPerByte,));
                        iInstr += 3;
                    else:
                        asLines.append('    /* %#04x/%d */ INVALID_OPCODE,'
                                       % (iInstr // cEntriesPerByte, iInstr % cEntriesPerByte));
                else:
                    asLines.append('    /* %#04x */ INVALID_OPCODE,' % (iInstr));
            elif isinstance(oInstr, list):
                if len(oInstr) != 0:
                    asLines.append('    /* %#04x */ ComplicatedListStuffNeedingWrapper, /* \n -- %s */'
                                   % (iInstr, '\n -- '.join([str(oItem) for oItem in oInstr]),));
                else:
                    asLines.append(__formatDisassemblerTableEntry(oInstr));
            else:
                asLines.append(__formatDisassemblerTableEntry(oInstr));

            iInstr += 1;

        if iInstrStart >= iInstrEnd:
            asLines.append('    /* dummy */ INVALID_OPCODE');

        asLines.append('};');
        asLines.append('AssertCompile(RT_ELEMENTS(%s) == %s);' % (oMap.getDisasTableName(), iInstrEnd - iInstrStart,));

        #
        # We always emit a map range record, assuming the linker will eliminate the unnecessary ones.
        #
        asHeaderLines.append('extern const DISOPMAPDESC %sRange;'  % (oMap.getDisasRangeName()));
        asLines.append('const DISOPMAPDESC %s = { &%s[0], %#04x, RT_ELEMENTS(%s) };'
                       % (oMap.getDisasRangeName(), oMap.getDisasTableName(), iInstrStart, oMap.getDisasTableName(),));

        #
        # Write out the lines.
        #
        oDstFile.write('\n'.join(asLines));
        oDstFile.write('\n');
        oDstFile.write('\n');
        #break; #for now
    return 0;

if __name__ == '__main__':
    sys.exit(generateDisassemblerTables());

