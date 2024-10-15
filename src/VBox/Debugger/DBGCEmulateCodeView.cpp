/* $Id: DBGCEmulateCodeView.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, CodeView / WinDbg Emulation.
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
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgfflowtrace.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/dis.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/time.h>

#include <stdlib.h>
#include <stdio.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDBGCCMD dbgcCmdBrkAccess;
static FNDBGCCMD dbgcCmdBrkClear;
static FNDBGCCMD dbgcCmdBrkDisable;
static FNDBGCCMD dbgcCmdBrkEnable;
static FNDBGCCMD dbgcCmdBrkList;
static FNDBGCCMD dbgcCmdBrkSet;
static FNDBGCCMD dbgcCmdBrkREM;
static FNDBGCCMD dbgcCmdDumpMem;
static FNDBGCCMD dbgcCmdDumpDT;
static FNDBGCCMD dbgcCmdDumpIDT;
static FNDBGCCMD dbgcCmdDumpPageDir;
static FNDBGCCMD dbgcCmdDumpPageDirBoth;
static FNDBGCCMD dbgcCmdDumpPageHierarchy;
static FNDBGCCMD dbgcCmdDumpPageTable;
static FNDBGCCMD dbgcCmdDumpPageTableBoth;
static FNDBGCCMD dbgcCmdDumpTSS;
static FNDBGCCMD dbgcCmdDumpTypeInfo;
static FNDBGCCMD dbgcCmdDumpTypedVal;
static FNDBGCCMD dbgcCmdEditMem;
static FNDBGCCMD dbgcCmdGo;
static FNDBGCCMD dbgcCmdGoUp;
static FNDBGCCMD dbgcCmdListModules;
static FNDBGCCMD dbgcCmdListNear;
static FNDBGCCMD dbgcCmdListSource;
static FNDBGCCMD dbgcCmdListSymbols;
static FNDBGCCMD dbgcCmdMemoryInfo;
static FNDBGCCMD dbgcCmdReg;
static FNDBGCCMD dbgcCmdRegGuest;
static FNDBGCCMD dbgcCmdRegTerse;
static FNDBGCCMD dbgcCmdSearchMem;
static FNDBGCCMD dbgcCmdSearchMemType;
static FNDBGCCMD dbgcCmdStepTrace;
static FNDBGCCMD dbgcCmdStepTraceTo;
static FNDBGCCMD dbgcCmdStepTraceToggle;
static FNDBGCCMD dbgcCmdEventCtrl;
static FNDBGCCMD dbgcCmdEventCtrlList;
static FNDBGCCMD dbgcCmdEventCtrlReset;
static FNDBGCCMD dbgcCmdStack;
static FNDBGCCMD dbgcCmdUnassemble;
static FNDBGCCMD dbgcCmdUnassembleCfg;
static FNDBGCCMD dbgcCmdTraceFlowClear;
static FNDBGCCMD dbgcCmdTraceFlowDisable;
static FNDBGCCMD dbgcCmdTraceFlowEnable;
static FNDBGCCMD dbgcCmdTraceFlowPrint;
static FNDBGCCMD dbgcCmdTraceFlowReset;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** 'ba' arguments. */
static const DBGCVARDESC    g_aArgBrkAcc[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "access",       "The access type: x=execute, rw=read/write (alias r), w=write, i=not implemented." },
    {  1,           1,          DBGCVAR_CAT_NUMBER,     0,                              "size",         "The access size: 1, 2, 4, or 8. 'x' access requires 1, and 8 requires amd64 long mode." },
    {  1,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "address",      "The address." },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "passes",       "The number of passes before we trigger the breakpoint. (0 is default)" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "max passes",   "The number of passes after which we stop triggering the breakpoint. (~0 is default)" },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "cmds",         "String of commands to be executed when the breakpoint is hit. Quote it!" },
};


/** 'bc', 'bd', 'be' arguments. */
static const DBGCVARDESC    g_aArgBrks[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "#bp",          "Breakpoint number." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "all",          "All breakpoints." },
};


/** 'bp' arguments. */
static const DBGCVARDESC    g_aArgBrkSet[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "address",      "The address." },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "passes",       "The number of passes before we trigger the breakpoint. (0 is default)" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "max passes",   "The number of passes after which we stop triggering the breakpoint. (~0 is default)" },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "cmds",         "String of commands to be executed when the breakpoint is hit. Quote it!" },
};


/** 'br' arguments. */
static const DBGCVARDESC    g_aArgBrkREM[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "address",      "The address." },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "passes",       "The number of passes before we trigger the breakpoint. (0 is default)" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "max passes",   "The number of passes after which we stop triggering the breakpoint. (~0 is default)" },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "cmds",         "String of commands to be executed when the breakpoint is hit. Quote it!" },
};


/** 'd?' arguments. */
static const DBGCVARDESC    g_aArgDumpMem[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address where to start dumping memory. Tip: Use the L or LB operator to specify how may items or bytes to dump." },
};


/** 'dg', 'dga', 'dl', 'dla' arguments. */
static const DBGCVARDESC    g_aArgDumpDT[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "sel",          "Selector or selector range." },
    {  0,           ~0U,        DBGCVAR_CAT_POINTER,    0,                              "address",      "Far address which selector should be dumped." },
};


/** 'di', 'dia' arguments. */
static const DBGCVARDESC    g_aArgDumpIDT[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "int",          "The interrupt vector or interrupt vector range." },
};


/** 'dpd*' arguments. */
static const DBGCVARDESC    g_aArgDumpPD[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "index",        "Index into the page directory." },
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address which page directory entry to start dumping from. Range is applied to the page directory." },
};


/** 'dpda' arguments. */
static const DBGCVARDESC    g_aArgDumpPDAddr[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address of the page directory entry to start dumping from." },
};


/** 'dph*' arguments. */
static const DBGCVARDESC    g_aArgDumpPH[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "address",      "Where in the address space to start dumping and for how long (range).  The default address/range will be used if omitted." },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "cr3",          "The CR3 value to use.  The current CR3 of the context will be used if omitted." },
    {  0,           1,          DBGCVAR_CAT_STRING,     DBGCVD_FLAGS_DEP_PREV,          "mode",         "The paging mode: legacy, pse, pae, long, ept. Append '-np' for nested paging and '-nx' for no-execute.  The current mode will be used if omitted." },
};


/** 'dpt?' arguments. */
static const DBGCVARDESC    g_aArgDumpPT[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address which page directory entry to start dumping from." },
};


/** 'dpta' arguments. */
static const DBGCVARDESC    g_aArgDumpPTAddr[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address of the page table entry to start dumping from." },
};


/** 'dt' arguments. */
static const DBGCVARDESC    g_aArgDumpTSS[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "tss",          "TSS selector number." },
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "tss:ign|addr", "TSS address. If the selector is a TSS selector, the offset will be ignored." }
};


/** 'dti' arguments. */
static const DBGCVARDESC    g_aArgDumpTypeInfo[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "type",         "The type to dump" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "levels",       "How many levels to dump the type information" }
};


/** 'dtv' arguments. */
static const DBGCVARDESC    g_aArgDumpTypedVal[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "type",         "The type to use" },
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address to start dumping from." },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "levels",       "How many levels to dump" }
};


/** 'e?' arguments. */
static const DBGCVARDESC    g_aArgEditMem[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address where to write." },
    {  1,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "value",        "Value to write." },
};


/** 'g' arguments. */
static const DBGCVARDESC    g_aArgGo[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "idCpu",        "CPU ID." },
};


/** 'lm' arguments. */
static const DBGCVARDESC    g_aArgListMods[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "module",       "Module name." },
};


/** 'ln' arguments. */
static const DBGCVARDESC    g_aArgListNear[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_POINTER,    0,                              "address",      "Address of the symbol to look up." },
    {  0,           ~0U,        DBGCVAR_CAT_SYMBOL,     0,                              "symbol",       "Symbol to lookup." },
};


/** 'ls' arguments. */
static const DBGCVARDESC    g_aArgListSource[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address where to start looking for source lines." },
};


/** 'm' argument. */
static const DBGCVARDESC    g_aArgMemoryInfo[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Pointer to obtain info about." },
};


/** 'p', 'pc', 'pt', 't', 'tc' and 'tt' arguments. */
static const DBGCVARDESC    g_aArgStepTrace[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "count",        "Number of instructions or source lines to step." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "cmds",         "String of commands to be executed afterwards. Quote it!" },
};


/** 'pa' and 'ta' arguments. */
static const DBGCVARDESC    g_aArgStepTraceTo[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Where to stop" },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "cmds",         "String of commands to be executed afterwards. Quote it!" },
};


/** 'r' arguments. */
static const DBGCVARDESC    g_aArgReg[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_SYMBOL,     0,                              "register",     "Register to show or set." },
    {  0,           1,          DBGCVAR_CAT_STRING, DBGCVD_FLAGS_DEP_PREV,              "=",            "Equal sign." },
    {  0,           1,          DBGCVAR_CAT_NUMBER, DBGCVD_FLAGS_DEP_PREV,              "value",        "New register value." },
};


/** 's' arguments. */
static const DBGCVARDESC    g_aArgSearchMem[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-b",           "Byte string." },
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-w",           "Word string." },
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-d",           "DWord string." },
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-q",           "QWord string." },
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-a",           "ASCII string." },
    {  0,           1,          DBGCVAR_CAT_OPTION,     0,                              "-u",           "Unicode string." },
    {  0,           1,          DBGCVAR_CAT_OPTION_NUMBER, 0,                           "-n <Hits>",    "Maximum number of hits." },
    {  0,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "range",        "Register to show or set." },
    {  0,           ~0U,        DBGCVAR_CAT_ANY,        0,                              "pattern",      "Pattern to search for." },
};


/** 's?' arguments. */
static const DBGCVARDESC    g_aArgSearchMemType[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "range",        "Register to show or set." },
    {  1,           ~0U,        DBGCVAR_CAT_ANY,        0,                              "pattern",      "Pattern to search for." },
};


/** 'sxe', 'sxn', 'sxi', 'sx-' arguments. */
static const DBGCVARDESC    g_aArgEventCtrl[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "-c",           "The -c option, requires <cmds>." },
    {  0,           1,          DBGCVAR_CAT_STRING,     DBGCVD_FLAGS_DEP_PREV,          "cmds",         "Command to execute on this event." },
    {  0 /*weird*/, ~0U,        DBGCVAR_CAT_STRING,     0,                              "event",        "One or more events, 'all' refering to all events." },
};

/** 'sx' and 'sr' arguments. */
static const DBGCVARDESC    g_aArgEventCtrlOpt[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "event",        "Zero or more events, 'all' refering to all events and being the default." },
};

/** 'u' arguments. */
static const DBGCVARDESC    g_aArgUnassemble[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address where to start disassembling." },
};

/** 'ucfg' arguments. */
static const DBGCVARDESC    g_aArgUnassembleCfg[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "Address where to start disassembling." },
};

/** 'x' arguments. */
static const DBGCVARDESC    g_aArgListSyms[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "symbols",      "The symbols to list, format is Module!Symbol with wildcards being supoprted." }
};

/** 'tflowc' arguments. */
static const DBGCVARDESC    g_aArgTraceFlowClear[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "#tf",          "Trace flow module number." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "all",          "All trace flow modules." },
};

/** 'tflowd' arguments. */
static const DBGCVARDESC    g_aArgTraceFlowDisable[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "#tf",          "Trace flow module number." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "all",          "All trace flow modules." },
};

/** 'tflowe' arguments. */
static const DBGCVARDESC    g_aArgTraceFlowEnable[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,       0,                           "address",      "Address where to start tracing." },
    {  0,           1,          DBGCVAR_CAT_OPTION_NUMBER, 0,                           "<Hits>",       "Maximum number of hits before the module is disabled." }
};

/** 'tflowp', 'tflowr' arguments. */
static const DBGCVARDESC    g_aArgTraceFlowPrintReset[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_NUMBER,     0,                              "#tf",          "Trace flow module number." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "all",          "All trace flow modules." },
};

/** Command descriptors for the CodeView / WinDbg emulation.
 * The emulation isn't attempting to be identical, only somewhat similar.
 */
