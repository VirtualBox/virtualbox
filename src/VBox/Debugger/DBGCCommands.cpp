/* $Id: DBGCCommands.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Native Commands.
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
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/env.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <stdlib.h>
#include <stdio.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDBGCCMD dbgcCmdHelp;
static FNDBGCCMD dbgcCmdQuit;
static FNDBGCCMD dbgcCmdStop;
static FNDBGCCMD dbgcCmdDetect;
static FNDBGCCMD dbgcCmdDmesg;
static FNDBGCCMD dbgcCmdCpu;
static FNDBGCCMD dbgcCmdInfo;
static FNDBGCCMD dbgcCmdLog;
static FNDBGCCMD dbgcCmdLogDest;
static FNDBGCCMD dbgcCmdLogFlags;
static FNDBGCCMD dbgcCmdLogFlush;
static FNDBGCCMD dbgcCmdFormat;
static FNDBGCCMD dbgcCmdLoadImage;
static FNDBGCCMD dbgcCmdLoadInMem;
static FNDBGCCMD dbgcCmdLoadMap;
static FNDBGCCMD dbgcCmdLoadSeg;
static FNDBGCCMD dbgcCmdMultiStep;
static FNDBGCCMD dbgcCmdUnload;
static FNDBGCCMD dbgcCmdSet;
static FNDBGCCMD dbgcCmdUnset;
static FNDBGCCMD dbgcCmdLoadVars;
static FNDBGCCMD dbgcCmdShowVars;
static FNDBGCCMD dbgcCmdSleep;
static FNDBGCCMD dbgcCmdLoadPlugIn;
static FNDBGCCMD dbgcCmdUnloadPlugIn;
static FNDBGCCMD dbgcCmdHarakiri;
static FNDBGCCMD dbgcCmdEcho;
static FNDBGCCMD dbgcCmdRunScript;
static FNDBGCCMD dbgcCmdWriteCore;
static FNDBGCCMD dbgcCmdWriteGstMem;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** One argument of any kind. */
static const DBGCVARDESC    g_aArgAny[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_ANY,        0,                              "var",          "Any type of argument." },
};

/** Multiple string arguments (min 1). */
static const DBGCVARDESC    g_aArgMultiStr[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "strings",      "One or more strings." },
};

/** Filename string. */
static const DBGCVARDESC    g_aArgFilename[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "path",         "Filename string." },
};


/** 'cpu' arguments. */
static const DBGCVARDESC    g_aArgCpu[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,     DBGCVAR_CAT_NUMBER_NO_RANGE, 0,                              "idCpu",        "CPU ID" },
};


/** 'dmesg' arguments. */
static const DBGCVARDESC    g_aArgDmesg[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,     DBGCVAR_CAT_NUMBER_NO_RANGE, 0,                              "messages",     "Limit the output to the last N messages. (optional)" },
};


/** 'dumpimage' arguments. */
static const DBGCVARDESC    g_aArgDumpImage[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_OPTION_STRING, 0,                           "options",      "Options." },
    {  1,           ~0U,        DBGCVAR_CAT_POINTER,    0,                              "address",      "Address of image to dump." },
};


/** 'help' arguments. */
static const DBGCVARDESC    g_aArgHelp[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "cmd/op",       "Zero or more command or operator names." },
};


/** 'info' arguments. */
static const DBGCVARDESC    g_aArgInfo[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "info",         "The name of the info to display." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "args",         "String arguments to the handler." },
};


/** loadimage arguments. */
static const DBGCVARDESC    g_aArgLoadImage[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "filename",     "Filename string." },
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "The module address." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "name",         "The module name. (optional)" },
};


/** loadmap arguments. */
static const DBGCVARDESC    g_aArgLoadMap[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "filename",     "Filename string." },
    {  1,           1,          DBGCVAR_CAT_POINTER,    DBGCVD_FLAGS_DEP_PREV,          "address",      "The module address." },
    {  0,           1,          DBGCVAR_CAT_STRING,     DBGCVD_FLAGS_DEP_PREV,          "name",         "The module name. Empty string means default. (optional)" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "subtrahend",   "Value to subtract from the addresses in the map file to rebase it correctly to address. (optional)" },
    {  0,           1,          DBGCVAR_CAT_NUMBER,     DBGCVD_FLAGS_DEP_PREV,          "seg",          "The module segment number (0-based). (optional)" },
};


/** loadinmem arguments. */
static const DBGCVARDESC    g_aArgLoadInMem[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "The module address." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "name",         "The module name. (optional)" },
};


/** loadseg arguments. */
static const DBGCVARDESC    g_aArgLoadSeg[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "filename",     "Filename string." },
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "The module address." },
    {  1,           1,          DBGCVAR_CAT_NUMBER,     0,                              "seg",          "The module segment number (0-based)." },
    {  0,           1,          DBGCVAR_CAT_STRING,     DBGCVD_FLAGS_DEP_PREV,          "name",         "The module name. Empty string means default. (optional)" },
};


/** log arguments. */
static const DBGCVARDESC    g_aArgLog[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "groups",       "Group modifier string (quote it!)." }
};


/** logdest arguments. */
static const DBGCVARDESC    g_aArgLogDest[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "dests",        "Destination modifier string (quote it!)." }
};


/** logflags arguments. */
static const DBGCVARDESC    g_aArgLogFlags[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "flags",        "Flag modifier string (quote it!)." }
};

/** multistep arguments. */
static const DBGCVARDESC    g_aArgMultiStep[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER_NO_RANGE, 0,                         "count",        "Number of steps to take, defaults to 64." },
    {  0,           1,          DBGCVAR_CAT_NUMBER_NO_RANGE, DBGCVD_FLAGS_DEP_PREV,     "stride",       "The length of each step, defaults to 1." },
};

/** ntrbtree arguments. */
static const DBGCVARDESC    g_aArgNtRbTree[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_GC_POINTER, 0,                              "rootaddr",     "The address of _RTL_RB_TREE" },
};

/** loadplugin, unloadplugin. */
static const DBGCVARDESC    g_aArgPlugIn[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "plugin",       "Plug-in name or filename." },
};


/** 'set' arguments */
static const DBGCVARDESC    g_aArgSet[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_SYMBOL,     0,                              "var",          "Variable name." },
    {  1,           1,          DBGCVAR_CAT_ANY,        0,                              "value",        "Value to assign to the variable." },
};

/** 'sleep' arguments */
static const DBGCVARDESC    g_aArgSleep[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_NUMBER,     0,                              "milliseconds", "The sleep interval in milliseconds (max 30000ms)." },
};

/** 'stop' arguments */
static const DBGCVARDESC    g_aArgStop[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_NUMBER,     0,                              "idCpu",        "CPU ID." },
};

/** loadplugin, unloadplugin. */
static const DBGCVARDESC    g_aArgUnload[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           ~0U,        DBGCVAR_CAT_STRING,     0,                              "modname",      "Unloads all mappings of the given modules in the active address space." },
};

/** 'unset' arguments */
static const DBGCVARDESC    g_aArgUnset[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           ~0U,        DBGCVAR_CAT_SYMBOL,     0,                              "vars",         "One or more variable names." },
};

/** writecore arguments. */
static const DBGCVARDESC    g_aArgWriteCore[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "path",         "Filename string." },
};

/** writegstmem arguments. */
static const DBGCVARDESC    g_aArgWriteGstMem[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "filename",     "Filename string." },
    {  1,           1,          DBGCVAR_CAT_POINTER,    0,                              "address",      "The guest address." }
};



