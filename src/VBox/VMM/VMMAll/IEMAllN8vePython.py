#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: IEMAllN8vePython.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=invalid-name

"""
Native recompiler side-kick for IEMAllThrdPython.py.

Analyzes the each threaded function variant to see if we can we're able to
recompile it, then provides modifies MC block code for doing so.
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

# Standard python imports:
import copy;
import sys;

# Out python imports:
import IEMAllInstPython as iai;

## Temporary flag for enabling / disabling experimental MCs depending on the
## SIMD register allocator.
g_fNativeSimd = True;

## Supplememnts g_dMcStmtParsers.
g_dMcStmtThreaded = {
    'IEM_MC_DEFER_TO_CIMPL_0_RET_THREADED':                              (None, True,  True,  True,  ),
    'IEM_MC_DEFER_TO_CIMPL_1_RET_THREADED':                              (None, True,  True,  True,  ),
    'IEM_MC_DEFER_TO_CIMPL_2_RET_THREADED':                              (None, True,  True,  True,  ),
    'IEM_MC_DEFER_TO_CIMPL_3_RET_THREADED':                              (None, True,  True,  True,  ),

    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16':                       (None, True,  True,  True,  ),
    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),

    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),

    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16':                        (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32':                        (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT':                   (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64':                        (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG':                (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16':                       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT':                  (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG':               (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT':                  (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG':               (None, True,  True,  True,  ),

    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16_WITH_FLAGS':             (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_WITH_FLAGS':             (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS':        (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_WITH_FLAGS':             (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS':     (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS':       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS':    (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS':       (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS':    (None, True,  True,  True,  ),

    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16':                      (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32':                      (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32':                      (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64':                      (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32':                      (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64':                      (None, True,  True,  False, ), # @todo These should never be called - can't encode this
    'IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32':                      (None, True,  True,  False, ), # @todo These should never be called - can't encode this
    'IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64':                      (None, True,  True,  True,  ),

    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  False, ), # @todo These should never be called - can't encode this
    'IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32_WITH_FLAGS':           (None, True,  True,  False, ), # @todo These should never be called - can't encode this
    'IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  True,  ),

    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC16':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC32':                       (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64':                       (None, True,  True,  True,  ),

    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC16_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC32_WITH_FLAGS':            (None, True,  True,  True,  ),
    'IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS':            (None, True,  True,  True,  ),

    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16':                      (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32':                      (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64':                      (None, True,  True,  False, ), # @todo These should never be called - can be called on AMD but not on Intel, 'call ax' in 64-bit code is valid and should push a 16-bit IP IIRC.
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC16':                      (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32':                      (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64':                      (None, True,  True,  False, ), # @todo These should never be called - can't encode this.
    'IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64':                      (None, True,  True,  True,  ),

    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  False, ), # @todo These should never be called - this is valid, see above.
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC16_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS':           (None, True,  True,  True,  ),
    'IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  False, ), # @todo These should never be called - can't encode this.
    'IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS':           (None, True,  True,  True,  ),

    'IEM_MC_RETN_AND_FINISH_THREADED_PC16':                              (None, True,  True,  True,  ),
    'IEM_MC_RETN_AND_FINISH_THREADED_PC32':                              (None, True,  True,  True,  ),
    'IEM_MC_RETN_AND_FINISH_THREADED_PC64':                              (None, True,  True,  True,  ),

    'IEM_MC_RETN_AND_FINISH_THREADED_PC16_WITH_FLAGS':                   (None, True,  True,  True,  ),
    'IEM_MC_RETN_AND_FINISH_THREADED_PC32_WITH_FLAGS':                   (None, True,  True,  True,  ),
    'IEM_MC_RETN_AND_FINISH_THREADED_PC64_WITH_FLAGS':                   (None, True,  True,  True,  ),

    'IEM_MC_CALC_RM_EFF_ADDR_THREADED_16':                               (None, False, False, True,  ),
    'IEM_MC_CALC_RM_EFF_ADDR_THREADED_32':                               (None, False, False, True,  ),
    'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32':                        (None, False, False, True,  ),
    'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS':                          (None, False, False, True,  ),
    'IEM_MC_CALC_RM_EFF_ADDR_THREADED_64':                               (None, False, False, True,  ),

    'IEM_MC_CALL_CIMPL_1_THREADED':                                      (None, True,  True,  True,  ),
    'IEM_MC_CALL_CIMPL_2_THREADED':                                      (None, True,  True,  True,  ),
    'IEM_MC_CALL_CIMPL_3_THREADED':                                      (None, True,  True,  True,  ),
    'IEM_MC_CALL_CIMPL_4_THREADED':                                      (None, True,  True,  True,  ),
    'IEM_MC_CALL_CIMPL_5_THREADED':                                      (None, True,  True,  True,  ),

    'IEM_MC_STORE_GREG_U8_THREADED':                                     (None, True,  True,  True,  ),
    'IEM_MC_STORE_GREG_U8_CONST_THREADED':                               (None, True,  True,  True,  ),
    'IEM_MC_FETCH_GREG_U8_THREADED':                                     (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_SX_U16_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_SX_U32_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_SX_U64_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_ZX_U16_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_ZX_U32_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_FETCH_GREG_U8_ZX_U64_THREADED':                              (None, False, False, True,  ),
    'IEM_MC_REF_GREG_U8_THREADED':                                       (None, True,  True,  True,  ),
    'IEM_MC_REF_GREG_U8_CONST_THREADED':                                 (None, True,  True,  True,  ),

    'IEM_MC_REF_EFLAGS_EX':                                              (None, False, False, True,  ),
    'IEM_MC_COMMIT_EFLAGS_EX':                                           (None, True,  True,  True,  ),
    'IEM_MC_COMMIT_EFLAGS_OPT_EX':                                       (None, True,  True,  True,  ),
    'IEM_MC_FETCH_EFLAGS_EX':                                            (None, False, False, True,  ),
    'IEM_MC_ASSERT_EFLAGS':                                              (None, True,  True,  True,  ),

    # Flat Mem:
    'IEM_MC_FETCH_MEM16_FLAT_U8':                                        (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM32_FLAT_U8':                                        (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_D80':                                         (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_I16':                                         (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_I16_DISP':                                    (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_I32':                                         (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_I32_DISP':                                    (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_I64':                                         (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_R32':                                         (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_R64':                                         (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_R80':                                         (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE':                              (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U128_NO_AC':                                  (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U128':                                        (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U16_DISP':                                    (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U16_SX_U32':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U16_SX_U64':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U16':                                         (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX':                              (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U256_NO_AC':                                  (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U256':                                        (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U32':                                         (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U32_DISP':                                    (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U32_SX_U64':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64':                                  (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U64':                                         (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_SX_U16':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_SX_U32':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_SX_U64':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8':                                          (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64':                                   (None, True,  True,  True,  ),
    'IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE':                               (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC':                                   (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128':                          (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE_AND_XREG_XMM':                  (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_XMM_U32_AND_XREG_XMM':                        (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_XMM_U64_AND_XREG_XMM':                        (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_RAX_RDX_U64':          (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64':   (None, True,  True,  False, ),
    'IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC':                                   (None, True,  True,  g_fNativeSimd),
    'IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX_AND_YREG_YMM':                  (None, True,  True,  False, ),
    'IEM_MC_MEM_FLAT_MAP_D80_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_I16_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_I32_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_I64_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_R32_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_R64_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_R80_WO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U8_ATOMIC':                                     (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U8_RO':                                         (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U8_RW':                                         (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U16_ATOMIC':                                    (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U16_RO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U16_RW':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U32_ATOMIC':                                    (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U32_RO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U32_RW':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U64_ATOMIC':                                    (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U64_RO':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U64_RW':                                        (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U128_ATOMIC':                                   (None, True,  True,  True,  ),
    'IEM_MC_MEM_FLAT_MAP_U128_RW':                                       (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U128':                                        (None, True,  True,  False, ),
    'IEM_MC_STORE_MEM_FLAT_U128_NO_AC':                                  (None, True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE':                              (None, True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_FLAT_U16':                                         (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U16_CONST':                                   (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U256':                                        (None, True,  True,  False, ),
    'IEM_MC_STORE_MEM_FLAT_U256_NO_AC':                                  (None, True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX':                              (None, True,  True,  g_fNativeSimd),
    'IEM_MC_STORE_MEM_FLAT_U32':                                         (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U32_CONST':                                   (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U64':                                         (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U64_CONST':                                   (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U8':                                          (None, True,  True,  True,  ),
    'IEM_MC_STORE_MEM_FLAT_U8_CONST':                                    (None, True,  True,  True,  ),

    # Flat Stack:
    'IEM_MC_FLAT64_PUSH_U16':                                            (None, True,  True,  True,  ),
    'IEM_MC_FLAT64_PUSH_U64':                                            (None, True,  True,  True,  ),
    'IEM_MC_FLAT64_POP_GREG_U16':                                        (None, True,  True,  True,  ),
    'IEM_MC_FLAT64_POP_GREG_U64':                                        (None, True,  True,  True,  ),
    'IEM_MC_FLAT32_PUSH_U16':                                            (None, True,  True,  True,  ),
    'IEM_MC_FLAT32_PUSH_U32':                                            (None, True,  True,  True,  ),
    'IEM_MC_FLAT32_POP_GREG_U16':                                        (None, True,  True,  True,  ),
    'IEM_MC_FLAT32_POP_GREG_U32':                                        (None, True,  True,  True,  ),
};

class NativeRecompFunctionVariation(object):
    """
    Class that deals with transforming a threaded function variation into a
    native recompiler function.

    This base class doesn't do any transforming and just renders the same
    code as for the threaded function.
    """

    def __init__(self, oVariation, sHostArch):
        self.oVariation = oVariation # type: ThreadedFunctionVariation
        self.sHostArch  = sHostArch;

    def isRecompilable(self):
        """
        Predicate that returns whether the variant can be recompiled natively
        (for the selected host architecture).
        """
        return True;

    def raiseProblem(self, sMessage):
        """ Raises a problem. """
        raise Exception('%s:%s: error: %s'
                        % (self.oVariation.oParent.oMcBlock.sSrcFile, self.oVariation.oParent.oMcBlock.iBeginLine, sMessage,));

    def __analyzeVariableLiveness(self, aoStmts, dVars, iDepth = 0):
        """
        Performs liveness analysis of the given statement list, inserting new
        statements to signal to the native recompiler that a variable is no
        longer used and can be freed.

        Returns list of freed variables.
        """

        class VarInfo(object):
            """ Variable info """
            def __init__(self, oStmt):
                self.oStmt         = oStmt;
                self.fIsArg        = isinstance(oStmt, iai.McStmtArg);
                self.oReferences   = None       # type: VarInfo
                self.oReferencedBy = None       # type: VarInfo

            def isArg(self):
                return self.fIsArg;

            def makeReference(self, oLocal, oParent):
                if not self.isArg():
                    oParent.raiseProblem('Attempt to make a reference out of an local variable: %s = &%s'
                                         % (self.oStmt.sVarName, oLocal.oStmt.sVarName,));
                if self.oReferences:
                    oParent.raiseProblem('Can only make a variable a reference once: %s = &%s; now = &%s'
                                         % (self.oStmt.sVarName, self.oReferences.oStmt.sVarName, oLocal.oStmt.sVarName,));
                if oLocal.isArg():
                    oParent.raiseProblem('Attempt to make a reference to an argument: %s = &%s'
                                         % (self.oStmt.sVarName, oLocal.oStmt.sVarName,));
                if oLocal.oReferencedBy:
                    oParent.raiseProblem('%s is already referenced by %s, so cannot make %s reference it as well'
                                         % (oLocal.oStmt.sVarName, oLocal.oReferencedBy.oStmt.sVarName, self.oStmt.sVarName,));
                self.oReferences = oLocal;
                self.oReferences.oReferencedBy = self;
                return True;

        #
        # Gather variable declarations and add them to dVars.
        # Also keep a local list of them for scoping when iDepth > 0.
        #
        asVarsInScope = [];
        for oStmt in aoStmts:
            if isinstance(oStmt, iai.McStmtCall) and oStmt.sName.startswith('IEM_MC_CALL_AIMPL_'):
                oStmt = iai.McStmtVar(oStmt.sName, oStmt.asParams[0:2], oStmt.asParams[0], oStmt.asParams[1]);

            if isinstance(oStmt, iai.McStmtVar):
                if oStmt.sVarName in dVars:
                    raise Exception('Duplicate variable: %s' % (oStmt.sVarName, ));

                oInfo = VarInfo(oStmt);
                if oInfo.isArg() and oStmt.sRefType == 'local':
                    oInfo.makeReference(dVars[oStmt.sRef], self);

                dVars[oStmt.sVarName] = oInfo;
                asVarsInScope.append(oStmt.sVarName);

        #
        # Now work the statements backwards and look for the last reference to
        # each of the variables in dVars.  We remove the variables from the
        # collections as we go along.
        #

        def freeVariable(aoStmts, iStmt, oVarInfo, dFreedVars, dVars, fIncludeReferences = True):
            sVarName = oVarInfo.oStmt.sVarName;
            if not oVarInfo.isArg():
                aoStmts.insert(iStmt + 1, iai.McStmt('IEM_MC_FREE_LOCAL', [sVarName,]));
                assert not oVarInfo.oReferences;
            else:
                aoStmts.insert(iStmt + 1, iai.McStmt('IEM_MC_FREE_ARG', [sVarName,]));
                if fIncludeReferences and oVarInfo.oReferences:
                    sRefVarName = oVarInfo.oReferences.oStmt.sVarName;
                    if sRefVarName in dVars:
                        dFreedVars[sRefVarName] = dVars[sRefVarName];
                        del dVars[sRefVarName];
                        aoStmts.insert(iStmt + 1, iai.McStmt('IEM_MC_FREE_LOCAL', [sRefVarName,]));
            dFreedVars[sVarName] = oVarInfo;
            if dVars is not None:
                del dVars[sVarName];

        def implicitFree(oStmt, dFreedVars, dVars, sVar):
            oVarInfo = dVars.get(sVar);
            if oVarInfo:
                dFreedVars[sVar] = oVarInfo;
                del dVars[sVar];
            else:
                self.raiseProblem('Variable %s was used after implictly freed by %s!' % (sVar, oStmt.sName,));

        dFreedVars = {};
        for iStmt in range(len(aoStmts) - 1, -1, -1):
            oStmt = aoStmts[iStmt];
            if isinstance(oStmt, iai.McStmtCond):
                #
                # Conditionals requires a bit more work...
                #

                # Start by replacing the conditional statement by a shallow copy.
                oStmt = copy.copy(oStmt);
                oStmt.aoIfBranch   = list(oStmt.aoIfBranch);
                oStmt.aoElseBranch = list(oStmt.aoElseBranch);
                aoStmts[iStmt] = oStmt;

                fHadEmptyElseBranch = len(oStmt.aoElseBranch) == 0;

                # Check the two branches for final references. Both branches must
                # start processing with the same dVars set, fortunately as shallow
                # copy suffices.
                dFreedInIfBranch   = self.__analyzeVariableLiveness(oStmt.aoIfBranch, dict(dVars), iDepth + 1);
                dFreedInElseBranch = self.__analyzeVariableLiveness(oStmt.aoElseBranch, dVars,     iDepth + 1);

                # Add free statements to the start of the IF-branch for variables use
                # for the last time in the else branch.
                for sVarName, oVarInfo in dFreedInElseBranch.items():
                    if sVarName not in dFreedInIfBranch:
                        freeVariable(oStmt.aoIfBranch, -1, oVarInfo, dFreedVars, None, False);
                    else:
                        dFreedVars[sVarName] = oVarInfo;

                # And vice versa.
                for sVarName, oVarInfo in dFreedInIfBranch.items():
                    if sVarName not in dFreedInElseBranch:
                        freeVariable(oStmt.aoElseBranch, -1, oVarInfo, dFreedVars, dVars, False);

                #
                # Now check if any remaining variables are used for the last time
                # in the conditional statement ifself, in which case we need to insert
                # free statements to both branches.
                #
                if not oStmt.isCppStmt():
                    aoFreeStmts = [];
                    for sParam in oStmt.asParams:
                        if sParam in dVars:
                            freeVariable(aoFreeStmts, -1, dVars[sParam], dFreedVars, dVars);
                    for oFreeStmt in aoFreeStmts:
                        oStmt.aoIfBranch.insert(0, oFreeStmt);
                        oStmt.aoElseBranch.insert(0, oFreeStmt);

                #
                # HACK ALERT!
                #
                # This is a bit backwards, but if the else branch was empty, just zap
                # it so we don't create a bunch of unnecessary jumps as well as a
                # potential troublesome dirty guest shadowed register flushing for the
                # if-branch.  The IEM_MC_ENDIF code is forgiving here and will
                # automatically free the lost variables when merging the states.
                #
                # (In fact this behaviour caused trouble if we moved the IEM_MC_FREE_LOCAL
                # statements ouf of the branches and put them after the IF/ELSE blocks
                # to try avoid the unnecessary jump troubles, as the variable would be
                # assigned a host register and thus differ in an incompatible, cause the
                # endif code to just free the register and variable both, with the result
                # that the IEM_MC_FREE_LOCAL following the IF/ELSE blocks would assert
                # since the variable was already freed.)
                #
                # See iemNativeRecompFunc_cmovne_Gv_Ev__greg64_nn_64 and
                # the other cmovcc functions for examples.
                #
                if fHadEmptyElseBranch:
                    oStmt.aoElseBranch = [];
                #while (    oStmt.aoIfBranch
                #       and oStmt.aoElseBranch
                #       and oStmt.aoIfBranch[-1] == oStmt.aoElseBranch[-1]):
                #    aoStmts.insert(iStmt + 1, oStmt.aoIfBranch[-1]);
                #    del oStmt.aoIfBranch[-1];
                #    del oStmt.aoElseBranch[-1];

            elif not oStmt.isCppStmt():
                if isinstance(oStmt, iai.McStmtCall):
                    #
                    # Call statements will make use of all argument variables and
                    # will implicitly free them.  So, iterate the variable and
                    # move them from dVars and onto dFreedVars.
                    #
                    # We explictly free any referenced variable that is still in
                    # dVar at this point (since only arguments can hold variable
                    # references).
                    #
                    asCallParams = oStmt.asParams[oStmt.idxParams:];
                    for sParam in asCallParams:
                        oVarInfo = dVars.get(sParam);
                        if oVarInfo:
                            if not oVarInfo.isArg():
                                self.raiseProblem('Argument %s in %s is not an argument!' % (sParam, oStmt.sName,));
                            if oVarInfo.oReferences:
                                sRefVarName = oVarInfo.oReferences.oStmt.sVarName;
                                if sRefVarName in dVars:
                                    dFreedVars[sRefVarName] = dVars[sRefVarName];
                                    del dVars[sRefVarName];
                                    aoStmts.insert(iStmt + 1, iai.McStmt('IEM_MC_FREE_LOCAL', [sRefVarName,]));
                            dFreedVars[sParam] = oVarInfo;
                            del dVars[sParam];
                        elif sParam in dFreedVars:
                            self.raiseProblem('Argument %s in %s was used after the call!' % (sParam, oStmt.sName,));
                        else:
                            self.raiseProblem('Argument %s in %s is not known to us' % (sParam, oStmt.sName,));

                    # Check for stray argument variables.
                    for oVarInfo in dVars.values():
                        if oVarInfo.isArg():
                            self.raiseProblem('Unused argument variable: %s' % (oVarInfo.oStmt.sVarName,));

                elif oStmt.sName in ('IEM_MC_MEM_COMMIT_AND_UNMAP_RW', 'IEM_MC_MEM_COMMIT_AND_UNMAP_RO',
                                     'IEM_MC_MEM_COMMIT_AND_UNMAP_WO', 'IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO',
                                     'IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC',
                                     'IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO'):
                    #
                    # The unmap info variable passed to IEM_MC_MEM_COMMIT_AND_UNMAP_RW
                    # and friends is implictly freed and we must make sure it wasn't
                    # used any later.  IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO takes
                    # an additional a_u16FSW argument, which receives the same treatement.
                    #
                    for sParam in oStmt.asParams:
                        implicitFree(oStmt, dFreedVars, dVars, sParam);

                elif oStmt.sName in ('IEM_MC_PUSH_U16', 'IEM_MC_PUSH_U32', 'IEM_MC_PUSH_U32_SREG', 'IEM_MC_PUSH_U64',
                                     'IEM_MC_FLAT32_PUSH_U16', 'IEM_MC_FLAT32_PUSH_U32', 'IEM_MC_FLAT32_PUSH_U32_SREG',
                                     'IEM_MC_FLAT64_PUSH_U16', 'IEM_MC_FLAT64_PUSH_U64',):
                    #
                    # The variable being pushed is implicitly freed.
                    #
                    for sParam in oStmt.asParams:
                        implicitFree(oStmt, dFreedVars, dVars, sParam);
                else:
                    #
                    # Scan all the parameters of generic statements.
                    #
                    for sParam in oStmt.asParams:
                        if sParam in dVars:
                            freeVariable(aoStmts, iStmt, dVars[sParam], dFreedVars, dVars);

        #
        # Free anything left from asVarsInScope that's now going out of scope.
        #
        if iDepth > 0:
            for sVarName in asVarsInScope:
                if sVarName in dVars:
                    freeVariable(aoStmts, len(aoStmts) - 1, dVars[sVarName], dFreedVars, dVars);
                if sVarName in dFreedVars:
                    del dFreedVars[sVarName];  ## @todo Try eliminate this one...
        return dFreedVars;

    kdOptionArchToVal = {
        'amd64': 'RT_ARCH_VAL_AMD64',
        'arm64': 'RT_ARCH_VAL_ARM64',
    };

    def __morphStatements(self, aoStmts, fForLiveness):
        """
        Morphs the given statement list into something more suitable for
        native recompilation.

        The following is currently done here:
            - Amend IEM_MC_BEGIN with all the IEM_CIMPL_F_XXX and IEM_MC_F_XXX
              flags found and derived, including IEM_MC_F_WITHOUT_FLAGS which
              we determine here.
            - Insert IEM_MC_FREE_LOCAL when after the last statment a local
              variable is last used.

        Returns a new list of statements.
        """
        _ = fForLiveness;

        #
        # We can skip IEM_MC_DEFER_TO_CIMPL_x_RET stuff.
        #
        if self.oVariation.oParent.oMcBlock.fDeferToCImpl:
            return aoStmts;

        #
        # We make a shallow copy of the list, and only make deep copies of the
        # statements we modify.
        #
        aoStmts = list(aoStmts) # type: list(iai.McStmt)

        #
        # First, amend the IEM_MC_BEGIN statment, adding all the flags found
        # to it so the native recompiler can correctly process ARG and CALL
        # statements (among other things).
        #
        # Also add IEM_MC_F_WITHOUT_FLAGS if this isn't a variation with eflags
        # checking and clearing while there are such variations for this
        # function (this sounds a bit backwards, but has to be done this way
        # for the use we make of the flags in CIMPL calls).
        #
        # Second, eliminate IEM_MC_NATIVE_IF statements.
        #
        iConvArgToLocal = 0;
        oNewBeginExStmt = None;
        cStmts          = len(aoStmts);
        iStmt           = 0;
        while iStmt < cStmts:
            oStmt = aoStmts[iStmt];
            if oStmt.sName == 'IEM_MC_BEGIN':
                oNewStmt = copy.deepcopy(oStmt);
                oNewStmt.sName = 'IEM_MC_BEGIN_EX';
                fWithFlags    = self.oVariation.isWithFlagsCheckingAndClearingVariation();
                fWithoutFlags = not fWithFlags and self.oVariation.oParent.hasWithFlagsCheckingAndClearingVariation();
                if fWithFlags or fWithoutFlags or self.oVariation.oParent.dsCImplFlags:
                    if fWithoutFlags:
                        oNewStmt.asParams[0] = ' | '.join(sorted(  list(self.oVariation.oParent.oMcBlock.dsMcFlags.keys())
                                                                 + ['IEM_MC_F_WITHOUT_FLAGS',] ));
                    else:
                        oNewStmt.asParams[0] = ' | '.join(sorted(  list(self.oVariation.oParent.oMcBlock.dsMcFlags.keys())
                                                                 + ['IEM_MC_F_WITH_FLAGS',] ));
                    if self.oVariation.oParent.dsCImplFlags:
                        oNewStmt.asParams[1] = ' | '.join(sorted(self.oVariation.oParent.dsCImplFlags.keys()));
                    if 'IEM_CIMPL_F_CALLS_CIMPL' in self.oVariation.oParent.dsCImplFlags:
                        sArgs = '%s + IEM_CIMPL_HIDDEN_ARGS' % (len(self.oVariation.oParent.oMcBlock.aoArgs),);
                    elif (   'IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE' in self.oVariation.oParent.dsCImplFlags
                          or 'IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE' in self.oVariation.oParent.dsCImplFlags):
                        sArgs = '%s' % (len(self.oVariation.oParent.oMcBlock.aoArgs) + 1,);
                    else:
                        sArgs = '%s' % (len(self.oVariation.oParent.oMcBlock.aoArgs),);
                elif not self.oVariation.oParent.oMcBlock.aoArgs:
                    sArgs = '0';
                else:
                    self.raiseProblem('Have arguments but no IEM_CIMPL_F_CALLS_XXX falgs!');
                oNewStmt.asParams.append(sArgs);

                aoStmts[iStmt]  = oNewStmt;
                oNewBeginExStmt = oNewStmt;
            elif isinstance(oStmt, iai.McStmtNativeIf):
                if self.kdOptionArchToVal[self.sHostArch] in oStmt.asArchitectures:
                    iConvArgToLocal += 1;
                    oBranch = oStmt.aoIfBranch;
                else:
                    iConvArgToLocal = -999;
                    oBranch = oStmt.aoElseBranch;
                aoStmts = aoStmts[:iStmt] + oBranch + aoStmts[iStmt+1:];
                cStmts  = len(aoStmts);
                continue;

            iStmt += 1;
        if iConvArgToLocal > 0:
            oNewBeginExStmt.asParams[2] = '0';

        #
        # If we encountered a IEM_MC_NATIVE_IF and took the native branch,
        # ASSUME that all ARG variables can be converted to LOCAL variables
        # because no calls will be made.
        #
        if iConvArgToLocal > 0:
            for iStmt, oStmt in enumerate(aoStmts):
                if isinstance(oStmt, iai.McStmtArg):
                    if oStmt.sName == 'IEM_MC_ARG':
                        aoStmts[iStmt] = iai.McStmtVar('IEM_MC_LOCAL', oStmt.asParams[:2],
                                                       oStmt.sType, oStmt.sVarName);
                    elif oStmt.sName == 'IEM_MC_ARG_CONST':
                        aoStmts[iStmt] = iai.McStmtVar('IEM_MC_LOCAL_CONST', oStmt.asParams[:3],
                                                       oStmt.sType, oStmt.sVarName, oStmt.sValue);
                    else:
                        self.raiseProblem('Unexpected argument declaration when emitting native code: %s (%s)'
                                          % (oStmt.sName, oStmt.asParams,));
                    assert(oStmt.sRefType == 'none');

        #
        # Do a simple liveness analysis of the variable and insert
        # IEM_MC_FREE_LOCAL statements after the last statements using each
        # variable.  We do this recursively to best handle conditionals and
        # scoping related to those.
        #
        self.__analyzeVariableLiveness(aoStmts, {});

        return aoStmts;


    def renderCode(self, cchIndent, fForLiveness = False):
        """
        Returns the native recompiler function body for this threaded variant.
        """
        return iai.McStmt.renderCodeForList(self.__morphStatements(self.oVariation.aoStmtsForThreadedFunction, fForLiveness),
                                            cchIndent);

    @staticmethod
    def checkStatements(aoStmts, sHostArch):
        """
        Checks that all the given statements are supported by the native recompiler.
        Returns dictionary with the unsupported statments.
        """
        dRet = {};
        _ = sHostArch;
        for oStmt in aoStmts:   # type: McStmt
            if not oStmt.isCppStmt():
                aInfo = iai.g_dMcStmtParsers.get(oStmt.sName);
                if not aInfo:
                    aInfo = g_dMcStmtThreaded.get(oStmt.sName);
                    if not aInfo:
                        raise Exception('Unknown statement: %s' % (oStmt.sName, ));
                if aInfo[3] is False:
                    dRet[oStmt.sName] = 1;
                elif aInfo[3] is not True:
                    if isinstance(aInfo[3], str):
                        if aInfo[3] != sHostArch:
                            dRet[oStmt.sName] = 1;
                    elif sHostArch not in aInfo[3]:
                        dRet[oStmt.sName] = 1;
            #elif not self.fDecode:

            if isinstance(oStmt, iai.McStmtCond):
                dRet.update(NativeRecompFunctionVariation.checkStatements(oStmt.aoIfBranch, sHostArch));
                dRet.update(NativeRecompFunctionVariation.checkStatements(oStmt.aoElseBranch, sHostArch));

        return dRet;


## Statistics: Number of MC blocks (value) depending on each unsupported statement (key).
g_dUnsupportedMcStmtStats = {}

## Statistics: List of variations (value) that is only missing this one statement (key).
g_dUnsupportedMcStmtLastOneStats = {}

### Statistics: List of variations (value) with aimpl_[^0] calls that is only missing this one statement (key).
#g_dUnsupportedMcStmtLastOneAImplStats = {}


def analyzeVariantForNativeRecomp(oVariation,
                                  sHostArch): # type: (ThreadedFunctionVariation, str) -> NativeRecompFunctionVariation
    """
    This function analyzes the threaded function variant and returns an
    NativeRecompFunctionVariation instance for it, unless it's not
    possible to recompile at present.

    Returns NativeRecompFunctionVariation or the number of unsupported MCs.
    """

    #
    # Analyze the statements.
    #
    aoStmts = oVariation.aoStmtsForThreadedFunction # type: list(McStmt)
    dUnsupportedStmts = NativeRecompFunctionVariation.checkStatements(aoStmts, sHostArch);
    if not dUnsupportedStmts:
        return NativeRecompFunctionVariation(oVariation, sHostArch);

    #
    # Update the statistics.
    #
    for sStmt in dUnsupportedStmts:
        g_dUnsupportedMcStmtStats[sStmt] = 1 + g_dUnsupportedMcStmtStats.get(sStmt, 0);

    if len(dUnsupportedStmts) == 1:
        for sStmt in dUnsupportedStmts:
            if sStmt in g_dUnsupportedMcStmtLastOneStats:
                g_dUnsupportedMcStmtLastOneStats[sStmt].append(oVariation);
            else:
                g_dUnsupportedMcStmtLastOneStats[sStmt] = [oVariation,];

    #if (    len(dUnsupportedStmts) in (1,2)
    #    and iai.McStmt.findStmtByNames(aoStmts,
    #                                   { 'IEM_MC_CALL_AIMPL_3': 1,
    #                                     'IEM_MC_CALL_AIMPL_4': 1,
    #                                     #'IEM_MC_CALL_VOID_AIMPL_0': 1, - can't test results... ?
    #                                     'IEM_MC_CALL_VOID_AIMPL_1': 1,
    #                                     'IEM_MC_CALL_VOID_AIMPL_2': 1,
    #                                     'IEM_MC_CALL_VOID_AIMPL_3': 1,
    #                                     'IEM_MC_CALL_VOID_AIMPL_4': 1,
    #                                     #'IEM_MC_CALL_FPU_AIMPL_1': 1,
    #                                     #'IEM_MC_CALL_FPU_AIMPL_2': 1,
    #                                     #'IEM_MC_CALL_FPU_AIMPL_3': 1,
    #                                     #'IEM_MC_CALL_MMX_AIMPL_2': 1,
    #                                     #'IEM_MC_CALL_MMX_AIMPL_3': 1,
    #                                     #'IEM_MC_CALL_SSE_AIMPL_2': 1,
    #                                     #'IEM_MC_CALL_SSE_AIMPL_3': 1,
    #                                     #'IEM_MC_CALL_AVX_AIMPL_2': 1,
    #                                     #'IEM_MC_CALL_AVX_AIMPL_3': 1,
    #                                     #'IEM_MC_CALL_AVX_AIMPL_4': 1,
    #                                    })):
    #    for sStmt in dUnsupportedStmts:
    #        if sStmt in g_dUnsupportedMcStmtLastOneAImplStats:
    #            g_dUnsupportedMcStmtLastOneAImplStats[sStmt].append(oVariation);
    #        else:
    #            g_dUnsupportedMcStmtLastOneAImplStats[sStmt] = [oVariation,];

    return None;


def analyzeThreadedFunctionsForNativeRecomp(aoThreadedFuncs, sHostArch): # type (list(ThreadedFunction)) -> True
    """
    Displays statistics.
    """
    print('todo:', file = sys.stderr);
    cTotal  = 0;
    cNative = 0;
    for oThreadedFunction in aoThreadedFuncs:
        cNativeVariations = 0;
        for oVariation in oThreadedFunction.aoVariations:
            cTotal += 1;
            oVariation.oNativeRecomp = analyzeVariantForNativeRecomp(oVariation, sHostArch);
            if oVariation.oNativeRecomp and oVariation.oNativeRecomp.isRecompilable():
                cNativeVariations += 1;
        cNative += cNativeVariations;

        # If all variations can be recompiled natively, annotate the threaded
        # function name accordingly so it'll be easy to spot in the stats.
        if oThreadedFunction.sSubName:
            if cNativeVariations == len(oThreadedFunction.aoVariations):
                aoStmts = oThreadedFunction.oMcBlock.decode();
                oStmt = iai.McStmt.findStmtByNames(aoStmts, {'IEM_MC_NATIVE_IF': True,});
                if oStmt and NativeRecompFunctionVariation.kdOptionArchToVal[sHostArch] in oStmt.asArchitectures:
                    oThreadedFunction.sSubName += '_ne';        # native emit
                elif oThreadedFunction.sSubName.find('aimpl') >= 0:
                    oThreadedFunction.sSubName += '_na';        # native aimpl
                else:
                    oThreadedFunction.sSubName += '_nn';        # native native
            elif cNativeVariations == 0:
                oThreadedFunction.sSubName += '_ntodo';         # native threaded todo
            else:
                oThreadedFunction.sSubName += '_nm';            # native mixed


    print('todo: %.1f%% / %u out of %u threaded function variations are recompilable'
          % (cNative * 100.0 / cTotal, cNative, cTotal), file = sys.stderr);
    if g_dUnsupportedMcStmtLastOneStats:
        asTopKeys = sorted(g_dUnsupportedMcStmtLastOneStats, reverse = True,
                           key = lambda sSortKey: len(g_dUnsupportedMcStmtLastOneStats[sSortKey]))[:16];
        print('todo:', file = sys.stderr);
        print('todo: Top %s variations with one unsupported statement dependency:' % (len(asTopKeys),),
              file = sys.stderr);
        cchMaxKey = max([len(sKey) for sKey in asTopKeys]);
        for sKey in asTopKeys:
            print('todo: %*s = %s (%s%s)'
                  % (cchMaxKey, sKey, len(g_dUnsupportedMcStmtLastOneStats[sKey]),
                     ', '.join([oVar.getShortName() for oVar in g_dUnsupportedMcStmtLastOneStats[sKey][:5]]),
                     ',...' if len(g_dUnsupportedMcStmtLastOneStats[sKey]) >= 5 else '', )
                     , file = sys.stderr);

        asTopKeys = sorted(g_dUnsupportedMcStmtStats, reverse = True,
                           key = lambda sSortKey: g_dUnsupportedMcStmtStats[sSortKey])[:16];
        print('todo:', file = sys.stderr);
        print('todo: Top %d most used unimplemented statements:' % (len(asTopKeys),), file = sys.stderr);
        cchMaxKey = max([len(sKey) for sKey in asTopKeys]);
        cTopKeys = len(asTopKeys);
        for i in range(0, cTopKeys & ~1, 2):
            print('todo:  %*s = %4d  %*s = %4d'
                  % ( cchMaxKey, asTopKeys[i],     g_dUnsupportedMcStmtStats[asTopKeys[i]],
                      cchMaxKey, asTopKeys[i + 1], g_dUnsupportedMcStmtStats[asTopKeys[i + 1]],),
                  file = sys.stderr);
        if cTopKeys & 1:
            print('todo:  %*s = %4d'
                  % ( cchMaxKey, asTopKeys[i],     g_dUnsupportedMcStmtStats[asTopKeys[i]],),
                  file = sys.stderr);
        print('todo:', file = sys.stderr);

    #if g_dUnsupportedMcStmtLastOneAImplStats:
    #    asTopKeys = sorted(g_dUnsupportedMcStmtLastOneAImplStats, reverse = True,
    #                       key = lambda sSortKey: len(g_dUnsupportedMcStmtLastOneAImplStats[sSortKey]))[:16];
    #    print('todo:', file = sys.stderr);
    #    print('todo: Top %s variations with AIMPL call and 1-2 unsupported statement dependencies:' % (len(asTopKeys),),
    #          file = sys.stderr);
    #    cchMaxKey = max([len(sKey) for sKey in asTopKeys]);
    #    for sKey in asTopKeys:
    #        print('todo: %*s = %s (%s%s)'
    #              % (cchMaxKey, sKey, len(g_dUnsupportedMcStmtLastOneAImplStats[sKey]),
    #                 ', '.join([oVar.getShortName() for oVar in g_dUnsupportedMcStmtLastOneAImplStats[sKey][:5]]),
    #                 ',...' if len(g_dUnsupportedMcStmtLastOneAImplStats[sKey]) >= 5 else '', )
    #                 , file = sys.stderr);

    return True;
