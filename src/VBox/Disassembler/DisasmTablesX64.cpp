/* $Id: DisasmTablesX64.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler - Tables for AMD64 (64-bit mode).
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/dis.h>
#include <VBox/disopcode-x86-amd64.h>
#include "DisasmInternal-x86-amd64.h"


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
/// @todo Verify tables for correctness
/// @todo opcode type (harmless, potentially dangerous, dangerous)
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#ifndef DIS_CORE_ONLY
static char g_szInvalidOpcode[] = "Invalid Opcode";
#endif

#define INVALID_OPCODE  \
    OP(g_szInvalidOpcode,    0,              0,          0,          OP_INVALID, OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_INVALID)

#define INVALID_OPCODE_BLOCK \
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,\
    INVALID_OPCODE,

/* Tables for the elegant Intel X64 instruction set */

const DISOPCODE g_aOneByteMapX64[256] =
{
    /* 0 */
    OP("add %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADD,     OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("add %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADD,     OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("add %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADD,     OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("add %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADD,     OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("add AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_ADD,     OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("add %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_ADD,     OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    OP("or %Eb,%Gb",         IDX_ParseModRM,     IDX_UseModRM,   0,          OP_OR,      OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("or %Ev,%Gv",         IDX_ParseModRM,     IDX_UseModRM,   0,          OP_OR,      OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("or %Gb,%Eb",         IDX_ParseModRM,     IDX_UseModRM,   0,          OP_OR,      OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("or %Gv,%Ev",         IDX_ParseModRM,     IDX_UseModRM,   0,          OP_OR,      OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("or AL,%Ib",          IDX_ParseFixedReg,  IDX_ParseImmByte, 0,        OP_OR,      OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("or %eAX,%Iz",        IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_OR,      OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    OP("2-BYTE ESCAPE",      IDX_ParseTwoByteEsc,0,          0,              OP_2B_ESC,  OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),

    /* 1 */
    OP("adc %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADC,     OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("adc %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADC,     OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("adc %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADC,     OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("adc %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_ADC,     OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("adc AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_ADC,     OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("adc %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_ADC,     OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    OP("sbb %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SBB,     OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sbb %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SBB,     OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sbb %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SBB,     OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sbb %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SBB,     OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sbb AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_SBB,     OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sbb %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_SBB,     OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,

    /* 2 */
    OP("and %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_AND,     OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("and %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_AND,     OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("and %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_AND,     OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("and %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_AND,     OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("and AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_AND,     OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("and %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_AND,     OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("SEG ES",             0,                  0,              0,          OP_SEG,     OP_PARM_REG_ES,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    OP("sub %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SUB,     OP_PARM_Eb,         OP_PARM_Gb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sub %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SUB,     OP_PARM_Ev,         OP_PARM_Gv ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sub %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SUB,     OP_PARM_Gb,         OP_PARM_Eb ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sub %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_SUB,     OP_PARM_Gv,         OP_PARM_Ev ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sub AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_SUB,     OP_PARM_REG_AL,     OP_PARM_Ib ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("sub %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_SUB,     OP_PARM_REG_EAX,    OP_PARM_Iz ,    OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    /* Branch not taken hint prefix for branches on a Pentium 4 or Xeon CPU (or higher)! */
    OP("SEG CS",             0,                  0,              0,          OP_SEG,     OP_PARM_REG_CS,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,

    /* 3 */
    OP("xor %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_XOR,     OP_PARM_Eb,         OP_PARM_Gb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xor %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_XOR,     OP_PARM_Ev,         OP_PARM_Gv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xor %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_XOR,     OP_PARM_Gb,         OP_PARM_Eb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xor %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_XOR,     OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xor AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_XOR,     OP_PARM_REG_AL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xor %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_XOR,     OP_PARM_REG_EAX,    OP_PARM_Iz,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("SEG SS",             0,                  0,              0,          OP_SEG,     OP_PARM_REG_SS,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    OP("cmp %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_CMP,     OP_PARM_Eb,         OP_PARM_Gb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmp %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_CMP,     OP_PARM_Ev,         OP_PARM_Gv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmp %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_CMP,     OP_PARM_Gb,         OP_PARM_Eb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmp %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,   0,          OP_CMP,     OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmp AL,%Ib",         IDX_ParseFixedReg,  IDX_ParseImmByte,0,         OP_CMP,     OP_PARM_REG_AL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmp %eAX,%Iz",       IDX_ParseFixedReg,  IDX_ParseImmZ,  0,          OP_CMP,     OP_PARM_REG_EAX,    OP_PARM_Iz,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    /* Branch not taken hint prefix for branches on a Pentium 4 or Xeon CPU (or higher)! */
    OP("SEG DS",             0,                  0,              0,          OP_SEG,     OP_PARM_REG_DS,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,

    /* 4 */
    OP("REX",                0,                  0,              0,          OP_REX,     OP_PARM_REX,        OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.B",              0,                  0,              0,          OP_REX,     OP_PARM_REX_B,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.X",              0,                  0,              0,          OP_REX,     OP_PARM_REX_X,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.XB",             0,                  0,              0,          OP_REX,     OP_PARM_REX_XB,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.R",              0,                  0,              0,          OP_REX,     OP_PARM_REX_R,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.RB",             0,                  0,              0,          OP_REX,     OP_PARM_REX_RB,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.RX",             0,                  0,              0,          OP_REX,     OP_PARM_REX_RX,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.RXB",            0,                  0,              0,          OP_REX,     OP_PARM_REX_RXB,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.W",              0,                  0,              0,          OP_REX,     OP_PARM_REX_W,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WB",             0,                  0,              0,          OP_REX,     OP_PARM_REX_WB,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WX",             0,                  0,              0,          OP_REX,     OP_PARM_REX_WX,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WXB",            0,                  0,              0,          OP_REX,     OP_PARM_REX_WXB,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WR",             0,                  0,              0,          OP_REX,     OP_PARM_REX_WR,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WRB",            0,                  0,              0,          OP_REX,     OP_PARM_REX_WRB,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WRX",            0,                  0,              0,          OP_REX,     OP_PARM_REX_WRX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("REX.WRXB",           0,                  0,              0,          OP_REX,     OP_PARM_REX_WRXB,   OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),

    /* 5 */
    OP("push %eAX",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_EAX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eCX",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_ECX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eDX",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_EDX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eBX",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_EBX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eSP",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_ESP,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eBP",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_EBP,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eSI",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_ESI,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("push %eDI",          IDX_ParseFixedReg,  0,          0,          OP_PUSH,    OP_PARM_REG_EDI,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eAX",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_EAX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eCX",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_ECX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eDX",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_EDX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eBX",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_EBX,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eSP",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_ESP,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eBP",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_EBP,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eSI",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_ESI,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("pop %eDI",           IDX_ParseFixedReg,  0,          0,          OP_POP,     OP_PARM_REG_EDI,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_REXB_EXTENDS_OPREG),

    /* 6 */
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
    OP("movsxd %Gv,%Ev",     IDX_ParseModRM,     IDX_UseModRM,      0,                  OP_MOVSXD,      OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("SEG FS",             0,                  0,                 0,                  OP_SEG,         OP_PARM_REG_FS,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("SEG GS",             0,                  0,                 0,                  OP_SEG,         OP_PARM_REG_GS,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("OP SIZE",            0,                  0,                 0,                  OP_OPSIZE,      OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("ADDR SIZE",          0,                  0,                 0,                  OP_ADDRSIZE,    OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("push %Iz",           IDX_ParseImmZ,      0,                 0,                  OP_PUSH,        OP_PARM_Iz,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE),
    OP("imul %Gv,%Ev,%Iz",   IDX_ParseModRM,     IDX_UseModRM,      IDX_ParseImmZ,      OP_IMUL,        OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_Iz,     DISOPTYPE_HARMLESS),
    OP("push %Ib",           IDX_ParseImmByteSX, 0,                 0,                  OP_PUSH,        OP_PARM_Ib,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE),
    OP("imul %Gv,%Ev,%Ib",   IDX_ParseModRM,     IDX_UseModRM,      IDX_ParseImmByteSX, OP_IMUL,        OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_Ib,     DISOPTYPE_HARMLESS),
    OP("insb %Yb,DX",        IDX_ParseYb,        IDX_ParseFixedReg, 0,                  OP_INSB,        OP_PARM_Yb,         OP_PARM_REG_DX, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("insw/d %Yv,DX",      IDX_ParseYv,        IDX_ParseFixedReg, 0,                  OP_INSWD,       OP_PARM_Yv,         OP_PARM_REG_DX, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("outsb DX,%Xb",       IDX_ParseFixedReg,  IDX_ParseXb,       0,                  OP_OUTSB,       OP_PARM_REG_DX,     OP_PARM_Yb,     OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),
    OP("outsw/d DX,%Xv",     IDX_ParseFixedReg,  IDX_ParseXv,       0,                  OP_OUTSWD,      OP_PARM_REG_DX,     OP_PARM_Yv,     OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),


    /* 7 */
    OP("jo %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JO,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jno %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNO,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jc %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JC,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jnc %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNC,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("je %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JE,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jne %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNE,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jbe %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JBE,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jnbe %Jb",           IDX_ParseImmBRel,   0,          0,          OP_JNBE,    OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("js %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JS,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jns %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNS,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jp %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JP,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jnp %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNP,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jl %Jb",             IDX_ParseImmBRel,   0,          0,          OP_JL,      OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jnl %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JNL,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jle %Jb",            IDX_ParseImmBRel,   0,          0,          OP_JLE,     OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jnle %Jb",           IDX_ParseImmBRel,   0,          0,          OP_JNLE,    OP_PARM_Jb  ,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),

    /* 8 */
    OP("Imm Grp1 %Eb,%Ib",   IDX_ParseGrp1,      0,             0,          OP_IMM_GRP1,OP_PARM_Eb,         OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Imm Grp1 %Ev,%Iz",   IDX_ParseGrp1,      0,             0,          OP_IMM_GRP1,OP_PARM_Ev,         OP_PARM_Iz,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    OP("Imm Grp1 %Ev,%Ib",   IDX_ParseGrp1,      0,             0,          OP_IMM_GRP1,OP_PARM_Ev,         OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("test %Eb,%Gb",       IDX_ParseModRM,     IDX_UseModRM,  0,          OP_TEST,    OP_PARM_Eb,         OP_PARM_Gb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("test %Ev,%Gv",       IDX_ParseModRM,     IDX_UseModRM,  0,          OP_TEST,    OP_PARM_Ev,         OP_PARM_Gv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xchg %Eb,%Gb",       IDX_ParseModRM,     IDX_UseModRM,  0,          OP_XCHG,    OP_PARM_Eb,         OP_PARM_Gb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("xchg %Ev,%Gv",       IDX_ParseModRM,     IDX_UseModRM,  0,          OP_XCHG,    OP_PARM_Ev,         OP_PARM_Gv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Eb,%Gb",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Eb,         OP_PARM_Gb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Ev,%Gv",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Ev,         OP_PARM_Gv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Gb,%Eb",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Gb,         OP_PARM_Eb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Gv,%Ev",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Gv,         OP_PARM_Ev,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Ev,%Sw",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Ev,         OP_PARM_Sw,     OP_PARM_NONE,   DISOPTYPE_POTENTIALLY_DANGEROUS),
    OP("lea %Gv,%M",         IDX_ParseModRM,     IDX_UseModRM,  0,          OP_LEA,     OP_PARM_Gv,         OP_PARM_M,      OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Sw,%Ev",        IDX_ParseModRM,     IDX_UseModRM,  0,          OP_MOV,     OP_PARM_Sw,         OP_PARM_Ev,     OP_PARM_NONE,   DISOPTYPE_POTENTIALLY_DANGEROUS | DISOPTYPE_INHIBIT_IRQS),
    /** @todo this is grp 1a, actually */
    OP("pop %Ev",            IDX_ParseModRM,     0,             0,          OP_POP,     OP_PARM_Ev,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),

    /* 9 */
    OP("nop/pause/xchg %eAX,%eAX",  IDX_ParseNopPause,  0,                  0,      OP_NOP,     OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eCX,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_ECX,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eDX,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_EDX,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eBX,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_EBX,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eSP,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_ESP,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eBP,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_EBP,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eSI,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_ESI,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("xchg %eDI,%eAX",            IDX_ParseFixedReg,  IDX_ParseFixedReg,  0,      OP_XCHG,    OP_PARM_REG_EDI,    OP_PARM_REG_EAX,    OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("cbw",                       0,                  0,                  0,      OP_CBW,     OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cwd",                       0,                  0,                  0,      OP_CWD,     OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    OP("wait",                      0,                  0,                  0,      OP_WAIT,    OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("pushf %Fv",                 0,                  0,                  0,      OP_PUSHF,   OP_PARM_Fv,         OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_POTENTIALLY_DANGEROUS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE),
    OP("popf %Fv",                  0,                  0,                  0,      OP_POPF,    OP_PARM_Fv,         OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_POTENTIALLY_DANGEROUS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE),
    OP("sahf",                      0,                  0,                  0,      OP_SAHF,    OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("lahf",                      0,                  0,                  0,      OP_LAHF,    OP_PARM_NONE,       OP_PARM_NONE,       OP_PARM_NONE,   DISOPTYPE_HARMLESS),


    /* A */
    OP("mov AL,%Ob",         IDX_ParseFixedReg,  IDX_ParseImmAddr,  0,          OP_MOV,     OP_PARM_REG_AL,     OP_PARM_Ob,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %eAX,%Ov",       IDX_ParseFixedReg,  IDX_ParseImmAddr,  0,          OP_MOV,     OP_PARM_REG_EAX,    OP_PARM_Ov,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Ob,AL",         IDX_ParseImmAddr,   IDX_ParseFixedReg, 0,          OP_MOV,     OP_PARM_Ob,         OP_PARM_REG_AL, OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Ov,%eAX",       IDX_ParseImmAddr,   IDX_ParseFixedReg, 0,          OP_MOV,     OP_PARM_Ov,         OP_PARM_REG_EAX,OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("movsb %Xb,%Yb",      IDX_ParseXb,        IDX_ParseYb,       0,          OP_MOVSB,   OP_PARM_Xb,         OP_PARM_Yb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("movsw/d %Xv,%Yv",    IDX_ParseXv,        IDX_ParseYv,       0,          OP_MOVSWD,  OP_PARM_Xv,         OP_PARM_Yv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmpsb %Xb,%Yb",      IDX_ParseXb,        IDX_ParseYb,       0,          OP_CMPSB,   OP_PARM_Xb,         OP_PARM_Yb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cmpsw/d %Xv,%Yv",    IDX_ParseXv,        IDX_ParseYv,       0,          OP_CMPWD,   OP_PARM_Xv,         OP_PARM_Yv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("test AL,%Ib",        IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_TEST,    OP_PARM_REG_AL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("test %eAX,%Iz",      IDX_ParseFixedReg,  IDX_ParseImmZ,     0,          OP_TEST,    OP_PARM_REG_EAX,    OP_PARM_Iz,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("stosb %Yb,AL",       IDX_ParseYb,        IDX_ParseFixedReg, 0,          OP_STOSB,   OP_PARM_Yb,         OP_PARM_REG_AL, OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("stosw/d %Yv,%eAX",   IDX_ParseYv,        IDX_ParseFixedReg, 0,          OP_STOSWD,  OP_PARM_Yv,         OP_PARM_REG_EAX,OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("lodsb AL,%Xb",       IDX_ParseFixedReg,  IDX_ParseXb,       0,          OP_LODSB,   OP_PARM_REG_AL,     OP_PARM_Xb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("lodsw/d %eAX,%Xv",   IDX_ParseFixedReg,  IDX_ParseXv,       0,          OP_LODSWD,  OP_PARM_REG_EAX,    OP_PARM_Xv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("scasb AL,%Xb",       IDX_ParseFixedReg,  IDX_ParseXb,       0,          OP_SCASB,   OP_PARM_REG_AL,     OP_PARM_Xb,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("scasw/d %eAX,%Xv",   IDX_ParseFixedReg,  IDX_ParseXv,       0,          OP_SCASWD,  OP_PARM_REG_EAX,    OP_PARM_Xv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),


    /* B */
    OP("mov %eAL,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_AL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eCL,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_CL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eDL,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_DL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eBL,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_BL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eAH,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_AH,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eCH,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_CH,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eDH,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_DH,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eBH,%Ib",       IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_MOV,     OP_PARM_REG_BH,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eAX,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_EAX,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eCX,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_ECX,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eDX,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_EDX,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eBX,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_EBX,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eSP,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_ESP,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eBP,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_EBP,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eSI,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_ESI,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),
    OP("mov %eDI,%Iv",       IDX_ParseFixedReg,  IDX_ParseImmV,     0,          OP_MOV,     OP_PARM_REG_EDI,    OP_PARM_Iv,     OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_REXB_EXTENDS_OPREG),

    /* C */
    OP("Shift Grp2 %Eb,%Ib", IDX_ParseShiftGrp2, 0,                 0,          OP_SHIFT_GRP2,  OP_PARM_Eb,      OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Shift Grp2 %Ev,%Ib", IDX_ParseShiftGrp2, 0,                 0,          OP_SHIFT_GRP2,  OP_PARM_Ev,      OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("retn %Iw",           IDX_ParseImmUshort, 0,                 0,          OP_RETN,        OP_PARM_Iw,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("retn",               0,                  0,                 0,          OP_RETN,        OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("VEX 3-byte",         IDX_ParseVex3b,     0,                 0,          OP_VEX3B,       OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("VEX 2-byte",         IDX_ParseVex2b,     0,                 0,          OP_VEX2B,       OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    /** @todo these two are actually group11 */
    OP("mov %Eb,%Ib",        IDX_ParseModRM,     IDX_ParseImmByte,  0,          OP_MOV,         OP_PARM_Eb,      OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("mov %Ev,%Iz",        IDX_ParseModRM,     IDX_ParseImmZ,     0,          OP_MOV,         OP_PARM_Ev,      OP_PARM_Iz,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("enter %Iw,%Ib",      IDX_ParseImmUshort, IDX_ParseImmByte,  0,          OP_ENTER,       OP_PARM_Iw,      OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("leave",              0,                  0,                 0,          OP_LEAVE,       OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_X86_DEFAULT_64_OP_SIZE),
    OP("retf %Iw",           IDX_ParseImmUshort, 0,                 0,          OP_RETF,        OP_PARM_Iw,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW),
    OP("retf",               0,                  0,                 0,          OP_RETF,        OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW),
    OP("int 3",              0,                  0,                 0,          OP_INT3,        OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_INTERRUPT),
    OP("int %Ib",            IDX_ParseImmByte,   0,                 0,          OP_INT,         OP_PARM_Ib,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_INTERRUPT),
    INVALID_OPCODE,
    OP("iret",               0,                  0,                 0,          OP_IRET,        OP_PARM_NONE,    OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW),

    /* D */
    OP("Shift Grp2 %Eb,1",   IDX_ParseShiftGrp2, 0,                 0,          OP_SHIFT_GRP2,  OP_PARM_Eb,         OP_PARM_1,      OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Shift Grp2 %Ev,1",   IDX_ParseShiftGrp2, 0,                 0,          OP_SHIFT_GRP2,  OP_PARM_Ev,         OP_PARM_1,      OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Shift Grp2 %Eb,CL",  IDX_ParseShiftGrp2, IDX_ParseFixedReg, 0,          OP_SHIFT_GRP2,  OP_PARM_Eb,         OP_PARM_REG_CL, OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Shift Grp2 %Ev,CL",  IDX_ParseShiftGrp2, IDX_ParseFixedReg, 0,          OP_SHIFT_GRP2,  OP_PARM_Ev,         OP_PARM_REG_CL, OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    /* setalc?? */
    INVALID_OPCODE,
    OP("xlat",               0,                  0,                 0,          OP_XLAT,        OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf0",           IDX_ParseEscFP,     0,                 0,          OP_ESCF0,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf1",           IDX_ParseEscFP,     0,                 0,          OP_ESCF1,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf2",           IDX_ParseEscFP,     0,                 0,          OP_ESCF2,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf3",           IDX_ParseEscFP,     0,                 0,          OP_ESCF3,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf4",           IDX_ParseEscFP,     0,                 0,          OP_ESCF4,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf5",           IDX_ParseEscFP,     0,                 0,          OP_ESCF5,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf6",           IDX_ParseEscFP,     0,                 0,          OP_ESCF6,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("esc 0xf7",           IDX_ParseEscFP,     0,                 0,          OP_ESCF7,       OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),


    /* E */
    OP("loopne %Jb",         IDX_ParseImmBRel,   0,                 0,          OP_LOOPNE,  OP_PARM_Jb,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW|DISOPTYPE_RELATIVE_CONTROLFLOW|DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("loope %Jb",          IDX_ParseImmBRel,   0,                 0,          OP_LOOPE,   OP_PARM_Jb,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW|DISOPTYPE_RELATIVE_CONTROLFLOW|DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("loop %Jb",           IDX_ParseImmBRel,   0,                 0,          OP_LOOP,    OP_PARM_Jb,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW|DISOPTYPE_RELATIVE_CONTROLFLOW|DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("j(e)cxz %Jb",        IDX_ParseImmBRel,   0,                 0,          OP_JECXZ,   OP_PARM_Jb,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW|DISOPTYPE_RELATIVE_CONTROLFLOW|DISOPTYPE_COND_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("in AL,%Ib",          IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_IN,      OP_PARM_REG_AL,     OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("in %eAX,%Ib",        IDX_ParseFixedReg,  IDX_ParseImmByte,  0,          OP_IN,      OP_PARM_REG_EAX,    OP_PARM_Ib,     OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("out %Ib,AL",         IDX_ParseImmByte,   IDX_ParseFixedReg, 0,          OP_OUT,     OP_PARM_Ib,         OP_PARM_REG_AL, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),
    OP("out %Ib,%eAX",       IDX_ParseImmByte,   IDX_ParseFixedReg, 0,          OP_OUT,     OP_PARM_Ib,         OP_PARM_REG_EAX,OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),
    OP("call %Jv",           IDX_ParseImmVRel,   0,                 0,          OP_CALL,    OP_PARM_Jv,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("jmp %Jv",            IDX_ParseImmVRel,   0,                 0,          OP_JMP,     OP_PARM_Jv,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    INVALID_OPCODE,
    OP("jmp %Jb",            IDX_ParseImmBRel,   0,                 0,          OP_JMP,     OP_PARM_Jb,         OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW | DISOPTYPE_RELATIVE_CONTROLFLOW | DISOPTYPE_X86_FORCED_64_OP_SIZE),
    OP("in AL,DX",           IDX_ParseFixedReg,  IDX_ParseFixedReg, 0,          OP_IN,      OP_PARM_REG_AL,     OP_PARM_REG_DX, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("in %eAX,DX",         IDX_ParseFixedReg,  IDX_ParseFixedReg, 0,          OP_IN,      OP_PARM_REG_EAX,    OP_PARM_REG_DX, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_READ),
    OP("out DX,AL",          IDX_ParseFixedReg,  IDX_ParseFixedReg, 0,          OP_OUT,     OP_PARM_REG_DX,     OP_PARM_REG_AL, OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),
    OP("out DX,%eAX",        IDX_ParseFixedReg,  IDX_ParseFixedReg, 0,          OP_OUT,     OP_PARM_REG_DX,     OP_PARM_REG_EAX,OP_PARM_NONE,   DISOPTYPE_PORTIO | DISOPTYPE_PRIVILEGED | DISOPTYPE_X86_PORTIO_WRITE),


    /* F */
    OP("lock",               0,              0,          0,          OP_LOCK,    OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("icebp",              0,              0,          0,          OP_INT1,    OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_INTERRUPT),
    OP("repne",              0,              0,          0,          OP_REPNE,   OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("rep(e)",             0,              0,          0,          OP_REPE,    OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("hlt",                0,              0,          0,          OP_HLT,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS | DISOPTYPE_PRIVILEGED),
    OP("cmc",                0,              0,          0,          OP_CMC,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Unary Grp3 %Eb",     IDX_ParseGrp3,  0,          0,          OP_UNARY_GRP3,  OP_PARM_Eb,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Unary Grp3 %Ev",     IDX_ParseGrp3,  0,          0,          OP_UNARY_GRP3,  OP_PARM_Ev,     OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("clc",                0,              0,          0,          OP_CLC,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("stc",                0,              0,          0,          OP_STC,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("cli",                0,              0,          0,          OP_CLI,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED),
    OP("sti",                0,              0,          0,          OP_STI,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED | DISOPTYPE_INHIBIT_IRQS),
    OP("cld",                0,              0,          0,          OP_CLD,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("std",                0,              0,          0,          OP_STD,     OP_PARM_NONE,       OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("inc/dec Grp4",       IDX_ParseGrp4,  0,          0,          OP_INC_GRP4, OP_PARM_NONE,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
    OP("Indirect Grp5",      IDX_ParseGrp5,  0,          0,          OP_IND_GRP5, OP_PARM_NONE,      OP_PARM_NONE,   OP_PARM_NONE,   DISOPTYPE_HARMLESS),
};

