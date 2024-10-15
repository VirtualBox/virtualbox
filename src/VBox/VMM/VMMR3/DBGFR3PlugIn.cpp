/* $Id: DBGFR3PlugIn.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Plug-In Support.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/dir.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define DBGF_PLUG_IN_READ_LOCK(pUVM) \
    do { int rcLock = RTCritSectRwEnterShared(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)
#define DBGF_PLUG_IN_READ_UNLOCK(pUVM) \
    do { int rcLock = RTCritSectRwLeaveShared(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)

#define DBGF_PLUG_IN_WRITE_LOCK(pUVM) \
    do { int rcLock = RTCritSectRwEnterExcl(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)
#define DBGF_PLUG_IN_WRITE_UNLOCK(pUVM) \
    do { int rcLock = RTCritSectRwLeaveExcl(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)

/** Max allowed length of a plug-in name (excludes the path and suffix). */
#define DBGFPLUGIN_MAX_NAME     64


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Plug-in tracking record.
 */
typedef struct DBGFPLUGIN
{
    /** Pointer to the next plug-in. */
    struct DBGFPLUGIN  *pNext;
    /** The loader handle.  */
    RTLDRMOD            hLdrMod;
    /** The plug-in entry point. */
    PFNDBGFPLUGIN       pfnEntry;
    /** The name length. */
    uint8_t             cchName;
    /** The plug-in name (variable length).  */
    char                szName[1];
} DBGFPLUGIN;
/** Pointer to plug-in tracking record. */
typedef DBGFPLUGIN *PDBGFPLUGIN;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(void) dbgfPlugInUnloadAll(PUVM pUVM);
static FNDBGFHANDLERINT dbgfR3PlugInInfoList;


/**
 * Internal init routine called by DBGFR3Init().
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 */
int dbgfR3PlugInInit(PUVM pUVM)
{
    return DBGFR3InfoRegisterInternal(pUVM->pVM, "plugins", "Lists the debugger plug-ins.", dbgfR3PlugInInfoList);
}


/**
 * Internal cleanup routine called by DBGFR3Term().
 *
 * @param   pUVM    The user mode VM handle.
 */
void dbgfR3PlugInTerm(PUVM pUVM)
{
    dbgfPlugInUnloadAll(pUVM);
}


/**
 * Extracts the plug-in name from a plug-in specifier that may or may not
 * include path and/or suffix.
 *
 * @returns VBox status code.
 *
 * @param   pszDst      Where to return the name. At least DBGFPLUGIN_MAX_NAME
 *                      worth of buffer space.
 * @param   pszPlugIn   The plug-in module specifier to parse.
 * @param   pErrInfo    Optional error information structure.
 */
static int dbgfPlugInExtractName(char *pszDst, const char *pszPlugIn, PRTERRINFO pErrInfo)
{
    /*
     * Parse out the name stopping at the extension.
     */
    const char *pszName = RTPathFilename(pszPlugIn);
    if (!pszName || !*pszName)
        return VERR_INVALID_NAME;
    if (!RTStrNICmp(pszName, RT_STR_TUPLE(DBGF_PLUG_IN_PREFIX)))
    {
        pszName += sizeof(DBGF_PLUG_IN_PREFIX) - 1;
        if (!*pszName)
            return RTErrInfoSetF(pErrInfo, VERR_INVALID_NAME, "Invalid plug-in name: nothing after the prefix");
    }

    int     ch;
    size_t  cchName = 0;
    while (   (ch = pszName[cchName]) != '\0'
           && ch != '.')
    {
        if (   RT_C_IS_ALPHA(ch)
            || (RT_C_IS_DIGIT(ch) && cchName != 0))
            cchName++;
        else
        {
            if (!RT_C_IS_DIGIT(ch))
                return RTErrInfoSetF(pErrInfo, VERR_INVALID_NAME, "Invalid plug-in name: '%c' is not alphanumeric", ch);
            return RTErrInfoSetF(pErrInfo, VERR_INVALID_NAME,
                                 "Invalid plug-in name: Cannot start with a digit (after the prefix)");
        }
    }

    if (cchName >= DBGFPLUGIN_MAX_NAME)
        return RTErrInfoSetF(pErrInfo, VERR_INVALID_NAME, "Invalid plug-in name: too long (max %u)", DBGFPLUGIN_MAX_NAME);

    /*
     * We're very picky about the extension when present.
     */
    if (   ch == '.'
        && RTStrICmp(&pszName[cchName], RTLdrGetSuff()))
        return RTErrInfoSetF(pErrInfo, VERR_INVALID_NAME,
                             "Invalid plug-in name: Suffix isn't the default dll/so/dylib one (%s): '%s'",
                             RTLdrGetSuff(), &pszName[cchName]);

    /*
     * Copy it.
     */
    memcpy(pszDst, pszName, cchName);
    pszDst[cchName] = '\0';
    return VINF_SUCCESS;
}