const DBGCCMD    g_aCmdsCodeView[] =
{
    /* pszCmd,      cArgsMin, cArgsMax, paArgDescs,         cArgDescs,                      fFlags,  pfnHandler          pszSyntax,          ....pszDescription */
    { "ba",         3,        6,        &g_aArgBrkAcc[0],   RT_ELEMENTS(g_aArgBrkAcc),      0,       dbgcCmdBrkAccess,   "<access> <size> <address> [passes [max passes]] [cmds]",
                                                                                                                                                 "Sets a data access breakpoint." },
    { "bc",         1,       ~0U,       &g_aArgBrks[0],     RT_ELEMENTS(g_aArgBrks),        0,       dbgcCmdBrkClear,    "all | <bp#> [bp# []]", "Deletes a set of breakpoints." },
    { "bd",         1,       ~0U,       &g_aArgBrks[0],     RT_ELEMENTS(g_aArgBrks),        0,       dbgcCmdBrkDisable,  "all | <bp#> [bp# []]", "Disables a set of breakpoints." },
    { "be",         1,       ~0U,       &g_aArgBrks[0],     RT_ELEMENTS(g_aArgBrks),        0,       dbgcCmdBrkEnable,   "all | <bp#> [bp# []]", "Enables a set of breakpoints." },
    { "bl",         0,        0,        NULL,               0,                              0,       dbgcCmdBrkList,     "",                     "Lists all the breakpoints." },
    { "bp",         1,        4,        &g_aArgBrkSet[0],   RT_ELEMENTS(g_aArgBrkSet),      0,       dbgcCmdBrkSet,      "<address> [passes [max passes]] [cmds]",
                                                                                                                                                 "Sets a breakpoint (int 3)." },
    { "br",         1,        4,        &g_aArgBrkREM[0],   RT_ELEMENTS(g_aArgBrkREM),      0,       dbgcCmdBrkREM,      "<address> [passes [max passes]] [cmds]",
                                                                                                                                                 "Sets a recompiler specific breakpoint." },
    { "d",          0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory using last element size and type." },
    { "dF",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as far 16:16." },
    { "dFs",        0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as far 16:16 with near symbols." },
    { "da",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as ascii string." },
    { "db",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in bytes." },
    { "dd",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in double words." },
    { "dds",        0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as double words with near symbols." },
    { "da",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as ascii string." },
    { "dg",         0,       ~0U,       &g_aArgDumpDT[0],   RT_ELEMENTS(g_aArgDumpDT),      0,       dbgcCmdDumpDT,      "[sel [..]]",           "Dump the global descriptor table (GDT)." },
    { "dga",        0,       ~0U,       &g_aArgDumpDT[0],   RT_ELEMENTS(g_aArgDumpDT),      0,       dbgcCmdDumpDT,      "[sel [..]]",           "Dump the global descriptor table (GDT) including not-present entries." },
    { "di",         0,       ~0U,       &g_aArgDumpIDT[0],  RT_ELEMENTS(g_aArgDumpIDT),     0,       dbgcCmdDumpIDT,     "[int [..]]",           "Dump the interrupt descriptor table (IDT)." },
    { "dia",        0,       ~0U,       &g_aArgDumpIDT[0],  RT_ELEMENTS(g_aArgDumpIDT),     0,       dbgcCmdDumpIDT,     "[int [..]]",           "Dump the interrupt descriptor table (IDT) including not-present entries." },
    { "dl",         0,       ~0U,       &g_aArgDumpDT[0],   RT_ELEMENTS(g_aArgDumpDT),      0,       dbgcCmdDumpDT,      "[sel [..]]",           "Dump the local descriptor table (LDT)." },
    { "dla",        0,       ~0U,       &g_aArgDumpDT[0],   RT_ELEMENTS(g_aArgDumpDT),      0,       dbgcCmdDumpDT,      "[sel [..]]",           "Dump the local descriptor table (LDT) including not-present entries." },
    { "dpd",        0,        1,        &g_aArgDumpPD[0],   RT_ELEMENTS(g_aArgDumpPD),      0,       dbgcCmdDumpPageDir, "[addr|index]",         "Dumps page directory entries of the default context." },
    { "dpda",       0,        1,        &g_aArgDumpPDAddr[0],RT_ELEMENTS(g_aArgDumpPDAddr), 0,       dbgcCmdDumpPageDir, "[addr]",               "Dumps memory at given address as a page directory." },
    { "dpdb",       0,        1,        &g_aArgDumpPD[0],   RT_ELEMENTS(g_aArgDumpPD),      0,       dbgcCmdDumpPageDirBoth, "[addr|index]",     "Dumps page directory entries of the guest and the hypervisor. " },
    { "dpdg",       0,        1,        &g_aArgDumpPD[0],   RT_ELEMENTS(g_aArgDumpPD),      0,       dbgcCmdDumpPageDir, "[addr|index]",         "Dumps page directory entries of the guest." },
    { "dpdh",       0,        1,        &g_aArgDumpPD[0],   RT_ELEMENTS(g_aArgDumpPD),      0,       dbgcCmdDumpPageDir, "[addr|index]",         "Dumps page directory entries of the hypervisor. " },
    { "dph",        0,        3,        &g_aArgDumpPH[0],   RT_ELEMENTS(g_aArgDumpPH),      0, dbgcCmdDumpPageHierarchy, "[addr [cr3 [mode]]",   "Dumps the paging hierarchy at for specfied address range. Default context." },
    { "dphg",       0,        3,        &g_aArgDumpPH[0],   RT_ELEMENTS(g_aArgDumpPH),      0, dbgcCmdDumpPageHierarchy, "[addr [cr3 [mode]]",   "Dumps the paging hierarchy at for specfied address range. Guest context." },
    { "dphh",       0,        3,        &g_aArgDumpPH[0],   RT_ELEMENTS(g_aArgDumpPH),      0, dbgcCmdDumpPageHierarchy, "[addr [cr3 [mode]]",   "Dumps the paging hierarchy at for specfied address range. Hypervisor context." },
    { "dp",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in mode sized words." },
    { "dps",        0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in mode sized words with near symbols." },
    { "dpt",        1,        1,        &g_aArgDumpPT[0],   RT_ELEMENTS(g_aArgDumpPT),      0,       dbgcCmdDumpPageTable,"<addr>",              "Dumps page table entries of the default context." },
    { "dpta",       1,        1,        &g_aArgDumpPTAddr[0],RT_ELEMENTS(g_aArgDumpPTAddr), 0,       dbgcCmdDumpPageTable,"<addr>",              "Dumps memory at given address as a page table." },
    { "dptb",       1,        1,        &g_aArgDumpPT[0],   RT_ELEMENTS(g_aArgDumpPT),      0,       dbgcCmdDumpPageTableBoth,"<addr>",          "Dumps page table entries of the guest and the hypervisor." },
    { "dptg",       1,        1,        &g_aArgDumpPT[0],   RT_ELEMENTS(g_aArgDumpPT),      0,       dbgcCmdDumpPageTable,"<addr>",              "Dumps page table entries of the guest." },
    { "dpth",       1,        1,        &g_aArgDumpPT[0],   RT_ELEMENTS(g_aArgDumpPT),      0,       dbgcCmdDumpPageTable,"<addr>",              "Dumps page table entries of the hypervisor." },
    { "dq",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in quad words." },
    { "dqs",        0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as quad words with near symbols." },
    { "dt",         0,        1,        &g_aArgDumpTSS[0],  RT_ELEMENTS(g_aArgDumpTSS),     0,       dbgcCmdDumpTSS,     "[tss|tss:ign|addr]",   "Dump the task state segment (TSS)." },
    { "dt16",       0,        1,        &g_aArgDumpTSS[0],  RT_ELEMENTS(g_aArgDumpTSS),     0,       dbgcCmdDumpTSS,     "[tss|tss:ign|addr]",   "Dump the 16-bit task state segment (TSS)." },
    { "dt32",       0,        1,        &g_aArgDumpTSS[0],  RT_ELEMENTS(g_aArgDumpTSS),     0,       dbgcCmdDumpTSS,     "[tss|tss:ign|addr]",   "Dump the 32-bit task state segment (TSS)." },
    { "dt64",       0,        1,        &g_aArgDumpTSS[0],  RT_ELEMENTS(g_aArgDumpTSS),     0,       dbgcCmdDumpTSS,     "[tss|tss:ign|addr]",   "Dump the 64-bit task state segment (TSS)." },
    { "dti",        1,        2,        &g_aArgDumpTypeInfo[0],RT_ELEMENTS(g_aArgDumpTypeInfo), 0,   dbgcCmdDumpTypeInfo,"<type> [levels]",      "Dump type information." },
    { "dtv",        2,        3,        &g_aArgDumpTypedVal[0],RT_ELEMENTS(g_aArgDumpTypedVal), 0,   dbgcCmdDumpTypedVal,"<type> <addr> [levels]", "Dump a memory buffer using the information in the given type." },
    { "du",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory as unicode string (little endian)." },
    { "dw",         0,        1,        &g_aArgDumpMem[0],  RT_ELEMENTS(g_aArgDumpMem),     0,       dbgcCmdDumpMem,     "[addr]",               "Dump memory in words." },
    /** @todo add 'e', 'ea str', 'eza str', 'eu str' and 'ezu str'. See also
     *        dbgcCmdSearchMem and its dbgcVarsToBytes usage. */
    { "eb",         2,        2,        &g_aArgEditMem[0],  RT_ELEMENTS(g_aArgEditMem),     0,       dbgcCmdEditMem,     "<addr> <value>",       "Write a 1-byte value to memory." },
    { "ew",         2,        2,        &g_aArgEditMem[0],  RT_ELEMENTS(g_aArgEditMem),     0,       dbgcCmdEditMem,     "<addr> <value>",       "Write a 2-byte value to memory." },
    { "ed",         2,        2,        &g_aArgEditMem[0],  RT_ELEMENTS(g_aArgEditMem),     0,       dbgcCmdEditMem,     "<addr> <value>",       "Write a 4-byte value to memory." },
    { "eq",         2,        2,        &g_aArgEditMem[0],  RT_ELEMENTS(g_aArgEditMem),     0,       dbgcCmdEditMem,     "<addr> <value>",       "Write a 8-byte value to memory." },
    { "g",          0,        1,        &g_aArgGo[0],       RT_ELEMENTS(g_aArgGo),          0,       dbgcCmdGo,          "[idCpu]",              "Continue execution of all or the specified CPU. (The latter is not recommended unless you know exactly what you're doing.)" },
    { "gu",         0,        0,        NULL,               0,                              0,       dbgcCmdGoUp,        "",                     "Go up - continue execution till after return." },
    { "k",          0,        0,        NULL,               0,                              0,       dbgcCmdStack,       "",                     "Callstack." },
    { "kv",         0,        0,        NULL,               0,                              0,       dbgcCmdStack,       "",                     "Verbose callstack." },
    { "kg",         0,        0,        NULL,               0,                              0,       dbgcCmdStack,       "",                     "Callstack - guest." },
    { "kgv",        0,        0,        NULL,               0,                              0,       dbgcCmdStack,       "",                     "Verbose callstack - guest." },
    { "kh",         0,        0,        NULL,               0,                              0,       dbgcCmdStack,       "",                     "Callstack - hypervisor." },
    { "lm",         0,        ~0U,      &g_aArgListMods[0], RT_ELEMENTS(g_aArgListMods),    0,       dbgcCmdListModules, "[module [..]]",        "List modules." },
    { "lmv",        0,        ~0U,      &g_aArgListMods[0], RT_ELEMENTS(g_aArgListMods),    0,       dbgcCmdListModules, "[module [..]]",        "List modules, verbose." },
    { "lmo",        0,        ~0U,      &g_aArgListMods[0], RT_ELEMENTS(g_aArgListMods),    0,       dbgcCmdListModules, "[module [..]]",        "List modules and their segments." },
    { "lmov",       0,        ~0U,      &g_aArgListMods[0], RT_ELEMENTS(g_aArgListMods),    0,       dbgcCmdListModules, "[module [..]]",        "List modules and their segments, verbose." },
    { "ln",         0,        ~0U,      &g_aArgListNear[0], RT_ELEMENTS(g_aArgListNear),    0,       dbgcCmdListNear,    "[addr/sym [..]]",      "List symbols near to the address. Default address is CS:EIP." },
    { "ls",         0,        1,        &g_aArgListSource[0],RT_ELEMENTS(g_aArgListSource), 0,       dbgcCmdListSource,  "[addr]",               "Source." },
    { "m",          1,        1,        &g_aArgMemoryInfo[0],RT_ELEMENTS(g_aArgMemoryInfo), 0,       dbgcCmdMemoryInfo,  "<addr>",               "Display information about that piece of memory." },
    { "p",          0,        2,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Step over." },
    { "pr",         0,        0,        NULL,               0,                              0,       dbgcCmdStepTraceToggle, "",                 "Toggle displaying registers for tracing & stepping (no code executed)." },
    { "pa",         1,        1,        &g_aArgStepTraceTo[0], RT_ELEMENTS(g_aArgStepTraceTo), 0,    dbgcCmdStepTraceTo, "<addr> [count] [cmds]","Step to the given address." },
    { "pc",         0,        0,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Step to the next call instruction." },
    { "pt",         0,        0,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Step to the next return instruction." },
    { "r",          0,        3,        &g_aArgReg[0],      RT_ELEMENTS(g_aArgReg),         0,       dbgcCmdReg,         "[reg [[=] newval]]",   "Show or set register(s) - active reg set. Special 'all' register for showing all. Append a dot '.' to display sub-fields and aliases." },
    { "rg",         0,        3,        &g_aArgReg[0],      RT_ELEMENTS(g_aArgReg),         0,       dbgcCmdRegGuest,    "[reg [[=] newval]]",   "Show or set register(s) - guest reg set. Special 'all' register for showing all. Append a dot '.' to display sub-fields and aliases." },
    { "rg32",       0,        0,        NULL,               0,                              0,       dbgcCmdRegGuest,    "",                     "Show 32-bit guest registers." },
    { "rg64",       0,        0,        NULL,               0,                              0,       dbgcCmdRegGuest,    "",                     "Show 64-bit guest registers." },
    { "rt",         0,        0,        NULL,               0,                              0,       dbgcCmdRegTerse,    "",                     "Toggles terse / verbose register info." },
    { "s",          0,       ~0U,       &g_aArgSearchMem[0], RT_ELEMENTS(g_aArgSearchMem),  0,       dbgcCmdSearchMem,   "[options] <range> <pattern>",  "Continue last search." },
    { "sa",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for an ascii string." },
    { "sb",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for one or more bytes." },
    { "sd",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for one or more double words." },
    { "sq",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for one or more quad words." },
    { "su",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for an unicode string." },
    { "sw",         2,       ~0U,       &g_aArgSearchMemType[0], RT_ELEMENTS(g_aArgSearchMemType),0, dbgcCmdSearchMemType, "<range> <pattern>",  "Search memory for one or more words." },
    { "sx",         0,       ~0U,       &g_aArgEventCtrlOpt[0], RT_ELEMENTS(g_aArgEventCtrlOpt), 0,  dbgcCmdEventCtrlList,  "[<event> [..]]", "Lists settings for exceptions, exits and other events.  All if no filter is specified." },
    { "sx-",        3,       ~0U,       &g_aArgEventCtrl[0], RT_ELEMENTS(g_aArgEventCtrl),  0,       dbgcCmdEventCtrl,      "-c <cmd> <event> [..]", "Modifies the command for one or more exceptions, exits or other event.  'all' addresses all." },
    { "sxe",        1,       ~0U,       &g_aArgEventCtrl[0], RT_ELEMENTS(g_aArgEventCtrl),  0,       dbgcCmdEventCtrl,      "[-c <cmd>] <event> [..]", "Enable: Break into the debugger on the specified exceptions, exits and other events.  'all' addresses all." },
    { "sxn",        1,       ~0U,       &g_aArgEventCtrl[0], RT_ELEMENTS(g_aArgEventCtrl),  0,       dbgcCmdEventCtrl,      "[-c <cmd>] <event> [..]", "Notify: Display info in the debugger and continue on the specified exceptions, exits and other events. 'all' addresses all." },
    { "sxi",        1,       ~0U,       &g_aArgEventCtrl[0], RT_ELEMENTS(g_aArgEventCtrl),  0,       dbgcCmdEventCtrl,      "[-c <cmd>] <event> [..]", "Ignore: Ignore the specified exceptions, exits and other events ('all' = all of them).  Without the -c option, the guest runs like normal." },
    { "sxr",        0,        0,        &g_aArgEventCtrlOpt[0], RT_ELEMENTS(g_aArgEventCtrlOpt), 0,  dbgcCmdEventCtrlReset, "",                    "Reset the settings to default for exceptions, exits and other events. All if no filter is specified." },
    { "t",          0,        2,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Trace ." },
    { "tflowc",     1,       ~0U,       &g_aArgTraceFlowClear[0],   RT_ELEMENTS(g_aArgTraceFlowClear),   0, dbgcCmdTraceFlowClear,   "all | <tf#> [tf# []]", "Clears trace execution flow for the given method." },
    { "tflowd",     0,        1,        &g_aArgTraceFlowDisable[0], RT_ELEMENTS(g_aArgTraceFlowDisable), 0, dbgcCmdTraceFlowDisable, "all | <tf#> [tf# []]", "Disables trace execution flow for the given method." },
    { "tflowe",     0,        2,        &g_aArgTraceFlowEnable[0],  RT_ELEMENTS(g_aArgTraceFlowEnable),  0, dbgcCmdTraceFlowEnable,  "<addr> <hits>",        "Enable trace execution flow of the given method." },
    { "tflowp",     0,        1,        &g_aArgTraceFlowPrintReset[0],   RT_ELEMENTS(g_aArgTraceFlowPrintReset),   0, dbgcCmdTraceFlowPrint,   "all | <tf#> [tf# []]", "Prints the collected trace data of the given method." },
    { "tflowr",     0,        1,        &g_aArgTraceFlowPrintReset[0],   RT_ELEMENTS(g_aArgTraceFlowPrintReset),   0, dbgcCmdTraceFlowReset,   "all | <tf#> [tf# []]", "Resets the collected trace data of the given trace flow module." },
    { "tr",         0,        0,        NULL,               0,                              0,       dbgcCmdStepTraceToggle, "",                 "Toggle displaying registers for tracing & stepping (no code executed)." },
    { "ta",         1,        1,        &g_aArgStepTraceTo[0], RT_ELEMENTS(g_aArgStepTraceTo), 0,    dbgcCmdStepTraceTo, "<addr> [count] [cmds]","Trace to the given address." },
    { "tc",         0,        0,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Trace to the next call instruction." },
    { "tt",         0,        0,        &g_aArgStepTrace[0], RT_ELEMENTS(g_aArgStepTrace),  0,       dbgcCmdStepTrace,   "[count] [cmds]",       "Trace to the next return instruction." },
    { "u",          0,        1,        &g_aArgUnassemble[0],RT_ELEMENTS(g_aArgUnassemble), 0,       dbgcCmdUnassemble,  "[addr]",               "Unassemble." },
    { "u64",        0,        1,        &g_aArgUnassemble[0],RT_ELEMENTS(g_aArgUnassemble), 0,       dbgcCmdUnassemble,  "[addr]",               "Unassemble 64-bit code." },
    { "u32",        0,        1,        &g_aArgUnassemble[0],RT_ELEMENTS(g_aArgUnassemble), 0,       dbgcCmdUnassemble,  "[addr]",               "Unassemble 32-bit code." },
    { "u16",        0,        1,        &g_aArgUnassemble[0],RT_ELEMENTS(g_aArgUnassemble), 0,       dbgcCmdUnassemble,  "[addr]",               "Unassemble 16-bit code." },
    { "uv86",       0,        1,        &g_aArgUnassemble[0],RT_ELEMENTS(g_aArgUnassemble), 0,       dbgcCmdUnassemble,  "[addr]",               "Unassemble 16-bit code with v8086/real mode addressing." },
    { "ucfg",       0,        1,        &g_aArgUnassembleCfg[0], RT_ELEMENTS(g_aArgUnassembleCfg), 0, dbgcCmdUnassembleCfg,  "[addr]",               "Unassemble creating a control flow graph." },
    { "ucfgc",      0,        1,        &g_aArgUnassembleCfg[0], RT_ELEMENTS(g_aArgUnassembleCfg), 0, dbgcCmdUnassembleCfg,  "[addr]",               "Unassemble creating a control flow graph with colors." },
    { "x",          1,        1,        &g_aArgListSyms[0], RT_ELEMENTS(g_aArgListSyms),    0,       dbgcCmdListSymbols,  "* | <Module!Symbol>", "Examine symbols." },
};

/** The number of commands in the CodeView/WinDbg emulation. */
const uint32_t g_cCmdsCodeView = RT_ELEMENTS(g_aCmdsCodeView);


/**
 * Selectable debug event descriptors.
 *
 * @remarks  Sorted by DBGCSXEVT::enmType value.
 */
const DBGCSXEVT g_aDbgcSxEvents[] =
{
    { DBGFEVENT_INTERRUPT_HARDWARE,     "hwint",                NULL,       kDbgcSxEventKind_Interrupt, kDbgcEvtState_Disabled, 0,                    "Hardware interrupt" },
    { DBGFEVENT_INTERRUPT_SOFTWARE,     "swint",                NULL,       kDbgcSxEventKind_Interrupt, kDbgcEvtState_Disabled, 0,                    "Software interrupt" },
    { DBGFEVENT_TRIPLE_FAULT,           "triplefault",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Enabled,  0,                    "Triple fault "},
    { DBGFEVENT_XCPT_DE,                "xcpt_de",              "de",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#DE (integer divide error)" },
    { DBGFEVENT_XCPT_DB,                "xcpt_db",              "db",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#DB (debug)" },
    { DBGFEVENT_XCPT_02,                "xcpt_02",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_BP,                "xcpt_bp",              "bp",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#BP (breakpoint)" },
    { DBGFEVENT_XCPT_OF,                "xcpt_of",              "of",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#OF (overflow (INTO))" },
    { DBGFEVENT_XCPT_BR,                "xcpt_br",              "br",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#BR (bound range exceeded)" },
    { DBGFEVENT_XCPT_UD,                "xcpt_ud",              "ud",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#UD (undefined opcode)" },
    { DBGFEVENT_XCPT_NM,                "xcpt_nm",              "nm",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#NM (FPU not available)" },
    { DBGFEVENT_XCPT_DF,                "xcpt_df",              "df",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#DF (double fault)" },
    { DBGFEVENT_XCPT_09,                "xcpt_09",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "Coprocessor segment overrun" },
    { DBGFEVENT_XCPT_TS,                "xcpt_ts",              "ts",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#TS (task switch)" },
    { DBGFEVENT_XCPT_NP,                "xcpt_np",              "np",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#NP (segment not present)" },
    { DBGFEVENT_XCPT_SS,                "xcpt_ss",              "ss",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#SS (stack segment fault)" },
    { DBGFEVENT_XCPT_GP,                "xcpt_gp",              "gp",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#GP (general protection fault)" },
    { DBGFEVENT_XCPT_PF,                "xcpt_pf",              "pf",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#PF (page fault)" },
    { DBGFEVENT_XCPT_0f,                "xcpt_0f",              "xcpt0f",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_MF,                "xcpt_mf",              "mf",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#MF (math fault)" },
    { DBGFEVENT_XCPT_AC,                "xcpt_ac",              "ac",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#AC (alignment check)" },
    { DBGFEVENT_XCPT_MC,                "xcpt_mc",              "mc",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#MC (machine check)" },
    { DBGFEVENT_XCPT_XF,                "xcpt_xf",              "xf",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#XF (SIMD floating-point exception)" },
    { DBGFEVENT_XCPT_VE,                "xcpt_vd",              "ve",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    "#VE (virtualization exception)" },
    { DBGFEVENT_XCPT_15,                "xcpt_15",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_16,                "xcpt_16",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_17,                "xcpt_17",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_18,                "xcpt_18",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_19,                "xcpt_19",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_1a,                "xcpt_1a",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_1b,                "xcpt_1b",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_1c,                "xcpt_1c",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_1d,                "xcpt_1d",              NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_XCPT_SX,                "xcpt_sx",              "sx",       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, "#SX (security exception)" },
    { DBGFEVENT_XCPT_1f,                "xcpt_1f",              "xcpt1f",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_HALT,             "instr_halt",           "hlt",      kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_MWAIT,            "instr_mwait",          "mwait",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_MONITOR,          "instr_monitor",        "monitor",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_CPUID,            "instr_cpuid",          "cpuid",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_INVD,             "instr_invd",           "invd",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_WBINVD,           "instr_wbinvd",         "wbinvd",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_INVLPG,           "instr_invlpg",         "invlpg",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDTSC,            "instr_rdtsc",          "rdtsc",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDTSCP,           "instr_rdtscp",         "rdtscp",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDPMC,            "instr_rdpmc",          "rdpmc",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDMSR,            "instr_rdmsr",          "rdmsr",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_WRMSR,            "instr_wrmsr",          "wrmsr",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_CRX_READ,         "instr_crx_read",       "crx_read", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, NULL },
    { DBGFEVENT_INSTR_CRX_WRITE,        "instr_crx_write",      "crx_write",kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, NULL },
    { DBGFEVENT_INSTR_DRX_READ,         "instr_drx_read",       "drx_read", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, NULL },
    { DBGFEVENT_INSTR_DRX_WRITE,        "instr_drx_write",      "drx_write",kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_TAKE_ARG, NULL },
    { DBGFEVENT_INSTR_PAUSE,            "instr_pause",          "pause",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_XSETBV,           "instr_xsetbv",         "xsetbv",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SIDT,             "instr_sidt",           "sidt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_LIDT,             "instr_lidt",           "lidt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SGDT,             "instr_sgdt",           "sgdt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_LGDT,             "instr_lgdt",           "lgdt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SLDT,             "instr_sldt",           "sldt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_LLDT,             "instr_lldt",           "lldt",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_STR,              "instr_str",            "str",      kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_LTR,              "instr_ltr",            "ltr",      kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_GETSEC,           "instr_getsec",         "getsec",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RSM,              "instr_rsm",            "rsm",      kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDRAND,           "instr_rdrand",         "rdrand",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_RDSEED,           "instr_rdseed",         "rdseed",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_XSAVES,           "instr_xsaves",         "xsaves",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_XRSTORS,          "instr_xrstors",        "xrstors",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMM_CALL,         "instr_vmm_call",       "vmm_call", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMCLEAR,      "instr_vmx_vmclear",    "vmclear",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMLAUNCH,     "instr_vmx_vmlaunch",   "vmlaunch", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMPTRLD,      "instr_vmx_vmptrld",    "vmptrld",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMPTRST,      "instr_vmx_vmptrst",    "vmptrst",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMREAD,       "instr_vmx_vmread",     "vmread",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMRESUME,     "instr_vmx_vmresume",   "vmresume", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMWRITE,      "instr_vmx_vmwrite",    "vmwrite",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMXOFF,       "instr_vmx_vmxoff",     "vmxoff",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMXON,        "instr_vmx_vmxon",      "vmxon",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_VMFUNC,       "instr_vmx_vmfunc",     "vmfunc",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_INVEPT,       "instr_vmx_invept",     "invept",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_INVVPID,      "instr_vmx_invvpid",    "invvpid",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_VMX_INVPCID,      "instr_vmx_invpcid",    "invpcid",  kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SVM_VMRUN,        "instr_svm_vmrun",      "vmrun",    kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SVM_VMLOAD,       "instr_svm_vmload",     "vmload",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SVM_VMSAVE,       "instr_svm_vmsave",     "vmsave",   kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SVM_STGI,         "instr_svm_stgi",       "stgi",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_INSTR_SVM_CLGI,         "instr_svm_clgi",       "clgi",     kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_TASK_SWITCH,       "exit_task_switch",  "task_switch", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_HALT,              "exit_halt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_MWAIT,             "exit_mwait",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_MONITOR,           "exit_monitor",         NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_CPUID,             "exit_cpuid",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_INVD,              "exit_invd",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_WBINVD,            "exit_wbinvd",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_INVLPG,            "exit_invlpg",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDTSC,             "exit_rdtsc",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDTSCP,            "exit_rdtscp",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDPMC,             "exit_rdpmc",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDMSR,             "exit_rdmsr",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_WRMSR,             "exit_wrmsr",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_CRX_READ,          "exit_crx_read",        NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_CRX_WRITE,         "exit_crx_write",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_DRX_READ,          "exit_drx_read",        NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_DRX_WRITE,         "exit_drx_write",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_PAUSE,             "exit_pause",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_XSETBV,            "exit_xsetbv",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SIDT,              "exit_sidt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_LIDT,              "exit_lidt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SGDT,              "exit_sgdt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_LGDT,              "exit_lgdt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SLDT,              "exit_sldt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_LLDT,              "exit_lldt",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_STR,               "exit_str",             NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_LTR,               "exit_ltr",             NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_GETSEC,            "exit_getsec",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RSM,               "exit_rsm",             NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDRAND,            "exit_rdrand",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_RDSEED,            "exit_rdseed",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_XSAVES,            "exit_xsaves",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_XRSTORS,           "exit_xrstors",         NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMM_CALL,          "exit_vmm_call",        NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMCLEAR,       "exit_vmx_vmclear",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMLAUNCH,      "exit_vmx_vmlaunch",    NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMPTRLD,       "exit_vmx_vmptrld",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMPTRST,       "exit_vmx_vmptrst",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMREAD,        "exit_vmx_vmread",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMRESUME,      "exit_vmx_vmresume",    NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMWRITE,       "exit_vmx_vmwrite",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMXOFF,        "exit_vmx_vmxoff",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMXON,         "exit_vmx_vmxon",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VMFUNC,        "exit_vmx_vmfunc",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_INVEPT,        "exit_vmx_invept",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_INVVPID,       "exit_vmx_invvpid",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_INVPCID,       "exit_vmx_invpcid",     NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_EPT_VIOLATION, "exit_vmx_ept_violation", "eptvio", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_EPT_MISCONFIG, "exit_vmx_ept_misconfig", "eptmis", kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VAPIC_ACCESS,  "exit_vmx_vapic_access", NULL,      kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_VMX_VAPIC_WRITE,   "exit_vmx_vapic_write", NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SVM_VMRUN,         "exit_svm_vmrun",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SVM_VMLOAD,        "exit_svm_vmload",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SVM_VMSAVE,        "exit_svm_vmsave",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SVM_STGI,          "exit_svm_stgi",        NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_EXIT_SVM_CLGI,          "exit_svm_clgi",        NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_VMX_SPLIT_LOCK,         "vmx_split_lock",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_IOPORT_UNASSIGNED,      "pio_unassigned",       NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_IOPORT_UNUSED,          "pio_unused",           NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_MEMORY_UNASSIGNED,      "mmio_unassigned",      NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_MEMORY_ROM_WRITE,       "rom_write",            NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, 0,                    NULL },
    { DBGFEVENT_BSOD_MSR,               "bsod_msr",             NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_BUGCHECK, NULL },
    { DBGFEVENT_BSOD_EFI,               "bsod_efi",             NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_BUGCHECK, NULL },
    { DBGFEVENT_BSOD_VMMDEV,            "bsod_vmmdev",          NULL,       kDbgcSxEventKind_Plain,     kDbgcEvtState_Disabled, DBGCSXEVT_F_BUGCHECK, NULL },
};
/** Number of entries in g_aDbgcSxEvents.  */
const uint32_t   g_cDbgcSxEvents = RT_ELEMENTS(g_aDbgcSxEvents);



/**
 * @callback_method_impl{FNDBGCCMD, The 'g' command.}
 */
static DECLCALLBACK(int) dbgcCmdGo(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Parse arguments.
     */
    VMCPUID idCpu = VMCPUID_ALL;
    if (cArgs == 1)
    {
        VMCPUID cCpus = DBGFR3CpuGetCount(pUVM);
        if (paArgs[0].u.u64Number >= cCpus)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "idCpu %RU64 is out of range! Highest valid ID is %u.\n",
                                  paArgs[0].u.u64Number, cCpus - 1);
        idCpu = (VMCPUID)paArgs[0].u.u64Number;
    }
    else
        Assert(cArgs == 0);

    /*
     * Try resume the VM or CPU.
     */
    int rc = DBGFR3Resume(pUVM, idCpu);
    if (RT_SUCCESS(rc))
    {
        Assert(rc == VINF_SUCCESS || rc == VWRN_DBGF_ALREADY_RUNNING);
        if (rc != VWRN_DBGF_ALREADY_RUNNING)
            return VINF_SUCCESS;
        if (idCpu == VMCPUID_ALL)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "The VM is already running");
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "CPU %u is already running", idCpu);
    }
    return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3Resume");
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'gu' command.}
 */
static DECLCALLBACK(int) dbgcCmdGoUp(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    RT_NOREF(pCmd, paArgs, cArgs);

    /* The simple way out. */
    PDBGFADDRESS pStackPop  = NULL; /** @todo try set up some stack limitations */
    RTGCPTR      cbStackPop = 0;
    int rc = DBGFR3StepEx(pUVM, pDbgc->idCpu, DBGF_STEP_F_OVER | DBGF_STEP_F_STOP_AFTER_RET, NULL, pStackPop, cbStackPop, _512K);
    if (RT_SUCCESS(rc))
        pDbgc->fReady = false;
    else
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3StepEx(,,DBGF_STEP_F_OVER | DBGF_STEP_F_STOP_AFTER_RET,) failed");
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'ba' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkAccess(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Interpret access type.
     */
    if (    !strchr("xrwi", paArgs[0].u.pszString[0])
        ||  paArgs[0].u.pszString[1])
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid access type '%s' for '%s'. Valid types are 'e', 'r', 'w' and 'i'",
                              paArgs[0].u.pszString, pCmd->pszCmd);
    uint8_t fType = 0;
    switch (paArgs[0].u.pszString[0])
    {
        case 'x':  fType = X86_DR7_RW_EO; break;
        case 'r':  fType = X86_DR7_RW_RW; break;
        case 'w':  fType = X86_DR7_RW_WO; break;
        case 'i':  fType = X86_DR7_RW_IO; break;
    }

    /*
     * Validate size.
     */
    if (fType == X86_DR7_RW_EO && paArgs[1].u.u64Number != 1)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid access size %RX64 for '%s'. 'x' access type requires size 1!",
                              paArgs[1].u.u64Number, pCmd->pszCmd);
    switch (paArgs[1].u.u64Number)
    {
        case 1:
        case 2:
        case 4:
            break;
        /*case 8: - later*/
        default:
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid access size %RX64 for '%s'. 1, 2 or 4!",
                                  paArgs[1].u.u64Number, pCmd->pszCmd);
    }
    uint8_t cb = (uint8_t)paArgs[1].u.u64Number;

    /*
     * Convert the pointer to a DBGF address.
     */
    DBGFADDRESS Address;
    int rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &paArgs[2], &Address);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr(,%DV,)", &paArgs[2]);

    /*
     * Pick out the optional arguments.
     */
    uint64_t iHitTrigger = 0;
    uint64_t iHitDisable = UINT64_MAX;
    const char *pszCmds = NULL;
    unsigned iArg = 3;
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
    {
        iHitTrigger = paArgs[iArg].u.u64Number;
        iArg++;
        if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
        {
            iHitDisable = paArgs[iArg].u.u64Number;
            iArg++;
        }
    }
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_STRING)
    {
        pszCmds = paArgs[iArg].u.pszString;
        iArg++;
    }

    /*
     * Try set the breakpoint.
     */
    uint32_t iBp;
    rc = DBGFR3BpSetReg(pUVM, &Address, iHitTrigger, iHitDisable, fType, cb, &iBp);
    if (RT_SUCCESS(rc))
    {
        PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
        rc = dbgcBpAdd(pDbgc, iBp, pszCmds);
        if (RT_SUCCESS(rc))
            return DBGCCmdHlpPrintf(pCmdHlp, "Set access breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        if (rc == VERR_DBGC_BP_EXISTS)
        {
            rc = dbgcBpUpdate(pDbgc, iBp, pszCmds);
            if (RT_SUCCESS(rc))
                return DBGCCmdHlpPrintf(pCmdHlp, "Updated access breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        }
        int rc2 = DBGFR3BpClear(pDbgc->pUVM, iBp);
        AssertRC(rc2);
    }
    return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to set access breakpoint at %RGv", Address.FlatPtr);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'bc' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkClear(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Enumerate the arguments.
     */
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int     rc    = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iBp = (uint32_t)paArgs[iArg].u.u64Number;
            if (iBp == paArgs[iArg].u.u64Number)
            {
                int rc2 = DBGFR3BpClear(pUVM, iBp);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3BpClear(,%#x)", iBp);
                if (RT_SUCCESS(rc2) || rc2 == VERR_DBGF_BP_NOT_FOUND)
                    dbgcBpDelete(pDbgc, iBp);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Breakpoint id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGCBP pBp = pDbgc->pFirstBp;
            while (pBp)
            {
                uint32_t iBp = pBp->iBp;
                pBp = pBp->pNext;

                int rc2 = DBGFR3BpClear(pUVM, iBp);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3BpClear(,%#x)", iBp);
                if (RT_SUCCESS(rc2) || rc2 == VERR_DBGF_BP_NOT_FOUND)
                    dbgcBpDelete(pDbgc, iBp);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'bd' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkDisable(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Enumerate the arguments.
     */
    int rc = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iBp = (uint32_t)paArgs[iArg].u.u64Number;
            if (iBp == paArgs[iArg].u.u64Number)
            {
                rc = DBGFR3BpDisable(pUVM, iBp);
                if (RT_FAILURE(rc))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3BpDisable failed for breakpoint %#x", iBp);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Breakpoint id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
            for (PDBGCBP pBp = pDbgc->pFirstBp; pBp; pBp = pBp->pNext)
            {
                int rc2 = DBGFR3BpDisable(pUVM, pBp->iBp);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3BpDisable failed for breakpoint %#x", pBp->iBp);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'be' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkEnable(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Enumerate the arguments.
     */
    int rc = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iBp = (uint32_t)paArgs[iArg].u.u64Number;
            if (iBp == paArgs[iArg].u.u64Number)
            {
                rc = DBGFR3BpEnable(pUVM, iBp);
                if (RT_FAILURE(rc))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3BpEnable failed for breakpoint %#x", iBp);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Breakpoint id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
            for (PDBGCBP pBp = pDbgc->pFirstBp; pBp; pBp = pBp->pNext)
            {
                int rc2 = DBGFR3BpEnable(pUVM, pBp->iBp);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3BpEnable failed for breakpoint %#x", pBp->iBp);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * Breakpoint enumeration callback function.
 *
 * @returns VBox status code. Any failure will stop the enumeration.
 * @param   pUVM        The user mode VM handle.
 * @param   pvUser      The user argument.
 * @param   hBp         The DBGF breakpoint handle.
 * @param   pBp         Pointer to the breakpoint information. (readonly)
 */
static DECLCALLBACK(int) dbgcEnumBreakpointsCallback(PUVM pUVM, void *pvUser, DBGFBP hBp, PCDBGFBPPUB pBp)
{
    PDBGC   pDbgc   = (PDBGC)pvUser;
    PDBGCBP pDbgcBp = dbgcBpGet(pDbgc, hBp);

    /*
     * BP type and size.
     */
    DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%#4x %c ", hBp, DBGF_BP_PUB_IS_ENABLED(pBp) ? 'e' : 'd');
    bool fHasAddress = false;
    switch (DBGF_BP_PUB_GET_TYPE(pBp))
    {
        case DBGFBPTYPE_INT3:
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " p %RGv", pBp->u.Int3.GCPtr);
            fHasAddress = true;
            break;
        case DBGFBPTYPE_REG:
        {
            char chType;
            switch (pBp->u.Reg.fType)
            {
                case X86_DR7_RW_EO: chType = 'x'; break;
                case X86_DR7_RW_WO: chType = 'w'; break;
                case X86_DR7_RW_IO: chType = 'i'; break;
                case X86_DR7_RW_RW: chType = 'r'; break;
                default:            chType = '?'; break;

            }
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%d %c %RGv", pBp->u.Reg.cb, chType, pBp->u.Reg.GCPtr);
            fHasAddress = true;
            break;
        }

/** @todo realign the list when I/O and MMIO breakpoint command have been added and it's possible to test this code. */
        case DBGFBPTYPE_PORT_IO:
        case DBGFBPTYPE_MMIO:
        {
            uint32_t fAccess = DBGF_BP_PUB_GET_TYPE(pBp) == DBGFBPTYPE_PORT_IO ? pBp->u.PortIo.fAccess : pBp->u.Mmio.fAccess;
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, DBGF_BP_PUB_GET_TYPE(pBp) == DBGFBPTYPE_PORT_IO ?  " i" : " m");
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " %c%c%c%c%c%c",
                             fAccess & DBGFBPIOACCESS_READ_MASK   ? 'r' : '-',
                             fAccess & DBGFBPIOACCESS_READ_BYTE   ? '1' : '-',
                             fAccess & DBGFBPIOACCESS_READ_WORD   ? '2' : '-',
                             fAccess & DBGFBPIOACCESS_READ_DWORD  ? '4' : '-',
                             fAccess & DBGFBPIOACCESS_READ_QWORD  ? '8' : '-',
                             fAccess & DBGFBPIOACCESS_READ_OTHER  ? '+' : '-');
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " %c%c%c%c%c%c",
                             fAccess & DBGFBPIOACCESS_WRITE_MASK  ? 'w' : '-',
                             fAccess & DBGFBPIOACCESS_WRITE_BYTE  ? '1' : '-',
                             fAccess & DBGFBPIOACCESS_WRITE_WORD  ? '2' : '-',
                             fAccess & DBGFBPIOACCESS_WRITE_DWORD ? '4' : '-',
                             fAccess & DBGFBPIOACCESS_WRITE_QWORD ? '8' : '-',
                             fAccess & DBGFBPIOACCESS_WRITE_OTHER ? '+' : '-');
            if (DBGF_BP_PUB_GET_TYPE(pBp) == DBGFBPTYPE_PORT_IO)
                DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " %04x-%04x",
                                 pBp->u.PortIo.uPort, pBp->u.PortIo.uPort + pBp->u.PortIo.cPorts - 1);
            else
                DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%RGp LB %03x", pBp->u.Mmio.PhysAddr, pBp->u.Mmio.cb);
            break;
        }

        default:
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " unknown type %d!!", DBGF_BP_PUB_GET_TYPE(pBp));
            AssertFailed();
            break;

    }
    if (pBp->iHitDisable == ~(uint64_t)0)
        DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " %04RX64 (%04RX64 to ~0)  ", pBp->cHits, pBp->iHitTrigger);
    else
        DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " %04RX64 (%04RX64 to %04RX64)", pBp->cHits, pBp->iHitTrigger, pBp->iHitDisable);

    /*
     * Try resolve the address if it has one.
     */
    if (fHasAddress)
    {
        RTDBGSYMBOL Sym;
        RTINTPTR    off;
        DBGFADDRESS Addr;
        int rc = DBGFR3AsSymbolByAddr(pUVM, pDbgc->hDbgAs, DBGFR3AddrFromFlat(pDbgc->pUVM, &Addr, pBp->u.GCPtr),
                                      RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                      &off, &Sym, NULL);
        if (RT_SUCCESS(rc))
        {
            if (!off)
                DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%s", Sym.szName);
            else if (off > 0)
                DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%s+%RGv", Sym.szName, off);
            else
                DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "%s-%RGv", Sym.szName, -off);
        }
    }

    /*
     * The commands.
     */
    if (pDbgcBp)
    {
        if (pDbgcBp->cchCmd)
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "\n  cmds: '%s'\n", pDbgcBp->szCmd);
        else
            DBGCCmdHlpPrintf(&pDbgc->CmdHlp, "\n");
    }
    else
        DBGCCmdHlpPrintf(&pDbgc->CmdHlp, " [unknown bp]\n");

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'bl' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkList(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, -1, cArgs == 0);
    NOREF(paArgs);

    /*
     * Enumerate the breakpoints.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int rc = DBGFR3BpEnum(pUVM, dbgcEnumBreakpointsCallback, pDbgc);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3BpEnum");
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'bp' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkSet(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Convert the pointer to a DBGF address.
     */
    DBGFADDRESS Address;
    int rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &paArgs[0], &Address);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr(,'%DV',)", &paArgs[0]);

    /*
     * Pick out the optional arguments.
     */
    uint64_t iHitTrigger = 0;
    uint64_t iHitDisable = UINT64_MAX;
    const char *pszCmds = NULL;
    unsigned iArg = 1;
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
    {
        iHitTrigger = paArgs[iArg].u.u64Number;
        iArg++;
        if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
        {
            iHitDisable = paArgs[iArg].u.u64Number;
            iArg++;
        }
    }
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_STRING)
    {
        pszCmds = paArgs[iArg].u.pszString;
        iArg++;
    }

    /*
     * Try set the breakpoint.
     */
    uint32_t iBp;
    PDBGC    pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    rc = DBGFR3BpSetInt3(pUVM, pDbgc->idCpu, &Address, iHitTrigger, iHitDisable, &iBp);
    if (RT_SUCCESS(rc))
    {
        rc = dbgcBpAdd(pDbgc, iBp, pszCmds);
        if (RT_SUCCESS(rc))
            return DBGCCmdHlpPrintf(pCmdHlp, "Set breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        if (rc == VERR_DBGC_BP_EXISTS)
        {
            rc = dbgcBpUpdate(pDbgc, iBp, pszCmds);
            if (RT_SUCCESS(rc))
                return DBGCCmdHlpPrintf(pCmdHlp, "Updated breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        }
        int rc2 = DBGFR3BpClear(pDbgc->pUVM, iBp);
        AssertRC(rc2);
    }
    return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to set breakpoint at %RGv", Address.FlatPtr);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'br' command.}
 */
static DECLCALLBACK(int) dbgcCmdBrkREM(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Convert the pointer to a DBGF address.
     */
    DBGFADDRESS Address;
    int rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &paArgs[0], &Address);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr(,'%DV',)", &paArgs[0]);

    /*
     * Pick out the optional arguments.
     */
    uint64_t iHitTrigger = 0;
    uint64_t iHitDisable = UINT64_MAX;
    const char *pszCmds = NULL;
    unsigned iArg = 1;
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
    {
        iHitTrigger = paArgs[iArg].u.u64Number;
        iArg++;
        if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER)
        {
            iHitDisable = paArgs[iArg].u.u64Number;
            iArg++;
        }
    }
    if (iArg < cArgs && paArgs[iArg].enmType == DBGCVAR_TYPE_STRING)
    {
        pszCmds = paArgs[iArg].u.pszString;
        iArg++;
    }

    /*
     * Try set the breakpoint.
     */
    uint32_t iBp;
    rc = DBGFR3BpSetREM(pUVM, &Address, iHitTrigger, iHitDisable, &iBp);
    if (RT_SUCCESS(rc))
    {
        PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
        rc = dbgcBpAdd(pDbgc, iBp, pszCmds);
        if (RT_SUCCESS(rc))
            return DBGCCmdHlpPrintf(pCmdHlp, "Set REM breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        if (rc == VERR_DBGC_BP_EXISTS)
        {
            rc = dbgcBpUpdate(pDbgc, iBp, pszCmds);
            if (RT_SUCCESS(rc))
                return DBGCCmdHlpPrintf(pCmdHlp, "Updated REM breakpoint %u at %RGv\n", iBp, Address.FlatPtr);
        }
        int rc2 = DBGFR3BpClear(pDbgc->pUVM, iBp);
        AssertRC(rc2);
    }
    return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to set REM breakpoint at %RGv", Address.FlatPtr);
}


/**
 * Helps the unassmble ('u') command display symbols it starts at and passes.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   pCmdHlp         The command helpers for printing via.
 * @param   hDbgAs          The address space to look up addresses in.
 * @param   pAddress        The current address.
 * @param   pcbCallAgain    Where to return the distance to the next check (in
 *                          instruction bytes).
 */
static void dbgcCmdUnassambleHelpListNear(PUVM pUVM, PDBGCCMDHLP pCmdHlp, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress,
                                         PRTUINTPTR pcbCallAgain)
{
    RTDBGSYMBOL Symbol;
    RTGCINTPTR  offDispSym;
    int rc = DBGFR3AsSymbolByAddr(pUVM, hDbgAs, pAddress,
                                  RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                  &offDispSym, &Symbol, NULL);
    if (RT_FAILURE(rc) || offDispSym > _1G)
        rc = DBGFR3AsSymbolByAddr(pUVM, hDbgAs, pAddress,
                                  RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                  &offDispSym, &Symbol, NULL);
    if (RT_SUCCESS(rc) && offDispSym < _1G)
    {
        if (!offDispSym)
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%s:\n", Symbol.szName);
            *pcbCallAgain = !Symbol.cb ? 64 : Symbol.cb;
        }
        else if (offDispSym > 0)
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%s+%#llx:\n", Symbol.szName, (uint64_t)offDispSym);
            *pcbCallAgain = !Symbol.cb ? 64 : Symbol.cb > (RTGCUINTPTR)offDispSym ? Symbol.cb - (RTGCUINTPTR)offDispSym : 1;
        }
        else
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%s-%#llx:\n", Symbol.szName, (uint64_t)-offDispSym);
            *pcbCallAgain = !Symbol.cb ? 64 : (RTGCUINTPTR)-offDispSym + Symbol.cb;
        }
    }
    else
        *pcbCallAgain = UINT32_MAX;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'u' command.}
 */
static DECLCALLBACK(int) dbgcCmdUnassemble(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, -1, cArgs <= 1);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 0 || DBGCVAR_ISPOINTER(paArgs[0].enmType));

    if (!cArgs && !DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Don't know where to start disassembling");

    /*
     * Check the desired mode.
     */
    unsigned fFlags = DBGF_DISAS_FLAGS_NO_ADDRESS | DBGF_DISAS_FLAGS_UNPATCHED_BYTES | DBGF_DISAS_FLAGS_ANNOTATE_PATCHED;
    switch (pCmd->pszCmd[1])
    {
        default: AssertFailed(); RT_FALL_THRU();
        case '\0':  fFlags |= DBGF_DISAS_FLAGS_DEFAULT_MODE;    break;
        case '6':   fFlags |= DBGF_DISAS_FLAGS_64BIT_MODE;      break;
        case '3':   fFlags |= DBGF_DISAS_FLAGS_32BIT_MODE;      break;
        case '1':   fFlags |= DBGF_DISAS_FLAGS_16BIT_MODE;      break;
        case 'v':   fFlags |= DBGF_DISAS_FLAGS_16BIT_REAL_MODE; break;
    }

    /** @todo should use DBGFADDRESS for everything */

    /*
     * Find address.
     */
    if (!cArgs)
    {
        if (!DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            /** @todo Batch query CS, RIP, CPU mode and flags. */
            PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, pDbgc->idCpu);
            if (CPUMIsGuestIn64BitCode(pVCpu))
            {
                pDbgc->DisasmPos.enmType    = DBGCVAR_TYPE_GC_FLAT;
                pDbgc->SourcePos.u.GCFlat   = CPUMGetGuestRIP(pVCpu);
            }
            else
            {
                pDbgc->DisasmPos.enmType     = DBGCVAR_TYPE_GC_FAR;
                pDbgc->SourcePos.u.GCFar.off = CPUMGetGuestEIP(pVCpu);
                pDbgc->SourcePos.u.GCFar.sel = CPUMGetGuestCS(pVCpu);
                if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE
                    && (CPUMGetGuestEFlags(pVCpu) & X86_EFL_VM))
                {
                    fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
                    fFlags |= DBGF_DISAS_FLAGS_16BIT_REAL_MODE;
                }
            }
#endif

            fFlags |= DBGF_DISAS_FLAGS_CURRENT_GUEST;
        }
        else if ((fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE && pDbgc->fDisasm)
        {
            fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
            fFlags |= pDbgc->fDisasm & DBGF_DISAS_FLAGS_MODE_MASK;
        }
        pDbgc->DisasmPos.enmRangeType = DBGCVAR_RANGE_NONE;
    }
    else
        pDbgc->DisasmPos = paArgs[0];
    pDbgc->pLastPos = &pDbgc->DisasmPos;

    /*
     * Range.
     */
    switch (pDbgc->DisasmPos.enmRangeType)
    {
        case DBGCVAR_RANGE_NONE:
            pDbgc->DisasmPos.enmRangeType = DBGCVAR_RANGE_ELEMENTS;
            pDbgc->DisasmPos.u64Range     = 10;
            break;

        case DBGCVAR_RANGE_ELEMENTS:
            if (pDbgc->DisasmPos.u64Range > 2048)
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "Too many lines requested. Max is 2048 lines");
            break;

        case DBGCVAR_RANGE_BYTES:
            if (pDbgc->DisasmPos.u64Range > 65536)
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "The requested range is too big. Max is 64KB");
            break;

        default:
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Unknown range type %d", pDbgc->DisasmPos.enmRangeType);
    }

    /*
     * Convert physical and host addresses to guest addresses.
     */
    RTDBGAS hDbgAs = pDbgc->hDbgAs;
    int rc;
    switch (pDbgc->DisasmPos.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
        case DBGCVAR_TYPE_GC_FAR:
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            hDbgAs = DBGF_AS_PHYS;
            RT_FALL_THRU();
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        {
            DBGCVAR VarTmp;
            rc = DBGCCmdHlpEval(pCmdHlp, &VarTmp, "%%(%Dv)", &pDbgc->DisasmPos);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "failed to evaluate '%%(%Dv)'", &pDbgc->DisasmPos);
            pDbgc->DisasmPos = VarTmp;
            break;
        }
        default: AssertFailed(); break;
    }

    DBGFADDRESS CurAddr;
    if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_16BIT_REAL_MODE
        && pDbgc->DisasmPos.enmType == DBGCVAR_TYPE_GC_FAR)
        DBGFR3AddrFromFlat(pUVM, &CurAddr, ((uint32_t)pDbgc->DisasmPos.u.GCFar.sel << 4) + pDbgc->DisasmPos.u.GCFar.off);
    else
    {
        rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &pDbgc->DisasmPos, &CurAddr);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr failed on '%Dv'", &pDbgc->DisasmPos);
    }

    pDbgc->fDisasm = fFlags;

    /*
     * Figure out where we are and display it.  Also calculate when we need to
     * check for a new symbol if possible.
     */
    RTGCUINTPTR cbCheckSymbol;
    dbgcCmdUnassambleHelpListNear(pUVM, pCmdHlp, hDbgAs, &CurAddr, &cbCheckSymbol);

    /*
     * Do the disassembling.
     */
    unsigned    cTries = 32;
    int         iRangeLeft = (int)pDbgc->DisasmPos.u64Range;
    if (iRangeLeft == 0)                /* kludge for 'r'. */
        iRangeLeft = -1;
    for (;;)
    {
        /*
         * Disassemble the instruction.
         */
        char        szDis[256];
        uint32_t    cbInstr = 1;
        if (pDbgc->DisasmPos.enmType == DBGCVAR_TYPE_GC_FLAT)
            rc = DBGFR3DisasInstrEx(pUVM, pDbgc->idCpu, DBGF_SEL_FLAT, pDbgc->DisasmPos.u.GCFlat, fFlags,
                                    &szDis[0], sizeof(szDis), &cbInstr);
        else
            rc = DBGFR3DisasInstrEx(pUVM, pDbgc->idCpu, pDbgc->DisasmPos.u.GCFar.sel, pDbgc->DisasmPos.u.GCFar.off, fFlags,
                                    &szDis[0], sizeof(szDis), &cbInstr);
        if (RT_SUCCESS(rc))
        {
            /* print it */
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%-16DV %s\n", &pDbgc->DisasmPos, &szDis[0]);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            /* bitch. */
            int rc2 = DBGCCmdHlpPrintf(pCmdHlp, "Failed to disassemble instruction, skipping one byte.\n");
            if (RT_FAILURE(rc2))
                return rc2;
            if (cTries-- > 0)
                return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Too many disassembly failures. Giving up");
            cbInstr = 1;
        }

        /* advance */
        if (iRangeLeft < 0)             /* 'r' */
            break;
        if (pDbgc->DisasmPos.enmRangeType == DBGCVAR_RANGE_ELEMENTS)
            iRangeLeft--;
        else
            iRangeLeft -= cbInstr;
        rc = DBGCCmdHlpEval(pCmdHlp, &pDbgc->DisasmPos, "(%Dv) + %x", &pDbgc->DisasmPos, cbInstr);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpEval(,,'(%Dv) + %x')", &pDbgc->DisasmPos, cbInstr);
        if (iRangeLeft <= 0)
            break;
        fFlags &= ~DBGF_DISAS_FLAGS_CURRENT_GUEST;

        /* Print next symbol? */
        if (cbCheckSymbol <= cbInstr)
        {
            if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_16BIT_REAL_MODE
                && pDbgc->DisasmPos.enmType == DBGCVAR_TYPE_GC_FAR)
                DBGFR3AddrFromFlat(pUVM, &CurAddr, ((uint32_t)pDbgc->DisasmPos.u.GCFar.sel << 4) + pDbgc->DisasmPos.u.GCFar.off);
            else
                rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &pDbgc->DisasmPos, &CurAddr);
            if (RT_SUCCESS(rc))
                dbgcCmdUnassambleHelpListNear(pUVM, pCmdHlp, hDbgAs, &CurAddr, &cbCheckSymbol);
            else
                cbCheckSymbol = UINT32_MAX;
        }
        else
            cbCheckSymbol -= cbInstr;
    }

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDGCSCREENBLIT}
 */
static DECLCALLBACK(int) dbgcCmdUnassembleCfgBlit(const char *psz, void *pvUser)
{
    PDBGCCMDHLP pCmdHlp = (PDBGCCMDHLP)pvUser;
    return DBGCCmdHlpPrintf(pCmdHlp, "%s", psz);
}


/**
 * Checks whether both addresses are equal.
 *
 * @returns true if both addresses point to the same location, false otherwise.
 * @param   pAddr1              First address.
 * @param   pAddr2              Second address.
 */
static bool dbgcCmdUnassembleCfgAddrEqual(PDBGFADDRESS pAddr1, PDBGFADDRESS pAddr2)
{
    return    pAddr1->Sel == pAddr2->Sel
           && pAddr1->off == pAddr2->off;
}


/**
 * Checks whether the first given address is lower than the second one.
 *
 * @returns true if both addresses point to the same location, false otherwise.
 * @param   pAddr1              First address.
 * @param   pAddr2              Second address.
 */
static bool dbgcCmdUnassembleCfgAddrLower(PDBGFADDRESS pAddr1, PDBGFADDRESS pAddr2)
{
    return    pAddr1->Sel == pAddr2->Sel
           && pAddr1->off < pAddr2->off;
}


/**
 * Calculates the size required for the given basic block including the
 * border and spacing on the edges.
 *
 * @param   hFlowBb              The basic block handle.
 * @param   pDumpBb             The dumper state to fill in for the basic block.
 */
static void dbgcCmdUnassembleCfgDumpCalcBbSize(DBGFFLOWBB hFlowBb, PDBGCFLOWBBDUMP pDumpBb)
{
    uint32_t fFlags = DBGFR3FlowBbGetFlags(hFlowBb);
    uint32_t cInstr = DBGFR3FlowBbGetInstrCount(hFlowBb);

    pDumpBb->hFlowBb   = hFlowBb;
    pDumpBb->cchHeight = cInstr + 4; /* Include spacing and border top and bottom. */
    pDumpBb->cchWidth  = 0;
    DBGFR3FlowBbGetStartAddress(hFlowBb, &pDumpBb->AddrStart);

    DBGFFLOWBBENDTYPE enmType = DBGFR3FlowBbGetType(hFlowBb);
    if (   enmType == DBGFFLOWBBENDTYPE_COND
        || enmType == DBGFFLOWBBENDTYPE_UNCOND_JMP
        || enmType == DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP)
        DBGFR3FlowBbGetBranchAddress(hFlowBb, &pDumpBb->AddrTarget);

    if (fFlags & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
    {
        const char *pszErr = NULL;
        DBGFR3FlowBbQueryError(hFlowBb, &pszErr);
        if (pszErr)
        {
            uint32_t cchErr = (uint32_t)strlen(pszErr);

            pDumpBb->cchHeight++;
            pDumpBb->cchWidth = RT_MAX(pDumpBb->cchWidth, cchErr);
        }
    }
    for (unsigned i = 0; i < cInstr; i++)
    {
        const char *pszInstr = NULL;
        int rc = DBGFR3FlowBbQueryInstr(hFlowBb, i, NULL, NULL, &pszInstr);
        AssertRC(rc);

        uint32_t cchInstr = (uint32_t)strlen(pszInstr);
        pDumpBb->cchWidth = RT_MAX(pDumpBb->cchWidth, cchInstr);
    }
    pDumpBb->cchWidth += 4; /* Include spacing and border left and right. */
}


/**
 * Dumps a top or bottom boundary line.
 *
 * @param   hScreen             The screen to draw to.
 * @param   uStartX             Where to start drawing the boundary.
 * @param   uStartY             Y coordinate.
 * @param   cchWidth            Width of the boundary.
 * @param   enmColor            The color to use for drawing.
 */
static void dbgcCmdUnassembleCfgDumpBbBoundary(DBGCSCREEN hScreen, uint32_t uStartX, uint32_t uStartY, uint32_t cchWidth,
                                               DBGCSCREENCOLOR enmColor)
{
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX, uStartY, '+', enmColor);
    dbgcScreenAsciiDrawLineHorizontal(hScreen, uStartX + 1, uStartX + 1 + cchWidth - 2,
                                      uStartY, '-', enmColor);
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX + cchWidth - 1, uStartY, '+', enmColor);
}


/**
 * Dumps a spacing line between the top or bottom boundary and the actual disassembly.
 *
 * @param   hScreen             The screen to draw to.
 * @param   uStartX             Where to start drawing the spacing.
 * @param   uStartY             Y coordinate.
 * @param   cchWidth            Width of the spacing.
 * @param   enmColor            The color to use for drawing.
 */
static void dbgcCmdUnassembleCfgDumpBbSpacing(DBGCSCREEN hScreen, uint32_t uStartX, uint32_t uStartY, uint32_t cchWidth,
                                              DBGCSCREENCOLOR enmColor)
{
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX, uStartY, '|', enmColor);
    dbgcScreenAsciiDrawLineHorizontal(hScreen, uStartX + 1, uStartX + 1 + cchWidth - 2,
                                      uStartY, ' ', enmColor);
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX + cchWidth - 1, uStartY, '|', enmColor);
}


/**
 * Writes a given text to the screen.
 *
 * @param   hScreen             The screen to draw to.
 * @param   uStartX             Where to start drawing the line.
 * @param   uStartY             Y coordinate.
 * @param   cchWidth            Maximum width of the text.
 * @param   pszText             The text to write.
 * @param   enmTextColor        The color to use for drawing the text.
 * @param   enmBorderColor      The color to use for drawing the border.
 */
static void dbgcCmdUnassembleCfgDumpBbText(DBGCSCREEN hScreen, uint32_t uStartX, uint32_t uStartY,
                                           uint32_t cchWidth, const char *pszText,
                                           DBGCSCREENCOLOR enmTextColor, DBGCSCREENCOLOR enmBorderColor)
{
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX, uStartY, '|', enmBorderColor);
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX + 1, uStartY, ' ', enmTextColor);
    dbgcScreenAsciiDrawString(hScreen, uStartX + 2, uStartY, pszText, enmTextColor);
    dbgcScreenAsciiDrawCharacter(hScreen, uStartX + cchWidth - 1, uStartY, '|', enmBorderColor);
}


/**
 * Dumps one basic block using the dumper callback.
 *
 * @param   pDumpBb             The basic block dump state to dump.
 * @param   hScreen             The screen to draw to.
 */
static void dbgcCmdUnassembleCfgDumpBb(PDBGCFLOWBBDUMP pDumpBb, DBGCSCREEN hScreen)
{
    uint32_t uStartY = pDumpBb->uStartY;
    bool fError = RT_BOOL(DBGFR3FlowBbGetFlags(pDumpBb->hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR);
    DBGCSCREENCOLOR enmColor = fError ? DBGCSCREENCOLOR_RED_BRIGHT : DBGCSCREENCOLOR_DEFAULT;

    dbgcCmdUnassembleCfgDumpBbBoundary(hScreen, pDumpBb->uStartX, uStartY, pDumpBb->cchWidth, enmColor);
    uStartY++;
    dbgcCmdUnassembleCfgDumpBbSpacing(hScreen, pDumpBb->uStartX, uStartY, pDumpBb->cchWidth, enmColor);
    uStartY++;

    uint32_t cInstr = DBGFR3FlowBbGetInstrCount(pDumpBb->hFlowBb);
    for (unsigned i = 0; i < cInstr; i++)
    {
        const char *pszInstr = NULL;
        DBGFR3FlowBbQueryInstr(pDumpBb->hFlowBb, i, NULL, NULL, &pszInstr);
        dbgcCmdUnassembleCfgDumpBbText(hScreen, pDumpBb->uStartX, uStartY + i,
                                       pDumpBb->cchWidth, pszInstr, DBGCSCREENCOLOR_DEFAULT,
                                       enmColor);
    }
    uStartY += cInstr;

    if (fError)
    {
        const char *pszErr = NULL;
        DBGFR3FlowBbQueryError(pDumpBb->hFlowBb, &pszErr);
        if (pszErr)
            dbgcCmdUnassembleCfgDumpBbText(hScreen, pDumpBb->uStartX, uStartY,
                                           pDumpBb->cchWidth, pszErr, enmColor,
                                           enmColor);
        uStartY++;
    }

    dbgcCmdUnassembleCfgDumpBbSpacing(hScreen, pDumpBb->uStartX, uStartY, pDumpBb->cchWidth, enmColor);
    uStartY++;
    dbgcCmdUnassembleCfgDumpBbBoundary(hScreen, pDumpBb->uStartX, uStartY, pDumpBb->cchWidth, enmColor);
    uStartY++;
}


/**
 * Dumps one branch table using the dumper callback.
 *
 * @param   pDumpBranchTbl      The basic block dump state to dump.
 * @param   hScreen             The screen to draw to.
 */
static void dbgcCmdUnassembleCfgDumpBranchTbl(PDBGCFLOWBRANCHTBLDUMP pDumpBranchTbl, DBGCSCREEN hScreen)
{
    uint32_t uStartY = pDumpBranchTbl->uStartY;
    DBGCSCREENCOLOR enmColor = DBGCSCREENCOLOR_CYAN_BRIGHT;

    dbgcCmdUnassembleCfgDumpBbBoundary(hScreen, pDumpBranchTbl->uStartX, uStartY, pDumpBranchTbl->cchWidth, enmColor);
    uStartY++;
    dbgcCmdUnassembleCfgDumpBbSpacing(hScreen, pDumpBranchTbl->uStartX, uStartY, pDumpBranchTbl->cchWidth, enmColor);
    uStartY++;

    uint32_t cSlots = DBGFR3FlowBranchTblGetSlots(pDumpBranchTbl->hFlowBranchTbl);
    for (unsigned i = 0; i < cSlots; i++)
    {
        DBGFADDRESS Addr;
        char szAddr[128];

        RT_ZERO(szAddr);
        DBGFR3FlowBranchTblGetAddrAtSlot(pDumpBranchTbl->hFlowBranchTbl, i, &Addr);

        if (Addr.Sel == DBGF_SEL_FLAT)
            RTStrPrintf(&szAddr[0], sizeof(szAddr), "%RGv", Addr.FlatPtr);
        else
            RTStrPrintf(&szAddr[0], sizeof(szAddr), "%04x:%RGv", Addr.Sel, Addr.off);

        dbgcCmdUnassembleCfgDumpBbText(hScreen, pDumpBranchTbl->uStartX, uStartY + i,
                                       pDumpBranchTbl->cchWidth, &szAddr[0], DBGCSCREENCOLOR_DEFAULT,
                                       enmColor);
    }
    uStartY += cSlots;

    dbgcCmdUnassembleCfgDumpBbSpacing(hScreen, pDumpBranchTbl->uStartX, uStartY, pDumpBranchTbl->cchWidth, enmColor);
    uStartY++;
    dbgcCmdUnassembleCfgDumpBbBoundary(hScreen, pDumpBranchTbl->uStartX, uStartY, pDumpBranchTbl->cchWidth, enmColor);
    uStartY++;
}


/**
 * Fills in the dump states for the basic blocks and branch tables.
 *
 * @param   hFlowIt             The control flow graph iterator handle.
 * @param   hFlowBranchTblIt    The control flow graph branch table iterator handle.
 * @param   paDumpBb            The array of basic block dump states.
 * @param   paDumpBranchTbl     The array of branch table dump states.
 * @param   cBbs                Number of basic blocks.
 * @param   cBranchTbls         Number of branch tables.
 */
static void dbgcCmdUnassembleCfgDumpCalcDimensions(DBGFFLOWIT hFlowIt, DBGFFLOWBRANCHTBLIT hFlowBranchTblIt,
                                                   PDBGCFLOWBBDUMP paDumpBb, PDBGCFLOWBRANCHTBLDUMP paDumpBranchTbl,
                                                   uint32_t cBbs, uint32_t cBranchTbls)
{
    RT_NOREF2(cBbs, cBranchTbls);

    /* Calculate the sizes of each basic block first. */
    DBGFFLOWBB hFlowBb = DBGFR3FlowItNext(hFlowIt);
    uint32_t idx = 0;
    while (hFlowBb)
    {
        dbgcCmdUnassembleCfgDumpCalcBbSize(hFlowBb, &paDumpBb[idx]);
        idx++;
        hFlowBb = DBGFR3FlowItNext(hFlowIt);
    }

    if (paDumpBranchTbl)
    {
        idx = 0;
        DBGFFLOWBRANCHTBL hFlowBranchTbl = DBGFR3FlowBranchTblItNext(hFlowBranchTblIt);
        while (hFlowBranchTbl)
        {
            paDumpBranchTbl[idx].hFlowBranchTbl = hFlowBranchTbl;
            paDumpBranchTbl[idx].cchHeight      = DBGFR3FlowBranchTblGetSlots(hFlowBranchTbl) + 4; /* Spacing and border. */
            paDumpBranchTbl[idx].cchWidth       = 25 + 4; /* Spacing and border. */
            idx++;
            hFlowBranchTbl = DBGFR3FlowBranchTblItNext(hFlowBranchTblIt);
        }
    }
}

/**
 * Dumps the given control flow graph to the output.
 *
 * @returns VBox status code.
 * @param   hCfg                The control flow graph handle.
 * @param   fUseColor           Flag whether the output should be colorized.
 * @param   pCmdHlp             The command helper callback table.
 */
static int dbgcCmdUnassembleCfgDump(DBGFFLOW hCfg, bool fUseColor, PDBGCCMDHLP pCmdHlp)
{
    int rc = VINF_SUCCESS;
    DBGFFLOWIT hCfgIt = NULL;
    DBGFFLOWBRANCHTBLIT hFlowBranchTblIt = NULL;
    uint32_t cBbs = DBGFR3FlowGetBbCount(hCfg);
    uint32_t cBranchTbls = DBGFR3FlowGetBranchTblCount(hCfg);
    PDBGCFLOWBBDUMP paDumpBb = (PDBGCFLOWBBDUMP)RTMemTmpAllocZ(cBbs * sizeof(DBGCFLOWBBDUMP));
    PDBGCFLOWBRANCHTBLDUMP paDumpBranchTbl = NULL;

    if (cBranchTbls)
        paDumpBranchTbl = (PDBGCFLOWBRANCHTBLDUMP)RTMemAllocZ(cBranchTbls * sizeof(DBGCFLOWBRANCHTBLDUMP));

    if (RT_UNLIKELY(!paDumpBb || (!paDumpBranchTbl && cBranchTbls > 0)))
        rc = VERR_NO_MEMORY;
    if (RT_SUCCESS(rc))
        rc = DBGFR3FlowItCreate(hCfg, DBGFFLOWITORDER_BY_ADDR_LOWEST_FIRST, &hCfgIt);
    if (RT_SUCCESS(rc) && cBranchTbls > 0)
        rc = DBGFR3FlowBranchTblItCreate(hCfg, DBGFFLOWITORDER_BY_ADDR_LOWEST_FIRST, &hFlowBranchTblIt);

    if (RT_SUCCESS(rc))
    {
        dbgcCmdUnassembleCfgDumpCalcDimensions(hCfgIt, hFlowBranchTblIt, paDumpBb, paDumpBranchTbl,
                                               cBbs, cBranchTbls);

        /* Calculate the ASCII screen dimensions and create one. */
        uint32_t cchWidth = 0;
        uint32_t cchLeftExtra = 5;
        uint32_t cchRightExtra = 5;
        uint32_t cchHeight = 0;
        for (unsigned i = 0; i < cBbs; i++)
        {
            PDBGCFLOWBBDUMP pDumpBb = &paDumpBb[i];
            cchWidth = RT_MAX(cchWidth, pDumpBb->cchWidth);
            cchHeight += pDumpBb->cchHeight;

            /* Incomplete blocks don't have a successor. */
            if (DBGFR3FlowBbGetFlags(pDumpBb->hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
                continue;

            switch (DBGFR3FlowBbGetType(pDumpBb->hFlowBb))
            {
                case DBGFFLOWBBENDTYPE_EXIT:
                case DBGFFLOWBBENDTYPE_LAST_DISASSEMBLED:
                    break;
                case DBGFFLOWBBENDTYPE_UNCOND_JMP:
                    if (   dbgcCmdUnassembleCfgAddrLower(&pDumpBb->AddrTarget, &pDumpBb->AddrStart)
                        || dbgcCmdUnassembleCfgAddrEqual(&pDumpBb->AddrTarget, &pDumpBb->AddrStart))
                        cchLeftExtra++;
                    else
                        cchRightExtra++;
                    break;
                case DBGFFLOWBBENDTYPE_UNCOND:
                    cchHeight += 2; /* For the arrow down to the next basic block. */
                    break;
                case DBGFFLOWBBENDTYPE_COND:
                    cchHeight += 2; /* For the arrow down to the next basic block. */
                    if (   dbgcCmdUnassembleCfgAddrLower(&pDumpBb->AddrTarget, &pDumpBb->AddrStart)
                        || dbgcCmdUnassembleCfgAddrEqual(&pDumpBb->AddrTarget, &pDumpBb->AddrStart))
                        cchLeftExtra++;
                    else
                        cchRightExtra++;
                    break;
                case DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP:
                default:
                    AssertFailed();
            }
        }

        for (unsigned i = 0; i < cBranchTbls; i++)
        {
            PDBGCFLOWBRANCHTBLDUMP pDumpBranchTbl = &paDumpBranchTbl[i];
            cchWidth = RT_MAX(cchWidth, pDumpBranchTbl->cchWidth);
            cchHeight += pDumpBranchTbl->cchHeight;
        }

        cchWidth += 2;

        DBGCSCREEN hScreen = NULL;
        rc = dbgcScreenAsciiCreate(&hScreen, cchWidth + cchLeftExtra + cchRightExtra, cchHeight);
        if (RT_SUCCESS(rc))
        {
            uint32_t uY = 0;

            /* Dump the branch tables first. */
            for (unsigned i = 0; i < cBranchTbls; i++)
            {
                paDumpBranchTbl[i].uStartX = cchLeftExtra + (cchWidth - paDumpBranchTbl[i].cchWidth) / 2;
                paDumpBranchTbl[i].uStartY = uY;
                dbgcCmdUnassembleCfgDumpBranchTbl(&paDumpBranchTbl[i], hScreen);
                uY += paDumpBranchTbl[i].cchHeight;
            }

            /* Dump the basic blocks and connections to the immediate successor. */
            for (unsigned i = 0; i < cBbs; i++)
            {
                paDumpBb[i].uStartX = cchLeftExtra + (cchWidth - paDumpBb[i].cchWidth) / 2;
                paDumpBb[i].uStartY = uY;
                dbgcCmdUnassembleCfgDumpBb(&paDumpBb[i], hScreen);
                uY += paDumpBb[i].cchHeight;

                /* Incomplete blocks don't have a successor. */
                if (DBGFR3FlowBbGetFlags(paDumpBb[i].hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
                    continue;

                switch (DBGFR3FlowBbGetType(paDumpBb[i].hFlowBb))
                {
                    case DBGFFLOWBBENDTYPE_EXIT:
                    case DBGFFLOWBBENDTYPE_LAST_DISASSEMBLED:
                    case DBGFFLOWBBENDTYPE_UNCOND_JMP:
                    case DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP:
                        break;
                    case DBGFFLOWBBENDTYPE_UNCOND:
                        /* Draw the arrow down to the next block. */
                        dbgcScreenAsciiDrawCharacter(hScreen, cchLeftExtra + cchWidth / 2, uY,
                                                     '|', DBGCSCREENCOLOR_BLUE_BRIGHT);
                        uY++;
                        dbgcScreenAsciiDrawCharacter(hScreen, cchLeftExtra + cchWidth / 2, uY,
                                                     'V', DBGCSCREENCOLOR_BLUE_BRIGHT);
                        uY++;
                        break;
                    case DBGFFLOWBBENDTYPE_COND:
                        /* Draw the arrow down to the next block. */
                        dbgcScreenAsciiDrawCharacter(hScreen, cchLeftExtra + cchWidth / 2, uY,
                                                     '|', DBGCSCREENCOLOR_RED_BRIGHT);
                        uY++;
                        dbgcScreenAsciiDrawCharacter(hScreen, cchLeftExtra + cchWidth / 2, uY,
                                                     'V', DBGCSCREENCOLOR_RED_BRIGHT);
                        uY++;
                        break;
                    default:
                        AssertFailed();
                }
            }

            /* Last pass, connect all remaining branches. */
            uint32_t uBackConns = 0;
            uint32_t uFwdConns = 0;
            for (unsigned i = 0; i < cBbs; i++)
            {
                PDBGCFLOWBBDUMP pDumpBb = &paDumpBb[i];
                DBGFFLOWBBENDTYPE enmEndType = DBGFR3FlowBbGetType(pDumpBb->hFlowBb);

                /* Incomplete blocks don't have a successor. */
                if (DBGFR3FlowBbGetFlags(pDumpBb->hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
                    continue;

                switch (enmEndType)
                {
                    case DBGFFLOWBBENDTYPE_EXIT:
                    case DBGFFLOWBBENDTYPE_LAST_DISASSEMBLED:
                    case DBGFFLOWBBENDTYPE_UNCOND:
                        break;
                    case DBGFFLOWBBENDTYPE_COND:
                    case DBGFFLOWBBENDTYPE_UNCOND_JMP:
                    {
                        /* Find the target first to get the coordinates. */
                        PDBGCFLOWBBDUMP pDumpBbTgt = NULL;
                        for (unsigned idxDumpBb = 0; idxDumpBb < cBbs; idxDumpBb++)
                        {
                            pDumpBbTgt = &paDumpBb[idxDumpBb];
                            if (dbgcCmdUnassembleCfgAddrEqual(&pDumpBb->AddrTarget, &pDumpBbTgt->AddrStart))
                                break;
                        }

                        DBGCSCREENCOLOR enmColor =   enmEndType == DBGFFLOWBBENDTYPE_UNCOND_JMP
                                                   ? DBGCSCREENCOLOR_YELLOW_BRIGHT
                                                   : DBGCSCREENCOLOR_GREEN_BRIGHT;

                        /*
                         * Use the right side for targets with higher addresses,
                         * left when jumping backwards.
                         */
                        if (   dbgcCmdUnassembleCfgAddrLower(&pDumpBb->AddrTarget, &pDumpBb->AddrStart)
                            || dbgcCmdUnassembleCfgAddrEqual(&pDumpBb->AddrTarget, &pDumpBb->AddrStart))
                        {
                            /* Going backwards. */
                            uint32_t uXVerLine = /*cchLeftExtra - 1 -*/ uBackConns + 1;
                            uint32_t uYHorLine = pDumpBb->uStartY + pDumpBb->cchHeight - 1 - 2;
                            uBackConns++;

                            /* Draw the arrow pointing to the target block. */
                            dbgcScreenAsciiDrawCharacter(hScreen, pDumpBbTgt->uStartX - 1, pDumpBbTgt->uStartY,
                                                         '>', enmColor);
                            /* Draw the horizontal line. */
                            dbgcScreenAsciiDrawLineHorizontal(hScreen, uXVerLine + 1, pDumpBbTgt->uStartX - 2,
                                                              pDumpBbTgt->uStartY, '-', enmColor);
                            dbgcScreenAsciiDrawCharacter(hScreen, uXVerLine, pDumpBbTgt->uStartY, '+',
                                                         enmColor);
                            /* Draw the vertical line down to the source block. */
                            dbgcScreenAsciiDrawLineVertical(hScreen, uXVerLine, pDumpBbTgt->uStartY + 1, uYHorLine - 1,
                                                            '|', enmColor);
                            dbgcScreenAsciiDrawCharacter(hScreen, uXVerLine, uYHorLine, '+', enmColor);
                            /* Draw the horizontal connection between the source block and vertical part. */
                            dbgcScreenAsciiDrawLineHorizontal(hScreen, uXVerLine + 1, pDumpBb->uStartX - 1,
                                                              uYHorLine, '-', enmColor);

                        }
                        else
                        {
                            /* Going forward. */
                            uint32_t uXVerLine = cchWidth + cchLeftExtra + (cchRightExtra - uFwdConns) - 1;
                            uint32_t uYHorLine = pDumpBb->uStartY + pDumpBb->cchHeight - 1 - 2;
                            uFwdConns++;

                            /* Draw the horizontal line. */
                            dbgcScreenAsciiDrawLineHorizontal(hScreen, pDumpBb->uStartX + pDumpBb->cchWidth,
                                                              uXVerLine - 1, uYHorLine, '-', enmColor);
                            dbgcScreenAsciiDrawCharacter(hScreen, uXVerLine, uYHorLine, '+', enmColor);
                            /* Draw the vertical line down to the target block. */
                            dbgcScreenAsciiDrawLineVertical(hScreen, uXVerLine, uYHorLine + 1, pDumpBbTgt->uStartY - 1,
                                                            '|', enmColor);
                            /* Draw the horizontal connection between the target block and vertical part. */
                            dbgcScreenAsciiDrawLineHorizontal(hScreen, pDumpBbTgt->uStartX + pDumpBbTgt->cchWidth,
                                                              uXVerLine, pDumpBbTgt->uStartY, '-', enmColor);
                            dbgcScreenAsciiDrawCharacter(hScreen, uXVerLine, pDumpBbTgt->uStartY, '+',
                                                         enmColor);
                            /* Draw the arrow pointing to the target block. */
                            dbgcScreenAsciiDrawCharacter(hScreen, pDumpBbTgt->uStartX + pDumpBbTgt->cchWidth,
                                                         pDumpBbTgt->uStartY, '<', enmColor);
                        }
                        break;
                    }
                    case DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP:
                    default:
                        AssertFailed();
                }
            }

            rc = dbgcScreenAsciiBlit(hScreen, dbgcCmdUnassembleCfgBlit, pCmdHlp, fUseColor);
            dbgcScreenAsciiDestroy(hScreen);
        }
    }

    if (paDumpBb)
    {
        for (unsigned i = 0; i < cBbs; i++)
            DBGFR3FlowBbRelease(paDumpBb[i].hFlowBb);
        RTMemTmpFree(paDumpBb);
    }

    if (paDumpBranchTbl)
    {
        for (unsigned i = 0; i < cBranchTbls; i++)
            DBGFR3FlowBranchTblRelease(paDumpBranchTbl[i].hFlowBranchTbl);
        RTMemTmpFree(paDumpBranchTbl);
    }

    if (hCfgIt)
        DBGFR3FlowItDestroy(hCfgIt);
    if (hFlowBranchTblIt)
        DBGFR3FlowBranchTblItDestroy(hFlowBranchTblIt);

    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'ucfg' command.}
 */
static DECLCALLBACK(int) dbgcCmdUnassembleCfg(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, -1, cArgs <= 1);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 0 || DBGCVAR_ISPOINTER(paArgs[0].enmType));

    if (!cArgs && !DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Don't know where to start disassembling");

    /*
     * Check the desired mode.
     */
    unsigned fFlags =  DBGF_DISAS_FLAGS_UNPATCHED_BYTES | DBGF_DISAS_FLAGS_ANNOTATE_PATCHED;
    bool fUseColor = false;
    switch (pCmd->pszCmd[4])
    {
        default: AssertFailed(); RT_FALL_THRU();
        case '\0':  fFlags |= DBGF_DISAS_FLAGS_DEFAULT_MODE;    break;
        case '6':   fFlags |= DBGF_DISAS_FLAGS_64BIT_MODE;      break;
        case '3':   fFlags |= DBGF_DISAS_FLAGS_32BIT_MODE;      break;
        case '1':   fFlags |= DBGF_DISAS_FLAGS_16BIT_MODE;      break;
        case 'v':   fFlags |= DBGF_DISAS_FLAGS_16BIT_REAL_MODE; break;
        case 'c':   fUseColor = true; break;
    }

    /** @todo should use DBGFADDRESS for everything */

    /*
     * Find address.
     */
    if (!cArgs)
    {
        if (!DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            /** @todo Batch query CS, RIP, CPU mode and flags. */
            PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, pDbgc->idCpu);
            if (CPUMIsGuestIn64BitCode(pVCpu))
            {
                pDbgc->DisasmPos.enmType    = DBGCVAR_TYPE_GC_FLAT;
                pDbgc->SourcePos.u.GCFlat   = CPUMGetGuestRIP(pVCpu);
            }
            else
            {
                pDbgc->DisasmPos.enmType     = DBGCVAR_TYPE_GC_FAR;
                pDbgc->SourcePos.u.GCFar.off = CPUMGetGuestEIP(pVCpu);
                pDbgc->SourcePos.u.GCFar.sel = CPUMGetGuestCS(pVCpu);
                if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE
                    && (CPUMGetGuestEFlags(pVCpu) & X86_EFL_VM))
                {
                    fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
                    fFlags |= DBGF_DISAS_FLAGS_16BIT_REAL_MODE;
                }
            }
#endif

            fFlags |= DBGF_DISAS_FLAGS_CURRENT_GUEST;
        }
        else if ((fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE && pDbgc->fDisasm)
        {
            fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
            fFlags |= pDbgc->fDisasm & DBGF_DISAS_FLAGS_MODE_MASK;
        }
        pDbgc->DisasmPos.enmRangeType = DBGCVAR_RANGE_NONE;
    }
    else
        pDbgc->DisasmPos = paArgs[0];
    pDbgc->pLastPos = &pDbgc->DisasmPos;

    /*
     * Range.
     */
    switch (pDbgc->DisasmPos.enmRangeType)
    {
        case DBGCVAR_RANGE_NONE:
            pDbgc->DisasmPos.enmRangeType = DBGCVAR_RANGE_ELEMENTS;
            pDbgc->DisasmPos.u64Range     = 10;
            break;

        case DBGCVAR_RANGE_ELEMENTS:
            if (pDbgc->DisasmPos.u64Range > 2048)
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "Too many lines requested. Max is 2048 lines");
            break;

        case DBGCVAR_RANGE_BYTES:
            if (pDbgc->DisasmPos.u64Range > 65536)
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "The requested range is too big. Max is 64KB");
            break;

        default:
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Unknown range type %d", pDbgc->DisasmPos.enmRangeType);
    }

    int rc;
#if 0 /** @todo Unused right now. */
    /*
     * Convert physical and host addresses to guest addresses.
     */
    RTDBGAS hDbgAs = pDbgc->hDbgAs;
    switch (pDbgc->DisasmPos.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
        case DBGCVAR_TYPE_GC_FAR:
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            hDbgAs = DBGF_AS_PHYS;
            RT_FALL_THRU();
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        {
            DBGCVAR VarTmp;
            rc = DBGCCmdHlpEval(pCmdHlp, &VarTmp, "%%(%Dv)", &pDbgc->DisasmPos);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "failed to evaluate '%%(%Dv)'", &pDbgc->DisasmPos);
            pDbgc->DisasmPos = VarTmp;
            break;
        }
        default: AssertFailed(); break;
    }
#endif

    DBGFADDRESS CurAddr;
    if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_16BIT_REAL_MODE
        && pDbgc->DisasmPos.enmType == DBGCVAR_TYPE_GC_FAR)
        DBGFR3AddrFromFlat(pUVM, &CurAddr, ((uint32_t)pDbgc->DisasmPos.u.GCFar.sel << 4) + pDbgc->DisasmPos.u.GCFar.off);
    else
    {
        rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &pDbgc->DisasmPos, &CurAddr);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr failed on '%Dv'", &pDbgc->DisasmPos);
    }

    DBGFFLOW hCfg;
    rc = DBGFR3FlowCreate(pUVM, pDbgc->idCpu, &CurAddr, 0 /*cbDisasmMax*/,
                          DBGF_FLOW_CREATE_F_TRY_RESOLVE_INDIRECT_BRANCHES, fFlags, &hCfg);
    if (RT_SUCCESS(rc))
    {
        /* Dump the graph. */
        rc = dbgcCmdUnassembleCfgDump(hCfg, fUseColor, pCmdHlp);
        DBGFR3FlowRelease(hCfg);
    }
    else
        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowCreate failed on '%Dv'", &pDbgc->DisasmPos);

    NOREF(pCmd);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'ls' command.}
 */
static DECLCALLBACK(int) dbgcCmdListSource(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC  pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs <= 1);
    if (cArgs == 1)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISPOINTER(paArgs[0].enmType));
    if (!pUVM && !cArgs && !DBGCVAR_ISPOINTER(pDbgc->SourcePos.enmType))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Don't know where to start listing...");
    if (!pUVM && cArgs && DBGCVAR_ISGCPOINTER(paArgs[0].enmType))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "GC address but no VM");

    /*
     * Find address.
     */
    if (!cArgs)
    {
        if (!DBGCVAR_ISPOINTER(pDbgc->SourcePos.enmType))
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, pDbgc->idCpu);
            pDbgc->SourcePos.enmType     = DBGCVAR_TYPE_GC_FAR;
            pDbgc->SourcePos.u.GCFar.off = CPUMGetGuestEIP(pVCpu);
            pDbgc->SourcePos.u.GCFar.sel = CPUMGetGuestCS(pVCpu);
#endif
        }
        pDbgc->SourcePos.enmRangeType = DBGCVAR_RANGE_NONE;
    }
    else
        pDbgc->SourcePos = paArgs[0];
    pDbgc->pLastPos = &pDbgc->SourcePos;

    /*
     * Ensure the source address is flat GC.
     */
    switch (pDbgc->SourcePos.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            break;
        case DBGCVAR_TYPE_GC_PHYS:
        case DBGCVAR_TYPE_GC_FAR:
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        {
            int rc = DBGCCmdHlpEval(pCmdHlp, &pDbgc->SourcePos, "%%(%Dv)", &pDbgc->SourcePos);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpPrintf(pCmdHlp, "error: Invalid address or address type. (rc=%d)\n", rc);
            break;
        }
        default: AssertFailed(); break;
    }

    /*
     * Range.
     */
    switch (pDbgc->SourcePos.enmRangeType)
    {
        case DBGCVAR_RANGE_NONE:
            pDbgc->SourcePos.enmRangeType = DBGCVAR_RANGE_ELEMENTS;
            pDbgc->SourcePos.u64Range     = 10;
            break;

        case DBGCVAR_RANGE_ELEMENTS:
            if (pDbgc->SourcePos.u64Range > 2048)
                return DBGCCmdHlpPrintf(pCmdHlp, "error: Too many lines requested. Max is 2048 lines.\n");
            break;

        case DBGCVAR_RANGE_BYTES:
            if (pDbgc->SourcePos.u64Range > 65536)
                return DBGCCmdHlpPrintf(pCmdHlp, "error: The requested range is too big. Max is 64KB.\n");
            break;

        default:
            return DBGCCmdHlpPrintf(pCmdHlp, "internal error: Unknown range type %d.\n", pDbgc->SourcePos.enmRangeType);
    }

    /*
     * Do the disassembling.
     */
    bool        fFirst = 1;
    RTDBGLINE   LinePrev = { 0, 0, 0, 0, 0, "" };
    int         iRangeLeft = (int)pDbgc->SourcePos.u64Range;
    if (iRangeLeft == 0)                /* kludge for 'r'. */
        iRangeLeft = -1;
    for (;;)
    {
        /*
         * Get line info.
         */
        RTDBGLINE   Line;
        RTGCINTPTR  off;
        DBGFADDRESS SourcePosAddr;
        int rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &pDbgc->SourcePos, &SourcePosAddr);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr(,%Dv)", &pDbgc->SourcePos);
        rc = DBGFR3AsLineByAddr(pUVM, pDbgc->hDbgAs, &SourcePosAddr, &off, &Line, NULL);
        if (RT_FAILURE(rc))
            return VINF_SUCCESS;

        unsigned cLines = 0;
        if (memcmp(&Line, &LinePrev, sizeof(Line)))
        {
            /*
             * Print filenamename
             */
            if (!fFirst && strcmp(Line.szFilename, LinePrev.szFilename))
                fFirst = true;
            if (fFirst)
            {
                rc = DBGCCmdHlpPrintf(pCmdHlp, "[%s @ %d]\n", Line.szFilename, Line.uLineNo);
                if (RT_FAILURE(rc))
                    return rc;
            }

            /*
             * Try open the file and read the line.
             */
            FILE *phFile = fopen(Line.szFilename, "r");
            if (phFile)
            {
                /* Skip ahead to the desired line. */
                char szLine[4096];
                unsigned cBefore = fFirst ? RT_MIN(2, Line.uLineNo - 1) : Line.uLineNo - LinePrev.uLineNo - 1;
                if (cBefore > 7)
                    cBefore = 0;
                unsigned cLeft = Line.uLineNo - cBefore;
                while (cLeft > 0)
                {
                    szLine[0] = '\0';
                    if (!fgets(szLine, sizeof(szLine), phFile))
                        break;
                    cLeft--;
                }
                if (!cLeft)
                {
                    /* print the before lines */
                    for (;;)
                    {
                        size_t cch = strlen(szLine);
                        while (cch > 0 && (szLine[cch - 1] == '\r' ||  szLine[cch - 1] == '\n' || RT_C_IS_SPACE(szLine[cch - 1])) )
                            szLine[--cch] = '\0';
                        if (cBefore-- <= 0)
                            break;

                        rc = DBGCCmdHlpPrintf(pCmdHlp, "         %4d: %s\n", Line.uLineNo - cBefore - 1, szLine);
                        if (RT_FAILURE(rc))
                            break;
                        szLine[0] = '\0';
                        const char *pszShutUpGcc = fgets(szLine, sizeof(szLine), phFile); NOREF(pszShutUpGcc);
                        cLines++;
                    }
                    /* print the actual line */
                    if (RT_SUCCESS(rc))
                        rc = DBGCCmdHlpPrintf(pCmdHlp, "%08llx %4d: %s\n", Line.Address, Line.uLineNo, szLine);
                }
                fclose(phFile);
                if (RT_FAILURE(rc))
                    return rc;
                fFirst = false;
            }
            else
                return DBGCCmdHlpPrintf(pCmdHlp, "Warning: couldn't open source file '%s'\n", Line.szFilename);

            LinePrev = Line;
        }


        /*
         * Advance
         */
        if (iRangeLeft < 0)             /* 'r' */
            break;
        if (pDbgc->SourcePos.enmRangeType == DBGCVAR_RANGE_ELEMENTS)
            iRangeLeft -= cLines;
        else
            iRangeLeft -= 1;
        rc = DBGCCmdHlpEval(pCmdHlp, &pDbgc->SourcePos, "(%Dv) + %x", &pDbgc->SourcePos, 1);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Expression: (%Dv) + %x\n", &pDbgc->SourcePos, 1);
        if (iRangeLeft <= 0)
            break;
    }

    NOREF(pCmd);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'r' command.}
 */
static DECLCALLBACK(int) dbgcCmdReg(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    return dbgcCmdRegGuest(pCmd, pCmdHlp, pUVM, paArgs, cArgs);
}


/**
 * @callback_method_impl{FNDBGCCMD, Common worker for the dbgcCmdReg*()
 *                       commands.}
 */
static DECLCALLBACK(int) dbgcCmdRegCommon(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs,
                                          const char *pszPrefix)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int   rc;
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 1 || cArgs == 2 || cArgs == 3);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0,    paArgs[0].enmType == DBGCVAR_TYPE_STRING
                                                    || paArgs[0].enmType == DBGCVAR_TYPE_SYMBOL);

    /*
     * Parse the register name and kind.
     */
    bool const  fAllRegs = strcmp(paArgs[0].u.pszString, "all") == 0;
    const char *pszReg   = paArgs[0].u.pszString;
    if (*pszReg == '@')
        pszReg++;
    VMCPUID idCpu = pDbgc->idCpu;
    if (*pszPrefix)
        idCpu |= DBGFREG_HYPER_VMCPUID;
    if (*pszReg == '.')
    {
        pszReg++;
        idCpu |= DBGFREG_HYPER_VMCPUID;
    }
    const char * const pszActualPrefix = idCpu & DBGFREG_HYPER_VMCPUID ? "." : "";
    if (cArgs == 1)
    {
        /*
         * Show the register.
         *
         * If it ends with a '.' or '.*', we'll show any subfields and aliases as
         * well.  This is a special VBox twist.
         */
        size_t cchReg    = strlen(pszReg);
        size_t cchSuffix = 0;
        if (cchReg >= 2 && pszReg[cchReg - 1] == '.')
            cchSuffix = 1;
        else if (cchReg >= 3 && pszReg[cchReg - 1] == '*' && pszReg[cchReg - 2] == '.')
            cchSuffix = 2;

        char szValue[160];
        if (!cchSuffix && !fAllRegs)
        {
            DBGFREGVALTYPE  enmType;
            DBGFREGVAL      Value;
            rc = DBGFR3RegNmQuery(pUVM, idCpu, pszReg, &Value, &enmType);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_DBGF_REGISTER_NOT_FOUND)
                    return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown register: '%s%s'.\n",
                                               pszActualPrefix,  pszReg);
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegNmQuery failed querying '%s%s': %Rrc.\n",
                                           pszActualPrefix,  pszReg, rc);
            }

            rc = DBGFR3RegFormatValue(szValue, sizeof(szValue), &Value, enmType, true /*fSpecial*/);
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%s%s=%s\n", pszActualPrefix, pszReg, szValue);
            else
                rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegFormatValue failed: %Rrc.\n", rc);
        }
        else
        {
            /*
             * Register + aliases + subfields OR 'all'.
             */
            /* Duplicate the register specifier sans suffix. */
            char            *pszRegBase = RTStrDupN(pszReg, cchReg - cchSuffix);
            AssertReturn(pszRegBase, VERR_NO_STR_MEMORY);

            /* Make a rough guess on how many entires we need, or query it in the case of 'all'. */
            size_t          cRegsAlloc = 128;
            if (fAllRegs)
                DBGFR3RegNmQueryAllCount(pUVM, &cRegsAlloc);
            PDBGFREGENTRYNM paRegs = (PDBGFREGENTRYNM)RTMemTmpAlloc(sizeof(paRegs[0]) * cRegsAlloc);
            AssertReturnStmt(paRegs, RTStrFree(pszRegBase), VERR_NO_TMP_MEMORY);
            size_t          cRegs = cRegsAlloc;

            /* Query the registers.*/
            if (fAllRegs)
                rc = DBGFR3RegNmQueryAll(pUVM, paRegs, cRegs);
            else
                rc = DBGFR3RegNmQueryEx(pUVM, idCpu, pszRegBase, DBGFR3REG_QUERY_EX_F_SUBFIELDS | DBGFR3REG_QUERY_EX_F_ALIASES,
                                        paRegs, &cRegs);
            if (rc == VERR_BUFFER_OVERFLOW && !fAllRegs)
            {
                RTMemTmpFree(paRegs);
                cRegsAlloc = cRegs;
                paRegs = (PDBGFREGENTRYNM)RTMemTmpAlloc(sizeof(paRegs[0]) * cRegsAlloc);
                AssertReturnStmt(paRegs, RTStrFree(pszRegBase), VERR_NO_TMP_MEMORY);
                rc = DBGFR3RegNmQueryEx(pUVM, idCpu, pszRegBase,
                                        DBGFR3REG_QUERY_EX_F_SUBFIELDS | DBGFR3REG_QUERY_EX_F_ALIASES, paRegs, &cRegs);
            }
            if (RT_SUCCESS(rc))
            {
                /* Find max lengths and sizes for producing pretty columns. */
                size_t cchMaxNm         = 2;
                size_t cchMaxSubFieldNm = 2;
                size_t cMaxSubFieldBits = 1;
                if (*pszActualPrefix == '\0')
                    for (uint32_t iReg = 0; iReg < cRegs; iReg++)
                    {
                        size_t const cchName = strlen(paRegs[iReg].pszName);
                        if (cchMaxNm < cchName)
                            cchMaxNm = cchName;
                        if (paRegs[iReg].u.s.fSubField)
                        {
                            cchMaxSubFieldNm = RT_MAX(cchMaxSubFieldNm, cchName);
                            cMaxSubFieldBits = RT_MAX(cMaxSubFieldBits, paRegs[iReg].u.s.cBits);
                        }
                    }

                /* Output the registers. */
                size_t   cchMaxSubFieldValue = 2 + (cMaxSubFieldBits + 3) / 4;
                size_t   cMaxSameLine        = 80 / (2 + cchMaxSubFieldNm + 1 + cchMaxSubFieldValue);
                unsigned iSameLine           = 0;
                for (uint32_t iReg = 0; iReg < cRegs; iReg++)
                {
                    if (   !paRegs[iReg].u.s.fSubField
                        || !paRegs[iReg].u.s.cBits)
                        rc = DBGFR3RegFormatValue(szValue, sizeof(szValue), &paRegs[iReg].Val,
                                                  paRegs[iReg].enmType, true /*fSpecial*/);
                    else
                        rc = DBGFR3RegFormatValueEx(szValue, sizeof(szValue), &paRegs[iReg].Val, paRegs[iReg].enmType,
                                                    16,
                                                    (paRegs[iReg].u.s.cBits + 3) / 4,
                                                    0,
                                                    (paRegs[iReg].u.s.cBits == 1 ? 0 : RTSTR_F_SPECIAL) | RTSTR_F_WIDTH);
                    if (RT_SUCCESS(rc))
                    {
                        if (!paRegs[iReg].u.s.fSubField)
                        {
                            if (iSameLine > 0)
                                rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
                            if (RT_SUCCESS(rc))
                            {
                                if (*pszActualPrefix == '\0')
                                    rc = DBGCCmdHlpPrintf(pCmdHlp, "%*s=%s\n", cchMaxNm, paRegs[iReg].pszName, szValue);
                                else
                                    rc = DBGCCmdHlpPrintf(pCmdHlp, "%s%s=%s\n", pszActualPrefix, paRegs[iReg].pszName, szValue);
                            }
                            iSameLine = 0;
                        }
                        else
                        {
                            if (*pszActualPrefix == '\0')
                                rc = DBGCCmdHlpPrintf(pCmdHlp, "  %*s=%s", cchMaxSubFieldNm, paRegs[iReg].pszName, szValue);
                            else
                                rc = DBGCCmdHlpPrintf(pCmdHlp, "  %s%s=%s", pszActualPrefix, paRegs[iReg].pszName, szValue);
                            iSameLine++;
                            if (iSameLine < cMaxSameLine)
                            {
                                size_t cchValue = strlen(szValue);
                                if (cchValue < cchMaxSubFieldValue)
                                    rc = DBGCCmdHlpPrintf(pCmdHlp, "%*s", cchMaxSubFieldValue - cchValue, "");
                            }
                            else
                            {
                                rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
                                iSameLine = 0;
                            }
                        }
                    }
                    else
                        rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegFormatValue[Ex] failed for %s: %Rrc.\n",
                                                 paRegs[iReg].pszName, rc);
                }
                if (iSameLine > 0)
                    rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
            }
            else if (fAllRegs)
                rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegNmQueryAll failed: %Rrc.\n", rc);
            else if (rc == VERR_DBGF_REGISTER_NOT_FOUND)
                rc = DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown register: '%s%s'.\n",
                                         pszActualPrefix, pszRegBase);
            else
                rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegNmQueryEx failed querying '%s%s': %Rrc.\n",
                                         pszActualPrefix, pszRegBase, rc);
            RTStrFree(pszRegBase);
            RTMemTmpFree(paRegs);
        }
    }
    else
    {
        /*
         * We're about to modify the register.
         *
         * First we need to query the register type (see below).
         */
        DBGFREGVALTYPE  enmType;
        DBGFREGVAL      Value;
        rc = DBGFR3RegNmQuery(pUVM, idCpu, pszReg, &Value, &enmType);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_DBGF_REGISTER_NOT_FOUND)
                return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown register: '%s%s'.\n",
                                           pszActualPrefix,  pszReg);
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegNmQuery failed querying '%s%s': %Rrc.\n",
                                       pszActualPrefix,  pszReg, rc);
        }

        DBGCVAR   NewValueTmp;
        PCDBGCVAR pNewValue;
        if (cArgs == 3)
        {
            DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 1, paArgs[1].enmType == DBGCVAR_TYPE_STRING);
            if (strcmp(paArgs[1].u.pszString, "="))
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "Second argument must be '='.");
            pNewValue = &paArgs[2];
        }
        else
        {
            /* Not possible to convince the parser to support both codeview and
               windbg syntax and make the equal sign optional.  Try help it. */
            /** @todo make DBGCCmdHlpConvert do more with strings. */
            rc = DBGCCmdHlpConvert(pCmdHlp, &paArgs[1], DBGCVAR_TYPE_NUMBER, true /*fConvSyms*/, &NewValueTmp);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "The last argument must be a value or valid symbol.");
            pNewValue = &NewValueTmp;
        }

        /*
         * Modify the register.
         */
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 1, pNewValue->enmType == DBGCVAR_TYPE_NUMBER);
        if (enmType != DBGFREGVALTYPE_DTR)
        {
            enmType = DBGFREGVALTYPE_U64;
            rc = DBGCCmdHlpVarToNumber(pCmdHlp, pNewValue, &Value.u64);
        }
        else
        {
            enmType = DBGFREGVALTYPE_DTR;
            rc = DBGCCmdHlpVarToNumber(pCmdHlp, pNewValue, &Value.dtr.u64Base);
            if (RT_SUCCESS(rc) && pNewValue->enmRangeType != DBGCVAR_RANGE_NONE)
                Value.dtr.u32Limit = (uint32_t)pNewValue->u64Range;
        }
        if (RT_SUCCESS(rc))
        {
            rc = DBGFR3RegNmSet(pUVM, idCpu, pszReg, &Value, enmType);
            if (RT_FAILURE(rc))
                rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegNmSet failed settings '%s%s': %Rrc\n",
                                         pszActualPrefix, pszReg, rc);
            if (rc != VINF_SUCCESS)
                DBGCCmdHlpPrintf(pCmdHlp, "%s: warning: %Rrc\n", pCmd->pszCmd, rc);
        }
        else
            rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3RegFormatValue failed: %Rrc.\n", rc);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD,
 *      The 'rg'\, 'rg64' and 'rg32' commands\, worker for 'r'.}
 */
