/* $Id: DBGFAddrSpace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Address Space Management.
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


/** @page pg_dbgf_addr_space     DBGFAddrSpace - Address Space Management
 *
 * What's an address space? It's mainly a convenient way of stuffing
 * module segments and ad-hoc symbols together. It will also help out
 * when the debugger gets extended to deal with user processes later.
 *
 * There are two standard address spaces that will always be present:
 *   - The physical address space.
 *   - The global virtual address space.
 *
 * Additional address spaces will be added and removed at runtime for
 * guest processes. The global virtual address space will be used to
 * track the kernel parts of the OS, or at least the bits of the kernel
 * that is part of all address spaces (mac os x and 4G/4G patched linux).
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/mm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/param.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Address space database node.
 */
typedef struct DBGFASDBNODE
{
    /** The node core for DBGF::AsHandleTree, the key is the address space handle. */
    AVLPVNODECORE   HandleCore;
    /** The node core for DBGF::AsPidTree, the key is the process id. */
    AVLU32NODECORE  PidCore;
    /** The node core for DBGF::AsNameSpace, the string is the address space name. */
    RTSTRSPACECORE  NameCore;

} DBGFASDBNODE;
/** Pointer to an address space database node. */
typedef DBGFASDBNODE *PDBGFASDBNODE;


/**
 * For dbgfR3AsLoadImageOpenData and dbgfR3AsLoadMapOpenData.
 */
typedef struct DBGFR3ASLOADOPENDATA
{
    const char     *pszModName;
    RTGCUINTPTR     uSubtrahend;
    uint32_t        fFlags;
    RTDBGMOD        hMod;
} DBGFR3ASLOADOPENDATA;

#if 0 /* unused */
/**
 * Callback for dbgfR3AsSearchPath and dbgfR3AsSearchEnvPath.
 *
 * @returns VBox status code. If success, then the search is completed.
 * @param   pszFilename     The file name under evaluation.
 * @param   pvUser          The user argument.
 */
typedef int FNDBGFR3ASSEARCHOPEN(const char *pszFilename, void *pvUser);
/** Pointer to a FNDBGFR3ASSEARCHOPEN. */
typedef FNDBGFR3ASSEARCHOPEN *PFNDBGFR3ASSEARCHOPEN;
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Locks the address space database for writing. */
#define DBGF_AS_DB_LOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWRequestWrite((pUVM)->dbgf.s.hAsDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the address space database after writing. */
#define DBGF_AS_DB_UNLOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseWrite((pUVM)->dbgf.s.hAsDbLock); \
        AssertRC(rcSem); \
    } while (0)

/** Locks the address space database for reading. */
#define DBGF_AS_DB_LOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWRequestRead((pUVM)->dbgf.s.hAsDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the address space database after reading. */
#define DBGF_AS_DB_UNLOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseRead((pUVM)->dbgf.s.hAsDbLock); \
        AssertRC(rcSem); \
    } while (0)



/**
 * Initializes the address space parts of DBGF.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 */
int dbgfR3AsInit(PUVM pUVM)
{
    Assert(pUVM->pVM);

    /*
     * Create the semaphore.
     */
    int rc = RTSemRWCreate(&pUVM->dbgf.s.hAsDbLock);
    AssertRCReturn(rc, rc);

    /*
     * Create the debugging config instance and set it up, defaulting to
     * deferred loading in order to keep things fast.
     */
    rc = RTDbgCfgCreate(&pUVM->dbgf.s.hDbgCfg, "VBOXDBG_", true /*fNativePaths*/);
    AssertRCReturn(rc, rc);
    rc = RTDbgCfgChangeUInt(pUVM->dbgf.s.hDbgCfg, RTDBGCFGPROP_FLAGS, RTDBGCFGOP_PREPEND,
                            RTDBGCFG_FLAGS_DEFERRED);
    AssertRCReturn(rc, rc);

    static struct
    {
        RTDBGCFGPROP    enmProp;
        const char     *pszEnvName;
        const char     *pszCfgName;
    } const s_aProps[] =
    {
        { RTDBGCFGPROP_FLAGS,               "VBOXDBG_FLAGS",            "Flags"             },
        { RTDBGCFGPROP_PATH,                "VBOXDBG_PATH",             "Path"              },
        { RTDBGCFGPROP_SUFFIXES,            "VBOXDBG_SUFFIXES",         "Suffixes"          },
        { RTDBGCFGPROP_SRC_PATH,            "VBOXDBG_SRC_PATH",         "SrcPath"           },
    };
    PCFGMNODE pCfgDbgf = CFGMR3GetChild(CFGMR3GetRootU(pUVM), "/DBGF");
    for (unsigned i = 0; i < RT_ELEMENTS(s_aProps); i++)
    {
        char szEnvValue[8192];
        rc = RTEnvGetEx(RTENV_DEFAULT, s_aProps[i].pszEnvName, szEnvValue, sizeof(szEnvValue), NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTDbgCfgChangeString(pUVM->dbgf.s.hDbgCfg, s_aProps[i].enmProp, RTDBGCFGOP_PREPEND, szEnvValue);
            if (RT_FAILURE(rc))
                return VMR3SetError(pUVM, rc, RT_SRC_POS,
                                    "DBGF Config Error: %s=%s -> %Rrc", s_aProps[i].pszEnvName, szEnvValue, rc);
        }
        else if (rc != VERR_ENV_VAR_NOT_FOUND)
            return VMR3SetError(pUVM, rc, RT_SRC_POS,
                                "DBGF Config Error: Error querying env.var. %s: %Rrc", s_aProps[i].pszEnvName, rc);

        char *pszCfgValue;
        rc = CFGMR3QueryStringAllocDef(pCfgDbgf, s_aProps[i].pszCfgName, &pszCfgValue, NULL);
        if (RT_FAILURE(rc))
            return VMR3SetError(pUVM, rc, RT_SRC_POS,
                                "DBGF Config Error: Querying /DBGF/%s -> %Rrc", s_aProps[i].pszCfgName, rc);
        if (pszCfgValue)
        {
            rc = RTDbgCfgChangeString(pUVM->dbgf.s.hDbgCfg, s_aProps[i].enmProp, RTDBGCFGOP_PREPEND, pszCfgValue);
            if (RT_FAILURE(rc))
                return VMR3SetError(pUVM, rc, RT_SRC_POS,
                                    "DBGF Config Error: /DBGF/%s=%s -> %Rrc", s_aProps[i].pszCfgName, pszCfgValue, rc);
            MMR3HeapFree(pszCfgValue);
        }
    }

    /*
     * Prepend the NoArch and VBoxDbgSyms directories to the path.
     */
    char szPath[RTPATH_MAX];
    rc = RTPathAppPrivateNoArch(szPath, sizeof(szPath));
    AssertRCReturn(rc, rc);
#ifdef RT_OS_DARWIN
    rc = RTPathAppend(szPath, sizeof(szPath), "../Resources/VBoxDbgSyms/");
#else
    rc = RTDbgCfgChangeString(pUVM->dbgf.s.hDbgCfg, RTDBGCFGPROP_PATH, RTDBGCFGOP_PREPEND, szPath);
    AssertRCReturn(rc, rc);

    rc = RTPathAppend(szPath, sizeof(szPath), "VBoxDbgSyms/");
#endif
    AssertRCReturn(rc, rc);
    rc = RTDbgCfgChangeString(pUVM->dbgf.s.hDbgCfg, RTDBGCFGPROP_PATH, RTDBGCFGOP_PREPEND, szPath);
    AssertRCReturn(rc, rc);

    /*
     * Create the standard address spaces.
     */
    RTDBGAS hDbgAs;
    rc = RTDbgAsCreate(&hDbgAs, 0, RTGCPTR_MAX, "Global");
    AssertRCReturn(rc, rc);
    rc = DBGFR3AsAdd(pUVM, hDbgAs, NIL_RTPROCESS);
    AssertRCReturn(rc, rc);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_GLOBAL)] = hDbgAs;

    RTDbgAsRetain(hDbgAs);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_KERNEL)] = hDbgAs;

    rc = RTDbgAsCreate(&hDbgAs, 0, RTGCPHYS_MAX, "Physical");
    AssertRCReturn(rc, rc);
    rc = DBGFR3AsAdd(pUVM, hDbgAs, NIL_RTPROCESS);
    AssertRCReturn(rc, rc);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_PHYS)] = hDbgAs;

    rc = RTDbgAsCreate(&hDbgAs, 0, RTRCPTR_MAX, "HyperRawMode");
    AssertRCReturn(rc, rc);
    rc = DBGFR3AsAdd(pUVM, hDbgAs, NIL_RTPROCESS);
    AssertRCReturn(rc, rc);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_RC)] = hDbgAs;
    RTDbgAsRetain(hDbgAs);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_RC_AND_GC_GLOBAL)] = hDbgAs;

    rc = RTDbgAsCreate(&hDbgAs, 0, RTR0PTR_MAX, "HyperRing0");
    AssertRCReturn(rc, rc);
    rc = DBGFR3AsAdd(pUVM, hDbgAs, NIL_RTPROCESS);
    AssertRCReturn(rc, rc);
    pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_R0)] = hDbgAs;

    return VINF_SUCCESS;
}