/**
 * Locate a loaded plug-in.
 *
 * @returns Pointer to the plug-in tracking structure.
 * @param   pUVM                Pointer to the user-mode VM structure.
 * @param   pszName             The name of the plug-in we're looking for.
 * @param   ppPrev              Where to optionally return the pointer to the
 *                              previous list member.
 */
static PDBGFPLUGIN dbgfR3PlugInLocate(PUVM pUVM, const char *pszName, PDBGFPLUGIN *ppPrev)
{
    PDBGFPLUGIN pPrev = NULL;
    PDBGFPLUGIN pCur  = pUVM->dbgf.s.pPlugInHead;
    while (pCur)
    {
        if (!RTStrICmp(pCur->szName, pszName))
        {
            if (ppPrev)
                *ppPrev = pPrev;
            return pCur;
        }

        /* advance */
        pPrev = pCur;
        pCur  = pCur->pNext;
    }
    return NULL;
}


/**
 * Try load the specified plug-in module.
 *
 * @returns VINF_SUCCESS on success, path error or loader error on failure.
 *
 * @param   pPlugIn     The plug-in tracing record.
 * @param   pszModule   Module name.
 * @param   pErrInfo    Optional error information structure.
 */
static int dbgfR3PlugInTryLoad(PDBGFPLUGIN pPlugIn, const char *pszModule, PRTERRINFO pErrInfo)
{
    /*
     * Load it and try resolve the entry point.
     */
    int rc = SUPR3HardenedVerifyPlugIn(pszModule, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = RTLdrLoadEx(pszModule, &pPlugIn->hLdrMod, RTLDRLOAD_FLAGS_LOCAL, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(pPlugIn->hLdrMod, DBGF_PLUG_IN_ENTRYPOINT, (void **)&pPlugIn->pfnEntry);
        if (RT_SUCCESS(rc))
        {
            LogRel(("DBGF: Loaded Plug-In '%s' (%s)\n", pPlugIn->szName, pszModule));
            return VINF_SUCCESS;
        }

        RTErrInfoSet(pErrInfo, rc, "Failed to locate plug-in entrypoint (" DBGF_PLUG_IN_ENTRYPOINT ")" );
        LogRel(("DBGF: RTLdrGetSymbol('%s', '%s',) -> %Rrc\n", pszModule, DBGF_PLUG_IN_ENTRYPOINT, rc));

        RTLdrClose(pPlugIn->hLdrMod);
        pPlugIn->hLdrMod = NIL_RTLDRMOD;
    }
    return rc;
}


/**
 * RTPathTraverseList callback.
 *
 * @returns See FNRTPATHTRAVERSER.
 *
 * @param   pchPath     See FNRTPATHTRAVERSER.
 * @param   cchPath     See FNRTPATHTRAVERSER.
 * @param   pvUser1     The plug-in specifier.
 * @param   pvUser2     The plug-in tracking record.
 */
static DECLCALLBACK(int) dbgfR3PlugInLoadCallback(const char *pchPath, size_t cchPath, void *pvUser1, void *pvUser2)
{
    PDBGFPLUGIN pPlugIn   = (PDBGFPLUGIN)pvUser1;
    PRTERRINFO  pErrInfo  = (PRTERRINFO)pvUser2;

    /*
     * Join the path and the specified plug-in name, adding prefix and suffix.
     */
    const char  *pszSuff   = RTLdrGetSuff();
    size_t const cchSuff   = strlen(pszSuff);
    size_t const cchModule = cchPath + sizeof(RTPATH_SLASH_STR) + sizeof(DBGF_PLUG_IN_PREFIX) + pPlugIn->cchName + cchSuff + 4;
    char        *pszModule = (char *)alloca(cchModule);
    AssertReturn(pszModule, VERR_TRY_AGAIN);

    memcpy(pszModule, pchPath, cchPath);
    pszModule[cchPath] = '\0';

    int rc = RTPathAppend(pszModule, cchModule, DBGF_PLUG_IN_PREFIX);
    AssertRCReturn(rc, VERR_TRY_AGAIN); /* (This cannot possibly fail, just usual paranoia convention.) */
    size_t const cchWithPrefix = cchPath + strlen(&pszModule[cchPath]); /* (May have added a slash.) */

    Assert(cchWithPrefix + pPlugIn->cchName + cchSuff < cchModule - 4);
    memcpy(mempcpy(&pszModule[cchWithPrefix], pPlugIn->szName, pPlugIn->cchName), pszSuff, cchSuff + 1);

    if (RTPathExists(pszModule))
    {
        rc = dbgfR3PlugInTryLoad(pPlugIn, pszModule, pErrInfo);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
    }

    return VERR_TRY_AGAIN;
}


/**
 * Loads a plug-in.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user-mode VM structure.
 * @param   pszName             The plug-in name.
 * @param   pszMaybeModule      Path to the plug-in, or just the
 *                              plug-in name as specified by the user.  Ignored
 *                              if no path.
 * @param   pErrInfo            Optional error information structure.
 */
static DECLCALLBACK(int) dbgfR3PlugInLoad(PUVM pUVM, const char *pszName, const char *pszMaybeModule, PRTERRINFO pErrInfo)
{
    DBGF_PLUG_IN_WRITE_LOCK(pUVM);

    /*
     * Check if a plug-in by the given name already exists.
     */
    PDBGFPLUGIN pPlugIn = dbgfR3PlugInLocate(pUVM, pszName, NULL);
    if (pPlugIn)
    {
        DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
        return RTErrInfoSetF(pErrInfo, VERR_ALREADY_EXISTS, "A plug-in by the name '%s' already exists", pszName);
    }

    /*
     * Create a module structure and we can pass around via RTPathTraverseList if needed.
     */
    size_t cbName = strlen(pszName) + 1;
    pPlugIn = (PDBGFPLUGIN)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF, RT_UOFFSETOF_DYN(DBGFPLUGIN, szName[cbName]));
    if (RT_UNLIKELY(!pPlugIn))
    {
        DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
        return VERR_NO_MEMORY;
    }
    memcpy(pPlugIn->szName, pszName, cbName);
    pPlugIn->cchName = (uint8_t)cbName - 1;
    Assert(pPlugIn->cchName == cbName - 1);

    /*
     * If the caller specified a path, try load exactly what was specified.
     */
    int rc;
    if (RTPathHavePath(pszMaybeModule))
        rc = dbgfR3PlugInTryLoad(pPlugIn, pszMaybeModule, pErrInfo);
    else
    {
        /*
         * No path specified, search for the plug-in using the canonical
         * module name for it.
         */
        RTErrInfoClear(pErrInfo);

        /* 1. The private architecture directory. */
        char szPath[_4K];
        rc = RTPathAppPrivateArch(szPath, sizeof(szPath));
        if (RT_SUCCESS(rc))
            rc = RTPathTraverseList(szPath, '\0', dbgfR3PlugInLoadCallback, pPlugIn, pErrInfo);
        if (RT_FAILURE_NP(rc))
        {
            /* 2. The config value 'PlugInPath' */
            int rc2 = CFGMR3QueryString(CFGMR3GetChild(CFGMR3GetRootU(pUVM), "/DBGF"), "PlugInPath", szPath, sizeof(szPath));
            if (RT_SUCCESS(rc2))
                rc = RTPathTraverseList(szPath, ';', dbgfR3PlugInLoadCallback, pPlugIn, pErrInfo);
            if (RT_FAILURE_NP(rc))
            {
                /* 3. The VBOXDBG_PLUG_IN_PATH environment variable. */
                rc2 = RTEnvGetEx(RTENV_DEFAULT, "VBOXDBG_PLUG_IN_PATH", szPath, sizeof(szPath), NULL);
                if (RT_SUCCESS(rc2))
                    rc = RTPathTraverseList(szPath, ';', dbgfR3PlugInLoadCallback, pPlugIn, pErrInfo);
            }
        }

        if (rc == VERR_END_OF_STRING)
            rc = VERR_FILE_NOT_FOUND;
        if (pErrInfo && !RTErrInfoIsSet(pErrInfo))
            RTErrInfoSetF(pErrInfo, rc, "Failed to locate '%s'", pPlugIn->szName);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Try initialize it.
         */
        rc = pPlugIn->pfnEntry(DBGFPLUGINOP_INIT, pUVM, VMMR3GetVTable(), VBOX_VERSION);
        if (RT_SUCCESS(rc))
        {
            /*
             * Link it and we're good.
             */
            pPlugIn->pNext = pUVM->dbgf.s.pPlugInHead;
            pUVM->dbgf.s.pPlugInHead = pPlugIn;

            DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
            return VINF_SUCCESS;
        }

        RTErrInfoSet(pErrInfo, rc, "Plug-in init failed");
        LogRel(("DBGF: Plug-in '%s' failed during init: %Rrc\n", pPlugIn->szName, rc));
        RTLdrClose(pPlugIn->hLdrMod);
    }
    MMR3HeapFree(pPlugIn);

    DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
    return rc;
}