static DECLCALLBACK(int) dbgcCmdRegGuest(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Show all registers our selves.
     */
    if (cArgs == 0)
    {
        PDBGC       pDbgc      = DBGC_CMDHLP2DBGC(pCmdHlp);
        bool const  f64BitMode = !strcmp(pCmd->pszCmd, "rg64")
                              || (   strcmp(pCmd->pszCmd, "rg32") != 0
                                  && DBGFR3CpuIsIn64BitCode(pUVM, pDbgc->idCpu));
        return DBGCCmdHlpRegPrintf(pCmdHlp, pDbgc->idCpu, f64BitMode, pDbgc->fRegTerse);
    }
    return dbgcCmdRegCommon(pCmd, pCmdHlp, pUVM, paArgs, cArgs, "");
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'rt' command.}
 */
static DECLCALLBACK(int) dbgcCmdRegTerse(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    NOREF(pCmd); NOREF(pUVM); NOREF(paArgs); NOREF(cArgs);

    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    pDbgc->fRegTerse = !pDbgc->fRegTerse;
    return DBGCCmdHlpPrintf(pCmdHlp, pDbgc->fRegTerse ? "info: Terse register info.\n" : "info: Verbose register info.\n");
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'pr' and 'tr' commands.}
 */
static DECLCALLBACK(int) dbgcCmdStepTraceToggle(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    Assert(cArgs == 0); NOREF(pCmd); NOREF(pUVM); NOREF(paArgs); NOREF(cArgs);

    /* Note! windbg accepts 'r' as a flag to 'p', 'pa', 'pc', 'pt', 't',
             'ta', 'tc' and 'tt'.  We've simplified it.  */
    pDbgc->fStepTraceRegs = !pDbgc->fStepTraceRegs;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'p'\, 'pc'\, 'pt'\, 't'\, 'tc'\, and 'tt' commands.}
 */