/**
 * Callback used by dbgfR3AsTerm / RTAvlPVDestroy to release an address space.
 *
 * @returns 0.
 * @param   pNode           The address space database node.
 * @param   pvIgnore        NULL.
 */
static DECLCALLBACK(int) dbgfR3AsTermDestroyNode(PAVLPVNODECORE pNode, void *pvIgnore)
{
    PDBGFASDBNODE pDbNode = (PDBGFASDBNODE)pNode;
    RTDbgAsRelease((RTDBGAS)pDbNode->HandleCore.Key);
    pDbNode->HandleCore.Key = NIL_RTDBGAS;
    /* Don't bother freeing it here as MM will free it soon and MM is much at
       it when doing it wholesale instead of piecemeal. */
    NOREF(pvIgnore);
    return 0;
}


/**
 * Terminates the address space parts of DBGF.
 *
 * @param   pUVM        The user mode VM handle.
 */
void dbgfR3AsTerm(PUVM pUVM)
{
    /*
     * Create the semaphore.
     */
    int rc = RTSemRWDestroy(pUVM->dbgf.s.hAsDbLock);
    AssertRC(rc);
    pUVM->dbgf.s.hAsDbLock = NIL_RTSEMRW;

    /*
     * Release all the address spaces.
     */
    RTAvlPVDestroy(&pUVM->dbgf.s.AsHandleTree, dbgfR3AsTermDestroyNode, NULL);
    for (size_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.ahAsAliases); i++)
    {
        RTDbgAsRelease(pUVM->dbgf.s.ahAsAliases[i]);
        pUVM->dbgf.s.ahAsAliases[i] = NIL_RTDBGAS;
    }

    /*
     * Release the reference to the debugging config.
     */
    rc = RTDbgCfgRelease(pUVM->dbgf.s.hDbgCfg);
    AssertRC(rc);
}


/**
 * Relocates the RC address space.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   offDelta    The relocation delta.
 */