/**
 * Load a debugging plug-in.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the module was already loaded.
 * @retval  VINF_BUFFER_OVERFLOW if the actual plug-in name buffer was too small
 *          (the plug-in was still successfully loaded).
 * @param   pUVM        Pointer to the user-mode VM structure.
 * @param   pszPlugIn   The plug-in name.  This may specify the exact path to
 *                      the plug-in module, or it may just specify the core name
 *                      of the plug-in without prefix, suffix and path.
 * @param   pszActual   Buffer to return the actual plug-in name in. Optional.
 *                      This will be returned on VERR_ALREADY_EXSIST too.
 * @param   cbActual    The size of @a pszActual.
 * @param   pErrInfo    Optional error information structure.
 */
VMMR3DECL(int) DBGFR3PlugInLoad(PUVM pUVM, const char *pszPlugIn, char *pszActual, size_t cbActual, PRTERRINFO pErrInfo)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszPlugIn, VERR_INVALID_PARAMETER);

    /*
     * Extract the plug-in name.  Copy it to the return buffer as we'll want to
     * return it in the VERR_ALREADY_EXISTS case too.
     */
    char szName[DBGFPLUGIN_MAX_NAME];
    int rc = dbgfPlugInExtractName(szName, pszPlugIn, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        int rc2 = VINF_SUCCESS;
        if (pszActual)
            rc2 = RTStrCopy(pszActual, cbActual, szName);

        /*
         * Write lock releated DBGF bits and try load it.
         */
        rc = VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfR3PlugInLoad, 4, pUVM, szName, pszPlugIn, pErrInfo);
        if (rc2 != VINF_SUCCESS && RT_SUCCESS(rc))
            rc = VINF_BUFFER_OVERFLOW;
    }

    return rc;
}