static DECLCALLBACK(int) dbgcCmdStepTrace(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    if (cArgs != 0)
        return DBGCCmdHlpFail(pCmdHlp, pCmd,
                              "Sorry, but the '%s' command does not currently implement any arguments.\n", pCmd->pszCmd);

    /* The 'count' has to be implemented by DBGC, whereas the
       filtering is taken care of by DBGF. */

    /*
     * Convert the command to DBGF_STEP_F_XXX and other API input.
     */
    //DBGFADDRESS StackPop;
    PDBGFADDRESS pStackPop  = NULL;
    RTGCPTR      cbStackPop = 0;
    uint32_t     cMaxSteps  = pCmd->pszCmd[0] == 'p' ? _512K : _64K;
    uint32_t     fFlags     = pCmd->pszCmd[0] == 'p' ? DBGF_STEP_F_OVER : DBGF_STEP_F_INTO;
    if (pCmd->pszCmd[1] == 'c')
        fFlags |= DBGF_STEP_F_STOP_ON_CALL;
    else if (pCmd->pszCmd[1] == 't')
        fFlags |= DBGF_STEP_F_STOP_ON_RET;
    else if (pCmd->pszCmd[0] != 'p')
        cMaxSteps = 1;
    else
    {
        /** @todo consider passing RSP + 1 in for 'p' and something else sensible for
         *        the 'pt' command. */
    }

    int rc = DBGFR3StepEx(pUVM, pDbgc->idCpu, fFlags, NULL, pStackPop, cbStackPop, cMaxSteps);
    if (RT_SUCCESS(rc))
        pDbgc->fReady = false;
    else
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3StepEx(,,%#x,) failed", fFlags);

    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'pa' and 'ta' commands.}
 */
static DECLCALLBACK(int) dbgcCmdStepTraceTo(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    if (cArgs != 1)
        return DBGCCmdHlpFail(pCmdHlp, pCmd,
                              "Sorry, but the '%s' command only implements a single argument at present.\n", pCmd->pszCmd);
    DBGFADDRESS Address;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[0], &Address);
    if (RT_FAILURE(rc))
        return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "VarToDbgfAddr(,%Dv,)\n", &paArgs[0]);

    uint32_t cMaxSteps = pCmd->pszCmd[0] == 'p' ? _512K : 1;
    uint32_t fFlags    = pCmd->pszCmd[0] == 'p' ? DBGF_STEP_F_OVER : DBGF_STEP_F_INTO;
    rc = DBGFR3StepEx(pUVM, pDbgc->idCpu, fFlags, &Address, NULL, 0, cMaxSteps);
    if (RT_SUCCESS(rc))
        pDbgc->fReady = false;
    else
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3StepEx(,,%#x,) failed", fFlags);
    return rc;
}


/**
 * Helper that tries to resolve a far address to a symbol and formats it.
 *
 * @returns Pointer to symbol string on success, NULL if not resolved.
 *          Free using RTStrFree.
 * @param   pCmdHlp             The command helper structure.
 * @param   hAs                 The address space to use.  NIL_RTDBGAS means no symbol resolving.
 * @param   sel                 The selector part of the address.
 * @param   off                 The offset part of the address.
 * @param   pszPrefix           How to prefix the symbol string.
 * @param   pszSuffix           How to suffix the symbol string.
 */
static char *dbgcCmdHlpFarAddrToSymbol(PDBGCCMDHLP pCmdHlp, RTDBGAS hAs, RTSEL sel, uint64_t off,
                                       const char *pszPrefix, const char *pszSuffix)
{
    char *pszRet = NULL;
    if (hAs != NIL_RTDBGAS)
    {
        PDBGC        pDbgc      = DBGC_CMDHLP2DBGC(pCmdHlp);
        DBGFADDRESS  Addr;
        int rc = DBGFR3AddrFromSelOff(pDbgc->pUVM, pDbgc->idCpu, &Addr, sel, off);
        if (RT_SUCCESS(rc))
        {
            RTGCINTPTR   offDispSym = 0;
            PRTDBGSYMBOL pSymbol = DBGFR3AsSymbolByAddrA(pDbgc->pUVM, hAs, &Addr,
                                                           RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL
                                                         | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                                         &offDispSym, NULL);
            if (pSymbol)
            {
                if (offDispSym == 0)
                    pszRet = RTStrAPrintf2("%s%s%s", pszPrefix, pSymbol->szName, pszSuffix);
                else if (offDispSym > 0)
                    pszRet = RTStrAPrintf2("%s%s+%llx%s", pszPrefix, pSymbol->szName, (int64_t)offDispSym, pszSuffix);
                else
                    pszRet = RTStrAPrintf2("%s%s-%llx%s", pszPrefix, pSymbol->szName, -(int64_t)offDispSym, pszSuffix);
                RTDbgSymbolFree(pSymbol);
            }
        }
    }
    return pszRet;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'k'\, 'kg' and 'kh' commands.}
 */
static DECLCALLBACK(int) dbgcCmdStack(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Figure which context we're called for and start walking that stack.
     */
    int                 rc;
    PCDBGFSTACKFRAME    pFirstFrame;
    bool const          fGuest = true;
    bool const          fVerbose = pCmd->pszCmd[1] == 'v'
                                || (pCmd->pszCmd[1] != '\0' && pCmd->pszCmd[2] == 'v');
    rc = DBGFR3StackWalkBegin(pUVM, pDbgc->idCpu, fGuest ? DBGFCODETYPE_GUEST : DBGFCODETYPE_HYPER, &pFirstFrame);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpPrintf(pCmdHlp, "Failed to begin stack walk, rc=%Rrc\n", rc);

    /*
     * Print the frames.
     */
    char     szTmp[1024];
    uint32_t fBitFlags = 0;
    for (PCDBGFSTACKFRAME pFrame = pFirstFrame;
         pFrame;
         pFrame = DBGFR3StackWalkNext(pFrame))
    {
        uint32_t const fCurBitFlags = pFrame->fFlags & (DBGFSTACKFRAME_FLAGS_16BIT | DBGFSTACKFRAME_FLAGS_32BIT | DBGFSTACKFRAME_FLAGS_64BIT);
        if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_16BIT)
        {
            if (fCurBitFlags != fBitFlags)
                pCmdHlp->pfnPrintf(pCmdHlp,  NULL, "#  SS:BP     Ret SS:BP Ret CS:EIP    Arg0     Arg1     Arg2     Arg3     CS:EIP / Symbol [line]\n");
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%02x %04RX16:%04RX16 %04RX16:%04RX16 %04RX32:%08RX32 %08RX32 %08RX32 %08RX32 %08RX32",
                                  pFrame->iFrame,
                                  pFrame->AddrFrame.Sel,
                                  (uint16_t)pFrame->AddrFrame.off,
                                  pFrame->AddrReturnFrame.Sel,
                                  (uint16_t)pFrame->AddrReturnFrame.off,
                                  (uint32_t)pFrame->AddrReturnPC.Sel,
                                  (uint32_t)pFrame->AddrReturnPC.off,
                                  pFrame->Args.au32[0],
                                  pFrame->Args.au32[1],
                                  pFrame->Args.au32[2],
                                  pFrame->Args.au32[3]);
        }
        else if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_32BIT)
        {
            if (fCurBitFlags != fBitFlags)
                pCmdHlp->pfnPrintf(pCmdHlp,  NULL, "#  EBP      Ret EBP  Ret CS:EIP    Arg0     Arg1     Arg2     Arg3     CS:EIP / Symbol [line]\n");
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%02x %08RX32 %08RX32 %04RX32:%08RX32 %08RX32 %08RX32 %08RX32 %08RX32",
                                  pFrame->iFrame,
                                  (uint32_t)pFrame->AddrFrame.off,
                                  (uint32_t)pFrame->AddrReturnFrame.off,
                                  (uint32_t)pFrame->AddrReturnPC.Sel,
                                  (uint32_t)pFrame->AddrReturnPC.off,
                                  pFrame->Args.au32[0],
                                  pFrame->Args.au32[1],
                                  pFrame->Args.au32[2],
                                  pFrame->Args.au32[3]);
        }
        else if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_64BIT)
        {
            if (fCurBitFlags != fBitFlags)
                pCmdHlp->pfnPrintf(pCmdHlp,  NULL, "#  RBP              Ret SS:RBP            Ret RIP          CS:RIP / Symbol [line]\n");
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%02x %016RX64 %04RX16:%016RX64 %016RX64",
                                  pFrame->iFrame,
                                  (uint64_t)pFrame->AddrFrame.off,
                                  pFrame->AddrReturnFrame.Sel,
                                  (uint64_t)pFrame->AddrReturnFrame.off,
                                  (uint64_t)pFrame->AddrReturnPC.off);
        }
        if (RT_FAILURE(rc))
            break;
        if (!pFrame->pSymPC)
            rc = pCmdHlp->pfnPrintf(pCmdHlp, NULL,
                                    fCurBitFlags & DBGFSTACKFRAME_FLAGS_64BIT
                                    ? " %RTsel:%016RGv"
                                    : fCurBitFlags & DBGFSTACKFRAME_FLAGS_32BIT
                                    ? " %RTsel:%08RGv"
                                    : " %RTsel:%04RGv"
                                    , pFrame->AddrPC.Sel, pFrame->AddrPC.off);
        else
        {
            RTGCINTPTR offDisp = pFrame->AddrPC.FlatPtr - pFrame->pSymPC->Value; /** @todo this isn't 100% correct for segmented stuff. */
            if (offDisp > 0)
                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s+%llx", pFrame->pSymPC->szName, (int64_t)offDisp);
            else if (offDisp < 0)
                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s-%llx", pFrame->pSymPC->szName, -(int64_t)offDisp);
            else
                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s", pFrame->pSymPC->szName);
        }
        if (RT_SUCCESS(rc) && pFrame->pLinePC)
            rc = DBGCCmdHlpPrintf(pCmdHlp, " [%s @ 0i%d]", pFrame->pLinePC->szFilename, pFrame->pLinePC->uLineNo);
        if (RT_SUCCESS(rc))
            rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");

        if (fVerbose && RT_SUCCESS(rc))
        {
            /*
             * Display verbose frame info.
             */
            const char *pszRetType = "invalid";
            switch (pFrame->enmReturnType)
            {
                case RTDBGRETURNTYPE_NEAR16:        pszRetType = "retn/16"; break;
                case RTDBGRETURNTYPE_NEAR32:        pszRetType = "retn/32"; break;
                case RTDBGRETURNTYPE_NEAR64:        pszRetType = "retn/64"; break;
                case RTDBGRETURNTYPE_FAR16:         pszRetType = "retf/16"; break;
                case RTDBGRETURNTYPE_FAR32:         pszRetType = "retf/32"; break;
                case RTDBGRETURNTYPE_FAR64:         pszRetType = "retf/64"; break;
                case RTDBGRETURNTYPE_IRET16:        pszRetType = "iret-16"; break;
                case RTDBGRETURNTYPE_IRET32:        pszRetType = "iret/32s"; break;
                case RTDBGRETURNTYPE_IRET32_PRIV:   pszRetType = "iret/32p"; break;
                case RTDBGRETURNTYPE_IRET32_V86:    pszRetType = "iret/v86"; break;
                case RTDBGRETURNTYPE_IRET64:        pszRetType = "iret/64"; break;

                case RTDBGRETURNTYPE_END:
                case RTDBGRETURNTYPE_INVALID:
                case RTDBGRETURNTYPE_32BIT_HACK:
                    break;
            }
            size_t cchLine = DBGCCmdHlpPrintfLen(pCmdHlp, "   %s", pszRetType);
            if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_UNWIND_INFO)
                cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " used-unwind-info");
            if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_USED_ODD_EVEN)
                cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " used-odd-even");
            if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_REAL_V86)
                cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " real-v86");
            if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_MAX_DEPTH)
                cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " max-depth");
            if (pFrame->fFlags & DBGFSTACKFRAME_FLAGS_TRAP_FRAME)
                cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " trap-frame");

            if (pFrame->cSureRegs > 0)
            {
                cchLine = 1024; /* force new line */
                for (uint32_t i = 0; i < pFrame->cSureRegs; i++)
                {
                    if (cchLine > 80)
                    {
                        DBGCCmdHlpPrintf(pCmdHlp, "\n  ");
                        cchLine = 2;
                    }

                    szTmp[0] = '\0';
                    DBGFR3RegFormatValue(szTmp, sizeof(szTmp), &pFrame->paSureRegs[i].Value,
                                         pFrame->paSureRegs[i].enmType, false);
                    const char *pszName = pFrame->paSureRegs[i].enmReg != DBGFREG_END
                                        ? DBGFR3RegCpuName(pUVM, pFrame->paSureRegs[i].enmReg, pFrame->paSureRegs[i].enmType)
                                        : pFrame->paSureRegs[i].pszName;
                    cchLine += DBGCCmdHlpPrintfLen(pCmdHlp, " %s=%s", pszName, szTmp);
                }
            }

            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
        }

        if (RT_FAILURE(rc))
            break;

        fBitFlags = fCurBitFlags;
    }

    DBGFR3StackWalkEnd(pFirstFrame);

    NOREF(paArgs); NOREF(cArgs);
    return rc;
}


/**
 * Worker function that displays one descriptor entry (GDT, LDT, IDT).
 *
 * @returns pfnPrintf status code.
 * @param   pCmdHlp     The DBGC command helpers.
 * @param   pDesc       The descriptor to display.
 * @param   iEntry      The descriptor entry number.
 * @param   fHyper      Whether the selector belongs to the hypervisor or not.
 * @param   hAs         Address space to use when resolving symbols.
 * @param   pfDblEntry  Where to indicate whether the entry is two entries wide.
 *                      Optional.
 */
static int dbgcCmdDumpDTWorker64(PDBGCCMDHLP pCmdHlp, PCX86DESC64 pDesc, unsigned iEntry, bool fHyper, RTDBGAS hAs,
                                 bool *pfDblEntry)
{
    /* GUEST64 */
    int rc;

    const char *pszHyper = fHyper ? " HYPER" : "";
    const char *pszPresent = pDesc->Gen.u1Present ? "P " : "NP";
    if (pDesc->Gen.u1DescType)
    {
        static const char * const s_apszTypes[] =
        {
            "DataRO", /* 0 Read-Only */
            "DataRO", /* 1 Read-Only - Accessed */
            "DataRW", /* 2 Read/Write  */
            "DataRW", /* 3 Read/Write - Accessed  */
            "DownRO", /* 4 Expand-down, Read-Only  */
            "DownRO", /* 5 Expand-down, Read-Only - Accessed */
            "DownRW", /* 6 Expand-down, Read/Write  */
            "DownRW", /* 7 Expand-down, Read/Write - Accessed */
            "CodeEO", /* 8 Execute-Only */
            "CodeEO", /* 9 Execute-Only - Accessed */
            "CodeER", /* A Execute/Readable */
            "CodeER", /* B Execute/Readable - Accessed */
            "ConfE0", /* C Conforming, Execute-Only */
            "ConfE0", /* D Conforming, Execute-Only - Accessed */
            "ConfER", /* E Conforming, Execute/Readable */
            "ConfER"  /* F Conforming, Execute/Readable - Accessed */
        };
        const char *pszAccessed = pDesc->Gen.u4Type & RT_BIT(0) ? "A " : "NA";
        const char *pszGranularity = pDesc->Gen.u1Granularity ? "G" : " ";
        const char *pszBig = pDesc->Gen.u1DefBig ? "BIG" : "   ";
        uint32_t u32Base = X86DESC_BASE(pDesc);
        uint32_t cbLimit = X86DESC_LIMIT_G(pDesc);

        rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Bas=%08x Lim=%08x DPL=%d %s %s %s %s AVL=%d L=%d%s\n",
                                iEntry, s_apszTypes[pDesc->Gen.u4Type], u32Base, cbLimit,
                                pDesc->Gen.u2Dpl, pszPresent, pszAccessed, pszGranularity, pszBig,
                                pDesc->Gen.u1Available, pDesc->Gen.u1Long, pszHyper);
    }
    else
    {
        static const char * const s_apszTypes[] =
        {
            "Ill-0 ", /* 0 0000 Reserved (Illegal) */
            "Ill-1 ", /* 1 0001 Available 16-bit TSS */
            "LDT   ", /* 2 0010 LDT */
            "Ill-3 ", /* 3 0011 Busy 16-bit TSS */
            "Ill-4 ", /* 4 0100 16-bit Call Gate */
            "Ill-5 ", /* 5 0101 Task Gate */
            "Ill-6 ", /* 6 0110 16-bit Interrupt Gate */
            "Ill-7 ", /* 7 0111 16-bit Trap Gate */
            "Ill-8 ", /* 8 1000 Reserved (Illegal) */
            "Tss64A", /* 9 1001 Available 32-bit TSS */
            "Ill-A ", /* A 1010 Reserved (Illegal) */
            "Tss64B", /* B 1011 Busy 32-bit TSS */
            "Call64", /* C 1100 32-bit Call Gate */
            "Ill-D ", /* D 1101 Reserved (Illegal) */
            "Int64 ", /* E 1110 32-bit Interrupt Gate */
            "Trap64"  /* F 1111 32-bit Trap Gate */
        };
        switch (pDesc->Gen.u4Type)
        {
            /* raw */
            case X86_SEL_TYPE_SYS_UNDEFINED:
            case X86_SEL_TYPE_SYS_UNDEFINED2:
            case X86_SEL_TYPE_SYS_UNDEFINED4:
            case X86_SEL_TYPE_SYS_UNDEFINED3:
            case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
            case X86_SEL_TYPE_SYS_286_TSS_BUSY:
            case X86_SEL_TYPE_SYS_286_CALL_GATE:
            case X86_SEL_TYPE_SYS_286_INT_GATE:
            case X86_SEL_TYPE_SYS_286_TRAP_GATE:
            case X86_SEL_TYPE_SYS_TASK_GATE:
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s %.8Rhxs   DPL=%d %s%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], pDesc,
                                        pDesc->Gen.u2Dpl, pszPresent, pszHyper);
                break;

            case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
            case X86_SEL_TYPE_SYS_386_TSS_BUSY:
            case X86_SEL_TYPE_SYS_LDT:
            {
                const char *pszBusy        = pDesc->Gen.u4Type & RT_BIT(1) ? "B " : "NB";
                const char *pszBig         = pDesc->Gen.u1DefBig ? "BIG" : "   ";
                const char *pszLong        = pDesc->Gen.u1Long ? "LONG" : "   ";

                uint64_t u64Base = X86DESC64_BASE(pDesc);
                uint32_t cbLimit = X86DESC_LIMIT_G(pDesc);

                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Bas=%016RX64 Lim=%08x DPL=%d %s %s %s %sAVL=%d R=%d%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], u64Base, cbLimit,
                                        pDesc->Gen.u2Dpl, pszPresent, pszBusy, pszLong, pszBig,
                                        pDesc->Gen.u1Available, pDesc->Gen.u1Long | (pDesc->Gen.u1DefBig << 1),
                                        pszHyper);
                if (pfDblEntry)
                    *pfDblEntry = true;
                break;
            }

            case X86_SEL_TYPE_SYS_386_CALL_GATE:
            {
                unsigned cParams = pDesc->au8[4] & 0x1f;
                const char *pszCountOf = pDesc->Gen.u4Type & RT_BIT(3) ? "DC" : "WC";
                RTSEL sel = pDesc->au16[1];
                uint64_t off =    pDesc->au16[0]
                                | ((uint64_t)pDesc->au16[3] << 16)
                                | ((uint64_t)pDesc->Gen.u32BaseHigh3 << 32);
                char *pszSymbol = dbgcCmdHlpFarAddrToSymbol(pCmdHlp, hAs, sel, off, " (", ")");
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Sel:Off=%04x:%016RX64     DPL=%d %s %s=%d%s%s\n",
                                      iEntry, s_apszTypes[pDesc->Gen.u4Type], sel, off,
                                      pDesc->Gen.u2Dpl, pszPresent, pszCountOf, cParams, pszHyper, pszSymbol ? pszSymbol : "");
                RTStrFree(pszSymbol);
                if (pfDblEntry)
                    *pfDblEntry = true;
                break;
            }

            case X86_SEL_TYPE_SYS_386_INT_GATE:
            case X86_SEL_TYPE_SYS_386_TRAP_GATE:
            {
                RTSEL sel = pDesc->Gate.u16Sel;
                uint64_t off =            pDesc->Gate.u16OffsetLow
                             | ((uint64_t)pDesc->Gate.u16OffsetHigh << 16)
                             | ((uint64_t)pDesc->Gate.u32OffsetTop  << 32);
                char *pszSymbol = dbgcCmdHlpFarAddrToSymbol(pCmdHlp, hAs, sel, off, " (", ")");
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Sel:Off=%04x:%016RX64     DPL=%u %s IST=%u%s%s\n",
                                        iEntry, s_apszTypes[pDesc->Gate.u4Type], sel, off,
                                        pDesc->Gate.u2Dpl, pszPresent, pDesc->Gate.u3IST, pszHyper, pszSymbol ? pszSymbol : "");
                RTStrFree(pszSymbol);
                if (pfDblEntry)
                    *pfDblEntry = true;
                break;
            }

            /* impossible, just it's necessary to keep gcc happy. */
            default:
                return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * Worker function that displays one descriptor entry (GDT, LDT, IDT).
 *
 * @returns pfnPrintf status code.
 * @param   pCmdHlp     The DBGC command helpers.
 * @param   pDesc       The descriptor to display.
 * @param   iEntry      The descriptor entry number.
 * @param   fHyper      Whether the selector belongs to the hypervisor or not.
 * @param   hAs         Address space to use when resolving symbols.
 */
static int dbgcCmdDumpDTWorker32(PDBGCCMDHLP pCmdHlp, PCX86DESC pDesc, unsigned iEntry, bool fHyper, RTDBGAS hAs)
{
    int rc;

    const char *pszHyper = fHyper ? " HYPER" : "";
    const char *pszPresent = pDesc->Gen.u1Present ? "P " : "NP";
    if (pDesc->Gen.u1DescType)
    {
        static const char * const s_apszTypes[] =
        {
            "DataRO", /* 0 Read-Only */
            "DataRO", /* 1 Read-Only - Accessed */
            "DataRW", /* 2 Read/Write  */
            "DataRW", /* 3 Read/Write - Accessed  */
            "DownRO", /* 4 Expand-down, Read-Only  */
            "DownRO", /* 5 Expand-down, Read-Only - Accessed */
            "DownRW", /* 6 Expand-down, Read/Write  */
            "DownRW", /* 7 Expand-down, Read/Write - Accessed */
            "CodeEO", /* 8 Execute-Only */
            "CodeEO", /* 9 Execute-Only - Accessed */
            "CodeER", /* A Execute/Readable */
            "CodeER", /* B Execute/Readable - Accessed */
            "ConfE0", /* C Conforming, Execute-Only */
            "ConfE0", /* D Conforming, Execute-Only - Accessed */
            "ConfER", /* E Conforming, Execute/Readable */
            "ConfER"  /* F Conforming, Execute/Readable - Accessed */
        };
        const char *pszAccessed = pDesc->Gen.u4Type & RT_BIT(0) ? "A " : "NA";
        const char *pszGranularity = pDesc->Gen.u1Granularity ? "G" : " ";
        const char *pszBig = pDesc->Gen.u1DefBig ? "BIG" : "   ";
        uint32_t u32Base = pDesc->Gen.u16BaseLow
                         | ((uint32_t)pDesc->Gen.u8BaseHigh1 << 16)
                         | ((uint32_t)pDesc->Gen.u8BaseHigh2 << 24);
        uint32_t cbLimit = pDesc->Gen.u16LimitLow | (pDesc->Gen.u4LimitHigh << 16);
        if (pDesc->Gen.u1Granularity)
            cbLimit <<= GUEST_PAGE_SHIFT;

        rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Bas=%08x Lim=%08x DPL=%d %s %s %s %s AVL=%d L=%d%s\n",
                                iEntry, s_apszTypes[pDesc->Gen.u4Type], u32Base, cbLimit,
                                pDesc->Gen.u2Dpl, pszPresent, pszAccessed, pszGranularity, pszBig,
                                pDesc->Gen.u1Available, pDesc->Gen.u1Long, pszHyper);
    }
    else
    {
        static const char * const s_apszTypes[] =
        {
            "Ill-0 ", /* 0 0000 Reserved (Illegal) */
            "Tss16A", /* 1 0001 Available 16-bit TSS */
            "LDT   ", /* 2 0010 LDT */
            "Tss16B", /* 3 0011 Busy 16-bit TSS */
            "Call16", /* 4 0100 16-bit Call Gate */
            "TaskG ", /* 5 0101 Task Gate */
            "Int16 ", /* 6 0110 16-bit Interrupt Gate */
            "Trap16", /* 7 0111 16-bit Trap Gate */
            "Ill-8 ", /* 8 1000 Reserved (Illegal) */
            "Tss32A", /* 9 1001 Available 32-bit TSS */
            "Ill-A ", /* A 1010 Reserved (Illegal) */
            "Tss32B", /* B 1011 Busy 32-bit TSS */
            "Call32", /* C 1100 32-bit Call Gate */
            "Ill-D ", /* D 1101 Reserved (Illegal) */
            "Int32 ", /* E 1110 32-bit Interrupt Gate */
            "Trap32"  /* F 1111 32-bit Trap Gate */
        };
        switch (pDesc->Gen.u4Type)
        {
            /* raw */
            case X86_SEL_TYPE_SYS_UNDEFINED:
            case X86_SEL_TYPE_SYS_UNDEFINED2:
            case X86_SEL_TYPE_SYS_UNDEFINED4:
            case X86_SEL_TYPE_SYS_UNDEFINED3:
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s %.8Rhxs   DPL=%d %s%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], pDesc,
                                        pDesc->Gen.u2Dpl, pszPresent, pszHyper);
                break;

            case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
            case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
            case X86_SEL_TYPE_SYS_286_TSS_BUSY:
            case X86_SEL_TYPE_SYS_386_TSS_BUSY:
            case X86_SEL_TYPE_SYS_LDT:
            {
                const char *pszGranularity = pDesc->Gen.u1Granularity ? "G" : " ";
                const char *pszBusy = pDesc->Gen.u4Type & RT_BIT(1) ? "B " : "NB";
                const char *pszBig = pDesc->Gen.u1DefBig ? "BIG" : "   ";
                uint32_t u32Base = pDesc->Gen.u16BaseLow
                                 | ((uint32_t)pDesc->Gen.u8BaseHigh1 << 16)
                                 | ((uint32_t)pDesc->Gen.u8BaseHigh2 << 24);
                uint32_t cbLimit = pDesc->Gen.u16LimitLow | (pDesc->Gen.u4LimitHigh << 16);
                if (pDesc->Gen.u1Granularity)
                    cbLimit <<= GUEST_PAGE_SHIFT;

                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Bas=%08x Lim=%08x DPL=%d %s %s %s %s AVL=%d R=%d%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], u32Base, cbLimit,
                                        pDesc->Gen.u2Dpl, pszPresent, pszBusy, pszGranularity, pszBig,
                                        pDesc->Gen.u1Available, pDesc->Gen.u1Long | (pDesc->Gen.u1DefBig << 1),
                                        pszHyper);
                break;
            }

            case X86_SEL_TYPE_SYS_TASK_GATE:
            {
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s TSS=%04x                  DPL=%d %s%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], pDesc->au16[1],
                                        pDesc->Gen.u2Dpl, pszPresent, pszHyper);
                break;
            }

            case X86_SEL_TYPE_SYS_286_CALL_GATE:
            case X86_SEL_TYPE_SYS_386_CALL_GATE:
            {
                unsigned cParams = pDesc->au8[4] & 0x1f;
                const char *pszCountOf = pDesc->Gen.u4Type & RT_BIT(3) ? "DC" : "WC";
                RTSEL sel = pDesc->au16[1];
                uint32_t off = pDesc->au16[0] | ((uint32_t)pDesc->au16[3] << 16);
                char *pszSymbol = dbgcCmdHlpFarAddrToSymbol(pCmdHlp, hAs, sel, off, " (", ")");
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Sel:Off=%04x:%08x     DPL=%d %s %s=%d%s%s\n",
                                      iEntry, s_apszTypes[pDesc->Gen.u4Type], sel, off,
                                      pDesc->Gen.u2Dpl, pszPresent, pszCountOf, cParams, pszHyper, pszSymbol ? pszSymbol : "");
                RTStrFree(pszSymbol);
                break;
            }

            case X86_SEL_TYPE_SYS_286_INT_GATE:
            case X86_SEL_TYPE_SYS_386_INT_GATE:
            case X86_SEL_TYPE_SYS_286_TRAP_GATE:
            case X86_SEL_TYPE_SYS_386_TRAP_GATE:
            {
                RTSEL sel = pDesc->au16[1];
                uint32_t off = pDesc->au16[0] | ((uint32_t)pDesc->au16[3] << 16);
                char *pszSymbol = dbgcCmdHlpFarAddrToSymbol(pCmdHlp, hAs, sel, off, " (", ")");
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %s Sel:Off=%04x:%08x     DPL=%d %s%s%s\n",
                                        iEntry, s_apszTypes[pDesc->Gen.u4Type], sel, off,
                                        pDesc->Gen.u2Dpl, pszPresent, pszHyper, pszSymbol ? pszSymbol : "");
                RTStrFree(pszSymbol);
                break;
            }

            /* impossible, just it's necessary to keep gcc happy. */
            default:
                return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dg'\, 'dga'\, 'dl' and 'dla' commands.}
 */