void dbgfR3AsRelocate(PUVM pUVM, RTGCUINTPTR offDelta)
{
    /*
     * We will relocate the raw-mode context modules by offDelta if they have
     * been injected into the DBGF_AS_RC map.
     */
    if (   pUVM->dbgf.s.afAsAliasPopuplated[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_RC)]
        && offDelta != 0)
    {
        RTDBGAS hAs = pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(DBGF_AS_RC)];

        /* Take a snapshot of the modules as we might have overlapping
           addresses between the previous and new mapping. */
        RTDbgAsLockExcl(hAs);
        uint32_t cModules = RTDbgAsModuleCount(hAs);
        if (cModules > 0 && cModules < _4K)
        {
            struct DBGFASRELOCENTRY
            {
                RTDBGMOD    hDbgMod;
                RTRCPTR     uOldAddr;
            } *paEntries = (struct DBGFASRELOCENTRY *)RTMemTmpAllocZ(sizeof(paEntries[0]) * cModules);
            if (paEntries)
            {
                /* Snapshot. */
                for (uint32_t i = 0; i < cModules; i++)
                {
                    paEntries[i].hDbgMod = RTDbgAsModuleByIndex(hAs, i);
                    AssertLogRelMsg(paEntries[i].hDbgMod != NIL_RTDBGMOD, ("iModule=%#x\n", i));

                    RTDBGASMAPINFO  aMappings[1] = { { 0, 0 } };
                    uint32_t        cMappings = 1;
                    int rc = RTDbgAsModuleQueryMapByIndex(hAs, i, &aMappings[0], &cMappings, 0 /*fFlags*/);
                    if (RT_SUCCESS(rc) && cMappings == 1 && aMappings[0].iSeg == NIL_RTDBGSEGIDX)
                        paEntries[i].uOldAddr = (RTRCPTR)aMappings[0].Address;
                    else
                        AssertLogRelMsgFailed(("iModule=%#x rc=%Rrc cMappings=%#x.\n", i, rc, cMappings));
                }

                /* Unlink them. */
                for (uint32_t i = 0; i < cModules; i++)
                {
                    int rc = RTDbgAsModuleUnlink(hAs, paEntries[i].hDbgMod);
                    AssertLogRelMsg(RT_SUCCESS(rc), ("iModule=%#x rc=%Rrc hDbgMod=%p\n", i, rc, paEntries[i].hDbgMod));
                }

                /* Link them at the new locations. */
                for (uint32_t i = 0; i < cModules; i++)
                {
                    RTRCPTR uNewAddr = paEntries[i].uOldAddr + offDelta;
                    int rc = RTDbgAsModuleLink(hAs, paEntries[i].hDbgMod, uNewAddr,
                                               RTDBGASLINK_FLAGS_REPLACE);
                    AssertLogRelMsg(RT_SUCCESS(rc),
                                    ("iModule=%#x rc=%Rrc hDbgMod=%p %RRv -> %RRv\n", i, rc, paEntries[i].hDbgMod,
                                     paEntries[i].uOldAddr, uNewAddr));
                    RTDbgModRelease(paEntries[i].hDbgMod);
                }

                RTMemTmpFree(paEntries);
            }
            else
                AssertLogRelMsgFailed(("No memory for %#x modules.\n", cModules));
        }
        else
            AssertLogRelMsgFailed(("cModules=%#x\n", cModules));
        RTDbgAsUnlockExcl(hAs);
    }
}


/**
 * Gets the IPRT debugging configuration handle (no refs retained).
 *
 * @returns Config handle or NIL_RTDBGCFG.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(RTDBGCFG)     DBGFR3AsGetConfig(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NIL_RTDBGCFG);
    return pUVM->dbgf.s.hDbgCfg;
}


/**
 * Adds the address space to the database.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   hDbgAs      The address space handle. The reference of the caller
 *                      will NOT be consumed.
 * @param   ProcId      The process id or NIL_RTPROCESS.
 */
VMMR3DECL(int) DBGFR3AsAdd(PUVM pUVM, RTDBGAS hDbgAs, RTPROCESS ProcId)
{
    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    const char *pszName = RTDbgAsName(hDbgAs);
    if (!pszName)
        return VERR_INVALID_HANDLE;
    uint32_t cRefs = RTDbgAsRetain(hDbgAs);
    if (cRefs == UINT32_MAX)
        return VERR_INVALID_HANDLE;

    /*
     * Allocate a tracking node.
     */
    int rc = VERR_NO_MEMORY;
    PDBGFASDBNODE pDbNode = (PDBGFASDBNODE)MMR3HeapAllocU(pUVM, MM_TAG_DBGF_AS, sizeof(*pDbNode));
    if (pDbNode)
    {
        pDbNode->HandleCore.Key     = hDbgAs;
        pDbNode->PidCore.Key        = ProcId;
        pDbNode->NameCore.pszString = pszName;
        pDbNode->NameCore.cchString = strlen(pszName);
        DBGF_AS_DB_LOCK_WRITE(pUVM);
        if (RTStrSpaceInsert(&pUVM->dbgf.s.AsNameSpace, &pDbNode->NameCore))
        {
            if (RTAvlPVInsert(&pUVM->dbgf.s.AsHandleTree, &pDbNode->HandleCore))
            {
                DBGF_AS_DB_UNLOCK_WRITE(pUVM);
                return VINF_SUCCESS;
            }

            /* bail out */
            RTStrSpaceRemove(&pUVM->dbgf.s.AsNameSpace, pszName);
        }
        DBGF_AS_DB_UNLOCK_WRITE(pUVM);
        MMR3HeapFree(pDbNode);
    }
    RTDbgAsRelease(hDbgAs);
    return rc;
}


/**
 * Delete an address space from the database.
 *
 * The address space must not be engaged as any of the standard aliases.
 *
 * @returns VBox status code.
 * @retval  VERR_SHARING_VIOLATION if in use as an alias.
 * @retval  VERR_NOT_FOUND if not found in the address space database.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   hDbgAs      The address space handle. Aliases are not allowed.
 */
VMMR3DECL(int) DBGFR3AsDelete(PUVM pUVM, RTDBGAS hDbgAs)
{
    /*
     * Input validation. Retain the address space so it can be released outside
     * the lock as well as validated.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    if (hDbgAs == NIL_RTDBGAS)
        return VINF_SUCCESS;
    uint32_t cRefs = RTDbgAsRetain(hDbgAs);
    if (cRefs == UINT32_MAX)
        return VERR_INVALID_HANDLE;
    RTDbgAsRelease(hDbgAs);

    DBGF_AS_DB_LOCK_WRITE(pUVM);

    /*
     * You cannot delete any of the aliases.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pUVM->dbgf.s.ahAsAliases); i++)
        if (pUVM->dbgf.s.ahAsAliases[i] == hDbgAs)
        {
            DBGF_AS_DB_UNLOCK_WRITE(pUVM);
            return VERR_SHARING_VIOLATION;
        }

    /*
     * Ok, try remove it from the database.
     */
    PDBGFASDBNODE pDbNode = (PDBGFASDBNODE)RTAvlPVRemove(&pUVM->dbgf.s.AsHandleTree, hDbgAs);
    if (!pDbNode)
    {
        DBGF_AS_DB_UNLOCK_WRITE(pUVM);
        return VERR_NOT_FOUND;
    }
    RTStrSpaceRemove(&pUVM->dbgf.s.AsNameSpace, pDbNode->NameCore.pszString);
    if (pDbNode->PidCore.Key != NIL_RTPROCESS)
        RTAvlU32Remove(&pUVM->dbgf.s.AsPidTree, pDbNode->PidCore.Key);

    DBGF_AS_DB_UNLOCK_WRITE(pUVM);

    /*
     * Free the resources.
     */
    RTDbgAsRelease(hDbgAs);
    MMR3HeapFree(pDbNode);

    return VINF_SUCCESS;
}


