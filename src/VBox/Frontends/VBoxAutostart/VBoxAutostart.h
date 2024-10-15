/* $Id: VBoxAutostart.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxAutostart_VBoxAutostart_h
#define VBOX_INCLUDED_SRC_VBoxAutostart_VBoxAutostart_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/getopt.h>
#include <iprt/types.h>

#include <VBox/cdefs.h>
#include <VBox/types.h>

#include <VBox/com/com.h>
#include <VBox/com/VirtualBox.h>

/*******************************************************************************
*   Constants And Macros, Structures and Typedefs                              *
*******************************************************************************/

/**
 * Config AST node types.
 */
typedef enum CFGASTNODETYPE
{
    /** Invalid. */
    CFGASTNODETYPE_INVALID = 0,
    /** Key/Value pair. */
    CFGASTNODETYPE_KEYVALUE,
    /** Compound type. */
    CFGASTNODETYPE_COMPOUND,
    /** List type. */
    CFGASTNODETYPE_LIST,
    /** 32bit hack. */
    CFGASTNODETYPE_32BIT_HACK = 0x7fffffff
} CFGASTNODETYPE;
/** Pointer to a config AST node type. */
typedef CFGASTNODETYPE *PCFGASTNODETYPE;
/** Pointer to a const config AST node type. */
typedef const CFGASTNODETYPE *PCCFGASTNODETYPE;

/**
 * Config AST.
 */
typedef struct CFGAST
{
    /** AST node type. */
    CFGASTNODETYPE        enmType;
    /** Key or scope id. */
    char                 *pszKey;
    /** Type dependent data. */
    union
    {
        /** Key value pair. */
        struct
        {
            /** Number of characters in the value - excluding terminator. */
            size_t        cchValue;
            /** Value string - variable in size. */
            char          aszValue[1];
        } KeyValue;
        /** Compound type. */
        struct
        {
            /** Number of AST node entries in the array. */
            unsigned       cAstNodes;
            /** AST node array - variable in size. */
            struct CFGAST *apAstNodes[1];
        } Compound;
        /** List type. */
        struct
        {
            /** Number of entries in the list. */
            unsigned       cListEntries;
            /** Array of list entries - variable in size. */
            char          *apszEntries[1];
        } List;
    } u;
} CFGAST, *PCFGAST;

/** Flag whether we are in verbose logging mode. */
extern bool                      g_fVerbose;
/** Handle to the VirtualBox interface. */
extern ComPtr<IVirtualBox>       g_pVirtualBox;
/** Handle to the session interface. */
extern ComPtr<ISession>          g_pSession;
/** handle to the VirtualBox interface. */
extern ComPtr<IVirtualBoxClient> g_pVirtualBoxClient;
/**
 * System log type.
 */
typedef enum AUTOSTARTLOGTYPE
{
    /** Invalid log type. */
    AUTOSTARTLOGTYPE_INVALID = 0,
    /** Log info message. */
    AUTOSTARTLOGTYPE_INFO,
    /** Log error message. */
    AUTOSTARTLOGTYPE_ERROR,
    /** Log warning message. */
    AUTOSTARTLOGTYPE_WARNING,
    /** Log verbose message, only if verbose mode is activated. */
    AUTOSTARTLOGTYPE_VERBOSE,
    /** Famous 32bit hack. */
    AUTOSTARTLOGTYPE_32BIT_HACK = 0x7fffffff
} AUTOSTARTLOGTYPE;

/**
 * Prints the service header header (product name, version, ++) to stdout.
 */
DECLHIDDEN(void) autostartSvcShowHeader(void);

/**
 * Prints the service version information header to stdout.
 *
 * @param   fBrief            Whether to show brief information or not.
 */
DECLHIDDEN(void) autostartSvcShowVersion(bool fBrief);

/**
 * Log messages to the system and release log.
 *
 * @param   pszMsg            Message to log.
 * @param   enmLogType        Log type to use.
 */
DECLHIDDEN(void) autostartSvcOsLogStr(const char *pszMsg, AUTOSTARTLOGTYPE enmLogType);

/**
 * Print out progress on the console.
 *
 * This runs the main event queue every now and then to prevent piling up
 * unhandled things (which doesn't cause real problems, just makes things
 * react a little slower than in the ideal case).
 */
DECLHIDDEN(HRESULT) showProgress(ComPtr<IProgress> progress);

/**
 * Converts the machine state to a human readable string.
 *
 * @returns Pointer to the human readable state.
 * @param   enmMachineState    Machine state to convert.
 * @param   fShort             Flag whether to return a short form.
 */
DECLHIDDEN(const char *) machineStateToName(MachineState_T enmMachineState, bool fShort);

/**
 * Parse the given configuration file and return the interesting config parameters.
 *
 * @returns VBox status code.
 * @param   pszFilename    The config file to parse.
 * @param   ppCfgAst       Where to store the pointer to the root AST node on success.
 */
DECLHIDDEN(int) autostartParseConfig(const char *pszFilename, PCFGAST *ppCfgAst);

/**
 * Destroys the config AST and frees all resources.
 *
 * @param   pCfgAst        The config AST.
 */