static DECLCALLBACK(int) dbgcCmdDumpDT(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Get the CPU mode, check which command variation this is
     * and fix a default parameter if needed.
     */
    PDBGC       pDbgc   = DBGC_CMDHLP2DBGC(pCmdHlp);
    PVMCPU      pVCpu   = VMMR3GetCpuByIdU(pUVM, pDbgc->idCpu);
    CPUMMODE    enmMode = CPUMGetGuestMode(pVCpu);
    bool        fGdt    = pCmd->pszCmd[1] == 'g';
    bool        fAll    = pCmd->pszCmd[2] == 'a';
    RTSEL       SelTable = fGdt ? 0 : X86_SEL_LDT;

    DBGCVAR Var;
    if (!cArgs)
    {
        cArgs = 1;
        paArgs = &Var;
        Var.enmType = DBGCVAR_TYPE_NUMBER;
        Var.u.u64Number = fGdt ? 0 : 4;
        Var.enmRangeType = DBGCVAR_RANGE_ELEMENTS;
        Var.u64Range = 1024;
    }

    /*
     * Process the arguments.
     */
    for (unsigned i = 0; i < cArgs; i++)
    {
         /*
          * Retrieve the selector value from the argument.
          * The parser may confuse pointers and numbers if more than one
          * argument is given, that that into account.
          */
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, i, paArgs[i].enmType == DBGCVAR_TYPE_NUMBER || DBGCVAR_ISPOINTER(paArgs[i].enmType));
        uint64_t u64;
        unsigned cSels = 1;
        switch (paArgs[i].enmType)
        {
            case DBGCVAR_TYPE_NUMBER:
                u64 = paArgs[i].u.u64Number;
                if (paArgs[i].enmRangeType != DBGCVAR_RANGE_NONE)
                    cSels = RT_MIN(paArgs[i].u64Range, 1024);
                break;
            case DBGCVAR_TYPE_GC_FAR:   u64 = paArgs[i].u.GCFar.sel; break;
            case DBGCVAR_TYPE_GC_FLAT:  u64 = paArgs[i].u.GCFlat; break;
            case DBGCVAR_TYPE_GC_PHYS:  u64 = paArgs[i].u.GCPhys; break;
            case DBGCVAR_TYPE_HC_FLAT:  u64 = (uintptr_t)paArgs[i].u.pvHCFlat; break;
            case DBGCVAR_TYPE_HC_PHYS:  u64 = paArgs[i].u.HCPhys; break;
            default:                    u64 = _64K; break;
        }
        if (u64 < _64K)
        {
            unsigned Sel = (RTSEL)u64;

            /*
             * Dump the specified range.
             */
            bool fSingle = cSels == 1;
            while (     cSels-- > 0
                   &&   Sel < _64K)
            {
                DBGFSELINFO SelInfo;
                int rc = DBGFR3SelQueryInfo(pUVM, pDbgc->idCpu, Sel | SelTable, DBGFSELQI_FLAGS_DT_GUEST, &SelInfo);
                if (RT_SUCCESS(rc))
                {
                    if (SelInfo.fFlags & DBGFSELINFO_FLAGS_REAL_MODE)
                        rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x RealM   Bas=%04x     Lim=%04x\n",
                                                Sel, (unsigned)SelInfo.GCPtrBase, (unsigned)SelInfo.cbLimit);
                    else if (   fAll
                             || fSingle
                             || SelInfo.u.Raw.Gen.u1Present)
                    {
                        if (enmMode == CPUMMODE_PROTECTED)
                            rc = dbgcCmdDumpDTWorker32(pCmdHlp, &SelInfo.u.Raw, Sel,
                                                       !!(SelInfo.fFlags & DBGFSELINFO_FLAGS_HYPER), DBGF_AS_GLOBAL);
                        else
                        {
                            bool fDblSkip = false;
                            rc = dbgcCmdDumpDTWorker64(pCmdHlp, &SelInfo.u.Raw64, Sel,
                                                       !!(SelInfo.fFlags & DBGFSELINFO_FLAGS_HYPER), DBGF_AS_GLOBAL, &fDblSkip);
                            if (fDblSkip)
                                Sel += 4;
                        }
                    }
                }
                else
                {
                    rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %Rrc\n", Sel, rc);
                    if (!fAll)
                        return rc;
                }
                if (RT_FAILURE(rc))
                    return rc;

                /* next */
                Sel += 8;
            }
        }
        else
            DBGCCmdHlpPrintf(pCmdHlp, "error: %llx is out of bounds\n", u64);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'di' and 'dia' commands.}
 */
static DECLCALLBACK(int) dbgcCmdDumpIDT(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Establish some stuff like the current IDTR and CPU mode,
     * and fix a default parameter.
     */
    PDBGC       pDbgc     = DBGC_CMDHLP2DBGC(pCmdHlp);
    CPUMMODE    enmMode   = DBGCCmdHlpGetCpuMode(pCmdHlp);
    uint16_t    cbLimit   = 0;
    uint64_t    GCFlat    = 0;
    int rc = DBGFR3RegCpuQueryXdtr(pDbgc->pUVM, pDbgc->idCpu, DBGFREG_IDTR, &GCFlat, &cbLimit);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3RegCpuQueryXdtr/DBGFREG_IDTR");
    unsigned    cbEntry;
    switch (enmMode)
    {
        case CPUMMODE_REAL:         cbEntry = sizeof(RTFAR16); break;
        case CPUMMODE_PROTECTED:    cbEntry = sizeof(X86DESC); break;
        case CPUMMODE_LONG:         cbEntry = sizeof(X86DESC64); break;
        default:
            return DBGCCmdHlpPrintf(pCmdHlp, "error: Invalid CPU mode %d.\n", enmMode);
    }

    bool fAll = pCmd->pszCmd[2] == 'a';
    DBGCVAR Var;
    if (!cArgs)
    {
        cArgs = 1;
        paArgs = &Var;
        Var.enmType = DBGCVAR_TYPE_NUMBER;
        Var.u.u64Number = 0;
        Var.enmRangeType = DBGCVAR_RANGE_ELEMENTS;
        Var.u64Range = 256;
    }

    /*
     * Process the arguments.
     */
    for (unsigned i = 0; i < cArgs; i++)
    {
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, i, paArgs[i].enmType == DBGCVAR_TYPE_NUMBER);
        if (paArgs[i].u.u64Number < 256)
        {
            RTGCUINTPTR iInt = (RTGCUINTPTR)paArgs[i].u.u64Number;
            unsigned cInts = paArgs[i].enmRangeType != DBGCVAR_RANGE_NONE
                           ? paArgs[i].u64Range
                           : 1;
            bool fSingle = cInts == 1;
            while (     cInts-- > 0
                   &&   iInt < 256)
            {
                /*
                 * Try read it.
                 */
                union
                {
                    RTFAR16 Real;
                    X86DESC Prot;
                    X86DESC64 Long;
                } u;
                if (iInt * cbEntry  + (cbEntry - 1) > cbLimit)
                {
                    DBGCCmdHlpPrintf(pCmdHlp, "%04x not within the IDT\n", (unsigned)iInt);
                    if (!fAll && !fSingle)
                        return VINF_SUCCESS;
                }
                DBGCVAR AddrVar;
                AddrVar.enmType = DBGCVAR_TYPE_GC_FLAT;
                AddrVar.u.GCFlat = GCFlat + iInt * cbEntry;
                AddrVar.enmRangeType = DBGCVAR_RANGE_NONE;
                rc = pCmdHlp->pfnMemRead(pCmdHlp, &u, cbEntry, &AddrVar, NULL);
                if (RT_FAILURE(rc))
                    return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Reading IDT entry %#04x.\n", (unsigned)iInt);

                /*
                 * Display it.
                 */
                switch (enmMode)
                {
                    case CPUMMODE_REAL:
                    {
                        char *pszSymbol = dbgcCmdHlpFarAddrToSymbol(pCmdHlp, DBGF_AS_GLOBAL, u.Real.sel, u.Real.off, " (", ")");
                        rc = DBGCCmdHlpPrintf(pCmdHlp, "%04x %RTfp16%s\n", (unsigned)iInt, u.Real, pszSymbol ? pszSymbol : "");
                        RTStrFree(pszSymbol);
                        break;
                    }
                    case CPUMMODE_PROTECTED:
                        if (fAll || fSingle || u.Prot.Gen.u1Present)
                            rc = dbgcCmdDumpDTWorker32(pCmdHlp, &u.Prot, iInt, false, DBGF_AS_GLOBAL);
                        break;
                    case CPUMMODE_LONG:
                        if (fAll || fSingle || u.Long.Gen.u1Present)
                            rc = dbgcCmdDumpDTWorker64(pCmdHlp, &u.Long, iInt, false, DBGF_AS_GLOBAL, NULL);
                        break;
                    default: break; /* to shut up gcc */
                }
                if (RT_FAILURE(rc))
                    return rc;

                /* next */
                iInt++;
            }
        }
        else
            DBGCCmdHlpPrintf(pCmdHlp, "error: %llx is out of bounds (max 256)\n", paArgs[i].u.u64Number);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD,
 *      The 'da'\, 'dq'\, 'dqs'\, 'dd'\, 'dds'\, 'dw'\, 'db'\, 'dp'\, 'dps'\,
 *      and 'du' commands.}
 */
static DECLCALLBACK(int) dbgcCmdDumpMem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs <= 1);
    if (cArgs == 1)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISPOINTER(paArgs[0].enmType));
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

#define DBGC_DUMP_MEM_F_ASCII    RT_BIT_32(31)
#define DBGC_DUMP_MEM_F_UNICODE  RT_BIT_32(30)
#define DBGC_DUMP_MEM_F_FAR      RT_BIT_32(29)
#define DBGC_DUMP_MEM_F_SYMBOLS  RT_BIT_32(28)
#define DBGC_DUMP_MEM_F_SIZE     UINT32_C(0x0000ffff)

    /*
     * Figure out the element size.
     */
    unsigned    cbElement;
    bool        fAscii   = false;
    bool        fUnicode = false;
    bool        fFar     = false;
    bool        fSymbols = pCmd->pszCmd[1] && pCmd->pszCmd[2] == 's';
    switch (pCmd->pszCmd[1])
    {
        default:
        case 'b':   cbElement = 1; break;
        case 'w':   cbElement = 2; break;
        case 'd':   cbElement = 4; break;
        case 'q':   cbElement = 8; break;
        case 'a':
            cbElement = 1;
            fAscii = true;
            break;
        case 'F':
            cbElement = 4;
            fFar = true;
            break;
        case 'p':
            cbElement = DBGFR3CpuIsIn64BitCode(pUVM, pDbgc->idCpu) ? 8 : 4;
            break;
        case 'u':
            cbElement = 2;
            fUnicode = true;
            break;
        case '\0':
            fAscii    = RT_BOOL(pDbgc->cbDumpElement & DBGC_DUMP_MEM_F_ASCII);
            fSymbols  = RT_BOOL(pDbgc->cbDumpElement & DBGC_DUMP_MEM_F_SYMBOLS);
            fUnicode  = RT_BOOL(pDbgc->cbDumpElement & DBGC_DUMP_MEM_F_UNICODE);
            fFar      = RT_BOOL(pDbgc->cbDumpElement & DBGC_DUMP_MEM_F_FAR);
            cbElement = pDbgc->cbDumpElement & DBGC_DUMP_MEM_F_SIZE;
            if (!cbElement)
                cbElement = 1;
            break;
    }
    uint32_t const cbDumpElement = cbElement
                                 | (fSymbols ? DBGC_DUMP_MEM_F_SYMBOLS : 0)
                                 | (fFar     ? DBGC_DUMP_MEM_F_FAR     : 0)
                                 | (fUnicode ? DBGC_DUMP_MEM_F_UNICODE : 0)
                                 | (fAscii   ? DBGC_DUMP_MEM_F_ASCII   : 0);
    pDbgc->cbDumpElement = cbDumpElement;

    /*
     * Find address.
     */
    if (!cArgs)
        pDbgc->DumpPos.enmRangeType = DBGCVAR_RANGE_NONE;
    else
        pDbgc->DumpPos = paArgs[0];

    /*
     * Range.
     */
    switch (pDbgc->DumpPos.enmRangeType)
    {
        case DBGCVAR_RANGE_NONE:
            pDbgc->DumpPos.enmRangeType = DBGCVAR_RANGE_BYTES;
            pDbgc->DumpPos.u64Range     = 0x60;
            break;

        case DBGCVAR_RANGE_ELEMENTS:
            if (pDbgc->DumpPos.u64Range > 2048)
                return DBGCCmdHlpPrintf(pCmdHlp, "error: Too many elements requested. Max is 2048 elements.\n");
            pDbgc->DumpPos.enmRangeType = DBGCVAR_RANGE_BYTES;
            pDbgc->DumpPos.u64Range     = (cbElement ? cbElement : 1) * pDbgc->DumpPos.u64Range;
            break;

        case DBGCVAR_RANGE_BYTES:
            if (pDbgc->DumpPos.u64Range > 65536)
                return DBGCCmdHlpPrintf(pCmdHlp, "error: The requested range is too big. Max is 64KB.\n");
            break;

        default:
            return DBGCCmdHlpPrintf(pCmdHlp, "internal error: Unknown range type %d.\n", pDbgc->DumpPos.enmRangeType);
    }

    pDbgc->pLastPos = &pDbgc->DumpPos;

    /*
     * Do the dumping.
     */
    int     cbLeft = (int)pDbgc->DumpPos.u64Range;
    uint8_t u16Prev = '\0';
    for (;;)
    {
        /*
         * Read memory.
         */
        char    achBuffer[16];
        size_t  cbReq = RT_MIN((int)sizeof(achBuffer), cbLeft);
        size_t  cb = RT_MIN((int)sizeof(achBuffer), cbLeft);
        int rc = pCmdHlp->pfnMemRead(pCmdHlp, &achBuffer, cbReq, &pDbgc->DumpPos, &cb);
        if (RT_FAILURE(rc))
        {
            if (u16Prev && u16Prev != '\n')
                DBGCCmdHlpPrintf(pCmdHlp, "\n");
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Reading memory at %DV.\n", &pDbgc->DumpPos);
        }

        /*
         * Display it.
         */
        memset(&achBuffer[cb], 0, sizeof(achBuffer) - cb);
        if (!fAscii && !fUnicode)
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%DV:", &pDbgc->DumpPos);
            unsigned i;
            for (i = 0; i < cb; i += cbElement)
            {
                const char *pszSpace = " ";
                if (cbElement <= 2 && i == 8)
                    pszSpace = "-";
                switch (cbElement)
                {
                    case 1:
                        DBGCCmdHlpPrintf(pCmdHlp, "%s%02x",     pszSpace, *(uint8_t *)&achBuffer[i]);
                        break;
                    case 2:
                        DBGCCmdHlpPrintf(pCmdHlp, "%s%04x",     pszSpace, *(uint16_t *)&achBuffer[i]);
                        break;
                    case 4:
                        if (!fFar)
                            DBGCCmdHlpPrintf(pCmdHlp, "%s%08x", pszSpace, *(uint32_t *)&achBuffer[i]);
                        else
                            DBGCCmdHlpPrintf(pCmdHlp, "%s%04x:%04x:",
                                             pszSpace, *(uint16_t *)&achBuffer[i + 2], *(uint16_t *)&achBuffer[i]);
                        break;
                    case 8:
                        DBGCCmdHlpPrintf(pCmdHlp, "%s%016llx",  pszSpace, *(uint64_t *)&achBuffer[i]);
                        break;
                }

                if (fSymbols)
                {
                    /* Try lookup symbol for the above address. */
                    DBGFADDRESS Addr;
                    rc = VINF_SUCCESS;
                    if (cbElement == 8)
                        DBGFR3AddrFromFlat(pDbgc->pUVM, &Addr, *(uint64_t *)&achBuffer[i]);
                    else if (!fFar)
                        DBGFR3AddrFromFlat(pDbgc->pUVM, &Addr, *(uint32_t *)&achBuffer[i]);
                    else
                        rc = DBGFR3AddrFromSelOff(pDbgc->pUVM, pDbgc->idCpu, &Addr,
                                                  *(uint16_t *)&achBuffer[i + 2], *(uint16_t *)&achBuffer[i]);
                    if (RT_SUCCESS(rc))
                    {
                        RTINTPTR    offDisp;
                        RTDBGSYMBOL Symbol;
                        rc = DBGFR3AsSymbolByAddr(pUVM, pDbgc->hDbgAs, &Addr,
                                                  RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                                  &offDisp, &Symbol, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if (!offDisp)
                                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s", Symbol.szName);
                            else if (offDisp > 0)
                                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s + %RGv", Symbol.szName, offDisp);
                            else
                                rc = DBGCCmdHlpPrintf(pCmdHlp, " %s - %RGv", Symbol.szName, -offDisp);
                            if (   RT_SUCCESS(rc)
                                && Symbol.cb > 0)
                                DBGCCmdHlpPrintf(pCmdHlp, " (LB %RGv)", Symbol.cb);
                        }
                    }

                    /* Next line prefix. */
                    unsigned iNext = i + cbElement;
                    if (iNext < cb)
                    {
                        DBGCVAR TmpPos = pDbgc->DumpPos;
                        DBGCCmdHlpEval(pCmdHlp, &TmpPos, "(%Dv) + %x", &pDbgc->DumpPos, iNext);
                        DBGCCmdHlpPrintf(pCmdHlp, "\n%DV:", &pDbgc->DumpPos);
                    }
                }
            }

            /* Chars column. */
            if (cbElement == 1)
            {
                while (i++ < sizeof(achBuffer))
                    DBGCCmdHlpPrintf(pCmdHlp, "   ");
                DBGCCmdHlpPrintf(pCmdHlp, "  ");
                for (i = 0; i < cb; i += cbElement)
                {
                    uint8_t u8 = *(uint8_t *)&achBuffer[i];
                    if (RT_C_IS_PRINT(u8) && u8 < 127 && u8 >= 32)
                        DBGCCmdHlpPrintf(pCmdHlp, "%c", u8);
                    else
                        DBGCCmdHlpPrintf(pCmdHlp, ".");
                }
            }
            rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            /*
             * We print up to the first zero and stop there.
             * Only printables + '\t' and '\n' are printed.
             */
            if (!u16Prev)
                DBGCCmdHlpPrintf(pCmdHlp, "%DV:\n", &pDbgc->DumpPos);
            uint16_t u16 = '\0';
            unsigned i;
            for (i = 0; i < cb; i += cbElement)
            {
                u16Prev = u16;
                if (cbElement == 1)
                    u16 = *(uint8_t *)&achBuffer[i];
                else
                    u16 = *(uint16_t *)&achBuffer[i];
                if (    u16 < 127
                    && (    (RT_C_IS_PRINT(u16) && u16 >= 32)
                        ||  u16 == '\t'
                        ||  u16 == '\n'))
                    DBGCCmdHlpPrintf(pCmdHlp, "%c", (int)u16);
                else if (!u16)
                    break;
                else
                    DBGCCmdHlpPrintf(pCmdHlp, "\\x%0*x", cbElement * 2, u16);
            }
            if (u16 == '\0')
                cb = cbLeft = i + 1;
            if (cbLeft - cb <= 0 && u16Prev != '\n')
                DBGCCmdHlpPrintf(pCmdHlp, "\n");
        }

        /*
         * Advance
         */
        cbLeft -= (int)cb;
        rc = DBGCCmdHlpEval(pCmdHlp, &pDbgc->DumpPos, "(%Dv) + %x", &pDbgc->DumpPos, cb);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Expression: (%Dv) + %x\n", &pDbgc->DumpPos, cb);
        if (cbLeft <= 0)
            break;
    }

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * Best guess at which paging mode currently applies to the guest
 * paging structures.
 *
 * This have to come up with a decent answer even when the guest
 * is in non-paged protected mode or real mode.
 *
 * @returns cr3.
 * @param   pDbgc   The DBGC instance.
 * @param   pfPAE   Where to store the page address extension indicator.
 * @param   pfLME   Where to store the long mode enabled indicator.
 * @param   pfPSE   Where to store the page size extension indicator.
 * @param   pfPGE   Where to store the page global enabled indicator.
 * @param   pfNXE   Where to store the no-execution enabled indicator.
 */
static RTGCPHYS dbgcGetGuestPageMode(PDBGC pDbgc, bool *pfPAE, bool *pfLME, bool *pfPSE, bool *pfPGE, bool *pfNXE)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    AssertReleaseFailed();
    RT_NOREF(pDbgc, pfPAE, pfLME, pfPSE, pfPGE, pfNXE);
    *pfPAE = *pfLME = *pfPSE = *pfPGE = *pfNXE = false;
    return ~(RTGCPHYS)0;
#else
    PVMCPU      pVCpu = VMMR3GetCpuByIdU(pDbgc->pUVM, pDbgc->idCpu);
    RTGCUINTREG cr4   = CPUMGetGuestCR4(pVCpu);
    *pfPSE = !!(cr4 & X86_CR4_PSE);
    *pfPGE = !!(cr4 & X86_CR4_PGE);
    if (cr4 & X86_CR4_PAE)
    {
        *pfPSE = true;
        *pfPAE = true;
    }
    else
        *pfPAE = false;

    *pfLME = CPUMGetGuestMode(pVCpu) == CPUMMODE_LONG;
    *pfNXE = false; /* GUEST64 GUESTNX */
    return CPUMGetGuestCR3(pVCpu);
#endif
}


/**
 * Determine the shadow paging mode.
 *
 * @returns cr3.
 * @param   pDbgc   The DBGC instance.
 * @param   pfPAE   Where to store the page address extension indicator.
 * @param   pfLME   Where to store the long mode enabled indicator.
 * @param   pfPSE   Where to store the page size extension indicator.
 * @param   pfPGE   Where to store the page global enabled indicator.
 * @param   pfNXE   Where to store the no-execution enabled indicator.
 */
static RTHCPHYS dbgcGetShadowPageMode(PDBGC pDbgc, bool *pfPAE, bool *pfLME, bool *pfPSE, bool *pfPGE, bool *pfNXE)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pDbgc, pfPAE, pfLME, pfPSE, pfPGE, pfNXE);
    AssertReleaseFailed();
    *pfPAE = *pfLME = *pfPSE = *pfPGE = *pfNXE = false;
    return ~(RTHCPHYS)0;
#else
    PVMCPU pVCpu = VMMR3GetCpuByIdU(pDbgc->pUVM, pDbgc->idCpu);

    *pfPSE = true;
    *pfPGE = false;
    switch (PGMGetShadowMode(pVCpu))
    {
        default:
        case PGMMODE_32_BIT:
            *pfPAE = *pfLME = *pfNXE = false;
            break;
        case PGMMODE_PAE:
            *pfLME = *pfNXE = false;
            *pfPAE = true;
            break;
        case PGMMODE_PAE_NX:
            *pfLME = false;
            *pfPAE = *pfNXE = true;
            break;
        case PGMMODE_AMD64:
            *pfNXE = false;
            *pfPAE = *pfLME = true;
            break;
        case PGMMODE_AMD64_NX:
            *pfPAE = *pfLME = *pfNXE = true;
            break;
    }
    return PGMGetHyperCR3(pVCpu);
#endif
}


/**
 * @callback_method_impl{FNDBGCCMD,
 *      The 'dpd'\, 'dpda'\, 'dpdb'\, 'dpdg' and 'dpdh' commands.}
 */
static DECLCALLBACK(int) dbgcCmdDumpPageDir(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs <= 1);
    if (cArgs == 1 && pCmd->pszCmd[3] == 'a')
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISPOINTER(paArgs[0].enmType));
    if (cArgs == 1 && pCmd->pszCmd[3] != 'a')
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0,    paArgs[0].enmType == DBGCVAR_TYPE_NUMBER
                                                        || DBGCVAR_ISPOINTER(paArgs[0].enmType));
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Guest or shadow page directories? Get the paging parameters.
     */
    bool fGuest = pCmd->pszCmd[3] != 'h';
    if (!pCmd->pszCmd[3] || pCmd->pszCmd[3] == 'a')
        fGuest = paArgs[0].enmType == DBGCVAR_TYPE_NUMBER ? true : DBGCVAR_ISGCPOINTER(paArgs[0].enmType);

    bool fPAE, fLME, fPSE, fPGE, fNXE;
    uint64_t cr3 = fGuest
                 ? dbgcGetGuestPageMode(pDbgc, &fPAE, &fLME, &fPSE, &fPGE, &fNXE)
                 : dbgcGetShadowPageMode(pDbgc, &fPAE, &fLME, &fPSE, &fPGE, &fNXE);
    const unsigned cbEntry = fPAE ? sizeof(X86PTEPAE) : sizeof(X86PTE);

    /*
     * Setup default argument if none was specified.
     * Fix address / index confusion.
     */
    DBGCVAR VarDefault;
    if (!cArgs)
    {
        if (pCmd->pszCmd[3] == 'a')
        {
            if (fLME || fPAE)
                return DBGCCmdHlpPrintf(pCmdHlp, "Default argument for 'dpda' hasn't been fully implemented yet. Try with an address or use one of the other commands.\n");
            if (fGuest)
                DBGCVAR_INIT_GC_PHYS(&VarDefault, cr3);
            else
                DBGCVAR_INIT_HC_PHYS(&VarDefault, cr3);
        }
        else
            DBGCVAR_INIT_GC_FLAT(&VarDefault, 0);
        paArgs = &VarDefault;
        //cArgs = 1; Unused
    }
    else if (paArgs[0].enmType == DBGCVAR_TYPE_NUMBER)
    {
        /* If it's a number (not an address), it's an index, so convert it to an address. */
        Assert(pCmd->pszCmd[3] != 'a');
        VarDefault = paArgs[0];
        if (fPAE)
            return DBGCCmdHlpPrintf(pCmdHlp, "PDE indexing is only implemented for 32-bit paging.\n");
        if (VarDefault.u.u64Number >= GUEST_PAGE_SIZE / cbEntry)
            return DBGCCmdHlpPrintf(pCmdHlp, "PDE index is out of range [0..%d].\n", GUEST_PAGE_SIZE / cbEntry - 1);
        VarDefault.u.u64Number <<= X86_PD_SHIFT;
        VarDefault.enmType = DBGCVAR_TYPE_GC_FLAT;
        paArgs = &VarDefault;
    }

    /*
     * Locate the PDE to start displaying at.
     *
     * The 'dpda' command takes the address of a PDE, while the others are guest
     * virtual address which PDEs should be displayed. So, 'dpda' is rather simple
     * while the others require us to do all the tedious walking thru the paging
     * hierarchy to find the intended PDE.
     */
    unsigned    iEntry = ~0U;           /* The page directory index. ~0U for 'dpta'. */
    DBGCVAR     VarGCPtr = { NULL, };   /* The GC address corresponding to the current PDE (iEntry != ~0U). */
    DBGCVAR     VarPDEAddr;             /* The address of the current PDE. */
    unsigned    cEntries;               /* The number of entries to display. */
    unsigned    cEntriesMax;            /* The max number of entries to display. */
    int         rc;
    if (pCmd->pszCmd[3] == 'a')
    {
        VarPDEAddr = paArgs[0];
        switch (VarPDEAddr.enmRangeType)
        {
            case DBGCVAR_RANGE_BYTES:       cEntries = VarPDEAddr.u64Range / cbEntry; break;
            case DBGCVAR_RANGE_ELEMENTS:    cEntries = VarPDEAddr.u64Range; break;
            default:                        cEntries = 10; break;
        }
        cEntriesMax = GUEST_PAGE_SIZE / cbEntry;
    }
    else
    {
        /*
         * Determine the range.
         */
        switch (paArgs[0].enmRangeType)
        {
            case DBGCVAR_RANGE_BYTES:       cEntries = paArgs[0].u64Range / GUEST_PAGE_SIZE; break;
            case DBGCVAR_RANGE_ELEMENTS:    cEntries = paArgs[0].u64Range; break;
            default:                        cEntries = 10; break;
        }

        /*
         * Normalize the input address, it must be a flat GC address.
         */
        rc = DBGCCmdHlpEval(pCmdHlp, &VarGCPtr, "%%(%Dv)", &paArgs[0]);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "%%(%Dv)", &paArgs[0]);
        if (VarGCPtr.enmType == DBGCVAR_TYPE_HC_FLAT)
        {
            VarGCPtr.u.GCFlat = (uintptr_t)VarGCPtr.u.pvHCFlat;
            VarGCPtr.enmType = DBGCVAR_TYPE_GC_FLAT;
        }
        if (fPAE)
            VarGCPtr.u.GCFlat &= ~(((RTGCPTR)1 << X86_PD_PAE_SHIFT) - 1);
        else
            VarGCPtr.u.GCFlat &= ~(((RTGCPTR)1 << X86_PD_SHIFT) - 1);

        /*
         * Do the paging walk until we get to the page directory.
         */
        DBGCVAR VarCur;
        if (fGuest)
            DBGCVAR_INIT_GC_PHYS(&VarCur, cr3);
        else
            DBGCVAR_INIT_HC_PHYS(&VarCur, cr3);
        if (fLME)
        {
            /* Page Map Level 4 Lookup. */
            /* Check if it's a valid address first? */
            VarCur.u.u64Number &= X86_PTE_PAE_PG_MASK;
            VarCur.u.u64Number += (((uint64_t)VarGCPtr.u.GCFlat >> X86_PML4_SHIFT) & X86_PML4_MASK) * sizeof(X86PML4E);
            X86PML4E Pml4e;
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pml4e, sizeof(Pml4e), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PML4E memory at %DV.\n", &VarCur);
            if (!Pml4e.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page directory pointer table is not present for %Dv.\n", &VarGCPtr);

            VarCur.u.u64Number = Pml4e.u & X86_PML4E_PG_MASK;
            Assert(fPAE);
        }
        if (fPAE)
        {
            /* Page directory pointer table. */
            X86PDPE Pdpe;
            VarCur.u.u64Number += ((VarGCPtr.u.GCFlat >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE) * sizeof(Pdpe);
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pdpe, sizeof(Pdpe), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PDPE memory at %DV.\n", &VarCur);
            if (!Pdpe.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page directory is not present for %Dv.\n", &VarGCPtr);

            iEntry = (VarGCPtr.u.GCFlat >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
            VarPDEAddr = VarCur;
            VarPDEAddr.u.u64Number = Pdpe.u & X86_PDPE_PG_MASK;
            VarPDEAddr.u.u64Number += iEntry * sizeof(X86PDEPAE);
        }
        else
        {
            /* 32-bit legacy - CR3 == page directory. */
            iEntry = (VarGCPtr.u.GCFlat >> X86_PD_SHIFT) & X86_PD_MASK;
            VarPDEAddr = VarCur;
            VarPDEAddr.u.u64Number += iEntry * sizeof(X86PDE);
        }
        cEntriesMax = (GUEST_PAGE_SIZE - iEntry) / cbEntry;
    }

    /* adjust cEntries */
    cEntries = RT_MAX(1, cEntries);
    cEntries = RT_MIN(cEntries, cEntriesMax);

    /*
     * The display loop.
     */
    DBGCCmdHlpPrintf(pCmdHlp, iEntry != ~0U ? "%DV (index %#x):\n" : "%DV:\n",
                     &VarPDEAddr, iEntry);
    do
    {
        /*
         * Read.
         */
        X86PDEPAE Pde;
        Pde.u = 0;
        rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pde, cbEntry, &VarPDEAddr, NULL);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Reading PDE memory at %DV.\n", &VarPDEAddr);

        /*
         * Display.
         */
        if (iEntry != ~0U)
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%03x %DV: ", iEntry, &VarGCPtr);
            iEntry++;
        }
        if (fPSE && Pde.b.u1Size)
            DBGCCmdHlpPrintf(pCmdHlp,
                             fPAE
                             ? "%016llx big phys=%016llx %s %s %s %s %s avl=%02x %s %s %s %s %s"
                             :   "%08llx big phys=%08llx %s %s %s %s %s avl=%02x %s %s %s %s %s",
                             Pde.u,
                             Pde.u & X86_PDE_PAE_PG_MASK,
                             Pde.b.u1Present        ? "p "  : "np",
                             Pde.b.u1Write          ? "w"   : "r",
                             Pde.b.u1User           ? "u"   : "s",
                             Pde.b.u1Accessed       ? "a "  : "na",
                             Pde.b.u1Dirty          ? "d "  : "nd",
                             Pde.b.u3Available,
                             Pde.b.u1Global         ? (fPGE ? "g" : "G") : " ",
                             Pde.b.u1WriteThru      ? "pwt" : "   ",
                             Pde.b.u1CacheDisable   ? "pcd" : "   ",
                             Pde.b.u1PAT            ? "pat" : "",
                             Pde.b.u1NoExecute      ? (fNXE ? "nx" : "NX") : "  ");
        else
            DBGCCmdHlpPrintf(pCmdHlp,
                             fPAE
                             ? "%016llx 4kb phys=%016llx %s %s %s %s %s avl=%02x %s %s %s %s"
                             :   "%08llx 4kb phys=%08llx %s %s %s %s %s avl=%02x %s %s %s %s",
                             Pde.u,
                             Pde.u & X86_PDE_PAE_PG_MASK,
                             Pde.n.u1Present        ? "p "  : "np",
                             Pde.n.u1Write          ? "w"   : "r",
                             Pde.n.u1User           ? "u"   : "s",
                             Pde.n.u1Accessed       ? "a "  : "na",
                             Pde.u & RT_BIT(6)      ? "6 "  : "  ",
                             Pde.n.u3Available,
                             Pde.u & RT_BIT(8)      ? "8"   : " ",
                             Pde.n.u1WriteThru      ? "pwt" : "   ",
                             Pde.n.u1CacheDisable   ? "pcd" : "   ",
                             Pde.u & RT_BIT(7)      ? "7"   : "",
                             Pde.n.u1NoExecute      ? (fNXE ? "nx" : "NX") : "  ");
        if (Pde.u & UINT64_C(0x7fff000000000000))
            DBGCCmdHlpPrintf(pCmdHlp, " weird=%RX64", (Pde.u & UINT64_C(0x7fff000000000000)));
        rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Advance.
         */
        VarPDEAddr.u.u64Number += cbEntry;
        if (iEntry != ~0U)
            VarGCPtr.u.GCFlat += fPAE ? RT_BIT_32(X86_PD_PAE_SHIFT) : RT_BIT_32(X86_PD_SHIFT);
    } while (cEntries-- > 0);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dpdb' command.}
 */
static DECLCALLBACK(int) dbgcCmdDumpPageDirBoth(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    int rc1 = pCmdHlp->pfnExec(pCmdHlp, "dpdg %DV", &paArgs[0]);
    int rc2 = pCmdHlp->pfnExec(pCmdHlp, "dpdh %DV", &paArgs[0]);
    if (RT_FAILURE(rc1))
        return rc1;
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    return rc2;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dph*' commands and main part of 'm'.}
 */
static DECLCALLBACK(int) dbgcCmdDumpPageHierarchy(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Figure the context and base flags.
     */
    uint32_t fFlags = DBGFPGDMP_FLAGS_PAGE_INFO | DBGFPGDMP_FLAGS_PRINT_CR3;
    if (pCmd->pszCmd[0] == 'm')
        fFlags |= DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_SHADOW;
    else if (pCmd->pszCmd[3] == '\0')
        fFlags |= DBGFPGDMP_FLAGS_GUEST;
    else if (pCmd->pszCmd[3] == 'g')
        fFlags |= DBGFPGDMP_FLAGS_GUEST;
    else if (pCmd->pszCmd[3] == 'h')
        fFlags |= DBGFPGDMP_FLAGS_SHADOW;
    else
        AssertFailed();

    if (pDbgc->cPagingHierarchyDumps == 0)
        fFlags |= DBGFPGDMP_FLAGS_HEADER;
    pDbgc->cPagingHierarchyDumps = (pDbgc->cPagingHierarchyDumps + 1) % 42;

    /*
     * Get the range.
     */
    PCDBGCVAR   pRange = cArgs > 0 ? &paArgs[0] : pDbgc->pLastPos;
    RTGCPTR     GCPtrFirst = NIL_RTGCPTR;
    int rc = DBGCCmdHlpVarToFlatAddr(pCmdHlp, pRange, &GCPtrFirst);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to convert %DV to a flat address: %Rrc", pRange, rc);

    uint64_t cbRange;
    rc = DBGCCmdHlpVarGetRange(pCmdHlp, pRange, GUEST_PAGE_SIZE, GUEST_PAGE_SIZE * 8, &cbRange);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to obtain the range of %DV: %Rrc", pRange, rc);

    RTGCPTR GCPtrLast = RTGCPTR_MAX - GCPtrFirst;
    if (cbRange >= GCPtrLast)
        GCPtrLast = RTGCPTR_MAX;
    else if (!cbRange)
        GCPtrLast = GCPtrFirst;
    else
        GCPtrLast = GCPtrFirst + cbRange - 1;

    /*
     * Do we have a CR3?
     */
    uint64_t cr3 = 0;
    if (cArgs > 1)
    {
        if ((fFlags & (DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_SHADOW)) == (DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_SHADOW))
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "No CR3 or mode arguments when dumping both context, please.");
        if (paArgs[1].enmType != DBGCVAR_TYPE_NUMBER)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "The CR3 argument is not a number: %DV", &paArgs[1]);
        cr3 = paArgs[1].u.u64Number;
    }
    else
        fFlags |= DBGFPGDMP_FLAGS_CURRENT_CR3;

    /*
     * Do we have a mode?
     */
    if (cArgs > 2)
    {
        if (paArgs[2].enmType != DBGCVAR_TYPE_STRING)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "The mode argument is not a string: %DV", &paArgs[2]);
        static const struct MODETOFLAGS
        {
            const char *pszName;
            uint32_t    fFlags;
        } s_aModeToFlags[] =
        {
            { "ept",        DBGFPGDMP_FLAGS_EPT },
            { "legacy",     0 },
            { "legacy-np",  DBGFPGDMP_FLAGS_NP },
            { "pse",        DBGFPGDMP_FLAGS_PSE },
            { "pse-np",     DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_NP },
            { "pae",        DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE },
            { "pae-np",     DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_NP },
            { "pae-nx",     DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_NXE },
            { "pae-nx-np",  DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_NXE | DBGFPGDMP_FLAGS_NP },
            { "long",       DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME },
            { "long-np",    DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_NP },
            { "long-nx",    DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_NXE },
            { "long-nx-np", DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_NXE | DBGFPGDMP_FLAGS_NP }
        };
        int i = RT_ELEMENTS(s_aModeToFlags);
        while (i-- > 0)
            if (!strcmp(s_aModeToFlags[i].pszName, paArgs[2].u.pszString))
            {
                fFlags |= s_aModeToFlags[i].fFlags;
                break;
            }
        if (i < 0)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Unknown mode: \"%s\"", paArgs[2].u.pszString);
    }
    else
        fFlags |= DBGFPGDMP_FLAGS_CURRENT_MODE;

    /*
     * Call the worker.
     */
    rc = DBGFR3PagingDumpEx(pUVM, pDbgc->idCpu, fFlags, cr3, GCPtrFirst, GCPtrLast, 99 /*cMaxDepth*/,
                            DBGCCmdHlpGetDbgfOutputHlp(pCmdHlp));
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "DBGFR3PagingDumpEx: %Rrc\n", rc);
    return VINF_SUCCESS;
}



/**
 * @callback_method_impl{FNDBGCCMD, The 'dpg*' commands.}
 */