/**
 * Changes an alias to point to a new address space.
 *
 * Not all the aliases can be changed, currently it's only DBGF_AS_GLOBAL
 * and DBGF_AS_KERNEL.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   hAlias      The alias to change.
 * @param   hAliasFor   The address space hAlias should be an alias for.  This
 *                      can be an alias. The caller's reference to this address
 *                      space will NOT be consumed.
 */
VMMR3DECL(int) DBGFR3AsSetAlias(PUVM pUVM, RTDBGAS hAlias, RTDBGAS hAliasFor)
{
    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertMsgReturn(DBGF_AS_IS_ALIAS(hAlias), ("%p\n", hAlias), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!DBGF_AS_IS_FIXED_ALIAS(hAlias), ("%p\n", hAlias), VERR_INVALID_PARAMETER);
    RTDBGAS hRealAliasFor = DBGFR3AsResolveAndRetain(pUVM, hAliasFor);
    if (hRealAliasFor == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    /*
     * Make sure the handle is already in the database.
     */
    int rc = VERR_NOT_FOUND;
    DBGF_AS_DB_LOCK_WRITE(pUVM);
    if (RTAvlPVGet(&pUVM->dbgf.s.AsHandleTree, hRealAliasFor))
    {
        /*
         * Update the alias table and release the current address space.
         */
        RTDBGAS hAsOld;
        ASMAtomicXchgHandle(&pUVM->dbgf.s.ahAsAliases[DBGF_AS_ALIAS_2_INDEX(hAlias)], hRealAliasFor, &hAsOld);
        uint32_t cRefs = RTDbgAsRelease(hAsOld);
        Assert(cRefs > 0); Assert(cRefs != UINT32_MAX); NOREF(cRefs);
        rc = VINF_SUCCESS;
    }
    else
        RTDbgAsRelease(hRealAliasFor);
    DBGF_AS_DB_UNLOCK_WRITE(pUVM);

    return rc;
}


/**
 * @callback_method_impl{FNPDMR3ENUM}
 */
static DECLCALLBACK(int) dbgfR3AsLazyPopulateR0Callback(PVM pVM, const char *pszFilename, const char *pszName,
                                                        RTUINTPTR ImageBase, size_t cbImage, PDMLDRCTX enmCtx, void *pvArg)
{
    NOREF(pVM); NOREF(cbImage);

    /* Only ring-0 modules. */
    if (enmCtx == PDMLDRCTX_RING_0)
    {
        RTDBGMOD hDbgMod;
        int rc = RTDbgModCreateFromImage(&hDbgMod, pszFilename, pszName, RTLDRARCH_HOST, pVM->pUVM->dbgf.s.hDbgCfg);
        if (RT_SUCCESS(rc))
        {
            rc = RTDbgAsModuleLink((RTDBGAS)pvArg, hDbgMod, ImageBase, 0 /*fFlags*/);
            if (RT_FAILURE(rc))
                LogRel(("DBGF: Failed to link module \"%s\" into DBGF_AS_R0 at %RTptr: %Rrc\n",
                        pszName, ImageBase, rc));
        }
        else
            LogRel(("DBGF: RTDbgModCreateFromImage failed with rc=%Rrc for module \"%s\" (%s)\n",
                    rc, pszName, pszFilename));
    }
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_RAW_MODE_KEEP
/**
 * @callback_method_impl{FNPDMR3ENUM}
 */
static DECLCALLBACK(int) dbgfR3AsLazyPopulateRCCallback(PVM pVM, const char *pszFilename, const char *pszName,
                                                        RTUINTPTR ImageBase, size_t cbImage, PDMLDRCTX enmCtx, void *pvArg)
{
    NOREF(pVM); NOREF(cbImage);

    /* Only raw-mode modules. */
    if (enmCtx == PDMLDRCTX_RAW_MODE)
    {
        RTDBGMOD hDbgMod;
        int rc = RTDbgModCreateFromImage(&hDbgMod, pszFilename, pszName, RTLDRARCH_X86_32, pVM->pUVM->dbgf.s.hDbgCfg);
        if (RT_SUCCESS(rc))
        {
            rc = RTDbgAsModuleLink((RTDBGAS)pvArg, hDbgMod, ImageBase, 0 /*fFlags*/);
            if (RT_FAILURE(rc))
                LogRel(("DBGF: Failed to link module \"%s\" into DBGF_AS_RC at %RTptr: %Rrc\n",
                        pszName, ImageBase, rc));
        }
        else
            LogRel(("DBGF: RTDbgModCreateFromImage failed with rc=%Rrc for module \"%s\" (%s)\n",
                    rc, pszName, pszFilename));
    }
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_RAW_MODE_KEEP */


/**
 * Lazily populates the specified address space.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   hAlias      The alias.
 */
static void dbgfR3AsLazyPopulate(PUVM pUVM, RTDBGAS hAlias)
{
    DBGF_AS_DB_LOCK_WRITE(pUVM);
    uintptr_t iAlias = DBGF_AS_ALIAS_2_INDEX(hAlias);
    if (!pUVM->dbgf.s.afAsAliasPopuplated[iAlias])
    {
        RTDBGAS hDbgAs = pUVM->dbgf.s.ahAsAliases[iAlias];
        if (hAlias == DBGF_AS_R0 && pUVM->pVM)
            PDMR3LdrEnumModules(pUVM->pVM, dbgfR3AsLazyPopulateR0Callback, hDbgAs);
#ifdef VBOX_WITH_RAW_MODE_KEEP /* needs fixing */
        else if (hAlias == DBGF_AS_RC && pUVM->pVM && VM_IS_RAW_MODE_ENABLED(pUVM->pVM))
        {
            LogRel(("DBGF: Lazy init of RC address space\n"));
            PDMR3LdrEnumModules(pUVM->pVM, dbgfR3AsLazyPopulateRCCallback, hDbgAs);
        }
#endif
        else if (hAlias == DBGF_AS_PHYS && pUVM->pVM)
        {
            /** @todo Lazy load pc and vga bios symbols or the EFI stuff. */
        }

        pUVM->dbgf.s.afAsAliasPopuplated[iAlias] = true;
    }
    DBGF_AS_DB_UNLOCK_WRITE(pUVM);
}


/**
 * Resolves the address space handle into a real handle if it's an alias.
 *
 * @returns Real address space handle. NIL_RTDBGAS if invalid handle.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   hAlias      The possibly address space alias.
 *
 * @remarks Doesn't take any locks.
 */
VMMR3DECL(RTDBGAS) DBGFR3AsResolve(PUVM pUVM, RTDBGAS hAlias)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    AssertCompileNS(NIL_RTDBGAS == (RTDBGAS)0);

    uintptr_t   iAlias = DBGF_AS_ALIAS_2_INDEX(hAlias);
    if (iAlias < DBGF_AS_COUNT)
        ASMAtomicReadHandle(&pUVM->dbgf.s.ahAsAliases[iAlias], &hAlias);
    return hAlias;
}


/**
 * Resolves the address space handle into a real handle if it's an alias,
 * and retains whatever it is.
 *
 * @returns Real address space handle. NIL_RTDBGAS if invalid handle.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   hAlias      The possibly address space alias.
 */
VMMR3DECL(RTDBGAS) DBGFR3AsResolveAndRetain(PUVM pUVM, RTDBGAS hAlias)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    AssertCompileNS(NIL_RTDBGAS == (RTDBGAS)0);

    uint32_t    cRefs;
    uintptr_t   iAlias = DBGF_AS_ALIAS_2_INDEX(hAlias);
    if (iAlias < DBGF_AS_COUNT)
    {
        if (DBGF_AS_IS_FIXED_ALIAS(hAlias))
        {
            /* Perform lazy address space population. */
            if (!pUVM->dbgf.s.afAsAliasPopuplated[iAlias])
                dbgfR3AsLazyPopulate(pUVM, hAlias);

            /* Won't ever change, no need to grab the lock. */
            hAlias = pUVM->dbgf.s.ahAsAliases[iAlias];
            cRefs = RTDbgAsRetain(hAlias);
        }
        else
        {
            /* May change, grab the lock so we can read it safely. */
            DBGF_AS_DB_LOCK_READ(pUVM);
            hAlias = pUVM->dbgf.s.ahAsAliases[iAlias];
            cRefs = RTDbgAsRetain(hAlias);
            DBGF_AS_DB_UNLOCK_READ(pUVM);
        }
    }
    else
        /* Not an alias, just retain it. */
        cRefs = RTDbgAsRetain(hAlias);

    return cRefs != UINT32_MAX ? hAlias : NIL_RTDBGAS;
}


/**
 * Query an address space by name.
 *
 * @returns Retained address space handle if found, NIL_RTDBGAS if not.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   pszName     The name.
 */
VMMR3DECL(RTDBGAS) DBGFR3AsQueryByName(PUVM pUVM, const char *pszName)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NIL_RTDBGAS);
    AssertPtrReturn(pszName, NIL_RTDBGAS);
    AssertReturn(*pszName, NIL_RTDBGAS);

    /*
     * Look it up in the string space and retain the result.
     */
    RTDBGAS hDbgAs = NIL_RTDBGAS;
    DBGF_AS_DB_LOCK_READ(pUVM);

    PRTSTRSPACECORE pNode = RTStrSpaceGet(&pUVM->dbgf.s.AsNameSpace, pszName);
    if (pNode)
    {
        PDBGFASDBNODE pDbNode = RT_FROM_MEMBER(pNode, DBGFASDBNODE, NameCore);
        hDbgAs = (RTDBGAS)pDbNode->HandleCore.Key;
        uint32_t cRefs = RTDbgAsRetain(hDbgAs);
        if (RT_UNLIKELY(cRefs == UINT32_MAX))
            hDbgAs = NIL_RTDBGAS;
    }

    DBGF_AS_DB_UNLOCK_READ(pUVM);
    return hDbgAs;
}