DECLHIDDEN(void) autostartConfigAstDestroy(PCFGAST pCfgAst);

/**
 * Return the config AST node with the given name or NULL if it doesn't exist.
 *
 * @returns Matching config AST node for the given name or NULL if not found.
 * @param   pCfgAst    The config ASt to search.
 * @param   pszName    The name to search for.
 */
DECLHIDDEN(PCFGAST) autostartConfigAstGetByName(PCFGAST pCfgAst, const char *pszName);

/**
 * Main routine for the autostart daemon.
 *
 * @returns VBox status code.
 * @param   pCfgAst        Config AST for the startup part of the autostart daemon.
 */
DECLHIDDEN(int) autostartStartMain(PCFGAST pCfgAst);

/**
 * Main routine for the autostart daemon when stopping virtual machines
 * during system shutdown.
 *
 * @returns VBox status code.
 * @param   pCfgAst        Config AST for the shutdown part of the autostart daemon.
 */
DECLHIDDEN(int) autostartStopMain(PCFGAST pCfgAst);

/**
 * Logs a verbose message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   cVerbosity  Verbosity level when logging should happen.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogVerboseV(unsigned cVerbosity, const char *pszFormat, va_list va);

/**
 * Logs a verbose message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   cVerbosity  Verbosity level when logging should happen.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogVerbose(unsigned cVerbosity, const char *pszFormat, ...);

/**
 * Logs a warning message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogWarningV(const char *pszFormat, va_list va);

/**
 * Logs a warning message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogWarning(const char *pszFormat, ...);

/**
 * Logs a info message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogInfoV(const char *pszFormat, va_list va);

/**
 * Logs a info message to the appropriate system log and stdout + release log (if configured).
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(void) autostartSvcLogInfo(const char *pszFormat, ...);

/**
 * Logs the message to the appropriate system log and stderr + release log (if configured).
 *
 * In debug builds this will also put it in the debug log.
 *
 * @returns VBox status code.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(int) autostartSvcLogErrorV(const char *pszFormat, va_list va);

/**
 * Logs the message to the appropriate system log and stderr + release log (if configured).
 *
 * In debug builds this will also put it in the debug log.
 *
 * @returns VBox status code.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(int) autostartSvcLogError(const char *pszFormat, ...);

/**
 * Logs the message to the appropriate system log.
 *
 * In debug builds this will also put it in the debug log.
 *
 * @returns VBox status code specified by \a rc.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 *
 * @note    Convenience function to return directly with the specified \a rc.
 */
DECLHIDDEN(int) autostartSvcLogErrorRcV(int rc, const char *pszFormat, va_list va);

/**
 * Logs the error message to the appropriate system log.
 *
 * In debug builds this will also put it in the debug log.
 *
 * @returns VBox status code specified by \a rc.
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 *
 * @note    Convenience function to return directly with the specified \a rc.
 */
DECLHIDDEN(int) autostartSvcLogErrorRc(int rc, const char *pszFormat, ...);

/**
 * Deals with RTGetOpt failure, bitching in the system log.
 *
 * @returns VBox status code specified by \a rc.
 * @param   pszAction       The action name.
 * @param   rc              The RTGetOpt return value.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 * @param   pValue          The value returned by RTGetOpt.
 */
DECLHIDDEN(int) autostartSvcLogGetOptError(const char *pszAction, int rc, int argc, char **argv, int iArg, PCRTGETOPTUNION pValue);

/**
 * Bitch about too many arguments (after RTGetOpt stops) in the system log.
 *
 * @returns VERR_INVALID_PARAMETER
 * @param   pszAction       The action name.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 */
DECLHIDDEN(int) autostartSvcLogTooManyArgsError(const char *pszAction, int argc, char **argv, int iArg);

/**
 * Prints an error message to the screen.
 *
 * @returns RTEXITCODE
 * @param   pszFormat   The message format string.
 * @param   va          Format arguments.
 */
DECLHIDDEN(RTEXITCODE) autostartSvcDisplayErrorV(const char *pszFormat, va_list va);

/**
 * Prints an error message to the screen.
 *
 * @returns RTEXITCODE
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
DECLHIDDEN(RTEXITCODE) autostartSvcDisplayError(const char *pszFormat, ...);

/**
 * Deals with RTGetOpt failure, i.e. an syntax error.
 *
 * @returns RTEXITCODE_SYNTAX
 * @param   pszAction       The action name.
 * @param   rc              The RTGetOpt return value.
 * @param   pValue          The value returned by RTGetOpt.
 */
DECLHIDDEN(RTEXITCODE) autostartSvcDisplayGetOptError(const char *pszAction, int rc, PCRTGETOPTUNION pValue);

/**
 * Starts the autostart environment by initializing all needed (global) objects.
 *
 * @returns VBox status code.
 *
 * @note    This currently does NOT support multiple instances, be aware of this!
 */
DECLHIDDEN(int) autostartSetup(void);

/**
 * Stops the autostart environment.
 *
 * @note    This currently does NOT support multiple instances, be aware of this!
 */
DECLHIDDEN(void) autostartShutdown(void);

#endif /* !VBOX_INCLUDED_SRC_VBoxAutostart_VBoxAutostart_h */