static DECLCALLBACK(int) dbgcCmdDumpPageTable(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 1);
    if (pCmd->pszCmd[3] == 'a')
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISPOINTER(paArgs[0].enmType));
    else
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0,    paArgs[0].enmType == DBGCVAR_TYPE_NUMBER
                                                        || DBGCVAR_ISPOINTER(paArgs[0].enmType));
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Guest or shadow page tables? Get the paging parameters.
     */
    bool fGuest = pCmd->pszCmd[3] != 'h';
    if (!pCmd->pszCmd[3] || pCmd->pszCmd[3] == 'a')
        fGuest = paArgs[0].enmType == DBGCVAR_TYPE_NUMBER ? true : DBGCVAR_ISGCPOINTER(paArgs[0].enmType);

    bool fPAE, fLME, fPSE, fPGE, fNXE;
    uint64_t cr3 = fGuest
                 ? dbgcGetGuestPageMode(pDbgc, &fPAE, &fLME, &fPSE, &fPGE, &fNXE)
                 : dbgcGetShadowPageMode(pDbgc, &fPAE, &fLME, &fPSE, &fPGE, &fNXE);
    const unsigned cbEntry = fPAE ? sizeof(X86PTEPAE) : sizeof(X86PTE);

    /*
     * Locate the PTE to start displaying at.
     *
     * The 'dpta' command takes the address of a PTE, while the others are guest
     * virtual address which PTEs should be displayed. So, 'pdta' is rather simple
     * while the others require us to do all the tedious walking thru the paging
     * hierarchy to find the intended PTE.
     */
    unsigned    iEntry = ~0U;           /* The page table index. ~0U for 'dpta'. */
    DBGCVAR     VarGCPtr;               /* The GC address corresponding to the current PTE (iEntry != ~0U). */
    DBGCVAR     VarPTEAddr;             /* The address of the current PTE. */
    unsigned    cEntries;               /* The number of entries to display. */
    unsigned    cEntriesMax;            /* The max number of entries to display. */
    int         rc;
    if (pCmd->pszCmd[3] == 'a')
    {
        VarPTEAddr = paArgs[0];
        switch (VarPTEAddr.enmRangeType)
        {
            case DBGCVAR_RANGE_BYTES:       cEntries = VarPTEAddr.u64Range / cbEntry; break;
            case DBGCVAR_RANGE_ELEMENTS:    cEntries = VarPTEAddr.u64Range; break;
            default:                        cEntries = 10; break;
        }
        cEntriesMax = GUEST_PAGE_SIZE / cbEntry;
    }
    else
    {
        /*
         * Determine the range.
         */
        switch (paArgs[0].enmRangeType)
        {
            case DBGCVAR_RANGE_BYTES:       cEntries = paArgs[0].u64Range / GUEST_PAGE_SIZE; break;
            case DBGCVAR_RANGE_ELEMENTS:    cEntries = paArgs[0].u64Range; break;
            default:                        cEntries = 10; break;
        }

        /*
         * Normalize the input address, it must be a flat GC address.
         */
        rc = DBGCCmdHlpEval(pCmdHlp, &VarGCPtr, "%%(%Dv)", &paArgs[0]);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "%%(%Dv)", &paArgs[0]);
        if (VarGCPtr.enmType == DBGCVAR_TYPE_HC_FLAT)
        {
            VarGCPtr.u.GCFlat = (uintptr_t)VarGCPtr.u.pvHCFlat;
            VarGCPtr.enmType = DBGCVAR_TYPE_GC_FLAT;
        }
        VarGCPtr.u.GCFlat &= ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;

        /*
         * Do the paging walk until we get to the page table.
         */
        DBGCVAR VarCur;
        if (fGuest)
            DBGCVAR_INIT_GC_PHYS(&VarCur, cr3);
        else
            DBGCVAR_INIT_HC_PHYS(&VarCur, cr3);
        if (fLME)
        {
            /* Page Map Level 4 Lookup. */
            /* Check if it's a valid address first? */
            VarCur.u.u64Number &= X86_PTE_PAE_PG_MASK;
            VarCur.u.u64Number += (((uint64_t)VarGCPtr.u.GCFlat >> X86_PML4_SHIFT) & X86_PML4_MASK) * sizeof(X86PML4E);
            X86PML4E Pml4e;
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pml4e, sizeof(Pml4e), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PML4E memory at %DV.\n", &VarCur);
            if (!Pml4e.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page directory pointer table is not present for %Dv.\n", &VarGCPtr);

            VarCur.u.u64Number = Pml4e.u & X86_PML4E_PG_MASK;
            Assert(fPAE);
        }
        if (fPAE)
        {
            /* Page directory pointer table. */
            X86PDPE Pdpe;
            VarCur.u.u64Number += ((VarGCPtr.u.GCFlat >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE) * sizeof(Pdpe);
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pdpe, sizeof(Pdpe), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PDPE memory at %DV.\n", &VarCur);
            if (!Pdpe.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page directory is not present for %Dv.\n", &VarGCPtr);

            VarCur.u.u64Number = Pdpe.u & X86_PDPE_PG_MASK;

            /* Page directory (PAE). */
            X86PDEPAE Pde;
            VarCur.u.u64Number += ((VarGCPtr.u.GCFlat >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK) * sizeof(Pde);
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pde, sizeof(Pde), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PDE memory at %DV.\n", &VarCur);
            if (!Pde.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page table is not present for %Dv.\n", &VarGCPtr);
            if (fPSE && Pde.n.u1Size)
                return pCmdHlp->pfnExec(pCmdHlp, "dpd%s %Dv L3", &pCmd->pszCmd[3], &VarGCPtr);

            iEntry = (VarGCPtr.u.GCFlat >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK;
            VarPTEAddr = VarCur;
            VarPTEAddr.u.u64Number = Pde.u & X86_PDE_PAE_PG_MASK;
            VarPTEAddr.u.u64Number += iEntry * sizeof(X86PTEPAE);
        }
        else
        {
            /* Page directory (legacy). */
            X86PDE Pde;
            VarCur.u.u64Number += ((VarGCPtr.u.GCFlat >> X86_PD_SHIFT) & X86_PD_MASK) * sizeof(Pde);
            rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pde, sizeof(Pde), &VarCur, NULL);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PDE memory at %DV.\n", &VarCur);
            if (!Pde.n.u1Present)
                return DBGCCmdHlpPrintf(pCmdHlp, "Page table is not present for %Dv.\n", &VarGCPtr);
            if (fPSE && Pde.n.u1Size)
                return pCmdHlp->pfnExec(pCmdHlp, "dpd%s %Dv L3", &pCmd->pszCmd[3], &VarGCPtr);

            iEntry = (VarGCPtr.u.GCFlat >> X86_PT_SHIFT) & X86_PT_MASK;
            VarPTEAddr = VarCur;
            VarPTEAddr.u.u64Number = Pde.u & X86_PDE_PG_MASK;
            VarPTEAddr.u.u64Number += iEntry * sizeof(X86PTE);
        }
        cEntriesMax = (GUEST_PAGE_SIZE - iEntry) / cbEntry;
    }

    /* adjust cEntries */
    cEntries = RT_MAX(1, cEntries);
    cEntries = RT_MIN(cEntries, cEntriesMax);

    /*
     * The display loop.
     */
    DBGCCmdHlpPrintf(pCmdHlp, iEntry != ~0U ? "%DV (base %DV / index %#x):\n" : "%DV:\n",
                     &VarPTEAddr, &VarGCPtr, iEntry);
    do
    {
        /*
         * Read.
         */
        X86PTEPAE Pte;
        Pte.u = 0;
        rc = pCmdHlp->pfnMemRead(pCmdHlp, &Pte, cbEntry, &VarPTEAddr, NULL);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Reading PTE memory at %DV.\n", &VarPTEAddr);

        /*
         * Display.
         */
        if (iEntry != ~0U)
        {
            DBGCCmdHlpPrintf(pCmdHlp, "%03x %DV: ", iEntry, &VarGCPtr);
            iEntry++;
        }
        DBGCCmdHlpPrintf(pCmdHlp,
                         fPAE
                         ? "%016llx 4kb phys=%016llx %s %s %s %s %s avl=%02x %s %s %s %s %s"
                         :   "%08llx 4kb phys=%08llx %s %s %s %s %s avl=%02x %s %s %s %s %s",
                         Pte.u,
                         Pte.u & X86_PTE_PAE_PG_MASK,
                         Pte.n.u1Present         ? "p " : "np",
                         Pte.n.u1Write           ? "w" : "r",
                         Pte.n.u1User            ? "u" : "s",
                         Pte.n.u1Accessed        ? "a " : "na",
                         Pte.n.u1Dirty           ? "d " : "nd",
                         Pte.n.u3Available,
                         Pte.n.u1Global          ? (fPGE ? "g" : "G") : " ",
                         Pte.n.u1WriteThru       ? "pwt" : "   ",
                         Pte.n.u1CacheDisable    ? "pcd" : "   ",
                         Pte.n.u1PAT             ? "pat" : "   ",
                         Pte.n.u1NoExecute       ? (fNXE ? "nx" : "NX") : "  "
                         );
        if (Pte.u & UINT64_C(0x7fff000000000000))
            DBGCCmdHlpPrintf(pCmdHlp, " weird=%RX64", (Pte.u & UINT64_C(0x7fff000000000000)));
        rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Advance.
         */
        VarPTEAddr.u.u64Number += cbEntry;
        if (iEntry != ~0U)
            VarGCPtr.u.GCFlat += GUEST_PAGE_SIZE;
    } while (cEntries-- > 0);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dptb' command.}
 */
static DECLCALLBACK(int) dbgcCmdDumpPageTableBoth(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    int rc1 = pCmdHlp->pfnExec(pCmdHlp, "dptg %DV", &paArgs[0]);
    int rc2 = pCmdHlp->pfnExec(pCmdHlp, "dpth %DV", &paArgs[0]);
    if (RT_FAILURE(rc1))
        return rc1;
    NOREF(pCmd); NOREF(cArgs);
    return rc2;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dt' command.}
 */
static DECLCALLBACK(int) dbgcCmdDumpTSS(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int   rc;

    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs <= 1);
    if (cArgs == 1)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0,    paArgs[0].enmType != DBGCVAR_TYPE_STRING
                                                        && paArgs[0].enmType != DBGCVAR_TYPE_SYMBOL);

    /*
     * Check if the command indicates the type.
     */
    enum { kTss16, kTss32, kTss64, kTssToBeDetermined } enmTssType = kTssToBeDetermined;
    if (!strcmp(pCmd->pszCmd, "dt16"))
        enmTssType = kTss16;
    else if (!strcmp(pCmd->pszCmd, "dt32"))
        enmTssType = kTss32;
    else if (!strcmp(pCmd->pszCmd, "dt64"))
        enmTssType = kTss64;

    /*
     * We can get a TSS selector (number), a far pointer using a TSS selector, or some kind of TSS pointer.
     */
    uint32_t SelTss = UINT32_MAX;
    DBGCVAR  VarTssAddr;
    if (cArgs == 0)
    {
        /** @todo consider querying the hidden bits instead (missing API). */
        uint16_t SelTR;
        rc = DBGFR3RegCpuQueryU16(pUVM, pDbgc->idCpu, DBGFREG_TR, &SelTR);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to query TR, rc=%Rrc\n", rc);
        DBGCVAR_INIT_GC_FAR(&VarTssAddr, SelTR, 0);
        SelTss = SelTR;
    }
    else if (paArgs[0].enmType == DBGCVAR_TYPE_NUMBER)
    {
        if (paArgs[0].u.u64Number < 0xffff)
            DBGCVAR_INIT_GC_FAR(&VarTssAddr, (RTSEL)paArgs[0].u.u64Number, 0);
        else
        {
            if (paArgs[0].enmRangeType == DBGCVAR_RANGE_ELEMENTS)
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "Element count doesn't combine with a TSS address.\n");
            DBGCVAR_INIT_GC_FLAT(&VarTssAddr, paArgs[0].u.u64Number);
            if (paArgs[0].enmRangeType == DBGCVAR_RANGE_BYTES)
            {
                VarTssAddr.enmRangeType = paArgs[0].enmRangeType;
                VarTssAddr.u64Range     = paArgs[0].u64Range;
            }
        }
    }
    else
        VarTssAddr = paArgs[0];

    /*
     * Deal with TSS:ign by means of the GDT.
     */
    if (VarTssAddr.enmType == DBGCVAR_TYPE_GC_FAR)
    {
        SelTss = VarTssAddr.u.GCFar.sel;
        DBGFSELINFO SelInfo;
        rc = DBGFR3SelQueryInfo(pUVM, pDbgc->idCpu, VarTssAddr.u.GCFar.sel, DBGFSELQI_FLAGS_DT_GUEST, &SelInfo);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "DBGFR3SelQueryInfo(,%u,%d,,) -> %Rrc.\n",
                                  pDbgc->idCpu, VarTssAddr.u.GCFar.sel, rc);

        if (SelInfo.u.Raw.Gen.u1DescType)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "%04x is not a TSS selector. (!sys)\n", VarTssAddr.u.GCFar.sel);

        switch (SelInfo.u.Raw.Gen.u4Type)
        {
            case X86_SEL_TYPE_SYS_286_TSS_BUSY:
            case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
                if (enmTssType == kTssToBeDetermined)
                    enmTssType = kTss16;
                break;

            case X86_SEL_TYPE_SYS_386_TSS_BUSY:  /* AMD64 too */
            case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
                if (enmTssType == kTssToBeDetermined)
                    enmTssType = SelInfo.fFlags & DBGFSELINFO_FLAGS_LONG_MODE ? kTss64 : kTss32;
                break;

            default:
                return DBGCCmdHlpFail(pCmdHlp, pCmd, "%04x is not a TSS selector. (type=%x)\n",
                                      VarTssAddr.u.GCFar.sel, SelInfo.u.Raw.Gen.u4Type);
        }

        DBGCVAR_INIT_GC_FLAT(&VarTssAddr, SelInfo.GCPtrBase);
        DBGCVAR_SET_RANGE(&VarTssAddr, DBGCVAR_RANGE_BYTES, RT_MAX(SelInfo.cbLimit + 1, SelInfo.cbLimit));
    }

    /*
     * Determine the TSS type if none is currently given.
     */
    if (enmTssType == kTssToBeDetermined)
    {
        if (    VarTssAddr.u64Range > 0
            &&  VarTssAddr.u64Range < sizeof(X86TSS32) - 4)
            enmTssType = kTss16;
        else
        {
            uint64_t uEfer;
            rc = DBGFR3RegCpuQueryU64(pUVM, pDbgc->idCpu, DBGFREG_MSR_K6_EFER, &uEfer);
            if (   RT_FAILURE(rc)
                || !(uEfer &  MSR_K6_EFER_LMA) )
                enmTssType = kTss32;
            else
                enmTssType = kTss64;
        }
    }

    /*
     * Figure the min/max sizes.
     * ASSUMES max TSS size is 64 KB.
     */
    uint32_t cbTssMin;
    uint32_t cbTssMax;
    switch (enmTssType)
    {
        case kTss16:
            cbTssMin = cbTssMax = X86_SEL_TYPE_SYS_286_TSS_LIMIT_MIN + 1;
            break;
        case kTss32:
            cbTssMin = X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN + 1;
            cbTssMax = _64K;
            break;
        case kTss64:
            cbTssMin = X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN + 1;
            cbTssMax = _64K;
            break;
        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR);
    }
    uint32_t cbTss = VarTssAddr.enmRangeType == DBGCVAR_RANGE_BYTES ? (uint32_t)VarTssAddr.u64Range : 0;
    if (cbTss == 0)
        cbTss = cbTssMin;
    else if (cbTss < cbTssMin)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Minimum TSS size is %u bytes, you specified %llu (%llx) bytes.\n",
                              cbTssMin, VarTssAddr.u64Range, VarTssAddr.u64Range);
    else if (cbTss > cbTssMax)
        cbTss = cbTssMax;
    DBGCVAR_SET_RANGE(&VarTssAddr, DBGCVAR_RANGE_BYTES, cbTss);

    /*
     * Read the TSS into a temporary buffer.
     */
    uint8_t  abBuf[_64K];
    size_t   cbTssRead;
    rc = DBGCCmdHlpMemRead(pCmdHlp, abBuf, cbTss, &VarTssAddr, &cbTssRead);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to read TSS at %Dv: %Rrc\n", &VarTssAddr, rc);
    if (cbTssRead < cbTssMin)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to read essential parts of the TSS (read %zu, min %zu).\n",
                              cbTssRead, cbTssMin);
    if (cbTssRead < cbTss)
        memset(&abBuf[cbTssRead], 0xff, cbTss - cbTssRead);


    /*
     * Format the TSS.
     */
    uint16_t offIoBitmap;
    switch (enmTssType)
    {
        case kTss16:
        {
            PCX86TSS16 pTss = (PCX86TSS16)&abBuf[0];
            if (SelTss != UINT32_MAX)
                DBGCCmdHlpPrintf(pCmdHlp, "%04x TSS16 at %Dv\n", SelTss, &VarTssAddr);
            else
                DBGCCmdHlpPrintf(pCmdHlp, "TSS16 at %Dv\n", &VarTssAddr);
            DBGCCmdHlpPrintf(pCmdHlp,
                             "ax=%04x bx=%04x cx=%04x dx=%04x si=%04x di=%04x\n"
                             "ip=%04x sp=%04x bp=%04x\n"
                             "cs=%04x ss=%04x ds=%04x es=%04x      flags=%04x\n"
                             "ss:sp0=%04x:%04x ss:sp1=%04x:%04x ss:sp2=%04x:%04x\n"
                             "prev=%04x ldtr=%04x\n"
                             ,
                             pTss->ax, pTss->bx, pTss->cx, pTss->dx, pTss->si, pTss->di,
                             pTss->ip, pTss->sp, pTss->bp,
                             pTss->cs, pTss->ss, pTss->ds, pTss->es, pTss->flags,
                             pTss->ss0, pTss->sp0,  pTss->ss1, pTss->sp1,  pTss->ss2, pTss->sp2,
                             pTss->selPrev, pTss->selLdt);
            if (pTss->cs != 0)
                pCmdHlp->pfnExec(pCmdHlp, "u %04x:%04x L 0", pTss->cs, pTss->ip);
            offIoBitmap = 0;
            break;
        }

        case kTss32:
        {
            PCX86TSS32 pTss = (PCX86TSS32)&abBuf[0];
            if (SelTss != UINT32_MAX)
                DBGCCmdHlpPrintf(pCmdHlp, "%04x TSS32 at %Dv (min=%04x)\n", SelTss, &VarTssAddr, cbTssMin);
            else
                DBGCCmdHlpPrintf(pCmdHlp, "TSS32 at %Dv  (min=%04x)\n", &VarTssAddr, cbTssMin);
            DBGCCmdHlpPrintf(pCmdHlp,
                             "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n"
                             "eip=%08x esp=%08x ebp=%08x\n"
                             "cs=%04x  ss=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x         eflags=%08x\n"
                             "ss:esp0=%04x:%08x ss:esp1=%04x:%08x ss:esp2=%04x:%08x\n"
                             "prev=%04x ldtr=%04x cr3=%08x debug=%u iomap=%04x\n"
                             ,
                             pTss->eax, pTss->ebx, pTss->ecx, pTss->edx, pTss->esi, pTss->edi,
                             pTss->eip, pTss->esp, pTss->ebp,
                             pTss->cs, pTss->ss, pTss->ds, pTss->es, pTss->fs, pTss->gs, pTss->eflags,
                             pTss->ss0, pTss->esp0,  pTss->ss1, pTss->esp1,  pTss->ss2, pTss->esp2,
                             pTss->selPrev, pTss->selLdt, pTss->cr3, pTss->fDebugTrap, pTss->offIoBitmap);
            if (pTss->cs != 0)
                pCmdHlp->pfnExec(pCmdHlp, "u %04x:%08x L 0", pTss->cs, pTss->eip);
            offIoBitmap = pTss->offIoBitmap;
            break;
        }

        case kTss64:
        {
            PCX86TSS64 pTss = (PCX86TSS64)&abBuf[0];
            if (SelTss != UINT32_MAX)
                DBGCCmdHlpPrintf(pCmdHlp, "%04x TSS64 at %Dv (min=%04x)\n", SelTss, &VarTssAddr, cbTssMin);
            else
                DBGCCmdHlpPrintf(pCmdHlp, "TSS64 at %Dv (min=%04x)\n", &VarTssAddr, cbTssMin);
            DBGCCmdHlpPrintf(pCmdHlp,
                             "rsp0=%016RX64 rsp1=%016RX64 rsp2=%016RX64\n"
                             "ist1=%016RX64 ist2=%016RX64\n"
                             "ist3=%016RX64 ist4=%016RX64\n"
                             "ist5=%016RX64 ist6=%016RX64\n"
                             "ist7=%016RX64 iomap=%04x\n"
                             ,
                             pTss->rsp0, pTss->rsp1, pTss->rsp2,
                             pTss->ist1, pTss->ist2,
                             pTss->ist3, pTss->ist4,
                             pTss->ist5, pTss->ist6,
                             pTss->ist7, pTss->offIoBitmap);
            offIoBitmap = pTss->offIoBitmap;
            break;
        }

        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR);
    }

    /*
     * Dump the interrupt redirection bitmap.
     */
    if (enmTssType != kTss16)
    {
        if (   offIoBitmap > cbTssMin
            && offIoBitmap < cbTss)  /** @todo check exactly what the edge cases are here. */
        {
            if (offIoBitmap - cbTssMin >= 32)
            {
                DBGCCmdHlpPrintf(pCmdHlp, "Interrupt redirection:\n");
                uint8_t const *pbIntRedirBitmap = &abBuf[offIoBitmap - 32];
                uint32_t    iStart = 0;
                bool        fPrev  = ASMBitTest(pbIntRedirBitmap, 0); /* LE/BE issue */
                for (uint32_t i = 0; i < 256; i++)
                {
                    bool fThis = ASMBitTest(pbIntRedirBitmap, i);
                    if (fThis != fPrev)
                    {
                        DBGCCmdHlpPrintf(pCmdHlp, "%02x-%02x %s\n", iStart, i - 1, fPrev ? "Protected mode" : "Redirected");
                        fPrev  = fThis;
                        iStart = i;
                    }
                }
                DBGCCmdHlpPrintf(pCmdHlp, "%02x-%02x %s\n", iStart, 255, fPrev ? "Protected mode" : "Redirected");
            }
            else
                DBGCCmdHlpPrintf(pCmdHlp, "Invalid interrupt redirection bitmap size: %u (%#x), expected 32 bytes.\n",
                                 offIoBitmap - cbTssMin, offIoBitmap - cbTssMin);
        }
        else if (offIoBitmap > 0)
            DBGCCmdHlpPrintf(pCmdHlp, "No interrupt redirection bitmap (-%#x)\n", cbTssMin - offIoBitmap);
        else
            DBGCCmdHlpPrintf(pCmdHlp, "No interrupt redirection bitmap\n");
    }

    /*
     * Dump the I/O permission bitmap if present. The IOPM cannot start below offset 0x68
     * (that applies to both 32-bit and 64-bit TSSs since their size is the same).
     * Note that there is always one padding byte that is not technically part of the bitmap
     * and "must have all bits set". It's not clear what happens when it doesn't. All ports
     * not covered by the bitmap are considered to be not accessible.
     */
    if (enmTssType != kTss16)
    {
        if (offIoBitmap < cbTss && offIoBitmap >= 0x68)
        {
            uint32_t        cPorts      = RT_MIN((cbTss - offIoBitmap) * 8, _64K);
            DBGCVAR         VarAddr;
            DBGCCmdHlpEval(pCmdHlp, &VarAddr, "%DV + %#x", &VarTssAddr, offIoBitmap);
            DBGCCmdHlpPrintf(pCmdHlp, "I/O bitmap at %DV - %#x ports:\n", &VarAddr, cPorts);

            uint8_t const  *pbIoBitmap  = &abBuf[offIoBitmap];
            uint32_t        iStart      = 0;
            bool            fPrev       = ASMBitTest(pbIoBitmap, 0);
            uint32_t        cLine       = 0;
            for (uint32_t i = 1; i < _64K; i++)
            {
                bool fThis = i < cPorts ? ASMBitTest(pbIoBitmap, i) : true;
                if (fThis != fPrev)
                {
                    cLine++;
                    DBGCCmdHlpPrintf(pCmdHlp, "%04x-%04x %s%s", iStart, i-1,
                                     fPrev ? "GP" : "OK", (cLine % 6) == 0 ? "\n" : "  ");
                    fPrev  = fThis;
                    iStart = i;
                }
            }
            DBGCCmdHlpPrintf(pCmdHlp, "%04x-%04x %s\n", iStart, _64K-1, fPrev ? "GP" : "OK");
        }
        else if (offIoBitmap > 0)
            DBGCCmdHlpPrintf(pCmdHlp, "No I/O bitmap (-%#x)\n", cbTssMin - offIoBitmap);
        else
            DBGCCmdHlpPrintf(pCmdHlp, "No I/O bitmap\n");
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGFR3TYPEDUMP, The 'dti' command dumper callback.}
 */
static DECLCALLBACK(int) dbgcCmdDumpTypeInfoCallback(uint32_t off, const char *pszField, uint32_t iLvl,
                                                     const char *pszType, uint32_t fTypeFlags,
                                                     uint32_t cElements, void *pvUser)
{
    PDBGCCMDHLP pCmdHlp = (PDBGCCMDHLP)pvUser;

    /* Pad with spaces to match the level. */
    for (uint32_t i = 0; i < iLvl; i++)
        DBGCCmdHlpPrintf(pCmdHlp, "    ");

    size_t cbWritten = 0;
    DBGCCmdHlpPrintfEx(pCmdHlp, &cbWritten, "+0x%04x %s", off, pszField);
    while (cbWritten < 32)
    {
        /* Fill with spaces to get proper aligning. */
        DBGCCmdHlpPrintf(pCmdHlp, " ");
        cbWritten++;
    }

    DBGCCmdHlpPrintf(pCmdHlp, ": ");
    if (fTypeFlags & DBGFTYPEREGMEMBER_F_ARRAY)
        DBGCCmdHlpPrintf(pCmdHlp, "[%u] ", cElements);
    if (fTypeFlags & DBGFTYPEREGMEMBER_F_POINTER)
        DBGCCmdHlpPrintf(pCmdHlp, "Ptr ");
    DBGCCmdHlpPrintf(pCmdHlp, "%s\n", pszType);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dti' command.}
 */
static DECLCALLBACK(int) dbgcCmdDumpTypeInfo(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 1 || cArgs == 2);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[0].enmType == DBGCVAR_TYPE_STRING);
    if (cArgs == 2)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[1].enmType == DBGCVAR_TYPE_NUMBER);

    uint32_t cLvlMax = cArgs == 2 ? (uint32_t)paArgs[1].u.u64Number : UINT32_MAX;
    return DBGFR3TypeDumpEx(pUVM, paArgs[0].u.pszString, 0 /* fFlags */, cLvlMax,
                            dbgcCmdDumpTypeInfoCallback, pCmdHlp);
}


static void dbgcCmdDumpTypedValCallbackBuiltin(PDBGCCMDHLP pCmdHlp, DBGFTYPEBUILTIN enmType, size_t cbType,
                                               PDBGFTYPEVALBUF pValBuf)
{
    switch (enmType)
    {
        case DBGFTYPEBUILTIN_UINT8:
            DBGCCmdHlpPrintf(pCmdHlp, "%RU8", pValBuf->u8);
            break;
        case DBGFTYPEBUILTIN_INT8:
            DBGCCmdHlpPrintf(pCmdHlp, "%RI8", pValBuf->i8);
            break;
        case DBGFTYPEBUILTIN_UINT16:
            DBGCCmdHlpPrintf(pCmdHlp, "%RU16", pValBuf->u16);
            break;
        case DBGFTYPEBUILTIN_INT16:
            DBGCCmdHlpPrintf(pCmdHlp, "%RI16", pValBuf->i16);
            break;
        case DBGFTYPEBUILTIN_UINT32:
            DBGCCmdHlpPrintf(pCmdHlp, "%RU32", pValBuf->u32);
            break;
        case DBGFTYPEBUILTIN_INT32:
            DBGCCmdHlpPrintf(pCmdHlp, "%RI32", pValBuf->i32);
            break;
        case DBGFTYPEBUILTIN_UINT64:
            DBGCCmdHlpPrintf(pCmdHlp, "%RU64", pValBuf->u64);
            break;
        case DBGFTYPEBUILTIN_INT64:
            DBGCCmdHlpPrintf(pCmdHlp, "%RI64", pValBuf->i64);
            break;
        case DBGFTYPEBUILTIN_PTR32:
            DBGCCmdHlpPrintf(pCmdHlp, "%RX32", pValBuf->GCPtr);
            break;
        case DBGFTYPEBUILTIN_PTR64:
            DBGCCmdHlpPrintf(pCmdHlp, "%RX64", pValBuf->GCPtr);
            break;
        case DBGFTYPEBUILTIN_PTR:
            if (cbType == sizeof(uint32_t))
                DBGCCmdHlpPrintf(pCmdHlp, "%RX32", pValBuf->GCPtr);
            else if (cbType == sizeof(uint64_t))
                DBGCCmdHlpPrintf(pCmdHlp, "%RX64", pValBuf->GCPtr);
            else
                DBGCCmdHlpPrintf(pCmdHlp, "<Unsupported pointer width %u>", cbType);
            break;
        case DBGFTYPEBUILTIN_SIZE:
            if (cbType == sizeof(uint32_t))
                DBGCCmdHlpPrintf(pCmdHlp, "%RU32", pValBuf->size);
            else if (cbType == sizeof(uint64_t))
                DBGCCmdHlpPrintf(pCmdHlp, "%RU64", pValBuf->size);
            else
                DBGCCmdHlpPrintf(pCmdHlp, "<Unsupported size width %u>", cbType);
            break;
        case DBGFTYPEBUILTIN_FLOAT32:
        case DBGFTYPEBUILTIN_FLOAT64:
        case DBGFTYPEBUILTIN_COMPOUND:
        default:
            AssertMsgFailed(("Invalid built-in type: %d\n", enmType));
    }
}

/**
 * @callback_method_impl{FNDBGFR3TYPEDUMP, The 'dtv' command dumper callback.}
 */
static DECLCALLBACK(int) dbgcCmdDumpTypedValCallback(uint32_t off, const char *pszField, uint32_t iLvl,
                                                     DBGFTYPEBUILTIN enmType, size_t cbType,
                                                     PDBGFTYPEVALBUF pValBuf, uint32_t cValBufs,
                                                     void *pvUser)
{
    PDBGCCMDHLP pCmdHlp = (PDBGCCMDHLP)pvUser;

    /* Pad with spaces to match the level. */
    for (uint32_t i = 0; i < iLvl; i++)
        DBGCCmdHlpPrintf(pCmdHlp, "    ");

    size_t cbWritten = 0;
    DBGCCmdHlpPrintfEx(pCmdHlp, &cbWritten, "+0x%04x %s", off, pszField);
    while (cbWritten < 32)
    {
        /* Fill with spaces to get proper aligning. */
        DBGCCmdHlpPrintf(pCmdHlp, " ");
        cbWritten++;
    }

    DBGCCmdHlpPrintf(pCmdHlp, ": ");
    if (cValBufs > 1)
        DBGCCmdHlpPrintf(pCmdHlp, "[%u] [ ", cValBufs);

    for (uint32_t i = 0; i < cValBufs; i++)
    {
        dbgcCmdDumpTypedValCallbackBuiltin(pCmdHlp, enmType, cbType, pValBuf);
        if (i < cValBufs - 1)
            DBGCCmdHlpPrintf(pCmdHlp, " , ");
        pValBuf++;
    }

    if (cValBufs > 1)
        DBGCCmdHlpPrintf(pCmdHlp, " ]");
    DBGCCmdHlpPrintf(pCmdHlp, "\n");

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dtv' command.}
 */
static DECLCALLBACK(int) dbgcCmdDumpTypedVal(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 2 || cArgs == 3);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[0].enmType == DBGCVAR_TYPE_STRING);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISGCPOINTER(paArgs[1].enmType));
    if (cArgs == 3)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[2].enmType == DBGCVAR_TYPE_NUMBER);

    /*
     * Make DBGF address and fix the range.
     */
    DBGFADDRESS Address;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[1], &Address);
    if (RT_FAILURE(rc))
        return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "VarToDbgfAddr(,%Dv,)\n", &paArgs[1]);

    uint32_t cLvlMax = cArgs == 3 ? (uint32_t)paArgs[2].u.u64Number : UINT32_MAX;
    return DBGFR3TypeValDumpEx(pUVM, &Address, paArgs[0].u.pszString, 0 /* fFlags */, cLvlMax,
                            dbgcCmdDumpTypedValCallback, pCmdHlp);
}

/**
 * @callback_method_impl{FNDBGCCMD, The 'm' command.}
 */
static DECLCALLBACK(int) dbgcCmdMemoryInfo(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGCCmdHlpPrintf(pCmdHlp, "Address: %DV\n", &paArgs[0]);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    return dbgcCmdDumpPageHierarchy(pCmd, pCmdHlp, pUVM, paArgs, cArgs);
}


/**
 * Converts one or more variables into a byte buffer for a
 * given unit size.
 *
 * @returns VBox status codes:
 * @retval  VERR_TOO_MUCH_DATA if the buffer is too small, bitched.
 * @retval  VERR_INTERNAL_ERROR on bad variable type, bitched.
 * @retval  VINF_SUCCESS on success.
 *
 * @param   pCmdHlp The command helper callback table.
 * @param   pvBuf   The buffer to convert into.
 * @param   pcbBuf  The buffer size on input. The size of the result on output.
 * @param   cbUnit  The unit size to apply when converting.
 *                  The high bit is used to indicate unicode string.
 * @param   paVars  The array of variables to convert.
 * @param   cVars   The number of variables.
 */
static int dbgcVarsToBytes(PDBGCCMDHLP pCmdHlp, void *pvBuf, uint32_t *pcbBuf, size_t cbUnit, PCDBGCVAR paVars, unsigned cVars)
{
    union
    {
        uint8_t *pu8;
        uint16_t *pu16;
        uint32_t *pu32;
        uint64_t *pu64;
    } u, uEnd;
    u.pu8 = (uint8_t *)pvBuf;
    uEnd.pu8 = u.pu8 + *pcbBuf;

    unsigned i;
    for (i = 0; i < cVars && u.pu8 < uEnd.pu8; i++)
    {
        switch (paVars[i].enmType)
        {
            case DBGCVAR_TYPE_GC_FAR:
            case DBGCVAR_TYPE_GC_FLAT:
            case DBGCVAR_TYPE_GC_PHYS:
            case DBGCVAR_TYPE_HC_FLAT:
            case DBGCVAR_TYPE_HC_PHYS:
            case DBGCVAR_TYPE_NUMBER:
            {
                uint64_t u64 = paVars[i].u.u64Number;
                switch (cbUnit & 0x1f)
                {
                    case 1:
                        do
                        {
                            *u.pu8++ = u64;
                            u64 >>= 8;
                        } while (u64);
                        break;
                    case 2:
                        do
                        {
                            *u.pu16++ = u64;
                            u64 >>= 16;
                        } while (u64);
                        break;
                    case 4:
                        *u.pu32++ = u64;
                        u64 >>= 32;
                        if (u64)
                            *u.pu32++ = u64;
                        break;
                    case 8:
                        *u.pu64++ = u64;
                        break;
                }
                break;
            }

            case DBGCVAR_TYPE_STRING:
            case DBGCVAR_TYPE_SYMBOL:
            {
                const char *psz = paVars[i].u.pszString;
                size_t cbString = strlen(psz);
                if (cbUnit & RT_BIT_32(31))
                {
                    /* Explode char to unit. */
                    if (cbString > (uintptr_t)(uEnd.pu8 - u.pu8) * (cbUnit & 0x1f))
                    {
                        pCmdHlp->pfnVBoxError(pCmdHlp, VERR_TOO_MUCH_DATA, "Max %d bytes.\n", uEnd.pu8 - (uint8_t *)pvBuf);
                        return VERR_TOO_MUCH_DATA;
                    }
                    while (*psz)
                    {
                        switch (cbUnit & 0x1f)
                        {
                            case 1: *u.pu8++ = *psz; break;
                            case 2: *u.pu16++ = *psz; break;
                            case 4: *u.pu32++ = *psz; break;
                            case 8: *u.pu64++ = *psz; break;
                        }
                        psz++;
                    }
                }
                else
                {
                    /* Raw copy with zero padding if the size isn't aligned. */
                    if (cbString > (uintptr_t)(uEnd.pu8 - u.pu8))
                    {
                        pCmdHlp->pfnVBoxError(pCmdHlp, VERR_TOO_MUCH_DATA, "Max %d bytes.\n", uEnd.pu8 - (uint8_t *)pvBuf);
                        return VERR_TOO_MUCH_DATA;
                    }

                    size_t cbCopy = cbString & ~(cbUnit - 1);
                    memcpy(u.pu8, psz, cbCopy);
                    u.pu8 += cbCopy;
                    psz += cbCopy;

                    size_t cbReminder = cbString & (cbUnit - 1);
                    if (cbReminder)
                    {
                        memcpy(u.pu8, psz, cbString & (cbUnit - 1));
                        memset(u.pu8 + cbReminder, 0, cbUnit - cbReminder);
                        u.pu8 += cbUnit;
                    }
                }
                break;
            }

            default:
                *pcbBuf = u.pu8 - (uint8_t *)pvBuf;
                pCmdHlp->pfnVBoxError(pCmdHlp, VERR_INTERNAL_ERROR,
                                      "i=%d enmType=%d\n", i, paVars[i].enmType);
                return VERR_INTERNAL_ERROR;
        }
    }
    *pcbBuf = u.pu8 - (uint8_t *)pvBuf;
    if (i != cVars)
    {
        pCmdHlp->pfnVBoxError(pCmdHlp, VERR_TOO_MUCH_DATA, "Max %d bytes.\n", uEnd.pu8 - (uint8_t *)pvBuf);
        return VERR_TOO_MUCH_DATA;
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'eb'\, 'ew'\, 'ed' and 'eq' commands.}
 */
static DECLCALLBACK(int) dbgcCmdEditMem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs >= 2);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISPOINTER(paArgs[0].enmType));
    for (unsigned iArg = 1; iArg < cArgs; iArg++)
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[iArg].enmType == DBGCVAR_TYPE_NUMBER);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Figure out the element size.
     */
    unsigned    cbElement;
    switch (pCmd->pszCmd[1])
    {
        default:
        case 'b':   cbElement = 1; break;
        case 'w':   cbElement = 2; break;
        case 'd':   cbElement = 4; break;
        case 'q':   cbElement = 8; break;
    }

    /*
     * Do setting.
     */
    DBGCVAR Addr = paArgs[0];
    for (unsigned iArg = 1;;)
    {
        size_t cbWritten;
        int rc = pCmdHlp->pfnMemWrite(pCmdHlp, &paArgs[iArg].u, cbElement, &Addr, &cbWritten);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Writing memory at %DV.\n", &Addr);
        if (cbWritten != cbElement)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Only wrote %u out of %u bytes!\n", cbWritten, cbElement);

        /* advance. */
        iArg++;
        if (iArg >= cArgs)
            break;
        rc = DBGCCmdHlpEval(pCmdHlp, &Addr, "%Dv + %#x", &Addr, cbElement);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "%%(%Dv)", &paArgs[0]);
    }

    return VINF_SUCCESS;
}