/**
 * Query an address space by process ID.
 *
 * @returns Retained address space handle if found, NIL_RTDBGAS if not.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   ProcId      The process ID.
 */
VMMR3DECL(RTDBGAS) DBGFR3AsQueryByPid(PUVM pUVM, RTPROCESS ProcId)
{
    /*
     * Validate the input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NIL_RTDBGAS);
    AssertReturn(ProcId != NIL_RTPROCESS, NIL_RTDBGAS);

    /*
     * Look it up in the PID tree and retain the result.
     */
    RTDBGAS hDbgAs = NIL_RTDBGAS;
    DBGF_AS_DB_LOCK_READ(pUVM);

    PAVLU32NODECORE pNode = RTAvlU32Get(&pUVM->dbgf.s.AsPidTree, ProcId);
    if (pNode)
    {
        PDBGFASDBNODE pDbNode = RT_FROM_MEMBER(pNode, DBGFASDBNODE, PidCore);
        hDbgAs = (RTDBGAS)pDbNode->HandleCore.Key;
        uint32_t cRefs = RTDbgAsRetain(hDbgAs);
        if (RT_UNLIKELY(cRefs == UINT32_MAX))
            hDbgAs = NIL_RTDBGAS;
    }
    DBGF_AS_DB_UNLOCK_READ(pUVM);

    return hDbgAs;
}

#if 0 /* unused */

/**
 * Searches for the file in the path.
 *
 * The file is first tested without any path modification, then we walk the path
 * looking in each directory.
 *
 * @returns VBox status code.
 * @param   pszFilename     The file to search for.
 * @param   pszPath         The search path.
 * @param   pfnOpen         The open callback function.
 * @param   pvUser          User argument for the callback.
 */