/** Command descriptors for the basic commands. */
const DBGCCMD    g_aDbgcCmds[] =
{
    /* pszCmd,      cArgsMin, cArgsMax, paArgDescs,          cArgDescs,               fFlags, pfnHandler        pszSyntax,          ....pszDescription */
    { "bye",        0,        0,        NULL,                0,                            0, dbgcCmdQuit,      "",                     "Exits the debugger." },
    { "cpu",        0,        1,        &g_aArgCpu[0],       RT_ELEMENTS(g_aArgCpu),       0, dbgcCmdCpu,       "[idCpu]",              "If no argument, display the current CPU, else change to the specified CPU." },
    { "echo",       1,        ~0U,      &g_aArgMultiStr[0],  RT_ELEMENTS(g_aArgMultiStr),  0, dbgcCmdEcho,      "<str1> [str2..[strN]]", "Displays the strings separated by one blank space and the last one followed by a newline." },
    { "exit",       0,        0,        NULL,                0,                            0, dbgcCmdQuit,      "",                     "Exits the debugger." },
    { "format",     1,        1,        &g_aArgAny[0],       RT_ELEMENTS(g_aArgAny),       0, dbgcCmdFormat,    "",                     "Evaluates an expression and formats it." },
    { "detect",     0,        0,        NULL,                0,                            0, dbgcCmdDetect,    "",                     "Detects or re-detects the guest os and starts the OS specific digger." },
    { "dmesg",      0,        1,        &g_aArgDmesg[0],     RT_ELEMENTS(g_aArgDmesg),     0, dbgcCmdDmesg,     "[N last messages]",    "Displays the guest os kernel messages, if available." },
    { "dumpimage",  1,        ~0U,      &g_aArgDumpImage[0], RT_ELEMENTS(g_aArgDumpImage), 0, dbgcCmdDumpImage, "<addr1> [addr2..[addrN]]", "Dumps executable images." },
    { "harakiri",   0,        0,        NULL,                0,                            0, dbgcCmdHarakiri,  "",                     "Kills debugger process." },
    { "help",       0,        ~0U,      &g_aArgHelp[0],      RT_ELEMENTS(g_aArgHelp),      0, dbgcCmdHelp,      "[cmd/op [..]]",        "Display help. For help about info items try 'info help'." },
    { "info",       1,        2,        &g_aArgInfo[0],      RT_ELEMENTS(g_aArgInfo),      0, dbgcCmdInfo,      "<info> [args]",        "Display info register in the DBGF. For a list of info items try 'info help'." },
    { "loadimage",  2,        3,        &g_aArgLoadImage[0], RT_ELEMENTS(g_aArgLoadImage), 0, dbgcCmdLoadImage, "<filename> <address> [name]",
                                                                                                                                                 "Loads the symbols of an executable image at the specified address. "
                                                                                                                                                 /*"Optionally giving the module a name other than the file name stem."*/ }, /** @todo implement line breaks */
    { "loadimage32",2,        3,        &g_aArgLoadImage[0], RT_ELEMENTS(g_aArgLoadImage), 0, dbgcCmdLoadImage, "<filename> <address> [name]", "loadimage variant for selecting 32-bit images (mach-o)." },
    { "loadimage64",2,        3,        &g_aArgLoadImage[0], RT_ELEMENTS(g_aArgLoadImage), 0, dbgcCmdLoadImage, "<filename> <address> [name]", "loadimage variant for selecting 64-bit images (mach-o)." },
    { "loadinmem",  1,        2,        &g_aArgLoadInMem[0], RT_ELEMENTS(g_aArgLoadInMem), 0, dbgcCmdLoadInMem, "<address> [name]",    "Tries to load a image mapped at the given address." },
    { "loadmap",    2,        5,        &g_aArgLoadMap[0],   RT_ELEMENTS(g_aArgLoadMap),   0, dbgcCmdLoadMap,   "<filename> <address> [name] [subtrahend] [seg]",
                                                                                                                                       "Loads the symbols from a map file, usually at a specified address. "
                                                                                                                                       /*"Optionally giving the module a name other than the file name stem "
                                                                                                                                       "and a subtrahend to subtract from the addresses."*/ },
    { "loadplugin", 1,        1,        &g_aArgPlugIn[0],    RT_ELEMENTS(g_aArgPlugIn),    0, dbgcCmdLoadPlugIn,"<plugin1> [plugin2..N]", "Loads one or more plugins" },
    { "loadseg",    3,        4,        &g_aArgLoadSeg[0],   RT_ELEMENTS(g_aArgLoadSeg),   0, dbgcCmdLoadSeg,   "<filename> <address> <seg> [name]",
                                                                                                                                       "Loads the symbols of a segment in the executable image at the specified address. "
                                                                                                                                       /*"Optionally giving the module a name other than the file name stem."*/ },
    { "loadvars",   1,        1,        &g_aArgFilename[0],  RT_ELEMENTS(g_aArgFilename),  0, dbgcCmdLoadVars,  "<filename>",           "Load variables from file. One per line, same as the args to the set command." },
    { "log",        0,        1,        &g_aArgLog[0],       RT_ELEMENTS(g_aArgLog),       0, dbgcCmdLog,       "[group string]",       "Displays or modifies the logging group settings (VBOX_LOG)" },
    { "logdest",    0,        1,        &g_aArgLogDest[0],   RT_ELEMENTS(g_aArgLogDest),   0, dbgcCmdLogDest,   "[dest string]",        "Displays or modifies the logging destination (VBOX_LOG_DEST)." },
    { "logflags",   0,        1,        &g_aArgLogFlags[0],  RT_ELEMENTS(g_aArgLogFlags),  0, dbgcCmdLogFlags,  "[flags string]",       "Displays or modifies the logging flags (VBOX_LOG_FLAGS)." },
    { "logflush",   0,        0,        NULL,                0,                            0, dbgcCmdLogFlush,  "",                     "Flushes the log buffers." },
    { "multistep",  0,        2,        &g_aArgMultiStep[0], RT_ELEMENTS(g_aArgMultiStep), 0, dbgcCmdMultiStep, "[count [stride]",      "Performs the specified number of step-into operations. Stops early if non-step event occurs." },
    { "ntrbtree",   1,        1,        &g_aArgNtRbTree[0],  RT_ELEMENTS(g_aArgNtRbTree),  0, dbgcCmdNtRbTree,  "<rb-tree-address>",    "Windows: Dumps a red-black tree." },
    { "quit",       0,        0,        NULL,                0,                            0, dbgcCmdQuit,      "",                     "Exits the debugger." },
    { "runscript",  1,        1,        &g_aArgFilename[0],  RT_ELEMENTS(g_aArgFilename),  0, dbgcCmdRunScript, "<filename>",           "Runs the command listed in the script. Lines starting with '#' "
                                                                                                                                        "(after removing blanks) are comment. blank lines are ignored. Stops on failure." },
    { "set",        2,        2,        &g_aArgSet[0],       RT_ELEMENTS(g_aArgSet),       0, dbgcCmdSet,       "<var> <value>",        "Sets a global variable." },
    { "showvars",   0,        0,        NULL,                0,                            0, dbgcCmdShowVars,  "",                     "List all the defined variables." },
    { "sleep",      1,        1,        &g_aArgSleep[0],     RT_ELEMENTS(g_aArgSleep),     0, dbgcCmdSleep,     "<milliseconds>",       "Sleeps for the given number of milliseconds (max 30000)." },
    { "stop",       0,        1,        &g_aArgStop[0],      RT_ELEMENTS(g_aArgStop),      0, dbgcCmdStop,      "[idCpu]",              "Stop execution either of all or the specified CPU. (The latter is not recommended unless you know exactly what you're doing.)" },
    { "unload",     1,       ~0U,       &g_aArgUnload[0],    RT_ELEMENTS(g_aArgUnload),    0, dbgcCmdUnload,    "<modname1> [modname2..N]", "Unloads one or more modules in the current address space." },
    { "unloadplugin", 1,     ~0U,       &g_aArgPlugIn[0],    RT_ELEMENTS(g_aArgPlugIn),    0, dbgcCmdUnloadPlugIn, "<plugin1> [plugin2..N]", "Unloads one or more plugins." },
    { "unset",      1,       ~0U,       &g_aArgUnset[0],     RT_ELEMENTS(g_aArgUnset),     0, dbgcCmdUnset,     "<var1> [var1..[varN]]",  "Unsets (delete) one or more global variables." },
    { "writecore",  1,        1,        &g_aArgWriteCore[0], RT_ELEMENTS(g_aArgWriteCore), 0, dbgcCmdWriteCore,   "<filename>",           "Write core to file." },
    { "writegstmem",  2,      2,        &g_aArgWriteGstMem[0], RT_ELEMENTS(g_aArgWriteGstMem), 0, dbgcCmdWriteGstMem, "<filename> <address>",           "Load data from the given file and write it to guest memory at the given start address." },
};
/** The number of native commands. */
const uint32_t      g_cDbgcCmds = RT_ELEMENTS(g_aDbgcCmds);
/** Pointer to head of the list of external commands. */
static PDBGCEXTCMDS g_pExtCmdsHead;




/**
 * Finds a routine.
 *
 * @returns Pointer to the command descriptor.
 *          If the request was for an external command, the caller is responsible for
 *          unlocking the external command list.
 * @returns NULL if not found.
 * @param   pDbgc       The debug console instance.
 * @param   pachName    Pointer to the routine string (not terminated).
 * @param   cchName     Length of the routine name.
 * @param   fExternal   Whether or not the routine is external.
 */
PCDBGCCMD dbgcCommandLookup(PDBGC pDbgc, const char *pachName, size_t cchName, bool fExternal)
{
    if (!fExternal)
    {
        /* emulation first, so commands can be overloaded (info ++). */
        PCDBGCCMD pCmd = pDbgc->paEmulationCmds;
        unsigned cLeft = pDbgc->cEmulationCmds;
        while (cLeft-- > 0)
        {
            if (    !strncmp(pachName, pCmd->pszCmd, cchName)
                &&  !pCmd->pszCmd[cchName])
                return pCmd;
            pCmd++;
        }

        for (unsigned iCmd = 0; iCmd < RT_ELEMENTS(g_aDbgcCmds); iCmd++)
        {
            if (    !strncmp(pachName, g_aDbgcCmds[iCmd].pszCmd, cchName)
                &&  !g_aDbgcCmds[iCmd].pszCmd[cchName])
                return &g_aDbgcCmds[iCmd];
        }
    }
    else
    {
        DBGCEXTLISTS_LOCK_RD();
        for (PDBGCEXTCMDS pExtCmds = g_pExtCmdsHead; pExtCmds; pExtCmds = pExtCmds->pNext)
        {
            for (unsigned iCmd = 0; iCmd < pExtCmds->cCmds; iCmd++)
            {
                if (    !strncmp(pachName, pExtCmds->paCmds[iCmd].pszCmd, cchName)
                    &&  !pExtCmds->paCmds[iCmd].pszCmd[cchName])
                    return &pExtCmds->paCmds[iCmd];
            }
        }
        DBGCEXTLISTS_UNLOCK_RD();
    }

    return NULL;
}


/**
 * Register one or more external commands.
 *
 * @returns VBox status code.
 * @param   paCommands      Pointer to an array of command descriptors.
 *                          The commands must be unique. It's not possible
 *                          to register the same commands more than once.
 * @param   cCommands       Number of commands.
 */