/**
 * Load all plug-ins from the architechture private directory of VBox.
 *
 * @param   pUVM    Pointer to the user-mode VM structure.
 */
VMMR3DECL(void) DBGFR3PlugInLoadAll(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN_VOID(pUVM);

    /*
     * Pass it on to EMT(0) if necessary (thanks to DBGFR3Os*).
     */
    if (VMR3GetVMCPUId(pUVM->pVM) != 0)
    {
        VMR3ReqPriorityCallVoidWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)DBGFR3PlugInLoadAll, 1, pUVM);
        return;
    }


    /*
     * Open the architecture specific directory with a filter on our prefix
     * and names including a dot.
     */
    const char *pszSuff = RTLdrGetSuff();
    size_t      cchSuff = strlen(pszSuff);

    char szPath[RTPATH_MAX];
    int rc = RTPathAppPrivateArch(szPath, sizeof(szPath) - cchSuff);
    AssertRCReturnVoid(rc);
    size_t offDir = strlen(szPath);

    rc = RTPathAppend(szPath, sizeof(szPath) - cchSuff, DBGF_PLUG_IN_PREFIX "*");
    AssertRCReturnVoid(rc);
    rc = RTStrCat(szPath, sizeof(szPath), pszSuff);
    AssertRCReturnVoid(rc);

    RTDIR hDir;
    rc = RTDirOpenFiltered(&hDir, szPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Now read it and try load each of the plug-in modules.
         */
        RTDIRENTRY DirEntry;
        while (RT_SUCCESS(RTDirRead(hDir, &DirEntry, NULL)))
        {
            szPath[offDir] = '\0';
            rc = RTPathAppend(szPath, sizeof(szPath), DirEntry.szName);
            if (RT_SUCCESS(rc))
            {
                char szName[DBGFPLUGIN_MAX_NAME];
                rc = dbgfPlugInExtractName(szName, DirEntry.szName, NULL);
                if (RT_SUCCESS(rc))
                {
                    DBGF_PLUG_IN_WRITE_LOCK(pUVM);
                    dbgfR3PlugInLoad(pUVM, szName, szPath, NULL);
                    DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
                }
            }
        }

        RTDirClose(hDir);
    }
}