static int dbgfR3AsSearchPath(const char *pszFilename, const char *pszPath, PFNDBGFR3ASSEARCHOPEN pfnOpen, void *pvUser)
{
    char szFound[RTPATH_MAX];

    /* Check the filename length. */
    size_t const    cchFilename = strlen(pszFilename);
    if (cchFilename >= sizeof(szFound))
        return VERR_FILENAME_TOO_LONG;
    const char     *pszName = RTPathFilename(pszFilename);
    if (!pszName)
        return VERR_IS_A_DIRECTORY;
    size_t const    cchName = strlen(pszName);

    /*
     * Try default location first.
     */
    memcpy(szFound, pszFilename, cchFilename + 1);
    int rc = pfnOpen(szFound, pvUser);
    if (RT_SUCCESS(rc))
        return rc;

    /*
     * Walk the search path.
     */
    const char *psz = pszPath;
    while (*psz)
    {
        /* Skip leading blanks - no directories with leading spaces, thank you. */
        while (RT_C_IS_BLANK(*psz))
            psz++;

        /* Find the end of this element. */
        const char *pszNext;
        const char *pszEnd = strchr(psz, ';');
        if (!pszEnd)
            pszEnd = pszNext = strchr(psz, '\0');
        else
            pszNext = pszEnd + 1;
        if (pszEnd != psz)
        {
            size_t const cch = pszEnd - psz;
            if (cch + 1 + cchName < sizeof(szFound))
            {
                /** @todo RTPathCompose, RTPathComposeN(). This code isn't right
                 * for 'E:' on DOS systems. It may also create unwanted double slashes. */
                memcpy(szFound, psz, cch);
                szFound[cch] = '/';
                memcpy(szFound + cch + 1, pszName, cchName + 1);
                int rc2 = pfnOpen(szFound, pvUser);
                if (RT_SUCCESS(rc2))
                    return rc2;
                if (    rc2 != rc
                    &&  (   rc == VERR_FILE_NOT_FOUND
                         || rc == VERR_OPEN_FAILED))
                    rc = rc2;
            }
        }

        /* advance */
        psz = pszNext;
    }

    /*
     * Walk the path once again, this time do a depth search.
     */
    /** @todo do a depth search using the specified path. */

    /* failed */
    return rc;
}


/**
 * Same as dbgfR3AsSearchEnv, except that the path is taken from the environment.
 *
 * If the environment variable doesn't exist, the current directory is searched
 * instead.
 *
 * @returns VBox status code.
 * @param   pszFilename     The filename.
 * @param   pszEnvVar       The environment variable name.
 * @param   pfnOpen         The open callback function.
 * @param   pvUser          User argument for the callback.
 */
static int dbgfR3AsSearchEnvPath(const char *pszFilename, const char *pszEnvVar, PFNDBGFR3ASSEARCHOPEN pfnOpen, void *pvUser)
{
    int     rc;
    char   *pszPath = RTEnvDupEx(RTENV_DEFAULT, pszEnvVar);
    if (pszPath)
    {
        rc = dbgfR3AsSearchPath(pszFilename, pszPath, pfnOpen, pvUser);
        RTStrFree(pszPath);
    }
    else
        rc = dbgfR3AsSearchPath(pszFilename, ".", pfnOpen, pvUser);
    return rc;
}


/**
 * Same as dbgfR3AsSearchEnv, except that the path is taken from the DBGF config
 * (CFGM).
 *
 * Nothing is done if the CFGM variable isn't set.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszFilename     The filename.
 * @param   pszCfgValue     The name of the config variable (under /DBGF/).
 * @param   pfnOpen         The open callback function.
 * @param   pvUser          User argument for the callback.
 */
static int dbgfR3AsSearchCfgPath(PUVM pUVM, const char *pszFilename, const char *pszCfgValue,
                                 PFNDBGFR3ASSEARCHOPEN pfnOpen, void *pvUser)
{
    char *pszPath;
    int rc = CFGMR3QueryStringAllocDef(CFGMR3GetChild(CFGMR3GetRootU(pUVM), "/DBGF"), pszCfgValue, &pszPath, NULL);
    if (RT_FAILURE(rc))
        return rc;
    if (!pszPath)
        return VERR_FILE_NOT_FOUND;
    rc = dbgfR3AsSearchPath(pszFilename, pszPath, pfnOpen, pvUser);
    MMR3HeapFree(pszPath);
    return rc;
}

#endif /* unused */


/**
 * Load symbols from an executable module into the specified address space.
 *
 * If an module exist at the specified address it will be replaced by this
 * call, otherwise a new module is created.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space.
 * @param   pszFilename     The filename of the executable module.
 * @param   pszModName      The module name. If NULL, then then the file name
 *                          base is used (no extension or nothing).
 * @param   enmArch         The desired architecture, use RTLDRARCH_WHATEVER if
 *                          it's not relevant or known.
 * @param   pModAddress     The load address of the module.
 * @param   iModSeg         The segment to load, pass NIL_RTDBGSEGIDX to load
 *                          the whole image.
 * @param   fFlags          For DBGFR3AsLinkModule, see RTDBGASLINK_FLAGS_*.
 */