DBGDECL(int)    DBGCRegisterCommands(PCDBGCCMD paCommands, unsigned cCommands)
{
    /*
     * Lock the list.
     */
    DBGCEXTLISTS_LOCK_WR();
    PDBGCEXTCMDS pCur = g_pExtCmdsHead;
    while (pCur)
    {
        if (paCommands == pCur->paCmds)
        {
            DBGCEXTLISTS_UNLOCK_WR();
            AssertMsgFailed(("Attempt at re-registering %d command(s)!\n", cCommands));
            return VWRN_DBGC_ALREADY_REGISTERED;
        }
        pCur = pCur->pNext;
    }

    /*
     * Allocate new chunk.
     */
    int rc = 0;
    pCur = (PDBGCEXTCMDS)RTMemAlloc(sizeof(*pCur));
    RTMEM_MAY_LEAK(pCur);
    if (pCur)
    {
        pCur->cCmds  = cCommands;
        pCur->paCmds = paCommands;
        pCur->pNext = g_pExtCmdsHead;
        g_pExtCmdsHead = pCur;
    }
    else
        rc = VERR_NO_MEMORY;
    DBGCEXTLISTS_UNLOCK_WR();

    return rc;
}


/**
 * Deregister one or more external commands previously registered by
 * DBGCRegisterCommands().
 *
 * @returns VBox status code.
 * @param   paCommands      Pointer to an array of command descriptors
 *                          as given to DBGCRegisterCommands().
 * @param   cCommands       Number of commands.
 */
DBGDECL(int)    DBGCDeregisterCommands(PCDBGCCMD paCommands, unsigned cCommands)
{
    /*
     * Lock the list.
     */
    DBGCEXTLISTS_LOCK_WR();
    PDBGCEXTCMDS pPrev = NULL;
    PDBGCEXTCMDS pCur = g_pExtCmdsHead;
    while (pCur)
    {
        if (paCommands == pCur->paCmds)
        {
            if (pPrev)
                pPrev->pNext = pCur->pNext;
            else
                g_pExtCmdsHead = pCur->pNext;
            DBGCEXTLISTS_UNLOCK_WR();

            RTMemFree(pCur);
            return VINF_SUCCESS;
        }
        pPrev = pCur;
        pCur = pCur->pNext;
    }
    DBGCEXTLISTS_UNLOCK_WR();

    NOREF(cCommands);
    return VERR_DBGC_COMMANDS_NOT_REGISTERED;
}


/**
 * Outputs a command or function summary line.
 *
 * @returns Output status code
 * @param   pCmdHlp         The command helpers.
 * @param   pszName         The name of the function or command.
 * @param   fExternal       Whether it's external.
 * @param   pszSyntax       The syntax.
 * @param   pszDescription  The description.
 */
static int dbgcCmdHelpCmdOrFunc(PDBGCCMDHLP pCmdHlp, const char *pszName, bool fExternal,
                                const char *pszSyntax, const char *pszDescription)
{
    /*
     * Aiming for "%-11s %-30s %s".  Need to adjust when any of the two
     * columns are two wide as well as break the last column up if its
     * too wide.
     */
    size_t const cchMaxWidth = 100;
    size_t const cchCol1     = 11;
    size_t const cchCol2     = 30;
    size_t const cchCol3     = cchMaxWidth - cchCol1 - cchCol2 - 2;

    size_t const cchName     = strlen(pszName) + fExternal;
    size_t const cchSyntax   = strlen(pszSyntax);
    size_t       cchDesc     = strlen(pszDescription);

    /* Can we do it the simple + fast way? */
    if (   cchName   <= cchCol1
        && cchSyntax <= cchCol2
        && cchDesc   <= cchCol3)
        return DBGCCmdHlpPrintf(pCmdHlp,
                                !fExternal ? "%-*s %-*s %s\n" :  ".%-*s %-*s %s\n",
                                cchCol1, pszName,
                                cchCol2, pszSyntax,
                                pszDescription);

    /* Column 1. */
    size_t off = 0;
    DBGCCmdHlpPrintf(pCmdHlp, !fExternal ? "%s" :  ".%s", pszName);
    off += cchName;
    ssize_t cchPadding = cchCol1 - off;
    if (cchPadding <= 0)
        cchPadding = 0;

    /* Column 2. */
    DBGCCmdHlpPrintf(pCmdHlp, "%*s %s", cchPadding, "", pszSyntax);
    off += cchPadding + 1 + cchSyntax;
    cchPadding = cchCol1 + 1 + cchCol2 - off;
    if (cchPadding <= 0)
        cchPadding = 0;
    off += cchPadding;

    /* Column 3. */
    for (;;)
    {
        ssize_t cchCurWidth = cchMaxWidth - off - 1;
        if (cchCurWidth != (ssize_t)cchCol3)
            DBGCCmdHlpPrintf(pCmdHlp, "\n");
        else if ((ssize_t)cchDesc <= cchCurWidth)
            return DBGCCmdHlpPrintf(pCmdHlp, "%*s %s\n", cchPadding, "", pszDescription);
        else
        {
            /* Split on preceding blank. */
            const char *pszEnd  = &pszDescription[cchCurWidth];
            if (!RT_C_IS_BLANK(*pszEnd))
                while (pszEnd != pszDescription && !RT_C_IS_BLANK(pszEnd[-1]))
                    pszEnd--;
            const char *pszNext = pszEnd;

            while (pszEnd != pszDescription && RT_C_IS_BLANK(pszEnd[-1]))
                pszEnd--;
            if (pszEnd == pszDescription)
            {
                while (*pszEnd && !RT_C_IS_BLANK(*pszEnd))
                    pszEnd++;
                pszNext = pszEnd;
            }

            while (RT_C_IS_BLANK(*pszNext))
                pszNext++;

            /* Output it and advance to the next line. */
            if (!*pszNext)
                return DBGCCmdHlpPrintf(pCmdHlp, "%*s %.*s\n", cchPadding, "", pszEnd - pszDescription, pszDescription);
            DBGCCmdHlpPrintf(pCmdHlp, "%*s %.*s\n", cchPadding, "", pszEnd - pszDescription, pszDescription);

            /* next */
            cchDesc -= pszNext - pszDescription;
            pszDescription = pszNext;
        }
        off = cchCol1 + 1 + cchCol2;
        cchPadding = off;
    }
}


/**
 * Prints full command help.
 */
static void dbgcCmdHelpCmdOrFuncFull(PDBGCCMDHLP pCmdHlp, const char *pszName, bool fExternal,
                                     const char *pszSyntax, const char *pszDescription,
                                     uint32_t cArgsMin, uint32_t cArgsMax,
                                     PCDBGCVARDESC paArgDescs, uint32_t cArgDescs, uint32_t *pcHits)
{
    if (*pcHits)
        DBGCCmdHlpPrintf(pCmdHlp, "\n");
    *pcHits += 1;

    /* the command */
     dbgcCmdHelpCmdOrFunc(pCmdHlp, pszName, fExternal, pszSyntax, pszDescription);
#if 1
    char szTmp[80];
    if (!cArgsMin && cArgsMin == cArgsMax)
        RTStrPrintf(szTmp, sizeof(szTmp), "<no args>");
    else if (cArgsMin == cArgsMax)
        RTStrPrintf(szTmp, sizeof(szTmp), " <%u args>", cArgsMin);
    else if (cArgsMax == ~0U)
        RTStrPrintf(szTmp, sizeof(szTmp), " <%u+ args>", cArgsMin);
    else
        RTStrPrintf(szTmp, sizeof(szTmp), " <%u to %u args>", cArgsMin, cArgsMax);
    dbgcCmdHelpCmdOrFunc(pCmdHlp, "", false, szTmp, "");
#endif

    /* argument descriptions. */
    for (uint32_t i = 0; i < cArgDescs; i++)
    {
        DBGCCmdHlpPrintf(pCmdHlp, "    %-12s %s", paArgDescs[i].pszName, paArgDescs[i].pszDescription);
        if (!paArgDescs[i].cTimesMin)
        {
            if (paArgDescs[i].cTimesMax == ~0U)
                DBGCCmdHlpPrintf(pCmdHlp, " <optional+>\n");
            else
                DBGCCmdHlpPrintf(pCmdHlp, " <optional-%u>\n", paArgDescs[i].cTimesMax);
        }
        else
        {
            if (paArgDescs[i].cTimesMax == ~0U)
                DBGCCmdHlpPrintf(pCmdHlp, " <%u+>\n", paArgDescs[i].cTimesMin);
            else
                DBGCCmdHlpPrintf(pCmdHlp, " <%u-%u>\n", paArgDescs[i].cTimesMin, paArgDescs[i].cTimesMax);
        }
    }
}



/**
 * Prints full command help.
 */
static void dbgcPrintHelpCmd(PDBGCCMDHLP pCmdHlp, PCDBGCCMD pCmd, bool fExternal, uint32_t *pcHits)
{
    dbgcCmdHelpCmdOrFuncFull(pCmdHlp, pCmd->pszCmd, fExternal, pCmd->pszSyntax, pCmd->pszDescription,
                             pCmd->cArgsMin, pCmd->cArgsMax, pCmd->paArgDescs, pCmd->cArgDescs, pcHits);
}


/**
 * Prints full function help.
 */
static void dbgcPrintHelpFunction(PDBGCCMDHLP pCmdHlp, PCDBGCFUNC pFunc, bool fExternal, uint32_t *pcHits)
{
    dbgcCmdHelpCmdOrFuncFull(pCmdHlp, pFunc->pszFuncNm, fExternal, pFunc->pszSyntax, pFunc->pszDescription,
                             pFunc->cArgsMin, pFunc->cArgsMax, pFunc->paArgDescs, pFunc->cArgDescs, pcHits);
}