/**
 * Unloads a plug-in by name (no path, prefix or suffix).
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the specified plug-in wasn't found.
 * @param   pUVM        Pointer to the user-mode VM structure.
 * @param   pszName     The name of the plug-in to unload.
 */
VMMR3DECL(int) DBGFR3PlugInUnload(PUVM pUVM, const char *pszName)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * Pass it on to EMT(0) if necessary (thanks to DBGFR3Os*).
     */
    if (VMR3GetVMCPUId(pUVM->pVM) != 0)
        return VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)DBGFR3PlugInUnload, 2, pUVM, pszName);


    /*
     * Find the plug-in.
     */
    DBGF_PLUG_IN_WRITE_LOCK(pUVM);

    int rc;
    PDBGFPLUGIN pPrevPlugIn;
    PDBGFPLUGIN pPlugIn = dbgfR3PlugInLocate(pUVM, pszName, &pPrevPlugIn);
    if (pPlugIn)
    {
        /*
         * Unlink, terminate, unload and free the plug-in.
         */
        if (pPrevPlugIn)
            pPrevPlugIn->pNext = pPlugIn->pNext;
        else
            pUVM->dbgf.s.pPlugInHead = pPlugIn->pNext;

        pPlugIn->pfnEntry(DBGFPLUGINOP_TERM, pUVM, VMMR3GetVTable(), 0);
        RTLdrClose(pPlugIn->hLdrMod);

        pPlugIn->pfnEntry = NULL;
        pPlugIn->hLdrMod  = NIL_RTLDRMOD;
        MMR3HeapFree(pPlugIn->pNext);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_FOUND;

    DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
    return rc;
}


/**
 * Unload all plug-ins.
 *
 * @param   pUVM    Pointer to the user-mode VM structure.
 */
static DECLCALLBACK(void) dbgfPlugInUnloadAll(PUVM pUVM)
{
    DBGF_PLUG_IN_WRITE_LOCK(pUVM);

    while (pUVM->dbgf.s.pPlugInHead)
    {
        PDBGFPLUGIN pPlugin = pUVM->dbgf.s.pPlugInHead;
        pUVM->dbgf.s.pPlugInHead = pPlugin->pNext;

        pPlugin->pfnEntry(DBGFPLUGINOP_TERM, pUVM, VMMR3GetVTable(), 0);

        int rc2 = RTLdrClose(pPlugin->hLdrMod);
        AssertRC(rc2);

        pPlugin->pfnEntry = NULL;
        pPlugin->hLdrMod  = NIL_RTLDRMOD;
        MMR3HeapFree(pPlugin);
    }

    DBGF_PLUG_IN_WRITE_UNLOCK(pUVM);
}


/**
 * Unloads all plug-ins.
 *
 * @param   pUVM    Pointer to the user-mode VM structure.
 */
VMMR3DECL(void) DBGFR3PlugInUnloadAll(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN_VOID(pUVM);
    /* Thanks to DBGFR3Os, this must be done on EMT(0). */
    VMR3ReqPriorityCallVoidWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfPlugInUnloadAll, 1, pUVM);
}



/**
 * @callback_method_impl{FNDBGFHANDLERINT, The 'plugins' info item.}
 */
static DECLCALLBACK(void) dbgfR3PlugInInfoList(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDBGFPLUGIN pPlugIn = pVM->pUVM->dbgf.s.pPlugInHead;
    RT_NOREF_PV(pszArgs);
    if (pPlugIn)
    {
        pHlp->pfnPrintf(pHlp, "Debugging plug-in%s: %s", pPlugIn->pNext ? "s" : "", pPlugIn->szName);
        while ((pPlugIn = pPlugIn->pNext) != NULL)
            pHlp->pfnPrintf(pHlp, ", %s", pPlugIn->szName);
        pHlp->pfnPrintf(pHlp, "\n");

    }
    else
        pHlp->pfnPrintf(pHlp, "No plug-ins loaded\n");
}