VMMR3DECL(int) DBGFR3AsLoadImage(PUVM pUVM, RTDBGAS hDbgAs, const char *pszFilename, const char *pszModName, RTLDRARCH enmArch,
                                 PCDBGFADDRESS pModAddress, RTDBGSEGIDX iModSeg, uint32_t fFlags)
{
    /*
     * Validate input
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pModAddress), VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTDBGASLINK_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    RTDBGMOD hDbgMod;
    int rc = RTDbgModCreateFromImage(&hDbgMod, pszFilename, pszModName, enmArch, pUVM->dbgf.s.hDbgCfg);
    if (RT_SUCCESS(rc))
    {
        rc = DBGFR3AsLinkModule(pUVM, hRealAS, hDbgMod, pModAddress, iModSeg, fFlags & RTDBGASLINK_FLAGS_VALID_MASK);
        if (RT_FAILURE(rc))
            RTDbgModRelease(hDbgMod);
    }

    RTDbgAsRelease(hRealAS);
    return rc;
}


/**
 * Load symbols from a map file into a module at the specified address space.
 *
 * If an module exist at the specified address it will be replaced by this
 * call, otherwise a new module is created.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space.
 * @param   pszFilename     The map file.
 * @param   pszModName      The module name. If NULL, then then the file name
 *                          base is used (no extension or nothing).
 * @param   pModAddress     The load address of the module.
 * @param   iModSeg         The segment to load, pass NIL_RTDBGSEGIDX to load
 *                          the whole image.
 * @param   uSubtrahend     Value to to subtract from the symbols in the map
 *                          file. This is useful for the linux System.map and
 *                          /proc/kallsyms.
 * @param   fFlags          Flags reserved for future extensions, must be 0.
 */
VMMR3DECL(int) DBGFR3AsLoadMap(PUVM pUVM, RTDBGAS hDbgAs, const char *pszFilename, const char *pszModName,
                               PCDBGFADDRESS pModAddress, RTDBGSEGIDX iModSeg, RTGCUINTPTR uSubtrahend, uint32_t fFlags)
{
    /*
     * Validate input
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pModAddress), VERR_INVALID_PARAMETER);
    AssertReturn(fFlags == 0, VERR_INVALID_PARAMETER);
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    RTDBGMOD hDbgMod;
    int rc = RTDbgModCreateFromMap(&hDbgMod, pszFilename, pszModName, uSubtrahend, pUVM->dbgf.s.hDbgCfg);
    if (RT_SUCCESS(rc))
    {
        rc = DBGFR3AsLinkModule(pUVM, hRealAS, hDbgMod, pModAddress, iModSeg, 0);
        if (RT_FAILURE(rc))
            RTDbgModRelease(hDbgMod);
    }

    RTDbgAsRelease(hRealAS);
    return rc;
}


/**
 * Wrapper around RTDbgAsModuleLink, RTDbgAsModuleLinkSeg and DBGFR3AsResolve.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space handle.
 * @param   hMod            The module handle.
 * @param   pModAddress     The link address.
 * @param   iModSeg         The segment to link, NIL_RTDBGSEGIDX for the entire image.
 * @param   fFlags          Flags to pass to the link functions, see RTDBGASLINK_FLAGS_*.
 */
VMMR3DECL(int) DBGFR3AsLinkModule(PUVM pUVM, RTDBGAS hDbgAs, RTDBGMOD hMod, PCDBGFADDRESS pModAddress,
                                  RTDBGSEGIDX iModSeg, uint32_t fFlags)
{
    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pModAddress), VERR_INVALID_PARAMETER);
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    int rc;
    if (iModSeg == NIL_RTDBGSEGIDX)
        rc = RTDbgAsModuleLink(hRealAS, hMod, pModAddress->FlatPtr, fFlags);
    else
        rc = RTDbgAsModuleLinkSeg(hRealAS, hMod, iModSeg, pModAddress->FlatPtr, fFlags);

    RTDbgAsRelease(hRealAS);
    return rc;
}


/**
 * Wrapper around RTDbgAsModuleByName and RTDbgAsModuleUnlink.
 *
 * Unlinks all mappings matching the given module name.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space handle.
 * @param   pszModName      The name of the module to unlink.
 */
VMMR3DECL(int) DBGFR3AsUnlinkModuleByName(PUVM pUVM, RTDBGAS hDbgAs, const char *pszModName)
{
    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    RTDBGMOD hMod;
    int rc = RTDbgAsModuleByName(hRealAS, pszModName, 0, &hMod);
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            rc = RTDbgAsModuleUnlink(hRealAS, hMod);
            RTDbgModRelease(hMod);
            if (RT_FAILURE(rc))
                break;
            rc = RTDbgAsModuleByName(hRealAS, pszModName, 0, &hMod);
            if (RT_FAILURE_NP(rc))
            {
                if (rc == VERR_NOT_FOUND)
                    rc = VINF_SUCCESS;
                break;
            }
        }
    }

    RTDbgAsRelease(hRealAS);
    return rc;
}


/**
 * Adds the module name to the symbol name.
 *
 * @param   pSymbol         The symbol info (in/out).
 * @param   hMod            The module handle.
 */
static void dbgfR3AsSymbolJoinNames(PRTDBGSYMBOL pSymbol, RTDBGMOD hMod)
{
    /* Figure the lengths, adjust them if the result is too long. */
    const char *pszModName = RTDbgModName(hMod);
    size_t      cchModName = strlen(pszModName);
    size_t      cchSymbol  = strlen(pSymbol->szName);
    if (cchModName + 1 + cchSymbol >= sizeof(pSymbol->szName))
    {
        if (cchModName >= sizeof(pSymbol->szName) / 4)
            cchModName = sizeof(pSymbol->szName) / 4;
        if (cchModName + 1 + cchSymbol >= sizeof(pSymbol->szName))
            cchSymbol = sizeof(pSymbol->szName) - cchModName - 2;
        Assert(cchModName + 1 + cchSymbol < sizeof(pSymbol->szName));
    }

    /* Do the moving and copying. */
    memmove(&pSymbol->szName[cchModName + 1], &pSymbol->szName[0], cchSymbol + 1);
    memcpy(&pSymbol->szName[0], pszModName, cchModName);
    pSymbol->szName[cchModName] = '!';
}


/**
 * Query a symbol by address.
 *
 * The returned symbol is the one we consider closes to the specified address.
 *
 * @returns VBox status code. See RTDbgAsSymbolByAddr.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space handle.
 * @param   pAddress        The address to lookup.
 * @param   fFlags          One of the RTDBGSYMADDR_FLAGS_XXX flags.
 * @param   poffDisp        Where to return the distance between the returned
 *                          symbol and pAddress. Optional.
 * @param   pSymbol         Where to return the symbol information. The returned
 *                          symbol name will be prefixed by the module name as
 *                          far as space allows.
 * @param   phMod           Where to return the module handle. Optional.
 */