static void dbgcCmdHelpCommandsWorker(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, PCDBGCCMD paCmds, uint32_t cCmds, bool fExternal,
                                      const char *pszDescFmt, ...)
{
    RT_NOREF1(pDbgc);
    if (pszDescFmt)
    {
        va_list va;
        va_start(va, pszDescFmt);
        pCmdHlp->pfnPrintfV(pCmdHlp, NULL, pszDescFmt, va);
        va_end(va);
    }

    for (uint32_t i = 0; i < cCmds; i++)
        dbgcCmdHelpCmdOrFunc(pCmdHlp, paCmds[i].pszCmd, fExternal, paCmds[i].pszSyntax, paCmds[i].pszDescription);
}


static void dbgcCmdHelpCommands(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, uint32_t *pcHits)
{
    if (*pcHits)
        DBGCCmdHlpPrintf(pCmdHlp, "\n");
    *pcHits += 1;

    dbgcCmdHelpCommandsWorker(pDbgc, pCmdHlp, pDbgc->paEmulationCmds, pDbgc->cEmulationCmds, false,
                              "Commands for %s emulation:\n", pDbgc->pszEmulation);
    dbgcCmdHelpCommandsWorker(pDbgc, pCmdHlp, g_aDbgcCmds, RT_ELEMENTS(g_aDbgcCmds), false,
                              "\nCommon Commands:\n");

    DBGCEXTLISTS_LOCK_RD();
    const char *pszDesc = "\nExternal Commands:\n";
    for (PDBGCEXTCMDS pExtCmd = g_pExtCmdsHead; pExtCmd; pExtCmd = pExtCmd->pNext)
    {
        dbgcCmdHelpCommandsWorker(pDbgc, pCmdHlp, pExtCmd->paCmds, pExtCmd->cCmds, true, pszDesc);
        pszDesc = NULL;
    }
    DBGCEXTLISTS_UNLOCK_RD();
}


static void dbgcCmdHelpFunctionsWorker(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, PCDBGCFUNC paFuncs, size_t cFuncs, bool fExternal,
                                       const char *pszDescFmt, ...)
{
    RT_NOREF1(pDbgc);
    if (pszDescFmt)
    {
        va_list va;
        va_start(va, pszDescFmt);
        DBGCCmdHlpPrintf(pCmdHlp, pszDescFmt, va);
        va_end(va);
    }

    for (uint32_t i = 0; i < cFuncs; i++)
        dbgcCmdHelpCmdOrFunc(pCmdHlp, paFuncs[i].pszFuncNm, fExternal, paFuncs[i].pszSyntax, paFuncs[i].pszDescription);
}


static void dbgcCmdHelpFunctions(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, uint32_t *pcHits)
{
    if (*pcHits)
        DBGCCmdHlpPrintf(pCmdHlp, "\n");
    *pcHits += 1;

    dbgcCmdHelpFunctionsWorker(pDbgc, pCmdHlp, pDbgc->paEmulationFuncs, pDbgc->cEmulationFuncs, false,
                               "Functions for %s emulation:\n", pDbgc->pszEmulation);
    dbgcCmdHelpFunctionsWorker(pDbgc, pCmdHlp, g_aDbgcFuncs, g_cDbgcFuncs, false,
                               "\nCommon Functions:\n");
#if 0
    DBGCEXTLISTS_LOCK_RD();
    const char *pszDesc = "\nExternal Functions:\n";
    for (PDBGCEXTFUNCS pExtFunc = g_pExtFuncsHead; pExtFunc; pExtFunc = pExtFunc->pNext)
    {
        dbgcCmdHelpFunctionsWorker(pDbgc, pCmdHlp, pExtFunc->paFuncs, pExtFunc->cFuncs, false,
                                        pszDesc);
        pszDesc = NULL;
    }
    DBGCEXTLISTS_UNLOCK_RD();
#endif
}


static void dbgcCmdHelpOperators(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, uint32_t *pcHits)
{
    RT_NOREF1(pDbgc);
    DBGCCmdHlpPrintf(pCmdHlp, !*pcHits ? "Operators:\n" : "\nOperators:\n");
    *pcHits += 1;

    unsigned iPrecedence = 0;
    unsigned cLeft       = g_cDbgcOps;
    while (cLeft > 0)
    {
        for (unsigned i = 0; i < g_cDbgcOps; i++)
            if (g_aDbgcOps[i].iPrecedence == iPrecedence)
            {
                dbgcCmdHelpCmdOrFunc(pCmdHlp, g_aDbgcOps[i].szName, false,
                                     g_aDbgcOps[i].fBinary ? "Binary" : "Unary ",
                                     g_aDbgcOps[i].pszDescription);
                cLeft--;
            }
        iPrecedence++;
    }
}


static void dbgcCmdHelpAll(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, uint32_t *pcHits)
{
    *pcHits += 1;
    DBGCCmdHlpPrintf(pCmdHlp,
                     "\n"
                     "VirtualBox Debugger Help\n"
                     "------------------------\n"
                     "\n");
    dbgcCmdHelpCommands(pDbgc, pCmdHlp, pcHits);
    DBGCCmdHlpPrintf(pCmdHlp, "\n");
    dbgcCmdHelpFunctions(pDbgc, pCmdHlp, pcHits);
    DBGCCmdHlpPrintf(pCmdHlp, "\n");
    dbgcCmdHelpOperators(pDbgc, pCmdHlp, pcHits);
}


static void dbgcCmdHelpSummary(PDBGC pDbgc, PDBGCCMDHLP pCmdHlp, uint32_t *pcHits)
{
    RT_NOREF1(pDbgc);
    *pcHits += 1;
    DBGCCmdHlpPrintf(pCmdHlp,
                     "\n"
                     "VirtualBox Debugger Help Summary\n"
                     "--------------------------------\n"
                     "\n"
                     "help commands      Show help on all commands.\n"
                     "help functions     Show help on all functions.\n"
                     "help operators     Show help on all operators.\n"
                     "help all           All the above.\n"
                     "help <cmd-pattern> [...]\n"
                     "                   Show details help on individual commands, simple\n"
                     "                   patterns can be used to match several commands.\n"
                     "help [summary]     Displays this message.\n"
                     );
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'help' command.}
 */
static DECLCALLBACK(int) dbgcCmdHelp(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC       pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    int         rc    = VINF_SUCCESS;
    uint32_t    cHits = 0;

    if (!cArgs)
        /*
         * No arguments, show summary.
         */
        dbgcCmdHelpSummary(pDbgc, pCmdHlp, &cHits);
    else
    {
        /*
         * Search for the arguments (strings).
         */
        DBGCEXTCMDS  aFixedCmds[] =
        {
            { pDbgc->cEmulationCmds,    pDbgc->paEmulationCmds,     NULL },
            { g_cDbgcCmds,              g_aDbgcCmds,                NULL },
        };
        DBGCEXTFUNCS aFixedFuncs[] =
        {
            { pDbgc->cEmulationFuncs,   pDbgc->paEmulationFuncs,    NULL },
            { g_cDbgcFuncs,             g_aDbgcFuncs,               NULL },
        };

        for (unsigned iArg = 0; iArg < cArgs; iArg++)
        {
            AssertReturn(paArgs[iArg].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_BUG);
            const char *pszPattern = paArgs[iArg].u.pszString;

            /* aliases */
            if (   !strcmp(pszPattern, "commands")
                || !strcmp(pszPattern, "cmds") )
                dbgcCmdHelpCommands(pDbgc, pCmdHlp, &cHits);
            else if (   !strcmp(pszPattern, "functions")
                     || !strcmp(pszPattern, "funcs") )
                dbgcCmdHelpFunctions(pDbgc, pCmdHlp, &cHits);
            else if (   !strcmp(pszPattern, "operators")
                     || !strcmp(pszPattern, "ops") )
                dbgcCmdHelpOperators(pDbgc, pCmdHlp, &cHits);
            else if (!strcmp(pszPattern, "all"))
                dbgcCmdHelpAll(pDbgc, pCmdHlp, &cHits);
            else if (!strcmp(pszPattern, "summary"))
                dbgcCmdHelpSummary(pDbgc, pCmdHlp, &cHits);
            /* Individual commands. */
            else
            {
                uint32_t const cPrevHits = cHits;

                /* lookup in the emulation command list first */
                for (unsigned j = 0; j < RT_ELEMENTS(aFixedCmds); j++)
                    for (unsigned i = 0; i < aFixedCmds[j].cCmds; i++)
                        if (RTStrSimplePatternMatch(pszPattern, aFixedCmds[j].paCmds[i].pszCmd))
                            dbgcPrintHelpCmd(pCmdHlp, &aFixedCmds[j].paCmds[i], false, &cHits);
                for (unsigned j = 0; j < RT_ELEMENTS(aFixedFuncs); j++)
                    for (unsigned i = 0; i < aFixedFuncs[j].cFuncs; i++)
                        if (RTStrSimplePatternMatch(pszPattern, aFixedFuncs[j].paFuncs[i].pszFuncNm))
                            dbgcPrintHelpFunction(pCmdHlp, &aFixedFuncs[j].paFuncs[i], false, &cHits);

               /* external commands */
               if (     g_pExtCmdsHead
                   &&   (   *pszPattern == '.'
                         || *pszPattern == '?'
                         || *pszPattern == '*'))
               {
                   DBGCEXTLISTS_LOCK_RD();
                   const char *pszPattern2 = pszPattern + (*pszPattern == '.' || *pszPattern == '?');
                   for (PDBGCEXTCMDS pExtCmd = g_pExtCmdsHead; pExtCmd; pExtCmd = pExtCmd->pNext)
                       for (unsigned i = 0; i < pExtCmd->cCmds; i++)
                           if (RTStrSimplePatternMatch(pszPattern2, pExtCmd->paCmds[i].pszCmd))
                               dbgcPrintHelpCmd(pCmdHlp, &pExtCmd->paCmds[i], true, &cHits);
#if 0
                   for (PDBGCEXTFUNCS pExtFunc = g_pExtFuncsHead; pExtFunc; pExtFunc = pExtFunc->pNext)
                       for (unsigned i = 0; i < pExtFunc->cFuncs; i++)
                           if (RTStrSimplePatternMatch(pszPattern2, pExtFunc->paFuncs[i].pszFuncNm))
                               dbgcPrintHelpFunction(pCmdHlp, &pExtFunc->paFuncs[i], true, &cHits);
#endif
                   DBGCEXTLISTS_UNLOCK_RD();
               }

               /* operators */
               if (cHits == cPrevHits && strlen(paArgs[iArg].u.pszString) < sizeof(g_aDbgcOps[0].szName))
                   for (unsigned i = 0; i < g_cDbgcOps && RT_SUCCESS(rc); i++)
                       if (RTStrSimplePatternMatch(pszPattern, g_aDbgcOps[i].szName))
                       {
                           if (cHits++)
                               DBGCCmdHlpPrintf(pCmdHlp, "\n");
                           dbgcCmdHelpCmdOrFunc(pCmdHlp, g_aDbgcOps[i].szName, false,
                                                g_aDbgcOps[i].fBinary ? "Binary" : "Unary ",
                                                g_aDbgcOps[i].pszDescription);
                       }

               /* found? */
               if (cHits == cPrevHits)
               {
                   DBGCCmdHlpPrintf(pCmdHlp, "error: '%s' was not found!\n",
                                    paArgs[iArg].u.pszString);
                   rc = VERR_DBGC_COMMAND_FAILED;
               }
            }
        } /* foreach argument */
    }

    NOREF(pCmd);
    NOREF(pUVM);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'multistep' command.}
 */