/**
 * Executes the search.
 *
 * @returns VBox status code.
 * @param   pCmdHlp     The command helpers.
 * @param   pUVM        The user mode VM handle.
 * @param   pAddress    The address to start searching from. (undefined on output)
 * @param   cbRange     The address range to search. Must not wrap.
 * @param   pabBytes    The byte pattern to search for.
 * @param   cbBytes     The size of the pattern.
 * @param   cbUnit      The search unit.
 * @param   cMaxHits    The max number of hits.
 * @param   pResult     Where to store the result if it's a function invocation.
 */
static int dbgcCmdWorkerSearchMemDoIt(PDBGCCMDHLP pCmdHlp, PUVM pUVM, PDBGFADDRESS pAddress, RTGCUINTPTR cbRange,
                                      const uint8_t *pabBytes, uint32_t cbBytes,
                                      uint32_t cbUnit, uint64_t cMaxHits, PDBGCVAR pResult)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Do the search.
     */
    uint64_t cHits = 0;
    for (;;)
    {
        /* search */
        DBGFADDRESS HitAddress;
        int rc = DBGFR3MemScan(pUVM, pDbgc->idCpu, pAddress, cbRange, 1, pabBytes, cbBytes, &HitAddress);
        if (RT_FAILURE(rc))
        {
            if (rc != VERR_DBGF_MEM_NOT_FOUND)
                return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "DBGFR3MemScan\n");

            /* update the current address so we can save it (later). */
            pAddress->off += cbRange;
            pAddress->FlatPtr += cbRange;
            cbRange = 0;
            break;
        }

        /* report result */
        DBGCVAR VarCur;
        rc = DBGCCmdHlpVarFromDbgfAddr(pCmdHlp, &HitAddress, &VarCur);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGCCmdHlpVarFromDbgfAddr\n");
        if (!pResult)
            pCmdHlp->pfnExec(pCmdHlp, "db %DV LB 10", &VarCur);
        else
            DBGCVAR_ASSIGN(pResult, &VarCur);

        /* advance */
        cbRange -= HitAddress.FlatPtr - pAddress->FlatPtr;
        *pAddress = HitAddress;
        pAddress->FlatPtr += cbBytes;
        pAddress->off += cbBytes;
        if (cbRange <= cbBytes)
        {
            cbRange = 0;
            break;
        }
        cbRange -= cbBytes;

        if (++cHits >= cMaxHits)
        {
            /// @todo save the search.
            break;
        }
    }

    /*
     * Save the search so we can resume it...
     */
    if (pDbgc->abSearch != pabBytes)
    {
        memcpy(pDbgc->abSearch, pabBytes, cbBytes);
        pDbgc->cbSearch = cbBytes;
        pDbgc->cbSearchUnit = cbUnit;
    }
    pDbgc->cMaxSearchHits = cMaxHits;
    pDbgc->SearchAddr = *pAddress;
    pDbgc->cbSearchRange = cbRange;

    return cHits ? VINF_SUCCESS : VERR_DBGC_COMMAND_FAILED;
}


/**
 * Resumes the previous search.
 *
 * @returns VBox status code.
 * @param   pCmdHlp     Pointer to the command helper functions.
 * @param   pUVM        The user mode VM handle.
 * @param   pResult     Where to store the result of a function invocation.
 */
static int dbgcCmdWorkerSearchMemResume(PDBGCCMDHLP pCmdHlp, PUVM pUVM, PDBGCVAR pResult)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Make sure there is a previous command.
     */
    if (!pDbgc->cbSearch)
    {
        DBGCCmdHlpPrintf(pCmdHlp, "Error: No previous search\n");
        return VERR_DBGC_COMMAND_FAILED;
    }

    /*
     * Make range and address adjustments.
     */
    DBGFADDRESS Address = pDbgc->SearchAddr;
    if (Address.FlatPtr == ~(RTGCUINTPTR)0)
    {
        Address.FlatPtr -= Address.off;
        Address.off = 0;
    }

    RTGCUINTPTR cbRange = pDbgc->cbSearchRange;
    if (!cbRange)
        cbRange = ~(RTGCUINTPTR)0;
    if (Address.FlatPtr + cbRange < pDbgc->SearchAddr.FlatPtr)
        cbRange = ~(RTGCUINTPTR)0 - pDbgc->SearchAddr.FlatPtr + !!pDbgc->SearchAddr.FlatPtr;

    return dbgcCmdWorkerSearchMemDoIt(pCmdHlp, pUVM, &Address, cbRange, pDbgc->abSearch, pDbgc->cbSearch,
                                      pDbgc->cbSearchUnit, pDbgc->cMaxSearchHits, pResult);
}


/**
 * Search memory, worker for the 's' and 's?' functions.
 *
 * @returns VBox status code.
 * @param   pCmdHlp     Pointer to the command helper functions.
 * @param   pUVM        The user mode VM handle.
 * @param   pAddress    Where to start searching. If no range, search till end of address space.
 * @param   cMaxHits    The maximum number of hits.
 * @param   chType      The search type.
 * @param   paPatArgs   The pattern variable array.
 * @param   cPatArgs    Number of pattern variables.
 * @param   pResult     Where to store the result of a function invocation.
 */
static int dbgcCmdWorkerSearchMem(PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR pAddress, uint64_t cMaxHits, char chType,
                                  PCDBGCVAR paPatArgs, unsigned cPatArgs, PDBGCVAR pResult)
{
    if (pResult)
        DBGCVAR_INIT_GC_FLAT(pResult, 0);

    /*
     * Convert the search pattern into bytes and DBGFR3MemScan can deal with.
     */
    uint32_t cbUnit;
    switch (chType)
    {
        case 'a':
        case 'b':   cbUnit = 1; break;
        case 'u':   cbUnit = 2 | RT_BIT_32(31); break;
        case 'w':   cbUnit = 2; break;
        case 'd':   cbUnit = 4; break;
        case 'q':   cbUnit = 8; break;
        default:
            return pCmdHlp->pfnVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "chType=%c\n", chType);
    }
    uint8_t abBytes[RT_SIZEOFMEMB(DBGC, abSearch)];
    uint32_t cbBytes = sizeof(abBytes);
    int rc = dbgcVarsToBytes(pCmdHlp, abBytes, &cbBytes, cbUnit, paPatArgs, cPatArgs);
    if (RT_FAILURE(rc))
        return VERR_DBGC_COMMAND_FAILED;

    /*
     * Make DBGF address and fix the range.
     */
    DBGFADDRESS Address;
    rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, pAddress, &Address);
    if (RT_FAILURE(rc))
        return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "VarToDbgfAddr(,%Dv,)\n", pAddress);

    RTGCUINTPTR cbRange;
    switch (pAddress->enmRangeType)
    {
        case DBGCVAR_RANGE_BYTES:
            cbRange = pAddress->u64Range;
            if (cbRange != pAddress->u64Range)
                cbRange = ~(RTGCUINTPTR)0;
            break;

        case DBGCVAR_RANGE_ELEMENTS:
            cbRange = (RTGCUINTPTR)(pAddress->u64Range * cbUnit);
            if (    cbRange != pAddress->u64Range * cbUnit
                ||  cbRange < pAddress->u64Range)
                cbRange = ~(RTGCUINTPTR)0;
            break;

        default:
            cbRange = ~(RTGCUINTPTR)0;
            break;
    }
    if (Address.FlatPtr + cbRange < Address.FlatPtr)
        cbRange = ~(RTGCUINTPTR)0 - Address.FlatPtr + !!Address.FlatPtr;

    /*
     * Ok, do it.
     */
    return dbgcCmdWorkerSearchMemDoIt(pCmdHlp, pUVM, &Address, cbRange, abBytes, cbBytes, cbUnit, cMaxHits, pResult);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 's' command.}
 */
static DECLCALLBACK(int) dbgcCmdSearchMem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF2(pCmd, paArgs);

    /* check that the parser did what it's supposed to do. */
    //if (    cArgs <= 2
    //    &&  paArgs[0].enmType != DBGCVAR_TYPE_STRING)
    //    return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");

    /*
     * Repeat previous search?
     */
    if (cArgs == 0)
        return dbgcCmdWorkerSearchMemResume(pCmdHlp, pUVM, NULL);

    /*
     * Parse arguments.
     */

    return -1;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 's?' command.}
 */
static DECLCALLBACK(int) dbgcCmdSearchMemType(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /* check that the parser did what it's supposed to do. */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs >= 2 && DBGCVAR_ISGCPOINTER(paArgs[0].enmType));
    return dbgcCmdWorkerSearchMem(pCmdHlp, pUVM, &paArgs[0], 25, pCmd->pszCmd[1], paArgs + 1, cArgs - 1, NULL);
}


/**
 * Matching function for interrupts event names.
 *
 * This parses the interrupt number and length.
 *
 * @returns True if match, false if not.
 * @param   pPattern    The user specified pattern to match.
 * @param   pszEvtName  The event name.
 * @param   pCmdHlp     Command helpers for warning about malformed stuff.
 * @param   piFirst     Where to return start interrupt number on success.
 * @param   pcInts      Where to return the number of interrupts on success.
 */
static bool dbgcEventIsMatchingInt(PCDBGCVAR pPattern, const char *pszEvtName, PDBGCCMDHLP pCmdHlp,
                                   uint8_t *piFirst, uint16_t *pcInts)
{
    /*
     * Ignore trailing hex digits when comparing with the event base name.
     */
    const char *pszPattern = pPattern->u.pszString;
    const char *pszEnd = RTStrEnd(pszPattern, RTSTR_MAX);
    while (   (uintptr_t)pszEnd > (uintptr_t)pszPattern
           && RT_C_IS_XDIGIT(pszEnd[-1]))
        pszEnd -= 1;
    if (RTStrSimplePatternNMatch(pszPattern, pszEnd - pszPattern, pszEvtName, RTSTR_MAX))
    {
        /*
         * Parse the index and length.
         */
        if (!*pszEnd)
            *piFirst = 0;
        else
        {
            int rc = RTStrToUInt8Full(pszEnd, 16, piFirst);
            if (rc != VINF_SUCCESS)
            {
                if (RT_FAILURE(rc))
                    *piFirst = 0;
                DBGCCmdHlpPrintf(pCmdHlp, "Warning: %Rrc parsing '%s' - interpreting it as %#x\n", rc, pszEnd, *piFirst);
            }
        }

        if (pPattern->enmRangeType == DBGCVAR_RANGE_NONE)
            *pcInts = 1;
        else
            *pcInts = RT_MAX(RT_MIN((uint16_t)pPattern->u64Range, 256 - *piFirst), 1);
        return true;
    }
    return false;
}


/**
 * Updates a DBGC event config.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   ppEvtCfg        The event configuration entry to update.
 * @param   pszCmd          The new command. Leave command alone if NULL.
 * @param   enmEvtState     The new event state.
 * @param   fChangeCmdOnly  Whether to only update the command.
 */
static int dbgcEventUpdate(PDBGCEVTCFG *ppEvtCfg, const char *pszCmd, DBGCEVTSTATE enmEvtState, bool fChangeCmdOnly)
{
    PDBGCEVTCFG pEvtCfg = *ppEvtCfg;

    /*
     * If we've got a command string, update the command too.
     */
    if (pszCmd)
    {
        size_t cchCmd = strlen(pszCmd);
        if (   !cchCmd
            && (  !fChangeCmdOnly
                ? enmEvtState == kDbgcEvtState_Disabled
                : !pEvtCfg || pEvtCfg->enmState == kDbgcEvtState_Disabled))
        {
            /* NULL entry is fine if no command and disabled. */
            RTMemFree(pEvtCfg);
            *ppEvtCfg = NULL;
        }
        else
        {
            if (!pEvtCfg || pEvtCfg->cchCmd < cchCmd)
            {
                RTMemFree(pEvtCfg);
                *ppEvtCfg = pEvtCfg = (PDBGCEVTCFG)RTMemAlloc(RT_UOFFSETOF_DYN(DBGCEVTCFG, szCmd[cchCmd + 1]));
                if (!pEvtCfg)
                    return VERR_NO_MEMORY;
            }
            pEvtCfg->enmState = enmEvtState;
            pEvtCfg->cchCmd   = cchCmd;
            memcpy(pEvtCfg->szCmd, pszCmd, cchCmd + 1);
        }
    }
    /*
     * Update existing or enable new.  If NULL and not enabled, we can keep it that way.
     */
    else if (pEvtCfg || enmEvtState != kDbgcEvtState_Disabled)
    {
        if (!pEvtCfg)
        {
            *ppEvtCfg = pEvtCfg = (PDBGCEVTCFG)RTMemAlloc(sizeof(DBGCEVTCFG));
            if (!pEvtCfg)
                return VERR_NO_MEMORY;
            pEvtCfg->cchCmd = 0;
            pEvtCfg->szCmd[0] = '\0';
        }
        pEvtCfg->enmState = enmEvtState;
    }

    return VINF_SUCCESS;
}


/**
 * Record one settings change for a plain event.
 *
 * @returns The new @a cIntCfgs value.
 * @param   paEventCfgs The event setttings array.  Must have DBGFEVENT_END
 *                      entries.
 * @param   cEventCfgs  The current number of entries in @a paEventCfgs.
 * @param   enmType     The event to change the settings for.
 * @param   enmEvtState The new event state.
 * @param   iSxEvt      Index into the g_aDbgcSxEvents array.
 *
 * @remarks We use abUnused[0] for the enmEvtState, while abUnused[1] and
 *          abUnused[2] are used for iSxEvt.
 */
static uint32_t dbgcEventAddPlainConfig(PDBGFEVENTCONFIG paEventCfgs, uint32_t cEventCfgs, DBGFEVENTTYPE enmType,
                                        DBGCEVTSTATE enmEvtState, uint16_t iSxEvt)
{
    uint32_t iCfg;
    for (iCfg = 0; iCfg < cEventCfgs; iCfg++)
        if (paEventCfgs[iCfg].enmType == enmType)
            break;
    if (iCfg == cEventCfgs)
    {
        Assert(cEventCfgs < DBGFEVENT_END);
        paEventCfgs[iCfg].enmType = enmType;
        cEventCfgs++;
    }
    paEventCfgs[iCfg].fEnabled    = enmEvtState > kDbgcEvtState_Disabled;
    paEventCfgs[iCfg].abUnused[0] = enmEvtState;
    paEventCfgs[iCfg].abUnused[1] = (uint8_t)iSxEvt;
    paEventCfgs[iCfg].abUnused[2] = (uint8_t)(iSxEvt >> 8);
    return cEventCfgs;
}


/**
 * Record one or more interrupt event config changes.
 *
 * @returns The new @a cIntCfgs value.
 * @param   paIntCfgs   Interrupt confiruation array. Must have 256 entries.
 * @param   cIntCfgs    The current number of entries in @a paIntCfgs.
 * @param   iInt        The interrupt number to start with.
 * @param   cInts       The number of interrupts to change.
 * @param   pszName     The settings name (hwint/swint).
 * @param   enmEvtState The new event state.
 * @param   bIntOp      The new DBGF interrupt state.
 */
static uint32_t dbgcEventAddIntConfig(PDBGFINTERRUPTCONFIG paIntCfgs, uint32_t cIntCfgs, uint8_t iInt, uint16_t cInts,
                                      const char *pszName, DBGCEVTSTATE enmEvtState, uint8_t bIntOp)
{
    bool const fHwInt = *pszName == 'h';

    bIntOp |= (uint8_t)enmEvtState << 4;
    uint8_t const   bSoftState = !fHwInt ? bIntOp : DBGFINTERRUPTSTATE_DONT_TOUCH;
    uint8_t const   bHardState = fHwInt  ? bIntOp : DBGFINTERRUPTSTATE_DONT_TOUCH;

    while (cInts > 0)
    {
        uint32_t iCfg;
        for (iCfg = 0; iCfg < cIntCfgs; iCfg++)
            if (paIntCfgs[iCfg].iInterrupt == iInt)
                break;
        if (iCfg == cIntCfgs)
            break;
        if (fHwInt)
            paIntCfgs[iCfg].enmHardState = bHardState;
        else
            paIntCfgs[iCfg].enmSoftState = bSoftState;
        iInt++;
        cInts--;
    }

    while (cInts > 0)
    {
        Assert(cIntCfgs < 256);
        paIntCfgs[cIntCfgs].iInterrupt   = iInt;
        paIntCfgs[cIntCfgs].enmHardState = bHardState;
        paIntCfgs[cIntCfgs].enmSoftState = bSoftState;
        cIntCfgs++;
        iInt++;
        cInts--;
    }

    return cIntCfgs;
}


/**
 * Applies event settings changes to DBGC and DBGF.
 *
 * @returns VBox status code (fully bitched)
 * @param   pCmdHlp         The command helpers.
 * @param   pUVM            The user mode VM handle.
 * @param   paIntCfgs       Interrupt configuration array.  We use the upper 4
 *                          bits of the settings for the DBGCEVTSTATE.  This
 *                          will be cleared.
 * @param   cIntCfgs        Number of interrupt configuration changes.
 * @param   paEventCfgs     The generic event configuration array.  We use the
 *                          abUnused[0] member for the DBGCEVTSTATE, and
 *                          abUnused[2:1] for the g_aDbgcSxEvents index.
 * @param   cEventCfgs      The number of generic event settings changes.
 * @param   pszCmd          The commands to associate with the changed events.
 *                          If this is NULL, don't touch the command.
 * @param   fChangeCmdOnly  Whether to only change the commands (sx-).
 */
static int dbgcEventApplyChanges(PDBGCCMDHLP pCmdHlp, PUVM pUVM, PDBGFINTERRUPTCONFIG paIntCfgs, uint32_t cIntCfgs,
                                 PCDBGFEVENTCONFIG paEventCfgs, uint32_t cEventCfgs, const char *pszCmd, bool fChangeCmdOnly)
{
    int rc;

    /*
     * Apply changes to DBGC.  This can only fail with out of memory error.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    if (cIntCfgs)
        for (uint32_t iCfg = 0; iCfg < cIntCfgs; iCfg++)
        {
            DBGCEVTSTATE enmEvtState = (DBGCEVTSTATE)(paIntCfgs[iCfg].enmHardState >> 4);
            paIntCfgs[iCfg].enmHardState &= 0xf;
            if (paIntCfgs[iCfg].enmHardState != DBGFINTERRUPTSTATE_DONT_TOUCH)
            {
                rc = dbgcEventUpdate(&pDbgc->apHardInts[paIntCfgs[iCfg].iInterrupt], pszCmd, enmEvtState, fChangeCmdOnly);
                if (RT_FAILURE(rc))
                    return rc;
            }

            enmEvtState = (DBGCEVTSTATE)(paIntCfgs[iCfg].enmSoftState >> 4);
            paIntCfgs[iCfg].enmSoftState &= 0xf;
            if (paIntCfgs[iCfg].enmSoftState != DBGFINTERRUPTSTATE_DONT_TOUCH)
            {
                rc = dbgcEventUpdate(&pDbgc->apSoftInts[paIntCfgs[iCfg].iInterrupt], pszCmd, enmEvtState, fChangeCmdOnly);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }

    if (cEventCfgs)
    {
        for (uint32_t iCfg = 0; iCfg < cEventCfgs; iCfg++)
        {
            Assert((unsigned)paEventCfgs[iCfg].enmType < RT_ELEMENTS(pDbgc->apEventCfgs));
            uint16_t iSxEvt = RT_MAKE_U16(paEventCfgs[iCfg].abUnused[1], paEventCfgs[iCfg].abUnused[2]);
            Assert(iSxEvt < RT_ELEMENTS(g_aDbgcSxEvents));
            rc = dbgcEventUpdate(&pDbgc->apEventCfgs[iSxEvt], pszCmd, (DBGCEVTSTATE)paEventCfgs[iCfg].abUnused[0], fChangeCmdOnly);
            if (RT_FAILURE(rc))
                return rc;
        }
    }

    /*
     * Apply changes to DBGF.
     */
    if (!fChangeCmdOnly)
    {
        if (cIntCfgs)
        {
            rc = DBGFR3InterruptConfigEx(pUVM, paIntCfgs, cIntCfgs);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3InterruptConfigEx: %Rrc\n", rc);
        }
        if (cEventCfgs)
        {
            rc = DBGFR3EventConfigEx(pUVM, paEventCfgs, cEventCfgs);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3EventConfigEx: %Rrc\n", rc);
        }
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'sx[eni-]' commands.}
 */
static DECLCALLBACK(int) dbgcCmdEventCtrl(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Figure out which command this is.
     */
    uint8_t         bIntOp;
    DBGCEVTSTATE    enmEvtState;
    bool            fChangeCmdOnly;
    switch (pCmd->pszCmd[2])
    {
        case 'e': bIntOp = DBGFINTERRUPTSTATE_ENABLED;  enmEvtState = kDbgcEvtState_Enabled;  fChangeCmdOnly = false; break;
        case 'n': bIntOp = DBGFINTERRUPTSTATE_ENABLED;  enmEvtState = kDbgcEvtState_Notify;   fChangeCmdOnly = false; break;
        case '-': bIntOp = DBGFINTERRUPTSTATE_ENABLED;  enmEvtState = kDbgcEvtState_Invalid;  fChangeCmdOnly = true;  break;
        case 'i': bIntOp = DBGFINTERRUPTSTATE_DISABLED; enmEvtState = kDbgcEvtState_Disabled; fChangeCmdOnly = false; break;
        default:
            return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "pszCmd=%s\n", pCmd->pszCmd);
    }

    /*
     * Command option.
     */
    unsigned    iArg = 0;
    const char *pszCmd = NULL;
    if (   cArgs >= iArg + 2
        && paArgs[iArg].enmType == DBGCVAR_TYPE_STRING
        && paArgs[iArg + 1].enmType == DBGCVAR_TYPE_STRING
        && strcmp(paArgs[iArg].u.pszString, "-c") == 0)
    {
        pszCmd = paArgs[iArg + 1].u.pszString;
        iArg += 2;
    }
    if (fChangeCmdOnly && !pszCmd)
        return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "The 'sx-' requires the '-c cmd' arguments.\n");

    /*
     * The remaining arguments are event specifiers to which the operation should be applied.
     */
    uint32_t            cIntCfgs   = 0;
    DBGFINTERRUPTCONFIG aIntCfgs[256];
    uint32_t            cEventCfgs = 0;
    DBGFEVENTCONFIG     aEventCfgs[DBGFEVENT_END];

    for (; iArg < cArgs; iArg++)
    {
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, iArg, paArgs[iArg].enmType == DBGCVAR_TYPE_STRING
                                                        || paArgs[iArg].enmType == DBGCVAR_TYPE_SYMBOL);
        uint32_t cHits = 0;
        for (uint32_t iEvt = 0; iEvt < RT_ELEMENTS(g_aDbgcSxEvents); iEvt++)
            if (g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Plain)
            {
                if (   RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszName)
                    || (   g_aDbgcSxEvents[iEvt].pszAltNm
                        && RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszAltNm)) )
                {
                    cEventCfgs = dbgcEventAddPlainConfig(aEventCfgs, cEventCfgs, g_aDbgcSxEvents[iEvt].enmType,
                                                         enmEvtState, iEvt);
                    cHits++;
                }
            }
            else
            {
                Assert(g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Interrupt);
                uint8_t  iInt;
                uint16_t cInts;
                if (dbgcEventIsMatchingInt(&paArgs[iArg], g_aDbgcSxEvents[iEvt].pszName, pCmdHlp, &iInt, &cInts))
                {
                    cIntCfgs = dbgcEventAddIntConfig(aIntCfgs, cIntCfgs, iInt, cInts, g_aDbgcSxEvents[iEvt].pszName,
                                                     enmEvtState, bIntOp);
                    cHits++;
                }
            }
        if (!cHits)
            return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown event: '%s'\n", paArgs[iArg].u.pszString);
    }

    /*
     * Apply the changes.
     */
    return dbgcEventApplyChanges(pCmdHlp, pUVM, aIntCfgs, cIntCfgs, aEventCfgs, cEventCfgs, pszCmd, fChangeCmdOnly);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'sxr' commands.}
 */
static DECLCALLBACK(int) dbgcCmdEventCtrlReset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF1(pCmd);
    uint32_t            cEventCfgs = 0;
    DBGFEVENTCONFIG     aEventCfgs[DBGFEVENT_END];
    uint32_t            cIntCfgs   = 0;
    DBGFINTERRUPTCONFIG aIntCfgs[256];

    if (cArgs == 0)
    {
        /*
         * All events.
         */
        for (uint32_t iInt = 0; iInt < 256; iInt++)
        {
            aIntCfgs[iInt].iInterrupt   = iInt;
            aIntCfgs[iInt].enmHardState = DBGFINTERRUPTSTATE_DONT_TOUCH;
            aIntCfgs[iInt].enmSoftState = DBGFINTERRUPTSTATE_DONT_TOUCH;
        }
        cIntCfgs = 256;

        for (uint32_t iEvt = 0; iEvt < RT_ELEMENTS(g_aDbgcSxEvents); iEvt++)
            if (g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Plain)
            {
                aEventCfgs[cEventCfgs].enmType     = g_aDbgcSxEvents[iEvt].enmType;
                aEventCfgs[cEventCfgs].fEnabled    = g_aDbgcSxEvents[iEvt].enmDefault > kDbgcEvtState_Disabled;
                aEventCfgs[cEventCfgs].abUnused[0] = g_aDbgcSxEvents[iEvt].enmDefault;
                aEventCfgs[cEventCfgs].abUnused[1] = (uint8_t)iEvt;
                aEventCfgs[cEventCfgs].abUnused[2] = (uint8_t)(iEvt >> 8);
                cEventCfgs++;
            }
            else
            {
                uint8_t const bState = (  g_aDbgcSxEvents[iEvt].enmDefault > kDbgcEvtState_Disabled
                                        ? DBGFINTERRUPTSTATE_ENABLED : DBGFINTERRUPTSTATE_DISABLED)
                                     | ((uint8_t)g_aDbgcSxEvents[iEvt].enmDefault << 4);
                if (strcmp(g_aDbgcSxEvents[iEvt].pszName, "hwint") == 0)
                    for (uint32_t iInt = 0; iInt < 256; iInt++)
                        aIntCfgs[iInt].enmHardState = bState;
                else
                    for (uint32_t iInt = 0; iInt < 256; iInt++)
                        aIntCfgs[iInt].enmSoftState = bState;
            }
    }
    else
    {
        /*
         * Selected events.
         */
        for (uint32_t iArg = 0; iArg < cArgs; iArg++)
        {
            unsigned cHits = 0;
            for (uint32_t iEvt = 0; iEvt < RT_ELEMENTS(g_aDbgcSxEvents); iEvt++)
                if (g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Plain)
                {
                    if (   RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszName)
                        || (   g_aDbgcSxEvents[iEvt].pszAltNm
                            && RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszAltNm)) )
                    {
                        cEventCfgs = dbgcEventAddPlainConfig(aEventCfgs, cEventCfgs, g_aDbgcSxEvents[iEvt].enmType,
                                                             g_aDbgcSxEvents[iEvt].enmDefault, iEvt);
                        cHits++;
                    }
                }
                else
                {
                    Assert(g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Interrupt);
                    uint8_t  iInt;
                    uint16_t cInts;
                    if (dbgcEventIsMatchingInt(&paArgs[iArg], g_aDbgcSxEvents[iEvt].pszName, pCmdHlp, &iInt, &cInts))
                    {
                        cIntCfgs = dbgcEventAddIntConfig(aIntCfgs, cIntCfgs, iInt, cInts, g_aDbgcSxEvents[iEvt].pszName,
                                                         g_aDbgcSxEvents[iEvt].enmDefault,
                                                         g_aDbgcSxEvents[iEvt].enmDefault > kDbgcEvtState_Disabled
                                                         ? DBGFINTERRUPTSTATE_ENABLED : DBGFINTERRUPTSTATE_DISABLED);
                        cHits++;
                    }
                }
            if (!cHits)
                return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown event: '%s'\n", paArgs[iArg].u.pszString);
        }
    }

    /*
     * Apply the reset changes.
     */
    return dbgcEventApplyChanges(pCmdHlp, pUVM, aIntCfgs, cIntCfgs, aEventCfgs, cEventCfgs, "", false);
}


/**
 * Used during DBGC initialization to configure events with defaults.
 *
 * @param   pDbgc       The DBGC instance.
 */
void dbgcEventInit(PDBGC pDbgc)
{
    if (pDbgc->pUVM)
        dbgcCmdEventCtrlReset(NULL, &pDbgc->CmdHlp, pDbgc->pUVM, NULL, 0);
}


/**
 * Used during DBGC termination to disable all events.
 *
 * @param   pDbgc       The DBGC instance.
 */
void dbgcEventTerm(PDBGC pDbgc)
{
/** @todo need to do more than just reset later. */
    if (pDbgc->pUVM && VMR3GetStateU(pDbgc->pUVM) < VMSTATE_DESTROYING)
        dbgcCmdEventCtrlReset(NULL, &pDbgc->CmdHlp, pDbgc->pUVM, NULL, 0);
}


static void dbgcEventDisplay(PDBGCCMDHLP pCmdHlp, const char *pszName, DBGCEVTSTATE enmDefault, PDBGCEVTCFG const *ppEvtCfg)
{
    RT_NOREF1(enmDefault);
    PDBGCEVTCFG pEvtCfg = *ppEvtCfg;

    const char *pszState;
    switch (pEvtCfg ? pEvtCfg->enmState : kDbgcEvtState_Disabled)
    {
        case kDbgcEvtState_Disabled:    pszState = "ignore"; break;
        case kDbgcEvtState_Enabled:     pszState = "enabled"; break;
        case kDbgcEvtState_Notify:      pszState = "notify"; break;
        default:
            AssertFailed();
            pszState = "invalid";
            break;
    }

    if (pEvtCfg && pEvtCfg->cchCmd > 0)
        DBGCCmdHlpPrintf(pCmdHlp, "%-22s  %-7s  \"%s\"\n", pszName, pszState, pEvtCfg->szCmd);
    else
        DBGCCmdHlpPrintf(pCmdHlp, "%-22s  %s\n", pszName, pszState);
}


static void dbgcEventDisplayRange(PDBGCCMDHLP pCmdHlp, const char *pszBaseNm, DBGCEVTSTATE enmDefault,
                                  PDBGCEVTCFG const *papEvtCfgs, unsigned iCfg, unsigned cCfgs)
{
    do
    {
        PCDBGCEVTCFG pFirstCfg = papEvtCfgs[iCfg];
        if (pFirstCfg && pFirstCfg->enmState == kDbgcEvtState_Disabled && pFirstCfg->cchCmd == 0)
            pFirstCfg = NULL;

        unsigned const iFirstCfg = iCfg;
        iCfg++;
        while (iCfg < cCfgs)
        {
            PCDBGCEVTCFG pCurCfg = papEvtCfgs[iCfg];
            if (pCurCfg && pCurCfg->enmState == kDbgcEvtState_Disabled  && pCurCfg->cchCmd == 0)
                pCurCfg = NULL;
            if (pCurCfg != pFirstCfg)
            {
                if (!pCurCfg || !pFirstCfg)
                    break;
                if (pCurCfg->enmState != pFirstCfg->enmState)
                    break;
                if (pCurCfg->cchCmd != pFirstCfg->cchCmd)
                    break;
                if (memcmp(pCurCfg->szCmd, pFirstCfg->szCmd, pFirstCfg->cchCmd) != 0)
                    break;
            }
            iCfg++;
        }

        char szName[16];
        unsigned cEntries = iCfg - iFirstCfg;
        if (cEntries == 1)
            RTStrPrintf(szName, sizeof(szName), "%s%02x", pszBaseNm, iFirstCfg);
        else
            RTStrPrintf(szName, sizeof(szName), "%s%02x L %#x", pszBaseNm, iFirstCfg, cEntries);
        dbgcEventDisplay(pCmdHlp, szName, enmDefault, &papEvtCfgs[iFirstCfg]);

        cCfgs -= cEntries;
    } while (cCfgs > 0);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'sx' commands.}
 */
static DECLCALLBACK(int) dbgcCmdEventCtrlList(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF2(pCmd, pUVM);
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    if (cArgs == 0)
    {
        /*
         * All events.
         */
        for (uint32_t iEvt = 0; iEvt < RT_ELEMENTS(g_aDbgcSxEvents); iEvt++)
            if (g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Plain)
                dbgcEventDisplay(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                 &pDbgc->apEventCfgs[iEvt]);
            else if (strcmp(g_aDbgcSxEvents[iEvt].pszName, "hwint") == 0)
                dbgcEventDisplayRange(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                      pDbgc->apHardInts, 0, 256);
            else
                dbgcEventDisplayRange(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                      pDbgc->apSoftInts, 0, 256);
    }
    else
    {
        /*
         * Selected events.
         */
        for (uint32_t iArg = 0; iArg < cArgs; iArg++)
        {
            unsigned cHits = 0;
            for (uint32_t iEvt = 0; iEvt < RT_ELEMENTS(g_aDbgcSxEvents); iEvt++)
                if (g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Plain)
                {
                    if (   RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszName)
                        || (   g_aDbgcSxEvents[iEvt].pszAltNm
                            && RTStrSimplePatternMatch(paArgs[iArg].u.pszString, g_aDbgcSxEvents[iEvt].pszAltNm)) )
                    {
                        dbgcEventDisplay(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                         &pDbgc->apEventCfgs[iEvt]);
                        cHits++;
                    }
                }
                else
                {
                    Assert(g_aDbgcSxEvents[iEvt].enmKind == kDbgcSxEventKind_Interrupt);
                    uint8_t  iInt;
                    uint16_t cInts;
                    if (dbgcEventIsMatchingInt(&paArgs[iArg], g_aDbgcSxEvents[iEvt].pszName, pCmdHlp, &iInt, &cInts))
                    {
                        if (strcmp(g_aDbgcSxEvents[iEvt].pszName, "hwint") == 0)
                            dbgcEventDisplayRange(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                                  pDbgc->apHardInts, iInt, cInts);
                        else
                            dbgcEventDisplayRange(pCmdHlp, g_aDbgcSxEvents[iEvt].pszName, g_aDbgcSxEvents[iEvt].enmDefault,
                                                  pDbgc->apSoftInts, iInt, cInts);
                        cHits++;
                    }
                }
            if (cHits == 0)
                return DBGCCmdHlpVBoxError(pCmdHlp, VERR_INVALID_PARAMETER, "Unknown event: '%s'\n", paArgs[iArg].u.pszString);
        }
    }

    return VINF_SUCCESS;
}



/**
 * List near symbol.
 *
 * @returns VBox status code.
 * @param   pCmdHlp     Pointer to command helper functions.
 * @param   pUVM        The user mode VM handle.
 * @param   pArg        Pointer to the address or symbol to lookup.
 */
static int dbgcDoListNear(PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR pArg)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    RTDBGSYMBOL Symbol;
    int         rc;
    if (pArg->enmType == DBGCVAR_TYPE_SYMBOL)
    {
        /*
         * Lookup the symbol address.
         */
        rc = DBGFR3AsSymbolByName(pUVM, pDbgc->hDbgAs, pArg->u.pszString, &Symbol, NULL);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "DBGFR3AsSymbolByName(,,%s,)\n", pArg->u.pszString);

        rc = DBGCCmdHlpPrintf(pCmdHlp, "%RTptr %s\n", Symbol.Value, Symbol.szName);
    }
    else
    {
        /*
         * Convert it to a flat GC address and lookup that address.
         */
        DBGCVAR AddrVar;
        rc = DBGCCmdHlpEval(pCmdHlp, &AddrVar, "%%(%DV)", pArg);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "%%(%DV)\n", pArg);

        RTINTPTR    offDisp;
        DBGFADDRESS Addr;
        rc = DBGFR3AsSymbolByAddr(pUVM, pDbgc->hDbgAs, DBGFR3AddrFromFlat(pDbgc->pUVM, &Addr, AddrVar.u.GCFlat),
                                  RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                  &offDisp, &Symbol, NULL);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "DBGFR3AsSymbolByAddr(,,%RGv,,)\n", AddrVar.u.GCFlat);

        if (!offDisp)
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%DV %s", &AddrVar, Symbol.szName);
        else if (offDisp > 0)
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%DV %s + %RGv", &AddrVar, Symbol.szName, offDisp);
        else
            rc = DBGCCmdHlpPrintf(pCmdHlp, "%DV %s - %RGv", &AddrVar, Symbol.szName, -offDisp);
        if (RT_SUCCESS(rc))
        {
            if (Symbol.cb > 0)
                rc = DBGCCmdHlpPrintf(pCmdHlp, " (LB %RGv)\n", Symbol.cb);
            else
                rc = DBGCCmdHlpPrintf(pCmdHlp, "\n");
        }
    }

    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'ln' (listnear) command.}
 */
static DECLCALLBACK(int) dbgcCmdListNear(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    if (!cArgs)
    {
        /*
         * Current cs:eip symbol.
         */
        DBGCVAR AddrVar;
        const char *pszFmtExpr = "%%(cs:eip)";
        int rc = DBGCCmdHlpEval(pCmdHlp, &AddrVar, pszFmtExpr);
        if (RT_FAILURE(rc))
            return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "%s\n", pszFmtExpr + 1);
        return dbgcDoListNear(pCmdHlp, pUVM, &AddrVar);
    }

/** @todo Fix the darn parser, it's resolving symbols specified as arguments before we get in here. */
    /*
     * Iterate arguments.
     */
    for (unsigned iArg = 0; iArg < cArgs; iArg++)
    {
        int rc = dbgcDoListNear(pCmdHlp, pUVM, &paArgs[iArg]);
        if (RT_FAILURE(rc))
            return rc;
    }

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * Matches the module patters against a module name.
 *
 * @returns true if matching, otherwise false.
 * @param   pszName     The module name.
 * @param   paArgs      The module pattern argument list.
 * @param   cArgs       Number of arguments.
 */