VMMR3DECL(int) DBGFR3AsSymbolByAddr(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress, uint32_t fFlags,
                                    PRTGCINTPTR poffDisp, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    /*
     * Implement the special address space aliases the lazy way.
     */
    if (hDbgAs == DBGF_AS_RC_AND_GC_GLOBAL)
    {
        int rc = DBGFR3AsSymbolByAddr(pUVM, DBGF_AS_RC, pAddress, fFlags, poffDisp, pSymbol, phMod);
        if (RT_FAILURE(rc))
            rc = DBGFR3AsSymbolByAddr(pUVM, DBGF_AS_GLOBAL, pAddress, fFlags, poffDisp, pSymbol, phMod);
        return rc;
    }

    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pAddress), VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(poffDisp, VERR_INVALID_POINTER);
    AssertPtrReturn(pSymbol, VERR_INVALID_POINTER);
    AssertPtrNullReturn(phMod, VERR_INVALID_POINTER);
    if (poffDisp)
        *poffDisp = 0;
    if (phMod)
        *phMod = NIL_RTDBGMOD;
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    /*
     * Do the lookup.
     */
    RTDBGMOD hMod;
    int rc = RTDbgAsSymbolByAddr(hRealAS, pAddress->FlatPtr, fFlags, poffDisp, pSymbol, &hMod);
    if (RT_SUCCESS(rc))
    {
        dbgfR3AsSymbolJoinNames(pSymbol, hMod);
        if (!phMod)
            RTDbgModRelease(hMod);
        else
            *phMod = hMod;
    }

    RTDbgAsRelease(hRealAS);
    return rc;
}


/**
 * Convenience function that combines RTDbgSymbolDup and DBGFR3AsSymbolByAddr.
 *
 * @returns Pointer to the symbol on success. This must be free using
 *          RTDbgSymbolFree(). NULL is returned if not found or any error
 *          occurs.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          See DBGFR3AsSymbolByAddr.
 * @param   pAddress        See DBGFR3AsSymbolByAddr.
 * @param   fFlags          See DBGFR3AsSymbolByAddr.
 * @param   poffDisp        See DBGFR3AsSymbolByAddr.
 * @param   phMod           See DBGFR3AsSymbolByAddr.
 */
VMMR3DECL(PRTDBGSYMBOL) DBGFR3AsSymbolByAddrA(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress, uint32_t fFlags,
                                              PRTGCINTPTR poffDisp, PRTDBGMOD phMod)
{
    RTDBGSYMBOL SymInfo;
    int rc = DBGFR3AsSymbolByAddr(pUVM, hDbgAs, pAddress, fFlags, poffDisp, &SymInfo, phMod);
    if (RT_SUCCESS(rc))
        return RTDbgSymbolDup(&SymInfo);
    return NULL;
}


/**
 * Query a symbol by name.
 *
 * The symbol can be prefixed by a module name pattern to scope the search. The
 * pattern is a simple string pattern with '*' and '?' as wild chars. See
 * RTStrSimplePatternMatch().
 *
 * @returns VBox status code. See RTDbgAsSymbolByAddr.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   hDbgAs          The address space handle.
 * @param   pszSymbol       The symbol to search for, maybe prefixed by a
 *                          module pattern.
 * @param   pSymbol         Where to return the symbol information.
 *                          The returned symbol name will be prefixed by
 *                          the module name as far as space allows.
 * @param   phMod           Where to return the module handle. Optional.
 */
VMMR3DECL(int) DBGFR3AsSymbolByName(PUVM pUVM, RTDBGAS hDbgAs, const char *pszSymbol,
                                    PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    /*
     * Implement the special address space aliases the lazy way.
     */
    if (hDbgAs == DBGF_AS_RC_AND_GC_GLOBAL)
    {
        int rc = DBGFR3AsSymbolByName(pUVM, DBGF_AS_RC, pszSymbol, pSymbol, phMod);
        if (RT_FAILURE(rc))
            rc = DBGFR3AsSymbolByName(pUVM, DBGF_AS_GLOBAL, pszSymbol, pSymbol, phMod);
        return rc;
    }

    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pSymbol, VERR_INVALID_POINTER);
    AssertPtrNullReturn(phMod, VERR_INVALID_POINTER);
    if (phMod)
        *phMod = NIL_RTDBGMOD;
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;


    /*
     * Do the lookup.
     */
    RTDBGMOD hMod;
    int rc = RTDbgAsSymbolByName(hRealAS, pszSymbol, pSymbol, &hMod);
    if (RT_SUCCESS(rc))
    {
        dbgfR3AsSymbolJoinNames(pSymbol, hMod);
        if (!phMod)
            RTDbgModRelease(hMod);
    }

    RTDbgAsRelease(hRealAS);
    return rc;
}


VMMR3DECL(int)          DBGFR3AsLineByAddr(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress,
                                           PRTGCINTPTR poffDisp, PRTDBGLINE pLine, PRTDBGMOD phMod)
{
    /*
     * Implement the special address space aliases the lazy way.
     */
    if (hDbgAs == DBGF_AS_RC_AND_GC_GLOBAL)
    {
        int rc = DBGFR3AsLineByAddr(pUVM, DBGF_AS_RC, pAddress, poffDisp, pLine, phMod);
        if (RT_FAILURE(rc))
            rc = DBGFR3AsLineByAddr(pUVM, DBGF_AS_GLOBAL, pAddress, poffDisp, pLine, phMod);
        return rc;
    }

    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(DBGFR3AddrIsValid(pUVM, pAddress), VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(poffDisp, VERR_INVALID_POINTER);
    AssertPtrReturn(pLine, VERR_INVALID_POINTER);
    AssertPtrNullReturn(phMod, VERR_INVALID_POINTER);
    if (poffDisp)
        *poffDisp = 0;
    if (phMod)
        *phMod = NIL_RTDBGMOD;
    RTDBGAS hRealAS = DBGFR3AsResolveAndRetain(pUVM, hDbgAs);
    if (hRealAS == NIL_RTDBGAS)
        return VERR_INVALID_HANDLE;

    /*
     * Do the lookup.
     */
    int rc = RTDbgAsLineByAddr(hRealAS, pAddress->FlatPtr, poffDisp, pLine, phMod);

    RTDbgAsRelease(hRealAS);
    return rc;
}


VMMR3DECL(PRTDBGLINE)   DBGFR3AsLineByAddrA(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress,
                                            PRTGCINTPTR poffDisp, PRTDBGMOD phMod)
{
    RTDBGLINE Line;
    int rc = DBGFR3AsLineByAddr(pUVM, hDbgAs, pAddress, poffDisp, &Line, phMod);
    if (RT_SUCCESS(rc))
        return RTDbgLineDup(&Line);
    return NULL;
}