static DECLCALLBACK(int) dbgcCmdMultiStep(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Parse arguments.
     */
    uint32_t cSteps = 64;
    if (cArgs > 0)
    {
        if (paArgs[0].u.u64Number == 0 || paArgs[0].u.u64Number > _2G)
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_OUT_OF_RANGE,
                                    "The 'count' argument is out of range: %#llx - 1..2GiB\n", paArgs[0].u.u64Number);
        cSteps = (uint32_t)paArgs[0].u.u64Number;
    }
    uint32_t uStrideLength = 1;
    if (cArgs > 1)
    {
        if (paArgs[1].u.u64Number == 0 || paArgs[1].u.u64Number > _2G)
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, VERR_OUT_OF_RANGE,
                                    "The 'stride' argument is out of range: %#llx - 1..2GiB\n", paArgs[0].u.u64Number);
        uStrideLength = (uint32_t)paArgs[0].u.u64Number;
    }

    /*
     * Take the first step.
     */
    int rc = DBGFR3StepEx(pUVM, pDbgc->idCpu, DBGF_STEP_F_INTO, NULL, NULL, 0, uStrideLength);
    if (RT_SUCCESS(rc))
    {
        pDbgc->cMultiStepsLeft          = cSteps;
        pDbgc->uMultiStepStrideLength   = uStrideLength;
        pDbgc->pMultiStepCmd            = pCmd;
        pDbgc->fReady                   = false;
    }
    else
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3StepEx(,,DBGF_STEP_F_INTO,) failed");

    NOREF(pCmd);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'quit'\, 'exit' and 'bye' commands. }
 */
static DECLCALLBACK(int) dbgcCmdQuit(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    DBGCCmdHlpPrintf(pCmdHlp, "Quitting console...\n");
    NOREF(pCmd);
    NOREF(pUVM);
    NOREF(paArgs);
    NOREF(cArgs);
    return VERR_DBGC_QUIT;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'stop' command.}
 */
static DECLCALLBACK(int) dbgcCmdStop(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
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
     * Try halt the VM or VCpu.
     */
    int rc = DBGFR3Halt(pUVM, idCpu);
    if (RT_SUCCESS(rc))
    {
        Assert(rc == VINF_SUCCESS || rc == VWRN_DBGF_ALREADY_HALTED);
        if (rc != VWRN_DBGF_ALREADY_HALTED)
            rc = VWRN_DBGC_CMD_PENDING;
        else if (idCpu == VMCPUID_ALL)
            rc = DBGCCmdHlpPrintf(pCmdHlp, "warning: The VM is already halted...\n");
        else
            rc = DBGCCmdHlpPrintf(pCmdHlp, "warning: CPU %u is already halted...\n", idCpu);
    }
    else
        rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "Executing DBGFR3Halt().");

    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'echo' command.}
 */
static DECLCALLBACK(int) dbgcCmdEcho(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Loop thru the arguments and print them with one space between.
     */
    int rc = 0;
    for (unsigned i = 0; i < cArgs; i++)
    {
        AssertReturn(paArgs[i].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_BUG);
        rc = DBGCCmdHlpPrintf(pCmdHlp, i ? " %s" : "%s", paArgs[i].u.pszString);
        if (RT_FAILURE(rc))
            return rc;
    }
    NOREF(pCmd); NOREF(pUVM);
    return DBGCCmdHlpPrintf(pCmdHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'runscript' command.}
 */
static DECLCALLBACK(int) dbgcCmdRunScript(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF2(pUVM, pCmd);

    /* check that the parser did what it's supposed to do. */
    if (    cArgs != 1
        ||  paArgs[0].enmType != DBGCVAR_TYPE_STRING)
        return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");

    /* Pass it on to a common function. */
    const char *pszFilename = paArgs[0].u.pszString;
    return dbgcEvalScript(DBGC_CMDHLP2DBGC(pCmdHlp), pszFilename, false /*fAnnounce*/);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'detect' command.}
 */
static DECLCALLBACK(int) dbgcCmdDetect(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /* check that the parser did what it's supposed to do. */
    if (cArgs != 0)
        return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");

    /*
     * Perform the detection.
     */
    char szName[64];
    int rc = DBGFR3OSDetect(pUVM, szName, sizeof(szName));
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "Executing DBGFR3OSDetect().\n");
    if (rc == VINF_SUCCESS)
    {
        rc = DBGCCmdHlpPrintf(pCmdHlp, "Guest OS: %s\n", szName);
        char szVersion[512];
        int rc2 = DBGFR3OSQueryNameAndVersion(pUVM, NULL, 0, szVersion, sizeof(szVersion));
        if (RT_SUCCESS(rc2))
            rc = DBGCCmdHlpPrintf(pCmdHlp, "Version : %s\n", szVersion);
    }
    else
        rc = DBGCCmdHlpPrintf(pCmdHlp, "Unable to figure out which guest OS it is, sorry.\n");
    NOREF(pCmd); NOREF(paArgs);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'dmesg' command.}
 */
static DECLCALLBACK(int) dbgcCmdDmesg(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /* check that the parser did what it's supposed to do. */
    if (cArgs > 1)
        return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");
    uint32_t cMessages = UINT32_MAX;
    if (cArgs == 1)
    {
        if (paArgs[0].enmType != DBGCVAR_TYPE_NUMBER)
            return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");
        cMessages = paArgs[0].u.u64Number <= UINT32_MAX ? (uint32_t)paArgs[0].u.u64Number : UINT32_MAX;
    }

    /*
     * Query the interface.
     */
    int rc;
    PDBGFOSIDMESG pDmesg = (PDBGFOSIDMESG)DBGFR3OSQueryInterface(pUVM, DBGFOSINTERFACE_DMESG);
    if (pDmesg)
    {
        size_t  cbActual;
        size_t  cbBuf  = _512K;
        char   *pszBuf = (char *)RTMemAlloc(cbBuf);
        if (pszBuf)
        {
            rc = pDmesg->pfnQueryKernelLog(pDmesg, pUVM, VMMR3GetVTable(), 0 /*fFlags*/, cMessages, pszBuf, cbBuf, &cbActual);

            uint32_t cTries = 10;
            while (rc == VERR_BUFFER_OVERFLOW && cbBuf < 16*_1M && cTries-- > 0)
            {
                RTMemFree(pszBuf);
                cbBuf = RT_ALIGN_Z(cbActual + _4K, _4K);
                pszBuf = (char *)RTMemAlloc(cbBuf);
                if (RT_UNLIKELY(!pszBuf))
                {
                    rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Error allocating %#zu bytes.\n", cbBuf);
                    break;
                }
                rc = pDmesg->pfnQueryKernelLog(pDmesg, pUVM, VMMR3GetVTable(), 0 /*fFlags*/, cMessages, pszBuf, cbBuf, &cbActual);
            }
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%s\n", pszBuf);
            else if (rc == VERR_BUFFER_OVERFLOW && pszBuf)
                rc = DBGCCmdHlpPrintf(pCmdHlp, "%s\nWarning: incomplete\n", pszBuf);
            else
                rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "pfnQueryKernelLog failed: %Rrc\n", rc);
            RTMemFree(pszBuf);
        }
        else
            rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "Error allocating %#zu bytes.\n", cbBuf);
    }
    else
        rc = DBGCCmdHlpFail(pCmdHlp, pCmd, "The dmesg interface isn't implemented by guest OS.\n");
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'cpu' command.}
 */
