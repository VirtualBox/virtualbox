/* $Id: DBGCFunctions.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Native Functions.
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
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/rand.h>
#include <iprt/string.h>

#include "DBGCInternal.h"



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to head of the list of exteranl functions. */
static PDBGCEXTFUNCS    g_pExtFuncsHead;




/**
 * @callback_method_impl{FNDBGCFUNC, The randu32() function implementation.}
 */
static DECLCALLBACK(int) dbgcFuncRandU32(PCDBGCFUNC pFunc, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, uint32_t cArgs,
                                         PDBGCVAR pResult)
{
    AssertReturn(cArgs == 0, VERR_DBGC_PARSE_BUG);
    uint32_t u32 = RTRandU32();
    DBGCVAR_INIT_NUMBER(pResult, u32);
    NOREF(pFunc); NOREF(pCmdHlp); NOREF(pUVM); NOREF(paArgs);
    return VINF_SUCCESS;
}


/** Functions descriptors for the basic functions. */
const DBGCFUNC  g_aDbgcFuncs[] =
{
    /* pszCmd,      cArgsMin, cArgsMax, paArgDescs,          cArgDescs,               fFlags, pfnHandler        pszSyntax,          ....pszDescription */
    { "randu32",    0,        0,        NULL,                0,                            0, dbgcFuncRandU32,  "",                     "Returns an unsigned 32-bit random number." },
};

/** The number of function descriptions in g_aDbgcFuncs. */
const uint32_t g_cDbgcFuncs = RT_ELEMENTS(g_aDbgcFuncs);


/**
 * Looks up a function.
 *
 * @returns Pointer to the function descriptor on success, NULL if not found.
 * @param   pDbgc               The DBGC instance.
 * @param   pachName            The first charater in the name.
 * @param   cchName             The length of the function name.
 * @param   fExternal           Whether it's an external function.
 */
PCDBGCFUNC dbgcFunctionLookup(PDBGC pDbgc, const char *pachName, size_t cchName, bool fExternal)
{
    if (!fExternal)
    {
        /* emulation first, so commands can be overloaded (info ++). */
        PCDBGCFUNC pFunc = pDbgc->paEmulationFuncs;
        uint32_t   cLeft = pDbgc->cEmulationFuncs;
        while (cLeft-- > 0)
        {
            if (   !strncmp(pachName, pFunc->pszFuncNm, cchName)
                && !pFunc->pszFuncNm[cchName])
                return pFunc;
            pFunc++;
        }

        for (uint32_t iFunc = 0; iFunc < RT_ELEMENTS(g_aDbgcFuncs); iFunc++)
        {
            if (    !strncmp(pachName, g_aDbgcFuncs[iFunc].pszFuncNm, cchName)
                &&  !g_aDbgcFuncs[iFunc].pszFuncNm[cchName])
                return &g_aDbgcFuncs[iFunc];
        }
    }
    else
    {
        DBGCEXTLISTS_LOCK_RD();
        for (PDBGCEXTFUNCS pExtFuncs = g_pExtFuncsHead; pExtFuncs; pExtFuncs = pExtFuncs->pNext)
        {
            for (uint32_t iFunc = 0; iFunc < pExtFuncs->cFuncs; iFunc++)
            {
                if (    !strncmp(pachName, pExtFuncs->paFuncs[iFunc].pszFuncNm, cchName)
                    &&  !pExtFuncs->paFuncs[iFunc].pszFuncNm[cchName])
                    return &pExtFuncs->paFuncs[iFunc];
            }
        }
        DBGCEXTLISTS_UNLOCK_RD();
    }

    return NULL;
}