static bool dbgcCmdListModuleMatch(const char *pszName, PCDBGCVAR paArgs, unsigned cArgs)
{
    for (uint32_t i = 0; i < cArgs; i++)
        if (RTStrSimplePatternMatch(paArgs[i].u.pszString, pszName))
            return true;
    return false;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'ln' (list near) command.}
 */
static DECLCALLBACK(int) dbgcCmdListModules(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    bool const  fMappings   = pCmd->pszCmd[2] == 'o';
    bool const  fVerbose    = pCmd->pszCmd[strlen(pCmd->pszCmd) - 1] == 'v';
    PDBGC       pDbgc       = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Iterate the modules in the current address space and print info about
     * those matching the input.
     */
    RTDBGAS hAsCurAlias = pDbgc->hDbgAs;
    for (uint32_t iAs = 0;; iAs++)
    {
        RTDBGAS     hAs         = DBGFR3AsResolveAndRetain(pUVM, hAsCurAlias);
        uint32_t    cMods       = RTDbgAsModuleCount(hAs);
        for (uint32_t iMod = 0; iMod < cMods; iMod++)
        {
            RTDBGMOD hMod = RTDbgAsModuleByIndex(hAs, iMod);
            if (hMod != NIL_RTDBGMOD)
            {
                bool const          fDeferred       = RTDbgModIsDeferred(hMod);
                bool const          fExports        = RTDbgModIsExports(hMod);
                uint32_t const      cSegs           = fDeferred ? 1 : RTDbgModSegmentCount(hMod);
                const char * const  pszName         = RTDbgModName(hMod);
                const char * const  pszImgFile      = RTDbgModImageFile(hMod);
                const char * const  pszImgFileUsed  = RTDbgModImageFileUsed(hMod);
                const char * const  pszDbgFile      = RTDbgModDebugFile(hMod);
                if (    cArgs == 0
                    ||  dbgcCmdListModuleMatch(pszName, paArgs, cArgs))
                {
                    /*
                     * Find the mapping with the lower address, preferring a full
                     * image mapping, for the main line.
                     */
                    RTDBGASMAPINFO  aMappings[128];
                    uint32_t        cMappings = RT_ELEMENTS(aMappings);
                    int rc = RTDbgAsModuleQueryMapByIndex(hAs, iMod, &aMappings[0], &cMappings, 0 /*fFlags*/);
                    if (RT_SUCCESS(rc))
                    {
                        bool        fFull = false;
                        RTUINTPTR   uMin = RTUINTPTR_MAX;
                        for (uint32_t iMap = 0; iMap < cMappings; iMap++)
                            if (    aMappings[iMap].Address < uMin
                                &&  (   !fFull
                                     ||  aMappings[iMap].iSeg == NIL_RTDBGSEGIDX))
                                uMin = aMappings[iMap].Address;
                        if (!fVerbose || !pszImgFile)
                            DBGCCmdHlpPrintf(pCmdHlp, "%RGv %04x %s%s\n", (RTGCUINTPTR)uMin, cSegs, pszName,
                                             fExports ? " (exports)" : fDeferred ? " (deferred)" : "");
                        else
                            DBGCCmdHlpPrintf(pCmdHlp, "%RGv %04x %-12s  %s%s\n", (RTGCUINTPTR)uMin, cSegs, pszName, pszImgFile,
                                             fExports ? "  (exports)" : fDeferred ? "  (deferred)" : "");
                        if (fVerbose && pszImgFileUsed)
                            DBGCCmdHlpPrintf(pCmdHlp, "    Local image: %s\n", pszImgFileUsed);
                        if (fVerbose && pszDbgFile)
                            DBGCCmdHlpPrintf(pCmdHlp, "    Debug file:  %s\n", pszDbgFile);
                        if (fVerbose)
                        {
                            char szTmp[64];
                            RTTIMESPEC TimeSpec;
                            int64_t    secTs = 0;
                            if (RT_SUCCESS(RTDbgModImageQueryProp(hMod, RTLDRPROP_TIMESTAMP_SECONDS, &secTs, sizeof(secTs), NULL)))
                                DBGCCmdHlpPrintf(pCmdHlp, "    Timestamp:   %08RX64  %s\n", secTs,
                                                 RTTimeSpecToString(RTTimeSpecSetSeconds(&TimeSpec, secTs), szTmp, sizeof(szTmp)));
                            RTUUID Uuid;
                            if (RT_SUCCESS(RTDbgModImageQueryProp(hMod, RTLDRPROP_UUID, &Uuid, sizeof(Uuid), NULL)))
                                DBGCCmdHlpPrintf(pCmdHlp, "    UUID:        %RTuuid\n", &Uuid);
                        }

                        if (fMappings)
                        {
                            /* sort by address first - not very efficient. */
                            for (uint32_t i = 0; i + 1 < cMappings; i++)
                                for (uint32_t j = i + 1; j < cMappings; j++)
                                    if (aMappings[j].Address < aMappings[i].Address)
                                    {
                                        RTDBGASMAPINFO Tmp = aMappings[j];
                                        aMappings[j] = aMappings[i];
                                        aMappings[i] = Tmp;
                                    }

                            /* print */
                            if (   cMappings == 1
                                && aMappings[0].iSeg == NIL_RTDBGSEGIDX
                                && !fDeferred)
                            {
                                for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
                                {
                                    RTDBGSEGMENT SegInfo;
                                    rc = RTDbgModSegmentByIndex(hMod, iSeg, &SegInfo);
                                    if (RT_SUCCESS(rc))
                                    {
                                        if (SegInfo.uRva != RTUINTPTR_MAX)
                                            DBGCCmdHlpPrintf(pCmdHlp, "    %RGv %RGv #%02x %s\n",
                                                             (RTGCUINTPTR)(aMappings[0].Address + SegInfo.uRva),
                                                             (RTGCUINTPTR)SegInfo.cb, iSeg, SegInfo.szName);
                                        else
                                            DBGCCmdHlpPrintf(pCmdHlp, "    %*s %RGv #%02x %s\n",
                                                             sizeof(RTGCUINTPTR)*2, "noload",
                                                             (RTGCUINTPTR)SegInfo.cb, iSeg, SegInfo.szName);
                                    }
                                    else
                                        DBGCCmdHlpPrintf(pCmdHlp, "    Error query segment #%u: %Rrc\n", iSeg, rc);
                                }
                            }
                            else
                            {
                                for (uint32_t iMap = 0; iMap < cMappings; iMap++)
                                    if (aMappings[iMap].iSeg == NIL_RTDBGSEGIDX)
                                        DBGCCmdHlpPrintf(pCmdHlp, "    %RGv %RGv <everything>\n",
                                                         (RTGCUINTPTR)aMappings[iMap].Address,
                                                         (RTGCUINTPTR)RTDbgModImageSize(hMod));
                                    else if (!fDeferred)
                                    {
                                        RTDBGSEGMENT SegInfo;
                                        rc = RTDbgModSegmentByIndex(hMod, aMappings[iMap].iSeg, &SegInfo);
                                        if (RT_FAILURE(rc))
                                        {
                                            RT_ZERO(SegInfo);
                                            strcpy(SegInfo.szName, "error");
                                        }
                                        DBGCCmdHlpPrintf(pCmdHlp, "    %RGv %RGv #%02x %s\n",
                                                         (RTGCUINTPTR)aMappings[iMap].Address,
                                                         (RTGCUINTPTR)SegInfo.cb,
                                                         aMappings[iMap].iSeg, SegInfo.szName);
                                    }
                                    else
                                        DBGCCmdHlpPrintf(pCmdHlp, "    %RGv #%02x\n",
                                                         (RTGCUINTPTR)aMappings[iMap].Address, aMappings[iMap].iSeg);
                            }
                        }
                    }
                    else
                        DBGCCmdHlpPrintf(pCmdHlp, "%.*s %04x %s (rc=%Rrc)\n",
                                         sizeof(RTGCPTR) * 2, "???????????", cSegs, pszName, rc);
                    /** @todo missing address space API for enumerating the mappings. */
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hAs);

        /* For DBGF_AS_RC_AND_GC_GLOBAL we're required to do more work. */
        if (hAsCurAlias != DBGF_AS_RC_AND_GC_GLOBAL)
            break;
        AssertBreak(iAs == 0);
        hAsCurAlias = DBGF_AS_GLOBAL;
    }

    NOREF(pCmd);
    return VINF_SUCCESS;
}



/**
 * @callback_method_impl{FNDBGCCMD, The 'x' (examine symbols) command.}
 */
static DECLCALLBACK(int) dbgcCmdListSymbols(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_BUG);

    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Allowed is either a single * to match everything or the Module!Symbol style
     * which requiresa ! to separate module and symbol.
     */
    bool fDumpAll = strcmp(paArgs[0].u.pszString, "*") == 0;
    const char *pszModule = NULL;
    size_t cchModule = 0;
    const char *pszSymbol = NULL;
    if (!fDumpAll)
    {
        const char *pszDelimiter = strchr(paArgs[0].u.pszString, '!');
        if (!pszDelimiter)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid search string '%s' for '%s'. Valid are either '*' or the form <Module>!<Symbol> where the <Module> and <Symbol> can contain wildcards",
                              paArgs[0].u.pszString, pCmd->pszCmd);

        pszModule = paArgs[0].u.pszString;
        cchModule = pszDelimiter - pszModule;
        pszSymbol = pszDelimiter + 1;
    }

    /*
     * Iterate the modules in the current address space and print info about
     * those matching the input.
     */
    RTDBGAS hAsCurAlias = pDbgc->hDbgAs;
    for (uint32_t iAs = 0;; iAs++)
    {
        RTDBGAS     hAs         = DBGFR3AsResolveAndRetain(pUVM, hAsCurAlias);
        uint32_t    cMods       = RTDbgAsModuleCount(hAs);
        for (uint32_t iMod = 0; iMod < cMods; iMod++)
        {
            RTDBGMOD hMod = RTDbgAsModuleByIndex(hAs, iMod);
            if (hMod != NIL_RTDBGMOD)
            {
                const char *pszModName = RTDbgModName(hMod);
                if (   fDumpAll
                    || RTStrSimplePatternNMatch(pszModule, cchModule, pszModName, strlen(pszModName)))
                {
                    RTDBGASMAPINFO  aMappings[128];
                    uint32_t        cMappings = RT_ELEMENTS(aMappings);
                    RTUINTPTR       uMapping = 0;

                    /* Get the minimum mapping address of the module so we can print absolute values for the symbol later on. */
                    int rc = RTDbgAsModuleQueryMapByIndex(hAs, iMod, &aMappings[0], &cMappings, 0 /*fFlags*/);
                    if (RT_SUCCESS(rc))
                    {
                        uMapping = RTUINTPTR_MAX;
                        for (uint32_t iMap = 0; iMap < cMappings; iMap++)
                            if (aMappings[iMap].Address < uMapping)
                                uMapping = aMappings[iMap].Address;
                    }

                    /* Go through the symbols and print any matches. */
                    uint32_t cSyms = RTDbgModSymbolCount(hMod);
                    for (uint32_t iSym = 0; iSym < cSyms; iSym++)
                    {
                        RTDBGSYMBOL SymInfo;
                        rc = RTDbgModSymbolByOrdinal(hMod, iSym, &SymInfo);
                        if (   RT_SUCCESS(rc)
                            && (   fDumpAll
                                || RTStrSimplePatternMatch(pszSymbol, &SymInfo.szName[0])))
                            DBGCCmdHlpPrintf(pCmdHlp, "%RGv    %s!%s\n", uMapping + RTDbgModSegmentRva(hMod, SymInfo.iSeg) + (RTGCUINTPTR)SymInfo.Value, pszModName, &SymInfo.szName[0]);
                    }
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hAs);

        /* For DBGF_AS_RC_AND_GC_GLOBAL we're required to do more work. */
        if (hAsCurAlias != DBGF_AS_RC_AND_GC_GLOBAL)
            break;
        AssertBreak(iAs == 0);
        hAsCurAlias = DBGF_AS_GLOBAL;
    }

    RT_NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'tflowc' (clear trace flow) command.}
 */
static DECLCALLBACK(int) dbgcCmdTraceFlowClear(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Enumerate the arguments.
     */
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int     rc    = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iFlowTraceMod = (uint32_t)paArgs[iArg].u.u64Number;
            if (iFlowTraceMod == paArgs[iArg].u.u64Number)
            {
                PDBGCTFLOW pFlowTrace = dbgcFlowTraceModGet(pDbgc, iFlowTraceMod);
                if (pFlowTrace)
                {
                    rc = DBGFR3FlowTraceModRelease(pFlowTrace->hTraceFlowMod);
                    if (RT_FAILURE(rc))
                        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowTraceModRelease failed for flow trace module %#x", iFlowTraceMod);
                    rc = DBGFR3FlowRelease(pFlowTrace->hFlow);
                    if (RT_FAILURE(rc))
                        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowRelease failed for flow trace module %#x", iFlowTraceMod);
                    dbgcFlowTraceModDelete(pDbgc, iFlowTraceMod);
                }
                else
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_NOT_FOUND, "Flow trace module %#x doesn't exist", iFlowTraceMod);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Flow trace mod id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGCTFLOW pIt, pItNext;
            RTListForEachSafe(&pDbgc->LstTraceFlowMods, pIt, pItNext, DBGCTFLOW, NdTraceFlow)
            {
                int rc2 = DBGFR3FlowTraceModRelease(pIt->hTraceFlowMod);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3FlowTraceModDisable failed for flow trace module %#x", pIt->iTraceFlowMod);
                dbgcFlowTraceModDelete(pDbgc, pIt->iTraceFlowMod);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'tflowd' (disable trace flow) command.}
 */
static DECLCALLBACK(int) dbgcCmdTraceFlowDisable(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Enumerate the arguments.
     */
    RT_NOREF1(pUVM);
    int rc = VINF_SUCCESS;
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iFlowTraceMod = (uint32_t)paArgs[iArg].u.u64Number;
            if (iFlowTraceMod == paArgs[iArg].u.u64Number)
            {
                PDBGCTFLOW pFlowTrace = dbgcFlowTraceModGet(pDbgc, iFlowTraceMod);
                if (pFlowTrace)
                {
                    rc = DBGFR3FlowTraceModDisable(pFlowTrace->hTraceFlowMod);
                    if (RT_FAILURE(rc))
                        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowTraceModDisable failed for flow trace module %#x", iFlowTraceMod);
                }
                else
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_NOT_FOUND, "Flow trace module %#x doesn't exist", iFlowTraceMod);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Breakpoint id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGCTFLOW pIt;
            RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
            {
                int rc2 = DBGFR3FlowTraceModDisable(pIt->hTraceFlowMod);
                if (RT_FAILURE(rc2))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc2, "DBGFR3FlowTraceModDisable failed for flow trace module %#x",
                                          pIt->iTraceFlowMod);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'tflowe' (enable trace flow) command.}
 */
static DECLCALLBACK(int) dbgcCmdTraceFlowEnable(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, -1, cArgs <= 2);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 0 || DBGCVAR_ISPOINTER(paArgs[0].enmType));

    if (!cArgs && !DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Don't know where to start disassembling");

    /*
     * Check the desired mode.
     */
    unsigned fFlags =  DBGF_DISAS_FLAGS_UNPATCHED_BYTES | DBGF_DISAS_FLAGS_ANNOTATE_PATCHED | DBGF_DISAS_FLAGS_DEFAULT_MODE;

    /** @todo should use DBGFADDRESS for everything */

    /*
     * Find address.
     */
    if (!cArgs)
    {
        if (!DBGCVAR_ISPOINTER(pDbgc->DisasmPos.enmType))
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            /** @todo Batch query CS, RIP, CPU mode and flags. */
            PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, pDbgc->idCpu);
            if (CPUMIsGuestIn64BitCode(pVCpu))
            {
                pDbgc->DisasmPos.enmType    = DBGCVAR_TYPE_GC_FLAT;
                pDbgc->SourcePos.u.GCFlat   = CPUMGetGuestRIP(pVCpu);
            }
            else
            {
                pDbgc->DisasmPos.enmType     = DBGCVAR_TYPE_GC_FAR;
                pDbgc->SourcePos.u.GCFar.off = CPUMGetGuestEIP(pVCpu);
                pDbgc->SourcePos.u.GCFar.sel = CPUMGetGuestCS(pVCpu);
                if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE
                    && (CPUMGetGuestEFlags(pVCpu) & X86_EFL_VM))
                {
                    fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
                    fFlags |= DBGF_DISAS_FLAGS_16BIT_REAL_MODE;
                }
            }
#endif

            fFlags |= DBGF_DISAS_FLAGS_CURRENT_GUEST;
        }
        else if ((fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_DEFAULT_MODE && pDbgc->fDisasm)
        {
            fFlags &= ~DBGF_DISAS_FLAGS_MODE_MASK;
            fFlags |= pDbgc->fDisasm & DBGF_DISAS_FLAGS_MODE_MASK;
        }
        pDbgc->DisasmPos.enmRangeType = DBGCVAR_RANGE_NONE;
    }
    else
        pDbgc->DisasmPos = paArgs[0];
    pDbgc->pLastPos = &pDbgc->DisasmPos;


    int rc;
#if 0 /** @todo unused right now */
    /*
     * Convert physical and host addresses to guest addresses.
     */
    RTDBGAS hDbgAs = pDbgc->hDbgAs;
    switch (pDbgc->DisasmPos.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
        case DBGCVAR_TYPE_GC_FAR:
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            hDbgAs = DBGF_AS_PHYS;
            /* fall thru */
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        {
            DBGCVAR VarTmp;
            rc = DBGCCmdHlpEval(pCmdHlp, &VarTmp, "%%(%Dv)", &pDbgc->DisasmPos);
            if (RT_FAILURE(rc))
                return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "failed to evaluate '%%(%Dv)'", &pDbgc->DisasmPos);
            pDbgc->DisasmPos = VarTmp;
            break;
        }
        default: AssertFailed(); break;
    }
#endif

    DBGFADDRESS CurAddr;
    if (   (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_16BIT_REAL_MODE
        && pDbgc->DisasmPos.enmType == DBGCVAR_TYPE_GC_FAR)
        DBGFR3AddrFromFlat(pUVM, &CurAddr, ((uint32_t)pDbgc->DisasmPos.u.GCFar.sel << 4) + pDbgc->DisasmPos.u.GCFar.off);
    else
    {
        rc = DBGCCmdHlpVarToDbgfAddr(pCmdHlp, &pDbgc->DisasmPos, &CurAddr);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToDbgfAddr failed on '%Dv'", &pDbgc->DisasmPos);
    }

    DBGFFLOW hCfg;
    rc = DBGFR3FlowCreate(pUVM, pDbgc->idCpu, &CurAddr, 0 /*cbDisasmMax*/,
                          DBGF_FLOW_CREATE_F_TRY_RESOLVE_INDIRECT_BRANCHES, fFlags, &hCfg);
    if (RT_SUCCESS(rc))
    {
        /* Create a probe. */
        DBGFFLOWTRACEPROBE hFlowTraceProbe = NULL;
        DBGFFLOWTRACEPROBE hFlowTraceProbeExit = NULL;
        DBGFFLOWTRACEPROBEENTRY Entry;
        DBGFFLOWTRACEMOD hFlowTraceMod = NULL;
        uint32_t iTraceModId = 0;

        RT_ZERO(Entry);
        Entry.enmType = DBGFFLOWTRACEPROBEENTRYTYPE_DEBUGGER;

        rc = DBGFR3FlowTraceProbeCreate(pUVM, NULL, &hFlowTraceProbe);
        if (RT_SUCCESS(rc))
            rc = DBGFR3FlowTraceProbeCreate(pUVM, NULL, &hFlowTraceProbeExit);
        if (RT_SUCCESS(rc))
            rc = DBGFR3FlowTraceProbeEntriesAdd(hFlowTraceProbeExit, &Entry, 1 /*cEntries*/);
        if (RT_SUCCESS(rc))
            rc = DBGFR3FlowTraceModCreateFromFlowGraph(pUVM, VMCPUID_ANY, hCfg, NULL,
                                                       hFlowTraceProbe, hFlowTraceProbe,
                                                       hFlowTraceProbeExit, &hFlowTraceMod);
        if (RT_SUCCESS(rc))
            rc = dbgcFlowTraceModAdd(pDbgc, hFlowTraceMod, hCfg, &iTraceModId);
        if (RT_SUCCESS(rc))
            rc = DBGFR3FlowTraceModEnable(hFlowTraceMod, 0, 0);
        if (RT_SUCCESS(rc))
            DBGCCmdHlpPrintf(pCmdHlp, "Enabled execution flow tracing %u at %RGv\n",
                             iTraceModId, CurAddr.FlatPtr);

        if (hFlowTraceProbe)
            DBGFR3FlowTraceProbeRelease(hFlowTraceProbe);
        if (hFlowTraceProbeExit)
            DBGFR3FlowTraceProbeRelease(hFlowTraceProbeExit);
    }
    else
        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowCreate failed on '%Dv'", &pDbgc->DisasmPos);

    NOREF(pCmd);
    return rc;
}


/**
 * Enumerates and prints all records contained in the given flow tarce module.
 *
 * @returns VBox status code.
 * @param   pCmd          The command.
 * @param   pCmdHlp       The command helpers.
 * @param   hFlowTraceMod The flow trace module to print.
 * @param   hFlow         The control flow graph assoicated with the given module.
 * @param   iFlowTraceMod The flow trace module identifier.
 */
static int dbgcCmdTraceFlowPrintOne(PDBGCCMDHLP pCmdHlp, PCDBGCCMD pCmd, DBGFFLOWTRACEMOD hFlowTraceMod,
                                    DBGFFLOW hFlow, uint32_t iFlowTraceMod)
{
    RT_NOREF(hFlow);

    DBGFFLOWTRACEREPORT hFlowTraceReport;
    int rc = DBGFR3FlowTraceModQueryReport(hFlowTraceMod, &hFlowTraceReport);
    if (RT_SUCCESS(rc))
    {
        uint32_t cRecords = DBGFR3FlowTraceReportGetRecordCount(hFlowTraceReport);
        DBGCCmdHlpPrintf(pCmdHlp, "Report for flow trace module %#x (%u records):\n",
                         iFlowTraceMod, cRecords);

        PDBGCFLOWBBDUMP paDumpBb = (PDBGCFLOWBBDUMP)RTMemTmpAllocZ(cRecords * sizeof(DBGCFLOWBBDUMP));
        if (RT_LIKELY(paDumpBb))
        {
            /* Query the basic block referenced for each record and calculate the size. */
            for (uint32_t i = 0; i < cRecords && RT_SUCCESS(rc); i++)
            {
                DBGFFLOWTRACERECORD hRec = NULL;
                rc = DBGFR3FlowTraceReportQueryRecord(hFlowTraceReport, i, &hRec);
                if (RT_SUCCESS(rc))
                {
                    DBGFADDRESS Addr;
                    DBGFR3FlowTraceRecordGetAddr(hRec, &Addr);

                    DBGFFLOWBB hFlowBb = NULL;
                    rc = DBGFR3FlowQueryBbByAddress(hFlow, &Addr, &hFlowBb);
                    if (RT_SUCCESS(rc))
                        dbgcCmdUnassembleCfgDumpCalcBbSize(hFlowBb, &paDumpBb[i]);

                    DBGFR3FlowTraceRecordRelease(hRec);
                }
            }

            if (RT_SUCCESS(rc))
            {
                /* Calculate the ASCII screen dimensions and create one. */
                uint32_t cchWidth = 0;
                uint32_t cchHeight = 0;
                for (unsigned i = 0; i < cRecords; i++)
                {
                    PDBGCFLOWBBDUMP pDumpBb = &paDumpBb[i];
                    cchWidth = RT_MAX(cchWidth, pDumpBb->cchWidth);
                    cchHeight += pDumpBb->cchHeight;

                    /* Incomplete blocks don't have a successor. */
                    if (DBGFR3FlowBbGetFlags(pDumpBb->hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
                        continue;

                    cchHeight += 2; /* For the arrow down to the next basic block. */
                }


                DBGCSCREEN hScreen = NULL;
                rc = dbgcScreenAsciiCreate(&hScreen, cchWidth, cchHeight);
                if (RT_SUCCESS(rc))
                {
                    uint32_t uY = 0;

                    /* Dump the basic blocks and connections to the immediate successor. */
                    for (unsigned i = 0; i < cRecords; i++)
                    {
                        paDumpBb[i].uStartX = (cchWidth - paDumpBb[i].cchWidth) / 2;
                        paDumpBb[i].uStartY = uY;
                        dbgcCmdUnassembleCfgDumpBb(&paDumpBb[i], hScreen);
                        uY += paDumpBb[i].cchHeight;

                        /* Incomplete blocks don't have a successor. */
                        if (DBGFR3FlowBbGetFlags(paDumpBb[i].hFlowBb) & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
                            continue;

                        if (DBGFR3FlowBbGetType(paDumpBb[i].hFlowBb) != DBGFFLOWBBENDTYPE_EXIT)
                        {
                            /* Draw the arrow down to the next block. */
                            dbgcScreenAsciiDrawCharacter(hScreen, cchWidth / 2, uY,
                                                         '|', DBGCSCREENCOLOR_BLUE_BRIGHT);
                            uY++;
                            dbgcScreenAsciiDrawCharacter(hScreen, cchWidth / 2, uY,
                                                         'V', DBGCSCREENCOLOR_BLUE_BRIGHT);
                            uY++;
                        }
                    }

                    rc = dbgcScreenAsciiBlit(hScreen, dbgcCmdUnassembleCfgBlit, pCmdHlp, false /*fUseColor*/);
                    dbgcScreenAsciiDestroy(hScreen);
                }
                else
                    rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to create virtual screen for flow trace module %#x", iFlowTraceMod);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to query all records of flow trace module %#x", iFlowTraceMod);

            for (unsigned i = 0; i < cRecords; i++)
            {
                if (paDumpBb[i].hFlowBb)
                    DBGFR3FlowBbRelease(paDumpBb[i].hFlowBb);
            }

            RTMemTmpFree(paDumpBb);
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to allocate memory for %u records", cRecords);

        DBGFR3FlowTraceReportRelease(hFlowTraceReport);
    }
    else
        rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Failed to query report for flow trace module %#x", iFlowTraceMod);

    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'tflowp' (print trace flow) command.}
 */
static DECLCALLBACK(int) dbgcCmdTraceFlowPrint(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Enumerate the arguments.
     */
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int     rc    = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iFlowTraceMod = (uint32_t)paArgs[iArg].u.u64Number;
            if (iFlowTraceMod == paArgs[iArg].u.u64Number)
            {
                PDBGCTFLOW pFlowTrace = dbgcFlowTraceModGet(pDbgc, iFlowTraceMod);
                if (pFlowTrace)
                    rc = dbgcCmdTraceFlowPrintOne(pCmdHlp, pCmd, pFlowTrace->hTraceFlowMod,
                                                  pFlowTrace->hFlow, pFlowTrace->iTraceFlowMod);
                else
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_NOT_FOUND, "Flow trace module %#x doesn't exist", iFlowTraceMod);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Flow trace mod id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGCTFLOW pIt;
            RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
            {
                rc = dbgcCmdTraceFlowPrintOne(pCmdHlp, pCmd, pIt->hTraceFlowMod,
                                              pIt->hFlow, pIt->iTraceFlowMod);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'tflowr' (reset trace flow) command.}
 */
static DECLCALLBACK(int) dbgcCmdTraceFlowReset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Enumerate the arguments.
     */
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int     rc    = VINF_SUCCESS;
    for (unsigned iArg = 0; iArg < cArgs && RT_SUCCESS(rc); iArg++)
    {
        if (paArgs[iArg].enmType != DBGCVAR_TYPE_STRING)
        {
            /* one */
            uint32_t iFlowTraceMod = (uint32_t)paArgs[iArg].u.u64Number;
            if (iFlowTraceMod == paArgs[iArg].u.u64Number)
            {
                PDBGCTFLOW pFlowTrace = dbgcFlowTraceModGet(pDbgc, iFlowTraceMod);
                if (pFlowTrace)
                {
                    rc = DBGFR3FlowTraceModClear(pFlowTrace->hTraceFlowMod);
                    if (RT_FAILURE(rc))
                        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowTraceModClear failed for flow trace module %#x", iFlowTraceMod);
                }
                else
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_NOT_FOUND, "Flow trace module %#x doesn't exist", iFlowTraceMod);
            }
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Flow trace mod id %RX64 is too large", paArgs[iArg].u.u64Number);
        }
        else if (!strcmp(paArgs[iArg].u.pszString, "all"))
        {
            /* all */
            PDBGCTFLOW pIt;
            RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
            {
                rc = DBGFR3FlowTraceModClear(pIt->hTraceFlowMod);
                if (RT_FAILURE(rc))
                    rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3FlowTraceModClear failed for flow trace module %#x", pIt->iTraceFlowMod);
            }
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid argument '%s'", paArgs[iArg].u.pszString);
    }
    return rc;
}



/**
 * @callback_method_impl{FNDBGCFUNC, Reads a unsigned 8-bit value.}
 */
static DECLCALLBACK(int) dbgcFuncReadU8(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                        PDBGCVAR pResult)
{
    RT_NOREF1(pUVM);
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(DBGCVAR_ISPOINTER(paArgs[0].enmType), VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmRangeType == DBGCVAR_RANGE_NONE, VERR_DBGC_PARSE_BUG);

    uint8_t b;
    int rc = DBGCCmdHlpMemRead(pCmdHlp, &b, sizeof(b), &paArgs[0], NULL);
    if (RT_FAILURE(rc))
        return rc;
    DBGCVAR_INIT_NUMBER(pResult, b);

    NOREF(pFunc);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC, Reads a unsigned 16-bit value.}
 */
static DECLCALLBACK(int) dbgcFuncReadU16(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                         PDBGCVAR pResult)
{
    RT_NOREF1(pUVM);
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(DBGCVAR_ISPOINTER(paArgs[0].enmType), VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmRangeType == DBGCVAR_RANGE_NONE, VERR_DBGC_PARSE_BUG);

    uint16_t u16;
    int rc = DBGCCmdHlpMemRead(pCmdHlp, &u16, sizeof(u16), &paArgs[0], NULL);
    if (RT_FAILURE(rc))
        return rc;
    DBGCVAR_INIT_NUMBER(pResult, u16);

    NOREF(pFunc);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC, Reads a unsigned 32-bit value.}
 */
static DECLCALLBACK(int) dbgcFuncReadU32(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                         PDBGCVAR pResult)
{
    RT_NOREF1(pUVM);
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(DBGCVAR_ISPOINTER(paArgs[0].enmType), VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmRangeType == DBGCVAR_RANGE_NONE, VERR_DBGC_PARSE_BUG);

    uint32_t u32;
    int rc = DBGCCmdHlpMemRead(pCmdHlp, &u32, sizeof(u32), &paArgs[0], NULL);
    if (RT_FAILURE(rc))
        return rc;
    DBGCVAR_INIT_NUMBER(pResult, u32);

    NOREF(pFunc);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC, Reads a unsigned 64-bit value.}
 */
static DECLCALLBACK(int) dbgcFuncReadU64(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                         PDBGCVAR pResult)
{
    RT_NOREF1(pUVM);
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(DBGCVAR_ISPOINTER(paArgs[0].enmType), VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmRangeType == DBGCVAR_RANGE_NONE, VERR_DBGC_PARSE_BUG);

    uint64_t u64;
    int rc = DBGCCmdHlpMemRead(pCmdHlp, &u64, sizeof(u64), &paArgs[0], NULL);
    if (RT_FAILURE(rc))
        return rc;
    DBGCVAR_INIT_NUMBER(pResult, u64);

    NOREF(pFunc);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC, Reads a unsigned pointer-sized value.}
 */
static DECLCALLBACK(int) dbgcFuncReadPtr(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                         PDBGCVAR pResult)
{
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    AssertReturn(DBGCVAR_ISPOINTER(paArgs[0].enmType), VERR_DBGC_PARSE_BUG);
    AssertReturn(paArgs[0].enmRangeType == DBGCVAR_RANGE_NONE, VERR_DBGC_PARSE_BUG);

    CPUMMODE enmMode = DBGCCmdHlpGetCpuMode(pCmdHlp);
    if (enmMode == CPUMMODE_LONG)
        return dbgcFuncReadU64(pFunc, pCmdHlp, pUVM, paArgs, cArgs, pResult);
    return dbgcFuncReadU32(pFunc, pCmdHlp, pUVM, paArgs, cArgs, pResult);
}


/**
 * @callback_method_impl{FNDBGCFUNC, The hi(value) function implementation.}
 */
static DECLCALLBACK(int) dbgcFuncHi(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                    PDBGCVAR pResult)
{
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);

    uint16_t uHi;
    switch (paArgs[0].enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:  uHi = (uint16_t)(paArgs[0].u.GCFlat >> 16); break;
        case DBGCVAR_TYPE_GC_FAR:   uHi = (uint16_t)paArgs[0].u.GCFar.sel; break;
        case DBGCVAR_TYPE_GC_PHYS:  uHi = (uint16_t)(paArgs[0].u.GCPhys >> 16); break;
        case DBGCVAR_TYPE_HC_FLAT:  uHi = (uint16_t)((uintptr_t)paArgs[0].u.pvHCFlat >> 16); break;
        case DBGCVAR_TYPE_HC_PHYS:  uHi = (uint16_t)(paArgs[0].u.HCPhys >> 16); break;
        case DBGCVAR_TYPE_NUMBER:   uHi = (uint16_t)(paArgs[0].u.u64Number >> 16); break;
        default:
            AssertFailedReturn(VERR_DBGC_PARSE_BUG);
    }
    DBGCVAR_INIT_NUMBER(pResult, uHi);
    DBGCVAR_SET_RANGE(pResult, paArgs[0].enmRangeType, paArgs[0].u64Range);

    NOREF(pFunc); NOREF(pCmdHlp); NOREF(pUVM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC, The low(value) function implementation.}
 */
static DECLCALLBACK(int) dbgcFuncLow(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                     PDBGCVAR pResult)
{
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);

    uint16_t uLow;
    switch (paArgs[0].enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:  uLow = (uint16_t)paArgs[0].u.GCFlat; break;
        case DBGCVAR_TYPE_GC_FAR:   uLow = (uint16_t)paArgs[0].u.GCFar.off; break;
        case DBGCVAR_TYPE_GC_PHYS:  uLow = (uint16_t)paArgs[0].u.GCPhys; break;
        case DBGCVAR_TYPE_HC_FLAT:  uLow = (uint16_t)(uintptr_t)paArgs[0].u.pvHCFlat; break;
        case DBGCVAR_TYPE_HC_PHYS:  uLow = (uint16_t)paArgs[0].u.HCPhys; break;
        case DBGCVAR_TYPE_NUMBER:   uLow = (uint16_t)paArgs[0].u.u64Number; break;
        default:
            AssertFailedReturn(VERR_DBGC_PARSE_BUG);
    }
    DBGCVAR_INIT_NUMBER(pResult, uLow);
    DBGCVAR_SET_RANGE(pResult, paArgs[0].enmRangeType, paArgs[0].u64Range);

    NOREF(pFunc); NOREF(pCmdHlp); NOREF(pUVM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCFUNC,The low(value) function implementation.}
 */
static DECLCALLBACK(int) dbgcFuncNot(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                     PDBGCVAR pResult)
{
    AssertReturn(cArgs == 1, VERR_DBGC_PARSE_BUG);
    NOREF(pFunc); NOREF(pCmdHlp); NOREF(pUVM);
    return DBGCCmdHlpEval(pCmdHlp, pResult, "!(%Dv)", &paArgs[0]);
}


/** Generic pointer argument wo/ range. */
static const DBGCVARDESC    g_aArgPointerWoRange[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,    DBGCVAR_CAT_POINTER_NO_RANGE, 0,                              "value",        "Address or number." },
};

/** Generic pointer or number argument. */
static const DBGCVARDESC    g_aArgPointerNumber[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,      DBGCVAR_CAT_POINTER_NUMBER, 0,                              "value",        "Address or number." },
};



/** Function descriptors for the CodeView / WinDbg emulation.
 * The emulation isn't attempting to be identical, only somewhat similar.
 */
const DBGCFUNC g_aFuncsCodeView[] =
{
    { "by",     1, 1,   &g_aArgPointerWoRange[0],   RT_ELEMENTS(g_aArgPointerWoRange),  0, dbgcFuncReadU8,  "address", "Reads a byte at the given address." },
    { "dwo",    1, 1,   &g_aArgPointerWoRange[0],   RT_ELEMENTS(g_aArgPointerWoRange),  0, dbgcFuncReadU32, "address", "Reads a 32-bit value at the given address." },
    { "hi",     1, 1,   &g_aArgPointerNumber[0],    RT_ELEMENTS(g_aArgPointerNumber),   0, dbgcFuncHi,      "value", "Returns the high 16-bit bits of a value." },
    { "low",    1, 1,   &g_aArgPointerNumber[0],    RT_ELEMENTS(g_aArgPointerNumber),   0, dbgcFuncLow,     "value", "Returns the low 16-bit bits of a value." },
    { "not",    1, 1,   &g_aArgPointerNumber[0],    RT_ELEMENTS(g_aArgPointerNumber),   0, dbgcFuncNot,     "address", "Boolean NOT." },
    { "poi",    1, 1,   &g_aArgPointerWoRange[0],   RT_ELEMENTS(g_aArgPointerWoRange),  0, dbgcFuncReadPtr, "address", "Reads a pointer sized (CS) value at the given address." },
    { "qwo",    1, 1,   &g_aArgPointerWoRange[0],   RT_ELEMENTS(g_aArgPointerWoRange),  0, dbgcFuncReadU64, "address", "Reads a 32-bit value at the given address." },
    { "wo",     1, 1,   &g_aArgPointerWoRange[0],   RT_ELEMENTS(g_aArgPointerWoRange),  0, dbgcFuncReadU16, "address", "Reads a 16-bit value at the given address." },
};

/** The number of functions in the CodeView/WinDbg emulation. */
const uint32_t g_cFuncsCodeView = RT_ELEMENTS(g_aFuncsCodeView);