static DECLCALLBACK(int) dbgcCmdCpu(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC       pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /* check that the parser did what it's supposed to do. */
    if (    cArgs != 0
        &&  (   cArgs != 1
             || paArgs[0].enmType != DBGCVAR_TYPE_NUMBER))
        return DBGCCmdHlpPrintf(pCmdHlp, "parser error\n");
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    int rc;
    if (!cArgs)
        rc = DBGCCmdHlpPrintf(pCmdHlp, "Current CPU ID: %u\n", pDbgc->idCpu);
    else
    {
        VMCPUID cCpus = DBGFR3CpuGetCount(pUVM);
        if (paArgs[0].u.u64Number >= cCpus)
            rc = DBGCCmdHlpPrintf(pCmdHlp, "error: idCpu %u is out of range! Highest ID is %u.\n",
                                    paArgs[0].u.u64Number, cCpus-1);
        else
        {
            rc = DBGCCmdHlpPrintf(pCmdHlp, "Changed CPU from %u to %u.\n",
                                    pDbgc->idCpu, (VMCPUID)paArgs[0].u.u64Number);
            pDbgc->idCpu = (VMCPUID)paArgs[0].u.u64Number;
        }
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'info' command.}
 */
static DECLCALLBACK(int) dbgcCmdInfo(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC       pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate input.
     */
    if (    cArgs < 1
        ||  cArgs > 2
        ||  paArgs[0].enmType != DBGCVAR_TYPE_STRING
        ||  paArgs[cArgs - 1].enmType != DBGCVAR_TYPE_STRING)
        return DBGCCmdHlpPrintf(pCmdHlp, "internal error: The parser doesn't do its job properly yet.. quote the string.\n");
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    /*
     * Dump it.
     */
    int rc = DBGFR3InfoEx(pUVM, pDbgc->idCpu,
                          paArgs[0].u.pszString,
                          cArgs == 2 ? paArgs[1].u.pszString : NULL,
                          DBGCCmdHlpGetDbgfOutputHlp(pCmdHlp));
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3InfoEx()\n");

    NOREF(pCmd);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'log' command.}
 */
static DECLCALLBACK(int) dbgcCmdLog(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    int rc;
    if (cArgs == 0)
    {
        char szBuf[_64K];
        rc = RTLogQueryGroupSettings(NULL, szBuf, sizeof(szBuf));
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "RTLogQueryGroupSettings(NULL,,%#zx)\n", sizeof(szBuf));
        DBGCCmdHlpPrintf(pCmdHlp, "VBOX_LOG=%s\n", szBuf);
    }
    else
    {
        rc = DBGFR3LogModifyGroups(pUVM, paArgs[0].u.pszString);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3LogModifyGroups(%p,'%s')\n", pUVM, paArgs[0].u.pszString);
    }
    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'logdest' command.}
 */
static DECLCALLBACK(int) dbgcCmdLogDest(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    int rc;
    if (cArgs == 0)
    {
        char szBuf[_16K];
        rc = RTLogQueryDestinations(NULL, szBuf, sizeof(szBuf));
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "RTLogQueryDestinations(NULL,,%#zx)\n", sizeof(szBuf));
        DBGCCmdHlpPrintf(pCmdHlp, "VBOX_LOG_DEST=%s\n", szBuf);
    }
    else
    {
        rc = DBGFR3LogModifyDestinations(pUVM, paArgs[0].u.pszString);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3LogModifyDestinations(%p,'%s')\n", pUVM, paArgs[0].u.pszString);
    }
    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'logflags' command.}
 */
static DECLCALLBACK(int) dbgcCmdLogFlags(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    int rc;
    if (cArgs == 0)
    {
        char szBuf[_16K];
        rc = RTLogQueryFlags(NULL, szBuf, sizeof(szBuf));
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "RTLogQueryFlags(NULL,,%#zx)\n", sizeof(szBuf));
        DBGCCmdHlpPrintf(pCmdHlp, "VBOX_LOG_FLAGS=%s\n", szBuf);
    }
    else
    {
        rc = DBGFR3LogModifyFlags(pUVM, paArgs[0].u.pszString);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3LogModifyFlags(%p,'%s')\n", pUVM, paArgs[0].u.pszString);
    }

    NOREF(pCmd);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'logflush' command.}
 */
static DECLCALLBACK(int) dbgcCmdLogFlush(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF3(pCmdHlp, pUVM, paArgs);

    RTLogFlush(NULL);
    PRTLOGGER pLogRel = RTLogRelGetDefaultInstance();
    if (pLogRel)
        RTLogFlush(pLogRel);

    NOREF(pCmd); NOREF(cArgs);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'format' command.}
 */
static DECLCALLBACK(int) dbgcCmdFormat(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    LogFlow(("dbgcCmdFormat\n"));
    static const char *apszRangeDesc[] =
    {
        "none", "bytes", "elements"
    };
    int rc = VINF_SUCCESS;

    for (unsigned iArg = 0; iArg < cArgs; iArg++)
    {
        switch (paArgs[iArg].enmType)
        {
            case DBGCVAR_TYPE_UNKNOWN:
                rc = DBGCCmdHlpPrintf(pCmdHlp,
                    "Unknown variable type!\n");
                break;
            case DBGCVAR_TYPE_GC_FLAT:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest flat address: %%%08x range %lld %s\n",
                        paArgs[iArg].u.GCFlat,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest flat address: %%%08x\n",
                        paArgs[iArg].u.GCFlat);
                break;
            case DBGCVAR_TYPE_GC_FAR:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest far address: %04x:%08x range %lld %s\n",
                        paArgs[iArg].u.GCFar.sel,
                        paArgs[iArg].u.GCFar.off,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest far address: %04x:%08x\n",
                        paArgs[iArg].u.GCFar.sel,
                        paArgs[iArg].u.GCFar.off);
                break;
            case DBGCVAR_TYPE_GC_PHYS:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest physical address: %%%%%08x range %lld %s\n",
                        paArgs[iArg].u.GCPhys,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Guest physical address: %%%%%08x\n",
                        paArgs[iArg].u.GCPhys);
                break;
            case DBGCVAR_TYPE_HC_FLAT:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Host flat address: %%%08x range %lld %s\n",
                        paArgs[iArg].u.pvHCFlat,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Host flat address: %%%08x\n",
                        paArgs[iArg].u.pvHCFlat);
                break;
            case DBGCVAR_TYPE_HC_PHYS:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Host physical address: %RHp range %lld %s\n",
                        paArgs[iArg].u.HCPhys,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Host physical address: %RHp\n",
                        paArgs[iArg].u.HCPhys);
                break;

            case DBGCVAR_TYPE_STRING:
                rc = DBGCCmdHlpPrintf(pCmdHlp,
                    "String, %lld bytes long: %s\n",
                    paArgs[iArg].u64Range,
                    paArgs[iArg].u.pszString);
                break;

            case DBGCVAR_TYPE_SYMBOL:
                rc = DBGCCmdHlpPrintf(pCmdHlp,
                    "Symbol, %lld bytes long: %s\n",
                    paArgs[iArg].u64Range,
                    paArgs[iArg].u.pszString);
                break;

            case DBGCVAR_TYPE_NUMBER:
                if (paArgs[iArg].enmRangeType != DBGCVAR_RANGE_NONE)
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Number: hex %llx  dec 0i%lld  oct 0t%llo  range %lld %s\n",
                        paArgs[iArg].u.u64Number,
                        paArgs[iArg].u.u64Number,
                        paArgs[iArg].u.u64Number,
                        paArgs[iArg].u64Range,
                        apszRangeDesc[paArgs[iArg].enmRangeType]);
                else
                    rc = DBGCCmdHlpPrintf(pCmdHlp,
                        "Number: hex %llx  dec 0i%lld  oct 0t%llo\n",
                        paArgs[iArg].u.u64Number,
                        paArgs[iArg].u.u64Number,
                        paArgs[iArg].u.u64Number);
                break;

            default:
                rc = DBGCCmdHlpPrintf(pCmdHlp,
                    "Invalid argument type %d\n",
                    paArgs[iArg].enmType);
                break;
        }
    } /* arg loop */

    NOREF(pCmd); NOREF(pUVM);
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadimage' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadImage(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs >= 2
                 &&  cArgs <= 3
                 &&  paArgs[0].enmType == DBGCVAR_TYPE_STRING
                 &&  DBGCVAR_ISPOINTER(paArgs[1].enmType),
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

    const char     *pszFilename = paArgs[0].u.pszString;

    DBGFADDRESS     ModAddress;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[1], &ModAddress);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "pfnVarToDbgfAddr: %Dv\n", &paArgs[1]);

    const char     *pszModName  = NULL;
    if (cArgs >= 3)
    {
        AssertReturn(paArgs[2].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
        pszModName = paArgs[2].u.pszString;
    }

    /*
     * Determine the desired image arch from the load command used.
     */
    RTLDRARCH enmArch = RTLDRARCH_WHATEVER;
    if (pCmd->pszCmd[sizeof("loadimage") - 1] == '3')
        enmArch = RTLDRARCH_X86_32;
    else if (pCmd->pszCmd[sizeof("loadimage") - 1] == '6')
        enmArch = RTLDRARCH_AMD64;

    /*
     * Try create a module for it.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    rc = DBGFR3AsLoadImage(pUVM, pDbgc->hDbgAs, pszFilename, pszModName, enmArch, &ModAddress, NIL_RTDBGSEGIDX, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3ModuleLoadImage(,,'%s','%s',%Dv,)\n",
                                   pszFilename, pszModName, &paArgs[1]);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadinmem' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadInMem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs >= 1
                 &&  cArgs <= 2
                 &&  DBGCVAR_ISPOINTER(paArgs[0].enmType)
                 &&  (cArgs < 2 || paArgs[1].enmType == DBGCVAR_TYPE_STRING),
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

    RTLDRARCH       enmArch    = RTLDRARCH_WHATEVER;
    const char     *pszModName = cArgs >= 2 ? paArgs[1].u.pszString : NULL;
    DBGFADDRESS     ModAddress;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[0], &ModAddress);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "pfnVarToDbgfAddr: %Dv\n", &paArgs[1]);

    /*
     * Try create a module for it.
     */
    uint32_t fFlags = DBGFMODINMEM_F_NO_CONTAINER_FALLBACK | DBGFMODINMEM_F_NO_READER_FALLBACK;
    RTDBGMOD hDbgMod;
    RTERRINFOSTATIC ErrInfo;
    rc = DBGFR3ModInMem(pUVM, &ModAddress, fFlags, pszModName, pszModName, enmArch, 0 /*cbImage*/,
                        &hDbgMod, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        if (RTErrInfoIsSet(&ErrInfo.Core))
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "DBGFR3ModInMem failed for %Dv: %s",  &ModAddress, ErrInfo.Core.pszMsg);
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3ModInMem failed for %Dv", &ModAddress);
    }

    /*
     * Link the module into the appropriate address space.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    rc = DBGFR3AsLinkModule(pUVM, pDbgc->hDbgAs, hDbgMod, &ModAddress, NIL_RTDBGSEGIDX, RTDBGASLINK_FLAGS_REPLACE);
    RTDbgModRelease(hDbgMod);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3AsLinkModule failed for %Dv", &ModAddress);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadmap' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadMap(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs >= 2
                 &&  cArgs <= 5
                 &&  paArgs[0].enmType == DBGCVAR_TYPE_STRING
                 &&  DBGCVAR_ISPOINTER(paArgs[1].enmType),
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

    const char     *pszFilename = paArgs[0].u.pszString;

    DBGFADDRESS     ModAddress;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[1], &ModAddress);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "pfnVarToDbgfAddr: %Dv\n", &paArgs[1]);

    const char     *pszModName  = NULL;
    if (cArgs >= 3)
    {
        AssertReturn(paArgs[2].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
        pszModName = paArgs[2].u.pszString;
    }

    RTGCUINTPTR uSubtrahend = 0;
    if (cArgs >= 4)
    {
        AssertReturn(paArgs[3].enmType == DBGCVAR_TYPE_NUMBER, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
        uSubtrahend = paArgs[3].u.u64Number;
    }

    RTDBGSEGIDX     iModSeg = NIL_RTDBGSEGIDX;
    if (cArgs >= 5)
    {
        AssertReturn(paArgs[4].enmType == DBGCVAR_TYPE_NUMBER, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
        iModSeg = (RTDBGSEGIDX)paArgs[4].u.u64Number;
        if (    iModSeg != paArgs[4].u.u64Number
            ||  iModSeg > RTDBGSEGIDX_LAST)
            return DBGCCmdHlpPrintf(pCmdHlp, "Segment index out of range: %Dv; range={0..%#x}\n", &paArgs[1], RTDBGSEGIDX_LAST);
    }

    /*
     * Try create a module for it.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    rc = DBGFR3AsLoadMap(pUVM, pDbgc->hDbgAs, pszFilename, pszModName, &ModAddress, NIL_RTDBGSEGIDX, uSubtrahend, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3AsLoadMap(,,'%s','%s',%Dv,)\n",
                                   pszFilename, pszModName, &paArgs[1]);

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadseg' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadSeg(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs >= 3
                 &&  cArgs <= 4
                 &&  paArgs[0].enmType == DBGCVAR_TYPE_STRING
                 &&  DBGCVAR_ISPOINTER(paArgs[1].enmType)
                 &&  paArgs[2].enmType == DBGCVAR_TYPE_NUMBER,
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

    const char     *pszFilename = paArgs[0].u.pszString;

    DBGFADDRESS     ModAddress;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[1], &ModAddress);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "pfnVarToDbgfAddr: %Dv\n", &paArgs[1]);

    RTDBGSEGIDX     iModSeg     = (RTDBGSEGIDX)paArgs[2].u.u64Number;
    if (    iModSeg != paArgs[2].u.u64Number
        ||  iModSeg > RTDBGSEGIDX_LAST)
        return DBGCCmdHlpPrintf(pCmdHlp, "Segment index out of range: %Dv; range={0..%#x}\n", &paArgs[1], RTDBGSEGIDX_LAST);

    const char     *pszModName  = NULL;
    if (cArgs >= 4)
    {
        AssertReturn(paArgs[3].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
        pszModName = paArgs[3].u.pszString;
    }

    /*
     * Call the debug info manager about this loading.
     */
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    rc = DBGFR3AsLoadImage(pUVM, pDbgc->hDbgAs, pszFilename, pszModName, RTLDRARCH_WHATEVER,
                           &ModAddress, iModSeg, RTDBGASLINK_FLAGS_REPLACE);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3ModuleLoadImage(,,'%s','%s',%Dv,,)\n",
                                   pszFilename, pszModName, &paArgs[1]);

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'unload' command.}
 */
static DECLCALLBACK(int) dbgcCmdUnload(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs >= 1
                 &&  paArgs[0].enmType == DBGCVAR_TYPE_STRING,
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);
    for (unsigned i = 0; i < cArgs; i++)
    {
        AssertReturn(paArgs[i].enmType == DBGCVAR_TYPE_STRING, VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

        int rc = DBGFR3AsUnlinkModuleByName(pUVM, pDbgc->hDbgAs, paArgs[i].u.pszString);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGFR3AsUnlinkModuleByName(,,'%s')\n", paArgs[i].u.pszString);
    }

    NOREF(pCmd);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'set' command.}
 */
static DECLCALLBACK(int) dbgcCmdSet(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /* parse sanity check. */
    AssertMsg(paArgs[0].enmType == DBGCVAR_TYPE_STRING, ("expected string not %d as first arg!\n", paArgs[0].enmType));
    if (paArgs[0].enmType != DBGCVAR_TYPE_STRING)
        return VERR_DBGC_PARSE_INCORRECT_ARG_TYPE;


    /*
     * A variable must start with an alpha chars and only contain alpha numerical chars.
     */
    const char *pszVar = paArgs[0].u.pszString;
    if (!RT_C_IS_ALPHA(*pszVar) || *pszVar == '_')
        return DBGCCmdHlpPrintf(pCmdHlp,
                                "syntax error: Invalid variable name '%s'. Variable names must match regex '[_a-zA-Z][_a-zA-Z0-9*'!",
                                paArgs[0].u.pszString);

    while (RT_C_IS_ALNUM(*pszVar) || *pszVar == '_')
        pszVar++;
    if (*pszVar)
        return DBGCCmdHlpPrintf(pCmdHlp,
                                "syntax error: Invalid variable name '%s'. Variable names must match regex '[_a-zA-Z][_a-zA-Z0-9*]'!",
                                paArgs[0].u.pszString);


    /*
     * Calc variable size.
     */
    size_t  cbVar = (size_t)paArgs[0].u64Range + sizeof(DBGCNAMEDVAR);
    if (paArgs[1].enmType == DBGCVAR_TYPE_STRING)
        cbVar += 1 + (size_t)paArgs[1].u64Range;

    /*
     * Look for existing one.
     */
    pszVar = paArgs[0].u.pszString;
    for (unsigned iVar = 0; iVar < pDbgc->cVars; iVar++)
    {
        if (!strcmp(pszVar, pDbgc->papVars[iVar]->szName))
        {
            /*
             * Update existing variable.
             */
            void *pv = RTMemRealloc(pDbgc->papVars[iVar], cbVar);
            if (!pv)
                return VERR_DBGC_PARSE_NO_MEMORY;
            PDBGCNAMEDVAR pVar = pDbgc->papVars[iVar] = (PDBGCNAMEDVAR)pv;

            pVar->Var = paArgs[1];
            memcpy(pVar->szName, paArgs[0].u.pszString, (size_t)paArgs[0].u64Range + 1);
            if (paArgs[1].enmType == DBGCVAR_TYPE_STRING)
                pVar->Var.u.pszString = (char *)memcpy(&pVar->szName[paArgs[0].u64Range + 1], paArgs[1].u.pszString, (size_t)paArgs[1].u64Range + 1);
            return 0;
        }
    }

    /*
     * Allocate another.
     */
    PDBGCNAMEDVAR pVar = (PDBGCNAMEDVAR)RTMemAlloc(cbVar);

    pVar->Var = paArgs[1];
    memcpy(pVar->szName, pszVar, (size_t)paArgs[0].u64Range + 1);
    if (paArgs[1].enmType == DBGCVAR_TYPE_STRING)
        pVar->Var.u.pszString = (char *)memcpy(&pVar->szName[paArgs[0].u64Range + 1], paArgs[1].u.pszString, (size_t)paArgs[1].u64Range + 1);

    /* need to reallocate the pointer array too? */
    if (!(pDbgc->cVars % 0x20))
    {
        void *pv = RTMemRealloc(pDbgc->papVars, (pDbgc->cVars + 0x20) * sizeof(pDbgc->papVars[0]));
        if (!pv)
        {
            RTMemFree(pVar);
            return VERR_DBGC_PARSE_NO_MEMORY;
        }
        pDbgc->papVars = (PDBGCNAMEDVAR *)pv;
    }
    pDbgc->papVars[pDbgc->cVars++] = pVar;

    NOREF(pCmd); NOREF(pUVM); NOREF(cArgs);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'unset' command.}
 */
static DECLCALLBACK(int) dbgcCmdUnset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC   pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    for (unsigned  i = 0; i < cArgs; i++)
        AssertReturn(paArgs[i].enmType == DBGCVAR_TYPE_SYMBOL, VERR_DBGC_PARSE_BUG);

    /*
     * Iterate the variables and unset them.
     */
    for (unsigned iArg = 0; iArg < cArgs; iArg++)
    {
        const char *pszVar = paArgs[iArg].u.pszString;

        /*
         * Look up the variable.
         */
        for (unsigned iVar = 0; iVar < pDbgc->cVars; iVar++)
        {
            if (!strcmp(pszVar, pDbgc->papVars[iVar]->szName))
            {
                /*
                 * Shuffle the array removing this entry.
                 */
                void *pvFree = pDbgc->papVars[iVar];
                if (iVar + 1 < pDbgc->cVars)
                    memmove(&pDbgc->papVars[iVar],
                            &pDbgc->papVars[iVar + 1],
                            (pDbgc->cVars - iVar - 1) * sizeof(pDbgc->papVars[0]));
                pDbgc->papVars[--pDbgc->cVars] = NULL;

                RTMemFree(pvFree);
            }
        } /* lookup */
    } /* arg loop */

    NOREF(pCmd); NOREF(pUVM);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadvars' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadVars(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Don't trust the parser.
     */
    if (    cArgs != 1
        ||  paArgs[0].enmType != DBGCVAR_TYPE_STRING)
    {
        AssertMsgFailed(("Expected one string exactly!\n"));
        return VERR_DBGC_PARSE_INCORRECT_ARG_TYPE;
    }

    /*
     * Iterate the variables and unset them.
     */
    FILE *pFile = fopen(paArgs[0].u.pszString, "r");
    if (pFile)
    {
        char szLine[4096];
        while (fgets(szLine, sizeof(szLine), pFile))
        {
            /* Strip it. */
            char *psz = szLine;
            while (RT_C_IS_BLANK(*psz))
                psz++;
            int i = (int)strlen(psz) - 1;
            while (i >= 0 && RT_C_IS_SPACE(psz[i]))
                psz[i--] ='\0';
            /* Execute it if not comment or empty line. */
            if (    *psz != '\0'
                &&  *psz != '#'
                &&  *psz != ';')
            {
                DBGCCmdHlpPrintf(pCmdHlp, "dbg: set %s", psz);
                pCmdHlp->pfnExec(pCmdHlp, "set %s", psz);
            }
        }
        fclose(pFile);
    }
    else
        return DBGCCmdHlpPrintf(pCmdHlp, "Failed to open file '%s'.\n", paArgs[0].u.pszString);

    NOREF(pCmd); NOREF(pUVM);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'showvars' command.}
 */
static DECLCALLBACK(int) dbgcCmdShowVars(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    for (unsigned iVar = 0; iVar < pDbgc->cVars; iVar++)
    {
        int rc = DBGCCmdHlpPrintf(pCmdHlp, "%-20s ", &pDbgc->papVars[iVar]->szName);
        if (!rc)
            rc = dbgcCmdFormat(pCmd, pCmdHlp, pUVM, &pDbgc->papVars[iVar]->Var, 1);
        if (rc)
            return rc;
    }

    NOREF(paArgs); NOREF(cArgs);
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'sleep' command.}
 */
static DECLCALLBACK(int) dbgcCmdSleep(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF(pCmd, pCmdHlp, pUVM, cArgs);
    /** @todo make this interruptible. For now the command is limited to 30 sec. */
    RTThreadSleep(RT_MIN((RTMSINTERVAL)paArgs[0].u.u64Number, RT_MS_30SEC));
    return 0;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'loadplugin' command.}
 */
static DECLCALLBACK(int) dbgcCmdLoadPlugIn(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF1(pUVM);
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Loop thru the plugin names.
     */
    for (unsigned i = 0; i < cArgs; i++)
    {
        char            szPlugIn[128];
        RTERRINFOSTATIC ErrInfo;
        szPlugIn[0] = '\0';
        int rc = DBGFR3PlugInLoad(pDbgc->pUVM, paArgs[i].u.pszString, szPlugIn, sizeof(szPlugIn), RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
            DBGCCmdHlpPrintf(pCmdHlp, "Loaded plug-in '%s' (%s)\n", szPlugIn, paArgs[i].u.pszString);
        else if (rc == VERR_ALREADY_EXISTS)
            DBGCCmdHlpPrintf(pCmdHlp, "A plug-in named '%s' is already loaded\n", szPlugIn);
        else if (szPlugIn[0])
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3PlugInLoad failed for '%s' ('%s'): %s",
                                    szPlugIn, paArgs[i].u.pszString, ErrInfo.szMsg);
        else
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3PlugInLoad failed for '%s': %s",
                                    paArgs[i].u.pszString, ErrInfo.szMsg);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'unload' command.}
 */
static DECLCALLBACK(int) dbgcCmdUnloadPlugIn(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    RT_NOREF1(pUVM);
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);

    /*
     * Loop thru the given plug-in names.
     */
    for (unsigned i = 0; i < cArgs; i++)
    {
        int rc = DBGFR3PlugInUnload(pDbgc->pUVM, paArgs[i].u.pszString);
        if (RT_SUCCESS(rc))
            DBGCCmdHlpPrintf(pCmdHlp, "Unloaded plug-in '%s'\n", paArgs[i].u.pszString);
        else if (rc == VERR_NOT_FOUND)
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "'%s' was not found\n", paArgs[i].u.pszString);
        else
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3PlugInUnload failed for '%s'", paArgs[i].u.pszString);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'harakiri' command.}
 */
static DECLCALLBACK(int) dbgcCmdHarakiri(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    Log(("dbgcCmdHarakiri\n"));
    for (;;)
        exit(126);
    NOREF(pCmd); NOREF(pCmdHlp); NOREF(pUVM); NOREF(paArgs); NOREF(cArgs);
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'writecore' command.}
 */
static DECLCALLBACK(int) dbgcCmdWriteCore(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    Log(("dbgcCmdWriteCore\n"));

    /*
     * Validate input, lots of paranoia here.
     */
    if (    cArgs != 1
        ||  paArgs[0].enmType != DBGCVAR_TYPE_STRING)
    {
        AssertMsgFailed(("Expected one string exactly!\n"));
        return VERR_DBGC_PARSE_INCORRECT_ARG_TYPE;
    }

    const char *pszDumpPath = paArgs[0].u.pszString;
    if (!pszDumpPath)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Missing file path.\n");

    int rc = DBGFR3CoreWrite(pUVM, pszDumpPath, true /*fReplaceFile*/);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "DBGFR3WriteCore failed. rc=%Rrc\n", rc);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The 'writegstmem' command.}
 */
static DECLCALLBACK(int) dbgcCmdWriteGstMem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    PDBGC pDbgc = DBGC_CMDHLP2DBGC(pCmdHlp);
    LogFunc(("\n"));

    /*
     * Validate the parsing and make sense of the input.
     * This is a mess as usual because we don't trust the parser yet.
     */
    AssertReturn(    cArgs == 2
                 &&  paArgs[0].enmType == DBGCVAR_TYPE_STRING
                 &&  DBGCVAR_ISPOINTER(paArgs[1].enmType),
                 VERR_DBGC_PARSE_INCORRECT_ARG_TYPE);

    const char *pszFile = paArgs[0].u.pszString;
    if (!pszFile)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Missing file path.\n");

    DBGFADDRESS     LoadAddress;
    int rc = pCmdHlp->pfnVarToDbgfAddr(pCmdHlp, &paArgs[1], &LoadAddress);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpVBoxError(pCmdHlp, rc, "pfnVarToDbgfAddr: %Dv\n", &paArgs[1]);

    RTFILE hFile = NIL_RTFILE;
    rc = RTFileOpen(&hFile, pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbFile;
        rc = RTFileQuerySize(hFile, &cbFile);
        if (RT_SUCCESS(rc))
        {
            void *pvBuf = RTMemTmpAlloc(_16K);
            if (RT_LIKELY(pvBuf))
            {
                size_t cbLeft = cbFile;

                while (   cbLeft
                       && RT_SUCCESS(rc))
                {
                    uint64_t cbThisCopy = RT_MIN(cbFile, _16K);

                    rc = RTFileRead(hFile, pvBuf, cbThisCopy, NULL /*pcbRead*/);
                    if (RT_SUCCESS(rc))
                    {
                        rc = DBGFR3MemWrite(pUVM, pDbgc->idCpu, &LoadAddress, pvBuf, cbThisCopy);
                        if (RT_SUCCESS(rc))
                            DBGFR3AddrAdd(&LoadAddress, cbThisCopy);
                        else
                        {
                            DBGCVAR VarCur;
                            rc = DBGCCmdHlpVarFromDbgfAddr(pCmdHlp, &LoadAddress, &VarCur);
                            if (RT_SUCCESS(rc))
                                rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGFR3MemWrite(,,%DV,,%RX64) failed. rc=%Rrc\n", &VarCur, cbThisCopy, rc);
                            else
                                rc = DBGCCmdHlpVBoxError(pCmdHlp, rc, "DBGCCmdHlpVarFromDbgfAddr\n");
                        }
                    }
                    else
                        rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "RTFileRead() failed. rc=%Rrc\n", rc);

                    cbLeft -= cbThisCopy;
                }

                if (RT_SUCCESS(rc))
                    DBGCCmdHlpPrintf(pCmdHlp, "Wrote 0x%RX64 (%RU64) bytes to %Dv\n", cbFile, cbFile, &paArgs[1]);

                RTMemTmpFree(pvBuf);
            }
            else
            {
                rc = VERR_NO_MEMORY;
                rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "RTMemTmpAlloc() failed. rc=%Rrc\n", rc);
            }
        }
        else
            rc = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "RTFileQuerySize() failed. rc=%Rrc\n", rc);

        RTFileClose(hFile);
    }
    else
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "RTFileOpen(,%s,) failed. rc=%Rrc\n", pszFile, rc);

    return rc;
}

